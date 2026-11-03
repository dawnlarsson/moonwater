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

_Static_assert(WAYLAND_ROWS == WINDOWS, "the shim's rows are the compositor's table");

static struct
{
        int listen;
        int fd[WAYLAND_CLIENTS];
        int uid[WAYLAND_CLIENTS], pid[WAYLAND_CLIENTS];
        unsigned char asked[WAYLAND_CLIENTS]; // the connection's first window has been opened, so its launch is not read again
        int started, handler;
        unsigned long frames;
        // The keymap of each layout, made when a keyboard first needs it: a sealed file of xkb's text, kept for as long as the
        // compositor runs, and the layout the clients were last told.
        int keymap[16];
        unsigned long keymap_size[16];
        unsigned int layout_told;
        // Where each pool is mapped, so a SIGBUS can be put to the pool whose file was cut.
        struct
        {
                volatile unsigned long at, size;
                volatile int bad;
        } map[WAYLAND_CLIENTS * WAYLAND_POOLS];
} compositor_wl = {.listen = -1};

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
        long got = compositor_call(syscall(mremap), (positive)at, (positive)old_size, (positive)new_size, 1, 0, 0);

        (void)context;
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

static void compositor_wl_row_close(void *context, int row)
{
        (void)context;
        compositor_clip_leave(row);
        compositor_popup_leave(row);
        compositor_leave(row);
}

