/*
        The taskbar's layout, hit test, damage and painting (ui/ui.c defines its interface to the compositor).

        A strip along an output edge holding floating glass pills (moon/launcher, window tabs, tray) laid out as a
        ui_tree (taskbar_lay): rectangles, hit testing (with touch reach), damage, and painting all read from that tree.
        Every rectangle is on the output in device pixels; painting works in band coordinates over the blurred glass
        buffer, using the kit theme's tints (compositor_tab_target) and easing highlight/slide/moon tweens on ui_kit.now.
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
        int moon_rest;                          // the glow's strength at rest, percent of the hover's: TASKBAR_MOON_REST_GLOW less one, 0 for none (a check's)
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

static int taskbar_clamp(int v, int lo, int hi)
{
        return v < lo ? lo : v > hi ? hi : v;
}

static long long taskbar_now_sec(long long fixed)
{
        timespec now = {0, 0};

        if (fixed)
                return fixed;
        clock_gettime(CLOCK_REALTIME, address_of now);
        return now.tv_sec;
}

static int taskbar_broken_time(long long fixed, tm *broken)
{
        time_t seconds = (time_t)taskbar_now_sec(fixed);

        return localtime_r(&seconds, broken) != 0;
}

static int taskbar_fits(const struct ui_node address_to outer, const struct ui_node address_to inner)
{
        return inner && inner->x >= outer->x && inner->y >= outer->y &&
               inner->x + inner->w <= outer->x + outer->w && inner->y + inner->h <= outer->y + outer->h;
}

static unsigned int address_to taskbar_remap(unsigned int address_to old, positive old_bytes, positive bytes)
{
        long got;

        if (old)
                compositor_call(syscall(munmap), (positive)old, old_bytes, 0, 0, 0, 0);
        got = ui_map(bytes);
        return got < 0 && got > -4096 ? null : (unsigned int address_to)got;
}

static void taskbar_blit_square(unsigned int address_to dst, long dst_pitch, const unsigned int address_to src, long src_pitch, int size)
{
        for (int row = 0; row < size; row++)
                memory_copy(dst + (long)row * dst_pitch, src + (long)row * src_pitch, (positive)size * 4);
}

// Rest of the first line starting with key (and sep when non-zero), or null.
static const char *taskbar_line_value(const char *text, const char *key, char sep)
{
        positive size = string_length((string_address)key);

        for (string_address line = (string_address)text; line && *line;)
        {
                string_address nl;

                if (!string_compare_max(line, (string_address)key, size) && (!sep || line[size] == sep))
                        return (const char *)(line + size + (sep != 0));
                nl = string_first_of(line, '\n');
                line = nl ? nl + 1 : null;
        }
        return null;
}

static b64 taskbar_tween_goal(const struct ui_tween address_to t)
{
        return t->pending ? t->want : t->to;
}

static unsigned int taskbar_minute_now(void)
{
        return (unsigned int)(taskbar_now_sec(0) / 60);
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

        return taskbar_clamp(side > room ? room : side, 1, UI_ICON_MOST);
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
        The band as a ui_tree: root = output (shifted while sliding), node 1 = band along its edge, containing three zones
        (start, centre, end) holding the moon, tabs, and tray pills. Node ids are slot + 1 (0 = unnamed): 0+ tabs,
        TASKBAR_TRAY (-2), -3 clock, -4 battery, TASKBAR_LAUNCHER (-5), TASKBAR_VOLUME (-6), TASKBAR_MIC (-7),
        TASKBAR_NOTICES (-8), -9 custom tray text, TASKBAR_TABS (-10). Nodes outside the band lose their id and size.
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

// A leaf node of the band: opened, optionally given a centred mark (>0) or centred alignment (<0), and closed.
static fn taskbar_leaf(struct ui_tree address_to t, int which, int m, int gap, int length, int high, int mark)
{
        struct ui_node address_to n = taskbar_open(t, which, m, gap, length, high);

        if (mark > 0)
                taskbar_mark(t, n, mark);
        else if (mark < 0)
                n->justify = UI_CENTER;
        ui_tree_close(t);
}

// The inset of the plates from the band's edges across it, at the band's scale: two logical pixels, one device pixel at least.
static int taskbar_inset(int scale)
{
        int inset = 2 * scale / 100;

        return inset < 1 ? 1 : inset;
}

// The tray item a slot is, its bit in taskbar.tray_before: the dot, the microphone, the speaker, the battery, a format's text, the clock; -1 for none.
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

#ifdef COMPOSITOR_ROWS
#define taskbar_tray_items_count() compositor_tray_count()
#define taskbar_tray_items_gen() compositor_tray_gen()
#define taskbar_tray_items_pixels(i) compositor_tray_pixels(i)
#else
#define taskbar_tray_items_count() 0
#define taskbar_tray_items_gen() 0
#define taskbar_tray_items_pixels(i) ((const unsigned int *)0)
#endif

struct taskbar_metrics
{
        int m, scale, pad, high, side, icon_gap, apart, glyph, square, cell, clocked, text_w;
};

/*
        A tray pill: the items that are on its side of the tasks (before, the pill before them, TASKBAR_TRAY_BEFORE; else the pill
        after, TASKBAR_TRAY), each the kit's padding either side of what it shows: the dot, the microphone and the speaker a mark,
        the battery and a format of the person's a text. Its length, or with t its tree's elements too, margin the room before it.
*/
static int taskbar_tray_lay(struct ui_tree address_to t, int before, const struct taskbar_metrics *M, int margin)
{
        int scale = M->scale, glyph = M->glyph;
        int xtray = !before ? taskbar_tray_items_count() : 0, length = xtray > 0 ? xtray * M->square : 0;
        const struct
        {
                int slot, w, mark;
        } cells[] = {
#define TASKBAR_CELL(slot, shown, mark, paint) {slot, (shown) ? ((mark) ? M->square : (slot) == -3 ? M->clocked : M->cell) : 0, mark},
            TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
                {-9, taskbar.tray_custom ? M->text_w : 0, 0},
        };
        int ncells = (int)(sizeof(cells) / sizeof(cells[0]));

        for (int i = 0; i < ncells; i++)
                if (cells[i].w && taskbar_before(cells[i].slot) == before)
                        length += cells[i].w;
        if (!t || !length)
                return length;
        taskbar_open(t, before ? TASKBAR_TRAY_BEFORE : TASKBAR_TRAY, M->m, margin, length, M->high);
        for (int i = 0; i < xtray && i < TASKBAR_TRAY_ITEM_MAX; i++)
                taskbar_leaf(t, TASKBAR_TRAY_ITEM_BASE - i, M->m, 0, M->square, M->high, M->glyph);
        for (int i = 0; i < ncells; i++)
                if (cells[i].w && taskbar_before(cells[i].slot) == before)
                        taskbar_leaf(t, cells[i].slot, M->m, 0, cells[i].w, M->high, cells[i].mark);
        ui_tree_close(t);
        return length;
}

// The strips of the tasks (taskbar_strip): where each side is scrolled to, the chevrons it has, what it shows of what it lists.
static int taskbar_strip_from[2], taskbar_strip_ends[2][2], taskbar_strip_shown[2], taskbar_strip_listed[2];
static int taskbar_strip_follow = 1, taskbar_strip_lit[2] = {-1, -1}; // it keeps the window with the keys in it, until the person scrolls it
static struct ui_grown taskbar_lay_room;      // taskbar_lay's words of every tab
static bool taskbar_tab_full(const struct taskbar_view *view, int at);

// A pill of tasks, named slot (TASKBAR_TABS, or TASKBAR_TABS_LEFT for the tasks on the moon's left when they are split round it).
static fn taskbar_tasks_lay(const struct taskbar_view *view, struct ui_tree address_to t, int slot, int strip, const int *list, int listed,
                            const int *widths, const int *titled, const struct taskbar_metrics *M, int margin)
{
        int length = 0, bare = M->side + 2 * M->pad;

        for (int k = 0; k < listed; k++)
                length += widths[list[k]];
        if (!length)
                return;
        length += (taskbar_strip_ends[strip][0] + taskbar_strip_ends[strip][1]) * bare;
        if (!taskbar.separate)
                taskbar_open(t, slot, M->m, margin, length, M->high);
        if (taskbar_strip_ends[strip][0])
        {
                taskbar_leaf(t, TASKBAR_STRIP_BACK, M->m, taskbar.separate ? margin : 0, bare, M->high, -1);
                margin = M->apart;
        }
        for (int k = 0; k < listed; k++)
        {
                int at = list[k];
                struct ui_node address_to n = taskbar_open(t, at, M->m, taskbar.separate ? (k ? M->apart : margin) : 0, widths[at], M->high), address_to part;
                bool full = taskbar_tab_full(view, at);

                n->align = UI_CENTER;
                if (titled[at])
                        n->pad[2 * M->m] = full ? 0 : M->pad;
                else
                        n->justify = UI_CENTER;
                part = ui_tree_open(t);
                part->size[0] = part->size[1] = (struct ui_size){UI_FIXED, full ? (M->high < widths[at] ? M->high : widths[at]) : M->side, 0, UI_TREE_MOST, 0};
                ui_tree_close(t);
                if (titled[at])
                {
                        part = ui_tree_open(t);
                        part->margin[2 * M->m] = M->icon_gap, part->margin[2 * M->m + 1] = M->pad;
                        part->size[M->m] = (struct ui_size){UI_GROW, 1, 0, UI_TREE_MOST, 0};
                        part->size[!M->m] = (struct ui_size){UI_FIXED, M->high, 0, UI_TREE_MOST, 0};
                        ui_tree_close(t);
                }
                ui_tree_close(t);
        }
        if (taskbar_strip_ends[strip][1])
                taskbar_leaf(t, TASKBAR_STRIP_ON, M->m, taskbar.separate ? M->apart : 0, bare, M->high, -1);
        if (!taskbar.separate)
                ui_tree_close(t);
}

/*
        The tasks of a side that cannot all be icons in its room: a strip of as many as fit beside a chevron at each end, from the one the
        strip is scrolled to (taskbar_strip_from), moved so the window with the keys is in it. The list is cut to them in place and its
        new length answered; the chevrons each side are said in taskbar_strip_ends. A side whose icons fit is listed whole.
*/
static int taskbar_strip(const struct taskbar_view *view, int strip, int *list, int listed, int room, int bare, int apart)
{
        int each = bare + (taskbar.separate ? apart : 0), shown, from, lit = -1;

        taskbar_strip_ends[strip][0] = taskbar_strip_ends[strip][1] = 0;
        taskbar_strip_listed[strip] = listed;
        if (listed <= 0 || (long)listed * each - (taskbar.separate ? apart : 0) <= room)
                return taskbar_strip_shown[strip] = listed;
        shown = taskbar_clamp((room - 2 * each) / each, 1, listed);
        for (int k = 0; k < listed && view->marks; k++)
                if (view->marks[list[k]] & COMPOSITOR_TAB_LIT)
                        lit = k;
        if (lit >= 0 && (view->rows ? (int)view->rows[list[lit]] : list[lit]) != taskbar_strip_lit[strip])
                taskbar_strip_lit[strip] = view->rows ? (int)view->rows[list[lit]] : list[lit], taskbar_strip_follow = 1;
        from = taskbar_strip_from[strip];
        if (lit >= 0 && taskbar_strip_follow)
                from = lit < from ? lit : lit >= from + shown ? lit - shown + 1 : from;
        from = taskbar_clamp(from, 0, listed - shown);
        taskbar_strip_from[strip] = from;
        taskbar_strip_ends[strip][0] = from > 0, taskbar_strip_ends[strip][1] = from + shown < listed;
        for (int k = 0; k < shown; k++)
                list[k] = list[from + k];
        return taskbar_strip_shown[strip] = shown;
}

// The strip scrolled by `by` tabs (a page is the tabs it shows): it no longer follows the keys until they move to another window.
static b32 taskbar_strip_step(int by)
{
        b32 moved = 0;

        for (int s = 0; s < 2; s++)
        {
                int to = taskbar_clamp(taskbar_strip_from[s] + by, 0, taskbar_strip_listed[s] - taskbar_strip_shown[s]);

                moved |= to != taskbar_strip_from[s];
                taskbar_strip_from[s] = to;
        }
        taskbar_strip_follow = 0;
        return moved;
}

static b32 taskbar_strip_page(int direction)
{
        int page = taskbar_strip_shown[0] > taskbar_strip_shown[1] ? taskbar_strip_shown[0] : taskbar_strip_shown[1];

        return taskbar_strip_step(direction * (page > 1 ? page : 1));
}

// The tasks' widths for room: each what its icon and title ask, or an equal share when they ask for more, and under a share with the room
// for seven characters of a title, every one its icon alone. Answers their length.
static int taskbar_tasks_fit(const struct taskbar_view *view, int strip, int *list, int *listed_at, int room, const struct taskbar_metrics *M,
                             int longest, int *widths, int *titled)
{
        int sum = 0, share, tabs = 0, alone = taskbar_tab_length(M->high, M->pad, M->scale, 7), bare = M->side + 2 * M->pad;
        int listed = taskbar_strip(view, strip, list, *listed_at, room, M->side + 2, M->apart);
        int ends = taskbar_strip_ends[strip][0] + taskbar_strip_ends[strip][1];
        int gaps = taskbar.separate && listed + ends > 1 ? (listed + ends - 1) * M->apart : 0;

        *listed_at = listed;
        room -= gaps + ends * bare, tabs = gaps + ends * bare;
        for (int k = 0; k < listed; k++)
        {
                int at = list[k];

                widths[at] = view->lengths ? taskbar_tab_length(M->high, M->pad, M->scale, view->lengths[at]) : longest;
                widths[at] = widths[at] > longest ? longest : widths[at];
                sum += widths[at];
        }
        share = listed && room > 0 ? room / listed : 0;
        for (int k = 0; k < listed; k++)
        {
                int at = list[k];

                widths[at] = sum > room && widths[at] > share ? share : widths[at];
                titled[at] = (!view->lengths || view->lengths[at] > 0) && (sum <= room || share >= alone);
                if (!titled[at])
                        widths[at] = widths[at] < bare ? widths[at] : bare;
                tabs += widths[at];
        }
        return tabs;
}

