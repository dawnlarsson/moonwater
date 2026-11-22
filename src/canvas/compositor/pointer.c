/*
        What a hand means, and what the pointer looks like: the gain on speed, a key's repeat, a shake, a tablet's
        reach, the shapes for an edge, and the pointer's picture made and drawn (its cover table, inside a target, and
        as the card's records inside MOONWATER_GPU_C).

        It is part of the backend CHECK_canvas_cells builds without a compositor (the section between its two lines
        below), so it names nothing of struct compositor: the compositor's state holds its structs (compositor_pointer,
        compositor_shake, compositor_sway) and passes them in. Where the pointer is and when it moves are input.c's; its
        plane on the card is kms.c's (compositor_cursor_*). It needs compositor.c's prelude (the lit edges, struct
        compositor_target), compose.c's GPU records and window.c's pointer shapes. Called by kms.c (the plane's picture),
        present.c and scanout.c (drawn in the scene), gpu_host.c, input.c (the gain, the shake), manage.c (an edge's
        shape), session.c (the repeat) and wayland_host.c (a client's shape).
*/

/* ---- pointer: what a hand means ---- */

/*
        Acceleration, the way a desktop does it: a mouse reports counts, not
        pixels, and the same count means aiming when the hand is slow and
        crossing the screen when it is fast, so the gain is a curve on speed.
        Below the floor it is exactly one to one, which is what makes careful
        movement land where it is aimed; above the ceiling it is flat. Fixed
        point, speed in counts a millisecond: the kernel desktop's curve.
*/
#define CP_ACCEL_ONE 1024
#define CP_ACCEL_FLOOR 1
#define CP_ACCEL_CEILING 8
#define CP_ACCEL_MAX (CP_ACCEL_ONE * 5 / 2)

// The curve is the library's (pointer_gain, src/lib.c), which is written with these numbers in it.
_Static_assert(CP_ACCEL_ONE == 1024 && CP_ACCEL_FLOOR == 1 && CP_ACCEL_CEILING == 8 && CP_ACCEL_MAX == 2560,
               "the curve pointer_gain is written for");

static inline INLINE CONST unsigned long long compositor_isqrt(unsigned long long value)
{
#if defined(__x86_64__)
        if (likely(value < (1ull << 52)))
        {
                unsigned long long r;
                __asm__("cvtsi2sdq %1, %%xmm0\n\tsqrtsd %%xmm0, %%xmm0\n\tcvttsd2siq %%xmm0, %0" : "=r"(r) : "r"(value) : "xmm0");
                return r;
        }
#elif defined(__aarch64__)
        if (likely(value < (1ull << 52)))
        {
                unsigned long long r;
                __asm__("ucvtf d0, %1\n\tfsqrt d0, d0\n\tfcvtzu %0, d0" : "=r"(r) : "r"(value) : "d0");
                return r;
        }
#endif
        unsigned long long root = 0, bit = 1ull << 62;

        while (bit > value)
                bit >>= 2;
        for (; bit; bit >>= 2)
                if (value >= root + bit)
                {
                        value -= root + bit;
                        root = (root >> 1) + bit;
                }
                else
                        root >>= 1;
        return root;
}

// The gain of one movement of the hand, dx and dy together, in the time since the last: the distance's whole root, the speed
// in counts a millisecond, and the curve's gain at it, which are the library's instructions on each machine (pointer_gain).
static int compositor_gain_of(int dx, int dy, unsigned long long interval)
{
        return (int)pointer_gain(dx, dy, interval);
}

// A count turned into pixels. The remainder is what stops a gain that is not
// whole from dropping the fraction of every movement: a slow drag would come
// up short of where it was aimed.
static int compositor_accel(int delta, int *remainder, int gain)
{
        return (int)pointer_scale(delta, remainder, (unsigned long long)(long long)gain);
}

// A device's counts added up, held to WINDOW_COORD_MAX either way: an evdev value is any 32 bits a device (a USB one is
// anyone's) chose, and two of them, or one past a place, overflowed the int they were added into.
static int compositor_counts(int sum, long long more)
{
        long long total = (long long)sum + more;

        return (int)(total > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : total < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : total);
}

/*
        The gain the hand has chosen: the curve's at the speed, or one to one when
        `moonwater mouse acceleration off` says the curve is not wanted, times the
        sensitivity in percent (`moonwater mouse sensitivity`; none given, or 100,
        leaves the gain as it was, to the bit).
*/
static int compositor_gain_wanted(int gain, int flat, int percent)
{
        long long base = flat ? CP_ACCEL_ONE : gain;

        if (!percent || percent == 100)
                return (int)base;
        return (int)(base * percent / 100);
}

/*
        What a keyboard's repeat is made of for `moonwater keyboard repeat`: the delay
        before the first repeat in milliseconds and the period between repeats (0 stops them),
        each the setting's when it is kept (delay and rate are 0 when not, a rate of
        none being the setting's off, which is told by rate_kept) and else the
        device's own, as it was when it was opened.
*/
static int compositor_repeat_period(int rate)
{
        return rate > 0 ? (1000 + rate / 2) / rate : 0;
}

static void compositor_repeat_target(int kept, int delay, int rate, const int own[2], int into[2])
{
        into[0] = kept & 1 ? delay : own[0];
        into[1] = kept & 2 ? compositor_repeat_period(rate) : own[1];
}

// Where a tablet's report is on a size: the report held to its range, then
// the offset in it times the size over the range, rounded down.
static int compositor_tablet(int value, int minimum, int maximum, int size)
{
        long long span = (long long)maximum - minimum, offset;

        if (span <= 0)
                return 0;
        offset = (long long)(value < minimum ? minimum : value > maximum ? maximum : value) - minimum;
        return (int)(offset * size / span);
}

/*
        Shake to find it: reversing direction takes a real movement each
        time, so a slow wobble or a hand resting on the mouse is not a shake,
        and the reversals have to come inside one window or the count starts
        again. How much it takes is `moonwater canvas cursor shake
        sensitivity`: normal (the standard) is CP_SHAKE_STEP pixels a
        reversal and CP_SHAKE_REVERSALS of them, low asks more of the hand
        (a step of 9 and 7 reversals) and high less (4 and 4).
*/
#define CP_SHAKE_WINDOW 700000000ull
#define CP_SHAKE_STEP 6
#define CP_SHAKE_REVERSALS 5

struct compositor_shake
{
        unsigned long long window;
        int direction, count;
        int feel; // `moonwater canvas cursor shake sensitivity`: 0 normal (the standard), 1 low, 2 high, its word's place among its words
};

