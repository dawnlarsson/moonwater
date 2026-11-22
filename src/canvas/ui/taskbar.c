/* Taskbar layout, hit test, damage, painting, menus, force-stop, pins, and edit mode (interface declared in ui.c). */

#define TASKBAR_MOON_SPIN 60
#define TASKBAR_MOON_STEPS 5
#define TASKBAR_MOON_IDLE (120ull * 1000000000ull)
#define TASKBAR_MOON_GROW 12
#define TASKBAR_MOON_GROW_MS 180
#define TASKBAR_EDGE_KEEP 60
#define TASKBAR_SLIDE_NS UI_WINDOW_NS
#define TASKBAR_ICON 16
#define TASKBAR_ICON_GAP UI_GAP2
#define TASKBAR_TAB_CHUNK 256
#define TASKBAR_LIT_MS 350
#define TASKBAR_LIT_FADE_MS 200
#define TASKBAR_MOON_HALO 45
#define TASKBAR_MOON_HALO_ALPHA 0.5
#define TASKBAR_MOON_REST_GLOW 30
#define TASKBAR_MOON_KEPT (32ul << 20)
#define TASKBAR_MOON_BAKES 2
#define TASKBAR_PIN_TITLE 32
#define TASKBAR_PIN_AGAIN (3ull * 1000000000ull)
#define TASKBAR_STARTING_FRAME 33000000ull

static struct taskbar_state
{
        int stale;
        unsigned int minute, language, position_default, position;
        int all;
        char battery[BATTERY_NAME];
        int battery_item, battery_percent, battery_charging, edge, tray_custom;
        char tray_format[SETTING_TRAY_MOST + 1], tray_text[SETTING_TRAY_MOST * 4];
        int tray_chars, launcher, hide_fullscreen;
        b64 shift, slide_from, slide_to;
        positive slide_at;
        int volume_item, volume_percent, volume_muted, volume_flash, mic_item, mic_muted, notices_item;
        unsigned int notices_severity;
        int scale, padding, glides, moon_zone, tasks_zone, tasks_right;
        unsigned int tray_before;
        unsigned char order[3];
        int editing, clock_tray, clock_tab, clock_none;
        char clock_format[SETTING_TRAY_MOST + 1], clock_text[SETTING_TRAY_MOST * 4];
        int clock_chars, clock_12h, clock_bulge, moon_rest, moon_ink, clock_read;
        long long moon_at;
        int spin, separate, edge_in, moon_hover;
        positive moon_seen;
        long long moon_step;
        b64 moon_turn;
} taskbar = {1, 0, 0, TASKBAR_BOTTOM, TASKBAR_BOTTOM, 0, {0}, 0, 0, 0,
             .scale = TASKBAR_SCALE_STANDARD, .padding = TASKBAR_PADDING_STANDARD, .glides = 1,
             .spin = TASKBAR_MOON_SPIN, .moon_zone = TASKBAR_ZONE_CENTRE, .tasks_zone = TASKBAR_ZONE_SPLIT,
             .tray_before = 1u << 1 | 1u << 2, .clock_bulge = 60,
             .order = {TASKBAR_PILL_MOON, TASKBAR_PILL_TASKS, TASKBAR_PILL_TRAY}};

_Static_assert(TASKBAR_GAP == UI_GAP1, "the gap between the band's items is the kit's gap between pieces that go together");
_Static_assert(TASKBAR_TAB_BITS + 2 <= 64, "a redo bit a tab, the tray's and the launcher's, in a word");

static unsigned int taskbar_tint[TASKBAR_TAB_CHUNK];
static const unsigned int taskbar_clear[TASKBAR_TAB_CHUNK];

static void compositor_meet(int *into, const int *other);
static void compositor_union(int *into, const int *other);
static const unsigned int *compositor_grain_row(unsigned int light, int x, int y);
static unsigned int compositor_vivid(unsigned int pixel);
static inline b32 language_rtl(void);
static inline INLINE CONST int taskbar_inset(int scale);
static unsigned int taskbar_accent(void);
static void taskbar_pins_read(void);
static bool taskbar_tab_full(const struct taskbar_view *view, int at);
static int taskbar_format_render(const char *format, char *into, int room);

static inline INLINE CONST int taskbar_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline INLINE CONST int taskbar_vertical(unsigned int pos) { return pos == TASKBAR_LEFT || pos == TASKBAR_RIGHT; }
static inline INLINE PURE int taskbar_scaled(int scale) { return scale * taskbar.scale / 100; }
static inline INLINE PURE int taskbar_thick(int scale) { return COMPOSITOR_PANEL * taskbar_scaled(scale) / 100; }
static inline INLINE CONST int taskbar_inset(int scale) { int n = 2 * scale / 100; return n < 1 ? 1 : n; }
static inline INLINE PURE b64 taskbar_tween_goal(const struct ui_tween address_to t) { return t->pending ? t->want : t->to; }
static inline INLINE PURE int taskbar_tween_moving(const struct ui_tween address_to t, positive now) { return t->pending || (t->start && (b64)now - t->start < t->duration); }

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

static inline INLINE PURE int taskbar_fits(const struct ui_node address_to outer, const struct ui_node address_to inner)
{
        return inner && inner->x >= outer->x && inner->y >= outer->y &&
               inner->x + inner->w <= outer->x + outer->w && inner->y + inner->h <= outer->y + outer->h;
}

static unsigned int address_to taskbar_remap(unsigned int address_to old, positive old_bytes, positive bytes)
{
        if (old)
                compositor_call(syscall(munmap), (positive)old, old_bytes, 0, 0, 0, 0);
        long got = ui_map(bytes);
        return got < 0 && got > -4096 ? null : (unsigned int address_to)got;
}

static inline INLINE void taskbar_blit_square(unsigned int *restrict dst, long dst_pitch, const unsigned int *restrict src, long src_pitch, int size)
{
        if (size > 0)
                memory_copy_rect_u32(dst, (positive)dst_pitch, src, (positive)src_pitch, (positive)size, (positive)size);
}

static const char *taskbar_line_value(const char *text, const char *key, char sep)
{
        positive size = string_length((string_address)key);

        for (string_address line = (string_address)text; line && *line;)
        {
                if (!string_compare_max(line, (string_address)key, size) && (!sep || line[size] == sep))
                        return (const char *)(line + size + (sep != 0));
                string_address nl = string_first_of(line, '\n');
                line = nl ? nl + 1 : null;
        }
        return null;
}

static unsigned int taskbar_minute_now(void)
{
        tm broken;

        if (taskbar_broken_time(0, &broken))
                return (unsigned int)((((broken.tm_year & 0x3ff) * 366 + broken.tm_yday) * 1440) + broken.tm_hour * 60 + broken.tm_min);
        return (unsigned int)(taskbar_now_sec(0) / 60);
}

static int taskbar_clock_stale(void)
{
        unsigned int language_now = compositor.table ? compositor.table->language : 0;
        return taskbar_minute_now() != taskbar.minute || language_now != taskbar.language;
}

static int taskbar_edge_px(const struct taskbar_view *view)
{
        int scale = taskbar_scaled(view->scale), thick = taskbar_thick(view->scale), inset = taskbar_inset(scale);
        int edge = taskbar.edge_in * scale / 100, least = -((thick - 2 * inset) * (100 - TASKBAR_EDGE_KEEP) / 100 + inset);
        return edge < least ? least : edge;
}

#ifdef COMPOSITOR_ROWS
#define taskbar_mc_push_px() compositor_mc_push_for(compositor.band_at)
#else
#define taskbar_mc_push_px() 0
#endif

static int taskbar_off(const struct taskbar_view *view, b64 shift)
{
        int thick = taskbar_thick(view->scale), in = taskbar_edge_px(view);
        int off = (int)(((long long)(thick + in) * (long long)shift) / (long long)UI_ONE) - in;
        off -= taskbar_mc_push_px();
        return view->position == TASKBAR_BOTTOM || view->position == TASKBAR_RIGHT ? off : -off;
}

static b64 taskbar_shift_at(positive now)
{
        if (!taskbar.slide_at)
                return taskbar.slide_to;
        if (now <= taskbar.slide_at)
                return taskbar.slide_from;
        b64 done = ui_bezier_at(&ui_ease, ui_progress((b64)(now - taskbar.slide_at), ui_paced(TASKBAR_SLIDE_NS)));
        return (b64)((long long)taskbar.slide_from + ((long long)taskbar.slide_to - (long long)taskbar.slide_from) * (long long)done / (long long)UI_ONE);
}

static int taskbar_slide_now(int fullscreen, positive now, int animate, b64 *before)
{
        b64 target = taskbar.hide_fullscreen && fullscreen ? UI_ONE : 0, was = taskbar.shift;

        *before = was;
        taskbar.glides = animate != 0;
        if (target != taskbar.slide_to)
        {
                taskbar.slide_from = was, taskbar.slide_to = target;
                taskbar.slide_at = animate ? now : 0;
        }
        taskbar.shift = taskbar_shift_at(now);
        return taskbar.shift != was;
}

static int taskbar_sliding(positive now) { return taskbar.slide_at && now > taskbar.slide_at && now - taskbar.slide_at < (positive)ui_paced(TASKBAR_SLIDE_NS); }
static b64 taskbar_shift(void) { return taskbar.shift; }

static int taskbar_text(int high, int pad, int characters)
{
        int wanted = characters * high * 308 / 1000 + 2 * pad;
        return wanted < high ? high : wanted;
}

static int taskbar_icon_side(int high, int scale)
{
        int side = TASKBAR_ICON * scale / 100, room = high - 4 * scale / 100;
        return taskbar_clamp(side > room ? room : side, 1, UI_ICON_MOST);
}

static int taskbar_tab_length(int high, int pad, int scale, int characters)
{
        int wanted = 2 * pad + taskbar_icon_side(high, scale) + (characters > 0 ? TASKBAR_ICON_GAP * scale / 100 + characters * high * 308 / 1000 : 0);
        return wanted < high ? high : wanted;
}

#define TASKBAR_TRAY_CELLS(CELL)                                                                                          \
        CELL(TASKBAR_NOTICES, taskbar.notices_item, (UI_DOT * scale + 50) / 100, taskbar_notices_cell(view, t, n))       \
        CELL(TASKBAR_MIC, taskbar.mic_item, glyph, taskbar_mic_cell(view, t, n))                                         \
        CELL(TASKBAR_VOLUME, taskbar.volume_item, glyph, taskbar_volume_cell(view, t, n, ink, ground))                   \
        CELL(-4, taskbar.battery_item && !taskbar.tray_custom, 0, taskbar_say(view, t, n, text, ink, ground))            \
        CELL(-3, taskbar.clock_tray && !taskbar.clock_tab && !taskbar.tray_custom, 0, taskbar_say(view, t, n, taskbar_clock_said(clock), ink, ground))

static struct ui_node address_to taskbar_open(struct ui_tree address_to t, int which, int m, int gap, int length, int high)
{
        struct ui_node address_to n = ui_tree_open(t);

        n->id = which + 1, n->dir = m, n->margin[2 * m] = gap;
        n->size[m] = (struct ui_size){UI_FIXED, length, 0, UI_TREE_MOST, 0};
        n->size[!m] = (struct ui_size){UI_FIXED, high, 0, UI_TREE_MOST, 0};
        return n;
}

static fn taskbar_leaf(struct ui_tree address_to t, int which, int m, int gap, int length, int high, int mark)
{
        struct ui_node address_to n = taskbar_open(t, which, m, gap, length, high);

        if (mark > 0)
        {
                n->justify = n->align = UI_CENTER;
                struct ui_node address_to c = ui_tree_open(t);
                c->size[0] = c->size[1] = (struct ui_size){UI_FIXED, mark, 0, UI_TREE_MOST, 0};
                ui_tree_close(t);
        }
        else if (mark < 0)
                n->justify = UI_CENTER;
        ui_tree_close(t);
}

static int taskbar_item_of(int slot)
{
        return slot == TASKBAR_NOTICES ? 0 : slot == TASKBAR_MIC ? 1 : slot == TASKBAR_VOLUME ? 2 : slot == -4 ? 3 : slot == -9 ? 4 : slot == -3 ? 5 : -1;
}

static int taskbar_before(int slot)
{
        int item = taskbar_item_of(slot);
        return item >= 0 && (taskbar.tray_before >> item & 1);
}

#ifdef COMPOSITOR_ROWS
#define taskbar_tray_items_count() compositor_tray_count()
#define taskbar_tray_items_gen() compositor_tray_gen()
#define taskbar_tray_items_pixels(i) compositor_tray_pixels(i)
#define taskbar_spaces_count() compositor_spaces_count_on(compositor.band_at)
#define taskbar_spaces_active() compositor_spaces_active_index(compositor.band_at)
#define taskbar_spaces_kind(i) compositor_spaces_kind_at(compositor.band_at, (i))
#define taskbar_spaces_gen() compositor_spaces_gen()
#else
#define taskbar_tray_items_count() 0
#define taskbar_tray_items_gen() 0
#define taskbar_tray_items_pixels(i) ((const unsigned int *)0)
#define taskbar_spaces_count() 0
#define taskbar_spaces_active() 0
#define taskbar_spaces_kind(i) 0
#define taskbar_spaces_gen() 0u
#endif

struct taskbar_metrics { int m, scale, pad, high, side, icon_gap, apart, glyph, square, cell, clocked, text_w; };

static int taskbar_tray_lay(struct ui_tree address_to t, int before, const struct taskbar_metrics *M, int margin)
{
        int scale = M->scale, glyph = M->glyph;
        int spaces = !before ? taskbar_spaces_count() : 0;
        if (spaces > TASKBAR_SPACE_MAX)
                spaces = TASKBAR_SPACE_MAX;
        int space_add = spaces > 0 && spaces < TASKBAR_SPACE_MAX ? 1 : 0;
        int xtray = !before ? taskbar_tray_items_count() : 0;
        if (xtray > TASKBAR_TRAY_ITEM_MAX)
                xtray = TASKBAR_TRAY_ITEM_MAX;
        int length = (spaces + space_add + (xtray > 0 ? xtray : 0)) * M->square;
        const struct { int slot, w, mark; } cells[] = {
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
        for (int i = 0; i < spaces && i < TASKBAR_SPACE_MAX; i++)
                taskbar_leaf(t, TASKBAR_SPACE_BASE - i, M->m, 0, M->square, M->high, 0);
        if (space_add)
                taskbar_leaf(t, TASKBAR_SPACE_ADD, M->m, 0, M->square, M->high, 0);
        for (int i = 0; i < xtray && i < TASKBAR_TRAY_ITEM_MAX; i++)
                taskbar_leaf(t, TASKBAR_TRAY_ITEM_BASE - i, M->m, 0, M->square, M->high, M->glyph);
        for (int i = 0; i < ncells; i++)
                if (cells[i].w && taskbar_before(cells[i].slot) == before)
                        taskbar_leaf(t, cells[i].slot, M->m, 0, cells[i].w, M->high, cells[i].mark);
        ui_tree_close(t);
        return length;
}

static int taskbar_strip_from[2], taskbar_strip_ends[2][2], taskbar_strip_shown[2], taskbar_strip_listed[2];
static int taskbar_strip_follow = 1, taskbar_strip_lit[2] = {-1, -1};
static struct ui_grown taskbar_lay_room;

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
                taskbar_leaf(t, TASKBAR_STRIP_BACK, M->m, taskbar.separate ? margin : 0, bare, M->high, -1), margin = M->apart;
        for (int k = 0; k < listed; k++)
        {
                int at = list[k];
                struct ui_node address_to n = taskbar_open(t, at, M->m, taskbar.separate ? (k ? M->apart : margin) : 0, widths[at], M->high), address_to p;
                bool full = taskbar_tab_full(view, at);

                n->align = UI_CENTER;
                if (titled[at])
                        n->pad[2 * M->m] = full ? 0 : M->pad;
                else
                        n->justify = UI_CENTER;
                p = ui_tree_open(t);
                p->size[0] = p->size[1] = (struct ui_size){UI_FIXED, full ? (M->high < widths[at] ? M->high : widths[at]) : M->side, 0, UI_TREE_MOST, 0};
                ui_tree_close(t);
                if (titled[at])
                {
                        p = ui_tree_open(t);
                        p->margin[2 * M->m] = M->icon_gap, p->margin[2 * M->m + 1] = M->pad;
                        p->size[M->m] = (struct ui_size){UI_GROW, 1, 0, UI_TREE_MOST, 0};
                        p->size[!M->m] = (struct ui_size){UI_FIXED, M->high, 0, UI_TREE_MOST, 0};
                        ui_tree_close(t);
                }
                ui_tree_close(t);
        }
        if (taskbar_strip_ends[strip][1])
                taskbar_leaf(t, TASKBAR_STRIP_ON, M->m, taskbar.separate ? M->apart : 0, bare, M->high, -1);
        if (!taskbar.separate)
                ui_tree_close(t);
}

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
        if (lit >= 0)
        {
                int id = view->rows ? (int)view->rows[list[lit]] : list[lit];
                if (id != taskbar_strip_lit[strip])
                        taskbar_strip_lit[strip] = id, taskbar_strip_follow = 1;
        }
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

