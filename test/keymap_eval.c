/*
        Differential C-reference parity, security, guard-page, fuzz & throughput/size
        benchmark for src/canvas/compositor/keymap_eval.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/keymap_eval.c -o /tmp/mw_keymap_eval && /tmp/mw_keymap_eval
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/keymap_eval.c -o /tmp/mw_keymap_eval_asan && /tmp/mw_keymap_eval_asan
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define COMPOSITOR_KEYMAP_EVAL_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/keymap_eval.asm.c"

/* ── Real Moonwater keymap tables (exact copy from src/canvas/compositor/keymap.c) ── */

#define T_ACUTE      0xB4u
#define T_GRAVE      '`'
#define T_DIAERESIS  0xA8u
#define T_CIRCUMFLEX '^'
#define T_TILDE      '~'
#define TD(c)        (KM_DEAD | (c))

static const km_u8 t_us[KM_TABLE][2] = {
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

static const struct km_patch t_uk[] = {
    {3, {'2', '"', '@', 0}}, {4, {'3', 0xA3, 0xA3, 0}}, {40, {'\'', '@', 0, 0}},
    {41, {'`', 0xAC, 0xA6, 0}}, {43, {'#', '~', '\\', 0}}, {86, {'\\', '|', 0, 0}},
};

static const struct km_patch t_de[] = {
    {12, {0xDF, '?', '\\', 0}}, {13, {TD(T_ACUTE), TD(T_GRAVE), 0, 0}}, {21, {'z', 'Z', 0, 0}},
    {26, {0xFC, 0xDC, 0, 0}},   {27, {'+', '*', '~', 0}},               {39, {0xF6, 0xD6, 0, 0}},
    {40, {0xE4, 0xC4, 0, 0}},   {41, {TD(T_CIRCUMFLEX), 0xB0, 0, 0}},   {43, {'#', '\'', 0, 0}},
    {44, {'y', 'Y', 0, 0}},     {86, {'<', '>', '|', 0}},               {8, {'7', '/', '{', 0}},
    {9, {'8', '(', '[', 0}},    {10, {'9', ')', ']', 0}},               {11, {'0', '=', '}', 0}},
    {16, {'q', 'Q', '@', 0}},   {18, {'e', 'E', 0x20AC, 0}},
};

static const struct km_patch t_se[] = {
    {3, {'2', '"', '@', 0}},   {4, {'3', '#', 0xA3, 0}},   {5, {'4', 0xA4, '$', 0}},
    {6, {'5', '%', 0x20AC, 0}}, {7, {'6', '&', 0, 0}},     {8, {'7', '/', '{', 0}},
    {9, {'8', '(', '[', 0}},   {10, {'9', ')', ']', 0}},   {11, {'0', '=', '}', 0}},
    {12, {'+', '?', '\\', 0}}, {13, {TD(T_ACUTE), TD(T_GRAVE), 0, 0}},
    {26, {0xE5, 0xC5, 0, 0}},  {27, {TD(T_DIAERESIS), TD(T_CIRCUMFLEX), TD(T_TILDE), 0}},
    {39, {0xF6, 0xD6, 0, 0}},  {40, {0xE4, 0xC4, 0, 0}},   {41, {0xA7, 0xBD, 0, 0}},
    {43, {'\'', '*', 0, 0}},   {53, {'-', '_', 0, 0}},     {86, {'<', '>', '|', 0}},
};

static const struct km_patch t_no[] = {
    {3, {'2', '"', '@', 0}},   {4, {'3', '#', 0xA3, 0}},   {5, {'4', 0xA4, '$', 0}},
    {6, {'5', '%', 0x20AC, 0}}, {8, {'7', '/', '{', 0}},   {9, {'8', '(', '[', 0}},
    {10, {'9', ')', ']', 0}},  {11, {'0', '=', '}', 0}},   {12, {'+', '?', 0, 0}},
    {13, {'\\', TD(T_GRAVE), TD(T_ACUTE), 0}},             {26, {0xE5, 0xC5, 0, 0}},
    {27, {TD(T_DIAERESIS), TD(T_CIRCUMFLEX), TD(T_TILDE), 0}},
    {39, {0xF8, 0xD8, 0, 0}},  {40, {0xE6, 0xC6, 0, 0}},   {41, {'|', 0xA7, 0, 0}},
    {43, {'\'', '*', 0, 0}},   {53, {'-', '_', 0, 0}},     {86, {'<', '>', 0, 0}},
};

static const struct km_patch t_fr[] = {
    {2, {'&', '1', 0, 0}},       {3, {0xE9, '2', '~', 0}},   {4, {'"', '3', '#', 0}},
    {5, {'\'', '4', '{', 0}},    {6, {'(', '5', '[', 0}},    {7, {'-', '6', '|', 0}},
    {8, {0xE8, '7', '`', 0}},    {9, {'_', '8', '\\', 0}},   {10, {0xE7, '9', '^', 0}},
    {11, {0xE0, '0', '@', 0}},   {12, {')', 0xB0, ']', 0}},  {13, {'=', '+', '}', 0}},
    {16, {'a', 'A', 0, 0}},      {17, {'z', 'Z', 0, 0}},     {18, {'e', 'E', 0x20AC, 0}},
    {26, {TD(T_CIRCUMFLEX), TD(T_DIAERESIS), 0, 0}},         {27, {'$', 0xA3, 0xA4, 0}},
    {30, {'q', 'Q', 0, 0}},      {38, {'m', 'M', 0, 0}},     {39, {0xF9, '%', 0, 0}},
    {40, {'*', 0xB5, 0, 0}},     {44, {'w', 'W', 0, 0}},     {50, {',', '?', 0, 0}},
    {51, {';', '.', 0, 0}},      {52, {':', '/', 0, 0}},     {53, {'!', 0xA7, 0, 0}},
    {86, {'<', '>', 0, 0}},
};

static const struct km_patch t_ar[] = {
    {41, {0x630, 0x651, 0, 0}}, {16, {0x636, 0x64E, 0, 0}}, {17, {0x635, 0x64B, 0, 0}},
    {18, {0x62B, 0x64F, 0, 0}}, {46, {0x624, '}', 0, 0}},   {48, {0xFEFB, 0xFEF5, 0, 0}},
};

static const struct km_patch t_il[] = {
    {18, {0x5E7, 0, 0, 0}}, {19, {0x5E8, 0, 0, 0}}, {30, {0x5E9, 0, 0, 0}}, {46, {0x5D1, 0, 0, 0}},
};

/* Synthetic 4-level patch to exercise level[3] (Shift+AltGr) and fallback chains */
static const struct km_patch t_l4[] = {
    {16, {'a', 'A', 0xE1, 0xC1}},
    {17, {'b', 0, 0xE2, 0}},
    {18, {0, 'E', 0, 0xC9}},
    {19, {0, 0, 0, 0xDF}},
    {20, {0, 0, 0, 0}},
    {16, {'q', 'Q', 0xE4, 0xC4}}, /* duplicate code: last wins */
};

#define TL(n, tab) {n, tab, (km_u8)(sizeof(tab) / sizeof((tab)[0]))}
static const struct km_layout t_layouts[] = {
    {"us", 0, 0},
    TL("uk", t_uk), TL("gb", t_uk), TL("de", t_de),
    TL("se", t_se), TL("sv", t_se), TL("fi", t_se),
    TL("no", t_no), TL("nb", t_no), TL("dk", t_no),
    TL("fr", t_fr), TL("ar", t_ar), TL("il", t_il),
    TL("l4", t_l4),
};

static const struct km_composed t_composed[] = {
    {T_ACUTE, "aeiouyAEIOUY", {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD}},
    {T_GRAVE, "aeiouAEIOU", {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9}},
    {T_DIAERESIS, "aeiouyAEIOU", {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC}},
    {T_CIRCUMFLEX, "aeiouAEIOU", {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB}},
    {T_TILDE, "anoANO", {0xE3, 0xF1, 0xF5, 0xC3, 0xD1, 0xD5}},
};

static const struct km_tables t_tables = {
    &t_us[0][0], t_layouts, t_composed,
    (km_u32)(sizeof(t_layouts) / sizeof(t_layouts[0])),
    (km_u32)(sizeof(t_composed) / sizeof(t_composed[0])),
};

/* ── Reference C implementations (noinline + external linkage for exact nm --print-size) ── */

__attribute__((noinline)) km_u32 ref_km_held_bit(km_u32 code)
{
        switch (code) {
        case 42: return 1u;
        case 54: return 2u;
        case 29: return 4u;
        case 97: return 8u;
        case 56: return 16u;
        case 100: return 32u;
        case 125: return 64u;
        case 126: return 128u;
        }
        return 0u;
}

__attribute__((noinline)) km_u32 ref_km_held_flags(km_u32 held)
{
        return ((held & 3u) ? KM_MOD_SHIFT : 0u) |
               ((held & 12u) ? KM_MOD_CONTROL : 0u) |
               ((held & 16u) ? KM_MOD_ALT : 0u) |
               ((held & 32u) ? KM_MOD_ALTGR : 0u) |
               ((held & 192u) ? KM_MOD_SUPER : 0u);
}

__attribute__((noinline)) km_i32 ref_km_layout_index(const char *name, const struct km_layout *layouts, km_u32 count)
{
        if (!name || !layouts || !count)
                return -1;
        if (!name[0] || !name[1] || name[2])
                return -1;
        for (km_u32 at = 0; at < count; at++)
                if (name[0] == layouts[at].name[0] && name[1] == layouts[at].name[1])
                        return (km_i32)at;
        return -1;
}

__attribute__((noinline)) km_u32 ref_km_letter_flip(km_u32 c)
{
        if ((c | 0x20u) - 'a' < 26u)
                return c ^ 0x20u;
        if ((c >= 0xE0u && c <= 0xFEu && c != 0xF7u) || (c >= 0xC0u && c <= 0xDEu && c != 0xD7u))
                return c ^ 0x20u;
        return c;
}

__attribute__((noinline)) km_u32 ref_km_compose(km_u32 accent, km_u32 c, const struct km_composed *table, km_u32 count)
{
        if (c == ' ' || c == accent)
                return accent;
        if (!table || !count || !c || c >= 128u)
                return 0u;
        for (km_u32 row = 0; row < count; row++)
                if (table[row].accent == accent)
                        for (km_u32 at = 0; at < 12u && table[row].bases[at]; at++)
                                if ((km_u8)table[row].bases[at] == (km_u8)c)
                                        return table[row].made[at];
        return 0u;
}

__attribute__((noinline)) km_u32 ref_km_character(const struct km_tables *tables, const struct km_state *map,
                                                  km_u32 code, km_u32 modifiers)
{
        km_u32 level[KM_LEVELS] = {0, 0, 0, 0};
        km_u32 want = 0, c;

        if (!tables || !map || !tables->us || code >= KM_TABLE)
                return 0u;

        level[0] = tables->us[code * 2u + 0u];
        level[1] = tables->us[code * 2u + 1u];

        if (tables->layouts && map->layout < tables->layout_count) {
                const struct km_patch *patch = tables->layouts[map->layout].patch;
                km_u32 count = tables->layouts[map->layout].count;
                if (patch)
                        for (km_u32 at = 0; at < count; at++)
                                if (patch[at].code == code)
                                        memcpy(level, patch[at].level, sizeof(level));
        }

        if (modifiers & KM_MOD_ALTGR)
                want += 2u;
        if (modifiers & KM_MOD_SHIFT)
                want += 1u;

        c = level[want];
        if (!c && want)
                c = level[want & 2u];
        if (!c && (modifiers & KM_MOD_SHIFT))
                c = level[1];
        if (!c)
                c = level[0];
        if (!c)
                return 0u;

        if ((modifiers & KM_MOD_CONTROL) && c > 0x7fu && !(c & KM_DEAD))
                c = tables->us[code * 2u + ((modifiers & KM_MOD_SHIFT) != 0u)];

        if (map->caps && !(c & KM_DEAD))
                c = ref_km_letter_flip(c);

        if ((modifiers & KM_MOD_CONTROL) && !(c & KM_DEAD) && (c | 0x20u) - 'a' < 26u)
                return c & 0x1fu;

        return c;
}

__attribute__((noinline)) km_i32 ref_km_event(const struct km_tables *tables, struct km_state *map,
                                              km_u32 code, km_i32 value, km_u32 modifiers, km_u32 out[2])
{
        if (!tables || !map || !out || code >= KM_MAX_CODE)
                return 0;

        km_u32 bit = 1u << (code & 31u);
        int taken = (map->hidden[code >> 5] & bit) != 0u;

        if (!value) {
                map->hidden[code >> 5] &= ~bit;
                return taken ? -1 : 0;
        }
        if (taken)
                return -1;
        if (code == KM_CAPSLOCK) {
                if (value == 1)
                        map->caps ^= 1u;
                return 0;
        }

        km_u32 c = ref_km_character(tables, map, code, modifiers);
        if (!c)
                return 0;

        if (c & KM_DEAD) {
                km_u32 accent = c & ~KM_DEAD, was = map->dead;
                map->hidden[code >> 5] |= bit;
                map->dead = accent;
                if (!was)
                        return -1;
                map->hidden[code >> 5] &= ~bit;
                if (was == accent)
                        map->dead = 0;
                out[0] = was;
                return 1;
        }

        if (!map->dead) {
                out[0] = c;
                return 1;
        }

        if (c < ' ' || c == 127u) {
                map->dead = 0;
                out[0] = c;
                return 1;
        }

        km_u32 made = ref_km_compose(map->dead, c, tables->composed, tables->composed_count);
        if (made) {
                map->dead = 0;
                out[0] = made;
                return 1;
        }

        out[0] = map->dead;
        out[1] = c;
        map->dead = 0;
        return 2;
}

/* ── Helpers ── */

static int failures = 0;
#define CHECK(cond, fmt, ...) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL line %d: " fmt "\n", __LINE__, ##__VA_ARGS__); \
                if (++failures > 20) exit(1); \
        } \
} while (0)

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint32_t xorshift32(void)
{
        uint64_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng_state = x;
        return (uint32_t)(x >> 32);
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Allocate a buffer sandwiched between two PROT_NONE guard pages. */
struct guarded_buf {
        void  *base;
        size_t total;
        size_t page;
};

static struct guarded_buf guarded_alloc(size_t data_pages)
{
        size_t page = (size_t)sysconf(_SC_PAGESIZE);
        size_t total = (data_pages + 2) * page;
        void *p = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) {
                perror("mmap");
                exit(1);
        }
        if (mprotect(p, page, PROT_NONE) != 0 ||
            mprotect((char *)p + (data_pages + 1) * page, page, PROT_NONE) != 0) {
                perror("mprotect");
                exit(1);
        }
        return (struct guarded_buf){p, total, page};
}