static int compositor_shake(struct compositor_shake *shake, int delta, unsigned long long now)
{
        static const int steps[3] = {CP_SHAKE_STEP, 9, 4}, reversals[3] = {CP_SHAKE_REVERSALS, 7, 4};
        int direction, feel = shake->feel >= 0 && shake->feel < 3 ? shake->feel : 0;

        if (delta > -steps[feel] && delta < steps[feel])
                return 0;

        direction = delta > 0 ? 1 : -1;
        if (now - shake->window > CP_SHAKE_WINDOW)
        {
                shake->window = now;
                shake->count = 0;
        }

        if (direction == shake->direction)
                return 0;

        shake->direction = direction;
        if (++shake->count >= reversals[feel])
        {
                shake->count = 0;
                return 1;
        }
        return 0;
}


/*
        Cursors. The classic set is drawn at the size the classic Windows cursors are (the arrow 12 pixels by 21, the hand 17 by 22, the
        hourglass 13 by 22, a resize arrow 21 long), each in a cell CP_CELL pixels square, by hand and to their look: none is copied from a
        file. A row is '#' where the shape is its edge ink (black, on the light look), 'o' where it is its fill ink (white) and '.' where it
        is clear. The arrow, the hand and the hourglass are white with a black line; the I-beam, the cross, the resize arrows, the four
        arrows of a move and the circle and slash are black with a white line round them, which is what shows them on a dark ground. The
        hotspot is the pixel the pointer is: the tip of the arrow and of the hand's index finger, and the middle of each other shape.
*/
#define CP_CELL 24
#define CP_ARROW 0
#define CP_RESIZE_H 1
#define CP_RESIZE_V 2
#define CP_RESIZE_NWSE 3
#define CP_RESIZE_NESW 4
#define CP_TEXT 5               // the I-beam (text), and the two shapes a client may ask for that the compositor draws too
#define CP_CROSS 6              // the crosshair
#define CP_MOVE 7               // four arrows (move, all scroll)
#define CP_HAND 8               // the hand: a link, a grab
#define CP_WAIT 9               // the hourglass: busy
#define CP_NOTALLOWED 10        // the circle and slash: no drop, not allowed
#define CP_SHAPES 11
_Static_assert(WINDOW_POINTER_ARROW == CP_ARROW && WINDOW_POINTER_TEXT == CP_TEXT && WINDOW_POINTER_HAND == CP_HAND && WINDOW_POINTER_RESIZE == CP_RESIZE_H,
               "a program's pointer shapes are the compositor's CP_ codes");
// The cursor plane is as large as the smallest a driver makes, and CP_SPAN_MOST the side of the largest picture drawn into a scene.
#define CP_PLANE 64
#define CP_SPAN_MOST 384

/*
        The dynamic cursor (`moonwater canvas cursor style dynamic`, and every pointer a touch uses) is one filled shape with a line at its
        edge and nothing in it: a circle of CP_DYN_RADIUS cells, which over text is a rounded rectangle a touch taller than a terminal
        cell (one wide, three tall), the one easing into the other; over a window's edge it keeps the circle's core and thickness
        (CP_DYN_RADIUS) with points easing out to CP_DYN_LENS_W along the way the edge moves. A fast hand stretches it along its motion and squashes it across by as much,
        so its area stays: nothing under CP_DYN_SLOW logical pixels a second, all of CP_DYN_STRETCH from CP_DYN_FAST up, and it lets go
        CP_DYN_IDLE_NS after the last motion. Its cell is CP_DYN_CELL square with the pointer at its middle, so the plane holds it at twice
        its size with the longest stretch.

        Either picture is made a pixel at a time from samples of its cell (compositor_pointer_rows), so it is drawn at any scale: a whole
        scale gives the classic bitmaps pixel for pixel, and a scale between is smooth. That is what a held button uses: the left one
        grows the pointer a little, by half the squeeze the settings give, whichever button it is and in every style, over CP_SWAY_NS on the
        kit's ease.
*/
#define CP_DYN_CELL 32
#define CP_DYN_RADIUS 8
#define CP_DYN_TEXT_W 4         // the rectangle over text: its half width, half height and corner radius, in cells
#define CP_DYN_TEXT_H 12
#define CP_DYN_TEXT_R 2
#define CP_DYN_LENS_W 12 // the resize indicator over a window's edge: its half length to each point, and core radius (CP_DYN_RADIUS), in cells
#define CP_DYN_LENS_H CP_DYN_RADIUS
#define CP_DYN_SLOW 1500
#define CP_DYN_FAST 6000
#define CP_DYN_STRETCH 154      // 256ths the long axis grows by at most: six tenths
#define CP_DYN_IDLE_NS 50000000ull
#define CP_DYN_SAMPLE_NS 8000000ull
#define CP_SWAY_NS 100000000ull

// What a pointer's picture is of: everything its pixels come from, so two that are equal are one picture.
struct compositor_pointer
{
        int dyn;                // the dynamic shape, and not a bitmap
        unsigned int shape;     // the bitmap (0 for the dynamic shape)
        int scale;              // device pixels a cell, in 256ths
        int hw, hh, c;          // the dynamic shape's half width, half height and corner radius, in 256ths of a cell
        int ux, uy, grow;       // its stretch: the unit direction times 64, and how much longer along it in 256ths (0 for none)
        int lens;               // the lens over a window's edge, along ux, uy (hw its half length, hh its half thickness), not stretched
        int span, hot_x, hot_y; // a client's picture: its square's side, and the pointer's pixel of the square (compositor_picture)
        unsigned long picture;  // which picture of a window's client it is (compositor_picture.made), 0 for the compositor's own shapes
};

// A value on its way to another (0 at rest): where it left from, where it goes, and when it left.
struct compositor_sway
{
        int from, to;
        positive at;
};

