/*
        The taskbar's geometry, hit test, damage and painting (taskbar.h says what
        it is and what the compositor gives it).

        Along the edge the band runs in (its length) the tabs come first, in the
        order the windows showed, each as long as its title needs at a width a
        character has, and the tray is at the far end: the clock, and the battery
        beside it when the settings name one. Across the band a tab is its
        thickness less an inset, and it hangs off the output's edge it is along, so
        its round corners show on the side towards the middle of the output. A
        bottom or top band runs left to right, with the tray at the right end and
        the tabs centred on the band itself, not in what the tray leaves of it
        (moved off the middle only as far as the button or the tray would
        otherwise be run into); a left or right band runs top to
        bottom, with the tray at the bottom, the battery above the clock. A band
        on a side writes its titles and its tray turned a quarter, the tops of
        the letters toward the middle of its output or toward the edge it is
        on, as `taskbar text` says.

        With `taskbar launcher on` the band also has a button at its leading end (the left of a bottom or top band, the top
        of a side one): a tab-sized square with a grid of nine dots on it, that opens the launcher. It is a slot of the band like the
        tray, with a bit of its own in the redo mask, drawn as a tab is (lit while the launcher is up), and the tabs are laid
        out in what it leaves. Off, which is the default, the band is as it was.

        Every rectangle here is on the output and in device pixels. The painting
        works in the band's own coordinates (the pixels start at its top left) and
        the glass in the rectangle the compositor blurred for the span.

        Its own look, kept: the compositor paints it into its scene (glass, no kit window and no kit frame clock, so no
        ripple), in the kit theme's tints (compositor_tab_target), with a finger's reach (taskbar_hit_reach).
*/

// The taskbar's own state: what the settings say, the minute the tray shows, and the battery it has read.
static struct taskbar_state
{
        int stale;                              // the band is drawn whole at the next present
        unsigned int minute;                    // the minute the tray shows
        unsigned int language;                  // the language the tray's words were last taken in (taskbar_tick)
        unsigned int position_default;          // where the band sits when no setting keeps one (-t: top)
        unsigned int position;                  // where it sits now
        int all;                                // `taskbar display all`: a band on every screen, each its own windows
        char battery[BATTERY_NAME];      // the settings' word: "" or "auto" for the first battery found, "none", or a name
        int battery_item;                       // the battery is shown: its row is not none and a battery was read
        int battery_percent, battery_charging;  // what it read
        int edge;                               // a side band's letters have their tops toward its edge, not the middle
        int tray_custom;                        // the tray is a format of the person's (`taskbar tray`), drawn as one text
        char tray_format[SETTING_TRAY_MOST + 1]; // that format
        char tray_text[SETTING_TRAY_MOST * 4];  // what it says this minute
        int tray_chars;                         // characters in tray_text
        int launcher;                           // `taskbar launcher on`: the band has a launcher button at its leading end
        int hide_fullscreen;                    // `taskbar fullscreen on`: the band slides off its edge while a window is fullscreen
        b64 shift, slide_from, slide_to;        // how far off its edge the band is now (UI_ONE: all of its thickness), and the slide it is in
        positive slide_at;                      // when that slide began on the clock (0: none is going, the band is at slide_to)
        int volume_item;                        // the sound server is there: the tray has a volume cell before the battery's
        int volume_percent, volume_muted;       // what it says (the master volume, 0 to 100, and whether it is muted)
        int volume_flash;                       // a key or the wheel changed it just now: the cell says the percent, not the speaker
        int mic_item, mic_muted;                // a program records from a microphone (or its mute was just thrown): the tray has a microphone cell, struck when muted
        int notices_item;                       // the board holds a notice or an issue: the tray starts with a notices dot (at the leading end)
        unsigned int notices_severity;          // the worst of them: WINDOWS_ISSUE_ERROR (red) or WINDOWS_ISSUE_WARNING (amber)
        int scale;                              // `taskbar scale`: percent of its screen's scale the band is laid out and drawn at
        int padding;                            // `taskbar padding`: logical pixels, at that scale, between its items and at its ends
} taskbar = {1, 0, 0, TASKBAR_BOTTOM, TASKBAR_BOTTOM, 0, {0}, 0, 0, 0, .scale = TASKBAR_SCALE_STANDARD, .padding = TASKBAR_PADDING_STANDARD};

_Static_assert(TASKBAR_PADDING_STANDARD == UI_GAP1, "the standard padding is the kit's gap between pieces that go together");

// A tab's glass is tinted a chunk at a time into this, and grained over no picture at all.
#define TASKBAR_TAB_CHUNK 256
static unsigned int taskbar_tint[TASKBAR_TAB_CHUNK];
static const unsigned int taskbar_clear[TASKBAR_TAB_CHUNK];

// What the band takes from compositor.c, which comes before this file in every build and defines them: the meet of two
// rectangles, the grain a glass row is made with, and a colour made more vivid.
static void compositor_meet(int *into, const int *other);
static const unsigned int *compositor_grain_row(unsigned int light, int x, int y);
static unsigned int compositor_vivid(unsigned int pixel);

static inline b32 language_rtl(void); // the language taken reads from the right (ui/language.c)

// The band's geometry as one struct: computed once for a call, from the view and the state.
struct taskbar_layout
{
        int vertical;                // the band runs top to bottom
        int bx, by, bw, bh;          // the band on the output
        int origin, length, thick;   // along the edge: where it starts, how long it is; across it: how thick
        int high;                    // a tab's thickness: the band's less the inset, and the text's height
        int gap, inset;
        int count, most;             // the tabs, and the widest a tab may be
        const unsigned char *lengths; // each tab's title in characters, which its tab fits
        int start, windows;          // where the first tab is along the edge, and what the tabs take with their gaps
        int tray, tray_at;           // the tray's length and where it starts along the edge
        int across, across_len;      // the tabs' place and thickness across the edge
        int button, lead;            // the launcher button's length along the edge (0: none), and what it and its gap take of the start
};

static unsigned int taskbar_minute_now(void)
{
        timespec now = {0, 0};

        clock_gettime(CLOCK_REALTIME, address_of now);
        return (unsigned int)(now.tv_sec / 60);
}

static int taskbar_vertical(unsigned int position)
{
        return position == TASKBAR_LEFT || position == TASKBAR_RIGHT;
}

// The scale the band is laid out and drawn at: its screen's (percent) and its own on top (`taskbar scale`).
static int taskbar_scaled(int scale)
{
        return scale * taskbar.scale / 100;
}

// How thick the band is across the edge, in device pixels, on a screen at scale (percent).
static int taskbar_thick(int scale)
{
        return COMPOSITOR_PANEL * taskbar_scaled(scale) / 100;
}

// The band at a shift: along the edge its position names, COMPOSITOR_PANEL logical pixels thick, and moved off that edge by shift of
// its thickness (UI_ONE is all of it, which is off the output).
static void taskbar_band_at(const struct taskbar_view *view, b64 shift, int *x, int *y, int *width, int *height)
{
        int thick = taskbar_thick(view->scale);
        int off = (int)(((long long)thick * (long long)shift) / (long long)UI_ONE);

        switch (view->position)
        {
        case TASKBAR_TOP:
                *x = 0, *y = -off, *width = view->width, *height = thick;
                break;
        case TASKBAR_LEFT:
                *x = -off, *y = 0, *width = thick, *height = view->height;
                break;
        case TASKBAR_RIGHT:
                *x = view->width - thick + off, *y = 0, *width = thick, *height = view->height;
                break;
        default:
                *x = 0, *y = view->height - thick + off, *width = view->width, *height = thick;
                break;
        }
        // On its screen, wherever that screen is in the space: every rectangle below is made from this one.
        *x += view->x;
        *y += view->y;
}

