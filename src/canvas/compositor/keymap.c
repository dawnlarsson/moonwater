/*
        What a key means: the layouts' tables, the levels and dead keys over them, and the modifiers held.

        Tables and pure functions of a key and the held state only: no device is read here and no window told (input.c
        reads the keys and asks this what each one is; wayland_host.c asks it for a client's keymap and its dead keys). It
        is part of the backend CHECK_canvas_cells builds without a compositor (the section between its two lines below,
        which the core_state lane cuts by their text), so it names nothing of struct compositor, which keeps one struct
        compositor_keymap of the held state from here. It needs only window.c's WINDOW_KEY_* flags. Called by compositor.c
        (its state, the layout read at the start), session.c (compositor_layout_index), input.c and wayland_host.c.
*/

/* ---- keymap: what a key means ---- */

/*
        What a keyboard means is the compositor's to decide, as what a
        string looks like is: a program is handed characters and not
        scancodes and a table of its own. These are the kernel desktop's
        tables, the US one and a patch over it for each of the other
        layouts, and the same rules over them -- AltGr is level three and
        Shift adds one, a level a layout leaves empty falls back the way it
        always did, and Control over a letter is the control code that
        letter names. Built on top of them, which the kernel desktop never
        had: dead keys, Caps Lock, and the characters a dead key and a
        letter make between them.

        A patch is a key and its four levels (plain, Shift, AltGr, AltGr and
        Shift); a character with CK_DEAD on is an accent that waits for the
        letter it goes on. Layouts are named as the CLI names them.

        A pure function of a state and a key: the checks drive it with no
        card and no compositor.
*/
#define CK_DEAD 0x10000u // above the Basic Multilingual Plane's last character: the presentation forms of Arabic (U+FExx) are characters too
#define CK_TABLE 99
#define CK_LEVELS 4
#define CK_102ND 86
#define CK_CAPSLOCK 58
#define CK_KEY_BACKSPACE 14
#define CK_KEY_TAB 15
#define CK_KEY_F4 62
#define CK_KEY_F9 67
#define CK_KEY_MUTE 113
#define CK_KEY_VOLUME_DOWN 114
#define CK_KEY_VOLUME_UP 115
#define CK_KEY_MICMUTE 248
#define CK_KEY_UP 103
#define CK_KEY_LEFT 105
#define CK_KEY_RIGHT 106
#define CK_KEY_DOWN 108
#define CK_KEY_ESC 1
#define CK_ACUTE 0xB4u
#define CK_GRAVE '`'
#define CK_DIAERESIS 0xA8u
#define CK_CIRCUMFLEX '^'
#define CK_TILDE '~'
#define D(c) (CK_DEAD | (c))

struct compositor_patch
{
        unsigned char code;
        unsigned int level[CK_LEVELS];
};

// Backspace is DEL and Return a line feed, as every window has read them.
static const unsigned char compositor_us[CK_TABLE][2] = {
    [1] = {27, 27},
    [2] = {'1', '!'},  [3] = {'2', '@'},  [4] = {'3', '#'},
    [5] = {'4', '$'},  [6] = {'5', '%'},  [7] = {'6', '^'},
    [8] = {'7', '&'},  [9] = {'8', '*'},  [10] = {'9', '('},
    [11] = {'0', ')'}, [12] = {'-', '_'}, [13] = {'=', '+'},
    [14] = {127, 127}, [15] = {'\t', '\t'},
    [16] = {'q', 'Q'}, [17] = {'w', 'W'}, [18] = {'e', 'E'},
    [19] = {'r', 'R'}, [20] = {'t', 'T'}, [21] = {'y', 'Y'},
    [22] = {'u', 'U'}, [23] = {'i', 'I'}, [24] = {'o', 'O'},
    [25] = {'p', 'P'}, [26] = {'[', '{'}, [27] = {']', '}'},
    [28] = {'\n', '\n'},
    [30] = {'a', 'A'}, [31] = {'s', 'S'}, [32] = {'d', 'D'},
    [33] = {'f', 'F'}, [34] = {'g', 'G'}, [35] = {'h', 'H'},
    [36] = {'j', 'J'}, [37] = {'k', 'K'}, [38] = {'l', 'L'},
    [39] = {';', ':'}, [40] = {'\'', '"'}, [41] = {'`', '~'},
    [43] = {'\\', '|'},
    [44] = {'z', 'Z'}, [45] = {'x', 'X'}, [46] = {'c', 'C'},
    [47] = {'v', 'V'}, [48] = {'b', 'B'}, [49] = {'n', 'N'},
    [50] = {'m', 'M'}, [51] = {',', '<'}, [52] = {'.', '>'},
    [53] = {'/', '?'}, [55] = {'*', '*'}, [57] = {' ', ' '},
    [71] = {'7', '7'}, [72] = {'8', '8'}, [73] = {'9', '9'},
    [74] = {'-', '-'}, [75] = {'4', '4'}, [76] = {'5', '5'},
    [77] = {'6', '6'}, [78] = {'+', '+'}, [79] = {'1', '1'},
    [80] = {'2', '2'}, [81] = {'3', '3'}, [82] = {'0', '0'},
    [83] = {'.', '.'}, [96] = {'\n', '\n'}, [98] = {'/', '/'},
};

