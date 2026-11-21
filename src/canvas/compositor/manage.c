/*
        Window management: stack and focus, grips and shared dividers, tiles/grid/snap overlays, room & placement
        persistence, row intake/teardown (compositor_take, compositor_leave), OSK, clipboard, DnD, popups, and doors.
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

#define COMPOSITOR_ROW_HOOKS_MAX 8
#define COMPOSITOR_OVERLAY_ASKS (WINDOW_DISMISS | WINDOW_NO_CLOSE)

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

#define COMPOSITOR_ROW_HOOK_CALL(fn, ...)                                                \
        do                                                                               \
        {                                                                                \
                for (unsigned int _hi = 0; _hi < compositor_row_hook_count; _hi++)       \
                        if (compositor_row_hook_list[_hi]->fn)                           \
                                compositor_row_hook_list[_hi]->fn(compositor_row_hook_list[_hi]->ctx, __VA_ARGS__); \
        } while (0)

static void compositor_row_notify_stack(int row) { COMPOSITOR_ROW_HOOK_CALL(stack, row); }

static signed char compositor_layer_clamp(int layer)
{
        return layer < WINDOW_LAYER_BACKGROUND ? WINDOW_LAYER_BACKGROUND
               : layer > WINDOW_LAYER_OVERLAY  ? WINDOW_LAYER_OVERLAY
                                               : (signed char)layer;
}

static int compositor_popup_root(int row);

static signed char compositor_layer_of(int row)
{
        signed char layer = compositor.layer[row];

        for (int p = compositor_popup_parent[row], hops = 0;
             p >= 0 && p < (int)compositor_rows_in_use && hops++ < (int)compositor_rows_in_use; p = compositor_popup_parent[p])
        {
                signed char pl = compositor.layer[p];

                if (pl == WINDOW_LAYER_NORMAL && (compositor.asks[p] & COMPOSITOR_OVERLAY_ASKS) == COMPOSITOR_OVERLAY_ASKS)
                        pl = WINDOW_LAYER_OVERLAY;
                if (pl > layer)
                        layer = pl;
        }
        if (layer == WINDOW_LAYER_NORMAL && compositor_row_xoverride(row) && !compositor.fullscreen[row] &&
            !((int)compositor.w[row] >= compositor.width && (int)compositor.h[row] >= compositor.height))
                return WINDOW_LAYER_ABOVE;
        if (layer == WINDOW_LAYER_NORMAL && (compositor.asks[row] & COMPOSITOR_OVERLAY_ASKS) == COMPOSITOR_OVERLAY_ASKS)
                return WINDOW_LAYER_OVERLAY;
        return layer;
}

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

static void compositor_raise(int row)
{
        compositor_raise_one(row);
        if (compositor_popup_parent[row] >= 0)
                return;
        for (int c = 0; c < (int)compositor_rows_in_use; c++)
                if (c != row && compositor_owner[c] == row + 1 && compositor_visible(c) && compositor_popup_parent[c] < 0)
                        compositor_raise_one(c);
        for (int c = 0; c < (int)compositor_rows_in_use; c++)
                if (c != row && compositor_popup_parent[c] >= 0 && compositor_popup_root(c) == row && compositor_visible(c))
                        compositor_raise_one(c);
}

static unsigned int compositor_modifiers(void);

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

static void compositor_focus(int row)
{
        int was = compositor.focus;

        if (was == row || (row >= 0 && (compositor_popup_parent[row] >= 0 || (compositor.asks[row] & WINDOW_NO_KEYS))))
                return;
        if (was >= 0)
                compositor_popups_close(was, -1);
        compositor.focus = row;
        compositor.table->focus = row < 0 ? WINDOWS : (unsigned int)row;
        COMPOSITOR_ROW_HOOK_CALL(focus, was, row);
        if (compositor.keys_to != row)
                compositor_keys_release();
        if (was >= 0)
                rows_add(&compositor.woken, was), compositor_damage_row(was);
        if (row >= 0)
                rows_add(&compositor.woken, row), compositor_damage_row(row);
        compositor_tabs_changed();
        compositor_osk_sync();
}

static b32 compositor_cycles(int row) { return !(compositor.asks[row] & WINDOW_NO_TAB); }

static void compositor_refocus(void)
{
        int next = COMPOSITOR_NONE;

        for (unsigned int at = compositor.stacked; at-- && next < 0;)
                if (compositor_visible(compositor.stack[at]) && compositor_popup_parent[compositor.stack[at]] < 0 &&
                    !(compositor.state[compositor.stack[at]] & WINDOW_CLOSING) && compositor_cycles(compositor.stack[at]))
                        next = compositor.stack[at];
        compositor_focus(next);
}

static int compositor_screen_of(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        return count < 2 ? 0
                         : compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                                   (int)compositor.h[row]);
}

static void compositor_row_rect(int row, int rect[4])
{
        rect[0] = compositor.x[row], rect[1] = compositor.y[row];
        rect[2] = (int)compositor.w[row], rect[3] = (int)compositor.h[row];
}

// Minimum and maximum size a window accepts on an axis (down 0 = width, 1 = height) in device pixels; *most == 0 means unbounded.
static void compositor_bounds(int row, int down, int *least_out, int *most_out)
{
        b32 named = row >= 0 && row < COMPOSITOR_ROWS && compositor.table;
        unsigned int own_min = named ? (down ? compositor.table->least_height[row] : compositor.table->least_width[row]) : 0;
        unsigned int own_max = named ? (down ? compositor.table->most_height[row] : compositor.table->most_width[row]) : 0;
        unsigned int scale = named ? compositor.table->window_scale[row] : 0;
        int floor_min = (down ? WINDOW_MIN_HEIGHT : WINDOW_MIN_WIDTH) * compositor.scale / 100, asked_min;

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

static unsigned int compositor_resizable_axes(int row)
{
        int lw, mw, lh, mh;

        compositor_bounds(row, 0, &lw, &mw);
        compositor_bounds(row, 1, &lh, &mh);
        return ((!mw || mw > lw) ? (COMPOSITOR_EDGE_LEFT | COMPOSITOR_EDGE_RIGHT) : 0u) |
               ((!mh || mh > lh) ? (COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_BOTTOM) : 0u);
}

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

static int compositor_grip(void) { return compositor.grip ? compositor.grip : COMPOSITOR_GRIP; }

static unsigned int compositor_edge_zone(int row, int x, int y)
{
        unsigned int axes, edges;
        int touch, s = compositor.scale, line, grip, in, x1, y1, x2, y2;

        if (compositor_popup_parent[row] >= 0 || !(axes = compositor_resizable_axes(row)))
                return 0;
        touch = compositor.touching, line = compositor_line(row);
        grip = (touch ? COMPOSITOR_TOUCH_GRIP : compositor_grip()) * s / 100 + line;
        in = (touch ? COMPOSITOR_TOUCH_GRIP_IN : COMPOSITOR_GRIP_IN) * s / 100;
        x1 = compositor.x[row], y1 = compositor.y[row], x2 = x1 + (int)compositor.w[row], y2 = y1 + (int)compositor.h[row];
        edges = compositor_edges_at(x, y, x1, y1, x2, y2, grip, in, COMPOSITOR_GRIP_IN * s / 100, touch ? COMPOSITOR_TOUCH_CORNER * s / 100 : 0);
        if (touch)
        {
                unsigned int corner = compositor_corner_reach(x, y, x1, y1, x2, y2, COMPOSITOR_GRIP * s / 100 + line,
                                                              COMPOSITOR_GRIP_IN * s / 100, COMPOSITOR_FINGER_TARGET * s / 100);
                if (corner)
                        edges = corner;
        }
        edges &= axes;
        if (!edges)
                return 0;
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                unsigned int count;
                const int *sc = compositor_screens(&count)[compositor_screen_of(row)];

                edges &= ~((y1 <= sc[1] ? (unsigned int)COMPOSITOR_EDGE_TOP : 0u) |
                           (compositor.arranged[row] == COMPOSITOR_ROOM || y2 >= sc[1] + sc[3] ? (unsigned int)COMPOSITOR_EDGE_BOTTOM : 0u) |
                           (x1 <= sc[0] ? (unsigned int)COMPOSITOR_EDGE_LEFT : 0u) |
                           (x2 >= sc[0] + sc[2] ? (unsigned int)COMPOSITOR_EDGE_RIGHT : 0u));
        }
        return edges;
}

static int compositor_band_hit(int x, int y)
{
        int hit = TASKBAR_HIT_NONE, bands = compositor_bands();
        int reach = compositor.touching ? COMPOSITOR_FINGER_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; compositor.band_at < bands && hit == TASKBAR_HIT_NONE; compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                hit = taskbar_hit_reach(&view, x, y, reach);
        }
        compositor.band_at = 0;
        return hit;
}

static int compositor_reach_most(void)
{
        int scale = compositor.scale;

        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor.output[at].scale > scale)
                        scale = compositor.output[at].scale;
        return (compositor.touching ? COMPOSITOR_TOUCH_GRIP + COMPOSITOR_GRIP + COMPOSITOR_FINGER_TARGET : compositor_grip()) *
                   compositor.scale / 100 + compositor_line_px(scale) + 1;
}

static int compositor_under(int x, int y, int *tab)
{
        const address_any columns[4] = {compositor.x, compositor.y, compositor.w, compositor.h};
        positive reach, count = compositor.stacked;

        *tab = compositor_band_hit(x, y);
        if (*tab != TASKBAR_HIT_NONE)
                return COMPOSITOR_NONE;
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
                      y < compositor.y[row] + (int)compositor.h[row]) || compositor_edge_zone(row, x, y)))
                        return row;
                count = (positive)at;
        }
        return COMPOSITOR_NONE;
}

static void compositor_put(int row, int x, int y)
{
        int changed = compositor.x[row] != x || compositor.y[row] != y;

        compositor.x[row] = compositor.table->x[row] = x;
        compositor.y[row] = compositor.table->y[row] = y;
        if (changed && compositor_row_hook_count)
                COMPOSITOR_ROW_HOOK_CALL(configure, row, x, y, (int)compositor.w[row], (int)compositor.h[row]);
}

static void compositor_place(int row, int x, int y)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        compositor_screen_keep_window(screens, count, &x, &y, (int)compositor.w[row], (int)compositor.h[row],
                                      compositor_line(row), COMPOSITOR_GRAB * compositor.scale / 100);
        compositor_put(row, x, y);
}

static void compositor_row_clamp(int row, int *width, int *height)
{
        int lw, mw, lh, mh, cap_w = compositor_most_width(), cap_h = compositor_most_height();

        compositor_bounds(row, 0, &lw, &mw);
        compositor_bounds(row, 1, &lh, &mh);
        if (!compositor.fullscreen[row] && compositor.layer[row] != WINDOW_LAYER_BACKGROUND)
        {
                if (mw > 0 && *width > mw)
                        *width = mw;
                if (mh > 0 && *height > mh)
                        *height = mh;
        }
        // Clamp min first, hard buffer cap last so extreme scales never exceed buffer pitch.
        if (*width < lw)
                *width = lw;
        if (*width > cap_w)
                *width = cap_w;
        if (*height < lh)
                *height = lh;
        if (*height > cap_h)
                *height = cap_h;
}

struct compositor_row_facts
{
        int x, y, scale;
        unsigned int width, height, want_width, want_height, arranged, closing;
        unsigned char fullscreen, focused, resizing, minimized, filling;
        signed char layer;
};

static int compositor_row_facts(int row, struct compositor_row_facts *out)
{
        struct windows *table = compositor.table;
        b32 want;

        if (!out || row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row])
                return -1;
        want = table && (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row]);
        *out = (struct compositor_row_facts){
            .x = compositor.x[row], .y = compositor.y[row], .scale = compositor_row_scale(row),
            .width = compositor.w[row], .height = compositor.h[row],
            .want_width = want ? table->want_width[row] : 0, .want_height = want ? table->want_height[row] : 0,
            .arranged = compositor.arranged[row], .fullscreen = compositor.fullscreen[row], .layer = compositor.layer[row],
            .focused = compositor.focus == row, .resizing = compositor.resize == row,
            .minimized = (compositor.state[row] & WINDOW_MINIMIZED) != 0, .filling = compositor_filling(row) != 0,
            .closing = (compositor.state[row] & WINDOW_CLOSING) ? (unsigned int)compositor.close_asks[row] : 0,
        };
        return 0;
}

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
        memory_zero(title, sizeof(title));
        if (at)
                memory_copy(title, utf8, at);
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
        rect[0] = compositor.saved_x[row], rect[1] = compositor.saved_y[row];
        rect[2] = (int)compositor.saved_w[row], rect[3] = (int)compositor.saved_h[row];
}

static void compositor_row_saved_set(int row, const int rect[4])
{
        compositor.saved_x[row] = rect[0], compositor.saved_y[row] = rect[1];
        compositor.saved_w[row] = (unsigned int)rect[2], compositor.saved_h[row] = (unsigned int)rect[3];
}

static void compositor_row_save_floating(int row)
{
        if (compositor.arranged[row] == COMPOSITOR_FLOATING)
        {
                int cur[4];
                compositor_row_rect(row, cur);
                compositor_row_saved_set(row, cur);
        }
}

static void compositor_resize_place(int row, unsigned int width, unsigned int height)
{
        int x = compositor.x[row], y = compositor.y[row];

        compositor.w[row] = width, compositor.h[row] = height;
        if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                x = compositor.far_x[row] - (int)width;
        if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                y = compositor.far_y[row] - (int)height;
        compositor_place(row, x, y);
}

static void compositor_want(int row, int width, int height)
{
        compositor_row_clamp(row, &width, &height);
        compositor.table->want_width[row] = (unsigned int)width;
        compositor.table->want_height[row] = (unsigned int)height;
        rows_add(&compositor.woken, row);
        if (!compositor.showing[row])
                return;
        compositor_damage_row(row);
        compositor.asking[row] = compositor.picture_w[row] != (unsigned int)width || compositor.picture_h[row] != (unsigned int)height;
        compositor_resize_place(row, (unsigned int)width, (unsigned int)height);
        COMPOSITOR_ROW_HOOK_CALL(configure, row, compositor.x[row], compositor.y[row], width, height);
        compositor_wl_settle(row);
        compositor_stretch(row);
        compositor_damage_row(row);
}

static void compositor_fit_row(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int room[4], on, w, h;

        if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_FLOATING)
                return;
        on = compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]);
        compositor_room(screens[on], room);
        w = (int)compositor.w[row] > room[2] ? room[2] : (int)compositor.w[row];
        h = (int)compositor.h[row] > room[3] ? room[3] : (int)compositor.h[row];
        if (w != (int)compositor.w[row] || h != (int)compositor.h[row])
                compositor_want(row, w, h);
}

static void compositor_scale_fit(void)
{
        rows_none(&compositor.fit);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.showing[row] && compositor.arranged[row] == COMPOSITOR_FLOATING)
                        rows_add(&compositor.fit, row), compositor_fit_row(row);
        compositor.fit_until = clock_monotonic_nanoseconds() + 3000000000ull;
}

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

/* ---- grabs, shared dividers, resize and arrangement ---- */