static b32 taskbar_strip_step(int by)
{
        b32 moved = 0;

        for (int s = 0; s < 2; s++)
        {
                int to = taskbar_clamp(taskbar_strip_from[s] + by, 0, taskbar_strip_listed[s] - taskbar_strip_shown[s]);
                moved |= to != taskbar_strip_from[s], taskbar_strip_from[s] = to;
        }
        taskbar_strip_follow = 0;
        return moved;
}

static b32 taskbar_strip_page(int dir)
{
        int page = taskbar_strip_shown[0] > taskbar_strip_shown[1] ? taskbar_strip_shown[0] : taskbar_strip_shown[1];
        return taskbar_strip_step(dir * (page > 1 ? page : 1));
}

static int taskbar_tasks_fit(const struct taskbar_view *view, int strip, int *list, int *listed_at, int room, const struct taskbar_metrics *M,
                             int longest, int *widths, int *titled)
{
        int sum = 0, alone = taskbar_tab_length(M->high, M->pad, M->scale, 7), bare = M->side + 2 * M->pad;
        int listed = taskbar_strip(view, strip, list, *listed_at, room, M->side + 2, M->apart);
        int ends = taskbar_strip_ends[strip][0] + taskbar_strip_ends[strip][1];
        int tabs = (taskbar.separate && listed + ends > 1 ? (listed + ends - 1) * M->apart : 0) + ends * bare;

        *listed_at = listed;
        room -= tabs;
        for (int k = 0; k < listed; k++)
        {
                int at = list[k], w = view->lengths ? taskbar_tab_length(M->high, M->pad, M->scale, view->lengths[at]) : longest;
                widths[at] = w > longest ? longest : w, sum += widths[at];
        }
        int share = listed && room > 0 ? room / listed : 0;
        for (int k = 0; k < listed; k++)
        {
                int at = list[k];
                widths[at] = sum > room && widths[at] > share ? share : widths[at];
                titled[at] = (!view->lengths || view->lengths[at] > 0) && (sum <= room || share >= alone);
                if (!titled[at] && widths[at] > bare)
                        widths[at] = bare;
                tabs += widths[at];
        }
        return tabs;
}

static inline int taskbar_pill_gap(int *first, int apart) { int g = *first ? 0 : apart; *first = 0; return g; }

static fn taskbar_lay(struct ui_tree address_to t, const struct taskbar_view *view)
{
        static const int just[2][3] = {{UI_START, UI_CENTER, UI_END}, {UI_END, UI_CENTER, UI_START}};
        ui_tree_ready(t);
        int m = taskbar_vertical(view->position), c = !m, far = view->position == TASKBAR_BOTTOM || view->position == TASKBAR_RIGHT;
        int scale = taskbar_scaled(view->scale), length = m ? view->height : view->width, thick = taskbar_thick(view->scale);
        int gap = TASKBAR_GAP * scale / 100, apart = 2 * gap, pad = taskbar.padding * scale / 100;
        int high = thick - 2 * taskbar_inset(scale), count = (int)view->count, split = taskbar.tasks_zone == TASKBAR_ZONE_SPLIT;
        int moon = thick <= length - 2 * gap ? thick : 0, most = length / 2 - moon - apart, inner = length - 2 * gap;
        int trays[2], zones[3], *list[2], listed[2] = {0, 0}, tabs[2] = {0, 0}, moon_zone = split ? TASKBAR_ZONE_CENTRE : taskbar.moon_zone;

        count = count > 0 && ui_grown_room(&taskbar_lay_room, 4 * sizeof(int), UI_TREE_GROWN, count) ? count : 0;
        int *widths = (int *)taskbar_lay_room.at, *titled = widths + count;
        list[0] = titled + count, list[1] = list[0] + count;
        high = high < 1 ? 1 : high;
        struct taskbar_metrics M = {m, scale, pad, high, taskbar_icon_side(high, scale), TASKBAR_ICON_GAP * scale / 100, apart,
                                    high * 64 / 100, high * 64 / 100 + 2 * pad, taskbar_text(high, pad, 4),
                                    taskbar_text(high, pad, taskbar.clock_chars > 0 ? taskbar.clock_chars : 5),
                                    taskbar.tray_custom ? taskbar_text(high, pad, taskbar.tray_chars > 0 ? taskbar.tray_chars : 1) : 0};
        int tab = taskbar.clock_tab ? M.clocked : 0, done = taskbar.editing ? taskbar_text(high, pad, 4) : 0;
        int longest = 8 * thick + M.side + M.icon_gap;

        for (int b = 0; b < 2; b++)
                trays[b] = taskbar_tray_lay(null, b, &M, 0);
        if (trays[0] + trays[1] > most && most > 0)
        {
                int over = trays[0] + trays[1] - most;
                if (taskbar.tray_custom && M.text_w - over >= high)
                        M.text_w -= over;
                else
                {
                        int sp = taskbar_spaces_count();
                        if (sp > TASKBAR_SPACE_MAX) sp = TASKBAR_SPACE_MAX;
                        int sp_add = sp > 0 && sp < TASKBAR_SPACE_MAX ? 1 : 0;
                        int xt = taskbar_tray_items_count();
                        if (xt > TASKBAR_TRAY_ITEM_MAX) xt = TASKBAR_TRAY_ITEM_MAX;
                        int items = (taskbar.notices_item != 0) + (taskbar.mic_item != 0) + (taskbar.volume_item != 0) +
                                    (taskbar.battery_item && !taskbar.tray_custom) + (taskbar.tray_custom != 0) + (taskbar.clock_tray != 0) +
                                    sp + sp_add + (xt > 0 ? xt : 0);
                        M.square = M.cell = M.text_w = M.clocked = items ? most / items : 0;
                }
                for (int b = 0; b < 2; b++)
                        trays[b] = taskbar_tray_lay(null, b, &M, 0);
        }
        if (most <= 0 || trays[0] + trays[1] > most)
                trays[0] = trays[1] = 0;
        if (done && done + apart + moon > inner / 2)
                done = 0;
        if (split)
        {
                int half = (inner - moon) / 2, per = 1 << 30;
                for (int i = 0; i < count; i++)
                {
                        int right = (taskbar.tasks_right ? i + 1 : i) % 2 == 0;
                        list[right][listed[right]++] = i;
                }
                for (int k = 0; k < listed[0] / 2; k++)
                {
                        int keep = list[0][k];
                        list[0][k] = list[0][listed[0] - 1 - k], list[0][listed[0] - 1 - k] = keep;
                }
                if (half - 2 * apart - trays[1] < 0)
                        trays[1] = 0;
                if (half - 2 * apart - trays[0] - (done ? done + apart : 0) < 0)
                        trays[0] = 0;
                int rooms[2] = {half - apart - (trays[1] ? trays[1] + apart : 0),
                                half - apart - (trays[0] ? trays[0] + apart : 0) - (done ? done + apart : 0) - (tab ? tab + apart : 0)};
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
                int ends = done ? done + apart : 0, pills = (trays[0] > 0) + (trays[1] > 0) + (tab > 0);
                int group = moon_zone != taskbar.tasks_zone && taskbar.tasks_zone == TASKBAR_ZONE_CENTRE
                                ? inner - 2 * (moon + apart > ends ? moon + apart : ends)
                                : moon_zone != taskbar.tasks_zone && moon_zone == TASKBAR_ZONE_CENTRE
                                      ? length / 2 - gap - moon / 2 - apart - (taskbar.tasks_zone == TASKBAR_ZONE_END ? ends : 0)
                                      : inner - moon - apart - ends;
                for (int i = 0; i < count; i++)
                        list[1][listed[1]++] = taskbar.tasks_right ? count - 1 - i : i;
                taskbar_strip_ends[0][0] = taskbar_strip_ends[0][1] = 0;
                tabs[1] = taskbar_tasks_fit(view, 1, list[1], &listed[1], group - trays[0] - trays[1] - tab - pills * apart, &M, longest, widths, titled);
                for (int z = TASKBAR_ZONE_START; z <= TASKBAR_ZONE_END; z++)
                {
                        int parts = (moon_zone == z) + (z == TASKBAR_ZONE_END && done);
                        zones[z] = (moon_zone == z ? moon : 0) + (z == TASKBAR_ZONE_END ? done : 0);
                        if (taskbar.tasks_zone == z)
                                zones[z] += tabs[1] + trays[0] + trays[1] + tab, parts += (tabs[1] > 0) + pills;
                        zones[z] += (parts > 1 ? parts - 1 : 0) * apart;
                }
                zones[TASKBAR_ZONE_CENTRE] = zones[TASKBAR_ZONE_CENTRE] < inner ? zones[TASKBAR_ZONE_CENTRE] : inner;
                int rest = inner - zones[TASKBAR_ZONE_CENTRE];
                if (zones[TASKBAR_ZONE_START] > rest / 2)
                        zones[TASKBAR_ZONE_END] = rest - zones[TASKBAR_ZONE_START];
                else if (zones[TASKBAR_ZONE_END] > rest / 2)
                        zones[TASKBAR_ZONE_START] = rest - zones[TASKBAR_ZONE_END];
                else
                        zones[TASKBAR_ZONE_START] = rest / 2, zones[TASKBAR_ZONE_END] = rest - rest / 2;
        }
        ui_tree_begin(t);
        struct ui_node address_to n = ui_tree_open(t);
        n->dir = c, n->justify = far ? UI_END : UI_START, n->size[0].kind = n->size[1].kind = UI_GROW;
        n->x = view->x, n->y = view->y, *ui_tree_at(n, c) += taskbar_off(view, taskbar.shift);
        n = ui_tree_open(t);
        n->dir = m, n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0}, n->pad[2 * m] = n->pad[2 * m + 1] = gap;
        for (int z = TASKBAR_ZONE_START; z <= TASKBAR_ZONE_END; z++)
        {
                int first = 1;
                n = ui_tree_open(t);
                n->id = taskbar.editing ? TASKBAR_ZONE - z + 1 : 0;
                n->dir = m, n->align = UI_CENTER, n->justify = just[split][z];
                n->size[c] = (struct ui_size){UI_FIXED, thick, 0, UI_TREE_MOST, 0};
                n->size[m] = (struct ui_size){UI_FIXED, zones[z] > 0 ? zones[z] : 0, 0, UI_TREE_MOST, 0};
                if (split)
                {
                        if (z == TASKBAR_ZONE_START)
                        {
                                n->pad[2 * m + 1] = apart;
                                if (trays[1])
                                        taskbar_tray_lay(t, 1, &M, taskbar_pill_gap(&first, apart));
                                taskbar_tasks_lay(view, t, TASKBAR_TABS_LEFT, 0, list[0], listed[0], widths, titled, &M, taskbar_pill_gap(&first, apart));
                        }
                        else if (z == TASKBAR_ZONE_CENTRE && moon)
                                taskbar_leaf(t, TASKBAR_LAUNCHER, m, 0, moon, moon, moon);
                        else if (z == TASKBAR_ZONE_END)
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
                                if (pill == TASKBAR_PILL_MOON && moon_zone == z && moon)
                                        taskbar_leaf(t, TASKBAR_LAUNCHER, m, taskbar_pill_gap(&first, apart), moon, moon, moon);
                                if (pill == TASKBAR_PILL_TASKS && taskbar.tasks_zone == z)
                                {
                                        if (trays[1])
                                                taskbar_tray_lay(t, 1, &M, taskbar_pill_gap(&first, apart));
                                        if (tabs[1] > 0)
                                                taskbar_tasks_lay(view, t, TASKBAR_TABS, 1, list[1], listed[1], widths, titled, &M, taskbar_pill_gap(&first, apart));
                                }
                                if (pill == TASKBAR_PILL_TRAY && taskbar.tasks_zone == z)
                                {
                                        if (trays[0])
                                                taskbar_tray_lay(t, 0, &M, taskbar_pill_gap(&first, apart));
                                        if (tab)
                                                taskbar_leaf(t, -3, m, taskbar_pill_gap(&first, apart), tab, high, 0);
                                }
                        }
                        if (z == TASKBAR_ZONE_END && done)
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
                if (!(t->node[at].w > 0 && t->node[at].h > 0 && taskbar_fits(&t->node[1], &t->node[at])))
                        t->node[at].id = 0, t->node[at].w = t->node[at].h = 0;
}

struct taskbar_key
{
        b64 shift;
        int width, height, scale, x, y, position, count, titled, own, padding, launcher, tray, cells, rtl, arrange, editing;
        unsigned int lengths;
        int strip[2], follow;
        unsigned int marks;
        int tray_gen;
        unsigned int spaces_gen;
        int mc_push;
        unsigned int apps_gen, pictures; // the registry read and the apps' pictures made: a task with a picture is laid out as one
};
_Static_assert(sizeof(struct taskbar_key) == 8 + 26 * 4, "a key is compared as bytes, so it has no padding");

struct taskbar_lit { struct ui_tween at, length, shown; int row; };

static struct taskbar_band
{
        struct taskbar_key key;
        struct ui_tree tree;
        struct taskbar_lit lit;
        struct ui_tween grow;
        int moon_damage[4], moon_own, glow_key[5], glow_shown;
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
                                  {taskbar_strip_from[0], taskbar_strip_from[1]}, taskbar_strip_follow, 0, taskbar_tray_items_gen(), taskbar_spaces_gen(), taskbar_mc_push_px(),
                                  ui_apps.generation, ui_apps_pictures_made};

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
                band->key = key, taskbar_lay(&band->tree, view);
        return &band->tree;
}

static struct ui_node address_to taskbar_node(const struct taskbar_view *view, int which) { return ui_tree_find(taskbar_tree(view), which + 1); }
static inline fn taskbar_rect(const struct ui_node address_to n, int *x, int *y, int *w, int *h) { *x = n->x, *y = n->y, *w = n->w, *h = n->h; }
static void taskbar_band(const struct taskbar_view *view, int *x, int *y, int *w, int *h) { taskbar_rect(&taskbar_tree(view)->node[1], x, y, w, h); }

static void taskbar_slide_rect(const struct taskbar_view *view, b64 before, int *x, int *y, int *w, int *h)
{
        int r[4], c = !taskbar_vertical(view->position), came = taskbar_off(view, before) - taskbar_off(view, taskbar.shift);

        taskbar_band(view, &r[0], &r[1], &r[2], &r[3]);
        r[c] += came < 0 ? came : 0, r[2 + c] += came < 0 ? -came : came;
        *x = r[0], *y = r[1], *w = r[2], *h = r[3];
}

static int taskbar_slot(const struct taskbar_view *view, int which, int *x, int *y, int *w, int *h)
{
        const struct ui_node address_to n = taskbar_node(view, which);
        if (n)
                taskbar_rect(n, x, y, w, h);
        return n != null;
}

static int taskbar_plate(const struct taskbar_view *view, int at, int *which, int *x, int *y, int *w, int *h)
{
        const struct ui_tree address_to t = taskbar_tree(view);

        for (int z = t->count > 1 ? t->node[1].first : -1; z >= 0; z = t->node[z].next)
                for (int k = t->node[z].first; k >= 0; k = t->node[k].next)
                        if (t->node[k].id && !at--)
                        {
                                *which = t->node[k].id - 1;
                                taskbar_rect(&t->node[k], x, y, w, h);
                                if (*which == TASKBAR_LAUNCHER)
                                {
                                        int more = taskbar_moon_more(view);
                                        *x -= more, *y -= more, *w += 2 * more, *h += 2 * more;
                                }
                                return 1;
                        }
        return 0;
}

static int taskbar_hit_raw(const struct taskbar_view *view, int px, int py, const int grown[2], int *which)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int at = ui_tree_hit(t, px, py, grown);
        return at < 0 ? 0 : (*which = t->node[at].id - 1, 1);
}

static int taskbar_hit_reach(const struct taskbar_view *view, int px, int py, int reach)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        int grown[2] = {0, 0}, at, which;

        grown[!t->node[1].dir] = reach + taskbar_inset(taskbar_scaled(view->scale));
        if ((at = ui_tree_hit(t, px, py, grown)) < 0)
                return TASKBAR_HIT_NONE;
        which = t->node[at].id - 1;
        return which == -4 || which == -3 || which == -9 || which == TASKBAR_TRAY_BEFORE ? TASKBAR_TRAY
               : which == TASKBAR_TABS || which == TASKBAR_TABS_LEFT || (which <= TASKBAR_ZONE && which > TASKBAR_DONE && !taskbar.editing)
                     ? TASKBAR_HIT_NONE : which;
}

