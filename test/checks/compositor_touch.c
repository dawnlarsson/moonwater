/*
        test/checks/compositor_touch.c: touch through the compositor, the snap's frames, the touch path, auto-scroll,
        the caret, scrolling, flicks, the wheel and the throw, the trace, the screen's edges.

        Rows of the compositor's input.c, manage.c and damage.c. One part of the program that the terminal's, the kit
        frames', the GPU kit's, the draw list's and the GPU model kit's sections share (checks.c opens and closes it).
        The terminal's program, the kit's frames, the draw list and the GPU model's kit run with the four faces:
        fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef COMPOSITOR_LINE
/*
        A touch screen through the compositor's reader, from a pipe that stands for one in slot 0, its events as a
        multitouch driver and the kernel say them: each finger's slot, id and place, then the kernel's BTN_TOUCH when
        the first finger lands or the last lifts, then the oldest finger's ABS_X and ABS_Y, then the report's end. Its
        units are four to a pixel of the 256 by 256 screen. With TOUCH_AS_BEFORE the device is read as it was before
        touch screens were (a tablet whose BTN_TOUCH is the left button), which is how each row is seen red.
*/
static positive touch_clock; // microseconds, the events' stamps
static int touch_down;      // what BTN_TOUCH said last

static fn touch_feed(unsigned short type, unsigned short code, int value)
{
        struct compositor_input_event event = {(long)(touch_clock / 1000000), (long)(touch_clock % 1000000), type, code, value};

        system_write_once(wheel_pipe[1], &event, sizeof(event));
}

// One report: count fingers, each {slot, id, x, y} with id -1 lifted; the first whose id is not -1 is the oldest.
static fn touch_report(int count, const int (address_to fingers)[4])
{
        int down = 0, oldest = -1;

        for (int at = 0; at < count; at++)
        {
                touch_feed(COMPOSITOR_EV_ABS, COMPOSITOR_ABS_MT_SLOT, fingers[at][0]);
                touch_feed(COMPOSITOR_EV_ABS, COMPOSITOR_ABS_MT_TRACKING_ID, fingers[at][1]);
                if (fingers[at][1] < 0)
                        continue;
                touch_feed(COMPOSITOR_EV_ABS, COMPOSITOR_ABS_MT_POSITION_X, fingers[at][2] * 4);
                touch_feed(COMPOSITOR_EV_ABS, COMPOSITOR_ABS_MT_POSITION_Y, fingers[at][3] * 4);
                down = 1;
                oldest = oldest < 0 ? at : oldest;
        }
        if (down != touch_down)
                touch_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOUCH, down);
        touch_down = down;
        if (oldest >= 0)
        {
                touch_feed(COMPOSITOR_EV_ABS, 0, fingers[oldest][2] * 4);
                touch_feed(COMPOSITOR_EV_ABS, 1, fingers[oldest][3] * 4);
        }
        touch_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
}

// One finger in slot 0, id 1: down or moved to x, y, or lifted; the clock moves on 10 ms.
static fn touch_one(int x, int y, b32 lift)
{
        const int finger[1][4] = {{0, lift ? -1 : 1, x, y}};

        touch_clock += 10000;
        touch_report(1, finger);
}

// The events of row since head of a type, with a for a button's code (or any for -1), and the flags they carried.
static unsigned int touch_seen(int row, unsigned int head, unsigned int type, int a, unsigned int address_to flags)
{
        struct windows address_to table = compositor.table;
        unsigned int count = 0;

        if (flags)
                address_to flags = 0;
        for (unsigned int at = head; at != table->event_head[row]; at++)
                if (table->event_type[row][at % WINDOWS_EVENTS] == type && (a < 0 || table->event_a[row][at % WINDOWS_EVENTS] == a))
                {
                        count++;
                        if (flags)
                                address_to flags |= table->event_flags[row][at % WINDOWS_EVENTS];
                }
        return count;
}

// The button events of row since head, each its code and whether it went down, as a number to compare: 0x1110 down.
static unsigned long touch_buttons(int row, unsigned int head)
{
        struct windows address_to table = compositor.table;
        unsigned long made = 0;

        for (unsigned int at = head; at != table->event_head[row]; at++)
                if (table->event_type[row][at % WINDOWS_EVENTS] == WINDOW_EVENT_BUTTON)
                        made = made << 16 | (unsigned long)table->event_a[row][at % WINDOWS_EVENTS] << 4 |
                               (table->event_flags[row][at % WINDOWS_EVENTS] & WINDOW_KEY_DOWN ? 1 : 0) |
                               (table->event_flags[row][at % WINDOWS_EVENTS] & WINDOW_KEY_CANCEL ? 2 : 0);
        return made;
}

/*
        A snap sequence's drawn frames. After each present the scanout is what a full recompose of the scene is: nothing stale
        is left by a damage rectangle that was not drawn, a tint that is gone, a thumbnail that came up or a grid that went
        down. And every pixel that changed from the frame before lies in the window's old and new rectangles, the preview's old
        and new rectangles, or a thumbnail the assist shows (each 2 px grown for its line). Nothing else on the screen changes.
        Run at the motion the table asks for and at reduced motion, where the overlays do not fade.
*/
static unsigned int snap_stale_pixels(void)
{
    static unsigned int reference[LAYOUT_SIDE * LAYOUT_SIDE];
    unsigned int *live = compositor.scanout, pitch = compositor.scanout_pitch / 4, stale = 0;

    compositor.scanout = reference;
    compositor_damage(0, 0, (int)compositor.width, (int)compositor.height);
    compositor_present();
    compositor.scanout = live;
    for (unsigned int y = 0; y < compositor.height; y++)
        for (unsigned int x = 0; x < compositor.width; x++)
            stale += (live[y * pitch + x] & 0xffffffu) != (reference[y * pitch + x] & 0xffffffu);
    return stale;
}

static int snap_rect_holds(const int *rect, int x, int y)
{
    return rect[2] > 0 && x >= rect[0] - 2 && x < rect[0] + rect[2] + 2 && y >= rect[1] - 2 && y < rect[1] + rect[3] + 2;
}

static void snap_window_rect(int row, int *rect)
{
    rect[0] = (int)compositor.x[row], rect[1] = (int)compositor.y[row];
    rect[2] = (int)compositor.w[row], rect[3] = (int)compositor.h[row];
}

// The thumbnails the snap assist shows, copied: what a step may change besides the window and the preview.
static void snap_thumbs_copy(int (*into)[4], int *count)
{
    *count = compositor.assist_count;
    for (int thumb = 0; thumb < compositor.assist_count; thumb++)
        memory_copy(into[thumb], compositor.assist_rect[thumb], sizeof(into[thumb]));
}

// Whether a pixel is in one of the thumbnails copied (a thumbnail before the step or after it).
static int snap_thumb_holds(const int (*thumbs)[4], int count, int x, int y)
{
    for (int thumb = 0; thumb < count; thumb++)
        if (snap_rect_holds(thumbs[thumb], x, y))
            return 1;
    return 0;
}

static void check_compositor_snap_frames(void)
{
    static unsigned int before[LAYOUT_SIDE * LAYOUT_SIDE];
    static const int path[][2] = {{20, 60}, {0, 60}, {0, 0}, {100, 100}, {156, 60}, {100, 176}, {60, 60}, {0, 0}, {156, 0}};
    unsigned int stale = 0, outside = 0, pitch = 0;
    int steps = 0, let_go_clean = 0;

    if (!layout_table_map(256))
    {
        check("snap frames: the table is mapped", false);
        return;
    }
    for (int motion = 0; motion < 2; motion++)
    {
        int prev_window[4], prev_preview[4], window[4], preview[4], prev_thumbs[WINDOWS][4], thumbs[WINDOWS][4], prev_count, count;

        snap_fresh();
        compositor.table->motion = (unsigned int)motion;
        layout_show(2, 150, 150, 80, 60);
        layout_paint(1, 0x406080u);
        layout_paint(2, 0x804020u);
        compositor_damage(0, 0, 256, 256);
        compositor_present();
        pitch = compositor.scanout_pitch / 4;
        for (unsigned int at = 0; at < sizeof(path) / sizeof(path[0]); at++)
        {
            memory_copy(before, layout_pixels, sizeof(before));
            snap_window_rect(1, prev_window);
            memory_copy(prev_preview, compositor.preview, sizeof(prev_preview));
            snap_thumbs_copy(prev_thumbs, &prev_count);
            snap_hand(1, path[at][0], path[at][1], 50, 40);
            compositor_present();
            stale += snap_stale_pixels();
            snap_window_rect(1, window);
            memory_copy(preview, compositor.preview, sizeof(preview));
            snap_thumbs_copy(thumbs, &count);
            for (unsigned int y = 0; y < 256; y++)
                for (unsigned int x = 0; x < 256; x++)
                    if ((layout_pixels[y * pitch + x] & 0xffffffu) != (before[y * pitch + x] & 0xffffffu) &&
                        !snap_rect_holds(prev_window, (int)x, (int)y) && !snap_rect_holds(window, (int)x, (int)y) &&
                        !snap_rect_holds(prev_preview, (int)x, (int)y) && !snap_rect_holds(preview, (int)x, (int)y) &&
                        !snap_thumb_holds(prev_thumbs, prev_count, (int)x, (int)y) && !snap_thumb_holds(thumbs, count, (int)x, (int)y))
                        outside++;
            steps++;
        }
        snap_let_go(156, 0, 50, 40);
        compositor_present();
        stale += snap_stale_pixels();
        let_go_clean = let_go_clean || (compositor.preview[2] == 0 && compositor.preview_row == COMPOSITOR_NONE);
    }
    check("snap frames: every frame of a drag, at the table's motion and at reduced motion, is the full recompose of the scene",
          stale == 0);
    check("snap frames: each frame changes only the window, the preview, a thumbnail the assist shows, and their old places",
          outside == 0);
    check("snap frames: the sequences ran, and each let go left no preview", steps == 18 && let_go_clean);
    compositor.table->motion = 0;
}

/*
        A finger's window touches an edge, and its preview holds while the finger is down. A finger is led (the window is drawn
        where the finger is going), and a frame with no new sample takes the lead away again (compositor_predict_clear), so the
        window's rectangle changes between samples by the lead. The snap is the finger's own rectangle, so the preview is on
        from the sample the finger touches the edge and stays on at every sample and between them.
*/
/*
        One observation of the snap preview, after a report or a clear: whether it is on, and whether its rectangle changed from
        the last one on. A preview that goes off after it was on is lost; one whose rectangle changes is counted.
*/
static void snap_note(int *seen, int *last, int *changes, int *lost)
{
    int on = compositor.preview[2] > 0;

    if (on && *seen && memory_compare(last, compositor.preview, sizeof(compositor.preview[0]) * 4))
        (*changes)++;
    if (!on && *seen)
        (*lost)++;
    if (on)
    {
        memory_copy(last, compositor.preview, sizeof(compositor.preview[0]) * 4);
        *seen = 1;
    }
}

static void check_compositor_touch_hold(void)
{
    int lost = 0, changes = 0, sampled = 0, seen = 0, last[4] = {0, 0, 0, 0};

    snap_fresh();
    if (system_call_2(syscall(pipe2), (positive)wheel_pipe, O_NONBLOCK))
    {
        check("touch hold: the input pipe is made", false);
        return;
    }
    compositor.inputs[0] = wheel_pipe[0];
    compositor.input_count = 1;
    compositor.touchscreen[0] = 1;
    compositor.screen_touch[0] = (struct compositor_touch_screen){0};
    for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        compositor.screen_touch[0].id[slot] = compositor.screen_touch[0].was[slot] = -1;
    compositor.screen_touch[0].slots = 1;
    compositor.screen_touch[0].range[1] = compositor.screen_touch[0].range[3] = 1024;
    touch_clock = 1000000;
    touch_down = 0;
    touch_one(80, 82, false); /* (just under the window's top left corner's target, which a finger 19 px in along both edges is in) */
    check("touch hold: a finger down on a window's top starts its drag", compositor.drag == 1);

    /* Up to the top edge: the window's top touches, and the maximized target shows. */
    for (int y = 80; y >= 20; y -= 2)
    {
        touch_one(80, y, false);
        touch_clock += 8000;
        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
    }
    check("touch hold: a finger up to the top edge shows the maximized target", compositor.preview_row == 1 && compositor.preview[2] == 256);

    /* Along the top toward the corner: the target is the quarter once the window's left is near the corner, and that
       changes once as the finger moves on. Each report takes the lead off again between reports; a target that flips
       back and forth between them is the flicker. */
    for (int x = 80; x >= 30; x -= 2)
    {
        touch_one(x, 20, false);
        sampled = 1;
        snap_note(&seen, last, &changes, &lost);
        touch_clock += 8000;
        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
        snap_note(&seen, last, &changes, &lost);
    }
    check("touch hold: sliding along the top, the target is held while the finger is down, and it changes only once, to the quarter",
          sampled && seen && lost == 0 && changes <= 1 && compositor.preview[2] == 128 && compositor.preview[0] == 0);
    touch_one(30, 20, true);
    check("touch hold: the finger lifted takes the quarter it held", compositor.arranged[1] == COMPOSITOR_TOP_LEFT);
    compositor.table->motion = 0;
}

/*
        The touch path, end to end as a field's window gets it: a finger's reports go through the compositor's input, and the
        events the window's own queue holds (windows_event) are drained into a text field the way the app's loop drains them
        (a motion's place is kept for the presses after it). The finger taps the field, is held for a long press, drags on by
        words, lifts; then it takes the end's handle and moves that end by characters. The window is far from the screen's
        edges, as a field's is in a window with room, so no edge grip takes a press meant for the text. Each step is checked on
        the field's selection and on the pixels its handle draws.
*/
#define TOUCH_PATH_LEFT 20
#define TOUCH_PATH_TOP 20

static int touch_path_x(struct ui address_to wui, struct ui_text address_to t, p64 offset)
{
    static struct ui_text_layout l;

    ui_text_layout_line(wui, t, 0, &l);
    return t->left - (int)t->scroll_x + ui_text_x_of(&l, offset);
}

static void touch_path_drain(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, int *px, int *py, p32 address_to asks)
{
    struct window_event event;

    while (windows_event(own, &event))
    {
        if (event.type == WINDOW_EVENT_MOTION)
            *px = event.a, *py = event.b;
        *asks |= ui_field_event(wui, t, null, &event, *px, *py);
    }
}

