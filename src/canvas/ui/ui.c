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

// The shapes and touch sizes the compositor draws too (theme.h, which the compositor includes before this file).
#include "theme.h"

// What text is below the look: documents, history, selections, segmentation (text.c, which says why it is its own file).
#include "text.c"

// What the windows say, in the language chosen: the table of words and the file of each other language (language.c).
#include "language.c"

/*
        A place where something is drawn that reading from the right does not turn over (a check mark, a signal's bars): its
        place is mirrored with its box, its drawing is not. The kit says so here, in device pixels, and a build says nothing of it;
        test/checks.c defines it before this file to hold the rest of a window to its mirror.
*/
#ifndef UI_KEEP
#define UI_KEEP(x, y, w, h) ((void)0)
#endif

#define UI_ROW (ui_row_height()) // a row of a list or a column: 40, and more as the text grows past its size
#define UI_ROW_LEAST 40
#define UI_PAD 16      // the window's margin
#define UI_INSET 14    // a row's text in from its edges
#define UI_SCRATCH_ROWS 96

/*
        The text's size, as a percent of what every role's size says (canvas text scale, 50 to 300): taken from the table with the
        theme, 0 until one is, which is 100. It is multiplied into the text only (ui_em, and so what a run is drawn and measured at),
        so a label wider than before makes its button wider and a list's rows are as tall as the text needs: a row is 40 logical
        pixels at 100 and below, and grows with the text above it, which everything laid out in rows follows.
*/
static unsigned int ui_text_percent;

static inline int ui_row_height(void)
{
        return ui_text_percent > 100 ? (int)(UI_ROW_LEAST * ui_text_percent / 100) : UI_ROW_LEAST;
}

/*
        A frame recorded for the GPU (src/canvas/gpu.c) in place of being painted, when ui.scene is set: ops of 8 words, and the cover
        bitmaps the text ops hold. A text op is {3 | the bitmap's pitch << 8, x0, y0, x1, y1 (the pixels it covers, cut to what may be
        drawn), the ink, alpha | stroke << 8, the bitmap's place in the GPU's buffer}; its bitmap is the cover of each pixel, one byte,
        with a border of nothing round it so that the stroke finds its neighbours without a test. A box is {2 | the number of records
        that change its cover << 8, x0, y0, x1, y1 (256ths), the colour, rx, ry} and then those records, each a box in the same terms
        under a kind of its own: 8 a hole (the cover less the hole's, a ring), 9 a clip (the product of the two covers, a ripple cut to its
        button, the rows a list is scrolled under). lost is set when a record did not fit: the frame is then the processor's.
*/
struct ui_scene
{
        unsigned int address_to ops;
        unsigned int count, room;
        unsigned char address_to cover;
        unsigned long used, size, at; // the pool of bitmaps: bytes used, its size, and where it is in the GPU's buffer
        int lost;
};

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
        struct ui_scene address_to scene; // when set, a text run is recorded into it and not painted
};

// The em of a logical size at the window's scale and the text's, in the font engine's 26.6: the size at the scale alone (bit for bit
// what it always was) while the text's percent is 100.
static inline p32 ui_em(const struct ui address_to ui, int size)
{
        return (p32)((((b64)size * (b64)ui->scale * (b64)(ui_text_percent ? ui_text_percent : 100)) << 6) / 10000);
}

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

// A rectangle (pixels, already cut to what may be drawn) recorded for the GPU: the colour as it is when it is opaque or when it is to be written (a
// fill), laid over what is there when it is not (a tone over; none at all when it has no alpha).
static void ui_scene_rect(struct ui address_to ui, int x0, int y0, int x1, int y1, unsigned int tone, b32 write)
{
        struct ui_scene address_to scene = ui->scene;
        unsigned int address_to op;

        if (!write && !(tone >> 24))
                return;
        if (scene->count >= scene->room)
        {
                scene->lost = 1;
                return;
        }
        op = scene->ops + 8 * scene->count++;
        op[0] = write || tone >> 24 == 255 ? 0 : 1, op[1] = (unsigned int)x0, op[2] = (unsigned int)y0, op[3] = (unsigned int)x1, op[4] = (unsigned int)y1;
        op[5] = tone, op[6] = op[7] = 0;
}

static fn ui_dot(struct ui address_to ui, int x, int y, unsigned int tone)
{
        unsigned int address_to at;

        if (x < 0 || x >= ui->width || y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y))
                return;
        if (ui->scene)
        {
                ui_scene_rect(ui, x, y, x + 1, y + 1, tone, false);
                return;
        }
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
        if (bottom > top && ui->scene)
                ui_scene_rect(ui, 0, top, ui->width, bottom, ui->opaque ? ui_role_surface() : 0, true);
        else if (bottom > top)
                memory_fill_u32_rect(ui->pixels + (unsigned long)top * ui->pitch, (positive)ui->pitch, (positive)ui->width,
                                     (positive)(bottom - top), ui->opaque ? ui_role_surface() : 0);
}

// The picture of a whole window, width by height, its rows pitch pixels apart, cleared to the glass.
// A picture begins whole: the band a repaint left on the ui (settings' last partial paint) is not the next picture's, and a
// menu opened from that ui would draw only the band's rows (no labels elsewhere).
static fn ui_begin(struct ui address_to ui, unsigned int address_to pixels, unsigned long pitch, int width, int height)
{
        ui->pixels = pixels;
        ui->pitch = pitch;
        ui->width = width;
        ui->height = height;
        ui->top = 0;
        ui->bottom = height;
        ui->band_top = ui->band_bottom = 0;
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
        if (ui->scene)
        {
                if (x2 > x && y2 > y)
                        ui_scene_rect(ui, x, y, x2, y2, tone, false);
                return;
        }
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
static unsigned int ui_round_cover(int x, int y, int width, int height, int radius)
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
        A corner's cover depends on its radius alone: the radius is already in device pixels, and ui_round_cover reads no
        other input. Its sample points are symmetric about the middle of the corner's square, so a pixel of any corner
        samples the same circle as the pixel mirrored across that middle into the top left quarter. That quarter is made
        once for each radius, the first time the radius is drawn, and kept: radius r at the sum of the squares below it,
        so the masks of every radius up to UI_CORNER_MOST take UI_CORNER_BYTES (22140), and a larger radius is not kept
        (ui_round_cover answers it, as before). A radius is one slot, so no two radii share one.
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
                // The quarter of a corner of radius, read from ui_round_cover itself: its square is 2 radius across.
                for (int y = 0; y < radius; y++)
                        for (int x = 0; x < radius; x++)
                                mask[y * radius + x] = (unsigned char)ui_round_cover(x, y, 2 * radius, 2 * radius, radius);
                ui_corner_made[radius] = 1;
        }
        return mask;
}

// ui_round_cover, read from a corner's mask where the pixel is in a corner's square. A pixel left of the middle or above it
// is its own place in the top left quarter; one to the right or below is mirrored across the middle.
static unsigned int ui_round_cover_cached(int x, int y, int width, int height, int radius)
{
        int mx, my;

        if (radius < 1 || radius > UI_CORNER_MOST)
                return ui_round_cover(x, y, width, height, radius);
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
                        cover = ui_round_cover_cached(column, row, width, height, radius);

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
/*
        Which way the window reads: from the right when the language taken says so (language_rtl), ui_kit.rtl. The kit is laid
        out by start and end and not by left and right, through these three and nothing else: a box of w that is `in` from the
        start edge of a container (ui_start_x), or from its end (ui_end_x), and a box already placed in LTR coordinates mirrored
        inside its container (ui_mirror). Mirroring is in the layout, before a painter is called: no painter knows a direction.
        The text is the font engine's (FONT_RTL is the paragraph's direction, so a label and its neighbours read in order).
*/
static inline b32 ui_rtl(void);
static inline int ui_dir(void);

// The font engine's flags for a run of the kit's: its paragraph direction, and bold for the fallback faces of a bold run.
static inline p32 ui_font_flags(const struct ui address_to ui, const font_face address_to face)
{
        return (ui_rtl() ? FONT_RTL : 0) | (face && face == ui->bold && ui->bold != ui->regular ? FONT_BOLD : 0);
}

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

// A run's advance in whole pixels, laid out and not drawn, with the engine's flags.
static int ui_advance_flagged(struct ui address_to ui, const font_face address_to const address_to faces, string_address text,
                              positive length, p32 em, p32 flags)
{
        return (int)(font_extent(ui->engine, faces, 2, (const p8 address_to)text, length, em, flags) >> 16);
}

static int ui_advance(struct ui address_to ui, const font_face address_to const address_to faces, string_address text,
                      positive length, p32 em)
{
        return ui_advance_flagged(ui, faces, text, length, em, ui_font_flags(ui, faces[0]));
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

// A text run recorded for the GPU: its coverage (the scratch's green byte, as the ink's cover) as a bitmap with a border, and an op for it.
static void ui_scene_text(struct ui address_to ui, int wide, int rows, int top, int pen_x, int first, int last, unsigned int colour, unsigned int alpha,
                          b32 light, unsigned int stroke)
{
        struct ui_scene address_to scene = ui->scene;
        int from = top, to = top + rows, pitch = wide + 2;
        unsigned long bytes = (unsigned long)pitch * (unsigned long)(rows + 2);
        unsigned char address_to bitmap;
        unsigned int address_to op;

        from = from < ui->top ? ui->top : from, to = to > ui->bottom ? ui->bottom : to, to = to > ui->height ? ui->height : to;
        if (ui->band_bottom > ui->band_top)
                from = from < ui->band_top ? ui->band_top : from, to = to > ui->band_bottom ? ui->band_bottom : to;
        if (first >= last || from >= to)
                return;
        if (scene->count >= scene->room || scene->used + bytes + 16 > scene->size)
        {
                scene->lost = 1;
                return;
        }
        bitmap = scene->cover + scene->used;
        memory_zero(bitmap, bytes);
        for (int row = 0; row < rows; row++)
                for (int column = 0; column < wide; column++)
                {
                        unsigned int value = ui->scratch[(unsigned long)row * (unsigned long)wide + (unsigned long)column] >> 8 & 255;

                        bitmap[(unsigned long)(row + 1) * (unsigned long)pitch + (unsigned long)column + 1] = (unsigned char)(light ? value : 255 - value);
                }
        op = scene->ops + 8 * scene->count++;
        op[0] = 3u | (unsigned int)pitch << 8, op[1] = (unsigned int)(pen_x + first), op[2] = (unsigned int)from, op[3] = (unsigned int)(pen_x + last), op[4] = (unsigned int)to;
        op[5] = colour, op[6] = (alpha > 255 ? 255 : alpha) | (stroke > 255 ? 255 : stroke) << 8;
        op[7] = (unsigned int)(scene->at + scene->used + (unsigned long)(1 + from - top) * (unsigned long)pitch + (unsigned long)(1 + first));
        scene->used += (bytes + 15) & ~15ul;
}

static int ui_run_flagged(struct ui address_to ui, const font_face address_to const address_to faces, int x, int baseline,
                          string_address text, int size, unsigned int colour, unsigned int alpha, int low, int high,
                          unsigned int stroke, p32 flags)
{
        p32 em = ui_em(ui, size);
        positive length = string_length(text);
        int width = ui_advance_flagged(ui, faces, text, length, em, flags);
        font_metrics metrics = font_metrics_at(faces[0], em);
        int above = metrics.baseline + 2 * ui->scale / 100, rows = metrics.cell_height + 4 * ui->scale / 100;
        int left = 2 * ui->scale / 100, wide = width + 2 * left, top = baseline - above;
        b32 light = window_light(colour); // light ink: drawn white on black, so the coverage is the light text's own
        unsigned int paper = light ? 0 : 0xffffffu;

        if (!alpha || !ui->scratch)
                return width;
        wide = wide > ui->scratch_width ? ui->scratch_width : wide;
        rows = rows > UI_SCRATCH_ROWS * ui->scale / 100 ? UI_SCRATCH_ROWS * ui->scale / 100 : rows;
        if (ui->band_bottom > ui->band_top && (top + rows <= ui->band_top || top >= ui->band_bottom))
                return width;

        memory_fill_u32_rect(ui->scratch, (positive)wide, (positive)wide, (positive)rows, paper);
        {
                font_target line = {ui->scratch, (positive)wide, wide, rows};

                font_draw(ui->engine, faces, 2, (const p8 address_to)text, length, em, flags, &line, (b32)left << 16, above,
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
                if (ui->scene)
                {
                        ui_scene_text(ui, wide, rows, top, pen_x, first, last, colour, alpha, light, stroke);
                        return width;
                }
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

static int ui_run_in(struct ui address_to ui, const font_face address_to const address_to faces, int x, int baseline,
                     string_address text, int size, unsigned int colour, unsigned int alpha, int low, int high,
                     unsigned int stroke)
{
        return ui_run_flagged(ui, faces, x, baseline, text, size, colour, alpha, low, high, stroke, ui_font_flags(ui, faces[0]));
}

// A run's capital height at size, in whole pixels: font.c's metrics are 16.16.
static int ui_capital(struct ui address_to ui, int size)
{
        font_metrics metrics = font_metrics_at(ui->regular, ui_em(ui, size));

        return (int)((metrics.cap_height + 32768) >> 16);
}

// A logical size in pixels at the window's scale.
static int ui_px(const struct ui address_to ui, int logical)
{
        return (logical * ui->scale + 50) / 100;
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
#define UI_FIELD_H UI_TARGET // every text field's height: a field is a touch target, the same in every window

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

// Whether x, y is past the slop from x0, y0: the one slop of every press (UI_SLOP logical pixels at the scale).
static b32 ui_press_past(const struct ui address_to ui, int x0, int y0, int x, int y)
{
        long dx = (long)x - x0, dy = (long)y - y0, slop = UI_SLOP * (long)ui->scale / 100;

        return dx * dx + dy * dy >= slop * slop;
}

static b32 ui_press_move(const struct ui address_to ui, struct ui_press address_to press, int x, int y)
{
        if (press->down && !press->dragged && ui_press_past(ui, press->x, press->y, x, y))
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

/*
        A click's count, the kit's one rule for every text and the terminal: a press within UI_CLICK_NS (450 ms) of the last
        one, and within UI_CLICK_PX (4 logical px) of where it was, is the next click of its run (a double click, then a
        triple, then the first again); any other press is the first of a run.
*/
#define UI_CLICK_NS 450000000ull
#define UI_CLICK_PX 4

struct ui_click
{
        b64 at;
        int x, y, count;
};

static int ui_click_count(const struct ui address_to ui, struct ui_click address_to k, b64 now, int x, int y)
{
        long dx = (long)x - k->x, dy = (long)y - k->y, reach = UI_CLICK_PX * (long)ui->scale / 100;
        b32 again = k->count && now - k->at < UI_CLICK_NS && dx * dx + dy * dy <= reach * reach;

        k->count = again ? k->count % 3 + 1 : 1;
        k->at = now, k->x = x, k->y = y;
        return k->count;
}


/*
        A press again to confirm, the kit's one default for an action that is not undone (a forget, a command that asks, a
        close with changes in it). The first press arms it, and the line that says so is ui_confirm_line's, the same in every
        app; the next press with nothing between, of the same thing with the same text, fires it. A press or a key that is
        not that one drops it, and Escape is a key, so it does too. What is armed is an app's number (what) and its text.
*/
#define UI_CONFIRM_TEXT 96

static b32 ui_kit_repeat; // the event is a key held down repeating (WINDOW_KEY_REPEAT): a repeat is never a second press

struct ui_confirm
{
        int what;
        b32 armed;
        char text[UI_CONFIRM_TEXT];
};

// Answers true when this press fires the confirm; any other press arms it for this thing and says nothing. A held key's repeats are
// not presses: they neither arm nor fire, so holding the key that asked cannot answer its own question.
static b32 ui_confirm_ask(struct ui_confirm address_to c, int what, const char address_to text)
{
        if (ui_kit_repeat)
                return false;
        if (c->armed && c->what == what && string_equals((string_address)c->text, (string_address)text))
        {
                c->armed = 0;
                return true;
        }
        c->armed = 1, c->what = what;
        string_copy_bounded((string_address)c->text, (string_address)text, sizeof(c->text));
        return false;
}

// A press or a key that is not the confirm's own: the confirm is no longer armed.
static void ui_confirm_drop(struct ui_confirm address_to c)
{
        c->armed = 0;
}

// The line an armed confirm shows: the hint, then what the next press does.
static void ui_confirm_line(char address_to into, positive room, const char address_to what)
{
        string_copy_bounded((string_address)into, (string_address)WORDS(ui_confirm_again), room);
        string_append_bounded((string_address)into, (string_address)what, room);
}

static b32 ui_kit_touch; // the event is a finger's (WINDOW_KEY_TOUCH): what it can press is at least UI_TARGET each way
static b32 ui_kit_retract; // the event is a hand's lead taken away (WINDOW_KEY_SETTLE): a place and no step the hand made

// Whether px, py hits a shape x, y, width by height: a finger's reach is the shape grown to UI_TARGET each way where it is smaller
// (ui_reach_hit, theme.h's rule, which the compositor's resize corners and buttons take too).
static b32 ui_hit(const struct ui address_to ui, b32 touch, int px, int py, int x, int y, int width, int height)
{
        return ui_reach_hit(touch, UI_TARGET * ui->scale / 100, px, py, x, y, width, height);
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

#define UI_DRAG_SAMPLES 16
#define UI_FLING_NS 40000000ul // the horizon a flick's velocity is fitted over: the reports of its last 40 ms (at least three)
#define UI_FLING_STALE_NS 60000000ul // a lift this long after the last report is a finger that rested: it throws nothing

struct ui_drag
{
        int last;          // where the finger was at the last move
        long speed;        // pixels a second, signed
        b64 owed;          // a throw's move not yet given, in Q30 pixels
        b64 rate;          // a throw's speed in Q30 pixels a second, the one the steps fall by e of
        positive at;       // when the last move or step was
        b32 flying;
        int samples;       // the finger's last reports, oldest first: when, and where it was
        positive when[UI_DRAG_SAMPLES];
        int where[UI_DRAG_SAMPLES];
        int batch;         // the first of the reports handed over together (within 2 ms of batch_at), whose time is shared out
        positive batch_at; // when the batch came
};

static fn ui_drag_start(struct ui_drag address_to drag, int at, positive now)
{
        drag->last = at, drag->speed = 0, drag->owed = 0, drag->rate = 0, drag->at = now, drag->flying = false;
        drag->samples = 0, drag->batch = 0, drag->batch_at = 0;
}

static int ui_drag_follow(struct ui_drag address_to drag, int at, positive now)
{
        int moved = at - drag->last;
        long gap = now > drag->at ? (long)(now - drag->at) : 0;

        // The compositor's taking back of the finger's lead (at its lift, or in a frame with no report) puts the hand where it is: the
        // list goes with it, but it is no report of how fast the finger goes, and a step back at the lift read as the finger turning.
        if (ui_kit_retract)
        {
                drag->last = at;
                return moved;
        }

        {
                // Reports handed over together (a window that was busy, or a compositor that joins what came within a frame) carry
                // the time they were handed over at, which is no time at all: the time since the report before them is shared out
                // evenly among them, so a burst of four reads as four steps and not as one finger that stopped.
                b32 joined = drag->samples && drag->batch_at && now >= drag->batch_at && now - drag->batch_at < 2000000ul;

                if (drag->samples == UI_DRAG_SAMPLES)
                {
                        for (int k = 1; k < UI_DRAG_SAMPLES; k++)
                                drag->when[k - 1] = drag->when[k], drag->where[k - 1] = drag->where[k];
                        drag->samples--;
                        drag->batch = drag->batch > 0 ? drag->batch - 1 : 0;
                }
                drag->when[drag->samples] = now, drag->where[drag->samples] = at, drag->samples++;
                if (!joined)
                        drag->batch = drag->samples - 1, drag->batch_at = now;
                else if (drag->batch > 0)
                {
                        positive from = drag->when[drag->batch - 1];
                        long count = drag->samples - drag->batch;

                        for (long j = 0; j < count; j++)
                                drag->when[drag->batch + j] = from + (now - from) * (positive)(j + 1) / (positive)count;
                }
        }

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

/*
        The finger's velocity at its lift, pixels a second, signed: the derivative at its last report of a parabola fitted (least squares)
        through the reports of the UI_FLING_NS before it (at least the last three). A line through a window is the window's mean, and a
        finger that slowed to a stop in it still has a mean: the parabola's slope at the end is what the finger was doing as it let go,
        so a gesture that came to rest throws nothing and a flick that is still fast keeps its speed, at any report rate. Fitted rather
        than differenced, so one jittery report does not set the throw. Two reports give the line through them; none, one, a stale last
        report (the lift is more than UI_FLING_STALE_NS after it: the finger rested) or a slope that goes the other way than the line
        gives 0; and the speed is never more than a quarter over the fastest step between two of the reports. Times are in ms before the
        last report, which keeps the sums in 64 bits (at most sixteen reports in 100 ms); a slope in pixels a ms is a thousand times that
        a second. Reports handed over together have the time since the report before them shared out evenly (ui_drag_follow), so a
        batch handed over at once is not a speed made of equal times.
*/
static long ui_drag_slope(long address_to num, long address_to den)
{
        long whole, rest, unit;

        if (*den <= 0)
                return 0;
        whole = *num / *den;
        rest = *num % *den;
        unit = *den / 1000;
        return whole * 1000 + (unit ? rest / unit : rest * 1000 / *den);
}

static long ui_drag_fling(const struct ui_drag address_to drag, positive now)
{
        long t[UI_DRAG_SAMPLES], y[UI_DRAG_SAMPLES], n = 0, st = 0, sy = 0, stt = 0, sty = 0, fastest = 0, line_num, line_den, line, speed;
        positive last;

        if (drag->samples < 2)
                return 0;
        last = drag->when[drag->samples - 1];
        if (now > last && now - last > UI_FLING_STALE_NS)
                return 0;
        // The newest first: those within the horizon, and at least the last three while they are within 100 ms.
        for (int k = drag->samples - 1; k >= 0; k--)
        {
                positive age = last - drag->when[k];

                if (drag->when[k] > last || age > 100000000ul || (age > UI_FLING_NS && n >= 3))
                        break;
                t[n] = -(long)((age + 500000ul) / 1000000ul);
                y[n] = (long)drag->where[k] - (long)drag->where[drag->samples - 1];
                n++;
        }
        if (n < 2)
                return 0;
        for (int k = 0; k < n; k++)
        {
                st += t[k], sy += y[k], stt += t[k] * t[k], sty += t[k] * y[k];
                if (k && t[k - 1] > t[k])
                {
                        long step = (y[k - 1] - y[k]) * 1000 / (t[k - 1] - t[k]);

                        fastest = (step < 0 ? -step : step) > fastest ? (step < 0 ? -step : step) : fastest;
                }
        }
        line_num = n * sty - st * sy, line_den = n * stt - st * st;
        line = ui_drag_slope(&line_num, &line_den);
        speed = line;
        if (n >= 3)
        {
                long s3 = 0, s4 = 0, s2y = 0, det, det_b;

                for (int k = 0; k < n; k++)
                        s3 += t[k] * t[k] * t[k], s4 += t[k] * t[k] * t[k] * t[k], s2y += t[k] * t[k] * y[k];
                // The normal equations of y = a + b t + c t^2: b by Cramer's rule, the slope at t = 0 (the last report).
                det = n * (stt * s4 - s3 * s3) - st * (st * s4 - s3 * stt) + stt * (st * s3 - stt * stt);
                det_b = n * (sty * s4 - s3 * s2y) - sy * (st * s4 - s3 * stt) + stt * (st * s2y - sty * stt);
                if (det > 0)
                        speed = ui_drag_slope(&det_b, &det);
        }
        if (speed == 0 || (speed > 0) != (line > 0))
                return 0;
        fastest += fastest / 4;
        return speed > fastest ? fastest : speed < -fastest ? -fastest : speed;
}

// The release's velocity, for every drag (a finger, a trackpad's hand, a terminal's scrollback): the fit over its last reports, and
// none when the finger stopped before it lifted (no report for 80 ms).
static long ui_drag_velocity(const struct ui_drag address_to drag, positive now)
{
        if (now > drag->at && now - drag->at > 80000000ul)
                return 0;
        return ui_drag_fling(drag, now);
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
        The motion, in Q30 (motion.h). Springs: ui_spring_go (a kick), ui_spring_at (the value now), ui_spring_stop. Tweens:
        ui_tween_put (the target), ui_tween_at, ui_tween_still. Ripples: ui_ripple_go, ui_ripple_at, ui_ripple_exit. Slides:
        ui_slide_go, ui_slide_value, ui_slide_land. The time is the last argument, in nanoseconds, and the caller passes
        ui_kit.now (set by ui_event_context); a caller's own clock is what check_kit_standard counts.
*/

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
            ripple      NB(): grows in 225 ms and travels to the middle in 425 (all its times
                        at a pace of 1.2 to 1.6 by its reach, ui_ripple_go)
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
        b64 speed[2]; // the speed each channel starts at, per second (Q30): a release carries it into the spring
        b64 pace;     // how much faster than ui.js's clock it runs (Q30; 0 is 1): the same curve in less time, its carried speed with it
};

static fn ui_spring_go(struct ui_spring address_to s, b64 scale, b64 nudge)
{
        s->amplitude[0] = s->value[0] = scale;
        s->amplitude[1] = s->value[1] = nudge;
        s->speed[0] = s->speed[1] = 0;
        s->pace = 0; // ui.js's clock; a caller that wants another sets it after this call (the scroll's back spring does)
        s->start = 0;
        s->live = 1; // the first frame says whether any of it is past rest
}

// A channel's speed from the first frame (Q30 a second): the release's, so the spring runs on from it.
static fn ui_spring_speed(struct ui_spring address_to s, int k, b64 v)
{
        s->speed[k] = v;
        s->live = 1;
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
        if (s->pace)
                h = ui_qmul(h, s->pace);
        j = ui_exp_neg(ui_qmul(UI_SPRING_DECAY, h));
        ui_sincos(ui_qmul(UI_SPRING_FREQ, h), &sine, &cosine);
        for (int k = 0; k < 2; k++)
        {
                b64 a = s->amplitude[k], b = ui_qmul(UI_SPRING_UB, a) + (s->speed[k] ? ui_qdiv(s->pace ? ui_qdiv(s->speed[k], s->pace) : s->speed[k], UI_SPRING_FREQ) : 0);
                b64 f = ui_qmul(j, ui_qmul(a, cosine) + ui_qmul(b, sine));
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
        curve is ui.js's. Its size is the clip's farthest corner from the press
        (and a little over): the circle starts at the press and moves to the
        box's middle as it grows, and a circle that reached only the farthest
        corner from the middle had its edge inside the clip when it was full
        grown (the middle was not reached yet), so a ripple that began at an end
        of a wide row ended as a disc. The distance to the farthest corner is
        the largest over every place the circle's middle passes (it is a convex
        function of the place, and the middle minimises it), so a circle that
        reaches that far from the press covers the clip once grown, whatever
        the middle has reached. The clock starts at the first frame; exit is
        when the pointer went up or away.
*/
#define UI_RIPPLE_OVER 2 // logical pixels past the corner: its pixel is wholly inside the circle, not on its edge
struct ui_ripple
{
        b64 x, y, w, h, margin;   // the press and the box, Q30 logical pixels
        b64 reach;                // the radius at full size: the circle covers the whole clip, from the press and from every place on its way to the middle
        b64 mid;                  // the radius ui.js's ripple grows to, from the clip's middle to its farthest corner: what the pace is made of
        b64 pace;                 // how much longer than ui.js's times it takes (Q30; 0 is 1): by its reach, so a wide row's is seen
        b64 start, exit, hold;    // ns: the first frame, the exit (0 before it), what the exit waits
        b32 live;
        b32 rides;                // a tab's ripple from a press on the tab the indicator was on: it moves with the indicator
};

static fn ui_ripple_go(struct ui_ripple address_to r, b64 x, b64 y, b64 w, b64 h, b64 margin)
{
        b64 f = margin + w / 2, v = margin + h / 2, across = w + 2 * margin, down = h + 2 * margin;
        b64 rx = f > across - f ? f : across - f, ry = v > down - v ? v : down - v;
        b64 px = x + margin, py = y + margin; // the press in the clip, held inside it

        px = px < 0 ? 0 : px > across ? across : px;
        py = py < 0 ? 0 : py > down ? down : py;
        r->x = x, r->y = y, r->w = w, r->h = h, r->margin = margin;
        r->mid = ui_qsqrt(ui_qmul(rx, rx) + ui_qmul(ry, ry));
        {
                b64 fx = px > across - px ? px : across - px, fy = py > down - py ? py : down - py;

                r->reach = ui_qsqrt(ui_qmul(fx, fx) + ui_qmul(fy, fy)) + ui_q(UI_RIPPLE_OVER);
        }
        // Slower than ui.js's by a touch on a small control and by more on a wide row, where the same 225 ms flew past: 1.2 at a
        // reach of 80 px or less, rising to 1.6 at 300 px and over. The reach is the control's (from its middle), not the press's:
        // a press at an end of a row grows to cover it in the row's own time.
        {
                b64 over = r->mid - ui_q(80), span = ui_q(220);

                over = over < 0 ? 0 : over > span ? span : over;
                r->pace = UI_Q2(120) + ui_qmul(UI_Q2(40), ui_qdiv(over, span));
        }
        r->start = r->exit = r->hold = 0;
        r->live = 1;
        r->rides = 0;
}

// A time of ui.js's ripple, ms, at this ripple's pace: ns.
static inline b64 ui_ripple_ns(const struct ui_ripple address_to r, b64 ms)
{
        return ui_qmul(ms * UI_MS, r->pace ? r->pace : UI_ONE);
}

static fn ui_ripple_exit(struct ui_ripple address_to r, b64 now)
{
        b64 shown, grown = ui_ripple_ns(r, 225);

        if (!r->live || r->exit)
                return;
        r->exit = now;
        shown = r->start ? now - r->start : 0;
        r->hold = shown > 0 && shown < grown ? grown - shown : 0;
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
        grow = ui_ripple_ease(ui_progress(h, ui_ripple_ns(r, 225)));
        travel = ui_ripple_ease(ui_progress(h, ui_ripple_ns(r, 425)));
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

                a = n < 0 ? UI_ONE : UI_ONE - ui_progress(n, ui_ripple_ns(r, 150));
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
/*
        The theme: a colour is a role (ui_role), a well is a role at an alpha (ui_well), a text's colour is ui_text_colour.
        Direction: ui_rtl() says the language reads right to left; ui_mirror turns a box over its middle; ui_arrow(key) is +1
        toward the end of a row; ui_start_x and ui_end_x are the edges. Layout mirrors, painters never do: a mark that must not
        turn says UI_KEEP (see the macro above; a build says nothing of it).
*/

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

// The caret's blink and width, the places of canvas caret blink (slow, normal, fast, off) and canvas caret thickness (thin,
// normal, thick). The table keeps one more than a place, so a table nobody has set holds 0, which is the standard.
#define UI_CARET_SLOW 0u
#define UI_CARET_NORMAL 1u
#define UI_CARET_FAST 2u
#define UI_CARET_OFF 3u
#define UI_CARET_THIN 0u
#define UI_CARET_MID 1u
#define UI_CARET_THICK 2u

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
        unsigned int wheel_off;            // 1 when a notch of the wheel moves a row at once (canvas wheel smooth off), 0 the glide
        unsigned int caret_speed;          // UI_CARET_SLOW, _NORMAL, _FAST or _OFF: the caret's blink (canvas caret blink)
        unsigned int caret_thick;          // UI_CARET_THIN, _MID or _THICK: the caret's width (canvas caret thickness)
        unsigned int rtl;                  // 1 to lay every window out mirrored whatever the language says (a check draws a page both ways)
        unsigned int read_rtl;             // the way the language read when the controls were last placed
        b64 wheel_clock;                   // a test's clock for the wheel's rate, ns; 0 the monotonic clock
        unsigned int themed;               // the table's theme count as last taken
        b64 now;                           // the frame's time, ns
        b64 drawn;                        // the last frame's time: the next is a display period after it
        b32 busy;                          // something moved: another frame is due
        b32 made;
        int damage_top, damage_bottom;     // the rows (device pixels) the moving controls drew this frame
        b64 epoch;                         // counted up when every control is to be placed again (ui_kit_settle)
        // The control being drawn said which of its rows move (logical pixels, none when bottom <= top), and they
        // are all that is marked of it: tabs on end, whose box is one item of a column.
        b32 reached;
        int reach_top, reach_bottom;
        int pointer_x, pointer_y;          // the window's pointer, device pixels: the last motion's place (ui_event_context)
} ui_kit = {.caret_speed = UI_CARET_NORMAL, .caret_thick = UI_CARET_MID};

// The caret's settings as the table holds them, taken with the wheel's: a table nobody has set is the standard.
static fn ui_kit_caret_take(const struct windows address_to table)
{
        ui_kit.caret_speed = table->caret_speed ? table->caret_speed - 1 : UI_CARET_NORMAL;
        ui_kit.caret_thick = table->caret_thick ? table->caret_thick - 1 : UI_CARET_MID;
}

// The window reads from the right when the language taken says so, read from the language itself (so the apps' language listeners, which run
// as it is taken, lay out the new way) or when the kit is told to (ui_kit.rtl: a check's, to draw the same page both ways).
static inline b32 ui_rtl(void)
{
        return ui_kit.rtl || language_rtl();
}

static inline int ui_dir(void)
{
        return ui_rtl() ? -1 : 1;
}

// A horizontal arrow key, as the way it goes along what is read: +1 toward the end (Right, or Left reading from the right), -1 toward the start.
// A text goes by what is on the screen (Left is left) and does not ask; a row of buttons, a menu's submenus and a stepper do.
static inline int ui_arrow(int key)
{
        return key == 106 ? ui_dir() : -ui_dir();
}

// A box w wide at x (as laid out left to right) inside the container [left, left + width), mirrored in it when the window reads from the right.
static inline int ui_mirror(int x, int w, int left, int width)
{
        return ui_rtl() ? 2 * left + width - x - w : x;
}

// A device pixel x of the container [left, left + width) where it is across the container from the other end, when the window reads from the right.
static inline int ui_point(int x, int left, int width)
{
        return ui_rtl() ? 2 * left + width - 1 - x : x;
}

// A box w wide that is `in` from the start edge of the container [left, left + width): the left one reading from the left, the right one from the right.
static inline int ui_start_x(int left, int width, int in, int w)
{
        return ui_rtl() ? left + width - in - w : left + in;
}

// And `in` from its end edge.
static inline int ui_end_x(int left, int width, int in, int w)
{
        return ui_rtl() ? left + in : left + width - in - w;
}

static fn ui_kit_settle(void);

// The language the table says, taken wherever the theme is: the one place a window of this program learns it, with the way it reads.
static fn ui_kit_language_take(const struct windows address_to table)
{
        language_take(table->language);
        // A language that reads the other way places every control again, as if it had just appeared.
        if (ui_kit.read_rtl != (unsigned int)language_rtl())
        {
                ui_kit.read_rtl = (unsigned int)language_rtl();
                ui_kit_settle();
        }
}

// The text's size the table says, taken wherever the theme is: a size out of its range is the standard. Every control is placed again
// when it changes, as if it had just appeared: its text is another width, and a list's rows another height.
static fn ui_kit_text_take(const struct windows address_to table)
{
        unsigned int percent = table->text_scale >= 50 && table->text_scale <= 300 ? table->text_scale : 0;

        if (percent == ui_text_percent)
                return;
        ui_text_percent = percent;
        ui_kit_settle();
}

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
        Everything is placed again, not moved: the window was resized or the
        scale changed. A control drawn after this is drawn at its state, its
        transitions and springs put at their ends (ui_control_place). A theme
        turn is not this: it moves each colour through the kit's clock and
        leaves every control where it is (ui_kit_theme).
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
        ui_kit.epoch += !ui_kit.epoch; // the first theme: the kit's controls are placed from the start (a later turn places none)
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
        The box layer: boxes in Q8 device pixels (ui_q8 = device px * 256, ui_stroke the one-pixel stroke). ui_kit_fill paints a
        box in an argb with an optional clip box, ui_kit_ring a ring, ui_kit_outline a line, ui_kit_put one pixel, ui_kit_run a
        run of cover. ui_box_* is box geometry (radii, spans). Nothing here knows a control: a box, a colour and a cover.
*/

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

// A box (device 256ths) turned over the vertical middle of a container whose left and right edges (256ths) add to sum, when the window reads from the right.
static inline void ui_flip(struct ui_box address_to box, b32 sum)
{
        if (ui_rtl())
        {
                b32 x0 = sum - box->x1;

                box->x1 = sum - box->x0;
                box->x0 = x0;
        }
}

static unsigned short ui_kit_line[UI_KIT_LINE], ui_kit_clip[UI_KIT_LINE];

// A box from a logical rectangle (Q30 logical pixels) at the window's scale, with a corner radius (Q30, logical).
static struct ui_box ui_box_of(const struct ui address_to ui, b64 x, b64 y, b64 w, b64 h, b64 radius)
{
        struct ui_box box;
        b64 s = ui->scale;

        box.x0 = (b32)((x * s / 100) >> 22);
        box.y0 = (b32)((y * s / 100) >> 22);
        box.x1 = (b32)(((x + w) * s / 100) >> 22);
        box.y1 = (b32)(((y + h) * s / 100) >> 22);
        box.rx = box.ry = (b32)((radius * s / 100) >> 22);
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
// The argb at cover (256ths) of itself, premultiplied: the box layer's cover. ui_cover is the same at 255ths on each channel.
static inline unsigned int ui_kit_cover(unsigned int argb, unsigned int cover)
{
        if (cover >= UI_FULL && argb >> 24 == 255)
                return argb | 0xff000000u;
        return ui_premultiplied(argb, cover > UI_FULL ? UI_FULL : cover);
}

// A box, or a ring of it (the box less the box inset by thickness), cut to clip when there is one and to the rows that may be drawn, recorded for the GPU:
// its record, then the records that change its cover, the hole first, the clips after.
static void ui_scene_box(struct ui address_to ui, const struct ui_box address_to box, unsigned int argb, b32 ring, b32 thickness, const struct ui_box address_to clip)
{
        struct ui_scene address_to scene = ui->scene;
        int from = ui->top < 0 ? 0 : ui->top, to = ui->bottom > ui->height ? ui->height : ui->bottom;
        unsigned int cut, mods, address_to op;

        if (ui->band_bottom > ui->band_top)
                from = from < ui->band_top ? ui->band_top : from, to = to > ui->band_bottom ? ui->band_bottom : to;
        if (from >= to)
                return;
        cut = from > 0 || to < ui->height;
        mods = (ring != 0) + (clip != 0) + cut;
        if (scene->count + 1 + mods > scene->room)
        {
                scene->lost = 1;
                return;
        }
        op = scene->ops + 8 * scene->count;
        scene->count += 1 + mods;
        op[0] = 2u | mods << 8, op[1] = (unsigned int)box->x0, op[2] = (unsigned int)box->y0, op[3] = (unsigned int)box->x1, op[4] = (unsigned int)box->y1;
        op[5] = argb, op[6] = (unsigned int)box->rx, op[7] = (unsigned int)box->ry;
        if (ring)
        {
                op += 8;
                op[0] = 8, op[1] = (unsigned int)(box->x0 + thickness), op[2] = (unsigned int)(box->y0 + thickness), op[3] = (unsigned int)(box->x1 - thickness);
                op[4] = (unsigned int)(box->y1 - thickness), op[5] = 0, op[6] = box->rx > thickness ? (unsigned int)(box->rx - thickness) : 0;
                op[7] = box->ry > thickness ? (unsigned int)(box->ry - thickness) : 0;
        }
        if (clip)
        {
                op += 8;
                op[0] = 9, op[1] = (unsigned int)clip->x0, op[2] = (unsigned int)clip->y0, op[3] = (unsigned int)clip->x1, op[4] = (unsigned int)clip->y1;
                op[5] = 0, op[6] = (unsigned int)clip->rx, op[7] = (unsigned int)clip->ry;
        }
        if (cut)
        {
                op += 8;
                op[0] = 9, op[1] = 0, op[2] = (unsigned int)from << 8, op[3] = (unsigned int)ui->width << 8, op[4] = (unsigned int)to << 8;
                op[5] = op[6] = op[7] = 0;
        }
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
        if (ui->scene)
        {
                ui_scene_box(ui, box, argb, false, 0, clip);
                return;
        }
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
                                ui_kit_run(row + a, b - a, ui_kit_cover(argb, inside));
                        for (b32 x = b > a ? b : hi; x < hi; x++)
                                ui_kit_put(row, x, clip ? ui_spans_cover(&spans, x) * ui_spans_cover(&cut, x) / UI_FULL : ui_spans_cover(&spans, x), argb);
                }
        }
}


// A box laid down in the coordinates a control works in (as if the window read from the left), turned over the control's middle
// (sum: its left and right edges added, device 256ths) when it reads from the right; the box that clips it turned with it.
static fn ui_kit_fill_in(struct ui address_to ui, struct ui_box box, unsigned int argb, const struct ui_box address_to clip, b32 sum)
{
        struct ui_box clipped;

        ui_flip(&box, sum);
        if (clip)
        {
                clipped = *clip;
                ui_flip(&clipped, sum);
                clip = &clipped;
        }
        ui_kit_fill(ui, &box, argb, clip);
}

// A ring: the box less the box inset by thickness (256ths), in one colour (an outline, a hairline).
static fn ui_kit_ring(struct ui address_to ui, const struct ui_box address_to box, b32 thickness, unsigned int argb)
{
        struct ui_box inner = *box;
        b32 first = box->x0 >> 8, last = (box->x1 + 255) >> 8, top = box->y0 >> 8, bottom = (box->y1 + 255) >> 8;

        b32 ry, inner_ry;

        if (ui->scene)
        {
                ui_scene_box(ui, box, argb, true, thickness, 0);
                return;
        }

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
                                        ui_kit_put(row, p0, c0, argb);
                                        if (p1 > p0)
                                        {
                                                ui_kit_run(row + p0 + 1, p1 - p0 - 1, ui_premultiplied(argb, UI_FULL));
                                                ui_kit_put(row, p1, c1, argb);
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
                                        ui_kit_put(row, x, (unsigned int)cover, argb);
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
                                ui_kit_put(row, x, cover, argb);
                }
        }
        memory_zero(ui_kit_clip, (positive)(last > first ? last - first : 0) * sizeof(ui_kit_clip[0]));
}

/* ---- end of the kit's shapes ---- */

/* ---- kit: controls ---- */
/*
        The controls: ui_control_draw paints one control of a page (its kind picks a static ui_<kind>_draw), ui_control_hit and
        ui_control_place are its hit test and place, ui_slider_* the sliders, ui_tabs_* and ui_vtabs_* the tabs, ui_kit_text_* and
        ui_kit_measure a run of text in a box. A control is data (kind, label, value, box); the kit draws, hits and moves it.
*/

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

// Text of a role at x on the baseline (device pixels, whole), in a colour at its own alpha, with a stroke, cut at high (device
// pixels: the window's width, or a control's right edge); its width.
static int ui_kit_text_between(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb,
                               unsigned int stroke, int low, int high)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to faces[2] = {t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular,
                                               ui->regular};

        return ui_run_in(ui, faces, x, baseline, text, t->size, argb & 0xffffffu, argb >> 24, low, high, stroke);
}

static int ui_kit_text_width(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb,
                             unsigned int stroke, int high)
{
        return ui_kit_text_between(ui, x, baseline, text, type, argb, stroke, 0, high);
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
// The same, cut between low and high (device pixels): a control's text is cut at its own box, not at the window's edges.
static fn ui_kit_text_in(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb, unsigned int stroke,
                         int low, int high)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to face = t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular;
        font_metrics metrics = font_metrics_at(face, ui_em(ui, t->size));
        int rows = metrics.cell_height + 4 * ui->scale / 100, top = baseline - metrics.baseline - 2 * ui->scale / 100;

        rows = rows > UI_SCRATCH_ROWS * ui->scale / 100 ? UI_SCRATCH_ROWS * ui->scale / 100 : rows;
        if (ui_rows_seen(ui, top, top + rows))
                ui_kit_text_between(ui, x, baseline, text, type, argb, stroke, low, high);
}

static fn ui_kit_text_clip(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb, unsigned int stroke,
                           int high)
{
        ui_kit_text_in(ui, x, baseline, text, type, argb, stroke, 0, high);
}

static fn ui_kit_text(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb, unsigned int stroke)
{
        ui_kit_text_clip(ui, x, baseline, text, type, argb, stroke, ui->width);
}

static int ui_kit_measure(struct ui address_to ui, string_address text, int type)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to faces[2] = {t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular,
                                               ui->regular};

        return ui_run_in(ui, faces, 0, 0, text, t->size, 0, 0, 0, ui->width, 0);
}

// A run laid out as if the window read from the left, with its left end at x in the container [left, left + span), mirrored in
// it when the window reads from the right; the width of the run. A row of a list works left to right and draws through this.
static int ui_kit_text_flipped(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb, unsigned int stroke,
                               int left, int span)
{
        int width;

        if (!ui_rtl())
                return ui_kit_text_width(ui, x, baseline, text, type, argb, stroke, ui->width);
        width = ui_kit_measure(ui, text, type);
        ui_kit_text(ui, ui_mirror(x, width, left, span), baseline, text, type, argb, stroke);
        return width;
}

// A run that starts at `edge` (device pixels): its left end there reading from the left, its right end reading from the right,
// cut between low and high. The run is the font engine's, whole in either direction; only the place of its box is the layout's.
static fn ui_kit_start(struct ui address_to ui, int edge, int baseline, string_address text, int type, unsigned int argb, unsigned int stroke,
                       int low, int high)
{
        ui_kit_text_in(ui, ui_rtl() ? edge - ui_kit_measure(ui, text, type) : edge, baseline, text, type, argb, stroke, low, high);
}

// The baseline of a role's line box whose top is top (device pixels): the line's half-leading above the face's ascent.
static int ui_kit_baseline(struct ui address_to ui, int top, int type)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to face = t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular;
        font_metrics metrics = font_metrics_at(face, ui_em(ui, t->size));

        return top + (t->line * ui->scale / 100 - metrics.cell_height) / 2 + metrics.baseline;
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
#define UI_SLIDER_VERTICAL 4 // stood on its end, without a label: the mixer's (ui_vslider_draw)
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
        const struct ui_scroll address_to span; // the region a column's items are drawn in: a stretch maps them (ui_scroll_map), or none
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
static inline b32 ui_dev(const struct ui address_to ui, int logical) { return logical * ui->scale / 100; }
static inline b32 ui_q8(const struct ui address_to ui, int logical) { return logical * ui->scale / 100 * 256; }
// A stroke of one logical pixel in Q8, never less than a device pixel: at 75 percent it is 0.75 of one, which the whole-pixel rule takes to 0.
static inline b32 ui_stroke(const struct ui address_to ui) { return ui_q8(ui, 1) > 256 ? ui_q8(ui, 1) : 256; }

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

/*
        How long a new target takes, span ns. A theme turn moves every colour's target each frame (a role turns over the
        .2 s), so while it turns a running tween keeps the clock it has: the new target takes what is left of its own run
        or of the turn, whichever is longer, and lands when the turn does. Nothing running, or no turn, it takes its span.
*/
static b64 ui_kit_span(const struct ui_tween address_to t, b64 span)
{
        b64 left, turn;

        if (!span || !ui_kit.turn.start || !t->start) // no turn under way (its start is 0 once it has landed), or nothing running
                return span;
        turn = ui_kit.turn.start + ui_kit.turn.duration - ui_kit.now;
        left = t->start + t->duration - ui_kit.now;
        if (turn > left)
                left = turn;
        return left > 0 ? left : span;
}

// A colour to transition to over the kit's .2s (or at once with reduced motion); its first is where it starts.
static inline fn ui_kit_colour_to(struct ui_tween address_to t, unsigned int argb, int ms, const struct ui_bezier address_to ease)
{
        if (!t->placed)
                ui_tween_put(t, ui_tween_colour(argb), UI_TWEEN_COLOUR);
        else
                ui_tween_set(t, ui_tween_colour(argb), ui_kit_span(t, ui_kit.motion ? 0 : ms * UI_MS), ease);
}

static inline fn ui_kit_number_to(struct ui_tween address_to t, b64 value, int ms, const struct ui_bezier address_to ease)
{
        if (!t->placed)
                ui_tween_put(t, value, UI_TWEEN_NUMBER);
        else
                ui_tween_set(t, value, ui_kit_span(t, ui_kit.motion ? 0 : ms * UI_MS), ease);
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

// A control's label, unstroked, its x dx and its line's top dy logical pixels from the control's corner, in a type and ink,
// cut at the control's right edge (its width, when it has one; a control whose width is not yet set is cut at the window).
static inline fn ui_control_label(struct ui address_to ui, const struct ui_control address_to c, int dx, int dy, int type, unsigned int ink)
{
        int baseline = ui_kit_baseline(ui, ui_dev(ui, c->y + dy), type);

        // Reading from the right it starts dx in from the control's right edge and is cut at its left (a control with no width yet reads from the left).
        if (ui_rtl() && c->w > 0)
                ui_kit_start(ui, ui_dev(ui, c->x + c->w - dx), baseline, (string_address)c->label, type, ink, 0, ui_dev(ui, c->x), ui_dev(ui, c->x + c->w));
        else
                ui_kit_text_clip(ui, ui_dev(ui, c->x + dx), baseline, (string_address)c->label, type, ink, 0,
                                 c->w > 0 ? ui_dev(ui, c->x + c->w) : ui->width);
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
                dot.x0 = ox + (b32)(((cx - radius) * ui->scale / 100) >> 22);
                dot.x1 = ox + (b32)(((cx + radius) * ui->scale / 100) >> 22);
                dot.y0 = oy + (b32)(((cy - radius) * ui->scale / 100) >> 22);
                dot.y1 = oy + (b32)(((cy + radius) * ui->scale / 100) >> 22);
                dot.rx = dot.ry = (b32)((radius * ui->scale / 100) >> 22);
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
        ui_ripple_go(r, ui_qdiv(ui_q(x - bx), ui_q(ui->scale) / 100), ui_qdiv(ui_q(y - by), ui_q(ui->scale) / 100), ui_qdiv(ui_q(bw), ui_q(ui->scale) / 100),
                     ui_qdiv(ui_q(bh), ui_q(ui->scale) / 100), ui_q(margin));
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
        *tx = (b32)((drawn[2] * ui->scale / 100) >> 22);
        *ty = (b32)((drawn[3] * ui->scale / 100) >> 22);
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
        b32 bx = ui_rtl() ? rx : rx + rw - ui_q8(ui, 52), by = oy - ui_q8(ui, 14), bw = ui_q8(ui, 52), bh = ui_q8(ui, 28);
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
        // Reading from the right the pill fills from its right end: the two bars are each other's places.
        if (ui_rtl())
        {
                fill.x0 = bx + bw - (fill_w > 0 ? fill_w : 0), fill.x1 = bx + bw;
                rest.x0 = bx, rest.x1 = bx + (rest_w > 0 ? rest_w : 0);
        }
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
                b32 left = ui_rtl() ? rx + rw - width * 256 : rx, middle = left + width * 128, anchor = c->dom_origin > 0 ? left : left + width * 256;
                b64 stretch = UI_ONE + ui_qmul(dom - UI_ONE, UI_Q2(60));

                middle = anchor + (b32)ui_qmul(middle - anchor, stretch);
                middle = ox + tx + (b32)ui_qmul(middle - ox, sx);
                x = middle / 256 - width / 2;
        }
        baseline = ui_kit_baseline(ui, (oy + ty) / 256 - ui_dev(ui, 12), UI_BODY_LARGE);
        // Cut before the pill (its 52 and a gap of 12 at its end): a long label is not written under the bars.
        ui_kit_text_in(ui, x, baseline, (string_address)c->label, UI_BODY_LARGE, c->disabled ? ui_mix(label, 38, 0) : label,
                       (unsigned int)(ui_kit_tween(&c->stroke) >> UI_Q), ui_rtl() && c->w > 64 ? ui_dev(ui, c->x + 64) : 0,
                       !ui_rtl() && c->w > 64 ? ui_dev(ui, c->x + c->w - 64) : ui->width);
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
        b32 cx = ui_q8(ui, ui_rtl() ? c->x + 12 + 14 : c->x + c->w - 12 - 14); // the circles' middle: 12 in from the row's end, 28 wide

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
                // The ripple is cut to the row it is on, the box the hover is drawn on: not an outset of it, and not moved with the press.
                clip = (struct ui_box){rx, ry, rx + rw, ry + rh, 0, 0};
                // The rest circle: scale(rest) about its middle, inside the bars' scaleY(dom) scale(hover) about the origin.
                if (rest > 0)
                {
                        struct ui_box r = ui_box_transform(dot, rest, rest, cx, oy, 0, 0);

                        r = ui_box_transform(r, hover, ui_qmul(dom, hover), cx, origin, 0, 0);
                        r = ui_box_transform(r, row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty);
                        ui_kit_fill(ui, &r, ui_role(UI_CONTROL_OFF), null);
                }
                label = ui_kit_colour(&c->item_ink[item]);
                x = ui_rtl() ? (rx + rw + row_tx) / 256 - ui_kit_measure(ui, (string_address)c->items[item], UI_BODY_LARGE) : (rx + row_tx) / 256;
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

                        ui_kit_ripples(ui, c, rx, ry, &place, &clip, ui_role(UI_PRIMARY));
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
                b32 py = (b32)((pip_y * ui->scale / 100) >> 22);
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

/*
        A vertical slider (UI_SLIDER_VERTICAL), the mixer's: a track UI_VSLIDER_BAR wide (hovered 8, held 4) from the box's top to
        its bottom, the value filling it from the bottom in the accent, the rest in the controls' off colour, and a knob that
        rides the value (16 round, 18 hovered, 14 held) with the slider's halo round it while the pointer is on it. The knob's
        centre travels the box less its own size, so the knob lies inside the box at both ends. c->on is a muted track: its fill
        is the outline's grey, and it can still be moved. Keys ring the knob. The click's slide is the horizontal slider's.
*/
#define UI_VSLIDER_KNOB 16

// The share of the track a value is, 0 at the bottom (the horizontal slider's ratio).
static b64 ui_vslider_travel(const struct ui address_to ui, const struct ui_control address_to c)
{
        return (b64)ui_dev(ui, c->h) - ui_dev(ui, UI_VSLIDER_KNOB);
}

// The value a device y is: the knob's centre, clamped to the track and snapped to the step.
static b64 ui_vslider_at(struct ui address_to ui, const struct ui_control address_to c, int y)
{
        b64 travel = ui_vslider_travel(ui, c), from = (b64)ui_dev(ui, c->y) + ui_dev(ui, c->h) - ui_dev(ui, UI_VSLIDER_KNOB) / 2 - y, ratio;

        ratio = travel > 0 ? ui_qdiv(ui_q((int)from), ui_q((int)travel)) : 0;
        ratio = ratio < 0 ? 0 : ratio > UI_ONE ? UI_ONE : ratio;
        return ui_slider_snap(c, c->low + ui_qmul(ratio, c->high - c->low));
}

// The knob's centre (256ths of a device pixel) for a value.
static b32 ui_vslider_knob_y(const struct ui address_to ui, const struct ui_control address_to c, b64 value)
{
        return ui_q8(ui, c->y + c->h) - ui_q8(ui, UI_VSLIDER_KNOB) / 2 - (b32)ui_qmul(ui_slider_ratio(c, value), ui_vslider_travel(ui, c) * 256);
}

static fn ui_vslider_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 live = 0, on_knob;
        b64 value = c->value, halo;
        int knob = c->down ? 14 : c->hover ? 18 : UI_VSLIDER_KNOB, bar = c->down ? 4 : c->hover ? 8 : 6;
        b32 cx = ui_q8(ui, c->x) + ui_q8(ui, c->w) / 2, top = ui_q8(ui, c->y), bottom = ui_q8(ui, c->y + c->h), ky, r;
        unsigned int accent = ui_role(UI_PRIMARY), fill_ink = c->on ? ui_role(UI_OUTLINE) : accent, off = ui_role(UI_CONTROL_OFF);
        struct ui_box track, fill, dot;

        if (c->sliding)
        {
                value = ui_slider_snap(c, ui_slide_value(&c->slide, ui_kit.now, &live));
                c->value = value;
                if (!live)
                        c->sliding = 0;
                ui_kit.busy |= live;
        }
        ky = ui_vslider_knob_y(ui, c, value);
        track = (struct ui_box){cx - ui_q8(ui, bar) / 2, top, cx + ui_q8(ui, bar) / 2, bottom, ui_q8(ui, bar) / 2, ui_q8(ui, bar) / 2};
        fill = track;
        fill.y0 = ky;
        ui_kit_fill(ui, &track, off, null);
        if (fill.y0 < fill.y1)
                ui_kit_fill(ui, &fill, fill_ink, &track);

        // The halo: the pointer on the knob rings it at 8%, held at 12%, on the slider's own timing.
        on_knob = (c->hover || c->down) && (b32)c->pointer_y * 256 >= ky - ui_q8(ui, knob) / 2 && (b32)c->pointer_y * 256 < ky + ui_q8(ui, knob) / 2;
        ui_kit_number_to(&c->shadow, on_knob ? (c->down ? UI_Q2(12) : UI_Q2(8)) : 0, 200, &ui_ease);
        ui_kit_number_to(&c->extent, on_knob ? ui_q(c->down ? 14 : 10) : 0, 200, &ui_ease);
        halo = ui_kit_tween(&c->shadow);
        if (halo > 0)
        {
                b32 ring = ui_q8(ui, knob) / 2 + (b32)((ui_kit_tween(&c->extent) * ui->scale / 100) >> 22);
                struct ui_box h = {cx - ring, ky - ring, cx + ring, ky + ring, ring, ring};

                ui_kit_fill(ui, &h, ui_mix(accent, (unsigned int)((halo * 100 + UI_HALF) >> UI_Q), 0), null);
        }
        r = ui_q8(ui, knob) / 2;
        dot = (struct ui_box){cx - r, ky - r, cx + r, ky + r, r, r};
        ui_kit_fill(ui, &dot, c->on ? ui_role(UI_OUTLINE) : ui_role(UI_ON_SURFACE), null);
        // The keys are on it: a ring in the accent, 3 out from the knob.
        if (c->focused)
        {
                b32 out = ui_q8(ui, 3);
                struct ui_box ring = {dot.x0 - out, dot.y0 - out, dot.x1 + out, dot.y1 + out, r + out, r + out};

                ui_kit_ring(ui, &ring, ui_q8(ui, 2), accent);
        }
}

static fn ui_slider_draw(struct ui address_to ui, struct ui_control address_to c)
{
        if (c->variant & UI_SLIDER_VERTICAL)
        {
                ui_vslider_draw(ui, c);
                return;
        }
        b64 sx, sy, value, ratio, squish = 0;
        b32 tx, ty, live = 0;
        b32 rx = ui_q8(ui, c->x), ry = ui_q8(ui, c->y), rw = ui_q8(ui, c->w), rh = ui_q8(ui, c->h);
        b32 ox = rx + rw / 2, oy = ry + rh / 2, sum = 2 * rx + rw;
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
        // The squish's side is the hand's (left or right of the window); this works as if reading from the left, so it is turned like the rest.
        origin = (ui_rtl() ? -c->squish_origin : c->squish_origin) > 0 ? rx : (ui_rtl() ? -c->squish_origin : c->squish_origin) < 0 ? rx + rw : rx + rw / 2;

        // The spot under the hand.
        {
                b64 spot = ui_kit_tween(&c->spot);

                if (spot > 0)
                {
                        // spot_x is the press's logical x in the control: scaled with the rest, not added in device pixels as it was.
                        b32 spx = ui_q8(ui, c->x + c->spot_x), spy = ry + rh - ui_q8(ui, 24);

                        // A radial ellipse 180 by 80, the accent at its middle to nothing at 70%: rings of it, outermost first.
                        for (int ring = 7; ring >= 0; ring--)
                        {
                                b32 hw = ui_q8(ui, 90) * (ring + 1) / 8 * 7 / 10, hh = ui_q8(ui, 40) * (ring + 1) / 8 * 7 / 10;
                                struct ui_box e = {spx - hw, spy - hh, spx + hw, spy + hh, hw, hh};
                                unsigned int share = (unsigned int)((spot * 100 + UI_HALF) >> UI_Q) * 100 / 8 / 100;

                                e = ui_box_transform(e, sx, sy, ox, oy, tx, ty);
                                ui_kit_fill_in(ui, e, ui_mix(accent, share ? share : 1, 0), null, sum);
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
                ui_kit_fill_in(ui, rest, off, null, sum);
        if (ratio > 0)
                ui_kit_fill_in(ui, fill, accent, null, sum);

        // The thumb's halo: the pointer on the thumb (its box, 20 hovered), a ring of 10 at 8% round it; pressed, 14 at 12%.
        {
                b32 thumb_x = rx + stop, hx = ui_point(c->pointer_x, ui_dev(ui, c->x), ui_dev(ui, c->w)) * 256;
                b32 on_thumb = (c->hover || c->down) && hx >= thumb_x - ui_q8(ui, thumb) / 2 && hx < thumb_x + ui_q8(ui, thumb) / 2;
                b64 halo;

                ui_kit_number_to(&c->shadow, on_thumb ? (c->down ? UI_Q2(12) : UI_Q2(8)) : 0, 200, &ui_ease);
                ui_kit_number_to(&c->extent, on_thumb ? ui_q(c->down ? 14 : 10) : 0, 200, &ui_ease);
                halo = ui_kit_tween(&c->shadow);
                if (halo > 0)
                {
                        b32 r = ui_q8(ui, thumb) / 2 + (b32)((ui_kit_tween(&c->extent) * ui->scale / 100) >> 22);
                        struct ui_box ring = {thumb_x - r, track_y - r, thumb_x + r, track_y + r, r, r};

                        ring = ui_box_transform(ring, sx, sy, ox, oy, tx, ty);
                        // The ring and the thumb's own background, both the accent at the halo's share.
                        ui_kit_fill_in(ui, ring, ui_mix(accent, (unsigned int)((halo * 100 + UI_HALF) >> UI_Q), 0), null, sum);
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
                        ui_kit_fill_in(ui, g, ghost, &bar_box, sum);
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
                        ui_kit_fill_in(ui, dot, filled && !line ? ui_mix(0xffffffffu, 90, 0) : ui_mix(ui_role(UI_ON_SURFACE), 22, 0), null, sum);
                }
        }

        // The label and the value, which ride the squish too.
        ink = c->hover || c->down ? accent : ui_role(UI_ON_SURFACE);
        baseline = ui_kit_baseline(ui, (ry + ui_q8(ui, 8) + ty) / 256, UI_BODY_LARGE);
        {
                // The label is at the start of the control and the value at its end: reading from the right, the label is at its right edge.
                b32 lx = origin + (b32)ui_qmul((ui_rtl() ? rx + rw - ui_kit_measure(ui, (string_address)c->label, UI_BODY_LARGE) * 256 : rx) - origin, UI_ONE + squish);

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
                vx = origin + (b32)ui_qmul((ui_rtl() ? rx : rx + rw - width * 256) - origin, UI_ONE + squish);
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
        return ui_kit_measure(ui, (string_address)c->items[item], UI_LABEL_LARGE) * 100 / ui->scale + 28;
}

static int ui_tab_left(struct ui address_to ui, const struct ui_control address_to c, int item)
{
        int x = c->x, gap = c->variant == UI_TABS_SEGMENTED ? 4 : 2;

        for (int k = 0; k < item; k++)
                x += ui_tab_width(ui, c, k) + gap;
        // Reading from the right the first tab is at the box's right end, and the strip runs to the left from it.
        return ui_rtl() ? c->x + c->w - (x - c->x) - ui_tab_width(ui, c, item) : x;
}

// The length of the strip's tabs and the gaps between them, whichever way it reads.
static int ui_tabs_length(struct ui address_to ui, const struct ui_control address_to c)
{
        int length = 0;

        for (int k = 0; k < c->count; k++)
                length += ui_tab_width(ui, c, k) + (k ? (c->variant == UI_TABS_SEGMENTED ? 4 : 2) : 0);
        return length;
}

static fn ui_vtabs_draw(struct ui address_to ui, struct ui_control address_to c);

#define UI_TABS_H 32 // a segmented tab's height, the pill (a strip of tabs, the editor's file tabs too)
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
        int height = seg ? UI_TABS_H : 40, chosen = c->at;
        b64 pip_x, pip_w;

        if (!seg)
        {
                struct ui_box hair = {ui_q8(ui, c->x), ui_q8(ui, c->y + height), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + height) + ui_stroke(ui), 0, 0};

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
static int ui_scroll_map(const struct ui_scroll address_to s, int y);

// A column's box edge in device Q8, mapped by its region's stretch (in whole device pixels, the items' own snap).
static b32 ui_span_q8(const struct ui_control address_to c, b32 q8)
{
        int px = (int)(q8 >> 8), mapped = c->span ? ui_scroll_map(c->span, px) : px;

        return mapped == px ? q8 : (b32)mapped * 256 + (q8 & 255); // at rest the value is the same, its fraction and all
}

// Where a column's rows run across, logical pixels: the whole row the page gave it (row_x, row_w: flush with the window's
// edge to the middle of the scroll bar's track), or the column's own box where it has none.
static fn ui_vtabs_row(const struct ui_control address_to c, int address_to x, int address_to w)
{
        *x = c->row_w > 0 ? c->row_x : c->x;
        *w = c->row_w > 0 ? c->row_w : c->w;
}

static fn ui_vtabs_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 busy = ui_kit.busy;
        b64 pip_y;
        int rx, rw;

        ui_vtabs_row(c, &rx, &rw);

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
                // The chosen group is its whole row, square, a touch off the ground: the surface's high face, as a field's well is.
                struct ui_box pip = ui_box_of(ui, ui_q(rx), pip_y, ui_q(rw), ui_q(c->h), 0);

                pip.y0 = (b32)ui_span_q8(c, (b32)((pip_y * ui->scale / 100) >> 22)), pip.y1 = (b32)ui_span_q8(c, (b32)(((pip_y + ui_q(c->h)) * ui->scale / 100) >> 22));

                if (!c->disabled && c->count > 0) // no items, no chosen one to show
                        ui_kit_fill(ui, &pip, ui_role(UI_SURFACE_HIGH), null);
                // The keys in the column: the kit's focus ring, on the row's own edge (it runs to the window's).
                if (c->focused && !c->disabled)
                        ui_kit_outline(ui, &pip, ui_q8(ui, 2), ui_role(UI_PRIMARY));
        }
        for (int item = 0; item < c->count; item++)
        {
                int y = c->y + item * c->h;
                b32 live = 0;
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live);
                b32 by = ui_span_q8(c, ui_q8(ui, y)), bh = ui_span_q8(c, ui_q8(ui, y + c->h)) - by, anchor = c->dom_origin > 0 ? by : by + bh;
                struct ui_box face = ui_box_of(ui, ui_q(rx), ui_q(y), ui_q(rw), ui_q(c->h), 0);

                face.y0 = (b32)by, face.y1 = (b32)(by + bh);
                unsigned int ink;
                b32 middle;

                busy |= live;
                ui_kit.busy = live;
                if (y + c->h + 8 < ui->top * 100 / ui->scale || y - 8 > ui->bottom * 100 / ui->scale)
                        continue;
                ui_kit_colour_to(&c->item_ink[item], item == c->at && !c->disabled ? ui_role(UI_ON_SURFACE) : ui_role(UI_ON_SURFACE_VARIANT), 200, &ui_ease);
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
                                struct ui_box pill = ui_box_of(ui, ui_q(rx), pip_y, ui_q(rw), ui_q(c->h), 0);

                                ui_kit_ripples_on(ui, c, ui_q8(ui, rx - 8), by - ui_q8(ui, 8), &place, &face, ink, &pill);
                        }
                }
                // The text at whole pixels where the stretch takes the item's middle.
                middle = anchor + (b32)ui_qmul(by + bh / 2 - anchor, dom);
                ui_kit_start(ui, ui_rtl() ? ui_dev(ui, c->x + c->w - 12) : ui_dev(ui, c->x + 12), ui_kit_baseline(ui, middle / 256 - ui_dev(ui, 10), UI_LABEL_LARGE),
                             (string_address)c->items[item], item == c->at ? UI_LABEL_LARGE : UI_BODY_MEDIUM, ink, 0, 0, ui->width);
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
        int above = bare ? 0 : 24 + 6, tall = bare && c->h ? c->h : UI_FIELD_H;
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
                b32 x = (b32)((now[k * 5] * ui->scale / 100) >> 22), y = (b32)((now[k * 5 + 1] * ui->scale / 100) >> 22);
                b32 blur = (b32)((now[k * 5 + 2] * ui->scale / 100) >> 22), spread = (b32)((now[k * 5 + 3] * ui->scale / 100) >> 22);
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
        {
                const font_face address_to faces[2] = {ui->regular, ui->regular};
                int start = ui_rtl() ? ui_dev(ui, c->x + c->w - 14) - ui_run_in(ui, faces, 0, 0, (string_address)c->items[0], 15, 0, 0, 0, ui->width, 0)
                                     : ui_dev(ui, c->x + 14);

                ui_run_in(ui, faces, start, baseline, (string_address)c->items[0], 15, ui_text_colour(ui, UI_ON_SURFACE) & 0xffffffu, 92,
                          ui_dev(ui, c->x + 14), ui_dev(ui, c->x + c->w - 14), 0);
        }
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
                ui_kit_fill_in(ui, fill, ui_role(UI_PRIMARY), &track, track.x0 + track.x1);
                if (!ui_kit.motion)
                        ui_kit.busy = 1;
                return;
        }
        ui_kit_number_to(&c->extent, ui_progress_whole(c->value), 450, &ui_ease);
        fill.x1 = fill.x0 + (b32)ui_qmul(ui_kit_tween(&c->extent), track.x1 - track.x0);
        if (fill.x1 > fill.x0)
                ui_kit_fill_in(ui, fill, ui_role(UI_PRIMARY), null, track.x0 + track.x1);
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
#define UI_DOT 6     // a status dot's width: a badge's, and a tab's in the editor (which takes it from here)
#define UI_DOT_IN 12 // a tab's dot in from the tab's right edge

// A status dot, UI_DOT across, its top left at a device point, in one colour: a badge's, and an editor tab's.
static fn ui_kit_dot(struct ui address_to ui, int x, int y, unsigned int argb)
{
        ui_kit_fill(ui, &(struct ui_box){x * 256, y * 256, (x + ui_px(ui, UI_DOT)) * 256, (y + ui_px(ui, UI_DOT)) * 256, ui_px(ui, UI_DOT / 2) * 256,
                                         ui_px(ui, UI_DOT / 2) * 256}, argb, null);
}

// A badge, a tag or a key takes the width of its words: what its draw sets (c->w, logical pixels), said first for a layout that places it.
static fn ui_control_size(struct ui address_to ui, struct ui_control address_to c)
{
        int width;

        switch (c->kind)
        {
        case UI_BADGE:
                c->w = ui_kit_measure(ui, (string_address)c->label, UI_LABEL_MEDIUM) * 100 / ui->scale + 18 + (c->on ? 2 * UI_DOT : 0);
                c->h = 22;
                break;
        case UI_TAG:
                c->w = ui_kit_measure(ui, (string_address)c->label, UI_MONO) * 100 / ui->scale + 20;
                c->h = 26;
                break;
        case UI_KBD:
                width = ui_kit_measure(ui, (string_address)c->label, UI_MONO) * 100 / ui->scale + 10;
                c->w = width < 20 ? 20 : width;
                break;
        }
}

static fn ui_badge_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int role = c->role, dot = c->on ? 2 * UI_DOT : 0;
        // A role's container is the next role, except the error's: its on-colour is laid before its container (role + 2).
        unsigned int fill = role == UI_PRIMARY ? ui_role(UI_PRIMARY_CONTAINER)
                            : role < 0         ? 0
                                               : ui_role(role == UI_ERROR ? UI_ERROR_CONTAINER : role + 1);
        unsigned int ink = role == UI_PRIMARY ? ui_role(UI_ON_PRIMARY_CONTAINER) : role < 0 ? ui_role(UI_ON_SURFACE_VARIANT) : ui_role(role);
        struct ui_box box;

        if (role == UI_SURFACE_HIGHEST)
                fill = ui_role(UI_SURFACE_HIGHEST), ink = ui_role(UI_ON_SURFACE_VARIANT);
        ui_control_size(ui, c);
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(22), ui_q(6));
        if (fill)
                ui_kit_fill(ui, &box, fill, null);
        if (role < 0)
                ui_kit_outline(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
        if (dot)
                ui_kit_dot(ui, ui_rtl() ? ui_dev(ui, c->x + c->w - 9) - ui_px(ui, UI_DOT) : ui_dev(ui, c->x + 9), ui_dev(ui, c->y + 8), ink);
        ui_control_label(ui, c, 9 + dot, 3, UI_LABEL_MEDIUM, ink);
}

static fn ui_tag_draw(struct ui address_to ui, struct ui_control address_to c)
{
        unsigned int ink = c->on ? ui_role(UI_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT);
        struct ui_box box;

        ui_control_size(ui, c);
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
                  (string_address)c->label, t->size, ui_role(UI_ON_SURFACE) & 0xffffffu, 255, ui_dev(ui, c->x), ui_dev(ui, c->x + size), 0);
        if (c->on)
        {
                struct ui_box ring = ui_box_of(ui, ui_q(c->x + size - 8 - 2), ui_q(c->y + size - 8 - 2), ui_q(13), ui_q(13), ui_q(7));
                struct ui_box dot = ui_box_of(ui, ui_q(c->x + size - 8), ui_q(c->y + size - 8), ui_q(9), ui_q(9), ui_q(5));

                ui_kit_fill_in(ui, ring, ui_role(UI_SURFACE_LOW), null, box.x0 + box.x1);
                ui_kit_fill_in(ui, dot, ui_role(UI_SUCCESS), null, box.x0 + box.x1);
        }
}

static fn ui_spinner_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = c->w ? c->w : 20;
        b64 ring = size == 14 ? ui_q(2) : size == 28 ? ui_q(3) : ui_q(5) / 2;
        b32 cx = ui_q8(ui, c->x) + ui_q8(ui, size) / 2, cy = ui_q8(ui, c->y) + ui_q8(ui, size) / 2;
        b32 outer = ui_q8(ui, size) / 2, inner = outer - (b32)((ring * ui->scale / 100) >> 22);
        b64 turn = ui_kit.motion ? 0 : ui_progress(ui_kit.now % (700 * UI_MS), 700 * UI_MS); // linear, a turn in .7 s
        b64 sine, cosine;
        b32 first = (cx - outer) >> 8, last = (cx + outer + 255) >> 8;

        // It turns the way a clock's hands do whichever way the window reads: its place is mirrored, its picture is not.
        UI_KEEP(first, (cy - outer) >> 8, last - first, ((cy + outer + 255) >> 8) - ((cy - outer) >> 8));
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

                                ui_kit_put(at, 0, hits * UI_FULL / 16, ui_role(UI_PRIMARY));
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
                        ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x), ui_q8(ui, c->y + 68), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + 68) + ui_stroke(ui), 0, 0},
                                    ui_role(UI_OUTLINE_VARIANT), null);
                }
        }
        else
                ui_kit_outline(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
}

static fn ui_callout_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int role = c->role;
        unsigned int fill = role ? ui_role(role == UI_PRIMARY ? UI_PRIMARY_CONTAINER : role == UI_ERROR ? UI_ERROR_CONTAINER : role + 1)
                                 : ui_role(UI_SURFACE_LOW);
        unsigned int ink = role ? ui_role(role == UI_PRIMARY ? UI_ON_PRIMARY_CONTAINER : role == UI_ERROR ? UI_ON_ERROR_CONTAINER : role + 2)
                                : ui_role(UI_ON_SURFACE_VARIANT);
        unsigned int line = role ? ui_mix(ui_role(role), 28, ui_role(UI_OUTLINE_VARIANT)) : ui_role(UI_OUTLINE_VARIANT);
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(14));

        ui_kit_fill(ui, &box, fill, null);
        ui_kit_outline(ui, &box, ui_q8(ui, 1), line);
        ui_control_label(ui, c, 16, 14, UI_LABEL_LARGE, role ? ink : ui_role(UI_ON_SURFACE));
        if (c->text[0])
                ui_kit_start(ui, ui_rtl() ? ui_dev(ui, c->x + c->w - 16) : ui_dev(ui, c->x + 16), ui_kit_baseline(ui, ui_dev(ui, c->y + 14 + 24), UI_BODY_MEDIUM),
                             (string_address)c->text, UI_BODY_MEDIUM, ink, 0, ui_rtl() ? ui_dev(ui, c->x) : 0, ui_dev(ui, c->x + c->w));
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
                ui_kit_start(ui, ui_rtl() ? ui_dev(ui, c->x + c->w - 14) : ui_dev(ui, c->x + 14), ui_kit_baseline(ui, ui_dev(ui, c->y + 37 + 14 + k * 21), UI_CODE),
                             (string_address)c->items[k], UI_CODE, ui_role(UI_ON_SURFACE), 0, ui_rtl() ? ui_dev(ui, c->x) : 0, ui_dev(ui, c->x + c->w));
}

static fn ui_kbd_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int width = ui_kit_measure(ui, (string_address)c->label, UI_MONO);
        struct ui_box box, foot;

        ui_control_size(ui, c);
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
        int along = ui_point(x, ui_dev(ui, c->x), ui_dev(ui, c->w)); // as if reading from the left

        if (!c->count || along < left || along >= left + c->count * step ||
            !ui_hit(ui, ui_kit_touch, x, y, 0, ui_dev(ui, c->y + c->h / 2 - UI_SWATCH / 2), ui->width, ui_dev(ui, UI_SWATCH)))
                return -1;
        return (along - left) / step;
}

static fn ui_swatches_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = ui_dev(ui, UI_SWATCH), step = ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP), left = ui_swatches_left(ui, c);
        b32 out = ui_q8(ui, 3), s = size * 256, y = (ui_dev(ui, c->y) + (ui_dev(ui, c->h) - size) / 2) * 256;

        for (int k = 0; k < c->count; k++)
        {
                long colour = window_colour_parse((string_address)c->items[k]);
                b32 x = ui_mirror(left + k * step, size, ui_dev(ui, c->x), ui_dev(ui, c->w)) * 256;
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
                if (ui_rtl() && c->w > 0)
                        ui_kit_start(ui, ui_dev(ui, c->x + c->w), ui_kit_baseline(ui, ui_dev(ui, c->y), c->variant), (string_address)c->label, c->variant,
                                     ui_role(c->role), 0, ui_dev(ui, c->x), ui_dev(ui, c->x + c->w));
                else
                        ui_kit_text_clip(ui, ui_dev(ui, c->x), ui_kit_baseline(ui, ui_dev(ui, c->y), c->variant), (string_address)c->label, c->variant,
                                         ui_role(c->role), 0, c->w > 0 ? ui_dev(ui, c->x + c->w) : ui->width);
                break;
        case UI_RULE:
                ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x), ui_q8(ui, c->y), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y) + ui_stroke(ui), 0, 0},
                            ui_role(UI_OUTLINE_VARIANT), null);
                break;
        }
}

/* ---- the pointer ---- */
/*
        The pointer and the scroll region. ui_event_context(event) is the one place an event's touch flag and time are taken
        (ui_kit_touch, ui_kit.now). ui_scroll_* is every scrolled region: ui_scroll_set, ui_scroll_show and ui_scroll_reveal place
        it, ui_scroll_pointer and ui_scroll_wheel take input, ui_scroll_draw paints the bar and the stretch. The rubber band, the
        wheel's burst and the hand are the kit's (ui_wheel_*, ui_scroll_hand): an app forwards its input and keeps no stretch.
*/

// Whether a device point is in a control's box (a radio group: which row; tabs: which tab), -1 when not.
static int ui_control_hit(struct ui address_to ui, const struct ui_control address_to c, int x, int y)
{
        int lx = x * 100 / ui->scale, ly = y * 100 / ui->scale;

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
                int rx, rw;

                ui_vtabs_row(c, &rx, &rw);
                for (int k = 0; k < c->count; k++)
                        if (ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, rx), ui_dev(ui, c->y + k * c->h), ui_dev(ui, rw), ui_dev(ui, c->h)))
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
                return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h ? c->h : UI_FIELD_H)) ? 0 : -1;
        if (c->kind == UI_INPUT)
                return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + 30), ui_dev(ui, c->w), ui_dev(ui, UI_FIELD_H)) ? 0 : -1;
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
static b64 ui_slider_at(struct ui address_to ui, const struct ui_control address_to c, int x, int y)
{
        if (c->variant & UI_SLIDER_VERTICAL)
                return ui_vslider_at(ui, c, y);
        b64 ratio = ui_qdiv(ui_q(ui_point(x, ui_dev(ui, c->x), ui_dev(ui, c->w)) - ui_dev(ui, c->x)), ui_q(ui_dev(ui, c->w)));

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

/*
        What every event is handled in, taken once for every loop that reads events (ui_app_run, a popup's pump), so no
        app and no control passes it around: whether it is a finger's (WINDOW_KEY_TOUCH: what it can press and grab is at
        least UI_TARGET each way, in every control and every scrollbar, ui_hit) and the time it is handled at, which is what
        the animations an event starts are stamped with (the frame's clock is the last frame's, and an idle window's is old).
*/
static fn ui_event_context(const struct window_event address_to event)
{
        ui_kit_touch = (event->flags & WINDOW_KEY_TOUCH) != 0;
        ui_kit_retract = event->type == WINDOW_EVENT_MOTION && (event->flags & WINDOW_KEY_SETTLE) != 0;
        ui_kit_repeat = event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_REPEAT) != 0;
        ui_kit.now = (b64)clock_monotonic_nanoseconds();
        // A press or a key carries no place: the window's pointer is the last motion's, so an app asks the kit for it (ui_text_send).
        if (event->type == WINDOW_EVENT_MOTION)
                ui_kit.pointer_x = event->a, ui_kit.pointer_y = event->b;
}

// The pointer's buttons, the evdev codes a press carries in a (the left, the right and the middle).
#define UI_POINTER_LEFT 0x110
#define UI_POINTER_RIGHT 0x111
#define UI_POINTER_MIDDLE 0x112

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
                if (c->bend > UI_REST_SCALE && !ui_kit.motion)
                        ui_spring_go(&c->held_squish, c->bend, 0);
                c->bend = c->bend_target = 0;
                return null;
        }

        if (type == WINDOW_EVENT_MOTION)
        {
                struct ui_control address_to c = ui_kit_held;

                if (c)
                {
                        int dx = x - c->press_x, dy = y - c->press_y;
                        b64 ldx = ui_qdiv(ui_q(dx), ui_q(ui->scale) / 100), ldy = ui_qdiv(ui_q(dy), ui_q(ui->scale) / 100);

                        c->pointer_x = x, c->pointer_y = y;
                        // A finger that moves past the slop is a drag: what it lifts on is no click (the kit's press rule).
                        if (ui_kit_touch && !c->moved && ui_press_past(ui, c->press_x, c->press_y, x, y))
                                c->moved = 1;
                        if (c->kind == UI_BUTTON)
                        {
                                ui_squish_move(&c->squish, ui_squash_of(ldx, ldy));
                                // pointerleave ends the ripple.
                                if (ui_control_hit(ui, c, x, y) < 0)
                                        ui_kit_ripples_exit(c);
                        }
                        else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS)
                        {
                                b32 right = dx > 0, toward = (right != ui_rtl()) != (c->kind == UI_RADIOS ? c->item == c->at : c->on);
                                b64 fill;

                                if (!c->moved && dx < 3 * ui->scale / 100 && dx > -3 * ui->scale / 100 && dy < 3 * ui->scale / 100 && dy > -3 * ui->scale / 100)
                                        return null;
                                c->moved = 1;
                                if (c->kind != UI_RADIOS)
                                {
                                        fill = (c->on ? UI_ONE : 0) + (ui_rtl() ? -ldx : ldx) / 100;
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

                                if (c->variant & UI_SLIDER_VERTICAL)
                                {
                                        // Stood on its end it follows the hand's y, and has no stretch.
                                        if (!c->moved && dy < 4 * ui->scale / 100 && dy > -4 * ui->scale / 100)
                                                return null;
                                        c->moved = 1;
                                        c->sliding = 0;
                                        c->value = ui_slider_at(ui, c, x, y);
                                        return c;
                                }
                                if (!c->moved && dx < 4 * ui->scale / 100 && dx > -4 * ui->scale / 100)
                                        return null;
                                c->moved = 1;
                                c->sliding = 0;
                                c->value = ui_slider_at(ui, c, x, y);
                                c->spot_x = ui_point(x, left, right - left) * 100 / ui->scale - c->x;
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
                                c->hover_x = hit >= 0 ? ui_point(x, ui_dev(ui, c->x), ui_dev(ui, c->w)) : -1, c->pointer_x = x, c->pointer_y = y; // the thumb's ring follows the hand
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
                                int rx, rw;
                                struct ui_ripple address_to r;

                                ui_vtabs_row(c, &rx, &rw);
                                r = c->variant == UI_TABS_VERTICAL
                                                                    ? ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, rx), ui_dev(ui, c->y + hit * c->h),
                                                                                          ui_dev(ui, rw), ui_dev(ui, c->h), 8)
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
                                ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x), ui_dev(ui, ui_radio_row_y(c, hit)), ui_dev(ui, c->w), ui_dev(ui, c->h), 0);
                        else if (c->kind == UI_INPUT)
                                c->focused = 1;
                        else if (c->kind == UI_SLIDER)
                        {
                                b64 from = c->value, to = ui_slider_at(ui, c, x, y);

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
                                c->spot_x = x * 100 / ui->scale - c->x;
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
                        if (!c->moved && ui_control_hit(ui, c, x, y) >= 0)
                                changed = c;
                }
                else if (c->kind == UI_TABS)
                {
                        ui_squish_release(&c->squish);
                        if (!c->moved && ui_control_hit(ui, c, x, y) == c->item)
                        {
                                ui_tabs_choose(ui, c, c->item);
                                changed = c;
                        }
                }
                else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS)
                {
                        int dx = x - c->press_x;
                        b64 ldx = ui_qdiv(ui_q(dx), ui_q(ui->scale) / 100);
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
                                                int lx = x * 100 / ui->scale;

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
                                        c->dom_origin = (c->on ? 1 : -1) * ui_dir();
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
        Tabs handed a pointer event: a press on a tab and its release are the tabs' (taken), a motion moves their hover, and what another
        control holds the pointer for is left to it. The bits say what to do with it: the picture is due again, the event was the tabs',
        a tab was chosen (c->at is it). x, y: where the pointer is, for a press, which carries no place of its own.
*/
#define UI_TABS_DRAW 1u
#define UI_TABS_TAKEN 2u
#define UI_TABS_CHOSEN 4u

static p32 ui_tabs_pointer(struct ui address_to ui, struct ui_control address_to c, const struct window_event address_to event, int x, int y)
{
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0, had = c->down;
        int was = c->hover | c->down << 1;
        p32 bits;

        if ((event->type != WINDOW_EVENT_MOTION && event->type != WINDOW_EVENT_BUTTON) || (ui_kit_held && ui_kit_held != c) ||
            (event->type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT))
                return 0;
        if (event->type == WINDOW_EVENT_MOTION)
                x = event->a, y = event->b;
        bits = ui_kit_pointer(ui, c, 1, (int)event->type, x, y, down, event->flags) ? UI_TABS_DRAW | UI_TABS_CHOSEN : 0;
        bits |= was != (c->hover | c->down << 1) ? UI_TABS_DRAW : 0;
        bits |= event->type == WINDOW_EVENT_BUTTON && (c->down || had) ? UI_TABS_TAKEN : 0;
        return bits;
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
        // A list's bounds (UI_LIST_STRETCH): a pull past an end stretches the rows in the scroll axis, from the edge it is
        // pulled past, and the position stays at the end; edge is the end the spring takes home (-1 top, 1 bottom). A region
        // that fits does not move under a finger unless it is bounded. scale is the kit's, for the pull's size in logical pixels.
        b32 bounds;
        int edge, scale;
        b64 notch;                 // when the region last took a notch of the wheel, ns (ui_wheel_rate)
        b64 pulled_at;             // when the hand took the region, ns: a wheel's pull springs home at its apex (UI_NOTCH_APEX_NS)
        b32 absorb;                // a wheel's burst has had its bump at an end: the notches that go on pushing there are taken and move nothing
        b32 hand;                  // a notch past an end or a precise input has the region in hand (ui_scroll_hand)
        b32 precise;               // the hand is a trackpad's: its release throws on its speed. A wheel's notches are no finger, and throw nothing
        struct ui_drag hand_drag;  // the hand's reports, fitted for the release's speed (ui_scroll_release)
};

// A list stretches past its bounds (ui_list_row_span): a pull past an end, and a wheel turn at one, scale its rows in the scroll axis.
#define UI_LIST_STRETCH 1

/*
        The track's top and height (device pixels): the region's, from its top to its end, except that it starts below its
        inset: a window's close button is there, so a window's bar reaches the window's bottom and no higher than that.
*/
#define UI_SCROLL_INSET 1
static void ui_scroll_track(const struct ui address_to ui, const struct ui_scroll address_to s, int address_to top, int address_to height)
{
        int inset = s->inset * ui->scale / 100, bottom = s->y + s->height;

        *top = s->y > inset ? s->y : inset;
        *height = bottom > *top ? bottom - *top : 0;
}

// The thumb's top and length (device pixels); false when everything shows and there is no bar. A held thumb is drawn at its
// target, the pointer's or a wheel turn's glide aim, while a glide runs to it, and otherwise where the scroll is: a bar whose
// aim was never set is not drawn at it, and nothing lags the hand.
static b32 ui_scroll_thumb(const struct ui address_to ui, const struct ui_scroll address_to s, int address_to top, int address_to length)
{
        int track, at, least = UI_SCROLL_LEAST * ui->scale / 100, rest = s->content - s->view, scroll;

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

// The region's position is moving: a glide, a hand on it, or a stretch springing home (ui_scroll_moving is the thumb's ink).
static b32 ui_scroll_going(const struct ui_scroll address_to s)
{
        return s->gliding || s->back.live || s->dragging;
}

// The thumb's box for a top and length (device pixels): across the track, its thickness is the drawn one times the growth.
static int ui_scroll_map(const struct ui_scroll address_to s, int y);
static int ui_scroll_track_map(const struct ui_scroll address_to s, int y);

static void ui_scroll_box(const struct ui address_to ui, struct ui_scroll address_to s, int top, int length, struct ui_box address_to thumb)
{
        int border = UI_SCROLL_BORDER * ui->scale / 100, width = UI_SCROLL_WIDTH * ui->scale / 100;
        b64 centre = (b64)(s->x + width / 2) * 256, half = (b64)(width / 2 - border) * 256;
        b64 r = ui_qmul(half, ui_kit_tween(&s->grow));

        // The thumb's ends go where a stretch draws the track's: the same map as the items, in the same axis and by the same spring.
        thumb->x0 = (b32)(centre - r), thumb->x1 = (b32)(centre + r);
        thumb->y0 = (b32)(ui_scroll_track_map(s, top + border) * 256), thumb->y1 = (b32)(ui_scroll_track_map(s, top + length - border) * 256);
        thumb->rx = thumb->ry = (b32)r;
}

// The track's rows are drawn again next frame, as a moving control's are.
static fn ui_scroll_mark(const struct ui address_to ui, const struct ui_scroll address_to s)
{
        int top, height, bottom;

        ui_scroll_track(ui, s, &top, &height);
        bottom = ui_scroll_track_map(s, top + height), top = ui_scroll_track_map(s, top);
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
static b32 ui_scroll_reach(const struct ui address_to ui, const struct ui_scroll address_to s, int x, int y)
{
        int top, height;

        ui_scroll_track(ui, s, &top, &height);
        return ui_hit(ui, ui_kit_touch, x, y, s->x, top, UI_SCROLL_WIDTH * ui->scale / 100, height);
}

/*
        The pointer on a scrollbar: motion (a held thumb follows the hand; hover lights it), a press (on the thumb it is
        held, on the track it pages) and a release. A finger (touch) takes the bar within its reach (ui_scroll_reach) and
        the thumb within UI_TARGET likewise. Answers the scroll it asks for, or -1 when the event was not its own
        (a press off it, a motion with nothing held); a press on it that pages answers the new scroll.
*/
static void ui_scroll_show(struct ui_scroll address_to s);

static int ui_scroll_pointer(const struct ui address_to ui, struct ui_scroll address_to s, int type, int x, int y, b32 down)
{
        int top, length, at, track, rest = s->content - s->view;
        b32 touch = ui_kit_touch;
        b32 bar = ui_scroll_thumb(ui, s, &top, &length);
        b32 on_track = bar && ui_scroll_reach(ui, s, x, y);
        b32 on_thumb = on_track && ui_hit(ui, touch, x, y, s->x, top, UI_SCROLL_WIDTH * ui->scale / 100, length);

        ui_scroll_track(ui, s, &at, &track);
        if (type == WINDOW_EVENT_MOTION)
        {
                s->hover = on_thumb && !touch; // a finger hovers nothing: the thumb lights where it is held
                if (!s->held || !bar)
                        return -1;
                if (track - length <= 0)
                        return s->scroll;
                // A bounded region's thumb dragged past an end stretches it from that edge, one for one with the hand, as a finger's
                // pull does: the scroll stays at the end and the stretch is held until the release (ui_page_bars lets it go).
                if (s->bounds && (y - s->grab < at || y - s->grab > at + track - length))
                {
                        int thumb = y - s->grab;
                        b64 pull = thumb > at ? thumb - (at + track - length) : thumb - at;

                        s->dragging = 1, s->gliding = 0, s->back.live = 0, s->speed = s->stamp = 0, s->flung = 0;
                        s->at = thumb > at ? ((b64)rest + pull) * 256 : pull * 256; // 256ths of a device pixel
                        ui_scroll_show(s);
                        return s->scroll;
                }
                s->dragging = 0; // back within the track: the drag is the scroll's again
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
        // A finger beside the bar but off its thumb is on the content it means to drag or press, not the track: only a pointer pages.
        if (touch)
                return -1;
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
#define UI_FLING_TAU_NS 500000000ll  // a throw's speed falls by e in this long, whatever the region's length: macOS's normal rate (0.998 a millisecond)
#define UI_SCROLL_BACK_PACE UI_Q2(125) // the overscroll's spring home runs this much faster than the kit's release spring (a touch quicker)
#define UI_HAND_GAP_NS 40000000l     // the hand is let go this long after its last report: a wheel or a precise input has no release of its own
#define UI_NOTCH_APEX_NS 120000000l  // a wheel's pull grows this long, then springs home though the notches go on (a Mac's trackpad sends them for a second)
#define UI_NOTCH_BURST_NS 150000000l // notches closer than this are one burst: a burst bumps an end once, and what goes on pushing there is taken
#define UI_RUBBER_CARRY 400          // the most a release carries past an end, in px a second: the spring's first frames run on from it
#define UI_FLING_MAX 6000            // the fastest a throw may be, in px a second: a release is a flick, not a jump
#define UI_SCROLL_VIEW_PAST 1        // the hand may hold the overscroll up to a view past the end
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

// Where the region is drawn, in 256ths: where it is, or past an end while a finger holds it there. A bounded region stays at
// its end (the pull is its stretch); a region that fits does not move at all.
static b64 ui_scroll_shown(const struct ui_scroll address_to s)
{
        b64 most = ui_scroll_most256(s);

        if (!s->dragging)
                return s->at;
        if (s->bounds)
                return s->at < 0 ? 0 : s->at > most ? most : s->at;
        if (!most)
                return 0;
        if (s->at < 0)
                return s->flung ? 0 : ui_scroll_give(s, s->at);
        return s->at > most ? (s->flung ? most : most + ui_scroll_give(s, s->at - most)) : s->at;
}

// The stretch a pull of past (256ths of a device pixel) makes: the kit's squash curve of it over 400 logical pixels, as the
// squish's is (Q30 less one). A scale that rises with the pull and flattens as it grows.
static b64 ui_scroll_pull(const struct ui_scroll address_to s, b64 past)
{
        b64 z = past < 0 ? 0 : past > (1ll << 24) ? (1ll << 24) : past;

        z = (z << 22) * 100 / (s->scale > 0 ? s->scale : 100) / 400;
        return ui_squash_curve(z);
}

// A bounded region's stretch now, Q30 less one, and the edge it is stretched from (-1 top, 1 bottom, 0 none): a finger's pull
// past an end (none while it flings, which clamps), else the release's spring taking it home. The spring does not overshoot:
// its amplitude is the pull and its decay is faster than its ring, so it runs down to rest from the pull's side.
static b64 ui_scroll_stretched(const struct ui_scroll address_to s, int address_to edge)
{
        b64 most = ui_scroll_most256(s);

        *edge = 0;
        if (!s->bounds)
                return 0;
        if (s->dragging)
        {
                if (s->flung)
                        return 0;
                if (s->at < 0)
                {
                        *edge = -1;
                        return ui_scroll_pull(s, -s->at);
                }
                if (s->at > most)
                {
                        *edge = 1;
                        return ui_scroll_pull(s, s->at - most);
                }
                return 0;
        }
        if (!s->back.live)
                return 0;
        *edge = s->edge;
        if (s->bounds)
        {
                b64 past = s->back.value[1] >> 22; // the overscroll, in 256ths, outward

                return ui_scroll_pull(s, past > 0 ? past : 0);
        }
        return s->back.value[0];
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
        s->hand = s->absorb = 0;
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
        {
                // A finger may hold the region past its ends by as much as a hand does (UI_SCROLL_VIEW_PAST views), and no more: a pull
                // of any length is a stretch of one size, and springs home in one time.
                b64 most = ui_scroll_most256(s), lim = (b64)(s->view > 0 ? s->view : 0) * UI_SCROLL_SCALE * UI_SCROLL_VIEW_PAST;

                s->at = s->at < -lim ? -lim : s->at > most + lim ? most + lim : s->at;
        }
        ui_scroll_show(s);
        return s->scroll != before || s->bounds; // a bounded region's pull is a stretch, drawn whatever the scroll does
}

static b64 ui_wheel_clock(void);
static b32 ui_scroll_wheel(const struct ui address_to ui, struct ui_scroll address_to s, int delta);

// The overscroll a bounded region's spring holds, where its end is: past the edge, outward, in 256ths.
static b64 ui_scroll_past_at(const struct ui_scroll address_to s)
{
        b64 most = ui_scroll_most256(s), past = s->back.value[1] >> 22, edge = s->edge < 0 ? 0 : most;

        past = past < 0 ? 0 : past;
        return s->edge < 0 ? edge - past : edge + past;
}

// A finger on the region: whatever was moving it stops where it is drawn, and the drag takes it from there. A stretch being sprung
// home stops past its end by what the spring still holds, so nothing jumps.
static void ui_scroll_stop(struct ui_scroll address_to s)
{
        if (s->gliding || s->back.live)
        {
                s->at = s->aim = s->bounds && s->back.live ? ui_scroll_past_at(s) : ui_scroll_shown(s);
                s->gliding = s->back.live = 0;
                ui_spring_stop(&s->back);
                s->speed = s->stamp = 0;
        }
        s->hand = s->absorb = 0;
}

// The finger let go: a pull springs back to the end it is past, carrying the release's speed outward, and a throw glides on from
// where it is (speed in pixels a second, the region's way).
static void ui_scroll_let_go(struct ui_scroll address_to s, b32 thrown, long speed)
{
        b64 most = ui_scroll_most256(s), shown, over;
        int from = 0;
        b32 pulled;

        if (!s->dragging)
                return;
        shown = ui_scroll_shown(s);
        pulled = ui_scroll_stretched(s, &from) != 0;
        s->dragging = 0;
        s->stamp = 0;
        if (s->at < 0 || s->at > most)
        {
                b64 edge = s->at < 0 ? 0 : most;
                long carry = speed > UI_RUBBER_CARRY ? UI_RUBBER_CARRY : speed < -UI_RUBBER_CARRY ? -UI_RUBBER_CARRY : speed;

                over = s->at < 0 ? -s->at : s->at - most;
                from = s->at < 0 ? -1 : 1;
                s->gliding = s->speed = 0;
                if (s->bounds)
                {
                        // The overscroll is the distance past the end, in Q30 pixels, and the spring takes it home; its speed is the
                        // release's, outward, so a fast release runs on a little before the spring turns it home.
                        s->at = s->aim = edge, s->edge = from;
                        if (pulled)
                        {
                                ui_spring_go(&s->back, 0, over << 22);
                                s->back.pace = UI_SCROLL_BACK_PACE;
                                ui_spring_speed(&s->back, 1, (b64)(from > 0 ? carry : -carry) * UI_ONE);
                        }
                }
                else if (!most)
                        s->at = s->aim = 0; // a region that fits moved for nothing: nothing springs
                else
                {
                        s->at = shown, s->aim = edge;
                        // The kit's release spring takes the part past the end home: its amplitude is that part, in Q30 pixels.
                        ui_spring_go(&s->back, 0, (shown - edge) << 22);
                        s->back.pace = UI_SCROLL_BACK_PACE;
                }
                ui_scroll_show(s);
                return;
        }
        if (thrown)
        {
                // A throw's speed falls by e in UI_FLING_TAU_NS whatever the region's length, the one rule for every flick (the
                // distance is the speed times it); the glide stops at the region's ends (ui_scroll_fit) and never passes its aim.
                speed = speed > UI_FLING_MAX ? UI_FLING_MAX : speed < -UI_FLING_MAX ? -UI_FLING_MAX : speed;
                s->speed = (b64)speed * UI_SCROLL_SCALE;
                ui_scroll_glide_to(s, s->at + s->speed * UI_FLING_TAU_NS / 1000000000ll, UI_FLING_TAU_NS);
        }
        ui_scroll_show(s);
}

// The hand let go (no report for UI_HAND_GAP_NS): its release is the fit over its last reports, from the last of them, and the
// region goes on as a finger's release does. A trackpad's hand is a finger, and throws on that speed; a wheel's notches are not
// (they are a distance each, not a place the finger is at), and a stretch they pulled springs home and a region inside its ends stops.
static void ui_scroll_release(struct ui_scroll address_to s)
{
        positive last = s->hand_drag.samples > 0 ? s->hand_drag.when[s->hand_drag.samples - 1] : 0;
        long speed = last ? ui_drag_velocity(&s->hand_drag, last) : 0;
        b32 thrown = s->precise && (speed >= UI_DRAG_THROW || speed <= -UI_DRAG_THROW);

        s->hand = 0;
        ui_scroll_let_go(s, thrown, speed);
        // A wheel's notches have no release of their own: the gap that ended the hand is not the end of the burst (a trackpad's momentum
        // goes on for a second), so what goes on pushing at the end is taken (ui_scroll_wheel) and the stretch goes home once.
        s->absorb = !s->precise && s->bounds && s->back.live;
}

// The hand's move, in 256ths of a device pixel (positive: the content moves down the region). It takes the region from where it is
// drawn: a glide or a spring is stopped there, and a region at rest starts from its place. Its reports are fitted for the release,
// and the region may be held past its ends by UI_SCROLL_VIEW_PAST views (the stretch draws how far, through ui_scroll_pull).
static void ui_scroll_hand(struct ui_scroll address_to s, b64 move, positive now)
{
        b64 most = ui_scroll_most256(s), lim = (b64)(s->view > 0 ? s->view : 0) * UI_SCROLL_SCALE * UI_SCROLL_VIEW_PAST;

        if (!s->dragging || !s->hand)
        {
                // A stretch being sprung home is taken where it is (ui_scroll_stop put the position there): a pull that came while
                // the last one was going home goes on from it, and does not start again from the end.
                b32 sprung = s->bounds && s->back.live;

                ui_scroll_stop(s);
                if (!sprung)
                        ui_scroll_rest(s);
                s->dragging = 1, s->gliding = 0, s->back.live = 0, s->speed = s->stamp = 0, s->flung = 0;
                s->hand = 1;
                s->pulled_at = now;
                ui_drag_start(&s->hand_drag, (int)(s->at >> 8), now);
        }
        s->at += move;
        s->at = s->at < -lim ? -lim : s->at > most + lim ? most + lim : s->at;
        ui_scroll_show(s);
        ui_drag_follow(&s->hand_drag, (int)(s->at >> 8), now);
}

// A precise input (256ths of a device pixel, positive: the content moves down): one for one, with no notch and no glide.
static b32 ui_scroll_precise(const struct ui address_to ui, struct ui_scroll address_to s, int q)
{
        int before = s->scroll;
        b64 now = ui_wheel_clock();

        if (!q)
                return false;
        s->notch = now;
        s->precise = 1;
        s->absorb = 0;
        ui_scroll_hand(s, -(b64)q, now);
        return s->scroll != before || s->bounds;
}

// Only the overscroll of a bounded region is drawn past its end: the rest is the glide's, and the same for every region.
static void ui_scroll_busy(const struct ui address_to ui, struct ui_scroll address_to s)
{
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
}

// The glide's frame, drawn at the kit's time: where the region is now, and the kit busy with it while it moves (its band
// is marked to be drawn again, as a moving control's rows are). A pull springing home is the kit's spring. True when moving.
static b32 ui_scroll_glide(const struct ui address_to ui, struct ui_scroll address_to s)
{
        b64 now = (b64)ui_kit.now, before = s->at, q, w, e, d, v, c, ct;

        if (s->hand && s->dragging)
        {
                b64 wheel = ui_wheel_clock();

                if (wheel > s->notch && wheel - s->notch > UI_HAND_GAP_NS)
                        ui_scroll_release(s);
                if (s->dragging)
                {
                        ui_scroll_show(s);
                        ui_scroll_busy(ui, s);
                        return true;
                }
        }
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
        {
                ui_spring_at(&s->back, now);
                // A bounded region's spring holds its position at the end and draws the overscroll (ui_scroll_stretched).
                if (!s->bounds)
                        s->at = s->aim + (s->back.value[1] >> 22);
        }
        ui_scroll_show(s);
        if (!s->gliding && !s->back.live && s->at == before)
                return false;
        ui_scroll_busy(ui, s);
        return true;
}

// The wheel's clock: a test's, when it has one, else the monotonic clock the notches arrive on.
static b64 ui_wheel_clock(void)
{
        return ui_kit.wheel_clock ? ui_kit.wheel_clock : (b64)clock_monotonic_nanoseconds();
}

/*
        The wheel's acceleration, one curve for every region that takes the wheel: a notch after a pause of UI_WHEEL_SLOW_NS
        moves one unit, a notch within UI_WHEEL_FAST_NS of the region's last moves UI_WHEEL_FAST (Q8) units, and between them
        the rate is a straight line in the time since that last notch. A fast spin travels further than the same notches fired
        slowly, and a single notch still moves one unit. ui_scroll_wheel caps the travel (UI_WHEEL_REACH) so a long spin does
        not run away, and the glide settles on its aim (ui_scroll_limit), so it never overshoots.
*/
#define UI_WHEEL_SLOW_NS 180000000l
#define UI_WHEEL_FAST_NS 45000000l
#define UI_WHEEL_FAST 1024 // Q8: four units a notch at the fast end
#define UI_WHEEL_REACH 12  // units a spin may carry the aim past where the region is drawn
#define UI_WHEEL_RUN 60    // rows a second the glide of a spin may run at most

static b64 ui_wheel_rate(const struct ui_scroll address_to s, b64 now)
{
        b64 gap = s->notch && now > s->notch ? now - s->notch : UI_WHEEL_SLOW_NS;

        if (gap >= UI_WHEEL_SLOW_NS)
                return 256;
        if (gap <= UI_WHEEL_FAST_NS)
                return UI_WHEEL_FAST;
        return 256 + (UI_WHEEL_FAST - 256) * (UI_WHEEL_SLOW_NS - gap) / (UI_WHEEL_SLOW_NS - UI_WHEEL_FAST_NS);
}

// A turn of the wheel, in the 120ths of a notch the compositor sends: what is under a step is kept for the next turn.
// A notch is a row (a unit, where there are them), and its distance is added to the glide's speed as well as its aim, at the
// wheel's rate (ui_wheel_rate). With the wheel smooth off (ui_kit.wheel_off) a notch is one row at once: no glide, no rate.
// A notch that carries the content itself past an end of a bounded region (from where it is drawn, by its own distance) takes the
// region in hand (ui_scroll_hand), one for one, so the overscroll grows with the notches from the first pixel past the end and
// springs home on the release; nothing runs to the end first. Where the content is still short of the end the notch is a glide's as
// anywhere else, its aim held at the end (ui_scroll_fit): the aim of a spin runs up to twelve rows ahead of the content, and a hand
// taken when the AIM passed the end stopped the glide where the hand found it (three notches from 500 took the content to 540,
// two to 700) and read its release as a finger's (a fourth took it to the end).
static b32 ui_scroll_wheel(const struct ui address_to ui, struct ui_scroll address_to s, int delta)
{
        long total = (long)s->wheel + (long)delta * (s->unit > 0 ? 1 : UI_ROW * ui->scale / 100);
        int steps = (int)(total / 120);
        b64 base, aim, now, rate, reach, lim, pos, most, last, unit = s->unit > 0 ? s->unit : 1;

        s->wheel = (int)(total - (long)steps * 120);
        if (!steps)
                return false;
        now = ui_wheel_clock();
        rate = ui_kit.wheel_off ? 256 : ui_wheel_rate(s, now);
        last = s->notch;
        s->notch = now;
        most = ui_scroll_most256(s);
        pos = s->gliding || s->back.live || s->dragging ? s->at : (b64)s->scroll << 8;
        if (s->bounds && most && !ui_kit.wheel_off)
        {
                // The hand moves one notch's distance at the wheel's unit, with no acceleration: a notch past an end is a pull, not a spin.
                b64 move = -(b64)steps * unit * UI_SCROLL_SCALE;
                b32 held = s->hand && s->dragging && (s->at < 0 || s->at > most), past = held || pos + move < 0 || pos + move > most;

                // A burst bumps an end once. Its pull grows for UI_NOTCH_APEX_NS and the stretch goes home from its apex, whatever is still
                // coming (a Mac's trackpad, through the machine's tablet, sends a notch to every report of its finger and of the momentum
                // after it: a second of them held the pull at its largest and then fought the spring home in pieces); the notches that
                // push at the end after that, and within UI_NOTCH_BURST_NS of each other, are taken. A notch back, or a pause, ends it.
                if (s->absorb && (!past || now - last >= UI_NOTCH_BURST_NS))
                        s->absorb = 0;
                if (s->absorb)
                        return false;
                if (held && !s->precise && now - s->pulled_at >= UI_NOTCH_APEX_NS)
                {
                        s->hand = 0;
                        ui_scroll_let_go(s, 0, 0);
                        s->absorb = s->back.live;
                        return true;
                }
                if (past)
                {
                        s->precise = 0;
                        ui_scroll_hand(s, move, now);
                        ui_scroll_show(s);
                        return true;
                }
                if (s->hand)
                {
                        // The notches took the content back inside its ends: the hand lets go of it where it is, as a stop and not a throw,
                        // and this notch is the glide's.
                        s->hand = 0;
                        ui_scroll_let_go(s, 0, 0);
                        pos = s->gliding || s->back.live || s->dragging ? s->at : (b64)s->scroll << 8;
                }
        }
        base = s->gliding || s->back.live ? s->aim : s->dragging ? s->at : (b64)s->scroll << 8;
        aim = base - (b64)steps * unit * UI_SCROLL_SCALE * rate / 256;
        // The travel is capped where the region is drawn: a spin's aim is at most UI_WHEEL_REACH units past it.
        reach = (b64)UI_WHEEL_REACH * (s->unit > 0 ? s->unit : UI_ROW * ui->scale / 100) * UI_SCROLL_SCALE;
        aim = aim < pos - reach ? pos - reach : aim > pos + reach ? pos + reach : aim;
        aim = ui_scroll_fit(s, aim);
        if (ui_kit.wheel_off)
                return ui_scroll_set(s, (int)(aim >> 8));
        if (aim == base)
                return false;
        if (s->dragging)
                s->dragging = 0, s->speed = 0;
        ui_scroll_rest(s);
        // The notch's own speed is its distance over the time constant, the throw's rule, so a notch and a throw decay alike; a spin
        // is held to UI_WHEEL_RUN rows a second, so the glide does not run away however fast the notches come.
        s->speed += (aim - base) * 1000000000ll / UI_SCROLL_TAU_NS;
        lim = (b64)UI_WHEEL_RUN * (s->unit > 0 ? s->unit : UI_ROW * ui->scale / 100) * UI_SCROLL_SCALE;
        s->speed = s->speed > lim ? lim : s->speed < -lim ? -lim : s->speed;
        ui_scroll_glide_to(s, aim, UI_SCROLL_TAU_NS);
        return true;
}

// The wheel turns a region by its vertical notches, the one rule for every list and page: a horizontal notch (event->b) turns nothing.
// A precise input (WINDOW_WHEEL_PRECISE) is a trackpad's motion in 120ths of a notch, and the region takes it one for one: a notch
// is a row (UI_ROW at the window's scale), so 120ths of one are that over 120, in 256ths of a device pixel. Where a region takes
// whole units (a menu's choices) it is the notches' own.
static b32 ui_wheel_turn(const struct ui address_to ui, struct ui_scroll address_to s, const struct window_event address_to event)
{
        if (event->b)
                return false;
        if (!(event->flags & WINDOW_WHEEL_PRECISE) || s->unit > 0)
                return ui_scroll_wheel(ui, s, event->a);
        return ui_scroll_precise(ui, s, (int)((long)event->a * UI_ROW * ui->scale / 100 * UI_SCROLL_SCALE / 120));
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
        compositor_fallbacks(ui->engine);
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
        unsigned int scratch = width + 8u * (unsigned int)ui->scale / 100;
        long mapped;

        if (scratch <= (unsigned int)ui->scratch_width && (!ui->scratch_scale ? ui->scratch != null : ui->scale <= ui->scratch_scale))
                return;
        scratch = scratch > (unsigned int)ui->scratch_width ? scratch : (unsigned int)ui->scratch_width;
        mapped = ui_map((positive)scratch * UI_SCRATCH_ROWS * (positive)ui->scale / 100 * 4);
        if (mapped < 0 && mapped > -4096)
                return;
        if (ui->scratch && ui->scratch_scale)
                compositor_call(syscall(munmap), (positive)ui->scratch,
                                (positive)ui->scratch_width * UI_SCRATCH_ROWS * (positive)ui->scratch_scale / 100 * 4, 0, 0, 0, 0);
        ui->scratch = (unsigned int address_to)mapped;
        ui->scratch_width = (int)scratch;
        ui->scratch_scale = ui->scale;
}

/* ---- menus: one set of defaults, a popup surface and the list in it ---- */
/*
        A menu: ui_popmenu_add (an item), ui_popmenu_choosable, ui_popmenu_size and ui_popmenu_at (its size and place),
        ui_popmenu_event (its input), ui_popmenu_draw. A popup window: ui_popup_open and ui_popup_close, ui_popup_present,
        ui_popup_pump (its event loop) and ui_popup_scroll. The sizes are ui_menu_sizes, one set for every menu.
*/

/*
        Every menu of the kit is one of these, and these are its defaults, all in logical pixels (ui_menu_sizes gives them
        in device pixels, and every size of a menu is read from there): a row of 28, a finger's row of 44, the face's pad of
        6 at top and bottom, a label in from the left by 30, no narrower than 140, a margin of 8 from a window's edge, and a
        highlight's corner of 6.

        A menu is a popup row of its own (window.c's popup columns), so it reaches past its window's edges and the compositor
        places it, flipping it where the screen ends. It is glass, a temporary overlay: no face and no outline, its inks are
        the ground's. Its items are labels with an optional accelerator's text, a mnemonic marked with & (the letter typed
        chooses it), check and radio marks, separators, section headers and disabled items; a submenu item is answered
        UI_MENU_OPEN, for the app to open another popup against the item (WINDOW_POPUP_RIGHT).

        A menu chooses on a release, as a button does: a press on an item shows it pressed, and the release chooses it when it
        is over that same item. A press released anywhere else chooses nothing. The hot item (the hand's or the keys') is lit.
        A menu whose items do not all show has the kit's bar down its right, which a held thumb drags and a press on the
        track pages. A scroll (the wheel or a touchpad's) closes it, anywhere: the content it lay over has moved away. The keys while
        it is open: Up and Down (wrapping), Home, End, Page Up and Page Down move the hot item over what can be chosen; Enter and
        Space choose; Right answers a submenu's item and Left and Escape close; a letter chooses a mnemonic, else goes to the
        first item the letters typed so far start.
*/
#define UI_MENU_ITEMS 40
#define UI_WORD 64           // a word a page keeps (a choice's name, a row's)
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
#define UI_MENU_INSET 30
#define UI_MENU_LEAST 140
#define UI_MENU_MARGIN 8
#define UI_MENU_RADIUS 6
#define UI_MENU_DEFAULTS 1 // the kit has these menus' one set of defaults (test/checks.c's menu_defaults_check)
#define UI_MENU_RIPPLE 1   // an item's press ripple is drawn in its own full-width rect (checks.c's click check)

struct ui_menu_sizes
{
        int row, pad, inset, least, margin, radius; // device pixels
};

// Every menu's size, from the one place its numbers are: the mouse's row, or a finger's (touch).
static struct ui_menu_sizes ui_menu_sizes(const struct ui address_to ui, b32 touch)
{
        struct ui_menu_sizes sizes = {ui_dev(ui, touch ? UI_MENU_TOUCH_ROW : UI_MENU_ROW), ui_dev(ui, UI_MENU_PAD), ui_dev(ui, UI_MENU_INSET),
                                      ui_dev(ui, UI_MENU_LEAST), ui_dev(ui, UI_MENU_MARGIN), ui_dev(ui, UI_MENU_RADIUS)};

        return sizes;
}

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
        b32 touch;
        struct ui_press press;       // a press on an item: its target is the item it went down on, -1 none
        struct ui_ripple ripple;     // the press's ripple, drawn in the item's own rect (ripple_at), and whether it is moving
        int ripple_at;
        b32 moving;
        int width, height;
        struct ui_scroll scroll;     // the kit's bar: its track down the right, its scroll the top item's offset (in pixels)
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
        return ui_menu_sizes(ui, m->touch).row;
}

// The height of an item: a separator is a thin rule between two pads, every other item is a row.
static int ui_popmenu_tall(const struct ui address_to ui, const struct ui_popmenu address_to m, int at)
{
        return m->items[at].flags & UI_MENU_SEPARATOR ? (2 * UI_MENU_PAD + 1) * ui->scale / 100 : ui_popmenu_row(ui, m);
}

// Where item at starts, in device pixels from the top of the pad (the pad is not counted).
static int ui_popmenu_offset(const struct ui address_to ui, const struct ui_popmenu address_to m, int at)
{
        int y = 0;

        for (int k = 0; k < at && k < m->count; k++)
                y += ui_popmenu_tall(ui, m, k);
        return y;
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
        struct ui_menu_sizes sizes = ui_menu_sizes(ui, m->touch);
        int widest = 0, height = 2 * sizes.pad;

        for (int at = 0; at < m->count; at++)
        {
                p8 shown[96];
                p32 mnemonic;
                int wide;

                if (m->items[at].flags & UI_MENU_SEPARATOR)
                {
                        height += ui_popmenu_tall(ui, m, at);
                        continue;
                }
                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                wide = ui_kit_measure(ui, shown, UI_BODY_MEDIUM) + (m->items[at].keys ? ui_kit_measure(ui, (string_address)m->items[at].keys, UI_BODY_MEDIUM) + 32 * ui->scale / 100 : 0);
                widest = wide > widest ? wide : widest;
                height += ui_popmenu_row(ui, m);
        }
        m->width = widest + 2 * (sizes.inset + sizes.pad);
        m->width = m->width < sizes.least ? sizes.least : m->width;
        m->height = limit > 0 && height > limit ? limit : height;
        m->rows_shown = (m->height - 2 * sizes.pad) / sizes.row;
        m->scroll.content = height - 2 * sizes.pad;
        m->scroll.view = m->height - 2 * sizes.pad;
        m->scroll.x = ui_mirror(m->width - ui_dev(ui, UI_SCROLL_WIDTH), ui_dev(ui, UI_SCROLL_WIDTH), 0, m->width), m->scroll.y = sizes.pad, m->scroll.height = m->height - 2 * sizes.pad;
        m->scroll.unit = 0;
        m->scroll.scroll = ui_popmenu_offset(ui, m, m->top);
        m->scroll.held = m->scroll.hover = 0;
}

// The item at y (device pixels from the menu's top), or -1. A row that starts in the bottom padding is not drawn (the draw
// stops there), so the padding under the last row drawn is no item.
static int ui_popmenu_at(const struct ui address_to ui, const struct ui_popmenu address_to m, int y)
{
        int top = ui_menu_sizes(ui, m->touch).pad;

        for (int at = m->top; at < m->count; at++)
        {
                int tall = ui_popmenu_tall(ui, m, at);

                if (y >= top && y < top + tall)
                        return (m->items[at].flags & UI_MENU_SEPARATOR) || top >= m->height - ui_menu_sizes(ui, m->touch).pad ? -1 : at;
                top += tall;
        }
        return -1;
}

// The first item the bar's scroll puts at the top: the last whose top is at or above it.
static fn ui_popmenu_sync(const struct ui address_to ui, struct ui_popmenu address_to m)
{
        m->top = 0;
        for (int at = 1; at < m->count && ui_popmenu_offset(ui, m, at) <= m->scroll.scroll; at++)
                m->top = at;
}

/*
        An item's rect: the whole width of the menu, its row, square. It is the one element an item has: its hover (the hot item,
        a press's too) is that rect in the selection's layer, and its press ripple is drawn inside it.
*/
static void ui_popmenu_rect(const struct ui address_to ui, const struct ui_popmenu address_to m, int y, struct ui_box address_to rect)
{
        int row = ui_popmenu_row(ui, m);

        rect->x0 = 0, rect->y0 = y * 256, rect->x1 = m->width * 256, rect->y1 = (y + row) * 256;
        rect->rx = rect->ry = 0;
}

// The press's ripple inside the item's rect (its clock is the kit's): the kit's ripple, drawn as the controls' are, cut to the rect.
static fn ui_popmenu_ripple(struct ui address_to ui, struct ui_popmenu address_to m, int y)
{
        struct ui_box rect, dot;
        b64 cx, cy, radius, opacity;

        ui_popmenu_rect(ui, m, y, &rect);
        if (!ui_ripple_at(&m->ripple, ui_kit.now, &cx, &cy, &radius, &opacity))
                return;
        m->moving = 1;
        dot.x0 = (b32)(((cx - radius) * ui->scale / 100) >> 22), dot.x1 = (b32)(((cx + radius) * ui->scale / 100) >> 22);
        dot.y0 = y * 256 + (b32)(((cy - radius) * ui->scale / 100) >> 22), dot.y1 = y * 256 + (b32)(((cy + radius) * ui->scale / 100) >> 22);
        dot.rx = dot.ry = (b32)((radius * ui->scale / 100) >> 22);
        // The press sinks the item toward the ground it lies on (the glass's, or the surface's): the label is paired with the
        // highlight's ink, so a press that brightened the highlight would take the label's contrast below UI_READABLE.
        ui_kit_fill(ui, &dot, ui_mix(ui->glass ? 0xff000000u | (ui->ground & 0xffffffu) : ui_role(UI_SURFACE),
                                     (unsigned int)((opacity * 100 + UI_HALF) >> UI_Q), 0), &rect);
}

static fn ui_popmenu_draw(struct ui address_to ui, struct ui_popmenu address_to m)
{
        struct ui_menu_sizes sizes = ui_menu_sizes(ui, m->touch);
        int y = sizes.pad;

        m->scroll.scroll = ui_popmenu_offset(ui, m, m->top);
        m->moving = 0;
        for (int at = m->top; at < m->count && y < m->height - sizes.pad; at++)
        {
                const struct ui_menu_item address_to item = &m->items[at];
                unsigned int ink = ui_text_colour(ui, UI_ON_SURFACE);
                p8 shown[96];
                p32 mnemonic;
                int baseline = y + (sizes.row - ui_capital(ui, 14)) / 2 + ui_capital(ui, 14);
                unsigned int alpha = item->flags & UI_MENU_DISABLED ? 97 : 255;

                if (item->flags & UI_MENU_SEPARATOR)
                {
                        ui_fill(ui, sizes.pad, y + sizes.pad, m->width - 2 * sizes.pad, ui_px(ui, 1),
                                ui_tone(ui->ink & 0xffffffu, 40));
                        y += ui_popmenu_tall(ui, m, at);
                        continue;
                }
                // The hot item's rect: ui_fill lays a premultiplied tone, and the well is straight (its alpha is its cover), so it is
                // premultiplied first. The press's ripple is laid over it, cut to the rect, and sinks toward the ground (ui_popmenu_ripple).
                if (ui_popmenu_choosable(m, at) && at == m->hot)
                        ui_fill(ui, 0, y, m->width, ui_popmenu_row(ui, m), ui_premultiplied(ui_well(ui, UI_SELECTION, 28), UI_FULL));
                if (at == m->ripple_at)
                        ui_popmenu_ripple(ui, m, y);
                ui_popmenu_shown(item->label, shown, sizeof(shown), &mnemonic);
                if (item->flags & UI_MENU_HEADER)
                        ui_kit_text_flipped(ui, (UI_MENU_PAD + 12) * ui->scale / 100, baseline, shown, UI_LABEL_MEDIUM, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0, 0, m->width);
                else
                        ui_kit_text_flipped(ui, sizes.pad + sizes.inset, baseline, shown, UI_BODY_MEDIUM, (alpha << 24) | (ink & 0xffffffu), 0, 0, m->width);
                if ((item->flags & (UI_MENU_CHECK | UI_MENU_RADIO)) && (item->flags & UI_MENU_ON))
                {
                        int cx = (UI_MENU_PAD + 15) * ui->scale / 100, cy = y + sizes.row / 2;

                        if (item->flags & UI_MENU_RADIO)
                                ui_round(ui, ui_mirror(cx - 3 * ui->scale / 100, 6 * ui->scale / 100, 0, m->width), cy - 3 * ui->scale / 100, 6 * ui->scale / 100, 6 * ui->scale / 100, 3 * ui->scale / 100, ui_tone(ink & 0xffffffu, alpha));
                        else
                        {
                                int tick = ui_kit_measure(ui, (string_address) "\xe2\x9c\x93", UI_BODY_MEDIUM), tick_x = ui_mirror(cx - 5 * ui->scale / 100, tick, 0, m->width);

                                UI_KEEP(tick_x, baseline - ui_capital(ui, 14), tick, ui_capital(ui, 14));
                                ui_kit_text(ui, tick_x, baseline, (string_address) "\xe2\x9c\x93", UI_BODY_MEDIUM, (alpha << 24) | (ink & 0xffffffu), 0);
                        }
                }
                if (item->keys)
                        ui_kit_text_flipped(ui, m->width - (UI_MENU_PAD + 14) * ui->scale / 100 - ui_kit_measure(ui, (string_address)item->keys, UI_BODY_MEDIUM), baseline,
                                            (string_address)item->keys, UI_BODY_MEDIUM,
                                            (alpha << 24) | ((ui->glass && at == m->hot ? ink : ui_text_colour(ui, UI_ON_SURFACE_VARIANT)) & 0xffffffu), 0, 0, m->width);
                if (item->flags & UI_MENU_SUBMENU)
                        ui_kit_text_flipped(ui, m->width - (UI_MENU_PAD + 14) * ui->scale / 100, baseline, (string_address) "\xe2\x80\xba", UI_BODY_MEDIUM,
                                            (alpha << 24) | (ink & 0xffffffu), 0, 0, m->width);
                y += sizes.row;
        }
        if (m->scroll.content > m->scroll.view)
                ui_scroll_draw(ui, &m->scroll);
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

// A page of the keys: the hot item moves by the rows that show, and stops at the first or last item (it does not wrap), on
// the nearest that can be chosen, and the view follows it as a step's does.
static fn ui_popmenu_page(struct ui_popmenu address_to m, int step)
{
        int by = m->rows_shown > 0 ? m->rows_shown : 1, at = m->hot < 0 ? (step > 0 ? 0 : m->count - 1) : m->hot;

        at += step * by;
        at = at < 0 ? 0 : at >= m->count ? m->count - 1 : at;
        while (at > 0 && at < m->count - 1 && !ui_popmenu_choosable(m, at))
                at -= step > 0 ? 1 : -1;
        if (ui_popmenu_choosable(m, at))
                m->hot = at;
        if (m->hot < m->top)
                m->top = m->hot;
        if (m->rows_shown > 0 && m->hot >= m->top + m->rows_shown)
                m->top = m->hot - m->rows_shown + 1;
}

/*
        The menu's bar, when it has one: a motion, a press or a release on it is its (a held thumb follows the hand, a press on
        the track pages it). True when it took the event, and the menu's top follows its scroll.
*/
static b32 ui_popmenu_bar(const struct ui address_to ui, struct ui_popmenu address_to m, int type, int x, int y, b32 down)
{
        int scroll;

        if (m->scroll.content <= m->scroll.view && !m->scroll.held)
                return false;
        scroll = ui_scroll_pointer(ui, &m->scroll, type, x, y, down);
        if (scroll < 0)
                return false;
        if (!m->scroll.dragging)
                ui_scroll_set(&m->scroll, scroll);
        ui_popmenu_sync(ui, m);
        return true;
}

/*
        An event of the menu's row: the id of the item chosen, UI_MENU_NOTHING, UI_MENU_GONE (Escape, Left, a scroll, the compositor
        asking it to go), or UI_MENU_OPEN for a submenu's item (its id in hot's item). x and y are the menu's own device pixels.
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
                case 104: ui_popmenu_page(m, -1); return UI_MENU_NOTHING;
                case 109: ui_popmenu_page(m, 1); return UI_MENU_NOTHING;
                case 1: return UI_MENU_GONE;
                case 105: case 106:
                        // The arrow toward the start of what is read closes the menu, the one toward its end opens a submenu (the mark is there).
                        if (ui_arrow((int)event->a) < 0)
                                return UI_MENU_GONE;
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

                // A finger's press that moves past the slop is a drag, and its lift chooses nothing (the kit's press rule).
                if (m->press.down && (event->flags & WINDOW_KEY_TOUCH))
                        ui_press_move(ui, &m->press, x, y);
                if (!ui_popmenu_bar(ui, m, WINDOW_EVENT_MOTION, x, y, false))
                        m->hot = at;
                return UI_MENU_NOTHING;
        }
        // A scroll (the wheel's or a touchpad's) over the menu closes it, as a scroll anywhere does (ui_popup_scroll).
        if (event->type == WINDOW_EVENT_WHEEL)
                return UI_MENU_GONE;
        if (event->type == WINDOW_EVENT_BUTTON && event->a >= 0x110 && event->a <= 0x112)
        {
                int at = x >= 0 && x < m->width ? ui_popmenu_at(ui, m, y) : -1, clicked;

                m->touch |= (event->flags & WINDOW_KEY_TOUCH) != 0;
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        if (ui_popmenu_bar(ui, m, WINDOW_EVENT_BUTTON, x, y, true))
                                return UI_MENU_NOTHING;
                        ui_press_begin(&m->press, x, y, at, m->touch);
                        m->hot = at;
                        if (ui_popmenu_choosable(m, at))
                        {
                                int top = ui_menu_sizes(ui, m->touch).pad + ui_popmenu_offset(ui, m, at);

                                ui_ripple_go(&m->ripple, ui_qdiv(ui_q(x), ui_q(ui->scale) / 100), ui_qdiv(ui_q(y - top), ui_q(ui->scale) / 100),
                                             ui_qdiv(ui_q(m->width), ui_q(ui->scale) / 100), ui_qdiv(ui_q(ui_popmenu_row(ui, m)), ui_q(ui->scale) / 100), 0);
                                m->ripple_at = at;
                        }
                        return UI_MENU_NOTHING;
                }
                if (ui_popmenu_bar(ui, m, WINDOW_EVENT_BUTTON, x, y, false))
                        return UI_MENU_NOTHING;
                // The release chooses an item only where its press went down (the kit's press: a cancel, or no press, is none).
                clicked = ui_press_end(&m->press, (event->flags & WINDOW_KEY_CANCEL) != 0);
                ui_ripple_exit(&m->ripple, ui_kit.now);
                if (clicked < 0 || at != clicked || !ui_popmenu_choosable(m, at))
                        return UI_MENU_NOTHING;
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
        b32 open, pending;
        int shown, x, y;
        struct ui_text address_to field; // the text widget its menu was opened for (ui_text_menu_open), or none
};

/*
        A picture of the menu, into the buffer the compositor is not reading. While the compositor has not taken the last one
        there is no buffer free: the picture is kept as pending, and ui_popup_pump presents it once the compositor's knock says
        it has taken it. A change made in between (a hot item moved by a key) is drawn then, not lost.
*/
static fn ui_popup_present(struct ui_popup address_to p)
{
        struct windows address_to table = p->own.table;
        unsigned int row = p->own.row;
        int slot = p->shown < 0 ? 0 : 1 - p->shown;

        if (p->shown >= 0 && !windows_ready(&p->own))
        {
                p->pending = true;
                return;
        }
        p->pending = false;
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
        struct ui own = p->ui;

        p->ui = *parent_ui;
        // The menu's own scratch line, mapped once and kept (the parent's is not the menu's to share).
        p->ui.scratch = own.scratch, p->ui.scratch_width = own.scratch_width, p->ui.scratch_scale = own.scratch_scale;
        // A parent whose faces are not the kit's (the terminal's own) gets the kit's regular face here, loaded once.
        if (!p->ui.regular)
        {
                static font_face face;
                static int state;

                if (!state)
                        state = ui_face_load(&face, (string_address)UI_FONT_REGULAR) ? 1 : -1;
                if (state > 0)
                        p->ui.regular = p->ui.bold = &face;
        }
        ui_scratch_fit(&p->ui, 640u * (unsigned int)p->ui.scale / 100);
        p->ui.opaque = false, p->ui.glass = true;
        ui_inks(&p->ui, ground ? ground : parent_ui->ground ? parent_ui->ground : UI_GLASS_GROUND);
        table->glass[p->own.row] = UI_POPUP_GLASS;
}

/*
        The menu on its row, which ui_popup_open has taken: its look, its place and its first picture. Split from the take so a
        check can put a menu on a row of its own table (checks.c's popup reopen).
*/
static b32 ui_popup_shows(struct ui_popup address_to p, const struct window_row address_to parent, const struct ui address_to parent_ui,
                          int x, int y, int width, int height, unsigned int flags, b32 touch)
{
        struct windows address_to table = p->own.table;
        int screen_x, screen_y, screen_w, screen_h;

        // A popup opens in the theme
        // A popup opens in the theme as it is now. One changed while its window slept is taken here (the window's
        // own loop still turns to it after, as ever), and a turn under way is taken to its end: the terminal's menu
        // opened in the old theme, its loop reading the new one only after the press that opened it.
        if (__atomic_load_n(&table->themed, __ATOMIC_ACQUIRE) != ui_kit.themed)
        {
                ui_kit.wheel_off = table->wheel_off ? 1 : 0, ui_kit_caret_take(table), ui_kit_language_take(table), ui_kit_text_take(table);
                ui_kit_theme(table->theme, table->accent, table->motion);
        }
        ui_kit.shown = ui_kit.to;
        ui_popup_look(p, table, parent_ui);
        p->shown = -1;
        p->pending = false;
        p->menu.touch = touch;
        p->menu.hot = -1;
        memory_zero(&p->menu.press, sizeof(p->menu.press));
        p->menu.ripple.live = 0, p->menu.ripple_at = -1, p->menu.moving = 0;
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

static b32 ui_popup_open(struct ui_popup address_to p, const struct window_row address_to parent, const struct ui address_to parent_ui,
                         int x, int y, int width, int height, unsigned int flags, b32 touch)
{
        if (p->open)
                windows_leave(&p->own);
        p->open = false;
        if (windows_take(&p->own) < 0)
                return false;
        return ui_popup_shows(p, parent, parent_ui, x, y, width, height, flags, touch);
}

static fn ui_popup_close(struct ui_popup address_to p)
{
        if (p->open)
                windows_leave(&p->own);
        p->open = false;
}

/*
        The kit's default for a scroll: a wheel or a touchpad's scroll that the app's window receives closes its open popup, as
        Escape does, and the answer is UI_MENU_GONE for the app (the app's loop calls this on each event it reads, once). A menu
        that stayed open would float over content the scroll has moved away from.
*/
static int ui_popup_scroll(struct ui_popup address_to p, const struct window_event address_to event)
{
        if (!p->open || event->type != WINDOW_EVENT_WHEEL)
                return UI_MENU_NOTHING;
        ui_popup_close(p);
        return UI_MENU_GONE;
}

// The popup's events: the id chosen (the popup closed), UI_MENU_GONE when it went, UI_MENU_NOTHING otherwise.
static int ui_popup_pump(struct ui_popup address_to p)
{
        struct window_event event;
        int answer = UI_MENU_NOTHING;

        if (!p->open)
                return UI_MENU_NOTHING;
        // The menu runs on the kit's clock as it is now: its ripples are timed from the press and drawn at the frame, and over an
        // idle window the last frame's clock is old, so a ripple would not age and the menu would stay busy with it.
        ui_kit.now = (b64)clock_monotonic_nanoseconds();
        // A picture the compositor was not ready for goes now, when it has taken the one before (its knock wakes the app).
        if (p->pending && windows_ready(&p->own))
                ui_popup_present(p);
        // The compositor's ground for the menu as it came: its inks follow, and the menu is drawn again in them.
        if ((p->own.table->ground[p->own.row] & 0xffffffu) && (p->own.table->ground[p->own.row] & 0xffffffu) != p->ui.ground)
        {
                ui_inks(&p->ui, p->own.table->ground[p->own.row]);
                ui_popup_present(p);
        }
        while (windows_event(&p->own, &event) && answer == UI_MENU_NOTHING)
        {
                ui_event_context(&event);
                // What the picture shows: the hot item, the top one (a wheel or the bar moves it) and the bar's own lit state.
                int hot = p->menu.hot, top = p->menu.top, scroll = p->menu.scroll.scroll;
                b32 lit = p->menu.scroll.hover, held = p->menu.scroll.held;

                if (event.type == WINDOW_EVENT_MOTION)
                        p->x = event.a, p->y = event.b;
                answer = ui_popmenu_event(&p->ui, &p->menu, &event, p->x, p->y);
                if (answer == UI_MENU_OPEN)
                        answer = UI_MENU_NOTHING;
                if (p->menu.hot != hot || p->menu.top != top || p->menu.scroll.scroll != scroll || p->menu.scroll.hover != lit ||
                    p->menu.scroll.held != held)
                        ui_popup_present(p);
        }
        if (p->menu.moving && answer == UI_MENU_NOTHING)
                ui_popup_present(p);
        if (p->own.table->state[p->own.row] & WINDOW_CLOSING)
                answer = UI_MENU_GONE;
        if (answer != UI_MENU_NOTHING)
                ui_popup_close(p);
        return answer;
}

/* ---- the kit's composites: what a window is made of ---- */
/*
        The composites: ui_buttons_set, ui_buttons_event and ui_buttons_draw (a row of buttons, which lays itself out and takes
        its own pointer and keys), ui_cut_by and ui_cut_write (text cut to a width), ui_fit, ui_signal (the signal bars), ui_chevron.
        An app gives the labels and the actions.
*/

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
#define UI_SEARCH_Y 8        // centred in the title band (UI_PAGE_TOP): the field is UI_FIELD_H (44) high, so it lies wholly in the band
#define UI_SEARCH_RIGHT 46   // and ends this far from the window's right edge: clear of the close button (14 px, 5 in) and the minimise button beside it, with a gap of 8
#define UI_COLUMN_SHARE 30   // a page's column: this share of the window,
#define UI_COLUMN_LEAST 88   // no narrower than this, no wider than the most, and never more than half of the window
#define UI_COLUMN_MOST 170
#define UI_COLUMN_GUTTER 14  // between the column and the body
#define UI_COLUMN_PAGE 3     // Page Up and Page Down in a column move three groups
#define UI_SECTION 34        // a section's header over its first row
#define UI_SECTION_GAP 10    // under its last row
#define UI_ROW_FIELD_W 220   // a field in a row: at most this wide, and at least the least
#define UI_ROW_FIELD_LEAST 100
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
#define UI_KEY_BACKSPACE 14
#define UI_KEY_TAB 15
#define UI_KEY_P 25
#define UI_KEY_N 49
#define UI_KEY_LEFT 105
#define UI_KEY_RIGHT 106
#define UI_KEY_DELETE 111

// Whether a key is a modifier alone (Control, Shift, Alt, AltGr, Super, Caps Lock), which no widget takes as a key of its own.
static b32 ui_key_modifier(int code)
{
        return code == 29 || code == 97 || code == 42 || code == 54 || code == 56 || code == 100 || code == 125 || code == 126 || code == 58;
}

/*
        A row of buttons that are one thing: the actions at the foot of a window (a launcher's Power off and Restart), the kit's
        outlined buttons at the right of a row of UI_ROW, the first leftmost. The kit does all of it, so an app says only what
        each is, hands the row its events and acts on the one that fired:

            ui_buttons_set     button n: its label, and what it says when armed (none: it acts at once)
            ui_buttons_place   the row, in device pixels: its left, top and width
            ui_buttons_event   a key or a pointer event: the UI_BUTTONS_ bits of what the row made of it
            ui_buttons_draw    the buttons, and at the left of the row what an armed one will do

        An action that is not undone says what it does and takes the kit's press again to confirm (ui_confirm): the first press
        arms it (it fills with the accent, and the row says "press again to confirm: ..."), and the next, with no other press or
        key between, acts. A key held down is one press: its repeats are the row's and confirm nothing. The keys are one focus
        among the buttons: Shift-Tab gives them to the row, at its first button; Left and Right move along it and stop at its
        ends; Tab and Shift-Tab move along it and past either end give them back, as Up does; Enter and Space press the focused
        button. Any other key is the app's and gives the keys back (Escape too), and a modifier alone is nobody's. A press on a
        button focuses it; a press anywhere else drops what was armed. The pointer is ui_kit_pointer's, with its hover, its press
        and its ripple.
*/
#define UI_BUTTONS_MOST 4
#define UI_BUTTONS_W 104 // logical pixels: a button, its label inside it
#define UI_BUTTONS_H 32
#define UI_BUTTONS_DRAW 1u  // the row's picture is due again
#define UI_BUTTONS_TAKEN 2u // the event was the row's, and the app does not have it
#define UI_BUTTONS_FIRED 4u // a button acted: fired is its number

struct ui_buttons
{
        struct ui_control button[UI_BUTTONS_MOST];
        struct ui_control line;                      // what an armed button says, at the row's left
        const char address_to says[UI_BUTTONS_MOST]; // what each does, said when armed; none: it acts at once
        int count, keys, fired;                      // the buttons, the one the keys are on and 1 (0: the app's), the one that acted
        struct ui_confirm confirm;
        char said[UI_CONFIRM_TEXT];                  // the line as it is drawn
};

static fn ui_buttons_set(struct ui_buttons address_to b, int n, const char address_to label, const char address_to says)
{
        b->button[n].kind = UI_BUTTON, b->button[n].label = label;
        b->says[n] = says;
        b->count = b->count > n ? b->count : n + 1;
}

static fn ui_buttons_place(struct ui address_to ui, struct ui_buttons address_to b, int left, int top, int width)
{
        int right = (left + width) * 100 / ui->scale, row = top * 100 / ui->scale;

        for (int k = 0; k < b->count; k++)
        {
                struct ui_control address_to c = &b->button[k];

                c->variant = b->confirm.armed && b->confirm.what == k + 1 ? UI_BUTTON_PRIMARY : UI_BUTTON_OUTLINED;
                c->w = UI_BUTTONS_W, c->h = UI_BUTTONS_H;
                c->x = right - (b->count - k) * UI_BUTTONS_W - (b->count - 1 - k) * UI_GAP2;
                c->y = row + (UI_ROW - UI_BUTTONS_H) / 2;
        }
        b->line.kind = UI_HEADING, b->line.variant = UI_BODY_MEDIUM, b->line.role = UI_ON_SURFACE_VARIANT;
        b->line.x = left * 100 / ui->scale, b->line.y = row + (UI_ROW - ui_types[UI_BODY_MEDIUM].line) / 2;
        b->line.w = b->button[0].x - UI_GAP2 - b->line.x;
        // Reading from the right the row is the same row turned over its width: the buttons at its left, the first of them at its right end of them.
        if (ui_rtl())
        {
                int across = left * 100 / ui->scale, span = (left + width) * 100 / ui->scale - across;

                for (int k = 0; k < b->count; k++)
                        b->button[k].x = ui_mirror(b->button[k].x, UI_BUTTONS_W, across, span);
                b->line.x = ui_mirror(b->line.x, b->line.w, across, span);
        }
}

// keys: the window has the keys, so the focused button shows its ring.
static fn ui_buttons_draw(struct ui address_to ui, struct ui_buttons address_to b, b32 keys)
{
        if (b->confirm.armed && b->confirm.what >= 1 && b->confirm.what <= b->count && b->says[b->confirm.what - 1])
        {
                ui_confirm_line(b->said, sizeof(b->said), b->says[b->confirm.what - 1]);
                b->line.label = b->said;
                ui_control_draw(ui, &b->line);
        }
        for (int k = 0; k < b->count; k++)
        {
                b->button[k].focused = keys && b->keys == k + 1;
                ui_control_draw(ui, &b->button[k]);
        }
}

// A press of button which: it is armed when it says what it does and was not armed, else it acts.
static p32 ui_buttons_press(struct ui_buttons address_to b, int which)
{
        b->keys = which + 1;
        if (b->says[which] && !ui_confirm_ask(&b->confirm, which + 1, b->says[which]))
                return UI_BUTTONS_DRAW | UI_BUTTONS_TAKEN;
        b->fired = which;
        return UI_BUTTONS_DRAW | UI_BUTTONS_TAKEN | UI_BUTTONS_FIRED;
}

static p32 ui_buttons_key(struct ui_buttons address_to b, const struct window_event address_to event)
{
        b32 repeat = (event->flags & WINDOW_KEY_REPEAT) != 0, back = (event->flags & WINDOW_KEY_SHIFT) != 0;
        int at = b->keys - 1, key = event->a;
        p32 drawn = b->keys || b->confirm.armed ? UI_BUTTONS_DRAW : 0;

        if (!(event->flags & WINDOW_KEY_DOWN) || ui_key_modifier(key) || !b->count)
                return 0;
        if (!b->keys)
        {
                // Shift-Tab gives the row the keys; any other press of a key drops what was armed, and a repeat is nothing.
                if (key == UI_KEY_TAB && back && !repeat)
                {
                        ui_confirm_drop(&b->confirm);
                        b->keys = 1;
                        return UI_BUTTONS_DRAW | UI_BUTTONS_TAKEN;
                }
                if (repeat)
                        return 0;
                ui_confirm_drop(&b->confirm);
                return drawn;
        }
        if (key == UI_KEY_ENTER || key == UI_KEY_ENTER_PAD || key == UI_KEY_SPACE)
                return repeat ? UI_BUTTONS_TAKEN : ui_buttons_press(b, at);
        ui_confirm_drop(&b->confirm);
        if (key == UI_KEY_LEFT || key == UI_KEY_RIGHT)
        {
                at += ui_arrow(key);
                b->keys = (at < 0 ? 0 : at >= b->count ? b->count - 1 : at) + 1;
        }
        else if (key == UI_KEY_TAB)
        {
                at += back ? -1 : 1;
                b->keys = at >= 0 && at < b->count ? at + 1 : 0;
        }
        else if (key == UI_KEY_UP)
                b->keys = 0;
        else if (key != UI_KEY_DOWN)
        {
                b->keys = 0; // another key is the app's, and the row lets go of the keys
                return UI_BUTTONS_DRAW;
        }
        return UI_BUTTONS_DRAW | UI_BUTTONS_TAKEN;
}

// x, y: where the pointer is, for a button's press, which carries no place of its own.
static p32 ui_buttons_pointer(struct ui address_to ui, struct ui_buttons address_to b, const struct window_event address_to event, int x, int y)
{
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0, held = ui_kit_held >= b->button && ui_kit_held < b->button + b->count;
        int was = 0, now = 0, hit = -1;
        struct ui_control address_to clicked;
        p32 asks = 0;

        if (event->type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT)
        {
                // Another button of the mouse is a press that is not a button's: it drops what was armed.
                if (!down)
                        return 0;
                asks = b->keys || b->confirm.armed ? UI_BUTTONS_DRAW : 0;
                b->keys = 0;
                ui_confirm_drop(&b->confirm);
                return asks;
        }
        if (event->type == WINDOW_EVENT_MOTION)
                x = event->a, y = event->b;
        for (int k = 0; k < b->count; k++)
                was |= (b->button[k].hover | b->button[k].down << 1) << (2 * k);
        clicked = ui_kit_pointer(ui, b->button, b->count, (int)event->type, x, y, down, event->flags);
        for (int k = 0; k < b->count; k++)
        {
                now |= (b->button[k].hover | b->button[k].down << 1) << (2 * k);
                hit = b->button[k].down ? k : hit;
        }
        asks = was != now ? UI_BUTTONS_DRAW : 0;
        if (event->type != WINDOW_EVENT_BUTTON)
                return asks;
        if (down)
        {
                if (hit >= 0)
                {
                        b->keys = hit + 1;
                        return asks | UI_BUTTONS_DRAW | UI_BUTTONS_TAKEN;
                }
                asks |= b->keys || b->confirm.armed ? UI_BUTTONS_DRAW : 0;
                b->keys = 0;
                ui_confirm_drop(&b->confirm);
                return asks;
        }
        if (clicked)
                return asks | ui_buttons_press(b, (int)(clicked - b->button));
        return asks | (held ? UI_BUTTONS_DRAW | UI_BUTTONS_TAKEN : 0);
}

static p32 ui_buttons_event(struct ui address_to ui, struct ui_buttons address_to b, const struct window_event address_to event, int x, int y)
{
        if (event->type == WINDOW_EVENT_KEY)
                return ui_buttons_key(b, event);
        if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
                return ui_buttons_pointer(ui, b, event, x, y);
        return 0;
}

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
        unsigned int percent; // the text's percent when it was cut: the same words are another width at another size
        unsigned char kind; // 1 whole, 2 the first cut bytes and the dots, 3 the first cut bytes alone
        unsigned char rtl;  // the window read from the right when it was cut: the same words are another width there
};

static struct ui_cut address_to ui_cuts;

/*
        The one cut of a text to room, for every label that is fitted (a kit role's, or a band's own face's): width measures a
        text (a NUL-terminated string) with whatever it is asked with, by. Answers the kind: 1 whole, the text fits; 2 the
        longest start that ends before a whole character and fits with three dots after it, found by halving (a start grows
        with its length); 3 its first character alone, whole, with no dots. *cut is the bytes of text kept, and fitted (room
        for capacity bytes) is the scratch the candidates are written in.
*/
typedef int (*ui_cut_width)(address_any by, string_address text);

static unsigned char ui_cut_by(ui_cut_width width, address_any by, string_address text, positive length, int room, char address_to fitted,
                               positive capacity, positive address_to cut)
{
        positive n = length < capacity ? length : capacity - 1, low = 1, high, best = 0, first = 1;

        if (width(by, text) <= room)
                return *cut = length, 1;
        high = n > 1 ? n - 1 : 0;
        while (low <= high)
        {
                positive mid = (low + high) / 2, at = mid;

                while (at > 1 && (((unsigned char)text[at]) & 0xc0) == 0x80)
                        at--;
                memory_copy(fitted, text, at);
                fitted[at] = 0;
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
                if (width(by, (string_address)fitted) <= room)
                        best = at, low = mid + 1;
                else
                        high = mid - 1;
        }
        if (best)
                return *cut = best, 2;
        while (first < n && (((unsigned char)text[first]) & 0xc0) == 0x80)
                first++;
        return *cut = first < n ? first : n, 3;
}

// The text a cut of it is, written to fitted: its kept bytes, and the three dots after them for kind 2. Answers its length.
static positive ui_cut_write(char address_to fitted, string_address text, positive length, positive cut, unsigned char kind, positive capacity)
{
        positive keep = kind == 1 ? length : cut;

        keep = keep < capacity ? keep : capacity - 1;
        memory_copy(fitted, text, keep);
        fitted[keep] = 0;
        if (kind == 2)
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
        return string_length((string_address)fitted);
}

// The kit's width of a role's text, for a cut of it.
struct ui_cut_role
{
        struct ui address_to ui;
        int type;
};

static int ui_cut_kit(address_any by, string_address text)
{
        const struct ui_cut_role address_to role = by;

        return ui_kit_measure(role->ui, text, role->type);
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
                hash = (hash ^ (unsigned int)ui_rtl()) * 16777619u;
                hash = (hash ^ ui_text_percent) * 16777619u;
                memo = &ui_cuts[(hash ^ hash >> 15) & (UI_CUTS - 1)];
                if (memo->kind && memo->length == length && memo->type == type && memo->room == room && memo->scale == ui->scale &&
                    memo->percent == ui_text_percent && memo->rtl == (unsigned char)ui_rtl() && memo->capacity == capacity &&
                    !memory_compare(memo->text, text, length))
                        kind = memo->kind, cut = memo->cut;
        }
        if (!kind)
        {
                struct ui_cut_role role = {ui, type};

                kind = ui_cut_by(ui_cut_kit, &role, text, length, room, fitted, capacity, &cut);
                if (memo)
                {
                        memory_copy(memo->text, text, length);
                        memo->length = length, memo->type = type, memo->room = room, memo->scale = ui->scale, memo->percent = ui_text_percent, memo->rtl = (unsigned char)ui_rtl();
                        memo->capacity = capacity, memo->cut = cut, memo->kind = kind;
                }
        }
        if (kind == 1)
                return text;
        ui_cut_write(fitted, text, length, cut, kind, capacity);
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
static int ui_signal_width(const struct ui address_to ui)
{
        return 4 * ui_dev(ui, 3) + 3 * ui_dev(ui, 2);
}

static fn ui_signal(struct ui address_to ui, int right, int middle, int bars)
{
        int width = ui_dev(ui, 3), gap = ui_dev(ui, 2), bottom = middle + ui_dev(ui, 7);

        UI_KEEP(right - ui_signal_width(ui), bottom - ui_dev(ui, 13), ui_signal_width(ui), ui_dev(ui, 13));
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
        b32 thick = ui_stroke(ui) + 128;

        for (int step = 0; step <= size; step++)
        {
                b32 y = (cy - size / 2 + step) * 256;

                ui_kit_fill(ui, &(struct ui_box){(cx - size + step) * 256, y, (cx - size + step) * 256 + thick, y + ui_stroke(ui), 0, 0}, argb, null);
                ui_kit_fill(ui, &(struct ui_box){(cx + size - step + 1) * 256 - thick, y, (cx + size - step + 1) * 256, y + ui_stroke(ui), 0, 0}, argb, null);
        }
}

/* ---- a speaker, a level meter and strips of both: what a mixer is made of ---- */
/*
        A speaker, a level meter and the strips of both (the mixer's composite): ui_speaker, ui_level_* (the meter's feed and
        motion), ui_strips_begin, ui_strips_add and ui_strips_end (a strip is data: a name and a level), ui_strips_event and
        ui_strips_draw. The strips own their scroll and their pointer; the app adds strips.
*/

/*
        A speaker, size to a side with its left edge at left and its top at top (device pixels), in one colour: a body and a
        cone that opens toward the right, then waves (0 to 2 arcs of the circle round the cone's mouth) or, when struck, a cross
        where they would be. It is drawn on a grid of 24 in whole rows, each row's edges exact, and faces the way the window
        reads: a window that reads from the right has it turned over.
*/
static fn ui_speaker_span(struct ui address_to ui, int left, int size, int row, f64 from, f64 to, unsigned int argb)
{
        f64 grid = (f64)size * 256.0 / 24.0;
        b32 x0 = (b32)((f64)left * 256.0 + from * grid), x1 = (b32)((f64)left * 256.0 + to * grid), y = (b32)(row * 256);

        if (ui_rtl())
        {
                b32 sum = left * 512 + size * 256, turned = sum - x1;

                x1 = sum - x0, x0 = turned;
        }
        ui_kit_fill(ui, &(struct ui_box){x0, y, x1, y + 256, 0, 0}, argb, null);
}

static fn ui_speaker(struct ui address_to ui, int left, int top, int size, int waves, b32 struck, unsigned int argb)
{
        f64 high = 24.0 / (f64)size; // a row's height on the grid

        for (int row = 0; row < size; row++)
        {
                f64 up = ((f64)row) * high - 12.0, down = up + high, middle = (up + down) / 2.0;
                f64 near = middle < 0.0 ? -middle : middle;

                // The body and the cone: x 2 to 7 over the middle six rows, and from there the cone's edge to x 13 at a half-height of 8.
                if (near <= 3.0)
                        ui_speaker_span(ui, left, size, top + row, 2.0, 13.0, argb);
                else if (near <= 8.0)
                        ui_speaker_span(ui, left, size, top + row, 7.0 + (near - 3.0) * 1.2, 13.0, argb);
                // The waves, within 45 degrees of the middle: x = 12 + the root of r squared less y squared, two wide.
                for (int wave = 0; wave < waves; wave++)
                {
                        f64 radius = wave ? 9.0 : 5.0, a = up < 0.0 ? -up : up, b = down < 0.0 ? -down : down, lo = a < b ? a : b, hi = a < b ? b : a;
                        f64 xa, xb;

                        if (lo * 100.0 > radius * 72.0)
                                continue;
                        hi = hi > radius ? radius : hi;
                        xa = __builtin_sqrt(radius * radius - lo * lo), xb = __builtin_sqrt(radius * radius - hi * hi);
                        ui_speaker_span(ui, left, size, top + row, 12.0 + xb - 1.0, 12.0 + xa + 1.0, argb);
                }
                // The cross of a struck speaker: two diagonals of two wide, over x 14 to 22 and y -4 to 4.
                if (struck && near <= 4.0)
                        for (int arm = -1; arm <= 1; arm += 2)
                        {
                                f64 x = 18.0 + (f64)arm * middle, reach = 1.0 + high / 2.0;

                                ui_speaker_span(ui, left, size, top + row, x - reach, x + reach, argb);
                        }
        }
}

/*
        A microphone, in the speaker's grid (24, whole rows, size to a side, the window's direction): a capsule 6 wide with round ends,
        the holder under it (a half ring with two short sides), a stem and a foot. Struck, a diagonal runs from the top left to
        the bottom right across all of it, a little longer than the glyph so that it shows where it lies over the capsule.
*/
static fn ui_microphone(struct ui address_to ui, int left, int top, int size, b32 struck, unsigned int argb)
{
        f64 high = 24.0 / (f64)size;

        for (int row = 0; row < size; row++)
        {
                f64 y = ((f64)row) * high - 12.0 + high / 2.0, reach = 1.0 + high / 2.0;

                if (y >= -10.0 && y <= 2.0)
                {
                        f64 beyond = y < -7.0 ? y + 7.0 : y > -1.0 ? y + 1.0 : 0.0, half = __builtin_sqrt(9.0 - beyond * beyond);

                        ui_speaker_span(ui, left, size, top + row, 12.0 - half, 12.0 + half, argb);
                }
                if (y >= -4.0 && y <= 5.5)
                {
                        f64 down = y + 1.0, outer = y < -1.0 ? 6.5 : __builtin_sqrt(6.5 * 6.5 - down * down), inner = y < -1.0 ? 4.5 : down < 4.5 ? __builtin_sqrt(4.5 * 4.5 - down * down) : 0.0;

                        if (inner > 0.0)
                        {
                                ui_speaker_span(ui, left, size, top + row, 12.0 - outer, 12.0 - inner, argb);
                                ui_speaker_span(ui, left, size, top + row, 12.0 + inner, 12.0 + outer, argb);
                        }
                        else
                                ui_speaker_span(ui, left, size, top + row, 12.0 - outer, 12.0 + outer, argb);
                }
                if (y > 5.5 && y <= 8.0)
                        ui_speaker_span(ui, left, size, top + row, 11.0, 13.0, argb);
                if (y > 8.0 && y <= 10.0)
                        ui_speaker_span(ui, left, size, top + row, 8.0, 16.0, argb);
                if (struck && y >= -10.0 && y <= 10.0)
                        ui_speaker_span(ui, left, size, top + row, 12.0 + y * 0.8 - reach, 12.0 + y * 0.8 + reach, argb);
        }
}

/*
        A level meter's state, one channel's. What is fed it is the greatest sample since the last look (15 bits, the sound
        server's page), and what it shows is that on a scale of decibels from 60 under full scale to full scale, which rises
        at once and falls over 1.5 seconds from the top to the bottom, with a line that holds the highest for a second and
        then falls the same way. Nothing in it is a clock of its own: the times are the caller's.
*/
#define UI_LEVEL_FALL_NS 1500000000ll
#define UI_LEVEL_HOLD_NS 1000000000ll
#define UI_LEVEL_FULL 32768u // full scale in the page's 15 bits and one

struct ui_level
{
        b64 shown, hold; // the bar and the line, as they were at at and at held
        b64 at, held;    // ns: the last feed, and when the line began to wait
};

// A peak as a share of the meter (Q30): 20 log10 (peak / full scale) over 60 dB, plus one, held to 0 and 1. The logarithm is the
// whole part of the binary one and the rest of the number as a fraction of the octave, which is a half decibel out at worst.
static b64 ui_level_of(unsigned int peak)
{
        unsigned int octave = 0;
        b64 log2, share;

        if (!peak)
                return 0;
        peak = peak > UI_LEVEL_FULL ? UI_LEVEL_FULL : peak;
        while (peak >> (octave + 1))
                octave++;
        log2 = ((b64)octave << 16) + (((b64)(peak - (1u << octave)) << 16) >> octave);
        share = UI_ONE + (log2 - (15 << 16)) * (UI_ONE >> 16) * 100343 / 1000000; // 6.0206 dB an octave, over 60
        return share < 0 ? 0 : share > UI_ONE ? UI_ONE : share;
}

// What the meter shows at now: the bar after its fall, and the line after its wait and its fall, never under the bar.
static fn ui_level_now(const struct ui_level address_to l, b64 now, b64 address_to shown, b64 address_to hold)
{
        b64 bar = l->shown - ui_progress(now - l->at, UI_LEVEL_FALL_NS), line;

        bar = bar < 0 ? 0 : bar;
        line = now - l->held <= UI_LEVEL_HOLD_NS ? l->hold : l->hold - ui_progress(now - l->held - UI_LEVEL_HOLD_NS, UI_LEVEL_FALL_NS);
        *shown = bar;
        *hold = line < bar ? bar : line;
}

// A peak seen at now.
static fn ui_level_feed(struct ui_level address_to l, unsigned int peak, b64 now)
{
        b64 target = ui_level_of(peak), bar, line;

        ui_level_now(l, now, &bar, &line);
        if (target > bar)
                bar = target;
        if (target >= line)
                line = target, l->held = now;
        else if (line < l->hold)
                l->held = now - UI_LEVEL_HOLD_NS; // the line has begun to fall: it goes on from here
        l->hold = line;
        l->shown = bar;
        l->at = now;
}

// Whether the meter shows anything now, so that the window is drawn again.
static b32 ui_level_moving(const struct ui_level address_to l, b64 now)
{
        b64 bar, line;

        ui_level_now(l, now, &bar, &line);
        return bar > 0 || line > 0;
}

/*
        The meter drawn: two bars (left, right) UI_METER_BAR wide with UI_METER_GAP between, from top to bottom (device pixels), x the
        left of the pair as it is laid out from the left. A bar is green up to -15 dB, amber to -5 and red above, and its line
        is a thin bar of the colour it stands in.
*/
#define UI_METER_BAR 4
#define UI_METER_GAP 2
#define UI_METER_WIDTH (2 * UI_METER_BAR + UI_METER_GAP)
#define UI_METER_AMBER UI_Q2(75) // three quarters of the meter: -15 dB
#define UI_METER_RED UI_Q2(92)   // and -5 dB

static unsigned int ui_meter_ink(b64 share)
{
        return ui_role(share > UI_METER_RED ? UI_ERROR : share > UI_METER_AMBER ? UI_WARNING : UI_SUCCESS);
}

static fn ui_meter_draw(struct ui address_to ui, int x, int top, int bottom, const b64 address_to shown, const b64 address_to hold)
{
        for (int channel = 0; channel < 2; channel++)
        {
                int bar = ui_dev(ui, UI_METER_BAR), left = x + (ui_rtl() ? 1 - channel : channel) * (bar + ui_dev(ui, UI_METER_GAP));
                b32 height = (b32)(bottom - top) * 256;
                struct ui_box track = {left * 256, top * 256, (left + bar) * 256, bottom * 256, bar * 128, bar * 128}, piece;
                b64 edges[4] = {0, UI_METER_AMBER, UI_METER_RED, UI_ONE};

                ui_kit_fill(ui, &track, ui_mix(ui_role(UI_ON_SURFACE), 14, 0), null);
                for (int part = 0; part < 3; part++)
                {
                        b64 low = edges[part], high = edges[part + 1] < shown[channel] ? edges[part + 1] : shown[channel];

                        if (high <= low)
                                break;
                        piece = (struct ui_box){track.x0, track.y1 - (b32)ui_qmul(high, height), track.x1, track.y1 - (b32)ui_qmul(low, height), 0, 0};
                        ui_kit_fill(ui, &piece, ui_meter_ink(low + 1), &track);
                }
                if (hold[channel] > 0)
                {
                        b32 line = track.y1 - (b32)ui_qmul(hold[channel], height), thick = ui_q8(ui, 2);

                        piece = (struct ui_box){track.x0, line - thick / 2, track.x1, line + thick - thick / 2, 0, 0};
                        ui_kit_fill(ui, &piece, ui_meter_ink(hold[channel]), &track);
                }
        }
}

/*
        Strips: the columns of a mixer, one for each thing that plays and the device first. A strip is a disc (a speaker for the
        device, the first letter of a program for a program, a dot on it while it plays), its name, its volume as a number, a
        level meter and a vertical slider side by side, and a mute button under them. They are laid out from the start edge,
        UI_STRIP_W each (the device's set off from the rest by UI_STRIP_GAP), and scroll along when they do not fit: the wheel
        over the empty ground, the wheel turned sideways, a drag of the ground, and the keys, which move the focus and scroll to
        it. An app says only what each strip is and acts on what the kit reports:

            ui_strips_begin   a new listing is coming; ui_strips_add, one call for each strip in order; ui_strips_end
            ui_strips_event   a key or a pointer event: the UI_STRIPS_ bits of what the strips made of it; the strip that
                              changed is s->changed, with its new percent and mute in s->item[]
            ui_strips_draw    the strips, in the rectangle given to ui_strips_place

        The keys, with the window's: Left and Right (the way the window reads) and Tab pick a strip, Up and Down change its
        volume by one, Page Up and Page Down by ten, Home sets 100 and End sets 0, M and Space and Enter mute and unmute it.
        The wheel over a strip changes its volume by two a notch, up raising it. A pointer press on the slider sets the volume
        where it lands and goes on following the hand; a finger does the same with a target at least UI_TARGET each way.
        A volume the person has just changed is not replaced by what the app lists for UI_STRIPS_HOLD_NS: the server reports it
        a moment after it has taken it.
*/
#define UI_STRIPS_MOST 34
#define UI_STRIP_W 92         // a strip with its gutter, logical pixels
#define UI_STRIP_GAP 12       // extra after the first
#define UI_STRIP_PAD 8        // before the first, and the window's edge
#define UI_STRIP_FACE 36      // the disc
#define UI_STRIP_SLIDER_W 28  // the slider's box: the knob and its halo
#define UI_STRIP_GROUP (UI_METER_WIDTH + 6 + UI_STRIP_SLIDER_W)
#define UI_STRIP_MUTE_W 76
#define UI_STRIP_MUTE_H 32
#define UI_STRIP_LEAST 60     // the shortest a slider is, before the strips are too short to be used
#define UI_STRIPS_HOLD_NS 600000000ll
#define UI_STRIPS_WHEEL 2     // percent a notch
#define UI_STRIPS_DRAW 1u     // the strips' picture is due again
#define UI_STRIPS_TAKEN 2u    // the event was the strips', and the app does not have it
#define UI_STRIPS_VOLUME 4u   // the volume of strip s->changed changed
#define UI_STRIPS_MUTE 8u     // the mute of strip s->changed changed
#define UI_KEY_M 50

struct ui_strip
{
        unsigned long id;     // the app's own number for it (a sink input's index), which its state follows through a new listing
        char name[96];
        char initial[8];
        int percent;          // 0 to 100
        b32 muted, master, playing;
        struct ui_level level[2];
        b64 changed_at;       // ns: when the person last changed it
};

struct ui_strips
{
        struct ui_strip item[UI_STRIPS_MOST], before[UI_STRIPS_MOST];
        int count, before_count, changed, focus;
        unsigned long focus_id;
        struct ui_control control[2 * UI_STRIPS_MOST]; // strip k's slider is 2k and its mute button 2k + 1
        struct ui_control face;                        // the disc of a program
        struct ui_tween shift;                         // how far they are scrolled, device pixels
        int x, y, w, h;                                // where they are, device pixels
        int pointer_x, pointer_y, wheel, grab_x;
        b32 listing, ready, dragging;
        b64 grab_shift;
        char mute_label[UI_STRIPS_MOST][2][24];
};

static fn ui_strips_begin(struct ui_strips address_to s)
{
        if (!s->ready)
        {
                ui_tween_put(&s->shift, 0, UI_TWEEN_NUMBER);
                s->changed = -1;
                s->focus = 0;
                s->ready = 1;
        }
        memory_copy(s->before, s->item, (positive)s->count * sizeof(s->item[0]));
        s->before_count = s->count;
        s->count = 0;
        s->listing = 1;
}

// The first letter of a name, capital, whole (UTF-8): the disc's letter.
static fn ui_strip_initial(char address_to into, positive room, const char address_to name)
{
        positive size = 1;

        into[0] = 0;
        if (!name[0] || room < 4)
                return;
        if ((p8)name[0] >= 0xc0)
                for (; size < 4 && ((p8)name[size] & 0xc0) == 0x80; size++)
                        ;
        memory_copy(into, name, size);
        into[size] = 0;
        if (size == 1 && into[0] >= 'a' && into[0] <= 'z')
                into[0] = (char)(into[0] - 32);
}

static fn ui_strips_add(struct ui_strips address_to s, unsigned long id, const char address_to name, int percent, b32 muted, b32 master, b32 playing,
                        unsigned int left, unsigned int right, b64 now)
{
        struct ui_strip address_to strip;
        struct ui_strip address_to old = null;

        if (!s->listing || s->count >= UI_STRIPS_MOST)
                return;
        for (int k = 0; k < s->before_count; k++)
                if (s->before[k].id == id)
                        old = &s->before[k];
        strip = &s->item[s->count++];
        if (old)
                *strip = *old;
        else
                memory_zero(strip, sizeof(*strip));
        strip->id = id;
        string_copy_bounded((string_address)strip->name, (string_address)name, sizeof(strip->name));
        ui_strip_initial(strip->initial, sizeof(strip->initial), name);
        strip->master = master, strip->playing = playing;
        if (!old || now - strip->changed_at > UI_STRIPS_HOLD_NS)
        {
                strip->percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
                strip->muted = muted;
        }
        ui_level_feed(&strip->level[0], left, now);
        ui_level_feed(&strip->level[1], right, now);
}

static fn ui_strips_end(struct ui_strips address_to s)
{
        s->listing = 0;
        // The keys stay on the strip they were on, by its number, and else on the one in its place.
        for (int k = 0; k < s->count; k++)
                if (s->item[k].id == s->focus_id)
                {
                        s->focus = k;
                        return;
                }
        s->focus = s->focus >= s->count ? s->count - 1 : s->focus;
        s->focus = s->focus < 0 ? 0 : s->focus;
        s->focus_id = s->count ? s->item[s->focus].id : 0;
}

// The device pixels the strips take from the start edge to the end of the last, and how far they can be scrolled.
static int ui_strips_total(const struct ui address_to ui, const struct ui_strips address_to s)
{
        return s->count ? ui_dev(ui, 2 * UI_STRIP_PAD + s->count * UI_STRIP_W + UI_STRIP_GAP * (s->count > 1)) : 0;
}

static int ui_strips_most(const struct ui address_to ui, const struct ui_strips address_to s)
{
        int most = ui_strips_total(ui, s) - s->w;

        return most > 0 ? most : 0;
}

// The left of strip k as it is laid out from the left, before the scroll, and then where it is (scrolled, and turned for a window that reads from the right).
static int ui_strip_left(const struct ui address_to ui, int k)
{
        return ui_dev(ui, UI_STRIP_PAD + k * UI_STRIP_W + (k > 0 ? UI_STRIP_GAP : 0));
}

static int ui_strip_at(const struct ui address_to ui, const struct ui_strips address_to s, int k, b64 shift)
{
        return ui_mirror(s->x + ui_strip_left(ui, k) - (int)shift, ui_dev(ui, UI_STRIP_W), s->x, s->w);
}

// One element of a strip, offset from the strip's start edge and w wide, as it lies in the window.
static int ui_strip_part(const struct ui address_to ui, int strip_left, int offset, int w)
{
        return ui_rtl() ? strip_left + ui_dev(ui, UI_STRIP_W) - offset - w : strip_left + offset;
}

// How far the strips are scrolled now, held to what there is to scroll (the window may have grown, or a strip gone, since it was asked).
static b64 ui_strips_shift(const struct ui address_to ui, struct ui_strips address_to s)
{
        b64 shift = s->ready ? ui_tween_at(&s->shift, ui_kit.now, &(b32){0}) : 0;
        int most = ui_strips_most(ui, s);

        return shift < 0 ? 0 : shift > most ? most : shift;
}

static fn ui_strips_reveal(struct ui address_to ui, struct ui_strips address_to s, int k);

/*
        Where the strips are, and every control of them laid out: the slider's box and the mute button, in logical pixels, at the
        scroll the strips are at now; and what the controls show of what the strips are (a value, a muted track, the button's
        words). Said again at every frame and before every event, so the controls and the picture agree.
*/
static fn ui_strips_place(struct ui address_to ui, struct ui_strips address_to s, int x, int y, int w, int h)
{
        b64 shift;
        int track_top, track_bottom, group = (UI_STRIP_W - UI_STRIP_GROUP) / 2;

        s->x = x, s->y = y, s->w = w, s->h = h;
        shift = ui_strips_shift(ui, s);
        track_top = 16 + UI_STRIP_FACE + 8 + 16 + 24;
        track_bottom = h * 100 / ui->scale - 16 - UI_STRIP_MUTE_H - 12;
        if (track_bottom - track_top < UI_STRIP_LEAST)
                track_bottom = track_top + UI_STRIP_LEAST;
        for (int k = 0; k < s->count; k++)
        {
                struct ui_control address_to slider = &s->control[2 * k], address_to mute = &s->control[2 * k + 1];
                struct ui_strip address_to strip = &s->item[k];
                int left = ui_strip_at(ui, s, k, shift), box = ui_strip_part(ui, left, ui_dev(ui, group + UI_METER_WIDTH + 6), ui_dev(ui, UI_STRIP_SLIDER_W));
                int button = ui_strip_part(ui, left, ui_dev(ui, (UI_STRIP_W - UI_STRIP_MUTE_W) / 2), ui_dev(ui, UI_STRIP_MUTE_W));

                slider->kind = UI_SLIDER, slider->variant = UI_SLIDER_VERTICAL | UI_SLIDER_LINE;
                slider->low = 0, slider->high = ui_q(100), slider->step = ui_q(1);
                slider->x = box * 100 / ui->scale, slider->y = (y * 100 / ui->scale) + track_top;
                slider->w = UI_STRIP_SLIDER_W, slider->h = track_bottom - track_top;
                slider->on = strip->muted;
                if (!slider->down && !slider->sliding)
                        slider->value = ui_q(strip->percent);
                mute->kind = UI_BUTTON, mute->variant = strip->muted ? UI_BUTTON_PRIMARY : UI_BUTTON_OUTLINED;
                mute->x = button * 100 / ui->scale, mute->y = (y * 100 / ui->scale) + h * 100 / ui->scale - 16 - UI_STRIP_MUTE_H;
                mute->w = UI_STRIP_MUTE_W, mute->h = UI_STRIP_MUTE_H;
                string_copy_bounded((string_address)s->mute_label[k][strip->muted], (string_address)(strip->muted ? WORDS(mixer_unmute) : WORDS(mixer_mute)),
                                    sizeof(s->mute_label[k][0]));
                mute->label = s->mute_label[k][strip->muted];
        }
}

// The strips scrolled so that strip k is whole in view: the nearest scroll that shows it, over 220 ms.
static fn ui_strips_reveal(struct ui address_to ui, struct ui_strips address_to s, int k)
{
        int near = ui_strip_left(ui, k) - ui_dev(ui, UI_STRIP_PAD), far = ui_strip_left(ui, k) + ui_dev(ui, UI_STRIP_W) + ui_dev(ui, UI_STRIP_PAD);
        b64 now = ui_tween_at(&s->shift, ui_kit.now, &(b32){0}), want = now;
        int most = ui_strips_most(ui, s);

        if (k == 0)
                near = 0;
        if (now > near)
                want = near;
        else if (now < far - s->w)
                want = far - s->w;
        want = want < 0 ? 0 : want > most ? most : want;
        ui_kit_number_to(&s->shift, want, 220, &ui_ease);
}

// The strip a device x is over, or -1.
static int ui_strips_over(const struct ui address_to ui, struct ui_strips address_to s, int x)
{
        b64 shift = ui_strips_shift(ui, s);

        for (int k = 0; k < s->count; k++)
        {
                int left = ui_strip_at(ui, s, k, shift);

                if (x >= left && x < left + ui_dev(ui, UI_STRIP_W))
                        return k;
        }
        return -1;
}

static fn ui_strips_draw(struct ui address_to ui, struct ui_strips address_to s, b32 keys)
{
        b64 shift;
        char text[96], fitted[96];

        ui_kit_tween(&s->shift);
        ui_strips_place(ui, s, s->x, s->y, s->w, s->h);
        shift = ui_strips_shift(ui, s);
        for (int k = 0; k < s->count; k++)
        {
                struct ui_strip address_to strip = &s->item[k];
                struct ui_control address_to slider = &s->control[2 * k];
                int left = ui_strip_at(ui, s, k, shift), face = ui_dev(ui, UI_STRIP_FACE), centre = left + ui_dev(ui, UI_STRIP_W) / 2;
                int room = ui_dev(ui, UI_STRIP_W - 8), width, group = (UI_STRIP_W - UI_STRIP_GROUP) / 2;
                string_address name;
                b64 shown[2], hold[2];
                unsigned int ink = strip->muted ? ui_role(UI_ON_SURFACE_VARIANT) : ui_role(UI_ON_SURFACE);

                if (left + ui_dev(ui, UI_STRIP_W) <= s->x || left >= s->x + s->w)
                        continue;
                // The disc: the output device's has a speaker in it, the microphone's (master 2) a microphone, a program's its letter and a dot while it plays.
                if (strip->master)
                {
                        int waves = strip->muted || !strip->percent ? 0 : strip->percent < 50 ? 1 : 2, size = ui_dev(ui, 22);

                        ui_kit_fill(ui, &(struct ui_box){(centre - face / 2) * 256, (s->y + ui_dev(ui, 16)) * 256, (centre + face / 2) * 256,
                                                         (s->y + ui_dev(ui, 16) + face) * 256, face * 128, face * 128},
                                    ui_role(UI_SURFACE_HIGHEST), null);
                        if (strip->master == 2)
                                ui_microphone(ui, centre - size / 2, s->y + ui_dev(ui, 16) + (face - size) / 2, size, strip->muted, ink);
                        else
                                ui_speaker(ui, centre - size / 2, s->y + ui_dev(ui, 16) + (face - size) / 2, size, waves, strip->muted, ink);
                }
                else
                {
                        s->face.kind = UI_AVATAR, s->face.variant = 0, s->face.w = UI_STRIP_FACE, s->face.on = strip->playing;
                        s->face.label = strip->initial;
                        s->face.x = (centre - face / 2) * 100 / ui->scale, s->face.y = s->y * 100 / ui->scale + 16;
                        ui_avatar_draw(ui, &s->face);
                }
                // Its name, cut to the strip, and its volume.
                name = ui_fit(ui, (string_address)strip->name, UI_LABEL_MEDIUM, room, fitted, sizeof(fitted));
                width = ui_kit_measure(ui, name, UI_LABEL_MEDIUM);
                ui_kit_text(ui, centre - width / 2, ui_kit_baseline(ui, s->y + ui_dev(ui, 16 + UI_STRIP_FACE + 8), UI_LABEL_MEDIUM), name, UI_LABEL_MEDIUM, ink, 0);
                language_format(text, sizeof(text), WORDS(settings_percent), (long)strip->percent);
                width = ui_kit_measure(ui, (string_address)text, UI_BODY_MEDIUM);
                ui_kit_text(ui, centre - width / 2, ui_kit_baseline(ui, s->y + ui_dev(ui, 16 + UI_STRIP_FACE + 8 + 16), UI_BODY_MEDIUM), (string_address)text,
                            UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0);
                // The meter beside the slider, the length of its track.
                for (int channel = 0; channel < 2; channel++)
                        ui_level_now(&strip->level[channel], ui_kit.now, &shown[channel], &hold[channel]);
                ui_meter_draw(ui, ui_strip_part(ui, left, ui_dev(ui, group), ui_dev(ui, UI_METER_WIDTH)), ui_dev(ui, slider->y), ui_dev(ui, slider->y + slider->h),
                              shown, hold);
                if (shown[0] > 0 || shown[1] > 0 || hold[0] > shown[0] || hold[1] > shown[1])
                        ui_kit.busy = 1;
                slider->focused = keys && s->focus == k;
                ui_control_draw(ui, slider);
                ui_control_draw(ui, &s->control[2 * k + 1]);
        }
        // Where the strips are scrolled to, when they are: a thin bar along the foot.
        if (ui_strips_most(ui, s) > 0)
        {
                int total = ui_strips_total(ui, s), bar = (int)((b64)s->w * s->w / total), at = (int)((b64)(s->w - bar) * shift / ui_strips_most(ui, s));
                int y = s->y + s->h - ui_dev(ui, 6), thick = ui_dev(ui, 3);

                at = ui_rtl() ? s->w - bar - at : at;
                ui_kit_fill(ui, &(struct ui_box){(s->x + at) * 256, y * 256, (s->x + at + bar) * 256, (y + thick) * 256, thick * 128, thick * 128},
                            ui_mix(ui_role(UI_ON_SURFACE), 24, 0), null);
        }
}

// A line of words in the middle of the area to the right of the first strip, or of all of it with none: what there is to say instead of strips.
static fn ui_strips_message(struct ui address_to ui, struct ui_strips address_to s, const char address_to text, b32 beside)
{
        int taken = beside ? ui_dev(ui, UI_STRIP_PAD + UI_STRIP_W + UI_STRIP_GAP) : 0, left = s->x + (ui_rtl() ? 0 : taken), width = s->w - taken;
        int measured = ui_kit_measure(ui, (string_address)text, UI_BODY_MEDIUM), at = measured < width ? (width - measured) / 2 : 0;

        ui_kit_text_in(ui, left + at, ui_kit_baseline(ui, s->y + s->h / 2 - ui_dev(ui, 10), UI_BODY_MEDIUM), (string_address)text, UI_BODY_MEDIUM,
                       ui_role(UI_ON_SURFACE_VARIANT), 0, left, left + width);
}

// A value set for strip k by the person: it is theirs for a moment, whatever the app lists meanwhile, and the slider shows it.
static fn ui_strips_set(struct ui_strips address_to s, int k, int percent)
{
        struct ui_control address_to slider = &s->control[2 * k];

        percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
        s->item[k].percent = percent;
        s->item[k].changed_at = ui_kit.now;
        slider->value = ui_q(percent);
        slider->sliding = 0;
        s->changed = k;
}

static p32 ui_strips_key(struct ui address_to ui, struct ui_strips address_to s, const struct window_event address_to event)
{
        int key = event->a, k = s->focus, percent;

        if (!(event->flags & WINDOW_KEY_DOWN) || ui_key_modifier(key) || !s->count)
                return 0;
        s->focus_id = s->item[k].id;
        percent = s->item[k].percent;
        switch (key)
        {
        case UI_KEY_UP:
        case UI_KEY_DOWN:
        case UI_KEY_PAGE_UP:
        case UI_KEY_PAGE_DOWN:
        case UI_KEY_HOME:
        case UI_KEY_END:
                percent = key == UI_KEY_UP ? percent + 1 : key == UI_KEY_DOWN ? percent - 1 : key == UI_KEY_PAGE_UP ? percent + 10
                          : key == UI_KEY_PAGE_DOWN ? percent - 10 : key == UI_KEY_HOME ? 100 : 0;
                percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
                if (percent == s->item[k].percent)
                        return UI_STRIPS_TAKEN;
                ui_strips_set(s, k, percent);
                return UI_STRIPS_DRAW | UI_STRIPS_TAKEN | UI_STRIPS_VOLUME;
        case UI_KEY_M:
        case UI_KEY_SPACE:
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
                if (event->flags & WINDOW_KEY_REPEAT)
                        return UI_STRIPS_TAKEN;
                s->item[k].muted = !s->item[k].muted;
                s->item[k].changed_at = ui_kit.now;
                s->changed = k;
                return UI_STRIPS_DRAW | UI_STRIPS_TAKEN | UI_STRIPS_MUTE;
        case UI_KEY_LEFT:
        case UI_KEY_RIGHT:
                k += ui_arrow(key);
                break;
        case UI_KEY_TAB:
                k += (event->flags & WINDOW_KEY_SHIFT) ? -1 : 1;
                k = k < 0 ? s->count - 1 : k >= s->count ? 0 : k;
                break;
        default:
                return 0;
        }
        k = k < 0 ? 0 : k >= s->count ? s->count - 1 : k;
        s->focus = k;
        s->focus_id = s->item[k].id;
        ui_strips_reveal(ui, s, k);
        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;
}

// The wheel: over a strip it changes the volume, two a notch, up raising it; elsewhere, or sideways, or with Shift, it scrolls the strips.
static p32 ui_strips_wheel(struct ui address_to ui, struct ui_strips address_to s, const struct window_event address_to event)
{
        int over = ui_strips_over(ui, s, s->pointer_x), steps;
        b32 sideways = event->b != 0 || (event->flags & WINDOW_KEY_SHIFT) != 0 || over < 0 || s->pointer_y < s->y || s->pointer_y >= s->y + s->h;
        long total = (long)s->wheel + (long)(event->b ? event->b : event->a);

        steps = (int)(total / 120);
        s->wheel = (int)(total - (long)steps * 120);
        if (!steps)
                return UI_STRIPS_TAKEN;
        if (sideways)
        {
                b64 now = ui_tween_at(&s->shift, ui_kit.now, &(b32){0});
                int most = ui_strips_most(ui, s);
                b64 want = now - (b64)(event->b ? -steps : steps) * ui_dev(ui, UI_STRIP_W) * ui_dir();

                ui_kit_number_to(&s->shift, want < 0 ? 0 : want > most ? most : want, 220, &ui_ease);
                return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;
        }
        s->focus = over, s->focus_id = s->item[over].id;
        if ((steps > 0 && s->item[over].percent >= 100) || (steps < 0 && s->item[over].percent <= 0))
                return UI_STRIPS_TAKEN;
        ui_strips_set(s, over, s->item[over].percent + steps * UI_STRIPS_WHEEL);
        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN | UI_STRIPS_VOLUME;
}

static p32 ui_strips_pointer(struct ui address_to ui, struct ui_strips address_to s, const struct window_event address_to event)
{
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        struct ui_control address_to changed;
        int type = (int)event->type, x = s->pointer_x, y = s->pointer_y;
        p32 bits = 0;

        if (type == WINDOW_EVENT_MOTION)
        {
                x = s->pointer_x = event->a, y = s->pointer_y = event->b;
                if (s->dragging)
                {
                        int most = ui_strips_most(ui, s);
                        b64 want = s->grab_shift - (b64)(x - s->grab_x) * ui_dir();

                        ui_tween_put(&s->shift, want < 0 ? 0 : want > most ? most : want, UI_TWEEN_NUMBER);
                        return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;
                }
        }
        if (type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT)
                return 0;
        ui_strips_place(ui, s, s->x, s->y, s->w, s->h);
        if (type == WINDOW_EVENT_BUTTON && !down && s->dragging)
        {
                s->dragging = 0;
                return UI_STRIPS_DRAW | UI_STRIPS_TAKEN;
        }
        changed = ui_kit_pointer(ui, s->control, 2 * s->count, type, x, y, down, event->flags);
        bits = UI_STRIPS_DRAW;
        if (type == WINDOW_EVENT_BUTTON && down)
        {
                if (ui_kit_held >= s->control && ui_kit_held < s->control + 2 * s->count)
                {
                        s->focus = (int)(ui_kit_held - s->control) / 2;
                        s->focus_id = s->item[s->focus].id;
                        bits |= UI_STRIPS_TAKEN;
                }
                else if (ui_strips_most(ui, s) > 0 && x >= s->x && x < s->x + s->w && y >= s->y && y < s->y + s->h)
                {
                        s->dragging = 1, s->grab_x = x, s->grab_shift = ui_tween_at(&s->shift, ui_kit.now, &(b32){0});
                        bits |= UI_STRIPS_TAKEN;
                }
        }
        if (changed && changed >= s->control && changed < s->control + 2 * s->count)
        {
                int k = (int)(changed - s->control) / 2;

                s->changed = k;
                if ((changed - s->control) % 2)
                {
                        s->item[k].muted = !s->item[k].muted;
                        s->item[k].changed_at = ui_kit.now;
                        bits |= UI_STRIPS_MUTE | UI_STRIPS_TAKEN;
                }
                else
                {
                        b64 target = changed->sliding ? changed->slide.to : changed->value;
                        int percent = (int)((target + UI_HALF) >> UI_Q);

                        s->item[k].percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
                        s->item[k].changed_at = ui_kit.now;
                        bits |= UI_STRIPS_VOLUME | UI_STRIPS_TAKEN;
                }
        }
        return bits;
}

static p32 ui_strips_event(struct ui address_to ui, struct ui_strips address_to s, const struct window_event address_to event)
{
        if (event->type == WINDOW_EVENT_KEY)
                return ui_strips_key(ui, s, event);
        if (event->type == WINDOW_EVENT_WHEEL)
                return ui_strips_wheel(ui, s, event);
        if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
                return ui_strips_pointer(ui, s, event);
        return 0;
}

/* ---- a list of rows in sections ---- */
/*
        A list of rows in sections: ui_list_layout places the rows, ui_list_row_y and ui_list_hit map a row to pixels and back,
        ui_list_pointer and ui_list_key take input, ui_list_press_slide and ui_list_let_go finish a press, ui_list_damage marks a
        row dirty and ui_list_draw paints it. An app gives its rows; the kit owns the press, the slide, the scroll and the damage.
*/

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
        int x, y, w, h;                    // where it is in the window, device pixels: the box its words and controls keep to
        int row_x, row_w;                  // how far across its rows run (hover, press, the row the keys are on), device pixels: 0 wide is x, w
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
        // The bar is in the margin at the content's end: its right, or its left reading from the right (the mirror of the box).
        l->scroll.x = ui_rtl() ? x - ui_dev(ui, UI_SCROLL_WIDTH) - (ui_dev(ui, UI_PAD) - ui_dev(ui, UI_SCROLL_WIDTH)) / 2
                               : x + w + (ui_dev(ui, UI_PAD) - ui_dev(ui, UI_SCROLL_WIDTH)) / 2;
        l->scroll.y = y, l->scroll.height = h, l->scroll.view = h;
        l->scroll.content = l->content;
        l->scroll.bounds = 1, l->scroll.scale = ui->scale;
        ui_scroll_clamp(&l->scroll);
}

// How far across the list's rows run (device pixels): what the page gave them, else the list's own box.
static void ui_list_across(const struct ui_list address_to l, int address_to x, int address_to w)
{
        *x = l->row_w > 0 ? l->row_x : l->x;
        *w = l->row_w > 0 ? l->row_w : l->w;
}

// The same in logical pixels, for the rows' controls. A list the page extended has both edges rounded up: the left edge then
// lies at or past the edge of what is beside it (the column's rows end at that logical px) and the right one at or past the
// window's edge, which clips it, so no hairline of the ground is left between a row and the window's edge.
static void ui_list_across_logical(const struct ui address_to ui, const struct ui_list address_to l, int address_to x, int address_to w)
{
        int across, wide;

        ui_list_across(l, &across, &wide);
        if (l->row_w <= 0)
        {
                *x = across * 100 / ui->scale, *w = wide * 100 / ui->scale;
                return;
        }
        *x = (across * 100 + ui->scale - 1) / ui->scale;
        *w = ((across + wide) * 100 + ui->scale - 1) / ui->scale - *x;
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

/*
        Where an item of a region is drawn, in device pixels: its top and its height. At rest that is its place and its height.
        A pull past an end of the region stretches it in the scroll axis from the edge it is pulled past (ui_scroll_stretched):
        the items keep their places from that edge, their tops and bottoms scaled about it by the stretch, and the item's text
        is set in the middle of the stretched item (the kit's stretch of a label). Every region that scrolls takes it the same.
*/
// A y in the region's scroll axis as its stretch draws it: scaled about the edge the region is pulled past, by the stretch (the
// same for its items, its track and its thumb). At rest it is y.
static int ui_scroll_map(const struct ui_scroll address_to s, int y)
{
        int edge = 0, pivot, drawn_end;
        b64 scale = UI_ONE + ui_scroll_stretched(s, &edge);

        if (!edge)
                return y;
        // The edge pulled past: the region's top, or where its drawn items end (the view's bottom once it scrolls).
        drawn_end = s->content - s->scroll;
        pivot = edge < 0 ? s->y : s->y + (drawn_end < s->view ? drawn_end : s->view);
        return pivot + (int)ui_qmul(y - pivot, scale);
}

// A y of the track, and of the thumb in it, as a stretch draws it: the track shrinks toward the wall it is pushed against, its end
// at that wall fixed and its far end moved toward the wall by the stretch, as a body squashes against a wall. The thumb's ends go
// through the same map, so the thumb follows the track. At rest it is y.
static int ui_scroll_track_map(const struct ui_scroll address_to s, int y)
{
        int edge = 0, pivot, drawn_end;
        b64 scale = UI_ONE - ui_scroll_stretched(s, &edge);

        if (!edge)
                return y;
        drawn_end = s->content - s->scroll;
        pivot = edge < 0 ? s->y : s->y + (drawn_end < s->view ? drawn_end : s->view);
        return pivot + (int)ui_qmul(y - pivot, scale);
}

static void ui_scroll_span(const struct ui_scroll address_to s, int y, int row, int address_to top, int address_to height)
{
        *top = ui_scroll_map(s, y);
        *height = ui_scroll_map(s, y + row) - *top;
}

// A row of a list, where it is drawn (ui_scroll_span).
static void ui_list_row_span(const struct ui address_to ui, const struct ui_list address_to l, int at, int address_to top, int address_to height)
{
        ui_scroll_span(&l->scroll, ui_list_row_y(l, at), ui_dev(ui, UI_ROW), top, height);
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
        int inside = y - l->y + l->scroll.scroll, across, wide;

        ui_list_across(l, &across, &wide);
        if (x < across || x >= across + wide || y < l->y || y >= l->y + l->h)
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
        int page = l->scroll.view / ui_dev(ui, UI_ROW) - 1, by;

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
                c->y = (y + ui_dev(ui, 6)) * 100 / ui->scale, c->h = (height - ui_dev(ui, 12)) * 100 / ui->scale;
                c->w = (room > ui_dev(ui, UI_ROW_FIELD_W) && !row->head ? ui_dev(ui, UI_ROW_FIELD_W) : room < ui_dev(ui, UI_ROW_FIELD_LEAST) ? ui_dev(ui, UI_ROW_FIELD_LEAST) : room) * 100 / ui->scale;
                c->x = ui_mirror(right * 100 / ui->scale - c->w, c->w, l->x * 100 / ui->scale, l->w * 100 / ui->scale);
                return c;
        }
        ui_items_words(c, row->words);
        c->y = (y + height / 2) * 100 / ui->scale - 16, c->h = 32;
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
                c->w = ui_tabs_length(ui, c);
        }
        else
        {
                c->at = row->at;
                c->w = c->count * (UI_SWATCH + UI_SWATCH_GAP) - UI_SWATCH_GAP;
        }
        c->x = ui_mirror(right * 100 / ui->scale - c->w, c->w, l->x * 100 / ui->scale, l->w * 100 / ui->scale);
        c->item = ui_control_hit(ui, c, l->pointer_x, l->pointer_y);
        c->hover = c->item >= 0 && l->hover == id;
        return c;
}

/*
        A row at its place, y its top: the row the keys are on, the whole row square in the secondary container (ringed in the accent while
        the keys are in the list), the row's own control (its hover layer and its ripple, edge to edge, or its switch),
        the label at the left cut to leave its value a third of the row, and the value as its kind says at the right.
*/
static fn ui_list_row(struct ui address_to ui, struct ui_list address_to l, int id, int y, int height)
{
        struct ui_row row;
        int x = l->x, w = l->w, inset = ui_dev(ui, UI_INSET), gap = ui_dev(ui, 10);
        int right = x + w - inset, text_left = x + inset, baseline, wide = 0, label_room, across, across_w;
        unsigned int ink = ui_text_colour(ui, UI_ON_SURFACE), quiet = ui_text_colour(ui, UI_ON_SURFACE_VARIANT);
        struct ui_control address_to own = ui_list_control(l, id, 0), address_to trailing = null;
        char cut[UI_CUT_TEXT];
        // Whether any of its rows may be written, with room for a run's rows round its line.
        b32 seen = ui_rows_seen(ui, y - ui_dev(ui, 4), y + height + ui_dev(ui, 4));

        ui_list_describe(l, id, &row);
        ui_list_across(l, &across, &across_w);
        baseline = ui_kit_baseline(ui, y + (height - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM);
        if (row.dim)
                ink = quiet;
        if (l->lit && id == l->focus)
        {
                // The row the keys are on is the whole row, square, as the hover layer is: the row's own extents, not a box set in from them.
                struct ui_box box = {across * 256, y * 256, (across + across_w) * 256, (y + height) * 256, 0, 0};

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
                ui_list_across_logical(ui, l, &own->row_x, &own->row_w);
                own->row_y = y * 100 / ui->scale, own->row_h = height * 100 / ui->scale;
                own->x = own->row_x, own->y = own->row_y, own->w = own->row_w, own->h = own->row_h;
                own->hover = l->hover == id, own->down = l->down == id;
                own->disabled = row.kind == UI_ROW_FIELD;
                if (kind == UI_SWITCH)
                {
                        own->on = row.on;
                        own->x = ui_mirror(right * 100 / ui->scale - 52, 52, x * 100 / ui->scale, w * 100 / ui->scale), own->w = 52;
                        own->y = (y + height / 2) * 100 / ui->scale - 24, own->h = 48;
                }
                ui_control_draw(ui, own);
        }
        if (row.mark)
        {
                int dot = ui_dev(ui, 7);
                int dot_x = ui_mirror(text_left, dot, x, w);
                struct ui_box box = {dot_x * 256, (y + (height - dot) / 2) * 256, (dot_x + dot) * 256, (y + (height + dot) / 2) * 256, dot * 128, dot * 128};

                ui_kit_fill(ui, &box, ui_role(UI_PRIMARY), null);
                text_left += ui_dev(ui, 17);
        }
        // The trailing pieces from the right: the signal, then the control or the value, then the note.
        if (row.bars > 0)
        {
                // The bars are not turned over reading from the right (as a signal's are not): their place is the mirror of the left to right one.
                ui_signal(ui, ui_mirror(right - ui_signal_width(ui), ui_signal_width(ui), x, w) + ui_signal_width(ui), y + height / 2, row.bars);
                right -= ui_dev(ui, 18) + gap;
        }
        if (row.kind == UI_ROW_TABS || row.kind == UI_ROW_SWATCHES)
                trailing = ui_list_trailing(ui, l, id, &row, right, y, 0);
        label_room = right - text_left - (row.kind == UI_ROW_SWITCH ? ui_dev(ui, 52) + gap : trailing ? ui_dev(ui, trailing->w) + gap
                                         : row.kind == UI_ROW_FIELD && row.head ? (w - 2 * inset) * 2 / 3 : (w - 2 * inset) / 3);
        // A row no pixel of which may be written (a repaint of other rows) has its controls drawn, which is where its
        // motion is read, and no word measured: its label's width places only its value and a field's well.
        if (row.label && (seen || row.kind == UI_ROW_FIELD))
                wide = ui_kit_text_flipped(ui, text_left, baseline, ui_fit(ui, (string_address)row.label, UI_BODY_MEDIUM,
                                                                           label_room < ui_dev(ui, 40) ? ui_dev(ui, 40) : label_room, cut, sizeof(cut)),
                                           UI_BODY_MEDIUM, ink, 0, x, w);
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

                        ui_chevron(ui, ui_mirror(right - 2 * size, 2 * size + 1, x, w) + size, baseline - ui_dev(ui, 5), size, quiet);
                        right -= 2 * size + ui_dev(ui, 8);
                }
                else if (row.kind == UI_ROW_STEP)
                {
                        marks = ui_kit_measure(ui, (string_address) "\xe2\x80\xba", UI_LABEL_LARGE);
                        ui_kit_text_flipped(ui, right - marks, baseline, (string_address) "\xe2\x80\xba", UI_LABEL_LARGE, quiet, 0, x, w);
                        right -= marks + ui_dev(ui, 8);
                        room -= 2 * (marks + ui_dev(ui, 8));
                }
                room = row.kind == UI_ROW_CHOICE ? room - ui_dev(ui, 16) : room;
                shown = ui_fit(ui, (string_address)row.value, UI_BODY_MEDIUM, room < ui_dev(ui, 24) ? ui_dev(ui, 24) : room, cut, sizeof(cut));
                width = ui_kit_measure(ui, shown, UI_BODY_MEDIUM);
                ui_kit_text_flipped(ui, right - width, baseline, shown, UI_BODY_MEDIUM, row.kind != UI_ROW_PLAIN && !row.dim ? ui_text_colour(ui, UI_ON_SURFACE) : quiet, 0, x, w);
                if (row.kind == UI_ROW_STEP)
                        ui_kit_text_flipped(ui, right - width - ui_dev(ui, 8) - marks, baseline, (string_address) "\xe2\x80\xb9", UI_LABEL_LARGE, quiet, 0, x, w);
                right -= width + (marks ? marks + ui_dev(ui, 8) : 0) + gap;
        }
        if (row.note)
        {
                int width = ui_kit_measure(ui, (string_address)row.note, UI_LABEL_MEDIUM);

                if (right - width > text_left + wide + gap)
                        ui_kit_text_flipped(ui, right - width, ui_kit_baseline(ui, y + (height - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM), (string_address)row.note,
                                            UI_LABEL_MEDIUM, row.mark ? ui_text_colour(ui, UI_ON_SURFACE) : quiet, 0, x, w);
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
                ui_kit_text_flipped(ui, l->x + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, l->y + (row_height - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM),
                                    (string_address)l->empty, UI_BODY_MEDIUM, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0, l->x, l->w);
        for (int at = 0; at < l->count; at++)
        {
                int y, height;

                ui_list_row_span(ui, l, at, &y, &height);
                if ((l->starts[at] & UI_LIST_HEADED) && y > l->y && y - ui_dev(ui, UI_SECTION) < l->y + l->h)
                {
                        struct ui_row row;

                        ui_list_describe(l, l->id[at], &row);
                        ui_kit_text_flipped(ui, l->x + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, y - ui_dev(ui, UI_SECTION) + ui_dev(ui, 8), UI_TITLE_SMALL),
                                            (string_address)row.header, UI_TITLE_SMALL, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0, l->x, l->w);
                }
                if (y + height > l->y && y < l->y + l->h)
                        ui_list_row(ui, l, l->id[at], y, height);
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

/*
        A mouse's press held on a list's row shows the row pressed while the pointer is on the row the press went down on, and
        not once it slides off; row is the row under the pointer (-1 for none). True when what shows pressed changed.
*/
static b32 ui_list_press_slide(const struct ui address_to ui, struct ui_list address_to l, const struct ui_press address_to press, int row)
{
        int on = row >= 0 && row == press->target ? row : -1;

        (void)ui;
        if (!press->down || on == l->down)
                return false;
        ui_list_let_go(ui, l);
        if (on >= 0)
                l->down = on;
        return true;
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
                if (row.kind == UI_ROW_STEP && ui_point(x, l->x, l->w) < l->x + l->w - ui_dev(ui, UI_INSET + 40))
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

/* ---- a finger on a page ---- */
/*
        A finger on a page: ui_finger_down, ui_finger_move and ui_finger_up (a target, a drag, a throw into the page's scroll
        region). A finger grab is ui_press_begin with touch set. The text view keeps its own grab (ui_text_finger_*): a
        duplicate, on the list in ui-audit-kit-2026-11-04.
*/

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
                // The drag follows from where the finger is when the slop is passed, so the list does not jump by the slop.
                ui_drag_start(&f->drag, y, (positive)ui_kit.now);
                changed = true;
        }
        if (f->press.dragged && f->region)
        {
                // A pull past an end gives as the hand's does, from its first pixel, whatever the finger's speed: a stretch that
                // came and went with the speed popped in and out under a finger crossing the end.
                int moved = ui_drag_follow(&f->drag, y, (positive)ui_kit.now);

                changed |= ui_scroll_drag(f->region, -moved);
        }
        return changed;
}

// The finger lifted: what it pressed, done, or -1.
static int ui_finger_up(struct ui_finger address_to f, b32 cancelled)
{
        if (f->press.dragged)
        {
                positive now = (positive)ui_kit.now; // the event's own time (ui_event_context)
                // The throw's speed is the finger's velocity over its last reports (ui_drag_fling), down the screen positive; the
                // region moves the other way (ui_finger_move).
                long speed = ui_drag_velocity(&f->drag, now);
                b32 thrown = speed >= UI_DRAG_THROW || speed <= -UI_DRAG_THROW;

                if (f->region)
                        ui_scroll_let_go(f->region, thrown, -speed);
        }
        return f->press.down ? ui_press_end(&f->press, cancelled) : -1;
}

// Whether the finger is down on the widget it went down on, and whether it has moved past the slop as a drag (ui_finger_move).
static b32 ui_finger_held(const struct ui_finger address_to f)
{
        return f->press.down;
}

static b32 ui_finger_dragged(const struct ui_finger address_to f)
{
        return f->press.dragged;
}

/* ---- a page ---- */
/*
        A page: ui_page_layout and ui_page_draw (a title, a search field, a column of groups, a body and a status line), the
        column's ui_page_column_* (hit, reveal, key, press, bar) and ui_page_wheel. ui_app_run is the main loop: it calls
        ui_event_context once for each event and forwards it. ui_argument reads a program's argument. An app declares its
        groups and rows; the page lays out, scrolls and draws them.
*/

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
        int seam;                          // the middle of the column's scroll bar, device px: the column's rows end and the body's begin there
        struct ui_control search;          // a bare field at the title's right, when the app gave it a placeholder
        struct ui_control column;          // the column: the kit's tabs on end
        struct ui_scroll column_scroll;
        char names[UI_ITEMS][UI_WORD];
        int pointer_x, pointer_y;          // the last pointer position (ui_page_track)
        struct ui_press press;             // a mouse's press on the page: what it went down on, done on its release (ui_page_pointer)
        struct ui_finger finger;           // a finger on the page: its press and the region it drags
        b32 card;                          // the body is a card, not a list: no row is hit and a press on it is its one target
        int chosen;                        // the column's group chosen (the body's rows are its rows)
        b32 pane;                          // where the keys are: 0 the column of groups, 1 the body
        // The app's: what scrolls in the body (its list's region, or a page of text's), and where a change is drawn again
        // (rows of the window, device pixels).
        struct ui_scroll address_to body;
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
        p->seam = p->column_on ? p->column_scroll.x + ui_dev(ui, UI_SCROLL_WIDTH) / 2 : 0;
        // Reading from the right it is the same page turned over the window's middle: the column at the right edge, the body to its left.
        if (ui_rtl())
        {
                int left = p->left;

                p->left = width - p->right, p->right = width - left;
                p->column_scroll.x = width - p->column_scroll.x - ui_dev(ui, UI_SCROLL_WIDTH);
                p->seam = p->column_on ? width - p->seam : 0;
        }
        // The column scrolls as a list does: its ends squash under a pull or a wheel turn (ui_scroll_span, the same for every region).
        p->column_scroll.bounds = 1, p->column_scroll.scale = ui->scale;
        p->column_scroll.y = p->top, p->column_scroll.height = p->bottom - p->top, p->column_scroll.view = p->bottom - p->top;
        p->column_scroll.content = p->column.count * ui_dev(ui, UI_ROW);
        ui_scroll_clamp(&p->column_scroll);
}

/*
        The body's rows run from the seam to the window's own edge, flush: the column's rows end at the middle of the column's scroll
        track and these begin there, so the track lies across the join of two surfaces and not in a margin of either, and the list's
        own track is drawn over its rows' last stretch, which is blank. The list's box (ui_list_place's) is unchanged: its words and
        controls keep the page's margins at both ends, and nothing sits under a track. A page with no column starts at the window's edge.
*/
static fn ui_page_rows(const struct ui_page address_to p, struct ui_list address_to l)
{
        l->row_x = p->column_on && !ui_rtl() ? p->seam : 0;
        l->row_w = p->column_on && ui_rtl() ? p->seam : p->width - l->row_x;
}

// A page's bottom edge moved from the layout's (an app's body runs to the window's edge): the column's view follows it, so its
// scroll range ends where its drawing does.
static fn ui_page_set_bottom(struct ui_page address_to p, int bottom)
{
        p->bottom = bottom;
        p->column_scroll.height = p->column_scroll.view = p->bottom - p->top;
        ui_scroll_clamp(&p->column_scroll);
}

// The group under a point of the window, or -1.
// Whether a device x of the window is in the column's side of the page: left of the seam, or right of it reading from the right.
static inline b32 ui_page_in_column(const struct ui_page address_to p, int x)
{
        return ui_rtl() ? x >= p->seam && x < p->width : x >= 0 && x < p->seam;
}

static int ui_page_column_hit(const struct ui address_to ui, const struct ui_page address_to p, int x, int y)
{
        int group = (y - p->top + p->column_scroll.scroll) / ui_dev(ui, UI_ROW);

        if (!p->column_on || y < p->top || y >= p->bottom || !ui_page_in_column(p, x) || group >= p->column.count)
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
static fn ui_page_column(struct ui address_to ui, struct ui_page address_to p, b32 keys, b32 disabled)
{
        struct ui_control address_to c = &p->column;
        int chosen = p->chosen;

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
        c->x = ui_rtl() ? p->width * 100 / ui->scale - UI_PAD - p->column_w * 100 / ui->scale : UI_PAD;
        c->span = &p->column_scroll;
        c->y = (p->top - p->column_scroll.scroll) * 100 / ui->scale;
        c->w = p->column_w * 100 / ui->scale;
        c->h = UI_ROW;
        // A group is a whole row, flush with the window's edge, to the middle of the bar's track: logical px rounded up, the edge the list's rows
        // begin at (ui_list_across_logical), so the two lie edge to edge at every scale.
        c->row_x = 0, c->row_w = ((ui_rtl() ? p->width - p->seam : p->seam) * 100 + ui->scale - 1) / ui->scale;
        if (ui_rtl())
                c->row_x = p->width * 100 / ui->scale - c->row_w;
        c->disabled = disabled;
        c->focused = keys;
        c->item = ui_control_hit(ui, c, p->pointer_x, p->pointer_y);
        c->hover = c->item >= 0;
        if (chosen != c->at)
                ui_tabs_choose(ui, c, chosen); // placed at its first frame, moving after
        ui_control_draw(ui, c);
}

// The column's bar, drawn last of the page (after the body's rows): its track lies across the seam, so the body's rows (their hover,
// their chosen band, their ripple) are under its right half, and drawn after it they would be over it.
static fn ui_page_column_bar(struct ui address_to ui, struct ui_page address_to p)
{
        if (!p->column_on)
                return;
        ui->top = p->top;
        ui->bottom = p->bottom;
        ui_scroll_draw(ui, &p->column_scroll);
}

/*
        The page's regions: the column at the left of the gutter's middle, and the body. A region that moved is drawn again;
        the column and the body are the body's band. A choice's menu is a popup of its own, with the wheel and the keys.
*/
static struct ui_scroll address_to ui_page_region(const struct ui address_to ui, struct ui_page address_to p, int x, int y)
{
        if (p->column_on && ui_page_in_column(p, x))
                return &p->column_scroll;
        return p->body;
}

static fn ui_page_moved(const struct ui address_to ui, struct ui_page address_to p, const struct ui_scroll address_to region)
{
        if (!p->damaged || !region)
                return;
        p->damaged(p->top - ui_dev(ui, UI_ROW), p->bottom + ui_dev(ui, UI_ROW));
}

// The wheel on the page: it turns the region under the pointer.
static fn ui_page_wheel(const struct ui address_to ui, struct ui_page address_to p, const struct window_event address_to event)
{
        struct ui_scroll address_to region = ui_page_region(ui, p, p->pointer_x, p->pointer_y);

        ui_drag_stop(&p->finger.drag);

        if (region && ui_wheel_turn(ui, region, event))
                ui_page_moved(ui, p, region);
}

/*
        A mouse's motion, press or release on the page's scrollbars (the body's and the column's): the thumb lights under the
        pointer, is dragged, and a press on the track pages. True when it was a bar's.
*/
static b32 ui_page_bars(const struct ui address_to ui, struct ui_page address_to p, int type, b32 down)
{
        struct ui_scroll address_to bars[3] = {null, p->body, !p->column_on ? null : &p->column_scroll};
        b32 taken = false;

        for (int k = 0; k < 3; k++)
        {
                b32 lit, held;
                int scroll;

                if (!bars[k])
                        continue;
                lit = bars[k]->hover;
                held = bars[k]->held;
                scroll = ui_scroll_pointer(ui, bars[k], type, p->pointer_x, p->pointer_y, down);
                if (scroll >= 0)
                {
                        taken = true;
                        // A thumb stretching its region past an end keeps the stretch; its release lets it go, as a finger's does.
                        if (!bars[k]->dragging)
                                ui_scroll_set(bars[k], scroll);
                }
                // Only a thumb that was held lets go: a finger dragging the content has the region dragging too, and its own release throws it.
                if (type == WINDOW_EVENT_BUTTON && !down && held && bars[k]->dragging)
                        ui_scroll_let_go(bars[k], 0, 0);
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
        A page's pointer, for a mouse and a finger alike. A press is numbered by what it went down on: a row by its list id (rows
        are below UI_PAGE_GROUP), a group of the column by UI_PAGE_GROUP plus the group, the card (p->card) by UI_PAGE_CARD. What
        a press shows pressed as it goes down is the kit's: a row's state layer, a group's ripple, the card nothing. A mouse's
        press is done on its release where it went down, if the release is on that target too; a press on nothing is done at
        once. A finger's is done as it lifts, where it went down, if it did not move off the target; past the slop it drags the
        region it went down on, which follows it, and is thrown on the lift if it was moving. done is the app's action for a press
        (it reads the page's pointer), and ui_kit_touch says whether the press is a finger's (what a menu it opens is).
*/
#define UI_PAGE_GROUP 0x1000
#define UI_PAGE_CARD 0x2000

// The last pointer position, from a motion (the pointer is the page's: a button's event does not carry it).
static fn ui_page_track(struct ui_page address_to p, const struct window_event address_to event)
{
        if (event->type == WINDOW_EVENT_MOTION)
                p->pointer_x = event->a, p->pointer_y = event->b;
}

// A key or a turn of the wheel takes a finger's drag from the page.
static fn ui_page_stop(struct ui_page address_to p)
{
        ui_drag_stop(&p->finger.drag);
}

// The page's body changed in rows: the app's damage hook, over the page's band.
static fn ui_page_changed(struct ui_page address_to p)
{
        if (p->damaged)
                p->damaged(p->top, p->bottom);
}

// The target under a point of the page: the card, a group of the column, or a row of the list (-1 none).
static int ui_page_target(const struct ui address_to ui, const struct ui_page address_to p, const struct ui_list address_to l, int x, int y)
{
        int at;

        if (p->card)
                return UI_PAGE_CARD;
        if ((at = ui_page_column_hit(ui, p, x, y)) >= 0)
                return UI_PAGE_GROUP + at;
        return ui_list_hit(ui, l, x, y);
}

// What a target shows pressed as it goes down, from the point it went down at.
static fn ui_page_press_show(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l, int target, int x, int y)
{
        if (target < 0)
                return;
        if (target < UI_PAGE_GROUP)
                l->down = target;
        else if (target < UI_PAGE_CARD)
                ui_page_column_press(ui, p, target - UI_PAGE_GROUP, x, y);
}

// A press done where it went down: the action runs at that point, the list lets go and the column's ripple goes out; the
// pointer is put back where the release was.
static fn ui_page_press_done(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l, void (*done)(void),
                             int press_x, int press_y, int x, int y)
{
        p->pointer_x = press_x, p->pointer_y = press_y;
        done();
        ui_list_let_go(ui, l);
        ui_kit_ripples_exit(&p->column);
        p->pointer_x = x, p->pointer_y = y;
}

// A mouse's press began: its target shows pressed. False when it went down on nothing (the action is then done at once).
static b32 ui_page_press_begin(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l)
{
        int x = p->pointer_x, y = p->pointer_y, target = ui_page_target(ui, p, l, x, y);

        if (target < 0)
                return false;
        ui_press_begin(&p->press, x, y, target, false);
        ui_page_press_show(ui, p, l, target, x, y);
        ui_page_changed(p);
        return true;
}

// The mouse moved with its press held: a row is shown pressed while the pointer is on the row the press went down on.
static fn ui_page_press_slide(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l, int x, int y)
{
        if (p->press.target < UI_PAGE_GROUP && ui_list_press_slide(ui, l, &p->press, ui_page_target(ui, p, l, x, y)))
                ui_page_changed(p);
}

// A mouse's release with its press held: done where it went down, if the release is on that target too; one taken back
// (WINDOW_KEY_CANCEL) is no click.
static fn ui_page_press_up(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l, void (*done)(void),
                           b32 cancelled)
{
        int clicked = ui_press_end(&p->press, cancelled), x = p->pointer_x, y = p->pointer_y;

        ui_list_let_go(ui, l);
        ui_page_changed(p);
        if (clicked < 0 || clicked != ui_page_target(ui, p, l, x, y))
                return;
        ui_page_press_done(ui, p, l, done, p->press.x, p->press.y, x, y);
}

// A finger's motion: its press becomes a drag past the slop, and the region it drags follows it.
static fn ui_page_finger_move(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l, int x, int y)
{
        b32 dragged = p->finger.press.dragged;

        if (ui_finger_move(ui, &p->finger, x, y))
        {
                if (!dragged && p->finger.press.dragged)
                        ui_list_let_go(ui, l);
                ui_page_moved(ui, p, p->finger.region);
        }
}

// A finger's button: down, its target shows pressed and nothing is done yet; up, the press is done where it went down if it
// did not move off its target, and the region is thrown if it was moving.
static fn ui_page_finger_button(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l,
                                const struct window_event address_to event, void (*done)(void))
{
        int x = p->pointer_x, y = p->pointer_y;

        if (event->flags & WINDOW_KEY_DOWN)
        {
                int target = ui_page_target(ui, p, l, x, y);

                ui_finger_down(&p->finger, x, y, target, ui_page_region(ui, p, x, y));
                ui_page_press_show(ui, p, l, target, x, y);
                ui_page_changed(p);
                return;
        }
        {
                int clicked, release_x = x, release_y = y;

                x = p->finger.press.x, y = p->finger.press.y;
                clicked = ui_finger_up(&p->finger, (event->flags & WINDOW_KEY_CANCEL) != 0);
                ui_list_let_go(ui, l);
                ui_page_changed(p);
                if (clicked >= 0 && clicked == ui_page_target(ui, p, l, x, y))
                        ui_page_press_done(ui, p, l, done, x, y, release_x, release_y);
        }
}

/*
        The page's pointer: a motion or a button of the mouse or a finger, forwarded here by the app (its map, bars and fields
        have had the first say). done is the app's press action.
*/
static fn ui_page_pointer(const struct ui address_to ui, struct ui_page address_to p, struct ui_list address_to l,
                          const struct window_event address_to event, void (*done)(void))
{
        // The event's own touch flag, as ui_event_context sets it for the loop's events (a menu the press opens is a finger's).
        ui_kit_touch = (event->flags & WINDOW_KEY_TOUCH) != 0;
        if (event->type == WINDOW_EVENT_MOTION)
        {
                if (event->flags & WINDOW_KEY_TOUCH)
                {
                        ui_page_finger_move(ui, p, l, event->a, event->b);
                        return;
                }
                if (p->press.down)
                        ui_page_press_slide(ui, p, l, event->a, event->b);
                // The rows and the column: what is under the pointer is lit (a card has no rows).
                if (!p->card)
                {
                        ui_list_pointer(ui, l, WINDOW_EVENT_MOTION, event->a, event->b, false);
                        if (ui_control_hit(ui, &p->column, event->a, event->b) != p->column.item)
                                ui_page_changed(p);
                }
                return;
        }
        if (event->type != WINDOW_EVENT_BUTTON || event->a != UI_POINTER_LEFT)
                return;
        // The release: the column's ripple and the row's go out (a finger's as a mouse's).
        if (!(event->flags & WINDOW_KEY_DOWN))
        {
                ui_kit_ripples_exit(&p->column);
                ui_list_pointer(ui, l, WINDOW_EVENT_BUTTON, p->pointer_x, p->pointer_y, false);
                ui_page_changed(p);
                if (!(event->flags & WINDOW_KEY_TOUCH) && p->press.down)
                        ui_page_press_up(ui, p, l, done, (event->flags & WINDOW_KEY_CANCEL) != 0);
        }
        if (event->flags & WINDOW_KEY_TOUCH)
        {
                ui_page_finger_button(ui, p, l, event, done);
                return;
        }
        if (event->flags & WINDOW_KEY_DOWN)
        {
                ui_drag_stop(&p->finger.drag);
                if (!ui_page_press_begin(ui, p, l))
                        done();
        }
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
                ui_kit_text_flipped(ui, left, ui_kit_baseline(ui, ui_dev(ui, 8) + (p->top - ui_dev(ui, 8) - ui_dev(ui, 28)) / 2, UI_TITLE_LARGE),
                                    (string_address)p->title, UI_TITLE_LARGE, ui_role(UI_ON_SURFACE), 0, 0, p->width);
        if (p->search.items[0])
        {
                struct ui_control address_to c = &p->search;

                if (c->kind != UI_INPUT)
                {
                        c->kind = UI_INPUT, c->variant = UI_FIELD_BARE;
                        ui_control_start(c);
                }
                c->w = UI_SEARCH_W, c->h = UI_FIELD_H;
                c->x = ui_mirror((p->width - pad) * 100 / ui->scale - c->w - UI_SEARCH_RIGHT, c->w, 0, p->width * 100 / ui->scale), c->y = UI_SEARCH_Y;
                c->focused = keys;
                // Its own clip: the title band. A field drawn past the band's edge was cut where the list below repaints.
                ui->bottom = p->top;
                ui_control_draw(ui, c);
                ui->bottom = p->height;
        }
        ui->top = p->bottom;
        if (p->status && p->status[0])
        {
                unsigned int quiet = ui_role(UI_ON_SURFACE_VARIANT);
                positive length = string_length((string_address)p->status), cut = 0;
                char first[256];

                if (ui_kit_measure(ui, (string_address)p->status, UI_LABEL_MEDIUM) <= room || length >= sizeof(first))
                {
                        ui_kit_text_flipped(ui, left, ui_kit_baseline(ui, p->bottom + (foot - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM), (string_address)p->status,
                                            UI_LABEL_MEDIUM, quiet, 0, 0, p->width);
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
                ui_kit_text_flipped(ui, left, ui_kit_baseline(ui, p->bottom + foot / 2 - ui_dev(ui, 16), UI_LABEL_MEDIUM), (string_address)first,
                                    UI_LABEL_MEDIUM, quiet, 0, 0, p->width);
                ui_kit_text_flipped(ui, left, ui_kit_baseline(ui, p->bottom + foot / 2, UI_LABEL_MEDIUM), (string_address)p->status + cut + (cut < length),
                                    UI_LABEL_MEDIUM, quiet, 0, 0, p->width);
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
                int y = top + at * height, drawn, span, shift;

                // A line stretched by a pull is drawn in its stretched box, its text at the middle of that box.
                ui_scroll_span(s, y, height, &drawn, &span);
                if (drawn + span <= ui->top || drawn >= ui->bottom)
                        continue;
                shift = drawn + span / 2 - (y + height / 2);
                ui_kit_text_flipped(ui, left + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, y + shift, UI_BODY_MEDIUM), (string_address)line(at, text, sizeof(text)),
                                    UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
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
        The two buffers of an app's window: the compositor reads one while the app paints the other. The kit keeps what each
        lacks, the rows that changed since it was last drawn (the frame before, or the one before that for the other) and the
        rows the compositor spoiled in it, and draws only those, or the whole picture when a buffer lacks all of it. Rows are
        device pixels, [top, bottom); shown is the buffer on the screen, -1 when none is yet. The app says what changed
        (ui_buffers_all, ui_buffers_rows) and calls ui_buffers_present for its picture.
*/
struct ui_buffers
{
        int shown;
        int change_top, change_bottom;         // the rows changed since the last picture (empty when bottom <= top)
        b32 change_all;                        // or all of them
        int stale_top[2], stale_bottom[2];     // for each buffer, the rows it lacks since it was last drawn
        b32 stale_all[2];                      // or all of them
};

// Rows [from, to) added to the rows [top, bottom) (empty when bottom <= top): the union, the empty one none.
static void ui_rows_add(int address_to top, int address_to bottom, int from, int to)
{
        if (to <= from)
                return;
        if (*bottom <= *top)
        {
                *top = from;
                *bottom = to;
                return;
        }
        *top = from < *top ? from : *top;
        *bottom = to > *bottom ? to : *bottom;
}

// Both buffers lack the whole picture: the next one is drawn whole.
static fn ui_buffers_all(struct ui_buffers address_to b)
{
        b->change_all = true;
}

// Rows [top, bottom) of the picture may have changed, and no others: the next picture draws them.
static fn ui_buffers_rows(struct ui_buffers address_to b, int top, int bottom)
{
        ui_rows_add(&b->change_top, &b->change_bottom, top, bottom);
}

// A window's start: nothing shown, and both buffers lack the whole picture.
static fn ui_buffers_start(struct ui_buffers address_to b)
{
        b->shown = -1;
        b->change_all = b->stale_all[0] = b->stale_all[1] = true;
}

/*
        One picture into the buffer the compositor is not reading: paint draws its rows [top, bottom), within height. False
        while the compositor has not taken the last one (the picture is due again). True once it is done, even when no row
        changed and nothing was shown: showing the picture again is a knock, a take and a present for no pixel.
*/
static b32 ui_buffers_present(struct ui_buffers address_to b, const struct window_row address_to own, int width, int height,
                              void (*paint)(unsigned int address_to pixels, int top, int bottom))
{
        struct windows address_to table = own->table;
        unsigned int row = own->row;
        int slot = b->shown < 0 ? 0 : 1 - b->shown, other = 1 - slot, spoiled_x, spoiled_y, spoiled_width, spoiled_height;
        int spoiled_top = 0, spoiled_bottom = 0, drawn_top = b->stale_top[slot], drawn_bottom = b->stale_bottom[slot];
        b32 whole = b->stale_all[slot] || b->change_all;

        if (b->shown >= 0 && !windows_ready(own))
                return false;
        if (windows_spoiled(own, (unsigned int)slot, &spoiled_x, &spoiled_y, &spoiled_width, &spoiled_height))
                spoiled_top = spoiled_y, spoiled_bottom = spoiled_y + spoiled_height;
        ui_rows_add(&drawn_top, &drawn_bottom, b->change_top, b->change_bottom);
        ui_rows_add(&drawn_top, &drawn_bottom, spoiled_top, spoiled_bottom);
        if (whole)
                drawn_top = 0, drawn_bottom = height;
        drawn_top = drawn_top < 0 ? 0 : drawn_top;
        drawn_bottom = drawn_bottom > height ? height : drawn_bottom;
        if (drawn_bottom > drawn_top)
                paint(windows_buffer(table, row, (unsigned int)slot), drawn_top, drawn_bottom);
        else
                drawn_top = drawn_bottom = 0;
        // The buffer is the picture now; the other lacks what changed since it was last drawn, which is what changed now.
        b->stale_top[slot] = b->stale_bottom[slot] = 0;
        b->stale_all[slot] = false;
        b->stale_top[other] = b->change_top;
        b->stale_bottom[other] = b->change_bottom;
        b->stale_all[other] = b->change_all;
        b->change_top = b->change_bottom = 0;
        b->change_all = false;
        if (!whole && drawn_bottom <= drawn_top && b->shown >= 0)
                return true;
        table->width[row] = (unsigned int)width;
        table->height[row] = (unsigned int)height;
        if (whole)
                table->damage_width[row] = 0;
        else
        {
                table->damage_x[row] = 0;
                table->damage_y[row] = drawn_top;
                table->damage_width[row] = (unsigned int)width;
                table->damage_height[row] = drawn_bottom - drawn_top;
        }
        windows_show(own, (unsigned int)slot);
        b->shown = slot;
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
        b32 own_title;                  // the app names its window itself as it runs (the terminal): the kit sets the title once, at the start
        b32 dismiss;                    // a popup: the loop ends when the keys go elsewhere after they have been its (had_keys is the kit's)
        b32 had_keys;
};

// The keys came or went: whether the app goes on. A popup (dismiss) that has had the keys and loses them is done, as is an app whose focus hook says so.
static b32 ui_app_keys(struct ui_app address_to app, b32 focused)
{
        app->had_keys |= focused;
        if (app->dismiss && app->had_keys && !focused)
                return false;
        return !app->focus || app->focus(focused);
}

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
        app->ui->scale = table->window_scale[row] >= 25 && table->window_scale[row] <= 800 ? (int)table->window_scale[row]
                         : table->scale >= 25 && table->scale <= 800 ? (int)table->scale : 100;
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
        ui_kit.wheel_off = table->wheel_off ? 1 : 0, ui_kit_caret_take(table), ui_kit_language_take(table), ui_kit_text_take(table);
        ui_kit_theme(ui_kit.themed ? table->theme : UI_THEME_DARK, ui_kit.themed ? table->accent : UI_ACCENT_STANDARD, table->motion);
        string_copy_bounded((string_address)table->title[row], (string_address)language_of((const char address_to)app->title), WINDOWS_TITLE);
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
                {
                        ui_event_context(&event);
                        // A scroll over the window closes its open popup before the app has the scroll (ui_popup_scroll).
                        if (app->popup)
                        {
                                int gone = ui_popup_scroll(app->popup, &event);

                                if (gone != UI_MENU_NOTHING && app->chose)
                                        app->chose(gone);
                        }
                        if (!app->event(&event))
                                running = false;
                }
                if (app->popup && app->popup->open)
                {
                        int chosen = ui_popup_pump(app->popup);

                        if (chosen != UI_MENU_NOTHING && app->chose)
                                app->chose(chosen);
                        // A menu's ripple is moving: its next picture is due a display period on.
                        if (app->popup->open && app->popup->menu.moving)
                                timeout = 16666667;
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
                        if (!ui_app_keys(app, *app->focused))
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
                        unsigned int was = language.code;

                        ui_kit.wheel_off = table->wheel_off ? 1 : 0, ui_kit_caret_take(table), ui_kit_language_take(table), ui_kit_text_take(table);
                        // The title is a text the kit copied for the app, so the kit sets it again when the language changed (a theme's
                        // knock leaves it, and so does an app that names its window itself).
                        if (language.code != was && !app->own_title)
                                string_copy_bounded((string_address)table->title[row], (string_address)language_of((const char address_to)app->title), WINDOWS_TITLE);
                        ui_kit_theme(table->theme, table->accent, table->motion);
                        ui_kit_inks(app->ui);
                        ui_app_changed(app);
                }
                // The screen the window is on draws it at that screen's scale (the compositor counts it into the table): the
                // picture is laid out again at the new scale, at the size it has, as a resize lays it out.
                if (table->window_scale[row] >= 25 && table->window_scale[row] <= 800 &&
                    (int)table->window_scale[row] != app->ui->scale)
                {
                        app->ui->scale = (int)table->window_scale[row];
                        ui_kit_settle();
                        ui_app_changed(app);
                        if (table->width[row] && table->height[row])
                                app->size(table->width[row], table->height[row]);
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

/*
        A window that opens beside a panel's button (the launcher from the panel with -a, a menu from a cell of the panel): the
        rectangle the compositor wrote in the window table when the button was pressed, the edge the panel is along and the screen
        it is on. ui_anchor_take reads it once, when the app was asked to open beside (asked); a table with no press in it, or a
        screen or an edge that is none, leaves it unanchored, in the middle. ui_anchor_place finds where a width by height window
        goes beside the button (above a bottom panel, below a top one, beside a left or right one, the other way kept where the
        button is), kept inside the screen; ui_anchor_set asks the compositor for that place, with the picture it is for.
*/
struct ui_anchor
{
        b32 anchored;
        int button[4], screen[4];  // the panel's button (x, y, width, height) and the screen's rectangle
        unsigned int edge, number; // the edge the panel is along, and the screen's number
};

static void ui_anchor_place(unsigned int edge, const int address_to button, const int address_to screen, int width, int height,
                            int address_to x, int address_to y)
{
        *x = button[0], *y = button[1];
        if (edge == TASKBAR_BOTTOM)
                *y = button[1] - height;
        else if (edge == TASKBAR_TOP)
                *y = button[1] + button[3];
        else if (edge == TASKBAR_LEFT)
                *x = button[0] + button[2];
        else
                *x = button[0] - width;
        *x = *x > screen[0] + screen[2] - width ? screen[0] + screen[2] - width : *x;
        *y = *y > screen[1] + screen[3] - height ? screen[1] + screen[3] - height : *y;
        *x = *x < screen[0] ? screen[0] : *x;
        *y = *y < screen[1] ? screen[1] : *y;
}

static void ui_anchor_take(struct ui_anchor address_to anchor, const struct windows address_to table, b32 asked)
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);

        if (!asked || !table->button_w || !table->button_h || table->button_edge > TASKBAR_RIGHT ||
            table->button_screen >= (count ? count : 1))
                return;
        anchor->button[0] = table->button_x, anchor->button[1] = table->button_y;
        anchor->button[2] = (int)table->button_w, anchor->button[3] = (int)table->button_h;
        anchor->edge = table->button_edge, anchor->number = table->button_screen;
        windows_output(table, anchor->number, &anchor->screen[0], &anchor->screen[1], &anchor->screen[2], &anchor->screen[3]);
        anchor->anchored = true;
}

// The place asked of the compositor for the window in row, as a width by height window beside the button (ui_anchor_place).
static void ui_anchor_set(const struct ui_anchor address_to anchor, struct windows address_to table, unsigned int row, int width, int height)
{
        struct window_place place;
        int x, y;

        memory_zero(&place, sizeof(place));
        ui_anchor_place(anchor->edge, anchor->button, anchor->screen, width, height, &x, &y);
        place.how = WINDOW_PLACE_AT | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE;
        place.x = x - anchor->screen[0], place.y = y - anchor->screen[1];
        place.width = (unsigned int)width, place.height = (unsigned int)height;
        place.screen = anchor->number;
        windows_place_set(table, row, &place);
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
        Text: a field (ui_field_open and ui_field_event take every typed input of Canvas; ui_field_digits for numbers), a text
        area and the code view (ui_text_open, ui_text_close, ui_text_set, ui_text_event, ui_text_draw), the carets (ui_caret_*),
        the text menu (ui_text_menu_*) and the overlay bars. Typing and selection live here (check_no_hand_typing); text.c holds
        the documents.
*/

/*
        One widget for every place text is typed: a field (one line, the kit's UI_INPUT draws the well and this draws
        what is in it), a text area (lines, wrapped, in the UI's face) and the code view (lines in the mono, a gutter of
        line numbers, the tokenizer's colours, soft wrap on or off, zoom). The document, its history and selections are
        text.c's; this is the hand and the pixels.

        The hand: a click puts the caret, a drag selects, Shift with a click extends, a double click takes a word and a
        drag after it whole words, a triple click a line and whole lines, Alt with a click adds a caret, Alt and Shift
        with a drag selects a box of columns, the middle button pastes PRIMARY, the right button asks the app for a
        context menu (ui_text_asks), a finger's tap is a caret and a finger's drag scrolls (thrown, it slows as the
        kit's lists do), a finger's long press or double tap selects a word and shows handles and an actions bar (the
        finger's selection, below), and the wheel scrolls (Ctrl and the wheel zoom the code view). The keys are the keymap's
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
#define UI_TEXT_COMMIT 256u   // a press outside a field that has the keys: the field's plain value is kept (the app commits it)

/*
        What a finger on a text widget holds, from its press to its lift: the text (a tap, or a drag that scrolls it), an
        end of the selection by its handle (the drag moves that end), an item of the actions bar (the lift does it), or a
        selection it is making by words (a long press, or a double tap, and the finger going on). UI_TEXT_BAR_ITEMS is the
        most the bar has: Cut, Copy, Paste and Select All.
*/
#define UI_TEXT_FINGER_TEXT 1
#define UI_TEXT_FINGER_HANDLE 2
#define UI_TEXT_FINGER_BAR 3
#define UI_TEXT_FINGER_SELECT 4
#define UI_TEXT_BAR_ITEMS 4

/*
        An edge scroll (ui_edge_steps): a drag held past an edge scrolls what it is in, a step at a time as the time passes. past is
        how far past the edge the drag is in device pixels (negative past the top or left, 0 inside, which stops it), at when the last
        step was (0 before the first), and owed the scroll that step owes and has not made, in thousandths of a device pixel. The text
        widget and the terminal's selection keep one each.
*/
struct ui_edge
{
        int past;
        positive at;
        long owed;
};

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
        int laid_width, laid_height, laid_line; // the box and line height the last draw had: a change reveals the caret
        p64 top_line;
        int top_row;
        b64 scroll_x;
        int clicks;
        struct ui_click click;                  // the kit's click count (ui_click_count): a double click and a triple
        b32 dragging, drag_box;
        b32 holding;                            // a finger's long press holds the selection (ui_text_hold) until its lift
        struct ui_press tap;                    // a finger's press: a lift with no drag is a tap (target 0 the text, 1 the selection, 2 a handle)
        p64 tap_at;                             // the offset a tap went down on
        int finger;                             // what a finger on it holds (UI_TEXT_FINGER_*), 0 none
        b32 handles;                            // a touch selection: its two ends show handles a finger drags (ui_text_overlay)
        int grab_dx, grab_dy;                   // a handle's finger holds it where it took it: the end is at the finger plus this
        b32 bar;                                // the actions bar is wanted: a touch selection's Cut, Copy, Paste and Select All
        b32 bar_want, bar_up;                   // whether the last overlay meant to show it, and whether it drew it
        positive bar_since, quiet_at;           // when it was first drawn (its fade); and when the view last moved, which it waits out
        int bar_x, bar_y, bar_w, bar_h;         // where it was drawn, device pixels: what a finger's press can hit
        int bar_count, bar_down;                // its items, and the one a finger is on (item + 1, 0 none)
        int bar_at[UI_TEXT_BAR_ITEMS + 1];      // each item's left edge from bar_x, then the bar's right
        int bar_id[UI_TEXT_BAR_ITEMS];          // each item's UI_TEXT_MENU_* id
        int over_top, over_bottom;              // the rows its handles and bar covered: an app that draws in bands draws them again
        int drag_x, drag_y;                     // a selection drag's last point
        struct ui_edge edge;                    // the drag's edge: how far past the box it is, and its autoscroll (ui_text_autoscroll)
        int drag_kind;                          // 1 characters, 2 words, 3 lines
        p64 drag_start, drag_end;
        p64 box_line, box_column;
        struct ui_drag kinetic;
        int finger_left;                        // the finger's or throw's pixels not yet a line, up positive
        positive blink_at;                      // where the caret's phase restarted (ui_text_wake): a key or a move
        unsigned int caret_fade;                // how far the caret has faded: 0 solid, UI_CARET_FULL gone (0 new, so drawn)
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
        b32 cancels;                            // a command or a passphrase: a press outside cancels it (a masked field always does); a plain value commits
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
        return code >= '0' && code <= '9' ? null : WORDS(ui_field_digits);
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
        t->refused = t->most_said ? t->most_said : WORDS(ui_field_full);
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
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds(), t->caret_fade = 0;
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
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds(), t->caret_fade = 0;
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
        /*
                A line the font engine reorders (it has a run that reads from the right, or the window does, and a field's text
                follows its first strong letter): x[k] is where the caret is at boundary k, which is the cluster's edge on the side
                its direction ends at, left[k] and right[k] are the cluster's own place and level[k] its level, base is the paragraph's
                (1 reads from the right) and width the line's. A line the engine does not reorder has base 0 and x as it was.
        */
        b32 bidi;
        int base, width;
        int left[UI_TEXT_CLUSTERS], right[UI_TEXT_CLUSTERS];
        p8 level[UI_TEXT_CLUSTERS];
};

static const font_face address_to ui_text_face(const struct ui address_to ui, const struct ui_text address_to t)
{
        return t->mode == UI_TEXT_CODE && ui->mono ? ui->mono : ui->regular;
}

static p32 ui_text_em(const struct ui address_to ui, const struct ui_text address_to t)
{
        int size = t->size + t->zoom;

        size = size < 6 ? 6 : size > 72 ? 72 : size;
        return ui_em(ui, size);
}

// The flags a text widget's lines are laid out and drawn with: the first strong letter's direction, the window's where there is none.
static p32 ui_text_flags(const struct ui_text address_to t)
{
        return FONT_AUTO | (t->mode != UI_TEXT_CODE && ui_rtl() ? FONT_RTL : 0);
}

// Each cluster's place on a line the engine reorders (font_segments), and the place of the caret at each boundary between them.
static fn ui_text_bidi(struct ui address_to ui, const struct ui_text address_to t, struct ui_text_layout address_to l,
                       const font_face address_to const address_to faces, p32 em)
{
        static b32 from[UI_TEXT_CLUSTERS], to[UI_TEXT_CLUSTERS];
        p32 flags = ui_text_flags(t);
        positive count = l->count;

        if (!count)
        {
                // An empty line's caret is at its start: the box's right where the line reads from the right.
                l->base = font_base_level(l->shown, l->shown_length, flags);
                l->width = 0, l->x[0] = 0;
                return;
        }
        if (font_segments(ui->engine, faces, 2, l->shown, l->shown_length, em, flags, l->shown_at, count, from, to, l->level) != count)
                return; // a line too long for the engine to lay out as one keeps the pen's reckoning, left to right
        l->base = font_base_level(l->shown, l->shown_length, flags);
        l->width = (int)(font_extent(ui->engine, faces, 2, l->shown, l->shown_length, em, flags) >> 16);
        for (positive k = 0; k < count; k++)
                l->left[k] = (int)((from[k] + 0x8000) >> 16), l->right[k] = (int)((to[k] + 0x8000) >> 16);
        for (positive b = 0; b <= count; b++)
        {
                // The edge of the cluster before it where that cluster ends reading, or of the one after it where that one begins; where the two
                // run the same way they share one edge, and where they do not the one that runs the way the line does is the caret's.
                b32 before = b > 0, after = b < count, odd_before = before && (l->level[b - 1] & 1), odd_after = after && (l->level[b] & 1);
                int trailing = before ? (odd_before ? l->left[b - 1] : l->right[b - 1]) : 0, leading = after ? (odd_after ? l->right[b] : l->left[b]) : 0;

                if (!before)
                        l->x[b] = leading;
                else if (!after)
                        l->x[b] = trailing;
                else if (odd_before == odd_after)
                        l->x[b] = odd_before ? l->left[b - 1] : l->right[b - 1];
                else
                        l->x[b] = odd_before == (b32)(l->base & 1) ? trailing : leading;
        }
        l->bidi = 1;
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
        l->bidi = 0, l->base = 0, l->width = l->x[l->count];
        // A field and a page of text take their direction from their first strong letter, and from the window where there is none; a code
        // view's lines read from the left unless they have a letter that does not. Reordered, the line is the engine's, whole.
        if (t->mode == UI_TEXT_CODE ? !font_is_simple(l->shown, l->shown_length) : ui_rtl() || !font_is_simple(l->shown, l->shown_length))
                ui_text_bidi(ui, t, l, faces, em);
}

// Where a laid line's x = 0 is on the window: the box's left less the scroll, and for a line that reads from the right the box's right less
// the line's width, the scroll bringing the end of it into view (the scroll is a distance from the line's start either way).
static int ui_text_origin(const struct ui_text address_to t, const struct ui_text_layout address_to l)
{
        return l->base ? t->right - l->width + (int)t->scroll_x : t->left - (int)t->scroll_x;
}

// A distance along a line from its start edge: x reading from the left, and from its right end reading from the right.
static int ui_text_along(const struct ui_text_layout address_to l, int x)
{
        return l->base ? l->width - x : x;
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
        A wrapped line's rows. The line is cut where it would pass the box's width, at a blank when one is near, and each
        row after the first is indented as the line is (by its leading blanks, when they are under half the width). The draw,
        the hand (ui_text_hit) and the handles (ui_text_where) all cut it here, so they agree on where a row starts.
*/
static int ui_text_wrap_indent(const struct ui_text_layout address_to l, int room)
{
        int indent = 0;

        if (l->bidi)
                return 0;
        while (indent < (int)l->count && l->shown[l->shown_at[indent]] == ' ')
                indent++;
        return l->x[indent] < room / 2 ? l->x[indent] : 0;
}

// The cluster after the last of the row that starts at from: at most room wide, cut after the last blank in it when it is cut.
static p32 ui_text_wrap_to(const struct ui_text_layout address_to l, p32 from, int room, int indent)
{
        p32 to = from, blank = 0;
        int offset_x = l->x[from] - (from ? indent : 0);

        if (l->bidi)
                return l->count; // a line of reordered text is one row: its rows would each be reordered again
        while (to < l->count && l->x[to + 1] - offset_x <= room)
        {
                if (l->shown[l->shown_at[to]] == ' ')
                        blank = to + 1;
                to++;
        }
        if (to < l->count && blank > from)
                to = blank;
        to = to > from ? to : from + 1;
        return to > l->count ? l->count : to;
}

// Whether the widget has a fold closed: the rows are then not the lines from the top one on, a closed fold's lines being none.
static b32 ui_text_folds(const struct ui_text address_to t)
{
        for (positive r = 0; r < t->doc.range_count; r++)
                if (t->doc.ranges[r].kind == DOC_RANGE_FOLD)
                        return true;
        return false;
}

// How many rows a line takes: none hidden in a fold, one unless the widget wraps.
static int ui_text_line_rows(struct ui address_to ui, struct ui_text address_to t, p64 line)
{
        static struct ui_text_layout l;
        int rows = 0, room = t->right - t->left, indent;
        p32 from = 0, to;

        if (t->mode != UI_TEXT_FIELD && doc_hidden(&t->doc, line))
                return 0;
        if (!t->wrap || t->mode == UI_TEXT_FIELD)
                return 1;
        ui_text_layout_line(ui, t, line, &l);
        indent = ui_text_wrap_indent(&l, room);
        for (;;)
        {
                to = ui_text_wrap_to(&l, from, room, indent);
                rows++;
                if (to >= l.count)
                        return rows;
                from = to;
        }
}

/*
        One row of the view, a line as it is drawn: the document line, the clusters of its layout the row holds (from up to to),
        how far left of its place on the laid line the row is drawn (a wrapped line's later rows start at the line's indent),
        and whether it is the line's last row.
*/
struct ui_text_row
{
        p64 line;
        p32 from, to;
        int shift;
        b32 last;
};

/*
        The row at index (0 the top one, a row a line high) of the view, with its line laid out in l: the lines from the top
        one on, a line a closed fold hides left out and a wrapped line as many rows as it is cut in. False when the view has
        no row at index, and then row is the last it has.
*/
static b32 ui_text_nth_row(struct ui address_to ui, struct ui_text address_to t, int index, struct ui_text_layout address_to l,
                           struct ui_text_row address_to row)
{
        b32 field = t->mode == UI_TEXT_FIELD, wrapped = t->wrap && !field;
        p64 lines = doc_line_count(&t->doc), line = field ? 0 : t->top_line, use;
        int at = 0, room = t->right - t->left, indent;
        p32 from, to;

        line = line < lines ? line : lines - 1;
        use = line;
        for (; line < lines; line++)
        {
                if (!field && doc_hidden(&t->doc, line))
                        continue;
                use = line;
                if (!wrapped)
                {
                        if (at == index)
                                break;
                        at++;
                        continue;
                }
                ui_text_layout_line(ui, t, line, l);
                indent = ui_text_wrap_indent(l, room);
                for (from = 0;;)
                {
                        to = ui_text_wrap_to(l, from, room, indent);
                        row->line = line, row->from = from, row->to = to, row->last = to >= l->count;
                        row->shift = l->bidi ? 0 : l->x[from] - (from ? indent : 0);
                        if (at == index)
                                return true;
                        at++;
                        if (to >= l->count)
                                break;
                        from = to;
                }
        }
        if (wrapped)
                return false;
        ui_text_layout_line(ui, t, use, l);
        row->line = use, row->from = 0, row->to = l->count, row->shift = 0, row->last = true;
        return line < lines;
}

/*
        Where an offset is drawn: the x of its place and the top of its row, device pixels. 0 when it is in the box, -1 when it
        is before it (its row above the top, hidden in a fold, or a field's place left of the box) and 1 past it (its row
        below, or a field's place right of it). A row only partly in the box is past it.
*/
static int ui_text_where(struct ui address_to ui, struct ui_text address_to t, p64 offset, int address_to x, int address_to top)
{
        static struct ui_text_layout l;
        b32 field = t->mode == UI_TEXT_FIELD, wrapped = t->wrap && !field;
        p64 target = doc_line_of(&t->doc, offset), line = field ? 0 : t->top_line;
        int at = 0, rows = field ? 1 : ui_text_rows(t), room = t->right - t->left, indent, place;
        p32 k, from = 0, to;

        if (!field && (target < line || doc_hidden(&t->doc, target)))
                return -1;
        for (; line < target && at < rows; line++)
                at += ui_text_line_rows(ui, t, line);
        if (at >= rows)
                return 1;
        ui_text_layout_line(ui, t, target, &l);
        for (k = 0; k < l.count && l.offset[k] < offset; k++)
                ;
        place = l.x[k];
        if (wrapped && !l.bidi)
        {
                indent = ui_text_wrap_indent(&l, room);
                for (;;)
                {
                        to = ui_text_wrap_to(&l, from, room, indent);
                        if (k < to || to >= l.count)
                                break;
                        from = to;
                        at++;
                }
                place -= l.x[from] - (from ? indent : 0);
                if (at >= rows)
                        return 1;
        }
        *top = t->top + at * t->line_height;
        *x = ui_text_origin(t, &l) + place;
        return *x < t->left ? -1 : *x > t->right ? 1 : 0;
}

/*
        The view keeps the primary caret in sight: lines scroll so it is between the top and the bottom row, columns so
        it is inside the text box (a field scrolls sideways only). Where rows are not lines (a fold hides some, a wrapped
        line is many) the rows from the top line to the caret's are counted, and the top moves down until they fit.
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
                else if (!t->wrap && !ui_text_folds(t))
                {
                        if (rows > 0 && line >= t->top_line + (p64)rows)
                                t->top_line = line - (p64)rows + 1;
                }
                else if (rows > 0)
                {
                        int used = 0;

                        for (p64 each = t->top_line; each <= line; each++)
                                used += ui_text_line_rows(ui, t, each);
                        while (used > rows && t->top_line < line)
                                used -= ui_text_line_rows(ui, t, t->top_line++);
                }
        }
        if (t->wrap && t->mode != UI_TEXT_FIELD)
        {
                t->scroll_x = 0;
                return;
        }
        ui_text_layout_line(ui, t, line, &l);
        x = ui_text_along(&l, ui_text_x_of(&l, caret)); // from the line's start, which is its right where it reads from the right
        if (x - t->scroll_x < 0)
                t->scroll_x = x - width / 4 > 0 ? x - width / 4 : 0;
        else if (x - t->scroll_x > width - 2 * ui->scale / 100)
                t->scroll_x = x - width + width / 4;
        // A field whose line fits is not scrolled at all; one longer shows no blank past its end.
        if (t->mode == UI_TEXT_FIELD && width > 0)
        {
                b64 most = (b64)l.width - width + 2 * ui->scale / 100;

                t->scroll_x = t->scroll_x > most ? (most > 0 ? most : 0) : t->scroll_x;
        }
}

/*
        The document offset under device pixels x, y of the widget's text box (clamped to it): the row at y is the view's
        row (ui_text_nth_row), a wrapped line's or a fold's included, and in it the boundary nearest x. Past the end of a wrapped
        row that ends at a blank it is the place before the blank, which is where the row's text ends.
*/
static p64 ui_text_hit(struct ui address_to ui, struct ui_text address_to t, int x, int y)
{
        static struct ui_text_layout l;
        struct ui_text_row row;
        int index = y > t->top && t->line_height ? (y - t->top) / t->line_height : 0, px;

        ui_text_nth_row(ui, t, index < 4096 ? index : 4096, &l, &row);
        if (l.bidi)
        {
                // A reordered line: the boundary whose place is nearest, whichever way it runs.
                int along = x - ui_text_origin(t, &l), best = 0;

                for (p32 b = 1; b <= l.count; b++)
                        if ((l.x[b] > along ? l.x[b] - along : along - l.x[b]) < (l.x[best] > along ? l.x[best] - along : along - l.x[best]))
                                best = (int)b;
                return l.offset[best];
        }
        px = x - t->left + (int)t->scroll_x + row.shift;
        for (p32 k = row.from; k < row.to; k++)
                if (px < (l.x[k] + l.x[k + 1]) / 2)
                        return l.offset[k];
        return !row.last && row.to > row.from && l.shown[l.shown_at[row.to - 1]] == ' ' ? l.offset[row.to - 1] : l.offset[row.to];
}

/*
        The caret's blink: a rounded square wave, not a sine. The caret is solid for most of each half of its cycle and gone
        for most of the other half, and each change between the two is a smoothstep fade across UI_CARET_EDGE_PERCENT of
        a half, centred on the change, so it reads as steady and never jumps. The cycle is the person's (canvas caret
        blink): 1.6 s slow, 1.06 s normal, 0.53 s fast; off is no cycle at all, a solid caret.
*/
#define UI_CARET_FULL 255u
#define UI_CARET_EDGE_PERCENT 15ull    // a fade's length, percent of a half cycle
#define UI_CARET_STEP_NS 16000000ull   // a fade is drawn at most this often: a display period

// The cycle in ns, or 0 when the caret does not blink.
static b64 ui_caret_cycle(void)
{
        switch (ui_kit.caret_speed)
        {
        case UI_CARET_SLOW:
                return 1600000000ull;
        case UI_CARET_FAST:
                return 530000000ull;
        case UI_CARET_OFF:
                return 0;
        default:
                return 1060000000ull;
        }
}

// The width in logical pixels of the caret's bar: 1 thin, 2 normal, 3 thick.
static int ui_caret_thick(void)
{
        return ui_kit.caret_thick == UI_CARET_THIN ? 1 : ui_kit.caret_thick == UI_CARET_THICK ? 3 : 2;
}

// The caret's width in device pixels: its logical width at the scale, and never less than one (a thin caret is 0 at 75 percent).
static int ui_caret_px(const struct ui address_to ui)
{
        int w = ui_caret_thick() * ui->scale / 100;

        return w > 0 ? w : 1;
}

// A fade's cover, 0 to UI_CARET_FULL, at `at` ns into a fade of `edge` ns: smoothstep, rising or falling.
static unsigned int ui_caret_ramp(b64 at, b64 edge, int rising)
{
        b64 x = at * 65536 / edge, s = (3 * x * x * 65536 - 2 * x * x * x) >> 32; // 0 to 65536, exact in 64 bits
        unsigned int cover = (unsigned int)(s * UI_CARET_FULL / 65536);

        return rising ? cover : UI_CARET_FULL - cover;
}

// The caret's cover at q ns into a cycle of `cycle` ns (q below cycle): solid, then gone, each change a fade centred on it.
// The solid half runs from reach to half - reach, so the fades are edge long, and the wrap joins the fade up to 0 at cycle.
static unsigned int ui_caret_wave(b64 q, b64 cycle)
{
        b64 half = cycle / 2, edge = half * UI_CARET_EDGE_PERCENT / 100, reach = edge / 2;

        if (q < reach)
                return ui_caret_ramp(q + reach, edge, 1);
        if (q < half - reach)
                return UI_CARET_FULL;
        if (q < half + reach)
                return ui_caret_ramp(q - (half - reach), edge, 0);
        if (q < cycle - reach)
                return 0;
        return ui_caret_ramp(q - (cycle - reach), edge, 1);
}

// Whether the cover is flat at q: solid or gone, away from the fades. A fade starts and ends on a solid or a gone cover, so the
// cover's value does not say it: a wake there must wait for the fade's steps, not for its far end.
static b32 ui_caret_flat(b64 q, b64 cycle)
{
        b64 half = cycle / 2, reach = half * UI_CARET_EDGE_PERCENT / 100 / 2;

        return (q >= reach && q < half - reach) || (q >= half + reach && q < cycle - reach);
}

// The ns from q until the cover can next change: to the next fade while it is flat, and a frame's step while it fades.
static b64 ui_caret_until(b64 q, b64 cycle)
{
        b64 half = cycle / 2, reach = half * UI_CARET_EDGE_PERCENT / 100 / 2, stops[4] = {reach, half - reach, half + reach, cycle - reach};
        b64 next = cycle - q;

        for (positive k = 0; k < 4; k++)
                if (stops[k] > q && stops[k] - q < next)
                        next = stops[k] - q;
        if (ui_caret_flat(q, cycle) || next < UI_CARET_STEP_NS)
                return next;
        return UI_CARET_STEP_NS;
}

// The caret is solid for the next half cycle whatever the blink says: it is moving, or just typed at. Its phase starts a
// quarter of a cycle into the solid half, so the caret is solid at once and stays solid while the keys keep coming.
static fn ui_text_wake(struct ui_text address_to t)
{
        t->blink_at = t->awake_at = clock_monotonic_nanoseconds();
        t->caret_fade = 0;
}

/*
        The blink: when the widget next needs a frame (0 for never: it has not the keys), the caret's cover as its phase
        says. The app asks after each pass and sleeps till then; nothing blinks in a window without the keys. Ten seconds
        after the last key or move the caret stops blinking and stays drawn, so a field left alone wakes nothing.
*/
/*
        A caret's cover at now, for a phase that restarted at since and an awake time (a key or a move, the last of them),
        and when that cover next changes (0 for never). Every caret runs this: a field's and the terminal's.
*/
static unsigned int ui_caret_at(positive now, positive since, positive awake, positive address_to due)
{
        b64 cycle = ui_caret_cycle(), q;
        unsigned int cover;

        *due = 0;
        if (!cycle || (now > awake && now - awake >= UI_TEXT_BLINK_FOR_NS))
                return UI_CARET_FULL;
        q = now < since ? cycle / 4 : (now - since + cycle / 4) % cycle;
        cover = ui_caret_wave(q, cycle);
        *due = now + ui_caret_until(q, cycle);
        // The caret stops at ten seconds, whatever phase it is in: the wait ends there, so a caret gone then is drawn then.
        if (now >= awake && awake + UI_TEXT_BLINK_FOR_NS < *due)
                *due = awake + UI_TEXT_BLINK_FOR_NS;
        return cover;
}

static positive ui_text_tick(struct ui_text address_to t, positive now, b32 address_to redraw)
{
        positive due;
        unsigned int cover;

        if (!t->focused || t->read_only)
                return 0;
        cover = ui_caret_at(now, t->blink_at, t->awake_at, &due);
        if (t->caret_fade != UI_CARET_FULL - cover)
                t->caret_fade = UI_CARET_FULL - cover, *redraw = true;
        return due;
}

static positive ui_text_autoscroll(struct ui address_to ui, struct ui_text address_to t, positive now, b32 address_to redraw);
static positive ui_text_bar_tick(struct ui_text address_to t, positive now, b32 address_to redraw);
static b32 ui_text_fling(struct ui_text address_to t, positive now);

/*
        The widget's next frame as a loop's timeout: its throw (a finger's flick, ui_text_fling, for a field as for a code view),
        its blink, its selection's scroll (ui_text_autoscroll, a drag held past an edge) and the actions bar's coming and going
        (ui_text_bar_tick), each folded into the timeout the loop sleeps (-1 for none, as the tick answers it), with the redraw
        they asked for. A throw's step that moves the lines asks for the picture, the last one too (the bar's own return is its tick's).
        Every app's tick asks this for its field, and none of them keeps its own deadline.
*/
static long ui_text_timeout(struct ui address_to ui, struct ui_text address_to t, long timeout, positive now, b32 address_to redraw)
{
        positive due, scroll, bar;

        if (t->kinetic.flying)
        {
                p64 was = t->top_line;
                b32 flying = ui_text_fling(t, now);

                if (t->top_line != was)
                        *redraw = true;
                if (flying && (timeout < 0 || (long)UI_DRAG_FRAME_NS < timeout))
                        timeout = UI_DRAG_FRAME_NS;
        }
        due = ui_text_tick(t, now, redraw), scroll = ui_text_autoscroll(ui, t, now, redraw), bar = ui_text_bar_tick(t, now, redraw);
        if (due && (timeout < 0 || (long)(due - now) < timeout))
                timeout = (long)(due - now);
        if (scroll && (timeout < 0 || (long)(scroll - now) < timeout))
                timeout = (long)(scroll - now);
        if (bar && (timeout < 0 || (long)(bar - now) < timeout))
                timeout = (long)(bar - now);
        return timeout;
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
/*
        Left and Right go where the screen says: on a line that reads from the right Left is forward in the text, and on a line of both
        directions the caret goes to the boundary that is next to it on the screen (by the clusters, as the line is laid out), none
        being next, the line's logical end. A line that reads from the left is the default. A word goes by the line's direction.
*/
static b32 ui_text_visual(struct ui address_to ui, struct ui_text address_to t, p32 address_to command)
{
        static struct ui_text_layout l;
        static const p32 pairs[4][2] = {{DOC_CMD_LEFT, DOC_CMD_RIGHT}, {DOC_CMD_SELECT_LEFT, DOC_CMD_SELECT_RIGHT},
                                        {DOC_CMD_WORD_LEFT, DOC_CMD_WORD_RIGHT}, {DOC_CMD_SELECT_WORD_LEFT, DOC_CMD_SELECT_WORD_RIGHT}};
        int pair = -1, side = 0;
        struct doc_selection address_to caret = &t->doc.selections[t->doc.primary];
        p32 at = 0, best = 0;
        b32 found = false;

        for (int k = 0; k < 4; k++)
                for (int j = 0; j < 2; j++)
                        if (*command == pairs[k][j])
                                pair = k, side = j;
        if (pair < 0)
                return false;
        ui_text_layout_line(ui, t, doc_line_of(&t->doc, caret->active), &l);
        if (!l.bidi)
                return false;
        if (pair >= 2 || t->doc.selection_count != 1)
        {
                // A word, or carets in a number: the way the line reads decides, as the text goes.
                *command = pairs[pair][l.base ? 1 - side : side];
                return false;
        }
        while (at < l.count && l.offset[at] < caret->active)
                at++;
        for (p32 b = 0; b <= l.count; b++)
                if (side ? l.x[b] > l.x[at] : l.x[b] < l.x[at])
                        if (!found || (side ? l.x[b] < l.x[best] : l.x[b] > l.x[best]))
                                best = b, found = true;
        if (!found)
        {
                *command = pairs[pair][l.base ? 1 - side : side]; // the line's end: on to the next or the one before
                return false;
        }
        doc_select_set(&t->doc, pair == 1 ? caret->anchor : l.offset[best], l.offset[best]);
        return true;
}

static p32 ui_text_command(struct ui address_to ui, struct ui_text address_to t, p32 command)
{
        p64 version = t->doc.version;
        int page = ui_text_rows(t) > 1 ? ui_text_rows(t) - 1 : 1;

        if (t->mode == UI_TEXT_FIELD && !ui_text_field_command(command))
                return 0;
        if (t->masked && (command = ui_text_masked_command(command)) == DOC_CMD_NONE)
                return UI_TEXT_MOVED;
        if (ui_text_visual(ui, t, &command))
        {
                ui_text_wake(t);
                ui_text_reveal(ui, t);
                return UI_TEXT_MOVED;
        }
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
                t->menu_x = ui_text_origin(t, &l) + ui_text_x_of(&l, caret);
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
        Answers what changed, and UI_TEXT_MENU for the right button (menu_x and menu_y say where). A mouse's presses and
        drags are here; a finger's are ui_text_finger's, further on.
*/
static b32 ui_text_selected(const struct ui_text address_to t, p64 offset);
static p32 ui_text_finger(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y,
                          positive now);
static b32 ui_text_reach(struct ui address_to ui, struct ui_text address_to t, int x, int y);

// How long the view stays still after it moved before the actions bar comes back, and how long the bar takes to fade in.
#define UI_TEXT_BAR_AFTER_NS 200000000ull
#define UI_TEXT_BAR_FADE_NS 120000000ull

// A drag's selection, taken at the point at: it grows by the drag's kind from where the drag began (drag_kind, drag_start and
// drag_end). A word or a line is taken whole, back to the start of one before the drag's start and on to the end of one after.
static fn ui_text_extend(struct ui address_to ui, struct ui_text address_to t, p64 at, b32 reveal)
{
        p64 start = at, finish = at;
        struct doc_selection address_to primary = &t->doc.selections[t->doc.primary];

        if (t->drag_kind == 2)
                doc_word_at(&t->doc, at, &start, &finish);
        else if (t->drag_kind == 3)
        {
                p64 line = doc_line_of(&t->doc, at);

                start = doc_line_start(&t->doc, line), finish = doc_line_start(&t->doc, line + 1);
        }
        if (start < t->drag_start)
                primary->anchor = t->drag_end, primary->active = start;
        else
                primary->anchor = t->drag_start, primary->active = finish > t->drag_end ? finish : t->drag_end;
        if (t->drag_kind == 1)
                primary->active = at;
        doc_normalize(&t->doc);
        if (reveal)
                ui_text_reveal(ui, t);
        ui_text_wake(t);
}

// The lines a finger's or a throw's pixels make, up positive: the rest is kept for the next move, so a report shorter than a line
// still scrolls once the pixels add up to one.
static long ui_text_lines_moved(struct ui_text address_to t, int up)
{
        long pending = (long)t->finger_left + up, lines = t->line_height > 0 ? pending / t->line_height : 0;

        t->finger_left = t->line_height > 0 ? (int)(pending - lines * t->line_height) : 0;
        return lines;
}

static p32 ui_text_pointer(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y)
{
        b32 well = x >= t->well_left && x < t->well_right && y >= t->well_top && y < t->well_bottom;
        b32 shift = (event->flags & WINDOW_KEY_SHIFT) != 0, alt = (event->flags & WINDOW_KEY_ALT) != 0 && t->mode != UI_TEXT_FIELD;
        positive now = clock_monotonic_nanoseconds();

        if (event->type == WINDOW_EVENT_WHEEL)
        {
                if (!well)
                        return 0;
                if ((event->flags & WINDOW_KEY_CONTROL) && t->mode == UI_TEXT_CODE)
                        return ui_text_command(ui, t, event->a > 0 ? DOC_CMD_ZOOM_IN : DOC_CMD_ZOOM_OUT);
                t->quiet_at = now + UI_TEXT_BAR_AFTER_NS;
                if (event->b)
                        t->scroll_x = t->scroll_x - (b64)event->a * t->cell * 3 / 120 > 0 ? t->scroll_x - (b64)event->a * t->cell * 3 / 120 : 0;
                else
                {
                        b64 lines = -(b64)event->a * 3 / 120, to = (b64)t->top_line + (lines ? lines : event->a < 0 ? 1 : -1);

                        t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
                }
                return UI_TEXT_MOVED;
        }
        // A finger's: its left button (the press, the lift, the compositor taking it back), the right button of its long press
        // (WINDOW_KEY_HOLD), and its moves while it holds something. A two fingers' right button is a mouse's, and asks the menu.
        if (event->type == WINDOW_EVENT_BUTTON && ((event->a == 0x110 && (event->flags & WINDOW_KEY_TOUCH)) ||
                                                   (event->a == 0x111 && (event->flags & WINDOW_KEY_HOLD))))
                return ui_text_finger(ui, t, event, x, y, now);
        if (event->type == WINDOW_EVENT_MOTION && t->finger)
                return event->flags & WINDOW_KEY_TOUCH ? ui_text_finger(ui, t, event, x, y, now) : 0;
        if (event->type == WINDOW_EVENT_BUTTON)
        {
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

                if (!down)
                {
                        b32 was = t->dragging;

                        t->dragging = false, t->edge.past = 0;
                        if (was && doc_any_selected(&t->doc))
                                ui_text_copy(t, WINDOWS_CLIP_PRIMARY);
                        return 0;
                }
                if (!well)
                        return 0;
                t->handles = t->bar = false;
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
                {
                        p64 at = ui_text_hit(ui, t, x, y), start = at, finish = at;

                        t->clicks = ui_click_count(ui, &t->click, now, x, y);
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
                // A drag past the top or bottom edge scrolls the box (ui_text_autoscroll), and the selection is taken at the
                // edge's row meanwhile: the drag's last point is kept for that.
                int edge = y < t->top ? t->top : y >= t->bottom ? t->bottom - 1 : y;
                p64 at = ui_text_hit(ui, t, x, edge);

                // A field's edge is its left and right: past them the field scrolls sideways, as a code view does up and down.
                t->drag_x = x, t->drag_y = y;
                t->edge.past = t->mode == UI_TEXT_FIELD ? (x < t->left ? x - t->left : x >= t->right ? x - t->right + 1 : 0)
                                 : y < t->top ? y - t->top : y >= t->bottom ? y - t->bottom + 1 : 0;
                if (t->drag_box)
                {
                        doc_select_box(&t->doc, t->box_line, t->box_column, doc_line_of(&t->doc, at), doc_column(&t->doc, at));
                        return UI_TEXT_MOVED;
                }
                // Past the edge the view is the tick's to move, a step at a time: the reveal waits for the drag to come back.
                ui_text_extend(ui, t, at, t->edge.past == 0);
                return UI_TEXT_MOVED;
        }
        return 0;
}

// A thrown finger's scroll, a frame at a time: whether it still moves.
static b32 ui_text_fling(struct ui_text address_to t, positive now)
{
        int moved = ui_drag_step(&t->kinetic, now);
        long lines = ui_text_lines_moved(t, -moved);
        b64 to = (b64)t->top_line + lines;

        if (lines)
                t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
        return t->kinetic.flying;
}

/*
        A selection drag past an edge of a text widget scrolls it, smoothly, as the time passes: a code view up past its top and
        down past its bottom a line at a time, a field left past its left edge and right past its right a device pixel at a time;
        120 logical pixels a second at the edge, 6 more for each logical pixel the drag is past it, at most 2400. Each step the
        selection is taken again at the edge (the row, or the column), so it grows as the text scrolls, and a drag back inside, or
        its lift, stops it. A drag by a box is not scrolled. Answers when the next step is due, 0 for none.
*/
#define UI_TEXT_SCROLL_SPEED 120
#define UI_TEXT_SCROLL_FAR 6
#define UI_TEXT_SCROLL_MOST 2400

// The speed a drag past an edge scrolls at, in device pixels a second, for past device pixels past it at scale (the terminal's too).
static long ui_edge_speed(long past, int scale)
{
        long distance = (past < 0 ? -past : past) * 100 / scale, speed = UI_TEXT_SCROLL_SPEED + distance * UI_TEXT_SCROLL_FAR;

        speed = speed > UI_TEXT_SCROLL_MOST ? UI_TEXT_SCROLL_MOST : speed;
        return speed * scale / 100;
}

/*
        The steps an edge scroll owes at now (ui_edge_speed for the drag's edge), each unit thousandths of a device pixel: a line
        (its height times 1000) for a text widget's lines and the terminal's rows, 1000 for a field's pixels. Zero for none, and the
        first call after the drag starts (or after it stopped) owes none and takes its time as the step's start. The caller is the
        one that knows the drag is on, and what a step does.
*/
static long ui_edge_steps(struct ui_edge address_to e, positive now, int scale, long unit)
{
        long steps;

        if (!e->at || now < e->at)
        {
                e->at = now, e->owed = 0;
                return 0;
        }
        // Device pixels a second over the time since the last step: thousandths of a device pixel owed.
        e->owed += ui_edge_speed(e->past, scale) * (long)(now - e->at) / 1000000l;
        e->at = now;
        steps = e->owed / unit;
        if (steps <= 0)
                return 0;
        e->owed -= steps * unit;
        return steps;
}

// The most a field's view scrolls: its line's end past the box, with a little room after it (ui_text_reveal's rule).
static b64 ui_text_scroll_most(struct ui address_to ui, struct ui_text address_to t)
{
        static struct ui_text_layout l;
        b64 most;

        ui_text_layout_line(ui, t, 0, &l);
        most = (b64)l.x[l.count] - (t->right - t->left) + 2 * ui->scale / 100;
        return most > 0 ? most : 0;
}

static positive ui_text_autoscroll(struct ui address_to ui, struct ui_text address_to t, positive now, b32 address_to redraw)
{
        b32 field = t->mode == UI_TEXT_FIELD;
        p64 lines = doc_line_count(&t->doc);
        long steps;
        int edge_x, edge_y;

        if (!t->dragging || !t->edge.past || t->drag_box || (field ? t->right <= t->left : t->line_height <= 0))
        {
                t->edge.at = 0;
                return 0;
        }
        steps = ui_edge_steps(&t->edge, now, ui->scale, field ? 1000l : 1000l * t->line_height);
        if (steps <= 0)
                return now + UI_DRAG_FRAME_NS;
        if (field)
        {
                if (t->edge.past < 0)
                        t->scroll_x = t->scroll_x > (b64)steps ? t->scroll_x - (b64)steps : 0;
                else
                {
                        b64 most = ui_text_scroll_most(ui, t);

                        t->scroll_x = t->scroll_x + (b64)steps < most ? t->scroll_x + (b64)steps : most;
                }
        }
        else
        {
                if (t->edge.past < 0)
                        t->top_line = (p64)steps < t->top_line ? t->top_line - (p64)steps : 0;
                else
                        t->top_line = t->top_line + (p64)steps < lines ? t->top_line + (p64)steps : lines - 1;
        }
        edge_x = t->drag_x < t->left ? t->left : t->drag_x >= t->right ? t->right - 1 : t->drag_x;
        edge_y = t->drag_y < t->top ? t->top : t->drag_y >= t->bottom ? t->bottom - 1 : t->drag_y;
        ui_text_extend(ui, t, ui_text_hit(ui, t, edge_x, edge_y), false);
        *redraw = true;
        return now + UI_DRAG_FRAME_NS;
}

// Any event of the window the widget may take: keys only while it has the keys; the pointer within its box. A key is
// no finger's selection (its handles and bar go), and neither is a text that changed.
static p32 ui_text_event(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y)
{
        t->asks = 0;
        if (event->type == WINDOW_EVENT_KEY)
        {
                if (!t->focused)
                        return 0;
                if (event->flags & WINDOW_KEY_DOWN)
                        t->handles = t->bar = false;
                return t->asks = ui_text_key(ui, t, event);
        }
        t->asks = event->type == WINDOW_EVENT_CLIP ? ui_text_clip(t, event) : ui_text_pointer(ui, t, event, x, y);
        if (t->asks & UI_TEXT_CHANGED)
                t->handles = t->bar = false;
        return t->asks;
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

// The items of the menu, or with actions only the four a finger's selection offers (the actions bar's): Cut, Copy, Paste and
// Select All, each greyed when the widget can not do it (the bar leaves those out).
static fn ui_text_menu_items(struct ui_popmenu address_to m, const struct ui_text address_to t, b32 actions)
{
        p32 none = doc_any_selected(&t->doc) ? 0 : UI_MENU_DISABLED, fixed = t->read_only ? UI_MENU_DISABLED : 0;
        p32 hidden = t->masked ? UI_MENU_DISABLED : 0;

        if (actions)
        {
                ui_popmenu_add(m, WORDS(menu_cut), ui_text_menu_key(t, UI_TEXT_MENU_CUT, DOC_CMD_CUT), none | fixed | hidden, UI_TEXT_MENU_CUT);
                ui_popmenu_add(m, WORDS(menu_copy), ui_text_menu_key(t, UI_TEXT_MENU_COPY, DOC_CMD_COPY), none | hidden, UI_TEXT_MENU_COPY);
                ui_popmenu_add(m, WORDS(menu_paste), ui_text_menu_key(t, UI_TEXT_MENU_PASTE, DOC_CMD_PASTE),
                               fixed | (t->own ? 0 : UI_MENU_DISABLED), UI_TEXT_MENU_PASTE);
                ui_popmenu_add(m, WORDS(menu_select_all), ui_text_menu_key(t, UI_TEXT_MENU_ALL, DOC_CMD_SELECT_ALL),
                               doc_length(&t->doc) ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_ALL);
                return;
        }
        ui_popmenu_add(m, WORDS(menu_undo), ui_text_menu_key(t, UI_TEXT_MENU_UNDO, DOC_CMD_UNDO),
                       t->doc.done && !fixed ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_UNDO);
        ui_popmenu_add(m, WORDS(menu_redo), ui_text_menu_key(t, UI_TEXT_MENU_REDO, DOC_CMD_REDO),
                       t->doc.done < t->doc.group_count && !fixed ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_REDO);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, WORDS(menu_cut), ui_text_menu_key(t, UI_TEXT_MENU_CUT, DOC_CMD_CUT), none | fixed | hidden, UI_TEXT_MENU_CUT);
        ui_popmenu_add(m, WORDS(menu_copy), ui_text_menu_key(t, UI_TEXT_MENU_COPY, DOC_CMD_COPY), none | hidden, UI_TEXT_MENU_COPY);
        ui_popmenu_add(m, WORDS(menu_paste), ui_text_menu_key(t, UI_TEXT_MENU_PASTE, DOC_CMD_PASTE),
                       fixed | (t->own ? 0 : UI_MENU_DISABLED), UI_TEXT_MENU_PASTE);
        ui_popmenu_add(m, WORDS(menu_delete), ui_text_menu_key(t, UI_TEXT_MENU_DELETE, DOC_CMD_DELETE), none | fixed, UI_TEXT_MENU_DELETE);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, WORDS(menu_select_all), ui_text_menu_key(t, UI_TEXT_MENU_ALL, DOC_CMD_SELECT_ALL),
                       doc_length(&t->doc) ? 0 : UI_MENU_DISABLED, UI_TEXT_MENU_ALL);
}

static b32 ui_text_menu_open(struct ui_popup address_to p, struct ui_text address_to t, const struct ui address_to ui, int x, int y, b32 touch)
{
        if (!p || !t->own)
                return false;
        p->menu.count = 0;
        ui_text_menu_items(&p->menu, t, false);
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
        if (id == UI_TEXT_MENU_ALL)
                t->handles = true;
        return ui_text_command(ui, t, commands[id]);
}

// Whether a point of the window (device pixels) is on the widget: a field's whole well, as it was last drawn. The rule is ui_reach_hit's
// (the one hit test), with no finger's reach: the well is what the finger touches.
static b32 ui_text_inside(const struct ui_text address_to t, int x, int y)
{
        return ui_reach_hit(0, 0, x, y, t->well_left, t->well_top, t->well_right - t->well_left, t->well_bottom - t->well_top);
}

/*
        A finger's selection. A long press on text selects the word under it (the compositor sends it as the right button
        flagged WINDOW_KEY_HOLD), a double tap selects a word and a triple tap a line, and the finger going on after either
        extends the selection by words until it lifts. The selection then shows a handle at each end, and an actions bar (Cut,
        Copy, Paste and Select All, the ones the widget can do: ui_text_menu_items) over it. A finger on a handle moves that end,
        anywhere in the text, past the edge of the view too (the view scrolls while the finger is held in the band at the edge,
        at the speed of a mouse's selection drag); the two ends may cross. A finger that lands on the text and moves still
        scrolls it, as before; a tap on the selection or on a handle shows the bar or takes it away, and a tap anywhere else puts
        the caret there. The bar is not shown while a handle or the text moves under a finger, and comes back when it is still.

        The handles and the bar are the widget's overlay (ui_text_overlay), which the app draws last in its picture so nothing
        is drawn over them, and they are inside the window: a press on them is the widget's (ui_text_reach). What a finger can
        hit is at least UI_TARGET logical pixels each way (ui_hit), the drawn shapes small in the middle of it.
*/
#define UI_TEXT_HANDLE 6    // a handle's knob, its radius in logical pixels; the stem over it is two wide
#define UI_TEXT_TITLEBAR 24 // the top of a window the compositor takes for the window (COMPOSITOR_GRAB): the bar keeps below it
#define UI_TEXT_BAR_PAD 16  // the bar's item, its label's room each side
#define UI_TEXT_BAR_GAP 8   // the bar's room from the selection
#define UI_TEXT_BAR_EDGE 12 // and from the window's edge, which a finger's grip (the compositor's) takes the press of

// The label of an item of the actions bar.
static const char address_to ui_text_action_label(int id)
{
        return id == UI_TEXT_MENU_CUT     ? WORDS(action_cut)
               : id == UI_TEXT_MENU_COPY  ? WORDS(action_copy)
               : id == UI_TEXT_MENU_PASTE ? WORDS(action_paste)
                                          : WORDS(action_select_all);
}

// The bar's items now, in bar_id: the menu's four (ui_text_menu_items) the widget can do, Select All left off when everything
// is selected already. How many.
static int ui_text_actions(struct ui_text address_to t)
{
        static struct ui_popmenu m;
        const struct doc_selection address_to s = &t->doc.selections[t->doc.primary];
        b32 all = doc_length(&t->doc) && doc_sel_start(s) == 0 && doc_sel_end(s) == doc_length(&t->doc);
        int count = 0;

        m.count = 0;
        ui_text_menu_items(&m, t, true);
        for (int at = 0; at < m.count && count < UI_TEXT_BAR_ITEMS; at++)
                if (!(m.items[at].flags & UI_MENU_DISABLED) && !(m.items[at].id == UI_TEXT_MENU_ALL && all))
                        t->bar_id[count++] = m.items[at].id;
        return count;
}

// The item of the bar under x, y as it was drawn, or -1: none when the bar is not up.
static int ui_text_bar_item(const struct ui_text address_to t, int x, int y)
{
        if (!t->bar_up || x < t->bar_x || x >= t->bar_x + t->bar_w || y < t->bar_y || y >= t->bar_y + t->bar_h)
                return -1;
        for (int k = 0; k < t->bar_count; k++)
                if (x - t->bar_x < t->bar_at[k + 1])
                        return k;
        return -1;
}

// Which end's handle a finger at x, y is on (1 the start, 2 the end), or 0: the shape is the knob, its reach the finger's (ui_hit);
// where the two reaches meet it is the nearer knob's. A handle whose row is out of the box is not there to be taken.
static int ui_text_handle_hit(struct ui address_to ui, struct ui_text address_to t, int x, int y)
{
        const struct doc_selection address_to s = &t->doc.selections[t->doc.primary];
        int radius = ui_dev(ui, UI_TEXT_HANDLE), best = 0;
        long near = 0;

        if (!t->handles || t->doc.selection_count != 1 || doc_sel_start(s) >= doc_sel_end(s))
                return 0;
        for (int side = 1; side <= 2; side++)
        {
                int hx, top;
                long dx, dy, far;

                if (ui_text_where(ui, t, side == 2 ? doc_sel_end(s) : doc_sel_start(s), &hx, &top) ||
                    !ui_hit(ui, true, x, y, hx - radius, top + t->line_height, 2 * radius, 2 * radius))
                        continue;
                dx = x - hx, dy = y - (top + t->line_height + radius);
                far = dx * dx + dy * dy;
                if (!best || far < near)
                        best = side, near = far;
        }
        return best;
}

// Whether a press at x, y is the widget's: its well, its actions bar over whatever is under it, or a handle.
static b32 ui_text_reach(struct ui address_to ui, struct ui_text address_to t, int x, int y)
{
        return ui_text_inside(t, x, y) || (t->bar_up && x >= t->bar_x && x < t->bar_x + t->bar_w && y >= t->bar_y && y < t->bar_y + t->bar_h) ||
               ui_text_handle_hit(ui, t, x, y);
}

/*
        Whether the bar may be seen now: no finger is moving a handle, a selection or the text (a press that has not moved is
        none), nothing is thrown, and the view has been still for UI_TEXT_BAR_AFTER_NS.
*/
static b32 ui_text_bar_still(const struct ui_text address_to t, positive now)
{
        return t->finger != UI_TEXT_FINGER_HANDLE && t->finger != UI_TEXT_FINGER_SELECT &&
               !(t->finger == UI_TEXT_FINGER_TEXT && t->tap.dragged) && !t->kinetic.flying && now >= t->quiet_at;
}

/*
        Where the bar goes: its items' widths, and over the selection (centred on the first row's part of it, above its first
        visible row) or below it when there is no room above, clear of the handles hung under it. It stays inside the window,
        in from its edges and below its top, which the compositor takes for the window's own. False when there is nothing
        to offer or none of the selection is in the view.
*/
static b32 ui_text_bar_place(struct ui address_to ui, struct ui_text address_to t)
{
        const struct doc_selection address_to s = &t->doc.selections[t->doc.primary];
        int sx = t->left, st = t->top, ex = t->right, et = t->top, count = ui_text_actions(t);
        int a = ui_text_where(ui, t, doc_sel_start(s), &sx, &st), b = ui_text_where(ui, t, doc_sel_end(s), &ex, &et);
        int pad = ui_dev(ui, UI_TEXT_BAR_PAD), edge = ui_dev(ui, UI_TEXT_BAR_EDGE), room = ui->width - 2 * edge, titlebar = ui_dev(ui, UI_TEXT_TITLEBAR);
        int top, bottom, mid, text = 0, wide = 0, x, y;

        if (!count || (a < 0 && b < 0) || (a > 0 && b > 0))
                return false;
        top = a == 0 ? st : t->top;
        bottom = b == 0 ? et + t->line_height : t->bottom;
        mid = a == 0 && b == 0 && st == et ? (sx + ex) / 2 : a == 0 ? (sx + t->right) / 2 : (t->left + t->right) / 2;
        for (int k = 0; k < count; k++)
                text += ui_kit_measure(ui, (string_address)ui_text_action_label(t->bar_id[k]), UI_BODY_MEDIUM);
        // A window too narrow for the bar gives its items less room, down to a little.
        wide = text + 2 * pad * count;
        if (wide > room && room > 0)
                pad = (room - text) / (2 * count) > ui_dev(ui, 2) ? (room - text) / (2 * count) : ui_dev(ui, 2), wide = text + 2 * pad * count;
        t->bar_w = wide, t->bar_h = ui_dev(ui, UI_TARGET);
        t->bar_count = count;
        t->bar_at[0] = 0;
        for (int k = 0; k < count; k++)
                t->bar_at[k + 1] = t->bar_at[k] + ui_kit_measure(ui, (string_address)ui_text_action_label(t->bar_id[k]), UI_BODY_MEDIUM) + 2 * pad;
        x = mid - wide / 2;
        x = x > ui->width - wide - edge ? ui->width - wide - edge : x;
        x = x < edge ? edge : x;
        y = top - t->bar_h - ui_dev(ui, UI_TEXT_BAR_GAP);
        if (y < titlebar)
                y = bottom + 2 * ui_dev(ui, UI_TEXT_HANDLE) + ui_dev(ui, UI_TEXT_BAR_GAP);
        y = y > ui->height - t->bar_h - edge ? ui->height - t->bar_h - edge : y;
        y = y < titlebar ? titlebar : y;
        t->bar_x = x, t->bar_y = y;
        return true;
}

/*
        The handles and the bar, over everything the app drew: a flat stem down the end's row in the accent with a knob hung
        under it, at each end of a finger's selection that is in the box; then the bar, in the surface's highest layer with a line
        round it and a rule between its items, the one a finger is on lit, faded in over UI_TEXT_BAR_FADE_NS the frame it first comes
        (at once with reduced motion). Nothing is drawn for a widget that has neither, and what it covered is kept (over_top,
        over_bottom) for an app that draws in bands. An app calls it last, for each widget it draws, in the same frame.
*/
static fn ui_text_overlay_draw(struct ui address_to ui, struct ui_text address_to t)
{
        const struct doc_selection address_to s = &t->doc.selections[t->doc.primary];
        positive now = clock_monotonic_nanoseconds();
        b32 was = t->bar_up;
        int radius = ui_dev(ui, UI_TEXT_HANDLE), stem = ui_dev(ui, 2) ? ui_dev(ui, 2) : 1;

        t->over_top = t->over_bottom = 0;
        t->bar_up = false;
        if (t->handles && t->doc.selection_count == 1 && doc_sel_start(s) < doc_sel_end(s))
        {
                unsigned int tone = ui_tone(ui_role(UI_PRIMARY) & 0xffffffu, 255);

                for (int side = 0; side < 2; side++)
                {
                        int x, top;

                        if (ui_text_where(ui, t, side ? doc_sel_end(s) : doc_sel_start(s), &x, &top))
                                continue;
                        ui_fill(ui, x - stem / 2, top, stem, t->line_height, tone);
                        ui_round(ui, x - radius, top + t->line_height, 2 * radius, 2 * radius, radius, tone);
                        t->over_top = !t->over_bottom || top < t->over_top ? top : t->over_top;
                        t->over_bottom = top + t->line_height + 2 * radius > t->over_bottom ? top + t->line_height + 2 * radius : t->over_bottom;
                }
        }
        t->bar_want = t->bar && ui_text_bar_still(t, now);
        if (t->bar_want && ui_text_bar_place(ui, t))
        {
                int line = ui_px(ui, 1), corner = ui_dev(ui, UI_MENU_RADIUS), baseline, alpha = 255;
                b64 age = now - (was ? t->bar_since : now);

                if (!was)
                        t->bar_since = now;
                if (!ui_kit.motion && age < (b64)UI_TEXT_BAR_FADE_NS)
                        alpha = (int)(age * 255 / (b64)UI_TEXT_BAR_FADE_NS), ui_kit.busy = 1;
                ui_round(ui, t->bar_x, t->bar_y, t->bar_w, t->bar_h, corner, ui_tone(ui_role(UI_OUTLINE_VARIANT) & 0xffffffu, (unsigned int)alpha));
                ui_round(ui, t->bar_x + line, t->bar_y + line, t->bar_w - 2 * line, t->bar_h - 2 * line, corner > line ? corner - line : 0,
                         ui_tone(ui_role(UI_SURFACE_HIGHEST) & 0xffffffu, (unsigned int)alpha));
                baseline = t->bar_y + (t->bar_h - ui_capital(ui, 14)) / 2 + ui_capital(ui, 14);
                for (int k = 0; k < t->bar_count; k++)
                {
                        const char address_to label = ui_text_action_label(t->bar_id[k]);
                        int left = t->bar_x + t->bar_at[k], right = t->bar_x + t->bar_at[k + 1], wide = ui_kit_measure(ui, (string_address)label, UI_BODY_MEDIUM);

                        if (t->bar_down == k + 1)
                                ui_fill(ui, left + (k ? 0 : line), t->bar_y + line, right - left - (k + 1 == t->bar_count ? line : 0), t->bar_h - 2 * line,
                                        ui_tone(ui_role(UI_SELECTION) & 0xffffffu, (unsigned int)alpha * (ui_role(UI_SELECTION) >> 24) / 255));
                        if (k)
                                ui_fill(ui, left, t->bar_y + t->bar_h / 4, line, t->bar_h / 2, ui_tone(ui_role(UI_OUTLINE_VARIANT) & 0xffffffu, (unsigned int)alpha));
                        ui_kit_text(ui, (left + right - wide) / 2, baseline, (string_address)label, UI_BODY_MEDIUM,
                                    ((unsigned int)alpha << 24) | (ui_role(UI_ON_SURFACE) & 0xffffffu), 0);
                }
                t->bar_up = true;
                t->over_top = !t->over_bottom || t->bar_y < t->over_top ? t->bar_y : t->over_top;
                t->over_bottom = t->bar_y + t->bar_h > t->over_bottom ? t->bar_y + t->bar_h : t->over_bottom;
        }
}

// The overlay over the whole picture: a window draws a region at a time (the page's body under its own clip, a list's rows) and
// leaves the clip of the last, which would cut a handle in the title band above it; the overlay is drawn with the picture's whole
// height, in the band a repaint is drawing (a band is a repaint's, and kept), and the clip left as it was.
static fn ui_text_overlay(struct ui address_to ui, struct ui_text address_to t)
{
        int top = ui->top, bottom = ui->bottom;

        ui->top = 0, ui->bottom = ui->height;
        ui_text_overlay_draw(ui, t);
        ui->top = top, ui->bottom = bottom;
}

/*
        The text under a finger that is held still, or the word under a double tap (clicks 2) and the line under a triple (3):
        selected, with its handles, and the finger going on extends it by words (lines for a triple) until it lifts. A masked
        field has no words, so a word of it is all of it, as a double click takes it all.
*/
static p32 ui_text_pick(struct ui address_to ui, struct ui_text address_to t, p64 at, int clicks, b32 holding)
{
        p64 start = at, finish = at;

        if (clicks == 3)
        {
                p64 line = doc_line_of(&t->doc, at);

                start = doc_line_start(&t->doc, line), finish = doc_line_start(&t->doc, line + 1);
        }
        else if (t->masked)
                start = 0, finish = doc_length(&t->doc);
        else
                doc_word_at(&t->doc, at, &start, &finish);
        ui_drag_stop(&t->kinetic);
        doc_select_set(&t->doc, start, finish);
        t->finger = UI_TEXT_FINGER_SELECT, t->holding = holding, t->dragging = true, t->drag_box = false;
        t->handles = true, t->bar = false, t->tap.down = false;
        t->drag_kind = clicks == 3 ? 3 : 2, t->drag_start = start, t->drag_end = finish;
        ui_text_reveal(ui, t);
        ui_text_wake(t);
        return UI_TEXT_MOVED;
}

// A finger lands: on the bar's item it shows pressed; on a handle it takes that end; on the text it is a press (a tap, or the start of
// a scroll), a second tap a word and a third a line, and a caret where it landed when nothing is selected.
static p32 ui_text_finger_down(struct ui address_to ui, struct ui_text address_to t, int x, int y, positive now)
{
        int item = ui_text_bar_item(t, x, y), grab = item < 0 ? ui_text_handle_hit(ui, t, x, y) : 0, clicks;
        p64 at;
        b32 on;

        t->holding = false;
        if (item >= 0)
        {
                t->finger = UI_TEXT_FINGER_BAR, t->dragging = true, t->bar_down = item + 1;
                ui_press_begin(&t->tap, x, y, item, true);
                return UI_TEXT_MOVED;
        }
        if (grab)
        {
                const struct doc_selection address_to s = &t->doc.selections[t->doc.primary];
                int hx = 0, top = 0;

                ui_text_where(ui, t, grab == 2 ? doc_sel_end(s) : doc_sel_start(s), &hx, &top);
                // The other end stays; the finger holds this one where it took it, so the end does not jump to the finger.
                t->drag_kind = 1, t->drag_start = t->drag_end = grab == 2 ? doc_sel_start(s) : doc_sel_end(s);
                t->grab_dx = hx - x, t->grab_dy = top + t->line_height / 2 - y;
                ui_drag_stop(&t->kinetic);
                t->finger = UI_TEXT_FINGER_HANDLE, t->dragging = true, t->drag_box = false;
                ui_press_begin(&t->tap, x, y, 2, true);
                ui_text_wake(t);
                return UI_TEXT_MOVED;
        }
        if (!ui_text_inside(t, x, y))
                return 0;
        at = ui_text_hit(ui, t, x, y);
        clicks = ui_click_count(ui, &t->click, now, x, y);
        if (clicks >= 2)
                return ui_text_pick(ui, t, at, clicks, false);
        // On the selection, or on the caret of none, a tap shows the bar; anywhere else it puts the caret there, which a tap with
        // a selection waits for the lift to do (the finger may be about to scroll).
        on = doc_any_selected(&t->doc) ? ui_text_selected(t, at) : at == t->doc.selections[t->doc.primary].active;
        t->finger = UI_TEXT_FINGER_TEXT, t->dragging = true, t->tap_at = at;
        ui_drag_start(&t->kinetic, y, now);
        t->finger_left = 0;
        ui_press_begin(&t->tap, x, y, on, true);
        if (!doc_any_selected(&t->doc) && !on)
        {
                doc_select_set(&t->doc, at, at);
                t->handles = t->bar = false;
        }
        ui_text_wake(t);
        return UI_TEXT_MOVED;
}

// A finger that holds something moves.
static p32 ui_text_finger_move(struct ui address_to ui, struct ui_text address_to t, int x, int y, positive now)
{
        if (t->finger == UI_TEXT_FINGER_BAR)
        {
                int was = t->bar_down;

                ui_press_move(ui, &t->tap, x, y);
                t->bar_down = !t->tap.dragged && ui_text_bar_item(t, x, y) == t->tap.target ? t->tap.target + 1 : 0;
                return was != t->bar_down ? UI_TEXT_MOVED : 0;
        }
        if (t->finger == UI_TEXT_FINGER_TEXT)
        {
                // The text follows the finger, a line at a time as its pixels make lines; the bar goes as the press turns to a drag.
                b32 was = t->tap.dragged;
                int moved = ui_drag_follow(&t->kinetic, y, now);
                long lines = ui_text_lines_moved(t, -moved);
                b64 to = (b64)t->top_line + lines;

                ui_press_move(ui, &t->tap, x, y);
                if (lines)
                        t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
                return lines || was != t->tap.dragged ? UI_TEXT_MOVED : 0;
        }
        {
                // A handle's end, or a selection by words, follows the finger: the end is where the finger plus what it took the handle
                // by is, taken at the box's edge when it is past it. The view scrolls (ui_text_autoscroll) while the finger is in the
                // band at the box's edge, a finger's width in from it, the faster the further in.
                int handle = t->finger == UI_TEXT_FINGER_HANDLE, ex = x + (handle ? t->grab_dx : 0), ey = y + (handle ? t->grab_dy : 0);
                int band = ui_dev(ui, UI_TARGET) / 2, edge_x = ex < t->left ? t->left : ex >= t->right ? t->right - 1 : ex;
                int edge_y = ey < t->top ? t->top : ey >= t->bottom ? t->bottom - 1 : ey;

                if (handle)
                        ui_press_move(ui, &t->tap, x, y);
                t->drag_x = ex, t->drag_y = ey;
                t->edge.past = t->mode == UI_TEXT_FIELD ? (x < t->left + band ? x - (t->left + band) : x >= t->right - band ? x - (t->right - band) + 1 : 0)
                                 : y < t->top + band ? y - (t->top + band) : y >= t->bottom - band ? y - (t->bottom - band) + 1 : 0;
                ui_text_extend(ui, t, ui_text_hit(ui, t, edge_x, edge_y), t->edge.past == 0);
                return UI_TEXT_MOVED;
        }
}

// A finger lifts, or the compositor takes its press back (cancelled), or the lift is a long press's (hold).
static p32 ui_text_finger_up(struct ui address_to ui, struct ui_text address_to t, int x, int y, b32 cancelled, b32 hold, positive now)
{
        int finger = t->finger, tapped;
        p32 asks = UI_TEXT_MOVED;

        // A long press's left release that comes before its right button is the press taken back, and no lift: a press on the text
        // ends, one on a handle or the bar goes on to the lift that comes after it.
        if (hold && !t->holding)
        {
                if (finger == UI_TEXT_FINGER_TEXT)
                {
                        ui_press_end(&t->tap, true);
                        t->finger = 0, t->dragging = false;
                }
                return 0;
        }
        t->finger = 0, t->dragging = false, t->holding = false, t->edge.past = 0;
        if (finger == UI_TEXT_FINGER_TEXT)
        {
                tapped = ui_press_end(&t->tap, cancelled);
                if (tapped >= 0)
                {
                        // A tap on the selection is the bar's, to show or take away; anywhere else it puts the caret.
                        if (tapped)
                                t->bar = !t->bar;
                        else
                        {
                                if (doc_any_selected(&t->doc))
                                        doc_select_set(&t->doc, t->tap_at, t->tap_at);
                                t->handles = t->bar = false;
                        }
                        return asks;
                }
                return ui_drag_let_go(&t->kinetic, now) ? asks : 0;
        }
        if (finger == UI_TEXT_FINGER_HANDLE)
        {
                tapped = ui_press_end(&t->tap, cancelled);
                t->bar = tapped >= 0 ? !t->bar : true;
                if (tapped < 0 && doc_any_selected(&t->doc))
                        ui_text_copy(t, WINDOWS_CLIP_PRIMARY);
                return asks;
        }
        if (finger == UI_TEXT_FINGER_BAR)
        {
                tapped = ui_press_end(&t->tap, cancelled);
                t->bar_down = 0;
                if (tapped >= 0 && tapped < t->bar_count && tapped == ui_text_bar_item(t, x, y))
                {
                        int id = t->bar_id[tapped];

                        asks |= ui_text_menu_do(ui, t, id);
                        // Cut, Copy and Paste are done with the selection and the bar; Select All shows it over all of the text.
                        if (id != UI_TEXT_MENU_ALL || (asks & UI_TEXT_CHANGED))
                                t->bar = false;
                        if (asks & UI_TEXT_CHANGED)
                                t->handles = false;
                }
                return asks;
        }
        if (finger == UI_TEXT_FINGER_SELECT)
        {
                if (doc_any_selected(&t->doc))
                        ui_text_copy(t, WINDOWS_CLIP_PRIMARY);
                t->bar = true;
                return asks;
        }
        return 0;
}

// What a finger does on the widget: its left button's press, lift and cancel, the right button of its long press, and its moves.
static p32 ui_text_finger(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y,
                          positive now)
{
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

        if (event->type == WINDOW_EVENT_MOTION)
                return ui_text_finger_move(ui, t, x, y, now);
        if (event->a == 0x111)
        {
                // The long press's right button: on the text it selects the word; a finger on a handle or the bar only says it was
                // held, and that press's lift is what it was.
                if (!down)
                        return 0;
                if (t->finger == UI_TEXT_FINGER_HANDLE || t->finger == UI_TEXT_FINGER_BAR)
                {
                        t->holding = true;
                        return 0;
                }
                return ui_text_inside(t, x, y) ? ui_text_pick(ui, t, ui_text_hit(ui, t, x, y), 2, true) : 0;
        }
        if (down)
                return ui_text_finger_down(ui, t, x, y, now);
        return ui_text_finger_up(ui, t, x, y, (event->flags & WINDOW_KEY_CANCEL) != 0, (event->flags & WINDOW_KEY_HOLD) != 0, now);
}

/*
        The bar's coming and going as a tick sees it: it is wanted and the view is still and it was not shown (or the other way
        round), so the frame is drawn; and when it waits for the view to be still, when that is. 0 for no wait.
*/
static positive ui_text_bar_tick(struct ui_text address_to t, positive now, b32 address_to redraw)
{
        b32 want = t->bar && ui_text_bar_still(t, now);

        if (want != t->bar_want)
                *redraw = true;
        if (t->bar && now < t->quiet_at && t->finger != UI_TEXT_FINGER_HANDLE && t->finger != UI_TEXT_FINGER_SELECT && !t->kinetic.flying)
                return t->quiet_at;
        return 0;
}

/*
        A field's events, the one call an app makes for one: the keys while it has them, the pointer on its well (a
        press of any button there, and the motion and the release of a drag it began), the clipboard's answer to a
        paste it asked for, and the context menu it asks for, opened at once in menu (the app's popup; none: no
        menu). The wheel is never a field's. A press outside a field with the keys answers UI_TEXT_COMMIT, or UI_TEXT_CANCEL for
        a command or a secret, and is the app's still. Answers what changed (UI_TEXT_*); 0 when the event is not the field's,
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
                // A press outside a field that has the keys ends the typing, by the one rule for every field: a plain value is
                // kept (UI_TEXT_COMMIT), and a command or a secret is cancelled (UI_TEXT_CANCEL). The press is not the field's:
                // the app goes on with it, to the row or the button it is on.
                if ((event->flags & WINDOW_KEY_DOWN) && !ui_text_reach(ui, t, x, y))
                        return t->focused ? (t->masked || t->cancels ? UI_TEXT_CANCEL : UI_TEXT_COMMIT) : 0;
                if (!(event->flags & WINDOW_KEY_DOWN) && !t->dragging)
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

/*
        The same two, with the place the window's pointer is at (ui_kit.pointer_x/y, the last motion's, taken in ui_event_context):
        an app forwards the event and keeps no pointer of its own. The places are the ones ui_text_event and ui_field_event take.
*/
static p32 ui_text_send(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event)
{
        return ui_text_event(ui, t, event, ui_kit.pointer_x, ui_kit.pointer_y);
}

static p32 ui_field_send(struct ui address_to ui, struct ui_text address_to t, struct ui_popup address_to menu, const struct window_event address_to event)
{
        return ui_field_event(ui, t, menu, event, ui_kit.pointer_x, ui_kit.pointer_y);
}

/* ---- drawing ---- */
/*
        The text painters: ui_text_draw paints a text widget, ui_text_field_draw a field, ui_text_draw_line one line. The colours
        are the text's roles (ui_text_colour, ui_text_range_role).
*/

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
// A fill of a line's decoration (a highlight, a selection, a squiggle) cut to the widget's box across: it lies inside
// the box, not over the gutter or past the body's right edge.
static fn ui_text_fill(struct ui address_to ui, const struct ui_text address_to t, int x, int y, int width, int height, unsigned int tone)
{
        int x0 = x < t->left ? t->left : x, x1 = x + width > t->right ? t->right : x + width;

        if (x1 > x0)
                ui_fill(ui, x0, y, x1 - x0, height, tone);
}

static fn ui_text_draw_line(struct ui address_to ui, struct ui_text address_to t, const struct ui_text_layout address_to l, p64 line,
                            int y, int baseline, unsigned int ground)
{
        int origin = ui_text_origin(t, l);
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
                if (!l->bidi)
                        ui_text_fill(ui, t, origin + ui_text_x_of(l, range->start), y, ui_text_x_of(l, range->finish) - ui_text_x_of(l, range->start),
                                     t->line_height, 0xff000000u | window_mix(ground, ui_text_range_role(range->data) & 0xffffffu, 64));
                else
                        for (p32 k = 0; k < l->count; k++)
                                if (l->offset[k] >= range->start && l->offset[k] < range->finish)
                                        ui_text_fill(ui, t, origin + l->left[k], y, l->right[k] - l->left[k], t->line_height,
                                                     0xff000000u | window_mix(ground, ui_text_range_role(range->data) & 0xffffffu, 64));
        }
        // The selection: one rectangle a run of selected clusters that lie side by side (all of them on a line that reads one way), and a
        // sliver past the end of a line the selection goes on from, at the end the line runs to.
        for (p32 k = 0; k < l->count; k++)
        {
                int x0, x1;

                if (!ui_text_selected(t, l->offset[k]))
                        continue;
                x0 = l->bidi ? l->left[k] : l->x[k], x1 = l->bidi ? l->right[k] : l->x[k + 1];
                while (k + 1 < l->count && ui_text_selected(t, l->offset[k + 1]))
                {
                        int next0 = l->bidi ? l->left[k + 1] : l->x[k + 1], next1 = l->bidi ? l->right[k + 1] : l->x[k + 2];

                        if (next0 == x1)
                                x1 = next1;
                        else if (next1 == x0)
                                x0 = next0;
                        else
                                break;
                        k++;
                }
                ui_text_fill(ui, t, origin + x0, y, x1 - x0, t->line_height, ui_tone(selection & 0xffffffu, selection >> 24));
        }
        if (ui_text_selected(t, l->offset[l->count]) && t->mode != UI_TEXT_FIELD)
                ui_text_fill(ui, t, origin + l->x[l->count] - (l->base ? t->cell / 2 + ui_px(ui, 1) : 0), y, t->cell / 2 + ui_px(ui, 1), t->line_height,
                             ui_tone(selection & 0xffffffu, selection >> 24));

        if (t->mode != UI_TEXT_CODE || l->bidi)
                ui_run_flagged(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, origin, baseline, (string_address)l->shown,
                               t->size + t->zoom, text & 0xffffffu, 255, t->left, t->right, 0, ui_text_flags(t));
        else
        {
                // A run is the clusters of one role; each cluster of a single code point one cell is part of a run drawn
                // with font_cells, any other (marks, wide, emoji) a font_draw of its own at its cell.
                const font_face address_to faces[2] = {ui_text_face(ui, t), ui->regular};
                // The cells are drawn into the body alone: a target at the body's top left, so a cell past its left or right
                // edge is cut there and not at the window's (the gutter's numbers are not painted over).
                font_target target = {ui->pixels + (unsigned long)t->top * ui->pitch + t->left, (positive)ui->pitch, t->right - t->left,
                                      t->bottom - t->top};
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
                                                          (b32)((b64)(origin + l->x[k] - t->left) * 65536), baseline - t->top, ink & 0xffffffu,
                                                          FONT_UNKNOWN);
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

                                font_cells(ui->engine, faces, 2, codes, run, &grid, ui_text_em(ui, t), 0, &target, origin + l->x[start] - t->left,
                                           y + (t->line_height - grid.cell_height) / 2 - t->top, ink & 0xffffffu, paper);
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
                                ui_fill(ui, at, y, ui_px(ui, 1), t->line_height, guide);
                }
                // A selection's blanks, seen: a dot in a space's cell, a line along a tab's.
                for (p32 k = 0; k < l->count; k++)
                {
                        p8 c = l->shown[l->shown_at[k]];
                        int x0 = origin + l->x[k], x1 = origin + l->x[k + 1], mid = y + t->line_height / 2;

                        if ((c != ' ' && c != '\t') || !ui_text_selected(t, l->offset[k]) || x1 <= t->left || x0 >= t->right)
                                continue;
                        if (c == ' ')
                                ui_text_fill(ui, t, (x0 + x1) / 2 - ui_px(ui, 1) / 2, mid - ui_px(ui, 1) / 2, ui_px(ui, 1), ui_px(ui, 1), blank);
                        else
                                ui_text_fill(ui, t, x0 + t->cell / 4, mid, x1 - x0 - t->cell / 2, ui_px(ui, 1), blank);
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
                        ui_text_fill(ui, t, x, baseline + 2 * ui->scale / 100 + ((x / (2 * ui->scale / 100)) & 1) * ui->scale / 100, 1, ui_px(ui, 1), ui_tone(colour & 0xffffffu, 255));
        }
        // The carets on this line, in the accent, at the blink's cover (solid, faded or gone: ui_text_wake); a composition's
        // text after the primary, underlined. A bar caret stands on the edge between two characters, half in each one's
        // bearing: drawn from the edge rightwards it covered the next letter's stem (a caret before "beta" read "peta").
        if (t->focused && t->caret_fade < UI_CARET_FULL)
                for (positive s = 0; s < t->doc.selection_count; s++)
                {
                        p64 caret = t->doc.selections[s].active;
                        unsigned int cover = UI_CARET_FULL - t->caret_fade;

                        if (caret < l->offset[0] || caret > l->offset[l->count])
                                continue;
                        ui_fill(ui, origin + ui_text_x_of(l, caret) - (t->doc.overwrite ? 0 : ui_px(ui, 1)), y + ui_px(ui, 1),
                                t->doc.overwrite ? t->cell : ui_caret_px(ui),
                                t->line_height - 2 * ui->scale / 100,
                                ui_tone(ui_role(UI_PRIMARY) & 0xffffffu, t->doc.overwrite ? 110 * cover / UI_CARET_FULL : cover));
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

                        ui_text_fill(ui, t, x, baseline + 2 * ui->scale / 100, wide, ui_px(ui, 1), ui_tone(text & 0xffffffu, 255));
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
        // The gutter is at the start of the lines: the left, or the right where the window reads from the right.
        t->left = x + (ui_rtl() ? 0 : t->gutter), t->top = y, t->right = x + width - (ui_rtl() ? t->gutter : 0), t->bottom = y + height;
        t->well_left = x, t->well_top = y, t->well_right = x + width, t->well_bottom = y + height;
        // A field's caret is always in sight, in the box it is drawn in now (its text set by the app, or the box moved).
        if (t->mode == UI_TEXT_FIELD)
        {
                t->top = y + (height - t->line_height) / 2, t->bottom = t->top + t->line_height;
                // A drag past the field's edge is the tick's to scroll (ui_text_autoscroll): a draw in the middle of it does not
                // reveal the point, which sits past the edge, or the view would jump a quarter of the field at every draw.
                if (!(t->dragging && t->edge.past))
                        ui_text_reveal(ui, t);
        }
        // A code view's caret is in sight in the box it is drawn in: a box that changed size since the last draw (a window
        // shrunk, a zoom) reveals it once. The first draw leaves the view where it was put (a restored scroll), and a draw
        // with the same box leaves it where the reading put it.
        if (t->mode == UI_TEXT_CODE && t->laid_width > 0 && (width != t->laid_width || height != t->laid_height || t->line_height != t->laid_line))
                ui_text_reveal(ui, t);
        t->laid_width = width, t->laid_height = height, t->laid_line = t->line_height;
        if (!doc_length(&t->doc) && t->placeholder && !t->preedit_length)
        {
                unsigned int text = ui_text_colour(ui, UI_ON_SURFACE), alpha = 92;

                // On glass the placeholder is the quiet ink at full strength: the ink at 36% is 2.0 to 2.7:1 from the glass,
                // and the quiet ink is held at the readable threshold (ui_glass_quiet). The kit's surfaces keep the 36%.
                if (ui->glass)
                        text = ui_text_colour(ui, UI_ON_SURFACE_VARIANT), alpha = 255;
                const font_face address_to faces[2] = {ui_text_face(ui, t), ui->regular};
                int start = ui_rtl() ? t->right - ui_run_in(ui, faces, 0, 0, (string_address)t->placeholder, t->size + t->zoom, 0, 0, 0, ui->width, 0) : t->left;

                ui_run_in(ui, faces, start, t->top + m.baseline, (string_address)t->placeholder,
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
                                ui_round(ui, ui_mirror(x + t->cell / 4, t->cell / 2, x, width), row_y + (t->line_height - t->cell / 2) / 2, t->cell / 2, t->cell / 2, t->cell / 4,
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
                        // The numbers are right-aligned in the gutter, which is at the left of the lines, or left-aligned at their right.
                        ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, ui_rtl() ? t->right + t->cell : t->left - t->cell - wide,
                                  baseline, (string_address)number, t->size + t->zoom, ui_role(UI_ON_SURFACE) & 0xffffffu, current ? 220 : 110,
                                  ui_rtl() ? t->right : x, ui_rtl() ? x + width : t->left, 0);
                        if (doc_folded_at(&t->doc, line, &hidden_to))
                                ui_chevron(ui, ui_mirror(x + t->cell / 2 - t->cell / 3, 2 * (t->cell / 3) + 1, x, width) + t->cell / 3, row_y + t->line_height / 2, t->cell / 3,
                                           ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 160));
                }
                if (!t->wrap || t->mode == UI_TEXT_FIELD || l.bidi)
                {
                        ui_text_draw_line(ui, t, &l, line, row_y, baseline, line_ground);
                        row_y += t->line_height;
                        continue;
                }
                // Wrapped: the line cut where it would pass the box's width, at a blank when one is near, each row
                // after the first indented as the line is (ui_text_wrap_to, which the hand and the handles cut it by too).
                {
                        static struct ui_text_layout part;
                        p32 from = 0;
                        int room = t->right - t->left, indent = ui_text_wrap_indent(&l, room);

                        do
                        {
                                p32 to = ui_text_wrap_to(&l, from, room, indent);
                                int offset_x = l.x[from] - (from ? indent : 0);

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
