#ifndef MOONWATER_UI_TOUCH
#define MOONWATER_UI_TOUCH

/*
        The kit's touch recogniser: what the fingers on a touch screen mean, as gestures, and nothing about the windows or the
        device they are on. The compositor reads the device (slots, ids, the points in the desktop's pixels), keeps the windows,
        and applies the events this file answers with (ui_touch_out): a finger that went down is the pointer's, its press is the
        left button where it went down, and it moves nothing until it is UI_SLOP past where it went down (a tap is a click where it
        landed); a second finger before the first has moved makes the two a wheel from their middle (the left press is taken back
        with a release that says it was no click), and the two lifted soon, neither moved, are a click of the right button; a long
        press (UI_TOUCH_HOLD_NS, still, with the left button taken) is the right button, held, and the finger may move on until it
        lifts, which ends the press. A window's close button under the finger is the caller's: it marks the press pending
        (ui_touch_pend), and a lift unmoved then is UI_TOUCH_CLOSE, a move past the slop UI_TOUCH_RESUME.

        Use: each report of the device, ui_touch_begin when no finger was down (it answers the first finger's START and PRESS),
        apply those events, then ui_touch_report with the frame as it now is (the press may have made a drag, or marked a close),
        apply its events. A tick (ui_touch_tick, ui_touch_due) asks for the long press when its time is come. A device that
        drops events or goes away is ui_touch_drop or ui_touch_reset.
*/
#include "theme.h"

#define UI_TOUCH_SLOTS 10                  // the fingers a touch screen says at once that are followed
#define UI_TOUCH_NONE 0
#define UI_TOUCH_ONE 1                     // the first finger is down: the pointer's, and its press is made
#define UI_TOUCH_TWO 2                     // two fingers: the wheel, or the right button when they are lifted soon
#define UI_TOUCH_HELD 3                    // a long press: nothing more until every finger is up
#define UI_TOUCH_HOLD_NS 400000000ull      // still this long from its landing, the first finger is a long press
#define UI_TOUCH_TAP_NS 400000000ull       // two fingers lifted within this of the second landing, neither moved, are a right click
#define UI_TOUCH_NOTCH 48                  // the fingers' pixels a notch of the wheel is (120ths), at the scale of 100 percent
#define UI_TOUCH_EVENTS 12                 // the most events one call answers

enum ui_touch_kind
{
        UI_TOUCH_START,    // a finger went down: the pointer goes to x, y (hidden), and the press follows
        UI_TOUCH_MOVE,     // the pointer goes to x, y
        UI_TOUCH_PRESS,    // the left button goes down at the pointer
        UI_TOUCH_RELEASE,  // the left button goes up where it is
        UI_TOUCH_CANCEL,   // the left button goes up and says it was no click (the press was taken back)
        UI_TOUCH_CLOSE,    // a pending press lifted unmoved: the window whose close button it was closes
        UI_TOUCH_RESUME,   // a pending press moved past the slop: the press is made again, without the close button
        UI_TOUCH_RIGHT,    // a click of the right button at the pointer
        UI_TOUCH_WHEEL,    // a turn of the wheel at the pointer, v120 in 120ths of a notch, horizontal or vertical
        UI_TOUCH_HOLD,     // a long press: the left press is taken back, the right button goes down held at the pointer
        UI_TOUCH_HOLD_END, // the lift that ends a long press
        UI_TOUCH_IDLE     // no finger is down
};

struct ui_touch_event
{
        enum ui_touch_kind kind;
        int x, y;          // START and MOVE: the place
        int v120;          // WHEEL: the turn
        int horizontal;    // WHEEL: sideways
};

struct ui_touch_out
{
        unsigned int count;
        struct ui_touch_event event[UI_TOUCH_EVENTS];
};

struct ui_touch
{
        int mode, first, count, tcount;      // the mode; the first finger's slot; the fingers down, and in the two's last report
        int moved, two_moved, pending;       // the first finger is past the slop; the two's wheel has turned; a close is pending
        int start_x, start_y, mid_x, mid_y;  // the first finger's landing; the two's middle, where the wheel last counted from
        int left_x, left_y;                  // what the wheel has not yet turned, in 120ths times the pixels of a notch
        b64 start, two_at;                   // when the first finger landed; when the two last were two
};

// One device report: each slot's id (negative for none down now) and the id it had at the last report, and the point of each slot
// that is down, in the desktop's pixels; the pointer where it is; whether a drag or a resize holds the hand; the scale in percent;
// and the time.
struct ui_touch_frame
{
        int id[UI_TOUCH_SLOTS], was[UI_TOUCH_SLOTS], x[UI_TOUCH_SLOTS], y[UI_TOUCH_SLOTS];
        int pointer_x, pointer_y, busy, scale;
        b64 now;
};