// The band: where it is now, which is its place unless it is sliding off (taskbar_slide_now).
static void taskbar_band(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        taskbar_band_at(view, taskbar.shift, x, y, width, height);
}

/*
        The slide (`taskbar fullscreen on`): while a window is fullscreen the band moves off its edge, and back when none is. The
        move is the kit's window motion (UI_WINDOW_NS on --ease, the glide a window makes), from where the band is when it is
        asked to go. The compositor asks on every pass of its loop, with the clock (taskbar_slide_now), damages the band where it
        was and where it goes when the answer moves it, and keeps asking through taskbar_sliding while it is still going.
*/
#define TASKBAR_SLIDE_NS UI_WINDOW_NS

// How far off its edge the band is at now: the slide's share of the way from slide_from to slide_to, on the ease.
static b64 taskbar_shift_at(positive now)
{
        b64 done;
        long long moved;

        if (!taskbar.slide_at)
                return taskbar.slide_to;
        if (now <= taskbar.slide_at)
                return taskbar.slide_from;
        done = ui_bezier_at(&ui_ease, ui_progress((b64)(now - taskbar.slide_at), TASKBAR_SLIDE_NS));
        moved = ((long long)taskbar.slide_to - (long long)taskbar.slide_from) * (long long)done / (long long)UI_ONE;
        return (b64)((long long)taskbar.slide_from + moved);
}

// The band's place for now: off its edge while `taskbar fullscreen on` and fullscreen is up, else on it. A change of place begins a
// slide from where the band is, which at once when animate is 0 (reduced motion). Returns whether the band moved, and before is the
// shift it was at before this call, for the damage of its old place.
static int taskbar_slide_now(int fullscreen, positive now, int animate, b64 *before)
{
        b64 target = taskbar.hide_fullscreen && fullscreen ? UI_ONE : 0, was = taskbar.shift;

        *before = was;
        if (target != taskbar.slide_to)
        {
                taskbar.slide_from = was;
                taskbar.slide_to = target;
                taskbar.slide_at = animate ? now : 0;
        }
        taskbar.shift = taskbar_shift_at(now);
        return taskbar.shift != was;
}

// Whether a slide is still going at now: its band is damaged again after each present until it ends.
static int taskbar_sliding(positive now)
{
        return taskbar.slide_at && now > taskbar.slide_at && now - taskbar.slide_at < TASKBAR_SLIDE_NS;
}

// How far off its edge the band is now.
static b64 taskbar_shift(void)
{
        return taskbar.shift;
}

// The one rectangle the band covers while it moves from before to where it is now, so that its damage covers both places.
static void taskbar_slide_rect(const struct taskbar_view *view, b64 before, int *x, int *y, int *width, int *height)
{
        int x0, y0, w0, h0, x1, y1, w1, h1, right, bottom;

        taskbar_band_at(view, before, &x0, &y0, &w0, &h0);
        taskbar_band_at(view, taskbar.shift, &x1, &y1, &w1, &h1);
        right = x0 + w0 > x1 + w1 ? x0 + w0 : x1 + w1;
        bottom = y0 + h0 > y1 + h1 ? y0 + h0 : y1 + h1;
        *x = x0 < x1 ? x0 : x1;
        *y = y0 < y1 ? y0 : y1;
        *width = right - *x;
        *height = bottom - *y;
}

/*
        A run of characters' width at the text's height: a character a little
        over half of it, and room either side as there is above and below a
        capital, so the title sits in the tab with the same margin all round.
        Never narrower than the text is high.
*/
static int taskbar_text(int high, int characters)
{
        int wanted = (characters * high * 308 + high * 604) / 1000;

        return wanted < high ? high : wanted;
}

// The tray's length along the edge: the clock, and the battery before it, on a band of any side.
static int taskbar_tray(int high)
{
        if (taskbar.tray_custom)
                return taskbar_text(high, taskbar.tray_chars > 0 ? taskbar.tray_chars : 1);
        return taskbar_text(high, 5) * (1 + (taskbar.battery_item ? 1 : 0) + (taskbar.volume_item ? 1 : 0) + (taskbar.mic_item ? 1 : 0) +
                                        (taskbar.notices_item ? 1 : 0));
}

static int taskbar_tab_width(const struct taskbar_layout *lay, int which)
{
        int wanted = lay->lengths ? taskbar_text(lay->high, lay->lengths[which]) : lay->most;

        return wanted < lay->most ? wanted : lay->most;
}

static void taskbar_layout(const struct taskbar_view *view, struct taskbar_layout *lay)
{
        int room, windows = 0;

        taskbar_band(view, &lay->bx, &lay->by, &lay->bw, &lay->bh);
        lay->vertical = taskbar_vertical(view->position);
        lay->thick = lay->vertical ? lay->bw : lay->bh;
        lay->length = lay->vertical ? lay->bh : lay->bw;
        lay->origin = lay->vertical ? lay->by : lay->bx;
        lay->gap = taskbar.padding * taskbar_scaled(view->scale) / 100; // `taskbar padding`, the kit's gap between pieces that go together at its standard
        lay->inset = 3 * taskbar_scaled(view->scale) / 100;
        lay->high = lay->thick - lay->inset;
        lay->count = (int)view->count;
        lay->lengths = view->lengths;
        lay->tray = taskbar_tray(lay->high);
        // A format of the person's may be long, but the tabs keep half of the band.
        if (taskbar.tray_custom && lay->tray > lay->length / 2)
                lay->tray = lay->length / 2;
        // The launcher button is a square a tab high at the leading end, a gap in from it; the tabs have what is left.
        lay->button = taskbar.launcher && lay->high > 0 ? lay->high : 0;
        lay->lead = lay->button ? lay->gap + lay->button : 0;
        room = lay->length - lay->lead - lay->tray - (lay->count + 2) * lay->gap;
        lay->most = lay->count && room > 0 ? room / lay->count : 0;
        lay->most = lay->most > 8 * lay->thick ? 8 * lay->thick : lay->most;
        for (int at = 0; lay->most && at < lay->count; at++)
                windows += taskbar_tab_width(lay, at) + lay->gap;
        lay->windows = windows;
        // The tray is held to the far end. Along a bottom or top band the tabs are centred on the band itself, whatever the button
        // at one end and the tray at the other take of it, and are moved off the middle only as far as they would otherwise run into
        // one of them; along a side one the tabs start below a gap at the top.
        lay->tray_at = lay->origin + lay->length - lay->gap - lay->tray;
        if (lay->vertical)
                lay->start = lay->origin + lay->gap + lay->lead;
        else
        {
                int group = windows > lay->gap ? windows - lay->gap : 0; // the tabs and the gaps between them, not the one after the last
                int least = lay->origin + lay->lead + lay->gap, most_at = lay->tray_at - lay->gap - group;

                lay->start = lay->origin + (lay->length - group) / 2;
                lay->start = lay->start > most_at ? most_at : lay->start;
                lay->start = lay->start < least ? least : lay->start;
        }
        lay->across = lay->vertical ? (view->position == TASKBAR_LEFT ? lay->bx : lay->bx + lay->inset)
                                    : (view->position == TASKBAR_TOP ? lay->by : lay->by + lay->inset);
        lay->across_len = lay->thick - lay->inset;
}