static void check_touch_path(struct ui address_to wui)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static struct ui_text t;
    struct windows address_to table;
    struct window_row own;
    int px = 0, py = 0, bx, by, gx, ex, knob;
    unsigned int accent = ui_role(UI_PRIMARY) & 0xffffffu;
    p32 asks = 0;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.lit_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.hold = -1;
    compositor.holds = ~0ul;
    compositor.told_row = COMPOSITOR_NONE;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map() || system_call_2(syscall(pipe2), (positive)wheel_pipe, O_NONBLOCK))
    {
        check("touch path: the table and the device are made", false);
        return;
    }
    table = compositor.table;
    table->x[1] = TOUCH_PATH_LEFT, table->y[1] = TOUCH_PATH_TOP, table->width[1] = 230, table->height[1] = 200;
    glass_paint(1, 0, 0, 230, 200, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    compositor.inputs[0] = wheel_pipe[0];
    compositor.input_count = 1;
    touch_clock = 1000000;
    touch_down = 0;
    compositor.touchscreen[0] = 1;
    compositor.screen_touch[0] = (struct compositor_touch_screen){0};
    for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        compositor.screen_touch[0].id[slot] = compositor.screen_touch[0].was[slot] = -1;
    compositor.screen_touch[0].slots = 1;
    compositor.screen_touch[0].range[1] = compositor.screen_touch[0].range[3] = 1020;
    compositor.pointer_x = 60;
    compositor.pointer_y = 70;

    own.table = table, own.row = 1, own.door = -1;
    ui_field_open(&t, &own, "alpha beta gamma");
    doc_select_set(&t.doc, 0, 0);
    ui_begin(wui, wui->pixels, wui->pitch, wui->width, wui->height);
    ui_text_draw(wui, &t, 20, 60, 190, 44, 0);
    t.focused = true;
    by = t.top + t.line_height / 2;
    bx = touch_path_x(wui, &t, 6) + 1;
    gx = touch_path_x(wui, &t, 13) + 1;
    touch_path_drain(wui, &t, &own, &px, &py, &asks);

    /* The tap: a finger that lands and lifts puts a caret, and asks for no menu. */
    touch_one(TOUCH_PATH_LEFT + bx, TOUCH_PATH_TOP + by, false);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    touch_one(TOUCH_PATH_LEFT + bx, TOUCH_PATH_TOP + by, true);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    check("touch path: a tap puts the caret where the finger is", doc_sel_start(&t.doc.selections[0]) == 6 && doc_sel_end(&t.doc.selections[0]) == 6);

    /* The long press: held still, the word under the finger is selected, and the right button is no menu. */
    asks = 0;
    touch_one(TOUCH_PATH_LEFT + bx, TOUCH_PATH_TOP + by, false);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    check("touch path: a long press selects the word under the finger", doc_sel_start(&t.doc.selections[0]) == 6 && doc_sel_end(&t.doc.selections[0]) == 10);
    check("touch path: and the long press asks for no menu", !(asks & UI_TEXT_MENU));

    /* The drag after it: the finger moves on, and the selection grows by words to the end of the word it reaches. */
    asks = 0;
    touch_one(TOUCH_PATH_LEFT + gx, TOUCH_PATH_TOP + by, false);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    check("touch path: a drag after the long press extends the selection by words", doc_sel_start(&t.doc.selections[0]) == 6 && doc_sel_end(&t.doc.selections[0]) == 16);
    touch_one(TOUCH_PATH_LEFT + gx, TOUCH_PATH_TOP + by, true);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    check("touch path: the lift keeps the selection, and nothing is still dragged", doc_sel_end(&t.doc.selections[0]) == 16 && !t.sel.dragging);

    /* The handle: the end of the selection has a knob hung under its line, in the accent, and a finger on it moves that end by
       characters, the start staying where it is. */
    ui_begin(wui, wui->pixels, wui->pitch, wui->width, wui->height);
    ui_text_draw(wui, &t, 20, 60, 190, 44, 0);
    t.sel.bar = false; // the actions bar the lift showed lies over the field in a window this small: it is another row's
    ui_text_overlay(wui, &t);
    ex = touch_path_x(wui, &t, 16);
    knob = t.top + t.line_height + 6 * compositor.scale / 100;
    check("touch path: the end of the selection has a handle, a knob in the accent under its line",
          (wui->pixels[(positive)knob * wui->pitch + (positive)ex] & 0xffffffu) == accent);
    touch_one(TOUCH_PATH_LEFT + ex, TOUCH_PATH_TOP + knob, false);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    touch_one(TOUCH_PATH_LEFT + gx, TOUCH_PATH_TOP + by, false);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    touch_one(TOUCH_PATH_LEFT + gx, TOUCH_PATH_TOP + by, true);
    touch_path_drain(wui, &t, &own, &px, &py, &asks);
    check("touch path: a finger on the end's handle moves that end by characters, the start staying",
          doc_sel_start(&t.doc.selections[0]) == 6 && doc_sel_end(&t.doc.selections[0]) == 13);

    /*
        A long press whose pointer a second device left elsewhere: a tablet's stale place arrives while the finger is held (the USB tablet
        of a guest, the second pointing device of a machine), and the hold is placed at the finger's own place all the same: the pointer
        is where the finger is, not where the tablet is.
    */
    {
        int fx = TOUCH_PATH_LEFT + gx, fy = TOUCH_PATH_TOP + by;

        touch_one(fx, fy, false);
        touch_path_drain(wui, &t, &own, &px, &py, &asks);
        compositor.tablet_x = 1200, compositor.tablet_y = 900, compositor.tablet_have = 3;
        compositor_report((positive)touch_clock, (positive)touch_clock);
        check("touch path: a tablet's place does not move the pointer off a finger that is down", compositor.pointer_x == fx && compositor.pointer_y == fy);
        compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
        touch_path_drain(wui, &t, &own, &px, &py, &asks);
        check("touch path: a long press is placed at the finger, whatever a tablet reported while the finger was down",
              compositor.pointer_x == fx && compositor.pointer_y == fy);
        touch_one(fx, fy, true);
        touch_path_drain(wui, &t, &own, &px, &py, &asks);
    }

    ui_text_close(&t);
}

/*
        Auto-scroll of a text selection, end to end as a window gets it: a mouse held down in a text surface and dragged past
        its visible bounds, and a finger that has held a word and drags it past the bounds. The window's events come from the
        compositor (its grab for a mouse, its finger reports), are drained from the window's queue into the widget the way the
        app's loop does, and the app's tick steps the scroll 16 ms at a time. A code view scrolls up or down past its box; a
        field (the find bar, the kit's fields, the settings' fields) scrolls left or right past its box.
*/
#define SCROLL_LEFT 20
#define SCROLL_TOP 20
#define SCROLL_CODE_LEFT 10
#define SCROLL_CODE_TOP 30
#define SCROLL_CODE_WIDTH 210
#define SCROLL_CODE_HEIGHT 150
#define SCROLL_FIELD_LEFT 10
#define SCROLL_FIELD_TOP 60
#define SCROLL_FIELD_WIDTH 200
#define SCROLL_FIELD_HEIGHT 44
#define SCROLL_LINE 8 // bytes a line of the code view's text takes: "line NN\n"

struct scroll_answer
{
    int top, before, scroll, moved;
    p64 start, finish;
};

static void scroll_setup(int touch)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.lit_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.hold = -1;
    compositor.holds = ~0ul;
    compositor.told_row = COMPOSITOR_NONE;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    glass_table_map();
    system_call_2(syscall(pipe2), (positive)wheel_pipe, O_NONBLOCK);
    table = compositor.table;
    table->x[1] = SCROLL_LEFT, table->y[1] = SCROLL_TOP, table->width[1] = 230, table->height[1] = 200;
    glass_paint(1, 0, 0, 230, 200, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    compositor.inputs[0] = wheel_pipe[0];
    compositor.input_count = 1;
    touch_clock = 1000000;
    touch_down = 0;
    if (touch)
    {
        compositor.touchscreen[0] = 1;
        compositor.screen_touch[0] = (struct compositor_touch_screen){0};
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
            compositor.screen_touch[0].id[slot] = compositor.screen_touch[0].was[slot] = -1;
        compositor.screen_touch[0].slots = 1;
        compositor.screen_touch[0].range[1] = compositor.screen_touch[0].range[3] = 1020;
    }
    compositor.pointer_x = 60;
    compositor.pointer_y = 70;
}

// The mouse's place, in desktop pixels, kept and real: no lead and no clamp.
static void scroll_point(int x, int y)
{
    compositor.kept_x = compositor.real_x = x;
    compositor.kept_y = compositor.real_y = y;
    compositor.pointer_x = x;
    compositor.pointer_y = y;
}

// The window's queue drained into the widget as the app's loop drains it: a motion's place is kept for the presses after it;
// a field takes the event through ui_field_event, a code view through ui_text_event.
static void scroll_drain(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, int *px, int *py, int field)
{
    struct window_event event;

    while (windows_event(own, &event))
    {
        if (event.type == WINDOW_EVENT_MOTION)
            *px = event.a, *py = event.b;
        if (field)
            ui_field_event(wui, t, null, &event, *px, *py);
        else
            ui_text_event(wui, t, &event, *px, *py);
    }
}

// The app's tick, 16 ms at a time for 200 ms from now.
static void scroll_ticks(struct ui address_to wui, struct ui_text address_to t, positive now)
{
    b32 redraw = false;

    for (int step = 0; step <= 12; step++)
        (ui_kit.now = ui_kit.test_clock = (b64)(now + (positive)step * 16000000ull), ui_select_autoscroll(wui, ui_text_sel(t), &redraw));
}

static void scroll_code(struct ui address_to wui, struct ui_text address_to t, int top)
{
    static char text[40 * SCROLL_LINE];
    positive length = 0;

    for (int n = 0; n < 40; n++)
    {
        text[length++] = 'l', text[length++] = 'i', text[length++] = 'n', text[length++] = 'e', text[length++] = ' ';
        text[length++] = (char)('0' + n / 10), text[length++] = (char)('0' + n % 10), text[length++] = '\n';
    }
    ui_text_open(t, UI_TEXT_CODE, (const p8 address_to)text, length, ui_text_keymap());
    t->focused = true;
    t->top_line = (p64)top;
    t->sel.dragging = false;
    t->sel.edge.past = 0;
    t->sel.edge.at = 0;
    ui_text_draw(wui, t, SCROLL_CODE_LEFT, SCROLL_CODE_TOP, SCROLL_CODE_WIDTH, SCROLL_CODE_HEIGHT, 0);
}

// The same code view with one line longer than its box: line 12 is SCROLL_WIDE_COLUMNS columns, the rest seven as before.
#define SCROLL_WIDE_LINE 12
#define SCROLL_WIDE_COLUMNS 64

static void scroll_code_wide(struct ui address_to wui, struct ui_text address_to t, int top)
{
    static char text[40 * SCROLL_LINE + SCROLL_WIDE_COLUMNS];
    positive length = 0;

    for (int n = 0; n < 40; n++)
    {
        text[length++] = 'l', text[length++] = 'i', text[length++] = 'n', text[length++] = 'e', text[length++] = ' ';
        text[length++] = (char)('0' + n / 10), text[length++] = (char)('0' + n % 10);
        for (int column = 7; n == SCROLL_WIDE_LINE && column < SCROLL_WIDE_COLUMNS; column++)
            text[length++] = 'x';
        text[length++] = '\n';
    }
    ui_text_open(t, UI_TEXT_CODE, (const p8 address_to)text, length, ui_text_keymap());
    t->focused = true;
    t->top_line = (p64)top;
    t->sel.dragging = false;
    t->sel.edge.past = 0;
    t->sel.edge.at = 0;
    ui_text_draw(wui, t, SCROLL_CODE_LEFT, SCROLL_CODE_TOP, SCROLL_CODE_WIDTH, SCROLL_CODE_HEIGHT, 0);
}

static void scroll_field(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, b32 at_end)
{
    const char address_to text = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";
    p64 length = (p64)string_length((string_address)text);

    ui_field_open(t, own, text);
    t->focused = true;
    doc_select_set(&t->doc, at_end ? length : 0, at_end ? length : 0);
    t->sel.dragging = false;
    t->sel.edge.past = 0;
    t->sel.edge.at = 0;
    ui_text_draw(wui, t, SCROLL_FIELD_LEFT, SCROLL_FIELD_TOP, SCROLL_FIELD_WIDTH, SCROLL_FIELD_HEIGHT, 0);
}

static void scroll_answer_of(struct ui_text address_to t, struct scroll_answer address_to a)
{
    a->top = (int)t->top_line;
    a->scroll = (int)t->scroll_x;
    a->start = doc_sel_start(&t->doc.selections[t->doc.primary]);
    a->finish = doc_sel_end(&t->doc.selections[t->doc.primary]);
}

// A code view, a mouse: the press on the third row, the drag to target_y (window-local, perhaps off the window), the ticks, the lift.
static void scroll_code_mouse(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, int target_y,
                              struct scroll_answer address_to a)
{
    int px = 0, py = 0, row, x = SCROLL_CODE_LEFT + 40;
    positive now = 1000000000ull;

    scroll_code(wui, t, 10);
    row = t->top + 2 * t->line_height + t->line_height / 2;
    scroll_point(SCROLL_LEFT + x, SCROLL_TOP + row);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    scroll_drain(wui, t, own, &px, &py, 0);
    scroll_point(SCROLL_LEFT + x, SCROLL_TOP + target_y);
    compositor_motion();
    scroll_drain(wui, t, own, &px, &py, 0);
    scroll_ticks(wui, t, now);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    scroll_drain(wui, t, own, &px, &py, 0);
    scroll_answer_of(t, a);
    ui_text_close(t);
}

// A code view, a finger: it lands on the third row, is held for the long press (its word selected), and drags to target_y.
static void scroll_code_touch(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, int target_y,
                              struct scroll_answer address_to a)
{
    int px = 0, py = 0, row, x = SCROLL_CODE_LEFT + 40;
    positive now = 1000000000ull;

    scroll_code(wui, t, 10);
    row = t->top + 2 * t->line_height + t->line_height / 2;
    touch_one(SCROLL_LEFT + x, SCROLL_TOP + row, false);
    scroll_drain(wui, t, own, &px, &py, 0);
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
    scroll_drain(wui, t, own, &px, &py, 0);
    touch_one(SCROLL_LEFT + x, SCROLL_TOP + target_y, false);
    scroll_drain(wui, t, own, &px, &py, 0);
    scroll_ticks(wui, t, now);
    touch_one(SCROLL_LEFT + x, SCROLL_TOP + target_y, true);
    scroll_drain(wui, t, own, &px, &py, 0);
    scroll_answer_of(t, a);
    ui_text_close(t);
}

// A field, a mouse: the press 40 px into the box, the drag to target_x (window-local), the ticks, the lift.
static void scroll_field_mouse(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, b32 at_end,
                               int target_x, struct scroll_answer address_to a)
{
    int px = 0, py = 0, y = SCROLL_FIELD_TOP + SCROLL_FIELD_HEIGHT / 2, x = SCROLL_FIELD_LEFT + 40;
    positive now = 1000000000ull;

    scroll_field(wui, t, own, at_end);
    a->before = (int)t->scroll_x;
    scroll_point(SCROLL_LEFT + x, SCROLL_TOP + y);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    scroll_drain(wui, t, own, &px, &py, 1);
    scroll_point(SCROLL_LEFT + target_x, SCROLL_TOP + y);
    compositor_motion();
    scroll_drain(wui, t, own, &px, &py, 1);
    a->moved = (int)t->scroll_x;
    scroll_ticks(wui, t, now);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    scroll_drain(wui, t, own, &px, &py, 1);
    scroll_answer_of(t, a);
    ui_text_close(t);
}

// A field, a finger: held on the box for the long press (its word selected), dragged to target_x, lifted.
static void scroll_field_touch(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, b32 at_end,
                               int target_x, struct scroll_answer address_to a)
{
    int px = 0, py = 0, y = SCROLL_FIELD_TOP + SCROLL_FIELD_HEIGHT / 2, x = SCROLL_FIELD_LEFT + 40;
    positive now = 1000000000ull;

    scroll_field(wui, t, own, at_end);
    a->before = (int)t->scroll_x;
    touch_one(SCROLL_LEFT + x, SCROLL_TOP + y, false);
    scroll_drain(wui, t, own, &px, &py, 1);
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
    scroll_drain(wui, t, own, &px, &py, 1);
    touch_one(SCROLL_LEFT + target_x, SCROLL_TOP + y, false);
    scroll_drain(wui, t, own, &px, &py, 1);
    a->moved = (int)t->scroll_x;
    scroll_ticks(wui, t, now);
    touch_one(SCROLL_LEFT + target_x, SCROLL_TOP + y, true);
    scroll_drain(wui, t, own, &px, &py, 1);
    scroll_answer_of(t, a);
    ui_text_close(t);
}

/*
        A field's drag past its right edge, with the app drawing between the events and the ticks: the view does not jump. The
        drag's point is not revealed while it is past the edge (the tick scrolls it), so a draw in the middle of the drag leaves
        the view where it was, and each tick moves it a few pixels, never a quarter of the field.
*/
static void scroll_field_drawn(struct ui address_to wui, struct ui_text address_to t, const struct window_row address_to own, int target_x,
                               int *drawn_moved, int *largest)
{
    int px = 0, py = 0, y = SCROLL_FIELD_TOP + SCROLL_FIELD_HEIGHT / 2, x = SCROLL_FIELD_LEFT + 40, prev;
    positive now = 1000000000ull;
    b32 redraw = false;

    scroll_field(wui, t, own, 0);
    scroll_point(SCROLL_LEFT + x, SCROLL_TOP + y);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    scroll_drain(wui, t, own, &px, &py, 1);
    scroll_point(SCROLL_LEFT + target_x, SCROLL_TOP + y);
    compositor_motion();
    scroll_drain(wui, t, own, &px, &py, 1);
    ui_text_draw(wui, t, SCROLL_FIELD_LEFT, SCROLL_FIELD_TOP, SCROLL_FIELD_WIDTH, SCROLL_FIELD_HEIGHT, 0);
    *drawn_moved = (int)t->scroll_x;
    prev = (int)t->scroll_x;
    *largest = 0;
    for (int step = 1; step <= 12; step++)
    {
        int jump;

        (ui_kit.now = ui_kit.test_clock = (b64)(now + (positive)step * 16000000ull), ui_select_autoscroll(wui, ui_text_sel(t), &redraw));
        ui_text_draw(wui, t, SCROLL_FIELD_LEFT, SCROLL_FIELD_TOP, SCROLL_FIELD_WIDTH, SCROLL_FIELD_HEIGHT, 0);
        jump = (int)t->scroll_x - prev;
        *largest = jump > *largest ? jump : *largest;
        prev = (int)t->scroll_x;
    }
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    scroll_drain(wui, t, own, &px, &py, 1);
    ui_text_close(t);
}

/*
        The caret's blink as the loop wakes it: each wait ends at the change it was asked for and 100 microseconds late, as a
        sleep does. A fade begins and ends on a solid or a gone cover (its smoothstep is 0 or FULL at its ends), and a wake that
        read one of those as "flat" slept the whole fade, so the caret snapped. Over a cycle and a half the fades are drawn.
*/
static void caret_fade_check(void)
{
    positive since = 1000000000ull, now = since, due = 0, stop = since + 1500000000ull;
    unsigned int saved = ui_kit.caret_speed;
    int mids = 0;

    ui_kit.caret_speed = UI_CARET_NORMAL;
    while (now < stop)
    {
        unsigned int cover = (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_caret_at(since, &due));

        mids += cover > 0 && cover < UI_CARET_FULL;
        if (!due)
            break;
        now = due + 100000;
    }
    ui_kit.caret_speed = saved;
    check("caret: a fade is drawn, not skipped: it has at least 8 covers between solid and gone over a cycle and a half", mids >= 8);
}