static void compositor_resize_begin(int row, unsigned int edges);
static void compositor_hold_take(int more, int row);
static unsigned int compositor_tile_cells(unsigned int how);

static void compositor_grab_set(unsigned int kind, int row, unsigned int code, unsigned int edges)
{
        compositor.active_grab = (struct compositor_grab){kind, row, code, edges};
}

static void compositor_grab_clear(void) { compositor_grab_set(COMPOSITOR_GRAB_NONE, COMPOSITOR_NONE, 0, 0); }

static int compositor_edge_axis(unsigned int edge, int *vertical, int *high)
{
        if (edge != COMPOSITOR_EDGE_LEFT && edge != COMPOSITOR_EDGE_RIGHT && edge != COMPOSITOR_EDGE_TOP && edge != COMPOSITOR_EDGE_BOTTOM)
                return 0;
        *vertical = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_RIGHT;
        *high = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_TOP;
        return 1;
}

static void compositor_row_span(int row, int vertical, int *near, int *far, int *a, int *b)
{
        *near = vertical ? compositor.x[row] : compositor.y[row];
        *far = *near + (int)(vertical ? compositor.w[row] : compositor.h[row]);
        *a = vertical ? compositor.y[row] : compositor.x[row];
        *b = *a + (int)(vertical ? compositor.h[row] : compositor.w[row]);
}

static int compositor_meets(int row, unsigned int edge)
{
        int vertical, high, near, far, a, b;

        if (!compositor_edge_axis(edge, &vertical, &high))
                return COMPOSITOR_NONE;
        compositor_row_span(row, vertical, &near, &far, &a, &b);
        for (int other = 0; other < (int)compositor_rows_in_use; other++)
        {
                int onear, ofar, oa, ob;

                if (other == row || !compositor_visible(other) || compositor_popup_parent[other] >= 0)
                        continue;
                compositor_row_span(other, vertical, &onear, &ofar, &oa, &ob);
                if (oa == a && ob == b && (high ? ofar == near : onear == far))
                        return other;
        }
        return COMPOSITOR_NONE;
}

struct compositor_divider
{
        struct compositor_rows lo, hi;
        int lo_row, hi_row, vertical, line, from, to;
};

static void compositor_divider_measure(struct compositor_divider *d)
{
        int first = 1;

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                int near, far, a, b;

                if (!rows_has(&d->lo, row) && !rows_has(&d->hi, row))
                        continue;
                compositor_row_span(row, d->vertical, &near, &far, &a, &b);
                if (first || a < d->from)
                        d->from = a;
                if (first || b > d->to)
                        d->to = b;
                if (rows_has(&d->hi, row))
                        d->line = near;
                first = 0;
        }
}

static int compositor_divider_of(int row, unsigned int edge, struct compositor_divider *d)
{
        int vertical, high, near, far, from, to, line, other, screen = compositor_screen_of(row), grew = 1;

        if (!compositor_edge_axis(edge, &vertical, &high))
                return 0;
        other = compositor_meets(row, edge);
        compositor_row_span(row, vertical, &near, &far, &from, &to);
        line = high ? near : far;
        d->vertical = vertical;
        rows_none(&d->lo), rows_none(&d->hi), rows_add(high ? &d->hi : &d->lo, row);
        d->lo_row = high ? other : row, d->hi_row = high ? row : other;
        while (grew && compositor_tile_cells(compositor.arranged[row]))
        {
                grew = 0;
                for (int each = 0; each < (int)compositor_rows_in_use; each++)
                {
                        int en, ef, ea, eb;

                        if (rows_has(&d->lo, each) || rows_has(&d->hi, each) || !compositor_visible(each) || compositor_popup_parent[each] >= 0 ||
                            !compositor_tile_cells(compositor.arranged[each]) || compositor_screen_of(each) != screen)
                                continue;
                        compositor_row_span(each, vertical, &en, &ef, &ea, &eb);
                        if (ea >= to || eb <= from || (en != line && ef != line))
                                continue;
                        if (ef == line)
                                rows_add(&d->lo, each), d->lo_row = d->lo_row >= 0 ? d->lo_row : each;
                        else
                                rows_add(&d->hi, each), d->hi_row = d->hi_row >= 0 ? d->hi_row : each;
                        from = ea < from ? ea : from, to = eb > to ? eb : to, grew = 1;
                }
        }
        if (!rows_any(&d->lo) || !rows_any(&d->hi))
        {
                if (other == COMPOSITOR_NONE)
                        return 0;
                rows_none(&d->lo), rows_none(&d->hi), rows_add(&d->lo, high ? other : row), rows_add(&d->hi, high ? row : other);
        }
        compositor_divider_measure(d);
        return 1;
}

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

static int compositor_shared_at(int row, unsigned int edge, int x, int y, struct compositor_divider *d)
{
        int rect[4];

        if (!edge || (edge & (edge - 1)) || !compositor_divider_of(row, edge, d))
                return 0;
        compositor_shared_strip(d, rect);
        return d->vertical ? y >= rect[1] && y < rect[1] + rect[3] : x >= rect[0] && x < rect[0] + rect[2];
}

