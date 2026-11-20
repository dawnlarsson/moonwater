/*
        The windows: the stack and which has the keys, the grips and the edges, the dividers windows share, tiles, the
        grid and the snap's overlays, the room the bands leave and a window fitted to it, the places kept for a program's
        windows, a row taken and let go (compositor_take, compositor_leave), the keyboard on the screen, the clipboard and
        the popups a window opens.

        What a window is and where; not what a person did (input.c turns a press, a drag or a key into the calls here),
        not how it is drawn (compose.c, present.c) and not what changed (damage.c, which it tells). It reads the
        compositor's state and nearly every part before it; what it asks of input.c is declared before the call.
        compositor_identity_take is the kit's. Called by input.c above all, by kms.c (a space taken again carries the
        windows and fits them), session.c (the keyboard's and the clipboard's settings), damage.c and present.c (the
        overlays), compositor.c's table and loop (a row's door, the clipboard's socket) and wayland_host.c.
*/

static void compositor_stack_remove(int row)
{
        unsigned int at = 0;

        while (at < compositor.stacked && compositor.stack[at] != row)
                at++;

        if (at >= compositor.stacked)
                return;

        memory_copy(compositor.stack + at, compositor.stack + at + 1, (compositor.stacked - at - 1) * sizeof(*compositor.stack));
        compositor.stacked--;
        if (!++compositor.stack_gen)
                compositor.stack_gen++;
        compositor_row_notify_stack(row);
}

/*
        Protocol-neutral row lifecycle observer: X11, Wayland, and other protocol bridges subscribe
        directly to Canvas row detach, focus, stack, and configure events instead of polling.
*/
#define COMPOSITOR_ROW_HOOKS_MAX 8

struct compositor_row_hooks
{
        void *ctx;
        void (*detach)(void *ctx, int row);
        void (*focus)(void *ctx, int old_row, int new_row);
        void (*stack)(void *ctx, int row);
        void (*configure)(void *ctx, int row, int x, int y, int width, int height);
};

static const struct compositor_row_hooks *compositor_row_hook_list[COMPOSITOR_ROW_HOOKS_MAX];
static unsigned int compositor_row_hook_count;

static int compositor_row_hooks_register(const struct compositor_row_hooks *hooks)
{
        if (!hooks || compositor_row_hook_count >= COMPOSITOR_ROW_HOOKS_MAX)
                return -1;
        for (unsigned int i = 0; i < compositor_row_hook_count; i++)
                if (compositor_row_hook_list[i] == hooks)
                        return 0;
        compositor_row_hook_list[compositor_row_hook_count++] = hooks;
        return 0;
}

static void compositor_row_hooks_unregister(const struct compositor_row_hooks *hooks)
{
        for (unsigned int i = 0; i < compositor_row_hook_count; i++)
                if (compositor_row_hook_list[i] == hooks)
                {
                        compositor_row_hook_list[i] = compositor_row_hook_list[--compositor_row_hook_count];
                        return;
                }
}

static void compositor_row_notify_stack(int row)
{
        for (unsigned int i = 0; i < compositor_row_hook_count; i++)
                if (compositor_row_hook_list[i]->stack)
                        compositor_row_hook_list[i]->stack(compositor_row_hook_list[i]->ctx, row);
}

static void compositor_row_notify_focus(int old_row, int new_row)
{
        for (unsigned int i = 0; i < compositor_row_hook_count; i++)
                if (compositor_row_hook_list[i]->focus)
                        compositor_row_hook_list[i]->focus(compositor_row_hook_list[i]->ctx, old_row, new_row);
}

static void compositor_row_notify_detach(int row)
{
        for (unsigned int i = 0; i < compositor_row_hook_count; i++)
                if (compositor_row_hook_list[i]->detach)
                        compositor_row_hook_list[i]->detach(compositor_row_hook_list[i]->ctx, row);
}

static void compositor_row_notify_configure(int row, int x, int y, int width, int height)
{
        for (unsigned int i = 0; i < compositor_row_hook_count; i++)
                if (compositor_row_hook_list[i]->configure)
                        compositor_row_hook_list[i]->configure(compositor_row_hook_list[i]->ctx, row, x, y, width, height);
}

static signed char compositor_layer_clamp(int layer)
{
        return layer < WINDOW_LAYER_BACKGROUND ? WINDOW_LAYER_BACKGROUND
               : layer > WINDOW_LAYER_OVERLAY  ? WINDOW_LAYER_OVERLAY
                                               : (signed char)layer;
}

static int compositor_popup_root(int row);

// Effective stacking band of a row: its declared layer, inheriting at least its popup parent chain's layer
// (so context menus opened from overlays like the launcher always stack above them), or WINDOW_LAYER_ABOVE
// for small non-fullscreen override-redirect X11 menus/popups.
static signed char compositor_layer_of(int row)
{
        signed char layer = compositor.layer[row];

        if (compositor_popup_parent[row] >= 0)
        {
                int parent = compositor_popup_parent[row], hops = 0;

                while (parent >= 0 && parent < (int)compositor_rows_in_use && hops++ < (int)compositor_rows_in_use)
                {
                        signed char pl = compositor.layer[parent];

                        if (pl == WINDOW_LAYER_NORMAL && (compositor.asks[parent] & (WINDOW_DISMISS | WINDOW_NO_CLOSE)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE))
                                pl = WINDOW_LAYER_OVERLAY;
                        if (pl > layer)
                                layer = pl;
                        parent = compositor_popup_parent[parent];
                }
        }
        if (layer == WINDOW_LAYER_NORMAL && compositor_row_xoverride(row) && !compositor.fullscreen[row] &&
            !((int)compositor.w[row] >= compositor.width && (int)compositor.h[row] >= compositor.height))
                return WINDOW_LAYER_ABOVE;
        if (layer == WINDOW_LAYER_NORMAL && (compositor.asks[row] & (WINDOW_DISMISS | WINDOW_NO_CLOSE)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE))
                return WINDOW_LAYER_OVERLAY;
        return layer;
}

// Single-row raise inside its stacking layer band (BACKGROUND < NORMAL < ABOVE < OVERLAY).
static void compositor_raise_one(int row)
{
        signed char mine = compositor_layer_of(row);
        unsigned int at = 0;

        compositor_stack_remove(row);
        while (at < compositor.stacked && compositor_layer_of((int)compositor.stack[at]) <= mine)
                at++;
        if (at < compositor.stacked)
                memory_copy(compositor.stack + at + 1, compositor.stack + at, (compositor.stacked - at) * sizeof(*compositor.stack));
        compositor.stack[at] = (unsigned int)row;
        compositor.stacked++;
        if (!++compositor.stack_gen)
                compositor.stack_gen++;
        compositor_row_notify_stack(row);
        compositor_damage_row(row);
}

// On top of its own stacking layer band, keeping owned transient/modal dialogs and open popups above their owner.
static void compositor_raise(int row)
{
        compositor_raise_one(row);
        if (compositor_popup_parent[row] >= 0)
                return;
        for (int child = 0; child < (int)compositor_rows_in_use; child++)
                if (child != row && compositor_owner[child] == row + 1 && compositor_visible(child) && compositor_popup_parent[child] < 0)
                        compositor_raise_one(child);
        for (int child = 0; child < (int)compositor_rows_in_use; child++)
                if (child != row && compositor_popup_parent[child] >= 0 && compositor_popup_root(child) == row && compositor_visible(child))
                        compositor_raise_one(child);
}

static unsigned int compositor_modifiers(void);

/*
        The keys that are down on the window that has the keyboard, by code.
        A key's repeats and its release follow its press, so a key held when
        the keyboard goes to another window is over for the one it was sent
        to: its release is sent there (its press was), and nothing further of
        it goes to the window that has the keys now, which never saw it go down.
*/
static void compositor_keys_release(void)
{
        int to = compositor.keys_to;

        compositor.keys_to = COMPOSITOR_NONE;
        if (to < 0)
                return;

        for (unsigned int code = 0; code < 256; code++)
                if (compositor.keys_down[code >> 3] & (1u << (code & 7)))
                        compositor_post(to, WINDOW_EVENT_KEY, compositor_modifiers(), (int)code, 0);
        memory_zero(compositor.keys_down, sizeof(compositor.keys_down));
}

// The keys to a window, or to none; both are knocked on, and read focus in the table.
static void compositor_focus(int row)
{
        int was = compositor.focus;

        // A popup never takes the keys from its window; the window that loses them loses its popups. A window that asked for no
        // keys (WINDOW_NO_KEYS, the on-screen keyboard) is never given them, by a press on it or by anything else.
        if (was == row || (row >= 0 && (compositor_popup_parent[row] >= 0 || (compositor.asks[row] & WINDOW_NO_KEYS))))
                return;
        if (was >= 0)
                compositor_popups_close(was, -1);

        compositor.focus = row;
        compositor.table->focus = row < 0 ? WINDOWS : (unsigned int)row;
        compositor_row_notify_focus(was, row);
        if (compositor.keys_to != row)
                compositor_keys_release();
        // Both are knocked on, and both lines drawn again: the one with the keys is lit.
        if (was >= 0)
        {
                rows_add(&compositor.woken, was);
                compositor_damage_row(was);
        }
        if (row >= 0)
        {
                rows_add(&compositor.woken, row);
                compositor_damage_row(row);
        }
        compositor_tabs_changed();
        compositor_osk_sync();
}

// Whether a window is in the focus cycle: one that asked for no tab (WINDOW_NO_TAB, the identify overlays) is not, so
// neither Alt-Tab nor the refocus gives it the keys. Asked for them by its own program, it has them as any window does.
static b32 compositor_cycles(int row)
{
        return !(compositor.asks[row] & WINDOW_NO_TAB);
}

// The topmost window there is to give the keys to; none that is put away.
static void compositor_refocus(void)
{
        int next = COMPOSITOR_NONE;

        for (unsigned int at = compositor.stacked; at-- && next < 0;)
                if (compositor_visible(compositor.stack[at]) && compositor_popup_parent[compositor.stack[at]] < 0 &&
                    !(compositor.state[compositor.stack[at]] & WINDOW_CLOSING) && compositor_cycles(compositor.stack[at]))
                        next = compositor.stack[at];

        compositor_focus(next);
}

// A window's screen, by the rule of compositor_screen_owner: the one that holds the most of it.
static int compositor_screen_of(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        return count < 2 ? 0
                         : compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                                   (int)compositor.h[row]);
}

/*
        The corner of a window a finger is on, its two edges (CP_EDGE_*), or 0. A corner's own shape is the pointer's, grip
        outside the window and in inside it each way; a finger's reach is that shape grown to at least least each way
        (ui_reach_hit, UI_TARGET at the scale), about its middle, which is where the finger's whole hand lands on a corner. A
        window too small for two corners' targets apart gives the point to the corner nearest it.
*/
static unsigned int compositor_corner_reach(int px, int py, int x1, int y1, int x2, int y2, int grip, int in, int least)
{
        unsigned int found = 0;
        long long best = -1;

        for (unsigned int corner = 0; corner < 4; corner++)
        {
                int right = corner & 1, down = corner >> 1 & 1;
                int cx = right ? x2 : x1, cy = down ? y2 : y1;
                int x = right ? x2 - in : x1 - grip, y = down ? y2 - in : y1 - grip;
                long long dx = px - cx, dy = py - cy, far = dx * dx + dy * dy;

                if (!ui_reach_hit(1, least, px, py, x, y, grip + in, grip + in) || (best >= 0 && far >= best))
                        continue;
                best = far;
                found = (right ? CP_EDGE_RIGHT : CP_EDGE_LEFT) | (down ? CP_EDGE_BOTTOM : CP_EDGE_TOP);
        }
        return found;
}

// How far outside a window's edge a pointer's press resizes it, in logical pixels: `moonwater canvas border grip`, or COMPOSITOR_GRIP.
static int compositor_grip(void)
{
        return compositor.grip ? compositor.grip : COMPOSITOR_GRIP;
}

static unsigned int compositor_resizable_axes(int row);

// Whether a point is in the band at a window's edges that resizes it -- just inside its contents, its line,
// and past it -- and which edges.
static unsigned int compositor_edge_zone(int row, int x, int y)
{
        unsigned int axes;

        // A menu is not resized: its edges are no grip, for a pointer or a finger, and draw no indicator (none of
        // what is outside it is the menu's to take).
        if (compositor_popup_parent[row] >= 0)
                return 0;
        axes = compositor_resizable_axes(row);
        if (!axes)
                return 0;
        // A finger's are wider (COMPOSITOR_TOUCH_GRIP), its corners further along each edge, and its top a pointer's.
        int touch = compositor.touching;
        int grip = (touch ? COMPOSITOR_TOUCH_GRIP : compositor_grip()) * compositor.scale / 100 + compositor_line(row);
        int in = (touch ? COMPOSITOR_TOUCH_GRIP_IN : COMPOSITOR_GRIP_IN) * compositor.scale / 100;
        int x2 = compositor.x[row] + (int)compositor.w[row], y2 = compositor.y[row] + (int)compositor.h[row];
        unsigned int edges = compositor_edges_at(x, y, compositor.x[row], compositor.y[row], x2, y2, grip, in,
                                                 COMPOSITOR_GRIP_IN * compositor.scale / 100,
                                                 touch ? COMPOSITOR_TOUCH_CORNER * compositor.scale / 100 : 0);

        // A finger's corner is a target of UI_TARGET each way, and wins where it is: a corner is the window's own resize.
        if (touch)
        {
                unsigned int corner = compositor_corner_reach(x, y, compositor.x[row], compositor.y[row], x2, y2,
                                                              COMPOSITOR_GRIP * compositor.scale / 100 + compositor_line(row),
                                                              COMPOSITOR_GRIP_IN * compositor.scale / 100,
                                                              COMPOSITOR_FINGER_TARGET * compositor.scale / 100);

                edges = corner ? corner : edges;
        }
        edges &= axes;
        if (!edges)
                return 0;

        // A window that fills its screen or half of it has no edge on the screen's own: the pointer is held
        // there, and flung to the top to take the window it would start a resize instead.
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                unsigned int count;
                const int (*screens)[4] = compositor_screens(&count);
                const int *screen = screens[compositor_screen_of(row)];

                // The screen's own edges are no grip: the top where the window is at the top, the bottom where it is at the
                // bottom or fills a room (whose bottom is the taskbar's). A window's edge against another is a grip.
                edges &= compositor.y[row] <= screen[1] ? ~(unsigned int)COMPOSITOR_EDGE_TOP : ~0u;
                edges &= compositor.arranged[row] == COMPOSITOR_ROOM || y2 >= screen[1] + screen[3] ? ~(unsigned int)COMPOSITOR_EDGE_BOTTOM : ~0u;
                edges &= compositor.x[row] <= screen[0] ? ~(unsigned int)COMPOSITOR_EDGE_LEFT : ~0u;
                edges &= x2 >= screen[0] + screen[2] ? ~(unsigned int)COMPOSITOR_EDGE_RIGHT : ~0u;
        }
        return edges;
}

// What of the bands is at a point (taskbar_hit_reach's answer), with the listing of the band it is on in compositor.listed,
// which is what a tab's number names.
static int compositor_band_hit(int x, int y)
{
        int hit = TASKBAR_HIT_NONE, bands = compositor_bands();
        int reach = compositor.touching ? COMPOSITOR_FINGER_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; compositor.band_at < bands && hit == TASKBAR_HIT_NONE; compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();

                hit = taskbar_hit_reach(&view, x, y, reach);
        }
        // The listing of the band hit stays: the last listed.
        compositor.band_at = 0;
        return hit;
}

/*
        What is under a point: a tab, which tab is set to (TASKBAR_HIT_NONE
        when it is none), and otherwise the topmost window whose contents or
        grip, just outside its edges, hold it. Between the tabs is whatever is
        below them.
*/
static int compositor_band_hit(int x, int y);

/*
        How far outside a window's contents its grip can be, at the most, for any window: what compositor_edge_zone reaches
        with, and no less. A pointer's grip is the setting's at the desktop's scale and the window's line; a finger's is its
        own grip or a corner's target, which is a square of COMPOSITOR_FINGER_TARGET about the corner's own box. The line is
        the thickest any screen's scale draws.
*/
static int compositor_reach_most(void)
{
        int scale = compositor.scale, line;

        for (unsigned int at = 0; at < compositor.outputs; at++)
                scale = compositor.output[at].scale > scale ? compositor.output[at].scale : scale;
        line = compositor_line_px(scale);
        return (compositor.touching ? COMPOSITOR_TOUCH_GRIP + COMPOSITOR_GRIP + COMPOSITOR_FINGER_TARGET : compositor_grip()) *
                   compositor.scale / 100 + line + 1;
}

static int compositor_under(int x, int y, int *tab)
{
        const address_any columns[4] = {compositor.x, compositor.y, compositor.w, compositor.h};
        positive reach, count = compositor.stacked;

        *tab = compositor_band_hit(x, y);
        if (*tab != TASKBAR_HIT_NONE)
                return COMPOSITOR_NONE;

        // The stack is asked from the top for a window whose rectangle, grown by the furthest a grip reaches, holds the point
        // (rects_point_w, src/lib.c: two compares a window, the stack a word a row), and only such a window is asked what it has there, where every
        // window above the one found, and every window when none was, had its grip worked out at each motion.
        reach = (positive)compositor_reach_most();
        while (count)
        {
                bipolar at = rects_point_w(columns, compositor.stack, count, x, y, reach);
                int row;

                if (at < 0)
                        break;
                row = compositor.stack[at];
                if (compositor_visible(row) &&
                    ((x >= compositor.x[row] && x < compositor.x[row] + (int)compositor.w[row] && y >= compositor.y[row] &&
                      y < compositor.y[row] + (int)compositor.h[row]) ||
                     compositor_edge_zone(row, x, y)))
                        return row;
                count = (positive)at;
        }

        return COMPOSITOR_NONE;
}

// A window where it is put, and the table told.
static void compositor_put(int row, int x, int y)
{
        int changed = compositor.x[row] != x || compositor.y[row] != y;

        compositor.x[row] = compositor.table->x[row] = x;
        compositor.y[row] = compositor.table->y[row] = y;
        if (changed && compositor_row_hook_count)
                compositor_row_notify_configure(row, x, y, (int)compositor.w[row], (int)compositor.h[row]);
}

// Kept where a person can reach it (compositor_screen_keep_window): on one screen, all of it when it fits and its
// top left when it does not; across screens, its top on one of them.
static void compositor_place(int row, int x, int y)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        compositor_screen_keep_window(screens, count, &x, &y, (int)compositor.w[row], (int)compositor.h[row],
                                      compositor_line(row), COMPOSITOR_GRAB * compositor.scale / 100);
        compositor_put(row, x, y);
}

/*
        The least a window is made, device pixels, across (down 0) or down (down 1): the desktop's least for any window, or the
        window's own where its program says one in the table (least_width, least_height: logical pixels at the scale the window
        is drawn at, read as at most WINDOW_LEAST_MOST so what a program writes there multiplies safely), whichever is more. The
        hand's edge, a divider, a tile and a snap all ask for a size through compositor_want, which holds it to this: a tile
        narrower than a window's least is the window at its least, held by the tile's edges that are its screen's.
*/
// Minimum and maximum size a window accepts on an axis (down 0 = width, 1 = height) in device pixels; *most == 0 means unbounded.
static void compositor_bounds(int row, int down, int *least_out, int *most_out)
{
        b32 named = row >= 0 && row < COMPOSITOR_ROWS && compositor.table;
        unsigned int own_min = named ? (down ? compositor.table->least_height[row] : compositor.table->least_width[row]) : 0;
        unsigned int own_max = named ? (down ? compositor.table->most_height[row] : compositor.table->most_width[row]) : 0;
        unsigned int scale = named ? compositor.table->window_scale[row] : 0;
        int floor_min = (down ? WINDOW_MIN_HEIGHT : WINDOW_MIN_WIDTH) * compositor.scale / 100;
        int asked_min;

        scale = scale >= 25 && scale <= 800 ? scale : (unsigned int)compositor.scale;
        own_min = own_min > WINDOW_LEAST_MOST ? WINDOW_LEAST_MOST : own_min;
        own_max = own_max > WINDOW_LEAST_MOST ? WINDOW_LEAST_MOST : own_max;
        asked_min = (int)(own_min * scale / 100);
        if (least_out)
                *least_out = asked_min > floor_min ? asked_min : floor_min;
        if (most_out)
                *most_out = own_max ? (int)(own_max * scale / 100) : 0;
}

static int compositor_least(int row, int down)
{
        int least = 0;

        compositor_bounds(row, down, &least, 0);
        return least;
}

static int compositor_most(int row, int down)
{
        int most = 0;

        compositor_bounds(row, down, 0, &most);
        return most;
}

// Bitmask of COMPOSITOR_EDGE_* axes on which a row may be resized (0 for fixed-size windows where most <= least on both axes).
static unsigned int compositor_resizable_axes(int row)
{
        int least_w, most_w, least_h, most_h;
        unsigned int axes = 0;

        compositor_bounds(row, 0, &least_w, &most_w);
        compositor_bounds(row, 1, &least_h, &most_h);
        if (!most_w || most_w > least_w)
                axes |= COMPOSITOR_EDGE_LEFT | COMPOSITOR_EDGE_RIGHT;
        if (!most_h || most_h > least_h)
                axes |= COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_BOTTOM;
        return axes;
}

// Clamp width/height to row's min/max and display bounds (exempting fullscreen/background rows from max bounds).
static void compositor_row_clamp(int row, int *width, int *height)
{
        int least_w, most_w, least_h, most_h;

        compositor_bounds(row, 0, &least_w, &most_w);
        compositor_bounds(row, 1, &least_h, &most_h);
        if (!compositor.fullscreen[row] && compositor.layer[row] != WINDOW_LAYER_BACKGROUND)
        {
                if (most_w > 0 && *width > most_w)
                        *width = most_w;
                if (most_h > 0 && *height > most_h)
                        *height = most_h;
        }
        // The least first and the most last: at a scale whose least window is past the largest screen (96 pixels at 800
        // percent is 768), the least won and a window was made wider than its buffer's rows, which compositor_stretch
        // then filled into the rows after them -- the next window's pixels, and for the last row past the table.
        if (*width < least_w)
                *width = least_w;
        if (*width > compositor_most_width())
                *width = compositor_most_width();
        if (*height < least_h)
                *height = least_h;
        if (*height > compositor_most_height())
                *height = compositor_most_height();
}

// Canonical protocol-neutral window state snapshot for Wayland/XWM/compositor callers.
struct compositor_row_facts
{
        int x, y;
        unsigned int width, height;
        unsigned int want_width, want_height;
        int scale;
        unsigned int arranged;
        unsigned char fullscreen;
        signed char layer;
        unsigned char focused, resizing, minimized, filling;
        unsigned int closing;
};

static int compositor_row_facts(int row, struct compositor_row_facts *out)
{
        struct windows *table = compositor.table;

        if (!out || row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row])
                return -1;
        out->x = compositor.x[row];
        out->y = compositor.y[row];
        out->width = compositor.w[row];
        out->height = compositor.h[row];
        out->want_width = table && (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row]) ? table->want_width[row] : 0;
        out->want_height = table && (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row]) ? table->want_height[row] : 0;
        out->scale = compositor_row_scale(row);
        out->arranged = compositor.arranged[row];
        out->fullscreen = compositor.fullscreen[row];
        out->layer = compositor.layer[row];
        out->focused = compositor.focus == row;
        out->resizing = compositor.resize == row;
        out->minimized = (compositor.state[row] & WINDOW_MINIMIZED) != 0;
        out->filling = compositor_filling(row) != 0;
        out->closing = (compositor.state[row] & WINDOW_CLOSING) ? (unsigned int)compositor.close_asks[row] : 0;
        return 0;
}

// Write a UTF-8-safe truncated title into the row's table and compositor state, refreshing tabs on change.
static void compositor_row_set_title(int row, const char *utf8, unsigned long len)
{
        char title[WINDOWS_TITLE];
        unsigned long used = 0, at;

        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (utf8)
                while (used < len && utf8[used])
                        used++;
        at = used < WINDOWS_TITLE - 1 ? used : WINDOWS_TITLE - 1;
        while (at && at < used && ((unsigned char)utf8[at] & 0xc0u) == 0x80u)
                at--;
        for (unsigned int i = 0; i < WINDOWS_TITLE; i++)
                title[i] = i < at ? utf8[i] : 0;
        if (compositor.table)
                memory_copy(compositor.table->title[row], title, WINDOWS_TITLE);
        if (memory_compare(title, compositor.title[row], WINDOWS_TITLE))
        {
                memory_copy(compositor.title[row], title, WINDOWS_TITLE);
                if (compositor.showing[row])
                        compositor_tabs_changed();
        }
}

static void compositor_row_saved_get(int row, int rect[4])
{
        rect[0] = compositor.saved_x[row];
        rect[1] = compositor.saved_y[row];
        rect[2] = (int)compositor.saved_w[row];
        rect[3] = (int)compositor.saved_h[row];
}

static void compositor_row_saved_set(int row, const int rect[4])
{
        compositor.saved_x[row] = rect[0];
        compositor.saved_y[row] = rect[1];
        compositor.saved_w[row] = (unsigned int)rect[2];
        compositor.saved_h[row] = (unsigned int)rect[3];
}

