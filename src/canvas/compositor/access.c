/*
        Access: the desktop reached with no mouse to hand, the same way whatever a person holds. It is one engine, and a game
        controller is its first device: a pad on xpad (an Xbox pad, an Elite, the Xbox Adaptive Controller and what plugs into it),
        hid-playstation, hid-nintendo, hid-steam (a Steam Deck in its gamepad mode) or a joystick on hid-generic (an assistive one
        among them). Switches, the keys as a pointer and dwell are its next devices, on the same three parts:

        - the pointer engine: a direction with a size (a stick, later the keys and a switch's scan) moves the pointer at a speed
          on one curve, slow and exact near the middle, a screen's height and more a second at the rim;
        - the actions: a device's buttons are named by where they sit (the bottom, right, left and top face buttons, the
          shoulders, the triggers, the middle three), and each name is an action: the pointer's buttons, back, home, the
          overview, the arrows, the previous and next tab, and finding the pointer;
        - the owner: a pad is the focused program's when that program reads pads itself (a game in game mode, a Steam game,
          or one that holds a pad's device open), and the desktop's otherwise. While it is a program's, only a long press of
          the home button is the desktop's: it opens the launcher, and with the launcher focused the pad is the desktop's again.

        The pointer the pad moves is the dynamic one (compositor_pointer_dynamic), as a touch's always is. The kernel's evdev
        hands every reader every event, so a program that reads the same pad loses nothing to the compositor's read; it is
        never grabbed. A pad made by a program (uinput's, under devices/virtual/input: Steam's virtual pads) is that program's
        output for a game, and is never the desktop's.
*/

#define COMPOSITOR_ACCESS_PADS 8
#define COMPOSITOR_ACCESS_LINKED_PHYS "waterlink/" // the phys of a pad Waterlink made of another machine's (uinput's UI_SET_PHYS), then the machine's name
#define COMPOSITOR_ACCESS_DEAD 3932             // a stick's middle that moves nothing: 12% of its reach, in 32767ths
#define COMPOSITOR_ACCESS_FULL 31128            // and its rim, where the speed is all of it: 95%
#define COMPOSITOR_ACCESS_SPEED 17              // the pointer at the rim: 1.7 heights of its screen a second (in tenths)
#define COMPOSITOR_ACCESS_SCROLL 2880           // the scroll at the rim: 24 notches a second, in 120ths
#define COMPOSITOR_ACCESS_TRIGGER_ON 13107      // a trigger is pressed from 40% of its travel and let go under 25%
#define COMPOSITOR_ACCESS_TRIGGER_OFF 8192
#define COMPOSITOR_ACCESS_DELAY_NS 400000000ull // an arrow held repeats after 400 ms, 20 a second
#define COMPOSITOR_ACCESS_REPEAT_NS 50000000ull
#define COMPOSITOR_ACCESS_LONG_NS 700000000ull  // the home button held this long is the desktop's even while a game holds the pad
#define COMPOSITOR_ACCESS_TICK_MOST 50000000ull // a tick later than this moves as far as this one would (a stalled loop never leaps)
#define COMPOSITOR_ACCESS_ASK_NS 2000000000ull  // the focused program's pads are looked at again after this, while a pad is in use

// A pad's axes, by what they are: the two sticks, the two triggers and the cross when it is a hat (-1, 0 or 1).
enum
{
        ACCESS_LX, ACCESS_LY, ACCESS_RX, ACCESS_RY, ACCESS_LT, ACCESS_RT, ACCESS_HX, ACCESS_HY, ACCESS_AXES
};

// Its buttons, by where they sit (Documentation/input/gamepad.rst's names), as bits.
enum
{
        ACCESS_SOUTH, ACCESS_EAST, ACCESS_NORTH, ACCESS_WEST, ACCESS_L1, ACCESS_R1, ACCESS_L2, ACCESS_R2,
        ACCESS_SELECT, ACCESS_START, ACCESS_HOME, ACCESS_L3, ACCESS_R3, ACCESS_UP, ACCESS_DOWN, ACCESS_LEFT, ACCESS_RIGHT,
        ACCESS_BUTTONS
};

// What a button does.
enum
{
        ACCESS_NONE, ACCESS_PRIMARY, ACCESS_SECONDARY, ACCESS_MIDDLE, ACCESS_BACK, ACCESS_GO_HOME, ACCESS_OVERVIEW,
        ACCESS_PREVIOUS, ACCESS_NEXT, ACCESS_KEY_UP, ACCESS_KEY_DOWN, ACCESS_KEY_LEFT, ACCESS_KEY_RIGHT, ACCESS_FIND,
        ACCESS_KEYBOARD, ACCESS_BACKSPACE, ACCESS_ENTER, ACCESS_MENU, ACCESS_ACTIONS
};

/*
        A controller's buttons as a person reaches for them, on any pad: the bottom face button (A on an Xbox pad, the cross on
        a PlayStation's) and the right trigger are the pointer's left button, the left face button and the left trigger its right
        one, and the right face button is back (Escape). The menu button and the home button open the launcher, the view button
        the Scenes overview. The shoulders are the previous and next tab (Ctrl+Shift+Tab and Ctrl+Tab), the cross is the arrows,
        the left stick's press is the middle button and the right stick's finds the pointer. The left stick moves the pointer and
        the right one scrolls. The top face button asks for the keyboard on the screen, or puts it away.

        While that keyboard is up, the left face button is Backspace and the menu button Enter (compositor_access_typing), so a
        word is typed with the stick and the bottom button and mended and sent without reaching for a key.
*/
static const unsigned char compositor_access_actions[ACCESS_BUTTONS] = {
        [ACCESS_SOUTH] = ACCESS_PRIMARY, [ACCESS_EAST] = ACCESS_BACK, [ACCESS_WEST] = ACCESS_SECONDARY, [ACCESS_NORTH] = ACCESS_KEYBOARD,
        [ACCESS_L1] = ACCESS_PREVIOUS, [ACCESS_R1] = ACCESS_NEXT, [ACCESS_L2] = ACCESS_SECONDARY, [ACCESS_R2] = ACCESS_PRIMARY,
        [ACCESS_SELECT] = ACCESS_OVERVIEW, [ACCESS_START] = ACCESS_GO_HOME, [ACCESS_HOME] = ACCESS_GO_HOME,
        [ACCESS_L3] = ACCESS_MIDDLE, [ACCESS_R3] = ACCESS_FIND,
        [ACCESS_UP] = ACCESS_KEY_UP, [ACCESS_DOWN] = ACCESS_KEY_DOWN, [ACCESS_LEFT] = ACCESS_KEY_LEFT, [ACCESS_RIGHT] = ACCESS_KEY_RIGHT,
};

// The buttons that are something else while the keyboard on the screen is up.
static const unsigned char compositor_access_typing[ACCESS_BUTTONS] = {
        [ACCESS_WEST] = ACCESS_BACKSPACE, [ACCESS_START] = ACCESS_ENTER,
};

// The key an action is, for the ones that are keys (0 for the rest).
static const unsigned char compositor_access_keys[ACCESS_ACTIONS] = {
        [ACCESS_BACK] = CK_KEY_ESC, [ACCESS_KEY_UP] = CK_KEY_UP, [ACCESS_KEY_DOWN] = CK_KEY_DOWN, [ACCESS_KEY_LEFT] = 105,
        [ACCESS_KEY_RIGHT] = 106, [ACCESS_BACKSPACE] = CK_KEY_BACKSPACE, [ACCESS_ENTER] = 28, [ACCESS_MENU] = 127, // KEY_COMPOSE: the menu key
};

struct compositor_access_pad
{
        int at;                         // the input slot it is read from, -1 for a free entry
        unsigned char xbox_letters;     // its driver reports the face buttons by their Xbox letters: BTN_X (0x133) left, BTN_Y (0x134) top
        unsigned char physical;         // a post of its read is the machine's own (compositor_input_physical)
        int low[ACCESS_AXES], high[ACCESS_AXES]; // each axis's range (high <= low for an axis it does not have)
        int value[ACCESS_AXES];         // and where it is
        unsigned int held;              // the buttons down (the triggers' among them), as bits
        unsigned int acted;             // and as they were when they were last acted on
        unsigned char pressed_as[ACCESS_BUTTONS]; // the action each button down was pressed as (its let go is that action's)
        unsigned char map[ACCESS_BUTTONS];        // what each button does on this pad: the defaults, and the person's map (/root/access.map)
        positive home_at;               // when the home button went down, while the pad is a program's
};

static struct
{
        struct compositor_access_pad pad[COMPOSITOR_ACCESS_PADS];
        signed char of[COMPOSITOR_INPUTS]; // an input slot's pad, -1 for none
        unsigned char down[ACCESS_ACTIONS]; // how many buttons hold each action down (a pointer button or a key is pressed once)
        positive ticked;                // when the pointer and the scroll last moved
        long long left_x, left_y;       // what of a move is not yet a whole pixel, in 65536ths
        long long wheel_x, wheel_y;     // and of a scroll, in 65536ths of a 120th
        b32 scrolled;                   // a scroll is under way (its stop is owed when the stick is let go)
        int repeat;                     // the arrow held (an action), 0 for none
        positive repeat_at;             // and when it is next repeated
        b32 hand;                       // a pad was the last thing a hand moved (the pointer is the dynamic one)
        b32 keyed;                      // the last thing the pad did was move the focus (its cross, the arrows), not the pointer: its
                                        // bottom button is Enter on the focused control (the kit's), not a click where the pointer is
        b32 yields;                     // the pad is the focused program's
        int asked_row;                  // the row the program question was last asked for, its process, and when
        unsigned int asked_pid;
        positive asked_at;
        b32 reads;                      // and the answer: the program reads pads
        unsigned int hidraw;            // the character major of /dev/hidraw*, 0 when not found
        b32 hidraw_read;
        unsigned int games;             // how many of game[] are kept
        unsigned int game[32];          // the minors of the event devices found to be a game's (major 13), opened here or not
} compositor_access = {.of = {[0 ... COMPOSITOR_INPUTS - 1] = -1}, .asked_row = -1,
                       // Every pad's slot free at the start (at -1): a slot of 0 was taken by the device at input 0, so a pad anywhere else
                       // found no free slot and was never opened (the checks free them by hand, so they never saw it).
                       .pad = {[0 ... COMPOSITOR_ACCESS_PADS - 1] = {.at = -1}}};

static void compositor_access_act(unsigned int action, int down, unsigned int at, positive now);

static b32 compositor_access_drives(void) { return compositor_access.hand; }

static b32 compositor_access_present(void)
{
        for (unsigned int each = 0; each < COMPOSITOR_ACCESS_PADS; each++)
                if (compositor_access.pad[each].at >= 0)
                        return 1;
        return 0;
}

// A controller is the hand now: the keyboard on the screen is looked at again (it comes up on a field for a controller, as for a touch).
static void compositor_access_take_hand(void)
{
        if (!compositor_access.hand)
                compositor_access.hand = 1, compositor.osk_typed = 0, compositor_osk_sync(), compositor_access_seen(WINDOWS_MODALITY_KEYS);
}

