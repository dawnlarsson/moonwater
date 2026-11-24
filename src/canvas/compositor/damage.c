/*
        What changed, and what each window is as a frame.

        The damage list and its rectangles (compositor_area, compositor_union, compositor_meet), a window's landing and
        its glide, the snap preview's state, a lent buffer's state as the shim keeps it (compositor_lent_*, compositor_wl_*),
        a row's pixels and the runs of one colour learned in them, the frame each window is drawn as (compositor_frame_of),
        the windows that have a tab, and the bands: how many, and the view of each the taskbar is given
        (compositor_taskbar_view and compositor_band_view, which are the kit's to change).

        What changed is said here and drawn elsewhere: no pixel of the scanout is written here (present.c composes the
        damage), and no window is moved or raised (manage.c, input.c). It reads the compositor's state, so it comes after
        compositor.c's state and helpers, and after compose.c and outputs.c; the few things it asks of parts that come
        later (the overlays and popups of manage.c, the scale of kms.c, the theme's inks of session.c, the snap of input.c)
        are declared before their first call, here or in compositor.c's prelude. compositor_damage is called from every
        part after it (glass.c, scanout.c, present.c, kms.c, session.c, manage.c, input.c), compositor.c's startup and
        loop and wayland_host.c; compose.c calls compositor_taskbar_view, which compositor.c declares.
*/

/*
        Growable anonymous mmap scratch helper shared across the compositor (damage, glass, scanout, record, gpu_host).
        Handles alignment (4 KiB or 64 KiB), optional 2x growth, optional gpu_forget before munmap, and optional content preservation.
*/
#define MEM_BUF_GPU_FORGET 1u
#define MEM_BUF_GROW_2X    2u
#define MEM_BUF_ALIGN_64K  4u
#define MEM_BUF_KEEP       8u

static void mem_buf_free(void **ptr, unsigned long *cap, unsigned int flags)
{
        if (*ptr)
        {
                if (flags & MEM_BUF_GPU_FORGET)
                        gpu_forget((const void *)*ptr, (positive)*cap);
                gpu_own((const void *)*ptr, (positive)*cap, 0);
                compositor_call(syscall(munmap), (positive)*ptr, *cap, 0, 0, 0, 0);
        }
        *ptr = 0;
        *cap = 0;
}

static void *mem_buf_grow(void **ptr, unsigned long *cap, unsigned long need, unsigned int flags)
{
        unsigned long align = (flags & MEM_BUF_ALIGN_64K) ? 65536ul : 4096ul;
        unsigned long bytes = need;
        long mapped;

        if (!need)
                return *ptr;
        if (*ptr && *cap >= need)
                return *ptr;
        if ((flags & MEM_BUF_GROW_2X) && bytes < *cap * 2)
                bytes = *cap * 2;
        bytes = (bytes + align - 1) & ~(align - 1);
        if (!(flags & MEM_BUF_KEEP))
                mem_buf_free(ptr, cap, flags);
        mapped = (long)compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
                return 0;
        gpu_own((const void *)mapped, (positive)bytes, 1); // the compositor's own private pages: the card may map them written where it has no read-only maps
        if (*ptr)
        {
                memory_copy((void *)mapped, *ptr, *cap);
                mem_buf_free(ptr, cap, flags);
        }
        *ptr = (void *)mapped;
        *cap = bytes;
        return *ptr;
}

/*
        Shared rectangle, damage-list, switch-file, and cubic-bezier tween primitives.
*/
struct canvas_rect
{
        int x1, y1, x2, y2;
};

_Static_assert(sizeof(struct canvas_rect) == 4 * sizeof(int), "canvas_rect matches int[4]");

struct canvas_damage
{
        struct canvas_rect r[COMPOSITOR_DAMAGE];
        unsigned int count;
};

static inline INLINE CONST int rect_empty(struct canvas_rect r)
{
        return r.x2 <= r.x1 || r.y2 <= r.y1;
}

static inline INLINE CONST long rect_area(struct canvas_rect r)
{
        return (long)(r.x2 - r.x1) * (r.y2 - r.y1);
}

static inline INLINE CONST struct canvas_rect rect_join(struct canvas_rect a, struct canvas_rect b)
{
        return (struct canvas_rect){a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1,
                                    a.x2 > b.x2 ? a.x2 : b.x2, a.y2 > b.y2 ? a.y2 : b.y2};
}

static inline INLINE CONST struct canvas_rect rect_meet(struct canvas_rect a, struct canvas_rect b)
{
        struct canvas_rect m = {a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1,
                                a.x2 < b.x2 ? a.x2 : b.x2, a.y2 < b.y2 ? a.y2 : b.y2};

        return rect_empty(m) ? (struct canvas_rect){0, 0, 0, 0} : m;
}

// Whether outer covers inner: every point of inner is in outer (an empty inner is covered by anything).
static inline INLINE CONST int rect_cover(struct canvas_rect outer, struct canvas_rect inner)
{
        return rect_empty(inner) || (outer.x1 <= inner.x1 && outer.y1 <= inner.y1 && outer.x2 >= inner.x2 && outer.y2 >= inner.y2);
}

// The pieces of r outside cut, at most four, into out: the rows above and below cut at r's full width, and the sides between them. A cut that
// does not meet r leaves r whole. Answers how many pieces (0 for an empty r).
static inline INLINE int rect_cut(struct canvas_rect r, struct canvas_rect cut, struct canvas_rect out[4])
{
        struct canvas_rect m = rect_meet(r, cut);
        struct canvas_rect parts[4] = {
            {r.x1, r.y1, r.x2, m.y1},
            {r.x1, m.y2, r.x2, r.y2},
            {r.x1, m.y1, m.x1, m.y2},
            {m.x2, m.y1, r.x2, m.y2},
        };
        int n = 0;

        if (rect_empty(r))
                return 0;
        if (rect_empty(m))
        {
                out[0] = r;
                return 1;
        }
        for (int k = 0; k < 4; k++)
                if (!rect_empty(parts[k]))
                        out[n++] = parts[k];
        return n;
}

static inline INLINE PURE struct canvas_rect rect_from_int4(const int p[4])
{
        return (struct canvas_rect){p[0], p[1], p[2], p[3]};
}

static inline INLINE void rect_to_int4(struct canvas_rect r, int p[4])
{
        p[0] = r.x1, p[1] = r.y1, p[2] = r.x2, p[3] = r.y2;
}

// Add rectangle r to damage list d, coalescing whenever union area <= sum + slack (0 for exact damage, >0 for GPU slack merge).
HOT static void damage_add(struct canvas_damage *restrict d, struct canvas_rect r, long slack)
{
        unsigned int at = 0;

        if (unlikely(rect_empty(r)))
                return;
        while (at < d->count)
        {
                struct canvas_rect joined = rect_join(r, d->r[at]);

                if (rect_area(joined) <= rect_area(r) + rect_area(d->r[at]) + slack)
                {
                        r = joined;
                        d->r[at] = d->r[--d->count];
                        at = 0;
                }
                else
                        at++;
        }
        if (unlikely(d->count == COMPOSITOR_DAMAGE))
        {
                unsigned int best = 0;
                long least = -1;

                for (at = 0; at < COMPOSITOR_DAMAGE; at++)
                {
                        long grown = rect_area(rect_join(r, d->r[at])) - rect_area(d->r[at]);

                        if (least < 0 || grown < least)
                                least = grown, best = at;
                }
                d->r[best] = rect_join(r, d->r[best]);
                return;
        }
        d->r[d->count++] = r;
}

// Subtract cut from every rectangle in d; returns 0 (leaving d untouched) if the pieces exceed COMPOSITOR_DAMAGE.
HOT static int damage_subtract(struct canvas_damage *restrict d, struct canvas_rect cut)
{
        struct canvas_damage out;

        out.count = 0;
        for (unsigned int at = 0; at < d->count; at++)
        {
                struct canvas_rect r = d->r[at];

                if (r.x2 <= cut.x1 || r.x1 >= cut.x2 || r.y2 <= cut.y1 || r.y1 >= cut.y2)
                {
                        if (unlikely(out.count == COMPOSITOR_DAMAGE))
                                return 0;
                        out.r[out.count++] = r;
                        continue;
                }
                {
                        int mid_y1 = cut.y1 > r.y1 ? cut.y1 : r.y1, mid_y2 = cut.y2 < r.y2 ? cut.y2 : r.y2;
                        struct canvas_rect parts[4] = {
                            {r.x1, r.y1, r.x2, cut.y1},
                            {r.x1, cut.y2, r.x2, r.y2},
                            {r.x1, mid_y1, cut.x1, mid_y2},
                            {cut.x2, mid_y1, r.x2, mid_y2},
                        };

                        for (unsigned int k = 0; k < 4; k++)
                        {
                                struct canvas_rect c = rect_meet(parts[k], r);

                                if (rect_empty(c))
                                        continue;
                                if (unlikely(out.count == COMPOSITOR_DAMAGE))
                                        return 0;
                                out.r[out.count++] = c;
                        }
                }
        }
        *d = out;
        return 1;
}

static void damage_load_compositor(struct canvas_damage *d)
{
        d->count = compositor.damages < COMPOSITOR_DAMAGE ? compositor.damages : COMPOSITOR_DAMAGE;
        if (d->count)
                memory_copy(d->r, compositor.damage, d->count * sizeof(d->r[0]));
}

static void damage_store_compositor(const struct canvas_damage *d)
{
        compositor.damages = d->count;
        if (d->count)
                memory_copy(compositor.damage, d->r, d->count * sizeof(d->r[0]));
}

// Shared /run/moonwater/* switch-file probe (immediate or periodic-mask throttled).
struct canvas_switch
{
        unsigned int probes;
        int active;
};

static b32 canvas_switch_exists(const char *path, unsigned int extra_flags)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | extra_flags);

        if (handle < 0)
                return 0;
        system_close(handle);
        return 1;
}

static int canvas_switch_poll(struct canvas_switch *sw, const char *path, unsigned int mask)
{
        if ((sw->probes++ & mask) == 0)
                sw->active = (int)canvas_switch_exists(path, 0);
        return sw->active;
}

// Shared cubic-bezier easing helpers (land, glide, preview, sway).
static b64 canvas_tween_ease(positive at, positive now, b64 duration_ns)
{
        positive gone;

        if (!at)
                return 0;
        gone = now > at ? now - at : 0;
        return ui_bezier_at(&ui_ease, ui_progress((b64)gone, ui_paced(duration_ns)));
}

static inline int canvas_tween_lerp(int from, int to, b64 t)
{
        return from + (int)(((long long)(to - from) * (long long)t) / (long long)UI_ONE);
}

static void canvas_tween_rect(int out[4], const int from[4], const int to[4], b64 t)
{
        for (int k = 0; k < 4; k++)
                out[k] = canvas_tween_lerp(from[k], to[k], t);
}

static long compositor_area(const int *rectangle)
{
        return rect_area(rect_from_int4(rectangle));
}

static long compositor_joined(const int *a, const int *b, int *into)
{
        struct canvas_rect j = rect_join(rect_from_int4(a), rect_from_int4(b));

        rect_to_int4(j, into);
        return rect_area(j);
}

