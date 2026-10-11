/*
        The compositor in userspace, and the smallest program that draws on it.

        `compositor` takes the display from Canvas the way any program can:
        it opens the card, becomes its master, puts a buffer of its own on
        the screen at the mode Canvas left, and reads the keyboard and the
        pointer through evdev. While it holds the card Canvas draws nothing
        and keeps the console keyboard muted, and its own input handler
        still hears Control-Alt-Backspace, which ends this program's session
        if it ever stops answering. When it exits, the card's last file
        closes and Canvas's restore hook redraws the kernel desktop at once.

        What it draws is the classic look of the kernel's compose path, pixel
        for pixel -- the desktop ink, and around each surface the border, the
        titlebar and the close box -- so the golden scene G-S1 hashes the
        same from either (test/checks.c). Surfaces are protocol version 2
        (canvas_abi.h, window.c): a program connects to SURFACE_SOCKET, hands
        over a sealed page, and commits buffers into it. Each surface is an
        ordinary application window in M1: it stacks, the press of a button
        raises and focuses it, its titlebar drags it, its close box asks it
        to go, keys reach it while it has focus and the pointer reaches it
        when it asks for the pointer. Damage is the union of what the
        programs say changed, composed in RAM and copied to the scanout only
        where it changed.

        `surface` is a test client: a window of one colour, a line of text
        drawn with font.c when it is given a TrueType file, and a report on
        standard output of every event it is sent.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_COMPOSITOR
#define MOONWATER_CANVAS_COMPOSITOR

/* ---- backend: the classic look on the CPU ---- */

// The classic palette (canvas spec 03 section 4), today's kernel inks.
#define COMPOSITOR_INK_DESKTOP 0x1b2733u
#define COMPOSITOR_INK_FRAME 0x2f3f52u
#define COMPOSITOR_INK_TITLE 0x2b3a4cu
#define COMPOSITOR_INK_TITLE_LIT 0x4c6785u
#define COMPOSITOR_INK_TEXT 0xdfe7efu

// The metrics the kernel draws with, in logical pixels.
#define COMPOSITOR_TITLE 20
#define COMPOSITOR_BORDER 2
#define COMPOSITOR_CELL 8

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch; // pixels a row
        int width, height;
        int x1, y1, x2, y2;  // the damage: nothing outside it is written
};

struct compositor_frame
{
        int x, y;            // the titlebar's top left, as version 1 has it
        int width, height;   // the contents
        int scale;
        int framed, focused, closable;
        const unsigned int *pixels;
        unsigned long pitch;
};

// The surface's whole rectangle: border, titlebar, contents, border.
static void compositor_outline(const struct compositor_frame *frame, int *x, int *y,
                               int *width, int *height)
{
        int border = frame->framed ? COMPOSITOR_BORDER * frame->scale : 0;
        int title = frame->framed ? COMPOSITOR_TITLE * frame->scale : 0;

        *x = frame->x - border;
        *y = frame->y - border;
        *width = frame->width + border * 2;
        *height = frame->height + title + border * 3;
}

/*
        The close box, for drawing and for the press alike: one square in the
        titlebar's right end, inset by the border, and none on a window too
        narrow to hold it and two cells of titlebar beside it.
*/
static int compositor_close_box(const struct compositor_frame *frame, int *x, int *y,
                                int *side)
{
        int border = COMPOSITOR_BORDER * frame->scale;
        int box = COMPOSITOR_TITLE * frame->scale - border * 2;

        if (!frame->framed || !frame->closable || box <= 0 ||
            frame->width < box + COMPOSITOR_CELL * frame->scale * 2)
                return 0;

        *side = box;
        *x = frame->x + frame->width - box - border;
        *y = frame->y + border;
        return 1;
}

static void compositor_fill(const struct compositor_target *t, int x, int y, int width,
                            int height, unsigned int colour)
{
        int x2 = x + width, y2 = y + height;

        x = x > t->x1 ? x : t->x1;
        y = y > t->y1 ? y : t->y1;
        x2 = x2 < t->x2 ? x2 : t->x2;
        y2 = y2 < t->y2 ? y2 : t->y2;

        if (x2 <= x || y2 <= y)
                return;

        memory_fill_u32_rect(t->pixels + (unsigned long)y * t->pitch + (unsigned long)x,
                             t->pitch, (unsigned long)(x2 - x), (unsigned long)(y2 - y),
                             colour);
}

static void compositor_blit(const struct compositor_target *t, int x, int y, int width,
                            int height, const unsigned int *source, unsigned long pitch)
{
        int x1 = x > t->x1 ? x : t->x1;
        int y1 = y > t->y1 ? y : t->y1;
        int x2 = x + width < t->x2 ? x + width : t->x2;
        int y2 = y + height < t->y2 ? y + height : t->y2;

        if (x2 <= x1)
                return;

        for (int row = y1; row < y2; row++)
                memory_copy(t->pixels + (unsigned long)row * t->pitch + (unsigned long)x1,
                            source + (unsigned long)(row - y) * pitch + (unsigned long)(x1 - x),
                            (unsigned long)(x2 - x1) * 4);
}

// The X, two pixels thick so it reads at scale one: the kernel's bitmap.
static const unsigned char compositor_close_bits[8] = {
    0xc3, 0xe7, 0x7e, 0x3c, 0x3c, 0x7e, 0xe7, 0xc3,
};

static void compositor_bits(const struct compositor_target *t, int x, int y, int scale,
                            const unsigned char *bits, unsigned int rows, unsigned int colour)
{
        // A glyph the damage does not touch is not walked.
        if (x >= t->x2 || x + 8 * scale <= t->x1 || y >= t->y2 || y + (int)rows * scale <= t->y1)
                return;

        // One fill per run of lit columns in a row, not one per lit pixel.
        for (unsigned int row = 0; row < rows; row++)
                for (unsigned int column = 0; column < 8;)
                {
                        unsigned int run = 0;

                        if (!(bits[row] & (0x80u >> column)))
                        {
                                column++;
                                continue;
                        }

                        while (column + run < 8 && (bits[row] & (0x80u >> (column + run))))
                                run++;
                        compositor_fill(t, x + (int)column * scale, y + (int)row * scale,
                                        (int)run * scale, scale, colour);
                        column += run;
                }
}

static void compositor_draw(const struct compositor_target *t,
                            const struct compositor_frame *frame)
{
        int title = frame->framed ? COMPOSITOR_TITLE * frame->scale : 0;

        if (frame->framed)
        {
                int fx, fy, fw, fh, cx, cy, side;
                int bottom = frame->y + title + frame->height;
                int span = title + frame->height;

                compositor_outline(frame, &fx, &fy, &fw, &fh);
                compositor_fill(t, fx, fy, fw, frame->y - fy, COMPOSITOR_INK_FRAME);
                compositor_fill(t, fx, bottom, fw, fy + fh - bottom, COMPOSITOR_INK_FRAME);
                compositor_fill(t, fx, frame->y, frame->x - fx, span, COMPOSITOR_INK_FRAME);
                compositor_fill(t, frame->x + frame->width, frame->y,
                                fx + fw - (frame->x + frame->width), span, COMPOSITOR_INK_FRAME);
                compositor_fill(t, frame->x, frame->y, frame->width, title,
                                frame->focused ? COMPOSITOR_INK_TITLE_LIT : COMPOSITOR_INK_TITLE);

                if (compositor_close_box(frame, &cx, &cy, &side))
                        compositor_bits(t, cx + (side - COMPOSITOR_CELL * frame->scale) / 2,
                                        cy + (side - COMPOSITOR_CELL * frame->scale) / 2,
                                        frame->scale, compositor_close_bits, 8,
                                        COMPOSITOR_INK_TEXT);
        }

        compositor_blit(t, frame->x, frame->y + title, frame->width, frame->height,
                        frame->pixels, frame->pitch);
}