/*
        A client's buffer, the rows that changed, into the row's picture: written in place into the buffer the compositor
        shows, because it is this process that reads it, and into the other when the card is reading that one (the whole
        of it then). The table is then as a program leaves it after a picture, and the row is taken at once.
*/
static int compositor_wl_row_picture(void *context, int row, const struct wayland_picture *picture)
{
        struct windows *table = compositor.table;
        unsigned int width = (unsigned int)picture->width, height = (unsigned int)picture->height;
        unsigned int which = compositor.showing[row] ? compositor.front[row] & 1u : 0;
        unsigned long pitch = (unsigned long)compositor_stride();
        long x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        int full = (int)picture->full || !picture->damages;
        unsigned int *to;

        (void)context;
        width = width > (unsigned int)compositor_most_width() ? (unsigned int)compositor_most_width() : width;
        height = height > (unsigned int)compositor_most_height() ? (unsigned int)compositor_most_height() : height;
        if (compositor.showing[row] && (table->pinned[row] || compositor.direct_row == row))
        {
                which ^= 1u;
                full = 1;
        }
        to = (unsigned int *)((char *)table + compositor.pixels + ((unsigned long)row * 2 + which) * compositor.buffer);
        if (full)
        {
                memory_copy_rect_u32(to, pitch, (const unsigned int *)picture->pixels, (unsigned long)picture->stride / 4, width, height);
                x2 = (long)width;
                y2 = (long)height;
        }
        else
                for (long at = 0; at < picture->damages; at++)
                {
                        long left = picture->damage[at].x, top = picture->damage[at].y;
                        long right = left + picture->damage[at].width, bottom = top + picture->damage[at].height;

                        right = right > (long)width ? (long)width : right;
                        bottom = bottom > (long)height ? (long)height : bottom;
                        if (right <= left || bottom <= top)
                                continue;
                        memory_copy_rect_u32(to + (unsigned long)top * pitch + (unsigned long)left, pitch,
                                             (const unsigned int *)(picture->pixels + top * picture->stride) + left,
                                             (unsigned long)picture->stride / 4, (unsigned long)(right - left), (unsigned long)(bottom - top));
                        if (x2 == 0 && y2 == 0)
                                x1 = left, y1 = top, x2 = right, y2 = bottom;
                        else
                        {
                                x1 = left < x1 ? left : x1;
                                y1 = top < y1 ? top : y1;
                                x2 = right > x2 ? right : x2;
                                y2 = bottom > y2 ? bottom : y2;
                        }
                }
        if (compositor_wl_cut(picture->pixels))
                return -1;
        table->width[row] = width;
        table->height[row] = height;
        table->buffer_index[row] = which;
        table->damage_x[row] = (int)x1;
        table->damage_y[row] = (int)y1;
        table->damage_width[row] = full ? 0 : (int)(x2 - x1);
        table->damage_height[row] = full ? 0 : (int)(y2 - y1);
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

static void compositor_wl_row_do(void *context, int row, long what, long a, long b)
{
        (void)context;
        (void)b;
        if (row < 0 || row >= WINDOWS || !compositor.showing[row])
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
                compositor_arrange(row, COMPOSITOR_MAXIMIZED);
                break;
        case WAYLAND_DO_UNMAXIMIZE:
        case WAYLAND_DO_UNFULLSCREEN:
                compositor_float(row);
                break;
        case WAYLAND_DO_CLOSE:
                compositor_close(row);
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
static long compositor_wl_seat(void *context)
{
        (void)context;
        return WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard;
}

// Caps Lock, as the keymap's own Lock mask.
static long compositor_wl_locks(void *context)
{
        (void)context;
        return compositor.keymap.caps ? 2 : 0;
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
};

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
        if (wayland_start(&compositor_wl_host) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        compositor_wl.started = 1;
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
                // The descriptors, from the control data: SCM_RIGHTS in SOL_SOCKET.
                for (unsigned char *at = (unsigned char *)control; message[5] >= 16 && at < (unsigned char *)control + message[5];)
                {
                        unsigned long length = *(unsigned long *)at;
                        int level = *(int *)(at + 8), type = *(int *)(at + 12);

                        if (length < 16 || at + length > (unsigned char *)control + message[5])
                                break;
                        if (level == 1 && type == 1)
                                for (unsigned long item = 16; item + 4 <= length && count < WAYLAND_FDS; item += 4)
                                        fds[count++] = *(int *)(at + item);
                        at += (length + 7) & ~7ul;
                }
                // MSG_CTRUNC: descriptors were lost, and what is left of the stream can no longer be trusted to line up.
                if (message[6] & 0x8)
                {
                        for (long item = 0; item < count; item++)
                                system_close((int)fds[item]);
                        compositor_wl_drop(slot);
                        return;
                }
                if (wayland_feed(slot, bytes, got, fds, count) < 0)
                        return;
                if (got < (long)sizeof bytes)
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
                        wayland_sent(slot, got);
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
        if (!compositor_wl.started)
                return count;
        wanted[count++] = (struct compositor_poll){compositor_wl.listen, COMPOSITOR_POLLIN, 0};
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                const unsigned char *bytes;
                const long *fds;
                long fd_count;
                short events = COMPOSITOR_POLLIN;

                if (compositor_wl.fd[slot] >= 0 && wayland_out(slot, &bytes, &fds, &fd_count))
                        events |= COMPOSITOR_POLLOUT;
                wanted[count++] = (struct compositor_poll){compositor_wl.fd[slot], events, 0};
        }
        return count;
}

static void compositor_wl_pass(const struct compositor_poll *wanted)
{
        if (!compositor_wl.started)
                return;
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
                        if (!(flags & WINDOW_KEY_REPEAT))
                                wayland_key(row, a, (flags & WINDOW_KEY_DOWN) ? 1 : 0);
                        continue;
                }
                if (type != WINDOW_EVENT_MOTION && type != WINDOW_EVENT_BUTTON && type != WINDOW_EVENT_WHEEL)
                        continue;
                // Whatever the event, the pointer is on this window for the client first.
                if (wayland.pointer_row != row)
                        wayland_pointer_to(row, type == WINDOW_EVENT_MOTION ? a : compositor.pointer_x - compositor.x[row],
                                           type == WINDOW_EVENT_MOTION ? b : compositor.pointer_y - compositor.y[row]);
                if (type == WINDOW_EVENT_MOTION)
                        wayland_pointer_motion(row, a, b);
                else if (type == WINDOW_EVENT_BUTTON)
                        wayland_pointer_button(row, a, (flags & WINDOW_KEY_DOWN) ? 1 : 0);
                else
                        // A wheel's v120 is the evdev sign, up positive; the protocol's vertical axis is positive downward.
                        wayland_pointer_axis(row, b != 0, b ? a : -a, (flags & WINDOW_WHEEL_PRECISE) != 0);
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
        if (stats.rows > 0 || stats.clients > 0)
        {
                int want = compositor_wl_pointer_row(), keys = compositor_wl_key_row();

                if (compositor.keymap.layout != compositor_wl.layout_told)
                {
                        compositor_wl.layout_told = compositor.keymap.layout;
                        wayland_keymap_changed();
                }
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

static void compositor_wl_stop(void)
{
        if (!compositor_wl.started)
                return;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_drop(slot);
        wayland_stop();
        for (long at = 0; at < 16; at++)
                if (compositor_wl.keymap[at] >= 0)
                        system_close(compositor_wl.keymap[at]);
        system_close(compositor_wl.listen);
        compositor_wl.listen = -1;
        compositor_wl.started = 0;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
}

#endif /* WAYLAND_HOST_INCLUDED */