static const struct compositor_patch compositor_uk[] = {
    {3, {'2', '"', '@'}},   {4, {'3', 0xA3, 0xA3}}, {40, {'\'', '@'}},
    {41, {'`', 0xAC, 0xA6}}, {43, {'#', '~', '\\'}}, {CK_102ND, {'\\', '|'}},
};

static const struct compositor_patch compositor_de[] = {
    {12, {0xDF, '?', '\\'}}, {13, {D(CK_ACUTE), D(CK_GRAVE)}}, {21, {'z', 'Z'}},
    {26, {0xFC, 0xDC}},      {27, {'+', '*', '~'}},            {39, {0xF6, 0xD6}},
    {40, {0xE4, 0xC4}},      {41, {D(CK_CIRCUMFLEX), 0xB0}},   {43, {'#', '\''}},
    {44, {'y', 'Y'}},        {CK_102ND, {'<', '>', '|'}},      {8, {'7', '/', '{'}},
    {9, {'8', '(', '['}},    {10, {'9', ')', ']'}},            {11, {'0', '=', '}'}},
    {16, {'q', 'Q', '@'}},   {18, {'e', 'E', 0x20AC}},
};

static const struct compositor_patch compositor_se[] = {
    {3, {'2', '"', '@'}},   {4, {'3', '#', 0xA3}},   {5, {'4', 0xA4, '$'}},
    {6, {'5', '%', 0x20AC}}, {7, {'6', '&'}},        {8, {'7', '/', '{'}},
    {9, {'8', '(', '['}},   {10, {'9', ')', ']'}},   {11, {'0', '=', '}'}},
    {12, {'+', '?', '\\'}}, {13, {D(CK_ACUTE), D(CK_GRAVE)}},
    {26, {0xE5, 0xC5}},     {27, {D(CK_DIAERESIS), D(CK_CIRCUMFLEX), D(CK_TILDE)}},
    {39, {0xF6, 0xD6}},     {40, {0xE4, 0xC4}},      {41, {0xA7, 0xBD}},
    {43, {'\'', '*'}},      {53, {'-', '_'}},        {CK_102ND, {'<', '>', '|'}},
};

// The Norwegian table has the ¨ ^ ~ key the kernel desktop's left as the US bracket.
static const struct compositor_patch compositor_no[] = {
    {3, {'2', '"', '@'}},   {4, {'3', '#', 0xA3}},   {5, {'4', 0xA4, '$'}},
    {6, {'5', '%', 0x20AC}}, {8, {'7', '/', '{'}},   {9, {'8', '(', '['}},
    {10, {'9', ')', ']'}},  {11, {'0', '=', '}'}},   {12, {'+', '?'}},
    {13, {'\\', D(CK_GRAVE), D(CK_ACUTE)}},          {26, {0xE5, 0xC5}},
    {27, {D(CK_DIAERESIS), D(CK_CIRCUMFLEX), D(CK_TILDE)}},
    {39, {0xF8, 0xD8}},     {40, {0xE6, 0xC6}},      {41, {'|', 0xA7}},
    {43, {'\'', '*'}},      {53, {'-', '_'}},        {CK_102ND, {'<', '>'}},
};