// A rectangle, x1, y1, x2, y2, grown to take in another; one with x2 not past x1 is none.
static void compositor_union(int *into, const int *other)
{
        struct canvas_rect o = rect_from_int4(other), i = rect_from_int4(into);

        if (rect_empty(o))
                return;
        rect_to_int4(rect_empty(i) ? o : rect_join(i, o), into);
}

// A rectangle cut to another; none when they do not meet.
static void compositor_meet(int *into, const int *other)
{
        rect_to_int4(rect_meet(rect_from_int4(into), rect_from_int4(other)), into);
}


// The glass's blur constants (the block, the spread and the reach) are in ui/ui.c, where the band reads them too.

// The rectangle of a window's contents, x1, y1, x2, y2, on the output and in it.
static void compositor_contents_box(int row, int *box)
{
        int output[4] = {0, 0, compositor.width, compositor.height};

        box[0] = compositor.x[row];
        box[1] = compositor.y[row];
        box[2] = compositor.x[row] + (int)compositor.w[row];
        box[3] = compositor.y[row] + (int)compositor.h[row];
        compositor_meet(box, output);
}

// Where a row is in the stack, from 0 at the bottom; -1 when it is not in it.
static int compositor_stack_place(int row)
{
        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor.stack[at] == row)
                        return (int)at;
        return -1;
}

/*
        What a change does to the glass windows. A window's own picture is
        composed over its backdrop again where it changed; anything else in
        the scene -- the desktop, a window moved, raised or gone, or the
        picture of a window under it -- is blurred again under it as far as
        the blur reaches from the change, and only that. A window's picture
        changes nothing of the glass below it, and what lies over every
        window (the pointer drawn in software, the tabs) nothing of any.
*/
static void compositor_glass_touch(const int *rectangle, int damage_by)
{
        int reach = COMPOSITOR_GLASS_REACH;
        int by = damage_by > 0 ? damage_by - 1 : COMPOSITOR_NONE;
        int lowest = by >= 0 ? compositor_stack_place(by) : -1;

        if (damage_by == COMPOSITOR_OVER)
                return;

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                int box[4], change[4];

                if (!compositor.glassy[row] || !compositor.showing[row])
                        continue;
                compositor_contents_box(row, box);
                memory_copy(change, rectangle, sizeof(change));
                if (row == by)
                {
                        compositor_meet(change, box);
                        compositor_union(compositor.dirty[row], change);
                        continue;
                }
                if (by >= 0 && compositor_stack_place(row) < lowest)
                        continue;
                // The change itself, where it is near enough to reach the window.
                box[0] -= reach, box[1] -= reach, box[2] += reach, box[3] += reach;
                compositor_meet(change, box);
                compositor_union(compositor.stale[row], change);
        }
}

static int compositor_visible(int row);
static void compositor_frame_of(int row, struct compositor_frame *frame);
static int compositor_row_scale(int row);
static unsigned int compositor_snap(const int *box, unsigned int *touch, int *screen);
static void compositor_assist_refresh(void);
static int compositor_overlays_up(void);
static void compositor_overlays_damage(void);
static void compositor_overlays_damage_all(void);
static int compositor_overlays_shown(void);
static void compositor_overlay_fade(unsigned int goal);
static unsigned int compositor_overlays(struct compositor_frame *frames, unsigned int at);

/*
        A change that touches a window's close button touches all of it: the X
        is the mean under the box, so a change to part of the box changes the
        ink of the rest, and a box drawn in part would be half old. The change
        grows to take the whole box in, as often as another box is touched by
        the growing.
*/
static int compositor_controls_wanted(int row); // the bar's buttons, placed by the picture (compositor_controls, below)

static void compositor_close_join(int *rectangle)
{
        int grown = 1;

        while (grown)
        {
                grown = 0;
                for (unsigned int at = 0; at < compositor.stacked; at++)
                {
                        struct compositor_frame frame;
                        int row = compositor.stack[at], box[4], bx, by, side, meet[4], mx, my, mside, right;

                        if (!compositor_visible(row))
                                continue;
                        // The buttons are read from what places them only: the window's rectangle, its scale and its asks, which are its
                        // frame's while it neither glides nor lands (those move or shrink the frame, which is then made whole). Its whole
                        // frame (compositor_frame_of: the picture settled, the land and glide, the lit edge, the preview) was made for every
                        // window at every rectangle damaged, a client's commit included, to find two discs.
                        if (compositor.glide[row] || compositor.landing[row] || compositor_controls_wanted(row))
                                compositor_frame_of(row, &frame);
                        else
                        {
                                // Inside the rectangle, or nothing of it is touched.
                                if (rectangle[2] <= compositor.x[row] || rectangle[0] >= compositor.x[row] + (int)compositor.w[row] ||
                                    rectangle[3] <= compositor.y[row] || rectangle[1] >= compositor.y[row] + (int)compositor.h[row])
                                        continue;
                                frame = (struct compositor_frame){.x = compositor.x[row], .y = compositor.y[row], .width = (int)compositor.w[row],
                                                                  .height = (int)compositor.h[row], .scale = compositor_row_scale(row),
                                                                  .close = !(compositor.asks[row] & WINDOW_NO_CLOSE),
                                                                  .minimize = !(compositor.asks[row] & WINDOW_NO_TAB)};
                        }
                        if (!compositor_close_box(&frame, &bx, &by, &side))
                                continue;
                        // Union the close and minimize button boxes (works in both LTR and RTL layouts).
                        right = bx + side;
                        if (compositor_minimize_box(&frame, &mx, &my, &mside))
                        {
                                if (mx < bx)
                                        bx = mx;
                                if (mx + mside > right)
                                        right = mx + mside;
                        }
                        box[0] = bx < 0 ? 0 : bx, box[1] = by < 0 ? 0 : by;
                        box[2] = right > compositor.width ? compositor.width : right;
                        box[3] = by + side > compositor.height ? compositor.height : by + side;
                        memory_copy(meet, rectangle, sizeof(meet));
                        compositor_meet(meet, box);
                        if (meet[2] > meet[0] && meet[3] > meet[1] && (rectangle[0] > box[0] || rectangle[1] > box[1] ||
                                                                      rectangle[2] < box[2] || rectangle[3] < box[3]))
                        {
                                compositor_union(rectangle, box);
                                grown = 1;
                        }
                }
        }
}

static void compositor_damage_from(int x, int y, int width, int height, int damage_by)
{
        long long rx2, ry2;
        int rectangle[4];
        struct canvas_damage d;

        if (width <= 0 || height <= 0)
                return;
        rx2 = (long long)x + width;
        ry2 = (long long)y + height;
        rectangle[0] = x < 0 ? 0 : x;
        rectangle[1] = y < 0 ? 0 : y;
        rectangle[2] = rx2 > compositor.width ? compositor.width : (int)rx2;
        rectangle[3] = ry2 > compositor.height ? compositor.height : (int)ry2;

        if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
                return;

        compositor_close_join(rectangle);
        compositor_glass_touch(rectangle, damage_by);

        damage_load_compositor(&d);
        damage_add(&d, rect_from_int4(rectangle), 0);
        damage_store_compositor(&d);
}

static void compositor_damage(int x, int y, int width, int height)
{
        compositor_damage_from(x, y, width, height, compositor.damage_by);
}

// Whether a window is on the screen: it has shown a picture, is not put away, and belongs to its screen's active Space.
static int compositor_visible(int row)
{
        return compositor.showing[row] && !(compositor.state[row] & WINDOW_MINIMIZED) && compositor_space_row_active(row);
}

static int compositor_row_scale(int row);

// The line round a window while it floats, in device pixels, at the scale of the screen it is on; a window filling the
// output or half of it meets the output's edges with none, and square. Glass has the same line and corners: it is what is
// under it, inside them.
static int compositor_line(int row)
{
        return compositor.arranged[row] == COMPOSITOR_FLOATING ? compositor_line_px(compositor_row_scale(row)) : 0;
}

// A window filling the output, or its room: its edges are the screen's, so it has no line and square corners.
static int compositor_filling(int row)
{
        return compositor.arranged[row] == COMPOSITOR_MAXIMIZED || compositor.arranged[row] == COMPOSITOR_ROOM;
}

// Whether a window's corners are round: only a floating window's. A window arranged, filling the output or its room or
// a half or a quarter of its screen, is square (it meets the screen's edges and its neighbours', as a maximized one does).
static int compositor_rounded(int row)
{
        return compositor.arranged[row] == COMPOSITOR_FLOATING;
}

// The line a window is drawn with, at the scale of its screen: a floating window's, and a tile's (it reads as a window, and
// where it meets another tile the line is what tells them apart). A window filling the output, or its room, has none. The
// geometry keeps compositor_line, which a placement clamps to.
static int compositor_frame_line(int row)
{
        return compositor_filling(row) ? 0 : compositor_line_px(compositor_row_scale(row));
}

/*
        A window's first show lands: its picture starts at 96 percent of its size and grows to all of it over
        COMPOSITOR_LAND_NS at the pace (ui_paced), on the kit's --ease, about its centre. A scale only: no fade, no shadow, no outline.
        The clock starts at the first present that draws the window (compositor_land_keep), so that frame is the
        96 percent one. Reduced motion, the table's as the kit reads it, lands nothing: the window is drawn at its
        size at once. A window shown again, moved or resized does not land.
*/
#define COMPOSITOR_LAND_NS (170 * UI_MS)  // the landing, from the first present to its size
#define COMPOSITOR_LAND_SMALL UI_Q2(96)  // the size it starts at, as a part of its own (Q30)

// How much of a landing window's size is still to grow back, Q30: 4 percent before its first present, none once landed.
static b64 compositor_land_short(int row)
{
        b64 done;

        if (!compositor.landing[row])
                return 0;
        done = canvas_tween_ease(compositor.land_at[row], clock_monotonic_nanoseconds(), COMPOSITOR_LAND_NS);
        return ui_qmul(UI_ONE - COMPOSITOR_LAND_SMALL, UI_ONE - done);
}

// A landing window's frame: its rectangle less the part still to grow back, about its centre, with its picture's size.
static void compositor_land_frame(int row, struct compositor_frame *frame)
{
        b64 shrink = compositor_land_short(row);
        int dw, dh;

        if (!shrink || !compositor.w[row] || !compositor.h[row])
                return;
        dw = (int)(((b64)compositor.w[row] * shrink + UI_HALF) >> UI_Q);
        dh = (int)(((b64)compositor.h[row] * shrink + UI_HALF) >> UI_Q);
        frame->x += dw / 2;
        frame->y += dh / 2;
        frame->width -= dw;
        frame->height -= dh;
        frame->source_w = (int)compositor.w[row];
        frame->source_h = (int)compositor.h[row];
}

// The whole of a landing window's rectangle at its size: every frame of its landing lies in it.
static void compositor_land_damage(int row)
{
        int line = compositor_line(row);

        compositor_damage(compositor.x[row] - line, compositor.y[row] - line, (int)compositor.w[row] + 2 * line,
                          (int)compositor.h[row] + 2 * line);
}

// A window's first show: it lands from COMPOSITOR_LAND_SMALL, unless motion is reduced.
static void compositor_land(int row)
{
        if (!compositor.table || compositor.table->motion)
                return;
        compositor.landing[row] = 1;
        compositor.land_at[row] = 0;
        compositor_land_damage(row);
}

