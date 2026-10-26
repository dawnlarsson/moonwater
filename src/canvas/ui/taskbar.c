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
        the tabs centred in what the tray leaves; a left or right band runs top to
        bottom, with the tray at the bottom, the battery above the clock. A band
        on a side writes its titles and its tray turned a quarter, the tops of
        the letters toward the middle of its output or toward the edge it is
        on, as `taskbar text` says.

        Every rectangle here is on the output and in device pixels. The painting
        works in the band's own coordinates (the pixels start at its top left) and
        the glass in the rectangle the compositor blurred for the span.
*/

// The taskbar's own state: what the settings say, the minute the tray shows, and the battery it has read.
static struct taskbar_state
{
        int stale;                              // the band is drawn whole at the next present
        unsigned int minute;                    // the minute the tray shows
        unsigned int position_default;          // where the band sits when no setting keeps one (-t: top)
        unsigned int position;                  // where it sits now
        int all;                                // `taskbar display all`: not supported, drawn on the one display
        char battery[BATTERY_NAME];      // the settings' word: "" or "auto" for the first battery found, "none", or a name
        int battery_item;                       // the battery is shown: its row is not none and a battery was read
        int battery_percent, battery_charging;  // what it read
        int edge;                               // a side band's letters have their tops toward its edge, not the middle
} taskbar = {1, 0, TASKBAR_BOTTOM, TASKBAR_BOTTOM, 0, {0}, 0, 0, 0};

// A tab's glass is tinted a chunk at a time into this, and grained over no picture at all.
#define TASKBAR_TAB_CHUNK 256
static unsigned int taskbar_tint[TASKBAR_TAB_CHUNK];
static const unsigned int taskbar_clear[TASKBAR_TAB_CHUNK];

// What the band takes from compositor.c, which comes before this file in every build and defines them: the meet of two
// rectangles, the grain a glass row is made with, and a colour made more vivid.
static void compositor_meet(int *into, const int *other);
static const unsigned int *compositor_grain_row(unsigned int light, int x, int y);
static unsigned int compositor_vivid(unsigned int pixel);

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

// The band: along the edge its position names, COMPOSITOR_PANEL logical pixels thick.
static void taskbar_band(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        int thick = COMPOSITOR_PANEL * view->scale;

        switch (view->position)
        {
        case TASKBAR_TOP:
                *x = 0, *y = 0, *width = view->width, *height = thick;
                break;
        case TASKBAR_LEFT:
                *x = 0, *y = 0, *width = thick, *height = view->height;
                break;
        case TASKBAR_RIGHT:
                *x = view->width - thick, *y = 0, *width = thick, *height = view->height;
                break;
        default:
                *x = 0, *y = view->height - thick, *width = view->width, *height = thick;
                break;
        }
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
        return taskbar_text(high, 5) * (taskbar.battery_item ? 2 : 1);
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
        lay->gap = 4 * view->scale;
        lay->inset = 3 * view->scale;
        lay->high = lay->thick - 3 * view->scale;
        lay->count = (int)view->count;
        lay->lengths = view->lengths;
        lay->tray = taskbar_tray(lay->high);
        room = lay->length - lay->tray - (lay->count + 2) * lay->gap;
        lay->most = lay->count && room > 0 ? room / lay->count : 0;
        lay->most = lay->most > 8 * lay->thick ? 8 * lay->thick : lay->most;
        for (int at = 0; lay->most && at < lay->count; at++)
                windows += taskbar_tab_width(lay, at) + lay->gap;
        lay->windows = windows;
        // Along a bottom or top band the row is centred; along a side one the tabs start below a gap at the top and
        // the tray is held to the bottom.
        lay->start = lay->vertical ? lay->origin + lay->gap : lay->origin + (lay->length - (windows + lay->tray)) / 2;
        lay->tray_at = lay->vertical ? lay->origin + lay->length - lay->gap - lay->tray : lay->start + windows;
        lay->across = lay->vertical ? (view->position == TASKBAR_LEFT ? lay->bx : lay->bx + lay->inset)
                                    : (view->position == TASKBAR_TOP ? lay->by : lay->by + lay->inset);
        lay->across_len = lay->thick - lay->inset;
}