static void compositor_mark_set(int row, int with, const int *rect)
{
        if (row == compositor.mark_row && with == compositor.mark_with && !memory_compare(rect, compositor.mark, sizeof(compositor.mark)))
                return;
        if (compositor.mark[2] > 0)
                compositor_damage(compositor.mark[0], compositor.mark[1], compositor.mark[2], compositor.mark[3]);
        memory_copy(compositor.mark, rect, sizeof(compositor.mark));
        compositor.mark_row = row, compositor.mark_with = with;
        if (rect[2] > 0)
                compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

static void compositor_mark_clear(void)
{
        int none[4] = {0, 0, 0, 0};

        compositor_mark_set(COMPOSITOR_NONE, COMPOSITOR_NONE, none);
        rows_none(&compositor.mark_rows);
}

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

static int compositor_shared_begin(int row, unsigned int edge)
{
        struct compositor_divider d;
        unsigned int was = compositor.arranged[row], lo_edge, hi_edge;
        struct compositor_rows all;

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

static void compositor_shared_move(void)
{
        int vertical = compositor.shared_vertical, low = -2147483647, high = 2147483647;
        int travel = vertical ? compositor.pointer_x - compositor.press_x : compositor.pointer_y - compositor.press_y;
        struct compositor_rows all = compositor.shared_lo_set;
        struct compositor_divider d;

        rows_or(&all, &compositor.shared_hi_set);
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
                if (rows_has(&all, each))
                {
                        int size = compositor.shared_sizes[each] + (rows_has(&compositor.shared_lo_set, each) ? travel : -travel);
                        compositor_want(each, vertical ? size : (int)compositor.w[each], vertical ? (int)compositor.h[each] : size);
                }
        d.lo = compositor.shared_lo_set, d.hi = compositor.shared_hi_set;
        d.lo_row = compositor.shared_lo, d.hi_row = compositor.shared_hi, d.vertical = vertical;
        compositor_divider_measure(&d);
        compositor_mark_shared(&d);
}

static void compositor_resize_begin(int row, unsigned int edges)
{
        if (!(edges &= compositor_resizable_axes(row)))
                return;
        compositor_hold_take(0, row);
        compositor.resize = row, compositor.shared_row = COMPOSITOR_NONE;
        compositor_grab_set(COMPOSITOR_GRAB_RESIZE, row, COMPOSITOR_BTN_LEFT, edges);
        compositor_mark_clear();
        compositor.resize_w = (int)compositor.w[row], compositor.resize_h = (int)compositor.h[row];
        compositor.press_x = compositor.pointer_x, compositor.press_y = compositor.pointer_y;
        if (!compositor_tile_cells(compositor.arranged[row]))
                compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0, compositor.layer[row] = WINDOW_LAYER_NORMAL;
        compositor.edges[row] = edges;
        compositor.far_x[row] = compositor.x[row] + (int)compositor.w[row];
        compositor.far_y[row] = compositor.y[row] + (int)compositor.h[row];
}

static void compositor_resize_move(void)
{
        int row = compositor.resize, dx, dy, w, h, max_w, max_h;

        if (compositor.shared_row >= 0)
        {
                compositor_shared_move();
                return;
        }
        dx = compositor.pointer_x - compositor.press_x, dy = compositor.pointer_y - compositor.press_y;
        w = compositor.resize_w, h = compositor.resize_h;
        max_w = compositor_most_width() - 2 * compositor_line(row);
        max_h = compositor_most_height() - 2 * compositor_line(row);
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

static unsigned int compositor_tile_edges(unsigned int how)
{
        return (how == COMPOSITOR_RIGHT || how == COMPOSITOR_TOP_RIGHT || how == COMPOSITOR_BOTTOM_RIGHT ? COMPOSITOR_EDGE_LEFT : 0) |
               (how == COMPOSITOR_BOTTOM || how == COMPOSITOR_BOTTOM_LEFT || how == COMPOSITOR_BOTTOM_RIGHT ? COMPOSITOR_EDGE_TOP : 0);
}

static void compositor_set_rect(int row, unsigned int how, const int *rect)
{
        int was[4], now[4];

        compositor_row_rect(row, was);
        if (how != COMPOSITOR_MAXIMIZED)
                compositor.fullscreen[row] = 0;
        compositor_damage_row(row);
        compositor.arranged[row] = how, compositor.edges[row] = compositor_tile_edges(how);
        compositor.far_x[row] = rect[0] + rect[2], compositor.far_y[row] = rect[1] + rect[3];
        compositor_put(row, rect[0], rect[1]);
        compositor_damage_row(row);
        compositor_want(row, rect[2], rect[3]);
        compositor_row_rect(row, now);
        if (memory_compare(was, now, sizeof(was)))
                compositor_glide_start(row, was);
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

static void compositor_hold_take(int more, int row)
{
        if (!more)
                rows_none(&compositor.hold_rows);
        rows_add(&compositor.hold_rows, row);
        compositor_row_rect(row, compositor.hold_rect[row]);
        compositor.hold_how[row] = compositor.arranged[row];
}

// Unified grab teardown: COMMIT (release), CANCEL (revert hold_rows), STOP (client stop), or DROP (row teardown).
static int compositor_grab_end(int how, int drop_row)
{
        if (how == COMPOSITOR_GRAB_DROP)
        {
                int *slots[] = {&compositor.drag, &compositor.press, &compositor.resize, &compositor.touch.closing, &compositor.touch.hold_row};

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
                        compositor.shared_row = compositor.resize = COMPOSITOR_NONE;
                for (unsigned int i = 0; i < array_count(slots); i++)
                        if (*slots[i] == drop_row)
                                *slots[i] = COMPOSITOR_NONE;
                if (compositor.drag_glide == drop_row + 1)
                        compositor.drag_glide = 0;
                if (compositor.edge_right == (unsigned int)drop_row + 1)
                        compositor.edge_right = 0;
                if (compositor.lit_row == drop_row)
                        compositor.lit_row = COMPOSITOR_NONE, compositor.lit_edges = compositor.lit_held = 0;
                if (compositor.active_grab.row == drop_row ||
                    (compositor.active_grab.kind == COMPOSITOR_GRAB_DIVIDER && compositor.shared_row < 0))
                        compositor_grab_clear();
                return 1;
        }
        if (how == COMPOSITOR_GRAB_STOP || how == COMPOSITOR_GRAB_CANCEL)
        {
                int stop = how == COMPOSITOR_GRAB_STOP, resized = stop && compositor.resize == drop_row;

                if (stop ? (drop_row < 0 || (compositor.drag != drop_row && compositor.resize != drop_row))
                         : (compositor.drag < 0 && compositor.resize < 0))
                        return 0;
                compositor_preview(COMPOSITOR_NONE);
                compositor.drag = compositor.resize = compositor.shared_row = COMPOSITOR_NONE;
                compositor.edge_right = 0;
                if (stop ? compositor.drag_glide == drop_row + 1 : 1)
                        compositor.drag_glide = 0;
                if (!stop)
                        compositor.press = COMPOSITOR_NONE;
                compositor_mark_clear();
                compositor_lit(COMPOSITOR_NONE, 0, 0);
                compositor_grab_clear();
                if (!stop)
                        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                                if (rows_has(&compositor.hold_rows, row) && compositor.showing[row])
                                        compositor_set_rect(row, compositor.hold_how[row], compositor.hold_rect[row]);
                rows_none(&compositor.hold_rows);
                if (resized)
                        rows_add(&compositor.woken, drop_row);
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

static int compositor_hold_cancel(void) { return compositor_grab_end(COMPOSITOR_GRAB_CANCEL, COMPOSITOR_NONE); }

/* ---- tiles, snap grid, snap assist and overlays ---- */

static void compositor_float(int row);

// Quarter-cell bitmask table (TL=1, TR=2, BL=4, BR=8), halves before quarters for zone decomposition.
static const struct { unsigned char how, cells; } compositor_tile_map[] = {
    {COMPOSITOR_LEFT, 5}, {COMPOSITOR_RIGHT, 10}, {COMPOSITOR_BOTTOM, 12},
    {COMPOSITOR_TOP_LEFT, 1}, {COMPOSITOR_TOP_RIGHT, 2}, {COMPOSITOR_BOTTOM_LEFT, 4}, {COMPOSITOR_BOTTOM_RIGHT, 8},
};

static const unsigned int compositor_grid_regions[COMPOSITOR_GRID_TILES] = {
    COMPOSITOR_MAXIMIZED, COMPOSITOR_LEFT, COMPOSITOR_RIGHT,
    COMPOSITOR_TOP_LEFT, COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT,
};

static unsigned int compositor_tile_cells(unsigned int how)
{
        for (unsigned int i = 0; i < array_count(compositor_tile_map); i++)
                if (compositor_tile_map[i].how == how)
                        return compositor_tile_map[i].cells;
        return 0;
}

static unsigned int compositor_cells_tile(unsigned int cells)
{
        for (unsigned int i = 0; i < array_count(compositor_tile_map); i++)
                if (compositor_tile_map[i].cells == cells)
                        return compositor_tile_map[i].how;
        return COMPOSITOR_FLOATING;
}

static unsigned int compositor_region_cells(unsigned int how)
{
        return how == COMPOSITOR_MAXIMIZED ? COMPOSITOR_CELLS : compositor_tile_cells(how);
}

static void compositor_tile_others(int row, unsigned int how, int screen)
{
        unsigned int count, taken = compositor_region_cells(how);
        const int (*screens)[4] = compositor_screens(&count);

        for (int other = 0; other < (int)compositor_rows_in_use; other++)
        {
                unsigned int was = compositor_tile_cells(compositor.arranged[other]), left;

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
                        int part[4];
                        compositor_screen_part(screens[screen], rest, part);
                        compositor_arrange_rect(other, rest, part);
                }
        }
}

static unsigned int compositor_occupied(int row, int screen)
{
        unsigned int cells = 0;

        for (int other = 0; other < (int)compositor_rows_in_use; other++)
                if (other != row && compositor.showing[other] && compositor_screen_of(other) == screen)
                        cells |= compositor_tile_cells(compositor.arranged[other]);
        return cells;
}

static void compositor_grid_tile(const int *screen, unsigned int at, int *tile)
{
        int gap = COMPOSITOR_GRID_GAP * compositor.scale / 100, w = screen[2] / 10;
        int span = COMPOSITOR_GRID_TILES * w + (COMPOSITOR_GRID_TILES - 1) * gap;

        tile[0] = screen[0] + (screen[2] - span) / 2 + (int)at * (w + gap), tile[1] = screen[1];
        tile[2] = w, tile[3] = w > 0 ? screen[3] * w / screen[2] : 0;
}

static int compositor_grid_band(const int *screen, int x, int y)
{
        return x >= screen[0] && x < screen[0] + screen[2] && y >= screen[1] && y - screen[1] < COMPOSITOR_SNAP * compositor.scale / 100;
}

static int compositor_grid_panel(const int *screen, int x, int y)
{
        int first[4], last[4];

        compositor_grid_tile(screen, 0, first);
        compositor_grid_tile(screen, COMPOSITOR_GRID_TILES - 1, last);
        return x >= first[0] && x < last[0] + last[2] && y >= screen[1] && y - screen[1] < first[3];
}

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

static unsigned int compositor_target(int row, int x, int y, const int *box, int *screen, unsigned int *touch)
{
        unsigned int count, tile;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_at(screens, count, x, y);

        if (at >= 0 && compositor.grid_on && compositor.grid_screen == at && compositor_grid_panel(screens[at], x, y) &&
            (tile = compositor_grid_under(screens[at], x, y)) < COMPOSITOR_GRID_TILES)
        {
                unsigned int how = compositor_grid_regions[tile];
                *screen = at, *touch = 0;
                return compositor_region_cells(how) & compositor_occupied(row, at) ? COMPOSITOR_FLOATING : how;
        }
        return compositor_snap(box, touch, screen);
}

static unsigned int compositor_zones(unsigned int free, unsigned int *zones)
{
        unsigned int count = 0;

        for (unsigned int i = 0; i < array_count(compositor_tile_map); i++)
                if ((free & compositor_tile_map[i].cells) == compositor_tile_map[i].cells)
                        zones[count++] = compositor_tile_map[i].how, free &= ~compositor_tile_map[i].cells;
        return count;
}

static void compositor_assist_place(const int *zone, unsigned int at, unsigned int of, int w, int h, int *rect)
{
        int margin = COMPOSITOR_GRID_GAP * compositor.scale / 100, cols = 1, rows, cw, ch, fw, fh;

        while (cols * cols < (int)of)
                cols++;
        rows = ((int)of + cols - 1) / cols;
        cw = (zone[2] - 2 * margin) / cols, ch = (zone[3] - 2 * margin) / rows;
        cw = cw < 1 ? 1 : cw, ch = ch < 1 ? 1 : ch;
        if ((long)w * ch > (long)h * cw)
                fw = cw, fh = (int)((long)h * cw / w);
        else
                fh = ch, fw = (int)((long)w * ch / h);
        rect[0] = zone[0] + margin + (int)(at % (unsigned int)cols) * cw + (cw - fw) / 2;
        rect[1] = zone[1] + margin + (int)(at / (unsigned int)cols) * ch + (ch - fh) / 2;
        rect[2] = fw, rect[3] = fh;
}

static void compositor_assist_refresh(void)
{
        unsigned int count, zones[4], zc, free, cand[COMPOSITOR_ASSIST_MAX], taken[4] = {0}, found = 0;
        const int (*screens)[4] = compositor_screens(&count);

        compositor.assist_count = 0;
        if (!compositor.assist_on || compositor.assist_screen < 0 || (unsigned int)compositor.assist_screen >= count)
                return;
        free = COMPOSITOR_CELLS & ~(compositor_region_cells(compositor.assist_how) | compositor_occupied(compositor.assist_row, compositor.assist_screen));
        if (!(zc = compositor_zones(free, zones)))
                return;
        for (int at = compositor.stacked - 1; at >= 0 && found < COMPOSITOR_ASSIST_MAX; at--)
        {
                int row = compositor.stack[at];
                if (row != compositor.assist_row && compositor_visible(row) && compositor.arranged[row] == COMPOSITOR_FLOATING &&
                    compositor_screen_of(row) == compositor.assist_screen && compositor.w[row] && compositor.h[row])
                        cand[found++] = (unsigned int)row;
        }
        for (unsigned int at = 0; at < found; at++)
                taken[at % zc]++;
        for (unsigned int at = 0; at < found; at++)
        {
                unsigned int z = at % zc;
                int part[4], row = (int)cand[at];

                compositor_screen_part(screens[compositor.assist_screen], zones[z], part);
                compositor_assist_place(part, at / zc, taken[z], (int)compositor.w[row], (int)compositor.h[row], compositor.assist_rect[at]);
                compositor.assist_rows[at] = cand[at], compositor.assist_zone[at] = zones[z];
        }
        compositor.assist_count = (int)found;
}

static int compositor_assist_at(int x, int y)
{
        for (int at = 0; at < compositor.assist_count; at++)
        {
                const int *r = compositor.assist_rect[at];
                if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3])
                        return at;
        }
        return -1;
}

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

static int compositor_overlays_up(void) { return compositor.assist_on || compositor.grid_on; }
static int compositor_overlays_shown(void) { return compositor_overlays_up() || compositor_overlay_alpha() > 0; }

static void compositor_overlays_keep(void)
{
        if (!compositor.overlay_at)
                return;
        if (clock_monotonic_nanoseconds() - compositor.overlay_at < (positive)ui_paced(COMPOSITOR_SNAP_FADE_NS))
        {
                compositor_overlays_damage();
                return;
        }
        compositor_overlays_damage_all();
        if (!compositor.overlays_up)
                compositor.assist_count = 0;
        compositor.overlay_at = 0;
}

static unsigned int compositor_snap_edge(unsigned int percent)
{
        return ui_mix(compositor_snap_ink() | 0xff000000u, percent > 100 ? 100 : percent, compositor_ground | 0xff000000u) & 0xffffffu;
}

static int compositor_overlay_scale(int screen)
{
        int s = compositor_output_scale(screen);
        return s > 0 ? s : compositor.scale;
}

static int compositor_overlay_boxes_of(int boxes[COMPOSITOR_ASSIST_MAX + 1][4], int all)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int n = 0, first[4], last[4], shown = all || compositor_overlays_shown();

        for (int t = 0; shown && t < compositor.assist_count; t++)
        {
                const int *r = compositor.assist_rect[t];
                int line = compositor_line_px(compositor_overlay_scale(compositor.assist_screen));
                boxes[n][0] = r[0] - line, boxes[n][1] = r[1] - line, boxes[n][2] = r[2] + 2 * line, boxes[n][3] = r[3] + 2 * line, n++;
        }
        if (shown && compositor.grid_on && compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
                const int *sc = screens[compositor.grid_screen];
                int line = compositor_line_px(compositor_overlay_scale(compositor.grid_screen));
                compositor_grid_tile(sc, 0, first);
                compositor_grid_tile(sc, COMPOSITOR_GRID_TILES - 1, last);
                boxes[n][0] = first[0] - line, boxes[n][1] = sc[1] - line;
                boxes[n][2] = last[0] + last[2] - first[0] + 2 * line, boxes[n][3] = first[3] + 2 * line, n++;
        }
        return n;
}

static void compositor_overlays_damage_of(int all)
{
        int boxes[COMPOSITOR_ASSIST_MAX + 1][4], n = compositor_overlay_boxes_of(boxes, all);

        for (int at = 0; at < n; at++)
                compositor_damage(boxes[at][0], boxes[at][1], boxes[at][2], boxes[at][3]);
}

static void compositor_overlays_damage(void) { compositor_overlays_damage_of(0); }
static void compositor_overlays_damage_all(void) { compositor_overlays_damage_of(1); }

static unsigned int compositor_ink_fade(unsigned int ink, unsigned int percent)
{
        return (ink & 0xffffffu) | ((ink >> 24) * percent / 100) << 24;
}

static void compositor_overlay_blank(struct compositor_frame *f)
{
        memory_zero(f, sizeof(*f));
        f->scale = compositor.scale;
}

static unsigned int compositor_overlays(struct compositor_frame *frames, unsigned int at)
{
        unsigned int count, added = 0, pct = compositor_overlay_alpha(), edge = compositor_snap_edge(pct);
        const int (*screens)[4] = compositor_screens(&count);
        int shown = compositor_overlays_shown();

        for (int t = 0; shown && t < compositor.assist_count; t++)
        {
                const int *r = compositor.assist_rect[t];
                int row = (int)compositor.assist_rows[t], sc = compositor_overlay_scale(compositor.assist_screen), line = compositor_line_px(sc);
                struct compositor_frame *pic = &frames[at + added], *veil = &frames[at + added + 1];

                compositor_frame_of(row, pic);
                pic->x = r[0], pic->y = r[1], pic->width = r[2], pic->height = r[3];
                pic->source_w = (int)compositor.w[row], pic->source_h = (int)compositor.h[row];
                pic->radius = UI_RADIUS * sc / 100, pic->line = line, pic->edge = edge, pic->close = pic->lit = 0;
                memory_zero(pic->preview, sizeof(pic->preview)), memory_zero(pic->mark, sizeof(pic->mark));
                compositor_overlay_blank(veil);
                if (pct < 100)
                {
                        veil->mark[0] = r[0] - line, veil->mark[1] = r[1] - line, veil->mark[2] = r[2] + 2 * line, veil->mark[3] = r[3] + 2 * line;
                        veil->mark_ink = (compositor_ground & 0xffffffu) | (100 - pct) * 255 / 100 << 24;
                }
                added += 2;
        }
        if (shown && compositor.grid_on && compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
                const int *sc = screens[compositor.grid_screen];
                int scale = compositor_overlay_scale(compositor.grid_screen), line = compositor_line_px(scale), rad = UI_RADIUS * scale / 100;
                unsigned int occ = compositor_occupied(compositor.drag, compositor.grid_screen);

                for (unsigned int tile = 0; tile < COMPOSITOR_GRID_TILES; tile++, added += 2)
                {
                        int box[4];
                        struct compositor_frame *tint = &frames[at + added], *border = &frames[at + added + 1];
                        unsigned int ink = tile == compositor.grid_hover ? compositor_snap_ink() : COMPOSITOR_GRID_TINT;

                        compositor_grid_tile(sc, tile, box);
                        compositor_overlay_blank(tint);
                        memory_copy(tint->mark, box, sizeof(box));
                        tint->mark_ink = compositor_ink_fade(ink, pct);
                        compositor_overlay_blank(border);
                        border->x = box[0], border->y = box[1], border->width = box[2], border->height = box[3];
                        border->radius = rad, border->line = line, border->edge = edge;
                }
                for (unsigned int tile = 0; tile < COMPOSITOR_GRID_TILES; tile++, added++)
                {
                        int part[4], tb[4], box[4];
                        unsigned int how = compositor_grid_regions[tile];
                        unsigned int ink = (compositor_region_cells(how) & occ) ? COMPOSITOR_GRID_TAKEN
                                           : tile == compositor.grid_hover      ? compositor_snap_ink()
                                                                                : COMPOSITOR_GRID_TINT;
                        compositor_grid_tile(sc, tile, tb);
                        compositor_screen_part(sc, how, part);
                        box[0] = tb[0] + (part[0] - sc[0]) * tb[2] / sc[2], box[1] = tb[1] + (part[1] - sc[1]) * tb[3] / sc[3];
                        box[2] = part[2] * tb[2] / sc[2], box[3] = part[3] * tb[3] / sc[3];
                        compositor_overlay_blank(&frames[at + added]);
                        memory_copy(frames[at + added].preview, box, sizeof(box));
                        frames[at + added].preview_ink = compositor_ink_fade(ink, pct);
                }
        }
        return added;
}

static int compositor_grid_rests(const int (*screens)[4], unsigned int count, int at, int x, int y)
{
        int point[4] = {x, y, 1, 1};

        return at >= 0 && compositor_grid_band(screens[at], x, y) && !(compositor_modifiers() & WINDOW_KEY_CONTROL) &&
               (compositor_snap_free(screens, count, point, at) & COMPOSITOR_SNAP_TOP);
}

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
                compositor.grid_on = 1, compositor.grid_screen = at, compositor.grid_hover = compositor_grid_under(screens[at], x, y);
        else
                compositor.grid_on = 0;
        compositor_overlays_damage();
}

static positive compositor_snap_due(void)
{
        return compositor.drag >= 0 && compositor.grid_since && !compositor.grid_on ? compositor.grid_since + COMPOSITOR_GRID_DWELL_NS : 0;
}

static void compositor_snap_tick(positive now)
{
        if (compositor_snap_due() && now >= compositor_snap_due())
                compositor_preview(compositor.drag);
}

static void compositor_assist_close(void)
{
        compositor_overlays_damage();
        compositor.assist_on = compositor.assist_armed = 0;
        if (compositor.grid_on)
                compositor.assist_count = 0;
}

/* ---- mode transitions, layers, float-for-drag ---- */

static void compositor_row_mode(int row, unsigned int arrange, int fullscreen, int screen)
{
        unsigned int count, target = fullscreen ? COMPOSITOR_MAXIMIZED : arrange, was;
        const int (*screens)[4];
        int part[4], on;
        unsigned char was_fs;

        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row])
                return;
        if (!fullscreen && arrange == COMPOSITOR_FLOATING)
        {
                int was_r[4], saved[4];

                if (compositor.arranged[row] == COMPOSITOR_FLOATING)
                {
                        compositor.fullscreen[row] = 0;
                        return;
                }
                compositor_osk_unpan(row);
                compositor_row_rect(row, was_r);
                compositor_row_saved_get(row, saved);
                compositor_damage_row(row);
                compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0, compositor.edges[row] = 0;
                compositor_put(row, saved[0], saved[1]);
                compositor_damage_row(row);
                compositor_want(row, saved[2], saved[3]);
                compositor_glide_start(row, was_r);
                compositor_osk_sync();
                return;
        }
        screens = compositor_screens(&count);
        on = screen >= 0 && (unsigned int)screen < count ? screen : compositor_screen_of(row);
        was_fs = compositor.fullscreen[row];
        was = was_fs ? compositor.fullscreen_was[row] : compositor.arranged[row];
        // Set fullscreen before arrange so compositor_row_clamp/stretch do not clamp fixed-size fullscreen windows.
        compositor.fullscreen[row] = (unsigned char)(fullscreen != 0);
        compositor_screen_part(screens[on], target, part);
        compositor_arrange_rect(row, target, part);
        if (fullscreen || compositor_region_cells(target))
                compositor_tile_others(row, target, on);
        if (fullscreen)
        {
                if (compositor.arranged[row] != COMPOSITOR_MAXIMIZED)
                {
                        compositor.fullscreen[row] = was_fs;
                        return;
                }
                compositor.fullscreen_was[row] = (unsigned char)was;
                compositor_raise(row);
                compositor_focus(row);
                if (!compositor_wl_locked())
                        compositor_motion();
        }
}