static void compositor_row_save_floating(int row)
{
        if (compositor.arranged[row] == COMPOSITOR_FLOATING)
        {
                int cur[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

                compositor_row_saved_set(row, cur);
        }
}

/*
        The size a window is asked to be, in the table, and a knock. It draws
        whatever size it will and says so with its next picture; the edges the
        hand does not hold stay where they were, whatever comes back.
*/
static void compositor_want(int row, int width, int height)
{
        int x = compositor.x[row], y = compositor.y[row];

        compositor_row_clamp(row, &width, &height);
        compositor.table->want_width[row] = (unsigned int)width;
        compositor.table->want_height[row] = (unsigned int)height;
        rows_add(&compositor.woken, row);
        if (!compositor.showing[row])
                return;

        // Shown at it now, its picture stretched or cut to it, and the edges the hand does not hold where they were. A size its picture
        // is already is no ask to wait on: a window asked for the size it has (a fit, a place remembered) drew it before the ask and
        // need not draw it again, and an idle one would not, which left the ask standing and its next size of its own stretched to it.
        compositor_damage_row(row);
        compositor.asking[row] = compositor.picture_w[row] != (unsigned int)width || compositor.picture_h[row] != (unsigned int)height;
        compositor.w[row] = (unsigned int)width;
        compositor.h[row] = (unsigned int)height;
        if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                x = compositor.far_x[row] - width;
        if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                y = compositor.far_y[row] - height;
        compositor_place(row, x, y);
        compositor_row_notify_configure(row, compositor.x[row], compositor.y[row], width, height);
        compositor_wl_settle(row);
        compositor_stretch(row);
        compositor_damage_row(row);
}

/*
        A window whose size is more than the room of its screen is asked for the room (the band left out), as a window the
        hand stretched past the edge is.
*/
static void compositor_fit_row(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int room[4], on, width, height;

        if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_FLOATING)
                return;
        on = compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                     (int)compositor.h[row]);
        compositor_room(screens[on], room);
        width = (int)compositor.w[row] > room[2] ? room[2] : (int)compositor.w[row];
        height = (int)compositor.h[row] > room[3] ? room[3] : (int)compositor.h[row];
        if (width != (int)compositor.w[row] || height != (int)compositor.h[row])
                compositor_want(row, width, height);
}

/*
        A scale taken: a window that keeps its size in its own units is larger in pixels at a larger scale. One that is past
        the room of its screen now is asked to fit it; and every window that floats is asked to fit with its next picture of
        another size, which is the one it draws at the new scale (for the next three seconds, then it is what it draws).
        A window that fills a room was laid again with the rooms.
*/
static void compositor_scale_fit(void)
{
        rows_none(&compositor.fit);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_FLOATING)
                        continue;
                rows_add(&compositor.fit, row);
                compositor_fit_row(row);
        }
        compositor.fit_until = clock_monotonic_nanoseconds() + 3000000000ull;
}

/*
        A scale taken, by a card's screens or by the headless line: the desktop's scale and the table's, the cursor's plane drawn again, the
        bands' room, the floating windows fitted to it (compositor_scale_fit), the tabs and the screen damaged. The caller sets what the
        scale is taken from (a screen's own scale) first.
*/
static void compositor_scale_set(int scale)
{
        compositor.scale = scale;
        if (compositor.table)
                compositor.table->scale = (unsigned int)scale;
        compositor.cursor.plane.scale = 0;
        compositor_cursor_move();
        compositor_band_room();
        compositor_scale_fit();
        compositor_tabs_changed();
        compositor_damage(0, 0, compositor.width, compositor.height);
}

/*
        A shared edge: windows that meet along one line, a divider (compositor_divider_of). A press anywhere along it, from
        one end of the line to the other (compositor_shared_at), takes all of them: compositor_shared_begin takes hold of
        each, and compositor_shared_move grows one side as the other shrinks, so they go on meeting. A corner of a window
        (an edge of it that is on two sides) keeps its own resize, as a single window's does. The indicator
        (compositor_mark_shared) is a tint across the whole line, COMPOSITOR_SHARED_MARK logical pixels either side of it, over
        the topmost of the windows; it is shown while the pointer is over the line (compositor_shared_hover) and while it is held.
*/

static void compositor_resize_begin(int row, unsigned int edges);
static void compositor_hold_take(int at, int row);
static unsigned int compositor_tile_cells(unsigned int how);

static void compositor_grab_set(unsigned int kind, int row, unsigned int code, unsigned int edges)
{
        compositor.active_grab.kind = kind;
        compositor.active_grab.row = row;
        compositor.active_grab.code = code;
        compositor.active_grab.edges = edges;
}

static void compositor_grab_clear(void)
{
        compositor.active_grab.kind = COMPOSITOR_GRAB_NONE;
        compositor.active_grab.row = COMPOSITOR_NONE;
        compositor.active_grab.code = 0;
        compositor.active_grab.edges = 0;
}

// The window on the far side of row's edge (one edge: a corner is the window's own) that meets it edge to edge with the
// same extent along it, or COMPOSITOR_NONE.
static int compositor_meets(int row, unsigned int edge)
{
        int x = compositor.x[row], y = compositor.y[row], w = (int)compositor.w[row], h = (int)compositor.h[row];
        int vertical = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_RIGHT;

        if (edge != COMPOSITOR_EDGE_LEFT && edge != COMPOSITOR_EDGE_RIGHT && edge != COMPOSITOR_EDGE_TOP &&
            edge != COMPOSITOR_EDGE_BOTTOM)
                return COMPOSITOR_NONE;
        for (int other = 0; other < (int)compositor_rows_in_use; other++)
        {
                int ox = compositor.x[other], oy = compositor.y[other], ow = (int)compositor.w[other], oh = (int)compositor.h[other];

                // A menu is no window of the edge it lies against: nothing is shared with it.
                if (other == row || !compositor_visible(other) || compositor_popup_parent[other] >= 0)
                        continue;
                if (vertical ? oy != y || oh != h : ox != x || ow != w)
                        continue;
                if (edge == COMPOSITOR_EDGE_RIGHT ? ox == x + w : edge == COMPOSITOR_EDGE_LEFT ? ox + ow == x
                    : edge == COMPOSITOR_EDGE_BOTTOM ? oy == y + h : oy + oh == y)
                        return other;
        }
        return COMPOSITOR_NONE;
}

/*
        A divider: the line one window's edge lies on and every window whose edge lies on it too, reached along the line by
        overlapping -- the left half's right edge and both right quarters' left edges are one divider, which moves as one,
        so no gap or overlap opens between tiles and what a click finds follows them. lo is the windows on the left or top
        (their right or bottom on the line), hi those on the right or bottom, a bit each; lo_row and hi_row one of each, the
        window held and the one across that meets it where there is one. Windows that are no tile share a line only two at a
        time, edge to edge with the same extent (compositor_meets); tiles, every tile of the screen on the line.
*/
struct compositor_divider
{
        struct compositor_rows lo, hi;
        int lo_row, hi_row, vertical, line, from, to; // the line, and the span of it the windows cover
};

// The line and the span of a divider's windows, from where they are now.
static void compositor_divider_measure(struct compositor_divider *d)
{
        int first = 1;

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                int a, b;

                if (!rows_has(&d->lo, row) && !rows_has(&d->hi, row))
                        continue;
                a = d->vertical ? compositor.y[row] : compositor.x[row];
                b = a + (int)(d->vertical ? compositor.h[row] : compositor.w[row]);
                if (first || a < d->from)
                        d->from = a;
                if (first || b > d->to)
                        d->to = b;
                if (rows_has(&d->hi, row))
                        d->line = d->vertical ? compositor.x[row] : compositor.y[row];
                first = 0;
        }
}

static int compositor_divider_of(int row, unsigned int edge, struct compositor_divider *d)
{
        int other = compositor_meets(row, edge), high = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_TOP;
        int vertical = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_RIGHT, screen = compositor_screen_of(row), grew = 1;
        int line, from, to;

        if (edge != COMPOSITOR_EDGE_LEFT && edge != COMPOSITOR_EDGE_RIGHT && edge != COMPOSITOR_EDGE_TOP && edge != COMPOSITOR_EDGE_BOTTOM)
                return 0;
        line = vertical ? compositor.x[row] + (high ? 0 : (int)compositor.w[row]) : compositor.y[row] + (high ? 0 : (int)compositor.h[row]);
        from = vertical ? compositor.y[row] : compositor.x[row];
        to = from + (int)(vertical ? compositor.h[row] : compositor.w[row]);
        d->vertical = vertical;
        rows_none(&d->lo), rows_none(&d->hi), rows_add(high ? &d->hi : &d->lo, row);
        d->lo_row = high ? other : row, d->hi_row = high ? row : other;
        while (grew && compositor_tile_cells(compositor.arranged[row]))
        {
                grew = 0;
                for (int each = 0; each < (int)compositor_rows_in_use; each++)
                {
                        int near, far, a, b;

                        if (rows_has(&d->lo, each) || rows_has(&d->hi, each) || !compositor_visible(each) || compositor_popup_parent[each] >= 0 ||
                            !compositor_tile_cells(compositor.arranged[each]) || compositor_screen_of(each) != screen)
                                continue;
                        near = vertical ? compositor.x[each] : compositor.y[each];
                        far = near + (int)(vertical ? compositor.w[each] : compositor.h[each]);
                        a = vertical ? compositor.y[each] : compositor.x[each];
                        b = a + (int)(vertical ? compositor.h[each] : compositor.w[each]);
                        if (a >= to || b <= from || (near != line && far != line))
                                continue;
                        if (far == line)
                                rows_add(&d->lo, each), d->lo_row = d->lo_row >= 0 ? d->lo_row : each;
                        else
                                rows_add(&d->hi, each), d->hi_row = d->hi_row >= 0 ? d->hi_row : each;
                        from = a < from ? a : from;
                        to = b > to ? b : to;
                        grew = 1;
                }
        }
        // No tile across it: a pair edge to edge with the same extent, whatever the two are, or nothing.
        if (!rows_any(&d->lo) || !rows_any(&d->hi))
        {
                if (other == COMPOSITOR_NONE)
                        return 0;
                rows_none(&d->lo), rows_none(&d->hi), rows_add(&d->lo, high ? other : row), rows_add(&d->hi, high ? row : other);
        }
        compositor_divider_measure(d);
        return 1;
}

// The indicator's rectangle across a divider (vertical: one that runs down): the whole span its windows cover, from one
// end of the line to the other, and COMPOSITOR_SHARED_MARK logical pixels (at the scale of its windows' screen, a pixel at
// the least) either side of the line.
static void compositor_shared_strip(const struct compositor_divider *d, int *rect)
{
        int row = d->hi_row >= 0 ? d->hi_row : d->lo_row, scale = row >= 0 ? compositor_row_scale(row) : compositor.scale;
        int s = COMPOSITOR_SHARED_MARK * scale / 100, span = d->to - d->from;

        s = s < 1 ? 1 : s;
        if (d->vertical)
                rect[0] = d->line - s, rect[1] = d->from, rect[2] = 2 * s, rect[3] = span;
        else
                rect[0] = d->from, rect[1] = d->line - s, rect[2] = span, rect[3] = 2 * s;
}

// Whether row's edge is on a divider with the point along it (anywhere from one end to the other), and the divider.
static int compositor_shared_at(int row, unsigned int edge, int x, int y, struct compositor_divider *d)
{
        int rect[4];

        if (!edge || (edge & (edge - 1)) || !compositor_divider_of(row, edge, d))
                return 0;
        compositor_shared_strip(d, rect);
        return d->vertical ? y >= rect[1] && y < rect[1] + rect[3] : x >= rect[0] && x < rect[0] + rect[2];
}

// The indicator's rectangle over the window row, the window it meets being with (rect none for none), damaged where it
// was and where it is.
static void compositor_mark_set(int row, int with, const int *rect)
{
        if (row == compositor.mark_row && with == compositor.mark_with && !memory_compare(rect, compositor.mark, sizeof(compositor.mark)))
                return;
        if (compositor.mark[2] > 0)
                compositor_damage(compositor.mark[0], compositor.mark[1], compositor.mark[2], compositor.mark[3]);
        memory_copy(compositor.mark, rect, sizeof(compositor.mark));
        compositor.mark_row = row;
        compositor.mark_with = with;
        if (rect[2] > 0)
                compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

static void compositor_mark_clear(void)
{
        int none[4] = {0, 0, 0, 0};

        compositor_mark_set(COMPOSITOR_NONE, COMPOSITOR_NONE, none);
        rows_none(&compositor.mark_rows);
}

// The indicator of a divider, over the topmost of its windows (so nothing above it covers it); the other is the one across.
static void compositor_mark_shared(const struct compositor_divider *d)
{
        int rect[4], top = d->hi_row, with = d->lo_row;

        compositor_shared_strip(d, rect);
        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (rows_has(&d->lo, compositor.stack[at]) || rows_has(&d->hi, compositor.stack[at]))
                        top = compositor.stack[at], with = rows_has(&d->lo, top) ? d->hi_row : d->lo_row;
        compositor_mark_set(top, with, rect);
        compositor.mark_rows = d->lo, rows_or(&compositor.mark_rows, &d->hi);
}

// The indicator while no button is held: over a divider under the pointer, and none otherwise.
static void compositor_shared_hover(int under)
{
        unsigned int edge = under < 0 || compositor.grab >= 0 || compositor.touching ? 0
                            : compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y);
        struct compositor_divider d;

        if (compositor_shared_at(under, edge, compositor.pointer_x, compositor.pointer_y, &d))
                compositor_mark_shared(&d);
        else
                compositor_mark_clear();
}

// The divider being resized, from the windows it took at the press.
static void compositor_shared_divider(struct compositor_divider *d)
{
        d->lo = compositor.shared_lo_set, d->hi = compositor.shared_hi_set;
        d->lo_row = compositor.shared_lo, d->hi_row = compositor.shared_hi, d->vertical = compositor.shared_vertical;
        compositor_divider_measure(d);
}

/*
        A press along a divider takes all of its windows: row, pressed on its edge, is resized with the others,
        each holding its far edge (the low side's left or top, the high side's right or bottom) while the line moves. Each
        keeps what it fills -- a window that floats has an outline the screen keeps in view, which would take them off the
        line -- and the rectangle it had before. Answers 0 when there is no divider here, and the press is row's own.
*/
static int compositor_shared_begin(int row, unsigned int edge)
{
        struct compositor_divider d;
        unsigned int was = compositor.arranged[row];
        struct compositor_rows all;
        unsigned int lo_edge, hi_edge;

        if (!compositor_shared_at(row, edge, compositor.pointer_x, compositor.pointer_y, &d))
                return 0;
        all = d.lo, rows_or(&all, &d.hi);
        lo_edge = d.vertical ? COMPOSITOR_EDGE_RIGHT : COMPOSITOR_EDGE_BOTTOM;
        hi_edge = d.vertical ? COMPOSITOR_EDGE_LEFT : COMPOSITOR_EDGE_TOP;
        compositor_resize_begin(row, edge);
        compositor.arranged[row] = was;
        compositor.shared_row = rows_has(&d.lo, row) ? d.hi_row : d.lo_row;
        compositor_grab_set(COMPOSITOR_GRAB_DIVIDER, row, COMPOSITOR_BTN_LEFT, edge);
        compositor.shared_lo = d.lo_row, compositor.shared_hi = d.hi_row;
        compositor.shared_lo_set = d.lo, compositor.shared_hi_set = d.hi;
        compositor.shared_vertical = d.vertical;
        for (int each = 0; each < (int)compositor_rows_in_use; each++)
        {
                if (!rows_has(&all, each))
                        continue;
                compositor.shared_sizes[each] = d.vertical ? (int)compositor.w[each] : (int)compositor.h[each];
                if (each == row)
                        continue;
                compositor_hold_take(1, each);
                compositor_damage_row(each);
                compositor.edges[each] = rows_has(&d.lo, each) ? lo_edge : hi_edge;
                compositor.far_x[each] = compositor.x[each] + (int)compositor.w[each];
                compositor.far_y[each] = compositor.y[each] + (int)compositor.h[each];
                compositor_damage_row(each);
        }
        compositor_mark_shared(&d);
        return 1;
}

/*
        The divider moved by the pointer's travel since the press, every window of it kept to the least and most a window
        is, so they go on meeting: the low side's far edges stay and the high side's near edges move with the line.
*/
static void compositor_shared_move(void)
{
        int vertical = compositor.shared_vertical, travel = vertical ? compositor.pointer_x - compositor.press_x
                                                                         : compositor.pointer_y - compositor.press_y;
        int low = -2147483647, high = 2147483647;
        struct compositor_rows all = compositor.shared_lo_set;

        rows_or(&all, &compositor.shared_hi_set);
        struct compositor_divider d;

        for (int each = 0; each < (int)compositor_rows_in_use; each++)
        {
                int size = compositor.shared_sizes[each], least, row_most;
                int most = (vertical ? compositor_most_width() : compositor_most_height()) - 2 * compositor_line(each);

                if (!rows_has(&all, each))
                        continue;
                compositor_bounds(each, !vertical, &least, &row_most);
                if (row_most > 0 && row_most < most)
                        most = row_most;
                if (rows_has(&compositor.shared_lo_set, each))
                        low = least - size > low ? least - size : low, high = most - size < high ? most - size : high;
                else
                        low = size - most > low ? size - most : low, high = size - least < high ? size - least : high;
        }
        if (low > high)
                return;
        travel = travel < low ? low : travel > high ? high : travel;
        for (int each = 0; each < (int)compositor_rows_in_use; each++)
        {
                int size;

                if (!rows_has(&all, each))
                        continue;
                size = compositor.shared_sizes[each] + (rows_has(&compositor.shared_lo_set, each) ? travel : -travel);
                if (vertical)
                        compositor_want(each, size, (int)compositor.h[each]);
                else
                        compositor_want(each, (int)compositor.w[each], size);
        }
        compositor_shared_divider(&d);
        compositor_mark_shared(&d);
}

/*
        A resize takes hold of an edge (or a corner). A tile resized stays a tile: its screen's edges stay its own (no grip,
        no line kept off them, so nothing moves by a pixel when the hand takes it), and it keeps the rectangle it had before
        it was first arranged, which a drag away or a restore gives back, as a tile only moved between its neighbours does.
        A window filling its screen resized floats.
*/
static void compositor_resize_begin(int row, unsigned int edges)
{
        edges &= compositor_resizable_axes(row);
        if (!edges)
                return;
        compositor_hold_take(0, row);
        compositor.resize = row;
        compositor.shared_row = COMPOSITOR_NONE;
        compositor_grab_set(COMPOSITOR_GRAB_RESIZE, row, COMPOSITOR_BTN_LEFT, edges);
        compositor_mark_clear();
        compositor.resize_w = (int)compositor.w[row];
        compositor.resize_h = (int)compositor.h[row];
        compositor.press_x = compositor.pointer_x;
        compositor.press_y = compositor.pointer_y;
        if (!compositor_tile_cells(compositor.arranged[row]))
                compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0, compositor.layer[row] = WINDOW_LAYER_NORMAL;
        compositor.edges[row] = edges;
        compositor.far_x[row] = compositor.x[row] + (int)compositor.w[row];
        compositor.far_y[row] = compositor.y[row] + (int)compositor.h[row];
}

static void compositor_resize_move(void)
{
        int row = compositor.resize;

        if (compositor.shared_row >= 0)
        {
                compositor_shared_move();
                return;
        }
        int dx = compositor.pointer_x - compositor.press_x;
        int dy = compositor.pointer_y - compositor.press_y;
        int w = compositor.resize_w, h = compositor.resize_h;
        int max_w = compositor_most_width() - 2 * compositor_line(row);
        int max_h = compositor_most_height() - 2 * compositor_line(row);

        if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                w -= dx;
        else if (compositor.edges[row] & COMPOSITOR_EDGE_RIGHT)
                w += dx;

        if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                h -= dy;
        else if (compositor.edges[row] & COMPOSITOR_EDGE_BOTTOM)
                h += dy;

        compositor_want(row, w > max_w ? max_w : w, h > max_h ? max_h : h);
}

/*
        Filling a rectangle as how says: all of a screen or half of it, or
        the room a placement fills (COMPOSITOR_ROOM). A window is asked for
        the size the rectangle gives it and put where the rectangle begins,
        and the rectangle it had is kept, from the time it left floating and
        not at every arrangement, so snapping left, then right, then dragging
        off puts it back where it was and not on the left half. The edge that
        is the screen's stays where it is whatever size comes back: the right
        half is held by its right.
*/
// The edges a window filling how holds whatever size its program comes back at: those that are not the screen's, so the
// screen's stay where they are (a window on the right is held by its right, one at the bottom by its bottom).
static unsigned int compositor_tile_edges(unsigned int how)
{
        return (how == COMPOSITOR_RIGHT || how == COMPOSITOR_TOP_RIGHT || how == COMPOSITOR_BOTTOM_RIGHT ? COMPOSITOR_EDGE_LEFT : 0) |
               (how == COMPOSITOR_BOTTOM || how == COMPOSITOR_BOTTOM_LEFT || how == COMPOSITOR_BOTTOM_RIGHT ? COMPOSITOR_EDGE_TOP : 0);
}

