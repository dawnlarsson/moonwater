/*
        The mixer, a window of the sound server's volumes: the output device first, then a strip for every program that has
        a sound (the kit's strips, ui.c: a disc, its name, its volume, a level meter beside a vertical slider, a mute
        button). It says what each strip is, hands the strips its events and acts on the one that changed; the sliders,
        the keys, the wheel, the meters' fall and hold, the scrolling when there are more strips than room and the
        mirroring of a window that reads from the right are the kit's.

        What it shows is the sound server's page of memory (src/audio/state.c, read without a call: the page is mapped
        read-only and copied whole), at the pace the page is written while anything plays and twice a second when all is quiet.
        What it changes goes the other way over the server's socket (src/audio/control.c): a program's volume or mute is
        the program's streams' (the streams of one program, by the name it gave, are one strip), and the device's volume
        and mute are the sink's, which the server keeps in /root/audio.volume and /root/audio.mute for the next boot. A
        strip the person is moving is the person's for a moment whatever the page says meanwhile (ui.c), and what is
        changed is sent once a pass of the loop however many events came, so a drag costs a request a frame.

        With the server not running, or no page, the window says so, and the strips come when the page does.

            mixer [-f FONT.ttf] [-b BOLD.ttf] [-p PAGE] [-s SOCKET]

        One window: a second start finds the first and lifts it. Escape closes it.

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
        char sink[64];                      // the device as programs name it, for its volume
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
        b32 focused, dirty, have, connected, listed;
        b64 next_poll, seen_at;
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
        The strips from the page copied: the device, then the programs in the order the page has them, the streams of one
        program together as one strip with the loudest of their volumes, muted when all of them are and playing when
        any is, and the greatest of their peaks. The members of each are kept for what is sent.
*/
static fn mixer_list(b64 now)
{
        struct mixer_big address_to big = mixer.big;
        const struct audio_state address_to page = &big->copy;
        struct
        {
                int first, percent;
                b32 muted, playing;
                unsigned int left, right;
        } agg[UI_STRIPS_MOST];
        int groups = 0;

        ui_strips_begin(&big->strips);
        if (mixer.have)
        {
                ui_strips_add(&big->strips, 0xfffffffffffffffeul, page->device[0] ? page->device : page->name[0] ? page->name : "?", mixer_percent(page->volume),
                              page->muted != 0, 1, 0, page->peak_left, page->peak_right, now);
                string_copy_bounded((string_address)big->sink, (string_address)page->name, sizeof(big->sink));
        }
        for (int slot = 0; mixer.have && slot < AUDIO_STATE_STREAMS && groups + 1 < UI_STRIPS_MOST; slot++)
        {
                const struct audio_state_stream address_to stream = &page->stream[slot];
                struct mixer_group address_to group;
                int at = -1;

                if (stream->index == MIXER_NONE)
                        continue;
                for (int k = 0; k < groups; k++)
                        if (mixer_same(&page->stream[agg[k].first], stream))
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
                const struct audio_state_stream address_to first = &page->stream[agg[k].first];

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
                if (strip->master)
                {
                        if (big->send[k] & 1)
                                answer = ctl_set_sink_volume(&big->ctl, big->sink, volume);
                        if (answer >= 0 && (big->send[k] & 2))
                                answer = ctl_set_sink_mute(&big->ctl, big->sink, strip->muted);
                }
                else
                        for (int member = 0; member < big->group[k - 1].count && answer >= 0; member++)
                        {
                                if (big->send[k] & 1)
                                        answer = ctl_set_input_volume(&big->ctl, big->group[k - 1].member[member], volume);
                                if (answer >= 0 && (big->send[k] & 2))
                                        answer = ctl_set_input_mute(&big->ctl, big->group[k - 1].member[member], strip->muted);
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

static fn mixer_dirty(void)
{
        mixer.dirty = true;
}

static fn mixer_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &mixer.ui;
        struct ui_strips address_to strips = &mixer.big->strips;

        ui_begin(ui, pixels, mixer.own.table->stride, mixer.width, mixer.height);
        ui_strips_place(ui, strips, 0, 0, mixer.width, mixer.height);
        if (!mixer.have)
        {
                ui_strips_message(ui, strips, WORDS(mixer_off), false);
                return;
        }
        ui_strips_draw(ui, strips, mixer.focused);
        if (strips->count <= 1)
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

static b32 mixer_event(const struct window_event address_to event)
{
        p32 bits;

        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && event->a == UI_KEY_ESCAPE)
                return false;
        bits = ui_strips_event(&mixer.ui, &mixer.big->strips, event);
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
        b64 now = (b64)clock_monotonic_nanoseconds(), wait;
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

static int mixer_open(void)
{
        struct windows address_to table = mixer.own.table;
        unsigned int row = mixer.own.row;
        int width = ui_px(&mixer.ui, MIXER_WIDTH), height = ui_px(&mixer.ui, MIXER_HEIGHT), room = (int)table->output_height - ui_px(&mixer.ui, 2 * (24 + 24));
        long mapped;

        if (ui_lift_other(&mixer.own, (string_address) "mixer", false))
                return 0;
        mapped = ui_map(sizeof(struct mixer_big));
        if (mapped < 0 && mapped > -4096)
        {
                compositor_kmsg((string_address) "mixer: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }
        mixer.big = (struct mixer_big address_to)mapped;
        mixer_size((unsigned int)(width > (int)table->output_width ? (int)table->output_width : width),
                   (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_centre(table, row, mixer.width, mixer.height, ui_px(&mixer.ui, 12));
        language_listen(mixer_language);
        mixer_poll((b64)clock_monotonic_nanoseconds());
        mixer_frame();
        return -1;
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
};

static b32 mixer_main()
{
        memory_zero(&mixer, sizeof(mixer));
        mixer.shown = -1;
        mixer.page_path = AUDIO_STATE_PATH;
        mixer.socket_path = MIXER_SOCKET;
        for (positive at = 1; at + 1 < (positive)program_argument_count(); at++)
        {
                string_address value;

                if ((value = ui_argument(at, (string_address) "-f")))
                        mixer_app.font = value;
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
