/*
        test/checks/compositor_windows.c: the compositor's windows: their lifecycle, the screens they are on, the card
        and the table, the snap, tiles, the grid and dividers, the lit edge.

        Rows of the compositor's manage.c, outputs.c, kms.c and session.c. One part of the program that the
        terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model kit's sections share (checks.c
        opens and closes it). The terminal's program, the kit's frames, the draw list and the GPU model's kit run with
        the four faces: fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

/*
        The lifecycle sweep: windows opened, placed, moved, put away, brought back, closed and left, on one screen and then
        across the seam of two. After every step life_holds reads the table, the stacking, the focus and the drawing against
        one another: a window shown is in the stack once and one that is not is in none, with no size, state or keys; the
        keys are with a window shown and not put away, or with none; the table's focus is the compositor's; and every window
        drawn lies on the screens. life_drawn finds each window's own colour where it is drawn, and life_ghost finds none of
        it where a window was once it has closed.
*/
static unsigned int life_colour(int row)
{
        return (0x202020u + (unsigned int)row * 0x2d3e4fu) & 0xffffffu;
}

static int life_covered(int x, int y, const int (*screens)[4], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
                if (x >= screens[at][0] && x < screens[at][0] + screens[at][2] && y >= screens[at][1] &&
                    y < screens[at][1] + screens[at][3])
                        return 1;
        return 0;
}

/*
        Whether a window lies on the screens. One screen: the whole of it, when it fits on the screen, and its top left when
        it does not (compositor_screen_keep_window). Two or more: its top left and the middle of its top edge are on a
        screen, which is the rule the keep makes for a window across the seam -- a window may hang below a shorter screen
        by design, so its bottom is not held to one. The size is the window's own, not the picture's: a landing one is
        drawn at 96 percent of it.
*/
static int life_on_screens(int x, int y, int width, int height, const int (*screens)[4], unsigned int count)
{
        int right = x + width - 1, bottom = y + height - 1, fits = 0;

        if (width <= 0 || height <= 0)
                return 0;
        for (unsigned int at = 0; at < count; at++)
                if (width <= screens[at][2] && height <= screens[at][3])
                        fits = 1;
        if (count > 1 || !fits)
                return life_covered(x, y, screens, count) && life_covered(x + width / 2, y, screens, count);
        return life_covered(x, y, screens, count) && life_covered(right, y, screens, count) &&
               life_covered(x, bottom, screens, count) && life_covered(right, bottom, screens, count) &&
               life_covered(x + width / 2, y, screens, count) && life_covered(x + width / 2, bottom, screens, count) &&
               life_covered(x, y + height / 2, screens, count) && life_covered(right, y + height / 2, screens, count);
}

static int life_on(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        if (!compositor_visible(row))
                return 0;
        return life_on_screens(compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row],
                               screens, count);
}

static int life_holds(const char **why)
{
        unsigned int count, stacked_count[WINDOWS];
        const int (*screens)[4] = compositor_screens(&count);

        memory_zero(stacked_count, sizeof(stacked_count));
        for (unsigned int at = 0; at < compositor.stacked; at++)
        {
                if (compositor.stack[at] >= WINDOWS)
                        return *why = "a stack entry is no row", 0;
                stacked_count[compositor.stack[at]]++;
        }
        for (int row = 0; row < WINDOWS; row++)
        {
                if (stacked_count[row] > 1)
                        return *why = "a window is stacked twice", 0;
                if (!compositor.showing[row] != !stacked_count[row])
                        return *why = "a window is stacked without being shown, or shown without being stacked", 0;
                if (!compositor.showing[row])
                {
                        if (compositor.w[row] || compositor.h[row] || compositor.state[row] || compositor.focus == row)
                                return *why = "a row not shown keeps its size, its state or the keys", 0;
                        continue;
                }
                if (compositor_visible(row) && !life_on_screens(compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                                                (int)compositor.h[row], screens, count))
                        return *why = "a window is drawn past its screen", 0;
        }
        if (compositor.focus >= 0 && !compositor_visible(compositor.focus))
                return *why = "the keys are with a window that is gone or put away", 0;
        if (compositor.table->focus != (compositor.focus < 0 ? WINDOWS : (unsigned int)compositor.focus))
                return *why = "the table's focus is not the compositor's", 0;
        return 1;
}

/*
        Each window shown is drawn in its own colour at its middle, where the window on top there is. A point beside a
        window above the one drawn there is skipped: that window's line is drawn in its shadow ink just outside its
        picture, and the line is not the window beneath's to be seen through.
*/
static int life_drawn(void)
{
        struct compositor_frame frame;
        unsigned int pitch = compositor.scanout_pitch / 4;

        for (int row = 0; row < WINDOWS; row++)
        {
                int x, y, top = COMPOSITOR_NONE, beside = 0;

                // A window under 48 pixels a side has its close button and minimise button over its middle: its colour is not there to find.
                if (!compositor_visible(row) || compositor.w[row] < 48 || compositor.h[row] < 48)
                        continue;
                compositor_frame_of(row, &frame);
                x = frame.x + frame.width / 2;
                y = frame.y + frame.height / 2;
                for (unsigned int at = compositor.stacked; at-- > 0;)
                {
                        struct compositor_frame other;
                        int q = compositor.stack[at];

                        if (!compositor_visible(q))
                                continue;
                        compositor_frame_of(q, &other);
                        if (x >= other.x && x < other.x + other.width && y >= other.y && y < other.y + other.height)
                        {
                                top = q;
                                break;
                        }
                        if (x >= other.x - 2 && x < other.x + other.width + 2 && y >= other.y - 2 && y < other.y + other.height + 2)
                                beside = 1;
                }
                if (top < 0 || beside)
                        continue;
                if ((compositor.scanout[y * pitch + x] & 0xffffffu) != life_colour(top))
                        return 0;
        }
        return 1;
}

// The pixels of a closed window's colour left where it was drawn: none, once it is gone and the screen is presented.
static unsigned int life_ghost(int row, const struct compositor_frame *was)
{
        unsigned int pitch = compositor.scanout_pitch / 4, left = 0;

        for (int y = was->y; y < was->y + was->height; y++)
                for (int x = was->x; x < was->x + was->width; x++)
                        if (x >= 0 && y >= 0 && x < compositor.width && y < compositor.height &&
                            (compositor.scanout[y * pitch + x] & 0xffffffu) == life_colour(row))
                                left++;
        return left;
}

static void life_present(void)
{
        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_present();
}

// A picture of width by height on a row, drawn in the row's colour and knocked on as a program would: the picture
// is cut to the buffer's stride and room_rows, which is how far the program may draw.
static void life_paint(int row, int width, int height, int room_rows)
{
        struct windows address_to table = compositor.table;
        unsigned int address_to pixels = windows_buffer(table, row, 0);
        int stride = (int)table->stride;
        int cut_w = width > stride ? stride : width, cut_h = height > room_rows ? room_rows : height;

        for (int line = 0; line < cut_h; line++)
                for (int column = 0; column < cut_w; column++)
                        pixels[line * stride + column] = life_colour(row);
        table->width[row] = (unsigned int)width;
        table->height[row] = (unsigned int)height;
        table->buffer_index[row] = 0;
        table->damage_x[row] = table->damage_y[row] = table->damage_width[row] = 0;
        table->damage_height[row] = (unsigned int)height;
        table->frame[row]++;
        compositor_take(row);
}

// The hand's close, then the door's: the program has gone, so the window is left as the door leaves it.
static void life_close(int row)
{
        compositor_close(row);
        compositor_popup_leave(row);
        compositor_clip_leave(row);
        compositor_leave(row);
}

// One step, presented and held: the step happened, and the table, the stacking, the focus and the drawing hold.
static void life_step(const char *step, int happened)
{
        const char *why = 0;
        int okay;

        life_present();
        okay = life_holds(&why) && life_drawn();
        checks++;
        if (!happened || !okay)
        {
                failures++;
                string_format(log, "  FAIL lifecycle %s: %s\n", step, !happened ? "the step did not happen" :
                              why ? why : "a window is not drawn where it is");
                log_flush();
        }
}

static void check_compositor_lifecycle(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct compositor_frame was;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("lifecycle: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->motion = 0; // the landing runs, so the first pictures are the ones a window shows
    table->focus = WINDOWS; // as the table is made: no row has the keys

    // Opened at the edges: off the top left, past the bottom right, and larger than the screen.
    table->x[1] = -30, table->y[1] = -20;
    life_paint(1, 100, 80, GLASS_SIDE);
    life_step("open off the top left", compositor.showing[1] != 0);
    check("lifecycle: a window opened off the top left is put on the screen", life_on(1));
    check("lifecycle: and with no keys asked for it takes none", compositor.focus == COMPOSITOR_NONE);

    table->x[2] = 250, table->y[2] = 240;
    life_paint(2, 60, 60, GLASS_SIDE);
    life_step("open past the bottom right", compositor.showing[2] != 0);
    check("lifecycle: a window opened past the bottom right is put whole on the screen",
          life_on(2) && compositor.x[2] + (int)compositor.w[2] <= GLASS_SIDE && compositor.y[2] + (int)compositor.h[2] <= GLASS_SIDE);

    table->x[3] = 0, table->y[3] = 0;
    life_paint(3, 400, 300, GLASS_SIDE);
    life_step("open larger than the screen", compositor.showing[3] != 0);
    check("lifecycle: a window larger than the screen is shown no larger than the screen, on it",
          compositor.w[3] == GLASS_SIDE && compositor.h[3] == GLASS_SIDE && life_on(3));

    // Moved: to the bottom right, and off every screen either way.
    compositor_place(1, 200, 200);
    life_step("move a window to the bottom right", compositor.showing[1] != 0);
    check("lifecycle: a window moved to the bottom right is kept whole on the screen", life_on(1));
    compositor_place(1, 5000, 5000);
    life_step("move a window off the bottom right", compositor.showing[1] != 0);
    check("lifecycle: a window moved off every screen is brought back on to it", life_on(1));
    compositor_place(1, -5000, -5000);
    life_step("move a window off the top left", compositor.showing[1] != 0);
    check("lifecycle: and off the top left too", life_on(1));

    // Put away and brought back: the keys go under it, and come back when its tab is pressed.
    compositor_focus(2);
    life_step("give the keys to the second window", compositor.focus == 2);
    compositor_minimize_row(2);
    life_step("put the focused window away", (compositor.state[2] & WINDOW_MINIMIZED) != 0);
    check("lifecycle: a window put away is still shown and stacked, and the keys go to the top one shown",
          compositor.showing[2] && compositor_stack_place(2) >= 0 && !compositor_visible(2) && compositor.focus == 3);
    compositor_minimize_row(2);
    life_step("put away a window again", 1);
    check("lifecycle: a window put away twice is put away once, the keys where they were", compositor.focus == 3);
    compositor_reveal(2);
    life_step("bring the put away window back", compositor_visible(2) != 0);
    check("lifecycle: bringing it back rises it over the others and gives it the keys",
          compositor.focus == 2 && compositor.stack[compositor.stacked - 1] == 2 && table->state[2] == 0);

    // Closed without the keys: no pixel of its own is left, and the keys stay where they were.
    compositor_frame_of(1, &was);
    life_close(1);
    life_step("close a window without the keys", !compositor.showing[1]);
    check("lifecycle: a closed window leaves none of its colour on the screen", life_ghost(1, &was) == 0);
    check("lifecycle: and the keys stay with the window they were with", compositor.focus == 2);

    // Closed with the keys: they go to the top window shown.
    compositor_frame_of(2, &was);
    life_close(2);
    life_step("close the focused window", !compositor.showing[2]);
    check("lifecycle: closing the focused window gives the keys to the top window shown", compositor.focus == 3);
    check("lifecycle: and it leaves none of its colour on the screen", life_ghost(2, &was) == 0);

    compositor_frame_of(3, &was);
    life_close(3);
    life_step("close the last window shown", !compositor.showing[3]);
    check("lifecycle: with none shown the keys are with none, and the table says so",
          compositor.focus == COMPOSITOR_NONE && compositor.stacked == 0 && table->focus == WINDOWS);
    check("lifecycle: and the last one leaves none of its colour on the screen", life_ghost(3, &was) == 0);

    // A window put away then closed is gone whole: the next program in its row starts shown, put away by nothing.
    life_paint(1, 100, 80, GLASS_SIDE);
    life_step("open a window in a row that was closed", compositor.showing[1] != 0);
    compositor_minimize_row(1);
    life_step("put it away", (compositor.state[1] & WINDOW_MINIMIZED) != 0);
    life_close(1);
    life_step("close it put away", !compositor.showing[1] && compositor.stacked == 0);
    life_paint(1, 100, 80, GLASS_SIDE);
    life_step("the next program in the row opens", compositor.showing[1] != 0);
    check("lifecycle: the next program in a row is not put away by the one that closed in it",
          !(compositor.state[1] & WINDOW_MINIMIZED) && compositor_visible(1) && table->state[1] == 0);
    life_close(1);
    life_step("close it again", !compositor.showing[1]);

    // Many windows at once, every row, scattered over the edges, closed in an order that is not the opening one.
    for (int row = 0; row < WINDOWS; row++)
    {
        table->x[row] = (row * 37) % 230 - 20;
        table->y[row] = (row * 53) % 230 - 20;
        life_paint(row, 60, 40, GLASS_SIDE);
        life_step("open every row", compositor.showing[row] != 0);
    }
    check("lifecycle: thirty-two windows are shown, each on the screen", life_on(0) && life_on(31));
    for (int at = 0; at < WINDOWS; at++)
    {
        int row = (at * 11 + 5) % WINDOWS;

        if (!compositor.showing[row])
            continue;
        life_close(row);
        life_step("close one of many", !compositor.showing[row]);
    }
    check("lifecycle: and when all are closed nothing is stacked, shown or focused",
          compositor.stacked == 0 && compositor.focus == COMPOSITOR_NONE);
}

/*
        A window asked for less than the least a window is, at a scale whose least is past the screen (96 by 48 at 800
        percent is 768 by 384, on a screen of 256): compositor_want let the least win over the most, so the window was made
        wider and taller than the rows of its buffer, and the stretch to that size wrote past them -- into its other buffer
        here, into the next row's (another window's pixels) when it showed its second buffer, and past the table for the last
        row. The most is the last word: the window is no larger than the screen, and nothing past its own buffer is written.
*/
static void check_compositor_least_scale(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    unsigned int address_to others[3];
    unsigned long words = (unsigned long)GLASS_SIDE * GLASS_SIDE, changed = 0;
    struct windows address_to table;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("least scale: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->motion = 1;
    table->focus = WINDOWS;
    table->x[1] = table->y[1] = 10;
    life_paint(1, 100, 80, GLASS_SIDE);
    others[0] = windows_buffer(table, 1, 1);
    others[1] = windows_buffer(table, 2, 0);
    others[2] = windows_buffer(table, 2, 1);
    for (unsigned int at = 0; at < 3; at++)
        for (unsigned long word = 0; word < words; word++)
            others[at][word] = 0x00123456u;
    compositor.scale = 800;
    compositor_want(1, 10, 10);
    for (unsigned int at = 0; at < 3; at++)
        for (unsigned long word = 0; word < words; word++)
            changed += others[at][word] != 0x00123456u;
    check("least scale: a window asked for less than the least at 800 percent is no larger than the screen",
          compositor.w[1] <= GLASS_SIDE && compositor.h[1] <= GLASS_SIDE && table->want_width[1] <= GLASS_SIDE &&
              table->want_height[1] <= GLASS_SIDE);
    check("least scale: and nothing past its own buffer is written (its other buffer, the next window's)", changed == 0);
    compositor.scale = 100;
    life_close(1);
}

/*
        The connectors a card lists, asked for twice as the kernel wants (how many, then the list into room for them): a
        screen that came between the two (a dock's MST port) makes the second answer's count larger than the list the
        kernel wrote, and the compositor went on through the count, reading the ids past the 16 it had room for and asking
        the card of whatever was in its stack. It reads what was written: the two connectors listed, and no other.
*/
static void check_compositor_card_lists(void)
{
    struct compositor_connected found;
    unsigned int unlisted = 0;

    memory_zero(&compositor, sizeof(compositor));
    if (!card_fake_open(800, 1280))
    {
        check("card lists: the fake card is made", false);
        return;
    }
    card_fake.connectors = 2;
    card_fake.connectors_after = 40;
    compositor_connected_read(&found);
    for (unsigned int at = 0; at < card_fake.asked_count; at++)
        unlisted += card_fake.asked[at] != CARD_FAKE_CONNECTOR && card_fake.asked[at] != CARD_FAKE_CONNECTOR + 1;
    check("card lists: connectors that came between the two calls are not read past the list the kernel wrote",
          unlisted == 0 && card_fake.asked_count >= 2 && found.count == 1 && found.connector[0] == CARD_FAKE_CONNECTOR);
    card_fake_close();
}

/*
        The compositor's reads of /root (a setting's word, a file's text) and its write of keys.fire are opened not through a
        link and without waiting: a link in a setting's place had the root compositor read whatever it named, a FIFO with no
        writer in its place held the compositor in the open for ever (the desktop with it), and a link in keys.fire's place had
        the chord written into what it named. A link or a FIFO is no file: nothing is read and nothing written.
*/
static void check_compositor_setting_opens(void)
{
    static const char file[] = "compositor-setting-file.tmp", link[] = "compositor-setting-link.tmp", fifo[] = "compositor-setting-fifo.tmp";
    static const char target[] = "compositor-fire-file.tmp", fire[] = "compositor-fire-link.tmp";
    char word[16], text[64], plain[16];
    bipolar handle, child = -1, waited = 0;
    positive status = 0, written = 1;

    for (unsigned int at = 0; at < 5; at++)
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)(const char *[]){file, link, fifo, target, fire}[at], 0);
    handle = system_open_at_mode(AT_FDCWD, (string_address)file, FILE_READ_WRITE | FILE_CREATE | O_CLOEXEC, 0600);
    if (handle >= 0)
        system_write_once(handle, "off\n", 4), system_close(handle);
    handle = system_open_at_mode(AT_FDCWD, (string_address)target, FILE_READ_WRITE | FILE_CREATE | O_CLOEXEC, 0600);
    if (handle >= 0)
        system_close(handle);
    system_call_3(syscall(symlinkat), (positive)file, (positive)AT_FDCWD, (positive)link);
    system_call_3(syscall(symlinkat), (positive)target, (positive)AT_FDCWD, (positive)fire);
    system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)fifo, 0010600, 0);

    compositor_word(file, plain, sizeof(plain));
    compositor_word(link, word, sizeof(word));
    compositor_text(link, text, sizeof(text));
    check("setting opens: a link in a setting's place is not read through, as a word or a file's text",
          !memory_compare(plain, "off", 4) && !word[0] && !text[0]);

    // A FIFO with no writer, in a child the check waits two seconds for.
    child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0);
    if (child == 0)
    {
        compositor_word(fifo, word, sizeof(word));
        system_call_1(syscall(exit_group), word[0] ? 3 : 0);
    }
    for (int tries = 0; child > 0 && tries < 200 && waited != child; tries++)
    {
        timespec nap = {0, 10000000};

        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 1, 0);
        if (waited != child)
            system_call_2(syscall(nanosleep), (positive)&nap, 0);
    }
    if (child > 0 && waited != child)
    {
        system_call_2(syscall(kill), (positive)child, 9);
        system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    }
    check("setting opens: a FIFO in a setting's place is read at once as nothing, and the compositor is not held",
          child > 0 && waited == child && (status & 0xffff) == 0);

    // keys.fire a link to a file: the chord is written into nothing.
    compositor_keys_fire = fire;
    compositor_script_say("key_super_l");
    compositor_keys_fire = COMPOSITOR_KEYS_FIRE;
    handle = system_open_at(AT_FDCWD, (string_address)target, FILE_READ | O_CLOEXEC);
    if (handle >= 0)
    {
        written = (positive)compositor_call(syscall(lseek), (positive)handle, 0, 2, 0, 0, 0);
        system_close(handle);
    }
    check("setting opens: a link in keys.fire's place is not written through", handle >= 0 && written == 0);

    for (unsigned int at = 0; at < 5; at++)
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)(const char *[]){file, link, fifo, target, fire}[at], 0);
}

/*
        A write the compositor makes to a pipe whose reader has gone (keys.fire closed by the script between the open and the
        write, the sound server's socket) is EPIPE, not the compositor's end: it ignores SIGPIPE itself, as it set its signals
        up (compositor_signals), where only the Wayland shim did before. In a child, which writes to such a pipe.
*/
/*
        The signal that has the compositor read /root again (SIGUSR1) is one of the descriptor's, and is told from the ones
        that end it: sent to a child that has the compositor's signals, it is read off the descriptor as its number, and
        SIGTERM after it as its own; where it was no signal of the compositor's and ended the program, as it does a child
        that has not taken the signals.
*/
static void check_compositor_signal_again(void)
{
    bipolar child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0), waited = -1;
    positive status = 0;

    if (child == 0)
    {
        bipolar self = (bipolar)system_call(syscall(getpid));
        unsigned int first, second;

        compositor.signals = compositor_signals();
        system_call_2(syscall(kill), (positive)self, COMPOSITOR_SIGUSR1);
        first = compositor_signal_taken();
        system_call_2(syscall(kill), (positive)self, 15);
        second = compositor_signal_taken();
        system_call_1(syscall(exit_group), first == COMPOSITOR_SIGUSR1 && second == 15 && !compositor_signal_taken() ? 0 : 3);
    }
    if (child > 0)
        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    check("signal again: SIGUSR1 is read off the compositor's descriptor as itself, SIGTERM after it as itself, and neither ends it unread",
          child > 0 && waited == child && (status & 0xffff) == 0);
}

static void check_compositor_pipe_ignored(void)
{
    bipolar child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0), waited = -1;
    positive status = 0;

    if (child == 0)
    {
        int ends[2];
        bipolar wrote;

        compositor_pipe_ignore();
        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) != 0)
            system_call_1(syscall(exit_group), 4);
        system_close(ends[0]);
        wrote = system_write_once(ends[1], "x", 1);
        system_call_1(syscall(exit_group), wrote == -32 ? 0 : 3); // EPIPE
    }
    if (child > 0)
        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    check("pipe ignored: a write to a pipe with no reader is EPIPE, and the compositor is not ended by SIGPIPE",
          child > 0 && waited == child && (status & 0xffff) == 0);
}

/*
        A process with an end of a window's door or wake can hold the pipe's lock (a splice into the pipe from a socket that
        has nothing to say holds it while it waits), and every read and write of the pipe waits for that lock whatever
        O_NONBLOCK says: the loop drained the door and wrote the wake itself, so the whole compositor waited, past SIGKILL.
        With the doors' threads (compositor_doors) the loop's knock, its wake and a chord's line return at once while the
        locks are held, and the row's thread is the one that waits. In a child the check waits three seconds for.
*/
static fn doors_hold(address_any context)
{
        const long address_to ends = (const long address_to)context;

        compositor_call(syscall(splice), (positive)ends[0], 0, (positive)ends[1], 0, 4096, 0);
}

static void check_compositor_doors_held(void)
{
    static const char door[] = "compositor-door.tmp", wake[] = "compositor-wake.tmp";
    bipolar child, waited = -1;
    positive status = 0;

    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)door, 0);
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)wake, 0);
    if (system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)door, 0010600, 0) != 0 ||
        system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)wake, 0010600, 0) != 0)
    {
        check("doors held: the FIFOs are made", false);
        return;
    }
    child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0);
    if (child == 0)
    {
        static long held[2][2];
        int door_socket[2], wake_socket[2];
        timespec nap = {0, 100000000};

        memory_zero(&compositor, sizeof(compositor));
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        if (!glass_table_map())
            system_call_1(syscall(exit_group), 4);
        compositor.door[1] = (int)system_open_at(AT_FDCWD, (string_address)door, FILE_READ | O_NONBLOCK | O_CLOEXEC);
        compositor.wake[1] = (int)system_open_at(AT_FDCWD, (string_address)wake, FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC);
        // Blocking ends: a splice into a pipe whose file is O_NONBLOCK does not wait on its socket, and holds no lock.
        held[0][1] = (long)system_open_at(AT_FDCWD, (string_address)door, O_WRONLY | O_CLOEXEC);
        held[1][1] = (long)system_open_at(AT_FDCWD, (string_address)wake, O_WRONLY | O_CLOEXEC);
        if (compositor_call(syscall(socketpair), 1, 1, 0, (positive)door_socket, 0, 0) != 0 ||
            compositor_call(syscall(socketpair), 1, 1, 0, (positive)wake_socket, 0, 0) != 0 || compositor.door[1] < 0 ||
            compositor.wake[1] < 0 || held[0][1] < 0 || held[1][1] < 0)
            system_call_1(syscall(exit_group), 5);
        held[0][0] = door_socket[0], held[1][0] = wake_socket[0];
        // The two splices take the door's and the wake's locks and keep them: their sockets have nothing to say.
        thread_start(doors_hold, (address_any)held[0]);
        thread_start(doors_hold, (address_any)held[1]);
        system_call_2(syscall(nanosleep), (positive)&nap, 0);
        compositor_doors_start(-1);
        compositor_door(1, COMPOSITOR_POLLIN);
        compositor.woken = 1u << 1;
        compositor_knock();
        compositor_door(1, COMPOSITOR_POLLIN | COMPOSITOR_POLLHUP);
        system_call_1(syscall(exit_group), 0);
    }
    for (int tries = 0; child > 0 && tries < 300 && waited != child; tries++)
    {
        timespec nap = {0, 10000000};

        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 1, 0);
        if (waited != child)
            system_call_2(syscall(nanosleep), (positive)&nap, 0);
    }
    if (child > 0 && waited != child)
    {
        system_call_2(syscall(kill), (positive)child, 9);
        system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    }
    check("doors held: a door's and a wake's held locks do not hold the loop: a knock, a wake and a close return at once",
          child > 0 && waited == child && (status & 0xffff) == 0);
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)door, 0);
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)wake, 0);
}

/*
        A headless compositor's table is a memfd sealed against shrinking, as one with a card has: it was a plain file in
        /run/moonwater, which whatever writes the table could cut to nothing under the compositor's mapping, and its next read
        of the table was SIGBUS. In a child, root of a user namespace of its own with a /run of its own: the table made as
        compositor_table makes it with no card, cut through its path, and read.
*/
static fn table_sealed_write(const char *path, const char *text)
{
    bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_WRITE | O_CLOEXEC);

    if (handle >= 0)
        system_write_once(handle, text, string_length((string_address)text)), system_close(handle);
}