/*
        Where a slot is along the edge: its start and length. Slot -2 is the
        tray as a whole, -3 the clock, -4 the battery, TASKBAR_VOLUME (-6) the
        volume item and TASKBAR_MIC (-7) the microphone's (all inside the tray, the
        microphone first, then the volume), and
        TASKBAR_LAUNCHER (-5) the launcher button, a gap in from the leading
        end. Answers 0 for a slot that has none: a tab past the count or too
        narrow for one, the battery when none is shown, the volume when the
        sound server is not there, the microphone when nothing records, or the
        button when the band has none.
*/
static int taskbar_along(const struct taskbar_layout *lay, int which, int *along, int *length)
{
        int text = taskbar_text(lay->high, 5), at, notice = taskbar.notices_item ? text : 0;

        if (which >= 0)
        {
                if (which >= lay->count || lay->most < 1)
                        return 0;
                *along = lay->start;
                for (at = 0; at < which; at++)
                        *along += taskbar_tab_width(lay, at) + lay->gap;
                *length = taskbar_tab_width(lay, which);
        }
        else if (which == TASKBAR_LAUNCHER)
        {
                if (!lay->button)
                        return 0;
                *along = lay->origin + lay->gap, *length = lay->button;
        }
        else if (which == -2)
                *along = lay->tray_at, *length = lay->tray;
        else if (which == -3)
        {
                if (taskbar.tray_custom)
                        return 0;
                *along = lay->tray_at + notice + (taskbar.battery_item ? text : 0) + (taskbar.volume_item ? text : 0) + (taskbar.mic_item ? text : 0), *length = text;
        }
        else if (which == -4)
        {
                if (!taskbar.battery_item || taskbar.tray_custom)
                        return 0;
                *along = lay->tray_at + notice + (taskbar.volume_item ? text : 0) + (taskbar.mic_item ? text : 0), *length = text;
        }
        else if (which == TASKBAR_VOLUME)
        {
                if (!taskbar.volume_item || taskbar.tray_custom)
                        return 0;
                *along = lay->tray_at + notice + (taskbar.mic_item ? text : 0), *length = text;
        }
        else if (which == TASKBAR_MIC)
        {
                if (!taskbar.mic_item || taskbar.tray_custom)
                        return 0;
                *along = lay->tray_at + notice, *length = text;
        }
        else if (which == TASKBAR_NOTICES)
        {
                if (!taskbar.notices_item || taskbar.tray_custom)
                        return 0;
                *along = lay->tray_at, *length = text;
        }
        else
                return 0;
        // Along a bottom or top band, reading from the right, the band is the same band turned over its length: the launcher's
        // button at its right end, the tabs in the order the windows showed from the right, the tray at its left end.
        if (!lay->vertical && language_rtl())
                *along = lay->origin + lay->length - (*along - lay->origin) - *length;
        return 1;
}

// A slot's rectangle on the output, from its place along the edge and across it.
static void taskbar_map(const struct taskbar_layout *lay, int along, int length, int *x, int *y, int *width, int *height)
{
        if (lay->vertical)
                *x = lay->across, *y = along, *width = lay->across_len, *height = length;
        else
                *x = along, *y = lay->across, *width = length, *height = lay->across_len;
}

// The span of the band's glass over the tabs from first to last along the edge, on the output.
static void taskbar_span_rect(const struct taskbar_layout *lay, int first, int last, int *rect)
{
        if (lay->vertical)
                rect[0] = lay->bx, rect[1] = first, rect[2] = lay->bw, rect[3] = last - first;
        else
                rect[0] = first, rect[1] = lay->by, rect[2] = last - first, rect[3] = lay->bh;
}

static int taskbar_slot(const struct taskbar_view *view, int which, int *x, int *y, int *width, int *height)
{
        struct taskbar_layout lay;
        int along, length;

        taskbar_layout(view, &lay);
        if (!taskbar_along(&lay, which, &along, &length))
                return 0;
        taskbar_map(&lay, along, length, x, y, width, height);
        // A band too narrow for a tab has none: nothing is drawn outside the band.
        return *height > 0 && *width > 0 && lay.across_len > 0 && along >= lay.origin && along + length <= lay.origin + lay.length;
}

/*
        What is at a point of the band: a tab's number, -2 for the tray, or TASKBAR_HIT_NONE. With reach, a finger's:
        each slot reaches that much further toward the middle of its screen and half the gap further along the band
        either side, so a tab a finger lands on the edge of is still the tab, and nothing is drawn any larger.
*/
static int taskbar_hit_reach(const struct taskbar_view *view, int px, int py, int reach)
{
        int half = reach ? 2 * view->scale / 100 : 0;

        for (int which = TASKBAR_NOTICES; which < (int)view->count; which++)
        {
                int x, y, width, height;

                if (which == -4 || which == -3 || !taskbar_slot(view, which, &x, &y, &width, &height))
                        continue;
                if (taskbar_vertical(view->position))
                        y -= half, height += 2 * half, width += reach, x -= view->position == TASKBAR_RIGHT ? reach : 0;
                else
                        x -= half, width += 2 * half, height += reach, y -= view->position == TASKBAR_TOP ? 0 : reach;
                if (px >= x && px < x + width && py >= y && py < y + height)
                        return which;
        }
        return TASKBAR_HIT_NONE;
}

static int taskbar_hit(const struct taskbar_view *view, int px, int py)
{
        return taskbar_hit_reach(view, px, py, 0);
}

// The span: from the first tab (or the tray, with no tabs) to the end of the tray. Answers 0 when there is no tray.
static int taskbar_span(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        struct taskbar_layout lay;
        int along, length, first, last, tab, rect[4];

        taskbar_layout(view, &lay);
        if (!taskbar_along(&lay, -2, &along, &length))
                return 0;
        last = along + length;
        first = along;
        if (lay.count && taskbar_along(&lay, 0, &tab, &length))
        {
                // The first tab is before the tray, or after it reading from the right: the span is whichever side it runs to.
                first = tab < first ? tab : first;
                last = tab + length > last ? tab + length : last;
        }
        taskbar_span_rect(&lay, first, last, rect);
        *x = rect[0], *y = rect[1], *width = rect[2], *height = rect[3];
        return 1;
}

// A slot's redo bit: a tab's, the tray's on its own bit past the tabs, and the launcher button's on the one after.
static unsigned long taskbar_bit(int which)
{
        return which == TASKBAR_LAUNCHER ? 1ul << (WINDOWS + 1) : which < 0 ? 1ul << WINDOWS : 1ul << which;
}

/*
        The slots the damage meets, with the blur's reach round each: a change
        in the scene under a tab, or under its reach, is one to its glass. The
        reach runs across the whole band and along the slot.
*/
static unsigned long taskbar_meets(const struct taskbar_view *view, int (*damage)[4], unsigned int damages)
{
        struct taskbar_layout lay;
        unsigned long redo = 0;

        taskbar_layout(view, &lay);
        for (int which = TASKBAR_LAUNCHER; which < lay.count; which++)
        {
                int x, y, width, height, reach[4];

                if (which == -4 || which == -3 || !taskbar_slot(view, which, &x, &y, &width, &height))
                        continue;
                if (lay.vertical)
                        reach[0] = lay.bx - COMPOSITOR_GLASS_REACH, reach[1] = y - COMPOSITOR_GLASS_REACH,
                        reach[2] = lay.bx + lay.bw + COMPOSITOR_GLASS_REACH, reach[3] = y + height + COMPOSITOR_GLASS_REACH;
                else
                        reach[0] = x - COMPOSITOR_GLASS_REACH, reach[1] = lay.by - COMPOSITOR_GLASS_REACH,
                        reach[2] = x + width + COMPOSITOR_GLASS_REACH, reach[3] = lay.by + lay.bh + COMPOSITOR_GLASS_REACH;
                for (unsigned int at = 0; at < damages; at++)
                {
                        int meet[4];

                        memory_copy(meet, damage[at], sizeof(meet));
                        compositor_meet(meet, reach);
                        if (meet[2] > meet[0] && meet[3] > meet[1])
                        {
                                redo |= taskbar_bit(which);
                                break;
                        }
                }
        }
        return redo;
}

