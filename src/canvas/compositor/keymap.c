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
#define CK_KEY_F11 87
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

struct compositor_keymap
{
        unsigned char layout;    // an index into compositor_layouts
        unsigned char caps;      // Caps Lock is on
        unsigned int dead;       // the accent that waits for a letter, or 0
        unsigned int hidden[4];  // keys whose press was taken and so whose release is
};

#define COMPOSITOR_KEYMAP_EVAL_ASM_IMPLEMENTATION
#if defined(__has_include)
# if __has_include("keymap_eval.asm.c")
#  include "keymap_eval.asm.c"
# elif __has_include("compositor/keymap_eval.asm.c")
#  include "compositor/keymap_eval.asm.c"
# else
#  include "src/canvas/compositor/keymap_eval.asm.c"
# endif
#else
# include "keymap_eval.asm.c"
#endif

_Static_assert(sizeof(struct compositor_patch) == sizeof(struct km_patch), "compositor_patch != km_patch size");
_Static_assert(KM_EVAL_OFFSET(struct compositor_patch, code) == KM_EVAL_OFFSET(struct km_patch, code) &&
               KM_EVAL_OFFSET(struct compositor_patch, level) == KM_EVAL_OFFSET(struct km_patch, level),
               "compositor_patch != km_patch layout");
_Static_assert(sizeof(compositor_layouts[0]) == sizeof(struct km_layout), "compositor_layouts != km_layout size");
_Static_assert(sizeof(compositor_composed[0]) == sizeof(struct km_composed), "compositor_composed != km_composed size");
_Static_assert(sizeof(struct compositor_keymap) == sizeof(struct km_state), "compositor_keymap != km_state size");
_Static_assert(KM_EVAL_OFFSET(struct compositor_keymap, layout) == KM_EVAL_OFFSET(struct km_state, layout) &&
               KM_EVAL_OFFSET(struct compositor_keymap, caps) == KM_EVAL_OFFSET(struct km_state, caps) &&
               KM_EVAL_OFFSET(struct compositor_keymap, dead) == KM_EVAL_OFFSET(struct km_state, dead) &&
               KM_EVAL_OFFSET(struct compositor_keymap, hidden) == KM_EVAL_OFFSET(struct km_state, hidden),
               "compositor_keymap != km_state layout");
_Static_assert(CK_DEAD == KM_DEAD && CK_TABLE == KM_TABLE && CK_LEVELS == KM_LEVELS && CK_CAPSLOCK == KM_CAPSLOCK,
               "CK_* != KM_* constants");
_Static_assert(WINDOW_KEY_SHIFT == KM_MOD_SHIFT && WINDOW_KEY_CONTROL == KM_MOD_CONTROL &&
               WINDOW_KEY_ALT == KM_MOD_ALT && WINDOW_KEY_ALTGR == KM_MOD_ALTGR &&
               WINDOW_KEY_SUPER == KM_MOD_SUPER,
               "WINDOW_KEY_* != KM_MOD_* flags");

static const struct km_tables compositor_km_tables = {
    &compositor_us[0][0],
    (const struct km_layout *)(const void *)compositor_layouts,
    (const struct km_composed *)(const void *)compositor_composed,
    (km_u32)(sizeof(compositor_layouts) / sizeof(compositor_layouts[0])),
    (km_u32)(sizeof(compositor_composed) / sizeof(compositor_composed[0])),
};

static unsigned int compositor_held_bit(unsigned int code)
{
        return km_held_bit(code);
}

// What is held, as the flags a key event carries.
static unsigned int compositor_held_flags(unsigned int held)
{
        return km_held_flags(held);
}

// A layout by name, or -1; the name it is kept under is what asked for it.
static int compositor_layout_index(const char *name)
{
        return km_layout_index(name, compositor_km_tables.layouts, compositor_km_tables.layout_count);
}

// The case of a letter turned over, which is what Caps Lock does to one.
static unsigned int compositor_letter_flip(unsigned int c)
{
        return km_letter_flip(c);
}