static const struct compositor_patch compositor_fr[] = {
    {2, {'&', '1'}},         {3, {0xE9, '2', '~'}},   {4, {'"', '3', '#'}},
    {5, {'\'', '4', '{'}},   {6, {'(', '5', '['}},    {7, {'-', '6', '|'}},
    {8, {0xE8, '7', '`'}},   {9, {'_', '8', '\\'}},   {10, {0xE7, '9', '^'}},
    {11, {0xE0, '0', '@'}},  {12, {')', 0xB0, ']'}},  {13, {'=', '+', '}'}},
    {16, {'a', 'A'}},        {17, {'z', 'Z'}},        {18, {'e', 'E', 0x20AC}},
    {26, {D(CK_CIRCUMFLEX), D(CK_DIAERESIS)}},        {27, {'$', 0xA3, 0xA4}},
    {30, {'q', 'Q'}},        {38, {'m', 'M'}},        {39, {0xF9, '%'}},
    {40, {'*', 0xB5}},       {44, {'w', 'W'}},        {50, {',', '?'}},
    {51, {';', '.'}},        {52, {':', '/'}},        {53, {'!', 0xA7}},
    {CK_102ND, {'<', '>'}},
};

static const struct compositor_patch compositor_es[] = {
    {3, {'2', '"', '@'}},    {4, {'3', 0xB7, '#'}},   {8, {'7', '/', '{'}},
    {11, {'0', '=', '}'}},   {12, {'\'', '?'}},       {13, {0xA1, 0xBF}},
    {26, {D(CK_GRAVE), D(CK_CIRCUMFLEX), '['}},       {27, {'+', '*', ']'}},
    {39, {0xF1, 0xD1}},      {40, {D(CK_ACUTE), D(CK_DIAERESIS), '{'}},
    {41, {0xBA, 0xAA, '\\'}}, {43, {0xE7, 0xC7, '}'}}, {CK_102ND, {'<', '>'}},
    {18, {'e', 'E', 0x20AC}},
};

static const struct compositor_patch compositor_it[] = {
    {3, {'2', '"'}},         {4, {'3', 0xA3}},        {8, {'7', '{'}},
    {9, {'8', '['}},         {10, {'9', ']'}},        {11, {'0', '}'}},
    {12, {'\'', '?'}},       {13, {0xEC, '^'}},       {26, {0xE8, 0xE9, '['}},
    {27, {'+', '*', ']'}},   {39, {0xF2, 0xE7, '@'}}, {40, {0xE0, 0xB0, '#'}},
    {41, {'\\', '|'}},       {43, {0xF9, 0xA7}},      {CK_102ND, {'<', '>'}},
    {18, {'e', 'E', 0x20AC}},
};

// Arabic: the letters of the standard 101-key layout (XKB's ara) on the keys of their place, the marks (the vowel signs, shadda, sukun) and
// the signs of the script at Shift, the digits left as they are: the person's numbers are the Western ones until they choose others.
static const struct compositor_patch compositor_ar[] = {
    {41, {0x630, 0x651}},   {16, {0x636, 0x64E}},   {17, {0x635, 0x64B}},   {18, {0x62B, 0x64F}},   {19, {0x642, 0x64C}},
    {20, {0x641, 0xFEF9}},  {21, {0x63A, 0x625}},   {22, {0x639, '`'}},     {23, {0x647, 0xF7}},    {24, {0x62E, 0xD7}},
    {25, {0x62D, 0x61B}},   {26, {0x62C, '<'}},     {27, {0x62F, '>'}},     {30, {0x634, 0x650}},   {31, {0x633, 0x64D}},
    {32, {0x64A, ']'}},     {33, {0x628, '['}},     {34, {0x644, 0xFEF7}},  {35, {0x627, 0x623}},   {36, {0x62A, 0x640}},
    {37, {0x646, 0x60C}},   {38, {0x645, '/'}},     {39, {0x643, ':'}},     {40, {0x637, '"'}},     {44, {0x626, '~'}},
    {45, {0x621, 0x652}},   {46, {0x624, '}'}},     {47, {0x631, '{'}},     {48, {0xFEFB, 0xFEF5}}, {49, {0x649, 0x622}},
    {50, {0x629, '\''}},    {51, {0x648, ','}},     {52, {0x632, '.'}},     {53, {0x638, 0x61F}},
};