static unsigned int taskbar_tab_ink(int slot, unsigned int mark)
{
        return slot < 0 && slot != TASKBAR_LAUNCHER ? compositor_ink_shadow()
               : mark & COMPOSITOR_TAB_AWAY ? COMPOSITOR_INK_PANEL
               : mark & COMPOSITOR_TAB_LIT  ? COMPOSITOR_INK_BUTTON_LIT
                                            : compositor_ink_shadow();
}

// The corners the tabs keep square: the ones on the edge the band is along, so the round ones face the middle.
static unsigned int taskbar_square(unsigned int position)
{
        return position == TASKBAR_TOP ? 1 | 2 : position == TASKBAR_LEFT ? 1 | 4 : position == TASKBAR_RIGHT ? 2 | 8 : 4 | 8;
}

// The band's text is the kit's (ui_cell_line, ui_cell_turned): its picture, engine and face are the band's, which taskbar_cell keeps.
static struct ui taskbar_cell_ui;

static struct ui address_to taskbar_cell(const struct taskbar_view *view, int bw)
{
        taskbar_cell_ui.pixels = view->pixels, taskbar_cell_ui.pitch = (unsigned long)bw, taskbar_cell_ui.scale = taskbar_scaled(view->scale);
        taskbar_cell_ui.engine = view->engine, taskbar_cell_ui.regular = view->face;
        return &taskbar_cell_ui;
}

// One line of text in the band, at band-local x, y of its rectangle, in the tab's colour for ground (ui_cell_line).
static void taskbar_text_line(const struct taskbar_view *view, int bx, int by, int bw, int x, int y, int width,
                              int height, const char *text, unsigned int ink, unsigned int ground)
{
        ui_cell_line(taskbar_cell(view, bw), x - bx, y - by, width, height, text, ink, ground);
}

/*
        Which way a band at position turns its text: none on a bottom or top
        band, and on a side the way that puts the tops of the letters toward
        the middle of the output, or toward the edge the band is on when
        `taskbar text edge` says so. A left band's middle is to its right.
*/
static unsigned int taskbar_turn(unsigned int position)
{
        if (!taskbar_vertical(position))
                return 0;
        return (position == TASKBAR_LEFT) != (taskbar.edge != 0) ? FONT_TOPS_RIGHT : FONT_TOPS_LEFT;
}

/*
        One line of text in a slot of a band on a side, turned as taskbar_turn
        says: laid out as a bottom band lays a slot's text, along the slot's
        length and in the middle of its thickness, and turned onto it. What is
        longer than the slot, which a bottom band would cut off at its end, is
        cut to fit the slot less the margin either end (the room above and
        below a capital) with dots after it.
*/
static void taskbar_text_turned(const struct taskbar_view *view, int bx, int by, int bw, int x, int y, int width,
                                int height, const char *text, unsigned int ink, unsigned int ground)
{
        ui_cell_turned(taskbar_cell(view, bw), x - bx, y - by, width, height, text, taskbar_turn(view->position), ink, ground);
}

/*
        A tab is glass: the blurred scene under it (view->glass, the rectangle
        gx, gy, gw wide) made more vivid and tinted toward its own side of
        lightness. Answers what it came to in its middle, which its text stands
        out from. The band is at bx, by and bw wide.
*/
static unsigned int taskbar_glass_tab(const struct taskbar_view *view, int gx, int gy, int gw, int bx, int by, int bw,
                                      int x, int y, int width, int height, int slot, unsigned int mark)
{
        // The glass is painted on the band, not recorded (R3 in the note): the frame is the picture's, and this paints it.
        if (view->scene)
                view->scene->lost = UI_LOST_GLASS;
        unsigned int middle = compositor_vivid(view->glass[(long)(y + height / 2 - gy) * gw + x - gx + width / 2]);
        // The tray (-2) is never lit; a tab with the keys is, and so is the launcher button while the launcher is up.
        b32 lit = (slot >= 0 || slot == TASKBAR_LAUNCHER) && (mark & COMPOSITOR_TAB_LIT);
        unsigned int amount = lit ? 140 : 179;
        // The tabs follow the theme: toward its surface, and the tab of the window with the keys toward its fill.
        unsigned int target = compositor_tab_target(lit);

        for (int row = y; row < y + height; row++)
        {
                const unsigned int *from = view->glass + (long)(row - gy) * gw + (x - gx);
                unsigned int *into = view->pixels + (long)(row - by) * bw + (x - bx);

                for (int at = 0; at < width; at += TASKBAR_TAB_CHUNK)
                {
                        int count = width - at < TASKBAR_TAB_CHUNK ? width - at : TASKBAR_TAB_CHUNK;

                        for (int column = 0; column < count; column++)
                                taskbar_tint[column] = window_mix(compositor_vivid(from[at + column]), target, amount);
                        memory_glass_over_u32(into + at, taskbar_clear, taskbar_tint, (positive)count,
                                              compositor_grain_row(target != 0, (x - bx) + at, row - by));
                }
        }
        return window_mix(middle, target, amount);
}

// Decimal digits of a value from 0 to 999, with no sign.
static void taskbar_digits(char *into, int value)
{
        char backwards[4];
        int count = 0, at = 0;

        do
                backwards[count++] = (char)('0' + value % 10), value /= 10;
        while (value > 0 && count < 4);
        while (count > 0)
                into[at++] = backwards[--count];
        into[at] = 0;
}

// The battery as the tray shows it: its percentage, with a plus in front while it charges.
static void taskbar_battery_text(char *into, int charging, int percent)
{
        int at = 0;

        if (charging)
                into[at++] = '+';
        taskbar_digits(into + at, percent);
        while (into[at])
                at++;
        into[at++] = '%';
        into[at] = 0;
}

/*
        The tray's format, made into the text it says now. A {name} is filled in from the clock, the battery the band
        has read, or a file of /proc or /sys read for it; one whose reading is not there is empty. The names are
        setting_tray_names (src/sh/registry.c), in the order of the cases below.
*/
static void taskbar_tray_put(char *into, int *at, int room, const char *text)
{
        for (int each = 0; text[each] && *at + 1 < room; each++)
                into[(*at)++] = text[each];
        into[*at] = 0;
}

static void taskbar_tray_two(char *into, int *at, int room, int value)
{
        char two[3] = {(char)('0' + value / 10 % 10), (char)('0' + value % 10), 0};

        taskbar_tray_put(into, at, room, two);
}

// The first line of a file, at most room - 1 bytes of it; empty when it can not be read.
static void taskbar_tray_file(const char *path, char *into, int room)
{
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)into, (positive)room - 1);
        int at = 0;

        if (got < 0)
                got = 0;
        while (at < got && into[at] != '\n' && (unsigned char)into[at] >= 0x20)
                at++;
        into[at] = 0;
}

// The file {host} is read from: the kernel's host name. A check points it at a file of its own, as battery_root is.
static const char *taskbar_host_file = "/proc/sys/kernel/hostname";

// A whole number at the start of text, 0 with none.
static long taskbar_tray_number(const char *text)
{
        long value = 0;

        while (*text == ' ')
                text++;
        while (*text >= '0' && *text <= '9' && value < 1000000000000L)
                value = value * 10 + (*text++ - '0');
        return value;
}