// A window set to rect, filling how, gliding there from where it was: what an arrangement and a put back share.
static void compositor_set_rect(int row, unsigned int how, const int *rect)
{
        int was[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

        // Only maximised windows can be fullscreen: whatever else a window is set to is not.
        if (how != COMPOSITOR_MAXIMIZED)
                compositor.fullscreen[row] = 0;

        // Its line goes with it: the outline it had is damaged before it is a filling one.
        compositor_damage_row(row);
        compositor.arranged[row] = how;
        compositor.edges[row] = compositor_tile_edges(how);
        compositor.far_x[row] = rect[0] + rect[2];
        compositor.far_y[row] = rect[1] + rect[3];
        compositor_put(row, rect[0], rect[1]);
        compositor_damage_row(row);
        compositor_want(row, rect[2], rect[3]);
        {
                int now[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

                if (memory_compare(was, now, sizeof(was)))
                        compositor_glide_start(row, was);
        }
}

static void compositor_arrange_rect(int row, unsigned int how, const int *rect)
{
        if (!compositor.showing[row])
                return;

        compositor_osk_unpan(row);
        compositor_row_save_floating(row);
        compositor_set_rect(row, how, rect);
        compositor_osk_sync();
}

/*
        Escape, or a right press, during a drag or a resize: what the hand took is put back as it was at the press -- its
        rectangle and what it filled, the remembered rectangle untouched -- gliding there from where it is, and the hand
        holds nothing. Its preview, the grid and its lit line go with it; the press is no half of a double click.
*/
// row taken hold of: the first of a hold (first, which forgets the last one's) or one more of it.
static void compositor_hold_take(int more, int row)
{
        int rect[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

        if (!more)
                rows_none(&compositor.hold_rows);
        rows_add(&compositor.hold_rows, row);
        memory_copy(compositor.hold_rect[row], rect, sizeof(rect));
        compositor.hold_how[row] = compositor.arranged[row];
}

// Unified grab teardown: COMMIT (release), CANCEL (revert hold_rows), STOP (client stop), or DROP (row teardown).
static int compositor_grab_end(int how, int drop_row)
{
        if (how == COMPOSITOR_GRAB_DROP)
        {
                if (drop_row < 0)
                        return 0;
                if (compositor.preview_row == drop_row)
                        compositor_preview(COMPOSITOR_NONE);
                if (compositor.preview_draw == drop_row + 1)
                {
                        int drawn[4];

                        if (compositor_preview_shown(drawn))
                                compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
                        compositor.preview_draw = 0;
                }
                rows_del(&compositor.hold_rows, drop_row);
                if (compositor.mark_row == drop_row || compositor.mark_with == drop_row || rows_has(&compositor.mark_rows, drop_row))
                        compositor_mark_clear();
                if (compositor.shared_row >= 0 &&
                    (rows_has(&compositor.shared_lo_set, drop_row) || rows_has(&compositor.shared_hi_set, drop_row)))
                        compositor.shared_row = COMPOSITOR_NONE, compositor.resize = COMPOSITOR_NONE;
                if (compositor.drag == drop_row)
                        compositor.drag = COMPOSITOR_NONE;
                if (compositor.press == drop_row)
                        compositor.press = COMPOSITOR_NONE;
                if (compositor.resize == drop_row)
                        compositor.resize = COMPOSITOR_NONE;
                if (compositor.drag_glide == drop_row + 1)
                        compositor.drag_glide = 0;
                if (compositor.edge_right == (unsigned int)drop_row + 1)
                        compositor.edge_right = 0;
                if (compositor.lit_row == drop_row)
                        compositor.lit_row = COMPOSITOR_NONE, compositor.lit_edges = compositor.lit_held = 0;
                if (compositor.touch.closing == drop_row)
                        compositor.touch.closing = COMPOSITOR_NONE;
                if (compositor.touch.hold_row == drop_row)
                        compositor.touch.hold_row = COMPOSITOR_NONE;
                if (compositor.active_grab.row == drop_row ||
                    (compositor.active_grab.kind == COMPOSITOR_GRAB_DIVIDER && compositor.shared_row < 0))
                        compositor_grab_clear();
                return 1;
        }
        if (how == COMPOSITOR_GRAB_STOP)
        {
                int resized;

                if (drop_row < 0 || (compositor.drag != drop_row && compositor.resize != drop_row))
                        return 0;
                resized = compositor.resize == drop_row;
                compositor_preview(COMPOSITOR_NONE);
                compositor.drag = compositor.resize = compositor.shared_row = COMPOSITOR_NONE;
                compositor.edge_right = 0;
                if (compositor.drag_glide == drop_row + 1)
                        compositor.drag_glide = 0;
                compositor_mark_clear();
                compositor_lit(COMPOSITOR_NONE, 0, 0);
                rows_none(&compositor.hold_rows);
                compositor_grab_clear();
                if (resized)
                        rows_add(&compositor.woken, drop_row);
                return 1;
        }
        if (how == COMPOSITOR_GRAB_CANCEL)
        {
                if (compositor.drag < 0 && compositor.resize < 0)
                        return 0;
                compositor_preview(COMPOSITOR_NONE);
                compositor.drag = compositor.resize = compositor.shared_row = COMPOSITOR_NONE;
                compositor.edge_right = 0;
                compositor.press = COMPOSITOR_NONE;
                compositor.drag_glide = 0;
                compositor_mark_clear();
                compositor_lit(COMPOSITOR_NONE, 0, 0);
                compositor_grab_clear();
                for (int row = 0; row < (int)compositor_rows_in_use; row++)
                        if (rows_has(&compositor.hold_rows, row) && compositor.showing[row])
                                compositor_set_rect(row, compositor.hold_how[row], compositor.hold_rect[row]);
                rows_none(&compositor.hold_rows);
                return 1;
        }
        {
                int dragged = compositor.drag, resized = compositor.resize;

                compositor_preview(COMPOSITOR_NONE);
                compositor.drag = compositor.resize = COMPOSITOR_NONE;
                if (dragged >= 0 || resized >= 0)
                        compositor_lit(COMPOSITOR_NONE, 0, 0);
                if (resized >= 0)
                {
                        compositor_mark_clear();
                        rows_add(&compositor.woken, resized);
                        if (compositor.shared_row >= 0)
                        {
                                rows_or(&compositor.woken, &compositor.shared_lo_set);
                                rows_or(&compositor.woken, &compositor.shared_hi_set);
                                compositor.shared_row = COMPOSITOR_NONE;
                        }
                }
                rows_none(&compositor.hold_rows);
                if (compositor.active_grab.kind == COMPOSITOR_GRAB_MOVE ||
                    compositor.active_grab.kind == COMPOSITOR_GRAB_RESIZE ||
                    compositor.active_grab.kind == COMPOSITOR_GRAB_DIVIDER)
                        compositor_grab_clear();
                return dragged >= 0 || resized >= 0;
        }
}

static int compositor_hold_cancel(void)
{
        return compositor_grab_end(COMPOSITOR_GRAB_CANCEL, COMPOSITOR_NONE);
}

static void compositor_float(int row);
static unsigned int compositor_region_cells(unsigned int how);

/*
        The cells a tile fills, one bit a quarter: top left 1, top right 2, bottom left 4, bottom right 8. A half is two of
        them. A window that is not a tile (floating, maximized, placed in its room) fills none.
*/
static unsigned int compositor_tile_cells(unsigned int how)
{
        switch (how)
        {
        case COMPOSITOR_LEFT: return 1u | 4u;
        case COMPOSITOR_RIGHT: return 2u | 8u;
        case COMPOSITOR_BOTTOM: return 4u | 8u;
        case COMPOSITOR_TOP_LEFT: case COMPOSITOR_TOP_RIGHT: case COMPOSITOR_BOTTOM_LEFT: case COMPOSITOR_BOTTOM_RIGHT:
                return 1u << (how - COMPOSITOR_TOP_LEFT);
        default: return 0;
        }
}

// The tile that fills exactly cells, or COMPOSITOR_FLOATING when no tile does.
static unsigned int compositor_cells_tile(unsigned int cells)
{
        static const unsigned int tiles[] = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_BOTTOM, COMPOSITOR_TOP_LEFT,
                                             COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};

        for (unsigned int at = 0; at < array_count(tiles); at++)
                if (compositor_tile_cells(tiles[at]) == cells)
                        return tiles[at];
        return COMPOSITOR_FLOATING;
}

/*
        A tile that row fills on screen leaves the tiles of the other windows on that screen the cells it does not fill.
        Each one it covers is cut to what is left, which is a tile again: a half that loses its top quarter is its bottom
        quarter, and a quarter that loses its half is the other quarter of it. One left nothing floats back to its own
        rectangle (compositor_float). A cut under a window's least is asked for the least at the cut's corner
        (compositor_want), so the rest of the cut is left free for the snap assist. A window that is not a tile is not
        touched, and neither is one on another screen.
*/
static void compositor_tile_others(int row, unsigned int how, int screen)
{
        unsigned int count, taken = compositor_region_cells(how);
        const int (*screens)[4] = compositor_screens(&count);

        for (int other = 0; other < (int)compositor_rows_in_use; other++)
        {
                unsigned int was = compositor_tile_cells(compositor.arranged[other]), left;
                int part[4];

                if (other == row || !was || !compositor.showing[other] || compositor_screen_of(other) != screen)
                        continue;
                left = was & ~taken;
                if (left == was)
                        continue;
                if (!left)
                        compositor_float(other);
                else
                {
                        unsigned int rest = compositor_cells_tile(left);

                        compositor_screen_part(screens[screen], rest, part);
                        compositor_arrange_rect(other, rest, part);
                }
        }
}

/*
        The snap assist and the top edge's grid. A drag over a snap target shows the windows floating on its screen as
        thumbnails in the space the target leaves (compositor_assist_refresh). A drop there keeps them up, armed, until a
        click on one of them places that window in the tile it is over (compositor_assist_at), or a click elsewhere, or Escape,
        takes them away. A drag at the top edge of a screen shows the grid of regions along it (compositor_grid_tile, and the one
        under the pointer compositor_grid_under): a drop on a region takes it, and a region a window already fills is shown
        taken and is no target (compositor_target).
*/

// The grid's regions, in the order of its tiles, left to right.
static const unsigned int compositor_grid_regions[COMPOSITOR_GRID_TILES] = {COMPOSITOR_MAXIMIZED, COMPOSITOR_LEFT, COMPOSITOR_RIGHT,
                                                                            COMPOSITOR_TOP_LEFT, COMPOSITOR_TOP_RIGHT,
                                                                            COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};

// The cells a region fills: a tile's, or all four for the whole screen.
static unsigned int compositor_region_cells(unsigned int how)
{
        return how == COMPOSITOR_MAXIMIZED ? COMPOSITOR_CELLS : compositor_tile_cells(how);
}

// The cells the tiles of the other windows on a screen fill, row's own left out.
static unsigned int compositor_occupied(int row, int screen)
{
        unsigned int cells = 0;

        for (int other = 0; other < (int)compositor_rows_in_use; other++)
                if (other != row && compositor.showing[other] && compositor_screen_of(other) == screen)
                        cells |= compositor_tile_cells(compositor.arranged[other]);
        return cells;
}

/*
        The grid's tiles on a screen: seven mini screens in a row along its top edge, each a tenth of the screen's width and
        as tall as the screen, with a gap between them, the row centred. Tile at is the region compositor_grid_regions gives it.
*/
static void compositor_grid_tile(const int *screen, unsigned int at, int *tile)
{
        int gap = COMPOSITOR_GRID_GAP * compositor.scale / 100, width = screen[2] / 10, row = COMPOSITOR_GRID_TILES * width + (COMPOSITOR_GRID_TILES - 1) * gap;

        tile[0] = screen[0] + (screen[2] - row) / 2 + (int)at * (width + gap);
        tile[1] = screen[1];
        tile[2] = width;
        tile[3] = width > 0 ? screen[3] * width / screen[2] : 0;
}

// Whether a pointer at x, y on screen is in the top band that brings the grid up.
static int compositor_grid_band(const int *screen, int x, int y)
{
        return x >= screen[0] && x < screen[0] + screen[2] && y >= screen[1] && y - screen[1] < COMPOSITOR_SNAP * compositor.scale / 100;
}

// Whether a pointer at x, y on screen is in the row of the grid's tiles, which stays up once it is (compositor_overlays_drag).
static int compositor_grid_panel(const int *screen, int x, int y)
{
        int first[4], last[4];

        compositor_grid_tile(screen, 0, first);
        compositor_grid_tile(screen, COMPOSITOR_GRID_TILES - 1, last);
        return x >= first[0] && x < last[0] + last[2] && y >= screen[1] && y - screen[1] < first[3];
}

// The tile of the grid under x, y on screen, or COMPOSITOR_GRID_TILES for none.
static unsigned int compositor_grid_under(const int *screen, int x, int y)
{
        int tile[4];

        for (unsigned int at = 0; at < COMPOSITOR_GRID_TILES; at++)
        {
                compositor_grid_tile(screen, at, tile);
                if (x >= tile[0] && x < tile[0] + tile[2] && y >= tile[1] && y < tile[1] + tile[3])
                        return at;
        }
        return COMPOSITOR_GRID_TILES;
}

// Where a drop of the window row at x, y arranges it: the grid's region under the pointer, which is no target when a window
// already fills it, or else the snap target of the window's rectangle box (compositor_snap). touch is the edges it touches.
static unsigned int compositor_target(int row, int x, int y, const int *box, int *screen, unsigned int *touch)
{
        unsigned int count, tile;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_at(screens, count, x, y);

        if (at >= 0 && compositor.grid_on && compositor.grid_screen == at && compositor_grid_panel(screens[at], x, y))
        {
                tile = compositor_grid_under(screens[at], x, y);
                if (tile < COMPOSITOR_GRID_TILES)
                {
                        unsigned int how = compositor_grid_regions[tile];

                        *screen = at;
                        *touch = 0;
                        return compositor_region_cells(how) & compositor_occupied(row, at) ? COMPOSITOR_FLOATING : how;
                }
        }
        return compositor_snap(box, touch, screen);
}

// The tiles a set of free cells is cut into, the larger first: a half where it holds one, then quarters.
static unsigned int compositor_zones(unsigned int free, unsigned int *zones)
{
        static const unsigned int order[] = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_BOTTOM, COMPOSITOR_TOP_LEFT,
                                             COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};
        unsigned int count = 0;

        for (unsigned int at = 0; at < array_count(order); at++)
        {
                unsigned int cells = compositor_tile_cells(order[at]);

                if ((free & cells) == cells)
                {
                        zones[count++] = order[at];
                        free &= ~cells;
                }
        }
        return count;
}

// The rectangle of a thumbnail of a w by h window, at place of of in zone: a grid of the cells that many need, each the
// window's picture as large as its cell takes, centred in it.
static void compositor_assist_place(const int *zone, unsigned int at, unsigned int of, int w, int h, int *rect)
{
        int margin = COMPOSITOR_GRID_GAP * compositor.scale / 100, cols = 1, rows, cell_w, cell_h, fit_w, fit_h;

        while (cols * cols < (int)of)
                cols++;
        rows = ((int)of + cols - 1) / cols;
        cell_w = (zone[2] - 2 * margin) / cols;
        cell_h = (zone[3] - 2 * margin) / rows;
        if (cell_w < 1)
                cell_w = 1;
        if (cell_h < 1)
                cell_h = 1;
        if ((long)w * cell_h > (long)h * cell_w)
                fit_w = cell_w, fit_h = (int)((long)h * cell_w / w);
        else
                fit_h = cell_h, fit_w = (int)((long)w * cell_h / h);
        rect[0] = zone[0] + margin + (int)(at % (unsigned int)cols) * cell_w + (cell_w - fit_w) / 2;
        rect[1] = zone[1] + margin + (int)(at / (unsigned int)cols) * cell_h + (cell_h - fit_h) / 2;
        rect[2] = fit_w;
        rect[3] = fit_h;
}

// The snap assist's thumbnails, for the window assist_row arranged as assist_how on assist_screen: the visible windows
// floating there, most recent first, each in the tile the space the region leaves is cut into, round-robin over the tiles.
static void compositor_assist_refresh(void)
{
        unsigned int count, zones[4], zones_count, free, candidates[COMPOSITOR_ASSIST_MAX], taken[4] = {0, 0, 0, 0};
        unsigned int found = 0;
        const int (*screens)[4] = compositor_screens(&count);

        compositor.assist_count = 0;
        if (!compositor.assist_on || compositor.assist_screen < 0 || (unsigned int)compositor.assist_screen >= count)
                return;
        free = COMPOSITOR_CELLS & ~(compositor_region_cells(compositor.assist_how) |
                                    compositor_occupied(compositor.assist_row, compositor.assist_screen));
        zones_count = compositor_zones(free, zones);
        if (!zones_count)
                return;
        for (int at = compositor.stacked - 1; at >= 0 && found < COMPOSITOR_ASSIST_MAX; at--)
        {
                int row = compositor.stack[at];

                if (row == compositor.assist_row || !compositor_visible(row) || compositor.arranged[row] != COMPOSITOR_FLOATING ||
                    compositor_screen_of(row) != compositor.assist_screen || !compositor.w[row] || !compositor.h[row])
                        continue;
                candidates[found++] = (unsigned int)row;
        }
        for (unsigned int at = 0; at < found; at++)
                taken[at % zones_count]++;
        for (unsigned int at = 0; at < found; at++)
        {
                unsigned int zone = at % zones_count, place = at / zones_count;
                int part[4];
                int row = (int)candidates[at];

                compositor_screen_part(screens[compositor.assist_screen], zones[zone], part);
                compositor_assist_place(part, place, taken[zone], (int)compositor.w[row], (int)compositor.h[row],
                                        compositor.assist_rect[at]);
                compositor.assist_rows[at] = candidates[at];
                compositor.assist_zone[at] = zones[zone];
        }
        compositor.assist_count = (int)found;
}

// The thumbnail under x, y, or -1 for none.
static int compositor_assist_at(int x, int y)
{
        for (int at = 0; at < compositor.assist_count; at++)
        {
                const int *rect = compositor.assist_rect[at];

                if (x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3])
                        return at;
        }
        return -1;
}

/*
        The overlays' fade: the percent they are drawn at now, from overlay_from toward overlay_goal over COMPOSITOR_SNAP_FADE_NS
        at the pace on the kit's ease (reduced motion: the goal at once). They are shown while they are up or still fading out.
*/
static unsigned int compositor_overlay_alpha(void)
{
        int from = (int)compositor.overlay_from, goal = (int)compositor.overlay_goal;
        b64 done;

        if (!compositor.overlay_at || !compositor_glides())
                return compositor.overlay_goal;
        done = ui_bezier_at(&ui_ease, ui_progress((b64)(clock_monotonic_nanoseconds() - compositor.overlay_at), ui_paced(COMPOSITOR_SNAP_FADE_NS)));
        return (unsigned int)(from + (int)((long long)(goal - from) * (long long)done / (long long)UI_ONE));
}

static void compositor_overlay_fade(unsigned int goal)
{
        compositor.overlay_from = compositor_overlay_alpha();
        compositor.overlay_goal = goal;
        compositor.overlay_at = compositor_glides() ? clock_monotonic_nanoseconds() : 0;
}

// Whether the overlays are drawn: up now, or still fading out. Up is read live (compositor_overlays_up), not as the last
// present saw it: a drag brings the snap assist up and damages its thumbnails before the next present, and those are
// the rectangles it must repaint.
static int compositor_overlays_shown(void)
{
        return compositor_overlays_up() || compositor_overlay_alpha() > 0;
}

/*
        After a present: overlays still fading are damaged again, so the next present draws them at their next percent. One
        that has faded out is gone, its thumbnails with it, and the loop goes idle.
*/
static void compositor_overlays_keep(void)
{
        if (!compositor.overlay_at)
                return;
        if (clock_monotonic_nanoseconds() - compositor.overlay_at < (positive)ui_paced(COMPOSITOR_SNAP_FADE_NS))
        {
                compositor_overlays_damage();
                return;
        }
        // Faded: where they were is drawn once more, or the faint last frame of the fade stays on the screen.
        compositor_overlays_damage_all();
        if (!compositor.overlays_up)
                compositor.assist_count = 0;
        compositor.overlay_at = 0;
}

// The snap's edge colour at a percent of itself over the wallpaper: a thumbnail's and a grid tile's line, which fades.
static unsigned int compositor_snap_edge(unsigned int percent)
{
        unsigned int ink = compositor_snap_ink() | 0xff000000u, ground = compositor_ground | 0xff000000u;

        return ui_mix(ink, percent > 100 ? 100 : percent, ground) & 0xffffffu;
}

// The percent the overlays of a screen are drawn at: the screen's own, or the desktop's where it has none.
static int compositor_overlay_scale(int screen)
{
        int scale = compositor_output_scale(screen);

        return scale > 0 ? scale : compositor.scale;
}

// The rectangles the overlays are drawn in now: each thumbnail's, and the grid's row while it is up or fading out (or, with
// all, wherever they were last, shown or not).
static int compositor_overlay_boxes_of(int boxes[COMPOSITOR_ASSIST_MAX + 1][4], int all)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int n = 0, first[4], last[4], shown = all || compositor_overlays_shown();

        for (int thumb = 0; shown && thumb < compositor.assist_count; thumb++)
        {
                const int *rect = compositor.assist_rect[thumb];
                int line = compositor_line_px(compositor_overlay_scale(compositor.assist_screen));

                boxes[n][0] = rect[0] - line, boxes[n][1] = rect[1] - line;
                boxes[n][2] = rect[2] + 2 * line, boxes[n][3] = rect[3] + 2 * line;
                n++;
        }
        if (shown && compositor.grid_on && compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
                const int *screen = screens[compositor.grid_screen];
                int line = compositor_line_px(compositor_overlay_scale(compositor.grid_screen));

                compositor_grid_tile(screen, 0, first);
                compositor_grid_tile(screen, COMPOSITOR_GRID_TILES - 1, last);
                boxes[n][0] = first[0] - line, boxes[n][1] = screen[1] - line;
                boxes[n][2] = last[0] + last[2] - first[0] + 2 * line;
                boxes[n][3] = first[3] + 2 * line;
                n++;
        }
        return n;
}

static void compositor_overlays_damage_of(int all)
{
        int boxes[COMPOSITOR_ASSIST_MAX + 1][4];
        int n = compositor_overlay_boxes_of(boxes, all);

        for (int at = 0; at < n; at++)
                compositor_damage(boxes[at][0], boxes[at][1], boxes[at][2], boxes[at][3]);
}

static void compositor_overlays_damage(void)
{
        compositor_overlays_damage_of(0);
}

// Where the overlays were last, shown or not: what a fade that has ended draws once more.
static void compositor_overlays_damage_all(void)
{
        compositor_overlays_damage_of(1);
}

// Whether the snap assist or the grid is up: what the frames of a present add (compositor_overlays).
static int compositor_overlays_up(void)
{
        return compositor.assist_on || compositor.grid_on;
}

/*
        The overlays of a present, from at in the frame list, all at the overlays' fade. Each thumbnail: its window's picture
        scaled into its rectangle, with the window's line and 8 px corners in the snap's colour, and a veil of the wallpaper
        over the pair that fades the picture in or out (a tint, drawn over what is composed below it). Then the grid: each
        tile a tint with its line and corners drawn over it (a tint frame, then a frame that is only its border), and each
        region in it tinted, the taken ones dark. Answers the frames added.
*/
static unsigned int compositor_overlays(struct compositor_frame *frames, unsigned int at)
{
        unsigned int count, added = 0, percent = compositor_overlay_alpha(), grid_ink, taken, ground;
        const int (*screens)[4] = compositor_screens(&count);
        int shown = compositor_overlays_shown();

        ground = compositor_ground;
        for (int thumb = 0; shown && thumb < compositor.assist_count; thumb++)
        {
                const int *rect = compositor.assist_rect[thumb];
                int row = (int)compositor.assist_rows[thumb], scale = compositor_overlay_scale(compositor.assist_screen);
                int line = compositor_line_px(scale);
                struct compositor_frame *picture = &frames[at + added], *veil = &frames[at + added + 1];

                compositor_frame_of(row, picture);
                picture->x = rect[0], picture->y = rect[1];
                picture->width = rect[2], picture->height = rect[3];
                picture->source_w = (int)compositor.w[row], picture->source_h = (int)compositor.h[row];
                picture->radius = UI_RADIUS * scale / 100, picture->line = line;
                picture->edge = compositor_snap_edge(percent);
                picture->close = 0;
                picture->lit = 0;
                memory_zero(picture->preview, sizeof(picture->preview));
                memory_zero(picture->mark, sizeof(picture->mark));
                memory_zero(veil, sizeof(*veil));
                veil->scale = compositor.scale;
                if (percent < 100)
                {
                        veil->mark[0] = rect[0] - line, veil->mark[1] = rect[1] - line;
                        veil->mark[2] = rect[2] + 2 * line, veil->mark[3] = rect[3] + 2 * line;
                        veil->mark_ink = (ground & 0xffffffu) | (100 - percent) * 255 / 100 << 24;
                }
                added += 2;
        }
        if (shown && compositor.grid_on && compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
                const int *screen = screens[compositor.grid_screen];
                int scale = compositor_overlay_scale(compositor.grid_screen);
                unsigned int line = (unsigned int)compositor_line_px(scale);

                for (unsigned int tile = 0; tile < COMPOSITOR_GRID_TILES; tile++)
                {
                        int box[4];
                        struct compositor_frame *tint = &frames[at + added], *border = &frames[at + added + 1];
                        unsigned int ink = tile == compositor.grid_hover ? compositor_snap_ink() : COMPOSITOR_GRID_TINT;

                        compositor_grid_tile(screen, tile, box);
                        memory_zero(tint, sizeof(*tint));
                        tint->scale = compositor.scale;
                        memory_copy(tint->mark, box, sizeof(box));
                        tint->mark_ink = (ink & 0xffffffu) | ((ink >> 24) * percent / 100) << 24;
                        memory_zero(border, sizeof(*border));
                        border->scale = compositor.scale;
                        border->x = box[0], border->y = box[1];
                        border->width = box[2], border->height = box[3];
                        border->radius = UI_RADIUS * scale / 100, border->line = (int)line;
                        border->edge = compositor_snap_edge(percent);
                        added += 2;
                }
                for (unsigned int tile = 0; tile < COMPOSITOR_GRID_TILES; tile++)
                {
                        int part[4], tile_box[4], box[4];
                        unsigned int how = compositor_grid_regions[tile];

                        compositor_grid_tile(screen, tile, tile_box);
                        compositor_screen_part(screen, how, part);
                        box[0] = tile_box[0] + (part[0] - screen[0]) * tile_box[2] / screen[2];
                        box[1] = tile_box[1] + (part[1] - screen[1]) * tile_box[3] / screen[3];
                        box[2] = part[2] * tile_box[2] / screen[2];
                        box[3] = part[3] * tile_box[3] / screen[3];
                        taken = (compositor_region_cells(how) & compositor_occupied(compositor.drag, compositor.grid_screen)) != 0;
                        grid_ink = taken ? COMPOSITOR_GRID_TAKEN : tile == compositor.grid_hover ? compositor_snap_ink() : COMPOSITOR_GRID_TINT;
                        memory_zero(&frames[at + added], sizeof(frames[0]));
                        frames[at + added].scale = compositor.scale;
                        memory_copy(frames[at + added].preview, box, sizeof(box));
                        frames[at + added].preview_ink = (grid_ink & 0xffffffu) | ((grid_ink >> 24) * percent / 100) << 24;
                        added++;
                }
        }
        return added;
}

/*
        Whether a drag's pointer at x, y is resting in the top band of screen at where that band may bring the grid: the
        screen's top there is free (no screen above it: a drag on its way up to another screen crosses the band and is
        not caught), and Control, which is no snap, is not held.
*/
static int compositor_grid_rests(const int (*screens)[4], unsigned int count, int at, int x, int y)
{
        int point[4] = {x, y, 1, 1};

        return at >= 0 && compositor_grid_band(screens[at], x, y) && !(compositor_modifiers() & WINDOW_KEY_CONTROL) &&
               (compositor_snap_free(screens, count, point, at) & COMPOSITOR_SNAP_TOP);
}

/*
        The grid as a drag's pointer is at x, y. It comes up only once the pointer has rested in a free top band for
        COMPOSITOR_GRID_DWELL_NS (grid_since is when it came to it; the loop wakes for the rest, compositor_snap_due), so a
        window thrown up to the top is filled at once and the grid is for one held there. Up, it stays while the pointer is
        in its row of tiles, so a drop there can take a region. The snap assist is not shown during a drag, only the
        preview: the thumbnails are offered after the drop (compositor_release_as).
*/
static void compositor_overlays_drag(int x, int y)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_at(screens, count, x, y), rests = compositor_grid_rests(screens, count, at, x, y);
        positive now = clock_monotonic_nanoseconds();

        compositor_overlays_damage();
        if (!rests)
                compositor.grid_since = 0;
        else if (!compositor.grid_since || compositor.grid_since_screen != at)
                compositor.grid_since = now ? now : 1, compositor.grid_since_screen = at;

        if (at >= 0 && ((rests && now - compositor.grid_since >= COMPOSITOR_GRID_DWELL_NS) ||
                        (compositor.grid_on && compositor.grid_screen == at && compositor_grid_panel(screens[at], x, y) &&
                         !(compositor_modifiers() & WINDOW_KEY_CONTROL))))
        {
                compositor.grid_on = 1;
                compositor.grid_screen = at;
                compositor.grid_hover = compositor_grid_under(screens[at], x, y);
        }
        else
                compositor.grid_on = 0;
        compositor_overlays_damage();
}

