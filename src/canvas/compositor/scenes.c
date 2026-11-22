#include "scenes.h"

// The Scenes overview's model and layout (C8, comp-manage): pure arithmetic from rects in, rects out. The overview draws
// nothing here; the drawing and the input sit on these rects. Rects are {x, y, w, h}.

// The fit of count windows in cols columns of the area: the scale, in 1/1024 of a window, that every window's rect takes to fit
// its cell (the least of them), never more than its own size (1024); -1 when a cell has no room.
static long long compositor_scenes_fit(const int (*size)[2], unsigned int count, const int area[4], int gap, unsigned int cols)
{
        unsigned int rows = (count + cols - 1) / cols;
        long long cw = ((long long)area[2] - (long long)(cols - 1) * gap) / cols, ch = ((long long)area[3] - (long long)(rows - 1) * gap) / rows;
        long long fit = -1;

        if (cw <= 0 || ch <= 0)
                return -1;
        for (unsigned int at = 0; at < count; at++)
        {
                long long w = size[at][0] > 0 ? size[at][0] : 1, h = size[at][1] > 0 ? size[at][1] : 1;
                long long s = cw * h <= ch * w ? cw * 1024 / w : ch * 1024 / h;

                s = s > 1024 ? 1024 : s;
                if (fit < 0 || s < fit)
                        fit = s;
        }
        return fit;
}

// The column count that fits every window the largest, ties to fewer columns (0 for none).
static unsigned int compositor_scenes_cols(const int (*size)[2], unsigned int count, const int area[4], int gap)
{
        unsigned int best_cols = 0;
        long long best = -1;

        if (!count || area[2] <= 0 || area[3] <= 0)
                return 0;
        for (unsigned int cols = 1; cols <= count; cols++)
        {
                long long fit = compositor_scenes_fit(size, count, area, gap, cols);

                if (fit > best)
                        best = fit, best_cols = cols;
        }
        return best < 0 ? 0 : best_cols;
}

// The windows of one Scene, laid out large and without overlap in the area, each in its own aspect and never larger than it is
// (Exposé). Returns the number placed (all of them). Window `at` takes cell `at`, row by row, in the grid of the column count
// that fits every window's rect the largest.
static unsigned int compositor_scenes_grid(const int (*size)[2], unsigned int count, const int area[4], int gap, int (*out)[4])
{
        unsigned int cols = compositor_scenes_cols(size, count, area, gap);

        if (!cols)
                return 0;
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int rows = (count + cols - 1) / cols, col = at % cols, row = at / cols;
                long long cw = ((long long)area[2] - (long long)(cols - 1) * gap) / cols, ch = ((long long)area[3] - (long long)(rows - 1) * gap) / rows;
                long long w = size[at][0] > 0 ? size[at][0] : 1, h = size[at][1] > 0 ? size[at][1] : 1;
                long long fw, fh;

                if (cw * h <= ch * w)
                        fw = cw, fh = h * cw / w;
                else
                        fh = ch, fw = w * ch / h;
                if (fw > w || fh > h)
                        fw = w, fh = h;
                out[at][0] = area[0] + (int)(col * (cw + gap) + (cw - fw) / 2);
                out[at][1] = area[1] + (int)(row * (ch + gap) + (ch - fh) / 2);
                out[at][2] = (int)fw;
                out[at][3] = (int)fh;
        }
        return count;
}

// The Scene strip along the top: one card per Scene at the screen's aspect, and a "+" card last. Cards are the same height and
// shrink together to fit the width; the row of them is centred in the strip. Returns the number of cards (scenes + 1).
static unsigned int compositor_scenes_strip(unsigned int scenes, const int area[4], int screen_w, int screen_h, int gap, int (*out)[4])
{
        unsigned int cards = scenes + 1;
        long long ch = area[3], cw = ch * screen_w / (screen_h > 0 ? screen_h : 1), left;

        if (!cards || area[2] <= 0 || area[3] <= 0 || screen_w <= 0 || screen_h <= 0)
                return 0;
        if ((long long)cards * cw + (long long)(cards - 1) * gap > area[2])
        {
                cw = ((long long)area[2] - (long long)(cards - 1) * gap) / cards;
                ch = cw * screen_h / screen_w;
        }
        left = ((long long)area[2] - (long long)cards * cw - (long long)(cards - 1) * gap) / 2;
        for (unsigned int at = 0; at < cards; at++)
        {
                out[at][0] = area[0] + (int)(left + at * (cw + gap));
                out[at][1] = area[1] + (int)((area[3] - ch) / 2);
                out[at][2] = (int)cw;
                out[at][3] = (int)ch;
        }
        return cards;
}

// The index of the rect under (x, y), or -1. Rects are tested in order; the first that holds the point wins.
static int compositor_scenes_hit(const int (*rect)[4], unsigned int count, int x, int y)
{
        for (unsigned int at = 0; at < count; at++)
                if (x >= rect[at][0] && x < rect[at][0] + rect[at][2] && y >= rect[at][1] && y < rect[at][1] + rect[at][3])
                        return (int)at;
        return -1;
}


// The overview's model: what is on it, from the screen and the Spaces. The Scene cards are the Spaces of the screen, then "+";
// the windows are the current Scene's (the rows whose Space is the active one), laid out large in the middle.
static int compositor_scenes_gap(void)
{
        int gap = 16 * compositor.scale / 100;

        return gap < 4 ? 4 : gap;
}

