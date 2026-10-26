/*
        The taskbar: the strip along one edge of the output with a tab for
        each window, and the tray at the far end of it with the time and, when
        the settings name one, the battery. The edge is the position: bottom
        (the default), top, left or right. compositor.c keeps the windows, the
        glass and the frames; taskbar.c has the band's geometry, its hit test,
        its damage and the painting of its tabs and tray, all read from the
        struct taskbar_view the compositor fills for each call.

        Included by compositor.c. With MOONWATER_UI_TASKBAR 0 taskbar.c is not
        built, and these stubs answer as if there were no band at all: nothing
        is drawn, nothing is hit and nothing is damaged.
*/
#ifndef MOONWATER_CANVAS_TASKBAR
#define MOONWATER_CANVAS_TASKBAR

#include "features.h"

// Where the band sits: the edge of the output it is along.
#define TASKBAR_BOTTOM 0u
#define TASKBAR_TOP 1u
#define TASKBAR_LEFT 2u
#define TASKBAR_RIGHT 3u

// What a point of the band is over: a tab's number (0 up), -2 the tray, or TASKBAR_HIT_NONE for neither.
#define TASKBAR_HIT_NONE (-3)

// The kept settings the battery is named in (the battery verb keeps "battery NAME" there), and the name of that file in
// /root, which the compositor reads again when it is written.
#define TASKBAR_TUNE_PATH "/root/tune"
#define TASKBAR_TUNE_NAME "tune"

// A battery's name, its null included: battery.c's BATTERY_NAME, here so the compositor can size its buffers
// before that file is read. The two agree.
#ifndef BATTERY_NAME
#define BATTERY_NAME 32
#endif

// The band's size and ink, which compositor.c draws with as well as taskbar.c: the panel's height in logical pixels,
// the ink of the panel and of a tab (black, lighter while its window has the keys, a little off black while it is put
// away) for a tab drawn without a font, and each tab's mark.
#define COMPOSITOR_PANEL 24
#define COMPOSITOR_INK_PANEL 0x141416u
#define COMPOSITOR_INK_BUTTON 0x000000u
#define COMPOSITOR_INK_BUTTON_LIT 0x38383cu
// A window's tab: lit while it has the keys, dim while it is put away.
#define COMPOSITOR_TAB_LIT 1u
#define COMPOSITOR_TAB_AWAY 2u
// The blur under the tabs: the glass is halved this many times, a block of the output a side, and the small blur's reach
// in blocks. Its reach is what a change of the band reaches through the blur and the rise, in device pixels.
#define COMPOSITOR_GLASS_LEVELS 3
#define COMPOSITOR_GLASS_BLOCK (1 << COMPOSITOR_GLASS_LEVELS)
#define COMPOSITOR_GLASS_SPREAD 3 // the small blur's reach, in small pixels: two rounds of a box of four
#define COMPOSITOR_GLASS_REACH ((COMPOSITOR_GLASS_SPREAD + 3) * COMPOSITOR_GLASS_BLOCK)
// The glass under the tabs, half its width and half its height in logical pixels.
#define COMPOSITOR_GLASS_ACROSS 8
#define COMPOSITOR_GLASS_DOWN 4

// What the compositor shows the taskbar: the output, the windows that have a tab and the buffers the band is
// drawn into (none when there is no font, and the tabs are shapes the compositor draws).
struct taskbar_view
{
        int width, height, scale;        // the output, and the device pixels a logical one is
        int x, y;                        // where that output is in the desktop's space: its screen's top left
        unsigned int position;           // TASKBAR_BOTTOM, TASKBAR_TOP, TASKBAR_LEFT or TASKBAR_RIGHT
        unsigned int *pixels;            // the band's pixels, or none
        unsigned long band_bytes;        // how much memory pixels has
        unsigned int *glass;             // the blurred scene under the tabs
        font_face *face;                 // the face the tabs are written in
        font_engine *engine;             // and its engine
        unsigned int count;              // the windows with a tab, in the order they are shown
        const unsigned char *marks;      // each tab's mark: lit, put away
        const unsigned char *lengths;    // each tab's title, in characters
        const char *titles[WINDOWS];     // each tab's title
};

// The glass the band's tabs are made of over a rectangle of the output: the compositor blurs it into
// view->glass and answers the row stride (0 when there is no memory for it).
typedef int (*taskbar_glass_fn)(void *context, int x, int y, int width, int height);

#if MOONWATER_UI_TASKBAR

static void taskbar_band(const struct taskbar_view *view, int *x, int *y, int *width, int *height);
static int taskbar_slot(const struct taskbar_view *view, int which, int *x, int *y, int *width, int *height);
static int taskbar_hit(const struct taskbar_view *view, int px, int py);
static int taskbar_span(const struct taskbar_view *view, int *x, int *y, int *width, int *height);
static unsigned long taskbar_meets(const struct taskbar_view *view, int (*damage)[4], unsigned int damages);
static int taskbar_draw(const struct taskbar_view *view, unsigned long redo, taskbar_glass_fn glass, void *context,
                        int *rect);
static unsigned int taskbar_tab_ink(int slot, unsigned int mark);
static unsigned int taskbar_square(unsigned int position);
static int taskbar_tick(void);
static void taskbar_changed(void);
static int taskbar_stale(void);
static void taskbar_bytes(int width, int height, int scale, unsigned long *band, unsigned long *glass);
static unsigned int taskbar_position(void);
static void taskbar_position_default(unsigned int position);
static void taskbar_settings_set(const char *position, const char *display, const char *tune);
static int taskbar_display_all(void);
static void taskbar_text_set(int edge);

#else

static inline void taskbar_band(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{ (void)view; *x = *y = *width = *height = 0; }
static inline int taskbar_slot(const struct taskbar_view *view, int which, int *x, int *y, int *width, int *height)
{ (void)view, (void)which, (void)x, (void)y, (void)width, (void)height; return 0; }
static inline int taskbar_hit(const struct taskbar_view *view, int px, int py)
{ (void)view, (void)px, (void)py; return TASKBAR_HIT_NONE; }
static inline int taskbar_span(const struct taskbar_view *view, int *x, int *y, int *width, int *height)
{ (void)view, (void)x, (void)y, (void)width, (void)height; return 0; }
static inline unsigned long taskbar_meets(const struct taskbar_view *view, int (*damage)[4], unsigned int damages)
{ (void)view, (void)damage, (void)damages; return 0; }
static inline int taskbar_draw(const struct taskbar_view *view, unsigned long redo, taskbar_glass_fn glass, void *context,
                               int *rect)
{ (void)view, (void)redo, (void)glass, (void)context, (void)rect; return 0; }
static inline unsigned int taskbar_tab_ink(int slot, unsigned int mark)
{ (void)slot, (void)mark; return 0; }
static inline unsigned int taskbar_square(unsigned int position)
{ (void)position; return 0; }
static inline int taskbar_tick(void) { return 0; }
static inline void taskbar_changed(void) {}
static inline int taskbar_stale(void) { return 0; }
static inline void taskbar_bytes(int width, int height, int scale, unsigned long *band, unsigned long *glass)
{ (void)width, (void)height, (void)scale; *band = *glass = 0; }
static inline unsigned int taskbar_position(void) { return TASKBAR_BOTTOM; }
static inline void taskbar_position_default(unsigned int position) { (void)position; }
static inline void taskbar_settings_set(const char *position, const char *display, const char *tune)
{ (void)position, (void)display, (void)tune; }
static inline int taskbar_display_all(void) { return 0; }
static inline void taskbar_text_set(int edge) { (void)edge; }

#endif

#endif