static void guarded_free(struct guarded_buf g)
{
        munmap(g.base, g.total);
}

/* ── 1. Exhaustive unit & edge-case tests ── */

static void test_primitives(void)
{
        /* km_held_bit: 0..512 + extreme u32 values */
        for (km_u32 c = 0; c <= 512; c++) {
                km_u32 a = km_held_bit(c), r = ref_km_held_bit(c);
                CHECK(a == r, "km_held_bit(%u): asm=%u ref=%u", c, a, r);
        }
        const km_u32 extremes[] = {0x7fffffffu, 0x80000000u, 0x8000002au, 0xffffffffu, 0xffffff2au};
        for (size_t i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++) {
                km_u32 a = km_held_bit(extremes[i]), r = ref_km_held_bit(extremes[i]);
                CHECK(a == r, "km_held_bit(0x%x): asm=%u ref=%u", extremes[i], a, r);
        }

        /* km_held_flags: 0..65535 + extremes */
        for (km_u32 h = 0; h < 65536u; h++) {
                km_u32 a = km_held_flags(h), r = ref_km_held_flags(h);
                CHECK(a == r, "km_held_flags(0x%x): asm=0x%x ref=0x%x", h, a, r);
        }
        for (size_t i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++) {
                km_u32 a = km_held_flags(extremes[i]), r = ref_km_held_flags(extremes[i]);
                CHECK(a == r, "km_held_flags(0x%x): asm=0x%x ref=0x%x", extremes[i], a, r);
        }

        /* km_layout_index */
        const char *names[] = {
            NULL, "", "u", "us", "uk", "gb", "de", "se", "sv", "fi",
            "no", "nb", "dk", "fr", "ar", "il", "l4", "xx", "usa", "de1", "u\0s",
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
                km_i32 a = km_layout_index(names[i], t_layouts, t_tables.layout_count);
                km_i32 r = ref_km_layout_index(names[i], t_layouts, t_tables.layout_count);
                CHECK(a == r, "km_layout_index(%s): asm=%d ref=%d", names[i] ? names[i] : "NULL", a, r);
        }
        CHECK(km_layout_index("us", NULL, 10) == -1, "NULL layouts");
        CHECK(km_layout_index("us", t_layouts, 0) == -1, "count=0");

        /* km_letter_flip: 0..65535 + dead bit + extremes */
        for (km_u32 c = 0; c < 65536u; c++) {
                km_u32 a = km_letter_flip(c), r = ref_km_letter_flip(c);
                CHECK(a == r, "km_letter_flip(0x%x): asm=0x%x ref=0x%x", c, a, r);
        }
        for (km_u32 c = 0x10000u; c <= 0x10100u; c++) {
                km_u32 a = km_letter_flip(c), r = ref_km_letter_flip(c);
                CHECK(a == r, "km_letter_flip(0x%x): asm=0x%x ref=0x%x", c, a, r);
        }
        for (size_t i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++) {
                km_u32 a = km_letter_flip(extremes[i]), r = ref_km_letter_flip(extremes[i]);
                CHECK(a == r, "km_letter_flip(0x%x): asm=0x%x ref=0x%x", extremes[i], a, r);
        }

        /* km_compose: all accents + unknown accents x 0..300 + NULL/zero/unterminated */
        const km_u32 accents[] = {0, T_ACUTE, T_GRAVE, T_DIAERESIS, T_CIRCUMFLEX, T_TILDE, '?', 0x1234u, 0xffffffffu};
        for (size_t i = 0; i < sizeof(accents) / sizeof(accents[0]); i++) {
                for (km_u32 c = 0; c <= 300; c++) {
                        km_u32 a = km_compose(accents[i], c, t_composed, t_tables.composed_count);
                        km_u32 r = ref_km_compose(accents[i], c, t_composed, t_tables.composed_count);
                        CHECK(a == r, "km_compose(0x%x, 0x%x): asm=0x%x ref=0x%x", accents[i], c, a, r);
                }
                CHECK(km_compose(accents[i], accents[i], NULL, 0) == accents[i], "compose self with NULL");
                CHECK(km_compose(accents[i], ' ', NULL, 0) == accents[i], "compose space with NULL");
                CHECK(km_compose(accents[i], 'a', NULL, 5) == 0, "compose NULL table");
                CHECK(km_compose(accents[i], 'a', t_composed, 0) == 0, "compose count=0");
        }
}