#define COMPOSITOR_CLIENTS 32

/*
        The scene: inside the target's damage, each surface where nothing
        above it covers it and the desktop where nothing is, every pixel
        written once and none read. The frames are bottom first.

        The damage is cut into bands at the top and bottom edge of every
        surface, so in one band each surface covers the same columns from
        the band's top to its bottom. The columns are given out from the top
        surface down: each takes what it covers of the spans still unclaimed
        and leaves the rest, which are never more than one more than there
        are surfaces. What no surface claims is desktop. The target is
        drawn on through a clip of the cell, so the picture is the one the
        surfaces drawn bottom to top over a desktop would be.
*/
static void compositor_scene(const struct compositor_target *t,
                             const struct compositor_frame *frames, unsigned int count)
{
        int edge[2 * COMPOSITOR_CLIENTS + 2];
        int box[COMPOSITOR_CLIENTS][4];
        unsigned int edges = 2;

        edge[0] = t->y1;
        edge[1] = t->y2;

        for (unsigned int at = 0; at < count; at++)
        {
                int width, height;

                compositor_outline(&frames[at], &box[at][0], &box[at][1], &width, &height);
                box[at][2] = box[at][0] + width;
                box[at][3] = box[at][1] + height;

                for (unsigned int side = 1; side < 4; side += 2)
                        if (box[at][side] > t->y1 && box[at][side] < t->y2)
                        {
                                unsigned int place = edges++;

                                for (; place && edge[place - 1] > box[at][side]; place--)
                                        edge[place] = edge[place - 1];
                                edge[place] = box[at][side];
                        }
        }

        for (unsigned int band = 0; band + 1 < edges; band++)
        {
                int top = edge[band], bottom = edge[band + 1];
                int span[COMPOSITOR_CLIENTS + 1][2];
                unsigned int spans = 1;

                if (bottom <= top)
                        continue;

                span[0][0] = t->x1;
                span[0][1] = t->x2;

                for (unsigned int at = count; at-- && spans;)
                {
                        if (box[at][1] >= bottom || box[at][3] <= top)
                                continue;

                        for (unsigned int s = 0; s < spans; s++)
                        {
                                int low = span[s][0], high = span[s][1];
                                int x1 = low > box[at][0] ? low : box[at][0];
                                int x2 = high < box[at][2] ? high : box[at][2];
                                struct compositor_target cell = *t;

                                if (x2 <= x1)
                                        continue;

                                cell.x1 = x1;
                                cell.y1 = top;
                                cell.x2 = x2;
                                cell.y2 = bottom;
                                compositor_draw(&cell, &frames[at]);

                                if (low < x1 && x2 < high)
                                {
                                        span[s][1] = x1;
                                        span[spans][0] = x2;
                                        span[spans++][1] = high;
                                }
                                else if (low < x1)
                                        span[s][1] = x1;
                                else if (x2 < high)
                                        span[s][0] = x2;
                                else
                                {
                                        // Claimed whole; the last span takes its place.
                                        spans--;
                                        span[s][0] = span[spans][0];
                                        span[s][1] = span[spans][1];
                                        s--;
                                }
                        }
                }

                for (unsigned int s = 0; s < spans; s++)
                        compositor_fill(t, span[s][0], top, span[s][1] - span[s][0], bottom - top,
                                        COMPOSITOR_INK_DESKTOP);
        }
}

/* ---- end of the backend ---- */

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

struct compositor_input_event
{
        long seconds, microseconds;
        unsigned short type, code;
        int value;
};

struct compositor_absinfo
{
        int value, minimum, maximum, fuzz, flat, resolution;
};

#define COMPOSITOR_EVIOCGBIT(type, bytes) (0x80000000u | (bytes) << 16 | 0x4500u | (0x20u + (type)))
#define COMPOSITOR_EVIOCGABS(axis) (0x80184540u + (axis))

#define COMPOSITOR_EV_KEY 1u
#define COMPOSITOR_EV_REL 2u
#define COMPOSITOR_EV_ABS 3u
#define COMPOSITOR_REL_X 0u
#define COMPOSITOR_REL_Y 1u
#define COMPOSITOR_REL_WHEEL 8u
#define COMPOSITOR_REL_HWHEEL 6u
#define COMPOSITOR_BTN_LEFT 0x110u
#define COMPOSITOR_BTN_TASK 0x117u
#define COMPOSITOR_KEY_A 30u

struct compositor_poll
{
        int handle;
        short events, returned;
};

#define COMPOSITOR_POLLIN 1
#define COMPOSITOR_POLLERR 8
#define COMPOSITOR_POLLHUP 16

#define COMPOSITOR_INPUTS 16
#define COMPOSITOR_DAMAGE 8
#define COMPOSITOR_BURST 64
#define COMPOSITOR_EAGAIN 11
#define COMPOSITOR_EPERM 1
#define COMPOSITOR_EINVAL 22
#define COMPOSITOR_EBUSY 16
#define COMPOSITOR_ENOSPC 28
#define COMPOSITOR_EOPNOTSUPP 95

#define COMPOSITOR_F_GET_SEALS 1034
#define COMPOSITOR_SEAL_SHRINK 2u
#define COMPOSITOR_SOCK_NONBLOCK 0x800

// A client, and once it has created one, its surface.
struct compositor_client
{
        int handle;          // the connection; -1 for a free slot
        struct surface *page;
        unsigned long bytes;
        unsigned int id;
        unsigned int width, height, slots;
        int x, y;            // where it is, which the page's x, y request
        int asked_x, asked_y; // the request this was last placed from
        unsigned int style;
        unsigned int sequence;
        unsigned int slot;   // the slot on the screen
        unsigned int shown;  // committed once, so there is something to show
        unsigned int committed; // the sequence that put slot there
        unsigned int wake;   // events written since the last wake
};

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch; // pitch in bytes
        unsigned int fb, crtc, cursor_handle;
        int width, height;
        int damage[COMPOSITOR_DAMAGE][4]; // x1, y1, x2, y2: what changed, apart
        unsigned int damages;

        int listener, signals;
        int inputs[COMPOSITOR_INPUTS];
        int absolute_x[COMPOSITOR_INPUTS][2], absolute_y[COMPOSITOR_INPUTS][2];
        unsigned int input_count;

        struct compositor_client clients[COMPOSITOR_CLIENTS];
        struct compositor_client *stack[COMPOSITOR_CLIENTS]; // bottom first
        unsigned int stacked;
        struct compositor_client *focus, *hover, *grab, *drag;
        int drag_x, drag_y;
        int pointer_x, pointer_y;
        unsigned int modifiers;
        unsigned int next_id, placed;
        unsigned long painted; // pixels composed, all frames
} compositor;