static void compositor_row_promote_toplevel(int row, int fullscreen_output)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (compositor.table)
                compositor.table->glass[row] &= ~WINDOW_NO_TAB, compositor.table->popup_parent[row] = 0;
        compositor.asks[row] &= ~WINDOW_NO_TAB, compositor_popup_parent[row] = -1;
        if (fullscreen_output >= 0)
                compositor_row_mode(row, COMPOSITOR_MAXIMIZED, 1, fullscreen_output);
        compositor_raise(row);
        compositor_focus(row);
        compositor_tabs_changed();
        if (!compositor_wl_locked())
                compositor_motion();
}

static void compositor_arrange_on(int row, unsigned int how, int on) { compositor_row_mode(row, how, 0, on); }
static void compositor_arrange(int row, unsigned int how) { compositor_row_mode(row, how, 0, -1); }
static void compositor_fullscreen_on(int row, int screen) { compositor_row_mode(row, COMPOSITOR_MAXIMIZED, 1, screen); }
static void compositor_fullscreen_row(int row) { compositor_row_mode(row, COMPOSITOR_MAXIMIZED, 1, -1); }
static void compositor_float(int row) { compositor_row_mode(row, COMPOSITOR_FLOATING, 0, -1); }

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

static void compositor_fullscreen_off(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.fullscreen[row])
                return;
        compositor_row_mode(row, compositor.fullscreen_was[row], 0, -1);
        compositor_damage_row(row);
}

static void compositor_fullscreen_toggle(int row)
{
        if (row >= 0 && row < COMPOSITOR_ROWS)
                compositor.fullscreen[row] ? compositor_fullscreen_off(row) : compositor_fullscreen_on(row, -1);
}

static void compositor_float_for_drag(int row)
{
        int width = (int)compositor.saved_w[row], height = (int)compositor.saved_h[row];
        int in_x, in_y, grab, at;
        struct compositor_frame drawn;

        compositor_osk_unpan(row);
        compositor_row_clamp(row, &width, &height);
        in_x = compositor.press_x - compositor.x[row], in_y = compositor.press_y - compositor.y[row];
        grab = compositor_fit((int)((long)in_x * width / (compositor.w[row] ? (int)compositor.w[row] : 1)), 0, width - 1);
        at = compositor_fit(in_y < COMPOSITOR_GRAB * compositor.scale / 100
                                ? in_y
                                : (int)((long)in_y * height / (compositor.h[row] ? (int)compositor.h[row] : 1)),
                            0, height - 1);
        compositor_frame_of(row, &drawn);
        if (compositor.glide[row])
                compositor_glide_damage_now(row);
        compositor.glide[row] = 0;
        compositor_damage_row(row);
        compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0, compositor.edges[row] = 0;
        compositor_put(row, compositor.pointer_x - grab, compositor.pointer_y - at);
        compositor_damage_row(row);
        compositor_want(row, width, height);
        compositor.drag_x = grab, compositor.drag_y = at;
        if (compositor_glides())
        {
                int from[4] = {drawn.x + compositor.pointer_x - compositor.press_x, drawn.y + compositor.pointer_y - compositor.press_y,
                               drawn.width, drawn.height};
                compositor_glide_begin(row, from);
                compositor.drag_glide = row + 1;
        }
}

/* ---- placement, room and saved places ---- */

static unsigned int compositor_center_screen(int row, const int (*screens)[4], unsigned int count)
{
        return place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2, compositor.y[row] + (int)compositor.h[row] / 2, screens, count);
}

static void compositor_first_place(int row, int *x, int *y)
{
        int step = COMPOSITOR_STEP * compositor.scale / 100;
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int on = compositor_screen_at(screens, count, *x, *y);
        const int *sc = screens[on < 0 ? 0 : on];
        int right = sc[0] + sc[2], bottom = sc[1] + sc[3];

        for (int at = 0; at < 8; at++)
        {
                int taken = 0, tx = *x + at * step, ty = *y + at * step, kx = tx, ky = ty;

                if (tx + (int)compositor.w[row] > right || ty + (int)compositor.h[row] > bottom)
                        break;
                compositor_screen_keep_window(screens, count, &kx, &ky, (int)compositor.w[row], (int)compositor.h[row],
                                              compositor_line(row), COMPOSITOR_GRAB * compositor.scale / 100);
                for (int o = 0; o < (int)compositor_rows_in_use; o++)
                        taken |= o != row && compositor.showing[o] &&
                                 ((compositor.x[o] == tx && compositor.y[o] == ty) || (compositor.x[o] == kx && compositor.y[o] == ky));
                if (!taken)
                {
                        *x = tx, *y = ty;
                        return;
                }
        }
}

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

static void compositor_room_follow(void)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int room[4];

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_ROOM)
                        continue;
                compositor_room(screens[compositor_center_screen(row, screens, count)], room);
                if (room[0] != compositor.x[row] || room[1] != compositor.y[row] || room[2] != (int)compositor.w[row] || room[3] != (int)compositor.h[row])
                        compositor_arrange_rect(row, COMPOSITOR_ROOM, room);
        }
}

static void compositor_place_read(int row, struct window_place *place)
{
        struct windows *table = compositor.table;
        unsigned int how = __atomic_load_n(&table->place[row], __ATOMIC_ACQUIRE);
        int x = table->place_x[row], y = table->place_y[row];
        unsigned int w = table->place_width[row], h = table->place_height[row], sc = table->place_screen[row];

        memory_zero(place, sizeof(*place));
        if ((how & WINDOW_PLACE_HOW) <= WINDOW_PLACE_FULLSCREEN &&
            ((how & WINDOW_PLACE_HOW) != WINDOW_PLACE_AT ||
             (x >= -WINDOW_PLACE_MOST && x <= WINDOW_PLACE_MOST && y >= -WINDOW_PLACE_MOST && y <= WINDOW_PLACE_MOST)))
                place->how = how & WINDOW_PLACE_HOW, place->x = x, place->y = y;
        place->how |= how & WINDOW_PLACE_FREE;
        if ((how & WINDOW_PLACE_SCREEN) && sc < WINDOW_PLACE_SCREENS)
                place->how |= WINDOW_PLACE_SCREEN, place->screen = sc;
        if ((how & WINDOW_PLACE_SIZE) && w && h && w <= WINDOW_PLACE_MOST && h <= WINDOW_PLACE_MOST)
                place->how |= WINDOW_PLACE_SIZE, place->width = w, place->height = h;
        for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                place->name[at] = ((volatile char *)table->name[row])[at];
        place->name[WINDOWS_NAME - 1] = 0;
        if (!window_name_valid(place->name))
                memory_zero(place->name, sizeof(place->name));
        memory_copy(compositor.name[row], place->name, WINDOWS_NAME);
}

static const char *compositor_places_path = SETTING_WINDOWS_PLACES_PATH;
static const char *compositor_remember_path = SETTING_WINDOWS_REMEMBER_PATH;

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

static void compositor_places_write(const struct places *places)
{
        static char text[PLACES_BYTES];
        unsigned long length = places_say(places, text, sizeof(text));

        (void)file_write_atomic_at(AT_FDCWD, (string_address)compositor_places_path, text, (positive)length, 0600);
}

static unsigned int place_kept_screen(const struct place_kept *kept, const char (*names)[WINDOWS_NAME],
                                      const int (*screens)[4], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
                if (!memory_compare(names[at], kept->screen, WINDOWS_NAME))
                        return at;
        return place_screen_at(kept->origin_x + kept->x + kept->width / 2, kept->origin_y + kept->y + kept->height / 2, screens, count);
}

static void compositor_screen_names(char (*names)[WINDOWS_NAME], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
        {
                const char *name = compositor_output_name(at);
                memory_zero(names[at], WINDOWS_NAME);
                for (unsigned int i = 0; name[i] && i < WINDOWS_NAME - 1; i++)
                        names[at][i] = name[i];
        }
}

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
        on = compositor_center_screen(row, screens, count);
        compositor_screen_names(names, count);
        memory_zero(&kept, sizeof(kept));
        memory_copy(kept.name, compositor.name[row], WINDOWS_NAME);
        memory_copy(kept.screen, names[on], WINDOWS_NAME);
        kept.origin_x = screens[on][0], kept.origin_y = screens[on][1];
        kept.x = compositor.x[row] - screens[on][0], kept.y = compositor.y[row] - screens[on][1];
        kept.width = (int)compositor.w[row], kept.height = (int)compositor.h[row];
        if (kept.x < 0 || kept.y < 0 || kept.x >= screens[on][2] || kept.y >= screens[on][3] || kept.width < 1 || kept.height < 1 ||
            kept.width > WINDOW_PLACE_MOST || kept.height > WINDOW_PLACE_MOST || !place_screen_name_valid(kept.screen))
                return;
        compositor_places_read(&places);
        if (places.count && !memory_compare(&places.kept[0], &kept, sizeof(kept)))
                return;
        places_note(&places, &kept);
        compositor_places_write(&places);
}

static int compositor_recall(int row, const int (*screens)[4], unsigned int count)
{
        char names[WINDOW_PLACE_SCREENS][WINDOWS_NAME];
        int lw = compositor_least(row, 0), lh = compositor_least(row, 1), found, x, y;
        unsigned int on, w, h;
        struct places places;

        if (!compositor.name[row][0] || !compositor_remembering())
                return 0;
        compositor_places_read(&places);
        if ((found = places_find(&places, compositor.name[row])) < 0)
                return 0;
        compositor_screen_names(names, count);
        on = place_kept_screen(&places.kept[found], names, screens, count);
        x = screens[on][0] + places.kept[found].x, y = screens[on][1] + places.kept[found].y;
        w = (unsigned int)place_size((unsigned int)places.kept[found].width, screens[on][2], lw);
        h = (unsigned int)place_size((unsigned int)places.kept[found].height, screens[on][3], lh);
        compositor_put(row, x, y);
        if (w != compositor.w[row] || h != compositor.h[row])
                compositor_want(row, (int)w, (int)h);
        compositor_first_place(row, &x, &y);
        place_clamp(screens[on], (int)compositor.w[row], (int)compositor.h[row], compositor_line(row), &x, &y);
        compositor_put(row, x, y);
        return 1;
}

static void compositor_free_place(int row, const struct window_place *place, const int (*screens)[4], unsigned int count, int *x, int *y)
{
        int owner = compositor_owner[row] - 1, room[4], rect[4];
        b32 owned = owner >= 0 && owner < (int)compositor_rows_in_use && owner != row && compositor.showing[owner];
        unsigned int on = (place->how & WINDOW_PLACE_SCREEN) && place->screen < count ? place->screen
                          : owned ? compositor_center_screen(owner, screens, count)
                                  : place_screen_at(compositor.pointer_x, compositor.pointer_y, screens, count);

        compositor_room(screens[on], room);
        if (owned)
                compositor_row_rect(owner, rect);
        place_free(room, owned ? rect : 0, (int)compositor.w[row], (int)compositor.h[row], compositor_line(row), x, y);
}