// The bitmap of each shape, one row of CP_CELL pixels a line (see above).
static const char compositor_cursor_art[CP_SHAPES][CP_CELL][CP_CELL + 1] = {
        [CP_ARROW] = {
            "#.......................",
            "##......................",
            "#o#.....................",
            "#oo#....................",
            "#ooo#...................",
            "#oooo#..................",
            "#ooooo#.................",
            "#oooooo#................",
            "#ooooooo#...............",
            "#oooooooo#..............",
            "#ooooooooo#.............",
            "#oooooooooo#............",
            "#oooooo#####............",
            "#ooo#oo#................",
            "#oo#.#oo#...............",
            "#o#..#oo#...............",
            "##....#oo#..............",
            "#.....#oo#..............",
            ".......#oo#.............",
            ".......#oo#.............",
            "........##..............",
            "........................",
            "........................",
            "........................",
        },
        [CP_RESIZE_H] = {
            "........................",
            "........................",
            "........................",
            "........................",
            "........................",
            "........................",
            "....ooo.........ooo.....",
            "...oo#o.........o#oo....",
            "..oo##o.........o##oo...",
            ".oo###o.........o###oo..",
            "oo####ooooooooooo####oo.",
            "o#####################o.",
            "oo####ooooooooooo####oo.",
            ".oo###o.........o###oo..",
            "..oo##o.........o##oo...",
            "...oo#o.........o#oo....",
            "....ooo.........ooo.....",
            "........................",
            "........................",
            "........................",
            "........................",
            "........................",
            "........................",
            "........................",
        },
        [CP_RESIZE_V] = {
            "..........ooo...........",
            ".........oo#oo..........",
            "........oo###oo.........",
            ".......oo#####oo........",
            "......oo#######oo.......",
            "......o#########o.......",
            "......ooooo#ooooo.......",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "......ooooo#ooooo.......",
            "......o#########o.......",
            "......oo#######oo.......",
            ".......oo#####oo........",
            "........oo###oo.........",
            ".........oo#oo..........",
            "..........ooo...........",
            "........................",
        },
        [CP_RESIZE_NWSE] = {
            "........................",
            "........................",
            "........................",
            "...ooooooooo............",
            "...o#######o............",
            "...o######oo............",
            "...o#####oo.............",
            "...o####oo..............",
            "...o###o#oo.............",
            "...o##ooo#oo............",
            "...o#oo.oo#oo...........",
            "...ooo...oo#oo...ooo....",
            "..........oo#oo.oo#o....",
            "...........oo#ooo##o....",
            "............oo#o###o....",
            ".............oo####o....",
            "............oo#####o....",
            "...........oo######o....",
            "...........o#######o....",
            "...........ooooooooo....",
            "........................",
            "........................",
            "........................",
            "........................",
        },
        [CP_RESIZE_NESW] = {
            "........................",
            "........................",
            "........................",
            "...........ooooooooo....",
            "...........o#######o....",
            "...........oo######o....",
            "............oo#####o....",
            ".............oo####o....",
            "............oo#o###o....",
            "...........oo#ooo##o....",
            "..........oo#oo.oo#o....",
            "...ooo...oo#oo...ooo....",
            "...o#oo.oo#oo...........",
            "...o##ooo#oo............",
            "...o###o#oo.............",
            "...o####oo..............",
            "...o#####oo.............",
            "...o######oo............",
            "...o#######o............",
            "...ooooooooo............",
            "........................",
            "........................",
            "........................",
            "........................",
        },
        [CP_TEXT] = {
            "........................",
            "........................",
            "........................",
            ".......ooooooooo........",
            ".......o###o###o........",
            ".......oooo#oooo........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            ".......oooo#oooo........",
            ".......o###o###o........",
            ".......ooooooooo........",
            "........................",
            "........................",
            "........................",
        },
        [CP_CROSS] = {
            "........................",
            "........................",
            "..........ooo...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..ooooooooo#ooooooooo...",
            "..o#################o...",
            "..ooooooooo#ooooooooo...",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........o#o...........",
            "..........ooo...........",
            "........................",
            "........................",
            "........................",
        },
        [CP_MOVE] = {
            "..........ooo...........",
            ".........oo#oo..........",
            "........oo###oo.........",
            ".......oo#####oo........",
            ".......o#######o........",
            ".......oooo#oooo........",
            "..........o#o...........",
            "...ooo....o#o....ooo....",
            "..oo#o....o#o....o#oo...",
            ".oo##o....o#o....o##oo..",
            "oo###oooooo#oooooo###oo.",
            "o#####################o.",
            "oo###oooooo#oooooo###oo.",
            ".oo##o....o#o....o##oo..",
            "..oo#o....o#o....o#oo...",
            "...ooo....o#o....ooo....",
            "..........o#o...........",
            ".......oooo#oooo........",
            ".......o#######o........",
            ".......oo#####oo........",
            "........oo###oo.........",
            ".........oo#oo..........",
            "..........ooo...........",
            "........................",
        },
        [CP_HAND] = {
            ".....##.................",
            "....#oo#................",
            "....#oo#................",
            "....#oo#................",
            "....#oo#................",
            "....#oo###..............",
            "....#oo#oo###...........",
            "....#oo#oo#oo##.........",
            "....#oo#oo#oo#o#........",
            "###.#oo#oo#oo#oo#.......",
            "#oo##oooooooo#oo#.......",
            "#ooo#ooooooooooo#.......",
            ".#oo#ooooooooooo#.......",
            "..#o#ooooooooooo#.......",
            "..#ooooooooooooo#.......",
            "...#oooooooooooo#.......",
            "...#ooooooooooo#........",
            "....#oooooooooo#........",
            "....#oooooooooo#........",
            ".....#oooooooo#.........",
            ".....#oooooooo#.........",
            ".....##########.........",
            "........................",
            "........................",
        },
        [CP_WAIT] = {
            "........................",
            ".....#############......",
            ".....##ooooooooo##......",
            ".....#############......",
            "......#ooooooooo#.......",
            "......#ooooooooo#.......",
            "......#oo#o#o#oo#.......",
            "......#ooo#o#ooo#.......",
            "......##ooo#ooo##.......",
            ".......##ooooo##........",
            "........##ooo##.........",
            ".........##o##..........",
            ".........##o##..........",
            "........##ooo##.........",
            ".......##oo#oo##........",
            "......##ooooooo##.......",
            "......#oooo#oooo#.......",
            "......#ooo#o#ooo#.......",
            "......#oo#o#o#oo#.......",
            "......#o#o#o#o#o#.......",
            ".....#############......",
            ".....##ooooooooo##......",
            ".....#############......",
            "........................",
        },
        [CP_NOTALLOWED] = {
            "........................",
            "........oooooooo........",
            "......ooo######ooo......",
            "....ooo##########ooo....",
            "...oo##############oo...",
            "...o####oooooooo####o...",
            "..oo####oo.....oo###oo..",
            "..o######oo.....oo###o..",
            ".oo##oo###oo.....oo##oo.",
            ".o###ooo###oo.....o###o.",
            ".o###o.oo###oo....o###o.",
            ".o###o..oo###oo...o###o.",
            ".o###o...oo###oo..o###o.",
            ".o###o....oo###oo.o###o.",
            ".o###o.....oo###ooo###o.",
            ".oo##oo.....oo###oo##oo.",
            "..o###oo.....oo######o..",
            "..oo###oo.....oo####oo..",
            "...o####oooooooo####o...",
            "...oo##############oo...",
            "....ooo##########ooo....",
            "......ooo######ooo......",
            "........oooooooo........",
            "........................",
        },
};

