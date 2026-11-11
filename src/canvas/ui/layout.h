/*
        The kit's layout: a tree of boxes and the rectangle of each, by the rules of CSS's flexbox, in whole pixels. A tree is
        declared into an array the caller owns (struct ui_tree: no allocation, no pointers), opened and closed in the order the
        elements nest, and two passes give every element its rectangle. Integer arithmetic only, so every platform gets the
        same rectangles; the units are the caller's (the view, ui.c, runs it in device pixels).

            ui_tree_begin(&tree);
            n = ui_tree_open(&tree);          // the root: element 0; n is the element, to be written
                n->dir = 1, n->gap = 8;
                n = ui_tree_open(&tree);      // a child of the root
                ui_tree_close(&tree);
            ui_tree_close(&tree);
            ui_tree_run(&tree, width, height);    // tree.node[i].x, y, w, h, in the order the elements were opened

        The element is the whole of what it declares, in one value (struct ui_node):

            dir          0 lays its children out left to right (a row), 1 top to bottom (a column): the main axis
            gap          between two children along the main axis
            pad, margin  left, right, top, bottom: inside the element's edge, and outside it (a margin is the child's, the
                         parent's pad is the room its children are in)
            justify      the children along the main axis, in the room the sizes and gaps leave: UI_START (0 too), UI_CENTER,
                         UI_END, UI_BETWEEN (the room between the children, the first gaps a pixel wider by what does not
                         divide) and UI_AROUND (an equal room round each)
            align        the children across it: UI_START, UI_CENTER, UI_END, or UI_STRETCH (0 too): a child of fit size fills
            scroll       1: the children are not shrunk, run past the end, and are moved back by offset (clamped, answered in
                         offset), with the length of what they take in extent: overflow: auto along the main axis
            size[0..1]   the width and the height, below

        Sizing, per axis (size[0] is the width, size[1] the height):
            UI_FIT       the content: along the main axis its children's sizes, margins and the gaps between them; across it,
                         the largest child with its margins; plus its pad. A leaf has none of its own: its content[] is what
                         the caller measured (a line of text). A child that grows or is a percent counts as its min.
            UI_FIXED     value pixels.
            UI_GROW      flex-grow: a share of the parent's leftover room on the main axis in proportion to value (the weight;
                         0 takes no share) on top of its min; across it, all the room.
            UI_PERCENT   value percent of the parent's inner size on that axis (its size less its pad).
            min, max     the limits, applied last to every size; a max below its min reads as the min.
            shrink       flex-shrink: when the children are more than the parent has room for, a child with a weight gives up a
                         share of the excess in proportion to the weight and its size, down to its min. A GROW child never.

        Pass 1 (bottom-up, the reverse of open order) gives every element its preferred size. Pass 2 (top-down, in open order)
        gives the root its size from the viewport (GROW: all of it, PERCENT: that share of it) and then, each parent in turn, its
        children their main sizes (FIXED and FIT as pass 1 has them, PERCENT of the inner size, the GROW shares of what is left
        after the sizes, margins and gaps) and their places. A share is in whole pixels, floor(room * weight / total weight), and the
        pixels the floors leave go one each to the first children with a weight, in open order. A GROW child that would pass its max
        is frozen at the max and the room shared again among the rest; a shrinking child that would pass its min is frozen at the min
        and the excess shared again; each repeats till none passes. Children are never clipped: one larger than its parent runs
        past it.

        Every value is read within its range: sizes, gaps, pads, margins and the viewport lie in [0, UI_TREE_MOST], weights in
        [0, UI_TREE_WEIGHT], a direction other than 1 is 0, a kind outside 0 to 3 is UI_FIT, a justify or an align the table does
        not have is the first; a position is kept to +-2^30. So every product stays in 64 bits.

        Limits, and what happens at them: UI_TREE_NODES elements, UI_TREE_DEPTH open at once. An open that would pass either, or is
        a second root, or sits inside a refused open, is refused: it adds no element, is counted in dropped, and what is written
        to the node it answers goes to a node nothing reads. Its close is paired as usual. Nothing is written outside the arrays.

        This is the reference. The routine the kit runs is this C; there is no assembly under it yet, and the one thing that
        would be is ui_tree_run's pass 2, the only part with work per child. The struct is laid out for it (the asserts below pin
        the sizes it indexes by shift), and the shape of its inner loops is the usb.tools layout's, which is assembly on arm64 and
        x86-64 and was checked against this same arithmetic rectangle by rectangle. Nothing is measured here to say it wins.
*/
#ifndef MOONWATER_UI_LAYOUT
#define MOONWATER_UI_LAYOUT