// What a key says at the levels the modifiers pick, with CK_DEAD on an accent.
static unsigned int compositor_keymap_character(const struct compositor_keymap *map, unsigned int code,
                                                unsigned int modifiers)
{
        return km_character(&compositor_km_tables, (const struct km_state *)(const void *)map, code, modifiers);
}

// What a dead accent and a character make together, or 0 when they make nothing.
static unsigned int compositor_keymap_compose(unsigned int accent, unsigned int c)
{
        return km_compose(accent, c, compositor_km_tables.composed, compositor_km_tables.composed_count);
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
        return km_event(&compositor_km_tables, (struct km_state *)(void *)map, code, value, modifiers, out);
}

/*
        Self-contained XKB keymap synthesizer directly from compositor_layouts[] / compositor_keymap_character():
        pure function of (layout, into, room), returning byte length written (0 on overflow), usable by Wayland,
        Xwayland, and any external client protocol without duplicating keymap tables.
*/
struct compositor_xkb_buf
{
        char *at;
        unsigned long room, used;
        int full;
};

static void compositor_xkb_put(struct compositor_xkb_buf *b, const char *s)
{
        while (*s)
        {
                if (b->used + 1 >= b->room)
                {
                        b->full = 1;
                        return;
                }
                b->at[b->used++] = *s++;
        }
}

static void compositor_xkb_u32(struct compositor_xkb_buf *b, unsigned int v)
{
        char buf[12];
        unsigned int n = 0;

        do
                buf[n++] = (char)('0' + v % 10);
        while (v /= 10);
        while (n)
        {
                char ch[2] = {buf[--n], 0};

                compositor_xkb_put(b, ch);
        }
}

static void compositor_xkb_sym(struct compositor_xkb_buf *b, unsigned int c)
{
        static const struct
        {
                unsigned int accent;
                const char *name;
        } dead[] = {
            {CK_ACUTE, "dead_acute"},           {CK_GRAVE, "dead_grave"},
            {CK_DIAERESIS, "dead_diaeresis"},   {CK_CIRCUMFLEX, "dead_circumflex"},
            {CK_TILDE, "dead_tilde"},
        };

        if (c & CK_DEAD)
        {
                unsigned int a = c & ~CK_DEAD;

                for (unsigned int i = 0; i < sizeof(dead) / sizeof(dead[0]); i++)
                        if (dead[i].accent == a)
                        {
                                compositor_xkb_put(b, dead[i].name);
                                return;
                        }
                compositor_xkb_put(b, "NoSymbol");
                return;
        }
        if (!c)
        {
                compositor_xkb_put(b, "NoSymbol");
                return;
        }
        {
                char hex[8] = {'U', 0, 0, 0, 0, 0, 0, 0};
                int d = c > 0xffffu ? 6 : 4;

                for (int i = 0; i < d; i++)
                        hex[1 + i] = "0123456789abcdef"[(c >> (4 * (d - 1 - i))) & 15u];
                compositor_xkb_put(b, hex);
        }
}

