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
        ACCESS_KEYBOARD, ACCESS_BACKSPACE, ACCESS_ENTER, ACCESS_ACTIONS
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
        [ACCESS_KEY_RIGHT] = 106, [ACCESS_BACKSPACE] = CK_KEY_BACKSPACE, [ACCESS_ENTER] = 28,
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
        b32 yields;                     // the pad is the focused program's
        int asked_row;                  // the row the program question was last asked for, its process, and when
        unsigned int asked_pid;
        positive asked_at;
        b32 reads;                      // and the answer: the program reads pads
        unsigned int hidraw;            // the character major of /dev/hidraw*, 0 when not found
        b32 hidraw_read;
        unsigned int games;             // how many of game[] are kept
        unsigned int game[32];          // the minors of the event devices found to be a game's (major 13), opened here or not
} compositor_access = {.of = {[0 ... COMPOSITOR_INPUTS - 1] = -1}, .asked_row = -1};

static void compositor_access_act(unsigned int action, int down, unsigned int at, positive now);

static b32 compositor_access_drives(void) { return compositor_access.hand; }

// A controller is the hand now: the keyboard on the screen is looked at again (it comes up on a field for a controller, as for a touch).
static void compositor_access_take_hand(void)
{
        if (!compositor_access.hand)
                compositor_access.hand = 1, compositor.osk_typed = 0, compositor_osk_sync();
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
static b32 compositor_access_open(unsigned int at, bipolar handle, const p8 *keys, unsigned long absolute, unsigned int properties,
                                  const char *link, unsigned int number)
{
        static const char *const xbox[] = {"/xpad", "/hid-steam", "/microsoft"};
        struct compositor_access_pad *pad = null;
        char path[64], driver[160];
        long got;
        int index = -1;

        if ((properties & 0x40) || (absolute & 3) != 3 || at >= COMPOSITOR_INPUTS ||
            !((keys[0x130 / 8] >> (0x130 % 8) & 1) || (keys[0x120 / 8] >> (0x120 % 8) & 1)) ||
            !link || string_search((string_address)link, (string_address) "/devices/virtual/input/"))
                return 0;
        for (int each = 0; each < COMPOSITOR_ACCESS_PADS && index < 0; each++)
                if (compositor_access.pad[each].at < 0 || compositor_access.pad[each].at == (int)at)
                        index = each;
        if (index < 0)
                return 0;
        pad = &compositor_access.pad[index];
        memory_zero(pad, sizeof *pad);
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
        case ACCESS_BACKSPACE: case ACCESS_ENTER:
                compositor_key(at, compositor_access_keys[action], down, now);
                if (action != ACCESS_BACK && action != ACCESS_ENTER)
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
                        action = compositor.osk_shown && compositor_access_typing[button] ? compositor_access_typing[button] : compositor_access_actions[button],
                        pad->pressed_as[button] = (unsigned char)action;
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
        return compositor_access_share(lx, ly, &r) || compositor_access_share(rx, ry, &r) || compositor_access.scrolled;
}

// When the loop is next owed a tick of the pads: a frame on while a stick is pushed, or the next repeat of an arrow; 0 for none.
static positive compositor_access_due(void)
{
        positive period = compositor.period ? compositor.period : 1000000000ull / 60, due = 0;

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
        if ((share = compositor_access_share(lx, ly, &r)) && since && !compositor_wl_locked())
        {
                // The move in 65536ths of a pixel: the speed at the rim (a share of the screen's height) times the stick's share, over the time.
                long long step = compositor_access_height() * COMPOSITOR_ACCESS_SPEED / 10 * share * (long long)since / 1000000000ll;
                int dx, dy;

                compositor_access.left_x += step * lx / (long long)r, compositor_access.left_y += step * ly / (long long)r;
                dx = (int)(compositor_access.left_x / 65536), dy = (int)(compositor_access.left_y / 65536);
                compositor_access.left_x -= (long long)dx * 65536, compositor_access.left_y -= (long long)dy * 65536;
                compositor_access_take_hand();
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
