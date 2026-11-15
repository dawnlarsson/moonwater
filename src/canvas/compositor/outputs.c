/* ---- outputs: the screens one desktop spans ---- */

/*
        Every screen the card has connected is driven, each at its own mode,
        and the desktop is one space across them: a screen is a rectangle of
        it, x, y, width, height, and the space's top left is 0, 0. These are
        the rules of that space, each a function of the rectangles alone, so
        the checks hold them without a card:

        The arrangement. A screen the setting does not name is laid to the
        right of the ones before it, tops level, in the order the card lists
        its connectors; a named one comes first, in the setting's order, at
        the place it gives when it gives one, and dark when it says off. The
        first screen laid is the primary: the taskbar's, and where a window
        opens. The space is then moved so its top left is 0, 0.

        The pointer is on a screen and nowhere else: it crosses from one to
        the next only where their edges meet, and beside that part of an edge
        (a shorter screen's bottom, an offset one's corner, a gap) it stops,
        as at the desktop's own edge. A movement that ends off every screen
        is kept on the screen it started on; a point that came from nowhere
        (a tablet's, which spans the space) goes to the nearest point of the
        nearest screen.

        A window's screen is the one that holds the most of it, the lower
        index of two that hold as much, and the nearest when none holds any
        of it: that is the screen it is maximized and halved on, and the one
        whose taskbar lists it. A drag is aimed by the hand, so where one
        ends is read on the screen under the pointer, and only at an edge no
        other screen is against: a window let go just past a shared edge has
        crossed it, and is not arranged.
*/

// A connector's name as the kernel spells it: its type's name, a dash and its number among those of the type.
static void compositor_connector_name(unsigned int type, unsigned int number, char *into)
{
        static const char *const types[] = {"Unknown", "VGA", "DVI-I", "DVI-D", "DVI-A", "Composite", "SVIDEO",
                                            "LVDS", "Component", "DIN", "DP", "HDMI-A", "HDMI-B", "TV", "eDP",
                                            "Virtual", "DSI", "DPI", "Writeback", "SPI", "USB"};
        const char *name = type < sizeof(types) / sizeof(types[0]) ? types[type] : types[0];
        char digits[12];
        unsigned int length = 0, count = 0;

        while (name[length] && length < WINDOWS_OUTPUT_NAME - 2)
                into[length] = name[length], length++;
        into[length++] = '-';
        do
                digits[count++] = (char)('0' + number % 10), number /= 10;
        while (number && count < sizeof(digits));
        while (count && length < WINDOWS_OUTPUT_NAME - 1)
                into[length++] = digits[--count];
        into[length] = 0;
}

/*
        Whether a connector is a machine's own panel (eDP, LVDS or DSI: a laptop's, a tablet's, a handheld's), which a
        touch screen inside the machine is glued to. The card does not say which screen a touch screen is; a touch
        screen on no external bus is the first such panel's, and any other, or with no such panel, the primary's.
*/
static int compositor_builtin_panel(const char *name)
{
        return (name[0] == 'e' && name[1] == 'D' && name[2] == 'P' && name[3] == '-') ||
               (name[0] == 'L' && name[1] == 'V' && name[2] == 'D' && name[3] == 'S' && name[4] == '-') ||
               (name[0] == 'D' && name[1] == 'S' && name[2] == 'I' && name[3] == '-');
}

// A rectangle, x, y, width, height, and the point x, y: whether the one holds the other.
static int compositor_screen_holds(const int *screen, int x, int y)
{
        return x >= screen[0] && y >= screen[1] && x - screen[0] < screen[2] && y - screen[1] < screen[3];
}

// The screen a point is on, the first of the list that holds it; -1 for none.
static int compositor_screen_at(const int (*screens)[4], unsigned int count, int x, int y)
{
        for (unsigned int at = 0; at < count; at++)
                if (compositor_screen_holds(screens[at], x, y))
                        return (int)at;
        return -1;
}