static int taskbar_anchor(const struct taskbar_view *view, int which, int *x, int *y, int *w, int *h)
{
        const struct ui_node address_to n = taskbar_node(view, which), address_to band = &taskbar_tree(view)->node[1];

        if (!n)
                return 0;
        taskbar_rect(n, x, y, w, h);
        if (band->dir)
                *x = band->x, *w = band->w;
        else
                *y = band->y, *h = band->h;
        return 1;
}

static unsigned long taskbar_bit(int which)
{
        return which == TASKBAR_LAUNCHER ? 1ul << (TASKBAR_TAB_BITS + 1) : which < 0 ? 1ul << TASKBAR_TAB_BITS : 1ul << (which % TASKBAR_TAB_BITS);
}

static inline int taskbar_slot_own(int which)
{
        return which != -4 && (which != -3 || taskbar.clock_tab) && which != TASKBAR_TABS &&
               which != TASKBAR_TABS_LEFT && !(which <= TASKBAR_ZONE && which > TASKBAR_DONE);
}
static inline int taskbar_is_moon_damage(const struct taskbar_band address_to own, const int rect[4]) { return own->moon_own && !memory_compare(rect, own->moon_damage, sizeof(own->moon_damage)); }

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
        for (int which = TASKBAR_STRIP_ON; which < (int)view->count; which++)
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
               : mark & COMPOSITOR_TAB_LIT ? COMPOSITOR_INK_BUTTON_LIT : compositor_ink_shadow();
}

static unsigned int taskbar_ink_on(unsigned int ground, unsigned int mark)
{
        unsigned int ink = window_contrast(ground);
        return mark & COMPOSITOR_TAB_LIT ? ink : window_mix(ground, ink, mark & COMPOSITOR_TAB_AWAY ? 110 : 170);
}

static struct ui taskbar_cell_ui;

// The band's pixels as a cell's ui: bw by bh, which ui_cell_line and ui_cell_turned hold a cell inside (b9ec9629c).
static struct ui address_to taskbar_cell(const struct taskbar_view *view, int bw, int bh)
{
        taskbar_cell_ui.pixels = view->pixels, taskbar_cell_ui.pitch = (unsigned long)bw, taskbar_cell_ui.scale = taskbar_scaled(view->scale);
        taskbar_cell_ui.width = bw, taskbar_cell_ui.height = bh, taskbar_cell_ui.bottom = bh;
        taskbar_cell_ui.engine = view->engine, taskbar_cell_ui.regular = view->face;
        return &taskbar_cell_ui;
}

static unsigned int taskbar_turn(unsigned int position)
{
        return !taskbar_vertical(position) ? 0 : (position == TASKBAR_LEFT) != (taskbar.edge != 0) ? FONT_TOPS_RIGHT : FONT_TOPS_LEFT;
}

static void taskbar_say(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, const char *text,
                        unsigned int ink, unsigned int ground)
{
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1];
        if (band->dir)
                ui_cell_turned(taskbar_cell(view, band->w, band->h), n->x - band->x, n->y - band->y, n->w, n->h, text, taskbar_turn(view->position), ink, ground);
        else
                ui_cell_line(taskbar_cell(view, band->w, band->h), n->x - band->x, n->y - band->y, n->w, n->h, text, ink, ground);
}

static void taskbar_band_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t)
{
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1];

        memory_zero(ui, sizeof(*ui));
        ui->pixels = view->pixels, ui->pitch = (unsigned long)band->w, ui->width = band->w, ui->height = band->h, ui->bottom = band->h;
        ui->scale = taskbar_scaled(view->scale), ui->scene = view->scene, ui->engine = view->engine, ui->regular = view->face;
}

static const struct ui_node address_to taskbar_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t,
                                                  const struct ui_node address_to n, int *x, int *y)
{
        taskbar_band_ui(ui, view, t);
        const struct ui_node address_to band = &t->node[1], address_to mark = &t->node[n->first];
        *x = mark->x - band->x, *y = mark->y - band->y;
        return mark;
}

static unsigned int taskbar_glass_tab(const struct taskbar_view *view, int gx, int gy, int gw, int bx, int by, int bw, int x, int y, int width, int height)
{
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
                        memory_saturate_u32(taskbar_tint, from + at, (positive)count);
                        memory_tint_u32(taskbar_tint, taskbar_tint, (positive)count, target, amount, 255, 0);
                        memory_glass_over_u32(into + at, taskbar_clear, taskbar_tint, (positive)count,
                                              compositor_grain_row(target != 0, (x - bx) + at, row - by));
                }
        }
        return window_mix(middle, target, amount);
}

static fn taskbar_glass_ui(struct ui *ui, const struct taskbar_view *view, const struct ui_tree address_to t, unsigned int ground)
{
        taskbar_band_ui(ui, view, t);
        ui->glass = 1;
        ui_inks(ui, ground);
}

static struct ui_box taskbar_face(const struct ui_tree address_to t, const struct ui_node address_to n, b64 at, b64 length)
{
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1];
        int x = n->x - band->x, y = n->y - band->y;
        struct ui_box face = {x * 256, y * 256, (x + n->w) * 256, (y + n->h) * 256, 0, 0};

        if (length > 0)
        {
                b32 from = (b32)(at >> (UI_Q - 8)), to = (b32)((at + length) >> (UI_Q - 8));
                if (band->dir)
                        face.y0 = from, face.y1 = to;
                else
                        face.x0 = from, face.x1 = to;
        }
        face.rx = face.ry = (n->w < n->h ? n->w : n->h) * 128;
        return face;
}

static fn taskbar_lit_to(struct taskbar_lit address_to lit, const struct ui_tree address_to t, const struct taskbar_view *view)
{
        ui_tree_ready(t);
        const struct ui_node address_to band = &t->node[1], address_to tab = null;
        b32 motion = ui_kit.motion, live = 0;
        int chosen = -1, m = band->dir;

        ui_kit.now = ui_kit_clock(), ui_kit.motion = !taskbar.glides;
        for (int at = 0; view->marks && at < (int)view->count && chosen < 0; at++)
                if (view->marks[at] & COMPOSITOR_TAB_LIT)
                        chosen = at, tab = ui_tree_find((struct ui_tree address_to)t, at + 1);
        if (tab)
        {
                int row = (view->rows ? (int)view->rows[chosen] : chosen) + 1;
                b64 at = ui_q(m ? tab->y - band->y : tab->x - band->x), length = ui_q(m ? tab->h : tab->w), shown = ui_tween_at(&lit->shown, &live);
                if (row != lit->row && lit->at.placed && shown > 0)
                        ui_kit_number_to(&lit->at, at, TASKBAR_LIT_MS, &ui_ease), ui_kit_number_to(&lit->length, length, TASKBAR_LIT_MS, &ui_ease);
                else if (at != taskbar_tween_goal(&lit->at) || length != taskbar_tween_goal(&lit->length) || !lit->at.placed)
                        ui_tween_put(&lit->at, at, UI_TWEEN_NUMBER), ui_tween_put(&lit->length, length, UI_TWEEN_NUMBER);
                if (!lit->shown.placed)
                        ui_tween_put(&lit->shown, UI_ONE, UI_TWEEN_NUMBER);
                ui_kit_number_to(&lit->shown, UI_ONE, TASKBAR_LIT_FADE_MS, &ui_ease);
                lit->row = row;
        }
        else if (lit->shown.placed)
                ui_kit_number_to(&lit->shown, 0, TASKBAR_LIT_FADE_MS, &ui_ease);
        ui_kit.motion = motion;
}

/* Moon growth, glow, spin, wallpaper bridge, and background frame baker. */
static b64 taskbar_moon_share(void)
{
        b32 live = 0;
        b64 s = ui_tween_at(&taskbar_band_cur()->grow, &live);
        return s < 0 ? 0 : s > UI_ONE ? UI_ONE : s;
}

static int taskbar_moon_most(int size) { return (int)((positive)size * TASKBAR_MOON_GROW / 200 + 1); }
static int taskbar_moon_more(const struct taskbar_view *view) { return (int)(((positive)taskbar_thick(view->scale) * TASKBAR_MOON_GROW * (positive)taskbar_moon_share() / UI_ONE + 199) / 200); }
static void taskbar_moon_hover(int band) { taskbar.moon_hover = band + 1; }
static void taskbar_moon_seen(positive now) { taskbar.moon_seen = now; }
static int taskbar_motion_about(positive now) { return taskbar.glides && !taskbar.moon_at && taskbar.moon_seen && now - taskbar.moon_seen < TASKBAR_MOON_IDLE; }
static void taskbar_spin_set(int s) { taskbar.spin = s >= 10 && s <= 3600 ? s : s == 0 ? 0 : TASKBAR_MOON_SPIN; }
static void taskbar_bar_set(int sep, int edge) { taskbar.separate = sep != 0, taskbar.edge_in = edge >= TASKBAR_EDGE_LEAST && edge <= TASKBAR_EDGE_MOST ? edge : 0, taskbar.stale = 1; }

static void taskbar_moon_damaged(int band, const int *rect)
{
        if (band >= 0 && band < WINDOWS_OUTPUTS)
                memory_copy(taskbar_bands[band].moon_damage, rect, sizeof(taskbar_bands[band].moon_damage)), taskbar_bands[band].moon_own = 1;
}

static b64 taskbar_moon_glow_level(void)
{
        int rest = taskbar.moon_rest ? taskbar.moon_rest - 1 : TASKBAR_MOON_REST_GLOW;
        return (b64)UI_ONE * rest / 100 + taskbar_moon_share() * (100 - rest) / 100;
}

static int taskbar_moon_glow_square(const struct taskbar_view *view, int sq[4])
{
        int x, y, w, h;
        if (!taskbar_slot(view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || w <= 0 || h <= 0 || taskbar.moon_ink)
                return 0;
        int size = w < h ? w : h, reach = size * TASKBAR_MOON_HALO / 100 + taskbar_moon_most(size) + 1;
        sq[0] = x + (w - size) / 2 - reach, sq[1] = y + (h - size) / 2 - reach, sq[2] = sq[3] = size + 2 * reach;
        return 1;
}

static int taskbar_moon_glow_was(int band, int shown)
{
        if (band < 0 || band >= WINDOWS_OUTPUTS)
                return 0;
        int was = taskbar_bands[band].glow_shown;
        return taskbar_bands[band].glow_shown = shown, was;
}

HOT static int taskbar_moon_glow(const struct taskbar_view *view, unsigned int *restrict pixels, const int square[4])
{
        struct taskbar_band address_to band = taskbar_band_cur();
        b64 share = taskbar_moon_share(), level = taskbar_moon_glow_level();
        int side = square[2], x, y, w, h;
        unsigned int accent = taskbar_accent();

        if (level <= 0 || !taskbar_slot(view, TASKBAR_LAUNCHER, &x, &y, &w, &h))
                return 0;
        int size = w < h ? w : h;
        int key[5] = {side, (int)(share >> 10), (int)accent, window_light(compositor_tab_target(0)), (int)(positive)pixels};
        if (!memory_compare(band->glow_key, key, sizeof(key)))
                return 1;
        memory_copy(band->glow_key, key, sizeof(key));
        decimal strength = TASKBAR_MOON_HALO_ALPHA * (decimal)level / (decimal)UI_ONE, c = side / 2.0;
        decimal r = size * (1 + (decimal)share / (decimal)UI_ONE * TASKBAR_MOON_GROW / 100.0) / 2, reach = size * TASKBAR_MOON_HALO / 100.0;
        decimal inv_r = 1.0 / r, inv_reach = 1.0 / reach, inner_r = r - 1 > 0 ? r - 1 : 0, inner2 = inner_r * inner_r, outer2 = (r + reach) * (r + reach);
        unsigned int bloom = accent ? accent : 0x6f8fa8u, neg = ui_moon_hue_turn(bloom, -0.10), pos = ui_moon_hue_turn(bloom, 0.10);
        for (int py = 0; py < side; py++)
        {
                decimal dy = py + 0.5 - c, dy2 = dy * dy;
                unsigned int *restrict row = pixels + (long)py * side;
                for (int px = 0; px < side; px++)
                {
                        decimal dx = px + 0.5 - c, d2 = dx * dx + dy2;
                        if (d2 < inner2 || d2 >= outer2)
                        {
                                row[px] = 0;
                                continue;
                        }
                        decimal d = square_root(d2), t = (d - r) * inv_reach;
                        decimal a = d < r - 1 || t >= 1 ? 0 : strength * (t < 0 ? 1 : (1 - t) * (1 - t)) * (d < r ? d - (r - 1) : 1);
                        if (a <= 0)
                        {
                                row[px] = 0;
                                continue;
                        }
                        unsigned int h = ui_moon_hash((unsigned int)px * 73856093u ^ (unsigned int)py * 19349663u);
                        decimal side_x = dx * inv_r;
                        decimal abs_x = side_x < 0 ? -side_x : side_x;
                        if (abs_x > 1)
                                abs_x = 1;
                        unsigned int hue = window_mix(bloom, side_x < 0 ? neg : pos, (unsigned int)(abs_x * 256));
                        decimal dither = ((h & 255) + (h >> 8 & 255)) / 512.0 - 0.5, val[4];
                        val[3] = a * 255 + dither, val[3] = val[3] < 0 ? 0 : val[3] > 255 ? 255 : val[3];
                        for (int k = 0; k < 3; k++)
                                val[k] = (hue >> (16 - 8 * k) & 255) * a + dither, val[k] = val[k] < 0 ? 0 : val[k] > val[3] ? val[3] : val[k];
                        row[px] = (unsigned int)(val[3] + 0.5) << 24 | (unsigned int)(val[0] + 0.5) << 16 |
                                  (unsigned int)(val[1] + 0.5) << 8 | (unsigned int)(val[2] + 0.5);
                }
        }
        return 1;
}

static int taskbar_moon_tick(positive now, int launcher)
{
        int changed = 0;
        b32 motion = ui_kit.motion;

        if (taskbar.spin > 0 && taskbar_motion_about(now))
        {
                timespec real = {0, 0};
                long long period = (long long)taskbar.spin * TASKBAR_MOON_STEPS;
                clock_gettime(CLOCK_REALTIME, address_of real);
                long long step = (long long)real.tv_sec * TASKBAR_MOON_STEPS + real.tv_nsec / (1000000000l / TASKBAR_MOON_STEPS);
                if (step != taskbar.moon_step)
                        taskbar.moon_step = step, taskbar.moon_turn = (b64)(step % period) * UI_ONE / (b64)period, changed = 1;
        }
        ui_kit.now = ui_kit_clock(), ui_kit.motion = !taskbar.glides;
        for (int at = 0; at < WINDOWS_OUTPUTS; at++)
        {
                struct ui_tween address_to grow = &taskbar_bands[at].grow;
                b64 want = launcher || taskbar.moon_hover == at + 1 ? UI_ONE : 0;
                if (!grow->placed)
                        ui_tween_put(grow, 0, UI_TWEEN_NUMBER);
                if (want != taskbar_tween_goal(grow))
                {
                        b32 live = 0;
                        ui_kit_number_to(grow, want, TASKBAR_MOON_GROW_MS, &ui_ease), changed = 1, ui_tween_at(grow, &live);
                }
                changed |= taskbar_tween_moving(grow, ui_kit.now);
        }
        ui_kit.motion = motion;
        return changed;
}

static positive taskbar_moon_due(positive now)
{
        timespec real = {0, 0};
        positive each = 1000000000ull / TASKBAR_MOON_STEPS;
        if (!(taskbar.spin > 0 && taskbar_motion_about(now)))
                return 0;
        clock_gettime(CLOCK_REALTIME, address_of real);
        return now + each - (positive)real.tv_nsec % each + 1000000ull;
}

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

/* Tray formatting, battery, and /proc|/sys readers. */
static void taskbar_digits(char *into, int value) { into[positive_into_string((string_address)into, (positive)taskbar_clamp(value, 0, 9999))] = 0; }

static void taskbar_battery_text(char *into, int charging, int percent)
{
        int at = charging ? (into[0] = '+', 1) : 0;
        at += (int)positive_into_string((string_address)(into + at), (positive)taskbar_clamp(percent, 0, 9999));
        into[at++] = '%', into[at] = 0;
}

static void taskbar_tray_put(char *into, int *at, int room, const char *text)
{
        for (int i = 0; text[i] && *at + 1 < room; i++)
                into[(*at)++] = text[i];
        into[*at] = 0;
}

static void taskbar_path_num(char *into, int room, const char *a, const char *b, positive num, const char *c)
{
        int at = 0;
        char digits[24];
        digits[positive_into_string((string_address)digits, num)] = 0;
        taskbar_tray_put(into, &at, room, a), taskbar_tray_put(into, &at, room, b);
        taskbar_tray_put(into, &at, room, digits), taskbar_tray_put(into, &at, room, c);
}

static void taskbar_tray_two(char *into, int *at, int room, int v)
{
        char two[3] = {(char)('0' + v / 10 % 10), (char)('0' + v % 10), 0};
        taskbar_tray_put(into, at, room, two);
}

static void taskbar_tray_file(const char *path, char *into, int room)
{
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)into, (positive)room - 1);
        int at = 0;
        while (at < got && into[at] != '\n' && (unsigned char)into[at] >= 0x20)
                at++;
        into[at] = 0;
}

