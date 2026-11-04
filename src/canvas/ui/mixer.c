/*
        The mixer, a window of the sound server's volumes in two pages (the kit's segmented tabs along its top): Playback, the
        output device first and then a strip for every program that has a sound, and Recording, the microphone programs are
        given first and then a strip for every program that records from one (a program that records what plays is none);
        the kit's strips, ui.c: a disc, its name, its volume, a level meter beside a vertical slider, a mute button. It says
        what each strip is, hands the strips its events and acts on the one that changed; the sliders,
        the keys, the wheel, the meters' fall and hold, the scrolling when there are more strips than room and the
        mirroring of a window that reads from the right are the kit's.

        What it shows is the sound server's page of memory (src/audio/state.c, read without a call: the page is mapped
        read-only and copied whole), at the pace the page is written while anything plays and twice a second when all is quiet.
        What it changes goes the other way over the server's socket (src/audio/control.c): a program's volume or mute is
        the program's streams' (the streams of one program, by the name it gave, are one strip), and the device's volume
        and mute are the sink's, which the server keeps in /root/audio.volume and /root/audio.mute for the next boot (the
        microphone's, on the Recording page, are the source's, in /root/audio.source.volume and .mute). A
        strip the person is moving is the person's for a moment whatever the page says meanwhile (ui.c), and what is
        changed is sent once a pass of the loop however many events came, so a drag costs a request a frame.

        With the server not running, or no page, the window says so, and the strips come when the page does. On the Recording
        page, with recording turned off (/root/audio.capture, `moonwater audio capture`), it says so and has a button, Allow,
        that turns it on; with no microphone it says that.

            mixer [-q [-a]] [-i] [-f FONT.ttf] [-b BOLD.ttf] [-p PAGE] [-s SOCKET]

        One window: a second start finds the first and lifts it. Escape closes it.

        -i starts it on the Recording page.

        -q is the tray's popup (a press on the tray's volume cell, the machine script starts it): the device's strip alone, in a window
        that is put away by a press anywhere but on it, by losing the keys and by Escape, with a row under it, Mixer, that starts the
        whole mixer and puts the popup away. With -i (the tray's microphone cell: recording is never silent) it is the Recording page's
        strips instead, the microphone and the programs that record, up to four wide. -a opens it beside the cell that was pressed (the compositor leaves its rectangle, the edge
        its band is along and its screen in the table, as it does for the launcher's button), above a bottom band, below a top one and
        beside a side one, kept on the screen.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_MIXER
#define MOONWATER_CANVAS_MIXER

#include "../../audio/state.c"
#include "../../audio/control.c"

#define MIXER_WIDTH 640   // logical pixels, times the scale
#define MIXER_HEIGHT 400
#define MIXER_SOCKET "/run/moonwater/pulse/native"
#define MIXER_POLL_NS 16000000ll       // the page is looked at this often while anything plays,
#define MIXER_QUIET_NS 100000000ll     // this often when nothing does,
#define MIXER_HIDDEN_NS 1000000000ll   // and this when the window is put away
#define MIXER_REMAP_NS 2000000000ll    // a page that has not moved for this long is looked for again by its name
#define MIXER_MEMBERS 16               // the streams of one program that are one strip
#define MIXER_VOLUME 65536u
#define MIXER_NONE 0xffffffffu
#define MIXER_QUICK_WIDTH(n) (2 * UI_STRIP_PAD + (n) * UI_STRIP_W + ((n) > 1 ? UI_STRIP_GAP : 0)) // the popup: n strips wide,
#define MIXER_QUICK_W MIXER_QUICK_WIDTH(1)                                                      // one for the output,
#define MIXER_QUICK_H 380                                                                       // this tall, the last of it the Mixer row
#define MIXER_TABS (UI_TABS_H + 16)                                                             // the tabs of the full window and their room

struct mixer_group
{
        unsigned int member[MIXER_MEMBERS];
        int count;
};

// Everything that is large: one mapping, not the image's bss.
struct mixer_big
{
        struct ui_strips strips;
        struct mixer_group group[UI_STRIPS_MOST];
        struct audio_state copy;
        struct ctl ctl;
        unsigned char send[UI_STRIPS_MOST]; // what is to be sent for a strip: 1 its volume, 2 its mute
        char sink[64];                      // the output device as programs name it, for its volume
        char source[64];                    // and the microphone
        int device;                         // 1 when strip 0 is the device's: the programs' groups follow it
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct mixer_big address_to big;
        const volatile struct audio_state address_to page;
        const char address_to page_path;
        const char address_to socket_path;
        int width, height, shown;
        unsigned int ground;
        b32 focused, dirty, have, connected, listed, recording, capture_off;
        b64 next_poll, seen_at;
        // The tray's popup (-q), and where it opens (-a).
        b32 quick, beside;
        struct ui_anchor anchor; // the panel's button it opens beside (-a): the kit's
        struct ui_buttons row;   // its Mixer row
        struct ui_buttons allow; // Allow, on the Recording page with recording turned off
        struct ui_control tabs;  // the two pages
        const char address_to capture_path;
        string_address font;     // the face this is drawn in (-f), which the mixer it starts is drawn in too
} mixer;

/* ---- the page ---- */

