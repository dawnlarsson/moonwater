/*
        The compositor in userspace, and the smallest program that draws on it.

        `compositor` takes the display from Canvas the way any program can: it takes the card that has a screen and
        makes it its master, drives every screen connected as one space across them, each at the mode Canvas left on it
        (or its preferred one) and the rate its displays line asks, and reads keyboards, pointers, tablets and touch
        screens through evdev, the ones that are there and the ones plugged in later. While it holds the card Canvas
        draws nothing and keeps the console keyboard muted, and its own input handler still hears Control-Alt-Backspace,
        which ends this program's session if it ever stops answering. When it exits, the card's last file closes and
        Canvas's restore hook draws its ground again at once. With -H it draws into a file instead of a card.

        A window is a row of one table (struct windows, window.c) that the compositor makes and every program maps: the
        program writes its picture, its size and its title there and knocks on its row's door, and the compositor writes
        where the window is, the size it asks for, which window has the keys and the events, and knocks back. A Wayland
        client is given a row by the shim (wayland.c, wayland_host.c), and Xwayland's windows by its window manager
        (xwm.c). What a person does with the keys, the hand and the fingers moves, arranges and closes windows here;
        what a window shows is its program's.

        What it draws is each window in its frame, the theme's line and round corners, over the desktop's ground, and the
        taskbar's band of tabs and tray on an edge of a screen, or of each, through glass (the kit draws the band:
        ui/taskbar.c). Only what changed is drawn: on the processor or, where the card can, by its render node (gpu.c);
        into the second of two plates and flipped to, where the card shows what its buffer holds, or in place and told
        the rows that changed, where it copies them; and not at all for a window that fills its screen, which the card
        shows straight from the window's own buffer, the tabs on a plane of their own where it has one.

        The settings are the files under /root that `moonwater canvas` and the settings write: the theme and the look,
        the ground and the pointer, the displays and their arrangement, the taskbar, the keyboard, the hand, touch and
        the GPU, each read at the start and again whenever it is written.

        This file holds the switches, the kit's shared part and gpu.c, the kernel's interfaces, the compositor's state,
        the table and its doors, the signals and uevents, a headless screen, the options, the startup and the loop. The
        rest is in the thirteen parts it includes from compositor/, each of which says what it holds: compose.c,
        keymap.c, pointer.c and outputs.c (the backend: the look, what a key and a hand mean, the rules of the space),
        damage.c, glass.c, scanout.c, gpu_host.c and present.c (what changed, and how a frame reaches the screen), kms.c
        (the card and its screens), session.c (a sleep, the settings, the devices, the doors), manage.c (the windows) and
        input.c (what a person does).

        `surface` is a test client: a window of one colour, a line of text drawn with font.c when it is given a TrueType
        file, and a report on standard output of everything it is told.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_COMPOSITOR
#define MOONWATER_CANVAS_COMPOSITOR

// The Wayland shim is on unless the build's header says SHELL_NO_WAYLAND (CONFIG_MOONWATER_WAYLAND=n).
#if !defined(SHELL_NO_WAYLAND) && !defined(CONFIG_MOONWATER_WAYLAND)
#define CONFIG_MOONWATER_WAYLAND 1
#endif

/*
        The files under /run/moonwater that change what the compositor does: switches for the machine's owner and the checks, each
        only looked for (it is there or it is not), and none a setting a person keeps (those are under /root). Who looks, and when; whoever
        adds a switch file adds its row here:

            present-early     compositor.c's start, once: every present by the old rule, early in its period (compositor_late.early)
            present-wait      compositor.c's start, once: every job of the card's present waited for in place (compositor_card.nowait 0)
            lists-off         gpu_host.c, compositor_lists_switch, every 512th present: while it is there no window is drawn from its list
            lists-nowait      compositor.c's start, once: a list's render goes to the card without a wait (compositor_card.lists_nowait);
                              without it the render is waited for in place
            gpu-nouserptr     gpu.c, gpu_start, at each start of the card: every map by the dma-buf route, never userptr
            gpu-own-userptr   gpu.c, gpu_start: the maps' old order, userptr first for the compositor's own memory too
            gpu-gfx10         gpu.c, gpu_start: the cards of family 143 (RDNA1 and 2) let in, which no one has run the kernels on
            gpu-round-off     gpu.c, gpu_start: a blended window whose picture reaches into a round corner is the processor's, as before
                              the card drew that case
            gpu-cut-off       gpu.c, gpu_start: the bins made without the cut at binning, every record a cell touches in its list, as before
            touch-trace       window.c, window_trace_touch, at each touch-down, the compositor's and each program's: the trace appended
                              to touch-trace.log
            layers-off        wayland_host.c, compositor_wl_start, once: a client's subsurfaces laid into its window's picture as before,
                              not drawn as layers of their own
            cursor-plane-probe  kms.c, at the pointer's start: the cursor plane tried on i915 and xe, with a vblank probe that keeps it only
                              where the pipe runs on with it

        And the files that are not switches: keys (the machine script's chords, written by the script before the compositor starts and read
        once then, input.c) and the FIFO keys.fire a chord's name is written to; record/ (record.h: start and report, taken at a look when the
        directory moved, inject at a frame's class, and state, report.txt and frame-*.ppm written there); the Wayland shim's socket wayland-0
        and Xwayland's window manager's xwm/ (wayland_host.c, xwm.c).
*/

/* ---- backend: the look on the CPU ---- */

// The desktop's ink: neutral grey, no hue, and not a kit role. The line round a window without the keys and with them, the
// chrome's shadow and the band's inks are made from the kit's roles (compositor_chrome, the theme in ui/ui.c), which is what the
// windows draw with, so the chrome turns with the theme; the tabs drawn without a font are in ui/ui.c.
#define COMPOSITOR_INK_DESKTOP 0x1e1e1eu
/*
        An edge or a corner of a window the hand holds to resize it, or all of its line while it is moved: the text's
        ink, well over the dark theme's line with the keys (a7a7a7), so a person sees which handle they have; and an edge the
        pointer is over, that a press would take, a grey between the line without the keys and the held ink, drawn as wide as
        the held one. A finger's mark (`moonwater canvas touch on`) is in the held ink too.
*/
#define COMPOSITOR_INK_LINE_HELD 0xe4e4e4u
#define COMPOSITOR_INK_LINE_HOVER 0x9a9a9au
// The lit bits of a frame: its edges, as CP_EDGE_* number them (the pointer's, below), and every edge and corner, a
// window being moved.
#define COMPOSITOR_LIT_LEFT 1u
#define COMPOSITOR_LIT_RIGHT 2u
#define COMPOSITOR_LIT_TOP 4u
#define COMPOSITOR_LIT_BOTTOM 8u
#define COMPOSITOR_LIT_ALL 16u
// How far along each of its two edges a held corner is lit, in logical pixels: the corner's arc alone is too short to see.
#define COMPOSITOR_LIT_REACH 32
// The width of a lit edge or corner in logical pixels, the setting's (compositor_lit_px): drawn inward from the window's outline,
// over its contents where it is wider than the line, which is one pixel at 100 percent. The standard is COMPOSITOR_BORDER_STANDARD.
#define COMPOSITOR_BORDER_STANDARD 2
#define COMPOSITOR_BORDER_MOST 4

// The desktop's ink as `moonwater canvas background` set it, and the default when none is: what is drawn
// where no window is, and behind a window's round corners. The tabs' glass is made from what is on the screen.
static unsigned int compositor_ground = COMPOSITOR_INK_DESKTOP;

// The radius of a window's corners and the width of the line round it, in logical pixels; and the largest
// radius in device pixels, which is what the corners' scratch is made for. (The panel's height is in ui/ui.c.)
#define COMPOSITOR_LINE 1
/*
        The snap's look, in one place. A snap preview is the selection's colour at COMPOSITOR_SNAP_PERCENT (more on a light
        wallpaper, up to COMPOSITOR_SNAP_PERCENT_MOST), its colour deepened till its edge reads COMPOSITOR_SNAP_READ
        (compositor_snap_ink; ui_contrast's hundredths, so 300 is 3 to 1). A thumbnail and a grid tile have the window's
        line and corners, and their fades run COMPOSITOR_SNAP_FADE_NS, the same time both ways. A window arranged, a tile or
        filling its screen, is square: a tile has the window's line, a window filling its screen has none. The preview of
        either has the round corners of the window being dragged, so where it lies over another window it is a window's
        shape and not a cut through it; a preview of a tile has a line of the tint at COMPOSITOR_SNAP_LINE percent of the
        tint's own strength, and one filling the screen none. It lies under the window being dragged (compositor_preview_lift).
*/
#define COMPOSITOR_SNAP_PERCENT 40
#define COMPOSITOR_SNAP_PERCENT_MOST 60
#define COMPOSITOR_SNAP_READ 300
#define COMPOSITOR_SNAP_LINE 200
#define COMPOSITOR_SNAP_FADE_NS (120 * UI_MS)
// The grid's tiles: the gap between them, which a thumbnail keeps from its cell's edges too; a tile's tint, white at a quarter;
// and the tint of a region a window fills, black at a half (it is shown taken, not hidden).
#define COMPOSITOR_GRID_GAP 4
#define COMPOSITOR_GRID_TINT 0x40ffffffu
#define COMPOSITOR_GRID_TAKEN 0x80000000u
// The top of a window that is a titlebar with nothing drawn -- a press there takes the window whatever is
// under it -- and its close button, a disc that far in from the window's top right, in logical pixels.
#define COMPOSITOR_GRAB 24
// The minimise button beside the close one: the same size, the same gap between them, and its dash this far in from each side.
#define COMPOSITOR_MINIMIZE_IN 3
#define COMPOSITOR_RADIUS_MOST 64

// The kit's shared part (ui/ui.c): the switches, the theme's roles, the motion, the touch recogniser and what the taskbar is called by.
// The rest of the kit reads the compositor, so it is taken after this file (shell.c).
#define UI_SHARED_ONLY
#include "ui/ui.c"
#undef UI_SHARED_ONLY
#include "../audio/state.c" // the sound server's page: the tray's volume cell and the volume keys
#include "../audio/control.c" // and its socket, which the keys and the wheel tell the volume to at once
#if !defined(SHELL_NO_GPU) && !defined(MOONWATER_BUILD_TOOL)
#include "gpu.c" // GPU rendering, on unless the build's header says SHELL_NO_GPU (CONFIG_MOONWATER_GPU=n); the build tool, which makes its kernels, is without it
#else
// Without the GPU a client's dma-buf is never imported (no card to read it), so no picture is one the card reads.
static unsigned int gpu_epoch;
static int gpu_ready(void)
{
        return 0;
}
static positive gpu_import_fd(int fd, positive bytes)
{
        (void)fd, (void)bytes;
        return 0;
}
static void gpu_release_import(positive at)
{
        (void)at;
}
static void gpu_forget(const void *addr, positive bytes)
{
        (void)addr, (void)bytes;
}
static b32 gpu_idle(void)
{
        return true;
}
static positive gpu_va_bytes(const void *pointer, positive bytes)
{
        (void)pointer, (void)bytes;
        return 0;
}
static positive gpu_va_written(const void *pointer, positive bytes)
{
        (void)pointer, (void)bytes;
        return 0;
}
static b32 gpu_map_as(const void *addr, positive size, b32 written)
{
        (void)addr, (void)size, (void)written;
        return false;
}
#define gpu_map(addr, size) gpu_map_as(addr, size, 0)
#define gpu_map_written(addr, size) gpu_map_as(addr, size, 1)
static b32 gpu_blit(positive src, unsigned int sp, positive dst, unsigned int dp, positive tab, unsigned int op, unsigned int w, unsigned int h)
{
        (void)src, (void)sp, (void)dst, (void)dp, (void)tab, (void)op, (void)w, (void)h;
        return false;
}
#endif

static struct taskbar_view compositor_taskbar_view(void);
static unsigned int compositor_line_lit(void); // the line of the window with the keys, the theme's (compositor_chrome_now)
static unsigned int compositor_line_unlit(void); // the line of a window without the keys: the theme's outline
static unsigned int compositor_ink_shadow(void); // the chrome's shadow ink: the tray's and a tab's drawn without a font
static unsigned int compositor_ink_selection(void); // the chrome's selection, 0xaarrggbb: the snap preview's tint
static unsigned int compositor_snap_ink(void); // the snap's tint, 0xaarrggbb: the selection at its percent, read on the wallpaper
static void compositor_preview(int row); // the snap preview of the drag of row (none clears it), from the pointer
static unsigned int compositor_tab_target(b32 lit); // what a tab's glass is tinted toward, the theme's
static unsigned int compositor_ink_role(int role); // a role of the chrome's colours, 0xaarrggbb: the tray's cells (the notices dot, the microphone) are drawn in it

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch; // pixels a row
        int width, height;
        int x1, y1, x2, y2;  // the damage: nothing outside it is written
        int ox, oy;          // the point pixels[0] is, on the output (a corner's scratch); 0 for the output
        unsigned int *scratch; // where the scene draws a round corner's square; none is the one of its own
        int out;             // only written: a scanout, which nothing here reads back, so its copies stream past the cache
};

// The reads of a target only written, counted: none is the rule (compositor_scene_reads), and a count is a path that reads one.
unsigned long compositor_out_reads;

// What reads the target it draws on says so here: a target only written counts it.
static inline void compositor_reading(const struct compositor_target *t)
{
        if (t->out)
                __atomic_fetch_add(&compositor_out_reads, 1, __ATOMIC_RELAXED);
}

/*
        The compositor is one translation unit: this file and the thirteen parts it includes, in two runs, each part in
        the place its text had when it was all one file, so every static is defined where it was and nothing is declared
        twice. The order is load-bearing, for two reasons.

        The first four (compose.c, keymap.c, pointer.c, outputs.c) are the backend and come before the kernel's
        interfaces and the compositor's state below: the core_state lane cuts them out of this text by their section
        lines and builds them for CHECK_canvas_cells with no card and no compositor, so they name nothing declared after
        them, and the state itself is made of their types (struct compositor_frame, compositor_keymap, compositor_pointer,
        the pointer's sizes).

        The other nine come after the state, each after the parts whose types, macros and functions it uses: damage.c
        (what changed, which every later part says), glass.c, scanout.c (with record.h), gpu_host.c, present.c (which
        ties them together), kms.c (a take presents), session.c, manage.c and input.c. A call to a part later in the
        order is made through a declaration before it, so moving an include moves definitions past their callers and
        each such call needs one; a macro or a type cannot be declared ahead, and must stay above its first use. The
        table, the startup and the loop below the second run call into all of them, and the Wayland shim (wayland.c,
        wayland_host.c) is included after them for the same reason.
*/
#include "compositor/compose.c" // the look on the processor and the card's frames: windows, lines, corners, the panel's tabs, the scene and its plan
#include "compositor/keymap.c" // what a key means: the layouts, dead keys and the held modifiers
#include "compositor/pointer.c" // what a hand means: the gain, the repeat, the shake, the pointer's shapes and its picture
#include "compositor/outputs.c" // the screens one desktop spans: a point and a window kept on them, the snap's rules, a screen's cells

/*
        The DRM and evdev interface this needs, as the kernel's uapi lays it
        out. The ioctl numbers are _IOWR('d', n, size) with the size in bits
        16 to 29, and every struct is the same on the three machines.
*/
struct compositor_card_res
{
        unsigned long fbs, crtcs, connectors, encoders;
        unsigned int fb_count, crtc_count, connector_count, encoder_count;
        unsigned int min_width, max_width, min_height, max_height;
};

#define COMPOSITOR_MODE_USERDEF 32u // DRM_MODE_TYPE_USERDEF

struct compositor_mode
{
        unsigned int clock;
        unsigned short hdisplay, hsync_start, hsync_end, htotal, hskew;
        unsigned short vdisplay, vsync_start, vsync_end, vtotal, vscan;
        unsigned int vrefresh, flags, type;
        char name[32];
};

struct compositor_connector
{
        unsigned long encoders, modes, props, prop_values;
        unsigned int mode_count, prop_count, encoder_count;
        unsigned int encoder_id, connector_id, connector_type, connector_type_id;
        unsigned int connection, mm_width, mm_height, subpixel, pad;
};

struct compositor_encoder
{
        unsigned int encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
};

struct compositor_crtc
{
        unsigned long connectors;
        unsigned int connector_count, crtc_id, fb_id, x, y, gamma_size, mode_valid;
        struct compositor_mode mode;
};

struct compositor_dumb
{
        unsigned int height, width, bpp, flags, handle, pitch;
        unsigned long size;
};

struct compositor_map_dumb
{
        unsigned int handle, pad;
        unsigned long offset;
};

struct compositor_fb
{
        unsigned int fb_id, width, height, pitch, bpp, depth, handle;
};

struct compositor_dirty
{
        unsigned int fb_id, flags, colour, clip_count;
        unsigned long clips;
};

struct compositor_version
{
        int major, minor, patchlevel;
        unsigned long name_length;
        char *name;
        unsigned long date_length;
        char *date;
        unsigned long description_length;
        char *description;
};

_Static_assert(sizeof(struct compositor_version) == 64, "drm_version");

#define COMPOSITOR_VERSION COMPOSITOR_DRM(0x00u, 64u)

struct compositor_cursor
{
        unsigned int flags, crtc_id;
        int x, y;
        unsigned int width, height, handle;
};

_Static_assert(sizeof(struct compositor_card_res) == 64, "drm_mode_card_res");
_Static_assert(sizeof(struct compositor_mode) == 68, "drm_mode_modeinfo");
_Static_assert(sizeof(struct compositor_connector) == 80, "drm_mode_get_connector");
_Static_assert(sizeof(struct compositor_crtc) == 104, "drm_mode_crtc");
_Static_assert(sizeof(struct compositor_dumb) == 32, "drm_mode_create_dumb");
_Static_assert(sizeof(struct compositor_fb) == 28, "drm_mode_fb_cmd");
_Static_assert(sizeof(struct compositor_dirty) == 24, "drm_mode_fb_dirty_cmd");
_Static_assert(sizeof(struct compositor_cursor) == 28, "drm_mode_cursor");

#define COMPOSITOR_DRM(nr, size) (0xc0000000u | (size) << 16 | 0x6400u | (nr))
#define COMPOSITOR_SET_MASTER 0x641eu
#define COMPOSITOR_GETRESOURCES COMPOSITOR_DRM(0xa0u, 64u)
#define COMPOSITOR_GETCRTC COMPOSITOR_DRM(0xa1u, 104u)
#define COMPOSITOR_SETCRTC COMPOSITOR_DRM(0xa2u, 104u)
#define COMPOSITOR_CURSOR COMPOSITOR_DRM(0xa3u, 28u)
#define COMPOSITOR_GETENCODER COMPOSITOR_DRM(0xa6u, 20u)
#define COMPOSITOR_GETCONNECTOR COMPOSITOR_DRM(0xa7u, 80u)
#define COMPOSITOR_ADDFB COMPOSITOR_DRM(0xaeu, 28u)
#define COMPOSITOR_DIRTYFB COMPOSITOR_DRM(0xb1u, 24u)
#define COMPOSITOR_CREATE_DUMB COMPOSITOR_DRM(0xb2u, 32u)
#define COMPOSITOR_MAP_DUMB COMPOSITOR_DRM(0xb3u, 16u)
// A window shown straight from its buffer: its pages a dma-buf (udmabuf), imported, a framebuffer, flipped to.
#define COMPOSITOR_RMFB COMPOSITOR_DRM(0xafu, 4u)
#define COMPOSITOR_PAGE_FLIP COMPOSITOR_DRM(0xb0u, 24u)
#define COMPOSITOR_ADDFB2 COMPOSITOR_DRM(0xb8u, 104u)
#define COMPOSITOR_PRIME_FD_TO_HANDLE COMPOSITOR_DRM(0x2eu, 12u)
#define COMPOSITOR_GEM_CLOSE 0x40086409u    // DRM_IOW(0x09, struct drm_gem_close)
#define COMPOSITOR_UDMABUF_CREATE 0x40187542u // _IOW('u', 0x42, struct udmabuf_create)
#define COMPOSITOR_XRGB8888 0x34325258u
#define COMPOSITOR_FLIP_EVENT 1u
#define COMPOSITOR_FLIP_ASYNC 2u
#define COMPOSITOR_EVENT_FLIP_COMPLETE 2u

struct compositor_input_event
{
        long seconds, microseconds;
        unsigned short type, code;
        int value;
};

// A record as the loop reads it (evdev_decode, src/lib.c): the device's stamp in nanoseconds, and the type, code and value as they lay.
struct compositor_report_event
{
        unsigned long long stamp;
        unsigned short type, code;
        int value;
};
_Static_assert(sizeof(struct compositor_input_event) == 24 && sizeof(struct compositor_report_event) == 16, "what evdev_decode reads and writes");

struct compositor_absinfo
{
        int value, minimum, maximum, fuzz, flat, resolution;
};

#define COMPOSITOR_EVIOCGBIT(type, bytes) (0x80000000u | (bytes) << 16 | 0x4500u | (0x20u + (type)))
#define COMPOSITOR_EVIOCGABS(axis) (0x80184540u + (axis))
#define COMPOSITOR_EVIOCGID 0x80084502u // bus, vendor, product, version
#define COMPOSITOR_EVIOCGNAME(bytes) (0x80000000u | (bytes) << 16 | 0x4506u)
#define COMPOSITOR_EVIOCGPROP(bytes) (0x80000000u | (bytes) << 16 | 0x4509u)
#define COMPOSITOR_SYN_DROPPED 3u
#define COMPOSITOR_BTN_TOOL_FINGER 0x145u
#define COMPOSITOR_BTN_TOOL_QUINTTAP 0x148u // five fingers
#define COMPOSITOR_BTN_TOOL_DOUBLETAP 0x14du
#define COMPOSITOR_BTN_TOOL_TRIPLETAP 0x14eu
#define COMPOSITOR_BTN_TOOL_QUADTAP 0x14fu
#define COMPOSITOR_ABS_MT_POSITION_X 0x35u

/*
        A touchpad. A finger on it is a mouse, read from where the finger is against where it was at the last
        report, so the pointer goes on from where it was and not to the place on the screen the place on the
        pad would be, as a tablet's does. Two fingers on it are the wheel: up as they move up, right as they
        move right, as a mouse's notches go. A touch is no press, or every hand put on the pad would be a click:
        the pad's own button is (a clickpad's is the whole pad, pressed down), and pressed with two fingers on
        the pad it is the right button. The rates are of the hand, by the pad's resolution: a millimetre a
        finger moves is COMPOSITOR_PAD_COUNTS of a mouse's counts (half of what a 1000 dpi mouse says), which
        the pointer's curve and sensitivity then take as they take a mouse's, and a millimetre two fingers move
        is COMPOSITOR_PAD_WHEEL 120ths of a notch. A notch is a row (40 device pixels at 100 percent, the kit's UI_ROW), so 12
        is four device pixels a millimetre at 100 percent: a two-finger swipe of 40 mm moves the content 160 px, as far as the
        same finger would move on a 21.5 inch 1920 by 1080 panel (1920 px over 476 mm is 4.03 px a mm). A pad that does not say its
        resolution is taken as a hundred millimetres wide, its height in the same units.
*/
#define COMPOSITOR_PAD_COUNTS 20
#define COMPOSITOR_PAD_WHEEL 12