// The value of a /proc/meminfo key in kB, 0 when it is not there.
static long taskbar_tray_meminfo(const char *text, const char *key)
{
        int size = 0;

        while (key[size])
                size++;
        for (int at = 0; text[at]; at++)
        {
                int same = 1;

                if (at && text[at - 1] != '\n')
                        continue;
                for (int column = 0; column < size && same; column++)
                        same = text[at + column] == key[column];
                if (same)
                        return taskbar_tray_number(text + at + size);
        }
        return 0;
}

static void taskbar_tray_one(char *into, int *at, int room, int which, const tm *broken)
{
        char word[64];

        word[0] = 0;
        switch (which)
        {
        case 0: // battery
                if (taskbar.battery_item)
                        taskbar_battery_text(word, taskbar.battery_charging, taskbar.battery_percent);
                break;
        case 1: // battery.percent
                if (taskbar.battery_item)
                        taskbar_digits(word, taskbar.battery_percent);
                break;
        case 2: // battery.state
                if (taskbar.battery_item)
                        taskbar_tray_put(word, &(int){0}, (int)sizeof(word), taskbar.battery_charging ? WORDS(tray_charging) : WORDS(tray_discharging));
                break;
        case 3:
                taskbar_tray_two(into, at, room, broken->tm_hour);
                return;
        case 4:
                taskbar_tray_two(into, at, room, broken->tm_min);
                return;
        case 5:
                taskbar_digits(word, broken->tm_hour % 12 ? broken->tm_hour % 12 : 12);
                break;
        case 6:
                taskbar_tray_put(word, &(int){0}, (int)sizeof(word), broken->tm_hour < 12 ? WORDS(tray_am) : WORDS(tray_pm));
                break;
        case 7:
        {
                int year = broken->tm_year + 1900;

                word[0] = (char)('0' + year / 1000 % 10), word[1] = (char)('0' + year / 100 % 10);
                word[2] = (char)('0' + year / 10 % 10), word[3] = (char)('0' + year % 10), word[4] = 0;
                break;
        }
        case 8:
                taskbar_tray_two(into, at, room, broken->tm_mon + 1);
                return;
        case 9:
                taskbar_tray_two(into, at, room, broken->tm_mday);
                return;
        case 10:
        {
                static const unsigned short days[7] = {LANGUAGE_tray_day_sun, LANGUAGE_tray_day_mon, LANGUAGE_tray_day_tue,
                                                       LANGUAGE_tray_day_wed, LANGUAGE_tray_day_thu, LANGUAGE_tray_day_fri,
                                                       LANGUAGE_tray_day_sat};

                taskbar_tray_put(word, &(int){0}, (int)sizeof(word), language_text(days[(unsigned)broken->tm_wday % 7]));
                break;
        }
        case 11:
        {
                static const unsigned short months[12] = {LANGUAGE_tray_month_jan, LANGUAGE_tray_month_feb, LANGUAGE_tray_month_mar,
                                                          LANGUAGE_tray_month_apr, LANGUAGE_tray_month_may, LANGUAGE_tray_month_jun,
                                                          LANGUAGE_tray_month_jul, LANGUAGE_tray_month_aug, LANGUAGE_tray_month_sep,
                                                          LANGUAGE_tray_month_oct, LANGUAGE_tray_month_nov, LANGUAGE_tray_month_dec};

                taskbar_tray_put(word, &(int){0}, (int)sizeof(word), language_text(months[(unsigned)broken->tm_mon % 12]));
                break;
        }
        case 12: // cpu.load: the first word of /proc/loadavg
        {
                taskbar_tray_file("/proc/loadavg", word, (int)sizeof(word));
                for (int each = 0; word[each]; each++)
                        if (word[each] == ' ')
                                word[each] = 0;
                break;
        }
        case 13: // mem.used, in percent
        {
                char info[1024];
                long total, free;
                bipolar got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/meminfo", (p8 address_to)info, sizeof(info) - 1);

                info[got > 0 ? got : 0] = 0;
                total = taskbar_tray_meminfo(info, "MemTotal:");
                free = taskbar_tray_meminfo(info, "MemAvailable:");
                if (total > 0 && free <= total)
                        taskbar_digits(word, (int)((total - free) * 100 / total));
                break;
        }
        case 14: // temp: the hottest thermal zone, in degrees
        {
                long hottest = 0;

                for (int zone = 0; zone < 8; zone++)
                {
                        char path[64], reading[24];
                        int length = 0;
                        long milli;

                        taskbar_tray_put(path, &length, (int)sizeof(path), "/sys/class/thermal/thermal_zone");
                        path[length++] = (char)('0' + zone);
                        path[length] = 0;
                        taskbar_tray_put(path, &length, (int)sizeof(path), "/temp");
                        taskbar_tray_file(path, reading, (int)sizeof(reading));
                        milli = taskbar_tray_number(reading);
                        if (milli > hottest)
                                hottest = milli;
                }
                if (hottest > 0 && hottest < 1000000)
                        taskbar_digits(word, (int)(hottest / 1000));
                break;
        }
        case 15: // host
                taskbar_tray_file(taskbar_host_file, word, 33);
                break;
        default:
                break;
        }
        taskbar_tray_put(into, at, room, word);
}

static void taskbar_tray_render(void)
{
        const char *format = taskbar.tray_format;
        char *into = taskbar.tray_text;
        int room = (int)sizeof(taskbar.tray_text), at = 0;
        timespec now = {0, 0};
        time_t seconds;
        tm broken;

        // The words of the tray are the language the compositor has published in the window table (its program reads /root/language).
        language_take(compositor.table ? compositor.table->language : 0);
        into[0] = 0;
        taskbar.tray_chars = 0;
        if (!taskbar.tray_custom)
                return;
        clock_gettime(CLOCK_REALTIME, address_of now);
        seconds = now.tv_sec;
        if (!localtime_r(&seconds, &broken))
                return;
        for (int each = 0; format[each];)
        {
                // A brace itself, as every character, only where there is room for it and the end: a text the names filled
                // to its end has no room for one more.
                if ((format[each] == '{' || format[each] == '}') && format[each + 1] == format[each])
                {
                        if (at + 1 < room)
                                into[at++] = format[each];
                        into[at] = 0;
                        each += 2;
                }
                else if (format[each] == '{')
                {
                        int close = each + 1;

                        while (format[close] && format[close] != '}')
                                close++;
                        taskbar_tray_one(into, &at, room, setting_tray_name(format + each + 1, (positive)(close - each - 1)), &broken);
                        each = format[close] ? close + 1 : close;
                }
                else
                {
                        if (at + 1 < room)
                                into[at++] = format[each];
                        into[at] = 0;
                        each++;
                }
        }
        for (int each = 0; into[each]; each++)
                if (((unsigned char)into[each] & 0xc0) != 0x80)
                        taskbar.tray_chars++;
}

// `taskbar tray`: the format the file keeps ("" or default is the standard, which is drawn as the tray always was).
static void taskbar_tray_set(const char *text)
{
        char format[SETTING_TRAY_MOST + 1];
        int at = 0;

        while (text[at] && text[at] != '\n' && text[at] != '\r' && at < SETTING_TRAY_MOST)
                format[at] = text[at], at++;
        format[at] = 0;
        taskbar.tray_custom = format[0] && !string_equals((string_address)format, (string_address) "default") &&
                              !string_equals((string_address)format, (string_address)SETTING_TASKBAR_TRAY_STANDARD) &&
                              setting_tray_valid(format, null);
        for (at = 0; format[at] && taskbar.tray_custom; at++)
                taskbar.tray_format[at] = format[at];
        taskbar.tray_format[taskbar.tray_custom ? at : 0] = 0;
        taskbar_tray_render();
        taskbar.stale = 1;
}