/*
        A program takes the headless compositor's table by its path (windows_take: the device gives none, so the plain path). The compositor
        makes the table as compositor_table makes it with no card (a memfd reached through WINDOWS_TABLE, a symlink to /proc/PID/fd/N), and a
        program opens that path, maps the table read-write, reads the compositor's header through its own mapping, and finds the rows free. A cut
        through the path is refused (the seal), and the program's own mapping still reads the header. In a child with a /run of its own, as above.
*/
static void check_compositor_table_taken(void)
{
    unsigned int uid = (unsigned int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
    unsigned int gid = (unsigned int)compositor_call(syscall(getgid), 0, 0, 0, 0, 0, 0);
    bipolar child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0), waited = -1;
    positive status = 0;

    if (child == 0)
    {
        char map[40] = "0 ";
        positive at = 2;
        bipolar handle;
        long mapped;
        struct windows address_to table;

        if (compositor_call(syscall(unshare), 0x10000000 | 0x00020000, 0, 0, 0, 0, 0) != 0)
            system_call_1(syscall(exit_group), 77);
        table_sealed_write("/proc/self/setgroups", "deny");
        at += positive_into_string((p8 address_to)map + at, uid);
        string_copy((string_address)map + at, (string_address) " 1");
        table_sealed_write("/proc/self/uid_map", map);
        at = 2;
        at += positive_into_string((p8 address_to)map + at, gid);
        string_copy((string_address)map + at, (string_address) " 1");
        table_sealed_write("/proc/self/gid_map", map);
        if (compositor_call(syscall(mount), (positive) "none", (positive) "/run", (positive) "tmpfs", 0, 0, 0) != 0)
            system_call_1(syscall(exit_group), 77);
        memory_zero(&compositor, sizeof(compositor));
        compositor.card = -1;
        compositor.width = compositor.height = 64;
        compositor.scale = 100;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        if (compositor_table() < 0)
            system_call_1(syscall(exit_group), 78);
        // the program: its own open of the path, its own mapping of the table, the compositor's header read through it
        handle = system_open_at(AT_FDCWD, (string_address)WINDOWS_TABLE, FILE_READ_WRITE | O_CLOEXEC);
        if (handle < 0)
            system_call_1(syscall(exit_group), 79);
        mapped = compositor_call(syscall(mmap), 0, sizeof(struct windows), 3, 1, (positive)handle, 0);
        if (mapped < 0 && mapped > -4096)
            system_call_1(syscall(exit_group), 80);
        table = (struct windows address_to)mapped;
        if (table->output_width != 64 || table->output_height != 64)
            system_call_1(syscall(exit_group), 81);
        // a row free to take through the program's mapping is the compositor's table's: the compositor sees the claim
        if (table->pid[0] != 0)
            system_call_1(syscall(exit_group), 82);
        __atomic_store_n(&table->pid[0], 4242u, __ATOMIC_RELEASE);
        if (compositor.table->pid[0] != 4242u)
            system_call_1(syscall(exit_group), 83);
        // the cut through the path is refused, and the header is what it was
        if (compositor_call(syscall(ftruncate), (positive)handle, 0, 0, 0, 0, 0) >= 0)
            system_call_1(syscall(exit_group), 84);
        if (table->output_width != 64)
            system_call_1(syscall(exit_group), 85);
        system_call_1(syscall(exit_group), 0);
    }
    if (child > 0)
        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    if (waited == child && (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 77)
    {
        string_format(log, "  table taken: NOT RUN, no user namespace with a /run of its own here\n");
        return;
    }
    check("table taken: a program opens the headless table through its path, maps it, claims a row, and a cut through the path is refused",
          child > 0 && waited == child && (status & 0xffff) == 0);
    if (child > 0 && waited == child && (status & 0x7f) == 0 && ((status >> 8) & 0xff) != 0)
        string_format(log, "  table taken: the program's step %p failed\n", (positive)((status >> 8) & 0xff));
}

/*
        The cursor plane's probe (compositor_cursor_probe, compositor/kms.c), with the card's answer given by hand, as no card is here:
        without the file i915 and xe are refused the plane by name as before; asked, they are not; a vblank that came keeps the plane,
        none takes it off and says so in moonwater debug's word, and a card that cannot be asked tells nothing, so the names' refusal
        stands for theirs and no other. The file is looked for at its path, in a child with a /run of its own.
*/
static void check_compositor_cursor_probe(void)
{
    char was[sizeof(compositor.driver)];
    int card = compositor.card, soft = compositor.cursor.soft;
    unsigned int uid = (unsigned int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
    unsigned int gid = (unsigned int)compositor_call(syscall(getgid), 0, 0, 0, 0, 0, 0);
    bipolar child, waited = -1;
    positive status = 0;
    b32 named, asked;

    memory_copy(was, compositor.driver, sizeof(was));
    memory_zero(&compositor_cursor_probe, sizeof(compositor_cursor_probe));
    string_copy((string_address)compositor.driver, (string_address) "i915");
    named = !compositor_cursor_plane_wanted();
    string_copy((string_address)compositor.driver, (string_address) "xe");
    named = named && !compositor_cursor_plane_wanted();
    string_copy((string_address)compositor.driver, (string_address) "amdgpu");
    check("cursor probe: not asked, i915 and xe are refused the plane by name, and another driver is not", named && compositor_cursor_plane_wanted());
    compositor_cursor_probe.asked = 1;
    string_copy((string_address)compositor.driver, (string_address) "i915");
    check("cursor probe: asked for, i915 is given the plane to try", compositor_cursor_plane_wanted());
    compositor.card = -1;
    check("cursor probe: a card that cannot be asked for a vblank answers that it cannot", compositor_cursor_vblank(0) == -1);
    check("cursor probe: a vblank that came keeps the plane, and the probe is made once",
          compositor_cursor_probed(1) == 0 && compositor_cursor_probe.done && !compositor_cursor_probe.stopped);
    memory_zero(&compositor_cursor_probe, sizeof(compositor_cursor_probe));
    compositor_cursor_probe.asked = 1;
    compositor.cursor.soft = 1;
    check("cursor probe: none takes the plane off, and moonwater debug's pointer word says the probe drew it",
          compositor_cursor_probed(0) == -1 && compositor_cursor_probe.stopped && compositor_pointer_plane_word() == 2);
    memory_zero(&compositor_cursor_probe, sizeof(compositor_cursor_probe));
    check("cursor probe: a card that cannot be asked keeps the names' refusal for i915", compositor_cursor_probed(-1) == -1);
    memory_zero(&compositor_cursor_probe, sizeof(compositor_cursor_probe));
    string_copy((string_address)compositor.driver, (string_address) "amdgpu");
    check("cursor probe: and for no other driver", compositor_cursor_probed(-1) == 0 && !compositor_cursor_probe.stopped);
    compositor.cursor.soft = 0;
    check("cursor probe: the pointer word is 1 on the plane and 0 drawn for no probe", compositor_pointer_plane_word() == 1 &&
                                                                                          (compositor.cursor.soft = 1, compositor_pointer_plane_word() == 0));
    memory_zero(&compositor_cursor_probe, sizeof(compositor_cursor_probe));
    memory_copy(compositor.driver, was, sizeof(was));
    compositor.card = card, compositor.cursor.soft = soft;

    child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0);
    if (child == 0)
    {
        char map[40] = "0 ";
        positive at = 2;

        if (compositor_call(syscall(unshare), 0x10000000 | 0x00020000, 0, 0, 0, 0, 0) != 0)
            system_call_1(syscall(exit_group), 77);
        table_sealed_write("/proc/self/setgroups", "deny");
        at += positive_into_string((p8 address_to)map + at, uid);
        string_copy((string_address)map + at, (string_address) " 1");
        table_sealed_write("/proc/self/uid_map", map);
        at = 2;
        at += positive_into_string((p8 address_to)map + at, gid);
        string_copy((string_address)map + at, (string_address) " 1");
        table_sealed_write("/proc/self/gid_map", map);
        if (compositor_call(syscall(mount), (positive) "none", (positive) "/run", (positive) "tmpfs", 0, 0, 0) != 0)
            system_call_1(syscall(exit_group), 77);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);
        asked = compositor_cursor_probe_asked();
        {
            bipolar made = system_open_at_mode(AT_FDCWD, (string_address)COMPOSITOR_CURSOR_PROBE_FILE, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);

            if (made >= 0)
                system_close(made);
        }
        system_call_1(syscall(exit_group), !asked && compositor_cursor_probe_asked() ? 0 : 1);
    }
    if (child > 0)
        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    if (waited == child && (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 77)
    {
        string_format(log, "  cursor probe: the file's rows NOT RUN, no user namespace with a /run of its own here\n");
        return;
    }
    check("cursor probe: asked for by " COMPOSITOR_CURSOR_PROBE_FILE " alone: none there is not asked, one there is",
          child > 0 && waited == child && (status & 0xffff) == 0);
}

static void check_compositor_table_sealed(void)
{
    unsigned int uid = (unsigned int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
    unsigned int gid = (unsigned int)compositor_call(syscall(getgid), 0, 0, 0, 0, 0, 0);
    bipolar child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0), waited = -1;
    positive status = 0;

    if (child == 0)
    {
        char map[40] = "0 ";
        positive at = 2;
        bipolar handle;
        volatile unsigned int frame;

        if (compositor_call(syscall(unshare), 0x10000000 | 0x00020000, 0, 0, 0, 0, 0) != 0) // CLONE_NEWUSER | CLONE_NEWNS
            system_call_1(syscall(exit_group), 77);
        table_sealed_write("/proc/self/setgroups", "deny");
        at += positive_into_string((p8 address_to)map + at, uid);
        string_copy((string_address)map + at, (string_address) " 1");
        table_sealed_write("/proc/self/uid_map", map);
        at = 2;
        at += positive_into_string((p8 address_to)map + at, gid);
        string_copy((string_address)map + at, (string_address) " 1");
        table_sealed_write("/proc/self/gid_map", map);
        if (compositor_call(syscall(mount), (positive) "none", (positive) "/run", (positive) "tmpfs", 0, 0, 0) != 0)
            system_call_1(syscall(exit_group), 77);
        memory_zero(&compositor, sizeof(compositor));
        compositor.card = -1;
        compositor.width = compositor.height = 64;
        compositor.scale = 100;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        if (compositor_table() < 0)
            system_call_1(syscall(exit_group), 78);
        handle = system_open_at(AT_FDCWD, (string_address)WINDOWS_TABLE, FILE_READ_WRITE | O_CLOEXEC);
        if (handle >= 0)
            compositor_call(syscall(ftruncate), (positive)handle, 0, 0, 0, 0, 0);
        frame = compositor.table->frame[0];
        compositor.table->focus = frame;
        system_call_1(syscall(exit_group), 0);
    }
    if (child > 0)
        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    if (waited == child && (status & 0x7f) == 0 && ((status >> 8) & 0xff) >= 77)
    {
        string_format(log, "  table sealed: NOT RUN, no user namespace with a /run of its own here (%p)\n", (status >> 8) & 0xff);
        return;
    }
    check("table sealed: a headless compositor's table cut through its path is not cut, and the compositor reads it on",
          child > 0 && waited == child && (status & 0xffff) == 0);
}

/*
        A monitor's name from its EDID, whatever its length: the blob was asked for at 256 bytes, and the kernel copies a blob
        only when the length asked is its own, so a monitor with no extension (128) or with two or more (384 and up) had no
        name. Every length of whole blocks up to a base block and 255 extensions is read; one that is not whole blocks, or
        is longer, is none.
*/
static void check_compositor_card_edid(void)
{
    static const unsigned int lengths[6] = {128, 256, 384, 32768, 200, 32896};
    static const unsigned char named[6] = {1, 1, 1, 1, 0, 0};
    unsigned int right = 0;

    memory_zero(&compositor, sizeof(compositor));
    if (!card_fake_open(1920, 1080))
    {
        check("card edid: the fake card is made", false);
        return;
    }
    for (unsigned int at = 0; at < 6; at++)
    {
        static const unsigned char header[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        static const char model[13] = {'P', 'a', 'n', 'e', 'l', '\n', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
        char name[WINDOWS_OUTPUT_MODEL];

        memory_zero(card_fake.edid, sizeof(card_fake.edid));
        memory_copy(card_fake.edid, header, sizeof(header));
        card_fake.edid[54 + 3] = 0xfc;
        memory_copy(card_fake.edid + 54 + 5, model, sizeof(model));
        card_fake.edid[126] = (unsigned char)(lengths[at] / 128 - 1);
        card_fake.edid_length = lengths[at];
        compositor_connector_model(CARD_FAKE_CONNECTOR, name, sizeof(name));
        right += named[at] ? !memory_compare(name, "Panel", 6) : !name[0];
    }
    check("card edid: a monitor's name is read from an EDID of any whole number of blocks, and none from one that is not, or too long",
          right == 6);
    card_fake_close();
}

/*
        A screen its displays line turns a quarter (a panel built portrait, the Steam Deck's), on a card whose primary plane can
        turn: the plane's rotation property was asked for with the count of its values the first call left and a null pointer
        for them, which the kernel faults on, so no card's turn was ever found and every turned screen was turned in software;
        and found, the turn would have failed, since the mode was set on the scanout with the plane not turned yet (a viewport
        the space does not hold) and a refused turn fell back with no buffer to turn into. Now the card turns it; started
        again on a plane still turned it still does; and a card that refuses the turn has it turned in software, on its own
        buffer, from the origin.
*/
static int card_turn_take(int refuse, b32 fresh)
{
    b32 status;

    if (fresh)
    {
        card_fake_close();
        if (!card_fake_open(800, 1280))
            return -1;
        card_fake.rotation = 1;
    }
    card_fake.refuse_atomic = refuse;
    memory_zero(&compositor, sizeof(compositor));
    memory_zero(compositor_turns, sizeof(compositor_turns));
    compositor.card = card_fake.fd;
    compositor.scale = 100;
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    status = compositor_space_take(1);
    return (int)status;
}

static fn card_turn_drop(void)
{
    if (compositor.scanout)
        compositor_call(syscall(munmap), (positive)compositor.scanout, compositor.scanout_bytes, 0, 0, 0, 0);
    for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
        if (compositor_turns[at].pixels)
            compositor_call(syscall(munmap), (positive)compositor_turns[at].pixels, compositor_turns[at].bytes, 0, 0, 0, 0);
    if (compositor.mirror)
        compositor_call(syscall(munmap), (positive)compositor.mirror, compositor.mirror_bytes, 0, 0, 0, 0);
    memory_zero(&compositor, sizeof(compositor));
    memory_zero(compositor_turns, sizeof(compositor_turns));
    compositor.card = -1;
}

static void check_compositor_card_turn(void)
{
    static const char path[] = "compositor-card-turn.tmp", line[] = "eDP-1 rotation=90\n";
    bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, FILE_READ_WRITE | FILE_CREATE | O_TRUNC | O_CLOEXEC, 0600);
    unsigned int plane = 0, property = 0;
    unsigned long value = 0;
    int status;

    if (handle < 0)
    {
        check("card turn: the displays line is written", false);
        return;
    }
    system_write_once(handle, line, sizeof(line) - 1);
    system_close(handle);
    compositor_displays_path = path;
    memory_zero(&compositor, sizeof(compositor));
    if (!card_fake_open(800, 1280))
    {
        check("card turn: the fake card is made", false);
        return;
    }
    card_fake.rotation = 1;
    check("card turn: the plane's rotation property is read, and a quarter turn is the card's",
          compositor_rotation_hardware(0, 90, &plane, &property, &value) && plane == CARD_FAKE_PLANE && property == 12 && value == 2);

    status = card_turn_take(0, true);
    check("card turn: a screen turned a quarter is taken, the plane turning the scanout, with no mode refused",
          status == 0 && compositor.outputs == 1 && compositor.output[0].turned == 1 && card_fake.plane_rotation == 2 &&
              card_fake.crtc_fb == compositor.fb && card_fake.atomic_fb == compositor.fb && !card_fake.refused &&
              compositor.width == 1280 && compositor.height == 800);
    card_turn_drop();

    status = card_turn_take(0, false);
    check("card turn: started again on the plane still turned, it is taken turned by the card",
          status == 0 && compositor.output[0].turned == 1 && card_fake.plane_rotation == 2 && card_fake.crtc_fb == compositor.fb);
    card_turn_drop();

    status = card_turn_take(1, true);
    check("card turn: a card that refuses the turn has it turned in software, on its own buffer from the origin",
          status == 0 && compositor.output[0].turned == 2 && compositor_turns[0].pixels && compositor_turns[0].fb &&
              card_fake.crtc_fb == compositor_turns[0].fb && card_fake.crtc_x == 0 && card_fake.crtc_y == 0 &&
              compositor.output[0].fb == compositor_turns[0].fb);
    card_turn_drop();

    card_fake_close();
    compositor_displays_path = SETTING_CANVAS_DISPLAYS_PATH;
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
}

/*
        The space taken again at another size under a running desktop: a screen swapped for a larger or a smaller one, a
        game's mode, a scale, a turn. What the compositor keeps for the space (the scanout and its second plate, the
        mirror and the slices it composes apart in, a turned screen's own buffer, the bands and their glass) has to be the
        space's size by the next time anything is drawn into it: the owner's compositor died of a fill one row past a
        buffer's end, a store into the page after it (accd044, the overlay's buffers kept at an older width, 2abdbf66).
        Here the fake card's panel goes from 1280 by 720 to 1920 by 1080 and to 1024 by 768, the screen is asked for 200
        percent and is turned a quarter by Canvas, and it comes back to 1280 by 720, with three windows and the band on
        each of the four edges, written in a face, and after each a whole present as each kind of card has it (one that
        scans its buffer out: the two plates; one that copies what it is told changed: composed apart; a screen turned:
        the mirror). Every mapping the compositor makes has a page after it that nothing may touch (space_guarded), and it
        all runs in a child, so a store past a buffer's end is the child's fault and this row's red, with the step it was
        at. The overlay and a window shown straight need a plane and udmabuf the fake card has not: CHECK_gpu_kit's rows
        hold those two buffers (gpu_overlay_rows, compositor_direct_under_ready's).
*/
#define SPACE_STEPS_NAMED 9

static const char address_to const space_step_names[SPACE_STEPS_NAMED] = {
    "the first take at 1280 by 720", "the windows and the band", "1280 by 720 drawn", "taken again at 1920 by 1080",
    "1920 by 1080 drawn", "taken again at 1024 by 768", "1024 by 768 drawn", "200 percent, then turned a quarter, drawn",
    "back at 1280 by 720, drawn"};

static b32 space_displays(const char address_to path, const char address_to line)
{
    bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, FILE_READ_WRITE | FILE_CREATE | O_TRUNC | O_CLOEXEC, 0600);
    positive length = 0;

    if (handle < 0)
        return false;
    while (line[length])
        length++;
    if (length)
        system_write_once(handle, line, length);
    system_close(handle);
    return true;
}

// Every kind of card's present of the whole space, with the band on each edge: what the space's buffers are drawn into.
static fn space_draw_all(void)
{
    static const char address_to const kinds[3] = {"fake", "virtio_gpu", "amdgpu"};
    static const char address_to const edges[4] = {"bottom", "left", "top", "right"};

    for (unsigned int edge = 0; edge < 4; edge++)
    {
        // What compositor_taskbar_settings_read does with a position read: the band laid there, its memory, its tabs and the room.
        compositor_tabs_changed();
        taskbar_settings_set(edges[edge], "primary", "");
        compositor_band_room();
        compositor_tabs_changed();
        compositor_room_follow();
        for (unsigned int kind = 0; kind < 3; kind++)
        {
            memory_zero(compositor.driver, sizeof(compositor.driver));
            memory_copy(compositor.driver, kinds[kind], string_length((string_address)kinds[kind]));
            compositor_apart_drop(); // the card's kind is asked again (compositor_changed_ready), as after a take
            compositor_damage_space();
            compositor_present();
            compositor_flip_done();
        }
    }
}

// The child: the steps, each said in step before it is begun; 0 when every one was done.
static int space_session(volatile unsigned int address_to step, const char address_to path)
{
    static const char face[] = "fonts/inter/Inter-Regular.ttf";
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul, buffer;
    long mapped;
    int rooms;

    space_guarded = 1;
    *step = 0;
    if (!space_displays(path, "") || !card_fake_open(1280, 720))
        return 10;
    compositor_displays_path = path;
    compositor.scale = 100;
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.touch.closing = compositor.touch.hold_row = COMPOSITOR_NONE;
    if (compositor_space_take(1) != 0 || compositor.width != 1280 || compositor.height != 720)
        return 11;

    // The table, its buffers the session's room (compositor_buffer_room), mapped as the compositor maps its own, and three windows in it.
    *step = 1;
    buffer = compositor_buffer_room();
    mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * WINDOWS, 3, 0x22, (positive)-1, 0);
    if (mapped < 0 && mapped > -4096)
        return 12;
    compositor.table = (struct windows address_to)mapped;
    compositor.table->motion = 1;
    compositor.pixels = compositor.table->pixels = header;
    compositor.buffer = compositor.table->buffer = buffer;
    compositor.table->stride = (unsigned int)compositor_stride();
    compositor_table_outputs();
    rooms = (int)(buffer / ((unsigned long)compositor_stride() * 4));
    compositor.table->x[1] = 40, compositor.table->y[1] = 30;
    life_paint(1, 400, 300, rooms);
    compositor.table->x[2] = 900, compositor.table->y[2] = 400;
    life_paint(2, 640, 480, rooms);
    compositor.table->x[3] = 0, compositor.table->y[3] = 0;
    life_paint(3, 1280, 720, rooms);
    compositor_font((string_address)face);
    if (!compositor.band)
        return 13;

    *step = 2;
    space_draw_all();

    *step = 3;
    card_fake.width = 1920, card_fake.height = 1080;
    if (compositor_space_take(0) != 0 || compositor.width != 1920 || compositor.height != 1080)
        return 14;
    *step = 4;
    space_draw_all();

    *step = 5;
    card_fake.width = 1024, card_fake.height = 768;
    if (compositor_space_take(0) != 0 || compositor.width != 1024 || compositor.height != 768)
        return 15;
    *step = 6;
    space_draw_all();

    *step = 7;
    if (!space_displays(path, "eDP-1 scale=200\n") || compositor_space_take(0) != 0 || compositor.scale != 200)
        return 16;
    space_draw_all();
    if (!space_displays(path, "eDP-1 scale=200 rotation=90\n") || compositor_space_take(0) != 0 ||
        compositor.output[0].turned != 2 || compositor.width != 768 || compositor.height != 1024)
        return 17;
    space_draw_all();

    *step = 8;
    card_fake.width = 1280, card_fake.height = 720;
    if (!space_displays(path, "") || compositor_space_take(0) != 0 || compositor.width != 1280 || compositor.height != 720 ||
        compositor.output[0].turned)
        return 18;
    space_draw_all();
    return 0;
}

/*
        moonwater canvas record's files are looked for with one statx of their directory, and opened only when it changed since they were read
        (compositor_record_look_wanted): where every look was a mkdirat and two openats, every COMPOSITOR_RECORD_POLL presents. The directory
        moves its mtime with every file made or taken away, and one made in the same tick of the filesystem's clock as the last read leaves it
        where it was, so a read that soon after the mtime is made again.
*/
static void check_compositor_record_look(void)
{
    long long at = 1800000000ll * 1000000000ll, tick = 4000000ll;

    check("record look: a directory not there is made and read", compositor_record_look_wanted(0, 0, 0) && compositor_record_look_wanted(0, at, at));
    check("record look: a directory never read is read", compositor_record_look_wanted(at, 0, 0));
    check("record look: a directory whose mtime moved since the last read is read", compositor_record_look_wanted(at + tick, at, at + 3 * COMPOSITOR_RECORD_SETTLE_NS));
    check("record look: one that did not move, read well after its mtime, is not opened again",
          !compositor_record_look_wanted(at, at, at + COMPOSITOR_RECORD_SETTLE_NS) && !compositor_record_look_wanted(at, at, at + 60 * 1000000000ll));
    check("record look: one read in the tick of its mtime is read again, a file made after that read leaving the mtime as it was",
          compositor_record_look_wanted(at, at, at + tick / 2) && compositor_record_look_wanted(at, at, at + COMPOSITOR_RECORD_SETTLE_NS - 1));
}

static void check_compositor_space_sizes(void)
{
    static const char path[] = "compositor-space-sizes.tmp";
    volatile unsigned int address_to step;
    bipolar child, waited = 0;
    positive status = 0;
    long shared = (long)system_call_6(syscall(mmap), 0, 4096, 3, 0x21, (positive)-1, 0); // shared with the child, anonymous

    if (shared < 0 && shared > -4096)
    {
        check("space sizes: the page the child says its step in is made", false);
        return;
    }
    step = (volatile unsigned int address_to)shared;
    *step = SPACE_STEPS_NAMED;
    child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0);
    if (child == 0)
        system_call_1(syscall(exit_group), (positive)space_session(step, path));
    for (int tries = 0; child > 0 && tries < 6000 && waited != child; tries++)
    {
        timespec nap = {0, 10000000};

        waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 1, 0);
        if (waited != child)
            system_call_2(syscall(nanosleep), (positive)&nap, 0);
    }
    if (child > 0 && waited != child)
    {
        system_call_2(syscall(kill), (positive)child, 9);
        system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
    }
    checks++;
    if (child <= 0 || waited != child || (status & 0xffff) != 0)
    {
        failures++;
        string_format(log, "  FAIL space sizes: the compositor taken again at 1920 by 1080, 1024 by 768, 200 percent, turned and back, "
                           "and drawn whole each time with the band on every edge, stores nothing past a buffer it keeps for the space: "
                           "stopped at %s, %s (the child's status %p)\n",
                      (string_address)(*step < SPACE_STEPS_NAMED ? space_step_names[*step] : "its start"),
                      (string_address)((status & 0x7f) == 11 ? "a fault, a store past a buffer's end"
                                       : (status & 0x7f) ? "killed" : "a step that did not happen"), status);
        log_flush();
    }
    system_call_2(syscall(munmap), (positive)shared, 4096);
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
}

/*
        The card the compositor takes at its start (compositor_cards_walk): every minor is tried and the first card that makes
        the compositor its master and has a connected screen is the one, the real cards before a firmware card; a card that is
        not taken is closed; and with no screen on any card that could be had the answer is the one the start waits on
        (-COMPOSITOR_ENODEV), where only /dev/dri/card0 was opened and every other machine's compositor ended at once. The
        cards are the fake's (card_fake_node), a memfd behind each minor a row says is there.
*/
static b32 card_walk_begin(unsigned long nodes, unsigned long dark, unsigned long busy, unsigned long firmware)
{
    memory_zero(&compositor, sizeof(compositor));
    if (!card_fake_open(1024, 768))
        return false;
    card_fake.base = card_fake.fd;
    card_fake.nodes = nodes, card_fake.dark = dark, card_fake.busy = busy, card_fake.firmware = firmware;
    for (unsigned int at = 0; at < 64; at++)
        card_fake.node[at] = -1;
    compositor.card = compositor.uevents = -1;
    compositor.scale = 100;
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    return true;
}

// Whether a minor's last descriptor is closed (or the minor was never opened).
static b32 card_walk_closed(unsigned int minor)
{
    return card_fake.node[minor] < 0 || system_call_3(syscall(fcntl), (positive)card_fake.node[minor], 1, 0) < 0; // F_GETFD
}

static fn card_walk_end(void)
{
    int taken = compositor.card;

    // The second buffer a taken card was given is outside the compositor's own state, and is the card's to give back.
    compositor_plate_drop();
    card_turn_drop();
    if (taken >= 0 && taken != card_fake.base)
        system_close(taken);
    card_fake.fd = card_fake.base;
    card_fake_close();
}

static void check_compositor_card_walk(void)
{
    b32 status;

    if (!card_walk_begin(1ul << 3, 0, 0, 0))
    {
        check("card walk: the fake card is made", false);
        return;
    }
    status = compositor_cards_walk();
    check("card walk: a screen on card3, with no card0, is taken",
          status == 0 && compositor.card >= 0 && compositor.card == card_fake.node[3] && compositor.outputs == 1 &&
              compositor.width == 1024 && compositor.height == 768 && string_equals((string_address)compositor.driver, (string_address) "fake"));
    // The only screen unplugged and plugged back: the same connector, crtc and mode, so nothing of the desktop's changed, and the link is
    // set up only by a mode set. The crtc is turned off and then on, which is one; taken as the same screens, no SETCRTC was made at all.
    if (status == 0)
    {
        unsigned int sets;

        card_fake.dark = 1ul << 3;
        status = compositor_space_take(0);
        check("screens gone: a take that finds no screen where there was one is the answer for none, and the desktop keeps what it has",
              status == -COMPOSITOR_ENODEV && compositor.outputs == 1 && compositor.width == 1024 && compositor.screens_gone == 1);
        card_fake.dark = 0, sets = card_fake.set_count;
        status = compositor_space_take(0);
        check("screens gone: the screen that comes back is set anew, its crtc turned off and then on",
              status == 0 && compositor.outputs == 1 && !compositor.screens_gone && card_fake.set_count >= sets + 2 && sets + 1 < 32 &&
                  card_fake.set_was[sets] == 0 && card_fake.set_was[sets + 1] == 1 && card_fake.crtc_fb != 0);
        sets = card_fake.set_count;
        status = compositor_space_take(0);
        check("screens gone: and a take after that, with nothing changed, sets nothing", status == 0 && card_fake.set_count == sets);
    }
    card_walk_end();

    // A connector passed over after it was read into its slot leaves the slot empty for the next (compositor_connected_blank): the ghost's
    // encoder can have only a crtc the card has not, and left in the slot that was the panel's too, so the panel had no crtc and was no screen.
    if (card_walk_begin(1ul << 0, 0, 0, 0))
    {
        struct compositor_connected found;

        card_fake.connectors = 2, card_fake.ghost = 1;
        card_fake_node(0);
        compositor.card = card_fake.fd;
        status = compositor_connected_read(&found);
        check("connector slot: a connector passed over leaves nothing in its slot for the next one read",
              status == 0 && found.count == 1 && found.connector[0] == CARD_FAKE_CONNECTOR + 1 && found.crtc[0] == CARD_FAKE_CRTC &&
                  found.possible[0] == 0 && found.mode[0].hdisplay == 1024 && string_equals((string_address)found.name[0], (string_address) "eDP-2"));
        card_walk_end();
    }

    card_walk_begin(1ul << 0 | 1ul << 2, 1ul << 0, 0, 0);
    status = compositor_cards_walk();
    check("card walk: a card with no connected screen is passed over for the next that has one, and is not left open",
          status == 0 && compositor.card == card_fake.node[2] && card_fake.minor == 2 && card_walk_closed(0));
    card_walk_end();

    card_walk_begin(1ul << 1, 1ul << 1, 0, 0);
    status = compositor_cards_walk();
    check("card walk: no screen on the only card is the answer the start waits on, and no card is held while it waits",
          status == -COMPOSITOR_ENODEV && compositor.card == -1 && card_walk_closed(1) && !compositor.driver[0]);
    card_fake.dark = 0;
    status = compositor_cards_walk();
    check("card walk: walked again once a screen is connected, the card is taken",
          status == 0 && compositor.card == card_fake.node[1] && compositor.outputs == 1);
    card_walk_end();

    card_walk_begin(1ul << 0 | 1ul << 1, 0, 0, 1ul << 0);
    status = compositor_cards_walk();
    check("card walk: a real card is taken before the firmware's, whatever their minors",
          status == 0 && compositor.card == card_fake.node[1] && card_walk_closed(0) &&
              string_equals((string_address)compositor.driver, (string_address) "fake"));
    card_walk_end();

    card_walk_begin(1ul << 0 | 1ul << 1, 1ul << 1, 0, 1ul << 0);
    status = compositor_cards_walk();
    check("card walk: the firmware's card is taken when no real card has a screen",
          status == 0 && compositor.card == card_fake.node[0] && card_walk_closed(1) &&
              string_equals((string_address)compositor.driver, (string_address) "simpledrm"));
    check("card walk: and only the firmware's minor is opened a second time", card_fake.opens == 65 && card_fake.opened[64] == 0);
    card_walk_end();

    card_walk_begin(1ul << 0 | 1ul << 5, 0, 1ul << 0, 0);
    status = compositor_cards_walk();
    check("card walk: a card another program is the master of is passed over", status == 0 && compositor.card == card_fake.node[5] && card_walk_closed(0));
    card_walk_end();

    card_walk_begin(1ul << 0, 0, 1ul << 0, 0);
    status = compositor_cards_walk();
    check("card walk: every card another's is busy, which ends the start as it did", status == -COMPOSITOR_EBUSY && compositor.card == -1 && card_walk_closed(0));
    card_walk_end();

    card_walk_begin(1ul << 0 | 1ul << 1, 1ul << 1, 1ul << 0, 0);
    status = compositor_cards_walk();
    check("card walk: one card another's and one with no screen is a wait for the screen", status == -COMPOSITOR_ENODEV && compositor.card == -1);
    card_walk_end();

    card_walk_begin(0, 0, 0, 0);
    status = compositor_cards_walk();
    check("card walk: no card at all is no such card, and every minor was asked once", status == -COMPOSITOR_ENOENT && card_fake.opens == 64);
    card_walk_end();

    // The first take asks the kernel for what it has and makes it look at nothing; the look is owed, and a take after it looks.
    card_walk_begin(1ul << 0, 0, 0, 0);
    status = compositor_cards_walk();
    check("first take: a card the kernel has the lists of is taken with no connector made to look, and the look is owed",
          status == 0 && compositor.outputs == 1 && compositor.width == 1024 && card_fake.looks == 0 && compositor.probe_owed == 1);
    status = compositor_space_take(0);
    check("first take: the take after it looks at the connectors", status == 0 && card_fake.looks >= 1 && compositor.outputs == 1);
    card_walk_end();

    card_walk_begin(1ul << 0, 0, 0, 0);
    card_fake.unlooked = 1;
    status = compositor_cards_walk();
    check("first take: a card the kernel has never looked at is taken by looking, at once, and no look is owed",
          status == 0 && compositor.outputs == 1 && card_fake.looks >= 1 && !compositor.probe_owed);
    card_walk_end();
}