static fn mixer_unmap(void)
{
        if (mixer.page)
                compositor_call(syscall(munmap), (positive)mixer.page, (positive)((sizeof(struct audio_state) + 4095) & ~4095ul), 0, 0, 0, 0);
        mixer.page = null;
}

// The page of the server mapped read-only: false when the server has none (it is not running, or not yet).
static b32 mixer_map(void)
{
        long fd, size, got;

        if (mixer.page)
                return true;
        fd = compositor_call(syscall(openat), (positive)AT_FDCWD, (positive)mixer.page_path, 0x80000, 0, 0, 0);
        if (fd < 0)
                return false;
        size = compositor_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0);
        got = size < (long)sizeof(struct audio_state) ? -1
              : compositor_call(syscall(mmap), 0, (positive)((sizeof(struct audio_state) + 4095) & ~4095ul), 1, 1, (positive)fd, 0);
        compositor_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (got < 0 && got > -4096)
                return false;
        mixer.page = (const volatile struct audio_state address_to)got;
        return true;
}

// A program's name on a strip: what it called itself, else what it plays, else its icon's name, else a question mark.
static const char address_to mixer_name(const struct audio_state_stream address_to stream)
{
        return stream->app[0] ? stream->app : stream->media[0] ? stream->media : stream->icon[0] ? stream->icon : "?";
}

// Whether two streams are one program's: the same name from the same user; a stream with no name is its connection's alone.
static b32 mixer_same(const struct audio_state_stream address_to a, const struct audio_state_stream address_to b)
{
        if (!a->app[0] || !b->app[0])
                return a->client == b->client && !a->app[0] && !b->app[0];
        return a->uid == b->uid && string_equals((string_address)a->app, (string_address)b->app);
}

static int mixer_percent(unsigned int volume)
{
        unsigned long percent = ((unsigned long)volume * 100 + MIXER_VOLUME / 2) / MIXER_VOLUME;

        return percent > 100 ? 100 : (int)percent;
}

/*
        What the page says of the output that plays, under its strip: nothing is plugged into it (and then the strip is dim), and why it is
        the one that plays (not said when the server plays to a clock, which is no output to be picked).
*/
static fn mixer_device_notes(const struct audio_state address_to page)
{
        struct ui_strips address_to strips = &mixer.big->strips;
        unsigned int why = page->output_why;
        b32 unplugged = false;

        for (unsigned int k = 0; k < page->devices && k < AUDIO_STATE_DEVICES; k++)
                unplugged |= (page->device_list[k].flags & AUDIO_STATE_DEVICE_ACTIVE) && (page->device_list[k].flags & AUDIO_STATE_DEVICE_UNPLUGGED);
        ui_strips_dim(strips, unplugged);
        ui_strips_note(strips, 0, unplugged ? WORDS(mixer_unplugged) : "");
        ui_strips_note(strips, 1,
                       (page->output_state & AUDIO_STATE_OUT_CLOCK) ? ""
                       : why == AUDIO_STATE_WHY_CHOSEN  ? WORDS(mixer_why_chosen)
                       : why == AUDIO_STATE_WHY_NAMED   ? WORDS(mixer_why_named)
                       : why == AUDIO_STATE_WHY_ANALOG  ? WORDS(mixer_why_analog)
                       : why == AUDIO_STATE_WHY_DISPLAY ? WORDS(mixer_why_display)
                       : why == AUDIO_STATE_WHY_HDMI    ? WORDS(mixer_why_hdmi)
                       : why == AUDIO_STATE_WHY_NOTHING ? WORDS(mixer_why_nothing)
                                                        : "");
}