// An event device's minor kept as a game's (game) or dropped (a device that is not one took the minor), for compositor_access_holds_pad.
static void compositor_access_note(unsigned int minor, b32 game)
{
        unsigned int at = 0;

        while (at < compositor_access.games && compositor_access.game[at] != minor)
                at++;
        if (game && at == compositor_access.games && at < sizeof(compositor_access.game) / sizeof(compositor_access.game[0]))
                compositor_access.game[compositor_access.games++] = minor;
        else if (!game && at < compositor_access.games)
                compositor_access.game[at] = compositor_access.game[--compositor_access.games];
}

// A mouse, a touchpad, a pen or a touch moved the pointer: it is no longer the pad's.
static void compositor_access_let_go(void)
{
        compositor_access.keyed = 0;
        if (compositor_access.hand)
                compositor_access.hand = 0, compositor_osk_sync();
}

static struct compositor_access_pad *compositor_access_pad_of(unsigned int at)
{
        int index = at < COMPOSITOR_INPUTS ? compositor_access.of[at] : -1;

        return index >= 0 ? &compositor_access.pad[index] : null;
}

// The square root of v, rounded down.
static unsigned int compositor_access_root(unsigned long long v)
{
        unsigned long long root = 0, bit = 1ull << 62;

        while (bit > v)
                bit >>= 2;
        for (; bit; bit >>= 2)
                if (v >= root + bit)
                        v -= root + bit, root = (root >> 1) + bit;
                else
                        root >>= 1;
        return (unsigned int)root;
}

// An axis where it is, from -32767 to 32767 across its range (a trigger's from 0 to 32767); 0 for one it does not have.
static int compositor_access_axis(const struct compositor_access_pad *pad, unsigned int axis)
{
        long long low = pad->low[axis], high = pad->high[axis], middle, half, at;

        if (high <= low)
                return 0;
        if (axis == ACCESS_LT || axis == ACCESS_RT)
                return (int)compositor_clamp64(((long long)pad->value[axis] - low) * 32767 / (high - low), 0, 32767);
        middle = (low + high) / 2, half = (high - low) / 2;
        at = ((long long)pad->value[axis] - middle) * 32767 / (half ? half : 1);
        return (int)compositor_clamp64(at, -32767, 32767);
}

/*
        A stick's share of its speed, in 65536ths: nothing in its middle (COMPOSITOR_ACCESS_DEAD, round, so a stick's rest is
        never a drift), all of it from COMPOSITOR_ACCESS_FULL out, and between them three fifths of the square and two of the
        cube of how far it is across, so a small push is a slow, exact move and the rim a fast one. *r is how far the stick is.
*/
static long long compositor_access_share(int x, int y, unsigned int *r)
{
        long long across;

        *r = compositor_access_root((unsigned long long)((long long)x * x + (long long)y * y));
        if (*r <= COMPOSITOR_ACCESS_DEAD)
                return 0;
        across = ((long long)*r - COMPOSITOR_ACCESS_DEAD) * 65536 / (COMPOSITOR_ACCESS_FULL - COMPOSITOR_ACCESS_DEAD);
        across = across > 65536 ? 65536 : across;
        return (3 * (across * across >> 16) + 2 * ((across * across >> 16) * across >> 16)) / 5;
}

// A pad's code for an evdev button: where the button sits (an ACCESS_ bit number), -1 for one with no name here.
static int compositor_access_button(const struct compositor_access_pad *pad, unsigned int code)
{
        switch (code)
        {
        case 0x130: return ACCESS_SOUTH;
        case 0x131: return ACCESS_EAST;
        case 0x133: return pad->xbox_letters ? ACCESS_WEST : ACCESS_NORTH;
        case 0x134: return pad->xbox_letters ? ACCESS_NORTH : ACCESS_WEST;
        case 0x136: return ACCESS_L1;
        case 0x137: return ACCESS_R1;
        case 0x138: return ACCESS_L2;
        case 0x139: return ACCESS_R2;
        case 0x13a: return ACCESS_SELECT;
        case 0x13b: return ACCESS_START;
        case 0x13c: return ACCESS_HOME;
        case 0x13d: return ACCESS_L3;
        case 0x13e: return ACCESS_R3;
        case 0x220: return ACCESS_UP;
        case 0x221: return ACCESS_DOWN;
        case 0x222: return ACCESS_LEFT;
        case 0x223: return ACCESS_RIGHT;
        // A joystick's (BTN_TRIGGER, BTN_THUMB, BTN_THUMB2): its trigger is the left button, its thumb buttons the right one and back.
        case 0x120: return ACCESS_SOUTH;
        case 0x121: return ACCESS_WEST;
        case 0x122: return ACCESS_EAST;
        }
        return -1;
}

// An evdev axis's place among a pad's (-1 for one not read). A hat that reaches past 1 is a touchpad (a Steam Deck's), not a cross.
static int compositor_access_axis_of(unsigned int code)
{
        switch (code)
        {
        case 0x00: return ACCESS_LX;
        case 0x01: return ACCESS_LY;
        case 0x03: return ACCESS_RX;
        case 0x04: return ACCESS_RY;
        case 0x02: case 0x15: return ACCESS_LT; // ABS_Z, and ABS_HAT2Y on a Steam Deck
        case 0x05: case 0x14: return ACCESS_RT; // ABS_RZ, and ABS_HAT2X on a Steam Deck
        case 0x10: return ACCESS_HX;
        case 0x11: return ACCESS_HY;
        }
        return -1;
}

/*
        Whether a game device opened at the slot is the desktop's to read as a controller, and its axes and the letters of its
        buttons if it is: one with a bottom face button or a joystick's trigger and both axes of a stick, not an accelerometer,
        and not a program's own (uinput's). link is where /sys/dev/char has it (compositor_input_handle_link).
*/
/*
        The left sticks on the input reader's thread (input.c, compositor_reader_run), at the hardware's own pace: a pad reports
        every millisecond while it moves (the Elite 2's endpoint polls at 1 ms, full speed's least), and each report moves the
        pointer at once by the time since the last move at the stick as it was then, by the kernel's own stamp of the report.
        While the stick is held still it reports nothing, and the reader moves it each millisecond itself. The cursor plane is
        moved from that thread (kms.c's fast path, as a mouse's is), and the loop is told the same pixels by a note in the
        reader's ring (COMPOSITOR_READER_PAD_NOTE) so the pointer it keeps is the plane's. Before this the loop moved the
        pointer by the sticks once a frame, so a push changed it a refresh late at most (6.9 ms at 144 Hz), and a loop busy
        with a present held it longer.

        The reader's lanes are its own: no lock. What it reads of the loop's is a range per slot, written at the pad's open
        before its shape says it is a pad (the release the shape's generation is published with orders it), and the word
        free, which the loop writes each turn: the desktop may move the pointer by a stick (no program reads the pads, no
        program holds the pointer locked).
*/
static void compositor_access_move(int dx, int dy); // below
static void compositor_access_take_hand(void);       // below
#define COMPOSITOR_ACCESS_LANE_NS 1000000ull // a stick held still moves the pointer each millisecond, as a report would
struct compositor_access_lane
{
        int x, y;            // the stick at its last report, -32767 to 32767 from its middle
        positive stamp;      // when the pointer was last moved by it (ns); 0 at rest or while the desktop may not
        long long fx, fy;    // what of a pixel is owed, in 65536ths
        unsigned int held;   // the stick is away from its middle
        unsigned int generation; // the slot's generation its reports came with (a slot taken by another device is another)
        unsigned int code;   // the stick's first axis code: ABS_X (0), or ABS_RX (3) on a pad with only a right stick (a right Joy-Con)
};
static struct
{
        int low[COMPOSITOR_INPUTS][2], high[COMPOSITOR_INPUTS][2]; // the pointer stick's range of each pad's slot (the loop's, at open)
        unsigned int code[COMPOSITOR_INPUTS];                      // and its first axis code: ABS_X, or ABS_RX on a pad with no left stick
        __attribute__((aligned(64))) unsigned int free;            // the loop's word: 1 while the desktop may move the pointer by a stick
        __attribute__((aligned(64))) struct compositor_access_lane lane[COMPOSITOR_INPUTS]; // the reader's own
        unsigned int active;                                       // the reader's: how many lanes have a stick away from its middle
        // The reader's count of a report's moves of the plane, and how long after the device's stamp it moved: the stats line's.
        unsigned long long moved, late, late_most;
} compositor_access_reader;

// A pad opened: its left stick's range where the reader reads it, before the slot's shape says it is a pad.
static void compositor_access_lane_open(unsigned int at, const struct compositor_access_pad *pad)
{
        unsigned int right = pad->high[ACCESS_LX] <= pad->low[ACCESS_LX], x = right ? ACCESS_RX : ACCESS_LX, y = right ? ACCESS_RY : ACCESS_LY;

        compositor_access_reader.low[at][0] = pad->low[x], compositor_access_reader.high[at][0] = pad->high[x];
        compositor_access_reader.low[at][1] = pad->low[y], compositor_access_reader.high[at][1] = pad->high[y];
        __atomic_store_n(&compositor_access_reader.code[at], right ? 3u : 0u, __ATOMIC_RELEASE);
        // The shape that says so is published after this (session.c's add calls compositor_reader_changed once the slot is whole).
}

// Whether slot at is a pad with a left stick (its shape's pad bit, input.c's compositor_shape_of).
static unsigned int compositor_access_stick_of(unsigned int at)
{
        return at < COMPOSITOR_INPUTS && compositor_access.of[at] >= 0 && compositor_access_reader.high[at][0] > compositor_access_reader.low[at][0];
}

// A stick's value from its middle, -32767 to 32767, by the range the loop published (compositor_access_axis's sum).
HOT static int compositor_access_lane_axis(unsigned int at, unsigned int which, int value)
{
        long long low = compositor_access_reader.low[at][which], high = compositor_access_reader.high[at][which], middle = (low + high) / 2,
                  half = (high - low) / 2, from = ((long long)value - middle) * 32767 / (half ? half : 1);

        return high <= low ? 0 : (int)(from < -32767 ? -32767 : from > 32767 ? 32767 : from);
}