#define UI_TREE_NODES 128
#define UI_TREE_DEPTH 16
#define UI_TREE_MOST (1 << 22)   // the largest size, gap, pad or margin; a larger value reads as this
#define UI_TREE_WEIGHT 1024      // the largest weight
#define UI_TREE_FAR (1 << 30)    // the farthest a position is kept from the origin

// The sizing kinds.
#define UI_FIT 0
#define UI_FIXED 1
#define UI_GROW 2
#define UI_PERCENT 3

// Justify and align: 0 is the first of each (start for a justify, stretch for an align).
#define UI_START 1
#define UI_CENTER 2
#define UI_END 3
#define UI_BETWEEN 4
#define UI_AROUND 5
#define UI_STRETCH 6

struct ui_size
{
        int kind;   // UI_FIT, UI_FIXED, UI_GROW or UI_PERCENT
        int value;  // FIXED: pixels; GROW: the weight; PERCENT: percent; FIT: unused
        int min;    // the smallest, pixels
        int max;    // the largest, pixels (UI_TREE_MOST: none)
        int shrink; // the weight of what it gives up when its parent is too small (0: none)
};

struct ui_node
{
        int dir, justify, align, scroll, gap;
        int pad[4], margin[4];             // left, right, top, bottom
        struct ui_size size[2];            // [0] the width, [1] the height
        int content[2];                    // a leaf's own size per axis (what FIT takes when it has no children)
        int offset, extent;                // a scroll element's offset (in, then clamped) and the length of its children (out)
        int parent, first, last, next, children;
        int pref[2], base, frozen;         // the scratch of the passes
        b64 sum[2];
        int widest[2];
        int x, y, w, h;                    // the answer: the rectangle, from the root's top left
        int spare[2];
};

struct ui_tree
{
        struct ui_node node[UI_TREE_NODES];
        struct ui_node discard;            // what a refused open answers, to be written and not read
        int count, depth, over, dropped;
        int open[UI_TREE_DEPTH];           // the open stack: each open's element, or -1 for a refused one
};

_Static_assert(sizeof(struct ui_size) == 20, "ui_size: five ints");
_Static_assert(sizeof(struct ui_node) == 192, "ui_node: three cache lines");
_Static_assert(offset_of(struct ui_node, x) == 168 && offset_of(struct ui_node, sum) == 144, "ui_node offsets");

static inline int ui_tree_pixels(int v)
{
        return v < 0 ? 0 : v > UI_TREE_MOST ? UI_TREE_MOST : v;
}

static inline int ui_tree_weight(int v)
{
        return v < 0 ? 0 : v > UI_TREE_WEIGHT ? UI_TREE_WEIGHT : v;
}

static inline int ui_tree_place(b64 v)
{
        return v < -UI_TREE_FAR ? -UI_TREE_FAR : v > UI_TREE_FAR ? UI_TREE_FAR : (int)v;
}

// v within [low, high], low <= high.
static inline b64 ui_tree_clamp(b64 v, b64 low, b64 high)
{
        return v < low ? low : v > high ? high : v;
}

// The position and the length of an element on an axis (0 across, 1 down).
static inline int address_to ui_tree_at(struct ui_node address_to n, int axis)
{
        return axis ? &n->y : &n->x;
}

static inline int address_to ui_tree_len(struct ui_node address_to n, int axis)
{
        return axis ? &n->h : &n->w;
}

// The gaps of an element's children: one fewer than they are.
static inline b64 ui_tree_gaps(const struct ui_node address_to n)
{
        return n->children > 1 ? (b64)(n->children - 1) * n->gap : 0;
}