// The battery the settings name, read again each minute: its percentage and whether it is charging. The name "none"
// shows nothing; an empty name or "auto" is the first battery the machine has. A battery that is missing, or that
// gives no reading, is not drawn.
static void taskbar_battery_refresh(void)
{
        struct battery_state state;
        const char *name = taskbar.battery;

        taskbar.battery_item = 0;
        if (string_equals((string_address)name, (string_address)"none"))
                return;
        if (string_equals((string_address)name, (string_address)"auto"))
                name = "";
        memory_zero(&state, sizeof(state));
        if (!battery_read(name[0] ? (string_address)name : null, &state) || !state.present || state.percent < 0)
                return;
        taskbar.battery_item = 1;
        taskbar.battery_percent = state.percent;
        taskbar.battery_charging = state.charging == BATTERY_CHARGING;
}

/*
        The launcher button's mark: the apps mark, a grid of three by three round dots, centred on the button. The grid is about
        three quarters of the button across (the dots' pitch 28 percent of the button, a dot 65 percent of the pitch), the dots whole
        device pixels across and apart at every scale, each the kit's fill with a corner of half its width (as the notices dot is), so there is
        no icon file and no font in it. It is recorded into the frame's scene when there is one, as the dot and the speaker are.
*/
static void taskbar_launcher_glyph(const struct taskbar_view *view, const struct taskbar_layout *lay, int x, int y,
                                   int width, int height, unsigned int ink)
{
        struct ui ui;
        int side = width < height ? width : height, step = (side * 28 + 50) / 100, dot, span, left, top;

        dot = (step * 65 + 50) / 100;
        dot = dot < 1 ? 1 : dot;
        step = step < dot + 1 ? dot + 1 : step;
        span = 2 * step + dot;
        left = x - lay->bx + (width - span) / 2;
        top = y - lay->by + (height - span) / 2;
        memory_zero(&ui, sizeof(ui));
        ui.pixels = view->pixels, ui.pitch = (unsigned long)lay->bw, ui.width = lay->bw, ui.height = lay->bh, ui.top = 0, ui.bottom = lay->bh, ui.scale = taskbar_scaled(view->scale);
        ui.scene = view->scene;
        for (int row = 0; row < 3; row++)
                for (int column = 0; column < 3; column++)
                {
                        int at_x = left + column * step, at_y = top + row * step;

                        ui_kit_fill(&ui, &(struct ui_box){at_x * 256, at_y * 256, (at_x + dot) * 256, (at_y + dot) * 256, dot * 128, dot * 128},
                                    ink | 0xff000000u, null);
                }
}

/*
        The volume cell: the speaker (ui_speaker, in the band's own pixels) with no waves, one or two by the volume, or crossed out
        when it is muted; for a moment after a key or the wheel changed the volume, the number instead, as the battery's is.
*/
static void taskbar_volume_cell(const struct taskbar_view *view, const struct taskbar_layout *lay, int x, int y, int width, int height,
                                unsigned int ink, unsigned int ground)
{
        struct ui ui;
        int size = (width < height ? width : height) * 70 / 100, waves = taskbar.volume_muted || !taskbar.volume_percent ? 0 : taskbar.volume_percent < 50 ? 1 : 2;

        if (taskbar.volume_flash)
        {
                char text[8];
                int at = 0;

                if (taskbar.volume_percent >= 100)
                        text[at++] = '1', text[at++] = '0', text[at++] = '0';
                else
                {
                        if (taskbar.volume_percent >= 10)
                                text[at++] = (char)('0' + taskbar.volume_percent / 10);
                        text[at++] = (char)('0' + taskbar.volume_percent % 10);
                }
                text[at++] = '%', text[at] = 0;
                if (taskbar.volume_muted)
                        text[0] = '-', text[1] = '-', text[2] = 0;
                if (lay->vertical)
                        taskbar_text_turned(view, lay->bx, lay->by, lay->bw, x, y, width, height, text, ink, ground);
                else
                        taskbar_text_line(view, lay->bx, lay->by, lay->bw, x, y, width, height, text, ink, ground);
                return;
        }
        memory_zero(&ui, sizeof(ui));
        ui.pixels = view->pixels, ui.pitch = (unsigned long)lay->bw, ui.width = lay->bw, ui.height = lay->bh, ui.top = 0, ui.bottom = lay->bh, ui.scale = taskbar_scaled(view->scale);
        ui.scene = view->scene;
        ui_speaker(&ui, x - lay->bx + (width - size) / 2, y - lay->by + (height - size) / 2, size, waves, taskbar.volume_muted, ink | 0xff000000u);
}

/*
        The microphone cell: the microphone (ui_microphone, in the band's own pixels) in the error colour, so that a program recording is
        never silent in the tray, and struck when the microphone is muted.
*/
static void taskbar_mic_cell(const struct taskbar_view *view, const struct taskbar_layout *lay, int x, int y, int width, int height)
{
        struct ui ui;
        int size = (width < height ? width : height) * 70 / 100;

        memory_zero(&ui, sizeof(ui));
        ui.pixels = view->pixels, ui.pitch = (unsigned long)lay->bw, ui.width = lay->bw, ui.height = lay->bh, ui.top = 0, ui.bottom = lay->bh, ui.scale = taskbar_scaled(view->scale);
        ui.scene = view->scene;
        ui_microphone(&ui, x - lay->bx + (width - size) / 2, y - lay->by + (height - size) / 2, size, taskbar.mic_muted, compositor_ink_role(UI_ERROR) | 0xff000000u);
}

/*
        The notices dot: the kit's status dot (ui_kit_dot, the size a badge's dot is) in the badge colour of the worst notice, the error's
        red or the warning's amber, centred in its cell. It is drawn only while the board holds one (taskbar_notices_set).
*/
static void taskbar_notices_cell(const struct taskbar_view *view, const struct taskbar_layout *lay, int x, int y, int width, int height)
{
        struct ui ui;
        unsigned int colour = compositor_ink_role(taskbar.notices_severity == WINDOWS_ISSUE_ERROR ? UI_ERROR : UI_WARNING);
        int size;

        memory_zero(&ui, sizeof(ui));
        ui.pixels = view->pixels, ui.pitch = (unsigned long)lay->bw, ui.width = lay->bw, ui.height = lay->bh, ui.top = 0, ui.bottom = lay->bh, ui.scale = taskbar_scaled(view->scale);
        ui.scene = view->scene;
        size = ui_px(&ui, UI_DOT);
        ui_kit_dot(&ui, x - lay->bx + (width - size) / 2, y - lay->by + (height - size) / 2, colour | 0xff000000u);
}