/*
        Where a slot is along the edge: its start and length. Slot -2 is the
        tray as a whole, -3 the clock and -4 the battery (both inside the tray).
        Answers 0 for a slot that has none: a tab past the count or too narrow
        for one, or the battery when none is shown.
*/
static int taskbar_along(const struct taskbar_layout *lay, int which, int *along, int *length)
{
        int text = taskbar_text(lay->high, 5), at;

        if (which >= 0)
        {
                if (which >= lay->count || lay->most < 1)
                        return 0;
                *along = lay->start;
                for (at = 0; at < which; at++)
                        *along += taskbar_tab_width(lay, at) + lay->gap;
                *length = taskbar_tab_width(lay, which);
        }
        else if (which == -2)
                *along = lay->tray_at, *length = lay->tray;
        else if (which == -3)
                *along = lay->tray_at + (taskbar.battery_item ? text : 0), *length = text;
        else if (which == -4)
        {
                if (!taskbar.battery_item)
                        return 0;
                *along = lay->tray_at, *length = text;
        }
        else
                return 0;
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

// What is at a point of the band: a tab's number, -2 for the tray, or TASKBAR_HIT_NONE.
static int taskbar_hit(const struct taskbar_view *view, int px, int py)
{
        for (int which = -2; which < (int)view->count; which++)
        {
                int x, y, width, height;

                if (taskbar_slot(view, which, &x, &y, &width, &height) &&
                    px >= x && px < x + width && py >= y && py < y + height)
                        return which;
        }
        return TASKBAR_HIT_NONE;
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
                first = tab;
        taskbar_span_rect(&lay, first, last, rect);
        *x = rect[0], *y = rect[1], *width = rect[2], *height = rect[3];
        return 1;
}

// A slot's redo bit: a tab's, and the tray's on its own bit past the tabs.
static unsigned long taskbar_bit(int which)
{
        return which < 0 ? 1ul << WINDOWS : 1ul << which;
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
        for (int which = -2; which < lay.count; which++)
        {
                int x, y, width, height, reach[4];

                if (!taskbar_slot(view, which, &x, &y, &width, &height))
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
        return slot < 0                 ? COMPOSITOR_INK_BUTTON
               : mark & COMPOSITOR_TAB_AWAY ? COMPOSITOR_INK_PANEL
               : mark & COMPOSITOR_TAB_LIT  ? COMPOSITOR_INK_BUTTON_LIT
                                            : COMPOSITOR_INK_BUTTON;
}

// The corners the tabs keep square: the ones on the edge the band is along, so the round ones face the middle.
static unsigned int taskbar_square(unsigned int position)
{
        return position == TASKBAR_TOP ? 1 | 2 : position == TASKBAR_LEFT ? 1 | 4 : position == TASKBAR_RIGHT ? 2 | 8 : 4 | 8;
}

// One line of text in the band, at band-local x, y of its rectangle, in the tab's colour for ground and cut off at its edge.
static void taskbar_text_line(const struct taskbar_view *view, int bx, int by, int bw, int x, int y, int width,
                              int height, const char *text, unsigned int ink, unsigned int ground)
{
        const font_face address_to faces[1] = {view->face};
        p32 em = (p32)(height * 11 / 20) << 6;
        font_metrics grid = font_metrics_at(view->face, em);
        font_target here = {view->pixels + (long)(y - by) * bw + (x - bx), (positive)bw, width, height};
        unsigned int scratch[1] = {0};
        font_target nowhere = {scratch, 1, 1, 1};
        positive length = string_length((string_address)text);
        int pad;

        // How wide the run is, before it is put anywhere.
        pad = (width - (int)(font_draw(view->engine, faces, 1, (const p8 address_to)text, length, em, 0,
                                       &nowhere, 0, 0, ink, ground) >> 16)) / 2;
        font_draw(view->engine, faces, 1, (const p8 address_to)text, length, em, 0, &here,
                  (b32)(pad > 0 ? pad : 0) << 16, (height - grid.cell_height) / 2 + grid.baseline, ink, ground);
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
        A text cut to room pixels as the settings cut a label: whole when it
        fits, else the longest beginning that ends before a character and fits
        with "..." after it, found by halving (a beginning grows with its
        length), else its first character alone. Written to fitted; answers
        its length.
*/
static positive taskbar_fit(const struct taskbar_view *view, const font_face address_to const address_to faces,
                            const char *text, p32 em, int room, char *fitted, positive capacity)
{
        positive length = string_length((string_address)text), low = 1, high, best = 0, one = 1;

        length = length + 4 > capacity ? capacity - 4 : length;
        memory_copy(fitted, text, length);
        if (length <= 1 || (int)(font_extent(view->engine, faces, 1, (const p8 address_to)text, length, em, 0) >> 16) <= room)
                return length;
        high = length - 1;
        while (low <= high)
        {
                positive middle = (low + high) / 2, at = middle;

                while (at && ((unsigned char)text[at] & 0xc0) == 0x80)
                        at--;
                memory_copy(fitted, text, at);
                memory_copy(fitted + at, "...", 3);
                if (at && (int)(font_extent(view->engine, faces, 1, (const p8 address_to)fitted, at + 3, em, 0) >> 16) <= room)
                        best = at, low = middle + 1;
                else
                        high = middle - 1;
        }
        memory_copy(fitted, text, length);
        if (best)
        {
                memory_copy(fitted + best, "...", 3);
                return best + 3;
        }
        while (one < length && ((unsigned char)text[one] & 0xc0) == 0x80)
                one++;
        return one;
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
        const font_face address_to faces[1] = {view->face};
        p32 em = (p32)(width * 11 / 20) << 6;
        font_metrics grid = font_metrics_at(view->face, em);
        font_target here = {view->pixels + (long)(y - by) * bw + (x - bx), (positive)bw, width, height};
        char fitted[WINDOWS_TITLE + 4];
        int capital = (int)((grid.cap_height + 32768) >> 16), pad;
        positive length = string_length((string_address)text);
        int advance = (int)(font_extent(view->engine, faces, 1, (const p8 address_to)text, length, em, 0) >> 16);

        if (advance > height)
        {
                length = taskbar_fit(view, faces, text, em, height - (width - capital), fitted, sizeof(fitted));
                text = fitted;
                advance = (int)(font_extent(view->engine, faces, 1, (const p8 address_to)text, length, em, 0) >> 16);
        }
        pad = (height - advance) / 2;
        font_draw_turned(view->engine, faces, 1, (const p8 address_to)text, length, em, 0, taskbar_turn(view->position),
                         &here, (b32)(pad > 0 ? pad : 0) << 16, (width - grid.cell_height) / 2 + grid.baseline, ink,
                         ground);
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
        unsigned int middle = compositor_vivid(view->glass[(long)(y + height / 2 - gy) * gw + x - gx + width / 2]);
        unsigned int target = window_light(middle) ? 0xffffffu : 0x000000u;
        unsigned int amount = slot >= 0 && (mark & COMPOSITOR_TAB_LIT) ? 140 : 179;

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
        for (int which = -2; which < lay.count; which++)
        {
                int along, length;

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
                // The clock has the tray's far slot, past the battery's when one is shown; a side band writes both
                // turned, as its titles.
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
                if (taskbar.battery_item)
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
        unsigned int now = taskbar_minute_now();

        if (now == taskbar.minute)
                return 0;
        taskbar.minute = now;
        taskbar_battery_refresh();
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
        int thick = COMPOSITOR_PANEL * scale, across = COMPOSITOR_GLASS_ACROSS * scale, down = COMPOSITOR_GLASS_DOWN * scale;
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

// `taskbar text`: edge turns a side band's letters' tops toward the edge it is on, else toward the middle.
static void taskbar_text_set(int edge)
{
        taskbar.edge = edge != 0;
        taskbar.stale = 1;
}