// The slop: a finger is past it when its distance from where it landed is at least this (the kit's one touch slop, UI_SLOP at the
// scale). The window's motion and the gestures take the same test.
static inline int ui_touch_past(int x0, int y0, int x, int y, int slop)
{
        long long dx = (long long)x - x0, dy = (long long)y - y0;

        return dx * dx + dy * dy >= (long long)slop * slop;
}

static inline int ui_touch_slop(int scale)
{
        return UI_SLOP * scale / 100;
}

static inline void ui_touch_put(struct ui_touch_out *out, enum ui_touch_kind kind, int x, int y)
{
        if (out->count < UI_TOUCH_EVENTS)
        {
                struct ui_touch_event *e = &out->event[out->count++];

                e->kind = kind, e->x = x, e->y = y, e->v120 = 0, e->horizontal = 0;
        }
}

static inline void ui_touch_put_wheel(struct ui_touch_out *out, int v120, int horizontal)
{
        if (out->count < UI_TOUCH_EVENTS)
        {
                struct ui_touch_event *e = &out->event[out->count++];

                e->kind = UI_TOUCH_WHEEL, e->x = e->y = 0, e->v120 = v120, e->horizontal = horizontal;
        }
}

// The fingers down in the frame: how many, and the lowest slot down other than skip (-1 for none).
static inline int ui_touch_fingers(const struct ui_touch_frame *f, int skip, int *other)
{
        int count = 0;

        *other = -1;
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (f->id[slot] >= 0)
                {
                        count++;
                        if (slot != skip && *other < 0)
                                *other = slot;
                }
        return count;
}

/*
        The first finger down, when no finger was: the pointer goes to its landing (START), and the left button is pressed there
        (PRESS). Nothing else answers in the same report: the caller applies these, then reports the frame as it is then.
*/
static inline void ui_touch_begin(struct ui_touch *t, const struct ui_touch_frame *f, struct ui_touch_out *out)
{
        int other;

        out->count = 0;
        if (t->mode != UI_TOUCH_NONE || !ui_touch_fingers(f, -1, &other))
                return;
        t->mode = UI_TOUCH_ONE, t->first = other;
        t->moved = t->two_moved = t->pending = 0;
        t->start = f->now;
        t->start_x = f->x[other], t->start_y = f->y[other];
        ui_touch_put(out, UI_TOUCH_START, t->start_x, t->start_y);
        ui_touch_put(out, UI_TOUCH_PRESS, t->start_x, t->start_y);
}