// A value held to low to high, low winning where they cross: a window that does not fit keeps its top left.
static int compositor_fit(int value, int low, int high)
{
        value = value > high ? high : value;
        return value < low ? low : value;
}

// The point of a screen nearest x, y, and how far that is, squared.
static long long compositor_screen_nearest(const int *screen, int *x, int *y)
{
        int nx = compositor_fit(*x, screen[0], screen[0] + screen[2] - 1);
        int ny = compositor_fit(*y, screen[1], screen[1] + screen[3] - 1);
        long long dx = (long long)*x - nx, dy = (long long)*y - ny;

        *x = nx;
        *y = ny;
        return dx * dx + dy * dy;
}

/*
        The pointer moved from from_x, from_y to x, y: where it is, on a
        screen. A point on one is where it is; one off them all is kept on
        the screen the movement started on, at its edge; and one with no
        screen to start from goes to the nearest point of the nearest screen
        (the lower index of two as near).
*/
static void compositor_screen_keep_point(const int (*screens)[4], unsigned int count, int from_x, int from_y, int *x,
                                         int *y)
{
        int from = compositor_screen_at(screens, count, from_x, from_y), best_x = *x, best_y = *y;
        long long least = -1;

        if (!count || compositor_screen_at(screens, count, *x, *y) >= 0)
                return;
        if (from >= 0)
        {
                compositor_screen_nearest(screens[from], x, y);
                return;
        }
        for (unsigned int at = 0; at < count; at++)
        {
                int nx = *x, ny = *y;
                long long far = compositor_screen_nearest(screens[at], &nx, &ny);

                if (least < 0 || far < least)
                        least = far, best_x = nx, best_y = ny;
        }
        *x = best_x;
        *y = best_y;
}

// How much of a rectangle, x, y, width, height, lies on a screen, in pixels.
static long long compositor_screen_cover(const int *screen, int x, int y, int width, int height)
{
        long long x1 = x > screen[0] ? x : screen[0], y1 = y > screen[1] ? y : screen[1];
        long long x2 = (long long)x + width < (long long)screen[0] + screen[2] ? (long long)x + width
                                                                               : (long long)screen[0] + screen[2];
        long long y2 = (long long)y + height < (long long)screen[1] + screen[3] ? (long long)y + height
                                                                                : (long long)screen[1] + screen[3];

        return x2 > x1 && y2 > y1 ? (x2 - x1) * (y2 - y1) : 0;
}

// A window's screen: the one that holds the most of it, the lower index of two that hold as much, and with none
// holding any, the one nearest its middle.
static int compositor_screen_owner(const int (*screens)[4], unsigned int count, int x, int y, int width, int height)
{
        long long most = 0, least = -1;
        int owner = -1;

        for (unsigned int at = 0; at < count; at++)
        {
                long long cover = compositor_screen_cover(screens[at], x, y, width, height);

                if (cover > most)
                        most = cover, owner = (int)at;
        }
        if (owner >= 0 || !count)
                return owner < 0 ? 0 : owner;
        for (unsigned int at = 0; at < count; at++)
        {
                int mx = (int)(((long long)x * 2 + width) / 2), my = (int)(((long long)y * 2 + height) / 2);
                long long far = compositor_screen_nearest(screens[at], &mx, &my);

                if (least < 0 || far < least)
                        least = far, owner = (int)at;
        }
        return owner;
}

// The rectangle that holds every screen.
static void compositor_screens_box(const int (*screens)[4], unsigned int count, int *box)
{
        box[0] = box[1] = box[2] = box[3] = 0;
        for (unsigned int at = 0; at < count; at++)
        {
                int x2 = at ? box[0] + box[2] : screens[at][0] + screens[at][2];
                int y2 = at ? box[1] + box[3] : screens[at][1] + screens[at][3];

                x2 = x2 > screens[at][0] + screens[at][2] ? x2 : screens[at][0] + screens[at][2];
                y2 = y2 > screens[at][1] + screens[at][3] ? y2 : screens[at][1] + screens[at][3];
                box[0] = at && box[0] < screens[at][0] ? box[0] : screens[at][0];
                box[1] = at && box[1] < screens[at][1] ? box[1] : screens[at][1];
                box[2] = x2 - box[0];
                box[3] = y2 - box[1];
        }
}