// When the grid is due to come up under a pointer resting in the top band (compositor_overlays_drag), or 0 for never.
static positive compositor_snap_due(void)
{
        return compositor.drag >= 0 && compositor.grid_since && !compositor.grid_on ? compositor.grid_since + COMPOSITOR_GRID_DWELL_NS : 0;
}

// The loop woke for the grid: the drag's overlays and preview are read again where the pointer rests.
static void compositor_snap_tick(positive now)
{
        if (compositor_snap_due() && now >= compositor_snap_due())
                compositor_preview(compositor.drag);
}

// The snap assist taken away: a click elsewhere, Escape, or a window placed from it.
static void compositor_assist_close(void)
{
        compositor_overlays_damage();
        compositor.assist_on = compositor.assist_armed = 0;
        // The thumbnails fade out with the overlays (compositor_overlays_keep), unless the grid stays up: it has none of them.
        if (compositor.grid_on)
                compositor.assist_count = 0;
}

// Canonical window arrangement and fullscreen mode transition: saves/restores floating geometry,
// updates fullscreen/tiles/OSK, and handles raise/focus on fullscreen entry.
static void compositor_row_mode(int row, unsigned int arrange, int fullscreen, int screen)
{
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row])
                return;
        if (fullscreen)
        {
                unsigned char was_fs = compositor.fullscreen[row];
                unsigned int was = was_fs ? compositor.fullscreen_was[row] : compositor.arranged[row];
                unsigned int count;
                const int (*screens)[4] = compositor_screens(&count);
                int part[4], on = screen >= 0 && (unsigned int)screen < count ? screen : compositor_screen_of(row);

                // Set fullscreen before arrange so compositor_row_clamp/stretch do not clamp fixed-size fullscreen windows.
                compositor.fullscreen[row] = 1;
                compositor_screen_part(screens[on], COMPOSITOR_MAXIMIZED, part);
                compositor_arrange_rect(row, COMPOSITOR_MAXIMIZED, part);
                compositor_tile_others(row, COMPOSITOR_MAXIMIZED, on);
                if (compositor.arranged[row] != COMPOSITOR_MAXIMIZED)
                {
                        compositor.fullscreen[row] = was_fs;
                        return;
                }
                compositor.fullscreen_was[row] = (unsigned char)was;
                compositor_raise(row);
                compositor_focus(row);
                return;
        }
        if (arrange == COMPOSITOR_FLOATING)
        {
                int was[4], saved[4];

                if (compositor.arranged[row] == COMPOSITOR_FLOATING)
                {
                        compositor.fullscreen[row] = 0;
                        return;
                }
                compositor_osk_unpan(row);
                was[0] = compositor.x[row], was[1] = compositor.y[row], was[2] = (int)compositor.w[row], was[3] = (int)compositor.h[row];
                compositor_row_saved_get(row, saved);
                compositor_damage_row(row);
                compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0;
                compositor.edges[row] = 0;
                compositor_put(row, saved[0], saved[1]);
                compositor_damage_row(row);
                compositor_want(row, saved[2], saved[3]);
                compositor_glide_start(row, was);
                compositor_osk_sync();
                return;
        }
        {
                unsigned int count;
                const int (*screens)[4] = compositor_screens(&count);
                int part[4], on = screen >= 0 && (unsigned int)screen < count ? screen : compositor_screen_of(row);

                compositor.fullscreen[row] = 0;
                compositor_screen_part(screens[on], arrange, part);
                compositor_arrange_rect(row, arrange, part);
                // A maximized window covers every tile on its screen, so they float back as a half that a half covers does.
                if (compositor_region_cells(arrange))
                        compositor_tile_others(row, arrange, on);
        }
}

// Promote a popup/override row to a managed toplevel (optionally fullscreen on fullscreen_output >= 0).
static void compositor_row_promote_toplevel(int row, int fullscreen_output)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (compositor.table)
        {
                compositor.table->glass[row] &= ~WINDOW_NO_TAB;
                compositor.table->popup_parent[row] = 0;
        }
        compositor.asks[row] &= ~WINDOW_NO_TAB;
        compositor_popup_parent[row] = -1;
        if (fullscreen_output >= 0)
                compositor_row_mode(row, COMPOSITOR_MAXIMIZED, 1, fullscreen_output);
        compositor_raise(row);
        compositor_focus(row);
        compositor_tabs_changed();
}

// All of a screen or half of it (compositor_screen_part): the screen named, or with none (-1) the window's own
// (compositor_screen_of). A tile leaves the others on the screen their share (compositor_tile_others).
static void compositor_arrange_on(int row, unsigned int how, int on)
{
        compositor_row_mode(row, how, 0, on);
}

static void compositor_arrange(int row, unsigned int how)
{
        compositor_row_mode(row, how, 0, -1);
}

static void compositor_fullscreen_on(int row, int screen)
{
        compositor_row_mode(row, COMPOSITOR_MAXIMIZED, 1, screen);
}

static void compositor_fullscreen_row(int row)
{
        compositor_row_mode(row, COMPOSITOR_MAXIMIZED, 1, -1);
}

// Set a window's stacking layer (BACKGROUND, NORMAL, ABOVE, OVERLAY). BACKGROUND windows fill their output behind all windows and tabs.
static void compositor_layer_set(int row, signed char layer, int screen)
{
        layer = compositor_layer_clamp(layer);
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row])
                return;
        compositor.layer[row] = layer;
        if (compositor.table)
                compositor.table->layer[row] = layer;
        if (layer == WINDOW_LAYER_BACKGROUND)
        {
                compositor.fullscreen[row] = 0;
                compositor.asks[row] |= WINDOW_NO_TAB;
                if (compositor.table)
                        compositor.table->glass[row] |= WINDOW_NO_TAB;
                compositor_arrange_on(row, COMPOSITOR_MAXIMIZED, screen);
        }
        compositor_raise(row);
        compositor_tabs_changed();
}

// Out of fullscreen: back to what it filled before (fullscreen_was), a floating window to the place and size it had. A window that is not
// fullscreen is left as it is.
static void compositor_fullscreen_off(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.fullscreen[row])
                return;
        compositor_row_mode(row, compositor.fullscreen_was[row], 0, -1);
        // The band comes back, and what it covers is drawn again.
        compositor_damage_row(row);
}

// F11 (compositor_fullscreen_key): a fullscreen window out of it, any other into it on the screen it is mostly on.
static void compositor_fullscreen_toggle(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (compositor.fullscreen[row])
                compositor_fullscreen_off(row);
        else
                compositor_fullscreen_on(row, -1);
}

// Back to the rectangle it had, wherever it was put since.
static void compositor_float(int row)
{
        compositor_row_mode(row, COMPOSITOR_FLOATING, 0, -1);
}

/*
        Taking an arranged window takes the window it was: back to the size it had, under the hand where the hand had it.
        Where the press took it (press_x, press_y) keeps its share of the width, so taking the right side does not make the
        window jump until its left is under the pointer; taken by its top, it keeps its distance from the top (the top is
        the same height whatever the window's size), and taken lower down (Alt) its share of the height. The pointer is
        always on the window it takes.
*/
static void compositor_float_for_drag(int row)
{
        int width = (int)compositor.saved_w[row], height = (int)compositor.saved_h[row];

        compositor_osk_unpan(row);
        compositor_row_clamp(row, &width, &height);
        int in_x = compositor.press_x - compositor.x[row], in_y = compositor.press_y - compositor.y[row];
        int grab = (int)((long)in_x * width / (compositor.w[row] ? (int)compositor.w[row] : 1));
        int at = in_y < COMPOSITOR_GRAB * compositor.scale / 100
                     ? in_y
                     : (int)((long)in_y * height / (compositor.h[row] ? (int)compositor.h[row] : 1));

        grab = compositor_fit(grab, 0, width - 1);
        at = compositor_fit(at, 0, height - 1);

        /*
                It shrinks to its own size about the hand: it glides from the tile as it is drawn, moved by as much as the hand
                has moved since the press (so the point pressed is under the pointer), to its own size, and the glide follows
                the hand (compositor_glide_origin). The point pressed keeps its share of both, so it stays under the pointer
                all the way. Reduced motion is its size at once.
        */
        struct compositor_frame drawn;

        compositor_frame_of(row, &drawn);
        if (compositor.glide[row])
                compositor_glide_damage_now(row);
        compositor.glide[row] = 0;
        compositor_damage_row(row);
        compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0;
        compositor.edges[row] = 0;
        compositor_put(row, compositor.pointer_x - grab, compositor.pointer_y - at);
        compositor_damage_row(row);
        compositor_want(row, width, height);
        // The hand holds it by the grab, wherever the screen's edge keeps the window: read from the place the window was kept
        // at, a tile torn off by the edge of the screen would slide under the pointer, and its edge would never reach the
        // screen's again to snap.
        compositor.drag_x = grab;
        compositor.drag_y = at;
        if (compositor_glides())
        {
                int from[4] = {drawn.x + compositor.pointer_x - compositor.press_x, drawn.y + compositor.pointer_y - compositor.press_y,
                               drawn.width, drawn.height};

                compositor_glide_begin(row, from);
                compositor.drag_glide = row + 1;
        }
}

/*
        Where a window goes the first time it shows: where it asked to be,
        and a window asking for the place another holds is put down and
        across, twenty pixels a step, until it is on none that it can be
        seen from. Bounded: eight steps down is most of the way across a
        small screen, and past that the honest answer is to stop and let
        them overlap; a step that would put it off the bottom or the right
        stops the search too.
*/
static void compositor_first_place(int row, int *x, int *y)
{
        int step = COMPOSITOR_STEP * compositor.scale / 100;
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int on = compositor_screen_at(screens, count, *x, *y);
        // The screen it asked to be on, or the primary: the search stops at that screen's bottom and right.
        int right = screens[on < 0 ? 0 : on][0] + screens[on < 0 ? 0 : on][2];
        int bottom = screens[on < 0 ? 0 : on][1] + screens[on < 0 ? 0 : on][3];

        for (int at = 0; at < 8; at++)
        {
                int taken = 0, tx = *x + at * step, ty = *y + at * step, kx = tx, ky = ty;

                if (tx + (int)compositor.w[row] > right || ty + (int)compositor.h[row] > bottom)
                        break;
                // Where the place is kept to (compositor_place: a window's line inside the screen), which is where one put there before
                // is: a window asking for 0,0 (a Wayland or an X top-level, which ask for no place) lands at 1,1, and the next one asking
                // 0,0 was on it, as 0,0 was no window's.
                compositor_screen_keep_window(screens, count, &kx, &ky, (int)compositor.w[row], (int)compositor.h[row], compositor_line(row),
                                              COMPOSITOR_GRAB * compositor.scale / 100);
                for (int other = 0; other < (int)compositor_rows_in_use; other++)
                        taken |= other != row && compositor.showing[other] &&
                                 ((compositor.x[other] == tx && compositor.y[other] == ty) || (compositor.x[other] == kx && compositor.y[other] == ky));

                if (!taken)
                {
                        *x += at * step;
                        *y += at * step;
                        return;
                }
        }
}

// The room of a screen, the band on it where it is now taken out: the primary's, or with a band on every screen
// (`taskbar display all`) the screen's own.
static void compositor_room(const int *screen, int *room)
{
        memory_copy(room, screen, 4 * sizeof(int));
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();
                int band[4], cut[4];

                taskbar_band(&view, &band[0], &band[1], &band[2], &band[3]);
                place_room(screen, band, view.position, cut);
                if (memory_compare(cut, screen, sizeof(cut)))
                {
                        memory_copy(room, cut, sizeof(cut));
                        break;
                }
        }
        compositor.band_at = 0;
}

// The taskbar moved, or its screen changed: every window that fills the room of a screen fills it where it is now.
static void compositor_room_follow(void)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int room[4];

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                unsigned int on;

                if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_ROOM)
                        continue;
                on = place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2,
                                     compositor.y[row] + (int)compositor.h[row] / 2, screens, count);
                compositor_room(screens[on], room);
                if (room[0] != compositor.x[row] || room[1] != compositor.y[row] || room[2] != (int)compositor.w[row] ||
                    room[3] != (int)compositor.h[row])
                        compositor_arrange_rect(row, COMPOSITOR_ROOM, room);
        }
}

/*
        A row's placement, read once into the compositor's own copy and clamped there: a mode it knows, a screen it
        can name, numbers inside WINDOW_PLACE_MOST and a size of at least a pixel. Anything else is the mode or the
        part left out, which is where the program asked, as a window always went. The name too, kept for what the
        compositor remembers, and none when it is not one.
*/
static void compositor_place_read(int row, struct window_place *place)
{
        struct windows *table = compositor.table;
        unsigned int how = __atomic_load_n(&table->place[row], __ATOMIC_ACQUIRE);
        int x = table->place_x[row], y = table->place_y[row];
        unsigned int width = table->place_width[row], height = table->place_height[row], screen = table->place_screen[row];

        memory_zero(place, sizeof(*place));
        if ((how & WINDOW_PLACE_HOW) <= WINDOW_PLACE_FULLSCREEN &&
            ((how & WINDOW_PLACE_HOW) != WINDOW_PLACE_AT ||
             (x >= -WINDOW_PLACE_MOST && x <= WINDOW_PLACE_MOST && y >= -WINDOW_PLACE_MOST && y <= WINDOW_PLACE_MOST)))
        {
                place->how = how & WINDOW_PLACE_HOW;
                place->x = x;
                place->y = y;
        }
        place->how |= how & WINDOW_PLACE_FREE;
        if ((how & WINDOW_PLACE_SCREEN) && screen < WINDOW_PLACE_SCREENS)
        {
                place->how |= WINDOW_PLACE_SCREEN;
                place->screen = screen;
        }
        if ((how & WINDOW_PLACE_SIZE) && width && height && width <= WINDOW_PLACE_MOST && height <= WINDOW_PLACE_MOST)
        {
                place->how |= WINDOW_PLACE_SIZE;
                place->width = width;
                place->height = height;
        }
        for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                place->name[at] = ((volatile char *)table->name[row])[at];
        place->name[WINDOWS_NAME - 1] = 0;
        if (!window_name_valid(place->name))
                memory_zero(place->name, sizeof(place->name));
        memory_copy(compositor.name[row], place->name, WINDOWS_NAME);
}

/*
        Where windows were last put, kept (window.c's places): the files are /root's, and a checks run names others.
        The file is read when a window of a name opens and read, noted in and written whole when the hand lets one
        go, so nothing is held between the two and `canvas windows forget`, which takes the file away, needs to tell
        the compositor nothing.
*/
static const char *compositor_places_path = SETTING_WINDOWS_PLACES_PATH;
static const char *compositor_remember_path = SETTING_WINDOWS_REMEMBER_PATH;

// Whether windows are remembered: `canvas windows remember`, on unless it keeps off.
static int compositor_remembering(void)
{
        char word[16];
        long value = 0;

        compositor_word(compositor_remember_path, word, sizeof(word));
        return !(compositor_setting_kept(SETTING_WINDOWS_REMEMBER_PATH, word, &value) && value == 1);
}

static void compositor_places_read(struct places *places)
{
        static char text[PLACES_BYTES + 1];

        compositor_text(compositor_places_path, text, sizeof(text));
        places_parse(places, text, string_length((string_address)text));
}

/*
        The places written whole beside the file and renamed over it, so a reader finds the old one or the new and
        never half of either. Not synced: a drag's end is not to wait on a disk, and what a power cut takes is the
        last place a window was put, which the next drag puts back.
*/
static void compositor_places_write(const struct places *places)
{
        static char text[PLACES_BYTES];
        unsigned long length = places_say(places, text, sizeof(text));

        (void)file_write_atomic_at(AT_FDCWD, (string_address)compositor_places_path, text, (positive)length, 0600);
}

// The screen a kept place goes on: the one of its connector's name, else the one nearest where it was, else the first.
static unsigned int place_kept_screen(const struct place_kept *kept, const char (*names)[WINDOWS_NAME],
                                      const int (*screens)[4], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
                if (!memory_compare(names[at], kept->screen, WINDOWS_NAME))
                        return at;
        return place_screen_at(kept->origin_x + kept->x + kept->width / 2, kept->origin_y + kept->y + kept->height / 2,
                               screens, count);
}

// The screens' names, each the full WINDOWS_NAME bytes, nulls after.
static void compositor_screen_names(char (*names)[WINDOWS_NAME], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
        {
                const char *name = compositor_output_name(at);
                unsigned int letter = 0;

                memory_zero(names[at], WINDOWS_NAME);
                for (; name[letter] && letter < WINDOWS_NAME - 1; letter++)
                        names[at][letter] = name[letter];
        }
}

/*
        Where the hand left a window, noted under its name: a window that has one and floats, let go of by the hand
        after a drag or a resize. Its place is kept from the top left of the screen its middle is on; a place off
        that screen is not kept, and nothing is written when the newest line already says it.
*/
static void compositor_remember(int row)
{
        char names[WINDOW_PLACE_SCREENS][WINDOWS_NAME];
        unsigned int count, on;
        const int (*screens)[4];
        struct places places;
        struct place_kept kept;

        if (row < 0 || !compositor.showing[row] || !compositor.name[row][0] ||
            compositor.arranged[row] != COMPOSITOR_FLOATING || !compositor_remembering())
                return;
        screens = compositor_screens(&count);
        on = place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2, compositor.y[row] + (int)compositor.h[row] / 2,
                             screens, count);
        compositor_screen_names(names, count);
        memory_zero(&kept, sizeof(kept));
        memory_copy(kept.name, compositor.name[row], WINDOWS_NAME);
        memory_copy(kept.screen, names[on], WINDOWS_NAME);
        kept.origin_x = screens[on][0];
        kept.origin_y = screens[on][1];
        kept.x = compositor.x[row] - screens[on][0];
        kept.y = compositor.y[row] - screens[on][1];
        kept.width = (int)compositor.w[row];
        kept.height = (int)compositor.h[row];
        if (kept.x < 0 || kept.y < 0 || kept.x >= screens[on][2] || kept.y >= screens[on][3] || kept.width < 1 ||
            kept.height < 1 || kept.width > WINDOW_PLACE_MOST || kept.height > WINDOW_PLACE_MOST ||
            !place_screen_name_valid(kept.screen))
                return;
        compositor_places_read(&places);
        if (places.count && !memory_compare(&places.kept[0], &kept, sizeof(kept)))
                return;
        places_note(&places, &kept);
        compositor_places_write(&places);
}

/*
        A window of a name put where the hand last left one: on the screen it was on, or the nearest to where that
        was, at the size it had up to the screen's, a step down and across from a window already there, and kept
        on the screen. False when no place is kept for it, and it goes where it would have.
*/
static int compositor_recall(int row, const int (*screens)[4], unsigned int count)
{
        char names[WINDOW_PLACE_SCREENS][WINDOWS_NAME];
        int least_w = compositor_least(row, 0), least_h = compositor_least(row, 1), found, x, y;
        unsigned int on, width, height;
        struct places places;

        if (!compositor.name[row][0] || !compositor_remembering())
                return 0;
        compositor_places_read(&places);
        found = places_find(&places, compositor.name[row]);
        if (found < 0)
                return 0;
        compositor_screen_names(names, count);
        on = place_kept_screen(&places.kept[found], names, screens, count);
        x = screens[on][0] + places.kept[found].x;
        y = screens[on][1] + places.kept[found].y;
        width = (unsigned int)place_size((unsigned int)places.kept[found].width, screens[on][2], least_w);
        height = (unsigned int)place_size((unsigned int)places.kept[found].height, screens[on][3], least_h);
        compositor_put(row, x, y);
        if (width != compositor.w[row] || height != compositor.h[row])
                compositor_want(row, (int)width, (int)height);
        compositor_first_place(row, &x, &y);
        place_clamp(screens[on], (int)compositor.w[row], (int)compositor.h[row], compositor_line(row), &x, &y);
        compositor_put(row, x, y);
        return 1;
}

/*
        Where a row's window with no place of its own goes (place_free): on its owner's window when that shows, in the room of
        the owner's screen; else in the room of the screen named, or of the one under the pointer.
*/
static void compositor_free_place(int row, const struct window_place *place, const int (*screens)[4], unsigned int count, int *x,
                                  int *y)
{
        int owner = compositor_owner[row] - 1, room[4], rect[4];
        b32 owned = owner >= 0 && owner < (int)compositor_rows_in_use && owner != row && compositor.showing[owner];
        unsigned int on = place->how & WINDOW_PLACE_SCREEN && place->screen < count ? place->screen
                          : owned ? place_screen_at(compositor.x[owner] + (int)compositor.w[owner] / 2,
                                                    compositor.y[owner] + (int)compositor.h[owner] / 2, screens, count)
                                  : place_screen_at(compositor.pointer_x, compositor.pointer_y, screens, count);

        compositor_room(screens[on], room);
        if (owned)
                rect[0] = compositor.x[owner], rect[1] = compositor.y[owner], rect[2] = (int)compositor.w[owner], rect[3] = (int)compositor.h[owner];
        place_free(room, owned ? rect : 0, (int)compositor.w[row], (int)compositor.h[row], compositor_line(row), x, y);
}

/*
        Where a window goes the first time it shows, from where its program asked (x, y) and the placement it was
        given: none is where the hand last put a window of its name (compositor_recall), and with nothing kept for it
        where it asked when the place is its own, and centred by compositor_free_place when it has none
        (WINDOW_PLACE_FREE: a program that gave no place, a Wayland toplevel, an X window with no USPosition or
        PPosition), on its owner or the screen under the pointer, where it was put at 0,0 or where it asked and
        stepped down and across off any window there; center and at are the rectangle place_rect works out, on the screen named or the first;
        maximized and fullscreen are where it would have gone with none, filling the room or the screen from there,
        so taking it out of that gives it back the place and size it would have had. A size is asked of the
        program as the hand asks one, and the window is shown at it until a picture of it comes.
*/
static void compositor_first_put(int row, int x, int y)
{
        int room[4], rect[4];
        int least_w = compositor_least(row, 0), least_h = compositor_least(row, 1);
        unsigned int count, on, how, fills;
        const int (*screens)[4] = compositor_screens(&count);
        struct window_place place;

        compositor_place_read(row, &place);
        // A placement stands over where the hand last put a window of the name; a name alone is no placement.
        if (!(place.how & (WINDOW_PLACE_HOW | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE)) && compositor_recall(row, screens, count))
                return;
        how = place.how & WINDOW_PLACE_HOW;
        on = (place.how & WINDOW_PLACE_SCREEN) && place.screen < count ? place.screen : 0;
        compositor_room(screens[on], room);

        // Where it asked is the first screen's, and the same place on the screen named.
        x += screens[on][0] - screens[0][0];
        y += screens[on][1] - screens[0][1];
        if (how == WINDOW_PLACE_AUTO || how == WINDOW_PLACE_MAXIMIZED || how == WINDOW_PLACE_FULLSCREEN)
        {
                if (place.how & WINDOW_PLACE_SIZE)
                {
                        compositor_put(row, x, y);
                        compositor_want(row, place_size(place.width, screens[on][2], least_w),
                                        place_size(place.height, screens[on][3], least_h));
                }
                // No place of its own: the compositor's (compositor_free_place). A place of its own is where it is, kept on the screen.
                if (place.how & WINDOW_PLACE_FREE)
                        compositor_free_place(row, &place, screens, count, &x, &y);
                compositor_place(row, x, y);
                if (how == WINDOW_PLACE_AUTO)
                        return;
        }
        fills = place_rect(&place, screens[on], room, x, y, compositor.w[row], compositor.h[row], compositor_line(row),
                           least_w, least_h, rect);
        if (fills != COMPOSITOR_FLOATING)
        {
                unsigned int was = compositor.fullscreen[row] ? compositor.fullscreen_was[row] : compositor.arranged[row];
                unsigned char want_fs = fills == COMPOSITOR_MAXIMIZED && how == WINDOW_PLACE_FULLSCREEN;

                if (want_fs)
                        compositor.fullscreen[row] = 1, compositor.fullscreen_was[row] = (unsigned char)was;
                compositor_arrange_rect(row, fills, rect);
                if (want_fs && compositor.arranged[row] != COMPOSITOR_MAXIMIZED)
                        compositor.fullscreen[row] = 0;
                return;
        }
        compositor_put(row, rect[0], rect[1]);
        if (rect[2] != (int)compositor.w[row] || rect[3] != (int)compositor.h[row])
                compositor_want(row, rect[2], rect[3]);
}

