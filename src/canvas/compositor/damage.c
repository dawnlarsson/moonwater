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
        What changed, as a few rectangles that do not overlap. A rectangle
        joins one it overlaps or lines up with, which costs nothing, and
        stays apart from the rest, so two windows at opposite corners are
        two small rectangles and not the screen between them. When the list
        is full the rectangle joins whichever grows least.
*/
static long compositor_area(const int *rectangle)
{
        return (long)(rectangle[2] - rectangle[0]) * (rectangle[3] - rectangle[1]);
}

static long compositor_joined(const int *a, const int *b, int *into)
{
        into[0] = a[0] < b[0] ? a[0] : b[0];
        into[1] = a[1] < b[1] ? a[1] : b[1];
        into[2] = a[2] > b[2] ? a[2] : b[2];
        into[3] = a[3] > b[3] ? a[3] : b[3];
        return compositor_area(into);
}

// A rectangle, x1, y1, x2, y2, grown to take in another; one with x2 not past x1 is none.
static void compositor_union(int *into, const int *other)
{
        if (other[2] <= other[0] || other[3] <= other[1])
                return;
        if (into[2] <= into[0] || into[3] <= into[1])
        {
                memory_copy(into, other, 4 * sizeof(int));
                return;
        }
        compositor_joined(into, other, into);
}

