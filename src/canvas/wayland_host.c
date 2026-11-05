/*
        The compositor's side of the Wayland shim: the socket, the clients' descriptors, and the shim's host (struct
        wayland_host) made of the compositor's own functions. It is included by compositor.c after wayland.c, when
        CONFIG_MOONWATER_WAYLAND is on, and is part of the compositor's one loop: the loop polls the listening socket
        and each client's, calls wayland_feed with what was read, and writes back what the shim queued, once a pass
        before the screen is drawn (so an event waits for no frame) and once after (for the frame's own callbacks).

        A client's window is a row of the table that this process claims for it; what a program does to its row
        with the table, the shim does here by calling the compositor's functions on it, and what the compositor says
        of a row it reads back in wayland_facts. Nothing a client sent reaches any compositor function unchecked:
        the shim checks first, and every call below is one the compositor makes for its own programs.

        The socket is /run/moonwater/wayland-0, mode 0666, so any program of the machine may connect and is held to
        the same caps whoever it is; the peer's pid and uid are kept for the table and for the per-user cap (a user
        may hold at most COMPOSITOR_WL_PER_USER of the slots, root as many as are free), so one user cannot take every
        slot and starve the others. A pool is mapped read-only once; the one thing a client can do to a mapping is
        shrink its file, which raises SIGBUS where the compositor reads it, and the handler maps zero pages over the
        pool and marks it, and the copy that was in progress finishes on zeros and is refused.
*/
#ifndef WAYLAND_HOST_INCLUDED
#define WAYLAND_HOST_INCLUDED

#define COMPOSITOR_WL_PATH "/run/moonwater/wayland-0"
#define COMPOSITOR_WL_BACKLOG 16
#define COMPOSITOR_WL_PER_USER 8
#define COMPOSITOR_WL_READ 16384        // one read; a client gets at most COMPOSITOR_WL_READS of them a pass
#define COMPOSITOR_WL_READS 4
#define COMPOSITOR_WL_ACCEPTS 4
#define COMPOSITOR_POLLOUT 4
#define COMPOSITOR_POLLERR 8
#define COMPOSITOR_WL_SIGBUS 7
#define COMPOSITOR_WL_SIGPIPE 13
#define COMPOSITOR_WL_SIGALRM 14
#define COMPOSITOR_WL_TRANSFERS 16               // copies being read and pastes being written, all clients together
#define COMPOSITOR_WL_TRANSFERS_PER_CLIENT 4
#define COMPOSITOR_WL_PULL_NS 3000000000ull      // a copy's source has this long to write its text and end
#define COMPOSITOR_WL_PUSH_NS 5000000000ull      // a paste's reader has this long to take the text
#define COMPOSITOR_WL_CHUNK 4096                 // PIPE_BUF: a write of this much to a pipe that poll said is ready does not wait
#define COMPOSITOR_WL_PULL 1
#define COMPOSITOR_WL_PUSH 2
#define COMPOSITOR_WL_DMABUF_SYNC 0x40086200u         // DMA_BUF_IOCTL_SYNC
#define COMPOSITOR_WL_DMABUF_WAIT_NS 30000000ull      // how long a dma-buf's writer is waited for
#define COMPOSITOR_WL_DMABUF_SLOW_NS 2000000000ull    // after one that did not finish in time, a client's are not waited for this long
#define COMPOSITOR_WL_FOURCC_ARGB8888 0x34325241u
#define COMPOSITOR_WL_FOURCC_XRGB8888 0x34325258u

_Static_assert(WAYLAND_ROWS == WINDOWS, "the shim's rows are the compositor's table");

static struct
{
        int listen;
        int fd[WAYLAND_CLIENTS];
        int uid[WAYLAND_CLIENTS], pid[WAYLAND_CLIENTS];
        unsigned char asked[WAYLAND_CLIENTS]; // the connection's first window has been opened, so its launch is not read again
        int started, handler;
        unsigned long frames;
        // The picture being laid on each window: whether one is begun, the buffer of the table it goes in, what it has touched so
        // far, and whether all of it is changed.
        struct
        {
                unsigned char open, touched, full, which;
                int x1, y1, x2, y2;
        } laying[WINDOWS];
        // The keymap of each layout, made when a keyboard first needs it: a sealed file of xkb's text, kept for as long as the
        // compositor runs, and the layout the clients were last told.
        int keymap[16];
        unsigned long keymap_size[16];
        unsigned int layout_told;
        unsigned long screens_told; // the screens the clients were last told, as compositor_wl_screens_digest says them
        long repeat_told[2];        // and the key repeat they were last told: its rate and its delay
        // The clipboard's transfers. A pull is a copy's source being read: its pipe's read end (in) and the sealed file the text is
        // gathered in (keep); the end the source writes to is the shim's, in the client's queue until it is sent, and the
        // compositor's copy goes when it is, so the source's closing is the end of the file. A push is a paste: the compositor's text
        // (in, a descriptor of its own for the file, so a newer copy cannot change it half way) written to the client's (out).
        // Each is read and written by a thread of its own (worker), which says how it ended in finished (1 badly, 2 well) and
        // stops when the loop sets cancel; total is a paste's length, limit the most a copy may be.
        struct
        {
                unsigned char state, which;
                int client, row, in, out, keep;
                unsigned long total, limit;
                positive deadline;
                thread *worker;
                int finished, cancel;
        } transfer[COMPOSITOR_WL_TRANSFERS];
        int wake;                       // the eventfd a transfer's thread knocks on when it has ended
        int hold_row, hold;             // the window that holds the pointer, and how: 0 free, 1 locked, 2 confined
        int dmabuf_table;               // the file of format and modifier pairs the feedback names, made the first time
        unsigned char cursor_asked[WINDOWS]; // the cursor a window's client asked for (wp_cursor_shape, set_cursor): its CP_ shape + 1, COMPOSITOR_WL_HIDDEN, or 0 for none
        unsigned long presents;         // the frames drawn and flipped, numbered for wp_presentation's sequence
        int presenting_waiting;         // a drawn frame's feedbacks wait for its flip to complete (compositor_wl_flip_presented)
        positive presenting_since;      // when they began to wait: a flip that never completes is answered by the clock two frames on
        unsigned long render;           // the render node clients allocate on (a dev_t), found the first time; 1 when there is none
        positive slow_until[WAYLAND_CLIENTS];
        // Where each pool is mapped, so a SIGBUS can be put to the pool whose file was cut.
        struct
        {
                volatile unsigned long at, size;
                volatile int bad;
        } map[WAYLAND_CLIENTS * WAYLAND_POOLS];
        // The client's own pictures the row shows, read in place (compositor_wl_lend): the first pixel of the picture in the client's
        // buffer, its pitch in pixels and its size, the card's address of that pixel when it is a dma-buf (its import's address, let go
        // of when the picture is) and the mapping the buffer is in. pixels 0 for a row whose picture is the table's.
        struct
        {
                const unsigned int *pixels;
                unsigned long pitch;
                long width, height;
                positive va, import;
                const unsigned char *map;
                long fd;
        } lent[WINDOWS];
        // The dma-buf a row's damage is copied from on the card (compositor_wl_card_rect): its mapping and its import's address, let go of
        // when the buffer changes or the row closes.
        struct
        {
                const unsigned char *map;
                positive va;
        } source[WINDOWS];
} compositor_wl = {.listen = -1, .wake = -1, .lent = {{0}}};

/* ---- the keymap a client reads ---- */

/*
        The compositor decides what a key means with its own tables (compositor_keymap_character); a Wayland client
        decides it itself, from a keymap in xkb's text the compositor sends, and then handles dead keys, compose and
        repeat as its toolkit does. So the keymap is made from the same tables: every key of the layout at its four
        levels as the compositor reads them (the characters as Unicode keysyms, the accents as dead keys), the keys that
        are no characters by name (modifiers, the function and cursor keys, the keypad), and the modifiers the way a
        client's xkb reads them: Shift, Lock, Control, Alt on Mod1, Num Lock on Mod2, the logo keys on Mod4 and AltGr,
        the third level, on Mod5. It is written in full, with no `include`, so a client that has no xkb data on the
        machine reads it all the same.
*/
#define COMPOSITOR_WL_KEYMAP_ROOM 24576
/*
        The card copies a commit's damage when the damage sums to at least this many pixels, and the processor copies all of a smaller one. Measured on
        the box (gpu copy bench in test/checks.c, the card warm: a job back to back, cold source and destination): the processor copies a square of
        4k, 16k, 64k, 256k and 1M pixels in 0, 2, 13, 49 and 179 us; the card's job takes 81, 101, 130, 139 and 326 us of wall time, which is longer at
        every size (the card is never cheaper in time), but 50 to 60 us of the compositor's own CPU at every size (the wait is spun), 90 us at 1M.
        So the card is the cheaper CPU from about 256k pixels (512 by 512, even there) and clearly cheaper from 1M. A card awake for the commit
        (the job of its neighbour in the tick) is the same wall time, not less, so the threshold does not move with it. One constant.
*/
#define COMPOSITOR_WL_CARD_COPY_PIXELS 262144

// The keys that are no characters, by Linux code: the symbol's name, and the type of key it is.
static const struct
{
        unsigned char code;
        const char *name;
} compositor_wl_named[] = {
    {1, "Escape"},      {14, "BackSpace"}, {15, "Tab"},        {28, "Return"},     {29, "Control_L"}, {42, "Shift_L"},
    {54, "Shift_R"},    {56, "Alt_L"},     {58, "Caps_Lock"},  {59, "F1"},         {60, "F2"},        {61, "F3"},
    {62, "F4"},         {63, "F5"},        {64, "F6"},         {65, "F7"},         {66, "F8"},        {67, "F9"},
    {68, "F10"},        {69, "Num_Lock"},  {70, "Scroll_Lock"}, {87, "F11"},       {88, "F12"},       {96, "KP_Enter"},
    {97, "Control_R"},  {98, "KP_Divide"}, {99, "Print"},      {100, "ISO_Level3_Shift"}, {102, "Home"}, {103, "Up"},
    {104, "Prior"},     {105, "Left"},     {106, "Right"},     {107, "End"},       {108, "Down"},     {109, "Next"},
    {110, "Insert"},    {111, "Delete"},   {119, "Pause"},     {125, "Super_L"},   {126, "Super_R"},  {127, "Menu"},
};

// The keypad's, which Num Lock turns into digits: the code, the symbol without Num Lock and the one with.
static const struct
{
        unsigned char code;
        const char *plain, *locked;
} compositor_wl_keypad[] = {
    {71, "KP_Home", "KP_7"},   {72, "KP_Up", "KP_8"},      {73, "KP_Prior", "KP_9"}, {75, "KP_Left", "KP_4"},
    {76, "KP_Begin", "KP_5"},  {77, "KP_Right", "KP_6"},   {79, "KP_End", "KP_1"},   {80, "KP_Down", "KP_2"},
    {81, "KP_Next", "KP_3"},   {82, "KP_Insert", "KP_0"},  {83, "KP_Delete", "KP_Decimal"},
};

struct compositor_wl_text
{
        char *at;
        unsigned long room, used;
        int full;
};

static void compositor_wl_put(struct compositor_wl_text *t, const char *text)
{
        while (*text)
        {
                if (t->used + 1 >= t->room)
                {
                        t->full = 1;
                        return;
                }
                t->at[t->used++] = *text++;
        }
}

static void compositor_wl_number(struct compositor_wl_text *t, unsigned int value)
{
        char text[8];
        unsigned int digits = 0;

        for (unsigned int copy = value; copy; copy /= 10)
                digits++;
        digits = digits ? digits : 1;
        for (unsigned int index = digits; index--; value /= 10)
                text[index] = (char)('0' + value % 10);
        text[digits] = 0;
        compositor_wl_put(t, text);
}

static void compositor_wl_put_hex(struct compositor_wl_text *t, unsigned int value)
{
        char text[8] = {'U', 0, 0, 0, 0, 0, 0, 0};
        int digits = value > 0xffff ? 6 : 4;

        for (int at = 0; at < digits; at++)
                text[1 + at] = "0123456789abcdef"[(value >> (4 * (digits - 1 - at))) & 15];
        compositor_wl_put(t, text);
}

// A character as the keysym a client's xkb reads: a dead accent by name, anything else as the Unicode keysym of it.
static void compositor_wl_symbol(struct compositor_wl_text *t, unsigned int c)
{
        if (c & CK_DEAD)
        {
                unsigned int accent = c & ~CK_DEAD;

                compositor_wl_put(t, accent == CK_ACUTE      ? "dead_acute"
                                     : accent == CK_GRAVE     ? "dead_grave"
                                     : accent == CK_DIAERESIS ? "dead_diaeresis"
                                     : accent == CK_CIRCUMFLEX ? "dead_circumflex"
                                     : accent == CK_TILDE      ? "dead_tilde"
                                                               : "NoSymbol");
                return;
        }
        if (!c)
        {
                compositor_wl_put(t, "NoSymbol");
                return;
        }
        compositor_wl_put_hex(t, c);
}

// Whether a pair of characters is a letter and its capital, which Caps Lock turns over.
static int compositor_wl_letters(unsigned int low, unsigned int high)
{
        return low && high && !(low & CK_DEAD) && !(high & CK_DEAD) && compositor_letter_flip(low) == high && low != high;
}