/*
        A machine that slept and woke (compositor_woke, compositor_wake_pass, compositor_space_take's regained), on the fake card. The
        wake is heard by the clocks (the boot clock's lead grew) or by the sleep verb's SIGUSR2, and the two of one wake are one. A flip
        the sleep cut off never sends its event: it is let go of as landed, with the picture it held, and the whole space is drawn. At
        the wake's passes every connector is asked again and every crtc turned off and set anew, so a monitor that came back at another
        mode is the one set; a take that waits for a flip in flight still owes the mode set; one the card refuses is looked at again
        until one is set; and a guest's card is set without an off. Before this a wake damaged the space and, at half a second, set the
        crtc off and on again with the connector and the mode it had before, whatever came of it: a refused set left the screen off.
*/
static void check_compositor_wake(void)
{
    static struct windows table;
    positive seen = 0, now;
    unsigned int sets;
    b32 status;

    check("wake: a lead of the boot clock that grew by less than COMPOSITOR_SLEPT_NS is no sleep",
          !compositor_slept(COMPOSITOR_SLEPT_NS, &seen) && seen == COMPOSITOR_SLEPT_NS);
    check("wake: one that grew by a sleep is one, and the lead is kept",
          compositor_slept(5000000000ull, &seen) && seen == 5000000000ull && !compositor_slept(5000000000ull + 1000, &seen));

    // SIGUSR2, the sleep verb's word, is read off the compositor's descriptor as itself.
    {
        bipolar child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0), waited = -1;
        positive code = 0;

        if (child == 0)
        {
            compositor.signals = compositor_signals();
            system_call_2(syscall(kill), (positive)system_call(syscall(getpid)), COMPOSITOR_SIGUSR2);
            system_call_1(syscall(exit_group), compositor_signal_taken() == COMPOSITOR_SIGUSR2 ? 0 : 3);
        }
        if (child > 0)
            waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&code, 0, 0);
        check("wake: the sleep verb's SIGUSR2 is read off the compositor's descriptor, and does not end it",
              child > 0 && waited == child && (code & 0xffff) == 0);
    }

    if (!card_walk_begin(1ul << 0, 0, 0, 0) || compositor_cards_walk() != 0)
    {
        check("wake: the fake card is taken", false);
        card_walk_end();
        return;
    }
    memory_zero(&table, sizeof(table));
    compositor.table = &table;
    compositor.watch = -1;
    compositor_table_outputs();

    // A flip in flight when the machine slept, a window's picture held for it.
    now = clock_monotonic_nanoseconds();
    compositor.flip_pending = 1, compositor.flip_issued = now - 1000, compositor.flip_row = -1;
    compositor.held_rows = 1u << 3, compositor.taken_held[3] = 9;
    compositor.damages = 0;
    compositor_woke(now, COMPOSITOR_WAKE_CLOCK);
    check("wake: the flip the sleep cut off is let go of as landed, and the picture it held is said taken",
          !compositor.flip_pending && !compositor.held_rows && table.shown[3] == 9);
    {
        int whole = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
            whole |= compositor.damage[at][0] <= 0 && compositor.damage[at][1] <= 0 && compositor.damage[at][2] >= compositor.width &&
                     compositor.damage[at][3] >= compositor.height;
        check("wake: the whole space is to be drawn, and the screens are taken again at half a second",
              whole && compositor.wake_stage == 1 && compositor.wake_at == now + COMPOSITOR_WAKE_FIRST_NS &&
                  !compositor.retrain);
    }
    compositor_woke(now + 1000, COMPOSITOR_WAKE_SIGNAL);
    check("wake: the sleep verb's word of the same wake is that wake, heard both ways",
          compositor.wakes == 1 && table.wakes == 1 && table.wake_how == (COMPOSITOR_WAKE_CLOCK | COMPOSITOR_WAKE_SIGNAL) &&
              compositor.wake_at == now + COMPOSITOR_WAKE_FIRST_NS);

    // The monitor came back at another mode. The pass asks for the take; the loop makes it once nothing holds the card (space_wanted).
    card_fake.width = 1280, card_fake.height = 720;
    compositor_wake_pass(now + COMPOSITOR_WAKE_FIRST_NS - 1);
    check("wake: nothing is taken before the pass is due", !compositor.retrain && compositor.wake_stage == 1);
    compositor_wake_pass(now + COMPOSITOR_WAKE_FIRST_NS);
    check("wake: at half a second the screens are asked for again, and the next pass is at two and a half",
          compositor.retrain && compositor.space_wanted && compositor.wake_stage == 2 &&
              compositor.wake_at == now + COMPOSITOR_WAKE_FIRST_NS + COMPOSITOR_WAKE_SECOND_NS);
    sets = card_fake.set_count;
    compositor_space_retake();
    check("wake: the connector is asked again and the screen taken at the mode it came back with",
          compositor.outputs == 1 && compositor.width == 1280 && compositor.height == 720 && compositor.output[0].mode.hdisplay == 1280 &&
              !compositor.retrain && !compositor.space_wanted && table.wake_sets == 1);
    check("wake: its crtc is turned off and then set, a mode set, showing the compositor's scanout",
          card_fake.set_count >= sets + 2 && sets + 1 < 32 && card_fake.set_was[sets] == 0 && card_fake.set_was[sets + 1] == 1 &&
              card_fake.crtc_fb == compositor.fb && compositor.on_screen == compositor.fb);

    // The second pass, with a flip in flight: the take waits for it, and the mode set is still owed.
    compositor.flip_pending = 1, compositor.flip_issued = clock_monotonic_nanoseconds(), compositor.flip_row = -1;
    compositor_wake_pass(now + COMPOSITOR_WAKE_FIRST_NS + COMPOSITOR_WAKE_SECOND_NS);
    sets = card_fake.set_count;
    status = compositor_space_take(0);
    check("wake: a take while a flip is in flight waits for it, and the wake's mode set is still owed",
          status == 0 && compositor.space_wanted && compositor.retrain && card_fake.set_count == sets && compositor.wake_stage == 0);
    compositor_flip_done();
    compositor_space_retake();
    check("wake: once it has landed the take sets the crtc anew, though nothing of the screen changed",
          !compositor.retrain && card_fake.set_count >= sets + 2 && card_fake.set_was[sets] == 0 && card_fake.set_was[sets + 1] == 1 &&
              table.wake_sets == 2);

    // A wake whose set the card refuses (a link not up yet): the screen is looked at again, as one that went, until a take sets it.
    compositor_woke(now + 60000000000ull, COMPOSITOR_WAKE_SIGNAL);
    compositor_wake_pass(compositor.wake_at);
    card_fake.refuse_sets = 8; // every set of the take: the crtc's, and the one a present makes of a flip refused three times running
    compositor_space_retake();
    card_fake.refuse_sets = 0;
    check("wake: a set the card refuses leaves the screen to be looked at again, and says so",
          compositor.screens_gone && !compositor.retrain && table.wake_refused == 1 && compositor.wakes == 2 && table.wake_how == COMPOSITOR_WAKE_SIGNAL);
    // Its present's flip was refused too, which swaps the plates: the take freed the scanout it began with, which was the spare by then.
    check("wake: and the take kept both plates it composes into mapped (a refused flip had swapped them)",
          compositor.scanout && system_call_3(syscall(msync), (positive)compositor.scanout, compositor.scanout_bytes, 1) == 0 &&
              (!compositor_spare.fb || system_call_3(syscall(msync), (positive)compositor_spare.pixels, compositor_spare.bytes, 1) == 0));
    sets = card_fake.set_count;
    compositor_space_retake();
    check("wake: the look after it sets the crtc anew, off and on",
          !compositor.screens_gone && compositor.outputs == 1 && card_fake.set_count >= sets + 2 && card_fake.set_was[sets] == 0 &&
              card_fake.set_was[sets + 1] == 1 && card_fake.crtc_fb == compositor.fb);

    // A guest's card: set again, never turned off.
    string_copy((string_address)compositor.driver, (string_address) "virtio_gpu");
    compositor.retrain = 1;
    sets = card_fake.set_count;
    compositor_space_retake();
    {
        unsigned int offs = 0;

        for (unsigned int at = sets; at < card_fake.set_count && at < 32; at++)
            offs += card_fake.set_was[at] == 0;
        check("wake: a guest's card has its crtc set again and not turned off",
              !compositor.retrain && card_fake.set_count > sets && card_fake.set_count < 32 && offs == 0);
    }
    compositor.table = 0;
    card_walk_end();
}

/*
        The wait for a screen (compositor_screen_wait) on the socket the kernel's uevents come down, here one end of a pair: a
        uevent of the drm subsystem ends it at once, one of another subsystem does not, and with none it ends at its time.
*/
static void check_compositor_screen_wait(void)
{
    static const char usb[] = "add@/devices/usb1\0ACTION=add\0SUBSYSTEM=usb";
    static const char drm[] = "change@/devices/card1\0ACTION=change\0HOTPLUG=1\0SUBSYSTEM=drm";
    b32 pair[2] = {-1, -1};
    positive began;
    int came;

    if (system_call_4(syscall(socketpair), 1, 2 | 0x80000, 0, (positive)pair) != 0) // AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC
    {
        check("screen wait: the pair of sockets is made", false);
        return;
    }
    compositor.uevents = pair[0];

    began = clock_monotonic_nanoseconds();
    came = compositor_screen_wait(20000000ull);
    check("screen wait: with nothing said it ends at its time, and not before", !came && clock_monotonic_nanoseconds() - began >= 20000000ull);

    system_write_once(pair[1], usb, sizeof(usb));
    began = clock_monotonic_nanoseconds();
    came = compositor_screen_wait(30000000ull);
    check("screen wait: what another subsystem says does not end it", !came && clock_monotonic_nanoseconds() - began >= 30000000ull);

    system_write_once(pair[1], usb, sizeof(usb));
    system_write_once(pair[1], drm, sizeof(drm));
    began = clock_monotonic_nanoseconds();
    came = compositor_screen_wait(20000000000ull);
    check("screen wait: a uevent of the drm subsystem ends it at once", came == 1 && clock_monotonic_nanoseconds() - began < 5000000000ull);

    system_close(pair[0]);
    system_close(pair[1]);
    compositor.uevents = -1;
}

/*
        The descriptor table has its room before the compositor has a thread (compositor_descriptors_room): the size the
        kernel says the table is (FDSize in /proc/self/status) is the whole of what the limit allows, up to 1024, so no
        descriptor the session opens grows a table its threads share, which is a wait for every processor (16 ms at the
        65th descriptor of every start). And the word that the desktop is up (compositor_up_say) is a line and then the
        end of the file on the descriptor given, and nothing on one that is none or the program's own three.
*/
static positive check_descriptor_table_size(void)
{
    static p8 text[4096];
    bipolar handle = system_open_at(AT_FDCWD, (string_address) "/proc/self/status", FILE_READ | O_CLOEXEC);
    bipolar got = handle < 0 ? -1 : system_read_once(handle, text, sizeof(text) - 1);
    positive size = 0;

    if (handle >= 0)
        system_close(handle);
    if (got <= 0)
        return 0;
    text[got] = 0;
    for (bipolar at = 0; at + 8 < got; at++)
        if (!memory_compare(text + at, "FDSize:", 7))
        {
            for (at += 7; text[at] == ' ' || text[at] == '\t'; at++)
                ;
            for (; text[at] >= '0' && text[at] <= '9'; at++)
                size = size * 10 + (positive)(text[at] - '0');
            break;
        }
    return size;
}

static void check_compositor_start_room(void)
{
    positive limit[2] = {0, 0}, want, size;
    b32 ends[2] = {-1, -1};
    p8 word[8];

    system_call_4(syscall(prlimit64), 0, 7, 0, (positive)limit); // RLIMIT_NOFILE
    want = limit[0] > 1023 ? 1024 : limit[0] > 511 ? 512 : limit[0] > 255 ? 256 : limit[0] > 127 ? 128 : 0;
    compositor_descriptors_room();
    size = check_descriptor_table_size();
    if (!size)
        log_direct(str("descriptor room: /proc/self/status has no FDSize here, the table's size NOT RUN\n"));
    else
        check("descriptor room: the table has the room the limit allows, up to 1024, before any thread shares it", size >= want && want >= 128);

    // O_CLOEXEC | O_NONBLOCK: a descriptor left open is a read that answers there is nothing yet, a red row and not a check that never ends.
    if (system_call_2(syscall(pipe2), (positive)ends, 0x80000 | 0x800) != 0)
    {
        check("up word: the pipe is made", false);
        return;
    }
    compositor_up_say(-1);
    compositor_up_say(1);
    check("up word: no descriptor, or one of the program's own three, is left alone", system_call_3(syscall(fcntl), 1, 1, 0) >= 0);
    compositor_up_say(ends[1]);
    check("up word: the descriptor given is told the desktop is up in a line",
          system_read_once(ends[0], word, sizeof(word)) == 3 && !memory_compare(word, "up\n", 3));
    check("up word: and is closed, so its reader has the end of the file after it", system_read_once(ends[0], word, sizeof(word)) == 0);
    system_close(ends[0]);
}

/*
        A long press on a touch screen lands on the window under the finger (compositor_touch_tick keeps its row), and the
        lift that ends it is a release posted to that row. The row was not let go when its window went, so a window that
        closed under a held finger had the lift posted to whatever program took the row next: a release it never had the
        press of, and, as every button event is, a gesture of the person's on it, which lets a row copy and paste for a
        moment (compositor_clip_gesture). Now the hold goes with its window, and the lift goes nowhere.
*/
static void check_compositor_touch_hold_gone(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int head;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.touch.closing = compositor.touch.hold_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("touch hold gone: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->motion = 1;
    table->focus = WINDOWS;
    compositor.touchscreen[0] = 1;
    compositor.screen_touch[0] = (struct compositor_touch_screen){0};
    compositor.screen_touch[0].slots = 1;
    for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        compositor.screen_touch[0].id[slot] = compositor.screen_touch[0].was[slot] = -1;
    table->x[1] = table->y[1] = 20;
    life_paint(1, 100, 80, GLASS_SIDE);

    // Held on the window in row 1, as compositor_touch_tick leaves a long press; then the window goes and the next program takes row 1.
    compositor.touch.gesture.mode = UI_TOUCH_HELD;
    compositor.touch.device = 0;
    compositor.touch.gesture.first = 0;
    compositor.touch.hold_row = 1;
    life_close(1);
    table->x[1] = table->y[1] = 20;
    life_paint(1, 100, 80, GLASS_SIDE);
    table->event_tail[1] = head = table->event_head[1];
    compositor_clip.gesture[1] = 0;

    // The finger lifts.
    compositor_touch_report(0, clock_monotonic_nanoseconds());
    check("touch hold gone: the lift of a press held on a window that went is posted to no program in its row",
          table->event_head[1] == head && compositor.touch.gesture.mode == UI_TOUCH_NONE);
    check("touch hold gone: and the next program in the row is given no gesture of the person's for the clipboard",
          compositor_clip.gesture[1] == 0);

    // The finger's device goes away while the press is held, so no lift will come: the hold ends with the device, its row let go.
    {
        int dead[2];

        compositor.touch.gesture.mode = UI_TOUCH_HELD;
        compositor.touch.device = 0;
        compositor.touch.hold_row = 1;
        compositor.touchscreen[0] = 1;
        if (system_call_2(syscall(pipe2), (positive)dead, 0) == 0)
        {
            system_close(dead[0]);
            system_close(dead[1]);
            compositor.inputs[0] = dead[0];
            compositor.input_count = 1;
            compositor_input_read(0);
        }
        check("touch hold gone: a device gone while its finger is held ends the hold: the row is let go of, and the gesture is none",
              compositor.touch.hold_row == COMPOSITOR_NONE && compositor.touch.gesture.mode == UI_TOUCH_NONE);
    }
    life_close(1);
    compositor.touchscreen[0] = 0;
}

/*
        The same sweep across the seam of two screens, A (320 by 192) and B (256 by 240) side by side: a window that
        straddles the seam is drawn on both, one put past B's edge is kept on B, and one put off both is brought back on.
*/
static void check_compositor_lifecycle_seam(void)
{
    static unsigned int scanout[SCREENS_W * SCREENS_H];
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)SCREENS_A_W * SCREENS_H * 4 + 4095) & ~4095ul;
    long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * WINDOWS, 3, 0x22, (positive)-1, 0);
    struct windows address_to table;
    struct compositor_frame was;
    char names[2][WINDOWS_OUTPUT_NAME] = {"A-1", "B-1"};

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = SCREENS_W;
    compositor.height = SCREENS_H;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = scanout;
    compositor.scanout_pitch = SCREENS_W * 4;
    compositor.outputs = 2;
    compositor.screen[0][2] = SCREENS_A_W, compositor.screen[0][3] = SCREENS_A_H;
    compositor.screen[1][0] = SCREENS_A_W, compositor.screen[1][2] = SCREENS_B_W, compositor.screen[1][3] = SCREENS_H;
    memory_copy(compositor.output[0].name, names[0], sizeof(names[0]));
    memory_copy(compositor.output[1].name, names[1], sizeof(names[1]));
    compositor.stride = compositor.most_w = SCREENS_A_W;
    compositor.room_h = compositor.most_h = SCREENS_H;
    if (mapped < 0 && mapped > -4096)
    {
        check("lifecycle seam: the table is mapped", false);
        return;
    }
    table = compositor.table = (struct windows address_to)mapped;
    table->motion = 0;
    table->focus = WINDOWS; // as the table is made: no row has the keys
    compositor.pixels = table->pixels = header;
    compositor.buffer = table->buffer = buffer;
    table->stride = SCREENS_A_W;
    compositor_table_outputs();

    table->x[1] = 280, table->y[1] = 20;
    life_paint(1, 80, 60, SCREENS_H);
    life_step("open across the seam", compositor.showing[1] != 0);
    check("lifecycle seam: a window across the seam is drawn on the screens it straddles", life_on(1));

    table->x[2] = 500, table->y[2] = 200;
    life_paint(2, 100, 100, SCREENS_H);
    life_step("open past B's bottom right", compositor.showing[2] != 0);
    check("lifecycle seam: a window past B's bottom right is kept on B", life_on(2) && compositor.x[2] >= SCREENS_A_W);

    table->x[3] = 100, table->y[3] = 100;
    life_paint(3, 400, 200, SCREENS_H);
    life_step("open wider than A", compositor.showing[3] != 0);
    check("lifecycle seam: a window wider than the widest screen is cut to it, and on the screens",
          compositor.w[3] == SCREENS_A_W && life_on(3));

    compositor_place(2, -50, 150);
    life_step("move B's window to the left, past A", compositor.showing[2] != 0);
    check("lifecycle seam: a window moved past the left of A is kept on the screens", life_on(2));
    compositor_place(2, 9000, 9000);
    life_step("move B's window off both screens", compositor.showing[2] != 0);
    check("lifecycle seam: a window moved off both screens is brought back on to B", life_on(2));
    compositor_place(3, 300, 50);
    life_step("move the wide window across the seam", compositor.showing[3] != 0);
    check("lifecycle seam: a wide window moved across the seam is on the screens", life_on(3));

    table->x[4] = 40, table->y[4] = 30;
    life_paint(4, SCREENS_B_W, SCREENS_H, SCREENS_H);
    life_step("open a window the size of B", compositor.showing[4] != 0);
    check("lifecycle seam: a window the size of B is kept on the screens, whole",
          life_on(4) && compositor.w[4] == SCREENS_B_W && compositor.h[4] == SCREENS_H);
    compositor_place(4, SCREENS_A_W, 0);
    life_step("move it to B", compositor.showing[4] != 0);
    check("lifecycle seam: and moved to B's corner it stays on the screens, whole", life_on(4) && compositor.w[4] == SCREENS_B_W);
    life_close(4);
    life_step("close the window the size of B", !compositor.showing[4]);

    compositor_focus(1);
    life_step("give the keys to the window on A", compositor.focus == 1);
    compositor_frame_of(1, &was);
    life_close(1);
    life_step("close the window on A with the keys", !compositor.showing[1]);
    check("lifecycle seam: closing it gives the keys to the top window shown, which is on the screens",
          compositor.focus == 3 && life_on(3));
    check("lifecycle seam: and it leaves none of its colour on either screen", life_ghost(1, &was) == 0);

    compositor_frame_of(2, &was);
    life_close(2);
    life_step("close the window on B", !compositor.showing[2]);
    check("lifecycle seam: and the one on B leaves none of its colour either", life_ghost(2, &was) == 0);
}

/*
        The rest of the lifecycle, on one screen: a window maximised by Alt-Up and restored by Alt-Down to where it was; one
        closed while it lands, and the next in its row landing again; one lifted back by its program out of a put away; one
        closed under an Alt-Tab, which ends the cycle; a window the size of its screen moved, which keeps its corner; and
        rows opened, moved, put away, shown and closed in no order at all, every step held as in the sweep above.
*/
static void check_compositor_lifecycle_more(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct compositor_frame was;
    unsigned int seed = 0x2545f491u;
    int x, y, width, height, under;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("lifecycle more: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->motion = 0;
    table->focus = WINDOWS;

    // Maximised by Alt-Up, and restored by Alt-Down to the rectangle it had.
    table->x[1] = 40, table->y[1] = 30;
    life_paint(1, 100, 80, GLASS_SIDE);
    compositor_focus(1);
    life_step("give the keys to a window", compositor.focus == 1);
    x = compositor.x[1], y = compositor.y[1], width = (int)compositor.w[1], height = (int)compositor.h[1];
    compositor_tile_chord(CK_KEY_UP);
    life_step("maximise with Alt-Up", compositor.arranged[1] == COMPOSITOR_MAXIMIZED);
    check("lifecycle more: a maximised window fills the screen",
          life_on(1) && compositor.w[1] == GLASS_SIDE && compositor.h[1] == GLASS_SIDE && compositor.x[1] == 0 && compositor.y[1] == 0);
    compositor_tile_chord(CK_KEY_DOWN);
    life_step("restore with Alt-Down", compositor.arranged[1] == COMPOSITOR_FLOATING);
    check("lifecycle more: restored, it is where it was and as large as it was",
          compositor.x[1] == x && compositor.y[1] == y && (int)compositor.w[1] == width && (int)compositor.h[1] == height &&
              table->x[1] == x && table->y[1] == y);

    // Closed while maximised: the next window in its row is not maximised, nor sized by the one that went.
    compositor_tile_chord(CK_KEY_UP);
    life_step("maximise it again", compositor.arranged[1] == COMPOSITOR_MAXIMIZED);
    life_close(1);
    life_step("close it maximised", !compositor.showing[1]);
    life_paint(1, 100, 80, GLASS_SIDE);
    life_step("open the next window in the row", compositor.showing[1] != 0);
    check("lifecycle more: the next window in the row is floating, at its own size",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.w[1] == 100 && compositor.h[1] == 80);
    life_close(1);
    life_step("close it again", !compositor.showing[1]);

    // Closed while it lands: the landing ends with it, and the next window in the row lands again.
    table->x[2] = 60, table->y[2] = 60;
    life_paint(2, 60, 60, GLASS_SIDE);
    check("lifecycle more: a window opened lands", compositor.landing[2] != 0);
    life_close(2);
    life_step("close it while it lands", !compositor.showing[2]);
    check("lifecycle more: the landing ends with the window", compositor.landing[2] == 0);
    life_paint(2, 60, 60, GLASS_SIDE);
    life_step("the next window in the row opens", compositor.showing[2] != 0);
    check("lifecycle more: the next window in the row lands again", compositor.landing[2] != 0);
    life_close(2);
    life_step("close it again", !compositor.showing[2]);

    // Put away, then lifted by its program: it comes back, on top and with the keys.
    table->x[3] = 80, table->y[3] = 20;
    life_paint(3, 90, 70, GLASS_SIDE);
    compositor_focus(3);
    compositor_frame_of(3, &was);
    compositor_minimize_row(3);
    life_step("put the window away", (compositor.state[3] & WINDOW_MINIMIZED) != 0);
    check("lifecycle more: a window put away leaves none of its colour on the screen", life_ghost(3, &was) == 0);
    table->lift[3] = 1;
    life_paint(3, 90, 70, GLASS_SIDE);
    life_step("the program lifts it", compositor_visible(3) != 0);
    check("lifecycle more: a lifted window comes back on top, with the keys",
          compositor.focus == 3 && compositor.stack[compositor.stacked - 1] == 3 && table->state[3] == 0);
    life_close(3);
    life_step("close the lifted window", !compositor.showing[3]);

    // Closed under an Alt-Tab: the cycle ends with it, and the keys go to a window shown.
    for (int row = 10; row < 13; row++)
    {
        table->x[row] = 20 * row - 150, table->y[row] = 20 * row - 150;
        life_paint(row, 80, 60, GLASS_SIDE);
    }
    compositor_focus(12);
    compositor_focus_step();
    life_step("step Alt-Tab", compositor.cycle != 0);
    under = compositor.focus;
    life_close(under);
    life_step("close the window Alt-Tab stopped on", !compositor.showing[under]);
    check("lifecycle more: closing it ends the cycle, and the keys are with a window shown or with none",
          compositor.cycle == 0 && (compositor.focus < 0 || compositor_visible(compositor.focus)));
    for (int row = 10; row < 13; row++)
        if (compositor.showing[row])
        {
            life_close(row);
            life_step("close the rest of the Alt-Tab rows", !compositor.showing[row]);
        }

    // A window the size of its screen is kept in its corner when it is moved to the middle of it.
    table->x[13] = 0, table->y[13] = 0;
    life_paint(13, GLASS_SIDE, GLASS_SIDE, GLASS_SIDE);
    life_step("open a window the size of the screen", compositor.showing[13] != 0);
    compositor_place(13, 50, 50);
    life_step("move it into the middle", compositor.showing[13] != 0);
    check("lifecycle more: a window the size of the screen keeps its corner on the screen",
          compositor.x[13] == 0 && compositor.y[13] == 0 && compositor.w[13] == GLASS_SIDE && compositor.h[13] == GLASS_SIDE);
    life_close(13);
    life_step("close the screen-sized window", !compositor.showing[13]);

    // Rows opened, moved, put away, shown and closed in no order, a few hundred times over.
    for (int step = 0; step < 400; step++)
    {
        int row, action;

        seed = seed * 1103515245u + 12345u;
        row = (int)((seed >> 8) % 20);
        action = (int)((seed >> 20) % 5);
        if (!compositor.showing[row])
        {
            table->x[row] = (int)((seed >> 3) % 300) - 40;
            table->y[row] = (int)((seed >> 11) % 300) - 40;
            life_paint(row, 20 + (int)((seed >> 5) % 60), 20 + (int)((seed >> 13) % 50), GLASS_SIDE);
            life_step("churn: open a row", compositor.showing[row] != 0);
        }
        else if (action == 0)
        {
            life_close(row);
            life_step("churn: close a row", !compositor.showing[row]);
        }
        else if (action == 1)
        {
            compositor_minimize_row(row);
            life_step("churn: put a row away", 1);
        }
        else if (action == 2)
        {
            compositor_reveal(row);
            life_step("churn: reveal a row", compositor_visible(row) != 0);
        }
        else if (action == 3 && compositor_visible(row))
        {
            compositor_focus(row);
            life_step("churn: give a row the keys", compositor.focus == row);
        }
        else
        {
            compositor_place(row, (int)((seed >> 4) % 5000) - 2500, (int)((seed >> 9) % 5000) - 2500);
            life_step("churn: move a row", !compositor_visible(row) || life_on(row));
        }
    }
    for (int row = 0; row < WINDOWS; row++)
        if (compositor.showing[row])
        {
            compositor_frame_of(row, &was);
            life_close(row);
            life_step("churn: close the rows left", !compositor.showing[row]);
        }
    check("lifecycle more: after the churn nothing is shown, stacked or focused",
          compositor.stacked == 0 && compositor.focus == COMPOSITOR_NONE && table->focus == WINDOWS);

    // A popup of a window, which is closed while the popup is open: the popup is asked to go, and it is no popup of the next
    // window that takes its row.
    table->x[6] = 50, table->y[6] = 50;
    life_paint(6, 120, 100, GLASS_SIDE);
    table->popup_parent[7] = 7; // named: the window it is of, counted from one
    table->popup_x[7] = 10, table->popup_y[7] = 10, table->popup_w[7] = 40, table->popup_h[7] = 30;
    life_paint(7, 40, 30, GLASS_SIDE);
    life_step("open a popup of a window", compositor.showing[7] != 0);
    check("lifecycle more: a popup is a popup of its window, and takes no keys", compositor_popup_parent[7] == 6 && compositor.focus != 7);
    life_close(6);
    life_step("close the window the popup is of", !compositor.showing[6]);
    check("lifecycle more: its popup is asked to go", (table->state[7] & WINDOW_CLOSING) != 0);
    life_paint(6, 100, 80, GLASS_SIDE);
    life_step("a new window takes the row", compositor.showing[6] != 0);
    check("lifecycle more: the popup of the window that went is no popup of the next one in its row", compositor_popup_parent[7] < 0);
    life_close(7);
    life_step("close the popup", !compositor.showing[7]);
    life_close(6);
    life_step("close the window again", !compositor.showing[6]);

    // A row whose buffer the card refused to show straight is pinned for the attempt (its program may not give its pages
    // back then), and that pin is no one's once the window is gone: the next program in the row discards as any does.
    // /dev/null stands in for udmabuf: its ioctl fails, which is the refusal the card would have given.
    {
        int devnull = (int)system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ_WRITE | O_CLOEXEC);

        if (devnull < 0)
        {
            check("lifecycle more: /dev/null opens to stand in for udmabuf", false);
            return;
        }
        table->x[5] = 40, table->y[5] = 40;
        life_paint(5, 60, 60, GLASS_SIDE);
        compositor.table_memfd = devnull + 1;
        compositor.udmabuf = devnull + 1;
        check("lifecycle more: a buffer the card refuses is pinned for the attempt", compositor_window_fb(5, 0) == 0 && table->pinned[5] == 1);
        life_close(5);
        life_step("close a row the card refused", !compositor.showing[5]);
        check("lifecycle more: a closed row is not pinned, so its next program discards its pages as any does", table->pinned[5] == 0);
        system_close(devnull);
    }
}