// A rectangle cut to another; none when they do not meet.
static void compositor_meet(int *into, const int *other)
{
        into[0] = into[0] > other[0] ? into[0] : other[0];
        into[1] = into[1] > other[1] ? into[1] : other[1];
        into[2] = into[2] < other[2] ? into[2] : other[2];
        into[3] = into[3] < other[3] ? into[3] : other[3];
        if (into[2] <= into[0] || into[3] <= into[1])
                into[0] = into[1] = into[2] = into[3] = 0;
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
static void compositor_glass_touch(const int *rectangle)
{
        int reach = COMPOSITOR_GLASS_REACH;
        int by = compositor.damage_by > 0 ? compositor.damage_by - 1 : COMPOSITOR_NONE;
        int lowest = by >= 0 ? compositor_stack_place(by) : -1;

        if (compositor.damage_by == COMPOSITOR_OVER)
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
                        if (compositor.glide[row] || compositor.landing[row])
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
                        // The minimise button is left of the close one: its damage is the pair's, the close button's right edge kept.
                        right = bx + side;
                        if (compositor_minimize_box(&frame, &mx, &my, &mside))
                                bx = mx;
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

static void compositor_damage(int x, int y, int width, int height)
{
        int rectangle[4] = {x < 0 ? 0 : x, y < 0 ? 0 : y,
                            x + width > compositor.width ? compositor.width : x + width,
                            y + height > compositor.height ? compositor.height : y + height};
        int joined[4];
        unsigned int at = 0;

        if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
                return;

        compositor_close_join(rectangle);
        compositor_glass_touch(rectangle);

        while (at < compositor.damages)
        {
                int *other = compositor.damage[at];

                if (compositor_joined(rectangle, other, joined) <=
                    compositor_area(rectangle) + compositor_area(other))
                {
                        memory_copy(rectangle, joined, sizeof(joined));
                        memory_copy(other, compositor.damage[--compositor.damages], sizeof(joined));
                        at = 0;
                }
                else
                        at++;
        }

        if (compositor.damages == COMPOSITOR_DAMAGE)
        {
                unsigned int best = 0;
                long least = -1;

                for (at = 0; at < COMPOSITOR_DAMAGE; at++)
                {
                        long grown = compositor_joined(rectangle, compositor.damage[at], joined) -
                                     compositor_area(compositor.damage[at]);

                        if (least < 0 || grown < least)
                        {
                                least = grown;
                                best = at;
                        }
                }

                compositor_joined(rectangle, compositor.damage[best], compositor.damage[best]);
                return;
        }

        memory_copy(compositor.damage[compositor.damages++], rectangle, sizeof(rectangle));
}

// Whether a window is on the screen: it has shown a picture and is not put away.
static int compositor_visible(int row)
{
        return compositor.showing[row] && !(compositor.state[row] & WINDOW_MINIMIZED);
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
        if (!compositor.land_at[row])
                return UI_ONE - COMPOSITOR_LAND_SMALL;
        done = ui_bezier_at(&ui_ease, ui_progress((b64)(clock_monotonic_nanoseconds() - compositor.land_at[row]), ui_paced(COMPOSITOR_LAND_NS)));
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
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
        {
                positive now;

                if (!compositor.landing[row])
                        continue;
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
        return at ? ui_bezier_at(&ui_ease, ui_progress((b64)(clock_monotonic_nanoseconds() - at), ui_paced(COMPOSITOR_GLIDE_NS))) : 0;
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
                for (int k = 0; k < 4; k++)
                        drawn[k] = origin[k] + (int)(((long long)(from[k] - origin[k]) * (long long)done) / (long long)UI_ONE);
                compositor_glide_damage(row, origin);
                from = drawn;
        }
        compositor.drag_glide = compositor.drag_glide == row + 1 ? 0 : compositor.drag_glide;
        compositor_glide_begin(row, from);
}

// A window's rectangle as it is in the frame: where it glides from, to where it is, by the ease of the time since its first present.
static void compositor_glide_frame(int row, struct compositor_frame *frame)
{
        int from[4];
        long long to[4] = {compositor.x[row], compositor.y[row], (long long)compositor.w[row], (long long)compositor.h[row]};
        long long at[4];
        b64 done = compositor_glide_done(compositor.glide_at[row]);

        compositor_glide_origin(row, from);
        for (int k = 0; k < 4; k++)
                at[k] = from[k] + ((long long)(to[k] - from[k]) * (long long)done) / (long long)UI_ONE;
        frame->x = (int)at[0], frame->y = (int)at[1], frame->width = (int)at[2], frame->height = (int)at[3];
        frame->source_w = (int)compositor.w[row], frame->source_h = (int)compositor.h[row];
}

/*
        After a present: a window still gliding is damaged again, so the next present draws it, and the one that ends it is
        drawn where it is. Nothing is damaged after that, and the loop goes idle.
*/
static void compositor_glide_keep(void)
{
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
        {
                positive now;

                if (!compositor.glide[row])
                        continue;
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
        done = compositor_glide_done(compositor.preview_at);
        if (compositor.preview_fading || compositor.preview[2] <= 0)
        {
                memory_copy(rect, compositor.preview_from, sizeof(compositor.preview_from));
                return compositor.preview_fading
                           ? (unsigned int)(compositor.preview_from_alpha -
                                            (long long)compositor.preview_from_alpha * (long long)done / (long long)UI_ONE)
                           : 0;
        }
        for (int k = 0; k < 4; k++)
                rect[k] = compositor.preview_from[k] +
                          (int)((long long)(compositor.preview[k] - compositor.preview_from[k]) * (long long)done / (long long)UI_ONE);
        return compositor.preview_from_alpha +
               (unsigned int)((long long)(100 - compositor.preview_from_alpha) * (long long)done / (long long)UI_ONE);
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
        Whether a row is an override-redirect X window's (a menu, a tooltip, a drag's window): it places itself and keeps its own grabs, as
        it does under any X window manager, so it stays over the windows the compositor arranges (a press that raises one does not put it
        under) and is not put away by a press elsewhere or its window losing the keys, as a popup of the compositor's is; its client ends
        it, as its focus leaves. None without the Wayland shim.
*/
#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_wl_row_xoverride(int row);
#define compositor_row_xoverride(row) compositor_wl_row_xoverride(row)
#else
#define compositor_row_xoverride(row) 0
#endif

// A row's picture the client's own buffer, read in place (wayland_host.c): its pixels and pitch, the card's address of them for a dma-buf, and
// whether the row's picture is one at all. A row that is not one has its picture in the table.
#ifdef CONFIG_MOONWATER_WAYLAND
static const unsigned int *compositor_lent_pixels(int row, unsigned long *pitch);
static positive compositor_lent_va(int row);
static int compositor_lent(int row);
static int compositor_lent_swap(int row);
static unsigned int compositor_lent_fb(int row);
static unsigned int compositor_lent_fb_opaque(int row);
static void compositor_lent_fb_origin(int row, int *x, int *y);
static void compositor_lent_fb_fail(int row);
static int compositor_client_scans(int row);
static void compositor_wl_straight_pays(int keep);
static void compositor_wl_straight_settle(int row, long y0, long y1);
static unsigned int compositor_straight_desc(int row, unsigned int desc[8]);
static int compositor_wl_row_async(int row);
static int compositor_wl_row_opaque(int row, int x1, int y1, int x2, int y2);
static int compositor_wl_row_unscannable(int row);
static int compositor_wl_held(void);
static int compositor_wl_row_blend(int row);
static void compositor_wl_settle(int row);
static void compositor_wl_count_card(const unsigned int *rows, unsigned int windows);
static void compositor_wl_lent_sync(int start);
static void compositor_wl_tiled_settle(void);
static unsigned int compositor_wl_layers_frames(int row, struct compositor_frame *frames, unsigned int *rows, unsigned int at);
static void compositor_wl_gathers_fill(void);
#else
static const unsigned int *compositor_lent_pixels(int row, unsigned long *pitch)
{
        (void)row, (void)pitch;
        return 0;
}
static positive compositor_lent_va(int row)
{
        (void)row;
        return 0;
}
static int compositor_lent(int row)
{
        (void)row;
        return 0;
}
static int compositor_lent_swap(int row)
{
        (void)row;
        return 0;
}
static unsigned int compositor_lent_fb(int row)
{
        (void)row;
        return 0;
}
static unsigned int compositor_lent_fb_opaque(int row)
{
        (void)row;
        return 0;
}
static void compositor_lent_fb_origin(int row, int *x, int *y)
{
        (void)row;
        *x = *y = 0;
}
static void compositor_lent_fb_fail(int row)
{
        (void)row;
}
static int compositor_client_scans(int row)
{
        (void)row;
        return 0;
}
static void compositor_wl_straight_pays(int keep)
{
        (void)keep;
}
static void compositor_wl_straight_settle(int row, long y0, long y1)
{
        (void)row, (void)y0, (void)y1;
}
static unsigned int compositor_straight_desc(int row, unsigned int desc[8])
{
        (void)row, (void)desc;
        return 0;
}
static int compositor_wl_row_async(int row)
{
        (void)row;
        return 0;
}
static int compositor_wl_row_opaque(int row, int x1, int y1, int x2, int y2)
{
        (void)row, (void)x1, (void)y1, (void)x2, (void)y2;
        return 0;
}
static int compositor_wl_row_unscannable(int row)
{
        (void)row;
        return 0;
}
static int compositor_wl_held(void)
{
        return 0;
}
static int compositor_wl_row_blend(int row)
{
        (void)row;
        return 0;
}
static void compositor_wl_settle(int row)
{
        (void)row;
}
static void compositor_wl_count_card(const unsigned int *rows, unsigned int windows)
{
        (void)rows, (void)windows;
}
static void compositor_wl_lent_sync(int start)
{
        (void)start;
}
static unsigned int compositor_wl_layers_frames(int row, struct compositor_frame *frames, unsigned int *rows, unsigned int at)
{
        (void)row, (void)frames, (void)rows, (void)at;
        return 0;
}
static void compositor_wl_gathers_fill(void)
{
}
#endif

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

// Aspect-preserving centered destination rectangle for a fullscreen window whose committed buffer is smaller/larger than its output.
static int compositor_fullscreen_fit(int row, int *dx, int *dy, int *dw, int *dh)
{
        unsigned int pw, ph, w, h;
        unsigned long lw, lh;

        if (row < 0 || row >= (int)compositor_rows_in_use ||
            (!compositor.fullscreen[row] && compositor.layer[row] != WINDOW_LAYER_BACKGROUND))
                return 0;
        pw = compositor.picture_w[row], ph = compositor.picture_h[row];
        w = compositor.w[row], h = compositor.h[row];
        if (!pw || !ph || !w || !h || (pw == w && ph == h))
                return 0;
        lw = w, lh = h;
        if ((unsigned long)pw * lh >= (unsigned long)ph * lw)
                lh = ((unsigned long)ph * lw + pw / 2) / pw;
        else
                lw = ((unsigned long)pw * lh + ph / 2) / ph;
        if (lw < 1)
                lw = 1;
        if (lh < 1)
                lh = 1;
        if (lw > w)
                lw = w;
        if (lh > h)
                lh = h;
        *dw = (int)lw;
        *dh = (int)lh;
        *dx = compositor.x[row] + (int)((w - lw) / 2);
        *dy = compositor.y[row] + (int)((h - lh) / 2);
        return 1;
}

// Screen coordinates mapped into a window's surface coordinates (scaling through compositor_fullscreen_fit when active).
static void compositor_window_point(int row, int sx, int sy, int *wx, int *wy)
{
        int dx, dy, dw, dh;

        if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh))
        {
                int rx = sx - dx, ry = sy - dy;

                if (rx < 0)
                        rx = 0;
                else if (rx >= dw)
                        rx = dw - 1;
                if (ry < 0)
                        ry = 0;
                else if (ry >= dh)
                        ry = dh - 1;
                *wx = (int)((long)rx * (long)compositor.picture_w[row] / (long)dw);
                *wy = (int)((long)ry * (long)compositor.picture_h[row] / (long)dh);
                return;
        }
        *wx = sx - compositor.x[row];
        *wy = sy - compositor.y[row];
}

// Inverse of compositor_window_point: surface coordinates mapped back to screen coordinates (0 if outside the surface).
static int compositor_screen_point(int row, int wx, int wy, int *sx, int *sy)
{
        int dx, dy, dw, dh;

        if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh))
        {
                if (wx < 0 || wx >= (int)compositor.picture_w[row] || wy < 0 || wy >= (int)compositor.picture_h[row])
                        return 0;
                *sx = dx + (int)((long)wx * (long)dw / (long)compositor.picture_w[row]);
                *sy = dy + (int)((long)wy * (long)dh / (long)compositor.picture_h[row]);
                return 1;
        }
        if (wx < 0 || wx >= (int)compositor.w[row] || wy < 0 || wy >= (int)compositor.h[row])
                return 0;
        *sx = compositor.x[row] + wx;
        *sy = compositor.y[row] + wy;
        return 1;
}