/*
        The keymap of a layout, written into into: its length, or 0 when it did not fit (a full keymap is about 8
        KiB). A key's four levels are what compositor_keymap_character says at none, Shift, AltGr and both.
*/
static unsigned long compositor_wl_keymap_text(unsigned int layout, char *into, unsigned long room)
{
        struct compositor_wl_text text = {into, room, 0, 0};
        struct compositor_keymap map;
        static const unsigned char codes[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                                              23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
                                              45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
                                              67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 86, 87, 88, 96, 97,
                                              98, 99, 100, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 119, 125, 126, 127};

        memory_zero(&map, sizeof map);
        map.layout = (unsigned char)layout;
        compositor_wl_put(&text, "xkb_keymap {\n");
        compositor_wl_put(&text, "xkb_keycodes \"canvas\" {\n  minimum = 8;\n  maximum = 255;\n");
        for (unsigned long at = 0; at < sizeof codes; at++)
        {
                unsigned int code = codes[at];

                compositor_wl_put(&text, "  <K");
                compositor_wl_number(&text, code);
                compositor_wl_put(&text, "> = ");
                compositor_wl_number(&text, code + 8);
                compositor_wl_put(&text, ";\n");
        }
        // The lights: an X server (Xwayland) updates them when a modifier changes, and a keymap with none leaves it nothing to update.
        compositor_wl_put(&text, "  indicator 1 = \"Caps Lock\";\n  indicator 2 = \"Num Lock\";\n");
        compositor_wl_put(&text, "};\n");
        compositor_wl_put(&text, "xkb_types \"canvas\" {\n  virtual_modifiers NumLock,LevelThree;\n");
        compositor_wl_put(&text, "  type \"ONE_LEVEL\" { modifiers = none; map[none] = Level1; };\n");
        compositor_wl_put(&text, "  type \"TWO_LEVEL\" { modifiers = Shift; map[Shift] = Level2; };\n");
        compositor_wl_put(&text, "  type \"ALPHABETIC\" { modifiers = Shift+Lock; map[Shift] = Level2; map[Lock] = Level2; };\n");
        compositor_wl_put(&text, "  type \"FOUR_LEVEL\" { modifiers = Shift+LevelThree; map[Shift] = Level2; map[LevelThree] = Level3;"
                                 " map[Shift+LevelThree] = Level4; };\n");
        compositor_wl_put(&text, "  type \"FOUR_LEVEL_ALPHABETIC\" { modifiers = Shift+Lock+LevelThree; map[Shift] = Level2;"
                                 " map[Lock] = Level2; map[LevelThree] = Level3; map[Shift+LevelThree] = Level4;"
                                 " map[Lock+LevelThree] = Level4; map[Shift+Lock+LevelThree] = Level3; };\n");
        compositor_wl_put(&text, "  type \"KEYPAD\" { modifiers = Shift+NumLock; map[none] = Level1; map[Shift] = Level2;"
                                 " map[NumLock] = Level2; map[Shift+NumLock] = Level1; };\n");
        compositor_wl_put(&text, "};\n");
        compositor_wl_put(&text, "xkb_compat \"canvas\" {\n  virtual_modifiers NumLock,LevelThree;\n");
        compositor_wl_put(&text, "  interpret.useModMapMods = AnyLevel;\n  interpret.repeat = False;\n");
        compositor_wl_put(&text, "  interpret Shift_L { action = SetMods(modifiers = Shift); };\n");
        compositor_wl_put(&text, "  interpret Shift_R { action = SetMods(modifiers = Shift); };\n");
        compositor_wl_put(&text, "  interpret Control_L { action = SetMods(modifiers = Control); };\n");
        compositor_wl_put(&text, "  interpret Control_R { action = SetMods(modifiers = Control); };\n");
        compositor_wl_put(&text, "  interpret Alt_L { action = SetMods(modifiers = Mod1); };\n");
        compositor_wl_put(&text, "  interpret Super_L { action = SetMods(modifiers = Mod4); };\n");
        compositor_wl_put(&text, "  interpret Super_R { action = SetMods(modifiers = Mod4); };\n");
        compositor_wl_put(&text, "  interpret Caps_Lock { action = LockMods(modifiers = Lock); };\n");
        compositor_wl_put(&text, "  interpret Num_Lock { virtualModifier = NumLock; action = LockMods(modifiers = NumLock); };\n");
        compositor_wl_put(&text, "  interpret ISO_Level3_Shift { virtualModifier = LevelThree; action = SetMods(modifiers = LevelThree); };\n");
        compositor_wl_put(&text, "  indicator \"Caps Lock\" { !allowExplicit; whichModState = Locked; modifiers = Lock; };\n");
        compositor_wl_put(&text, "  indicator \"Num Lock\" { !allowExplicit; whichModState = Locked; modifiers = NumLock; };\n");
        compositor_wl_put(&text, "};\n");
        compositor_wl_put(&text, "xkb_symbols \"canvas\" {\n  name[Group1] = \"Canvas\";\n");
        for (unsigned long at = 0; at < sizeof codes; at++)
        {
                unsigned int code = codes[at], levels[4], named = 0, keypad = 0;
                const char *name = 0, *plain = 0, *locked = 0;

                for (unsigned long index = 0; index < sizeof compositor_wl_named / sizeof compositor_wl_named[0]; index++)
                        if (compositor_wl_named[index].code == code)
                                name = compositor_wl_named[index].name, named = 1;
                for (unsigned long index = 0; index < sizeof compositor_wl_keypad / sizeof compositor_wl_keypad[0]; index++)
                        if (compositor_wl_keypad[index].code == code)
                                plain = compositor_wl_keypad[index].plain, locked = compositor_wl_keypad[index].locked, keypad = 1;
                compositor_wl_put(&text, "  key <K");
                compositor_wl_number(&text, code);
                compositor_wl_put(&text, "> { ");
                if (named)
                {
                        compositor_wl_put(&text, "[ ");
                        compositor_wl_put(&text, name);
                        compositor_wl_put(&text, " ] };\n");
                        continue;
                }
                if (keypad)
                {
                        compositor_wl_put(&text, "type = \"KEYPAD\", [ ");
                        compositor_wl_put(&text, plain);
                        compositor_wl_put(&text, ", ");
                        compositor_wl_put(&text, locked);
                        compositor_wl_put(&text, " ] };\n");
                        continue;
                }
                levels[0] = compositor_keymap_character(&map, code, 0);
                levels[1] = compositor_keymap_character(&map, code, WINDOW_KEY_SHIFT);
                levels[2] = compositor_keymap_character(&map, code, WINDOW_KEY_ALTGR);
                levels[3] = compositor_keymap_character(&map, code, WINDOW_KEY_SHIFT | WINDOW_KEY_ALTGR);
                {
                        int letters = compositor_wl_letters(levels[0], levels[1]);
                        int four = levels[2] != levels[0] || levels[3] != levels[1];

                        compositor_wl_put(&text, "type = \"");
                        compositor_wl_put(&text, four ? (letters ? "FOUR_LEVEL_ALPHABETIC" : "FOUR_LEVEL")
                                                      : levels[0] == levels[1] ? "ONE_LEVEL" : letters ? "ALPHABETIC" : "TWO_LEVEL");
                        compositor_wl_put(&text, "\", [ ");
                        for (int level = 0; level < (four ? 4 : levels[0] == levels[1] ? 1 : 2); level++)
                        {
                                if (level)
                                        compositor_wl_put(&text, ", ");
                                compositor_wl_symbol(&text, levels[level]);
                        }
                        compositor_wl_put(&text, " ] };\n");
                }
        }
        compositor_wl_put(&text, "  modifier_map Shift { <K42>, <K54> };\n  modifier_map Lock { <K58> };\n");
        compositor_wl_put(&text, "  modifier_map Control { <K29>, <K97> };\n  modifier_map Mod1 { <K56> };\n");
        compositor_wl_put(&text, "  modifier_map Mod2 { <K69> };\n  modifier_map Mod4 { <K125>, <K126> };\n");
        compositor_wl_put(&text, "  modifier_map Mod5 { <K100> };\n};\n};\n");
        if (text.full)
                return 0;
        into[text.used] = 0;
        return text.used;
}

/* ---- the host ---- */

static void *compositor_wl_alloc(void *context, long bytes)
{
        (void)context;
        return compositor_map_zeroed((unsigned long)bytes);
}

static void compositor_wl_release(void *context, void *block, long bytes)
{
        (void)context;
        compositor_call(syscall(munmap), (positive)block, (positive)bytes, 0, 0, 0, 0);
}

/* A regular file of at least size bytes, mapped read-only and shared; null for anything else. */
static void *compositor_wl_map(void *context, long fd, long size)
{
        // struct statx: stx_mode at 28 (u16), stx_size at 40 (u64); asked for the type and the size, of the descriptor itself.
        unsigned char info[256];
        long got, at;
        unsigned int mode;
        unsigned long length;

        (void)context;
        memory_zero(info, sizeof info);
        got = compositor_call(syscall(statx), (positive)fd, (positive) "", 0x1000, 0x201, (positive)info, 0);
        if (got < 0)
                return 0;
        mode = (unsigned int)info[28] | (unsigned int)info[29] << 8;
        length = *(unsigned long *)(info + 40);
        if ((mode & 0170000) != 0100000 || length < (unsigned long)size)
                return 0;
        got = compositor_call(syscall(mmap), 0, (positive)size, 1, 1, (positive)fd, 0);
        if (got < 0 && got > -4096)
                return 0;
        for (at = 0; at < WAYLAND_CLIENTS * WAYLAND_POOLS; at++)
                if (!compositor_wl.map[at].at)
                {
                        compositor_wl.map[at].bad = 0;
                        compositor_wl.map[at].size = (unsigned long)size;
                        compositor_wl.map[at].at = (unsigned long)got;
                        return (void *)got;
                }
        compositor_call(syscall(munmap), (positive)got, (positive)size, 0, 0, 0, 0);
        return 0;
}

static void *compositor_wl_remap(void *context, void *at, long old_size, long new_size)
{
        long got;

        (void)context;
        // The card's maps of the old pages are let go of: the pages move (a map of them would be the old ones).
        gpu_forget(at, (positive)old_size);
        got = compositor_call(syscall(mremap), (positive)at, (positive)old_size, (positive)new_size, 1, 0, 0);
        if (got < 0 && got > -4096)
                return 0;
        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at == (unsigned long)at)
                {
                        compositor_wl.map[slot].size = (unsigned long)new_size;
                        compositor_wl.map[slot].at = (unsigned long)got;
                }
        return (void *)got;
}

static void compositor_wl_unmap(void *context, void *at, long size)
{
        (void)context;
        gpu_forget(at, (positive)size);
        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at == (unsigned long)at)
                {
                        compositor_wl.map[slot].at = 0;
                        compositor_wl.map[slot].size = 0;
                        compositor_wl.map[slot].bad = 0;
                }
        compositor_call(syscall(munmap), (positive)at, (positive)size, 0, 0, 0, 0);
}

static void compositor_wl_close(void *context, long fd)
{
        (void)context;
        system_close((int)fd);
}

/*
        A pool whose file was cut under the compositor: the signal handler of SIGBUS. The faulting address names the pool;
        zero pages are mapped over all of it, so the read that faulted goes on and finds zeros, and the pool is marked.
        A SIGBUS that is not on a pool is not this handler's: it is put back to its default and returned from, which
        raises it again and ends the compositor as it would have ended.
*/
static void compositor_wl_bus(int number, void *info, void *context)
{
        unsigned long where = *(unsigned long *)((unsigned char *)info + 16);

        (void)context;
        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at && where >= compositor_wl.map[slot].at &&
                    where - compositor_wl.map[slot].at < compositor_wl.map[slot].size)
                {
                        // PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED
                        compositor_call(syscall(mmap), compositor_wl.map[slot].at, compositor_wl.map[slot].size, 1, 0x32,
                                        (positive)-1, 0);
                        compositor_wl.map[slot].bad = 1;
                        return;
                }
        system_signal_install((b32)number, 0, 0, 0, null);
}

/* Whether the pool the pixels are in was cut. */
static int compositor_wl_cut(const void *pixels)
{
        unsigned long at = (unsigned long)pixels;

        for (long slot = 0; slot < WAYLAND_CLIENTS * WAYLAND_POOLS; slot++)
                if (compositor_wl.map[slot].at && at >= compositor_wl.map[slot].at &&
                    at - compositor_wl.map[slot].at < compositor_wl.map[slot].size)
                        return compositor_wl.map[slot].bad;
        return 0;
}

/*
        Whether the person asked for the program a client is: MOONWATER_ASKED=1 in its environment, which the launcher, a typed
        command or a terminal the keys opened set, and which a program of the compositor's own empties after its first window.
        Read from /proc as the compositor, which is root; at most a few KiB, and the first window of a connection only.
*/
static int compositor_wl_asked(long client)
{
        char path[32] = "/proc/", text[4096];
        unsigned int length = 6, pid = compositor_wl.pid[client] > 0 ? (unsigned int)compositor_wl.pid[client] : 0, digits = 0;
        long handle, got, held = 0;
        int found = 0;

        if (!pid)
                return 0;
        for (unsigned int copy = pid; copy; copy /= 10)
                digits++;
        for (unsigned int index = digits; index--; pid /= 10)
                path[length + index] = (char)('0' + pid % 10);
        length += digits;
        for (const char *tail = "/environ"; *tail; tail++)
                path[length++] = *tail;
        path[length] = 0;
        handle = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return 0;
        // The variables run together, ended by NULs; an entry cut by the end of what was read is not looked at.
        while (!found && (got = (long)system_read_once((int)handle, text + held, sizeof text - 1 - (unsigned long)held)) > 0)
        {
                long start = 0, limit = held + got;

                for (long at = 0; at < limit; at++)
                        if (!text[at])
                        {
                                found |= at - start == 17 && !memory_compare(text + start, "MOONWATER_ASKED=1", 17);
                                start = at + 1;
                        }
                held = limit - start;
                for (long at = 0; at < held; at++)
                        text[at] = text[start + at];
                if (held >= (long)sizeof text - 1)
                        held = 0;
        }
        system_close((int)handle);
        return found;
}

/* A table row for a client's window, claimed as a program claims one, with its pid. -1 when none is free. */
static int compositor_wl_row_open(void *context, long client, long kind, int parent, long x, long y)
{
        struct windows *table = compositor.table;

        (void)context;
        if (!table || client < 0 || client >= WAYLAND_CLIENTS)
                return -1;
        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned int none = 0;

                if (!__atomic_compare_exchange_n(&table->pid[row], &none, WINDOWS_TAKING, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
                        continue;
                windows_reset_row(table, (unsigned int)row);
                compositor_wl.laying[row].open = 0;
                // A client draws its own frame, and keeps the whole of its picture, the top included.
                table->glass[row] = WINDOW_NO_CLOSE | WINDOW_NO_GRAB;
                // The person's own launch takes the keys with its first window, as a program of the compositor's does.
                if (kind == WAYLAND_KIND_TOPLEVEL && !compositor_wl.asked[client])
                {
                        compositor_wl.asked[client] = 1;
                        table->asked[row] = (unsigned int)compositor_wl_asked(client);
                }
                if (kind == WAYLAND_KIND_POPUP)
                {
                        table->popup_parent[row] = (unsigned int)parent + 1;
                        table->popup_x[row] = (int)x;
                        table->popup_y[row] = (int)y;
                        table->popup_flags[row] = WINDOW_POPUP_POINT | WINDOW_POPUP_EXACT;
                        table->glass[row] |= WINDOW_NO_TAB;
                }
                // The client's pid as the kernel gave it; a peer in a namespace the compositor cannot see has none, and a row's pid
                // is never 0 while it is taken.
                __atomic_store_n(&table->pid[row], compositor_wl.pid[client] > 0 ? (unsigned int)compositor_wl.pid[client] : 1u,
                                 __ATOMIC_RELEASE);
                return row;
        }
        return -1;
}

static void compositor_wl_unlend(int row);

static void compositor_wl_row_close(void *context, int row)
{
        (void)context;
        if (row >= 0 && row < WINDOWS)
                compositor_wl.laying[row].open = 0, compositor_wl_unlend(row);
        if (row >= 0 && row < WINDOWS && compositor_wl.source[row].va)
                gpu_release_import(compositor_wl.source[row].va), compositor_wl.source[row].va = 0, compositor_wl.source[row].map = 0;
        compositor_clip_leave(row);
        compositor_popup_leave(row);
        compositor_leave(row);
}

/*
        One pixel of a picture laid over another, both premultiplied: the channels of the one over are kept and the other's let
        through by what it does not cover. The result is opaque, as a window's picture is.
*/
static unsigned int compositor_wl_over(unsigned int over, unsigned int under)
{
        unsigned int through = 255u - (over >> 24), out = 0xff000000u;

        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                unsigned int sum = ((over >> shift) & 255u) + ((((under >> shift) & 255u) * through + 127u) / 255u);

                out |= (sum > 255u ? 255u : sum) << shift;
        }
        return out;
}