static long compositor_call(positive number, positive a, positive b, positive c,
                            positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

/*
        What changed, as a few rectangles that do not overlap. A rectangle
        joins one it overlaps or lines up with, which costs nothing, and
        stays apart from the rest, so two windows at opposite corners are
        two small rectangles and not the screen between them. When the list
        is full the rectangle joins whichever grows least.
*/
static long compositor_area(const int *rectangle)
{
        return (long)(rectangle[2] - rectangle[0]) * (rectangle[3] - rectangle[1]);
}

static long compositor_joined(const int *a, const int *b, int *into)
{
        into[0] = a[0] < b[0] ? a[0] : b[0];
        into[1] = a[1] < b[1] ? a[1] : b[1];
        into[2] = a[2] > b[2] ? a[2] : b[2];
        into[3] = a[3] > b[3] ? a[3] : b[3];
        return compositor_area(into);
}

static void compositor_damage(int x, int y, int width, int height)
{
        int rectangle[4] = {x < 0 ? 0 : x, y < 0 ? 0 : y,
                            x + width > compositor.width ? compositor.width : x + width,
                            y + height > compositor.height ? compositor.height : y + height};
        int joined[4];
        unsigned int at = 0;

        if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
                return;

        while (at < compositor.damages)
        {
                int *other = compositor.damage[at];

                if (compositor_joined(rectangle, other, joined) <=
                    compositor_area(rectangle) + compositor_area(other))
                {
                        memory_copy(rectangle, joined, sizeof(joined));
                        memory_copy(other, compositor.damage[--compositor.damages], sizeof(joined));
                        at = 0;
                }
                else
                        at++;
        }

        if (compositor.damages == COMPOSITOR_DAMAGE)
        {
                unsigned int best = 0;
                long least = -1;

                for (at = 0; at < COMPOSITOR_DAMAGE; at++)
                {
                        long grown = compositor_joined(rectangle, compositor.damage[at], joined) -
                                     compositor_area(compositor.damage[at]);

                        if (least < 0 || grown < least)
                        {
                                least = grown;
                                best = at;
                        }
                }

                compositor_joined(rectangle, compositor.damage[best], compositor.damage[best]);
                return;
        }

        memory_copy(compositor.damage[compositor.damages++], rectangle, sizeof(rectangle));
}

static void compositor_frame_of(const struct compositor_client *client,
                                struct compositor_frame *frame)
{
        frame->x = client->x;
        frame->y = client->y;
        frame->width = (int)client->width;
        frame->height = (int)client->height;
        frame->scale = 1;
        frame->framed = (client->style & WINDOW_FRAME) != 0;
        frame->focused = client == compositor.focus;
        frame->closable = 1;
        // Not surface_buffer: the page is the program's, and its buffer_offset is whatever it last wrote.
        frame->pixels = surface_slot(client->page, client->slot, client->width, client->height);
        frame->pitch = client->width;
}

static void compositor_damage_client(const struct compositor_client *client)
{
        struct compositor_frame frame;
        int x, y, width, height;

        if (!client->shown)
                return;

        compositor_frame_of(client, &frame);
        compositor_outline(&frame, &x, &y, &width, &height);
        compositor_damage(x, y, width, height);
}

/*
        The damage, composed straight onto the scanout and then announced.
        The scanout is a mapping of the card's memory, so it is only ever
        written: every pixel of the damage once, by the stores of
        memory_fill_u32_rect and memory_copy (full 64-byte lines where a row
        is long enough, a masked store or two overlapping ones at the ends),
        none non-temporal and none a read. The card is then told the
        rectangles that changed, in one call: a virtual card copies nothing
        to its host without that.
*/
static void compositor_present(void)
{
        struct compositor_frame frames[COMPOSITOR_CLIENTS];
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_dirty dirty = {compositor.fb, 0, 0, compositor.damages, (unsigned long)clip};
        unsigned int count = 0;

        if (!compositor.damages)
                return;

        for (unsigned int at = 0; at < compositor.stacked; at++)
        {
                struct compositor_client *client = compositor.stack[at];

                if (client->shown && !(client->style & WINDOW_MINIMIZED))
                        compositor_frame_of(client, &frames[count++]);
        }

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *rectangle = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rectangle[0], rectangle[1], rectangle[2], rectangle[3]};

                compositor_scene(&target, frames, count);

                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)rectangle[side];
                compositor.painted += (unsigned long)compositor_area(rectangle);
        }

        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        compositor.damages = 0;
}

/*
        The card, at the mode Canvas left on it.

        Taking Canvas's own mode is what keeps the switch a plane change and
        not a modeset, and what keeps every coordinate the same on both sides
        of it. A connector with no crtc driving it takes its first mode.
*/
static b32 compositor_card_open(void)
{
        struct compositor_card_res resources = {0};
        unsigned int connectors[16], crtcs[16];
        struct compositor_mode mode;
        unsigned int connector = 0, crtc = 0;
        int have_mode = 0;

        compositor.card = (int)system_open_at(AT_FDCWD, (string_address) "/dev/dri/card0",
                                              FILE_READ_WRITE | O_CLOEXEC);
        if (compositor.card < 0)
                return compositor.card;

        if (system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EBUSY;

        if (resources.connector_count > 16 || resources.crtc_count > 16 || !resources.crtc_count)
                return -COMPOSITOR_EINVAL;

        resources.connectors = (unsigned long)connectors;
        resources.crtcs = (unsigned long)crtcs;
        resources.fb_count = resources.encoder_count = 0;
        if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EINVAL;

        for (unsigned int at = 0; at < resources.connector_count && !have_mode; at++)
        {
                struct compositor_connector probe = {.connector_id = connectors[at]};
                struct compositor_mode modes[64];
                struct compositor_encoder encoder = {0};
                struct compositor_crtc current = {0};

                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0 ||
                    probe.connection != 1 || !probe.mode_count)
                        continue;

                probe.mode_count = probe.mode_count > 64 ? 64 : probe.mode_count;
                probe.modes = (unsigned long)modes;
                probe.prop_count = probe.encoder_count = 0;
                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0)
                        continue;

                connector = connectors[at];
                crtc = crtcs[0];
                mode = modes[0];
                have_mode = 1;

                encoder.encoder_id = probe.encoder_id;
                if (probe.encoder_id &&
                    system_control(compositor.card, COMPOSITOR_GETENCODER, address_of encoder) == 0 &&
                    encoder.crtc_id)
                {
                        current.crtc_id = encoder.crtc_id;
                        crtc = encoder.crtc_id;
                        if (system_control(compositor.card, COMPOSITOR_GETCRTC, address_of current) == 0 &&
                            current.mode_valid)
                                mode = current.mode;
                }
        }

        if (!have_mode)
                return -COMPOSITOR_EINVAL;

        compositor.width = mode.hdisplay;
        compositor.height = mode.vdisplay;
        compositor.crtc = crtc;

        struct compositor_dumb dumb = {(unsigned int)compositor.height,
                                       (unsigned int)compositor.width, 32, 0, 0, 0, 0};
        struct compositor_map_dumb map = {0};
        struct compositor_fb fb = {0};

        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                return -COMPOSITOR_ENOSPC;

        fb.width = dumb.width;
        fb.height = dumb.height;
        fb.pitch = dumb.pitch;
        fb.bpp = 32;
        fb.depth = 24;
        fb.handle = dumb.handle;
        map.handle = dumb.handle;
        if (system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) != 0 ||
            system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                return -COMPOSITOR_EINVAL;

        long mapped = compositor_call(syscall(mmap), 0, dumb.size, 3, 1,
                                      (positive)compositor.card, map.offset);

        if (mapped < 0 && mapped > -4096)
                return -COMPOSITOR_ENOSPC;

        compositor.scanout = (unsigned int *)mapped;
        compositor.scanout_bytes = dumb.size;
        compositor.scanout_pitch = dumb.pitch;
        compositor.fb = fb.fb_id;

        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_present();

        struct compositor_crtc set = {(unsigned long)address_of connector, 1, crtc, fb.fb_id,
                                      0, 0, 0, 1, mode};

        if (system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) != 0)
                return -COMPOSITOR_EINVAL;

        return 0;
}

/*
        The pointer on the card's cursor plane: the kernel's arrow, white
        edged in black. A card with no plane has no pointer drawn, and still
        has one that moves and presses.
*/
static const unsigned short compositor_arrow_edge[20] = {
    0x0080, 0x00c0, 0x00a0, 0x0090, 0x0088, 0x0084, 0x0082, 0x0081, 0x8080, 0x4080,
    0xe083, 0x0092, 0x00a9, 0x00c9, 0x8084, 0x8004, 0x8002, 0x8003, 0x0000, 0x0000};