/*
        A fade is drawn on the steps of UI_CARET_STEP_NS (16 ms into the cycle) and at no other time: a wake between two steps reads the cover the step drew and the
        loop's frame is not due. The compositor knocks a program after each frame it takes, the knock wakes it at once, and a cover that moved a part in 255 between
        two wakes was a frame each time (a frame a millisecond through each fade, 52 to 202 frames a second at rest in Settings with its filter focused).
*/
static void caret_step_check(void)
{
    positive since = 1000000000ull, due = 0, due_in = 0;
    unsigned int saved = ui_kit.caret_speed, step_start, in_step, late_in_step, next_step, solid, gone;
    // 1.06 s a cycle: the caret is read a quarter of a cycle in, and its fade down runs from 490.25 ms to 569.75 ms into the cycle, so 496 ms is a step's start in it
    positive at_496 = since + 496000000ull - 265000000ull;

    ui_kit.caret_speed = UI_CARET_NORMAL;
    step_start = (ui_kit.now = ui_kit.test_clock = (b64)(at_496), ui_caret_at(since, &due));
    in_step = (ui_kit.now = ui_kit.test_clock = (b64)(at_496 + 4000000ull), ui_caret_at(since, &due));
    due_in = due - (at_496 + 4000000ull);
    late_in_step = (ui_kit.now = ui_kit.test_clock = (b64)(at_496 + 15900000ull), ui_caret_at(since, &due));
    next_step = (ui_kit.now = ui_kit.test_clock = (b64)(at_496 + 16000000ull), ui_caret_at(since, &due));
    solid = (ui_kit.now = ui_kit.test_clock = (b64)(since + 100000000ull), ui_caret_at(since, &due));
    gone = (ui_kit.now = ui_kit.test_clock = (b64)(since + 400000000ull), ui_caret_at(since, &due));
    ui_kit.caret_speed = saved;
    check("caret: a fade in the middle of a step shows the cover the step drew", step_start == in_step && step_start == late_in_step);
    check("caret: the next step is another cover, and a fading caret is between solid and gone", next_step != step_start && step_start > 0 && step_start < UI_CARET_FULL);
    check("caret: the loop's wait in a fade ends at the next step", due_in == 12000000ull);
    check("caret: away from the fades it is solid, then gone", solid == UI_CARET_FULL && gone == 0);
}

/*
        A field's caret as the loop has it. A focused field blinks for as long as it has the keys: over one period it goes solid, then
        gone, then solid where the period began, and each toggle asks for a frame. It still blinks after a long idle (the caret used to
        stop ten seconds after the last key). A field without the keys draws no caret, whatever its phase, and asks for no frame.
*/
static unsigned int caret_field_tick(struct ui_text address_to t, positive now, positive address_to due, b32 address_to redraw)
{
    *redraw = false;
    ui_kit.now = ui_kit.test_clock = (b64)now;
    *due = ui_text_tick(t, redraw);
    return UI_CARET_FULL - t->caret_fade;
}

static void caret_field_check(struct ui address_to wui)
{
    static unsigned int plain[400 * 60], drawn[400 * 60], shown[400 * 60];
    static struct ui_text t;
    unsigned int *kept_pixels = wui->pixels;
    int kept_pitch = wui->pitch, kept_width = wui->width, kept_height = wui->height, toggles = 0, waits = 0, seen_solid = 0, seen_gone = 0, differ_off = 0, differ_on = 0;
    positive since = 1000000000ull, now = since, due = 0, off_due = 0, period_end = since + 1060000000ull, idle = since + 30000000000ull;
    b64 kept_clock = ui_kit.test_clock, kept_now = ui_kit.now;
    unsigned int saved = ui_kit.caret_speed, start, last, cover, i;
    b32 redraw = false;

    ui_kit.caret_speed = UI_CARET_NORMAL;
    ui_kit.test_clock = since;
    ui_text_open(&t, UI_TEXT_FIELD, (const p8 address_to) "alpha", 5, ui_text_keymap());
    t.focused = true;
    start = caret_field_tick(&t, now, &due, &redraw);
    while (due && due < period_end)
    {
        now = due;
        cover = caret_field_tick(&t, now, &due, &redraw);
        seen_solid += cover == UI_CARET_FULL;
        seen_gone += cover == 0;
        toggles += redraw;
        waits += due != 0;
    }
    last = caret_field_tick(&t, period_end, &due, &redraw);
    check("caret field: a focused field's caret goes solid, then gone, then solid where the period began, over one period",
          start == UI_CARET_FULL && seen_solid && seen_gone && last == start);
    check("caret field: each toggle of a focused field asks for a redraw, and it keeps asking for the next", toggles >= 2 && waits >= 4);

    caret_field_tick(&t, idle, &due, &redraw);
    check("caret field: a focused field still blinks after thirty seconds idle, with a frame due at its next toggle", due > idle && due - idle <= 1060000000ull);

    t.focused = false;
    t.caret_fade = 0;
    caret_field_tick(&t, idle + 100000000ull, &off_due, &redraw);
    check("caret field: a field without the keys asks for no frame, and nothing redraws it", off_due == 0 && !redraw);

    ui_begin(wui, plain, 400, 400, 60);
    ui_text_draw(wui, &t, 10, 10, 380, 40, 0);
    t.focused = true, t.caret_fade = UI_CARET_FULL;
    ui_begin(wui, drawn, 400, 400, 60);
    ui_text_draw(wui, &t, 10, 10, 380, 40, 0);
    t.caret_fade = 0;
    ui_begin(wui, shown, 400, 400, 60);
    ui_text_draw(wui, &t, 10, 10, 380, 40, 0);
    for (i = 0; i < 400 * 60; i++)
    {
        differ_off += plain[i] != drawn[i];
        differ_on += plain[i] != shown[i];
    }
    check("caret field: a field without the keys draws no caret: its picture is a focused field's with the caret gone", differ_off == 0);
    check("caret field: a focused field draws its caret solid, in pixels the unfocused field does not have", differ_on > 0);
    // A line longer than the window is wide, the caret at its end: its last words are drawn at the field's right, not cut where the
    // kit's scratch (as wide as the window) ends.
    {
        static p8 path[512];
        int inked = 0;

        for (i = 0; i < 400; i++)
            path[i] = i % 10 == 9 ? '/' : (p8)('a' + i % 7);
        path[400] = 0;
        ui_text_set(&t, path, 400);
        t.focused = false, t.caret_fade = 0;
        ui_begin(wui, plain, 400, 400, 60);
        ui_text_draw(wui, &t, 10, 10, 380, 40, 0);
        ui_text_set(&t, path, 0);
        ui_begin(wui, drawn, 400, 400, 60);
        ui_text_draw(wui, &t, 10, 10, 380, 40, 0);
        for (i = 0; i < 400 * 60; i++)
            inked += i % 400 >= 300 && i % 400 < 388 && plain[i] != drawn[i];
        check("field: a line longer than the window is wide is drawn where it is scrolled to: with the caret at its end its last words are at the field's right", inked > 60);
    }

    ui_text_close(&t);
    ui_kit.caret_speed = saved;
    ui_kit.test_clock = kept_clock, ui_kit.now = kept_now;
    wui->pixels = kept_pixels, wui->pitch = kept_pitch, wui->width = kept_width, wui->height = kept_height;
}

/*
        The scroll region's motion, read off its body (ui_body, s->body), in 256ths of a device pixel (the unit the rows are
        written in). at is where the motion has carried the body (past an end, the overscroll is in it); aim is the notch's
        target while one is followed, else at; shown is where it is drawn (the rubber band's share past an end). gliding is a
        release's coast or a notch's follow, back a spring home, dragging a hand or finger on it.
*/
static b64 scroll_at(const struct ui_scroll address_to s)
{
    return s->body.raw >> (UI_Q - 8);
}

static b64 scroll_aim(const struct ui_scroll address_to s)
{
    return (s->body.state == BODY_NOTCH ? s->body.target : s->body.raw) >> (UI_Q - 8);
}

static b64 scroll_shown(const struct ui_scroll address_to s)
{
    struct ui_body b = s->body;

    ui_body_fit(&b, (b64)(s->view > 0 ? s->view : 0) << UI_Q, (b64)(s->content > 0 ? s->content : 0) << UI_Q);
    return ui_body_drawn(&b) >> (UI_Q - 8);
}

// The drawn position (256ths) a body at raw (256ths) is drawn at, held as a finger holds it: the rubber band past an end.
static b64 scroll_band256(const struct ui_scroll address_to s, b64 raw)
{
    struct ui_body b = s->body;

    ui_body_fit(&b, (b64)(s->view > 0 ? s->view : 0) << UI_Q, (b64)(s->content > 0 ? s->content : 0) << UI_Q);
    b.raw = raw << (UI_Q - 8), b.state = BODY_TRACK;
    return ui_body_drawn(&b) >> (UI_Q - 8);
}

static b32 scroll_gliding(const struct ui_scroll address_to s)
{
    return s->body.state == BODY_COAST || s->body.state == BODY_NOTCH;
}

static b32 scroll_back(const struct ui_scroll address_to s)
{
    return s->body.state == BODY_SPRING;
}

static b32 scroll_dragging(const struct ui_scroll address_to s)
{
    return s->body.state == BODY_TRACK;
}

// A trackpad's hand holds the region (its precise input moves it one for one, and it is let go by its release).
static b32 scroll_hand(const struct ui_scroll address_to s)
{
    return s->precise && s->body.state == BODY_TRACK;
}

static b64 scroll_most256(const struct ui_scroll address_to s)
{
    return (b64)ui_scroll_most(s) * UI_SCROLL_SCALE;
}

// The region's frames at the kit's clock, a millisecond a frame, as the kit steps them: answers how many it took to stop, at most frames.
static int scroll_run(const struct ui address_to ui, struct ui_scroll address_to s, b64 address_to now, int frames)
{
    int k = 0;

    for (; k < frames; k++)
    {
        *now += 1000000ll;
        ui_kit.now = *now;
        if (!ui_scroll_frame(ui, s))
            break;
    }
    return k;
}

/*
        A flick is the same flick at any scale: a finger's speed is in device pixels, and so is the list, so the throw's time
        constant is the list's length in logical pixels (UI_FLING_NS_PER_PX a logical pixel). It was the list's device length,
        which doubled the throw at 200 percent: the same flick of 1000 logical pixels a second over a 3000 logical pixel list
        went twice as far in logical pixels.
*/
static void fling_scale_check(void)
{
    static const int scales[2] = {100, 200};
    b64 distance[2];

    for (int k = 0; k < 2; k++)
    {
        struct ui ui;
        struct ui_scroll s;
        int scale = scales[k];
        b64 now = 1000000000ll;

        memory_zero(&ui, sizeof(ui)), memory_zero(&s, sizeof(s));
        ui.scale = scale;
        s.scale = scale, s.content = 3000 * scale / 100, s.view = 400 * scale / 100;
        ui_kit.now = now;
        ui_scroll_drag(&s, 0);
        ui_scroll_let_go(&s, 1000 * scale / 100);
        scroll_run(&ui, &s, &now, 20000);
        distance[k] = scroll_at(&s) * 100 / ((b64)scale * UI_SCROLL_SCALE);
    }
    check("fling: the same flick of the same list goes the same logical distance at 100 and 200 percent (within 5 percent)",
          distance[0] > 0 && distance[1] > 0 && (distance[1] - distance[0]) * 20 <= distance[0] && (distance[0] - distance[1]) * 20 <= distance[0]);
}

/*
        A switch dragged sideways and then taken back by the compositor (a second finger, a long press for the right button):
        what the drag stretched springs home as a release's does, from the stretch it had, and no value changes.
*/
static void switch_cancel_check(struct ui address_to wui)
{
    static struct ui_control sw;
    b32 motion_was = ui_kit.motion;
    b64 bend;
    b32 on_was;

    memory_zero(&sw, sizeof(sw));
    sw.kind = UI_SWITCH, sw.x = 520, sw.y = 220, sw.w = 52, sw.h = 48, sw.label = "";
    sw.row_x = 10, sw.row_y = 220, sw.row_w = 580, sw.row_h = 48;
    ui_control_start(&sw);
    ui_kit.motion = 0;
    ui_kit.now += 1000000000ll;
    ui_control_draw(wui, &sw);
    on_was = sw.on;
    ui_kit_pointer(wui, &sw, 1, WINDOW_EVENT_BUTTON, 560, 244, true, 0);
    ui_kit_pointer(wui, &sw, 1, WINDOW_EVENT_MOTION, 620, 244, false, 0);
    bend = sw.bend;
    ui_kit_pointer(wui, &sw, 1, WINDOW_EVENT_BUTTON, 620, 244, false, WINDOW_KEY_CANCEL);
    check("kit switch: a press the compositor takes back springs the drag's stretch home, and changes nothing",
          bend > UI_REST_SCALE && sw.held_squish.live && sw.held_squish.amplitude[0] == bend && sw.on == on_was);
    ui_kit.motion = motion_was;
}

/*
        A code view under a finger: a drag of 5 pixels a report, 60 reports, is 300 pixels, and the view moves the lines they make.
        A report short of a line used to round to no line, and the drag scrolled nothing unless each report was a line or more.
*/
static void check_code_finger(struct ui address_to wui)
{
    static struct ui_text view;
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
    struct window_event move = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
    struct window_event up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
    int x = SCROLL_CODE_LEFT + 40, y = SCROLL_CODE_TOP + SCROLL_CODE_HEIGHT / 2;
    p64 before;
    long lines, want;

    scroll_setup(0);
    scroll_code(wui, &view, 0);
    before = view.top_line;
    ui_text_event(wui, &view, &press, x, y);
    for (int step = 1; step <= 60; step++)
        ui_text_event(wui, &view, &move, x, y - 5 * step);
    lines = (long)view.top_line - (long)before;
    want = 300 / (long)view.line_height;
    check("code view: a finger dragged 300 pixels up, in reports of 5, scrolls the lines that makes",
          view.line_height > 5 && lines >= want - 1 && lines <= want + 1);
    ui_text_event(wui, &view, &up, x, y - 300);
    ui_text_close(&view);
}

/*
        A finger flicked along a field and lifted: the throw is the kit's, and the app's ticks (ui_text_timeout, all the app
        asks) spend it. A field's throw nothing ticked stayed set, and kinetic.flying with it held back the actions bar.
*/
static void check_field_flick(struct ui address_to wui)
{
    static struct ui_text field;
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
    struct window_event move = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
    struct window_event up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
    int x = SCROLL_FIELD_LEFT + 40, y = SCROLL_FIELD_TOP + SCROLL_FIELD_HEIGHT / 2;
    positive now = 2000000000ull;
    b32 redraw = false, thrown;

    ui_field_open(&field, null, "alpha beta gamma");
    ui_text_draw(wui, &field, SCROLL_FIELD_LEFT, SCROLL_FIELD_TOP, SCROLL_FIELD_WIDTH, SCROLL_FIELD_HEIGHT, 0);
    ui_kit.now = now;
    ui_field_event(wui, &field, null, &press, x, y);
    for (int step = 1; step <= 10; step++)
    {
        ui_kit.now = now += 16000000ull;
        ui_field_event(wui, &field, null, &move, x, y - 40 * step);
    }
    ui_kit.now = now += 16000000ull;
    ui_field_event(wui, &field, null, &up, x, y - 400);
    thrown = ui_body_going(&field.sel.fling);
    for (int step = 1; step <= 150; step++)
        ui_kit.now = now += 16000000ull, (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_text_timeout(wui, &field, -1, &redraw));
    check("field flick: a flick along a field throws it, and the app's ticks spend the throw", thrown && !ui_body_going(&field.sel.fling));
    ui_text_close(&field);
}

/*
        The throw's last step moves the lines it still owes, and a tick that moves the view asks for the picture, the one that
        stops the throw too: the view is never left a step behind its own scroll.
*/
static void check_throw_last_step(struct ui address_to wui)
{
    static struct ui_text view;
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
    struct window_event move = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
    struct window_event up = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
    int x = SCROLL_CODE_LEFT + 40, y = SCROLL_CODE_TOP + SCROLL_CODE_HEIGHT / 2;
    positive now = 3000000000ull;
    b32 moved_unasked = false, moved = false;

    scroll_setup(0);
    scroll_code(wui, &view, 0);
    ui_kit.now = now;
    ui_text_event(wui, &view, &press, x, y);
    for (int step = 1; step <= 10; step++)
    {
        ui_kit.now = now += 16000000ull;
        ui_text_event(wui, &view, &move, x, y - 40 * step);
    }
    ui_kit.now = now += 16000000ull;
    ui_text_event(wui, &view, &up, x, y - 400);
    for (int step = 1; step <= 300 && ui_body_going(&view.sel.fling); step++)
    {
        p64 was = view.top_line;
        b32 redraw = false;

        ui_kit.now = now += 16000000ull, (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_text_timeout(wui, &view, -1, &redraw));
        if (view.top_line != was)
            moved = true, moved_unasked |= !redraw;
    }
    check("code view throw: it moves the lines, and each tick that moves them asks for the picture, the one that stops it too",
          moved && !moved_unasked && !ui_body_going(&view.sel.fling));
    ui_text_close(&view);
}