/* ── 2. Exhaustive km_character matrix (all layouts x all keycodes x all mods x caps) ── */

static void test_character_matrix(void)
{
        const km_u32 mod_bits[5] = {KM_MOD_SHIFT, KM_MOD_CONTROL, KM_MOD_ALT, KM_MOD_ALTGR, KM_MOD_SUPER};

        for (km_u32 lay = 0; lay <= t_tables.layout_count + 1; lay++) {
                for (km_u32 caps = 0; caps <= 1; caps++) {
                        struct km_state st = {(km_u8)lay, (km_u8)caps, 0, {0, 0, 0, 0}};
                        for (km_u32 m = 0; m < 32u; m++) {
                                km_u32 mods = 0;
                                for (int b = 0; b < 5; b++)
                                        if (m & (1u << b))
                                                mods |= mod_bits[b];
                                for (km_u32 code = 0; code <= 130; code++) {
                                        km_u32 a = km_character(&t_tables, &st, code, mods);
                                        km_u32 r = ref_km_character(&t_tables, &st, code, mods);
                                        CHECK(a == r, "km_character(lay=%u,caps=%u,code=%u,mods=0x%x): asm=0x%x ref=0x%x",
                                              lay, caps, code, mods, a, r);
                                }
                        }
                }
        }

        /* NULL / degenerate safety */
        struct km_state st = {0, 0, 0, {0, 0, 0, 0}};
        struct km_tables bad_us = {NULL, t_layouts, t_composed, t_tables.layout_count, t_tables.composed_count};
        struct km_tables bad_lay = {&t_us[0][0], NULL, t_composed, 5, t_tables.composed_count};
        CHECK(km_character(NULL, &st, 30, 0) == 0, "NULL tables");
        CHECK(km_character(&t_tables, NULL, 30, 0) == 0, "NULL map");
        CHECK(km_character(&bad_us, &st, 30, 0) == 0, "NULL us");
        CHECK(km_character(&bad_lay, &st, 30, 0) == 'a', "NULL layouts falls back to US");
        CHECK(km_character(&t_tables, &st, 0xffffffffu, 0) == 0, "code=0xffffffff");
}