// What the overview draws, kept between presents (C8 step 4): the layout's sizes, the generations it was made at, and the
// wallpaper's miniature every Scene card is drawn on.
static struct
{
        int size[COMPOSITOR_SCENES_WINDOWS][2]; // each laid out window's size when it was laid out: a new size lays them out again
        unsigned int stack_gen, spaces_gen;     // the stack and the Spaces the layout was made from
        unsigned int *wall;                     // the wallpaper's miniature, a card's size (its pitch the card's width)
        unsigned long wall_bytes;
        const unsigned int *wall_of;            // the wallpaper it was made from, at its turn, and the screen and card it was made for
        b64 wall_turn;
        int wall_screen[4], wall_w, wall_h;
        b64 shown, speed; // how far the overview is shown, 0 to COMPOSITOR_SCENES_FULL, and its speed (the same units a second): the kit's spring
        positive stepped; // when the spring was last stepped (0: at rest)
} compositor_scenes_kept;

/*
        The overview's motion (C8 step 5). It is shown by a share, 0 closed to all of it open: the current Scene's windows go from where
        they are to their rects by it, and the strip comes down from above the screen with it. Opened or closed, the share goes to its
        end on the kit's spring home (ui_body_spring_step: critically damped, 90 percent of the way in 0.17 s, at the kit's pace), from
        the speed it had; a drag from the globe sets it from the drag's progress, and its release carries the hand's speed into the
        spring. Reduced motion has no spring: the share is its end at once, and a drag's progress.
*/
#define COMPOSITOR_SCENES_FULL (1000 * UI_ONE) // the whole share, in units the spring rests within a quarter of (BODY_REST)
static inline b32 ui_body_spring_step(b64 address_to pos, b64 address_to vel, b64 rest, b64 dt); // ui.c, the kit's spring

// The share shown, Q30 (0 to UI_ONE).
static b64 compositor_scenes_t(void)
{
        if (!compositor_glides())
                return compositor.scenes.open ? UI_ONE : compositor.scenes.drag ? (b64)compositor.scenes.progress * UI_ONE / COMPOSITOR_SCENES_PROGRESS_ONE : 0;
        return compositor_scenes_kept.shown / 1000;
}

// Whether a window is one the overview lays out: shown, not minimised or closing, not a popup and in a Space.
static int compositor_scenes_member(int row)
{
        return row >= 0 && row < COMPOSITOR_ROWS && compositor.showing[row] && !(compositor.state[row] & (WINDOW_MINIMIZED | WINDOW_CLOSING)) &&
               (!compositor_popup_parent || compositor_popup_parent[row] < 0) && !compositor_row_space_exempt(row) && compositor.row_space;
}

static void compositor_scenes_damage(void)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor.scenes.screen;

        if (screen >= 0 && (unsigned int)screen < count)
                compositor_damage(screens[screen][0], screens[screen][1], screens[screen][2], screens[screen][3]);
}

static void compositor_scenes_relayout(void)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor.scenes.screen, gap = compositor_scenes_gap(), strip_h, strip[4], win[4], room[4];
        int sizes[COMPOSITOR_SCENES_WINDOWS][2], centre[COMPOSITOR_SCENES_WINDOWS][2];
        unsigned int n = 0, cols;
        struct compositor_space *active;

        if (screen < 0 || (unsigned int)screen >= count)
                return;
        compositor_scenes_kept.stack_gen = compositor.stack_gen, compositor_scenes_kept.spaces_gen = compositor.spaces.gen;
        // The overview is laid out in the screen's room: the band keeps its place over it, on any edge.
        compositor_room(screens[screen], room);
        strip_h = room[3] * 18 / 100;
        strip[0] = room[0] + gap, strip[1] = room[1] + gap, strip[2] = room[2] - 2 * gap, strip[3] = strip_h;
        win[0] = room[0] + 2 * gap, win[1] = room[1] + strip_h + 3 * gap, win[2] = room[2] - 4 * gap, win[3] = room[3] - strip_h - 5 * gap;
        compositor.scenes.cards = compositor_scenes_strip((unsigned int)compositor_spaces_count_on(screen), strip, screens[screen][2],
                                                          screens[screen][3], gap, compositor.scenes.card);
        active = compositor_space_at(screen, compositor_spaces_active_index(screen));
        for (unsigned int at = 0; at < compositor.stacked && n < COMPOSITOR_SCENES_WINDOWS && active; at++)
        {
                int row = (int)compositor.stack[at];

                if (!compositor_scenes_member(row) || compositor.row_space[row] != active->id)
                        continue;
                compositor.scenes.row[n] = row;
                sizes[n][0] = (int)compositor.w[row], sizes[n][1] = (int)compositor.h[row];
                centre[n][0] = compositor.x[row] + sizes[n][0] / 2, centre[n][1] = compositor.y[row] + sizes[n][1] / 2;
                n++;
        }
        // Each window takes the cell nearest where it is: the windows by their centre's height, a grid row of them at a time, and
        // each row left to right; so the overview moves every window the least it can.
        cols = compositor_scenes_cols((const int (*)[2])sizes, n, win, gap);
        for (unsigned int i = 1; i < n; i++)
                for (unsigned int j = i; j > 0 && centre[j][1] < centre[j - 1][1]; j--)
                {
                        int r = compositor.scenes.row[j], s0 = sizes[j][0], s1 = sizes[j][1], c0 = centre[j][0], c1 = centre[j][1];

                        compositor.scenes.row[j] = compositor.scenes.row[j - 1], sizes[j][0] = sizes[j - 1][0], sizes[j][1] = sizes[j - 1][1];
                        centre[j][0] = centre[j - 1][0], centre[j][1] = centre[j - 1][1];
                        compositor.scenes.row[j - 1] = r, sizes[j - 1][0] = s0, sizes[j - 1][1] = s1, centre[j - 1][0] = c0, centre[j - 1][1] = c1;
                }
        for (unsigned int first = 0; cols && first < n; first += cols)
                for (unsigned int i = first + 1; i < n && i < first + cols; i++)
                        for (unsigned int j = i; j > first && centre[j][0] < centre[j - 1][0]; j--)
                        {
                                int r = compositor.scenes.row[j], s0 = sizes[j][0], s1 = sizes[j][1], c0 = centre[j][0], c1 = centre[j][1];

                                compositor.scenes.row[j] = compositor.scenes.row[j - 1], sizes[j][0] = sizes[j - 1][0], sizes[j][1] = sizes[j - 1][1];
                                centre[j][0] = centre[j - 1][0], centre[j][1] = centre[j - 1][1];
                                compositor.scenes.row[j - 1] = r, sizes[j - 1][0] = s0, sizes[j - 1][1] = s1, centre[j - 1][0] = c0, centre[j - 1][1] = c1;
                        }
        memory_copy(compositor_scenes_kept.size, sizes, (positive)n * sizeof(sizes[0]));
        compositor.scenes.windows = compositor_scenes_grid((const int (*)[2])sizes, n, win, gap, compositor.scenes.rect);
}