// The line a window keeps off a span's edges: none where the window is exactly the span, its line having no room there
// (kept at its contents, the last column or row would be off the screen).
static int compositor_keep_line(int line, int size, int span)
{
        return size == span ? 0 : line;
}

/*
        A window, width by height with a line round it, kept where a person
        can reach it: on the space, all of it when it fits and its top left
        when it does not, as on one screen; and its top, the strip a press
        takes it by (grab rows of it, or all of a shorter window), wholly on
        a screen for as much of it as a press needs (grab pixels of it, or
        all of a narrower window). A window whose top is not is put on its
        screen, all of it when it fits and its top left when not.
*/
static void compositor_screen_keep_window(const int (*screens)[4], unsigned int count, int *x, int *y, int width,
                                          int height, int line, int grab)
{
        int box[4], strip = height < grab ? height : grab, need = width < grab ? width : grab, owner;
        int line_x, line_y;

        compositor_screens_box(screens, count, box);
        line_x = compositor_keep_line(line, width, box[2]);
        line_y = compositor_keep_line(line, height, box[3]);
        *x = compositor_fit(*x, box[0] + line_x, box[0] + box[2] - width - line_x);
        *y = compositor_fit(*y, box[1] + line_y, box[1] + box[3] - height - line_y);
        if (count < 2)
                return;

        for (unsigned int at = 0; at < count; at++)
        {
                const int *s = screens[at];
                long long x1 = *x > s[0] ? *x : s[0];
                long long x2 = (long long)*x + width < (long long)s[0] + s[2] ? (long long)*x + width : (long long)s[0] + s[2];

                if (*y >= s[1] && (long long)*y + strip <= (long long)s[1] + s[3] && x2 - x1 >= need)
                        return;
        }
        owner = compositor_screen_owner(screens, count, *x, *y, width, height);
        line_x = compositor_keep_line(line, width, screens[owner][2]);
        line_y = compositor_keep_line(line, height, screens[owner][3]);
        *x = compositor_fit(*x, screens[owner][0] + line_x, screens[owner][0] + screens[owner][2] - width - line_x);
        *y = compositor_fit(*y, screens[owner][1] + line_y, screens[owner][1] + screens[owner][3] - height - line_y);
}

// The screen's edges a snap may take, as bits: a window's contact with them (compositor_snap_rule) is one of these.
#define COMPOSITOR_SNAP_LEFT 1u
#define COMPOSITOR_SNAP_RIGHT 2u
#define COMPOSITOR_SNAP_TOP 4u
#define COMPOSITOR_SNAP_BOTTOM 8u

/*
        The edges of screen at that a window at box may snap to: all four, less each one another screen is against across
        the span the window and the screen share. The seam between two screens is never an edge, the other sides of a
        screen are.
*/
static unsigned int compositor_snap_free(const int (*screens)[4], unsigned int count, const int *box, int at)
{
        const int *s = screens[at];
        int top = box[1] > s[1] ? box[1] : s[1], bottom = box[1] + box[3] < s[1] + s[3] ? box[1] + box[3] : s[1] + s[3];
        int left = box[0] > s[0] ? box[0] : s[0], right = box[0] + box[2] < s[0] + s[2] ? box[0] + box[2] : s[0] + s[2];
        unsigned int free = COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT | COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM;

        for (unsigned int each = 0; each < count; each++)
        {
                const int *o = screens[each];

                if (each == (unsigned int)at)
                        continue;
                if (o[0] + o[2] == s[0] && o[1] < bottom && o[1] + o[3] > top)
                        free &= ~COMPOSITOR_SNAP_LEFT;
                if (o[0] == s[0] + s[2] && o[1] < bottom && o[1] + o[3] > top)
                        free &= ~COMPOSITOR_SNAP_RIGHT;
                if (o[1] + o[3] == s[1] && o[0] < right && o[0] + o[2] > left)
                        free &= ~COMPOSITOR_SNAP_TOP;
                if (o[1] == s[1] + s[3] && o[0] < right && o[0] + o[2] > left)
                        free &= ~COMPOSITOR_SNAP_BOTTOM;
        }
        return free;
}