// A lane moved on to now: the pointer by the stick as it was since its last move, on the plane at once and to the loop by a note.
HOT static int compositor_access_lane_run(unsigned int at, unsigned int generation, struct compositor_access_lane *lane, positive now)
{
        positive since;
        long long share, step;
        unsigned int r;
        int dx, dy;

        // A report stamped before the reader's own last move (the turn ran between the hardware's stamp and the read) moves
        // nothing: the time up to the later of the two is already moved, and the lane's clock never goes back.
        if (!lane->stamp || now <= lane->stamp)
                return 0;
        since = now - lane->stamp, lane->stamp = now;
        since = since > COMPOSITOR_ACCESS_TICK_MOST ? COMPOSITOR_ACCESS_TICK_MOST : since;
        if (!since || !(share = compositor_access_share(lane->x, lane->y, &r)))
                return 0;
        // The move in 65536ths of a pixel: the speed at the rim (a share of the screen's height) times the stick's share, over the time.
        step = (long long)compositor_cursor_async_height() * COMPOSITOR_ACCESS_SPEED / 10 * share * (long long)since / 1000000000ll;
        lane->fx += step * lane->x / (long long)r, lane->fy += step * lane->y / (long long)r;
        dx = (int)(lane->fx / 65536), dy = (int)(lane->fy / 65536);
        lane->fx -= (long long)dx * 65536, lane->fy -= (long long)dy * 65536;
        if (dx || dy)
        {
                struct compositor_input_event note = {dx, dy, COMPOSITOR_READER_PAD_NOTE, 0, 0};

                compositor_cursor_async_shift(dx, dy, now);
                compositor_reader_put(at, generation, &note);
                compositor_reader_knock();
                return 1;
        }
        return 0;
}

// A lane let go: its stick at rest, nothing owed (a pad gone, its slot another device's, the desktop no longer the pad's).
static void compositor_access_lane_rest(struct compositor_access_lane *lane)
{
        compositor_access_reader.active -= lane->held;
        lane->held = 0, lane->stamp = 0, lane->fx = lane->fy = 0, lane->x = lane->y = 0;
}

// A pad's slot gone on the reader's thread (its read failed): its lane at rest, so a stick held as it went moves nothing after.
static void compositor_access_reader_gone(unsigned int at)
{
        if (at < COMPOSITOR_INPUTS)
                compositor_access_lane_rest(&compositor_access_reader.lane[at]);
}

// A pad's report on the reader's thread: the pointer moved to the report's stamp by the stick as it was, then the stick taken as it is.
HOT static void compositor_access_reader_report(unsigned int at, unsigned int generation, const struct compositor_input_event *raw, unsigned int count)
{
        struct compositor_access_lane *lane = &compositor_access_reader.lane[at];
        positive stamp = (positive)raw[count - 1].seconds * 1000000000ull + (positive)raw[count - 1].microseconds * 1000ull;
        int x, y, changed = 0;
        unsigned int r, code = __atomic_load_n(&compositor_access_reader.code[at], __ATOMIC_ACQUIRE);

        if (lane->generation != generation)
                compositor_access_lane_rest(lane), lane->generation = generation;
        x = lane->x, y = lane->y;
        for (unsigned int k = 0; k < count; k++)
                if (raw[k].type == COMPOSITOR_EV_ABS && (raw[k].code == code || raw[k].code == code + 1))
                {
                        int value = compositor_access_lane_axis(at, raw[k].code - code, raw[k].value);
                        changed |= raw[k].code != code ? value != y : value != x;
                        if (raw[k].code != code) y = value; else x = value;
                }
        if (!changed)
                return;
        if (__atomic_load_n(&compositor_access_reader.free, __ATOMIC_ACQUIRE) && compositor_access_lane_run(at, generation, lane, stamp))
        {
                positive late = clock_monotonic_nanoseconds() - stamp;
                compositor_access_reader.moved++, compositor_access_reader.late += late >= 100000ull;
                if (late > compositor_access_reader.late_most && late < 1000000000ull)
                        compositor_access_reader.late_most = late;
        }
        lane->x = x, lane->y = y;
        if (!compositor_access_share(x, y, &r))
        {
                compositor_access_lane_rest(lane);
                return;
        }
        compositor_access_reader.active += !lane->held, lane->held = 1;
        // A push from rest moves at its first report, by a report's time (a millisecond), not a report later.
        if (!lane->stamp && stamp > COMPOSITOR_ACCESS_LANE_NS && __atomic_load_n(&compositor_access_reader.free, __ATOMIC_ACQUIRE))
                lane->stamp = stamp - COMPOSITOR_ACCESS_LANE_NS, compositor_access_lane_run(at, generation, lane, stamp);
}

// The reader's turn while a stick is held away from its middle: each lane moved on to now. How long it may wait, in ns (0: no stick).
HOT static positive compositor_access_reader_turn(const unsigned int *generations)
{
        positive now;
        unsigned int running = 0;

        if (!compositor_access_reader.active)
                return 0;
        now = clock_monotonic_nanoseconds();
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
        {
                struct compositor_access_lane *lane = &compositor_access_reader.lane[at];

                if (!lane->held)
                        continue;
                // A slot no longer a pad's, or another device's now: its lane let go.
                if (!(__atomic_load_n(&compositor_reader.shape[at], __ATOMIC_ACQUIRE) & COMPOSITOR_SHAPE_PAD) || generations[at] != lane->generation)
                        compositor_access_lane_rest(lane);
                else if (!__atomic_load_n(&compositor_access_reader.free, __ATOMIC_ACQUIRE))
                        lane->stamp = 0, lane->fx = lane->fy = 0;
                else
                {
                        running = 1;
                        if (!lane->stamp)
                                lane->stamp = now;
                        else if (now - lane->stamp >= COMPOSITOR_ACCESS_LANE_NS - 50000ull)
                                compositor_access_lane_run(at, generations[at], lane, now);
                }
        }
        // While a program reads the pads the reader sleeps till a report: a stick held in a game wakes it no more than the game's pad does.
        return running ? COMPOSITOR_ACCESS_LANE_NS : 0;
}

// The note of a lane's move, in the loop: the pointer it keeps moved by the same pixels the plane was (and the hand the pad's).
static void compositor_access_lane_noted(int dx, int dy)
{
        if (compositor_access.yields)
                return;
        compositor_access.keyed = 0;
        compositor_access_take_hand();
        compositor_access_move(dx, dy);
}

/*
        The person's map of a pad's buttons: /root/access.map, root's home with the machine's other settings (the data partition, so it
        stays through a reboot). A line is [VENDOR:PRODUCT] BUTTON ACTION, or VENDOR:PRODUCT on|off: the buttons by where they sit
        (south east west north l1 r1 l2 r2 select start home l3 r3 up down left right) and what one does (none primary secondary
        middle back home overview previous next up down left right find keyboard backspace enter). A line with no device is every
        pad's; one with a device that pad's alone; a later line wins over an earlier. A device off is no pad of the desktop's (a
        game's alone); on is one even where its class says not (a wheel, a flight stick). # starts a comment. The file is read as it
        comes, a line at a time, so it may be as long as it likes; a line longer than COMPOSITOR_ACCESS_MAP_LINE is no line (a person's
        map has no word that long) and is passed over.
*/
#define COMPOSITOR_ACCESS_MAP_PATH "/root/access.map"
#define COMPOSITOR_ACCESS_MAP_LINE 256
static const char *const compositor_access_button_names[ACCESS_BUTTONS] = {
    "south", "east", "north", "west", "l1", "r1", "l2", "r2", "select", "start", "home", "l3", "r3", "up", "down", "left", "right"};
static const char *const compositor_access_action_names[ACCESS_ACTIONS] = {
    "none", "primary", "secondary", "middle", "back", "home", "overview", "previous", "next", "up", "down", "left", "right", "find",
    "keyboard", "backspace", "enter", "menu"};

// The next word of a line (bytes up to a blank), its length; *at moves past it and the blanks after.
static unsigned int compositor_access_word(const char *line, unsigned int length, unsigned int *at, const char **word)
{
        unsigned int from;
        while (*at < length && (line[*at] == ' ' || line[*at] == '\t'))
                (*at)++;
        from = *at, *word = line + from;
        while (*at < length && line[*at] != ' ' && line[*at] != '\t')
                (*at)++;
        return *at - from;
}

static int compositor_access_named(const char *const *names, unsigned int count, const char *word, unsigned int length)
{
        for (unsigned int k = 0; k < count; k++)
                if (string_length((string_address)names[k]) == length && !memory_compare(names[k], word, length))
                        return (int)k;
        return -1;
}

// A VENDOR:PRODUCT word as the two numbers, or 0 when it is none (four hexadecimal digits each).
static b32 compositor_access_ids(const char *word, unsigned int length, unsigned int *vendor, unsigned int *product)
{
        unsigned int value = 0;
        if (length != 9 || word[4] != ':')
                return 0;
        for (unsigned int k = 0; k < 9; k++)
        {
                char c = word[k];
                if (k == 4)
                        continue;
                c = (char)(c | 0x20);
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                        return 0;
                value = value << 4 | (unsigned int)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        return *vendor = value >> 16, *product = value & 0xffffu, 1;
}

// One line of the map for a pad of vendor:product: its buttons changed in map, and *taken on (1) or off (0) where the line says so.
static void compositor_access_map_line(const char *line, unsigned int length, unsigned int vendor, unsigned int product, unsigned char *map, int *taken)
{
        unsigned int at = 0, wanted_vendor, wanted_product, size;
        const char *word;
        int button, action;

        size = compositor_access_word(line, length, &at, &word);
        if (!size || word[0] == '#')
                return;
        if (compositor_access_ids(word, size, &wanted_vendor, &wanted_product))
        {
                if (wanted_vendor != vendor || wanted_product != product)
                        return;
                if (!(size = compositor_access_word(line, length, &at, &word)))
                        return;
                if ((size == 2 && !memory_compare(word, "on", 2)) || (size == 3 && !memory_compare(word, "off", 3)))
                        return (void)(*taken = size == 2);
        }
        if ((button = compositor_access_named(compositor_access_button_names, ACCESS_BUTTONS, word, size)) < 0 ||
            !(size = compositor_access_word(line, length, &at, &word)) ||
            (action = compositor_access_named(compositor_access_action_names, ACCESS_ACTIONS, word, size)) < 0)
                return;
        map[button] = (unsigned char)action;
}

// A pad's map read: the defaults, then the person's file line by line. The answer: 1 the device is taken on, 0 off, -1 the file says nothing.
static int compositor_access_map_read(unsigned int vendor, unsigned int product, unsigned char *map)
{
        char chunk[4096], line[COMPOSITOR_ACCESS_MAP_LINE];
        unsigned int used = 0, skipping = 0;
        int taken = -1;
        long handle, got;

        memory_copy(map, compositor_access_actions, ACCESS_BUTTONS);
        if ((handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_ACCESS_MAP_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)) < 0)
                return taken;
        while ((got = (long)system_read_once((int)handle, chunk, sizeof(chunk))) > 0)
                for (long k = 0; k < got; k++)
                {
                        if (chunk[k] == '\n')
                        {
                                if (!skipping)
                                        compositor_access_map_line(line, used, vendor, product, map, &taken);
                                used = 0, skipping = 0;
                        }
                        else if (used < sizeof(line))
                                line[used++] = chunk[k];
                        else
                                skipping = 1;
                }
        if (used && !skipping)
                compositor_access_map_line(line, used, vendor, product, map, &taken);
        system_close((int)handle);
        return taken;
}

