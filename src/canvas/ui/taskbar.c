/*
        The taskbar's layout, hit test, damage and painting (the taskbar's part of ui/ui.c says what
        it is and what the compositor gives it).

        Along the edge the band runs in (its length) the tabs come first, in the
        order the windows showed, each as long as its title needs at a width a
        character has, and the tray is at the far end: the clock, and the battery
        beside it when the settings name one, and the volume, microphone and notices
        cells when they are there. Across the band a tab is its
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

        With `taskbar launcher on` the band also has a tab at its leading end (the left of a bottom or top band, the top of a
        side one) with the launcher's mark, a grid of nine dots, and no title: a tab as high as the window tabs, in the same glass,
        corners and ink (lit while the launcher is up, as a window's tab is while it has the keys), as long as its mark and the
        padding either side of it, that opens the launcher. It is a slot of the band with a bit of its own in the redo mask,
        and the tabs are laid out in what it leaves. Off, which is the default, the band is as it was.

        None of that is worked out here: the band is declared as a tree of the kit's layout (taskbar_lay, below), and where a
        slot is, what a point is over, what a change under the band reaches and where each thing is painted are read from the
        rectangles the layout gives the tree.

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
        int padding;                            // `taskbar padding`: logical pixels, at that scale, round the text inside each tab, either side
} taskbar = {1, 0, 0, TASKBAR_BOTTOM, TASKBAR_BOTTOM, 0, {0}, 0, 0, 0, .scale = TASKBAR_SCALE_STANDARD, .padding = TASKBAR_PADDING_STANDARD};

_Static_assert(TASKBAR_GAP == UI_GAP1, "the gap between the band's items is the kit's gap between pieces that go together");

// A tab's glass is tinted a chunk at a time into this, and grained over no picture at all.
#define TASKBAR_TAB_CHUNK 256
static unsigned int taskbar_tint[TASKBAR_TAB_CHUNK];
static const unsigned int taskbar_clear[TASKBAR_TAB_CHUNK];

// What the band takes from compositor.c, which comes before this file in every build and defines them: the meet of two
// rectangles and one grown to take in another, the grain a glass row is made with, and a colour made more vivid.
static void compositor_meet(int *into, const int *other);
static void compositor_union(int *into, const int *other);
static const unsigned int *compositor_grain_row(unsigned int light, int x, int y);
static unsigned int compositor_vivid(unsigned int pixel);

static inline b32 language_rtl(void); // the language taken reads from the right (ui/ui.c)

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

// How far a band at a shift is moved from its place across its edge, in device pixels: off that edge, which is toward the end of
// its output for a bottom or a right band and toward the start of it for a top or a left one (UI_ONE is all of its thickness,
// which is off the output).
static int taskbar_off(const struct taskbar_view *view, b64 shift)
{
        int thick = taskbar_thick(view->scale);
        int off = (int)(((long long)thick * (long long)shift) / (long long)UI_ONE);

        return view->position == TASKBAR_BOTTOM || view->position == TASKBAR_RIGHT ? off : -off;
}

/*
        The slide (`taskbar fullscreen on`): while a window is fullscreen the band moves off its edge, and back when none is. The
        move is the kit's window motion (UI_WINDOW_NS at the pace, ui_paced, on --ease: the glide a window makes), from where the band is when it is
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
        done = ui_bezier_at(&ui_ease, ui_progress((b64)(now - taskbar.slide_at), ui_paced(TASKBAR_SLIDE_NS)));
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
        return taskbar.slide_at && now > taskbar.slide_at && now - taskbar.slide_at < (positive)ui_paced(TASKBAR_SLIDE_NS);
}

// How far off its edge the band is now.
static b64 taskbar_shift(void)
{
        return taskbar.shift;
}

/*
        A run of characters' width at the text's height: a character a little
        over half of it, and the padding (`taskbar padding`, at the band's
        scale) either side of the run, which is the room round the text inside
        the tab. Never narrower than the text is high.
*/
static int taskbar_text(int high, int pad, int characters)
{
        int wanted = characters * high * 308 / 1000 + 2 * pad;

        return wanted < high ? high : wanted;
}