/*
        A window takes the keys when it first shows only when its launch was asked for: MOONWATER_ASKED=1 in the
        environment its program started with (windows_asked_take reads it and empties it, as it does a placement). A
        window its program opened on its own is raised and shows, and the keys stay where they were.
*/
static void check_compositor_asked(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static char asked_yes[] = "MOONWATER_ASKED=1", asked_no[] = "MOONWATER_ASKED=yes";
    string_address environment_yes[] = {"PATH=/bin", asked_yes, null};
    string_address environment_no[] = {asked_no, null};
    struct windows address_to table;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("asked: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    table->x[2] = 180, table->y[2] = 110, table->width[2] = 60, table->height[2] = 60;
    windows_asked_take(table, 1, environment_no);
    windows_asked_take(table, 2, environment_yes);
    check("asked: MOONWATER_ASKED=1 marks the row and is emptied in the environment, and any other value does not mark it",
          table->asked[2] == 1 && table->asked[1] == 0 && asked_yes[sizeof("MOONWATER_ASKED=") - 1] == 0 &&
              asked_no[sizeof("MOONWATER_ASKED=") - 1] == 0);

    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    check("asked: a window the program opened on its own shows and takes no keys",
          compositor.showing[1] && compositor.focus == COMPOSITOR_NONE);

    glass_paint(2, 0, 0, 60, 60, 0x404040u, true);
    compositor_present();
    check("asked: a window its program was asked to open takes the keys when it first shows",
          compositor.showing[2] && compositor.focus == 2 && table->focus == 2);
}

/*
        Super with Left or Right puts the window with the keys on the half of its screen at that side, Super with Up fills
        the screen, and Super with Down gives back the rectangle it had. Shift with Super is no tiling, and a chord the
        machine script binds is its own: the window stays where it is.
*/
static void check_compositor_tiling(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    int half = GLASS_SIDE / 2;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("tiling: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    table->asked[1] = 1;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    compositor_focus(1);
    compositor.input_count = 1;
    compositor.held[0] = CK_HELD_LSUPER;

    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("tiling: Super and Left puts the window with the keys on the left half of its screen",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.x[1] == 0 && compositor.y[1] == 0 &&
              table->want_width[1] == (unsigned int)half);
    compositor_key(0, CK_KEY_RIGHT, 1, 0);
    check("tiling: Super and Right on the left half undoes it: the rectangle the window had",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 50 && compositor.y[1] == 30 &&
              table->want_width[1] == 120 && table->want_height[1] == 80);
    compositor_key(0, CK_KEY_RIGHT, 1, 0);
    check("tiling: Super and Right puts it on the right half, its left edge at the middle",
          compositor.arranged[1] == COMPOSITOR_RIGHT && compositor.x[1] == half && table->want_width[1] == (unsigned int)half);
    compositor_key(0, CK_KEY_UP, 1, 0);
    check("tiling: Super and Up on the right half is its top quarter",
          compositor.arranged[1] == COMPOSITOR_TOP_RIGHT && compositor.x[1] == half && compositor.y[1] == 0 &&
              table->want_width[1] == (unsigned int)half && table->want_height[1] == (unsigned int)half);
    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("tiling: Super and Left on the top right quarter is the top left one", compositor.arranged[1] == COMPOSITOR_TOP_LEFT &&
                                                                                  compositor.x[1] == 0 && compositor.y[1] == 0);
    compositor_key(0, CK_KEY_DOWN, 1, 0);
    check("tiling: Super and Down on the top left quarter is the left half", compositor.arranged[1] == COMPOSITOR_LEFT &&
                                                                              table->want_height[1] == GLASS_SIDE);
    compositor_key(0, CK_KEY_DOWN, 1, 0);
    check("tiling: Super and Down on the left half is its bottom quarter",
          compositor.arranged[1] == COMPOSITOR_BOTTOM_LEFT && compositor.y[1] == half);
    compositor_key(0, CK_KEY_DOWN, 1, 0);
    check("tiling: Super and Down on the bottom quarter is nothing", compositor.arranged[1] == COMPOSITOR_BOTTOM_LEFT);
    compositor_key(0, CK_KEY_UP, 1, 0);
    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("tiling: Super and Up on it is the left half again, and Left on the left half is nothing: it stays on its screen",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.x[1] == 0);
    compositor_key(0, CK_KEY_UP, 1, 0);
    compositor_key(0, CK_KEY_UP, 1, 0);
    check("tiling: Super and Up twice from the left half is its top quarter and then the whole screen",
          compositor.arranged[1] == COMPOSITOR_MAXIMIZED && compositor.x[1] == 0 && table->want_width[1] == GLASS_SIDE &&
              table->want_height[1] == GLASS_SIDE);
    compositor_key(0, CK_KEY_DOWN, 1, 0);
    check("tiling: Super and Down on the filled window gives back the rectangle the window had before it was tiled",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 50 && compositor.y[1] == 30 &&
              table->want_width[1] == 120 && table->want_height[1] == 80);
    compositor_key(0, CK_KEY_DOWN, 1, 0);
    check("tiling: Super and Down on a floating window is nothing: it is not put away",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 50 && !(compositor.state[1] & WINDOW_MINIMIZED) &&
              compositor_visible(1));

    compositor.held[0] = CK_HELD_LSUPER | CK_HELD_LSHIFT;
    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("tiling: Shift with Super and Left is no tiling, and with one screen moves nothing",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 50);
    compositor.held[0] = CK_HELD_LSUPER;

    compositor_keys.count = 1;
    compositor_keys.code[0] = CK_KEY_LEFT;
    compositor_keys.modifiers[0] = WINDOW_KEY_SUPER;
    string_copy((string_address)compositor_keys.chord[0], (string_address) "key_super_left");
    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("tiling: a chord the machine script binds is the script's, and the window does not move",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 50);
    compositor_keys.count = 0;
}

/*
        A snap leaves the others on its screen their share. A window that snaps to a half or a quarter takes that region, and
        a tile (half or quarter) of another window on the screen that the region covers is given the cells it leaves: a half
        that loses its top quarter becomes its bottom quarter. A tile the region covers wholly floats back to the rectangle
        its window had. A tile below a window's least is cut to the least at its corner, and the rest of the tile stays free.
        A maximized window is not a tile. Super with Left or Right and a drop on the same edge give the same rectangles.
*/
#define LAYOUT_SIDE 512
static unsigned int layout_pixels[LAYOUT_SIDE * LAYOUT_SIDE];

static unsigned int address_to layout_table_map(int side)
{
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)side * side * 4 + 4095) & ~4095ul;
    long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * WINDOWS, 3, 0x22, (positive)-1, 0);

    if (mapped < 0 && mapped > -4096)
        return null;
    compositor.table = (struct windows address_to)mapped;
    compositor.table->motion = 1;
    compositor.pixels = compositor.table->pixels = header;
    compositor.buffer = compositor.table->buffer = buffer;
    compositor.table->stride = compositor.table->output_width = compositor.table->output_height = (unsigned int)side;
    return (unsigned int address_to)mapped;
}

// A compositor of one screen side by side at scale, with no window and the Super key held for the chords.
static void layout_setup(int side, int scale)
{
    struct windows address_to table = compositor.table;

    memory_zero(&compositor, sizeof(compositor));
    compositor.table = table;
    compositor.pixels = table->pixels;
    compositor.buffer = table->buffer;
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.shared_row = compositor.mark_row = compositor.preview_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = (unsigned int)side;
    compositor.scale = scale * 100;
    compositor.card = -1;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.scanout = layout_pixels;
    compositor.scanout_pitch = (unsigned int)side * 4;
    compositor.stride = compositor.most_w = (unsigned int)side;
    compositor.room_h = compositor.most_h = (unsigned int)side;
    compositor.input_count = 1;
    compositor.held[0] = CK_HELD_LSUPER;
}

// A window shown at x, y by width and height, floating: its rectangle is the one a snap keeps for it to come back to. It has no tab: the
// tabs of the band are centred on it, so on a screen this small one would sit in its middle, over the pixels and under the fingers these
// checks read, and what they are about is where windows go.
static void layout_show(int row, int x, int y, int width, int height)
{
    compositor.showing[row] = 1;
    compositor.asks[row] |= WINDOW_NO_TAB;
    compositor.table->pid[row] = (unsigned int)row;
    compositor.x[row] = compositor.table->x[row] = (unsigned int)x;
    compositor.y[row] = compositor.table->y[row] = (unsigned int)y;
    compositor.w[row] = compositor.table->width[row] = (unsigned int)width;
    compositor.h[row] = compositor.table->height[row] = (unsigned int)height;
    compositor.table->want_width[row] = (unsigned int)width;
    compositor.table->want_height[row] = (unsigned int)height;
    compositor.saved_x[row] = (unsigned int)x, compositor.saved_y[row] = (unsigned int)y;
    compositor.saved_w[row] = (unsigned int)width, compositor.saved_h[row] = (unsigned int)height;
    if (compositor.stacked < WINDOWS)
        compositor.stack[compositor.stacked++] = row;
}

// Whether row is arranged as how, at x, y, and asked for width by height.
static int layout_is(int row, unsigned int how, int x, int y, int width, int height)
{
    return compositor.arranged[row] == how && compositor.x[row] == (unsigned int)x && compositor.y[row] == (unsigned int)y &&
           compositor.table->want_width[row] == (unsigned int)width && compositor.table->want_height[row] == (unsigned int)height;
}

// A window is dropped by its top: the press at its top left plus from, a move to x, y and the let go there.
static void layout_drop(int row, int from_x, int from_y, int x, int y)
{
    screens_hand((int)compositor.x[row] + from_x, (int)compositor.y[row] + from_y, 1);
    screens_hand(x, y, 0);
    screens_hand(x, y, -1);
    compositor.press = COMPOSITOR_NONE;
}

// The chord Super with key on the focused window, as the keys make it.
static void layout_chord(int row, unsigned int key)
{
    compositor_focus(row);
    compositor_key(0, key, 1, 0);
}

static void check_compositor_layout(void)
{
    int chord[2][4], drop[2][4];

    /* A half, then a quarter on the other half: the half is cut to the quarter it has left. */
    if (!layout_table_map(256))
    {
        check("layout: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    layout_show(1, 60, 60, 120, 80);
    layout_show(2, 150, 150, 100, 80);
    layout_show(3, 20, 20, 100, 100);
    layout_chord(1, CK_KEY_LEFT);
    compositor_arrange(2, COMPOSITOR_TOP_LEFT);
    check("layout: a quarter snapped on the half it was on leaves the half its bottom quarter, and the quarter is its own",
          layout_is(1, COMPOSITOR_BOTTOM_LEFT, 0, 128, 128, 128) && layout_is(2, COMPOSITOR_TOP_LEFT, 0, 0, 128, 128));
    check("layout: a window floating on the other half is not moved by a quarter snapped on the first half",
          compositor.arranged[3] == COMPOSITOR_FLOATING && compositor.x[3] == 20 && compositor.y[3] == 20);

    /* A half snapped over the quarter of the half's window: the window's cells are all taken, so it floats back. */
    compositor_arrange(2, COMPOSITOR_LEFT);
    check("layout: a half over the quarter that was the other window's floats that window back to its own rectangle",
          layout_is(1, COMPOSITOR_FLOATING, 60, 60, 120, 80) && compositor.arranged[2] == COMPOSITOR_LEFT);

    /* A maximized window is not a tile: a half snapped beside it leaves it maximized. */
    layout_setup(256, 1);
    layout_show(1, 0, 0, 256, 256);
    layout_show(2, 40, 40, 100, 100);
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    compositor_arrange(2, COMPOSITOR_LEFT);
    check("layout: a maximized window is not a tile, and a half snapped on it leaves it maximized",
          layout_is(1, COMPOSITOR_MAXIMIZED, 0, 0, 256, 256));

    /* The chord and a drop on the same edges give the same rectangles. */
    layout_setup(256, 1);
    layout_show(1, 60, 60, 120, 80);
    layout_show(2, 150, 150, 100, 80);
    layout_chord(1, CK_KEY_LEFT);
    layout_chord(2, CK_KEY_RIGHT);
    for (int row = 1; row <= 2; row++)
        chord[row - 1][0] = (int)compositor.x[row], chord[row - 1][1] = (int)compositor.y[row],
        chord[row - 1][2] = (int)compositor.w[row], chord[row - 1][3] = (int)compositor.h[row];
    layout_setup(256, 1);
    layout_show(1, 60, 60, 120, 80);
    layout_show(2, 150, 150, 100, 80);
    layout_drop(1, 20, 4, 0, 100);
    layout_drop(2, 20, 4, 255, 100);
    for (int row = 1; row <= 2; row++)
        drop[row - 1][0] = (int)compositor.x[row], drop[row - 1][1] = (int)compositor.y[row],
        drop[row - 1][2] = (int)compositor.w[row], drop[row - 1][3] = (int)compositor.h[row];
    check("layout: Super with Left and Right and a drop on the same edges make the same rectangles",
          !memory_compare(chord, drop, sizeof(chord)) && layout_is(1, COMPOSITOR_LEFT, 0, 0, 128, 256) &&
              layout_is(2, COMPOSITOR_RIGHT, 128, 0, 128, 256));

    /* A drop in the top left corner is a quarter, and the half that was there keeps its bottom quarter. */
    layout_setup(256, 1);
    layout_show(1, 60, 60, 120, 80);
    layout_show(2, 150, 150, 100, 80);
    layout_chord(1, CK_KEY_LEFT);
    layout_drop(2, 20, 4, 0, 0);
    check("layout: a drop in the top left corner is that quarter, and the half on the left is cut to its bottom quarter",
          layout_is(2, COMPOSITOR_TOP_LEFT, 0, 0, 128, 128) && layout_is(1, COMPOSITOR_BOTTOM_LEFT, 0, 128, 128, 128));

    /* At 2x the same rectangles are in device pixels. */
    if (!layout_table_map(LAYOUT_SIDE))
    {
        check("layout: the 2x table is mapped", false);
        return;
    }
    layout_setup(LAYOUT_SIDE, 2);
    layout_show(1, 120, 120, 240, 160);
    layout_show(2, 300, 300, 200, 160);
    layout_chord(1, CK_KEY_LEFT);
    layout_chord(2, CK_KEY_RIGHT);
    check("layout at 2x: a half is 256 device pixels across, and the other half is the other window's",
          layout_is(1, COMPOSITOR_LEFT, 0, 0, 256, 512) && layout_is(2, COMPOSITOR_RIGHT, 256, 0, 256, 512));
    layout_setup(LAYOUT_SIDE, 2);
    layout_show(1, 120, 120, 240, 160);
    layout_show(2, 300, 300, 200, 160);
    layout_chord(1, CK_KEY_LEFT);
    compositor_arrange(2, COMPOSITOR_TOP_LEFT);
    check("layout at 2x: a quarter on a half leaves the half its bottom quarter, 256 device pixels high",
          layout_is(1, COMPOSITOR_BOTTOM_LEFT, 0, 256, 256, 256) && layout_is(2, COMPOSITOR_TOP_LEFT, 0, 0, 256, 256));

    /* A screen so small that a quarter is under the least a window is: the half is cut to the least at its corner. */
    if (!layout_table_map(160))
    {
        check("layout: the small table is mapped", false);
        return;
    }
    layout_setup(160, 1);
    layout_show(1, 30, 30, 60, 60);
    layout_show(2, 120, 120, 30, 30);
    layout_chord(1, CK_KEY_LEFT);
    compositor_arrange(2, COMPOSITOR_TOP_LEFT);
    check("layout: a quarter under the least a window is cut to the least at its corner, and the rest of its tile is free",
          layout_is(1, COMPOSITOR_BOTTOM_LEFT, 0, 80, WINDOW_MIN_WIDTH, 80) && compositor.table->want_width[1] == WINDOW_MIN_WIDTH);
}

/*
        A window pushed to an edge snaps there by its own edge, not only by the pointer: a window within 8 px of a free edge
        of its screen takes that edge's band, while the pointer is 20 px or more from it. The window starts at 60, 60 and is
        grabbed 20 px in from its top left; the pointer goes to the target and the window follows it. A window away from every
        edge snaps to nothing, and the preview is shown while the window is pushed and not after it is pulled back.
*/
static void push_drag(int x0, int y0, int to_x, int to_y, unsigned int *how, int *shown)
{
    layout_setup(256, 1);
    layout_show(1, x0, y0, 100, 80);
    screens_hand(x0 + 20, y0 + 20, 1);
    screens_hand(to_x, to_y, 0);
    *shown = compositor.preview_row == 1 && compositor.preview[2] > 0;
    screens_hand(to_x, to_y, -1);
    compositor.press = COMPOSITOR_NONE;
    *how = compositor.arranged[1];
}

static void check_compositor_push(void)
{
    unsigned int how;
    int shown;

    if (!layout_table_map(256))
    {
        check("push: the table is mapped", false);
        return;
    }

    /* The grab is 20 px in from the window's top left, so the pointer is 20 px past the window's edge it is pushed to. */
    push_drag(60, 60, 20, 100, &how, &shown);
    check("push: a window pushed to the left edge, touching it, snaps to the left half",
          how == COMPOSITOR_LEFT && shown);
    push_drag(60, 60, 176, 100, &how, &shown);
    check("push: a window pushed to the right edge, touching it, snaps to the right half",
          how == COMPOSITOR_RIGHT && shown);
    push_drag(60, 60, 80, 20, &how, &shown);
    check("push: a window pushed to the top edge, touching it, is maximized", how == COMPOSITOR_MAXIMIZED && shown);
    push_drag(60, 60, 80, 196, &how, &shown);
    check("push: a window pushed to the bottom edge, touching it, is no target: the bottom alone is not one",
          how == COMPOSITOR_FLOATING && !shown);
    push_drag(60, 60, 30, 20, &how, &shown);
    check("push: a window pushed into the top left corner, its top touching and its left 10 px off, takes that quarter",
          how == COMPOSITOR_TOP_LEFT && shown);
    push_drag(60, 60, 100, 120, &how, &shown);
    check("push: a window away from every edge snaps to nothing, and shows no preview", how == COMPOSITOR_FLOATING && !shown);
}

/*
        Snapping across two displays, in both arrangements: the second display to the right of the first (A 320 by 192 at the
        left, B 256 by 240 beside it) and the second to its left (the primary 320 by 192 at the right, the second 256 by 240).
        A window snaps onto any free edge of either screen, the far edge of the second included: each edge no other screen
        is beyond. The edge where the two meet is no edge, and a drop on it arranges nothing. The window starts on the primary
        and is grabbed 20 px in from its top left. It starts on the seam: its right edge on the primary's left one in layout 0,
        its left edge on the primary's right one in layout 1.
*/
static const int seam_screens[2][2][4] = {{{0, 0, 320, 192}, {320, 0, 256, 240}}, {{256, 0, 320, 192}, {0, 0, 256, 240}}};

// A drag of the window from x0, y0 by its top left (grabbed 20 px in) to to_x, to_y, and the let go there.
static void seam_drop(int x0, int y0, int to_x, int to_y)
{
    screens_hand(x0 + 20, y0 + 20, 1);
    screens_hand(to_x, to_y, 0);
    screens_hand(to_x, to_y, -1);
    compositor.press = COMPOSITOR_NONE;
}

// The window back on the screen at x0, y0 as it was floating, ready for the next drop.
static void seam_reset(int x0, int y0)
{
    compositor_float(1);
    compositor.x[1] = compositor.table->x[1] = x0, compositor.y[1] = compositor.table->y[1] = y0;
    compositor.saved_x[1] = x0, compositor.saved_y[1] = y0;
}

static void check_compositor_seam(void)
{
    static unsigned int scanout[SCREENS_W * SCREENS_H];
    struct windows address_to table;
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)SCREENS_A_W * SCREENS_H * 4 + 4095) & ~4095ul;
    long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * WINDOWS, 3, 0x22, (positive)-1, 0);
    char names[2][WINDOWS_OUTPUT_NAME] = {"A-1", "B-1"};
    int part[4];

    if (mapped < 0 && mapped > -4096)
    {
        check("seam: the table is mapped", false);
        return;
    }
    for (int layout = 0; layout < 2; layout++)
    {
        int start_x = layout ? 256 : 220, start_y = 20;

        memory_zero(&compositor, sizeof(compositor));
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.preview_row = COMPOSITOR_NONE;
        compositor.shared_row = compositor.mark_row = COMPOSITOR_NONE;
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
        compositor.width = SCREENS_W;
        compositor.height = SCREENS_H;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = scanout;
        compositor.scanout_pitch = SCREENS_W * 4;
        compositor.outputs = 2;
        for (int screen = 0; screen < 2; screen++)
                memory_copy(compositor.screen[screen], seam_screens[layout][screen], sizeof(compositor.screen[screen])),
                memory_copy(compositor.output[screen].name, names[screen], sizeof(names[screen]));
        compositor.stride = compositor.most_w = SCREENS_A_W;
        compositor.room_h = compositor.most_h = SCREENS_H;
        table = compositor.table = (struct windows address_to)mapped;
        table->motion = 1;
        compositor.pixels = table->pixels = header;
        compositor.buffer = table->buffer = buffer;
        table->stride = SCREENS_A_W;
        compositor_table_outputs();
        compositor.input_count = 1;

        compositor.showing[1] = 1;
        table->pid[1] = 1;
        compositor.stack[0] = 1, compositor.stacked = 1;
        table->width[1] = 100, table->height[1] = 80;
        compositor.w[1] = 100, compositor.h[1] = 80;
        table->want_width[1] = 100, table->want_height[1] = 80;
        seam_reset(start_x, start_y);

        /* Dragged onto the far edges of the second display, and onto the free edges of the primary. Layout 0 has the second
           to the right, layout 1 to the left. */
        if (layout == 0)
        {
            seam_drop(start_x, start_y, SCREENS_W - 1, 100);
            check("seam: a window dragged onto the far right edge of the second display takes its right half",
                  compositor.arranged[1] == COMPOSITOR_RIGHT && compositor_screen_of(1) == 1 && compositor.x[1] == SCREENS_A_W + SCREENS_B_W / 2 &&
                      compositor.y[1] == 0);
        }
        else
        {
            seam_drop(start_x, start_y, 0, 100);
            check("seam: a window dragged onto the far left edge of the second display takes its left half",
                  compositor.arranged[1] == COMPOSITOR_LEFT && compositor_screen_of(1) == 1 && compositor.x[1] == 0 && compositor.y[1] == 0);
        }

        seam_reset(start_x, start_y);
        seam_drop(start_x, start_y, layout ? 40 : 360, 3);
        compositor_screen_part(compositor.screen[1], COMPOSITOR_MAXIMIZED, part);
        check("seam: onto the far top edge of the second display it is maximized there",
              compositor.arranged[1] == COMPOSITOR_MAXIMIZED && compositor_screen_of(1) == 1 && compositor.x[1] == part[0] && compositor.y[1] == part[1]);

        seam_reset(start_x, start_y);
        seam_drop(start_x, start_y, layout ? 0 : SCREENS_W - 1, 237);
        compositor_screen_part(compositor.screen[1], layout ? COMPOSITOR_BOTTOM_LEFT : COMPOSITOR_BOTTOM_RIGHT, part);
        check("seam: into the far bottom corner of the second display it takes that quarter there",
              compositor.arranged[1] == (layout ? COMPOSITOR_BOTTOM_LEFT : COMPOSITOR_BOTTOM_RIGHT) && compositor_screen_of(1) == 1 &&
                  compositor.y[1] == part[1] && compositor.x[1] == part[0]);

        seam_reset(start_x, start_y);
        seam_drop(start_x, start_y, layout ? SCREENS_W - 3 : 2, 100);
        compositor_screen_part(compositor.screen[0], layout ? COMPOSITOR_RIGHT : COMPOSITOR_LEFT, part);
        check("seam: onto the free outer edge of the primary, which the second display is not beyond, it takes that half",
              compositor.arranged[1] == (layout ? COMPOSITOR_RIGHT : COMPOSITOR_LEFT) && compositor_screen_of(1) == 0 &&
                  compositor.x[1] == part[0]);

        seam_reset(start_x, start_y);
        seam_drop(start_x, start_y, layout ? 400 : 100, SCREENS_A_H - 3);
        check("seam: onto the bottom edge of the first display alone, it is no target and stays on that display",
              compositor.arranged[1] == COMPOSITOR_FLOATING && compositor_screen_of(1) == 0);

        seam_reset(start_x, start_y);
        seam_drop(start_x, start_y, layout ? SCREENS_W - 1 : 2, SCREENS_A_H - 3);
        compositor_screen_part(compositor.screen[0], layout ? COMPOSITOR_BOTTOM_RIGHT : COMPOSITOR_BOTTOM_LEFT, part);
        check("seam: into the outer bottom corner of the first display, which no screen is under, it takes that quarter",
              compositor.arranged[1] == (layout ? COMPOSITOR_BOTTOM_RIGHT : COMPOSITOR_BOTTOM_LEFT) && compositor_screen_of(1) == 0 &&
                  compositor.y[1] == part[1] && compositor.x[1] == part[0]);

        seam_reset(start_x, start_y);
        seam_drop(start_x, start_y, layout ? 258 : SCREENS_A_W - 2, 100);
        check("seam: a drop on the edge where the displays meet arranges nothing", compositor.arranged[1] == COMPOSITOR_FLOATING);
    }
}

/*
        The snap assist: a drop on a snap target leaves the windows floating on its screen as thumbnails in the space it leaves,
        inside that space and not over the window that took the region; a click on one places that window in the tile it is
        over, and a click elsewhere or Escape takes them away. While a drag is over a target they show before the drop.
        The top edge's grid: its tiles are along the top band, the grid stays up while the pointer is in its row, a drop on a
        region takes it, and a region a window already fills is taken and is no target.
*/
static void assist_fresh(void)
{
    layout_setup(256, 1);
    layout_show(1, 60, 60, 120, 80);
    layout_show(2, 150, 150, 60, 60);
    layout_show(3, 10, 10, 50, 50);
    compositor.stack[0] = 1, compositor.stack[1] = 2, compositor.stack[2] = 3, compositor.stacked = 3;
}

// The pointer of a drag rested in the top band for the grid's time: its rest started that long ago, and the loop's wake for it.
static void grid_dwell(void)
{
    compositor.grid_since = clock_monotonic_nanoseconds() - COMPOSITOR_GRID_DWELL_NS - 1;
    compositor_snap_tick(clock_monotonic_nanoseconds());
}