// A sysfs number of the input device (its id/vendor, id/product), hexadecimal, or 0.
static unsigned int compositor_access_sys_hex(unsigned int number, const char *leaf)
{
        char path[80], text[16];
        long got;
        unsigned int value = 0;

        windows_num_path(path, sizeof(path), "/sys/class/input/event", number, leaf);
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 *)text, sizeof(text) - 1);
        for (long k = 0; k < got; k++)
        {
                char c = (char)(text[k] | 0x20);
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
                        value = value << 4 | (unsigned int)(c <= '9' ? c - '0' : c - 'a' + 10);
                else
                        break;
        }
        return value;
}

/*
        The classes a controller is of, which say whether it is the desktop's: a pad (a stick, or a cross, and a bottom face button
        or a joystick's trigger) is; a wheel or a pedal (force feedback beyond rumble: a constant force, a spring, a damper, which a
        pad never asks for) and a flight stick (a throttle or a rudder and no face buttons) are a game's alone, as a wheel at rest or
        a throttle left half way would drift the pointer. The person's map turns either way (VENDOR:PRODUCT on|off).
*/
static b32 compositor_access_games_only(bipolar handle, const p8 *keys, unsigned long absolute)
{
        unsigned char ff[16];
        b32 face = (keys[0x130 / 8] >> (0x130 % 8) & 1) != 0;

        memory_zero(ff, sizeof(ff));
        system_control(handle, COMPOSITOR_EVIOCGBIT(0x15, sizeof(ff)), ff);
        // FF_CONSTANT 0x52, FF_SPRING 0x53, FF_FRICTION 0x54, FF_DAMPER 0x55, FF_INERTIA 0x56, FF_RAMP 0x57.
        for (unsigned int code = 0x52; code <= 0x57; code++)
                if (ff[code / 8] >> (code % 8) & 1)
                        return 1;
        // ABS_THROTTLE 6, ABS_RUDDER 7, ABS_WHEEL 8.
        return !face && (absolute >> 6 & 7) != 0;
}

static b32 compositor_access_open(unsigned int at, bipolar handle, const p8 *keys, unsigned long absolute, unsigned int properties,
                                  const char *link, unsigned int number)
{
        static const char *const xbox[] = {"/xpad", "/hid-steam", "/microsoft"};
        struct compositor_access_pad *pad = null;
        char path[64], driver[160];
        long got;
        int index = -1;

        // A pad: a left stick, a right one alone (a right Joy-Con), or a cross on a hat alone (a pad of buttons and a cross).
        if ((properties & 0x40) || ((absolute & 3) != 3 && (absolute >> 3 & 3) != 3 && (absolute >> 0x10 & 3) != 3) || at >= COMPOSITOR_INPUTS ||
            !((keys[0x130 / 8] >> (0x130 % 8) & 1) || (keys[0x120 / 8] >> (0x120 % 8) & 1)) || !link)
                return 0;
        // A virtual pad is no pad of the desktop's (Steam's, a game's own: they are made from a pad the desktop already reads), but for one
        // Waterlink made of another machine's pad: its phys says so (COMPOSITOR_ACCESS_LINKED_PHYS), and it is a pad here as it was there.
        if (string_search((string_address)link, (string_address) "/devices/virtual/input/"))
        {
                char phys_path[64], phys[64];
                long read;

                windows_num_path(phys_path, sizeof(phys_path), "/sys/class/input/event", number, "/device/phys");
                read = file_slurp_once_at(AT_FDCWD, (string_address)phys_path, (p8 *)phys, sizeof(phys) - 1);
                phys[read > 0 ? read : 0] = 0;
                if (!string_has_prefix((string_address)phys, COMPOSITOR_ACCESS_LINKED_PHYS))
                        return 0;
        }
        {
                // The person's map says first whether the device is the desktop's, then its class does.
                unsigned char map[ACCESS_BUTTONS];
                int taken = compositor_access_map_read(compositor_access_sys_hex(number, "/device/id/vendor"),
                                                       compositor_access_sys_hex(number, "/device/id/product"), map);

                if (taken == 0 || (taken < 0 && compositor_access_games_only(handle, keys, absolute)))
                        return 0;
                for (int each = 0; each < COMPOSITOR_ACCESS_PADS && index < 0; each++)
                        if (compositor_access.pad[each].at < 0 || compositor_access.pad[each].at == (int)at)
                                index = each;
                if (index < 0)
                        return 0;
                pad = &compositor_access.pad[index];
                memory_zero(pad, sizeof *pad);
                memory_copy(pad->map, map, ACCESS_BUTTONS);
        }
        pad->at = (int)at;
        for (unsigned int code = 0; code < 0x16; code++)
        {
                int axis = compositor_access_axis_of(code);
                struct compositor_absinfo info = {0};

                if (axis < 0 || !(absolute >> code & 1) || system_control(handle, COMPOSITOR_EVIOCGABS(code), address_of info) != 0)
                        continue;
                // A hat wider than a cross (the Steam Deck's left pad on ABS_HAT0) is not read as one.
                if ((axis == ACCESS_HX || axis == ACCESS_HY) && (info.minimum < -1 || info.maximum > 1))
                        continue;
                pad->low[axis] = info.minimum, pad->high[axis] = info.maximum, pad->value[axis] = info.value;
        }
        // The driver is the device's parent's: /sys/dev/char/MAJOR:MINOR/device (the input device), then its device's driver.
        windows_num_path(path, sizeof(path), "/sys/class/input/event", number, "/device/device/driver");
        got = compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)driver, sizeof(driver) - 1, 0, 0);
        driver[got > 0 ? got : 0] = 0;
        for (unsigned int each = 0; each < sizeof(xbox) / sizeof(xbox[0]); each++)
        {
                unsigned long size = string_length((string_address)driver), tail = string_length((string_address)xbox[each]);

                pad->xbox_letters |= (unsigned char)(size >= tail && string_equals((string_address)driver + size - tail, (string_address)xbox[each]));
        }
        compositor_access.of[at] = (signed char)index;
        compositor_access_lane_open(at, pad);
        compositor_osk_sync();
        return 1;
}

// What a pad held is let go: a pointer button or a key it pressed is released, so none is left down when it goes or is a program's.
static void compositor_access_release_all(void)
{
        for (unsigned int action = 1; action < ACCESS_ACTIONS; action++)
                if (compositor_access.down[action])
                {
                        compositor_access.down[action] = 1;
                        compositor_access_act(action, 0, 0, clock_monotonic_nanoseconds());
                }
        compositor_access.repeat = 0;
        if (compositor_access.scrolled)
                compositor_access.scrolled = 0, compositor_wheel(0, 0, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP);
        // What each pad has acted on is left as it is: while a program reads the pad it follows the buttons (a button held
        // then is never pressed here when the desktop takes the pad back), and a press in the report that hands it back acts.
}

static void compositor_access_close(unsigned int at)
{
        struct compositor_access_pad *pad = compositor_access_pad_of(at);

        if (!pad)
                return;
        pad->held = 0;
        compositor_access_release_all();
        pad->at = -1, compositor_access.of[at] = -1;
        compositor_access_reader.low[at][0] = compositor_access_reader.high[at][0] = 0;
        compositor_osk_sync();
}

// /dev/hidraw*'s character major, as /proc/devices lists it ("NNN hidraw").
static unsigned int compositor_access_hidraw(void)
{
        char text[2048];
        long got;
        bipolar handle;

        if (compositor_access.hidraw_read)
                return compositor_access.hidraw;
        compositor_access.hidraw_read = 1;
        if ((handle = system_open_at(AT_FDCWD, (string_address) "/proc/devices", FILE_READ | O_CLOEXEC)) < 0)
                return 0;
        got = system_read_once(handle, text, sizeof(text) - 1);
        system_close(handle);
        text[got > 0 ? got : 0] = 0;
        for (long at = 0; at < got; at++)
        {
                unsigned int major = 0;
                long from = at;

                for (; at < got && text[at] == ' '; at++)
                        ;
                for (; at < got && text[at] >= '0' && text[at] <= '9'; at++)
                        major = major * 10 + (unsigned int)(text[at] - '0');
                if (at > from && text[at] == ' ' && at + 8 <= got && !memory_compare(text + at + 1, "hidraw\n", 7))
                        return compositor_access.hidraw = major;
                for (; at < got && text[at] != '\n'; at++)
                        ;
        }
        return 0;
}

/*
        Whether a process holds a pad open: a descriptor of one of its that is an event device found to be a game's, a joystick
        (/dev/input/js*) or a raw HID device (/dev/hidraw*, which is how Steam reads its controllers). Each is looked at by its
        device numbers (statx through /proc/PID/fd), so where the program's own /dev is mounted makes no difference.
*/
static b32 compositor_access_holds_pad(unsigned int pid)
{
        char path[40];
        p8 buffer[2048], statx[256];
        bipolar handle, got;
        unsigned int hidraw = compositor_access_hidraw();
        b32 holds = 0;

        windows_num_path(path, sizeof(path), "/proc/", pid, "/fd");
        if ((handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_DIRECTORY | O_CLOEXEC)) < 0)
                return 0;
        while (!holds && (got = system_call_3(syscall(getdents64), (positive)handle, (positive)buffer, sizeof(buffer))) > 0)
                for (bipolar at = 0; !holds && at < got;)
                {
                        string_address name = (string_address)(buffer + at + 19);
                        unsigned int major, minor;
                        p16 mode;

                        at += memory_load_unaligned(p16, buffer + at + 16);
                        if (*name < '0' || *name > '9' ||
                            compositor_call(syscall(statx), (positive)handle, (positive)name, 0, 3, (positive)statx, 0) != 0)
                                continue;
                        memory_copy(address_of mode, statx + 0x1c, 2);
                        memory_copy(address_of major, statx + 128, 4);
                        memory_copy(address_of minor, statx + 132, 4);
                        if ((mode & 0170000) != 0020000)
                                continue;
                        holds = (major == 13 && minor < 32) || (hidraw && major == hidraw);
                        for (unsigned int each = 0; !holds && major == 13 && each < compositor_access.games; each++)
                                holds = minor == compositor_access.game[each];
                }
        system_close(handle);
        return holds;
}

/*
        Whether the pads are the focused program's now: it is the game the game mode is for (the fullscreen top), a Steam game's
        (its process has SteamGameId), or it holds a pad open itself. The answer is kept for the row and looked at again after
        COMPOSITOR_ACCESS_ASK_NS, since a program opens its pads when it wants them.
*/
static b32 compositor_access_program_reads(positive now)
{
        int row = compositor.focus;
        unsigned int pids[4], count, me, pid;
        char game[24];

        if (row < 0 || row >= COMPOSITOR_ROWS)
                return compositor_access.asked_row = -1, 0;
        if (compositor.game >= 0 && compositor_fullscreen_top() == row)
                return 1;
        pid = __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE);
        // A program found to read pads keeps the answer while it is focused (a game is not looked through again mid-play);
        // one found not to is asked again now and then, as it may open its pads later.
        if (row == compositor_access.asked_row && pid == compositor_access.asked_pid &&
            (compositor_access.reads || now - compositor_access.asked_at < COMPOSITOR_ACCESS_ASK_NS))
                return compositor_access.reads;
        compositor_access.asked_row = row, compositor_access.asked_pid = pid, compositor_access.asked_at = now, compositor_access.reads = 0;
        me = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        count = compositor_row_pids(row, me, 0, pids);
        for (unsigned int each = 0; each < count && !compositor_access.reads; each++)
                compositor_access.reads = compositor_environ_word(pids[each], "SteamGameId=", game, sizeof game) ||
                                          compositor_access_holds_pad(pids[each]);
        return compositor_access.reads;
}