// Hebrew: the standard layout (XKB's il, SI 1452); a letter has no other case, so Shift on one is the letter.
static const struct compositor_patch compositor_il[] = {
    {41, {';', '~'}},       {16, {'/', '/'}},       {17, {'\'', '\''}},     {18, {0x5E7}},          {19, {0x5E8}},
    {20, {0x5D0}},          {21, {0x5D8}},          {22, {0x5D5}},          {23, {0x5DF}},          {24, {0x5DD}},
    {25, {0x5E4}},          {26, {']', '}'}},       {27, {'[', '{'}},       {30, {0x5E9}},          {31, {0x5D3}},
    {32, {0x5D2}},          {33, {0x5DB}},          {34, {0x5E2}},          {35, {0x5D9}},          {36, {0x5D7}},
    {37, {0x5DC}},          {38, {0x5DA}},          {39, {0x5E3, ':'}},     {40, {',', '"'}},       {44, {0x5D6}},
    {45, {0x5E1}},          {46, {0x5D1}},          {47, {0x5D4}},          {48, {0x5E0}},          {49, {0x5DE}},
    {50, {0x5E6}},          {51, {0x5EA, '<'}},     {52, {0x5E5, '>'}},     {53, {'.', '?'}},
};

// Persian: the standard layout (XKB's ir): the Arabic letters of the 101-key layout, with the four Persian ones (pe, che, ke, ge) and ye.
static const struct compositor_patch compositor_ir[] = {
    {16, {0x636}},          {17, {0x635}},          {18, {0x62B}},          {19, {0x642}},          {20, {0x641}},
    {21, {0x63A}},          {22, {0x639}},          {23, {0x647}},          {24, {0x62E}},          {25, {0x62D}},
    {26, {0x62C}},          {27, {0x686}},          {30, {0x634}},          {31, {0x633}},          {32, {0x6CC}},
    {33, {0x628}},          {34, {0x644}},          {35, {0x627}},          {36, {0x62A}},          {37, {0x646}},
    {38, {0x645}},          {39, {0x6A9, ':'}},     {40, {0x6AF, '"'}},     {44, {0x638}},          {45, {0x637}},
    {46, {0x632}},          {47, {0x631}},          {48, {0x630}},          {49, {0x62F}},          {50, {0x67E}},
    {51, {0x648, '<'}},     {52, {'.', '>'}},       {53, {'/', '?'}},
};

#define CK_LAYOUT(name, table) {name, table, sizeof(table) / sizeof((table)[0])}
static const struct
{
        char name[3];
        const struct compositor_patch *patch;
        unsigned char count;
} compositor_layouts[] = {
    {"us", 0, 0},
    CK_LAYOUT("uk", compositor_uk), CK_LAYOUT("gb", compositor_uk), CK_LAYOUT("de", compositor_de),
    CK_LAYOUT("se", compositor_se), CK_LAYOUT("sv", compositor_se), CK_LAYOUT("fi", compositor_se),
    CK_LAYOUT("no", compositor_no), CK_LAYOUT("nb", compositor_no), CK_LAYOUT("dk", compositor_no),
    CK_LAYOUT("fr", compositor_fr), CK_LAYOUT("es", compositor_es), CK_LAYOUT("it", compositor_it),
    CK_LAYOUT("ar", compositor_ar), CK_LAYOUT("il", compositor_il), CK_LAYOUT("ir", compositor_ir),
};