/*
        A window that shows, put again where its row's placement says now (`moonwater window move`): taken out of
        what it fills first, as a hand taking it would be, then put as compositor_first_put puts a new one, from
        where it is, on the screen named or the one it is on; with a screen and no mode it is at the same place on
        that screen. It is kept on the screen whatever it asked. One the hand holds is the hand's, and a placement
        of nothing leaves it where it is. The name it opened with is its name still.
*/
static void compositor_move(int row)
{
        int room[4], rect[4];
        int least_w = compositor_least(row, 0), least_h = compositor_least(row, 1);
        unsigned int count, from, on, fills;
        const int (*screens)[4] = compositor_screens(&count);
        char name[WINDOWS_NAME];
        struct window_place place;

        memory_copy(name, compositor.name[row], WINDOWS_NAME);
        compositor_place_read(row, &place);
        memory_copy(compositor.name[row], name, WINDOWS_NAME);
        if (compositor.drag == row || compositor.resize == row ||
            !(place.how & (WINDOW_PLACE_HOW | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE)))
                return;
        compositor_float(row);
        from = place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2, compositor.y[row] + (int)compositor.h[row] / 2,
                               screens, count);
        on = (place.how & WINDOW_PLACE_SCREEN) && place.screen < count ? place.screen : from;
        compositor_room(screens[on], room);
        fills = place_rect(&place, screens[on], room, compositor.x[row] - screens[from][0] + screens[on][0],
                           compositor.y[row] - screens[from][1] + screens[on][1], compositor.w[row], compositor.h[row],
                           compositor_line(row), least_w, least_h, rect);
        if (fills != COMPOSITOR_FLOATING)
        {
                compositor_arrange_rect(row, fills, rect);
                return;
        }
        place_clamp(screens[on], rect[2], rect[3], compositor_line(row), &rect[0], &rect[1]);
        compositor_damage_row(row);
        compositor_put(row, rect[0], rect[1]);
        compositor_damage_row(row);
        if (rect[2] != (int)compositor.w[row] || rect[3] != (int)compositor.h[row])
                compositor_want(row, rect[2], rect[3]);
}

static void compositor_reveal(int row);

/*
        WINDOW LISTS. A window that publishes its frame as a list (window.c's
        window_list_of, ui/ui.c's format) is drawn by the compositor into its
        picture, buffer 0, when the list is new and valid, and the picture is shown
        as any picture is. compositor_gpu_list_render draws it: the list (checked by
        window_list_valid; it reads the ops with ui_draw_list_open and checks them
        with ui_draw_list_still, a torn read answered 0), the picture, its pitch in
        pixels, the picture's width and height, and the damage it sets (x0 y0 x1 y1,
        device pixels). It answers 1 when the picture is drawn, or 0 when it cannot
        draw the list: the program's next frame is then its pixels.

        The renderer is the card's (compositor_gpu_list_render_bytes in the GPU
        section: the list cut to what a window is made of, the picture mapped for the
        card to write, one scene job); a build with no card answers 0 for every list.
        A check that draws its own renderer defines COMPOSITOR_LIST_RENDER_EXTERNAL first.
*/
static int compositor_gpu_list_render(int row, const unsigned int *list, unsigned int *picture, unsigned long pitch_words, int width,
                                      int height, int *damage);
#ifndef COMPOSITOR_LIST_RENDER_EXTERNAL
static int compositor_gpu_list_render(int row, const unsigned int *list, unsigned int *picture, unsigned long pitch_words, int width,
                                      int height, int *damage)
{
#ifdef MOONWATER_GPU_C
        return compositor_gpu_list_render_bytes(row, (unsigned char *)list, WINDOW_LIST_BYTES, picture, pitch_words, width, height, damage);
#else
        (void)row, (void)list, (void)picture, (void)pitch_words, (void)width, (void)height, (void)damage;
        return 0;
#endif
}
#endif

/*
        A row's list at a new picture (its generation is not the one taken last, and the table is in list mode): drawn into the picture
        and the row's state 1, or refused (state 2, the whole picture, and counted in list_refused). A picture that is not a list, or a
        list in the mode without lists, clears a refusal, so the program's next frame is tried as a list again.
*/
static unsigned char *compositor_listed; // the row's picture in buffer 0 is the card's drawing of its last list (so a partial list is a change over it)
/*
        A list that comes while the card is busy (a present's jobs on it, or a list's) waits for it, where it was refused: a refusal has the program paint
        the whole frame in pixels and its next list be whole, and at a card's idle clock (the Deck's 200 MHz, a present's job 2.4 ms) the lists of every
        window were refused that way most of the time they changed. The take answers not now, the row is put in list_later, and the card's tick takes it
        again once the card is idle (compositor_lists_later). COMPOSITOR_LIST_WAITS such turns in a row and it is refused as before, so a card that stays
        busy never holds a window's frame back.
*/
#define COMPOSITOR_LIST_WAITS 4
static unsigned char *compositor_list_waits;
static unsigned int compositor_list_invalids[WINDOW_LIST_REASONS];

// A list window_list_check refused, counted by its reason in the table's list_invalid, and for one of another size the list's picture and the
// size the row's picture was taken at.
static void compositor_list_invalid(struct windows *table, unsigned int why, const struct ui_draw_view *view, unsigned int width, unsigned int height)
{
        compositor_list_invalids[why]++;
        table->list_invalid[why] = compositor_list_invalids[why];
        if (why == WINDOW_LIST_SIZE)
        {
                table->list_size_list[0] = view->width, table->list_size_list[1] = view->height;
                table->list_size_taken[0] = width, table->list_size_taken[1] = height;
        }
}

static int compositor_lists_take(int row, unsigned int width, unsigned int height, int *damage)
{
        struct windows *table = compositor.table;
        unsigned char *list = window_list_of(table, row);
        struct ui_draw_view view;
        unsigned int gen, invalid;

        if (!list)
                return 0;
        gen = __atomic_load_n(&((unsigned int *)list)[UI_DRAW_LIST_GEN], __ATOMIC_ACQUIRE);
        if (gen == table->list_seen[row] || !__atomic_load_n(&table->list_mode, __ATOMIC_ACQUIRE))
        {
                if (compositor_listed[row] && __atomic_load_n(&table->list_mode, __ATOMIC_ACQUIRE)) // a switch from the card's picture to the program's
                {
                        compositor_list_counts[COMPOSITOR_LIST_PIXELS]++;
                        table->list_why[COMPOSITOR_LIST_PIXELS] = compositor_list_counts[COMPOSITOR_LIST_PIXELS];
                }
                compositor_listed[row] = 0; // no new list with this frame, or no list mode: the frame is the program's pixels
                table->list_seen[row] = gen;
                if (__atomic_load_n(&table->list_state[row], __ATOMIC_ACQUIRE) == 2)
                        __atomic_store_n(&table->list_state[row], 0u, __ATOMIC_RELEASE);
                return 0;
        }
        // A publish writing the header (an odd generation, now or since the generation was read) is no refusal: the list is not taken this
        // time, nor its generation seen, nor counted, and the take after the publish, which the program's knock brings, looks at it again.
        if (gen & 1)
                return -1;
        invalid = window_list_check(list, (int)width, (int)height, &view);
        if (invalid == WINDOW_LIST_PUBLISHING)
                return -1;
        unsigned int seen_before = table->list_seen[row];

        table->list_seen[row] = gen;
        // A list that is not one to render is refused, and counted by which of window_list_check's tests it failed.
        if (invalid != WINDOW_LIST_VALID)
        {
                compositor_list_invalid(table, invalid, &view, width, height);
                compositor_list_why = COMPOSITOR_LIST_INVALID;
                goto refused;
        }
        // A list is the frame's changes over the picture the card drew from the last one. After a frame the program painted in pixels (a refused or lost frame)
        // buffer 0 is not that picture, so the first list after it has to be the whole frame; a partial one is refused (the program then paints that frame in
        // pixels, whole), or buffer 0 would show the old frame with this frame's band on it, and the window would flicker between the two.
        if (!compositor_listed[row] && (view.x0 > 0 || view.y0 > 0 || view.x1 < (int)width || view.y1 < (int)height))
        {
                compositor_list_why = COMPOSITOR_LIST_PARTIAL;
                goto refused;
        }
        compositor_list_why = COMPOSITOR_LIST_INVALID;
        compositor_list_busy = false;
        if (compositor_gpu_list_render(row, (const unsigned int *)list, compositor_buffer(row, 0), table->stride, (int)width, (int)height, damage) == 1)
        {
                compositor_list_waits[row] = 0;
                compositor_list_counts[COMPOSITOR_LIST_DRAWN]++;
                table->list_why[COMPOSITOR_LIST_DRAWN] = compositor_list_counts[COMPOSITOR_LIST_DRAWN];
                compositor_listed[row] = 1;
                __atomic_store_n(&table->list_state[row], 1u, __ATOMIC_RELEASE);
                return 1;
        }
        // The card busy: not now, the list's generation not seen, and the row taken again when the card is idle.
        if (compositor_list_why == COMPOSITOR_LIST_CARD && compositor_list_busy && compositor_list_waits[row] < COMPOSITOR_LIST_WAITS)
        {
                compositor_list_waits[row]++;
                table->list_seen[row] = seen_before;
                rows_add(&compositor.list_later, row);
                return -1;
        }
refused:
        compositor_list_waits[row] = 0;
        compositor_list_counts[compositor_list_why]++;
        table->list_why[compositor_list_why] = compositor_list_counts[compositor_list_why];
        compositor_listed[row] = 0;
        damage[0] = damage[1] = damage[2] = damage[3] = 0;
        __atomic_store_n(&table->list_state[row], 2u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&table->list_refused, 1, __ATOMIC_RELEASE);
        return -2;
}

// The program the on-screen keyboard is, by its name (/proc/PID/comm, with trailing newline stripped by windows_read_line).
#define COMPOSITOR_OSK_NAME "osk"

// Whether a /proc path names the same file as the compositor's own executable's: the device and the inode, which the link of
// /proc/PID/exe gives for the file it runs, whoever asks. The statx follows the link (no AT_SYMLINK_NOFOLLOW).
static b32 compositor_same_file(const char *path, const unsigned char *mine)
{
        unsigned char theirs[256];

        memory_zero(theirs, sizeof(theirs));
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)path, 0, 0x7ff, (positive)theirs, 0) < 0)
                return 0;
        // struct statx: stx_ino (u64) at 32, stx_dev_major and stx_dev_minor (u32 each) at 136, contiguous.
        return !memory_compare(theirs + 32, mine + 32, 8) && !memory_compare(theirs + 136, mine + 136, 8);
}

/*
        Whether a window's process is the on-screen keyboard (osk.c), the window whose keyboard flags the compositor believes: the
        process runs the compositor's own executable (/proc/self/exe, the multi-call shell, so any applet of it runs that), and its
        name is osk. Another program of the same executable is named otherwise, and a copy of the executable runs another file.
        Judged once when the window is taken (compositor_take), not per key.
*/
static b32 compositor_osk_is(unsigned int pid)
{
        unsigned char mine[256];
        char path[40], comm[8];

        if (!pid)
                return 0;
        memory_zero(mine, sizeof(mine));
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive) "/proc/self/exe", 0, 0x7ff, (positive)mine, 0) < 0)
                return 0;
        windows_num_path(path, sizeof(path), "/proc/", pid, "/exe");
        if (!compositor_same_file(path, mine))
                return 0;
        windows_num_path(path, sizeof(path), "/proc/", pid, "/comm");
        memory_zero(comm, sizeof(comm));
        if (windows_read_line(path, comm, sizeof(comm), FILE_READ | O_CLOEXEC) < 0)
                return 0;
        return string_equals((string_address)comm, (string_address)COMPOSITOR_OSK_NAME);
}

/*
        A knock: what the row says now, read once into the compositor's own
        columns and clamped there. A new title goes to its tab. A new picture
        is damaged and said to be taken; the first puts the window on the
        screen, where it asked to be or was placed (compositor_first_put) and
        on the output, over the others and with the keys unless it asked to
        be put away.
*/

/*
        Who the window in a row is, read from the table into the compositor's own columns: its app_id and its icon (window.c's
        windows_app_id_read and windows_icon_read, which hold whatever the row says to its bounds). A row whose icon a writer is in
        the middle of keeps the one it had until the next knock. Either changing is a change to its tab. It runs at every knock
        (compositor_take), so a program that sets either and knocks, and the Wayland host that sets them for a client and takes the
        row, are both seen at once.
*/
static void compositor_identity_take(int row)
{
        struct windows *table = compositor.table;
        unsigned int seq = __atomic_load_n(&table->icon_seq[row], __ATOMIC_ACQUIRE), changed = 0;
        char app_id[WINDOWS_APP_ID];

        memory_zero(app_id, sizeof(app_id));
        windows_app_id_read(table, (unsigned int)row, app_id);
        if (memory_compare(app_id, compositor.app_id[row], WINDOWS_APP_ID))
        {
                memory_copy(compositor.app_id[row], app_id, WINDOWS_APP_ID);
                changed = 1;
        }
        if (!seq && compositor.icon_seq[row])
                compositor.icon_seq[row] = 0, changed = 1;
        else if (seq && !(seq & 1u) && seq != compositor.icon_seq[row])
        {
                static unsigned int picture[WINDOWS_ICON * WINDOWS_ICON];
                unsigned int read = windows_icon_read(table, (unsigned int)row, picture);

                if (read)
                {
                        memory_copy(compositor.icon[row], picture, sizeof(picture));
                        compositor.icon_seq[row] = read, changed = 1;
                }
        }
        if (changed && compositor.showing[row])
                compositor_tabs_changed();
}

static void compositor_take(int row)
{
        struct windows *table = compositor.table;
        unsigned int frame = __atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE);
        unsigned int width = table->width[row], height = table->height[row], front = table->buffer_index[row] & 1u;
        int damage[4] = {table->damage_x[row], table->damage_y[row], table->damage_width[row], table->damage_height[row]};
        char title[WINDOWS_TITLE];

        // The title, bounded and ended whatever the table holds.
        for (unsigned int at = 0; at < WINDOWS_TITLE; at++)
                title[at] = ((volatile char *)table->title[row])[at];
        title[WINDOWS_TITLE - 1] = 0;
        if (memory_compare(title, compositor.title[row], WINDOWS_TITLE))
        {
                memory_copy(compositor.title[row], title, WINDOWS_TITLE);
                if (compositor.showing[row])
                        compositor_tabs_changed();
        }
        compositor_identity_take(row);

        // Lifted: brought back, raised and given the keys, as its tab would.
        if (compositor.showing[row] && table->lift[row] != compositor.lifted[row])
        {
                compositor.lifted[row] = table->lift[row];
                compositor_reveal(row);
        }
        // Moved: put where its placement says now.
        if (compositor.showing[row] && __atomic_load_n(&table->moved[row], __ATOMIC_ACQUIRE) != compositor.moved[row])
        {
                compositor.moved[row] = table->moved[row];
                compositor_move(row);
        }

        if (frame == compositor.seen[row])
                return;
        compositor_frametrace_note('T', row, clock_monotonic_nanoseconds());

        unsigned int seen = compositor.seen[row];
        b32 reclaimed = rows_has(&compositor.reclaim, row);
        rows_del(&compositor.reclaim, row);
        compositor.seen[row] = frame;
        width = width > (unsigned int)compositor_most_width() ? (unsigned int)compositor_most_width() : width;
        height = height > (unsigned int)compositor_most_height() ? (unsigned int)compositor_most_height() : height;
        // A window's list (window_list_of) is drawn into its picture here (1 drawn, 0 pixels, -1 defer, -2 refused).
        int listed = compositor_lists_take(row, width, height, damage);
        // A publish was writing the list: the frame is not taken now (the window shows what it showed), and the next take takes it.
        if (listed == -1)
        {
                compositor.seen[row] = seen;
                if (reclaimed)
                        rows_add(&compositor.reclaim, row);
                return;
        }
        b32 list_refused = listed == -2;

        if (listed == 1)
        {
                front = 0;
                __atomic_store_n(&table->buffer_index[row], 0u, __ATOMIC_RELEASE);
        }
        else if (list_refused)
        {
                if (compositor.showing[row])
                {
                        front = compositor.front[row] & 1u;
                        width = compositor.picture_w[row];
                        height = compositor.picture_h[row];
                        __atomic_store_n(&table->buffer_index[row], front, __ATOMIC_RELEASE);
                }
        }
        // A window the card shows straight is told the picture was taken once the card has let go of the buffer
        // it showed before, at the flip's event, and one a job of the card may still be reading in place once that job is
        // over (compositor_card_reads; the late present gives those back); any other at once.
        if (row == compositor.direct_row || compositor_card_reads(row))
        {
                compositor.taken_held[row] = frame;
                rows_add(&compositor.held_rows, row);
        }
        else
                __atomic_store_n(&table->shown[row], frame, __ATOMIC_RELEASE);
        rows_add(&compositor.woken, row);

        if (list_refused)
        {
                if (compositor.showing[row] && rows_has(&compositor.held_rows, row) && compositor_visible(row))
                        compositor_damage(compositor.x[row], compositor.y[row], 1, 1);
                return;
        }

        // A picture of nothing shows nothing.
        if (!width || !height)
                return;

        if (!compositor.showing[row])
        {
                int x = table->x[row], y = table->y[row];

                // Bounded to WINDOW_COORD_MAX, so the sums in compositor_first_place cannot wrap (a place of INT_MAX made
                // tx + w overflow). compositor_place keeps the window on the output either way.
                x = x > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : x < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : x;
                y = y > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : y < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : y;
                compositor.w[row] = compositor.picture_w[row] = width;
                compositor.h[row] = compositor.picture_h[row] = height;
                compositor.asking[row] = 0;
                compositor.front[row] = front;
                compositor.state[row] = table->state[row] & WINDOW_MINIMIZED;
                compositor_close_count(row, 0);
                table->state[row] = compositor.state[row];
                compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0, compositor.layer[row] = WINDOW_LAYER_NORMAL;
                compositor.edges[row] = 0;
                compositor.lifted[row] = table->lift[row];
                compositor.moved[row] = table->moved[row];
                compositor.glassy[row] = table->glass[row] & WINDOW_GLASS;
                compositor.asks[row] = table->glass[row] & (WINDOW_NO_CLOSE | WINDOW_DISMISS | WINDOW_NO_TAB | WINDOW_NO_GRAB | WINDOW_NO_KEYS |
                                                            WINDOW_KEYBOARD);
                // The keyboard's two are the on-screen keyboard's own (compositor_osk_is): any other window's are dropped, and it
                // takes the keys as a window with no flags does.
                if ((compositor.asks[row] & (WINDOW_NO_KEYS | WINDOW_KEYBOARD)) && !compositor_osk_is(table->pid[row]))
                        compositor.asks[row] &= ~(WINDOW_NO_KEYS | WINDOW_KEYBOARD);
                compositor.layer[row] = compositor_layer_clamp(table->layer[row]);
                if (compositor.layer[row] == WINDOW_LAYER_BACKGROUND)
                        compositor.asks[row] |= WINDOW_NO_TAB;
                compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
                compositor.showing[row] = 1;
                compositor.made[row] = ++compositor.next_made;
                // A popup lands as a window does (its size grows back about its middle), so a menu opens rather than appearing whole.
                if (compositor_popup_first(row))
                {
                        compositor_land(row);
                        return;
                }
                compositor_first_put(row, x, y);
                if (compositor.layer[row] == WINDOW_LAYER_BACKGROUND)
                        compositor_arrange_on(row, COMPOSITOR_MAXIMIZED, -1);
                compositor_raise(row);
                // Raised over the others, but given the keys only when the person asked for the program (its launch
                // carried WINDOW_ASKED_VARIABLE): a window a program opens on its own shows and leaves the keys alone.
                if (compositor.layer[row] != WINDOW_LAYER_BACKGROUND && compositor_visible(row) &&
                    __atomic_load_n(&table->asked[row], __ATOMIC_ACQUIRE))
                        compositor_focus(row);
                compositor_land(row);
                compositor_tabs_changed();
                if (compositor.asks[row] & WINDOW_KEYBOARD)
                        compositor_osk_sync();
                return;
        }

        compositor.picture_w[row] = width;
        compositor.picture_h[row] = height;
        // The buffer handed back is the program's again, whatever the tabs left on it: it draws that again.
        if (compositor.front[row] != front)
                compositor.patched[row][compositor.front[row] & 1u] = 0;
        compositor.front[row] = front;

        // The picture of the size asked for: shown as it is from here on, and, the hand let go, the edges are the
        // window's own again. One of another size while an ask is out is shown at the size asked, stretched. The
        // other window of a shared edge keeps the edge it shares until the hand lets go (compositor_shared_begin).
        if (compositor.asking[row] && width == table->want_width[row] && height == table->want_height[row])
        {
                compositor.asking[row] = 0;
                if (compositor.resize != row && compositor.shared_row != row)
                        compositor.edges[row] = 0;
        }
        if (!compositor.asking[row] && (width != compositor.w[row] || height != compositor.h[row]))
        {
                int x = compositor.x[row], y = compositor.y[row];

                compositor_damage_row(row);
                compositor.w[row] = width;
                compositor.h[row] = height;
                if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                        x = compositor.far_x[row] - (int)width;
                if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                        y = compositor.far_y[row] - (int)height;
                compositor_place(row, x, y);
                damage[2] = 0;
                // The picture a scale taken made: if it is past the room it is asked to fit it.
                if (rows_has(&compositor.fit, row))
                {
                        rows_del(&compositor.fit, row);
                        if (clock_monotonic_nanoseconds() < compositor.fit_until)
                                compositor_fit_row(row);
                }
        }
        if (width != compositor.w[row] || height != compositor.h[row])
        {
                compositor_stretch(row);
                damage[2] = 0;
        }

        if (!compositor_visible(row))
                return;

        // Clipped to the picture in a type the program's numbers cannot wrap; a width of 0 is all of it, and a
        // height of 0 none.
        {
                long x1 = damage[0] < 0 ? 0 : damage[0], y1 = damage[1] < 0 ? 0 : damage[1];
                long x2 = (long)damage[0] + damage[2], y2 = (long)damage[1] + damage[3];
                const int (*part)[4] = 0;
                int parts = 0;

                x2 = x2 > (long)width ? (long)width : x2;
                y2 = y2 > (long)height ? (long)height : y2;
                compositor.damage_by = row + 1;
                if (damage[2] <= 0)
                        compositor_damage_row(row);
                // A client's commit of several rectangles: each is damaged, cut to the picture as the box is, and not the box
                // round them all (compositor_wl_parts; the table has room for the box alone).
                else if (x2 > x1 && y2 > y1 && (parts = compositor_wl_parts(row, &part)) > 0)
                        for (int at = 0; at < parts; at++)
                        {
                                long px1 = part[at][0] < 0 ? 0 : part[at][0], py1 = part[at][1] < 0 ? 0 : part[at][1];
                                long px2 = (long)part[at][0] + part[at][2], py2 = (long)part[at][1] + part[at][3];

                                px2 = px2 > (long)width ? (long)width : px2;
                                py2 = py2 > (long)height ? (long)height : py2;
                                if (px2 > px1 && py2 > py1)
                                        compositor_damage(compositor.x[row] + (int)px1, compositor.y[row] + (int)py1, (int)(px2 - px1), (int)(py2 - py1));
                        }
                else if (x2 > x1 && y2 > y1)
                        compositor_damage(compositor.x[row] + (int)x1, compositor.y[row] + (int)y1, (int)(x2 - x1),
                                          (int)(y2 - y1));
                // A picture the card shows straight from its buffer is said taken by the present that puts it there,
                // and a present comes only for damage: one that changed no pixel still asks for one (a pixel of the
                // window on the output), or its program waits on it until something else is damaged.
                else if (rows_has(&compositor.held_rows, row))
                {
                        int x = compositor.x[row], y = compositor.y[row];

                        compositor_damage(x < 0 ? 0 : x >= compositor.width ? compositor.width - 1 : x,
                                          y < 0 ? 0 : y >= compositor.height ? compositor.height - 1 : y, 1, 1);
                }
                compositor.damage_by = 0;
        }
}

static void compositor_motion(void);
static void compositor_chip_hide(void);

/*
        The window the pointer is held to (compositor.grab): the one a button still down was pressed in, kept while it is
        the window of one (the first's when more are down), or none. A button let go, or the window of one going, moves
        the hold to the window of another button still down, so the motion and the wheel stay with the press that holds them.
*/
static void compositor_grab_settle(void)
{
        int held = COMPOSITOR_NONE;

        for (unsigned int index = 0; index < 8; index++)
        {
                if (!(compositor.button_down >> index & 1))
                        continue;
                if (compositor.button_row[index] == compositor.grab)
                        return;
                if (held < 0)
                        held = compositor.button_row[index];
        }
        compositor.grab = held;
}