/*
        After a present: a window still landing is damaged again, so the next present draws it, and the one that
        lands it is drawn at its size once more. Nothing is damaged after that, and the loop goes idle.
*/
static void compositor_land_keep(void)
{
        positive now = 0;

        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
        {
                if (!compositor.landing[row])
                        continue;
                if (!now)
                        now = clock_monotonic_nanoseconds();
                if (!compositor.land_at[row])
                        compositor.land_at[row] = now;
                if (!compositor.showing[row] || !compositor_visible(row) || now - compositor.land_at[row] >= (positive)ui_paced(COMPOSITOR_LAND_NS))
                        compositor.landing[row] = 0;
                compositor_land_damage(row);
        }
}

/*
        A window arranged or floated glides from the rectangle it had to the one it has, over COMPOSITOR_GLIDE_NS (the kit's
        window motion) at the pace on the kit's --ease, so the eye sees where it went. It is called once the change is made (from is what
        it was, taken before); a window still gliding starts from where it is drawn, not from where it was going, so two
        changes in a row never jump. Reduced motion, the table's as the kit reads it, glides nothing: the window is where it
        is at once. A window that is dragged does not glide, and one that stops being shown ends its glide (compositor_glide_keep).
*/
#define COMPOSITOR_GLIDE_NS UI_WINDOW_NS

static void compositor_damage_row(int row);

static int compositor_glides(void)
{
        return compositor.table && !compositor.table->motion;
}

// Where a window glides from now: where it began, moved by as much as the window was moved since (compositor.glide_base).
static void compositor_glide_origin(int row, int *from)
{
        memory_copy(from, compositor.glide_from[row], sizeof(compositor.glide_from[row]));
        from[0] += compositor.x[row] - compositor.glide_base[row][0];
        from[1] += compositor.y[row] - compositor.glide_base[row][1];
}

static void compositor_glide_damage(int row, const int *from)
{
        int line = compositor_frame_line(row), x1 = from[0] < compositor.x[row] ? from[0] : compositor.x[row],
            y1 = from[1] < compositor.y[row] ? from[1] : compositor.y[row];
        int x2 = from[0] + from[2] > compositor.x[row] + (int)compositor.w[row] ? from[0] + from[2] : compositor.x[row] + (int)compositor.w[row];
        int y2 = from[1] + from[3] > compositor.y[row] + (int)compositor.h[row] ? from[1] + from[3] : compositor.y[row] + (int)compositor.h[row];

        compositor_damage(x1 - line, y1 - line, x2 - x1 + 2 * line, y2 - y1 + 2 * line);
}

// A glide's whole span damaged: from where it glides from now to where the window is.
static void compositor_glide_damage_now(int row)
{
        int from[4];

        compositor_glide_origin(row, from);
        compositor_glide_damage(row, from);
}

// How far a glide that began at `at` is, Q30 on the kit's ease: none before its first present (at 0).
static b64 compositor_glide_done(positive at)
{
        return canvas_tween_ease(at, clock_monotonic_nanoseconds(), COMPOSITOR_GLIDE_NS);
}

// A glide from `from` to where the window is now, begun at its next present.
static void compositor_glide_begin(int row, const int *from)
{
        memory_copy(compositor.glide_from[row], from, sizeof(compositor.glide_from[row]));
        compositor.glide_base[row][0] = compositor.x[row], compositor.glide_base[row][1] = compositor.y[row];
        compositor.glide[row] = 1;
        compositor.glide_at[row] = 0;
        compositor_glide_damage(row, from);
}

static void compositor_glide_start(int row, const int *from)
{
        int drawn[4], origin[4];

        if (!compositor_glides() || row < 0 || !compositor.showing[row] || row == compositor.drag)
                return;
        // Still gliding toward from: it is drawn between where it glided from and there, and goes on from that. Where it glided
        // from moved with the window up to from, where it was put before this change (the window is already where it goes).
        if (compositor.glide[row])
        {
                b64 done = compositor_glide_done(compositor.glide_at[row]);

                memory_copy(origin, compositor.glide_from[row], sizeof(origin));
                origin[0] += from[0] - compositor.glide_base[row][0];
                origin[1] += from[1] - compositor.glide_base[row][1];
                canvas_tween_rect(drawn, origin, from, done);
                compositor_glide_damage(row, origin);
                from = drawn;
        }
        compositor.drag_glide = compositor.drag_glide == row + 1 ? 0 : compositor.drag_glide;
        compositor_glide_begin(row, from);
}

// A window's rectangle as it is in the frame: where it glides from, to where it is, by the ease of the time since its first present.
static void compositor_glide_frame(int row, struct compositor_frame *frame)
{
        int from[4], to[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]}, at[4];

        compositor_glide_origin(row, from);
        canvas_tween_rect(at, from, to, compositor_glide_done(compositor.glide_at[row]));
        frame->x = at[0], frame->y = at[1], frame->width = at[2], frame->height = at[3];
        frame->source_w = (int)compositor.w[row], frame->source_h = (int)compositor.h[row];
}

/*
        The roll from one Scene to the next on a screen: both Scenes' windows move across the screen together, the old out of one
        side as the new comes in at the other, as a billboard turns to its next poster, so the way the person went is plain. They
        move by depth: a Scene's top window travels COMPOSITOR_ROLL_DEPTH more of the screen than its bottom one, so the layers
        pass at their own speeds over the desktop, which stays where it is. The old Scene's windows are drawn for the roll alone
        (compositor_roll_holds): they take no press and no key. The new Scene's windows are where they are for the hand from the
        start, only drawn on their way. One screen only: a window rolled off it would be drawn on the next.
*/
#define COMPOSITOR_ROLL_NS (UI_WINDOW_NS * 2)
#define COMPOSITOR_ROLL_DEPTH 250 // thousandths of the screen's width

static struct
{
        unsigned int from, to; // the Scene rolled out and the one rolled in
        int screen, dir, width; // its screen; 1 the new Scene comes in at the right, -1 at the left; the screen's width
        positive at;           // the roll's first present, 0 before it
        unsigned char on;
} compositor_roll;

static void compositor_roll_begin(int screen, unsigned int from, unsigned int to, int dir, int width)
{
        compositor_roll.from = from, compositor_roll.to = to, compositor_roll.screen = screen;
        compositor_roll.dir = dir < 0 ? -1 : 1, compositor_roll.width = width, compositor_roll.at = 0, compositor_roll.on = 1;
}

// The old Scene's windows on the roll's screen, drawn while it goes.
static int compositor_roll_holds(int row)
{
        return compositor_roll.on && compositor.row_space && row >= 0 && row < COMPOSITOR_ROWS && compositor.showing[row] &&
               compositor.row_space[row] == compositor_roll.from && !(compositor.state[row] & WINDOW_MINIMIZED);
}