// The side of one axis a snap takes, of the two that bound it (lo, hi): one touched, else the one alone near, else none.
static unsigned int compositor_snap_side(unsigned int touch, unsigned int near, unsigned int lo, unsigned int hi)
{
        if (touch & lo)
                return lo;
        if (touch & hi)
                return hi;
        if ((near & lo) && !(near & hi))
                return lo;
        if ((near & hi) && !(near & lo))
                return hi;
        return 0;
}

/*
        The snap target of a window at box (x, y, width, height), before any clamp, on screen, or COMPOSITOR_FLOATING for
        none. This reads the window's rectangle only. An edge is touched when the window's edge is at the screen's edge or
        past it (a gap of 0 or less): the first frame it is, and that frame's target is shown. A touched edge holds until it
        is edge pixels inside the screen's (the dead zone), which is held (the touched edges of the last frame). An axis the
        window spans, with both its edges touched, takes no snap. A corner is the quarter of its two edges when one of them
        is touched and the other is within corner pixels of the screen's corner: it wins over the edge that is touched. Else a
        touched top is the whole screen, and a left or right side that half. The bottom alone is no target: only its two
        corners are, so a tall window set low is not taken by the edge it rests on. free is the edges that may be snapped to
        (compositor_snap_free). touch is set to the edges touched now, for the next frame's held.
*/
static unsigned int compositor_snap_rule(const int *box, const int *screen, unsigned int free, unsigned int held, int edge, int corner,
                                         unsigned int *touch)
{
        int gap[4] = {box[0] - screen[0], screen[0] + screen[2] - (box[0] + box[2]), box[1] - screen[1],
                      screen[1] + screen[3] - (box[1] + box[3])};
        unsigned int contact = 0, near = 0, side_h, side_v;

        for (int each = 0; each < 4; each++)
        {
                unsigned int bit = 1u << each;

                if (!(free & bit))
                        continue;
                if (gap[each] <= 0 || ((held & bit) && gap[each] < edge))
                        contact |= bit;
                if (gap[each] < corner)
                        near |= bit;
        }
        if ((contact & (COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT)) == (COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT))
                contact &= ~(COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT);
        if ((contact & (COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM)) == (COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM))
                contact &= ~(COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM);
        *touch = contact;
        side_h = compositor_snap_side(contact, near, COMPOSITOR_SNAP_LEFT, COMPOSITOR_SNAP_RIGHT);
        side_v = compositor_snap_side(contact, near, COMPOSITOR_SNAP_TOP, COMPOSITOR_SNAP_BOTTOM);
        if (side_h && side_v && (contact & (side_h | side_v)))
        {
                if (side_v == COMPOSITOR_SNAP_TOP)
                        return side_h == COMPOSITOR_SNAP_LEFT ? COMPOSITOR_TOP_LEFT : COMPOSITOR_TOP_RIGHT;
                return side_h == COMPOSITOR_SNAP_LEFT ? COMPOSITOR_BOTTOM_LEFT : COMPOSITOR_BOTTOM_RIGHT;
        }
        if (contact & COMPOSITOR_SNAP_TOP)
                return COMPOSITOR_MAXIMIZED;
        if (contact & COMPOSITOR_SNAP_LEFT)
                return COMPOSITOR_LEFT;
        if (contact & COMPOSITOR_SNAP_RIGHT)
                return COMPOSITOR_RIGHT;
        return COMPOSITOR_FLOATING;
}