static const signed char compositor_cursor_hot[CP_SHAPES][2] = {{0, 0}, {11, 11}, {11, 11}, {11, 11}, {11, 11}, {11, 11}, {11, 11}, {11, 11}, {5, 0}, {11, 11}, {11, 11}};

// The edges of a window a hand has hold of, and the shape that says so.
#define CP_EDGE_LEFT 1u
#define CP_EDGE_RIGHT 2u
#define CP_EDGE_TOP 4u
#define CP_EDGE_BOTTOM 8u
static const unsigned char compositor_cursor_by_edges[16] = {
    [CP_EDGE_LEFT] = CP_RESIZE_H,
    [CP_EDGE_RIGHT] = CP_RESIZE_H,
    [CP_EDGE_TOP] = CP_RESIZE_V,
    [CP_EDGE_BOTTOM] = CP_RESIZE_V,
    [CP_EDGE_TOP | CP_EDGE_LEFT] = CP_RESIZE_NWSE,
    [CP_EDGE_BOTTOM | CP_EDGE_RIGHT] = CP_RESIZE_NWSE,
    [CP_EDGE_TOP | CP_EDGE_RIGHT] = CP_RESIZE_NESW,
    [CP_EDGE_BOTTOM | CP_EDGE_LEFT] = CP_RESIZE_NESW,
};

/*
        A window's client's own picture of the pointer (wl_pointer.set_cursor with a surface, wayland.c's wayland_cursor_picture), copied
        in the pass the shim names it and worn over the client's window in place of the shape the client was told (the arrow). The copy
        is in the screen's device pixels: the buffer's pixels times the desktop's scale over the buffer's, each pixel of the copy the
        mean of the buffer's under it (premultiplied, so the mean is of what shows), and no more than the plane, CP_PLANE, either way: a
        larger one is made smaller to fit, its shape kept. A buffer more than CP_PICTURE_SOURCE_MOST pixels either way is not read at all
        and the pointer is the arrow, so a client cannot have a pass read more of its pool than that for a picture. The colours are kept
        two ways: premultiplied, as the plane takes them (a colour past its alpha, which no premultiplied picture has, is taken at its
        alpha), and as an ink and a cover, the colour over its alpha and the alpha, which a scene mixes with memory_mix_u32_row as it
        mixes the compositor's own shapes. The square drawn holds the picture and the hotspot both (the pointer's pixel, which the
        client may put outside its picture), so the picture is where the client put it; a hotspot further out than the plane reaches
        is taken at the plane's edge.
*/
#define CP_PICTURE_SOURCE_MOST 256
#define CP_PICTURE_SCALE_MOST 8 // a buffer's scale past this is not read either

static struct
{
        unsigned long made;     // counted up for each copy made, so a pointer's picture names its copy (compositor_pointer.picture)
        unsigned long worn;     // the copy the pointer wears now, 0 for the compositor's own shape
        unsigned long serial;   // the shim's count of what it named (wayland_cursor_picture) when the copy was last asked for
        int asked;              // whether it has been asked for since the pointer came to the window
        int width, height;      // the copy, in device pixels
        int left, top;          // the copy's corner in its square
        int span, hot_x, hot_y; // the square's side, and the pointer's pixel of it
        unsigned int premultiplied[CP_PLANE * CP_PLANE]; // rows CP_PLANE apart: the plane's pixels and software scene source
} compositor_picture;

// n over d rounded toward minus infinity, d above zero: a hotspot left of a picture is left of its copy too.
static long compositor_picture_floor(long n, long d)
{
        return n / d - (n % d < 0);
}

/*
        A client's picture copied (see above): pixels the first, stride bytes a row, width by height of them, premultiplied ARGB8888 or,
        opaque, XRGB8888; the hotspot in its pixels; the buffer's scale; the desktop's in percent. The copy's number (never 0), or 0 when
        it is not one the pointer wears.
*/
static unsigned long compositor_picture_take(const unsigned char *pixels, long stride, long width, long height, int opaque, long hot_x,
                                             long hot_y, long scale, int percent)
{
        long wide, tall, most, hx, hy, left, top, right, bottom;

        if (!pixels || width <= 0 || height <= 0 || width > CP_PICTURE_SOURCE_MOST || height > CP_PICTURE_SOURCE_MOST || stride < 4 * width ||
            scale <= 0 || scale > CP_PICTURE_SCALE_MOST || percent <= 0)
                return 0;
        wide = (width * percent + 50 * scale) / (100 * scale);
        tall = (height * percent + 50 * scale) / (100 * scale);
        wide = wide > 0 ? wide : 1, tall = tall > 0 ? tall : 1;
        most = wide > tall ? wide : tall;
        if (most > CP_PLANE)
        {
                wide = wide * CP_PLANE / most, tall = tall * CP_PLANE / most;
                wide = wide > 0 ? wide : 1, tall = tall > 0 ? tall : 1;
        }
        for (long y = 0; y < tall; y++)
        {
                long y0 = y * height / tall, y1 = (y + 1) * height / tall;

                y1 = y1 > y0 ? y1 : y0 + 1;
                for (long x = 0; x < wide; x++)
                {
                        long x0 = x * width / wide, x1 = (x + 1) * width / wide;
                        unsigned long sums[4] = {0, 0, 0, 0}, count, mean[4];
                        unsigned int at = (unsigned int)(y * CP_PLANE + x);

                        x1 = x1 > x0 ? x1 : x0 + 1;
                        count = (unsigned long)((y1 - y0) * (x1 - x0));
                        for (long v = y0; v < y1; v++)
                                for (long u = x0; u < x1; u++)
                                {
                                        const unsigned char *pixel = pixels + v * stride + u * 4;
                                        unsigned int alpha = opaque ? 255u : pixel[3];

                                        for (int channel = 0; channel < 3; channel++)
                                                sums[channel] += pixel[channel] < alpha ? pixel[channel] : alpha;
                                        sums[3] += alpha;
                                }
                        for (int channel = 0; channel < 4; channel++)
                                mean[channel] = (sums[channel] + count / 2) / count;
                        compositor_picture.premultiplied[at] = (unsigned int)(mean[3] << 24 | mean[2] << 16 | mean[1] << 8 | mean[0]);
                }
        }
        hx = compositor_picture_floor(hot_x * wide, width), hy = compositor_picture_floor(hot_y * tall, height);
        hx = hx < wide - CP_PLANE ? wide - CP_PLANE : hx > CP_PLANE - 1 ? CP_PLANE - 1 : hx;
        hy = hy < tall - CP_PLANE ? tall - CP_PLANE : hy > CP_PLANE - 1 ? CP_PLANE - 1 : hy;
        left = hx < 0 ? hx : 0, top = hy < 0 ? hy : 0;
        right = hx + 1 > wide ? hx + 1 : wide, bottom = hy + 1 > tall ? hy + 1 : tall;
        compositor_picture.width = (int)wide, compositor_picture.height = (int)tall;
        compositor_picture.left = (int)-left, compositor_picture.top = (int)-top;
        compositor_picture.hot_x = (int)(hx - left), compositor_picture.hot_y = (int)(hy - top);
        compositor_picture.span = (int)(right - left > bottom - top ? right - left : bottom - top);
        if (!++compositor_picture.made)
                compositor_picture.made++;
        return compositor_picture.made;
}