// How far across a window of either Scene is drawn now: its travel (the screen, and more the higher it is in its Scene's stack)
// times what is left of the roll for the new Scene, or what has gone of it for the old.
static int compositor_roll_dx(int row)
{
        unsigned int scene, above = 0, all = 0;
        long long travel;
        b64 done;

        if (!compositor_roll.on || !compositor.row_space || row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        scene = compositor.row_space[row];
        if (scene != compositor_roll.from && scene != compositor_roll.to)
                return 0;
        for (unsigned int at = 0; at < compositor.stacked; at++)
        {
                int other = (int)compositor.stack[at];

                if (compositor.row_space[other] != scene || !compositor.showing[other])
                        continue;
                all++;
                if (other == row)
                        above = 0;
                else
                        above++;
        }
        travel = (long long)compositor_roll.width * (1000 + (all > 1 ? (long long)COMPOSITOR_ROLL_DEPTH * (all - 1 - above) / (all - 1) : COMPOSITOR_ROLL_DEPTH)) / 1000;
        done = canvas_tween_ease(compositor_roll.at, clock_monotonic_nanoseconds(), COMPOSITOR_ROLL_NS);
        return scene == compositor_roll.to ? (int)(compositor_roll.dir * travel * (long long)(UI_ONE - done) / (long long)UI_ONE)
                                           : (int)(-compositor_roll.dir * travel * (long long)done / (long long)UI_ONE);
}

// After a present: the roll's screen drawn again while it rolls, and once more when it ends, without the old Scene.
static void compositor_roll_keep(void)
{
        unsigned int count;
        const int (*screens)[4];
        positive now;

        if (!compositor_roll.on)
                return;
        now = clock_monotonic_nanoseconds();
        if (!compositor_roll.at)
                compositor_roll.at = now;
        if (now - compositor_roll.at >= (positive)ui_paced(COMPOSITOR_ROLL_NS) || !compositor_glides())
                compositor_roll.on = 0;
        screens = compositor_screens(&count);
        if ((unsigned int)compositor_roll.screen < count)
                compositor_damage(screens[compositor_roll.screen][0], screens[compositor_roll.screen][1], screens[compositor_roll.screen][2],
                                  screens[compositor_roll.screen][3]);
        else
                compositor_damage(0, 0, compositor.width, compositor.height);
}

/*
        After a present: a window still gliding is damaged again, so the next present draws it, and the one that ends it is
        drawn where it is. Nothing is damaged after that, and the loop goes idle.
*/
static void compositor_glide_keep(void)
{
        compositor_roll_keep();
        positive now = 0;

        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
        {
                if (!compositor.glide[row])
                        continue;
                if (!now)
                        now = clock_monotonic_nanoseconds();
                if (!compositor.glide_at[row])
                        compositor.glide_at[row] = now;
                if (!compositor_visible((int)row) || (row == (unsigned int)compositor.drag && compositor.drag_glide != (int)row + 1) ||
                    now - compositor.glide_at[row] >= (positive)ui_paced(COMPOSITOR_GLIDE_NS))
                {
                        // Where it was is drawn again too: the last picture of it was at the glide's place, not its own.
                        compositor_glide_damage_now((int)row);
                        compositor.glide[row] = 0;
                        compositor.drag_glide = compositor.drag_glide == (int)row + 1 ? 0 : compositor.drag_glide;
                        compositor_damage_row((int)row);
                        continue;
                }
                compositor_glide_damage_now((int)row);
        }
}

/*
        The snap preview as it is drawn. It is there at full strength the frame its target is touched; a new target (an edge's
        corner) is reached from where the preview is drawn, over UI_WINDOW_NS on the kit's ease, so it moves and does not
        cut; and with no target -- lost, or let go on, the window gliding into it -- it fades out where it was drawn over the
        same time, so the window covers it as it lands. Reduced motion cuts. compositor.preview, the target, is the target
        from the first frame whatever is drawn, so a let go at any point lands on it.
*/
// The preview drawn now: its rectangle, and its strength in percent, 0 for none.
static unsigned int compositor_preview_shown(int *rect)
{
        b64 done;

        if (!compositor.preview_draw)
                return 0;
        done = canvas_tween_ease(compositor.preview_at, clock_monotonic_nanoseconds(), UI_WINDOW_NS);
        if (compositor.preview_fading || compositor.preview[2] <= 0)
        {
                memory_copy(rect, compositor.preview_from, sizeof(compositor.preview_from));
                return compositor.preview_fading ? (unsigned int)canvas_tween_lerp((int)compositor.preview_from_alpha, 0, done) : 0;
        }
        canvas_tween_rect(rect, compositor.preview_from, compositor.preview, done);
        return (unsigned int)canvas_tween_lerp((int)compositor.preview_from_alpha, 100, done);
}

// A new target (rect, filling how) for row's preview, before compositor.preview is set to it: from where it is drawn when it
// is drawn for row, else there at once.
static void compositor_preview_enter(int row, const int *rect, unsigned int how)
{
        int drawn[4];
        unsigned int alpha = compositor_preview_shown(drawn);

        if (alpha)
                compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
        if (alpha && compositor_glides() && compositor.preview_draw == row + 1)
        {
                memory_copy(compositor.preview_from, drawn, sizeof(drawn));
                compositor.preview_from_alpha = alpha;
        }
        else
        {
                memory_copy(compositor.preview_from, rect, sizeof(compositor.preview_from));
                compositor.preview_from_alpha = 100;
        }
        compositor.preview_fading = 0;
        compositor.preview_at = 0;
        compositor.preview_draw = row + 1;
        compositor.preview_how = how;
}

// The target gone, before compositor.preview is cleared: the preview fades out where it is drawn.
static void compositor_preview_leave(void)
{
        int drawn[4];
        unsigned int alpha = compositor_preview_shown(drawn);

        if (alpha)
                compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
        if (!alpha || !compositor_glides())
        {
                compositor.preview_draw = 0;
                return;
        }
        memory_copy(compositor.preview_from, drawn, sizeof(drawn));
        compositor.preview_from_alpha = alpha;
        compositor.preview_fading = 1;
        compositor.preview_at = 0;
}

/*
        After a present: a preview still moving or fading is damaged again (where it moves from and to), its clock started at
        the present that first drew it; one that has arrived is still, and one that has faded is gone. A window that stops
        being shown takes its preview with it.
*/
static void compositor_preview_keep(void)
{
        int row = compositor.preview_draw - 1, drawn[4], *to = compositor.preview_fading || compositor.preview[2] <= 0
                                                                   ? compositor.preview_from : compositor.preview;
        int x1, y1, x2, y2;
        positive now;

        if (row < 0)
                return;
        if (!compositor_visible(row))
        {
                if (compositor_preview_shown(drawn))
                        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
                compositor.preview_draw = 0;
                return;
        }
        if (!compositor.preview_fading && compositor.preview_from_alpha == 100 &&
            !memory_compare(compositor.preview_from, compositor.preview, sizeof(compositor.preview)))
                return;
        now = clock_monotonic_nanoseconds();
        if (!compositor.preview_at)
                compositor.preview_at = now;
        x1 = to[0] < compositor.preview_from[0] ? to[0] : compositor.preview_from[0];
        y1 = to[1] < compositor.preview_from[1] ? to[1] : compositor.preview_from[1];
        x2 = to[0] + to[2] > compositor.preview_from[0] + compositor.preview_from[2] ? to[0] + to[2]
                                                                                      : compositor.preview_from[0] + compositor.preview_from[2];
        y2 = to[1] + to[3] > compositor.preview_from[1] + compositor.preview_from[3] ? to[1] + to[3]
                                                                                      : compositor.preview_from[1] + compositor.preview_from[3];
        compositor_damage(x1, y1, x2 - x1, y2 - y1);
        if (now - compositor.preview_at < (positive)ui_paced(UI_WINDOW_NS))
                return;
        if (compositor.preview_fading || compositor.preview[2] <= 0)
                compositor.preview_draw = 0;
        else
        {
                memory_copy(compositor.preview_from, compositor.preview, sizeof(compositor.preview));
                compositor.preview_from_alpha = 100;
                compositor.preview_at = 0;
        }
}

/*
        Native Canvas external-surface row state & host callback table:
        unified per-row state + vtable letting Wayland, X11, Proton, and native external clients
        plug into the compositor without protocol-specific backdoor declarations.
*/
enum
{
        CANVAS_EXT_LENT        = 1u << 0,
        CANVAS_EXT_STRAIGHT    = 1u << 1,
        CANVAS_EXT_SWAP        = 1u << 2,
        CANVAS_EXT_BLEND       = 1u << 3,
        CANVAS_EXT_ASYNC       = 1u << 4,
        CANVAS_EXT_UNSCANNABLE = 1u << 5,
        CANVAS_EXT_XOVERRIDE   = 1u << 6,
};

struct canvas_ext_row_state
{
        const unsigned int *pixels;
        unsigned long pitch;
        positive gpu_va;
        unsigned int gpu_epoch, flags;
        unsigned int scan_fb, scan_x, scan_y;
        unsigned int tiled_kind, tiled_desc[8];
};

struct canvas_ext_ops
{
        const unsigned int *(*lent_info)(int row, unsigned long *pitch, positive *va);
        unsigned int (*row_flags)(int row, unsigned int mask);
        unsigned int (*scan_fb)(int row, int opaque);
        void (*scan_origin)(int row, int *x, int *y);
        void (*scan_fail)(int row);
        int (*row_opaque)(int row, int x1, int y1, int x2, int y2);
        void (*settle)(int row);
        void (*straight_pays)(int keep);
        void (*straight_settle)(int row, long y0, long y1);
        unsigned int (*straight_desc)(int row, unsigned int desc[8]);
        int (*held)(void);
        void (*count_card)(const unsigned int *rows, unsigned int windows);
        void (*lent_sync)(int start);
        void (*tiled_settle)(void);
        unsigned int (*layers_frames)(int row, struct compositor_frame *frames, unsigned int *rows, unsigned int at);
        void (*gathers_fill)(void);
        int (*row_parts)(int row, const int (**rects)[4]);
        int (*asked_shape)(int row);
};

static struct canvas_ext_row_state *canvas_ext_rows;
static unsigned long canvas_ext_rows_bytes;
static unsigned int canvas_ext_rows_count;
static const struct canvas_ext_ops *canvas_ext_host;

static void canvas_ext_register(const struct canvas_ext_ops *ops)
{
        canvas_ext_host = ops;
}

static struct canvas_ext_row_state *canvas_ext_row_at(int row)
{
        unsigned int want;

        if (row < 0)
                return 0;
        if ((unsigned int)row < canvas_ext_rows_count)
                return &canvas_ext_rows[row];
        want = compositor_rows_in_use > (unsigned int)row ? compositor_rows_in_use : (unsigned int)row + 1;
        {
                void *p = canvas_ext_rows;

                if (!mem_buf_grow(&p, &canvas_ext_rows_bytes, (unsigned long)want * sizeof(*canvas_ext_rows), MEM_BUF_KEEP))
                        return 0;
                canvas_ext_rows = (struct canvas_ext_row_state *)p;
                canvas_ext_rows_count = want;
        }
        return &canvas_ext_rows[row];
}

static const struct canvas_ext_row_state *canvas_ext_row_peek(int row)
{
        return row >= 0 && (unsigned int)row < canvas_ext_rows_count ? &canvas_ext_rows[row] : 0;
}

static void canvas_ext_row_clear(int row)
{
        if (row >= 0 && (unsigned int)row < canvas_ext_rows_count)
                memory_zero(&canvas_ext_rows[row], sizeof(canvas_ext_rows[row]));
}

static int canvas_ext_row_has(int row, unsigned int flag)
{
        const struct canvas_ext_row_state *s = canvas_ext_row_peek(row);

        return s && (s->flags & flag) != 0;
}

// Protocol-neutral external-surface dispatchers backed by canvas_ext_row_state and canvas_ext_host.
static inline unsigned int canvas_ext_flags_of(int row, unsigned int mask)
{
        if (canvas_ext_host && canvas_ext_host->row_flags)
                return canvas_ext_host->row_flags(row, mask);
        {
                const struct canvas_ext_row_state *s = canvas_ext_row_peek(row);

                return s ? (s->flags & mask) : 0u;
        }
}

static inline int compositor_row_xoverride(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_XOVERRIDE) & CANVAS_EXT_XOVERRIDE) != 0;
}

static const unsigned int *compositor_lent_pixels(int row, unsigned long *pitch)
{
        if (canvas_ext_host && canvas_ext_host->lent_info)
                return canvas_ext_host->lent_info(row, pitch, 0);
        {
                const struct canvas_ext_row_state *s = canvas_ext_row_peek(row);

                if (!s || !(s->flags & CANVAS_EXT_LENT))
                        return 0;
                if (pitch)
                        *pitch = s->pitch;
                return s->pixels;
        }
}

static positive compositor_lent_va(int row)
{
        positive va = 0;

        if (canvas_ext_host && canvas_ext_host->lent_info)
        {
                canvas_ext_host->lent_info(row, 0, &va);
                return va;
        }
        {
                const struct canvas_ext_row_state *s = canvas_ext_row_peek(row);

                return s && (s->flags & CANVAS_EXT_LENT) ? s->gpu_va : 0;
        }
}

static inline int compositor_lent(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_LENT) & CANVAS_EXT_LENT) != 0;
}

static inline int compositor_lent_swap(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_SWAP) & CANVAS_EXT_SWAP) != 0;
}

static inline int compositor_client_scans(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_LENT | CANVAS_EXT_STRAIGHT) & (CANVAS_EXT_LENT | CANVAS_EXT_STRAIGHT)) != 0;
}

static inline int compositor_wl_row_async(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_ASYNC) & CANVAS_EXT_ASYNC) != 0;
}

static inline int compositor_wl_row_unscannable(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_UNSCANNABLE) & CANVAS_EXT_UNSCANNABLE) != 0;
}

static inline int compositor_wl_row_blend(int row)
{
        return (canvas_ext_flags_of(row, CANVAS_EXT_BLEND) & CANVAS_EXT_BLEND) != 0;
}

static unsigned int compositor_lent_fb(int row)
{
        return canvas_ext_host && canvas_ext_host->scan_fb ? canvas_ext_host->scan_fb(row, -1) : 0;
}

static unsigned int compositor_lent_fb_opaque(int row)
{
        return canvas_ext_host && canvas_ext_host->scan_fb ? canvas_ext_host->scan_fb(row, 1) : 0;
}

static void compositor_lent_fb_origin(int row, int *x, int *y)
{
        *x = *y = 0;
        if (canvas_ext_host && canvas_ext_host->scan_origin)
                canvas_ext_host->scan_origin(row, x, y);
}

static void compositor_lent_fb_fail(int row)
{
        if (canvas_ext_host && canvas_ext_host->scan_fail)
                canvas_ext_host->scan_fail(row);
}

static void compositor_wl_straight_pays(int keep)
{
        if (canvas_ext_host && canvas_ext_host->straight_pays)
                canvas_ext_host->straight_pays(keep);
}

static void compositor_wl_straight_settle(int row, long y0, long y1)
{
        if (canvas_ext_host && canvas_ext_host->straight_settle)
                canvas_ext_host->straight_settle(row, y0, y1);
}

static unsigned int compositor_straight_desc(int row, unsigned int desc[8])
{
        return canvas_ext_host && canvas_ext_host->straight_desc ? canvas_ext_host->straight_desc(row, desc) : 0;
}