/*
        A client's buffer, the rows that changed, into the row's picture. A window's picture is made of one or more of these
        (the window's own buffer and the subsurfaces over it), so they are laid one after another into the buffer of the table
        the compositor will show, and the row is taken once, when the last of them is down, with what they touched as its damage.

        The buffer is written in place, because it is this process that reads it, and the other buffer when the card is reading
        this one (a window that is scanned out by itself), seeded from the one shown unless the first picture covers it. Whatever
        goes into the other buffer is a window the compositor has to compose again from the start.
*/
static void compositor_wl_laid(int row, long x1, long y1, long x2, long y2)
{
        if (compositor_wl.laying[row].touched)
        {
                x1 = x1 < compositor_wl.laying[row].x1 ? x1 : compositor_wl.laying[row].x1;
                y1 = y1 < compositor_wl.laying[row].y1 ? y1 : compositor_wl.laying[row].y1;
                x2 = x2 > compositor_wl.laying[row].x2 ? x2 : compositor_wl.laying[row].x2;
                y2 = y2 > compositor_wl.laying[row].y2 ? y2 : compositor_wl.laying[row].y2;
        }
        compositor_wl.laying[row].touched = 1;
        compositor_wl.laying[row].x1 = (int)x1;
        compositor_wl.laying[row].y1 = (int)y1;
        compositor_wl.laying[row].x2 = (int)x2;
        compositor_wl.laying[row].y2 = (int)y2;
}

static int compositor_wl_dmabuf_ready(long client, int fd);
static int compositor_wl_sync_start(long client, int fd);

/*
        A row's picture the client's own buffer (compositor_wl_lend): the table is not written for it at all. The card reads the buffer's pages
        (a pool is a file, mapped for it as a window's picture is; a dma-buf is imported to the GPU and its address kept), and the processor the
        same pixels, through the pitch given here. It holds while the picture is the row's: a commit that replaces it, a copy the row takes, a
        size the window grows to (compositor_wl_settle) and the row's close each let it go, and what it needed is let go with it (the import).
*/
static int compositor_lent(int row)
{
        return row >= 0 && row < WINDOWS && compositor_wl.lent[row].pixels != 0;
}

static const unsigned int *compositor_lent_pixels(int row, unsigned long *pitch)
{
        if (!compositor_lent(row))
                return 0;
        *pitch = compositor_wl.lent[row].pitch;
        return compositor_wl.lent[row].pixels;
}

// The card's address of a lent picture's first pixel, for a dma-buf (0 for a pool, which the card maps by its pages).
static positive compositor_lent_va(int row)
{
        return compositor_lent(row) ? compositor_wl.lent[row].va : 0;
}

// The lent picture let go of: its import (a dma-buf's) released, and the row's picture the table's again, with nothing in it of the client's.
static void compositor_wl_unlend(int row)
{
        if (!compositor_lent(row))
                return;
        if (compositor_wl.lent[row].import)
                gpu_release_import(compositor_wl.lent[row].import);
        memory_zero(&compositor_wl.lent[row], sizeof compositor_wl.lent[row]);
}

// The lent picture copied into a table buffer, whole, and then let go of: the row's picture is that buffer's from here on. Its size is the
// picture's, which the table's buffers hold (a lend is only for a picture of the table's size at most).
static void compositor_wl_seed(int row, unsigned int which)
{
        unsigned int *to;
        unsigned long sync = 1; // DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ, and then END, as a copy of a dma-buf is bracketed

        if (!compositor_lent(row))
                return;
        to = (unsigned int *)((char *)compositor.table + compositor.pixels + ((unsigned long)row * 2 + (which & 1u)) * compositor.buffer);
        if (compositor_wl.lent[row].fd >= 0)
                compositor_call(syscall(ioctl), (positive)compositor_wl.lent[row].fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&sync, 0, 0, 0);
        if (compositor_wl.lent[row].fd >= 0)
                memory_copy_rect_stream_u32(to, (unsigned long)compositor_stride(), compositor_wl.lent[row].pixels, compositor_wl.lent[row].pitch,
                                            (unsigned long)compositor_wl.lent[row].width, (unsigned long)compositor_wl.lent[row].height);
        else
                memory_copy_rect_u32(to, (unsigned long)compositor_stride(), compositor_wl.lent[row].pixels, compositor_wl.lent[row].pitch,
                                     (unsigned long)compositor_wl.lent[row].width, (unsigned long)compositor_wl.lent[row].height);
        if (compositor_wl.lent[row].fd >= 0)
        {
                sync = 5; // DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ
                compositor_call(syscall(ioctl), (positive)compositor_wl.lent[row].fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&sync, 0, 0, 0);
        }
        compositor_wl_unlend(row);
}

/*
        The lent picture of a row whose window is not the size of the picture (it grew or shrank since the commit, and the compositor is to
        draw the picture stretched, or a picture of another size is the one shown): the picture is copied into the table, which can be
        stretched there, and the row is the table's. A lent picture that the pool was cut under is drawn from the zero pages the SIGBUS
        handler maps over it, and its card maps are let go of, so the card maps the zero pages too.
*/
static void compositor_wl_settle(int row)
{
        if (!compositor_lent(row))
                return;
        if (compositor_wl_cut(compositor_wl.lent[row].pixels))
                gpu_forget(compositor_wl.lent[row].pixels, (positive)compositor_wl.lent[row].height * compositor_wl.lent[row].pitch * 4);
        if (compositor.w[row] != (unsigned int)compositor_wl.lent[row].width || compositor.h[row] != (unsigned int)compositor_wl.lent[row].height)
                compositor_wl_seed(row, compositor.front[row]);
}

// The dma-buf reads of the pictures shown in place are bracketed for a present: started before the frames are built and ended after (the
// kernel's sync, which is what makes a processor read of a dma-buf coherent on the architectures that need it).
static void compositor_wl_lent_sync(int start)
{
        unsigned long sync = start ? 1 : 5; // DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ, then END

        for (int row = 0; row < WINDOWS; row++)
                if (compositor_lent(row) && compositor_wl.lent[row].fd >= 0)
                        compositor_call(syscall(ioctl), (positive)compositor_wl.lent[row].fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&sync, 0, 0, 0);
}

// A present the card drew: counted once when it read a client's picture in place.
static void compositor_wl_count_card(const unsigned char *rows, unsigned int windows)
{
        for (unsigned int at = 0; at < windows && compositor.table; at++)
                if (compositor_lent(rows[at]))
                {
                        compositor.table->client_card++;
                        return;
                }
}

// A picture the card or the processor can read where it is: the whole window's, or the part of its buffer the window is (the picture's
// offset is minus its destination), of the size the window is, from a buffer that holds it. A dma-buf's card address is its import's,
// which is made here, and is refused (no lend) when the card is not up or has no room for it.
static int compositor_wl_lend(int row, const struct wayland_picture *picture, long width, long height)
{
        long x = -picture->dest_x, y = -picture->dest_y;
        unsigned long stride = (unsigned long)picture->stride;
        const unsigned char *at;
        positive va = 0, import = 0;
        int same = compositor_lent(row) && compositor_wl.lent[row].map == picture->map && compositor_wl.lent[row].fd == picture->sync_fd;

        // Only a picture the client says is all changed is the buffer's: a partial damage is a change over what the window showed, which a
        // client that reuses its buffers (Xwayland's root) does not keep whole in the buffer (the copy lays the damage over the window).
        if (!picture->pixels || !picture->map || picture->blend || !picture->last || !(picture->full || !picture->damages) || x < 0 || y < 0 ||
            width <= 0 || height <= 0)
                return 0;
        if (width > (long)compositor_most_width() || height > (long)compositor_most_height() || x + width > picture->width || y + height > picture->height)
                return 0;
        if (stride & 3 || stride < (unsigned long)picture->width * 4)
                return 0;
        at = picture->pixels + (unsigned long)y * stride + (unsigned long)x * 4;
        // The pool's file cut under the read: the window's first and last pixel are read here, as a copy would read them, so a pool cut before
        // this commit is found now (the SIGBUS handler maps zeros over it and marks it), and the picture is refused as a copy is refused.
        {
                volatile unsigned int probe;

                probe = *(const volatile unsigned int *)at;
                probe = probe + *(const volatile unsigned int *)(at + (unsigned long)(height - 1) * stride + (unsigned long)(width - 1) * 4);
                (void)probe;
        }
        if (compositor_wl_cut(picture->pixels))
                return 0;
        if (picture->sync_fd >= 0)
        {
                if (!gpu_ready())
                        return 0;
                // The import is the buffer's, from its start: the same one while the buffer is the same, else a new one.
                import = same ? compositor_wl.lent[row].import : gpu_import_fd((int)picture->sync_fd, (positive)picture->map_bytes);
                if (!import)
                        return 0;
                va = import + (positive)(at - picture->map);
        }
        if (compositor_lent(row) && !same && compositor_wl.lent[row].import)
                gpu_release_import(compositor_wl.lent[row].import);
        compositor_wl.lent[row].pixels = (const unsigned int *)at;
        compositor_wl.lent[row].pitch = stride / 4;
        compositor_wl.lent[row].width = width;
        compositor_wl.lent[row].height = height;
        compositor_wl.lent[row].va = va;
        compositor_wl.lent[row].import = import;
        compositor_wl.lent[row].map = picture->map;
        compositor_wl.lent[row].fd = picture->sync_fd;
        return 1;
}

/*
        A damage rectangle copied by the card instead of the processor: the rectangle sw by sh at (sx, sy) of the client's buffer, into the picture
        being laid at (dx, dy) in the table's buffer to. The card reads the buffer where the processor would: a pool's pages through a read-only map,
        or a dma-buf through its import, which is held for the buffer while it is the one the damage is copied from (compositor_wl.source), and it
        writes the picture's buffer through a written map of it. The job is complete when gpu_copy returns (gpu_run waits), before the caller goes on
        and before the client's buffer is released. Refused, with the processor's copy done instead: a rectangle under
        COMPOSITOR_WL_CARD_COPY_PIXELS, a blend, a card not up or busy, a pool cut under the read (the processor finds it, as before: its zeros are
        refused), and a mapping the kernel refuses.
*/
static int compositor_wl_card_rect(int row, const struct wayland_picture *picture, unsigned int *to, unsigned long pitch,
                                   long dx, long dy, long sx, long sy, long w, long h, int wanted)
{
        unsigned long stride = (unsigned long)picture->stride, extent;
        const unsigned char *src;
        positive src_va, dst_va;
        volatile unsigned int probe;

        if (!wanted || !gpu_ready() || picture->blend || !picture->pixels || !picture->map || picture->map_bytes <= 0 || w <= 0 || h <= 0 || !gpu_idle())
                return 0;
        src = picture->pixels + (unsigned long)sy * stride + (unsigned long)sx * 4;
        extent = 4 * ((stride / 4) * (unsigned long)(h - 1) + (unsigned long)w);
        // The rectangle's first and last pixels are read first: a pool cut under the read raises the SIGBUS the handler answers (zeros), here, so the
        // card is not given the pages of a file that is gone.
        probe = *(const volatile unsigned int *)src;
        probe += *(const volatile unsigned int *)(src + extent - 4);
        (void)probe;
        if (compositor_wl_cut(picture->pixels))
                return 0;
        if (picture->sync_fd < 0)
        {
                src_va = gpu_va_bytes(src, extent);
                if (!src_va && gpu_map(picture->map, (positive)picture->map_bytes))
                        src_va = gpu_va_bytes(src, extent);
                if (!src_va)
                        return 0;
        }
        else
        {
                if (compositor_wl.source[row].map != picture->map)
                {
                        if (compositor_wl.source[row].va)
                                gpu_release_import(compositor_wl.source[row].va);
                        compositor_wl.source[row].va = gpu_import_fd((int)picture->sync_fd, (positive)picture->map_bytes);
                        compositor_wl.source[row].map = compositor_wl.source[row].va ? picture->map : 0;
                        if (!compositor_wl.source[row].va)
                                return 0;
                }
                src_va = compositor_wl.source[row].va + (positive)(src - picture->map);
        }
        dst_va = gpu_va_written(to, compositor.buffer);
        if (!dst_va && gpu_map_written(to, compositor.buffer))
                dst_va = gpu_va_written(to, compositor.buffer);
        if (!dst_va)
                return 0;
        return gpu_copy(src_va, (unsigned int)(stride / 4), dst_va + 4 * ((positive)dy * pitch + (positive)dx), (unsigned int)pitch,
                        (unsigned int)w, (unsigned int)h);
}