static int taskbar_pill_gap(int *first, int apart)
{
        int gap = *first ? 0 : apart;

        *first = 0;
        return gap;
}

static fn taskbar_lay(struct ui_tree address_to t, const struct taskbar_view *view)
{
        static const int just[2][3] = {{UI_START, UI_CENTER, UI_END}, {UI_END, UI_CENTER, UI_START}};
        ui_tree_ready(t);
        int m = taskbar_vertical(view->position), c = !m, far = view->position == TASKBAR_BOTTOM || view->position == TASKBAR_RIGHT;
        int scale = taskbar_scaled(view->scale), length = m ? view->height : view->width, thick = taskbar_thick(view->scale);
        int gap = TASKBAR_GAP * scale / 100, apart = 2 * gap, pad = taskbar.padding * scale / 100;
        int high = thick - 2 * taskbar_inset(scale), count = (int)view->count, done = 0, split = taskbar.tasks_zone == TASKBAR_ZONE_SPLIT;
        int moon = thick <= length - 2 * gap ? thick : 0, icons = (taskbar.notices_item != 0) + (taskbar.mic_item != 0) + (taskbar.volume_item != 0);
        int tab, longest, trays[2], most = length / 2 - moon - apart, inner = length - 2 * gap;
        int *widths, *titled, zones[3], *list[2], listed[2] = {0, 0}, tabs[2] = {0, 0}, moon_zone = split ? TASKBAR_ZONE_CENTRE : taskbar.moon_zone;
        struct taskbar_metrics M;
        struct ui_node address_to n;

        count = count > 0 && ui_grown_room(&taskbar_lay_room, 4 * sizeof(int), UI_TREE_GROWN, count) ? count : 0;
        widths = (int *)taskbar_lay_room.at, titled = widths + count, list[0] = titled + count, list[1] = list[0] + count;
        high = high < 1 ? 1 : high;
        M = (struct taskbar_metrics){
            .m = m, .scale = scale, .pad = pad, .high = high, .side = taskbar_icon_side(high, scale),
            .icon_gap = TASKBAR_ICON_GAP * scale / 100, .apart = apart, .glyph = high * 64 / 100,
            .square = high * 64 / 100 + 2 * pad, .cell = taskbar_text(high, pad, 4),
            .clocked = taskbar_text(high, pad, taskbar.clock_chars > 0 ? taskbar.clock_chars : 5),
            .text_w = taskbar.tray_custom ? taskbar_text(high, pad, taskbar.tray_chars > 0 ? taskbar.tray_chars : 1) : 0,
        };
        tab = taskbar.clock_tab ? M.clocked : 0;
        for (int b = 0; b < 2; b++)
                trays[b] = taskbar_tray_lay(null, b, &M, 0);
        if (trays[0] + trays[1] > most && most > 0)
        {
                int over = trays[0] + trays[1] - most;

                if (taskbar.tray_custom && M.text_w - over >= high)
                        M.text_w -= over;
                else
                {
                        int items = icons + (taskbar.battery_item && !taskbar.tray_custom) + (taskbar.tray_custom != 0) + (taskbar.clock_tray != 0);

                        M.square = M.cell = M.text_w = M.clocked = items ? most / items : 0;
                }
                for (int b = 0; b < 2; b++)
                        trays[b] = taskbar_tray_lay(null, b, &M, 0);
        }
        if (most <= 0 || trays[0] + trays[1] > most)
                trays[0] = trays[1] = 0;
        if (taskbar.editing)
                done = taskbar_text(high, pad, 4);
        if (done && done + apart + moon > inner / 2)
                done = 0;
        longest = 8 * thick + M.side + M.icon_gap;

        if (split)
        {
                int half = (inner - moon) / 2, rooms[2], per = 1 << 30;

                for (int each = 0; each < count; each++)
                {
                        int right = (taskbar.tasks_right ? each + 1 : each) % 2 == 0;

                        list[right][listed[right]++] = each;
                }
                for (int k = 0; k < listed[0] / 2; k++)
                {
                        int keep = list[0][k];

                        list[0][k] = list[0][listed[0] - 1 - k], list[0][listed[0] - 1 - k] = keep;
                }
                if (half - apart - trays[1] - apart < 0)
                        trays[1] = 0;
                if (half - apart - trays[0] - apart - (done ? done + apart : 0) < 0)
                        trays[0] = 0;
                rooms[0] = half - apart - (trays[1] ? trays[1] + apart : 0);
                rooms[1] = half - apart - (trays[0] ? trays[0] + apart : 0) - (done ? done + apart : 0) - (tab ? tab + apart : 0);
                for (int s = 0; s < 2; s++)
                        if (listed[s] && rooms[s] / listed[s] < per)
                                per = rooms[s] / listed[s];
                per = per > 0 ? per : 0;
                for (int s = 0; s < 2; s++)
                        tabs[s] = taskbar_tasks_fit(view, s, list[s], &listed[s], per * listed[s], &M, longest, widths, titled);
                zones[TASKBAR_ZONE_CENTRE] = moon, zones[TASKBAR_ZONE_START] = half, zones[TASKBAR_ZONE_END] = inner - moon - half;
        }
        else
        {
                int ends = done ? done + apart : 0, group, pills = (trays[0] > 0) + (trays[1] > 0) + (tab > 0), rest;

                for (int each = 0; each < count; each++)
                        list[1][listed[1]++] = taskbar.tasks_right ? count - 1 - each : each;
                if (moon_zone != taskbar.tasks_zone && taskbar.tasks_zone == TASKBAR_ZONE_CENTRE)
                        group = inner - 2 * (moon + apart > ends ? moon + apart : ends);
                else if (moon_zone != taskbar.tasks_zone && moon_zone == TASKBAR_ZONE_CENTRE)
                        group = length / 2 - gap - moon / 2 - apart - (taskbar.tasks_zone == TASKBAR_ZONE_END ? ends : 0);
                else
                        group = inner - moon - apart - ends;
                taskbar_strip_ends[0][0] = taskbar_strip_ends[0][1] = 0;
                tabs[1] = taskbar_tasks_fit(view, 1, list[1], &listed[1], group - trays[0] - trays[1] - tab - pills * apart, &M, longest, widths, titled);
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

        ui_tree_begin(t);
        n = ui_tree_open(t);
        n->dir = c, n->justify = far ? UI_END : UI_START, n->size[0].kind = n->size[1].kind = UI_GROW;
        n->x = view->x, n->y = view->y, *ui_tree_at(n, c) += taskbar_off(view, taskbar.shift);
        n = ui_tree_open(t);
        n->dir = m, n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0};
        n->pad[2 * m] = n->pad[2 * m + 1] = gap;
        for (int zone = TASKBAR_ZONE_START; zone <= TASKBAR_ZONE_END; zone++)
        {
                int first = 1;

                n = ui_tree_open(t);
                n->id = taskbar.editing ? TASKBAR_ZONE - zone + 1 : 0;
                n->dir = m, n->align = UI_CENTER, n->justify = just[split][zone];
                n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0};
                n->size[m] = (struct ui_size){UI_FIXED, zones[zone], 0, UI_TREE_MOST, 0};
                if (split)
                {
                        if (zone == TASKBAR_ZONE_START)
                        {
                                n->pad[2 * m + 1] = apart;
                                if (trays[1])
                                        taskbar_tray_lay(t, 1, &M, taskbar_pill_gap(&first, apart));
                                taskbar_tasks_lay(view, t, TASKBAR_TABS_LEFT, 0, list[0], listed[0], widths, titled, &M, taskbar_pill_gap(&first, apart));
                        }
                        else if (zone == TASKBAR_ZONE_CENTRE && moon)
                                taskbar_leaf(t, TASKBAR_LAUNCHER, m, 0, moon, moon, moon);
                        else
                        {
                                n->pad[2 * m] = apart;
                                taskbar_tasks_lay(view, t, TASKBAR_TABS, 1, list[1], listed[1], widths, titled, &M, 0);
                                first = !tabs[1];
                                if (trays[0])
                                        taskbar_tray_lay(t, 0, &M, taskbar_pill_gap(&first, apart));
                                if (tab)
                                        taskbar_leaf(t, -3, m, taskbar_pill_gap(&first, apart), tab, high, 0);
                                if (done)
                                        taskbar_leaf(t, TASKBAR_DONE, m, apart, done, high, 0);
                        }
                }
                else
                {
                        for (int k = 0; k < 3; k++)
                        {
                                int pill = taskbar.order[k];

                                if (pill == TASKBAR_PILL_MOON && moon_zone == zone && moon)
                                        taskbar_leaf(t, TASKBAR_LAUNCHER, m, taskbar_pill_gap(&first, apart), moon, moon, moon);
                                if (pill == TASKBAR_PILL_TASKS && taskbar.tasks_zone == zone)
                                {
                                        if (trays[1])
                                                taskbar_tray_lay(t, 1, &M, taskbar_pill_gap(&first, apart));
                                        if (tabs[1] > 0)
                                                taskbar_tasks_lay(view, t, TASKBAR_TABS, 1, list[1], listed[1], widths, titled, &M, taskbar_pill_gap(&first, apart));
                                }
                                if (pill == TASKBAR_PILL_TRAY && taskbar.tasks_zone == zone && trays[0])
                                        taskbar_tray_lay(t, 0, &M, taskbar_pill_gap(&first, apart));
                                if (pill == TASKBAR_PILL_TRAY && taskbar.tasks_zone == zone && tab)
                                        taskbar_leaf(t, -3, m, taskbar_pill_gap(&first, apart), tab, high, 0);
                        }
                        if (zone == TASKBAR_ZONE_END && done)
                                taskbar_leaf(t, TASKBAR_DONE, m, taskbar_pill_gap(&first, apart), done, high, 0);
                }
                ui_tree_close(t);
        }

        ui_tree_close(t);
        ui_tree_close(t);
        ui_tree_run(t, view->width, view->height);
        if (!m && language_rtl())
                ui_tree_mirror(t);
        for (int at = 2; at < t->count; at++)
        {
                n = &t->node[at];
                if (n->w > 0 && n->h > 0 && taskbar_fits(&t->node[1], n))
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
        unsigned int lengths;           // each tab's title in characters, their digest (FNV-1a over them)
        int strip[2], follow;           // where each side's strip is scrolled to, and whether it follows the keys
        unsigned int marks;             // the tabs' marks, their digest (the lit one is what a strip follows)
        int tray_gen;                   // tray items generation (invalidates layout when tray icons dock/undock/change)
};

_Static_assert(sizeof(struct taskbar_key) == 8 + 22 * 4, "a key is compared as bytes, so it has no padding");

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

static inline struct taskbar_band address_to taskbar_band_cur(void)
{
        return &taskbar_bands[(unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? compositor.band_at : 0];
}

static struct ui_tree address_to taskbar_tree(const struct taskbar_view *view)
{
        struct taskbar_band address_to band = taskbar_band_cur();
        struct taskbar_key key = {taskbar.shift, view->width, view->height, view->scale, view->x, view->y, (int)view->position, (int)view->count,
                                  view->lengths != null, taskbar.scale, taskbar.padding, taskbar.launcher != 0,
                                  taskbar.tray_custom ? taskbar.tray_chars : -1, 0, language_rtl(),
                                  taskbar.moon_zone | taskbar.tasks_zone << 2 | taskbar.tasks_right << 4 | (int)taskbar.tray_before << 5 |
                                      taskbar.order[0] << 12 | taskbar.order[1] << 14 | taskbar.order[2] << 16,
                                  taskbar.editing | taskbar.clock_tray << 1 | taskbar.moon_ink << 2 | taskbar.clock_tab << 3 | taskbar.clock_none << 4 |
                                      taskbar.separate << 5 | (taskbar.edge_in + 64) << 6 | taskbar.clock_chars << 14, 0,
                                  {taskbar_strip_from[0], taskbar_strip_from[1]}, taskbar_strip_follow, 0, taskbar_tray_items_gen()};

#define TASKBAR_CELL(slot, shown, mark, paint) key.cells = key.cells << 1 | ((shown) != 0);
        TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
        key.lengths = key.marks = 2166136261u;
        for (unsigned int at = 0; at < view->count; at++)
        {
                key.lengths = (key.lengths ^ (view->lengths ? view->lengths[at] : 0)) * 16777619u;
                key.marks = (key.marks ^ (view->marks ? view->marks[at] : 0)) * 16777619u;
        }
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

static int taskbar_hit_raw(const struct taskbar_view *view, int px, int py, const int grown[2], int *which)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int at = ui_tree_hit(t, px, py, grown);

        if (at < 0)
                return 0;
        *which = t->node[at].id - 1;
        return 1;
}

// Slot at (px, py) with touch reach across the band (clock/battery/custom text map to TASKBAR_TRAY).
static int taskbar_hit_reach(const struct taskbar_view *view, int px, int py, int reach)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int grown[2], which;

        grown[t->node[1].dir] = 0;
        grown[!t->node[1].dir] = reach + taskbar_inset(taskbar_scaled(view->scale));
        if (!taskbar_hit_raw(view, px, py, grown, &which))
                return TASKBAR_HIT_NONE;
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
        return which == TASKBAR_LAUNCHER ? 1ul << (TASKBAR_TAB_BITS + 1) : which < 0 ? 1ul << TASKBAR_TAB_BITS : 1ul << (which % TASKBAR_TAB_BITS);
}
_Static_assert(TASKBAR_TAB_BITS + 2 <= 64, "a redo bit a tab, the tray's and the launcher's, in a word");