/*
        The standard tray's cells, each written once, from the tray's start along the edge to its far end: the slot it answers to,
        what says it is there now, the side of the mark in its middle (0 for a cell of text; taskbar_lay's names: the scale, and
        glyph, the side of the speaker and the microphone) and what draws it (taskbar_draw's names: the view, the tree t, the cell's
        element n, the ink and ground, the clock and the battery's text). The table is the cells of the tray in the band's tree
        (taskbar_lay), how many they are, which of them the tree was made with (taskbar_tree) and the drawing of each
        (taskbar_draw), all at once.
*/
#define TASKBAR_TRAY_CELLS(CELL)                                                                                          \
        CELL(TASKBAR_NOTICES, taskbar.notices_item, (UI_DOT * scale + 50) / 100, taskbar_notices_cell(view, t, n))       \
        CELL(TASKBAR_MIC, taskbar.mic_item, glyph, taskbar_mic_cell(view, t, n))                                        \
        CELL(TASKBAR_VOLUME, taskbar.volume_item, glyph, taskbar_volume_cell(view, t, n, ink, ground))                  \
        CELL(-4, taskbar.battery_item && !taskbar.tray_custom, 0, taskbar_say(view, t, n, text, ink, ground))           \
        CELL(-3, !taskbar.tray_custom, 0, taskbar_say(view, t, n, clock, ink, ground))

/*
        The band as the kit's layout has it (ui_tree, ui/ui.c). The root is the band's output, where it is in the desktop's
        space (and moved off its edge with the band while the band slides), and the band is its one child, element 1: at the
        start or the end of the output across the edge, as thick as its scale makes it and as long as the output, with the inset
        on its side toward the middle of the output. The band holds three boxes along the edge:

            the lead    the launcher's button, its mark in its middle: three rows of three dots
            the tabs    a tab for each window, as long as its title needs
            the tail    the tray, at the far end: its cells, the speaker, the microphone and the dot each with its mark in its
                        middle, or the one text of a format of the person's

        Each thing has the gap before it, and the band has the gap at its far end. Along a bottom or top band the lead and the tail
        each ask for all of the band and give way alike (ui/ui.c: a size that gives way), so they are as long as each other and the
        tabs between them are in the middle of the band itself, whatever the button and the tray take of it; one that has come down
        to what it holds gives no more, and only then are the tabs off the middle, by as much as they would otherwise run into it.
        Along a side band the lead is what it holds, so the tabs start below it. Reading from the right, a bottom or top band is
        the same band turned over its length (ui_tree_mirror).

        What the layout does not work out is said first, as numbers. The launcher mark's grid in a tab: the pitch of its dots (28
        percent of the tab's height) and a dot (65 percent of the pitch, a pixel at least, a pixel apart at least), and the tab as
        long as the three of a row and the padding either side, never under its height (as a tab of text is: taskbar_text). The
        tray, held to half the band (the tabs keep the rest) and to what the launcher and its gaps leave of it: a format of the
        person's is one text, cut to that, and the standard tray's cells are each narrower when they would not all fit. And a tab,
        no longer than an equal share of what the button and the tray leave, nor than eight times the band's thickness.

        An element is named for its slot, the slot plus one, so that 0, the name of an element given none, is no slot. A slot is
        a tab's number (0 up), TASKBAR_TRAY (-2) for the tray as a whole, -3 the clock, -4 the battery, TASKBAR_LAUNCHER (-5),
        TASKBAR_VOLUME (-6), TASKBAR_MIC (-7) and TASKBAR_NOTICES (-8). A slot that is not there has no element: a tab past the
        count or with no room for one, the battery when none is shown, the volume when the sound server is not there, the
        microphone when nothing records, the dot when the board is clean, a cell of a tray of the person's (only the whole tray
        has a slot), the button when the band has none. And an element the band has not the room for whole has its name taken
        from it, so a band too short for a slot has none and nothing is drawn outside the band.
*/