static const unsigned short compositor_arrow_fill[20] = {
    0x0000, 0x0000, 0x0040, 0x0060, 0x0070, 0x0078, 0x007c, 0x007e, 0x007f, 0x807f,
    0x007c, 0x006c, 0x0046, 0x0006, 0x0003, 0x0003, 0x0001, 0x0000, 0x0000, 0x0000};

static void compositor_cursor_start(void)
{
        struct compositor_dumb dumb = {64, 64, 32, 0, 0, 0, 0};
        struct compositor_map_dumb map = {0};
        struct compositor_cursor cursor = {1, compositor.crtc, 0, 0, 64, 64, 0};
        long mapped;

        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                return;

        map.handle = dumb.handle;
        if (system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                return;

        mapped = compositor_call(syscall(mmap), 0, dumb.size, 3, 1,
                                 (positive)compositor.card, map.offset);
        if (mapped < 0 && mapped > -4096)
                return;

        for (unsigned int y = 0; y < 64; y++)
        {
                unsigned int *row = (unsigned int *)((p8 address_to)mapped + (positive)y * dumb.pitch);

                for (unsigned int x = 0; x < 64; x++)
                {
                        unsigned int bit = x < 16 && y < 20
                                               ? (x < 8 ? 0x80u >> x : 0x8000u >> (x - 8))
                                               : 0;
                        unsigned int edge = bit && (compositor_arrow_edge[y] & bit);
                        unsigned int fill = bit && (compositor_arrow_fill[y] & bit);

                        row[x] = edge ? 0xff000000u : fill ? 0xffffffffu : 0;
                }
        }

        compositor_call(syscall(munmap), (positive)mapped, dumb.size, 0, 0, 0, 0);

        cursor.handle = dumb.handle;
        if (system_control(compositor.card, COMPOSITOR_CURSOR, address_of cursor) == 0)
                compositor.cursor_handle = dumb.handle;
}

static void compositor_cursor_move(void)
{
        struct compositor_cursor cursor = {2, compositor.crtc, compositor.pointer_x,
                                           compositor.pointer_y, 0, 0, 0};

        if (compositor.cursor_handle)
                system_control(compositor.card, COMPOSITOR_CURSOR, address_of cursor);
}

/*
        Every keyboard and pointer evdev has. Read beside Canvas's own handler,
        not grabbed from it: a grab would take Control-Alt-Backspace away from
        the kernel, and that chord is how a person gets out of a compositor
        that has stopped answering.
*/
static void compositor_inputs_open(void)
{
        for (unsigned int number = 0; number < 32 && compositor.input_count < COMPOSITOR_INPUTS; number++)
        {
                p8 path[32] = "/dev/input/event";
                unsigned long kinds = 0;
                p8 keys[96] = {0};
                unsigned long relative = 0, absolute = 0;
                positive length = string_length(path);
                bipolar handle;
                unsigned int at = compositor.input_count;

                length += positive_into_string(path + length, number);
                path[length] = 0;

                handle = system_open_at(AT_FDCWD, (string_address)path,
                                        FILE_READ | O_CLOEXEC | O_NONBLOCK);
                if (handle < 0)
                        continue;

                system_control(handle, COMPOSITOR_EVIOCGBIT(0, 8), address_of kinds);
                system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_KEY, 96), keys);
                system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_REL, 8), address_of relative);
                system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_ABS, 8), address_of absolute);

                int keyboard = (kinds >> COMPOSITOR_EV_KEY & 1) &&
                               (keys[COMPOSITOR_KEY_A / 8] >> (COMPOSITOR_KEY_A % 8) & 1);
                int mouse = (kinds >> COMPOSITOR_EV_REL & 1) && (relative & 3) == 3;
                int tablet = (kinds >> COMPOSITOR_EV_ABS & 1) && (absolute & 3) == 3;

                if (!keyboard && !mouse && !tablet)
                {
                        system_close(handle);
                        continue;
                }

                compositor.absolute_x[at][0] = compositor.absolute_x[at][1] = 0;
                compositor.absolute_y[at][0] = compositor.absolute_y[at][1] = 0;

                if (tablet)
                {
                        struct compositor_absinfo x = {0}, y = {0};

                        system_control(handle, COMPOSITOR_EVIOCGABS(0), address_of x);
                        system_control(handle, COMPOSITOR_EVIOCGABS(1), address_of y);
                        compositor.absolute_x[at][0] = x.minimum;
                        compositor.absolute_x[at][1] = x.maximum;
                        compositor.absolute_y[at][0] = y.minimum;
                        compositor.absolute_y[at][1] = y.maximum;
                }

                compositor.inputs[at] = (int)handle;
                compositor.input_count++;
        }
}

/*
        An event into a surface's ring. One side writes head and the other
        tail, and a full ring keeps what came first; a pointer motion behind
        a motion the program has not read yet replaces it, as version 1 does.
        tail is the program's, so it is read once and only believed as far as
        the ring is long.
*/
static void compositor_post(struct compositor_client *client, unsigned int type,
                            unsigned int flags, unsigned int serial, int a, int b)
{
        struct surface *page = client->page;
        unsigned int head = page->event_head;
        unsigned int used = head - __atomic_load_n(&page->event_tail, __ATOMIC_ACQUIRE);
        struct surface_event *last = &page->event[(head - 1) % SURFACE_EVENTS];

        if (type == SURFACE_EVENT_POINTER_MOTION && used && used <= SURFACE_EVENTS &&
            last->type == SURFACE_EVENT_POINTER_MOTION)
        {
                last->a = a;
                last->b = b;
                client->wake = 1;
                return;
        }

        if (used >= SURFACE_EVENTS)
        {
                page->event_dropped++;
                return;
        }

        struct surface_event *event = &page->event[head % SURFACE_EVENTS];

        event->type = (unsigned short)type;
        event->flags = (unsigned short)flags;
        event->serial = serial;
        event->a = a;
        event->b = b;
        __atomic_store_n(&page->event_head, head + 1, __ATOMIC_RELEASE);
        client->wake = 1;
}

static int compositor_wants_pointer(const struct compositor_client *client)
{
        return client && (__atomic_load_n(&client->page->window.want, __ATOMIC_RELAXED) &
                          WINDOW_WANT_POINTER);
}

static void compositor_focus(struct compositor_client *client)
{
        struct compositor_client *was = compositor.focus;

        if (was == client)
                return;

        compositor.focus = client;

        if (was)
        {
                was->page->window.state &= ~WINDOW_FOCUSED;
                compositor_post(was, SURFACE_EVENT_FOCUS, 0, 0, 0, 0);
                compositor_damage_client(was);
        }

        if (client)
        {
                client->page->window.state |= WINDOW_FOCUSED;
                compositor_post(client, SURFACE_EVENT_FOCUS, 0, 0, 1, 0);
                compositor_damage_client(client);
        }
}

static void compositor_raise(struct compositor_client *client)
{
        unsigned int at = 0;

        while (at < compositor.stacked && compositor.stack[at] != client)
                at++;

        if (at >= compositor.stacked || at == compositor.stacked - 1)
                return;

        for (; at + 1 < compositor.stacked; at++)
                compositor.stack[at] = compositor.stack[at + 1];

        compositor.stack[at] = client;
        client->page->window.z = (int)at;
        compositor_damage_client(client);
}