/*
        A row whose door has closed: its program is gone. The window goes
        from the screen, its pixels are given back, and the row is free.
*/
static void compositor_leave(int row)
{
        struct windows *table = compositor.table;
        // The window the pointer was over, or whose shape it wore, goes: the pointer is read again over what is under it, which
        // no motion does while it rests (the hover, the cursor's shape and whether a client hid it).
        int pointed = compositor.hover == row || compositor.cursor.asked_row == row;

        compositor_row_notify_detach(row);

        if (compositor.showing[row])
        {
                compositor_damage_row(row);
                compositor_tabs_changed();
        }
        // A controls chip is its window's: it goes with the window, or a tap where it was would act on the next window in the row.
        if (compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.row == row)
                compositor_chip_hide();
        // A key the keyboard had down is over with it: the next keyboard in the row starts with none held (compositor_osk_read).
        compositor_osk_held[row] = 0;
        compositor_owed_drop(row);
        // Its owner is no one's now, and it is the owner of no window that shows after it.
        compositor_owner[row] = 0;
        for (int other = 0; other < (int)compositor_rows_in_use; other++)
                if (compositor_owner[other] == row + 1)
                        compositor_owner[other] = 0;

        compositor_stack_remove(row);
        // An armed assist ends with its own window, and loses the thumbnail of one that goes (it is no window to place).
        if (compositor.assist_on && compositor.assist_row == row)
                compositor_assist_close();
        else if (compositor.assist_on)
                compositor_assist_refresh();
        if (compositor.keys_to == row)
        {
                compositor.keys_to = COMPOSITOR_NONE;
                memory_zero(compositor.keys_down, sizeof(compositor.keys_down));
        }
        compositor.hover = compositor.hover == row ? COMPOSITOR_NONE : compositor.hover;
        compositor.told_row = compositor.told_row == row ? COMPOSITOR_NONE : compositor.told_row;
        compositor_grab_end(COMPOSITOR_GRAB_DROP, row);
        // A button held over the window that went is let go nowhere: the row is the next program's, which never had
        // the press (compositor_release).
        for (unsigned int index = 0; index < 8; index++)
                if (compositor.button_row[index] == row)
                        compositor.button_down &= (unsigned char)~(1u << index);
        compositor_grab_settle();
        // The stack is one shorter, so an Alt-Tab under way ends here, on the window it stopped on, as letting go of
        // Alt would have ended it: that window is brought back if it was put away, and has the keys.
        if (compositor.cycle)
        {
                compositor.cycle = 0;
                if (compositor.focus >= 0 && compositor.focus != row)
                        compositor_reveal(compositor.focus);
        }

        compositor.showing[row] = compositor.seen[row] = compositor.state[row] = 0;
        compositor_close_count(row, 0);
        compositor.landing[row] = 0;
        compositor.glide[row] = 0;
        compositor.w[row] = compositor.h[row] = compositor.edges[row] = 0;
        compositor.picture_w[row] = compositor.picture_h[row] = compositor.asking[row] = 0;
        compositor.asks[row] = 0;
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.fullscreen[row] = 0;
        compositor.fullscreen_was[row] = COMPOSITOR_FLOATING;
        compositor.layer[row] = WINDOW_LAYER_NORMAL;
        if (table)
                table->layer[row] = WINDOW_LAYER_NORMAL;
        compositor.banded[row] = 0;
        compositor.saved_x[row] = compositor.saved_y[row] = 0;
        compositor.saved_w[row] = compositor.saved_h[row] = 0;
        compositor.title[row][0] = 0;
        memory_zero(compositor.app_id[row], WINDOWS_APP_ID), compositor.icon_seq[row] = 0;
        compositor.name[row][0] = 0;
        compositor_glass_drop(row);
        compositor_runs_drop(row);
        compositor.glass_side[row] = compositor.glass_told[row] = 0;
        // Shown straight: the screen is the compositor's again at the next frame, and the window's framebuffers
        // go when the card is off them (now, if it is).
        rows_del(&compositor.held_rows, row);
        if (compositor.direct_row == row)
        {
                compositor.direct_row = -1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        // On the overlay: the card scans the window's framebuffer there until a commit says otherwise, and the next present makes it
        // (compositor_present's plane off, or the plane given to the window under it). Where the row was only forgotten here, nothing took the
        // overlay off: the closed window stayed on the screen over everything, its framebuffer and pages held (compositor_shows_row), and its
        // pages read white or black as the client let go of the buffer (box: a Chromium window closed with no window to take the plane after it,
        // the overlay still on at its place with its framebuffer, the row gone).
        if (compositor.plane_row == row)
        {
                compositor.plane_row = -1;
                compositor.plane_gone = 1;
                compositor_damage(compositor.plane_rect[0], compositor.plane_rect[1], compositor.plane_rect[2] - compositor.plane_rect[0],
                                  compositor.plane_rect[3] - compositor.plane_rect[1]);
        }
        if (compositor_shows_row(row) || (compositor.flip_pending && compositor.flip_row == row))
                rows_add(&compositor.dropping, row);
        else if (compositor.scan_fb[row][0] || compositor.scan_fb[row][1])
                compositor_window_fb_drop(row);
        // Pinned for a framebuffer the card refused, which was never made: no drop above would let go of it.
        else
                __atomic_store_n(&table->pinned[row], compositor_gpu_pins[row] ? 1u : 0u, __ATOMIC_RELEASE);
        table->spoiled_height[row][0] = table->spoiled_height[row][1] = 0;
        table->ground[row] = 0;
        table->hover_off[row] = 0;
        table->pinch_wanted[row] = 0;
        // A pinch that began on this window goes nowhere now: the row is free to be another window's, and its updates and end
        // are that window's only if it began them (compositor_pinch sends updates to the row the begin named, and no other).
        if (compositor.touch.pinch_row == row)
                compositor.touch.pinch_row = COMPOSITOR_NONE;
        if (compositor.swipe.row == row)
                compositor.swipe.row = COMPOSITOR_NONE;
        if (compositor.swipe.armed_row == row)
                compositor.swipe.armed_row = COMPOSITOR_NONE, compositor.swipe.armed_until = 0;
        table->state[row] = 0;
        table->want_width[row] = table->want_height[row] = 0;
        table->shown[row] = 0;

        // MADV_REMOVE: the pages of its two buffers, deferred while the card still shows or pins the row's pages. The card's maps of them go
        // first (gpu_forget): a map holds the pages it was made over, and the next window of the row was written by the processor into new
        // pages while the card read the old ones, older frames of the last window and blank ones (a browser's page in patches of what it
        // showed before, and the wallpaper through it, until the card was started again).
        if (rows_has(&compositor.dropping, row) || compositor_gpu_pins[row])
                rows_add(&compositor.reclaim, row);
        else
        {
                gpu_forget((const void *)((char *)compositor.table + compositor.pixels + row * 2 * compositor.buffer), 2 * compositor.buffer);
                compositor_call(syscall(madvise), (positive)compositor.table + compositor.pixels + row * 2 * compositor.buffer,
                                2 * compositor.buffer, 9, 0, 0, 0);
        }

        compositor.osk_pan[row] = 0;
        compositor.osk_height[row] = 0;
        compositor.osk_edges[row] = 0;
        if (compositor.osk_turn_focus == row)
                compositor.osk_turn_focus = COMPOSITOR_NONE, compositor.osk_turned = 0, compositor.osk_turn_blur = 0;
        compositor.caret[row] = 0;
        compositor.text_purpose[row] = 0;
        if (compositor.focus == row)
                compositor_refocus();
        rows_del(&compositor.woken, row);
        __atomic_store_n(&table->pid[row], 0, __ATOMIC_RELEASE);
        compositor_osk_sync();
        if (pointed)
                compositor_motion();
}

/*
        What a tab does: its window comes back if it was put away, rises over
        the others and has the keys.
*/
static void compositor_reveal(int row)
{
        if (row < 0 || !compositor.showing[row])
                return;

        if (compositor.state[row] & WINDOW_MINIMIZED)
        {
                compositor.state[row] &= ~WINDOW_MINIMIZED;
                compositor.table->state[row] = compositor.state[row];
                compositor_damage_row(row);
                compositor_tabs_changed();
        }

        compositor_raise(row);
        compositor_focus(row);
}

/*
        Alt-Tab: one step gives the keys to the next window down, put away or
        not, and leaves the stack as it is, so a window put away can be looked
        at and passed; letting go of Alt brings the one it stopped on back if
        it was put away and raises it. Starting from the top, when the window
        with the keys is the one that is on top, it is the one behind; when it
        is not (it was put away and the keys went down) it is the one on top,
        which is the one that was put away, so one Alt-Tab restores it. A window that asked for no tab is passed over
        (compositor_cycles): it has no entry in the cycle.
*/
static void compositor_focus_step(void)
{
        int top = (int)compositor.stacked, focused = 0, below, next;

        for (int at = 0; at < (int)compositor.stacked; at++)
                if (compositor.stack[at] == compositor.focus)
                        focused = at + 1;

        below = compositor.cycle ? compositor.cycle : focused && focused == top ? focused : top + 1;
        next = below > 1 ? below - 1 : top;
        for (int skipped = 0; next && skipped < top && !compositor_cycles(compositor.stack[next - 1]); skipped++)
                next = next > 1 ? next - 1 : top;

        if (!next || next == focused || !compositor_cycles(compositor.stack[next - 1]))
                return;

        compositor.cycle = next;
        compositor_focus(compositor.stack[next - 1]);
}

static void compositor_focus_commit(void)
{
        compositor.cycle = 0;
        compositor_reveal(compositor.focus);
}

// A window put away, by its minimise button or Alt-F9; its tab brings it back. The keys go to the one under it if it had them.
static void compositor_minimize_row(int row)
{
        // A window with no tab (WINDOW_NO_TAB) is in no tab and no Alt-Tab step: put away, nothing would bring it back, so it is not put away.
        if (row < 0 || !compositor_visible(row) || (compositor.asks[row] & WINDOW_NO_TAB))
                return;

        compositor_damage_row(row);
        compositor.state[row] |= WINDOW_MINIMIZED;
        compositor.table->state[row] = compositor.state[row];
        if (compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.row == row)
                compositor_chip_hide();
        if (row == compositor.focus)
                compositor_refocus();
        compositor_tabs_changed();
}


/*
        The on-screen keyboard: the window that asks WINDOW_KEYBOARD (osk.c), shown while the focused window has a text field with
        the keys (the table's field count, ui.c's ui_field_focus) and the setting lets it. on: always, with a field; off: never;
        auto: with a field, when no keyboard is plugged in or the last input was a touch or a pen (not a key of a physical keyboard,
        not the mouse). Shown, it is raised, and the focused window is moved up by the overlap of its bottom with the keyboard's
        top, not past the top of its screen; the amount is kept (osk_pan) and moved back when the keyboard is put away, the focus
        goes elsewhere, or the window goes. It is asked from each place the answer can change: the focus, a knock (a field's count
        is written before its program knocks), a key, a touch or the mouse, the setting, and the rows' make and release.
*/
#define COMPOSITOR_OSK_AUTO 0
#define COMPOSITOR_OSK_OFF 1
#define COMPOSITOR_OSK_ON 2

// The keyboard's row, a window shown that asks WINDOW_KEYBOARD, or -1 when none is.
static int compositor_osk_row(void)
{
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if (compositor.showing[row] && (compositor.asks[row] & WINDOW_KEYBOARD))
                        return (int)row;
        return -1;
}

// A keyboard that is taken as one is plugged in (WINDOWS_INPUT_KEYBOARD). The headless compositor's fifo is not one.
static b32 compositor_keyboard_plugged(void)
{
        for (unsigned int at = 0; at < compositor.input_count; at++)
                if (compositor.inputs[at] >= 0 && (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD))
                        return 1;
        return 0;
}

// A screen that fingers touch is plugged in (compositor_input_add's touchscreen).
static b32 compositor_touchscreen_plugged(void)
{
        for (unsigned int at = 0; at < compositor.input_count; at++)
                if (compositor.inputs[at] >= 0 && compositor.touchscreen[at])
                        return 1;
        return 0;
}

// Whether the desktop starts the keyboard at its start (osk_start): the setting says on, or it is auto and there is a touch
// screen or no physical keyboard to type on.
static b32 compositor_osk_starts(void)
{
        if (compositor.osk_setting == COMPOSITOR_OSK_ON)
                return 1;
        return compositor.osk_setting == COMPOSITOR_OSK_AUTO && (compositor_touchscreen_plugged() || !compositor_keyboard_plugged());
}

// A row put away (the keyboard, while nothing has its field) or shown again; its picture is drawn again both times.
static void compositor_osk_put_away(int row, b32 away)
{
        if (((compositor.state[row] & WINDOW_MINIMIZED) != 0) == away)
                return;
        compositor_damage_row(row);
        if (away)
                compositor.state[row] |= WINDOW_MINIMIZED;
        else
        {
                compositor.state[row] &= ~WINDOW_MINIMIZED;
                compositor_raise(row);
        }
        compositor.table->state[row] = compositor.state[row];
        compositor_damage_row(row);
        rows_add(&compositor.woken, row);
}

// The focused row moved up by the overlap with the keyboard's top, as far as its own screen's top allows: nothing when it is
// already moved or does not overlap. A window that fills its screen (or a part of it) is not moved: it is made as high as the room
// above the keyboard, its top kept, and the height it had is kept to be given back (too little room above, it is left as it is).
static void compositor_osk_pan(int row, int keyboard)
{
        int x, y, width, height, overlap, room, amount, was;

        if (compositor.osk_pan[row] || compositor.osk_height[row])
                return;
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                int above = compositor.y[keyboard] - compositor.y[row], least = compositor_least(row, 1);

                if (above < least || above >= (int)compositor.h[row])
                        return;
                compositor.osk_height[row] = (int)compositor.h[row];
                compositor.osk_edges[row] = compositor.edges[row];
                // The edges are none while it is made shorter, so its top stays where it is (the size is taken off the bottom).
                compositor.edges[row] = 0;
                compositor_want(row, (int)compositor.w[row], above);
                return;
        }
        compositor_output_rect(compositor_screen_of(row), &x, &y, &width, &height);
        // The bottom that must clear the keyboard: the text cursor's, when its client gave one, else the window's own.
        overlap = compositor.y[row] + (compositor.caret[row] > 0 && compositor.caret[row] < (int)compositor.h[row] ? compositor.caret[row] : (int)compositor.h[row]) -
                  compositor.y[keyboard];
        room = compositor.y[row] - y;
        amount = overlap < room ? overlap : room;
        if (amount <= 0)
                return;
        // Kept as it was moved: the screen keeps a window's top where it may be (a pixel below the edge), not always where asked.
        was = compositor.y[row];
        compositor_place(row, compositor.x[row], was - amount);
        compositor.osk_pan[row] = was - compositor.y[row];
}

// A row moved for the keyboard goes back where it was (it may have moved since: the amount is taken off where it is), and a row made
// shorter for it gets its height and edges back.
static void compositor_osk_unpan(int row)
{
        int amount = compositor.osk_pan[row], height = compositor.osk_height[row];

        compositor.osk_pan[row] = 0;
        compositor.osk_height[row] = 0;
        if (height)
        {
                compositor.edges[row] = compositor.osk_edges[row];
                if (compositor.showing[row])
                        compositor_want(row, (int)compositor.w[row], height);
        }
        if (amount && compositor.showing[row])
                compositor_place(row, compositor.x[row], compositor.y[row] + amount);
}

// What moonwater debug says of the keyboard: its setting, whether it is shown, the last input, whether a physical one is plugged in,
// and whether the desktop starts it (compositor_osk_starts, read by `moonwater keyboard start`).
static void compositor_osk_publish(void)
{
        struct windows *table = compositor.table;

        table->osk_setting = compositor.osk_setting;
        table->osk_shown = compositor.osk_shown;
        table->osk_input = compositor.osk_typed ? 2 : compositor.owner != COMPOSITOR_PREDICT_MOUSE ? 1 : 0;
        table->osk_physical = compositor_keyboard_plugged();
        // The layout's name, its two letters packed (us is 'u' | 's' << 8): the keyboard lays its letters out by it (osk.c).
        table->osk_layout = (unsigned int)(unsigned char)compositor_layouts[compositor.keymap.layout].name[0] |
                            (unsigned int)(unsigned char)compositor_layouts[compositor.keymap.layout].name[1] << 8;
        table->osk_start = compositor_osk_starts();
        // The focus's text field: its content purpose, which the keyboard picks its page by (a change wakes it), and its cursor.
        {
                int focus = compositor.focus, keyboard = compositor_osk_row();
                unsigned int purpose = focus >= 0 && table->field[focus] ? compositor.text_purpose[focus] : 0;

                if (table->osk_purpose != purpose && keyboard >= 0)
                        rows_add(&compositor.woken, keyboard);
                table->osk_purpose = purpose;
                table->osk_caret = focus >= 0 && table->field[focus] && compositor.caret[focus] > 0 ? (unsigned int)compositor.caret[focus] : 0;
        }
}

static void compositor_osk_sync(void)
{
        int keyboard = compositor_osk_row(), focus = compositor.focus, want = 0;
        b32 touch = !compositor.osk_typed && compositor.owner != COMPOSITOR_PREDICT_MOUSE;

        // The hold of a turn ends when the focus goes elsewhere, or when a field of the focused window is focused again after one lost it.
        if (compositor.osk_turned)
        {
                if (focus != compositor.osk_turn_focus)
                        compositor.osk_turned = 0;
                else if (focus >= 0 && !compositor.table->field[focus])
                        compositor.osk_turn_blur = 1;
                else if (focus >= 0 && compositor.osk_turn_blur)
                        compositor.osk_turned = 0;
        }
        if (keyboard >= 0 && focus >= 0 && focus != keyboard && compositor.table->field[focus] && compositor.osk_setting != COMPOSITOR_OSK_OFF &&
            !compositor.osk_turned)
                want = compositor.osk_setting == COMPOSITOR_OSK_ON || touch || !compositor_keyboard_plugged();
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if ((compositor.osk_pan[row] || compositor.osk_height[row]) && (!want || (int)row != focus))
                        compositor_osk_unpan((int)row);
        if (keyboard >= 0)
        {
                compositor_osk_put_away(keyboard, !want);
                if (want)
                        compositor_osk_pan(focus, keyboard);
        }
        compositor.osk_shown = (unsigned char)(keyboard >= 0 && want);
        compositor_osk_publish();
}

// A screen turned (compositor_space_retake saw a screen's turn change): the keyboard is put away and held away until the hold ends
// (compositor_osk_sync). Its own size and place follow the output it is on (osk.c, osk_tick), so nothing else is asked here.
static void compositor_osk_turned(void)
{
        compositor.osk_turned = 1;
        compositor.osk_turn_blur = 0;
        compositor.osk_turn_focus = compositor.focus;
        compositor_osk_sync();
}

// The Settings choice, keyboard onscreen: auto unless the file says off or on.
static void compositor_osk_setting_read(void)
{
        char word[16];

        compositor_word(SETTING_ONSCREEN_PATH, word, sizeof(word));
        compositor.osk_setting = string_equals((string_address)word, (string_address)"off") ? COMPOSITOR_OSK_OFF
                                 : string_equals((string_address)word, (string_address)"on") ? COMPOSITOR_OSK_ON : COMPOSITOR_OSK_AUTO;
        compositor_osk_sync();
}

/*
        A window asked to go: by its close button, Alt-F4, or a middle press on its tab. What happens next is the program's, and
        it may be to stay (unsaved changes, and Cancel): so each ask of a window is an ask of its own, counted in close_asks, and
        every program hears each ask once, whatever tells it: a kit program reads the count in its row of the table
        (windows_close_asked), the X window manager and the Wayland shim keep the count each window was told of
        (compositor_xwm_close_owed, wayland_row_wake) and send a message when it is behind. WINDOW_CLOSING alone said it, a bit
        that is set and stays, so a message went once and a window that had stayed could not be closed by its button again, and a
        kit program read the bit on every pass, so one that had stayed left the moment its reason to stay was gone. A popup is
        asked once: it is dismissed, not asked, and every press outside it while it is on its way out comes here.
*/
static void compositor_close(int row)
{
        if (row < 0 || !compositor.showing[row])
                return;
        if (compositor_popup_parent[row] >= 0 && (compositor.state[row] & WINDOW_CLOSING))
                return;

        compositor_close_count(row, compositor.close_asks[row] == 255 ? 1u : compositor.close_asks[row] + 1u);
        compositor.state[row] |= WINDOW_CLOSING;
        compositor.table->state[row] = compositor.state[row];
        rows_add(&compositor.woken, row);
}

// A door knocked on or closed: the row read, and a closed one let go of and opened again for the next program.
/* ---- the clipboard ---- */

/*
        The two selections are the compositor's, held as the memfds programs made them in: a copy is a sealed memfd a
        window names in its row, opened here through /proc/PID/fd (the compositor is root, as every window is), checked
        -- sealed against growing, shrinking and writing, a copy that holds together whatever its types and size, its text
        UTF-8 with no NUL (windows_clip_valid) -- and kept at the
        descriptor WINDOWS_CLIP_FD + which, a number nothing else is given. A paste is that descriptor opened again by
        the window the compositor answers READY to, which is only the window with the keys (or a popup of it), and only
        within COMPOSITOR_CLIP_GESTURE_NS of a key or a button of the person's on it: a window in the background, or
        one that asks with nobody at the keys, is answered REFUSED and learns nothing. A copy is a gesture too: a
        window cannot fill the clipboard on its own. The command line (`moonwater clipboard`, root) offers and clears
        through the header and knocks on WINDOWS_CLIP_DOOR. Nothing is written to a disk: the history, when it is kept
        (`moonwater canvas clipboard history N`), is memfds in this process, and off empties it all.
*/
#define COMPOSITOR_CLIP_GESTURE_NS 1500000000ull
#define COMPOSITOR_CLIP_HISTORY 32
#define COMPOSITOR_F_DUPFD_CLOEXEC 1030
#define COMPOSITOR_CLIP_SEALS 14 // SHRINK, GROW and WRITE: the bytes and the size are what they were when it was taken

static struct
{
        int door;
        int fd[3]; // the two selections, and the drag's copy (WINDOWS_CLIP_DRAG: wayland_host.c's compositor_drag)
        unsigned int *offer_seen, *ask_seen, cli_seen;
        positive *gesture;
        unsigned int history_most, history_count;
        int history[COMPOSITOR_CLIP_HISTORY];
        int blank; // /dev/null, which holds a selection's number while there is no selection (compositor_clip_reserve)
        const unsigned char *map[3]; // each selection's copy mapped whole and read-only while it is held: its types are read there
} compositor_clip = {.door = -1, .fd = {-1, -1, -1}, .blank = -1};

// A selection's mapping let go, and the new one's made (0 for none: its types are then none).
static void compositor_clip_mapped(unsigned int which, unsigned long size)
{
        long mapped;

        if (compositor_clip.map[which])
                compositor_call(syscall(munmap), (positive)compositor_clip.map[which], (positive)compositor.table->clip_length[which], 0, 0, 0, 0);
        compositor_clip.map[which] = 0;
        if (!size || compositor_clip.fd[which] < 0)
                return;
        mapped = compositor_call(syscall(mmap), 0, (positive)size, 1, 1, (positive)compositor_clip.fd[which], 0);
        compositor_clip.map[which] = mapped < 0 && mapped > -4096 ? 0 : (const unsigned char *)mapped;
}

/*
        The selections' two numbers, WINDOWS_CLIP_FD and the next, are held from the start and for the session, by /dev/null
        while a selection is empty. A copy is put at its number with dup3, which closes whatever is there, and a paste opens
        that number of the compositor's through /proc: so the numbers have to be ones the kernel gives to nothing else, and the
        kernel gives the lowest free number, which is 900 once 900 descriptors are open. The compositor's own are few, but it
        holds its Wayland clients' too (each may leave 28 it has not used, and its dma-bufs), and with enough of them a client's
        socket or buffer was at 900: the next copy closed it under the shim, which went on reading the clipboard's memfd as that
        client, and closed the clipboard when it gave the client up. A selection let go of gives its number back to /dev/null,
        not to the table.
*/
static void compositor_clip_reserve(void)
{
        if (compositor_clip.blank < 0)
                compositor_clip.blank = (int)system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ | O_CLOEXEC);
        for (unsigned int which = 0; which < 3 && compositor_clip.blank >= 0; which++)
                if (compositor_clip.fd[which] < 0)
                        compositor_call(syscall(dup3), (positive)compositor_clip.blank, WINDOWS_CLIP_FD + which, O_CLOEXEC, 0, 0, 0);
}

/*
        The window a row belongs to: itself, or the window at the root of its popups, as the compositor took them with their
        first pictures (compositor_popup_parent). The table's popup_parent is the program's to write at any time: a row that
        wrote the focused window's there after it showed was that window's to paste for, though it is no popup of it.
*/
static int compositor_popup_root(int row);

static int compositor_clip_root(int row)
{
        return row >= 0 && row < COMPOSITOR_ROWS ? compositor_popup_root(row) : -1;
}

// A key or a button the person pressed on a row: the row and the window it belongs to may paste for a while.
static void compositor_clip_gesture(int row)
{
        positive now = clock_monotonic_nanoseconds();
        int root = compositor_clip_root(row);

        if (row >= 0)
                compositor_clip.gesture[row] = now;
        if (root >= 0)
                compositor_clip.gesture[root] = now;
}