/*
        A wheel's events from one device that come no more than UI_HAND_GAP_NS apart are a stream: a trackpad's motion as the wheel
        reports it (a notch to every report, tens to 120 a second, or hi-res 120ths of a finger's pixels). A stream's notch is the
        gain's pixels (compositor_stream_gain, 8 to 32 px a notch) in 120ths of UI_NOTCH, sent as a precise turn (WINDOW_WHEEL_PRECISE
        and WINDOW_WHEEL_STREAM) that the body follows. A notch after a longer gap is a lone notch, stepped as a row, and so is the
        first notch of a stream, since nothing says yet that a stream has begun.
*/
#define COMPOSITOR_WHEEL_STREAMS 3          // stream notches in a gesture before the device is known to stream
#define COMPOSITOR_WHEEL_MEMORY_NS 3000000000ull // how long a device that streamed keeps the stream for its next gesture's first notch
#define COMPOSITOR_GAIN_BASE 2048           // the base gain, in 256ths of a device pixel a notch: 8 px
#define COMPOSITOR_STREAM_HIST 6            // the stream notches the rate is measured over (about 50 to 100 ms)
#define COMPOSITOR_GAIN_TURN (256 * 100 * UI_NOTCH) // the stream turn's divisor: 120ths of a notch, 256ths of a pixel, percent, over the notch's pixels
#define COMPOSITOR_PAD_WIDTH_MM 100

struct compositor_pad
{
        int next_x, next_y;     // where the pad says the finger is, in its units, kept from report to report
        int at_x, at_y;         // and where it was at the last report
        int moving;             // at_x and at_y are a place this touch moves from: not at its first report
        int fingers, touching;  // on the pad, as its tool keys say, and whether one touches it
        int turned;             // two fingers have turned the wheel since the fingers landed: their lift is that turn's stop (WINDOW_WHEEL_STOP)
        int per_mm_x, per_mm_y; // its units to a millimetre
        int left_x, left_y;     // what a move or a turn left under a whole count or 120th, times the units to a millimetre
        unsigned int clicked;   // the button the pad's press went down as
        unsigned long long gain_at; // when its last two-finger report was read (ns), for the speed of the hand
        int gain_left_x, gain_left_y; // what the speed's gain left under a whole 120th, in 256ths
        // A multitouch pad's fingers (ABS_MT_SLOT, TRACKING_ID and POSITION_X and Y, compositor_pad_slot): a pinch is the span of
        // two of them (compositor_pad_pinch), and is read from these, not from the primary finger the wheel reads.
        int slot;                           // the slot the next events are about
        int mt_down[UI_TOUCH_SLOTS];        // a finger is down in the slot
        int mt_x[UI_TOUCH_SLOTS], mt_y[UI_TOUCH_SLOTS]; // its place, in the pad's units
        int pinch_count, pinch_ref;         // the fingers down the span was last measured with, and that span (micro-millimetres)
        int pinching, pinch_scale;          // a pinch is under way, and the scale it last reported (1024ths)
};

/*
        A touch screen: a screen the fingers are on (INPUT_PROP_DIRECT, BTN_TOUCH or slots, and no pen). It is read by
        its slots (ABS_MT_SLOT, ABS_MT_TRACKING_ID and ABS_MT_POSITION_X and Y), each a finger, and only once a report
        is over: the kernel says BTN_TOUCH before the place it made of the first finger (ABS_X and ABS_Y), so a press
        read where it is said went down where the last finger was lifted, and that place is the oldest finger's, which a
        second finger takes when the first is lifted. A screen of one finger and no slots is slot 0, BTN_TOUCH saying
        whether it is down. Each finger is its id (-1: none) and its place in the device's units; was is the id the
        last report ended with.
*/
#define COMPOSITOR_ABS_MT_SLOT 0x2fu
#define COMPOSITOR_ABS_MT_POSITION_Y 0x36u
#define COMPOSITOR_ABS_MT_TRACKING_ID 0x39u
#define COMPOSITOR_BTN_TOOL_PEN 0x140u

struct compositor_touch_screen
{
        int slot, slots;   // the slot the events are about, and whether it has slots at all
        int range[4];      // x from and to, y from and to, in its units
        int dropped;       // the kernel dropped events: what comes until the report's end is not to be trusted
        int id[UI_TOUCH_SLOTS], was[UI_TOUCH_SLOTS], x[UI_TOUCH_SLOTS], y[UI_TOUCH_SLOTS];
};

/*
        What the fingers on a touch screen are doing, one screen at a time (device, -1 for none). The finger that went
        down first is the pointer: its press is the left button, at once, where it went down; it moves nothing until it
        is past the slop (moved), and then it is the pointer's motion. A second finger before the first has moved makes
        it two (mode 2): the left press is taken back (a release that says it was no click) and the two are the wheel
        from their middle; lifted soon and neither moved, they are a click of the right button. Held still, the first is
        the right button (mode 3, nothing more until every finger is up). hidden: a finger was the last thing used, so
        the pointer is not shown under it.
*/
/*
        The fingers' gesture is the kit's (ui/ui.c: the modes, the slop, the hold, the wheel, the taps); here are the compositor's
        parts of it: the device it is on, the windows its events land on, and the close and hold rows.
*/
struct compositor_touch
{
        struct ui_touch gesture; // what the fingers mean (ui/ui.c)
        int device;              // the touch screen the gesture is on
        int hidden;              // a finger was the last thing used: the pointer under it is the dynamic one, whatever the style says
        int closing;             // the window whose close button the finger went down on, which a tap closes
        int resumed;             // the press is being made again without the close button (compositor_touch_resume)
        int hold_row;            // the window a long press landed on, which its lift is told, or COMPOSITOR_NONE
        int holding;             // the long press's right button is being sent: its events carry WINDOW_KEY_HOLD
        int physical;            // the finger is the machine's own (compositor_input_physical of its device at its start), so its posts are
        int x, y;                // the last place a finger reported: a long press is placed there, whatever the pointer is
        int pinch_row;           // the window a pinch began on (its focal point then), which its updates and end go to, or COMPOSITOR_NONE
        int pinch_level;         // a legacy window's pinch: the scale (1024ths) of the last Ctrl and wheel notch sent
};

/*
        The screen edges (ui/ui.c). A finger that lands in the top or the bottom band of its screen is the system's until it
        is decided: the kit is not told of it (COMPOSITOR_SWIPE_UNDECIDED), and the window under it gets nothing. A decided swipe is
        the system's (COMPOSITOR_SWIPE_SWIPED) until every finger is up. A finger that is not a swipe after all is handed to the
        kit as it would have been without the band: its landing, then the report as it is (compositor_swipe_hand). The landing
        frame is kept for that, and the deadline (ui_hold_ns) is the time a finger in the band may take to be decided.
        A window that covers its screen (COMPOSITOR_MAXIMIZED) takes a first swipe as its own, with an indicator at the edge;
        a second within COMPOSITOR_SWIPE_ARM_NS is the system's.
*/
#define COMPOSITOR_SWIPE_NONE 0
#define COMPOSITOR_SWIPE_UNDECIDED 1
#define COMPOSITOR_SWIPE_SWIPED 2
#define COMPOSITOR_SWIPE_ARM_NS 2000000000ull // a second swipe into a window that covers its screen, within this, is the system's
#define COMPOSITOR_SWIPE_LAUNCHER_DP 48       // the launcher's rectangle when a swipe opens it: its width, beside the finger
// What a side edge runs, as the settings say it (0 off, the default): the window switcher's step, or on the left the launcher and on the right the volume popup.
#define COMPOSITOR_SIDE_OFF 0
#define COMPOSITOR_SIDE_WINDOWS 1
#define COMPOSITOR_SIDE_ACT 2
struct compositor_swipe
{
        int state;                     // COMPOSITOR_SWIPE_NONE, _UNDECIDED or _SWIPED
        int device;                    // the touch screen the finger is on
        int slot;                      // the finger's slot
        int side;                      // the UI_EDGE_* side it landed in
        int cell;                      // a control's cell, when it landed on one (side UI_EDGE_CONTROL)
        int row;                       // the window under it when it landed, or COMPOSITOR_NONE
        int rect[4];                   // the screen it landed on, in the desktop's pixels, and its number
        int screen;
        int x0, y0;                    // where it landed
        positive deadline;             // when an undecided finger is no swipe any more
        struct ui_touch_frame landing; // the report it landed in, which the kit is given if it is not a swipe
        int armed_row;                 // the window whose first swipe was handed to it, or COMPOSITOR_NONE
        positive armed_until;          // until when a second swipe into armed_row is the system's
};

/*
        The chip: what a swipe shows over the desktop, drawn by the compositor over the scene (as the marks are). A controls chip
        (COMPOSITOR_CHIP_CONTROLS) is the window's controls: close, minimise and maximise, three cells over the top band of the
        window it was swiped down on. It slides down into its place (the kit's ease), holds for
        COMPOSITOR_CHIP_HOLD_NS, and fades over COMPOSITOR_CHIP_FADE_NS; the slide and the fade are at the pace (ui_paced) and the
        hold is not. An indicator (COMPOSITOR_CHIP_HINT) is a bar at the edge
        the first swipe into a full-screen window came from. Its coverage is kept in cover: the body, then the glyphs, w x h each.
*/
#define COMPOSITOR_CHIP_NONE 0
#define COMPOSITOR_CHIP_CONTROLS 1
#define COMPOSITOR_CHIP_HINT 2
#define COMPOSITOR_CHIP_DRAG 3              // a drag's, beside the pointer (compositor_chip_drag)
#define COMPOSITOR_CHIP_DRAG_GAP_DP 18      // the drag's chip's top left from the pointer
#define COMPOSITOR_CHIP_W_MOST 960            // the largest chip in device pixels: three cells at 800 percent
#define COMPOSITOR_CHIP_H_MOST 256            // and the strip's height at 800 percent
#define COMPOSITOR_CHIP_HOLD_NS 3000000000ull // a controls chip is shown this long, then fades
#define COMPOSITOR_CHIP_FADE_NS 300000000ull
#define COMPOSITOR_CHIP_HINT_NS 1500000000ull // an indicator is shown this long
#define COMPOSITOR_CHIP_SLIDE_NS 180000000ull // a controls chip slides in over this
#define COMPOSITOR_CHIP_TOP_DP 6              // a controls chip's top, below its window's top edge
#define COMPOSITOR_CHIP_HEIGHT_DP 32          // a controls chip's height
#define COMPOSITOR_CHIP_GLYPH_DP 6            // a glyph's half size
#define COMPOSITOR_CHIP_HINT_W_DP 48
#define COMPOSITOR_CHIP_HINT_H_DP 4
#define COMPOSITOR_CHIP_HINT_MARGIN_DP 6      // the indicator's gap from its screen's edge
#define COMPOSITOR_CHIP_BODY 0x1e1e22u        // the strip's body, and the glyphs and the indicator
#define COMPOSITOR_CHIP_GLYPH 0xe4e4e4u
#define COMPOSITOR_CHIP_BODY_COVER 224        // the body's strength, of 255
struct compositor_chip
{
        int kind;                  // COMPOSITOR_CHIP_NONE, _CONTROLS or _HINT
        int row;                   // CONTROLS: the window it is of (the box follows it)
        int rel_x, rel_y;          // CONTROLS: the top left in the window's pixels; HINT: its place in the desktop
        int w, h;                  // the size in device pixels
        int cells;                 // CONTROLS: its cells, 3 (close, minimise, maximise), or 2 for a window with no tab, which has no minimise
        int slide;                 // CONTROLS: how far above its place it is still, in device pixels
        int drawn[5];              // the box and alpha last drawn: x, y, w, h, alpha (what damage is taken for)
        unsigned int alpha;        // 0..255 now
        positive shown;            // when it was shown
        positive hold, fade;       // it holds this long, then fades over this long
        unsigned char *cover;      // the coverage: the body's w x h, then the glyphs' (COMPOSITOR_CHIP_W_MOST x COMPOSITOR_CHIP_H_MOST each, mapped once)
        unsigned int *scratch;     // the scene under the chip, composed for a target that is only written (the scanout)
};

#define COMPOSITOR_EV_KEY 1u
#define COMPOSITOR_EV_REL 2u
#define COMPOSITOR_EV_ABS 3u
#define COMPOSITOR_EV_SW 5u
#define COMPOSITOR_REL_X 0u
#define COMPOSITOR_REL_Y 1u
#define COMPOSITOR_REL_WHEEL 8u
#define COMPOSITOR_REL_HWHEEL 6u
#define COMPOSITOR_REL_WHEEL_HI 11u
#define COMPOSITOR_REL_HWHEEL_HI 12u
#define COMPOSITOR_EV_SYN 0u
#define COMPOSITOR_BTN_TOUCH 0x14au
#define COMPOSITOR_EVIOCSCLOCKID 0x400445a0u
#define COMPOSITOR_EVIOCGREP 0x80084503u // the delay and the period of a keyboard's repeat, in milliseconds
#define COMPOSITOR_EVIOCSREP 0x40084503u
#define COMPOSITOR_BTN_LEFT 0x110u
#define COMPOSITOR_BTN_RIGHT 0x111u
#define COMPOSITOR_BTN_MIDDLE 0x112u
#define COMPOSITOR_BTN_TASK 0x117u
#define COMPOSITOR_KEY_A 30u
#define COMPOSITOR_KEY_POWER 116u

/*
        The rows of the window table in use: the high-water of the rows the compositor has made doors for, which the table publishes
        (windows.rows) and every walk of the rows goes to, where it went to WINDOWS. COMPOSITOR_ROWS_FIRST are made with the table, and
        more as they are wanted (compositor_rows_grow): whenever fewer than COMPOSITOR_ROWS_SPARE of them are free at a pass of the loop
        (compositor_rows_spare), when a program knocks for one it did not find (the clipboard's door wakes the loop) and when a client's
        window finds none. A step is half the rows in use, or the spares, whichever is more; the rows the table has room for
        (compositor_rows_room, its capacity) are the end of it, and a row whose memory, door or thread the kernel refuses is where the
        growth stops, until it is asked again.
*/
#define COMPOSITOR_ROWS_FIRST 32
#define COMPOSITOR_ROWS_SPARE 16
static unsigned int compositor_rows_in_use = COMPOSITOR_ROWS_FIRST;
static unsigned int compositor_rows_room = WINDOWS;
static unsigned int compositor_rows_grow(unsigned int want);
static unsigned int compositor_rows_made; // the rows whose memory is committed (compositor_rows_make)
// A row of the compositor's is one below the rows in use: a number past them (a program's word in the table, a client's) names no row,
// and its memory is not there to be read (compositor_rows_make).
#define COMPOSITOR_ROWS ((int)compositor_rows_in_use)

/*
        A set of rows, a bit each, with one past them (the doors' slot that is no row, COMPOSITOR_DOOR_MISC): what the masks of a
        bit for each row were, 32 bits that could not name a row past 31. The words walked are those of the rows in use and the
        last (rows_walk); none is ever set past them, so a set copied, emptied or joined to another costs the rows in use and not
        WINDOWS of them (rows_copy for a copy: an assignment of the struct is every word).
*/
#define COMPOSITOR_ROW_WORDS ((WINDOWS + 1 + 63) / 64)
struct compositor_rows
{
        unsigned long long word[COMPOSITOR_ROW_WORDS];
};

static inline unsigned int compositor_rows_words(void)
{
        unsigned int used = (compositor_rows_in_use + 63) / 64;

        return used < COMPOSITOR_ROW_WORDS ? used : COMPOSITOR_ROW_WORDS;
}

static inline int rows_has(const struct compositor_rows *s, long at)
{
        return at >= 0 && at <= WINDOWS && (s->word[at / 64] >> (at % 64) & 1u);
}

static inline void rows_add(struct compositor_rows *s, long at)
{
        if (at >= 0 && at <= WINDOWS)
                s->word[at / 64] |= 1ULL << (at % 64);
}

static inline void rows_del(struct compositor_rows *s, long at)
{
        if (at >= 0 && at <= WINDOWS)
                s->word[at / 64] &= ~(1ULL << (at % 64));
}

// The words of the rows in use, then the last (the misc slot's) when it is not among them: the k-th word walked.
static inline unsigned int rows_walked(void)
{
        unsigned int used = compositor_rows_words();

        return used < COMPOSITOR_ROW_WORDS ? used + 1 : used;
}

static inline unsigned int rows_walk(unsigned int k)
{
        return k < compositor_rows_words() ? k : COMPOSITOR_ROW_WORDS - 1;
}

// Taken from another thread's set and emptied there, word by word (each word's exchange is the thread's or-fetch's partner).
static inline void rows_take(struct compositor_rows *into, struct compositor_rows *from)
{
        for (unsigned int k = 0, walked = rows_walked(), w; k < walked; k++)
                w = rows_walk(k), into->word[w] = from->word[w] ? __atomic_exchange_n(&from->word[w], 0ULL, __ATOMIC_ACQ_REL) : 0;
}

static inline void rows_copy(struct compositor_rows *into, const struct compositor_rows *from)
{
        for (unsigned int k = 0, walked = rows_walked(), w; k < walked; k++)
                w = rows_walk(k), into->word[w] = from->word[w];
}

static inline void rows_add_atomic(struct compositor_rows *s, long at)
{
        if (at >= 0 && at <= WINDOWS)
                __atomic_or_fetch(&s->word[at / 64], 1ULL << (at % 64), __ATOMIC_RELEASE);
}

static inline int rows_any(const struct compositor_rows *s)
{
        for (unsigned int w = 0; w < compositor_rows_words(); w++)
                if (s->word[w])
                        return 1;
        return s->word[COMPOSITOR_ROW_WORDS - 1] != 0;
}

static inline void rows_none(struct compositor_rows *s)
{
        for (unsigned int w = 0; w < compositor_rows_words(); w++)
                s->word[w] = 0;
        s->word[COMPOSITOR_ROW_WORDS - 1] = 0;
}

static inline void rows_or(struct compositor_rows *into, const struct compositor_rows *with)
{
        for (unsigned int k = 0, walked = rows_walked(), w; k < walked; k++)
                w = rows_walk(k), into->word[w] |= with->word[w];
}

// The first 64 rows as a word, and a set of them from one: what the checks compare and set (their tables have a few rows).
static inline unsigned long long rows_low(const struct compositor_rows *s)
{
        return s->word[0];
}

static inline struct compositor_rows rows_from(unsigned long long low)
{
        struct compositor_rows s = {{0}};

        s.word[0] = low;
        return s;
}

static inline void rows_and_not(struct compositor_rows *into, const struct compositor_rows *without)
{
        for (unsigned int k = 0, walked = rows_walked(), w; k < walked; k++)
                w = rows_walk(k), into->word[w] &= ~without->word[w];
}

struct compositor_poll
{
        int handle;
        short events, returned;
};

#define COMPOSITOR_POLLIN 1
#define COMPOSITOR_POLLHUP 16

// The input devices read, and the evdev numbers the hotplug watch can name: as many as the kernel has, INPUT_MAX_CHAR_DEVICES (1024, the
// input subsystem's character minors: eventN is below it, its dynamic numbers 256 to 1023), where it read 32 and named event0 to event63.
#define COMPOSITOR_INPUTS 1024
#define COMPOSITOR_INPUTS_KEPT 32 // the descriptors kept back from the rows for the inputs (a lid, the jacks, a power button, a mouse plugged in later)
#define COMPOSITOR_WHEEL_JOIN 960 // eight notches, in 120ths: the most one wheel event joined from several holds
#define COMPOSITOR_DEVICES COMPOSITOR_INPUTS
// A machine slept when the clock that runs through a sleep got this far ahead of the one that does
// not (or the sleep verb said so), and the screens are taken again, every crtc set anew, at half a
// second and at two and a half: a card's driver and a monitor are still settling after a wake.
#define COMPOSITOR_SLEPT_NS 50000000ull
#define COMPOSITOR_WAKE_FIRST_NS 500000000ull
#define COMPOSITOR_WAKE_SECOND_NS 2000000000ull
// A drag that ends this near the edge of the output fills it or half of it. (Double presses are the kit's click count, ui_click_again.)
#define COMPOSITOR_TRAY TASKBAR_TRAY // taskbar_hit_reach's answer for the tray

// The band at a window's edges a press resizes it by -- six pixels outside them and two inside, so a hand
// a pixel short of a line one pixel wide still has it; `moonwater canvas border grip` sets the six for a
// pointer, 2 to 16 (compositor_grip) -- and the step a new window is put down and across from one in its
// place, in logical pixels.
#define COMPOSITOR_GRIP 6
#define COMPOSITOR_GRIP_IN 2
/*
        A finger on a touch screen, in logical pixels and nanoseconds. It is wider than a pixel and shakes as it lands,
        so what it takes is wider than what a pointer takes: a window's edge is COMPOSITOR_TOUCH_GRIP outside it and
        COMPOSITOR_TOUCH_GRIP_IN inside (the top as a pointer's, where the titlebar is), a corner is UI_TARGET each way
        (compositor_corner_reach, the kit's one rule for a finger's target, ui_reach_hit) and COMPOSITOR_TOUCH_CORNER along
        either edge from it, a tab reaches the finger's UI_TARGET further toward the middle of its screen and to half the gap
        either side, and the close and the minimise button are the finger's within COMPOSITOR_TOUCH_BUTTON_SLOP of their disc
        (the window goes when the finger lifts from the X: one that moves past the slop was making the press the corner or the
        titlebar gives, and makes it). It moves nothing until it is UI_SLOP from where it went down, so a tap is a click
        where it landed and not a drag. Held ui_hold_ns without moving it is a long press: the right button,
        flagged WINDOW_KEY_HOLD, and then the finger may move on, the pointer following it, until it lifts, which ends the
        press with a release (CANCEL and HOLD) in the window it landed on. Two fingers lifted within UI_TOUCH_TAP_NS
        of the second landing, neither moved, are the right button too; two that move are a precise turn of the wheel in
        UI_NOTCH's 120ths, one for one with the fingers' middle, what is under them following them (the same path as a
        trackpad's two fingers, compositor_pad_apply).
*/
#define COMPOSITOR_TOUCH_GRIP 16
#define COMPOSITOR_TOUCH_GRIP_IN 8
// A finger's target on a window's corners and edges, in logical pixels: 44, as before. The kit's controls take UI_TARGET (48 dp), but
// a window's corner grown to 48 reaches the titlebar and the minimise and close buttons (compositor_corner_reach, the top band), so
// the window keeps its own size until its titlebar is laid out for it (the corner and touch hold rows show the overlap at 48).
#define COMPOSITOR_FINGER_TARGET 44
#define COMPOSITOR_TOUCH_CORNER 24
// The close and the minimise button of a window, to a finger: the disc and this much more on every side, in logical pixels. The
// corner is a finger's UI_TARGET, and wins everywhere in it but the disc (compositor_corner_reach, compositor_on_button).
#define COMPOSITOR_TOUCH_BUTTON_SLOP 4
/*
        The prediction a frame ahead, for a touch screen's fingers and a tablet or pen. The mouse is not predicted: its lead is
        always zero and it is never sampled, so its pointer is its real place. The pointer is a finger's or a pen's real place
        (compositor.real_x, real_y) led by the class's lead over one frame, no further than the cap, and held on a screen. The pointer is where every consumer of motion reads it (the cursor, the windows told a motion, drags, hovers,
        snaps), so the lead lives at the root of input; a press or a release is handled at the real place (compositor.at_real),
        so a click lands where the hand was. A class with no new place for a frame is back on its real place, so a stop is not
        carried past the cap.

        The lead is a least-squares slope over the places of the last COMPOSITOR_PREDICT_WINDOW_NS, each in 1/1024 of a pixel
        (a mouse's place keeps the fraction its acceleration left) and at the time its device reported it. It is zero for a
        motion that is still (a hand that moved less than COMPOSITOR_PREDICT_STILL_LO to COMPOSITOR_PREDICT_STILL_HI
        1/1024 px a millisecond over the window, ramped), for a motion that is a wiggle (a net move under
        COMPOSITOR_PREDICT_STRAIGHT_LO thousandths of the path it took, ramped), and for an axis whose newest step turned back.
*/
#define COMPOSITOR_PREDICT_MOUSE 0
#define COMPOSITOR_PREDICT_TOUCH 1
#define COMPOSITOR_PREDICT_PEN 2
#define COMPOSITOR_PREDICTS 3
#define COMPOSITOR_PREDICT_CAP 4                  // logical pixels a lead may take a place past its newest
#define COMPOSITOR_PREDICT_FX 1024                // places and leads are in 1/1024 of a pixel
#define COMPOSITOR_PREDICT_HISTORY 16             // the newest places held per class
#define COMPOSITOR_PREDICT_WINDOW_NS 50000000ull  // the places a fit is taken over: the last 50 ms of their device times
#define COMPOSITOR_PREDICT_SPAN_NS 4000000ull     // a fit needs places spanning this much, or the batch of one read is no motion
#define COMPOSITOR_PREDICT_STILL_LO 40            // the speed, in 1/1024 px a ms over the window, below which there is no lead
#define COMPOSITOR_PREDICT_STILL_HI 80            // and the speed at which the whole lead is given
#define COMPOSITOR_PREDICT_STRAIGHT_LO 600        // the net move over the path, in thousandths, below which there is no lead
#define COMPOSITOR_PREDICT_STRAIGHT_SPAN 300      // and the net move over the path at which the whole lead is given