static int compositor_wl_row_picture(void *context, int row, const struct wayland_picture *picture)
{
        struct windows *table = compositor.table;
        unsigned long pitch = (unsigned long)compositor_stride();
        long width = picture->row_width, height = picture->row_height;
        long count = picture->full || !picture->damages ? 1 : picture->damages;
        unsigned int *to;
        unsigned long sync = 1; // DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ, and then END

        (void)context;
        if (row < 0 || row >= WINDOWS || !table)
                return -1;
        if (picture->sync_fd >= 0 && picture->pixels)
        {
                if (!compositor_wl_dmabuf_ready(wayland_row_owner(row), (int)picture->sync_fd) ||
                    !compositor_wl_sync_start(wayland_row_owner(row), (int)picture->sync_fd))
                        return 1;
        }
        width = width < 0 ? 0 : width > (long)compositor_most_width() ? (long)compositor_most_width() : width;
        height = height < 0 ? 0 : height > (long)compositor_most_height() ? (long)compositor_most_height() : height;
        // A picture that is a whole window's, from one buffer, is read where it is (compositor_wl_lend): nothing is copied, and the window is
        // drawn whole from it. Its dma-buf's reads are bracketed as a copy's are (the writer waited for above), and ended here.
        if (picture->pixels && picture->last && !compositor_wl.laying[row].open && compositor_wl_lend(row, picture, width, height))
        {
                unsigned long sync = 5; // DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ: nothing is read here, the card and the processor read it later

                if (picture->sync_fd >= 0)
                        compositor_call(syscall(ioctl), (positive)picture->sync_fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&sync, 0, 0, 0);
                table->width[row] = (unsigned int)width;
                table->height[row] = (unsigned int)height;
                table->damage_x[row] = table->damage_y[row] = table->damage_width[row] = table->damage_height[row] = 0; // the whole picture
                table->client_in_place++;
                __atomic_store_n(&table->frame[row], table->frame[row] + 1, __ATOMIC_RELEASE);
                compositor_take(row);
                return WAYLAND_PICTURE_LENT;
        }
        if (!compositor_wl.laying[row].open)
        {
                unsigned int shown = compositor.showing[row] ? compositor.front[row] & 1u : 0;
                int pinned = compositor.showing[row] && (table->pinned[row] || compositor.direct_row == row);
                int covers = picture->pixels && !picture->blend && !picture->dest_x && !picture->dest_y && picture->width >= width &&
                             picture->height >= height && (picture->full || !picture->damages);

                compositor_wl.laying[row].open = 1;
                compositor_wl.laying[row].touched = 0;
                compositor_wl.laying[row].full = (unsigned char)pinned;
                compositor_wl.laying[row].which = (unsigned char)(shown ^ (unsigned int)pinned);
                if (pinned && !covers)
                        memory_copy_rect_u32((unsigned int *)((char *)table + compositor.pixels +
                                                              ((unsigned long)row * 2 + compositor_wl.laying[row].which) * compositor.buffer),
                                             pitch,
                                             (const unsigned int *)((char *)table + compositor.pixels +
                                                                    ((unsigned long)row * 2 + shown) * compositor.buffer),
                                             pitch, (unsigned long)width, (unsigned long)height);
                // A row that was shown in place: the table's buffer is made the picture's first (a copy of the whole of it, unless this picture
                // covers the window), so the copies below lay over what the window showed, and the window is drawn whole.
                if (compositor_lent(row))
                {
                        if (!covers)
                                compositor_wl_seed(row, compositor_wl.laying[row].which);
                        else
                                compositor_wl_unlend(row);
                        compositor_wl.laying[row].full = 1;
                }
        }
        to = (unsigned int *)((char *)table + compositor.pixels + ((unsigned long)row * 2 + compositor_wl.laying[row].which) * compositor.buffer);
        if (picture->pixels)
        {
                long summed = 0;
                int card_wanted;

                if (count > WAYLAND_DAMAGE)
                        count = WAYLAND_DAMAGE;
                // The commit's damage, summed: the card copies all of it or none (COMPOSITOR_WL_CARD_COPY_PIXELS)
                for (long at = 0; at < count; at++)
                        summed += picture->full || !picture->damages ? (long)picture->width * (long)picture->height
                                                                   : (long)picture->damage[at].width * (long)picture->damage[at].height;
                card_wanted = summed >= COMPOSITOR_WL_CARD_COPY_PIXELS;
                for (long at = 0; at < count; at++)
                {
                        long left = 0, top = 0, right = picture->width, bottom = picture->height;
                        long x = picture->dest_x, y = picture->dest_y;

                        if (!picture->full && picture->damages)
                        {
                                left = picture->damage[at].x;
                                top = picture->damage[at].y;
                                right = left + picture->damage[at].width;
                                bottom = top + picture->damage[at].height;
                        }
                        // Within the buffer, then within the window, with the source moving as the place does.
                        left = left < 0 ? 0 : left;
                        top = top < 0 ? 0 : top;
                        right = right > picture->width ? picture->width : right;
                        bottom = bottom > picture->height ? picture->height : bottom;
                        if (x + left < 0)
                                left = -x;
                        if (y + top < 0)
                                top = -y;
                        if (x + right > width)
                                right = width - x;
                        if (y + bottom > height)
                                bottom = height - y;
                        if (right <= left || bottom <= top)
                                continue;
                        if (picture->blend)
                                for (long r = top; r < bottom; r++)
                                {
                                        const unsigned int *from = (const unsigned int *)(picture->pixels + r * picture->stride);
                                        unsigned int *into = to + (unsigned long)(y + r) * pitch + (unsigned long)x;

                                        for (long c = left; c < right; c++)
                                                into[c] = compositor_wl_over(from[c], into[c]);
                                }
                        else if (compositor_wl_card_rect(row, picture, to, pitch, x + left, y + top, left, top, right - left, bottom - top, card_wanted))
                                table->client_rects_card++;
                        else if (picture->sync_fd >= 0)
                        {
                                table->client_rects_cpu++;
                                // A GPU's buffer, mapped write-combined: read with the streaming loads that are nine times as fast.
                                memory_copy_rect_stream_u32(to + (unsigned long)(y + top) * pitch + (unsigned long)(x + left), pitch,
                                                            (const unsigned int *)(picture->pixels + top * picture->stride) + left,
                                                            (unsigned long)picture->stride / 4, (unsigned long)(right - left),
                                                            (unsigned long)(bottom - top));
                        }
                        else
                        {
                                table->client_rects_cpu++;
                                memory_copy_rect_u32(to + (unsigned long)(y + top) * pitch + (unsigned long)(x + left), pitch,
                                                     (const unsigned int *)(picture->pixels + top * picture->stride) + left,
                                                     (unsigned long)picture->stride / 4, (unsigned long)(right - left),
                                                     (unsigned long)(bottom - top));
                        }
                        compositor_wl_laid(row, x + left, y + top, x + right, y + bottom);
                }
                if (picture->sync_fd >= 0)
                {
                        sync = 5; // DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ
                        compositor_call(syscall(ioctl), (positive)picture->sync_fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&sync, 0, 0, 0);
                }
                if (compositor_wl_cut(picture->pixels))
                {
                        compositor_wl.laying[row].open = 0;
                        return -1;
                }
        }
        if (!picture->last)
                return 0;
        table->client_copied++;
        if (width != (long)table->width[row] || height != (long)table->height[row] || !compositor_wl.laying[row].touched)
                compositor_wl.laying[row].full = 1;
        table->width[row] = (unsigned int)width;
        table->height[row] = (unsigned int)height;
        table->buffer_index[row] = compositor_wl.laying[row].which;
        table->damage_x[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].x1;
        table->damage_y[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].y1;
        table->damage_width[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].x2 - compositor_wl.laying[row].x1;
        table->damage_height[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].y2 - compositor_wl.laying[row].y1;
        compositor_wl.laying[row].open = 0;
        __atomic_store_n(&table->frame[row], table->frame[row] + 1, __ATOMIC_RELEASE);
        compositor_take(row);
        return 0;
}

// A title into the table, as a program writes one: cut to the room on a character's boundary.
static void compositor_wl_row_title(void *context, int row, const char *text, long length)
{
        struct windows *table = compositor.table;
        unsigned int used = 0, at;

        (void)context;
        (void)length;
        while (text[used])
                used++;
        at = used < WINDOWS_TITLE - 1 ? used : WINDOWS_TITLE - 1;
        // Not in the middle of a character: back to the byte that starts one.
        while (at && at < used && ((unsigned char)text[at] & 0xc0) == 0x80)
                at--;
        for (unsigned int index = 0; index < WINDOWS_TITLE; index++)
                table->title[row][index] = index < at ? text[index] : 0;
        compositor_take(row);
}

// The cursor_asked value for a pointer the client hid (set_cursor with no surface).
#define COMPOSITOR_WL_HIDDEN 0xffu

// The compositor's shape for each WAYLAND_CURSOR_* code (wayland.c), in order.
static const unsigned char compositor_wl_cursor_of[11] = {CP_ARROW, CP_TEXT, CP_CROSS, CP_MOVE, CP_RESIZE_H, CP_RESIZE_V, CP_RESIZE_NWSE,
                                                          CP_RESIZE_NESW, CP_HAND, CP_WAIT, CP_NOTALLOWED};

// The shape a window's client asked for, as a compositor shape: -1 for none (the arrow), -2 for no pointer at all (compositor_motion
// asks inside the window).
static int compositor_wl_asked_shape(int row)
{
        if (row < 0 || row >= WINDOWS || !compositor_wl.cursor_asked[row])
                return -1;
        return compositor_wl.cursor_asked[row] == COMPOSITOR_WL_HIDDEN ? -2 : compositor_wl.cursor_asked[row] - 1;
}

/*
        A text field of a window's client has the keys (delta 1), gives them back (-1), or says its content purpose (0): the window
        table's count of focused fields, which the on-screen keyboard shows by (ui_field_focus's count, so one place), and the purpose
        the keyboard's page is picked by. A purpose is kept while the count is held and is none when it goes.
*/
static void compositor_wl_field(int row, long delta, long purpose)
{
        unsigned int *count = &compositor.table->field[row];

        if (delta > 0)
                (*count)++;
        else if (delta < 0 && *count)
                (*count)--;
        compositor.text_purpose[row] = *count && purpose >= 0 && purpose <= 13 ? (unsigned char)purpose : 0;
        compositor_osk_sync();
}

/*
        The bottom of a text cursor, in the window's pixels (0 for none). A cursor below the window's bottom is the bottom; the keyboard
        is asked again, and a window already moved for an earlier cursor is moved back first, so it is moved for this one.
*/
static void compositor_wl_caret(int row, long bottom)
{
        int want = bottom > 0 ? (int)(bottom < WAYLAND_DIMENSION ? bottom : WAYLAND_DIMENSION) : 0;

        if (compositor.caret[row] == want)
                return;
        compositor.caret[row] = want;
        if (compositor.osk_pan[row] || compositor.osk_height[row])
                compositor_osk_unpan(row);
        compositor_osk_sync();
}

static void compositor_wl_row_do(void *context, int row, long what, long a, long b)
{
        (void)context;
        if (row < 0 || row >= WINDOWS)
                return;
        // A text field's count is kept whether or not the window has shown a picture yet: the shim counts it from the enable, and
        // gives it back when the window goes, which is before the row closes.
        if (what == WAYLAND_DO_TEXT)
        {
                compositor_wl_field(row, a, b);
                return;
        }
        if (what == WAYLAND_DO_CARET)
        {
                compositor_wl_caret(row, a);
                return;
        }
        if (!compositor.showing[row])
                return;
        switch (what)
        {
        case WAYLAND_DO_MINIMIZE:
                compositor_minimize_row(row);
                break;
        case WAYLAND_DO_MAXIMIZE:
                compositor_arrange(row, COMPOSITOR_ROOM);
                break;
        case WAYLAND_DO_FULLSCREEN:
                compositor_fullscreen_row(row);
                break;
        case WAYLAND_DO_UNMAXIMIZE:
        case WAYLAND_DO_UNFULLSCREEN:
                compositor_float(row);
                break;
        case WAYLAND_DO_CLOSE:
                compositor_close(row);
                break;
        case WAYLAND_DO_FOCUS:
                compositor_raise(row);
                compositor_focus(row);
                break;
        case WAYLAND_DO_PINCH_WANT:
                compositor.table->pinch_wanted[row] = a ? 1u : 0u;
                break;
        case WAYLAND_DO_CURSOR:
                // The shape the window's client asked for over the pointer: its CP_ shape, stored +1 (0 for none).
                compositor_wl.cursor_asked[row] = (unsigned char)(a == WAYLAND_CURSOR_HIDDEN ? COMPOSITOR_WL_HIDDEN
                                                                  : a < 0 || a > WAYLAND_CURSOR_NOTALLOWED ? 0 : compositor_wl_cursor_of[a] + 1);
                break;
        case WAYLAND_DO_MOVE:
                // Only while the person holds the left button on this window: a client cannot take the pointer.
                if ((compositor.button_down & 1u) && compositor.button_row[0] == row && compositor.drag < 0 && compositor.resize < 0)
                        compositor_drag_begin(row);
                break;
        case WAYLAND_DO_RESIZE:
        {
                // The protocol's edges (top 1, bottom 2, left 4, right 8) are the compositor's own bits turned about.
                unsigned int edges = ((a & 1) ? COMPOSITOR_EDGE_TOP : 0) | ((a & 2) ? COMPOSITOR_EDGE_BOTTOM : 0) |
                                     ((a & 4) ? COMPOSITOR_EDGE_LEFT : 0) | ((a & 8) ? COMPOSITOR_EDGE_RIGHT : 0);

                if (edges && (compositor.button_down & 1u) && compositor.button_row[0] == row && compositor.drag < 0 && compositor.resize < 0)
                {
                        compositor_resize_begin(row, edges);
                        compositor_lit(row, edges, 1);
                }
                break;
        }
        default:
                break;
        }
}

/* What the compositor says of a row, in the shim's words. */
static int compositor_wl_row_facts(void *context, int row, struct wayland_facts *facts)
{
        struct windows *table = compositor.table;
        long states = 0;

        (void)context;
        if (row < 0 || row >= WINDOWS || !compositor.showing[row])
                return -1;
        facts->width = facts->height = 0;
        if (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row])
        {
                facts->width = (long)table->want_width[row];
                facts->height = (long)table->want_height[row];
        }
        if (compositor.arranged[row] == COMPOSITOR_ROOM)
                states |= WAYLAND_STATE_MAXIMIZED;
        else if (compositor.arranged[row] == COMPOSITOR_MAXIMIZED)
                states |= WAYLAND_STATE_FULLSCREEN;
        else if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                states |= WAYLAND_STATE_TILED_LEFT | WAYLAND_STATE_TILED_RIGHT | WAYLAND_STATE_TILED_TOP | WAYLAND_STATE_TILED_BOTTOM;
        if (compositor.focus == row)
                states |= WAYLAND_STATE_ACTIVATED;
        if (compositor.resize == row)
                states |= WAYLAND_STATE_RESIZING;
        if (compositor.state[row] & WINDOW_MINIMIZED)
                states |= WAYLAND_STATE_SUSPENDED;
        facts->states = states;
        facts->closing = (compositor.state[row] & WINDOW_CLOSING) != 0;
        facts->x = compositor.x[row];
        facts->y = compositor.y[row];
        facts->scale = compositor_row_scale(row);
        return 0;
}

static long compositor_wl_screens(void *context, struct wayland_screen *screens, long room)
{
        long count = (long)compositor_output_count();

        (void)context;
        count = count > room ? room : count;
        for (long at = 0; at < count; at++)
        {
                const char *name = compositor_output_name((int)at);
                int x, y, width, height;

                compositor_output_rect((int)at, &x, &y, &width, &height);
                screens[at].x = x;
                screens[at].y = y;
                screens[at].width = width;
                screens[at].height = height;
                screens[at].scale = compositor_output_scale((int)at) > 0 ? compositor_output_scale((int)at) : compositor.scale;
                screens[at].refresh = compositor.period ? (long)(1000000000000ull / compositor.period) : 60000;
                screens[at].physical_width = screens[at].physical_height = 0;
                for (long index = 0; index < 16; index++)
                        screens[at].name[index] = 0;
                for (long index = 0; name[index] && index < 15; index++)
                        screens[at].name[index] = name[index];
                if (!screens[at].name[0])
                {
                        screens[at].name[0] = 'C';
                        screens[at].name[1] = 'a';
                        screens[at].name[2] = 'n';
                        screens[at].name[3] = 'v';
                        screens[at].name[4] = 'a';
                        screens[at].name[5] = 's';
                }
        }
        return count;
}

/*
        What the screens are, as one number: their rectangles, scales, rates and names, in order. A scale, a mode, a place or a
        turn that changes any of it changes this, and the clients are told the screens again (compositor_wl_wake).
*/
static unsigned long compositor_wl_screens_digest(void)
{
        struct wayland_screen screens[WAYLAND_SCREENS];
        long count = compositor_wl_screens(0, screens, WAYLAND_SCREENS);
        unsigned long hash = 14695981039346656037ul;

        for (long at = 0; at < count; at++)
        {
                const long values[] = {screens[at].x, screens[at].y, screens[at].width, screens[at].height, screens[at].scale,
                                       screens[at].refresh, screens[at].physical_width, screens[at].physical_height};

                for (unsigned long each = 0; each < sizeof values / sizeof values[0]; each++)
                        hash = (hash ^ (unsigned long)values[each]) * 1099511628211ul;
                for (long letter = 0; letter < 16 && screens[at].name[letter]; letter++)
                        hash = (hash ^ (unsigned char)screens[at].name[letter]) * 1099511628211ul;
                hash = (hash ^ 0x100) * 1099511628211ul;
        }
        return hash ^ (unsigned long)count;
}