// The Scene card or window rect under (x, y). A window is an index into rect, a card an index into card; -1 is none.
static int compositor_scenes_card_at(int x, int y)
{
        return compositor_scenes_hit(compositor.scenes.card, compositor.scenes.cards, x, y);
}

static int compositor_scenes_window_at(int x, int y)
{
        int at = compositor_scenes_hit(compositor.scenes.rect, compositor.scenes.windows, x, y);

        return at < 0 ? COMPOSITOR_NONE : compositor.scenes.row[at];
}

// The Space id of Scene card i (the "+" card has none).
static unsigned int compositor_scenes_sid(int card)
{
        struct compositor_space *sp = compositor_space_at(compositor.scenes.screen, card);

        return sp ? sp->id : 0;
}

// The overview opens on a screen: the Scene strip and the current Scene's windows are laid out there. Mission Control, the
// drawer this replaces, is closed when it opens.
static void compositor_scenes_open(int screen)
{
        if (screen < 0 || screen >= compositor_output_count())
                return;
        compositor_spaces_ensure();
        compositor_mc_close();
        compositor.scenes.open = 1, compositor.scenes.screen = screen;
        compositor.scenes.pressed = 0;
        compositor_scenes_relayout();
        compositor_scenes_damage();
}

// Where the hand's drag is: its progress as the share, and its speed in the share's units (progress is a whole share across half the
// screen from the band's edge, scenes.h).
static void compositor_scenes_follow(int progress, int velocity)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        unsigned int pos = taskbar_position();
        int screen = compositor.scenes.screen, half = 1;

        if (screen >= 0 && (unsigned int)screen < count)
                half = (pos == TASKBAR_LEFT || pos == TASKBAR_RIGHT ? screens[screen][2] : screens[screen][3]) / 2;
        compositor_scenes_kept.shown = (b64)progress * COMPOSITOR_SCENES_FULL / COMPOSITOR_SCENES_PROGRESS_ONE;
        compositor_scenes_kept.speed = (b64)velocity * COMPOSITOR_SCENES_FULL / (half > 0 ? half : 1);
}

static void compositor_scenes_close(void)
{
        if (!compositor.scenes.open)
                return;
        compositor_scenes_damage();
        compositor.scenes.open = 0, compositor.scenes.pressed = 0;
}

// A click that did not drag: a window is focused and the overview leaves; a Scene card switches to that Scene and leaves; the
// "+" card adds a Scene and stays open; anything else leaves.
static void compositor_scenes_click(int x, int y)
{
        int card = compositor_scenes_card_at(x, y), row = compositor_scenes_window_at(x, y);

        if (row != COMPOSITOR_NONE)
        {
                compositor_scenes_close();
                compositor_focus(row);
                return;
        }
        if (card >= 0 && (unsigned int)card + 1 < compositor.scenes.cards)
        {
                int screen = compositor.scenes.screen;

                compositor_scenes_close();
                compositor_space_switch_index(screen, card);
                compositor_scenes_relayout(); // the Scene switched to goes home from the overview, its windows from their rects
                return;
        }
        if (card >= 0)
        {
                compositor_space_add(compositor.scenes.screen, 1);
                compositor_scenes_relayout();
                compositor_scenes_damage();
                return;
        }
        compositor_scenes_close();
}

// A window dropped on a Scene card moves to that Scene (its membership changes; the overview stays open). Dropped anywhere else
// it stays where it was.
static void compositor_scenes_drop(int row, int x, int y)
{
        int card = compositor_scenes_card_at(x, y);

        if (row == COMPOSITOR_NONE || card < 0 || (unsigned int)card + 1 >= compositor.scenes.cards)
                return;
        compositor_space_move_row(row, compositor_scenes_sid(card), 0);
        compositor_scenes_relayout();
        compositor_scenes_damage();
}