struct compositor_predict
{
        long long x[COMPOSITOR_PREDICT_HISTORY], y[COMPOSITOR_PREDICT_HISTORY]; // the places the class reported, in 1/1024 px
        positive when[COMPOSITOR_PREDICT_HISTORY];   // when each was reported, by its device (the event's time, or the arrival)
        positive arrived[COMPOSITOR_PREDICT_HISTORY]; // when each was read by the compositor
        unsigned int count; // how many of them are held, the newest last
        positive last;      // when the newest was read (a new place, not a repeat of one)
        long long lead_x, lead_y; // how far ahead of the newest place it is shown or delivered, in 1/1024 px; 0 for none
};

// A lead in 1/1024 px, as the whole pixel it is shown at (to the nearest).
static long long compositor_predict_px(long long fx)
{
        long long half = COMPOSITOR_PREDICT_FX / 2;

        return fx >= 0 ? (fx + half) / COMPOSITOR_PREDICT_FX : -((-fx + half) / COMPOSITOR_PREDICT_FX);
}
static void compositor_pointer_sync(void);
static void compositor_pointer_shown(void);

// How long a finger's mark fades once it is up.
#define COMPOSITOR_SPOT_FADE_NS 1000000000ull
#define COMPOSITOR_STEP 20
#define COMPOSITOR_EDGE_LEFT CP_EDGE_LEFT
#define COMPOSITOR_EDGE_RIGHT CP_EDGE_RIGHT
#define COMPOSITOR_EDGE_TOP CP_EDGE_TOP
#define COMPOSITOR_EDGE_BOTTOM CP_EDGE_BOTTOM
// The band at the top of an output that brings the grid up, and the Display map's snap band, in logical pixels; and how
// long a drag's pointer rests in it before the grid comes (a window thrown to the top is filled, and not caught by it).
#define COMPOSITOR_SNAP 8
#define COMPOSITOR_GRID_DWELL_NS (400 * UI_MS)
// The snap's dead zone, in logical pixels: a window's edge touched at its screen's edge holds until it is this far inside
// (compositor_snap_rule). A touch is at 0 px, with no band in front of it.
#define COMPOSITOR_SNAP_EDGE 8
// The snap's corner dead zone, in logical pixels: a corner whose two edges are both this near its screen's corner, with
// one of them touched, is the quarter, and wins over the edge (compositor_snap_rule).
#define COMPOSITOR_SNAP_CORNER 16
// The indicator of a shared edge: its half thickness across the edge, in logical pixels (compositor_shared_strip).
#define COMPOSITOR_SHARED_MARK 2
// How long a pointer that was shaken stays large.
#define COMPOSITOR_MAGNIFIED_NS 1200000000ull
#define COMPOSITOR_DAMAGE 8
#define COMPOSITOR_ENOENT 2
#define COMPOSITOR_EAGAIN 11
#define COMPOSITOR_EBUSY 16
#define COMPOSITOR_ENODEV 19
#define COMPOSITOR_EINVAL 22
#define COMPOSITOR_ENOSPC 28
#define COMPOSITOR_IN_CLOSE_WRITE 0x8u
#define COMPOSITOR_IN_CREATE 0x100u
#define COMPOSITOR_IN_MOVED_TO 0x80u
#define COMPOSITOR_NONBLOCK 0x800
#define COMPOSITOR_CLOEXEC 0x80000
// No row: no window has the keys, is under the pointer, is held.
#define COMPOSITOR_NONE (-1)
#define COMPOSITOR_OVER (-1) // damage_by: the pointer or the tabs, over every window and under none

/*
        A screen the compositor drives: its connector and the crtc that scans
        it, the mode that crtc is at, and its name. Its rectangle of the space
        is compositor.screen at the same index. The cursor plane is a crtc's,
        so what each screen's plane was given is its own: shows is the shape
        it wears (the paint it was given, 0 for none), at x, y on the screen.
*/
struct compositor_output
{
        unsigned int crtc, crtc_index, connector;
        struct compositor_mode mode;
        char name[WINDOWS_OUTPUT_NAME], model[WINDOWS_OUTPUT_MODEL];
        int scale;             // the percent this screen is drawn at: its displays line's, 100 when none
        unsigned int shows;
        int cursor_x, cursor_y;
        // The framebuffer its crtc shows: on one screen the scanout's own, and on more a framebuffer of the same buffer
        // for each, so a DIRTYFB of one screen's change commits that screen's crtc and waits on its vertical blank
        // and on no other's: one framebuffer every crtc showed held a 60 Hz screen to a 50 Hz one's pace.
        unsigned int fb;
        // The refresh rates it lists at its size (hz_count of them), and its turn in degrees with whether Canvas does
        // the turn (turn_software); the table says them to the Displays field (compositor_table_outputs).
        unsigned int hz[WINDOWS_OUTPUT_HZ], hz_count;
        unsigned int turn, turn_software;
        // The turn its displays line asks for: degrees, and turned 0 none, 1 by the card (its primary plane's property
        // turn_property, set to turn_value on turn_plane) or 2 by Canvas in software.
        unsigned int degrees, turned, turn_plane, turn_property;
        unsigned long turn_value;
};

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch; // pitch in bytes
        unsigned int fb, crtc;
        // Where a store to the card is dear: slices of the scene composed apart, and what the card holds (mirror, words a row
        // mirror_pitch; the scanout itself where the card's memory is the machine's). changed: 0 not yet asked, 1 on,
        // -1 off. mirror_stale: the card may hold anything, so the next present stores all it composes. changed_turned:
        // whether a screen was turned in software when changed was decided (a turn reads the scene whole, from memory that is
        // the machine's, so it is decided again when that changes).
        unsigned int *composing, *mirror;
        unsigned long mirror_pitch;
        int changed, mirror_stale, changed_turned;
        int width, height;   // the space every screen is a rectangle of, and the scanout that is as large
        /*
                The screens, how many there are (none listed is one screen, the whole space: a headless
                compositor, a check), and each one's rectangle of the space, x, y, width, height, in the order of
                the table's list: the primary first. layout counts the times the list changed.
        */
        struct compositor_output output[WINDOWS_OUTPUTS];
        int screen[WINDOWS_OUTPUTS][4];
        unsigned int outputs, layout;
        /*
                What every window's buffer is made for, for the session: stride the pixels a row of it (the
                widest screen connected when the compositor started) and most_w by most_h the largest a window
                may be, which is the largest screen. 0 is the space's own width and height: one screen.
        */
        int stride, most_w, most_h, room_h; // room_h: the rows a window's buffer holds
        unsigned int scanout_handle;        // the scanout's dumb buffer, given back when another takes its place
        unsigned long composing_bytes, mirror_bytes; // what compositor_changed_ready mapped, given back with it
        int space_wanted;   // the screens are to be taken again once the card has finished a flip
        int screens_gone;   // a take found no screen connected where there had been some: the next that finds one sets every crtc anew
        int retrain;        // the machine woke: the next take reads every connector again and sets every crtc anew, as for screens_gone
        positive screens_look; // while they are gone, when the connectors are next asked without a word from the kernel
        int kept_x, kept_y; // where the pointer was last kept on a screen, which a movement starts from
        int absolute;       // the pointer was put by a tablet, whose point comes from no screen
        int damage[COMPOSITOR_DAMAGE][4]; // x1, y1, x2, y2: what changed, apart
        unsigned int damages;

        int signals;
        int inputs[COMPOSITOR_INPUTS];
        int absolute_x[COMPOSITOR_INPUTS][2], absolute_y[COMPOSITOR_INPUTS][2];
        unsigned int input_count;

        int watch;           // inotify on /dev/input, -1 when there is none
        int probe_owed;      // the first take read the kernel's lists without a look (compositor_space_first): the loop looks, once
        int uevents;         // the kernel's uevents (compositor_uevents_read), -1 when there is no socket for them
        unsigned long devices[COMPOSITOR_DEVICES / 64]; // the evdev numbers that are open, a bit each
        int device_number[COMPOSITOR_INPUTS];

        int scale;           // device pixels a drawn one is
        int boot_scale;      // the percent the program was started at (-s, else WINDOWS_SCALE_DEFAULT): what a screen with no displays line is drawn at

        /*
                The windows: the table every program maps (window.c), each row's door and wake, and the
                compositor's own columns beside it -- what it took from each row once, checked, and what
                it decided. A row is a window everywhere: the stack, the keys, the hand are rows.
        */
        struct windows *table;
        unsigned long pixels, buffer; // where the buffers are, one buffer: never the table's numbers
        /*
                A window shown straight from its buffer, with no copy: the table is then a memfd, given to
                /dev/spark by table_giver (a descriptor of the device, plus one) and linked at WINDOWS_TABLE (its
                descriptor plus one; 0 when the table is a plain file), /dev/udmabuf is open
                (plus one), and each buffer asked for is a framebuffer (scan_fb, its card handle scan_handle).
                on_screen is the framebuffer the card shows: fb, the compositor's own, or a window's, whose row
                is direct_row (-1 for none). A flip in flight (flip_pending, to
                flip_row's flip_buffer) holds back what a window is told was taken (held: a frame, plus one) until
                the card has let go of the buffer it showed before. The tabs written over a window's buffer are
                kept there with what they covered (under, a buffer's worth of the band each, patched when it is).
        */
        int table_memfd, table_giver, udmabuf, direct_row, direct_refused, flip_pending, flip_row;
        positive flip_when;  // the last flip-complete event's time (its vblank, on the monotonic clock), and its sequence
        positive flip_issued; // when the flip in flight was asked of the card (the monotonic clock)
        positive flip_serial; // the number the flip in flight was asked with (its event's user data): an event with another is a flip given up on
        unsigned int flip_seq;
        int flip_stamped;    // whether flip_when came with the event (a card that stamps its vblanks)
        /*
                The tabs over a window shown straight, on an overlay plane where the card has one
                (compositor_overlay_ready: overlay_state 0 not yet asked, 1 found, -1 none or refused): the
                primary plane and the overlay with their property ids, two buffers of the card's the band is
                composed into (overlay_back the one drawn next, overlay_rows how many rows each holds), and
                overlay_on with overlay_rect, what the card shows of it. crtc_index is the crtc's place in the
                card's list, which a plane's possible crtcs are a mask of.
        */
        int overlay_state, overlay_on, overlay_rect[4];
        // A window on the overlay where it does not fill the output (plane_row, -1 for none): what of it the overlay
        // shows on the output (plane_rect), and the framebuffer it shows (plane_fb, 0 for none).
        int plane_row, plane_rect[4];
        unsigned int plane_fb;
        unsigned int crtc_index, primary, overlay, primary_props[11], overlay_props[11];
        unsigned int overlay_fb[2], overlay_handle[2], overlay_back, overlay_rows;
        unsigned int *overlay_pixels[2];
        unsigned long overlay_pitch, overlay_bytes; // pitch in words
        unsigned int (*scan_fb)[2], (*scan_handle)[2], on_screen, flip_buffer;
        unsigned int *taken_held, (*patched)[2];
        struct compositor_rows held_rows, dropping, reclaim;
        int screen_stale; // the compositor's own screen holds nothing current: drawn whole before it is shown again
        unsigned int memfd_failed; // the step the table's memfd was refused at, said in the table's refused
        unsigned int *under[2];
        int under_rect[2][4];
        int *door, *wake;
        struct compositor_rows woken;  // the rows to knock on at the end of the pass, a bit each
        struct compositor_rows fit;    // the rows whose next picture of another size is asked to fit its screen (a scale was taken), a bit each
        positive fit_until;  // and until when (compositor_scale_fit)
        unsigned int *seen;     // the frame last taken
        // A window's solid runs (compositor_runs_note): its picture's, mapped when first read, the picture they are of (the tag of
        // its frame and buffer), how many presents composed that picture, and what became of the read: 0 not read, 1 read and used
        // by the compose, 2 read and not worth using.
        struct compositor_run_row **runs;
        unsigned long *runs_bytes;
        unsigned int *runs_tag, *runs_composed;
        unsigned char *runs_state;
        unsigned int *showing;  // it has shown a picture: it is on the screen and has a tab
        unsigned char *landing; // its first show is landing: drawn smaller, growing to its size (compositor_land)
        positive *land_at;      // when its landing began, at the first present that drew it; 0 until then
        // A window arranged or floated glides from the rectangle it had (compositor_glide_start): drawn between that and where
        // it is, from glide_at (0 until its first present), while glide[] is 1.
        unsigned char *glide;
        int (*glide_from)[4];
        positive *glide_at;
        // Where the window was put when its glide began: a window moved since (a hand, a placement) takes where it glides
        // from along with it. And the window the hand holds that glides all the same, plus one (0 for none): a tile torn
        // off, shrinking to its own size about the hand (compositor_float_for_drag).
        int (*glide_base)[2];
        int drag_glide;
        unsigned int *made;     // the order the windows first showed in, which their tabs are in
        int *x, *y;
        unsigned int *w, *h, *front, *state; // w and h as it is shown
        // How many times the row's window has been asked to go since it showed (compositor_close), never 0 once it has been: what
        // carries the ask to a program is the count it has answered, not WINDOW_CLOSING, which stays. The table's close_asks is this
        // count said to the row's program (compositor_close_count is the one writer of both); the X window manager and the Wayland
        // shim read this one, which no program can write.
        unsigned char *close_asks;
        unsigned int *picture_w, *picture_h; // as its program drew it
        unsigned int *asking;   // shown at a size asked of it, until a picture of that size comes
        unsigned int *arranged; // COMPOSITOR_FLOATING, or what it fills: all, the left half, the right
        unsigned char *fullscreen; // a window that asked to fill its screen under the taskbar too (fullscreen): a client's request, or a placement's
        unsigned char *fullscreen_was; // what a fullscreen window filled before (arranged), which leaving fullscreen puts back
        unsigned int *edges;    // the edges the hand holds, until the window is the size asked for
        int *far_x, *far_y; // where the edges it does not hold are
        int *saved_x, *saved_y; // where it was floating, and how large
        unsigned int *saved_w, *saved_h;
        char (*title)[WINDOWS_TITLE];
        // Who the window is, the compositor's own copy of the row's identity (compositor_identity_take), which its tab is drawn from:
        // its app_id, and its icon with the sequence that picture is (0: none).
        char (*app_id)[WINDOWS_APP_ID];
        unsigned int *icon_seq;
        unsigned int (*icon)[WINDOWS_ICON * WINDOWS_ICON];
        char (*name)[WINDOWS_NAME]; // what it is remembered by, taken with its first picture; none for nothing
        unsigned int *moved;      // the row's moved, as it was last taken
        unsigned int *stack;    // bottom first
        unsigned int stacked, next_made;

        // The tabs: the rows that have one in their order, what each shows, and the band drawn with the font.
        unsigned int *listed;
        unsigned char *marks, *lengths;
        unsigned int listing;
        unsigned int *band;  // the band's pixels, or none: no font, and the tabs are shapes
        /*
                `taskbar display all`, a band on every screen listing that screen's windows: band_at is the band
                being drawn or asked of (screen band_at's; 0, the primary's, is band above), each other band's
                pixels are its own, and each band's marks are kept as they were listed for its frames.
        */
        int band_at;
        unsigned int *band_more[WINDOWS_OUTPUTS];
        unsigned long band_more_bytes[WINDOWS_OUTPUTS];
        unsigned char (*band_marks)[WINDOWS]; // WINDOWS_OUTPUTS of them
        unsigned char *banded; // with a band on every screen, the screen each window's tab was last drawn on, + 1
        unsigned long band_bytes; // how much memory the band has
        unsigned int *glass; // what is under the tabs, blurred: as wide as the band and its margins, as high
        unsigned long glass_bytes;

        /*
                The windows that are glass (compositor_glass_pass): what is under each, blurred and not yet tinted,
                and its picture over that, each cut at the window's own size; the size and place the backdrop was
                made for (a width of 0: never); what of it is to be blurred again and what composed again,
                x1, y1, x2, y2 on the output and empty when x2 is not past x1; which side of lightness its tint
                goes to, and the ground it told its program. The scratch the blur is made in is shared.
        */
        unsigned int *glassy;
        unsigned int *asks; // what the program asked of its window besides glass (WINDOW_NO_CLOSE, WINDOW_DISMISS, WINDOW_NO_TAB, WINDOW_NO_GRAB)
        unsigned int **backdrop, **tinted, **composed;
        unsigned int **boosted; // compositor_vivid of each backdrop cell, as the cell last was
        unsigned long (*glass_sum)[3]; // per channel, the sums of boosted over the blocks: the side's mean
        int (*glass_blocks)[4]; // the first block kept of the small picture under it, across and down, and how many each way
        unsigned long *glass_cut;
        int (*glass_made)[4], (*stale)[4], (*dirty)[4];
        unsigned int *glass_side, *glass_told, *lifted;
        // The theme (`moonwater canvas theme`, its accent and motion): the word kept (0 light, 1 dark, 2 auto), and the
        // side every glass window is tinted to, 1 light and 0 dark, which the theme chooses and what is under it does not.
        unsigned int theme_word, theme_side;
        unsigned int *blur, *frost, *rise; // scratch: the tabs' small picture, a band of the scene halved, the levels risen
        unsigned long blur_bytes, frost_bytes, rise_bytes;
        positive blurred, recomposed; // pixels blurred again and composed again, all frames
        int damage_by; // what is being damaged: 0 the scene, a row and one its own picture, COMPOSITOR_OVER what lies over all
        font_face face;
        font_face bold; // the face's bold, the taskbar clock's (compositor_font), when it loaded
        int bold_ok;
        font_engine *engine;

        int focus, hover, grab, drag, resize, press; // rows, or COMPOSITOR_NONE
        struct ui_click press_click; // the presses on a window's top: a double press is the kit's click count
        struct taskbar_cells cells;  // the panel's cells: what a press on one does (taskbar_cell_press), and the tray's double press
        unsigned int settings_asked; // the times the script was asked for the settings (compositor_settings_open)
        unsigned int launcher_asked; // the times the script was asked for the launcher by its button (compositor_launcher_open)
        unsigned char logo_alone;    // a logo key is down and nothing else has been pressed since: its release is a press on the launcher's tab (compositor_logo_tap)
        int drag_x, drag_y;
        int drag_moved; // the window being moved has moved: only then is where it is let go an arrangement
        // The floating rectangle the window being moved is remembered by, taken at the press: its own if it floated, else the
        // one it had before it was first arranged. A drop on a target keeps it, so a restore puts back where the window was
        // before the drag, not where it was let go against the edge, and snaps in a row keep the first one.
        int drag_keep[4];
        // What a drag or a resize took hold of, for Escape or a right press to put back (compositor_hold_cancel): the rows, a
        // bit each (a divider holds all of its windows), and each one's rectangle and what it filled at the press.
        struct compositor_rows hold_rows;
        int (*hold_rect)[4];
        unsigned int *hold_how;
        // The snap assist (compositor_assist_refresh): up over a drag's target, or armed after its drop until a click or Escape;
        // the row snapped, the region it took and its screen; the thumbnails, each its row, its tile and its rectangle.
        int assist_on, assist_armed, assist_row, assist_screen, assist_count;
        unsigned int assist_how, assist_rows[COMPOSITOR_ASSIST_MAX], assist_zone[COMPOSITOR_ASSIST_MAX];
        int assist_rect[COMPOSITOR_ASSIST_MAX][4];
        // The top edge's grid (compositor_overlays_drag): up on a screen, and the tile the pointer is over (compositor_grid_under); when a drag's pointer
        // came to rest in the top band of screen grid_since_screen, which brings it up after COMPOSITOR_GRID_DWELL_NS (0: not there).
        int grid_on, grid_screen, grid_since_screen;
        unsigned int grid_hover;
        positive grid_since;
        // The overlays (the assist and the grid) are up when overlays_up; they fade in and out (compositor_overlay_fade) from
        // overlay_from toward overlay_goal percent since overlay_at, and are drawn while they are up or fading out.
        unsigned int overlays_up, overlay_from, overlay_goal;
        positive overlay_at;
        // The window being resized, how large it was and where the hand took hold (press_x, press_y: a drag's too).
        // button_x, button_y keep where the left button went down inside a window, before a client asks to move.
        int resize_w, resize_h, press_x, press_y, button_x, button_y;
        // A shared edge's resize (compositor_shared_begin): the window on its other side (COMPOSITOR_NONE for a resize of
        // one), the low side's and the high side's (left or top), and whether the edge runs down; the size each had is shared_sizes'.
        int shared_row, shared_lo, shared_hi, shared_vertical;
        // All the windows of a divider's resize (compositor_divider), a bit each, the low side's and the high side's, and the
        // size each had across the line at the press.
        struct compositor_rows shared_lo_set, shared_hi_set;
        int *shared_sizes;
        // The shared edge's indicator (compositor_mark_set): the rectangle, the window it is drawn over and the other, and all
        // the windows of its divider, a bit each.
        int mark_row, mark_with, mark[4];
        struct compositor_rows mark_rows;
        int pointer_x, pointer_y;
        struct compositor_keymap keymap;
        // The keys whose press went to keys_to, one bit a code: their repeats and
        // their release follow the press, and a focus change ends them (see compositor_keys_release).
        unsigned char keys_down[32];
        int keys_to;
        struct
        {
                unsigned int handle[2]; // the plane's two buffers, 0 when it has none
                int back;               // the one that is not showing
                int plane_on;           // the plane shows a picture, and
                struct compositor_pointer plane, drawn_as; // the one it wears; and the one the pointer drawn into the scanout is of
                int soft;               // the pointer is drawn into the scanout
                int drawn, x, y;        // where the software pointer was last damaged
                int client_hidden;   // a window's client asked for no pointer (wl_pointer.set_cursor with no surface), over that window
                int hold_hidden;     // a client holds the pointer locked (zwp_locked_pointer_v1)
                unsigned int shape;     // what the pointer wears now
                int asked_row;          // the window whose shape the pointer took at the last motion (COMPOSITOR_NONE for none), and the shape it had
                int asked_seen;
                int magnified;          // it is shaken: found
                positive magnified_until;
                int magnify;            // the hand shook it since the last motion
                int shake_off;          // `moonwater canvas cursor shake off`: a shake finds nothing
                int click_off;          // `moonwater canvas cursor click off`: a held button leaves the pointer its size (on by default)
                positive shake_ns;      // how long a shaken pointer stays large (0 is COMPOSITOR_MAGNIFIED_NS)
                int size, look;         // `moonwater canvas cursor`: its size in percent of the desktop's, 0 for 100; 1 light, 2 dark, 0 for light too
                unsigned long paints, moves, refused; // on the plane: shapes given it, moves, and times it refused
                int style;              // `moonwater canvas cursor style`: 1 dynamic (the default), 0 classic
                int dyn;                // the dynamic cursor is what is drawn now: the style, or a touch was the last input (touch.hidden)
                int squeeze;            // `moonwater canvas cursor squeeze`: the percent a pointer shrinks by while a button is held (compositor_pointer_press)
                unsigned int held;      // the buttons held down, a bit each from BTN_LEFT
                // In 256ths: what a held button adds to the pointer's size, how far the dynamic shape is the text's rectangle, and how far it is stretched.
                struct compositor_sway press, morph, stretch;
                int ux, uy;             // the unit direction of the motion that stretches it, times 64
                int last_x, last_y;     // where the pointer was when its speed was last taken, and when
                positive last_ns, idle_until; // and when the stretch lets go if nothing moves
        } cursor;
        struct compositor_shake shake;
        char driver[16];            // the card's driver, which says whether it is a real card
        struct compositor_mode mode; // the mode the crtc is on
        unsigned int connector;
        positive asleep;            // the clock that runs through a sleep less the one that does not, when last looked at
        positive wake_at;           // when the next of the redraws after a wake is due
        int wake_stage;             // 0, or the take of the screens after a wake that is next: 1 at half a second, 2 at two and a half
        unsigned int wakes;         // the wakes heard since the start (compositor_woke)
        positive motion_ts;         // when the hand's oldest movement not yet shown was reported
        int hold;                   // /dev/cpu_dma_latency, open with a zero in it while a person is at the machine, or -1
        positive hold_until;
        unsigned long holds;        // how many times it was taken
        int game;                   // /dev/cpu_dma_latency, open while a fullscreen window is on top (compositor_game_watch), or -1
        unsigned long games;        // how many times it was taken; ~0 when the machine has no such file or the owner said no
        struct { positive total, count, worst; } latency[3]; // pointer to plane, pointer to frame, key to window
        unsigned char wakeonly[COMPOSITOR_INPUTS]; // a power button or a lid: it wakes the loop and says nothing it uses
        int cycle;           // an Alt-Tab is under way: the place in the stack of the window it is on, from 1
        int raw_x, raw_y;    // what the mouse has said since its last report
        int accel_x, accel_y; // the fraction of a pixel the gain has not yet paid out
        int tablet_x, tablet_y, tablet_have; // what a tablet has said since its last report
        positive stamp;      // when the last movement was
        unsigned char hires[COMPOSITOR_INPUTS]; // the device reports its wheel in 120ths: bit 0 the vertical one, bit 1 the sideways one
        positive wheel_at[COMPOSITOR_INPUTS];   // when the device's last wheel event came, ns (compositor_wheel_stream: a stream's gap)
        positive wheel_stream_at[COMPOSITOR_INPUTS]; // when its last stream notch came, ns: the memory's three seconds run from it
        unsigned int wheel_run[COMPOSITOR_INPUTS];   // the stream notches in its current gesture (compositor_wheel_stream)
        unsigned char wheel_streamed[COMPOSITOR_INPUTS]; // it has streamed: a gesture of COMPOSITOR_WHEEL_STREAMS stream notches
        long long wheel_rest[COMPOSITOR_INPUTS][2]; // what a stream's notches have not turned whole, per axis, in COMPOSITOR_GAIN_TURN units (compositor_stream_turn)
        positive stream_hist[COMPOSITOR_INPUTS][COMPOSITOR_STREAM_HIST]; // the times of the device's last stream notches, a ring
        unsigned int stream_hist_n[COMPOSITOR_INPUTS];                    // how many stream notches the gesture has had (the ring's count)
        unsigned int trackpad_speed; // the setting `canvas trackpad speed` as a percent: 0 is 100 (what a zeroed compositor is)
        unsigned char touchpad[COMPOSITOR_INPUTS]; // the device is a touchpad, read as compositor_pad_report says
        struct compositor_pad pad[COMPOSITOR_INPUTS];
        // The device is a touch screen (compositor_touch_report), and its fingers; what they are doing; and whether the
        // press, release or motion being handled is a finger's, which hit wider and are told so (WINDOW_KEY_TOUCH).
        unsigned char touchscreen[COMPOSITOR_INPUTS];
        struct compositor_touch_screen screen_touch[COMPOSITOR_INPUTS];
        struct compositor_touch touch;
        struct compositor_swipe swipe; // a finger the screen's edges may take (compositor_swipe_report)
        struct compositor_chip chip;   // what a swipe shows over the desktop (compositor_chip_*)
        int side_left, side_right;     // what a swipe in from the left or the right edge runs (COMPOSITOR_SIDE_*, from the settings Left edge and Right edge)
        // The prediction a frame ahead, per class (COMPOSITOR_PREDICT_*), and the classes `moonwater canvas predict` turned off.
        struct compositor_predict predict[COMPOSITOR_PREDICTS];
        unsigned char predict_off[COMPOSITOR_PREDICTS];
        // The on-screen keyboard (compositor_osk_sync): its setting (COMPOSITOR_OSK_*), whether the last input was a key of a
        // physical keyboard, whether it is shown, and how far each floating window was moved up for it (osk_pan), to be moved back.
        // A window that fills its screen is made as high as the room above the keyboard instead: its height (osk_height) and its
        // edges (osk_edges) are kept, to be given back.
        unsigned char osk_setting, osk_typed, osk_shown;
        // A screen turned (compositor_osk_turned): the keyboard is held away until the focus goes to another window, or a field of this
        // one is focused again after it lost its focus (osk_turn_blur: the focused window's field count was none).
        unsigned char osk_turned, osk_turn_blur;
        int osk_turn_focus;
        int *osk_pan, *osk_height;
        unsigned int *osk_edges;
        // A Wayland client's text field (wayland_host.c): the content purpose of its field, and the bottom of its text cursor in the
        // window's pixels (0 for none). The pan keeps the cursor above the keyboard, not the window's bottom edge.
        unsigned char *text_purpose;
        int *caret;
        // Where the hand is, before its lead (real_x, real_y), and the class that moved the pointer last (owner). The pointer
        // itself (pointer_x, pointer_y) is where that is led to, and every consumer of motion reads it; at_real is set while a
        // press is handled, so a press is at the real place.
        int real_x, real_y;
        int shown_x, shown_y; // the pointer as compositor_pointer_shown last set it: a pointer set anywhere else is the real place
        unsigned int owner;
        unsigned int at_real;
        int touching;
        int retracting; // a finger's or a pen's lead is being taken away (compositor_predict_clear): the windows are not told
        /*
                The marks of the fingers (`moonwater canvas touch on`, spots_on), one a slot of the touch screen being
                touched: where each is on the space, on 1 while its finger is down and 2 while it fades from lifted,
                its alpha, and what was damaged for it last (x, y, width; a width of 0 none).
        */
        int spots_on;
        unsigned int spots_shown, spots_fading; // a bit a slot: its mark is drawn, and it is fading
        struct
        {
                int x, y, on;
                positive lifted;
                unsigned int alpha;
                int drawn[3];
        } spot[UI_TOUCH_SLOTS];
        unsigned int *spot_scratch; // what a mark is composed over before it goes to a scanout that is only written
        // The window whose line is lit (lit_row, COMPOSITOR_NONE for none), what of it, and whether held (else hovered).
        int lit_row;
        unsigned int lit_edges, lit_held;
        unsigned int border; // the lit edges' width in logical pixels, 1 to 4 as the setting says (compositor_border_publish); 0 is the standard
        // The snap preview a drag shows (compositor_preview): the row whose drag it is, and its target's rectangle, from the
        // frame the target is touched. The edges of the window's screen its rectangle touched last frame (compositor_snap_rule's
        // dead zone).
        int preview_row;
        int preview[4];
        unsigned int snap_held;
        /*
                The preview as it is drawn (compositor_preview_shown), under the window preview_draw - 1 (0 for none) and
                shaped as preview_how fills: it moves from preview_from, at preview_from_alpha percent, to the target over
                UI_WINDOW_NS once the target changes, and with no target (lost, or let go on) it fades out where it was
                drawn; preview_at is when that began, 0 until the first present draws it (compositor_preview_keep).
        */
        int preview_draw, preview_from[4], preview_fading;
        unsigned int preview_how, preview_from_alpha;
        positive preview_at;
        // What moonwater debug is told of each device, kept here for the table that is made after they are opened.
        unsigned int input_kinds[COMPOSITOR_INPUTS];
        unsigned short input_id[COMPOSITOR_INPUTS][4];
        char input_name[COMPOSITOR_INPUTS][WINDOWS_INPUT_NAME];
        int told_row, told_x, told_y; // where the pointer was last told to a window: its row and the place in its picture
        unsigned char button_down;    // the buttons (BTN_LEFT up, a bit each) a window was sent the press of
        int button_row[8];            // and the window each press went to, which its release goes to
        unsigned char held[COMPOSITOR_INPUTS]; // the modifiers each keyboard holds down
        // `moonwater keyboard repeat` and `moonwater mouse`: what the settings kept, and what each keyboard has been told.
        unsigned char repeats[COMPOSITOR_INPUTS]; // the keyboard's repeat can be set: it answered EVIOCGREP
        int repeat_own[COMPOSITOR_INPUTS][2], repeat_now[COMPOSITOR_INPUTS][2]; // delay, period: as it was, as it is
        int repeat_kept, repeat_delay, repeat_rate; // bit 1 delay, bit 2 rate kept; the delay in ms, the rate a second (0 off)
        int sensitivity, flat; // percent of the gain (0 is 100); one to one, with no curve
        int grip;              // `moonwater canvas border grip`: logical pixels outside a window's edge a pointer's press resizes it by (0 is COMPOSITOR_GRIP)
        int layout_given; // -l named one, and the settings are not looked at
        unsigned long painted; // pixels composed, all frames
        positive period, presented; // a frame in nanoseconds, and when the last one went out
} compositor;