static long compositor_wl_now(void *context)
{
        (void)context;
        return (long)(clock_monotonic_nanoseconds() / 1000000ull);
}

/*
        The keymap of the layout in use, as a sealed file the shim hands out beside the keyboard's event: made the first time,
        written whole and sealed against any change, and the same file for every client after. Clients map it private and
        read-only, as xkb's libraries do.
*/
static long compositor_wl_keymap(void *context, long *size)
{
        static char text[COMPOSITOR_WL_KEYMAP_ROOM];
        unsigned int layout = compositor.keymap.layout;
        unsigned long length, done = 0;
        long fd;

        (void)context;
        if (layout >= 16)
                return -1;
        if (compositor_wl.keymap[layout] < 0)
        {
                length = compositor_wl_keymap_text(layout, text, sizeof text);
                if (!length)
                        return -1;
                // MFD_CLOEXEC | MFD_ALLOW_SEALING
                fd = compositor_call(syscall(memfd_create), (positive) "moonwater-keymap", 3, 0, 0, 0, 0);
                if (fd < 0)
                        return -1;
                while (done < length)
                {
                        long put = compositor_call(syscall(write), (positive)fd, (positive)(text + done), length - done, 0, 0, 0);

                        if (put <= 0)
                        {
                                system_close((int)fd);
                                return -1;
                        }
                        done += (unsigned long)put;
                }
                // F_ADD_SEALS: SEAL, SHRINK, GROW and WRITE
                compositor_call(syscall(fcntl), (positive)fd, 1033, 15, 0, 0, 0);
                compositor_wl.keymap[layout] = (int)fd;
                compositor_wl.keymap_size[layout] = length;
        }
        *size = (long)compositor_wl.keymap_size[layout];
        return compositor_wl.keymap[layout];
}

// How fast a held key repeats, as the settings keep it, or as the keyboard has it: the delay in milliseconds and the rate a second.
static void compositor_wl_repeat(void *context, long *rate, long *delay)
{
        long own_delay = 250, own_period = 33;

        (void)context;
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
                if (compositor.repeats[at])
                {
                        own_delay = compositor.repeat_own[at][0] > 0 ? compositor.repeat_own[at][0] : own_delay;
                        own_period = compositor.repeat_own[at][1] > 0 ? compositor.repeat_own[at][1] : own_period;
                        break;
                }
        *delay = (compositor.repeat_kept & 1) ? compositor.repeat_delay : own_delay;
        *rate = (compositor.repeat_kept & 2) ? compositor.repeat_rate : 1000 / own_period;
        if (*delay < 0 || *delay > 10000)
                *delay = 250;
        if (*rate < 0 || *rate > 1000)
                *rate = 30;
}

// What the seat can do: a pointer and a keyboard; touch is not offered yet.
// A touch capability while a touch screen is on the machine (the screens' fingers are sent to the windows as wl_touch).
// Random bytes for the xdg-activation tokens: the kernel's, from the pool as it is (a token is no secret of a key's size).
static int compositor_wl_random(void *context, unsigned char *into, long bytes)
{
        (void)context;
        return system_random_fill(into, (positive)bytes, 0) == 0;
}

static long compositor_wl_seat(void *context)
{
        long caps = WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard;

        (void)context;
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
                if (compositor.touchscreen[at])
                        caps |= WLN_wl_seat_capability_touch;
        return caps;
}

// Caps Lock, as the keymap's own Lock mask.
static long compositor_wl_locks(void *context)
{
        (void)context;
        return compositor.keymap.caps ? 2 : 0;
}

/* ---- games: the pointer held by a client's window ---- */

/*
        A client's window holds the pointer when the shim says so (a lock or a confinement, which the shim ends the moment the window
        is not under the pointer or does not have the keys, so the person can always leave). Locked, the hand's counts are the
        client's as they come, unaccelerated, and the pointer is neither moved nor drawn (it is drawn again where it stopped when the
        lock ends); confined, the pointer moves as ever and is kept inside the window.
*/
static void compositor_wl_pointer_hold(void *context, long row, long mode)
{
        (void)context;
        compositor_wl.hold_row = (int)row;
        compositor_wl.hold = (int)mode;
        compositor.touch.hidden = mode == 1;
        compositor_cursor_move();
}

// The hand's counts, to the window that holds the pointer locked; 1 when they went there and the pointer is not to move.
static int compositor_wl_hold_counts(int dx, int dy, positive now)
{
        if (compositor_wl.hold != 1 || compositor_wl.hold_row < 0)
                return 0;
        wayland_pointer_relative(compositor_wl.hold_row, dx, dy, (long)(now / 1000));
        return 1;
}

// A pointer confined to a window, moved, is put back inside it.
static void compositor_wl_hold_keep(void)
{
        int row = compositor_wl.hold_row;

        if (compositor_wl.hold != 2 || row < 0)
                return;
        if (compositor.real_x < compositor.x[row])
                compositor.real_x = compositor.x[row];
        if (compositor.real_x > compositor.x[row] + (int)compositor.w[row] - 1)
                compositor.real_x = compositor.x[row] + (int)compositor.w[row] - 1;
        if (compositor.real_y < compositor.y[row])
                compositor.real_y = compositor.y[row];
        if (compositor.real_y > compositor.y[row] + (int)compositor.h[row] - 1)
                compositor.real_y = compositor.y[row] + (int)compositor.h[row] - 1;
}

/* ---- dma-bufs ---- */

/*
        What a client that draws with the GPU is told it may hand over: linear ARGB8888 and XRGB8888 on the render node of the
        card the compositor shows on (or, with no card, the first render node there is), because what is composed from a buffer
        here is read by the processor, which can read linear memory and no tiled or compressed kind. A buffer the processor
        cannot wait for is skipped, and one client that makes it wait is not waited for again for a while.
*/
static const struct
{
        unsigned int format;
        unsigned long modifier;
} compositor_wl_dmabuf_entry[2] = {{COMPOSITOR_WL_FOURCC_ARGB8888, 0}, {COMPOSITOR_WL_FOURCC_XRGB8888, 0}};

// A device number as the kernel's stat gives it (the C library's dev_t).
static unsigned long compositor_wl_dev(unsigned long major, unsigned long minor)
{
        return ((major & 0xfffUL) << 8) | (minor & 0xffUL) | ((minor & ~0xffUL) << 12) | ((major & ~0xfffUL) << 32);
}

// The device number of a node (a statx of its path), 0 for none.
static unsigned long compositor_wl_node(const char *path)
{
        unsigned char info[256];

        memory_zero(info, sizeof info);
        // struct statx: stx_rdev_major at 128 and stx_rdev_minor at 132 (u32 each).
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)path, 0, 0x7ff, (positive)info, 0) < 0)
                return 0;
        return compositor_wl_dev(*(unsigned int *)(info + 128), *(unsigned int *)(info + 132));
}

// A decimal number appended to a path.
static char *compositor_wl_decimal(char *at, unsigned long value)
{
        char digits[24];
        unsigned int count = 0;

        do
                digits[count++] = (char)('0' + value % 10);
        while ((value /= 10) && count < sizeof digits);
        while (count)
                *at++ = digits[--count];
        return at;
}

static char *compositor_wl_text(char *at, const char *text)
{
        while (*text)
                *at++ = *text++;
        return at;
}

/*
        The render node that goes with the card: the node next to it in sysfs (its device's drm directory holds both), else the first
        render node the machine has. 0 when there is none.
*/
static unsigned long compositor_wl_render_node(void)
{
        unsigned char info[256];
        char path[96], *tail;
        unsigned long device;

        if (compositor.card >= 0)
        {
                memory_zero(info, sizeof info);
                // AT_EMPTY_PATH: the card's own descriptor.
                if (compositor_call(syscall(statx), (positive)compositor.card, (positive) "", 0x1000, 0x7ff, (positive)info, 0) >= 0)
                        for (unsigned long number = 128; number < 192; number++)
                        {
                                tail = compositor_wl_text(path, "/sys/dev/char/");
                                tail = compositor_wl_decimal(tail, *(unsigned int *)(info + 128));
                                *tail++ = ':';
                                tail = compositor_wl_decimal(tail, *(unsigned int *)(info + 132));
                                tail = compositor_wl_text(tail, "/../renderD");
                                tail = compositor_wl_decimal(tail, number);
                                *tail = 0;
                                if (compositor_call(syscall(faccessat), (positive)AT_FDCWD, (positive)path, 0, 0, 0, 0) < 0)
                                        continue;
                                tail = compositor_wl_text(path, "/dev/dri/renderD");
                                tail = compositor_wl_decimal(tail, number);
                                *tail = 0;
                                device = compositor_wl_node(path);
                                if (device)
                                        return device;
                        }
        }
        for (unsigned long number = 128; number < 192; number++)
        {
                tail = compositor_wl_text(path, "/dev/dri/renderD");
                tail = compositor_wl_decimal(tail, number);
                *tail = 0;
                device = compositor_wl_node(path);
                if (device)
                        return device;
        }
        return 0;
}

static long compositor_wl_dmabuf_info(void *context, struct wayland_dmabuf_info *info)
{
        (void)context;
        if (!compositor_wl.render)
        {
                unsigned long found = compositor_wl_render_node();

                compositor_wl.render = found ? found : 1;
        }
        if (compositor_wl.render == 1)
                return 0;
        info->device = compositor_wl.render;
        info->count = 2;
        for (long at = 0; at < 2; at++)
        {
                info->entry[at].format = compositor_wl_dmabuf_entry[at].format;
                info->entry[at].modifier = compositor_wl_dmabuf_entry[at].modifier;
        }
        info->tranches = 1;
        info->tranche[0].device = info->device;
        info->tranche[0].flags = 0;
        info->tranche[0].count = 2;
        info->tranche[0].index[0] = 0;
        info->tranche[0].index[1] = 1;
        return 1;
}

// The table the feedback names the pairs by: a sealed file of entries of a format, four bytes of padding and a modifier.
static long compositor_wl_dmabuf_table(void *context, long *size)
{
        unsigned char bytes[32];
        long fd, done = 0;

        (void)context;
        if (compositor_wl.dmabuf_table <= 0)
        {
                memory_zero(bytes, sizeof bytes);
                for (long at = 0; at < 2; at++)
                        for (long byte = 0; byte < 4; byte++)
                                bytes[at * 16 + byte] = (unsigned char)(compositor_wl_dmabuf_entry[at].format >> (8 * byte));
                // MFD_CLOEXEC | MFD_ALLOW_SEALING
                fd = compositor_call(syscall(memfd_create), (positive) "moonwater-dmabuf-table", 3, 0, 0, 0, 0);
                if (fd < 0)
                        return -1;
                while (done < (long)sizeof bytes)
                {
                        long put = compositor_call(syscall(write), (positive)fd, (positive)(bytes + done), sizeof bytes - (unsigned long)done, 0, 0, 0);

                        if (put <= 0)
                        {
                                system_close((int)fd);
                                return -1;
                        }
                        done += put;
                }
                compositor_call(syscall(fcntl), (positive)fd, 1033, 15, 0, 0, 0); // F_ADD_SEALS: all four
                compositor_wl.dmabuf_table = (int)fd;
        }
        *size = 32;
        return compositor_wl.dmabuf_table;
}

/*
        A descriptor that is a dma-buf of at least size bytes, mapped read-only and shared, in the table the SIGBUS handler reads, like a pool.
        A dma-buf is told from any other file by the one ioctl that gives it the answer EINVAL (a sync with no direction), where another
        file says it knows no such ioctl; its size is what seeking to its end gives.
*/
static void *compositor_wl_dmabuf_map(void *context, long fd, long size)
{
        unsigned long none = 0;
        long extent, got;

        (void)context;
        if (size < 1 || compositor_call(syscall(ioctl), (positive)fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&none, 0, 0, 0) != -22)
                return 0;
        extent = compositor_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0);
        if (extent < size)
                return 0;
        got = compositor_call(syscall(mmap), 0, (positive)size, 1, 1, (positive)fd, 0);
        if (got < 0 && got > -4096)
                return 0;
        for (long at = 0; at < WAYLAND_CLIENTS * WAYLAND_POOLS; at++)
                if (!compositor_wl.map[at].at)
                {
                        compositor_wl.map[at].bad = 0;
                        compositor_wl.map[at].size = (unsigned long)size;
                        compositor_wl.map[at].at = (unsigned long)got;
                        return (void *)got;
                }
        compositor_call(syscall(munmap), (positive)got, (positive)size, 0, 0, 0, 0);
        return 0;
}

/*
        Whether a dma-buf has nobody writing it, for a client's picture: the descriptor says it can be read when the GPU is done. A short
        wait for one that is not; if it is not done then, this picture is not read and its client is not waited for again for a while,
        so a buffer that never finishes cannot hold the compositor more than once in two seconds.
*/
static int compositor_wl_dmabuf_ready(long client, int fd)
{
        struct compositor_poll watch = {fd, COMPOSITOR_POLLIN, 0};
        positive now = clock_monotonic_nanoseconds(), wait[2] = {0, 0};

        if (client < 0 || client >= WAYLAND_CLIENTS || now >= compositor_wl.slow_until[client])
                wait[1] = COMPOSITOR_WL_DMABUF_WAIT_NS;
        compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 0, 0);
        if (watch.returned & COMPOSITOR_POLLIN)
                return 1;
        if (client >= 0 && client < WAYLAND_CLIENTS)
                compositor_wl.slow_until[client] = now + COMPOSITOR_WL_DMABUF_SLOW_NS;
        return 0;
}

/*
        The sync that begins a read of a dma-buf (DMA_BUF_SYNC_START | READ). It waits for the GPU work the buffer was given, and the
        poll above says only that it had none a moment before: work its client gives it in between is waited for, however long it runs
        (measured on the box's amdgpu: the ioctl returned when the owner's shader was done, 150 ms for one of 5000 steps a pixel, 600 ms
        for 20000, and up to the GPU's job timeout would do). The wait is interruptible, so a timer of COMPOSITOR_WL_DMABUF_WAIT_NS that
        ends it (a SIGALRM that does nothing, taken without SA_RESTART: EINTR at 30.0 ms in the same measure) bounds it, and a read
        whose sync was cut is not made: the picture is skipped as one whose writer was not done, and its client is not waited for again
        for a while. True when the read may be made.
*/
static void compositor_wl_alarm(int number)
{
        (void)number;
}

static int compositor_wl_sync_start(long client, int fd)
{
        unsigned long sync = 1; // DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ
        positive timer[4] = {0, 0, 0, COMPOSITOR_WL_DMABUF_WAIT_NS / 1000}, off[4] = {0, 0, 0, 0}; // struct itimerval, in us
        long got;

        compositor_call(syscall(setitimer), 0, (positive)timer, 0, 0, 0, 0); // ITIMER_REAL
        got = compositor_call(syscall(ioctl), (positive)fd, COMPOSITOR_WL_DMABUF_SYNC, (positive)&sync, 0, 0, 0);
        compositor_call(syscall(setitimer), 0, (positive)off, 0, 0, 0, 0);
        if (got != -4) // EINTR: the timer ended the wait; anything else is read as it always was
                return 1;
        if (client >= 0 && client < WAYLAND_CLIENTS)
                compositor_wl.slow_until[client] = clock_monotonic_nanoseconds() + COMPOSITOR_WL_DMABUF_SLOW_NS;
        return 0;
}