/*
        A report of the fingers, after ui_touch_begin's: an idle answer when none is down, then the mode's own step. ONE: the
        first finger's lift is a release or a close; a second finger (not moved, and not while a drag or a resize holds the hand, nor
        pending) takes the press back and makes the two; else the first finger moves past the slop and is the pointer's motion.
        TWO: lifted soon and not moved, a right click; two or more, the wheel from their middle; one, the two are one again.
        HELD: lifted, the hold ends; moved on, the pointer follows it.
*/
static inline void ui_touch_report(struct ui_touch *t, const struct ui_touch_frame *f, struct ui_touch_out *out)
{
        int other, count, x, y, slop = ui_touch_slop(f->scale);

        out->count = 0;
        count = ui_touch_fingers(f, -1, &other);
        if (!count)
                ui_touch_put(out, UI_TOUCH_IDLE, 0, 0);

        if (t->mode == UI_TOUCH_ONE)
        {
                int first = t->first;

                if (f->id[first] < 0 || (f->was[first] >= 0 && f->id[first] != f->was[first]))
                {
                        // Lifted: a release where it is, or a close on the button it went down on, unmoved.
                        ui_touch_put(out, t->pending && !t->moved ? UI_TOUCH_CLOSE : UI_TOUCH_RELEASE, 0, 0);
                        t->pending = 0;
                        t->mode = count ? UI_TOUCH_HELD : UI_TOUCH_NONE;
                }
                else if (count >= 2 && !t->moved && !f->busy && !t->pending)
                {
                        // A second finger: the press is taken back, and the two are the wheel or the right button.
                        ui_touch_put(out, UI_TOUCH_CANCEL, 0, 0);
                        ui_touch_fingers(f, first, &other);
                        t->mode = UI_TOUCH_TWO;
                        t->two_at = f->now;
                        t->tcount = count;
                        t->mid_x = (f->x[other] + f->x[first]) / 2;
                        t->mid_y = (f->y[other] + f->y[first]) / 2;
                        t->left_x = t->left_y = 0;
                }
                else
                {
                        x = f->x[first], y = f->y[first];
                        if (!t->moved && ui_touch_past(t->start_x, t->start_y, x, y, slop))
                        {
                                int was_pending = t->pending;

                                t->moved = 1, t->pending = 0;
                                if (was_pending)
                                        ui_touch_put(out, UI_TOUCH_RESUME, x, y);
                        }
                        if (t->moved && (x != f->pointer_x || y != f->pointer_y))
                                ui_touch_put(out, UI_TOUCH_MOVE, x, y);
                }
        }
        else if (t->mode == UI_TOUCH_TWO)
        {
                if (!count)
                {
                        if (!t->two_moved && f->now - t->two_at <= UI_TOUCH_TAP_NS)
                                ui_touch_put(out, UI_TOUCH_RIGHT, 0, 0);
                        t->mode = UI_TOUCH_NONE;
                }
                else if (count >= 2)
                {
                        int one = f->id[t->first] >= 0 ? t->first : other, per = UI_TOUCH_NOTCH * f->scale / 100;
                        int mid_x, mid_y;

                        ui_touch_fingers(f, one, &other);
                        mid_x = (f->x[other] + f->x[one]) / 2;
                        mid_y = (f->y[other] + f->y[one]) / 2;
                        // A finger more or less: the middle is somewhere else, and the wheel counts from it again.
                        if (count != t->tcount)
                        {
                                t->tcount = count;
                                t->mid_x = mid_x, t->mid_y = mid_y;
                        }
                        else if (t->two_moved || ui_touch_past(t->mid_x, t->mid_y, mid_x, mid_y, slop))
                        {
                                // What is under them follows them: fingers down are the wheel up, to the right it left.
                                long down = (long)t->left_y + (long)(mid_y - t->mid_y) * 120;
                                long right = (long)t->left_x - (long)(mid_x - t->mid_x) * 120;
                                int turn_y = (int)(down / per), turn_x = (int)(right / per);

                                t->two_moved = 1;
                                t->left_y = (int)(down - (long)turn_y * per);
                                t->left_x = (int)(right - (long)turn_x * per);
                                t->mid_x = mid_x, t->mid_y = mid_y;
                                if (turn_y)
                                        ui_touch_put_wheel(out, turn_y, 0);
                                if (turn_x)
                                        ui_touch_put_wheel(out, turn_x, 1);
                        }
                }
                else
                        t->tcount = count;
        }
        else if (t->mode == UI_TOUCH_HELD && !count)
        {
                ui_touch_put(out, UI_TOUCH_HOLD_END, 0, 0);
                t->mode = UI_TOUCH_NONE;
        }
        else if (t->mode == UI_TOUCH_HELD && count == 1 && f->id[t->first] >= 0)
        {
                // Held, then moved on: the pointer follows the finger, and what it drags follows it. Nothing is pressed.
                x = f->x[t->first], y = f->y[t->first];
                if (!t->moved && ui_touch_past(t->start_x, t->start_y, x, y, slop))
                        t->moved = 1;
                if (t->moved && (x != f->pointer_x || y != f->pointer_y))
                        ui_touch_put(out, UI_TOUCH_MOVE, x, y);
        }
}

// When the first finger is a long press: its time when it is one (still, the left button pressed and not a close), or 0.
static inline b64 ui_touch_due(const struct ui_touch *t, int pressed)
{
        return t->mode == UI_TOUCH_ONE && !t->moved && !t->pending && pressed ? t->start + (b64)UI_TOUCH_HOLD_NS : 0;
}

// The tick: at or after the long press's time, the first finger is a hold (UI_TOUCH_HOLD) and the press is HELD.
static inline void ui_touch_tick(struct ui_touch *t, b64 now, int pressed, struct ui_touch_out *out)
{
        b64 due = ui_touch_due(t, pressed);

        out->count = 0;
        if (!due || now < due)
                return;
        ui_touch_put(out, UI_TOUCH_HOLD, 0, 0);
        t->mode = UI_TOUCH_HELD;
}

// A close button under the first finger: its press is pending until the finger lifts unmoved (UI_TOUCH_CLOSE) or moves (RESUME).
static inline void ui_touch_pend(struct ui_touch *t, int on)
{
        t->pending = on != 0;
}

// The events lost: what the first finger was doing ends as if it were lifted (a cancel), and no click is made.
static inline void ui_touch_drop(struct ui_touch *t, struct ui_touch_out *out)
{
        out->count = 0;
        if (t->mode == UI_TOUCH_ONE)
                ui_touch_put(out, UI_TOUCH_CANCEL, 0, 0);
        t->mode = UI_TOUCH_NONE;
        t->pending = 0;
}

// The device is gone: no event, and the gesture is over.
static inline void ui_touch_reset(struct ui_touch *t)
{
        t->mode = UI_TOUCH_NONE;
        t->pending = 0;
}

#endif
