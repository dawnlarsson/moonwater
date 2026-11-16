/*
        The taskbar's layout, hit test, damage and painting (the taskbar's part of ui/ui.c says what
        it is and what the compositor gives it).

        The band is a strip along an edge of the output that draws nothing of its own: in its middle float
        three plates of glass, each a pill with every corner round, the band's inset clear of its edges and
        twice the kit's gap apart, and on each the kit's buttons, drawn on glass (ui_glass_button):

            start  |  the windows' tabs  |  the tray

        The launcher's button comes first (with `taskbar launcher on`; off, which is the default, the band
        has none), as long as it is high, the kit's apps mark in its middle (UI_ICON_APPS). Then the tabs,
        in the order the windows showed, each its window's icon and then its title, as long as the two need
        at a width a character has (a tab is no longer than its icon and eight times the band's thickness,
        and a title with no room in it is cut with dots); tabs that would not all fit share what there is,
        and a share too short for a title is the icon alone. Then the tray: the notices dot, the
        microphone and the volume when they are there, the battery when the settings name one, and the
        clock. The three are one group in the middle of the band, so it grows from the middle either way as
        windows open and never runs over the button or the tray. A side band is the same group down its
        middle, and writes its titles and its tray turned a quarter, the tops of the letters toward the
        middle of its output or toward the edge it is on, as `taskbar text` says.

        The tab whose window has the keys is the lit button, the kit's chosen face on glass (ui_kit_lit),
        and when the keys go to another window the face moves to its tab as the kit's tabs move their pip
        (ui_kit_number_to on --ease); the launcher's button is lit while the launcher is up. A press is
        taken where the button is drawn along the band, and across the whole of the band's thickness, so
        the screen's last row of pixels is still the button above it.

        None of that is worked out here: the band is declared as a tree of the kit's layout (taskbar_lay, below), and where a
        slot is, what a point is over, what a change under the band reaches and where each thing is painted are read from the
        rectangles the layout gives the tree.

        Every rectangle here is on the output and in device pixels. The painting
        works in the band's own coordinates (the pixels start at its top left) and
        the glass in the rectangle the compositor blurred for the span.

        Its own look, kept: the compositor paints it into its scene (glass, no kit window and no kit frame clock, so no
        ripple; the highlight's motion is on the kit's clock, which the compositor's loop reads), in the kit theme's tints
        (compositor_tab_target), with a finger's reach (taskbar_hit_reach).
*/

/*
        The moon moves two ways, each with the kit's motion and none with reduced motion (taskbar.glides). It turns, once in
        `taskbar moon spin` seconds (TASKBAR_MOON_SPIN), in TASKBAR_MOON_STEPS steps a second on the real clock, and stops
        TASKBAR_MOON_IDLE after the last thing a hand did; each step redraws the moon's square alone. And it grows about its middle by
        TASKBAR_MOON_GROW percent while the pointer is over it or the launcher is up, over TASKBAR_MOON_GROW_MS on --ease, out into the
        band's gap round it: the tasks do not move, and what a press finds is the disc at rest.
*/
#define TASKBAR_MOON_SPIN 60
#define TASKBAR_MOON_STEPS 5
#define TASKBAR_MOON_IDLE (120ull * 1000000000ull)
#define TASKBAR_MOON_GROW 12
#define TASKBAR_MOON_GROW_MS 180

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
        int glides;                             // the compositor's motion is on (compositor_glides, given to taskbar_slide_now): the slide and the highlight move
        int moon_zone, tasks_zone;              // where the moon's pill is along the band and where the tasks' are: TASKBAR_ZONE_START, _CENTRE or _END
        int tasks_right;                        // the newest task is at the tasks' left end (0, the order the windows showed) or at their right (1)
        unsigned int tray_before;               // a bit for each tray item (taskbar_item_of): it is in the pill before the tasks, not the one after
        unsigned char order[3];                 // the pills' order where they share a place: TASKBAR_PILL_MOON, _TASKS and _TRAY
        int editing;                            // the band is being arranged: each item ringed, a drop aimed at a place, and the Done button
        int clock_tray;                         // the clock is a cell of the tray, not on the moon (`taskbar clock tray`)
        int clock_tab;                          // the clock is a pill of its own after the tray's (`taskbar clock tab`)
        int clock_none;                         // no clock anywhere, the moon alone (`taskbar clock none`)
        char clock_format[SETTING_TRAY_MOST + 1]; // what the clock outside the moon says: the time in the hours the clock reads, and the date
        char clock_text[SETTING_TRAY_MOST * 4];  // what it says this minute
        int clock_chars;                        // characters in clock_text
        int clock_12h;                          // the clock reads twelve hours (`time format 12h`): its hours so, and AM and PM left to the menu
        int clock_bulge;                        // how far the clock's digits swell onto the sphere, percent (ui_moon_label's bulge)
        int moon_ink;                           // `taskbar moon look ink`: the moon drawn in ink (ui_moon_paper), not as it is (realistic, the standard)
        int clock_read;                         // how the clock's digits stood out the last time they were drawn, hundredths (4.5 to 1 is 450)
        long long moon_at;                      // the time the moon is drawn at, seconds (a check's or a picture's); 0: now
        int spin;                               // `taskbar moon spin`: seconds the moon takes to turn once, 0 not at all
        int separate;                           // `taskbar tasks separate`: each task a pill of its own, the kit's gap twice between them
        int edge_in;                            // `taskbar edge`: logical pixels the pills float in from the screen's edge (less than 0: past it)
        int moon_hover;                         // the band whose moon the pointer is over, plus one; 0 none
        positive moon_seen;                     // when a hand last did anything (the monotonic clock): the moon stops turning a while after
        long long moon_step;                    // the step of the turn the moon is drawn at (TASKBAR_MOON_STEPS a second on the real clock)
        b64 moon_turn;                          // how far round it is then, Q30 of a turn
} taskbar = {1, 0, 0, TASKBAR_BOTTOM, TASKBAR_BOTTOM, 0, {0}, 0, 0, 0, .scale = TASKBAR_SCALE_STANDARD, .padding = TASKBAR_PADDING_STANDARD, .glides = 1,
   .spin = TASKBAR_MOON_SPIN, .moon_zone = TASKBAR_ZONE_CENTRE, .tasks_zone = TASKBAR_ZONE_SPLIT, .tray_before = 1u << 1 | 1u << 2, .clock_bulge = 60, .order = {TASKBAR_PILL_MOON, TASKBAR_PILL_TASKS, TASKBAR_PILL_TRAY}};

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
/*
        `taskbar edge` in device pixels at the band's scale: in from the screen's edge for more than 0, out past it for less. Out, it is held
        to TASKBAR_EDGE_KEEP of the pills' thickness on the screen, so the clock and every control can still be read and pressed.
*/
static int taskbar_inset(int scale);

#define TASKBAR_EDGE_KEEP 60 // percent of a pill's thickness that stays on the screen at the most negative edge

static int taskbar_edge_px(const struct taskbar_view *view)
{
        int scale = taskbar_scaled(view->scale), thick = taskbar_thick(view->scale), inset = taskbar_inset(scale), high = thick - 2 * inset;
        int edge = taskbar.edge_in * scale / 100, least = -(high * (100 - TASKBAR_EDGE_KEEP) / 100 + inset);

        return edge < least ? least : edge;
}

static int taskbar_off(const struct taskbar_view *view, b64 shift)
{
        int thick = taskbar_thick(view->scale), in = taskbar_edge_px(view);
        int off = (int)(((long long)(thick + in) * (long long)shift) / (long long)UI_ONE) - in;

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
        taskbar.glides = animate != 0;
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
        A window's tab: its icon and then its title. The icon is TASKBAR_ICON logical pixels to a side at the band's scale, or as much
        as a tab's height leaves with two pixels above and below; the title is what a run of its characters takes (taskbar_text's
        width of a character); between the two is TASKBAR_ICON_GAP, and the padding (`taskbar padding`) is before the icon and
        after the title. A window with no title is its icon and the padding. Never shorter than the tab is high. taskbar_lay holds
        a tab to what the band has for each and to its icon, the gap and eight times the band's thickness, the room a title had
        before there was an icon, and a title that does not fit what is left is cut with dots where it is drawn (taskbar_say).
*/
#define TASKBAR_ICON 16
#define TASKBAR_ICON_GAP UI_GAP2 // the kit's gap between an icon and its words

static int taskbar_icon_side(int high, int scale)
{
        int side = TASKBAR_ICON * scale / 100, room = high - 4 * scale / 100;

        side = side > room ? room : side;
        return side < 1 ? 1 : side > UI_ICON_MOST ? UI_ICON_MOST : side;
}