/*
        The grid waits for a rest and never catches a drag on its way to another screen: two screens one above the other,
        the upper's bottom against the lower's top, and a window dragged up from the lower with its pointer in the lower's
        top band. That band is a seam, no free top: no grid however long the pointer rests there, and no wake is asked for.
*/
static void check_compositor_grid_seam(void)
{
    if (!layout_table_map(256))
    {
        check("grid seam: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 128, compositor.screen[0][2] = 256, compositor.screen[0][3] = 128;
    compositor.screen[1][0] = 0, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 128;
    layout_show(1, 60, 160, 100, 40);
    compositor.stack[0] = 1, compositor.stacked = 1;
    screens_hand(80, 164, 1);
    screens_hand(127, 130, 0);
    check("grid seam: the pointer in the lower screen's top band, against the upper screen, brings no grid and asks no wake",
          !compositor.grid_on && !compositor_snap_due());
    grid_dwell();
    check("grid seam: rested there past the grid's time, still no grid", !compositor.grid_on);
    screens_hand(127, 2, 0);
    check("grid seam: on up to the upper screen's free top band, the rest starts there", !compositor.grid_on && compositor_snap_due() > 0);
    grid_dwell();
    check("grid seam: and the grid comes up on the upper screen after it", compositor.grid_on && compositor.grid_screen == 1);
    compositor.held[0] |= CK_HELD_LCONTROL;
    screens_hand(127, 3, 0);
    check("grid seam: Control held takes the grid away (no snap at all)", !compositor.grid_on);
    compositor.held[0] &= ~CK_HELD_LCONTROL;
    screens_hand(127, 3, -1);
    compositor.press = COMPOSITOR_NONE;
}

static int assist_row_at(int row)
{
    for (int at = 0; at < compositor.assist_count; at++)
        if (compositor.assist_rows[at] == (unsigned int)row)
            return at;
    return -1;
}

static void check_compositor_assist(void)
{
    int at, inside = 1;

    if (!layout_table_map(256))
    {
        check("assist: the table is mapped", false);
        return;
    }

    /* A drop on the left edge leaves the right half to the two floating windows, as thumbnails in it. */
    assist_fresh();
    layout_drop(1, 20, 4, 0, 100);
    for (at = 0; at < compositor.assist_count; at++)
    {
        const int *r = compositor.assist_rect[at];

        inside = inside && r[0] >= 128 && r[0] + r[2] <= 256 && r[1] >= 0 && r[1] + r[3] <= 256 && r[2] > 0 && r[3] > 0;
    }
    check("assist: a drop on the left edge arms the assist with the other two floating windows as thumbnails",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.assist_armed && compositor.assist_on && compositor.assist_count == 2 &&
              assist_row_at(3) == 0 && assist_row_at(2) == 1);
    check("assist: each thumbnail is inside the space the drop left, and not over the window that took the left half", inside);

    /* Presented, a thumbnail has the window's 1 px line in the snap's colour, and its corners rounded: the pixel just outside
       the middle of its left edge is the line, and the one just outside its top left corner is the ground. */
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    at = assist_row_at(2);
    check("assist: presented, a thumbnail has the window's 1 px line round it in the snap's colour, and its corner is rounded",
          compositor.assist_count == 2 &&
              (layout_pixels[(compositor.assist_rect[at][1] + compositor.assist_rect[at][3] / 2) * 256 + compositor.assist_rect[at][0] - 1] &
               0xffffffu) == (compositor_snap_ink() & 0xffffffu) &&
              (layout_pixels[(compositor.assist_rect[at][1] - 1) * 256 + compositor.assist_rect[at][0] - 1] & 0xffffffu) ==
                  (compositor_ground & 0xffffffu));

    /* A click on a thumbnail places its window in the space over it; the assist is gone. */
    at = assist_row_at(2);
    screens_hand(compositor.assist_rect[at][0] + 2, compositor.assist_rect[at][1] + 2, 0);
    compositor_press(COMPOSITOR_BTN_LEFT, 1);
    check("assist: a click on a thumbnail places that window in the space it is over, the right half",
          compositor.arranged[2] == COMPOSITOR_RIGHT && compositor.x[2] == 128 && compositor.y[2] == 0 &&
              compositor.table->want_width[2] == 128 && !compositor.assist_armed && !compositor.assist_on);
    compositor_press(COMPOSITOR_BTN_LEFT, 0);

    /* Over the target while the drag is held, only the preview shows: the thumbnails are offered after the drop. */
    assist_fresh();
    screens_hand(80, 64, 1);
    screens_hand(0, 100, 0);
    check("assist: a drag held over the left edge shows the preview and no thumbnail; the assist waits for the drop",
          !compositor.assist_on && !compositor.assist_armed && compositor.assist_count == 0 && compositor.preview[2] == 128);
    screens_hand(0, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    check("assist: and the drop offers them", compositor.assist_armed && compositor.assist_count == 2);

    /* Any key that is not a modifier takes the assist away; a modifier alone does not. */
    assist_fresh();
    layout_drop(1, 20, 4, 0, 100);
    compositor_key(0, 42, 1, 0); /* left Shift */
    compositor_key(0, 42, 0, 0);
    check("assist: a modifier alone keeps the thumbnails", compositor.assist_armed && compositor.assist_count == 2);
    compositor_key(0, 30, 1, 0); /* A */
    compositor_key(0, 30, 0, 0);
    check("assist: any other key takes the thumbnails away and places nothing",
          !compositor.assist_armed && !compositor.assist_on && compositor.arranged[2] == COMPOSITOR_FLOATING &&
              compositor.arranged[3] == COMPOSITOR_FLOATING);

    /* A click elsewhere takes the assist away, and the press is still the window's own. */
    assist_fresh();
    layout_drop(1, 20, 4, 0, 100);
    compositor_press(COMPOSITOR_BTN_LEFT, 1);
    check("assist: a click elsewhere takes the thumbnails away and places nothing",
          !compositor.assist_armed && !compositor.assist_on && compositor.arranged[2] == COMPOSITOR_FLOATING);
    compositor_press(COMPOSITOR_BTN_LEFT, 0);

    /* Escape takes it away. */
    assist_fresh();
    layout_drop(1, 20, 4, 0, 100);
    compositor_key(0, CK_KEY_ESC, 1, 0);
    check("assist: Escape takes the thumbnails away and places nothing",
          !compositor.assist_armed && !compositor.assist_on && compositor.arranged[2] == COMPOSITOR_FLOATING && compositor.arranged[3] == COMPOSITOR_FLOATING);

    /* A drop with nothing else floating leaves no assist. */
    assist_fresh();
    compositor_float(2), compositor_float(3);
    compositor.showing[2] = compositor.showing[3] = 0;
    layout_drop(1, 20, 4, 0, 100);
    check("assist: a drop with no other window floating arms nothing", !compositor.assist_armed && compositor.assist_count == 0);

    /* The grid: the pointer resting in the top band brings it up after its time, and the pointer in its row keeps it. */
    assist_fresh();
    screens_hand(80, 64, 1);
    screens_hand(127, 4, 0);
    check("grid: the pointer come to the top edge does not bring the grid at once: the window thrown there shows the fill",
          !compositor.grid_on && compositor.preview[2] == 256 && compositor_snap_due() > 0);
    grid_dwell();
    check("grid: rested in the top band for its time, the grid comes up on that screen with no motion, hovering the top left quarter",
          compositor.grid_on && compositor.grid_screen == 0 && compositor.grid_hover == 3 && !compositor_snap_due());
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    check("grid: presented, the top left tile's mini screen is tinted where the quarter is",
          (layout_pixels[5 * 256 + 117] & 0xffffffu) != (compositor_ground & 0xffffffu));
    screens_hand(127, 15, 0);
    check("grid: the pointer moved down into the row of tiles keeps it up", compositor.grid_on && compositor.grid_hover == 3);
    screens_hand(127, 60, 0);
    check("grid: the pointer moved down out of the row takes it away", !compositor.grid_on);
    screens_hand(127, 4, 0);
    check("grid: back in the band, the rest starts again: no grid yet", !compositor.grid_on);
    grid_dwell();
    screens_hand(127, 4, -1);
    compositor.press = COMPOSITOR_NONE;
    check("grid: a drop on the top left tile takes the top left quarter",
          compositor.arranged[1] == COMPOSITOR_TOP_LEFT && compositor.x[1] == 0 && compositor.y[1] == 0 &&
              compositor.table->want_width[1] == 128 && compositor.table->want_height[1] == 128 && !compositor.grid_on);

    /* A region a window fills is taken: the top band's full tile is no target while the top left is filled, and the right
       half is still one. */
    assist_fresh();
    layout_drop(1, 20, 4, 0, 0);
    compositor.grid_on = 1, compositor.grid_screen = 0;
    {
        int box[4] = {0, 0, 50, 50}, screen;
        unsigned int touch;

        check("grid: a region a window already fills is taken and is no target, and a free one is",
              compositor_target(2, 40, 4, box, &screen, &touch) == COMPOSITOR_FLOATING &&
                  compositor_target(2, 98, 4, box, &screen, &touch) == COMPOSITOR_RIGHT);
    }
    compositor.grid_on = 0;
}

/* Every pixel of a window's picture, both buffers, in one colour: what a tile's edge and corners are read against. */
static void layout_paint(int row, unsigned int colour)
{
    unsigned int front = compositor.front[row];

    for (unsigned int which = 0; which < 2; which++)
    {
        unsigned int *pixels;

        compositor.front[row] = which;
        pixels = compositor_pixels(row);
        for (unsigned int y = 0; y < compositor.h[row]; y++)
            for (unsigned int x = 0; x < compositor.w[row]; x++)
                pixels[y * (unsigned int)compositor_stride() + x] = colour;
    }
    compositor.front[row] = front;
}

/*
        A tile is drawn as a window is in its line and as a maximized one is in its corners: the line round it is the window's
        1 px line, outside its picture on the inner sides (the screen's own sides are off the output), and its corners are
        square. The left half of a 256 px screen: its right line is the column past its picture and runs the whole height
        (the line's own corners are square too), its top left corner is its picture, and a point 12 px in is its picture.
*/
static void check_compositor_tile_line(void)
{
    const unsigned int picture = 0xc06020u, ground = compositor_ground;
    unsigned int line_at, corner_at, inside_at, beyond_at;

    assist_fresh();
    compositor.showing[2] = compositor.showing[3] = 0; /* the others would cover the tile's corner */
    compositor_arrange(1, COMPOSITOR_LEFT);
    layout_paint(1, picture);
    screens_hand(250, 250, 0); /* the pointer's own picture is not the tile's */
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    line_at = layout_pixels[100 * 256 + 128] & 0xffffffu;
    beyond_at = layout_pixels[100 * 256 + 129] & 0xffffffu;
    corner_at = layout_pixels[0] & 0xffffffu;
    inside_at = layout_pixels[12 * 256 + 12] & 0xffffffu;
    check("tile line: a half is drawn with the window's 1 px line on its inner side, and nothing past it",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.w[1] == 128 &&
              line_at == (compositor_line_unlit() & 0xffffffu) && beyond_at == (ground & 0xffffffu));
    check("tile corners: a half's corners are square: the corner pixel is its picture, the same as 12 px in",
          corner_at == picture && inside_at == picture);
    check("tile corners: and so are the corners of its line, which runs the whole height of its inner side",
          (layout_pixels[0 * 256 + 128] & 0xffffffu) == (compositor_line_unlit() & 0xffffffu) &&
              (layout_pixels[255 * 256 + 128] & 0xffffffu) == (compositor_line_unlit() & 0xffffffu) &&
              (layout_pixels[255 * 256 + 127] & 0xffffffu) == picture && (layout_pixels[0 * 256 + 127] & 0xffffffu) == picture);
    compositor_float(1);
}

/*
        The snap's bugs, one check each: a maximized window covers the tiles on its screen wholly, and they float back as a
        half that a half covers does; and an armed assist lets go of a thumbnail whose window closes, and of its own window.
*/
static void check_compositor_snap_cover(void)
{
    if (!layout_table_map(256))
    {
        check("cover: the table is mapped", false);
        return;
    }

    assist_fresh();
    compositor.showing[3] = 0;
    compositor_arrange(2, COMPOSITOR_LEFT);
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    check("cover: a maximized window floats back the tile it covers wholly, as a half that a half covers does",
          compositor.arranged[2] == COMPOSITOR_FLOATING && compositor.arranged[1] == COMPOSITOR_MAXIMIZED);
}

static void check_compositor_snap_closing(void)
{
    if (!layout_table_map(256))
    {
        check("closing: the table is mapped", false);
        return;
    }

    assist_fresh();
    layout_drop(1, 20, 4, 0, 100);
    compositor_leave(2);
    check("assist: a window that closes while the assist is armed is no thumbnail any more", assist_row_at(2) < 0);
    assist_fresh();
    layout_drop(1, 20, 4, 0, 100);
    compositor_leave(1);
    check("assist: the window the assist is armed for closes, and the assist goes with it",
          !compositor.assist_armed && !compositor.assist_on);
}

/*
        A window arranged or floated glides from the rectangle it had to the one it has: at its first present it is where it
        was, in the middle of COMPOSITOR_GLIDE_NS it is between the two, and at the end of it it is at the rectangle exactly,
        and the glide is over (so the loop is idle). Reduced motion snaps: no glide at all, and the rectangle at once.
*/
static void check_compositor_glide(void)
{
    struct compositor_frame frame;
    positive now;
    int was_x, was_w, mid_x, mid_w;

    if (!layout_table_map(256))
    {
        check("glide: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    compositor.table->motion = 0;
    layout_show(1, 40, 40, 100, 100); /* not under the least a window is, which a floated window is never asked for */
    compositor.stack[0] = 1, compositor.stacked = 1;

    compositor_arrange(1, COMPOSITOR_LEFT);
    compositor_frame_of(1, &frame);
    was_x = frame.x, was_w = frame.width;
    check("glide: an arranged window starts from the rectangle it had, and is drawn there at its first present",
          compositor.glide[1] && compositor.glide_from[1][0] == 40 && compositor.glide_from[1][2] == 100 && was_x == 40 && was_w == 100);

    now = clock_monotonic_nanoseconds();
    compositor.glide_at[1] = now - COMPOSITOR_GLIDE_NS / 2;
    compositor_frame_of(1, &frame);
    mid_x = frame.x, mid_w = frame.width;
    check("glide: half way through it is between the two rectangles, moving from 40 toward 0 and growing from 100 toward 128",
          mid_x > 0 && mid_x < 40 && mid_w > 100 && mid_w < 128);

    compositor.glide_at[1] = now - COMPOSITOR_GLIDE_NS - 1;
    compositor_glide_keep();
    compositor_frame_of(1, &frame);
    check("glide: at its end it is at the rectangle it has, exactly, and the glide is over",
          !compositor.glide[1] && frame.x == 0 && frame.y == 0 && frame.width == 128 && frame.height == 256);

    compositor_float(1);
    compositor_frame_of(1, &frame);
    check("glide: floated back, it glides from the tile to its own rectangle, starting on the tile",
          compositor.glide[1] && frame.x == 0 && frame.width == 128);
    compositor.glide_at[1] = now - COMPOSITOR_GLIDE_NS - 1;
    compositor_glide_keep();
    compositor_frame_of(1, &frame);
    check("glide: floated back, it ends on its own rectangle exactly", !compositor.glide[1] && frame.x == 40 && frame.width == 100);

    compositor.table->motion = 1;
    compositor_arrange(1, COMPOSITOR_RIGHT);
    compositor_frame_of(1, &frame);
    check("glide: reduced motion snaps: no glide, and the window is at its rectangle at once",
          !compositor.glide[1] && frame.x == 128 && frame.width == 128);
    compositor.table->motion = 0;
}

/*
        The snap preview is not faded: it is the target at full tint in the frame the window touches it, and gone in the frame
        the contact is lost or the window is let go (the window glides to the place on its own). Reduced motion is the same.
*/
static void check_compositor_preview_at_once(void)
{
    if (!layout_table_map(256))
    {
        check("preview at once: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    compositor.table->motion = 0;
    assist_fresh();
    compositor.showing[2] = compositor.showing[3] = 0;

    screens_hand(80, 64, 1);
    screens_hand(0, 100, 0);
    check("preview at once: entered, its rectangle is the target in the same frame", compositor.preview[2] == 128);

    screens_hand(100, 100, 0);
    check("preview at once: left, it is gone in the same frame, with no row",
          compositor.preview[2] == 0 && compositor.preview_row == COMPOSITOR_NONE);

    screens_hand(0, 100, 0);
    screens_hand(0, 100, -1);
    check("preview at once: let go, it is gone and the window glides to the place it took",
          compositor.preview[2] == 0 && compositor.preview_row == COMPOSITOR_NONE && compositor.glide[1] &&
              compositor.arranged[1] == COMPOSITOR_LEFT);

    layout_setup(256, 1);
    compositor.table->motion = 1;
    assist_fresh();
    compositor.showing[2] = compositor.showing[3] = 0;
    screens_hand(80, 64, 1);
    screens_hand(0, 100, 0);
    check("preview at once: reduced motion is the same: the target at once", compositor.preview[2] == 128);
    screens_hand(0, 100, -1);
    compositor.table->motion = 0;
}

/*
        The snap trigger reads the window's rectangle against its screen's, never the hand. An edge touches its screen's
        edge at 0 px, and the preview is the target at full tint from that first frame. Once touched it holds until the
        edge is 8 px inside (a dead zone, so a jitter does not cancel it); a corner whose two edges are within 16 px of
        the screen's corner wins over the edge that touches it. The pointer on an edge with the window away from it is no
        trigger, and a let go with no preview leaves the window where it is. A window is on the screen it is mostly on,
        and a seam between two screens is never an edge.
*/
static void snap_fresh(void)
{
    layout_setup(256, 1);
    compositor.table->motion = 0;
    layout_show(1, 60, 60, 100, 80);
}

// A hand holding row by grab_x, grab_y in its rectangle, which is box_x, box_y before any clamp: the button down on the first.
static void snap_hand(int row, int box_x, int box_y, int grab_x, int grab_y)
{
    if (compositor.drag != row)
    {
        int keep[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

        compositor.drag = row;
        compositor.drag_moved = 0;
        compositor.drag_x = grab_x, compositor.drag_y = grab_y;
        compositor.press_x = compositor.x[row] + grab_x, compositor.press_y = compositor.y[row] + grab_y;
        memory_copy(compositor.drag_keep, keep, sizeof(keep)); /* what a press keeps for a restore: where it floats */
        compositor.button_down = 1;
        compositor.button_row[0] = row;
    }
    screens_hand(box_x + grab_x, box_y + grab_y, 0);
}

static void snap_let_go(int box_x, int box_y, int grab_x, int grab_y)
{
    screens_hand(box_x + grab_x, box_y + grab_y, -1);
}

static int snap_rect_is(const int *rect)
{
    return compositor.preview_row == 1 && compositor.preview[0] == rect[0] && compositor.preview[1] == rect[1] &&
           compositor.preview[2] == rect[2] && compositor.preview[3] == rect[3];
}

static void check_compositor_snap_trigger(void)
{
    static const struct { int x, y; int rect[4]; } contact[3] = {
        {0, 60, {0, 0, 128, 256}},     /* left, touched at 0 px */
        {156, 60, {128, 0, 128, 256}}, /* right */
        {60, 0, {0, 0, 256, 256}},     /* top: maximized */
    };
    int every = 1, rect[4];

    if (!layout_table_map(256))
    {
        check("snap trigger: the table is mapped", false);
        return;
    }
    for (int each = 0; each < 3; each++)
    {
        snap_fresh();
        snap_hand(1, contact[each].x, contact[each].y, 50, 40);
        every = every && snap_rect_is(contact[each].rect);
    }
    check("snap contact: the left, the right and the top touched at 0 px show their target at the first frame, exactly", every);

    snap_fresh();
    snap_hand(1, 5, 60, 50, 40);
    check("snap contact: 5 px short of the left edge is no contact, no preview", compositor.preview[2] == 0);
    snap_fresh();
    snap_hand(1, 60, 171, 50, 40);
    check("snap contact: 5 px short of the bottom edge is no contact, no preview", compositor.preview[2] == 0);

    snap_fresh();
    snap_hand(1, 0, 60, 50, 40);
    snap_hand(1, 5, 60, 50, 40);
    check("snap contact: touched, then 5 px inside, the target holds (the dead zone)",
          compositor.preview[2] == 128 && compositor.preview_row == 1);
    snap_hand(1, 8, 60, 50, 40);
    check("snap contact: 8 px inside the edge the contact is lost, and the preview clears at once",
          compositor.preview[2] == 0 && compositor.preview_row == COMPOSITOR_NONE);

    /* A corner: its two edges within 16 px of the screen's corner, one of them touching, is the quarter. */
    snap_fresh();
    snap_hand(1, 10, 0, 50, 40);
    rect[0] = 0, rect[1] = 0, rect[2] = 128, rect[3] = 128;
    check("snap corner: the top touching with the left 10 px short of the corner, the quarter wins over the top",
          snap_rect_is(rect) && compositor.preview_row == 1);
    snap_fresh();
    snap_hand(1, 146, 176, 50, 40);
    rect[0] = 128, rect[1] = 128, rect[2] = 128, rect[3] = 128;
    check("snap corner: the bottom touching with the right 10 px short of the corner, the bottom right quarter wins",
          snap_rect_is(rect));
    snap_fresh();
    snap_hand(1, 0, 0, 50, 40);
    rect[0] = 0, rect[1] = 0, rect[2] = 128, rect[3] = 128;
    check("snap corner: both edges touching, the corner's quarter", snap_rect_is(rect));
    snap_fresh();
    snap_hand(1, 20, 0, 50, 40);
    rect[0] = 0, rect[1] = 0, rect[2] = 256, rect[3] = 256;
    check("snap corner: the left 20 px short is outside the corner dead zone, and the top alone maximizes",
          snap_rect_is(rect));

    /* The pointer alone is no trigger: the window is elsewhere, and the hand is on the edge. */
    snap_fresh();
    snap_hand(1, 120, 60, -120, 40);
    check("snap cursor: the pointer on the left edge with the window in the middle is no trigger", compositor.preview[2] == 0);
    snap_fresh();
    snap_hand(1, 60, 110, 40, -110);
    check("snap cursor: the pointer on the top edge with the window in the middle is no trigger", compositor.preview[2] == 0);

    /* A let go: it takes the target it shows, which clears at once, and takes none where the window is away. */
    snap_fresh();
    snap_hand(1, 0, 60, 50, 40);
    snap_let_go(0, 60, 50, 40);
    check("snap release: let go on the target the window takes it, and the preview is gone at once",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.x[1] == 0 && compositor.y[1] == 0 &&
              compositor.table->want_width[1] == 128 && compositor.table->want_height[1] == 256 && compositor.preview[2] == 0);
    snap_fresh();
    snap_hand(1, 120, 60, -120, 40);
    snap_let_go(120, 60, -120, 40);
    check("snap release: let go with only the pointer on the edge, the window stays where it is",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 120 && compositor.y[1] == 60);
    snap_fresh();
    snap_hand(1, 0, 60, 50, 40);
    snap_hand(1, 20, 60, 50, 40);
    check("snap release: dragged 20 px away from the edge before the let go, the preview is gone", compositor.preview[2] == 0);
    snap_let_go(20, 60, 50, 40);
    check("snap release: and the let go takes no target", compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 20);

    /* Two screens side by side, A and B, the space 512 by 256. A window is on the screen it is mostly on. */
    snap_fresh();
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
    compositor.width = 512;
    snap_hand(1, 200, 0, 80, 40);
    rect[0] = 0, rect[1] = 0, rect[2] = 256, rect[3] = 256;
    check("snap screen: a window across two screens, mostly on A, maximizes on A and not on the B under its pointer",
          snap_rect_is(rect) && compositor.preview[0] == 0);
    snap_fresh();
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
    compositor.width = 512;
    snap_hand(1, 156, 60, 50, 40);
    check("snap screen: A's right edge against B is a seam, never an edge: no preview", compositor.preview[2] == 0);
    snap_fresh();
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
    compositor.width = 512;
    snap_hand(1, 256, 60, 50, 40);
    check("snap screen: B's left edge against A is a seam, never an edge: no preview", compositor.preview[2] == 0);
    snap_fresh();
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
    compositor.width = 512;
    snap_hand(1, 412, 60, 50, 40);
    rect[0] = 384, rect[1] = 0, rect[2] = 128, rect[3] = 256;
    check("snap screen: B's free right edge touched shows B's right half", snap_rect_is(rect));
    snap_let_go(412, 60, 50, 40);
    check("snap screen: and the let go takes B's right half", compositor.arranged[1] == COMPOSITOR_RIGHT && compositor.x[1] == 384);

    /* The drawn frames: the tint is in the frame while the target is; after the let go it fades out under the window gliding
       into it (its first frame is the tint as it was), and once the fade is over no frame has any of it. */
    {
        unsigned int ink, tint, drawn = 0, fading = 0, after = 0;

        snap_fresh();
        snap_hand(1, 0, 60, 50, 40);
        compositor_damage(0, 0, 256, 256);
        compositor_present();
        ink = compositor_snap_ink();
        tint = compositor_mix(compositor_ground, ink & 0xffffffu, ink >> 24);
        for (int at = 0; at < 256 * 256; at++)
            drawn += (layout_pixels[at] & 0xffffffu) == (tint & 0xffffffu);
        snap_let_go(0, 60, 50, 40);
        compositor_damage(0, 0, 256, 256);
        compositor_present();
        for (int at = 0; at < 256 * 256; at++)
            fading += (layout_pixels[at] & 0xffffffu) == (tint & 0xffffffu);
        compositor.preview_at = clock_monotonic_nanoseconds() - UI_WINDOW_NS - 1;
        compositor.glide_at[1] = clock_monotonic_nanoseconds() - UI_WINDOW_NS - 1;
        compositor_preview_keep();
        compositor_glide_keep();
        compositor_damage(0, 0, 256, 256);
        compositor_present();
        for (int at = 0; at < 256 * 256; at++)
            after += (layout_pixels[at] & 0xffffffu) == (tint & 0xffffffu);
        check("snap frames: while the target is touched the frame is tinted; the let go's first frame still has it, fading under "
              "the gliding window, and after the fade no frame has any of the tint",
              drawn > 0 && fading > 0 && after == 0 && !compositor.preview_draw);
    }
}

/*
        The snap's edges: the bottom alone is no target (only its corners are), so a tall window set low is not taken by the
        edge it rests on; and Control held during a drag is no snap at all, so a window can be set flush against an edge,
        while letting go of Control brings the target back at once, with no motion.
*/
static void check_compositor_snap_edges(void)
{
    int rect[4];

    if (!layout_table_map(256))
    {
        check("snap edges: the table is mapped", false);
        return;
    }
    snap_fresh();
    snap_hand(1, 60, 176, 50, 40);
    check("snap edges: the bottom edge touched alone shows no target", compositor.preview[2] == 0);
    snap_let_go(60, 176, 50, 40);
    check("snap edges: and the let go leaves the window floating where the hand put it, its line on the screen's last row",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 60 && compositor.y[1] == 175);

    snap_fresh();
    snap_hand(1, 0, 176, 50, 40);
    rect[0] = 0, rect[1] = 128, rect[2] = 128, rect[3] = 128;
    check("snap edges: the bottom touching with the left touching too is the bottom left quarter", snap_rect_is(rect));
    snap_let_go(0, 176, 50, 40);

    snap_fresh();
    compositor.held[0] |= CK_HELD_LCONTROL;
    snap_hand(1, 0, 60, 50, 40);
    check("snap edges: Control held, the left edge touched shows no target", compositor.preview[2] == 0);
    compositor_key(0, 29, 0, 0); /* left Control let go, the hand still */
    check("snap edges: Control let go with the hand still, the target is shown at once", compositor.preview[2] == 128);
    compositor_key(0, 29, 1, 0);
    check("snap edges: Control held again, it is gone at once", compositor.preview[2] == 0);
    snap_let_go(0, 60, 50, 40);
    check("snap edges: let go with Control held, the window stays flush against the edge (its line on the first column), floating",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 1 && compositor.y[1] == 60);
    compositor_key(0, 29, 0, 0);
}

/*
        Restoring a snapped window. A filled window pressed on its top moves nothing until the hand is UI_SLOP from the
        press, so a click with a shaking hand, or a double click, is never a tear off; past it the window is its remembered
        size under the pointer, the grab keeping its share of the width and, on the top, its distance from the top (taken
        lower down with Alt, its share of the height). A drop on a target keeps the rectangle the window had before the
        drag, not the one it was let go at against the edge, and snaps in a row keep the first one.
*/
static void restore_fresh(void)
{
    layout_setup(256, 1);
    compositor.table->motion = 1;
    layout_show(1, 60, 60, 100, 80);
    compositor.stack[0] = 1, compositor.stacked = 1;
}

static int restore_kept_is(int row, int x, int y, int width, int height)
{
    return compositor.saved_x[row] == x && compositor.saved_y[row] == y && (int)compositor.saved_w[row] == width &&
           (int)compositor.saved_h[row] == height;
}

static void check_compositor_restore(void)
{
    if (!layout_table_map(256))
    {
        check("restore: the table is mapped", false);
        return;
    }
    restore_fresh();
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    screens_hand(128, 10, 1);
    screens_hand(131, 14, 0);
    check("restore slop: a filled window pressed on its top and moved 5 px, under the slop, is still filled and not moved",
          compositor.arranged[1] == COMPOSITOR_MAXIMIZED && compositor.x[1] == 0 && compositor.y[1] == 0 && compositor.w[1] == 256);
    screens_hand(128, 30, 0);
    check("restore slop: past the slop it is torn off at its remembered size",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.w[1] == 100 && compositor.h[1] == 80);
    check("restore grab: the grab keeps its share of the width (128 of 256 is 50 of 100) and its 10 px from the top",
          compositor.x[1] == 128 - 50 && compositor.y[1] == 30 - 10);
    screens_hand(128, 30, -1);
    compositor.press = COMPOSITOR_NONE;

    /* A double click with a shaking hand: a 3 px jitter between the press and the release is still a click. The presses of the
       checks above are a few milliseconds ago, so the click count starts afresh, as a click after a pause does. */
    compositor.press_click = (struct ui_click){0};
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    screens_hand(128, 10, 1);
    screens_hand(130, 12, 0);
    screens_hand(130, 12, -1);
    check("restore slop: a click with a 3 px shake on a filled window's top leaves it filled",
          compositor.arranged[1] == COMPOSITOR_MAXIMIZED && compositor.w[1] == 256);
    screens_hand(130, 12, 1);
    check("restore slop: and the second click of the pair gives back its own rectangle",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 78 && compositor.y[1] == 20 && compositor.w[1] == 100);
    screens_hand(130, 12, -1);
    compositor.press = COMPOSITOR_NONE;

    /* Taken lower down with Alt: its share of the height as well. */
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    compositor.held[0] |= CK_HELD_ALT;
    screens_hand(128, 128, 1);
    screens_hand(128, 160, 0);
    check("restore grab: taken lower down with Alt, it keeps its share of the height too (128 of 256 is 40 of 80)",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 128 - 50 && compositor.y[1] == 160 - 40);
    screens_hand(128, 160, -1);
    compositor.held[0] &= ~CK_HELD_ALT;
    compositor.press = COMPOSITOR_NONE;

    /* Dragged from the middle onto the left edge: the rectangle kept is the one before the drag. */
    restore_fresh();
    screens_hand(80, 64, 1);
    screens_hand(0, 100, 0);
    screens_hand(0, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    check("restore keep: dropped on the left edge, it keeps the rectangle it had before the drag, not the one let go at the edge",
          compositor.arranged[1] == COMPOSITOR_LEFT && restore_kept_is(1, 60, 60, 100, 80));
    compositor_float(1);
    check("restore keep: and a restore puts it back there", compositor.x[1] == 60 && compositor.y[1] == 60 && compositor.w[1] == 100);

    /* Snaps in a row keep the first: left, then torn off and dropped on the right edge. */
    screens_hand(80, 64, 1);
    screens_hand(0, 100, 0);
    screens_hand(0, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    screens_hand(64, 10, 1);
    screens_hand(250, 100, 0);
    check("restore grab: torn off by the screen's right edge, the hand keeps its grab (50 px in) though the screen keeps the "
          "window, and the window's edge past the screen's shows the right half",
          compositor.drag_x == 50 && compositor.drag_y == 10 && compositor.preview_row == 1 && compositor.preview[0] == 128 &&
              compositor.preview[2] == 128);
    screens_hand(250, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    check("restore keep: snapped left, torn off and dropped on the right, it still keeps the rectangle from before the first snap",
          compositor.arranged[1] == COMPOSITOR_RIGHT && restore_kept_is(1, 60, 60, 100, 80));
}

/*
        Escape, or a right press, during a drag or a resize takes it back: the window is put back as it was at the press -- a
        floating window where it was, a torn off tile as that tile, a resized window at its size -- the preview and the lit
        line go, the remembered rectangle is untouched, and Escape is no key of the window's.
*/
static void check_compositor_cancel(void)
{
    unsigned int head;

    if (!layout_table_map(256))
    {
        check("cancel: the table is mapped", false);
        return;
    }
    restore_fresh();
    compositor_focus(1);
    screens_hand(80, 64, 1);
    screens_hand(0, 100, 0);
    head = compositor.table->event_head[1];
    compositor_key(0, CK_KEY_ESC, 1, 0);
    compositor_key(0, CK_KEY_ESC, 0, 0);
    check("cancel: Escape during a drag over the left edge puts the window back where the press took it, with no preview",
          compositor.drag == COMPOSITOR_NONE && compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 60 &&
              compositor.y[1] == 60 && compositor.preview[2] == 0 && compositor.lit_row == COMPOSITOR_NONE);
    check("cancel: and Escape is no key of the window's", compositor.table->event_head[1] == head);
    screens_hand(0, 100, 0);
    screens_hand(0, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    check("cancel: the hand moved on and let go after it, nothing is arranged",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 60);

    /* A tile torn off and taken back by a right press is that tile again, and still remembers its rectangle. */
    compositor_arrange(1, COMPOSITOR_LEFT);
    screens_hand(64, 10, 1);
    screens_hand(180, 120, 0);
    check("cancel: (the tile is torn off)", compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.w[1] == 100);
    compositor_button(COMPOSITOR_BTN_RIGHT, 1);
    check("cancel: a right press during the drag puts the torn off tile back as the left half, remembering its rectangle",
          layout_is(1, COMPOSITOR_LEFT, 0, 0, 128, 256) && restore_kept_is(1, 60, 60, 100, 80) && compositor.drag == COMPOSITOR_NONE);
    compositor_button(COMPOSITOR_BTN_RIGHT, 0);
    screens_hand(180, 120, -1);
    compositor.press = COMPOSITOR_NONE;
    check("cancel: and the left let go after it arranges nothing more", layout_is(1, COMPOSITOR_LEFT, 0, 0, 128, 256));

    /* A resize taken back is the size it was. */
    compositor_float(1);
    screens_hand(60 + 100 + 2, 100, 1);
    screens_hand(60 + 100 + 2 + 40, 100, 0);
    check("cancel: (the right edge is resized)", compositor.resize == 1 && compositor.table->want_width[1] == 140);
    compositor_key(0, CK_KEY_ESC, 1, 0);
    compositor_key(0, CK_KEY_ESC, 0, 0);
    check("cancel: Escape during a resize puts the window back at the size it was, and the hand holds nothing",
          compositor.resize == COMPOSITOR_NONE && compositor.table->want_width[1] == 100 && compositor.x[1] == 60);
    screens_hand(60 + 100 + 2 + 40, 100, -1);
    compositor.press = COMPOSITOR_NONE;
}

/*
        Super and Shift with Left or Right takes the window with the keys to the next screen that way: floating at its share
        of the room, a tile as the same tile there, its remembered rectangle carried with it, and no wrap past the last
        screen. Two screens side by side, A at the left and B at the right, each 256 by 256.
*/
static void check_compositor_screen_chord(void)
{
    if (!layout_table_map(512))
    {
        check("screen chord: the table is mapped", false);
        return;
    }
    layout_setup(512, 1);
    compositor.table->motion = 1;
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
    layout_show(1, 10, 20, 100, 40);
    compositor.stack[0] = 1, compositor.stacked = 1;
    compositor_focus(1);
    compositor.held[0] = CK_HELD_LSUPER | CK_HELD_LSHIFT;
    compositor_key(0, CK_KEY_RIGHT, 1, 0);
    check("screen chord: floating, it goes to the screen on the right at its share of the room",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor_screen_of(1) == 1 && compositor.x[1] == 256 + 10 &&
              compositor.y[1] == 20);
    compositor_key(0, CK_KEY_RIGHT, 1, 0);
    check("screen chord: past the last screen there is no wrap", compositor_screen_of(1) == 1 && compositor.x[1] == 266);
    compositor.held[0] = CK_HELD_LSUPER;
    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("screen chord: (Super and Left: the left half of the right screen)", layout_is(1, COMPOSITOR_LEFT, 256, 0, 128, 256));
    compositor.held[0] = CK_HELD_LSUPER | CK_HELD_LSHIFT;
    compositor_key(0, CK_KEY_LEFT, 1, 0);
    check("screen chord: a tile goes as the same tile, the left half of the left screen",
          layout_is(1, COMPOSITOR_LEFT, 0, 0, 128, 256) && compositor_screen_of(1) == 0);
    compositor.held[0] = CK_HELD_LSUPER;
    compositor_key(0, CK_KEY_RIGHT, 1, 0);
    check("screen chord: and its remembered rectangle came with it there and back: undone, it is where it floated on the left screen",
          compositor.arranged[1] == COMPOSITOR_FLOATING && compositor.x[1] == 10 && compositor.y[1] == 20 && compositor_screen_of(1) == 0);
}

/*
        The preview's motion and shape. Entering a target it is drawn there at full strength at once; a new target (the left
        half's corner) is reached from where it is drawn, between the two half way and on the quarter exactly at the end;
        dragged off, its target is gone at once and what is drawn fades out where it was; let go on a target, it fades out
        under the window gliding into it, over the same time. A tile's preview has a window's round corners and the window's
        line, stronger than its fill; a filled screen's has the round corners and no line. Reduced motion cuts. A glide that starts while another
        runs starts from where the window is drawn.
*/
static void check_compositor_preview_motion(void)
{
    int rect[4], half[4] = {0, 0, 128, 256}, quarter[4] = {0, 0, 128, 128}, was[4];
    unsigned int alpha, ink, fill, line_at, fill_at, corner_at;
    positive now;
    struct compositor_frame frame;

    if (!layout_table_map(256))
    {
        check("preview motion: the table is mapped", false);
        return;
    }
    snap_fresh();
    snap_hand(1, 0, 60, 50, 40);
    alpha = compositor_preview_shown(rect);
    check("preview motion: entering a target it is drawn there at once, at full strength",
          alpha == 100 && !memory_compare(rect, half, sizeof(rect)));

    snap_hand(1, 0, 6, 50, 40); /* the top within the corner's 16 px: the top left quarter */
    alpha = compositor_preview_shown(rect);
    check("preview motion: a new target is reached from where it is drawn: at its first frame still the half",
          compositor.preview[3] == 128 && alpha == 100 && !memory_compare(rect, half, sizeof(rect)));
    now = clock_monotonic_nanoseconds();
    compositor.preview_at = now - UI_WINDOW_NS / 2;
    compositor_preview_shown(rect);
    check("preview motion: half way it is between the half and the quarter", rect[3] > 128 && rect[3] < 256 && rect[2] == 128);
    compositor.preview_at = now - UI_WINDOW_NS - 1;
    compositor_preview_keep();
    alpha = compositor_preview_shown(rect);
    check("preview motion: at the end it is on the quarter exactly, and still", alpha == 100 && !memory_compare(rect, quarter, sizeof(rect)) &&
                                                                                    compositor.preview_at == 0);

    snap_hand(1, 60, 60, 50, 40);
    alpha = compositor_preview_shown(rect);
    check("preview motion: dragged off, the target is gone at once and what is drawn fades from where it was",
          compositor.preview[2] == 0 && compositor.preview_row == COMPOSITOR_NONE && alpha == 100 && !memory_compare(rect, quarter, sizeof(rect)));
    compositor.preview_at = clock_monotonic_nanoseconds() - UI_WINDOW_NS / 2;
    alpha = compositor_preview_shown(rect);
    check("preview motion: half way through the fade it is fainter, where it was", alpha > 0 && alpha < 100 &&
                                                                                     !memory_compare(rect, quarter, sizeof(rect)));
    compositor.preview_at = clock_monotonic_nanoseconds() - UI_WINDOW_NS - 1;
    compositor_preview_keep();
    check("preview motion: and then it is gone", !compositor.preview_draw && !compositor_preview_shown(rect));

    /* The shape, presented above the window (it is at 60 down): a tile is square with a line, filling the screen is square with none. */
    snap_fresh();
    snap_hand(1, 0, 60, 50, 40);
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    ink = compositor_snap_ink();
    fill = compositor_mix(compositor_ground, ink & 0xffffffu, ink >> 24) & 0xffffffu;
    corner_at = layout_pixels[0] & 0xffffffu, fill_at = layout_pixels[30 * 256 + 64] & 0xffffffu, line_at = layout_pixels[30 * 256 + 127] & 0xffffffu;
    check("preview shape: a half's preview has a window's round corners (its corner pixel is the wallpaper), its fill inside, and a "
          "line stronger than the fill along its edge",
          corner_at == (compositor_ground & 0xffffffu) && fill_at == fill && line_at != fill && line_at != (compositor_ground & 0xffffffu));
    snap_hand(1, 60, 0, 50, 40);
    compositor.preview_at = clock_monotonic_nanoseconds() - UI_WINDOW_NS - 1;
    compositor_preview_keep();
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    check("preview shape: filling the screen it has a window's round corners and no line: its corner pixel is the wallpaper, its "
          "edge the fill",
          (layout_pixels[0] & 0xffffffu) == (compositor_ground & 0xffffffu) && (layout_pixels[30 * 256 + 255] & 0xffffffu) == fill);
    snap_let_go(60, 0, 50, 40);

    /* Let go on a target: the window glides into it and the preview fades out over the same time. */
    snap_fresh();
    snap_hand(1, 0, 60, 50, 40);
    snap_let_go(0, 60, 50, 40);
    alpha = compositor_preview_shown(rect);
    check("preview motion: let go on the half, the window glides into it and the preview fades from where it was, under it",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.glide[1] && compositor.preview_fading && alpha == 100 &&
              !memory_compare(rect, half, sizeof(rect)) && compositor.preview_draw == 2);
    compositor_frame_of(1, &frame);
    check("preview motion: the window's frame carries the fading preview, with a window's round corners and the line of the window, "
          "which is square",
          frame.preview[2] == 128 && frame.preview_radius == UI_RADIUS && frame.preview_line == frame.line && frame.line > 0 &&
              frame.radius == 0);

    /* Reduced motion cuts: off a target, nothing is drawn at once. */
    snap_fresh();
    compositor.table->motion = 1;
    snap_hand(1, 0, 60, 50, 40);
    snap_hand(1, 60, 60, 50, 40);
    check("preview motion: reduced motion, dragged off, nothing is drawn at once", !compositor_preview_shown(rect) && !compositor.preview_draw);
    snap_let_go(60, 60, 50, 40);
    compositor.table->motion = 0;

    /* A glide begun while another runs starts from where the window is drawn. */
    layout_setup(256, 1);
    compositor.table->motion = 0;
    layout_show(1, 40, 40, 100, 100);
    compositor.stack[0] = 1, compositor.stacked = 1;
    compositor_arrange(1, COMPOSITOR_LEFT);
    compositor.glide_at[1] = clock_monotonic_nanoseconds() - UI_WINDOW_NS / 2;
    compositor_frame_of(1, &frame);
    was[0] = frame.x, was[1] = frame.y, was[2] = frame.width, was[3] = frame.height;
    compositor_arrange(1, COMPOSITOR_RIGHT);
    check("glide: a glide begun half way through another starts from where the window is drawn, not from the left half",
          compositor.glide[1] && compositor.glide_from[1][0] - was[0] <= 1 && was[0] - compositor.glide_from[1][0] <= 1 &&
              compositor.glide_from[1][2] - was[2] <= 1 && was[2] - compositor.glide_from[1][2] <= 1 && compositor.glide_from[1][2] != 128);
}

/*
        A window arranged is square, as a maximized one is: a half, a quarter, the bottom half, the whole screen and the room a
        placement fills, at every scale; a floating window is round, and the line is what it was (a tile has the window's, a
        window filling its screen has none). The tile is square where it is drawn too, at its four corners and at the corners of
        its line; its preview before the drop has a window's round corners and the line the window will have; a window torn off a tile floats
        and is round again; a tile resized by an inner edge stays a tile and stays square; the Super chords walk the states and
        the shape follows each; a window moved to another screen is square or round at that screen's scale; and a tile that
        glides or lands is square from its first frame.
*/
static int square_line(int scale)
{
    return scale / 100 > 1 ? scale / 100 : 1;
}

static void check_compositor_tile_square(void)
{
    static const unsigned int tiles[] = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_BOTTOM, COMPOSITOR_TOP_LEFT,
                                         COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};
    static const char *const names[] = {"left half", "right half", "bottom half", "top left", "top right", "bottom left", "bottom right"};
    const unsigned int picture = 0xc06020u;
    struct compositor_frame frame;
    unsigned int shapes = ~0u, previews = ~0u, drawn = ~0u;
    int room[4] = {0, 0, 256, 200};

    if (!layout_table_map(256))
    {
        check("tile square: the table is mapped", false);
        return;
    }

    // Every arrangement at every scale: the frame's radius and line.
    for (unsigned int at = 0; at < array_count(corner_scales); at++)
    {
        int scale = corner_scales[at], round = UI_RADIUS * scale / 100, line = square_line(scale), bad = 0;

        layout_setup(256, 1);
        compositor.table->motion = 1;
        compositor.scale = scale;
        layout_show(1, 60, 60, 100, 80);
        compositor.stack[0] = 1, compositor.stacked = 1;
        compositor_frame_of(1, &frame);
        bad |= !(frame.radius == round && frame.line == line);
        for (unsigned int tile = 0; tile < array_count(tiles); tile++)
        {
            compositor_arrange(1, tiles[tile]);
            compositor_frame_of(1, &frame);
            if (frame.radius != 0 || compositor_radius(&frame) != 0 || frame.line != line)
            {
                bad |= 2;
                string_format(log, "  tile square: %p percent, %s: radius %p, line %p\n", (positive)scale, names[tile],
                              (positive)frame.radius, (positive)frame.line);
            }
        }
        compositor_arrange(1, COMPOSITOR_MAXIMIZED);
        compositor_frame_of(1, &frame);
        bad |= (frame.radius != 0 || frame.line != 0) << 2;
        compositor_float(1);
        compositor_arrange_rect(1, COMPOSITOR_ROOM, room);
        compositor_frame_of(1, &frame);
        bad |= (frame.radius != 0 || frame.line != 0) << 3;
        compositor_float(1);
        compositor_frame_of(1, &frame);
        bad |= (frame.radius != round || frame.line != line) << 4;
        shapes &= bad ? ~(1u << at) : ~0u;
    }
    check("tile square: at 50, 75, 100, 125, 150 and 200 percent every tile (a half, a quarter, the bottom half) is square with the "
          "window's line, the whole screen and the room are square with none, and a floating window is round again after each",
          shapes == ~0u);

    // The tiles drawn: the four corners of the picture are the picture, at scale 100, for every tile.
    for (unsigned int tile = 0; tile < array_count(tiles); tile++)
    {
        int x, y, w, h;
        unsigned int ok;

        layout_setup(256, 1);
        compositor.table->motion = 1;
        layout_show(1, 60, 60, 100, 80);
        compositor.stack[0] = 1, compositor.stacked = 1;
        compositor_arrange(1, tiles[tile]);
        layout_paint(1, picture);
        compositor_damage(0, 0, 256, 256);
        compositor_present();
        x = compositor.x[1], y = compositor.y[1], w = (int)compositor.w[1], h = (int)compositor.h[1];
        ok = (layout_pixels[y * 256 + x] & 0xffffffu) == picture && (layout_pixels[y * 256 + x + w - 1] & 0xffffffu) == picture &&
             (layout_pixels[(y + h - 1) * 256 + x] & 0xffffffu) == picture &&
             (layout_pixels[(y + h - 1) * 256 + x + w - 1] & 0xffffffu) == picture;
        drawn &= ok ? ~0u : ~(1u << tile);
        if (!ok)
            string_format(log, "  tile square: %s drawn with a cut corner\n", names[tile]);
    }
    check("tile square: drawn, the four corners of every tile's picture are the picture", drawn == ~0u);

    // The preview of a tile and of the whole screen, at every scale: a window's round corners, with the window's line for a tile and none to fill.
    for (unsigned int at = 0; at < array_count(corner_scales); at++)
    {
        int scale = corner_scales[at], bad = 0;

        snap_fresh();
        compositor.scale = scale;
        snap_hand(1, 0, 60, 50, 40);
        compositor_frame_of(1, &frame);
        bad |= !(frame.preview[2] > 0 && frame.preview_radius == UI_RADIUS * scale / 100 && frame.preview_line == square_line(scale) &&
                 frame.preview_line == frame.line && (frame.preview_edge >> 24) > 0);
        snap_hand(1, 60, 0, 50, 40);
        compositor_frame_of(1, &frame);
        bad |= (!(frame.preview[2] > 0 && frame.preview_radius == UI_RADIUS * scale / 100 && frame.preview_line == 0 &&
                  frame.preview_edge == 0)) << 1;
        snap_let_go(60, 0, 50, 40);
        previews &= bad ? ~(1u << at) : ~0u;
    }
    check("tile square: at every scale the preview of a tile has a window's round corners and the window's line, and of the whole "
          "screen the round corners and no line",
          previews == ~0u);

    // Torn off: a tile is square, and pulled off by its top past the slop the window floats and is round again.
    {
        int before, after, round_again;

        restore_fresh();
        compositor_arrange(1, COMPOSITOR_LEFT);
        compositor_frame_of(1, &frame);
        before = frame.radius;
        screens_hand(64, 10, 1);
        screens_hand(64, 12, 0);
        compositor_frame_of(1, &frame);
        after = compositor.arranged[1] == COMPOSITOR_LEFT && frame.radius == 0;
        screens_hand(64, 40, 0);
        compositor_frame_of(1, &frame);
        round_again = compositor.arranged[1] == COMPOSITOR_FLOATING && frame.radius == UI_RADIUS && frame.line == 1;
        screens_hand(64, 40, -1);
        compositor.press = COMPOSITOR_NONE;
        check("tile square: a tile is square, a press on it moves nothing within the slop and it stays square, torn off past the slop "
              "it floats and is round again",
              before == 0 && after && round_again);
    }

    // Resized by an inner edge: still a tile, still square.
    {
        int moved;

        restore_fresh();
        compositor_arrange(1, COMPOSITOR_LEFT);
        screens_hand(130, 100, 1);
        screens_hand(150, 100, 0);
        compositor_frame_of(1, &frame);
        moved = compositor.arranged[1] == COMPOSITOR_LEFT && compositor.w[1] == 148 && frame.radius == 0 && frame.line == 1;
        screens_hand(150, 100, -1);
        compositor.press = COMPOSITOR_NONE;
        compositor_frame_of(1, &frame);
        check("tile square: a tile resized by its inner edge stays a tile and stays square, held and let go",
              moved && compositor.arranged[1] == COMPOSITOR_LEFT && frame.radius == 0);
    }

    // The Super chords walk the states, and the shape follows each step.
    {
        static const unsigned int walk[] = {CK_KEY_RIGHT, CK_KEY_UP, CK_KEY_LEFT, CK_KEY_DOWN, CK_KEY_DOWN, CK_KEY_UP, CK_KEY_UP,
                                            CK_KEY_UP, CK_KEY_DOWN, CK_KEY_DOWN};
        unsigned int steps = 0, wrong = 0;

        layout_setup(256, 1);
        compositor.table->motion = 1;
        layout_show(1, 50, 30, 120, 80);
        compositor.stack[0] = 1, compositor.stacked = 1;
        for (unsigned int at = 0; at < array_count(walk); at++)
        {
            layout_chord(1, walk[at]);
            compositor_frame_of(1, &frame);
            steps += compositor.arranged[1] != COMPOSITOR_FLOATING;
            wrong += frame.radius != (compositor.arranged[1] == COMPOSITOR_FLOATING ? UI_RADIUS : 0);
        }
        check("tile square: walking Super with the arrows (half, quarter, quarter, half, bottom quarter, half, quarter, whole screen, "
              "back to floating) the window is square in every arranged state and round when it floats",
              steps >= 7 && wrong == 0 && compositor.arranged[1] == COMPOSITOR_FLOATING);
    }

    // Moved to another screen with Super and Shift: square or round, at that screen's scale.
    {
        unsigned int ok;

        if (!layout_table_map(512))
        {
            check("tile square: the table is mapped for two screens", false);
            return;
        }
        layout_setup(512, 1);
        compositor.table->motion = 1;
        compositor.outputs = 2;
        compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
        compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
        compositor.output[0].scale = 100, compositor.output[1].scale = 200;
        for (unsigned int row = 0; row < WINDOWS; row++)
            compositor.wake[row] = -1;
        layout_show(1, 10, 20, 100, 40);
        compositor.stack[0] = 1, compositor.stacked = 1;
        compositor_focus(1);
        compositor.held[0] = CK_HELD_LSUPER | CK_HELD_LSHIFT;
        compositor_key(0, CK_KEY_RIGHT, 1, 0);
        compositor_frame_of(1, &frame);
        ok = compositor.arranged[1] == COMPOSITOR_FLOATING && frame.radius == 2 * UI_RADIUS && frame.line == 2;
        compositor.held[0] = CK_HELD_LSUPER;
        compositor_key(0, CK_KEY_LEFT, 1, 0);
        compositor_frame_of(1, &frame);
        ok &= compositor.arranged[1] == COMPOSITOR_LEFT && compositor_screen_of(1) == 1 && frame.radius == 0 && frame.line == 2;
        compositor.held[0] = CK_HELD_LSUPER | CK_HELD_LSHIFT;
        compositor_key(0, CK_KEY_LEFT, 1, 0);
        compositor_frame_of(1, &frame);
        ok &= compositor.arranged[1] == COMPOSITOR_LEFT && compositor_screen_of(1) == 0 && frame.radius == 0 && frame.line == 1;
        compositor.held[0] = CK_HELD_LSUPER;
        compositor_key(0, CK_KEY_RIGHT, 1, 0);
        compositor_frame_of(1, &frame);
        ok &= compositor.arranged[1] == COMPOSITOR_FLOATING && frame.radius == UI_RADIUS && frame.line == 1;
        check("tile square: moved with Super and Shift between a screen at 100 and one at 200 percent a window is round floating "
              "(8, then 16 px), square as a tile on either (line 1, then 2), and round again where it floats",
              ok);
    }

    // A tile that glides into its place and one that lands there are square from their first frame.
    {
        int glides, lands;

        layout_setup(256, 1);
        compositor.table->motion = 0;
        layout_show(1, 60, 60, 100, 80);
        compositor.stack[0] = 1, compositor.stacked = 1;
        compositor_arrange(1, COMPOSITOR_TOP_LEFT);
        compositor_frame_of(1, &frame);
        glides = compositor.glide[1] && frame.radius == 0 && frame.line == 1;
        compositor.glide[1] = 0;
        compositor.landing[1] = 1, compositor.land_at[1] = clock_monotonic_nanoseconds();
        compositor_frame_of(1, &frame);
        lands = frame.source_w > 0 && frame.width < (int)compositor.w[1] && frame.radius == 0 && frame.line == 1;
        check("tile square: a tile gliding into its place and one landing there are square from the first frame", glides && lands);
    }
}

/*
        The snap preview lies under the window being dragged, which a press raised over the rest, and over every window under
        it, a glass one too; the tabs of the band and the overlays (the assist's thumbnails and the grid's tints) are over it.
        Where it lies over a window under the one dragged the pixel is the scene's own pixel with the tint mixed over it, the
        scene being what the screen shows with no preview; the window dragged and the tray's tab are their own pixels; and the
        tint changes no pixel outside the target. Four windows, the second glass and the first dragged and on top, the band,
        the preview on the right half held and fading, and then on two screens at 100 and 200 percent; and a preview that goes
        leaves nothing of itself behind.
*/
#define OVER_SIDE GLASS_SIDE

static void over_setup(unsigned int *made, unsigned int *band)
{
    struct windows address_to table;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.shared_row = compositor.mark_row = compositor.preview_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = OVER_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = made;
    compositor.scanout_pitch = OVER_SIDE * 4;
    compositor.band = band;
    if (!glass_table_map())
        return;
    table = compositor.table;
    for (int row = 1; row <= 4; row++)
    {
        table->x[row] = row == 4 ? 130 : 10 + 40 * row;
        table->y[row] = row == 4 ? 100 : 6 + 20 * row;
        table->width[row] = 100;
        table->height[row] = 100;
        table->glass[row] = row == 2 ? WINDOW_GLASS : 0;
        glass_paint(row, 0, 0, 100, 100, row == 1 ? 0x2060c0u : row == 2 ? 0xc04020u : row == 3 ? 0x20c060u : 0xa0a0a0u, true);
    }
    // The first is the one dragged: a press raised it over the rest and gave it the keys.
    compositor.stack[0] = 2, compositor.stack[1] = 3, compositor.stack[2] = 4, compositor.stack[3] = 1, compositor.stacked = 4;
    compositor.focus = 1;
}

static void over_render(unsigned int *into)
{
    compositor_damage(0, 0, OVER_SIDE, OVER_SIDE);
    compositor_present();
    memory_copy(into, compositor.scanout, sizeof(unsigned int) * OVER_SIDE * OVER_SIDE);
}

// The window row's preview being drawn, over rect, at alpha percent (held at 100, or fading from alpha).
static void over_preview(int row, const int *rect, unsigned int alpha, int fading, unsigned int how)
{
    compositor.preview_draw = row + 1;
    compositor.preview_row = fading ? COMPOSITOR_NONE : row;
    memory_copy(compositor.preview, rect, sizeof(compositor.preview));
    if (fading)
        memory_zero(compositor.preview, sizeof(compositor.preview));
    memory_copy(compositor.preview_from, rect, sizeof(compositor.preview_from));
    compositor.preview_from_alpha = alpha;
    compositor.preview_fading = fading;
    compositor.preview_at = 0;
    compositor.preview_how = how;
}

static void over_preview_off(void)
{
    compositor.preview_draw = 0;
    compositor.preview_row = COMPOSITOR_NONE;
    memory_zero(compositor.preview, sizeof(compositor.preview));
    compositor.preview_fading = 0;
}

// Whether x, y is in row's rectangle grown by margin.
static int over_in(int row, int x, int y, int margin)
{
    return x >= compositor.x[row] - margin && x < compositor.x[row] + (int)compositor.w[row] + margin &&
           y >= compositor.y[row] - margin && y < compositor.y[row] + (int)compositor.h[row] + margin;
}

// A point of rect, at least 8 px inside it and 50 above the band, that only window row is at (and no other within 8 px).
static int over_only(int row, const int *rect, int *px, int *py)
{
    for (int y = rect[1] + 8; y < rect[1] + rect[3] - 50; y += 2)
        for (int x = rect[0] + 8; x < rect[0] + rect[2] - 8; x += 2)
        {
            int clear = over_in(row, x, y, -6);

            for (int other = 1; other <= 4 && clear; other++)
                if (other != row && over_in(other, x, y, 8))
                    clear = 0;
            if (clear)
            {
                *px = x, *py = y;
                return 1;
            }
        }
    return 0;
}

// The preview's rows against the plain scene: each point is the plain pixel with the tint mixed over it, and no pixel
// outside the target changed. Answers how many points were as they should be, of those given.
static unsigned int over_compare(const unsigned int *plain, const unsigned int *with, const int *rect, unsigned int alpha,
                                 const int (*points)[2], unsigned int count)
{
    unsigned int ink = compositor_snap_ink(), good = 0, outside = 0;
    unsigned int cover = (ink >> 24) * alpha / 100;

    for (unsigned int at = 0; at < count; at++)
    {
        int x = points[at][0], y = points[at][1];
        unsigned int want = compositor_mix(plain[y * OVER_SIDE + x] & 0xffffffu, ink & 0xffffffu, cover);

        good += (with[y * OVER_SIDE + x] & 0xffffffu) == want && want != (plain[y * OVER_SIDE + x] & 0xffffffu);
    }
    for (int y = 0; y < OVER_SIDE; y++)
        for (int x = 0; x < OVER_SIDE; x++)
            if (with[y * OVER_SIDE + x] != plain[y * OVER_SIDE + x] &&
                (x < rect[0] || x >= rect[0] + rect[2] || y < rect[1] || y >= rect[1] + rect[3]))
                outside++;
    return outside ? 0u : good;
}

// Whether every point is the plain pixel, as with no preview: what lies over the tint.
static int over_clear(const unsigned int *plain, const unsigned int *with, const int (*points)[2], unsigned int count)
{
    for (unsigned int at = 0; at < count; at++)
        if (with[points[at][1] * OVER_SIDE + points[at][0]] != plain[points[at][1] * OVER_SIDE + points[at][0]])
            return 0;
    return 1;
}

static void check_compositor_preview_over(void)
{
    static unsigned int made[OVER_SIDE * OVER_SIDE], band[OVER_SIDE * OVER_SIDE], plain[OVER_SIDE * OVER_SIDE],
        with[OVER_SIDE * OVER_SIDE], scene[OVER_SIDE * OVER_SIDE];
    int target[4] = {128, 0, 128, 256}, points[5][2], found = 0, tab_x = 0, tab_y = 0;
    unsigned int held, fading, thumb = 0;
    unsigned int address_to first_map;
    struct taskbar_view view;

    over_setup(made, band);
    if (!compositor.table)
    {
        check("preview over: the table is mapped", false);
        return;
    }
    first_map = (unsigned int address_to)compositor.table;
    over_render(plain);
    // Where each kind of thing is, in the target: row 3 alone, row 4 alone and the glass window (row 2) alone, all under the window
    // dragged; the window dragged (row 1) alone; and a tab of the band.
    found += over_only(3, target, &points[0][0], &points[0][1]);
    found += over_only(4, target, &points[1][0], &points[1][1]);
    found += over_only(2, target, &points[2][0], &points[2][1]);
    found += over_only(1, target, &points[3][0], &points[3][1]);
    view = compositor_taskbar_view();
    {
        int x, y, w, h;

        if (taskbar_slot(&view, -2, &x, &y, &w, &h) && x + w / 2 >= target[0] && x + w / 2 < target[0] + target[2])
        {
            tab_x = x + w / 2, tab_y = y + h / 2;
            points[4][0] = tab_x, points[4][1] = tab_y;
            found += (plain[tab_y * OVER_SIDE + tab_x] & 0xffffffu) != (compositor_ground & 0xffffffu);
        }
    }
    check("preview over: the places are found: two windows and the glass window under the one dragged, the window dragged and the "
          "tray's tab in the target",
          found == 5);
    if (found != 5)
    {
        corner_scene_end(first_map);
        return;
    }

    over_preview(1, target, 100, 0, COMPOSITOR_RIGHT);
    over_render(with);
    held = over_compare(plain, with, target, 100, points, 3);
    check("preview over: held, the tint is over the windows under the one dragged, the glass one too, each the pixel it had with the "
          "tint mixed over it, and over nothing outside the target",
          held == 3);
    check("preview over: held, the window dragged and the tray's tab are over the tint, each the pixel it has with no preview",
          over_clear(plain, with, points + 3, 2));
    if (held != 3)
        string_format(log, "  preview over: held: %p of 3 as they should be\n", (positive)held);

    over_preview(1, target, 60, 1, COMPOSITOR_RIGHT);
    over_render(with);
    fading = over_compare(plain, with, target, 60, points, 3);
    check("preview over: fading, it is as faint over the three under the window dragged and as clear (the tint at the percent it fades "
          "to), and the window and the tab are still over it",
          fading == 3 && over_clear(plain, with, points + 3, 2));

    // A thumbnail of the assist is over the preview: its pixels are what they are with no preview.
    over_preview_off();
    compositor.assist_on = 1, compositor.assist_count = 1, compositor.assist_rows[0] = 3, compositor.assist_screen = 0;
    compositor.assist_rect[0][0] = 150, compositor.assist_rect[0][1] = 20, compositor.assist_rect[0][2] = 80;
    compositor.assist_rect[0][3] = 60;
    compositor.overlay_goal = 100;
    over_render(scene);
    over_preview(1, target, 100, 0, COMPOSITOR_RIGHT);
    over_render(with);
    thumb = (with[50 * OVER_SIDE + 190] == scene[50 * OVER_SIDE + 190]) && (with[40 * OVER_SIDE + 170] == scene[40 * OVER_SIDE + 170]) &&
            (with[60 * OVER_SIDE + 210] == scene[60 * OVER_SIDE + 210]) && with[10 * OVER_SIDE + 200] != scene[10 * OVER_SIDE + 200];
    check("preview over: the assist's thumbnail is over the tint (its pixels are as with no preview), and the tint is over what is beside it",
          thumb);
    compositor.assist_on = 0, compositor.assist_count = 0, compositor.overlay_goal = 0;

    // A preview that goes leaves nothing of itself: the target damaged as it leaves, and the fade ended, the screen is the plain one.
    {
        int trail = 0;

        over_preview_off();
        over_render(scene);
        over_preview(1, target, 100, 0, COMPOSITOR_RIGHT);
        over_render(with);
        compositor_preview_leave();
        over_preview_off();
        compositor_present();
        for (unsigned int at = 0; at < OVER_SIDE * OVER_SIDE; at++)
            trail += compositor.scanout[at] != scene[at];
        check("preview over: a preview that goes leaves no trail: the screen is the one with no preview", trail == 0);
    }
    corner_scene_end(first_map);

    // Two screens at 100 and 200 percent: a window on the second, under the dragged one on the first (a press raised it), under the
    // tint of the second's left half, which has the second's line and a window's corners at the second's scale.
    {
        static unsigned int pair[OVER_SIDE * OVER_SIDE];
        int rect[4] = {128, 0, 64, 256}, point[1][2] = {{170, 70}}, seen, line, radius;
        unsigned int address_to mapped = corner_scene(pair, 2, 100, 200, 80);
        struct compositor_frame frame;

        if (!mapped)
        {
            check("preview over: the table is mapped for two screens", false);
            return;
        }
        compositor.stack[0] = 2, compositor.stack[1] = 1, compositor.stacked = 2;
        over_render(scene);
        over_preview(1, rect, 100, 0, COMPOSITOR_RIGHT);
        over_render(with);
        compositor_frame_of(1, &frame);
        seen = over_compare(scene, with, rect, 100, point, 1) == 1;
        line = frame.preview_line, radius = frame.preview_radius;
        corner_scene_end(mapped);
        check("preview over: on the second of two screens (at 200 percent) the tint is over a window under the one dragged, and has "
              "that screen's line (2 px) and a window's corners there (16 px)",
              seen && line == 2 && radius == 2 * UI_RADIUS);
    }
}

/*
        A tile torn off shrinks to its own size about the hand: at its first frame it is the tile as it was, moved by the hand
        since the press so the point pressed is under the pointer; half way it is between the two with the pointer at the same
        share of its width and the same distance from its top; the hand moving on takes the glide with it; at the end it is
        the window's own rectangle. Reduced motion is the size at once.
*/
static void check_compositor_restore_glide(void)
{
    struct compositor_frame frame;
    positive now;

    if (!layout_table_map(256))
    {
        check("restore glide: the table is mapped", false);
        return;
    }
    restore_fresh();
    compositor.table->motion = 0;
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    compositor.glide[1] = 0;
    screens_hand(128, 10, 1);
    screens_hand(128, 30, 0);
    compositor_frame_of(1, &frame);
    check("restore glide: torn off, its first frame is the tile moved by the hand, the point pressed under the pointer",
          compositor.glide[1] && compositor.drag_glide == 2 && frame.x == 0 && frame.y == 20 && frame.width == 256 && frame.height == 256);
    now = clock_monotonic_nanoseconds();
    compositor.glide_at[1] = now - UI_WINDOW_NS / 2;
    compositor_frame_of(1, &frame);
    check("restore glide: half way it is between the tile and its size, the pointer at half its width and 10 px from its top",
          frame.width > 100 && frame.width < 256 && frame.x + frame.width / 2 - 128 <= 1 && 128 - (frame.x + frame.width / 2) <= 1 &&
              frame.y == 20);
    screens_hand(138, 40, 0);
    compositor_frame_of(1, &frame);
    check("restore glide: the hand moving on takes the glide with it", frame.x + frame.width / 2 - 138 <= 1 &&
                                                                          138 - (frame.x + frame.width / 2) <= 1 && frame.y == 30);
    compositor.glide_at[1] = now - UI_WINDOW_NS - 1;
    compositor_glide_keep();
    compositor_frame_of(1, &frame);
    check("restore glide: at the end it is the window's own rectangle, and the glide is over",
          !compositor.glide[1] && !compositor.drag_glide && frame.x == 138 - 50 && frame.y == 30 && frame.width == 100 && frame.height == 80);
    screens_hand(138, 40, -1);
    compositor.press = COMPOSITOR_NONE;

    restore_fresh();
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    screens_hand(128, 10, 1);
    screens_hand(128, 30, 0);
    compositor_frame_of(1, &frame);
    check("restore glide: reduced motion, it is its own size at once", !compositor.glide[1] && frame.width == 100 && frame.x == 78);
    screens_hand(128, 30, -1);
    compositor.press = COMPOSITOR_NONE;
}

/*
        A tile resized stays a tile: the left half's inner edge taken and moved leaves its screen's edges where they were (it
        does not move a pixel off them as a floating window's line would), it is still the left half, and it remembers the
        rectangle from before it was arranged, which a drag away gives back. Two tiles resized by the edge they share stay
        tiles and keep theirs too.
*/
static void check_compositor_tile_resize(void)
{
    if (!layout_table_map(256))
    {
        check("tile resize: the table is mapped", false);
        return;
    }
    restore_fresh();
    compositor_arrange(1, COMPOSITOR_LEFT);
    screens_hand(130, 100, 1);
    check("tile resize: (the left half's inner edge is taken)", compositor.resize == 1);
    screens_hand(150, 100, 0);
    check("tile resize: moved 20 px it is still the left half, its screen's edges where they were, 148 wide",
          layout_is(1, COMPOSITOR_LEFT, 0, 0, 148, 256));
    screens_hand(150, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    check("tile resize: let go, it is still the left half and remembers the rectangle from before it was arranged",
          compositor.arranged[1] == COMPOSITOR_LEFT && restore_kept_is(1, 60, 60, 100, 80));
    screens_hand(64, 10, 1);
    screens_hand(100, 120, 0);
    check("tile resize: dragged away, it is that rectangle's size", compositor.w[1] == 100 && compositor.h[1] == 80);
    screens_hand(100, 120, -1);
    compositor.press = COMPOSITOR_NONE;

    /* Two tiles side by side, resized by the edge they share. */
    restore_fresh();
    layout_show(2, 150, 150, 90, 60);
    compositor.stack[0] = 1, compositor.stack[1] = 2, compositor.stacked = 2;
    compositor_arrange(1, COMPOSITOR_LEFT);
    compositor_arrange(2, COMPOSITOR_RIGHT);
    screens_hand(128, 128, 1);
    screens_hand(148, 128, 0);
    check("tile resize: (the shared edge is taken by both)", compositor.shared_row >= 0 && compositor.w[1] == 148 && compositor.x[2] == 148);
    screens_hand(148, 128, -1);
    compositor.press = COMPOSITOR_NONE;
    check("tile resize: let go, both are still tiles and each remembers its own rectangle from before",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.arranged[2] == COMPOSITOR_RIGHT && restore_kept_is(1, 60, 60, 100, 80) &&
              restore_kept_is(2, 150, 150, 90, 60));
}

/*
        A window's own least: a program that says one in the table (least_width and least_height, logical pixels) is not made smaller
        than it by an edge dragged past it, nor by a tile narrower than it. A window 200 by 150 at 20, 20 on a screen of 256 that says
        150 by 120: its right edge dragged 120 to the left leaves it 150 wide, its foot dragged 110 up leaves it 120 tall, the edges the
        hand does not hold where they were; saying none, the same drag leaves it the desktop's least, 96. Put in the left half (128 wide)
        it is 150 wide from the screen's left edge, and in the right half 150 wide to the screen's right edge.
*/
static void check_compositor_least(void)
{
    b32 wide, tall, none, left, right;

    if (!layout_table_map(256))
    {
        check("least: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    compositor.table->motion = 1;
    layout_show(1, 20, 20, 200, 150);
    compositor.stack[0] = 1, compositor.stacked = 1;
    compositor.table->least_width[1] = 150, compositor.table->least_height[1] = 120;
    screens_hand(222, 95, 1);
    screens_hand(102, 95, 0);
    wide = compositor.resize == 1 && compositor.w[1] == 150 && compositor.table->want_width[1] == 150 && compositor.x[1] == 20 && compositor.h[1] == 150;
    screens_hand(102, 95, -1);
    compositor.press = COMPOSITOR_NONE;
    screens_hand(95, 172, 1);
    screens_hand(95, 62, 0);
    tall = compositor.resize == 1 && compositor.h[1] == 120 && compositor.table->want_height[1] == 120 && compositor.y[1] == 20 && compositor.w[1] == 150;
    screens_hand(95, 62, -1);
    compositor.press = COMPOSITOR_NONE;
    check("least: a window that says its least is not made narrower or shorter than it by an edge dragged past it, the edges not held where they were",
          wide && tall);
    compositor.table->least_width[1] = compositor.table->least_height[1] = 0;
    screens_hand(172, 95, 1);
    screens_hand(52, 95, 0);
    none = compositor.w[1] == WINDOW_MIN_WIDTH && compositor.x[1] == 20;
    screens_hand(52, 95, -1);
    compositor.press = COMPOSITOR_NONE;
    check("least: one that says none is made as narrow as the desktop's least", none);
    compositor.table->least_width[1] = 150, compositor.table->least_height[1] = 120;
    compositor_arrange(1, COMPOSITOR_LEFT);
    left = layout_is(1, COMPOSITOR_LEFT, 0, 0, 150, 256) && compositor.w[1] == 150;
    compositor_arrange(1, COMPOSITOR_RIGHT);
    right = layout_is(1, COMPOSITOR_RIGHT, 106, 0, 150, 256) && compositor.x[1] + (int)compositor.w[1] == 256;
    check("least: a tile narrower than a window's least is the window at its least, from the tile's edge that is its screen's", left && right);
    compositor.table->least_width[1] = compositor.table->least_height[1] = 0;
}

/*
        A divider of more than two tiles moves as one: the left half beside the two right quarters share one line, and the
        right quarters' left edge taken and moved widens the left half and narrows both quarters, so no gap or overlap opens
        and what a click finds follows them; its indicator spans the whole line, and a press anywhere along it takes all
        three, but at the corner of a quarter where the line meets the edge between the two quarters, which is that
        quarter's own corner; let go, all three are still tiles keeping their rectangles; Escape during it puts all three back.
*/
static void divider_fresh(void)
{
    restore_fresh();
    layout_show(2, 150, 20, 90, 60);
    layout_show(3, 150, 160, 90, 60);
    compositor.stack[0] = 1, compositor.stack[1] = 2, compositor.stack[2] = 3, compositor.stacked = 3;
    compositor_arrange(1, COMPOSITOR_LEFT);
    compositor_arrange(2, COMPOSITOR_TOP_RIGHT);
    compositor_arrange(3, COMPOSITOR_BOTTOM_RIGHT);
}

static void check_compositor_divider(void)
{
    if (!layout_table_map(256))
    {
        check("divider: the table is mapped", false);
        return;
    }
    divider_fresh();
    screens_hand(126, 100, 0);
    check("divider: the pointer on the line shows the indicator over all of it, from the top of the screen to its bottom, not one quarter's",
          compositor.mark[2] > 0 && compositor.mark[1] == 0 && compositor.mark[3] == 256 && compositor.mark_rows == 14u);
    screens_hand(126, 100, 1);
    screens_hand(146, 100, 0);
    check("divider: moved 20 px, the left half is 20 wider and both right quarters 20 narrower, meeting it",
          layout_is(1, COMPOSITOR_LEFT, 0, 0, 148, 256) && layout_is(2, COMPOSITOR_TOP_RIGHT, 148, 0, 108, 128) &&
              layout_is(3, COMPOSITOR_BOTTOM_RIGHT, 148, 128, 108, 128));
    check("divider: a click finds them where they are: the point just right of the line is the bottom right quarter's",
          compositor_under(150, 200, &(int){0}) == 3);
    screens_hand(146, 100, -1);
    compositor.press = COMPOSITOR_NONE;
    check("divider: let go, all three are still tiles keeping the rectangles from before",
          compositor.arranged[1] == COMPOSITOR_LEFT && compositor.arranged[2] == COMPOSITOR_TOP_RIGHT &&
              compositor.arranged[3] == COMPOSITOR_BOTTOM_RIGHT && restore_kept_is(1, 60, 60, 100, 80) && restore_kept_is(3, 150, 160, 90, 60));

    // A press at the first row, the quarter points and the last quarter of the line takes all three, and at the corner where the
    // line meets the edge between the quarters it is the quarter's own (a corner keeps priority, as a single window's does).
    // (The tray's tab is over the bottom of the screen's middle, where the line ends: nothing is pressed there but the tab.)
    {
        static const int along[] = {0, 1, 40, 100, 127 - 8, 130, 190, 210};
        int all = 1, ends = 1;

        for (unsigned int at = 0; at < array_count(along); at++)
        {
            int hover, took;

            divider_fresh();
            screens_hand(126, along[at], 0);
            hover = compositor.mark[2] > 0 && compositor.mark[1] == 0 && compositor.mark[3] == 256;
            ends &= hover;
            screens_hand(126, along[at], 1);
            took = compositor.resize >= 0 && compositor.shared_row >= 0 &&
                   (compositor.shared_lo_set | compositor.shared_hi_set) == 14u;
            all &= took;
            if (!hover || !took)
                string_format(log, "  divider: at y %p: mark %p %p %p, resize %p, shared %p, rows %p\n", (positive)along[at],
                              (positive)compositor.mark[1], (positive)compositor.mark[3], (positive)compositor.mark_rows,
                              (positive)compositor.resize, (positive)compositor.shared_row,
                              (positive)(compositor.shared_lo_set | compositor.shared_hi_set));
            screens_hand(126, along[at], -1);
            compositor.press = COMPOSITOR_NONE;
        }
        check("divider: the indicator is the whole line wherever along it the pointer is, from the first row of the screen to the last", ends);
        check("divider: a press on the first row, the top quarter, the middle of each quarter and the last quarter of the line "
              "takes all three tiles", all);
    }
    {
        int corner;

        divider_fresh();
        screens_hand(129, 127, 0);
        screens_hand(129, 127, 1);
        corner = compositor.resize == 3 && compositor.shared_row == COMPOSITOR_NONE &&
                 compositor.edges[3] == (COMPOSITOR_EDGE_LEFT | COMPOSITOR_EDGE_TOP);
        screens_hand(129, 127, -1);
        compositor.press = COMPOSITOR_NONE;
        divider_fresh();
        compositor.stack[1] = 3, compositor.stack[2] = 2;
        screens_hand(129, 127, 0);
        screens_hand(129, 127, 1);
        corner &= compositor.resize == 2 && compositor.shared_row == COMPOSITOR_NONE &&
                  compositor.edges[2] == (COMPOSITOR_EDGE_LEFT | COMPOSITOR_EDGE_BOTTOM);
        screens_hand(129, 127, -1);
        compositor.press = COMPOSITOR_NONE;
        check("divider: where the line meets the edge between the two quarters each quarter's corner is its own resize, not the line's",
              corner);
    }

    divider_fresh();
    screens_hand(126, 100, 1);
    screens_hand(146, 100, 0);
    compositor_key(0, CK_KEY_ESC, 1, 0);
    compositor_key(0, CK_KEY_ESC, 0, 0);
    check("divider: Escape during it puts all three back", layout_is(1, COMPOSITOR_LEFT, 0, 0, 128, 256) &&
                                                               layout_is(2, COMPOSITOR_TOP_RIGHT, 128, 0, 128, 128) &&
                                                               layout_is(3, COMPOSITOR_BOTTOM_RIGHT, 128, 128, 128, 128));
    screens_hand(146, 100, -1);
    compositor.press = COMPOSITOR_NONE;
}

/*
        The indicator of a divider is drawn at the scale of the screen its tiles are on, whole pixels and one at the least on
        either side of the line, and as long as the line: two halves at 50, 75, 100 and 200 percent, and two screens at 100
        and 200 percent, the halves of each.
*/
static void check_compositor_divider_scales(void)
{
    static const int scales[] = {50, 75, 100, 200};
    unsigned int widths = ~0u, extents = ~0u;
    int on_two;

    if (!layout_table_map(512))
    {
        check("divider scales: the table is mapped", false);
        return;
    }
    for (unsigned int at = 0; at < array_count(scales); at++)
    {
        int half = 2 * scales[at] / 100 > 1 ? 2 * scales[at] / 100 : 1;

        layout_setup(512, 1);
        compositor.table->motion = 1;
        compositor.scale = scales[at];
        layout_show(1, 60, 60, 100, 80);
        layout_show(2, 150, 150, 90, 60);
        compositor.stack[0] = 1, compositor.stack[1] = 2, compositor.stacked = 2;
        compositor_arrange(1, COMPOSITOR_LEFT);
        compositor_arrange(2, COMPOSITOR_RIGHT);
        screens_hand(255, 100, 0);
        widths &= compositor.mark[2] == 2 * half && compositor.mark[0] == 256 - half ? ~0u : ~(1u << at);
        extents &= compositor.mark[1] == 0 && compositor.mark[3] == 512 ? ~0u : ~(1u << at);
        if (compositor.mark[2] != 2 * half || compositor.mark[1] != 0 || compositor.mark[3] != 512)
            string_format(log, "  divider scales: %p percent: mark %p %p %p %p\n", (positive)scales[at], (positive)compositor.mark[0],
                          (positive)compositor.mark[1], (positive)compositor.mark[2], (positive)compositor.mark[3]);
    }
    check("divider scales: the indicator is a pixel or more either side of the line, 2 logical pixels at the scale (1 at 50 and 75 percent, "
          "2 at 100, 4 at 200)", widths == ~0u);
    check("divider scales: and as long as the line at every scale", extents == ~0u);

    layout_setup(512, 1);
    compositor.table->motion = 1;
    compositor.outputs = 2;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 256, compositor.screen[0][3] = 256;
    compositor.screen[1][0] = 256, compositor.screen[1][1] = 0, compositor.screen[1][2] = 256, compositor.screen[1][3] = 256;
    compositor.output[0].scale = 100, compositor.output[1].scale = 200;
    for (unsigned int row = 0; row < WINDOWS; row++)
        compositor.wake[row] = -1;
    layout_show(1, 10, 10, 100, 80);
    layout_show(2, 20, 20, 100, 80);
    layout_show(3, 150, 150, 90, 60);
    layout_show(4, 160, 160, 90, 60);
    compositor.stack[0] = 1, compositor.stack[1] = 2, compositor.stack[2] = 3, compositor.stack[3] = 4, compositor.stacked = 4;
    compositor_arrange_on(1, COMPOSITOR_LEFT, 0);
    compositor_arrange_on(2, COMPOSITOR_RIGHT, 0);
    compositor_arrange_on(3, COMPOSITOR_LEFT, 1);
    compositor_arrange_on(4, COMPOSITOR_RIGHT, 1);
    screens_hand(127, 100, 0);
    on_two = compositor.mark[2] == 4 && compositor.mark[1] == 0 && compositor.mark[3] == 256;
    screens_hand(383, 100, 0);
    on_two &= compositor.mark[2] == 8 && compositor.mark[0] == 384 - 4 && compositor.mark[1] == 0 && compositor.mark[3] == 256;
    if (!on_two)
        string_format(log, "  divider scales: two screens: mark %p %p %p %p\n", (positive)compositor.mark[0], (positive)compositor.mark[1],
                      (positive)compositor.mark[2], (positive)compositor.mark[3]);
    check("divider scales: on two screens at 100 and 200 percent the halves of each have the indicator at their own screen's scale "
          "(4 and 8 px across), the length of their line",
          on_two);
}

/*
        The grid is over a window dragged under it, so its tiles, the targets the hand aims at, stay seen: the window being
        moved is tinted where the grid's row lies across it. The snap preview of the tile under the hand is under the window:
        below the row, inside the preview, the window is its own picture.
*/
static void check_compositor_grid_drag(void)
{
    const unsigned int picture = 0xc06020u;

    if (!layout_table_map(256))
    {
        check("grid drag: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    compositor.table->motion = 1;
    assist_fresh();
    compositor.showing[3] = 0;
    layout_show(2, 100, 40, 60, 60);
    layout_paint(2, picture);
    screens_hand(120, 50, 1);
    screens_hand(120, 4, 0);
    grid_dwell();
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    check("grid drag: the grid is up while the window is held over the top, and the window is moved with the hand",
          compositor.grid_on && compositor.drag == 2 && compositor.y[2] < 25);
    check("grid drag: the grid is over the window being moved, which is tinted by it where the row lies across it",
          (layout_pixels[12 * 256 + 130] & 0xffffffu) != (picture & 0xffffffu));
    {
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int tile[4], rect[4], below = 0, inside = 0;

        if (compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
            compositor_grid_tile(screens[compositor.grid_screen], 0, tile);
            below = tile[1] + tile[3] + 2;
            inside = compositor_preview_shown(rect) && compositor.preview_draw == 3 && 130 >= rect[0] && 130 < rect[0] + rect[2] &&
                     below >= rect[1] && below < rect[1] + rect[3] && below < (int)(compositor.y[2] + compositor.h[2]);
        }
        check("grid drag: the snap preview of the tile under the hand is under the window being moved: below the row, inside the "
              "preview, the window is its own picture",
              inside && (layout_pixels[below * 256 + 130] & 0xffffffu) == (picture & 0xffffffu));
    }
    /* The hand moves within the top left tile with the grid up: that frame's target is the tile, which has no snap edges, so the
       next frame's dead zone reads none (snap_held), not the edges of a snap the tile replaced. */
    screens_hand(122, 4, 0);
    check("grid drag: the window moved within a grid tile leaves no snap edges for the next frame's dead zone (snap_held 0)",
          compositor.grid_on && compositor.snap_held == 0);
    screens_hand(120, 4, -1);
    compositor.press = COMPOSITOR_NONE;
}

/*
        The snap assist and the grid fade: a thumbnail comes up from the wallpaper (its picture veiled) and is the picture at the
        end of COMPOSITOR_SNAP_FADE_NS, and goes out from the picture to the wallpaper; a grid tile has the window's line and
        corners in the snap's colour, the line outside the tile where the wallpaper is.
*/
static void check_compositor_overlay_fade(void)
{
    const unsigned int picture = 0xc06020u, ground = compositor_ground;
    unsigned int centre_at, border_at;
    positive now;
    int at;

    if (!layout_table_map(256))
    {
        check("overlay fade: the table is mapped", false);
        return;
    }
    layout_setup(256, 1);
    compositor.table->motion = 0;
    assist_fresh();
    compositor.showing[3] = 0;
    layout_paint(2, picture);
    layout_drop(1, 20, 4, 0, 100);
    now = clock_monotonic_nanoseconds();
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    at = assist_row_at(2);
    centre_at = layout_pixels[(compositor.assist_rect[at][1] + compositor.assist_rect[at][3] / 2) * 256 +
                              compositor.assist_rect[at][0] + compositor.assist_rect[at][2] / 2] & 0xffffffu;
    check("overlay fade: a thumbnail comes up from the wallpaper: at its first present its picture is not drawn yet",
          at >= 0 && centre_at == (ground & 0xffffffu));

    compositor.overlay_at = now - COMPOSITOR_SNAP_FADE_NS / 2;
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    centre_at = layout_pixels[(compositor.assist_rect[at][1] + compositor.assist_rect[at][3] / 2) * 256 +
                              compositor.assist_rect[at][0] + compositor.assist_rect[at][2] / 2] & 0xffffffu;
    check("overlay fade: half way up the picture is neither the wallpaper nor the picture",
          centre_at != (ground & 0xffffffu) && centre_at != (picture & 0xffffffu));

    compositor.overlay_at = now - COMPOSITOR_SNAP_FADE_NS - 1;
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    centre_at = layout_pixels[(compositor.assist_rect[at][1] + compositor.assist_rect[at][3] / 2) * 256 +
                              compositor.assist_rect[at][0] + compositor.assist_rect[at][2] / 2] & 0xffffffu;
    check("overlay fade: up, the thumbnail is its picture", centre_at == (picture & 0xffffffu));

    compositor_assist_close();
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    centre_at = layout_pixels[(compositor.assist_rect[at][1] + compositor.assist_rect[at][3] / 2) * 256 +
                              compositor.assist_rect[at][0] + compositor.assist_rect[at][2] / 2] & 0xffffffu;
    check("overlay fade: a thumbnail that goes starts from its picture, and is still there while it fades",
          centre_at == (picture & 0xffffffu) && compositor.assist_count == 1);
    compositor.overlay_at = now - COMPOSITOR_SNAP_FADE_NS - 1;
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    centre_at = layout_pixels[(compositor.assist_rect[at][1] + compositor.assist_rect[at][3] / 2) * 256 +
                              compositor.assist_rect[at][0] + compositor.assist_rect[at][2] / 2] & 0xffffffu;
    check("overlay fade: gone, the wallpaper is where it was, and the thumbnails are none", centre_at == (ground & 0xffffffu));
    compositor_overlays_keep();
    check("overlay fade: faded out, the thumbnails are none", compositor.assist_count == 0);

    /* Drawn only where damage says, as the loop does: a fade that ends draws where it was once more, so no faint last frame
       of it stays on the screen. */
    layout_setup(256, 1);
    compositor.table->motion = 0;
    assist_fresh();
    compositor.showing[3] = 0;
    layout_paint(2, picture);
    layout_drop(1, 20, 4, 0, 100);
    compositor_damage(0, 0, 256, 256);
    compositor_present(); /* the fade in starts */
    compositor.overlay_at = clock_monotonic_nanoseconds() - COMPOSITOR_SNAP_FADE_NS - 1;
    compositor_overlays_keep(); /* and is over */
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    at = assist_row_at(2);
    {
        int rect[4] = {compositor.assist_rect[at][0], compositor.assist_rect[at][1], compositor.assist_rect[at][2],
                       compositor.assist_rect[at][3]};
        unsigned int left = 0;

        compositor_assist_close();
        compositor_present();
        compositor.overlay_at = clock_monotonic_nanoseconds() - COMPOSITOR_SNAP_FADE_NS / 2;
        compositor_overlays_keep();
        compositor_present();
        compositor.overlay_at = clock_monotonic_nanoseconds() - COMPOSITOR_SNAP_FADE_NS - 1;
        compositor_overlays_keep();
        compositor_present();
        for (int y = rect[1]; y < rect[1] + rect[3]; y++)
            for (int x = rect[0]; x < rect[0] + rect[2]; x++)
                if (x < (int)compositor.x[2] - 2 || x >= (int)(compositor.x[2] + compositor.w[2]) + 2 || y < (int)compositor.y[2] - 2 ||
                    y >= (int)(compositor.y[2] + compositor.h[2]) + 2) /* the window itself lies there too */
                    left += (layout_pixels[y * 256 + x] & 0xffffffu) != (ground & 0xffffffu);
        check("overlay fade: a fade that ends draws where it was once more: nothing of the thumbnail is left on the screen", left == 0);
    }

    /* The grid: a tile's line is the snap's colour at its full strength, outside the tile where the wallpaper is. */
    layout_setup(256, 1);
    compositor.table->motion = 1;
    assist_fresh();
    compositor.showing[2] = compositor.showing[3] = 0;
    screens_hand(80, 64, 1);
    screens_hand(127, 4, 0);
    grid_dwell();
    compositor_damage(0, 0, 256, 256);
    compositor_present();
    border_at = layout_pixels[12 * 256 + 27] & 0xffffffu;
    /* The line's corner arc runs from (27, 3) to (35, -1)-ish: (29, 1) is on it, and (31, 3) is inside the tile. */
    check("overlay fade: the grid's tile has the snap's line round it, its corner is rounded, and inside the corner is the tile",
          compositor.grid_on && border_at == (compositor_snap_ink() & 0xffffffu) && (layout_pixels[27] & 0xffffffu) != (compositor_snap_ink() & 0xffffffu) &&
              (layout_pixels[3 * 256 + 31] & 0xffffffu) != (compositor_snap_ink() & 0xffffffu));
    screens_hand(127, 4, -1);
    compositor.press = COMPOSITOR_NONE;
    compositor.table->motion = 0;
}

static void check_compositor_wheel(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int events;
    int last, sum;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
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
        check("wheel: the table and the device are made", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_present();
    compositor.inputs[0] = wheel_pipe[0];
    compositor.input_count = 1;
    compositor.pointer_x = 60;
    compositor.pointer_y = 40;

    /* A wheel that reports in 120ths: a notch is read twice, sent once. */
    compositor.hires[0] = 1;
    wheel_notch(1);
    compositor_input_read(0);
    sum = wheel_unread(1, &events, &last);
    check("wheel: a notch of a wheel in 120ths is one turn of 120 for the window under the pointer", sum == 120 && events == 1);
    check("wheel: and moonwater debug is told it was read twice, used once and sent to that window",
          table->input_wheel_read[0] == 2 && table->input_wheel_used[0] == 1 && table->wheel_sent == 1 && table->wheel_row == 1 &&
              table->wheel_x == 10 && table->wheel_y == 10 && table->input_wheel_last[0] == 0);
    table->event_tail[1] = table->event_head[1];

    /* One whose notches are all it says, and one that says eighths of a notch. */
    compositor.hires[0] = 0;
    wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL, -1);
    wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
    compositor_input_read(0);
    sum = wheel_unread(1, &events, &last);
    check("wheel: a notch of a wheel in notches is a turn of -120", sum == -120 && events == 1);
    table->event_tail[1] = table->event_head[1];
    compositor.hires[0] = 1;
    for (int step = 0; step < 8; step++)
    {
        if (step == 7)
            wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL, 1);
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL_HI, 15);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
    }
    compositor_input_read(0);
    sum = wheel_unread(1, &events, &last);
    check("wheel: eight eighths are eight turns that are a notch", sum == 120 && events == 8);
    table->event_tail[1] = table->event_head[1];

    /*
            A swipe at a window that is not reading: 150 notches, faster than it reads any of them. The compositor reads
            its device every 40 notches: a pipe of a user over the kernel's soft limit of pipe pages holds two pages
            (341 events), and on a box running many lanes the rest of the swipe was refused at the write.
    */
    for (int notch = 0; notch < 150; notch++)
    {
        wheel_notch(-1);
        if (notch % 40 == 39)
            compositor_input_read(0);
    }
    compositor_input_read(0);
    sum = wheel_unread(1, &events, &last);
    check("wheel: a swipe of 150 notches at a window that is not reading is all there when it reads",
          sum == -150 * 120 && table->wheel_full == 0 && events < WINDOWS_EVENTS);
    table->event_tail[1] = table->event_head[1];

    /* Turned back in the middle of one, the way back is not added to the way there. */
    for (int notch = 0; notch < 80; notch++)
    {
        wheel_notch(notch < 40 ? 1 : -1);
        if (notch % 40 == 39)
            compositor_input_read(0);
    }
    compositor_input_read(0);
    sum = wheel_unread(1, &events, &last);
    check("wheel: and a swipe turned back half way comes to nothing, ending the way it was turned last", sum == 0 && last < 0);
    table->event_tail[1] = table->event_head[1];

    /*
            The program takes an event's number before its tail moves on (windows_event leaves 0 in its place). A turn
            that comes between the two is not added to the taken number, where the program would never see it: it is
            an event of its own.
    */
    for (int notch = 0; notch < 40; notch++)
        wheel_notch(1);
    compositor_input_read(0);
    {
        unsigned int head = table->event_head[1], taken = (head - 1) % WINDOWS_EVENTS;
        int number = __atomic_exchange_n(&table->event_a[1][taken], 0, __ATOMIC_ACQ_REL);

        wheel_notch(1);
        compositor_input_read(0);
        check("wheel: a turn after the program took the last one's number is an event of its own, not lost in the taken one",
              number > 0 && table->event_a[1][taken] == 0 && table->event_head[1] == head + 1 &&
                  table->event_a[1][head % WINDOWS_EVENTS] == 120);
    }
    table->event_tail[1] = table->event_head[1];

    /*
            A place the ring had no room for (full, its last event not a motion) is not taken as told: the wheel after
            the program has read the ring tells it first, or the program turns it where the pointer was before.
    */
    {
        unsigned int head;

        compositor_pointer_to(1);
        table->event_tail[1] = table->event_head[1];
        for (int at = 0; at < WINDOWS_EVENTS; at++)
            compositor_post(1, WINDOW_EVENT_KEY, 0, 30, 0);
        compositor.pointer_x = 90;
        compositor_pointer_to(1);
        table->event_tail[1] = head = table->event_head[1];
        wheel_notch(1);
        compositor_input_read(0);
        check("wheel: a place a full ring had no room for is told before the next turn, not taken as told",
              table->event_type[1][head % WINDOWS_EVENTS] == WINDOW_EVENT_MOTION &&
                  table->event_a[1][head % WINDOWS_EVENTS] == 40 &&
                  table->event_type[1][(head + 1) % WINDOWS_EVENTS] == WINDOW_EVENT_WHEEL);
        table->event_tail[1] = table->event_head[1];
        compositor.pointer_x = 60;
    }

    /* With no window under the pointer, and when the kernel had to drop what was not read in time. */
    compositor.pointer_x = 5;
    compositor.pointer_y = 5;
    wheel_notch(1);
    wheel_feed(COMPOSITOR_EV_SYN, COMPOSITOR_SYN_DROPPED, 0);
    compositor_input_read(0);
    check("wheel: a notch with no window under the pointer is said to have gone nowhere, and a drop is counted",
          table->wheel_nowhere == 1 && table->wheel_row < 0 && table->input_dropped[0] == 1);

    /*
            A touchpad, as the kernel reports one: its finger's place in its units (40 to a millimetre here), BTN_TOUCH,
            and a tool key that counts the fingers. It used to be taken as a tablet: the pointer jumped to where the
            finger was, every touch was a press, and two fingers did nothing.
    */
    compositor.touchpad[0] = 1;
    compositor.pad[0] = (struct compositor_pad){0};
    compositor.pad[0].per_mm_x = compositor.pad[0].per_mm_y = 40;
    compositor.flat = 1;
    compositor.sensitivity = 100;
    compositor.pointer_x = 60;
    compositor.pointer_y = 40;
    table->event_tail[1] = table->event_head[1];
    {
        unsigned int head = table->event_head[1], buttons = 0;
        int moved_x;

        wheel_feed(COMPOSITOR_EV_ABS, 0, 4000);
        wheel_feed(COMPOSITOR_EV_ABS, 1, 2000);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOUCH, 1);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOOL_FINGER, 1);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        for (unsigned int at = head; at != table->event_head[1]; at++)
            buttons += table->event_type[1][at % WINDOWS_EVENTS] == WINDOW_EVENT_BUTTON;
        check("pad: a finger put on a touchpad is no press, and the pointer stays where it was",
              buttons == 0 && compositor.pointer_x == 60 && compositor.pointer_y == 40);
        // Two millimetres to the right, a report at a time.
        for (int step = 1; step <= 4; step++)
        {
            wheel_feed(COMPOSITOR_EV_ABS, 0, 4000 + step * 20);
            wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        }
        compositor_input_read(0);
        moved_x = compositor.pointer_x - 60;
        check("pad: a finger moved two millimetres moves the pointer on from where it was, forty counts at one to one",
              moved_x == 40 && compositor.pointer_y == 40);
        // The kernel dropped what it could not hand over in time, a lift and a touch twenty millimetres away among it:
        // the next report is where the finger is now, and no move of the pointer.
        wheel_feed(COMPOSITOR_EV_SYN, COMPOSITOR_SYN_DROPPED, 0);
        wheel_feed(COMPOSITOR_EV_ABS, 0, 4080 + 800);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        check("pad: after the kernel dropped reports, the finger's next place is no jump of the pointer",
              compositor.pointer_x == 100 && compositor.pointer_y == 40);
        compositor.pointer_x = compositor.kept_x = 100;
        // A second finger, and the two ten millimetres up (12 of the wheel's 120ths a millimetre): a notch of the wheel up, and no move
        // of the pointer.
        table->event_tail[1] = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOOL_FINGER, 0);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOOL_DOUBLETAP, 1);
        wheel_feed(COMPOSITOR_EV_ABS, 1, 3000);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        for (int step = 1; step <= 10; step++)
        {
            wheel_feed(COMPOSITOR_EV_ABS, 1, 3000 - step * 40);
            wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        }
        compositor_input_read(0);
        sum = wheel_unread(1, &events, &last);
        check("pad: two fingers ten millimetres up are a notch of the wheel up, and the pointer stays",
              sum == 120 && compositor.pointer_x == 100 && compositor.pointer_y == 40);
        table->event_tail[1] = table->event_head[1];
        for (int step = 1; step <= 20; step++)
        {
            wheel_feed(COMPOSITOR_EV_ABS, 1, 2600 + step * 40);
            wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        }
        compositor_input_read(0);
        sum = wheel_unread(1, &events, &last);
        check("pad: and twenty millimetres down are two notches down", sum == -240);
        // The pad pressed with two fingers on it is the right button, let go as the right.
        table->event_tail[1] = table->event_head[1];
        head = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_LEFT, 1);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_LEFT, 0);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        buttons = 0;
        for (unsigned int at = head; at != table->event_head[1]; at++)
            if (table->event_type[1][at % WINDOWS_EVENTS] == WINDOW_EVENT_BUTTON)
                buttons = buttons << 16 | (unsigned int)table->event_a[1][at % WINDOWS_EVENTS];
        check("pad: pressed with two fingers on it, it is the right button down and up", buttons == (0x111u << 16 | 0x111u));
        compositor.grab = COMPOSITOR_NONE;
        // Lifted, nothing moves or turns.
        table->event_tail[1] = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOUCH, 0);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOOL_DOUBLETAP, 0);
        wheel_feed(COMPOSITOR_EV_ABS, 1, 500);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        sum = wheel_unread(1, &events, &last);
        // The lift is one stop (WINDOW_WHEEL_STOP, no turn) at most: nothing moves, and no notch turns.
        check("pad: lifted, the last place it says moves and turns nothing (a stop at most, which turns nothing)", events <= 1 && sum == 0 && compositor.pointer_x == 100 &&
                                                                             compositor.pointer_y == 40);
        // The stop is the turn's: fingers that turned the wheel end it with one, and a finger that only moved the pointer ends
        // nothing, so its lift is no wheel event at all (a stop goes to the window under the pointer, an open menu among them).
        {
            unsigned int stops = 0;

            for (unsigned int at = table->event_tail[1]; at != table->event_head[1]; at++)
                stops += table->event_type[1][at % WINDOWS_EVENTS] == WINDOW_EVENT_WHEEL && (table->event_flags[1][at % WINDOWS_EVENTS] & WINDOW_WHEEL_STOP);
            check("pad: the lift of two fingers that turned the wheel is one stop", events == 1 && stops == 1);
        }
        table->event_tail[1] = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_ABS, 0, 4000);
        wheel_feed(COMPOSITOR_EV_ABS, 1, 2000);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOUCH, 1);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOOL_FINGER, 1);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        wheel_feed(COMPOSITOR_EV_ABS, 0, 4040);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOUCH, 0);
        wheel_feed(COMPOSITOR_EV_KEY, COMPOSITOR_BTN_TOOL_FINGER, 0);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        sum = wheel_unread(1, &events, &last);
        check("pad: a finger that moved the pointer and turned no wheel lifts with no wheel event, not a stop",
              events == 0 && sum == 0 && compositor.pointer_x > 100 && compositor.pointer_y == 40);
        compositor.pointer_x = compositor.kept_x = 100;
        table->event_tail[1] = table->event_head[1];
    }
    compositor.touchpad[0] = 0;

    /*
            A motion and a wheel in one read: the motion takes the pointer past the screen's edge and nothing keeps
            it on the screen until the read is over. The wheel goes to the window at the edge, where the pointer is
            drawn, and not to no window at all.
    */
    {
        int was_x = compositor.x[1];

        compositor.x[1] = GLASS_SIDE - 120;
        table->event_tail[1] = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_X, 4000);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        wheel_notch(1);
        compositor_input_read(0);
        sum = wheel_unread(1, &events, &last);
        check("wheel: a wheel read with a motion past the screen's edge goes to the window at the edge",
              sum == 120 && compositor.pointer_x == GLASS_SIDE - 1);
        compositor.x[1] = was_x;
        compositor.pointer_x = 100;
        compositor.pointer_y = 40;
        table->event_tail[1] = table->event_head[1];
    }

    /*
            A device's value is any 32 bits its maker chose, and a USB device is anyone's: a notch of 2^31 - 1 was
            multiplied by 120 in an int, which wrapped to a notch the other way; two counts of it in a report were
            added into an int that wrapped back left; and twenty reports of it at the fastest sensitivity (500
            percent) in one read moved the place past 2^31 before the read was over and the pointer kept on a
            screen. Each sum is held to WINDOW_COORD_MAX, the way the device said.
    */
    {
        int was_flat = compositor.flat, was_sensitivity = compositor.sensitivity;

        compositor.hires[0] = 0;
        table->event_tail[1] = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL, 0x7fffffff);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        sum = wheel_unread(1, &events, &last);
        check("device: a notch of 2^31 - 1 turns the window the way the device said, as far as a turn is held to",
              events == 1 && last == WINDOW_COORD_MAX);
        table->event_tail[1] = table->event_head[1];
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_X, 0x7fffffff);
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_X, 0x7fffffff);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        check("device: two counts of 2^31 - 1 to the right in one report put the pointer at the right edge, not back to the left",
              compositor.pointer_x == GLASS_SIDE - 1 && compositor.pointer_y == 40);
        compositor.pointer_x = 100;
        compositor.flat = 0;
        compositor.sensitivity = 500;
        for (int report = 0; report < 20; report++)
        {
            wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_X, 0x7fffffff);
            wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        }
        compositor_input_read(0);
        check("device: twenty reports of 2^31 - 1 at 500 percent in one read put the pointer at the right edge too",
              compositor.pointer_x == GLASS_SIDE - 1 && compositor.pointer_y == 40);
        compositor.flat = was_flat;
        compositor.sensitivity = was_sensitivity;
        compositor.pointer_x = 100;
        compositor.pointer_y = 40;
        compositor.cursor.magnify = compositor.cursor.magnified = 0;
        table->event_tail[1] = table->event_head[1];
    }

    /*
            A button held over a window whose program goes is let go nowhere: the row is the next program's, which
            never had the press, and a release with no press is a click it did not get (a terminal reports it).
    */
    {
        unsigned int head;

        compositor.grab = COMPOSITOR_NONE;
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_leave(1);
        glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
        compositor_present();
        table->event_tail[1] = head = table->event_head[1];
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
        check("release: a button held over a window that went is not let go in the next window in its row",
              table->event_head[1] == head && !compositor.button_down);
    }
    /*
            A turn's devices are read and what they said is forwarded in the same call, before the turn does anything else: the
            row given an event has its wake's byte when compositor_inputs_turn returns, where it had it after the frame.
    */
    {
        struct compositor_poll turn[1] = {{wheel_pipe[0], COMPOSITOR_POLLIN, COMPOSITOR_POLLIN}};
        int knock[2] = {-1, -1}, was_wake = compositor.wake[1];
        char bytes[8];

        if (system_call_2(syscall(pipe2), (positive)knock, O_NONBLOCK) == 0)
        {
            table->event_tail[1] = table->event_head[1];
            compositor.woken = 0;
            compositor.wake[1] = knock[1];
            compositor.hires[0] = 0;
            wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL, 1);
            wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
            compositor_inputs_turn(turn);
            check("forward: a device's event is in its window's ring and the window is knocked on when the turn's devices are read",
                  table->event_head[1] != table->event_tail[1] && system_read_once(knock[0], bytes, sizeof(bytes)) == 1);
            check("forward: and the row is not owed another knock at the turn's end", !(compositor.woken & 2u));
            turn[0].returned = 0;
            wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL, 1);
            wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
            compositor_inputs_turn(turn);
            check("forward: a device the poll did not name is not read, and nobody is knocked on for it",
                  system_read_once(knock[0], bytes, sizeof(bytes)) < 0);
            compositor_input_read(0);
            compositor.woken = 0;
            table->event_tail[1] = table->event_head[1];
            compositor.wake[1] = was_wake;
            system_close(knock[0]);
            system_close(knock[1]);
        }
        else
            check("forward: the wake's pipe is made", false);
    }

    /*
            What is under a point is what the walk that asked every window of the stack found, for every point of a desktop
            and round it: the list is asked first for the windows whose rectangle, grown by the furthest a grip reaches, holds
            the point (rects_point), and a grip that reached further than that growth would be a window no hand could take.
            Windows over each other, one put away, a menu (no grip), at two scales, the grip at its least and its most, a
            pointer and a finger.
    */
    {
        static const int rects[7][4] = {{20, 20, 90, 70}, {60, 50, 120, 90}, {150, 10, 80, 60}, {5, 150, 60, 80}, {100, 160, 100, 70},
                                        {200, 120, 40, 120}, {110, 100, 30, 30}};
        unsigned int was_stacked = compositor.stacked, differ = 0, points = 0, gripped = 0, none = 0;
        int was_scale = compositor.scale, was_grip = compositor.grip, was_touching = compositor.touching;
        unsigned char was_stack[WINDOWS];

        memory_copy(was_stack, compositor.stack, sizeof(was_stack));
        compositor.stacked = 0;
        for (int row = 2; row < 9; row++)
        {
            compositor.x[row] = rects[row - 2][0], compositor.y[row] = rects[row - 2][1];
            compositor.w[row] = (unsigned int)rects[row - 2][2], compositor.h[row] = (unsigned int)rects[row - 2][3];
            compositor.showing[row] = 1;
            compositor.state[row] = row == 5 ? WINDOW_MINIMIZED : 0;
            compositor.arranged[row] = COMPOSITOR_FLOATING;
            compositor_popup_parent[row] = row == 8 ? 3 : -1;
            compositor.stack[compositor.stacked++] = (unsigned char)row;
        }
        for (int turn = 0; turn < 16; turn++)
        {
            compositor.scale = turn & 1 ? 200 : 100;
            compositor.touching = turn >> 1 & 1;
            compositor.grip = turn >> 2 & 1 ? 16 : 2;
            // The stack the other way up for half of them, so each window is found over and under the others.
            if (turn == 8)
                for (unsigned int at = 0; at < compositor.stacked / 2; at++)
                {
                    unsigned char other = compositor.stack[compositor.stacked - 1 - at];

                    compositor.stack[compositor.stacked - 1 - at] = compositor.stack[at];
                    compositor.stack[at] = other;
                }
            for (int y = -130 + (turn >> 3 & 1); y < GLASS_SIDE + 130; y += 2)
                for (int x = -130; x < GLASS_SIDE + 130; x++)
                {
                    int tab, found = compositor_under(x, y, &tab), wanted = COMPOSITOR_NONE;

                    if (compositor_band_hit(x, y) == TASKBAR_HIT_NONE)
                        for (unsigned int at = compositor.stacked; at-- && wanted < 0;)
                        {
                            int row = compositor.stack[at];

                            if (compositor_visible(row) &&
                                ((x >= compositor.x[row] && x < compositor.x[row] + (int)compositor.w[row] && y >= compositor.y[row] &&
                                  y < compositor.y[row] + (int)compositor.h[row]) ||
                                 compositor_edge_zone(row, x, y)))
                                wanted = row;
                        }
                    differ += found != wanted;
                    points++;
                    none += wanted < 0;
                    gripped += wanted >= 0 && !(x >= compositor.x[wanted] && x < compositor.x[wanted] + (int)compositor.w[wanted] &&
                                                y >= compositor.y[wanted] && y < compositor.y[wanted] + (int)compositor.h[wanted]);
                }
        }
        check("under: the list asked first finds the window the walk of every window found, at every point of a desktop and round it",
              differ == 0 && points > 2000000);
        check("under: and the points were on windows' grips outside them, and on no window at all", gripped > 20000 && none > 200000);
        for (int row = 2; row < 9; row++)
        {
            compositor.showing[row] = 0, compositor.state[row] = 0, compositor_popup_parent[row] = -1;
            compositor.x[row] = compositor.y[row] = 0, compositor.w[row] = compositor.h[row] = 0;
        }
        memory_copy(compositor.stack, was_stack, sizeof(was_stack));
        compositor.stacked = was_stacked;
        compositor.scale = was_scale, compositor.grip = was_grip, compositor.touching = was_touching;
    }

    /*
            A device that never stops: one turn reads it COMPOSITOR_INPUT_READS times, sixty-four records a read, and leaves the
            rest for the next turn, so the frame and the doors have a turn between; and a read that comes back short is the last.
            A file stands for the device: it is readable for ever, as a device that floods is.
    */
    {
        bipolar file = (bipolar)system_call_2(syscall(memfd_create), (positive) "moonwater-input-flood", 0);
        struct compositor_input_event record = {1, 0, COMPOSITOR_EV_SYN, 0, 0};
        int was_input = compositor.inputs[0];
        b32 written = file >= 0;

        for (int at = 0; written && at < 2000; at++)
            written = system_write_once((int)file, &record, sizeof(record)) == (bipolar)sizeof(record);
        if (written)
        {
            system_call_3(syscall(lseek), (positive)file, 0, 0);
            compositor.inputs[0] = (int)file;
            compositor_input_read(0);
            check("flood: a device with two thousand records waiting is read sixteen times of sixty-four in one turn, and no more",
                  system_call_3(syscall(lseek), (positive)file, 0, 1) == COMPOSITOR_INPUT_READS * 64 * (long)sizeof(record));
            compositor_input_read(0);
            check("flood: the next turn reads the rest, and its short read is its last",
                  system_call_3(syscall(lseek), (positive)file, 0, 1) == 2000 * (long)sizeof(record));
            compositor_input_read(0);
            check("flood: and a device with nothing more is still the compositor's", compositor.inputs[0] == (int)file);
            compositor.inputs[0] = was_input;
        }
        else
            check("flood: the file that stands for the device is made", false);
        if (file >= 0)
            system_close((int)file);
    }
    /*
            Where an event came from (WINDOW_EVENT_VIRTUAL): a device's read posts as its device is (compositor_input_physical, which
            compositor_input_add sets from where the device hangs) and only for the read's span; a post from anywhere else is marked;
            the kernel's repeat of a key is the device's; a pinch never has the bit, which is its end there. The device is the pipe
            that stands for one, found the machine's own or not by hand: no input device is here.
    */
    {
        int focus = compositor.focus;
        unsigned int at, flags[8];

        compositor.focus = 1;
        table->event_tail[1] = table->event_head[1];
        compositor_input_physical[0] = 1;
        wheel_feed(COMPOSITOR_EV_KEY, 30, 1);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        at = (table->event_head[1] - 1) % WINDOWS_EVENTS;
        check("origin: a key read from a device found to be the machine's own is not marked",
              table->event_head[1] != table->event_tail[1] && table->event_type[1][at] == WINDOW_EVENT_KEY &&
                  (table->event_flags[1][at] & WINDOW_KEY_DOWN) && !(table->event_flags[1][at] & WINDOW_EVENT_VIRTUAL));
        wheel_feed(COMPOSITOR_EV_KEY, 30, 2);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        at = (table->event_head[1] - 1) % WINDOWS_EVENTS;
        check("origin: and the kernel's repeat of it, which is the same device's, is not either",
              (table->event_flags[1][at] & WINDOW_KEY_REPEAT) && !(table->event_flags[1][at] & WINDOW_EVENT_VIRTUAL));
        wheel_feed(COMPOSITOR_EV_KEY, 30, 0);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        compositor_post(1, WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 30, 'a');
        at = (table->event_head[1] - 1) % WINDOWS_EVENTS;
        check("origin: the read leaves no origin behind it: a key posted after it, from anywhere else, is marked",
              !compositor_post_physical && (table->event_flags[1][at] & WINDOW_EVENT_VIRTUAL));
        compositor_input_physical[0] = 0;
        wheel_feed(COMPOSITOR_EV_KEY, 31, 1);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        at = (table->event_head[1] - 1) % WINDOWS_EVENTS;
        check("origin: a key read from a device not found to be the machine's own (uinput's, uhid's, one not known) is marked",
              table->event_type[1][at] == WINDOW_EVENT_KEY && (table->event_flags[1][at] & WINDOW_EVENT_VIRTUAL));
        wheel_feed(COMPOSITOR_EV_KEY, 31, 0);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
        compositor_input_read(0);
        compositor_post(1, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, COMPOSITOR_BTN_LEFT, 0);
        flags[0] = table->event_flags[1][(table->event_head[1] - 1) % WINDOWS_EVENTS];
        compositor_post(1, WINDOW_EVENT_BUTTON, 0, COMPOSITOR_BTN_LEFT, 0);
        compositor_post(1, WINDOW_EVENT_MOTION, 0, 5, 5);
        flags[1] = table->event_flags[1][(table->event_head[1] - 1) % WINDOWS_EVENTS];
        compositor_post(1, WINDOW_EVENT_WHEEL, 0, 120, 0);
        flags[2] = table->event_flags[1][(table->event_head[1] - 1) % WINDOWS_EVENTS];
        compositor_post(1, WINDOW_EVENT_PINCH, WINDOW_PINCH_BEGIN, 1024, 0);
        flags[3] = table->event_flags[1][(table->event_head[1] - 1) % WINDOWS_EVENTS];
        check("origin: a button, a motion and a wheel posted from outside a device's read are marked, and a pinch is not given the bit",
              (flags[0] & WINDOW_EVENT_VIRTUAL) && (flags[1] & WINDOW_EVENT_VIRTUAL) && (flags[2] & WINDOW_EVENT_VIRTUAL) &&
                  flags[3] == WINDOW_PINCH_BEGIN);
        compositor_post_physical = 1;
        compositor_post(1, WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 32, 'd');
        flags[4] = table->event_flags[1][(table->event_head[1] - 1) % WINDOWS_EVENTS];
        compositor_post_physical = 0;
        check("origin: one posted while a device of the machine's own is read is not marked", flags[4] == WINDOW_KEY_DOWN);
        compositor.focus = focus;
        compositor.button_down = 0;
        table->event_tail[1] = table->event_head[1];
    }
    system_close(wheel_pipe[0]);
    system_close(wheel_pipe[1]);
    compositor.input_count = 0;
}