// The topmost surface whose rectangle holds the point, frame and all.
static struct compositor_client *compositor_under(int x, int y)
{
        for (unsigned int at = compositor.stacked; at--;)
        {
                struct compositor_client *client = compositor.stack[at];
                struct compositor_frame frame;
                int fx, fy, fw, fh;

                if (!client->shown || (client->style & WINDOW_MINIMIZED))
                        continue;

                compositor_frame_of(client, &frame);
                compositor_outline(&frame, &fx, &fy, &fw, &fh);
                if (x >= fx && x < fx + fw && y >= fy && y < fy + fh)
                        return client;
        }

        return null;
}

static int compositor_title_height(const struct compositor_client *client)
{
        return client->style & WINDOW_FRAME ? COMPOSITOR_TITLE : 0;
}

static void compositor_destroy(struct compositor_client *client)
{
        unsigned int at = 0;

        compositor_damage_client(client);

        while (at < compositor.stacked && compositor.stack[at] != client)
                at++;

        if (at < compositor.stacked)
        {
                for (; at + 1 < compositor.stacked; at++)
                        compositor.stack[at] = compositor.stack[at + 1];
                compositor.stacked--;
        }

        if (compositor.hover == client)
                compositor.hover = null;
        if (compositor.grab == client)
                compositor.grab = null;
        if (compositor.drag == client)
                compositor.drag = null;
        if (compositor.focus == client)
        {
                compositor.focus = null;
                if (compositor.stacked)
                        compositor_focus(compositor.stack[compositor.stacked - 1]);
        }

        if (client->page)
                compositor_call(syscall(munmap), (positive)client->page, client->bytes, 0, 0, 0, 0);

        system_close(client->handle);
        memory_zero(client, sizeof(*client));
        client->handle = -1;
}

static void compositor_reply(struct compositor_client *client, int status,
                             const struct surface_request *request)
{
        struct surface_message message = {SURFACE_CREATED, status, *request};

        surface_send(client->handle, &message, -1);
}

/*
        A create: the request checked, then the page. Only an application
        window can be had in M1 -- the other roles need the grants of canvas
        spec 02, and until there are grants nobody holds one, so each is
        refused as a program without a grant would be.
*/
static int compositor_create(struct compositor_client *client, struct surface_request *request,
                             int file)
{
        unsigned long slot, bytes;
        long size, seals, mapped;
        struct surface *page;

        if (client->page)
                return -COMPOSITOR_EBUSY;

        if (request->size != sizeof(*request) ||
            (request->protocol != 0 && request->protocol != SURFACE_PROTOCOL) ||
            request->flags & ~(SURFACE_REQUEST_FOCUS | SURFACE_REQUEST_TRANSIENT) ||
            (request->buffer_slots != 1 && request->buffer_slots != 2) ||
            !request->width || !request->height ||
            request->width > (unsigned int)compositor.width ||
            request->height > (unsigned int)compositor.height)
                return -COMPOSITOR_EINVAL;

        if (request->role == SURFACE_ROLE_LOG || request->role > SURFACE_ROLE_LEGACY)
                return -COMPOSITOR_EINVAL;

        if (request->role != SURFACE_ROLE_APP)
                return -COMPOSITOR_EPERM;

        if (file < 0)
                return -COMPOSITOR_EINVAL;

        slot = SURFACE_PAGE_ROUND((unsigned long)request->width * request->height * 4);
        bytes = WINDOW_PIXELS + slot * request->buffer_slots;

        size = compositor_call(syscall(lseek), (positive)file, 0, 2, 0, 0, 0);
        seals = compositor_call(syscall(fcntl), (positive)file, COMPOSITOR_F_GET_SEALS, 0, 0, 0, 0);
        if (size < (long)bytes || seals < 0 || !(seals & COMPOSITOR_SEAL_SHRINK))
                return -COMPOSITOR_EINVAL;

        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 1, (positive)file, 0);
        if (mapped < 0 && mapped > -4096)
                return -COMPOSITOR_ENOSPC;

        page = (struct surface *)mapped;
        client->page = page;
        client->bytes = bytes;
        client->id = ++compositor.next_id;
        client->width = request->width;
        client->height = request->height;
        client->slots = request->buffer_slots;

        // Placed in a cascade from where the kernel puts a new window.
        client->x = 80 + (int)(compositor.placed % 8) * 32;
        client->y = 80 + (int)(compositor.placed % 8) * 32;
        compositor.placed++;
        client->asked_x = client->x;
        client->asked_y = client->y;
        client->style = WINDOW_FRAME;

        page->window.x = client->x;
        page->window.y = client->y;
        page->window.z = (int)compositor.stacked;
        page->window.width = request->width;
        page->window.height = request->height;
        page->window.style = WINDOW_FRAME;
        page->window.pitch = request->width;
        page->window.max_width = request->width;
        page->window.max_height = request->height;
        page->window.display_width = (unsigned int)compositor.width;
        page->window.display_height = (unsigned int)compositor.height;
        page->window.awake = 1;
        page->window.mapping = (unsigned int)bytes;
        page->window.state = 0;

        page->protocol = SURFACE_PROTOCOL;
        page->header_size = SURFACE_HEADER;
        page->role_requested = request->role;
        page->role_state = SURFACE_ROLE_ACTIVE;
        page->role_result = 0;
        page->layer = SURFACE_LAYER_NORMAL;
        page->lock_applied = SURFACE_LOCK_MOVABLE | SURFACE_LOCK_RESIZABLE | SURFACE_LOCK_DECORATED;
        page->surface_id = client->id;
        page->applied_width = request->width;
        page->applied_height = request->height;
        page->buffer_slots = request->buffer_slots;
        page->buffer_format = SURFACE_FORMAT_XR24;
        page->buffer_stride = request->width * 4;
        page->buffer_size = request->width * request->height * 4;
        page->buffer_offset[0] = WINDOW_PIXELS;
        page->buffer_offset[1] = (unsigned int)(WINDOW_PIXELS + slot);
        __atomic_store_n(&page->magic, SURFACE_MAGIC, __ATOMIC_RELEASE);

        client->sequence = page->window.sequence;

        compositor.stack[compositor.stacked++] = client;

        request->protocol = SURFACE_PROTOCOL;
        request->surface_id = client->id;
        request->bytes = (unsigned int)bytes;
        return 0;
}

/*
        A commit: what the page says now, read once into the compositor's own
        copy and clamped there, then damaged. The geometry stays what create
        checked; the program moves, frames or minimizes its window, and names
        the slot to show.
*/
static void compositor_commit(struct compositor_client *client)
{
        struct surface *page = client->page;
        unsigned int sequence = __atomic_load_n(&page->window.sequence, __ATOMIC_ACQUIRE);
        unsigned int slot = __atomic_load_n(&page->buffer_index, __ATOMIC_RELAXED);
        unsigned int style = __atomic_load_n(&page->window.style, __ATOMIC_RELAXED);
        int x = __atomic_load_n(&page->window.x, __ATOMIC_RELAXED);
        int y = __atomic_load_n(&page->window.y, __ATOMIC_RELAXED);
        unsigned int count = __atomic_load_n(&page->damage_count, __ATOMIC_RELAXED);
        int whole = !client->shown;

        if (sequence == client->sequence && client->shown)
                return;

        client->sequence = sequence;
        slot = slot < client->slots ? slot : 0;

        // A move the program asked for since the last one it was given.
        if (x != client->asked_x || y != client->asked_y)
        {
                x = x < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : x > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : x;
                y = y < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : y > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : y;
                compositor_damage_client(client);
                client->x = client->asked_x = x;
                client->y = client->asked_y = y;
                whole = 1;
        }

        style &= WINDOW_FRAME | WINDOW_MINIMIZED;
        if (style != client->style)
        {
                compositor_damage_client(client);
                client->style = style;
                whole = 1;
        }

        // The slot that was showing is the program's again.
        if (client->shown && slot != client->slot)
        {
                page->release_serial = client->committed;
                compositor_post(client, SURFACE_EVENT_RELEASE, 0, client->committed,
                                (int)client->slot, 0);
                whole = 1;
        }

        client->slot = slot;
        client->committed = sequence;
        client->shown = 1;

        if (whole || !count || count > SURFACE_DAMAGE_MAX)
        {
                compositor_damage_client(client);
                return;
        }

        for (unsigned int at = 0; at < count; at++)
        {
                // Clipped to the buffer in a type the program's numbers
                // cannot wrap; an empty or inverted one is no damage.
                struct surface_damage rectangle = page->damage[at];
                long x1 = rectangle.x < 0 ? 0 : rectangle.x;
                long y1 = rectangle.y < 0 ? 0 : rectangle.y;
                long x2 = (long)rectangle.x + rectangle.width;
                long y2 = (long)rectangle.y + rectangle.height;

                x2 = x2 > (long)client->width ? (long)client->width : x2;
                y2 = y2 > (long)client->height ? (long)client->height : y2;
                if (rectangle.width <= 0 || rectangle.height <= 0 || x2 <= x1 || y2 <= y1)
                        continue;

                compositor_damage(client->x + (int)x1,
                                  client->y + compositor_title_height(client) + (int)y1,
                                  (int)(x2 - x1), (int)(y2 - y1));
        }
}