static unsigned long compositor_keymap_xkb_build(unsigned int layout, char *into, unsigned long room)
{
        static const unsigned char codes[] = {
            1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,
            23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
            45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,
            67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  86,  87,  88,  96,  97,
            98,  99,  100, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 119, 125, 126, 127,
        };
        static const struct
        {
                unsigned char code;
                const char *name;
        } named[] = {
            {1, "Escape"},      {14, "BackSpace"}, {15, "Tab"},        {28, "Return"},     {29, "Control_L"},
            {42, "Shift_L"},    {54, "Shift_R"},   {56, "Alt_L"},      {58, "Caps_Lock"},  {59, "F1"},
            {60, "F2"},         {61, "F3"},        {62, "F4"},         {63, "F5"},         {64, "F6"},
            {65, "F7"},         {66, "F8"},        {67, "F9"},         {68, "F10"},        {69, "Num_Lock"},
            {70, "Scroll_Lock"}, {87, "F11"},      {88, "F12"},        {96, "KP_Enter"},   {97, "Control_R"},
            {98, "KP_Divide"},  {99, "Print"},     {100, "ISO_Level3_Shift"}, {102, "Home"}, {103, "Up"},
            {104, "Prior"},     {105, "Left"},     {106, "Right"},     {107, "End"},       {108, "Down"},
            {109, "Next"},      {110, "Insert"},   {111, "Delete"},    {119, "Pause"},     {125, "Super_L"},
            {126, "Super_R"},   {127, "Menu"},
        };
        static const struct
        {
                unsigned char code;
                const char *plain, *locked;
        } keypad[] = {
            {71, "KP_Home", "KP_7"},   {72, "KP_Up", "KP_8"},    {73, "KP_Prior", "KP_9"}, {75, "KP_Left", "KP_4"},
            {76, "KP_Begin", "KP_5"},  {77, "KP_Right", "KP_6"}, {79, "KP_End", "KP_1"},   {80, "KP_Down", "KP_2"},
            {81, "KP_Next", "KP_3"},   {82, "KP_Insert", "KP_0"}, {83, "KP_Delete", "KP_Decimal"},
        };
        static const char mid[] =
            "  indicator 1 = \"Caps Lock\";\n  indicator 2 = \"Num Lock\";\n};\n"
            "xkb_types \"canvas\" {\n  virtual_modifiers NumLock,LevelThree;\n"
            "  type \"ONE_LEVEL\" { modifiers = none; map[none] = Level1; };\n"
            "  type \"TWO_LEVEL\" { modifiers = Shift; map[Shift] = Level2; };\n"
            "  type \"ALPHABETIC\" { modifiers = Shift+Lock; map[Shift] = Level2; map[Lock] = Level2; };\n"
            "  type \"FOUR_LEVEL\" { modifiers = Shift+LevelThree; map[Shift] = Level2; map[LevelThree] = Level3;"
            " map[Shift+LevelThree] = Level4; };\n"
            "  type \"FOUR_LEVEL_ALPHABETIC\" { modifiers = Shift+Lock+LevelThree; map[Shift] = Level2;"
            " map[Lock] = Level2; map[LevelThree] = Level3; map[Shift+LevelThree] = Level4;"
            " map[Lock+LevelThree] = Level4; map[Shift+Lock+LevelThree] = Level3; };\n"
            "  type \"KEYPAD\" { modifiers = Shift+NumLock; map[none] = Level1; map[Shift] = Level2;"
            " map[NumLock] = Level2; map[Shift+NumLock] = Level1; };\n};\n"
            "xkb_compat \"canvas\" {\n  virtual_modifiers NumLock,LevelThree;\n"
            "  interpret.useModMapMods = AnyLevel;\n  interpret.repeat = False;\n"
            "  interpret Shift_L { action = SetMods(modifiers = Shift); };\n"
            "  interpret Shift_R { action = SetMods(modifiers = Shift); };\n"
            "  interpret Control_L { action = SetMods(modifiers = Control); };\n"
            "  interpret Control_R { action = SetMods(modifiers = Control); };\n"
            "  interpret Alt_L { action = SetMods(modifiers = Mod1); };\n"
            "  interpret Super_L { action = SetMods(modifiers = Mod4); };\n"
            "  interpret Super_R { action = SetMods(modifiers = Mod4); };\n"
            "  interpret Caps_Lock { action = LockMods(modifiers = Lock); };\n"
            "  interpret Num_Lock { virtualModifier = NumLock; action = LockMods(modifiers = NumLock); };\n"
            "  interpret ISO_Level3_Shift { virtualModifier = LevelThree; action = SetMods(modifiers = LevelThree); };\n"
            "  indicator \"Caps Lock\" { !allowExplicit; whichModState = Locked; modifiers = Lock; };\n"
            "  indicator \"Num Lock\" { !allowExplicit; whichModState = Locked; modifiers = NumLock; };\n};\n"
            "xkb_symbols \"canvas\" {\n  name[Group1] = \"Canvas\";\n";
        static const char tail[] =
            "  modifier_map Shift { <K42>, <K54> };\n  modifier_map Lock { <K58> };\n"
            "  modifier_map Control { <K29>, <K97> };\n  modifier_map Mod1 { <K56> };\n"
            "  modifier_map Mod2 { <K69> };\n  modifier_map Mod4 { <K125>, <K126> };\n"
            "  modifier_map Mod5 { <K100> };\n};\n};\n";
        struct compositor_xkb_buf b = {into, room, 0, 0};
        struct compositor_keymap map = {0};

        if (layout < sizeof(compositor_layouts) / sizeof(compositor_layouts[0]))
                map.layout = (unsigned char)layout;
        compositor_xkb_put(&b, "xkb_keymap {\nxkb_keycodes \"canvas\" {\n  minimum = 8;\n  maximum = 255;\n");
        for (unsigned long i = 0; i < sizeof(codes); i++)
        {
                compositor_xkb_put(&b, "  <K");
                compositor_xkb_u32(&b, codes[i]);
                compositor_xkb_put(&b, "> = ");
                compositor_xkb_u32(&b, (unsigned int)codes[i] + 8u);
                compositor_xkb_put(&b, ";\n");
        }
        compositor_xkb_put(&b, mid);
        for (unsigned long i = 0; i < sizeof(codes); i++)
        {
                unsigned int code = codes[i], lv[4];
                const char *nm = 0, *kp_p = 0, *kp_l = 0;

                for (unsigned int k = 0; k < sizeof(named) / sizeof(named[0]); k++)
                        if (named[k].code == code)
                                nm = named[k].name;
                for (unsigned int k = 0; !nm && k < sizeof(keypad) / sizeof(keypad[0]); k++)
                        if (keypad[k].code == code)
                                kp_p = keypad[k].plain, kp_l = keypad[k].locked;
                compositor_xkb_put(&b, "  key <K");
                compositor_xkb_u32(&b, code);
                compositor_xkb_put(&b, "> { ");
                if (nm)
                {
                        compositor_xkb_put(&b, "[ ");
                        compositor_xkb_put(&b, nm);
                        compositor_xkb_put(&b, " ] };\n");
                        continue;
                }
                if (kp_p)
                {
                        compositor_xkb_put(&b, "type = \"KEYPAD\", [ ");
                        compositor_xkb_put(&b, kp_p);
                        compositor_xkb_put(&b, ", ");
                        compositor_xkb_put(&b, kp_l);
                        compositor_xkb_put(&b, " ] };\n");
                        continue;
                }
                lv[0] = compositor_keymap_character(&map, code, 0);
                lv[1] = compositor_keymap_character(&map, code, WINDOW_KEY_SHIFT);
                lv[2] = compositor_keymap_character(&map, code, WINDOW_KEY_ALTGR);
                lv[3] = compositor_keymap_character(&map, code, WINDOW_KEY_SHIFT | WINDOW_KEY_ALTGR);
                {
                        int alpha = lv[0] && lv[1] && !(lv[0] & CK_DEAD) && !(lv[1] & CK_DEAD) &&
                                    compositor_letter_flip(lv[0]) == lv[1] && lv[0] != lv[1];
                        int four = lv[2] != lv[0] || lv[3] != lv[1];
                        int n = four ? 4 : lv[0] == lv[1] ? 1 : 2;

                        compositor_xkb_put(&b, "type = \"");
                        compositor_xkb_put(&b, four ? (alpha ? "FOUR_LEVEL_ALPHABETIC" : "FOUR_LEVEL")
                                                    : n == 1 ? "ONE_LEVEL" : alpha ? "ALPHABETIC" : "TWO_LEVEL");
                        compositor_xkb_put(&b, "\", [ ");
                        for (int k = 0; k < n; k++)
                        {
                                if (k)
                                        compositor_xkb_put(&b, ", ");
                                compositor_xkb_sym(&b, lv[k]);
                        }
                        compositor_xkb_put(&b, " ] };\n");
                }
        }
        compositor_xkb_put(&b, tail);
        if (b.full || !room || b.used + 1 > room)
                return 0;
        into[b.used] = 0;
        return b.used + 1;
}

/* ---- end of the keymap ---- */

