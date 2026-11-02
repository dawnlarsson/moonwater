/*
        The system's own look: what a window Moonwater draws for itself is
        made of, so the settings, the launcher and every window like them
        look like one system. It is the kit (Dawning Native, ported): its
        theme's roles, its type roles, its shapes drawn with their edges
        where they fall, its controls with their motion, and the composites
        a window is made of -- a page (a title, a search field, a column of
        groups, a body and a status line), a list of rows in sections, the
        menu a choice opens, a page of text, a scrollbar on whatever scrolls
        and a finger's press, drag and throw. An app declares what it shows;
        every size, colour, state and motion is the kit's, with nothing to
        set. Sizes are logical pixels, times the scale the compositor draws
        at. Text is font.c's, Inter; a run is drawn as coverage into a
        scratch line and laid over the picture, premultiplied like everything
        else, at whole pixels and in grey, never coloured by the subpixels.

        A window of the kit is opaque, the theme's surface (ui->opaque); a
        glass window (WINDOW_GLASS, window.c) is premultiplied over what is
        under it, and its ground's inks (ui_inks: ink, quiet, lit, ring) are
        what stands out from the tint the compositor gives it.

        Glass is for temporary overlays only: a launcher, a prompt, a
        notification, a transient popup that goes away on its own or on a key.
        Every window that stays (settings, the editor, the terminal, a
        document) is the kit's opaque surface. A temporary overlay is glass
        because it sits over what is under it and is meant to be read in
        passing; a window that holds state a person works in is never glass.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_UI
#define MOONWATER_CANVAS_UI

// What text is below the look: documents, history, selections, segmentation (text.c, which says why it is its own file).
#include "text.c"

#define UI_ROW 40      // a row of a list or a column
#define UI_PAD 16      // the window's margin
#define UI_INSET 14    // a row's text in from its edges
#define UI_RADIUS 8    // the windows' corners
#define UI_SCRATCH_ROWS 96

struct ui
{
        unsigned int address_to pixels; // premultiplied ARGB, pitch pixels a row
        unsigned long pitch;
        int width, height;
        int top, bottom;                // the rows that may be drawn: a list scrolled under a title is cut
        int scale;
        font_engine address_to engine;
        const font_face address_to regular;
        const font_face address_to bold;
        unsigned int ground, ink, quiet, lit, ring;
        unsigned int address_to scratch; // a line of text, as coverage
        int scratch_width;
        // A repaint's rows, when band_bottom > band_top: nothing else is written. Last, so the positional initialisers of a ui stay.
        int band_top, band_bottom;
        font_face faces[2];             // the regular and the bold, loaded by the runner
        const font_face address_to mono; // the kit's mono (a tag, a code well, a key), or none: then the regular
        font_face face_mono;
        b32 opaque;                     // a window of the kit's surface, not glass: cleared to the theme's surface
        int scratch_scale;              // the scale ui_scratch_fit mapped the scratch at, 0 for a scratch the caller gave
        b32 glass;                      // a glass window (WINDOW_GLASS): its pieces are the ground's inks, ui_text_colour and ui_well
};

/*
        x / 255 for x below 65535, exact: x plus its high byte, plus one, shifted. The blends
        below never make a product past 255 * 255 + 127, which is well under that.
*/
static inline unsigned int ui_div255(unsigned int x)
{
        return (x + 1 + (x >> 8)) >> 8;
}

// Whether row y may be written: a repaint's band, or every row when there is none.
static inline b32 ui_in_band(const struct ui address_to ui, int y)
{
        return ui->band_bottom <= ui->band_top || (y >= ui->band_top && y < ui->band_bottom);
}

// A colour at alpha (0 to 255), premultiplied: what every piece is drawn in.
static unsigned int ui_tone(unsigned int colour, unsigned int alpha)
{
        alpha = alpha > 255 ? 255 : alpha;
        return alpha << 24 | window_mix(0, colour & 0xffffffu, alpha + (alpha >> 7));
}

/*
        A text run's cover ramps for one ink, alpha and ground: the tone of each cover (ui_tone at the cover's alpha) and
        the tone over the ground. A frame's runs share a few inks (the settings page has three, interleaved), so a few
        ramps are kept for the frame, and ui_clear, which begins each frame's picture, makes them all again. Keyed by all
        three, a ramp kept is the one a fresh run would have made.
*/
#define UI_RAMPS 4
struct ui_ramp
{
        unsigned int colour, alpha, ground;
        b32 set;
        unsigned int tone[256], over[256];
        unsigned long long known[4], made[4];
};
static struct ui_ramp ui_ramps[UI_RAMPS];
static unsigned int ui_ramps_next;

static struct ui_ramp address_to ui_ramp_for(unsigned int colour, unsigned int alpha, unsigned int ground)
{
        struct ui_ramp address_to ramp;

        for (int k = 0; k < UI_RAMPS; k++)
                if (ui_ramps[k].set && ui_ramps[k].colour == colour && ui_ramps[k].alpha == alpha && ui_ramps[k].ground == ground)
                        return &ui_ramps[k];
        ramp = &ui_ramps[ui_ramps_next++ % UI_RAMPS];
        ramp->set = 1, ramp->colour = colour, ramp->alpha = alpha, ramp->ground = ground;
        memory_zero(ramp->known, sizeof(ramp->known));
        memory_zero(ramp->made, sizeof(ramp->made));
        return ramp;
}

/*
        One premultiplied pixel over another, the blend every piece of the kit lays on the picture. On x86-64 and arm64 it
        is one out-of-line body of assembly, ui_over_body, which the callers reach through this inline wrapper. The C
        below is the body on riscv64, which measured no fewer instructions in assembly (a whole settings frame came out
        0.4% more), and it is the reference the check in test/checks.c compares the bodies with. The bodies are the same
        arithmetic, branch free after the early outs: a lane that came past 255 is pinned to 255 by its high bit, which
        is what the lane-by-lane clamp gives.
*/
#if (X64 || ARM64) && !defined(UI_OVER_C)
#define UI_OVER_ASM 1
#endif
#if (X64 || ARM64) && !defined(UI_OVER_C)
#define UI_KIT_PUT_ASM 1
#endif

#ifdef UI_OVER_ASM
extern unsigned int ui_over_body(unsigned int pixel, unsigned int under) __asm__("ui_over_body")
        __attribute__((visibility("hidden")));

static inline unsigned int ui_over(unsigned int pixel, unsigned int under)
{
        return ui_over_body(pixel, under);
}

#if X64
asm(".pushsection .text\n"
    ".balign 16\n"
    ".local ui_over_body\n"
    ".type ui_over_body, " ASM_TYPE "\n"
    "ui_over_body:\n" ASM_ENDBR
    /*
            edi pixel, esi under; the result in eax. A zero alpha is under. The four channels are 16-bit lanes in
            xmm0 (under) and xmm1 (pixel): under times rest (255 - alpha) plus 128, then ((x) * 257) >> 16 is that
            lane's share of 255: the same as the C's (x + 1 + (x >> 8)) >> 8 with x = under * rest + 127, for every x up
            to 65152 (checked over the whole range). The pixel's channel is added, and packuswb pins the lane to 255.
    */
    "mov %edi, %ecx\n   shr $24, %ecx\n   jz 2f\n"
    "mov $255, %eax\n   sub %ecx, %eax\n   movd %eax, %xmm3\n   pshuflw $0, %xmm3, %xmm3\n"
    "movd %esi, %xmm0\n   movd %edi, %xmm1\n   pxor %xmm2, %xmm2\n"
    "punpcklbw %xmm2, %xmm0\n   punpcklbw %xmm2, %xmm1\n"
    "pmullw %xmm3, %xmm0\n   paddw ui_over_128(%rip), %xmm0\n   pmulhuw ui_over_257(%rip), %xmm0\n"
    "paddw %xmm1, %xmm0\n   packuswb %xmm0, %xmm0\n   movd %xmm0, %eax\n"
    ASM_RET
    "2:  mov %esi, %eax\n   " ASM_RET
    ".balign 16\n"
    "ui_over_128: .short 128, 128, 128, 128, 128, 128, 128, 128\n"
    "ui_over_257: .short 257, 257, 257, 257, 257, 257, 257, 257\n"
    ".size ui_over_body, .-ui_over_body\n"
    ".popsection\n");
/*
        ui_kit_put_body: rdi the pixel, esi the cover (UI_FULL is 4096), edx the colour. A cover of nothing is nothing;
        a whole cover of an opaque colour is the colour; otherwise the alpha is alpha times cover over 4096, the colour
        premultiplied by it (window_mix: each channel times alpha plus alpha >> 7 in 256ths, rounded) and laid over the
        pixel by ui_over_body's arithmetic. A tone of nothing is the pixel, so a zero alpha writes nothing.
*/
asm(".pushsection .text\n"
    ".balign 16\n"
    ".local ui_kit_put_body\n"
    ".type ui_kit_put_body, " ASM_TYPE "\n"
    "ui_kit_put_body:\n" ASM_ENDBR
    "test %esi, %esi\n   jz 9f\n"
    "cmp $4096, %esi\n   jb 2f\n"
    "mov %edx, %eax\n   shr $24, %eax\n   cmp $255, %eax\n   jne 1f\n"
    "mov %edx, (%rdi)\n" ASM_RET
    "1:  mov $4096, %esi\n"
    "2:  mov %edx, %eax\n   shr $24, %eax\n   imul %esi, %eax\n   shr $12, %eax\n   jz 9f\n"
    "mov %eax, %ecx\n   shr $7, %ecx\n   add %eax, %ecx\n"
    "movd %ecx, %xmm1\n   pshuflw $0, %xmm1, %xmm1\n"
    "pxor %xmm2, %xmm2\n   movd %edx, %xmm0\n   punpcklbw %xmm2, %xmm0\n"
    "pmullw %xmm1, %xmm0\n   paddw ui_over_128(%rip), %xmm0\n   psrlw $8, %xmm0\n   pinsrw $3, %eax, %xmm0\n"
    "movd (%rdi), %xmm1\n   punpcklbw %xmm2, %xmm1\n"
    "mov $255, %ecx\n   sub %eax, %ecx\n   movd %ecx, %xmm3\n   pshuflw $0, %xmm3, %xmm3\n"
    "pmullw %xmm3, %xmm1\n   paddw ui_over_128(%rip), %xmm1\n   pmulhuw ui_over_257(%rip), %xmm1\n"
    "paddw %xmm0, %xmm1\n   packuswb %xmm1, %xmm1\n   movd %xmm1, (%rdi)\n"
    "9:  " ASM_RET
    ".size ui_kit_put_body, .-ui_kit_put_body\n"
    ".popsection\n");
#elif ARM64
asm(".pushsection .text\n"
    ".balign 16\n"
    ".local ui_over_body\n"
    ".type ui_over_body, " ASM_TYPE "\n"
    "ui_over_body:\n" ASM_ENDBR
    /*
            w0 pixel, w1 under; the result in w0. A zero alpha is under. The lanes are as on x86-64: under times rest in
            16-bit lanes, + 128, then ((x) * 257) >> 16 is the lane's share of 255 (the C's, for every x up to 65152), the
            pixel's channel is added, and sqxtun pins the lane to 255.
    */
    "lsr w2, w0, #24\n   cbz w2, 2f\n"
    "mov w3, #255\n   sub w3, w3, w2\n"
    "fmov s0, w1\n   uxtl v0.8h, v0.8b\n   dup v1.8h, w3\n   mul v0.8h, v0.8h, v1.8h\n"
    "movi v2.8h, #128\n   add v0.8h, v0.8h, v2.8h\n"
    "movz w4, #257\n   dup v3.4h, w4\n   umull v4.4s, v0.4h, v3.4h\n   shrn v4.4h, v4.4s, #16\n"
    "fmov s5, w0\n   uxtl v5.8h, v5.8b\n   add v4.4h, v4.4h, v5.4h\n   sqxtun v4.8b, v4.8h\n"
    "fmov w0, s4\n   " ASM_RET
    "2:  mov w0, w1\n   " ASM_RET
    ".size ui_over_body, .-ui_over_body\n"
    ".popsection\n");
/*
        ui_kit_put_body, as on x86-64: x0 the pixel, w1 the cover, w2 the colour. The tone is the colour times alpha
        (cover over 4096) plus alpha >> 7, halved by 256, in the lanes; the under is laid over it with the same arithmetic.
*/
asm(".pushsection .text\n"
    ".balign 16\n"
    ".local ui_kit_put_body\n"
    ".type ui_kit_put_body, " ASM_TYPE "\n"
    "ui_kit_put_body:\n" ASM_ENDBR
    "cbz w1, 9f\n"
    "mov w3, #4096\n   cmp w1, w3\n   b.lo 2f\n"
    "lsr w4, w2, #24\n   cmp w4, #255\n   b.ne 1f\n"
    "str w2, [x0]\n" ASM_RET
    "1:  mov w1, w3\n"
    "2:  lsr w4, w2, #24\n   mul w4, w4, w1\n   lsr w4, w4, #12\n   cbz w4, 9f\n"
    "add w5, w4, w4, lsr #7\n"
    "dup v1.8h, w5\n   fmov s0, w2\n   uxtl v0.8h, v0.8b\n   mul v0.8h, v0.8h, v1.8h\n"
    "movi v2.8h, #128\n   add v0.8h, v0.8h, v2.8h\n   ushr v0.8h, v0.8h, #8\n   mov v0.h[3], w4\n"
    "ldr s1, [x0]\n   uxtl v1.8h, v1.8b\n"
    "mov w6, #255\n   sub w6, w6, w4\n   dup v3.8h, w6\n   mul v1.8h, v1.8h, v3.8h\n"
    "add v1.8h, v1.8h, v2.8h\n"
    "movz w7, #257\n   dup v4.4h, w7\n   umull v5.4s, v1.4h, v4.4h\n   shrn v5.4h, v5.4s, #16\n"
    "add v5.4h, v5.4h, v0.4h\n   sqxtun v5.8b, v5.8h\n   str s5, [x0]\n"
    "9:  " ASM_RET
    ".size ui_kit_put_body, .-ui_kit_put_body\n"
    ".popsection\n");
#endif

#else
// One premultiplied pixel over another.
static inline unsigned int ui_over(unsigned int pixel, unsigned int under)
{
        unsigned int rest = 255 - (pixel >> 24), out = 0;

        if (!(pixel >> 24))
                return under;
        if (!rest)
                return pixel;
        // Over nothing (a picture cleared to the glass), the pixel is the pixel: the blend adds nothing.
        if (!under)
                return pixel;
        /*
                The four channels in two words of 16-bit lanes (red and blue, green and alpha): under times rest plus 127
                in each lane is at most 65152, so no lane carries into the next, and ui_div255 is one shift and an add.
                A sum past 255 in a lane (a tone that is not premultiplied) is clamped the long way.
        */
        {
                unsigned int v_rb = (under & 0x00ff00ffu) * rest + 0x007f007fu;
                unsigned int v_ag = ((under >> 8) & 0x00ff00ffu) * rest + 0x007f007fu;
                unsigned int sum_rb = (pixel & 0x00ff00ffu) + ((v_rb + 0x00010001u + ((v_rb >> 8) & 0x00ff00ffu)) >> 8 & 0x00ff00ffu);
                unsigned int sum_ag = ((pixel >> 8) & 0x00ff00ffu) + ((v_ag + 0x00010001u + ((v_ag >> 8) & 0x00ff00ffu)) >> 8 & 0x00ff00ffu);

                if (!((sum_rb | sum_ag) & 0x01000100u))
                        return (sum_rb & 0x00ff00ffu) | (sum_ag & 0x00ff00ffu) << 8;
        }
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                unsigned int channel = (pixel >> shift & 255) + ui_div255((under >> shift & 255) * rest + 127);

                out |= (channel > 255 ? 255 : channel) << shift;
        }
        return out;
}
#endif

// A tone at cover 255ths of itself.
static unsigned int ui_cover(unsigned int tone, unsigned int cover)
{
        unsigned int out = 0;

        if (cover >= 255)
                return tone;
        for (unsigned int shift = 0; shift < 32; shift += 8)
                out |= ui_div255((tone >> shift & 255) * cover + 127) << shift;
        return out;
}

static fn ui_dot(struct ui address_to ui, int x, int y, unsigned int tone)
{
        unsigned int address_to at;

        if (x < 0 || x >= ui->width || y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y))
                return;
        at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x;
        address_to at = ui_over(tone, address_to at);
}

static unsigned int ui_role_surface(void);

// The whole picture clear: the glass alone (the kit's surface for an opaque window); within a repaint's band, its rows.
static fn ui_clear(struct ui address_to ui)
{
        int top = 0, bottom = ui->height;

        for (int k = 0; k < UI_RAMPS; k++)
                ui_ramps[k].set = 0;
        if (ui->band_bottom > ui->band_top)
        {
                top = ui->band_top < 0 ? 0 : ui->band_top;
                bottom = ui->band_bottom > ui->height ? ui->height : ui->band_bottom;
        }
        if (bottom > top)
                memory_fill_u32_rect(ui->pixels + (unsigned long)top * ui->pitch, (positive)ui->pitch, (positive)ui->width,
                                     (positive)(bottom - top), ui->opaque ? ui_role_surface() : 0);
}

// The picture of a whole window, width by height, its rows pitch pixels apart, cleared to the glass.
static fn ui_begin(struct ui address_to ui, unsigned int address_to pixels, unsigned long pitch, int width, int height)
{
        ui->pixels = pixels;
        ui->pitch = pitch;
        ui->width = width;
        ui->height = height;
        ui->top = 0;
        ui->bottom = height;
        ui_clear(ui);
}

static fn ui_fill(struct ui address_to ui, int x, int y, int width, int height, unsigned int tone)
{
        int x2 = x + width, y2 = y + height;

        x = x < 0 ? 0 : x;
        y = y < ui->top ? ui->top : y;
        x2 = x2 > ui->width ? ui->width : x2;
        y2 = y2 > ui->bottom ? ui->bottom : y2;
        y2 = y2 > ui->height ? ui->height : y2;
        if (ui->band_bottom > ui->band_top)
        {
                y = y < ui->band_top ? ui->band_top : y;
                y2 = y2 > ui->band_bottom ? ui->band_bottom : y2;
        }
        // The rows are the picture's: a clip row or a band above the picture's first row is cut to it.
        y = y < 0 ? 0 : y;
        // An opaque tone over a pixel is that tone: the rectangle is one fill.
        if (tone >> 24 == 255)
        {
                if (x2 > x && y2 > y)
                        memory_fill_u32_rect(ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x, (positive)ui->pitch,
                                             (positive)(x2 - x), (positive)(y2 - y), tone);
                return;
        }
        if (x2 > x)
                for (int row = y; row < y2; row++)
                        memory_over_tone_u32(ui->pixels + (unsigned long)row * ui->pitch + (unsigned long)x, (positive)(x2 - x), tone);
}

/*
        How much of a pixel a rounded rectangle covers, in 255ths: sixteen
        sample points, those in a corner's square counted inside its quarter
        circle. x and y are the pixel's place from the rectangle's top left.
*/
static unsigned int ui_inside(int x, int y, int width, int height, int radius)
{
        unsigned int inside = 0;

        if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
        if ((x >= radius && x < width - radius) || (y >= radius && y < height - radius))
                return 255;

        for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                        // In eighths of a pixel, from the nearest corner's circle's centre. In 64 bits: past a radius of
                        // 5792 px the squares leave int (the corner's sides are two radii and more).
                        b64 px = 8 * (b64)x + 2 * sx + 1, py = 8 * (b64)y + 2 * sy + 1;
                        b64 cx = x < radius ? 8 * (b64)radius : 8 * (b64)(width - radius);
                        b64 cy = y < radius ? 8 * (b64)radius : 8 * (b64)(height - radius);
                        b64 dx = px - cx, dy = py - cy;

                        inside += dx * dx + dy * dy <= 64 * (b64)radius * (b64)radius;
                }
        return (inside * 255 + 8) / 16;
}

/*
        A corner's cover depends on its radius alone: the radius is already in device pixels, and ui_inside reads no
        other input. Its sample points are symmetric about the middle of the corner's square, so a pixel of any corner
        samples the same circle as the pixel mirrored across that middle into the top left quarter. That quarter is made
        once for each radius, the first time the radius is drawn, and kept: radius r at the sum of the squares below it,
        so the masks of every radius up to UI_CORNER_MOST take UI_CORNER_BYTES (22140), and a larger radius is not kept
        (ui_inside answers it, as before). A radius is one slot, so no two radii share one.
*/
#define UI_CORNER_MOST 40
#define UI_CORNER_BYTES (UI_CORNER_MOST * (UI_CORNER_MOST + 1) * (2 * UI_CORNER_MOST + 1) / 6)
static unsigned char ui_corner_masks[UI_CORNER_BYTES];
static unsigned char ui_corner_made[UI_CORNER_MOST + 1];

static const unsigned char address_to ui_corner_mask(int radius)
{
        unsigned char address_to mask = ui_corner_masks + (unsigned long)(radius - 1) * radius * (2 * radius - 1) / 6;

        if (!ui_corner_made[radius])
        {
                // The quarter of a corner of radius, read from ui_inside itself: its square is 2 radius across.
                for (int y = 0; y < radius; y++)
                        for (int x = 0; x < radius; x++)
                                mask[y * radius + x] = (unsigned char)ui_inside(x, y, 2 * radius, 2 * radius, radius);
                ui_corner_made[radius] = 1;
        }
        return mask;
}

// ui_inside, read from a corner's mask where the pixel is in a corner's square. A pixel left of the middle or above it
// is its own place in the top left quarter; one to the right or below is mirrored across the middle.
static unsigned int ui_inside_cached(int x, int y, int width, int height, int radius)
{
        int mx, my;

        if (radius < 1 || radius > UI_CORNER_MOST)
                return ui_inside(x, y, width, height, radius);
        if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
        if ((x >= radius && x < width - radius) || (y >= radius && y < height - radius))
                return 255;
        mx = x < radius ? x : width - 1 - x;
        my = y < radius ? y : height - 1 - y;
        return ui_corner_mask(radius)[my * radius + mx];
}

// A rounded rectangle, filled.
static fn ui_round(struct ui address_to ui, int x, int y, int width, int height, int radius, unsigned int tone)
{
        radius = radius * 2 > width ? width / 2 : radius;
        radius = radius * 2 > height ? height / 2 : radius;
        for (int row = 0; row < height; row++)
        {
                // The straight middle rows are one fill.
                if (row >= radius && row < height - radius)
                {
                        int last = height - radius;

                        ui_fill(ui, x, y + row, width, last - row, tone);
                        row = last - 1;
                        continue;
                }
                // A row outside the band is not walked; a corner row's straight middle is one fill, as its cover is all of it.
                if (!ui_in_band(ui, y + row))
                        continue;
                for (int column = 0; column < width; column++)
                {
                        unsigned int cover;

                        if (column == radius && width - radius > radius)
                        {
                                ui_fill(ui, x + radius, y + row, width - 2 * radius, 1, tone);
                                column = width - radius - 1;
                                continue;
                        }
                        cover = ui_inside_cached(column, row, width, height, radius);

                        if (cover)
                                ui_dot(ui, x + column, y + row, ui_cover(tone, cover));
                }
        }
}

/*
        A run laid a code point at a time: the pen after the last glyph, which
        is the advance font_draw answers for the run so far, kerning and all.
        The same steps font_draw takes, without the coverage.
*/
struct ui_pen
{
        b64 reach;        // the pen after the last glyph, in 16.16
        b64 cell;         // the advance of a cell the font does not know
        p32 glyph, face;  // the last glyph, and the face it is from (FONT_DRAWN for none)
};

static void ui_pen_start(const font_face address_to const address_to faces, p32 em, struct ui_pen address_to pen)
{
        pen->reach = 0;
        pen->cell = (b64)font_cell_width(faces[0], em) << 16;
        pen->glyph = 0;
        pen->face = FONT_DRAWN;
}

static void ui_pen_step(struct ui address_to ui, const font_face address_to const address_to faces, p32 em, p32 code,
                        struct ui_pen address_to pen)
{
        p32 face, glyph;
        b64 advance;

        font_find(ui->engine, faces, 2, code, &face, &glyph);
        if (face != FONT_DRAWN && pen->face == face)
                pen->reach += font_kern(ui->engine, faces[face], pen->glyph, glyph, em);
        advance = face == FONT_DRAWN ? pen->cell : font_advance(faces[face], glyph, em);
        pen->reach += advance;
        pen->glyph = glyph;
        pen->face = face;
}

static int ui_pen_width(const struct ui_pen address_to pen)
{
        return (int)(pen->reach >> 16);
}

// A run's advance in whole pixels, laid out and not drawn.
static int ui_advance(struct ui address_to ui, const font_face address_to const address_to faces, string_address text,
                      positive length, p32 em)
{
        struct ui_pen pen;
        positive at = 0;

        ui_pen_start(faces, em, &pen);
        while (at < length)
        {
                p32 code;

                at = font_decode((const p8 address_to)text, length, at, &code);
                ui_pen_step(ui, faces, em, code, &pen);
        }
        return ui_pen_width(&pen);
}

/*
        A run of text drawn with its pen at x on the baseline in colour (alpha
        255 is opaque ink, less a fainter one), and its width answered. Only
        the columns from low to high are drawn: a field's text is cut at its
        well, and the rest of the run is not. A run wholly outside the band
        of a repaint is measured and not drawn.
*/
/*
        The run in a face of the caller's (faces is it and the regular, for what
        it has not), with a stroke: the kit's -webkit-text-stroke .5px is 64, a
        pixel taking half its strongest neighbour's cover (measured against
        Chrome's stroke by eye on the filmstrips), which never moves a glyph off
        its whole pixel. 0 is no stroke.
*/
static int ui_run_in(struct ui address_to ui, const font_face address_to const address_to faces, int x, int baseline,
                     string_address text, int size, unsigned int colour, unsigned int alpha, int low, int high,
                     unsigned int stroke);

static int ui_run(struct ui address_to ui, int x, int baseline, string_address text, int size, b32 bold,
                  unsigned int colour, unsigned int alpha, int low, int high)
{
        const font_face address_to faces[2] = {bold && ui->bold ? ui->bold : ui->regular, ui->regular};

        return ui_run_in(ui, faces, x, baseline, text, size, colour, alpha, low, high, 0);
}

/*
        The walk of a run's rows and columns (ui_run_in's, after its scratch is drawn): each row in the band, each column from
        first to last, the coverage of the column as the cover of its pixel (the ink's cover, with the stroke's share of a
        neighbour), laid over the picture by the ramp -- the over of the cover on the ground, made the first time the walk
        meets it (ui_run_make_over), else the tone of the cover (ui_run_make_tone) laid over what is there by ui_run_blend.
        A paper run is skipped a vector at a time. On x86-64 the walk is ui_run_walk's body, which reads this struct at the
        offsets the asserts below pin; elsewhere ui_run_in's own loop does it, and checks.c holds that loop as the reference.
*/
struct ui_walk
{
        unsigned int address_to scratch;   // the coverage, row 0: wide words a row
        long wide, rows, top, first, last; // the coverage's width and rows, the y of row 0, the columns walked
        unsigned int address_to pixels;    // the picture
        long pitch, pen_x;                 // its pitch in words, and the pen's x in device pixels
        long ui_top, ui_bottom, ui_height; // the rows the ui may draw
        long band_top, band_bottom;        // a repaint's band (none when band_bottom <= band_top)
        unsigned long paper, ground;       // the paper the coverage is clear to, and the ground the picture is cleared to
        unsigned long flip;                // 255 for a dark ink (cover = 255 - value), 0 for a light one
        unsigned long stroke;              // the stroke, 0 for none
        struct ui_ramp address_to ramp;
};
_Static_assert(__builtin_offsetof(struct ui_walk, scratch) == 0 && __builtin_offsetof(struct ui_walk, wide) == 8 &&
               __builtin_offsetof(struct ui_walk, rows) == 16 && __builtin_offsetof(struct ui_walk, top) == 24 &&
               __builtin_offsetof(struct ui_walk, first) == 32 && __builtin_offsetof(struct ui_walk, last) == 40 &&
               __builtin_offsetof(struct ui_walk, pixels) == 48 && __builtin_offsetof(struct ui_walk, pitch) == 56 &&
               __builtin_offsetof(struct ui_walk, pen_x) == 64 && __builtin_offsetof(struct ui_walk, ui_top) == 72 &&
               __builtin_offsetof(struct ui_walk, ui_bottom) == 80 && __builtin_offsetof(struct ui_walk, ui_height) == 88 &&
               __builtin_offsetof(struct ui_walk, band_top) == 96 && __builtin_offsetof(struct ui_walk, band_bottom) == 104 &&
               __builtin_offsetof(struct ui_walk, paper) == 112 && __builtin_offsetof(struct ui_walk, ground) == 120 &&
               __builtin_offsetof(struct ui_walk, flip) == 128 && __builtin_offsetof(struct ui_walk, stroke) == 136 &&
               __builtin_offsetof(struct ui_walk, ramp) == 144,
               "ui_run_walk reads struct ui_walk at fixed offsets");
_Static_assert(__builtin_offsetof(struct ui_ramp, tone) == 16 && __builtin_offsetof(struct ui_ramp, over) == 1040 &&
               __builtin_offsetof(struct ui_ramp, known) == 2064 && __builtin_offsetof(struct ui_ramp, made) == 2096,
               "ui_run_walk reads struct ui_ramp at fixed offsets");

// The walk's out-of-line parts: a cover's tone, and its over the ground, made the first time it is met, and the blend.
__attribute__((used, externally_visible, noinline)) void ui_run_make_tone(struct ui_ramp address_to ramp, unsigned int cover)
{
        ramp->tone[cover] = ui_tone(ramp->colour, cover * ramp->alpha / 255);
        ramp->known[cover >> 6] |= 1ull << (cover & 63);
}

__attribute__((used, externally_visible, noinline)) void ui_run_make_over(struct ui_ramp address_to ramp, unsigned int cover)
{
        ramp->over[cover] = ui_over(ui_tone(ramp->colour, cover * ramp->alpha / 255), ramp->ground);
        ramp->made[cover >> 6] |= 1ull << (cover & 63);
}

__attribute__((used, externally_visible, noinline)) unsigned int ui_run_blend(unsigned int pixel, unsigned int under)
{
        return ui_over(pixel, under);
}

void ui_run_walk(struct ui_walk address_to walk);

#if X64
/*
        ui_run_walk on x86-64, in the registers: rbx the walk, r12 the row, r13 the column, r14 the picture's pixel for column 0
        of the row, r15 the coverage's, rbp the cover across the calls. The stack holds the alignment only.
*/
__asm__(
    ASM_FUNC(ui_run_walk)
    "push %rbx\n   push %rbp\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n   sub $8, %rsp\n"
    "mov %rdi, %rbx\n"
    "mov 32(%rbx), %r13\n   cmp 40(%rbx), %r13\n   jge .Lrw_done\n"
    "xor %r12d, %r12d\n"
    ".Lrw_row:\n"
    "cmp 16(%rbx), %r12\n   jge .Lrw_done\n"
    "mov 24(%rbx), %rax\n   add %r12, %rax\n"
    "cmp 72(%rbx), %rax\n   jl .Lrw_next_row\n"
    "cmp 80(%rbx), %rax\n   jge .Lrw_next_row\n"
    "cmp 88(%rbx), %rax\n   jge .Lrw_next_row\n"
    "mov 104(%rbx), %rcx\n   cmp 96(%rbx), %rcx\n   jle .Lrw_band\n"
    "cmp 96(%rbx), %rax\n   jl .Lrw_next_row\n"
    "cmp 104(%rbx), %rax\n   jge .Lrw_next_row\n"
    ".Lrw_band:\n"
    "mov 56(%rbx), %rcx\n   imul %rax, %rcx\n   add 64(%rbx), %rcx\n   mov 48(%rbx), %r14\n   lea (%r14,%rcx,4), %r14\n"
    "mov 8(%rbx), %rcx\n   mov %r12, %rdx\n   imul %rcx, %rdx\n   mov (%rbx), %r15\n   lea (%r15,%rdx,4), %r15\n"
    "mov 32(%rbx), %r13\n"
    ".Lrw_col:\n"
    "cmp 40(%rbx), %r13\n   jge .Lrw_next_row\n"
    "cmpq $0, 136(%rbx)\n   jne .Lrw_value\n"
    "mov (%r15,%r13,4), %eax\n   mov 112(%rbx), %rdx\n   cmp %edx, %eax\n   jne .Lrw_value\n"
    "lea (%r15,%r13,4), %rdi\n   mov 40(%rbx), %rsi\n   sub %r13, %rsi\n"
    "call memory_first_differs_u32\n"
    "add %rax, %r13\n   cmp 40(%rbx), %r13\n   jge .Lrw_next_row\n"
    ".Lrw_value:\n"
    "mov (%r15,%r13,4), %eax\n   shr $8, %eax\n   and $255, %eax\n   xor 128(%rbx), %eax\n"
    "cmpq $0, 136(%rbx)\n   je .Lrw_cover\n"
    "cmp $255, %eax\n   jae .Lrw_cover\n"
    "xor %r9d, %r9d\n"
    "test %r13, %r13\n   jz .Lrw_s1\n   mov -4(%r15,%r13,4), %edx\n"
    "shr $8, %edx\n   and $255, %edx\n   xor 128(%rbx), %edx\n   cmp %r9d, %edx\n   cmova %edx, %r9d\n"
    ".Lrw_s1:\n"
    "lea 1(%r13), %rdx\n   cmp 8(%rbx), %rdx\n   jge .Lrw_s2\n   mov 4(%r15,%r13,4), %edx\n"
    "shr $8, %edx\n   and $255, %edx\n   xor 128(%rbx), %edx\n   cmp %r9d, %edx\n   cmova %edx, %r9d\n"
    ".Lrw_s2:\n"
    "test %r12, %r12\n   jz .Lrw_s3\n"
    "mov 8(%rbx), %rdx\n   shl $2, %rdx\n   mov %r15, %rsi\n   sub %rdx, %rsi\n   mov (%rsi,%r13,4), %edx\n"
    "shr $8, %edx\n   and $255, %edx\n   xor 128(%rbx), %edx\n   cmp %r9d, %edx\n   cmova %edx, %r9d\n"
    ".Lrw_s3:\n"
    "lea 1(%r12), %rdx\n   cmp 16(%rbx), %rdx\n   jge .Lrw_s4\n"
    "mov 8(%rbx), %rdx\n   shl $2, %rdx\n   mov %r15, %rsi\n   add %rdx, %rsi\n   mov (%rsi,%r13,4), %edx\n"
    "shr $8, %edx\n   and $255, %edx\n   xor 128(%rbx), %edx\n   cmp %r9d, %edx\n   cmova %edx, %r9d\n"
    ".Lrw_s4:\n"
    "mov %r9d, %edx\n   imull 136(%rbx), %edx\n   shr $7, %edx\n   add %edx, %eax\n"
    "cmp $255, %eax\n   jbe .Lrw_cover\n   mov $255, %eax\n"
    ".Lrw_cover:\n"
    "test %eax, %eax\n   jz .Lrw_next\n"
    "mov %eax, %ebp\n"
    "mov 144(%rbx), %rsi\n"
    "mov %ebp, %ecx\n   and $63, %ecx\n"
    "mov %ebp, %edx\n   shr $6, %edx\n"
    "mov (%r14,%r13,4), %r8d\n"
    "cmp 120(%rbx), %r8d\n   jne .Lrw_known\n"
    "mov 2096(%rsi,%rdx,8), %rax\n   bt %rcx, %rax\n   jnc .Lrw_make_over\n"
    ".Lrw_have_over:\n"
    "mov 1040(%rsi,%rbp,4), %eax\n   mov %eax, (%r14,%r13,4)\n   jmp .Lrw_next\n"
    ".Lrw_make_over:\n"
    "mov %rsi, %rdi\n   mov %ebp, %esi\n   call ui_run_make_over\n"
    "mov 144(%rbx), %rsi\n   jmp .Lrw_have_over\n"
    ".Lrw_known:\n"
    "mov 2064(%rsi,%rdx,8), %rax\n   bt %rcx, %rax\n   jc .Lrw_blend\n"
    "mov %rsi, %rdi\n   mov %ebp, %esi\n   call ui_run_make_tone\n"
    "mov 144(%rbx), %rsi\n"
    ".Lrw_blend:\n"
    "mov 16(%rsi,%rbp,4), %edi\n   mov (%r14,%r13,4), %esi\n   call ui_run_blend\n"
    "mov %eax, (%r14,%r13,4)\n"
    ".Lrw_next:\n"
    "inc %r13\n   jmp .Lrw_col\n"
    ".Lrw_next_row:\n"
    "inc %r12\n   jmp .Lrw_row\n"
    ".Lrw_done:\n"
    "add $8, %rsp\n   pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbp\n   pop %rbx\n"
    ASM_RET
    ASM_END(ui_run_walk)
);
#endif

static int ui_run_in(struct ui address_to ui, const font_face address_to const address_to faces, int x, int baseline,
                     string_address text, int size, unsigned int colour, unsigned int alpha, int low, int high,
                     unsigned int stroke)
{
        p32 em = (p32)(size * ui->scale) << 6;
        positive length = string_length(text);
        int width = ui_advance(ui, faces, text, length, em);
        font_metrics metrics = font_metrics_at(faces[0], em);
        int above = metrics.baseline + 2 * ui->scale, rows = metrics.cell_height + 4 * ui->scale;
        int left = 2 * ui->scale, wide = width + 2 * left, top = baseline - above;
        b32 light = window_light(colour); // light ink: drawn white on black, so the coverage is the light text's own
        unsigned int paper = light ? 0 : 0xffffffu;

        if (!alpha || !ui->scratch)
                return width;
        wide = wide > ui->scratch_width ? ui->scratch_width : wide;
        rows = rows > UI_SCRATCH_ROWS * ui->scale ? UI_SCRATCH_ROWS * ui->scale : rows;
        if (ui->band_bottom > ui->band_top && (top + rows <= ui->band_top || top >= ui->band_bottom))
                return width;

        memory_fill_u32_rect(ui->scratch, (positive)wide, (positive)wide, (positive)rows, paper);
        {
                font_target line = {ui->scratch, (positive)wide, wide, rows};

                font_draw(ui->engine, faces, 2, (const p8 address_to)text, length, em, 0, &line, (b32)left << 16, above,
                          light ? 0xffffffu : 0, paper);
        }

        // The coverage, as the ink at that alpha, over the picture: one tone for each coverage, made once. The columns
        // drawn are those in low to high and in the picture, worked out once, so the walk over a row has no test in it.
        //
        // Text lies mostly on one ground (the surface the band was cleared to, or a fill under it): the tone over that
        // ground is kept for each coverage as it is first met, so a pixel on it is a compare and a load, and only a
        // pixel on anything else is laid over (ui_over) -- the same pixel either way.
        {
                unsigned int ground = ui->opaque ? ui_role_surface() : 0;
                struct ui_ramp address_to ramp = ui_ramp_for(colour, alpha, ground);
                int pen_x = x - left, first = 0, last = wide;

                if (low - pen_x > first)
                        first = low - pen_x;
                if (high - pen_x < last)
                        last = high - pen_x;
                if (-pen_x > first)
                        first = -pen_x;
                if (ui->width - pen_x < last)
                        last = ui->width - pen_x;
#if X64
                {
                        struct ui_walk walk = {.scratch = ui->scratch, .wide = wide, .rows = rows, .top = top, .first = first,
                                               .last = last, .pixels = ui->pixels, .pitch = (long)ui->pitch, .pen_x = pen_x,
                                               .ui_top = ui->top, .ui_bottom = ui->bottom, .ui_height = ui->height,
                                               .band_top = ui->band_top, .band_bottom = ui->band_bottom, .paper = paper,
                                               .ground = ground, .flip = light ? 0 : 255, .stroke = stroke, .ramp = ramp};

                        ui_run_walk(&walk);
                }
#else
                for (int row = 0; row < rows && first < last; row++)
                {
                        int y = top + row;
                        const unsigned int address_to coverage = ui->scratch + (unsigned long)row * (unsigned long)wide;
                        unsigned int address_to picture;

                        if (y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y))
                                continue;
                        picture = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)(pen_x + first);
                        for (int column = first; column < last; column++, picture++)
                        {
                                unsigned int value, cover, bit;

                                // Paper no glyph touched (most of the run's rows) is no cover: passed a vector at a time.
                                // A stroke reaches into it from a neighbour, and reads every pixel.
                                if (!stroke && coverage[column] == paper)
                                {
                                        int from = column;

                                        column += (int)memory_first_differs_u32(coverage + column, (positive)(last - column), paper);
                                        picture += column - from;
                                        if (column >= last)
                                                break;
                                }
                                value = coverage[column] >> 8 & 255;
                                cover = light ? value : 255 - value;
                                if (stroke && cover < 255)
                                {
                                        // The stroke: a pixel takes a share of its strongest neighbour's cover (left, right,
                                        // above, below, those inside the run's scratch). A pixel of paper among paper takes none.
                                        const unsigned int address_to here = coverage + column;
                                        unsigned int most = 0, n[4], have = 0;

                                        if (column > 0)
                                                n[have++] = here[-1];
                                        if (column + 1 < wide)
                                                n[have++] = here[1];
                                        if (row > 0)
                                                n[have++] = here[-wide];
                                        if (row + 1 < rows)
                                                n[have++] = here[wide];
                                        for (unsigned int k = 0; k < have; k++)
                                        {
                                                unsigned int v = n[k] >> 8 & 255, c = light ? v : 255 - v;

                                                most = c > most ? c : most;
                                        }
                                        cover += most * stroke / 128;
                                        cover = cover > 255 ? 255 : cover;
                                }
                                bit = cover & 63;

                                if (!cover)
                                        continue;
                                if (*picture == ground)
                                {
                                        if (!(ramp->made[cover >> 6] >> bit & 1))
                                        {
                                                ramp->over[cover] = ui_over(ui_tone(colour, cover * alpha / 255), ground);
                                                ramp->made[cover >> 6] |= 1ull << bit;
                                        }
                                        *picture = ramp->over[cover];
                                        continue;
                                }
                                if (!(ramp->known[cover >> 6] >> bit & 1))
                                {
                                        ramp->tone[cover] = ui_tone(colour, cover * alpha / 255);
                                        ramp->known[cover >> 6] |= 1ull << bit;
                                }
                                *picture = ui_over(ramp->tone[cover], *picture);
                        }
                }
#endif
        }
        return width;
}

// A run's capital height at size, in whole pixels: font.c's metrics are 16.16.
static int ui_capital(struct ui address_to ui, int size)
{
        font_metrics metrics = font_metrics_at(ui->regular, (p32)(size * ui->scale) << 6);

        return (int)((metrics.cap_height + 32768) >> 16);
}

// A logical size in pixels at the window's scale.
static int ui_px(const struct ui address_to ui, int logical)
{
        return logical * ui->scale;
}

/*
        A finger, and a press. A finger on a touch screen (WINDOW_KEY_TOUCH on what the compositor sends) is wider than
        a pointer and shakes as it lands, so what it can press is at least UI_TARGET logical pixels each way however
        small it is drawn, the drawn shape in the middle of that (ui_hit); a press held moves nothing until it is
        UI_SLOP from where it went down, and past that it is a drag (the list follows the finger, ui_drag) and no
        click; and a press the compositor takes back (WINDOW_KEY_CANCEL: a second finger, or one held for the right
        button) is no click either. What is under a press is shown pressed the moment it goes down, not when it comes
        up, for a mouse's press as for a finger's; the app numbers what can be pressed as it likes (a row, a button):

            ui_press_begin(press, x, y, target, touch)  a button or a finger went down on target (-1 for nothing)
            ui_press_move(ui, press, x, y)              true once the press is a drag: target is pressed no more
            ui_press_end(press, cancelled)              what was clicked as the press comes up, or -1
            ui_pressed(press, target)                   whether target is shown pressed now

        How pressed looks is the kit's state layer: a list's row shows it (ui_list's down), a menu's choice too.
*/
#define UI_TARGET 44 // a finger's least target in logical pixels: about 9 mm at the 96 to 120 dpi of most screens
#define UI_SLOP 8    // how far a press moves before it is a drag, as the compositor's finger (COMPOSITOR_TOUCH_SLOP)

struct ui_press
{
        int x, y, target;        // where it went down and on what (-1 nothing)
        b32 down, touch, dragged;
};

static fn ui_press_begin(struct ui_press address_to press, int x, int y, int target, b32 touch)
{
        press->x = x, press->y = y, press->target = target;
        press->down = true, press->touch = touch, press->dragged = false;
}

static b32 ui_press_move(const struct ui address_to ui, struct ui_press address_to press, int x, int y)
{
        long dx = (long)x - press->x, dy = (long)y - press->y, slop = UI_SLOP * (long)ui->scale;

        if (press->down && !press->dragged && dx * dx + dy * dy >= slop * slop)
                press->dragged = true;
        return press->down && press->dragged;
}

static int ui_press_end(struct ui_press address_to press, b32 cancelled)
{
        int clicked = press->down && !press->dragged && !cancelled ? press->target : -1;

        press->down = false;
        press->target = -1;
        return clicked;
}

static b32 ui_pressed(const struct ui_press address_to press, int target)
{
        return press->down && !press->dragged && target >= 0 && press->target == target;
}

static b32 ui_kit_touch; // the event is a finger's (WINDOW_KEY_TOUCH): what it can press is at least UI_TARGET each way

// Whether px, py hits a shape x, y, width by height: a finger's reach is the shape grown to UI_TARGET each way where it is smaller.
static b32 ui_hit(const struct ui address_to ui, b32 touch, int px, int py, int x, int y, int width, int height)
{
        int least = touch ? UI_TARGET * ui->scale : 0;

        if (width < least)
                x -= (least - width) / 2, width = least;
        if (height < least)
                y -= (least - height) / 2, height = least;
        return px >= x && px < x + width && py >= y && py < y + height;
}

/*
        A list dragged by a finger follows it, and let go while moving it goes on, slowing as a thing thrown does,
        until it stops. Speeds are pixels a second, times are the monotonic clock's nanoseconds, all whole numbers.

            ui_drag_start(drag, at, now)   the finger took the list at at (a y, or an x)
            ui_drag_follow(drag, at, now)  the finger is at at: how far the list moves, the finger's move since the last
            ui_drag_let_go(drag, now)      the finger lifted: whether the list is thrown (ui_drag_step moves it then)
            ui_drag_step(drag, now)        a throw's move since the last step, and 0 once it stopped (flying is false);
                                           the app steps it again UI_DRAG_FRAME_NS later while it flies
            ui_drag_stop(drag)             it stops at once: a press, a key, the end of the list
*/
#include "motion.h"
static inline b64 ui_qmul(b64 a, b64 b);
static b64 ui_exp_neg(b64 x);
#define UI_DRAG_FRAME_NS 16000000l
#define UI_DRAG_THROW 300   // the least speed a finger lets go at that throws the list
#define UI_DRAG_STILL 20    // the speed a throw stops at
#define UI_DRAG_SLOW_NS 325000000l // a throw's speed falls by e in this long, whatever the step (the wheel glide's rule)

struct ui_drag
{
        int last;          // where the finger was at the last move
        long speed;        // pixels a second, signed
        b64 owed;          // a throw's move not yet given, in Q30 pixels
        b64 rate;          // a throw's speed in Q30 pixels a second, the one the steps fall by e of
        positive at;       // when the last move or step was
        b32 flying;
};

static fn ui_drag_start(struct ui_drag address_to drag, int at, positive now)
{
        drag->last = at, drag->speed = 0, drag->owed = 0, drag->rate = 0, drag->at = now, drag->flying = false;
}

static int ui_drag_follow(struct ui_drag address_to drag, int at, positive now)
{
        int moved = at - drag->last;
        long gap = now > drag->at ? (long)(now - drag->at) : 0;

        // The speed is the moves of the last few reports, the newest weighed most.
        if (gap > 0)
        {
                long seen = (long)moved * 1000000000l / (gap < 4000000l ? 4000000l : gap);

                drag->speed = gap > 100000000l ? seen : (drag->speed * 2 + seen * 3) / 5;
        }
        drag->last = at;
        drag->at = now;
        return moved;
}

static b32 ui_drag_let_go(struct ui_drag address_to drag, positive now)
{
        // A finger that stopped before it lifted throws nothing.
        if (now > drag->at && now - drag->at > 80000000ul)
                drag->speed = 0;
        drag->flying = drag->speed >= UI_DRAG_THROW || drag->speed <= -UI_DRAG_THROW;
        drag->owed = 0;
        drag->rate = (b64)drag->speed * UI_ONE;
        drag->at = now;
        return drag->flying;
}

static int ui_drag_step(struct ui_drag address_to drag, positive now)
{
        long gap = now > drag->at ? (long)(now - drag->at) : 0, whole;
        b64 e, moved;

        if (!drag->flying)
                return 0;
        gap = gap > 100000000l ? 100000000l : gap;
        // The speed falls by e in UI_DRAG_SLOW_NS, from where it is, as the wheel glide's speed does: the move is the speed's
        // integral over the gap and the speed after is its fall's end. So a step of any length moves the same distance, and
        // a throw stepped at 60 Hz covers what it covers at 120 Hz (a floor per step made the faster steps slow down less).
        e = ui_exp_neg((b64)gap * UI_ONE / UI_DRAG_SLOW_NS);
        moved = ui_qmul(ui_qmul(drag->rate, UI_DRAG_SLOW_NS * UI_ONE / 1000000000ll), UI_ONE - e);
        drag->owed += moved;
        whole = (long)(drag->owed / UI_ONE);
        drag->owed -= (b64)whole * UI_ONE;
        drag->rate = ui_qmul(drag->rate, e);
        drag->speed = (long)(drag->rate / UI_ONE);
        drag->at = now;
        // It stops when it is slower than a pixel's share a second: the stop is the speed, not a round-down in the steps.
        if (drag->rate < UI_DRAG_STILL * UI_ONE && drag->rate > -UI_DRAG_STILL * UI_ONE)
                drag->flying = false;
        return (int)whole;
}

static fn ui_drag_stop(struct ui_drag address_to drag)
{
        drag->flying = false;
        drag->speed = 0;
        drag->owed = 0;
        drag->rate = 0;
}


/* ---- kit: motion ---- */

/*
        The kit's motion, Dawning Native's Rubber Band standard (its ui.js and
        ui.css), to the number. A value is Q30 in a b64 (UI_ONE is one), a time
        is nanoseconds on the monotonic clock, and nothing here is floating
        point, so the same frame times make the same values, and the same
        pictures, on x86-64, arm64 and riscv64. The host check (test/checks.c,
        the kit rows) holds every curve below to numbers the kit's own
        JavaScript produced at the same frame times, at 60 Hz, at 120 Hz and at
        an uneven pace.

        The pieces, each the one in ui.js or ui.css it is named for:

            spring      P(): the release, a damped ring from --rb-spring-freq
                        4.905 rad/s and --rb-spring-decay 24.164 /s; a channel
                        rests below 0.0001 (a scale) or 0.05 px (a nudge), and
                        its clock starts at the first frame drawn after it is let go
            squash      C() and BB(): the pull of a drag, 400 px to the unit
            stretch     the domino: up 80 ms (ease out), held 15, back 210 (ease in)
            ripple      NB(): grows in 225 ms and travels to the middle in 425
                        on cubic-bezier(.4,0,.2,1) solved as ui.js solves it, is
                        in for 75 ms, held to 225 ms after the press and out in 150
            tween       a CSS transition: a cubic-bezier solved as Chrome
                        solves it, starting at the first frame after the change,
                        and turned back the way the CSS Transitions spec turns
                        a transition back (the reversing shortening factor)
            keyframes   a CSS animation, its timing function within each interval
            slide       the slider's click: 120 ms plus 180 for the whole track,
                        ease-out cubic, snapped to the step, then the landing
                        squish (60 ms up, 220 back)
*/
#define UI_LN2 744261118ll                 // ln 2
#define UI_INV_LN2 1549082005ll            // 1 / ln 2
#define UI_PI_2_Q60 1811004864519280711ll  // pi / 2, in Q60
#define UI_TWO_OVER_PI 683565276ll         // 2 / pi

// A whole number of logical pixels, or a count, as Q30.
static inline b64 ui_q(b64 whole)
{
        return whole * UI_ONE;
}

// A span of nanoseconds as milliseconds in Q30; past a minute it is a minute.
static inline b64 ui_ms(b64 ns)
{
        ns = ns < 0 ? 0 : ns > 60000 * UI_MS ? 60000 * UI_MS : ns;
        return (ns << 24) / 15625; // 2^30 / 10^6 is 2^24 / 15625
}

// e to the -x, x 0 or more: x taken down by ln 2 till under it, the rest's series, and the halvings.
static b64 ui_exp_neg(b64 x)
{
        b64 n, r, sum = UI_ONE;

        if (x <= 0)
                return UI_ONE;
        if (x >= 22 * UI_ONE)
                return 0; // under half the last place
        n = (b64)(((b128)x * UI_INV_LN2) >> (2 * UI_Q));
        r = x - n * UI_LN2;
        while (r >= UI_LN2)
                r -= UI_LN2, n++;
        while (r < 0)
                r += UI_LN2, n--;
        for (int k = 14; k >= 1; k--)
                sum = UI_ONE - ui_qmul(r, sum) / k;
        return n ? (sum + (1ll << (n - 1))) >> n : sum;
}

// The sine and cosine of x radians, x 0 or more: x less the nearest quarter turn, and the two series.
static void ui_sincos(b64 x, b64 address_to sine, b64 address_to cosine)
{
        b64 k = (b64)(((b128)x * UI_TWO_OVER_PI + ((b128)1 << (2 * UI_Q - 1))) >> (2 * UI_Q));
        b64 r = (b64)((((b128)x << UI_Q) - (b128)k * UI_PI_2_Q60 + ((b128)1 << (UI_Q - 1))) >> UI_Q);
        b64 r2 = ui_qmul(r, r), s = UI_ONE, c = UI_ONE;

        for (int n = 16; n >= 2; n -= 2)
        {
                s = UI_ONE - ui_qmul(r2, s) / ((n + 1) * n);
                c = UI_ONE - ui_qmul(r2, c) / (n * (n - 1));
        }
        s = ui_qmul(r, s);
        switch (k & 3)
        {
        case 0: *sine = s, *cosine = c; break;
        case 1: *sine = c, *cosine = -s; break;
        case 2: *sine = -s, *cosine = -c; break;
        default: *sine = -c, *cosine = s; break;
        }
}

// The whole square root of n.
static p64 ui_isqrt(p64 n)
{
        p64 root = 0, bit = 1ull << 62;

        while (bit > n)
                bit >>= 2;
        while (bit)
        {
                if (n >= root + bit)
                        n -= root + bit, root = (root >> 1) + bit;
                else
                        root >>= 1;
                bit >>= 2;
        }
        return root;
}

// The square root of a Q30 value 0 or more, Q30: of x shifted by as much of 30 as it holds, and the rest after.
static inline b64 ui_qsqrt(b64 x)
{
        int shift = UI_Q;

        if (x <= 0)
                return 0;
        while (shift > 0 && (p64)x >> (63 - shift))
                shift -= 2;
        return (b64)(ui_isqrt((p64)x << shift) << ((UI_Q - shift) / 2));
}

static const struct ui_bezier ui_ease_rise = UI_BEZIER(UI_Q2(40), UI_Q2(65), UI_Q2(30), UI_ONE); // --rb-ease-rise
static const struct ui_bezier ui_ease_out = UI_BEZIER(0, 0, UI_Q2(58), UI_ONE);              // CSS ease-out
static const struct ui_bezier ui_ease_ripple = UI_BEZIER(UI_Q2(40), 0, UI_Q2(20), UI_ONE);   // ui.js i()

// ui.js's i(): the ripple's cubic-bezier(.4,0,.2,1) by five of Newton's steps from t = x, no bracket.
static b64 ui_ripple_ease(b64 x)
{
        const struct ui_bezier address_to e = &ui_ease_ripple;
        b64 t = x;

        if (x <= 0)
                return 0;
        if (x >= UI_ONE)
                return UI_ONE;
        for (int step = 0; step < 5; step++)
        {
                b64 off = ui_bezier_x(e, t) - x, slope = ui_bezier_dx(e, t);

                if (slope > -11 && slope < 11) // 1e-8
                        break;
                t -= ui_qdiv(off, slope);
        }
        return ui_bezier_y(e, t);
}

// ui.js's C(): how far a pull of k units squashes, 0.064 of it and 0.064 more that comes on fast.
static b64 ui_squash_curve(b64 k)
{
        b64 z = k < 0 ? -k : k;
        b64 out = ui_qmul(68719477, z) + ui_qmul(68719477, UI_ONE - ui_exp_neg(ui_qmul(z, 8844645117ll)));

        return k < 0 ? -out : out;
}

// BB(): a drag of dx, dy logical pixels (Q30) as the scale it squashes by along each axis and the nudge with it.
struct ui_squash
{
        b64 sx, sy, tx, ty; // scales less one, and nudges in logical pixels, Q30
};

static struct ui_squash ui_squash_of(b64 dx, b64 dy)
{
        b64 z = ui_squash_curve((dx < 0 ? -dx : dx) / 400), w = ui_squash_curve((dy < 0 ? -dy : dy) / 400);
        struct ui_squash out;

        out.sx = z - ui_qmul(w, UI_Q2(35));
        out.sy = w - ui_qmul(z, UI_Q2(35));
        out.tx = dx > 0 ? out.sx * 8 : dx < 0 ? -out.sx * 8 : 0;
        out.ty = dy > 0 ? out.sy * 8 : dy < 0 ? -out.sy * 8 : 0;
        return out;
}

/*
        P(): channels let go at amplitudes, the first a scale (at rest under
        0.0001) and the second a nudge (at rest under 0.05 px). The clock starts
        at the first frame drawn after the release, as ui.js's rAF does; a
        channel at rest is 0, and the spring is live while either is not.
*/
#define UI_SPRING_DECAY 25945897435ll // --rb-spring-decay 24.164
#define UI_SPRING_FREQ 5266703647ll   // --rb-spring-freq 4.905
#define UI_SPRING_UB 5289683473ll     // decay / freq, ui.js's UB
#define UI_REST_SCALE 107374ll        // 0.0001
#define UI_REST_PX 53687091ll         // 0.05

struct ui_spring
{
        b64 amplitude[2];
        b64 value[2];
        b64 start; // the first frame's time, 0 before it
        b32 live;
};

static fn ui_spring_go(struct ui_spring address_to s, b64 scale, b64 nudge)
{
        s->amplitude[0] = s->value[0] = scale;
        s->amplitude[1] = s->value[1] = nudge;
        s->start = 0;
        s->live = 1; // the first frame says whether any of it is past rest
}

static fn ui_spring_stop(struct ui_spring address_to s)
{
        s->live = 0;
        s->value[0] = s->value[1] = 0;
}

static b32 ui_spring_at(struct ui_spring address_to s, b64 now)
{
        b64 h, j, sine, cosine;
        b32 live = 0;

        if (!s->live)
                return 0;
        if (!s->start)
                s->start = now;
        h = ui_ms(now - s->start) / 1000;
        j = ui_exp_neg(ui_qmul(UI_SPRING_DECAY, h));
        ui_sincos(ui_qmul(UI_SPRING_FREQ, h), &sine, &cosine);
        for (int k = 0; k < 2; k++)
        {
                b64 a = s->amplitude[k], f = ui_qmul(j, ui_qmul(a, cosine) + ui_qmul(ui_qmul(UI_SPRING_UB, a), sine));
                b64 rest = k ? UI_REST_PX : UI_REST_SCALE;

                s->value[k] = f < rest && f > -rest ? 0 : f;
                live |= s->value[k] != 0;
        }
        s->live = live;
        return live;
}

/*
        The domino stretch(): a scale along one axis to most and back, called at
        a time (a cascade calls it late, by its place along the travel) and drawn
        from the first frame at or after it. 1 before and after.
*/
struct ui_stretch
{
        b64 at;   // when it was called, 0 for none
        b64 most; // the peak scale, Q30
};

static fn ui_stretch_go(struct ui_stretch address_to s, b64 at, b64 most)
{
        s->at = at ? at : 1;
        s->most = most;
}

static b64 ui_stretch_at(struct ui_stretch address_to s, b64 now, b32 address_to live)
{
        b64 e, p, u;

        if (!s->at)
                return UI_ONE;
        if (now < s->at)
        {
                *live = 1;
                return UI_ONE;
        }
        e = ui_ms(now - s->at);
        if (e < ui_q(80))
        {
                u = UI_ONE - e / 80;
                p = UI_ONE - ui_qmul(u, u);
        }
        else if (e < ui_q(95))
                p = UI_ONE;
        else if (e < ui_q(305))
        {
                u = UI_ONE - (e - ui_q(95)) / 210;
                p = ui_qmul(u, u);
        }
        else
        {
                s->at = 0;
                return UI_ONE;
        }
        *live = 1;
        return UI_ONE + ui_qmul(s->most - UI_ONE, p);
}

/*
        A CSS transition of one value. A change is taken at the first frame
        after it (the style change event), from the value the transition has
        then; a change back to where a running one came from is shortened by
        the spec's reversing shortening factor. kind says how a value moves:
        a number, or a colour (four 16-bit channels of 8.8, premultiplied).
*/
#define UI_TWEEN_NUMBER 0
#define UI_TWEEN_COLOUR 1

struct ui_tween
{
        b64 from, to, adjusted;   // the start, the end, and the reversing-adjusted start
        b64 start, duration;      // ns; start 0 is done (or never begun)
        b64 factor;               // the reversing shortening factor, Q30
        b64 want;                 // a change not yet taken
        b64 want_duration;
        const struct ui_bezier address_to ease;
        b32 pending, kind;
        b32 placed; // it has a value: CSS does not transition an element's first style, so the first value is put, not set
};

// A colour 0xaarrggbb as a tween's value: alpha and each channel premultiplied by it, 8.8 each, a 16-bit lane apiece.
static b64 ui_tween_colour(unsigned int argb)
{
        p64 a = argb >> 24, out = a << 56;

        for (int shift = 0; shift < 24; shift += 8)
                out |= (((p64)(argb >> shift & 255) * a * 256 + 127) / 255) << (shift * 2);
        return (b64)out;
}

// And back: 0xaarrggbb, the alpha divided out again, rounded.
static unsigned int ui_tween_argb(b64 value)
{
        p64 v = (p64)value, a = v >> 48 & 0xffff, alpha = (a + 128) >> 8;
        unsigned int out;

        if (!a)
                return 0;
        out = (unsigned int)(alpha > 255 ? 255 : alpha) << 24;
        for (int shift = 0; shift < 24; shift += 8)
        {
                p64 c = ((v >> (shift * 2) & 0xffff) * 255 + a / 2) / a;

                out |= (unsigned int)(c > 255 ? 255 : c) << shift;
        }
        return out;
}

static b64 ui_tween_mix(b32 kind, b64 from, b64 to, b64 eased)
{
        p64 out = 0;

        if (kind == UI_TWEEN_NUMBER)
                return from + ui_qmul(to - from, eased);
        for (int shift = 0; shift < 64; shift += 16)
        {
                b64 a = (b64)((p64)from >> shift & 0xffff), b = (b64)((p64)to >> shift & 0xffff);

                out |= (p64)(a + ui_qmul(b - a, eased)) << shift;
        }
        return (b64)out;
}

// Start where it is: a value with no transition running.
static fn ui_tween_put(struct ui_tween address_to t, b64 value, b32 kind)
{
        memory_zero(t, sizeof(*t));
        t->from = t->to = t->adjusted = t->want = value;
        t->kind = kind;
        t->factor = UI_ONE;
        t->placed = 1;
}

// The value wanted from now on, over duration ns on ease (0: at once).
static fn ui_tween_set(struct ui_tween address_to t, b64 value, b64 duration, const struct ui_bezier address_to ease)
{
        if (value == (t->pending ? t->want : t->to))
                return;
        t->want = value;
        t->want_duration = duration;
        t->ease = ease ? ease : &ui_ease;
        t->pending = 1;
}

// The value at a frame, taking a change first; live is set while it moves.
static b64 ui_tween_at(struct ui_tween address_to t, b64 now, b32 address_to live)
{
        b64 eased = UI_ONE, current;

        if (t->start)
                eased = ui_bezier_at(t->ease, ui_progress(now - t->start, t->duration));
        current = t->start ? ui_tween_mix(t->kind, t->from, t->to, eased) : t->to;
        if (t->pending)
        {
                b64 want = t->want, duration = t->want_duration;

                t->pending = 0;
                if (current == want || duration <= 0)
                {
                        t->from = t->to = t->adjusted = want;
                        t->start = 0;
                        t->factor = UI_ONE;
                        return want;
                }
                if (t->start && t->adjusted == want)
                {
                        // Back the way it came: the new one runs for the share the old had covered.
                        b64 factor = ui_qmul(eased, t->factor) + UI_ONE - t->factor;

                        factor = factor < 0 ? -factor : factor;
                        t->factor = factor > UI_ONE ? UI_ONE : factor;
                        t->adjusted = t->to;
                        t->duration = ui_qmul(duration, t->factor);
                }
                else
                {
                        t->factor = UI_ONE;
                        t->adjusted = current;
                        t->duration = duration;
                }
                t->from = current;
                t->to = want;
                t->start = now;
                eased = 0;
                current = t->from;
                if (t->duration <= 0)
                {
                        t->start = 0;
                        return t->to;
                }
        }
        if (t->start && now - t->start >= t->duration)
        {
                t->start = 0;
                return t->to;
        }
        if (t->start)
                *live = 1;
        return current;
}

// A tween that is done or never ran: its end, at once.
static inline b32 ui_tween_still(const struct ui_tween address_to t)
{
        return !t->start && !t->pending;
}

/*
        A CSS animation over stops (offset and value, Q30), its timing function
        within each interval as CSS applies it, from start (the first frame) for
        duration. The value at the end is the last stop's.
*/
struct ui_stop
{
        b64 at, value;
};

static b64 ui_keyframes_at(const struct ui_stop address_to stops, int count, const struct ui_bezier address_to ease, b64 start,
                           b64 duration, b64 now, b32 address_to live)
{
        b64 p = ui_progress(now - start, duration);

        if (!start)
                return stops[count - 1].value;
        if (p < UI_ONE)
                *live = 1;
        for (int at = 1; at < count; at++)
                if (p <= stops[at].at)
                {
                        b64 within = ui_qdiv(p - stops[at - 1].at, stops[at].at - stops[at - 1].at);

                        return stops[at - 1].value + ui_qmul(stops[at].value - stops[at - 1].value,
                                                             ease ? ui_bezier_at(ease, within) : within);
                }
        return stops[count - 1].value;
}

/*
        NB()'s ripple, for a press at x, y (Q30 logical pixels) in a box of w by
        h whose clip reaches margin past it on every side (8 for a button). Its
        size reaches the clip's farthest corner from the box's middle. The clock
        starts at the first frame; exit is when the pointer went up or away.
*/
struct ui_ripple
{
        b64 x, y, w, h, margin;   // the press and the box, Q30 logical pixels
        b64 reach;                // the radius at full size
        b64 start, exit, hold;    // ns: the first frame, the exit (0 before it), what the exit waits
        b32 live;
        b32 rides;                // a tab's ripple from a press on the tab the indicator was on: it moves with the indicator
};

static fn ui_ripple_go(struct ui_ripple address_to r, b64 x, b64 y, b64 w, b64 h, b64 margin)
{
        b64 f = margin + w / 2, v = margin + h / 2, across = w + 2 * margin, down = h + 2 * margin;
        b64 rx = f > across - f ? f : across - f, ry = v > down - v ? v : down - v;

        r->x = x, r->y = y, r->w = w, r->h = h, r->margin = margin;
        r->reach = ui_qsqrt(ui_qmul(rx, rx) + ui_qmul(ry, ry));
        r->start = r->exit = r->hold = 0;
        r->live = 1;
        r->rides = 0;
}

static fn ui_ripple_exit(struct ui_ripple address_to r, b64 now)
{
        b64 shown;

        if (!r->live || r->exit)
                return;
        r->exit = now;
        shown = r->start ? now - r->start : 0;
        r->hold = shown > 0 && shown < 225 * UI_MS ? 225 * UI_MS - shown : 0;
}

// The ripple at a frame: its middle in the clip's own place (x, y from the clip's top left), its radius and its
// opacity (Q30, the 0.2 of the kit included). False when it is gone.
static b32 ui_ripple_at(struct ui_ripple address_to r, b64 now, b64 address_to cx, b64 address_to cy, b64 address_to radius,
                        b64 address_to opacity)
{
        b64 h, grow, travel, f, v, a;

        if (!r->live)
                return 0;
        if (!r->start)
                r->start = now;
        h = now - r->start;
        grow = ui_ripple_ease(ui_progress(h, 225 * UI_MS));
        travel = ui_ripple_ease(ui_progress(h, 425 * UI_MS));
        f = r->margin + r->w / 2;
        v = r->margin + r->h / 2;
        *cx = f + ui_qmul(r->x + r->margin - f, UI_ONE - travel);
        *cy = v + ui_qmul(r->y + r->margin - v, UI_ONE - travel);
        *radius = ui_qmul(r->reach, grow);
        if (!r->exit)
                a = ui_progress(h, 75 * UI_MS);
        else
        {
                b64 n = now - r->exit - r->hold;

                a = n < 0 ? UI_ONE : UI_ONE - ui_progress(n, 150 * UI_MS);
        }
        *opacity = ui_qmul(a, UI_Q2(20));
        if (r->exit && a <= 0)
        {
                r->live = 0;
                return 0;
        }
        return 1;
}

/*
        The slider's click (its pointerdown handler): the value slides from
        where it was to the value clicked over 120 ms and 180 more for the whole
        track, ease-out cubic, and on the release the bar stretches toward the
        throw by a peak of 0.015 and 0.04 of the share of the track (at most
        0.045): 60 ms up, ease out, then 220 back, cubic.
*/
struct ui_slide
{
        b64 from, to;      // values, Q30
        b64 at, duration;  // ns: the press, and how long
        b64 peak;          // the landing's peak, Q30
        b64 landed;        // ns: the release, 0 for none
};

static fn ui_slide_go(struct ui_slide address_to s, b64 from, b64 to, b64 span, b64 at)
{
        b64 jump = to > from ? to - from : from - to, ratio = ui_qdiv(jump, span ? span : UI_ONE);
        b64 peak = UI_Q2(1) + UI_Q2(1) / 2 + ui_qmul(ratio, UI_Q2(4));

        s->from = from, s->to = to, s->at = at ? at : 1, s->landed = 0;
        s->duration = 120 * UI_MS + ui_qmul(ratio, 180 * UI_MS);
        s->peak = peak > UI_Q2(4) + UI_Q2(1) / 2 ? UI_Q2(4) + UI_Q2(1) / 2 : peak;
}

// The value the slide has reached at a frame: ease-out cubic of the time.
static b64 ui_slide_value(const struct ui_slide address_to s, b64 now, b32 address_to live)
{
        b64 p = ui_progress(now - s->at, s->duration), u = UI_ONE - p;

        if (p < UI_ONE)
                *live = 1;
        return s->from + ui_qmul(s->to - s->from, UI_ONE - ui_qmul(ui_qmul(u, u), u));
}

// The landing squish at a frame, a scale (UI_ONE at rest).
static b64 ui_slide_land(struct ui_slide address_to s, b64 now, b32 address_to live)
{
        b64 e, u;

        if (!s->landed)
                return UI_ONE;
        e = ui_ms(now - s->landed);
        if (e < ui_q(60))
        {
                u = UI_ONE - e / 60;
                *live = 1;
                return UI_ONE + ui_qmul(s->peak, UI_ONE - ui_qmul(u, u));
        }
        if (e < ui_q(280))
        {
                u = UI_ONE - (e - ui_q(60)) / 220;
                *live = 1;
                return UI_ONE + ui_qmul(s->peak, ui_qmul(ui_qmul(u, u), u));
        }
        s->landed = 0;
        return UI_ONE;
}

/* ---- end of the kit's motion ---- */

/* ---- kit: theme ---- */

#include "theme.h"

/*
        The kit's threshold for text on a glass ground, in hundredths (ui_contrast, theme.h): WCAG's 4.5:1 and 0.2 more, kept
        for the compositor's grain, which moves a glass ground up to three levels a pixel (a 4.5:1 fill came out at 4.46:1).
*/
#define UI_READABLE 470
#define UI_GLASS_GROUND 0x101012u // a glass window's ground before the compositor has tinted one: a dark glass's

/*
        The quiet ink on a glass ground: the ground mixed toward the ink from 176 of 256, or further toward it, the least
        that stands out UI_READABLE from the ground. The ink is the most the kit's rule gives (window_contrast), so it
        always does; the quiet one does not on the glass's ends, a dark glass at its brightest or a light one at its dimmest.
*/
static unsigned int ui_glass_quiet(unsigned int ground, unsigned int ink)
{
        unsigned int low = 176, high = 256;

        while (low < high)
        {
                unsigned int mid = (low + high) / 2;

                if (ui_contrast(window_mix(ground, ink, mid), ground) >= UI_READABLE)
                        high = mid;
                else
                        low = mid + 1;
        }
        return window_mix(ground, ink, low);
}

// The inks for a ground: what the compositor says the glass came to.
static fn ui_inks(struct ui address_to ui, unsigned int ground)
{
        ui->ground = ground & 0xffffffu;
        ui->ink = window_contrast(ui->ground);
        ui->quiet = ui_glass_quiet(ui->ground, ui->ink);
        ui->lit = ui_tone(ui->ink, 36);
        ui->ring = ui_tone(ui->ink, 190);
}

/*
        What one process draws with: the theme as the table last said it and
        the colours shown this frame, and the frame's clock. A frame sets
        busy when anything in it is still moving, and the loop asks for the
        next frame a display period on; nothing moving, it sleeps.
*/
static struct ui_kit
{
        struct ui_theme shown, from, to;
        struct ui_tween turn;              // 0 to UI_ONE while the theme turns
        unsigned int mode, accent, motion; // UI_THEME_LIGHT or _DARK, 0xrrggbb, 0 full or 1 reduced
        unsigned int themed;               // the table's theme count as last taken
        b64 now;                           // the frame's time, ns
        b64 drawn;                         // the last frame's time: the next is a display period after it
        b32 busy;                          // something moved: another frame is due
        b32 made;
        int damage_top, damage_bottom;     // the rows (device pixels) the moving controls drew this frame
        b64 epoch;                         // counted up when every control is to be placed again (ui_kit_settle)
        // The control being drawn said which of its rows move (logical pixels, none when bottom <= top), and they
        // are all that is marked of it: tabs on end, whose box is one item of a column.
        b32 reached;
        int reach_top, reach_bottom;
} ui_kit;

// Rows top to bottom (logical pixels) of the control being drawn move this frame.
static fn ui_kit_reach(int top, int bottom)
{
        ui_kit.reached = 1;
        if (ui_kit.reach_bottom <= ui_kit.reach_top)
                ui_kit.reach_top = top, ui_kit.reach_bottom = bottom;
        else
        {
                ui_kit.reach_top = top < ui_kit.reach_top ? top : ui_kit.reach_top;
                ui_kit.reach_bottom = bottom > ui_kit.reach_bottom ? bottom : ui_kit.reach_bottom;
        }
}

/*
        Everything is placed again, not moved: the window was resized, the
        theme or the scale changed. A control drawn after this is drawn at its
        state, its transitions and springs put at their ends (ui_control_place).
*/
static fn ui_kit_settle(void)
{
        ui_kit.epoch++;
}

// The colour of a role this frame.
static inline unsigned int ui_role(int role)
{
        return ui_kit.shown.role[role];
}

/*
        A glass window (ui->glass, set from WINDOW_GLASS) is what is under it, so its pieces are drawn in its ground's inks
        (ui_inks), not in the surface's roles: its text is the ink that stands out from the tint, and a well, a selection
        or a row's highlight is that ink laid over the glass at a low alpha. Every other window keeps its roles, and every
        helper gives the role it always gave there.
*/
// A text colour for a role, straight with alpha 255: the ink for on-surface text, the quiet ink for its variant.
static unsigned int ui_text_colour(const struct ui address_to ui, int role)
{
        if (!ui->glass)
                return ui_role(role);
        return 0xff000000u | (role == UI_ON_SURFACE_VARIANT ? ui->quiet : ui->ink) & 0xffffffu;
}

/*
        A glass well's percent: the most of the ink that can be laid over the ground and still have the ink stand out
        UI_READABLE from what the well comes to. The ink is what the well's text is drawn in (ui_text_colour), so a well
        is never darker than that allows; a dark glass at its brightest (92 a channel) takes a 6% well, a light one at its
        dimmest (180) 29%, and the
        glass at 0 is the ink's own contrast, which always passes. Monotone in percent, so a search finds the most.
*/
static unsigned int ui_glass_well(const struct ui address_to ui, unsigned int percent)
{
        unsigned int ink = ui->ink & 0xffffffu, low = 0, high = percent;

        while (low < high)
        {
                unsigned int mid = (low + high + 1) / 2, alpha = ui_mix(0xff000000u | ink, mid, 0) >> 24;

                // The blend ui_tone makes of the well's alpha, the same figures the well is drawn with.
                if (ui_contrast(ink, window_mix(ui->ground, ink, alpha + (alpha >> 7))) >= UI_READABLE)
                        low = mid;
                else
                        high = mid - 1;
        }
        return low;
}

// A surface piece's colour: its role in an opaque window; in a glass one, the ink at percent (straight, as ui_kit_fill takes it),
// at most the percent ui_glass_well allows.
static unsigned int ui_well(const struct ui address_to ui, int role, unsigned int percent)
{
        if (!ui->glass)
                return ui_role(role);
        return ui_mix(0xff000000u | (ui->ink & 0xffffffu), ui_glass_well(ui, percent), 0);
}

// The window's surface, opaque: what a kit window is cleared to.
static unsigned int ui_role_surface(void)
{
        return ui_kit.shown.role[UI_SURFACE] | 0xff000000u;
}

// A kit window's inks: those that stand out from its surface (the rows' text, a header, the focus).
static fn ui_kit_inks(struct ui address_to ui)
{
        if (ui->opaque)
                ui_inks(ui, ui_kit.to.role[UI_SURFACE] & 0xffffffu);
}

// The theme to turn to: at once the first time, and over .2s --ease after (at once with reduced motion).
static fn ui_kit_theme(unsigned int mode, unsigned int accent, unsigned int motion)
{
        ui_kit.motion = motion;
        if (ui_kit.made && mode == ui_kit.mode && (accent & 0xffffffu) == ui_kit.accent)
                return;
        ui_kit.mode = mode;
        ui_kit.accent = accent & 0xffffffu;
        ui_kit.epoch += 1 + !ui_kit.epoch; // the controls are placed in the new colours; only the theme's own turn moves
        ui_kit.from = ui_kit.shown;
        ui_theme_make(&ui_kit.to, mode == UI_THEME_DARK, ui_kit.accent);
        if (!ui_kit.made || motion)
        {
                ui_kit.shown = ui_kit.from = ui_kit.to;
                ui_tween_put(&ui_kit.turn, UI_ONE, UI_TWEEN_NUMBER);
                ui_kit.made = 1;
                return;
        }
        ui_tween_put(&ui_kit.turn, 0, UI_TWEEN_NUMBER);
        ui_tween_set(&ui_kit.turn, UI_ONE, 200 * UI_MS, &ui_ease);
}

// The theme's colours at this frame: a turn under way mixes every role from the old to the new.
static fn ui_kit_turn(void)
{
        b32 live = 0;
        b64 p;

        if (ui_tween_still(&ui_kit.turn))
                return;
        p = ui_tween_at(&ui_kit.turn, ui_kit.now, &live);
        for (int role = 0; role < UI_ROLES; role++)
                ui_kit.shown.role[role] = ui_tween_argb(ui_tween_mix(UI_TWEEN_COLOUR, ui_tween_colour(ui_kit.from.role[role]),
                                                                     ui_tween_colour(ui_kit.to.role[role]), p));
        if (live)
                ui_kit.busy = 1;
        else
                ui_kit.shown = ui_kit.to;
}

/* ---- end of the kit's theme ---- */

/* ---- kit: shapes ---- */

/*
        The kit's shapes, drawn with their edges where they fall: a rounded
        rectangle whose corners are ellipses (a squashed button's are), a
        circle, a pill, a ring as one less another. Places are 256ths of a
        device pixel and are never rounded to a pixel while a shape moves.
        Across a row the cover is exact; down it a row is sixteen lines of
        it, so an edge that is near flat is in sixteen steps. Text is not
        drawn here: it is ui_run's, at whole pixels, and a stretched control's
        label is moved with it (rounded) and never resampled.
*/
#define UI_SUB 16                 // lines a row is taken in
#define UI_FULL (256 * UI_SUB)    // the cover of a pixel wholly inside
#define UI_KIT_LINE 8192          // the widest a shape is drawn, in device pixels

struct ui_box
{
        b32 x0, y0, x1, y1; // the edges, 256ths of a device pixel
        b32 rx, ry;         // each corner's radii
};

static unsigned short ui_kit_line[UI_KIT_LINE], ui_kit_clip[UI_KIT_LINE];

// A box from a logical rectangle (Q30 logical pixels) at the window's scale, with a corner radius (Q30, logical).
static struct ui_box ui_box_of(const struct ui address_to ui, b64 x, b64 y, b64 w, b64 h, b64 radius)
{
        struct ui_box box;
        b64 s = ui->scale;

        box.x0 = (b32)((x * s) >> 22);
        box.y0 = (b32)((y * s) >> 22);
        box.x1 = (b32)(((x + w) * s) >> 22);
        box.y1 = (b32)(((y + h) * s) >> 22);
        box.rx = box.ry = (b32)((radius * s) >> 22);
        return box;
}

// A box scaled by sx, sy (Q30) about the point ox, oy (256ths), then moved by tx, ty (256ths): a CSS transform
// translate() scaleX() scaleY() with its origin. The corners scale with it, into ellipses.
static struct ui_box ui_box_transform(struct ui_box box, b64 sx, b64 sy, b32 ox, b32 oy, b32 tx, b32 ty)
{
        struct ui_box out;

        out.x0 = ox + tx + (b32)ui_qmul(box.x0 - ox, sx);
        out.x1 = ox + tx + (b32)ui_qmul(box.x1 - ox, sx);
        out.y0 = oy + ty + (b32)ui_qmul(box.y0 - oy, sy);
        out.y1 = oy + ty + (b32)ui_qmul(box.y1 - oy, sy);
        out.rx = (b32)ui_qmul(box.rx, sx);
        out.ry = (b32)ui_qmul(box.ry, sy);
        return out;
}

// A box's radii as CSS draws them: where two corners' radii come to more than the side, every radius is scaled by
// the one factor that fits them all, both of each corner's.
static fn ui_box_radii(const struct ui_box address_to box, b32 address_to rx, b32 address_to ry)
{
        p64 w = (p64)(box->x1 - box->x0), h = (p64)(box->y1 - box->y0), f = 65536;

        if (*rx > 0 && 2 * (p64)*rx > w)
                f = (w << 16) / (2 * (p64)*rx);
        if (*ry > 0 && 2 * (p64)*ry > h && (h << 16) / (2 * (p64)*ry) < f)
                f = (h << 16) / (2 * (p64)*ry);
        if (f < 65536)
        {
                *rx = (b32)(((p64)*rx * f) >> 16);
                *ry = (b32)(((p64)*ry * f) >> 16);
        }
}

/*
        A corner's half-width at a line, as the unit circle's: the root of 2^32 less u squared (u below 65536, the
        line's height in the corner over its radius, 16.16). The hardware takes it every time: a table of roots by u
        missed half the settings frame's lookups (1904 distinct places a frame, and no 2-way or doubled table held them
        all), and a miss is a root anyway, so the table cost more than it saved.
*/
static p64 ui_arc(p64 u)
{
        // 2^32 less u squared is at most 2^32, well inside a double's 53 bits, and a root just short of a whole number k
        // is short of it by 1/2k at least (k at most 65536), far past a double's last place, so the root truncated is
        // ui_isqrt's for every u -- one instruction for its bit loop's 33 turns.
        return (unsigned int)__builtin_sqrt((f64)((1ull << 32) - u * u));
}

// One line's span of a box, at y (256ths): false when the line misses it.
static b32 ui_box_span(const struct ui_box address_to box, b32 y, b32 address_to left, b32 address_to right)
{
        b32 dy = 0, rx = box->rx, ry = box->ry;

        if (y < box->y0 || y >= box->y1 || box->x1 <= box->x0)
                return 0;
        ui_box_radii(box, &rx, &ry);
        if (y < box->y0 + ry)
                dy = box->y0 + ry - y;
        else if (y >= box->y1 - ry)
                dy = y - (box->y1 - ry) + 1;
        if (dy > 0 && ry > 0 && rx > 0)
        {
                p64 u = ((p64)dy << 16) / (p64)ry;
                b32 dx = u >= 65536 ? 0 : (b32)(((p64)rx * ui_arc(u)) >> 16);

                *left = box->x0 + rx - dx;
                *right = box->x1 - rx + dx;
                return *right > *left;
        }
        *left = box->x0;
        *right = box->x1;
        return 1;
}

/*
        A span's cover added into a line, from pixel first (the line's 0), weight lines of it. The line holds first
        differences, not covers: a pixel's cover is the running sum of the entries up to it (the callers take that sum
        as they read the row), so a span is at most four entries however wide. The running sums are the covers the
        per-pixel adds made, modulo 2^16, for every span, since each cover is a difference of covers and the wrap is the
        same. An entry at width (past the row) is never read, so it is not written.
*/
static fn ui_line_add(unsigned short address_to line, b32 first, b32 width, b32 left, b32 right, b32 weight)
{
        b32 p0, p1, a, b;

        left -= first * 256;
        right -= first * 256;
        left = left < 0 ? 0 : left;
        right = right > width * 256 ? width * 256 : right;
        if (right <= left)
                return;
        p0 = left >> 8;
        p1 = (right - 1) >> 8;
        if (p0 == p1)
        {
                b = (right - left) * weight;
                line[p0] += (unsigned short)b;
                if (p0 + 1 < width)
                        line[p0 + 1] += (unsigned short)-b;
                return;
        }
        // Covers: a at p0, 256 * weight through the middle, b at p1. Steps: a at p0, then to the middle (or to b), then
        // from the middle to b, and back to nothing past p1.
        a = (256 - (left & 255)) * weight;
        b = (right - p1 * 256) * weight;
        line[p0] += (unsigned short)a;
        line[p0 + 1] += (unsigned short)((p1 == p0 + 1 ? b : 256 * weight) - a);
        if (p1 >= p0 + 2)
                line[p1] += (unsigned short)(b - 256 * weight);
        if (p1 + 1 < width)
                line[p1 + 1] += (unsigned short)-b;
}

// The cover of a box over the row y of pixels, into line from pixel first, width wide: false when none.
static b32 ui_box_row(const struct ui_box address_to box, b32 y, unsigned short address_to line, b32 first, b32 width)
{
        b32 top = y * 256, bottom = top + 256, ry = box->ry, any = 0;

        if (bottom <= box->y0 || top >= box->y1)
                return 0;
        {
                b32 rx = box->rx;

                ui_box_radii(box, &rx, &ry);
        }
        // A row in the straight middle, all of it inside: one span, every line.
        if (top >= box->y0 + ry && bottom <= box->y1 - ry && top >= box->y0 && bottom <= box->y1)
        {
                ui_line_add(line, first, width, box->x0, box->x1, UI_SUB);
                return 1;
        }
        for (b32 sub = 0; sub < UI_SUB; sub++)
        {
                b32 left, right;

                if (ui_box_span(box, top + sub * (256 / UI_SUB) + 256 / UI_SUB / 2, &left, &right))
                {
                        ui_line_add(line, first, width, left, right, 1);
                        any = 1;
                }
        }
        return any;
}

#if defined(__SSE2__) || defined(__aarch64__)
// Four lines at a time, in GCC and clang's vector extensions (SSE2 on x86-64, NEON on arm64); elsewhere one line at a time.
typedef int ui_lanes4 __attribute__((vector_size(16), may_alias));
#define UI_SPANS_LANES 1
#endif

/*
        A box over one row of pixels as its lines' spans, the cover a line would make worked out pixel by pixel only
        at the shape's edges. Each of the UI_SUB lines is a span (256ths, from pixel first, cut to width pixels);
        where every line there is covers a pixel whole, the pixel's cover is the same, inside (the lines there times
        256): pixels a to b. Edge pixels, lo to a and b to hi, are the sums of what each line covers of them -- the
        cover ui_box_row's line holds, without a pass over every pixel for every line.
*/
struct ui_spans
{
        b32 lo, hi, a, b;     // the pixels touched, and those of them every line covers whole
        unsigned int inside;  // the cover of a pixel from a to b
        int count;            // the lines that cover anything
        b32 left[UI_SUB] __attribute__((aligned(16))), right[UI_SUB] __attribute__((aligned(16)));
};

/*
        The insets of a corner's lines: for each line's dy (0 to ry, its place over the radius), the box's inset from its side,
        rx times the corner's half-width there, (rx * floor(sqrt(2^32 - u^2))) >> 16 with u = (dy << 16) / ry. dy is 0 for a
        line in the straight middle, whose inset is rx, and dy = ry gives u = 65536 and an inset of 0, so every line is this
        one formula. On x86-64 two lines go through SSE2 at a time: every step is exact in doubles (dy << 16 and rx are below
        2^37, so the quotient's floor and the root's are the convert instructions' truncations). Elsewhere a line is ui_arc.
*/
#if defined(__x86_64__) && defined(__SSE2__)
#define UI_INSETS_X64 1
static void ui_insets_x64(const int address_to dy, int rx, int ry, int address_to inset)
{
        double rxd = (double)rx, ryd = (double)ry, scale = 65536.0, full = 4294967296.0, back = 1.0 / 65536.0;
        long i = 0;

        __asm__ volatile(
                "movsd %[rxd], %%xmm14\n\tunpcklpd %%xmm14, %%xmm14\n\t"
                "movsd %[ryd], %%xmm15\n\tunpcklpd %%xmm15, %%xmm15\n\t"
                "movsd %[scale], %%xmm13\n\tunpcklpd %%xmm13, %%xmm13\n\t"
                "movsd %[full], %%xmm12\n\tunpcklpd %%xmm12, %%xmm12\n\t"
                "movsd %[back], %%xmm11\n\tunpcklpd %%xmm11, %%xmm11\n\t"
                "1:\n\t"
                "movq (%[dy],%[i],4), %%xmm0\n\t"
                "cvtdq2pd %%xmm0, %%xmm0\n\t"
                "mulpd %%xmm13, %%xmm0\n\t"
                "divpd %%xmm15, %%xmm0\n\t"
                "cvttpd2dq %%xmm0, %%xmm1\n\t"
                "cvtdq2pd %%xmm1, %%xmm1\n\t"
                "mulpd %%xmm1, %%xmm1\n\t"
                "movapd %%xmm12, %%xmm2\n\t"
                "subpd %%xmm1, %%xmm2\n\t"
                "sqrtpd %%xmm2, %%xmm2\n\t"
                "cvttpd2dq %%xmm2, %%xmm2\n\t"
                "cvtdq2pd %%xmm2, %%xmm2\n\t"
                "mulpd %%xmm14, %%xmm2\n\t"
                "mulpd %%xmm11, %%xmm2\n\t"
                "cvttpd2dq %%xmm2, %%xmm2\n\t"
                "movq %%xmm2, (%[inset],%[i],4)\n\t"
                "add $2, %[i]\n\t"
                "cmp $16, %[i]\n\t"
                "jb 1b\n\t"
                : [i] "+r"(i)
                : [dy] "r"(dy), [inset] "r"(inset), [rxd] "m"(rxd), [ryd] "m"(ryd), [scale] "m"(scale), [full] "m"(full), [back] "m"(back)
                : "xmm0", "xmm1", "xmm2", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "memory", "cc");
}
#endif

static void ui_insets(const int address_to dy, int rx, int ry, int address_to inset)
{
#ifdef UI_INSETS_X64
        ui_insets_x64(dy, rx, ry, inset);
#else
        for (b32 sub = 0; sub < UI_SUB; sub++)
        {
                p64 u = ((p64)dy[sub] << 16) / (p64)ry;

                inset[sub] = u >= 65536 ? 0 : (b32)(((p64)rx * ui_arc(u)) >> 16);
        }
#endif
}

static b32 ui_box_spans_radii(const struct ui_box address_to box, b32 rx, b32 ry, b32 y, b32 first, b32 width, struct ui_spans address_to s)
{
        b32 top = y * 256, bottom = top + 256, low = 0, high = 0, inner_low = 0, inner_high = 0, base = first * 256, reach = width * 256;
        b32 straight, corner, outside = 0;
        int dys[UI_SUB] __attribute__((aligned(16))), inset[UI_SUB] __attribute__((aligned(16)));

        s->count = 0;
        if (bottom <= box->y0 || top >= box->y1 || box->x1 <= box->x0)
                return 0;
        // A row in the straight middle, all of it inside, is every line the box's own span.
        straight = top >= box->y0 + ry && bottom <= box->y1 - ry && top >= box->y0 && bottom <= box->y1;
        corner = !straight && ry > 0 && rx > 0;
        // Each line's dy over the radius (ui_box_span's), and the lines outside the box, which take no part.
        for (b32 sub = 0; sub < UI_SUB; sub++)
        {
                b32 at = top + sub * (256 / UI_SUB) + 256 / UI_SUB / 2, dy = 0;

                if (!straight)
                {
                        if (at < box->y0 || at >= box->y1)
                                outside |= 1u << sub;
                        else if (at < box->y0 + ry)
                                dy = box->y0 + ry - at;
                        else if (at >= box->y1 - ry)
                                dy = at - (box->y1 - ry) + 1;
                }
                dys[sub] = dy;
        }
        if (corner)
                ui_insets(dys, rx, ry, inset);
        else
                for (b32 sub = 0; sub < UI_SUB; sub++)
                        inset[sub] = rx;
        for (b32 sub = 0; sub < UI_SUB; sub++)
        {
                b32 left, right;

                if (outside >> sub & 1)
                        continue;
                left = box->x0 + rx - inset[sub], right = box->x1 - rx + inset[sub];
                if (right <= left)
                        continue;
                left -= base, right -= base;
                left = left < 0 ? 0 : left;
                right = right > reach ? reach : right;
                if (right <= left)
                        continue;
                if (!s->count)
                        low = inner_low = left, high = inner_high = right;
                low = left < low ? left : low, high = right > high ? right : high;
                inner_low = left > inner_low ? left : inner_low, inner_high = right < inner_high ? right : inner_high;
                s->left[s->count] = left, s->right[s->count] = right;
                s->count++;
        }
        // The lines past count are nothing (0 to 0), which the cover sum takes as none.
        for (b32 k = s->count; k < UI_SUB; k++)
                s->left[k] = s->right[k] = 0;
        if (!s->count)
                return 0;
        s->lo = low >> 8, s->hi = (high + 255) >> 8;
        s->a = (inner_low + 255) >> 8, s->b = inner_high >> 8;
        if (s->b <= s->a)
                s->a = s->b = s->hi;
        s->inside = (unsigned int)s->count * 256;
        return 1;
}

// ui_box_spans_radii with the box's radii as drawn (ui_box_radii), for a caller that has not resolved them.
static b32 ui_box_spans(const struct ui_box address_to box, b32 y, b32 first, b32 width, struct ui_spans address_to s)
{
        b32 rx = box->rx, ry = box->ry;

        ui_box_radii(box, &rx, &ry);
        return ui_box_spans_radii(box, rx, ry, y, first, width, s);
}

// The cover of pixel x of a row's spans: what each line covers of it, summed.
static unsigned int ui_spans_cover(const struct ui_spans address_to s, b32 x)
{
        b32 p0 = x * 256, p1 = p0 + 256;
        unsigned int cover = 0;

        if (x >= s->a && x < s->b)
                return s->inside;
#ifdef UI_SPANS_LANES
        {
                ui_lanes4 low = {p0, p0, p0, p0}, high = {p1, p1, p1, p1}, sum = {0, 0, 0, 0};

                for (int k = 0; k < UI_SUB; k += 4)
                {
                        ui_lanes4 l = *(const ui_lanes4 address_to)&s->left[k], r = *(const ui_lanes4 address_to)&s->right[k], m, from, to, d;

                        m = l > low;
                        from = (l & m) | (low & ~m);
                        m = r < high;
                        to = (r & m) | (high & ~m);
                        d = to - from;
                        sum += d & (d > 0);
                }
                cover = (unsigned int)(sum[0] + sum[1] + sum[2] + sum[3]);
        }
#else
        for (int k = 0; k < s->count; k++)
        {
                b32 from = s->left[k] > p0 ? s->left[k] : p0, to = s->right[k] < p1 ? s->right[k] : p1;

                cover += to > from ? (unsigned int)(to - from) : 0;
        }
#endif
        return cover;
}

// A straight colour (0xaarrggbb) at a cover of UI_FULL, as the premultiplied pixel ui_over takes.
static inline unsigned int ui_premultiplied(unsigned int argb, unsigned int cover)
{
        unsigned int a = (argb >> 24) * cover / UI_FULL;

        return a << 24 | window_mix(0, argb & 0xffffffu, a + (a >> 7));
}

// Whether the row y is in the straight middle of a box (its radii as drawn, ry): every line of it is the one span.
static inline b32 ui_box_straight(const struct ui_box address_to box, b32 ry, b32 y)
{
        b32 top = y * 256, bottom = top + 256;

        return top >= box->y0 + ry && bottom <= box->y1 - ry && top >= box->y0 && bottom <= box->y1;
}

static inline b32 ui_box_ry(const struct ui_box address_to box)
{
        b32 rx = box->rx, ry = box->ry;

        ui_box_radii(box, &rx, &ry);
        return ry;
}

/*
        A span of a straight row, left to right (256ths, from pixel first), clamped to width pixels, as the line its
        cover would make: the pixels it starts and ends in by their share, those between it wholly. Answers its first and
        last pixel and their covers (UI_FULL a pixel wholly in it), or false when it covers none.
*/
static inline b32 ui_span_ends(b32 first, b32 width, b32 left, b32 right, b32 address_to p0, b32 address_to p1, unsigned int address_to c0,
                               unsigned int address_to c1)
{
        left -= first * 256, right -= first * 256;
        left = left < 0 ? 0 : left;
        right = right > width * 256 ? width * 256 : right;
        if (right <= left)
                return false;
        *p0 = left >> 8, *p1 = (right - 1) >> 8;
        if (*p0 == *p1)
                *c0 = *c1 = (unsigned int)(right - left) * UI_SUB;
        else
                *c0 = (unsigned int)(256 - (left & 255)) * UI_SUB, *c1 = (unsigned int)(right - *p1 * 256) * UI_SUB;
        return true;
}

/*
        A pixel of a row at a cover of a colour (0xaarrggbb), over what is there: the premultiplied colour at the cover, laid
        over the pixel. On x86-64 and arm64 that pair is one body, ui_kit_put_body, which premultiplies in the lanes and lays
        the tone over the pixel in the same registers. The C below it is riscv64's (its fused body counted more instructions
        than this C, see ui_over) and the reference the bodies are checked against.
*/
#ifdef UI_KIT_PUT_ASM
extern void ui_kit_put_body(unsigned int address_to at, unsigned int cover, unsigned int argb) __asm__("ui_kit_put_body")
        __attribute__((visibility("hidden")));

static inline fn ui_kit_put(unsigned int address_to row, b32 x, unsigned int cover, unsigned int argb)
{
        ui_kit_put_body(row + x, cover, argb);
}
#else
static inline fn ui_kit_put(unsigned int address_to row, b32 x, unsigned int cover, unsigned int argb)
{
        if (!cover)
                return;
        if (cover >= UI_FULL && argb >> 24 == 255)
                row[x] = argb | 0xff000000u;
        else
                row[x] = ui_over(ui_premultiplied(argb, cover > UI_FULL ? UI_FULL : cover), row[x]);
}
#endif

/*
        A run of pixels laid over with one premultiplied tone, which is what ui_over gives each of them: an opaque tone
        is the library's fill, and any other the library's run over (memory_over_tone_u32, the ui_over of one tone at
        every pixel of the run).
*/
static fn ui_kit_run(unsigned int address_to row, b32 count, unsigned int tone)
{
        if (count <= 0 || !(tone >> 24))
                return;
        if (tone >> 24 == 255)
        {
                memory_fill_u32(row, (positive)count, tone);
                return;
        }
        memory_over_tone_u32(row, (positive)count, tone);
}

// The tone of a colour at a cover, as ui_kit_put lays it: a colour wholly over is itself, opaque or premultiplied.
static inline unsigned int ui_kit_tone(unsigned int argb, unsigned int cover)
{
        if (cover >= UI_FULL && argb >> 24 == 255)
                return argb | 0xff000000u;
        return ui_premultiplied(argb, cover > UI_FULL ? UI_FULL : cover);
}

/*
        A box filled with a colour, cut to clip when there is one (a ripple to its
        button): over the picture, in the rows the ui may write. A row in the box's
        straight middle is its one span, laid on without the cover line: the same
        pixels, without a pass over them for each. Any other row is its lines' spans
        (ui_box_spans): the pixels at its edges each their sum, and those every line
        covers whole one run of one tone.
*/
static fn ui_kit_fill(struct ui address_to ui, const struct ui_box address_to box, unsigned int argb, const struct ui_box address_to clip)
{
        b32 first = box->x0 >> 8, last = (box->x1 + 255) >> 8, top = box->y0 >> 8, bottom = (box->y1 + 255) >> 8, rx = box->rx, ry = box->ry;
        b32 cut_rx = 0, cut_ry = 0;
        unsigned int whole = argb >> 24 == 255 ? argb | 0xff000000u : ui_premultiplied(argb, UI_FULL);

        if (!(argb >> 24))
                return;
        ui_box_radii(box, &rx, &ry);
        if (clip)
                cut_rx = clip->rx, cut_ry = clip->ry, ui_box_radii(clip, &cut_rx, &cut_ry);
        first = first < 0 ? 0 : first;
        last = last > ui->width ? ui->width : last;
        if (clip)
        {
                first = first < clip->x0 >> 8 ? clip->x0 >> 8 : first;
                last = last > (clip->x1 + 255) >> 8 ? (clip->x1 + 255) >> 8 : last;
        }
        if (last - first > UI_KIT_LINE)
                last = first + UI_KIT_LINE;
        if (last <= first)
                return;
        for (b32 y = top; y < bottom; y++)
        {
                unsigned int address_to row;
                b32 width = last - first;

                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                if (!clip && ui_box_straight(box, ry, y))
                {
                        b32 p0, p1;
                        unsigned int c0, c1;

                        if (!ui_span_ends(first, width, box->x0, box->x1, &p0, &p1, &c0, &c1))
                                continue;
                        row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                        ui_kit_put(row, p0, c0, argb);
                        if (p1 > p0)
                        {
                                ui_kit_run(row + p0 + 1, p1 - p0 - 1, whole);
                                ui_kit_put(row, p1, c1, argb);
                        }
                        continue;
                }
                {
                        struct ui_spans spans, cut;
                        b32 lo, hi, a, b;
                        unsigned int inside;

                        if (!ui_box_spans_radii(box, rx, ry, y, first, width, &spans) || (clip && !ui_box_spans_radii(clip, cut_rx, cut_ry, y, first, width, &cut)))
                                continue;
                        row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                        lo = spans.lo, hi = spans.hi, a = spans.a, b = spans.b, inside = spans.inside;
                        // Cut to clip: the pixels both touch, whole where both are whole, each pixel the product of the two.
                        if (clip)
                        {
                                lo = cut.lo > lo ? cut.lo : lo, hi = cut.hi < hi ? cut.hi : hi;
                                a = cut.a > a ? cut.a : a, b = cut.b < b ? cut.b : b;
                                inside = inside * cut.inside / UI_FULL;
                        }
                        a = a < lo ? lo : a, b = b > hi ? hi : b;
                        if (b <= a)
                                a = b = hi;
                        for (b32 x = lo; x < a; x++)
                                ui_kit_put(row, x, clip ? ui_spans_cover(&spans, x) * ui_spans_cover(&cut, x) / UI_FULL : ui_spans_cover(&spans, x), argb);
                        if (b > a && inside)
                                ui_kit_run(row + a, b - a, ui_kit_tone(argb, inside));
                        for (b32 x = b > a ? b : hi; x < hi; x++)
                                ui_kit_put(row, x, clip ? ui_spans_cover(&spans, x) * ui_spans_cover(&cut, x) / UI_FULL : ui_spans_cover(&spans, x), argb);
                }
        }
}

// A ring: the box less the box inset by thickness (256ths), in one colour (an outline, a hairline).
static fn ui_kit_ring(struct ui address_to ui, const struct ui_box address_to box, b32 thickness, unsigned int argb)
{
        struct ui_box inner = *box;
        b32 first = box->x0 >> 8, last = (box->x1 + 255) >> 8, top = box->y0 >> 8, bottom = (box->y1 + 255) >> 8;

        b32 ry, inner_ry;

        inner.x0 += thickness, inner.y0 += thickness, inner.x1 -= thickness, inner.y1 -= thickness;
        inner.rx = inner.rx > thickness ? inner.rx - thickness : 0;
        inner.ry = inner.ry > thickness ? inner.ry - thickness : 0;
        ry = ui_box_ry(box), inner_ry = ui_box_ry(&inner);
        first = first < 0 ? 0 : first;
        last = last > ui->width ? ui->width : last;
        if (last - first > UI_KIT_LINE)
                last = first + UI_KIT_LINE;
        for (b32 y = top; y < bottom && last > first; y++)
        {
                unsigned int address_to row;
                b32 width = last - first;

                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                // A row straight in the box, past the inner box or straight in it too, its two bands apart: their spans alone.
                if (ui_box_straight(box, ry, y))
                {
                        b32 above = (y + 1) * 256 <= inner.y0 || y * 256 >= inner.y1 || inner.x1 <= inner.x0;
                        b32 spans[2][2] = {{box->x0, inner.x0}, {inner.x1, box->x1}};
                        int count = above ? 1 : 2;

                        if (above)
                                spans[0][1] = box->x1;
                        if (above || (ui_box_straight(&inner, inner_ry, y) && (inner.x0 - 1) >> 8 < inner.x1 >> 8))
                        {
                                row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                                for (int k = 0; k < count; k++)
                                {
                                        b32 p0, p1;
                                        unsigned int c0, c1;

                                        if (!ui_span_ends(first, width, spans[k][0], spans[k][1], &p0, &p1, &c0, &c1))
                                                continue;
                                        row[p0] = ui_over(ui_premultiplied(argb, c0), row[p0]);
                                        if (p1 > p0)
                                        {
                                                ui_kit_run(row + p0 + 1, p1 - p0 - 1, ui_premultiplied(argb, UI_FULL));
                                                row[p1] = ui_over(ui_premultiplied(argb, c1), row[p1]);
                                        }
                                }
                                continue;
                        }
                }
                // Any other row: the box's spans less the inner box's, pixel by pixel at the edges, and where both are
                // whole (the box's cover less the inner's, none where the inner covers what the box does) one run.
                {
                        struct ui_spans outer, hole;
                        b32 holed, a, b;
                        int inside;

                        if (!ui_box_spans(box, y, first, width, &outer))
                                continue;
                        holed = ui_box_spans(&inner, y, first, width, &hole);
                        row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                        a = holed ? (hole.a > outer.a ? hole.a : outer.a) : outer.a;
                        b = holed ? (hole.b < outer.b ? hole.b : outer.b) : outer.b;
                        inside = (int)outer.inside - (holed ? (int)hole.inside : 0);
                        if (b <= a || (holed && hole.a >= hole.b))
                                a = b = outer.hi;
                        for (b32 x = outer.lo; x < outer.hi; x++)
                        {
                                int cover;

                                if (x == a && b > a)
                                {
                                        if (inside > 0)
                                                ui_kit_run(row + a, b - a, ui_premultiplied(argb, (unsigned int)inside > UI_FULL ? UI_FULL : (unsigned int)inside));
                                        x = b - 1;
                                        continue;
                                }
                                cover = (int)ui_spans_cover(&outer, x) - (holed ? (int)ui_spans_cover(&hole, x) : 0);
                                if (cover > 0)
                                        row[x] = ui_over(ui_premultiplied(argb, (unsigned int)cover > UI_FULL ? UI_FULL : (unsigned int)cover), row[x]);
                        }
                }
        }
}

// A Gaussian's share left of x sigmas, x from -4 to 4 in sixteenths, 0 to 65535: a blurred edge.
static const unsigned short ui_edge[129] = {
        2, 3, 3, 5, 6, 7, 9, 12, 15, 19, 24, 30, 38, 47, 58, 72, 88, 108, 132, 161, 195, 236, 284, 341, 407, 485, 575, 680,
        801, 941, 1101, 1283, 1491, 1726, 1992, 2291, 2625, 2998, 3413, 3872, 4378, 4934, 5542, 6205, 6924, 7701, 8539, 9437,
        10397, 11420, 12503, 13648, 14852, 16114, 17431, 18801, 20220, 21684, 23188, 24728, 26299, 27894, 29508, 31135,
        32768, 34400, 36027, 37641, 39236, 40807, 42347, 43851, 45315, 46734, 48104, 49421, 50683, 51887, 53032, 54115,
        55138, 56098, 56996, 57834, 58611, 59330, 59993, 60601, 61157, 61663, 62122, 62537, 62910, 63244, 63543, 63809,
        64044, 64252, 64434, 64594, 64734, 64855, 64960, 65050, 65128, 65194, 65251, 65299, 65340, 65374, 65403, 65427,
        65447, 65463, 65477, 65488, 65497, 65505, 65511, 65516, 65520, 65523, 65526, 65528, 65529, 65530, 65532, 65532,
        65533,
};

// The share of a blurred span from low to high (256ths) at x, for a blur of sigma (256ths), 0 to 65535.
static unsigned int ui_blurred(b32 x, b32 low, b32 high, b32 sigma)
{
        b64 a = ((b64)(x - low) * 16) / sigma, b = ((b64)(x - high) * 16) / sigma;
        unsigned int in = a <= -64 ? 0 : a >= 64 ? 65535 : ui_edge[a + 64];
        unsigned int out = b <= -64 ? 0 : b >= 64 ? 65535 : ui_edge[b + 64];

        return in > out ? in - out : 0;
}

/*
        The kit is flat: it draws no shadow and no outline of its own. The owner asked for both to go
        (2026-10-30); a control's states are its fills, its text and its motion. Turning either back on
        is these two numbers, and every shadow and every decorative outline in this file goes through them.
        Rings that are a shape and not an edge (a spinner's track, a swatch's mark of the one chosen) are
        drawn with ui_kit_ring as before.
*/
#define UI_KIT_SHADOWS 0
#define UI_KIT_OUTLINES 0

static fn ui_kit_outline(struct ui address_to ui, const struct ui_box address_to box, b32 thickness, unsigned int argb)
{
#if UI_KIT_OUTLINES
        ui_kit_ring(ui, box, thickness, argb);
#else
        (void)ui, (void)box, (void)thickness, (void)argb;
#endif
}

/*
        A box-shadow: the box moved by dx, dy, grown by spread (all 256ths of a
        device pixel) and blurred by blur (CSS's blur radius, twice the
        Gaussian's sigma), in a colour at its own alpha. The blur is the box's
        two axes blurred apart and multiplied, from a ramp made once, so a shadow
        whose numbers move each frame (a field's, lifting on hover) is drawn
        afresh at no more than a multiply a pixel. Drawn outside the box only,
        as CSS draws an outer shadow: the box's own pixels are its face's.
*/
static fn ui_kit_shadow(struct ui address_to ui, const struct ui_box address_to box, b32 dx, b32 dy, b32 blur, b32 spread,
                        unsigned int argb)
{
        struct ui_box s = *box;
        b32 sigma = blur / 2, reach = blur + 256;

        if (!UI_KIT_SHADOWS)
                return;
        b32 first, last, top, bottom;

        if (!(argb >> 24))
                return;
        s.x0 += dx - spread, s.x1 += dx + spread, s.y0 += dy - spread, s.y1 += dy + spread;
        s.rx = s.rx + spread > 0 ? s.rx + spread : 0;
        s.ry = s.ry + spread > 0 ? s.ry + spread : 0;
        first = (s.x0 - reach) >> 8, last = (s.x1 + reach + 255) >> 8, top = (s.y0 - reach) >> 8, bottom = (s.y1 + reach + 255) >> 8;
        first = first < 0 ? 0 : first;
        last = last > ui->width ? ui->width : last;
        if (last - first > UI_KIT_LINE)
                last = first + UI_KIT_LINE;
        // The horizontal blur is the same on every row: worked out once, into the clip line, and given back zeroed.
        for (b32 x = 0; x < last - first; x++)
        {
                b32 cx = (first + x) * 256 + 128;

                ui_kit_clip[x] = (unsigned short)(sigma > 0 ? ui_blurred(cx, s.x0, s.x1, sigma) : (cx >= s.x0 && cx < s.x1 ? 65535 : 0));
        }
        for (b32 y = top; y < bottom && last > first; y++)
        {
                unsigned int address_to row;
                b32 width = last - first, cy = y * 256 + 128;
                unsigned int vertical;

                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                vertical = sigma > 0 ? ui_blurred(cy, s.y0, s.y1, sigma) : (cy >= s.y0 && cy < s.y1 ? 65535 : 0);
                if (!vertical)
                        continue;
                // The box itself is not shadowed: its cover is taken out of the shadow's.
                ui_box_row(box, y, ui_kit_line, first, width);
                row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                for (b32 x = 0, run = 0; x < width; x++)
                {
                        unsigned int cover = (unsigned int)(((p64)ui_kit_clip[x] * vertical) >> 20); // to UI_FULL (4096)
                        unsigned int inside;

                        // ui_kit_line holds first differences (ui_line_add): the running sum is the box's cover here.
                        run = (unsigned short)(run + ui_kit_line[x]);
                        inside = run;
                        ui_kit_line[x] = 0;
                        cover = inside >= UI_FULL ? 0 : cover * (UI_FULL - inside) / UI_FULL;
                        if (cover)
                                row[x] = ui_over(ui_premultiplied(argb, cover), row[x]);
                }
        }
        memory_zero(ui_kit_clip, (positive)(last > first ? last - first : 0) * sizeof(ui_kit_clip[0]));
}

/* ---- end of the kit's shapes ---- */

/* ---- kit: controls ---- */

/*
        The kit's type scale, its role names kept: Geist in the kit, here the
        faces Canvas ships, a weight of 600 and up the bold, 500 and under the
        regular, and the mono IBM Plex Mono. Sizes and line heights are the
        kit's, in logical pixels.
*/
#define UI_TITLE_LARGE 0  // 600 22/28
#define UI_TITLE_MEDIUM 1 // 600 16/24
#define UI_TITLE_SMALL 2  // 600 14/20
#define UI_BODY_LARGE 3   // 400 16/24
#define UI_BODY_MEDIUM 4  // 400 14/20
#define UI_LABEL_LARGE 5  // 600 14/20
#define UI_LABEL_MEDIUM 6 // 500 12/16
#define UI_MONO 7         // label-medium in the mono: a tag, a key
#define UI_CODE 8         // the mono at 13/1.6: a code well
#define UI_FIELD 9        // a field's text, 15px

static const struct ui_type
{
        unsigned char size, line, bold, mono;
} ui_types[] = {{22, 28, 1, 0}, {16, 24, 1, 0}, {14, 20, 1, 0}, {16, 24, 0, 0}, {14, 20, 0, 0},
                {14, 20, 1, 0}, {12, 16, 0, 0}, {12, 16, 0, 1}, {13, 21, 0, 1}, {15, 20, 0, 0}};

// Text of a role at x on the baseline (device pixels, whole), in a colour at its own alpha, with a stroke; its width.
static int ui_kit_text_width(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb,
                             unsigned int stroke)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to faces[2] = {t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular,
                                               ui->regular};

        return ui_run_in(ui, faces, x, baseline, text, t->size, argb & 0xffffffu, argb >> 24, 0, ui->width, stroke);
}

// Whether any of the rows top to bottom (device pixels) may be written: in the picture, the region and a repaint's band.
static b32 ui_rows_seen(const struct ui address_to ui, int top, int bottom)
{
        return bottom > ui->top && top < ui->bottom && top < ui->height &&
               (ui->band_bottom <= ui->band_top || (bottom > ui->band_top && top < ui->band_bottom));
}

/*
        The same, its width not asked: a run whose rows (ui_run_in's, the face's cell and the room round it) are all
        outside those the ui may write -- a repaint's band, the region's top and bottom -- writes nothing there, and
        is not measured either. A repaint of a row of a list was measuring every other row's every word.
*/
static fn ui_kit_text(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb, unsigned int stroke)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to face = t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular;
        font_metrics metrics = font_metrics_at(face, (p32)(t->size * ui->scale) << 6);
        int rows = metrics.cell_height + 4 * ui->scale, top = baseline - metrics.baseline - 2 * ui->scale;

        rows = rows > UI_SCRATCH_ROWS * ui->scale ? UI_SCRATCH_ROWS * ui->scale : rows;
        if (ui_rows_seen(ui, top, top + rows))
                ui_kit_text_width(ui, x, baseline, text, type, argb, stroke);
}

static int ui_kit_measure(struct ui address_to ui, string_address text, int type)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to faces[2] = {t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular,
                                               ui->regular};

        return ui_run_in(ui, faces, 0, 0, text, t->size, 0, 0, 0, ui->width, 0);
}

// The baseline of a role's line box whose top is top (device pixels): the line's half-leading above the face's ascent.
static int ui_kit_baseline(struct ui address_to ui, int top, int type)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to face = t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular;
        font_metrics metrics = font_metrics_at(face, (p32)(t->size * ui->scale) << 6);

        return top + (t->line * ui->scale - metrics.cell_height) / 2 + metrics.baseline;
}

/*
        The press channel, --press-x/-y and --press-tx/-ty: a drag holds the
        pull (KB), a release springs it home (pressSpring), and a new press
        freezes a spring where it is (cancelSprings leaves the last values
        written) until the pull is moved or let go of.
*/
struct ui_squish
{
        struct ui_spring x, y;
        struct ui_squash shown, pull;
        b32 holding, pulled;
};

static fn ui_squish_press(struct ui_squish address_to q)
{
        ui_spring_stop(&q->x);
        ui_spring_stop(&q->y);
        q->holding = 1;
        q->pulled = 0;
        memory_zero(&q->pull, sizeof(q->pull));
}

static fn ui_squish_move(struct ui_squish address_to q, struct ui_squash pull)
{
        // Reduced motion: no squash, and so nothing to spring home.
        if (ui_kit.motion)
                memory_zero(&pull, sizeof(pull));
        q->pull = q->shown = pull;
        q->pulled = 1;
}

static fn ui_squish_release(struct ui_squish address_to q)
{
        struct ui_squash s = q->pull;

        q->holding = 0;
        if (q->pulled && (s.sx > UI_REST_SCALE || s.sx < -UI_REST_SCALE || s.sy > UI_REST_SCALE || s.sy < -UI_REST_SCALE))
        {
                ui_spring_go(&q->x, s.sx, s.tx);
                ui_spring_go(&q->y, s.sy, s.ty);
                q->shown = s;
        }
        else
                memory_zero(&q->shown, sizeof(q->shown));
}

static struct ui_squash ui_squish_at(struct ui_squish address_to q, b64 now)
{
        if (q->holding)
                return q->shown;
        if (q->x.live | q->y.live)
        {
                ui_kit.busy |= ui_spring_at(&q->x, now) | ui_spring_at(&q->y, now);
                q->shown.sx = q->x.value[0], q->shown.tx = q->x.value[1];
                q->shown.sy = q->y.value[0], q->shown.ty = q->y.value[1];
        }
        return q->shown;
}

/*
        A button's drawn transform: ui.css gives a button `transition: transform
        .1s var(--ease)`, so each frame the values ui.js writes start a 100 ms
        transition from where the drawn transform is, and what is drawn at a
        frame is that transition's value there. Four channels as one value, as
        CSS turns one transform property.
*/
struct ui_chase
{
        b64 from[4], to[4], start;
};

static fn ui_chase_at(struct ui_chase address_to c, const b64 address_to want, b64 now, b64 address_to out)
{
        b64 p = c->start ? ui_bezier_at(&ui_ease, ui_progress(now - c->start, 100 * UI_MS)) : UI_ONE;
        b32 changed = 0;

        for (int k = 0; k < 4; k++)
        {
                out[k] = c->from[k] + ui_qmul(c->to[k] - c->from[k], p);
                changed |= want[k] != c->to[k];
        }
        if (changed && !ui_kit.motion)
        {
                for (int k = 0; k < 4; k++)
                        c->from[k] = out[k], c->to[k] = want[k];
                c->start = now;
        }
        else if (changed)
                for (int k = 0; k < 4; k++)
                        c->from[k] = c->to[k] = out[k] = want[k];
        if (c->start && now - c->start >= 100 * UI_MS && !changed)
                c->start = 0;
        if (c->start)
                ui_kit.busy = 1;
}

/*
        One control: its kind, its box in logical pixels (a row for a switch, a
        checkbox and each radio), what it holds and the motion it is in. A group
        of radios and a strip of tabs are each one control with items. The
        drawing reads the motion at ui_kit.now and marks busy while any of it
        moves; the pointer and the keys come in through ui_kit_pointer and
        ui_kit_key, which answer whether the control changed.
*/
#define UI_BUTTON 1
#define UI_SWITCH 2
#define UI_CHECKBOX 3
#define UI_RADIOS 4
#define UI_SLIDER 5
#define UI_TABS 6
#define UI_INPUT 7
#define UI_PROGRESS 8
#define UI_BADGE 9
#define UI_TAG 10
#define UI_AVATAR 11
#define UI_CALLOUT 12
#define UI_CODE_WELL 13
#define UI_KBD 14
#define UI_SPINNER 15
#define UI_CARD 16
#define UI_WINDOW 17
#define UI_HEADING 18 // a line of text in a role (a heading, a status line)
#define UI_RULE 19   // the hairline <hr>
#define UI_LIST_ROW 20 // a row of a list with no control of the kit's: its hover layer and its press ripple, edge to edge
#define UI_SWATCHES 21 // a colour each (items, rrggbb), 20 round and 8 apart, its right edge the box's, the chosen (at) ringed

// A button's variant; a slider's; tabs'; a badge's and a callout's (as roles: UI_SUCCESS...).
#define UI_BUTTON_DEFAULT 0
#define UI_BUTTON_PRIMARY 1
#define UI_BUTTON_OUTLINED 2
#define UI_BUTTON_TEXT 3
#define UI_SLIDER_STEPS 1
#define UI_SLIDER_LINE 2
#define UI_TABS_UNDERLINE 0
#define UI_TABS_SEGMENTED 1
#define UI_TABS_VERTICAL 2 // the segmented strip stood on its end: a column of items h tall, the pip travelling along y
#define UI_ITEMS 16
#define UI_RADIO_PITCH 64 // a radio row and the gap under it (48 + 16, the card's gap)

struct ui_control
{
        unsigned char kind, variant;
        b32 on, disabled;                  // checked; a radio group's or tabs' chosen item is at
        int at;
        int x, y, w, h;                    // logical pixels
        const char address_to label;
        const char address_to items[UI_ITEMS];
        int count;
        b64 value, low, high, step;        // a slider's (Q30); progress 0 to UI_ONE, below 0 indeterminate
        char text[64];                     // a field's
        int role;                          // a badge's, a callout's, a text's colour role; a text's type in variant
        // The pointer.
        b32 hover, down, moved, focused;
        int item;                          // the item under the pointer or pressed
        int press_x, press_y, pointer_x, pointer_y; // device pixels in the window
        b64 pressed_at;
        // Motion.
        struct ui_squish squish;
        struct ui_chase drawn;
        struct ui_ripple ripple[3];
        struct ui_tween layer, fill, ink, label_ink, stroke, scale, extent, shadow, pip_x, pip_w, pip_y;
        struct ui_tween item_ink[UI_ITEMS], item_scale[UI_ITEMS];
        struct ui_stretch dom, item_dom[UI_ITEMS];
        int dom_origin;                    // -1 the far edge, 1 the near (left, top), 0 the middle
        struct ui_spring held_squish;      // --sw-squish or --squish let go
        b64 bend, bend_target;             // --sw-squish / --squish held, Q30 over one; the slider's jello target
        int field_state;                   // a field's: rest, hover or focus, as its shadows were last sent to
        b64 field_start;                   // and when they left for it
        int squish_origin;                 // 1 left, -1 right
        b64 pip_squash, pip_press;         // the radio pip's keyframes: their first frames (0 none, 1 pending)
        b64 shadow_from[4][5], shadow_to[4][5]; // a field's four box-shadows: x, y, blur, spread, colour
        // The slider.
        struct ui_slide slide;
        b32 sliding, bounce, dragged;
        b64 slide_from;
        int hover_x;                       // where the pointer hovers over it, device pixels, or -1
        struct ui_tween spot;              // the spot's opacity
        int spot_x;
        b64 spot_gone;
        b64 out_at;                        // the pointer left: hovering ends 200 ms after
        b64 last_frame;
        struct ui_text address_to edit;    // a field's text: the engine's widget, which types, selects, pastes and draws it
        b64 placed;                        // the kit's epoch it was last placed in; 0 before its first frame
        // The row the control is in, logical pixels: what its press ripples across and its hover lights, edge to edge.
        // None (row_w 0) is the kit's own: the box and the card's padding round it (24 across, 8 down, ui.css's ripple-clip).
        int row_x, row_y, row_w, row_h;
};

// The row a control's ripple and hover cover, in logical pixels.
static fn ui_control_row(const struct ui_control address_to c, int address_to x, int address_to y, int address_to w, int address_to h)
{
        if (c->row_w > 0)
        {
                *x = c->row_x, *y = c->row_y, *w = c->row_w, *h = c->row_h;
                return;
        }
        *x = c->x - 24, *y = c->y - 8, *w = c->w + 48, *h = c->h + 16;
}

// A control to be placed at its next frame, as if it had just appeared: a page shown again, a list rebuilt, a row scrolled in.
static inline fn ui_control_forget(struct ui_control address_to c)
{
        c->placed = 0;
}

// The pieces a control needs in device pixels.
static inline b32 ui_dev(const struct ui address_to ui, int logical) { return logical * ui->scale; }
static inline b32 ui_q8(const struct ui address_to ui, int logical) { return logical * ui->scale * 256; }

// A determinate progress's fill as a whole: at most the track's end, UI_ONE (a value past its whole fills the track, no more).
static inline b64 ui_progress_whole(b64 value) { return value > UI_ONE ? UI_ONE : value; }

// A control before its first frame: its transitions have no value yet, so the first frame puts each at its state's.
static fn ui_control_start(struct ui_control address_to c)
{
        ui_tween_put(&c->extent, c->on ? UI_ONE : 0, UI_TWEEN_NUMBER);
        ui_tween_put(&c->spot, 0, UI_TWEEN_NUMBER);
        for (int k = 0; k < UI_ITEMS; k++)
                ui_tween_put(&c->item_scale[k], k == c->at ? 0 : UI_ONE, UI_TWEEN_NUMBER);
        if (c->kind == UI_PROGRESS && c->value >= 0)
                ui_tween_put(&c->extent, ui_progress_whole(c->value), UI_TWEEN_NUMBER);
        c->hover_x = -1;
        c->item = -1;
        c->pip_squash = c->pip_press = 0;
        for (int k = 0; k < 4; k++)
                c->drawn.from[k] = c->drawn.to[k] = 0;
}

// A tween's value now, marking the frame busy while it moves.
static inline b64 ui_kit_tween(struct ui_tween address_to t)
{
        b32 live = 0;
        b64 v = ui_tween_at(t, ui_kit.now, &live);

        ui_kit.busy |= live;
        return v;
}

static inline unsigned int ui_kit_colour(struct ui_tween address_to t)
{
        return ui_tween_argb(ui_kit_tween(t));
}

// A colour to transition to over the kit's .2s (or at once with reduced motion); its first is where it starts.
static inline fn ui_kit_colour_to(struct ui_tween address_to t, unsigned int argb, int ms, const struct ui_bezier address_to ease)
{
        if (!t->placed)
                ui_tween_put(t, ui_tween_colour(argb), UI_TWEEN_COLOUR);
        else
                ui_tween_set(t, ui_tween_colour(argb), ui_kit.motion ? 0 : ms * UI_MS, ease);
}

static inline fn ui_kit_number_to(struct ui_tween address_to t, b64 value, int ms, const struct ui_bezier address_to ease)
{
        if (!t->placed)
                ui_tween_put(t, value, UI_TWEEN_NUMBER);
        else
                ui_tween_set(t, value, ui_kit.motion ? 0 : ms * UI_MS, ease);
}

/*
        A control's state layer: the ink laid over its face at .06 hovered, .10 pressed and none otherwise (none while off),
        eased over .2s. ui_kit_layer_to sets its target, ui_kit_layer is its percentage now (0 for none), and
        ui_kit_layer_fill lays the ink over a box when there is one.
*/
static inline fn ui_kit_layer_to(struct ui_control address_to c, b32 off)
{
        ui_kit_number_to(&c->layer, off ? 0 : c->down ? UI_Q2(10) : c->hover ? UI_Q2(6) : 0, 200, &ui_ease);
}

static inline unsigned int ui_kit_layer(struct ui_control address_to c)
{
        return (unsigned int)((ui_kit_tween(&c->layer) * 100 + UI_HALF) >> UI_Q);
}

static inline fn ui_kit_layer_fill(struct ui address_to ui, const struct ui_box address_to box, unsigned int ink, unsigned int layer)
{
        if (layer)
                ui_kit_fill(ui, box, ui_mix(ink, layer, 0), null);
}

// A control's label, unstroked, its x dx and its line's top dy logical pixels from the control's corner, in a type and ink.
static inline fn ui_control_label(struct ui address_to ui, const struct ui_control address_to c, int dx, int dy, int type, unsigned int ink)
{
        ui_kit_text(ui, ui_dev(ui, c->x + dx), ui_kit_baseline(ui, ui_dev(ui, c->y + dy), type), (string_address)c->label, type, ink, 0);
}

/*
        The ripples of a control in a colour: measured from the top left of
        their clip box (the control's box and the margin round it, x0, y0 in
        device 256ths, as laid out), drawn through the control's transform
        (sx, sy about ox, oy, then tx, ty) and cut to clip.
*/
struct ui_place
{
        b64 sx, sy;
        b32 ox, oy, tx, ty;
};

static fn ui_kit_ripples_on(struct ui address_to ui, struct ui_control address_to c, b32 x0, b32 y0, const struct ui_place address_to place,
                            const struct ui_box address_to clip, unsigned int argb, const struct ui_box address_to pill)
{
        for (int k = 0; k < 3; k++)
        {
                b64 cx, cy, radius, opacity;
                struct ui_box dot;
                struct ui_place frame = *place;
                const struct ui_box address_to cut = clip;
                b32 ox = x0, oy = y0;

                if (!ui_ripple_at(&c->ripple[k], ui_kit.now, &cx, &cy, &radius, &opacity))
                        continue;
                ui_kit.busy = 1;
                // A tab's ripple that rides the indicator is measured from the indicator's own rectangle this frame, and cut to it.
                if (pill && c->ripple[k].rides)
                {
                        struct ui_place still = {UI_ONE, UI_ONE, 0, 0, 0, 0};

                        ox = pill->x0 - ui_q8(ui, 8), oy = pill->y0 - ui_q8(ui, 8), frame = still, cut = pill;
                }
                dot.x0 = ox + (b32)(((cx - radius) * ui->scale) >> 22);
                dot.x1 = ox + (b32)(((cx + radius) * ui->scale) >> 22);
                dot.y0 = oy + (b32)(((cy - radius) * ui->scale) >> 22);
                dot.y1 = oy + (b32)(((cy + radius) * ui->scale) >> 22);
                dot.rx = dot.ry = (b32)((radius * ui->scale) >> 22);
                dot = ui_box_transform(dot, frame.sx, frame.sy, frame.ox, frame.oy, frame.tx, frame.ty);
                ui_kit_fill(ui, &dot, ui_mix(argb, (unsigned int)((opacity * 100 + UI_HALF) >> UI_Q), 0), cut);
        }
}

static fn ui_kit_ripples(struct ui address_to ui, struct ui_control address_to c, b32 x0, b32 y0, const struct ui_place address_to place,
                         const struct ui_box address_to clip, unsigned int argb)
{
        ui_kit_ripples_on(ui, c, x0, y0, place, clip, argb, null);
}

// A ripple from a press at device x, y in a box (device px) whose clip reaches margin (logical) past it.
static struct ui_ripple address_to ui_kit_ripple_start(const struct ui address_to ui, struct ui_control address_to c, int x, int y, int bx, int by, int bw, int bh,
                                                       int margin)
{
        struct ui_ripple address_to r = &c->ripple[0];

        if (ui_kit.motion)
                return null;
        for (int k = 0; k < 3; k++)
                if (!c->ripple[k].live)
                {
                        r = &c->ripple[k];
                        break;
                }
        ui_ripple_go(r, ui_qdiv(ui_q(x - bx), ui_q(ui->scale)), ui_qdiv(ui_q(y - by), ui_q(ui->scale)), ui_qdiv(ui_q(bw), ui_q(ui->scale)),
                     ui_qdiv(ui_q(bh), ui_q(ui->scale)), ui_q(margin));
        return r;
}

static fn ui_kit_ripples_exit(struct ui_control address_to c)
{
        for (int k = 0; k < 3; k++)
                ui_ripple_exit(&c->ripple[k], ui_kit.now);
}

/*
        A button: a pill 36 tall, 14 in at each side (10 for a text button),
        the label-large text in its middle. Its fill and ink go over .2s
        --ease; its state layer (the ink at .06 hovered, .10 pressed, .2s) is
        over the fill; a press ripples in the ink at .2, cut to the pill; a drag
        squashes it (BB) and the release springs it home, drawn through the
        .1s transform transition. Disabled: the ink at 38%, the fill at 8%.
*/
static fn ui_button_targets(struct ui_control address_to c)
{
        unsigned int fill = 0, ink;

        switch (c->variant)
        {
        case UI_BUTTON_PRIMARY:
                fill = c->down ? ui_mix(ui_role(UI_ON_SURFACE), 16, ui_role(UI_PRIMARY)) : ui_role(UI_PRIMARY);
                ink = ui_role(UI_ON_PRIMARY);
                break;
        case UI_BUTTON_OUTLINED: ink = ui_role(UI_ON_SURFACE); break;
        case UI_BUTTON_TEXT: ink = ui_role(UI_PRIMARY); break;
        default:
                fill = ui_role(UI_SECONDARY_CONTAINER);
                ink = ui_role(UI_ON_SECONDARY_CONTAINER);
                break;
        }
        if (c->disabled)
        {
                fill = ui_mix(ui_role(UI_ON_SURFACE), 8, 0);
                ink = ui_mix(ui_role(UI_ON_SURFACE), 38, 0);
        }
        ui_kit_colour_to(&c->fill, fill, 200, &ui_ease);
        ui_kit_colour_to(&c->ink, ink, 200, &ui_ease);
        ui_kit_layer_to(c, c->disabled);
}

// The transform a control is drawn with this frame: scale less one and nudge (device 256ths), about its middle.
static fn ui_kit_press_transform(const struct ui address_to ui, struct ui_control address_to c, b32 chased, b64 address_to sx,
                                 b64 address_to sy, b32 address_to tx, b32 address_to ty)
{
        struct ui_squash s = ui_squish_at(&c->squish, ui_kit.now);
        b64 want[4] = {s.sx, s.sy, s.tx, s.ty}, drawn[4];

        if (chased)
                ui_chase_at(&c->drawn, want, ui_kit.now, drawn);
        else
                for (int k = 0; k < 4; k++)
                        drawn[k] = want[k];
        *sx = UI_ONE + drawn[0];
        *sy = UI_ONE + drawn[1];
        *tx = (b32)((drawn[2] * ui->scale) >> 22);
        *ty = (b32)((drawn[3] * ui->scale) >> 22);
}

static fn ui_button_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b64 sx, sy;
        b32 tx, ty, ox = ui_q8(ui, c->x) + ui_q8(ui, c->w) / 2, oy = ui_q8(ui, c->y) + ui_q8(ui, c->h) / 2;
        struct ui_box face = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(c->h) / 2);
        unsigned int fill, ink, layer;
        int width, baseline, x;

        ui_button_targets(c);
        fill = ui_kit_colour(&c->fill);
        ink = ui_kit_colour(&c->ink);
        layer = ui_kit_layer(c);
        ui_kit_press_transform(ui, c, 1, &sx, &sy, &tx, &ty);
        face = ui_box_transform(face, sx, sy, ox, oy, tx, ty);
        ui_kit_fill(ui, &face, fill, null);
        if (c->variant == UI_BUTTON_OUTLINED && !c->disabled)
                ui_kit_outline(ui, &face, ui_q8(ui, 1), ui_role(c->focused ? UI_PRIMARY : c->hover || c->down ? UI_OUTLINE : UI_OUTLINE_VARIANT));
        ui_kit_layer_fill(ui, &face, ink, layer);
        {
                struct ui_place place = {sx, sy, ox, oy, tx, ty};

                ui_kit_ripples(ui, c, ui_q8(ui, c->x - 8), ui_q8(ui, c->y - 8), &place, &face, ink);
        }
        width = ui_kit_measure(ui, (string_address)c->label, UI_LABEL_LARGE);
        // The label at whole pixels where the transform takes its middle: never scaled, never resampled.
        x = (ox + tx) / 256 - width / 2;
        baseline = ui_kit_baseline(ui, (oy + ty) / 256 - ui_dev(ui, ui_types[UI_LABEL_LARGE].line) / 2, UI_LABEL_LARGE);
        ui_kit_text(ui, x, baseline, (string_address)c->label, UI_LABEL_LARGE, ink, 0);
}

/*
        A switch or a checkbox (the kit draws them alike): the row's label at the
        left in body-large, half the ink, the accent hovered and full ink with a
        stroke when on (.2s); at the right a 52 by 28 pill, the rest in the
        control-off colour and the fill in the accent, which grow and shrink
        across it over .2s (dragged, they part by 8 and follow the hand), hovered
        1.05 and pressed 1.1 (.15s), squashed toward a drag (--sw-squish) and
        sprung home. A toggle stretches the label 1.11 toward where the knob went
        (the domino, at 0.6 of it on the text).
*/
static fn ui_toggle_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b64 sx, sy, extent, scale, squish, dom;
        b32 tx, ty, live = 0;
        b32 rx = ui_q8(ui, c->x), ry = ui_q8(ui, c->y), rw = ui_q8(ui, c->w), rh = ui_q8(ui, c->h);
        b32 ox = rx + rw / 2, oy = ry + rh / 2;
        b32 bx = rx + rw - ui_q8(ui, 52), by = oy - ui_q8(ui, 14), bw = ui_q8(ui, 52), bh = ui_q8(ui, 28);
        b32 gap = 0, fill_w, rest_w, origin_x;
        struct ui_box fill, rest, clip;
        unsigned int label;
        int width, baseline, x;

        ui_kit_colour_to(&c->label_ink, c->on ? ui_role(UI_ON_SURFACE) : c->hover && !c->disabled ? ui_role(UI_PRIMARY)
                                                                                      : ui_mix(ui_role(UI_ON_SURFACE), 50, 0), 200, &ui_ease);
        ui_kit_number_to(&c->stroke, c->on || (c->hover && !c->disabled) ? 64 * UI_ONE : 0, 200, &ui_ease);
        ui_kit_number_to(&c->scale, c->down ? UI_Q2(110) : c->hover ? UI_Q2(105) : UI_ONE, 150, &ui_ease);
        // A switch's state layer over its row, as a list row's (ui_list_row_draw): the ink at .06 hovered, .10 pressed, .2s.
        if (c->kind == UI_SWITCH)
                ui_kit_layer_to(c, c->disabled);
        if (!c->moved)
                ui_kit_number_to(&c->extent, c->on ? UI_ONE : 0, 200, &ui_ease);
        label = ui_kit_colour(&c->label_ink);
        extent = ui_kit_tween(&c->extent);
        scale = ui_kit_tween(&c->scale);
        // --sw-squish: held while dragged, else the spring that let it go.
        squish = c->moved ? (ui_kit.motion ? 0 : c->bend) : (ui_kit.busy |= ui_spring_at(&c->held_squish, ui_kit.now), c->held_squish.value[0]);
        dom = ui_stretch_at(&c->dom, ui_kit.now, &live);
        ui_kit.busy |= live;
        ui_kit_press_transform(ui, c, 0, &sx, &sy, &tx, &ty);

        // The two bars across the pill: dragged, each 4 short of the hand.
        if (c->moved)
                gap = ui_q8(ui, 4);
        fill_w = (b32)ui_qmul(extent, bw) - gap;
        rest_w = bw - (b32)ui_qmul(extent, bw) - gap;
        fill = (struct ui_box){bx, by, bx + (fill_w > 0 ? fill_w : 0), by + bh, ui_q8(ui, 14), ui_q8(ui, 14)};
        rest = (struct ui_box){bx + bw - (rest_w > 0 ? rest_w : 0), by, bx + bw, by + bh, ui_q8(ui, 14), ui_q8(ui, 14)};
        // The bars' own transform: scaleX(--sw-squish) then the hover scale, about the squish's origin (the middle at rest).
        origin_x = c->squish_origin > 0 ? bx : c->squish_origin < 0 ? bx + bw : bx + bw / 2;
        fill = ui_box_transform(fill, ui_qmul(UI_ONE + squish, scale), scale, origin_x, oy, 0, 0);
        rest = ui_box_transform(rest, ui_qmul(UI_ONE + squish, scale), scale, origin_x, oy, 0, 0);
        fill = ui_box_transform(fill, sx, sy, ox, oy, tx, ty);
        rest = ui_box_transform(rest, sx, sy, ox, oy, tx, ty);
        // The row the state layer and the ripple share: its box, edge to edge (by default to the card's edges), and not moved
        // by the press, so a switch's hover and its ripple are one rect, as a list row's are. The ripple itself still moves.
        {
                int cx, cy, cw, ch;

                ui_control_row(c, &cx, &cy, &cw, &ch);
                clip = (struct ui_box){ui_q8(ui, cx), ui_q8(ui, cy), ui_q8(ui, cx + cw), ui_q8(ui, cy + ch), 0, 0};
        }
        if (c->kind == UI_SWITCH)
        {
                ui_kit_layer_fill(ui, &clip, ui_role(UI_ON_SURFACE), ui_kit_layer(c));
        }
        if (rest_w > 0)
                ui_kit_fill(ui, &rest, ui_role(UI_CONTROL_OFF), null);
        if (fill_w > 0)
                ui_kit_fill(ui, &fill, ui_role(UI_PRIMARY), null);

        // The label: its box shrink-wraps the text, stretched 0.6 of the domino about its trailing edge, and the text
        // laid at whole pixels where the stretch takes the box's middle.
        width = ui_kit_measure(ui, (string_address)c->label, UI_BODY_LARGE);
        {
                b32 left = rx, middle = left + width * 128, anchor = c->dom_origin > 0 ? left : left + width * 256;
                b64 stretch = UI_ONE + ui_qmul(dom - UI_ONE, UI_Q2(60));

                middle = anchor + (b32)ui_qmul(middle - anchor, stretch);
                middle = ox + tx + (b32)ui_qmul(middle - ox, sx);
                x = middle / 256 - width / 2;
        }
        baseline = ui_kit_baseline(ui, (oy + ty) / 256 - ui_dev(ui, 12), UI_BODY_LARGE);
        ui_kit_text(ui, x, baseline, (string_address)c->label, UI_BODY_LARGE, c->disabled ? ui_mix(label, 38, 0) : label,
                    (unsigned int)(ui_kit_tween(&c->stroke) >> UI_Q));
        // The ripple, in the accent, over the row: its clip is the label's last child, so it lies over the bars and the text.
        {
                struct ui_place place = {sx, sy, ox, oy, tx, ty};
                int cx, cy, cw, ch;

                ui_control_row(c, &cx, &cy, &cw, &ch);
                ui_kit_ripples(ui, c, ui_q8(ui, cx), ui_q8(ui, cy), &place, &clip, ui_role(UI_PRIMARY));
        }
}

/*
        A group of radios, one row each UI_RADIO_PITCH apart: each a 28 circle in
        the middle of the switch's column, the rest a control-off circle that
        shrinks away (.2s linear) when it is the one chosen, and over them the
        one accent pip that flies to the chosen (.4s --ease-emphasized) with
        the pip-squash keyframes (.45s), the rows it rolls over stretched along
        the travel (1.34, as the pip reaches each over 240 ms) and their labels
        at 0.55 of it. Pressing the chosen one again squishes it (0.84) and the
        pip (pip-press, .3s).
*/
static b32 ui_radio_row_y(const struct ui_control address_to c, int item)
{
        return c->y + item * UI_RADIO_PITCH;
}

static fn ui_radios_draw(struct ui address_to ui, struct ui_control address_to c)
{
        static const struct ui_stop squash_x[] = {{0, UI_ONE}, {UI_Q2(20), UI_Q2(80)}, {UI_Q2(60), UI_Q2(110)}, {UI_ONE, UI_ONE}};
        static const struct ui_stop squash_y[] = {{0, UI_ONE}, {UI_Q2(20), UI_Q2(127)}, {UI_Q2(60), UI_Q2(91)}, {UI_ONE, UI_ONE}};
        static const struct ui_stop press[] = {{0, UI_ONE}, {UI_Q2(42), UI_Q2(82)}, {UI_ONE, UI_ONE}};
        b64 sx, sy, pip_y, psx = UI_ONE, psy = UI_ONE;
        b32 tx, ty, live = 0;
        b32 cx = ui_q8(ui, c->x + c->w - 12 - 14); // the circles' middle: 12 in from the row's right, 28 wide

        ui_kit_press_transform(ui, c, 0, &sx, &sy, &tx, &ty);
        for (int item = 0; item < c->count; item++)
        {
                b32 ry = ui_q8(ui, ui_radio_row_y(c, item)), rh = ui_q8(ui, c->h), oy = ry + rh / 2, rx = ui_q8(ui, c->x), rw = ui_q8(ui, c->w);
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live), hover, rest;
                b32 origin = c->dom_origin > 0 ? oy - ui_q8(ui, 14) : c->dom_origin < 0 ? oy + ui_q8(ui, 14) : oy;
                b32 lrow = c->item == item;
                struct ui_box dot = {cx - ui_q8(ui, 14), oy - ui_q8(ui, 14), cx + ui_q8(ui, 14), oy + ui_q8(ui, 14), ui_q8(ui, 14), ui_q8(ui, 14)};
                unsigned int label;
                int baseline, x;
                b64 row_sx = lrow ? sx : UI_ONE, row_sy = lrow ? sy : UI_ONE;
                b32 row_tx = lrow ? tx : 0, row_ty = lrow ? ty : 0;
                struct ui_box clip;

                ui_kit_number_to(&c->item_scale[item], item == c->at ? 0 : UI_ONE, 200, null);
                ui_kit_colour_to(&c->item_ink[item], item == c->at ? ui_role(UI_ON_SURFACE) : lrow && c->hover ? ui_role(UI_PRIMARY)
                                                                                                        : ui_mix(ui_role(UI_ON_SURFACE), 50, 0), 200, &ui_ease);
                hover = lrow ? (c->down ? UI_Q2(110) : c->hover ? UI_Q2(105) : UI_ONE) : UI_ONE;
                {
                        // The rest's scale goes linearly (.2s linear): a straight bezier.
                        static const struct ui_bezier linear = UI_BEZIER(0, 0, UI_ONE, UI_ONE);

                        if (c->item_scale[item].pending)
                                c->item_scale[item].ease = &linear;
                }
                rest = ui_kit_tween(&c->item_scale[item]);
                clip = ui_box_transform((struct ui_box){rx - ui_q8(ui, 24), ry - ui_q8(ui, 8), rx + rw + ui_q8(ui, 24), ry + rh + ui_q8(ui, 8), 0, 0},
                                        row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty);
                // The rest circle: scale(rest) about its middle, inside the bars' scaleY(dom) scale(hover) about the origin.
                if (rest > 0)
                {
                        struct ui_box r = ui_box_transform(dot, rest, rest, cx, oy, 0, 0);

                        r = ui_box_transform(r, hover, ui_qmul(dom, hover), cx, origin, 0, 0);
                        r = ui_box_transform(r, row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty);
                        ui_kit_fill(ui, &r, ui_role(UI_CONTROL_OFF), null);
                }
                label = ui_kit_colour(&c->item_ink[item]);
                x = (rx + row_tx) / 256;
                {
                        // The label stretches 0.55 of the row's along y about the same origin: its line moves with it.
                        b64 stretch = UI_ONE + ui_qmul(dom - UI_ONE, UI_Q2(55));
                        b32 middle = origin + (b32)ui_qmul(oy - origin, stretch);

                        baseline = ui_kit_baseline(ui, (middle + row_ty) / 256 - ui_dev(ui, 12), UI_BODY_LARGE);
                }
                ui_kit_text(ui, x, baseline, (string_address)c->items[item], UI_BODY_LARGE, label,
                            item == c->at || (lrow && c->hover) ? 64 : 0);
                if (lrow)
                {
                        struct ui_place place = {row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty};

                        ui_kit_ripples(ui, c, rx - ui_q8(ui, 24), ry - ui_q8(ui, 8), &place, &clip, ui_role(UI_PRIMARY));
                }
        }
        ui_kit.busy |= live;
        // The pip.
        // The pip's place is the group's own (from its top), so a group the layout moves takes its pip with it at once.
        ui_kit_number_to(&c->pip_y, ui_q(ui_radio_row_y(c, c->at) - c->y + c->h / 2), 400, &ui_ease);
        pip_y = ui_q(c->y) + ui_kit_tween(&c->pip_y);
        if (c->pip_squash == 1)
                c->pip_squash = ui_kit.now;
        if (c->pip_press == 1)
                c->pip_press = ui_kit.now;
        if (c->pip_squash)
        {
                b32 l = 0;

                psx = ui_keyframes_at(squash_x, 4, &ui_ease, c->pip_squash, 450 * UI_MS, ui_kit.now, &l);
                psy = ui_keyframes_at(squash_y, 4, &ui_ease, c->pip_squash, 450 * UI_MS, ui_kit.now, &l);
                if (!l)
                        c->pip_squash = 0;
                ui_kit.busy |= l;
        }
        else if (c->pip_press)
        {
                b32 l = 0;

                psx = psy = ui_keyframes_at(press, 3, &ui_ease, c->pip_press, 300 * UI_MS, ui_kit.now, &l);
                if (!l)
                        c->pip_press = 0;
                ui_kit.busy |= l;
        }
        if (c->count > 0) // a group with no items has no chosen one to show
        {
                b32 py = (b32)((pip_y * ui->scale) >> 22);
                struct ui_box pip = {cx - ui_q8(ui, 14), py - ui_q8(ui, 14), cx + ui_q8(ui, 14), py + ui_q8(ui, 14), ui_q8(ui, 14), ui_q8(ui, 14)};

                pip = ui_box_transform(pip, psx, psy, cx, py, 0, 0);
                ui_kit_fill(ui, &pip, ui_role(UI_PRIMARY), null);
        }
}

// The radio domino: the rows between the old and the new, stretched as the pip passes each (240 ms of flight).
static fn ui_radios_choose(struct ui_control address_to c, int item)
{
        int from = c->at, distance = item > from ? item - from : from - item;

        if (item == from)
                return;
        c->dom_origin = item > from ? 1 : -1;
        if (!ui_kit.motion)
                for (int k = 0; k < c->count; k++)
                {
                        int along = (item > from ? k - from : from - k);

                        if (along < 0 || along > distance)
                                continue;
                        ui_stretch_go(&c->item_dom[k], ui_kit.now + (b64)along * 240 * UI_MS / distance, UI_Q2(134));
                }
        c->at = item;
        c->pip_squash = ui_kit.motion ? 0 : 1;
}

/*
        A slider: its label and value on one line (body-large; both the accent
        and stroked while hovered), under them a 48 tall track with the fill to
        the value (less 2) and the rest after it (plus 2): pills --bar-h tall
        (16, hovered 20, pressed 12; a line slider 6, 8, 4), which snap, as the
        kit's unregistered properties do. Hovered, a ghost previews where a
        press would land (the accent at 40% on the control-off); a press slides
        there and lands with a squish; a drag follows the hand and stretches
        past an end like a band; the spot under the hand fades in .3s.
*/
static b64 ui_slider_ratio(const struct ui_control address_to c, b64 value)
{
        return c->high > c->low ? ui_qdiv(value - c->low, c->high - c->low) : 0;
}

// A value as the range input keeps it: snapped to its step from its low end, inside its ends.
static b64 ui_slider_snap(const struct ui_control address_to c, b64 value)
{
        b64 step = c->step > 0 ? c->step : UI_ONE, n = ui_qdiv(value - c->low, step);
        b64 last = c->high > c->low ? ui_qdiv(c->high - c->low, step) >> UI_Q : 0; // the last step the range holds

        // The nearest step, and past the range's end the last step under it, never the end itself when no step is there.
        n = (n + UI_HALF) >> UI_Q;
        n = n < 0 ? 0 : n > last ? last : n;
        value = c->low + ui_qmul(n << UI_Q, step);
        return value < c->low ? c->low : value > c->high ? c->high : value;
}

static fn ui_slider_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b64 sx, sy, value, ratio, squish = 0;
        b32 tx, ty, live = 0;
        b32 rx = ui_q8(ui, c->x), ry = ui_q8(ui, c->y), rw = ui_q8(ui, c->w), rh = ui_q8(ui, c->h);
        b32 ox = rx + rw / 2, oy = ry + rh / 2;
        b32 line = c->variant & UI_SLIDER_LINE;
        int thumb = line ? 20 : c->down ? 12 : c->hover ? 20 : 16;
        int bar = line ? (c->down ? 4 : c->hover ? 8 : 6) : thumb;
        b32 track_y = ry + rh - ui_q8(ui, 8) - ui_q8(ui, 24); // the bars' middle: 48 tall, 8 above the row's bottom
        b32 stop, gap = ui_q8(ui, 2), origin;
        struct ui_box fill, rest;
        unsigned int accent = ui_role(UI_PRIMARY), off = ui_role(UI_CONTROL_OFF), ink;
        char shown[24];
        int baseline;

        // The value: sliding after a click, else where it is; snapped to the step as the input keeps it.
        if (c->sliding)
        {
                value = ui_slider_snap(c, ui_slide_value(&c->slide, ui_kit.now, &live));
                c->value = value;
                if (!live)
                        c->sliding = 0;
        }
        value = c->value;
        ratio = ui_slider_ratio(c, value);
        stop = (b32)ui_qmul(ratio, rw - ui_q8(ui, thumb)) + ui_q8(ui, thumb) / 2;
        // The bars' scaleX: the jello while dragged past an end, the spring that lets it go, or the landing.
        if (c->moved && c->down)
        {
                // ui.js eases the stretch toward its target by 0.3 a frame at 60 Hz: the same per 1/60 s at any rate.
                b64 dt = c->last_frame ? ui_kit.now - c->last_frame : 16666667, keep = UI_Q2(70), k = UI_ONE;

                for (b64 left = dt; left >= 16666667; left -= 16666667)
                        k = ui_qmul(k, keep);
                if (dt % 16666667)
                        k = ui_qmul(k, UI_ONE - ui_qmul(UI_Q2(30), ui_progress(dt % 16666667, 16666667)));
                c->bend += ui_qmul(c->bend_target - c->bend, UI_ONE - k);
                squish = c->bend > 3221225 && !ui_kit.motion ? c->bend : 0; // > 0.003
                ui_kit.busy = 1;
        }
        else if (c->held_squish.live)
        {
                ui_kit.busy |= ui_spring_at(&c->held_squish, ui_kit.now);
                squish = c->held_squish.value[0];
        }
        else if (c->slide.landed)
                squish = ui_slide_land(&c->slide, ui_kit.now, &live) - UI_ONE;
        c->last_frame = ui_kit.now;
        ui_kit.busy |= live;
        ui_kit_press_transform(ui, c, 0, &sx, &sy, &tx, &ty);
        origin = c->squish_origin > 0 ? rx : c->squish_origin < 0 ? rx + rw : rx + rw / 2;

        // The spot under the hand.
        {
                b64 spot = ui_kit_tween(&c->spot);

                if (spot > 0)
                {
                        b32 spx = ui_q8(ui, c->x) + c->spot_x * 256, spy = ry + rh - ui_q8(ui, 24);

                        // A radial ellipse 180 by 80, the accent at its middle to nothing at 70%: rings of it, outermost first.
                        for (int ring = 7; ring >= 0; ring--)
                        {
                                b32 hw = ui_q8(ui, 90) * (ring + 1) / 8 * 7 / 10, hh = ui_q8(ui, 40) * (ring + 1) / 8 * 7 / 10;
                                struct ui_box e = {spx - hw, spy - hh, spx + hw, spy + hh, hw, hh};
                                unsigned int share = (unsigned int)((spot * 100 + UI_HALF) >> UI_Q) * 100 / 8 / 100;

                                e = ui_box_transform(e, sx, sy, ox, oy, tx, ty);
                                ui_kit_fill(ui, &e, ui_mix(accent, share ? share : 1, 0), null);
                        }
                }
        }

        fill = (struct ui_box){rx, track_y - ui_q8(ui, bar) / 2, rx + stop - gap, track_y + ui_q8(ui, bar) / 2, ui_q8(ui, bar) / 2, ui_q8(ui, bar) / 2};
        rest = (struct ui_box){rx + stop + gap, fill.y0, rx + rw, fill.y1, fill.rx, fill.ry};
        if (ratio <= 0)
                rest.x0 = rx;
        if (ratio >= UI_ONE)
                fill.x1 = rx + rw;
        fill = ui_box_transform(ui_box_transform(fill, UI_ONE + squish, UI_ONE, origin, track_y, 0, 0), sx, sy, ox, oy, tx, ty);
        rest = ui_box_transform(ui_box_transform(rest, UI_ONE + squish, UI_ONE, origin, track_y, 0, 0), sx, sy, ox, oy, tx, ty);
        if (ratio < UI_ONE)
                ui_kit_fill(ui, &rest, off, null);
        if (ratio > 0)
                ui_kit_fill(ui, &fill, accent, null);

        // The thumb's halo: the pointer on the thumb (its box, 20 hovered), a ring of 10 at 8% round it; pressed, 14 at 12%.
        {
                b32 thumb_x = rx + stop, hx = c->pointer_x * 256;
                b32 on_thumb = (c->hover || c->down) && hx >= thumb_x - ui_q8(ui, thumb) / 2 && hx < thumb_x + ui_q8(ui, thumb) / 2;
                b64 halo;

                ui_kit_number_to(&c->shadow, on_thumb ? (c->down ? UI_Q2(12) : UI_Q2(8)) : 0, 200, &ui_ease);
                ui_kit_number_to(&c->extent, on_thumb ? ui_q(c->down ? 14 : 10) : 0, 200, &ui_ease);
                halo = ui_kit_tween(&c->shadow);
                if (halo > 0)
                {
                        b32 r = ui_q8(ui, thumb) / 2 + (b32)((ui_kit_tween(&c->extent) * ui->scale) >> 22);
                        struct ui_box ring = {thumb_x - r, track_y - r, thumb_x + r, track_y + r, r, r};

                        ring = ui_box_transform(ring, sx, sy, ox, oy, tx, ty);
                        // The ring and the thumb's own background, both the accent at the halo's share.
                        ui_kit_fill(ui, &ring, ui_mix(accent, (unsigned int)((halo * 100 + UI_HALF) >> UI_Q), 0), null);
                }
        }

        // The ghost: hovered and not dragged, from the value to where a press would land, in the accent at 40%.
        if (c->hover && !c->down && c->hover_x >= 0)
        {
                b32 hx = c->hover_x * 256, left = stop < hx - rx ? rx + stop : hx, right = stop < hx - rx ? hx : rx + stop;
                b32 under = hx - rx < stop;
                unsigned int ghost = ui_mix(accent, 40, off);
                b32 thumb_x = rx + stop;

                if (c->variant & UI_SLIDER_STEPS)
                {
                        // Snapped to the steps the value takes (ui_slider_snap): the nearest, never past the last.
                        b64 span = c->step > 0 ? ui_qdiv(c->high - c->low, c->step) : UI_ONE;
                        b64 z = ui_qdiv(ui_q(hx - rx - ui_q8(ui, thumb) / 2), ui_q(rw - ui_q8(ui, thumb)));
                        b64 n;

                        z = z < 0 ? 0 : z > UI_ONE ? UI_ONE : z;
                        n = (ui_qmul(z, span) + UI_HALF) >> UI_Q;
                        n = n > span >> UI_Q ? span >> UI_Q : n;
                        z = ui_qdiv(n << UI_Q, span);
                        hx = rx + (b32)ui_qmul(z, rw - ui_q8(ui, thumb)) + ui_q8(ui, thumb) / 2;
                        left = stop < hx - rx ? rx + stop : hx;
                        right = stop < hx - rx ? hx : rx + stop;
                }
                // Over the thumb itself the ghost is the fill's colour (under) or none (past).
                if (hx > thumb_x - ui_q8(ui, 11) && hx < thumb_x + ui_q8(ui, 11))
                        ghost = under ? accent : 0;
                if (ghost && right > left)
                {
                        struct ui_box g = {left, fill.y0, right, fill.y1, 0, 0};
                        struct ui_box bar_box = under ? fill : rest;

                        g = ui_box_transform(g, sx, sy, ox, oy, tx, ty);
                        g.y0 = bar_box.y0, g.y1 = bar_box.y1;
                        ui_kit_fill(ui, &g, ghost, &bar_box);
                }
        }

        // A stepped slider's dots: a faint ink one on the rest, a paper knockout on the fill, none where the value is.
        if (c->variant & UI_SLIDER_STEPS && c->step > 0)
        {
                // The range in steps, not rounded: the steps are the whole ones it holds, each at its own place along the track.
                b64 span = ui_qdiv(c->high - c->low, c->step);
                int steps = (int)(span >> UI_Q), here = (int)((ui_qmul(ratio, span) + UI_HALF) >> UI_Q);

                for (int k = 0; k <= steps && steps > 0; k++)
                {
                        b64 along = ui_qdiv(ui_q(k), span);
                        b32 dx = rx + (b32)ui_qmul(along, rw - ui_q8(ui, thumb)) + ui_q8(ui, thumb) / 2;
                        b32 filled = along <= ratio + 107374;
                        struct ui_box dot;

                        if (line)
                                dot = (struct ui_box){dx - ui_q8(ui, 1), track_y + ui_q8(ui, 6) - ui_q8(ui, k == 0 || k == steps || 2 * k == steps ? 6 : 4),
                                                      dx + ui_q8(ui, 1), track_y + ui_q8(ui, 6) + ui_q8(ui, k == 0 || k == steps || 2 * k == steps ? 6 : 4),
                                                      ui_q8(ui, 1), ui_q8(ui, 1)};
                        else
                                dot = (struct ui_box){dx - ui_q8(ui, 3), track_y - ui_q8(ui, 3), dx + ui_q8(ui, 3), track_y + ui_q8(ui, 3), ui_q8(ui, 3), ui_q8(ui, 3)};
                        if (!line && k == here && k != 0 && k != steps)
                                continue;
                        dot = ui_box_transform(dot, sx, sy, ox, oy, tx, ty);
                        ui_kit_fill(ui, &dot, filled && !line ? ui_mix(0xffffffffu, 90, 0) : ui_mix(ui_role(UI_ON_SURFACE), 22, 0), null);
                }
        }

        // The label and the value, which ride the squish too.
        ink = c->hover || c->down ? accent : ui_role(UI_ON_SURFACE);
        baseline = ui_kit_baseline(ui, (ry + ui_q8(ui, 8) + ty) / 256, UI_BODY_LARGE);
        {
                b32 lx = origin + (b32)ui_qmul(rx - origin, UI_ONE + squish);

                ui_kit_text(ui, (ox + tx + (b32)ui_qmul(lx - ox, sx)) / 256, baseline, (string_address)c->label, UI_BODY_LARGE, ink,
                            c->hover || c->down ? 64 : 0);
        }
        {
                b64 whole = (value + UI_HALF) >> UI_Q;
                positive length = 0;
                int width;
                b32 vx;

                // A negative value is its sign and then its digits (the digits are unsigned, so the sign is put here).
                if (whole < 0)
                        shown[length++] = '-', whole = -whole;
                length += positive_into_string((p8 address_to)shown + length, (positive)whole);
                shown[length] = 0;
                width = ui_kit_measure(ui, (string_address)shown, UI_BODY_LARGE);
                vx = origin + (b32)ui_qmul(rx + rw - width * 256 - origin, UI_ONE + squish);
                ui_kit_text(ui, (ox + tx + (b32)ui_qmul(vx - ox, sx)) / 256, baseline, (string_address)shown, UI_BODY_LARGE,
                            c->hover || c->down ? accent : ui_role(UI_ON_SURFACE_VARIANT), c->hover || c->down ? 64 : 0);
        }
}

/*
        Tabs, a strip of buttons: segmented, pills 32 tall 4 apart with the
        accent pip behind the chosen (on-primary on it); underlined, 40 tall 2
        apart over a hairline with a 2 tall pip under the chosen, 8 in from its
        sides. The pip slides over .35s --ease-emphasized, and the tabs it rolls
        over stretch 1.13 along the travel (130 ms of flight); colours go in .2s.
*/
static int ui_tab_width(struct ui address_to ui, const struct ui_control address_to c, int item)
{
        return ui_kit_measure(ui, (string_address)c->items[item], UI_LABEL_LARGE) / ui->scale + 28;
}

static int ui_tab_left(struct ui address_to ui, const struct ui_control address_to c, int item)
{
        int x = c->x;

        for (int k = 0; k < item; k++)
                x += ui_tab_width(ui, c, k) + (c->variant == UI_TABS_SEGMENTED ? 4 : 2);
        return x;
}

static fn ui_vtabs_draw(struct ui address_to ui, struct ui_control address_to c);

static fn ui_tabs_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 seg = c->variant == UI_TABS_SEGMENTED, live = 0;

        if (c->variant == UI_TABS_VERTICAL)
        {
                ui_vtabs_draw(ui, c);
                return;
        }
        // A strip with no items has no pip to put on one, and no word to measure for its place.
        if (c->count == 0)
                return;
        int height = seg ? 32 : 40, chosen = c->at;
        b64 pip_x, pip_w;

        if (!seg)
        {
                struct ui_box hair = {ui_q8(ui, c->x), ui_q8(ui, c->y + height), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + height + 1), 0, 0};

                ui_kit_fill(ui, &hair, ui_role(UI_OUTLINE_VARIANT), null);
        }
        // The pip's place is the strip's own (from its left), so a strip the layout moves takes its pip with it at once.
        ui_kit_number_to(&c->pip_x, ui_q(ui_tab_left(ui, c, chosen) - c->x + (seg ? 0 : 8)), 350, &ui_ease);
        ui_kit_number_to(&c->pip_w, ui_q(ui_tab_width(ui, c, chosen) - (seg ? 0 : 16)), 350, &ui_ease);
        pip_x = ui_q(c->x) + ui_kit_tween(&c->pip_x);
        pip_w = ui_kit_tween(&c->pip_w);
        {
                struct ui_box pip = seg ? ui_box_of(ui, pip_x, ui_q(c->y), pip_w, ui_q(height), ui_q(height) / 2)
                                        : ui_box_of(ui, pip_x, ui_q(c->y + height - 1), pip_w, ui_q(2), ui_q(2));

                ui_kit_fill(ui, &pip, ui_role(UI_PRIMARY), null);
        }
        for (int item = 0; item < c->count; item++)
        {
                int x = ui_tab_left(ui, c, item), w = ui_tab_width(ui, c, item), width, baseline;
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live);
                b32 bx = ui_q8(ui, x), bw = ui_q8(ui, w), anchor = c->dom_origin > 0 ? bx : bx + bw;
                struct ui_box face = ui_box_of(ui, ui_q(x), ui_q(c->y), ui_q(w), ui_q(height), ui_q(height) / 2);
                unsigned int ink, layer;

                ui_kit_colour_to(&c->item_ink[item], item == chosen ? ui_role(seg ? UI_ON_PRIMARY : UI_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT),
                                 200, &ui_ease);
                ink = ui_kit_colour(&c->item_ink[item]);
                face = ui_box_transform(face, dom, UI_ONE, anchor, 0, 0, 0);
                // The state layer and the ripple: a tab is a button.
                if (c->item == item)
                {
                        ui_kit_layer_to(c, 0);
                        layer = ui_kit_layer(c);
                        ui_kit_layer_fill(ui, &face, ink, layer);
                        {
                                struct ui_place place = {dom, UI_ONE, anchor, 0, 0, 0};
                                struct ui_box pill = ui_box_of(ui, pip_x, ui_q(c->y), pip_w, ui_q(height), ui_q(height) / 2);

                                ui_kit_ripples_on(ui, c, bx - ui_q8(ui, 8), ui_q8(ui, c->y - 8), &place, &face, ink, seg ? &pill : null);
                        }
                }
                width = ui_kit_measure(ui, (string_address)c->items[item], UI_LABEL_LARGE);
                {
                        b32 middle = bx + bw / 2;

                        middle = anchor + (b32)ui_qmul(middle - anchor, dom);
                        baseline = ui_kit_baseline(ui, ui_dev(ui, c->y) + (ui_dev(ui, height) - ui_dev(ui, 20)) / 2, UI_LABEL_LARGE);
                        ui_kit_text(ui, middle / 256 - width / 2, baseline, (string_address)c->items[item], UI_LABEL_LARGE, ink, 0);
                }
        }
        ui_kit.busy |= live;
}

/*
        Tabs stood on end (UI_TABS_VERTICAL): a column of items h tall and w wide,
        their text at the left 12 in, the accent pip behind the chosen one sliding
        up and down to it over .35s --ease-emphasized, the items it rolls over
        stretched 1.13 along y as it passes (130 ms of flight, anchored at their
        trailing edge), colours .2s, the hovered one's state layer and a press's
        ripple: the horizontal strip's every number, on the other axis.
*/
static fn ui_vtabs_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 busy = ui_kit.busy;
        b64 pip_y;

        // What moves is said item by item: the column is count items, and its box (c->h) is one of them. The pip's
        // way is from where it set out to where it goes, which holds where it was drawn the frame before.
        ui_kit.reached = 1;
        ui_kit.busy = 0;
        ui_kit_number_to(&c->pip_y, ui_q(c->at * c->h), 350, &ui_ease);
        pip_y = ui_q(c->y) + ui_kit_tween(&c->pip_y);
        if (ui_kit.busy)
        {
                b64 from = c->pip_y.from < c->pip_y.to ? c->pip_y.from : c->pip_y.to, to = c->pip_y.from < c->pip_y.to ? c->pip_y.to : c->pip_y.from;

                ui_kit_reach(c->y + (int)(from >> UI_Q), c->y + (int)((to + UI_ONE - 1) >> UI_Q) + c->h);
        }
        busy |= ui_kit.busy;
        {
                struct ui_box pip = ui_box_of(ui, ui_q(c->x), pip_y, ui_q(c->w), ui_q(c->h), ui_q(c->h) / 2 < ui_q(8) ? ui_q(c->h) / 2 : ui_q(8));

                if (!c->disabled && c->count > 0) // no items, no chosen one to show
                        ui_kit_fill(ui, &pip, ui_role(UI_PRIMARY), null);
                // The keys in the column: the kit's focus ring, 2 out from the chosen item.
                if (c->focused && !c->disabled)
                {
                        struct ui_box ring = pip;

                        ring.x0 -= ui_q8(ui, 2), ring.y0 -= ui_q8(ui, 2), ring.x1 += ui_q8(ui, 2), ring.y1 += ui_q8(ui, 2);
                        ring.rx += ui_q8(ui, 2), ring.ry += ui_q8(ui, 2);
                        ui_kit_outline(ui, &ring, ui_q8(ui, 2), ui_role(UI_PRIMARY));
                }
        }
        for (int item = 0; item < c->count; item++)
        {
                int y = c->y + item * c->h;
                b32 live = 0;
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live);
                b32 by = ui_q8(ui, y), bh = ui_q8(ui, c->h), anchor = c->dom_origin > 0 ? by : by + bh;
                struct ui_box face = ui_box_of(ui, ui_q(c->x), ui_q(y), ui_q(c->w), ui_q(c->h), ui_q(c->h) / 2 < ui_q(8) ? ui_q(c->h) / 2 : ui_q(8));
                unsigned int ink;
                b32 middle;

                busy |= live;
                ui_kit.busy = live;
                if (y + c->h + 8 < ui->top / ui->scale || y - 8 > ui->bottom / ui->scale)
                        continue;
                ui_kit_colour_to(&c->item_ink[item], item == c->at && !c->disabled ? ui_role(UI_ON_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT), 200, &ui_ease);
                ink = ui_kit_colour(&c->item_ink[item]);
                face = ui_box_transform(face, UI_ONE, dom, 0, anchor, 0, 0);
                if (c->item == item)
                {
                        struct ui_place place = {UI_ONE, dom, 0, anchor, 0, 0};
                        unsigned int layer;

                        ui_kit_layer_to(c, 0);
                        layer = ui_kit_layer(c);
                        ui_kit_layer_fill(ui, &face, ink, layer);
                        {
                                b64 radius = ui_q(c->h) / 2 < ui_q(8) ? ui_q(c->h) / 2 : ui_q(8);
                                struct ui_box pill = ui_box_of(ui, ui_q(c->x), pip_y, ui_q(c->w), ui_q(c->h), radius);

                                ui_kit_ripples_on(ui, c, ui_q8(ui, c->x - 8), by - ui_q8(ui, 8), &place, &face, ink, &pill);
                        }
                }
                // The text at whole pixels where the stretch takes the item's middle.
                middle = anchor + (b32)ui_qmul(by + bh / 2 - anchor, dom);
                ui_kit_text(ui, ui_dev(ui, c->x + 12), ui_kit_baseline(ui, middle / 256 - ui_dev(ui, 10), UI_LABEL_LARGE),
                            (string_address)c->items[item], item == c->at ? UI_LABEL_LARGE : UI_BODY_MEDIUM, ink, 0);
                // Its stretch, its colour, its state layer or its ripple moving: the item's rows.
                if (ui_kit.busy)
                        ui_kit_reach(y, y + c->h);
                busy |= ui_kit.busy;
        }
        ui_kit.busy = busy;
}

// The vertical cascade: each item whose middle the pip passes stretches as it does (130 ms of flight).
static fn ui_vtabs_choose(struct ui_control address_to c, int item)
{
        int from = c->at, distance = item > from ? item - from : from - item;

        if (item == from || item < 0 || item >= c->count)
                return;
        c->dom_origin = item > from ? 1 : -1;
        if (!ui_kit.motion)
                for (int k = 0; k < c->count; k++)
                {
                        int along = item > from ? k - from : from - k;

                        if (along < 0 || along > distance)
                                continue;
                        ui_stretch_go(&c->item_dom[k], ui_kit.now + (b64)along * 130 * UI_MS / distance, UI_Q2(113));
                }
        c->at = item;
}

// The tabs' cascade: each tab whose middle the pip rolls over stretches as it passes (130 ms of flight).
static fn ui_tabs_choose(struct ui address_to ui, struct ui_control address_to c, int item)
{
        if (c->variant == UI_TABS_VERTICAL)
        {
                (void)ui;
                ui_vtabs_choose(c, item);
                return;
        }
        b64 from, to, length;

        if (item == c->at)
                return;
        from = ui_q(ui_tab_left(ui, c, c->at)) + ui_q(ui_tab_width(ui, c, c->at)) / 2;
        to = ui_q(ui_tab_left(ui, c, item)) + ui_q(ui_tab_width(ui, c, item)) / 2;
        length = to > from ? to - from : from - to;
        c->dom_origin = to >= from ? 1 : -1;
        if (!ui_kit.motion && length >= UI_ONE)
                for (int k = 0; k < c->count; k++)
                {
                        b64 middle = ui_q(ui_tab_left(ui, c, k)) + ui_q(ui_tab_width(ui, c, k)) / 2;
                        b64 along = to >= from ? middle - from : from - middle;

                        if (along < -ui_q(6) || along > length + ui_q(6))
                                continue;
                        along = along < 0 ? 0 : along > length ? length : along;
                        ui_stretch_go(&c->item_dom[k], ui_kit.now + ui_qmul(ui_qdiv(along, length), 130 * UI_MS), UI_Q2(113));
                }
        c->at = item;
}

/*
        A field, raised: the label above it (body-large, the ink), 6 under it
        a well 48 tall of the surface's high face, 6 round, 14 in, its text 15
        px, with four shadows -- the light caught on its top edge, a hairline,
        and two soft ones under it -- that lift on hover and turn to the bright
        accent's glow on focus, with a 4% wash of it in the face (.18s --ease).
*/
#define UI_FIELD_BARE 1 // a field's variant: no label above it, the well at the control's top and its own height
#define UI_FIELD_REST 0
#define UI_FIELD_HOVER 1
#define UI_FIELD_FOCUS 2

static fn ui_field_shadows(int state, b64 address_to into)
{
        // x, y, blur, spread (logical pixels, Q30) and the colour, for the three after the inset light.
        static const int numbers[3][3][4] = {
            {{0, 0, 0, 1}, {0, 1, 1, 0}, {0, 4, 10, -4}},
            {{0, 0, 0, 1}, {0, 2, 4, -1}, {0, 8, 18, -5}},
            {{0, 0, 11, 0}, {0, 0, 4, 0}, {0, 4, 10, -5}},
        };
        unsigned int shadow = ui_role(UI_SHADOW), bright = ui_role(UI_ACCENT_BRIGHT);
        unsigned int colours[3][3] = {
            {ui_mix(shadow, 5, 0), ui_mix(shadow, 4, 0), ui_mix(shadow, 11, 0)},
            {ui_mix(shadow, 7, 0), ui_mix(shadow, 6, 0), ui_mix(shadow, 13, 0)},
            {ui_mix(bright, 26, 0), ui_mix(bright, 18, 0), ui_mix(shadow, 7, 0)},
        };

        for (int k = 0; k < 3; k++)
        {
                for (int n = 0; n < 4; n++)
                        into[k * 5 + n] = ui_q(numbers[state][k][n]);
                into[k * 5 + 4] = ui_tween_colour(colours[state][k]);
        }
}

/*
        One row of a field's top edge: the light the face makes a pixel below its own top, inside it. Both rows hold first
        differences (ui_line_add), so their running sums are the covers, the face's less the low's; where that is positive
        the highlight is laid over the picture at its column, for the columns lo to hi that are on the picture, and every
        column of both rows is cleared. On x86-64 and arm64 this is ui_edge_row_body: the running sums in 16-bit lanes eight
        columns a step, the zero-cover columns skipped with one test, and a column the light reaches blended by e->put (the
        ui_edge_put below, the C it replaced). The C in ui_edge_row_c is the body on riscv64, and the reference the check in
        test/checks.c compares the body with.
*/
struct ui_edge_row
{
        unsigned short address_to face, address_to low;
        unsigned int address_to at; // the picture's pixel at column lo
        b32 width, lo, hi;
        unsigned int highlight;
        void (*put)(unsigned int address_to at, unsigned int highlight, unsigned int cover); // ui_edge_put, called through the struct
};

// The blend of one column the light reaches: the rare case, which the body calls through e->put.
static __attribute__((noinline)) void ui_edge_put(unsigned int address_to at, unsigned int highlight, unsigned int cover)
{
        *at = ui_over(ui_premultiplied(highlight, cover), *at);
}

#if (X64 || ARM64) && !defined(UI_EDGE_C)
#define UI_EDGE_ASM 1
extern void ui_edge_row_body(struct ui_edge_row address_to e) __asm__("ui_edge_row_body") __attribute__((visibility("hidden")));
#endif

#if UI_EDGE_ASM && X64
asm(".pushsection .text\n"
    ".balign 16\n"
    ".local ui_edge_row_body\n"
    ".type ui_edge_row_body, " ASM_TYPE "\n"
    "ui_edge_row_body:\n" ASM_ENDBR
    /*
            rdi the struct (its layout is struct ui_edge_row's: face 0, low 8, at 16, width 24, lo 28, hi 32, highlight 36, put 40).
            rbx the struct, r12 and r13 the face and low rows, r14 the picture's pixel at lo, r15d the width, ebp the column.
            xmm0 and xmm1 carry each row's running sum in every 16-bit lane into the next eight columns; xmm7 is zero.
            A block of eight whose light reaches nothing is cleared and skipped; one that does is walked column by column
            from its saved sums (the calls clobber the vector registers, so the carries are saved round them).
    */
    "push %rbx\n   push %rbp\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n   sub $72, %rsp\n"
    "mov %rdi, %rbx\n"
    "mov (%rbx), %r12\n   mov 8(%rbx), %r13\n   mov 16(%rbx), %r14\n   mov 24(%rbx), %r15d\n"
    "xor %ebp, %ebp\n"
    "pxor %xmm0, %xmm0\n   pxor %xmm1, %xmm1\n   pxor %xmm7, %xmm7\n"
    ".Ler_vec:\n"
    "lea 8(%rbp), %eax\n   cmp %r15d, %eax\n   jg .Ler_tail\n"
    "movdqu (%r12,%rbp,2), %xmm2\n   movdqu (%r13,%rbp,2), %xmm3\n"
    "movdqa %xmm2, %xmm4\n   pslldq $2, %xmm4\n   paddw %xmm4, %xmm2\n"
    "movdqa %xmm2, %xmm4\n   pslldq $4, %xmm4\n   paddw %xmm4, %xmm2\n"
    "movdqa %xmm2, %xmm4\n   pslldq $8, %xmm4\n   paddw %xmm4, %xmm2\n"
    "paddw %xmm0, %xmm2\n"
    "pshufhw $0xff, %xmm2, %xmm0\n   pshufd $0xff, %xmm0, %xmm0\n"
    "movdqa %xmm3, %xmm4\n   pslldq $2, %xmm4\n   paddw %xmm4, %xmm3\n"
    "movdqa %xmm3, %xmm4\n   pslldq $4, %xmm4\n   paddw %xmm4, %xmm3\n"
    "movdqa %xmm3, %xmm4\n   pslldq $8, %xmm4\n   paddw %xmm4, %xmm3\n"
    "paddw %xmm1, %xmm3\n"
    "pshufhw $0xff, %xmm3, %xmm1\n   pshufd $0xff, %xmm1, %xmm1\n"
    // A lane is a cover where the face's sum is above the low's: the saturating difference is not zero there.
    "movdqa %xmm2, %xmm4\n   psubusw %xmm3, %xmm4\n   pcmpeqw %xmm7, %xmm4\n   pmovmskb %xmm4, %eax\n"
    "cmp $0xffff, %eax\n   je .Ler_zero\n"
    ".Ler_hit:\n"
    "movdqu %xmm2, (%rsp)\n   movdqu %xmm3, 16(%rsp)\n   movdqu %xmm0, 32(%rsp)\n   movdqu %xmm1, 48(%rsp)\n"
    "movl $0, 64(%rsp)\n"
    ".Ler_k:\n"
    "movl 64(%rsp), %eax\n   cmp $8, %eax\n   jge .Ler_k_done\n"
    "movzwl (%rsp,%rax,2), %ecx\n   movzwl 16(%rsp,%rax,2), %edx\n   sub %edx, %ecx\n"
    "jle .Ler_k_next\n"
    "lea (%rbp,%rax), %r8\n"
    "cmp 28(%rbx), %r8d\n   jl .Ler_k_next\n   cmp 32(%rbx), %r8d\n   jge .Ler_k_next\n"
    "mov %r8d, %esi\n   sub 28(%rbx), %esi\n   movslq %esi, %rsi\n   lea (%r14,%rsi,4), %rdi\n"
    "mov 36(%rbx), %esi\n   mov %ecx, %edx\n"
    "call *40(%rbx)\n"
    ".Ler_k_next:\n"
    "incl 64(%rsp)\n   jmp .Ler_k\n"
    ".Ler_k_done:\n"
    "movdqu 32(%rsp), %xmm0\n   movdqu 48(%rsp), %xmm1\n   pxor %xmm7, %xmm7\n"
    ".Ler_zero:\n"
    "movdqu %xmm7, (%r12,%rbp,2)\n   movdqu %xmm7, (%r13,%rbp,2)\n"
    "add $8, %ebp\n   jmp .Ler_vec\n"
    // The last columns, fewer than eight: the same running sums in scalar, from the carries' low lanes.
    ".Ler_tail:\n"
    "movd %xmm0, %r8d\n   movzwl %r8w, %r8d\n   movd %xmm1, %r9d\n   movzwl %r9w, %r9d\n"
    ".Ler_t:\n"
    "cmp %r15d, %ebp\n   jge .Ler_done\n"
    "movzwl (%r12,%rbp,2), %eax\n   add %eax, %r8d\n   movzwl %r8w, %r8d\n"
    "movzwl (%r13,%rbp,2), %ecx\n   add %ecx, %r9d\n   movzwl %r9w, %r9d\n"
    "movw $0, (%r12,%rbp,2)\n   movw $0, (%r13,%rbp,2)\n"
    "mov %r8d, %eax\n   sub %r9d, %eax\n"
    "jle .Ler_t_next\n"
    "cmp 28(%rbx), %ebp\n   jl .Ler_t_next\n   cmp 32(%rbx), %ebp\n   jge .Ler_t_next\n"
    "mov %r8, (%rsp)\n   mov %r9, 8(%rsp)\n"
    "mov %eax, %edx\n"
    "mov %ebp, %esi\n   sub 28(%rbx), %esi\n   movslq %esi, %rsi\n   lea (%r14,%rsi,4), %rdi\n"
    "mov 36(%rbx), %esi\n"
    "call *40(%rbx)\n"
    "mov (%rsp), %r8\n   mov 8(%rsp), %r9\n"
    ".Ler_t_next:\n"
    "inc %ebp\n   jmp .Ler_t\n"
    ".Ler_done:\n"
    "add $72, %rsp\n   pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbp\n   pop %rbx\n"
    ASM_RET
    ".size ui_edge_row_body, .-ui_edge_row_body\n"
    ".popsection\n");
#elif UI_EDGE_ASM && ARM64
/*
        ui_edge_row_body on arm64: x0 the struct, x19 it, x20 and x21 the face and low rows, x22 the picture's pixel at lo,
        w23 the width, x24 the column; x26 and x27 the scalar sums of the last columns. v0 and v1 carry each row's running sum
        in every 16-bit lane, v7 is zero. The running sum of eight is three ext-and-adds (the zero enters at the low lane), then
        the carry; a block whose light reaches nothing (uqsub of the two sums, none above zero) is cleared and passed. A block
        that reaches is walked column by column from its saved sums, and a column the light reaches calls e->put.
*/
asm(".pushsection .text\n"
    ".balign 16\n"
    ".local ui_edge_row_body\n"
    ".type ui_edge_row_body, " ASM_TYPE "\n"
    "ui_edge_row_body:\n" ASM_ENDBR
    "stp x29, x30, [sp, #-160]!\n   mov x29, sp\n"
    "stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n   stp x23, x24, [sp, #48]\n   stp x25, x26, [sp, #64]\n"
    "stp x27, x28, [sp, #144]\n"
    "mov x19, x0\n"
    "ldr x20, [x19]\n   ldr x21, [x19, #8]\n   ldr x22, [x19, #16]\n   ldr w23, [x19, #24]\n"
    "movi v7.2d, #0\n   movi v0.2d, #0\n   movi v1.2d, #0\n"
    "mov x24, #0\n"
    ".Lev_vec:\n"
    "add w9, w24, #8\n   cmp w9, w23\n   b.gt .Lev_tail\n"
    "add x9, x20, x24, lsl #1\n   ld1 {v2.8h}, [x9]\n"
    "add x10, x21, x24, lsl #1\n   ld1 {v3.8h}, [x10]\n"
    "ext v4.16b, v7.16b, v2.16b, #14\n   add v2.8h, v2.8h, v4.8h\n"
    "ext v4.16b, v7.16b, v2.16b, #12\n   add v2.8h, v2.8h, v4.8h\n"
    "ext v4.16b, v7.16b, v2.16b, #8\n    add v2.8h, v2.8h, v4.8h\n"
    "add v2.8h, v2.8h, v0.8h\n   dup v0.8h, v2.h[7]\n"
    "ext v4.16b, v7.16b, v3.16b, #14\n   add v3.8h, v3.8h, v4.8h\n"
    "ext v4.16b, v7.16b, v3.16b, #12\n   add v3.8h, v3.8h, v4.8h\n"
    "ext v4.16b, v7.16b, v3.16b, #8\n    add v3.8h, v3.8h, v4.8h\n"
    "add v3.8h, v3.8h, v1.8h\n   dup v1.8h, v3.h[7]\n"
    "uqsub v4.8h, v2.8h, v3.8h\n   umaxv h5, v4.8h\n   umov w11, v5.h[0]\n"
    "cbz w11, .Lev_zero\n"
    "add x12, sp, #80\n   st1 {v2.8h}, [x12]\n"
    "add x12, sp, #96\n   st1 {v3.8h}, [x12]\n"
    "add x12, sp, #112\n  st1 {v0.8h}, [x12]\n"
    "add x12, sp, #128\n  st1 {v1.8h}, [x12]\n"
    "mov x25, #0\n"
    ".Lev_k:\n"
    "cmp x25, #8\n   b.ge .Lev_k_done\n"
    "add x12, sp, #80\n   ldrh w13, [x12, x25, lsl #1]\n"
    "add x14, sp, #96\n   ldrh w15, [x14, x25, lsl #1]\n"
    "subs w13, w13, w15\n   b.le .Lev_k_next\n"
    "add w16, w24, w25\n"
    "ldr w17, [x19, #28]\n   cmp w16, w17\n   b.lt .Lev_k_next\n"
    "ldr w17, [x19, #32]\n   cmp w16, w17\n   b.ge .Lev_k_next\n"
    "ldr w17, [x19, #28]\n   sub w17, w16, w17\n   add x0, x22, w17, sxtw #2\n"
    "ldr w1, [x19, #36]\n   mov w2, w13\n   ldr x9, [x19, #40]\n   blr x9\n"
    ".Lev_k_next:\n"
    "add x25, x25, #1\n   b .Lev_k\n"
    ".Lev_k_done:\n"
    "ldr q0, [sp, #112]\n   ldr q1, [sp, #128]\n   movi v7.2d, #0\n"
    ".Lev_zero:\n"
    "add x9, x20, x24, lsl #1\n   st1 {v7.8h}, [x9]\n"
    "add x10, x21, x24, lsl #1\n  st1 {v7.8h}, [x10]\n"
    "add w24, w24, #8\n   b .Lev_vec\n"
    ".Lev_tail:\n"
    "umov w26, v0.h[0]\n   umov w27, v1.h[0]\n"
    ".Lev_t:\n"
    "cmp w24, w23\n   b.ge .Lev_done\n"
    "add x9, x20, x24, lsl #1\n   ldrh w10, [x9]\n   add w26, w26, w10\n   and w26, w26, #0xffff\n   strh wzr, [x9]\n"
    "add x9, x21, x24, lsl #1\n   ldrh w10, [x9]\n   add w27, w27, w10\n   and w27, w27, #0xffff\n   strh wzr, [x9]\n"
    "subs w11, w26, w27\n   b.le .Lev_t_next\n"
    "ldr w12, [x19, #28]\n   cmp w24, w12\n   b.lt .Lev_t_next\n"
    "ldr w13, [x19, #32]\n   cmp w24, w13\n   b.ge .Lev_t_next\n"
    "sub w13, w24, w12\n   add x0, x22, w13, sxtw #2\n"
    "ldr w1, [x19, #36]\n   mov w2, w11\n   ldr x9, [x19, #40]\n   blr x9\n"
    ".Lev_t_next:\n"
    "add x24, x24, #1\n   b .Lev_t\n"
    ".Lev_done:\n"
    "ldp x27, x28, [sp, #144]\n   ldp x25, x26, [sp, #64]\n   ldp x23, x24, [sp, #48]\n"
    "ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #160\n"
    ASM_RET
    ".size ui_edge_row_body, .-ui_edge_row_body\n"
    ".popsection\n");
#endif

#ifndef UI_EDGE_ASM
static void ui_edge_row_c(struct ui_edge_row address_to e)
{
        for (b32 x = 0, face_run = 0, low_run = 0; x < e->width; x++)
        {
                int cover;

                face_run = (unsigned short)(face_run + e->face[x]);
                low_run = (unsigned short)(low_run + e->low[x]);
                cover = (int)face_run - (int)low_run;
                e->face[x] = e->low[x] = 0;
                if (cover > 0 && x >= e->lo && x < e->hi)
                        e->put(e->at + (x - e->lo), e->highlight, (unsigned int)cover);
        }
}
#endif

static inline void ui_edge_row(struct ui_edge_row address_to e)
{
#ifdef UI_EDGE_ASM
        ui_edge_row_body(e);
#else
        ui_edge_row_c(e);
#endif
}

struct ui_text;
static fn ui_text_field_draw(struct ui address_to ui, struct ui_control address_to c, int left, int right, int top, int bottom);

static fn ui_input_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int state = c->focused ? UI_FIELD_FOCUS : c->hover ? UI_FIELD_HOVER : UI_FIELD_REST;
        b64 want[15], now[15], p;
        b32 bare = c->variant == UI_FIELD_BARE;
        int above = bare ? 0 : 24 + 6, tall = bare && c->h ? c->h : 48;
        b32 fy = ui_dev(ui, c->y) + ui_dev(ui, above);
        struct ui_box face = ui_box_of(ui, ui_q(c->x), ui_q(c->y + above), ui_q(c->w), ui_q(tall), ui_q(6));
        unsigned int fill;
        int baseline;

        // The label above (a bare field has none).
        if (!bare && c->label)
                ui_control_label(ui, c, 0, 0, UI_BODY_LARGE, ui_role(UI_ON_SURFACE));
        // The shadows move as one transition (.18s --ease), from wherever they are when the state changes.
        ui_field_shadows(state, want);
        p = c->field_start ? ui_bezier_at(&ui_ease, ui_progress(ui_kit.now - c->field_start, 180 * UI_MS)) : UI_ONE;
        for (int k = 0; k < 15; k++)
                now[k] = ui_tween_mix(k % 5 == 4, ((b64 address_to)c->shadow_from)[k], ((b64 address_to)c->shadow_to)[k], p);
        if (!c->shadow_to[0][4] || state != c->field_state)
        {
                for (int k = 0; k < 15; k++)
                {
                        ((b64 address_to)c->shadow_from)[k] = c->shadow_to[0][4] ? now[k] : want[k];
                        ((b64 address_to)c->shadow_to)[k] = want[k];
                }
                c->field_start = c->shadow_to[0][4] && !ui_kit.motion ? ui_kit.now : 0;
                c->field_state = state;
                for (int k = 0; k < 15; k++)
                        now[k] = ((b64 address_to)c->shadow_from)[k];
        }
        else if (c->field_start && ui_kit.now - c->field_start >= 180 * UI_MS)
                c->field_start = 0;
        if (c->field_start)
                ui_kit.busy = 1;
        // A glass field is flat: no shadow and no ring round it, so nothing is drawn past its edge.
        for (int k = 2; k >= 0 && !ui->glass; k--)
        {
                b32 x = (b32)((now[k * 5] * ui->scale) >> 22), y = (b32)((now[k * 5 + 1] * ui->scale) >> 22);
                b32 blur = (b32)((now[k * 5 + 2] * ui->scale) >> 22), spread = (b32)((now[k * 5 + 3] * ui->scale) >> 22);
                unsigned int colour = ui_tween_argb(now[k * 5 + 4]);

                if (blur < 32 && spread > 0)
                {
                        // A hard ring: the face grown by spread, less the face.
                        struct ui_box ring = {face.x0 - spread + x, face.y0 - spread + y, face.x1 + spread + x, face.y1 + spread + y,
                                              face.rx + spread, face.ry + spread};

                        ui_kit_outline(ui, &ring, spread, colour);
                }
                else
                        ui_kit_shadow(ui, &face, x, y, blur, spread, colour);
        }
        ui_kit_colour_to(&c->fill, !ui->glass ? (c->focused ? ui_mix(ui_role(UI_ACCENT_BRIGHT), 4, ui_role(UI_SURFACE_HIGH)) : ui_role(UI_SURFACE_HIGH))
                                             : ui_well(ui, UI_SURFACE_HIGH, c->focused ? 24 : 16), 180, &ui_ease);
        fill = ui_kit_colour(&c->fill);
        ui_kit_fill(ui, &face, fill, null);
        // The light on its top edge: the face less itself a pixel down, inside it (none on a glass field).
        if (!ui->glass)
        {
                struct ui_box low = face;

                low.y0 += ui_q8(ui, 1);
                low.y1 += ui_q8(ui, 1);
                unsigned int highlight = ui_mix(ui_role(UI_HIGHLIGHT), 90, 0);
                for (b32 y = face.y0 >> 8; y < (face.y0 >> 8) + ui_dev(ui, 6); y++)
                {
                        b32 width = ui_dev(ui, c->w), first = face.x0 >> 8;

                        if (y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y) || width > UI_KIT_LINE)
                                continue;
                        ui_box_row(&face, y, ui_kit_line, first, width);
                        ui_box_row(&low, y, ui_kit_clip, first, width);
                        {
                                // The columns on the picture are first + x for x from lo to hi; none when the row is off it.
                                struct ui_edge_row edge = {ui_kit_line, ui_kit_clip, null, width, 0, 0, highlight, ui_edge_put};
                                b32 lo = first < 0 ? -first : 0, hi = ui->width - first < width ? ui->width - first : width;

                                if (lo < hi)
                                {
                                        edge.lo = lo, edge.hi = hi;
                                        edge.at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)(first + lo);
                                }
                                ui_edge_row(&edge);
                        }
                }
        }
        baseline = ui_kit_baseline(ui, fy + (ui_dev(ui, tall) - ui_dev(ui, 20)) / 2, UI_FIELD);
        // What is in the well is the engine's: the text (dots when masked), its selection, its caret and its placeholder.
        // A field with no engine is one nothing can be typed in, and shows its placeholder and no caret.
        if (c->edit)
                ui_text_field_draw(ui, c, ui_dev(ui, c->x + 14), ui_dev(ui, c->x + c->w - 14), fy, fy + ui_dev(ui, tall));
        else if (c->items[0])
                ui_run_in(ui, (const font_face address_to const[]){ui->regular, ui->regular}, ui_dev(ui, c->x + 14), baseline,
                          (string_address)c->items[0], 15, ui_text_colour(ui, UI_ON_SURFACE) & 0xffffffu, 92, ui_dev(ui, c->x + 14),
                          ui_dev(ui, c->x + c->w - 14), 0);
}

/*
        Progress: a 6 tall pill track in the surface's highest, the accent fill
        to its value (width over .45s --ease-emphasized), or, indeterminate, a
        34% pill sweeping from -34% to 100% every 1.5 s on the same ease, cut to
        the track.
*/
static fn ui_progress_draw(struct ui address_to ui, struct ui_control address_to c)
{
        struct ui_box track = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(6), ui_q(3)), fill = track;

        ui_kit_fill(ui, &track, ui_role(UI_SURFACE_HIGHEST), null);
        if (c->value < 0)
        {
                b64 period = 1500 * UI_MS, at = ui_kit.motion ? 0 : ui_kit.now % period;
                b64 left = -UI_Q2(34) + ui_qmul(UI_Q2(134), ui_bezier_at(&ui_ease, ui_progress(at, period)));

                fill = ui_box_of(ui, ui_q(c->x) + ui_qmul(left, ui_q(c->w)), ui_q(c->y), ui_qmul(UI_Q2(34), ui_q(c->w)), ui_q(6), ui_q(3));
                ui_kit_fill(ui, &fill, ui_role(UI_PRIMARY), &track);
                if (!ui_kit.motion)
                        ui_kit.busy = 1;
                return;
        }
        ui_kit_number_to(&c->extent, ui_progress_whole(c->value), 450, &ui_ease);
        fill.x1 = fill.x0 + (b32)ui_qmul(ui_kit_tween(&c->extent), track.x1 - track.x0);
        if (fill.x1 > fill.x0)
                ui_kit_fill(ui, &fill, ui_role(UI_PRIMARY), null);
}

/*
        The kit's still pieces: a badge (22 tall, 9 in, 6 round, label-medium,
        a role's container and ink, a dot of the ink), a tag (26 tall, mono, a
        hairline), an avatar (32 round, initials in the bold, a status dot),
        a callout (14 round, a tinted box, a strong first line), a code well
        (14 round, a header strip, mono 13 lines), a key (20 tall, 2 at its
        foot), a spinner (a ring and a quarter of it in the accent, a turn in
        .7 s), a card and a window (hairline, 14 and 20 round).
*/
static fn ui_badge_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int role = c->role, width = ui_kit_measure(ui, (string_address)c->label, UI_LABEL_MEDIUM), dot = c->on ? 12 : 0;
        unsigned int fill = role == UI_PRIMARY ? ui_role(UI_PRIMARY_CONTAINER) : role < 0 ? 0 : ui_role(role + 1);
        unsigned int ink = role == UI_PRIMARY ? ui_role(UI_ON_PRIMARY_CONTAINER) : role < 0 ? ui_role(UI_ON_SURFACE_VARIANT) : ui_role(role);
        struct ui_box box;

        if (role == UI_SURFACE_HIGHEST)
                fill = ui_role(UI_SURFACE_HIGHEST), ink = ui_role(UI_ON_SURFACE_VARIANT);
        c->w = width / ui->scale + 18 + dot;
        c->h = 22;
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(22), ui_q(6));
        if (fill)
                ui_kit_fill(ui, &box, fill, null);
        if (role < 0)
                ui_kit_outline(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
        if (dot)
                ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x + 9), ui_q8(ui, c->y + 8), ui_q8(ui, c->x + 15), ui_q8(ui, c->y + 14), ui_q8(ui, 3), ui_q8(ui, 3)},
                            ink, null);
        ui_control_label(ui, c, 9 + dot, 3, UI_LABEL_MEDIUM, ink);
}

static fn ui_tag_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int width = ui_kit_measure(ui, (string_address)c->label, UI_MONO);
        unsigned int ink = c->on ? ui_role(UI_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT);
        struct ui_box box;

        c->w = width / ui->scale + 20;
        c->h = 26;
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(26), ui_q(6));
        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_LOW), null);
        ui_kit_outline(ui, &box, ui_q8(ui, 1), c->on ? ui_mix(ui_role(UI_PRIMARY), 35, ui_role(UI_OUTLINE_VARIANT)) : ui_role(UI_OUTLINE_VARIANT));
        ui_control_label(ui, c, 10, 5, UI_MONO, ink);
}

static fn ui_avatar_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = c->w ? c->w : 32, width = ui_kit_measure(ui, (string_address)c->label, UI_TITLE_SMALL);
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(size), ui_q(size), ui_q(c->variant ? 6 : size / 2));
        const struct ui_type address_to t = &ui_types[UI_LABEL_MEDIUM];
        const font_face address_to faces[2] = {ui->bold ? ui->bold : ui->regular, ui->regular};

        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_HIGHEST), null);
        width = ui_run_in(ui, faces, 0, 0, (string_address)c->label, t->size, 0, 0, 0, ui->width, 0);
        ui_run_in(ui, faces, ui_dev(ui, c->x) + (ui_dev(ui, size) - width) / 2, ui_kit_baseline(ui, ui_dev(ui, c->y) + (ui_dev(ui, size) - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM),
                  (string_address)c->label, t->size, ui_role(UI_ON_SURFACE) & 0xffffffu, 255, 0, ui->width, 0);
        if (c->on)
        {
                struct ui_box ring = ui_box_of(ui, ui_q(c->x + size - 8 - 2), ui_q(c->y + size - 8 - 2), ui_q(13), ui_q(13), ui_q(7));
                struct ui_box dot = ui_box_of(ui, ui_q(c->x + size - 8), ui_q(c->y + size - 8), ui_q(9), ui_q(9), ui_q(5));

                ui_kit_fill(ui, &ring, ui_role(UI_SURFACE_LOW), null);
                ui_kit_fill(ui, &dot, ui_role(UI_SUCCESS), null);
        }
}

static fn ui_spinner_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = c->w ? c->w : 20;
        b64 ring = size == 14 ? ui_q(2) : size == 28 ? ui_q(3) : ui_q(5) / 2;
        b32 cx = ui_q8(ui, c->x) + ui_q8(ui, size) / 2, cy = ui_q8(ui, c->y) + ui_q8(ui, size) / 2;
        b32 outer = ui_q8(ui, size) / 2, inner = outer - (b32)((ring * ui->scale) >> 22);
        b64 turn = ui_kit.motion ? 0 : ui_progress(ui_kit.now % (700 * UI_MS), 700 * UI_MS); // linear, a turn in .7 s
        b64 sine, cosine;
        b32 first = (cx - outer) >> 8, last = (cx + outer + 255) >> 8;

        ui_sincos(ui_qmul(turn, 6746518852ll), &sine, &cosine); // 2 pi
        // The ring, and the top quarter of it (45 to 135 degrees, mitred as a border's corners are) turned.
        ui_kit_ring(ui, &(struct ui_box){cx - outer, cy - outer, cx + outer, cy + outer, outer, outer}, outer - inner, ui_role(UI_SURFACE_HIGHEST));
        for (b32 y = (cy - outer) >> 8; y < (cy + outer + 255) >> 8; y++)
        {
                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                for (b32 x = first < 0 ? 0 : first; x < last && x < ui->width; x++)
                {
                        unsigned int hits = 0;

                        for (int s = 0; s < 16; s++)
                        {
                                b64 px = ((b64)x * 256 + (s & 3) * 64 + 32 - cx), py = ((b64)y * 256 + (s >> 2) * 64 + 32 - cy);
                                // Turned back by the spin: is the point in the top quarter of the ring?
                                // px and py are negative left of and above the ring's middle, and a left shift of a
                                // negative is undefined: they are multiplied.
                                b64 ux = (ui_qmul(px * (1ll << 22), cosine) + ui_qmul(py * (1ll << 22), sine)) >> 22;
                                b64 uy = (ui_qmul(py * (1ll << 22), cosine) - ui_qmul(px * (1ll << 22), sine)) >> 22;
                                b64 r2 = ux * ux + uy * uy;

                                hits += r2 <= (b64)outer * outer && r2 >= (b64)inner * inner && uy < 0 && (ux < 0 ? -ux : ux) <= -uy;
                        }
                        if (hits)
                        {
                                unsigned int address_to at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x;

                                *at = ui_over(ui_premultiplied(ui_role(UI_PRIMARY), hits * UI_FULL / 16), *at);
                        }
                }
        }
        if (!ui_kit.motion)
                ui_kit.busy = 1;
}

// A surface: a card (14 round, the low face, a hairline) or a window (20 round, the high face, a hairline and a lift).
static fn ui_surface_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 window = c->kind == UI_WINDOW;
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(window ? 20 : 14));

        if (window)
        {
                ui_kit_shadow(ui, &box, 0, ui_q8(ui, 10), ui_q8(ui, 30), -ui_q8(ui, 12), ui_mix(ui_role(UI_SHADOW), 14, 0));
                ui_kit_shadow(ui, &box, 0, ui_q8(ui, 1), ui_q8(ui, 2), 0, ui_mix(ui_role(UI_SHADOW), 4, 0));
        }
        ui_kit_fill(ui, &box, ui_role(window ? UI_SURFACE_HIGH : UI_SURFACE_LOW), null);
        if (window)
        {
                struct ui_box outer = box;

                outer.x0 -= ui_q8(ui, 1), outer.y0 -= ui_q8(ui, 1), outer.x1 += ui_q8(ui, 1), outer.y1 += ui_q8(ui, 1);
                outer.rx += ui_q8(ui, 1), outer.ry += ui_q8(ui, 1);
                ui_kit_outline(ui, &outer, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
                if (c->label)
                {
                        ui_control_label(ui, c, 24, 24, UI_TITLE_LARGE, ui_role(UI_ON_SURFACE));
                        ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x), ui_q8(ui, c->y + 68), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + 69), 0, 0},
                                    ui_role(UI_OUTLINE_VARIANT), null);
                }
        }
        else
                ui_kit_outline(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
}

static fn ui_callout_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int role = c->role;
        unsigned int fill = role ? ui_role(role == UI_PRIMARY ? UI_PRIMARY_CONTAINER : role + 1) : ui_role(UI_SURFACE_LOW);
        unsigned int ink = role ? ui_role(role == UI_PRIMARY ? UI_ON_PRIMARY_CONTAINER : role + 2) : ui_role(UI_ON_SURFACE_VARIANT);
        unsigned int line = role ? ui_mix(ui_role(role), 28, ui_role(UI_OUTLINE_VARIANT)) : ui_role(UI_OUTLINE_VARIANT);
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(14));

        ui_kit_fill(ui, &box, fill, null);
        ui_kit_outline(ui, &box, ui_q8(ui, 1), line);
        ui_control_label(ui, c, 16, 14, UI_LABEL_LARGE, role ? ink : ui_role(UI_ON_SURFACE));
        if (c->text[0])
                ui_kit_text(ui, ui_dev(ui, c->x + 16), ui_kit_baseline(ui, ui_dev(ui, c->y + 14 + 24), UI_BODY_MEDIUM), (string_address)c->text,
                            UI_BODY_MEDIUM, ink, 0);
}

static fn ui_code_draw(struct ui address_to ui, struct ui_control address_to c)
{
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(14));
        struct ui_box head = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(37), 0);

        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_HIGHEST), null);
        ui_kit_fill(ui, &head, ui_role(UI_SURFACE_LOW), &box);
        ui_kit_fill(ui, &(struct ui_box){head.x0, head.y1 - ui_q8(ui, 1), head.x1, head.y1, 0, 0}, ui_role(UI_OUTLINE_VARIANT), null);
        ui_kit_outline(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
        ui_control_label(ui, c, 14, 10, UI_MONO, ui_role(UI_ON_SURFACE_VARIANT));
        for (int k = 0; k < c->count; k++)
                ui_kit_text(ui, ui_dev(ui, c->x + 14), ui_kit_baseline(ui, ui_dev(ui, c->y + 37 + 14 + k * 21), UI_CODE), (string_address)c->items[k],
                            UI_CODE, ui_role(UI_ON_SURFACE), 0);
}

static fn ui_kbd_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int width = ui_kit_measure(ui, (string_address)c->label, UI_MONO);
        struct ui_box box, foot;

        c->w = width / ui->scale + 10 < 20 ? 20 : width / ui->scale + 10;
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(20), ui_q(3));
        foot = box;
        foot.y1 -= ui_q8(ui, 1);
        ui_kit_fill(ui, &box, ui_role(UI_OUTLINE_VARIANT), null);
        foot.x0 += ui_q8(ui, 1), foot.y0 += ui_q8(ui, 1), foot.x1 -= ui_q8(ui, 1), foot.y1 -= ui_q8(ui, 1);
        foot.rx = foot.ry = ui_q8(ui, 2);
        ui_kit_fill(ui, &foot, ui_role(UI_SURFACE_LOW), null);
        ui_kit_text(ui, ui_dev(ui, c->x) + (ui_dev(ui, c->w) - width) / 2, ui_kit_baseline(ui, ui_dev(ui, c->y + 1), UI_MONO), (string_address)c->label,
                    UI_MONO, ui_role(UI_ON_SURFACE_VARIANT), 0);
}

/*
        RISE, the entrance: a control (or a group, staggered by index times
        --rb-step 48 ms) shows from opacity 0 and scaleY 1.45, anchored at its
        top (from below) or its bottom (from above), to 1 over --rb-rise 440 ms
        on --rb-ease-rise; with reduced motion it is shown at once.
*/
struct ui_rise
{
        b64 start;   // the first frame, 1 pending, 0 shown
        int index;
        b32 from_top;
};

// The rows a control that moved this frame reaches, with room for its ripple, its shadow and its stretch: those it
// said move, with room for a stretch and a ring, when it said (tabs on end), else its box and room round it.
static fn ui_kit_mark(const struct ui address_to ui, const struct ui_control address_to c)
{
        int top = c->y - 40, bottom = c->y + (c->kind == UI_RADIOS ? c->count * UI_RADIO_PITCH : c->h) + 40;

        if (ui_kit.reached)
        {
                if (ui_kit.reach_bottom <= ui_kit.reach_top)
                        return;
                top = ui_kit.reach_top - 8, bottom = ui_kit.reach_bottom + 8;
        }
        // A list's row (its own state layer and ripple, or its switch) draws nothing past the row: no list row is
        // squashed or moved, its ripple is cut to the row, and a switch's bars are 28 of its 40 grown at most 1.1.
        // The row is in logical pixels, cut from device ones: one more each way holds what the cut left off.
        else if (c->row_w > 0 && (c->kind == UI_LIST_ROW || c->kind == UI_SWITCH || c->kind == UI_CHECKBOX))
                top = c->row_y - 1, bottom = c->row_y + c->row_h + 1;
        top = ui_dev(ui, top < 0 ? 0 : top);
        bottom = ui_dev(ui, bottom);
        if (ui_kit.damage_bottom <= ui_kit.damage_top)
                ui_kit.damage_top = top, ui_kit.damage_bottom = bottom;
        else
        {
                ui_kit.damage_top = top < ui_kit.damage_top ? top : ui_kit.damage_top;
                ui_kit.damage_bottom = bottom > ui_kit.damage_bottom ? bottom : ui_kit.damage_bottom;
        }
}

static fn ui_control_draw_one(struct ui address_to ui, struct ui_control address_to c);

// One control drawn, whatever it is; the rows it reaches marked when anything in it is moving.
static fn ui_control_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 was = ui_kit.busy;

        ui_kit.busy = 0;
        ui_kit.reached = 0;
        ui_kit.reach_top = ui_kit.reach_bottom = 0;
        ui_control_draw_one(ui, c);
        if (ui_kit.busy)
                ui_kit_mark(ui, c);
        ui_kit.busy |= was;
}

/*
        A control placed: what it shows is its state now, with nothing moving.
        A control is placed at its first frame, so a value set when it is made,
        or any time before it is first drawn (the settings' values come from
        the verbs after the row is made), is where it starts and not a change;
        and again whenever the kit settles (a resize, a theme, a scale) or its
        app forgets it. Only a change after that is drawn moving.
*/
static fn ui_control_place(struct ui_control address_to c)
{
        struct ui_tween address_to unplaced[] = {&c->layer, &c->fill, &c->ink, &c->label_ink, &c->stroke, &c->scale, &c->pip_x, &c->pip_w, &c->pip_y, &c->shadow};

        for (positive k = 0; k < sizeof(unplaced) / sizeof(unplaced[0]); k++)
                unplaced[k]->placed = 0;
        for (int k = 0; k < UI_ITEMS; k++)
        {
                c->item_ink[k].placed = 0;
                ui_tween_put(&c->item_scale[k], k == c->at ? 0 : UI_ONE, UI_TWEEN_NUMBER);
                c->item_dom[k].at = 0;
        }
        // A slider's extent is its thumb's halo radius, which is its state's at the first frame, as its shadow is: unplaced.
        if (c->kind == UI_SLIDER)
                c->extent.placed = 0;
        else
                ui_tween_put(&c->extent, c->kind == UI_PROGRESS ? (c->value < 0 ? 0 : ui_progress_whole(c->value)) : c->on ? UI_ONE : 0, UI_TWEEN_NUMBER);
        ui_tween_put(&c->spot, 0, UI_TWEEN_NUMBER);
        for (int k = 0; k < 3; k++)
                c->ripple[k].live = 0;
        ui_spring_stop(&c->squish.x);
        ui_spring_stop(&c->squish.y);
        memory_zero(&c->squish.shown, sizeof(c->squish.shown));
        c->squish.holding = c->squish.pulled = 0;
        ui_spring_stop(&c->held_squish);
        memory_zero(&c->drawn, sizeof(c->drawn));
        c->dom.at = 0;
        c->pip_squash = c->pip_press = 0;
        c->sliding = 0;
        c->slide.landed = 0;
        c->bend = c->bend_target = 0;
        c->moved = c->down ? c->moved : 0; // a drag under way is still a drag: its release says whether it flips
        c->shadow_to[0][4] = 0; // a field's shadows are put at its state
        c->field_start = 0;
        c->placed = ui_kit.epoch;
}

/*
        A list's row with no control of the kit's in it (a choice, a field's row, a command): the kit's hover state layer
        (the ink at .06, .2s) and its press ripple in the accent, across the whole row, edge to edge.
*/
static fn ui_list_row_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int x, y, w, h;
        struct ui_box row;
        struct ui_place place = {UI_ONE, UI_ONE, 0, 0, 0, 0};
        unsigned int layer;

        ui_control_row(c, &x, &y, &w, &h);
        row = (struct ui_box){ui_q8(ui, x), ui_q8(ui, y), ui_q8(ui, x + w), ui_q8(ui, y + h), 0, 0};
        ui_kit_layer_to(c, c->disabled);
        layer = ui_kit_layer(c);
        ui_kit_layer_fill(ui, &row, ui_text_colour(ui, UI_ON_SURFACE), layer);
        ui_kit_ripples(ui, c, row.x0, row.y0, &place, &row, ui_role(UI_PRIMARY));
}

// A press on a list row: its ripple from the press, across the row (device pixels).
static fn ui_list_row_press(const struct ui address_to ui, struct ui_control address_to c, int px, int py)
{
        int x, y, w, h;

        ui_control_row(c, &x, &y, &w, &h);
        ui_kit_ripple_start(ui, c, px, py, ui_dev(ui, x), ui_dev(ui, y), ui_dev(ui, w), ui_dev(ui, h), 0);
}

/*
        Swatches: a colour each (the items, as rrggbb), 20 round and 8 apart, the last at the box's right edge and all of
        them on its middle; the chosen one (at) ringed in the ink 3 out, and the one under the pointer ringed faintly.
*/
#define UI_SWATCH 20
#define UI_SWATCH_GAP 8

static int ui_swatches_left(const struct ui address_to ui, const struct ui_control address_to c)
{
        return ui_dev(ui, c->x + c->w) - c->count * ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP) + ui_dev(ui, UI_SWATCH_GAP);
}

// The swatch under a device point, or -1: each owns its colour and half the gap either side.
static int ui_swatch_at(const struct ui address_to ui, const struct ui_control address_to c, int x, int y)
{
        int left = ui_swatches_left(ui, c) - ui_dev(ui, UI_SWATCH_GAP) / 2, step = ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP);

        if (!c->count || x < left || x >= left + c->count * step ||
            !ui_hit(ui, ui_kit_touch, x, y, 0, ui_dev(ui, c->y + c->h / 2 - UI_SWATCH / 2), ui->width, ui_dev(ui, UI_SWATCH)))
                return -1;
        return (x - left) / step;
}

static fn ui_swatches_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = ui_dev(ui, UI_SWATCH), step = ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP), left = ui_swatches_left(ui, c);
        b32 out = ui_q8(ui, 3), s = size * 256, y = (ui_dev(ui, c->y) + (ui_dev(ui, c->h) - size) / 2) * 256;

        for (int k = 0; k < c->count; k++)
        {
                long colour = window_colour_parse((string_address)c->items[k]);
                b32 x = (left + k * step) * 256;
                struct ui_box dot = {x, y, x + s, y + s, s / 2, s / 2}, ring = {x - out, y - out, x + s + out, y + s + out, s / 2 + out, s / 2 + out};

                ui_kit_fill(ui, &dot, 0xff000000u | (unsigned int)(colour < 0 ? 0 : colour), null);
                if (k == c->at)
                        ui_kit_ring(ui, &ring, ui_q8(ui, 2), ui_role(UI_ON_SURFACE));
                else if (c->hover && k == c->item && !c->disabled)
                        ui_kit_ring(ui, &ring, ui_q8(ui, 2), ui_mix(ui_role(UI_ON_SURFACE), 30, 0));
        }
}

static fn ui_control_draw_one(struct ui address_to ui, struct ui_control address_to c)
{
        if (c->placed != ui_kit.epoch)
                ui_control_place(c);
        switch (c->kind)
        {
        case UI_LIST_ROW: ui_list_row_draw(ui, c); break;
        case UI_SWATCHES: ui_swatches_draw(ui, c); break;
        case UI_BUTTON: ui_button_draw(ui, c); break;
        case UI_SWITCH:
        case UI_CHECKBOX: ui_toggle_draw(ui, c); break;
        case UI_RADIOS: ui_radios_draw(ui, c); break;
        case UI_SLIDER: ui_slider_draw(ui, c); break;
        case UI_TABS: ui_tabs_draw(ui, c); break;
        case UI_INPUT: ui_input_draw(ui, c); break;
        case UI_PROGRESS: ui_progress_draw(ui, c); break;
        case UI_BADGE: ui_badge_draw(ui, c); break;
        case UI_TAG: ui_tag_draw(ui, c); break;
        case UI_AVATAR: ui_avatar_draw(ui, c); break;
        case UI_CALLOUT: ui_callout_draw(ui, c); break;
        case UI_CODE_WELL: ui_code_draw(ui, c); break;
        case UI_KBD: ui_kbd_draw(ui, c); break;
        case UI_SPINNER: ui_spinner_draw(ui, c); break;
        case UI_CARD:
        case UI_WINDOW: ui_surface_draw(ui, c); break;
        case UI_HEADING:
                ui_kit_text(ui, ui_dev(ui, c->x), ui_kit_baseline(ui, ui_dev(ui, c->y), c->variant), (string_address)c->label, c->variant,
                            ui_role(c->role), 0);
                break;
        case UI_RULE:
                ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x), ui_q8(ui, c->y), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + 1), 0, 0},
                            ui_role(UI_OUTLINE_VARIANT), null);
                break;
        }
}

/* ---- the pointer ---- */

// Whether a device point is in a control's box (a radio group: which row; tabs: which tab), -1 when not.
static int ui_control_hit(struct ui address_to ui, const struct ui_control address_to c, int x, int y)
{
        int lx = x / ui->scale, ly = y / ui->scale;

        if (c->kind == UI_RADIOS)
        {
                for (int k = 0; k < c->count; k++)
                        if (lx >= c->x && lx < c->x + c->w && ly >= ui_radio_row_y(c, k) && ly < ui_radio_row_y(c, k) + c->h)
                                return k;
                return -1;
        }
        // A tab is a finger's UI_TARGET each way where it is smaller, as every control is (ui_hit).
        if (c->kind == UI_TABS && c->variant == UI_TABS_VERTICAL)
        {
                for (int k = 0; k < c->count; k++)
                        if (ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + k * c->h), ui_dev(ui, c->w), ui_dev(ui, c->h)))
                                return k;
                return -1;
        }
        if (c->kind == UI_TABS)
        {
                for (int k = 0; k < c->count; k++)
                        if (ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, ui_tab_left(ui, c, k)), ui_dev(ui, c->y), ui_dev(ui, ui_tab_width(ui, c, k)),
                                   ui_dev(ui, c->variant ? 32 : 40)))
                                return k;
                return -1;
        }
        if (c->kind == UI_INPUT && c->variant == UI_FIELD_BARE)
                return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h ? c->h : 48)) ? 0 : -1;
        if (c->kind == UI_INPUT)
                return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + 30), ui_dev(ui, c->w), ui_dev(ui, 48)) ? 0 : -1;
        if (c->kind == UI_SWATCHES)
                return ui_swatch_at(ui, c, x, y);
        (void)lx, (void)ly;
        return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h)) ? 0 : -1;
}

static b32 ui_control_live(const struct ui_control address_to c)
{
        return !c->disabled && (c->kind == UI_BUTTON || c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS ||
                                c->kind == UI_SLIDER || c->kind == UI_TABS || c->kind == UI_INPUT);
}

// The slider's value under a device x: ui.js's a(), the track's whole width, snapped to the step.
static b64 ui_slider_at(struct ui address_to ui, const struct ui_control address_to c, int x)
{
        b64 ratio = ui_qdiv(ui_q(x - ui_dev(ui, c->x)), ui_q(ui_dev(ui, c->w)));

        ratio = ratio < 0 ? 0 : ratio > UI_ONE ? UI_ONE : ratio;
        return ui_slider_snap(c, c->low + ui_qmul(ratio, c->high - c->low));
}

/*
        The pointer's events for the controls: motion, a press and a release
        (the left button), each as ui.js's handlers take them. Answers the
        control whose value changed (a toggle, a choice, a slider's value, a
        button's click), or null. A control holding the pointer takes every
        motion till the release (setPointerCapture).
*/
static struct ui_control address_to ui_kit_held;

static struct ui_control address_to ui_kit_pointer(struct ui address_to ui, struct ui_control address_to controls, int count, int type, int x, int y,
                                                   b32 down, unsigned int flags)
{
        struct ui_control address_to changed = null;

        ui_kit_touch = (flags & WINDOW_KEY_TOUCH) != 0;
        // A press the compositor takes back (a second finger, a finger held for the right button) clicks nothing:
        // what it held springs home as a release would, and no value changes.
        if (type == WINDOW_EVENT_BUTTON && !down && (flags & WINDOW_KEY_CANCEL) && ui_kit_held)
        {
                struct ui_control address_to c = ui_kit_held;

                ui_kit_held = null;
                c->down = c->moved = 0;
                c->sliding = c->sliding && c->kind == UI_SLIDER;
                ui_kit_ripples_exit(c);
                ui_squish_release(&c->squish);
                if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX)
                        ui_kit_number_to(&c->extent, c->on ? UI_ONE : 0, 200, &ui_ease);
                c->bend = c->bend_target = 0;
                return null;
        }

        if (type == WINDOW_EVENT_MOTION)
        {
                struct ui_control address_to c = ui_kit_held;

                if (c)
                {
                        int dx = x - c->press_x, dy = y - c->press_y;
                        b64 ldx = ui_qdiv(ui_q(dx), ui_q(ui->scale)), ldy = ui_qdiv(ui_q(dy), ui_q(ui->scale));

                        c->pointer_x = x, c->pointer_y = y;
                        if (c->kind == UI_BUTTON)
                        {
                                ui_squish_move(&c->squish, ui_squash_of(ldx, ldy));
                                // pointerleave ends the ripple.
                                if (ui_control_hit(ui, c, x, y) < 0)
                                        ui_kit_ripples_exit(c);
                        }
                        else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS)
                        {
                                b32 right = dx > 0, toward = right != (c->kind == UI_RADIOS ? c->item == c->at : c->on);
                                b64 fill;

                                if (!c->moved && dx < 3 * ui->scale && dx > -3 * ui->scale && dy < 3 * ui->scale && dy > -3 * ui->scale)
                                        return null;
                                c->moved = 1;
                                if (c->kind != UI_RADIOS)
                                {
                                        fill = (c->on ? UI_ONE : 0) + ldx / 100;
                                        fill = fill < 0 ? 0 : fill > UI_ONE ? UI_ONE : fill;
                                        ui_tween_put(&c->extent, fill, UI_TWEEN_NUMBER);
                                }
                                c->bend = ui_qmul(ui_squash_curve((ldx < 0 ? -ldx : ldx) / 52), toward ? UI_ONE : UI_Q2(30));
                                c->squish_origin = right ? 1 : -1;
                                ui_squish_move(&c->squish, ui_squash_of(ldx, ldy));
                        }
                        else if (c->kind == UI_SLIDER)
                        {
                                b64 over = 0, f = ldy;
                                int left = ui_dev(ui, c->x), right = left + ui_dev(ui, c->w);

                                if (!c->moved && dx < 4 * ui->scale && dx > -4 * ui->scale)
                                        return null;
                                c->moved = 1;
                                c->sliding = 0;
                                c->value = ui_slider_at(ui, c, x);
                                c->spot_x = x / ui->scale - c->x;
                                changed = c;
                                over = x < left ? ui_q(left - x) : x > right ? ui_q(x - right) : 0;
                                c->bend_target = over > 0 ? ui_qmul(ui_squash_curve(ui_qdiv(over, ui_q(right - left)) * 2), UI_Q2(130)) : 0;
                                if (over > 0)
                                        c->squish_origin = x < left ? -1 : 1;
                                {
                                        struct ui_squash s = {0, ui_squash_curve((f < 0 ? -f : f) / 400), 0, 0};

                                        s.ty = f > 0 ? s.sy * 8 : f < 0 ? -s.sy * 8 : 0;
                                        ui_squish_move(&c->squish, s);
                                }
                        }
                        return changed;
                }
                for (int k = 0; k < count; k++)
                {
                        struct ui_control address_to c = &controls[k];
                        int hit = ui_control_live(c) ? ui_control_hit(ui, c, x, y) : -1;

                        if ((hit >= 0) != c->hover || (hit >= 0 && hit != c->item))
                        {
                                if (hit < 0 && c->hover)
                                        ui_kit_ripples_exit(c);
                                c->hover = hit >= 0;
                                c->item = hit;
                        }
                        if (c->kind == UI_SLIDER)
                                c->hover_x = hit >= 0 ? x : -1, c->pointer_x = x, c->pointer_y = y; // the thumb's ring follows the hand
                }
                return null;
        }
        if (type != WINDOW_EVENT_BUTTON)
                return null;
        if (down)
        {
                // A press whose release never came is let go first, as a cancel lets it go: it is up, and its ripple runs
                // out. Left held, its ripple never exits and the frame never goes idle.
                if (ui_kit_held)
                {
                        struct ui_control address_to lost = ui_kit_held;

                        ui_kit_held = null;
                        lost->down = lost->moved = 0;
                        ui_kit_ripples_exit(lost);
                        ui_squish_release(&lost->squish);
                }
                // A press takes the focus from wherever it was: a field's, and a button's (a click focuses a button in Chrome).
                for (int k = 0; k < count; k++)
                        controls[k].focused = 0;
                for (int k = 0; k < count; k++)
                {
                        struct ui_control address_to c = &controls[k];
                        int hit = ui_control_live(c) ? ui_control_hit(ui, c, x, y) : -1;

                        if (hit < 0)
                                continue;
                        if (c->kind == UI_BUTTON)
                                c->focused = 1; // button[outlined]:focus: its border is the accent
                        ui_kit_held = c;
                        c->down = 1;
                        c->moved = 0;
                        c->item = hit;
                        c->hover = 1;
                        c->press_x = c->pointer_x = x, c->press_y = c->pointer_y = y;
                        c->pressed_at = ui_kit.now;
                        ui_squish_press(&c->squish);
                        if (c->kind == UI_BUTTON)
                                ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h), 8);
                        else if (c->kind == UI_TABS)
                        {
                                // The tab's own box (on end, its item's row); a press on the chosen tab rides its indicator.
                                struct ui_ripple address_to r = c->variant == UI_TABS_VERTICAL
                                                                    ? ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + hit * c->h),
                                                                                          ui_dev(ui, c->w), ui_dev(ui, c->h), 8)
                                                                    : ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, ui_tab_left(ui, c, hit)), ui_dev(ui, c->y),
                                                                                          ui_dev(ui, ui_tab_width(ui, c, hit)), ui_dev(ui, c->variant ? 32 : 40), 8);

                                if (r)
                                        r->rides = hit == c->at && c->variant != UI_TABS_UNDERLINE;
                        }
                        else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_LIST_ROW)
                        {
                                ui_spring_stop(&c->held_squish);
                                ui_list_row_press(ui, c, x, y);
                        }
                        else if (c->kind == UI_RADIOS)
                                ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x - 24), ui_dev(ui, ui_radio_row_y(c, hit) - 8), ui_dev(ui, c->w + 48),
                                                    ui_dev(ui, c->h + 16), 0);
                        else if (c->kind == UI_INPUT)
                                c->focused = 1;
                        else if (c->kind == UI_SLIDER)
                        {
                                b64 from = c->value, to = ui_slider_at(ui, c, x);

                                ui_spring_stop(&c->held_squish);
                                c->slide.landed = 0;
                                c->bend = c->bend_target = 0;
                                c->squish_origin = 0;
                                c->last_frame = 0;
                                c->bounce = 0;
                                c->slide_from = from;
                                if ((to > from ? to - from : from - to) > 1073741 && !ui_kit.motion) // 0.001
                                {
                                        ui_slide_go(&c->slide, from, to, c->high - c->low, ui_kit.now);
                                        c->sliding = c->bounce = 1;
                                        c->squish_origin = to >= from ? 1 : -1;
                                }
                                else
                                        c->value = to;
                                if (c->value != from || c->sliding)
                                        changed = c;
                                c->spot_x = x / ui->scale - c->x;
                                ui_tween_put(&c->spot, UI_Q2(10), UI_TWEEN_NUMBER);
                                c->hover_x = -1; // v(B): the hover preview is over till the pointer moves again
                        }
                        break;
                }
                return changed;
        }
        // The release.
        {
                struct ui_control address_to c = ui_kit_held;

                ui_kit_held = null;
                if (!c)
                        return null;
                c->down = 0;
                ui_kit_ripples_exit(c);
                if (c->kind == UI_BUTTON)
                {
                        ui_squish_release(&c->squish);
                        if (ui_control_hit(ui, c, x, y) >= 0)
                                changed = c;
                }
                else if (c->kind == UI_TABS)
                {
                        ui_squish_release(&c->squish);
                        if (ui_control_hit(ui, c, x, y) == c->item)
                        {
                                ui_tabs_choose(ui, c, c->item);
                                changed = c;
                        }
                }
                else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS)
                {
                        int dx = x - c->press_x;
                        b64 ldx = ui_qdiv(ui_q(dx), ui_q(ui->scale));
                        b32 right = dx > 0;

                        b32 was = c->kind == UI_RADIOS ? c->item == c->at : c->on;

                        if ((c->moved && right != was && (ldx > ui_q(100) || ldx < -ui_q(100))) || !c->moved)
                        {
                                if (c->kind == UI_RADIOS)
                                {
                                        if (c->item != c->at)
                                        {
                                                ui_radios_choose(c, c->item);
                                                changed = c;
                                        }
                                        else if (!ui_kit.motion)
                                        {
                                                // Pressed again on the chosen circle: a squish in place, and the pip's own.
                                                int lx = x / ui->scale;

                                                if (lx >= c->x + c->w - 40 && lx < c->x + c->w - 12)
                                                {
                                                        c->dom_origin = 0;
                                                        ui_stretch_go(&c->item_dom[c->at], ui_kit.now, UI_Q2(84));
                                                        c->pip_press = 1;
                                                }
                                        }
                                }
                                else
                                {
                                        c->on = !c->on;
                                        changed = c;
                                        // The change: the row stretches toward where the knob went.
                                        c->dom_origin = c->on ? 1 : -1;
                                        if (!ui_kit.motion)
                                                ui_stretch_go(&c->dom, ui_kit.now, UI_Q2(111));
                                }
                        }
                        if (c->kind != UI_RADIOS)
                        {
                                // The bars go back to the width their state gives, from where the hand left them.
                                c->moved = 0;
                                ui_kit_number_to(&c->extent, c->on ? UI_ONE : 0, 200, &ui_ease);
                        }
                        c->moved = 0;
                        if (c->bend > UI_REST_SCALE && !ui_kit.motion)
                                ui_spring_go(&c->held_squish, c->bend, 0);
                        c->bend = 0;
                        ui_squish_release(&c->squish);
                }
                else if (c->kind == UI_SLIDER)
                {
                        b64 cur = c->bend > 3221225 ? c->bend : 0;

                        if (c->moved && cur > 0 && !ui_kit.motion)
                                ui_spring_go(&c->held_squish, cur, 0);
                        else if (c->bounce && !c->moved && !ui_kit.motion)
                                c->slide.landed = ui_kit.now;
                        else
                                c->squish_origin = 0;
                        c->bend = c->bend_target = 0;
                        c->moved = 0;
                        ui_kit_number_to(&c->spot, 0, 300, &ui_ease);
                        ui_squish_release(&c->squish); // pressSpring(B, 0, vy, 0, vyT): the drag's vertical press only
                }
                // The release is where the pointer is: a press dragged off the control and let go there is not hovered
                // (the held motions above never moved hover, and no motion comes after a release to move it).
                {
                        int hit = ui_control_live(c) ? ui_control_hit(ui, c, x, y) : -1;

                        c->hover = hit >= 0;
                        c->item = hit;
                }
                return changed;
        }
}

/*
        The scrollbar, every scrolling region's (ui.css's ::-webkit-scrollbar): a
        track 10 wide at the region's right edge, a clear 3 round a pill thumb (4
        wide), the ink at .18 and .30 while hovered or held, shown whenever what
        scrolls is taller than its view and never hidden. The thumb is the view's
        share of the track (24 at least) where the scroll's share of what is left
        puts it; it is dragged, a press on the track pages a view toward the
        press, and the wheel and a finger go on scrolling the region itself. An
        app gives the region (x its track's left, y and height its view's) and the
        three numbers, all device pixels; the kit draws it and takes its pointer.
        The terminal's scrollback and an editor's view take it the same way.
*/
#define UI_SCROLL_WIDTH 10
#define UI_SCROLL_BORDER 3
#define UI_SCROLL_LEAST 24

struct ui_scroll
{
        int x, y, height;            // the track: its left, its top and its height
        int content, view, scroll;   // what scrolls, what shows of it, how far it is scrolled
        b32 hover, held;
        int grab;                    // where in the thumb the hand took it
        int unit;                    // it scrolls in whole units of this (a menu's choices), or pixels (0)
        int wheel, left;             // a wheel's turn under a unit, and a finger's move under one, kept for the next
        // The glide: a wheel's turn, a throw or a reveal moves at, in 256ths of a device pixel, to aim as a critically
        // damped spring turns, its speed (256ths a second) and its time constant (ns). A finger holds at past an end
        // (dragging): what is drawn is the rubber band's share of that. back is the spring that takes a pull home.
        b64 at, aim, speed, stamp, tau;
        b32 gliding, dragging;
        b32 flung;                   // the finger is moving fast: a pull past an end gives nothing while it does (a flick clamps)
        struct ui_spring back;
        struct ui_tween ink, grow; // the thumb's ink and its growth across the track: the kit's tweens to lit or unlit
        int inset;                 // logical pixels from the region's top the track may not reach into (UI_SCROLL_INSET); 0 for none
};

/*
        The track's top and height (device pixels): the region's, from its top to its end, except that it starts below its
        inset: a window's close button is there, so a window's bar reaches the window's bottom and no higher than that.
*/
#define UI_SCROLL_INSET 1
#define UI_CLOSE_BOTTOM 19 // a window's close button ends this many logical pixels from its top (compositor.c's disc)
static void ui_scroll_track(const struct ui address_to ui, const struct ui_scroll address_to s, int address_to top, int address_to height)
{
        int inset = s->inset * ui->scale, bottom = s->y + s->height;

        *top = s->y > inset ? s->y : inset;
        *height = bottom > *top ? bottom - *top : 0;
}

// The thumb's top and length (device pixels); false when everything shows and there is no bar. A held thumb is drawn at its
// target, the pointer's or a wheel turn's glide aim, while a glide runs to it, and otherwise where the scroll is: a bar whose
// aim was never set is not drawn at it, and nothing lags the hand.
static b32 ui_scroll_thumb(const struct ui address_to ui, const struct ui_scroll address_to s, int address_to top, int address_to length)
{
        int track, at, least = UI_SCROLL_LEAST * ui->scale, rest = s->content - s->view, scroll;

        ui_scroll_track(ui, s, &at, &track);
        if (s->content <= s->view || s->view <= 0 || track <= 0)
                return 0;
        *length = (int)((b64)track * s->view / s->content);
        *length = *length < least ? least : *length > track ? track : *length;
        // The aim in 256ths, rounded as ui_scroll_show rounds it.
        scroll = s->held && s->gliding ? (int)((s->aim + 128) >> 8) : s->scroll;
        scroll = scroll < 0 ? 0 : scroll > rest ? rest : scroll;
        *top = at + (int)((b64)(track - *length) * scroll / rest);
        return 1;
}

/*
        The thumb is lit while the pointer is on it or it is held: its ink the on-surface role at full strength, and its
        thickness grown about the track's middle to UI_SCROLL_LIT (1.25 times). Unlit it is 18 of the on-surface ink at the
        thickness it always had. Both move on the kit's own tween over UI_SCROLL_MS, so the kit is busy while they do and
        idle when they settle. The track keeps its width, so the bar never moves the content; the thumb is flat.
*/
#define UI_SCROLL_LIT UI_Q2(125)
#define UI_SCROLL_MS 120

// The thumb's ink or its growth is between its states: the bar is drawn again while it is, and its rows marked to be.
static b32 ui_scroll_moving(const struct ui_scroll address_to s)
{
        return !ui_tween_still(&s->ink) || !ui_tween_still(&s->grow);
}

// The thumb's box for a top and length (device pixels): across the track, its thickness is the drawn one times the growth.
static void ui_scroll_box(const struct ui address_to ui, struct ui_scroll address_to s, int top, int length, struct ui_box address_to thumb)
{
        int border = UI_SCROLL_BORDER * ui->scale, width = UI_SCROLL_WIDTH * ui->scale;
        b64 centre = (b64)(s->x + width / 2) * 256, half = (b64)(width / 2 - border) * 256;
        b64 r = ui_qmul(half, ui_kit_tween(&s->grow));

        thumb->x0 = (b32)(centre - r), thumb->x1 = (b32)(centre + r);
        thumb->y0 = (b32)((top + border) * 256), thumb->y1 = (b32)((top + length - border) * 256);
        thumb->rx = thumb->ry = (b32)r;
}

// The track's rows are drawn again next frame, as a moving control's are.
static fn ui_scroll_mark(const struct ui address_to ui, const struct ui_scroll address_to s)
{
        int top, height, bottom;

        ui_scroll_track(ui, s, &top, &height);
        bottom = top + height;
        if (ui_kit.damage_bottom <= ui_kit.damage_top)
                ui_kit.damage_top = top, ui_kit.damage_bottom = bottom;
        else
        {
                ui_kit.damage_top = top < ui_kit.damage_top ? top : ui_kit.damage_top;
                ui_kit.damage_bottom = bottom > ui_kit.damage_bottom ? bottom : ui_kit.damage_bottom;
        }
}

static fn ui_scroll_draw(struct ui address_to ui, struct ui_scroll address_to s)
{
        int top, length;
        struct ui_box thumb;
        unsigned int ink;
        b32 lit = s->hover || s->held;

        if (!ui_scroll_thumb(ui, s, &top, &length))
                return;
        ui_kit_colour_to(&s->ink, lit ? ui_role(UI_ON_SURFACE) : ui_mix(ui_role(UI_ON_SURFACE), 18, 0), UI_SCROLL_MS, &ui_ease);
        ui_kit_number_to(&s->grow, lit ? UI_SCROLL_LIT : UI_ONE, UI_SCROLL_MS, &ui_ease);
        ink = ui_kit_colour(&s->ink);
        ui_scroll_box(ui, s, top, length, &thumb);
        if (ui_scroll_moving(s))
                ui_scroll_mark(ui, s);
        ui_kit_fill(ui, &thumb, ink, null);
}

/*
        Whether a press at x, y takes the bar: its track. A finger's reach is UI_TARGET each way, the track's middle kept
        (ui_hit), so a finger lands on a bar four pixels wide; a pointer's is the track as drawn.
*/
static b32 ui_scroll_reach(const struct ui address_to ui, const struct ui_scroll address_to s, int x, int y, b32 touch)
{
        int top, height;

        ui_scroll_track(ui, s, &top, &height);
        return ui_hit(ui, touch, x, y, s->x, top, UI_SCROLL_WIDTH * ui->scale, height);
}

/*
        The pointer on a scrollbar: motion (a held thumb follows the hand; hover lights it), a press (on the thumb it is
        held, on the track it pages) and a release. A finger (touch) takes the bar within its reach (ui_scroll_reach) and
        the thumb within UI_TARGET likewise. Answers the scroll it asks for, or -1 when the event was not its own
        (a press off it, a motion with nothing held); a press on it that pages answers the new scroll.
*/
static int ui_scroll_pointer(const struct ui address_to ui, struct ui_scroll address_to s, int type, int x, int y, b32 down, b32 touch)
{
        int top, length, at, track, rest = s->content - s->view;
        b32 bar = ui_scroll_thumb(ui, s, &top, &length);
        b32 on_track = bar && ui_scroll_reach(ui, s, x, y, touch);
        b32 on_thumb = on_track && ui_hit(ui, touch, x, y, s->x, top, UI_SCROLL_WIDTH * ui->scale, length);

        ui_scroll_track(ui, s, &at, &track);
        if (type == WINDOW_EVENT_MOTION)
        {
                s->hover = on_thumb;
                if (!s->held || !bar)
                        return -1;
                if (track - length <= 0)
                        return s->scroll;
                return (int)((b64)(y - s->grab - at) * rest / (track - length));
        }
        if (type != WINDOW_EVENT_BUTTON)
                return -1;
        if (!down)
        {
                b32 was = s->held;

                s->held = 0;
                return was ? s->scroll : -1;
        }
        if (!on_track)
                return -1;
        if (on_thumb)
        {
                s->held = 1;
                s->grab = y - top;
                return s->scroll;
        }
        return y < top ? s->scroll - s->view : s->scroll + s->view;
}

/*
        How a region scrolls, whatever moves it: it is held between its top and its end, in whole units where it has
        them; a band of what scrolls is brought into view; the wheel turns it a row a notch (a unit, where it has
        them), a finger moves it pixel for pixel; and the keys move a text a line, a view, or to an end. Each answers
        whether it moved, which is what the app draws again.
*/
static int ui_scroll_most(const struct ui_scroll address_to s)
{
        int most = s->content - s->view;

        return most > 0 ? most : 0;
}

/*
        The glide. A wheel's notch adds its velocity to the region's speed and its distance to the aim; a throw starts
        from its speed and aims where that speed carries it (speed times the time constant), and a reveal aims at the
        row with no speed. The position moves to the aim as a critically damped spring does, from its own speed: its
        speed falls by e in the time constant, and it never passes the aim (the speed is held to the distance). A wheel
        and a throw share the time constant, so they decay alike. Nothing reads a frame count: each frame moves the
        region by the time since its last.
*/
#define UI_SCROLL_REVEAL_NS 100000000l // a reveal's glide: its speed falls by e in this long
#define UI_SCROLL_TAU_NS 36000000l     // a wheel notch's and a throw's glide: its speed falls by e in this long (a notch settles in 158 ms at 1x)
#define UI_SCROLL_REST 128             // a glide within half a device pixel of its aim is at it
#define UI_SCROLL_SCALE 256            // 256ths of a device pixel in a pixel
#define UI_SCROLL_GLIDE 1              // the glide is built (the checks ask for it)

static b64 ui_scroll_most256(const struct ui_scroll address_to s)
{
        return (b64)ui_scroll_most(s) * UI_SCROLL_SCALE;
}

// An aim held inside the region, and to a whole unit where it has them, in 256ths.
static b64 ui_scroll_fit(const struct ui_scroll address_to s, b64 aim)
{
        b64 most = ui_scroll_most256(s);
        int px;

        aim = aim < 0 ? 0 : aim > most ? most : aim;
        if (s->unit > 0)
        {
                px = (int)(aim / UI_SCROLL_SCALE);
                aim = (b64)(px - px % s->unit) * UI_SCROLL_SCALE;
        }
        return aim;
}

// The part of a pull past an end that is drawn: the rubber band's share, 11 x H / (11 x + 20 H) of the pull x (in
// pixels) for a view of H, Apple's curve for a scroll view. In 256ths.
static b64 ui_scroll_give(const struct ui_scroll address_to s, b64 past)
{
        // Clamped so the products stay in 64 bits: a pull past 65536 pixels, or a view past 65536, is as far as it goes.
        b64 view = s->view > 0 ? s->view > 65536 ? 65536 : s->view : 0, x = past < 0 ? -past : past, den, give;

        x = x > (1ll << 24) ? 1ll << 24 : x;
        den = 11 * x + 5120 * view;
        give = den > 0 ? 2816 * x * view / den : 0;

        return past < 0 ? -give : give;
}

// Where the region is drawn, in 256ths: where it is, or past an end while a finger holds it there.
static b64 ui_scroll_shown(const struct ui_scroll address_to s)
{
        b64 most = ui_scroll_most256(s);

        if (!s->dragging)
                return s->at;
        if (s->at < 0)
                return s->flung ? 0 : ui_scroll_give(s, s->at);
        return s->at > most ? (s->flung ? most : most + ui_scroll_give(s, s->at - most)) : s->at;
}

// The drawn position, in whole device pixels, from where it is.
static void ui_scroll_show(struct ui_scroll address_to s)
{
        s->scroll = (int)((ui_scroll_shown(s) + UI_SCROLL_SCALE / 2) >> 8);
}

// A region at rest starts from where it is drawn: a write to scroll is taken there. One that is moving keeps its place.
static void ui_scroll_rest(struct ui_scroll address_to s)
{
        if (!s->gliding && !s->back.live && !s->dragging && (int)((s->at + UI_SCROLL_SCALE / 2) >> 8) != s->scroll)
                s->at = (b64)s->scroll << 8;
}

// The speed may not beat the distance to the aim, or the glide would pass it.
static void ui_scroll_limit(struct ui_scroll address_to s)
{
        b64 d = s->at - s->aim, lim = (d < 0 ? -d : d) * 1000000000ll / s->tau;

        s->speed = s->speed > lim ? lim : s->speed < -lim ? -lim : s->speed;
}

static void ui_scroll_glide_to(struct ui_scroll address_to s, b64 aim, b64 tau)
{
        s->aim = ui_scroll_fit(s, aim);
        s->tau = tau;
        s->gliding = 1;
        s->back.live = 0;
        s->dragging = 0;
        ui_scroll_limit(s);
}

// The jump: the position is put at scroll and whatever was moving stops. True when the drawn position or the motion changed.
static b32 ui_scroll_set(struct ui_scroll address_to s, int scroll)
{
        int most = ui_scroll_most(s), before = s->scroll;
        b32 moving = s->gliding || s->back.live || s->dragging;

        scroll = scroll > most ? most : scroll < 0 ? 0 : scroll;
        if (s->unit > 0)
                scroll -= scroll % s->unit;
        s->scroll = scroll;
        s->at = s->aim = (b64)scroll << 8;
        s->gliding = s->dragging = s->back.live = 0;
        s->speed = s->stamp = 0;
        return scroll != before || moving;
}

// The region's ends moved (its content or its view): a glide's aim is kept inside them, and at rest the position is.
static void ui_scroll_clamp(struct ui_scroll address_to s)
{
        if (s->gliding)
        {
                s->aim = ui_scroll_fit(s, s->aim);
                ui_scroll_limit(s);
        }
        else if (!s->back.live && !s->dragging)
                ui_scroll_set(s, s->scroll);
}

// A key or a band brought into view: a glide to where it shows, from where the region is going. False when it shows already.
static b32 ui_scroll_reveal(struct ui_scroll address_to s, int top, int height)
{
        int at = (int)((s->gliding ? s->aim : (b64)s->scroll << 8) / UI_SCROLL_SCALE), aim = at;
        b64 target;

        if (top < at)
                aim = top;
        else if (top + height > at + s->view)
                aim = top + height - s->view + (s->unit > 0 ? s->unit - 1 : 0);
        target = ui_scroll_fit(s, (b64)aim * UI_SCROLL_SCALE);
        if (target == (s->gliding ? s->aim : (b64)s->scroll << 8))
                return false;
        ui_scroll_rest(s);
        ui_scroll_glide_to(s, target, UI_SCROLL_REVEAL_NS);
        return true;
}

// A finger's move of the region by pixels (whole units at a time where it has them): it follows the finger, and past an
// end the content is pulled and the rubber band gives. True when what is drawn moved.
static b32 ui_scroll_drag(struct ui_scroll address_to s, int pixels)
{
        int before = s->scroll;

        if (s->unit > 0)
        {
                long total = (long)s->left + pixels;
                int steps = (int)(total / s->unit);

                s->left = (int)(total - (long)steps * s->unit);
                return !steps || ui_scroll_set(s, s->scroll + steps * s->unit);
        }
        if (!s->dragging)
        {
                ui_scroll_rest(s);
                s->dragging = 1, s->gliding = 0, s->back.live = 0, s->speed = s->stamp = 0;
        }
        s->at += (b64)pixels * UI_SCROLL_SCALE;
        ui_scroll_show(s);
        return s->scroll != before;
}

// A finger on the region: whatever was moving it stops where it is drawn, and the drag takes it from there.
static void ui_scroll_stop(struct ui_scroll address_to s)
{
        if (s->gliding || s->back.live)
        {
                s->at = s->aim = ui_scroll_shown(s);
                s->gliding = s->back.live = 0;
                s->speed = s->stamp = 0;
        }
}

// The finger let go: a pull springs back to the end it is past, and a throw glides on from where it is (speed in pixels a second).
static void ui_scroll_let_go(struct ui_scroll address_to s, b32 thrown, long speed)
{
        b64 most = ui_scroll_most256(s), shown;

        if (!s->dragging)
                return;
        shown = ui_scroll_shown(s);
        s->dragging = 0;
        s->stamp = 0;
        if (s->at < 0 || s->at > most)
        {
                b64 edge = s->at < 0 ? 0 : most;

                s->at = shown, s->aim = edge;
                s->gliding = s->speed = 0;
                // The kit's release spring takes the part past the end home: its amplitude is that part, in Q30 pixels.
                ui_spring_go(&s->back, 0, (shown - edge) << 22);
                ui_scroll_show(s);
                return;
        }
        if (thrown)
        {
                s->speed = (b64)speed * UI_SCROLL_SCALE;
                ui_scroll_glide_to(s, s->at + s->speed * UI_SCROLL_TAU_NS / 1000000000ll, UI_SCROLL_TAU_NS);
        }
        ui_scroll_show(s);
}

// The glide's frame, drawn at the kit's time: where the region is now, and the kit busy with it while it moves (its band
// is marked to be drawn again, as a moving control's rows are). A pull springing home is the kit's spring. True when moving.
static b32 ui_scroll_glide(const struct ui address_to ui, struct ui_scroll address_to s)
{
        b64 now = (b64)ui_kit.now, before = s->at, q, w, e, d, v, c, ct;

        if (s->dragging || !(s->gliding || s->back.live))
                return false;
        if (s->gliding)
        {
                // The first frame drawn starts the clock, and nothing moves in it, as a spring's first frame does.
                if (!s->stamp)
                        s->stamp = now;
                else if (now > s->stamp)
                {
                        q = ui_ms(now - s->stamp) / 1000;
                        w = UI_ONE * 1000000000ll / s->tau;
                        e = ui_exp_neg(ui_qmul(w, q));
                        d = s->at - s->aim;
                        v = s->speed;
                        c = v + ui_qmul(w, d);
                        ct = ui_qmul(c, q);
                        d = ui_qmul(d + ct, e);
                        v = ui_qmul(v - ui_qmul(w, ct), e);
                        if (d < UI_SCROLL_REST && d > -UI_SCROLL_REST)
                                s->at = s->aim, s->speed = 0, s->gliding = 0, s->stamp = 0;
                        else
                                s->at = s->aim + d, s->speed = v, s->stamp = now;
                }
        }
        if (s->back.live)
                ui_spring_at(&s->back, now), s->at = s->aim + (s->back.value[1] >> 22);
        ui_scroll_show(s);
        if (!s->gliding && !s->back.live && s->at == before)
                return false;
        ui_kit.busy = 1;
        if (s->height > 0)
        {
                // A row that overlaps the region paints up to a row past either end (a switch's knob at its foot), so the band
                // is the region and a row each way: what a frame that moved it must draw again.
                int pad = ui_dev(ui, UI_ROW), top = s->y - pad, bottom = s->y + s->height + pad;

                top = top < 0 ? 0 : top;
                if (ui_kit.damage_bottom <= ui_kit.damage_top)
                        ui_kit.damage_top = top, ui_kit.damage_bottom = bottom;
                else
                {
                        ui_kit.damage_top = top < ui_kit.damage_top ? top : ui_kit.damage_top;
                        ui_kit.damage_bottom = bottom > ui_kit.damage_bottom ? bottom : ui_kit.damage_bottom;
                }
        }
        return true;
}

// A turn of the wheel, in the 120ths of a notch the compositor sends: what is under a step is kept for the next turn.
// A notch is a row (a unit, where there are them), and its distance is added to the glide's speed as well as its aim.
static b32 ui_scroll_wheel(const struct ui address_to ui, struct ui_scroll address_to s, int delta)
{
        long total = (long)s->wheel + (long)delta * (s->unit > 0 ? 1 : UI_ROW * ui->scale);
        int steps = (int)(total / 120);
        b64 base, aim;

        s->wheel = (int)(total - (long)steps * 120);
        if (!steps)
                return false;
        base = s->gliding || s->back.live ? s->aim : s->dragging ? s->at : (b64)s->scroll << 8;
        aim = ui_scroll_fit(s, base - (b64)steps * (s->unit > 0 ? s->unit : 1) * UI_SCROLL_SCALE);
        if (aim == base)
                return false;
        if (s->dragging)
                s->dragging = 0, s->speed = 0;
        ui_scroll_rest(s);
        // The notch's own speed is its distance over the time constant, the throw's rule, so a notch and a throw decay alike.
        s->speed += (aim - base) * 1000000000ll / UI_SCROLL_TAU_NS;
        ui_scroll_glide_to(s, aim, UI_SCROLL_TAU_NS);
        return true;
}

/* ---- end of the kit's controls ---- */

// Anonymous memory for an app's own state, or a negative errno.
static long ui_map(positive size)
{
        return compositor_call(syscall(mmap), 0, size, 3, 0x22, (positive)-1, 0);
}

// A font engine and its atlas, atlas pixels a side, as one mapping. The default, 512, is 256 KiB of the image's address space
// in every program that links this, a shell under a 12 MiB cap among them. False when the memory is not there.
static b32 ui_engine_open_sized(struct ui address_to ui, positive atlas)
{
        positive atlas_at = (sizeof(font_engine) + 4095) & ~(positive)4095;
        long engine = ui_map(atlas_at + atlas * atlas);

        if (engine < 0 && engine > -4096)
                return false;
        ui->engine = (font_engine address_to)engine;
        font_start(ui->engine, (p8 address_to)(engine + atlas_at), (p32)atlas, (p32)atlas);
        return true;
}

static b32 ui_engine_open(struct ui address_to ui)
{
        return ui_engine_open_sized(ui, 512);
}

// A face from a font file, loaded into the engine's face. False when there is no path, the file, or a font in it.
static b32 ui_face_load(font_face address_to face, string_address path)
{
        positive size = 0, bytes = path ? compositor_map_file(path, &size) : 0;

        return bytes && font_load(face, (const p8 address_to)bytes, size, 0) == 0;
}

// The fonts an app draws in when it names none.
#define UI_FONT_REGULAR "/usr/share/fonts/inter/Inter-Regular.ttf"
#define UI_FONT_BOLD "/usr/share/fonts/inter/Inter-Bold.ttf"
#define UI_FONT_MONO "/usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf"

/*
        The scratch line the text is drawn through is at least this wide, and UI_SCRATCH_ROWS rows at the scale: it is
        mapped again only when it must grow, wider or at a larger scale. What it gives back is the mapping it made, at
        the scale it made it at: the size worked out at the scale now gave back the pages after it (a table, a buffer)
        when the scale grew, and a scale grown with the width as it was left text written past the scratch's end. A
        scratch the caller gave (scratch_scale 0) is never given back.
*/
static void ui_scratch_fit(struct ui address_to ui, unsigned int width)
{
        unsigned int scratch = width + 8u * (unsigned int)ui->scale;
        long mapped;

        if (scratch <= (unsigned int)ui->scratch_width && (!ui->scratch_scale ? ui->scratch != null : ui->scale <= ui->scratch_scale))
                return;
        scratch = scratch > (unsigned int)ui->scratch_width ? scratch : (unsigned int)ui->scratch_width;
        mapped = ui_map((positive)scratch * UI_SCRATCH_ROWS * (positive)ui->scale * 4);
        if (mapped < 0 && mapped > -4096)
                return;
        if (ui->scratch && ui->scratch_scale)
                compositor_call(syscall(munmap), (positive)ui->scratch,
                                (positive)ui->scratch_width * UI_SCRATCH_ROWS * (positive)ui->scratch_scale * 4, 0, 0, 0, 0);
        ui->scratch = (unsigned int address_to)mapped;
        ui->scratch_width = (int)scratch;
        ui->scratch_scale = ui->scale;
}

/* ---- menus: a popup surface and the list in it ---- */

/*
        A menu is a popup row of its own (window.c's popup columns), so it reaches past its window's edges and the
        compositor places it, flipping it where the screen ends. Its items are labels with an optional accelerator's
        text, a mnemonic marked with & (the letter typed chooses it), check and radio marks, separators, section
        headers and disabled items; a submenu item is answered UI_MENU_OPEN, for the app to open another popup against
        the item with WINDOW_POPUP_RIGHT (the hover delay and the pointer's safe triangle toward it are not built yet).
        The keys while it is open: Up and Down (Home and End) move over what can be chosen, Enter and Space choose,
        Right answers a submenu's item and Left closes, Escape closes, letters jump to the first item they
        start (a mnemonic chooses). Rows are 28 logical pixels, 44 when a finger opened it; a menu taller than its
        screen scrolls with the wheel and the keys. Colours are the kit's: the surface container, the selection's
        layer for the item under the hand, the outline between groups.
*/
#define UI_MENU_ITEMS 40
#define UI_MENU_SEPARATOR 1u
#define UI_MENU_DISABLED 2u
#define UI_MENU_CHECK 4u   // a check mark when UI_MENU_ON
#define UI_MENU_RADIO 8u   // a dot when UI_MENU_ON
#define UI_MENU_ON 16u
#define UI_MENU_HEADER 32u
#define UI_MENU_SUBMENU 64u // id names the submenu the app opens for it
#define UI_MENU_ROW 28
#define UI_MENU_TOUCH_ROW 44
#define UI_MENU_PAD 6
#define UI_MENU_HOVER_NS 300000000ull

struct ui_menu_item
{
        const char address_to label, address_to keys;
        p32 flags;
        int id;
};

struct ui_popmenu
{
        struct ui_menu_item items[UI_MENU_ITEMS];
        int count, hot, top, rows_shown;
        b32 touch, pressed;
        int width, height;
        char typed[16];
        int typed_length;
        positive typed_at;
};

static fn ui_popmenu_add(struct ui_popmenu address_to m, const char address_to label, const char address_to keys, p32 flags, int id)
{
        if (m->count < UI_MENU_ITEMS)
                m->items[m->count++] = (struct ui_menu_item){label, keys, flags, id};
}

static b32 ui_popmenu_choosable(const struct ui_popmenu address_to m, int at)
{
        return at >= 0 && at < m->count && !(m->items[at].flags & (UI_MENU_SEPARATOR | UI_MENU_DISABLED | UI_MENU_HEADER));
}

static int ui_popmenu_row(const struct ui address_to ui, const struct ui_popmenu address_to m)
{
        return (m->touch ? UI_MENU_TOUCH_ROW : UI_MENU_ROW) * ui->scale;
}

// A label as drawn: its & left out (and && one &).
static positive ui_popmenu_shown(const char address_to label, p8 address_to into, positive room, p32 address_to mnemonic)
{
        positive out = 0;

        *mnemonic = 0;
        for (positive at = 0; label && label[at] && out + 1 < room; at++)
        {
                if (label[at] == '&' && label[at + 1] && label[at + 1] != '&')
                {
                        *mnemonic = (p32)byte_to_lower((b8)label[at + 1]);
                        continue;
                }
                if (label[at] == '&')
                        at++;
                into[out++] = (p8)label[at];
        }
        into[out] = 0;
        return out;
}

// How big the menu is drawn (device pixels), and how many rows show in at most limit pixels of height.
static fn ui_popmenu_size(struct ui address_to ui, struct ui_popmenu address_to m, int limit)
{
        int widest = 0, height = 2 * UI_MENU_PAD * ui->scale;

        for (int at = 0; at < m->count; at++)
        {
                p8 shown[96];
                p32 mnemonic;
                int wide;

                if (m->items[at].flags & UI_MENU_SEPARATOR)
                {
                        height += (2 * UI_MENU_PAD + 1) * ui->scale;
                        continue;
                }
                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                wide = ui_kit_measure(ui, shown, UI_BODY_MEDIUM) + (m->items[at].keys ? ui_kit_measure(ui, (string_address)m->items[at].keys, UI_BODY_MEDIUM) + 32 * ui->scale : 0);
                widest = wide > widest ? wide : widest;
                height += ui_popmenu_row(ui, m);
        }
        m->width = widest + 2 * (UI_MENU_PAD + 30) * ui->scale;
        m->height = limit > 0 && height > limit ? limit : height;
        m->rows_shown = (m->height - 2 * UI_MENU_PAD * ui->scale) / ui_popmenu_row(ui, m);
}

// The item at y (device pixels from the menu's top), or -1. A row that starts in the bottom padding is not drawn (the draw
// stops there), so the padding under the last row drawn is no item.
static int ui_popmenu_at(const struct ui address_to ui, const struct ui_popmenu address_to m, int y)
{
        int top = UI_MENU_PAD * ui->scale;

        for (int at = m->top; at < m->count; at++)
        {
                int tall = m->items[at].flags & UI_MENU_SEPARATOR ? (2 * UI_MENU_PAD + 1) * ui->scale : ui_popmenu_row(ui, m);

                if (y >= top && y < top + tall)
                        return (m->items[at].flags & UI_MENU_SEPARATOR) || top >= m->height - UI_MENU_PAD * ui->scale ? -1 : at;
                top += tall;
        }
        return -1;
}

static fn ui_popmenu_draw(struct ui address_to ui, struct ui_popmenu address_to m)
{
        struct ui_box face = ui_box_of(ui, 0, 0, ui_q(m->width / ui->scale), ui_q(m->height / ui->scale), ui_q(8));
        int y = UI_MENU_PAD * ui->scale, row = ui_popmenu_row(ui, m);

        // A glass menu has no face and no outline: what is under it is the face, and its inks are the ground's (flat).
        if (!ui->glass)
        {
                ui_kit_fill(ui, &face, ui_role(UI_SURFACE_CONTAINER), null);
                ui_kit_outline(ui, &face, ui->scale, ui_role(UI_OUTLINE_VARIANT));
        }
        for (int at = m->top; at < m->count && y < m->height - UI_MENU_PAD * ui->scale; at++)
        {
                const struct ui_menu_item address_to item = &m->items[at];
                unsigned int ink = ui_text_colour(ui, UI_ON_SURFACE);
                p8 shown[96];
                p32 mnemonic;
                int baseline = y + (row - ui_capital(ui, 14)) / 2 + ui_capital(ui, 14);
                unsigned int alpha = item->flags & UI_MENU_DISABLED ? 97 : 255;

                if (item->flags & UI_MENU_SEPARATOR)
                {
                        ui_fill(ui, UI_MENU_PAD * ui->scale, y + UI_MENU_PAD * ui->scale, m->width - 2 * UI_MENU_PAD * ui->scale, ui->scale,
                                ui_tone(ui->glass ? ui->ink & 0xffffffu : ui_role(UI_OUTLINE_VARIANT) & 0xffffffu, ui->glass ? 40 : 255));
                        y += (2 * UI_MENU_PAD + 1) * ui->scale;
                        continue;
                }
                if (at == m->hot && ui_popmenu_choosable(m, at))
                {
                        struct ui_box lit = ui_box_of(ui, ui_q(UI_MENU_PAD), ui_q(y / ui->scale), ui_q((m->width - 2 * UI_MENU_PAD * ui->scale) / ui->scale),
                                                      ui_q(row / ui->scale), ui_q(6));

                        ui_kit_fill(ui, &lit, ui_well(ui, UI_SELECTION, 40), null);
                }
                ui_popmenu_shown(item->label, shown, sizeof(shown), &mnemonic);
                if (item->flags & UI_MENU_HEADER)
                        ui_kit_text(ui, (UI_MENU_PAD + 12) * ui->scale, baseline, shown, UI_LABEL_MEDIUM, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0);
                else
                        ui_kit_text(ui, (UI_MENU_PAD + 30) * ui->scale, baseline, shown, UI_BODY_MEDIUM, (alpha << 24) | (ink & 0xffffffu), 0);
                if ((item->flags & (UI_MENU_CHECK | UI_MENU_RADIO)) && (item->flags & UI_MENU_ON))
                {
                        int cx = (UI_MENU_PAD + 15) * ui->scale, cy = y + row / 2;

                        if (item->flags & UI_MENU_RADIO)
                                ui_round(ui, cx - 3 * ui->scale, cy - 3 * ui->scale, 6 * ui->scale, 6 * ui->scale, 3 * ui->scale, ui_tone(ink & 0xffffffu, alpha));
                        else
                                ui_kit_text(ui, cx - 5 * ui->scale, baseline, (string_address) "\xe2\x9c\x93", UI_BODY_MEDIUM, (alpha << 24) | (ink & 0xffffffu), 0);
                }
                if (item->keys)
                        ui_kit_text(ui, m->width - (UI_MENU_PAD + 14) * ui->scale - ui_kit_measure(ui, (string_address)item->keys, UI_BODY_MEDIUM), baseline,
                                    (string_address)item->keys, UI_BODY_MEDIUM,
                                    (alpha << 24) | ((ui->glass && at == m->hot ? ink : ui_text_colour(ui, UI_ON_SURFACE_VARIANT)) & 0xffffffu), 0);
                if (item->flags & UI_MENU_SUBMENU)
                        ui_kit_text(ui, m->width - (UI_MENU_PAD + 14) * ui->scale, baseline, (string_address) "\xe2\x80\xba", UI_BODY_MEDIUM,
                                    (alpha << 24) | (ink & 0xffffffu), 0);
                y += row;
        }
}

// The hot item moved by step over what can be chosen (wrapping), the view following it. With none hot (-1) Down takes the
// first that can be chosen and Up the last; with nothing that can be chosen the view stays at the top.
static fn ui_popmenu_step(struct ui_popmenu address_to m, int step)
{
        for (int tries = 0, at = m->hot >= 0 ? m->hot : step > 0 ? -1 : m->count; tries < m->count; tries++)
        {
                at = (at + step + m->count) % m->count;
                if (ui_popmenu_choosable(m, at))
                {
                        m->hot = at;
                        break;
                }
        }
        if (m->hot < m->top)
                m->top = m->hot < 0 ? 0 : m->hot;
        if (m->rows_shown > 0 && m->hot >= m->top + m->rows_shown)
                m->top = m->hot - m->rows_shown + 1;
}

/*
        An event of the menu's row: the id of the item chosen, UI_MENU_NOTHING, UI_MENU_GONE (Escape, Left at the top,
        the compositor asking it to go), or UI_MENU_OPEN for a submenu's item (its id in hot's item). x and y are the
        menu's own device pixels.
*/
#define UI_MENU_NOTHING (-1)
#define UI_MENU_GONE (-2)
#define UI_MENU_OPEN (-3)

static int ui_popmenu_event(struct ui address_to ui, struct ui_popmenu address_to m, const struct window_event address_to event, int x, int y)
{
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN))
        {
                p32 c = (p32)event->b;

                switch (event->a)
                {
                case 103: ui_popmenu_step(m, -1); return UI_MENU_NOTHING;
                case 108: ui_popmenu_step(m, 1); return UI_MENU_NOTHING;
                case 102: m->hot = m->count - 1; ui_popmenu_step(m, 1); return UI_MENU_NOTHING;
                case 107: m->hot = 0; ui_popmenu_step(m, -1); return UI_MENU_NOTHING;
                case 1: case 105: return UI_MENU_GONE;
                case 106:
                        return ui_popmenu_choosable(m, m->hot) && (m->items[m->hot].flags & UI_MENU_SUBMENU) ? UI_MENU_OPEN : UI_MENU_NOTHING;
                case 28: case 96: case 57:
                        if (!ui_popmenu_choosable(m, m->hot))
                                return UI_MENU_NOTHING;
                        return m->items[m->hot].flags & UI_MENU_SUBMENU ? UI_MENU_OPEN : m->items[m->hot].id;
                }
                if (c > ' ' && c < 127)
                {
                        positive now = clock_monotonic_nanoseconds();
                        p8 shown[96];
                        p32 mnemonic;

                        if (now - m->typed_at > 1000000000ull || m->typed_length >= (int)sizeof(m->typed) - 1)
                                m->typed_length = 0;
                        m->typed[m->typed_length++] = (char)byte_to_lower((b8)c);
                        m->typed_at = now;
                        // A mnemonic chooses at once; else the first item the letters typed start.
                        for (int at = 0; at < m->count; at++)
                        {
                                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                                if (ui_popmenu_choosable(m, at) && mnemonic == byte_to_lower((b8)c) && m->typed_length == 1)
                                {
                                        m->hot = at;
                                        return m->items[at].flags & UI_MENU_SUBMENU ? UI_MENU_OPEN : m->items[at].id;
                                }
                        }
                        for (int at = 0; at < m->count; at++)
                        {
                                positive k = 0;

                                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                                while (k < (positive)m->typed_length && shown[k] && byte_to_lower((b8)shown[k]) == m->typed[k])
                                        k++;
                                if (ui_popmenu_choosable(m, at) && k == (positive)m->typed_length)
                                {
                                        m->hot = at;
                                        break;
                                }
                        }
                }
                return UI_MENU_NOTHING;
        }
        if (event->type == WINDOW_EVENT_MOTION)
        {
                int at = x >= 0 && x < m->width ? ui_popmenu_at(ui, m, y) : -1;

                m->hot = at;
                return UI_MENU_NOTHING;
        }
        if (event->type == WINDOW_EVENT_WHEEL && !event->b)
        {
                m->top += event->a < 0 ? 1 : -1;
                m->top = m->top < 0 ? 0 : m->top > m->count - m->rows_shown ? (m->count - m->rows_shown > 0 ? m->count - m->rows_shown : 0) : m->top;
                return UI_MENU_NOTHING;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a >= 0x110 && event->a <= 0x112)
        {
                int at = x >= 0 && x < m->width ? ui_popmenu_at(ui, m, y) : -1;

                m->touch |= (event->flags & WINDOW_KEY_TOUCH) != 0;
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        m->pressed = true;
                        m->hot = at;
                        return UI_MENU_NOTHING;
                }
                // A release on an item chooses it: after a press on it, or at the end of a drag from the button that
                // opened the menu (press, drag, release).
                if (event->flags & WINDOW_KEY_CANCEL)
                        return UI_MENU_NOTHING;
                if (ui_popmenu_choosable(m, at))
                        return m->items[at].flags & UI_MENU_SUBMENU ? UI_MENU_OPEN : m->items[at].id;
        }
        return UI_MENU_NOTHING;
}

/*
        A popup an app opens: its own row of the table, a picture of its own drawn with the app's engine and faces, and
        its menu. ui_popup_open takes the row (as a popup of the app's window, at an anchor in the window's pixels),
        ui_popup_pump reads its events and answers what was chosen, and ui_popup_close leaves the row. The app's runner
        waits on its wake too while it is open (ui_app.popup).
*/
struct ui_popup
{
        struct window_row own;
        struct ui ui;
        struct ui_popmenu menu;
        b32 open;
        int shown, x, y;
        struct ui_text address_to field; // the text widget its menu was opened for (ui_text_menu_open), or none
};

static fn ui_popup_present(struct ui_popup address_to p)
{
        struct windows address_to table = p->own.table;
        unsigned int row = p->own.row;
        int slot = p->shown < 0 ? 0 : 1 - p->shown;

        if (p->shown >= 0 && !windows_ready(&p->own))
                return;
        ui_begin(&p->ui, windows_pixels(&p->own, (unsigned int)slot), p->own.stride, p->menu.width, p->menu.height);
        ui_popmenu_draw(&p->ui, &p->menu);
        table->width[row] = (unsigned int)p->menu.width;
        table->height[row] = (unsigned int)p->menu.height;
        table->damage_width[row] = 0;
        windows_show(&p->own, (unsigned int)slot);
        p->shown = slot;
}

/*
        A menu's look: it is glass, a temporary overlay, so its face is what is under it and its inks are that ground's. The
        compositor gives the ground as it composes the menu (ui_popup_pump takes it again as it comes); until then the
        parent's will do. The row is glass the same way (WINDOW_GLASS, window.c), and no close button.
*/
#define UI_POPUP_GLASS (WINDOW_NO_CLOSE | WINDOW_GLASS)

static void ui_popup_look(struct ui_popup address_to p, struct windows address_to table, const struct ui address_to parent_ui)
{
        unsigned int ground = table->ground[p->own.row];

        p->ui = *parent_ui;
        p->ui.opaque = false, p->ui.glass = true;
        ui_inks(&p->ui, ground ? ground : parent_ui->ground ? parent_ui->ground : UI_GLASS_GROUND);
        table->glass[p->own.row] = UI_POPUP_GLASS;
}

static b32 ui_popup_open(struct ui_popup address_to p, const struct window_row address_to parent, const struct ui address_to parent_ui,
                         int x, int y, int width, int height, unsigned int flags, b32 touch)
{
        struct windows address_to table;
        int screen_x, screen_y, screen_w, screen_h;

        if (p->open)
                windows_leave(&p->own);
        p->open = false;
        if (windows_take(&p->own) < 0)
                return false;
        table = p->own.table;
        // A popup opens in the theme as it is now. One changed while its window slept is taken here (the window's
        // own loop still turns to it after, as ever), and a turn under way is taken to its end: the terminal's menu
        // opened in the old theme, its loop reading the new one only after the press that opened it.
        if (__atomic_load_n(&table->themed, __ATOMIC_ACQUIRE) != ui_kit.themed)
                ui_kit_theme(table->theme, table->accent, table->motion);
        ui_kit.shown = ui_kit.to;
        ui_popup_look(p, table, parent_ui);
        p->shown = -1;
        p->menu.touch = touch;
        p->menu.hot = -1;
        p->menu.top = 0;
        windows_output(table, 0, &screen_x, &screen_y, &screen_w, &screen_h);
        ui_popmenu_size(&p->ui, &p->menu, screen_h);
        table->popup_parent[p->own.row] = parent->row + 1;
        table->popup_x[p->own.row] = x, table->popup_y[p->own.row] = y;
        table->popup_w[p->own.row] = width, table->popup_h[p->own.row] = height;
        table->popup_flags[p->own.row] = flags;
        string_copy_bounded((string_address)table->title[p->own.row], (string_address) "menu", WINDOWS_TITLE);
        p->open = true;
        ui_popup_present(p);
        return true;
}

static fn ui_popup_close(struct ui_popup address_to p)
{
        if (p->open)
                windows_leave(&p->own);
        p->open = false;
}

// The popup's events: the id chosen (the popup closed), UI_MENU_GONE when it went, UI_MENU_NOTHING otherwise.
static int ui_popup_pump(struct ui_popup address_to p)
{
        struct window_event event;
        int answer = UI_MENU_NOTHING;

        if (!p->open)
                return UI_MENU_NOTHING;
        // The compositor's ground for the menu as it came: its inks follow, and the menu is drawn again in them.
        if ((p->own.table->ground[p->own.row] & 0xffffffu) && (p->own.table->ground[p->own.row] & 0xffffffu) != p->ui.ground)
        {
                ui_inks(&p->ui, p->own.table->ground[p->own.row]);
                ui_popup_present(p);
        }
        while (windows_event(&p->own, &event) && answer == UI_MENU_NOTHING)
        {
                int hot = p->menu.hot;

                if (event.type == WINDOW_EVENT_MOTION)
                        p->x = event.a, p->y = event.b;
                answer = ui_popmenu_event(&p->ui, &p->menu, &event, p->x, p->y);
                if (answer == UI_MENU_OPEN)
                        answer = UI_MENU_NOTHING;
                if (p->menu.hot != hot)
                        ui_popup_present(p);
        }
        if (p->own.table->state[p->own.row] & WINDOW_CLOSING)
                answer = UI_MENU_GONE;
        if (answer != UI_MENU_NOTHING)
                ui_popup_close(p);
        return answer;
}

/* ---- the kit's composites: what a window is made of ---- */

/*
        A window of the kit is a page: its title at the top left, a search
        field at the title's right when it has one, a column of groups at
        the left when it has one (the kit's tabs stood on end), the body (a
        list of rows in sections, or a page of text), and the status line at
        its foot; and over all of it, when a row opened one, a menu of a
        choice's words. An app says what it shows -- the rows, each one's
        label and value and what kind of value it is, the groups, the words
        of a choice, the status -- and the kit does the rest: every size, the
        type, the theme's colours, the hover and the press and its ripple
        edge to edge, the row the keys are on and their ring, a finger's
        reach, a scrollbar on whatever scrolls, the wheel, a finger's drag
        and throw, the keys that move in a list, a column or a menu, and a
        value placed at its first frame and moved only when it changes.

        A list's row is one of:

            plain       its value quiet at the right
            choice      its value in the ink and a chevron: it opens a menu
            step        its value between Left and Right marks
            switch      the kit's switch
            tabs        the kit's segmented tabs, one for each of its words
            swatches    a colour for each of its words, the chosen ringed
            field       the kit's field, typed in (as dots when masked)

        and any row may have a quiet note before its value, the bars of a
        signal at its edge and a mark before its label (the one in use).
        Sizes are logical pixels, times the window's scale.
*/
#define UI_GAP1 4            // the kit's gaps ([gap1] to [gap4]): between pieces that go together,
#define UI_GAP2 8            // between a field and what it lists,
#define UI_GAP3 12
#define UI_GAP4 16           // and between groups
#define UI_PAGE_TOP 60       // a page's title band
#define UI_PAGE_FOOT 40      // its status line's band
#define UI_PAGE_INSET 6      // the title and the status line in from the margin
#define UI_SEARCH_W 190      // a page's search field, at the title's right
#define UI_SEARCH_H 28
#define UI_SEARCH_Y 24
#define UI_COLUMN_SHARE 30   // a page's column: this share of the window,
#define UI_COLUMN_LEAST 88   // no narrower than this, no wider than the most, and never more than half of the window
#define UI_COLUMN_MOST 170
#define UI_COLUMN_GUTTER 14  // between the column and the body
#define UI_COLUMN_PAGE 3     // Page Up and Page Down in a column move three groups
#define UI_SECTION 34        // a section's header over its first row
#define UI_SECTION_GAP 10    // under its last row
#define UI_ROW_FIELD_W 220   // a field in a row: at most this wide, and at least the least
#define UI_ROW_FIELD_LEAST 100
#define UI_MENU_LEAST 140    // the narrowest a choice's menu is (its rows and pad are the menus', UI_MENU_ROW and UI_MENU_PAD)
#define UI_LINE 20           // a line of a page of text: body-medium's line
#define UI_LINES_PAD 10

// The keys the kit moves by (the evdev codes the compositor sends in a key's a).
#define UI_KEY_ESCAPE 1
#define UI_KEY_ENTER 28
#define UI_KEY_SPACE 57
#define UI_KEY_HOME 102
#define UI_KEY_UP 103
#define UI_KEY_PAGE_UP 104
#define UI_KEY_END 107
#define UI_KEY_DOWN 108
#define UI_KEY_PAGE_DOWN 109
#define UI_KEY_ENTER_PAD 96

/*
        A text cut to room at a role: the longest start, ending before a whole
        character, that fits with three dots after it, or the text itself when
        it fits. A cut is kept with what it was made for, so a text drawn again
        is not measured again; the memo is mapped at the first cut.
*/
#define UI_CUTS 128
#define UI_CUT_TEXT 128

struct ui_cut
{
        char text[UI_CUT_TEXT];
        positive length, cut, capacity;
        int type, room, scale;
        unsigned char kind; // 1 whole, 2 the first cut bytes and the dots, 3 the first cut bytes alone
};

static struct ui_cut address_to ui_cuts;

static unsigned char ui_cut_find(struct ui address_to ui, string_address text, positive length, int type, int room, char address_to fitted,
                                 positive capacity, positive address_to cut)
{
        positive n = length < capacity ? length : capacity - 1, low = 1, high, best = 0;

        if (ui_kit_measure(ui, text, type) <= room)
                return *cut = length, 1;
        if (n <= 1)
                return *cut = n, 3;
        high = n - 1;
        while (low <= high)
        {
                positive mid = (low + high) / 2, at = mid;

                while (at > 1 && (((unsigned char)text[at]) & 0xc0) == 0x80)
                        at--;
                memory_copy(fitted, text, at);
                fitted[at] = 0;
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
                if (ui_kit_measure(ui, (string_address)fitted, type) <= room)
                        best = at, low = mid + 1;
                else
                        high = mid - 1;
        }
        if (best)
                return *cut = best, 2;
        return *cut = 1, 3;
}

static string_address ui_fit(struct ui address_to ui, string_address text, int type, int room, char address_to fitted, positive capacity)
{
        positive length = string_length(text), cut = 0;
        unsigned char kind = 0;
        struct ui_cut address_to memo = null;

        if (!ui_cuts)
        {
                long mapped = ui_map(sizeof(struct ui_cut) * UI_CUTS);

                ui_cuts = mapped < 0 && mapped > -4096 ? null : (struct ui_cut address_to)mapped;
        }
        if (ui_cuts && length < UI_CUT_TEXT)
        {
                unsigned int hash = 2166136261u;

                for (positive at = 0; at < length; at++)
                        hash = (hash ^ (unsigned char)text[at]) * 16777619u;
                hash = (((hash ^ (unsigned int)type) * 16777619u ^ (unsigned int)room) * 16777619u ^ (unsigned int)ui->scale) * 16777619u;
                memo = &ui_cuts[(hash ^ hash >> 15) & (UI_CUTS - 1)];
                if (memo->kind && memo->length == length && memo->type == type && memo->room == room && memo->scale == ui->scale &&
                    memo->capacity == capacity && !memory_compare(memo->text, text, length))
                        kind = memo->kind, cut = memo->cut;
        }
        if (!kind)
        {
                kind = ui_cut_find(ui, text, length, type, room, fitted, capacity, &cut);
                if (memo)
                {
                        memory_copy(memo->text, text, length);
                        memo->length = length, memo->type = type, memo->room = room, memo->scale = ui->scale;
                        memo->capacity = capacity, memo->cut = cut, memo->kind = kind;
                }
        }
        if (kind == 1)
                return text;
        memory_copy(fitted, text, cut);
        fitted[cut] = 0;
        if (kind == 2)
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
        return (string_address)fitted;
}

// A control's items, the words of words a blank apart, kept in its text: as many as fit, UI_ITEMS at most.
static fn ui_items_words(struct ui_control address_to c, const char address_to words)
{
        char address_to into = c->text;
        positive left = sizeof(c->text);

        c->count = 0;
        while (words && *words && c->count < UI_ITEMS)
        {
                positive length = 0;

                while (*words == ' ')
                        words++;
                while (words[length] && words[length] != ' ')
                        length++;
                if (!length || length + 1 > left)
                        break;
                memory_copy(into, words, length);
                into[length] = 0;
                c->items[c->count++] = into;
                into += length + 1, left -= length + 1;
                words += length;
        }
}

/*
        The bars of a signal, its right edge at right and their middle on middle: four, as many in the ink as bars says
        and the rest in the outline.
*/
static fn ui_signal(struct ui address_to ui, int right, int middle, int bars)
{
        int width = ui_dev(ui, 3), gap = ui_dev(ui, 2), bottom = middle + ui_dev(ui, 7);

        for (int bar = 0; bar < 4; bar++)
        {
                int height = ui_dev(ui, 4 + 3 * bar), x = right - 4 * width - 3 * gap + bar * (width + gap);
                struct ui_box box = {x * 256, (bottom - height) * 256, (x + width) * 256, bottom * 256, ui_q8(ui, 1), ui_q8(ui, 1)};

                ui_kit_fill(ui, &box, bar < bars ? ui_role(UI_ON_SURFACE) : ui_role(UI_OUTLINE_VARIANT), null);
        }
}

/*
        A chevron pointing down, centred on cx, cy, size to a side: two strokes of the scale's width, each pixel's cover
        exact, in the colour.
*/
static fn ui_chevron(struct ui address_to ui, int cx, int cy, int size, unsigned int argb)
{
        b32 thick = ui_q8(ui, 1) + 128;

        for (int step = 0; step <= size; step++)
        {
                b32 y = (cy - size / 2 + step) * 256;

                ui_kit_fill(ui, &(struct ui_box){(cx - size + step) * 256, y, (cx - size + step) * 256 + thick, y + ui_q8(ui, 1), 0, 0}, argb, null);
                ui_kit_fill(ui, &(struct ui_box){(cx + size - step + 1) * 256 - thick, y, (cx + size - step + 1) * 256, y + ui_q8(ui, 1), 0, 0}, argb, null);
        }
}

/* ---- a list of rows in sections ---- */

#define UI_LIST_MOST 256 // the rows a list shows at once
#define UI_LIST_STARTS 1 // a row is its section's first
#define UI_LIST_HEADED 2 // and its section has a header over it

#define UI_ROW_PLAIN 0
#define UI_ROW_CHOICE 1
#define UI_ROW_STEP 2
#define UI_ROW_SWITCH 3
#define UI_ROW_TABS 4
#define UI_ROW_SWATCHES 5
#define UI_ROW_FIELD 6

// What a row shows, said by the app whenever the kit asks (ui_list.describe); what is not said is none.
struct ui_row
{
        const char address_to label;       // at the left
        const char address_to value;       // at the right
        const char address_to note;        // a quiet word before the value: what the row is, where it is from
        const char address_to words;       // a tabs row's choices, a swatches row's colours (rrggbb): words a blank apart
        const char address_to header;      // the header of its section, over the section's first row
        struct ui_text address_to edit;    // a field's engine (its text, dots when masked, its caret and its rules)
        const char address_to placeholder; // and what it says while empty
        int section;                       // the section it is in: a header where it changes
        int kind;                          // UI_ROW_*
        int at;                            // a tabs or a swatches row's choice now, -1 none
        int bars;                          // the bars of a signal at its edge, 1 to 4 (0 none)
        b32 on;                            // a switch's
        b32 dim;                           // the label and the value quiet: a note, a value that is not there
        b32 mark;                          // a dot before the label: the one in use
        b32 head;                          // a field right after the label, to the row's end (the words after a verb)
};

struct ui_list
{
        // The app's: the row (its own number) each place shows, in order, how it is described, and where a change must
        // be drawn again (rows of the window, device pixels).
        unsigned short id[UI_LIST_MOST];
        int count;
        int ids;                           // how many rows the app numbers: each has its controls while the window is open
        void (*describe)(int id, struct ui_row address_to row);
        fn (*damaged)(int top, int bottom);
        const char address_to empty;       // what it says when it shows nothing
        b32 lit;                           // the row the keys are on is shown (the keys are in it, or a search lists it)
        b32 keys;                          // and ringed: the keys are in it and the window has them
        int focus;                         // the row the keys are on, -1 none
        // The kit's.
        int top[UI_LIST_MOST];             // each place's top, from the list's
        unsigned char starts[UI_LIST_MOST]; // each place's first of a section, and with a header (UI_LIST_STARTS, _HEADED)
        int content;
        int x, y, w, h;                    // where it is in the window, device pixels
        struct ui_scroll scroll;
        int hover, down;                   // the row under the pointer and the row pressed, -1 none
        int part, way;                     // a press's: the item of a tabs or swatches row it was on (-1 none), a step's way
        int pointer_x, pointer_y;
        struct ui_control address_to controls; // two to a row: the row's own (or its switch) and its trailing control
};

static fn ui_list_describe(const struct ui_list address_to l, int id, struct ui_row address_to row)
{
        memory_zero(row, sizeof(*row));
        row->at = -1;
        l->describe(id, row);
}

// The rows' controls, mapped at the first draw: what is never drawn is never touched.
static struct ui_control address_to ui_list_control(struct ui_list address_to l, int id, int which)
{
        if (!l->controls && l->ids > 0)
        {
                long mapped = ui_map((positive)l->ids * 2 * sizeof(struct ui_control));

                l->controls = mapped < 0 && mapped > -4096 ? null : (struct ui_control address_to)mapped;
        }
        return l->controls && id >= 0 && id < l->ids ? &l->controls[2 * id + which] : null;
}

// Where each row is: a header over the first of a section that has one, a gap under the last of each.
static fn ui_list_layout(const struct ui address_to ui, struct ui_list address_to l)
{
        int y = 0, section = 0;

        for (int at = 0; at < l->count; at++)
        {
                struct ui_row row;

                ui_list_describe(l, l->id[at], &row);
                l->starts[at] = !at || row.section != section ? UI_LIST_STARTS | (row.header ? UI_LIST_HEADED : 0) : 0;
                if (at && l->starts[at])
                        y += ui_dev(ui, UI_SECTION_GAP);
                if (l->starts[at] & UI_LIST_HEADED)
                        y += ui_dev(ui, UI_SECTION);
                section = row.section;
                l->top[at] = y;
                y += ui_dev(ui, UI_ROW);
        }
        // A list in sections has the gap under its last one too; a list of rows alone ends with its last row.
        y += l->count && (l->starts[0] & UI_LIST_HEADED) ? ui_dev(ui, UI_SECTION_GAP) : 0;
        l->content = y;
        l->scroll.content = y;
        ui_scroll_clamp(&l->scroll);
}

// The list at x, y, w by h (device pixels); its scrollbar in the margin at its right.
static fn ui_list_place(const struct ui address_to ui, struct ui_list address_to l, int x, int y, int w, int h)
{
        l->x = x, l->y = y, l->w = w, l->h = h;
        l->scroll.x = x + w + (ui_dev(ui, UI_PAD) - ui_dev(ui, UI_SCROLL_WIDTH)) / 2;
        l->scroll.y = y, l->scroll.height = h, l->scroll.view = h;
        l->scroll.content = l->content;
        ui_scroll_clamp(&l->scroll);
}

// The place a row is at in the list, or -1.
static int ui_list_at(const struct ui_list address_to l, int id)
{
        for (int at = 0; at < l->count; at++)
                if (l->id[at] == id)
                        return at;
        return -1;
}

// A row's top in the window (device pixels).
static int ui_list_row_y(const struct ui_list address_to l, int at)
{
        return l->y + l->top[at] - l->scroll.scroll;
}

static fn ui_list_damage(const struct ui address_to ui, const struct ui_list address_to l, int id)
{
        int at = id >= 0 ? ui_list_at(l, id) : -1;

        if (at >= 0 && l->damaged)
                l->damaged(ui_list_row_y(l, at), ui_list_row_y(l, at) + ui_dev(ui, UI_ROW));
}

static fn ui_list_damage_all(const struct ui address_to ui, const struct ui_list address_to l)
{
        if (l->damaged)
                l->damaged(l->y - ui_dev(ui, UI_ROW), l->y + l->h + ui_dev(ui, UI_ROW));
}

// The row under a point of the window, or -1.
static int ui_list_hit(const struct ui address_to ui, const struct ui_list address_to l, int x, int y)
{
        int inside = y - l->y + l->scroll.scroll;

        if (x < l->x || x >= l->x + l->w || y < l->y || y >= l->y + l->h)
                return -1;
        for (int at = 0; at < l->count; at++)
                if (inside >= l->top[at] && inside < l->top[at] + ui_dev(ui, UI_ROW))
                        return l->id[at];
        return -1;
}

// The row the keys are on kept in sight, the header with it when it is a section's first: true when the list moved.
static b32 ui_list_reveal(const struct ui address_to ui, struct ui_list address_to l)
{
        int at = ui_list_at(l, l->focus), top, height = ui_dev(ui, UI_ROW);

        if (at < 0)
        {
                ui_scroll_clamp(&l->scroll);
                return false;
        }
        top = l->top[at];
        if (l->starts[at] & UI_LIST_HEADED)
                top -= ui_dev(ui, UI_SECTION), height += ui_dev(ui, UI_SECTION);
        if (!ui_scroll_reveal(&l->scroll, top, height))
                return false;
        ui_list_damage_all(ui, l);
        return true;
}

// The keys moved by whole rows; answers whether they went to another row.
static b32 ui_list_move(const struct ui address_to ui, struct ui_list address_to l, int by)
{
        int at = ui_list_at(l, l->focus), last = l->count - 1, before = l->focus;

        if (last < 0)
                return false;
        at = (at < 0 ? -1 : at) + by; // with no row the keys count from above the first, so Down takes it
        at = at < 0 ? 0 : at > last ? last : at;
        l->focus = l->id[at];
        ui_list_damage(ui, l, before);
        ui_list_damage(ui, l, l->focus);
        ui_list_reveal(ui, l);
        return l->focus != before;
}

// Up, Down, Home, End, Page Up and Page Down in a list (a page is a view of rows less one): true when the key was one.
static b32 ui_list_key(const struct ui address_to ui, struct ui_list address_to l, int key, b32 address_to moved)
{
        int page = l->h / ui_dev(ui, UI_ROW) - 1, by;

        page = page < 1 ? 1 : page;
        switch (key)
        {
        case UI_KEY_UP: by = -1; break;
        case UI_KEY_DOWN: by = 1; break;
        case UI_KEY_HOME: by = -l->count; break;
        case UI_KEY_END: by = l->count; break;
        case UI_KEY_PAGE_UP: by = -page; break;
        case UI_KEY_PAGE_DOWN: by = page; break;
        default: return false;
        }
        *moved = ui_list_move(ui, l, by);
        return true;
}

// A row's trailing control where it has one, made and put in its place at the right edge, in logical pixels.
static struct ui_control address_to ui_list_trailing(struct ui address_to ui, struct ui_list address_to l, int id, const struct ui_row address_to row,
                                                     int right, int y, int room)
{
        struct ui_control address_to c = ui_list_control(l, id, 1);
        unsigned char kind = row->kind == UI_ROW_TABS ? UI_TABS : row->kind == UI_ROW_SWATCHES ? UI_SWATCHES : UI_INPUT;
        int height = ui_dev(ui, UI_ROW);

        if (!c)
                return null;
        if (c->kind != kind)
        {
                memory_zero(c, sizeof(*c));
                c->kind = kind;
                c->variant = kind == UI_TABS ? UI_TABS_SEGMENTED : kind == UI_INPUT ? UI_FIELD_BARE : 0;
                ui_control_start(c);
        }
        if (kind == UI_INPUT)
        {
                // The engine draws and types it; it has the keys while the list has them.
                c->edit = row->edit, c->focused = l->keys;
                c->items[0] = row->placeholder;
                c->y = (y + ui_dev(ui, 6)) / ui->scale, c->h = (height - ui_dev(ui, 12)) / ui->scale;
                c->w = (room > ui_dev(ui, UI_ROW_FIELD_W) && !row->head ? ui_dev(ui, UI_ROW_FIELD_W) : room < ui_dev(ui, UI_ROW_FIELD_LEAST) ? ui_dev(ui, UI_ROW_FIELD_LEAST) : room) / ui->scale;
                c->x = right / ui->scale - c->w;
                return c;
        }
        ui_items_words(c, row->words);
        c->y = (y + height / 2) / ui->scale - 16, c->h = 32;
        if (kind == UI_TABS)
        {
                // The chosen segment: placed at the first frame, the pip moving to another after.
                if (row->at >= 0 && row->at != c->at)
                {
                        if (c->pip_x.placed)
                                ui_tabs_choose(ui, c, row->at);
                        else
                                c->at = row->at;
                }
                c->x = 0;
                c->w = c->count ? ui_tab_left(ui, c, c->count - 1) + ui_tab_width(ui, c, c->count - 1) : 0;
        }
        else
        {
                c->at = row->at;
                c->w = c->count * (UI_SWATCH + UI_SWATCH_GAP) - UI_SWATCH_GAP;
        }
        c->x = right / ui->scale - c->w;
        c->item = ui_control_hit(ui, c, l->pointer_x, l->pointer_y);
        c->hover = c->item >= 0 && l->hover == id;
        return c;
}

/*
        A row at its place, y its top: the row the keys are on in the secondary container (ringed in the accent while
        the keys are in the list), the row's own control (its hover layer and its ripple, edge to edge, or its switch),
        the label at the left cut to leave its value a third of the row, and the value as its kind says at the right.
*/
static fn ui_list_row(struct ui address_to ui, struct ui_list address_to l, int id, int y)
{
        struct ui_row row;
        int x = l->x, w = l->w, height = ui_dev(ui, UI_ROW), inset = ui_dev(ui, UI_INSET), gap = ui_dev(ui, 10);
        int right = x + w - inset, text_left = x + inset, baseline, wide = 0, label_room;
        unsigned int ink = ui_text_colour(ui, UI_ON_SURFACE), quiet = ui_text_colour(ui, UI_ON_SURFACE_VARIANT);
        struct ui_control address_to own = ui_list_control(l, id, 0), address_to trailing = null;
        char cut[UI_CUT_TEXT];
        // Whether any of its rows may be written, with room for a run's rows round its line.
        b32 seen = ui_rows_seen(ui, y - ui_dev(ui, 4), y + height + ui_dev(ui, 4));

        ui_list_describe(l, id, &row);
        baseline = ui_kit_baseline(ui, y + (height - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM);
        if (row.dim)
                ink = quiet;
        if (l->lit && id == l->focus)
        {
                struct ui_box box = {(x + ui_dev(ui, 3)) * 256, (y + ui_dev(ui, 2)) * 256, (x + w - ui_dev(ui, 3)) * 256,
                                     (y + height - ui_dev(ui, 2)) * 256, ui_q8(ui, 6), ui_q8(ui, 6)};

                ui_kit_fill(ui, &box, ui_well(ui, UI_SECONDARY_CONTAINER, 36), null);
                if (l->keys && !ui->glass)
                        ui_kit_outline(ui, &box, ui_q8(ui, 2), ui_role(UI_PRIMARY));
                // A glass row the keys are on is lit by a well of the ink, and the quiet ink stands out from the glass only:
                // its quiet words are the ink too, which stands out from the well (ui_glass_well).
                if (ui->glass)
                        quiet = ink;
        }
        // The row's own control: a switch is its row's (the kit's switch is a row), any other row the list's row.
        if (own)
        {
                unsigned char kind = row.kind == UI_ROW_SWITCH ? UI_SWITCH : UI_LIST_ROW;

                if (own->kind != kind)
                {
                        memory_zero(own, sizeof(*own));
                        own->kind = kind;
                        own->label = "";
                        ui_control_start(own);
                }
                own->row_x = x / ui->scale, own->row_y = y / ui->scale, own->row_w = w / ui->scale, own->row_h = height / ui->scale;
                own->x = own->row_x, own->y = own->row_y, own->w = own->row_w, own->h = own->row_h;
                own->hover = l->hover == id, own->down = l->down == id;
                own->disabled = row.kind == UI_ROW_FIELD;
                if (kind == UI_SWITCH)
                {
                        own->on = row.on;
                        own->x = right / ui->scale - 52, own->w = 52;
                        own->y = (y + height / 2) / ui->scale - 24, own->h = 48;
                }
                ui_control_draw(ui, own);
        }
        if (row.mark)
        {
                int dot = ui_dev(ui, 7);
                struct ui_box box = {text_left * 256, (y + (height - dot) / 2) * 256, (text_left + dot) * 256, (y + (height + dot) / 2) * 256, dot * 128, dot * 128};

                ui_kit_fill(ui, &box, ui_role(UI_PRIMARY), null);
                text_left += ui_dev(ui, 17);
        }
        // The trailing pieces from the right: the signal, then the control or the value, then the note.
        if (row.bars > 0)
        {
                ui_signal(ui, right, y + height / 2, row.bars);
                right -= ui_dev(ui, 18) + gap;
        }
        if (row.kind == UI_ROW_TABS || row.kind == UI_ROW_SWATCHES)
                trailing = ui_list_trailing(ui, l, id, &row, right, y, 0);
        label_room = right - text_left - (row.kind == UI_ROW_SWITCH ? ui_dev(ui, 52) + gap : trailing ? ui_dev(ui, trailing->w) + gap
                                         : row.kind == UI_ROW_FIELD && row.head ? (w - 2 * inset) * 2 / 3 : (w - 2 * inset) / 3);
        // A row no pixel of which may be written (a repaint of other rows) has its controls drawn, which is where its
        // motion is read, and no word measured: its label's width places only its value and a field's well.
        if (row.label && (seen || row.kind == UI_ROW_FIELD))
                wide = ui_kit_text_width(ui, text_left, baseline, ui_fit(ui, (string_address)row.label, UI_BODY_MEDIUM,
                                                                  label_room < ui_dev(ui, 40) ? ui_dev(ui, 40) : label_room, cut, sizeof(cut)),
                                   UI_BODY_MEDIUM, ink, 0);
        if (trailing)
        {
                ui_control_draw(ui, trailing);
                return;
        }
        if (row.kind == UI_ROW_FIELD)
        {
                int room = right - (text_left + wide + gap);

                trailing = ui_list_trailing(ui, l, id, &row, right, y, room);
                if (trailing)
                        ui_control_draw(ui, trailing);
                return;
        }
        if (row.kind == UI_ROW_SWITCH || !seen)
                return;
        if (row.value)
        {
                int room = right - (text_left + wide + ui_dev(ui, 16)), marks = 0, width;
                string_address shown;

                if (row.kind == UI_ROW_CHOICE)
                {
                        int size = ui_dev(ui, 4);

                        ui_chevron(ui, right - size, baseline - ui_dev(ui, 5), size, quiet);
                        right -= 2 * size + ui_dev(ui, 8);
                }
                else if (row.kind == UI_ROW_STEP)
                {
                        marks = ui_kit_measure(ui, (string_address) "\xe2\x80\xba", UI_LABEL_LARGE);
                        ui_kit_text(ui, right - marks, baseline, (string_address) "\xe2\x80\xba", UI_LABEL_LARGE, quiet, 0);
                        right -= marks + ui_dev(ui, 8);
                        room -= 2 * (marks + ui_dev(ui, 8));
                }
                room = row.kind == UI_ROW_CHOICE ? room - ui_dev(ui, 16) : room;
                shown = ui_fit(ui, (string_address)row.value, UI_BODY_MEDIUM, room < ui_dev(ui, 24) ? ui_dev(ui, 24) : room, cut, sizeof(cut));
                width = ui_kit_measure(ui, shown, UI_BODY_MEDIUM);
                ui_kit_text(ui, right - width, baseline, shown, UI_BODY_MEDIUM, row.kind != UI_ROW_PLAIN && !row.dim ? ui_text_colour(ui, UI_ON_SURFACE) : quiet, 0);
                if (row.kind == UI_ROW_STEP)
                        ui_kit_text(ui, right - width - ui_dev(ui, 8) - marks, baseline, (string_address) "\xe2\x80\xb9", UI_LABEL_LARGE, quiet, 0);
                right -= width + (marks ? marks + ui_dev(ui, 8) : 0) + gap;
        }
        if (row.note)
        {
                int width = ui_kit_measure(ui, (string_address)row.note, UI_LABEL_MEDIUM);

                if (right - width > text_left + wide + gap)
                        ui_kit_text(ui, right - width, ui_kit_baseline(ui, y + (height - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM), (string_address)row.note,
                                    UI_LABEL_MEDIUM, row.mark ? ui_text_colour(ui, UI_ON_SURFACE) : quiet, 0);
        }
}

// The list drawn: the headers of the sections in view and their rows, cut to the list's band, and its scrollbar.
static fn ui_list_draw(struct ui address_to ui, struct ui_list address_to l)
{
        int row_height = ui_dev(ui, UI_ROW);

        ui_scroll_glide(ui, &l->scroll);
        ui->top = l->y;
        ui->bottom = l->y + l->h;
        if (!l->count && l->empty)
                ui_kit_text(ui, l->x + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, l->y + (row_height - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM),
                            (string_address)l->empty, UI_BODY_MEDIUM, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0);
        for (int at = 0; at < l->count; at++)
        {
                int y = ui_list_row_y(l, at);

                if ((l->starts[at] & UI_LIST_HEADED) && y > l->y && y - ui_dev(ui, UI_SECTION) < l->y + l->h)
                {
                        struct ui_row row;

                        ui_list_describe(l, l->id[at], &row);
                        ui_kit_text(ui, l->x + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, y - ui_dev(ui, UI_SECTION) + ui_dev(ui, 8), UI_TITLE_SMALL),
                                    (string_address)row.header, UI_TITLE_SMALL, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0);
                }
                if (y + row_height > l->y && y < l->y + l->h)
                        ui_list_row(ui, l, l->id[at], y);
        }
        ui_scroll_draw(ui, &l->scroll);
}

/*
        The pointer on a list. Motion lights the row under it; a press is on a row (the keys go to it, it shows pressed
        and ripples from the press, edge to edge), and the release lets it go. A press answers its row, with part the
        item of a tabs or swatches row it was on (-1 none) and way -1 when it was on the left half of a step's value;
        everything else answers -1. A finger's press shows pressed the same way (touch), and is done when the app says.
*/
static fn ui_list_let_go(const struct ui address_to ui, struct ui_list address_to l)
{
        struct ui_control address_to own = ui_list_control(l, l->down, 0), address_to trailing = ui_list_control(l, l->down, 1);

        if (l->down < 0)
                return;
        if (own)
                ui_kit_ripples_exit(own);
        if (trailing)
                ui_kit_ripples_exit(trailing);
        ui_list_damage(ui, l, l->down);
        l->down = -1;
}

static int ui_list_pointer(struct ui address_to ui, struct ui_list address_to l, int type, int x, int y, b32 down)
{
        int id = ui_list_hit(ui, l, x, y);
        struct ui_control address_to own, address_to trailing;

        l->pointer_x = x, l->pointer_y = y;
        if (type == WINDOW_EVENT_MOTION)
        {
                if (id != l->hover)
                {
                        ui_list_damage(ui, l, l->hover);
                        ui_list_damage(ui, l, id);
                        l->hover = id;
                }
                else if (id >= 0 && (trailing = ui_list_control(l, id, 1)) && (trailing->kind == UI_TABS || trailing->kind == UI_SWATCHES))
                        ui_list_damage(ui, l, id); // a segment or a swatch under it
                return -1;
        }
        if (type != WINDOW_EVENT_BUTTON)
                return -1;
        if (!down)
        {
                ui_list_let_go(ui, l);
                return -1;
        }
        ui_list_let_go(ui, l);
        if (id < 0)
                return -1;
        l->part = -1, l->way = 1;
        trailing = ui_list_control(l, id, 1);
        if (trailing && (trailing->kind == UI_TABS || trailing->kind == UI_SWATCHES))
                l->part = ui_control_hit(ui, trailing, x, y);
        {
                struct ui_row row;

                ui_list_describe(l, id, &row);
                if (row.kind == UI_ROW_STEP && x < l->x + l->w - ui_dev(ui, UI_INSET + 40))
                        l->way = -1;
        }
        own = ui_list_control(l, id, 0);
        if (own && l->part < 0)
        {
                ui_spring_stop(&own->held_squish);
                ui_list_row_press(ui, own, x, y);
        }
        ui_list_damage(ui, l, l->focus);
        l->focus = id, l->down = id, l->hover = id;
        ui_list_damage(ui, l, id);
        return id;
}

/* ---- a menu of a choice's words ---- */

#define UI_MENU_MOST 32
#define UI_MENU_WORD 64

#define UI_MENU_KEPT 0   // a key moved in it, or did nothing
#define UI_MENU_CHOSEN 1 // the choice at is the one
#define UI_MENU_CLOSED 2 // closed with nothing chosen

struct ui_menu
{
        int owner;                         // the row it opened from (the app's number), -1 when closed
        char word[UI_MENU_MOST][UI_MENU_WORD];
        int count, at, current;            // its words, the keys' one, the one the row has now (-1 none)
        b32 touch;                         // a finger opened it: its choices are a finger's height
        int widest;                        // the widest word, device pixels
        int x, y, w, h, shown;             // where it is, device pixels, and how many choices show
        int pressed;                       // the choice a finger is on, -1 none
        struct ui_scroll scroll;           // in whole choices
        struct ui_popmenu list;            // the kit's menu it is drawn as
};

static int ui_menu_item_height(const struct ui address_to ui, const struct ui_menu address_to m)
{
        return ui_dev(ui, m->touch ? UI_MENU_TOUCH_ROW : UI_MENU_ROW);
}

static int ui_menu_top(const struct ui address_to ui, const struct ui_menu address_to m)
{
        return m->scroll.scroll / ui_menu_item_height(ui, m);
}

// A menu opened from a row, empty: the app adds its words, the one the row has now marked current.
static fn ui_menu_open(struct ui_menu address_to m, int owner, b32 touch)
{
        m->owner = owner, m->touch = touch;
        m->count = 0, m->at = 0, m->current = -1, m->widest = 0, m->pressed = -1;
        m->scroll.scroll = m->scroll.wheel = m->scroll.left = 0;
        m->scroll.held = m->scroll.hover = 0; // a thumb held when it closed (its release never came here) is not held now
}

static fn ui_menu_add(struct ui address_to ui, struct ui_menu address_to m, const char address_to word, b32 current)
{
        int wide;

        if (m->count >= UI_MENU_MOST)
                return;
        {
                positive out = 0;

                for (positive at = 0; word[at] && out + 2 < UI_MENU_WORD; at++)
                {
                        m->word[m->count][out++] = word[at];
                        if (word[at] == '&')
                                m->word[m->count][out++] = '&';
                }
                m->word[m->count][out] = 0;
        }
        wide = ui_kit_measure(ui, (string_address)word, UI_BODY_MEDIUM);
        m->widest = wide > m->widest ? wide : m->widest;
        if (current)
                m->current = m->at = m->count;
        m->count++;
}

// A word of the menu as it was added (its && the & it was).
static fn ui_menu_word(const struct ui_menu address_to m, int at, char address_to into, positive room)
{
        positive out = 0;

        for (positive k = 0; at >= 0 && at < m->count && m->word[at][k] && out + 1 < room; k++)
        {
                into[out++] = m->word[at][k];
                k += m->word[at][k] == '&' && m->word[at][k + 1] == '&';
        }
        into[out] = 0;
}

/*
        Where the menu is, from its row's band in the window (top to bottom), the edge its right side is at, and the
        window's size: under the row, or over it when there is more room there; with room on neither side for all of
        it, slid up over the row where it fits the window so, and else cut to the larger side, and it scrolls.
*/
static fn ui_menu_place(const struct ui address_to ui, struct ui_menu address_to m, int row_top, int row_bottom, int right, int width,
                        int height)
{
        int item = ui_menu_item_height(ui, m), pad = ui_dev(ui, UI_MENU_PAD), margin = ui_dev(ui, 8);
        int below = height - margin - (row_bottom - ui_dev(ui, 4)), above = row_top + ui_dev(ui, 4) - margin, most;

        most = (below >= above ? below : above) - 2 * pad;
        most = most / item < 1 ? 1 : most / item;
        if (m->count > most && m->count * item + 2 * pad <= height - 2 * margin)
                most = m->count;
        m->shown = m->count < most ? m->count : most;
        m->h = m->shown * item + 2 * pad;
        m->w = m->widest + 2 * ui_dev(ui, UI_MENU_PAD + 30);
        m->w = m->w < ui_dev(ui, UI_MENU_LEAST) ? ui_dev(ui, UI_MENU_LEAST) : m->w;
        m->w = m->w > width - 2 * margin ? width - 2 * margin : m->w;
        m->x = right - m->w;
        m->x = m->x < margin ? margin : m->x;
        if (m->h <= below)
                m->y = row_bottom - ui_dev(ui, 4);
        else if (m->h <= above)
                m->y = row_top + ui_dev(ui, 4) - m->h;
        else
        {
                m->y = height - margin - m->h;
                m->y = m->y < margin ? margin : m->y;
        }
        m->scroll.unit = item;
        m->scroll.content = m->count * item;
        m->scroll.view = m->shown * item;
        m->scroll.x = m->x + m->w - ui_dev(ui, UI_SCROLL_WIDTH);
        m->scroll.y = m->y + pad, m->scroll.height = m->h - 2 * pad;
        ui_scroll_clamp(&m->scroll);
        // The keys' choice stays in view.
        if (m->at < ui_menu_top(ui, m))
                m->at = ui_menu_top(ui, m);
        else if (m->at >= ui_menu_top(ui, m) + m->shown)
                m->at = ui_menu_top(ui, m) + m->shown - 1;
}

// The keys on a choice, kept in view.
static fn ui_menu_go(const struct ui address_to ui, struct ui_menu address_to m, int at)
{
        int item = ui_menu_item_height(ui, m);

        m->at = at < 0 ? 0 : at >= m->count ? m->count - 1 : at;
        ui_scroll_reveal(&m->scroll, m->at * item, item);
}

// The choice under a point of the window, or -1.
static int ui_menu_item(const struct ui address_to ui, const struct ui_menu address_to m, int x, int y)
{
        int pad = ui_dev(ui, UI_MENU_PAD), at;

        if (m->owner < 0 || x < m->x || x >= m->x + m->w || y < m->y + pad || y >= m->y + m->h - pad)
                return -1;
        at = ui_menu_top(ui, m) + (y - m->y - pad) / ui_menu_item_height(ui, m);
        return at < m->count ? at : -1;
}

/*
        A key with the menu open (it has every key): Up, Down, Home, End, Page Up and Page Down move in it, a letter or a
        digit goes to the next word that starts with it, Enter or Space chooses, Escape closes.
*/
static int ui_menu_key(const struct ui address_to ui, struct ui_menu address_to m, int key, unsigned int c, unsigned int flags)
{
        switch (key)
        {
        case UI_KEY_ESCAPE: return UI_MENU_CLOSED;
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
        case UI_KEY_SPACE: return UI_MENU_CHOSEN;
        case UI_KEY_UP: ui_menu_go(ui, m, m->at - 1); return UI_MENU_KEPT;
        case UI_KEY_DOWN: ui_menu_go(ui, m, m->at + 1); return UI_MENU_KEPT;
        case UI_KEY_HOME: ui_menu_go(ui, m, 0); return UI_MENU_KEPT;
        case UI_KEY_END: ui_menu_go(ui, m, m->count - 1); return UI_MENU_KEPT;
        case UI_KEY_PAGE_UP: ui_menu_go(ui, m, m->at - (m->shown > 0 ? m->shown : 1)); return UI_MENU_KEPT;
        case UI_KEY_PAGE_DOWN: ui_menu_go(ui, m, m->at + (m->shown > 0 ? m->shown : 1)); return UI_MENU_KEPT;
        }
        if (c > ' ' && c < 0x7f && !(flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)))
        {
                c = c >= 'A' && c <= 'Z' ? c + 32 : c;
                for (int step = 1; step <= m->count; step++)
                {
                        int at = (m->at + step) % m->count;
                        unsigned char first = (unsigned char)m->word[at][0];

                        if ((first >= 'A' && first <= 'Z' ? first + 32u : first) == c)
                        {
                                ui_menu_go(ui, m, at);
                                break;
                        }
                }
        }
        return UI_MENU_KEPT;
}

/*
        The menu over everything, as the kit's menus are drawn (ui_popmenu_draw, into the picture at the menu's place):
        the surface container, 8 round with a hairline, the keys' choice in the selection's layer, the row's own one
        dotted as a radio is; a finger's choice pressed, and the scrollbar when not all of it shows.

        Which menus are glass is decided by where they are drawn. A popup (ui_popup_open: a context menu, a choice
        dropdown that opens from a row as its own window) is a temporary overlay, so it is glass (UI_POPUP_GLASS) and has
        no face and no outline. This menu, ui_menu, is drawn inside its window's own picture: the settings window's
        choice dropdowns stay open in a window that stays, and that window is the kit's opaque surface. Making this menu
        glass would make the settings picture transparent under it, so it keeps the surface's face and outline.
*/
// The menu's view moves a frame at a time. The choice the keys are on is kept in the view it is going to (a reveal
// goes to the choice, so it stays; a wheel's turn goes past it, and the choice is put in the view it is turning to).
static b32 ui_menu_glide(const struct ui address_to ui, struct ui_menu address_to m)
{
        b32 moved = ui_scroll_glide(ui, &m->scroll);

        if (m->count > 0 && m->shown > 0)
        {
                int item = ui_menu_item_height(ui, m);
                int top = (int)((m->scroll.gliding ? m->scroll.aim : (b64)m->scroll.scroll << 8) / UI_SCROLL_SCALE) / item;

                m->at = m->at < top ? top : m->at >= top + m->shown ? top + m->shown - 1 : m->at;
        }
        return moved;
}

static fn ui_menu_draw(struct ui address_to ui, struct ui_menu address_to m)
{
        struct ui_popmenu address_to list = &m->list;
        struct ui sub = *ui;
        int item = ui_menu_item_height(ui, m), pad = ui_dev(ui, UI_MENU_PAD);

        ui_menu_glide(ui, m);

        if (m->owner < 0)
                return;
        ui->top = 0;
        ui->bottom = ui->height;
        list->count = 0;
        for (int at = 0; at < m->count; at++)
                ui_popmenu_add(list, m->word[at], null, UI_MENU_RADIO | (at == m->current ? UI_MENU_ON : 0), at);
        list->hot = m->at, list->top = ui_menu_top(ui, m), list->rows_shown = m->shown;
        list->width = m->w, list->height = m->h, list->touch = m->touch;
        sub.pixels = ui->pixels + (unsigned long)m->y * ui->pitch + (unsigned long)m->x;
        sub.width = m->w, sub.height = m->h;
        sub.top = 0, sub.bottom = m->h;
        sub.band_top = ui->band_top - m->y, sub.band_bottom = ui->band_bottom - m->y;
        ui_popmenu_draw(&sub, list);
        if (m->pressed >= list->top && m->pressed < list->top + m->shown)
                ui_kit_fill(ui, &(struct ui_box){(m->x + pad) * 256, (m->y + pad + (m->pressed - list->top) * item) * 256, (m->x + m->w - pad) * 256,
                                                 (m->y + pad + (m->pressed - list->top + 1) * item) * 256, ui_q8(ui, 6), ui_q8(ui, 6)},
                            ui_mix(ui_role(UI_ON_SURFACE), 10, 0), null);
        if (m->count > m->shown)
                ui_scroll_draw(ui, &m->scroll);
}

/* ---- a finger on a page ---- */

/*
        A finger: down, what it is on shows pressed and nothing is done yet; past the slop it drags the region it went
        down on, which follows it; up, the press is done where it went down if it did not move off it, and the region
        is thrown if it was moving; a press taken back does nothing. The region's moves are answered, to be drawn.
*/
struct ui_finger
{
        struct ui_press press;
        struct ui_drag drag;
        struct ui_scroll address_to region;
};

static fn ui_finger_down(struct ui_finger address_to f, int x, int y, int target, struct ui_scroll address_to region)
{
        ui_drag_stop(&f->drag);
        f->region = region;
        if (region)
                region->left = 0, region->flung = 0, ui_scroll_stop(region);
        ui_press_begin(&f->press, x, y, target, true);
}

// The finger moved: true when what it shows changed (its press became a drag, or the region moved).
static b32 ui_finger_move(const struct ui address_to ui, struct ui_finger address_to f, int x, int y)
{
        b32 changed = false;

        if (!f->press.down)
                return false;
        if (!f->press.dragged && ui_press_move(ui, &f->press, x, y))
        {
                ui_drag_start(&f->drag, f->press.y, clock_monotonic_nanoseconds());
                changed = true;
        }
        if (f->press.dragged && f->region)
        {
                int before = f->region->scroll, moved = ui_drag_follow(&f->drag, y, clock_monotonic_nanoseconds());

                // A finger moving at a throw's speed is flinging: it is not pulling, so the region does not give past an end.
                f->region->flung = f->drag.speed >= UI_DRAG_THROW || f->drag.speed <= -UI_DRAG_THROW;
                ui_scroll_drag(f->region, -moved);
                changed |= f->region->scroll != before;
        }
        return changed;
}

// The finger lifted: what it pressed, done, or -1.
static int ui_finger_up(struct ui_finger address_to f, b32 cancelled)
{
        if (f->press.dragged)
        {
                b32 thrown = ui_drag_let_go(&f->drag, clock_monotonic_nanoseconds());

                // The drag's speed is the finger's, down the screen positive; the region moves the other way (ui_finger_move).
                if (f->region)
                        ui_scroll_let_go(f->region, thrown, -f->drag.speed);
        }
        return f->press.down ? ui_press_end(&f->press, cancelled) : -1;
}

/* ---- a page ---- */

struct ui_page
{
        const char address_to title;       // at the top left
        const char address_to status;      // at the foot: a line, or two where it does not fit one
        b32 column_on;                     // it has a column of groups at the left
        int width, height;                 // the window, device pixels
        // Laid out by the kit, device pixels.
        int top, bottom;                   // the body's band
        int left, right;                   // and its sides
        int column_w;
        struct ui_control search;          // a bare field at the title's right, when the app gave it a placeholder
        struct ui_control column;          // the column: the kit's tabs on end
        struct ui_scroll column_scroll;
        char names[UI_ITEMS][UI_MENU_WORD];
        int pointer_x, pointer_y;
        // The app's: what scrolls in the body (its list's region, or a page of text's), the menu a row opened, and where
        // a change is drawn again (rows of the window, device pixels).
        struct ui_scroll address_to body;
        struct ui_menu address_to menu;
        fn (*damaged)(int top, int bottom);
};

static fn ui_page_layout(const struct ui address_to ui, struct ui_page address_to p, int width, int height)
{
        int pad = ui_dev(ui, UI_PAD), side = width * UI_COLUMN_SHARE / 100;

        p->width = width, p->height = height;
        p->top = ui_dev(ui, UI_PAGE_TOP);
        p->bottom = height - ui_dev(ui, UI_PAGE_FOOT);
        side = side < ui_dev(ui, UI_COLUMN_LEAST) ? ui_dev(ui, UI_COLUMN_LEAST) : side;
        side = side > ui_dev(ui, UI_COLUMN_MOST) ? ui_dev(ui, UI_COLUMN_MOST) : side;
        p->column_w = p->column_on ? (side > width / 2 ? width / 2 : side) : 0;
        p->left = pad + (p->column_on ? p->column_w + ui_dev(ui, UI_COLUMN_GUTTER) : 0);
        p->right = width - pad;
        p->column_scroll.x = pad + p->column_w + (ui_dev(ui, UI_COLUMN_GUTTER) - ui_dev(ui, UI_SCROLL_WIDTH)) / 2;
        p->column_scroll.y = p->top, p->column_scroll.height = p->bottom - p->top, p->column_scroll.view = p->bottom - p->top;
        p->column_scroll.content = p->column.count * ui_dev(ui, UI_ROW);
        ui_scroll_clamp(&p->column_scroll);
}

// The group under a point of the window, or -1.
static int ui_page_column_hit(const struct ui address_to ui, const struct ui_page address_to p, int x, int y)
{
        int group = (y - p->top + p->column_scroll.scroll) / ui_dev(ui, UI_ROW);

        if (!p->column_on || y < p->top || y >= p->bottom || x < ui_dev(ui, UI_PAD) || x >= ui_dev(ui, UI_PAD) + p->column_w ||
            group >= p->column.count)
                return -1;
        return group;
}

// The column's groups: their names, cut to the column, and how many.
static fn ui_page_groups(struct ui address_to ui, struct ui_page address_to p, const char address_to (*name)(int group), int count)
{
        p->column.count = count < UI_ITEMS ? count : UI_ITEMS;
        for (int group = 0; group < p->column.count; group++)
        {
                string_address fitted = ui_fit(ui, (string_address)name(group), UI_LABEL_LARGE, p->column_w - ui_dev(ui, 24),
                                               p->names[group], sizeof(p->names[group]));

                if (fitted != (string_address)p->names[group])
                        string_copy_bounded((string_address)p->names[group], fitted, sizeof(p->names[group]));
                p->column.items[group] = p->names[group];
        }
        p->column_scroll.content = p->column.count * ui_dev(ui, UI_ROW);
}

// The group chosen kept in sight in a column that does not fit: true when it moved.
static b32 ui_page_column_reveal(const struct ui address_to ui, struct ui_page address_to p, int group)
{
        return ui_scroll_reveal(&p->column_scroll, group * ui_dev(ui, UI_ROW), ui_dev(ui, UI_ROW));
}

// A key in the column: the group it moves to (Up, Down, Home, End, three for a page), or -1 when it is not one of them.
static int ui_page_column_key(const struct ui_page address_to p, int key, int group)
{
        int last = p->column.count - 1;

        switch (key)
        {
        case UI_KEY_UP: group--; break;
        case UI_KEY_DOWN: group++; break;
        case UI_KEY_HOME: group = 0; break;
        case UI_KEY_END: group = last; break;
        case UI_KEY_PAGE_UP: group -= UI_COLUMN_PAGE; break;
        case UI_KEY_PAGE_DOWN: group += UI_COLUMN_PAGE; break;
        default: return -1;
        }
        return group < 0 ? 0 : group > last ? last : group;
}

/*
        The column drawn: the chosen group under the accent's pip, which slides to another however it is chosen, ringed
        while the keys are in it; none chosen (disabled) while a search lists the rows of every group.
*/
static fn ui_page_column(struct ui address_to ui, struct ui_page address_to p, int chosen, b32 keys, b32 disabled)
{
        struct ui_control address_to c = &p->column;

        if (!p->column_on)
                return;
        ui_scroll_glide(ui, &p->column_scroll);
        ui->top = p->top;
        ui->bottom = p->bottom;
        if (c->kind != UI_TABS)
        {
                c->kind = UI_TABS, c->variant = UI_TABS_VERTICAL;
                c->at = chosen;
        }
        c->x = UI_PAD;
        c->y = (p->top - p->column_scroll.scroll) / ui->scale;
        c->w = p->column_w / ui->scale;
        c->h = UI_ROW;
        c->disabled = disabled;
        c->focused = keys;
        c->item = ui_control_hit(ui, c, p->pointer_x, p->pointer_y);
        c->hover = c->item >= 0;
        if (chosen != c->at)
                ui_tabs_choose(ui, c, chosen); // placed at its first frame, moving after
        ui_control_draw(ui, c);
        ui_scroll_draw(ui, &p->column_scroll);
}

/*
        The page's regions: the open menu (which has the wheel, and nothing else does while it is open), the column at
        the left of the gutter's middle, and the body. A region that moved is drawn again: the menu keeps the keys'
        choice in view as it goes, and is drawn whole; the column and the body are the body's band.
*/
static struct ui_scroll address_to ui_page_region(const struct ui address_to ui, struct ui_page address_to p, int x, int y)
{
        struct ui_menu address_to m = p->menu;

        if (m && m->owner >= 0 && x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
                return &m->scroll;
        if (p->column_on && x < p->left - ui_dev(ui, UI_COLUMN_GUTTER) / 2)
                return &p->column_scroll;
        return p->body;
}

static fn ui_page_moved(const struct ui address_to ui, struct ui_page address_to p, const struct ui_scroll address_to region)
{
        struct ui_menu address_to m = p->menu;

        if (!p->damaged || !region)
                return;
        if (m && region == &m->scroll)
        {
                int top = ui_menu_top(ui, m);

                m->at = m->at < top ? top : m->at >= top + m->shown ? top + m->shown - 1 : m->at;
                p->damaged(0, p->height);
        }
        else
                p->damaged(p->top - ui_dev(ui, UI_ROW), p->bottom + ui_dev(ui, UI_ROW));
}

// The wheel on the page: it turns the region under the pointer.
static fn ui_page_wheel(const struct ui address_to ui, struct ui_page address_to p, int delta)
{
        struct ui_scroll address_to region = p->menu && p->menu->owner >= 0 ? &p->menu->scroll : ui_page_region(ui, p, p->pointer_x, p->pointer_y);

        if (region && ui_scroll_wheel(ui, region, delta))
                ui_page_moved(ui, p, region);
}

/*
        A mouse's motion, press or release on the page's scrollbars (the menu's while it is open, else the body's and
        the column's): the thumb lights under the pointer, is dragged, and a press on the track pages. True when it
        was a bar's.
*/
static b32 ui_page_bars(const struct ui address_to ui, struct ui_page address_to p, int type, b32 down)
{
        struct ui_scroll address_to bars[3] = {p->menu && p->menu->owner >= 0 ? &p->menu->scroll : null,
                                               p->menu && p->menu->owner >= 0 ? null : p->body,
                                               p->menu && p->menu->owner >= 0 || !p->column_on ? null : &p->column_scroll};
        b32 taken = false;

        for (int k = 0; k < 3; k++)
        {
                b32 lit;
                int scroll;

                if (!bars[k])
                        continue;
                lit = bars[k]->hover;
                scroll = ui_scroll_pointer(ui, bars[k], type, p->pointer_x, p->pointer_y, down, 0);
                if (scroll >= 0)
                {
                        taken = true;
                        ui_scroll_set(bars[k], scroll);
                }
                if (scroll >= 0 || lit != bars[k]->hover)
                        ui_page_moved(ui, p, bars[k]);
        }
        return taken;
}

// A press on the column's group: its ripple, from the press.
static fn ui_page_column_press(const struct ui address_to ui, struct ui_page address_to p, int group, int x, int y)
{
        struct ui_control address_to c = &p->column;

        ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + group * c->h), ui_dev(ui, c->w), ui_dev(ui, c->h), 8);
}

/*
        The page's own parts: cleared to the surface, the title in the title role, the search field (the engine's field
        the app gave it, search.edit, lit while it has the keys), and the status line at the foot, quiet, broken at a
        blank where it does not fit one line.
*/
static fn ui_page_draw(struct ui address_to ui, struct ui_page address_to p, b32 keys)
{
        int pad = ui_dev(ui, UI_PAD), left = pad + ui_dev(ui, UI_PAGE_INSET), room = p->width - left - pad, foot = ui_dev(ui, UI_PAGE_FOOT);

        ui->top = 0;
        ui->bottom = p->height;
        ui_clear(ui);
        if (p->title)
                ui_kit_text(ui, left, ui_kit_baseline(ui, ui_dev(ui, 8) + (p->top - ui_dev(ui, 8) - ui_dev(ui, 28)) / 2, UI_TITLE_LARGE),
                            (string_address)p->title, UI_TITLE_LARGE, ui_role(UI_ON_SURFACE), 0);
        if (p->search.items[0])
        {
                struct ui_control address_to c = &p->search;

                if (c->kind != UI_INPUT)
                {
                        c->kind = UI_INPUT, c->variant = UI_FIELD_BARE;
                        ui_control_start(c);
                }
                c->w = UI_SEARCH_W, c->h = UI_SEARCH_H;
                c->x = (p->width - pad) / ui->scale - c->w, c->y = UI_SEARCH_Y;
                c->focused = keys;
                ui_control_draw(ui, c);
        }
        ui->top = p->bottom;
        if (p->status && p->status[0])
        {
                unsigned int quiet = ui_role(UI_ON_SURFACE_VARIANT);
                positive length = string_length((string_address)p->status), cut = 0;
                char first[256];

                if (ui_kit_measure(ui, (string_address)p->status, UI_LABEL_MEDIUM) <= room || length >= sizeof(first))
                {
                        ui_kit_text(ui, left, ui_kit_baseline(ui, p->bottom + (foot - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM), (string_address)p->status,
                                    UI_LABEL_MEDIUM, quiet, 0);
                        return;
                }
                for (positive at = 1; at < length; at++)
                {
                        if (p->status[at] != ' ')
                                continue;
                        memory_copy(first, p->status, at);
                        first[at] = 0;
                        if (ui_kit_measure(ui, (string_address)first, UI_LABEL_MEDIUM) > room)
                                break;
                        cut = at;
                }
                cut = cut ? cut : length;
                memory_copy(first, p->status, cut);
                first[cut] = 0;
                ui_kit_text(ui, left, ui_kit_baseline(ui, p->bottom + foot / 2 - ui_dev(ui, 16), UI_LABEL_MEDIUM), (string_address)first,
                            UI_LABEL_MEDIUM, quiet, 0);
                ui_kit_text(ui, left, ui_kit_baseline(ui, p->bottom + foot / 2, UI_LABEL_MEDIUM), (string_address)p->status + cut + (cut < length),
                            UI_LABEL_MEDIUM, quiet, 0);
        }
}

/*
        A page of text in the body: lines in the body role, a line's height apart and padded, drawn where they are in
        view of the region; and the region's height, for what it scrolls over.
*/
static int ui_lines_height(const struct ui address_to ui, int count)
{
        return count * ui_dev(ui, UI_LINE) + 2 * ui_dev(ui, UI_LINES_PAD);
}

static fn ui_lines_draw(struct ui address_to ui, struct ui_scroll address_to s, int left, int count,
                        const char address_to (*line)(int at, char address_to into, positive room))
{
        int height = ui_dev(ui, UI_LINE), top;
        char text[512];

        ui_scroll_glide(ui, s);
        top = s->y - s->scroll + ui_dev(ui, UI_LINES_PAD);

        ui->top = s->y;
        ui->bottom = s->y + s->view;
        for (int at = 0; at < count; at++)
        {
                int y = top + at * height;

                if (y + height <= ui->top || y >= ui->bottom)
                        continue;
                ui_kit_text(ui, left + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, y, UI_BODY_MEDIUM), (string_address)line(at, text, sizeof(text)),
                            UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0);
        }
        ui_scroll_draw(ui, s);
}

// A key on a page of text: Up and Down a line, Page Up and Down a view, Home and End; true when it was one, moved when it moved.
static b32 ui_lines_key(const struct ui address_to ui, struct ui_scroll address_to s, int key, b32 address_to moved)
{
        int line = ui_dev(ui, UI_LINE), to;

        switch (key)
        {
        case UI_KEY_UP: to = s->scroll - line; break;
        case UI_KEY_DOWN: to = s->scroll + line; break;
        case UI_KEY_PAGE_UP: to = s->scroll - s->view; break;
        case UI_KEY_PAGE_DOWN: to = s->scroll + s->view; break;
        case UI_KEY_HOME: to = 0; break;
        case UI_KEY_END: to = s->content; break;
        default: return false;
        }
        *moved = ui_scroll_set(s, to);
        return true;
}

/*
        One app's window and loop. The runner claims the standard descriptors,
        takes the window, sets its title, name, glass, scale and ground, and calls
        open. Then it runs the loop: events, closing, focus, ground, the size
        asked for by the hand, a due picture,
        the app's tick and its one descriptor, and the wait. An app keeps its
        state and its pictures; it does not repeat the loop.
*/
struct ui_app
{
        struct window_row address_to own;
        struct ui address_to ui;
        string_address missing;         // logged when the desktop has no window for it
        string_address title;
        string_address name;            // what the compositor remembers where it was put by, or none (windows_name)
        unsigned int glass;             // WINDOW_GLASS only for a temporary overlay (a launcher, a prompt, a notification); else the kit's surface
        b32 address_to dirty;           // a picture is due: frame paints it
        b32 address_to focused;         // the app's own: the keys are its
        unsigned int address_to ground; // the app's own: the window's ground, as the loop last saw it
        unsigned int asked_w, asked_h;  // the size the hand last asked for
        string_address font, bold;      // the faces to draw in, or the UI_FONT defaults
        b32 no_bold;                    // true: no bold face is loaded; the default is the bold face
        string_address nofont;          // logged, with the pid, when the faces do not load
        int (*open)(void);              // the app's own start; -1 runs the loop, else the status to leave with
        b32 (*frame)(void);             // paints the due picture; false: it is due again in 50 ms
        b32 (*event)(const struct window_event address_to event); // false: quit
        b32 (*focus)(b32 focused);      // the keys came or went, optional; false: quit
        void (*changed)(void);          // optional: run after a ground or focus change, which also makes the picture due
        void (*size)(unsigned int width, unsigned int height);     // the hand asked for this size
        long (*tick)(long timeout, long address_to other);         // the app's timer; 0 runs the loop again now; other: a descriptor
        void (*ready)(void);            // other is readable
        void (*close)(void);            // before the window is left
        // Added for the terminal, which is an app on this loop. Each is optional, and none set is the loop above.
        void (*pass)(void);             // runs at the top of each pass, before the events
        void (*closing)(void);          // the window is closing: runs each pass, and the loop goes on; unset, the loop ends
        b32 (*done)(void);              // checked after tick: true ends the loop before the wait
        b32 own_faces;                  // the app opens its engine and loads its faces in open; the runner loads neither
        // The kit: its controls moved this frame, in rows top to bottom (all of it while the theme turns); the app
        // marks them to be drawn again, as the next frame comes a display period on.
        void (*moving)(int top, int bottom, b32 all);
        // A menu the app has open (ui_popup), whose wake the loop waits on too, and what it chose.
        struct ui_popup address_to popup;
        void (*chose)(int id);
        b32 mono;                       // the kit's mono face is loaded too (a tag, a code well, a key)
};

// Every row is due: the picture, and the app's changed hook when it has one.
static fn ui_app_changed(struct ui_app address_to app)
{
        *app->dirty = true;
        if (app->changed)
                app->changed();
}

static int ui_app_run(struct ui_app address_to app)
{
        struct windows address_to table;
        unsigned int row;
        int status;
        b32 running = true;

        claim_standard_descriptors();
        if (windows_take(app->own) < 0)
        {
                log_direct((address_any)app->missing, string_length((const_string)app->missing));
                return 1;
        }
        table = app->own->table;
        row = app->own->row;
        app->ui->scale = table->scale >= 1 && table->scale <= 8 ? (int)table->scale : 1;
        if (!app->own_faces)
        {
                if (!ui_engine_open(app->ui) || !ui_face_load(&app->ui->faces[0], app->font ? app->font : (string_address)UI_FONT_REGULAR))
                {
                        compositor_kmsg(app->nofont, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                        windows_leave(app->own);
                        return 1;
                }
                app->ui->regular = &app->ui->faces[0];
                app->ui->bold = !app->no_bold && ui_face_load(&app->ui->faces[1], app->bold ? app->bold : (string_address)UI_FONT_BOLD)
                                    ? &app->ui->faces[1] : null;
                app->ui->mono = app->mono && ui_face_load(&app->ui->face_mono, (string_address)UI_FONT_MONO) ? &app->ui->face_mono : null;
        }
        // The theme the compositor keeps in the table, taken at once the first time.
        ui_kit.themed = __atomic_load_n(&table->themed, __ATOMIC_ACQUIRE);
        // A table the compositor has not themed yet is the standard: dark, white.
        ui_kit_theme(ui_kit.themed ? table->theme : UI_THEME_DARK, ui_kit.themed ? table->accent : UI_ACCENT_STANDARD, table->motion);
        string_copy_bounded((string_address)table->title[row], app->title, WINDOWS_TITLE);
        windows_name(app->own, (const char *)app->name);
        table->glass[row] = app->glass;
        *app->ground = table->ground[row];
        ui_inks(app->ui, *app->ground ? *app->ground : UI_GLASS_GROUND);
        // A window that is not glass is the kit's surface, and its inks are the surface's.
        app->ui->opaque = !(app->glass & WINDOW_GLASS);
        app->ui->glass = (app->glass & WINDOW_GLASS) != 0;
        ui_kit_inks(app->ui);
        *app->focused = false;
        status = app->open();
        if (status >= 0)
        {
                windows_leave(app->own);
                return status;
        }

        while (running)
        {
                struct window_event event;
                long timeout = -1, other = -1, ready;

                if (app->pass)
                        app->pass();
                while (running && windows_event(app->own, &event))
                        if (!app->event(&event))
                                running = false;
                if (app->popup && app->popup->open)
                {
                        int chosen = ui_popup_pump(app->popup);

                        if (chosen != UI_MENU_NOTHING && app->chose)
                                app->chose(chosen);
                }

                if (table->state[row] & WINDOW_CLOSING)
                {
                        if (app->closing)
                                app->closing();
                        else
                                running = false;
                }

                if ((table->focus == row) != *app->focused)
                {
                        *app->focused = table->focus == row;
                        ui_app_changed(app);
                        if (app->focus && !app->focus(*app->focused))
                                running = false;
                }
                if (table->ground[row] && table->ground[row] != *app->ground)
                {
                        *app->ground = table->ground[row];
                        ui_inks(app->ui, *app->ground);
                        ui_app_changed(app);
                }
                // A new theme (the compositor counted it up and knocked): the whole picture turns to it.
                if (__atomic_load_n(&table->themed, __ATOMIC_ACQUIRE) != ui_kit.themed)
                {
                        ui_kit.themed = __atomic_load_n(&table->themed, __ATOMIC_ACQUIRE);
                        ui_kit_theme(table->theme, table->accent, table->motion);
                        ui_kit_inks(app->ui);
                        ui_app_changed(app);
                }
                if (table->want_width[row] && table->want_height[row] &&
                    (table->want_width[row] != app->asked_w || table->want_height[row] != app->asked_h))
                {
                        app->asked_w = table->want_width[row];
                        app->asked_h = table->want_height[row];
                        ui_kit_settle();
                        app->size(app->asked_w, app->asked_h);
                }

                if (!running)
                        break;

                /*
                        The kit's clock: a frame is drawn at its time, and while anything in it moves (a spring, a
                        transition, a ripple, the theme turning) the next is due a display period after it, the
                        picture marked due again. Nothing moving, the loop waits on the window and draws nothing.
                */
                if (*app->dirty)
                {
                        positive period = table->period ? table->period : 16666667u, now = clock_monotonic_nanoseconds();

                        if (ui_kit.busy && now < (positive)ui_kit.drawn + period)
                                timeout = (long)((positive)ui_kit.drawn + period - now);
                        else
                        {
                                ui_kit.now = (b64)now;
                                ui_kit.busy = 0;
                                ui_kit.damage_top = ui_kit.damage_bottom = 0;
                                ui_kit_turn();
                                if (!app->frame())
                                        timeout = 50000000;
                                else
                                        ui_kit.drawn = (b64)now;
                                if (ui_kit.busy)
                                {
                                        *app->dirty = true;
                                        if (app->moving)
                                                app->moving(ui_kit.damage_top, ui_kit.damage_bottom, !ui_tween_still(&ui_kit.turn));
                                        timeout = timeout < 0 || timeout > (long)period ? (long)period : timeout;
                                }
                        }
                }
                if (app->tick)
                {
                        timeout = app->tick(timeout, &other);
                        if (timeout == 0)
                                continue;
                }
                if (app->done && app->done())
                        break;

                ready = windows_wait_also(app->own, app->popup && app->popup->open ? app->popup->own.wake : -1, other, timeout);
                if (ready < 0)
                        break;
                if (other >= 0)
                        app->ready();
        }

        if (app->close)
                app->close();
        windows_leave(app->own);
        return 0;
}

/*
        The value of an option of the command line: the argument after the one at at, when the argument at at is name
        exactly ("-f", "-m" and the like), or null when it is not, or no argument follows it. The caller moves past
        the value when it takes one.
*/
static string_address ui_argument(positive at, string_address name)
{
        string_address argument;

        if (at + 1 >= (positive)program_argument_count())
                return null;
        argument = program_argument(at);
        for (positive i = 0;; i++)
        {
                if (argument[i] != name[i])
                        return null;
                if (!argument[i])
                        return program_argument(at + 1);
        }
}

/*
        A window's place in the table, centred on the primary screen (windows_output's index 0, the output itself
        where the table lists no screens): x by its width, and y by its height, raised offset pixels above the
        middle (offset 0 for the middle itself).
*/
static void ui_centre(struct windows address_to table, unsigned int row, int width, int height, int offset)
{
        int x, y, screen_width, screen_height;

        windows_output(table, 0, &x, &y, &screen_width, &screen_height);
        table->x[row] = x + (screen_width - width) / 2;
        table->y[row] = y + (screen_height - height) / 2 - offset;
}

// Whether a process is the program name: its command name is name and a newline, as /proc/PID/comm has it.
static b32 ui_is_program(unsigned int pid, string_address name)
{
        p8 path[40] = "/proc/", comm[16] = {0};
        bipolar handle;
        positive length = string_length(name);

        path[6 + positive_into_string(path + 6, (positive)pid)] = 0;
        string_copy((string_address)path + string_length((string_address)path), (string_address) "/comm");
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return false;
        system_read_once((b32)handle, comm, sizeof(comm) - 1);
        system_close(handle);
        return length < sizeof(comm) - 1 && !memory_compare(comm, name, length) && comm[length] == '\n';
}

/*
        Another window of the program name that has shown a picture is lifted: the compositor is told to raise it, as a
        second start asks. With put_away, the program is sent SIGTERM instead, and the compositor sees its door close as
        it does any program's end. The window own is not another. True when there was one.
*/
static b32 ui_lift_other(const struct window_row address_to own, string_address name, b32 put_away)
{
        struct windows address_to table = own->table;

        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                unsigned int pid = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);
                char door_name[48];
                bipolar door;

                if (row == own->row || !pid || !__atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE) ||
                    !ui_is_program(pid, name))
                        continue;

                if (put_away)
                {
                        system_call_2(syscall(kill), (positive)pid, SIGTERM);
                        return true;
                }
                __atomic_add_fetch(&table->lift[row], 1, __ATOMIC_RELEASE);
                windows_door_name(door_name, row, 0);
                door = system_open_at(AT_FDCWD, (string_address)door_name, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                if (door >= 0)
                {
                        system_write_once((b32)door, "", 1);
                        system_close(door);
                }
                return true;
        }
        return false;
}

/* ---- text: a field, a text area and the code view, over text.c's documents ---- */

/*
        One widget for every place text is typed: a field (one line, the kit's UI_INPUT draws the well and this draws
        what is in it), a text area (lines, wrapped, in the UI's face) and the code view (lines in the mono, a gutter of
        line numbers, the tokenizer's colours, soft wrap on or off, zoom). The document, its history and selections are
        text.c's; this is the hand and the pixels.

        The hand: a click puts the caret, a drag selects, Shift with a click extends, a double click takes a word and a
        drag after it whole words, a triple click a line and whole lines, Alt with a click adds a caret, Alt and Shift
        with a drag selects a box of columns, the middle button pastes PRIMARY, the right button asks the app for a
        context menu (ui_text_asks), a finger's tap is a caret and a finger's drag scrolls (thrown, it slows as the
        kit's lists do), and the wheel scrolls (Ctrl and the wheel zoom the code view). The keys are the keymap's
        (text.c, overridden by /root/canvas.text.keys); a key that is no binding types its character, and AltGr always
        types. The caret blinks only while the widget has the keys, never while it moves, and ui_text_tick says when
        it next needs a frame, so an idle widget wakes nothing. Colours are the kit's roles: the selection, the caret
        (the accent), the text, the placeholder (the text at 36%), an error's underline.

        Glyphs land on whole pixels: the code view lays its characters on a grid of whole cells (font_cells, and
        font_draw for a cluster with marks), and a field and an area draw a line with the kit's text, whose caret and
        selection follow the same pen.
*/
#define UI_TEXT_FIELD 0
#define UI_TEXT_AREA 1
#define UI_TEXT_CODE 2
#define UI_TEXT_CLUSTERS 2048 // the clusters of a line laid out at most: past them a line is drawn to there
#define UI_TEXT_BLINK_NS 530000000ull
#define UI_TEXT_CLICKS_NS 450000000ull
#define UI_TEXT_ROWS 512

// What the widget asks of the app after an event: ui_text_event answers these bits.
#define UI_TEXT_CHANGED 1u    // the text changed
#define UI_TEXT_MOVED 2u      // only the carets or the view
#define UI_TEXT_MENU 4u       // the context menu, at menu_x, menu_y
#define UI_TEXT_SUBMIT 8u     // Enter in a field
#define UI_TEXT_CANCEL 16u    // Escape in a field with nothing to cancel
#define UI_TEXT_COMMAND 32u   // a command the app does (find, save, ...): command
#define UI_TEXT_PASTE 64u     // a paste was asked of the compositor
#define UI_TEXT_REFUSED 128u  // a field refused what was typed or pasted: refused says why, in the field's own words
#define UI_TEXT_BLINK_FOR_NS 10000000000ull // the caret blinks this long after the last key or move, then stays

struct ui_text
{
        struct doc doc;
        int mode;
        const struct doc_keymap address_to keymap;
        const struct window_row address_to own; // the window, for the clipboard
        const char address_to placeholder;
        const char address_to comment;          // a line comment, for the comment command
        b32 focused, read_only, wrap, numbers;
        int size;                               // logical pixels of the text
        int left, top, right, bottom, gutter;   // the last draw's text box, device pixels
        int line_height, cell;
        p64 top_line;
        int top_row;
        b64 scroll_x;
        int clicks, click_x, click_y;
        positive click_at;
        b32 dragging, drag_box, touch;
        b32 holding;                            // a finger's long press holds the selection (ui_text_hold) until its lift
        int drag_kind;                          // 1 characters, 2 words, 3 lines
        p64 drag_start, drag_end;
        p64 box_line, box_column;
        struct ui_drag kinetic;
        positive blink_at;
        b32 caret_off;
        p32 asks, command;
        int menu_x, menu_y;
        int zoom;
        p8 preedit[64];                         // composition: text an input method is making, drawn at the caret
        p32 preedit_length;
        /*
                A field's rules, which its app sets and the widget keeps for every way text comes in (a key, a paste, a
                composition): the bytes it holds at most (0 none) and what it says when that is reached, a character it
                may not hold (accept answers what it says then, null when it may), and masked (a passphrase: a dot for
                each character, nothing ever copied out of it, and no word of it told apart). refused is what the last
                refusal said, for the app's status line.
        */
        positive most;
        const char address_to most_said;
        const char address_to (*accept)(p32 code);
        const char address_to refused;
        b32 masked;
        b32 keeps_vertical;                     // the app takes Up and Down (a list's, rows', a scroll): else they go to the ends, as Home and End
        b32 pasting;                            // it asked the clipboard, and the answer is its
        int well_left, well_top, well_right, well_bottom; // where the hand reaches it, device pixels: a field's whole well
        positive awake_at;                      // the last key or move, from which the caret blinks a while
};

/*
        The keys every text widget of a process reads: the defaults (text.c's doc_default_keys) and the lines of
        /root/canvas.text.keys over them, read the first time a widget asks. The file is the person's own; a line that is
        not a binding is skipped, so a mistake in it costs that line and not the keys.
*/
#define UI_TEXT_KEYS_PATH "/root/canvas.text.keys"

static const struct doc_keymap address_to ui_text_keymap(void)
{
        static struct doc_keymap keys;
        static b32 read;

        if (!read)
        {
                static char text[16384];
                bipolar handle = system_open_at(AT_FDCWD, (string_address)UI_TEXT_KEYS_PATH, FILE_READ | O_CLOEXEC);
                bipolar got = handle >= 0 ? system_read_once((b32)handle, text, sizeof(text) - 1) : 0;

                if (handle >= 0)
                        system_close(handle);
                doc_keymap_defaults(&keys);
                if (got > 0)
                        doc_keymap_read(&keys, text, (positive)got);
                read = true;
        }
        return &keys;
}

// A widget over text (length bytes, kept by the caller while it lives: a field's typed text is the widget's own).
static b32 ui_text_open(struct ui_text address_to t, int mode, const p8 address_to text, p64 length, const struct doc_keymap address_to keymap)
{
        memory_zero(t, sizeof(*t));
        if (!doc_open(&t->doc, text, length))
                return false;
        t->mode = mode;
        t->keymap = keymap;
        t->doc.single_line = mode == UI_TEXT_FIELD;
        t->doc.auto_close = mode == UI_TEXT_CODE;
        t->doc.normalize_paste = true;
        t->size = mode == UI_TEXT_CODE ? 13 : 15;
        t->numbers = mode == UI_TEXT_CODE;
        t->wrap = mode == UI_TEXT_AREA;
        t->comment = "//";
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds();
        return true;
}

/*
        A field, as an app declares one: one line of the engine on text (kept by the caller while the field is open:
        undo goes back to it and no further), the caret at its end, the keys in it, its clipboard the window own's.
        Its rules (most, most_said, accept, masked) are the app's to set after; the kit holds every way in to them.
*/
static b32 ui_field_open(struct ui_text address_to t, const struct window_row address_to own, const char address_to text)
{
        positive length = text ? string_length((string_address)text) : 0;

        if (!ui_text_open(t, UI_TEXT_FIELD, (const p8 address_to)(text ? text : ""), length, ui_text_keymap()))
                return false;
        doc_select_set(&t->doc, length, length);
        t->own = own;
        t->focused = true;
        return true;
}

// A field's rule for a number: digits, and nothing else (a field's accept, for an app that asks a count or a line).
static const char address_to ui_field_digits(p32 code)
{
        return code >= '0' && code <= '9' ? null : "a number is digits";
}

// The widget let go; a masked field's text (every byte ever typed into it is in the added buffer) and composition are
// wiped first, so no passphrase outlives its field in memory handed back.
static fn ui_text_close(struct ui_text address_to t)
{
        if (t->masked && t->doc.added)
                memory_zero(t->doc.added, t->doc.added_room);
        memory_zero(t->preedit, sizeof(t->preedit));
        t->preedit_length = 0;
        doc_close(&t->doc);
}

// The whole text into into, room bytes at most, a NUL after it: its length. What a field's owner reads it as.
static positive ui_text_string(const struct ui_text address_to t, p8 address_to into, positive room)
{
        positive length = room ? doc_read(&t->doc, 0, into, room - 1) : 0;

        if (room)
                into[length] = 0;
        return length;
}

// The text replaced, the caret at its end: what an app that sets a field does.
static fn ui_text_set(struct ui_text address_to t, const p8 address_to text, positive length)
{
        doc_replace(&t->doc, 0, doc_length(&t->doc), text, length);
        doc_select_set(&t->doc, doc_length(&t->doc), doc_length(&t->doc));
}

/*
        How much of text may go in at the carets under the field's rules: every character one it accepts (none of a
        text with one it does not: refused says what accept said), and the text after it no longer than most bytes
        (with cut, as much of it as fits, cut before a whole character, as a paste is; without, none: a key's character
        goes in whole or not at all). refused says what most_said says when the room ran out.
*/
static positive ui_text_admit(struct ui_text address_to t, const p8 address_to text, positive length, b32 cut)
{
        positive at = 0, selected = 0, carets = t->doc.selection_count ? t->doc.selection_count : 1, room;

        while (t->accept && at < length)
        {
                p32 code;
                positive size = doc_decode(text + at, length - at, &code);
                const char address_to said = size ? t->accept(code) : null;

                if (said)
                {
                        t->refused = said;
                        return 0;
                }
                at += size ? size : 1;
        }
        if (!t->most)
                return length;
        for (positive s = 0; s < t->doc.selection_count; s++)
                selected += doc_sel_end(&t->doc.selections[s]) - doc_sel_start(&t->doc.selections[s]);
        room = doc_length(&t->doc) - selected < t->most ? (t->most - (doc_length(&t->doc) - selected)) / carets : 0;
        if (length <= room)
                return length;
        t->refused = t->most_said ? t->most_said : "that is as much as it holds";
        if (!cut)
                return 0;
        while (room && (text[room] & 0xc0) == 0x80)
                room--;
        return room;
}

/*
        Composition, for an input method to come: the text it is making is shown at the primary caret, underlined, and
        is no part of the document until it is committed (typed in, one undo of its own) or cancelled. What is shown is
        whole UTF-8 characters with no control among them (C0, DEL, C1), at most the room it has; anything else is
        refused and the composition left as it was. Answers whether it was taken.
*/
static b32 SPARE ui_text_preedit(struct ui_text address_to t, const p8 address_to text, positive length)
{
        positive at = 0;

        if (length >= sizeof(t->preedit) || t->read_only)
                return false;
        while (at < length)
        {
                p32 code;
                positive size = doc_decode(text + at, length - at, &code);

                if (!size || (code & DOC_INVALID) || code < 0x20 || (code >= 0x7f && code < 0xa0))
                        return false;
                at += size;
        }
        // Whole: the buffer is drawn as a string, so what the composition said before, past this length, is cleared.
        memory_zero(t->preedit, sizeof(t->preedit));
        memory_copy(t->preedit, text, length);
        t->preedit_length = (p32)length;
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds(), t->caret_off = false;
        return true;
}

// The composition typed in at the carets, as if it had been typed, and gone; or gone with nothing typed.
static p32 SPARE ui_text_preedit_end(struct ui_text address_to t, b32 commit)
{
        p64 version = t->doc.version;
        b32 refused = false;

        if (commit && t->preedit_length && !t->read_only)
        {
                // A field's rules hold for a composition as for a key: it goes in whole or not at all.
                refused = ui_text_admit(t, t->preedit, t->preedit_length, false) < t->preedit_length;
                t->doc.now = clock_monotonic_nanoseconds();
                if (!refused)
                        doc_type(&t->doc, t->preedit, t->preedit_length);
        }
        memory_zero(t->preedit, sizeof(t->preedit));
        t->preedit_length = 0;
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds(), t->caret_off = false;
        return (t->doc.version != version ? UI_TEXT_CHANGED : UI_TEXT_MOVED) | (refused ? UI_TEXT_REFUSED : 0);
}

/*
        A line laid out: each cluster's offset in the document and its left edge, in device pixels from the line's start,
        the last entry the line's end. The display text a line is drawn as goes into shown: a tab as spaces, a control
        as its control picture, a byte that is not UTF-8 as U+FFFD. Answers how many clusters.
*/
struct ui_text_layout
{
        p64 offset[UI_TEXT_CLUSTERS + 1];
        int x[UI_TEXT_CLUSTERS + 1];
        p32 shown_at[UI_TEXT_CLUSTERS + 1]; // where each cluster starts in shown
        p8 shown[UI_TEXT_CLUSTERS * 8];
        p32 shown_length;
        p32 count;
};

static const font_face address_to ui_text_face(const struct ui address_to ui, const struct ui_text address_to t)
{
        return t->mode == UI_TEXT_CODE && ui->mono ? ui->mono : ui->regular;
}

static p32 ui_text_em(const struct ui address_to ui, const struct ui_text address_to t)
{
        int size = t->size + t->zoom;

        size = size < 6 ? 6 : size > 72 ? 72 : size;
        return (p32)(size * ui->scale) << 6;
}

static fn ui_text_layout_line(struct ui address_to ui, const struct ui_text address_to t, p64 line, struct ui_text_layout address_to l)
{
        const font_face address_to faces[2] = {ui_text_face(ui, t), ui->regular};
        p32 em = ui_text_em(ui, t);
        p64 at = doc_line_start(&t->doc, line), stop = doc_line_end(&t->doc, line), column = 0;
        struct ui_pen pen;
        int cell;

        l->count = 0;
        l->shown_length = 0;
        // No face loaded (a program that never draws): nothing to lay out.
        if (!faces[0])
        {
                l->offset[0] = at;
                l->x[0] = 0;
                l->shown_at[0] = 0;
                l->shown[0] = 0;
                return;
        }
        cell = (int)font_metrics_at(faces[0], em).cell_width;
        ui_pen_start(faces, em, &pen);
        while (at < stop && l->count < UI_TEXT_CLUSTERS)
        {
                p64 next = doc_cluster_end(&t->doc, at);
                p8 cluster[32];
                positive size = doc_read(&t->doc, at, cluster, next - at < sizeof(cluster) ? (positive)(next - at) : sizeof(cluster));
                p32 code, width = doc_cluster_width(&t->doc, cluster, size, column);
                p8 shown[32];
                positive shown_size = 0;

                doc_decode(cluster, size, &code);
                // A masked field shows a dot for each character, whatever it is (a blank and a tab as well).
                if (t->masked)
                        memory_copy(shown, "\xe2\x80\xa2", shown_size = 3), width = 1;
                else if (code == '\t')
                        for (p32 k = 0; k < width && shown_size < sizeof(shown); k++)
                                shown[shown_size++] = ' ';
                else if (code & DOC_INVALID)
                        shown_size = memory_utf8_encode(shown, 4, 0xfffd);
                else if (code < 0x20 || code == 0x7f)
                        shown_size = memory_utf8_encode(shown, 4, code == 0x7f ? 0x2421 : 0x2400 + code);
                else if (code >= 0x80 && code < 0xa0)
                        shown_size = memory_utf8_encode(shown, 4, 0xfffd);
                else
                        memory_copy(shown, cluster, shown_size = size);
                l->offset[l->count] = at;
                l->shown_at[l->count] = l->shown_length;
                if (t->mode == UI_TEXT_CODE)
                        l->x[l->count] = (int)column * cell;
                else
                {
                        l->x[l->count] = ui_pen_width(&pen);
                        for (positive k = 0; k < shown_size;)
                        {
                                p32 one;

                                k = font_decode(shown, shown_size, k, &one);
                                ui_pen_step(ui, faces, em, one, &pen);
                        }
                }
                memory_copy(l->shown + l->shown_length, shown, shown_size);
                l->shown_length += (p32)shown_size;
                column += width;
                l->count++;
                at = next;
        }
        l->offset[l->count] = at;
        l->shown_at[l->count] = l->shown_length;
        l->x[l->count] = t->mode == UI_TEXT_CODE ? (int)column * cell : ui_pen_width(&pen);
        l->shown[l->shown_length] = 0;
}

// The x of an offset on its laid line (its cluster's left edge).
static int ui_text_x_of(const struct ui_text_layout address_to l, p64 offset)
{
        for (p32 k = 0; k <= l->count; k++)
                if (l->offset[k] >= offset)
                        return l->x[k];
        return l->x[l->count];
}

// The offset nearest an x on a laid line: the boundary whose edge is closest.
static p64 ui_text_at_x(const struct ui_text_layout address_to l, int x)
{
        for (p32 k = 0; k < l->count; k++)
                if (x < (l->x[k] + l->x[k + 1]) / 2)
                        return l->offset[k];
        return l->offset[l->count];
}

// The metrics the widget draws with, worked out again for the face, the size and the zoom.
static fn ui_text_metrics(struct ui address_to ui, struct ui_text address_to t)
{
        font_metrics m;

        if (!ui_text_face(ui, t))
                return;
        m = font_metrics_at(ui_text_face(ui, t), ui_text_em(ui, t));

        t->line_height = t->mode == UI_TEXT_FIELD ? (int)m.cell_height : (int)m.cell_height + (int)m.cell_height / 4;
        t->cell = (int)m.cell_width;
}

// The rows a view shows, and the document line and row the pointer at y is on.
static int ui_text_rows(const struct ui_text address_to t)
{
        return t->line_height ? (t->bottom - t->top) / t->line_height : 1;
}

/*
        The view keeps the primary caret in sight: lines scroll so it is between the top and the bottom row, columns so
        it is inside the text box (a field scrolls sideways only).
*/
static fn ui_text_reveal(struct ui address_to ui, struct ui_text address_to t)
{
        static struct ui_text_layout l;
        p64 caret = t->doc.selections[t->doc.primary].active, line = doc_line_of(&t->doc, caret);
        int rows = ui_text_rows(t), width = t->right - t->left, x;

        if (t->mode != UI_TEXT_FIELD)
        {
                if (line < t->top_line)
                        t->top_line = line;
                else if (rows > 0 && line >= t->top_line + (p64)rows)
                        t->top_line = line - (p64)rows + 1;
        }
        if (t->wrap && t->mode != UI_TEXT_FIELD)
        {
                t->scroll_x = 0;
                return;
        }
        ui_text_layout_line(ui, t, line, &l);
        x = ui_text_x_of(&l, caret);
        if (x - t->scroll_x < 0)
                t->scroll_x = x - width / 4 > 0 ? x - width / 4 : 0;
        else if (x - t->scroll_x > width - 2 * ui->scale)
                t->scroll_x = x - width + width / 4;
        // A field whose line fits is not scrolled at all; one longer shows no blank past its end.
        if (t->mode == UI_TEXT_FIELD && width > 0)
        {
                b64 most = (b64)l.x[l.count] - width + 2 * ui->scale;

                t->scroll_x = t->scroll_x > most ? (most > 0 ? most : 0) : t->scroll_x;
        }
}

// The document offset under device pixels x, y of the widget's text box (clamped to it).
static p64 ui_text_hit(struct ui address_to ui, struct ui_text address_to t, int x, int y)
{
        static struct ui_text_layout l;
        p64 line = t->mode == UI_TEXT_FIELD ? 0 : t->top_line + (p64)((y > t->top ? y - t->top : 0) / (t->line_height ? t->line_height : 1));

        line = line >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : line;
        ui_text_layout_line(ui, t, line, &l);
        return ui_text_at_x(&l, x - t->left + (int)t->scroll_x);
}

// The caret is drawn for the next half second whatever the blink says: it is moving, or just typed at.
static fn ui_text_wake(struct ui_text address_to t)
{
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds();
        t->caret_off = false;
}

/*
        The blink: when the widget next needs a frame (0 for never: it has not the keys), the caret turned when its time
        is up. The app asks after each pass and sleeps till then; nothing blinks in a window without the keys. Ten
        seconds after the last key or move the caret stops blinking and stays drawn, so a field left alone wakes nothing.
*/
static positive ui_text_tick(struct ui_text address_to t, positive now, b32 address_to redraw)
{
        positive due;

        if (!t->focused || t->read_only)
                return 0;
        if (now > t->awake_at && now - t->awake_at >= UI_TEXT_BLINK_FOR_NS)
        {
                if (t->caret_off)
                        t->caret_off = false, *redraw = true;
                return 0;
        }
        due = t->blink_at + UI_TEXT_BLINK_NS;
        if (now >= due)
        {
                t->caret_off = !t->caret_off;
                t->blink_at = now;
                *redraw = true;
                due = now + UI_TEXT_BLINK_NS;
        }
        return due;
}

// A selection's text to a selection of the clipboard; never a masked field's.
static fn ui_text_copy(struct ui_text address_to t, unsigned int which)
{
        p8 address_to text;
        positive room, length;

        if (t->masked)
                return;
        length = doc_copy_text(&t->doc, &text, &room);
        if (length && t->own)
                windows_clip_offer(t->own, which, text, length);
        array_store_release(text, room, length);
}

// The clipboard asked for a paste into the widget: its answer is the widget's, not another's of the same window.
static fn ui_text_ask(struct ui_text address_to t, unsigned int which)
{
        if (!t->own || t->read_only)
                return;
        windows_clip_ask(t->own, which);
        t->pasting = true;
}

/*
        Text pasted into the widget, as one undo. A field takes one line of what a person could type: a line end
        (LF, CR or both) or a tab is one blank, any other control and a byte that is no UTF-8 is left out, and then its
        rules hold, a paste with a character it refuses going in not at all and one too long cut to its room.
*/
static p32 ui_text_paste(struct ui_text address_to t, const p8 address_to bytes, positive length)
{
        p64 version = t->doc.version;
        p32 asks = 0;

        if (t->read_only)
                return 0;
        if (t->mode != UI_TEXT_FIELD)
                doc_paste(&t->doc, bytes, length);
        else
        {
                p8 address_to line = null;
                positive room = 0, used = 0, admitted;

                for (positive at = 0; at < length;)
                {
                        p32 code;
                        positive size = doc_decode(bytes + at, length - at, &code);

                        size = size ? size : 1;
                        if (!array_store_reserve(line, room, used, used + 4, 4096))
                                break;
                        if (code == '\r' && at + 1 < length && bytes[at + 1] == '\n')
                                size = 2;
                        if (code == '\n' || code == '\r' || code == '\t')
                                line[used++] = ' ';
                        else if (!(code & DOC_INVALID) && code >= 0x20 && code != 0x7f && (code < 0x80 || code >= 0xa0))
                                memory_copy(line + used, bytes + at, size), used += size;
                        at += size;
                }
                admitted = used ? ui_text_admit(t, line, used, true) : 0;
                if (admitted < used)
                        asks |= UI_TEXT_REFUSED;
                if (admitted)
                {
                        // The line has no line end left: the document's own normalising (a copy that is not wiped) is not wanted.
                        b32 single = t->doc.single_line, normal = t->doc.normalize_paste;

                        t->doc.single_line = t->doc.normalize_paste = false;
                        doc_paste(&t->doc, line, admitted);
                        t->doc.single_line = single, t->doc.normalize_paste = normal;
                }
                if (line)
                        memory_zero(line, room);
                array_store_release(line, room, used);
        }
        ui_text_wake(t);
        return asks | (t->doc.version != version ? UI_TEXT_CHANGED : UI_TEXT_MOVED);
}

// What the clipboard said: the paste this widget asked for, when it may be read.
static p32 ui_text_clip(struct ui_text address_to t, const struct window_event address_to event)
{
        if (event->b == WINDOWS_CLIP_TAKEN || event->b == WINDOWS_CLIP_DECLINED)
                windows_clip_done((unsigned int)event->a);
        if ((event->b == WINDOWS_CLIP_READY || event->b == WINDOWS_CLIP_REFUSED) && t->pasting)
        {
                p8 address_to bytes;
                unsigned long length;
                p32 asks = 0;

                t->pasting = false;
                if (event->b == WINDOWS_CLIP_READY && t->own && windows_clip_read(t->own, (unsigned int)event->a, &bytes, &length) > 0)
                {
                        asks = ui_text_paste(t, bytes, length);
                        windows_clip_free(bytes, length);
                }
                return asks;
        }
        return 0;
}

/*
        Whether a command is a field's: the moves along its line and the selections of it, the deletes, undo and redo,
        the clipboard, Enter (its submit) and Escape (its cancel), the menu. Any other key a field leaves to its app
        (Up and Down, Tab, a page, find, save, a chord the app means), which does with it what the app does.
*/
static b32 ui_text_field_command(p32 command)
{
        switch (command)
        {
        case DOC_CMD_LEFT: case DOC_CMD_RIGHT: case DOC_CMD_WORD_LEFT: case DOC_CMD_WORD_RIGHT: case DOC_CMD_HOME: case DOC_CMD_END:
        case DOC_CMD_TOP: case DOC_CMD_BOTTOM: case DOC_CMD_SELECT_LEFT: case DOC_CMD_SELECT_RIGHT: case DOC_CMD_SELECT_WORD_LEFT:
        case DOC_CMD_SELECT_WORD_RIGHT: case DOC_CMD_SELECT_HOME: case DOC_CMD_SELECT_END: case DOC_CMD_SELECT_TOP:
        case DOC_CMD_SELECT_BOTTOM: case DOC_CMD_BACKSPACE: case DOC_CMD_DELETE: case DOC_CMD_DELETE_WORD_LEFT:
        case DOC_CMD_DELETE_WORD_RIGHT: case DOC_CMD_DELETE_LINE_LEFT: case DOC_CMD_DELETE_LINE_RIGHT: case DOC_CMD_NEWLINE:
        case DOC_CMD_TRANSPOSE: case DOC_CMD_UNDO: case DOC_CMD_REDO: case DOC_CMD_CUT: case DOC_CMD_COPY: case DOC_CMD_PASTE:
        case DOC_CMD_SELECT_ALL: case DOC_CMD_SELECT_LINE: case DOC_CMD_EXPAND: case DOC_CMD_SHRINK: case DOC_CMD_SINGLE:
        case DOC_CMD_MENU:
                return true;
        }
        return false;
}

// A masked field has no words to tell apart: a word's move, delete or selection is the whole line's.
static p32 ui_text_masked_command(p32 command)
{
        switch (command)
        {
        case DOC_CMD_WORD_LEFT: return DOC_CMD_HOME;
        case DOC_CMD_WORD_RIGHT: return DOC_CMD_END;
        case DOC_CMD_SELECT_WORD_LEFT: return DOC_CMD_SELECT_HOME;
        case DOC_CMD_SELECT_WORD_RIGHT: return DOC_CMD_SELECT_END;
        case DOC_CMD_DELETE_WORD_LEFT: return DOC_CMD_DELETE_LINE_LEFT;
        case DOC_CMD_DELETE_WORD_RIGHT: return DOC_CMD_DELETE_LINE_RIGHT;
        case DOC_CMD_EXPAND: return DOC_CMD_SELECT_ALL;
        case DOC_CMD_TRANSPOSE: return DOC_CMD_NONE;
        }
        return command;
}

/*
        A command of the keymap that needs the view or the clipboard; the rest go to text.c's doc_command. Answers 0
        for a command that is not the widget's (a field's app does it), else what changed (UI_TEXT_MOVED at least).
*/
static p32 ui_text_command(struct ui address_to ui, struct ui_text address_to t, p32 command)
{
        p64 version = t->doc.version;
        int page = ui_text_rows(t) > 1 ? ui_text_rows(t) - 1 : 1;

        if (t->mode == UI_TEXT_FIELD && !ui_text_field_command(command))
                return 0;
        if (t->masked && (command = ui_text_masked_command(command)) == DOC_CMD_NONE)
                return UI_TEXT_MOVED;
        switch (command)
        {
        case DOC_CMD_COPY:
                ui_text_copy(t, WINDOWS_CLIP_CLIPBOARD);
                return UI_TEXT_MOVED;
        case DOC_CMD_CUT:
                // A masked field's text is never copied out, and so never cut out.
                if (t->read_only || t->masked)
                        return UI_TEXT_MOVED;
                ui_text_copy(t, WINDOWS_CLIP_CLIPBOARD);
                doc_erase_selections(&t->doc);
                break;
        case DOC_CMD_PASTE:
                ui_text_ask(t, WINDOWS_CLIP_CLIPBOARD);
                return UI_TEXT_PASTE;
        case DOC_CMD_PAGE_UP: case DOC_CMD_PAGE_DOWN: case DOC_CMD_SELECT_PAGE_UP: case DOC_CMD_SELECT_PAGE_DOWN:
        {
                b32 up = command == DOC_CMD_PAGE_UP || command == DOC_CMD_SELECT_PAGE_UP;

                doc_move_vertical(&t->doc, up ? -page : page, command == DOC_CMD_SELECT_PAGE_UP || command == DOC_CMD_SELECT_PAGE_DOWN);
                t->top_line = up ? (t->top_line > (p64)page ? t->top_line - (p64)page : 0) : t->top_line + (p64)page;
                break;
        }
        case DOC_CMD_ZOOM_IN: case DOC_CMD_ZOOM_OUT: case DOC_CMD_ZOOM_RESET:
                if (t->mode != UI_TEXT_CODE)
                        return 0;
                t->zoom = command == DOC_CMD_ZOOM_RESET ? 0 : t->zoom + (command == DOC_CMD_ZOOM_IN ? 1 : -1);
                t->zoom = t->zoom < -6 ? -6 : t->zoom > 40 ? 40 : t->zoom;
                ui_text_metrics(ui, t);
                return UI_TEXT_MOVED;
        case DOC_CMD_WRAP:
                if (t->mode == UI_TEXT_FIELD)
                        return 0;
                t->wrap = !t->wrap;
                return UI_TEXT_MOVED;
        case DOC_CMD_MENU:
        {
                // The menu at the caret, for a key that asks for it.
                static struct ui_text_layout l;
                p64 caret = t->doc.selections[t->doc.primary].active, line = doc_line_of(&t->doc, caret);

                ui_text_layout_line(ui, t, line, &l);
                t->menu_x = t->left + ui_text_x_of(&l, caret) - (int)t->scroll_x;
                t->menu_y = t->top + (int)(line >= t->top_line ? line - t->top_line + 1 : 1) * t->line_height;
                return UI_TEXT_MENU;
        }
        case DOC_CMD_FOLD: case DOC_CMD_UNFOLD:
                doc_fold(&t->doc, doc_line_of(&t->doc, t->doc.selections[t->doc.primary].active), command == DOC_CMD_FOLD);
                return UI_TEXT_MOVED;
        default:
                if (t->read_only && command >= DOC_CMD_BACKSPACE && command <= DOC_CMD_SORT_DESCENDING)
                        return 0;
                if (t->mode == UI_TEXT_FIELD && command == DOC_CMD_NEWLINE)
                        return UI_TEXT_SUBMIT;
                if (!doc_command(&t->doc, command, t->comment))
                {
                        t->command = command;
                        return UI_TEXT_COMMAND;
                }
        }
        ui_text_wake(t);
        ui_text_reveal(ui, t);
        return t->doc.version != version ? UI_TEXT_CHANGED : UI_TEXT_MOVED;
}

// A field whose app does not take Up and Down: they are Home and End (Shift selects), as a single line's ends are on
// the desktop. Any other command is left as it is.
static p32 ui_text_as_line(p32 command)
{
        switch (command)
        {
        case DOC_CMD_UP: return DOC_CMD_HOME;
        case DOC_CMD_DOWN: return DOC_CMD_END;
        case DOC_CMD_SELECT_UP: return DOC_CMD_SELECT_HOME;
        case DOC_CMD_SELECT_DOWN: return DOC_CMD_SELECT_END;
        }
        return command;
}

// A key: the keymap's command, or the character typed. AltGr always types; a key with Control or Alt that is no
// binding is not typed.
static p32 ui_text_key(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event)
{
        p32 flags = event->flags, character = (p32)event->b, modifiers = 0, command;

        if (!(flags & WINDOW_KEY_DOWN))
                return 0;
        if (!(flags & WINDOW_KEY_ALTGR))
        {
                modifiers = (flags & WINDOW_KEY_SHIFT ? DOC_MOD_SHIFT : 0) | (flags & WINDOW_KEY_CONTROL ? DOC_MOD_CONTROL : 0) |
                            (flags & WINDOW_KEY_ALT ? DOC_MOD_ALT : 0) | (flags & WINDOW_KEY_SUPER ? DOC_MOD_SUPER : 0);
                command = t->keymap ? doc_keymap_find(t->keymap, (p32)event->a, character, modifiers) : DOC_CMD_NONE;
                if (t->mode == UI_TEXT_FIELD && !t->keeps_vertical)
                        command = ui_text_as_line(command);
                // Escape in a field is its cancel, for its app to do (leave it, clear it, close the window).
                if (command == DOC_CMD_SINGLE && t->mode == UI_TEXT_FIELD && t->doc.selection_count == 1)
                        return UI_TEXT_CANCEL;
                if (command != DOC_CMD_NONE)
                        return ui_text_command(ui, t, command);
                if (modifiers & (DOC_MOD_CONTROL | DOC_MOD_ALT | DOC_MOD_SUPER))
                        return 0;
        }
        // A character: any but a control (a blank is one like any other); a C1 control is no character typed either.
        if (t->read_only || character < 0x20 || character == 0x7f || (character >= 0x80 && character < 0xa0) || character > 0x10ffff)
                return 0;
        {
                p8 bytes[4];
                positive size = memory_utf8_encode(bytes, sizeof(bytes), character);
                p64 version = t->doc.version;

                // A field's rules: a character it refuses, or one past its room, is not typed, and it says why.
                if (t->mode == UI_TEXT_FIELD && ui_text_admit(t, bytes, size, false) < size)
                {
                        ui_text_wake(t);
                        return UI_TEXT_REFUSED | UI_TEXT_MOVED;
                }
                t->doc.now = clock_monotonic_nanoseconds();
                doc_type(&t->doc, bytes, size);
                ui_text_wake(t);
                ui_text_reveal(ui, t);
                return t->doc.version != version ? UI_TEXT_CHANGED : UI_TEXT_MOVED;
        }
}

/*
        The pointer and the wheel. x and y are the window's device pixels; the widget's box is where it was last drawn.
        Answers what changed, and UI_TEXT_MENU for the right button (menu_x and menu_y say where).
*/
// A finger's long press (the compositor's right button, WINDOW_KEY_HOLD): the word under it is selected, and the drag after it
// is by words from there (drag_kind 2) until the lift. A masked field has no words, so its whole text is taken, as a double
// click takes it.
static p32 ui_text_hold(struct ui address_to ui, struct ui_text address_to t, int x, int y)
{
        p64 at = ui_text_hit(ui, t, x, y), start = at, finish = at;

        if (t->masked)
                start = 0, finish = doc_length(&t->doc);
        else
                doc_word_at(&t->doc, at, &start, &finish);
        ui_drag_stop(&t->kinetic);
        doc_select_set(&t->doc, start, finish);
        t->touch = false, t->holding = true, t->dragging = true, t->drag_box = false;
        t->drag_kind = 2, t->drag_start = start, t->drag_end = finish;
        ui_text_reveal(ui, t);
        ui_text_wake(t);
        return UI_TEXT_MOVED;
}

static p32 ui_text_pointer(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y)
{
        b32 inside = x >= t->well_left && x < t->well_right && y >= t->well_top && y < t->well_bottom;
        b32 shift = (event->flags & WINDOW_KEY_SHIFT) != 0, alt = (event->flags & WINDOW_KEY_ALT) != 0 && t->mode != UI_TEXT_FIELD;
        positive now = clock_monotonic_nanoseconds();

        if (event->type == WINDOW_EVENT_WHEEL)
        {
                if (!inside)
                        return 0;
                if ((event->flags & WINDOW_KEY_CONTROL) && t->mode == UI_TEXT_CODE)
                        return ui_text_command(ui, t, event->a > 0 ? DOC_CMD_ZOOM_IN : DOC_CMD_ZOOM_OUT);
                if (event->b)
                        t->scroll_x = t->scroll_x - (b64)event->a * t->cell * 3 / 120 > 0 ? t->scroll_x - (b64)event->a * t->cell * 3 / 120 : 0;
                else
                {
                        b64 lines = -(b64)event->a * 3 / 120, to = (b64)t->top_line + (lines ? lines : event->a < 0 ? 1 : -1);

                        t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
                }
                return UI_TEXT_MOVED;
        }
        if (event->type == WINDOW_EVENT_BUTTON)
        {
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

                // A finger held still (the compositor's long press): its right button selects the word under it, and the moves
                // after it extend the selection by words. The press's own cancel comes before that button and is no lift; the
                // left release after it is the lift, which ends the drag and keeps the selection.
                if (event->a == 0x111 && (event->flags & WINDOW_KEY_HOLD))
                        return down && inside ? ui_text_hold(ui, t, x, y) : 0;
                if (event->a == 0x110 && (event->flags & WINDOW_KEY_HOLD) && !down)
                {
                        b32 lifted = t->dragging && t->holding;

                        t->dragging = t->holding = false;
                        if (!lifted)
                                return 0;
                        if (doc_any_selected(&t->doc))
                                ui_text_copy(t, WINDOWS_CLIP_PRIMARY);
                        return UI_TEXT_MOVED;
                }
                if (!down)
                {
                        b32 was = t->dragging;

                        t->dragging = false;
                        if (t->touch && ui_drag_let_go(&t->kinetic, now))
                                return UI_TEXT_MOVED;
                        if (was && doc_any_selected(&t->doc))
                                ui_text_copy(t, WINDOWS_CLIP_PRIMARY);
                        return 0;
                }
                if (!inside)
                        return 0;
                if (event->a == 0x111)
                {
                        p64 at = ui_text_hit(ui, t, x, y);
                        b32 in_selection = false;

                        for (positive s = 0; s < t->doc.selection_count; s++)
                                in_selection |= at >= doc_sel_start(&t->doc.selections[s]) && at <= doc_sel_end(&t->doc.selections[s]) &&
                                                doc_any_selected(&t->doc);
                        if (!in_selection)
                                doc_select_set(&t->doc, at, at);
                        t->menu_x = x, t->menu_y = y;
                        return UI_TEXT_MENU | UI_TEXT_MOVED;
                }
                if (event->a == 0x112)
                {
                        p64 at = ui_text_hit(ui, t, x, y);

                        if (t->read_only || !t->own)
                                return 0;
                        doc_select_set(&t->doc, at, at);
                        ui_text_ask(t, WINDOWS_CLIP_PRIMARY);
                        ui_text_wake(t);
                        return UI_TEXT_PASTE | UI_TEXT_MOVED;
                }
                if (event->a != 0x110)
                        return 0;
                t->touch = (event->flags & WINDOW_KEY_TOUCH) != 0;
                if (t->touch)
                {
                        // A finger: a caret where it lands, and a drag scrolls rather than selects.
                        ui_drag_start(&t->kinetic, y, now);
                        doc_select_set(&t->doc, ui_text_hit(ui, t, x, y), ui_text_hit(ui, t, x, y));
                        t->dragging = true;
                        ui_text_wake(t);
                        return UI_TEXT_MOVED;
                }
                {
                        b32 again = now - t->click_at < UI_TEXT_CLICKS_NS && (t->click_x - x) * (t->click_x - x) + (t->click_y - y) * (t->click_y - y) <= 16 * ui->scale * ui->scale;
                        p64 at = ui_text_hit(ui, t, x, y), start = at, finish = at;

                        t->clicks = again ? t->clicks % 3 + 1 : 1;
                        t->click_at = now, t->click_x = x, t->click_y = y;
                        t->drag_box = false;
                        // A masked field has no words: a double click takes it all, as a triple does.
                        if (t->clicks == 2 && t->masked)
                                start = 0, finish = doc_length(&t->doc);
                        else if (t->clicks == 2)
                                doc_word_at(&t->doc, at, &start, &finish);
                        else if (t->clicks == 3)
                        {
                                p64 line = doc_line_of(&t->doc, at);

                                start = doc_line_start(&t->doc, line);
                                finish = doc_line_start(&t->doc, line + 1);
                        }
                        if (alt && shift && t->mode != UI_TEXT_FIELD)
                        {
                                t->drag_box = true;
                                t->box_line = doc_line_of(&t->doc, at);
                                t->box_column = doc_column(&t->doc, at);
                                doc_select_box(&t->doc, t->box_line, t->box_column, t->box_line, t->box_column);
                        }
                        else if (alt && t->clicks == 1)
                                doc_select_add(&t->doc, at, at);
                        else if (shift && t->clicks == 1)
                        {
                                struct doc_selection address_to primary = &t->doc.selections[t->doc.primary];

                                primary->active = at;
                                doc_normalize(&t->doc);
                                doc_seal(&t->doc);
                        }
                        else
                                doc_select_set(&t->doc, start, finish);
                        t->drag_kind = t->clicks;
                        t->drag_start = start, t->drag_end = finish;
                        t->dragging = true;
                        ui_text_wake(t);
                        return UI_TEXT_MOVED;
                }
        }
        if (event->type == WINDOW_EVENT_MOTION && t->dragging)
        {
                if (t->touch)
                {
                        int moved = ui_drag_follow(&t->kinetic, y, now), lines = t->line_height ? -moved / t->line_height : 0;
                        b64 to = (b64)t->top_line + lines;

                        if (lines)
                                t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
                        return lines ? UI_TEXT_MOVED : 0;
                }
                {
                        p64 at = ui_text_hit(ui, t, x, y), start = at, finish = at;
                        struct doc_selection address_to primary = &t->doc.selections[t->doc.primary];

                        if (t->drag_box)
                        {
                                doc_select_box(&t->doc, t->box_line, t->box_column, doc_line_of(&t->doc, at), doc_column(&t->doc, at));
                                return UI_TEXT_MOVED;
                        }
                        if (t->drag_kind == 2)
                                doc_word_at(&t->doc, at, &start, &finish);
                        else if (t->drag_kind == 3)
                        {
                                p64 line = doc_line_of(&t->doc, at);

                                start = doc_line_start(&t->doc, line), finish = doc_line_start(&t->doc, line + 1);
                        }
                        // The selection grows by its kind from where it began: back to the start of a word or a line
                        // before it, on to the end of one after it.
                        if (start < t->drag_start)
                                primary->anchor = t->drag_end, primary->active = start;
                        else
                                primary->anchor = t->drag_start, primary->active = finish > t->drag_end ? finish : t->drag_end;
                        if (t->drag_kind == 1)
                                primary->active = at;
                        doc_normalize(&t->doc);
                        ui_text_reveal(ui, t);
                        ui_text_wake(t);
                        return UI_TEXT_MOVED;
                }
        }
        return 0;
}

// A thrown finger's scroll, a frame at a time: whether it still moves.
static b32 ui_text_fling(struct ui_text address_to t, positive now)
{
        int moved = ui_drag_step(&t->kinetic, now), lines = t->line_height ? -moved / t->line_height : 0;
        b64 to = (b64)t->top_line + lines;

        if (lines)
                t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
        return t->kinetic.flying;
}

// Any event of the window the widget may take: keys only while it has the keys; the pointer within its box.
static p32 ui_text_event(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y)
{
        t->asks = 0;
        if (event->type == WINDOW_EVENT_KEY)
                return t->focused ? t->asks = ui_text_key(ui, t, event) : 0;
        if (event->type == WINDOW_EVENT_CLIP)
                return t->asks = ui_text_clip(t, event);
        return t->asks = ui_text_pointer(ui, t, event, x, y);
}

/*
        The context menu of a text widget, the same in every window: Undo, Redo, Cut, Copy, Paste, Delete and Select
        All, each one there and greyed when it can do nothing (nothing selected, nothing to undo, a masked field's
        text that is never copied out, a widget that is read only). ui_text_menu_open opens it in the app's popup at a
        point of its window; the app's chose hands the id back to ui_text_menu_do, for the widget the popup names.
*/
#define UI_TEXT_MENU_UNDO 1
#define UI_TEXT_MENU_REDO 2
#define UI_TEXT_MENU_CUT 3
#define UI_TEXT_MENU_COPY 4
#define UI_TEXT_MENU_PASTE 5
#define UI_TEXT_MENU_DELETE 6
#define UI_TEXT_MENU_ALL 7

// The keys each item of a text menu shows, by its id: the keymap's first key for the item's command (doc_keymap_name),
// kept here while the menu is open. Every widget reads the one keymap, so a command's words are the same in each.
static char ui_text_menu_keys[UI_TEXT_MENU_ALL + 1][40];

static const char address_to ui_text_menu_key(const struct ui_text address_to t, int id, p32 command)
{
        if (!t->keymap || !doc_keymap_name(t->keymap, command, ui_text_menu_keys[id], sizeof(ui_text_menu_keys[id])))
                return null;
        return ui_text_menu_keys[id];
}

static fn ui_text_menu_items(struct ui_popmenu address_to m, const struct ui_text address_to t)
{
        p32 none = doc_any_selected(&t->doc) ? 0 : UI_MENU_DISABLED, fixed = t->read_only ? UI_MENU_DISABLED : 0;
        p32 hidden = t->masked ? UI_MENU_DISABLED : 0;

        ui_popmenu_add(m, "&Undo", ui_text_menu_key(t, UI_TEXT_MENU_UNDO, DOC_CMD_UNDO),
                       t->doc.done && !fixed ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_UNDO);
        ui_popmenu_add(m, "&Redo", ui_text_menu_key(t, UI_TEXT_MENU_REDO, DOC_CMD_REDO),
                       t->doc.done < t->doc.group_count && !fixed ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_REDO);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, "Cu&t", ui_text_menu_key(t, UI_TEXT_MENU_CUT, DOC_CMD_CUT), none | fixed | hidden, UI_TEXT_MENU_CUT);
        ui_popmenu_add(m, "&Copy", ui_text_menu_key(t, UI_TEXT_MENU_COPY, DOC_CMD_COPY), none | hidden, UI_TEXT_MENU_COPY);
        ui_popmenu_add(m, "&Paste", ui_text_menu_key(t, UI_TEXT_MENU_PASTE, DOC_CMD_PASTE),
                       fixed | (t->own ? 0 : UI_MENU_DISABLED), UI_TEXT_MENU_PASTE);
        ui_popmenu_add(m, "&Delete", ui_text_menu_key(t, UI_TEXT_MENU_DELETE, DOC_CMD_DELETE), none | fixed, UI_TEXT_MENU_DELETE);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, "Select &All", ui_text_menu_key(t, UI_TEXT_MENU_ALL, DOC_CMD_SELECT_ALL),
                       doc_length(&t->doc) ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_ALL);
}

static b32 ui_text_menu_open(struct ui_popup address_to p, struct ui_text address_to t, const struct ui address_to ui, int x, int y, b32 touch)
{
        if (!p || !t->own)
                return false;
        p->menu.count = 0;
        ui_text_menu_items(&p->menu, t);
        p->field = t;
        return ui_popup_open(p, t->own, ui, x, y, 0, 0, WINDOW_POPUP_POINT, touch);
}

// What an item of the menu does, to the widget it was opened for: what changed, as ui_text_event answers.
static p32 ui_text_menu_do(struct ui address_to ui, struct ui_text address_to t, int id)
{
        static const p32 commands[] = {DOC_CMD_NONE, DOC_CMD_UNDO, DOC_CMD_REDO, DOC_CMD_CUT, DOC_CMD_COPY, DOC_CMD_PASTE,
                                       DOC_CMD_DELETE, DOC_CMD_SELECT_ALL};

        if (!t || id <= 0 || id > UI_TEXT_MENU_ALL)
                return 0;
        return ui_text_command(ui, t, commands[id]);
}

// Whether a point of the window (device pixels) is on the widget: a field's whole well, as it was last drawn.
static b32 ui_text_inside(const struct ui_text address_to t, int x, int y)
{
        return x >= t->well_left && x < t->well_right && y >= t->well_top && y < t->well_bottom;
}

/*
        A field's events, the one call an app makes for one: the keys while it has them, the pointer on its well (a
        press of any button there, and the motion and the release of a drag it began), the clipboard's answer to a
        paste it asked for, and the context menu it asks for, opened at once in menu (the app's popup; none: no
        menu). The wheel is never a field's. Answers what changed (UI_TEXT_*); 0 when the event is not the field's,
        which the app then does as its own: Enter is SUBMIT and Escape CANCEL for it to act on, and a key a field does
        not take (Up, Down, Tab, a page, a chord) comes back 0.
*/
static p32 ui_field_event(struct ui address_to ui, struct ui_text address_to t, struct ui_popup address_to menu,
                          const struct window_event address_to event, int x, int y)
{
        p32 asks;
        b32 ending = false;

        if (event->type == WINDOW_EVENT_WHEEL || (event->type == WINDOW_EVENT_MOTION && !t->dragging))
                return 0;
        if (event->type == WINDOW_EVENT_BUTTON)
        {
                if ((event->flags & WINDOW_KEY_DOWN) ? !ui_text_inside(t, x, y) : !t->dragging)
                        return 0;
                ending = !(event->flags & WINDOW_KEY_DOWN);
        }
        asks = ui_text_event(ui, t, event, x, y);
        if (ending)
                asks |= UI_TEXT_MOVED;
        if (asks & UI_TEXT_MENU)
                ui_text_menu_open(menu, t, ui, t->menu_x, t->menu_y, (event->flags & WINDOW_KEY_TOUCH) != 0);
        return asks;
}

/* ---- drawing ---- */

// The colour of a role of the tokenizer's, from the kit's roles: the text, and the accent and status colours for the rest.
static unsigned int ui_text_role(int role)
{
        switch (role)
        {
        case DOC_ROLE_KEYWORD: return ui_role(UI_PRIMARY);
        case DOC_ROLE_TYPE: return ui_role(UI_INFO);
        case DOC_ROLE_STRING: return ui_role(UI_SUCCESS);
        case DOC_ROLE_NUMBER: return ui_role(UI_WARNING);
        case DOC_ROLE_COMMENT: return ui_role(UI_ON_SURFACE_VARIANT);
        case DOC_ROLE_PREPROCESSOR: return ui_role(UI_WARNING);
        case DOC_ROLE_FUNCTION: return ui_role(UI_ON_SURFACE);
        case DOC_ROLE_VARIABLE: return ui_role(UI_INFO);
        case DOC_ROLE_INVALID: return ui_role(UI_ERROR);
        default: return ui_role(UI_ON_SURFACE);
        }
}

// Whether offset is inside any selection (an empty one: none).
static b32 ui_text_selected(const struct ui_text address_to t, p64 offset)
{
        for (positive s = 0; s < t->doc.selection_count; s++)
        {
                const struct doc_selection address_to sel = &t->doc.selections[s];

                if (offset >= doc_sel_start(sel) && offset < doc_sel_end(sel))
                        return true;
        }
        return false;
}

// A role a range's data names, for a line's ground, a highlight and a gutter's mark: 0 the accent, 1 an error, 2 a
// warning, 3 news.
static unsigned int ui_text_range_role(p32 data)
{
        return ui_role(data == 1 ? UI_ERROR : data == 2 ? UI_WARNING : data == 3 ? UI_INFO : UI_PRIMARY);
}

// The paper under an offset of a code view's line: the line's own, a highlight's over it, the selection's over both.
static unsigned int ui_text_paper(const struct ui_text address_to t, p64 offset, unsigned int ground)
{
        unsigned int selection = ui_role(UI_SELECTION);

        for (positive r = 0; r < t->doc.range_count; r++)
                if (t->doc.ranges[r].kind == DOC_RANGE_HIGHLIGHT && offset >= t->doc.ranges[r].start && offset < t->doc.ranges[r].finish)
                {
                        ground = window_mix(ground, ui_text_range_role(t->doc.ranges[r].data) & 0xffffffu, 64);
                        break;
                }
        return ui_text_selected(t, offset) ? window_mix(ground, selection & 0xffffffu, (selection >> 24) * 256 / 255) : ground;
}

// The columns a line's indent reaches: its leading blanks, a tab to its next stop; a blank line takes the indent of the
// next line that is not blank (a screen's worth on at most), so the guides go on through it.
static p64 ui_text_indent(const struct ui_text address_to t, p64 line)
{
        p64 lines = doc_line_count(&t->doc);

        for (p64 probe = line; probe < lines && probe < line + 64; probe++)
        {
                p64 at = doc_line_start(&t->doc, probe), stop = doc_line_end(&t->doc, probe), column = 0;
                p8 bytes[256];
                positive got = doc_read(&t->doc, at, bytes, stop - at < sizeof(bytes) ? (positive)(stop - at) : sizeof(bytes));
                positive k = 0;

                while (k < got && (bytes[k] == ' ' || bytes[k] == '\t'))
                        column = bytes[k++] == '\t' ? (column / t->doc.tab_size + 1) * t->doc.tab_size : column + 1;
                if (k < got || got == sizeof(bytes))
                        return column;
        }
        return 0;
}

/*
        A composition as it is drawn: the text it is making, or for a masked field a dot for each character of it, so
        what is typed for a passphrase is not on the screen while it is made. Whole and NUL-terminated in into.
*/
static positive ui_text_preedit_shown(const struct ui_text address_to t, p8 address_to into)
{
        positive length = 0;

        if (!t->masked)
        {
                memory_copy(into, t->preedit, t->preedit_length);
                length = t->preedit_length;
        }
        for (positive at = 0; t->masked && at < t->preedit_length;)
        {
                p32 code;
                positive size = doc_decode(t->preedit + at, t->preedit_length - at, &code);

                memory_copy(into + length, "\xe2\x80\xa2", 3);
                length += 3;
                at += size ? size : 1;
        }
        into[length] = 0;
        return length;
}

/*
        One laid line on the picture at its baseline: the selection behind it, then the text, then each caret on it.
        Proportional: the kit's text, cut to the box. The code view: whole cells, a run of one colour at a time, on the
        paper of its line (ground: the current line's, a line range's) and a highlight's; then the indent's guides,
        a selection's blanks (a dot a space, a line a tab) and the squiggles.
*/
static fn ui_text_draw_line(struct ui address_to ui, struct ui_text address_to t, const struct ui_text_layout address_to l, p64 line,
                            int y, int baseline, unsigned int ground)
{
        int origin = t->left - (int)t->scroll_x;
        unsigned int selection = ui_well(ui, UI_SELECTION, 40), text = ui_text_colour(ui, UI_ON_SURFACE);
        static struct doc_token tokens[512];
        positive token_count = t->mode == UI_TEXT_CODE ? doc_tokens(&t->doc, line, tokens, 512) : 0;

        // A highlight (a match found, a word's other places) under the text, as the code view's cells are told their
        // paper is (ui_text_paper).
        for (positive r = 0; r < t->doc.range_count; r++)
        {
                const struct doc_range address_to range = &t->doc.ranges[r];

                if (range->kind != DOC_RANGE_HIGHLIGHT || range->finish <= l->offset[0] || range->start >= l->offset[l->count])
                        continue;
                ui_fill(ui, origin + ui_text_x_of(l, range->start), y, ui_text_x_of(l, range->finish) - ui_text_x_of(l, range->start),
                        t->line_height, 0xff000000u | window_mix(ground, ui_text_range_role(range->data) & 0xffffffu, 64));
        }
        // The selection: one rectangle a run of selected clusters, and a sliver past the end of a line the selection
        // goes on from.
        for (p32 k = 0; k < l->count;)
        {
                p32 from = k;

                while (k < l->count && ui_text_selected(t, l->offset[k]))
                        k++;
                if (k > from)
                        ui_fill(ui, origin + l->x[from], y, l->x[k] - l->x[from], t->line_height,
                                ui_tone(selection & 0xffffffu, selection >> 24));
                else
                        k++;
        }
        if (ui_text_selected(t, l->offset[l->count]) && t->mode != UI_TEXT_FIELD)
                ui_fill(ui, origin + l->x[l->count], y, t->cell / 2 + ui->scale, t->line_height, ui_tone(selection & 0xffffffu, selection >> 24));

        if (t->mode != UI_TEXT_CODE)
                ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, origin, baseline, (string_address)l->shown,
                          t->size + t->zoom, text & 0xffffffu, 255, t->left, t->right, 0);
        else
        {
                // A run is the clusters of one role; each cluster of a single code point one cell is part of a run drawn
                // with font_cells, any other (marks, wide, emoji) a font_draw of its own at its cell.
                const font_face address_to faces[2] = {ui_text_face(ui, t), ui->regular};
                font_target target = {ui->pixels, ui->pitch, ui->width, ui->bottom};
                font_metrics grid = font_metrics_at(faces[0], ui_text_em(ui, t));
                p32 codes[256];
                positive token = 0;

                for (p32 k = 0; k < l->count;)
                {
                        p64 offset = l->offset[k] - l->offset[0];
                        unsigned int ink = text;
                        p32 run = 0, start = k;

                        while (token < token_count && tokens[token].start + tokens[token].length <= offset)
                                token++;
                        if (token < token_count && tokens[token].start <= offset)
                                ink = ui_text_role(tokens[token].role);
                        while (k < l->count && run < 256)
                        {
                                p32 code;
                                positive size = l->shown_at[k + 1] - l->shown_at[k];
                                p64 here = l->offset[k] - l->offset[0];
                                unsigned int ink_here = text;
                                positive look = token;

                                while (look < token_count && tokens[look].start + tokens[look].length <= here)
                                        look++;
                                if (look < token_count && tokens[look].start <= here)
                                        ink_here = ui_text_role(tokens[look].role);
                                if (ink_here != ink || (k > start && ui_text_paper(t, l->offset[k], ground) != ui_text_paper(t, l->offset[start], ground)))
                                        break;
                                if (doc_decode(l->shown + l->shown_at[k], size, &code) != size || l->x[k + 1] - l->x[k] != t->cell)
                                {
                                        if (k > start)
                                                break;
                                        if (origin + l->x[k] >= t->left - t->cell && origin + l->x[k] < t->right)
                                                font_draw(ui->engine, faces, 2, l->shown + l->shown_at[k], size, ui_text_em(ui, t), 0, &target,
                                                          (b32)((b64)(origin + l->x[k]) * 65536), baseline, ink & 0xffffffu, FONT_UNKNOWN);
                                        k++;
                                        run = 0;
                                        goto next;
                                }
                                codes[run++] = code;
                                k++;
                        }
                        if (run && origin + l->x[start] < t->right && origin + l->x[k] > t->left - t->cell)
                        {
                                unsigned int paper = ui_text_paper(t, l->offset[start], ground);

                                font_cells(ui->engine, faces, 2, codes, run, &grid, ui_text_em(ui, t), 0, &target, origin + l->x[start],
                                           y + (t->line_height - grid.cell_height) / 2, ink & 0xffffffu, paper);
                        }
                next:;
                }
        }
        if (t->mode == UI_TEXT_CODE && t->doc.tab_size)
        {
                // The indent's guides: a line a pixel wide at each tab stop inside the indent, at the left of its cell.
                p64 indent = ui_text_indent(t, line);
                unsigned int guide = ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 36), blank = ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 140);

                for (p64 column = t->doc.tab_size; column < indent; column += t->doc.tab_size)
                {
                        int at = origin + (int)column * t->cell;

                        if (at >= t->left && at < t->right)
                                ui_fill(ui, at, y, ui->scale, t->line_height, guide);
                }
                // A selection's blanks, seen: a dot in a space's cell, a line along a tab's.
                for (p32 k = 0; k < l->count; k++)
                {
                        p8 c = l->shown[l->shown_at[k]];
                        int x0 = origin + l->x[k], x1 = origin + l->x[k + 1], mid = y + t->line_height / 2;

                        if ((c != ' ' && c != '\t') || !ui_text_selected(t, l->offset[k]) || x1 <= t->left || x0 >= t->right)
                                continue;
                        if (c == ' ')
                                ui_fill(ui, (x0 + x1) / 2 - ui->scale / 2, mid - ui->scale / 2, ui->scale, ui->scale, blank);
                        else
                                ui_fill(ui, x0 + t->cell / 4, mid, x1 - x0 - t->cell / 2, ui->scale, blank);
                }
        }
        // Squiggles under what a diagnostic marks: a wave in the error's or the warning's colour.
        for (positive r = 0; r < t->doc.range_count; r++)
        {
                const struct doc_range address_to range = &t->doc.ranges[r];
                unsigned int colour;

                if (range->kind != DOC_RANGE_ERROR && range->kind != DOC_RANGE_WARNING && range->kind != DOC_RANGE_INFO)
                        continue;
                if (range->finish <= l->offset[0] || range->start > l->offset[l->count])
                        continue;
                colour = ui_role(range->kind == DOC_RANGE_ERROR ? UI_ERROR : range->kind == DOC_RANGE_WARNING ? UI_WARNING : UI_INFO);
                for (int x = origin + ui_text_x_of(l, range->start), stop = origin + ui_text_x_of(l, range->finish); x < stop; x++)
                        ui_fill(ui, x, baseline + 2 * ui->scale + ((x / (2 * ui->scale)) & 1) * ui->scale, 1, ui->scale, ui_tone(colour & 0xffffffu, 255));
        }
        // The carets on this line, in the accent, unless it is the blink's off half; a composition's text after the
        // primary, underlined. A bar caret stands on the edge between two characters, half in each one's bearing: drawn
        // from the edge rightwards it covered the next letter's stem (a caret before "beta" read "peta").
        if (t->focused && !t->caret_off)
                for (positive s = 0; s < t->doc.selection_count; s++)
                {
                        p64 caret = t->doc.selections[s].active;

                        if (caret < l->offset[0] || caret > l->offset[l->count])
                                continue;
                        ui_fill(ui, origin + ui_text_x_of(l, caret) - (t->doc.overwrite ? 0 : ui->scale), y + ui->scale,
                                t->doc.overwrite ? t->cell : 2 * ui->scale,
                                t->line_height - 2 * ui->scale, ui_tone(ui_role(UI_PRIMARY) & 0xffffffu, t->doc.overwrite ? 110 : 255));
                }
        if (t->preedit_length && t->focused)
        {
                p64 caret = t->doc.selections[t->doc.primary].active;

                if (caret >= l->offset[0] && caret <= l->offset[l->count])
                {
                        int x = origin + ui_text_x_of(l, caret);
                        p8 shown[sizeof(t->preedit) * 3];
                        int wide;

                        ui_text_preedit_shown(t, shown);
                        wide = ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, x, baseline, shown,
                                         t->size + t->zoom, text & 0xffffffu, 255, t->left, t->right, 0);

                        ui_fill(ui, x, baseline + 2 * ui->scale, wide, ui->scale, ui_tone(text & 0xffffffu, 255));
                }
        }
}

/*
        The widget drawn into the box x, y, width by height (device pixels), its ground already there (a field's well,
        a window's paper): ground is that colour, for the code view's cells. The rows from the top line down, wrapped
        when wrap is on, with the gutter's line numbers on the left of a code view.
*/
static fn ui_text_draw(struct ui address_to ui, struct ui_text address_to t, int x, int y, int width, int height, unsigned int ground)
{
        static struct ui_text_layout l;
        p64 lines = doc_line_count(&t->doc), line = t->mode == UI_TEXT_FIELD ? 0 : t->top_line;
        font_metrics m;
        int row_y;
        unsigned int line_ground;

        ui_text_metrics(ui, t);
        m = font_metrics_at(ui_text_face(ui, t), ui_text_em(ui, t));
        t->gutter = 0;
        if (t->numbers)
        {
                int digits = 1;

                for (p64 n = lines; n >= 10; n /= 10)
                        digits++;
                t->gutter = (digits + 2) * t->cell;
        }
        t->left = x + t->gutter, t->top = y, t->right = x + width, t->bottom = y + height;
        t->well_left = x, t->well_top = y, t->well_right = x + width, t->well_bottom = y + height;
        // A field's caret is always in sight, in the box it is drawn in now (its text set by the app, or the box moved).
        if (t->mode == UI_TEXT_FIELD)
        {
                t->top = y + (height - t->line_height) / 2, t->bottom = t->top + t->line_height;
                ui_text_reveal(ui, t);
        }
        if (!doc_length(&t->doc) && t->placeholder && !t->preedit_length)
        {
                unsigned int text = ui_text_colour(ui, UI_ON_SURFACE), alpha = 92;

                // On glass the placeholder is the quiet ink at full strength: the ink at 36% is 2.0 to 2.7:1 from the glass,
                // and the quiet ink is held at the readable threshold (ui_glass_quiet). The kit's surfaces keep the 36%.
                if (ui->glass)
                        text = ui_text_colour(ui, UI_ON_SURFACE_VARIANT), alpha = 255;
                ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, t->left, t->top + m.baseline, (string_address)t->placeholder,
                          t->size + t->zoom, text & 0xffffffu, alpha, t->left, t->right, 0);
        }
        for (row_y = t->top; row_y + t->line_height <= t->bottom + (t->mode == UI_TEXT_FIELD) && line < lines; line++)
        {
                p64 hidden_to;
                int baseline = row_y + (t->line_height - (int)m.cell_height) / 2 + (int)m.baseline;

                if (doc_hidden(&t->doc, line))
                        continue;
                ui_text_layout_line(ui, t, line, &l);
                // The line's paper: a line range's ground, and the current line lit faintly in a code view with one
                // caret; filled under the line, and the paper the code view's glyphs are laid on.
                line_ground = ground;
                for (positive r = 0; r < t->doc.range_count; r++)
                {
                        const struct doc_range address_to range = &t->doc.ranges[r];

                        if (range->kind == DOC_RANGE_LINE && range->start <= doc_line_end(&t->doc, line) &&
                            range->finish >= doc_line_start(&t->doc, line))
                                line_ground = window_mix(line_ground, ui_text_range_role(range->data) & 0xffffffu, 40);
                        if (range->kind == DOC_RANGE_MARK && t->gutter && doc_line_of(&t->doc, range->start) == line)
                                ui_round(ui, x + t->cell / 4, row_y + (t->line_height - t->cell / 2) / 2, t->cell / 2, t->cell / 2, t->cell / 4,
                                         ui_tone(ui_text_range_role(range->data) & 0xffffffu, 255));
                }
                if (t->mode == UI_TEXT_CODE && t->focused && t->doc.selection_count == 1 &&
                    doc_line_of(&t->doc, t->doc.selections[0].active) == line && !doc_any_selected(&t->doc))
                        line_ground = window_mix(line_ground, ui_role(UI_ON_SURFACE) & 0xffffffu, 10);
                if (line_ground != ground)
                        ui_fill(ui, t->left, row_y, t->right - t->left, t->line_height, 0xff000000u | line_ground);
                if (t->numbers)
                {
                        char number[24];
                        positive digits = positive_into_string((string_address)number, (positive)(line + 1));
                        b32 current = doc_line_of(&t->doc, t->doc.selections[t->doc.primary].active) == line;
                        int wide = (int)digits * t->cell;

                        number[digits] = 0;
                        ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, t->left - t->cell - wide, baseline,
                                  (string_address)number, t->size + t->zoom, ui_role(UI_ON_SURFACE) & 0xffffffu, current ? 220 : 110, x, t->left, 0);
                        if (doc_folded_at(&t->doc, line, &hidden_to))
                                ui_chevron(ui, x + t->cell / 2, row_y + t->line_height / 2, t->cell / 3, ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 160));
                }
                if (!t->wrap || t->mode == UI_TEXT_FIELD)
                {
                        ui_text_draw_line(ui, t, &l, line, row_y, baseline, line_ground);
                        row_y += t->line_height;
                        continue;
                }
                // Wrapped: the line cut where it would pass the box's width, at a blank when one is near, each row
                // after the first indented as the line is.
                {
                        static struct ui_text_layout part;
                        p32 from = 0, room = (p32)(t->right - t->left);
                        int indent = 0;

                        while (indent < (int)l.count && (l.shown[l.shown_at[indent]] == ' '))
                                indent++;
                        indent = l.x[indent] < (int)room / 2 ? l.x[indent] : 0;
                        do
                        {
                                p32 to = from, blank = 0;
                                int offset_x = l.x[from] - (from ? indent : 0);

                                while (to < l.count && l.x[to + 1] - offset_x <= (int)room)
                                {
                                        if (l.shown[l.shown_at[to]] == ' ')
                                                blank = to + 1;
                                        to++;
                                }
                                if (to < l.count && blank > from)
                                        to = blank;
                                to = to > from ? to : from + 1;
                                to = to > l.count ? l.count : to;
                                part.count = to - from;
                                for (p32 k = 0; k <= part.count; k++)
                                {
                                        part.offset[k] = l.offset[from + k];
                                        part.x[k] = l.x[from + k] - offset_x;
                                        part.shown_at[k] = l.shown_at[from + k] - l.shown_at[from];
                                }
                                part.shown_length = l.shown_at[to] - l.shown_at[from];
                                memory_copy(part.shown, l.shown + l.shown_at[from], part.shown_length);
                                part.shown[part.shown_length] = 0;
                                ui_text_draw_line(ui, t, &part, line, row_y, row_y + (t->line_height - (int)m.cell_height) / 2 + (int)m.baseline,
                                                  line_ground);
                                row_y += t->line_height;
                                from = to;
                        } while (from < l.count && row_y + t->line_height <= t->bottom);
                }
        }
}

/*
        A kit field (UI_INPUT) with a text widget behind it: the kit draws the well, the glow and the label, and asks
        this for what is inside it, the box being the well's text area. ui_control.edit names the widget.
*/
static fn ui_text_field_draw(struct ui address_to ui, struct ui_control address_to c, int left, int right, int top, int bottom)
{
        struct ui_text address_to t = c->edit;

        // The keys coming to it wake the caret, which then blinks a while.
        if (c->focused && !t->focused)
                ui_text_wake(t);
        t->focused = c->focused;
        t->placeholder = c->items[0];
        ui_text_draw(ui, t, left, top, right - left, bottom - top, ui->glass ? 0 : ui_role(UI_SURFACE_HIGH) & 0xffffffu);
        // The hand reaches it anywhere in its well, its padding as well as its text.
        t->well_left = ui_dev(ui, c->x), t->well_right = ui_dev(ui, c->x + c->w);
}

#endif // MOONWATER_CANVAS_UI