/*
        The strips from the page copied: the device, then the programs in the order the page has them, the streams of one
        program together as one strip with the loudest of their volumes, muted when all of them are and playing when
        any is, and the greatest of their peaks. The members of each are kept for what is sent.
*/
static fn mixer_list(b64 now)
{
        struct mixer_big address_to big = mixer.big;
        const struct audio_state address_to page = &big->copy;
        const struct audio_state_stream address_to streams = mixer.recording ? page->record : page->stream;
        int most = mixer.recording ? AUDIO_STATE_RECORDS : AUDIO_STATE_STREAMS;
        b32 off = mixer.have && mixer.recording && !page->capture, programs = mixer.have && (!mixer.quick || mixer.recording) && !off;
        struct
        {
                int first, percent;
                b32 muted, playing;
                unsigned int left, right;
        } agg[UI_STRIPS_MOST];
        int groups = 0;

        ui_strips_begin(&big->strips);
        // The Allow button has the keys when it appears: nothing else is on the page to have them.
        if (off && !mixer.capture_off)
                mixer.allow.keys = 1;
        mixer.capture_off = off;
        if (mixer.have && !mixer.recording)
        {
                ui_strips_add(&big->strips, 0xfffffffffffffffeul, page->device[0] ? page->device : page->name[0] ? page->name : "?", mixer_percent(page->volume),
                              page->muted != 0, 1, 0, page->peak_left, page->peak_right, now);
                string_copy_bounded((string_address)big->sink, (string_address)page->name, sizeof(big->sink));
                mixer_device_notes(page);
        }
        else if (programs && page->input[0])
        {
                ui_strips_add(&big->strips, 0xfffffffffffffffdul, page->input, mixer_percent(page->input_volume), page->input_muted != 0, 2, 0, page->input_peak_left,
                              page->input_peak_right, now);
                string_copy_bounded((string_address)big->source, (string_address)page->input_name, sizeof(big->source));
        }
        big->device = big->strips.count;
        for (int slot = 0; programs && slot < most && groups + 1 < UI_STRIPS_MOST; slot++)
        {
                const struct audio_state_stream address_to stream = &streams[slot];
                struct mixer_group address_to group;
                int at = -1;

                // A program that records what plays is no microphone's: it is not shown, and it is not the person's to be told of.
                if (stream->index == MIXER_NONE || (mixer.recording && (stream->flags & AUDIO_STATE_MONITOR)))
                        continue;
                for (int k = 0; k < groups; k++)
                        if (mixer_same(&streams[agg[k].first], stream))
                                at = k;
                if (at < 0)
                {
                        at = groups++;
                        agg[at].first = slot, agg[at].percent = 0, agg[at].muted = 1, agg[at].playing = 0, agg[at].left = agg[at].right = 0;
                        big->group[at].count = 0;
                }
                group = &big->group[at];
                if (group->count < MIXER_MEMBERS)
                        group->member[group->count++] = stream->index;
                agg[at].percent = mixer_percent(stream->volume) > agg[at].percent ? mixer_percent(stream->volume) : agg[at].percent;
                agg[at].muted = agg[at].muted && (stream->flags & AUDIO_STATE_MUTED) != 0;
                agg[at].playing = agg[at].playing || ((stream->flags & AUDIO_STATE_PLAYING) && !(stream->flags & AUDIO_STATE_CORKED));
                agg[at].left = stream->peak_left > agg[at].left ? stream->peak_left : agg[at].left;
                agg[at].right = stream->peak_right > agg[at].right ? stream->peak_right : agg[at].right;
        }
        for (int k = 0; k < groups; k++)
        {
                const struct audio_state_stream address_to first = &streams[agg[k].first];

                // A program's number is its name's and its user's, else its connection's: the same through every listing.
                unsigned long id = 0x100000000ul + first->client;

                if (first->app[0])
                {
                        unsigned int hash = 2166136261u;

                        for (const char address_to c = first->app; *c; c++)
                                hash = (hash ^ (p8)*c) * 16777619u;
                        id = ((unsigned long)first->uid << 32 | hash) | 0x8000000000000000ul;
                }
                ui_strips_add(&big->strips, id, mixer_name(first), agg[k].percent, agg[k].muted, 0, agg[k].playing, agg[k].left, agg[k].right, now);
        }
        ui_strips_end(&big->strips);
}