static void compositor_clip_tell(int row, unsigned int which, int what)
{
        if (row >= 0 && row < COMPOSITOR_ROWS && compositor.table->pid[row])
                compositor_post(row, WINDOW_EVENT_CLIP, 0, (int)which, what);
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_selection(unsigned int which);
#endif

// One selection let go, and its watchers told.
static void compositor_clip_clear(unsigned int which)
{
        // The number stays the compositor's (compositor_clip_reserve): /dev/null takes the memfd's place in the same call.
        if (compositor_clip.fd[which] >= 0 &&
            (compositor_clip.blank < 0 ||
             compositor_call(syscall(dup3), (positive)compositor_clip.blank, (positive)compositor_clip.fd[which], O_CLOEXEC, 0, 0, 0) < 0))
                system_close(compositor_clip.fd[which]);
        compositor_clip_mapped(which, 0);
        compositor_clip.fd[which] = -1;
        compositor.table->clip_length[which] = 0;
        compositor.table->clip_owner[which] = -1;
        __atomic_add_fetch(&compositor.table->clip_serial[which], 1, __ATOMIC_RELEASE);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.table->clip_watch[row])
                        compositor_clip_tell(row, which, WINDOWS_CLIP_CHANGED);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_selection(which);
#endif
}

// Every selection and the history let go: the service turned off, or `moonwater clipboard clear`.
static void compositor_clip_wipe(void)
{
        for (unsigned int which = 0; which < 2; which++)
                compositor_clip_clear(which);
        for (unsigned int at = 0; at < compositor_clip.history_count; at++)
                system_close(compositor_clip.history[at]);
        compositor_clip.history_count = 0;
}

// The settings: on or off, and how many copies the history keeps (none by default).
#ifndef SHELL_NO_GPU
// GPU rendering as the setting has it (alpha: off unless the file says on): what the compose path asks before it uses src/canvas/gpu.c.
static int compositor_gpu = 0;

static void compositor_gpu_settings(void)
{
        char word[16];
#ifdef MOONWATER_GPU_C
        int was = compositor_gpu;
#endif

        compositor_word(SETTING_GPU_PATH, word, sizeof(word));
        // No file in /root, or the word default (what `canvas gpu default` writes), is the registry's standard, on: the GPU is the default on every
        // machine; the word on is on too, and any other word (off) is off.
        compositor_gpu = !word[0] || string_equals((string_address)word, (string_address) "on") || string_equals((string_address)word, (string_address) "default");
#ifdef MOONWATER_GPU_C
        // Turned on again: a GPU given up on after three hangs is tried again (one that never started, a card that is not one the kernels are for or that
        // drew its self-test wrong, stays given up).
        if (compositor_gpu && !was)
        {
                // Turned on from off: the hangs and their cool-downs are forgiven. A card that never started is looked for again (its prerequisites checked once
                // more: a driver or a node that came late); a card that drew its self-test wrong stays given up.
                // Given up for the session after two hangs (compositor_gpu_job_failed) stays so: the turn does not bring it back.
                if (!compositor_gpu_state.hung_off && (compositor_gpu_state.up || compositor_gpu_state.dead_why != COMPOSITOR_GPU_WHY_UNTRUSTED))
                        compositor_gpu_state.dead = 0;
                // The turn is the owner asking: the card is started once whatever the nodes were (settled), and its outcome is said again even if it is the last one.
                compositor_gpu_state.settled = 0, compositor_gpu_state.said = 0;
                compositor_gpu_state.failures = 0, compositor_gpu_state.cool_until = 0, compositor_gpu_state.cool_steps = 0;
        }
#endif
}
#endif

static void compositor_clip_settings(void)
{
        char word[16];
        long value;

        if (!compositor.table)
                return;
        compositor_word(SETTING_CLIPBOARD_PATH, word, sizeof(word));
        compositor.table->clip_on = !string_equals((string_address)word, "off");
        compositor_word(SETTING_CLIPBOARD_HISTORY_PATH, word, sizeof(word));
        compositor_clip.history_most = compositor_setting_kept(SETTING_CLIPBOARD_HISTORY_PATH, word, &value) && value >= 0 &&
                                               value <= COMPOSITOR_CLIP_HISTORY
                                           ? (unsigned int)value
                                           : 0;
        if (!compositor.table->clip_on)
                compositor_clip_wipe();
        while (compositor_clip.history_count > compositor_clip.history_most)
                system_close(compositor_clip.history[--compositor_clip.history_count]);
}

/*
        A memfd another process holds, opened and checked: its descriptor here and its size, or -1. Sealed against
        every change of its bytes and size, no larger than WINDOWS_CLIP_MOST (it is read through here, on the loop: a
        copy of gigabytes held every frame and key for the read), and a copy that holds together (windows_clip_valid).
*/
static int compositor_clip_open(unsigned int pid, int fd, unsigned long address_to length)
{
        char path[48];
        unsigned char info[256];
        long held, opened, size, seals, mapped;
        unsigned int mode;
        int text;

        if (!pid || fd < 0)
                return -1;
        /*
                The descriptor is whichever the row names, so it is anything its program holds: the read end of a pipe or a
                FIFO, whose open for reading waits for a writer for ever and the desktop with it; a terminal, which a session
                leader's open takes for its own; a device, whose open is its driver's to act on. Its file is taken first by
                O_PATH, which opens nothing, and only a regular file (a memfd is one) is opened, through that hold, so the
                file opened is the one looked at whatever the program does to its descriptor in between.
        */
        windows_proc_fd(path, pid, (unsigned int)fd);
        held = (long)system_open_at(AT_FDCWD, (string_address)path, O_PATH | O_CLOEXEC);
        if (held < 0)
                return -1;
        // struct statx: stx_mode at 28 (u16); STATX_TYPE of the descriptor itself (AT_EMPTY_PATH).
        memory_zero(info, sizeof(info));
        mode = compositor_call(syscall(statx), (positive)held, (positive) "", 0x1000, 1, (positive)info, 0) < 0
                   ? 0
                   : (unsigned int)info[28] | (unsigned int)info[29] << 8;
        if ((mode & 0170000) == 0100000)
        {
                windows_proc_fd(path, (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (unsigned int)held);
                opened = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        }
        else
                opened = -1;
        system_close((bipolar)held);
        if (opened < 0)
                return -1;
        seals = compositor_call(syscall(fcntl), (positive)opened, WINDOWS_F_GET_SEALS, 0, 0, 0, 0);
        size = compositor_call(syscall(lseek), (positive)opened, 0, 2, 0, 0, 0);
        if (seals < 0 || (seals & COMPOSITOR_CLIP_SEALS) != COMPOSITOR_CLIP_SEALS || size <= 0 || (unsigned long)size > WINDOWS_CLIP_MOST)
        {
                system_close((bipolar)opened);
                return -1;
        }
        mapped = compositor_call(syscall(mmap), 0, (positive)size, 1, 2, (positive)opened, 0);
        text = !(mapped < 0 && mapped > -4096) && windows_clip_valid((const unsigned char *)mapped, (unsigned long)size);
        if (!(mapped < 0 && mapped > -4096))
                compositor_call(syscall(munmap), (positive)mapped, (positive)size, 0, 0, 0, 0);
        if (!text)
        {
                system_close((bipolar)opened);
                return -1;
        }
        *length = (unsigned long)size;
        return (int)opened;
}

// A checked memfd made selection which, owned by owner (a row, or -2 for the command line); watchers are told.
static void compositor_clip_keep(unsigned int which, int opened, unsigned long length, int owner)
{
        // The copy before it goes to the history, when one is kept: a descriptor of its own, newest first.
        if (which == WINDOWS_CLIP_CLIPBOARD && compositor_clip.fd[which] >= 0 && compositor_clip.history_most)
        {
                long kept = compositor_call(syscall(fcntl), (positive)compositor_clip.fd[which], COMPOSITOR_F_DUPFD_CLOEXEC,
                                            WINDOWS_CLIP_FD + 8, 0, 0, 0);

                if (kept >= 0)
                {
                        if (compositor_clip.history_count == compositor_clip.history_most)
                                system_close(compositor_clip.history[--compositor_clip.history_count]);
                        memory_copy(compositor_clip.history + 1, compositor_clip.history,
                                    compositor_clip.history_count * sizeof(compositor_clip.history[0]));
                        compositor_clip.history[0] = (int)kept;
                        compositor_clip.history_count++;
                }
        }
        // dup3 puts it at the selection's number, letting go of what was there in the same call.
        compositor_call(syscall(dup3), (positive)opened, WINDOWS_CLIP_FD + which, O_CLOEXEC, 0, 0, 0);
        system_close(opened);
        compositor_clip_mapped(which, 0);
        compositor_clip.fd[which] = WINDOWS_CLIP_FD + (int)which;
        compositor_clip_mapped(which, length);
        compositor.table->clip_length[which] = length;
        compositor.table->clip_owner[which] = owner;
        __atomic_add_fetch(&compositor.table->clip_serial[which], 1, __ATOMIC_RELEASE);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.table->clip_watch[row] && row != owner)
                        compositor_clip_tell(row, which, WINDOWS_CLIP_CHANGED);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_selection(which);
#endif
}

/*
        The on-screen keyboard's key requests (windows_keyboard_send), read from the row's ring in order and posted to the window with
        the keys, as a key of the keyboard's own would be (its code and character, its modifiers and its press). They are honoured only
        while the person's press is on the keyboard row: held now, or pressed within COMPOSITOR_CLIP_GESTURE_NS. So a program with no
        press of the person's on it can put no key into the window with the keys, and a row that is not a keyboard has no ring read.
        Anything else in the flags (the pointer's, the touch's) is dropped; the requests are read even when nothing is posted, so a
        ring that was refused is not left full.
*/
static void compositor_osk_read(int row)
{
        struct windows address_to table = compositor.table;
        unsigned int head;
        positive now;

        if (row < 0 || row >= COMPOSITOR_ROWS || !(compositor.asks[row] & WINDOW_KEYBOARD))
                return;
        head = __atomic_load_n(&table->osk_head[row], __ATOMIC_ACQUIRE);
        now = clock_monotonic_nanoseconds();
        while (table->osk_tail[row] != head)
        {
                unsigned int at = table->osk_tail[row] % WINDOWS_OSK;
                int target = compositor.focus >= 0 ? compositor_popup_top(compositor.focus) : -1;
                b32 pressed = compositor_osk_held[row] || now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS;

                if (pressed && target >= 0 && target != row)
                        compositor_post(target, WINDOW_EVENT_KEY,
                                        table->osk_flags[row][at] & (WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT |
                                                                     WINDOW_KEY_ALTGR | WINDOW_KEY_SUPER | WINDOW_KEY_REPEAT),
                                        table->osk_code[row][at], table->osk_character[row][at]);
                __atomic_store_n(&table->osk_tail[row], table->osk_tail[row] + 1, __ATOMIC_RELEASE);
        }
}

/*
        Protocol-neutral Canvas drag-and-drop engine: drives native Canvas drags directly
        (working even without Wayland) and lets external protocols (Wayland/X11) register
        thin hooks via compositor_dnd_register() for cross-protocol drags.
*/
#define COMPOSITOR_DND_ENTER   1
#define COMPOSITOR_DND_MOTION  2
#define COMPOSITOR_DND_LEAVE   3
#define COMPOSITOR_DND_DROP    4
#define COMPOSITOR_DND_END     5
#define COMPOSITOR_DND_BRIDGES 4

struct compositor_drag_ops
{
        void *ctx;
        int (*active)(void *ctx);
        int (*begin)(void *ctx, int source_row, const char *types, long types_bytes, unsigned int actions, int under, int wx, int wy);
        int (*target_owns)(void *ctx, int row);
        long (*target_event)(void *ctx, int what, int row, int x, int y);
        void (*status)(void *ctx, int accept, unsigned int action);
        int (*read_drop)(void *ctx, const char *mime, int target_row);
        void (*finished)(void *ctx, int ok);
        void (*cancel)(void *ctx);
        unsigned int (*ext_types)(void *ctx, const char **types_out, long *bytes_out);
        int (*ext_action)(void *ctx);
};

static struct
{
        int active; // 1 = native Canvas source, 2 = external protocol source
        int source, target;
        long take;
        unsigned int source_actions, take_actions, take_preferred, action, told_action;
        int dropped, named, place;
        const struct compositor_drag_ops *ext_bridge;    // active external source bridge
        const struct compositor_drag_ops *target_bridge; // bridge owning current target row
        const struct compositor_drag_ops *bridges[COMPOSITOR_DND_BRIDGES];
        unsigned int bridge_count;
        unsigned int *offer_seen, *take_seen, *done_seen;
} compositor_dnd = {.source = -1, .target = -1, .take = -1};

static int compositor_dnd_register(const struct compositor_drag_ops *ops)
{
        if (!ops || compositor_dnd.bridge_count >= COMPOSITOR_DND_BRIDGES)
                return -1;
        for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                if (compositor_dnd.bridges[i] == ops)
                        return 0;
        compositor_dnd.bridges[compositor_dnd.bridge_count++] = ops;
        return 0;
}

static void compositor_dnd_unregister(const struct compositor_drag_ops *ops)
{
        for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                if (compositor_dnd.bridges[i] == ops)
                {
                        compositor_dnd.bridges[i] = compositor_dnd.bridges[--compositor_dnd.bridge_count];
                        return;
                }
}

static void compositor_dnd_keep(int opened, unsigned long length)
{
        compositor_clip_mapped(WINDOWS_CLIP_DRAG, 0);
        compositor_call(syscall(dup3), (positive)opened, WINDOWS_CLIP_FD + WINDOWS_CLIP_DRAG, O_CLOEXEC, 0, 0, 0);
        system_close(opened);
        compositor_clip.fd[WINDOWS_CLIP_DRAG] = WINDOWS_CLIP_FD + (int)WINDOWS_CLIP_DRAG;
        compositor_clip_mapped(WINDOWS_CLIP_DRAG, length);
        compositor.table->clip_length[WINDOWS_CLIP_DRAG] = length;
        __atomic_add_fetch(&compositor.table->clip_serial[WINDOWS_CLIP_DRAG], 1, __ATOMIC_RELEASE);
}

static void compositor_dnd_drop_copy(void)
{
        if (compositor_clip.fd[WINDOWS_CLIP_DRAG] < 0)
                return;
        compositor_clip_mapped(WINDOWS_CLIP_DRAG, 0);
        if (compositor_clip.blank < 0 ||
            compositor_call(syscall(dup3), (positive)compositor_clip.blank, WINDOWS_CLIP_FD + WINDOWS_CLIP_DRAG, O_CLOEXEC, 0, 0, 0) < 0)
                system_close(compositor_clip.fd[WINDOWS_CLIP_DRAG]);
        compositor_clip.fd[WINDOWS_CLIP_DRAG] = -1;
        compositor.table->clip_length[WINDOWS_CLIP_DRAG] = 0;
        __atomic_add_fetch(&compositor.table->clip_serial[WINDOWS_CLIP_DRAG], 1, __ATOMIC_RELEASE);
}

static void compositor_dnd_tell(int row, int what, int word)
{
        unsigned int keys = what == WINDOWS_DRAG_ENTER || what == WINDOWS_DRAG_MOTION || what == WINDOWS_DRAG_DROP
                                ? compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)
                                : 0;

        if (row >= 0 && row < COMPOSITOR_ROWS && compositor.table && compositor.table->pid[row])
                compositor_post(row, WINDOW_EVENT_DRAG, keys, what, word);
}

static int compositor_dnd_place(int x, int y)
{
        return (int)(((unsigned int)(y & 0xffff) << 16) | (unsigned int)(x & 0xffff));
}

static const char *compositor_dnd_name(long nth)
{
        unsigned long length;

        if (nth < 0 || !compositor_clip.map[WINDOWS_CLIP_DRAG] || !compositor.table)
                return 0;
        return windows_clip_offered(compositor_clip.map[WINDOWS_CLIP_DRAG], compositor.table->clip_length[WINDOWS_CLIP_DRAG],
                                    (unsigned long)nth, 0, &length);
}

static int compositor_dnd_held(int row)
{
        for (int button = 0; button < 8; button++)
                if ((compositor.button_down & (1u << button)) && compositor.button_row[button] == row)
                        return 1;
        return 0;
}

static int compositor_dnd_active(void)
{
        if (compositor_dnd.active)
                return 1;
        for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                if (compositor_dnd.bridges[i]->active && compositor_dnd.bridges[i]->active(compositor_dnd.bridges[i]->ctx))
                        return 1;
        return 0;
}

static int compositor_dnd_action(void)
{
        unsigned int action;

        if (!compositor_dnd.active)
        {
                for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                        if (compositor_dnd.bridges[i]->ext_action)
                        {
                                int a = compositor_dnd.bridges[i]->ext_action(compositor_dnd.bridges[i]->ctx);

                                if (a >= 0)
                                        return a;
                        }
                return -1;
        }
        action = compositor_dnd.action;
        if (compositor_dnd.source >= 0 && action != compositor_dnd.told_action)
        {
                compositor_dnd.told_action = action;
                compositor_dnd_tell(compositor_dnd.source, WINDOWS_DRAG_ACTION, (int)action);
        }
        return (int)action;
}

static int compositor_dnd_shape(void)
{
        int action = compositor_dnd_action();

        return action < 0 ? -1 : action ? CP_HAND : CP_NOTALLOWED;
}

static const struct compositor_drag_ops *compositor_dnd_bridge_for(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                if (compositor_dnd.bridges[i]->target_owns && compositor_dnd.bridges[i]->target_owns(compositor_dnd.bridges[i]->ctx, row))
                        return compositor_dnd.bridges[i];
        return 0;
}

static void compositor_dnd_name_ext_types(void)
{
        const struct compositor_drag_ops *b = compositor_dnd.ext_bridge;
        const char *types = 0;
        long bytes = 0, count = 0, built, opened;
        struct windows_clip_part *parts;
        unsigned long length = 0;

        if (compositor_dnd.named || compositor_dnd.source >= 0 || !b || !b->ext_types)
                return;
        compositor_dnd.source_actions = b->ext_types(b->ctx, &types, &bytes) & 7u;
        if (bytes <= 0 || !types)
                return;
        for (long at = 0; at < bytes; at++)
                count += !types[at];
        parts = count ? (struct windows_clip_part *)compositor_map_zeroed((unsigned long)count * sizeof(*parts)) : 0;
        if (!parts)
                return;
        count = 0;
        for (long at = 0; at < bytes; at += (long)string_length((string_address)(types + at)) + 1)
                if (types[at])
                        parts[count].name = types + at, parts[count].bytes = "", parts[count++].length = 0;
        built = windows_clip_build(parts, (unsigned long)count);
        compositor_call(syscall(munmap), (positive)parts, (positive)((unsigned long)count * sizeof(*parts)), 0, 0, 0, 0);
        opened = built >= 0 ? compositor_clip_open(compositor.table->clip_pid, (int)built, &length) : -1;
        if (built >= 0)
                system_close((int)built);
        if (opened >= 0)
        {
                compositor_dnd_keep((int)opened, length);
                compositor_dnd.named = 1;
        }
}

static void compositor_dnd_end(int ok)
{
        int target = compositor_dnd.target, source = compositor_dnd.source;
        unsigned int action = compositor_dnd.action;

        if (!compositor_dnd.active)
                return;
        if (compositor_dnd.target_bridge && compositor_dnd.target_bridge->target_event)
                compositor_dnd.target_bridge->target_event(compositor_dnd.target_bridge->ctx, COMPOSITOR_DND_END, target, ok, (int)action);
        else if (target >= 0)
                compositor_dnd_tell(target, WINDOWS_DRAG_DONE, ok);
        if (source >= 0)
        {
                compositor_dnd_tell(source, WINDOWS_DRAG_ACTION, (int)action);
                compositor_dnd_tell(source, WINDOWS_DRAG_ENDED, ok);
        }
        if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->finished && !ok)
                compositor_dnd.ext_bridge->finished(compositor_dnd.ext_bridge->ctx, 0);
        compositor_dnd.active = 0;
        compositor_dnd.source = compositor_dnd.target = -1;
        compositor_dnd.take = -1;
        compositor_dnd.action = compositor_dnd.told_action = compositor_dnd.source_actions = 0;
        compositor_dnd.dropped = compositor_dnd.named = 0;
        compositor_dnd.ext_bridge = compositor_dnd.target_bridge = 0;
        if (source >= 0)
                for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                        if (compositor_dnd.bridges[i]->cancel && compositor_dnd.bridges[i]->active &&
                            compositor_dnd.bridges[i]->active(compositor_dnd.bridges[i]->ctx))
                                compositor_dnd.bridges[i]->cancel(compositor_dnd.bridges[i]->ctx);
        compositor_dnd_drop_copy();
        compositor_chip_drag(-1);
}

static void compositor_dnd_motion(int under)
{
        struct windows *table = compositor.table;
        const struct compositor_drag_ops *nb;
        int wx = 0, wy = 0;

        if (!compositor_dnd.active)
        {
                compositor_chip_drag(compositor_dnd_action());
                return;
        }
        if (compositor_dnd.dropped)
                return;
        if (under >= 0 && under < COMPOSITOR_ROWS)
                compositor_window_point(under, compositor.pointer_x, compositor.pointer_y, &wx, &wy);
        nb = compositor_dnd_bridge_for(under);
        if (nb)
        {
                if (compositor_dnd.active == 2 && nb == compositor_dnd.ext_bridge)
                {
                        if (compositor_dnd.target >= 0 && !compositor_dnd.target_bridge)
                                compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_LEAVE, 0);
                        compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
                        compositor_chip_drag(compositor_dnd_action());
                        return;
                }
                if (compositor_dnd.target != under || compositor_dnd.target_bridge != nb)
                {
                        if (compositor_dnd.target_bridge && compositor_dnd.target_bridge->target_event)
                                compositor_dnd.target_bridge->target_event(compositor_dnd.target_bridge->ctx, COMPOSITOR_DND_LEAVE, compositor_dnd.target, 0, 0);
                        else if (compositor_dnd.target >= 0)
                                compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_LEAVE, 0);
                        compositor_dnd.target = under, compositor_dnd.target_bridge = nb, compositor_dnd.take = -1, compositor_dnd.action = 0;
                        if (nb->target_event)
                                compositor_dnd.action = (unsigned int)nb->target_event(nb->ctx, COMPOSITOR_DND_ENTER, under, wx, wy);
                }
                else if (nb->target_event)
                        compositor_dnd.action = (unsigned int)nb->target_event(nb->ctx, COMPOSITOR_DND_MOTION, under, wx, wy);
                compositor_chip_drag(compositor_dnd_action());
                return;
        }
        if (compositor_dnd.target_bridge)
        {
                if (compositor_dnd.target_bridge->target_event)
                        compositor_dnd.target_bridge->target_event(compositor_dnd.target_bridge->ctx, COMPOSITOR_DND_LEAVE, compositor_dnd.target, 0, 0);
                compositor_dnd.target_bridge = 0, compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
        }
        if (under < 0 || under >= COMPOSITOR_ROWS || !table || !table->drop_ok[under] || !table->pid[under])
        {
                if (compositor_dnd.target >= 0)
                        compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_LEAVE, 0);
                compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
                if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->status)
                        compositor_dnd.ext_bridge->status(compositor_dnd.ext_bridge->ctx, 0, 0);
                compositor_chip_drag(compositor_dnd_action());
                return;
        }
        compositor_dnd_name_ext_types();
        compositor_dnd.place = compositor_dnd_place(wx, wy);
        if (under != compositor_dnd.target)
        {
                if (compositor_dnd.target >= 0)
                        compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_LEAVE, 0);
                compositor_dnd.target = under, compositor_dnd.take = -1, compositor_dnd.action = 0;
                compositor_dnd_tell(under, WINDOWS_DRAG_ENTER, compositor_dnd.place);
        }
        else
                compositor_dnd_tell(under, WINDOWS_DRAG_MOTION, compositor_dnd.place);
        compositor_chip_drag(compositor_dnd_action());
}

static void compositor_dnd_offer(int row)
{
        struct windows *table = compositor.table;
        unsigned long length = 0;
        int opened = -1, tab = 0, under;
        unsigned int actions;

        if (!table || compositor_dnd_active() || !compositor_dnd_held(row) ||
            (opened = compositor_clip_open(table->pid[row], table->drag_offer_fd[row], &length)) < 0)
        {
                compositor_dnd_tell(row, WINDOWS_DRAG_DECLINED, 0);
                return;
        }
        actions = table->drag_offer_actions[row] & 7u;
        compositor_dnd_keep(opened, length);
        if (!actions || !compositor_dnd_name(0))
        {
                compositor_dnd_drop_copy();
                compositor_dnd_tell(row, WINDOWS_DRAG_DECLINED, 0);
                return;
        }
        compositor_dnd.active = 1;
        compositor_dnd.source = row, compositor_dnd.target = -1, compositor_dnd.take = -1;
        compositor_dnd.source_actions = actions, compositor_dnd.action = 0, compositor_dnd.told_action = 0;
        compositor_dnd.dropped = 0, compositor_dnd.named = 0;
        compositor_dnd.ext_bridge = 0, compositor_dnd.target_bridge = 0;
        compositor_chip_drag_items = table->drag_offer_actions[row] >> WINDOWS_DRAG_ITEMS_SHIFT;
        compositor_chip_drag_kind = (table->drag_offer_actions[row] >> WINDOWS_DRAG_KIND_SHIFT) & 0xffu;
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
        if (compositor_dnd.bridge_count)
        {
                char types_buf[4096];
                long bytes = 0;
                const char *name;
                int wx = 0, wy = 0;

                for (long nth = 0; (name = compositor_dnd_name(nth)); nth++)
                {
                        long n = (long)string_length((string_address)name) + 1;

                        if (bytes + n > (long)sizeof(types_buf))
                                break;
                        memory_copy(types_buf + bytes, name, (positive)n);
                        bytes += n;
                }
                if (under >= 0)
                        compositor_window_point(under, compositor.pointer_x, compositor.pointer_y, &wx, &wy);
                for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                        if (compositor_dnd.bridges[i]->begin)
                                compositor_dnd.bridges[i]->begin(compositor_dnd.bridges[i]->ctx, row, types_buf, bytes, actions, under, wx, wy);
        }
        compositor_dnd_motion(under);
}