/*
        Where an input device hangs, as compositor_input_path_physical reads the link /sys/dev/char/MAJOR:MINOR resolves to: the
        machine's own under a real bus, and not under devices/virtual (uinput's, and uhid's, Bluetooth LE through bluez among them),
        nor a path that is not under devices at all or could not be read. A descriptor that is no input device's is not the machine's.
*/
static void check_compositor_input_origin(void)
{
    static const char *const own[] = {
        "../../devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/0003:046D:C52B.0003/input/input5/event5",
        "../../devices/platform/i8042/serio0/input/input3/event3",
        "../../devices/LNXSYSTM:00/LNXPWRBN:00/input/input2/event2",
        "../../devices/platform/INT33D5:00/input/input12/event12",
        "../../devices/pci0000:00/0000:00:15.1/i2c_designware.1/i2c-1/i2c-ELAN0501:01/0018:04F3:3060.0002/input/input18/event18",
        "../../devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3:1.0/bluetooth/hci0/hci0:256/0005:046D:B342.0004/input/input21/event21",
        "../../devices/pci0000:00/0000:00:06.0/virtio5/input/input8/event8"};
    static const char *const not_own[] = {
        "../../devices/virtual/input/input7/event7",
        "../../devices/virtual/misc/uhid/0005:046D:B01C.0001/input/input20/event20",
        "", "../../class/input/event3", "../../devices/", "devices"};
    b32 all = 1;
    int pipe_ends[2] = {-1, -1};

    for (unsigned int at = 0; at < sizeof(own) / sizeof(own[0]); at++)
        all &= compositor_input_path_physical(own[at]) == 1;
    check("origin: USB, i8042, an ACPI button, a platform's hotkeys, an I2C pad, classic Bluetooth (hidp) and a VM's virtio-input are the machine's own", all);
    all = !compositor_input_path_physical(null);
    for (unsigned int at = 0; at < sizeof(not_own) / sizeof(not_own[0]); at++)
        all &= compositor_input_path_physical(not_own[at]) == 0;
    check("origin: uinput, uhid (Bluetooth LE through bluez with it), and a path not under devices or none are not", all);
    system_call_2(syscall(pipe2), (positive)pipe_ends, O_NONBLOCK);
    check("origin: a descriptor that is no input device's (a pipe, none) is not the machine's",
          !compositor_input_handle_physical(-1) && pipe_ends[0] >= 0 && !compositor_input_handle_physical(pipe_ends[0]));
    if (pipe_ends[0] >= 0)
        system_close(pipe_ends[0]), system_close(pipe_ends[1]);
}