static const char *taskbar_host_file = "/proc/sys/kernel/hostname";

static long taskbar_tray_number(const char *text)
{
        positive used = 0, value;
        while (*text == ' ')
                text++;
        value = string_digits((string_address)text, &used);
        return used ? (long)value : 0;
}

static void taskbar_tray_one(char *into, int *at, int room, int which, const tm *broken)
{
        static const unsigned short days[7] = {LANGUAGE_tray_day_sun, LANGUAGE_tray_day_mon, LANGUAGE_tray_day_tue, LANGUAGE_tray_day_wed,
                                               LANGUAGE_tray_day_thu, LANGUAGE_tray_day_fri, LANGUAGE_tray_day_sat};
        static const unsigned short months[12] = {LANGUAGE_tray_month_jan, LANGUAGE_tray_month_feb, LANGUAGE_tray_month_mar, LANGUAGE_tray_month_apr,
                                                  LANGUAGE_tray_month_may, LANGUAGE_tray_month_jun, LANGUAGE_tray_month_jul, LANGUAGE_tray_month_aug,
                                                  LANGUAGE_tray_month_sep, LANGUAGE_tray_month_oct, LANGUAGE_tray_month_nov, LANGUAGE_tray_month_dec};
        char word[64] = {0};

        switch (which)
        {
        case 0: if (taskbar.battery_item) taskbar_battery_text(word, taskbar.battery_charging, taskbar.battery_percent); break;
        case 1: if (taskbar.battery_item) taskbar_digits(word, taskbar.battery_percent); break;
        case 2: if (taskbar.battery_item) taskbar_tray_put(into, at, room, taskbar.battery_charging ? WORDS(tray_charging) : WORDS(tray_discharging)); return;
        case 3: case 4: case 8: case 9:
                taskbar_tray_two(into, at, room, which == 3 ? broken->tm_hour : which == 4 ? broken->tm_min : which == 8 ? broken->tm_mon + 1 : broken->tm_mday); return;
        case 5: taskbar_digits(word, broken->tm_hour % 12 ? broken->tm_hour % 12 : 12); break;
        case 6: taskbar_tray_put(into, at, room, broken->tm_hour < 12 ? WORDS(tray_am) : WORDS(tray_pm)); return;
        case 7: taskbar_tray_two(into, at, room, (broken->tm_year + 1900) / 100); taskbar_tray_two(into, at, room, (broken->tm_year + 1900) % 100); return;
        case 10: taskbar_tray_put(into, at, room, language_text(days[(unsigned)broken->tm_wday % 7])); return;
        case 11: taskbar_tray_put(into, at, room, language_text(months[(unsigned)broken->tm_mon % 12])); return;
        case 12:
                taskbar_tray_file("/proc/loadavg", word, (int)sizeof(word));
                for (int i = 0; word[i]; i++)
                        if (word[i] == ' ')
                                word[i] = 0;
                break;
        case 13:
        {
                char info[1024];
                bipolar got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/meminfo", (p8 address_to)info, sizeof(info) - 1);
                info[got > 0 ? got : 0] = 0;
                const char *tv = taskbar_line_value(info, "MemTotal:", 0), *fv = taskbar_line_value(info, "MemAvailable:", 0);
                long total = tv ? taskbar_tray_number(tv) : 0, free = fv ? taskbar_tray_number(fv) : 0;
                if (total > 0 && free <= total)
                        taskbar_digits(word, (int)((total - free) * 100 / total));
                break;
        }
        case 14:
        {
                long hottest = 0;
                for (int z = 0; z < 32; z++)
                {
                        char path[64], reading[24];
                        taskbar_path_num(path, (int)sizeof(path), "/sys/class/thermal/thermal_zone", "", (positive)z, "/temp");
                        taskbar_tray_file(path, reading, (int)sizeof(reading));
                        long milli = taskbar_tray_number(reading);
                        if (milli > hottest)
                                hottest = milli;
                }
                if (hottest > 0 && hottest < 1000000)
                        taskbar_digits(word, (int)(hottest / 1000));
                break;
        }
        case 15: taskbar_tray_file(taskbar_host_file, word, 33); break;
        default: break;
        }
        taskbar_tray_put(into, at, room, word);
}

static void taskbar_tray_render(void)
{
        language_take(compositor.table ? compositor.table->language : 0);
        taskbar.tray_text[0] = taskbar.clock_text[0] = 0;
        taskbar.tray_chars = taskbar.tray_custom ? taskbar_format_render(taskbar.tray_format, taskbar.tray_text, (int)sizeof(taskbar.tray_text)) : 0;
        taskbar.clock_chars = taskbar.clock_format[0] ? taskbar_format_render(taskbar.clock_format, taskbar.clock_text, (int)sizeof(taskbar.clock_text)) : 0;
}

static int taskbar_format_render(const char *format, char *into, int room)
{
        int at = 0, chars = 0;
        tm broken;

        into[0] = 0;
        if (!taskbar_broken_time(0, &broken))
                return 0;
        for (int i = 0; format[i];)
        {
                if (format[i] == '{' && format[i + 1] != '{')
                {
                        int close = i + 1;
                        while (format[close] && format[close] != '}')
                                close++;
                        taskbar_tray_one(into, &at, room, setting_tray_name(format + i + 1, (positive)(close - i - 1)), &broken);
                        i = format[close] ? close + 1 : close;
                }
                else
                {
                        if (at + 1 < room)
                                into[at++] = format[i];
                        into[at] = 0;
                        i += ((format[i] == '{' || format[i] == '}') && format[i + 1] == format[i]) ? 2 : 1;
                }
        }
        for (int i = 0; into[i]; i++)
                chars += ((unsigned char)into[i] & 0xc0) != 0x80;
        return chars;
}

static inline const char *taskbar_clock_said(const char *clock) { return taskbar.clock_chars > 0 ? taskbar.clock_text : clock; }

static void taskbar_clock_set(const char *format)
{
        string_copy_bounded((p8 *)taskbar.clock_format, (string_address)format, sizeof(taskbar.clock_format));
        taskbar_tray_render(), taskbar.stale = 1;
}

static void taskbar_tray_set(const char *text)
{
        char format[SETTING_TRAY_MOST + 1];
        int at = 0;

        while (text[at] && text[at] != '\n' && text[at] != '\r' && at < SETTING_TRAY_MOST)
                format[at] = text[at], at++;
        format[at] = 0;
        taskbar.tray_custom = format[0] && !string_equals((string_address)format, (string_address) "default") &&
                              !string_equals((string_address)format, (string_address)SETTING_TASKBAR_TRAY_STANDARD) && setting_tray_valid(format, null);
        if (taskbar.tray_custom)
                string_copy_bounded((p8 *)taskbar.tray_format, (string_address)format, sizeof(taskbar.tray_format));
        else
                taskbar.tray_format[0] = 0;
        taskbar_tray_render(), taskbar.stale = 1;
}

static void taskbar_battery_refresh(void)
{
        struct battery_state state = {0};
        const char *name = taskbar.battery;

        taskbar.battery_item = 0;
        if (string_equals((string_address)name, (string_address) "none"))
                return;
        if (string_equals((string_address)name, (string_address) "auto"))
                name = "";
        if (!battery_read(name[0] ? (string_address)name : null, &state) || !state.present || state.percent < 0)
                return;
        taskbar.battery_item = 1, taskbar.battery_percent = state.percent, taskbar.battery_charging = state.charging == BATTERY_CHARGING;
}

/* Moon accent, wallpaper draw, background bake thread, and moon cell painting. */
static unsigned int taskbar_wall_accent(void) { return taskbar_accent(); }

static void taskbar_wall_draw(unsigned int *pixels, int pitch, int width, int height, int dark, int dimmed, int maria, int glow, unsigned int accent,
                              b64 turn, int moon_alone, int rect[4])
{
        struct ui ui = {0};
        ui.pixels = pixels, ui.pitch = (unsigned long)pitch, ui.width = width, ui.height = height, ui.bottom = height, ui.scale = 100;
        ui_moon_night_part(&ui, turn, dark ? UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | (dimmed ? UI_MOON_PAPER_DIM : 0) : 0, accent, glow ? 1 : 0,
                           glow > 1 ? 1 : 0, moon_alone, rect, maria / 100.0);
}

static unsigned int taskbar_accent(void)
{
        unsigned int accent = compositor.table && compositor.table->themed ? compositor.table->accent & 0xffffffu : 0;
        int r = (int)(accent >> 16 & 255), g = (int)(accent >> 8 & 255), b = (int)(accent & 255);
        int most = r > g ? (r > b ? r : b) : (g > b ? g : b), least = r < g ? (r < b ? r : b) : (g < b ? g : b);
        return most - least < 40 ? 0x5b7fa6u : accent;
}

static struct taskbar_moon_bake
{
        unsigned int address_to frames;
        unsigned char address_to have;
        positive bytes;
        int size, period, from;
        unsigned int rim, accent;
        unsigned long used;
} taskbar_bakes[TASKBAR_MOON_BAKES];
static int taskbar_bake_running, taskbar_bake_at, taskbar_bake_stop;
static thread address_to taskbar_bake_worker;
static unsigned long taskbar_bake_uses;

static void taskbar_moon_render(struct ui address_to ui, int x, int y, int size, int left, int top, int grown, b64 turn, decimal grow,
                                unsigned int rim, unsigned int accent)
{
        unsigned int opts = UI_MOON_RIM | (grow <= 0 ? UI_MOON_FULL_SQUARE : 0u);
        struct ui_moon_look look = {0xff0c0d10u, 0xfff7f5efu, 0, rim, opts, 0};
        (void)x, (void)y, (void)size;
        // Draw sphere and Fresnel rim with the exact same (left, top, grown) bounds so center/radius never drift on hover.
        ui_moon(ui, left, top, grown, UI_ONE / 2, turn, &look);
        ui_moon_rim(ui, left, top, grown, accent, 0.85);
}

static void taskbar_moon_bake_frame(struct taskbar_moon_bake address_to b, int step)
{
        struct ui ui = {0};
        ui.pixels = b->frames + (positive)b->size * (positive)b->size * (positive)step;
        ui.pitch = (unsigned long)b->size, ui.width = ui.height = ui.bottom = b->size;
        taskbar_moon_render(&ui, 0, 0, b->size, 0, 0, b->size, (b64)step * UI_ONE / (b64)b->period, 0, b->rim, b->accent);
        __atomic_store_n(&b->have[step], 1, __ATOMIC_RELEASE);
}

static fn taskbar_moon_bake_run(address_any context)
{
        struct taskbar_moon_bake address_to b = &taskbar_bakes[(long)context];
        compositor_ordinary(10);
        for (int k = 0; k < b->period && !__atomic_load_n(&taskbar_bake_stop, __ATOMIC_ACQUIRE); k++)
        {
                int step = (b->from + k) % b->period;
                if (!__atomic_load_n(&b->have[step], __ATOMIC_ACQUIRE))
                        taskbar_moon_bake_frame(b, step);
        }
        __atomic_store_n(&taskbar_bake_running, 0, __ATOMIC_RELEASE);
}

static void taskbar_moon_bake_halt(void)
{
        if (!taskbar_bake_worker)
                return;
        __atomic_store_n(&taskbar_bake_stop, 1, __ATOMIC_RELEASE);
        while (__atomic_load_n(&taskbar_bake_running, __ATOMIC_ACQUIRE))
                compositor_call(syscall(nanosleep), (positive)(positive[2]){0, 50000}, 0, 0, 0, 0, 0);
        thread_join(taskbar_bake_worker);
        taskbar_bake_worker = null;
        __atomic_store_n(&taskbar_bake_stop, 0, __ATOMIC_RELEASE);
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
        int period = taskbar.spin * TASKBAR_MOON_STEPS, at = -1, running = __atomic_load_n(&taskbar_bake_running, __ATOMIC_ACQUIRE);
        positive frame = (positive)size * (positive)size, bytes = frame * 4 * (positive)period + (positive)period;
        int step = (int)(taskbar.moon_step % (period > 0 ? period : 1));
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
                if (!(b->frames = taskbar_remap(b->frames, b->bytes, bytes)))
                        return null;
                b->have = (unsigned char address_to)((unsigned char address_to)b->frames + frame * 4 * (positive)period);
                b->bytes = bytes, b->size = size, b->period = period, b->rim = rim, b->accent = accent;
        }
        b = &taskbar_bakes[at], b->used = ++taskbar_bake_uses;
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
                if (!(taskbar_bake_worker = thread_start(taskbar_moon_bake_run, (address_any)(long)at)))
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

static void taskbar_edit_ring(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n,
                              unsigned int ground, unsigned int ink, unsigned int alpha)
{
        int s = taskbar_scaled(view->scale);
        struct ui grip;
        struct ui_box ring = taskbar_face(t, n, 0, 0);
        b32 in = (b32)(s / 100 > 1 ? s / 100 : 1) * 512;

        taskbar_glass_ui(&grip, view, t, ground);
        ring.x0 += in, ring.y0 += in, ring.x1 -= in, ring.y1 -= in;
        ring.rx = ring.ry = (ring.x1 - ring.x0 < ring.y1 - ring.y0 ? ring.x1 - ring.x0 : ring.y1 - ring.y0) / 2;
        ui_kit_ring(&grip, &ring, (b32)(s * 256 / 100 > 256 ? s * 256 / 100 : 256), (ink & 0xffffffu) | alpha);
}

static void taskbar_moon(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, int hours, int minutes, int lit)
{
        struct ui ui;
        int x, y;
        const struct ui_node address_to mark = taskbar_ui(&ui, view, t, n, &x, &y);
        int size = mark->w < mark->h ? mark->w : mark->h, light = window_light(compositor_tab_target(0));
        b64 turn = taskbar.moon_at ? (taskbar.glides ? (b64)(taskbar_now_sec(taskbar.moon_at) / 60 % 60) * UI_ONE / 60 : 0) : taskbar.moon_turn;
        decimal grow = (decimal)taskbar_moon_share() / (decimal)UI_ONE * TASKBAR_MOON_GROW / 100;
        int grown = size + (int)(size * grow + 0.5), left = x + (size - grown) / 2, top = y + (size - grown) / 2;
        unsigned int rim = lit ? 0xffffffffu : light ? 0xb0d8d8d8u : 0xb4e8e8e8u, accent = taskbar_accent();
        const unsigned int address_to cached_frame = null;

        if (taskbar.moon_ink)
        {
                unsigned int paper, ink;
                ui_moon_paper_tones(light ? 0 : UI_MOON_PAPER_DARK, &paper, &ink);
                int r0 = top < 0 ? 0 : top, r1 = top + grown < ui.height ? top + grown : ui.height;
                int c0 = left < 0 ? 0 : left, c1 = left + grown < ui.width ? left + grown : ui.width;
                if (c1 > c0 && r1 > r0)
                        memory_fill_u32_rect(ui.pixels + (long)r0 * (long)ui.pitch + c0, (positive)ui.pitch, (positive)(c1 - c0), (positive)(r1 - r0), ink);
                ui_moon_paper(&ui, left, top, grown, turn, light ? 0 : UI_MOON_PAPER_DARK, 0);
                ui_moon_rim(&ui, left, top, grown, accent, 0.85);
        }
        else
        {
                const unsigned int address_to frame = grow > 0 || x < 0 || y < 0 || x + size > ui.width || y + size > ui.height
                                                          ? null : taskbar_moon_baked(size, rim, accent);
                if (frame)
                        taskbar_blit_square(ui.pixels + (long)y * (long)ui.pitch + x, (long)ui.pitch, frame, size, size),
                            left = x, top = y, grown = size, cached_frame = frame;
                else
                        taskbar_moon_bake_halt(), taskbar_moon_render(&ui, x, y, size, left, top, grown, turn, grow, rim, accent);
        }
        x = left, y = top, size = grown;
        if (taskbar.clock_tray || taskbar.clock_tab || taskbar.clock_none || hours < 0 || !view->engine || !view->face)
                return;
        struct taskbar_moon_clock_key
        {
                const unsigned int address_to frame;
                const void address_to engine, address_to face;
                int size, hours, minutes, flags, bulge;
                unsigned int accent;
        } key = {cached_frame, view->engine, view->bold ? (const void address_to)view->bold : (const void address_to)view->face,
                 size, hours, minutes, taskbar.clock_12h, taskbar.clock_bulge, accent};
        static struct { struct taskbar_moon_clock_key key; int read; unsigned int address_to pixels; positive bytes; } shown;
        b32 whole = cached_frame && x >= 0 && y >= 0 && x + size <= ui.width && y + size <= ui.height;
        unsigned int address_to dst = ui.pixels + (long)y * (long)ui.pitch + x;
        positive need = (positive)size * (positive)size * 4;

        if (whole && shown.pixels && !memory_compare(&shown.key, &key, sizeof(key)))
        {
                taskbar_blit_square(dst, (long)ui.pitch, shown.pixels, size, size), taskbar.clock_read = shown.read;
                return;
        }
        int hr = taskbar.clock_12h ? (hours % 12 ? hours % 12 : 12) : hours;
        char upper[3] = {(char)('0' + hr / 10), (char)('0' + hr % 10), 0}, lower[3] = {(char)('0' + minutes / 10), (char)('0' + minutes % 10), 0};
        if (taskbar.clock_12h && hr < 10)
                upper[0] = upper[1], upper[1] = 0;
        taskbar.clock_read = (int)ui_moon_label(&ui, view->engine, view->bold ? view->bold : view->face, x, y, size, upper, lower,
                                                taskbar.clock_bulge / 100.0, accent);
        if (!whole)
                return;
        if (!shown.pixels || shown.bytes < need)
        {
                if (!(shown.pixels = taskbar_remap(shown.pixels, shown.bytes, need)))
                        return;
                shown.bytes = need;
        }
        taskbar_blit_square(shown.pixels, size, dst, (long)ui.pitch, size);
        shown.key = key, shown.read = taskbar.clock_read;
}

