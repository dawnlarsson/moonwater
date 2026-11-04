/*
        The kit's theme: the roles, the palettes they are written as, and the maker that turns a choice of light or
        dark and an accent into them. Nothing here draws. The compositor is built before ui.c, and with every UI feature
        switched off, so it makes the chrome it draws from these same roles that the windows take. ui.c includes this file.
*/
#ifndef MOONWATER_CANVAS_UI_THEME
#define MOONWATER_CANVAS_UI_THEME

/*
        The kit's colours, by role (the Material role names its ui.css maps
        onto), made from two choices: light or dark, and an accent. Light with
        the accent ff8800 is Dawning's palette as styles-core.css and ui.css
        have it, every value as written there; another accent derives the
        roles that follow it (its container, the ink on it, the bright tint
        the focus glow and the controls' off colour are mixed from) with CSS
        color-mix in srgb, and dark is a palette of its own on the same neutral
        ramp. The ink on a fill is whichever of the kit's ink and white stands
        out more by WCAG's contrast ratio; where neither reaches 4.5:1 (an
        accent of middle lightness) it is black or white, which always do. A
        colour is 0xaarrggbb, not premultiplied.

        `moonwater canvas theme` and `canvas theme accent` keep the choices;
        the compositor puts them in the window table's header and knocks, and a
        window turns to the new colours over the kit's .2s --ease, as the kit's
        own background and colour transitions take them.
*/
#define UI_PRIMARY 0
#define UI_ON_PRIMARY 1
#define UI_PRIMARY_CONTAINER 2
#define UI_ON_PRIMARY_CONTAINER 3
#define UI_SECONDARY_CONTAINER 4
#define UI_ON_SECONDARY_CONTAINER 5
#define UI_SURFACE 6
#define UI_SURFACE_DIM 7
#define UI_SURFACE_LOWEST 8
#define UI_SURFACE_LOW 9
#define UI_SURFACE_CONTAINER 10
#define UI_SURFACE_HIGH 11
#define UI_SURFACE_HIGHEST 12
#define UI_ON_SURFACE 13
#define UI_ON_SURFACE_VARIANT 14
#define UI_OUTLINE 15
#define UI_OUTLINE_VARIANT 16
#define UI_ERROR 17
#define UI_ON_ERROR 18
#define UI_ERROR_CONTAINER 19
#define UI_ON_ERROR_CONTAINER 20
#define UI_SUCCESS 21
#define UI_SUCCESS_CONTAINER 22
#define UI_ON_SUCCESS_CONTAINER 23
#define UI_WARNING 24
#define UI_WARNING_CONTAINER 25
#define UI_ON_WARNING_CONTAINER 26
#define UI_INFO 27
#define UI_INFO_CONTAINER 28
#define UI_ON_INFO_CONTAINER 29
#define UI_CONTROL_OFF 30   // the rest of a switch, a checkbox, a radio and a slider: the bright accent at 14% on paper
#define UI_ACCENT_BRIGHT 31 // --dawn-400: the focus glow, the selection and the controls' off colour are made from it
#define UI_SELECTION 32     // the bright accent at 28%
#define UI_SHADOW 33        // the ink of shadows, at the alphas the kit gives them
#define UI_HIGHLIGHT 34     // the light a raised field catches on its top edge
#define UI_ROLES 35

#define UI_THEME_LIGHT 0u
#define UI_THEME_DARK 1u
#define UI_THEME_AUTO 2u
#define UI_ACCENT_DAWN 0xff8800u // --dawn-500, the mark: one accent of the choices
#define UI_ACCENT_STANDARD 0xffffffu // the accent when none is chosen: white, on the dark theme (the standard)
#define UI_INK 0xff18181bu       // --ink

/*
        The kit's shapes and touch sizes that the compositor draws as well as the windows, so they are named here (the
        compositor is built before ui.c and includes this file, not that one). The windows' corner radius; how far a press
        moves before it is a drag (a finger's slop); the close button's disc, and how far in from the window's top right
        it sits. A close button ends UI_CLOSE_BOTTOM logical pixels from its top, which is where a titlebar's content starts.
*/
#define UI_RADIUS 8
#define UI_SLOP 8
#define UI_CLOSE 14
#define UI_CLOSE_IN 5
#define UI_CLOSE_BOTTOM (UI_CLOSE + UI_CLOSE_IN)
#define UI_TARGET 44 // a finger's least target in logical pixels: about 9 mm at the 96 to 120 dpi of most screens