/* ── 3. Deterministic dead-key, CapsLock, hidden-bit & state-machine tests ── */

static void run_event_pair(struct km_state *sa, struct km_state *sr,
                           km_u32 code, km_i32 value, km_u32 mods)
{
        km_u32 oa[2] = {0xdeadbeefu, 0xcafebabeu};
        km_u32 or_[2] = {0xdeadbeefu, 0xcafebabeu};
        km_i32 ra = km_event(&t_tables, sa, code, value, mods, oa);
        km_i32 rr = ref_km_event(&t_tables, sr, code, value, mods, or_);
        CHECK(ra == rr, "km_event(code=%u,val=%d,mods=0x%x): ret asm=%d ref=%d", code, value, mods, ra, rr);
        CHECK(memcmp(sa, sr, sizeof(*sa)) == 0, "km_event state mismatch (code=%u,val=%d)", code, value);
        if (ra >= 1) CHECK(oa[0] == or_[0], "out[0] mismatch: 0x%x vs 0x%x", oa[0], or_[0]);
        if (ra >= 2) CHECK(oa[1] == or_[1], "out[1] mismatch: 0x%x vs 0x%x", oa[1], or_[1]);
}

static void test_events_deterministic(void)
{
        /* NULL safety */
        struct km_state st = {0};
        km_u32 out[2] = {0, 0};
        CHECK(km_event(NULL, &st, 30, 1, 0, out) == 0, "NULL tables");
        CHECK(km_event(&t_tables, NULL, 30, 1, 0, out) == 0, "NULL map");
        CHECK(km_event(&t_tables, &st, 30, 1, 0, NULL) == 0, "NULL out");
        CHECK(km_event(&t_tables, &st, 128, 1, 0, out) == 0, "code=128");
        CHECK(km_event(&t_tables, &st, 0xffffffffu, 1, 0, out) == 0, "code=0xffffffff");

        /* Swedish layout (index 4): dead acute (13), dead diaeresis/circumflex/tilde (27) */
        struct km_state sa = {4, 0, 0, {0, 0, 0, 0}};
        struct km_state sr = sa;

        /* Dead acute + repeat + release + 'e' -> é */
        run_event_pair(&sa, &sr, 13, 1, 0); /* dead acute taken (-1) */
        run_event_pair(&sa, &sr, 13, 2, 0); /* repeat while hidden (-1) */
        run_event_pair(&sa, &sr, 13, 0, 0); /* release while hidden (-1) */
        run_event_pair(&sa, &sr, 13, 0, 0); /* second release (0) */
        run_event_pair(&sa, &sr, 18, 1, 0); /* 'e' -> 0xE9 */

        /* Dead acute + space -> standalone acute */
        run_event_pair(&sa, &sr, 13, 1, 0);
        run_event_pair(&sa, &sr, 13, 0, 0);
        run_event_pair(&sa, &sr, 57, 1, 0);

        /* Dead acute + dead acute -> standalone acute, dead cleared */
        run_event_pair(&sa, &sr, 13, 1, 0);
        run_event_pair(&sa, &sr, 13, 0, 0);
        run_event_pair(&sa, &sr, 13, 1, 0);
        run_event_pair(&sa, &sr, 13, 0, 0);

        /* Dead acute + dead diaeresis -> emits acute, diaeresis stays pending, + 'u' -> ü */
        run_event_pair(&sa, &sr, 13, 1, 0);
        run_event_pair(&sa, &sr, 13, 0, 0);
        run_event_pair(&sa, &sr, 27, 1, 0);
        run_event_pair(&sa, &sr, 27, 0, 0);
        run_event_pair(&sa, &sr, 22, 1, 0);

        /* Dead tilde (AltGr+27) + unknown 'x' -> 2 chars ('~', 'x') */
        run_event_pair(&sa, &sr, 27, 1, KM_MOD_ALTGR);
        run_event_pair(&sa, &sr, 27, 0, KM_MOD_ALTGR);
        run_event_pair(&sa, &sr, 45, 1, 0);

        /* Dead acute + Return (\n < ' ') and Backspace (127) */
        run_event_pair(&sa, &sr, 13, 1, 0);
        run_event_pair(&sa, &sr, 13, 0, 0);
        run_event_pair(&sa, &sr, 28, 1, 0);
        run_event_pair(&sa, &sr, 13, 1, 0);
        run_event_pair(&sa, &sr, 13, 0, 0);
        run_event_pair(&sa, &sr, 14, 1, 0);

        /* CapsLock press/repeat/release */
        run_event_pair(&sa, &sr, KM_CAPSLOCK, 1, 0);
        run_event_pair(&sa, &sr, KM_CAPSLOCK, 2, 0);
        run_event_pair(&sa, &sr, KM_CAPSLOCK, 0, 0);
        run_event_pair(&sa, &sr, 30, 1, 0); /* 'A' */
        run_event_pair(&sa, &sr, 26, 1, 0); /* 0xC5 (Å) */
        run_event_pair(&sa, &sr, KM_CAPSLOCK, 1, 0);

        /* Hidden bits across all 4 words (codes 0, 31, 32, 63, 64, 95, 96, 127) */
        const km_u32 hcodes[] = {0, 31, 32, 63, 64, 95, 96, 127};
        for (size_t i = 0; i < sizeof(hcodes) / sizeof(hcodes[0]); i++) {
                km_u32 c = hcodes[i];
                sa.hidden[c >> 5] |= 1u << (c & 31u);
                sr.hidden[c >> 5] |= 1u << (c & 31u);
                run_event_pair(&sa, &sr, c, 2, 0);
                run_event_pair(&sa, &sr, c, 0, 0);
                run_event_pair(&sa, &sr, c, 0, 0);
        }
}