// A thing of the band opened, for its caller to close: named for its slot, the gap before it along the edge, and length long.
static struct ui_node address_to taskbar_open(struct ui_tree address_to t, int which, int m, int gap, int length)
{
        struct ui_node address_to n = ui_tree_open(t);

        n->id = which + 1, n->dir = m, n->margin[2 * m] = gap;
        n->size[m] = (struct ui_size){UI_FIXED, length, 0, UI_TREE_MOST, 0};
        return n;
}

// A mark in the middle of an element: one more child of it, side by side.
static fn taskbar_mark(struct ui_tree address_to t, struct ui_node address_to n, int side)
{
        n->justify = n->align = UI_CENTER;
        n = ui_tree_open(t);
        n->size[0] = n->size[1] = (struct ui_size){UI_FIXED, side, 0, UI_TREE_MOST, 0};
        ui_tree_close(t);
}

static fn taskbar_lay(struct ui_tree address_to t, const struct taskbar_view *view)
{
        // The axis the band runs along (0 across its output, 1 down it) and the one across it, and whether the band is at the end of that one.
        int m = taskbar_vertical(view->position), c = !m, far = view->position == TASKBAR_BOTTOM || view->position == TASKBAR_RIGHT;
        int scale = taskbar_scaled(view->scale), length = m ? view->height : view->width, thick = taskbar_thick(view->scale);
        int gap = TASKBAR_GAP * scale / 100; // not a setting: the kit's gap between pieces that go together (TASKBAR_GAP), scaled like the rest
        int pad = taskbar.padding * scale / 100; // `taskbar padding`: the room round the text inside a tab
        int inset = 3 * scale / 100, high = thick - inset, count = (int)view->count, pitch = 0, dot = 0, button = 0, lead, items = 0, icons = 0, tray, tray_w, cell, text_w = 0, glyph, room, most;
        const struct ui_size gives = {UI_PERCENT, 100, 0, UI_TREE_MOST, 1};
        struct ui_node address_to n;

        // The numbers: the button, with what it and its gap take of the band's start; a cell of the tray, held to the most the tray may be; the mark of
        // the speaker and the microphone; and the longest a tab may be.
        if (taskbar.launcher && high > 0)
        {
                pitch = (high * 28 + 50) / 100, dot = (pitch * 65 + 50) / 100;
                dot = dot < 1 ? 1 : dot, pitch = pitch < dot + 1 ? dot + 1 : pitch;
                button = 2 * pitch + dot + 2 * pad < high ? high : 2 * pitch + dot + 2 * pad;
        }
        lead = button ? gap + button : 0;
#define TASKBAR_CELL(slot, shown, mark, paint) items += (shown) != 0;
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        icons = (taskbar.notices_item != 0) + (taskbar.mic_item != 0) + (taskbar.volume_item != 0);
        tray = length / 2 < length - lead - 2 * gap ? length / 2 : length - lead - 2 * gap; // the most the tray may be
        cell = taskbar_text(high, pad, 5);
        if (taskbar.tray_custom)
        {
                text_w = taskbar_text(high, pad, taskbar.tray_chars > 0 ? taskbar.tray_chars : 1);
                if (icons * cell + text_w > tray)
                {
                        int room_tray = tray > 0 ? tray : 0;

                        if (icons > 0 && icons * cell >= room_tray)
                                cell = text_w = room_tray / (icons + 1);
                        else
                                text_w = room_tray - icons * cell;
                }
                tray_w = icons * cell + text_w;
        }
        else
        {
                cell = cell * items > tray ? (tray > 0 ? tray : 0) / items : cell;
                tray_w = cell * items;
        }
        glyph = (cell < high ? cell : high) * 70 / 100;
        room = length - lead - tray_w - (count + 2) * gap;
        most = count && room > 0 ? room / count : 0;
        most = most > 8 * thick ? 8 * thick : most;

        // The output and the band.
        ui_tree_begin(t);
        n = ui_tree_open(t);
        n->dir = c, n->justify = far ? UI_END : UI_START, n->size[0].kind = n->size[1].kind = UI_GROW;
        n->x = view->x, n->y = view->y, *ui_tree_at(n, c) += taskbar_off(view, taskbar.shift);
        n = ui_tree_open(t);
        n->dir = m, n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0};
        n->pad[2 * m + 1] = gap, n->pad[2 * c + !far] = inset;

        // The lead.
        n = ui_tree_open(t);
        n->dir = m;
        if (!m)
                n->size[m] = gives, n->size[m].min = lead;
        if (button)
        {
                n = taskbar_open(t, TASKBAR_LAUNCHER, m, gap, button);
                n->justify = n->align = UI_CENTER;
                n = ui_tree_open(t);
                n->dir = 1, n->gap = pitch - dot;
                for (int row = 0; row < 3; row++)
                {
                        n = ui_tree_open(t);
                        n->gap = pitch - dot;
                        for (int column = 0; column < 3; column++)
                                taskbar_mark(t, n, dot);
                        ui_tree_close(t);
                }
                ui_tree_close(t);
                ui_tree_close(t);
        }
        ui_tree_close(t);

        // The tabs.
        n = ui_tree_open(t);
        n->dir = m;
        for (int at = 0; most && at < count; at++)
        {
                int wanted = view->lengths ? taskbar_text(high, pad, view->lengths[at]) : most;

                taskbar_open(t, at, m, gap, wanted < most ? wanted : most);
                ui_tree_close(t);
        }
        ui_tree_close(t);

        // The tail.
        n = ui_tree_open(t);
        n->dir = m, n->justify = UI_END, n->size[m] = gives, n->size[m].min = gap + tray_w;
        taskbar_open(t, TASKBAR_TRAY, m, gap, tray_w);