// An empty tree; the array is left as it is.
static fn ui_tree_begin(struct ui_tree address_to t)
{
        t->count = t->depth = t->over = t->dropped = 0;
}

// An element opened as the last child of the innermost open one (or as the root), zeroed but for the limits: fit sizes, no max.
// It is the node to write its declaration in. A refused open answers t->discard, zeroed, and the element is not there.
static struct ui_node address_to ui_tree_open(struct ui_tree address_to t)
{
        struct ui_node address_to n;
        int parent = -1, refused;

        if (t->depth == UI_TREE_DEPTH)
        {
                t->over++, t->dropped++;
                memory_zero(&t->discard, sizeof(t->discard));
                return &t->discard;
        }
        if (t->depth)
                parent = t->open[t->depth - 1];
        refused = (t->depth ? parent < 0 : t->count != 0) || t->count >= UI_TREE_NODES;
        if (refused)
        {
                t->dropped++;
                t->open[t->depth++] = -1;
                memory_zero(&t->discard, sizeof(t->discard));
                return &t->discard;
        }
        n = &t->node[t->count];
        memory_zero(n, sizeof(*n));
        n->size[0].max = n->size[1].max = UI_TREE_MOST;
        n->parent = parent;
        n->first = n->last = n->next = -1;
        if (parent >= 0)
        {
                struct ui_node address_to p = &t->node[parent];

                if (p->first < 0)
                        p->first = t->count;
                else
                        t->node[p->last].next = t->count;
                p->last = t->count;
                p->children++;
        }
        t->open[t->depth++] = t->count++;
        return n;
}

// The innermost open element is closed (a refused one past the depth first).
static fn ui_tree_close(struct ui_tree address_to t)
{
        if (t->over)
                t->over--;
        else if (t->depth)
                t->depth--;
}

// The grow children of e, on axis m, share left (> 0) pixels over their minimums.
static fn ui_tree_grow(struct ui_tree address_to t, const struct ui_node address_to e, int m, b64 left)
{
        for (;;)
        {
                b64 total = 0, shares = 0, freed = 0, rest;
                b32 violated = false;

                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                        if (t->node[ch].size[m].kind == UI_GROW && !t->node[ch].frozen)
                                total += t->node[ch].size[m].value;
                if (!total)
                {
                        // No weight left: those that are not frozen are at their minimums.
                        for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                                if (t->node[ch].size[m].kind == UI_GROW && !t->node[ch].frozen)
                                        *ui_tree_len(&t->node[ch], m) = t->node[ch].size[m].min;
                        return;
                }
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                        if (t->node[ch].size[m].kind == UI_GROW && !t->node[ch].frozen)
                                shares += left * t->node[ch].size[m].value / total;
                rest = left - shares;
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                {
                        struct ui_node address_to k = &t->node[ch];
                        b64 weight = k->size[m].value, amount = left * weight / total;

                        if (k->size[m].kind != UI_GROW || k->frozen)
                                continue;
                        if (weight > 0 && rest > 0)
                                amount++, rest--;
                        if (k->size[m].min + amount > k->size[m].max)
                        {
                                *ui_tree_len(k, m) = k->size[m].max;
                                k->frozen = 1;
                                freed += k->size[m].max - k->size[m].min;
                                violated = true;
                        }
                        else
                                *ui_tree_len(k, m) = (int)(k->size[m].min + amount);
                }
                if (!violated)
                        return;
                left -= freed;
        }
}