/* ---- the clipboard ---- */

/*
        The shim's copies and pastes go through the compositor's own clipboard (compositor_clip_*), text only, under its rules.
        A copy is a client's source whose text the compositor reads itself: it makes a pipe, hands the shim the write end for
        the source's send event, and gathers what comes into a file of its own, up to the clipboard's limit and for a few seconds;
        when the source ends the file is sealed, checked (UTF-8, no NUL, not empty) as any copy is, and is the selection, with the
        window the person touched as its owner. A paste is the text written to the descriptor the client gave, a pipe or a file,
        a page at a time as the pipe says it can take it, for a few seconds. Both are bounded: sixteen at a time, four a
        client. A copy is taken only for a client the person has just touched, and a paste given only to the window that has the
        keys and has just had one, which is the rule a program of the compositor's has.

        The reads and writes of a transfer are done on a thread of its own and never on the compositor's loop. A read or a write
        of a pipe takes the pipe's lock before it looks at O_NONBLOCK, and a program that has an end of the pipe can keep that lock
        for as long as it likes, so one call of the loop that met such a pipe held every window and the person's input with it
        (b0e0b1ce). The thread waits with poll, which takes no lock, a slice at a time, stops at the deadline or when the loop
        cancels it, says how it ended and knocks on the loop's eventfd; the loop then lets the transfer go or makes its text the
        selection (compositor_wl_transfers_done). Nothing closes a transfer's descriptors while its thread runs, so a source or a
        reader that stalls costs its place and its thread until its time is up, and one that holds a lock costs them until it
        lets go: the loop goes on either way.
*/
static void compositor_wl_transfer_free(long at)
{
        if (compositor_wl.transfer[at].in >= 0)
                system_close(compositor_wl.transfer[at].in);
        if (compositor_wl.transfer[at].out >= 0)
                system_close(compositor_wl.transfer[at].out);
        if (compositor_wl.transfer[at].keep >= 0)
                system_close(compositor_wl.transfer[at].keep);
        compositor_wl.transfer[at].state = 0;
        compositor_wl.transfer[at].in = compositor_wl.transfer[at].out = compositor_wl.transfer[at].keep = -1;
}

// The bytes of a selection the compositor holds, 0 for none or while the service is off.
static long compositor_wl_clip_size(void *context, long which)
{
        struct windows *table = compositor.table;

        (void)context;
        return table && which >= 0 && which < 2 && table->clip_on && compositor_clip.fd[which] >= 0 ? (long)table->clip_length[which] : 0;
}

// The transfers a client has, and a free place for another (-1 when it has its share or there is none).
static long compositor_wl_transfer_place(long client)
{
        long slot = -1, held = 0;

        for (long at = 0; at < COMPOSITOR_WL_TRANSFERS; at++)
        {
                if (!compositor_wl.transfer[at].state)
                        slot = slot < 0 ? at : slot;
                else if (compositor_wl.transfer[at].client == client)
                        held++;
        }
        return held >= COMPOSITOR_WL_TRANSFERS_PER_CLIENT ? -1 : slot;
}

#define COMPOSITOR_WL_SLICE_NS 50000000ull // how long a transfer's thread waits at a time before it looks at its deadline and cancel

/*
        A transfer's thread: a copy read from its pipe into its file until the source ends it (well), or fails, is too long, is
        out of time or is cancelled (badly); a paste written to the client's descriptor a page at a time until all of it is (well)
        or the reader goes, fails, is out of time or is cancelled (badly).
*/
static void compositor_wl_transfer_run(address_any context)
{
        long at = (long)context;
        unsigned char bytes[65536];
        int pull = compositor_wl.transfer[at].state == COMPOSITOR_WL_PULL, result = 1;
        int fd = pull ? compositor_wl.transfer[at].in : compositor_wl.transfer[at].out, from = compositor_wl.transfer[at].in;
        int keep = compositor_wl.transfer[at].keep;
        unsigned long done = 0, total = compositor_wl.transfer[at].total, limit = compositor_wl.transfer[at].limit, one = 1;
        positive deadline = compositor_wl.transfer[at].deadline;

        while (pull || done < total)
        {
                struct compositor_poll watch = {fd, pull ? COMPOSITOR_POLLIN : COMPOSITOR_POLLOUT, 0};
                positive now = clock_monotonic_nanoseconds(), wait[2] = {0, 0};
                long got, put = 0;

                if (__atomic_load_n(&compositor_wl.transfer[at].cancel, __ATOMIC_ACQUIRE) || now >= deadline)
                        break;
                wait[1] = deadline - now < COMPOSITOR_WL_SLICE_NS ? deadline - now : COMPOSITOR_WL_SLICE_NS;
                compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 0, 0);
                if (!pull && (watch.returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)))
                        break;
                if (!(watch.returned & (pull ? COMPOSITOR_POLLIN | COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR : COMPOSITOR_POLLOUT)))
                        continue;
                if (pull)
                {
                        got = compositor_call(syscall(read), (positive)fd, (positive)bytes, sizeof bytes, 0, 0, 0);
                        if (got == -11) // EAGAIN
                                continue;
                        if (got == 0)
                                result = 2;
                        if (got <= 0 || done + (unsigned long)got > limit)
                                break;
                        while (put < got)
                        {
                                long wrote = compositor_call(syscall(write), (positive)keep, (positive)(bytes + put), (positive)(got - put), 0, 0, 0);

                                if (wrote <= 0)
                                        break;
                                put += wrote;
                        }
                        if (put < got)
                                break;
                        done += (unsigned long)got;
                        continue;
                }
                got = compositor_call(syscall(pread64), (positive)from, (positive)bytes,
                                      total - done > COMPOSITOR_WL_CHUNK ? COMPOSITOR_WL_CHUNK : total - done, done, 0, 0);
                if (got <= 0)
                        break;
                put = compositor_call(syscall(write), (positive)fd, (positive)bytes, (positive)got, 0, 0, 0);
                if (put == -11)
                        continue;
                if (put <= 0)
                        break;
                done += (unsigned long)put;
        }
        if (!pull && done >= total)
                result = 2;
        __atomic_store_n(&compositor_wl.transfer[at].finished, result, __ATOMIC_RELEASE);
        compositor_call(syscall(write), (positive)compositor_wl.wake, (positive)&one, sizeof one, 0, 0, 0);
}

// A transfer whose place is filled in, given its thread; false (and the place let go) when no thread can be had.
static int compositor_wl_transfer_start(long at)
{
        compositor_wl.transfer[at].finished = 0;
        compositor_wl.transfer[at].cancel = 0;
        compositor_wl.transfer[at].worker = compositor_wl.wake >= 0 ? thread_start(compositor_wl_transfer_run, (address_any)at) : 0;
        if (compositor_wl.transfer[at].worker)
                return 1;
        compositor_wl_transfer_free(at);
        return 0;
}

// A transfer the loop no longer wants: its thread is told to stop, and the loop lets it go when it has (no client's any more).
static void compositor_wl_transfer_cancel(long at)
{
        compositor_wl.transfer[at].client = -1;
        __atomic_store_n(&compositor_wl.transfer[at].cancel, 1, __ATOMIC_RELEASE);
}

// The transfers whose threads have ended, after the eventfd's knock: a copy not cancelled is made the selection or refused, the rest let go.
static void compositor_wl_pull_end(long at, int ok);

static void compositor_wl_transfers_done(void)
{
        unsigned long knocks = 0;

        compositor_call(syscall(read), (positive)compositor_wl.wake, (positive)&knocks, sizeof knocks, 0, 0, 0);
        for (long at = 0; at < COMPOSITOR_WL_TRANSFERS; at++)
        {
                int finished = compositor_wl.transfer[at].worker ? __atomic_load_n(&compositor_wl.transfer[at].finished, __ATOMIC_ACQUIRE) : 0;

                if (!finished)
                        continue;
                thread_join(compositor_wl.transfer[at].worker);
                compositor_wl.transfer[at].worker = 0;
                if (compositor_wl.transfer[at].state == COMPOSITOR_WL_PULL && !compositor_wl.transfer[at].cancel)
                        compositor_wl_pull_end(at, finished == 2);
                else
                        compositor_wl_transfer_free(at);
        }
}

/*
        A copy begun: a pipe for the source to write to. The copy is the person's (a key or a button of theirs on a window of this
        client just now), the service is on, and there is room; else -1. The row the person touched becomes the owner of the text.
*/
static long compositor_wl_clip_pull(void *context, long client, long which)
{
        struct windows *table = compositor.table;
        positive now = clock_monotonic_nanoseconds();
        int fds[2] = {-1, -1}, row = -1;
        long slot, keep;

        (void)context;
        if (!table || which < 0 || which > 1 || !table->clip_on)
                return -1;
        for (int at = 0; at < WINDOWS; at++)
                if (wayland_row_owner(at) == client && now - compositor_clip.gesture[at] <= COMPOSITOR_CLIP_GESTURE_NS &&
                    (row < 0 || at == compositor.focus))
                        row = at;
        slot = compositor_wl_transfer_place(client);
        if (row < 0 || slot < 0)
                return -1;
        if (compositor_call(syscall(pipe2), (positive)fds, 0x80000, 0, 0, 0, 0) < 0) // O_CLOEXEC
                return -1;
        // MFD_CLOEXEC | MFD_ALLOW_SEALING
        keep = compositor_call(syscall(memfd_create), (positive) "moonwater-copy", 3, 0, 0, 0, 0);
        if (keep < 0)
        {
                system_close(fds[0]);
                system_close(fds[1]);
                return -1;
        }
        compositor_call(syscall(fcntl), (positive)fds[0], 4, 0x800, 0, 0, 0); // F_SETFL, O_NONBLOCK: only this end, which is ours
        compositor_wl.transfer[slot].state = COMPOSITOR_WL_PULL;
        compositor_wl.transfer[slot].which = (unsigned char)which;
        compositor_wl.transfer[slot].client = (int)client;
        compositor_wl.transfer[slot].row = row;
        compositor_wl.transfer[slot].in = fds[0];
        compositor_wl.transfer[slot].out = -1; // the end the source writes is the shim's once handed over (0976821d)
        compositor_wl.transfer[slot].keep = (int)keep;
        compositor_wl.transfer[slot].total = 0;
        compositor_wl.transfer[slot].limit = table->clip_limit;
        compositor_wl.transfer[slot].deadline = now + COMPOSITOR_WL_PULL_NS;
        if (compositor_wl_transfer_start(slot))
                return fds[1];
        system_close(fds[1]);
        return -1;
}

// A copy that is ended: the shim ended it (its source went, or another copy came), or the client did.
static void compositor_wl_clip_abort(void *context, long client, long which)
{
        (void)context;
        for (long at = 0; at < COMPOSITOR_WL_TRANSFERS; at++)
                if (compositor_wl.transfer[at].state == COMPOSITOR_WL_PULL && compositor_wl.transfer[at].client == client &&
                    compositor_wl.transfer[at].which == which)
                        compositor_wl_transfer_cancel(at);
}

/*
        A copy that ended, well or not: the text is the selection if it is whole and fit (sealed, checked as the command line's
        and every window's copies are), and the shim is told either way, after the compositor has told the watchers.
*/
static void compositor_wl_pull_end(long at, int ok)
{
        long client = compositor_wl.transfer[at].client;
        unsigned int length = 0, which = compositor_wl.transfer[at].which;
        int row = compositor_wl.transfer[at].row, kept = -1;

        if (ok)
        {
                // F_ADD_SEALS: SEAL, SHRINK, GROW and WRITE
                compositor_call(syscall(fcntl), (positive)compositor_wl.transfer[at].keep, 1033, 15, 0, 0, 0);
                kept = compositor_clip_open(compositor.table->clip_pid, compositor_wl.transfer[at].keep, &length);
        }
        compositor_wl_transfer_free(at);
        if (kept >= 0)
                compositor_clip_keep(which, kept, length, row);
        wayland_clip_pulled(client, (long)which, kept >= 0);
}

/*
        A paste: the text of selection which to the descriptor the client gave, which is taken whatever comes of it. Given only to
        a window that is the one with the keys and was just touched, for a descriptor that is a pipe (a file on a filesystem that
        waits, or a socket, could make the compositor wait, and a pipe is written only when poll says it can take a page), and
        while there is room.
*/
static void compositor_wl_clip_push(void *context, long row, long which, long fd)
{
        struct windows *table = compositor.table;
        positive now = clock_monotonic_nanoseconds();
        unsigned char info[256];
        long src, slot, client = wayland_row_owner(row);
        int root = row >= 0 && row < WINDOWS ? compositor_clip_root((int)row) : -1;
        unsigned int mode;

        (void)context;
        memory_zero(info, sizeof info);
        // struct statx: stx_mode at 28 (u16); asked for the type, of the descriptor itself.
        if (!table || which < 0 || which > 1 || !table->clip_on || compositor_clip.fd[which] < 0 || root < 0 || root != compositor.focus ||
            now - compositor_clip.gesture[row] > COMPOSITOR_CLIP_GESTURE_NS ||
            compositor_call(syscall(statx), (positive)fd, (positive) "", 0x1000, 0x1, (positive)info, 0) < 0)
        {
                system_close((int)fd);
                return;
        }
        mode = ((unsigned int)info[28] | (unsigned int)info[29] << 8) & 0170000;
        slot = compositor_wl_transfer_place(client);
        src = mode == 0010000 ? compositor_call(syscall(fcntl), (positive)compositor_clip.fd[which], COMPOSITOR_F_DUPFD_CLOEXEC, 0, 0, 0, 0) : -1;
        if (slot < 0 || src < 0)
        {
                if (src >= 0)
                        system_close((int)src);
                system_close((int)fd);
                return;
        }
        compositor_wl.transfer[slot].state = COMPOSITOR_WL_PUSH;
        compositor_wl.transfer[slot].which = (unsigned char)which;
        compositor_wl.transfer[slot].client = (int)client;
        compositor_wl.transfer[slot].row = (int)row;
        compositor_wl.transfer[slot].in = (int)src;
        compositor_wl.transfer[slot].out = (int)fd;
        compositor_wl.transfer[slot].keep = -1;
        compositor_wl.transfer[slot].total = table->clip_length[which];
        compositor_wl.transfer[slot].limit = 0;
        compositor_wl.transfer[slot].deadline = now + COMPOSITOR_WL_PUSH_NS;
        compositor_wl_transfer_start(slot);
}

// The selection emptied by a client that held it: the compositor lets it go and tells the others, as for any selection.
static void compositor_wl_clip_clear(void *context, long client, long which)
{
        (void)context;
        (void)client;
        if (compositor.table && which >= 0 && which < 2 && compositor_clip.fd[which] >= 0)
                compositor_clip_clear((unsigned int)which);
}

// What the clipboard changed: the shim offers it to the client that has the keys.
static void compositor_wl_selection(unsigned int which)
{
        if (compositor_wl.started)
                wayland_selection_changed((long)which);
}