#define TASKBAR_CELL(slot, shown, mark, paint) if (shown) { n = taskbar_open(t, slot, m, 0, cell); if (mark) taskbar_mark(t, n, mark); ui_tree_close(t); }
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        if (taskbar.tray_custom)
        {
                n = taskbar_open(t, -9, m, 0, text_w);
                ui_tree_close(t);
        }
        ui_tree_close(t);
        ui_tree_close(t);

        // The rectangles, turned over for a language that reads from the right, and the names of those the band has not the room for taken back.
        ui_tree_close(t);
        ui_tree_close(t);
        ui_tree_run(t, view->width, view->height);
        if (!m && language_rtl())
                ui_tree_mirror(t);
        for (int at = 2; at < t->count; at++)
        {
                const struct ui_node address_to band = &t->node[1];

                n = &t->node[at];
                n->id = n->w > 0 && n->h > 0 && n->x >= band->x && n->y >= band->y && n->x + n->w <= band->x + band->w && n->y + n->h <= band->y + band->h ? n->id : 0;
        }
}

/*
        The tree of each band, and what it was made from: the view's output and tabs, the taskbar's settings and what its tray
        holds now, how far off its edge it is, and which way the language reads. A tree is made again only when that is not what
        the view and the taskbar say now, so the many questions of a frame (a slot, a point, a damage, a draw) lay the band out
        once between them. The band is the one the compositor is asking about (band_at); a view of another band in its place
        is only a tree made again.
*/
struct taskbar_key
{
        b64 shift;                      // how far off its edge the band is
        int width, height, scale, x, y; // the view's output, where it is in the space, and its scale
        int position, count, titled;    // the edge, the tabs, and whether the view says how long their titles are
        int own, padding, launcher;     // the taskbar's settings: its scale, the room round a tab's text, the launcher's button
        int tray, cells, rtl;           // a format of the person's (its characters; -1: the standard cells), the cells there now (a bit each), reading from the right
        unsigned char lengths[WINDOWS]; // each tab's title in characters
};