// Whether a slot owns its own damage stripe (battery/clock cells share the tray's, unless clock is a tab; tabs' plate wraps tabs).
static int taskbar_slot_own(int which)
{
        return which != -4 && (which != -3 || taskbar.clock_tab) && which != TASKBAR_TABS;
}

static int taskbar_is_moon_damage(const struct taskbar_band address_to own, const int rect[4])
{
        return own->moon_own && !memory_compare(rect, own->moon_damage, sizeof(own->moon_damage));
}

/*
        The slots the damage meets, with the blur's reach round each: a change
        in the scene under a tab, or under its reach, is one to its glass. The
        reach runs across the whole band and along the slot.
*/
static unsigned long taskbar_meets(const struct taskbar_view *view, int (*damage)[4], unsigned int damages, int solid)
{
        const struct taskbar_band address_to own = taskbar_band_cur();
        unsigned long redo = 0;

        if (solid)
        {
                for (unsigned int at = 0; own->moon_own && at < damages; at++)
                        if (taskbar_is_moon_damage(own, damage[at]))
                                return taskbar_bit(TASKBAR_LAUNCHER);
                return 0;
        }
        for (int which = TASKBAR_TRAY_BEFORE; which < (int)view->count; which++)
        {
                int reach[4];

                if (!taskbar_slot_own(which) || !taskbar_anchor(view, which, &reach[0], &reach[1], &reach[2], &reach[3]))
                        continue;
                reach[2] += reach[0] + COMPOSITOR_GLASS_REACH, reach[3] += reach[1] + COMPOSITOR_GLASS_REACH;
                reach[0] -= COMPOSITOR_GLASS_REACH, reach[1] -= COMPOSITOR_GLASS_REACH;
                for (unsigned int at = 0; at < damages; at++)
                {
                        int meet[4];

                        if (which != TASKBAR_LAUNCHER && taskbar_is_moon_damage(own, damage[at]))
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
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1];

        if (band->dir)
                ui_cell_turned(taskbar_cell(view, band->w), n->x - band->x, n->y - band->y, n->w, n->h, text, taskbar_turn(view->position), ink, ground);
        else
                ui_cell_line(taskbar_cell(view, band->w), n->x - band->x, n->y - band->y, n->w, n->h, text, ink, ground);
}

// The kit's drawing state over the band's own pixels, recording into the frame's scene when there is one.
static void taskbar_band_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t)
{
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1];

        memory_zero(ui, sizeof(*ui));
        ui->pixels = view->pixels, ui->pitch = (unsigned long)band->w, ui->width = band->w, ui->height = band->h, ui->top = 0, ui->bottom = band->h;
        ui->scale = taskbar_scaled(view->scale), ui->scene = view->scene, ui->engine = view->engine, ui->regular = view->face;
}

// Answers the mark in the middle of the element n, and where it is in the band's pixels.
static const struct ui_node address_to taskbar_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t,
                                                  const struct ui_node address_to n, int *x, int *y)
{
        taskbar_band_ui(ui, view, t);
        const struct ui_node address_to band = &t->node[1], address_to mark = &t->node[n->first];

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
        taskbar_band_ui(ui, view, t);
        ui->glass = 1;
        ui_inks(ui, ground);
}

// A slot's button face: its rectangle in the band's pixels (device 256ths), a pill, along from at for length when those are given (Q30 device pixels from the band's start).
static struct ui_box taskbar_face(const struct ui_tree address_to t, const struct ui_node address_to n, b64 at, b64 length)
{
        ui_tree_ready(t);
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
        ui_tree_ready(t);
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
                else if (at != taskbar_tween_goal(&lit->at) || length != taskbar_tween_goal(&lit->length) || !lit->at.placed)
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
        b64 share = ui_tween_at(&taskbar_band_cur()->grow, &live);

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
        into the air round it. A touch of it is always there (TASKBAR_MOON_REST_GLOW percent), and the rest comes as the moon is grown (the
        same tween, the kit's ease over TASKBAR_MOON_GROW_MS, at once with reduced motion), strongest at the limb and falling off as the square of the way across TASKBAR_MOON_HALO percent of the moon, and
        dithered, so its fall has no rings. It reaches past the band, so it is a frame of its own under the band's plates (the compositor's,
        compositor_moon_glow_frame), premultiplied, in a square that holds the disc at its largest and the reach round it; that square is
        the moon's own damage while the glow is shown or going, and nothing else is.
*/
#define TASKBAR_MOON_HALO 45 // the glow's reach past the disc, percent of the moon's size
static unsigned int taskbar_accent(void);
#define TASKBAR_MOON_HALO_ALPHA 0.5 // its strength at the limb, the moon grown whole
#define TASKBAR_MOON_REST_GLOW 30 // percent of that strength the glow keeps with the moon at rest: always a touch of it, the rest on hover

// How strong the glow is now, Q30 of TASKBAR_MOON_HALO_ALPHA: TASKBAR_MOON_REST_GLOW percent at rest, all of it with the moon grown whole.
static b64 taskbar_moon_glow_level(void)
{
        int rest = taskbar.moon_rest ? taskbar.moon_rest - 1 : TASKBAR_MOON_REST_GLOW;

        return (b64)UI_ONE * rest / 100 + taskbar_moon_share() * (100 - rest) / 100;
}

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
        struct taskbar_band address_to band = taskbar_band_cur();
        b64 share = taskbar_moon_share(), level = taskbar_moon_glow_level();
        int side = square[2], x, y, w, h, size, key[5];
        unsigned int accent = taskbar_accent();
        decimal strength, grown, r, reach, c = side / 2.0;