// The overview's button: a press records where it began (and the window under it), a release is a click or, after a move, a
// drop. A press outside the overview, or a right button, leaves it.
static void compositor_scenes_button(unsigned int code, int down)
{
        int slop = 8 * compositor.scale / 100, dx, dy;

        if (code != COMPOSITOR_BTN_LEFT)
        {
                if (down)
                        compositor_scenes_close();
                return;
        }
        if (down)
        {
                compositor.scenes.pressed = 1, compositor.scenes.moved = 0;
                compositor.scenes.press_x = compositor.pointer_x, compositor.scenes.press_y = compositor.pointer_y;
                compositor.scenes.press_row = compositor_scenes_window_at(compositor.pointer_x, compositor.pointer_y);
                return;
        }
        if (!compositor.scenes.pressed)
                return;
        compositor.scenes.pressed = 0;
        dx = compositor.pointer_x - compositor.scenes.press_x, dy = compositor.pointer_y - compositor.scenes.press_y;
        if (dx < 0)
                dx = -dx;
        if (dy < 0)
                dy = -dy;
        if (dx + dy <= (slop < 2 ? 2 : slop))
                compositor_scenes_click(compositor.scenes.press_x, compositor.scenes.press_y);
        else if (compositor.scenes.press_row != COMPOSITOR_NONE)
                compositor_scenes_drop(compositor.scenes.press_row, compositor.pointer_x, compositor.pointer_y);
}

// The overview takes a press while it is open, and the release of a press that began in it. The release of a button held
// from before the overview opened goes to the normal path, so its button and grab are let go.
static int compositor_scenes_takes(unsigned int code, int down)
{
        unsigned int index = code - COMPOSITOR_BTN_LEFT;

        if (!compositor.scenes.open)
                return 0;
        if (down)
                return 1;
        return index < 8 && !((compositor.button_down >> index) & 1);
}

// The globe's drag (scenes.h). BEGIN starts a drag on a screen without opening anything; MOVE sets its progress and velocity;
// END decides: over the swipe speed away from the band commits, over it toward the band cancels, otherwise progress over half
// commits. A commit opens the overview on the screen. Phases outside a drag, and a BEGIN while the overview is open, do nothing.
void compositor_scenes_drag(unsigned int phase, int screen, int progress, int velocity)
{
        int commit;

        if (progress < 0)
                progress = 0;
        if (progress > COMPOSITOR_SCENES_PROGRESS_ONE)
                progress = COMPOSITOR_SCENES_PROGRESS_ONE;
        if (phase == COMPOSITOR_SCENES_DRAG_BEGIN)
        {
                if (compositor.scenes.open || compositor.scenes.drag || screen < 0 || (unsigned int)screen >= compositor_output_count())
                        return;
                compositor_spaces_ensure();
                compositor.scenes.drag = 1, compositor.scenes.screen = screen;
                compositor.scenes.progress = 0, compositor.scenes.velocity = 0;
                compositor_scenes_relayout();
        }
        else if (phase == COMPOSITOR_SCENES_DRAG_MOVE)
        {
                if (!compositor.scenes.drag)
                        return;
                compositor.scenes.progress = progress, compositor.scenes.velocity = velocity;
                compositor_scenes_follow(progress, velocity);
                compositor_scenes_damage();
        }
        else if (phase == COMPOSITOR_SCENES_DRAG_END)
        {
                if (!compositor.scenes.drag)
                        return;
                compositor.scenes.drag = 0;
                compositor_scenes_follow(progress, velocity);
                compositor_scenes_kept.stepped = 0;
                compositor_scenes_damage();
                if (velocity > COMPOSITOR_SCENES_SWIPE)
                        commit = 1;
                else if (velocity < -COMPOSITOR_SCENES_SWIPE)
                        commit = 0;
                else
                        commit = progress > COMPOSITOR_SCENES_PROGRESS_ONE / 2;
                if (commit)
                        compositor_scenes_open(compositor.scenes.screen);
        }
}

/*
        The overview drawn (C8 step 4). present.c's present_build_frames leaves the rows of the overview's screen out of the
        stack's frames (compositor_scenes_hides) and puts the overview's own frames in their place, under the band. So a window is
        not drawn at its size under the overview, and nothing has to cover it: the ground is the wallpaper, drawn under the
        overview's frames as under the desktop's, and the overview costs the screen no more than the desktop does, with no frame
        in which a window and a cover over it are both drawn.

        A window is its own frame (compositor_frame_of: its picture read where the client lends it, the card's address with it)
        moved to the overview's rect and scaled from its picture (source_w by source_h), and its layers (a client's subsurfaces,
        compositor_wl_layers_frames, made while the frame is still at its own size) are moved and scaled with it. The card scales
        each from the client's own picture (compositor_gpu_scale_frames); nothing of it is copied. A Scene card is the wallpaper's
        miniature (compositor_scenes_wall_of, made once and kept) with that Scene's windows over it, cut to the card.

        The frames are drawn by the overview, not the windows (layer: the glass pass leaves them, a glass window's picture is what
        it was made over), and none of them is the row's own (row_plus1 0: a close button's ink is the row's, kept for its return).
*/

// Whether the overview is drawn: open, or on its way open or shut, or dragged out by the globe.
static int compositor_scenes_shown(void)
{
        return compositor.scenes.open || compositor_scenes_t() > 0;
}

// Whether the overview draws a row in its own place or not at all: a window on its screen, but the desktop's ground (a
// background layer) and the keyboard on the screen, which stay where they are.
static int compositor_scenes_hides(int row)
{
        return compositor_scenes_shown() && row >= 0 && row < COMPOSITOR_ROWS && compositor.layer[row] != WINDOW_LAYER_BACKGROUND &&
               !(compositor.asks[row] & WINDOW_KEYBOARD) && compositor_screen_of(row) == compositor.scenes.screen;
}