// The children of e that give way, on axis m, give up excess (> 0) pixels between them, none below its min.
static fn ui_tree_shrink(struct ui_tree address_to t, const struct ui_node address_to e, int m, b64 excess)
{
        for (;;)
        {
                b64 total = 0, shares = 0, freed = 0, rest;
                b32 violated = false;

                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                        if (t->node[ch].size[m].kind != UI_GROW && !t->node[ch].frozen)
                                total += (b64)t->node[ch].size[m].shrink * t->node[ch].base;
                if (!total)
                        return;
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                        if (t->node[ch].size[m].kind != UI_GROW && !t->node[ch].frozen)
                                shares += excess * ((b64)t->node[ch].size[m].shrink * t->node[ch].base) / total;
                rest = excess - shares;
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                {
                        struct ui_node address_to k = &t->node[ch];
                        b64 weight = (b64)k->size[m].shrink * k->base, cut = excess * weight / total;

                        if (k->size[m].kind == UI_GROW || k->frozen)
                                continue;
                        if (weight > 0 && rest > 0)
                                cut++, rest--;
                        if (k->base - cut < k->size[m].min)
                        {
                                *ui_tree_len(k, m) = k->size[m].min;
                                k->frozen = 1;
                                freed += k->base - k->size[m].min;
                                violated = true;
                        }
                        else
                                *ui_tree_len(k, m) = (int)(k->base - cut);
                }
                if (!violated)
                        return;
                excess -= freed;
        }
}