/* ── 4. Randomized differential fuzzing (200,000 events) ── */

static void test_fuzz(void)
{
        struct km_state sa = {0}, sr = {0};
        const km_u32 mods_pool[] = {
            0, KM_MOD_SHIFT, KM_MOD_CONTROL, KM_MOD_ALT, KM_MOD_ALTGR, KM_MOD_SUPER,
            KM_MOD_SHIFT | KM_MOD_ALTGR, KM_MOD_SHIFT | KM_MOD_CONTROL,
            KM_MOD_CONTROL | KM_MOD_ALTGR, KM_MOD_SHIFT | KM_MOD_CONTROL | KM_MOD_ALTGR,
        };
        for (int i = 0; i < 200000; i++) {
                uint32_t r = xorshift32();
                if ((r & 0x3ffu) == 0u) {
                        km_u8 lay = (km_u8)(r % (t_tables.layout_count + 2u));
                        sa.layout = lay;
                        sr.layout = lay;
                }
                km_u32 code = (r & 0x1fu) == 0u ? (r >> 8) : (r >> 8) % 135u;
                km_i32 val = (km_i32)((r >> 24) % 4u); /* 0, 1, 2, 3 */
                km_u32 mods = mods_pool[(r >> 26) % (sizeof(mods_pool) / sizeof(mods_pool[0]))];
                run_event_pair(&sa, &sr, code, val, mods);
        }
}