// The wallpaper's miniature for a card of w by h of the screen s: each pixel the mean of up to 4 by 4 of the wallpaper's under
// it. Made when the wallpaper, its turn, the screen or the card's size changes, and kept; 0 with no wallpaper kept.
static const unsigned int *compositor_scenes_wall_of(const int s[4], int w, int h)
{
        unsigned long bytes = (unsigned long)w * (unsigned long)h * 4;

        if (!compositor_wall || w <= 0 || h <= 0 || s[2] <= 0 || s[3] <= 0 || s[0] < 0 || s[1] < 0 || s[0] + s[2] > compositor_wall_w ||
            s[1] + s[3] > compositor_wall_h)
                return 0;
        if (compositor_scenes_kept.wall && compositor_scenes_kept.wall_of == compositor_wall && compositor_scenes_kept.wall_turn == compositor_wall_turn &&
            compositor_scenes_kept.wall_w == w && compositor_scenes_kept.wall_h == h && !memory_compare(compositor_scenes_kept.wall_screen, s, 4 * sizeof(int)))
                return compositor_scenes_kept.wall;
        if (compositor_scenes_kept.wall_bytes < bytes)
        {
                void *p = compositor_scenes_kept.wall;

                if (!mem_buf_grow(&p, &compositor_scenes_kept.wall_bytes, bytes, MEM_BUF_GPU_FORGET))
                        return compositor_scenes_kept.wall = 0, compositor_scenes_kept.wall_bytes = 0, (const unsigned int *)0;
                compositor_scenes_kept.wall = (unsigned int *)p;
        }
        for (int y = 0; y < h; y++)
        {
                long long y0 = s[1] + (long long)y * s[3] / h, y1 = s[1] + ((long long)y + 1) * s[3] / h;

                for (int x = 0; x < w; x++)
                {
                        long long x0 = s[0] + (long long)x * s[2] / w, x1 = s[0] + ((long long)x + 1) * s[2] / w;
                        long long cx = x1 - x0 > 4 ? 4 : x1 - x0 < 1 ? 1 : x1 - x0, cy = y1 - y0 > 4 ? 4 : y1 - y0 < 1 ? 1 : y1 - y0;
                        unsigned int red = 0, green = 0, blue = 0, taps = 0;

                        for (long long j = 0; j < cy; j++)
                                for (long long i = 0; i < cx; i++)
                                {
                                        long long px = x0 + (x1 - x0) * (2 * i + 1) / (2 * cx), py = y0 + (y1 - y0) * (2 * j + 1) / (2 * cy);
                                        unsigned int c = compositor_wall[py * compositor_wall_w + px];

                                        red += c >> 16 & 255, green += c >> 8 & 255, blue += c & 255, taps++;
                                }
                        compositor_scenes_kept.wall[(unsigned long)y * (unsigned long)w + (unsigned long)x] =
                            0xff000000u | (red / taps) << 16 | (green / taps) << 8 | blue / taps;
                }
        }
        compositor_scenes_kept.wall_of = compositor_wall, compositor_scenes_kept.wall_turn = compositor_wall_turn;
        compositor_scenes_kept.wall_w = w, compositor_scenes_kept.wall_h = h;
        memory_copy(compositor_scenes_kept.wall_screen, s, 4 * sizeof(int));
        return compositor_scenes_kept.wall;
}

// A frame cut to clip (x0, y0, x1, y1): its rect, its picture's part (a scaled frame's source cut by the same share) and the card's
// address with it; a corner cut through is square. False when nothing of it is in the clip.
static int compositor_scenes_cut(struct compositor_frame *f, const int clip[4])
{
        int x0 = f->x > clip[0] ? f->x : clip[0], y0 = f->y > clip[1] ? f->y : clip[1];
        int x1 = f->x + f->width < clip[2] ? f->x + f->width : clip[2], y1 = f->y + f->height < clip[3] ? f->y + f->height : clip[3];
        long long sw = f->source_w > 0 ? f->source_w : f->width, sh = f->source_h > 0 ? f->source_h : f->height, px0, py0, px1, py1;

        if (x1 <= x0 || y1 <= y0 || f->width <= 0 || f->height <= 0)
                return 0;
        if (x0 == f->x && y0 == f->y && x1 == f->x + f->width && y1 == f->y + f->height)
                return 1;
        px0 = (long long)(x0 - f->x) * sw / f->width, py0 = (long long)(y0 - f->y) * sh / f->height;
        px1 = ((long long)(x1 - f->x) * sw + f->width - 1) / f->width, py1 = ((long long)(y1 - f->y) * sh + f->height - 1) / f->height;
        px1 = px1 > sw ? sw : px1 <= px0 ? px0 + 1 : px1, py1 = py1 > sh ? sh : py1 <= py0 ? py0 + 1 : py1;
        if (f->pixels)
        {
                f->pixels += py0 * (long long)f->pitch + px0;
                if (f->gpu_base)
                        f->gpu_base += 4ull * (positive)(py0 * (long long)f->pitch + px0);
        }
        f->square |= (x0 > f->x || y0 > f->y ? 1 : 0) | (x1 < f->x + f->width || y0 > f->y ? 2 : 0) | (x0 > f->x || y1 < f->y + f->height ? 4 : 0) |
                     (x1 < f->x + f->width || y1 < f->y + f->height ? 8 : 0);
        f->x = x0, f->y = y0, f->width = x1 - x0, f->height = y1 - y0;
        f->source_w = (int)(px1 - px0), f->source_h = (int)(py1 - py0);
        return 1;
}