// The count of a row's asks to go, kept and said: the compositor's own (close_asks) and the table's, which the row's program reads.
static void compositor_close_count(int row, unsigned int asks)
{
        compositor.close_asks[row] = (unsigned char)asks;
        if (compositor.table)
                __atomic_store_n(&compositor.table->close_asks[row], (unsigned int)compositor.close_asks[row], __ATOMIC_RELEASE);
}

// The fingers' marks (the touch section, below): whether any is drawn or fading, drawing them, and their masks.
static int compositor_spots_any(int fading);
static void compositor_spots_draw(const struct compositor_target *t);
static void compositor_spots_marks(const struct compositor_target *t);
static void compositor_spots_compose(const struct compositor_target *t, const struct compositor_frame *frames,
                                     unsigned int count);
// The software pointer over a target only written, composed aside (the touch section's neighbour, compositor/input.c).
static void compositor_cursor_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count);
// What moonwater debug says of the pointer: on the plane, drawn, or drawn for the plane's probe (compositor/kms.c).
static unsigned int compositor_pointer_plane_word(void);
static void compositor_spots_ready(void);

// A colour of the pointer: its fill is white and its outline black, or the other way for the dark one.
// The plane's pixels have an alpha, the scanout's none.
static unsigned int compositor_cursor_ink(int outline, unsigned int alpha)
{
        return ((compositor.cursor.look == 2) == outline ? 0xffffffu : 0u) | alpha;
}

// Whether the pointer is drawn into the scanout: when the card has no plane for it, or the plane is too
// small for the size it was asked to be.
static int compositor_turning(void);

/*
        The pointer's whole size, in 256ths of a pixel a cell: the desktop's own size for it (the whole device pixels of a logical pixel,
        compositor_px1) times the percent `moonwater canvas cursor size` gives, to the nearest 256th, and three times that while it is
        shaken large. At a desktop of 100 percent, 80 is 204.8 256ths and so 205, and at 200 percent it is 409.6 and so 410.
*/
static int compositor_cursor_size(void)
{
        int percent = compositor.cursor.size ? compositor.cursor.size : 100;
        int base = (compositor_px1(compositor.scale) * 256 * percent + 50) / 100;

        return compositor.cursor.magnified ? 3 * base : base;
}

static int compositor_cursor_in_scene(void)
{
        // A screen turned (by the card or by Canvas) has no plane that turns with it: its pointer is drawn into the scene. So is one
        // larger than two pixels a cell, shaken or not, which is the most the plane holds of the dynamic shape with its stretch. A
        // client's picture is never larger than the plane (compositor_picture_take), whatever size the pointer is set to.
        return compositor.cursor.soft || compositor_turning() ||
               (compositor.cursor.handle[0] && !compositor_picture.worn && compositor_cursor_size() > CP_PLANE / CP_DYN_CELL * 256);
}

// The screens as rectangles of the space, and how many: one, the whole space, where none is listed.
static const int (*compositor_screens(unsigned int *count))[4]
{
        static int whole[1][4];

        if (compositor.outputs)
        {
                *count = compositor.outputs;
                return (const int (*)[4])compositor.screen;
        }
        whole[0][0] = whole[0][1] = 0;
        whole[0][2] = compositor.width;
        whole[0][3] = compositor.height;
        *count = 1;
        return (const int (*)[4])whole;
}

// The pixels a row of every window's buffer is, and the largest a window may be (compositor.stride, most_w and most_h).
static int compositor_stride(void)
{
        return compositor.stride ? compositor.stride : compositor.width;
}

static int compositor_most_width(void)
{
        return compositor.most_w ? compositor.most_w : compositor.width;
}

static int compositor_most_height(void)
{
        return compositor.most_h ? compositor.most_h : compositor.height;
}

/*
        The bytes of one window's buffer, for the session: rows stride pixels long, as many as the tallest screen
        connected at the start, lit or dark (room_h). A screen dark at the start (arranged off, or past what the card
        could scan out) is lit later by an arrangement, and a window may then be as tall as it is.
*/
static unsigned long compositor_buffer_room(void)
{
        int rows = compositor.room_h > compositor_most_height() ? compositor.room_h : compositor_most_height();

        return ((unsigned long)compositor_stride() * (unsigned long)rows * 4 + 4095) & ~4095ul;
}

// How many screens the desktop spans: at least one.
static int compositor_output_count(void)
{
        return compositor.outputs ? (int)compositor.outputs : 1;
}

// Screen index's rectangle of the space, as the table lists it (0 the primary); an index past the list is the primary's.
static void compositor_output_rect(int index, int *x, int *y, int *width, int *height)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        index = index >= 0 && (unsigned int)index < count ? index : 0;
        *x = screens[index][0];
        *y = screens[index][1];
        *width = screens[index][2];
        *height = screens[index][3];
}

// Screen index's connector name; "" for a screen that is not a connector (headless) or past the list.
static const char *compositor_output_name(int index)
{
        return compositor.outputs && index >= 0 && (unsigned int)index < compositor.outputs ? compositor.output[index].name
                                                                                            : "";
}

static long compositor_call(positive number, positive a, positive b, positive c,
                            positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

/*
        The thread that calls it back to ordinary scheduling, at nice: the loop is real time (compositor_main) and a thread takes the policy
        of the one that made it, so a thread no frame waits for (the wallpaper's, a clipboard transfer's) says so, or it would take a core from
        a game's threads ahead of every ordinary one. Not allowed, it changes nothing.
*/
static void compositor_ordinary(int nice)
{
        int none = 0;

        compositor_call(syscall(sched_setscheduler), 0, 0, (positive)&none, 0, 0, 0); // pid 0: this thread; SCHED_OTHER
        if (nice)
                compositor_call(syscall(setpriority), 0, (positive)compositor_call(syscall(gettid), 0, 0, 0, 0, 0, 0), (positive)nice, 0, 0, 0);
}

// Zeroed pages of their own, or null.
static void *compositor_map_zeroed(unsigned long bytes)
{
        long got = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

        return got < 0 && got > -4096 ? 0 : (void *)got;
}

// The nine parts that read the state, in the order their uses need (the comment before the first run says why).
#include "compositor/damage.c" // what changed: rectangles and damage, landings and glides, the snap preview's state, a row's pixels and runs, the frames and the bands
#include "compositor/glass.c" // the glass under the panel and the tabs: the grain, the frost, the blur and its scene
#include "compositor/scanout.c" // what the card scans out: a window straight from its buffer, the record, the flip and its time, the planes, a turned screen
#include "compositor/gpu_host.c" // the card's part of a present: the render node's state, the scene and the window lists drawn on it, its hangs and its wake
#include "compositor/present.c" // a present: the metrics, the loop's tick and the frame composed and shown
#include "compositor/kms.c" // the card: the displays and their modes, the scanouts and plates, the space taken, the card found and opened, the cursor plane
#include "compositor/session.c" // the session: a machine that slept, the settings read and published, the input devices found, the doors and their threads
#include "compositor/manage.c" // the windows: the stack and the keys, grips and dividers, tiles and the grid, places, a row taken and let go, the keyboard on the screen, the clipboard, popups
#include "compositor/input.c" // what a person does: the keys and the chords, the volume, the hand's motion and buttons, the prediction, the wheel and the pad, the marks and touch

/*
        The table as a memfd, WINDOWS_TABLE a link to it in this process's
        /proc: what udmabuf makes a window's buffer a dma-buf of, so the card
        can show the window straight from where its program drew it. A program
        opens and maps it as it did the file, at the same path, which reopens
        the memfd (a memfd cannot be bound over a path: it lives on the
        kernel's own mount, which no namespace has). Opening it through /proc
        takes the right to look at this process, which root has; any program
        takes it from /dev/spark instead (WINDOWS_IOCTL_TABLE_GET), which is
        given it here, and the link is for where the device is not. It cannot
        shrink or grow (udmabuf's condition). The descriptor, or an error, and then the table is the
        plain file and every frame is copied (a headless compositor).
*/
static long compositor_table_memfd(unsigned long bytes)
{
        p8 path[48] = "/proc/";
        long handle = compositor_call(syscall(memfd_create), (positive) "moonwater-windows", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0);
        positive at = 6;

        if (handle < 0)
        {
                compositor.memfd_failed = 6;
                return handle;
        }
        // /proc/PID/fd/N, this process's own number and not self, which is whoever opens it.
        at += positive_into_string(path + at, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0));
        memory_copy(path + at, "/fd/", 4);
        at += 4;
        path[at + positive_into_string(path + at, (positive)handle)] = 0;
        // The seals: F_SEAL_SEAL, F_SEAL_SHRINK and F_SEAL_GROW.
        compositor.memfd_failed = compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0 ? 8
                                  : compositor_call(syscall(fcntl), (positive)handle, WINDOWS_F_ADD_SEALS, 1 | 2 | 4, 0, 0, 0) < 0 ? 9
                                  : compositor_call(syscall(fchmod), (positive)handle, WINDOWS_MODE, 0, 0, 0, 0) < 0 ? 10
                                  : compositor_call(syscall(symlinkat), (positive)path, (positive)AT_FDCWD,
                                                    (positive)WINDOWS_TABLE, 0, 0, 0) < 0
                                      ? 11
                                      : 0;
        if (compositor.memfd_failed)
        {
                system_close(handle);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);
                return -COMPOSITOR_EINVAL;
        }
        return handle;
}

/*
        What a screen's displays line asks for, and what the compositor did not do of it, for the table (WINDOWS_REFUSED_*):
        the rate asked, when the mode it is at is not that rate; the turn asked, when it is not turned; and a place whose x or
        y the file does not read. A screen with no line asks for nothing and is refused nothing.
*/
static void compositor_table_refusals(struct windows *table, unsigned int at, const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name, windows_name_length(name));
        unsigned int refused = 0;

        table->output_asked_hz[at] = display ? display->refresh : 0;
        table->output_asked_turn[at] = display ? display->rotation : 0;
        table->output_asked_scale[at] = display ? display->scale : WINDOWS_SCALE_DEFAULT;
        if (display && display->refresh && compositor.output[at].mode.vrefresh != display->refresh)
                refused |= WINDOWS_REFUSED_RATE;
        if (display && display->rotation && !compositor.output[at].turned)
                refused |= WINDOWS_REFUSED_TURN;
        if (display && (display->bad & (WINDOWS_DISPLAY_X | WINDOWS_DISPLAY_Y)))
                refused |= WINDOWS_REFUSED_PLACE;
        table->output_refused[at] = refused;
}

/*
        What the table says of the screens, at the start and whenever they change: the list, the primary first, each
        one's rectangle and connector name, and that it changed (layout).
*/
static void compositor_table_outputs(void)
{
        struct windows *table = compositor.table;
        int count = compositor_output_count();

        if (!table)
                return;
        for (int at = 0; at < count && at < WINDOWS_OUTPUTS; at++)
        {
                int x, y, width, height;
                const char *name = compositor_output_name(at);
                unsigned int length = 0;

                compositor_output_rect(at, &x, &y, &width, &height);
                table->output_x[at] = x;
                table->output_y[at] = y;
                table->output_w[at] = (unsigned int)width;
                table->output_h[at] = (unsigned int)height;
                while (name[length] && length < WINDOWS_OUTPUT_NAME - 1)
                        table->output_name[at][length] = name[length], length++;
                memory_zero(table->output_name[at] + length, WINDOWS_OUTPUT_NAME - length);
                memory_copy(table->output_model[at], compositor.output[at].model, WINDOWS_OUTPUT_MODEL);
                table->output_now[at] = compositor.output[at].mode.vrefresh;
                table->output_rates[at] = compositor.output[at].hz_count;
                memory_copy(table->output_hz[at], compositor.output[at].hz, sizeof(table->output_hz[at]));
                table->output_turn[at] = compositor.output[at].turn;
                table->output_software[at] = compositor.output[at].turn_software;
                compositor_table_refusals(table, at, name);
        }
        // A frame follows the screens too: the fastest one's period, which every kit window paces its frames by (ui_app_run, ui_kit_frame_ns).
        // Set once at the table's making, a take at another rate (a wake's, a hotplug's, the settings') left the windows at the old one.
        table->period = (unsigned int)compositor.period;
        // The largest a window may be follows the screens, inside the room its buffer has for the session.
        table->output_width = (unsigned int)compositor_most_width();
        table->output_height = (unsigned int)compositor_most_height();
        table->layout = compositor.layout;
        __atomic_store_n(&table->outputs, (unsigned int)count, __ATOMIC_RELEASE);
}