static int compositor_wl_row_opaque(int row, int x1, int y1, int x2, int y2)
{
        return canvas_ext_host && canvas_ext_host->row_opaque ? canvas_ext_host->row_opaque(row, x1, y1, x2, y2) : 0;
}

static int compositor_wl_held(void)
{
        return canvas_ext_host && canvas_ext_host->held ? canvas_ext_host->held() : 0;
}

static void compositor_wl_settle(int row)
{
        if (canvas_ext_host && canvas_ext_host->settle)
                canvas_ext_host->settle(row);
}

static void compositor_wl_count_card(const unsigned int *rows, unsigned int windows)
{
        if (canvas_ext_host && canvas_ext_host->count_card)
                canvas_ext_host->count_card(rows, windows);
}

static void compositor_wl_lent_sync(int start)
{
        if (canvas_ext_host && canvas_ext_host->lent_sync)
                canvas_ext_host->lent_sync(start);
}

static void compositor_wl_tiled_settle(void)
{
        if (canvas_ext_host && canvas_ext_host->tiled_settle)
                canvas_ext_host->tiled_settle();
}

static unsigned int compositor_wl_layers_frames(int row, struct compositor_frame *frames, unsigned int *rows, unsigned int at)
{
        return canvas_ext_host && canvas_ext_host->layers_frames ? canvas_ext_host->layers_frames(row, frames, rows, at) : 0;
}

static void compositor_wl_gathers_fill(void)
{
        if (canvas_ext_host && canvas_ext_host->gathers_fill)
                canvas_ext_host->gathers_fill();
}

static int compositor_wl_parts(int row, const int (**rects)[4])
{
        return canvas_ext_host && canvas_ext_host->row_parts ? canvas_ext_host->row_parts(row, rects) : 0;
}

static int compositor_wl_asked_shape(int row)
{
        return canvas_ext_host && canvas_ext_host->asked_shape ? canvas_ext_host->asked_shape(row) : -1;
}

// The buffer a window's picture is in, where the compositor made it: every row of it the output's width; a client's own buffer for a row that
// shows one (compositor_lent_pixels).
static unsigned int *compositor_pixels(int row)
{
        unsigned long lent_pitch;
        const unsigned int *lent = compositor_lent_pixels(row, &lent_pitch);

        if (lent)
                return (unsigned int *)lent;
        return (unsigned int *)((char *)compositor.table + compositor.pixels +
                                ((unsigned long)row * 2 + compositor.front[row]) * compositor.buffer);
}

/*
        A window composed again and again is composed from its runs. What a window shows is a picture that holds until its program
        gives another (a frame), and a drag, a glide or an uncovering composes the same picture each present: the whole of it, at a
        new place. The third present that composes a picture reads its runs off it (after the compose, on the pool), and from the
        next the stretches of one colour are filled where they were copied. A picture composed once or twice, which is every
        picture of a window that scrolls, is only counted. A run row is the compositor's own: it is made from the pixels, not
        taken from the table, so it is never wrong about them, and it is dropped when the picture changes (the tag is the
        frame and the buffer), when the compositor writes into the buffer (compositor_stretch, the tabs of a direct window),
        and with the window.
*/
#define COMPOSITOR_RUNS_AFTER 3
#define COMPOSITOR_RUNS_WIDTH 4096
#define COMPOSITOR_RUNS_WORTH 30 // the percent of a picture its runs must cover for the compose to use them

static void compositor_runs_drop(int row)
{
        void *p = compositor.runs[row];

        mem_buf_free(&p, &compositor.runs_bytes[row], 0);
        compositor.runs[row] = 0;
        compositor.runs_tag[row] = compositor.runs_composed[row] = compositor.runs_state[row] = 0;
}

// The picture is not what its runs were read from.
static void compositor_runs_forget(int row)
{
        if (row >= 0)
                compositor.runs_tag[row] = compositor.runs_composed[row] = compositor.runs_state[row] = 0;
}

// A window's run rows, mapped for the most rows a window's buffer holds; none when the map is refused.
static struct compositor_run_row *compositor_runs_of(int row)
{
        unsigned long rows = compositor_buffer_room() / ((unsigned long)compositor_stride() * 4);
        unsigned long bytes = rows * sizeof(struct compositor_run_row);
        void *was = compositor.runs[row], *p = was;

        if (!mem_buf_grow(&p, &compositor.runs_bytes[row], bytes, 0))
        {
                compositor.runs[row] = 0;
                compositor.runs_tag[row] = compositor.runs_composed[row] = compositor.runs_state[row] = 0;
                return 0;
        }
        if (p != was)
                compositor.runs_tag[row] = compositor.runs_composed[row] = compositor.runs_state[row] = 0;
        compositor.runs[row] = (struct compositor_run_row *)p;
        return compositor.runs[row];
}

struct compositor_learning
{
        struct compositor_run_row *rows;
        const unsigned int *from;
        unsigned long pitch, width;
        unsigned int height, bands;
};

static void compositor_learn_band(address_any context, positive index)
{
        struct compositor_learning *learning = (struct compositor_learning *)context;
        unsigned long from = (unsigned long)learning->height * index / learning->bands;
        unsigned long to = (unsigned long)learning->height * (index + 1) / learning->bands;

        memory_runs_u32((unsigned char *)(learning->rows + from), learning->from + from * learning->pitch, learning->pitch, learning->width,
                        to - from);
}

// Whether a frame is a window's own picture at its own size, composed as it is in the table's buffer, and large enough to count.
static int compositor_runs_eligible(int row, const struct compositor_frame *frame)
{
        return !frame->builtin && !frame->blend && frame->pixels && frame->pixels == compositor_pixels(row) && !compositor_scaled(frame) && !compositor_lent(row) &&
               frame->width >= 64 && frame->width <= COMPOSITOR_RUNS_WIDTH && frame->height >= 16 &&
               (unsigned long)frame->width * (unsigned long)frame->height * 4 >= PARALLEL_MINIMUM_BYTES;
}

// Whether some of the damage is on the frame's rectangle: the present composes it.
static int compositor_runs_damaged(const struct compositor_frame *frame)
{
        for (unsigned int at = 0; at < compositor.damages; at++)
                if (compositor.damage[at][0] < frame->x + frame->width && compositor.damage[at][2] > frame->x &&
                    compositor.damage[at][1] < frame->y + frame->height && compositor.damage[at][3] > frame->y)
                        return 1;
        return 0;
}

// Before the compose: each window's frame takes its runs when it has them, and the presents that compose its picture are counted.
static void compositor_runs_note(struct compositor_frame *frames, const unsigned int *rows, unsigned int windows)
{
        for (unsigned int at = 0; at < windows; at++)
        {
                int row = rows[at];
                unsigned int tag;

                frames[at].runs = 0;
                if (!compositor_runs_eligible(row, &frames[at]))
                        continue;
                tag = compositor.seen[row] * 2 + (compositor.front[row] & 1u) + 1;
                if (compositor.runs_tag[row] != tag)
                {
                        compositor.runs_tag[row] = tag;
                        compositor.runs_composed[row] = compositor.runs_state[row] = 0;
                }
                if (compositor.runs_composed[row] < COMPOSITOR_RUNS_AFTER && compositor_runs_damaged(&frames[at]))
                        compositor.runs_composed[row]++;
                if (compositor.runs_state[row] == 1)
                        frames[at].runs = compositor.runs[row];
        }
}

// After the compose: the picture composed enough times is read for its runs, once, over its whole height.
static void compositor_runs_learn(const struct compositor_frame *frames, const unsigned int *rows, unsigned int windows)
{
        for (unsigned int at = 0; at < windows; at++)
        {
                int row = rows[at];
                struct compositor_run_row *runs;
                struct compositor_learning learning;
                positive bands = parallel_width();
                unsigned long covered = 0, height = (unsigned long)frames[at].height;

                if (compositor.runs_state[row] || compositor.runs_composed[row] < COMPOSITOR_RUNS_AFTER ||
                    !compositor_runs_eligible(row, &frames[at]) || !(runs = compositor_runs_of(row)))
                        continue;
                learning = (struct compositor_learning){runs, frames[at].pixels, frames[at].pitch, (unsigned long)frames[at].width,
                                                        (unsigned int)height, 1};
                bands = bands < COMPOSITOR_BANDS ? bands : COMPOSITOR_BANDS;
                bands = bands < (positive)height / 16 ? bands : (positive)height / 16;
                if (bands >= 2)
                {
                        learning.bands = (unsigned int)bands;
                        parallel_for(compositor_learn_band, &learning, bands, (positive)frames[at].width * (positive)height * 4);
                }
                else
                        compositor_learn_band(&learning, 0);
                // A picture of few runs (a photograph, a gradient) is composed as it always was: its runs there are the copy in
                // more calls.
                for (unsigned long y = 0; y < height; y++)
                        for (unsigned int k = 0; k < runs[y].count && k < COMPOSITOR_RUN_ROW; k++)
                                covered += runs[y].run[k].x1 - runs[y].run[k].x0;
                compositor.runs_state[row] = covered * 100 >= (unsigned long)frames[at].width * height * COMPOSITOR_RUNS_WORTH ? 1 : 2;
        }
}

/*
        A picture shown at a size it was not drawn at: past what its program
        drew, its last column again to the right and its last row again
        below, and short of it, cut. A size the compositor asked for is shown
        the moment it is asked -- the edge under the hand, a window filling
        the output -- and the program's picture at that size takes its place
        when it comes; for a terminal its last column and row are its paper,
        so what fills the gap is what will be there. Written into the buffer
        the window shows, which its program does not draw into: it draws into
        the other until this one is let go, and every buffer is the output's
        size, so there is room.
*/
static void compositor_stretch(int row)
{
        unsigned int *pixels = compositor_pixels(row);
        unsigned int drawn_w = compositor.picture_w[row], drawn_h = compositor.picture_h[row];
        unsigned int width = compositor.w[row], height = compositor.h[row];
        unsigned long stride = (unsigned long)compositor_stride();

        // A client's picture is never written: its window is the picture's size (compositor_wl_settle).
        // A fullscreen or background wallpaper window with a smaller buffer is aspect-fitted and scaled (compositor_fullscreen_fit), never edge-smeared.
        if (!drawn_w || !drawn_h || compositor_lent(row) || compositor.fullscreen[row] || compositor.layer[row] == WINDOW_LAYER_BACKGROUND)
                return;
        compositor_runs_forget(row);

        if (width > drawn_w)
                for (unsigned int line = 0; line < drawn_h && line < height; line++)
                        memory_fill_u32(pixels + line * stride + drawn_w, width - drawn_w, pixels[line * stride + drawn_w - 1]);
        for (unsigned int line = drawn_h; line < height; line++)
                memory_copy(pixels + line * stride, pixels + (drawn_h - 1) * stride, (positive)width * 4);
}

static int compositor_output_scale(int at);

// The width of a lit edge or corner in device pixels at a scale in percent: the setting's (compositor.border, the standard when
// none is kept), its logical pixels cut to whole ones and never less than one, so a width of 1 is the line's own pixel as before.
static int compositor_lit_px(int percent)
{
        int px = (int)(compositor.border ? compositor.border : COMPOSITOR_BORDER_STANDARD) * percent / 100;

        return px < 1 ? 1 : px;
}