// The pointer moved by dx, dy whole pixels, as a mouse's move is shown (compositor_motion), with no gain of a mouse's put on it.
static void compositor_access_move(int dx, int dy)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];

        if (!dx && !dy)
                return;
        compositor_pointer_sync();
        track->lead_x = track->lead_y = 0;
        compositor.real_x = compositor_counts(compositor.real_x, dx), compositor.real_y = compositor_counts(compositor.real_y, dy);
        compositor.touch.hidden = 0;
        compositor_motion();
}

// The chord Ctrl+Tab, with Shift for the previous tab: pressed and let go at once, as the keys would be.
static void compositor_access_tab(unsigned int at, int previous, positive now)
{
        compositor_key(at, 29, 1, now);
        if (previous)
                compositor_key(at, 42, 1, now);
        compositor_key(at, CK_KEY_TAB, 1, now), compositor_key(at, CK_KEY_TAB, 0, now);
        if (previous)
                compositor_key(at, 42, 0, now);
        compositor_key(at, 29, 0, now);
}

/*
        An action pressed (down) or let go. A pointer button or a key is pressed by the first button that holds its action and
        let go by the last, so the right trigger and the bottom face button held together are one left button. at is the slot
        the keys are said from (their held modifiers are kept there).
*/
static void compositor_access_act(unsigned int action, int down, unsigned int at, positive now)
{
        unsigned char *count = &compositor_access.down[action];

        if (down ? (*count)++ : (!*count || --*count))
                return;
        if (down && compositor.dark)
                compositor_dark_woken();
        switch (action)
        {
        case ACCESS_PRIMARY: compositor_press(COMPOSITOR_BTN_LEFT, down); break;
        case ACCESS_SECONDARY: compositor_press(COMPOSITOR_BTN_RIGHT, down); break;
        case ACCESS_MIDDLE: compositor_press(COMPOSITOR_BTN_LEFT + 2, down); break;
        case ACCESS_BACK: case ACCESS_KEY_UP: case ACCESS_KEY_DOWN: case ACCESS_KEY_LEFT: case ACCESS_KEY_RIGHT:
        case ACCESS_BACKSPACE: case ACCESS_ENTER: case ACCESS_MENU:
                compositor_key(at, compositor_access_keys[action], down, now);
                if (action != ACCESS_BACK && action != ACCESS_ENTER && action != ACCESS_MENU)
                        compositor_access.repeat = down ? (int)action : 0, compositor_access.repeat_at = now + COMPOSITOR_ACCESS_DELAY_NS;
                break;
        case ACCESS_GO_HOME: if (down) compositor_logo_act(); break;
        case ACCESS_OVERVIEW:
                if (down)
                        compositor.scenes.open ? compositor_scenes_close() : compositor_scenes_open(compositor_space_current_screen());
                break;
        case ACCESS_PREVIOUS: case ACCESS_NEXT: if (down) compositor_access_tab(at, action == ACCESS_PREVIOUS, now); break;
        case ACCESS_FIND: if (down) compositor.cursor.magnify = 1, compositor_magnify(now); break;
        case ACCESS_KEYBOARD: if (down) compositor_osk_toggle(); break;
        }
}

// A pad's whole report taken (its SYN_REPORT): the triggers pressed or let go, and each button that changed acted on.
static void compositor_access_report(struct compositor_access_pad *pad, positive now)
{
        unsigned int changed, held = pad->held;
        int lt = compositor_access_axis(pad, ACCESS_LT), rt = compositor_access_axis(pad, ACCESS_RT);
        int hx = compositor_access_axis(pad, ACCESS_HX), hy = compositor_access_axis(pad, ACCESS_HY);
        b32 yields;

        // An analog trigger is a button with a gap between where it is pressed and where it is let go, so a finger resting on it never chatters.
        if (pad->high[ACCESS_LT] > pad->low[ACCESS_LT])
                held = lt >= COMPOSITOR_ACCESS_TRIGGER_ON ? held | 1u << ACCESS_L2 : lt < COMPOSITOR_ACCESS_TRIGGER_OFF ? held & ~(1u << ACCESS_L2) : held;
        if (pad->high[ACCESS_RT] > pad->low[ACCESS_RT])
                held = rt >= COMPOSITOR_ACCESS_TRIGGER_ON ? held | 1u << ACCESS_R2 : rt < COMPOSITOR_ACCESS_TRIGGER_OFF ? held & ~(1u << ACCESS_R2) : held;
        // A cross reported as a hat is the four arrow buttons.
        if (pad->high[ACCESS_HX] > pad->low[ACCESS_HX])
                held = (held & ~(1u << ACCESS_LEFT | 1u << ACCESS_RIGHT)) | (hx < 0 ? 1u << ACCESS_LEFT : 0) | (hx > 0 ? 1u << ACCESS_RIGHT : 0);
        if (pad->high[ACCESS_HY] > pad->low[ACCESS_HY])
                held = (held & ~(1u << ACCESS_UP | 1u << ACCESS_DOWN)) | (hy < 0 ? 1u << ACCESS_UP : 0) | (hy > 0 ? 1u << ACCESS_DOWN : 0);
        pad->held = held;
        yields = compositor_access_program_reads(now);
        if (yields != compositor_access.yields)
                compositor_access_release_all(), compositor_access.yields = yields;
        if ((changed = held ^ pad->acted) == 0)
                return;
        // While a program reads the pad, only a long press of home is the desktop's (compositor_access_tick times it).
        if (yields)
        {
                if (changed >> ACCESS_HOME & 1)
                        pad->home_at = held >> ACCESS_HOME & 1 ? now : 0;
                pad->acted = held;
                return;
        }
        compositor_access_take_hand();
#ifdef MOONWATER_GPU_C
        compositor_gpu_wake();
#endif
        compositor_hold_touch(now);
        for (unsigned int button = 0; button < ACCESS_BUTTONS; button++)
        {
                unsigned int action;

                if (!(changed >> button & 1))
                        continue;
                // A press is the action of the moment (the keyboard's when it is up); its let go is whatever it was pressed as.
                if (held >> button & 1)
                {
                        action = compositor.osk_shown && compositor_access_typing[button] ? compositor_access_typing[button] : pad->map[button];
                        // The cross moved the focus last: the primary button presses the focused control (Enter), as a Steam Deck's A does;
                        // after the stick moved the pointer it clicks where the pointer is.
                        if (action == ACCESS_PRIMARY && compositor_access.keyed)
                                action = ACCESS_ENTER;
                        // And the secondary button opens the focused control's menu (the menu key, which the kit takes as Shift+F10).
                        else if (action == ACCESS_SECONDARY && compositor_access.keyed)
                                action = ACCESS_MENU;
                        if (action >= ACCESS_KEY_UP && action <= ACCESS_KEY_RIGHT)
                                compositor_access.keyed = 1;
                        pad->pressed_as[button] = (unsigned char)action;
                }
                else
                        action = pad->pressed_as[button], pad->pressed_as[button] = 0;
                if (action)
                        compositor_access_act(action, (int)(held >> button & 1), (unsigned int)pad->at, now);
        }
        pad->acted = held;
}

// A pad's events, read from its slot: the axes and buttons kept, and acted on at the end of each report.
static void compositor_access_take(unsigned int at, const struct compositor_input_event *raw, bipolar got)
{
        struct compositor_access_pad *pad = compositor_access_pad_of(at);
        struct compositor_report_event events[64];
        positive count;

        if (!pad)
                return;
        pad->physical = compositor_post_physical != 0;
        count = evdev_decode((const p8 *)raw, (positive)got, (p64 *)events, 64);
        for (positive i = 0; i < count; i++)
        {
                const struct compositor_report_event *event = &events[i];
                int which;

                if (event->type == COMPOSITOR_EV_ABS && (which = compositor_access_axis_of(event->code)) >= 0)
                        pad->value[which] = event->value;
                else if (event->type == COMPOSITOR_EV_KEY && event->value != 2 && (which = compositor_access_button(pad, event->code)) >= 0)
                        pad->held = event->value ? pad->held | 1u << which : pad->held & ~(1u << which);
                else if (event->type == COMPOSITOR_EV_SYN && event->code == 0)
                        compositor_access_report(pad, event->stamp ? event->stamp : clock_monotonic_nanoseconds());
                else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                        compositor.table->input_dropped[at]++;
        }
}

// The sticks of every pad the desktop reads, summed: the left ones' and the right ones' (a second pad helps the first, as two mice do).
static void compositor_access_sticks(int *lx, int *ly, int *rx, int *ry)
{
        long long sum[4] = {0, 0, 0, 0};

        for (unsigned int each = 0; each < COMPOSITOR_ACCESS_PADS; each++)
        {
                const struct compositor_access_pad *pad = &compositor_access.pad[each];

                if (pad->at < 0)
                        continue;
                sum[0] += compositor_access_axis(pad, ACCESS_LX), sum[1] += compositor_access_axis(pad, ACCESS_LY);
                // A pad with a right stick alone (a right Joy-Con) points with it (the reader's lane), so it scrolls nothing.
                if (pad->high[ACCESS_LX] > pad->low[ACCESS_LX])
                        sum[2] += compositor_access_axis(pad, ACCESS_RX), sum[3] += compositor_access_axis(pad, ACCESS_RY);
        }
        *lx = (int)compositor_clamp64(sum[0], -32767, 32767), *ly = (int)compositor_clamp64(sum[1], -32767, 32767);
        *rx = (int)compositor_clamp64(sum[2], -32767, 32767), *ry = (int)compositor_clamp64(sum[3], -32767, 32767);
}

// Whether any pad the desktop reads has a stick away from its middle, or its home button held while a program reads it.
static b32 compositor_access_busy(void)
{
        int lx, ly, rx, ry;
        unsigned int r;

        for (unsigned int each = 0; each < COMPOSITOR_ACCESS_PADS; each++)
                if (compositor_access.pad[each].at >= 0 && compositor_access.pad[each].home_at)
                        return 1;
        // A stick pushed while a program reads the pad is the program's: it owes the loop no tick (no wake a frame during a game).
        if (compositor_access.yields)
                return 0;
        compositor_access_sticks(&lx, &ly, &rx, &ry);
        return (!compositor_reader_running() && compositor_access_share(lx, ly, &r)) || compositor_access_share(rx, ry, &r) || compositor_access.scrolled;
}