        if (level <= 0 || !taskbar_slot(view, TASKBAR_LAUNCHER, &x, &y, &w, &h))
                return 0;
        size = w < h ? w : h;
        key[0] = side, key[1] = (int)(share >> 10), key[2] = (int)accent, key[3] = window_light(compositor_tab_target(0)), key[4] = (int)(positive)pixels;
        if (!memory_compare(band->glow_key, key, sizeof(key)))
                return 1;
        memory_copy(band->glow_key, key, sizeof(key));
        strength = TASKBAR_MOON_HALO_ALPHA * (decimal)level / (decimal)UI_ONE;
        grown = size * (1 + (decimal)share / (decimal)UI_ONE * TASKBAR_MOON_GROW / 100.0), r = grown / 2, reach = size * TASKBAR_MOON_HALO / 100.0;
        for (int py = 0; py < side; py++)
                for (int px = 0; px < side; px++)
                {
                        decimal dx = px + 0.5 - c, dy = py + 0.5 - c, d = square_root(dx * dx + dy * dy), t = (d - r) / reach, a, value[4];
                        unsigned int hash = ui_moon_hash((unsigned int)px * 73856093u ^ (unsigned int)py * 19349663u), hue;
                        decimal dither = ((hash & 255) + (hash >> 8 & 255)) / 512.0 - 0.5;

                        a = d < r - 1 || t >= 1 ? 0 : strength * (t < 0 ? 1 : (1 - t) * (1 - t)) * (d < r ? d - (r - 1) : 1);
                        if (a <= 0)
                        {
                                pixels[(long)py * side + px] = 0;
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
                        pixels[(long)py * side + px] = (unsigned int)(value[3] + 0.5) << 24 | (unsigned int)(value[0] + 0.5) << 16 |
                                                       (unsigned int)(value[1] + 0.5) << 8 | (unsigned int)(value[2] + 0.5);
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

// Whether the compositor's motion is on and a hand was about within TASKBAR_MOON_IDLE: what the wallpaper's moon turns by too.
static int taskbar_motion_about(positive now)
{
        return taskbar.glides && !taskbar.moon_at && now - taskbar.moon_seen < TASKBAR_MOON_IDLE;
}

// Whether the moon is turning at now: the motion on, a spin, a hand seen within TASKBAR_MOON_IDLE, and no check's time.
static int taskbar_moon_turning(positive now)
{
        return taskbar.spin > 0 && taskbar_motion_about(now);
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
                if (want != taskbar_tween_goal(grow))
                {
                        b32 live = 0;

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

// Decimal digits of a value from 0 to 9999, with no sign.
static void taskbar_digits(char *into, int value)
{
        into[positive_into_string((string_address)into, (positive)taskbar_clamp(value, 0, 9999))] = 0;
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

static void taskbar_path_num(char *into, int room, const char *a, const char *b, positive num, const char *c)
{
        int at = 0;
        char digits[24];

        digits[positive_into_string((string_address)digits, num)] = 0;
        taskbar_tray_put(into, &at, room, a);
        taskbar_tray_put(into, &at, room, b);
        taskbar_tray_put(into, &at, room, digits);
        taskbar_tray_put(into, &at, room, c);
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
        const char *value = taskbar_line_value(text, key, 0);

        return value ? taskbar_tray_number(value) : 0;
}

static void taskbar_tray_one(char *into, int *at, int room, int which, const tm *broken)
{
        static const unsigned short days[7] = {LANGUAGE_tray_day_sun, LANGUAGE_tray_day_mon, LANGUAGE_tray_day_tue, LANGUAGE_tray_day_wed,
                                               LANGUAGE_tray_day_thu, LANGUAGE_tray_day_fri, LANGUAGE_tray_day_sat};
        static const unsigned short months[12] = {LANGUAGE_tray_month_jan, LANGUAGE_tray_month_feb, LANGUAGE_tray_month_mar, LANGUAGE_tray_month_apr,
                                                  LANGUAGE_tray_month_may, LANGUAGE_tray_month_jun, LANGUAGE_tray_month_jul, LANGUAGE_tray_month_aug,
                                                  LANGUAGE_tray_month_sep, LANGUAGE_tray_month_oct, LANGUAGE_tray_month_nov, LANGUAGE_tray_month_dec};
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
                        taskbar_tray_put(into, at, room, taskbar.battery_charging ? WORDS(tray_charging) : WORDS(tray_discharging));
                return;
        case 3:
        case 4:
        case 8:
        case 9:
                taskbar_tray_two(into, at, room, which == 3 ? broken->tm_hour : which == 4 ? broken->tm_min : which == 8 ? broken->tm_mon + 1 : broken->tm_mday);
                return;
        case 5:
                taskbar_digits(word, broken->tm_hour % 12 ? broken->tm_hour % 12 : 12);
                break;
        case 6:
                taskbar_tray_put(into, at, room, broken->tm_hour < 12 ? WORDS(tray_am) : WORDS(tray_pm));
                return;
        case 7:
                taskbar_tray_two(into, at, room, (broken->tm_year + 1900) / 100);
                taskbar_tray_two(into, at, room, (broken->tm_year + 1900) % 100);
                return;
        case 10:
                taskbar_tray_put(into, at, room, language_text(days[(unsigned)broken->tm_wday % 7]));
                return;
        case 11:
                taskbar_tray_put(into, at, room, language_text(months[(unsigned)broken->tm_mon % 12]));
                return;
        case 12: // cpu.load: the first word of /proc/loadavg
                taskbar_tray_file("/proc/loadavg", word, (int)sizeof(word));
                for (int each = 0; word[each]; each++)
                        if (word[each] == ' ')
                                word[each] = 0;
                break;
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

                for (int zone = 0; zone < 32; zone++)
                {
                        char path[64], reading[24];
                        long milli;

                        taskbar_path_num(path, (int)sizeof(path), "/sys/class/thermal/thermal_zone", "", (positive)zone, "/temp");
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
        language_take(compositor.table ? compositor.table->language : 0);
        taskbar.tray_text[0] = 0, taskbar.clock_text[0] = 0;
        taskbar.tray_chars = taskbar.tray_custom ? taskbar_format_render(taskbar.tray_format, taskbar.tray_text, (int)sizeof(taskbar.tray_text)) : 0;
        taskbar.clock_chars = taskbar.clock_format[0] ? taskbar_format_render(taskbar.clock_format, taskbar.clock_text, (int)sizeof(taskbar.clock_text)) : 0;
}

// A format's text this minute into into (room bytes), its names filled in (setting_tray_name); answers its characters.
static int taskbar_format_render(const char *format, char *into, int room)
{
        int at = 0, chars = 0;
        tm broken;

        into[0] = 0;
        if (!taskbar_broken_time(0, &broken))
                return 0;
        for (int each = 0; format[each];)
        {
                if (format[each] == '{' && format[each + 1] != '{')
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
                        each += ((format[each] == '{' || format[each] == '}') && format[each + 1] == format[each]) ? 2 : 1;
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

// `taskbar clock` outside the moon, and `time format` and `time date`: the format its text is, the standard tray's clock otherwise.
static void taskbar_clock_set(const char *format)
{
        string_copy_bounded((p8 *)taskbar.clock_format, (string_address)format, sizeof(taskbar.clock_format));
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
        if (taskbar.tray_custom)
                string_copy_bounded((p8 *)taskbar.tray_format, (string_address)format, sizeof(taskbar.tray_format));
        else
                taskbar.tray_format[0] = 0;
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

static unsigned int taskbar_accent(void)
{
        unsigned int accent = compositor.table && compositor.table->themed ? compositor.table->accent & 0xffffffu : 0;
        int r = (int)(accent >> 16 & 255), g = (int)(accent >> 8 & 255), b = (int)(accent & 255), most = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int least = r < g ? (r < b ? r : b) : (g < b ? g : b);

        return most - least < 40 ? 0x5b7fa6u : accent;
}

/*
        The moon's disc at rest with its rim, a frame for each step of its turn (`taskbar moon spin` seconds, TASKBAR_MOON_STEPS a
        second), made on a thread of its own (taskbar_moon_bake_run, at ordinary scheduling, nice 10) and never on the loop's: the loop
        copies the step's frame and draws the clock over it. Two looks are kept (the rim with the launcher up is white), each up to
        TASKBAR_MOON_KEPT bytes. While the thread draws, the loop calls no ui_moon: the sphere's tables are one for all.
*/
#define TASKBAR_MOON_KEPT (32ul << 20)
#define TASKBAR_MOON_BAKES 2

static struct taskbar_moon_bake
{
        unsigned int address_to frames; // period frames of size by size: the disc and its rim
        unsigned char address_to have;  // a byte a frame, set (release) once the frame is drawn
        positive bytes;
        int size, period, from;         // from: the step the thread starts at
        unsigned int rim, accent;
        unsigned long used;
} taskbar_bakes[TASKBAR_MOON_BAKES];
static int taskbar_bake_running, taskbar_bake_at; // the thread draws into taskbar_bakes[taskbar_bake_at] (acquire/release)
static int taskbar_bake_stop;                     // the loop asks the thread to end after the frame it draws (taskbar_moon_bake_halt)
static thread address_to taskbar_bake_worker;
static unsigned long taskbar_bake_uses;

static void taskbar_moon_render(struct ui address_to ui, int x, int y, int size, int left, int top, int grown, b64 turn, decimal grow,
                                unsigned int rim, unsigned int accent)
{
        struct ui_moon_look look = {0xff0c0d10u, 0xfff7f5efu, 0, rim, UI_MOON_RIM | UI_MOON_FULL_SQUARE, grow};

        ui_moon(ui, x, y, size, UI_ONE / 2, turn, &look);
        ui_moon_rim(ui, left, top, grown, accent, 1.2);
}

static void taskbar_moon_bake_frame(struct taskbar_moon_bake address_to b, int step)
{
        struct ui ui;

        memory_zero(&ui, sizeof(ui));
        ui.pixels = b->frames + (positive)b->size * (positive)b->size * (positive)step;
        ui.pitch = (unsigned long)b->size, ui.width = b->size, ui.height = b->size, ui.top = 0, ui.bottom = b->size;
        taskbar_moon_render(&ui, 0, 0, b->size, 0, 0, b->size, (b64)step * UI_ONE / (b64)b->period, 0, b->rim, b->accent);
        __atomic_store_n(&b->have[step], 1, __ATOMIC_RELEASE);
}

static fn taskbar_moon_bake_run(address_any context)
{
        struct taskbar_moon_bake address_to b = &taskbar_bakes[(long)context];

        compositor_ordinary(10);
        for (int k = 0; k < b->period; k++)
        {
                int step = (b->from + k) % b->period;

                if (__atomic_load_n(&taskbar_bake_stop, __ATOMIC_ACQUIRE))
                        break;
                if (!__atomic_load_n(&b->have[step], __ATOMIC_ACQUIRE))
                        taskbar_moon_bake_frame(b, step);
        }
        __atomic_store_n(&taskbar_bake_running, 0, __ATOMIC_RELEASE);
}

// The bake's thread ended after the frame it draws, before the loop draws a moon itself.
static void taskbar_moon_bake_halt(void)
{
        if (!taskbar_bake_worker)
                return;
        __atomic_store_n(&taskbar_bake_stop, 1, __ATOMIC_RELEASE);
        while (__atomic_load_n(&taskbar_bake_running, __ATOMIC_ACQUIRE))
        {
                positive nap[2] = {0, 50000};

                compositor_call(syscall(nanosleep), (positive)nap, 0, 0, 0, 0, 0);
        }
        thread_join(taskbar_bake_worker);
        taskbar_bake_worker = null;
        __atomic_store_n(&taskbar_bake_stop, 0, __ATOMIC_RELEASE);
}

static int taskbar_moon_baking(void)
{
        return __atomic_load_n(&taskbar_bake_running, __ATOMIC_ACQUIRE);
}

static int taskbar_bake_any(const struct taskbar_moon_bake address_to b, int want)
{
        for (int k = 0; k < b->period; k++)
                if ((__atomic_load_n(&b->have[k], __ATOMIC_ACQUIRE) != 0) == want)
                        return 1;
        return 0;
}

static const unsigned int address_to taskbar_moon_baked(int size, unsigned int rim, unsigned int accent)
{
        int period = taskbar.spin * TASKBAR_MOON_STEPS;
        positive frame = (positive)size * (positive)size, bytes = frame * 4 * (positive)period + (positive)period;
        int step = (int)(taskbar.moon_step % (period > 0 ? period : 1)), at = -1, running = taskbar_moon_baking();
        struct taskbar_moon_bake address_to b;

        if (period <= 0 || size <= 0 || bytes > TASKBAR_MOON_KEPT || taskbar.moon_at)
                return null;
        for (int k = 0; k < TASKBAR_MOON_BAKES && at < 0; k++)
                if (taskbar_bakes[k].frames && taskbar_bakes[k].size == size && taskbar_bakes[k].period == period &&
                    taskbar_bakes[k].rim == rim && taskbar_bakes[k].accent == accent)
                        at = k;
        if (at < 0)
        {
                for (int k = 0; k < TASKBAR_MOON_BAKES; k++)
                        if (!(running && k == taskbar_bake_at) && (at < 0 || taskbar_bakes[k].used < taskbar_bakes[at].used))
                                at = k;
                if (at < 0)
                        return null;
                b = &taskbar_bakes[at];
                b->frames = taskbar_remap(b->frames, b->bytes, bytes);
                if (!b->frames)
                        return null;
                b->have = (unsigned char address_to)((unsigned char address_to)b->frames + frame * 4 * (positive)period);
                b->bytes = bytes, b->size = size, b->period = period, b->rim = rim, b->accent = accent;
        }
        b = &taskbar_bakes[at];
        b->used = ++taskbar_bake_uses;
        if (!__atomic_load_n(&b->have[step], __ATOMIC_ACQUIRE) && !taskbar_bake_any(b, 1))
        {
                if (running)
                        taskbar_moon_bake_halt(), running = 0;
                taskbar_moon_bake_frame(b, step);
        }
        if (!running && taskbar_bake_any(b, 0))
        {
                if (taskbar_bake_worker)
                        thread_join(taskbar_bake_worker);
                b->from = (step + 1) % period, taskbar_bake_at = at;
                __atomic_store_n(&taskbar_bake_running, 1, __ATOMIC_RELEASE);
                taskbar_bake_worker = thread_start(taskbar_moon_bake_run, (address_any)(long)at);
                if (!taskbar_bake_worker)
                        __atomic_store_n(&taskbar_bake_running, 0, __ATOMIC_RELEASE);
        }
        for (int k = 0; k < period; k++)
        {
                int back = ((step - k) % period + period) % period;

                if (__atomic_load_n(&b->have[back], __ATOMIC_ACQUIRE))
                        return b->frames + frame * (positive)back;
        }
        taskbar_moon_bake_halt();
        taskbar_moon_bake_frame(b, step);
        return b->frames + frame * (positive)step;
}

// The arrangement ring of a slot or the moon: a thin ring of ink a little in from the slot's edges.
static void taskbar_edit_ring(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n,
                              unsigned int ground, unsigned int ink, unsigned int alpha)
{
        int s = taskbar_scaled(view->scale);
        struct ui grip;
        struct ui_box ring = taskbar_face(t, n, 0, 0);
        b32 in = (b32)(s / 100 > 1 ? s / 100 : 1) * 256 * 2;

        taskbar_glass_ui(&grip, view, t, ground);
        ring.x0 += in, ring.y0 += in, ring.x1 -= in, ring.y1 -= in;
        ring.rx = ring.ry = ((ring.x1 - ring.x0 < ring.y1 - ring.y0 ? ring.x1 - ring.x0 : ring.y1 - ring.y0) / 2);
        ui_kit_ring(&grip, &ring, (b32)(s * 256 / 100 > 256 ? s * 256 / 100 : 256), (ink & 0xffffffu) | alpha);
}

static void taskbar_moon_clock(const struct taskbar_view *view, struct ui address_to ui, int x, int y, int size, int hours, int minutes)
{
        int shown = taskbar.clock_12h ? (hours % 12 ? hours % 12 : 12) : hours;
        char upper[3] = {(char)('0' + shown / 10), (char)('0' + shown % 10), 0};
        char lower[3] = {(char)('0' + minutes / 10), (char)('0' + minutes % 10), 0};

        if (taskbar.clock_12h && shown < 10)
                upper[0] = upper[1], upper[1] = 0;
        taskbar.clock_read = (int)ui_moon_label(ui, view->engine, view->bold ? view->bold : view->face, x, y, size, upper, lower,
                                                taskbar.clock_bulge / 100.0, taskbar_accent());
}

static void taskbar_moon(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, int hours, int minutes, int lit)
{
        struct ui ui;
        int x, y;
        const struct ui_node address_to mark = taskbar_ui(&ui, view, t, n, &x, &y);
        int size = mark->w < mark->h ? mark->w : mark->h;
        long long seconds = taskbar_now_sec(taskbar.moon_at);
        b64 turn = taskbar.moon_at ? (taskbar.glides ? (b64)(seconds / 60 % 60) * UI_ONE / 60 : 0) : taskbar.moon_turn;
        decimal grow = (decimal)taskbar_moon_share() / (decimal)UI_ONE * TASKBAR_MOON_GROW / 100;
        int grown = size + (int)(size * grow + 0.5), left = x + (size - grown) / 2, top = y + (size - grown) / 2;
        int light = window_light(compositor_tab_target(0));
        unsigned int rim = lit ? 0xffffffffu : light ? 0xa0868686u : 0xb4e8e8e8u, accent = taskbar_accent();
        const unsigned int address_to cached_frame = null;

        if (taskbar.moon_ink)
        {
                unsigned int paper, ink;

                ui_moon_paper_tones(light ? 0 : UI_MOON_PAPER_DARK, &paper, &ink);
                for (int row = 0; row < grown; row++)
                        for (int column = 0; column < grown; column++)
                                if (top + row >= 0 && top + row < ui.height && left + column >= 0 && left + column < ui.width)
                                        ui.pixels[(long)(top + row) * (long)ui.pitch + left + column] = ink;
                ui_moon_paper(&ui, left, top, grown, turn, light ? 0 : UI_MOON_PAPER_DARK, 0);
                ui_moon_rim(&ui, left, top, grown, accent, 1.2);
        }
        else
        {
                const unsigned int address_to frame = grow > 0 || x < 0 || y < 0 || x + size > ui.width || y + size > ui.height
                                                          ? null
                                                          : taskbar_moon_baked(size, rim, accent);

                if (frame)
                {
                        taskbar_blit_square(ui.pixels + (long)y * (long)ui.pitch + x, (long)ui.pitch, frame, size, size);
                        left = x, top = y, grown = size, cached_frame = frame;
                }
                else
                {
                        taskbar_moon_bake_halt();
                        taskbar_moon_render(&ui, x, y, size, left, top, grown, turn, grow, rim, accent);
                }
        }
        x = left, y = top, size = grown;
        if (taskbar.clock_tray || taskbar.clock_tab || taskbar.clock_none || hours < 0 || !view->engine || !view->face)
                return;
        {
                struct taskbar_moon_clock_key
                {
                        const unsigned int address_to frame;
                        const void address_to engine, address_to face;
                        int size, hours, minutes, flags, bulge;
                        unsigned int accent;
                } key = {cached_frame, view->engine, view->bold ? (const void address_to)view->bold : (const void address_to)view->face,
                         size, hours, minutes, taskbar.clock_12h, taskbar.clock_bulge, accent};
                static struct
                {
                        struct taskbar_moon_clock_key key;
                        int read;
                        unsigned int address_to pixels;
                        positive bytes;
                } shown;
                b32 whole = cached_frame && x >= 0 && y >= 0 && x + size <= ui.width && y + size <= ui.height;
                unsigned int address_to dst = ui.pixels + (long)y * (long)ui.pitch + x;
                positive need = (positive)size * (positive)size * 4;

                if (whole && shown.pixels && !memory_compare(&shown.key, &key, sizeof(key)))
                {
                        taskbar_blit_square(dst, (long)ui.pitch, shown.pixels, size, size);
                        taskbar.clock_read = shown.read;
                        return;
                }
                taskbar_moon_clock(view, &ui, x, y, size, hours, minutes);
                if (!whole)
                        return;
                if (!shown.pixels || shown.bytes < need)
                {
                        shown.pixels = taskbar_remap(shown.pixels, shown.bytes, need);
                        if (!shown.pixels)
                                return;
                        shown.bytes = need;
                }
                taskbar_blit_square(shown.pixels, size, dst, (long)ui.pitch, size);
                shown.key = key, shown.read = taskbar.clock_read;
        }
}

// Tab icons: registry app picture first, then scaled window icon (cached by row), else kit symbol / letter tile / window icon.
struct taskbar_picture
{
        const unsigned int *from; // the picture it was made from
        unsigned int seq;         // and that picture's sequence
        int side;                 // how large it was made
        unsigned int pixels[UI_ICON_MOST * UI_ICON_MOST];
};
static struct ui_grown taskbar_pictures; // by the window's row, as many as the rows (pages touched only for the icons drawn)

// Whether the task numbered at has a picture: its app's, or the window's own.
static bool taskbar_tab_full(const struct taskbar_view *view, int at)
{
        return (view->apps && view->apps[at] && ui_apps_has_picture(ui_apps_match(view->apps[at]))) ||
               (view->icons && view->icon_seqs && view->icons[at] && view->icon_seqs[at]);
}

/*
        A picture of side pixels laid at x, y with the corners on the edges it shares with its task cut round (a radius of half the side, the
        task's round end): ends says which, a bit each, 1 the left, 2 the right, 4 the top, 8 the bottom. The cover of a pixel is how far
        its middle is inside the circle, held to 0 and 1, which is the half pixel of antialiasing a round plate has.
*/
static void taskbar_tab_rounded(struct ui address_to ui, int x, int y, int side, const unsigned int address_to picture, unsigned int ends)
{
        static unsigned int cut[UI_PICTURE_MOST * UI_PICTURE_MOST];

        if (side <= 0 || side > UI_PICTURE_MOST)
                return;
        for (int row = 0; row < side; row++)
                for (int column = 0; column < side; column++)
                {
                        unsigned int pixel = picture[row * side + column], cover = 256;
                        int dx = 2 * column + 1 - side, dy = 2 * row + 1 - side;

                        if (((ends & 1) && dx < 0) || ((ends & 2) && dx > 0) || ((ends & 4) && dy < 0) || ((ends & 8) && dy > 0))
                        {
                                int d = (int)ui_touch_isqrt((unsigned long long)(dx * dx + dy * dy));

                                cover = (unsigned int)taskbar_clamp((side - d) * 128 + 128, 0, 256);
                        }
                        if (cover < 256)
                                pixel = ((pixel >> 24) * cover >> 8) << 24 | ((pixel >> 16 & 255) * cover >> 8) << 16 |
                                        ((pixel >> 8 & 255) * cover >> 8) << 8 | ((pixel & 255) * cover >> 8);
                        cut[row * side + column] = pixel;
                }
        ui_icon_picture(ui, x, y, side, cut);
}

// Square element n in band pixels: fills ui, x, y and returns its side, or 0 if empty/clipped.
static int taskbar_in_band(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, int *x, int *y)
{
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1];
        int side = n->w < n->h ? n->w : n->h;

        *x = n->x - band->x, *y = n->y - band->y;
        if (side <= 0 || *x < 0 || *y < 0 || *x + side > band->w || *y + side > band->h)
                return 0;
        taskbar_band_ui(ui, view, t);
        return side;
}

// An app being started: the kit's spinner over its task's picture, the picture's size, turning on the monotonic clock.
static void taskbar_tab_turning(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n)
{
        int x, y, side = 0, scale = taskbar_scaled(view->scale);
        struct ui_control spin;
        struct ui ui;
        b32 motion = ui_kit.motion;

        if (scale <= 0 || !(side = taskbar_in_band(&ui, view, t, n, &x, &y)))
                return;
        memory_zero(&spin, sizeof(spin));
        spin.kind = UI_SPINNER, spin.w = side * 100 / scale, spin.x = x * 100 / scale, spin.y = y * 100 / scale;
        ui_kit.now = ui_kit_clock(), ui_kit.motion = !taskbar.glides;
        ui_spinner_draw(&ui, &spin);
        ui_kit.motion = motion;
}

static void taskbar_tab_icon(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, const struct ui_node address_to tab,
                             int which, unsigned int ink)
{
        const char *app = view->apps ? view->apps[which] : null;
        int x, y, side, symbol;
        unsigned int ends = 0;
        const unsigned int address_to picture;
        struct ui ui;

        if (!(side = taskbar_in_band(&ui, view, t, n, &x, &y)))
                return;
        if (tab && tab->w >= tab->h)
                ends = (n->x <= tab->x ? 1u : 0u) | (n->x + n->w >= tab->x + tab->w ? 2u : 0u);
        else if (tab)
                ends = (n->y <= tab->y ? 4u : 0u) | (n->y + n->h >= tab->y + tab->h ? 8u : 0u);
        if (!ends || (tab && n->w >= tab->w && n->h >= tab->h))
                ends = 15u;
        picture = side <= UI_PICTURE_MOST ? ui_apps_picture(ui_apps_match(app), side) : null;
        if (picture)
        {
                taskbar_tab_rounded(&ui, x, y, side, picture, ends);
                return;
        }
        if (view->icons && view->icon_seqs && view->icons[which] && view->icon_seqs[which] && side <= UI_ICON_MOST)
        {
                unsigned int row = view->rows ? view->rows[which] : (unsigned int)which;
                struct taskbar_picture address_to kept;

                if (!ui_grown_room(&taskbar_pictures, sizeof(struct taskbar_picture), WINDOWS, (long)row + 1))
                        return;
                kept = (struct taskbar_picture address_to)taskbar_pictures.at + row;
                if (kept->from != view->icons[which] || kept->seq != view->icon_seqs[which] || kept->side != side)
                {
                        ui_icon_scale(kept->pixels, side, view->icons[which]);
                        kept->from = view->icons[which], kept->seq = view->icon_seqs[which], kept->side = side;
                }
                taskbar_tab_rounded(&ui, x, y, side, kept->pixels, ends);
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
        struct taskbar_band address_to cur = taskbar_band_cur();
        struct taskbar_lit address_to lit = &cur->lit;
        int span[4] = {0, 0, 0, 0}, stripe[4], gw, time_ok;
        tm broken;
        unsigned int ground = 0, ink = 0;
        b64 lit_at, lit_length, lit_shown;
        b32 live = 0;
        char clock[6] = {0}, text[8];

        taskbar.stale = 0;
        cur->moon_own = 0;
        if (!view->pixels || (unsigned long)band->w * (unsigned long)band->h * 4 > view->band_bytes)
                return 0;
        for (int which = TASKBAR_TRAY_BEFORE; which < (int)view->count; which++)
                if (taskbar_slot_own(which) && (redo & taskbar_bit(which)) &&
                    taskbar_anchor(view, which, &stripe[0], &stripe[1], &stripe[2], &stripe[3]))
                {
                        if (which == TASKBAR_LAUNCHER)
                        {
                                int most = taskbar_moon_most(stripe[band->dir ? 3 : 2]);

                                stripe[band->dir] -= most, stripe[2 + band->dir] += 2 * most;
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
        time_ok = taskbar_broken_time(taskbar.moon_at, &broken);
        if (time_ok)
                clock[0] = (char)('0' + broken.tm_hour / 10), clock[1] = (char)('0' + broken.tm_hour % 10), clock[2] = ':',
                clock[3] = (char)('0' + broken.tm_min / 10), clock[4] = (char)('0' + broken.tm_min % 10);
        taskbar_battery_text(text, taskbar.battery_charging, taskbar.battery_percent);
        taskbar_lit_to(lit, t, view);
        lit_at = ui_tween_at(&lit->at, &live), lit_length = ui_tween_at(&lit->length, &live), lit_shown = ui_tween_at(&lit->shown, &live);
        for (int at = 2; at < t->count; at++)
        {
                const struct ui_node address_to n = &t->node[at];
                int which = n->id - 1, m = band->dir;
                b32 tray_item = which <= TASKBAR_TRAY_ITEM_BASE && which > TASKBAR_TRAY_ITEM_BASE - TASKBAR_TRAY_ITEM_MAX;
                struct ui ui;

                if (!n->id || which == TASKBAR_TABS || which == TASKBAR_TABS_LEFT || (which <= TASKBAR_ZONE && which > TASKBAR_DONE) || !(redo & taskbar_bit(which)) ||
                    (which < 0 && which != TASKBAR_LAUNCHER && which != TASKBAR_DONE && !tray_item && !time_ok))
                        continue;
                if (which >= 0 || which == TASKBAR_TRAY || which == TASKBAR_TRAY_BEFORE || which == TASKBAR_DONE || which == TASKBAR_STRIP_BACK ||
                    which == TASKBAR_STRIP_ON || (which == -3 && taskbar.clock_tab))
                {
                        unsigned int mark = which >= 0 ? view->marks[which] : 0;
                        struct ui_box clip = taskbar_face(t, n, 0, 0);

                        ground = taskbar_glass_tab(view, rect[0], rect[1], gw, band->x, band->y, band->w, n->x, n->y, n->w, n->h);
                        taskbar_glass_ui(&ui, view, t, ground);
                        if (which >= 0)
                        {
                                struct ui_box face = taskbar_face(t, n, lit_at, lit_length);
                                unsigned int full = ui_kit_lit(&ui);

                                clip.rx = clip.ry = 0;
                                if (lit_length > 0)
                                        ui_glass_button(&ui, &face, lit_shown, &clip);
                                ground = mark & COMPOSITOR_TAB_LIT ? window_mix(ground, full & 0xffffffu, (full >> 24) + (full >> 31)) : ground;
                        }
                        else if (which == TASKBAR_DONE)
                                ground = ui_glass_button(&ui, &clip, UI_ONE, null);
                        ink = which >= 0 ? taskbar_ink_on(ground, mark) : window_contrast(ground);
                }
                if (taskbar.editing && (which >= 0 || taskbar_item_of(which) >= 0))
                        taskbar_edit_ring(view, t, n, ground, ink, 0x66000000u);
                switch (which)
                {
                case TASKBAR_LAUNCHER:
                        taskbar_moon(view, t, n, time_ok ? broken.tm_hour : -1, time_ok ? broken.tm_min : -1, view->launcher_lit);
                        if (taskbar.editing)
                                taskbar_edit_ring(view, t, n, ground, window_contrast(ground), 0x99000000u);
                        break;
                case TASKBAR_TRAY:
                case TASKBAR_TRAY_BEFORE:
                        break;
                case TASKBAR_DONE:
                        taskbar_say(view, t, n, (const char *)WORDS(tray_done), ink, ground);
                        break;
                case TASKBAR_STRIP_BACK:
                case TASKBAR_STRIP_ON:
                        taskbar_say(view, t, n, (which == TASKBAR_STRIP_BACK) != (m == 0 && language_rtl()) ? "\xe2\x80\xb9" : "\xe2\x80\xba", ink, ground);
                        break;
                case -9:
                        if (taskbar.tray_custom)
                                taskbar_say(view, t, n, taskbar.tray_text, ink, ground);
                        break;
#define TASKBAR_CELL(slot, shown, mark, paint) case slot: paint; break;
                TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
                default:
                        if (tray_item)
                        {
                                const struct ui_node address_to mark = n->first >= 0 ? &t->node[n->first] : null;
                                const unsigned int *px = taskbar_tray_items_pixels(TASKBAR_TRAY_ITEM_BASE - which);

                                if (!time_ok)
                                        ground = taskbar_glass_tab(view, rect[0], rect[1], gw, band->x, band->y, band->w, n->x, n->y, n->w, n->h),
                                        ink = window_contrast(ground);
                                if (mark && px)
                                {
                                        int mx = mark->x - band->x, my = mark->y - band->y, mw = mark->w, mh = mark->h;

                                        for (int yy = 0; yy < mh && my + yy < band->h; yy++)
                                                for (int xx = 0; xx < mw && mx + xx < band->w; xx++)
                                                {
                                                        unsigned int src = px[(yy * TASKBAR_TRAY_ITEM_PX / mh) * TASKBAR_TRAY_ITEM_PX + (xx * TASKBAR_TRAY_ITEM_PX / mw)];
                                                        unsigned int a = src >> 24;

                                                        if (a && mx + xx >= 0 && my + yy >= 0)
                                                                view->pixels[(my + yy) * band->w + (mx + xx)] =
                                                                    window_mix(view->pixels[(my + yy) * band->w + (mx + xx)], src & 0xffffffu, (int)(a + (a >> 7)));
                                                }
                                }
                                else if (mark)
                                        taskbar_say(view, t, mark, "\xe2\x80\xa2", ink, ground);
                                break;
                        }
                        {
                                const struct ui_node address_to icon = n->first >= 0 ? &t->node[n->first] : null;
                                const struct ui_node address_to label = icon && icon->next >= 0 ? &t->node[icon->next] : null;

                                if (taskbar_fits(n, icon))
                                {
                                        taskbar_tab_icon(view, t, icon, n, which, ink);
                                        if (view->marks[which] & COMPOSITOR_TAB_STARTING)
                                                taskbar_tab_turning(view, t, icon);
                                }
                                if (taskbar_fits(n, label) && label->w > 0 && label->h > 0 && view->titles && view->titles[which] && view->titles[which][0])
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

// The value of a key in the kept settings: a line "key value", as the battery verb keeps /root/tune.
static void taskbar_tune_value(const char *text, const char *key, char *into, int room)
{
        const char *value = taskbar_line_value(text, key, ' ');
        int length = 0;

        into[0] = 0;
        if (!value)
                return;
        while (value[length] && value[length] != '\n' && length < room - 1)
                into[length] = value[length], length++;
        into[length] = 0;
}

// The settings: the position and the display ("" when none), and the battery named in /root/tune.
static void taskbar_pins_read(void);

static void taskbar_settings_set(const char *position, const char *display, const char *tune)
{
        static const struct
        {
                const char *word;
                unsigned int pos;
        } edges[4] = {{"bottom", TASKBAR_BOTTOM}, {"top", TASKBAR_TOP}, {"left", TASKBAR_LEFT}, {"right", TASKBAR_RIGHT}};
        unsigned int want = taskbar.position_default;

        for (int i = 0; i < 4; i++)
                if (string_equals((string_address)position, (string_address)edges[i].word))
                        want = edges[i].pos;
        taskbar.position = want;
        taskbar.all = string_equals((string_address)display, (string_address)"all");
        taskbar_tune_value(tune, "battery", taskbar.battery, sizeof(taskbar.battery));
        taskbar_battery_refresh();
        taskbar_pins_read();
        taskbar.stale = 1;
}

static int taskbar_display_all(void)
{
        return taskbar.all;
}

static int taskbar_volume_set(int present, int percent, int muted, int flash)
{
        int changed;

        present = present != 0, muted = muted != 0, flash = flash != 0;
        percent = taskbar_clamp(percent, 0, 100);
        changed = present != taskbar.volume_item || (present && (percent != taskbar.volume_percent || muted != taskbar.volume_muted || flash != taskbar.volume_flash));
        taskbar.volume_item = present, taskbar.volume_percent = percent, taskbar.volume_muted = muted, taskbar.volume_flash = flash;
        return taskbar.stale |= changed, changed;
}

static int taskbar_mic_set(int present, int muted)
{
        int changed;

        present = present != 0, muted = muted != 0;
        changed = present != taskbar.mic_item || (present && muted != taskbar.mic_muted);
        taskbar.mic_item = present, taskbar.mic_muted = muted;
        return taskbar.stale |= changed, changed;
}

static int taskbar_notices_set(int present, unsigned int severity)
{
        int changed;

        present = present != 0;
        severity = present && severity == WINDOWS_ISSUE_ERROR ? WINDOWS_ISSUE_ERROR : present ? WINDOWS_ISSUE_WARNING : 0;
        changed = present != taskbar.notices_item || severity != taskbar.notices_severity;
        taskbar.notices_item = present, taskbar.notices_severity = severity;
        return taskbar.stale |= changed, changed;
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
        A program pinned to the taskbar: an app of the registry (ui_apps), by its id and the key its windows say, what its task says while
        it has none (its name), and the words it is kept as (`taskbar pinned`: its id, the desktop file it is from and its launcher, the one
        it is started by), a null after each. The programs pinned are taskbar_pins (below); a menu keeps the one it may pin.
*/
#define TASKBAR_PIN_TITLE 32

struct taskbar_pin
{
        char app[WINDOWS_APP_ID];      // the app's id in the registry (ui_apps)
        char key[WINDOWS_APP_ID];      // and its window key, which a window of it says
        char title[TASKBAR_PIN_TITLE];
        unsigned short count;                  // words
        unsigned short at[SETTING_PINS_WORDS]; // where each begins in text
        char text[SETTING_PINS_MOST + SETTING_PINS_WORDS];
};

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
        int row;                       // a tab's menu: the row of the window it is for, and the process the row had when it was opened (0 when none)
        unsigned int pid;
        char stop[96];                 // its Force stop's words, with the windows it takes when it takes more than one
        struct taskbar_pin pin;        // what Pin to taskbar would pin: its app_id is the program an Unpin is for (none: no pin in the menu)
} taskbar_menu;

// The kit's ui the menu is drawn with: the band's faces and engine, the band's scale, the theme's surface for its ground.
static fn taskbar_menu_ui(const struct taskbar_view *view, unsigned int address_to pixels)
{
        struct ui address_to ui = &taskbar_menu.ui;
        struct windows address_to table = compositor.table;
        unsigned int themed = table && table->themed;

        //      The kit's theme, which a kit program takes from the table and the compositor had never been given: a menu's ground
        //      (ui_role_surface) and its words (ui_text_colour) were the zero theme's, black on black, with only the separator's
        //      line (ui->ink) to be seen. At once, as reduced motion turns it: the menu is drawn whole each time it is drawn.
        ui_kit_theme(themed ? table->theme : UI_THEME_LIGHT, themed ? table->accent : UI_ACCENT_STANDARD, 1);

        unsigned int address_to scratch = ui->scratch;
        int scratch_width = ui->scratch_width, scratch_scale = ui->scratch_scale;

        memory_zero(ui, sizeof(*ui));
        ui->engine = view->engine, ui->regular = view->face, ui->bold = view->bold ? view->bold : view->face;
        ui->scale = taskbar_scaled(view->scale), ui->opaque = 1;
        ui->pixels = pixels, ui->pitch = (unsigned long)taskbar_menu.width, ui->width = taskbar_menu.width, ui->height = taskbar_menu.height;
        ui->top = 0, ui->bottom = taskbar_menu.height;
        ui_inks(ui, compositor_tab_target(0) & 0xffffffu);
        //      The line a run of text is drawn through, kept from draw to draw and grown with the menu: with none (the ui made anew
        //      each time) every word was skipped, and the menu was its ground and its separators.
        ui->scratch = scratch, ui->scratch_width = scratch_width, ui->scratch_scale = scratch_scale;
        if (pixels)
                ui_scratch_fit(ui, (unsigned int)taskbar_menu.width);
}

// The menu made, sized and put beside the slot at x, y (width w, height h) of the band being drawn, on the screen's side of the band
// and held inside the screen: false with no room for it.
static int taskbar_menu_place(const struct taskbar_view *view, int x, int y, int w, int h)
{
        struct ui_anchor anchor;
        int gap;

        taskbar_menu.width = taskbar_menu.height = 1;
        taskbar_menu_ui(view, null);
        ui_popmenu_size(&taskbar_menu.ui, &taskbar_menu.menu, view->height / 2);
        taskbar_menu.width = taskbar_menu.menu.width, taskbar_menu.height = taskbar_menu.menu.height;
        if (taskbar_menu.width <= 0 || taskbar_menu.height <= 0)
                return 0;
        // Beside the slot on the screen's side of the band, its middle on the slot's, held inside the screen.
        gap = taskbar_scaled(view->scale) * 6 / 100;
        anchor = (struct ui_anchor){
            .anchored = true,
            .button = {x - gap, y - gap, w + 2 * gap, h + 2 * gap},
            .screen = {view->x, view->y, view->width, view->height},
            .edge = (unsigned int)view->position,
        };
        ui_anchor_beside(&anchor, taskbar_menu.width, taskbar_menu.height, &taskbar_menu.x, &taskbar_menu.y);
        taskbar_menu.open = 1, taskbar_menu.dirty = 1;
        return 1;
}

// Prepare taskbar_menu for slot: checks faces/anchor, resets state, fills anchor rect.
static int taskbar_menu_begin(const struct taskbar_view *view, int slot, int row, unsigned int pid, int rect[4])
{
        if (!view->engine || !view->face || !taskbar_anchor(view, slot, &rect[0], &rect[1], &rect[2], &rect[3]))
                return 0;
        taskbar_menu.row = row, taskbar_menu.pid = pid;
        memory_zero(&taskbar_menu.menu, sizeof(taskbar_menu.menu));
        taskbar_menu.menu.hot = -1;
        return 1;
}

static int taskbar_menu_build(const struct taskbar_view *view, const int rect[4], const struct ui_menu_spec *specs, int count)
{
        ui_popmenu_build(&taskbar_menu.menu, specs, count);
        return taskbar_menu_place(view, rect[0], rect[1], rect[2], rect[3]);
}

// The menu made and placed beside the moon of the band being drawn: false with no moon or no face to write it with.
static int taskbar_menu_open(const struct taskbar_view *view)
{
        int rect[4];
        const char *meridiem = null;
        tm broken;

        if (!taskbar_menu_begin(view, TASKBAR_LAUNCHER, -1, 0, rect))
                return 0;
        if (taskbar.clock_12h && taskbar_broken_time(taskbar.moon_at, &broken))
                meridiem = broken.tm_hour < 12 ? WORDS(tray_am) : WORDS(tray_pm);
        const struct ui_menu_spec specs[] = {
            {meridiem, null, UI_MENU_HEADER, 0, !meridiem},
            {WORDS(tray_edit), null, 0, TASKBAR_MENU_EDIT, false},
            {WORDS(tray_settings), null, 0, TASKBAR_MENU_SETTINGS, false},
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {taskbar.clock_12h ? WORDS(tray_clock_24h) : WORDS(tray_clock_12h), null, 0, TASKBAR_MENU_CLOCK, false},
        };
        return taskbar_menu_build(view, rect, specs, (int)(sizeof(specs) / sizeof(specs[0])));
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

/*
        Force stop: walks up from a window's pid to its program root (stopping below system/desktop/launcher/reaper or
        foreign-app window owners), SIGSTOPs the subtree iteratively until no newly forked children appear, then SIGKILLs.
*/
#define TASKBAR_PROC_MOST 8192
#define TASKBAR_PROC_PLAIN 0u
#define TASKBAR_PROC_DESKTOP 1u
#define TASKBAR_PROC_SYSTEM 2u
#define TASKBAR_PROC_LAUNCHER 3u
#define TASKBAR_PROC_LAUNCHER_HELPER 4u
#define TASKBAR_PROC_REAPER 5u
#define TASKBAR_WIN_SAME 1u
#define TASKBAR_WIN_FOREIGN 2u
#define TASKBAR_SIGKILL 9u
#define TASKBAR_SIGSTOP 19u

struct taskbar_proc
{
        int pid, ppid;
        unsigned int kind;
};

static const char *taskbar_proc_root = "/proc";
static struct taskbar_proc taskbar_procs[TASKBAR_PROC_MOST];
static int taskbar_stop_members[TASKBAR_PROC_MOST];

// A signal sent to a process: what the walk sends through (a check's own, which sends none, takes its place).
static void taskbar_signal_send(int pid, unsigned int signal)
{
        if (pid > 2)
                compositor_call(syscall(kill), (positive)pid, signal, 0, 0, 0, 0);
}

static void (*taskbar_signal)(int pid, unsigned int signal) = taskbar_signal_send;

// The kind of a process by its name, and by its number: pid 1, pid 2 (kthreadd), and self.
static unsigned int taskbar_proc_kind(int self, int pid, const char *comm)
{
        static const char *const system_names[] = {"init", "moonwater", "compositor", "audio", "sessionbus", "bowl", "osk", "kthreadd", null};
        static const char *const desktop_names[] = {"terminal", "settings", "launcher", "klog", "mixer", "notices", "files", "gallery", "editor", "identify", null};
        static const char *const launcher_names[] = {"steam", "lutris", "heroic", "bottles", "prismlauncher", "itch", "legendary", null};
        static const char *const helper_names[] = {"steamwebhelper", "steamerrorrepor", null};
        static const char *const reaper_names[] = {"reaper", "steam-launch-wr", "pv-bwrap", "pressure-vessel", "gamescope", "srt-bwrap", "gamemoderun", null};
        static const struct
        {
                unsigned int kind;
                const char *const *names;
        } groups[] = {
            {TASKBAR_PROC_SYSTEM, system_names}, {TASKBAR_PROC_DESKTOP, desktop_names}, {TASKBAR_PROC_LAUNCHER, launcher_names},
            {TASKBAR_PROC_LAUNCHER_HELPER, helper_names}, {TASKBAR_PROC_REAPER, reaper_names},
        };
        static const char x[] = "xwayland";
        int same = 1;

        if (pid <= 2 || pid == self)
                return TASKBAR_PROC_SYSTEM;
        for (int at = 0; same && x[at]; at++)
                same = (comm[at] | 32) == x[at];
        if (same)
                return TASKBAR_PROC_SYSTEM;
        for (int g = 0; g < (int)(sizeof(groups) / sizeof(groups[0])); g++)
                for (int at = 0; groups[g].names[at]; at++)
                        if (string_equals((string_address)comm, (string_address)groups[g].names[at]))
                                return groups[g].kind;
        return TASKBAR_PROC_PLAIN;
}

static int taskbar_is_launcher(unsigned int kind)
{
        return kind == TASKBAR_PROC_LAUNCHER || kind == TASKBAR_PROC_LAUNCHER_HELPER;
}

// A process's entry in the table, or -1.
static int taskbar_proc_at(const struct taskbar_proc *t, int n, int pid)
{
        for (int at = 0; at < n; at++)
                if (t[at].pid == pid)
                        return at;
        return -1;
}

// Whether two non-empty window app_ids belong to distinct programs (Steam's "steam" and "steamwebhelper" share one UI).
static int taskbar_apps_differ(const char *a, const char *b)
{
        if (!a || !a[0] || !b || !b[0])
                return 0;
        if ((ui_apps_same(a, "steam") || ui_apps_same(a, "steamwebhelper")) &&
            (ui_apps_same(b, "steam") || ui_apps_same(b, "steamwebhelper")))
                return 0;
        return !ui_apps_same(a, b);
}

// Whether pid is root_pid or a descendant of root_pid in the process table t (bounded against cycles).
static int taskbar_pid_in_subtree(const struct taskbar_proc *t, int n, int pid, int root_pid)
{
        int at = taskbar_proc_at(t, n, pid), steps = 0;

        while (at >= 0 && steps++ < n)
        {
                if (t[at].pid == root_pid)
                        return 1;
                at = taskbar_proc_at(t, n, t[at].ppid);
        }
        return 0;
}

// What live top-level windows live under sub_pid (excluding skip_pid's subtree when skip_pid > 0):
// TASKBAR_WIN_SAME if any window matches target_app, TASKBAR_WIN_FOREIGN if any belongs to another app.
static unsigned int taskbar_subtree_window_flags(const struct taskbar_proc *t, int n, int sub_pid, int skip_pid,
                                                 const char *target_app, int target_is_launcher)
{
        unsigned int flags = 0;

        if (!compositor.table || !compositor.showing || !compositor_popup_parent || !compositor.app_id)
                return 0;
        for (int row = 0; row < COMPOSITOR_ROWS; row++)
        {
                unsigned int wpid;
                const char *wapp;
                int wat;

                if (!compositor.showing[row] || compositor_popup_parent[row] >= 0)
                        continue;
                wpid = __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE);
                if (wpid <= 2 || wpid > 0x7fffffffu || !taskbar_pid_in_subtree(t, n, (int)wpid, sub_pid) ||
                    (skip_pid > 0 && taskbar_pid_in_subtree(t, n, (int)wpid, skip_pid)))
                        continue;
                wapp = compositor.app_id[row];
                wat = taskbar_proc_at(t, n, (int)wpid);
                if (taskbar_apps_differ(wapp, target_app))
                        flags |= TASKBAR_WIN_FOREIGN;
                else if (target_is_launcher && (!wapp || !wapp[0]) && wat >= 0 && !taskbar_is_launcher(t[wat].kind))
                        flags |= TASKBAR_WIN_FOREIGN;
                else if (target_app && target_app[0] && wapp && wapp[0])
                        flags |= TASKBAR_WIN_SAME;
        }
        return flags;
}

/*
        The root of the program the process pid is of: the highest process above it (or it) that is no system, desktop,
        launcher (for a spawned game/app) or foreign-app window owner, one step lower where its parent is a desktop one
        and it is not pid itself. A game launch wrapper (reaper, pv-bwrap, gamescope) stops before climbing into its
        parent launcher or shell. 0 for a pid that is not in the table or a system process, which has no Force stop.
*/
static int taskbar_stop_root_for(const struct taskbar_proc *t, int n, int pid, const char *target_app)
{
        int at = taskbar_proc_at(t, n, pid), below = at, steps = 0, is_launcher;

        if (at < 0 || t[at].kind == TASKBAR_PROC_SYSTEM)
                return 0;
        is_launcher = taskbar_is_launcher(t[at].kind);
        while (steps++ < n)
        {
                int up = taskbar_proc_at(t, n, t[at].ppid);

                if (up < 0 || t[up].kind == TASKBAR_PROC_SYSTEM)
                        break;
                if (t[up].kind == TASKBAR_PROC_DESKTOP)
                {
                        at = below != at ? below : at;
                        break;
                }
                if (!is_launcher && t[at].kind == TASKBAR_PROC_REAPER && t[up].kind != TASKBAR_PROC_REAPER)
                        break;
                if (!is_launcher && taskbar_is_launcher(t[up].kind))
                        break;
                if (!is_launcher && target_app && target_app[0] &&
                    (taskbar_subtree_window_flags(t, n, t[up].pid, t[at].pid, target_app, 0) & TASKBAR_WIN_FOREIGN))
                        break;
                below = at, at = up;
        }
        return t[at].pid;
}

static int taskbar_stop_root(const struct taskbar_proc *t, int n, int pid)
{
        return taskbar_stop_root_for(t, n, pid, null);
}

// Whether a pid is one of members.
static int taskbar_stop_has(const int *members, int count, unsigned int pid)
{
        for (int each = 0; each < count; each++)
                if ((unsigned int)members[each] == pid)
                        return 1;
        return 0;
}

/*
        What is under a root: the root and every process whose parent is one of them, leaving out system processes,
        game launch wrappers entered from outside (so Force-stopping Steam spares running games), and child subtrees
        that exclusively own another app's top-level window. Answers how many fit in members.
*/
static int taskbar_stop_tree_for(const struct taskbar_proc *t, int n, int root, int target_pid, const char *target_app,
                                 int *members, int most)
{
        int root_at = taskbar_proc_at(t, n, root), count = 0, grown = 1;
        int root_is_reaper, target_is_launcher, target_at;

        if (root_at < 0 || t[root_at].kind == TASKBAR_PROC_SYSTEM || most <= 0)
                return 0;
        target_at = target_pid > 0 ? taskbar_proc_at(t, n, target_pid) : root_at;
        root_is_reaper = t[root_at].kind == TASKBAR_PROC_REAPER;
        target_is_launcher = taskbar_is_launcher(t[root_at].kind) || (target_at >= 0 && taskbar_is_launcher(t[target_at].kind));
        members[count++] = root;
        while (grown)
        {
                grown = 0;
                for (int at = 0; at < n && count < most; at++)
                {
                        if (t[at].kind == TASKBAR_PROC_SYSTEM || !taskbar_stop_has(members, count, (unsigned int)t[at].ppid) ||
                            taskbar_stop_has(members, count, (unsigned int)t[at].pid) ||
                            (!root_is_reaper && t[at].kind == TASKBAR_PROC_REAPER))
                                continue;
                        if ((target_app && target_app[0]) || target_is_launcher)
                        {
                                unsigned int flags = taskbar_subtree_window_flags(t, n, t[at].pid, 0, target_app, target_is_launcher);
                                int has_target = (target_pid > 0 && taskbar_pid_in_subtree(t, n, target_pid, t[at].pid)) ||
                                                 (flags & TASKBAR_WIN_SAME);

                                if ((flags & TASKBAR_WIN_FOREIGN) && !has_target)
                                        continue;
                        }
                        members[count++] = t[at].pid, grown = 1;
                }
        }
        return count;
}

static int taskbar_stop_tree(const struct taskbar_proc *t, int n, int root, int *members, int most)
{
        return taskbar_stop_tree_for(t, n, root, 0, null, members, most);
}

// The table of /proc: each process's number, its parent's and its kind, from its stat file. -1 on overflow.
static int taskbar_proc_read(struct taskbar_proc *into, int most)
{
        char path[96], buffer[4096], text[512];
        bipolar handle;
        int count = 0, self = (int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);

        handle = system_open_at(AT_FDCWD, (string_address)taskbar_proc_root, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        if (handle < 0)
                return 0;
        for (;;)
        {
                bipolar got = system_read_directory(handle, buffer, sizeof(buffer));

                if (got <= 0)
                        break;
                for (positive at = 0; at < (positive)got;)
                {
                        const struct linux_dirent64 address_to record = (const struct linux_dirent64 address_to)(buffer + at);
                        int pid = 0, ok = record->d_name[0] >= '0' && record->d_name[0] <= '9', open = -1, close = -1, ppid = 0, comm_length;
                        char comm[24];

                        if (record->d_reclen <= 19 || at + record->d_reclen > (positive)got)
                                break;
                        at += record->d_reclen;
                        for (int digit = 0; ok && record->d_name[digit]; digit++)
                                ok = record->d_name[digit] >= '0' && record->d_name[digit] <= '9' && digit < 9,
                                pid = pid * 10 + record->d_name[digit] - '0';
                        if (!ok || !pid)
                                continue;
                        taskbar_path_num(path, (int)sizeof(path), taskbar_proc_root, "/", (positive)pid, "/stat");
                        if (file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)text, sizeof(text)) <= 0)
                                continue;
                        for (int each = 0; text[each]; each++)
                        {
                                open = open < 0 && text[each] == '(' ? each : open;
                                close = text[each] == ')' ? each : close;
                        }
                        if (open < 0 || close < open || !text[close + 1] || !text[close + 2] || !text[close + 3])
                                continue;
                        comm_length = close - open - 1 < (int)sizeof(comm) - 1 ? close - open - 1 : (int)sizeof(comm) - 1;
                        memory_copy(comm, text + open + 1, (unsigned long)comm_length);
                        comm[comm_length] = 0;
                        for (int each = close + 4; text[each] >= '0' && text[each] <= '9' && each < close + 14; each++)
                                ppid = ppid * 10 + text[each] - '0';
                        if (count >= most)
                        {
                                system_close(handle);
                                return -1;
                        }
                        into[count++] = (struct taskbar_proc){pid, ppid, taskbar_proc_kind(self, pid, comm)};
                }
        }
        system_close(handle);
        return count;
}

// A row's window may be stopped: its program's root and what is under it, from a table just read.
static int taskbar_stop_plan_for(unsigned int pid, const char *target_app, int *root)
{
        int n;

        *root = 0;
        if (pid <= 2 || pid > 0x7fffffffu)
                return 0;
        n = taskbar_proc_read(taskbar_procs, TASKBAR_PROC_MOST);
        if (n <= 0)
                return 0;
        *root = taskbar_stop_root_for(taskbar_procs, n, (int)pid, target_app);
        return *root ? taskbar_stop_tree_for(taskbar_procs, n, *root, (int)pid, target_app, taskbar_stop_members, TASKBAR_PROC_MOST) : 0;
}

// How many windows (rows that show, and are no popup of one) are of the plan's members now.
static int taskbar_stop_rows(int count)
{
        int windows = 0;

        for (int row = 0; compositor.table && row < COMPOSITOR_ROWS; row++)
                if (compositor.showing[row] && compositor_popup_parent[row] < 0)
                        windows += taskbar_stop_has(taskbar_stop_members, count, __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE));
        return windows;
}

// Every process of the program the row's pid is of ended: all stopped first, then all sent SIGKILL.
static int taskbar_stop_run_for(unsigned int pid, const char *target_app)
{
        static int stopped[TASKBAR_PROC_MOST];
        int count = 0, root;

        for (int pass = 0; pass < 8; pass++)
        {
                int members = taskbar_stop_plan_for(pid, target_app, &root), fresh = 0;

                if (!members)
                        break;
                for (int at = 0; at < members; at++)
                        if (!taskbar_stop_has(stopped, count, (unsigned int)taskbar_stop_members[at]) && count < TASKBAR_PROC_MOST)
                        {
                                stopped[count++] = taskbar_stop_members[at], fresh++;
                                taskbar_signal(taskbar_stop_members[at], TASKBAR_SIGSTOP);
                        }
                if (!fresh)
                        break;
        }
        for (int at = 0; at < count; at++)
                taskbar_signal(stopped[at], TASKBAR_SIGKILL);
        return count;
}

/*
        Pinned apps (`taskbar pinned`): registry entries (ui_apps) shown as ghost tabs after running windows while no window
        matches their key/id. Pressing a ghost writes its launcher words to a new /run/moonwater/pin.<N> file and notifies the
        session script (`pin_start_<N> <slot>`). Refreshed at most once per UI_APPS_LOOK_NS via ui_apps_fresh.
*/
static struct
{
        int count;
        struct taskbar_pin pin[SETTING_PINS_ENTRIES];
        int ghosts;                                  // the pinned programs shown as tasks now,
        int ghost_tab[SETTING_PINS_ENTRIES];         // the number of each one's task,
        int ghost_pin[SETTING_PINS_ENTRIES];         // and which pinned program it is
        positive started[SETTING_PINS_ENTRIES];      // when each was started last (the monotonic clock), 0 never: a press on it again soon is the same press
        unsigned int runs;                           // the starts made, which makes a start's file new
} taskbar_pins;

static const char *taskbar_run_root = "/run/moonwater";
#define TASKBAR_PIN_AGAIN (3ull * 1000000000ull)

static const char *taskbar_pin_word(const struct taskbar_pin *pin, int at)
{
        return pin->text + pin->at[at];
}

// A word appended to a pin's words: false with no room for it or for another word.
static bool taskbar_pin_put(struct taskbar_pin *pin, positive *used, const char *word)
{
        positive length = string_length((string_address)word) + 1;

        if (pin->count >= SETTING_PINS_WORDS || *used + length > sizeof(pin->text))
                return false;
        pin->at[pin->count++] = (unsigned short)*used;
        memory_copy(pin->text + *used, word, length);
        *used += length;
        return true;
}

static const char *taskbar_app_title(const struct ui_app_entry address_to app)
{
        return app->kit ? language_of(ui_kit_programs[app->kit - 1].label) : app->name;
}

static bool taskbar_app_showing(const char *key, const char *id)
{
        for (int row = 0; row < COMPOSITOR_ROWS; row++)
                if (compositor.showing[row] && (ui_apps_same(compositor.app_id[row], key) || ui_apps_same(compositor.app_id[row], id)))
                        return true;
        return false;
}

// The pin of an app of the registry: its id, its window key and its name, and its words (its id, the file it is from, its launcher).
static bool taskbar_pin_of(int which, struct taskbar_pin *pin)
{
        const struct ui_app_entry *app;
        positive used = 0;

        memory_zero(pin, sizeof(*pin));
        if (!ui_apps_pinnable(which))
                return false;
        app = &ui_apps.app[which];
        string_copy_bounded((p8 *)pin->app, (string_address)app->id, sizeof(pin->app));
        string_copy_bounded((p8 *)pin->key, (string_address)app->key, sizeof(pin->key));
        string_copy_bounded((p8 *)pin->title, (string_address)taskbar_app_title(app), sizeof(pin->title));
        return taskbar_pin_put(pin, &used, app->id) && taskbar_pin_put(pin, &used, app->source) && taskbar_pin_put(pin, &used, app->launch);
}

// Whether a window that says app (its app_id, or an X window's class) is a pinned app's.
static bool taskbar_pin_runs(const struct taskbar_pin *pin, const char *app)
{
        return app && app[0] && (ui_apps_same(app, pin->key) || ui_apps_same(app, pin->app));
}

static int taskbar_pin_find(const char *app)
{
        for (int each = 0; each < taskbar_pins.count; each++)
                if (string_equals((string_address)taskbar_pins.pin[each].app, (string_address)app))
                        return each;
        return -1;
}

// The pinned programs as a line says them (none, or a line that is none, has none): each entry's app of the registry, once.
static void taskbar_pins_set(const char *line)
{
        char word[SETTING_PINS_WORD];
        positive at = 0;
        int first = 1;

        taskbar_pins.count = 0;
        taskbar.stale = 1;
        if (!line || !line[0] || string_equals((string_address)line, (string_address) "none") || !setting_pins_valid(line, null))
                return;
        ui_apps_fresh((b64)ui_kit_time());
        for (;;)
        {
                int kind = setting_pins_next(line, &at, word, sizeof(word));

                if (kind == SETTING_PINS_WORD_NEXT && first)
                {
                        int which = ui_apps_find(word);

                        which = which >= 0 ? which : ui_apps_match(word);
                        first = 0;
                        if (which >= 0 && taskbar_pins.count < SETTING_PINS_ENTRIES && taskbar_pin_find(ui_apps.app[which].id) < 0 &&
                            taskbar_pin_of(which, &taskbar_pins.pin[taskbar_pins.count]))
                                taskbar_pins.count++;
                        continue;
                }
                if (kind == SETTING_PINS_WORD_NEXT)
                        continue;
                first = 1;
                if (kind != SETTING_PINS_SEPARATOR)
                        break;
        }
}

static void taskbar_pins_path(char *path, positive room)
{
        ui_apps_pins_path(compositor_settings_root, path, room);
}

static void taskbar_pins_read(void)
{
        char path[160], text[SETTING_PINS_MOST + 16];

        taskbar_pins_path(path, sizeof(path));
        windows_read_line(path, text, sizeof(text), FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        taskbar_pins_set(text);
}

static void taskbar_pin_keep(int which, bool on)
{
        if (ui_apps_pin(compositor_settings_root, which, on) > 0)
        {
                taskbar_pins_read();
                compositor_tabs_changed();
        }
}

/*
        The apps being started (the table's starting slots, windows_starting_set: the launcher's and a pin's): each app of the registry that
        has no window up yet, until WINDOWS_STARTING_NS has passed, is a task that turns.
*/
#define TASKBAR_STARTING_FRAME 33000000ull // the turn drawn again this often while an app is being started (monotonic ns)

static struct
{
        int count;                                    // the apps being started now,
        char id[WINDOWS_STARTING][WINDOWS_APP_ID];    // each one's registry id,
        char title[WINDOWS_STARTING][TASKBAR_PIN_TITLE]; // its name,
        b32 pinned[WINDOWS_STARTING];                 // and whether a pin's task is its
        positive drawn;                               // when the turn was last drawn
} taskbar_starting;

// The apps being started taken from the table at now: whether what is shown of them changed.
static bool taskbar_starting_take(positive now)
{
        char ids[WINDOWS_STARTING][WINDOWS_APP_ID], held[WINDOWS_APP_ID];
        int count = 0;
        bool changed;

        for (unsigned int slot = 0; compositor.table && slot < WINDOWS_STARTING; slot++)
        {
                unsigned long long stamp = windows_starting_read(compositor.table, slot, held);
                int which;

                if (!stamp)
                        continue;
                which = ui_apps_find(held);
                if (which < 0 || now - stamp > WINDOWS_STARTING_NS || taskbar_app_showing(ui_apps.app[which].key, ui_apps.app[which].id))
                {
                        windows_starting_done(compositor.table, slot);
                        continue;
                }
                string_copy_bounded((p8 *)ids[count++], (string_address)held, WINDOWS_APP_ID);
        }
        changed = count != taskbar_starting.count;
        for (int each = 0; each < count && !changed; each++)
                changed = !string_equals((string_address)ids[each], (string_address)taskbar_starting.id[each]);
        if (!changed)
                return false;
        taskbar_starting.count = count;
        for (int each = 0; each < count; each++)
        {
                const struct ui_app_entry address_to app = &ui_apps.app[ui_apps_find(ids[each])];

                string_copy_bounded((p8 *)taskbar_starting.id[each], (string_address)ids[each], WINDOWS_APP_ID);
                string_copy_bounded((p8 *)taskbar_starting.title[each], (string_address)taskbar_app_title(app), TASKBAR_PIN_TITLE);
        }
        return true;
}

static bool taskbar_pin_starting(const struct taskbar_pin *pin)
{
        for (int each = 0; each < taskbar_starting.count; each++)
                if (string_equals((string_address)taskbar_starting.id[each], (string_address)pin->app))
                        return taskbar_starting.pinned[each] = true;
        return false;
}

static positive taskbar_starting_due(void)
{
        return taskbar_starting.count ? taskbar_starting.drawn + TASKBAR_STARTING_FRAME : 0;
}

static bool taskbar_starting_keep(positive now)
{
        if (!taskbar_starting_take(now) && !(taskbar_starting.count && now >= taskbar_starting_due()))
                return false;
        taskbar_starting.drawn = now;
        return true;
}

static void taskbar_ghost_add(struct taskbar_view *view, unsigned char *marks, unsigned char *lengths, unsigned int *rows,
                              unsigned int at, const char *title, const char *app, unsigned char mark)
{
        view->titles[at] = title, view->apps[at] = app, view->icons[at] = null, view->icon_seqs[at] = 0;
        marks[at] = mark, lengths[at] = compositor_title_length(title, TASKBAR_PIN_TITLE), rows[at] = TASKBAR_GHOST;
}

// The tasks of the pinned programs that have no window, put after the windows' in a view. How many were added; only the first band has them.
static unsigned int taskbar_pins_tabs(struct taskbar_view *view, unsigned int room, unsigned char *marks, unsigned char *lengths, unsigned int *rows)
{
        unsigned int added = 0;

        taskbar_pins.ghosts = 0;
        if (compositor.band_at == 0 && ui_apps_fresh((b64)ui_kit_time()))
                taskbar_pins_read();
        if (compositor.band_at != 0 || !compositor.table)
                return 0;
        for (int each = 0; each < taskbar_starting.count; each++)
                taskbar_starting.pinned[each] = false;
        for (int each = 0; each < taskbar_pins.count && added < room; each++)
        {
                const struct taskbar_pin *pin = &taskbar_pins.pin[each];
                unsigned int at = view->count + added;

                if (taskbar_app_showing(pin->key, pin->app))
                        continue;
                taskbar_ghost_add(view, marks, lengths, rows, at, pin->title, pin->app,
                                  (unsigned char)(COMPOSITOR_TAB_AWAY | (taskbar_pin_starting(pin) ? COMPOSITOR_TAB_STARTING : 0)));
                taskbar_pins.ghost_tab[taskbar_pins.ghosts] = (int)at, taskbar_pins.ghost_pin[taskbar_pins.ghosts++] = each;
                added++;
        }
        for (int each = 0; each < taskbar_starting.count && added < room; each++)
                if (!taskbar_starting.pinned[each])
                        taskbar_ghost_add(view, marks, lengths, rows, view->count + added++, taskbar_starting.title[each],
                                          taskbar_starting.id[each], COMPOSITOR_TAB_AWAY | COMPOSITOR_TAB_STARTING);
        return added;
}

static int taskbar_pin_of_tab(int tab)
{
        for (int each = 0; each < taskbar_pins.ghosts; each++)
                if (taskbar_pins.ghost_tab[each] == tab)
                        return taskbar_pins.ghost_pin[each];
        return -1;
}

static void taskbar_pin_start(int tab, positive now)
{
        int which = taskbar_pin_of_tab(tab);

        if (which < 0 || (taskbar_pins.started[which] && now - taskbar_pins.started[which] < TASKBAR_PIN_AGAIN))
                return;
        for (unsigned int attempt = 0; attempt < 4; attempt++)
        {
                const struct taskbar_pin *pin = &taskbar_pins.pin[which];
                char path[96], name[32] = "pin_start_";
                positive number = ((now / 1000000ull) & 0x7fffffu) << 6 | (taskbar_pins.runs++ & 63u), at;
                bipolar handle;

                taskbar_path_num(path, (int)sizeof(path), taskbar_run_root, "/pin.", number, "");
                handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
                if (handle < 0)
                        continue;
                for (int word = 2; word < pin->count; word++)
                {
                        const char *text = taskbar_pin_word(pin, word);

                        system_write_all((b32)handle, text, string_length((string_address)text));
                        system_write_all((b32)handle, "\n", 1);
                }
                system_close(handle);
                at = 10 + positive_into_string((p8 *)name + 10, number);
                name[at++] = ' ';
                name[at + positive_into_string((p8 *)name + at, (positive)which)] = 0;
                taskbar_pins.started[which] = now;
                windows_starting_set(compositor.table, pin->app, now);
                compositor_script_say(name);
                return;
        }
}

static int taskbar_tab_row_is(int row, unsigned int pid)
{
        return row >= 0 && row < COMPOSITOR_ROWS && compositor.table && compositor.showing[row] &&
               __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE) == pid;
}

// The menu of a tab, beside the tab: a pinned ghost has Unpin alone; a window has Exit, Force stop, Pin/Unpin, Uninstall.
static int taskbar_tab_menu_open(const struct taskbar_view *view, int tab)
{
        int rect[4], row, root, members, windows, ghost, which, pinned = -1, pin_id = 0;
        unsigned int pid = 0;
        const char *pin_label = null;

        if (taskbar.editing || !view->rows || tab < 0 || tab >= (int)view->count)
                return 0;
        ghost = view->rows[tab] == TASKBAR_GHOST;
        row = ghost ? -1 : (int)view->rows[tab];
        if (ghost ? taskbar_pin_of_tab(tab) < 0 : (row < 0 || row >= COMPOSITOR_ROWS || !compositor.table))
                return 0;
        if (!ghost)
                pid = __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE);
        if (!taskbar_menu_begin(view, tab, row, pid, rect))
                return 0;
        memory_zero(&taskbar_menu.pin, sizeof(taskbar_menu.pin));
        if (ghost)
        {
                const struct ui_menu_spec specs[] = {{WORDS(taskbar_tab_unpin), null, 0, TASKBAR_MENU_UNPIN, false}};

                string_copy_bounded((p8 *)taskbar_menu.pin.app, (string_address)taskbar_pins.pin[taskbar_pin_of_tab(tab)].app, sizeof(taskbar_menu.pin.app));
                return taskbar_menu_build(view, rect, specs, 1);
        }
        members = taskbar_stop_plan_for(pid, compositor.app_id[row], &root);
        windows = members ? taskbar_stop_rows(members) : 0;
        if (windows > 1)
                language_format(taskbar_menu.stop, sizeof(taskbar_menu.stop), WORDS(taskbar_tab_stop_windows), (long)windows);
        which = ui_apps_match(compositor.app_id[row]);
        for (int each = 0; each < taskbar_pins.count && pinned < 0; each++)
                if (taskbar_pin_runs(&taskbar_pins.pin[each], compositor.app_id[row]))
                        pinned = each;
        if (pinned >= 0)
        {
                string_copy_bounded((p8 *)taskbar_menu.pin.app, (string_address)taskbar_pins.pin[pinned].app, sizeof(taskbar_menu.pin.app));
                pin_label = WORDS(taskbar_tab_unpin), pin_id = TASKBAR_MENU_UNPIN;
        }
        else if (which >= 0 && taskbar_pins.count < SETTING_PINS_ENTRIES && taskbar_pin_of(which, &taskbar_menu.pin))
                pin_label = WORDS(taskbar_tab_pin), pin_id = TASKBAR_MENU_PIN;
        else
                memory_zero(&taskbar_menu.pin, sizeof(taskbar_menu.pin));
        {
                const struct ui_menu_spec specs[] = {
                    {WORDS(taskbar_tab_exit), null, 0, TASKBAR_MENU_EXIT, false},
                    {windows > 1 ? taskbar_menu.stop : WORDS(taskbar_tab_stop), null, 0, TASKBAR_MENU_STOP, !members},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {pin_label, null, 0, pin_id, !pin_label},
                    {WORDS(taskbar_tab_uninstall), null, 0, TASKBAR_MENU_UNINSTALL, !(which >= 0 && ui_apps_uninstallable(which))},
                };

                return taskbar_menu_build(view, rect, specs, (int)(sizeof(specs) / sizeof(specs[0])));
        }
}

// What a chosen item of a tab's menu does: Unpin to the program the menu was opened for; the rest to the window it was opened for if that
// window is still there and still the program's it was.
static void taskbar_tab_chose(int id)
{
        int row = taskbar_menu.row, which;
        unsigned int pid = taskbar_menu.pid;

        if (id == TASKBAR_MENU_UNPIN)
        {
                taskbar_pin_keep(ui_apps_find(taskbar_menu.pin.app), false);
                return;
        }
        if (!taskbar_tab_row_is(row, pid))
                return;
        which = ui_apps_match(compositor.app_id[row]);
        switch (id)
        {
        case TASKBAR_MENU_EXIT:
                compositor_close(row);
                break;
        case TASKBAR_MENU_STOP:
                taskbar_stop_run_for(pid, compositor.app_id[row]);
                break;
        case TASKBAR_MENU_PIN:
                if (taskbar_menu.pin.app[0] && which >= 0 && which == ui_apps_find(taskbar_menu.pin.app))
                        taskbar_pin_keep(which, true);
                break;
        case TASKBAR_MENU_UNINSTALL:
                if (which >= 0 && ui_apps_uninstallable(which))
                {
                        taskbar_pin_keep(which, false);
                        ui_apps_uninstall(compositor_settings_root, which, ui_faces()->face, ui_faces()->bold);
                }
                break;
        default:
                break;
        }
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
        int which;

        if (!taskbar.editing || !taskbar_hit_raw(view, px, py, (int[2]){0, 0}, &which))
                return TASKBAR_HIT_NONE;
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