/* Tab icons, tray cells, and full band drawing. */
struct taskbar_picture { const unsigned int *from; unsigned int seq; int side; unsigned int pixels[UI_ICON_MOST * UI_ICON_MOST]; };
static struct ui_grown taskbar_pictures;

static bool taskbar_tab_full(const struct taskbar_view *view, int at)
{
        return (view->apps && view->apps[at] && ui_apps_has_picture_kept(ui_apps_match(view->apps[at]))) ||
               (view->icons && view->icon_seqs && view->icons[at] && view->icon_seqs[at]);
}

static void taskbar_tab_rounded(struct ui address_to ui, int x, int y, int side, const unsigned int address_to picture, unsigned int ends)
{
        static unsigned int cut[UI_PICTURE_MOST * UI_PICTURE_MOST];
        if (side <= 0 || side > UI_PICTURE_MOST)
                return;
        int inner_r = side > 1 ? (side - 1) * (side - 1) : 0, outer_r = (side + 1) * (side + 1);
        for (int r = 0; r < side; r++)
                for (int c = 0; c < side; c++)
                {
                        unsigned int p = picture[r * side + c], cov = 256;
                        int dx = 2 * c + 1 - side, dy = 2 * r + 1 - side;
                        if (((ends & 1) && dx < 0) || ((ends & 2) && dx > 0) || ((ends & 4) && dy < 0) || ((ends & 8) && dy > 0))
                        {
                                int d2 = dx * dx + dy * dy;
                                if (d2 >= outer_r)
                                        cov = 0;
                                else if (d2 > inner_r)
                                        cov = (unsigned int)taskbar_clamp((side - (int)ui_touch_isqrt((unsigned long long)d2)) * 128 + 128, 0, 256);
                        }
                        if (cov == 0)
                                p = 0;
                        else if (cov < 256)
                                p = ((p >> 24) * cov >> 8) << 24 | ((p >> 16 & 255) * cov >> 8) << 16 | ((p >> 8 & 255) * cov >> 8) << 8 | ((p & 255) * cov >> 8);
                        cut[r * side + c] = p;
                }
        ui_icon_picture(ui, x, y, side, cut);
}

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

static void taskbar_tab_turning(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n)
{
        int x, y, side, scale = taskbar_scaled(view->scale);
        struct ui ui;
        b32 motion = ui_kit.motion;

        if (scale <= 0 || !(side = taskbar_in_band(&ui, view, t, n, &x, &y)))
                return;
        struct ui_control spin = {.kind = UI_SPINNER, .w = side * 100 / scale, .x = x * 100 / scale, .y = y * 100 / scale};
        ui_kit.now = ui_kit_clock(), ui_kit.motion = !taskbar.glides;
        ui_spinner_draw(&ui, &spin);
        ui_kit.motion = motion;
}

static void taskbar_tab_icon(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n, const struct ui_node address_to tab,
                             int which, unsigned int ink)
{
        const char *app = view->apps ? view->apps[which] : null;
        int x, y, side;
        unsigned int ends = 0;
        struct ui ui;

        if (!(side = taskbar_in_band(&ui, view, t, n, &x, &y)))
                return;
        if (tab)
                ends = tab->w >= tab->h ? ((n->x <= tab->x ? 1u : 0u) | (n->x + n->w >= tab->x + tab->w ? 2u : 0u))
                                        : ((n->y <= tab->y ? 4u : 0u) | (n->y + n->h >= tab->y + tab->h ? 8u : 0u));
        if (!ends || (tab && n->w >= tab->w && n->h >= tab->h))
                ends = 15u;
        const unsigned int address_to picture = side <= UI_PICTURE_MOST ? ui_apps_picture_kept(ui_apps_match(app), side) : null;
        if (picture)
        {
                taskbar_tab_rounded(&ui, x, y, side, picture, ends);
                return;
        }
        if (view->icons && view->icon_seqs && view->icons[which] && view->icon_seqs[which] && side <= UI_ICON_MOST)
        {
                unsigned int row = view->rows ? view->rows[which] : (unsigned int)which;
                if (!ui_grown_room(&taskbar_pictures, sizeof(struct taskbar_picture), WINDOWS, (long)row + 1))
                        return;
                struct taskbar_picture address_to kept = (struct taskbar_picture address_to)taskbar_pictures.at + row;
                if (kept->from != view->icons[which] || kept->seq != view->icon_seqs[which] || kept->side != side)
                        ui_icon_scale(kept->pixels, side, view->icons[which]), kept->from = view->icons[which], kept->seq = view->icon_seqs[which], kept->side = side;
                taskbar_tab_rounded(&ui, x, y, side, kept->pixels, ends);
                return;
        }
        int symbol = ui_icon_of(app);
        if (symbol < 0 && app && app[0])
                ui_icon_tile(&ui, x, y, side, app);
        else
                ui_icon(&ui, symbol < 0 ? UI_ICON_WINDOW : symbol, x, y, side, 0, ink | 0xff000000u);
}

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

static void taskbar_mic_cell(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n)
{
        struct ui ui;
        int x, y;
        const struct ui_node address_to mark = taskbar_ui(&ui, view, t, n, &x, &y);
        ui_microphone(&ui, x, y, mark->w, taskbar.mic_muted, compositor_ink_role(UI_ERROR) | 0xff000000u);
}

static void taskbar_notices_cell(const struct taskbar_view *view, const struct ui_tree address_to t, const struct ui_node address_to n)
{
        struct ui ui;
        unsigned int colour = compositor_ink_role(taskbar.notices_severity == WINDOWS_ISSUE_ERROR ? UI_ERROR : UI_WARNING);
        int x, y;
        taskbar_ui(&ui, view, t, n, &x, &y);
        ui_kit_dot(&ui, x, y, colour | 0xff000000u);
}