/*
        The wheel on a code view. A mouse's notch is three lines, the same at any rate and turned by natural direction; a trackpad's
        hundred small reports are the pixels they are (4 px each at 100 percent: 400 px, the lines that make), and no report is a line of
        its own; and the lift of a quick hand (WINDOW_WHEEL_STOP) throws the view on its speed, which the app's ticks then spend.
*/
static void check_code_wheel(struct ui address_to wui)
{
    static struct ui_text view;
    struct window_event notch = {WINDOW_EVENT_WHEEL, 0, -120, 0};
    struct window_event small = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE, -12, 0};
    struct window_event quick = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE, -60, 0};
    struct window_event lift = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP, 0, 0};
    int x = SCROLL_CODE_LEFT + 40, y = SCROLL_CODE_TOP + SCROLL_CODE_HEIGHT / 2;
    positive now = 4000000000ull;
    long three, five, back, small_lines, first_three, pixels_lines, thrown_lines = 0;
    b32 thrown, spent, redraw = false;

    scroll_setup(0);
    ui_kit.scroll_off = 0;
    scroll_code(wui, &view, 10);
    ui_text_event(wui, &view, &notch, x, y);
    three = (long)view.top_line - 10;
    for (int k = 0; k < 4; k++)
        ui_text_event(wui, &view, &notch, x, y);
    five = (long)view.top_line - 10;
    ui_kit.scroll_off = WINDOW_SCROLL_FLIP;
    for (int k = 0; k < 5; k++)
        ui_text_event(wui, &view, &notch, x, y);
    back = (long)view.top_line - 10;
    ui_kit.scroll_off = 0;
    check("code view wheel: a notch is three lines, five are fifteen, and natural direction takes them back", three == 3 && five == 15 && back == 0);
    ui_text_close(&view);

    scroll_setup(0);
    scroll_code(wui, &view, 5);
    ui_kit.now = now;
    for (int k = 0; k < 3; k++)
        ui_text_event(wui, &view, &small, x, y);
    first_three = (long)view.top_line - 5;
    for (int k = 3; k < 100; k++)
        ui_text_event(wui, &view, &small, x, y);
    small_lines = (long)view.top_line - 5;
    pixels_lines = 400 / (long)view.line_height;
    check("code view wheel: a hundred reports of 4 px are 400 px of lines, and the first three (12 px) are not three lines",
          view.line_height > 12 && first_three == 0 && small_lines >= pixels_lines - 1 && small_lines <= pixels_lines);
    ui_text_event(wui, &view, &lift, x, y);
    ui_text_close(&view);

    scroll_setup(0);
    scroll_code(wui, &view, 2);
    for (int k = 0; k < 12; k++)
    {
        ui_kit.now = now += 16000000ull;
        ui_text_event(wui, &view, &quick, x, y);
    }
    ui_kit.now = now += 16000000ull;
    ui_text_event(wui, &view, &lift, x, y);
    thrown = ui_body_going(&view.sel.fling);
    for (int step = 1; step <= 300 && ui_body_going(&view.sel.fling); step++)
    {
        p64 was = view.top_line;

        ui_kit.now = now += 16000000ull, (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_text_timeout(wui, &view, -1, &redraw));
        thrown_lines += (long)view.top_line - (long)was;
    }
    spent = !ui_body_going(&view.sel.fling) && thrown_lines > 3;
    check("code view wheel: the lift of a quick hand throws the view, and the app's ticks spend the throw", thrown && spent);
    ui_text_close(&view);

    /*
            Across. The view shows a line longer than its box (scroll_code_wide), so there is something across to turn to: Shift turns a
            notch three columns toward the lines' end, a sideways turn (b set, its amount positive to the right, as the compositor sends
            it) goes the way it is turned, and both stop at the lines' start and where the widest line in view ends at the box's far side
            with the caret's room past it. A view whose lines all fit, and a wrapped one, are not turned across at all.
    */
    {
        struct window_event across = {WINDOW_EVENT_WHEEL, WINDOW_KEY_SHIFT, -120, 0}, back = {WINDOW_EVENT_WHEEL, WINDOW_KEY_SHIFT, 120, 0};
        struct window_event right = {WINDOW_EVENT_WHEEL, 0, 120, 1}, left = {WINDOW_EVENT_WHEEL, 0, -120, 1};
        struct window_event zoomed = {WINDOW_EVENT_WHEEL, WINDOW_KEY_CONTROL, 120, 0};
        long moved_across, lines_down, cell, most, held, home, tilted, tilted_back, fits, wrapped;
        p32 wrapped_asks;
        int zoom;

        scroll_setup(0);
        scroll_code_wide(wui, &view, 10);
        cell = (long)view.cell;
        most = SCROLL_WIDE_COLUMNS * cell - (view.right - view.left) + 2 * wui->scale / 100;
        ui_text_event(wui, &view, &across, x, y);
        moved_across = (long)view.scroll_x;
        lines_down = (long)view.top_line - 10;
        for (int k = 0; k < 40; k++)
            ui_text_event(wui, &view, &across, x, y);
        held = (long)view.scroll_x;
        for (int k = 0; k < 40; k++)
            ui_text_event(wui, &view, &back, x, y);
        home = (long)view.scroll_x;
        ui_text_event(wui, &view, &right, x, y);
        tilted = (long)view.scroll_x;
        ui_text_event(wui, &view, &left, x, y);
        tilted_back = (long)view.scroll_x;
        zoom = view.zoom;
        ui_text_event(wui, &view, &zoomed, x, y);
        check("code view wheel: Shift turns a notch across (three columns), and no line moves; Control zooms the view", moved_across == 3 * cell &&
              lines_down == 0 && view.zoom == zoom + 1);
        check("code view wheel: a turn across stops where the widest line in view ends, the caret's room kept, and comes back to the lines' start",
              most > 3 * cell && held == most && home == 0);
        check("code view wheel: a sideways turn goes the way it is turned, right toward the lines' end (three columns a notch) and left back",
              tilted == 3 * cell && tilted_back == 0);
        ui_text_close(&view);

        scroll_setup(0);
        scroll_code(wui, &view, 10);
        ui_text_event(wui, &view, &across, x, y);
        ui_text_event(wui, &view, &right, x, y);
        fits = (long)view.scroll_x;
        ui_text_close(&view);
        scroll_setup(0);
        scroll_code_wide(wui, &view, 10);
        view.wrap = true;
        wrapped_asks = ui_text_event(wui, &view, &across, x, y);
        wrapped_asks |= ui_text_event(wui, &view, &right, x, y);
        wrapped = (long)view.scroll_x;
        check("code view wheel: a view whose lines all fit is not turned across, and a wrapped one is not either and says nothing moved",
              fits == 0 && wrapped == 0 && !wrapped_asks && (long)view.top_line == 10);
        ui_text_close(&view);
    }
}

/*
        The window's pointer is the kit's: the last motion's place (ui_event_context), kept through a press and a key, and the sends
        (ui_text_send, ui_field_send) take it, so a send is the explicit call at that place: the same answer and the same caret.
*/
static void check_kit_pointer_send(struct ui address_to wui)
{
    static struct ui_text a, b;
    struct window_event motion = {WINDOW_EVENT_MOTION, 0, SCROLL_FIELD_LEFT + 60, SCROLL_FIELD_TOP + SCROLL_FIELD_HEIGHT / 2};
    struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
    struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
    int x = SCROLL_FIELD_LEFT + 60, y = SCROLL_FIELD_TOP + SCROLL_FIELD_HEIGHT / 2;
    p32 sent, direct;
    b32 same;

    ui_field_open(&a, null, "alpha beta gamma");
    ui_field_open(&b, null, "alpha beta gamma");
    ui_text_draw(wui, &a, SCROLL_FIELD_LEFT, SCROLL_FIELD_TOP, SCROLL_FIELD_WIDTH, SCROLL_FIELD_HEIGHT, 0);
    ui_text_draw(wui, &b, SCROLL_FIELD_LEFT, SCROLL_FIELD_TOP, SCROLL_FIELD_WIDTH, SCROLL_FIELD_HEIGHT, 0);
    ui_event_context(&motion);
    check("kit pointer: a motion's place is the window's", ui_kit.pointer_x == x && ui_kit.pointer_y == y);
    ui_event_context(&press);
    check("kit pointer: a press carries no place and the pointer stays", ui_kit.pointer_x == x && ui_kit.pointer_y == y);
    a.focused = b.focused = true;
    sent = ui_field_send(wui, &a, null, &press);
    direct = ui_field_event(wui, &b, null, &press, x, y);
    same = sent == direct && a.doc.selections[a.doc.primary].active == b.doc.selections[b.doc.primary].active;
    ui_event_context(&release);
    ui_field_send(wui, &a, null, &release);
    ui_field_event(wui, &b, null, &release, x, y);
    check("kit pointer: ui_field_send is ui_field_event at the pointer's place, press and release", same);
    ui_text_close(&a);
    ui_text_close(&b);
}

/*
        A tab's mark is a dot at the far end of that item, drawn by the kit: two strips alike but for the mark on item 1 differ in
        the pixels of item 1 and nowhere else (the editor's file tabs with changes are this).
*/
/*
        A selection collapses on a key that does not extend it: a horizontal key to the end or the start the key points at (and forgets
        the caret's goal column), and a vertical key from the start or the end, and then it walks a line from there (doc_collapse).
*/
static void check_doc_collapse(void)
{
    static unsigned char text[] = "abc\ndef\n";
    struct doc doc;
    struct doc_selection address_to primary;

    doc_open(&doc, text, 8);
    doc_select_set(&doc, 1, 5);
    doc_move_horizontal(&doc, 0, 0, 0);
    primary = &doc.selections[doc.primary];
    check("doc collapse: a left key with a selection collapses it to its start, its goal forgotten",
          doc.selection_count == 1 && primary->active == 1 && primary->anchor == 1 && primary->goal == -1);
    doc_select_set(&doc, 1, 5);
    doc_move_horizontal(&doc, 1, 0, 0);
    primary = &doc.selections[doc.primary];
    check("doc collapse: a right key with a selection collapses it to its end", doc.selection_count == 1 && primary->active == 5 && primary->anchor == 5);
    doc_select_set(&doc, 1, 5);
    doc_move_vertical(&doc, -1, 0);
    primary = &doc.selections[doc.primary];
    check("doc collapse: an up key with a selection walks from its start, the line above the start's line", primary->active == 0 && primary->anchor == 0);
    doc_close(&doc);
}

/*
        The line ends of a piece (doc_piece_line_end, the one rule): a CR and its LF are one line end even across two pieces (the CR the
        last byte of one, the LF the first of the next), and a lone CR at a piece's end is a line end. A place between the two is on
        the CR's line, as a single piece has it.
*/
static void check_doc_crlf_pieces(void)
{
    struct doc doc;

    doc_open(&doc, (const p8 address_to)"a\r", 2);
    doc_replace(&doc, 2, 0, (const p8 address_to)"\nb", 2);
    check("doc line ends: a CR and its LF in two pieces are one line end",
          doc_line_count(&doc) == 2 && doc_line_start(&doc, 1) == 3 && doc_line_of(&doc, 2) == 0 && doc_line_of(&doc, 3) == 1);
    doc_close(&doc);
    doc_open(&doc, (const p8 address_to)"a\r", 2);
    doc_replace(&doc, 2, 0, (const p8 address_to)"b", 1);
    check("doc line ends: a lone CR at the end of a piece is a line end", doc_line_count(&doc) == 2 && doc_line_start(&doc, 1) == 2 && doc_line_of(&doc, 2) == 1);
    doc_close(&doc);
    doc_open(&doc, (const p8 address_to)"a\r\nb", 4);
    check("doc line ends: a CR and its LF in one piece are one line end", doc_line_count(&doc) == 2 && doc_line_start(&doc, 1) == 3 && doc_line_of(&doc, 2) == 0);
    doc_close(&doc);
}

/*
        The columns of a line are one walk (doc_cluster_measure): a wide character takes two, a column in its first half is the
        boundary before it and in its second half the one after, and past the line's end the line's end. Both directions agree.
*/
static void check_doc_columns(void)
{
    static unsigned char text[] = "a\xe4\xb8\xad" "b\n";
    struct doc doc;
    positive length = 6;

    doc_open(&doc, text, length);
    check("doc columns: a wide character takes two columns, and the byte after it is three columns on", doc_column(&doc, 4) == 3 && doc_column(&doc, 1) == 1);
    check("doc at column: a column in a wide character's first half is the boundary before it, in its second half the one after",
          doc_at_column(&doc, 0, 1) == 1 && doc_at_column(&doc, 0, 2) == 4);
    check("doc at column: past a line's end it is the line's end", doc_at_column(&doc, 0, 9) == 5);
    doc_close(&doc);
}

static void check_tabs_marks(struct ui address_to wui)
{
    static unsigned int plain[400 * 60], marked[400 * 60];
    static struct ui_control base, a, b;
    b32 inside = false, outside = false;
    int left, wide;
    unsigned int *kept_pixels = wui->pixels;
    int kept_pitch = wui->pitch, kept_width = wui->width, kept_height = wui->height;

    memory_zero(&base, sizeof(base));
    base.kind = UI_TABS, base.variant = UI_TABS_SEGMENTED, base.x = 0, base.y = 0, base.w = 400, base.h = UI_TABS_H, base.count = 3;
    base.items[0] = "one", base.items[1] = "two", base.items[2] = "three";
    ui_control_start(&base);
    a = base, b = base;
    b.marks = 2;
    ui_begin(wui, plain, 400, 400, 60);
    ui_control_draw(wui, &a);
    ui_begin(wui, marked, 400, 400, 60);
    ui_control_draw(wui, &b);
    left = ui_dev(wui, ui_tab_left(wui, &base, 1)), wide = ui_dev(wui, ui_tab_width(wui, &base, 1));
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 400; x++)
            if (plain[(positive)y * 400 + (positive)x] != marked[(positive)y * 400 + (positive)x])
            {
                if (x >= left && x < left + wide)
                    inside = true;
                else
                    outside = true;
            }
    check("tabs: a mark is a dot at the far end of its item, and the picture changes only in that item", inside && !outside);
    // The window's picture is the caller's again: the strip was drawn into this check's own buffers.
    wui->pixels = kept_pixels, wui->pitch = kept_pitch, wui->width = kept_width, wui->height = kept_height;
}

static void check_field_drag_drawn(struct ui address_to wui)
{
    static struct ui_text view;
    static struct window_row own;
    int drawn_moved = -1, largest = -1;

    scroll_setup(0);
    own.table = compositor.table, own.row = 1, own.door = -1;
    scroll_field_drawn(wui, &view, &own, SCROLL_FIELD_LEFT + SCROLL_FIELD_WIDTH + 10, &drawn_moved, &largest);
    check("field drag: a draw in the middle of a drag past the right edge does not reveal the point, and the view stays", drawn_moved == 0);
    check("field drag: drawn between the ticks, the view moves a few pixels a tick, not a quarter of the field", largest >= 0 && largest <= 4);
}

/*
        A fast finger's drag on a window and its lift, as the window is told them: the finger is led a frame ahead (the pointer a
        finger moves is where it will be, by a few pixels), and at the lift the lead is taken away, which tells the window where the
        hand is (the predict rows). That motion is a step back, and no step the hand made: it says so (WINDOW_KEY_SETTLE), and a
        window that reads the finger's velocity off its moves leaves it out (ui_kit_retract, ui_press_track). Without the flag the
        velocity at the lift was read through that last step back, which is the finger turning, and a flick threw nothing (read off a
        booted guest: the terminal's scrollback, 92 and 0 a second for a finger that moved at 2500 and 1250). The window here reads
        each event as it comes, as an idle program does: no move it is told steps back but the one that is flagged, and that one is told.
*/
static void check_flick_lift(void)
{
    struct windows address_to table;
    struct window_row own;
    struct window_event event;
    int last = -1, back = 0, motions = 0, settled = 0;

    scroll_setup(1);
    table = compositor.table;
    own.table = table, own.row = 1, own.door = -1;
    for (int step = 0; step <= 11; step++)
    {
        if (step < 11)
            touch_one(100, 60 + step * 12, false);
        else
            touch_one(100, 60 + 10 * 12, true);
        while (windows_event(&own, &event))
            if (event.type == WINDOW_EVENT_MOTION)
            {
                if (event.flags & WINDOW_KEY_SETTLE)
                    settled++;
                else
                    back += last >= 0 && event.b < last;
                if (!(event.flags & WINDOW_KEY_SETTLE))
                    last = event.b;
                motions++;
            }
    }
    check("flick: the lead's retraction at a fast finger's lift is told to the window, flagged as no step the hand made, and no other move steps back",
          motions >= 5 && settled >= 1 && back == 0);
}