_Static_assert(sizeof(struct taskbar_key) == 8 + 14 * 4 + WINDOWS, "a key is compared as bytes, so it has no padding");

static struct taskbar_band
{
        struct taskbar_key key;
        struct ui_tree tree;
} taskbar_bands[WINDOWS_OUTPUTS];

static struct ui_tree address_to taskbar_tree(const struct taskbar_view *view)
{
        struct taskbar_band address_to band = &taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0];
        struct taskbar_key key = {taskbar.shift, view->width, view->height, view->scale, view->x, view->y, (int)view->position, (int)view->count,
                                  view->lengths != null, taskbar.scale, taskbar.padding, taskbar.launcher != 0,
                                  taskbar.tray_custom ? taskbar.tray_chars : -1, 0, language_rtl(), {0}};

#define TASKBAR_CELL(slot, shown, mark, paint) key.cells = key.cells << 1 | ((shown) != 0);
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        if (view->lengths)
                memory_copy(key.lengths, view->lengths, view->count < WINDOWS ? view->count : WINDOWS);
        if (!band->tree.count || memory_compare(&band->key, &key, sizeof(key)))
        {
                band->key = key;
                taskbar_lay(&band->tree, view);
        }
        return &band->tree;
}

// The element of a slot in the band's tree, or null when the band has no such slot now.
static struct ui_node address_to taskbar_node(const struct taskbar_view *view, int which)
{
        return ui_tree_find(taskbar_tree(view), which + 1);
}

static fn taskbar_rect(const struct ui_node address_to n, int *x, int *y, int *width, int *height)
{
        *x = n->x, *y = n->y, *width = n->w, *height = n->h;
}

// The band: where it is now, which is its place unless it is sliding off (taskbar_slide_now).
static void taskbar_band(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{
        taskbar_rect(&taskbar_tree(view)->node[1], x, y, width, height);
}

// The one rectangle the band covers while it moves from before to where it is now, so that its damage covers both places:
// where it is, and as much more as it has moved, on the side it came from.
static void taskbar_slide_rect(const struct taskbar_view *view, b64 before, int *x, int *y, int *width, int *height)
{
        int rect[4], c = !taskbar_vertical(view->position), came = taskbar_off(view, before) - taskbar_off(view, taskbar.shift);

        taskbar_band(view, &rect[0], &rect[1], &rect[2], &rect[3]);
        rect[c] += came < 0 ? came : 0, rect[2 + c] += came < 0 ? -came : came;
        *x = rect[0], *y = rect[1], *width = rect[2], *height = rect[3];
}

// A slot's rectangle on the output; 0 for a slot the band has not got now.
static int taskbar_slot(const struct taskbar_view *view, int which, int *x, int *y, int *width, int *height)
{
        const struct ui_node address_to n = taskbar_node(view, which);

        if (n)
                taskbar_rect(n, x, y, width, height);
        return n != null;
}

/*
        What is at a point of the band: a tab's number, a cell's slot, TASKBAR_TRAY for the tray (its clock and its battery are
        the tray to a press), or TASKBAR_HIT_NONE. With reach, a finger's: each slot reaches that much further across the band,
        which is toward the middle of its screen (the other way is off it), and half the gap further along the band either side,
        so a tab a finger lands on the edge of is still the tab, and nothing is drawn any larger (ui_tree_hit).
*/
static int taskbar_hit_reach(const struct taskbar_view *view, int px, int py, int reach)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int grown[2], at, which;

        grown[t->node[1].dir] = reach ? taskbar_scaled(view->scale) * TASKBAR_GAP / 200 : 0; // half the gap between the tabs, at the taskbar's scale
        grown[!t->node[1].dir] = reach;
        at = ui_tree_hit(t, px, py, grown);
        if (at < 0)
                return TASKBAR_HIT_NONE;
        which = t->node[at].id - 1;
        return which == -4 || which == -3 || which == -9 ? TASKBAR_TRAY : which;
}