static int compositor_fullscreen_fit(int row, int *dx, int *dy, int *dw, int *dh)
{
        unsigned int pw, ph, w, h;
        unsigned long lw, lh;

        if (row < 0 || row >= (int)compositor_rows_in_use ||
            (!compositor.fullscreen[row] && compositor.layer[row] != WINDOW_LAYER_BACKGROUND))
                return 0;
        pw = compositor.picture_w[row], ph = compositor.picture_h[row], w = compositor.w[row], h = compositor.h[row];
        if (!pw || !ph || !w || !h || (pw == w && ph == h))
                return 0;
        lw = w, lh = h;
        if ((unsigned long)pw * lh >= (unsigned long)ph * lw)
                lh = ((unsigned long)ph * lw + pw / 2) / pw;
        else
                lw = ((unsigned long)pw * lh + ph / 2) / ph;
        lw = lw < 1 ? 1 : lw > w ? w : lw, lh = lh < 1 ? 1 : lh > h ? h : lh;
        *dw = (int)lw, *dh = (int)lh;
        *dx = compositor.x[row] + (int)((w - lw) / 2), *dy = compositor.y[row] + (int)((h - lh) / 2);
        return 1;
}

/*
        A fullscreen window whose client shows a smaller buffer at its surface's size (a Wayland viewport scaling the whole buffer, the screen's
        shape: Xwayland's mode for a game at a lower resolution, 1280x720 shown at 1920x1080) is taken as a picture of the buffer's size
        (wayland_host.c, compositor_wl_view_take), which the display's plane scales to the screen straight from the client's buffer, or the
        card's compose does, read in place, where each frame was copied and scaled into the table first. Its client is still spoken to in its
        surface's size, which the shim keeps for the row and the picture size it was taken at (compositor_wl_client_size).
*/
#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_client_size(int row, unsigned int *cw, unsigned int *ch); // wayland_host.c: the surface's size for that picture
#endif

// The size a row's client is spoken to in, for a fullscreen picture fitted to the screen: its surface's, or the picture's.
static void compositor_client_size(int row, unsigned int *cw, unsigned int *ch)
{
        *cw = compositor.picture_w[row], *ch = compositor.picture_h[row];
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_client_size(row, cw, ch);
#endif
}

static void compositor_window_point(int row, int sx, int sy, int *wx, int *wy)
{
        int dx, dy, dw, dh;

        if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh))
        {
                int rx = sx - dx, ry = sy - dy;
                unsigned int cw, ch;

                compositor_client_size(row, &cw, &ch);
                rx = rx < 0 ? 0 : rx >= dw ? dw - 1 : rx, ry = ry < 0 ? 0 : ry >= dh ? dh - 1 : ry;
                *wx = (int)((long)rx * (long)cw / (long)dw);
                *wy = (int)((long)ry * (long)ch / (long)dh);
                return;
        }
        *wx = sx - compositor.x[row], *wy = sy - compositor.y[row];
}

static int compositor_screen_point(int row, int wx, int wy, int *sx, int *sy)
{
        int dx, dy, dw, dh;

        if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh))
        {
                unsigned int cw, ch;

                compositor_client_size(row, &cw, &ch);
                if (wx < 0 || wx >= (int)cw || wy < 0 || wy >= (int)ch)
                        return 0;
                *sx = dx + (int)((long)wx * (long)dw / (long)cw);
                *sy = dy + (int)((long)wy * (long)dh / (long)ch);
                return 1;
        }
        if (wx < 0 || wx >= (int)compositor.w[row] || wy < 0 || wy >= (int)compositor.h[row])
                return 0;
        *sx = compositor.x[row] + wx, *sy = compositor.y[row] + wy;
        return 1;
}

// Per-present row preparation: settles lent external buffers on resize and publishes per-window scale changes.
static void compositor_row_prepare(int row)
{
        int scale;

        compositor_wl_settle(row);
        scale = compositor_row_scale(row);
        if (compositor.table)
        {
                if (compositor.table->pid[row] && compositor.table->window_scale[row] != (unsigned int)scale)
                        rows_add(&compositor.woken, row);
                compositor.table->window_scale[row] = (unsigned int)scale;
        }
}

// Pure geometry & frame descriptor query for row (no buffer-settle or table/woken side effects).
/*
        A row's shape: the rectangles of its picture that are the window, x, y, width and height in the picture's pixels (an X
        window's bounding shape, the SHAPE extension, as xwm.c reads it). Wine cuts windows so: the Skyrim launcher's highlight is
        a 285 by 60 picture, black inside a frame of four rectangles, and drawn whole it hid the button under it. The rest of a
        shaped row's rectangle is not the window's and shows what is under it. The list grows to the highest row ever shaped.
*/
struct compositor_cut
{
        int (*rect)[4];
        unsigned long bytes;
        unsigned int count, shaped;
};
static struct compositor_cut *compositor_cuts;
static unsigned long compositor_cuts_bytes;
static unsigned int compositor_cuts_rows;

static unsigned int compositor_row_shape(int row, const int (**rect)[4], unsigned int *count)
{
        if (row < 0 || (unsigned int)row >= compositor_cuts_rows || !compositor_cuts[row].shaped)
                return 0;
        *rect = (const int (*)[4])compositor_cuts[row].rect, *count = compositor_cuts[row].count;
        return 1;
}

static int compositor_row_shaped(int row)
{
        return row >= 0 && (unsigned int)row < compositor_cuts_rows && compositor_cuts[row].shaped;
}

/*
        Controls for a window that asked for the manager's frame and draws none: an X window whose Motif hints left the frame on
        (Aseprite, a Wine program, a game in a window), set by xwm (compositor_controls_want). Canvas gives it no title bar of its own.
        It reads the window's picture instead: where the window's top at its start corner (the top right, the top left for a
        language read from the right) is a bar of one colour with nothing drawn on it, COMPOSITOR_CONTROLS_LEAST to COMPOSITOR_CONTROLS_MOST
        logical pixels deep (a menu bar's empty end, a strip of title), the close and minimise buttons are laid over it, sized to
        it (compositor_controls_apply). A press on the bar where its column is the bar's colour to the bar's foot moves the window,
        and two maximise it, as on a title bar (compositor_controls_grabs). Anything the window drew there (a word, a button, an edge)
        is the window's and takes the press. A colour deeper than the most (a game's sky) is no bar. A window with no bar found
        gets neither, and keeps the alt hold.
*/
#define COMPOSITOR_CONTROLS_LEAST 14 // logical pixels: a bar shallower than this has no room for a button
#define COMPOSITOR_CONTROLS_MOST 48  // logical pixels: one colour deeper than this is a picture, not a bar
#define COMPOSITOR_CONTROLS_SKIP 2   // logical pixels at the top read past: a window's own edge line
#define COMPOSITOR_CONTROLS_NEAR 12  // a channel within this of the bar's is the bar's colour
#define COMPOSITOR_CONTROLS_EVEN 2   // logical pixels the columns of a bar may end apart: its foot, drawn smooth

struct compositor_controls_row
{
        unsigned int ink;    // the bar's colour, 0xrrggbb
        unsigned short band; // the bar's depth from the window's top in device pixels; 0: none found
        unsigned char want;  // the window asked for the manager's frame
        unsigned char read;  // the bar has been read since the window asked
};
static struct compositor_controls_row *compositor_controls;

static int compositor_controls_wanted(int row)
{
        return compositor_controls && row >= 0 && row < COMPOSITOR_ROWS && compositor_controls[row].want;
}

static void compositor_controls_want(int row, int want)
{
        if (!compositor_controls || row < 0 || row >= COMPOSITOR_ROWS || compositor_controls[row].want == (want != 0))
                return;
        compositor_controls[row] = (struct compositor_controls_row){0, 0, (unsigned char)(want != 0), 0};
        compositor_damage_row(row);
}

// The buttons on the bar found, where every window of the system has them: the kit's size and its inset from the top and the side
// (compositor_close_box's own place) when the bar is as deep as that; on a shallower bar the kit's inset across and as large as the bar
// leaves room for, centred down it.
static void compositor_controls_apply(int row, struct compositor_frame *frame)
{
        int band, in, box, side;

        if (!compositor_controls || !compositor_controls[row].want)
                return;
        frame->close = frame->minimize = 0;
        if (!(band = compositor_controls[row].band) || compositor.fullscreen[row])
                return;
        box = UI_CLOSE * frame->scale / 100, in = UI_CLOSE_IN * frame->scale / 100;
        frame->close = 1, frame->minimize = !(compositor.asks[row] & WINDOW_NO_TAB);
        if (band >= box + in)
                return; // the kit's place: ctl_side 0
        side = band - 2;
        if (side < 4)
        {
                frame->close = frame->minimize = 0;
                return;
        }
        frame->ctl_side = side, frame->ctl_in = in, frame->ctl_top = (band - side) / 2;
}

static int compositor_controls_near(unsigned int a, unsigned int b)
{
        int dr = (int)(a >> 16 & 255) - (int)(b >> 16 & 255), dg = (int)(a >> 8 & 255) - (int)(b >> 8 & 255), db = (int)(a & 255) - (int)(b & 255);

        return dr <= COMPOSITOR_CONTROLS_NEAR && dr >= -COMPOSITOR_CONTROLS_NEAR && dg <= COMPOSITOR_CONTROLS_NEAR && dg >= -COMPOSITOR_CONTROLS_NEAR &&
               db <= COMPOSITOR_CONTROLS_NEAR && db >= -COMPOSITOR_CONTROLS_NEAR;
}

// The pitch of a glass window's composed picture, in pixels: its width in whole 64-byte lines. The card finds a picture's row only where the row
// begins on a line (i915's block reads: a row elsewhere in a line comes back in pieces from up to 15 pixels off, and a glass window composed at its
// own width drew as a sawtooth on a Haswell), so the picture the scene reads is made with its rows on lines, whatever the window's width.
static inline unsigned long compositor_glass_pitch(int row)
{
        return ((unsigned long)compositor.w[row] + 15) & ~15ul;
}

// A glass row's ground: its backdrop risen to full size, tinted, with the grain, a picture as large as the composed one and kept after it
// (compositor_glass_room). The program's picture laid over it is the window: the card lays it (the frame's glass_under and glass_over), and the
// composed picture is made of the two only where the processor reads it (compositor_glass_settle, glass.c).
static inline unsigned int *compositor_glass_ground(int row)
{
        return compositor.composed[row] ? compositor.composed[row] + compositor_glass_pitch(row) * (unsigned long)compositor.h[row] : null;
}
static void compositor_glass_settle(int row);
static void compositor_glass_settle_rect(int row, const int *c);
static void compositor_glass_settle_frames(const struct compositor_frame *frames, unsigned int count);
static int compositor_glass_lazy; // the frames being built are a present's: glass is left as layers, and the present settles what its path reads