// The velocity at the lift, with the lead's retraction coming as the finger's next report would (a report later, as it did on the
// guest: 8 ms on, then the lift): read as a place (flagged) it changes nothing, and read as a step it is the finger turning.
static void check_flick_retract(void)
{
    struct ui_press drag;
    positive start = 1000000000ull, ms = 1000000ull;
    long plain, flagged, unflagged;

    (ui_kit.now = (b64)(start), ui_press_track_start(&drag, 100));
    for (int k = 1; k <= 12; k++)
        (ui_kit.now = (b64)(start + (positive)k * 8 * ms), ui_press_track(&drag, 100 + 20 * k));
    plain = ui_press_velocity(&drag, start + 104 * ms + 200000ull);
    ui_kit_retract = true;
    (ui_kit.now = (b64)(start + 104 * ms), ui_press_track(&drag, 100 + 20 * 12 - 4));
    ui_kit_retract = false;
    flagged = ui_press_velocity(&drag, start + 104 * ms + 200000ull);
    (ui_kit.now = (b64)(start), ui_press_track_start(&drag, 100));
    for (int k = 1; k <= 12; k++)
        (ui_kit.now = (b64)(start + (positive)k * 8 * ms), ui_press_track(&drag, 100 + 20 * k));
    (ui_kit.now = (b64)(start + 104 * ms), ui_press_track(&drag, 100 + 20 * 12 - 4));
    unflagged = ui_press_velocity(&drag, start + 104 * ms + 200000ull);
    check("flick: a lead's retraction flagged as a place leaves the velocity at the lift as it was (within a fifth)",
          plain > 2000 && flagged * 5 >= plain * 4 && flagged * 5 <= plain * 6);
    check("flick: and the same step read as a move of the finger's takes most of it away, which is what the flag is for", unflagged * 3 < plain);
}

// A lift the device told late (an optical or resistive panel, a slow link, a busy compositor) throws what the finger was doing at its last
// report, less: as it was up to UI_FLING_STALE_NS after that report, then fallen by e for every UI_FLING_LATE_NS, and nothing past
// UI_FLING_GONE_NS (160 ms); and a finger that came to rest throws nothing however late or early it lifts. A lift 80 ms after the last report
// threw nothing (a flick on a Dell touch all-in-one stopped dead) before.
static void check_kit_late_lift(void)
{
    struct ui_press drag;
    positive start = 1000000000ull, ms = 1000000ull, last;
    long at10, at60, at100, at150, at300, at600, rested;
    static const int slowing[12] = {20, 16, 12, 8, 4, 2, 1, 0, 0, 0, 0, 0};
    int place = 100;

    (ui_kit.now = (b64)start, ui_press_track_start(&drag, 100));
    for (int k = 1; k <= 12; k++)
        (ui_kit.now = (b64)(start + (positive)k * 8 * ms), ui_press_track(&drag, 100 + 20 * k));
    last = start + 96 * ms;
    at10 = ui_press_velocity(&drag, last + 10 * ms);
    at60 = ui_press_velocity(&drag, last + 60 * ms);
    at100 = ui_press_velocity(&drag, last + 100 * ms);
    at150 = ui_press_velocity(&drag, last + 150 * ms);
    at300 = ui_press_velocity(&drag, last + 300 * ms);
    at600 = ui_press_velocity(&drag, last + 600 * ms);
    (ui_kit.now = (b64)start, ui_press_track_start(&drag, 100));
    for (int k = 1; k <= 12; k++)
        place += slowing[k - 1], (ui_kit.now = (b64)(start + (positive)k * 8 * ms), ui_press_track(&drag, place));
    rested = ui_press_velocity(&drag, start + 96 * ms + 150 * ms);
    string_format(log, "  kit late lift: a flick of 2500 px/s thrown at %b (10 ms late), %b (60), %b (100), %b (150), %b (300), %b (600); a finger come to rest throws %b\n",
                  (bipolar)at10, (bipolar)at60, (bipolar)at100, (bipolar)at150, (bipolar)at300, (bipolar)at600, (bipolar)rested);
    check("kit late lift: a lift up to UI_FLING_STALE_NS after the last report throws what the finger was doing", at10 > 2000 && at60 * 100 >= at10 * 99 && at60 * 100 <= at10 * 101);
    check("kit late lift: a lift 100 and 150 ms after it throws less, not nothing (about three quarters, then about a half)",
          at100 * 100 >= at10 * 55 && at100 * 100 <= at10 * 90 && at150 * 100 >= at10 * 25 && at150 * 100 <= at10 * 65 && at150 < at100);
    check("kit late lift: and a lift 300 ms after it, which is a finger that rested, throws nothing, as does one 600 ms after it", at300 == 0 && at600 == 0);
    check("kit late lift: a finger that came to rest throws nothing, lifted 150 ms after its last report", rested < UI_THROW_LEAST && rested > -UI_THROW_LEAST);
}

/*
    The throw's floor is UI_THROW_LEAST dp a second, in logical pixels, so at a scale of 200 it is twice as many device pixels. A drag of
    'step' device pixels every 'ms' milliseconds is a finger at step / ms thousand device pixels a second; the lift is 10 ms after the last
    report. Speeds are kept well away from the floor (the drag's smoothed speed settles a little under its rate).
*/
static b32 check_throw_flick(int scale, int step, int reports, long ms)
{
    struct ui_press drag;
    struct ui_body thrown;
    positive start = 1000000000ull, lift = start + (positive)reports * (positive)ms * 1000000ull + 10000000ull;

    (ui_kit.now = (b64)start, ui_press_track_start(&drag, 100));
    for (int k = 1; k <= reports; k++)
        (ui_kit.now = (b64)(start + (positive)k * (positive)ms * 1000000ull), ui_press_track(&drag, 100 + step * k));
    ui_kit.now = (b64)lift;
    memory_zero(&thrown, sizeof(thrown));
    ui_body_free(&thrown, ui_press_velocity(&drag, ui_press_now()), lift, scale, true);
    return ui_body_going(&thrown);
}

static void check_kit_throw_dp(void)
{
    // The floor is 300 dp a second: 325 device pixels a second (a flick of 13 px every 40 ms) throws at 100% and not at 200% (where it is 162 dp),
    // and 650 device pixels a second throws at 200%. 200 device pixels a second is under the floor at 100%.
    check("kit throw dp: a flick of 325 px a second is thrown at 100% (over the floor of 300 dp)", check_throw_flick(100, 13, 16, 40));
    check("kit throw dp: a flick of 325 px a second is not thrown at 200% (162 dp, under the floor of 300 dp)", !check_throw_flick(200, 13, 16, 40));
    check("kit throw dp: a flick of 650 px a second is thrown at 200% (325 dp, over the floor)", check_throw_flick(200, 26, 16, 40));
    check("kit throw dp: a flick of 200 px a second is not thrown at 100%", !check_throw_flick(100, 8, 16, 40));
    check("kit throw dp: the cap is 8000 dp a second at any scale (Android's maximum fling velocity), and the floor is UI_THROW_LEAST dp",
          ui_throw_most(200) == 16000 && ui_throw_most(100) == 8000 && !ui_throws(599, 200) && ui_throws(600, 200) && !ui_throws(299, 100) && ui_throws(300, 100) && ui_throws(-300, 100));
}

/*
    A touchpad's precise motion stops and its fingers lift p ms after the last report (WINDOW_WHEEL_STOP). A finger still for more than
    UI_HAND_GAP_NS (the one gate, 60 ms: the pause was 40 ms of its own) before its lift throws nothing; one that lifts within it throws what it was doing.
*/
static b32 check_pause_lift(long pause_ms)
{
    struct ui ui;
    struct ui_scroll region;
    struct window_event report = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE, -20, 0};
    struct window_event stop = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP, 0, 0};
    b64 clock = 40000000000ll, last;

    memory_zero(&ui, sizeof(ui));
    memory_zero(&region, sizeof(region));
    ui.scale = 100;
    region.x = 100, region.y = 10, region.height = 400, region.view = 400, region.content = 4000, region.scale = 100;
    ui_kit.test_clock = clock, ui_kit.now = clock, ui_kit.busy = 0;
    ui_scroll_set(&region, 1000);
    for (int k = 0; k < 8; k++)
    {
        clock += 16000000ull, ui_kit.test_clock = ui_kit.now = clock;
        ui_wheel_turn(&ui, &region, &report);
    }
    last = clock;
    ui_kit.test_clock = ui_kit.now = last + (b64)pause_ms * 1000000ull;
    ui_wheel_turn(&ui, &region, &stop);
    return scroll_gliding(&region) != 0;
}

/*
    A fling at 125 Hz: a report every 8 ms, 10 px each (1250 px a second), 24 reports, the finger lifting 4 ms after the last. Delivered
    as they came (each at its own dispatch time), or in batches of four handed over together at the batch's dispatch time (2 ms after
    its last report, as a busy program takes them). Each report carries its device stamp (ui_kit.stamp), the time it was made. The
    velocity at the lift is the same for both, because a drag reads the stamp (ui_press_now), not the dispatch time.
*/
static long check_batch_fling(int batched)
{
    struct ui_press drag;
    positive start = 1000000000ull, arrive, dispatch, last = 0;

    static positive at[25];

    ui_kit.stamp = 0, ui_kit.busy = 0;
    (ui_kit.now = (b64)start, ui_kit.test_clock = (b64)start, ui_kit.stamp = (b64)start, ui_press_track_start(&drag, 100));
    // Uneven: the reports come 5 ms and 11 ms apart in turn (a panel's jitter), so a batch spread evenly is not their times.
    arrive = start;
    for (int k = 1; k <= 24; k++)
        arrive += (k & 1) ? 5000000ull : 11000000ull, at[k] = arrive;
    for (int k = 1; k <= 24; k++)
    {
        // A batch is handed over together when its last report is in, at that report's time plus 2 ms; each of its reports
        // is then followed as it was made (its stamp), and the kit's own time is the batch's.
        if (batched && (k - 1) % 4 != 3 && k != 24)
            continue;
        if (batched)
        {
            for (int j = k - (k - 1) % 4; j <= k; j++)
            {
                dispatch = at[k] + 2000000ull;
                ui_kit.now = (b64)dispatch, ui_kit.test_clock = (b64)dispatch, ui_kit.stamp = (b64)at[j];
                ui_press_track(&drag, 100 + 10 * j);
            }
        }
        else
        {
            ui_kit.now = (b64)at[k], ui_kit.test_clock = (b64)at[k], ui_kit.stamp = (b64)at[k];
            ui_press_track(&drag, 100 + 10 * k);
        }
        last = at[k];
    }
    ui_kit.now = (b64)(last + 4000000ull), ui_kit.test_clock = ui_kit.now, ui_kit.stamp = ui_kit.now;
    {
        long speed = ui_press_velocity(&drag, (positive)ui_kit.stamp);

        ui_kit.stamp = 0, ui_kit.test_clock = 0;
        return speed;
    }
}

static void check_kit_batch_stamps(void)
{
    long single = check_batch_fling(0), batch = check_batch_fling(1);
    long gap = single > batch ? single - batch : batch - single;

    string_format(log, "  kit batch stamps: a 125 Hz fling at %b px a second as delivered one by one, %b as delivered in fours\n",
                  (bipolar)single, (bipolar)batch);
    check("kit batch stamps: a fling at 125 Hz with jitter is read as a fling, not a stop (800 to 1500 px a second, about its 1250)",
          single > 800 && single < 1500);
    check("kit batch stamps: the same fling delivered in batches of four has the same velocity (the device's stamps, not the dispatch)",
          gap * 100 <= single * 1);
}

/*
    The touch trace (window.c): off, a trace line writes nothing; on, it is one line of the shape written; the cap holds (a line that would
    pass 1 MiB is not written). A release's reason is no longer named: the throw rows (kit throw dp, kit late lift, a finger that rested)
    check the same tests.
*/
static unsigned int trace_lines;
static char trace_text[400];

static b32 trace_capture(const char address_to line, positive length)
{
    trace_lines++;
    for (positive at = 0; at < length && at < sizeof(trace_text) - 1; at++)
        trace_text[at] = line[at], trace_text[at + 1] = 0;
    return true;
}

// Whether two NUL-terminated strings are the same text.
static b32 trace_same(const char address_to a, const char address_to b)
{
    positive at = 0;

    for (; a[at] && b[at]; at++)
        if (a[at] != b[at])
            return false;
    return a[at] == b[at];
}

static void check_touch_trace(void)
{
    b32 same;
    const char address_to want = "kit lift: speed 12 px/s, thrown\n";

    window_trace_out = trace_capture;
    trace_lines = 0;
    window_trace_live = 0;
    WINDOW_TRACE_START(); WINDOW_TRACE_PART("kit lift: speed %b px/s, ", (bipolar)12); window_trace_word("thrown"); WINDOW_TRACE_END();
    check("touch trace: off, a line writes nothing (one branch, and no sink call)", trace_lines == 0);
    window_trace_live = 1;
    WINDOW_TRACE_START(); WINDOW_TRACE_PART("kit lift: speed %b px/s, ", (bipolar)12); window_trace_word("thrown"); WINDOW_TRACE_END();
    same = trace_lines == 1 && trace_same(trace_text, want);
    check("touch trace: on, the line is written in the shape the format gives, with its newline", same);
    check("touch trace: the cap holds: a line that would pass 1 MiB is not written, and one that fits is",
          !window_trace_room(WINDOW_TRACE_CAP - 5, 10) && window_trace_room(WINDOW_TRACE_CAP - 10, 10) && !window_trace_room(-1, 5) &&
          !window_trace_room(WINDOW_TRACE_CAP, 1));
    window_trace_live = 0;
    window_trace_out = window_trace_write;
    ui_kit.test_clock = 0;
}

static void check_kit_pause_rule(void)
{
    b32 soon = check_pause_lift(10), edge = check_pause_lift(60), later = check_pause_lift(80), longer = check_pause_lift(160);

    check("kit pause rule: a pad whose fingers lift 10 ms after its last move throws (its speed is what it was doing)", soon);
    check("kit pause rule: a hold of exactly the gate, 60 ms, has not passed it, so it still throws", edge);
    check("kit pause rule: a pad that stops and holds 80 ms, past the gate, before its lift throws nothing", !later);
    check("kit pause rule: and one that holds 160 ms throws nothing", !longer);
    ui_kit.test_clock = 0;
}

static void check_select_scroll(struct ui address_to wui)
{
    static struct ui_text view;
    static struct window_row own;
    struct scroll_answer a;

    /* Code view, mouse: up past the box (inside the window), up past the window, down past the box, down past the window. */
    scroll_setup(0);
    own.table = compositor.table, own.row = 1, own.door = -1;
    scroll_code_mouse(wui, &view, &own, SCROLL_CODE_TOP - 25, &a);
    check("scroll: a mouse held above a code view's top edge scrolls it up, and the selection follows the text",
          a.top < 10 && a.start / SCROLL_LINE == (p64)a.top);
    scroll_code_mouse(wui, &view, &own, -15, &a);
    check("scroll: a mouse held above the window scrolls a code view up as well", a.top < 10 && a.start / SCROLL_LINE == (p64)a.top);
    scroll_code_mouse(wui, &view, &own, SCROLL_CODE_TOP + SCROLL_CODE_HEIGHT + 25, &a);
    check("scroll: a mouse held below a code view's bottom edge scrolls it down, and the selection follows",
          a.top > 10 && a.finish / SCROLL_LINE >= (p64)a.top);
    scroll_code_mouse(wui, &view, &own, 215, &a);
    check("scroll: a mouse held below the window scrolls a code view down as well", a.top > 10 && a.finish / SCROLL_LINE >= (p64)a.top);

    /* Code view, finger: the same four, the finger held for its word first. */
    scroll_setup(1);
    own.table = compositor.table, own.row = 1, own.door = -1;
    scroll_code_touch(wui, &view, &own, SCROLL_CODE_TOP - 25, &a);
    check("scroll: a finger held above a code view's top edge scrolls it up", a.top < 10 && a.start / SCROLL_LINE == (p64)a.top);
    scroll_code_touch(wui, &view, &own, -15, &a);
    check("scroll: a finger held above the window scrolls a code view up as well", a.top < 10 && a.start / SCROLL_LINE == (p64)a.top);
    scroll_code_touch(wui, &view, &own, SCROLL_CODE_TOP + SCROLL_CODE_HEIGHT + 25, &a);
    check("scroll: a finger held below a code view's bottom edge scrolls it down", a.top > 10 && a.finish / SCROLL_LINE >= (p64)a.top);
    scroll_code_touch(wui, &view, &own, 215, &a);
    check("scroll: a finger held below the window scrolls a code view down as well", a.top > 10 && a.finish / SCROLL_LINE >= (p64)a.top);

    /* A field (the find bar, the kit's fields, the settings' fields), mouse: right past its box, left past its box. */
    scroll_setup(0);
    own.table = compositor.table, own.row = 1, own.door = -1;
    scroll_field_mouse(wui, &view, &own, 0, 220, &a);
    check("scroll: a mouse held past a field's right edge scrolls it right, the selection growing", a.scroll > a.before && a.finish > 5);
    check("scroll: and the motion does not jump the field: the tick moves it a step at a time", a.moved == a.before);
    scroll_field_mouse(wui, &view, &own, 0, 220, &a);
    check("scroll: and it keeps scrolling while the mouse is held past the edge", a.scroll > a.moved);
    scroll_field_mouse(wui, &view, &own, 1, 3, &a);
    check("scroll: a mouse held past a field's left edge scrolls it left", a.scroll < a.before && a.start < a.finish);
    check("scroll: and it keeps scrolling while held there, a step at a time", a.scroll < a.moved);
    check("scroll: and the motion does not jump the field left either", a.moved == a.before);

    /* A field, finger: the same two, the finger held on the field for its word first. */
    scroll_setup(1);
    own.table = compositor.table, own.row = 1, own.door = -1;
    scroll_field_touch(wui, &view, &own, 0, 220, &a);
    check("scroll: a finger held past a field's right edge scrolls it right", a.scroll > a.before && a.finish > 5);
    check("scroll: and the finger's motion does not jump the field either", a.moved == a.before);
    check("scroll: and it keeps scrolling while the finger is held past the edge", a.scroll > a.moved);
    scroll_field_touch(wui, &view, &own, 1, 3, &a);
    check("scroll: a finger held past a field's left edge scrolls it left", a.scroll < a.before && a.start < a.finish);
}