/*
        A client's picture (p, the copy it names) over t with its square's corner at left, top, cut to t's damage: mixed by its cover into
        a scene's pixels, or written premultiplied into a plane's (plane), which are clear round it. Nothing for a copy since replaced.
*/
static void compositor_picture_draw(const struct compositor_target *t, int left, int top, const struct compositor_pointer *p, int plane)
{
        int px = left + compositor_picture.left, py = top + compositor_picture.top;
        int x1 = px > t->x1 ? px : t->x1, x2 = px + compositor_picture.width < t->x2 ? px + compositor_picture.width : t->x2;
        int y1 = py > t->y1 ? py : t->y1, y2 = py + compositor_picture.height < t->y2 ? py + compositor_picture.height : t->y2;
        unsigned long from = (unsigned long)(y1 - py) * CP_PLANE + (unsigned long)(x1 - px);

        if (p->picture != compositor_picture.made || x2 <= x1 || y2 <= y1)
                return;
        if (plane)
        {
                memory_copy_rect_u32(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                                     compositor_picture.premultiplied + from, CP_PLANE, (positive)(x2 - x1), (positive)(y2 - y1));
                return;
        }
        memory_over_u32_rect(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                             compositor_picture.premultiplied + from, CP_PLANE, (positive)(x2 - x1), (positive)(y2 - y1));
}

/*
        A picture's square: its side in pixels, and the pixel of it the pointer is on (the dynamic shape's middle). A bitmap's hot pixel
        starts at its hot cell's corner (compositor_pointer_rows), so it is as many pixels in as the cells before the hot cell take, rounded
        up, and the square reaches as many past it as the cells after it take, rounded up: the whole cell is in it at any scale. At a
        whole scale that is the cell's own side times the scale; at a scale between, the cells before the hot cell rounded down would
        start the square inside the first column, and the tip of a resize arrow there (CP_RESIZE_H's, CP_MOVE's) was left out.
*/
static int compositor_pointer_span(const struct compositor_pointer *p, int *hot_x, int *hot_y)
{
        const signed char *hot = compositor_cursor_hot[p->shape];
        int span;

        // A client's picture says its own square (compositor_picture_take).
        if (p->picture)
        {
                *hot_x = p->hot_x, *hot_y = p->hot_y;
                return p->span;
        }
        if (p->dyn)
        {
                span = (CP_DYN_CELL * p->scale + 255) >> 8;
                *hot_x = *hot_y = span / 2;
        }
        else
        {
                int wide, tall;

                *hot_x = (hot[0] * p->scale + 255) >> 8;
                *hot_y = (hot[1] * p->scale + 255) >> 8;
                wide = *hot_x + (((CP_CELL - hot[0]) * p->scale + 255) >> 8);
                tall = *hot_y + (((CP_CELL - hot[1]) * p->scale + 255) >> 8);
                span = wide > tall ? wide : tall;
        }
        return span < CP_SPAN_MOST ? span : CP_SPAN_MOST;
}

/*
        The dynamic shape's covers at a place, in 256ths of a cell from its middle: its fill's and its edge's. It is a rounded rectangle (a
        circle when its corner radius is its half extents): the place's distance to the outline is its distance to the nearest side, or
        to the corner's arc where it is past both of a corner's centre lines, and that distance in pixels is the cover outright, half a
        pixel either side of the outline, so the edge is as smooth as a pixel's 256 levels allow at any size. The edge ink is the cell
        inside the outline, and a pixel of it below a whole scale, where a cell is less: a line thinner than a pixel was a faint grey
        ring round the circle at 50 percent on a light ground. A stretched shape is asked where the place was before the stretch (along
        the motion it is divided by the stretch, across it multiplied, so the area stays), and its distance there is put back in the
        screen's pixels by the length the stretch gives the outline's normal, which keeps the edge as wide all the way round. A place
        deep inside or well outside is answered without a square root.
*/
struct compositor_dyn_pre
{
        long long a, half, reach, line, lined, bound, tip, leg;
};