// The page read again: true when what is shown changed. The strips follow it.
static b32 mixer_poll(b64 now)
{
        unsigned int before = mixer.big->copy.seq;
        b32 had = mixer.have, changed;

        if (!mixer.page && !mixer_map())
                mixer.have = false;
        else
                mixer.have = audio_state_read(mixer.page, &mixer.big->copy);
        changed = !mixer.listed || mixer.have != had || (mixer.have && mixer.big->copy.seq != before);
        if (changed || !mixer.have)
                mixer.seen_at = now;
        // A server that was started again has a page of its own: the old one is let go when it has not moved for a while.
        if (mixer.page && now - mixer.seen_at > MIXER_REMAP_NS)
        {
                mixer_unmap();
                mixer.seen_at = now;
        }
        if (changed)
                mixer_list(now);
        mixer.listed = true;
        return changed;
}

/* ---- what is sent ---- */

static fn mixer_disconnect(void)
{
        ctl_disconnect(&mixer.big->ctl);
        mixer.connected = false;
}

// The volume and the mute that were changed, sent: once a pass of the loop, whatever number of events there were.
static fn mixer_flush(void)
{
        struct mixer_big address_to big = mixer.big;
        b32 failed = false;

        for (int k = 0; k < big->strips.count && !failed; k++)
        {
                struct ui_strip address_to strip = &big->strips.item[k];
                unsigned long volume = (unsigned long)strip->percent * MIXER_VOLUME / 100;
                long answer = 0;

                if (!big->send[k])
                        continue;
                if (!mixer.connected)
                        mixer.connected = ctl_connect(&big->ctl, mixer.socket_path) == 0;
                if (!mixer.connected)
                        break;
                if (strip->master && !mixer.recording)
                {
                        if (big->send[k] & 1)
                                answer = ctl_set_sink_volume(&big->ctl, big->sink, volume);
                        if (answer >= 0 && (big->send[k] & 2))
                                answer = ctl_set_sink_mute(&big->ctl, big->sink, strip->muted);
                }
                else if (strip->master)
                {
                        if (big->send[k] & 1)
                                answer = ctl_set_source_volume(&big->ctl, big->source, volume);
                        if (answer >= 0 && (big->send[k] & 2))
                                answer = ctl_set_source_mute(&big->ctl, big->source, strip->muted);
                }
                else
                        for (int member = 0; member < big->group[k - big->device].count && answer >= 0; member++)
                        {
                                unsigned long number = big->group[k - big->device].member[member];

                                if (big->send[k] & 1)
                                        answer = mixer.recording ? ctl_set_output_volume(&big->ctl, number, volume) : ctl_set_input_volume(&big->ctl, number, volume);
                                if (answer >= 0 && (big->send[k] & 2))
                                        answer = mixer.recording ? ctl_set_output_mute(&big->ctl, number, strip->muted) : ctl_set_input_mute(&big->ctl, number, strip->muted);
                        }
                // A connection that broke is made again at the next pass, and what was to be sent stays to be; what the server
                // refused or no longer has is not asked again.
                if (answer == CTL_FAILED)
                {
                        mixer_disconnect();
                        failed = true;
                        break;
                }
                big->send[k] = 0;
        }
}

/* ---- the window ---- */

// The other page shown: what was to be sent is sent first, and the strips are listed again from nothing (their keys, their scroll and what they held are the other page's).
static fn mixer_page(b32 recording)
{
        struct ui_strips address_to strips = &mixer.big->strips;

        if (mixer.recording == recording)
                return;
        mixer_flush();
        mixer.recording = recording;
        strips->ready = 0, strips->count = 0, strips->before_count = 0, strips->focus_id = 0;
        memory_zero(mixer.big->send, sizeof(mixer.big->send));
        mixer.capture_off = false;
        mixer_list(ui_kit.now);
        mixer.dirty = true;
}

// Recording turned on, where the server reads it; its page follows within a moment.
static fn mixer_allow(void)
{
        compositor_volume_write(mixer.capture_path, "on\n");
        mixer.dirty = true;
}