/*
        The screen's edges (compositor_swipe, compositor_chip_*). A finger that lands in a band at the top or the bottom of a screen is
        the system's until it is decided: a swipe up from the bottom opens the launcher beside the finger, a swipe down from a window's
        top band shows its controls (close, minimise, maximise) over the band, and a finger that is no swipe, in time or in travel, is
        the window's as if the band were not there. The judge (ui_edge_judge) and the bands (ui_edge_band_side) are checked on their own
        first. Then the compositor's state, and what each window was told, for synthetic fingers on a 256 by 256 screen (one unit of
        the touch screen a pixel, as touch_one gives it).
*/
static void edge_setup(int motion)
{
    struct windows address_to table;
    void address_to chip_cover = compositor.chip.cover, address_to chip_scratch = compositor.chip.scratch;

    layout_setup(256, 1);
    table = compositor.table;
    table->motion = (unsigned int)motion;
    /* Nothing reads these rows' events but the rows, so each starts with the ring empty (WINDOWS_EVENTS, a full ring drops a post). */
    for (unsigned int row = 0; row < WINDOWS; row++)
        table->event_tail[row] = table->event_head[row];
    /* The chip's memory is the compositor's, mapped once: layout_setup clears the compositor, so it is kept. */
    compositor.chip.cover = chip_cover, compositor.chip.scratch = chip_scratch;
    compositor.predict_off[COMPOSITOR_PREDICT_TOUCH] = 1;
    compositor.spots_on = 1;
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    if (system_call_2(syscall(pipe2), (positive)wheel_pipe, O_NONBLOCK))
    {
        check("edge: the input pipe is made", false);
        return;
    }
    compositor.inputs[0] = wheel_pipe[0];
    compositor.input_count = 1;
    compositor.touchscreen[0] = 1;
    compositor.screen_touch[0] = (struct compositor_touch_screen){0};
    for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        compositor.screen_touch[0].id[slot] = compositor.screen_touch[0].was[slot] = -1;
    compositor.screen_touch[0].slots = 1;
    compositor.screen_touch[0].range[1] = compositor.screen_touch[0].range[3] = 1020;
    touch_clock = 1000000;
    touch_down = 0;
    compositor.launcher_asked = 0;
}

// One report of one finger at x, y (lifted when lift), us microseconds after the last report.
static void edge_at(int x, int y, b32 lift, positive us)
{
    const int finger[1][4] = {{0, lift ? -1 : 1, x, y}};

    touch_clock += us;
    touch_report(1, finger);
}

// The finger moved from x0, y0 to x1, y1 in steps reports, each us microseconds after the last.
static void edge_drag(int x0, int y0, int x1, int y1, int steps, positive us)
{
    for (int i = 1; i <= steps; i++)
        edge_at(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, 0, us);
}

// A swipe down from a window's top band at x (a window 240 wide at 0, 0) shows its controls: the finger is lifted over the strip's middle.
static void edge_show_controls(void)
{
    edge_at(100, 4, 0, 10000);
    edge_drag(100, 4, 100, 56, 6, 10000);
    edge_at(100, 56, 1, 10000);
}

static void check_compositor_edges(void)
{
    struct windows address_to table;
    unsigned int head;
    long value = -1;
    int band_rect[4] = {0, 0, 256, 256}, second_rect[4] = {256, 0, 1920, 1080};

    // The moon and the tasks at the band's end, so the bottom edge's middle, where these swipes start, is no pill's.
    taskbar.moon_zone = TASKBAR_ZONE_END, taskbar.tasks_zone = TASKBAR_ZONE_END;
    taskbar_changed();
    /* The Settings rows' words are the choices the compositor reads: off, windows, and the launcher (left) or the volume popup (right). */
    check("edge settings: the Left edge and Right edge words are the choices the compositor reads",
          compositor_setting_kept(SETTING_TOUCH_LEFT_PATH, "windows", &value) && value == COMPOSITOR_SIDE_WINDOWS &&
              compositor_setting_kept(SETTING_TOUCH_LEFT_PATH, "launcher", &value) && value == COMPOSITOR_SIDE_ACT &&
              compositor_setting_kept(SETTING_TOUCH_RIGHT_PATH, "volume", &value) && value == COMPOSITOR_SIDE_ACT &&
              (compositor_setting_kept(SETTING_TOUCH_RIGHT_PATH, "off", &value), value == COMPOSITOR_SIDE_OFF));

    /* The judge and the bands, alone. */
    check("edge judge: a straight swipe up from the bottom band, 60 px, is a swipe",
          ui_edge_judge(UI_EDGE_BOTTOM, 100, 250, 100, 190, 8, 48) == UI_EDGE_SWIPED);
    check("edge judge: 30 px up is not yet decided", ui_edge_judge(UI_EDGE_BOTTOM, 100, 250, 100, 220, 8, 48) == UI_EDGE_UNDECIDED);
    check("edge judge: a curve out of the cone (30 px up and 30 across) fails", ui_edge_judge(UI_EDGE_BOTTOM, 100, 250, 130, 220, 8, 48) == UI_EDGE_FAILED);
    check("edge judge: a diagonal of about 25 degrees is still a swipe", ui_edge_judge(UI_EDGE_BOTTOM, 100, 250, 123, 200, 8, 48) == UI_EDGE_SWIPED);
    check("edge judge: back down past the slop fails it", ui_edge_judge(UI_EDGE_BOTTOM, 100, 250, 100, 258, 8, 48) == UI_EDGE_FAILED);
    check("edge judge: sideways out of the band fails it", ui_edge_judge(UI_EDGE_BOTTOM, 100, 250, 150, 252, 8, 48) == UI_EDGE_FAILED);
    check("edge judge: a top swipe down and a right-hand swipe left are swipes too",
          ui_edge_judge(UI_EDGE_TOP, 100, 0, 100, 60, 8, 48) == UI_EDGE_SWIPED && ui_edge_judge(UI_EDGE_RIGHT, 250, 100, 190, 100, 8, 48) == UI_EDGE_SWIPED);
    check("edge judge: a control's finger is judged by travel alone (3 px is nothing, 9 px is a drag)",
          ui_edge_judge(UI_EDGE_CONTROL, 100, 20, 103, 20, 8, 48) == UI_EDGE_UNDECIDED && ui_edge_judge(UI_EDGE_CONTROL, 100, 20, 109, 20, 8, 48) == UI_EDGE_FAILED);
    check("edge band: the nearest edge within 12 px, and none in the middle",
          ui_edge_band_side(5, 128, band_rect, 12) == UI_EDGE_LEFT && ui_edge_band_side(128, 250, band_rect, 12) == UI_EDGE_BOTTOM &&
              ui_edge_band_side(128, 3, band_rect, 12) == UI_EDGE_TOP && ui_edge_band_side(3, 250, band_rect, 12) == UI_EDGE_LEFT &&
              ui_edge_band_side(128, 128, band_rect, 12) == UI_EDGE_NONE);
    check("edge band: a second screen's own edges (its left at 256, its bottom at 1080)",
          ui_edge_band_side(261, 500, second_rect, 12) == UI_EDGE_LEFT && ui_edge_band_side(1000, 1075, second_rect, 12) == UI_EDGE_BOTTOM &&
              ui_edge_band_side(1000, 500, second_rect, 12) == UI_EDGE_NONE);

    /* A swipe up over a window that does not cover the band: the launcher opens, and the window gets nothing. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 150, 256, 106);
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    check("edge: a finger in the bottom band is the system's until it is decided, and its mark is shown",
          compositor.swipe.state == COMPOSITOR_SWIPE_UNDECIDED && compositor.spot[0].on == 1 && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0);
    edge_drag(128, 252, 128, 200, 6, 10000);
    check("edge: a swipe up 52 px opens the launcher, and the window under it gets no touch",
          compositor.launcher_asked == 1 && compositor.swipe.state == COMPOSITOR_SWIPE_SWIPED && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0 &&
              touch_seen(1, head, WINDOW_EVENT_MOTION, -1, null) == 0);
    edge_at(128, 200, 1, 10000);
    check("edge: lifted, the swipe is over and the next finger is the screen's again", compositor.swipe.state == COMPOSITOR_SWIPE_NONE &&
              compositor.chip.kind == COMPOSITOR_CHIP_NONE);

    /* A tap in the band with no travel is the window's click, where it landed. A window at the screen's bottom has a finger's grip
       there, so the window is maximized (no grip at the screen's edges). */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor.arranged[1] = COMPOSITOR_MAXIMIZED;
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    edge_at(128, 252, 1, 10000);
    check("edge: a tap in the band is the window's: its press and release where it landed, and no launcher",
          touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 2 && compositor.launcher_asked == 0 && compositor.swipe.state == COMPOSITOR_SWIPE_NONE);

    /* A slow drag up from the band: no swipe in time, so the window has the press, and the drag. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor.arranged[1] = COMPOSITOR_MAXIMIZED;
    head = table->event_head[1];
    edge_at(128, 252, 0, 100000);
    for (int i = 1; i <= 6; i++)
        edge_at(128, 252 - i * 2, 0, 100000);
    check("edge: a slow drag from the band is the window's: it has the press, then its motion, and no launcher",
          touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) >= 1 && touch_seen(1, head, WINDOW_EVENT_MOTION, -1, null) >= 1 &&
              compositor.launcher_asked == 0 && compositor.swipe.state == COMPOSITOR_SWIPE_NONE);
    edge_at(128, 240, 1, 100000);

    /* A finger that stays in the band past its time, with no report at all, is the window's when the tick comes. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor.arranged[1] = COMPOSITOR_MAXIMIZED;
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    check("edge: a finger in the band is due at its deadline, for the tick", compositor.swipe.state == COMPOSITOR_SWIPE_UNDECIDED &&
              compositor_touch_due() == compositor.swipe.deadline);
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
    check("edge: at its time, still, the window has the press (the kit's long press comes after it)",
          touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 1 && compositor.swipe.state == COMPOSITOR_SWIPE_NONE && compositor.launcher_asked == 0);
    edge_at(128, 252, 1, 10000);

    /* A second finger before the swipe is decided cancels it: the window has the press and its cancel, and no launcher. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor.arranged[1] = COMPOSITOR_MAXIMIZED;
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    {
        const int two[2][4] = {{0, 1, 128, 252}, {1, 2, 60, 200}};
        const int lift[2][4] = {{0, -1, 0, 0}, {1, -1, 0, 0}};

        touch_clock += 10000;
        touch_report(2, two);
        touch_clock += 10000;
        touch_report(2, lift);
    }
    check("edge: a second finger cancels the swipe: the window has the press and the cancel of it, and no launcher",
          touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) >= 2 && compositor.launcher_asked == 0 && compositor.swipe.state == COMPOSITOR_SWIPE_NONE);

    /* A finger that lands while a drag is under way is not the system's. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 150, 256, 106);
    compositor.drag = 1;
    edge_at(128, 252, 0, 10000);
    check("edge: a finger in the band while a window is being dragged is the window's, and no swipe begins", compositor.swipe.state == COMPOSITOR_SWIPE_NONE);
    compositor.drag = COMPOSITOR_NONE;
    edge_at(128, 252, 1, 10000);

    /* A left-hand swipe is the window's: its press and its motion, and no chip. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor.arranged[1] = COMPOSITOR_MAXIMIZED;
    head = table->event_head[1];
    edge_at(5, 100, 0, 10000);
    edge_drag(5, 100, 60, 100, 6, 10000);
    check("edge: a swipe in from the left edge is the window's, as a touch: no system gesture, no controls",
          touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) >= 1 && touch_seen(1, head, WINDOW_EVENT_MOTION, -1, null) >= 1 &&
              compositor.swipe.state == COMPOSITOR_SWIPE_NONE && compositor.chip.kind == COMPOSITOR_CHIP_NONE && compositor.launcher_asked == 0);
    edge_at(60, 100, 1, 10000);

    /* A window away from the screen's top: its own top band is where the controls come from, not the screen's. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 60, 240, 100);
    head = table->event_head[1];
    edge_at(100, 64, 0, 10000);
    edge_drag(100, 64, 100, 116, 6, 10000);
    check("edge: a swipe down from the top band of a window that is not at the screen's top shows its controls, and the window gets no touch",
          compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.row == 1 && touch_seen(1, head, WINDOW_EVENT_MOTION, -1, null) == 0 &&
              touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0);
    edge_at(100, 116, 1, 10000);

    /* A swipe down from a window's top band shows its controls over the band, and the window gets no touch. The window has a tab. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 240, 100);
    compositor.asks[1] &= ~WINDOW_NO_TAB;
    head = table->event_head[1];
    edge_at(100, 4, 0, 10000);
    edge_drag(100, 4, 100, 56, 6, 10000);
    check("edge: a swipe down from a window's top band shows its controls, and the window gets no touch",
          compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.row == 1 && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0 &&
              touch_seen(1, head, WINDOW_EVENT_MOTION, -1, null) == 0);
    check("edge: the controls are three cells, 120 px wide, centred over the top band at 6 px", compositor.chip.w == 120 &&
              compositor.chip.rel_x == 60 && compositor.chip.rel_y == 6 && compositor.chip.h == 32);
    edge_at(100, 56, 1, 10000);
    check("edge: lifted, the controls stay up until they fade", compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.swipe.state == COMPOSITOR_SWIPE_NONE);

    /* Each control, tapped: close, minimise, maximise. The window that is put away has a tab, which is what brings it back. */
    {
        int cells[3] = {80, 120, 160};

        for (int cell = 0; cell < 3; cell++)
        {
            edge_setup(0);
            table = compositor.table;
            layout_show(1, 0, 0, 240, 100);
            compositor.asks[1] &= ~WINDOW_NO_TAB;
            edge_show_controls();
            edge_at(cells[cell], 20, 0, 10000);
            edge_at(cells[cell], 20, 1, 10000);
            if (cell == 0)
                check("edge: a tap on the close control closes the window, and the controls go", (compositor.state[1] & WINDOW_CLOSING) != 0 &&
                          compositor.chip.kind == COMPOSITOR_CHIP_NONE);
            else if (cell == 1)
                check("edge: a tap on the minimise control puts the window away, and the controls go",
                      (compositor.state[1] & WINDOW_MINIMIZED) != 0 && compositor.chip.kind == COMPOSITOR_CHIP_NONE);
            else
                check("edge: a tap on the maximise control maximises the window, and the controls go",
                      compositor.arranged[1] == COMPOSITOR_MAXIMIZED && compositor.chip.kind == COMPOSITOR_CHIP_NONE);
        }
    }

    /* A window that asked for no tab has no minimise control, since nothing would bring it back: its controls are close and maximise,
       two cells centred over its top band (80 to 160), and a tap on the second maximises it. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 240, 100);
    edge_show_controls();
    check("edge: a window with no tab has two controls, close and maximise, 80 px wide and centred over its top band",
          compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.cells == 2 && compositor.chip.w == 80 && compositor.chip.rel_x == 80 &&
              compositor_chip_cell(130, 20) == 2 && compositor_chip_cell(100, 20) == 0);
    edge_at(140, 20, 0, 10000);
    edge_at(140, 20, 1, 10000);
    check("edge: a tap on its second control maximises it and puts nothing away, and the controls go",
          compositor_visible(1) && !(compositor.state[1] & WINDOW_MINIMIZED) && compositor.arranged[1] == COMPOSITOR_MAXIMIZED &&
              compositor.chip.kind == COMPOSITOR_CHIP_NONE);

    /* A drag that starts on a control is the window's, from its top band: the window moves with the finger. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 240, 100);
    edge_show_controls();
    edge_at(160, 20, 0, 10000);
    edge_drag(160, 20, 160, 50, 3, 10000);
    check("edge: a drag that starts on a control is the window's drag, and the window moves with the finger",
          compositor.drag == 1 && compositor.y[1] > 0);
    edge_at(160, 50, 1, 10000);

    /* The controls hold for 3 s, then fade out in 0.3 s. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 240, 100);
    edge_show_controls();
    {
        positive shown = compositor.chip.shown;

        compositor_chip_tick(shown + 2900000000ull);
        check("edge: the controls hold for 3 s", compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.alpha == 255);
        compositor_chip_tick(shown + 3150000000ull);
        check("edge: then they fade", compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.alpha > 0 && compositor.chip.alpha < 255);
        compositor_chip_tick(shown + 3310000000ull);
        check("edge: and are gone after the fade", compositor.chip.kind == COMPOSITOR_CHIP_NONE);
    }

    /* The controls slide in over 180 ms with the kit's ease, unless motion is reduced, when they are there at once. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 240, 100);
    edge_show_controls();
    {
        positive shown = compositor.chip.shown;
        int at_start = compositor.chip.slide;

        compositor_chip_tick(shown + 90000000ull);
        check("edge: the controls slide in: a little above their place at its start, nearer at the middle",
              at_start == compositor.chip.h && compositor.chip.slide > 0 && compositor.chip.slide < at_start);
        compositor_chip_tick(shown + 200000000ull);
        check("edge: and are in their place at its end", compositor.chip.slide == 0);
    }
    edge_setup(1);
    table = compositor.table;
    layout_show(1, 0, 0, 240, 100);
    edge_show_controls();
    check("edge: with motion reduced the controls are in their place at once", compositor.chip.kind == COMPOSITOR_CHIP_CONTROLS && compositor.chip.slide == 0);

    /* A full-screen window (a client's set_fullscreen, or a placement's): the first swipe into it is its own, with an indicator; a second
       within 2 s is the system's; a later one is the window's again. A maximised window that is not full-screen takes the first swipe at once. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor_fullscreen_row(1);
    check("edge fullscreen: a client's fullscreen request marks the window full-screen, and it is maximised",
          compositor.fullscreen[1] && compositor.arranged[1] == COMPOSITOR_MAXIMIZED);
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor.arranged[1] = COMPOSITOR_MAXIMIZED;
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    edge_drag(128, 252, 128, 200, 6, 10000);
    check("edge fullscreen: an ordinary maximised window takes the first swipe at once: the launcher opens, and it gets nothing",
          compositor.launcher_asked == 1 && compositor.chip.kind != COMPOSITOR_CHIP_HINT && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0);
    edge_at(128, 200, 1, 10000);
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 0, 256, 256);
    compositor_fullscreen_row(1);
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    edge_drag(128, 252, 128, 200, 6, 10000);
    check("edge: the first swipe into a full-screen window is the window's, with an indicator, and no launcher",
          compositor.chip.kind == COMPOSITOR_CHIP_HINT && compositor.launcher_asked == 0 && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) >= 1 &&
              touch_seen(1, head, WINDOW_EVENT_MOTION, -1, null) >= 1);
    edge_at(128, 200, 1, 10000);
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    edge_drag(128, 252, 128, 200, 6, 10000);
    check("edge: a second swipe within 2 s is the system's: the launcher opens, and the window gets nothing",
          compositor.launcher_asked == 1 && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0);
    edge_at(128, 200, 1, 10000);
    touch_clock += 3000000;
    head = table->event_head[1];
    edge_at(128, 252, 0, 10000);
    edge_drag(128, 252, 128, 200, 6, 10000);
    check("edge: a swipe after 2 s is the window's again, with its indicator",
          compositor.launcher_asked == 1 && compositor.chip.kind == COMPOSITOR_CHIP_HINT && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) >= 1);
    edge_at(128, 200, 1, 10000);

    /* A second screen has its own edges: a point is on the screen it is in, and its band is that screen's bottom. */
    edge_setup(0);
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 128, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 128, compositor.screen[1][1] = 0, compositor.screen[1][2] = 128, compositor.screen[1][3] = 256;
    {
        int rect[4] = {0, 0, 0, 0}, number = -1;

        check("edge screen: a point on the second screen is that screen, and its bottom band is the screen's own",
              compositor_swipe_screen(200, 252, rect, &number) && number == 1 && rect[0] == 128 && rect[2] == 128 &&
                  ui_edge_band_side(200, 252, rect, 12) == UI_EDGE_BOTTOM && ui_edge_band_side(130, 128, rect, 12) == UI_EDGE_LEFT);
    }
    compositor.outputs = 0;

    /* The side edges, when they are on (compositor.side_edges, off by default): a swipe in from the left is the window switcher's step, and
       one in from the right is the mixer's popup, beside the finger on the right. Neither reaches the window it began on. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 40, 120, 160);
    layout_show(2, 130, 40, 120, 160);
    compositor.asks[1] &= ~WINDOW_NO_TAB, compositor.asks[2] &= ~WINDOW_NO_TAB; /* the switcher steps through the windows that have a tab */
    compositor.side_left = COMPOSITOR_SIDE_WINDOWS;
    compositor.side_right = COMPOSITOR_SIDE_ACT;
    compositor.focus = 1;
    head = table->event_head[1];
    edge_at(5, 100, 0, 10000);
    edge_drag(5, 100, 60, 100, 6, 10000);
    check("edge sides: with the side edges on, a swipe in from the left steps the window switcher, and the window it began on gets nothing",
          compositor.focus == 2 && touch_seen(1, head, WINDOW_EVENT_BUTTON, -1, null) == 0 && compositor.swipe.state == COMPOSITOR_SWIPE_SWIPED);
    edge_at(60, 100, 1, 10000);
    check("edge sides: lifted, the swipe is over", compositor.swipe.state == COMPOSITOR_SWIPE_NONE);
    table->button_edge = TASKBAR_BOTTOM;
    edge_at(250, 100, 0, 10000);
    edge_drag(250, 100, 190, 100, 6, 10000);
    check("edge sides: a swipe in from the right is the mixer's popup, on the right edge, beside the finger",
          table->button_edge == TASKBAR_RIGHT && table->button_w > 0 && table->button_y <= 100 && table->button_y + (int)table->button_h > 100);
    edge_at(190, 100, 1, 10000);
    /* The left edge's launcher (a setting's choice): the launcher opens beside the finger on the left edge of its screen. */
    edge_setup(0);
    table = compositor.table;
    layout_show(1, 0, 40, 120, 160);
    compositor.side_left = COMPOSITOR_SIDE_ACT;
    table->button_edge = TASKBAR_BOTTOM;
    edge_at(5, 100, 0, 10000);
    edge_drag(5, 100, 60, 100, 6, 10000);
    check("edge sides: the left edge set to the launcher opens it on the left edge, beside the finger",
          compositor.launcher_asked == 1 && table->button_edge == TASKBAR_LEFT && table->button_w > 0);
    edge_at(60, 100, 1, 10000);
    compositor.side_left = COMPOSITOR_SIDE_OFF;
    compositor.side_right = COMPOSITOR_SIDE_OFF;
    taskbar_standard();
}

