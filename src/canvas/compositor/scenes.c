// The Scenes overview's model and layout (C8, comp-manage): pure arithmetic from rects in, rects out. The overview draws
// nothing here; the drawing and the input sit on these rects. Rects are {x, y, w, h}.

// The windows of one Scene, laid out large and without overlap in the area, each in its own aspect (Exposé). Returns the
// number placed (all of them). The grid is the column count that fits every window's rect the largest, ties to fewer columns.
static unsigned int compositor_scenes_grid(const int (*size)[2], unsigned int count, const int area[4], int gap, int (*out)[4])
{
        unsigned int best_cols = 1;
        long long best = -1;

        if (!count || area[2] <= 0 || area[3] <= 0)
                return 0;
        for (unsigned int cols = 1; cols <= count; cols++)
        {
                unsigned int rows = (count + cols - 1) / cols;
                long long cw = ((long long)area[2] - (long long)(cols - 1) * gap) / cols, ch = ((long long)area[3] - (long long)(rows - 1) * gap) / rows;
                long long fit = -1;

                if (cw <= 0 || ch <= 0)
                        continue;
                for (unsigned int at = 0; at < count; at++)
                {
                        long long w = size[at][0] > 0 ? size[at][0] : 1, h = size[at][1] > 0 ? size[at][1] : 1;
                        long long s = cw * h <= ch * w ? cw * 1024 / w : ch * 1024 / h; // the fit's scale in 1/1024 of the window

                        if (fit < 0 || s < fit)
                                fit = s;
                }
                if (fit > best)
                        best = fit, best_cols = cols;
        }
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int cols = best_cols, rows = (count + cols - 1) / cols, col = at % cols, row = at / cols;
                long long cw = ((long long)area[2] - (long long)(cols - 1) * gap) / cols, ch = ((long long)area[3] - (long long)(rows - 1) * gap) / rows;
                long long w = size[at][0] > 0 ? size[at][0] : 1, h = size[at][1] > 0 ? size[at][1] : 1;
                long long fw, fh;

                if (cw * h <= ch * w)
                        fw = cw, fh = h * cw / w;
                else
                        fh = ch, fw = w * ch / h;
                out[at][0] = area[0] + (int)(col * (cw + gap) + (cw - fw) / 2);
                out[at][1] = area[1] + (int)(row * (ch + gap) + (ch - fh) / 2);
                out[at][2] = (int)fw;
                out[at][3] = (int)fh;
        }
        return count;
}

// The Scene strip along the top: one card per Scene at the screen's aspect, and a "+" card last. Cards are the same height and
// shrink together to fit the width. Returns the number of cards (scenes + 1).
static unsigned int compositor_scenes_strip(unsigned int scenes, const int area[4], int screen_w, int screen_h, int gap, int (*out)[4])
{
        unsigned int cards = scenes + 1;
        long long ch = area[3], cw = ch * screen_w / (screen_h > 0 ? screen_h : 1);

        if (!cards || area[2] <= 0 || area[3] <= 0 || screen_w <= 0 || screen_h <= 0)
                return 0;
        if ((long long)cards * cw + (long long)(cards - 1) * gap > area[2])
        {
                cw = ((long long)area[2] - (long long)(cards - 1) * gap) / cards;
                ch = cw * screen_h / screen_w;
        }
        for (unsigned int at = 0; at < cards; at++)
        {
                out[at][0] = area[0] + (int)(at * (cw + gap));
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
        int screen = compositor.scenes.screen, gap = compositor_scenes_gap(), strip_h, strip[4], win[4];
        int sizes[COMPOSITOR_SCENES_WINDOWS][2];
        unsigned int n = 0;
        struct compositor_space *active;
        const int *s;

        if (screen < 0 || (unsigned int)screen >= count)
                return;
        s = screens[screen];
        strip_h = s[3] * 22 / 100;
        strip[0] = s[0] + gap, strip[1] = s[1] + gap, strip[2] = s[2] - 2 * gap, strip[3] = strip_h;
        win[0] = s[0] + gap, win[1] = s[1] + strip_h + 2 * gap, win[2] = s[2] - 2 * gap, win[3] = s[3] - strip_h - 3 * gap;
        compositor.scenes.cards = compositor_scenes_strip((unsigned int)compositor_spaces_count_on(screen), strip, s[2], s[3], gap,
                                                          compositor.scenes.card);
        active = compositor_space_at(screen, compositor_spaces_active_index(screen));
        for (unsigned int at = 0; at < compositor.stacked && n < COMPOSITOR_SCENES_WINDOWS && active && compositor.row_space; at++)
        {
                int row = (int)compositor.stack[at];

                if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.showing[row] ||
                    (compositor.state[row] & (WINDOW_MINIMIZED | WINDOW_CLOSING)) || compositor_popup_parent[row] >= 0 ||
                    compositor_row_space_exempt(row) || compositor.row_space[row] != active->id)
                        continue;
                compositor.scenes.row[n] = row;
                sizes[n][0] = (int)compositor.w[row], sizes[n][1] = (int)compositor.h[row];
                n++;
        }
        compositor.scenes.windows = compositor_scenes_grid(sizes, n, win, gap, compositor.scenes.rect);
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