/*
        The window lists' mode, the only writer of the table's list_mode: on makes the compositor render the lists its windows publish, off
        makes every window paint its own frames. themed counts up, as a theme change does, and each window that has a program is woken,
        so each repaints in the mode it is in now. compositor_gpu_lists_sync calls it (on while the lists go to the card, whose renderer is
        compositor_gpu_list_render), and the table's making with 0.
*/
static void compositor_lists_set(int on)
{
        struct windows *table = compositor.table;

        if (!table)
                return;
        __atomic_store_n(&table->list_mode, on ? 1u : 0u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (table->pid[row])
                        rows_add(&compositor.woken, row);
}

/*
        The table and its doors, made new: whatever is at the paths is from a
        compositor whose session is over. The table is sparse, so its size is
        the most every window could draw and what it costs is the pages that
        are drawn. The table is the compositor's memfd and the doors are
        root's (WINDOWS_DOOR_MODE): a window is a program a desktop session
        started, which is root, and a door the whole machine could write is a
        door the whole machine could hold open against the row it names.
*/
static long compositor_door_make(unsigned int row);

/*
        The address space the table may take, of every process that maps it: a quarter of the 47 bits a mapping is given below the
        kernel's (a machine with five levels of tables gives no more without being asked). The rows the table has room for are as many
        as fit in it, every one with its two buffers and its list, WINDOWS at the most: 1,048,576 rows at 1920 by 1080, 491,520 at 3840
        by 2160, as many as the descriptors and the processes a kernel has.
*/
#define COMPOSITOR_TABLE_MOST (1ul << 45)

// The rows a table of buffers of `buffer` bytes has room for.
static unsigned int compositor_table_room(unsigned long buffer)
{
        unsigned long per = 2 * buffer + WINDOW_LIST_BYTES, rows = (COMPOSITOR_TABLE_MOST - WINDOWS_HEADER) / per;

        rows = rows < WINDOWS ? rows : WINDOWS;
        return rows > COMPOSITOR_ROWS_FIRST ? (unsigned int)rows : COMPOSITOR_ROWS_FIRST;
}

static int compositor_table(void)
{
        // Every window's buffer as large as the largest window may ever be, a row of it stride pixels: for the session,
        // as the table is sealed at its size and mapped by every window. They start at WINDOWS_HEADER (window.c).
        unsigned long buffer = compositor_buffer_room();
        unsigned int room = compositor_table_room(buffer);
        // And a window list for every row after the pictures (window.c's WINDOW_LIST_BYTES): sparse, so only what a program writes is
        // there, and so are the rows' pictures (a memfd's pages are made where they are written).
        unsigned long bytes = WINDOWS_LISTS_AT(buffer, room) + WINDOW_LIST_BYTES * (unsigned long)room;
        long handle, mapped;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run", 0755, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);

        // A memfd for a headless compositor too: sealed against shrinking, so nothing that writes the table truncates it under
        // the compositor's mapping (a plain file truncated is SIGBUS at the compositor's next read of it). The plain file is
        // what is left when there is no memfd; what the card does with a memfd (udmabuf, the device's table) asks for a card.
        handle = compositor_table_memfd(bytes);
        compositor.table_memfd = handle >= 0 ? (int)handle + 1 : 0;
        // The device hands programs the table of the compositor that has the card, and no headless one's.
        if (handle >= 0 && compositor.card >= 0)
        {
                long spark = (long)system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);

                if (spark >= 0 && system_control(spark, WINDOWS_IOCTL_TABLE_SET, handle) == 0)
                        compositor.table_giver = (int)spark + 1;
                else if (spark >= 0)
                        system_close(spark);
        }
        if (handle < 0)
        {
                handle = (long)system_open_at_mode(AT_FDCWD, (string_address)WINDOWS_TABLE,
                                                   FILE_READ_WRITE | O_CREAT | O_EXCL | O_CLOEXEC, WINDOWS_MODE);
                if (handle < 0)
                        return (int)handle;
                if (compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0)
                {
                        system_close(handle);
                        return -COMPOSITOR_ENOSPC;
                }
        }
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 1, (positive)handle, 0);
        // The memfd is kept: udmabuf is given it, buffer by buffer.
        if (!compositor.table_memfd)
                system_close(handle);
        if (mapped < 0 && mapped > -4096)
                return (int)mapped;
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, WINDOWS_MODE, 0, 0, 0);

        compositor.table = (struct windows *)mapped;
        compositor.pixels = WINDOWS_HEADER;
        compositor.buffer = buffer;
        compositor.table->output_width = (unsigned int)compositor_most_width();
        compositor.table->output_height = (unsigned int)compositor_most_height();
        compositor.table->stride = (unsigned int)compositor_stride();
        compositor.table->scale = (unsigned int)compositor.scale; // the kit draws in percent of the logical size
        compositor.table->period = (unsigned int)compositor.period;
        compositor.table->focus = WINDOWS;
        compositor.table->refused = compositor.memfd_failed;
        compositor.table->pixels = WINDOWS_HEADER;
        compositor.table->buffer = buffer;
        compositor.table->lists = WINDOWS_LISTS_AT(buffer, room);
        compositor.table->capacity = room;
        compositor_rows_room = room;
        compositor.table->list_bytes = WINDOW_LIST_BYTES;
        // The layout's version first for a program to read, and the rows a program may take (every one has its doors below).
        __atomic_store_n(&compositor.table->abi, WINDOWS_ABI, __ATOMIC_RELEASE);
        __atomic_store_n(&compositor.table->rows, compositor_rows_in_use, __ATOMIC_RELEASE);
        compositor_lists_set(0);
        compositor_table_outputs();

        // The first rows' doors (compositor_door_make).
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if ((handle = compositor_door_make(row)) < 0)
                        return (int)handle;
        if (compositor_clip_start() < 0)
                return -1;

        return 0;
}

// The table and the doors gone with the session: a program that looks finds no compositor.
static void compositor_table_drop(void)
{
        p8 name[48];

        if (compositor.table_giver)
        {
                system_control(compositor.table_giver - 1, WINDOWS_IOCTL_TABLE_SET, -1l);
                system_close(compositor.table_giver - 1);
                compositor.table_giver = 0;
        }
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0, 0, 0, 0);
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                for (int wake = 0; wake < 2; wake++)
                {
                        windows_door_name((char *)name, row, wake);
                        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
                }
}

/*
        SIGPIPE ignored: a write to a FIFO or a socket whose reader has gone is EPIPE and no end of the compositor. keys.fire is
        opened only while the machine script reads it, and the script may close it between the open and the write; the sound
        server's socket may go. The Wayland shim ignored it when it started, so a build without the shim died of it.
*/
static void compositor_pipe_ignore(void)
{
        system_signal_install(13, 1, 0, 0, null); // SIGPIPE, SIG_IGN
}

/*
        SIGTERM, SIGINT and SIGHUP, which end the compositor, SIGUSR1, which has it read /root again
        (compositor_root_again), and SIGUSR2, the sleep verb's word that the machine woke (compositor_woke), as a
        descriptor the loop polls; SIGPIPE ignored.
*/
#define COMPOSITOR_SIGUSR1 10
#define COMPOSITOR_SIGUSR2 12

static int compositor_signals(void)
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1) | 1ul << (COMPOSITOR_SIGUSR1 - 1) | 1ul << (COMPOSITOR_SIGUSR2 - 1);

        compositor_pipe_ignore();

        compositor_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        return (int)compositor_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8,
                                    COMPOSITOR_NONBLOCK | COMPOSITOR_CLOEXEC, 0, 0);
}

// The signal the descriptor has, taken off it: its number, or 0 when there is none to read (a signalfd_siginfo is 128 bytes, the number first).
static unsigned int compositor_signal_taken(void)
{
        unsigned int said[32];

        return compositor_call(syscall(read), (positive)compositor.signals, (positive)said, sizeof(said), 0, 0, 0) == (long)sizeof(said) ? said[0] : 0;
}

static void compositor_say(string_address what, bipolar value)
{
        string_format(log, "compositor: %s %b\n", what, value);
        log_flush();
}

static void compositor_screens_say(void);

/*
        With no card the one screen is named Canvas (what the shim says it is called) and its displays line can ask for a
        scale, which is taken as a card's screen takes one: the desktop's scale, the table's, the bands and every pixel. A
        screen with no line is drawn at the scale the program was started with (-s).
*/
#define COMPOSITOR_HEADLESS_NAME "Canvas"

static void compositor_headless_scale(void)
{
        const struct windows_display *display;
        int scale;

        compositor_displays_read(0);
        display = windows_display_of(compositor_display, compositor_displays, COMPOSITOR_HEADLESS_NAME,
                                     windows_name_length(COMPOSITOR_HEADLESS_NAME));
        scale = display && display->scale ? (int)display->scale : compositor.boot_scale ? compositor.boot_scale : WINDOWS_SCALE_DEFAULT;
        if (scale != compositor.scale)
                compositor_scale_set(scale);
}

// The screens taken again (a screen came or went, the arrangement changed), and said in the log when they changed.
static void compositor_space_retake(void)
{
        unsigned int layout = compositor.layout, was_count = compositor.outputs;
        struct
        {
                char name[WINDOWS_OUTPUT_NAME];
                unsigned int degrees;
        } was[WINDOWS_OUTPUTS];
        b32 status = 0, turned = 0;
        int gone = compositor.screens_gone, wake = compositor.retrain && compositor.card >= 0;

        // Each screen's turn before the take, by its name: a screen that turns is one whose turn is another after it (the keyboard is put
        // away for it, compositor_osk_turned).
        for (unsigned int at = 0; at < was_count && at < WINDOWS_OUTPUTS; at++)
        {
                memory_copy(was[at].name, compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                was[at].degrees = compositor.output[at].degrees;
        }
        // After a wake the card's master is asked for again (the compositor's still, as a rule: then it is nothing), so a take is not refused for it.
        if (wake && system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0)
                compositor_kmsg((string_address) "compositor: wake: the card's master could not be had again", 0, (string_address) "");
        if (compositor.card >= 0)
                status = compositor_space_take(0);
        else
        {
                compositor.retrain = 0;
                compositor_headless_scale();
        }
        // What a wake's take did, in the kernel's log: the screens set, or why not (it is looked at again while they are dark).
        if (wake && status >= 0 && !compositor.retrain)
                compositor_kmsg((string_address) "compositor: wake: every crtc set again with a mode set, screens lit ", (positive)compositor.outputs,
                                (string_address) "");
        else if (wake && status < 0)
                compositor_kmsg((string_address) "compositor: wake: the screens could not be set yet, looked at again in 5 s, error ", (positive)-status,
                                (string_address) "");
        if (wake && status < 0 && compositor.screens_gone)
                compositor.retrain = 0; // the looks while they are dark set every crtc anew when one is found (screens_gone)
        if (wake && compositor.table)
        {
                if (status >= 0 && !compositor.retrain)
                        compositor.table->wake_sets++;
                else if (status < 0)
                        compositor.table->wake_refused++;
        }
        for (unsigned int at = 0; at < compositor.outputs && status >= 0; at++)
                for (unsigned int each = 0; each < was_count && each < WINDOWS_OUTPUTS; each++)
                        if (!memory_compare(was[each].name, compositor.output[at].name, WINDOWS_OUTPUT_NAME) &&
                            was[each].degrees != compositor.output[at].degrees)
                                turned = 1;

        // Screens that are still gone were said so when they went: the look every COMPOSITOR_SCREEN_LOOK says nothing more.
        if (status < 0 && !(gone && (status == -COMPOSITOR_ENODEV || status == -COMPOSITOR_EINVAL)))
                compositor_say("the screens could not be taken again, error", status);
        else if (status >= 0 && compositor.layout != layout)
                compositor_screens_say();
        if (turned)
                compositor_osk_turned();
        // The keyboard is sized for the output it is on, which may have changed: it is woken to look (osk_tick).
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if (compositor.asks[row] & WINDOW_KEYBOARD)
                        rows_add(&compositor.woken, row);
}

/*
        A screen plugged in or out. The kernel says so in a uevent of the drm
        subsystem -- HOTPLUG=1 on the card, a connector's change -- heard on a
        socket of NETLINK_KOBJECT_UEVENT, and the screens are taken again. A
        uevent is only a wake-up and is never believed further: any of the
        drm subsystem's is a reason to look, GETCONNECTOR asks every connector
        the card has, and taking the screens again when none changed is
        nothing (compositor_space_take). With no socket to be had the screens
        are the ones the desktop started with.
*/
#define COMPOSITOR_UEVENT 15 // NETLINK_KOBJECT_UEVENT

static int compositor_uevents_open(void)
{
        bipolar handle = socket_new(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, COMPOSITOR_UEVENT);
        socket_address_netlink self = {.family = AF_NETLINK, .groups = 1};

        if (handle < 0)
                return -1;
        if (socket_bind((b32)handle, address_of self, sizeof(self)) < 0)
        {
                system_close(handle);
                return -1;
        }
        return (int)handle;
}

// Whether a uevent, its lines KEY=VALUE each ended by a null, is the drm subsystem's.
static int compositor_uevent_drm(const char *message, long length)
{
        static const char want[] = "SUBSYSTEM=drm";

        for (long at = 0; at < length;)
        {
                long line = at;

                while (at < length && message[at])
                        at++;
                if (at - line == (long)sizeof(want) - 1 && !memory_compare(message + line, want, sizeof(want) - 1))
                        return 1;
                at++;
        }
        return 0;
}

// What the socket has, read to its end (a bounded number of messages a pass); the screens looked at once for a burst.
static void compositor_uevents_read(void)
{
        char message[2048];
        int drm = 0;

        for (int reads = 0; reads < 64; reads++)
        {
                bipolar got = socket_receive((b32)compositor.uevents, message, sizeof(message), MSG_DONTWAIT, 0, 0);

                if (got <= 0)
                        break;
                drm |= compositor_uevent_drm(message, got);
        }
        if (drm)
        {
                compositor_space_retake();
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_dmabuf_refresh();
#endif
        }
}

/*
        The card back to Canvas.

        The scanout's mapping holds the card's file open as surely as the
        descriptor does, so both go before the last close -- which is what
        runs Canvas's restore hook. The kernel says in its log when the card
        is Canvas's again.
*/
static void compositor_hand_back(void)
{
        gpu_forget((const void *)(compositor.scanout), (positive)(compositor.scanout_bytes)), compositor_call(syscall(munmap), (positive)compositor.scanout, compositor.scanout_bytes, 0, 0, 0, 0);
        system_close(compositor.card);
}


/*
        No card: the scanout is a file the program maps, which is what a test
        reads the picture out of, and the card's calls fail on a descriptor
        that is not there and are not looked at. -H WIDTHxHEIGHT FILE.
*/
// Headless: the scan image's spare beside it, <path>.spare, of the same size: the frames are composed into the spare and the two names are exchanged
// at each flip, so the image is whole at every moment (compositor_headless_flip). 0 when the names do not fit or the file does not make.
static b32 compositor_headless_plate(string_address path, long bytes)
{
        unsigned long length = 0, at;
        long handle, mapped;
        static const char suffix[] = ".spare";

        while (path[length])
                length++;
        if (length + sizeof(suffix) > sizeof(compositor_spare_name))
                return 0;
        for (at = 0; at <= length; at++)
                compositor_scan_name[at] = path[at], compositor_spare_name[at] = path[at];
        for (at = 0; suffix[at]; at++)
                compositor_spare_name[length + at] = suffix[at];
        compositor_spare_name[length + sizeof(suffix) - 1] = 0;
        handle = (long)system_open_at_mode(AT_FDCWD, (string_address)compositor_spare_name, FILE_READ_WRITE | O_CREAT | O_CLOEXEC, 0644);
        if (handle < 0)
                return 0;
        if (compositor_call(syscall(ftruncate), (positive)handle, (positive)bytes, 0, 0, 0, 0) < 0)
        {
                system_close((positive)handle);
                return 0;
        }
        mapped = compositor_call(syscall(mmap), 0, (positive)bytes, 3, 1, (positive)handle, 0);
        system_close((positive)handle);
        if (mapped < 0 && mapped > -4096)
                return 0;
        compositor_spare.fb = 2, compositor_spare.handle = 0, compositor_spare.pixels = (unsigned int *)mapped; // 2: the other name, as a framebuffer is
        compositor_spare.bytes = (unsigned long)bytes, compositor_spare.pitch = compositor.scanout_pitch;
        compositor_stale_all(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts);
        return 1;
}

static b32 compositor_headless(string_address size, string_address path)
{
        long width = 0, height = 0, handle, mapped, bytes;

        while (*size >= '0' && *size <= '9')
        {
                width = width * 10 + (*size++ - '0');
                if (width > 16384)
                        return -COMPOSITOR_EINVAL;
        }
        if (*size++ != 'x')
                return -COMPOSITOR_EINVAL;
        while (*size >= '0' && *size <= '9')
        {
                height = height * 10 + (*size++ - '0');
                if (height > 16384)
                        return -COMPOSITOR_EINVAL;
        }
        if (width < 64 || height < 64)
                return -COMPOSITOR_EINVAL;

        bytes = width * height * 4;
        handle = (long)system_open_at_mode(AT_FDCWD, path, FILE_READ_WRITE | O_CREAT | O_CLOEXEC, 0644);
        if (handle < 0)
                return -COMPOSITOR_EINVAL;
        if (compositor_call(syscall(ftruncate), (positive)handle, (positive)bytes, 0, 0, 0, 0) < 0)
        {
                system_close((positive)handle);
                return -COMPOSITOR_EINVAL;
        }

        mapped = compositor_call(syscall(mmap), 0, (positive)bytes, 3, 1, (positive)handle, 0);
        system_close((positive)handle);
        if (mapped < 0 && mapped > -4096)
                return -COMPOSITOR_ENOSPC;

        compositor.card = -1;
        compositor.scanout = (unsigned int *)mapped;
        compositor.scanout_bytes = (unsigned long)bytes;
        compositor.scanout_pitch = (unsigned long)width * 4;
        compositor.width = (int)width;
        compositor.height = (int)height;
        compositor.period = 1000000000ull / 60;
        // The scan image is the buffer shown (1: its name for the flips, as a card's framebuffer is), its spare the one composed into.
        compositor.fb = 1;
        compositor_plate_refused = 0;
        if (!compositor_headless_plate(path, bytes))
                compositor_spare.fb = 0;
        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_present();
        return 0;
}

static b32 compositor_option(string_address argument, char letter)
{
        return argument[0] == '-' && argument[1] == letter && !argument[2];
}

// A decimal number, or with hex a hexadecimal one, as far as it goes.
static bipolar compositor_number(string_address text, b32 hex)
{
        bipolar value = 0, sign = 1;

        if (*text == '-')
                sign = -1, text++;

        for (; *text; text++)
        {
                b32 digit = *text >= '0' && *text <= '9' ? *text - '0'
                            : hex && (*text | 32) >= 'a' && (*text | 32) <= 'f' ? (*text | 32) - 'a' + 10
                                                                                : -1;

                if (digit < 0)
                        break;
                value = value * (hex ? 16 : 10) + digit;
        }
        return value * sign;
}

// A file, mapped to be read for good: a font.
static positive compositor_map_file(string_address path, positive address_to size)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        long bytes, mapped;

        if (handle < 0)
                return 0;

        bytes = compositor_call(syscall(lseek), (positive)handle, 0, 2, 0, 0, 0);
        mapped = bytes > 0 ? compositor_call(syscall(mmap), 0, (positive)bytes, 1, 2, (positive)handle, 0) : -1;
        system_close(handle);

        if (mapped < 0 && mapped > -4096)
                return 0;

        address_to size = (positive)bytes;
        return (positive)mapped;
}

/*
        The faces behind the chain every engine draws with: Noto Sans Arabic and Noto Sans Hebrew, each regular and bold, which the
        font engine reaches for when the chain has no glyph for a code point (font_fallback). Mapped once for the program, by the
        first engine that asks, and put behind each engine after that; a file the image does not have leaves the chain as it was.
*/
static void compositor_fallbacks(font_engine address_to engine)
{
        static const string_address paths[4] = {"/usr/share/fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf",
                                                "/usr/share/fonts/noto-sans-arabic/NotoSansArabic-Bold.ttf",
                                                "/usr/share/fonts/noto-sans-hebrew/NotoSansHebrew-Regular.ttf",
                                                "/usr/share/fonts/noto-sans-hebrew/NotoSansHebrew-Bold.ttf"};
        static font_face faces[4];
        static b32 loaded[4], tried;

        if (!tried)
        {
                tried = 1;
                for (int at = 0; at < 4; at++)
                {
                        positive size = 0, bytes = compositor_map_file(paths[at], address_of size);

                        loaded[at] = bytes && font_load(&faces[at], (const p8 address_to)bytes, size, 0) == 0;
                }
        }
        for (int pair = 0; pair < 2; pair++)
                if (loaded[2 * pair])
                        font_fallback(engine, &faces[2 * pair], loaded[2 * pair + 1] ? &faces[2 * pair + 1] : null);
}

// The face the tabs are written in and the band they are drawn into; without one the tabs are shapes.
static void compositor_font(string_address path)
{
        static p8 atlas[128 * 128];
        positive size = 0, bytes = path && MOONWATER_UI_TASKBAR ? compositor_map_file(path, address_of size) : 0;
        unsigned long band_bytes = 0, glass_bytes = 0;
        long engine, band, glass;

        if (!bytes)
                return;

        // For the largest screen, which any screen the band is on fits in.
        taskbar_bytes(compositor_most_width(), compositor_most_height(), compositor.scale, &band_bytes, &glass_bytes);
        engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);
        band = compositor_call(syscall(mmap), 0, (positive)band_bytes, 3, 0x22, (positive)-1, 0);
        // The glass: the band and the blur's reach on every side, for a band on any edge.
        compositor.glass_bytes = glass_bytes;
        glass = compositor_call(syscall(mmap), 0, compositor.glass_bytes, 3, 0x22, (positive)-1, 0);
        if ((engine < 0 && engine > -4096) || (band < 0 && band > -4096) || (glass < 0 && glass > -4096) ||
            font_load(&compositor.face, (const p8 address_to)bytes, size, 0) != 0)
        {
                compositor_say("no font for the tabs, error", -COMPOSITOR_EINVAL);
                return;
        }

        // Its bold, the moon's clock's: the same file's name with Bold for Regular (Inter-Regular.ttf, Inter-Bold.ttf); without one the clock is in
        // the face.
        {
                char bold_path[512];
                positive at = 0, bold_size = 0, bold_bytes;

                while (path[at] && at < sizeof(bold_path) - 8)
                        at++;
                if (path[at] == 0)
                {
                        string_copy((string_address)bold_path, path);
                        for (positive k = 0; k + 7 <= at; k++)
                                if (!memory_compare(bold_path + k, "Regular", 7))
                                {
                                        string_copy((string_address)bold_path + k + 4, (string_address)path + k + 7);
                                        memory_copy(bold_path + k, "Bold", 4);
                                        bold_bytes = compositor_map_file((string_address)bold_path, address_of bold_size);
                                        compositor.bold_ok = bold_bytes && font_load(&compositor.bold, (const p8 address_to)bold_bytes, bold_size, 0) == 0;
                                        break;
                                }
                }
        }
        compositor.engine = (font_engine address_to)engine;
        font_start(compositor.engine, atlas, 128, 128);
        compositor_fallbacks(compositor.engine);
        compositor.band = (unsigned int *)band;
        compositor.band_bytes = band_bytes;
        compositor.glass = (unsigned int *)glass;
        taskbar_changed();
        // The other screens' bands, where `taskbar display all` asks for them.
        compositor_band_room();
}

// The screens, a line each, the primary first: what the log says of the space whenever it is taken.
static void compositor_screens_say(void)
{
        for (int at = 0; at < compositor_output_count(); at++)
        {
                int x, y, width, height;

                compositor_output_rect(at, &x, &y, &width, &height);
                string_format(log, "compositor: screen %b %s %b by %b at %b,%b\n", (bipolar)at,
                              compositor_output_name(at)[0] ? (string_address)compositor_output_name(at) : (string_address) "-",
                              (bipolar)width, (bipolar)height, (bipolar)x, (bipolar)y);
        }
        log_flush();
}

/*
        The bands' memory for the screens they are on now: each band's pixels
        made again when its screen wants more than they have (a larger screen
        became the primary), a band on each other screen made with `taskbar
        display all` and given back without it, and the glass, which every
        band is drawn through in turn, as large as the largest wants. With no
        memory for the primary's band or the glass every tab is a shape, as
        with no font; with none for another band that band's are: a band
        larger than its memory would be read past its end.
*/
static unsigned int *compositor_remap(unsigned int *was, unsigned long had, unsigned long bytes)
{
        long mapped = compositor_call(syscall(mmap), 0, (positive)bytes, 3, 0x22, (positive)-1, 0);

        if (was)
                gpu_forget((const void *)(was), (positive)(had)), compositor_call(syscall(munmap), (positive)was, had, 0, 0, 0, 0);
        return mapped < 0 && mapped > -4096 ? 0 : (unsigned int *)mapped;
}