static void check_compositor_touch(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int head1, head2, flags = 0;
    int unchanged;

    memory_zero(&compositor, sizeof(compositor));
    /* These are gestures, timed by the wall clock between reports: the lead a finger is given is check_compositor_predict's. */
    compositor.predict_off[COMPOSITOR_PREDICT_TOUCH] = 1;
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.lit_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.hold = -1;
    compositor.holds = ~0ul;
    compositor.told_row = COMPOSITOR_NONE;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map() || system_call_2(syscall(pipe2), (positive)wheel_pipe, O_NONBLOCK))
    {
        check("touch: the table and the device are made", false);
        return;
    }
    table = compositor.table;
    // The moon and the tasks at the band's start, so a finger in the lower right, within a finger's reach of the band, is the window's.
    taskbar.moon_zone = TASKBAR_ZONE_START, taskbar.tasks_zone = TASKBAR_ZONE_START;
    taskbar_changed();
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    table->x[2] = 150, table->y[2] = 120, table->width[2] = 90, table->height[2] = 100;
    glass_paint(2, 0, 0, 90, 100, 0x206020u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    compositor.inputs[0] = wheel_pipe[0];
    compositor.input_count = 1;
    touch_clock = 1000000;
    touch_down = 0;
#ifdef TOUCH_AS_BEFORE
    // A tablet, its range the screen's, as the reader took a touch screen with an ABS_X before.
    compositor.absolute_x[0][1] = compositor.absolute_y[0][1] = 1020;
#else
    compositor.touchscreen[0] = 1;
    compositor.screen_touch[0] = (struct compositor_touch_screen){0};
    for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        compositor.screen_touch[0].id[slot] = compositor.screen_touch[0].was[slot] = -1;
    compositor.screen_touch[0].slots = 1;
    compositor.screen_touch[0].range[1] = compositor.screen_touch[0].range[3] = 1020;
#endif
    compositor.pointer_x = 60;
    compositor.pointer_y = 70;

    /* Pressed where it lands, not where the pointer (or the last finger) was. */
    head1 = table->event_head[1], head2 = table->event_head[2];
    touch_one(190, 190, false);
    check("touch: a finger put down presses the window it lands on, where it lands, and none where the pointer was",
          touch_buttons(2, head2) == 0x1101 && touch_seen(1, head1, WINDOW_EVENT_BUTTON, -1, null) == 0 &&
              table->event_type[2][(table->event_head[2] - 2) % WINDOWS_EVENTS] == WINDOW_EVENT_MOTION &&
              table->event_a[2][(table->event_head[2] - 2) % WINDOWS_EVENTS] == 40 &&
              table->event_b[2][(table->event_head[2] - 2) % WINDOWS_EVENTS] == 70);
    touch_seen(2, head2, WINDOW_EVENT_BUTTON, -1, &flags);
    check("touch: and the press says a finger made it", (flags & WINDOW_KEY_TOUCH) != 0);
    check("touch: the pointer is hidden while a finger was the last thing used", compositor.touch.hidden);

    /* A finger shakes as it lands: under the slop it moves nothing, and the release is a click where it landed. */
    head2 = table->event_head[2];
    touch_one(193, 192, false);
    touch_one(195, 189, false);
    check("touch: a finger that shakes under the slop moves nothing", touch_seen(2, head2, WINDOW_EVENT_MOTION, -1, null) == 0 &&
                                                                        compositor.pointer_x == 190 && compositor.pointer_y == 190);
    touch_one(195, 189, true);
    check("touch: lifted, it is the release of the press in the same window, a click", touch_buttons(2, head2) == 0x1100);

    /* Past the slop it is the pointer's motion, told the window it pressed, as a finger's. */
    head1 = table->event_head[1], head2 = table->event_head[2];
    touch_one(190, 190, false);
    touch_one(190, 205, false);
    touch_seen(2, head2, WINDOW_EVENT_MOTION, -1, &flags);
    check("touch: moved past the slop, the window it pressed is told where it is now, as a finger's",
          table->event_type[2][(table->event_head[2] - 1) % WINDOWS_EVENTS] == WINDOW_EVENT_MOTION &&
              table->event_a[2][(table->event_head[2] - 1) % WINDOWS_EVENTS] == 40 &&
              table->event_b[2][(table->event_head[2] - 1) % WINDOWS_EVENTS] == 85 && (flags & WINDOW_KEY_TOUCH));

    /* A second finger lands on the other window, and the first lifts: the second never takes the pointer. */
    {
        const int both[2][4] = {{0, 1, 190, 205}, {1, 2, 80, 80}};
        const int second[2][4] = {{0, -1, 0, 0}, {1, 2, 80, 80}};
        const int none[1][4] = {{1, -1, 0, 0}};

        touch_clock += 10000;
        touch_report(2, both);
        touch_clock += 10000;
        touch_report(2, second);
        touch_clock += 10000;
        touch_report(1, none);
        check("touch: a second finger, left on after the first lifts, does not take the pointer to it",
              touch_seen(1, head1, WINDOW_EVENT_BUTTON, -1, null) == 0 && touch_seen(1, head1, WINDOW_EVENT_MOTION, -1, null) == 0 &&
                  compositor.pointer_x == 190 && compositor.pointer_y == 205 && touch_buttons(2, head2) == 0x11011100);
    }

    /* A window taken by its top moves with the finger from the first move past the slop, the hand's grip kept. */
    compositor_present();
    touch_one(170, 125, false);
    touch_one(170, 110, false);
    touch_one(172, 90, false);
    check("touch: a window taken by its top follows the finger, held where the finger took it",
          compositor.x[2] == 152 && compositor.y[2] == 85);
    compositor_present();
    check("touch: and all of its line is lit while it is held", glass_at(151, 140) == COMPOSITOR_INK_LINE_HELD &&
                                                                  glass_at(200, 84) == COMPOSITOR_INK_LINE_HELD);
    touch_one(172, 90, true);
    compositor_present();
    check("touch: let go, its line is the line with the keys again", glass_at(151, 140) == compositor_line_lit() &&
                                                                        compositor.arranged[2] == COMPOSITOR_FLOATING);

    /* A tap on the top of a window at the top of the screen: a click, which raises it and fills nothing. */
    compositor_put(1, 50, 0);
    compositor_raise(2);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor.pointer_x = 100, compositor.pointer_y = 3;
    touch_one(100, 3, false);
    touch_one(102, 5, false);
    touch_one(102, 5, true);
    check("touch: a tap that shakes on the top of a window at the screen's top raises it and fills nothing",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 50 && compositor.y[1] == 0 &&
              compositor.stack[compositor.stacked - 1] == 1);
    compositor_put(1, 50, 30);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();

    /* A finger's grip is wider: 12 pixels outside a window's left edge is the edge, and its corner 20 pixels along. */
    {
        int width = (int)compositor.w[1], height = (int)compositor.h[1];

        touch_one(38, 70, false);
        touch_one(18, 70, false);
        check("touch: twelve pixels outside a window's left edge, a finger takes the edge, and moved it is the window wider",
              (int)compositor.w[1] == width + 20 && compositor.x[1] == 30 && (int)compositor.h[1] == height);
        compositor_present();
        check("touch: the edge it holds is lit in the held ink, and the edges it does not hold are not",
              glass_at(29, 70) == COMPOSITOR_INK_LINE_HELD && glass_at(100, 29) == compositor_line_lit());
        touch_one(18, 70, true);
        compositor_want(1, width, height);
        compositor_put(1, 50, 30);
        compositor.w[1] = (unsigned int)width;
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        touch_one(47, 50, false);
        touch_one(30, 30, false);
        check("touch: a finger three pixels outside the left edge, twenty down from the top, takes the top left corner",
              compositor.resize == 1 && compositor.edges[1] == (COMPOSITOR_EDGE_LEFT | COMPOSITOR_EDGE_TOP));
        compositor_present();
        check("touch: a corner held is lit along both its edges for COMPOSITOR_LIT_REACH, and no further",
              glass_at(compositor.x[1] - 1, compositor.y[1] + 20) == COMPOSITOR_INK_LINE_HELD &&
                  glass_at(compositor.x[1] + 20, compositor.y[1] - 1) == COMPOSITOR_INK_LINE_HELD &&
                  glass_at(compositor.x[1] - 1, compositor.y[1] + 60) == compositor_line_lit());
        touch_one(30, 30, true);
        compositor_want(1, width, height);
        compositor.w[1] = (unsigned int)width, compositor.h[1] = (unsigned int)height;
        compositor_put(1, 50, 30);
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
    }

    /* The close button: a finger that lands on it closes nothing until it lifts there, and one dragged off it nothing (it resizes
       the corner it is in, which is restored here). */
    {
        int bx = compositor.x[2] + (int)compositor.w[2] - UI_CLOSE_IN - UI_CLOSE / 2;
        int by = compositor.y[2] + UI_CLOSE_IN + UI_CLOSE / 2;
        int keep_x = compositor.x[2], keep_y = compositor.y[2], keep_w = (int)compositor.w[2], keep_h = (int)compositor.h[2];

        touch_one(bx, by, false);
        unchanged = !(table->state[2] & WINDOW_CLOSING);
        touch_one(bx - 40, by + 2, false);
        touch_one(bx - 40, by + 2, true);
        unchanged = unchanged && !(table->state[2] & WINDOW_CLOSING);
        compositor_want(2, keep_w, keep_h);
        compositor.w[2] = (unsigned int)keep_w, compositor.h[2] = (unsigned int)keep_h;
        compositor_put(2, keep_x, keep_y);
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        touch_one(bx + 1, by + 9, false);
        touch_one(bx + 1, by + 9, true);
        check("touch: the close button closes when the finger lifts from it, a little wider than the X (4 px), and not when it lands or is dragged off",
              unchanged && (table->state[2] & WINDOW_CLOSING));
        table->state[2] &= ~WINDOW_CLOSING;
        compositor.state[2] &= ~WINDOW_CLOSING;
    }

    /*
        A finger's corner is the kit's target, UI_TARGET each way (about 9 mm), and the close and minimise buttons are the
        finger's only inside their disc and COMPOSITOR_TOUCH_BUTTON_SLOP more: the rest of the corner is the corner's. At each of
        the four corners, at 50, 100 and 200 percent, the points at the edges of the corner's target are that corner and the points
        past them are not; a pointer's corner is its own few pixels. A finger beside the X in the corner resizes; one that lands on
        the X and moves past the slop resizes from where it landed, and closes nothing; one that lifts there closes; one that lands
        7 px below the X's disc, past the slop, neither closes nor resizes; and the minimise button is the finger's within the slop
        of its disc and, past it, the window's top.
    */
    {
        static const int scales[] = {50, 100, 200};
        unsigned int targets = ~0u, pointer = ~0u;
        int x1 = compositor.x[1], y1 = compositor.y[1], x2 = x1 + (int)compositor.w[1], y2 = y1 + (int)compositor.h[1];

        for (unsigned int at = 0; at < array_count(scales); at++)
        {
            int s = scales[at], least = COMPOSITOR_FINGER_TARGET * s / 100, line = s / 100 > 1 ? s / 100 : 1, grip = 6 * s / 100 + line;
            int in = 2 * s / 100, before = (least - (grip + in)) / 2;

            compositor.scale = s;
            for (unsigned int corner = 0; corner < 4; corner++)
            {
                int right = corner & 1, down = corner >> 1, cx = right ? x2 : x1, cy = down ? y2 : y1;
                int lo_x = right ? cx - in - before : cx - grip - before, lo_y = down ? cy - in - before : cy - grip - before;
                unsigned int bits = (right ? COMPOSITOR_EDGE_RIGHT : COMPOSITOR_EDGE_LEFT) | (down ? COMPOSITOR_EDGE_BOTTOM : COMPOSITOR_EDGE_TOP);
                int in_x = right ? lo_x : lo_x + least - 1, in_y = down ? lo_y : lo_y + least - 1;
                int out_x = right ? lo_x + least - 1 : lo_x, out_y = down ? lo_y + least - 1 : lo_y;
                int ok;

                compositor.touching = 1;
                ok = compositor_edge_zone(1, in_x, in_y) == bits && compositor_edge_zone(1, out_x, out_y) == bits &&
                     compositor_edge_zone(1, in_x, out_y) == bits && compositor_edge_zone(1, out_x, in_y) == bits &&
                     compositor_edge_zone(1, right ? lo_x - 1 : lo_x + least, in_y) != bits &&
                     compositor_edge_zone(1, in_x, down ? lo_y - 1 : lo_y + least) != bits;
                compositor.touching = 0;
                pointer &= compositor_edge_zone(1, in_x, in_y) != bits ? ~0u : ~(1u << (at * 4 + corner));
                targets &= ok ? ~0u : ~(1u << (at * 4 + corner));
                if (!ok)
                    string_format(log, "  touch corner: %p percent, corner %p: not a target of %p each way\n", (positive)s,
                                  (positive)corner, (positive)least);
            }
        }
        compositor.scale = 100;
        check("touch: a finger's corner is UI_TARGET each way: the points at the edges of the target are the corner and the ones past it "
              "are not, at each of the four corners at 50, 100 and 200 percent",
              targets == ~0u);
        check("touch: a pointer's corner is its own few pixels, not the finger's target: 18 px in along both edges is no corner",
              pointer == ~0u);
    }
    {
        struct compositor_frame frame;
        int bx, by, side, mx, my, mside, cx, cy, mcx, mcy, resized, before_w = (int)compositor.w[2], before_h = (int)compositor.h[2];
        int closed, beside, below, x0 = compositor.x[2], y0 = compositor.y[2];

        compositor_present();
        compositor_frame_of(2, &frame);
        compositor_close_box(&frame, &bx, &by, &side);
        cx = bx + side / 2, cy = by + side / 2;

        // Beside the X in the corner, the last column of the window and 10 px down, past the X and its slop: the corner, which the X no longer takes.
        touch_one(compositor.x[2] + (int)compositor.w[2] - 1, compositor.y[2] + 10, false);
        beside = compositor.resize == 2 && compositor.edges[2] == (COMPOSITOR_EDGE_RIGHT | COMPOSITOR_EDGE_TOP) &&
                 !(table->state[2] & WINDOW_CLOSING);
        touch_one(compositor.x[2] + (int)compositor.w[2] - 1, compositor.y[2] + 10, true);
        check("touch: a finger beside the X, in the window's top right corner, takes the corner to resize and not the close button", beside);

        // On the X, moved past the slop: the corner's resize from where the finger landed, and nothing closed.
        touch_one(cx, cy, false);
        resized = compositor.resize == COMPOSITOR_NONE && !(table->state[2] & WINDOW_CLOSING);
        touch_one(cx + 20, cy, false);
        resized = resized && compositor.resize == 2 && compositor.edges[2] == (COMPOSITOR_EDGE_RIGHT | COMPOSITOR_EDGE_TOP) &&
                  (int)compositor.w[2] == before_w + 20 && (int)compositor.h[2] == before_h;
        touch_one(cx + 20, cy + 10, false);
        resized = resized && (int)compositor.h[2] == before_h - 10 && compositor.y[2] == y0 + 10;
        touch_one(cx + 20, cy + 10, true);
        resized = resized && !(table->state[2] & WINDOW_CLOSING);
        check("touch: a finger that lands on the X and moves past the slop resizes the corner from where it landed (wider, then shorter "
              "from the top) and closes nothing, neither when it moves nor when it lifts",
              resized);
        compositor_want(2, before_w, before_h);
        compositor.w[2] = (unsigned int)before_w, compositor.h[2] = (unsigned int)before_h;
        compositor_put(2, x0, y0);
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();

        // On the X and lifted there: closed. On the slop's edge too, and a hand's width below it neither closes nor resizes.
        touch_one(cx, cy, false);
        touch_one(cx, cy, true);
        closed = (table->state[2] & WINDOW_CLOSING) != 0;
        table->state[2] &= ~WINDOW_CLOSING;
        compositor.state[2] &= ~WINDOW_CLOSING;
        touch_one(cx, by + side + 3, false);
        touch_one(cx, by + side + 3, true);
        closed = closed && (table->state[2] & WINDOW_CLOSING) != 0;
        table->state[2] &= ~WINDOW_CLOSING;
        compositor.state[2] &= ~WINDOW_CLOSING;
        touch_one(cx, by + side + 7, false);
        below = compositor.resize == COMPOSITOR_NONE && !(table->state[2] & WINDOW_CLOSING);
        touch_one(cx, by + side + 7, true);
        below = below && !(table->state[2] & WINDOW_CLOSING);
        check("touch: the X closes when a finger lifts on its disc or within the slop of it, and a finger 7 px below the disc, past the "
              "slop, closes nothing",
              closed && below);

        // The minimise button: a finger's within the slop of its disc, and past it the window's top, which a finger takes to move it.
        compositor_frame_of(1, &frame);
        compositor_minimize_box(&frame, &mx, &my, &mside);
        mcx = mx - 3, mcy = my + mside / 2;
        touch_one(mcx, mcy, false);
        closed = (compositor.state[1] & WINDOW_MINIMIZED) != 0;
        touch_one(mcx, mcy, true);
        compositor.state[1] &= ~WINDOW_MINIMIZED;
        compositor.table->state[1] = compositor.state[1];
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        touch_one(mx - 6, mcy, false);
        below = compositor.drag == 1 && !(compositor.state[1] & WINDOW_MINIMIZED);
        touch_one(mx - 6, mcy, true);
        below = below && !(compositor.state[1] & WINDOW_MINIMIZED);
        check("touch: the minimise button is a finger's within the slop of its disc (3 px beside it puts the window away) and not past it "
              "(6 px beside it is the window's top, taken to move it)",
              closed && below);
        compositor_put(1, 50, 30);
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
    }

    /* Two fingers: the press is taken back, and moved down together they are the wheel up; tapped, the right button. */
    {
        int from[2][4] = {{0, 5, 70, 60}, {1, 6, 110, 60}};
        const int lifted[2][4] = {{0, -1, 0, 0}, {1, -1, 0, 0}};
        int sum = 0;

        head1 = table->event_head[1];
        touch_clock += 10000;
        touch_report(2, from);
        for (int step = 1; step <= 6; step++)
        {
            from[0][3] = from[1][3] = 60 + step * 8;
            touch_clock += 10000;
            touch_report(2, from);
        }
        for (unsigned int at = head1; at != table->event_head[1]; at++)
            if (table->event_type[1][at % WINDOWS_EVENTS] == WINDOW_EVENT_WHEEL && !table->event_b[1][at % WINDOWS_EVENTS])
                sum += table->event_a[1][at % WINDOWS_EVENTS];
        check("touch: two fingers moved 48 pixels down together are 144 of the wheel up (a notch is UI_NOTCH's 40 px, so 120), what is under them following them",
              sum == 144 && touch_buttons(1, head1) == 0x11011102);
        touch_clock += 10000;
        touch_report(2, lifted);
        head1 = table->event_head[1];
        from[0][1] = 7, from[1][1] = 8, from[0][3] = from[1][3] = 60;
        touch_clock += 10000;
        touch_report(2, from);
        touch_clock += 120000;
        touch_report(2, lifted);
        check("touch: two fingers tapped together are a click of the right button", touch_buttons(1, head1) == 0x1101110211111110ul);
    }

    /*
        Held still for a long press (UI_HOLD_NS), a finger is the right button there, flagged as a long press;
        its left press is taken back first. The finger may move on from there, the pointer following it, and its lift is
        the release that ends the press (cancelled, touch and held), told to the window it landed on.
    */
    head1 = table->event_head[1];
    touch_one(100, 80, false);
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS - 1000000);
    unchanged = touch_buttons(1, head1) == 0x1101;
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
    check("touch: held still a long press's time, a finger is the right button, its left press taken back first",
          unchanged && touch_buttons(1, head1) == 0x1101110211111110ul);
    flags = 0;
    touch_seen(1, head1, WINDOW_EVENT_BUTTON, COMPOSITOR_BTN_RIGHT, &flags);
    check("touch: the long press's right button says so (WINDOW_KEY_HOLD) and that a finger made it",
          (flags & WINDOW_KEY_HOLD) && (flags & WINDOW_KEY_TOUCH));
    head2 = table->event_head[1];
    touch_one(120, 80, false);
    check("touch: a finger held for a long press and moved on has the pointer follow it",
          touch_seen(1, head2, WINDOW_EVENT_MOTION, -1, null) > 0);
    head2 = table->event_head[1];
    touch_one(120, 80, true);
    flags = 0;
    check("touch: its lift is the one release that ends the press: a left release, cancelled, touch and held",
          touch_seen(1, head2, WINDOW_EVENT_BUTTON, COMPOSITOR_BTN_LEFT, &flags) == 1 &&
              touch_seen(1, head2, WINDOW_EVENT_BUTTON, -1, null) == 1 &&
              (flags & (WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD)) ==
                  (WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD));

    /*
        A window that asked for no motion of the pointer over it (a terminal that is in no mouse mode) is told a held finger's moves all
        the same: the long press took the press back, so no button is down for the grab to keep the pointer with the window, and what
        the finger selects after it has to follow it there as it does in a window that wants the hover.
    */
    table->hover_off[1] = 1;
    touch_one(100, 80, false);
    compositor_touch_tick(touch_clock * 1000 + UI_HOLD_NS);
    head2 = table->event_head[1];
    touch_one(120, 80, false);
    unchanged = touch_seen(1, head2, WINDOW_EVENT_MOTION, -1, null) > 0;
    touch_one(120, 80, true);
    table->hover_off[1] = 0;
    check("touch: a finger held for a long press and moved on is told to a window that asked for no hover, as the press's window", unchanged);
    table->hover_off[1] = 1;
    head2 = table->event_head[1];
    touch_one(100, 80, false);
    touch_one(140, 80, false);
    unchanged = touch_seen(1, head2, WINDOW_EVENT_MOTION, -1, null) > 0 && touch_buttons(1, head2) == 0x1101;
    touch_one(140, 80, true);
    table->hover_off[1] = 0;
    check("touch: a finger that is pressed and moved is told to it as before, the press holding the pointer with the window", unchanged);

    /* Its mark, `moonwater canvas touch on`: a disc and a ring round the finger over the windows, gone a second after. */
    compositor.spots_on = 0;
    compositor_touch_settings_read();
    {
        long value;

        check("touch: with nothing kept the marks are on, the row's standard, and a file that says default or a word that is none is the same",
              compositor.spots_on && !compositor_setting_kept(SETTING_TOUCH_PATH, "default", &value) && value == 0 &&
                  !compositor_setting_kept(SETTING_TOUCH_PATH, "sideways", &value) && value == 0 &&
                  compositor_setting_kept(SETTING_TOUCH_PATH, "off", &value) && value == 1);
    }
    compositor.spots_on = 1;
    compositor_present();
    touch_one(100, 80, false);
    compositor_present();
    {
        unsigned int middle = glass_at(100, 80), window = 0x404040u, ring = 0;
        int radius = COMPOSITOR_SPOT;

        for (int x = 100 - radius; x < 100 - radius + 3; x++)
            ring = glass_at(x, 80) > ring ? glass_at(x, 80) : ring;
        check("touch: with canvas touch on, a finger's mark is a faint disc of the held ink over the window, ringed brighter",
              middle == compositor_mix(window, COMPOSITOR_INK_LINE_HELD, COMPOSITOR_SPOT_FILL) && ring > middle &&
                  glass_at(100 - radius - 1, 80) == window && glass_at(100 + radius, 80) == window);
        touch_one(130, 80, false);
        compositor_present();
        check("touch: it follows the finger, and where it was is the window again",
              glass_at(130, 80) == middle && glass_at(85, 80) == window && glass_at(100, 80) != middle);
        touch_one(130, 80, true);
        compositor_spots_tick(touch_clock * 1000 + COMPOSITOR_SPOT_FADE_NS / 2);
        compositor_present();
        check("touch: lifted, it fades: half way through the second it is half as strong",
              glass_at(130, 80) == compositor_mix(window, COMPOSITOR_INK_LINE_HELD, COMPOSITOR_SPOT_FILL * 127 / 255));
        compositor_spots_tick(touch_clock * 1000 + COMPOSITOR_SPOT_FADE_NS);
        compositor_present();
        check("touch: and a second after the finger lifted it is gone, the window as it was, and nothing more to draw",
              glass_at(130, 80) == window && glass_at(130 - radius + 1, 80) == window && !compositor_spots_any(0) &&
                  !compositor.damages);
        compositor.spots_on = 0;
        touch_one(100, 80, false);
        compositor_present();
        check("touch: with canvas touch off no mark is drawn", glass_at(100, 80) == window && !compositor_spots_any(0));
        touch_one(100, 80, true);
    }

    /* On two screens a touch screen is the machine's own panel, and else the primary: not the space across both. */
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 128, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 128, compositor.screen[1][1] = 0, compositor.screen[1][2] = 128, compositor.screen[1][3] = 256;
    string_copy((string_address)compositor.output[0].name, (string_address) "HDMI-A-1");
    string_copy((string_address)compositor.output[1].name, (string_address) "eDP-1");
    touch_one(254, 200, false);
    unchanged = compositor.pointer_x == 128 + 1016 * 127 / 1020 && compositor.pointer_y == 200;
    touch_one(254, 200, true);
    string_copy((string_address)compositor.output[1].name, (string_address) "DP-2");
    touch_one(254, 200, false);
    touch_one(254, 200, true);
    check("touch: on two screens a finger is on the built-in panel's screen, and with none on the primary's",
          unchanged && compositor.pointer_x == 1016 * 127 / 1020 && compositor.pointer_y == 200);
    compositor.outputs = 0;

    /* The pointer under a finger is the dynamic one, whatever the style says (here classic), and a mouse moved after it draws the style's again. */
    compositor.cursor.soft = 1;
    compositor.cursor.style = 0;
    touch_one(100, 80, false);
    touch_one(100, 80, true);
    unchanged = compositor.cursor.drawn && compositor.cursor.drawn_as.dyn;
    wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_X, 3);
    wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
    compositor.touchscreen[0] = 0;
    compositor_input_read(0);
    check("touch: the pointer drawn under a finger is the dynamic one whatever the style says, and a mouse moved draws the style's again",
          unchanged && compositor.cursor.drawn && !compositor.cursor.drawn_as.dyn);
    compositor.cursor.soft = 0;

    system_close(wheel_pipe[0]);
    system_close(wheel_pipe[1]);
    compositor.input_count = 0;
    taskbar_standard();
}
#endif // COMPOSITOR_LINE

