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