// A client gone: what it was in the middle of is ended, its copies read to the end by nobody.
static void compositor_wl_transfers_drop(long client)
{
        for (long at = 0; at < COMPOSITOR_WL_TRANSFERS; at++)
                if (compositor_wl.transfer[at].state && compositor_wl.transfer[at].client == client)
                        compositor_wl_transfer_cancel(at);
}

/*
        When the loop is to wake for a client whose commit waits, or 0: the end of its window of time when it waits for its budget, the
        end of its time when it waits for its GPU (whose dma-buf the loop polls, so it wakes the moment the buffer is ready, and not a
        millisecond at a time for ten seconds). A transfer's thread keeps its own time.
*/
static positive compositor_wl_due(void)
{
        positive first = 0;

        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                long fd, due = compositor_wl.fd[slot] >= 0 ? wayland_stall_due(slot, &fd) : 0;
                positive at = due > 0 ? ((positive)due + 1) * 1000000ull : 0; // the shim's clock is this one in ms

                if (at && (!first || at < first))
                        first = at;
        }
        // The idle notifications: the next one that may go idle, on the same clock (ms).
        {
                long idle = wayland_idle_due();

                if (idle > 0 && (!first || (positive)(idle + 1) * 1000000ull < first))
                        first = (positive)(idle + 1) * 1000000ull;
        }
        return first;
}

// The shim's state for moonwater debug (window.c's table): the idle clock (when the shim last had input, the inhibitors that hold, the
// idle notifications), the bell rings and icon sets, and each window's content type, modal hint and icon.
static void compositor_wl_table(void)
{
        long input = 0, inhibitors = 0, idled = 0, bells = 0, icons = 0;
        struct windows *table = compositor.table;

        wayland_idle_state(&input, &inhibitors, &idled);
        wayland_hint_counts(&bells, &icons);
        table->wl_input_ms = (unsigned long)input;
        table->wl_inhibitors = (unsigned int)inhibitors;
        table->wl_idled = (unsigned int)idled;
        table->wl_bells = (unsigned int)bells;
        table->wl_icons = (unsigned int)icons;
        table->wl_shortcuts = 0;
        for (long row = 0; row < WINDOWS; row++)
        {
                table->wl_content[row] = (unsigned char)wayland_row_content(row);
                table->wl_modal[row] = (unsigned char)wayland_row_modal(row);
                table->wl_icon[row] = (unsigned char)wayland_row_icon(row);
                table->wl_shortcuts += wayland_row_shortcuts(row) != 0;
        }
}

// An input event the compositor read (a key, a button, a motion, a finger): the idle clock counts from it.
static void compositor_wl_input(void)
{
        if (compositor_wl.started)
                wayland_input_seen();
}

// Whether a dma-buf has no writer: asked at once, never waited for.
static int compositor_wl_dmabuf_signaled(void *context, long fd)
{
        struct compositor_poll watch = {(int)fd, COMPOSITOR_POLLIN, 0};
        positive none[2] = {0, 0};

        (void)context;
        compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)none, 0, 0, 0);
        return (watch.returned & COMPOSITOR_POLLIN) != 0;
}

static const struct wayland_host compositor_wl_host = {
    0,
    compositor_wl_alloc,
    compositor_wl_release,
    compositor_wl_map,
    compositor_wl_remap,
    compositor_wl_unmap,
    compositor_wl_close,
    compositor_wl_row_open,
    compositor_wl_row_close,
    compositor_wl_row_picture,
    compositor_wl_row_title,
    compositor_wl_row_do,
    compositor_wl_row_facts,
    compositor_wl_screens,
    compositor_wl_now,
    compositor_wl_keymap,
    compositor_wl_repeat,
    compositor_wl_seat,
    compositor_wl_locks,
    compositor_wl_pointer_hold,
    compositor_wl_dmabuf_info,
    compositor_wl_dmabuf_table,
    compositor_wl_dmabuf_map,
    compositor_wl_clip_size,
    compositor_wl_clip_pull,
    compositor_wl_clip_abort,
    compositor_wl_clip_push,
    compositor_wl_clip_clear,
    compositor_wl_dmabuf_signaled,
    compositor_wl_random,
};

/* The window manager of Xwayland (xwm.c): its socket, its connection and its window table, all in fixed storage. */
#include "xwm.c"

/* ---- the socket ---- */

/*
        The listening socket, made as the compositor starts: an old one at the path is taken away first (a compositor that
        crashed leaves it), the new one is open to every user, and no client waits on it: a connection waits in the kernel's
        queue (COMPOSITOR_WL_BACKLOG) until the loop accepts it.
*/
static void compositor_wl_start(void)
{
        unsigned char address[110];
        long length = 0, got;

        if (compositor_wl.started)
                return;
        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (COMPOSITOR_WL_PATH[length])
        {
                address[2 + length] = (unsigned char)COMPOSITOR_WL_PATH[length];
                length++;
        }
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
        // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        got = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        if (got < 0)
                return;
        compositor_wl.listen = (int)got;
        if (compositor_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)got, COMPOSITOR_WL_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0666, 0, 0, 0);
        for (long at = 0; at < WAYLAND_CLIENTS; at++)
                compositor_wl.fd[at] = -1;
        for (long at = 0; at < 16; at++)
                compositor_wl.keymap[at] = -1;
        compositor_wl.layout_told = compositor.keymap.layout;
        compositor_wl.screens_told = compositor_wl_screens_digest();
        compositor_wl_repeat(0, &compositor_wl.repeat_told[0], &compositor_wl.repeat_told[1]);
        if (wayland_start(&compositor_wl_host) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        compositor_wl.started = 1;
        compositor_xwm_start();
        for (long at = 0; at < COMPOSITOR_WL_TRANSFERS; at++)
                compositor_wl.transfer[at].in = compositor_wl.transfer[at].out = compositor_wl.transfer[at].keep = -1;
        // EFD_CLOEXEC | EFD_NONBLOCK: what the transfers' threads knock on; with none, no copy or paste is taken.
        compositor_wl.wake = (int)compositor_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0);
        compositor_wl.hold_row = -1;
        // A paste's reader that has gone is an error of the write, not a signal that ends the compositor.
        system_signal_install(COMPOSITOR_WL_SIGPIPE, 1, 0, 0, null);
        // The timer that bounds a dma-buf's sync (compositor_wl_sync_start): a handler that does nothing, and no restart, so the wait ends.
        system_signal_install(COMPOSITOR_WL_SIGALRM, (positive)compositor_wl_alarm, SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART,
                              SIGNAL_CATCH_RESTORER, null);
        // The handler of a pool cut under a read: SA_SIGINFO and SA_NODEFER, with the library's restorer.
        compositor_wl.handler = system_signal_install(COMPOSITOR_WL_SIGBUS, (positive)compositor_wl_bus,
                                                      (SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART) | 4 | 0x40000000,
                                                      SIGNAL_CATCH_RESTORER, null);
}

static void compositor_wl_drop(long slot)
{
        if (compositor_wl.fd[slot] >= 0)
                system_close(compositor_wl.fd[slot]);
        compositor_wl.fd[slot] = -1;
        wayland_drop(slot);
        compositor_wl_transfers_drop(slot);
}

/* One connection, if one waits: its peer is read from the socket, and it takes a slot unless the caps say no. */
static void compositor_wl_accept(void)
{
        for (int tries = 0; tries < COMPOSITOR_WL_ACCEPTS; tries++)
        {
                int credentials[3] = {0, 0, 0}, per_user = 0;
                unsigned int size = sizeof credentials;
                long fd = compositor_call(syscall(accept4), (positive)compositor_wl.listen, 0, 0, 0x800 | 0x80000, 0, 0), slot;

                if (fd < 0)
                        return;
                // SOL_SOCKET, SO_PEERCRED: the kernel's word for who is on the other end, not the client's.
                if (compositor_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)credentials, (positive)&size, 0) < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                for (slot = 0; slot < WAYLAND_CLIENTS; slot++)
                        per_user += compositor_wl.fd[slot] >= 0 && compositor_wl.uid[slot] == credentials[1];
                if (credentials[1] != 0 && per_user >= COMPOSITOR_WL_PER_USER)
                {
                        system_close((int)fd);
                        continue;
                }
                slot = wayland_connect(credentials[0], credentials[1]);
                if (slot < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                compositor_wl.fd[slot] = (int)fd;
                compositor_wl.slow_until[slot] = 0;
                compositor_wl.asked[slot] = 0;
                compositor_wl.pid[slot] = credentials[0];
                compositor_wl.uid[slot] = credentials[1];
        }
}

/*
        What a client has sent, a bounded amount of it: up to COMPOSITOR_WL_READS reads of COMPOSITOR_WL_READ bytes with the
        descriptors that came, to the shim. A client that sends faster than this has the rest next pass, after everyone
        else's turn. A stream that ended or failed, or control data that did not fit, is a client gone.
*/
static void compositor_wl_read(long slot)
{
        static unsigned char bytes[COMPOSITOR_WL_READ];

        for (int reads = 0; reads < COMPOSITOR_WL_READS; reads++)
        {
                positive control[(sizeof(positive) * 3 + WAYLAND_FDS * sizeof(int) + 15) / sizeof(positive) + 2];
                positive message[7], vector[2] = {(positive)bytes, sizeof bytes};
                long got, fds[WAYLAND_FDS], count = 0;
                int excess = 0;

                memory_zero(message, sizeof message);
                memory_zero(control, sizeof control);
                message[2] = (positive)vector;
                message[3] = 1;
                message[4] = (positive)control;
                message[5] = sizeof control;
                // MSG_DONTWAIT | MSG_CMSG_CLOEXEC
                got = compositor_call(syscall(recvmsg), (positive)compositor_wl.fd[slot], (positive)message, 0x40 | 0x40000000, 0, 0, 0);
                if (got == -11) // EAGAIN
                        return;
                if (got <= 0)
                {
                        compositor_wl_drop(slot);
                        return;
                }
                // The descriptors, from the control data: SCM_RIGHTS in SOL_SOCKET. The kernel puts as many in this process as
                // the control buffer holds, which is more than the shim takes (36 against 28): one past the shim's are closed
                // here, and the client is one that sent more than it may, so none of them stays open in the compositor.
                for (unsigned char *at = (unsigned char *)control; message[5] >= 16 && at < (unsigned char *)control + message[5];)
                {
                        unsigned long length = *(unsigned long *)at;
                        int level = *(int *)(at + 8), type = *(int *)(at + 12);

                        if (length < 16 || at + length > (unsigned char *)control + message[5])
                                break;
                        if (level == 1 && type == 1)
                                for (unsigned long item = 16; item + 4 <= length; item += 4)
                                        if (count < WAYLAND_FDS)
                                                fds[count++] = *(int *)(at + item);
                                        else
                                        {
                                                system_close(*(int *)(at + item));
                                                excess = 1;
                                        }
                        at += (length + 7) & ~7ul;
                }
                // MSG_CTRUNC: descriptors were lost, and what is left of the stream can no longer be trusted to line up.
                if ((message[6] & 0x8) || excess)
                {
                        for (long item = 0; item < count; item++)
                                system_close((int)fds[item]);
                        compositor_wl_drop(slot);
                        return;
                }
                if (wayland_feed(slot, bytes, got, fds, count) < 0)
                        return;
                // A commit that waits for its GPU holds what follows it: no more is read until it is made.
                if (got < (long)sizeof bytes || wayland_stalled(slot))
                        return;
        }
}

/*
        What the shim has queued for a client, written: the bytes with the descriptors that go with them, once. What the
        socket takes is taken off the queue and the rest waits for the loop to find the socket writable. A client whose
        queue is full is dead (the shim says so), and one that was sent a protocol error is dropped once it is sent.
*/
static void compositor_wl_flush(long slot)
{
        const unsigned char *bytes;
        const long *fds;
        long fd_count, length, got;
        int dead;

        if (compositor_wl.fd[slot] < 0)
                return;
        dead = (int)wayland_dead(slot);
        length = wayland_out(slot, &bytes, &fds, &fd_count);
        if (dead == 2)
        {
                compositor_wl_drop(slot);
                return;
        }
        if (length)
        {
                positive control[2 + WAYLAND_OUT_FDS], message[7], vector[2] = {(positive)bytes, (positive)length};
                int pass[WAYLAND_OUT_FDS];

                memory_zero(message, sizeof message);
                memory_zero(control, sizeof control);
                message[2] = (positive)vector;
                message[3] = 1;
                if (fd_count > 0)
                {
                        for (long at = 0; at < fd_count && at < WAYLAND_OUT_FDS; at++)
                                pass[at] = (int)fds[at];
                        control[0] = 16 + (positive)fd_count * 4;
                        control[1] = 1 | (positive)1 << 32; // SOL_SOCKET, SCM_RIGHTS
                        for (long at = 0; at < fd_count; at++)
                                ((int *)(control + 2))[at] = pass[at];
                        message[4] = (positive)control;
                        message[5] = ((16 + (positive)fd_count * 4) + 7) & ~(positive)7;
                }
                // MSG_DONTWAIT | MSG_NOSIGNAL
                got = compositor_call(syscall(sendmsg), (positive)compositor_wl.fd[slot], (positive)message, 0x40 | 0x4000, 0, 0, 0);
                if (got > 0)
                        wayland_sent(slot, got); // which closes a copy's pipe end that went with it: it is the shim's
                else if (got != -11)
                {
                        compositor_wl_drop(slot);
                        return;
                }
        }
        if (dead == 1 && !wayland_out(slot, &bytes, &fds, &fd_count))
                compositor_wl_drop(slot);
}

static void compositor_wl_flush_all(void)
{
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_flush(slot);
}

/* Where the polls for the clients go in the loop's list, and what comes of them. */
static unsigned int compositor_wl_poll(struct compositor_poll *wanted, unsigned int count)
{
        unsigned int base = count;

        if (!compositor_wl.started)
                return count;
        wanted[count++] = (struct compositor_poll){compositor_wl.listen, COMPOSITOR_POLLIN, 0};
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                const unsigned char *bytes;
                const long *fds;
                long fd_count;
                // A client whose commit waits for its GPU is not read (a hangup is told all the same), and the loop wakes to ask.
                short events = compositor_wl.fd[slot] >= 0 && wayland_stalled(slot) ? 0 : COMPOSITOR_POLLIN;

                if (compositor_wl.fd[slot] >= 0 && wayland_out(slot, &bytes, &fds, &fd_count))
                        events |= COMPOSITOR_POLLOUT;
                wanted[count++] = (struct compositor_poll){compositor_wl.fd[slot], events, 0};
        }
        // The knock of a transfer's thread that has ended, after the clients (a descriptor of -1, with no eventfd, is not polled).
        wanted[count++] = (struct compositor_poll){compositor_wl.wake, COMPOSITOR_POLLIN, 0};
        // The dma-bufs that stalled clients wait for, readable when their GPU is done (nothing is read of them: the pass resumes).
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                long fd = -1;

                if (compositor_wl.fd[slot] >= 0 && wayland_stall_due(slot, &fd) && fd >= 0)
                        wanted[count++] = (struct compositor_poll){(int)fd, COMPOSITOR_POLLIN, 0};
        }
        // The window manager of Xwayland (xwm.c): its listening socket and its connection.
        return compositor_xwm_poll(wanted, count, base);
}