/*
        A finger's target, the one rule: a finger on a touch screen is wider than a pointer and shakes as it lands, so what it
        presses is the shape grown about its middle to at least `least` device pixels each way where it is smaller (the kit's
        targets take UI_TARGET at the scale, ui_hit; a window's close button its disc and a little more); a pointer presses
        the shape as it is drawn. Whether px, py is on the shape x, y, width by height, for a finger or a pointer alike:
        only the size of the target differs.
*/
static inline int ui_reach_hit(int touch, int least, int px, int py, int x, int y, int width, int height)
{
        if (touch && width < least)
                x -= (least - width) / 2, width = least;
        if (touch && height < least)
                y -= (least - height) / 2, height = least;
        return px >= x && px < x + width && py >= y && py < y + height;
}

struct ui_theme
{
        unsigned int role[UI_ROLES];
};

// Dawning light, as styles-core.css and ui.css write it.
static const struct ui_theme ui_theme_dawning = {{
    0xffff8800, 0xff18181b, 0xffffe8cc, 0xff4a2600, 0xffededef, 0xff1c1c1f, 0xfff7f7f8, 0xffededef, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffededef, 0xff18181b, 0xff52525b, 0xffa1a1aa, 0xffe4e4e7, 0xffdc2e26, 0xffffffff, 0xfffce4e2,
    0xff5c1512, 0xff0d7f49, 0xffe3f3ea, 0xff0b3d23, 0xff9c6b00, 0xfff7ecd4, 0xff4a330a, 0xff1f5fd0, 0xffe2ecfb, 0xff143a7a,
    0xfffff2e2, 0xffff9f33, 0x47ff9f33, 0xff101014, 0xffffffff,
}};

/*
        Dark, a design and not the light one turned over: paper is the ramp's
        900, a card 850, a field's face 800 and a well or a track 700, text
        the ramp's 100 and its quiet the 300; the status colours are lifted to
        read on the dark and their containers are their deep colour at a
        quarter on the field. The accent's roles are mixed into it when made.
*/
static const struct ui_theme ui_theme_night = {{
    0xffff8800, 0xff18181b, 0xff47290c, 0xffffd199, 0xff2a2a30, 0xffededef, 0xff111113, 0xff0a0a0b, 0xff0a0a0b, 0xff161618,
    0xff1c1c1f, 0xff1c1c1f, 0xff2a2a30, 0xffededef, 0xffa1a1aa, 0xff52525b, 0xff2a2a30, 0xffff6b63, 0xff18181b, 0xff4a1d1c,
    0xffffb4ae, 0xff4cc38a, 0xff123826, 0xffa6e9c5, 0xffe5b54a, 0xff3d3012, 0xfff5d99a, 0xff7aa7ff, 0xff1b2c4f, 0xffbcd3ff,
    0xff3a2f27, 0xffff9f33, 0x47ff9f33, 0xff000000, 0x0fffffff,
}};