// A window's frames (its own, then its layers) moved from the rect `from` to `to` and each scaled from its picture by the same
// share, then cut to clip. The window's own keeps its corners (radius, in device pixels) and its line (line, 0 for none); a frame
// left with no area is dropped. Answers how many are left, in place.
static unsigned int compositor_scenes_put(struct compositor_frame *span, unsigned int count, const int from[4], const int to[4], const int clip[4],
                                          int radius, int line)
{
        unsigned int kept = 0;

        if (from[2] <= 0 || from[3] <= 0)
                return 0;
        for (unsigned int at = 0; at < count; at++)
        {
                struct compositor_frame f = span[at];
                long long fx0 = f.x - from[0], fy0 = f.y - from[1];
                int x0 = to[0] + (int)(fx0 * to[2] / from[2]), y0 = to[1] + (int)(fy0 * to[3] / from[3]);
                int x1 = to[0] + (int)((fx0 + f.width) * to[2] / from[2]), y1 = to[1] + (int)((fy0 + f.height) * to[3] / from[3]);

                if (!f.source_w || !f.source_h)
                        f.source_w = f.width, f.source_h = f.height;
                f.x = x0, f.y = y0, f.width = x1 - x0, f.height = y1 - y0;
                f.tiled_kind = 0, f.runs = 0;
                f.close = f.minimize = 0, f.lit = 0, f.row_plus1 = 0, f.layer = 1;
                f.preview[2] = f.preview[3] = 0, f.preview_under = 0, f.mark[2] = f.mark[3] = 0;
                if (!at)
                        f.radius = radius, f.line = line, f.square = 0, f.edge = compositor_line_unlit();
                if (f.width <= 0 || f.height <= 0 || (f.pitch && f.pitch < (unsigned long)f.source_w) || !compositor_scenes_cut(&f, clip))
                        continue;
                span[kept++] = f;
        }
        return kept;
}

// Room for a window and as many layers as a window may have (wayland.c's WAYLAND_LAYERS) at frames[at].
#define COMPOSITOR_SCENES_LAYERS 8

// A window's own frame and its layers where it is, at frames[at]: answers how many.
static unsigned int compositor_scenes_own(int row, struct compositor_frame *frames, unsigned int *rows, unsigned int at, unsigned int room)
{
        if (at + 1 > room)
                return 0;
        compositor_row_prepare(row);
        compositor_frame_of(row, &frames[at]);
        rows[at] = (unsigned int)row;
        // A client's tiled picture is read by the card at its own size only (the scene's image op, tiled_kind): the overview draws the
        // row's own picture, which the card brings up to date from it first (compositor_wl_straight_settle: one read of the client's).
        if (frames[at].tiled_kind)
                compositor_wl_straight_settle(row, 0, (long)compositor.h[row]), frames[at].tiled_kind = 0;
        return 1 + (at + 1 + COMPOSITOR_SCENES_LAYERS <= room ? compositor_wl_layers_frames(row, frames, rows, at + 1) : 0);
}

// A rect of the screen s mapped into the card c.
static void compositor_scenes_in_card(const int s[4], const int c[4], const int r[4], int out[4])
{
        long long x0 = c[0] + (long long)(r[0] - s[0]) * c[2] / s[2], y0 = c[1] + (long long)(r[1] - s[1]) * c[3] / s[3];
        long long x1 = c[0] + (long long)(r[0] + r[2] - s[0]) * c[2] / s[2], y1 = c[1] + (long long)(r[1] + r[3] - s[1]) * c[3] / s[3];

        out[0] = (int)x0, out[1] = (int)y0, out[2] = (int)(x1 - x0), out[3] = (int)(y1 - y0);
}

// A plain frame of the overview's own (a card's ground, the "+"), at frames[at] if there is room: answers how many (0 or 1).
static unsigned int compositor_scenes_plain(struct compositor_frame *frames, unsigned int *rows, unsigned int at, unsigned int room, unsigned int filler,
                                            const int r[4], const int clip[4], int scale)
{
        struct compositor_frame *f = &frames[at];

        if (at >= room)
                return 0;
        memory_zero(f, sizeof(*f));
        f->x = r[0], f->y = r[1], f->width = r[2], f->height = r[3], f->scale = scale, f->layer = 1;
        rows[at] = filler;
        return compositor_scenes_cut(f, clip) ? 1 : 0;
}

// Card i where it is drawn: its rect in the strip, lifted above the screen's top by the share not shown.
static void compositor_scenes_card_shown(unsigned int i, const int s[4], int gap, b64 t, int out[4])
{
        const int *c = compositor.scenes.card[i];
        long long lift = (long long)(c[1] + c[3] + gap - s[1]);

        memory_copy(out, c, 4 * sizeof(int));
        out[1] -= (int)((lift * (UI_ONE - t)) >> UI_Q);
}

