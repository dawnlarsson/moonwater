/*
        The kit's motion arithmetic that more than one part of Canvas needs: the fixed point (Q30), the
        products and quotients in it, the time-to-progress map, and the cubic-bezier timing function the kit
        calls --ease. ui.c keeps the rest of the motion. The compositor includes this file for the one
        landing tween (compositor_land), so the window and the kit ease with the same curve, to the number.
        Included by ui.c and by compositor.c; the kernel build has the types from lib.c.
*/
#ifndef MOONWATER_UI_MOTION
#define MOONWATER_UI_MOTION

#define UI_Q 30                    // the kit's fixed point, Q30: the drag's throw is kept in it, and the glide's
#define UI_ONE (1ll << UI_Q)
#define UI_HALF (1ll << (UI_Q - 1))
#define UI_MS 1000000ll                    // a millisecond in nanoseconds
static inline b64 ui_qmul(b64 a, b64 b)
{
        return (b64)(((b128)a * b + UI_HALF) >> UI_Q);
}

// a / b, both Q30, rounded to the nearest. A large a shifts less and b loses as many of its last places.
static inline b64 ui_qdiv(b64 a, b64 b)
{
        p64 n = (p64)(a < 0 ? -a : a), d = (p64)(b < 0 ? -b : b);
        int shift = UI_Q;

        while (shift > 0 && n >> (62 - shift))
                shift--;
        d >>= UI_Q - shift;
        if (!d)
                return 0;
        n = ((n << shift) + d / 2) / d;
        return (a < 0) != (b < 0) ? -(b64)n : (b64)n;
}

// How far through a span of duration a time elapsed is, 0 to UI_ONE.
static inline b64 ui_progress(b64 elapsed, b64 duration)
{
        if (elapsed <= 0 || duration <= 0)
                return duration <= 0 ? UI_ONE : 0;
        if (elapsed >= duration)
                return UI_ONE;
        // Both under 2^33 ns (8 s), so the shifted time stays in 64 bits; longer spans lose their last places.
        while (duration >= 1ll << 33)
                duration >>= 1, elapsed >>= 1;
        return (elapsed << UI_Q) / duration;
}

/*
        A cubic-bezier timing function: x(t) and y(t) as polynomials, Q30. CSS's
        are solved for t as Chrome solves them (gfx::CubicBezier: Newton's steps
        from the straight guess, then halving the bracket), to under 1e-7 of x.
*/
struct ui_bezier
{
        b64 ax, bx, cx, ay, by, cy;
};

#define UI_BEZIER(x1, y1, x2, y2)                                                                                     \
        {(UI_ONE - 3 * (x1) - (3 * ((x2) - (x1)) - 3 * (x1))), (3 * ((x2) - (x1)) - 3 * (x1)), 3 * (x1),                \
         (UI_ONE - 3 * (y1) - (3 * ((y2) - (y1)) - 3 * (y1))), (3 * ((y2) - (y1)) - 3 * (y1)), 3 * (y1)}
#define UI_Q2(hundredths) ((b64)(hundredths) * UI_ONE / 100)

static const struct ui_bezier ui_ease = UI_BEZIER(UI_Q2(20), 0, 0, UI_ONE);                  // --ease, --ease-emphasized
static inline b64 ui_bezier_x(const struct ui_bezier address_to e, b64 t)
{
        return ui_qmul(ui_qmul(ui_qmul(e->ax, t) + e->bx, t) + e->cx, t);
}

static inline b64 ui_bezier_y(const struct ui_bezier address_to e, b64 t)
{
        return ui_qmul(ui_qmul(ui_qmul(e->ay, t) + e->by, t) + e->cy, t);
}

static inline b64 ui_bezier_dx(const struct ui_bezier address_to e, b64 t)
{
        return ui_qmul(ui_qmul(3 * e->ax, t) + 2 * e->bx, t) + e->cx;
}

static b64 ui_bezier_at(const struct ui_bezier address_to e, b64 x)
{
        b64 t = x, low = 0, high = UI_ONE;

        if (x <= 0)
                return 0;
        if (x >= UI_ONE)
                return UI_ONE;
        for (int step = 0; step < 8; step++)
        {
                b64 off = ui_bezier_x(e, t) - x, slope = ui_bezier_dx(e, t);

                if (off >= -1 && off <= 1)
                        return ui_bezier_y(e, t);
                if (slope < 108) // 1e-7
                        break;
                t -= ui_qdiv(off, slope);
                t = t < 0 ? 0 : t > UI_ONE ? UI_ONE : t;
        }
        t = x;
        for (int step = 0; step < 48 && low < high; step++)
        {
                b64 at = ui_bezier_x(e, t);

                if (at - x >= -1 && at - x <= 1)
                        break;
                if (x > at)
                        low = t;
                else
                        high = t;
                t = (low + high) / 2;
        }
        return ui_bezier_y(e, t);
}

#endif