static void compositor_dyn_cover(const struct compositor_pointer *p, const struct compositor_dyn_pre *pre, long long u, long long v, unsigned char *fill, unsigned char *edge)
{
        long long a = pre->a, reach = pre->reach, x, y, away, nx = 64, ny = 0, along, across, outer, inner;
        long long line = pre->line, lined = pre->lined;

        *fill = *edge = 0;
        if (u > pre->bound || u < -pre->bound || v > pre->bound || v < -pre->bound)
                return;
        if (p->lens)
        {
                // Core circle of radius r (p->hh) with pointy ends at +-tip (p->hw) along (ux, uy): the convex hull of the circle and
                // its two tips, so the middle keeps the circle's aspect and curvature and the ends taper tangentially to points.
                long long la = (u * p->ux + v * p->uy) / 64, b = (v * p->ux - u * p->uy) / 64;
                long long r = p->hh, tip = pre->tip;

                la = la < 0 ? -la : la;
                b = b < 0 ? -b : b;
                if (tip == r)
                {
                        away = (long long)compositor_isqrt((unsigned long long)(la * la + b * b)) - r;
                        goto lined;
                }
                {
                        long long leg = pre->leg;

                        if (la * leg <= b * r)
                                away = (long long)compositor_isqrt((unsigned long long)(la * la + b * b)) - r;
                        else if ((la - tip) * leg >= b * r)
                                away = (long long)compositor_isqrt((unsigned long long)((la - tip) * (la - tip) + b * b));
                        else
                                away = (la * r + b * leg) / tip - r;
                }
                goto lined;
        }
        if (p->grow)
        {
                along = (u * p->ux + v * p->uy) / 64 * 256 / a, across = (v * p->ux - u * p->uy) / 64 * a / 256;
                u = (along * p->ux - across * p->uy) / 64;
                v = (along * p->uy + across * p->ux) / 64;
        }
        x = (u < 0 ? -u : u) - (p->hw - p->c), y = (v < 0 ? -v : v) - (p->hh - p->c); // from the corner's centre
        if (x > 0 && y > 0)
        {
                long long square = x * x + y * y, out = p->c + reach, in = p->c - lined * a / 256 - reach;

                if (square >= out * out)
                        return;
                if (in > 0 && square <= in * in)
                {
                        *fill = 255;
                        return;
                }
                away = (long long)compositor_isqrt((unsigned long long)square);
                nx = x * 64 / away, ny = y * 64 / away, away -= p->c;
        }
        else if (x > y)
                away = x - p->c;
        else
                away = y - p->c, nx = 0, ny = 64;
        if (p->grow)
        {
                nx = u < 0 ? -nx : nx, ny = v < 0 ? -ny : ny;
                along = (nx * p->ux + ny * p->uy) / 64 * 256 / a, across = (ny * p->ux - nx * p->uy) / 64 * a / 256;
                along = (long long)compositor_isqrt((unsigned long long)(along * along + across * across));
                away = away * 64 / (along ? along : 1);
        }
lined:
        away = away * p->scale / 256; // 256ths of a pixel from the outline, less inside it
        outer = away < -128 ? 256 : away > 128 ? 0 : 128 - away;
        inner = away + line < -128 ? 256 : away + line > 128 ? 0 : 128 - away - line;
        *edge = (unsigned char)((outer - inner) * 255 / 256);
        *fill = (unsigned char)(inner ? inner * 255 / (256 - (outer - inner)) : 0);
}

/*
        Rows first to first + rows of a picture, span pixels from its left, a row pitch bytes after the one before it: the cover of its
        fill ink a pixel, and of its edge ink over that. The fill's cover is of what the edge leaves, so that mixing the fill and then
        the edge gives each ink its own share. The dynamic shape's are worked out at the pixel's middle (compositor_dyn_cover). A
        bitmap's pixel is n by n samples of its cell, each at its place (from the hot pixel's corner, over the scale) and no two in one
        row or one column of the pixel, so at a whole scale every sample of a pixel is in one cell and the covers are all or none, and at
        a scale between an edge along the rows is as smooth as one across them. Below a whole scale a pixel is more than a cell, and the
        bitmaps' lines are a cell wide: each ink's samples count 256 over the scale times (the edge's first, the fill's out of what the
        edge leaves, neither past the whole pixel), so a line of one cell is a pixel's worth of its ink wherever it falls. Counted once
        each, the outline of the arrow at 80 percent was a grey of 80 percent at best and two greys of 40 where it fell between two
        pixels, and at 50 percent a line was half grey all along.

        What depends on the picture alone is made once a call: the place of each column's samples, as the cell across they fall in (-1
        off the bitmap), and the two covers of every count of edge and fill samples a pixel can have, so a pixel is its samples looked up
        and counted, and nothing is divided for it.
*/
static void compositor_pointer_rows(const struct compositor_pointer *p, int span, int hot_x, int hot_y, int first, int rows,
                                    unsigned char *fill, unsigned char *edge, int pitch)
{
        int n = p->dyn ? 1 : p->scale < 4 * 256 ? 4 : 2, all = n * n;
        long long step = (32768ll << 16) / (all * p->scale); // half a sample's column, in 256ths of a cell, Q16
        long long ox = hot_x * 65536ll / p->scale + (p->dyn ? 128 : -256 * compositor_cursor_hot[p->shape][0]);
        long long oy = hot_y * 65536ll / p->scale + (p->dyn ? 128 : -256 * compositor_cursor_hot[p->shape][1]);
        signed char across[CP_SPAN_MOST * 16], down[16];
        unsigned char share[17 * 17][2]; // [edge samples * (all + 1) + fill samples]: the fill's cover, the edge's

        for (int e = 0; !p->dyn && e <= all; e++)
                for (int f = 0; e + f <= all; f++)
                {
                        int lined = e * 255 / all, filled = f ? f * 255 / (all - e) : 0;

                        if (p->scale < 256)
                        {
                                lined = e * 255 * 256 / (all * p->scale), filled = f * 255 * 256 / (all * p->scale);
                                lined = lined < 255 ? lined : 255;
                                filled = filled < 255 - lined ? filled : 255 - lined;
                                filled = filled ? filled * 255 / (255 - lined) : 0;
                        }
                        share[e * (all + 1) + f][0] = (unsigned char)filled, share[e * (all + 1) + f][1] = (unsigned char)lined;
                }
        for (int k = 0; !p->dyn && k < span * all; k++)
        {
                long long u = ((2ll * k + 1) * step >> 16) - ox;

                across[k] = (signed char)(u >= 0 && u < CP_CELL * 256 ? u >> 8 : -1);
        }
        struct compositor_dyn_pre pre = {0};

        if (p->dyn)
        {
                pre.a = 256 + p->grow;
                pre.half = 32768 / p->scale + 1;
                pre.reach = pre.half * pre.a / 256 + 1;
                pre.line = p->scale < 256 ? 256 : p->scale;
                pre.lined = pre.line * 256 / p->scale;
                pre.bound = (p->hw + p->hh) * pre.a / 256 + pre.half;
                pre.tip = p->hw > p->hh ? p->hw : p->hh;
                pre.leg = p->lens && pre.tip > p->hh ? (long long)compositor_isqrt((unsigned long long)(pre.tip * pre.tip - (long long)p->hh * p->hh)) : 0;
        }
        for (int row = first; row < first + rows; row++, fill += pitch, edge += pitch)
        {
                if (p->dyn)
                {
                        long long v = ((2ll * row + 1) * step >> 16) - oy;
                        if (v > pre.bound || v < -pre.bound)
                        {
                                memory_zero(fill, (positive)span);
                                memory_zero(edge, (positive)span);
                                continue;
                        }
                        for (int column = 0; column < span; column++)
                                compositor_dyn_cover(p, &pre, ((2ll * column + 1) * step >> 16) - ox, v, &fill[column], &edge[column]);
                        continue;
                }
                for (int k = 0; k < all; k++)
                {
                        long long v = ((2ll * (row * all + k) + 1) * step >> 16) - oy;

                        down[k] = (signed char)(v >= 0 && v < CP_CELL * 256 ? v >> 8 : -1);
                }
                for (int column = 0; column < span; column++)
                {
                        const signed char *at = across + column * all;
                        int inks[3] = {0, 0, 0};

                        // Sample i, j of the pixel is the cell across of its column's sample i n + j, down of its row's sample j n + i.
                        for (int j = 0; j < n; j++)
                                for (int i = 0; i < n; i++)
                                {
                                        int u = at[i * n + j], v = down[j * n + i];
                                        char ink = u >= 0 && v >= 0 ? compositor_cursor_art[p->shape][v][u] : '.';

                                        inks[ink == '#' ? 2 : ink == 'o']++;
                                }
                        fill[column] = share[inks[2] * (all + 1) + inks[1]][0];
                        edge[column] = share[inks[2] * (all + 1) + inks[1]][1];
                }
        }
}