// The overview's frames at frames[at], the rows' in rows (room of each at the most): the current Scene's windows at their
// rects, then the strip, a card a Scene with its windows on the wallpaper's miniature, and the "+" card. Answers how many.
static unsigned int compositor_scenes_frames(struct compositor_frame *frames, unsigned int *rows, unsigned int at, unsigned int room)
{
        unsigned int count = 0, start = at, filler, span[COMPOSITOR_SCENES_WINDOWS][2];
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor.scenes.screen, scale, radius, line, s[4], clip[4], gap = compositor_scenes_gap();
        struct compositor_space *active;
        const unsigned int *wall = 0;
        b64 t = compositor_scenes_t();

        if (screen < 0 || (unsigned int)screen >= count || !compositor.row_space)
                return 0;
        memory_copy(s, screens[screen], sizeof(s));
        if (s[2] <= 0 || s[3] <= 0)
                return 0;
        clip[0] = s[0], clip[1] = s[1], clip[2] = s[0] + s[2], clip[3] = s[1] + s[3];
        scale = compositor_output_scale(screen) > 0 ? compositor_output_scale(screen) : compositor.scale;
        radius = UI_RADIUS * scale / 100, line = compositor_line_px(scale);
        filler = compositor.scenes.windows ? (unsigned int)compositor.scenes.row[0] : 0;
        active = compositor_space_at(screen, compositor_spaces_active_index(screen));
        // The current Scene's windows in the stack's order, each its own frames where it is (moved after the strip has its copies of them).
        for (unsigned int i = 0; i < compositor.scenes.windows; i++)
                span[i][0] = span[i][1] = 0;
        for (unsigned int k = 0; k < compositor.stacked; k++)
                for (unsigned int i = 0; i < compositor.scenes.windows; i++)
                        if ((unsigned int)compositor.scenes.row[i] == compositor.stack[k] && compositor_scenes_member(compositor.scenes.row[i]))
                        {
                                span[i][0] = at;
                                at += compositor_scenes_own(compositor.scenes.row[i], frames, rows, at, room);
                                span[i][1] = at;
                        }
        // The strip: each Scene's card, its ground the wallpaper's miniature (a fullscreen Scene's black), its windows over it.
        for (unsigned int card = 0; card + 1 < compositor.scenes.cards && card < COMPOSITOR_SPACES_MAX; card++)
        {
                int c[4];
                struct compositor_space *sp = compositor_space_at(screen, (int)card);
                int cut[4];

                compositor_scenes_card_shown(card, s, gap, t, c);
                cut[0] = c[0], cut[1] = c[1], cut[2] = c[0] + c[2], cut[3] = c[1] + c[3];

                if (!sp || !compositor_scenes_plain(frames, rows, at, room, filler, c, clip, scale))
                        continue;
                if (sp->kind != COMPOSITOR_SPACE_FULLSCREEN && !wall)
                        wall = compositor_scenes_wall_of(s, c[2], c[3]);
                if (sp->kind != COMPOSITOR_SPACE_FULLSCREEN && wall)
                        frames[at].pixels = wall + (frames[at].y - c[1]) * c[2] + (frames[at].x - c[0]), frames[at].pitch = (unsigned long)c[2];
                else
                        frames[at].fill = sp->kind == COMPOSITOR_SPACE_FULLSCREEN ? 0xff000000u : compositor_ground | 0xff000000u;
                frames[at].radius = radius, frames[at].line = line;
                frames[at].edge = active && sp->id == active->id ? compositor_ink_role(UI_ACCENT_BRIGHT) & 0xffffffu : compositor_line_unlit();
                at++;
                cut[0] = cut[0] > clip[0] ? cut[0] : clip[0], cut[1] = cut[1] > clip[1] ? cut[1] : clip[1];
                cut[2] = cut[2] < clip[2] ? cut[2] : clip[2], cut[3] = cut[3] < clip[3] ? cut[3] : clip[3];
                for (unsigned int k = 0; k < compositor.stacked && at < room; k++)
                {
                        int row = (int)compositor.stack[k], from[4], to[4];
                        unsigned int own = at, made = 0;

                        if (!compositor_scenes_member(row) || compositor.row_space[row] != sp->id)
                                continue;
                        // A window of the current Scene is copied from its frames above, which are still where it is.
                        for (unsigned int i = 0; i < compositor.scenes.windows && !made; i++)
                                if (compositor.scenes.row[i] == row && span[i][1] > span[i][0] && at + (span[i][1] - span[i][0]) <= room)
                                {
                                        memory_copy(&frames[at], &frames[span[i][0]], (positive)(span[i][1] - span[i][0]) * sizeof(frames[0]));
                                        memory_copy(&rows[at], &rows[span[i][0]], (positive)(span[i][1] - span[i][0]) * sizeof(rows[0]));
                                        made = span[i][1] - span[i][0];
                                }
                        if (!made && !(active && sp->id == active->id))
                                made = compositor_scenes_own(row, frames, rows, at, room);
                        if (!made)
                                continue;
                        from[0] = frames[own].x, from[1] = frames[own].y, from[2] = frames[own].width, from[3] = frames[own].height;
                        compositor_scenes_in_card(s, c, from, to);
                        at = own + compositor_scenes_put(&frames[own], made, from, to, cut, radius * c[2] / s[2], 0);
                }
        }
        // The "+" card: a plate of the kit's surface with a line, and a plus in its middle.
        int plus[4];

        if (compositor.scenes.cards)
                compositor_scenes_card_shown(compositor.scenes.cards - 1, s, gap, t, plus);
        if (compositor.scenes.cards && compositor_scenes_plain(frames, rows, at, room, filler, plus, clip, scale))
        {
                const int *c = plus;
                int arm = (c[2] < c[3] ? c[2] : c[3]) / 4, bar = compositor_px1(2 * scale), cx = c[0] + c[2] / 2, cy = c[1] + c[3] / 2;
                int across[4] = {cx - arm, cy - bar / 2, 2 * arm, bar}, down[4] = {cx - bar / 2, cy - arm, bar, 2 * arm};
                unsigned int ink = compositor_ink_role(UI_ON_SURFACE_VARIANT) | 0xff000000u;

                frames[at].fill = compositor_ink_role(UI_SURFACE_CONTAINER) | 0xff000000u;
                frames[at].radius = radius, frames[at].line = line, frames[at].edge = compositor_line_unlit();
                at++;
                if (compositor_scenes_plain(frames, rows, at, room, filler, across, clip, scale))
                        frames[at++].fill = ink;
                if (compositor_scenes_plain(frames, rows, at, room, filler, down, clip, scale))
                        frames[at++].fill = ink;
        }
        // The current Scene's windows moved to their rects, in place. A frame dropped (no area left) leaves the window's others moved
        // down over it, and a frame of nothing at the span's end.
        for (unsigned int i = 0; i < compositor.scenes.windows; i++)
        {
                unsigned int made = span[i][1] - span[i][0], left;
                const struct compositor_frame *own = &frames[span[i][0]];
                int from[4], to[4];

                if (!made)
                        continue;
                // From where it is, at its own corners and line, to its rect at the overview's, by the share shown.
                from[0] = own->x, from[1] = own->y, from[2] = own->width, from[3] = own->height;
                canvas_tween_rect(to, from, compositor.scenes.rect[i], t);
                left = compositor_scenes_put(&frames[span[i][0]], made, from, to, clip, canvas_tween_lerp(compositor_radius(own), radius, t),
                                             canvas_tween_lerp(own->line, line, t));
                for (unsigned int k = left; k < made; k++)
                        memory_zero(&frames[span[i][0] + k], sizeof(frames[0])), frames[span[i][0] + k].layer = 1, frames[span[i][0] + k].scale = scale;
        }
        return at - start;
}