// Everything a connection has said, until it would block.
static void compositor_client_read(struct compositor_client *client)
{
        for (;;)
        {
                struct surface_message message;
                struct surface_iovec part = {&message, sizeof(message)};
                unsigned long control[4] = {0};
                struct surface_msghdr header = {0, 0, &part, 1, control, sizeof(control), 0};
                struct surface_rights *rights = (struct surface_rights *)control;
                int files[4], file_count = 0;
                long got = compositor_call(syscall(recvmsg), (positive)client->handle,
                                           (positive)address_of header,
                                           SURFACE_MSG_DONTWAIT | 0x40000000u, 0, 0, 0);

                if (got == -COMPOSITOR_EAGAIN)
                        return;

                if (header.control_length >= 16 && rights->level == 1 && rights->type == 1 &&
                    rights->length >= 16 && rights->length <= sizeof(control))
                {
                        int *handles = (int *)((p8 address_to)control + 16);

                        file_count = (int)((rights->length - 16) / 4);
                        for (int at = 0; at < file_count; at++)
                                files[at] = handles[at];
                }

                if (got == (long)sizeof(message) && message.kind == SURFACE_CREATE && file_count == 1)
                {
                        int status = compositor_create(client, &message.request, files[0]);

                        compositor_reply(client, status, &message.request);
                        if (status < 0)
                                got = 0;
                }
                else if (got == (long)sizeof(message) && message.kind == SURFACE_COMMIT &&
                         client->page && !file_count)
                        compositor_commit(client);
                else
                        got = 0;

                for (int at = 0; at < file_count; at++)
                        system_close(files[at]);

                // Closed, refused, or said something that is not the protocol.
                if (got <= 0)
                {
                        compositor_destroy(client);
                        return;
                }
        }
}

static void compositor_accept(void)
{
        for (;;)
        {
                long handle = compositor_call(syscall(accept4), (positive)compositor.listener, 0, 0,
                                              COMPOSITOR_SOCK_NONBLOCK | SURFACE_SOCK_CLOEXEC, 0, 0);
                struct compositor_client *free_slot = null;

                if (handle < 0)
                        return;

                for (unsigned int at = 0; at < COMPOSITOR_CLIENTS && !free_slot; at++)
                        if (compositor.clients[at].handle < 0)
                                free_slot = &compositor.clients[at];

                if (!free_slot)
                {
                        system_close(handle);
                        continue;
                }

                memory_zero(free_slot, sizeof(*free_slot));
                free_slot->handle = (int)handle;
        }
}

// The US layout, enough to name what a test types; anything else is 0.
static const char compositor_plain[58] =
    "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\r\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char compositor_shifted[58] =
    "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\r\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

static unsigned int compositor_modifier(unsigned int code)
{
        switch (code)
        {
        case 42: case 54: return SURFACE_KEY_SHIFT;
        case 29: case 97: return SURFACE_KEY_CONTROL;
        case 56: return SURFACE_KEY_ALT;
        case 100: return SURFACE_KEY_ALTGR;
        case 125: case 126: return SURFACE_KEY_SUPER;
        }
        return 0;
}

static void compositor_key(unsigned int code, int value)
{
        unsigned int modifier = compositor_modifier(code);
        unsigned int character = 0;
        unsigned int flags;

        if (modifier)
        {
                if (value)
                        compositor.modifiers |= modifier;
                else
                        compositor.modifiers &= ~modifier;
        }

        if (code < sizeof(compositor_plain))
                character = (unsigned char)(compositor.modifiers & SURFACE_KEY_SHIFT
                                                ? compositor_shifted[code]
                                                : compositor_plain[code]);

        flags = compositor.modifiers | (value ? SURFACE_KEY_DOWN : 0) | (value == 2 ? SURFACE_KEY_REPEAT : 0);

        if (compositor.focus)
                compositor_post(compositor.focus, SURFACE_EVENT_KEY, flags, 0, (int)code, (int)character);
}

// Where the pointer is in a surface's buffer.
static void compositor_local(const struct compositor_client *client, int *x, int *y)
{
        *x = compositor.pointer_x - client->x;
        *y = compositor.pointer_y - client->y - compositor_title_height(client);
}

static void compositor_motion(void)
{
        struct compositor_client *under = compositor_under(compositor.pointer_x, compositor.pointer_y);
        int x, y;

        compositor_cursor_move();

        if (compositor.drag)
        {
                struct compositor_client *client = compositor.drag;

                compositor_damage_client(client);
                client->x = compositor.pointer_x - compositor.drag_x;
                client->y = compositor.pointer_y - compositor.drag_y;
                client->page->window.x = client->asked_x = client->x;
                client->page->window.y = client->asked_y = client->y;
                compositor_damage_client(client);
                return;
        }

        if (under != compositor.hover)
        {
                if (compositor_wants_pointer(compositor.hover))
                        compositor_post(compositor.hover, SURFACE_EVENT_POINTER_LEAVE, 0, 0, 0, 0);
                compositor.hover = under;
                if (compositor_wants_pointer(under))
                {
                        compositor_local(under, &x, &y);
                        compositor_post(under, SURFACE_EVENT_POINTER_ENTER, 0, 0, x, y);
                }
        }

        // A held button keeps the pointer with the surface it went down on.
        under = compositor.grab ? compositor.grab : under;
        if (compositor_wants_pointer(under))
        {
                compositor_local(under, &x, &y);
                compositor_post(under, SURFACE_EVENT_POINTER_MOTION, 0, 0, x, y);
        }
}