static void compositor_band_room(void)
{
        unsigned long glass_most = 0;
        int bands = compositor_bands();

        if (!compositor.band)
                return;
        for (int at = 0; at < WINDOWS_OUTPUTS; at++)
        {
                unsigned int **pixels = at ? &compositor.band_more[at] : &compositor.band;
                unsigned long *had = at ? &compositor.band_more_bytes[at] : &compositor.band_bytes;
                unsigned long band_bytes = 0, glass_bytes = 0;
                int x, y, width, height;

                if (at >= bands)
                {
                        if (*pixels)
                                gpu_forget((const void *)(*pixels), (positive)(*had)), compositor_call(syscall(munmap), (positive)*pixels, *had, 0, 0, 0, 0);
                        *pixels = 0;
                        *had = 0;
                        continue;
                }
                compositor_output_rect(at, &x, &y, &width, &height);
                taskbar_bytes(width, height, compositor.scale, &band_bytes, &glass_bytes);
                glass_most = glass_bytes > glass_most ? glass_bytes : glass_most;
                if (band_bytes <= *had)
                        continue;
                *pixels = compositor_remap(*pixels, *had, band_bytes);
                *had = *pixels ? band_bytes : 0;
                taskbar_changed();
                if (!at && !compositor.band)
                        break;
        }
        if (compositor.band && glass_most > compositor.glass_bytes)
        {
                compositor.glass = compositor_remap(compositor.glass, compositor.glass_bytes, glass_most);
                compositor.glass_bytes = compositor.glass ? glass_most : 0;
        }
        // No primary band, or no glass to draw any band through: every band's tabs are shapes.
        if (!compositor.band || !compositor.glass)
        {
                for (int at = 0; at < WINDOWS_OUTPUTS; at++)
                {
                        unsigned int **pixels = at ? &compositor.band_more[at] : &compositor.band;
                        unsigned long *had = at ? &compositor.band_more_bytes[at] : &compositor.band_bytes;

                        if (*pixels)
                                gpu_forget((const void *)(*pixels), (positive)(*had)), compositor_call(syscall(munmap), (positive)*pixels, *had, 0, 0, 0, 0);
                        *pixels = 0;
                        *had = 0;
                }
                if (compositor.glass)
                        gpu_forget((const void *)(compositor.glass), (positive)(compositor.glass_bytes)), compositor_call(syscall(munmap), (positive)compositor.glass, compositor.glass_bytes, 0, 0, 0, 0);
                compositor.glass = 0;
                compositor.glass_bytes = 0;
                taskbar_changed();
        }
}

/*
        compositor [-s SCALE] [-l LAYOUT] [-t] [-T] [-G] [-P] [-f FONT.ttf] [-u DESCRIPTOR] [-H WIDTHxHEIGHT FILE] [-i FILE]

        Takes the card, or with -H maps FILE as the screen instead and takes
        none, and then -G has the render node compose all the same (a build with the GPU; without -G a compositor with no
        card composes on the processor), and -P draws the pointer into FILE, as a card with no plane has it drawn (without
        it a headless screen has none). -T says the -i device is a tablet, a virtual machine's pointer, whose wheel can be
        a trackpad's stream. -s is the desktop scale when Canvas is not there to say, -t puts
        the tabs on the top edge and not the bottom, -f is the face the tabs'
        titles and the time are written in (without one the tabs are only
        shapes), -l is the keyboard layout when Canvas is not there to say (it
        is otherwise the one `moonwater keyboard` set, and follows it when it
        is set again), -u is a descriptor the compositor writes a line to and closes once its table and sockets are made
        (compositor_up_say), and -i reads one more input device, a file of evdev
        records: what a test types and points with when there is no hardware
        to do it.
*/
/*
        The descriptors the compositor may have, and whose they are. COMPOSITOR_DESCRIPTORS_WANTED is the limit it asks for at its
        start (compositor_descriptors_room; the kernel's own soft limit is 1024 and its hard one 4096), and compositor_descriptors_most
        what it got. The last COMPOSITOR_DESCRIPTORS_OWN numbers of that are the compositor's own: a descriptor a client hands it,
        or a client's connection, that the kernel numbers among them says the rest of the table is full (the kernel gives the
        lowest free number), and is refused (compositor_descriptor_crowded: the connection is closed, wayland_host.c). So clients
        cannot take the numbers the compositor opens a window's door, an input device plugged in or a copy with, nor leave the
        socket's accept failing for want of one, which would turn the loop without a wait for as long as they held them. With the
        limit asked for, everything the shim's own caps let every client hold at once fits below the line (the assert in
        wayland_host.c), so no client that keeps to them is refused.
*/
#define COMPOSITOR_DESCRIPTORS_WANTED (1L << 30) // as many as the kernel's hard limit lets it have: a client's dma-bufs are descriptors the compositor holds
#define COMPOSITOR_DESCRIPTORS_WARM 4096          // the table grown to at the start (the kernel grows it past this as it is used)
// The compositor's own that are no row's: the poll list's own, the inputs, the selections' history, the table, the card and the
// render node with the buffers given to it, the sound server's socket, the transfers' pipes and the doors' threads' kick; and two a row
// (its door and its wake). COMPOSITOR_DESCRIPTORS_OWN for the rows in use: the share grows as rows are made.
#define COMPOSITOR_DESCRIPTORS_FIXED (7 + 32 + 32 + 3 + 64 + 32)
#define COMPOSITOR_DESCRIPTORS_OWN (COMPOSITOR_DESCRIPTORS_FIXED + 2 * (long)compositor_rows_in_use)
static long compositor_descriptors_most = 1024;

static int compositor_descriptor_crowded(long descriptor)
{
        return descriptor >= compositor_descriptors_most - COMPOSITOR_DESCRIPTORS_OWN;
}

#ifdef CONFIG_MOONWATER_WAYLAND
// The Wayland shim: the protocol (wayland.c) and the compositor's side of it, the socket and the host (wayland_host.c).
#include "wayland.c"
#include "wayland_host.c"

static int compositor_wl_dmabuf_missing(void)
{
        return compositor_wl.started && !compositor_wl.render;
}

/*
        The picture the pointer wears over row (COMPOSITOR_NONE for none of a client's): the one row's client drew, copied when the shim's
        serial for it has moved since it was last asked (compositor_picture_take; the pixels are the client's pool, read in this pass and
        not after), and none when it drew none or one the pointer does not wear. True when what the pointer wears changed.
*/
static int compositor_picture_poll(int row)
{
        struct wayland_cursor picture;
        unsigned long was = compositor_picture.worn;

        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_wl.started || !wayland_cursor_picture(row, &picture))
        {
                compositor_picture.worn = 0;
                compositor_picture.asked = 0;
                return was != 0;
        }
        if (!compositor_picture.asked || picture.serial != compositor_picture.serial)
        {
                compositor_picture.asked = 1;
                compositor_picture.serial = picture.serial;
                compositor_picture.worn = compositor_picture_take(picture.pixels, picture.stride, picture.width, picture.height,
                                                                  picture.format == WLN_wl_shm_format_xrgb8888, picture.hot_x, picture.hot_y,
                                                                  picture.scale, compositor.scale);
        }
        return compositor_picture.worn != was;
}
#endif

/*
        The loop's poll list, sized by what can be in it: the loop's own (COMPOSITOR_POLLS_OWN: the signals, the watch of the input
        directory, the doors' threads' knock, the card while a flip is in flight, the sync_file of a job of the card's the loop waits
        on, the uevents and the clipboard's door), every
        input device, every window's door, and what the Wayland shim adds (COMPOSITOR_WL_POLLS, wayland_host.c, which counts the X
        window manager's with its own). The size was a sum of numbers written beside the loop, which the X window manager's two
        were left out of when they came: with every input, every client stalled and a flip in flight the list was written two
        entries past its end. Whoever adds an entry adds it to the constant its part is counted in.
*/
#ifndef COMPOSITOR_WL_POLLS
#define COMPOSITOR_WL_POLLS 0
#endif
#define COMPOSITOR_POLLS_OWN 7
// The list's room for `rows` rows in use and the clients there are (COMPOSITOR_WL_POLLS): the list grows with them (compositor_polls).
#define COMPOSITOR_POLLS_FOR(rows) (COMPOSITOR_POLLS_OWN + COMPOSITOR_INPUTS + (unsigned long)(rows) + COMPOSITOR_WL_POLLS)
#define COMPOSITOR_POLLS_MOST (1L << 26) // the entries the list's reservation has room for: address space only
static struct compositor_poll *compositor_polls_at;
static long compositor_polls_room;

// The loop's list with room for this pass (its rows and its clients), committed a doubling at a time; 0 when the memory is not there.
static struct compositor_poll *compositor_polls(void)
{
        long want = (long)COMPOSITOR_POLLS_FOR(compositor_rows_in_use), room;

        if (want <= compositor_polls_room)
                return compositor_polls_at;
        if (want > COMPOSITOR_POLLS_MOST)
                return 0;
        if (!compositor_polls_at)
        {
                long got = compositor_call(syscall(mmap), 0, (positive)(COMPOSITOR_POLLS_MOST * (long)sizeof(struct compositor_poll)), 0, 0x4022, (positive)-1, 0);

                if (got < 0 && got > -4096)
                        return 0;
                compositor_polls_at = (struct compositor_poll *)got;
        }
        room = compositor_polls_room ? 2 * compositor_polls_room : 1024;
        room = room < want ? want : room > COMPOSITOR_POLLS_MOST ? COMPOSITOR_POLLS_MOST : room;
        if (compositor_call(syscall(mprotect), (positive)compositor_polls_at, (positive)((room * (long)sizeof(struct compositor_poll) + 4095) & ~4095L), 3, 0, 0, 0) != 0)
                return 0;
        compositor_polls_room = room;
        return compositor_polls_at;
}
// The compositor's own descriptors that are no row's fit the share kept for them (COMPOSITOR_DESCRIPTORS_FIXED): the poll list's (but the
// clients'), the selections and their history, the table, the card, the render node and the buffers given to it, the sound server's
// socket and each transfer's pipe; a row's three are counted with it.
_Static_assert(COMPOSITOR_POLLS_OWN + COMPOSITOR_INPUTS_KEPT + COMPOSITOR_CLIP_HISTORY + 3 + 64 <= COMPOSITOR_DESCRIPTORS_FIXED,
               "the compositor's own descriptors fit the numbers kept for them");

/*
        The compositor's memory of each row: what was 81 arrays of WINDOWS in its structs and beside them, each a pointer into one
        reservation of address space (PROT_NONE and MAP_NORESERVE, of no memory until it is committed), its arrays one after another
        at page boundaries, every one with room for WINDOWS (WINDOWS + 1 for the doors' threads, committed whole, whose last is the misc door's; the
        bands' marks are a screen's each). A row's memory is committed (mprotect) when the row is made, compositor_rows_make, and its
        defaults set then (a popup's parent none); the elements never move, so a pointer to a row's taken before more rows were made
        is good after. A commit the kernel refuses is the rows not made, and nothing else. compositor_zero is the compositor's
        memory_zero: its struct zeroed, its arrays pointed at again and their rows zeroed, so a check that starts from nothing finds
        the rows made as they were. The groups: the compositor's own (zeroed with it), the Wayland host's, the card's, and those of
        the parts that keep theirs for the session.
*/
#define COMPOSITOR_ROWS_ROW(rows) (rows)
#define COMPOSITOR_ROWS_ROW1(rows) (WINDOWS + 1) // every slot's and the misc one's, at WINDOWS: pages touched only where a thread is
#define COMPOSITOR_ROWS_SCREENS(rows) WINDOWS_OUTPUTS
#define COMPOSITOR_ROWS_FRAMES(rows) COMPOSITOR_FRAMES_FOR(rows)
#define COMPOSITOR_ROWS_PIECES(rows) COMPOSITOR_PIECES_FOR(rows)
#define COMPOSITOR_ROWS_EDGES(rows) (2 * COMPOSITOR_PIECES_FOR(rows) + 2)
#define COMPOSITOR_ROWS_SPANS(rows) (COMPOSITOR_PIECES_FOR(rows) + 1)
#define COMPOSITOR_ROWS_LAYERED(rows) ((unsigned long)(rows) + COMPOSITOR_LAYERS)

#define COMPOSITOR_ROWS_OWN(X) \
        X(compositor.scan_fb, COMPOSITOR_ROWS_ROW) \
        X(compositor.scan_handle, COMPOSITOR_ROWS_ROW) \
        X(compositor.taken_held, COMPOSITOR_ROWS_ROW) \
        X(compositor.patched, COMPOSITOR_ROWS_ROW) \
        X(compositor.door, COMPOSITOR_ROWS_ROW) \
        X(compositor.wake, COMPOSITOR_ROWS_ROW) \
        X(compositor.seen, COMPOSITOR_ROWS_ROW) \
        X(compositor.runs, COMPOSITOR_ROWS_ROW) \
        X(compositor.runs_bytes, COMPOSITOR_ROWS_ROW) \
        X(compositor.runs_tag, COMPOSITOR_ROWS_ROW) \
        X(compositor.runs_composed, COMPOSITOR_ROWS_ROW) \
        X(compositor.runs_state, COMPOSITOR_ROWS_ROW) \
        X(compositor.showing, COMPOSITOR_ROWS_ROW) \
        X(compositor.landing, COMPOSITOR_ROWS_ROW) \
        X(compositor.land_at, COMPOSITOR_ROWS_ROW) \
        X(compositor.glide, COMPOSITOR_ROWS_ROW) \
        X(compositor.glide_from, COMPOSITOR_ROWS_ROW) \
        X(compositor.glide_at, COMPOSITOR_ROWS_ROW) \
        X(compositor.glide_base, COMPOSITOR_ROWS_ROW) \
        X(compositor.made, COMPOSITOR_ROWS_ROW) \
        X(compositor.x, COMPOSITOR_ROWS_ROW) \
        X(compositor.y, COMPOSITOR_ROWS_ROW) \
        X(compositor.w, COMPOSITOR_ROWS_ROW) \
        X(compositor.h, COMPOSITOR_ROWS_ROW) \
        X(compositor.front, COMPOSITOR_ROWS_ROW) \
        X(compositor.state, COMPOSITOR_ROWS_ROW) \
        X(compositor.close_asks, COMPOSITOR_ROWS_ROW) \
        X(compositor.picture_w, COMPOSITOR_ROWS_ROW) \
        X(compositor.picture_h, COMPOSITOR_ROWS_ROW) \
        X(compositor.asking, COMPOSITOR_ROWS_ROW) \
        X(compositor.arranged, COMPOSITOR_ROWS_ROW) \
        X(compositor.fullscreen, COMPOSITOR_ROWS_ROW) \
        X(compositor.fullscreen_was, COMPOSITOR_ROWS_ROW) \
        X(compositor.edges, COMPOSITOR_ROWS_ROW) \
        X(compositor.far_x, COMPOSITOR_ROWS_ROW) \
        X(compositor.far_y, COMPOSITOR_ROWS_ROW) \
        X(compositor.saved_x, COMPOSITOR_ROWS_ROW) \
        X(compositor.saved_y, COMPOSITOR_ROWS_ROW) \
        X(compositor.saved_w, COMPOSITOR_ROWS_ROW) \
        X(compositor.saved_h, COMPOSITOR_ROWS_ROW) \
        X(compositor.title, COMPOSITOR_ROWS_ROW) \
        X(compositor.app_id, COMPOSITOR_ROWS_ROW) \
        X(compositor.icon_seq, COMPOSITOR_ROWS_ROW) \
        X(compositor.icon, COMPOSITOR_ROWS_ROW) \
        X(compositor.name, COMPOSITOR_ROWS_ROW) \
        X(compositor.moved, COMPOSITOR_ROWS_ROW) \
        X(compositor.stack, COMPOSITOR_ROWS_ROW) \
        X(compositor.listed, COMPOSITOR_ROWS_ROW) \
        X(compositor.marks, COMPOSITOR_ROWS_ROW) \
        X(compositor.lengths, COMPOSITOR_ROWS_ROW) \
        X(compositor.banded, COMPOSITOR_ROWS_ROW) \
        X(compositor.glassy, COMPOSITOR_ROWS_ROW) \
        X(compositor.asks, COMPOSITOR_ROWS_ROW) \
        X(compositor.backdrop, COMPOSITOR_ROWS_ROW) \
        X(compositor.tinted, COMPOSITOR_ROWS_ROW) \
        X(compositor.composed, COMPOSITOR_ROWS_ROW) \
        X(compositor.boosted, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_sum, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_blocks, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_cut, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_made, COMPOSITOR_ROWS_ROW) \
        X(compositor.stale, COMPOSITOR_ROWS_ROW) \
        X(compositor.dirty, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_side, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_told, COMPOSITOR_ROWS_ROW) \
        X(compositor.lifted, COMPOSITOR_ROWS_ROW) \
        X(compositor.hold_rect, COMPOSITOR_ROWS_ROW) \
        X(compositor.hold_how, COMPOSITOR_ROWS_ROW) \
        X(compositor.shared_sizes, COMPOSITOR_ROWS_ROW) \
        X(compositor.osk_pan, COMPOSITOR_ROWS_ROW) \
        X(compositor.osk_height, COMPOSITOR_ROWS_ROW) \
        X(compositor.osk_edges, COMPOSITOR_ROWS_ROW) \
        X(compositor.text_purpose, COMPOSITOR_ROWS_ROW) \
        X(compositor.caret, COMPOSITOR_ROWS_ROW) \
        X(compositor.band_marks, COMPOSITOR_ROWS_SCREENS)

#ifdef CONFIG_MOONWATER_WAYLAND
#define COMPOSITOR_ROWS_WL(X) \
        X(compositor_wl.laying, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.parts, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.cursor_asked, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.drag_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.take_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.done_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.row_blend, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.lent, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.source, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.wc_reads, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.wc_copy, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl_layers.layer, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl_layers.count, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl_layers.root_refused, COMPOSITOR_ROWS_ROW)
#else
#define COMPOSITOR_ROWS_WL(X)
#endif

#define COMPOSITOR_ROWS_CARD(X) \
        X(compositor_card.list_seq, COMPOSITOR_ROWS_ROW)

#define COMPOSITOR_ROWS_KEPT(X) \
        X(compositor_popup_parent, COMPOSITOR_ROWS_ROW) \
        X(compositor_owner, COMPOSITOR_ROWS_ROW) \
        X(compositor_listed, COMPOSITOR_ROWS_ROW) \
        X(compositor_clip.offer_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_clip.ask_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_clip.gesture, COMPOSITOR_ROWS_ROW) \
        X(compositor_gpu_pins, COMPOSITOR_ROWS_ROW) \
        X(compositor_osk_held, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed_flags, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed_keys, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed_key_flags, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed_any, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed_physical, COMPOSITOR_ROWS_ROW) \
        X(compositor_owed_key_physical, COMPOSITOR_ROWS_ROW) \
        X(compositor_doors.asked, COMPOSITOR_ROWS_ROW1) \
        X(compositor_record_shots[0].rects, COMPOSITOR_ROWS_ROW) \
        X(compositor_record_shots[1].rects, COMPOSITOR_ROWS_ROW) \
        X(compositor_record_shots[2].rects, COMPOSITOR_ROWS_ROW) \
        X(compositor_record_pending, COMPOSITOR_ROWS_ROW) \
        X(compositor_tab_titles, COMPOSITOR_ROWS_ROW) \
        X(compositor_tab_apps, COMPOSITOR_ROWS_ROW) \
        X(compositor_tab_icons, COMPOSITOR_ROWS_ROW) \
        X(compositor_tab_seqs, COMPOSITOR_ROWS_ROW) \
        X(compositor_present_frame_room, COMPOSITOR_ROWS_FRAMES) \
        X(compositor_present_row_room, COMPOSITOR_ROWS_LAYERED) \
        X(compositor_scene_radii, COMPOSITOR_ROWS_FRAMES) \
        X(compositor_plan_box, COMPOSITOR_ROWS_PIECES) \
        X(compositor_plan_owner, COMPOSITOR_ROWS_PIECES) \
        X(compositor_plan_edge, COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_edge[0], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[0], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[1], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[1], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[2], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[2], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[3], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[3], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[4], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[4], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[5], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[5], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[6], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[6], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[7], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[7], COMPOSITOR_ROWS_SPANS) \
        X(compositor_lane_edge[8], COMPOSITOR_ROWS_EDGES) \
        X(compositor_lane_span[8], COMPOSITOR_ROWS_SPANS)

// The card's own: there only in a build with the card's code (MOONWATER_GPU_C), which the build tool and a build without it are not.
#ifdef MOONWATER_GPU_C
#define COMPOSITOR_ROWS_GPU(X) X(compositor_gpu_scaled_copy, COMPOSITOR_ROWS_FRAMES)
#else
#define COMPOSITOR_ROWS_GPU(X)
#endif

#define COMPOSITOR_ROWS_ALL(X) COMPOSITOR_ROWS_OWN(X) COMPOSITOR_ROWS_WL(X) COMPOSITOR_ROWS_CARD(X) COMPOSITOR_ROWS_GPU(X) COMPOSITOR_ROWS_KEPT(X)

static unsigned char *compositor_row_at; // the reservation, 0 before the first row is made
static unsigned long compositor_row_bytes;

static unsigned long compositor_row_round(unsigned long bytes)
{
        return (bytes + 4095ul) & ~4095ul;
}

// An array's first bytes made usable (zero where they were never touched): 1 when they are, 0 when the kernel refused.
static int compositor_row_commit(void *at, unsigned long bytes)
{
        return !bytes || compositor_call(syscall(mprotect), (positive)at, (positive)compositor_row_round(bytes), 3, 0, 0, 0) == 0;
}

#define COMPOSITOR_ROWS_SIZE(array, kind) bytes += compositor_row_round((unsigned long)kind(WINDOWS) * sizeof(*(array)));
#define COMPOSITOR_ROWS_POINT(array, kind) \
        (array) = (__typeof__(array))(void *)(compositor_row_at + at), at += compositor_row_round((unsigned long)kind(WINDOWS) * sizeof(*(array)));
#define COMPOSITOR_ROWS_COMMIT(array, kind) made = made && compositor_row_commit((array), (unsigned long)kind(rows) * sizeof(*(array)));
#define COMPOSITOR_ROWS_ZERO(array, kind) memory_zero((array), (unsigned long)kind(compositor_rows_made) * sizeof(*(array)));

// Every array pointed at its place in the reservation (after a struct that holds them was zeroed).
static void compositor_rows_point(void)
{
        unsigned long at = 0;

        if (!compositor_row_at)
                return;
        COMPOSITOR_ROWS_ALL(COMPOSITOR_ROWS_POINT)
}

// A row made: what it is before any window has it.
static void compositor_row_defaults(unsigned int row)
{
        compositor_popup_parent[row] = -1;
}

/*
        The rows below rows made, the reservation first: their memory committed and their defaults set. 1 when they are; 0 when the
        address space or the memory is not there, and the rows made are as they were.
*/
static int compositor_rows_make(unsigned int rows)
{
        int made = 1;

        if (rows > WINDOWS)
                return 0;
        if (!compositor_row_at)
        {
                unsigned long bytes = 0;
                long got;

                COMPOSITOR_ROWS_ALL(COMPOSITOR_ROWS_SIZE)
                got = compositor_call(syscall(mmap), 0, (positive)bytes, 0, 0x4022, (positive)-1, 0); // PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE
                if (got < 0 && got > -4096)
                        return 0;
                compositor_row_at = (unsigned char *)got;
                compositor_row_bytes = bytes;
                compositor_rows_point();
        }
        if (rows <= compositor_rows_made)
                return 1;
        COMPOSITOR_ROWS_ALL(COMPOSITOR_ROWS_COMMIT)
        if (!made)
                return 0;
        for (unsigned int row = compositor_rows_made; row < rows; row++)
                compositor_row_defaults(row);
        compositor_rows_made = rows;
        return 1;
}

// The compositor's struct as memory_zero left it, but for its rows: pointed at again, made (the rows in use) and zeroed.
static void compositor_zero(void)
{
        memory_zero(&compositor, sizeof(compositor));
        compositor_rows_point();
        compositor_rows_make(compositor_rows_in_use);
        COMPOSITOR_ROWS_OWN(COMPOSITOR_ROWS_ZERO)
}