static void compositor_frame_of(int row, struct compositor_frame *frame)
{
        int line, scale;
        unsigned long lent_pitch;

        // A client's picture is the size the window is, or it is let go of into the table first (its size changed).
        compositor_wl_settle(row);
        line = compositor_frame_line(row), scale = compositor_row_scale(row);

        // The corners are the window's own: UI_RADIUS at its scale, whatever the line comes to (it is a pixel at the least).
        *frame = (struct compositor_frame){compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                           (int)compositor.h[row], scale, compositor_pixels(row),
                                           (unsigned long)compositor_stride(), 0, null, 0,
                                           compositor_rounded(row) ? UI_RADIUS * scale / 100 : 0, line,
                                           row == compositor.focus ? compositor_line_lit() : compositor_line_unlit()};
        if (compositor_lent_pixels(row, &lent_pitch))
                frame->pitch = lent_pitch, frame->gpu_base = compositor_lent_va(row), frame->swap = compositor_lent_swap(row);
        frame->blend = compositor_wl_row_blend(row);
        frame->close = !(compositor.asks[row] & WINDOW_NO_CLOSE);
        frame->minimize = !(compositor.asks[row] & WINDOW_NO_TAB);
        // A program is knocked when the scale its window is drawn at is not the one the table had: the kit lays its picture out again at it.
        if (compositor.table->pid[row] && compositor.table->window_scale[row] != (unsigned int)frame->scale)
                rows_add(&compositor.woken, row);
        compositor.table->window_scale[row] = (unsigned int)frame->scale;
        compositor_land_frame(row, frame);
        if (compositor.glide[row] && !compositor.landing[row] && (row != compositor.drag || compositor.drag_glide == row + 1))
                compositor_glide_frame(row, frame);
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
        if (!compositor_scaled(frame))
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
                frame->pixels = compositor.composed[row];
                frame->pitch = compositor.w[row];
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
                if (compositor.showing[row] && compositor_popup_parent[row] < 0 && !(compositor.state[row] & WINDOW_CLOSING) &&
                    (compositor.asks[row] & (WINDOW_DISMISS | WINDOW_NO_CLOSE)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE))
                        return row;
        return COMPOSITOR_NONE;
}