/*
        A slot's stripe of the band: the slot's length along the edge and the band's whole thickness across it, on the output. It is
        what a change under the band reaches and what the glass is made over, and the rectangle a popup from the slot opens beside
        (the launcher's button, the volume, the microphone and the notices cells): a bottom band's popup ends where this starts, a
        top band's starts where it ends, and a side band's is beside it, its top at this top. Answers 0 when the band has no such
        slot now.
*/
static int taskbar_anchor(const struct taskbar_view *view, int which, int *x, int *y, int *width, int *height)
{
        const struct ui_node address_to n = taskbar_node(view, which), address_to band = &taskbar_tree(view)->node[1];

        if (!n)
                return 0;
        taskbar_rect(n, x, y, width, height);
        if (band->dir)
                *x = band->x, *width = band->w;
        else
                *y = band->y, *height = band->h;
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
        unsigned long redo = 0;

        for (int which = TASKBAR_LAUNCHER; which < (int)view->count; which++)
        {
                int reach[4];

                if (which == -4 || which == -3 || !taskbar_anchor(view, which, &reach[0], &reach[1], &reach[2], &reach[3]))
                        continue;
                reach[2] += reach[0] + COMPOSITOR_GLASS_REACH, reach[3] += reach[1] + COMPOSITOR_GLASS_REACH;
                reach[0] -= COMPOSITOR_GLASS_REACH, reach[1] -= COMPOSITOR_GLASS_REACH;
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

// The ink a tab's text (or its mark) has on its glass: the one that stands out from it, quieter the less the tab is lit.
static unsigned int taskbar_ink_on(unsigned int ground, unsigned int mark)
{
        unsigned int ink = window_contrast(ground);

        return mark & COMPOSITOR_TAB_LIT ? ink : mark & COMPOSITOR_TAB_AWAY ? window_mix(ground, ink, 110) : window_mix(ground, ink, 170);
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
        One line of text in an element of the band: upright on a bottom or top band, and on a side one turned as taskbar_turn
        says: laid out as a bottom band lays a slot's text, along the slot's
        length and in the middle of its thickness, and turned onto it. What is
        longer than the slot is cut to fit it with dots after it (the kit's: ui_cell_line, ui_cell_turned).
*/
static void taskbar_say(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, const char *text,
                        unsigned int ink, unsigned int ground)
{
        const struct ui_node address_to band = &t->node[1];

        if (band->dir)
                ui_cell_turned(taskbar_cell(view, band->w), n->x - band->x, n->y - band->y, n->w, n->h, text, taskbar_turn(view->position), ink, ground);
        else
                ui_cell_line(taskbar_cell(view, band->w), n->x - band->x, n->y - band->y, n->w, n->h, text, ink, ground);
}

// The kit's drawing state over the band's own pixels, recording into the frame's scene when there is one: what the marks that are
// drawn with the kit's painters stand on. Answers the mark in the middle of the element n, and where it is in those pixels.
static const struct ui_node address_to taskbar_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t,
                                                  const struct ui_node address_to n, int *x, int *y)
{
        const struct ui_node address_to band = &t->node[1], address_to mark = &t->node[n->first];

        memory_zero(ui, sizeof(*ui));
        ui->pixels = view->pixels, ui->pitch = (unsigned long)band->w, ui->width = band->w, ui->height = band->h, ui->top = 0, ui->bottom = band->h;
        ui->scale = taskbar_scaled(view->scale), ui->scene = view->scene;
        *x = mark->x - band->x, *y = mark->y - band->y;
        return mark;
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
        The launcher tab's mark: the apps mark, a grid of three by three round dots in the middle of the tab, each where the band's
        tree has it (taskbar_lay): whole device pixels across and apart at every scale, each the kit's
        fill with a corner of half its width (as the notices dot is), so there is no icon file and no font in it. It is recorded into the
        frame's scene when there is one, as the dot and the speaker are.
*/
static void taskbar_launcher_glyph(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, unsigned int ink)
{
        struct ui ui;
        int x, y;
        const struct ui_node address_to grid = taskbar_ui(&ui, view, t, n, &x, &y);

        for (int row = grid->first; row >= 0; row = t->node[row].next)
                for (int at = t->node[row].first; at >= 0; at = t->node[at].next)
                {
                        const struct ui_node address_to dot = &t->node[at];
                        int at_x = x + dot->x - grid->x, at_y = y + dot->y - grid->y;

                        ui_kit_fill(&ui, &(struct ui_box){at_x * 256, at_y * 256, (at_x + dot->w) * 256, (at_y + dot->w) * 256, dot->w * 128, dot->w * 128},
                                    ink | 0xff000000u, null);
                }
}

/*
        The volume cell: the speaker (ui_speaker, in the band's own pixels) with no waves, one or two by the volume, or crossed out
        when it is muted; for a moment after a key or the wheel changed the volume, the number instead, as the battery's is.
*/
static void taskbar_volume_cell(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, unsigned int ink,
                                unsigned int ground)
{
        struct ui ui;
        int x, y, waves = taskbar.volume_muted || !taskbar.volume_percent ? 0 : taskbar.volume_percent < 50 ? 1 : 2;
        const struct ui_node address_to mark = taskbar_ui(&ui, view, t, n, &x, &y);

        if (taskbar.volume_flash)
        {
                char text[8] = "--";

                if (!taskbar.volume_muted)
                        taskbar_battery_text(text, 0, taskbar.volume_percent);
                taskbar_say(view, t, n, text, ink, ground);
                return;
        }
        ui_speaker(&ui, x, y, mark->w, waves, taskbar.volume_muted, ink | 0xff000000u);
}

/*
        The microphone cell: the microphone (ui_microphone, in the band's own pixels) in the error colour, so that a program recording is
        never silent in the tray, and struck when the microphone is muted.
*/
static void taskbar_mic_cell(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n)
{
        struct ui ui;
        int x, y;
        const struct ui_node address_to mark = taskbar_ui(&ui, view, t, n, &x, &y);

        ui_microphone(&ui, x, y, mark->w, taskbar.mic_muted, compositor_ink_role(UI_ERROR) | 0xff000000u);
}

/*
        The notices dot: the kit's status dot (ui_kit_dot, the size a badge's dot is) in the badge colour of the worst notice, the error's
        red or the warning's amber, in the middle of its cell. It is drawn only while the board holds one (taskbar_notices_set).
*/
static void taskbar_notices_cell(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n)
{
        struct ui ui;
        unsigned int colour = compositor_ink_role(taskbar.notices_severity == WINDOWS_ISSUE_ERROR ? UI_ERROR : UI_WARNING);
        int x, y;

        taskbar_ui(&ui, view, t, n, &x, &y);
        ui_kit_dot(&ui, x, y, colour | 0xff000000u);
}

/*
        The band drawn over what is under it: each tab redone made glass with
        its title on it, and the tray made glass with the clock and the
        battery. Only the slots in redo are made again, and only their span
        is damaged: their stripes of the band, from the first to the last of them along the edge. Answers 1 with rect set to the damage, or 0 for none. The
        glass is made by glass over the span: it answers the row stride, or 0. The elements are drawn in the order the band's tree has
        them, each by its name: a slot with a glass of its own (the button, a tab, the tray) makes it and writes on it, and a cell of the
        tray, which comes after the tray, is drawn on the tray's.
*/
static int taskbar_draw(const struct taskbar_view *view, unsigned long redo, taskbar_glass_fn glass, void *context,
                        int *rect)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        const struct ui_node address_to band = &t->node[1];
        int span[4] = {0, 0, 0, 0}, stripe[4], gw, time_ok;
        timespec now = {0, 0};
        time_t seconds;
        tm broken;
        unsigned int ground = 0, ink = 0;
        char clock[6] = {0}, text[8];

        taskbar.stale = 0;
        if (!view->pixels || (unsigned long)band->w * (unsigned long)band->h * 4 > view->band_bytes)
                return 0;
        for (int which = TASKBAR_LAUNCHER; which < (int)view->count; which++)
                if (which != -4 && which != -3 && (redo & taskbar_bit(which)) && taskbar_anchor(view, which, &stripe[0], &stripe[1], &stripe[2], &stripe[3]))
                {
                        stripe[2] += stripe[0], stripe[3] += stripe[1];
                        compositor_union(span, stripe);
                }
        if (span[2] <= span[0])
                return 0;
        rect[0] = span[0], rect[1] = span[1], rect[2] = span[2] - span[0], rect[3] = span[3] - span[1];
        gw = glass(context, rect[0], rect[1], rect[2], rect[3]);
        if (!gw)
                return 1;

        taskbar.minute = taskbar_minute_now();
        clock_gettime(CLOCK_REALTIME, address_of now);
        seconds = now.tv_sec;
        time_ok = localtime_r(&seconds, &broken) != 0;
        if (time_ok)
                clock[0] = (char)('0' + broken.tm_hour / 10), clock[1] = (char)('0' + broken.tm_hour % 10), clock[2] = ':',
                clock[3] = (char)('0' + broken.tm_min / 10), clock[4] = (char)('0' + broken.tm_min % 10);
        taskbar_battery_text(text, taskbar.battery_charging, taskbar.battery_percent);
        for (int at = 2; at < t->count; at++)
        {
                const struct ui_node address_to n = &t->node[at];
                int which = n->id - 1;

                // The tray, and so its cells, is drawn only with the time to say.
                if (!n->id || !(redo & taskbar_bit(which)) || (which < 0 && which != TASKBAR_LAUNCHER && !time_ok))
                        continue;
                if (which >= 0 || which == TASKBAR_LAUNCHER || which == TASKBAR_TRAY)
                {
                        // A tab is lit while its window has the keys, and the launcher's while the launcher is up; the tray never is.
                        unsigned int mark = which >= 0 ? view->marks[which] : which == TASKBAR_LAUNCHER && view->launcher_lit ? COMPOSITOR_TAB_LIT : 0;

                        ground = taskbar_glass_tab(view, rect[0], rect[1], gw, band->x, band->y, band->w, n->x, n->y, n->w, n->h, which, mark);
                        ink = which == TASKBAR_TRAY ? window_contrast(ground) : taskbar_ink_on(ground, mark);
                }
                switch (which)
                {
                case TASKBAR_LAUNCHER: // its mark in place of a title
                        taskbar_launcher_glyph(view, t, n, ink);
                        break;
                case TASKBAR_TRAY:
                        break;
                case -9: // a format of the person's is drawn in its text cell after the tray icons
                        if (taskbar.tray_custom)
                                taskbar_say(view, t, n, taskbar.tray_text, ink, ground);
                        break;
#define TASKBAR_CELL(slot, shown, mark, paint) case slot: paint; break;
                TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
                default: // a window's tab: a band on a side writes its titles turned, a bottom or top one upright
                        taskbar_say(view, t, n, view->titles[which], ink, ground);
                }
        }
        return 1;
}

/*
        The tray's clock has changed minute, or the language its words are taken in: the
        band is drawn whole. Answers 1 when either has changed, and then the battery
        and the tray's text are read again.
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

// `taskbar launcher`: on puts the launcher's tab at the band's leading end, and the tabs are laid out in what it leaves.
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

// `taskbar padding`: the room round the text inside each tab, either side of it, in logical pixels at the taskbar's scale (0 to
// TASKBAR_PADDING_MOST; anything else is the standard). The gap between the tabs and at the band's ends is fixed (TASKBAR_GAP).
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