// The card's struct likewise.
static void compositor_card_zero(void)
{
        memory_zero(&compositor_card, sizeof(compositor_card));
        compositor_rows_point();
        COMPOSITOR_ROWS_CARD(COMPOSITOR_ROWS_ZERO)
}

/*
        A row's door and wake, made and opened (S_IFIFO, the table's mode): the door is read here and the wake written, each end kept
        for the session. 0, or the kernel's error. A number among the compositor's own last ones (the share no row has,
        COMPOSITOR_DESCRIPTORS_FIXED, below compositor_descriptors_most) is refused as one a row may not take: a row is made only while
        there is room for both its ends.
*/
static long compositor_door_make(unsigned int row)
{
        p8 name[48];

        for (int wake = 0; wake < 2; wake++)
        {
                long handle;

                windows_door_name((char *)name, row, wake);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
                compositor_call(syscall(mknodat), (positive)AT_FDCWD, (positive)name, 0010000 | WINDOWS_MODE, 0, 0, 0);
                compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)name, WINDOWS_MODE, 0, 0, 0);
                handle = (long)system_open_at(AT_FDCWD, name, (wake ? FILE_READ_WRITE : FILE_READ) | O_NONBLOCK | O_CLOEXEC);
                if (handle >= 0 && handle >= compositor_descriptors_most - COMPOSITOR_DESCRIPTORS_FIXED)
                {
                        system_close(handle);
                        handle = -24; // EMFILE
                }
                if (handle < 0)
                {
                        if (wake)
                                system_close(compositor.door[row]);
                        return handle;
                }
                if (wake)
                        compositor.wake[row] = (int)handle;
                else
                        compositor.door[row] = (int)handle;
        }
        return 0;
}

/*
        Rows made past the rows in use, to want (to the rows the table has room for at the most): their memory, their doors and their
        threads (when the doors' threads have started), each row in turn, and the table told when they are all made. A row the kernel
        refuses anything of is where it stops: the rows made before it are in use, and the next ask tries again. The rows in use.
*/
static unsigned int compositor_rows_grow(unsigned int want)
{
        unsigned int row = compositor_rows_in_use, to = want < compositor_rows_room ? want : compositor_rows_room;

        long refused = 0;

        if (!compositor.table || to <= row)
                return compositor_rows_in_use;
        if (!compositor_rows_make(to))
                refused = -12; // ENOMEM
        for (; !refused && row < to; row++)
        {
                if ((refused = compositor_door_make(row)) < 0)
                        break;
        }
        // Said at each stop, so the log says where a refusal of the kernel's held the rows.
        if (refused)
        {
                string_format(log, "compositor: rows made to %p of the %p asked, the kernel refused the next (error %b)\n", (positive)row,
                              (positive)to, (bipolar)refused);
                log_flush();
        }
        if (row > compositor_rows_in_use)
        {
                compositor_rows_in_use = row;
                __atomic_store_n(&compositor.table->rows, row, __ATOMIC_RELEASE);
        }
        return compositor_rows_in_use;
}

// At a pass of the loop: rows made when fewer than COMPOSITOR_ROWS_SPARE of those in use are free, looked for from the last (the rows a
// program takes are the lowest free, so the free ones are mostly the last). A step of half the rows in use, or the spares.
static void compositor_rows_spare(void)
{
        unsigned int free = 0, step = compositor_rows_in_use / 2 > COMPOSITOR_ROWS_SPARE ? compositor_rows_in_use / 2 : COMPOSITOR_ROWS_SPARE;

        if (!compositor.table || compositor_rows_in_use >= compositor_rows_room)
                return;
        for (unsigned int row = compositor_rows_in_use; row-- > 0 && free < COMPOSITOR_ROWS_SPARE;)
                free += __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE) == 0;
        if (free < COMPOSITOR_ROWS_SPARE)
                compositor_rows_grow(compositor_rows_in_use + step);
}

/*
        What the devices said this turn is on its way to the programs it is for, now: each row of the kit that was given an event is
        knocked on (its wake's byte, by its thread), before the turn reads a settings file, takes a door's picture or a client's
        commit, or composes a frame, where the one knock of a turn was made after the frame had been composed and presented and an
        event read as a frame came due reached its program a composition late. A row that is a Wayland client's is not knocked on:
        its ring is read and its socket written by compositor_wl_wake, before the frame, and its bit stays for that.
*/
static void compositor_forward(void)
{
        char byte = 0;

        if (!rows_any(&compositor.woken))
                return;
        // A word of no row to knock is passed over whole; a client's row keeps its bit (its socket is written by compositor_wl_wake).
        for (unsigned int w = 0; w < compositor_rows_words(); w++)
                for (int row = (int)w * 64; compositor.woken.word[w] && row < (int)(w + 1) * 64 && row < (int)compositor_rows_in_use; row++)
                {
                        if (!rows_has(&compositor.woken, row))
                                continue;
#ifdef CONFIG_MOONWATER_WAYLAND
                        if (compositor_wl.started && wayland_row_owner(row) >= 0)
                                continue;
#endif
                        rows_del(&compositor.woken, row);
                        if (!compositor_door_ask((unsigned int)row, COMPOSITOR_DOOR_WAKE))
                                system_write_once(compositor.wake[row], &byte, 1);
                }
}

// A turn's devices: each one the poll said has something is read, and what they said is forwarded at once (compositor_forward).
static void compositor_inputs_turn(const struct compositor_poll *wanted)
{
        for (unsigned int at = 0; at < compositor.input_count; at++)
                if (wanted[at].returned)
                        compositor_input_read(at);
        compositor_forward();
}

/*
        Room in the descriptor table for every descriptor the session can have, made while the program is one thread. The
        kernel grows a table when a descriptor past its end is asked for, and a table that threads share is grown with a
        wait for every processor to pass a quiet state (expand_fdtable's synchronize_rcu). The table starts at 64, the
        composing threads are started by the first frame and the 65th descriptor is the door of the 29th window, so every
        start stood still there: 9 to 16 ms between the first frame and the window table's doors, measured headless, and
        the same again at the 129th, 257th and 513th, in the middle of a session. A descriptor made at the last number the
        limit allows and closed leaves the table that size for good. The limit is raised first, to COMPOSITOR_DESCRIPTORS_WANTED
        or as near it as the hard limit lets it be: at the kernel's 1024 the clients of the Wayland socket could fill the table.
*/
static void compositor_descriptors_room(void)
{
        positive limits[2] = {0, 0};

        // RLIMIT_NOFILE: the soft limit and the hard one.
        if (compositor_call(syscall(prlimit64), 0, 7, 0, (positive)limits, 0, 0) == 0)
        {
                positive most = limits[1] < COMPOSITOR_DESCRIPTORS_WANTED ? limits[1] : COMPOSITOR_DESCRIPTORS_WANTED;

                if (limits[0] < most)
                {
                        positive raised[2] = {most, limits[1]};

                        if (compositor_call(syscall(prlimit64), 0, 7, (positive)raised, 0, 0, 0) == 0)
                                limits[0] = most;
                }
                compositor_descriptors_most = (long)(limits[0] < COMPOSITOR_DESCRIPTORS_WANTED ? limits[0] : COMPOSITOR_DESCRIPTORS_WANTED);
        }
        for (long last = (compositor_descriptors_most < COMPOSITOR_DESCRIPTORS_WARM ? compositor_descriptors_most : COMPOSITOR_DESCRIPTORS_WARM) - 1; last >= 127;
             last = (last - 1) / 2)
        {
                long made = compositor_call(syscall(fcntl), 0, COMPOSITOR_F_DUPFD_CLOEXEC, (positive)last, 0, 0, 0);

                if (made >= 0)
                {
                        system_close(made);
                        return;
                }
        }
}

/*
        The word that the desktop is up, for what started the compositor with -u DESCRIPTOR: a line written to the
        descriptor, which is then closed. The machine script hands the writing end of a FIFO and reads the other, so it
        goes on the moment the table, its doors and the sockets are there, and reads the end of the file at once if the
        compositor ends before that (the compositor's is the only writing end); it looked for the table's name every
        fiftieth of a second.
*/
static void compositor_up_say(int descriptor)
{
        if (descriptor < 3)
                return;
        system_write_once(descriptor, "up\n", 3);
        system_close(descriptor);
}

/*
        The loop's poll list for a turn, where compositor_loop_source reads it: the signals, the watch of the input directory, every input device, every
        window's door (a door its thread has, a drain or a reopen out, is not polled until the thread is done with it), the doors' threads' knock, the card
        while a flip is in flight, the sync_file of the card's job a present waits for, the uevents, the clipboard's door, and what the Wayland shim adds.
        held (compositor_card_clients_held): a present's jobs read the programs' pictures where they are, so no door, no doors' knock and no Wayland socket
        is read, which is everything a program could be told its picture is free by; the input devices are read whatever it says, and what they say is
        forwarded (compositor_inputs_turn, compositor_wl_wake), the doors' slots kept in their places (none polled).
*/
static void compositor_loop_wanted(struct compositor_poll *wanted, struct compositor_slots *slots, b32 held)
{
        unsigned int count = 0;

        memory_zero(slots, sizeof(*slots));
        wanted[count++] = (struct compositor_poll){compositor.signals, COMPOSITOR_POLLIN, 0};
        wanted[count++] = (struct compositor_poll){compositor.watch, COMPOSITOR_POLLIN, 0};
        slots->inputs = count;
        for (unsigned int at = 0; at < compositor.input_count; at++)
                wanted[count++] = (struct compositor_poll){compositor.inputs[at], COMPOSITOR_POLLIN, 0};
        slots->doors = count;
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                wanted[count++] = (struct compositor_poll){held || rows_has(&compositor_doors.busy, row) ? -1 : __atomic_load_n(&compositor.door[row], __ATOMIC_ACQUIRE),
                                                           COMPOSITOR_POLLIN, 0};
        if (compositor_doors.done >= 0 && !held)
        {
                slots->done_at = count;
                wanted[count++] = (struct compositor_poll){compositor_doors.done, COMPOSITOR_POLLIN, 0};
        }
        // A flip in flight: its event says the card has let go of the buffer it showed before.
        if (compositor.flip_pending)
        {
                slots->card_at = count;
                wanted[count++] = (struct compositor_poll){compositor.card, COMPOSITOR_POLLIN, 0};
        }
#ifdef MOONWATER_GPU_C
        // A job of the card's the loop waits for: its sync_file is readable once the job is done (-1, polled for nothing, when there is none to have).
        if (compositor_card_awaits())
        {
                slots->fence_at = count;
                wanted[count++] = (struct compositor_poll){compositor_card_poll(), COMPOSITOR_POLLIN, 0};
        }
#endif
        if (compositor.uevents >= 0)
        {
                slots->uevents_at = count;
                wanted[count++] = (struct compositor_poll){compositor.uevents, COMPOSITOR_POLLIN, 0};
        }
        if (compositor_clip.door >= 0 && !rows_has(&compositor_doors.busy, COMPOSITOR_DOOR_MISC))
        {
                slots->clip_at = count;
                wanted[count++] = (struct compositor_poll){compositor_clip.door, COMPOSITOR_POLLIN, 0};
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!held)
        {
                slots->wayland_at = count;
                count = compositor_wl_poll(wanted, count);
        }
#endif
        slots->count = count;
}

static b32 compositor_main()
{
        b32 status;
        int running = 1, extra = -1, tablet = 0, up = -1;
        string_address headless = null, file = null, font = null;

        claim_standard_descriptors();
        compositor_descriptors_room();
        // The loop's waits end when they are asked to: the kernel gives every wait of a thread 50 us of slack to end late in
        // (it joins timers by it), which a frame's timer and every other time of the loop had added to it. PR_SET_TIMERSLACK, 1 ns.
        compositor_call(syscall(prctl), 29, 1, 0, 0, 0, 0);

        compositor_zero();
        if (compositor_rows_made < compositor_rows_in_use)
        {
                string_format(log, "compositor: no memory for the rows of the window table\n");
                log_flush();
                return 1;
        }
        compositor.watch = compositor.uevents = -1;
        compositor.hold = compositor.game = -1;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = compositor.told_row = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.lit_row = compositor.preview_row = COMPOSITOR_NONE;
        compositor.mark_row = compositor.mark_with = compositor.shared_row = COMPOSITOR_NONE;
        compositor.touch.closing = COMPOSITOR_NONE;
        compositor.cursor.asked_row = COMPOSITOR_NONE;

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (compositor_option(option, 't'))
                        taskbar_position_default(TASKBAR_TOP);
                else if (compositor_option(option, 'T'))
                        tablet = 1; // the -i device is a tablet, a virtual machine's pointer: its wheel can be a trackpad's stream
                else if (compositor_option(option, 'P'))
                        compositor.cursor.soft = 1; // the pointer drawn into the screen, as on a card with no plane: a headless one's pixels have it
                else if (at + 1 < program_argument_count() && compositor_option(option, 's'))
                        compositor.scale = (int)compositor_number(program_argument(++at), 0) * 100;
                else if (at + 1 < program_argument_count() && compositor_option(option, 'f'))
                        font = program_argument(++at);
                else if (at + 1 < program_argument_count() && compositor_option(option, 'u'))
                        up = (int)compositor_number(program_argument(++at), 0);
#ifdef MOONWATER_GPU_C
                else if (compositor_option(option, 'G'))
                        compositor_gpu_state.force = 1;
#endif
                else if (at + 2 < program_argument_count() && compositor_option(option, 'H'))
                {
                        headless = program_argument(at + 1);
                        file = program_argument(at + 2);
                        at += 2;
                }
                else if (at + 1 < program_argument_count() && compositor_option(option, 'l'))
                {
                        int layout = compositor_layout_index((const char *)program_argument(++at));

                        if (layout >= 0)
                                compositor.keymap.layout = (unsigned char)layout, compositor.layout_given = 1;
                }
                else if (at + 1 < program_argument_count() && compositor_option(option, 'i'))
                        // Read and write, so that a fifo has a writer (the compositor's own) while the driver that feeds it has none between its steps: opened
                        // for reading alone, it answers every poll at once with a hangup and a read of nothing, and the loop turns at 100 percent of a core.
                        extra = (int)system_open_at(AT_FDCWD, program_argument(++at),
                                                    FILE_READ_WRITE | O_CLOEXEC | O_NONBLOCK);
        }

        compositor_keys_read();
        // A present late in its period, unless the machine's owner asks for every present early (compositor_present_due).
        {
                bipolar early = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/present-early", FILE_READ | O_CLOEXEC);

                compositor_late.early = early >= 0;
                if (early >= 0)
                        system_close(early);
        }
        // No game's hold of the processor's idle states, where the owner says so (compositor_game_watch).
        {
                bipolar off = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/game-hold-off", FILE_READ | O_CLOEXEC);

                if (off >= 0)
                        compositor.games = ~0ul, system_close(off);
        }
#ifdef MOONWATER_GPU_C
        // The card's present without a wait, unless the owner asks for every job waited for in place, as it was (/run/moonwater/present-wait).
        {
                bipolar wait = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/present-wait", FILE_READ | O_CLOEXEC);

                compositor_card.nowait = wait < 0;
                if (wait >= 0)
                        system_close(wait);
        }
        // The card's clocks held while input arrives (compositor_gpu_clocks), unless the owner's file /run/moonwater/gpu-clocks-off is there.
        {
                bipolar off = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/gpu-clocks-off", FILE_READ | O_CLOEXEC);

                if (off >= 0)
                        compositor_gpu_clocks.level = 0, system_close(off);
        }
        // A window's list drawn by a job of the card the loop does not wait for, where the owner asks for it (/run/moonwater/lists-nowait); waited for in
        // place otherwise, as it was.
        {
                bipolar lists = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/lists-nowait", FILE_READ | O_CLOEXEC);

                compositor_card.lists_nowait = lists >= 0;
                if (lists >= 0)
                        system_close(lists);
        }