static void compositor_button(unsigned int code, int down)
{
        struct compositor_client *under = compositor_under(compositor.pointer_x, compositor.pointer_y);
        int x, y;

        if (!down)
        {
                struct compositor_client *grab = compositor.grab;

                compositor.drag = null;
                compositor.grab = null;
                if (compositor_wants_pointer(grab))
                        compositor_post(grab, SURFACE_EVENT_POINTER_BUTTON, 0, 0, (int)code, 0);
                return;
        }

        if (!under)
                return;

        if (code == COMPOSITOR_BTN_LEFT && !compositor.grab)
        {
                struct compositor_frame frame;
                int cx, cy, side;

                compositor_raise(under);
                compositor_focus(under);

                compositor_frame_of(under, &frame);
                if (compositor_close_box(&frame, &cx, &cy, &side) &&
                    compositor.pointer_x >= cx && compositor.pointer_x < cx + side &&
                    compositor.pointer_y >= cy && compositor.pointer_y < cy + side)
                {
                        under->page->window.state |= WINDOW_CLOSING;
                        compositor_post(under, SURFACE_EVENT_CLOSE, 0, 0, SURFACE_CLOSE_REQUESTED, 0);
                        return;
                }

                if (compositor.pointer_y < under->y + compositor_title_height(under))
                {
                        compositor.drag = under;
                        compositor.drag_x = compositor.pointer_x - under->x;
                        compositor.drag_y = compositor.pointer_y - under->y;
                        return;
                }
        }

        compositor_local(under, &x, &y);
        if (y < 0 || x < 0 || x >= (int)under->width)
                return;

        compositor.grab = compositor.grab ? compositor.grab : under;
        if (compositor_wants_pointer(under))
        {
                // The button event carries its code; the position is the
                // motion's, sent just before it.
                compositor_post(under, SURFACE_EVENT_POINTER_MOTION, 0, 0, x, y);
                compositor_post(under, SURFACE_EVENT_POINTER_BUTTON, SURFACE_KEY_DOWN, 0, (int)code, 0);
        }
}

static void compositor_input_read(unsigned int at)
{
        struct compositor_input_event events[64];
        int moved = 0;

        for (;;)
        {
                bipolar got = system_read_once(compositor.inputs[at], events, sizeof(events));

                // A device that went away answers every poll at once, so it
                // is let go of rather than read in a loop.
                if (got < 0 && got != -COMPOSITOR_EAGAIN && got != -4)
                {
                        system_close(compositor.inputs[at]);
                        compositor.inputs[at] = -1;
                }

                if (got <= 0)
                        break;

                for (unsigned int i = 0; i < (unsigned int)got / sizeof(events[0]); i++)
                {
                        struct compositor_input_event *event = &events[i];

                        if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_X)
                                compositor.pointer_x += event->value, moved = 1;
                        else if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_Y)
                                compositor.pointer_y += event->value, moved = 1;
                        else if (event->type == COMPOSITOR_EV_REL &&
                                 (event->code == COMPOSITOR_REL_WHEEL || event->code == COMPOSITOR_REL_HWHEEL))
                        {
                                struct compositor_client *under = compositor.grab ? compositor.grab : compositor.hover;

                                if (compositor_wants_pointer(under))
                                        compositor_post(under, SURFACE_EVENT_POINTER_WHEEL, 0, 0,
                                                        event->value * 120,
                                                        event->code == COMPOSITOR_REL_HWHEEL);
                        }
                        else if (event->type == COMPOSITOR_EV_ABS && event->code < 2)
                        {
                                int *range = event->code ? compositor.absolute_y[at] : compositor.absolute_x[at];
                                long span = (long)range[1] - range[0];
                                long size = event->code ? compositor.height : compositor.width;
                                // As Canvas maps a tablet: offset times size over span.
                                long place = span > 0 ? ((long)event->value - range[0]) * size / span : 0;

                                if (event->code)
                                        compositor.pointer_y = (int)place;
                                else
                                        compositor.pointer_x = (int)place;
                                moved = 1;
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code >= COMPOSITOR_BTN_LEFT &&
                                 event->code <= COMPOSITOR_BTN_TASK)
                        {
                                if (moved)
                                        compositor_motion(), moved = 0;
                                compositor_button(event->code, event->value != 0);
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code < 256)
                                compositor_key(event->code, event->value);
                }

                compositor.pointer_x = compositor.pointer_x < 0 ? 0
                                       : compositor.pointer_x >= compositor.width ? compositor.width - 1
                                                                                : compositor.pointer_x;
                compositor.pointer_y = compositor.pointer_y < 0 ? 0
                                       : compositor.pointer_y >= compositor.height ? compositor.height - 1
                                                                                 : compositor.pointer_y;
        }

        if (moved)
                compositor_motion();
}

static int compositor_listen(void)
{
        struct {
                unsigned short family;
                char path[sizeof(SURFACE_SOCKET)];
        } where = {SURFACE_AF_UNIX, SURFACE_SOCKET};
        long handle;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run", 0755, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);

        // The card is this program's, so a socket left at the path is from a
        // compositor that is no longer running.
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)SURFACE_SOCKET, 0, 0, 0, 0);

        handle = compositor_call(syscall(socket), SURFACE_AF_UNIX,
                                 SURFACE_SOCK_SEQPACKET | SURFACE_SOCK_CLOEXEC | COMPOSITOR_SOCK_NONBLOCK,
                                 0, 0, 0, 0);
        if (handle < 0)
                return (int)handle;

        if (compositor_call(syscall(bind), (positive)handle, (positive)address_of where,
                            sizeof(where), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)handle, 16, 0, 0, 0, 0) < 0)
        {
                system_close(handle);
                return -COMPOSITOR_EINVAL;
        }

        // Any program may open an application window, as any may open
        // /dev/spark for a version 1 one.
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)SURFACE_SOCKET, 0666, 0, 0, 0);
        compositor.listener = (int)handle;
        return 0;
}

// SIGTERM, SIGINT and SIGHUP, as a descriptor the loop polls.
static int compositor_signals(void)
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1);

        compositor_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        return (int)compositor_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8,
                                    COMPOSITOR_SOCK_NONBLOCK | SURFACE_SOCK_CLOEXEC, 0, 0);
}

static void compositor_say(string_address what, bipolar value)
{
        string_format(log, "compositor: %s %b\n", what, value);
        log_flush();
}

/*
        The card back to Canvas, and how long Canvas took to draw again.

        The scanout's mapping holds the card's file open as surely as the
        descriptor does, so both go before the last close -- which is what
        runs Canvas's restore hook. The kernel's own count of composes says
        when the desktop was drawn again; it needs CAP_SYS_ADMIN, which a
        program that could take the card has.
*/
static void compositor_hand_back(void)
{
        struct input_stats before = {0}, after = {0};
        b32 spark = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
        b32 counted = spark >= 0 && system_control(spark, SPARK_IOCTL_INPUT_STATS, address_of before) == 0;
        positive start, now;

        compositor_call(syscall(munmap), (positive)compositor.scanout, compositor.scanout_bytes, 0, 0, 0, 0);
        start = clock_monotonic_nanoseconds();
        system_close(compositor.card);
        now = start;

        while (counted && now - start < 2000000000ull)
        {
                long nap[2] = {0, 200000};

                if (system_control(spark, SPARK_IOCTL_INPUT_STATS, address_of after) != 0)
                        break;
                now = clock_monotonic_nanoseconds();
                if (after.composes != before.composes)
                {
                        compositor_say("the kernel desktop was drawn again, microseconds after the card was let go:",
                                       (bipolar)((now - start) / 1000));
                        break;
                }
                system_call_2(syscall(nanosleep), (positive)nap, 0);
        }

        if (spark >= 0)
                system_close(spark);
}