// The two pages as the kit's segmented tabs along the top of the window.
static fn mixer_tabs_layout(struct ui address_to ui)
{
        struct ui_control address_to c = &mixer.tabs;

        if (c->kind != UI_TABS)
                c->kind = UI_TABS, c->variant = UI_TABS_SEGMENTED, ui_control_start(c);
        c->x = UI_STRIP_PAD, c->y = (MIXER_TABS - UI_TABS_H) / 2, c->w = mixer.width * 100 / ui->scale - 2 * UI_STRIP_PAD, c->h = UI_TABS_H;
        c->count = 2;
        c->items[0] = WORDS(mixer_playback), c->items[1] = WORDS(mixer_recording);
        c->at = mixer.recording;
}

static fn mixer_dirty(void)
{
        mixer.dirty = true;
}

static fn mixer_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &mixer.ui;
        struct ui_strips address_to strips = &mixer.big->strips;
        const struct audio_state address_to page = &mixer.big->copy;
        int row = mixer.quick ? ui_px(ui, UI_ROW) : 0, top = mixer.quick ? 0 : ui_px(ui, MIXER_TABS), button = ui_px(ui, UI_BUTTONS_W);

        ui_begin(ui, pixels, mixer.own.table->stride, mixer.width, mixer.height);
        ui_strips_place(ui, strips, 0, top, mixer.width, mixer.height - row - top);
        if (!mixer.have)
        {
                ui_strips_message(ui, strips, WORDS(mixer_off), false);
                return;
        }
        if (top)
        {
                mixer_tabs_layout(ui);
                ui_control_draw(ui, &mixer.tabs);
        }
        ui_strips_draw(ui, strips, mixer.focused && !mixer.row.keys && !mixer.allow.keys);
        if (mixer.quick)
        {
                ui_buttons_set(&mixer.row, 0, language_of("Mixer"), null);
                ui_buttons_place(ui, &mixer.row, (mixer.width - button) / 2, mixer.height - row, button);
                ui_buttons_draw(ui, &mixer.row, mixer.focused);
        }
        // What there is to say instead of strips or beside them.
        if (mixer.capture_off)
        {
                ui_strips_message(ui, strips, WORDS(mixer_recording_off), false);
                ui_buttons_set(&mixer.allow, 0, WORDS(mixer_allow), null);
                ui_buttons_place(ui, &mixer.allow, (mixer.width - button) / 2, strips->y + strips->h / 2, button);
                ui_buttons_draw(ui, &mixer.allow, mixer.focused);
        }
        else if (mixer.recording && !page->input[0] && !strips->count)
                ui_strips_message(ui, strips, WORDS(mixer_no_mic), false);
        else if (mixer.recording && strips->count <= 1)
                ui_strips_message(ui, strips, WORDS(mixer_nobody), true);
        else if (!mixer.recording && !mixer.quick && strips->count <= 1)
                ui_strips_message(ui, strips, WORDS(mixer_empty), true);
}

// One picture, into the buffer the compositor is not reading; false when there is none yet.
static b32 mixer_frame(void)
{
        struct windows address_to table = mixer.own.table;
        unsigned int row = mixer.own.row;
        int slot = mixer.shown < 0 ? 0 : 1 - mixer.shown;

        if (mixer.shown >= 0 && !windows_ready(&mixer.own))
                return false;
        mixer_draw(windows_buffer(table, row, (unsigned int)slot));
        table->width[row] = (unsigned int)mixer.width;
        table->height[row] = (unsigned int)mixer.height;
        table->damage_width[row] = 0;
        windows_show(&mixer.own, (unsigned int)slot);
        mixer.shown = slot;
        mixer.dirty = false;
        return true;
}

// Another program started apart: in a session of its own and a process nobody waits for, its input and output nowhere, as the machine
// script starts what is on the desktop (the launcher starts its programs the same way).
static b32 mixer_start(string_address address_to words, positive count)
{
        bipolar child = system_fork();
        positive status = 0;

        if (child < 0)
                return false;
        if (child == 0)
        {
                bipolar quiet;

                if (system_fork())
                        system_call_1(syscall(exit_group), 0);
                system_call(syscall(setsid));
                quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
                for (positive at = 0; quiet >= 0 && at < 3; at++)
                        system_call_3(syscall(dup3), (positive)quiet, at, 0);
                (void)shell_exec_file(words[0], words, count, window_asked_environment());
                system_call_1(syscall(exit_group), 127);
        }
        system_wait4_retry(child, address_of status, 0, null);
        return true;
}