static void compositor_dnd_take(int row)
{
        struct windows *table = compositor.table;
        unsigned int allowed, both, action;

        if (!compositor_dnd.active || row != compositor_dnd.target || compositor_dnd.dropped)
                return;
        compositor_dnd.take = table->drop_take[row] >= 0 && compositor_dnd_name(table->drop_take[row]) ? table->drop_take[row] : -1;
        compositor_dnd.take_actions = table->drop_take_actions[row] & 7u;
        compositor_dnd.take_preferred = table->drop_take_preferred[row] & 7u;
        allowed = compositor_dnd.source_actions ? compositor_dnd.source_actions : 7u;
        both = compositor_dnd.take_actions & allowed;
        action = (both & compositor_dnd.take_preferred) ? compositor_dnd.take_preferred
                 : (both & 1u)                          ? 1u
                 : (both & 2u)                          ? 2u
                 : (both & 4u)                          ? 4u
                                                        : 0;
        compositor_dnd.action = compositor_dnd.take >= 0 ? action : 0;
        if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->status)
                compositor_dnd.ext_bridge->status(compositor_dnd.ext_bridge->ctx, compositor_dnd.action != 0, compositor_dnd.action);
        compositor_chip_drag(compositor_dnd_action());
}

static void compositor_dnd_ready(void)
{
        if (!compositor_dnd.active || compositor_dnd.target < 0 || !compositor.table)
                return;
        compositor.table->clip_given_length[compositor_dnd.target] = compositor.table->clip_length[WINDOWS_CLIP_DRAG];
        compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_READY, 0);
}

static void compositor_dnd_release(int to)
{
        if (compositor_dnd.active != 1 || to != compositor_dnd.source || compositor_dnd.dropped)
                return;
        if (compositor_dnd.target_bridge)
        {
                if (compositor_dnd.target_bridge->target_event &&
                    compositor_dnd.target_bridge->target_event(compositor_dnd.target_bridge->ctx, COMPOSITOR_DND_DROP, compositor_dnd.target, 0, 0))
                {
                        compositor_dnd.dropped = 1;
                        return;
                }
                compositor_dnd_end(0);
                return;
        }
        if (compositor_dnd.target < 0 || compositor_dnd.take < 0 || !compositor_dnd.action)
        {
                compositor_dnd_end(0);
                return;
        }
        compositor_dnd.dropped = 1;
        compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_DROP, (int)compositor_dnd.action);
        compositor_dnd_ready();
}

static void compositor_dnd_door(int row)
{
        struct windows *table = compositor.table;
        unsigned int offer, take, done;

        if (!table || !compositor_dnd.offer_seen)
                return;
        offer = __atomic_load_n(&table->drag_offer_serial[row], __ATOMIC_ACQUIRE);
        take = __atomic_load_n(&table->drop_take_serial[row], __ATOMIC_ACQUIRE);
        done = __atomic_load_n(&table->drop_done_serial[row], __ATOMIC_ACQUIRE);
        if (offer != compositor_dnd.offer_seen[row])
        {
                compositor_dnd.offer_seen[row] = offer;
                compositor_dnd_offer(row);
        }
        if (take != compositor_dnd.take_seen[row])
        {
                compositor_dnd.take_seen[row] = take;
                compositor_dnd_take(row);
        }
        if (done != compositor_dnd.done_seen[row])
        {
                compositor_dnd.done_seen[row] = done;
                if (compositor_dnd.active && row == compositor_dnd.target && compositor_dnd.dropped)
                {
                        int ok = table->drop_done[row] == 1;

                        if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->finished)
                                compositor_dnd.ext_bridge->finished(compositor_dnd.ext_bridge->ctx, ok);
                        compositor_dnd_end(ok);
                }
        }
}

static int compositor_dnd_escape(int value)
{
        if (!compositor_dnd_active())
                return 0;
        if (value == 1)
        {
                if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->cancel)
                        compositor_dnd.ext_bridge->cancel(compositor_dnd.ext_bridge->ctx);
                for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                        if (compositor_dnd.bridges[i] != compositor_dnd.ext_bridge && compositor_dnd.bridges[i]->cancel &&
                            compositor_dnd.bridges[i]->active && compositor_dnd.bridges[i]->active(compositor_dnd.bridges[i]->ctx))
                                compositor_dnd.bridges[i]->cancel(compositor_dnd.bridges[i]->ctx);
                compositor_dnd_end(0);
        }
        return 1;
}

static void compositor_dnd_keys(void)
{
        if (compositor_dnd.active && compositor_dnd.target >= 0 && !compositor_dnd.target_bridge && !compositor_dnd.dropped)
                compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_MOTION, compositor_dnd.place);
}

static void compositor_dnd_gone(int row)
{
        if (!compositor_dnd.active)
                return;
        if (row == compositor_dnd.source)
                compositor_dnd_end(0);
        else if (row == compositor_dnd.target && !compositor_dnd.dropped)
                compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
}

// Protocol bridge entrypoints for external (Wayland/X11) drag sources over native Canvas targets.
static int compositor_dnd_ext_begin(const struct compositor_drag_ops *bridge, unsigned int actions)
{
        if (!bridge || compositor_dnd.active)
                return 0;
        compositor_dnd.active = 2;
        compositor_dnd.source = compositor_dnd.target = -1;
        compositor_dnd.take = -1;
        compositor_dnd.source_actions = actions & 7u;
        compositor_dnd.action = compositor_dnd.told_action = 0;
        compositor_dnd.dropped = compositor_dnd.named = 0;
        compositor_dnd.ext_bridge = bridge, compositor_dnd.target_bridge = 0;
        return 1;
}

static void compositor_dnd_ext_drop(void)
{
        const char *name;

        if (compositor_dnd.active != 2 || compositor_dnd.dropped)
                return;
        name = compositor_dnd_name(compositor_dnd.take);
        if (compositor_dnd.target < 0 || !name || !compositor_dnd.action)
        {
                compositor_dnd_end(0);
                return;
        }
        compositor_dnd.dropped = 1;
        compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_DROP, (int)compositor_dnd.action);
        if (!compositor_dnd.ext_bridge || !compositor_dnd.ext_bridge->read_drop ||
            !compositor_dnd.ext_bridge->read_drop(compositor_dnd.ext_bridge->ctx, name, compositor_dnd.target))
                compositor_dnd_end(0);
}

static void compositor_dnd_ext_ready(int opened, unsigned long length)
{
        if (compositor_dnd.active != 2 || !compositor_dnd.dropped || opened < 0)
        {
                if (opened >= 0)
                        system_close(opened);
                compositor_dnd_end(0);
                return;
        }
        compositor_dnd_keep(opened, length);
        compositor_dnd_ready();
}

// Canonical clipboard paste/read access policy: service enabled, row's root has focus,
// and the user performed an input gesture on row within COMPOSITOR_CLIP_GESTURE_NS.
static b32 compositor_clip_may_access(int row, positive now)
{
        int root;

        if (!compositor.table || !compositor.table->clip_on || row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        root = compositor_clip_root(row);
        return root >= 0 && root == compositor.focus &&
               now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS;
}

// Validates and installs a sealed clipboard memfd owned by this compositor process
// (shared by native, Wayland, and XWM selection bridges). Returns 1 if installed, 0 on failure.
static b32 compositor_clip_set_memfd(unsigned int which, int fd, int owner)
{
        unsigned long length = 0;
        int opened;

        if (!compositor.table || which > 1 || fd < 0)
                return 0;
        opened = compositor_clip_open(compositor.table->clip_pid, fd, &length);
        if (opened < 0)
                return 0;
        compositor_clip_keep(which, opened, length, owner);
        return 1;
}

// What a row's knock may carry for the clipboard: an offer of a copy, a paste asked for, a drag and a drop (compositor_drag_door). The
// keyboard's key requests are read here too.
static void compositor_clip_door(int row)
{
        struct windows address_to table = compositor.table;

        compositor_dnd_door(row);
        unsigned int offer = __atomic_load_n(&table->clip_offer_serial[row], __ATOMIC_ACQUIRE);
        unsigned int ask = __atomic_load_n(&table->clip_ask_serial[row], __ATOMIC_ACQUIRE);

        compositor_osk_read(row);
        compositor_osk_sync();
        positive now;

        if (offer == compositor_clip.offer_seen[row] && ask == compositor_clip.ask_seen[row])
                return;
        now = clock_monotonic_nanoseconds();
        if (offer != compositor_clip.offer_seen[row])
        {
                unsigned int which = table->clip_offer_which[row] & 1u;
                unsigned long length = 0;
                int opened = -1;

                compositor_clip.offer_seen[row] = offer;
                // A copy is the person's: a window copies after a key or a button of theirs on it, not on its own.
                if (table->clip_on && now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS)
                        opened = compositor_clip_open(table->pid[row], table->clip_offer_fd[row], &length);
                if (opened >= 0)
                        compositor_clip_keep(which, opened, length, row);
                __atomic_store_n(&table->clip_offer_answer[row], opened >= 0 ? offer : offer | WINDOWS_CLIP_NO, __ATOMIC_RELEASE);
                compositor_clip_tell(row, which, opened >= 0 ? WINDOWS_CLIP_TAKEN : WINDOWS_CLIP_DECLINED);
        }
        if (ask != compositor_clip.ask_seen[row])
        {
                unsigned int which = table->clip_ask_which[row] & 1u;
                b32 may = compositor_clip.fd[which] >= 0 && compositor_clip_may_access(row, now);

                compositor_clip.ask_seen[row] = ask;
                if (may)
                {
                        table->clip_given_length[row] = table->clip_length[which];
                        __atomic_store_n(&table->clip_given_serial[row], ask, __ATOMIC_RELEASE);
                }
                compositor_clip_tell(row, which, may ? WINDOWS_CLIP_READY : WINDOWS_CLIP_REFUSED);
        }
}

// What the command line asked in the header: an offer, a clear, the settings changed.
static void compositor_clip_cli_read(void)
{
        struct windows address_to table = compositor.table;
        unsigned int serial;

        if (table->cli_clear)
        {
                if (table->cli_clear == 2)
                        compositor_clip_settings();
                else
                        compositor_clip_wipe();
                __atomic_store_n(&table->cli_clear, 0u, __ATOMIC_RELEASE);
        }
        serial = __atomic_load_n(&table->cli_serial, __ATOMIC_ACQUIRE);
        if (serial != compositor_clip.cli_seen)
        {
                unsigned long length = 0;
                int opened = table->clip_on ? compositor_clip_open(table->cli_pid, (int)table->cli_fd, &length) : -1;

                compositor_clip.cli_seen = serial;
                if (opened >= 0)
                        compositor_clip_keep(table->cli_which & 1u, opened, length, -2);
                __atomic_store_n(&table->cli_answer, opened >= 0 ? serial : serial | WINDOWS_CLIP_NO, __ATOMIC_RELEASE);
        }
}

// A knock on the clipboard's own door: its bytes drained by its thread (the door not polled until it has), or here with none.
static void compositor_clip_cli(void)
{
        if (compositor_door_ask(COMPOSITOR_DOOR_MISC, COMPOSITOR_DOOR_DRAIN))
                rows_add(&compositor_doors.busy, COMPOSITOR_DOOR_MISC);
        else
                compositor_door_drain(compositor_clip.door);
        compositor_clip_cli_read();
}

// The door made with the table's, and the service's numbers in the header.
static int compositor_clip_start(void)
{
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0, 0, 0, 0);
        compositor_call(syscall(mknodat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0010000 | WINDOWS_MODE, 0, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, WINDOWS_MODE, 0, 0, 0);
        compositor_clip.door = (int)system_open_at(AT_FDCWD, (string_address)WINDOWS_CLIP_DOOR, FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC);
        compositor_clip_reserve();
        compositor.table->clip_pid = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        compositor.table->clip_owner[0] = compositor.table->clip_owner[1] = -1;
        compositor_clip_settings();
#ifndef SHELL_NO_GPU
        compositor_gpu_settings();
#endif
        return compositor_clip.door;
}

// A row gone: its copies stay (they are this process's now), and are owned by nobody.
static void compositor_clip_leave(int row)
{
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.table->clip_owner[which] == row)
                        compositor.table->clip_owner[which] = -1;
        compositor_clip.gesture[row] = 0;
        compositor_clip.offer_seen[row] = compositor.table->clip_offer_serial[row];
        compositor_clip.ask_seen[row] = compositor.table->clip_ask_serial[row];
        compositor_dnd_gone(row);
}

/* ---- popups: menus a window opens, placed and dismissed here ---- */

/*
        A popup is a row whose program named a parent row (popup_parent) and an anchor in the parent's pixels before
        its first picture. It is put against the anchor on the screen that holds the anchor -- a point's menu below
        and right of it, a rectangle's below it, a submenu's right of it -- and flipped to the other side where that
        screen ends, then held inside the screen. It has no tab, never takes the keys from its window (the window is
        still the one shown with them), but the keys go to the topmost popup of the window that has them while one is
        open. It is asked to go (WINDOW_CLOSING) by a press outside every popup, its window losing the keys, its
        window being taken hold of to move, or its window going.
*/
// compositor_popup_parent (damage.c): each row's parent, -1 for none, as a row is made (compositor_rows_make).

/*
        Where a popup width by height goes against an anchor (ax, ay, aw, ah in the desktop's space) on a screen (sx,
        sy, sw, sh): the pure rule, which the check holds to every edge. flags are WINDOW_POPUP_*.
*/
static void compositor_popup_place(int ax, int ay, int aw, int ah, int width, int height, unsigned int flags, const int *screen,
                                   int *x, int *y)
{
        // The window's flags say the side and the flips (ui_anchor_place does the placing, for the kit's panels too): EXACT is
        // its own place, its top left held on the screen; RIGHT is beside the rectangle's right edge, flipped left when it
        // would run off; a POINT is a rectangle of no size, below and flipped above, and left when it would run off the right;
        // else below the rectangle, flipped above when it would run off the bottom.
        int rect[4] = {ax, ay, aw, ah};
        unsigned int side = UI_BELOW, flip = UI_FLIP_SIDE;

        if (flags & WINDOW_POPUP_EXACT)
                side = UI_AT, flip = 0;
        else if (flags & WINDOW_POPUP_RIGHT)
                side = UI_RIGHT;
        else if (flags & WINDOW_POPUP_POINT)
                rect[2] = rect[3] = 0, flip |= UI_FLIP_CROSS;
        ui_anchor_place(side, flip, rect, screen, width, height, x, y);
}

// The window a popup belongs to at the root of its chain, or the row itself.
static int compositor_popup_root(int row)
{
        for (int hops = 0; hops < (int)compositor_rows_in_use && row >= 0 && compositor_popup_parent[row] >= 0; hops++)
                row = compositor_popup_parent[row];
        return row;
}

// The topmost popup of a window, which the keys go to while it is open, or the window itself. A popup asked to go (a press
// outside it, its window losing the keys) has none: until it has gone it is no one's to type to, and a hung one would keep them.
static int compositor_popup_top(int root)
{
        if (root < 0)
                return root;
        for (unsigned int at = compositor.stacked; at--;)
        {
                int row = compositor.stack[at];

                if (compositor_popup_parent[row] >= 0 && compositor.showing[row] && !(compositor.state[row] & WINDOW_CLOSING) &&
                    compositor_popup_root(row) == root)
                        return row;
        }
        return root;
}

// Every popup asked to go: of one window (root), or of every window (-1); one in keep's chain stays. True when a popup
// that was still open is asked now: one already asked, on its way out, is not counted.
static b32 compositor_popups_close(int root, int keep)
{
        b32 asked = false;

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                b32 kept = false;

                if (compositor_popup_parent[row] < 0 || !compositor.showing[row] || compositor_row_xoverride(row))
                        continue;
                if (root >= 0 && compositor_popup_root(row) != root)
                        continue;
                for (int link = keep, hops = 0; link >= 0 && hops < (int)compositor_rows_in_use; link = compositor_popup_parent[link], hops++)
                        kept |= link == row;
                if (!kept)
                {
                        asked |= !(compositor.state[row] & WINDOW_CLOSING);
                        compositor_close(row);
                }
        }
        return asked;
}

/*
        A first picture that is a popup's: its parent remembered, its place worked out against the anchor and the screen
        the anchor is on, and raised over everything without the keys. False for a window, which is placed as ever.
*/
static b32 compositor_popup_first(int row)
{
        struct windows *table = compositor.table;
        unsigned int named = table->popup_parent[row];
        int parent = named >= 1 && named <= (unsigned int)COMPOSITOR_ROWS ? (int)named - 1 : -1;
        unsigned int count;
        const int (*screens)[4];
        int ax, ay, aw, ah, x, y, at;

        if (parent < 0 || parent == row || !compositor.showing[parent])
                return false;
        compositor_popup_parent[row] = parent;
        ax = compositor.x[parent] + compositor_fit(table->popup_x[row], -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST);
        ay = compositor.y[parent] + compositor_fit(table->popup_y[row], -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST);
        aw = compositor_fit(table->popup_w[row], 0, WINDOW_PLACE_MOST);
        ah = compositor_fit(table->popup_h[row], 0, WINDOW_PLACE_MOST);
        screens = compositor_screens(&count);
        at = compositor_screen_at(screens, count, ax, ay);
        at = at < 0 ? compositor_screen_of(parent) : at;
        at = at < 0 ? 0 : at;
        compositor_popup_place(ax, ay, aw, ah, (int)compositor.w[row], (int)compositor.h[row], table->popup_flags[row], screens[at], &x, &y);
        compositor_put(row, x, y);
        compositor_raise(row);
        compositor_damage_row(row);
        return true;
}

// A row gone: it is no popup any more, and every popup descended from it goes too.
static void compositor_popup_leave(int row)
{
        compositor_popup_parent[row] = -1;
        for (int child = 0; child < (int)compositor_rows_in_use; child++)
        {
                b32 under = false;

                for (int link = compositor_popup_parent[child], hops = 0; link >= 0 && hops < (int)compositor_rows_in_use;
                     link = compositor_popup_parent[link], hops++)
                        if (link == row)
                        {
                                under = true;
                                break;
                        }
                if (under)
                {
                        if (compositor.showing[child] && !compositor_row_xoverride(child))
                                compositor_close(child);
                        compositor_popup_parent[child] = -1;
                }
        }
}

#ifdef CONFIG_MOONWATER_WAYLAND
static long wayland_row_owner(long row);
#endif

/*
        Protocol-neutral external row allocation and adoption: X11, Wayland, and Proton bridges
        claim and bind Canvas rows directly without duplicating table CAS/reset/popup plumbing.
*/
#define COMPOSITOR_ROW_TOPLEVEL 0
#define COMPOSITOR_ROW_PLACED   1
#define COMPOSITOR_ROW_POPUP    2

struct compositor_row_spec
{
        unsigned int pid;         // external process pid (0 -> 1u sentinel)
        unsigned int kind;        // COMPOSITOR_ROW_TOPLEVEL / PLACED / POPUP
        unsigned int glass;       // initial table->glass[row] flags
        unsigned int asked;       // non-zero if user explicitly launched this window
        int parent;               // owner or popup parent row, or -1
        int x, y;                 // position (PLACED) or popup anchor (POPUP)
        unsigned int popup_flags; // WINDOW_POPUP_* (0 -> WINDOW_POPUP_POINT | WINDOW_POPUP_EXACT)
};

// Configure an already-claimed Canvas row from a protocol-neutral spec and publish its pid.
static int compositor_row_adopt(int row, const struct compositor_row_spec *spec)
{
        struct windows *table = compositor.table;

        if (!table || !spec || row < 0 || row >= (int)compositor_rows_in_use)
                return -1;
        table->glass[row] = spec->glass;
        table->asked[row] = spec->asked ? 1u : 0u;
        compositor_owner[row] = (spec->kind == COMPOSITOR_ROW_TOPLEVEL || spec->kind == COMPOSITOR_ROW_PLACED) &&
                                        spec->parent >= 0 && spec->parent < (int)compositor_rows_in_use
                                    ? spec->parent + 1
                                    : 0;
        if (spec->kind == COMPOSITOR_ROW_TOPLEVEL)
                table->place[row] = WINDOW_PLACE_FREE;
        else if (spec->kind == COMPOSITOR_ROW_PLACED)
                table->x[row] = spec->x, table->y[row] = spec->y;
        else if (spec->kind == COMPOSITOR_ROW_POPUP && spec->parent >= 0)
        {
                table->popup_parent[row] = (unsigned int)spec->parent + 1;
                table->popup_x[row] = spec->x;
                table->popup_y[row] = spec->y;
                table->popup_flags[row] = spec->popup_flags ? spec->popup_flags : (WINDOW_POPUP_POINT | WINDOW_POPUP_EXACT);
                table->glass[row] |= WINDOW_NO_TAB;
        }
        __atomic_store_n(&table->pid[row], spec->pid > 0 ? spec->pid : 1u, __ATOMIC_RELEASE);
        return row;
}

// Atomically claim a free Canvas row (growing table rows if needed), reset it, and adopt spec.
static int compositor_row_open_external(const struct compositor_row_spec *spec)
{
        struct windows *table = compositor.table;

        if (!table || !spec)
                return -1;
        for (int row = 0;
             row < (int)compositor_rows_in_use ||
             (row == (int)compositor_rows_in_use && compositor_rows_grow(compositor_rows_in_use + COMPOSITOR_ROWS_SPARE) > (unsigned int)row);
             row++)
        {
                unsigned int none = 0;

                if (!__atomic_compare_exchange_n(&table->pid[row], &none, WINDOWS_TAKING, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
                        continue;
                windows_reset_row(table, (unsigned int)row);
                return compositor_row_adopt(row, spec);
        }
        return -1;
}

// Full row teardown: clipboard ownership/drag, popups, and compositor row state.
static void compositor_row_release(int row)
{
        compositor_clip_leave(row);
        compositor_popup_leave(row);
        compositor_leave(row);
}

// A row whose program has gone, its new door open: let go.
static void compositor_door_gone(int row)
{
#ifdef CONFIG_MOONWATER_WAYLAND
        //      A window of the shim's (a Wayland client's, or an X window's through Xwayland) has no program at its door: whoever opened
        //      it to knock (moonwater window move) closed it again, which says nothing of the window. The shim alone lets it go
        //      (row_close). It was let go here, while its client went on drawing into a row that was no longer on the screen.
        if (wayland_row_owner(row) >= 0)
                return;
#endif
        compositor_row_release(row);
}

/*
        A door knocked on or closed: the row read at once. The knock's bytes are drained, and a closed door opened again, by
        the row's thread (compositor_doors), which tells the loop when it is done: the row is read again then, and a row
        whose door closed is let go once its new door is open. With no thread it is all done here, the drain first.
*/
static void compositor_door(int row, int returned)
{
        if (compositor_door_ask((unsigned int)row, returned & COMPOSITOR_POLLHUP ? COMPOSITOR_DOOR_REOPEN : COMPOSITOR_DOOR_DRAIN))
        {
                rows_add(&compositor_doors.busy, row);
                compositor_take(row);
                compositor_clip_door(row);
                return;
        }
        // Any program may open a door and write to it, so what is read in one turn is bounded: one that never stops
        // writing is not the whole of the compositor's time.
        compositor_door_drain(compositor.door[row]);
        compositor_take(row);
        compositor_clip_door(row);
        if (!(returned & COMPOSITOR_POLLHUP))
                return;

        compositor_door_reopen((unsigned int)row);
        compositor_door_gone(row);
}

// What the doors' threads have done since the loop last looked: a drained row read again, a reopened one let go.
static void compositor_doors_done(void)
{
        unsigned long count = 0;
        struct compositor_rows drained, reopened;

        compositor_call(syscall(read), (positive)compositor_doors.done, (positive)&count, sizeof count, 0, 0, 0);
        rows_take(&drained, &compositor_doors.drained);
        rows_take(&reopened, &compositor_doors.reopened);
        rows_and_not(&compositor_doors.busy, &drained), rows_and_not(&compositor_doors.busy, &reopened);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (rows_has(&drained, row) || rows_has(&reopened, row))
                {
                        compositor_take(row);
                        compositor_clip_door(row);
                        if (rows_has(&reopened, row))
                                compositor_door_gone(row);
                }
        if (rows_has(&drained, COMPOSITOR_DOOR_MISC))
                compositor_clip_cli_read();
}