#endif
        if (compositor.scale < 25 || compositor.scale > 800)
                compositor.scale = WINDOWS_SCALE_DEFAULT;
        compositor.boot_scale = compositor.scale;
        if (!headless && !compositor.layout_given)
                compositor_layout_read();

        /*
                Out of memory, the kernel kills the process its score names, and the compositor holds every client's windows, so with it went
                the whole desktop: a client that made surfaces until memory ran out was as likely to take the compositor down as itself (what a
                client sends costs the compositor its records of it). -900 keeps the compositor for nearly last, as system daemons are kept,
                and not exempt (-1000), so a compositor that is itself the leak still goes. What a client's pools and dma-bufs hold is the
                client's: its memfds and buffers are charged to it. The compositor starts no program, so nothing inherits this. Not allowed (a
                compositor run without the privilege, a test's user namespace), it changes nothing.
        */
        {
                bipolar oom = system_open_at(AT_FDCWD, (string_address) "/proc/self/oom_score_adj", FILE_WRITE | O_CLOEXEC);

                if (oom >= 0)
                {
                        (void)system_write_all((positive)oom, (string_address) "-900\n", 5);
                        system_close(oom);
                }
        }
        /*
                The loop is real time: SCHED_FIFO at the lowest priority, the sound server's (src/audio/service.c), so neither cuts into the other's
                short work, and every ordinary thread (a game's, a build's) waits while a key, a vblank or a commit is worked. At ordinary
                scheduling the loop took its turn among them: on hasty-tail (four cores) under eight spinning processes a thread woken at 144 Hz,
                the loop's shape, woke 2028 us late at the 99th percentile and 2515 us at worst; real time, 5 and 7 us (the same thread idle: 40 and
                50 us at ordinary, 20 and 31 real time). The threads it starts take the policy (the doors, the bands of a frame composed on the
                processor); the ones no frame waits for go back to ordinary (compositor_ordinary). The compositor starts no program, so nothing else
                inherits it. A headless one (a test's, a shot's) stays ordinary; not allowed (a test's user namespace, a compositor without the
                privilege), it changes nothing.
        */
        if (!headless)
        {
                int lowest = 1;

                compositor_call(syscall(sched_setscheduler), 0, 1, (positive)&lowest, 0, 0, 0); // SCHED_FIFO
        }

        status = headless ? compositor_headless(headless, file) : compositor_card_open();
        if (status < 0)
        {
                compositor_say("cannot take the display, error", status);
                return 1;
        }

        if (!headless)
                compositor_cursor_start();
        compositor_inputs_open();
        if (extra >= 0 && compositor.input_count < COMPOSITOR_INPUTS)
        {
                // The -i device is no keyboard the compositor took: the headless fifo is not a physical one. It is a mouse's wheel and buttons, which
                // is what a driver writes to it, so its wheel is stepped as a mouse's is (compositor_wheel_streams); with -T it is a tablet's, and its
                // wheel is a stream when its notches come close together.
                compositor.input_kinds[compositor.input_count] = (tablet ? 0 : WINDOWS_INPUT_MOUSE) | WINDOWS_INPUT_WHEEL | WINDOWS_INPUT_BUTTONS;
                compositor.inputs[compositor.input_count] = extra;
                compositor.device_number[compositor.input_count++] = COMPOSITOR_DEVICES - 1;
        }
        // The middle of the primary screen.
        {
                int x, y, width, height;

                compositor_output_rect(0, &x, &y, &width, &height);
                compositor.real_x = compositor.kept_x = x + width / 2;
                compositor.real_y = compositor.kept_y = y + height / 2;
                compositor.owner = COMPOSITOR_PREDICT_MOUSE;
                compositor_pointer_shown();
        }
        compositor_look_read();
        compositor_input_settings_read();
        compositor_cursor_move();
        compositor_font(font);

        compositor.signals = compositor_signals();
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
        compositor.on_screen = compositor.fb;
        status = compositor_table();
        if (status < 0 || compositor.signals < 0)
        {
                compositor_say("cannot make the window table, error", status < 0 ? status : compositor.signals);
                if (!headless)
                        compositor_hand_back();
                return 1;
        }
        // The doors' threads, which do every read and write of a FIFO the loop would otherwise wait on (compositor_doors).
        compositor_doors_start(compositor_clip.door);
        if (headless)
                compositor_headless_scale();
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_start();
#endif
        // The theme goes in the table it now has, and what a hand takes (read before there was a table) with it.
        compositor_theme_read();
        compositor_hand_read();
        // The keyboard's setting too (compositor_osk_sync writes the table).
        compositor_osk_setting_read();
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++) // every slot: one no device is in is said to be none (-1)
                compositor_input_publish(at);
        compositor_up_say(up);

        string_format(log, "compositor: %b by %b (scale %b), %p input devices, pointer %s, windows in %s\n",
                      (bipolar)compositor.width, (bipolar)compositor.height,
                      (bipolar)compositor.scale, (positive)compositor.input_count,
                      compositor.cursor.soft ? "drawn" : compositor.cursor.handle[0] ? "on the plane" : "none",
                      WINDOWS_TABLE);
        compositor_screens_say();

        compositor.asleep = compositor_slept_clock();

        while (running)
        {
                struct compositor_poll *wanted = compositor_polls(); // every part of it is counted where it is added (COMPOSITOR_POLLS_FOR)
                struct compositor_slots slots;
                static unsigned int turns;
                unsigned int count, inputs, doors, card_at, fence_at, uevents_at, clip_at, wayland_at, done_at;
                positive now = 0, until = 0;
                b32 idle_turn = 0, held;
                long awaited, wait[2];

                // The look at the screens the first take owes (compositor_space_first), once the loop has made its first turn and
                // drawn its first frame: on the loop's thread, as every take of the screens is, so nothing else has the card (nor a
                // present's job: the take waits for it).
                if (compositor.probe_owed && turns++ && !compositor_card.on && !rows_any(&compositor_card.list_rows))
                {
                        compositor.probe_owed = 0;
                        compositor_space_retake();
                }
                // The clients wait while a present's jobs read their pictures, GPU_LATE_NS at the most (compositor_card_clients_held).
                held = compositor_card_clients_held(clock_monotonic_nanoseconds());
                // No memory for the list this pass: the pass is made again a moment later, when the kernel may have it.
                if (!wanted)
                {
                        positive nap[2] = {0, 10000000};

                        compositor_call(syscall(nanosleep), (positive)nap, 0, 0, 0, 0, 0);
                        continue;
                }
                compositor_loop_wanted(wanted, &slots, held);
                count = slots.count, inputs = slots.inputs, doors = slots.doors, done_at = slots.done_at, card_at = slots.card_at, fence_at = slots.fence_at;
                uevents_at = slots.uevents_at, clip_at = slots.clip_at, wayland_at = slots.wayland_at;
                // A part that adds an entry and not its count (COMPOSITOR_POLLS) has written past the list: said and ended at once, not polled.
                if (count > COMPOSITOR_POLLS_FOR(compositor_rows_in_use))
                {
                        compositor_say("the poll list ran past its count, entries", (bipolar)count);
                        break;
                }

                // With nothing to show or time the wait is for ever. With
                // something to show it is until a frame has gone by since the
                // last one went out, so a program that draws faster than the
                // card can scan costs one composition a frame and not one a
                // picture. The time in the tray is drawn at the next minute.
                now = clock_monotonic_nanoseconds();
                if (compositor.wake_stage)
                        until = !until || compositor.wake_at < until ? compositor.wake_at : until;

                if (compositor.hold >= 0)
                        until = !until || compositor.hold_until < until ? compositor.hold_until : until;

                if (compositor.cursor.magnified)
                        until = !until || compositor.cursor.magnified_until < until ? compositor.cursor.magnified_until
                                                                                     : until;

                // The pointer's own times: the next frame of a held button's change of size or of the dynamic shape's, and its stretch letting go.
                {
                        positive due = compositor_cursor_due();

                        if (due)
                                until = !until || due < until ? due : until;
                }

                // The sound server's volume, looked at for the tray's cell, while there is a band to show it on.
                if (compositor.band)
                {
                        positive due = compositor_volume.look_at ? compositor_volume.look_at : 1; // 1: not yet looked, so now

                        until = !until || due < until ? due : until;
                }

                // A lead is cleared a frame after its newest place, so the loop wakes for it.
                if (compositor_predict_due())
                        until = !until || compositor_predict_due() < until ? compositor_predict_due() : until;

                // A finger held still is the right button when its time is up; a finger's mark fades a frame at a time.
                // Neither wakes the loop while no finger is down and no mark is fading.
                if (compositor_touch_due())
                        until = !until || compositor_touch_due() < until ? compositor_touch_due() : until;
                // A drag's pointer resting in the top band brings the grid up when its time is up, with no motion.
                if (compositor_snap_due())
                        until = !until || compositor_snap_due() < until ? compositor_snap_due() : until;
                if (compositor_spots_any(1))
                {
                        positive period = compositor.period ? compositor.period : 16666667ull;
                        positive frame = compositor.presented + period;

                        frame = frame > now ? frame : now;
                        until = !until || frame < until ? frame : until;
                }

                // The wallpaper: one held back for the first frame, or one made on its thread with no knock to say it is done.
                if (compositor_wall_due(now))
                        until = !until || compositor_wall_due(now) < until ? compositor_wall_due(now) : until;
                // The moon's next step of its turn, while it turns.
                if (compositor.band && taskbar_moon_due(now))
                        until = !until || taskbar_moon_due(now) < until ? taskbar_moon_due(now) : until;
                if (compositor.band || compositor.theme_word == 2)
                {
                        timespec real = {0, 0};
                        positive minute;

                        clock_gettime(CLOCK_REALTIME, address_of real);
                        minute = now + (positive)(59 - real.tv_sec % 60) * 1000000000ull +
                                 (positive)(1000000000l - real.tv_nsec) + 1000000ull;
                        until = !until || minute < until ? minute : until;
                }

#ifdef CONFIG_MOONWATER_WAYLAND
                // A client that asked for a frame is told of one a frame's time after the last, whether or not anything was drawn (a present's jobs
                // on the card answer it when they are done).
                if (compositor_wl.started && wayland_frame_wanted() && !compositor.damages && !compositor_card.on)
                {
                        positive period = compositor.period ? compositor.period : 16666667ull;
                        positive frame = now - compositor.presented < period ? compositor.presented + period : now;

                        until = !until || frame < until ? frame : until;
                }
                // A copy or a paste that stalls is ended when its time is up.
                {
                        positive wl_due = compositor_wl.started ? compositor_wl_due() : 0;

                        if (wl_due)
                                until = !until || wl_due < until ? wl_due : until;
                }
#endif
                if (compositor.flip_pending && compositor_flip_lost(now))
                {
                        compositor_flip_done();
                        compositor_damage_space();
                }
                // No screen is connected: the connectors are asked again in a while, for a uevent that was lost and a connector that says none.
                if (compositor.screens_gone)
                {
                        if (!compositor.screens_look)
                                compositor.screens_look = now + COMPOSITOR_SCREEN_LOOK;
                        until = !until || compositor.screens_look < until ? compositor.screens_look : until;
                }
                // While the card holds the present (its jobs, or a job given up on still running) its fence wakes the loop, and the card's own
                // times below: the present is not due before.
                if (compositor.damages && !compositor_card_holds())
                {
                        positive frame, waiting = compositor_flip_wait(now);

                        // The flip's event wakes the loop; else the present is made when it is due (compositor_present_due).
                        frame = waiting ? waiting
                                        : compositor_present_due(compositor.presented, compositor.period, compositor.flip_when,
                                                                 compositor_present_lead(), now);
                        until = !until || frame < until ? frame : until;
                }
#ifdef MOONWATER_GPU_C
                // A job the loop waits on: its deadline, the next look for its fault, a millisecond with no sync_file to poll; and the clients' hold ends
                // GPU_LATE_NS after the present began, whether or not the job has.
                if (fence_at)
                {
                        positive due = compositor_card_due(now);

                        until = !until || due < until ? due : until;
                }
                if (held)
                {
                        positive due = compositor_card_held_until();

                        until = !until || due < until ? due : until;
                }
#endif
#ifdef MOONWATER_GPU_C
                // The GPU's late probe: a card that is not there is looked for again when its time comes. Nothing is waited for while the card is up or off.
                {
                        positive frame = compositor_gpu_late_at(now);

                        if (frame)
                                until = !until || frame < until ? frame : until;
                }
                {
                        positive frame = compositor_gpu_clocks_at();

                        if (frame)
                                until = !until || frame < until ? frame : until;
                }
#endif

                // A timespec: a wait past a second in its nanoseconds is EINVAL, and the loop would spin on it.
                wait[0] = until > now ? (long)((until - now) / 1000000000ull) : 0;
                wait[1] = until > now ? (long)((until - now) % 1000000000ull) : 0;
                idle_turn = until && until <= now;
                awaited = compositor_call(syscall(ppoll), (positive)wanted, count,
                                          until ? (positive)wait : 0, 0, 0, 0);
                now = clock_monotonic_nanoseconds();
                compositor_loop_tick(now, idle_turn);

                // Whatever woke the loop, a sleep may be what it woke from; and auto's theme may have turned with the clock.
                compositor_theme_tick();
                compositor_wake_check(now);
                compositor_wake_pass(now);
                compositor_hold_drop(now);
                compositor_game_watch();
#ifdef MOONWATER_GPU_C
                compositor_gpu_late_tick(now);
                compositor_gpu_clocks_tick(now);
#endif

                if (awaited < 0)
                        continue;

                for (unsigned int at = 0; awaited > 0 && at < count; at++)
                        if (wanted[at].returned)
                                compositor_loop_by[compositor_loop_source(at, doors, done_at, card_at, fence_at, uevents_at, clip_at, wayland_at)]++;

                // A signal: /root again for SIGUSR1, the wake for SIGUSR2, and the end for any other (and for a descriptor that cannot be read).
                if (wanted[0].returned)
                {
                        unsigned int taken = compositor_signal_taken();

                        if (taken == COMPOSITOR_SIGUSR1)
                                compositor_root_again();
                        else if (taken == COMPOSITOR_SIGUSR2)
                                compositor_woke(now, COMPOSITOR_WAKE_SIGNAL);
                        else
                                running = 0;
                }

                // The devices first, and what they said forwarded before anything else is done with the turn (compositor_inputs_turn):
                // a settings file read again, a door's picture taken, a client's commit and the frame all come after it.
                compositor_inputs_turn(wanted + inputs);
#ifdef CONFIG_MOONWATER_WAYLAND
                // A Wayland client's share of that is written to its socket now (the enter, the keys, the motion: compositor_wl_wake
                // sends what the shim queued), not after the doors, the other clients' commits and the ticks below: a commit that
                // copies a picture takes milliseconds, and sat in front of every client's input of the same turn.
                for (unsigned int at = 0; at < compositor.input_count; at++)
                        if (wanted[inputs + at].returned)
                        {
                                compositor_wl_wake();
                                break;
                        }
#endif
#ifdef MOONWATER_GPU_C
                // And once it is, the card is kept awake for what answers it (compositor_gpu_clocks).
                compositor_gpu_input_turn(now);
#endif

                // The settings' watch after the devices: it may open and close them, and the answers above are the list's as it was polled.
                if (wanted[1].returned)
                        compositor_inputs_watch();

                // A list's job submitted without a wait leaves the doors after it for a later turn, read from the first of them then (the card holds one job of
                // a list at a time, and the next list would be refused for want of it).
                {
                        b32 listing = rows_any(&compositor_card.list_rows);

                        for (unsigned int k = 0, row; k < compositor_rows_in_use; k++)
                        {
                                row = (compositor_card.door_from + k) % compositor_rows_in_use;
                                if (!wanted[doors + row].returned)
                                        continue;
                                if (compositor_card_door_waits(listing))
                                {
                                        compositor_card.door_from = row;
                                        break;
                                }
                                compositor_door((int)row, wanted[doors + row].returned);
                        }
                }
                if (done_at && wanted[done_at].returned)
                        compositor_doors_done();

#ifdef CONFIG_MOONWATER_WAYLAND
                if (wayland_at && compositor_wl.started)
                        compositor_wl_pass(wanted + wayland_at);
#endif
                if (card_at && wanted[card_at].returned)
                        compositor_card_read();
#ifdef MOONWATER_GPU_C
                // The card's jobs: the next of a present submitted, its flip committed, its end once they are done, or the hang path (compositor_card_tick).
                // Asked every turn one is awaited, the sync_file readable or not: a deadline is the clock's, and a fence's error is a done answer.
                if (fence_at)
                        compositor_card_tick();
#endif
                if (clip_at && wanted[clip_at].returned)
                        compositor_clip_cli();

                // A screen came or went, or the screens are taken now that a flip that held them back has landed (and a present's jobs are done).
                if (uevents_at && wanted[uevents_at].returned)
                        compositor_uevents_read();
                if (compositor.space_wanted && !compositor.flip_pending && !compositor_card.on && !rows_any(&compositor_card.list_rows))
                        compositor_space_retake();
                if (compositor.screens_gone && compositor.screens_look && now >= compositor.screens_look && !compositor_card.on && !rows_any(&compositor_card.list_rows))
                {
                        compositor.screens_look = 0;
                        compositor_space_retake();
                }

                // A pointer that was shaken is its own size again when its time is up.
                if (compositor.cursor.magnified && now >= compositor.cursor.magnified_until)
                {
                        compositor.cursor.magnified = 0;
                        compositor_cursor_move();
                }

                // The pointer's values on their way are drawn where they are now, and a stretch nothing moves under lets go (compositor_cursor_tick).
                compositor_cursor_tick(now);

                // The shape the window under a resting pointer publishes is shown (compositor_shape_sync).
                compositor_shape_sync();

                if (compositor_predict_due())
                        compositor_predict_expire(now);

                // A release a full ring dropped is owed to its window until it lands.
                compositor_owed_pass();

                // A finger held long enough, and the marks that fade.
                if (compositor_touch_due())
                        compositor_touch_tick(now);
                if (compositor_snap_due())
                        compositor_snap_tick(now);
                if (compositor_spots_any(1))
                        compositor_spots_tick(now);

                if (compositor_notices_look() && compositor.band)
                        compositor_tabs_changed();
                if (compositor.band && taskbar_tick())
                        compositor_tabs_changed();
                if (compositor.band && now >= compositor_volume.look_at)
                        compositor_volume_look(now);

#ifdef CONFIG_MOONWATER_WAYLAND
                // The windows' clients are told of what changed, before the frame is drawn: no event waits for it.
                compositor_wl_wake();
#endif

                // The taskbar's place for this frame, before the frame is drawn: a slide is one step a pass.
                compositor_taskbar_slide(now);
                compositor_moon_keep(now);
                // The wallpaper: one asked for, or one made on its thread taken.
                compositor_wall_keep();

                // Dated when it starts: a card that holds the call for the rest
                // of the frame has used the frame up.
                if (compositor.damages && !compositor.flip_pending && !compositor_card_holds())
                {
                        positive stamp = clock_monotonic_nanoseconds(), lead = compositor_present_lead();

                        // Due by the rule the wait above was made with, so the loop never wakes for a present it then does not make.
                        if (stamp >= compositor_present_due(compositor.presented, compositor.period, compositor.flip_when, lead, stamp) &&
                            !compositor_flip_wait(stamp))
                        {
                                positive after_present;

                                // A present late in its period is paced by the card's vblank and is dated as made; any other when it was due.
                                compositor_late_begin(lead, stamp);
                                compositor.presented = compositor_late.aim ? stamp : compositor_frame_dated(compositor.presented, compositor.period, stamp);
                                compositor_card.looped = 1; // the loop's present: its jobs may go without a wait (compositor_card)
#ifdef CONFIG_MOONWATER_WAYLAND
                                // What the clients committed is this frame's; what they commit from here is the next one's (wayland_latch).
                                compositor_wl_latch();
#endif
                                compositor_present();
                                compositor_card.looped = 0;
                                after_present = clock_monotonic_nanoseconds();
#ifdef CONFIG_MOONWATER_WAYLAND
                                // The frame is on its way to the screen: the presentation feedbacks its commits took are answered at its flip. A
                                // frame the card still draws is answered when its jobs are done (compositor_card_done).
                                if (!compositor_card.on)
                                        compositor_wl_presented();
#endif
                                compositor_land_keep();
                                compositor_glide_keep();
                                compositor_taskbar_keep(after_present);
                                compositor_preview_keep();
                                compositor_overlays_keep();
                                if (compositor.motion_ts)
                                {
                                        compositor_latency(1, compositor.motion_ts, after_present);
                                        compositor.motion_ts = 0;
                                }
#ifdef CONFIG_MOONWATER_WAYLAND
                                if (!compositor_card.on)
                                        compositor_wl_frame();
#endif
                        }
                }
#ifdef CONFIG_MOONWATER_WAYLAND
                // Callbacks or feedbacks that waited for a frame and no frame was drawn for: the frame's time has gone by all the same. Not while
                // a present's jobs are on the card: its end answers them.
                else if (!compositor.damages && compositor_wl.started && wayland_frame_wanted() && !compositor_card.on)
                {
                        positive stamp = clock_monotonic_nanoseconds();
                        positive period = compositor.period ? compositor.period : 16666667ull;

                        if (stamp - compositor.presented >= period)
                        {
                                compositor_wl_idle();
                                if (!compositor.damages)
                                        compositor.presented = compositor_frame_dated(compositor.presented, period, stamp);
                        }
                }
#endif

                // One knock a window a pass, however much it was given.
                compositor_knock();
                // Rows made while few are free (compositor_rows_spare), so the next window finds one.
                compositor_rows_spare();
        }

#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_stop();
#endif
        compositor_table_drop();
        string_format(log, "compositor: %p pixels composed, the plane was given %p shapes and %p moves and refused %p times\n",
                      (positive)compositor.painted, (positive)compositor.cursor.paints, (positive)compositor.cursor.moves,
                      (positive)compositor.cursor.refused);
        string_format(log, "compositor: the processor was held %p times\n", (positive)(compositor.holds == ~0ul ? 0 : compositor.holds));
        if (!compositor_late.early)
                string_format(log, "compositor: %p presents were made late in their period, and %p of them missed the vblank they were for\n",
                              compositor_late.made, compositor_late.missed);
        string_format(log, "compositor: glass blurred again %p pixels and composed again %p\n", compositor.blurred,
                      compositor.recomposed);
        for (unsigned int which = 0; which < 3; which++)
                string_format(log, "compositor: %s: %p, on average %p us, worst %p us\n",
                              which == 0 ? "pointer to the plane" : which == 1 ? "pointer to the frame" : "key to the window",
                              (positive)compositor.latency[which].count,
                              (positive)(compositor.latency[which].count
                                             ? compositor.latency[which].total / compositor.latency[which].count / 1000
                                             : 0),
                              (positive)(compositor.latency[which].worst / 1000));
        log_flush();
        if (!headless)
                compositor_hand_back();
        return 0;
}

// surface ----------------------------------------------------------
/*
        A test client: surface [-x X] [-y Y] [-w WIDTH] [-h HEIGHT] [-c RRGGBB]
        [-T TITLE] [-N NAME] [-p] [-m] [-R] [-n] [-G] [-a ALPHA] [-F PICTURES]
        [-f FONT.ttf] [-t TEXT] [-s SECONDS].

        A window of one colour at X, Y, a line of TEXT drawn with font.c on it
        when FONT is given, and a line on standard output for everything it is
        told: its row and where it is, each key, button and wheel, the keys
        coming and going, a close asked for, and with -p the pointer's
        motions. -m puts it away as it opens, -R draws the size it is asked
        for, -n never shows a picture, and -F shows PICTURES more, as fast as
        it can. -G makes it glass, its colour laid over the glass at ALPHA
        (0, the default, is the glass alone, 255 the colour alone). -N is the
        name the compositor remembers where it was put by; without one it is
        not remembered. It lives until it is asked to close, the compositor
        goes, or SECONDS.
*/
static void surface_draw(const struct window_row *own, unsigned int which, unsigned int width, unsigned int height,
                         unsigned int colour, string_address font, string_address text)
{
        unsigned int *pixels = windows_pixels(own, which);

        memory_fill_u32_rect(pixels, own->stride, width, height, colour);

        // Glass has no ground to draw text on.
        if (font && !own->table->glass[own->row])
        {
                positive size = 0;
                positive bytes = compositor_map_file(font, address_of size);
                positive atlas_at = (sizeof(font_engine) + 4095) & ~(positive)4095;
                long engine = compositor_call(syscall(mmap), 0, atlas_at + 256 * 256, 3, 0x22, (positive)-1, 0);
                font_face face;
                const font_face address_to faces[1] = {&face};
                font_target target = {pixels, own->stride, (b32)width, (b32)height};
                p32 em = 16 << 6;

                if (bytes && !(engine < 0 && engine > -4096) && font_load(&face, (const p8 address_to)bytes, size, 0) == 0)
                {
                        font_start((font_engine address_to)engine, (p8 address_to)(engine + atlas_at), 256, 256);
                        string_format(log, "surface: text drawn %b px wide\n",
                                      (bipolar)(font_draw((font_engine address_to)engine, faces, 1,
                                                          (const p8 address_to)text, string_length(text), em, 0,
                                                          &target, 8 << 16, 8 + font_metrics_at(&face, em).baseline,
                                                          0xffffff, colour) >>
                                                16));
                }
                else
                        log_direct(str("surface: no font\n"));
                if (!(engine < 0 && engine > -4096))
                        compositor_call(syscall(munmap), (positive)engine, atlas_at + 256 * 256, 0, 0, 0, 0);
                if (bytes)
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }

        own->table->width[own->row] = width;
        own->table->height[own->row] = height;
        own->table->damage_width[own->row] = 0;
}

static b32 surface_main()
{
        bipolar x = 0, y = 0, width = 320, height = 200, colour = 0x3366aa, seconds = -1, flood = 0, alpha = 0;
        b32 pointer = 0, never = 0, away = 0, sized = 0, focused = 0, glass = 0, placed = 0;
        string_address title = null, font = null, text = (string_address) "Moonwater", name = null;
        struct window_row own;
        struct windows *table;
        unsigned int which = 0;
        positive until;
        long taken;

        claim_standard_descriptors();

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2])
                        continue;
                if (option[1] == 'p' || option[1] == 'n' || option[1] == 'm' || option[1] == 'R' || option[1] == 'G')
                {
                        glass |= option[1] == 'G';
                        pointer |= option[1] == 'p';
                        sized |= option[1] == 'R';
                        never |= option[1] == 'n';
                        away |= option[1] == 'm';
                        continue;
                }
                if (!value)
                        break;
                at++;
                switch (option[1])
                {
                case 'x': x = compositor_number(value, 0), placed = 1; break;
                case 'y': y = compositor_number(value, 0), placed = 1; break;
                case 'w': width = compositor_number(value, 0); break;
                case 'h': height = compositor_number(value, 0); break;
                case 'c': colour = compositor_number(value, 1); break;
                case 's': seconds = compositor_number(value, 0); break;
                case 'F': flood = compositor_number(value, 0); break;
                case 'a': alpha = compositor_number(value, 0); break;
                case 'T': title = value; break;
                case 'N': name = value; break;
                case 'f': font = value; break;
                case 't': text = value; break;
                }
        }

        taken = windows_take(&own);
        if (taken < 0)
        {
                string_format(log, "surface: no window, error %b\n", (bipolar)taken);
                log_flush();
                return 1;
        }

        table = own.table;
        width = width < 1 ? 1 : width > (bipolar)table->output_width ? (bipolar)table->output_width : width;
        height = height < 1 ? 1 : height > (bipolar)table->output_height ? (bipolar)table->output_height : height;
        colour &= 0xffffff;
        if (glass)
        {
                // Premultiplied: each colour scaled by the alpha, which is the top byte.
                alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                colour = (bipolar)window_mix(0, (unsigned int)colour, (unsigned int)(alpha + (alpha >> 7))) |
                         (bipolar)((unsigned int)alpha << 24);
                table->glass[own.row] = WINDOW_GLASS;
        }
        table->x[own.row] = (int)x;
        table->y[own.row] = (int)y;
        // A place given (-x, -y) is the window's own; with none the compositor centres it.
        if (!placed)
                windows_place_free(table, own.row);
        table->state[own.row] = away ? WINDOW_MINIMIZED : 0;
        {
                positive at = 0;

                for (; title && title[at] && at < WINDOWS_TITLE - 1; at++)
                        table->title[own.row][at] = (char)title[at];
                table->title[own.row][at] = 0; // a row is reused: what it held past this title is not the title's
        }
        windows_name(&own, (const char *)name);
        surface_draw(&own, which, (unsigned int)width, (unsigned int)height, (unsigned int)colour, font, text);

        if (!never)
        {
                windows_show(&own, which);
                for (bipolar at = 0; at < flood; at++)
                        windows_show(&own, which);

                // Where it was put is the compositor's to say once it has taken the picture.
                until = clock_monotonic_nanoseconds() + 2000000000ull;
                while (!windows_ready(&own) && clock_monotonic_nanoseconds() < until)
                        windows_wait(&own, -1, 50000000);
        }

        string_format(log, "surface: %p at %b,%b\n", (positive)own.row, (bipolar)table->x[own.row],
                      (bipolar)table->y[own.row]);
        log_flush();

        until = seconds >= 0 ? clock_monotonic_nanoseconds() + (positive)seconds * 1000000000ull : 0;

        for (;;)
        {
                struct window_event event;
                long timeout = -1;
                b32 has = table->focus == own.row;

                if (has != focused)
                {
                        focused = has;
                        string_format(log, "surface: focus %b\n", (bipolar)focused);
                }

                // The size asked for, drawn once the last picture is taken, as a terminal does.
                if (sized && table->want_width[own.row] && table->want_height[own.row] &&
                    (table->want_width[own.row] != (unsigned int)width || table->want_height[own.row] != (unsigned int)height) &&
                    windows_ready(&own))
                {
                        width = table->want_width[own.row] > table->output_width ? table->output_width : table->want_width[own.row];
                        height = table->want_height[own.row] > table->output_height ? table->output_height
                                                                                    : table->want_height[own.row];
                        which ^= 1;
                        surface_draw(&own, which, (unsigned int)width, (unsigned int)height, (unsigned int)colour, null, text);
                        windows_show(&own, which);
                        string_format(log, "surface: size %b %b\n", width, height);
                }
                log_flush();

                if (table->state[own.row] & WINDOW_CLOSING)
                {
                        log_direct(str("surface: close\n"));
                        break;
                }

                if (until)
                {
                        positive now = clock_monotonic_nanoseconds();

                        if (now >= until)
                                break;
                        timeout = (long)(until - now);
                }

                if (windows_wait(&own, -1, timeout) < 0)
                {
                        log_direct(str("surface: the compositor has gone\n"));
                        break;
                }

                while (windows_event(&own, &event))
                        if (event.type == WINDOW_EVENT_KEY)
                                string_format(log, "surface: key %b %b %p\n", (bipolar)event.a, (bipolar)event.b,
                                              (positive)event.flags);
                        else if (event.type == WINDOW_EVENT_BUTTON)
                                string_format(log, "surface: button %b %p\n", (bipolar)event.a, (positive)event.flags);
                        else if (event.type == WINDOW_EVENT_WHEEL)
                                string_format(log, "surface: wheel %b %b\n", (bipolar)event.a, (bipolar)event.b);
                        else if (pointer)
                                string_format(log, "surface: motion %b %b\n", (bipolar)event.a, (bipolar)event.b);
        }

        log_flush();
        windows_leave(&own);
        return 0;
}

#endif // MOONWATER_CANVAS_COMPOSITOR