/*
        What of a window's line is lit, by a mouse, with what the compositor had before it lit any (so the rows run
        against it, and fail there): the edge the pointer is over in the hover ink 9a9a9a, an edge held to resize the
        window in the held ink e4e4e4 (the text's, over the a7a7a7 of the dark standard's line with the keys), a corner held along
        both its edges, all of the line of a window taken by its top, and only the line drawn again when it changes.
*/
static void check_compositor_lit(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int inside = 0;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("lit: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->asked[1] = table->asked[2] = table->asked[3] = 1;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();

    compositor.pointer_x = 47, compositor.pointer_y = 70;
    compositor_motion();
    for (unsigned int at = 0; at < compositor.damages; at++)
        inside += compositor.damage[at][0] <= 110 && compositor.damage[at][2] > 110 && compositor.damage[at][1] <= 70 &&
                  compositor.damage[at][3] > 70;
    check("lit: the pointer coming over an edge draws the window's line again, and not the middle of the window",
          compositor.damages && !inside);
    compositor_present();
    check("lit: the edge the pointer is over is lit in the hover ink, and the others are the line with the keys",
          glass_at(49, 70) == 0x9a9a9au && glass_at(100, 29) == 0xa7a7a7u && glass_at(170, 70) == 0xa7a7a7u);
    compositor.pointer_x = 100;
    compositor_motion();
    compositor_present();
    check("lit: off the edge it is the line again", glass_at(49, 70) == 0xa7a7a7u);

    compositor.pointer_x = 47;
    compositor_motion();
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_present();
    check("lit: pressed, the edge the hand holds is lit in the held ink, well over the line with the keys",
          glass_at(49, 70) == 0xe4e4e4u && glass_at(100, 29) == 0xa7a7a7u && glass_at(100, 110) == 0xa7a7a7u);
    compositor.pointer_x = 37;
    compositor_motion();
    compositor_present();
    check("lit: and it stays lit as the edge moves with the hand", compositor.x[1] == 40 && glass_at(39, 70) == 0xe4e4e4u);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor_present();
    check("lit: let go, the line is the line with the keys", glass_at(39, 70) == 0xa7a7a7u);

    compositor.pointer_x = 38, compositor.pointer_y = 28;
    compositor_motion();
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_present();
    check("lit: a corner held is lit along its two edges thirty-two pixels from its square, and no further",
          glass_at(39, 45) == 0xe4e4e4u && glass_at(60, 29) == 0xe4e4e4u && glass_at(39, 90) == 0xa7a7a7u &&
              glass_at(120, 29) == 0xa7a7a7u);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);

    compositor.pointer_x = 100, compositor.pointer_y = 35;
    compositor_motion();
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_present();
    check("lit: a window taken by its top has all of its line lit while it is held",
          glass_at(39, 70) == 0xe4e4e4u && glass_at(100, 29) == 0xe4e4e4u && glass_at(170, 70) == 0xe4e4e4u &&
              glass_at(100, 110) == 0xe4e4e4u);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor_present();
    check("lit: and none once it is let go", glass_at(100, 29) == 0xa7a7a7u && glass_at(39, 70) == 0xa7a7a7u);
}