static void compositor_place_apply(int row, const struct window_place *place, const int *screen, const int *room, int x, int y, b32 is_move)
{
        int rect[4], lw = compositor_least(row, 0), lh = compositor_least(row, 1);
        unsigned int fills = place_rect(place, screen, room, x, y, compositor.w[row], compositor.h[row], compositor_line(row), lw, lh, rect);

        if (fills != COMPOSITOR_FLOATING)
        {
                unsigned char want_fs = !is_move && fills == COMPOSITOR_MAXIMIZED && (place->how & WINDOW_PLACE_HOW) == WINDOW_PLACE_FULLSCREEN;

                if (want_fs)
                {
                        unsigned int was = compositor.fullscreen[row] ? compositor.fullscreen_was[row] : compositor.arranged[row];
                        compositor.fullscreen[row] = 1, compositor.fullscreen_was[row] = (unsigned char)was;
                }
                compositor_arrange_rect(row, fills, rect);
                if (want_fs && compositor.arranged[row] != COMPOSITOR_MAXIMIZED)
                        compositor.fullscreen[row] = 0;
                return;
        }
        if (is_move)
        {
                place_clamp(screen, rect[2], rect[3], compositor_line(row), &rect[0], &rect[1]);
                compositor_damage_row(row);
        }
        compositor_put(row, rect[0], rect[1]);
        if (is_move)
                compositor_damage_row(row);
        if (rect[2] != (int)compositor.w[row] || rect[3] != (int)compositor.h[row])
                compositor_want(row, rect[2], rect[3]);
}

static void compositor_first_put(int row, int x, int y)
{
        int room[4];
        unsigned int count, on, how;
        const int (*screens)[4] = compositor_screens(&count);
        struct window_place place;

        compositor_place_read(row, &place);
        if (!(place.how & (WINDOW_PLACE_HOW | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE)) && compositor_recall(row, screens, count))
                return;
        how = place.how & WINDOW_PLACE_HOW;
        on = (place.how & WINDOW_PLACE_SCREEN) && place.screen < count ? place.screen : 0;
        compositor_room(screens[on], room);
        x += screens[on][0] - screens[0][0], y += screens[on][1] - screens[0][1];
        if (how == WINDOW_PLACE_AUTO || how == WINDOW_PLACE_MAXIMIZED || how == WINDOW_PLACE_FULLSCREEN)
        {
                if (place.how & WINDOW_PLACE_SIZE)
                {
                        compositor_put(row, x, y);
                        compositor_want(row, place_size(place.width, screens[on][2], compositor_least(row, 0)),
                                        place_size(place.height, screens[on][3], compositor_least(row, 1)));
                }
                if (place.how & WINDOW_PLACE_FREE)
                        compositor_free_place(row, &place, screens, count, &x, &y);
                compositor_place(row, x, y);
                if (how == WINDOW_PLACE_AUTO)
                        return;
        }
        compositor_place_apply(row, &place, screens[on], room, x, y, 0);
}

static void compositor_move(int row)
{
        int room[4];
        unsigned int count, from, on;
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
        from = compositor_center_screen(row, screens, count);
        on = (place.how & WINDOW_PLACE_SCREEN) && place.screen < count ? place.screen : from;
        compositor_room(screens[on], room);
        compositor_place_apply(row, &place, screens[on], room,
                               compositor.x[row] - screens[from][0] + screens[on][0],
                               compositor.y[row] - screens[from][1] + screens[on][1], 1);
}

static void compositor_reveal(int row);

/* ---- GPU window draw lists ---- */

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

#define COMPOSITOR_LIST_WAITS 4
static unsigned char *compositor_listed;
static unsigned char *compositor_list_waits;
static unsigned int compositor_list_invalids[WINDOW_LIST_REASONS];

static void compositor_list_count(struct windows *table, unsigned int why)
{
        table->list_why[why] = ++compositor_list_counts[why];
}

static int compositor_lists_take(int row, unsigned int width, unsigned int height, int *damage)
{
        struct windows *table = compositor.table;
        unsigned char *list = window_list_of(table, row);
        struct ui_draw_view view;
        unsigned int gen, invalid, seen_before;

        if (!list)
                return 0;
        gen = __atomic_load_n(&((unsigned int *)list)[UI_DRAW_LIST_GEN], __ATOMIC_ACQUIRE);
        if (gen == table->list_seen[row] || !__atomic_load_n(&table->list_mode, __ATOMIC_ACQUIRE))
        {
                if (compositor_listed[row] && __atomic_load_n(&table->list_mode, __ATOMIC_ACQUIRE))
                        compositor_list_count(table, COMPOSITOR_LIST_PIXELS);
                compositor_listed[row] = 0, table->list_seen[row] = gen;
                if (__atomic_load_n(&table->list_state[row], __ATOMIC_ACQUIRE) == 2)
                        __atomic_store_n(&table->list_state[row], 0u, __ATOMIC_RELEASE);
                return 0;
        }
        if ((gen & 1) || (invalid = window_list_check(list, (int)width, (int)height, &view)) == WINDOW_LIST_PUBLISHING)
                return -1;
        seen_before = table->list_seen[row];
        table->list_seen[row] = gen;
        if (invalid != WINDOW_LIST_VALID)
        {
                table->list_invalid[invalid] = ++compositor_list_invalids[invalid];
                if (invalid == WINDOW_LIST_SIZE)
                {
                        table->list_size_list[0] = view.width, table->list_size_list[1] = view.height;
                        table->list_size_taken[0] = width, table->list_size_taken[1] = height;
                }
                compositor_list_why = COMPOSITOR_LIST_INVALID;
                goto refused;
        }
        if (!compositor_listed[row] && (view.x0 > 0 || view.y0 > 0 || view.x1 < (int)width || view.y1 < (int)height))
        {
                compositor_list_why = COMPOSITOR_LIST_PARTIAL;
                goto refused;
        }
        compositor_list_why = COMPOSITOR_LIST_INVALID, compositor_list_busy = false;
        if (compositor_gpu_list_render(row, (const unsigned int *)list, compositor_buffer(row, 0), table->stride, (int)width, (int)height, damage) == 1)
        {
                compositor_list_waits[row] = 0, compositor_listed[row] = 1;
                compositor_list_count(table, COMPOSITOR_LIST_DRAWN);
                __atomic_store_n(&table->list_state[row], 1u, __ATOMIC_RELEASE);
                return 1;
        }
        if (compositor_list_why == COMPOSITOR_LIST_CARD && compositor_list_busy && compositor_list_waits[row] < COMPOSITOR_LIST_WAITS)
        {
                compositor_list_waits[row]++, table->list_seen[row] = seen_before;
                rows_add(&compositor.list_later, row);
                return -1;
        }
refused:
        compositor_list_waits[row] = 0, compositor_listed[row] = 0;
        compositor_list_count(table, compositor_list_why);
        damage[0] = damage[1] = damage[2] = damage[3] = 0;
        __atomic_store_n(&table->list_state[row], 2u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&table->list_refused, 1, __ATOMIC_RELEASE);
        return -2;
}

/* ---- row intake, identity and teardown ---- */

#define COMPOSITOR_OSK_NAME "osk"

static b32 compositor_osk_is(unsigned int pid)
{
        unsigned char mine[256], theirs[256];
        char path[40], comm[8];

        if (!pid)
                return 0;
        memory_zero(mine, sizeof(mine)), memory_zero(theirs, sizeof(theirs)), memory_zero(comm, sizeof(comm));
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive) "/proc/self/exe", 0, 0x7ff, (positive)mine, 0) < 0)
                return 0;
        windows_num_path(path, sizeof(path), "/proc/", pid, "/exe");
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)path, 0, 0x7ff, (positive)theirs, 0) < 0 ||
            memory_compare(theirs + 32, mine + 32, 8) || memory_compare(theirs + 136, mine + 136, 8))
                return 0;
        windows_num_path(path, sizeof(path), "/proc/", pid, "/comm");
        return windows_read_line(path, comm, sizeof(comm), FILE_READ | O_CLOEXEC) >= 0 &&
               string_equals((string_address)comm, (string_address)COMPOSITOR_OSK_NAME);
}

static void compositor_identity_take(int row)
{
        struct windows *table = compositor.table;
        unsigned int seq = __atomic_load_n(&table->icon_seq[row], __ATOMIC_ACQUIRE), changed = 0;
        char app_id[WINDOWS_APP_ID];

        memory_zero(app_id, sizeof(app_id));
        windows_app_id_read(table, (unsigned int)row, app_id);
        if (memory_compare(app_id, compositor.app_id[row], WINDOWS_APP_ID))
                memory_copy(compositor.app_id[row], app_id, WINDOWS_APP_ID), changed = 1;
        if (!seq && compositor.icon_seq[row])
                compositor.icon_seq[row] = 0, changed = 1;
        else if (seq && !(seq & 1u) && seq != compositor.icon_seq[row])
        {
                static unsigned int picture[WINDOWS_ICON * WINDOWS_ICON];
                unsigned int read = windows_icon_read(table, (unsigned int)row, picture);
                if (read)
                        memory_copy(compositor.icon[row], picture, sizeof(picture)), compositor.icon_seq[row] = read, changed = 1;
        }
        if (changed && compositor.showing[row])
                compositor_tabs_changed();
}

static void compositor_take_first(int row, unsigned int width, unsigned int height, unsigned int front)
{
        struct windows *table = compositor.table;
        int x = compositor_fit(table->x[row], -WINDOW_COORD_MAX, WINDOW_COORD_MAX);
        int y = compositor_fit(table->y[row], -WINDOW_COORD_MAX, WINDOW_COORD_MAX);

        compositor.w[row] = compositor.picture_w[row] = width;
        compositor.h[row] = compositor.picture_h[row] = height;
        compositor.asking[row] = 0, compositor.front[row] = front;
        compositor.state[row] = table->state[row] & WINDOW_MINIMIZED;
        compositor_close_count(row, 0);
        table->state[row] = compositor.state[row];
        compositor.arranged[row] = COMPOSITOR_FLOATING, compositor.fullscreen[row] = 0, compositor.edges[row] = 0;
        compositor.lifted[row] = table->lift[row], compositor.moved[row] = table->moved[row];
        compositor.glassy[row] = table->glass[row] & WINDOW_GLASS;
        compositor.asks[row] = table->glass[row] & (WINDOW_NO_CLOSE | WINDOW_DISMISS | WINDOW_NO_TAB | WINDOW_NO_GRAB | WINDOW_NO_KEYS | WINDOW_KEYBOARD);
        if ((compositor.asks[row] & (WINDOW_NO_KEYS | WINDOW_KEYBOARD)) && !compositor_osk_is(table->pid[row]))
                compositor.asks[row] &= ~(WINDOW_NO_KEYS | WINDOW_KEYBOARD);
        compositor.layer[row] = compositor_layer_clamp(table->layer[row]);
        if (compositor.layer[row] == WINDOW_LAYER_BACKGROUND)
                compositor.asks[row] |= WINDOW_NO_TAB;
        compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
        compositor.showing[row] = 1, compositor.made[row] = ++compositor.next_made;
        if (compositor_popup_first(row))
        {
                compositor_land(row);
                return;
        }
        compositor_first_put(row, x, y);
        if (compositor.layer[row] == WINDOW_LAYER_BACKGROUND)
                compositor_arrange_on(row, COMPOSITOR_MAXIMIZED, -1);
        compositor_raise(row);
        if (compositor.layer[row] != WINDOW_LAYER_BACKGROUND && compositor_visible(row) &&
            __atomic_load_n(&table->asked[row], __ATOMIC_ACQUIRE))
                compositor_focus(row);
        compositor_land(row);
        compositor_tabs_changed();
        if (compositor.asks[row] & WINDOW_KEYBOARD)
                compositor_osk_sync();
}

static void compositor_damage_clipped(int row, const int r[4], unsigned int width, unsigned int height)
{
        long x1 = r[0] < 0 ? 0 : r[0], y1 = r[1] < 0 ? 0 : r[1];
        long x2 = (long)r[0] + r[2], y2 = (long)r[1] + r[3];

        if (x2 > (long)width)
                x2 = (long)width;
        if (y2 > (long)height)
                y2 = (long)height;
        if (x2 > x1 && y2 > y1)
                compositor_damage(compositor.x[row] + (int)x1, compositor.y[row] + (int)y1, (int)(x2 - x1), (int)(y2 - y1));
}