static b32 compositor_main()
{
        b32 status;
        int running = 1;

        claim_standard_descriptors();
        memory_zero(&compositor, sizeof(compositor));
        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                compositor.clients[at].handle = -1;

        status = compositor_card_open();
        if (status < 0)
        {
                compositor_say("cannot take the display, error", status);
                return 1;
        }

        compositor_cursor_start();
        compositor_inputs_open();
        compositor.pointer_x = compositor.width / 2;
        compositor.pointer_y = compositor.height / 2;
        compositor_cursor_move();

        compositor.signals = compositor_signals();
        status = compositor_listen();
        if (status < 0 || compositor.signals < 0)
        {
                compositor_say("cannot listen, error", status < 0 ? status : compositor.signals);
                compositor_hand_back();
                return 1;
        }

        string_format(log, "compositor: %b by %b, %p input devices, listening on %s\n",
                      (bipolar)compositor.width, (bipolar)compositor.height,
                      (positive)compositor.input_count, SURFACE_SOCKET);
        log_flush();

        while (running)
        {
                struct compositor_poll wanted[2 + COMPOSITOR_INPUTS + COMPOSITOR_CLIENTS];
                struct compositor_client *polled[COMPOSITOR_CLIENTS];
                unsigned int count = 0, clients = 0, inputs;

                wanted[count++] = (struct compositor_poll){compositor.signals, COMPOSITOR_POLLIN, 0};
                wanted[count++] = (struct compositor_poll){compositor.listener, COMPOSITOR_POLLIN, 0};
                for (unsigned int at = 0; at < compositor.input_count; at++)
                        wanted[count++] = (struct compositor_poll){compositor.inputs[at], COMPOSITOR_POLLIN, 0};
                inputs = count;
                for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                        if (compositor.clients[at].handle >= 0)
                        {
                                polled[clients++] = &compositor.clients[at];
                                wanted[count++] = (struct compositor_poll){compositor.clients[at].handle,
                                                                          COMPOSITOR_POLLIN, 0};
                        }

                if (compositor_call(syscall(ppoll), (positive)wanted, count, 0, 0, 0, 0) < 0)
                        continue;

                if (wanted[0].returned)
                        running = 0;

                for (unsigned int at = 0; at < compositor.input_count; at++)
                        if (wanted[2 + at].returned)
                                compositor_input_read(at);

                for (unsigned int at = 0; at < clients; at++)
                        if (wanted[inputs + at].returned && polled[at]->handle >= 0)
                                compositor_client_read(polled[at]);

                if (wanted[1].returned)
                        compositor_accept();

                compositor_present();

                // One wake a program per pass, however many events it got.
                for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                {
                        struct compositor_client *client = &compositor.clients[at];
                        struct surface_message wake = {SURFACE_WAKE, 0, {0}};

                        if (client->handle < 0 || !client->wake)
                                continue;

                        client->wake = 0;
                        surface_send(client->handle, &wake, -1);
                }
        }

        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                if (compositor.clients[at].handle >= 0)
                        compositor_destroy(&compositor.clients[at]);

        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)SURFACE_SOCKET, 0, 0, 0, 0);
        string_format(log, "compositor: %p pixels composed\n", (positive)compositor.painted);
        log_flush();
        compositor_hand_back();
        return 0;
}

// surface ----------------------------------------------------------
/*
        A test client: surface [-x X] [-y Y] [-w WIDTH] [-h HEIGHT]
        [-c RRGGBB] [-r ROLE] [-p] [-f FONT.ttf] [-t TEXT] [-s SECONDS].

        A window of one colour at X, Y, a line of TEXT drawn with font.c on
        it when FONT is given, pointer events asked for with -p, and every
        event it is sent written to standard output, one line each. It lives
        until its close box is pressed, the compositor goes, or SECONDS.
*/
static bipolar surface_number(string_address text, b32 hex)
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

static positive surface_map_file(string_address path, positive address_to size)
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

static void surface_text(struct surface *surface, unsigned int slot, string_address path,
                         string_address text, unsigned int colour)
{
        positive size = 0;
        positive bytes = surface_map_file(path, address_of size);
        font_face face;
        const font_face address_to faces[1] = {&face};
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);
        static p8 atlas[256 * 256];
        font_target target = {surface_buffer(surface, slot), surface->window.width,
                              (b32)surface->window.width, (b32)surface->window.height};
        p32 em = 16 << 6;
        b32 got;

        if (!bytes || (engine < 0 && engine > -4096) ||
            font_load(&face, (const p8 address_to)bytes, size, 0) != 0)
        {
                log_direct(str("surface: no font\n"));
                return;
        }

        font_start((font_engine address_to)engine, atlas, 256, 256);
        got = font_draw((font_engine address_to)engine, faces, 1, (const p8 address_to)text,
                        string_length(text), em, 0, &target, 8 << 16,
                        8 + font_metrics_at(&face, em).baseline,
                        0xffffff, colour);
        string_format(log, "surface: text drawn %b px wide\n", (bipolar)(got >> 16));
        log_flush();
}

static b32 surface_main()
{
        bipolar x = -1, y = -1, width = 320, height = 200, colour = 0x3366aa, role = SURFACE_ROLE_APP;
        bipolar seconds = -1;
        b32 pointer = 0;
        bipolar offset = -1;
        string_address font = null, text = (string_address) "Moonwater";
        struct surface *surface;
        positive until;

        claim_standard_descriptors();

        for (positive at = 1; at + 1 <= program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2])
                        continue;
                if (option[1] == 'p')
                {
                        pointer = 1;
                        continue;
                }
                if (!value)
                        break;
                at++;
                switch (option[1])
                {
                case 'x': x = surface_number(value, 0); break;
                case 'y': y = surface_number(value, 0); break;
                case 'w': width = surface_number(value, 0); break;
                case 'h': height = surface_number(value, 0); break;
                case 'c': colour = surface_number(value, 1); break;
                case 'r': role = surface_number(value, 0); break;
                case 's': seconds = surface_number(value, 0); break;
                case 'O': offset = surface_number(value, 0); break;
                case 'f': font = value; break;
                case 't': text = value; break;
                }
        }

        surface = width > 0 && height > 0 && role >= 0
                      ? surface_open((unsigned int)width, (unsigned int)height, (unsigned int)role)
                      : null;
        if (!surface)
        {
                string_format(log, "surface: refused %b\n", (bipolar)surface_refused);
                log_flush();
                return 1;
        }

        for (bipolar row = 0; row < height; row++)
                memory_fill_u32(surface_buffer(surface, 0) + row * width, (positive)width,
                                (p32)colour & 0xffffff);

        if (font)
                surface_text(surface, 0, font, text, (unsigned int)colour & 0xffffff);

        if (x >= 0)
                surface->window.x = (int)x;
        if (y >= 0)
                surface->window.y = (int)y;
        if (pointer)
                surface->window.want = WINDOW_WANT_POINTER;
        surface->buffer_index = 0;
        surface_commit(surface);

        // What a hostile program does: writes where the compositor wrote
        // its offsets, and commits.
        if (offset >= 0)
        {
                surface->buffer_offset[0] = (unsigned int)offset;
                surface_commit(surface);
        }

        string_format(log, "surface: %p at %b,%b\n", (positive)surface->surface_id,
                      (bipolar)surface->window.x, (bipolar)surface->window.y);
        log_flush();

        until = seconds >= 0 ? clock_monotonic_nanoseconds() + (positive)seconds * 1000000000ull : 0;

        for (;;)
        {
                struct surface_event event;
                long timeout = -1;
                b32 closing = 0;

                if (until)
                {
                        positive now = clock_monotonic_nanoseconds();

                        if (now >= until)
                                break;
                        timeout = (long)(until - now);
                }

                if (surface_wait(surface, -1, timeout) < 0)
                {
                        log_direct(str("surface: the compositor has gone\n"));
                        break;
                }

                while (surface_event(surface, &event))
                {
                        string_format(log, "surface: event %p %p %b %b\n", (positive)event.type,
                                      (positive)event.flags, (bipolar)event.a, (bipolar)event.b);
                        closing |= event.type == SURFACE_EVENT_CLOSE;
                }
                log_flush();

                if (closing)
                        break;
        }

        surface_close(surface);
        return 0;
}

#endif // MOONWATER_CANVAS_COMPOSITOR