static int taskbar_draw(const struct taskbar_view *view, unsigned long redo, taskbar_glass_fn glass, void *context, int *rect)
{
        const struct ui_tree address_to t = taskbar_tree(view);
        const struct ui_node address_to band = &t->node[1];
        struct taskbar_band address_to cur = taskbar_band_cur();
        struct taskbar_lit address_to lit = &cur->lit;
        int span[4] = {0, 0, 0, 0}, stripe[4], gw;
        tm broken;
        unsigned int ground = 0, ink = 0;
        b32 live = 0;
        char clock[6] = {0}, text[8];

        cur->moon_own = 0;
        if (!view->pixels || (unsigned long)band->w * (unsigned long)band->h * 4 > view->band_bytes)
                return 0;
        for (int which = TASKBAR_STRIP_ON; which < (int)view->count; which++)
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
        /* No glass this frame (the card is busy, a frost is refused, a timeout): nothing of the band is written, so the pixels it
           had stay, and nothing is damaged. The stale flag is kept, so the next pass redraws every tab (present.c). Returning 1
           here damaged the band over pixels that were never drawn. */
        if (!(gw = glass(context, rect[0], rect[1], rect[2], rect[3])))
                return 0;
        // The draw paints what the band holds and reads nothing: the battery, the tray's words and the minute are taskbar_tick's.
        if (redo == ~0ul)
                taskbar.stale = 0;
        int time_ok = taskbar_broken_time(taskbar.moon_at, &broken);
        if (time_ok)
                clock[0] = (char)('0' + broken.tm_hour / 10), clock[1] = (char)('0' + broken.tm_hour % 10), clock[2] = ':',
                clock[3] = (char)('0' + broken.tm_min / 10), clock[4] = (char)('0' + broken.tm_min % 10);
        taskbar_battery_text(text, taskbar.battery_charging, taskbar.battery_percent);
        taskbar_lit_to(lit, t, view);
        b64 lit_at = ui_tween_at(&lit->at, &live), lit_length = ui_tween_at(&lit->length, &live), lit_shown = ui_tween_at(&lit->shown, &live);
        for (int at = 2; at < t->count; at++)
        {
                const struct ui_node address_to n = &t->node[at];
                int which = n->id - 1, m = band->dir;
                b32 tray_item = which <= TASKBAR_TRAY_ITEM_BASE && which > TASKBAR_TRAY_ITEM_BASE - TASKBAR_TRAY_ITEM_MAX;
                b32 space_item = (which <= TASKBAR_SPACE_BASE && which > TASKBAR_SPACE_BASE - TASKBAR_SPACE_MAX) || which == TASKBAR_SPACE_ADD;

                if (!n->id || which == TASKBAR_TABS || which == TASKBAR_TABS_LEFT || (which <= TASKBAR_ZONE && which > TASKBAR_DONE) || !(redo & taskbar_bit(which)) ||
                    (which < 0 && which != TASKBAR_LAUNCHER && which != TASKBAR_DONE && !tray_item && !space_item && !time_ok))
                        continue;
                if (which >= 0 || which == TASKBAR_TRAY || which == TASKBAR_TRAY_BEFORE || which == TASKBAR_DONE || which == TASKBAR_STRIP_BACK ||
                    which == TASKBAR_STRIP_ON || (which == -3 && taskbar.clock_tab))
                {
                        unsigned int mark = which >= 0 ? view->marks[which] : 0;
                        struct ui ui;
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
                case TASKBAR_TRAY: case TASKBAR_TRAY_BEFORE: break;
                case TASKBAR_DONE: taskbar_say(view, t, n, (const char *)WORDS(tray_done), ink, ground); break;
                case TASKBAR_STRIP_BACK: case TASKBAR_STRIP_ON:
                        taskbar_say(view, t, n, (which == TASKBAR_STRIP_BACK) != (m == 0 && language_rtl()) ? "\xe2\x80\xb9" : "\xe2\x80\xba", ink, ground);
                        break;
                case -9: if (taskbar.tray_custom) taskbar_say(view, t, n, taskbar.tray_text, ink, ground); break;
#define TASKBAR_CELL(slot, shown, mark, paint) case slot: paint; break;
                TASKBAR_TRAY_CELLS(TASKBAR_CELL)
#undef TASKBAR_CELL
                default:
                        if (space_item)
                        {
                                char label[4] = {0};
                                unsigned int sg = ground, si = ink;

                                if (!time_ok)
                                        sg = taskbar_glass_tab(view, rect[0], rect[1], gw, band->x, band->y, band->w, n->x, n->y, n->w, n->h),
                                        si = window_contrast(sg);
                                if (which == TASKBAR_SPACE_ADD)
                                        label[0] = '+';
                                else
                                {
                                        int idx = TASKBAR_SPACE_BASE - which, active = taskbar_spaces_active();
                                        if (idx == active)
                                        {
                                                struct ui sui;
                                                struct ui_box sbox = taskbar_face(t, n, 0, 0);
                                                taskbar_glass_ui(&sui, view, t, sg);
                                                sg = ui_glass_button(&sui, &sbox, UI_ONE, null);
                                                si = window_contrast(sg);
                                        }
                                        if (taskbar_spaces_kind(idx))
                                                label[0] = 'F';
                                        else if (idx + 1 < 10)
                                                label[0] = (char)('1' + idx);
                                        else
                                                label[0] = (char)('0' + (idx + 1) / 10), label[1] = (char)('0' + (idx + 1) % 10);
                                }
                                taskbar_say(view, t, n, label, si, sg);
                                break;
                        }
                        if (tray_item)
                        {
                                const struct ui_node address_to mark = n->first >= 0 ? &t->node[n->first] : null;
                                const unsigned int *px = taskbar_tray_items_pixels(TASKBAR_TRAY_ITEM_BASE - which);
                                if (!time_ok)
                                        ground = taskbar_glass_tab(view, rect[0], rect[1], gw, band->x, band->y, band->w, n->x, n->y, n->w, n->h),
                                        ink = window_contrast(ground);
                                if (mark && px && mark->w > 0 && mark->h > 0)
                                {
                                        int mx = mark->x - band->x, my = mark->y - band->y, mw = mark->w, mh = mark->h;
                                        int yy0 = my < 0 ? -my : 0, yy1 = my + mh > band->h ? band->h - my : mh;
                                        int xx0 = mx < 0 ? -mx : 0, xx1 = mx + mw > band->w ? band->w - mx : mw;
                                        unsigned int step_x = ((unsigned int)TASKBAR_TRAY_ITEM_PX << 16) / (unsigned int)mw;
                                        for (int yy = yy0; yy < yy1; yy++)
                                        {
                                                const unsigned int *src_row = px + (yy * TASKBAR_TRAY_ITEM_PX / mh) * TASKBAR_TRAY_ITEM_PX;
                                                unsigned int *dst_row = view->pixels + (long)(my + yy) * band->w + mx;
                                                unsigned int sx = (unsigned int)xx0 * step_x;
                                                for (int xx = xx0; xx < xx1; xx++, sx += step_x)
                                                {
                                                        unsigned int src = src_row[sx >> 16], a = src >> 24;
                                                        if (a)
                                                                dst_row[xx] = window_mix(dst_row[xx], src & 0xffffffu, (int)(a + (a >> 7)));
                                                }
                                        }
                                }
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

/* Clock tick, sizing, and settings setters/getters. */
static int taskbar_tick(void)
{
        unsigned int now = taskbar_minute_now(), language_now = compositor.table ? compositor.table->language : 0;

        if (now == taskbar.minute && language_now == taskbar.language)
                return 0;
        taskbar.language = language_now, taskbar.minute = now;
        taskbar_battery_refresh();
        taskbar_tray_render();
        return 1;
}

static void taskbar_changed(void) { taskbar.stale = 1; }
static int taskbar_stale(void) { return taskbar.stale; }
static unsigned int taskbar_position(void) { return taskbar.position; }
static void taskbar_position_default(unsigned int pos) { taskbar.position_default = taskbar.position = pos; }
static int taskbar_display_all(void) { return taskbar.all; }
static void taskbar_text_set(int edge) { taskbar.edge = edge != 0, taskbar.stale = 1; }
static void taskbar_launcher_set(int on) { taskbar.launcher = on != 0, taskbar.stale = 1; }
static int taskbar_launcher_on(void) { return taskbar.launcher; }
static void taskbar_hide_set(int on) { taskbar.hide_fullscreen = on != 0; }
static void taskbar_scale_set(int p) { taskbar.scale = p >= TASKBAR_SCALE_LEAST && p <= TASKBAR_SCALE_MOST ? p : TASKBAR_SCALE_STANDARD, taskbar.stale = 1; }
static void taskbar_padding_set(int p) { taskbar.padding = p >= 0 && p <= TASKBAR_PADDING_MOST ? p : TASKBAR_PADDING_STANDARD, taskbar.stale = 1; }
static const char *taskbar_clock_other(void) { return taskbar.clock_12h ? "24h" : "12h"; }
static void taskbar_clock_turn(void) { taskbar.clock_12h = !taskbar.clock_12h, taskbar.stale = 1; }
static int taskbar_editing(void) { return taskbar.editing; }
static void taskbar_edit_set(int on) { taskbar.editing = on != 0, taskbar.stale = 1; }

static void taskbar_bytes(int width, int height, int scale, unsigned long *band, unsigned long *glass)
{
        int px = (scale + 99) / 100, thick = taskbar_thick(scale);
        int across = COMPOSITOR_GLASS_ACROSS * px, down = COMPOSITOR_GLASS_DOWN * px;
        long wide = (long)(width + 2 * across) * (long)(thick + 2 * down), tall = (long)(thick + 2 * across) * (long)(height + 2 * down);
        *band = (unsigned long)(width > height ? width : height) * (unsigned long)thick * 4;
        *glass = (unsigned long)(wide > tall ? wide : tall) * 4;
}

static void taskbar_settings_set(const char *position, const char *display, const char *tune)
{
        static const struct { const char *word; unsigned int pos; } edges[4] = {{"bottom", TASKBAR_BOTTOM}, {"top", TASKBAR_TOP}, {"left", TASKBAR_LEFT}, {"right", TASKBAR_RIGHT}};
        unsigned int want = taskbar.position_default;
        const char *val = taskbar_line_value(tune, "battery", ' ');
        int len = 0;

        for (int i = 0; i < 4; i++)
                if (string_equals((string_address)position, (string_address)edges[i].word))
                        want = edges[i].pos;
        taskbar.position = want, taskbar.all = string_equals((string_address)display, (string_address) "all");
        while (val && val[len] && val[len] != '\n' && len < (int)sizeof(taskbar.battery) - 1)
                taskbar.battery[len] = val[len], len++;
        taskbar.battery[len] = 0;
        taskbar_battery_refresh(), taskbar_pins_read(), taskbar.stale = 1;
}

static int taskbar_volume_set(int present, int percent, int muted, int flash)
{
        present = present != 0, muted = muted != 0, flash = flash != 0, percent = taskbar_clamp(percent, 0, 100);
        int changed = present != taskbar.volume_item || (present && (percent != taskbar.volume_percent || muted != taskbar.volume_muted || flash != taskbar.volume_flash));
        taskbar.volume_item = present, taskbar.volume_percent = percent, taskbar.volume_muted = muted, taskbar.volume_flash = flash;
        return taskbar.stale |= changed, changed;
}

static int taskbar_mic_set(int present, int muted)
{
        present = present != 0, muted = muted != 0;
        int changed = present != taskbar.mic_item || (present && muted != taskbar.mic_muted);
        taskbar.mic_item = present, taskbar.mic_muted = muted;
        return taskbar.stale |= changed, changed;
}

static int taskbar_notices_set(int present, unsigned int severity)
{
        present = present != 0;
        severity = present && severity == WINDOWS_ISSUE_ERROR ? WINDOWS_ISSUE_ERROR : present ? WINDOWS_ISSUE_WARNING : 0;
        int changed = present != taskbar.notices_item || severity != taskbar.notices_severity;
        taskbar.notices_item = present, taskbar.notices_severity = severity;
        return taskbar.stale |= changed, changed;
}

static void taskbar_arrange_set(int moon, int tasks, int newest, int order, unsigned int before, int look, int clock, int bulge, int twelve)
{
        static const unsigned char orders[6][3] = {
            {TASKBAR_PILL_MOON, TASKBAR_PILL_TASKS, TASKBAR_PILL_TRAY}, {TASKBAR_PILL_MOON, TASKBAR_PILL_TRAY, TASKBAR_PILL_TASKS},
            {TASKBAR_PILL_TASKS, TASKBAR_PILL_MOON, TASKBAR_PILL_TRAY}, {TASKBAR_PILL_TASKS, TASKBAR_PILL_TRAY, TASKBAR_PILL_MOON},
            {TASKBAR_PILL_TRAY, TASKBAR_PILL_MOON, TASKBAR_PILL_TASKS}, {TASKBAR_PILL_TRAY, TASKBAR_PILL_TASKS, TASKBAR_PILL_MOON}};
        static const int zones[4] = {TASKBAR_ZONE_SPLIT, TASKBAR_ZONE_START, TASKBAR_ZONE_CENTRE, TASKBAR_ZONE_END};

        taskbar.moon_zone = moon >= 0 && moon < 3 ? moon : TASKBAR_ZONE_CENTRE;
        taskbar.tasks_zone = tasks >= 0 && tasks < 4 ? zones[tasks] : TASKBAR_ZONE_SPLIT;
        taskbar.tasks_right = newest == 1;
        memory_copy(taskbar.order, orders[order >= 0 && order < 6 ? order : 0], sizeof(taskbar.order));
        taskbar.tray_before = before & 63u, taskbar.moon_ink = look == 1;
        taskbar.clock_none = clock == 1, taskbar.clock_tray = clock == 2 || clock == 3, taskbar.clock_tab = clock == 3;
        taskbar.clock_bulge = bulge >= 0 && bulge <= 100 ? bulge : 60, taskbar.clock_12h = twelve != 0, taskbar.stale = 1;
}

/* Pins, moon/tab context menus, and popup menu plumbing. */
struct taskbar_pin
{
        char app[WINDOWS_APP_ID], key[WINDOWS_APP_ID], title[TASKBAR_PIN_TITLE];
        unsigned short count, at[SETTING_PINS_WORDS];
        char text[SETTING_PINS_MOST + SETTING_PINS_WORDS];
};

static struct
{
        struct ui ui;
        struct ui_popmenu menu;
        int open, x, y, width, height;
        b32 dirty;
        int row;
        unsigned int pid;
        char stop[96];
        struct taskbar_pin pin;
} taskbar_menu;

/* The kit's theme is one state for the whole process: the menu takes the table's theme for its own draw, and the theme it
   found is put back after, so the windows drawn after it in the pass keep theirs. */
struct taskbar_theme_saved { struct ui_theme shown, from, to; struct ui_tween turn; unsigned int mode, accent, motion; b32 made; };

static void taskbar_theme_take(struct taskbar_theme_saved address_to t)
{
        t->shown = ui_kit.shown, t->from = ui_kit.from, t->to = ui_kit.to, t->turn = ui_kit.turn;
        t->mode = ui_kit.mode, t->accent = ui_kit.accent, t->motion = ui_kit.motion, t->made = ui_kit.made;
}

static void taskbar_theme_give(const struct taskbar_theme_saved address_to t)
{
        ui_kit.shown = t->shown, ui_kit.from = t->from, ui_kit.to = t->to, ui_kit.turn = t->turn;
        ui_kit.mode = t->mode, ui_kit.accent = t->accent, ui_kit.motion = t->motion, ui_kit.made = t->made;
}

static fn taskbar_menu_ui(const struct taskbar_view *view, unsigned int address_to pixels)
{
        struct ui address_to ui = &taskbar_menu.ui;
        struct windows address_to table = compositor.table;
        unsigned int themed = table && table->themed;
        unsigned int address_to scratch = ui->scratch;
        int sw = ui->scratch_width, ss = ui->scratch_scale;

        ui_kit_theme(themed ? table->theme : UI_THEME_LIGHT, themed ? table->accent : UI_ACCENT_STANDARD, 1);
        memory_zero(ui, sizeof(*ui));
        ui->engine = view->engine, ui->regular = view->face, ui->bold = view->bold ? view->bold : view->face;
        ui->scale = taskbar_scaled(view->scale), ui->opaque = 1;
        ui->pixels = pixels, ui->pitch = (unsigned long)taskbar_menu.width, ui->width = taskbar_menu.width;
        ui->height = ui->bottom = taskbar_menu.height;
        ui_inks(ui, compositor_tab_target(0) & 0xffffffu);
        ui->scratch = scratch, ui->scratch_width = sw, ui->scratch_scale = ss;
        if (pixels)
                ui_scratch_fit(ui, (unsigned int)taskbar_menu.width);
}

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
        struct taskbar_theme_saved theme;

        ui_popmenu_build(&taskbar_menu.menu, specs, count);
        taskbar_menu.width = taskbar_menu.height = 1;
        taskbar_theme_take(&theme);
        taskbar_menu_ui(view, null);
        ui_popmenu_size(&taskbar_menu.ui, &taskbar_menu.menu, view->height / 2);
        taskbar_theme_give(&theme);
        taskbar_menu.width = taskbar_menu.menu.width, taskbar_menu.height = taskbar_menu.menu.height;
        if (taskbar_menu.width <= 0 || taskbar_menu.height <= 0)
                return 0;
        int gap = taskbar_scaled(view->scale) * 6 / 100;
        struct ui_anchor anchor = {.anchored = true, .button = {rect[0] - gap, rect[1] - gap, rect[2] + 2 * gap, rect[3] + 2 * gap},
                                   .screen = {view->x, view->y, view->width, view->height}, .edge = (unsigned int)view->position};
        ui_anchor_beside(&anchor, taskbar_menu.width, taskbar_menu.height, &taskbar_menu.x, &taskbar_menu.y);
        return taskbar_menu.open = 1, taskbar_menu.dirty = 1, 1;
}

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
            {taskbar.clock_12h ? WORDS(tray_clock_24h) : WORDS(tray_clock_12h), null, 0, TASKBAR_MENU_CLOCK, false}};
        return taskbar_menu_build(view, rect, specs, (int)(sizeof(specs) / sizeof(specs[0])));
}

static fn taskbar_menu_draw(const struct taskbar_view *view, unsigned int address_to pixels)
{
        struct taskbar_theme_saved theme;

        taskbar_theme_take(&theme);
        taskbar_menu_ui(view, pixels);
        ui_clear(&taskbar_menu.ui);
        ui_kit.now = (b64)ui_kit_clock();
        ui_popmenu_draw(&taskbar_menu.ui, &taskbar_menu.menu);
        taskbar_theme_give(&theme);
        taskbar_menu.dirty = taskbar_menu.menu.moving;
}

static int taskbar_menu_hand(unsigned int type, unsigned int flags, int a, int b, int x, int y)
{
        if (!taskbar_menu.open)
                return -1;
        struct window_event event = {type, flags, a, b, 0};
        ui_kit.now = (b64)ui_kit_clock();
        int answer = ui_popmenu_event(&taskbar_menu.ui, &taskbar_menu.menu, &event, x - taskbar_menu.x, y - taskbar_menu.y);
        taskbar_menu.dirty = 1;
        return answer == UI_MENU_GONE ? -1 : answer > 0 ? answer : 0;
}

static int taskbar_menu_rect(int r[4]) { r[0] = taskbar_menu.x, r[1] = taskbar_menu.y, r[2] = taskbar_menu.width, r[3] = taskbar_menu.height; return taskbar_menu.open && r[2] > 0 && r[3] > 0; }
static int taskbar_menu_redraw(void) { return taskbar_menu.dirty; }
static void taskbar_menu_shut(void) { taskbar_menu.open = 0; }

/* Mission Control drawer slide tween and canvas painting (declared in ui.c, driven by compositor/glass.c). */
#define TASKBAR_MC_SLIDE_MS 220

static struct ui_tween taskbar_mc_slide;

static int taskbar_mc_tick(int open, int animate, int depth, int *moving)
{
        b32 motion = ui_kit.motion, live = 0;
        b64 want = open ? UI_ONE : 0, share;
        int push;

        ui_kit.now = ui_kit_clock(), ui_kit.motion = !animate;
        if (!taskbar_mc_slide.placed)
                ui_tween_put(&taskbar_mc_slide, 0, UI_TWEEN_NUMBER);
        if (want != taskbar_tween_goal(&taskbar_mc_slide))
                ui_kit_number_to(&taskbar_mc_slide, want, TASKBAR_MC_SLIDE_MS, &ui_ease);
        share = ui_tween_at(&taskbar_mc_slide, &live);
        if (moving)
                *moving = live || taskbar_tween_moving(&taskbar_mc_slide, ui_kit.now);
        ui_kit.motion = motion;
        if (share < 0)
                share = 0;
        if (share > UI_ONE)
                share = UI_ONE;
        push = (int)(((long long)depth * (long long)share + UI_ONE / 2) / (long long)UI_ONE);
        if (!open && !live)
                push = 0;
        return push;
}

static void taskbar_mc_draw(const struct taskbar_view *view, unsigned int *pixels, int buf_w, int buf_h, int screen, int scale)
{
#ifdef COMPOSITOR_ROWS
        struct ui ui;
        unsigned int pos = view->position;
        unsigned int base = compositor_tab_target(0) & 0xffffffu;
        int light = window_light(base);
        unsigned int accent = taskbar_accent() & 0xffffffu;
        unsigned int drawer_bg = light ? window_mix(base, 0xe8e8f0u, 190) : window_mix(base, 0x10131au, 210);
        unsigned int divider = window_mix(drawer_bg, window_contrast(drawer_bg) & 0xffffffu, 44);
        unsigned int ink = window_contrast(drawer_bg);
        int active_idx = compositor_spaces_active_index(screen);
        int radius = 6 * scale / 100;
        int label_h = (pos == TASKBAR_LEFT || pos == TASKBAR_RIGHT ? 18 : 20) * scale / 100;
        int border = 2 * scale / 100;

        if (!pixels || buf_w <= 0 || buf_h <= 0)
                return;
        if (!accent)
                accent = 0x5894f6u;
        if (radius < 4)
                radius = 4;
        if (border < 2)
                border = 2;
        memory_zero(&ui, sizeof(ui));
        ui.pixels = pixels, ui.pitch = (unsigned long)buf_w;
        ui.width = buf_w, ui.height = buf_h, ui.bottom = buf_h;
        ui.scale = scale, ui.engine = view->engine, ui.regular = view->face;
        ui_fill(&ui, 0, 0, buf_w, buf_h, 0xff000000u | drawer_bg);
        if (pos == TASKBAR_TOP)
                ui_fill(&ui, 0, buf_h - 1, buf_w, 1, 0xff000000u | divider);
        else if (pos == TASKBAR_LEFT)
                ui_fill(&ui, buf_w - 1, 0, 1, buf_h, 0xff000000u | divider);
        else if (pos == TASKBAR_RIGHT)
                ui_fill(&ui, 0, 0, 1, buf_h, 0xff000000u | divider);
        else
                ui_fill(&ui, 0, 0, buf_w, 1, 0xff000000u | divider);

        for (int ci = 0; ci < compositor.mc.card_count; ci++)
        {
                const struct compositor_mc_card *c = &compositor.mc.cards[ci];
                int is_active = !c->is_add && c->space_idx == active_idx;
                int is_hover = compositor.mc.hover_card == ci;
                int is_drop = compositor.mc.drop_target == ci;
                int is_fs = !c->is_add && compositor_spaces_kind_at(screen, c->space_idx) == COMPOSITOR_SPACE_FULLSCREEN;
                unsigned int frame_col, card_bg;

                if (is_drop || is_active)
                        frame_col = accent;
                else if (is_hover)
                        frame_col = window_mix(drawer_bg, ink & 0xffffffu, 110);
                else
                        frame_col = window_mix(drawer_bg, ink & 0xffffffu, 48);

                if (c->is_add)
                        card_bg = is_drop ? window_mix(drawer_bg, accent, 80) : is_hover ? window_mix(drawer_bg, ink & 0xffffffu, 36) : window_mix(drawer_bg, ink & 0xffffffu, 18);
                else if (is_fs)
                        card_bg = is_drop ? window_mix(0x141720u, accent, 64) : (light ? 0x262a34u : 0x0c0e14u);
                else
                        card_bg = is_drop ? window_mix(drawer_bg, accent, 72) : window_mix(light ? 0xd8dce6u : 0x1d222eu, accent, is_active ? 32 : 14);

                ui_round(&ui, c->x, c->y, c->w, c->h, radius, 0xff000000u | frame_col);
                {
                        int b = (is_active || is_drop) ? border : 1;
                        if (c->w > 2 * b && c->h > 2 * b)
                                ui_round(&ui, c->x + b, c->y + b, c->w - 2 * b, c->h - 2 * b, radius > b ? radius - b : 2, 0xff000000u | card_bg);
                }

                if (c->is_add)
                {
                        if (ui.engine && ui.regular)
                                ui_cell_line(&ui, c->x, c->y, c->w, c->h, "+", is_drop ? (0xff000000u | window_contrast(card_bg)) : ink, card_bg);
                        continue;
                }

                for (int ti = 0; ti < compositor.mc.thumb_count; ti++)
                {
                        const struct compositor_mc_thumb *tm = &compositor.mc.thumbs[ti];
                        int focused = tm->row == compositor.focus;
                        int dragged_now = (compositor.mc.dragging && tm->row == compositor.mc.press_row) ||
                                          (compositor.drag >= 0 && tm->row == compositor.drag);
                        unsigned int win_bg = focused ? window_mix(light ? 0xf6f7fau : 0x303748u, accent, 56)
                                                      : (light ? 0xebedf2u : 0x282e3cu);
                        unsigned int win_edge = focused ? accent : window_mix(win_bg, window_contrast(win_bg) & 0xffffffu, 72);
                        unsigned int bar_bg = window_mix(win_bg, focused ? accent : (window_contrast(win_bg) & 0xffffffu), focused ? 80 : 28);
                        int bar_h = 4 * scale / 100;

                        if (tm->card_idx != ci)
                                continue;
                        if (dragged_now)
                                win_bg = window_mix(card_bg, accent, 48), win_edge = accent;
                        if (bar_h < 2)
                                bar_h = 2;
                        if (bar_h > tm->h / 3)
                                bar_h = tm->h / 3;
                        ui_round(&ui, tm->x, tm->y, tm->w, tm->h, 2, 0xff000000u | win_edge);
                        if (tm->w > 2 && tm->h > 2)
                                ui_fill(&ui, tm->x + 1, tm->y + 1, tm->w - 2, tm->h - 2, 0xff000000u | win_bg);
                        if (tm->w > 2 && bar_h > 0)
                                ui_fill(&ui, tm->x + 1, tm->y + 1, tm->w - 2, bar_h, 0xff000000u | bar_bg);
                        if (ui.engine && ui.regular && tm->w >= 28 * scale / 100 && tm->h >= 16 * scale / 100 && compositor.title[tm->row][0])
                                ui_cell_line(&ui, tm->x + 2, tm->y + bar_h + 1, tm->w - 4, tm->h - bar_h - 2,
                                             compositor.title[tm->row], window_contrast(win_bg), win_bg);
                }

                if (c->removable)
                {
                        int close_hot = compositor.mc.hover_close == ci;
                        unsigned int cbg = close_hot ? 0xd94848u : window_mix(card_bg, window_contrast(card_bg) & 0xffffffu, is_hover ? 52 : 28);
                        ui_round(&ui, c->close_x, c->close_y, c->close_w, c->close_h, c->close_w / 2, 0xff000000u | cbg);
                        if (ui.engine && ui.regular)
                                ui_cell_line(&ui, c->close_x, c->close_y, c->close_w, c->close_h, "\xc3\x97",
                                             close_hot ? 0xffffffffu : window_contrast(cbg), cbg);
                }

                if (ui.engine && ui.regular)
                {
                        char label[8] = {0};
                        unsigned int l_ink = is_active || is_drop ? (0xff000000u | accent) : ink;

                        if (is_fs)
                                label[0] = 'F';
                        else if (c->space_idx + 1 < 10)
                                label[0] = (char)('1' + c->space_idx);
                        else
                                label[0] = (char)('0' + (c->space_idx + 1) / 10), label[1] = (char)('0' + (c->space_idx + 1) % 10);
                        ui_cell_line(&ui, c->x, c->y + c->h + 2, c->w, label_h - 2, label, l_ink, drawer_bg);
                }
        }
#else
        (void)view, (void)pixels, (void)buf_w, (void)buf_h, (void)screen, (void)scale;
#endif
}

/* Force stop: process-tree discovery and SIGSTOP/SIGKILL sweep. */
#define TASKBAR_PROC_MOST 8192
enum
{
        TASKBAR_PROC_PLAIN = 0u, TASKBAR_PROC_DESKTOP, TASKBAR_PROC_SYSTEM, TASKBAR_PROC_LAUNCHER, TASKBAR_PROC_LAUNCHER_HELPER, TASKBAR_PROC_REAPER,
        TASKBAR_WIN_SAME = 1u, TASKBAR_WIN_FOREIGN = 2u, TASKBAR_SIGKILL = 9u, TASKBAR_SIGSTOP = 19u
};

struct taskbar_proc { int pid, ppid; unsigned int kind; };

static const char *taskbar_proc_root = "/proc";
static struct taskbar_proc taskbar_procs[TASKBAR_PROC_MOST];
static int taskbar_stop_members[TASKBAR_PROC_MOST];

static void taskbar_signal_send(int pid, unsigned int sig) { if (pid > 2) compositor_call(syscall(kill), (positive)pid, sig, 0, 0, 0, 0); }
static void (*taskbar_signal)(int pid, unsigned int signal) = taskbar_signal_send;

static unsigned int taskbar_proc_kind(int self, int pid, const char *comm)
{
        static const struct { const char *name; unsigned char kind; } known[] = {
            {"init", TASKBAR_PROC_SYSTEM}, {"moonwater", TASKBAR_PROC_SYSTEM}, {"compositor", TASKBAR_PROC_SYSTEM}, {"audio", TASKBAR_PROC_SYSTEM},
            {"sessionbus", TASKBAR_PROC_SYSTEM}, {"bowl", TASKBAR_PROC_SYSTEM}, {"osk", TASKBAR_PROC_SYSTEM}, {"kthreadd", TASKBAR_PROC_SYSTEM},
            {"terminal", TASKBAR_PROC_DESKTOP}, {"settings", TASKBAR_PROC_DESKTOP}, {"launcher", TASKBAR_PROC_DESKTOP}, {"klog", TASKBAR_PROC_DESKTOP},
            {"mixer", TASKBAR_PROC_DESKTOP}, {"notices", TASKBAR_PROC_DESKTOP}, {"files", TASKBAR_PROC_DESKTOP}, {"gallery", TASKBAR_PROC_DESKTOP},
            {"editor", TASKBAR_PROC_DESKTOP}, {"identify", TASKBAR_PROC_DESKTOP},
            {"steam", TASKBAR_PROC_LAUNCHER}, {"lutris", TASKBAR_PROC_LAUNCHER}, {"heroic", TASKBAR_PROC_LAUNCHER}, {"bottles", TASKBAR_PROC_LAUNCHER},
            {"prismlauncher", TASKBAR_PROC_LAUNCHER}, {"itch", TASKBAR_PROC_LAUNCHER}, {"legendary", TASKBAR_PROC_LAUNCHER},
            {"steamwebhelper", TASKBAR_PROC_LAUNCHER_HELPER}, {"steamerrorrepor", TASKBAR_PROC_LAUNCHER_HELPER},
            {"reaper", TASKBAR_PROC_REAPER}, {"steam-launch-wr", TASKBAR_PROC_REAPER}, {"pv-bwrap", TASKBAR_PROC_REAPER},
            {"pressure-vessel", TASKBAR_PROC_REAPER}, {"gamescope", TASKBAR_PROC_REAPER}, {"srt-bwrap", TASKBAR_PROC_REAPER}, {"gamemoderun", TASKBAR_PROC_REAPER}};
        static const char x[] = "xwayland";
        int same = 1;

        if (pid <= 2 || pid == self)
                return TASKBAR_PROC_SYSTEM;
        for (int i = 0; same && x[i]; i++)
                same = (comm[i] | 32) == x[i];
        if (same)
                return TASKBAR_PROC_SYSTEM;
        for (int i = 0; i < (int)(sizeof(known) / sizeof(known[0])); i++)
                if (string_equals((string_address)comm, (string_address)known[i].name))
                        return known[i].kind;
        return TASKBAR_PROC_PLAIN;
}

static inline int taskbar_is_launcher(unsigned int k) { return k == TASKBAR_PROC_LAUNCHER || k == TASKBAR_PROC_LAUNCHER_HELPER; }

static int taskbar_proc_at(const struct taskbar_proc *t, int n, int pid)
{
        for (int i = 0; i < n; i++)
                if (t[i].pid == pid)
                        return i;
        return -1;
}

static int taskbar_apps_differ(const char *a, const char *b)
{
        if (!a || !a[0] || !b || !b[0])
                return 0;
        if ((ui_apps_same(a, "steam") || ui_apps_same(a, "steamwebhelper")) && (ui_apps_same(b, "steam") || ui_apps_same(b, "steamwebhelper")))
                return 0;
        return !ui_apps_same(a, b);
}

static int taskbar_pid_in_subtree(const struct taskbar_proc *t, int n, int pid, int root_pid)
{
        for (int at = taskbar_proc_at(t, n, pid), steps = 0; at >= 0 && steps++ < n; at = taskbar_proc_at(t, n, t[at].ppid))
                if (t[at].pid == root_pid)
                        return 1;
        return 0;
}

static unsigned int taskbar_subtree_window_flags(const struct taskbar_proc *t, int n, int sub_pid, int skip_pid, const char *target_app, int target_is_launcher)
{
        unsigned int flags = 0;
        if (!compositor.table || !compositor.showing || !compositor_popup_parent || !compositor.app_id)
                return 0;
        for (int row = 0; row < COMPOSITOR_ROWS; row++)
        {
                if (!compositor.showing[row] || compositor_popup_parent[row] >= 0)
                        continue;
                unsigned int wpid = __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE);
                if (wpid <= 2 || wpid > 0x7fffffffu || !taskbar_pid_in_subtree(t, n, (int)wpid, sub_pid) ||
                    (skip_pid > 0 && taskbar_pid_in_subtree(t, n, (int)wpid, skip_pid)))
                        continue;
                const char *wapp = compositor.app_id[row];
                int wat = taskbar_proc_at(t, n, (int)wpid);
                if (taskbar_apps_differ(wapp, target_app) || (target_is_launcher && (!wapp || !wapp[0]) && wat >= 0 && !taskbar_is_launcher(t[wat].kind)))
                        flags |= TASKBAR_WIN_FOREIGN;
                else if (target_app && target_app[0] && wapp && wapp[0])
                        flags |= TASKBAR_WIN_SAME;
        }
        return flags;
}