// The Mixer row: the whole mixer started, in the face this one is drawn in.
static b32 mixer_start_full(void)
{
        string_address face = mixer.font ? mixer.font : (string_address)UI_FONT_REGULAR;
        string_address words[5] = {(string_address) "/mixer", (string_address) "-f", face, null, null};

        if (mixer.recording)
                words[1] = (string_address) "-i", words[2] = (string_address) "-f", words[3] = face;
        return mixer_start(words, mixer.recording ? 4 : 3);
}

static b32 mixer_event(const struct window_event address_to event)
{
        struct ui address_to ui = &mixer.ui;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        p32 bits, allow, row;

        if (event->type == WINDOW_EVENT_KEY && down && event->a == UI_KEY_ESCAPE)
                return false;
        // The pages: Control with Page Up and Page Down, or Tab, and a press on a tab.
        if (!mixer.quick && mixer.have && event->type == WINDOW_EVENT_KEY && down && (event->flags & WINDOW_KEY_CONTROL) &&
            (event->a == UI_KEY_PAGE_UP || event->a == UI_KEY_PAGE_DOWN || event->a == UI_KEY_TAB))
        {
                mixer_page(event->a == UI_KEY_TAB ? !mixer.recording : event->a == UI_KEY_PAGE_DOWN);
                return true;
        }
        if (!mixer.quick && mixer.have)
        {
                p32 tabs;

                mixer_tabs_layout(ui);
                tabs = ui_tabs_pointer(ui, &mixer.tabs, event, ui_kit.pointer_x, ui_kit.pointer_y);
                if (tabs & UI_TABS_CHOSEN)
                        mixer_page(mixer.tabs.at == 1);
                mixer.dirty |= (tabs & UI_TABS_DRAW) != 0;
                if (tabs & UI_TABS_TAKEN)
                        return true;
        }
        // The buttons of the window: Allow (recording is off), and the popup's Mixer row.
        allow = mixer.capture_off ? ui_buttons_event(ui, &mixer.allow, event, ui_kit.pointer_x, ui_kit.pointer_y) : 0;
        if (allow & UI_BUTTONS_FIRED)
                mixer_allow();
        row = mixer.quick && !(allow & UI_BUTTONS_TAKEN) ? ui_buttons_event(ui, &mixer.row, event, ui_kit.pointer_x, ui_kit.pointer_y) : 0;
        if (row & UI_BUTTONS_FIRED)
                return !mixer_start_full();
        if ((allow | row) & UI_BUTTONS_DRAW)
                mixer.dirty = true;
        if ((allow | row) & UI_BUTTONS_TAKEN)
                return true;
        bits = ui_strips_event(ui, &mixer.big->strips, event);
        if (bits & UI_STRIPS_VOLUME)
                mixer.big->send[mixer.big->strips.changed] |= 1;
        if (bits & UI_STRIPS_MUTE)
                mixer.big->send[mixer.big->strips.changed] |= 2;
        if (bits & UI_STRIPS_DRAW)
                mixer.dirty = true;
        return true;
}

static fn mixer_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = mixer.own.table;

        mixer.width = (int)(width > table->output_width ? table->output_width : width);
        mixer.height = (int)(height > table->output_height ? table->output_height : height);
        ui_scratch_fit(&mixer.ui, (unsigned int)mixer.width);
        mixer.dirty = true;
}

// The timer: what is to be sent, then the page looked at as often as it is written, and the time to the next look.
static long mixer_tick(long timeout, long address_to other)
{
        b64 now = (b64)ui_kit.now, wait;
        b32 away = (mixer.own.table->state[mixer.own.row] & WINDOW_MINIMIZED) != 0;

        (void)other;
        mixer_flush();
        if (now >= mixer.next_poll)
        {
                b32 playing = false;

                if (mixer_poll(now) && !away)
                        mixer.dirty = true;
                for (int k = 0; k < mixer.big->strips.count; k++)
                        playing |= mixer.big->strips.item[k].playing;
                mixer.next_poll = now + (away ? MIXER_HIDDEN_NS : playing ? MIXER_POLL_NS : MIXER_QUIET_NS);
        }
        wait = mixer.next_poll - now;
        return timeout < 0 || (b64)timeout > wait ? (long)wait : timeout;
}

static fn mixer_language(void)
{
        mixer.dirty = true;
}

// How many strips wide the recording popup is: the microphone and up to three programs, and room for a line beside the microphone at least.
static int mixer_quick_shown(int count)
{
        return count < 3 ? 3 : count > 4 ? 4 : count;
}