/* ── 5. mmap guard-page tests (PROT_NONE before AND after every table/struct) ── */

static void test_guard_pages(void)
{
        struct guarded_buf g_name = guarded_alloc(1);
        struct guarded_buf g_lay = guarded_alloc(1);
        struct guarded_buf g_comp = guarded_alloc(1);
        struct guarded_buf g_us = guarded_alloc(1);
        struct guarded_buf g_patch = guarded_alloc(1);
        struct guarded_buf g_st = guarded_alloc(1);
        struct guarded_buf g_tab = guarded_alloc(1);
        struct guarded_buf g_out = guarded_alloc(1);

        char *p_end = (char *)g_name.base + 2 * g_name.page;
        char *p_start = (char *)g_name.base + g_name.page;

        /* Test name strings flush against trailing AND leading guard pages */
        const char *short_names[] = {"", "u", "us", "de", "zz", "usa"};
        struct km_layout *lay_end = (struct km_layout *)((char *)g_lay.base + 2 * g_lay.page - sizeof(t_layouts));
        struct km_layout *lay_start = (struct km_layout *)((char *)g_lay.base + g_lay.page);
        memcpy(lay_end, t_layouts, sizeof(t_layouts));

        for (size_t i = 0; i < sizeof(short_names) / sizeof(short_names[0]); i++) {
                size_t n = strlen(short_names[i]) + 1;
                char *s_end = p_end - n;
                memcpy(s_end, short_names[i], n);
                CHECK(km_layout_index(s_end, lay_end, t_tables.layout_count) ==
                      ref_km_layout_index(s_end, lay_end, t_tables.layout_count), "guard name tail %zu", i);
                memcpy(p_start, short_names[i], n);
                CHECK(km_layout_index(p_start, lay_end, t_tables.layout_count) ==
                      ref_km_layout_index(p_start, lay_end, t_tables.layout_count), "guard name head %zu", i);
        }
        memcpy(lay_start, t_layouts, sizeof(t_layouts));
        CHECK(km_layout_index("l4", lay_start, t_tables.layout_count) ==
              ref_km_layout_index("l4", lay_start, t_tables.layout_count), "guard lay head");

        /* Guarded composed table including a malformed row with NO NUL terminator in bases[13] */
        struct km_composed malformed[2];
        memcpy(&malformed[0], &t_composed[0], sizeof(malformed[0]));
        malformed[1].accent = 0x2222u;
        memset(malformed[1].bases, 'Z', sizeof(malformed[1].bases)); /* NO NUL! */
        memset(malformed[1].made, 0xAB, sizeof(malformed[1].made));
        struct km_composed *comp_end = (struct km_composed *)((char *)g_comp.base + 2 * g_comp.page - sizeof(malformed));
        memcpy(comp_end, malformed, sizeof(malformed));
        CHECK(km_compose(0x2222u, 'x', comp_end, 2) == 0, "guard malformed composed miss");
        CHECK(km_compose(0x2222u, 'Z', comp_end, 2) == 0xABu, "guard malformed composed hit");
        CHECK(km_compose(T_ACUTE, 'e', comp_end, 2) == 0xE9u, "guard composed valid");

        /* Guarded us, patch, state, tables, out — both trailing and leading edges */
        for (int edge = 0; edge < 2; edge++) {
                km_u8 *us_p = edge ? ((km_u8 *)g_us.base + 2 * g_us.page - sizeof(t_us))
                                   : ((km_u8 *)g_us.base + g_us.page);
                struct km_patch *pa_p = edge ? (struct km_patch *)((char *)g_patch.base + 2 * g_patch.page - sizeof(t_se))
                                             : (struct km_patch *)((char *)g_patch.base + g_patch.page);
                struct km_state *st_p = edge ? (struct km_state *)((char *)g_st.base + 2 * g_st.page - sizeof(struct km_state))
                                             : (struct km_state *)((char *)g_st.base + g_st.page);
                struct km_tables *tb_p = edge ? (struct km_tables *)((char *)g_tab.base + 2 * g_tab.page - sizeof(struct km_tables))
                                              : (struct km_tables *)((char *)g_tab.base + g_tab.page);
                km_u32 *out_p = edge ? (km_u32 *)((char *)g_out.base + 2 * g_out.page - 2 * sizeof(km_u32))
                                     : (km_u32 *)((char *)g_out.base + g_out.page);

                memcpy(us_p, t_us, sizeof(t_us));
                memcpy(pa_p, t_se, sizeof(t_se));
                struct km_layout one_lay = {"se", pa_p, (km_u8)(sizeof(t_se) / sizeof(t_se[0]))};
                struct km_layout *la_p = edge ? (struct km_layout *)((char *)g_lay.base + 2 * g_lay.page - sizeof(one_lay))
                                              : (struct km_layout *)((char *)g_lay.base + g_lay.page);
                memcpy(la_p, &one_lay, sizeof(one_lay));

                tb_p->us = us_p;
                tb_p->layouts = la_p;
                tb_p->composed = comp_end;
                tb_p->layout_count = 1;
                tb_p->composed_count = 2;
                memset(st_p, 0, sizeof(*st_p));

                /* Dead acute -> release -> 'e' -> é, plus out-of-range code 98, 99, 127, 128 */
                CHECK(km_event(tb_p, st_p, 13, 1, 0, out_p) == -1, "guard dead press edge=%d", edge);
                CHECK(km_event(tb_p, st_p, 13, 0, 0, out_p) == -1, "guard dead rel edge=%d", edge);
                CHECK(km_event(tb_p, st_p, 18, 1, 0, out_p) == 1 && out_p[0] == 0xE9u, "guard compose edge=%d", edge);
                CHECK(km_character(tb_p, st_p, 98, KM_MOD_SHIFT) == '/', "guard max code 98 edge=%d", edge);
                CHECK(km_character(tb_p, st_p, 99, 0) == 0, "guard oob code 99 edge=%d", edge);
                CHECK(km_event(tb_p, st_p, 127, 1, 0, out_p) == 0, "guard max event code 127 edge=%d", edge);
                CHECK(km_event(tb_p, st_p, 128, 1, 0, out_p) == 0, "guard oob event code 128 edge=%d", edge);
        }

        guarded_free(g_name);
        guarded_free(g_lay);
        guarded_free(g_comp);
        guarded_free(g_us);
        guarded_free(g_patch);
        guarded_free(g_st);
        guarded_free(g_tab);
        guarded_free(g_out);
}