// The rectangles of the tree for a viewport of width by height: every element's x, y, w and h.
static fn ui_tree_run(struct ui_tree address_to t, int width, int height)
{
        b64 view[2] = {ui_tree_pixels(width), ui_tree_pixels(height)};
        int count = t->count;

        if (!count)
                return;
        // Every value read within its range, and the scratch cleared.
        for (int i = 0; i < count; i++)
        {
                struct ui_node address_to e = &t->node[i];

                e->dir = e->dir == 1;
                e->scroll = e->scroll != 0;
                e->gap = ui_tree_pixels(e->gap);
                e->offset = ui_tree_pixels(e->offset);
                e->extent = 0;
                for (int k = 0; k < 4; k++)
                        e->pad[k] = ui_tree_pixels(e->pad[k]), e->margin[k] = ui_tree_pixels(e->margin[k]);
                for (int a = 0; a < 2; a++)
                {
                        struct ui_size address_to s = &e->size[a];

                        s->kind = (unsigned int)s->kind > UI_PERCENT ? UI_FIT : s->kind;
                        s->value = s->kind == UI_GROW ? ui_tree_weight(s->value) : ui_tree_pixels(s->value);
                        s->min = ui_tree_pixels(s->min), s->max = ui_tree_pixels(s->max);
                        s->max = s->max < s->min ? s->min : s->max;
                        s->shrink = ui_tree_weight(s->shrink);
                        e->content[a] = ui_tree_pixels(e->content[a]);
                        e->sum[a] = 0, e->widest[a] = 0;
                }
        }
        // Pass 1, bottom-up: each element's preferred size, and its part in its parent's.
        for (int i = count - 1; i >= 0; i--)
        {
                struct ui_node address_to e = &t->node[i];

                for (int a = 0; a < 2; a++)
                {
                        const struct ui_size address_to s = &e->size[a];
                        b64 v = 0;

                        if (s->kind == UI_FIXED)
                                v = s->value;
                        else if (s->kind == UI_FIT)
                                v = (!e->children ? e->content[a] : a == e->dir ? e->sum[a] + ui_tree_gaps(e) : e->widest[a]) + e->pad[2 * a] +
                                    e->pad[2 * a + 1];
                        e->pref[a] = (int)ui_tree_clamp(v, s->min, s->max);
                }
                if (e->parent >= 0)
                {
                        struct ui_node address_to p = &t->node[e->parent];

                        for (int a = 0; a < 2; a++)
                        {
                                int outer = e->pref[a] + e->margin[2 * a] + e->margin[2 * a + 1];

                                if (a == p->dir)
                                        p->sum[a] += outer;
                                else if (outer > p->widest[a])
                                        p->widest[a] = outer;
                        }
                }
        }
        // The root: from the viewport, at the origin.
        for (int a = 0; a < 2; a++)
        {
                const struct ui_size address_to s = &t->node[0].size[a];
                b64 v = s->kind == UI_GROW ? view[a] : s->kind == UI_PERCENT ? view[a] * s->value / 100 : t->node[0].pref[a];

                *ui_tree_at(&t->node[0], a) = 0;
                *ui_tree_len(&t->node[0], a) = (int)ui_tree_clamp(v, s->min, s->max);
        }
        // Pass 2, top-down: each parent places its children, in open order.
        for (int p = 0; p < count; p++)
        {
                struct ui_node address_to e = &t->node[p];
                int m = e->dir, c = 1 - m;
                b64 inner[2], origin[2], gaps = ui_tree_gaps(e), used = 0, total, slack, lead = 0, extra = 0, rest = 0, cursor;
                int stretch = e->align < UI_START || e->align > UI_END;

                if (e->first < 0)
                        continue;
                for (int a = 0; a < 2; a++)
                {
                        origin[a] = *ui_tree_at(e, a) + e->pad[2 * a];
                        inner[a] = *ui_tree_len(e, a) - e->pad[2 * a] - e->pad[2 * a + 1];
                        inner[a] = inner[a] < 0 ? 0 : inner[a];
                }
                // The main axis: every child at the size it asks for (a grow child at its min), and what is left.
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                {
                        struct ui_node address_to k = &t->node[ch];
                        const struct ui_size address_to s = &k->size[m];

                        k->frozen = 0;
                        k->base = s->kind == UI_GROW ? s->min
                                  : s->kind == UI_PERCENT ? (int)ui_tree_clamp(inner[m] * s->value / 100, s->min, s->max)
                                                          : k->pref[m];
                        *ui_tree_len(k, m) = k->base;
                        used += k->base + k->margin[2 * m] + k->margin[2 * m + 1];
                }
                if (inner[m] - used - gaps > 0)
                        ui_tree_grow(t, e, m, inner[m] - used - gaps);
                else if (inner[m] - used - gaps < 0 && !e->scroll)
                        ui_tree_shrink(t, e, m, used + gaps - inner[m]);
                // Where they go: the room the sizes, margins and gaps leave, by the justify (a scroll element has none).
                total = gaps;
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                        total += *ui_tree_len(&t->node[ch], m) + t->node[ch].margin[2 * m] + t->node[ch].margin[2 * m + 1];
                slack = inner[m] - total < 0 ? 0 : inner[m] - total;
                if (e->scroll)
                {
                        e->extent = (int)ui_tree_clamp(total, 0, UI_TREE_FAR);
                        e->offset = (int)ui_tree_clamp(e->offset, 0, total > inner[m] ? total - inner[m] : 0);
                }
                else if (e->justify == UI_CENTER)
                        lead = slack / 2;
                else if (e->justify == UI_END)
                        lead = slack;
                else if (e->justify == UI_BETWEEN && e->children > 1)
                        extra = slack / (e->children - 1), rest = slack - extra * (e->children - 1);
                else if (e->justify == UI_AROUND)
                        extra = slack / e->children, lead = extra / 2;
                cursor = origin[m] + lead - (e->scroll ? e->offset : 0);
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                {
                        struct ui_node address_to k = &t->node[ch];

                        *ui_tree_at(k, m) = ui_tree_place(cursor + k->margin[2 * m]);
                        cursor += *ui_tree_len(k, m) + k->margin[2 * m] + k->margin[2 * m + 1] + e->gap + extra + (rest > 0 ? 1 : 0);
                        rest -= rest > 0;
                }
                // The cross axis: grow fills the room, percent is of it, a fit child fills it too when the align is stretch, and
                // the child is placed in what it leaves by the align.
                for (int ch = e->first; ch >= 0; ch = t->node[ch].next)
                {
                        struct ui_node address_to k = &t->node[ch];
                        const struct ui_size address_to s = &k->size[c];
                        b64 room = inner[c] - k->margin[2 * c] - k->margin[2 * c + 1], v, left;

                        room = room < 0 ? 0 : room;
                        v = s->kind == UI_GROW || (s->kind == UI_FIT && stretch) ? room : s->kind == UI_PERCENT ? room * s->value / 100 : k->pref[c];
                        v = ui_tree_clamp(v, s->min, s->max);
                        *ui_tree_len(k, c) = (int)v;
                        left = room - v < 0 ? 0 : room - v;
                        *ui_tree_at(k, c) = ui_tree_place(origin[c] + k->margin[2 * c] + (e->align == UI_CENTER ? left / 2 : e->align == UI_END ? left : 0));
                }
        }
}

#endif /* MOONWATER_UI_LAYOUT */