static void compositor_wl_pass(const struct compositor_poll *wanted)
{
        if (!compositor_wl.started)
                return;
        // The commits that waited for their GPU, made when it is done, before what else the clients sent this pass.
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0 && wayland_stalled(slot))
                {
                        wayland_resume(slot);
                        if (wayland_dead(slot))
                                compositor_wl_flush(slot);
                }
        if (wanted[0].returned)
                compositor_wl_accept();
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                short returned = wanted[1 + slot].returned;

                if (compositor_wl.fd[slot] < 0 || !returned)
                        continue;
                if (returned & COMPOSITOR_POLLIN)
                        compositor_wl_read(slot);
                else if (returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR))
                {
                        compositor_wl_drop(slot);
                        continue;
                }
                if (compositor_wl.fd[slot] >= 0 && wayland_dead(slot))
                        compositor_wl_flush(slot);
        }
        // The transfers whose threads have ended: each thread ends itself at its deadline, so the loop waits on nothing else of them.
        if (wanted[1 + WAYLAND_CLIENTS].returned)
                compositor_wl_transfers_done();
        compositor_xwm_pass(wanted);
        // A commit that waited for the window manager to say what an X window is, made now that it has.
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0 && wayland_stalled(slot))
                {
                        wayland_resume(slot);
                        if (wayland_dead(slot))
                                compositor_wl_flush(slot);
                }
}

/* ---- input: the pointer and the keys, as a client is told of them ---- */

/*
        The window the pointer is over, for a client: the one a button is held on, else the one it moved over last, when it is
        a client's window and it is shown and is not being moved or resized. Just outside a window's picture is the edge the
        compositor resizes by, and no client's.
*/
static int compositor_wl_pointer_row(void)
{
        int row = compositor.grab >= 0 ? compositor.grab : compositor.hover;

        if (row < 0 || compositor.drag >= 0 || compositor.resize >= 0 || wayland_row_owner(row) < 0 || !compositor.showing[row] ||
            (compositor.state[row] & WINDOW_MINIMIZED))
                return -1;
        if (compositor.grab < 0 &&
            (compositor.pointer_x < compositor.x[row] || compositor.pointer_y < compositor.y[row] ||
             compositor.pointer_x >= compositor.x[row] + (int)compositor.w[row] || compositor.pointer_y >= compositor.y[row] + (int)compositor.h[row]))
                return -1;
        return row;
}

/*
        The window the keys go to, for a client: the topmost popup of the window that has them, but a popup that did not ask
        to grab (a tooltip) is not given the keys, its window is. -1 when it is not a client's window.
*/
static int compositor_wl_key_row(void)
{
        int row = compositor.focus;

        if (row < 0)
                return -1;
        row = compositor_popup_top(row);
        while (row >= 0 && compositor_popup_parent[row] >= 0 && wayland_row_owner(row) >= 0 && !wayland_row_grabs(row))
                row = compositor_popup_parent[row];
        return wayland_row_owner(row) >= 0 ? row : -1;
}

// What the compositor's chords do for a key, for the window that has the keys: 0 they run, 1 the window has the key (its shortcuts
// inhibitor is active), 2 the one chord the inhibitor keeps (Super and Escape), which is the compositor's (wayland_shortcuts_key).
static int compositor_wl_suspends(unsigned int code, int value, int super)
{
        int row;

        if (!compositor_wl.started)
                return 0;
        row = compositor_wl_key_row();
        return row < 0 ? 0 : (int)wayland_shortcuts_key(row, (long)code, (long)value, super);
}

/*
        A key that is the window's own to read: the key as it is, to the client that has the keys, with Caps Lock kept here
        where the compositor keeps it, and no repeat (the client repeats at the rate it was told). The compositor's own
        reading of a key (its characters, its dead accents) is for its own windows, which a client does by its keymap. True
        when the key was a client's.
*/
static int compositor_wl_key(unsigned int code, int value)
{
        int row;

        if (!compositor_wl.started)
                return 0;
        if (compositor.focus < 0)
                compositor_refocus();
        row = compositor_wl_key_row();
        if (row < 0)
                return 0;
        // The window is told it has the keys before it is given one: the focus may have changed in this very pass (the first
        // key after a window was shown, or the first of all), and the shim is told of focus only once a pass otherwise.
        wayland_keyboard_to(row);
        if (code == CK_CAPSLOCK && value == 1)
                compositor.keymap.caps ^= 1;
        // The keys down, and the window they went to, as the compositor keeps them for its own windows.
        if (code < 128)
        {
                unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[code >> 3];

                if (value == 1)
                {
                        if (compositor.keys_to != row)
                                compositor_keys_release();
                        *down |= mask;
                        compositor.keys_to = row;
                }
                else if (!(*down & mask))
                        return 1;
                else if (!value)
                        *down &= (unsigned char)~mask;
        }
        if (value == 1)
                compositor_clip_gesture(row);
        if (value != 2)
                wayland_key(row, code, value);
        return 1;
}

// A client's ring, read as the compositor posted it: a motion, a button, a turn of the wheel, and the releases of keys held.
static void compositor_wl_drain(int row)
{
        struct windows *table = compositor.table;
        unsigned int tail = table->event_tail[row], head = __atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE);

        for (unsigned int taken = 0; tail != head && taken < WINDOWS_EVENTS; taken++, tail++)
        {
                unsigned int at = tail % WINDOWS_EVENTS, type = table->event_type[row][at], flags = table->event_flags[row][at];
                int a = __atomic_exchange_n(&table->event_a[row][at], 0, __ATOMIC_ACQ_REL), b = table->event_b[row][at];

                if (type == WINDOW_EVENT_KEY)
                {
                        // The keyboard's key carries the Shift it makes its character with (osk.c): the client is told Shift as a person's.
                        if (!(flags & WINDOW_KEY_REPEAT))
                                wayland_key_shift(row, a, (flags & WINDOW_KEY_DOWN) ? 1 : 0, (flags & WINDOW_KEY_SHIFT) ? 1 : 0);
                        continue;
                }
                if (type == WINDOW_EVENT_PINCH)
                {
                        // A trackpad's pinch: its phase, its scale (1024ths) and its focal point, packed as the window's pixels.
                        long phase = flags & WINDOW_PINCH_BEGIN ? 1 : flags & WINDOW_PINCH_END ? 2 : 0;

                        wayland_pointer_pinch(row, phase, a, (long)(((unsigned int)b >> 16) & 0xffffu), (long)((unsigned int)b & 0xffffu));
                        continue;
                }
                if (type != WINDOW_EVENT_MOTION && type != WINDOW_EVENT_BUTTON && type != WINDOW_EVENT_WHEEL)
                        continue;
                // A finger on a window whose client holds a wl_touch is told of by its touch (compositor_wl_touch_report): the
                // pointer's version of the same finger (the kit's press and motion, which say so) is not sent to that client.
                if ((flags & WINDOW_KEY_TOUCH) && wayland_row_takes_touch(row))
                        continue;
                // Whatever the event, the pointer is on this window for the client first.
                if (wayland.pointer_row != row)
                        wayland_pointer_to(row, type == WINDOW_EVENT_MOTION ? a : compositor.pointer_x - compositor.x[row],
                                           type == WINDOW_EVENT_MOTION ? b : compositor.pointer_y - compositor.y[row]);
                if (type == WINDOW_EVENT_MOTION)
                        wayland_pointer_motion(row, a, b);
                else if (type == WINDOW_EVENT_BUTTON)
                        wayland_pointer_button(row, a, (flags & WINDOW_KEY_DOWN) ? 1 : 0);
                else if (flags & WINDOW_WHEEL_STOP)
                        // The last finger of a pad lifted: the precise scroll is over (axis_stop on both axes).
                        wayland_pointer_axis_stop(row);
                else
                        // A wheel's v120 is the evdev sign, up positive; the protocol's vertical axis is positive downward. A stream's
                        // notch is a wheel's (value120), a finger's turn is the window's notch in pixels (UI_NOTCH).
                        wayland_pointer_axis(row, b != 0, b ? a : -a,
                                             (flags & WINDOW_WHEEL_PRECISE) && !(flags & WINDOW_WHEEL_STREAM) ? UI_NOTCH : 0);
        }
        __atomic_store_n(&table->event_tail[row], tail, __ATOMIC_RELEASE);
}

/*
        What changed for the clients this pass: where the pointer and the keys are, the layout (every keyboard is sent the
        new keymap), and each window the compositor knocked on, its ring read and its state and size said. Called before the
        frame is drawn, so no event waits for it; everything queued is sent.
*/
static void compositor_wl_wake(void)
{
        struct wayland_stats stats;

        if (!compositor_wl.started)
                return;
        wayland_stats_get(&stats);
        // The screens: a scale, a mode, a place or a turn is told to every client whole, and each window the scale of its screen.
        if (compositor_wl_screens_digest() != compositor_wl.screens_told)
        {
                compositor_wl.screens_told = compositor_wl_screens_digest();
                wayland_screens_changed();
        }
        wayland_idle_tick();
        compositor_wl_table();
        if (stats.rows > 0)
                wayland_scales_sweep();
        if (stats.rows > 0 || stats.clients > 0)
        {
                int want = compositor_wl_pointer_row(), keys = compositor_wl_key_row();

                if (compositor.keymap.layout != compositor_wl.layout_told)
                {
                        compositor_wl.layout_told = compositor.keymap.layout;
                        wayland_keymap_changed();
                }
                {
                        long rate, delay;

                        compositor_wl_repeat(0, &rate, &delay);
                        if (rate != compositor_wl.repeat_told[0] || delay != compositor_wl.repeat_told[1])
                        {
                                compositor_wl.repeat_told[0] = rate;
                                compositor_wl.repeat_told[1] = delay;
                                wayland_repeat_changed();
                        }
                }
                // A finger was the last thing used (the pointer is hidden under it) on a window whose client holds a touch: the
                // pointer is not on that window for the client, the touch is; no enter is sent for the finger's move.
                if (want >= 0 && compositor.touch.hidden && wayland_row_takes_touch(want))
                        want = -1;
                if (want != wayland.pointer_row)
                        wayland_pointer_to(want, want >= 0 ? compositor.pointer_x - compositor.x[want] : 0,
                                           want >= 0 ? compositor.pointer_y - compositor.y[want] : 0);
                if (keys != wayland.keyboard_row)
                        wayland_keyboard_to(keys);
        }
        for (int row = 0; row < WINDOWS; row++)
                if ((compositor.woken & (1u << row)) && wayland_row_owner(row) >= 0)
                {
                        compositor_wl_drain(row);
                        wayland_row_wake(row);
                        compositor.woken &= ~(1u << row);
                }
        compositor_wl_flush_all();
}

/* A frame was drawn: the callbacks that waited for it are answered, and sent. */
static void compositor_wl_frame(void)
{
        if (!compositor_wl.started)
                return;
        wayland_frame();
        compositor_wl_flush_all();
}

/*
        The fingers of a touch screen, as the windows' clients are told them (wl_touch): a finger that is down now and was not in
        the last report is a touch down on the window under it, one that was down and is down still moved (a motion, when it
        is somewhere else), and one that was down and is not is lifted. dropped: the device lost its events, so every finger
        is cancelled instead. Called at the end of compositor_touch_report, before its ids become the last report's.
*/
static void compositor_wl_touch_report(unsigned int at, int dropped)
{
        const struct compositor_touch_screen *screen = &compositor.screen_touch[at];

        if (!compositor_wl.started)
                return;
        if (dropped)
        {
                wayland_touch_cancel();
                compositor_wl_flush_all();
                return;
        }
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                int x, y, row;

                if (screen->id[slot] >= 0)
                {
                        compositor_touch_point(at, slot, &x, &y);
                        if (screen->was[slot] < 0)
                        {
                                row = compositor_picture_under(x, y);
                                if (row >= 0)
                                        wayland_touch_down(row, screen->id[slot], x - compositor.x[row], y - compositor.y[row]);
                        }
                        else if ((row = wayland_touch_row(screen->id[slot])) >= 0)
                                wayland_touch_motion(screen->id[slot], x - compositor.x[row], y - compositor.y[row]);
                }
                else if (screen->was[slot] >= 0)
                        wayland_touch_up(screen->was[slot]);
        }
        compositor_wl_flush_all();
}

/*
        A frame was drawn and flipped: the presentation feedbacks its commits took are answered with the time of the flip, which is
        the compositor's clock as the flip is issued (no vblank event is read here, so the shim claims none), and the period.
*/
static void compositor_wl_presented(void)
{
        positive now = clock_monotonic_nanoseconds();

        if (!compositor_wl.started)
                return;
        // A frame whose flip is in flight waits for the card's completion (the time the card stamps it with). A flip that does not
        // complete is answered by the clock once two frames have gone by, so no feedback waits for ever.
        if (compositor.flip_pending && !compositor_wl.presenting_waiting)
        {
                compositor_wl.presenting_waiting = 1;
                compositor_wl.presenting_since = now;
                return;
        }
        if (compositor_wl.presenting_waiting && now - compositor_wl.presenting_since < 2 * compositor.period)
                return;
        compositor_wl.presenting_waiting = 0;
        wayland_presented((unsigned long)now, (unsigned long)compositor.period, compositor_wl.presents++, 0);
        compositor_wl_flush_all();
}

/*
        The card completed a flip: the frame it showed is answered with the card's own stamp (the vblank the card read), flagged
        as the hardware's: vsync, hw_clock and hw_completion. A flip whose stamp did not come is answered by the clock, flagless.
        The sequence is the card's, when it sent one.
*/
static void compositor_wl_flip_presented(void)
{
        if (!compositor_wl.started || !compositor_wl.presenting_waiting)
                return;
        compositor_wl.presenting_waiting = 0;
        if (compositor.flip_stamped && compositor.flip_when)
                wayland_presented((unsigned long)compositor.flip_when, (unsigned long)compositor.period, compositor.flip_seq, 7);
        else
                wayland_presented((unsigned long)clock_monotonic_nanoseconds(), (unsigned long)compositor.period, compositor_wl.presents++, 0);
        compositor_wl_flush_all();
}

static void compositor_wl_stop(void)
{
        if (!compositor_wl.started)
                return;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_drop(slot);
        wayland_stop();
        // A transfer whose thread still runs keeps its descriptors (and the eventfd stays) until the process ends.
        for (long at = 0; at < COMPOSITOR_WL_TRANSFERS; at++)
                if (compositor_wl.transfer[at].state && !compositor_wl.transfer[at].worker)
                        compositor_wl_transfer_free(at);
                else if (compositor_wl.transfer[at].state)
                        compositor_wl_transfer_cancel(at);
        if (compositor_wl.dmabuf_table > 0)
                system_close(compositor_wl.dmabuf_table);
        compositor_wl.dmabuf_table = 0;
        for (long at = 0; at < 16; at++)
                if (compositor_wl.keymap[at] >= 0)
                        system_close(compositor_wl.keymap[at]);
        compositor_xwm_stop();
        system_close(compositor_wl.listen);
        compositor_wl.listen = -1;
        compositor_wl.started = 0;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
}

#endif /* WAYLAND_HOST_INCLUDED */