static void compositor_frame_of(int row, struct compositor_frame *frame)
{
        int line = compositor_frame_line(row), scale = compositor_row_scale(row);
        unsigned long lent_pitch;

        // The corners are the window's own: UI_RADIUS at its scale, whatever the line comes to (it is a pixel at the least).
        *frame = (struct compositor_frame){compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                           (int)compositor.h[row], scale, compositor_pixels(row),
                                           (unsigned long)compositor_stride(), 0, null, 0,
                                           compositor_rounded(row) ? UI_RADIUS * scale / 100 : 0, line,
                                           row == compositor.focus ? compositor_line_lit() : compositor_line_unlit()};
        if (compositor_lent_pixels(row, &lent_pitch))
                frame->pitch = lent_pitch, frame->gpu_base = compositor_lent_va(row), frame->swap = compositor_lent_swap(row);
        frame->blend = compositor_wl_row_blend(row);
        frame->row_plus1 = row + 1;
        frame->close = !(compositor.asks[row] & WINDOW_NO_CLOSE);
        frame->minimize = !(compositor.asks[row] & WINDOW_NO_TAB);
        compositor_controls_apply(row, frame);
        if (compositor_row_shape(row, &frame->shape, &frame->shape_count))
        {
                frame->shaped = 1, frame->shape_w = (int)compositor.picture_w[row], frame->shape_h = (int)compositor.picture_h[row];
                frame->radius = 0, frame->line = line = 0, frame->close = 0, frame->minimize = 0;
        }
        compositor_land_frame(row, frame);
        if (compositor.glide[row] && !compositor.landing[row] && (row != compositor.drag || compositor.drag_glide == row + 1))
                compositor_glide_frame(row, frame);
        frame->x += compositor_roll_dx(row);
        if (!compositor.landing[row] && !compositor.glide[row])
        {
                int dx, dy, dw, dh;

                if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh))
                {
                        frame->x = dx, frame->y = dy, frame->width = dw, frame->height = dh;
                        frame->source_w = (int)compositor.picture_w[row], frame->source_h = (int)compositor.picture_h[row];
                        frame->runs = 0;
                }
        }
        // A scaled frame keeps its descriptor too: gpu_host's scaled pre-pass reads a tiled picture in place (gpu_image_scaled), and a linear
        // read of it would be garbage.
        frame->tiled_kind = compositor_straight_desc(row, frame->tiled_desc);
        // The edge or corner the hand holds or is over, or all of the line of a window being moved.
        if (row == compositor.lit_row && line)
        {
                frame->lit = compositor.lit_edges;
                frame->lit_ink = compositor.lit_held ? COMPOSITOR_INK_LINE_HELD : COMPOSITOR_INK_LINE_HOVER;
                frame->lit_w = compositor_lit_px(scale);
        }
        // The snap preview is the dragged window's, carried here for compositor_preview_lift to put under the window: where it
        // is drawn now (compositor_preview_shown), with a window's round corners (UI_RADIUS) and, for a tile, the window's line,
        // filling the screen with none, at the scale of the screen it is on, its line the tint's colour at COMPOSITOR_SNAP_LINE
        // percent of its strength (compositor_scene).
        if (row == compositor.preview_draw - 1)
        {
                int rect[4];
                unsigned int alpha = compositor_preview_shown(rect), ink = compositor_snap_ink(), a, edge;

                if (alpha && rect[2] > 0 && rect[3] > 0)
                {
                        unsigned int count;
                        const int (*screens)[4] = compositor_screens(&count);
                        int on = compositor_screen_owner(screens, count, rect[0], rect[1], rect[2], rect[3]);
                        int scale = compositor_output_scale(on) > 0 ? compositor_output_scale(on) : compositor.scale;
                        int fills = compositor.preview_how == COMPOSITOR_MAXIMIZED || compositor.preview_how == COMPOSITOR_ROOM;

                        a = (ink >> 24) * alpha / 100;
                        edge = (ink >> 24) * COMPOSITOR_SNAP_LINE / 100 > 255 ? 255 : (ink >> 24) * COMPOSITOR_SNAP_LINE / 100;
                        memory_copy(frame->preview, rect, sizeof(rect));
                        frame->preview_ink = (ink & 0xffffffu) | a << 24;
                        frame->preview_radius = UI_RADIUS * scale / 100;
                        frame->preview_line = fills ? 0 : compositor_line_px(scale);
                        frame->preview_edge = fills ? 0 : (ink & 0xffffffu) | (edge * alpha / 100) << 24;
                }
        }
        if (row == compositor.mark_row && compositor.mark[2] > 0)
        {
                memory_copy(frame->mark, compositor.mark, sizeof(frame->mark));
                frame->mark_ink = compositor_ink_selection();
        }
        // Glass is shown as its picture over its backdrop, composed at its own width.
        if (compositor.glassy[row] && compositor.composed[row] && compositor.glass_made[row][2] == (int)compositor.w[row] &&
            compositor.glass_made[row][3] == (int)compositor.h[row])
        {
                // A present's frame keeps the layers for the card (the picture the row had is the program's); any other reader is given the
                // composed picture made.
                if (compositor_glass_lazy && frame->pixels)
                        frame->glass_under = compositor_glass_ground(row), frame->glass_over = frame->pixels, frame->glass_over_pitch = frame->pitch;
                else
                        compositor_glass_settle(row);
                frame->pixels = compositor.composed[row];
                frame->pitch = compositor_glass_pitch(row);
        }
}

static void compositor_damage_row(int row)
{
        struct compositor_frame frame;
        int x, y, width, height;

        if (row < 0 || !compositor.showing[row])
                return;

        compositor_frame_of(row, &frame);
        compositor_outline(&frame, &x, &y, &width, &height);
        compositor_damage(x, y, width, height);
}

/*
        The bar under a window's buttons, read from its picture (compositor_controls): once when the window asks, and again whenever
        the present's damage meets the bar's room at the start corner, three buttons wide. Each column there is read down from
        past the window's edge line while it is the colour of the first; the bar is the shallowest column, kept when it is
        between the least and the most. A bar that comes, goes or changes depth or colour has the window drawn whole again.
*/
static void compositor_controls_read(struct compositor_frame *frame)
{
        int row = frame->row_plus1 - 1, scale = frame->scale, least, most, skip, reach, x0, band, step;
        unsigned int ink;

        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_controls || !compositor_controls[row].want || frame->builtin || frame->layer ||
            !frame->pixels || compositor_scaled(frame))
                return;
        least = COMPOSITOR_CONTROLS_LEAST * scale / 100, most = COMPOSITOR_CONTROLS_MOST * scale / 100;
        skip = COMPOSITOR_CONTROLS_SKIP * scale / 100 > 0 ? COMPOSITOR_CONTROLS_SKIP * scale / 100 : 1;
        reach = 3 * (UI_CLOSE + UI_CLOSE_IN) * scale / 100;
        x0 = language_rtl() ? skip : frame->width - reach - skip;
        if (frame->width < 4 * reach || frame->height < 2 * most || x0 < 0)
                band = 0, ink = 0;
        else
        {
                if (compositor_controls[row].read)
                {
                        int touched = 0;

                        for (unsigned int at = 0; at < compositor.damages && !touched; at++)
                        {
                                int box[4] = {frame->x + x0, frame->y, frame->x + x0 + reach, frame->y + most + 1};

                                compositor_meet(box, compositor.damage[at]);
                                touched = box[2] > box[0] && box[3] > box[1];
                        }
                        if (!touched)
                                return;
                }
                // A client's dma-buf mapped write-combined is read every other column: the bar is a run, and a word on it is wider.
                step = frame->gpu_base ? 2 : 1;
                ink = compositor_picture_at(frame, x0 + reach / 2, skip) & 0xffffffu;
                // A bar ends at one depth across its room: every column is the bar's colour down to its foot and no further (an edge, or
                // what is under the bar, starts there). A window's own button is not a bar: the columns through its glyph stop where the
                // glyph starts and the columns beside it run on down the caption, so the depths disagree (Chrome's close button, whose
                // X sits a dozen pixels down its caption, read as a bar to the glyph's top and had Canvas's buttons laid over its own).
                band = most + 1;
                {
                        int deepest = 0, even = COMPOSITOR_CONTROLS_EVEN * scale / 100 > 0 ? COMPOSITOR_CONTROLS_EVEN * scale / 100 : 1;

                        for (int x = x0; x < x0 + reach && band >= least; x += step)
                        {
                                int depth = skip;

                                while (depth <= most && compositor_controls_near(compositor_picture_at(frame, x, depth), ink))
                                        depth++;
                                band = depth < band ? depth : band;
                                deepest = depth > deepest ? depth : deepest;
                                if (deepest - band > even)
                                        band = 0;
                        }
                }
                if (band < least || band > most)
                        band = 0;
        }
        compositor_controls[row].read = 1;
        if ((unsigned int)band != compositor_controls[row].band || (band && ink != compositor_controls[row].ink))
        {
                compositor_controls[row].band = (unsigned short)band, compositor_controls[row].ink = ink;
                compositor_damage_row(row);
        }
        frame->ctl_side = frame->ctl_in = frame->ctl_top = 0;
        frame->close = !(compositor.asks[row] & WINDOW_NO_CLOSE), frame->minimize = !(compositor.asks[row] & WINDOW_NO_TAB);
        compositor_controls_apply(row, frame);
}

// A press at px, py on the bar found under a window's buttons, where its column, and the columns a few pixels either side, are the
// bar's colour from past the window's edge line to the bar's foot: nothing the window drew is there, so the press is the window's frame's.
static int compositor_controls_grabs(int row, int px, int py)
{
        struct compositor_frame frame;
        int band, dx, dy, half, skip;

        if (!compositor_controls || row < 0 || row >= COMPOSITOR_ROWS || !compositor_controls[row].want || !(band = compositor_controls[row].band) ||
            compositor.fullscreen[row])
                return 0;
        compositor_frame_of(row, &frame);
        dx = px - frame.x, dy = py - frame.y;
        if (!frame.pixels || compositor_scaled(&frame) || dx < 0 || dx >= frame.width || dy < 0 || dy >= band)
                return 0;
        half = 3 * frame.scale / 100 + 1, skip = COMPOSITOR_CONTROLS_SKIP * frame.scale / 100 > 0 ? COMPOSITOR_CONTROLS_SKIP * frame.scale / 100 : 1;
        for (int x = dx - half; x <= dx + half; x++)
                for (int y = skip; y < band; y++)
                        if (x >= 0 && x < frame.width && !compositor_controls_near(compositor_picture_at(&frame, x, y), compositor_controls[row].ink))
                                return 0;
        return 1;
}