// A window's new picture while the overview is drawn: where the overview draws it is drawn again (its card, and its rect in the
// current Scene), and not its own place, where it is not drawn. True when the overview draws the row or hides it.
static int compositor_scenes_took(int row)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor.scenes.screen;

        if (!compositor_scenes_shown() || row < 0 || row >= COMPOSITOR_ROWS || screen < 0 || (unsigned int)screen >= count)
                return 0;
        for (unsigned int i = 0; i < compositor.scenes.windows; i++)
                if (compositor.scenes.row[i] == row)
                {
                        const int *r = compositor.scenes.rect[i];

                        // A new size is a new aspect: the Scene is laid out again (and all of it drawn).
                        if ((int)compositor.w[row] != compositor_scenes_kept.size[i][0] || (int)compositor.h[row] != compositor_scenes_kept.size[i][1])
                                return compositor_scenes_relayout(), compositor_scenes_damage(), 1;
                        compositor_damage(r[0] - 8, r[1] - 8, r[2] + 16, r[3] + 16); // with its line and a lit ring round it
                }
        if (compositor_scenes_member(row))
                for (unsigned int card = 0; card + 1 < compositor.scenes.cards; card++)
                        if (compositor_scenes_sid((int)card) == compositor.row_space[row])
                        {
                                const int *c = compositor.scenes.card[card];

                                compositor_damage(c[0], c[1], c[2], c[3]);
                                return 1;
                        }
        return compositor_scenes_hides(row);
}

// The spring a step to its end (open or shut) over the time since its last step, in steps of a millisecond, at the kit's pace; a drag
// holds it where the hand is. A share that moves draws the overview again, and so does each pass while it is still on its way.
static void compositor_scenes_step(positive now)
{
        b64 goal = compositor.scenes.open ? COMPOSITOR_SCENES_FULL : 0, was = compositor_scenes_kept.shown;
        positive gone;
        b64 left;

        if (compositor.scenes.drag || !compositor_glides())
        {
                if (!compositor.scenes.drag)
                        compositor_scenes_kept.shown = goal, compositor_scenes_kept.speed = 0;
                compositor_scenes_kept.stepped = 0;
                if (compositor_scenes_kept.shown != was)
                        compositor_scenes_damage();
                return;
        }
        if (compositor_scenes_kept.shown == goal && !compositor_scenes_kept.speed)
        {
                compositor_scenes_kept.stepped = 0;
                return;
        }
        gone = compositor_scenes_kept.stepped && now > compositor_scenes_kept.stepped ? now - compositor_scenes_kept.stepped : 0;
        // A pass after a long stall steps half a second at the most: twelve of the spring's time constants, at rest from anywhere.
        left = ui_paced_elapsed((b64)(gone > 500 * UI_MS ? 500 * UI_MS : gone));
        while (left > 0)
        {
                b64 ns = left > UI_MS ? UI_MS : left;

                left -= ns;
                if (!ui_body_spring_step(&compositor_scenes_kept.shown, &compositor_scenes_kept.speed, goal, ns * UI_ONE / 1000000000))
                        break;
        }
        // Past an end is past it: the share is never more than all of it, nor less than none.
        if (compositor_scenes_kept.shown < 0)
                compositor_scenes_kept.shown = 0, compositor_scenes_kept.speed = 0;
        if (compositor_scenes_kept.shown > COMPOSITOR_SCENES_FULL)
                compositor_scenes_kept.shown = COMPOSITOR_SCENES_FULL, compositor_scenes_kept.speed = 0;
        compositor_scenes_kept.stepped = now;
        compositor_scenes_damage();
}

// Before a frame, every pass of the loop: the motion's step, and a change to the stack or the Spaces while the overview is open lays it
// out again.
static void compositor_scenes_tick(positive now)
{
        compositor_scenes_step(now);
        if (!compositor.scenes.open)
                return;
        if (compositor_scenes_kept.stack_gen != compositor.stack_gen || compositor_scenes_kept.spaces_gen != compositor.spaces.gen)
                compositor_scenes_relayout(), compositor_scenes_damage();
}