static void compositor_take(int row)
{
        struct windows *table = compositor.table;
        unsigned int frame = __atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE), seen;
        unsigned int width = table->width[row], height = table->height[row], front = table->buffer_index[row] & 1u;
        int damage[4] = {table->damage_x[row], table->damage_y[row], table->damage_width[row], table->damage_height[row]}, listed;
        char title[WINDOWS_TITLE];
        b32 reclaimed, list_refused;

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
        if (compositor.showing[row] && table->lift[row] != compositor.lifted[row])
                compositor.lifted[row] = table->lift[row], compositor_reveal(row);
        if (compositor.showing[row] && __atomic_load_n(&table->moved[row], __ATOMIC_ACQUIRE) != compositor.moved[row])
                compositor.moved[row] = table->moved[row], compositor_move(row);
        if (frame == compositor.seen[row])
                return;
        compositor_frametrace_note('T', row, clock_monotonic_nanoseconds());
        seen = compositor.seen[row], reclaimed = rows_has(&compositor.reclaim, row);
        rows_del(&compositor.reclaim, row);
        compositor.seen[row] = frame;
        width = width > (unsigned int)compositor_most_width() ? (unsigned int)compositor_most_width() : width;
        height = height > (unsigned int)compositor_most_height() ? (unsigned int)compositor_most_height() : height;
        if ((listed = compositor_lists_take(row, width, height, damage)) == -1)
        {
                compositor.seen[row] = seen;
                if (reclaimed)
                        rows_add(&compositor.reclaim, row);
                return;
        }
        list_refused = listed == -2;
        if (listed == 1)
                front = 0, __atomic_store_n(&table->buffer_index[row], 0u, __ATOMIC_RELEASE);
        else if (list_refused && compositor.showing[row])
        {
                front = compositor.front[row] & 1u, width = compositor.picture_w[row], height = compositor.picture_h[row];
                __atomic_store_n(&table->buffer_index[row], front, __ATOMIC_RELEASE);
        }
        if (row == compositor.direct_row || compositor_card_reads(row))
                compositor.taken_held[row] = frame, rows_add(&compositor.held_rows, row);
        else
                __atomic_store_n(&table->shown[row], frame, __ATOMIC_RELEASE);
        rows_add(&compositor.woken, row);
        if (list_refused)
        {
                if (compositor.showing[row] && rows_has(&compositor.held_rows, row) && compositor_visible(row))
                        compositor_damage(compositor.x[row], compositor.y[row], 1, 1);
                return;
        }
        if (!width || !height)
                return;
        if (!compositor.showing[row])
        {
                compositor_take_first(row, width, height, front);
                return;
        }
        compositor.picture_w[row] = width, compositor.picture_h[row] = height;
        if (compositor.front[row] != front)
                compositor.patched[row][compositor.front[row] & 1u] = 0;
        compositor.front[row] = front;
        if (compositor.asking[row] && width == table->want_width[row] && height == table->want_height[row])
        {
                compositor.asking[row] = 0;
                if (compositor.resize != row && compositor.shared_row != row)
                        compositor.edges[row] = 0;
        }
        if (!compositor.asking[row] && (width != compositor.w[row] || height != compositor.h[row]))
        {
                compositor_damage_row(row);
                compositor_resize_place(row, width, height);
                damage[2] = 0;
                if (rows_has(&compositor.fit, row))
                {
                        rows_del(&compositor.fit, row);
                        if (clock_monotonic_nanoseconds() < compositor.fit_until)
                                compositor_fit_row(row);
                }
        }
        if (width != compositor.w[row] || height != compositor.h[row])
                compositor_stretch(row), damage[2] = 0;
        if (!compositor_visible(row))
                return;
        {
                long x1 = damage[0] < 0 ? 0 : damage[0], y1 = damage[1] < 0 ? 0 : damage[1];
                long x2 = (long)damage[0] + damage[2], y2 = (long)damage[1] + damage[3];
                const int (*part)[4] = 0;
                int parts = 0;

                if (x2 > (long)width)
                        x2 = (long)width;
                if (y2 > (long)height)
                        y2 = (long)height;
                compositor.damage_by = row + 1;
                if (damage[2] <= 0)
                        compositor_damage_row(row);
                else if (x2 > x1 && y2 > y1 && (parts = compositor_wl_parts(row, &part)) > 0)
                        for (int at = 0; at < parts; at++)
                                compositor_damage_clipped(row, part[at], width, height);
                else if (x2 > x1 && y2 > y1)
                        compositor_damage_clipped(row, damage, width, height);
                else if (rows_has(&compositor.held_rows, row))
                        compositor_damage(compositor_fit(compositor.x[row], 0, compositor.width - 1),
                                          compositor_fit(compositor.y[row], 0, compositor.height - 1), 1, 1);
                compositor.damage_by = 0;
        }
}

static void compositor_motion(void);
static void compositor_chip_hide(void);

static void compositor_grab_settle(void)
{
        int held = COMPOSITOR_NONE;

        for (unsigned int i = 0; i < 8; i++)
                if (compositor.button_down >> i & 1)
                {
                        if (compositor.button_row[i] == compositor.grab)
                                return;
                        if (held < 0)
                                held = compositor.button_row[i];
                }
        compositor.grab = held;
}

static void compositor_leave(int row)
{
        struct windows *table = compositor.table;
        int pointed = compositor.hover == row || compositor.cursor.asked_row == row;

        COMPOSITOR_ROW_HOOK_CALL(detach, row);
        if (compositor.showing[row])
                compositor_damage_row(row), compositor_tabs_changed();
        if (compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.row == row)
                compositor_chip_hide();
        compositor_osk_held[row] = 0;
        compositor_owed_drop(row);
        compositor_owner[row] = 0;
        for (int o = 0; o < (int)compositor_rows_in_use; o++)
                if (compositor_owner[o] == row + 1)
                        compositor_owner[o] = 0;
        compositor_stack_remove(row);
        if (compositor.assist_on && compositor.assist_row == row)
                compositor_assist_close();
        else if (compositor.assist_on)
                compositor_assist_refresh();
        if (compositor.keys_to == row)
                compositor.keys_to = COMPOSITOR_NONE, memory_zero(compositor.keys_down, sizeof(compositor.keys_down));
        if (compositor.hover == row)
                compositor.hover = COMPOSITOR_NONE;
        if (compositor.told_row == row)
                compositor.told_row = COMPOSITOR_NONE;
        compositor_grab_end(COMPOSITOR_GRAB_DROP, row);
        for (unsigned int i = 0; i < 8; i++)
                if (compositor.button_row[i] == row)
                        compositor.button_down &= (unsigned char)~(1u << i);
        compositor_grab_settle();
        if (compositor.cycle)
        {
                compositor.cycle = 0;
                if (compositor.focus >= 0 && compositor.focus != row)
                        compositor_reveal(compositor.focus);
        }
        compositor.showing[row] = compositor.seen[row] = compositor.state[row] = 0;
        compositor_close_count(row, 0);
        compositor.landing[row] = compositor.glide[row] = compositor.asking[row] = compositor.asks[row] = 0;
        compositor.w[row] = compositor.h[row] = compositor.edges[row] = compositor.picture_w[row] = compositor.picture_h[row] = 0;
        compositor.arranged[row] = compositor.fullscreen_was[row] = COMPOSITOR_FLOATING;
        compositor.fullscreen[row] = compositor.banded[row] = 0;
        compositor.layer[row] = WINDOW_LAYER_NORMAL;
        if (table)
                table->layer[row] = WINDOW_LAYER_NORMAL;
        compositor.saved_x[row] = compositor.saved_y[row] = 0, compositor.saved_w[row] = compositor.saved_h[row] = 0;
        compositor.title[row][0] = compositor.name[row][0] = 0, compositor.icon_seq[row] = 0;
        memory_zero(compositor.app_id[row], WINDOWS_APP_ID);
        compositor_glass_drop(row);
        compositor_runs_drop(row);
        compositor.glass_side[row] = compositor.glass_told[row] = 0;
        rows_del(&compositor.held_rows, row);
        if (compositor.direct_row == row)
                compositor.direct_row = -1, compositor_damage(0, 0, compositor.width, compositor.height);
        if (compositor.plane_row == row)
        {
                compositor.plane_row = -1, compositor.plane_gone = 1;
                compositor_damage(compositor.plane_rect[0], compositor.plane_rect[1],
                                  compositor.plane_rect[2] - compositor.plane_rect[0], compositor.plane_rect[3] - compositor.plane_rect[1]);
        }
        if (compositor_shows_row(row) || (compositor.flip_pending && compositor.flip_row == row))
                rows_add(&compositor.dropping, row);
        else if (compositor.scan_fb[row][0] || compositor.scan_fb[row][1])
                compositor_window_fb_drop(row);
        else
                __atomic_store_n(&table->pinned[row], compositor_gpu_pins[row] ? 1u : 0u, __ATOMIC_RELEASE);
        table->spoiled_height[row][0] = table->spoiled_height[row][1] = 0;
        table->ground[row] = table->hover_off[row] = table->pinch_wanted[row] = 0;
        if (compositor.touch.pinch_row == row)
                compositor.touch.pinch_row = COMPOSITOR_NONE;
        if (compositor.swipe.row == row)
                compositor.swipe.row = COMPOSITOR_NONE;
        if (compositor.swipe.armed_row == row)
                compositor.swipe.armed_row = COMPOSITOR_NONE, compositor.swipe.armed_until = 0;
        table->state[row] = table->want_width[row] = table->want_height[row] = table->shown[row] = 0;
        if (rows_has(&compositor.dropping, row) || compositor_gpu_pins[row])
                rows_add(&compositor.reclaim, row);
        else
        {
                positive base = (positive)compositor.table + compositor.pixels + row * 2 * compositor.buffer;
                gpu_forget((const void *)base, 2 * compositor.buffer);
                compositor_call(syscall(madvise), base, 2 * compositor.buffer, 9, 0, 0, 0);
        }
        compositor.osk_pan[row] = compositor.osk_height[row] = compositor.osk_edges[row] = 0;
        if (compositor.osk_turn_focus == row)
                compositor.osk_turn_focus = COMPOSITOR_NONE, compositor.osk_turned = compositor.osk_turn_blur = 0;
        compositor.caret[row] = 0, compositor.text_purpose[row] = 0;
        if (compositor.focus == row)
                compositor_refocus();
        rows_del(&compositor.woken, row);
        __atomic_store_n(&table->pid[row], 0, __ATOMIC_RELEASE);
        compositor_osk_sync();
        if (pointed)
                compositor_motion();
}

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
        if (!compositor_wl_locked())
                compositor_motion();
}

static void compositor_focus_step(void)
{
        int top = (int)compositor.stacked, focused = 0, below, next;

        for (int at = 0; at < top; at++)
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

static void compositor_minimize_row(int row)
{
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

/* ---- on-screen keyboard ---- */

#define COMPOSITOR_OSK_AUTO 0
#define COMPOSITOR_OSK_OFF 1
#define COMPOSITOR_OSK_ON 2

static int compositor_osk_row(void)
{
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if (compositor.showing[row] && (compositor.asks[row] & WINDOW_KEYBOARD))
                        return (int)row;
        return -1;
}

static b32 compositor_input_has(b32 touch)
{
        for (unsigned int at = 0; at < compositor.input_count; at++)
                if (compositor.inputs[at] >= 0 && (touch ? compositor.touchscreen[at] : (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD) != 0))
                        return 1;
        return 0;
}

static void compositor_osk_pan(int row, int keyboard)
{
        int x, y, w, h, overlap, room, amount, was;

        if (compositor.osk_pan[row] || compositor.osk_height[row])
                return;
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                int above = compositor.y[keyboard] - compositor.y[row];
                if (above < compositor_least(row, 1) || above >= (int)compositor.h[row])
                        return;
                compositor.osk_height[row] = (int)compositor.h[row];
                compositor.osk_edges[row] = compositor.edges[row], compositor.edges[row] = 0;
                compositor_want(row, (int)compositor.w[row], above);
                return;
        }
        compositor_output_rect(compositor_screen_of(row), &x, &y, &w, &h);
        overlap = compositor.y[row] + (compositor.caret[row] > 0 && compositor.caret[row] < (int)compositor.h[row] ? compositor.caret[row] : (int)compositor.h[row]) -
                  compositor.y[keyboard];
        room = compositor.y[row] - y, amount = overlap < room ? overlap : room;
        if (amount <= 0)
                return;
        was = compositor.y[row];
        compositor_place(row, compositor.x[row], was - amount);
        compositor.osk_pan[row] = was - compositor.y[row];
}

static void compositor_osk_unpan(int row)
{
        int amount = compositor.osk_pan[row], height = compositor.osk_height[row];

        compositor.osk_pan[row] = compositor.osk_height[row] = 0;
        if (height)
        {
                compositor.edges[row] = compositor.osk_edges[row];
                if (compositor.showing[row])
                        compositor_want(row, (int)compositor.w[row], height);
        }
        if (amount && compositor.showing[row])
                compositor_place(row, compositor.x[row], compositor.y[row] + amount);
}

static void compositor_osk_sync(void)
{
        struct windows *table = compositor.table;
        int kb = compositor_osk_row(), focus = compositor.focus, want = 0;
        b32 touch = !compositor.osk_typed && compositor.owner != COMPOSITOR_PREDICT_MOUSE, kb_plugged = compositor_input_has(0);
        unsigned int purpose;

        if (compositor.osk_turned)
        {
                if (focus != compositor.osk_turn_focus)
                        compositor.osk_turned = 0;
                else if (focus >= 0 && !table->field[focus])
                        compositor.osk_turn_blur = 1;
                else if (focus >= 0 && compositor.osk_turn_blur)
                        compositor.osk_turned = 0;
        }
        if (kb >= 0 && focus >= 0 && focus != kb && table->field[focus] && compositor.osk_setting != COMPOSITOR_OSK_OFF && !compositor.osk_turned)
                want = compositor.osk_setting == COMPOSITOR_OSK_ON || touch || !kb_plugged;
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if ((compositor.osk_pan[row] || compositor.osk_height[row]) && (!want || (int)row != focus))
                        compositor_osk_unpan((int)row);
        if (kb >= 0)
        {
                b32 away = !want;
                if (((compositor.state[kb] & WINDOW_MINIMIZED) != 0) != away)
                {
                        compositor_damage_row(kb);
                        if (away)
                                compositor.state[kb] |= WINDOW_MINIMIZED;
                        else
                                compositor.state[kb] &= ~WINDOW_MINIMIZED, compositor_raise(kb);
                        table->state[kb] = compositor.state[kb];
                        compositor_damage_row(kb);
                        rows_add(&compositor.woken, kb);
                }
                if (want)
                        compositor_osk_pan(focus, kb);
        }
        compositor.osk_shown = (unsigned char)(kb >= 0 && want);
        table->osk_setting = compositor.osk_setting, table->osk_shown = compositor.osk_shown;
        table->osk_input = compositor.osk_typed ? 2 : compositor.owner != COMPOSITOR_PREDICT_MOUSE ? 1 : 0;
        table->osk_physical = kb_plugged;
        table->osk_layout = (unsigned int)(unsigned char)compositor_layouts[compositor.keymap.layout].name[0] |
                            (unsigned int)(unsigned char)compositor_layouts[compositor.keymap.layout].name[1] << 8;
        table->osk_start = compositor.osk_setting == COMPOSITOR_OSK_ON ||
                           (compositor.osk_setting == COMPOSITOR_OSK_AUTO && (compositor_input_has(1) || !kb_plugged));
        purpose = focus >= 0 && table->field[focus] ? compositor.text_purpose[focus] : 0;
        if (table->osk_purpose != purpose && kb >= 0)
                rows_add(&compositor.woken, kb);
        table->osk_purpose = purpose;
        table->osk_caret = focus >= 0 && table->field[focus] && compositor.caret[focus] > 0 ? (unsigned int)compositor.caret[focus] : 0;
}

static void compositor_osk_turned(void)
{
        compositor.osk_turned = 1, compositor.osk_turn_blur = 0, compositor.osk_turn_focus = compositor.focus;
        compositor_osk_sync();
}

static void compositor_osk_setting_read(void)
{
        char word[16];

        compositor_word(SETTING_ONSCREEN_PATH, word, sizeof(word));
        compositor.osk_setting = string_equals((string_address)word, (string_address) "off") ? COMPOSITOR_OSK_OFF
                                 : string_equals((string_address)word, (string_address) "on") ? COMPOSITOR_OSK_ON : COMPOSITOR_OSK_AUTO;
        compositor_osk_sync();
}

static void compositor_close(int row)
{
        if (row < 0 || !compositor.showing[row] || (compositor_popup_parent[row] >= 0 && (compositor.state[row] & WINDOW_CLOSING)))
                return;
        compositor_close_count(row, compositor.close_asks[row] == 255 ? 1u : compositor.close_asks[row] + 1u);
        compositor.state[row] |= WINDOW_CLOSING;
        compositor.table->state[row] = compositor.state[row];
        rows_add(&compositor.woken, row);
}

/* ---- clipboard and GPU toggle ---- */

#define COMPOSITOR_CLIP_GESTURE_NS 1500000000ull
#define COMPOSITOR_CLIP_HISTORY 32
#define COMPOSITOR_F_DUPFD_CLOEXEC 1030
#define COMPOSITOR_CLIP_SEALS 14
#define COMPOSITOR_CLIP_OBSERVERS_MAX 8

static struct
{
        int door, fd[3], blank;
        unsigned int *offer_seen, *ask_seen, cli_seen, history_most, history_count;
        positive *gesture;
        int history[COMPOSITOR_CLIP_HISTORY];
        const unsigned char *map[3];
} compositor_clip = {.door = -1, .fd = {-1, -1, -1}, .blank = -1};

static void (*compositor_clip_observers[COMPOSITOR_CLIP_OBSERVERS_MAX])(unsigned int which);
static unsigned int compositor_clip_observer_count;

static void compositor_clip_slot_map(unsigned int which, unsigned long size)
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

static void compositor_clip_slot_install(unsigned int which, int opened, unsigned long length)
{
        compositor_clip_slot_map(which, 0);
        compositor_call(syscall(dup3), (positive)opened, WINDOWS_CLIP_FD + which, O_CLOEXEC, 0, 0, 0);
        system_close(opened);
        compositor_clip.fd[which] = WINDOWS_CLIP_FD + (int)which;
        compositor_clip_slot_map(which, length);
        compositor.table->clip_length[which] = length;
        __atomic_add_fetch(&compositor.table->clip_serial[which], 1, __ATOMIC_RELEASE);
}

static void compositor_clip_slot_clear(unsigned int which)
{
        if (compositor_clip.fd[which] >= 0 &&
            (compositor_clip.blank < 0 ||
             compositor_call(syscall(dup3), (positive)compositor_clip.blank, (positive)compositor_clip.fd[which], O_CLOEXEC, 0, 0, 0) < 0))
                system_close(compositor_clip.fd[which]);
        compositor_clip_slot_map(which, 0);
        compositor_clip.fd[which] = -1;
        compositor.table->clip_length[which] = 0;
        __atomic_add_fetch(&compositor.table->clip_serial[which], 1, __ATOMIC_RELEASE);
}

static void compositor_clip_reserve(void)
{
        if (compositor_clip.blank < 0)
                compositor_clip.blank = (int)system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ | O_CLOEXEC);
        for (unsigned int which = 0; which < 3 && compositor_clip.blank >= 0; which++)
                if (compositor_clip.fd[which] < 0)
                        compositor_call(syscall(dup3), (positive)compositor_clip.blank, WINDOWS_CLIP_FD + which, O_CLOEXEC, 0, 0, 0);
}