// When the loop is next owed a tick of the pads: a frame on while a stick is pushed, or the next repeat of an arrow; 0 for none.
static positive compositor_access_due(void)
{
        positive period = compositor.period ? compositor.period : 1000000000ull / 60, due = 0;

        // A stick held still sends no report, so the loop looks at who reads the pads itself while one is held and the focus moved.
        if (compositor_access_reader.active && compositor.focus != compositor_access.asked_row)
        {
                b32 yields = compositor_access_program_reads(clock_monotonic_nanoseconds());
                if (yields != compositor_access.yields)
                        compositor_access_release_all(), compositor_access.yields = yields;
        }
        __atomic_store_n(&compositor_access_reader.free, (unsigned int)(!compositor_access.yields && !compositor_wl_locked()), __ATOMIC_RELEASE);

        if (compositor_access.repeat && !compositor_access.yields)
                due = compositor_access.repeat_at;
        if (compositor_access_busy())
        {
                positive next = compositor_access.ticked ? compositor_access.ticked + period : 1;
                due = !due || next < due ? next : due;
        }
        return due;
}

// The height of the screen the pointer is on, which a stick's speed is a share of.
static long long compositor_access_height(void)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_screen_at(screens, count, compositor.pointer_x, compositor.pointer_y);

        return screen >= 0 && (unsigned int)screen < count && screens[screen][3] > 0 ? screens[screen][3]
               : compositor.height > 0 ? compositor.height : 1080;
}

/*
        A tick of the pads: the pointer moved by the left sticks and the view scrolled by the right ones for the time since the
        last tick (no more than COMPOSITOR_ACCESS_TICK_MOST of it), an arrow held repeated, and a long press of home while a
        program reads the pad taken as the desktop's.
*/
static void compositor_access_tick(positive now)
{
        positive period = compositor.period ? compositor.period : 1000000000ull / 60;
        // The first tick of a push moves a frame's worth; the ones after it, the time since the last.
        positive since = !compositor_access.ticked ? period : now > compositor_access.ticked ? now - compositor_access.ticked : 0;
        int lx, ly, rx, ry, was = compositor_post_physical, physical = 0, at = -1;
        unsigned int r;
        long long share;
        b32 yields;

        since = since > COMPOSITOR_ACCESS_TICK_MOST ? COMPOSITOR_ACCESS_TICK_MOST : since;
        compositor_access.ticked = now;
        for (unsigned int each = 0; each < COMPOSITOR_ACCESS_PADS; each++)
        {
                struct compositor_access_pad *pad = &compositor_access.pad[each];

                if (pad->at < 0)
                        continue;
                physical |= pad->physical, at = at < 0 ? pad->at : at;
                if (pad->home_at && now - pad->home_at >= COMPOSITOR_ACCESS_LONG_NS)
                {
                        pad->home_at = 0;
                        compositor_post_physical = pad->physical;
                        compositor_logo_act();
                        compositor_access.asked_row = -1;
                        compositor_post_physical = was;
                }
        }
        if (at < 0)
                return;
        if ((yields = compositor_access_program_reads(now)) != compositor_access.yields)
                compositor_access_release_all(), compositor_access.yields = yields;
        if (yields)
                return;
        compositor_post_physical = physical;
        if (compositor_access.repeat && now >= compositor_access.repeat_at)
        {
                compositor_key((unsigned int)at, compositor_access_keys[compositor_access.repeat], 2, now);
                compositor_access.repeat_at = now + COMPOSITOR_ACCESS_REPEAT_NS;
        }
        compositor_access_sticks(&lx, &ly, &rx, &ry);
        if (!compositor_reader_running() && (share = compositor_access_share(lx, ly, &r)) && since && !compositor_wl_locked())
        {
                // The move in 65536ths of a pixel: the speed at the rim (a share of the screen's height) times the stick's share, over the time.
                long long step = compositor_access_height() * COMPOSITOR_ACCESS_SPEED / 10 * share * (long long)since / 1000000000ll;
                int dx, dy;

                compositor_access.left_x += step * lx / (long long)r, compositor_access.left_y += step * ly / (long long)r;
                dx = (int)(compositor_access.left_x / 65536), dy = (int)(compositor_access.left_y / 65536);
                compositor_access.left_x -= (long long)dx * 65536, compositor_access.left_y -= (long long)dy * 65536;
                compositor_access_take_hand();
                compositor_access.keyed = 0;
                compositor_access_move(dx, dy);
        }
        else if (!share)
                compositor_access.left_x = compositor_access.left_y = 0;
        if ((share = compositor_access_share(rx, ry, &r)) && since)
        {
                // A scroll in 65536ths of a 120th: a stick pushed up shows what is above (a wheel turned away), whatever the
                // natural setting does to a wheel, since a stick is pushed the way the view is to go (the turn is undone below).
                long long step = COMPOSITOR_ACCESS_SCROLL * share / 65536 * (long long)since * 65536 / 1000000000ll;
                int up, right;

                compositor_access.wheel_y += step * -ry / (long long)r, compositor_access.wheel_x += step * rx / (long long)r;
                up = (int)(compositor_access.wheel_y / 65536), right = (int)(compositor_access.wheel_x / 65536);
                compositor_access.wheel_y -= (long long)up * 65536, compositor_access.wheel_x -= (long long)right * 65536;
                compositor_access_take_hand();
                if (up)
                        compositor_wheel(compositor_wheel_turn(up), 0, WINDOW_WHEEL_PRECISE), compositor_access.scrolled = 1;
                if (right)
                        compositor_wheel(compositor_wheel_turn(right), 1, WINDOW_WHEEL_PRECISE), compositor_access.scrolled = 1;
        }
        else if (!share)
        {
                compositor_access.wheel_x = compositor_access.wheel_y = 0;
                if (compositor_access.scrolled)
                        compositor_access.scrolled = 0, compositor_wheel(0, 0, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP);
        }
        // Both sticks at rest: the next push starts from a frame's worth again.
        if (!compositor_access_busy())
                compositor_access.ticked = 0;
        compositor_post_physical = was;
}

/*
        The aids of the keys and the pointer: access.c is for every hand, not a controller's alone. Each is off until the person turns
        it on (the Accessibility group of the settings, moonwater access), and each is the one a desktop is known by:

        - sticky keys: Shift, Ctrl, Alt, AltGr or Super pressed and let go alone is latched for the next key; pressed and let go again
          it is locked, and a third time it is let go. A latched or locked modifier is pressed before the next key that is no modifier
          and let go after it, and a latch is spent by that key. What is latched and locked is the table's (access_latched and
          access_locked), for the taskbar or the keyboard on the screen to show. A tap of Super latches it and opens no launcher.
        - slow keys: a key counts only once it is held this long. Its press is given then, and it is held and let go as it is; one let
          go sooner was never pressed.
        - bounce keys: a press of a key this soon after its release is a bounce, and it and its release are dropped.
        - mouse keys: the keypad is the pointer whatever Num Lock says (numbers are the top row's while it is on). 8 2 4 6 and 7 9 1 3
          move it, a pixel at the press and on from a moment later, faster the longer they are held; 5 clicks the chosen button (held
          while 5 is), / * - choose the left, the middle or the right one, 0 holds it down and . lets it go.
        - dwell: the pointer held still, within a few pixels, this long clicks the left button once, and again only once it moved. A
          ring round the pointer fills as it waits (pointer.c's compositor_pointer_ring, drawn with the pointer's own covers, so the
          plane, the processor's draw and the card's all show it).

        While a feature of the keys is on, the reader's thread sends no key to a client itself (compositor_access_gate, the one relaxed
        load a key's report pays), and the loop's key goes through compositor_access_key. Their waits are the loop's deadlines
        (compositor_access_aid_due): nothing sleeps and there is no thread of theirs. With every one off, a key costs the loop one
        branch and the reader one load, and the pointer's paths cost the reader nothing and the loop's motion one branch.

        Which hand acted last is the table's too (input_modality, written when it changes): the keys or a pad, a pointer, or a touch,
        so a window shows its focus ring for the keys alone.
*/
#define ACCESS_STICKY 1u
#define ACCESS_SLOW 2u
#define ACCESS_BOUNCE 4u
#define ACCESS_MOUSE 8u
#define ACCESS_DWELL 16u
#define ACCESS_KEYED (ACCESS_STICKY | ACCESS_SLOW | ACCESS_BOUNCE | ACCESS_MOUSE) // the ones that take every key through the loop
#define COMPOSITOR_ACCESS_MOUSE_DELAY_NS 150000000ull // a keypad arrow moves a pixel at its press, and goes on from this long after it
#define COMPOSITOR_ACCESS_MOUSE_RAMP_MS 1500ll        // and is at its full speed this long after that
#define COMPOSITOR_ACCESS_MOUSE_LEAST 25ll            // its speed as it starts, in thousandths of the screen's height a second
#define COMPOSITOR_ACCESS_MOUSE_MOST 1000ll           // and its full speed: a height a second
#define COMPOSITOR_ACCESS_DWELL_SLOP 4                // how far (pixels at 100%) a pointer may wander and still be still

static struct
{
        unsigned int on;                // the aids on (ACCESS_*)
        positive slow_ns, bounce_ns, dwell_ns;
        unsigned int modality;          // the table's input_modality, as last written
        // Sticky keys, in WINDOW_KEY_* bits: latched, locked, held by hand, pressed and let go with no key between (alone), and
        // pressed here for the keys held now (pressed, from slot pressed_at); riding is how many keys are held with them, riders which.
        unsigned int latched, locked, physical, alone, pressed, pressed_at, riding;
        unsigned char riders[32];
        // Slow and bounce keys, a bit or an entry a code: a press not yet given, whether its device is the machine's own, when it went
        // down and from which slot; a press dropped as a bounce (its let go is dropped with it), and when each key was last let go.
        unsigned char waiting[32], physical_of[32], bounced[32];
        unsigned int waits;
        positive down_at[256], up_at[256];
        unsigned short from[256];
        // Mouse keys: the arrows held (a bit each), when the first went down and when they last moved the pointer, what of a pixel is
        // owed (65536ths), the button chosen, the one 0 holds and the one 5 holds (0 for none), and whether the keys are the machine's.
        unsigned int arrows;
        positive arrows_at, moved_at;
        long long left_x, left_y;
        unsigned int button, holding, clicking;
        b32 arrows_physical;
        // Dwell: where the pointer was still from and since when, whether its click is owed, whether the hand was the machine's, and
        // the ring as the pointer was last drawn with it (32nds of a turn).
        int still_x, still_y;
        positive still_at;
        b32 armed, still_physical;
        int ring;
} compositor_access_aid = {.button = COMPOSITOR_BTN_LEFT};

// The kind of hand that acted last (WINDOWS_MODALITY_*), in the table when it changes.
static void compositor_access_seen(unsigned int modality)
{
        if (likely(compositor_access_aid.modality == modality) || !compositor.table)
                return;
        compositor_access_aid.modality = modality;
        __atomic_store_n(&compositor.table->input_modality, modality, __ATOMIC_RELEASE);
}