static unsigned int compositor_windows(void)
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
                    (bands > 1 && compositor_screen_of(row) != compositor.band_at))
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

// Each tab's title, app_id, icon and its sequence, in the order the tabs are listed: the rows' (compositor_rows_make), one a row.
static const char **compositor_tab_titles, **compositor_tab_apps;
static const unsigned int **compositor_tab_icons;
static unsigned int *compositor_tab_seqs;

// What the taskbar is given of the compositor, filled for each call: the output, the band's buffers and the windows'
// tabs as compositor_windows lists them (the caller lists them first when they may have changed).
static struct taskbar_view compositor_taskbar_view(void)
{
        struct taskbar_view view;

        memory_zero(&view, sizeof(view));
        // The primary screen's (`taskbar display primary`), or with a band on every screen the one asked of.
        compositor_output_rect(compositor.band_at, &view.x, &view.y, &view.width, &view.height);
        view.scale = compositor.scale;
        view.position = taskbar_position();
        view.pixels = compositor.band_at > 0 ? compositor.band_more[compositor.band_at] : compositor.band;
        view.band_bytes = compositor.band_at > 0 ? compositor.band_more_bytes[compositor.band_at] : compositor.band_bytes;
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

static struct taskbar_view compositor_band_view(void)
{
        compositor_windows();
        return compositor_taskbar_view();
}

// The panel's band as a frame: the band's own pixels when there is a font to write the titles with, and shapes the
// scene draws when there is not.
// The band compositor.band_at is (0, the primary's, when there is one), as a frame; its marks are kept apart, so
// each band's frames keep their own however many bands are listed after it.
static void compositor_band_frame(struct compositor_frame *frame)
{
        struct taskbar_view view = compositor_band_view();
        unsigned char *marks = compositor.band_marks[compositor.band_at];
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

// Damage a rectangle in the overlay plane (above every window: bands, touch spots, chip).
static void compositor_damage_over(int x, int y, int width, int height)
{
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(x, y, width, height);
        compositor.damage_by = 0;
}

// What the tabs show changed: the windows, which has the keys, which are put away, a title, the minute, the battery.
static void compositor_tabs_changed(void)
{
        taskbar_changed();
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();
                int x, y, width, height;

                taskbar_band(&view, &x, &y, &width, &height);
                compositor_damage_over(x, y, width, height);
        }
        compositor.band_at = 0;
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