/*
        What a window arranged as how fills of a screen, as x, y, width,
        height. The halves and quarters are the screen's own halves: a right
        part starts at half the width, a bottom one at half the height, and
        the far half takes the odd pixel.
*/
static void compositor_screen_part(const int *screen, unsigned int how, int *part)
{
        int half_w = screen[2] / 2, half_h = screen[3] / 2;
        int wide = how == COMPOSITOR_LEFT || how == COMPOSITOR_RIGHT || how >= COMPOSITOR_TOP_LEFT;
        int tall = how == COMPOSITOR_BOTTOM || how >= COMPOSITOR_TOP_LEFT;
        int right = how == COMPOSITOR_RIGHT || how == COMPOSITOR_TOP_RIGHT || how == COMPOSITOR_BOTTOM_RIGHT;
        int low = how == COMPOSITOR_BOTTOM || how == COMPOSITOR_BOTTOM_LEFT || how == COMPOSITOR_BOTTOM_RIGHT;

        part[0] = screen[0] + (right ? half_w : 0);
        part[1] = screen[1] + (low ? half_h : 0);
        part[2] = !wide ? screen[2] : right ? screen[2] - half_w : half_w;
        part[3] = !tall ? screen[3] : low ? screen[3] - half_h : half_h;
}

/*
        A window width by height at x, y on the screen was, taken to the
        screen now: at the same share of the room it had to move in, across
        and down, so a screen that only moved takes its windows with it and
        one that went sends them to the same corner of another.
*/
static int compositor_carry_axis(int at, int was_at, int was_size, int now_at, int now_size, int size)
{
        long long room_was = (long long)was_size - size, room_now = (long long)now_size - size;
        long long offset = (long long)at - was_at;

        // A window as large as the room it had, or larger than the room it has, keeps its top left at the screen's.
        if (room_was <= 0 || room_now <= 0)
                return now_at;
        offset = offset < 0 ? 0 : offset > room_was ? room_was : offset;
        return now_at + (int)(offset * room_now / room_was);
}

static void compositor_screen_carry(const int *was, const int *now, int *x, int *y, int width, int height)
{
        *x = compositor_carry_axis(*x, was[0], was[2], now[0], now[2], width);
        *y = compositor_carry_axis(*y, was[1], was[3], now[1], now[3], height);
}

// The pointer's cell, size pixels square at left, top in the space, and a screen: where the cell is on that screen,
// and whether it meets it at all, which is whether that screen's cursor plane is to show it.
static int compositor_screen_cell(const int *screen, int left, int top, int size, int *x, int *y)
{
        *x = left - screen[0];
        *y = top - screen[1];
        return *x < screen[2] && *y < screen[3] && *x + size > 0 && *y + size > 0;
}

// What of a change (clips x1, y1, x2, y2 in the space) is on a screen: each clip cut to the screen, and those that miss
// it left out. Answers how many are left, into into.
static unsigned int compositor_screen_clips(const int *screen, const unsigned short (*clips)[4], unsigned int count,
                                            unsigned short (*into)[4])
{
        unsigned int kept = 0;

        for (unsigned int at = 0; at < count; at++)
        {
                int x1 = clips[at][0] > screen[0] ? clips[at][0] : screen[0];
                int y1 = clips[at][1] > screen[1] ? clips[at][1] : screen[1];
                int x2 = clips[at][2] < screen[0] + screen[2] ? clips[at][2] : screen[0] + screen[2];
                int y2 = clips[at][3] < screen[1] + screen[3] ? clips[at][3] : screen[1] + screen[3];

                if (x2 <= x1 || y2 <= y1)
                        continue;
                into[kept][0] = (unsigned short)x1, into[kept][1] = (unsigned short)y1;
                into[kept][2] = (unsigned short)x2, into[kept][3] = (unsigned short)y2;
                kept++;
        }
        return kept;
}

/* ---- end of the outputs ---- */
