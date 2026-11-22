#ifndef MOONWATER_CANVAS_COMPOSITOR_PLACE_H
#define MOONWATER_CANVAS_COMPOSITOR_PLACE_H

/*
        The one placement for a popup or a menu: the xdg positioner's model. The anchor is a rect (or a point, of zero size); side picks the
        anchor's point on it (0 centre, 1 top, 2 bottom, 3 left, 4 right, 5 top-left, 6 bottom-left, 7 top-right, 8 bottom-right) and gravity
        the way the popup grows from that point, by the same nine values. An axis that does not fit the screen is flipped first, then slid
        onto it, then resized to it, each only when the adjust bits ask. A zero-size anchor is valid.
*/
#define COMPOSITOR_PLACE_FLIP_X 1u
#define COMPOSITOR_PLACE_SLIDE_X 2u
#define COMPOSITOR_PLACE_RESIZE_X 4u
#define COMPOSITOR_PLACE_FLIP_Y 8u
#define COMPOSITOR_PLACE_SLIDE_Y 16u
#define COMPOSITOR_PLACE_RESIZE_Y 32u

struct compositor_place
{
        int anchor[4];          // x, y, width, height of the rect the popup hangs off, in the screen's space (zero size is a point)
        unsigned int side;      // the anchor's point on that rect, 0 to 8 as above
        unsigned int gravity;   // the way the popup grows from that point, 0 to 8 as above
        int offset[2];          // a gap in pixels, along x and y
        unsigned int adjust;    // COMPOSITOR_PLACE_* bits
        int size[2];            // the wanted width and height
        int least[2], most[2];  // the size's bounds; most 0 is none
        int screen[4];          // the room to fit in: x, y, width, height
};

static const signed char compositor_place_dir[9][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

static inline long compositor_place_at(long anchor_at, long anchor_len, int side, int gravity, long off, long len, int flip)
{
        int a = flip ? -side : side, g = flip ? -gravity : gravity;

        return anchor_at + (a < 0 ? 0 : a > 0 ? anchor_len : anchor_len / 2) + (flip ? -off : off) - (g < 0 ? len : g > 0 ? 0 : len / 2);
}

static inline void compositor_place_axis(long anchor_at, long anchor_len, int side, int gravity, long off, long len, unsigned int flip_ok,
                                  unsigned int slide_ok, unsigned int resize_ok, long screen_at, long screen_len, long *at_out, long *len_out)
{
        long se = screen_at + screen_len, a = compositor_place_at(anchor_at, anchor_len, side, gravity, off, len, 0);
        int out = screen_at > a || a + len > se;

        if (out && flip_ok)
        {
                long b = compositor_place_at(anchor_at, anchor_len, side, gravity, off, len, 1);

                if (b >= screen_at && b + len <= se)
                        a = b, out = 0;
        }
        if (out && slide_ok)
        {
                long at = a;

                if (at + len > se)
                        at = se - len;
                if (at < screen_at)
                        at = screen_at;
                a = at, out = a < screen_at || a + len > se;
        }
        if (out && resize_ok)
        {
                long lo = a > screen_at ? a : screen_at, hi = a + len < se ? a + len : se;

                if (hi > lo)
                        a = lo, len = hi - lo;
        }
        *at_out = a, *len_out = len;
}

// The popup's rect, out[0..3], placed by p. Nothing is read from the table or the compositor: a caller that has the anchor and the room
// has the answer, so the wayland, kit and taskbar callers share it.
static inline void compositor_place_rect(const struct compositor_place *p, int out[4])
{
        unsigned int side = p->side < 9 ? p->side : 0, gravity = p->gravity < 9 ? p->gravity : 0;
        long x, y, w = p->size[0], h = p->size[1];

        if (w < p->least[0])
                w = p->least[0];
        if (p->most[0] > 0 && w > p->most[0])
                w = p->most[0];
        if (h < p->least[1])
                h = p->least[1];
        if (p->most[1] > 0 && h > p->most[1])
                h = p->most[1];
        compositor_place_axis(p->anchor[0], p->anchor[2], compositor_place_dir[side][0], compositor_place_dir[gravity][0], p->offset[0], w,
                              p->adjust & COMPOSITOR_PLACE_FLIP_X, p->adjust & COMPOSITOR_PLACE_SLIDE_X, p->adjust & COMPOSITOR_PLACE_RESIZE_X,
                              p->screen[0], p->screen[2], &x, &w);
        compositor_place_axis(p->anchor[1], p->anchor[3], compositor_place_dir[side][1], compositor_place_dir[gravity][1], p->offset[1], h,
                              p->adjust & COMPOSITOR_PLACE_FLIP_Y, p->adjust & COMPOSITOR_PLACE_SLIDE_Y, p->adjust & COMPOSITOR_PLACE_RESIZE_Y,
                              p->screen[1], p->screen[3], &y, &h);
        out[0] = (int)x, out[1] = (int)y, out[2] = (int)w, out[3] = (int)h;
}


#endif