/* ── 6. Throughput benchmark (ns/op ASM vs -O2 C reference) ── */

static void bench(void)
{
        const int N = 2000000;
        volatile km_u32 sink = 0;
        uint64_t t0, t1, t_asm, t_ref;

        /* 1. km_held_bit */
        t0 = now_ns();
        km_u32 acc = 0;
        for (int i = 0; i < N; i++) acc += km_held_bit((km_u32)(i & 127));
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += ref_km_held_bit((km_u32)(i & 127));
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_held_bit     : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        /* 2. km_held_flags */
        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += km_held_flags((km_u32)(i & 255));
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += ref_km_held_flags((km_u32)(i & 255));
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_held_flags   : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        /* 3. km_layout_index */
        const char *qnames[4] = {"us", "de", "fr", "il"};
        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += (km_u32)km_layout_index(qnames[i & 3], t_layouts, t_tables.layout_count);
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += (km_u32)ref_km_layout_index(qnames[i & 3], t_layouts, t_tables.layout_count);
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_layout_index : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        /* 4. km_letter_flip */
        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += km_letter_flip((km_u32)(i & 255));
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += ref_km_letter_flip((km_u32)(i & 255));
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_letter_flip  : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        /* 5. km_compose */
        const km_u32 bchars[4] = {'e', 'a', 'u', 'z'};
        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += km_compose(T_ACUTE, bchars[i & 3], t_composed, t_tables.composed_count);
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += ref_km_compose(T_ACUTE, bchars[i & 3], t_composed, t_tables.composed_count);
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_compose      : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        /* 6. km_character (Swedish layout with patches) */
        struct km_state st = {4, 0, 0, {0, 0, 0, 0}};
        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += km_character(&t_tables, &st, (km_u32)(16 + (i & 31)), (i & 1) ? KM_MOD_SHIFT : 0u);
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += ref_km_character(&t_tables, &st, (km_u32)(16 + (i & 31)), (i & 1) ? KM_MOD_SHIFT : 0u);
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_character    : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        /* 7. km_event (mixed press/release + dead-key compose) */
        const km_u32 seq_code[4] = {13, 13, 18, 18};
        const km_i32 seq_val[4] = {1, 0, 1, 0};
        struct km_state sa = {4, 0, 0, {0, 0, 0, 0}};
        struct km_state sr = sa;
        km_u32 out[2];
        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += (km_u32)km_event(&t_tables, &sa, seq_code[i & 3], seq_val[i & 3], 0, out);
        t1 = now_ns(); t_asm = t1 - t0; sink ^= acc;

        t0 = now_ns(); acc = 0;
        for (int i = 0; i < N; i++) acc += (km_u32)ref_km_event(&t_tables, &sr, seq_code[i & 3], seq_val[i & 3], 0, out);
        t1 = now_ns(); t_ref = t1 - t0; sink ^= acc;
        printf("  km_event        : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)t_asm / N, (double)t_ref / N, (double)t_ref / (double)t_asm);

        (void)sink;
}

int main(void)
{
        test_primitives();
        test_character_matrix();
        test_events_deterministic();
        test_fuzz();
        test_guard_pages();

        if (failures) {
                fprintf(stderr, "FAILED: %d errors\n", failures);
                return 1;
        }
        printf("ALL TESTS PASSED (exhaustive + 200k fuzz + mmap guard pages)\n");
        bench();
        return 0;
}