/*
        The band drawn over what is under it: each tab redone made glass with
        its title on it, and the tray made glass with the clock and the
        battery. Only the slots in redo are made again, and only their span
        is damaged. Answers 1 with rect set to the damage, or 0 for none. The
        glass is made by glass over the span: it answers the row stride, or 0.
*/
static int taskbar_draw(const struct taskbar_view *view, unsigned long redo, taskbar_glass_fn glass, void *context,
                        int *rect)
{
        struct taskbar_layout lay;
        int first = 0, last = 0, any = 0, gw, x, y, width, height, time_ok;
        timespec now = {0, 0};
        time_t seconds;
        tm broken;
        unsigned int ground;
        unsigned int ink;

        taskbar.stale = 0;
        if (!view->pixels)
                return 0;
        taskbar_layout(view, &lay);
        if ((unsigned long)lay.bw * (unsigned long)lay.bh * 4 > view->band_bytes)
                return 0;
        for (int which = TASKBAR_LAUNCHER; which < lay.count; which++)
        {
                int along, length;

                if (which == -4 || which == -3)
                        continue;
                if ((redo & taskbar_bit(which)) && taskbar_along(&lay, which, &along, &length))
                {
                        if (!any || along < first)
                                first = along;
                        if (!any || along + length > last)
                                last = along + length;
                        any = 1;
                }
        }
        if (!any)
                return 0;
        taskbar_span_rect(&lay, first, last, rect);
        gw = glass(context, rect[0], rect[1], rect[2], rect[3]);
        if (!gw)
                return 1;

        for (int at = 0; at < lay.count; at++)
        {
                if ((redo & taskbar_bit(at)) && taskbar_slot(view, at, &x, &y, &width, &height))
                {
                        ground = taskbar_glass_tab(view, rect[0], rect[1], gw, lay.bx, lay.by, lay.bw, x, y, width,
                                                   height, at, view->marks[at]);
                        ink = window_contrast(ground);
                        ink = view->marks[at] & COMPOSITOR_TAB_LIT    ? ink
                              : view->marks[at] & COMPOSITOR_TAB_AWAY ? window_mix(ground, ink, 110)
                                                                      : window_mix(ground, ink, 170);

                        // A band on a side writes its titles turned, a bottom or top one upright.
                        if (lay.vertical)
                                taskbar_text_turned(view, lay.bx, lay.by, lay.bw, x, y, width, height, view->titles[at],
                                                    ink, ground);
                        else
                                taskbar_text_line(view, lay.bx, lay.by, lay.bw, x, y, width, height, view->titles[at],
                                                  ink, ground);
                }
        }

        // The launcher button: a tab's glass, lit while the launcher is up, with its grid of dots in the tab's ink.
        if ((redo & taskbar_bit(TASKBAR_LAUNCHER)) && taskbar_slot(view, TASKBAR_LAUNCHER, &x, &y, &width, &height))
        {
                unsigned int mark = view->launcher_lit ? COMPOSITOR_TAB_LIT : 0;

                ground = taskbar_glass_tab(view, rect[0], rect[1], gw, lay.bx, lay.by, lay.bw, x, y, width, height,
                                           TASKBAR_LAUNCHER, mark);
                ink = window_contrast(ground);
                ink = mark ? ink : window_mix(ground, ink, 170);
                taskbar_launcher_glyph(view, &lay, x, y, width, height, ink);
        }

        taskbar.minute = taskbar_minute_now();
        clock_gettime(CLOCK_REALTIME, address_of now);
        seconds = now.tv_sec;
        time_ok = localtime_r(&seconds, &broken) != 0;
        if ((redo & taskbar_bit(-2)) && time_ok && taskbar_slot(view, -2, &x, &y, &width, &height))
        {
                char clock[6] = {(char)('0' + broken.tm_hour / 10), (char)('0' + broken.tm_hour % 10), ':',
                                 (char)('0' + broken.tm_min / 10), (char)('0' + broken.tm_min % 10), 0};
                char text[8];

                ground = taskbar_glass_tab(view, rect[0], rect[1], gw, lay.bx, lay.by, lay.bw, x, y, width, height, -2, 0);
                ink = window_contrast(ground);
                // A format of the person's is one text over the whole tray.
                if (taskbar.tray_custom)
                {
                        if (lay.vertical)
                                taskbar_text_turned(view, lay.bx, lay.by, lay.bw, x, y, width, height, taskbar.tray_text, ink, ground);
                        else
                                taskbar_text_line(view, lay.bx, lay.by, lay.bw, x, y, width, height, taskbar.tray_text, ink, ground);
                }
                // The clock has the tray's far slot, past the battery's when one is shown; a side band writes both
                // turned, as its titles.
                else
                {
                        int cx, cy, cw, ch;

                        if (taskbar_slot(view, -3, &cx, &cy, &cw, &ch))
                        {
                                if (lay.vertical)
                                        taskbar_text_turned(view, lay.bx, lay.by, lay.bw, cx, cy, cw, ch, clock, ink, ground);
                                else
                                        taskbar_text_line(view, lay.bx, lay.by, lay.bw, cx, cy, cw, ch, clock, ink, ground);
                        }
                }
                if (taskbar.battery_item && !taskbar.tray_custom)
                {
                        int ax, ay, aw, ah;

                        taskbar_battery_text(text, taskbar.battery_charging, taskbar.battery_percent);
                        if (taskbar_slot(view, -4, &ax, &ay, &aw, &ah))
                        {
                                if (lay.vertical)
                                        taskbar_text_turned(view, lay.bx, lay.by, lay.bw, ax, ay, aw, ah, text, ink, ground);
                                else
                                        taskbar_text_line(view, lay.bx, lay.by, lay.bw, ax, ay, aw, ah, text, ink, ground);
                        }
                }
                if (taskbar_slot(view, TASKBAR_VOLUME, &x, &y, &width, &height))
                        taskbar_volume_cell(view, &lay, x, y, width, height, ink, ground);
                if (taskbar_slot(view, TASKBAR_MIC, &x, &y, &width, &height))
                        taskbar_mic_cell(view, &lay, x, y, width, height);
                if (taskbar_slot(view, TASKBAR_NOTICES, &x, &y, &width, &height))
                        taskbar_notices_cell(view, &lay, x, y, width, height);
        }
        return 1;
}

/*
        The tray's clock has changed minute, or the battery came or went: the
        band is drawn whole. Answers 1 when the minute has changed; the battery is
        read again then.
*/
static int taskbar_tick(void)
{
        unsigned int now = taskbar_minute_now(), language_now = compositor.table ? compositor.table->language : 0;

        // A language taken is the tray's words in it (a weekday, a month, AM and PM) at once, and not at the minute's end.
        if (now == taskbar.minute && language_now == taskbar.language)
                return 0;
        taskbar.language = language_now;
        taskbar.minute = now;
        taskbar_battery_refresh();
        taskbar_tray_render();
        return 1;
}

static void taskbar_changed(void)
{
        taskbar.stale = 1;
}

static int taskbar_stale(void)
{
        return taskbar.stale;
}

// The memory the band's pixels and its glass take at most, for any position at this output and scale.
static void taskbar_bytes(int width, int height, int scale, unsigned long *band, unsigned long *glass)
{
        // The scale is percent: whole device pixels of a logical one, rounded up, so these bound what is drawn. The band is as thick as the
        // taskbar's own scale makes it; the blur's reach is the screen's.
        int px = (scale + 99) / 100;
        int thick = COMPOSITOR_PANEL * ((taskbar_scaled(scale) + 99) / 100), across = COMPOSITOR_GLASS_ACROSS * px, down = COMPOSITOR_GLASS_DOWN * px;
        long along = width > height ? width : height;
        long wide = (long)(width + 2 * across) * (long)(thick + 2 * down);
        long tall = (long)(thick + 2 * across) * (long)(height + 2 * down);

        *band = (unsigned long)along * (unsigned long)thick * 4;
        *glass = (unsigned long)(wide > tall ? wide : tall) * 4;
}

static unsigned int taskbar_position(void)
{
        return taskbar.position;
}

// The position the band has when no setting keeps one: -t puts it on top.
static void taskbar_position_default(unsigned int position)
{
        taskbar.position_default = position;
        taskbar.position = position;
}