// The pixels of the hover ink on the left edge's row (y 70) of the window at 50, 30: the band is the line's one at width 1, and
// four pixels inward from the outline at 4.
static unsigned int border_lit_pixels(void)
{
    unsigned int count = 0;

    for (int x = 40; x < 60; x++)
        count += glass_at(x, 70) == 0x9a9a9au;
    return count;
}

/*
        The width of the lit edge (the setting `canvas border width`, 1 to 4 pixels, 2 by default): at 1 the lit edge is the
        line's pixel as it always was, at 4 it is four pixels inward from the window's outline, and nothing outside the
        window is drawn; a value outside 1 to 4 is refused, and the compositor keeps the standard for it.
*/
static void check_compositor_border(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int outside, one, four;
    long value = 0;

    memory_zero(&compositor, sizeof(compositor));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = compositor.lit_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("border: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->asked[1] = 1;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    compositor.pointer_x = 47, compositor.pointer_y = 70;
    compositor_motion();
    compositor_present();

    compositor_border_publish(1);
    compositor_present();
    one = border_lit_pixels();
    outside = glass_at(48, 70);
    check("border: a width of 1 lights the line's one pixel, and the contents beside it are not lit",
          one == 1 && glass_at(49, 70) == 0x9a9a9au && glass_at(50, 70) == 0x404040u);

    compositor_border_publish(4);
    compositor_present();
    four = border_lit_pixels();
    check("border: a width of 4 lights four pixels inward from the outline, and the outline stays and nothing outside it is drawn",
          four == 4 && glass_at(49, 70) == 0x9a9a9au && glass_at(52, 70) == 0x9a9a9au && glass_at(53, 70) == 0x404040u &&
              glass_at(48, 70) == outside);

    compositor.pointer_x = 100;
    compositor_motion();
    compositor_present();
    check("border: off the edge nothing is lit at any width", border_lit_pixels() == 0);

    check("border: 1 to 4 is kept, and the compositor reads a value outside it as the standard of 2",
          compositor_setting_kept(SETTING_BORDER_WIDTH_PATH, "1", &value) && value == 1 &&
              compositor_setting_kept(SETTING_BORDER_WIDTH_PATH, "4", &value) && value == 4 &&
              !compositor_setting_kept(SETTING_BORDER_WIDTH_PATH, "0", &value) && value == 2 &&
              !compositor_setting_kept(SETTING_BORDER_WIDTH_PATH, "5", &value) && value == 2 &&
              !compositor_setting_kept(SETTING_BORDER_WIDTH_PATH, "default", &value) && value == 2);
}