static int taskbar_tab_length(int high, int pad, int scale, int characters)
{
        int text = characters * high * 308 / 1000;
        int wanted = 2 * pad + taskbar_icon_side(high, scale) + (characters > 0 ? TASKBAR_ICON_GAP * scale / 100 + text : 0);

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
        CELL(-3, taskbar.clock_tray && !taskbar.clock_tab && !taskbar.tray_custom, 0, taskbar_say(view, t, n, taskbar_clock_said(clock), ink, ground))

/*
        The band as the kit's layout has it (ui_tree, ui/ui.c). The root is the band's output, where it is in the desktop's
        space (and moved off its edge with the band while the band slides), and the band is its one child, element 1: at the
        start or the end of the output across the edge, as thick as its scale makes it and as long as the output. In it, in its
        middle along the edge and across it, float up to three plates, each a pill of glass the band's inset in from its edges
        (the compositor draws each as a frame of its own, every corner round: compositor_panel_tabs), with the kit's gap twice
        over between one and the next, and on each its buttons side by side:

            the launcher    the launcher's button, as long as it is high (a round plate), its mark in its middle (UI_ICON_APPS)
            the tabs        TASKBAR_TABS, a tab for each window: the padding, its icon, a gap, its title in what is left
            the tray        its cells, the speaker, the microphone and the dot each a square with its mark in its middle, the
                            battery and the clock each the width of five characters; or the one text of a format of the person's

        The three are one group in the middle of the band, so it grows from the middle either way as windows open. What the band
        has for the tabs is what the button, the tray, the gaps between them and the kit's gap at either end leave: tabs that need
        more are each an equal share of it (never longer than its icon, the gap after it and eight times the band's thickness),
        and a share under what an icon and seven characters take is the icon alone, in the middle of a tab no longer than it and
        its padding. So the tabs never run over the button or the tray. The tray is held to half the band and to what the button
        leaves of it: a format of the person's is one text, cut to that, and the standard tray's cells are each narrower when
        they would not all fit. Reading from the right, a bottom or top band is the same band turned over its length
        (ui_tree_mirror). A side band is the same group down its middle.

        An element is named for its slot, the slot plus one, so that 0, the name of an element given none, is no slot. A slot is
        a tab's number (0 up), TASKBAR_TRAY (-2) for the tray as a whole, -3 the clock, -4 the battery, TASKBAR_LAUNCHER (-5),
        TASKBAR_VOLUME (-6), TASKBAR_MIC (-7), TASKBAR_NOTICES (-8), -9 a format's text and TASKBAR_TABS (-10) the tabs' plate. A
        slot that is not there has no element: a tab past the count or with no room for one, the plate with no tab, the battery
        when none is shown, the volume when the sound server is not there, the microphone when nothing records, the dot when the
        board is clean, a cell of a tray of the person's (only the whole tray has a slot), the button when the band has none. And
        an element the band has not the room for whole has its name taken from it, so a band too short for a slot has none and
        nothing is drawn outside the band.
*/

// A thing of the band opened, for its caller to close: named for its slot, the gap before it along the edge, length long and high across.
static struct ui_node address_to taskbar_open(struct ui_tree address_to t, int which, int m, int gap, int length, int high)
{
        struct ui_node address_to n = ui_tree_open(t);

        n->id = which + 1, n->dir = m, n->margin[2 * m] = gap;
        n->size[m] = (struct ui_size){UI_FIXED, length, 0, UI_TREE_MOST, 0};
        n->size[!m] = (struct ui_size){UI_FIXED, high, 0, UI_TREE_MOST, 0};
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

// The inset of the plates from the band's edges across it, at the band's scale: two logical pixels, one device pixel at least.
static int taskbar_inset(int scale)
{
        int inset = 2 * scale / 100;

        return inset < 1 ? 1 : inset;
}

// The tray item a slot is, its bit in taskbar.tray_before: the dot, the microphone, the speaker, the battery, a format's text; -1 for none.
static int taskbar_item_of(int slot)
{
        return slot == TASKBAR_NOTICES ? 0 : slot == TASKBAR_MIC ? 1 : slot == TASKBAR_VOLUME ? 2 : slot == -4 ? 3 : slot == -9 ? 4 : slot == -3 ? 5 : -1;
}

// Whether a tray item is in the pill before the tasks (else the one after them).
static int taskbar_before(int slot)
{
        int item = taskbar_item_of(slot);

        return item >= 0 && (taskbar.tray_before >> item & 1);
}

/*
        A tray pill: the items that are on its side of the tasks (before, the pill before them, TASKBAR_TRAY_BEFORE; else the pill
        after, TASKBAR_TRAY), each the kit's padding either side of what it shows: the dot, the microphone and the speaker a mark,
        the battery and a format of the person's a text. Its length, or with t its tree's elements too, margin the room before it.
*/
static int taskbar_clocked; // the clock's cell, as long as its text (taskbar_lay sets it before it lays the tray)
static int taskbar_apart;   // the kit's gap twice, between one pill and the next (taskbar_lay sets it before it lays the tasks)

static int taskbar_tray_lay(struct ui_tree address_to t, int before, int m, int margin, int scale, int glyph, int square, int cell, int text_w, int high)
{
        int clocked = taskbar_clocked;

        struct ui_node address_to n = null;
        int length = 0;

#define TASKBAR_CELL(slot, shown, mark, paint) if ((shown) && taskbar_before(slot) == before) length += (mark) ? square : slot == -3 ? clocked : cell;
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        if (taskbar.tray_custom && taskbar_before(-9) == before)
                length += text_w;
        if (!t || !length)
                return length;
        taskbar_open(t, before ? TASKBAR_TRAY_BEFORE : TASKBAR_TRAY, m, margin, length, high);
#define TASKBAR_CELL(slot, shown, mark, paint)                                                                                     \
        if ((shown) && taskbar_before(slot) == before)                                                                              \
        {                                                                                                                           \
                n = taskbar_open(t, slot, m, 0, (mark) ? square : slot == -3 ? clocked : cell, high);                               \
                if (mark)                                                                                                           \
                        taskbar_mark(t, n, mark);                                                                                   \
                ui_tree_close(t);                                                                                                   \
        }
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        if (taskbar.tray_custom && taskbar_before(-9) == before)
        {
                n = taskbar_open(t, -9, m, 0, text_w, high);
                ui_tree_close(t);
        }
        ui_tree_close(t);
        (void)n;
        return length;
}

// The clock's own pill (`taskbar clock tab`): its text, the padding either side, a plate of glass after the tray's.
static fn taskbar_clock_lay(struct ui_tree address_to t, int m, int margin, int length, int high)
{
        taskbar_open(t, -3, m, margin, length, high);
        ui_tree_close(t);
}

// A pill of tasks, named slot (TASKBAR_TABS, or TASKBAR_TABS_LEFT for the tasks on the moon's left when they are split round it): the tasks
// listed in order, each the padding, its icon in its middle across the band, the gap, its title in what is left and the padding, or its icon
// alone in its middle.
static fn taskbar_tasks_lay(struct ui_tree address_to t, int slot, const int *list, int listed, const int *widths, const int *titled, int m, int margin,
                            int pad, int side, int gap, int high)
{
        int length = 0, apart = taskbar_apart;

        for (int k = 0; k < listed; k++)
                length += widths[list[k]];
        if (!length)
                return;
        // Grouped, one pill of them all; separate, each task a pill of its own in the zone, the kit's gap twice between them.
        if (!taskbar.separate)
                taskbar_open(t, slot, m, margin, length, high);
        for (int k = 0; k < listed; k++)
        {
                int at = list[k];
                struct ui_node address_to n = taskbar_open(t, at, m, taskbar.separate ? (k ? apart : margin) : 0, widths[at], high), address_to part;

                n->align = UI_CENTER;
                if (titled[at])
                        n->pad[2 * m] = pad;
                else
                        n->justify = UI_CENTER;
                part = ui_tree_open(t);
                part->size[0] = part->size[1] = (struct ui_size){UI_FIXED, side, 0, UI_TREE_MOST, 0};
                ui_tree_close(t);
                if (titled[at])
                {
                        part = ui_tree_open(t);
                        part->margin[2 * m] = gap, part->margin[2 * m + 1] = pad;
                        part->size[m] = (struct ui_size){UI_GROW, 1, 0, UI_TREE_MOST, 0};
                        part->size[!m] = (struct ui_size){UI_FIXED, high, 0, UI_TREE_MOST, 0};
                        ui_tree_close(t);
                }
                ui_tree_close(t);
        }
        if (!taskbar.separate)
                ui_tree_close(t);
}

// The tasks' widths for room: each what its icon and title ask, or an equal share when they ask for more, and under a share with the room
// for seven characters of a title, every one its icon alone (so a short title is not kept where a long one beside it is not). Answers
// their length.
static int taskbar_tasks_fit(const struct taskbar_view *view, const int *list, int listed, int room, int high, int pad, int scale, int longest,
                             int *widths, int *titled)
{
        int sum = 0, share, tabs = 0, side = taskbar_icon_side(high, scale), alone = taskbar_tab_length(high, pad, scale, 7);
        int gaps = taskbar.separate && listed > 1 ? (listed - 1) * taskbar_apart : 0;

        // Separate, the gaps between the pills are the tasks' too: the room less them, and the length with them.
        room -= gaps, tabs = gaps;

        for (int k = 0; k < listed; k++)
        {
                int at = list[k];

                widths[at] = view->lengths ? taskbar_tab_length(high, pad, scale, view->lengths[at]) : longest;
                widths[at] = widths[at] > longest ? longest : widths[at];
                sum += widths[at];
        }
        share = listed && room > 0 ? room / listed : 0;
        for (int k = 0; k < listed; k++)
        {
                int at = list[k], bare = side + 2 * pad;

                widths[at] = sum > room && widths[at] > share ? share : widths[at];
                titled[at] = (!view->lengths || view->lengths[at] > 0) && (sum <= room || share >= alone);
                if (!titled[at])
                        widths[at] = widths[at] < bare ? widths[at] : bare;
                tabs += widths[at];
        }
        return tabs;
}

static fn taskbar_lay(struct ui_tree address_to t, const struct taskbar_view *view)
{
        // The axis the band runs along (0 across its output, 1 down it) and the one across it, and whether the band is at the end of that one.
        int m = taskbar_vertical(view->position), c = !m, far = view->position == TASKBAR_BOTTOM || view->position == TASKBAR_RIGHT;
        int scale = taskbar_scaled(view->scale), length = m ? view->height : view->width, thick = taskbar_thick(view->scale);
        int gap = TASKBAR_GAP * scale / 100; // not a setting: the kit's gap between pieces that go together (TASKBAR_GAP), scaled like the rest
        int apart = 2 * gap;                 // and between one pill and the next, twice it
        int pad = taskbar.padding * scale / 100; // the kit's padding in a glass button (UI_GLASS_PAD, `taskbar padding`), either side of what it shows
        int inset = taskbar_inset(scale), high = thick - 2 * inset, count = (int)view->count, moon, icons = 0, done = 0, split = taskbar.tasks_zone == TASKBAR_ZONE_SPLIT;
        int square, cell, clocked, tab, text_w = 0, glyph, longest, side, trays[2], most, inner = length - 2 * gap, icon_gap = TASKBAR_ICON_GAP * scale / 100;
        int widths[TASKBAR_TAB_ROOM], titled[TASKBAR_TAB_ROOM], zones[3], list[2][TASKBAR_TAB_ROOM], listed[2] = {0, 0}, tabs[2] = {0, 0}, moon_zone = split ? TASKBAR_ZONE_CENTRE : taskbar.moon_zone;
        struct ui_node address_to n;

        high = high < 1 ? 1 : high, count = count < TASKBAR_TAB_ROOM ? count : TASKBAR_TAB_ROOM, side = taskbar_icon_side(high, scale);
        // The numbers: the moon, bare on the band and as thick as it; the tray's items, each its padding either side of its mark or its text,
        // held to half the band; the mark of the speaker and the microphone; and each tab, no longer than its icon and eight thicknesses.
        moon = thick <= length - 2 * gap ? thick : 0; // and none on a band too short for it
        icons = (taskbar.notices_item != 0) + (taskbar.mic_item != 0) + (taskbar.volume_item != 0);
        glyph = high * 64 / 100;
        square = glyph + 2 * pad, cell = taskbar_text(high, pad, 4);
        clocked = taskbar_text(high, pad, taskbar.clock_chars > 0 ? taskbar.clock_chars : 5);
        tab = taskbar.clock_tab ? clocked : 0;
        taskbar_clocked = clocked, taskbar_apart = apart;
        if (taskbar.tray_custom)
                text_w = taskbar_text(high, pad, taskbar.tray_chars > 0 ? taskbar.tray_chars : 1);
        most = length / 2 - moon - apart;
        trays[0] = taskbar_tray_lay(null, 0, m, 0, scale, glyph, square, cell, text_w, high);
        trays[1] = taskbar_tray_lay(null, 1, m, 0, scale, glyph, square, cell, text_w, high);
        if (trays[0] + trays[1] > most && most > 0)
        {
                // Too long for half the band: a format's text is cut first, then every item is narrower alike.
                int over = trays[0] + trays[1] - most;

                if (taskbar.tray_custom && text_w - over >= high)
                        text_w -= over;
                else
                {
                        int items = icons + (taskbar.battery_item && !taskbar.tray_custom) + (taskbar.tray_custom != 0) + (taskbar.clock_tray != 0);

                        square = cell = text_w = items ? most / items : 0;
                        taskbar_clocked = cell;
                }
                trays[0] = taskbar_tray_lay(null, 0, m, 0, scale, glyph, square, cell, text_w, high);
                trays[1] = taskbar_tray_lay(null, 1, m, 0, scale, glyph, square, cell, text_w, high);
        }
        // A band too short for its tray has none, and one too short for the Done button besides the moon has none.
        if (most <= 0 || trays[0] + trays[1] > most)
                trays[0] = trays[1] = 0;
        if (taskbar.editing)
                done = taskbar_text(high, pad, 4);
        if (done && done + apart + moon > inner / 2)
                done = 0;
        longest = 8 * thick + side + icon_gap;

        // The places: each as long as what it holds; the middle in the band's middle, and the start and the end each half of what it
        // leaves, but that one that holds more than its half has what it holds and the other the rest. Split round the moon, the moon is the
        // middle and the tasks share the start and the end alike, a task to the moon's right and the next to its left, each further out
        // than the last, and the tray's pills outermost: the items before the tasks at the start, those after them at the end.
        if (split)
        {
                int half = (inner - moon) / 2, rooms[2];

                for (int each = 0; each < count; each++)
                {
                        int right = (taskbar.tasks_right ? each + 1 : each) % 2 == 0;

                        list[right][listed[right]++] = each;
                }
                // The left side listed from its outer end in: reversed, its nearest the moon last.
                for (int k = 0; k < listed[0] / 2; k++)
                {
                        int keep = list[0][k];

                        list[0][k] = list[0][listed[0] - 1 - k], list[0][listed[0] - 1 - k] = keep;
                }
                // A side too short for its tray pill has none.
                if (half - apart - trays[1] - apart < 0)
                        trays[1] = 0;
                if (half - apart - trays[0] - apart - (done ? done + apart : 0) < 0)
                        trays[0] = 0;
                rooms[0] = half - apart - (trays[1] ? trays[1] + apart : 0);
                rooms[1] = half - apart - (trays[0] ? trays[0] + apart : 0) - (done ? done + apart : 0) - (tab ? tab + apart : 0);
                // Both sides at the share of the side with less room for each of its tasks, so the moon's two sides look alike.
                {
                        int per = 1 << 30;

                        for (int s = 0; s < 2; s++)
                                if (listed[s] && rooms[s] / listed[s] < per)
                                        per = rooms[s] / listed[s];
                        per = per > 0 ? per : 0;
                        for (int s = 0; s < 2; s++)
                                tabs[s] = taskbar_tasks_fit(view, list[s], listed[s], per * listed[s], high, pad, scale, longest, widths, titled);
                }
                zones[TASKBAR_ZONE_CENTRE] = moon, zones[TASKBAR_ZONE_START] = half, zones[TASKBAR_ZONE_END] = inner - moon - half;
        }
        else
        {
                int ends = done ? done + apart : 0, group, pills, rest;

                for (int each = 0; each < count; each++)
                        list[1][listed[1]++] = taskbar.tasks_right ? count - 1 - each : each;
                if (moon_zone == taskbar.tasks_zone)
                        group = inner - moon - apart - ends;
                else if (taskbar.tasks_zone == TASKBAR_ZONE_CENTRE)
                        group = inner - 2 * (moon + apart > ends ? moon + apart : ends);
                else if (moon_zone == TASKBAR_ZONE_CENTRE)
                        group = length / 2 - gap - moon / 2 - apart - (taskbar.tasks_zone == TASKBAR_ZONE_END ? ends : 0);
                else
                        group = inner - moon - apart - ends;
                pills = (trays[0] > 0) + (trays[1] > 0);
                pills += tab > 0;
                tabs[1] = taskbar_tasks_fit(view, list[1], listed[1], group - trays[0] - trays[1] - tab - pills * apart, high, pad, scale, longest, widths, titled);
                for (int zone = TASKBAR_ZONE_START; zone <= TASKBAR_ZONE_END; zone++)
                {
                        int parts = 0;

                        zones[zone] = 0;
                        if (moon_zone == zone)
                                zones[zone] += moon, parts++;
                        if (taskbar.tasks_zone == zone)
                                zones[zone] += tabs[1] + trays[0] + trays[1] + tab, parts += (tabs[1] > 0) + (trays[0] > 0) + (trays[1] > 0) + (tab > 0);
                        if (zone == TASKBAR_ZONE_END && done)
                                zones[zone] += done, parts++;
                        zones[zone] += (parts > 1 ? parts - 1 : 0) * apart;
                }
                zones[TASKBAR_ZONE_CENTRE] = zones[TASKBAR_ZONE_CENTRE] < inner ? zones[TASKBAR_ZONE_CENTRE] : inner;
                rest = inner - zones[TASKBAR_ZONE_CENTRE];
                if (zones[TASKBAR_ZONE_START] > rest / 2)
                        zones[TASKBAR_ZONE_END] = rest - zones[TASKBAR_ZONE_START];
                else if (zones[TASKBAR_ZONE_END] > rest / 2)
                        zones[TASKBAR_ZONE_START] = rest - zones[TASKBAR_ZONE_END];
                else
                        zones[TASKBAR_ZONE_START] = rest / 2, zones[TASKBAR_ZONE_END] = rest - rest / 2;
        }
        for (int zone = TASKBAR_ZONE_START; zone <= TASKBAR_ZONE_END; zone++)
                zones[zone] = zones[zone] > 0 ? zones[zone] : 0;

        // The output and the band, and in it its three places.
        ui_tree_begin(t);
        n = ui_tree_open(t);
        n->dir = c, n->justify = far ? UI_END : UI_START, n->size[0].kind = n->size[1].kind = UI_GROW;
        n->x = view->x, n->y = view->y, *ui_tree_at(n, c) += taskbar_off(view, taskbar.shift);
        n = ui_tree_open(t);
        n->dir = m, n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0};
        n->pad[2 * m] = n->pad[2 * m + 1] = gap; // across, each pill is in the middle: the inset either side of it
        for (int zone = TASKBAR_ZONE_START; zone <= TASKBAR_ZONE_END; zone++)
        {
                int first = 1;

                n = ui_tree_open(t);
                n->id = taskbar.editing ? TASKBAR_ZONE - zone + 1 : 0;
                n->dir = m, n->align = UI_CENTER, n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0};
                n->size[m] = (struct ui_size){UI_FIXED, zones[zone], 0, UI_TREE_MOST, 0};
                if (split)
                {
                        // Split: the start holds the items before the tasks and the left tasks, against the moon; the end the right tasks and
                        // the items after them, from the moon out, and the Done button at its end.
                        n->justify = zone == TASKBAR_ZONE_START ? UI_END : zone == TASKBAR_ZONE_END ? UI_START : UI_CENTER;
                        if (zone == TASKBAR_ZONE_START)
                        {
                                n->pad[2 * m + 1] = apart;
                                if (trays[1])
                                        taskbar_tray_lay(t, 1, m, 0, scale, glyph, square, cell, text_w, high), first = 0;
                                taskbar_tasks_lay(t, TASKBAR_TABS_LEFT, list[0], listed[0], widths, titled, m, first ? 0 : apart, pad, side, icon_gap, high);
                        }
                        else if (zone == TASKBAR_ZONE_CENTRE && moon)
                        {
                                n = taskbar_open(t, TASKBAR_LAUNCHER, m, 0, moon, moon);
                                taskbar_mark(t, n, moon);
                                ui_tree_close(t);
                        }
                        else
                        {
                                n->pad[2 * m] = apart;
                                taskbar_tasks_lay(t, TASKBAR_TABS, list[1], listed[1], widths, titled, m, 0, pad, side, icon_gap, high);
                                if (trays[0])
                                        taskbar_tray_lay(t, 0, m, tabs[1] ? apart : 0, scale, glyph, square, cell, text_w, high);
                                if (tab)
                                        taskbar_clock_lay(t, m, tabs[1] || trays[0] ? apart : 0, tab, high);
                                if (done)
                                {
                                        n = taskbar_open(t, TASKBAR_DONE, m, apart, done, high);
                                        ui_tree_close(t);
                                }
                        }
                        ui_tree_close(t);
                        continue;
                }
                n->justify = zone == TASKBAR_ZONE_START ? UI_START : zone == TASKBAR_ZONE_END ? UI_END : UI_CENTER;
                for (int k = 0; k < 3; k++)
                {
                        int pill = taskbar.order[k];

                        if (pill == TASKBAR_PILL_MOON && moon_zone == zone && moon)
                        {
                                // The moon: the launcher's button, bare on the band, the clock on it (taskbar_moon).
                                n = taskbar_open(t, TASKBAR_LAUNCHER, m, first ? 0 : apart, moon, moon);
                                taskbar_mark(t, n, moon);
                                ui_tree_close(t);
                                first = 0;
                        }
                        if (pill == TASKBAR_PILL_TASKS && taskbar.tasks_zone == zone)
                        {
                                if (trays[1])
                                        taskbar_tray_lay(t, 1, m, first ? 0 : apart, scale, glyph, square, cell, text_w, high), first = 0;
                                if (tabs[1] > 0)
                                        taskbar_tasks_lay(t, TASKBAR_TABS, list[1], listed[1], widths, titled, m, first ? 0 : apart, pad, side, icon_gap, high), first = 0;
                        }
                        if (pill == TASKBAR_PILL_TRAY && taskbar.tasks_zone == zone && trays[0])
                                taskbar_tray_lay(t, 0, m, first ? 0 : apart, scale, glyph, square, cell, text_w, high), first = 0;
                        if (pill == TASKBAR_PILL_TRAY && taskbar.tasks_zone == zone && tab)
                                taskbar_clock_lay(t, m, first ? 0 : apart, tab, high), first = 0;
                }
                if (zone == TASKBAR_ZONE_END && done)
                {
                        // Arranging the band: its Done button, at the end.
                        n = taskbar_open(t, TASKBAR_DONE, m, first ? 0 : apart, done, high);
                        ui_tree_close(t);
                }
                ui_tree_close(t);
        }

        // The rectangles, turned over for a language that reads from the right, and those the band has not the room for given none, their names taken back.
        ui_tree_close(t);
        ui_tree_close(t);
        ui_tree_run(t, view->width, view->height);
        if (!m && language_rtl())
                ui_tree_mirror(t);
        for (int at = 2; at < t->count; at++)
        {
                const struct ui_node address_to band = &t->node[1];

                n = &t->node[at];
                if (n->w > 0 && n->h > 0 && n->x >= band->x && n->y >= band->y && n->x + n->w <= band->x + band->w && n->y + n->h <= band->y + band->h)
                        continue;
                n->id = 0;
                n->w = n->h = 0;
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
        int arrange, editing;           // where the pills are (the zones, the side of the newest task, the items before the tasks, the order) and whether it is arranged
        unsigned char lengths[TASKBAR_TAB_ROOM]; // each tab's title in characters
};

_Static_assert(sizeof(struct taskbar_key) == 8 + 16 * 4 + TASKBAR_TAB_ROOM, "a key is compared as bytes, so it has no padding");

/*
        The highlight of the tab whose window has the keys: the kit's lit face (ui_glass_button) over that tab, which moves from one tab
        to the next when the keys do, as the kit's tabs move their pip (ui_tabs_draw's pip_x and pip_w: ui_kit_number_to over
        TASKBAR_LIT_MS on --ease, at the pace, at once with reduced motion), and fades in and out over the kit's .2 s when a tab first has
        the keys or none has. Where it is along the band from the band's start and how long it is are device pixels in Q30. A tab that
        keeps the keys while the band is laid out again (a window opened or closed beside it) has it put where the tab is now.
*/
#define TASKBAR_LIT_MS 350
#define TASKBAR_LIT_FADE_MS 200

struct taskbar_lit
{
        struct ui_tween at, length, shown;
        int row; // the row of the window table (plus one) whose tab it is over or was last; 0 none yet
};

static struct taskbar_band
{
        struct taskbar_key key;
        struct ui_tree tree;
        struct taskbar_lit lit;
        struct ui_tween grow;   // how far the moon has grown, Q30 of TASKBAR_MOON_GROW
        int moon_damage[4];     // the damage the moon made of itself (left, top, right, bottom), which nothing under the band is
        int moon_own;           // and that it is there to be told apart
        int glow_key[5];        // what its glow's pixels were drawn for (taskbar_moon_glow): its side, the growth, the accent, the theme, the buffer
        int glow_shown;         // its glow was in the last frame, so the frame after it puts the ground back where it was
} taskbar_bands[WINDOWS_OUTPUTS];

static struct ui_tree address_to taskbar_tree(const struct taskbar_view *view)
{
        struct taskbar_band address_to band = &taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0];
        struct taskbar_key key = {taskbar.shift, view->width, view->height, view->scale, view->x, view->y, (int)view->position, (int)view->count,
                                  view->lengths != null, taskbar.scale, taskbar.padding, taskbar.launcher != 0,
                                  taskbar.tray_custom ? taskbar.tray_chars : -1, 0, language_rtl(),
                                  taskbar.moon_zone | taskbar.tasks_zone << 2 | taskbar.tasks_right << 4 | (int)taskbar.tray_before << 5 |
                                      taskbar.order[0] << 12 | taskbar.order[1] << 14 | taskbar.order[2] << 16,
                                  taskbar.editing | taskbar.clock_tray << 1 | taskbar.moon_ink << 2 | taskbar.clock_tab << 3 | taskbar.clock_none << 4 |
                                      taskbar.separate << 5 | (taskbar.edge_in + 64) << 6 | taskbar.clock_chars << 14, {0}};

#define TASKBAR_CELL(slot, shown, mark, paint) key.cells = key.cells << 1 | ((shown) != 0);
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        if (view->lengths)
                memory_copy(key.lengths, view->lengths, view->count < TASKBAR_TAB_ROOM ? view->count : TASKBAR_TAB_ROOM);
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

// The band's pills in order along it, the frames the compositor makes of the band (compositor_panel_tabs): the at'th of them, its slot
// and its rectangle; 0 past the last. A pill is a named element in one of the band's three places.
static int taskbar_plate(const struct taskbar_view *view, int at, int *which, int *x, int *y, int *width, int *height)
{
        const struct ui_tree address_to t = taskbar_tree(view);

        for (int zone = t->count > 1 ? t->node[1].first : -1; zone >= 0; zone = t->node[zone].next)
                for (int k = t->node[zone].first; k >= 0; k = t->node[k].next)
                        if (t->node[k].id && !at--)
                        {
                                *which = t->node[k].id - 1;
                                taskbar_rect(&t->node[k], x, y, width, height);
                                if (*which == TASKBAR_LAUNCHER)
                                {
                                        int more = taskbar_moon_more(view);

                                        *x -= more, *y -= more, *width += 2 * more, *height += 2 * more;
                                }
                                return 1;
                        }
        return 0;
}

/*
        What is at a point of the band: a tab's number, a cell's slot, TASKBAR_TRAY for the tray (its clock and its battery are
        the tray to a press), or TASKBAR_HIT_NONE. Along the band a slot is what is drawn of it, so a point between two plates is
        none and a point on a tab is that tab; across it a slot reaches the band's edges, past the inset its plate floats in, so the
        last row of pixels of the screen is still the tab above it. With reach, a finger's: each slot reaches that much further
        across, which is toward the middle of its screen (the other way is off it), and nothing is drawn any larger (ui_tree_hit).
*/
static int taskbar_hit_reach(const struct taskbar_view *view, int px, int py, int reach)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int grown[2], at, which;

        grown[t->node[1].dir] = 0;
        grown[!t->node[1].dir] = reach + taskbar_inset(taskbar_scaled(view->scale));
        at = ui_tree_hit(t, px, py, grown);
        if (at < 0)
                return TASKBAR_HIT_NONE;
        which = t->node[at].id - 1;
        return which == -4 || which == -3 || which == -9 || which == TASKBAR_TRAY_BEFORE ? TASKBAR_TRAY
               : which == TASKBAR_TABS || which == TASKBAR_TABS_LEFT || (which <= TASKBAR_ZONE && which > TASKBAR_DONE && !taskbar.editing) ? TASKBAR_HIT_NONE
                                                                                                     : which;
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
        return which == TASKBAR_LAUNCHER ? 1ul << (TASKBAR_TAB_ROOM + 1) : which < 0 ? 1ul << TASKBAR_TAB_ROOM : 1ul << which;
}
_Static_assert(TASKBAR_TAB_ROOM + 2 <= 64, "a redo bit a tab, the tray's and the launcher's, in a word");