static int taskbar_stop_root_for(const struct taskbar_proc *t, int n, int pid, const char *target_app)
{
        int at = taskbar_proc_at(t, n, pid), below = at, steps = 0;
        if (at < 0 || t[at].kind == TASKBAR_PROC_SYSTEM)
                return 0;
        int is_launcher = taskbar_is_launcher(t[at].kind);
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
                if (!is_launcher && ((t[at].kind == TASKBAR_PROC_REAPER && t[up].kind != TASKBAR_PROC_REAPER) || taskbar_is_launcher(t[up].kind) ||
                                     (target_app && target_app[0] && (taskbar_subtree_window_flags(t, n, t[up].pid, t[at].pid, target_app, 0) & TASKBAR_WIN_FOREIGN))))
                        break;
                below = at, at = up;
        }
        return t[at].pid;
}

static int taskbar_stop_has(const int *members, int count, unsigned int pid)
{
        for (int i = 0; i < count; i++)
                if ((unsigned int)members[i] == pid)
                        return 1;
        return 0;
}

static int taskbar_stop_root(const struct taskbar_proc *t, int n, int pid) { return taskbar_stop_root_for(t, n, pid, null); }

static int taskbar_stop_tree_for(const struct taskbar_proc *t, int n, int root, int target_pid, const char *target_app, int *members, int most)
{
        int root_at = taskbar_proc_at(t, n, root), count = 0, grown = 1;
        if (root_at < 0 || t[root_at].kind == TASKBAR_PROC_SYSTEM || most <= 0)
                return 0;
        int target_at = target_pid > 0 ? taskbar_proc_at(t, n, target_pid) : root_at;
        int root_is_reaper = t[root_at].kind == TASKBAR_PROC_REAPER;
        int target_is_launcher = taskbar_is_launcher(t[root_at].kind) || (target_at >= 0 && taskbar_is_launcher(t[target_at].kind));

        members[count++] = root;
        while (grown)
        {
                grown = 0;
                for (int at = 0; at < n && count < most; at++)
                {
                        if (t[at].kind == TASKBAR_PROC_SYSTEM || !taskbar_stop_has(members, count, (unsigned int)t[at].ppid) ||
                            taskbar_stop_has(members, count, (unsigned int)t[at].pid) || (!root_is_reaper && t[at].kind == TASKBAR_PROC_REAPER))
                                continue;
                        if ((target_app && target_app[0]) || target_is_launcher)
                        {
                                unsigned int flags = taskbar_subtree_window_flags(t, n, t[at].pid, 0, target_app, target_is_launcher);
                                if ((flags & TASKBAR_WIN_FOREIGN) && !((target_pid > 0 && taskbar_pid_in_subtree(t, n, target_pid, t[at].pid)) || (flags & TASKBAR_WIN_SAME)))
                                        continue;
                        }
                        members[count++] = t[at].pid, grown = 1;
                }
        }
        return count;
}

static int taskbar_stop_tree(const struct taskbar_proc *t, int n, int root, int *members, int most) { return taskbar_stop_tree_for(t, n, root, 0, null, members, most); }

static int taskbar_proc_read(struct taskbar_proc *into, int most)
{
        char path[96], buffer[4096], text[512];
        int count = 0, self = (int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        bipolar handle = system_open_at(AT_FDCWD, (string_address)taskbar_proc_root, FILE_READ | O_DIRECTORY | O_CLOEXEC);

        if (handle < 0)
                return 0;
        for (bipolar got; (got = system_read_directory(handle, buffer, sizeof(buffer))) > 0;)
                for (positive at = 0; at < (positive)got;)
                {
                        const struct linux_dirent64 address_to rec = (const struct linux_dirent64 address_to)(buffer + at);
                        int pid = 0, ok = rec->d_name[0] >= '0' && rec->d_name[0] <= '9', open = -1, close = -1, ppid = 0;
                        char comm[24];
                        if (rec->d_reclen <= 19 || at + rec->d_reclen > (positive)got)
                                break;
                        at += rec->d_reclen;
                        for (int d = 0; ok && rec->d_name[d]; d++)
                                ok = rec->d_name[d] >= '0' && rec->d_name[d] <= '9' && d < 9, pid = pid * 10 + rec->d_name[d] - '0';
                        if (!ok || !pid)
                                continue;
                        taskbar_path_num(path, (int)sizeof(path), taskbar_proc_root, "/", (positive)pid, "/stat");
                        bipolar slurped = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)text, sizeof(text) - 1);

                        if (slurped <= 0)
                                continue;
                        text[slurped] = 0;
                        for (int i = 0; text[i]; i++)
                                open = open < 0 && text[i] == '(' ? i : open, close = text[i] == ')' ? i : close;
                        if (open < 0 || close < open || !text[close + 1] || !text[close + 2] || !text[close + 3])
                                continue;
                        int clen = close - open - 1 < (int)sizeof(comm) - 1 ? close - open - 1 : (int)sizeof(comm) - 1;
                        memory_copy(comm, text + open + 1, (unsigned long)clen), comm[clen] = 0;
                        for (int i = close + 4; text[i] >= '0' && text[i] <= '9' && i < close + 14; i++)
                                ppid = ppid * 10 + text[i] - '0';
                        if (count >= most)
                                return system_close(handle), -1;
                        into[count++] = (struct taskbar_proc){pid, ppid, taskbar_proc_kind(self, pid, comm)};
                }
        system_close(handle);
        return count;
}

static int taskbar_stop_plan_for(unsigned int pid, const char *target_app, int *root)
{
        *root = 0;
        if (pid <= 2 || pid > 0x7fffffffu)
                return 0;
        int n = taskbar_proc_read(taskbar_procs, TASKBAR_PROC_MOST);
        if (n <= 0)
                return 0;
        *root = taskbar_stop_root_for(taskbar_procs, n, (int)pid, target_app);
        return *root ? taskbar_stop_tree_for(taskbar_procs, n, *root, (int)pid, target_app, taskbar_stop_members, TASKBAR_PROC_MOST) : 0;
}

static int taskbar_stop_rows(int count)
{
        int windows = 0;
        for (int row = 0; compositor.table && row < COMPOSITOR_ROWS; row++)
                if (compositor.showing[row] && compositor_popup_parent[row] < 0)
                        windows += taskbar_stop_has(taskbar_stop_members, count, __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE));
        return windows;
}

static int taskbar_stop_run_for(unsigned int pid, const char *target_app)
{
        static int stopped[TASKBAR_PROC_MOST];
        int count = 0, root;

        for (int pass = 0; pass < 8; pass++)
        {
                int members = taskbar_stop_plan_for(pid, target_app, &root), fresh = 0;
                for (int at = 0; at < members && count < TASKBAR_PROC_MOST; at++)
                        if (!taskbar_stop_has(stopped, count, (unsigned int)taskbar_stop_members[at]))
                                stopped[count++] = taskbar_stop_members[at], fresh++, taskbar_signal(taskbar_stop_members[at], TASKBAR_SIGSTOP);
                if (!members || !fresh)
                        break;
        }
        for (int at = 0; at < count; at++)
                taskbar_signal(stopped[at], TASKBAR_SIGKILL);
        return count;
}

/* Pinned apps, starting spinners, and tab context menu. */
static struct
{
        int count, ghosts, ghost_tab[SETTING_PINS_ENTRIES], ghost_pin[SETTING_PINS_ENTRIES];
        struct taskbar_pin pin[SETTING_PINS_ENTRIES];
        positive started[SETTING_PINS_ENTRIES];
        unsigned int runs;
} taskbar_pins;

static const char *taskbar_run_root = "/run/moonwater";

static const char *taskbar_pin_word(const struct taskbar_pin *pin, int at) { return pin->text + pin->at[at]; }
static inline const char *taskbar_app_title(const struct ui_app_entry address_to a) { return a->kit ? language_of(ui_kit_programs[a->kit - 1].label) : a->name; }
static inline bool taskbar_pin_runs(const struct taskbar_pin *p, const char *app) { return app && app[0] && (ui_apps_same(app, p->key) || ui_apps_same(app, p->app)); }

static bool taskbar_pin_put(struct taskbar_pin *pin, positive *used, const char *word)
{
        positive length = string_length((string_address)word) + 1;
        if (pin->count >= SETTING_PINS_WORDS || *used + length > sizeof(pin->text) ||
            string_first_of((string_address)word, '\n') || string_first_of((string_address)word, '\r'))
                return false;
        pin->at[pin->count++] = (unsigned short)*used;
        memory_copy(pin->text + *used, word, length), *used += length;
        return true;
}