/*
        The value of a key in the kept settings: a line "key value", as the battery verb keeps
        /root/tune. The first such line wins; with no line, the value is empty (auto).
*/
static void taskbar_tune_value(const char *text, const char *key, char *into, int room)
{
        int at = 0, size = 0;

        into[0] = 0;
        while (key[size])
                size++;
        while (text[at])
        {
                int line = at, same = 1;

                for (int column = 0; column < size && same; column++)
                        same = text[line + column] == key[column];
                while (text[at] && text[at] != '\n')
                        at++;
                if (same && text[line + size] == ' ')
                {
                        int value = line + size + 1, length = 0;

                        while (value + length < at && length < room - 1)
                                into[length] = text[value + length], length++;
                        into[length] = 0;
                        return;
                }
                if (text[at])
                        at++;
        }
}

// The settings: the position and the display (the words their files hold, "" when none), and the battery named in
// the kept settings text (/root/tune). "none" is no battery item; "" or "auto" is the first battery present.
static void taskbar_settings_set(const char *position, const char *display, const char *tune)
{
        unsigned int want = taskbar.position_default;
        char battery[BATTERY_NAME];
        int at = 0;

        if (string_equals((string_address)position, (string_address)"bottom"))
                want = TASKBAR_BOTTOM;
        else if (string_equals((string_address)position, (string_address)"top"))
                want = TASKBAR_TOP;
        else if (string_equals((string_address)position, (string_address)"left"))
                want = TASKBAR_LEFT;
        else if (string_equals((string_address)position, (string_address)"right"))
                want = TASKBAR_RIGHT;
        taskbar.position = want;
        taskbar.all = string_equals((string_address)display, (string_address)"all");
        taskbar_tune_value(tune, "battery", battery, sizeof(battery));
        while (at < BATTERY_NAME - 1 && battery[at])
                taskbar.battery[at] = battery[at], at++;
        taskbar.battery[at] = 0;
        taskbar_battery_refresh();
        taskbar.stale = 1;
}

static int taskbar_display_all(void)
{
        return taskbar.all;
}

/*
        What the sound server says of the master volume, for the volume cell: present when the server is there (else the cell is
        not), the percent, whether it is muted, and flash when the person has just changed it. Answers 1 when the tray is to be
        drawn again for it.
*/
static int taskbar_volume_set(int present, int percent, int muted, int flash)
{
        int changed;

        present = present != 0, muted = muted != 0, flash = flash != 0;
        percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
        changed = present != taskbar.volume_item || (present && (percent != taskbar.volume_percent || muted != taskbar.volume_muted || flash != taskbar.volume_flash));
        taskbar.volume_item = present, taskbar.volume_percent = percent, taskbar.volume_muted = muted, taskbar.volume_flash = flash;
        if (changed)
                taskbar.stale = 1;
        return changed;
}

/*
        Whether a program records from a microphone (or the microphone's mute was just thrown), for the microphone cell: present when
        the cell is there, and whether the microphone is muted. Answers 1 when the tray is to be drawn again for it.
*/
static int taskbar_mic_set(int present, int muted)
{
        int changed;

        present = present != 0, muted = muted != 0;
        changed = present != taskbar.mic_item || (present && muted != taskbar.mic_muted);
        taskbar.mic_item = present, taskbar.mic_muted = muted;
        if (changed)
                taskbar.stale = 1;
        return changed;
}

/*
        Whether the board holds a notice or an issue, and the worst of them (WINDOWS_ISSUE_ERROR or WINDOWS_ISSUE_WARNING), for the notices
        dot: present puts the dot in the tray, absent takes it out. Answers 1 when the tray is to be drawn again for it.
*/
static int taskbar_notices_set(int present, unsigned int severity)
{
        int changed;

        present = present != 0;
        severity = present && severity == WINDOWS_ISSUE_ERROR ? WINDOWS_ISSUE_ERROR : present ? WINDOWS_ISSUE_WARNING : 0;
        changed = present != taskbar.notices_item || severity != taskbar.notices_severity;
        taskbar.notices_item = present, taskbar.notices_severity = severity;
        if (changed)
                taskbar.stale = 1;
        return changed;
}

// `taskbar text`: edge turns a side band's letters' tops toward the edge it is on, else toward the middle.
static void taskbar_text_set(int edge)
{
        taskbar.edge = edge != 0;
        taskbar.stale = 1;
}

// `taskbar launcher`: on puts the launcher button at the band's leading end, and the tabs are laid out in what it leaves.
static void taskbar_launcher_set(int on)
{
        taskbar.launcher = on != 0;
        taskbar.stale = 1;
}

// `taskbar scale`: the band, and what is drawn in it, at this percent of its screen's scale (TASKBAR_SCALE_LEAST to TASKBAR_SCALE_MOST, under
// 100 a band and a text smaller than the screen's own; anything else is the standard). Its thickness changes with it, so the compositor reads its rectangle and its memory again (compositor_taskbar_settings_read).
static void taskbar_scale_set(int percent)
{
        taskbar.scale = percent >= TASKBAR_SCALE_LEAST && percent <= TASKBAR_SCALE_MOST ? percent : TASKBAR_SCALE_STANDARD;
        taskbar.stale = 1;
}

// `taskbar padding`: the space between the band's items and at its ends, in logical pixels at the taskbar's scale (0 to
// TASKBAR_PADDING_MOST; anything else is the standard).
static void taskbar_padding_set(int pixels)
{
        taskbar.padding = pixels >= 0 && pixels <= TASKBAR_PADDING_MOST ? pixels : TASKBAR_PADDING_STANDARD;
        taskbar.stale = 1;
}

// `taskbar fullscreen`: on, the band slides off its edge while a window is fullscreen (taskbar_slide_now).
static void taskbar_hide_set(int on)
{
        taskbar.hide_fullscreen = on != 0;
}

// The same for a cell of the tray: where a popup opens beside, the cell's length along the edge and the band's whole thickness across it.
static int taskbar_cell_anchor(const struct taskbar_view *view, int which, int *x, int *y, int *width, int *height)
{
        struct taskbar_layout lay;
        int along, length;

        taskbar_layout(view, &lay);
        if (!taskbar_along(&lay, which, &along, &length))
                return 0;
        if (lay.vertical)
                *x = lay.bx, *y = along, *width = lay.bw, *height = length;
        else
                *x = along, *y = lay.by, *width = length, *height = lay.bh;
        return 1;
}

static int taskbar_volume_anchor(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        return taskbar_cell_anchor(view, TASKBAR_VOLUME, x, y, width, height);
}

static int taskbar_mic_anchor(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        return taskbar_cell_anchor(view, TASKBAR_MIC, x, y, width, height);
}

static int taskbar_notices_anchor(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        return taskbar_cell_anchor(view, TASKBAR_NOTICES, x, y, width, height);
}

/*
        Where a menu from the launcher button opens, as the rectangle it opens beside: the button's length along the edge
        and the band's whole thickness across it, on the output. A bottom band's menu ends where this starts, a top band's
        starts where it ends, and a side band's is beside it, its top at this top. Answers 0 when the band has no button.
*/
static int taskbar_anchor(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        struct taskbar_layout lay;
        int along, length;

        taskbar_layout(view, &lay);
        if (!taskbar_along(&lay, TASKBAR_LAUNCHER, &along, &length))
                return 0;
        if (lay.vertical)
                *x = lay.bx, *y = along, *width = lay.bw, *height = length;
        else
                *x = along, *y = lay.by, *width = length, *height = lay.bh;
        return 1;
}