// What sticky keys holds, in the table.
static void compositor_access_sticky_publish(void)
{
        if (!compositor.table)
                return;
        __atomic_store_n(&compositor.table->access_latched, compositor_access_aid.latched, __ATOMIC_RELEASE);
        __atomic_store_n(&compositor.table->access_locked, compositor_access_aid.locked, __ATOMIC_RELEASE);
}

// A modifier's kind (WINDOW_KEY_SHIFT, _CONTROL, _ALT, _ALTGR or _SUPER), 0 for a key that is none.
static unsigned int compositor_access_kind(unsigned int code) { return compositor_held_flags(compositor_held_bit(code)); }

// The kinds pressed (down) or let go as the left key of each, the right Alt for AltGr, said from slot at.
static void compositor_access_kinds(unsigned int at, unsigned int kinds, int down, positive now)
{
        static const unsigned short kind[5] = {WINDOW_KEY_SHIFT, WINDOW_KEY_CONTROL, WINDOW_KEY_ALT, WINDOW_KEY_ALTGR, WINDOW_KEY_SUPER};
        static const unsigned char key[5] = {42, 29, 56, 100, 125};

        for (unsigned int k = 0; k < 5; k++)
                if (kinds & kind[k])
                        compositor_key(at, key[k], down, now);
}

// A key past slow and bounce keys while sticky keys is on: a modifier's press and let go counted, and a key that is none pressed with
// what is latched or locked pressed before it and let go after it.
static void compositor_access_sticky(unsigned int at, unsigned int code, int value, positive now)
{
        unsigned int kind = compositor_access_kind(code), byte = code >> 3 & 31;
        unsigned char bit = (unsigned char)(1u << (code & 7));

        if (kind)
        {
                if (value == 1)
                {
                        // One pressed here for a key, pressed by hand now: its press here is let go first, so the hand's is the one held.
                        if (compositor_access_aid.pressed & kind)
                                compositor_access_kinds(compositor_access_aid.pressed_at, kind, 0, now), compositor_access_aid.pressed &= ~kind;
                        compositor_access_aid.physical |= kind, compositor_access_aid.alone = kind;
                }
                else if (!value)
                        compositor_access_aid.physical &= ~kind;
                compositor_key(at, code, value, now);
                if (value == 1 && kind == WINDOW_KEY_SUPER)
                        compositor.logo_alone = 0; // a tap of Super is a latch, not the launcher
                if (!value && (compositor_access_aid.alone & kind))
                {
                        compositor_access_aid.alone &= ~kind;
                        if (compositor_access_aid.locked & kind)
                                compositor_access_aid.locked &= ~kind;
                        else if (compositor_access_aid.latched & kind)
                                compositor_access_aid.latched &= ~kind, compositor_access_aid.locked |= kind;
                        else
                                compositor_access_aid.latched |= kind;
                        compositor_access_sticky_publish();
                }
                return;
        }
        if (value == 1)
        {
                unsigned int want = (compositor_access_aid.latched | compositor_access_aid.locked) & ~compositor_access_aid.physical;
                unsigned int spent = compositor_access_aid.latched;

                compositor_access_aid.alone = 0;
                // What was pressed for a key still held and is not wanted now (a latch that key spent) is let go first: this key is not its.
                if (compositor_access_aid.pressed && compositor_access_aid.pressed_at != at)
                        compositor_access_kinds(compositor_access_aid.pressed_at, compositor_access_aid.pressed, 0, now), compositor_access_aid.pressed = 0;
                if (compositor_access_aid.pressed & ~want)
                        compositor_access_kinds(at, compositor_access_aid.pressed & ~want, 0, now);
                if (want & ~compositor_access_aid.pressed)
                        compositor_access_kinds(at, want & ~compositor_access_aid.pressed, 1, now);
                compositor_access_aid.pressed = want, compositor_access_aid.pressed_at = at, compositor_access_aid.latched = 0;
                if (spent)
                        compositor_access_sticky_publish();
                if (!(compositor_access_aid.riders[byte] & bit))
                        compositor_access_aid.riders[byte] |= bit, compositor_access_aid.riding++;
                compositor_key(at, code, 1, now);
                return;
        }
        compositor_key(at, code, value, now);
        if (!value && (compositor_access_aid.riders[byte] & bit))
        {
                compositor_access_aid.riders[byte] &= (unsigned char)~bit;
                if (!--compositor_access_aid.riding && compositor_access_aid.pressed)
                        compositor_access_kinds(compositor_access_aid.pressed_at, compositor_access_aid.pressed, 0, now), compositor_access_aid.pressed = 0;
        }
}