/*
        A row's shape set from X's rectangles as a GetRectangles reply has them (8 bytes each, little-endian: x and y signed, width
        and height unsigned), or let go (shaped 0: the row is its whole rectangle again). The row is drawn again where it was and
        where it is. A shape whose list cannot be had is let go: the window is drawn whole, as before shapes were read.
*/
static void compositor_row_shape_set(int row, const unsigned char *x_rects, unsigned long count, unsigned int shaped)
{
        struct compositor_cut *s;
        int changed;

        if (row < 0 || (!shaped && (unsigned int)row >= compositor_cuts_rows))
                return;
        if ((unsigned int)row >= compositor_cuts_rows)
        {
                if (!mem_buf_grow((void **)&compositor_cuts, &compositor_cuts_bytes, ((unsigned long)row + 1) * sizeof(struct compositor_cut),
                                  MEM_BUF_GROW_2X | MEM_BUF_KEEP))
                        return;
                compositor_cuts_rows = (unsigned int)(compositor_cuts_bytes / sizeof(struct compositor_cut));
        }
        s = &compositor_cuts[row];
        if (shaped && count > 4096u)
                count = 4096u;
        if (shaped && count > s->bytes / sizeof(s->rect[0]) &&
            !mem_buf_grow((void **)&s->rect, &s->bytes, count * sizeof(s->rect[0]), MEM_BUF_GROW_2X))
                shaped = 0, count = 0, s->count = 0;
        if (!shaped)
                count = 0;
        changed = s->shaped != shaped || s->count != count;
        for (unsigned long at = 0; at < count; at++)
        {
                const unsigned char *r = x_rects + at * 8;
                int one[4] = {(short)(r[0] | r[1] << 8), (short)(r[2] | r[3] << 8), r[4] | r[5] << 8, r[6] | r[7] << 8};

                changed |= memory_compare(s->rect[at], one, sizeof one) != 0;
                memory_copy(s->rect[at], one, sizeof one);
        }
        if (!changed)
                return;
        if (compositor.showing[row])
                compositor_damage_row(row);
        s->shaped = shaped, s->count = (unsigned int)count;
        if (compositor.showing[row])
                compositor_damage_row(row);
}

// A window's line and its corners' squares, and not what is inside them: what lighting an edge draws again.
static void compositor_damage_line(int row)
{
        struct compositor_frame frame;
        int x, y, width, height, band;

        if (row < 0 || !compositor.showing[row] || !compositor_line(row))
                return;
        compositor_frame_of(row, &frame);
        compositor_outline(&frame, &x, &y, &width, &height);
        band = compositor_radius(&frame) > frame.x - x ? compositor_radius(&frame) : frame.x - x;
        if (frame.lit && frame.lit_w > band)
                band = frame.lit_w;
        compositor_damage(x, y, width, band);
        compositor_damage(x, y + height - band, width, band);
        compositor_damage(x, y + band, band, height - 2 * band);
        compositor_damage(x + width - band, y + band, band, height - 2 * band);
}

/*
        What of a window's line is lit (compositor_frame_of): the edges the hand holds or is over, all of it for a
        window being moved, held or only hovered; none with no edges. Where it changes, both lines are drawn again.
*/
static void compositor_lit(int row, unsigned int edges, unsigned int held)
{
        row = edges ? row : COMPOSITOR_NONE;
        held = row >= 0 ? held : 0;
        if (row == compositor.lit_row && (row < 0 || (edges == compositor.lit_edges && held == compositor.lit_held)))
                return;
        if (compositor.lit_edges)
                compositor_damage_line(compositor.lit_row);
        compositor.lit_row = row;
        compositor.lit_edges = row >= 0 ? edges : 0;
        compositor.lit_held = held;
        compositor_damage_line(row);
}

static int compositor_screen_of(int row);

// How many bands there are: one on every screen with `taskbar display all`, and the primary's alone otherwise.
static int compositor_bands(void)
{
        return taskbar_display_all() ? compositor_output_count() : 1;
}

// The windows that have a tab, in the order they first showed, and what each tab shows: every window on the one band,
// and on each of several, the windows whose screen it is on (compositor_screen_of).
static int *compositor_popup_parent;
// The window a window with no place of its own is centred on when it first shows (a dialog's, a transient's parent): its row + 1, 0 for none.
static int *compositor_owner;
static int compositor_popup_top(int root);
static b32 compositor_popups_close(int root, int keep);
static b32 compositor_popup_first(int row);
static void compositor_popup_leave(int row);

/*
        The launcher that is up: the window that asked to be put away by a press anywhere but on it and has no close button
        (WINDOW_DISMISS and WINDOW_NO_CLOSE, which only the launcher asks for both of), unless it has been asked to go.
        The row, or COMPOSITOR_NONE.
*/
static int compositor_launcher_row(void)
{
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.showing[row] && compositor_popup_parent[row] < 0 && !(compositor.state[row] & (WINDOW_CLOSING | WINDOW_MINIMIZED)) &&
                    (compositor.asks[row] & (WINDOW_DISMISS | WINDOW_NO_CLOSE)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE))
                        return row;
        return COMPOSITOR_NONE;
}

// The launcher kept up and put away (WINDOW_RESIDENT, minimized): its button shows it at once, with no program started.
static int compositor_launcher_kept(void)
{
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.showing[row] && compositor_popup_parent[row] < 0 && (compositor.state[row] & (WINDOW_CLOSING | WINDOW_MINIMIZED)) == WINDOW_MINIMIZED &&
                    (compositor.asks[row] & (WINDOW_DISMISS | WINDOW_NO_CLOSE | WINDOW_RESIDENT)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE | WINDOW_RESIDENT))
                        return row;
        return COMPOSITOR_NONE;
}

static unsigned int compositor_windows_for(int band)
{
        unsigned int count = 0;
        int bands = compositor_bands();

        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                unsigned int place = count;

                // A popup has no tab: it is its window's. Nor has a window that asked for none (WINDOW_NO_TAB), nor the launcher, whose tab
                // is the band's launcher button, lit while it is up (compositor_launcher_row's test: a window that is dismissed and has no close).
                if (!compositor.showing[row] || compositor_popup_parent[row] >= 0 || (compositor.asks[row] & WINDOW_NO_TAB) ||
                    (compositor.asks[row] & (WINDOW_DISMISS | WINDOW_NO_CLOSE)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE) ||
                    !compositor_space_row_active(row) ||
                    (bands > 1 && compositor_screen_of(row) != band))
                        continue;
                while (place && compositor.made[compositor.listed[place - 1]] > compositor.made[row])
                        place--;
                memory_copy(compositor.listed + place + 1, compositor.listed + place, (count - place) * sizeof(*compositor.listed));
                compositor.listed[place] = (unsigned int)row;
                count++;
        }

        for (unsigned int at = 0; at < count; at++)
        {
                int row = compositor.listed[at];

                compositor.marks[at] = (unsigned char)((row == compositor.focus ? COMPOSITOR_TAB_LIT : 0) |
                                                       (compositor.state[row] & WINDOW_MINIMIZED ? COMPOSITOR_TAB_AWAY : 0));
                compositor.lengths[at] = compositor_title_length(compositor.title[row], WINDOWS_TITLE);
        }

        compositor.listing = count;
        return count;
}

static unsigned int compositor_windows(void)
{
        return compositor_windows_for(compositor.band_at);
}

// Each tab's title, app_id, icon and its sequence, in the order the tabs are listed: the rows' (compositor_rows_make), one a row.
static const char **compositor_tab_titles, **compositor_tab_apps;
static const unsigned int **compositor_tab_icons;
static unsigned int *compositor_tab_seqs;

// What the taskbar is given of the compositor, filled for each call: the output, the band's buffers and the windows'
// tabs as compositor_windows lists them (the caller lists them first when they may have changed).
static struct taskbar_view compositor_taskbar_view_for(int band)
{
        struct taskbar_view view;

        memory_zero(&view, sizeof(view));
        // The primary screen's (`taskbar display primary`), or with a band on every screen the one asked of.
        compositor_output_rect(band, &view.x, &view.y, &view.width, &view.height);
        view.scale = compositor.scale;
        view.position = taskbar_position();
        view.pixels = band > 0 ? compositor.band_more[band] : compositor.band;
        view.band_bytes = band > 0 ? compositor.band_more_bytes[band] : compositor.band_bytes;
        view.glass = compositor.glass;
        view.face = &compositor.face;
        view.bold = compositor.bold_ok ? &compositor.bold : null;
        view.engine = compositor.engine;
        view.count = compositor.listing;
        view.titles = compositor_tab_titles, view.apps = compositor_tab_apps, view.icons = compositor_tab_icons, view.icon_seqs = compositor_tab_seqs;
        view.launcher_lit = compositor_launcher_row() >= 0;
        view.marks = compositor.marks;
        view.lengths = compositor.lengths;
        view.rows = compositor.listed;
        for (unsigned int at = 0; at < view.count; at++)
        {
                int row = compositor.listed[at];

                view.titles[at] = compositor.title[row];
                view.apps[at] = compositor.app_id[row];
                view.icons[at] = compositor.icon_seq[row] ? compositor.icon[row] : null;
                view.icon_seqs[at] = compositor.icon_seq[row];
        }
        // The pinned programs that have no window follow the windows, as many as the arrays have room for.
        view.count += taskbar_pins_tabs(&view, COMPOSITOR_ROWS > (int)view.count ? (unsigned int)COMPOSITOR_ROWS - view.count : 0, compositor.marks,
                                        compositor.lengths, compositor.listed);
        return view;
}

static struct taskbar_view compositor_taskbar_view(void)
{
        return compositor_taskbar_view_for(compositor.band_at);
}

static struct taskbar_view compositor_band_view_for(int band)
{
        compositor_windows_for(band);
        return compositor_taskbar_view_for(band);
}

static struct taskbar_view compositor_band_view(void)
{
        return compositor_band_view_for(compositor.band_at);
}

// The panel's band as a frame: the band's own pixels when there is a font to write the titles with, and shapes the
// scene draws when there is not.
static void compositor_band_frame_for(int band, struct compositor_frame *frame)
{
        struct taskbar_view view = compositor_band_view_for(band);
        unsigned char *marks = compositor.band_marks[band];
        int x, y, width, height;

        memory_copy(marks, compositor.marks, compositor.listing);
        taskbar_band(&view, &x, &y, &width, &height);
        *frame = (struct compositor_frame){x, y, width, height, compositor.scale, view.pixels,
                                           (unsigned long)width, !view.pixels, marks,
                                           compositor.listing};
        frame->position = view.position;
        frame->lengths = compositor.lengths;
        frame->launcher_lit = view.launcher_lit;
}

static void compositor_band_frame(struct compositor_frame *frame)
{
        compositor_band_frame_for(compositor.band_at, frame);
}

// Damage a rectangle in the overlay plane (above every window: bands, touch spots, chip).
static void compositor_damage_over(int x, int y, int width, int height)
{
        compositor_damage_from(x, y, width, height, COMPOSITOR_OVER);
}

// What the tabs show changed: the windows, which has the keys, which are put away, a title, the minute, the battery.
static void compositor_tabs_changed(void)
{
        taskbar_changed();
        for (int band = 0; band < compositor_bands(); band++)
        {
                struct taskbar_view view = compositor_taskbar_view_for(band);
                int x, y, width, height;

                taskbar_band(&view, &x, &y, &width, &height);
                compositor_damage_over(x, y, width, height);
        }
}

/*
        With a band on every screen, a window whose screen changed -- dragged, snapped, moved, carried -- has its tab
        on another band, and nothing of the move need reach either band: both are listed and drawn again, or one goes
        on showing the tab it lost, and a press there (which lists the band anew) raises or closes another window.
*/
static void compositor_bands_follow(void)
{
        int changed = 0;

        if (compositor_bands() < 2)
                return;
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                unsigned char screen = (unsigned char)(compositor.showing[row] ? compositor_screen_of(row) + 1 : 0);

                changed |= compositor.banded[row] != screen;
                compositor.banded[row] = screen;
        }
        if (changed)
                compositor_tabs_changed();
}