// sRGB to linear light, 0 to 65536, for WCAG's relative luminance.
static const unsigned int ui_linear[256] = {
        0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219, 241, 264, 288, 313,
        340, 367, 396, 427, 458, 491, 526, 562, 599, 637, 677, 718, 761, 805, 851, 898,
        947, 997, 1048, 1101, 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1791, 1863,
        1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258,
        3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129, 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124,
        5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246, 6395, 6547, 6701, 6856, 7014, 7174, 7336, 7500,
        7666, 7834, 8004, 8177, 8352, 8529, 8708, 8889, 9072, 9258, 9446, 9636, 9828, 10022, 10219, 10418,
        10619, 10822, 11028, 11236, 11446, 11658, 11873, 12090, 12309, 12531, 12754, 12981, 13209, 13440, 13673, 13909,
        14147, 14387, 14629, 14874, 15122, 15372, 15624, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
        18278, 18556, 18838, 19121, 19408, 19696, 19988, 20281, 20578, 20876, 21178, 21481, 21788, 22096, 22408, 22722,
        23038, 23357, 23679, 24003, 24329, 24659, 24991, 25325, 25662, 26002, 26344, 26689, 27036, 27387, 27739, 28095,
        28453, 28813, 29177, 29543, 29911, 30283, 30657, 31033, 31413, 31795, 32180, 32567, 32957, 33350, 33746, 34144,
        34545, 34949, 35355, 35765, 36177, 36591, 37009, 37429, 37852, 38278, 38707, 39138, 39572, 40009, 40449, 40892,
        41337, 41786, 42237, 42691, 43147, 43607, 44069, 44534, 45003, 45474, 45947, 46424, 46904, 47386, 47871, 48360,
        48851, 49345, 49842, 50342, 50844, 51350, 51859, 52370, 52884, 53402, 53922, 54445, 54972, 55501, 56033, 56568,
        57106, 57647, 58191, 58738, 59288, 59841, 60397, 60956, 61518, 62083, 62651, 63222, 63796, 64373, 64953, 65536,
};

// WCAG's relative luminance of an opaque colour, 0 to 65536.
static unsigned int ui_luminance(unsigned int rgb)
{
        return (2126u * ui_linear[rgb >> 16 & 255] + 7152u * ui_linear[rgb >> 8 & 255] + 722u * ui_linear[rgb & 255] + 5000) / 10000;
}

// The contrast ratio of two opaque colours, in hundredths (700 is 7:1).
static unsigned int ui_contrast(unsigned int one, unsigned int two)
{
        unsigned int a = ui_luminance(one) + 3277, b = ui_luminance(two) + 3277; // + 0.05

        return a > b ? a * 100 / b : b * 100 / a;
}

/*
        color-mix(in srgb, one percent%, two): each colour premultiplied by its
        alpha, the two mixed channel by channel, and the alpha divided out
        again, rounded to the nearest; mixing with transparent keeps one's
        colour at percent of its alpha.
*/
static unsigned int ui_mix(unsigned int one, unsigned int percent, unsigned int two)
{
        unsigned int a1 = one >> 24, a2 = two >> 24, rest = 100 - percent;
        unsigned int alpha = a1 * percent + a2 * rest; // hundredths of an 8-bit alpha
        unsigned int out;

        if (!alpha)
                return 0;
        out = (alpha + 50) / 100 << 24;
        for (int shift = 0; shift < 24; shift += 8)
        {
                unsigned int c = ((one >> shift & 255) * a1 * percent + (two >> shift & 255) * a2 * rest + alpha / 2) / alpha;

                out |= (c > 255 ? 255 : c) << shift;
        }
        return out;
}

// The ink to write on a fill: the kit's ink or white, whichever stands out more; black or white where neither reaches 4.5:1.
static unsigned int ui_ink_on(unsigned int fill)
{
        unsigned int ink = ui_contrast(UI_INK, fill), white = ui_contrast(0xffffffffu, fill);

        if (ink >= 450 || white >= 450)
                return ink >= white ? UI_INK : 0xffffffffu;
        return ui_contrast(0xff000000u, fill) >= white ? 0xff000000u : 0xffffffffu;
}

// An ink for text on a container, from a start mixed toward black (or white on dark) till it stands out 4.5:1.
static unsigned int ui_ink_deepened(unsigned int start, unsigned int container, unsigned int toward)
{
        unsigned int ink = start;

        for (int step = 0; step < 24 && ui_contrast(ink, container) < 450; step++)
                ink = ui_mix(ink, 85, toward);
        return ink;
}