// One row of a picture, span bytes of each cover: the settings' try-out of the pointer makes its picture a row at a time.
static void compositor_pointer_row(const struct compositor_pointer *p, int row, int span, int hot_x, int hot_y, unsigned char *fill, unsigned char *edge)
{
        compositor_pointer_rows(p, span, hot_x, hot_y, row, 1, fill, edge, span);
}

/*
        The covers of the pointer's picture, made once a picture and read by every present that draws it (compositor_cursor_draw, and
        the GPU's cover tables, compositor_gpu_cursor): the fill's span rows of span bytes, then the edge's, for the picture key is of.
        A picture that has not changed is not made again, so a pointer moved costs its mixes and no more; one that changes every
        frame (the dynamic shape on its way, a held button's sway) is made once a frame, which is what its rows cost before. Mapped
        once, at the largest picture's size.

        One picture at a time is drawn: the bands of a present all draw the pointer drawn into it, and nothing else draws one while
        they run. A band that finds another making it makes its own rows (compositor_pointer_rows) and does not wait.
*/
static struct
{
        struct compositor_pointer key;
        unsigned char *cover;
        int made, busy;
} compositor_pointer_table;

static const unsigned char *compositor_pointer_cover(const struct compositor_pointer *p)
{
        int hot_x, hot_y, span;

        if (__atomic_load_n(&compositor_pointer_table.made, __ATOMIC_ACQUIRE) && !memory_compare(&compositor_pointer_table.key, p, sizeof(*p)))
                return compositor_pointer_table.cover;
        if (__atomic_exchange_n(&compositor_pointer_table.busy, 1, __ATOMIC_ACQUIRE))
                return 0;
        __atomic_store_n(&compositor_pointer_table.made, 0, __ATOMIC_RELAXED);
        if (!compositor_pointer_table.cover)
                compositor_pointer_table.cover = (unsigned char *)compositor_map_zeroed(2ul * CP_SPAN_MOST * CP_SPAN_MOST);
        if (compositor_pointer_table.cover)
        {
                span = compositor_pointer_span(p, &hot_x, &hot_y);
                compositor_pointer_rows(p, span, hot_x, hot_y, 0, span, compositor_pointer_table.cover, compositor_pointer_table.cover + span * span, span);
                compositor_pointer_table.key = *p;
                __atomic_store_n(&compositor_pointer_table.made, 1, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&compositor_pointer_table.busy, 0, __ATOMIC_RELEASE);
        return compositor_pointer_table.cover && compositor_pointer_table.made ? compositor_pointer_table.cover : 0;
}

/*
        A pointer's picture with the pointer at x, y, cut to the target's damage: its covers mix the fill ink and then the edge ink over
        it (memory_mix_const_u32_rect, a pixel at a time what the GPU's mix records do), from the picture's table, or a row at a time from
        its own rows where the table is not there. A fill ink with an alpha is the plane's: its pixels take the cover of both inks as
        their alpha, over colours the mixes have already multiplied by it.
*/
static void compositor_cursor_draw(const struct compositor_target *t, int x, int y, const struct compositor_pointer *p, unsigned int fill, unsigned int edge)
{
        unsigned char rows[2][CP_SPAN_MOST];
        int hot_x, hot_y, span = compositor_pointer_span(p, &hot_x, &hot_y), left = x - hot_x, top = y - hot_y;
        int x1 = left > t->x1 ? left : t->x1, x2 = left + span < t->x2 ? left + span : t->x2;
        int y1 = top > t->y1 ? top : t->y1, y2 = top + span < t->y2 ? top + span : t->y2;
        const unsigned char *made;

        // A client's own picture is its pixels, mixed by their alpha (or the plane's, written as they are).
        if (p->picture)
        {
                compositor_picture_draw(t, left, top, p, (fill >> 24) != 0);
                return;
        }
        if (x2 <= x1 || y2 <= y1)
                return;
        made = compositor_pointer_cover(p);
        for (int row = y1, band; row < y2; row += band)
        {
                unsigned int *pixels = t->pixels + (unsigned long)(row - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);
                const unsigned char *covers = made ? made + (unsigned long)(row - top) * span + (x1 - left) : rows[0] + (x1 - left);
                int pitch = made ? span : CP_SPAN_MOST;
                unsigned long plane = made ? (unsigned long)span * span : CP_SPAN_MOST;

                band = made ? y2 - row : 1;
                if (!made)
                        compositor_pointer_rows(p, span, hot_x, hot_y, row - top, 1, rows[0], rows[1], CP_SPAN_MOST);
                memory_mix_const_u32_rect(pixels, t->pitch, covers, (positive)pitch, (positive)(x2 - x1), (positive)band, fill & 0xffffffu, 255);
                memory_mix_const_u32_rect(pixels, t->pitch, covers + plane, (positive)pitch, (positive)(x2 - x1), (positive)band, edge & 0xffffffu, 255);
                for (int down = 0; (fill >> 24) && down < band; down++)
                        for (int at = 0; at < x2 - x1; at++)
                        {
                                unsigned int f = covers[(unsigned long)down * pitch + at], e = covers[plane + (unsigned long)down * pitch + at];
                                unsigned int v = f * (255u - e) + 127u;

                                pixels[(unsigned long)down * t->pitch + at] |= (e + ((v + 1u + (v >> 8)) >> 8)) << 24;
                        }
        }
}

#ifdef MOONWATER_GPU_C
/*
        The pointer the compositor draws into the GPU's scene, over everything: its two covers as tables of the pool and a mix of each ink
        by its table, the same two mixes compositor_cursor_draw makes. The tables are kept by the count of pictures, so one that has not
        changed is not made again. False when the pool has no room for them or a record no pitch for the picture's side (the processor
        draws the present).
*/
static b32 compositor_gpu_cursor(int x, int y, const struct compositor_pointer *p, unsigned int fill, unsigned int edge)
{
        static struct compositor_pointer last;
        static int count;
        unsigned char *table[2];
        int hot_x, hot_y, span = compositor_pointer_span(p, &hot_x, &hot_y), left = x - hot_x, top = y - hot_y;
        unsigned int at[2];
        const unsigned char *made;

        // A client's own RGBA cursor: cache premultiplied pixels in the GPU tables pool and emit op 11 (image over) so custom cursors never force CPU fallback.
        if (p->picture)
        {
                int px = left + compositor_picture.left, py = top + compositor_picture.top;
                int pw = compositor_picture.width, ph = compositor_picture.height;
                unsigned char *slot = 0;
                unsigned int pic_at;
                struct compositor_frame cur;

                if (p->picture != compositor_picture.made || pw <= 0 || ph <= 0)
                        return true;
                if (px + pw <= compositor_gscene.clip[0] || py + ph <= compositor_gscene.clip[1] ||
                    px >= compositor_gscene.clip[2] || py >= compositor_gscene.clip[3])
                        return true;
                memory_zero(&cur, sizeof(cur));
                cur.x = px, cur.y = py, cur.width = pw, cur.height = ph, cur.pitch = (unsigned long)pw;
                if (gpu_backend == &gpu_i915_row)
                {
                        positive tile_va = 0;
                        unsigned int *tile = compositor_gpu_i915_tile_alloc((unsigned long)pw * (unsigned long)ph, &tile_va);

                        if (!tile)
                                return compositor_gscene.why = COMPOSITOR_GPU_WHY_POOL, false;
                        memory_copy_rect_u32(tile, (positive)pw, compositor_picture.premultiplied, CP_PLANE, (positive)pw, (positive)ph);
                        cur.gpu_base = tile_va;
                }
                else
                {
                        pic_at = compositor_gpu_slot_tag(pw, ph, 6, 0, p->picture, (unsigned int)(pw * ph * 4), &slot);
                        if (!pic_at)
                        {
                                compositor_gscene.why = COMPOSITOR_GPU_WHY_POOL;
                                return false;
                        }
                        if (slot)
                                memory_copy_rect_u32((unsigned int *)slot, (positive)pw, compositor_picture.premultiplied, CP_PLANE, (positive)pw, (positive)ph);
                        cur.gpu_base = GPU_VA + pic_at;
                }
                compositor_gpu_rect(11, px, py, px + pw, py + ph, 0, &cur, 0, 0);
                return true;
        }
        if (left + span <= compositor_gscene.clip[0] || top + span <= compositor_gscene.clip[1] ||
            left >= compositor_gscene.clip[2] || top >= compositor_gscene.clip[3])
                return true;
        made = compositor_pointer_cover(p);
        if (memory_compare(&last, p, sizeof(last)))
                last = *p, count++;
        {
                unsigned int kept = compositor_gscene.kept, used = compositor_gscene.tables_used;

                at[0] = span > 255 || !made ? 0 : compositor_gpu_slot_tag(span, count, 5, 0, 0, (unsigned int)(span * span), &table[0]);
                at[1] = span > 255 || !made ? 0 : compositor_gpu_slot_tag(span, count, 5, 1, 0, (unsigned int)(span * span), &table[1]);
                if (!at[0] || !at[1])
                {
                        compositor_gscene.kept = kept, compositor_gscene.tables_used = used;
                        compositor_gscene.why = COMPOSITOR_GPU_WHY_POOL;
                        return false;
                }
        }
        // A table new to the pool is the picture's covers, the fill's or the edge's.
        for (int ink = 0; ink < 2; ink++)
                if (table[ink])
                        memory_copy(table[ink], made + (unsigned long)ink * span * span, (positive)span * (positive)span);
        compositor_gpu_rect(5, left, top, left + span, top + span, fill, 0, at[0], span);
        compositor_gpu_rect(5, left, top, left + span, top + span, edge, 0, at[1], span);
        return true;
}

#endif

_Static_assert(COMPOSITOR_LIT_LEFT == CP_EDGE_LEFT && COMPOSITOR_LIT_RIGHT == CP_EDGE_RIGHT && COMPOSITOR_LIT_TOP == CP_EDGE_TOP &&
                   COMPOSITOR_LIT_BOTTOM == CP_EDGE_BOTTOM,
               "a frame's lit edges are the hand's edges");

/*
        The edges of a window, x1, y1 to x2, y2 (its contents), a point is on: within grip outside them, and within in
        inside them (top_in at the top, where the window's titlebar starts), each edge by its own side; a corner where
        it is on two. With corner, a point on one edge within corner of the window's corner along it is on the corner
        too: a finger is not a pixel. 0 when it is on none, inside the window or outside the grip.
*/
static unsigned int compositor_edges_at(int px, int py, int x1, int y1, int x2, int y2, int grip, int in, int top_in,
                                        int corner)
{
        unsigned int edges = 0;

        if (px < x1 - grip || px >= x2 + grip || py < y1 - grip || py >= y2 + grip)
                return 0;
        edges |= px < x1 + in ? CP_EDGE_LEFT : px >= x2 - in ? CP_EDGE_RIGHT : 0;
        edges |= py < y1 + top_in ? CP_EDGE_TOP : py >= y2 - in ? CP_EDGE_BOTTOM : 0;
        if (corner && (edges & (CP_EDGE_LEFT | CP_EDGE_RIGHT)) && !(edges & (CP_EDGE_TOP | CP_EDGE_BOTTOM)))
                edges |= py < y1 + corner ? CP_EDGE_TOP : py >= y2 - corner ? CP_EDGE_BOTTOM : 0;
        else if (corner && (edges & (CP_EDGE_TOP | CP_EDGE_BOTTOM)) && !(edges & (CP_EDGE_LEFT | CP_EDGE_RIGHT)))
                edges |= px < x1 + corner ? CP_EDGE_LEFT : px >= x2 - corner ? CP_EDGE_RIGHT : 0;
        return edges;
}

/* ---- end of the pointer ---- */