// A dead accent and the letters it goes on, and what they make (Latin-1).
static const struct
{
        unsigned short accent;
        char bases[13];
        unsigned char made[12];
} compositor_composed[] = {
    {CK_ACUTE, "aeiouyAEIOUY", {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD}},
    {CK_GRAVE, "aeiouAEIOU", {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9}},
    {CK_DIAERESIS, "aeiouyAEIOU", {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC}},
    {CK_CIRCUMFLEX, "aeiouAEIOU", {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB}},
    {CK_TILDE, "anoANO", {0xE3, 0xF1, 0xF5, 0xC3, 0xD1, 0xD5}},
};
#undef D

// The keys that are modifiers, as the bit each is held in: the held word is
// per device, so that one keyboard letting go of Shift is not another's.
#define CK_HELD_LSHIFT 1u
#define CK_HELD_RSHIFT 2u
#define CK_HELD_LCONTROL 4u
#define CK_HELD_RCONTROL 8u
#define CK_HELD_ALT 16u
#define CK_HELD_ALTGR 32u
#define CK_HELD_LSUPER 64u
#define CK_HELD_RSUPER 128u

static unsigned int compositor_held_bit(unsigned int code)
{
        switch (code)
        {
        case 42: return CK_HELD_LSHIFT;
        case 54: return CK_HELD_RSHIFT;
        case 29: return CK_HELD_LCONTROL;
        case 97: return CK_HELD_RCONTROL;
        case 56: return CK_HELD_ALT;
        case 100: return CK_HELD_ALTGR; // the right Alt is AltGr, the right Control is Control
        case 125: return CK_HELD_LSUPER;
        case 126: return CK_HELD_RSUPER;
        }
        return 0;
}

// What is held, as the flags a key event carries.
static unsigned int compositor_held_flags(unsigned int held)
{
        return (held & (CK_HELD_LSHIFT | CK_HELD_RSHIFT) ? WINDOW_KEY_SHIFT : 0) |
               (held & (CK_HELD_LCONTROL | CK_HELD_RCONTROL) ? WINDOW_KEY_CONTROL : 0) |
               (held & CK_HELD_ALT ? WINDOW_KEY_ALT : 0) |
               (held & CK_HELD_ALTGR ? WINDOW_KEY_ALTGR : 0) |
               (held & (CK_HELD_LSUPER | CK_HELD_RSUPER) ? WINDOW_KEY_SUPER : 0);
}

struct compositor_keymap
{
        unsigned char layout;    // an index into compositor_layouts
        unsigned char caps;      // Caps Lock is on
        unsigned int dead;       // the accent that waits for a letter, or 0
        unsigned int hidden[4];  // keys whose press was taken and so whose release is
};

// A layout by name, or -1; the name it is kept under is what asked for it.
static int compositor_layout_index(const char *name)
{
        for (unsigned int at = 0; at < sizeof(compositor_layouts) / sizeof(compositor_layouts[0]); at++)
                if (name[0] == compositor_layouts[at].name[0] && name[1] == compositor_layouts[at].name[1] &&
                    !name[2])
                        return (int)at;
        return -1;
}

// The case of a letter turned over, which is what Caps Lock does to one.
static unsigned int compositor_letter_flip(unsigned int c)
{
        if ((c | 0x20) - 'a' < 26u)
                return c ^ 0x20;
        if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
                return c ^ 0x20;
        return c;
}