// The keypad's arrow at a code (8 2 4 6 7 9 1 3, a bit each), -1 for a key that is none; and the way each points, across and down.
static int compositor_access_arrow(unsigned int code)
{
        switch (code)
        {
        case 72: return 0;
        case 80: return 1;
        case 75: return 2;
        case 77: return 3;
        case 71: return 4;
        case 73: return 5;
        case 79: return 6;
        case 81: return 7;
        }
        return -1;
}
static const signed char compositor_access_ways[8][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

// The pointer moved by whole pixels for mouse keys, as the keys' device is (the machine's own or not), the plane at once.
static void compositor_access_mouse_move(int dx, int dy)
{
        int was = compositor_post_physical;

        compositor_post_physical = compositor_access_aid.arrows_physical;
        compositor_access_move(dx, dy);
        compositor_post_physical = was;
}

/*
        The pointer moved on by the arrows held, from their last move to now (a key's report or a millisecond's tick): nothing for the
        delay after the first went down, then a speed that grows with the square of the time since to its full one, by the height of
        the screen as a stick's is. Two that point against each other are none; a diagonal goes as fast as a straight way.
*/
static void compositor_access_mouse_run(positive now)
{
        positive since, after;
        long long ms, speed, step;
        int x = 0, y = 0, dx, dy;

        if (!compositor_access_aid.arrows || now <= compositor_access_aid.moved_at)
                return;
        since = now - compositor_access_aid.moved_at, compositor_access_aid.moved_at = now;
        if (now - compositor_access_aid.arrows_at <= COMPOSITOR_ACCESS_MOUSE_DELAY_NS)
                return;
        after = now - compositor_access_aid.arrows_at - COMPOSITOR_ACCESS_MOUSE_DELAY_NS;
        since = since > after ? after : since;
        since = since > COMPOSITOR_ACCESS_TICK_MOST ? COMPOSITOR_ACCESS_TICK_MOST : since;
        for (unsigned int k = 0; k < 8; k++)
                if (compositor_access_aid.arrows >> k & 1)
                        x += compositor_access_ways[k][0], y += compositor_access_ways[k][1];
        dx = x < 0 ? -1 : x > 0, dy = y < 0 ? -1 : y > 0;
        if (!dx && !dy)
                return;
        ms = (long long)(after / 1000000ull);
        ms = ms > COMPOSITOR_ACCESS_MOUSE_RAMP_MS ? COMPOSITOR_ACCESS_MOUSE_RAMP_MS : ms;
        speed = COMPOSITOR_ACCESS_MOUSE_LEAST +
                (COMPOSITOR_ACCESS_MOUSE_MOST - COMPOSITOR_ACCESS_MOUSE_LEAST) * ms * ms / (COMPOSITOR_ACCESS_MOUSE_RAMP_MS * COMPOSITOR_ACCESS_MOUSE_RAMP_MS);
        // The move in 65536ths of a pixel: the speed (thousandths of the height a second) over the time.
        step = compositor_access_height() * speed * 65536 / 1000 * (long long)since / 1000000000ll;
        if (dx && dy)
                step = step * 46341 / 65536;
        compositor_access_aid.left_x += step * dx, compositor_access_aid.left_y += step * dy;
        x = (int)(compositor_access_aid.left_x / 65536), y = (int)(compositor_access_aid.left_y / 65536);
        compositor_access_aid.left_x -= (long long)x * 65536, compositor_access_aid.left_y -= (long long)y * 65536;
        if (x || y)
                compositor_access_mouse_move(x, y);
}

// A key of the keypad while mouse keys is on: 1 when it is the pointer's (and taken), 0 for a key that is none of them.
static int compositor_access_keypad(unsigned int code, int value, positive now)
{
        int arrow = compositor_access_arrow(code);
        unsigned int button = code == 98 ? COMPOSITOR_BTN_LEFT : code == 55 ? COMPOSITOR_BTN_MIDDLE : code == 74 ? COMPOSITOR_BTN_RIGHT : 0;

        if (arrow < 0 && !button && code != 76 && code != 82 && code != 83)
                return 0;
        if (value == 2)
                return 1;
        compositor_access_seen(WINDOWS_MODALITY_POINTER);
        if (arrow >= 0)
        {
                // The time up to this report is moved by the arrows as they were; a press moves its own pixel at once.
                compositor_access_mouse_run(now);
                if (!value)
                        return compositor_access_aid.arrows &= ~(1u << arrow), 1;
                if (!compositor_access_aid.arrows)
                        compositor_access_aid.arrows_at = compositor_access_aid.moved_at = now, compositor_access_aid.left_x = compositor_access_aid.left_y = 0;
                compositor_access_aid.arrows |= 1u << arrow, compositor_access_aid.arrows_physical = compositor_post_physical != 0;
                compositor_access_mouse_move(compositor_access_ways[arrow][0], compositor_access_ways[arrow][1]);
                return 1;
        }
        if (button)
                return value == 1 ? (void)(compositor_access_aid.button = button) : (void)0, 1;
        if (code == 76 && value == 1 && !compositor_access_aid.clicking && compositor_access_aid.holding != compositor_access_aid.button)
                compositor_access_aid.clicking = compositor_access_aid.button, compositor_press(compositor_access_aid.clicking, 1);
        else if (code == 76 && !value && compositor_access_aid.clicking)
                compositor_press(compositor_access_aid.clicking, 0), compositor_access_aid.clicking = 0;
        else if (code == 82 && value == 1 && !compositor_access_aid.holding)
                compositor_access_aid.holding = compositor_access_aid.button, compositor_press(compositor_access_aid.holding, 1);
        else if (code == 83 && value == 1 && compositor_access_aid.holding)
                compositor_press(compositor_access_aid.holding, 0), compositor_access_aid.holding = 0;
        return 1;
}

// A key past slow and bounce keys: the keypad's while mouse keys is on, then sticky keys', then the loop's own.
static void compositor_access_given(unsigned int at, unsigned int code, int value, positive now)
{
        if ((compositor_access_aid.on & ACCESS_MOUSE) && compositor_access_keypad(code, value, now))
                return;
        if (compositor_access_aid.on & ACCESS_STICKY)
                compositor_access_sticky(at, code, value, now);
        else
                compositor_key(at, code, value, now);
}

// A key from a device while a feature of the keys is on (input.c's compositor_input_take): bounce keys first, then slow keys.
static void compositor_access_key(unsigned int at, unsigned int code, int value, positive now)
{
        unsigned int byte = code >> 3 & 31;
        unsigned char bit = (unsigned char)(1u << (code & 7));

        now = now ? now : clock_monotonic_nanoseconds();
        if (compositor_access_aid.on & ACCESS_BOUNCE)
        {
                if (compositor_access_aid.bounced[byte] & bit)
                {
                        if (!value)
                                compositor_access_aid.bounced[byte] &= (unsigned char)~bit;
                        return;
                }
                if (value == 1 && compositor_access_aid.up_at[code] && now - compositor_access_aid.up_at[code] < compositor_access_aid.bounce_ns)
                {
                        compositor_access_aid.bounced[byte] |= bit;
                        return;
                }
                if (!value)
                        compositor_access_aid.up_at[code] = now;
        }
        if (compositor_access_aid.on & ACCESS_SLOW)
        {
                if (value == 1)
                {
                        if (!(compositor_access_aid.waiting[byte] & bit))
                        {
                                compositor_access_aid.waiting[byte] |= bit, compositor_access_aid.waits++;
                                compositor_access_aid.down_at[code] = now, compositor_access_aid.from[code] = (unsigned short)at;
                                compositor_access_aid.physical_of[byte] = compositor_post_physical ? (unsigned char)(compositor_access_aid.physical_of[byte] | bit)
                                                                                                   : (unsigned char)(compositor_access_aid.physical_of[byte] & ~bit);
                        }
                        return;
                }
                // A repeat or a let go of a key not yet given is none.
                if (compositor_access_aid.waiting[byte] & bit)
                {
                        if (!value)
                                compositor_access_aid.waiting[byte] &= (unsigned char)~bit, compositor_access_aid.waits--;
                        return;
                }
        }
        compositor_access_given(at, code, value, now);
}

// The pointer moved, while dwell is on (input.c's compositor_motion): a move past the slop from where it was still starts the wait again.
static void compositor_access_dwell_moved(void)
{
        long long slop = COMPOSITOR_ACCESS_DWELL_SLOP * (compositor.scale > 0 ? compositor.scale : 100) / 100;
        long long dx = compositor.pointer_x - compositor_access_aid.still_x, dy = compositor.pointer_y - compositor_access_aid.still_y;

        if (dx * dx + dy * dy <= slop * slop)
                return;
        compositor_access_aid.still_x = compositor.pointer_x, compositor_access_aid.still_y = compositor.pointer_y;
        compositor_access_aid.still_at = clock_monotonic_nanoseconds();
        compositor_access_aid.still_physical = compositor_post_physical != 0;
        // A finger presses for itself: a pointer a touch moved is never clicked again by its stillness.
        compositor_access_aid.armed = compositor_access_aid.modality != WINDOWS_MODALITY_TOUCH;
}

/*
        The ring round the pointer while a dwell click is owed: how much of a turn its stillness has filled, in 32nds (pointer.c's
        CP_RING_STEPS), from the third on (a pointer that only paused while it moves shows none); 0 with none owed.
*/
static int compositor_access_ring_at(positive now)
{
        positive steps;

        if (likely(!compositor_access_aid.armed) || !compositor_access_aid.dwell_ns || now <= compositor_access_aid.still_at)
                return 0;
        steps = (now - compositor_access_aid.still_at) * CP_RING_STEPS / compositor_access_aid.dwell_ns;
        return steps < 3 ? 0 : steps > CP_RING_STEPS ? CP_RING_STEPS : (int)steps;
}

// The ring the pointer is drawn with now (kms.c's compositor_pointer_now), kept as drawn.
static int compositor_access_ring(void)
{
        return compositor_access_aid.ring = compositor_access_aid.armed ? compositor_access_ring_at(clock_monotonic_nanoseconds()) : 0;
}

// The pointer still for the dwell's time: the left button pressed and let go where it is, unless a button is held or a hand drags.
static void compositor_access_dwell_click(void)
{
        int was = compositor_post_physical;

        compositor_access_aid.armed = 0;
        if (compositor_access_aid.ring)
                compositor_cursor_move(); // the ring goes with the click
        if (compositor.button_down || compositor.drag >= 0 || compositor.resize >= 0 || compositor.touching)
                return;
        compositor_post_physical = compositor_access_aid.still_physical;
        compositor_press(COMPOSITOR_BTN_LEFT, 1), compositor_press(COMPOSITOR_BTN_LEFT, 0);
        compositor_post_physical = was;
        compositor_access_aid.still_x = compositor.pointer_x, compositor_access_aid.still_y = compositor.pointer_y;
}

// When the loop is next owed a tick of the aids: a slow key's time, a millisecond on while an arrow is held, the dwell's click; 0 for none.
static positive compositor_access_aid_due(void)
{
        positive due = 0, at;

        if (likely(!compositor_access_aid.on))
                return 0;
        for (unsigned int code = 0; compositor_access_aid.waits && code < 256; code++)
                if (compositor_access_aid.waiting[code >> 3] >> (code & 7) & 1)
                        at = compositor_access_aid.down_at[code] + compositor_access_aid.slow_ns, due = !due || at < due ? at : due;
        if (compositor_access_aid.arrows)
        {
                positive from = compositor_access_aid.arrows_at + COMPOSITOR_ACCESS_MOUSE_DELAY_NS;

                at = (compositor_access_aid.moved_at > from ? compositor_access_aid.moved_at : from) + COMPOSITOR_ACCESS_LANE_NS;
                due = !due || at < due ? at : due;
        }
        if (compositor_access_aid.armed)
        {
                // The click, and before it the ring's next 32nd of a turn.
                positive now = clock_monotonic_nanoseconds(), step = compositor_access_aid.dwell_ns / CP_RING_STEPS;

                at = compositor_access_aid.still_at + compositor_access_aid.dwell_ns, due = !due || at < due ? at : due;
                if (step && now >= compositor_access_aid.still_at)
                        at = compositor_access_aid.still_at + ((now - compositor_access_aid.still_at) / step + 1) * step, due = at < due ? at : due;
        }
        return due;
}

// A tick of the aids: each slow key held its time given, the arrows held moved on, and the dwell's click made.
static void compositor_access_aid_tick(positive now)
{
        int was = compositor_post_physical;

        for (unsigned int code = 0; compositor_access_aid.waits && code < 256; code++)
        {
                unsigned char bit = (unsigned char)(1u << (code & 7));

                if (!(compositor_access_aid.waiting[code >> 3] & bit) || now < compositor_access_aid.down_at[code] + compositor_access_aid.slow_ns)
                        continue;
                compositor_access_aid.waiting[code >> 3] &= (unsigned char)~bit, compositor_access_aid.waits--;
                compositor_post_physical = (compositor_access_aid.physical_of[code >> 3] & bit) != 0;
                compositor_access_given(compositor_access_aid.from[code], code, 1, now);
        }
        compositor_post_physical = was;
        compositor_access_mouse_run(now);
        if (compositor_access_aid.armed && now >= compositor_access_aid.still_at + compositor_access_aid.dwell_ns)
                compositor_access_dwell_click();
        else if (compositor_access_ring_at(now) != compositor_access_aid.ring)
                compositor_cursor_move(); // the ring a 32nd of a turn on (drawn with the pointer, compositor_pointer_now)
}

/*
        The aids as the settings have them: what was held for one turned off is let go (a modifier sticky keys pressed, the button mouse
        keys holds), what one waited on is dropped, and dwell starts from where the pointer is, owing no click till it moves. The gate
        the reader reads is published last.
*/
static void compositor_access_aid_set(unsigned int on, positive slow_ns, positive bounce_ns, positive dwell_ns)
{
        unsigned int was = compositor_access_aid.on, off = was & ~on;
        positive now = clock_monotonic_nanoseconds();

        compositor_access_aid.slow_ns = slow_ns, compositor_access_aid.bounce_ns = bounce_ns, compositor_access_aid.dwell_ns = dwell_ns;
        if (off & ACCESS_STICKY)
        {
                if (compositor_access_aid.pressed)
                        compositor_access_kinds(compositor_access_aid.pressed_at, compositor_access_aid.pressed, 0, now);
                compositor_access_aid.latched = compositor_access_aid.locked = compositor_access_aid.physical = compositor_access_aid.alone = 0;
                compositor_access_aid.pressed = compositor_access_aid.riding = 0;
                memory_zero(compositor_access_aid.riders, sizeof compositor_access_aid.riders);
                compositor_access_sticky_publish();
        }
        if (!(on & ACCESS_SLOW))
                memory_zero(compositor_access_aid.waiting, sizeof compositor_access_aid.waiting), compositor_access_aid.waits = 0;
        if (!(on & ACCESS_BOUNCE))
                memory_zero(compositor_access_aid.bounced, sizeof compositor_access_aid.bounced);
        if (off & ACCESS_MOUSE)
        {
                if (compositor_access_aid.clicking)
                        compositor_press(compositor_access_aid.clicking, 0);
                if (compositor_access_aid.holding)
                        compositor_press(compositor_access_aid.holding, 0);
                compositor_access_aid.clicking = compositor_access_aid.holding = compositor_access_aid.arrows = 0;
                compositor_access_aid.button = COMPOSITOR_BTN_LEFT;
        }
        if ((on & ACCESS_DWELL) && !(was & ACCESS_DWELL))
                compositor_access_aid.still_x = compositor.pointer_x, compositor_access_aid.still_y = compositor.pointer_y;
        compositor_access_aid.armed &= (on & ACCESS_DWELL) != 0 && (was & ACCESS_DWELL) != 0;
        if (compositor_access_aid.ring && !compositor_access_aid.armed)
                compositor_cursor_move();
        compositor_access_aid.on = on;
        compositor_access_dwells = (on & ACCESS_DWELL) != 0;
        __atomic_store_n(&compositor_access_gate, (on & ACCESS_KEYED) != 0 ? 1u : 0u, __ATOMIC_RELEASE);
}

// The Accessibility rows read (session.c's COMPOSITOR_SET_ACCESS): a switch's value is 0 for on, a time's is its ms or 0 for off.
static void compositor_access_settings_read(void)
{
        long sticky = 1, slow = 0, bounce = 0, mouse = 1, dwell = 0;

        compositor_setting_num(SETTING_ACCESS_STICKY_PATH, &sticky);
        compositor_setting_num(SETTING_ACCESS_SLOW_PATH, &slow);
        compositor_setting_num(SETTING_ACCESS_BOUNCE_PATH, &bounce);
        compositor_setting_num(SETTING_ACCESS_MOUSEKEYS_PATH, &mouse);
        compositor_setting_num(SETTING_ACCESS_DWELL_PATH, &dwell);
        compositor_access_aid_set((sticky == 0 ? ACCESS_STICKY : 0u) | (slow > 0 ? ACCESS_SLOW : 0u) | (bounce > 0 ? ACCESS_BOUNCE : 0u) |
                                      (mouse == 0 ? ACCESS_MOUSE : 0u) | (dwell > 0 ? ACCESS_DWELL : 0u),
                                  slow > 0 ? (positive)slow * 1000000ull : 0, bounce > 0 ? (positive)bounce * 1000000ull : 0,
                                  dwell > 0 ? (positive)dwell * 1000000ull : 0);
}