static void compositor_clip_gesture(int row)
{
        positive now = clock_monotonic_nanoseconds();
        int root = row >= 0 && row < COMPOSITOR_ROWS ? compositor_popup_root(row) : -1;

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

static void compositor_clip_observer_register(void (*fn)(unsigned int which))
{
        if (!fn || compositor_clip_observer_count >= COMPOSITOR_CLIP_OBSERVERS_MAX)
                return;
        for (unsigned int i = 0; i < compositor_clip_observer_count; i++)
                if (compositor_clip_observers[i] == fn)
                        return;
        compositor_clip_observers[compositor_clip_observer_count++] = fn;
}

static void compositor_clip_broadcast(unsigned int which, int skip_owner)
{
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.table->clip_watch[row] && row != skip_owner)
                        compositor_clip_tell(row, which, WINDOWS_CLIP_CHANGED);
        for (unsigned int i = 0; i < compositor_clip_observer_count; i++)
                compositor_clip_observers[i](which);
}

static void compositor_clip_clear(unsigned int which)
{
        compositor_clip_slot_clear(which);
        compositor.table->clip_owner[which] = -1;
        compositor_clip_broadcast(which, -1);
}

static void compositor_clip_wipe(void)
{
        for (unsigned int which = 0; which < 2; which++)
                compositor_clip_clear(which);
        while (compositor_clip.history_count)
                system_close(compositor_clip.history[--compositor_clip.history_count]);
}

#ifndef SHELL_NO_GPU
static int compositor_gpu = 0;

static void compositor_gpu_settings(void)
{
        char word[16];
#ifdef MOONWATER_GPU_C
        int was = compositor_gpu;
#endif
        compositor_word(SETTING_GPU_PATH, word, sizeof(word));
        compositor_gpu = !word[0] || string_equals((string_address)word, (string_address) "on") || string_equals((string_address)word, (string_address) "default");
#ifdef MOONWATER_GPU_C
        if (compositor_gpu && !was)
        {
                if (!compositor_gpu_state.hung_off && (compositor_gpu_state.up || compositor_gpu_state.dead_why != COMPOSITOR_GPU_WHY_UNTRUSTED))
                        compositor_gpu_state.dead = 0;
                compositor_gpu_state.settled = compositor_gpu_state.said = 0;
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
        compositor_clip.history_most = compositor_setting_kept(SETTING_CLIPBOARD_HISTORY_PATH, word, &value) && value >= 0 && value <= COMPOSITOR_CLIP_HISTORY
                                           ? (unsigned int)value
                                           : 0;
        if (!compositor.table->clip_on)
                compositor_clip_wipe();
        while (compositor_clip.history_count > compositor_clip.history_most)
                system_close(compositor_clip.history[--compositor_clip.history_count]);
}

static int compositor_clip_open(unsigned int pid, int fd, unsigned long address_to length)
{
        char path[48];
        unsigned char info[256];
        long held, opened = -1, size, seals, mapped;
        unsigned int mode;
        int ok;

        if (!pid || fd < 0)
                return -1;
        // Take via O_PATH first and statx AT_EMPTY_PATH so non-regular fds (pipes/ttys/devices) are never opened.
        windows_proc_fd(path, pid, (unsigned int)fd);
        if ((held = (long)system_open_at(AT_FDCWD, (string_address)path, O_PATH | O_CLOEXEC)) < 0)
                return -1;
        memory_zero(info, sizeof(info));
        mode = compositor_call(syscall(statx), (positive)held, (positive) "", 0x1000, 1, (positive)info, 0) < 0
                   ? 0
                   : (unsigned int)info[28] | (unsigned int)info[29] << 8;
        if ((mode & 0170000) == 0100000)
        {
                windows_proc_fd(path, (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (unsigned int)held);
                opened = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        }
        system_close((bipolar)held);
        if (opened < 0)
                return -1;
        seals = compositor_call(syscall(fcntl), (positive)opened, WINDOWS_F_GET_SEALS, 0, 0, 0, 0);
        size = compositor_call(syscall(lseek), (positive)opened, 0, 2, 0, 0, 0);
        if (seals < 0 || (seals & COMPOSITOR_CLIP_SEALS) != COMPOSITOR_CLIP_SEALS || size <= 0 || (unsigned long)size > WINDOWS_CLIP_MOST)
                goto fail;
        mapped = compositor_call(syscall(mmap), 0, (positive)size, 1, 2, (positive)opened, 0);
        ok = !(mapped < 0 && mapped > -4096) && windows_clip_valid((const unsigned char *)mapped, (unsigned long)size);
        if (!(mapped < 0 && mapped > -4096))
                compositor_call(syscall(munmap), (positive)mapped, (positive)size, 0, 0, 0, 0);
        if (!ok)
                goto fail;
        *length = (unsigned long)size;
        return (int)opened;
fail:
        system_close((bipolar)opened);
        return -1;
}

static void compositor_clip_keep(unsigned int which, int opened, unsigned long length, int owner)
{
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
                        compositor_clip.history[0] = (int)kept, compositor_clip.history_count++;
                }
        }
        compositor_clip_slot_install(which, opened, length);
        compositor.table->clip_owner[which] = owner;
        compositor_clip_broadcast(which, owner);
}

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

                if ((compositor_osk_held[row] || now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS) && target >= 0 && target != row)
                        compositor_post(target, WINDOW_EVENT_KEY,
                                        table->osk_flags[row][at] & (WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT |
                                                                     WINDOW_KEY_ALTGR | WINDOW_KEY_SUPER | WINDOW_KEY_REPEAT),
                                        table->osk_code[row][at], table->osk_character[row][at]);
                __atomic_store_n(&table->osk_tail[row], table->osk_tail[row] + 1, __ATOMIC_RELEASE);
        }
}

/* ---- protocol-neutral drag-and-drop ---- */

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
        int active, source, target, dropped, named, place;
        long take;
        unsigned int source_actions, take_actions, take_preferred, action, told_action, bridge_count;
        const struct compositor_drag_ops *ext_bridge, *target_bridge, *bridges[COMPOSITOR_DND_BRIDGES];
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

static void compositor_dnd_reset(int active, int source, unsigned int actions, const struct compositor_drag_ops *ext)
{
        compositor_dnd.active = active, compositor_dnd.source = source, compositor_dnd.target = -1, compositor_dnd.take = -1;
        compositor_dnd.source_actions = actions, compositor_dnd.action = compositor_dnd.told_action = 0;
        compositor_dnd.dropped = compositor_dnd.named = 0;
        compositor_dnd.ext_bridge = ext, compositor_dnd.target_bridge = 0;
}

static void compositor_dnd_tell(int row, int what, int word)
{
        unsigned int keys = what == WINDOWS_DRAG_ENTER || what == WINDOWS_DRAG_MOTION || what == WINDOWS_DRAG_DROP
                                ? compositor_modifiers() & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)
                                : 0;

        if (row >= 0 && row < COMPOSITOR_ROWS && compositor.table && compositor.table->pid[row])
                compositor_post(row, WINDOW_EVENT_DRAG, keys, what, word);
}

static int compositor_dnd_place(int x, int y) { return (int)(((unsigned int)(y & 0xffff) << 16) | (unsigned int)(x & 0xffff)); }

static const char *compositor_dnd_name(long nth)
{
        unsigned long length;

        if (nth < 0 || !compositor_clip.map[WINDOWS_CLIP_DRAG] || !compositor.table)
                return 0;
        return windows_clip_offered(compositor_clip.map[WINDOWS_CLIP_DRAG], compositor.table->clip_length[WINDOWS_CLIP_DRAG],
                                    (unsigned long)nth, 0, &length);
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
        if (compositor_dnd.source >= 0 && compositor_dnd.action != compositor_dnd.told_action)
        {
                compositor_dnd.told_action = compositor_dnd.action;
                compositor_dnd_tell(compositor_dnd.source, WINDOWS_DRAG_ACTION, (int)compositor_dnd.action);
        }
        return (int)compositor_dnd.action;
}

static int compositor_dnd_shape(void)
{
        int action = compositor_dnd_action();
        return action < 0 ? -1 : action ? CP_HAND : CP_NOTALLOWED;
}

static void compositor_dnd_leave_target(void)
{
        if (compositor_dnd.target_bridge && compositor_dnd.target_bridge->target_event)
                compositor_dnd.target_bridge->target_event(compositor_dnd.target_bridge->ctx, COMPOSITOR_DND_LEAVE, compositor_dnd.target, 0, 0);
        else if (compositor_dnd.target >= 0)
                compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_LEAVE, 0);
        compositor_dnd.target_bridge = 0, compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
}

static void compositor_dnd_cancel_bridges(const struct compositor_drag_ops *skip)
{
        for (unsigned int i = 0; i < compositor_dnd.bridge_count; i++)
                if (compositor_dnd.bridges[i] != skip && compositor_dnd.bridges[i]->cancel &&
                    compositor_dnd.bridges[i]->active && compositor_dnd.bridges[i]->active(compositor_dnd.bridges[i]->ctx))
                        compositor_dnd.bridges[i]->cancel(compositor_dnd.bridges[i]->ctx);
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
        if (!(parts = count ? (struct windows_clip_part *)compositor_map_zeroed((unsigned long)count * sizeof(*parts)) : 0))
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
                compositor_clip_slot_install(WINDOWS_CLIP_DRAG, (int)opened, length), compositor_dnd.named = 1;
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
                compositor_dnd_tell(source, WINDOWS_DRAG_ACTION, (int)action), compositor_dnd_tell(source, WINDOWS_DRAG_ENDED, ok);
        if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->finished && !ok)
                compositor_dnd.ext_bridge->finished(compositor_dnd.ext_bridge->ctx, 0);
        compositor_dnd_reset(0, -1, 0, 0);
        if (source >= 0)
                compositor_dnd_cancel_bridges(0);
        if (compositor_clip.fd[WINDOWS_CLIP_DRAG] >= 0)
                compositor_clip_slot_clear(WINDOWS_CLIP_DRAG);
        compositor_chip_drag(-1);
}