static int mixer_open(void)
{
        struct windows address_to table = mixer.own.table;
        unsigned int row = mixer.own.row;
        int width = ui_px(&mixer.ui, mixer.quick ? MIXER_QUICK_W : MIXER_WIDTH), height = ui_px(&mixer.ui, mixer.quick ? MIXER_QUICK_H : MIXER_HEIGHT);
        int room = (int)table->output_height - ui_px(&mixer.ui, 2 * (24 + 24));
        long mapped;

        // The popup is not lifted by a second start: pressed again, the cell puts it away.
        if (!mixer.quick && (mixer.recording ? ui_lift_page(&mixer.own, (string_address) "mixer", 1) : ui_lift_other(&mixer.own, (string_address) "mixer", false)))
                return 0;
        mapped = ui_map(sizeof(struct mixer_big));
        if (mapped < 0 && mapped > -4096)
        {
                compositor_kmsg((string_address) "mixer: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }
        mixer.big = (struct mixer_big address_to)mapped;
        // The page is read first: the recording popup is as wide as the strips it shows (the microphone and up to three programs; room for a line beside one at least).
        mixer_poll((b64)ui_kit.now);
        if (mixer.quick && mixer.recording)
                width = ui_px(&mixer.ui, MIXER_QUICK_WIDTH(mixer_quick_shown(mixer.big->strips.count)));
        mixer_size((unsigned int)(width > (int)table->output_width ? (int)table->output_width : width),
                   (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_anchor_take(&mixer.anchor, table, mixer.beside);
        if (mixer.anchor.anchored)
                ui_anchor_set(&mixer.anchor, table, row, mixer.width, mixer.height);
        else
                ui_centre(table, row, mixer.width, mixer.height, ui_px(&mixer.ui, 12));
        language_listen(mixer_language);
        mixer_frame();
        return -1;
}

// A second start of the mixer with -i asked this one for the Recording page: the page a press on its tab shows.
static fn mixer_asked(unsigned int page)
{
        if (!mixer.quick)
                mixer_page(page == 1);
}

static struct ui_app mixer_app = {
        .own = &mixer.own,
        .ui = &mixer.ui,
        .missing = (string_address) "mixer: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Mixer",
        .name = (string_address) "mixer",
        .glass = 0, // the kit's surface, as every window that stays is
        .nofont = (string_address) "mixer: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &mixer.dirty,
        .focused = &mixer.focused,
        .ground = &mixer.ground,
        .open = mixer_open,
        .frame = mixer_frame,
        .event = mixer_event,
        .changed = mixer_dirty,
        .size = mixer_size,
        .tick = mixer_tick,
        .page = mixer_asked,
};

static b32 mixer_main()
{
        memory_zero(&mixer, sizeof(mixer));
        mixer.shown = -1;
        mixer.page_path = AUDIO_STATE_PATH;
        mixer.socket_path = MIXER_SOCKET;
        mixer.capture_path = SETTING_AUDIO_CAPTURE_PATH;
        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address value;

                if (string_equals(program_argument(at), (string_address) "-i"))
                {
                        mixer.recording = true;
                        continue;
                }

                if (string_equals(program_argument(at), (string_address) "-q"))
                {
                        mixer.quick = true;
                        mixer_app.glass = WINDOW_GLASS | WINDOW_DISMISS | WINDOW_NO_TAB; // glass over what is under it, put away by a press anywhere but on it, and no tab
                        mixer_app.dismiss = true;                          // and by the keys going elsewhere
                        mixer_app.name = (string_address) "mixer-quick";
                        continue;
                }
                if (string_equals(program_argument(at), (string_address) "-a"))
                {
                        mixer.beside = true;
                        continue;
                }
                if (at + 1 >= (positive)program_argument_count())
                        break;
                if ((value = ui_argument(at, (string_address) "-f")))
                        mixer_app.font = mixer.font = value;
                else if ((value = ui_argument(at, (string_address) "-b")))
                        mixer_app.bold = value;
                else if ((value = ui_argument(at, (string_address) "-p")))
                        mixer.page_path = (const char address_to)value;
                else if ((value = ui_argument(at, (string_address) "-s")))
                        mixer.socket_path = (const char address_to)value;
                else
                        continue;
                at++;
        }
        return ui_app_run(&mixer_app);
}

#endif // MOONWATER_CANVAS_MIXER