// What a key says at the levels the modifiers pick, with CK_DEAD on an accent.
static unsigned int compositor_keymap_character(const struct compositor_keymap *map, unsigned int code,
                                                unsigned int modifiers)
{
        const struct compositor_patch *patch = compositor_layouts[map->layout].patch;
        unsigned int count = compositor_layouts[map->layout].count;
        unsigned int level[CK_LEVELS] = {0, 0, 0, 0};
        unsigned int at, want = 0, c;

        if (code >= CK_TABLE)
                return 0;

        level[0] = compositor_us[code][0];
        level[1] = compositor_us[code][1];
        for (at = 0; at < count; at++)
                if (patch[at].code == code)
                        memory_copy(level, patch[at].level, sizeof(level));

        if (modifiers & WINDOW_KEY_ALTGR)
                want += 2;
        if (modifiers & WINDOW_KEY_SHIFT)
                want += 1;

        // A level the layout leaves empty is the one below it.
        c = level[want];
        if (!c && want)
                c = level[want & 2];
        if (!c && (modifiers & WINDOW_KEY_SHIFT))
                c = level[1];
        if (!c)
                c = level[0];
        if (!c)
                return 0;

        // A chord with Control is the key's Latin letter, whatever the layout types: Ctrl+C copies on an Arabic or a Hebrew keyboard.
        if ((modifiers & WINDOW_KEY_CONTROL) && c > 0x7f && !(c & CK_DEAD))
                c = compositor_us[code][(modifiers & WINDOW_KEY_SHIFT) != 0];

        if (map->caps && !(c & CK_DEAD))
                c = compositor_letter_flip(c);

        // Control turns a letter into the control code that letter names,
        // which is the whole of why a terminal wants a modifier at all.
        if ((modifiers & WINDOW_KEY_CONTROL) && !(c & CK_DEAD) && (c | 0x20) - 'a' < 26u)
                return c & 0x1f;

        return c;
}

// What a dead accent and a character make together, or 0 when they make nothing.
static unsigned int compositor_keymap_compose(unsigned int accent, unsigned int c)
{
        if (c == ' ' || c == accent)
                return accent;

        for (unsigned int row = 0; row < sizeof(compositor_composed) / sizeof(compositor_composed[0]); row++)
                if (compositor_composed[row].accent == accent)
                        for (unsigned int at = 0; compositor_composed[row].bases[at]; at++)
                                if (c < 128 && compositor_composed[row].bases[at] == (char)c)
                                        return compositor_composed[row].made[at];
        return 0;
}

/*
        A key went down (value 1), repeated (2) or came up (0), with these
        modifiers held. What comes back is how many characters it makes, which
        go out one event each: none for a key that means nothing, a release
        or a modifier; one, and with a dead accent waiting a letter that
        takes it is that letter made; two when the letter takes no accent, as
        the accent and then the letter, so that nothing typed is lost. A dead
        key makes none and is taken whole -- its release and its repeats too --
        and the answer is -1 for a key that is not to be delivered at all.
        A key that is not a character (an arrow, a function key, Escape,
        Return, Backspace) leaves a waiting accent alone or, for those that
        are control characters, ends it.
*/
static int compositor_keymap_event(struct compositor_keymap *map, unsigned int code, int value,
                                   unsigned int modifiers, unsigned int out[2])
{
        unsigned int bit = 1u << (code & 31), c, made;
        int taken = code < 128 && (map->hidden[code >> 5] & bit);

        if (code >= 128)
                return 0;

        if (!value)
        {
                map->hidden[code >> 5] &= ~bit;
                return taken ? -1 : 0;
        }

        if (taken)
                return -1;

        if (code == CK_CAPSLOCK)
        {
                if (value == 1)
                        map->caps ^= 1;
                return 0;
        }

        c = compositor_keymap_character(map, code, modifiers);
        if (!c)
                return 0;

        if (c & CK_DEAD)
        {
                unsigned int accent = c & ~CK_DEAD, was = map->dead;

                map->hidden[code >> 5] |= bit;
                map->dead = accent;
                if (!was)
                        return -1;

                // Typed, so it is a press and its release goes out with it.
                map->hidden[code >> 5] &= ~bit;
                if (was == accent)
                        map->dead = 0;
                out[0] = was; // twice the same is that accent; another types the first
                return 1;
        }

        if (!map->dead)
        {
                out[0] = c;
                return 1;
        }

        if (c < ' ' || c == 127)
        {
                map->dead = 0;
                out[0] = c;
                return 1;
        }

        made = compositor_keymap_compose(map->dead, c);
        if (made)
        {
                map->dead = 0;
                out[0] = made;
                return 1;
        }

        out[0] = map->dead;
        out[1] = c;
        map->dead = 0;
        return 2;
}

/* ---- end of the keymap ---- */