/*
        The slots the damage meets, with the blur's reach round each: a change
        in the scene under a tab, or under its reach, is one to its glass. The
        reach runs across the whole band and along the slot.
*/
static unsigned long taskbar_meets(const struct taskbar_view *view, int (*damage)[4], unsigned int damages)
{
        unsigned long redo = 0;

        for (int which = TASKBAR_TRAY_BEFORE; which < (int)view->count; which++)
        {
                int reach[4];

                if (which == -4 || (which == -3 && !taskbar.clock_tab) || which == TASKBAR_TABS || !taskbar_anchor(view, which, &reach[0], &reach[1], &reach[2], &reach[3]))
                        continue;
                reach[2] += reach[0] + COMPOSITOR_GLASS_REACH, reach[3] += reach[1] + COMPOSITOR_GLASS_REACH;
                reach[0] -= COMPOSITOR_GLASS_REACH, reach[1] -= COMPOSITOR_GLASS_REACH;
                for (unsigned int at = 0; at < damages; at++)
                {
                        const struct taskbar_band address_to own = &taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0];
                        int meet[4];

                        // What the moon damaged of itself is no change under the band: it is the moon's to draw again, not the others'.
                        if (which != TASKBAR_LAUNCHER && own->moon_own && !memory_compare(damage[at], own->moon_damage, sizeof(meet)))
                                continue;
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
        A plate is glass: the blurred scene under it (view->glass, the rectangle gx, gy, gw wide) made more vivid and tinted toward
        the theme's surface (compositor_tab_target), a slot of it at a time, x, y, width, height. Answers what it came to in the
        slot's middle, which the slot's buttons are drawn on (taskbar_glass_ui). The band is at bx, by and bw wide. Every plate is the
        same glass: what is chosen on it is the kit's lit face over it (ui_glass_button), not a glass of its own.
*/
static unsigned int taskbar_glass_tab(const struct taskbar_view *view, int gx, int gy, int gw, int bx, int by, int bw,
                                      int x, int y, int width, int height)
{
        // The glass is painted on the band, not recorded (R3 in the note): the frame is the picture's, and this paints it.
        if (view->scene)
                view->scene->lost = UI_LOST_GLASS;
        unsigned int middle = compositor_vivid(view->glass[(long)(y + height / 2 - gy) * gw + x - gx + width / 2]);
        unsigned int amount = 179, target = compositor_tab_target(0);

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

// The kit's drawing state over the band's pixels as a glass whose ground is what a plate came to (ui_inks), for its buttons.
static fn taskbar_glass_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t, unsigned int ground)
{
        const struct ui_node address_to band = &t->node[1];

        memory_zero(ui, sizeof(*ui));
        ui->pixels = view->pixels, ui->pitch = (unsigned long)band->w, ui->width = band->w, ui->height = band->h, ui->top = 0, ui->bottom = band->h;
        ui->scale = taskbar_scaled(view->scale), ui->scene = view->scene, ui->glass = 1;
        ui_inks(ui, ground);
}

// A slot's button face: its rectangle in the band's pixels (device 256ths), a pill, along from at for length when those are given (Q30 device pixels from the band's start).
static struct ui_box taskbar_face(const struct ui_tree address_to t, const struct ui_node address_to n, b64 at, b64 length)
{
        const struct ui_node address_to band = &t->node[1];
        int m = band->dir, x = n->x - band->x, y = n->y - band->y;
        struct ui_box face = {x * 256, y * 256, (x + n->w) * 256, (y + n->h) * 256, 0, 0};

        if (length > 0)
        {
                b32 from = (b32)(at >> (UI_Q - 8)), to = (b32)((at + length) >> (UI_Q - 8));

                if (m)
                        face.y0 = from, face.y1 = to;
                else
                        face.x0 = from, face.x1 = to;
        }
        face.rx = face.ry = (n->w < n->h ? n->w : n->h) * 128;
        return face;
}

/*
        The highlight's targets for this draw (struct taskbar_lit): over the tab that has the keys, moved there from the last one, put
        there when it is that tab still or none was shown, and gone when no tab has the keys. It is read on the kit's clock
        (ui_kit_clock: the monotonic clock, or a check's), which the compositor's loop is on.
*/
static fn taskbar_lit_to(struct taskbar_lit address_to lit, const struct ui_tree address_to t, const struct taskbar_view *view)
{
        const struct ui_node address_to band = &t->node[1], address_to tab = null;
        b32 motion = ui_kit.motion, live = 0;
        int chosen = -1, m = band->dir;

        ui_kit.now = ui_kit_clock();
        ui_kit.motion = !taskbar.glides;
        for (int at = 0; view->marks && at < (int)view->count && chosen < 0; at++)
                if (view->marks[at] & COMPOSITOR_TAB_LIT)
                        chosen = at, tab = ui_tree_find((struct ui_tree address_to)t, at + 1);
        if (tab)
        {
                int row = (view->rows ? (int)view->rows[chosen] : chosen) + 1;
                b64 at = ui_q(m ? tab->y - band->y : tab->x - band->x), length = ui_q(m ? tab->h : tab->w);
                b64 shown = ui_tween_at(&lit->shown, &live);

                if (row != lit->row && lit->at.placed && shown > 0)
                {
                        ui_kit_number_to(&lit->at, at, TASKBAR_LIT_MS, &ui_ease);
                        ui_kit_number_to(&lit->length, length, TASKBAR_LIT_MS, &ui_ease);
                }
                else if (at != (lit->at.pending ? lit->at.want : lit->at.to) || length != (lit->length.pending ? lit->length.want : lit->length.to) ||
                         !lit->at.placed)
                {
                        ui_tween_put(&lit->at, at, UI_TWEEN_NUMBER);
                        ui_tween_put(&lit->length, length, UI_TWEEN_NUMBER);
                }
                // The band's first highlight is there at once; one that comes back after none had the keys fades in.
                if (!lit->shown.placed)
                        ui_tween_put(&lit->shown, UI_ONE, UI_TWEEN_NUMBER);
                ui_kit_number_to(&lit->shown, UI_ONE, TASKBAR_LIT_FADE_MS, &ui_ease);
                lit->row = row;
        }
        else if (lit->shown.placed)
                ui_kit_number_to(&lit->shown, 0, TASKBAR_LIT_FADE_MS, &ui_ease);
        ui_kit.motion = motion;
}

// Whether a tween is still moving at now (ns on the kit's clock): a change not yet taken, or a run not yet at its end.
static int taskbar_tween_moving(const struct ui_tween address_to t, positive now)
{
        return t->pending || (t->start && (b64)now - t->start < t->duration);
}

// How far the moon of the band being drawn has grown, Q30 of TASKBAR_MOON_GROW (none with no band).
static b64 taskbar_moon_share(void)
{
        b32 live = 0;
        b64 share = ui_tween_at(&taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0].grow, &live);

        return share < 0 ? 0 : share > UI_ONE ? UI_ONE : share;
}

// The most the moon of size pixels grows each side: what its square is grown by in the band at its largest, and its stripe.
static int taskbar_moon_most(int size)
{
        return (int)((positive)size * TASKBAR_MOON_GROW / 200 + 1);
}

// How far the moon's plate reaches past its square each side now: as far as the grown disc.
static int taskbar_moon_more(const struct taskbar_view *view)
{
        int size = taskbar_thick(view->scale);

        return (int)(((positive)size * TASKBAR_MOON_GROW * (positive)taskbar_moon_share() / UI_ONE + 199) / 200);
}

// The pointer is over the moon of a band (its number), or of none (-1).
static void taskbar_moon_hover(int band)
{
        taskbar.moon_hover = band + 1;
}

// A hand did something: the moon turns again, if it had stopped.
static void taskbar_moon_seen(positive now)
{
        taskbar.moon_seen = now;
}

// The compositor damaged rect of the band for the moon alone (left, top, right, bottom).
static void taskbar_moon_damaged(int band, const int *rect)
{
        if (band < 0 || band >= WINDOWS_OUTPUTS)
                return;
        memory_copy(taskbar_bands[band].moon_damage, rect, sizeof(taskbar_bands[band].moon_damage));
        taskbar_bands[band].moon_own = 1;
}

/*
        The moon's glow: while the pointer is over the moon or the launcher is up, a halo round it of the rim light's hues (the accent above,
        the cool turn of it toward the left and the warm toward the right, ui_moon_glow_hue), so the fresnel rim's tint runs on past the limb
        into the air round it. It is as strong as the moon is grown (the same tween, the kit's ease over TASKBAR_MOON_GROW_MS, at once with
        reduced motion), strongest at the limb and falling off as the square of the way across TASKBAR_MOON_HALO percent of the moon, and
        dithered, so its fall has no rings. It reaches past the band, so it is a frame of its own under the band's plates (the compositor's,
        compositor_moon_glow_frame), premultiplied, in a square that holds the disc at its largest and the reach round it; that square is
        the moon's own damage while the glow is shown or going, and nothing else is.
*/
#define TASKBAR_MOON_HALO 45 // the glow's reach past the disc, percent of the moon's size
static unsigned int taskbar_accent(void);
#define TASKBAR_MOON_HALO_ALPHA 0.5 // its strength at the limb, the moon grown whole

// The square of the glow round the moon of the band being drawn (left, top, side, side), in the space's pixels; false with no moon.
static int taskbar_moon_glow_square(const struct taskbar_view *view, int square[4])
{
        int x, y, w, h, size, reach;

        if (!taskbar_slot(view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || w <= 0 || h <= 0 || taskbar.moon_ink)
                return 0;
        size = w < h ? w : h, reach = size * TASKBAR_MOON_HALO / 100 + taskbar_moon_most(size) + 1;
        square[0] = x + (w - size) / 2 - reach, square[1] = y + (h - size) / 2 - reach, square[2] = square[3] = size + 2 * reach;
        return 1;
}

// Whether a band's glow was shown in the last frame, and now whether it is.
static int taskbar_moon_glow_was(int band, int shown)
{
        int was;

        if (band < 0 || band >= WINDOWS_OUTPUTS)
                return 0;
        was = taskbar_bands[band].glow_shown, taskbar_bands[band].glow_shown = shown;
        return was;
}

/*
        The glow's pixels for the band being drawn into pixels (its square's, a row of square[2]), premultiplied ARGB: false when there is
        none now (the moon at rest). Drawn again only when the growth, the size, the accent or the theme changed since the last.
*/
static int taskbar_moon_glow(const struct taskbar_view *view, unsigned int *pixels, const int square[4])
{
        struct taskbar_band address_to band = &taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0];
        b64 share = taskbar_moon_share();
        int side = square[2], size, key[5];
        unsigned int accent = taskbar_accent();
        decimal strength, grown, r, reach, c = side / 2.0;

        if (share <= 0)
                return 0;
        // The moon's size, as its slot has it.
        {
                int x, y, w, h;

                if (!taskbar_slot(view, TASKBAR_LAUNCHER, &x, &y, &w, &h))
                        return 0;
                size = w < h ? w : h;
        }
        key[0] = side, key[1] = (int)(share >> 10), key[2] = (int)accent, key[3] = window_light(compositor_tab_target(0)), key[4] = (int)(positive)pixels;
        if (!memory_compare(band->glow_key, key, sizeof(key)))
                return 1;
        memory_copy(band->glow_key, key, sizeof(key));
        strength = TASKBAR_MOON_HALO_ALPHA * (decimal)share / (decimal)UI_ONE;
        grown = size * (1 + (decimal)share / (decimal)UI_ONE * TASKBAR_MOON_GROW / 100.0), r = grown / 2, reach = size * TASKBAR_MOON_HALO / 100.0;
        for (int y = 0; y < side; y++)
                for (int x = 0; x < side; x++)
                {
                        decimal dx = x + 0.5 - c, dy = y + 0.5 - c, d = square_root(dx * dx + dy * dy), t = (d - r) / reach, a, value[4];
                        unsigned int h = ui_moon_hash((unsigned int)x * 73856093u ^ (unsigned int)y * 19349663u), hue, out = 0;
                        decimal dither = ((h & 255) + (h >> 8 & 255)) / 512.0 - 0.5;

                        // Under the disc nothing (the plate is over it), a pixel's ramp in at the limb, then the fall.
                        a = d < r - 1 || t >= 1 ? 0 : strength * (t < 0 ? 1 : (1 - t) * (1 - t)) * (d < r ? d - (r - 1) : 1);
                        if (a <= 0)
                        {
                                pixels[(long)y * side + x] = 0;
                                continue;
                        }
                        hue = ui_moon_glow_hue(accent, dx / r < -1 ? -1 : dx / r > 1 ? 1 : dx / r);
                        value[3] = a * 255 + dither;
                        value[3] = value[3] < 0 ? 0 : value[3] > 255 ? 255 : value[3];
                        for (int k = 0; k < 3; k++)
                        {
                                value[k] = (hue >> (16 - 8 * k) & 255) * a + dither;
                                value[k] = value[k] < 0 ? 0 : value[k] > value[3] ? value[3] : value[k];
                        }
                        out = (unsigned int)(value[3] + 0.5) << 24 | (unsigned int)(value[0] + 0.5) << 16 | (unsigned int)(value[1] + 0.5) << 8 | (unsigned int)(value[2] + 0.5);
                        pixels[(long)y * side + x] = out;
                }
        return 1;
}

// `taskbar tasks grouped|separate` (separate 1) and `taskbar edge` (logical pixels, TASKBAR_EDGE_LEAST to TASKBAR_EDGE_MOST, else 0).
static void taskbar_bar_set(int separate, int edge)
{
        taskbar.separate = separate != 0;
        taskbar.edge_in = edge >= TASKBAR_EDGE_LEAST && edge <= TASKBAR_EDGE_MOST ? edge : 0;
        taskbar.stale = 1;
}

// `taskbar moon spin`: seconds a turn, 10 to 3600, or 0 for none.
static void taskbar_spin_set(int seconds)
{
        taskbar.spin = seconds >= 10 && seconds <= 3600 ? seconds : seconds == 0 ? 0 : TASKBAR_MOON_SPIN;
}

// Whether the moon is turning at now: the motion on, a spin, a hand seen within TASKBAR_MOON_IDLE, and no check's time.
static int taskbar_moon_turning(positive now)
{
        return taskbar.glides && taskbar.spin > 0 && !taskbar.moon_at && now - taskbar.moon_seen < TASKBAR_MOON_IDLE;
}

/*
        The moon's motion at now, asked on every pass of the compositor's loop before a frame (launcher: whether the launcher is up): the turn
        taken on to the step the real clock is at, and each band's growth toward its pointer and its launcher. Answers whether the moon is
        to be drawn again: a step taken, or a growth still going.
*/
static int taskbar_moon_tick(positive now, int launcher)
{
        int changed = 0;
        b32 motion = ui_kit.motion;

        if (!taskbar.moon_seen)
                taskbar.moon_seen = now;
        if (taskbar_moon_turning(now))
        {
                timespec real = {0, 0};
                long long step, period = (long long)taskbar.spin * TASKBAR_MOON_STEPS;

                clock_gettime(CLOCK_REALTIME, address_of real);
                step = (long long)real.tv_sec * TASKBAR_MOON_STEPS + real.tv_nsec / (1000000000l / TASKBAR_MOON_STEPS);
                if (step != taskbar.moon_step)
                {
                        taskbar.moon_step = step;
                        taskbar.moon_turn = (b64)(step % period) * UI_ONE / (b64)period;
                        changed = 1;
                }
        }
        ui_kit.now = ui_kit_clock();
        ui_kit.motion = !taskbar.glides;
        for (int at = 0; at < WINDOWS_OUTPUTS; at++)
        {
                struct ui_tween address_to grow = &taskbar_bands[at].grow;
                b64 want = launcher || taskbar.moon_hover == at + 1 ? UI_ONE : 0;

                if (!grow->placed)
                        ui_tween_put(grow, 0, UI_TWEEN_NUMBER);
                if (want != (grow->pending ? grow->want : grow->to))
                {
                        b32 live = 0;

                        // Begun now, not at the next draw.
                        ui_kit_number_to(grow, want, TASKBAR_MOON_GROW_MS, &ui_ease), changed = 1;
                        ui_tween_at(grow, &live);
                }
                changed |= taskbar_tween_moving(grow, ui_kit.now);
        }
        ui_kit.motion = motion;
        return changed;
}

// When the moon is next to be drawn for its turn (the monotonic clock), or 0 while it is not turning.
static positive taskbar_moon_due(positive now)
{
        timespec real = {0, 0};
        positive each = 1000000000ull / TASKBAR_MOON_STEPS;

        if (!taskbar_moon_turning(now))
                return 0;
        clock_gettime(CLOCK_REALTIME, address_of real);
        return now + each - (positive)real.tv_nsec % each + 1000000ull;
}

// Whether a band's highlight is still moving at now: its band is damaged again after each present until it settles.
static int taskbar_moving(positive now)
{
        for (int at = 0; at < WINDOWS_OUTPUTS; at++)
        {
                const struct taskbar_lit address_to lit = &taskbar_bands[at].lit;

                if (taskbar_tween_moving(&lit->at, now) || taskbar_tween_moving(&lit->length, now) || taskbar_tween_moving(&lit->shown, now))
                        return 1;
        }
        return 0;
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
        positive used = 0, value;

        while (*text == ' ')
                text++;
        value = string_digits((string_address)text, &used);
        return used ? (long)value : 0;
}

// The value of a /proc/meminfo key in kB, 0 when it is not there.
static long taskbar_tray_meminfo(const char *text, const char *key)
{
        positive size = string_length((string_address)key);

        for (string_address line = (string_address)text; line && *line;)
        {
                string_address nl;

                if (!string_compare_max(line, (string_address)key, size))
                        return taskbar_tray_number((const char *)(line + size));
                nl = string_first_of(line, '\n');
                if (!nl)
                        break;
                line = nl + 1;
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

static int taskbar_format_render(const char *format, char *into, int room);

static void taskbar_tray_render(void)
{
        // The words of the tray are the language the compositor has published in the window table (its program reads /root/language).
        language_take(compositor.table ? compositor.table->language : 0);
        taskbar.tray_text[0] = 0, taskbar.clock_text[0] = 0;
        taskbar.tray_chars = taskbar.tray_custom ? taskbar_format_render(taskbar.tray_format, taskbar.tray_text, (int)sizeof(taskbar.tray_text)) : 0;
        taskbar.clock_chars = taskbar.clock_format[0] ? taskbar_format_render(taskbar.clock_format, taskbar.clock_text, (int)sizeof(taskbar.clock_text)) : 0;
}

// A format's text this minute into into (room bytes), its names filled in (setting_tray_name); answers its characters.
static int taskbar_format_render(const char *format, char *into, int room)
{
        int at = 0, chars = 0;
        timespec now = {0, 0};
        time_t seconds;
        tm broken;

        into[0] = 0;
        clock_gettime(CLOCK_REALTIME, address_of now);
        seconds = now.tv_sec;
        if (!localtime_r(&seconds, &broken))
                return 0;
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
                        chars++;
        return chars;
}

// What the clock outside the moon says: its format's text, or the hours and minutes the band has (clock) when it has none.
static const char *taskbar_clock_said(const char *clock)
{
        return taskbar.clock_chars > 0 ? taskbar.clock_text : clock;
}

// `taskbar clock` outside the moon, and `time format` and `time date`: the format its text is (the time in the hours it is read in, the date
// after it), the standard tray's clock otherwise.
static void taskbar_clock_set(const char *format)
{
        int at = 0;

        while (format[at] && at < SETTING_TRAY_MOST)
                taskbar.clock_format[at] = format[at], at++;
        taskbar.clock_format[at] = 0;
        taskbar_tray_render();
        taskbar.stale = 1;
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
        The moon: the launcher's button, bare on the band (no glass and no padding: its round frame is the disc, compositor_panel_tabs),
        the kit's moon (ui_moon) full and face on, its limb brighter (a rim of light, neutral), turning once an hour a step a minute (it
        moves only when the clock does: the band is drawn again each minute, taskbar_tick; with the compositor's motion off it stands
        still), its rim the brighter while the launcher is up. On it the clock (ui_moon_label), unless it is the tray's (`taskbar clock
        tray`): the hours over the minutes, bold, on the sphere at taskbar.clock_bulge, on a backing of their own shape, the ink's
        reading against what is behind it kept in taskbar.clock_read. In twelve hours the hours are so, and AM and PM are the menu's.
*/
// The accent the moon's rim light is made from: the theme's, or a cool blue where the theme's is a grey (the standard's white).
static unsigned int taskbar_accent(void);

// The accent the wallpaper's glow is in: the taskbar's, so the moon on the band and the moon on the ground are lit alike.
static unsigned int taskbar_wall_accent(void)
{
        return taskbar_accent();
}

/*
        The wallpaper of one screen, width by height pixels at pixels (pitch a row), for compositor_wall_keep: the moon over its sky
        (ui_moon_night), in the dark theme dimmed (its seas maria percent of its highlands) or inverted, in the light one ink on paper,
        the accent's glow round it (glow 1) and along the screen's edges too (glow 2).
*/
static void taskbar_wall_draw(unsigned int *pixels, int pitch, int width, int height, int dark, int dimmed, int maria, int glow, unsigned int accent,
                              b64 turn, int moon_alone, int rect[4])
{
        struct ui ui;

        memory_zero(&ui, sizeof(ui));
        ui.pixels = pixels, ui.pitch = (unsigned long)pitch, ui.width = width, ui.height = height, ui.top = 0, ui.bottom = height, ui.scale = 100;
        ui_moon_dim_seas = maria / 100.0;
        ui_moon_night_part(&ui, turn, dark ? UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | (dimmed ? UI_MOON_PAPER_DIM : 0) : 0, accent, glow ? 1 : 0,
                           glow > 1 ? 1 : 0, moon_alone, rect);
}

// Whether the compositor's motion is on and a hand was about within TASKBAR_MOON_IDLE: what the wallpaper's moon turns by too.
static int taskbar_motion_about(positive now)
{
        return taskbar.glides && !taskbar.moon_at && now - taskbar.moon_seen < TASKBAR_MOON_IDLE;
}

static unsigned int taskbar_accent(void)
{
        unsigned int accent = compositor.table && compositor.table->themed ? compositor.table->accent & 0xffffffu : 0;
        int r = (int)(accent >> 16 & 255), g = (int)(accent >> 8 & 255), b = (int)(accent & 255), most = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int least = r < g ? (r < b ? r : b) : (g < b ? g : b);

        return most - least < 40 ? 0x5b7fa6u : accent;
}

/*
        The moon's disc at rest, kept a frame for each step of its turn: the steps of one turn (`taskbar moon spin` seconds,
        TASKBAR_MOON_STEPS a second) at one size and one look, so after the first turn a step is a copy of its square and no more. The disc
        is drawn on its own square (UI_MOON_FULL_SQUARE), so what is behind it is no part of it. Up to TASKBAR_MOON_KEPT bytes; a moon
        larger, grown, at a check's time or in ink is drawn each time.
*/
#define TASKBAR_MOON_KEPT (8ul << 20)

static unsigned int address_to taskbar_moon_kept(int size, unsigned int look, b32 address_to fresh)
{
        static struct
        {
                unsigned int address_to frames;
                unsigned char address_to have;
                positive bytes;
                int size, period;
                unsigned int look;
        } kept;
        int period = taskbar.spin * TASKBAR_MOON_STEPS;
        positive frame = (positive)size * (positive)size, bytes = frame * 4 * (positive)period + (positive)period;
        long step = (long)(taskbar.moon_step % (period > 0 ? period : 1));

        *fresh = 0;
        if (period <= 0 || size <= 0 || bytes > TASKBAR_MOON_KEPT || taskbar.moon_at)
                return null;
        if (kept.size != size || kept.period != period || kept.look != look || !kept.frames)
        {
                if (kept.frames)
                        compositor_call(syscall(munmap), (positive)kept.frames, kept.bytes, 0, 0, 0, 0);
                kept.frames = null;
                {
                        long got = ui_map(bytes);

                        if (got < 0 && got > -4096)
                                return null;
                        kept.frames = (unsigned int address_to)got, kept.have = (unsigned char address_to)(got + (long)(frame * 4 * (positive)period));
                }
                kept.bytes = bytes, kept.size = size, kept.period = period, kept.look = look;
        }
        *fresh = !kept.have[step];
        kept.have[step] = 1;
        return kept.frames + frame * (positive)step;
}

static void taskbar_moon(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, int hours, int minutes, int lit)
{
        struct ui ui;
        int x, y;
        const struct ui_node address_to mark = taskbar_ui(&ui, view, t, n, &x, &y);
        int size = mark->w < mark->h ? mark->w : mark->h;
        long long seconds = taskbar.moon_at;
        b64 turn;
        decimal grow = (decimal)taskbar_moon_share() / (decimal)UI_ONE * TASKBAR_MOON_GROW / 100;
        int grown = size + (int)(size * grow + 0.5), left = x + (size - grown) / 2, top = y + (size - grown) / 2;
        // The rim is light alone: brighter toward white on a dark theme, darker toward a mid grey on a light one, so the disc's edge shows
        // on either; white while the launcher is up.
        int light = window_light(compositor_tab_target(0));
        struct ui_moon_look look = {0xff0c0d10u, 0xfff7f5efu, 0, lit ? 0xffffffffu : light ? 0xa0868686u : 0xb4e8e8e8u, UI_MOON_RIM | UI_MOON_FULL_SQUARE, 0};

        if (!seconds)
        {
                timespec now = {0, 0};

                clock_gettime(CLOCK_REALTIME, address_of now);
                seconds = now.tv_sec;
        }
        // A check's or a picture's moon at its time, a step a minute; else where its turn has got to (taskbar_moon_tick).
        turn = taskbar.moon_at ? (taskbar.glides ? (b64)(seconds / 60 % 60) * UI_ONE / 60 : 0) : taskbar.moon_turn;
        look.grow = grow;
        if (taskbar.moon_ink)
        {
                // In ink (`taskbar moon look ink`): the wallpaper's moon held to the button's size, ink on the paper's tone in light and
                // chalk on charcoal in dark; the square round the disc in the outline's ink first, for the round frame's edge.
                unsigned int paper, ink;

                ui_moon_paper_tones(light ? 0 : UI_MOON_PAPER_DARK, &paper, &ink);
                for (int row = 0; row < grown; row++)
                        for (int column = 0; column < grown; column++)
                                if (top + row >= 0 && top + row < ui.height && left + column >= 0 && left + column < ui.width)
                                        ui.pixels[(long)(top + row) * (long)ui.pitch + left + column] = ink;
                ui_moon_paper(&ui, left, top, grown, turn, light ? 0 : UI_MOON_PAPER_DARK, 0);
        }
        else
        {
                b32 fresh = 0;
                unsigned int address_to frame = grow > 0 || x < 0 || y < 0 || x + size > ui.width || y + size > ui.height ? null
                                                                                                                          : taskbar_moon_kept(size, look.rim, &fresh);

                if (frame && !fresh)
                        for (int row = 0; row < size; row++)
                                memory_copy(ui.pixels + (long)(y + row) * (long)ui.pitch + x, frame + row * size, (positive)size * 4);
                else
                {
                        ui_moon(&ui, x, y, size, UI_ONE / 2, turn, &look);
                        for (int row = 0; frame && row < size; row++)
                                memory_copy(frame + row * size, ui.pixels + (long)(y + row) * (long)ui.pitch + x, (positive)size * 4);
                }
        }
        // Lit at its rim as the wallpaper's moon is, by the glow's hues from the theme's accent (ui_moon_rim), a third stronger than it was.
        ui_moon_rim(&ui, left, top, grown, taskbar_accent(), 1.2);
        x = left, y = top, size = grown;
        if (taskbar.clock_tray || taskbar.clock_tab || taskbar.clock_none || hours < 0 || !view->engine || !view->face)
                return;
        {
                int shown = taskbar.clock_12h ? (hours % 12 ? hours % 12 : 12) : hours;
                char upper[3] = {(char)('0' + shown / 10), (char)('0' + shown % 10), 0}, lower[3] = {(char)('0' + minutes / 10), (char)('0' + minutes % 10), 0};

                // Twelve hours are written as they are read, 4 over 20, not 04; twenty-four keep their two digits.
                if (taskbar.clock_12h && shown < 10)
                        upper[0] = upper[1], upper[1] = 0;

                taskbar.clock_read = (int)ui_moon_label(&ui, view->engine, view->bold ? view->bold : view->face, x, y, size, upper, lower,
                                                        taskbar.clock_bulge / 100.0, taskbar_accent());
        }
}

/*
        A tab's icon, in the element the band's tree has for it (the first in the tab): the window's own picture where it has one, made
        the element's size once and kept until the picture or the size changes (taskbar_pictures, by the window's row of the table;
        ui_icon_scale), and laid over the tab's glass a row at a time by the library's over (ui_icon_picture); for a kit program with
        none, the kit's symbol for it in the tab's ink, so it follows the theme and dims with a tab put away; for any other program
        that says who it is, its lettered tile; and for a window that says nothing, the window symbol.
*/
static struct taskbar_picture
{
        const unsigned int *from; // the picture it was made from
        unsigned int seq;         // and that picture's sequence
        int side;                 // how large it was made
        unsigned int pixels[UI_ICON_MOST * UI_ICON_MOST];
} taskbar_pictures[TASKBAR_TAB_ROOM]; // by the window's row, past the room by its row's place among them (a picture made again only when it differs)

static void taskbar_tab_icon(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, int which, unsigned int ink)
{
        const struct ui_node address_to band = &t->node[1];
        const char *app = view->apps[which];
        int x = n->x - band->x, y = n->y - band->y, side = n->w < n->h ? n->w : n->h, symbol;
        struct ui ui;

        if (side <= 0 || x < 0 || y < 0 || x + side > band->w || y + side > band->h)
                return;
        memory_zero(&ui, sizeof(ui));
        ui.pixels = view->pixels, ui.pitch = (unsigned long)band->w, ui.width = band->w, ui.height = band->h, ui.top = 0, ui.bottom = band->h;
        ui.scale = taskbar_scaled(view->scale), ui.scene = view->scene, ui.engine = view->engine, ui.regular = view->face;
        if (view->icons[which] && view->icon_seqs[which] && side <= UI_ICON_MOST)
        {
                struct taskbar_picture address_to kept = &taskbar_pictures[(view->rows ? view->rows[which] : (unsigned int)which) % TASKBAR_TAB_ROOM];

                if (kept->from != view->icons[which] || kept->seq != view->icon_seqs[which] || kept->side != side)
                {
                        ui_icon_scale(kept->pixels, side, view->icons[which]);
                        kept->from = view->icons[which], kept->seq = view->icon_seqs[which], kept->side = side;
                }
                ui_icon_picture(&ui, x, y, side, kept->pixels);
                return;
        }
        symbol = ui_icon_of(app);
        if (symbol < 0 && app && app[0])
                ui_icon_tile(&ui, x, y, side, app);
        else
                ui_icon(&ui, symbol < 0 ? UI_ICON_WINDOW : symbol, x, y, side, 0, ink | 0xff000000u);
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
        The band drawn over what is under it: each slot redone made glass (its stretch of its plate) with its button on it, and the
        tray made glass with its cells on it. Only the slots in redo are made again, and only their span is damaged: their stripes of
        the band, from the first to the last of them along the edge. Answers 1 with rect set to the damage, or 0 for none. The glass
        is made by glass over the span: it answers the row stride, or 0. The elements are drawn in the order the band's tree has them,
        each by its name: a slot with glass of its own (the button, a tab, the tray) makes it and draws its button on it (the kit's,
        ui_glass_button: the launcher's lit while the launcher is up, and over the tabs the highlight where it is now, cut to each),
        and a cell of the tray, which comes after the tray, is drawn on the tray's.
*/
static int taskbar_draw(const struct taskbar_view *view, unsigned long redo, taskbar_glass_fn glass, void *context,
                        int *rect)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        const struct ui_node address_to band = &t->node[1];
        struct taskbar_lit address_to lit = &taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0].lit;
        int span[4] = {0, 0, 0, 0}, stripe[4], gw, time_ok;
        timespec now = {0, 0};
        time_t seconds;
        tm broken;
        unsigned int ground = 0, ink = 0;
        b64 lit_at, lit_length, lit_shown;
        b32 live = 0;
        char clock[6] = {0}, text[8];

        taskbar.stale = 0;
        taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0].moon_own = 0;
        if (!view->pixels || (unsigned long)band->w * (unsigned long)band->h * 4 > view->band_bytes)
                return 0;
        for (int which = TASKBAR_TRAY_BEFORE; which < (int)view->count; which++)
                if (which != -4 && (which != -3 || taskbar.clock_tab) && which != TASKBAR_TABS && (redo & taskbar_bit(which)) &&
                    taskbar_anchor(view, which, &stripe[0], &stripe[1], &stripe[2], &stripe[3]))
                {
                        // The moon's along the band as far as it grows, into the gap round it.
                        if (which == TASKBAR_LAUNCHER)
                        {
                                int most = taskbar_moon_most(stripe[band->dir ? 3 : 2]);

                                if (band->dir)
                                        stripe[1] -= most, stripe[3] += 2 * most;
                                else
                                        stripe[0] -= most, stripe[2] += 2 * most;
                        }
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
        seconds = taskbar.moon_at ? taskbar.moon_at : now.tv_sec; // a check's or a picture's time, the clock's as the moon's
        time_ok = localtime_r(&seconds, &broken) != 0;
        if (time_ok)
                clock[0] = (char)('0' + broken.tm_hour / 10), clock[1] = (char)('0' + broken.tm_hour % 10), clock[2] = ':',
                clock[3] = (char)('0' + broken.tm_min / 10), clock[4] = (char)('0' + broken.tm_min % 10);
        taskbar_battery_text(text, taskbar.battery_charging, taskbar.battery_percent);
        taskbar_lit_to(lit, t, view);
        lit_at = ui_tween_at(&lit->at, &live), lit_length = ui_tween_at(&lit->length, &live), lit_shown = ui_tween_at(&lit->shown, &live);
        for (int at = 2; at < t->count; at++)
        {
                const struct ui_node address_to n = &t->node[at];
                int which = n->id - 1, m = band->dir, s = taskbar_scaled(view->scale);
                struct ui ui;

                // The tray, and so its cells, is drawn only with the time to say. The tabs' plate is its tabs.
                if (!n->id || which == TASKBAR_TABS || which == TASKBAR_TABS_LEFT || (which <= TASKBAR_ZONE && which > TASKBAR_DONE) || !(redo & taskbar_bit(which)) ||
                    (which < 0 && which != TASKBAR_LAUNCHER && which != TASKBAR_DONE && !time_ok))
                        continue;
                if (which >= 0 || which == TASKBAR_TRAY || which == TASKBAR_TRAY_BEFORE || which == TASKBAR_DONE || (which == -3 && taskbar.clock_tab))
                {
                        // A tab is lit while its window has the keys, and the launcher's button while the launcher is up; the tray never is.
                        unsigned int mark = which >= 0 ? view->marks[which] : which == TASKBAR_LAUNCHER && view->launcher_lit ? COMPOSITOR_TAB_LIT : 0;

                        ground = taskbar_glass_tab(view, rect[0], rect[1], gw, band->x, band->y, band->w, n->x, n->y, n->w, n->h);
                        taskbar_glass_ui(&ui, view, t, ground);
                        if (which >= 0)
                        {
                                // The highlight, where it is now, cut to this tab: the part of it that is over the tab.
                                struct ui_box face = taskbar_face(t, n, lit_at, lit_length), clip = taskbar_face(t, n, 0, 0);
                                unsigned int full = ui_kit_lit(&ui);

                                clip.rx = clip.ry = 0;
                                if (lit_length > 0)
                                        ui_glass_button(&ui, &face, lit_shown, &clip);
                                ground = mark & COMPOSITOR_TAB_LIT ? window_mix(ground, full & 0xffffffu, (full >> 24) + (full >> 31)) : ground;
                        }
                        else if (which == TASKBAR_DONE)
                        {
                                struct ui_box face = taskbar_face(t, n, 0, 0);

                                ground = ui_glass_button(&ui, &face, UI_ONE, null);
                        }
                        ink = which >= 0 ? taskbar_ink_on(ground, mark) : window_contrast(ground);
                }
                if (taskbar.editing && (which >= 0 || taskbar_item_of(which) >= 0))
                {
                        // Arranging: each item that can be moved ringed, a thin line of its ink a little in from its edges.
                        struct ui grip;
                        struct ui_box ring = taskbar_face(t, n, 0, 0);
                        b32 in = (b32)(s / 100 > 1 ? s / 100 : 1) * 256 * 2;

                        taskbar_glass_ui(&grip, view, t, ground);
                        ring.x0 += in, ring.y0 += in, ring.x1 -= in, ring.y1 -= in;
                        ring.rx = ring.ry = ((ring.x1 - ring.x0 < ring.y1 - ring.y0 ? ring.x1 - ring.x0 : ring.y1 - ring.y0) / 2);
                        ui_kit_ring(&grip, &ring, (b32)(s * 256 / 100 > 256 ? s * 256 / 100 : 256), (ink & 0xffffffu) | 0x66000000u);
                }
                switch (which)
                {
                case TASKBAR_LAUNCHER: // the moon, the clock on it; ringed over it while the band is arranged
                        taskbar_moon(view, t, n, time_ok ? broken.tm_hour : -1, time_ok ? broken.tm_min : -1, view->launcher_lit);
                        if (taskbar.editing)
                        {
                                struct ui grip;
                                struct ui_box ring = taskbar_face(t, n, 0, 0);
                                b32 in = (b32)(s / 100 > 1 ? s / 100 : 1) * 256 * 2;

                                taskbar_glass_ui(&grip, view, t, ground);
                                ring.x0 += in, ring.y0 += in, ring.x1 -= in, ring.y1 -= in;
                                ring.rx = ring.ry = ((ring.x1 - ring.x0 < ring.y1 - ring.y0 ? ring.x1 - ring.x0 : ring.y1 - ring.y0) / 2);
                                ui_kit_ring(&grip, &ring, (b32)(s * 256 / 100 > 256 ? s * 256 / 100 : 256), (window_contrast(ground) & 0xffffffu) | 0x99000000u);
                        }
                        break;
                case TASKBAR_TRAY:
                case TASKBAR_TRAY_BEFORE:
                        break;
                case TASKBAR_DONE: // its word on its lit face
                        taskbar_say(view, t, n, (const char *)WORDS(tray_done), ink, ground);
                        break;
                case -9: // a format of the person's is drawn in its text cell after the tray icons
                        if (taskbar.tray_custom)
                                taskbar_say(view, t, n, taskbar.tray_text, ink, ground);
                        break;
#define TASKBAR_CELL(slot, shown, mark, paint) case slot: paint; break;
                TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
                default: // a window's tab: its icon, then its title, which a band on a side writes turned and a bottom or top one upright
                {
                        const struct ui_node address_to icon = n->first >= 0 ? &t->node[n->first] : null;
                        const struct ui_node address_to label = icon && icon->next >= 0 ? &t->node[icon->next] : null;

                        // Each only where the tab has the room for it whole: a tab too short for its icon and its padding draws none of what does not fit.
                        if (icon && icon->x >= n->x && icon->y >= n->y && icon->x + icon->w <= n->x + n->w && icon->y + icon->h <= n->y + n->h)
                                taskbar_tab_icon(view, t, icon, which, ink);
                        if (label && label->w > 0 && label->h > 0 && label->x >= n->x && label->y >= n->y && label->x + label->w <= n->x + n->w &&
                            label->y + label->h <= n->y + n->h && view->titles[which] && view->titles[which][0])
                                taskbar_say(view, t, label, view->titles[which], ink, ground);
                }
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

// `taskbar launcher`: on, a press on the moon (and the logo key alone) opens the launcher beside it; off, the moon is the clock's alone.
static void taskbar_launcher_set(int on)
{
        taskbar.launcher = on != 0;
        taskbar.stale = 1;
}

static int taskbar_launcher_on(void)
{
        return taskbar.launcher;
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

/*
        The arrangement as its settings keep it, each the index of its word among its row's choices (registry.c), which is what
        compositor_setting_kept reads: `taskbar moon` (start center end), `taskbar tasks` (split start center end), `taskbar newest`
        (left right), `taskbar order` (the six orders of moon, tasks and tray, as the row names them), a bit for each tray item before the
        tasks (taskbar_item_of's), `taskbar moon look` (realistic ink), `taskbar clock` (moon tray), its bulge in percent, and twelve
        hours. A word out of range is the standard.
*/
static void taskbar_arrange_set(int moon, int tasks, int newest, int order, unsigned int before, int look, int clock, int bulge, int twelve)
{
        static const unsigned char orders[6][3] = {{TASKBAR_PILL_MOON, TASKBAR_PILL_TASKS, TASKBAR_PILL_TRAY}, {TASKBAR_PILL_MOON, TASKBAR_PILL_TRAY, TASKBAR_PILL_TASKS},
                                                   {TASKBAR_PILL_TASKS, TASKBAR_PILL_MOON, TASKBAR_PILL_TRAY}, {TASKBAR_PILL_TASKS, TASKBAR_PILL_TRAY, TASKBAR_PILL_MOON},
                                                   {TASKBAR_PILL_TRAY, TASKBAR_PILL_MOON, TASKBAR_PILL_TASKS}, {TASKBAR_PILL_TRAY, TASKBAR_PILL_TASKS, TASKBAR_PILL_MOON}};
        static const int zones[4] = {TASKBAR_ZONE_SPLIT, TASKBAR_ZONE_START, TASKBAR_ZONE_CENTRE, TASKBAR_ZONE_END};

        taskbar.moon_zone = moon >= 0 && moon < 3 ? moon : TASKBAR_ZONE_CENTRE;
        taskbar.tasks_zone = tasks >= 0 && tasks < 4 ? zones[tasks] : TASKBAR_ZONE_SPLIT;
        taskbar.tasks_right = newest == 1;
        memory_copy(taskbar.order, orders[order >= 0 && order < 6 ? order : 0], sizeof(taskbar.order));
        taskbar.tray_before = before & 63u;
        taskbar.moon_ink = look == 1;
        // The clock: in the moon (0), nowhere (1), a cell of the tray (2) or a pill of its own (3).
        taskbar.clock_none = clock == 1, taskbar.clock_tray = clock == 2 || clock == 3, taskbar.clock_tab = clock == 3;
        taskbar.clock_bulge = bulge >= 0 && bulge <= 100 ? bulge : 60;
        taskbar.clock_12h = twelve != 0;
        taskbar.stale = 1;
}

/*
        The moon's menu: a right press on the moon (a finger's long press is one) opens the kit's menu (ui_popmenu) beside the moon, on the
        screen's side of the band. Under its header (AM or PM, while the clock is read in twelve hours, which the moon's digits do not
        say) are Edit taskbar, which puts the band into its arrangement (taskbar.editing), Settings, and the other clock format (24-hour
        clock while it is 12, 12-hour clock while it is 24), kept through `time format` as the verb keeps it. The compositor draws it as
        a frame of its own over everything and gives it the pointer while it is up; a press anywhere but on it, a wheel's turn or Escape
        puts it away (taskbar_menu_hand).
*/
static struct
{
        struct ui ui;
        struct ui_popmenu menu;
        int open, x, y, width, height; // where it is in the space, and its size, device pixels
        b32 dirty;                     // drawn again at the next present
} taskbar_menu;

// The kit's ui the menu is drawn with: the band's faces and engine, the band's scale, the theme's surface for its ground.
static fn taskbar_menu_ui(const struct taskbar_view *view, unsigned int address_to pixels)
{
        struct ui address_to ui = &taskbar_menu.ui;

        memory_zero(ui, sizeof(*ui));
        ui->engine = view->engine, ui->regular = view->face, ui->bold = view->bold ? view->bold : view->face;
        ui->scale = taskbar_scaled(view->scale), ui->opaque = 1;
        ui->pixels = pixels, ui->pitch = (unsigned long)taskbar_menu.width, ui->width = taskbar_menu.width, ui->height = taskbar_menu.height;
        ui->top = 0, ui->bottom = taskbar_menu.height;
        ui_inks(ui, compositor_tab_target(0) & 0xffffffu);
}

// The menu made and placed beside the moon of the band being drawn: false with no moon or no face to write it with.
static int taskbar_menu_open(const struct taskbar_view *view)
{
        int x, y, w, h, gap;

        if (!view->engine || !view->face || !taskbar_anchor(view, TASKBAR_LAUNCHER, &x, &y, &w, &h))
                return 0;
        memory_zero(&taskbar_menu.menu, sizeof(taskbar_menu.menu));
        taskbar_menu.menu.hot = -1;
        if (taskbar.clock_12h)
        {
                timespec now = {0, 0};
                time_t seconds;
                tm broken;

                clock_gettime(CLOCK_REALTIME, address_of now);
                seconds = taskbar.moon_at ? taskbar.moon_at : now.tv_sec;
                if (localtime_r(&seconds, &broken))
                        ui_popmenu_add(&taskbar_menu.menu, broken.tm_hour < 12 ? WORDS(tray_am) : WORDS(tray_pm), null, UI_MENU_HEADER, 0);
        }
        ui_popmenu_add(&taskbar_menu.menu, WORDS(tray_edit), null, 0, TASKBAR_MENU_EDIT);
        ui_popmenu_add(&taskbar_menu.menu, WORDS(tray_settings), null, 0, TASKBAR_MENU_SETTINGS);
        ui_popmenu_add(&taskbar_menu.menu, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(&taskbar_menu.menu, taskbar.clock_12h ? WORDS(tray_clock_24h) : WORDS(tray_clock_12h), null, 0, TASKBAR_MENU_CLOCK);
        taskbar_menu.width = taskbar_menu.height = 1;
        taskbar_menu_ui(view, null);
        ui_popmenu_size(&taskbar_menu.ui, &taskbar_menu.menu, view->height / 2);
        taskbar_menu.width = taskbar_menu.menu.width, taskbar_menu.height = taskbar_menu.menu.height;
        if (taskbar_menu.width <= 0 || taskbar_menu.height <= 0)
                return 0;
        // Beside the moon on the screen's side of the band, its middle on the moon's, held inside the screen.
        gap = taskbar_scaled(view->scale) * 6 / 100;
        taskbar_menu.x = x + w / 2 - taskbar_menu.width / 2, taskbar_menu.y = y + h / 2 - taskbar_menu.height / 2;
        if (view->position == TASKBAR_BOTTOM)
                taskbar_menu.y = y - gap - taskbar_menu.height;
        else if (view->position == TASKBAR_TOP)
                taskbar_menu.y = y + h + gap;
        else if (view->position == TASKBAR_LEFT)
                taskbar_menu.x = x + w + gap;
        else
                taskbar_menu.x = x - gap - taskbar_menu.width;
        taskbar_menu.x = taskbar_menu.x + taskbar_menu.width > view->x + view->width ? view->x + view->width - taskbar_menu.width : taskbar_menu.x;
        taskbar_menu.y = taskbar_menu.y + taskbar_menu.height > view->y + view->height ? view->y + view->height - taskbar_menu.height : taskbar_menu.y;
        taskbar_menu.x = taskbar_menu.x < view->x ? view->x : taskbar_menu.x, taskbar_menu.y = taskbar_menu.y < view->y ? view->y : taskbar_menu.y;
        taskbar_menu.open = 1, taskbar_menu.dirty = 1;
        return 1;
}

// The menu drawn into pixels (its width a row), with the faces of view.
static fn taskbar_menu_draw(const struct taskbar_view *view, unsigned int address_to pixels)
{
        taskbar_menu_ui(view, pixels);
        ui_clear(&taskbar_menu.ui);
        ui_kit.now = (b64)ui_kit_clock();
        ui_popmenu_draw(&taskbar_menu.ui, &taskbar_menu.menu);
        taskbar_menu.dirty = taskbar_menu.menu.moving;
}

// An event of the hand for the menu, at x, y in the space: the id of an item chosen, 0 for nothing, -1 for the menu to go.
static int taskbar_menu_hand(unsigned int type, unsigned int flags, int a, int b, int x, int y)
{
        struct window_event event = {type, flags, a, b, 0};
        int answer;

        if (!taskbar_menu.open)
                return -1;
        ui_kit.now = (b64)ui_kit_clock();
        answer = ui_popmenu_event(&taskbar_menu.ui, &taskbar_menu.menu, &event, x - taskbar_menu.x, y - taskbar_menu.y);
        taskbar_menu.dirty = 1;
        return answer == UI_MENU_GONE ? -1 : answer > 0 ? answer : 0;
}

// Whether the menu is up, and where (left, top, width, height in the space).
static int taskbar_menu_rect(int rect[4])
{
        rect[0] = taskbar_menu.x, rect[1] = taskbar_menu.y, rect[2] = taskbar_menu.width, rect[3] = taskbar_menu.height;
        return taskbar_menu.open && taskbar_menu.width > 0 && taskbar_menu.height > 0;
}

// Whether the menu's picture is to be drawn again.
static int taskbar_menu_redraw(void)
{
        return taskbar_menu.dirty;
}

static void taskbar_menu_shut(void)
{
        taskbar_menu.open = 0;
}

// The clock format the menu's clock item turns to (`time format`'s word), and the band's clock turned to it.
static const char *taskbar_clock_other(void)
{
        return taskbar.clock_12h ? "24h" : "12h";
}

static void taskbar_clock_turn(void)
{
        taskbar.clock_12h = !taskbar.clock_12h, taskbar.stale = 1;
}

static int taskbar_editing(void)
{
        return taskbar.editing;
}

/*
        The band's arrangement (taskbar.editing): each item that can be moved is ringed (the moon, every task and each tray item), the
        three places along the band are slots a drop is aimed at, and the Done button is at the end. An item pressed and let go elsewhere
        on the band is moved there (taskbar_edit_drop), at once, by a press of a device plugged into the machine alone (the compositor
        refuses one with WINDOW_EVENT_VIRTUAL, as Settings refuses a deliberate row for one); Done keeps the arrangement through the
        `taskbar` rows' own files and words (taskbar_edit_words), as the verbs and Settings keep them, and the band leaves its arrangement.
*/
static void taskbar_edit_set(int on)
{
        taskbar.editing = on != 0;
        taskbar.stale = 1;
}

// The item of the arrangement at a point of the band: the moon, a task, a tray item or the Done button; TASKBAR_HIT_NONE for none.
static int taskbar_edit_item(const struct taskbar_view *view, int px, int py)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int at, which;

        if (!taskbar.editing)
                return TASKBAR_HIT_NONE;
        at = ui_tree_hit(t, px, py, (int[2]){0, 0});
        if (at < 0)
                return TASKBAR_HIT_NONE;
        which = t->node[at].id - 1;
        return which >= 0 || which == TASKBAR_LAUNCHER || which == TASKBAR_DONE || taskbar_item_of(which) >= 0 ? which : TASKBAR_HIT_NONE;
}

// The middle of a slot along the band, or -1 with no such slot.
static int taskbar_edit_along(const struct taskbar_view *view, int which)
{
        const struct ui_node address_to n = taskbar_node(view, which), address_to band = &taskbar_tree(view)->node[1];

        return n ? (band->dir ? n->y + n->h / 2 : n->x + n->w / 2) : -1;
}

/*
        An item let go at px, py: the moon to the place it is let go in (the tasks no longer split round it unless it is in the middle);
        a task to the place, or onto the moon to split the tasks round it; a tray item before the tasks or after them, by the side of the
        tasks' middle it is let go on. Answers whether the arrangement changed.
*/
static int taskbar_edit_drop(const struct taskbar_view *view, int which, int px, int py)
{
        const struct ui_node address_to band = &taskbar_tree(view)->node[1];
        int along = band->dir ? py : px, zone = TASKBAR_ZONE_CENTRE, best = -1, moon = taskbar_edit_along(view, TASKBAR_LAUNCHER);
        int moon_zone = taskbar.moon_zone, tasks_zone = taskbar.tasks_zone;
        unsigned int before = taskbar.tray_before;

        if (!taskbar.editing)
                return 0;
        for (int each = TASKBAR_ZONE_START; each <= TASKBAR_ZONE_END; each++)
        {
                const struct ui_node address_to n = taskbar_node(view, TASKBAR_ZONE - each);
                int from, to, apart;

                if (!n)
                        continue;
                from = band->dir ? n->y : n->x, to = from + (band->dir ? n->h : n->w);
                apart = along < from ? from - along : along >= to ? along - to + 1 : 0;
                if (best < 0 || apart < best)
                        best = apart, zone = each;
        }
        if (which == TASKBAR_LAUNCHER)
        {
                moon_zone = zone;
                if (tasks_zone == TASKBAR_ZONE_SPLIT && zone != TASKBAR_ZONE_CENTRE)
                        tasks_zone = TASKBAR_ZONE_CENTRE;
        }
        else if (which >= 0)
        {
                const struct ui_node address_to n = taskbar_node(view, TASKBAR_LAUNCHER);
                int on_moon = n && px >= n->x && px < n->x + n->w && py >= n->y && py < n->y + n->h;

                tasks_zone = on_moon ? TASKBAR_ZONE_SPLIT : zone;
                if (on_moon)
                        moon_zone = TASKBAR_ZONE_CENTRE;
        }
        else if (taskbar_item_of(which) >= 0)
        {
                int tasks = taskbar.tasks_zone == TASKBAR_ZONE_SPLIT ? moon : taskbar_edit_along(view, TASKBAR_TABS);

                tasks = tasks < 0 ? moon : tasks;
                if (tasks < 0)
                        return 0;
                before = along < tasks ? before | 1u << taskbar_item_of(which) : before & ~(1u << taskbar_item_of(which));
        }
        else
                return 0;
        if (moon_zone == taskbar.moon_zone && tasks_zone == taskbar.tasks_zone && before == taskbar.tray_before)
                return 0;
        taskbar.moon_zone = moon_zone, taskbar.tasks_zone = tasks_zone, taskbar.tray_before = before, taskbar.stale = 1;
        return 1;
}

/*
        The arrangement as the `taskbar` rows keep it: for each row its file and its word, the moon's place, the tasks' and each tray item's
        side (the rows' choices, as `taskbar moon`, `taskbar tasks` and `taskbar item ITEM` take them). Answers how many.
*/
static int taskbar_edit_words(const char *paths[TASKBAR_EDIT_ROWS], const char *words[TASKBAR_EDIT_ROWS])
{
        static const char address_to const places[3] = {"start", "center", "end"};
        static const char address_to const items[6] = {SETTING_TASKBAR_ITEM_NOTICES_PATH, SETTING_TASKBAR_ITEM_MIC_PATH, SETTING_TASKBAR_ITEM_SOUND_PATH,
                                                         SETTING_TASKBAR_ITEM_BATTERY_PATH, SETTING_TASKBAR_ITEM_TEXT_PATH, SETTING_TASKBAR_ITEM_CLOCK_PATH};
        int count = 0;

        paths[count] = SETTING_TASKBAR_MOON_PATH, words[count++] = places[taskbar.moon_zone >= 0 && taskbar.moon_zone < 3 ? taskbar.moon_zone : 1];
        paths[count] = SETTING_TASKBAR_TASKS_PATH;
        words[count++] = taskbar.tasks_zone == TASKBAR_ZONE_SPLIT ? "split" : places[taskbar.tasks_zone >= 0 && taskbar.tasks_zone < 3 ? taskbar.tasks_zone : 1];
        for (int at = 0; at < 6; at++)
                paths[count] = items[at], words[count++] = taskbar.tray_before >> at & 1 ? "before" : "after";
        return count;
}

// `taskbar fullscreen`: on, the band slides off its edge while a window is fullscreen (taskbar_slide_now).
static void taskbar_hide_set(int on)
{
        taskbar.hide_fullscreen = on != 0;
}