static void compositor_dnd_motion(int under)
{
        struct windows *table = compositor.table;
        const struct compositor_drag_ops *nb = 0;
        int wx = 0, wy = 0;

        if (!compositor_dnd.active)
                goto done;
        if (compositor_dnd.dropped)
                return;
        if (under >= 0 && under < COMPOSITOR_ROWS)
        {
                compositor_window_point(under, compositor.pointer_x, compositor.pointer_y, &wx, &wy);
                for (unsigned int i = 0; i < compositor_dnd.bridge_count && !nb; i++)
                        if (compositor_dnd.bridges[i]->target_owns && compositor_dnd.bridges[i]->target_owns(compositor_dnd.bridges[i]->ctx, under))
                                nb = compositor_dnd.bridges[i];
        }
        if (nb)
        {
                if (compositor_dnd.active == 2 && nb == compositor_dnd.ext_bridge)
                {
                        compositor_dnd_leave_target();
                        goto done;
                }
                if (compositor_dnd.target != under || compositor_dnd.target_bridge != nb)
                {
                        compositor_dnd_leave_target();
                        compositor_dnd.target = under, compositor_dnd.target_bridge = nb;
                        if (nb->target_event)
                                compositor_dnd.action = (unsigned int)nb->target_event(nb->ctx, COMPOSITOR_DND_ENTER, under, wx, wy);
                }
                else if (nb->target_event)
                        compositor_dnd.action = (unsigned int)nb->target_event(nb->ctx, COMPOSITOR_DND_MOTION, under, wx, wy);
                goto done;
        }
        if (compositor_dnd.target_bridge)
                compositor_dnd_leave_target();
        if (under < 0 || under >= COMPOSITOR_ROWS || !table || !table->drop_ok[under] || !table->pid[under])
        {
                compositor_dnd_leave_target();
                if (compositor_dnd.ext_bridge && compositor_dnd.ext_bridge->status)
                        compositor_dnd.ext_bridge->status(compositor_dnd.ext_bridge->ctx, 0, 0);
                goto done;
        }
        compositor_dnd_name_ext_types();
        compositor_dnd.place = compositor_dnd_place(wx, wy);
        if (under != compositor_dnd.target)
        {
                compositor_dnd_leave_target();
                compositor_dnd.target = under;
                compositor_dnd_tell(under, WINDOWS_DRAG_ENTER, compositor_dnd.place);
        }
        else
                compositor_dnd_tell(under, WINDOWS_DRAG_MOTION, compositor_dnd.place);
done:
        compositor_chip_drag(compositor_dnd_action());
}

static void compositor_dnd_offer(int row)
{
        struct windows *table = compositor.table;
        unsigned long length = 0;
        int opened = -1, tab = 0, under, held = 0;
        unsigned int actions;

        for (int b = 0; b < 8; b++)
                held |= (compositor.button_down & (1u << b)) && compositor.button_row[b] == row;
        if (!table || compositor_dnd_active() || !held ||
            (opened = compositor_clip_open(table->pid[row], table->drag_offer_fd[row], &length)) < 0)
        {
                compositor_dnd_tell(row, WINDOWS_DRAG_DECLINED, 0);
                return;
        }
        actions = table->drag_offer_actions[row] & 7u;
        compositor_clip_slot_install(WINDOWS_CLIP_DRAG, opened, length);
        if (!actions || !compositor_dnd_name(0))
        {
                compositor_clip_slot_clear(WINDOWS_CLIP_DRAG);
                compositor_dnd_tell(row, WINDOWS_DRAG_DECLINED, 0);
                return;
        }
        compositor_dnd_reset(1, row, actions, 0);
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
                        memory_copy(types_buf + bytes, name, (positive)n), bytes += n;
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
        action = (both & compositor_dnd.take_preferred) ? compositor_dnd.take_preferred : (both & (~both + 1u));
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
                        compositor_dnd.dropped = 1;
                else
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
                compositor_dnd.offer_seen[row] = offer, compositor_dnd_offer(row);
        if (take != compositor_dnd.take_seen[row])
                compositor_dnd.take_seen[row] = take, compositor_dnd_take(row);
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
                compositor_dnd_cancel_bridges(compositor_dnd.ext_bridge);
                compositor_dnd_end(0);
        }
        return 1;
}

static void compositor_dnd_keys(void)
{
        if (compositor_dnd.active && compositor_dnd.target >= 0 && !compositor_dnd.target_bridge && !compositor_dnd.dropped)
                compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_MOTION, compositor_dnd.place);
}

static int compositor_dnd_ext_begin(const struct compositor_drag_ops *bridge, unsigned int actions)
{
        if (!bridge || compositor_dnd.active)
                return 0;
        compositor_dnd_reset(2, -1, actions & 7u, bridge);
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
        compositor_clip_slot_install(WINDOWS_CLIP_DRAG, opened, length);
        compositor_dnd_ready();
}

/* ---- clipboard access policy, door, CLI and lifecycle ---- */

static b32 compositor_clip_may_access(int row, positive now)
{
        int root;

        if (!compositor.table || !compositor.table->clip_on || row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        root = compositor_popup_root(row);
        return root >= 0 && root == compositor.focus && now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS;
}

static b32 compositor_clip_set_memfd(unsigned int which, int fd, int owner)
{
        unsigned long length = 0;
        int opened;

        if (!compositor.table || which > 1 || fd < 0 || (opened = compositor_clip_open(compositor.table->clip_pid, fd, &length)) < 0)
                return 0;
        compositor_clip_keep(which, opened, length, owner);
        return 1;
}

static int compositor_clip_take_offer(b32 allowed, unsigned int pid, int fd, unsigned int which, int owner, unsigned int serial, unsigned int *answer)
{
        unsigned long length = 0;
        int opened = allowed ? compositor_clip_open(pid, fd, &length) : -1;

        if (opened >= 0)
                compositor_clip_keep(which, opened, length, owner);
        __atomic_store_n(answer, opened >= 0 ? serial : serial | WINDOWS_CLIP_NO, __ATOMIC_RELEASE);
        return opened >= 0;
}

static void compositor_clip_door(int row)
{
        struct windows address_to table = compositor.table;
        unsigned int offer, ask;
        positive now;

        compositor_dnd_door(row);
        offer = __atomic_load_n(&table->clip_offer_serial[row], __ATOMIC_ACQUIRE);
        ask = __atomic_load_n(&table->clip_ask_serial[row], __ATOMIC_ACQUIRE);
        compositor_osk_read(row);
        compositor_osk_sync();
        if (offer == compositor_clip.offer_seen[row] && ask == compositor_clip.ask_seen[row])
                return;
        now = clock_monotonic_nanoseconds();
        if (offer != compositor_clip.offer_seen[row])
        {
                unsigned int which = table->clip_offer_which[row] & 1u;
                int ok;

                compositor_clip.offer_seen[row] = offer;
                ok = compositor_clip_take_offer(table->clip_on && now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS,
                                                table->pid[row], table->clip_offer_fd[row], which, row, offer, &table->clip_offer_answer[row]);
                compositor_clip_tell(row, which, ok ? WINDOWS_CLIP_TAKEN : WINDOWS_CLIP_DECLINED);
        }
        if (ask != compositor_clip.ask_seen[row])
        {
                unsigned int which = table->clip_ask_which[row] & 1u;
                b32 may = compositor_clip.fd[which] >= 0 && compositor_clip_may_access(row, now);

                compositor_clip.ask_seen[row] = ask;
                if (may)
                        table->clip_given_length[row] = table->clip_length[which], __atomic_store_n(&table->clip_given_serial[row], ask, __ATOMIC_RELEASE);
                compositor_clip_tell(row, which, may ? WINDOWS_CLIP_READY : WINDOWS_CLIP_REFUSED);
        }
}

static void compositor_clip_cli_read(void)
{
        struct windows address_to table = compositor.table;
        unsigned int serial;

        if (table->cli_clear)
        {
                table->cli_clear == 2 ? compositor_clip_settings() : compositor_clip_wipe();
                __atomic_store_n(&table->cli_clear, 0u, __ATOMIC_RELEASE);
        }
        if ((serial = __atomic_load_n(&table->cli_serial, __ATOMIC_ACQUIRE)) != compositor_clip.cli_seen)
        {
                compositor_clip.cli_seen = serial;
                compositor_clip_take_offer(table->clip_on, table->cli_pid, (int)table->cli_fd, table->cli_which & 1u, -2, serial, &table->cli_answer);
        }
}

static void compositor_clip_cli(void)
{
        if (compositor_door_ask(COMPOSITOR_DOOR_MISC, COMPOSITOR_DOOR_DRAIN))
                rows_add(&compositor_doors.busy, COMPOSITOR_DOOR_MISC);
        else
                compositor_door_drain(compositor_clip.door);
        compositor_clip_cli_read();
}

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

static void compositor_clip_leave(int row)
{
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.table->clip_owner[which] == row)
                        compositor.table->clip_owner[which] = -1;
        compositor_clip.gesture[row] = 0;
        compositor_clip.offer_seen[row] = compositor.table->clip_offer_serial[row];
        compositor_clip.ask_seen[row] = compositor.table->clip_ask_serial[row];
        if (compositor_dnd.active)
        {
                if (row == compositor_dnd.source)
                        compositor_dnd_end(0);
                else if (row == compositor_dnd.target && !compositor_dnd.dropped)
                        compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
        }
}

/* ---- popups ---- */

static void compositor_popup_place(int ax, int ay, int aw, int ah, int width, int height, unsigned int flags, const int *screen,
                                   int *x, int *y)
{
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

static int compositor_popup_root(int row)
{
        for (int hops = 0; hops < (int)compositor_rows_in_use && row >= 0 && compositor_popup_parent[row] >= 0; hops++)
                row = compositor_popup_parent[row];
        return row;
}

static b32 compositor_popup_in_chain(int link, int target)
{
        for (int hops = 0; link >= 0 && hops < (int)compositor_rows_in_use; link = compositor_popup_parent[link], hops++)
                if (link == target)
                        return true;
        return false;
}

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

static b32 compositor_popups_close(int root, int keep)
{
        b32 asked = false;

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                if (compositor_popup_parent[row] < 0 || !compositor.showing[row] || compositor_row_xoverride(row))
                        continue;
                if ((root >= 0 && compositor_popup_root(row) != root) || compositor_popup_in_chain(keep, row))
                        continue;
                asked |= !(compositor.state[row] & WINDOW_CLOSING);
                compositor_close(row);
        }
        return asked;
}

static b32 compositor_popup_first(int row)
{
        struct windows *table = compositor.table;
        unsigned int named = table->popup_parent[row], count;
        int parent = named >= 1 && named <= (unsigned int)COMPOSITOR_ROWS ? (int)named - 1 : -1, ax, ay, x, y, at;
        const int (*screens)[4];

        if (parent < 0 || parent == row || !compositor.showing[parent])
                return false;
        compositor_popup_parent[row] = parent;
        ax = compositor.x[parent] + compositor_fit(table->popup_x[row], -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST);
        ay = compositor.y[parent] + compositor_fit(table->popup_y[row], -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST);
        screens = compositor_screens(&count);
        at = compositor_screen_at(screens, count, ax, ay);
        at = at < 0 ? compositor_screen_of(parent) : at;
        compositor_popup_place(ax, ay, compositor_fit(table->popup_w[row], 0, WINDOW_PLACE_MOST),
                               compositor_fit(table->popup_h[row], 0, WINDOW_PLACE_MOST),
                               (int)compositor.w[row], (int)compositor.h[row], table->popup_flags[row], screens[at < 0 ? 0 : at], &x, &y);
        compositor_put(row, x, y);
        compositor_raise(row);
        compositor_damage_row(row);
        return true;
}

static void compositor_popup_leave(int row)
{
        compositor_popup_parent[row] = -1;
        for (int child = 0; child < (int)compositor_rows_in_use; child++)
                if (compositor_popup_in_chain(compositor_popup_parent[child], row))
                {
                        if (compositor.showing[child] && !compositor_row_xoverride(child))
                                compositor_close(child);
                        compositor_popup_parent[child] = -1;
                }
}

/* ---- external row allocation and doors ---- */

#ifdef CONFIG_MOONWATER_WAYLAND
static long wayland_row_owner(long row);
#endif

#define COMPOSITOR_ROW_TOPLEVEL 0
#define COMPOSITOR_ROW_PLACED   1
#define COMPOSITOR_ROW_POPUP    2

struct compositor_row_spec
{
        unsigned int pid, kind, glass, asked, popup_flags;
        int parent, x, y;
};

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
                table->glass[row] = spec->glass, table->asked[row] = spec->asked ? 1u : 0u;
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
                        table->popup_x[row] = spec->x, table->popup_y[row] = spec->y;
                        table->popup_flags[row] = spec->popup_flags ? spec->popup_flags : (WINDOW_POPUP_POINT | WINDOW_POPUP_EXACT);
                        table->glass[row] |= WINDOW_NO_TAB;
                }
                __atomic_store_n(&table->pid[row], spec->pid > 0 ? spec->pid : 1u, __ATOMIC_RELEASE);
                return row;
        }
        return -1;
}

static void compositor_row_release(int row)
{
        compositor_clip_leave(row);
        compositor_popup_leave(row);
        compositor_leave(row);
}

static void compositor_door_gone(int row)
{
#ifdef CONFIG_MOONWATER_WAYLAND
        if (wayland_row_owner(row) >= 0)
                return;
#endif
        compositor_row_release(row);
}

static void compositor_door(int row, int returned)
{
        if (compositor_door_ask((unsigned int)row, returned & COMPOSITOR_POLLHUP ? COMPOSITOR_DOOR_REOPEN : COMPOSITOR_DOOR_DRAIN))
        {
                rows_add(&compositor_doors.busy, row);
                compositor_take(row);
                compositor_clip_door(row);
                return;
        }
        compositor_door_drain(compositor.door[row]);
        compositor_take(row);
        compositor_clip_door(row);
        if (!(returned & COMPOSITOR_POLLHUP))
                return;
        compositor_door_reopen((unsigned int)row);
        compositor_door_gone(row);
}

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