static bool taskbar_app_showing(const char *key, const char *id)
{
        for (int row = 0; row < COMPOSITOR_ROWS; row++)
                if (compositor.showing[row] && (ui_apps_same(compositor.app_id[row], key) || ui_apps_same(compositor.app_id[row], id)))
                        return true;
        return false;
}

static bool taskbar_pin_of(int which, struct taskbar_pin *pin)
{
        positive used = 0;
        memory_zero(pin, sizeof(*pin));
        if (!ui_apps_pinnable(which))
                return false;
        const struct ui_app_entry *app = &ui_apps.app[which];
        string_copy_bounded((p8 *)pin->app, (string_address)app->id, sizeof(pin->app));
        string_copy_bounded((p8 *)pin->key, (string_address)app->key, sizeof(pin->key));
        string_copy_bounded((p8 *)pin->title, (string_address)taskbar_app_title(app), sizeof(pin->title));
        return taskbar_pin_put(pin, &used, app->id) && taskbar_pin_put(pin, &used, app->source) && taskbar_pin_put(pin, &used, app->launch);
}

static int taskbar_pin_find(const char *app)
{
        for (int i = 0; i < taskbar_pins.count; i++)
                if (string_equals((string_address)taskbar_pins.pin[i].app, (string_address)app))
                        return i;
        return -1;
}

/* The pins file is read by the kit's registry (ui_apps_pinned_list): each pinned entry, in the file's order, once. The registry is
   looked at only when the file names something, as it was before the parse was the registry's: a file of none or none at all leaves it
   as it is. */
static void taskbar_pins_read(void)
{
        char path[160], line[SETTING_PINS_MOST + 16];
        int which[SETTING_PINS_ENTRIES], listed;

        taskbar_pins.count = 0, taskbar.stale = 1;
        if (!ui_apps_pins_read(compositor_settings_root, path, sizeof(path), line, sizeof(line)) || !line[0] ||
            string_equals((string_address)line, (string_address) "none"))
                return;
        ui_apps_fresh((b64)ui_kit_time());
        listed = ui_apps_pinned_list(compositor_settings_root, which, SETTING_PINS_ENTRIES);
        for (int i = 0; i < listed; i++)
                if (taskbar_pin_find(ui_apps.app[which[i]].id) < 0 && taskbar_pin_of(which[i], &taskbar_pins.pin[taskbar_pins.count]))
                        taskbar_pins.count++;
}

static void taskbar_pin_keep(int which, bool on)
{
        if (ui_apps_pin(compositor_settings_root, which, on) > 0)
                taskbar_pins_read(), compositor_tabs_changed();
}

static struct
{
        int count;
        char id[WINDOWS_STARTING][WINDOWS_APP_ID], title[WINDOWS_STARTING][TASKBAR_PIN_TITLE];
        b32 pinned[WINDOWS_STARTING];
        positive drawn;
} taskbar_starting;

static bool taskbar_starting_take(positive now)
{
        char ids[WINDOWS_STARTING][WINDOWS_APP_ID], held[WINDOWS_APP_ID];
        int count = 0;

        for (unsigned int slot = 0; compositor.table && slot < WINDOWS_STARTING; slot++)
        {
                unsigned long long stamp = windows_starting_read(compositor.table, slot, held);
                if (!stamp)
                        continue;
                int which = ui_apps_find(held);
                if (which < 0 || now - stamp > WINDOWS_STARTING_NS || taskbar_app_showing(ui_apps.app[which].key, ui_apps.app[which].id))
                        windows_starting_done(compositor.table, slot);
                else
                        string_copy_bounded((p8 *)ids[count++], (string_address)held, WINDOWS_APP_ID);
        }
        bool changed = count != taskbar_starting.count;
        for (int i = 0; i < count && !changed; i++)
                changed = !string_equals((string_address)ids[i], (string_address)taskbar_starting.id[i]);
        if (!changed)
                return false;
        taskbar_starting.count = count;
        for (int i = 0; i < count; i++)
        {
                const struct ui_app_entry address_to app = &ui_apps.app[ui_apps_find(ids[i])];
                string_copy_bounded((p8 *)taskbar_starting.id[i], (string_address)ids[i], WINDOWS_APP_ID);
                string_copy_bounded((p8 *)taskbar_starting.title[i], (string_address)taskbar_app_title(app), TASKBAR_PIN_TITLE);
        }
        return true;
}

static positive taskbar_pictures_looked; // the loop's last pass while an app's picture was made off the draw (ui_apps_picture_kept)
static positive taskbar_spin_due(void) { return taskbar_starting.count ? taskbar_starting.drawn + TASKBAR_STARTING_FRAME : 0; }
// When the loop is to pass again: the next frame of a starting app's turn, and a frame on while an app's picture is being made.
static positive taskbar_starting_due(void)
{
        positive due = taskbar_spin_due(), look = ui_apps_picture_making() ? taskbar_pictures_looked + TASKBAR_STARTING_FRAME : 0;
        return !due || (look && look < due) ? look : due;
}
// Asked on every pass of the loop before a frame: true when the band is to be made again, for a start shown or turned, or for an
// app's picture made off the draw and taken now (its task is laid out with it: the tree keys on ui_apps_pictures_made).
static bool taskbar_starting_keep(positive now)
{
        b32 picture = ui_apps_picture_take();
        taskbar_pictures_looked = now;
        if (!picture && !taskbar_starting_take(now) && !(taskbar_starting.count && now >= taskbar_spin_due()))
                return false;
        return taskbar_starting.drawn = now, true;
}

static void taskbar_ghost_add(struct taskbar_view *view, unsigned char *marks, unsigned char *lengths, unsigned int *rows,
                              unsigned int at, const char *title, const char *app, unsigned char mark)
{
        view->titles[at] = title, view->apps[at] = app, view->icons[at] = null, view->icon_seqs[at] = 0;
        marks[at] = mark, lengths[at] = compositor_title_length(title, TASKBAR_PIN_TITLE), rows[at] = TASKBAR_GHOST;
}

static unsigned int taskbar_pins_tabs(struct taskbar_view *view, unsigned int room, unsigned char *marks, unsigned char *lengths, unsigned int *rows)
{
        unsigned int added = 0;

        taskbar_pins.ghosts = 0;
        if (compositor.band_at == 0 && ui_apps_fresh((b64)ui_kit_time()))
                taskbar_pins_read();
        if (compositor.band_at != 0 || !compositor.table)
                return 0;
        memory_zero(taskbar_starting.pinned, sizeof(taskbar_starting.pinned));
        for (int i = 0; i < taskbar_pins.count && added < room; i++)
        {
                const struct taskbar_pin *pin = &taskbar_pins.pin[i];
                if (taskbar_app_showing(pin->key, pin->app))
                        continue;
                bool starting = false;
                for (int s = 0; s < taskbar_starting.count && !starting; s++)
                        if (string_equals((string_address)taskbar_starting.id[s], (string_address)pin->app))
                                starting = taskbar_starting.pinned[s] = true;
                unsigned int at = view->count + added++;
                taskbar_ghost_add(view, marks, lengths, rows, at, pin->title, pin->app, (unsigned char)(COMPOSITOR_TAB_AWAY | (starting ? COMPOSITOR_TAB_STARTING : 0)));
                taskbar_pins.ghost_tab[taskbar_pins.ghosts] = (int)at, taskbar_pins.ghost_pin[taskbar_pins.ghosts++] = i;
        }
        for (int i = 0; i < taskbar_starting.count && added < room; i++)
                if (!taskbar_starting.pinned[i])
                        taskbar_ghost_add(view, marks, lengths, rows, view->count + added++, taskbar_starting.title[i],
                                          taskbar_starting.id[i], COMPOSITOR_TAB_AWAY | COMPOSITOR_TAB_STARTING);
        return added;
}

static int taskbar_pin_of_tab(int tab)
{
        for (int i = 0; i < taskbar_pins.ghosts; i++)
                if (taskbar_pins.ghost_tab[i] == tab)
                        return taskbar_pins.ghost_pin[i];
        return -1;
}

static void taskbar_pin_start(int tab, positive now)
{
        int which = taskbar_pin_of_tab(tab);
        if (which < 0 || (taskbar_pins.started[which] && now - taskbar_pins.started[which] < TASKBAR_PIN_AGAIN))
                return;
        const struct taskbar_pin *pin = &taskbar_pins.pin[which];
        for (unsigned int attempt = 0; attempt < 4; attempt++)
        {
                char path[96], name[32] = "pin_start_";
                positive number = ((now / 1000000ull) & 0x7fffffu) << 6 | (taskbar_pins.runs++ & 63u);
                taskbar_path_num(path, (int)sizeof(path), taskbar_run_root, "/pin.", number, "");
                bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
                if (handle < 0)
                        continue;
                for (int w = 2; w < pin->count; w++)
                {
                        const char *t = taskbar_pin_word(pin, w);
                        system_write_all((b32)handle, t, string_length((string_address)t));
                        system_write_all((b32)handle, "\n", 1);
                }
                system_close(handle);
                positive at = 10 + positive_into_string((p8 *)name + 10, number);
                name[at++] = ' ', name[at + positive_into_string((p8 *)name + at, (positive)which)] = 0;
                taskbar_pins.started[which] = now;
                windows_starting_set(compositor.table, pin->app, now);
                compositor_script_say(name);
                return;
        }
}

static int taskbar_tab_menu_open(const struct taskbar_view *view, int tab)
{
        int rect[4], root, pinned = -1, pin_id = 0;
        const char *pin_label = null;

        if (taskbar.editing || !view->rows || tab < 0 || tab >= (int)view->count)
                return 0;
        int ghost = view->rows[tab] == TASKBAR_GHOST, row = ghost ? -1 : (int)view->rows[tab];
        if (ghost ? taskbar_pin_of_tab(tab) < 0 : (row < 0 || row >= COMPOSITOR_ROWS || !compositor.table))
                return 0;
        unsigned int pid = ghost ? 0 : __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE);
        if (!taskbar_menu_begin(view, tab, row, pid, rect))
                return 0;
        memory_zero(&taskbar_menu.pin, sizeof(taskbar_menu.pin));
        if (ghost)
        {
                const struct ui_menu_spec specs[] = {{WORDS(taskbar_tab_unpin), null, 0, TASKBAR_MENU_UNPIN, false}};
                string_copy_bounded((p8 *)taskbar_menu.pin.app, (string_address)taskbar_pins.pin[taskbar_pin_of_tab(tab)].app, sizeof(taskbar_menu.pin.app));
                return taskbar_menu_build(view, rect, specs, 1);
        }
        /* The row's pid is a client's claim: a tree is planned only for the process the compositor took for this row. */
        int members = windows_owner_ok(&windows_owner_held[row], pid) ? taskbar_stop_plan_for(pid, compositor.app_id[row], &root) : 0;
        int windows = members ? taskbar_stop_rows(members) : 0;
        int which = ui_apps_match(compositor.app_id[row]);
        if (windows > 1)
                language_format(taskbar_menu.stop, sizeof(taskbar_menu.stop), WORDS(taskbar_tab_stop_windows), (long)windows);
        for (int i = 0; i < taskbar_pins.count && pinned < 0; i++)
                if (taskbar_pin_runs(&taskbar_pins.pin[i], compositor.app_id[row]))
                        pinned = i;
        if (pinned >= 0)
                string_copy_bounded((p8 *)taskbar_menu.pin.app, (string_address)taskbar_pins.pin[pinned].app, sizeof(taskbar_menu.pin.app)),
                    pin_label = WORDS(taskbar_tab_unpin), pin_id = TASKBAR_MENU_UNPIN;
        else if (which >= 0 && taskbar_pins.count < SETTING_PINS_ENTRIES && taskbar_pin_of(which, &taskbar_menu.pin))
                pin_label = WORDS(taskbar_tab_pin), pin_id = TASKBAR_MENU_PIN;
        else
                memory_zero(&taskbar_menu.pin, sizeof(taskbar_menu.pin));
        const struct ui_menu_spec specs[] = {
            {WORDS(taskbar_tab_exit), null, 0, TASKBAR_MENU_EXIT, false},
            {windows > 1 ? taskbar_menu.stop : WORDS(taskbar_tab_stop), null, 0, TASKBAR_MENU_STOP, !members},
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {pin_label, null, 0, pin_id, !pin_label},
            {WORDS(taskbar_tab_uninstall), null, 0, TASKBAR_MENU_UNINSTALL, !(which >= 0 && ui_apps_uninstallable(which))}};
        return taskbar_menu_build(view, rect, specs, (int)(sizeof(specs) / sizeof(specs[0])));
}

static void taskbar_tab_chose(int id)
{
        int row = taskbar_menu.row;
        unsigned int pid = taskbar_menu.pid;

        if (id == TASKBAR_MENU_UNPIN)
        {
                taskbar_pin_keep(ui_apps_find(taskbar_menu.pin.app), false);
                return;
        }
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.table || !compositor.showing[row] ||
            __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE) != pid)
                return;
        int which = ui_apps_match(compositor.app_id[row]);
        if (id == TASKBAR_MENU_EXIT)
                compositor_close(row);
        else if (id == TASKBAR_MENU_STOP)
        {
                /* Stop signals processes: only the one the compositor took for this row (its owner, by start time) may be the root. */
                if (windows_owner_ok(&windows_owner_held[row], pid))
                        taskbar_stop_run_for(pid, compositor.app_id[row]);
        }
        else if (id == TASKBAR_MENU_PIN && taskbar_menu.pin.app[0] && which >= 0 && which == ui_apps_find(taskbar_menu.pin.app))
                taskbar_pin_keep(which, true);
        else if (id == TASKBAR_MENU_UNINSTALL && which >= 0 && ui_apps_uninstallable(which))
                taskbar_pin_keep(which, false), ui_apps_uninstall(compositor_settings_root, which, ui_faces()->face, ui_faces()->bold);
}

/* Band arrangement edit mode. */
static int taskbar_edit_item(const struct taskbar_view *view, int px, int py)
{
        int which;
        if (!taskbar.editing || !taskbar_hit_raw(view, px, py, (int[2]){0, 0}, &which))
                return TASKBAR_HIT_NONE;
        return which >= 0 || which == TASKBAR_LAUNCHER || which == TASKBAR_DONE || taskbar_item_of(which) >= 0 ? which : TASKBAR_HIT_NONE;
}

static int taskbar_edit_along(const struct taskbar_view *view, int which)
{
        const struct ui_node address_to n = taskbar_node(view, which), address_to band = &taskbar_tree(view)->node[1];
        return n ? (band->dir ? n->y + n->h / 2 : n->x + n->w / 2) : -1;
}

static int taskbar_edit_drop(const struct taskbar_view *view, int which, int px, int py)
{
        const struct ui_node address_to band = &taskbar_tree(view)->node[1];
        int along = band->dir ? py : px, zone = TASKBAR_ZONE_CENTRE, best = -1, moon = taskbar_edit_along(view, TASKBAR_LAUNCHER);
        int moon_zone = taskbar.moon_zone, tasks_zone = taskbar.tasks_zone;
        unsigned int before = taskbar.tray_before;

        if (!taskbar.editing)
                return 0;
        for (int z = TASKBAR_ZONE_START; z <= TASKBAR_ZONE_END; z++)
        {
                const struct ui_node address_to n = taskbar_node(view, TASKBAR_ZONE - z);
                if (!n)
                        continue;
                int from = band->dir ? n->y : n->x, to = from + (band->dir ? n->h : n->w);
                int apart = along < from ? from - along : along >= to ? along - to + 1 : 0;
                if (best < 0 || apart < best)
                        best = apart, zone = z;
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
                if ((tasks = tasks < 0 ? moon : tasks) < 0)
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

static int taskbar_edit_words(const char *paths[TASKBAR_EDIT_ROWS], const char *words[TASKBAR_EDIT_ROWS])
{
        static const char address_to const places[3] = {"start", "center", "end"};
        static const char address_to const items[6] = {SETTING_TASKBAR_ITEM_NOTICES_PATH, SETTING_TASKBAR_ITEM_MIC_PATH, SETTING_TASKBAR_ITEM_SOUND_PATH,
                                                       SETTING_TASKBAR_ITEM_BATTERY_PATH, SETTING_TASKBAR_ITEM_TEXT_PATH, SETTING_TASKBAR_ITEM_CLOCK_PATH};
        int count = 0;

        paths[count] = SETTING_TASKBAR_MOON_PATH, words[count++] = places[taskbar.moon_zone >= 0 && taskbar.moon_zone < 3 ? taskbar.moon_zone : 1];
        paths[count] = SETTING_TASKBAR_TASKS_PATH;
        words[count++] = taskbar.tasks_zone == TASKBAR_ZONE_SPLIT ? "split" : places[taskbar.tasks_zone >= 0 && taskbar.tasks_zone < 3 ? taskbar.tasks_zone : 1];
        for (int i = 0; i < 6; i++)
                paths[count] = items[i], words[count++] = taskbar.tray_before >> i & 1 ? "before" : "after";
        return count;
}