// The roles for light or dark and an accent (0xrrggbb).
static fn ui_theme_make(struct ui_theme address_to theme, unsigned int dark, unsigned int accent)
{
        unsigned int fill = 0xff000000u | (accent & 0xffffffu), bright;

        *theme = dark ? ui_theme_night : ui_theme_dawning;
        if (!dark && (accent & 0xffffffu) == UI_ACCENT_DAWN)
                return; // the palette as written, to the value
        // An accent that does not stand out from the surfaces (white on the light theme) fills with the theme's ink.
        if (ui_contrast(fill, theme->role[UI_SURFACE_HIGH]) < 150)
                fill = theme->role[UI_ON_SURFACE];
        bright = (accent & 0xffffffu) == UI_ACCENT_DAWN ? 0xffff9f33u : ui_mix(fill, 80, 0xffffffffu);
        theme->role[UI_PRIMARY] = fill;
        theme->role[UI_ON_PRIMARY] = ui_ink_on(fill);
        theme->role[UI_ACCENT_BRIGHT] = bright;
        theme->role[UI_SELECTION] = ui_mix(bright, 28, 0);
        if (dark)
        {
                theme->role[UI_PRIMARY_CONTAINER] = ui_mix(fill, 22, theme->role[UI_SURFACE_HIGH]);
                theme->role[UI_ON_PRIMARY_CONTAINER] = ui_ink_deepened(ui_mix(fill, 40, 0xffffffffu), theme->role[UI_PRIMARY_CONTAINER], 0xffffffffu);
                theme->role[UI_CONTROL_OFF] = ui_mix(bright, 14, theme->role[UI_SURFACE_HIGHEST]);
        }
        else
        {
                theme->role[UI_PRIMARY_CONTAINER] = ui_mix(fill, 20, 0xffffffffu);
                theme->role[UI_ON_PRIMARY_CONTAINER] = ui_ink_deepened(ui_mix(fill, 29, 0xff000000u), theme->role[UI_PRIMARY_CONTAINER], 0xff000000u);
                theme->role[UI_CONTROL_OFF] = ui_mix(bright, 14, 0xffffffffu);
        }
}

/*
        Where a width by height window goes beside a rectangle (rect: x, y, width, height, in the desktop's space) on a screen
        (x, y, width, height). side is where it goes: UI_AT its top left, UI_BELOW the rectangle's bottom edge, UI_ABOVE its top
        edge, UI_RIGHT its right edge and UI_LEFT its left edge; it is aligned to the rectangle along the other axis and kept
        inside the screen. flip says what to do when it does not fit: UI_FLIP_SIDE turns it to the other side of the rectangle
        (below is flipped above, to the right is flipped to the left), and UI_FLIP_CROSS turns it to the other side along the
        other axis (a menu at a point goes left when it would run off the right). Without flips it is kept on its side and
        clamped. This is the one place a window is put beside something: the kit's panel buttons (ui_anchor_set), the menus
        and the compositor's popups (compositor_popup_place maps a window's popup flags onto it).
*/
#define UI_AT 0
#define UI_BELOW 1
#define UI_ABOVE 2
#define UI_RIGHT 3
#define UI_LEFT 4
#define UI_FLIP_SIDE 1u
#define UI_FLIP_CROSS 2u

static inline void ui_anchor_place(unsigned int side, unsigned int flip, const int *rect, const int *screen, int width, int height,
                                   int *x, int *y)
{
        int left = screen[0], top = screen[1], right = screen[0] + screen[2], bottom = screen[1] + screen[3];
        int ax = rect[0], ay = rect[1], aw = rect[2], ah = rect[3];

        *x = ax, *y = ay;
        if (side == UI_BELOW)
        {
                *y = ay + ah;
                if ((flip & UI_FLIP_SIDE) && *y + height > bottom)
                        *y = ay - height;
        }
        else if (side == UI_ABOVE)
        {
                *y = ay - height;
                if ((flip & UI_FLIP_SIDE) && *y < top)
                        *y = ay + ah;
        }
        else if (side == UI_RIGHT)
        {
                *x = ax + aw;
                if ((flip & UI_FLIP_SIDE) && *x + width > right)
                        *x = ax - width;
        }
        else if (side == UI_LEFT)
                *x = ax - width;
        if ((side == UI_BELOW || side == UI_ABOVE) && (flip & UI_FLIP_CROSS) && *x + width > right)
                *x = ax - width;
        *x = *x + width > right ? right - width : *x;
        *y = *y + height > bottom ? bottom - height : *y;
        *x = *x < left ? left : *x;
        *y = *y < top ? top : *y;
}

#endif
