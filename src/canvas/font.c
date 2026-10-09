/*
        The text engine: TrueType outlines into coverage, coverage into pixels.

        One engine for every surface that draws text -- the terminal, the
        panel, the control panel -- in userspace, inside the program that
        includes it after lib.util.c. The kernel's fallback console keeps its
        8x16 face. No outline code is ever in ring 0, and this file refuses to
        compile there.

        A font is attacker input. It is read in place from the caller's bytes
        and never trusted: every read is bounded by the file, every count by
        the bytes that would have to hold it, and recursion, points, contours
        and components by fixed ceilings. A file this cannot read is refused
        whole at load or glyph by glyph, and nothing is ever read past its end.

        What it does, in the order the eye notices it:

            outlines    glyf, simple and composite (scale, two by two,
                        offsets scaled or not, point matching), loca short
                        and long, cmap formats 4 and 12, hmtx and hhea, head,
                        maxp, OS/2 metrics (typo metrics when USE_TYPO_METRICS
                        says so, hhea otherwise), post's isFixedPitch
            coverage    exact area: the signed area and cover of every edge
                        in every cell it crosses, a running sum along each
                        row, nonzero by the magnitude clamped at full
            blending    in linear light through the sRGB transfer curve, with
                        a contrast curve that thickens dark text the way
                        linear blending thins it, and stem darkening for
                        small text on request
            fitting     below hint_until pixels, the baseline, x-height and
                        cap height land on whole pixels by a piecewise
                        vertical map; nothing horizontal moves
            position    eight horizontal phases a pixel, in the cache key
            kerning     GPOS pair adjustment, formats 1 and 2, through
                        extension lookups, from the kern feature of the
                        default script; the kern table when GPOS has none
            cells       box drawing (U+2500-257F), block elements
                        (U+2580-259F) and braille (U+2800-28FF) drawn to the
                        cell, so neighbours join at every size
            cache       one 8-bit coverage atlas, shelf packed, keyed by face,
                        glyph, size, phase and style, emptied whole when full
            layout      UTF-8 through lib.util.c's decoder, advances,
                        kerning, a chain of faces for missing glyphs, line
                        metrics, and the terminal's monospace cell

        Refused at load: CFF and CFF2 outlines, variable fonts (fvar), and
        colour fonts with no outlines (CBDT, sbix). Not done: bytecode hinting
        (outlines are drawn unhinted, then fitted as above), COLR layers (the
        monochrome base glyphs are drawn), and shaping beyond one glyph per
        code point.

        THE ARITHMETIC, AND HOW FAR FROM EXACT EACH STEP IS

        Font units become pixels once, as the exact rational units * size /
        units per em, rounded to the nearest (halves away from zero) and never
        rounded again on the way: a composite is assembled in font units kept
        to 2^-14 (a transform's F2Dot14 product rounded there), and only the
        finished outline is scaled, to 1/1024 of a pixel. A point is therefore
        within 1/2048 px of where the font puts it at any size. Metrics,
        advances, kerning and the pen are the same rational in 16.16, so a
        line of a thousand glyphs drifts by under 1/100 of a pixel.

        A quadratic is cut into n equal steps of its parameter, the fewest
        that leave each piece with a second difference of at most FONT_FLAT
        1024ths on either axis. A piece bows from its chord by a quarter of
        that, 1/1024 px, and the points between pieces are the curve's own,
        rounded to the nearest 1024th by a forward difference carried to
        2^-32 of a unit (2^-8 of a unit off after the longest walk), so the
        drawn outline is never 3/2048 px from the true curve on either axis:
        under 1/256 px, and under one 8-bit level of any pixel's area.
        Integrating the quadratic analytically per cell would still end on
        the same 1/1024 grid, and would cost a square root at every cell
        crossing; this costs integer adds, and comes out the same bytes on
        all three machines.

        Coverage is exact for that polygon: each cell gets the cover (the
        height an edge spans in it, signed by direction) and the area (cover
        times the sum of the x where the edge enters and leaves), exact in
        integers at 1/1024 px, and the byte is the area of the pixel covered
        times 255 rounded to the nearest, halves up. Measured against an
        exact rendering of every glyph of three faces at 8 to 400 px, no
        byte is more than 2 levels from 255 times the true area.

        sRGB is the piecewise curve itself (the linear foot below 0.04045, the
        2.4 power above it, evaluated in double precision). Each byte's light
        is held in 16 bits, and the way back is a table on all 65536 of them,
        each entry the byte nearest in sRGB: every byte survives the round
        trip, and a blend is the nearest byte to its 16-bit linear mix.

        Eight phases: a glyph lands within 1/16 px of its exact position, so
        the space between two neighbours is off by at most 1/8 px. On a 4K
        screen seen at a desk (a 27 inch panel at 60 cm, a pixel subtends
        about 53 seconds of arc, and 24 to 32 inch panels are within 10
        percent of that) 1/8 px is under 7 seconds of arc, below the 10 of
        vernier acuity, the eye's finest judgement of alignment; four phases
        would allow 13.

        THE INTERFACE

        Sizes are pixels per em in 26.6 (16 px is 1024); positions,
        advances and metrics are pixels in 16.16 unless named in whole
        pixels; colours are 0xRRGGBB into xrgb8888.

        b32 font_load(font_face *face, const p8 *bytes, positive size, p32 index)
                Reads the font in bytes, which must outlive the face; index
                picks a face of a collection. 0, FONT_BAD for a file that is
                not a usable TrueType font, FONT_UNSUPPORTED for one that is
                a format out of scope.

        font_metrics font_metrics_at(const font_face *face, p32 size)
                Ascent, descent, gap, x-height, cap height and underline,
                and the monospace cell in whole pixels: its width, its
                height, and the baseline's distance from its top.

        p32 font_glyph(const font_face *face, p32 code)
                The glyph for a Unicode scalar, or 0 when the face has none.

        b32 font_advance(const font_face *face, p32 glyph, p32 size)
        b32 font_kerning(const font_face *face, p32 left, p32 right, p32 size)
                A glyph's advance, and the adjustment between two glyphs.

        fn font_start(font_engine *engine, p8 *atlas, p32 width, p32 height)
                Readies an engine (684 KB; one per thread) with the
                caller's width by height bytes for its atlas, which bounds
                the largest glyph it will draw by area. The settings at the
                top of font_engine may be changed between calls.

        b32 font_render(font_engine *engine, const font_face *face, p32 glyph,
                        p32 size, p32 phase, p32 flags, p8 *coverage,
                        positive stride, positive capacity, font_bitmap *box)
                One glyph at phase eighths of a pixel right of the pen into
                the caller's coverage rows, uncached. box says where the
                bitmap sits from the pen and the baseline; with
                FONT_TOO_LARGE it still says how much room the glyph needs.

        positive font_layout(font_engine *engine, const font_face *const *faces,
                             p32 count, const p8 *text, positive length,
                             p32 size, p32 flags, font_place *out, positive room)
                Places UTF-8 text on one line: each code point's face and
                glyph from the first face of the chain that has it, its pen
                position and advance. Answers how many it placed.

        b32 font_draw(font_engine *engine, const font_face *const *faces,
                      p32 count, const p8 *text, positive length, p32 size,
                      p32 flags, const font_target *target, b32 x, b32 baseline,
                      p32 colour, p32 background)
                Draws the run with its pen starting at x on the baseline row,
                and answers its advance.

        fn font_cell(font_engine *engine, const font_face *const *faces,
                     p32 count, p32 code, p32 cells, const font_metrics *grid,
                     p32 size, p32 flags, const font_target *target, b32 x,
                     b32 y, p32 colour, p32 background)
                Draws one code point centred in cells cells of the grid whose
                top left pixel is x, y, as the terminal does: box drawing,
                blocks and braille drawn to the cell, never kerned, and the
                whole cell's rectangle written in its background.

        background is the colour the text lies on when the caller knows it,
        which is what makes a glyph one table lookup a pixel; FONT_UNKNOWN
        reads every pixel under the glyph and blends with it instead.

        flags: FONT_UNHINTED draws the outline as the font gives it,
        FONT_DARKEN asks for stem darkening, FONT_UNKERNED leaves pairs
        alone.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/
#ifdef KERNEL_MODE
#error "font.c is userspace: the kernel console keeps its bitmap face"
#endif
#ifndef MOONWATER_CANVAS_FONT
#define MOONWATER_CANVAS_FONT

#define FONT_BAD -1
#define FONT_UNSUPPORTED -2
#define FONT_TOO_LARGE -3

#define FONT_UNHINTED 1
#define FONT_DARKEN 2
#define FONT_UNKERNED 4

#define FONT_DRAWN 0xffff
#define FONT_UNKNOWN 0xff000000u
#define FONT_PHASES 8
#define FONT_EXACT 14
#define FONT_ONE 1024
#define FONT_SHIFT 10
#define FONT_FLAT 4

/*      The ceilings that make a hostile file cost a bounded amount. Real
        fonts sit far under each: Noto's CJK glyphs have under a thousand
        points, and no shipping font nests composites four deep. */
#define FONT_POINTS 8192
#define FONT_CONTOURS 4096
#define FONT_DEPTH 8
#define FONT_COMPONENTS 512
#define FONT_SUBTABLES 256
#define FONT_EDGE 8192
#define FONT_BAND (1 << 16)
#define FONT_ROWS 1024
#define FONT_SLOTS 4096
#define FONT_SHELVES 512
#define FONT_LOOKUPS 16
#define FONT_PAIRS 64
#define FONT_MEMO 2048
#define FONT_GROUP 6144
#define FONT_NEAR 512
#define FONT_EVENTS 4096

#define FONT_TAG(a, b, c, d) ((p32)(a) << 24 | (p32)(b) << 16 | (p32)(c) << 8 | (p32)(d))

typedef struct
{
        const p8 address_to data;
        positive size;
        positive glyf, glyf_size, loca, hmtx, cmap, lookup_list, kern;
        p32 glyphs, metrics, units, id;
        p16 lookups[FONT_LOOKUPS];
        p8 lookup_count, long_loca, cmap_format, symbol, fixed;
        b16 ascent, descent, gap, x_height, cap_height, underline, thickness;
        p16 em_advance;
} font_face;

typedef struct
{
        b32 ascent, descent, gap, x_height, cap_height, underline, thickness;
        b32 cell_width, cell_height, baseline;
} font_metrics;

typedef struct
{
        const p8 address_to coverage;
        positive stride;
        b32 left, top, width, height;
} font_bitmap;

typedef struct
{
        p32 address_to pixels;
        positive stride;
        b32 width, height;
} font_target;

typedef struct
{
        p32 glyph, face;
        b32 x, advance;
} font_place;

typedef struct
{
        p16 y, height, x;
} font_shelf;

typedef struct
{
        p64 key, cell;
        p16 x, y, width, height;
        b16 left, top;
} font_slot;

typedef struct
{
        b64 x, y;
} font_point;

typedef struct
{
        p64 key;
        p32 pixel[256];
        p8 planes[4][256];
} font_pairing;

typedef struct
{
        p64 key;
        b32 value;
} font_memo;

typedef struct
{
        p32 hint_until;         // largest size in pixels that is fitted (24)
        p32 darken;             // darkening at small sizes, 1/64 pixel (12)
        p32 darken_until;       // size in pixels where it has faded out (32)
        p32 contrast;           // dark text's contrast boost, 1/64 (64)
        bool linear;            // blend in linear light (true)

        p8 address_to atlas;
        p32 atlas_width, atlas_height, shelf_count, shelf_bottom, slot_count;
        bool scratched;
        font_shelf shelves[FONT_SHELVES];
        font_slot slots[FONT_SLOTS];

        font_point points[FONT_POINTS];
        p8 on[FONT_POINTS];
        p16 ends[FONT_CONTOURS];
        p32 point_count, contour_count, components;
        b32 left_side;

        p32 cells[FONT_BAND];
        p64 touched[FONT_ROWS];
        b32 band, rows, stride, width, grain, x, y;
        b32 cell_x, cell_y;
        p32 cover, area, gathered;
        bool clip, gather, overflow;

        p8 ramp[256];
        p32 ramp_colour, ramp_contrast;
        bool ramp_ready;

        font_pairing pairs[FONT_PAIRS];
        font_memo glyph_memo[FONT_MEMO], kern_memo[FONT_MEMO];
} font_engine;

/*
        Reading the file. Every read answers 0 past the end, so a parser walks
        a damaged table into zeros instead of into memory that is not the
        font's; where a count would drive a long walk it is clamped to what
        the bytes could hold. Offsets are sums of 32-bit fields in a 64-bit
        position, which cannot wrap.
*/
static inline p32 font_u8(const font_face address_to face, positive at)
{
        return at < face->size ? face->data[at] : 0;
}

static inline p32 font_u16(const font_face address_to face, positive at)
{
        return at + 2 <= face->size
                   ? (p32)face->data[at] << 8 | face->data[at + 1]
                   : 0;
}

static inline b32 font_i16(const font_face address_to face, positive at)
{
        return (b16)font_u16(face, at);
}

static inline p32 font_u32(const font_face address_to face, positive at)
{
        return at + 4 <= face->size
                   ? (p32)face->data[at] << 24 | (p32)face->data[at + 1] << 16 |
                         (p32)face->data[at + 2] << 8 | face->data[at + 3]
                   : 0;
}

//      The first of count records stride bytes apart from table whose key,
//      bytes wide at offset into the record, is not below value.
static p32 font_search(const font_face address_to face, positive table, p32 count,
                       positive stride, positive offset, p32 bytes, p32 value)
{
        p32 low = 0;

        for (p32 high = count; low < high;)
        {
                p32 middle = low + (high - low) / 2;
                positive at = table + stride * middle + offset;

                if ((bytes == 4 ? font_u32(face, at) : font_u16(face, at)) < value)
                        low = middle + 1;
                else
                        high = middle;
        }
        return low;
}

//      value / denominator to the nearest, halves away from zero, and font
//      units to pixels in 16.16 by the same rule: the one rounding every
//      metric, advance and kerning value takes.
static inline b64 font_divide(b64 value, b64 denominator)
{
        b64 half = denominator / 2;

        return value >= 0 ? (value + half) / denominator : -((half - value) / denominator);
}

static inline b32 font_fixed(const font_face address_to face, b64 units, p32 size)
{
        return (b32)font_divide(units * size * 1024, face->units);
}

/*
        Loading: the table directory, the tables every glyph needs, and the
        few numbers worth keeping from the rest. Which cmap subtable serves
        is decided here once: format 12 over the whole of Unicode first, then
        format 4 over the BMP, then a symbol font's format 4.
*/
static fn font_kern_lookups(font_face address_to face, positive gpos)
{
        if (font_u16(face, gpos) != 1)
                return;

        positive scripts = gpos + font_u16(face, gpos + 4);
        positive features = gpos + font_u16(face, gpos + 6);
        positive langsys = 0;
        p32 count = font_u16(face, scripts);

        face->lookup_list = gpos + font_u16(face, gpos + 8);

        //      The default language of DFLT, else of latn, else of whichever
        //      script comes first. With none, every kern feature serves.
        for (p32 pass = 0; pass < 3 && !langsys; pass++)
                for (p32 i = 0; i < count && !langsys; i++)
                {
                        positive record = scripts + 2 + 6 * (positive)i;
                        p32 tag = font_u32(face, record);
                        positive script = scripts + font_u16(face, record + 4);
                        p32 language = font_u16(face, script);

                        if (language && (pass == 2 ||
                                         tag == (pass ? FONT_TAG('l', 'a', 't', 'n')
                                                      : FONT_TAG('D', 'F', 'L', 'T'))))
                                langsys = script + language;
                }

        p32 wanted = langsys ? font_u16(face, langsys + 4) : font_u16(face, features);

        for (p32 i = 0; i < wanted; i++)
        {
                p32 feature = langsys ? font_u16(face, langsys + 6 + 2 * (positive)i) : i;
                positive record = features + 2 + 6 * (positive)feature;

                if (font_u32(face, record) != FONT_TAG('k', 'e', 'r', 'n'))
                        continue;

                positive table = features + font_u16(face, record + 4);
                p32 lookups = font_u16(face, table + 2);

                for (p32 k = 0; k < lookups; k++)
                {
                        p32 lookup = font_u16(face, table + 4 + 2 * (positive)k), seen = 0;

                        while (seen < face->lookup_count && face->lookups[seen] != lookup)
                                seen++;
                        if (seen == face->lookup_count && seen < FONT_LOOKUPS)
                                face->lookups[face->lookup_count++] = (p16)lookup;
                }
        }
}

//      Where a glyph's data starts and stops in glyf, both zero when it has none.
static positive font_location(const font_face address_to face, p32 glyph, positive address_to stop)
{
        positive start = face->long_loca ? font_u32(face, face->loca + 4 * (positive)glyph)
                                         : 2 * (positive)font_u16(face, face->loca + 2 * (positive)glyph);

        *stop = face->long_loca ? font_u32(face, face->loca + 4 * (positive)glyph + 4)
                                : 2 * (positive)font_u16(face, face->loca + 2 * (positive)glyph + 2);
        return start;
}

//      The top of a glyph's outline as its header says, for a face whose
//      OS/2 table does not give an x-height or a cap height.
static b32 font_height_of(const font_face address_to face, p32 glyph)
{
        positive stop, start = font_location(face, glyph, &stop);

        return glyph && start + 10 <= stop && stop <= face->glyf_size
                   ? font_i16(face, face->glyf + start + 8)
                   : 0;
}

p32 font_glyph(const font_face address_to face, p32 code);

b32 font_load(font_face address_to face, const p8 address_to bytes, positive size,
              p32 index)
{
        static p32 faces_loaded;
        static const p32 tags[] = {
            FONT_TAG('c', 'm', 'a', 'p'), FONT_TAG('h', 'e', 'a', 'd'),
            FONT_TAG('h', 'h', 'e', 'a'), FONT_TAG('h', 'm', 't', 'x'),
            FONT_TAG('m', 'a', 'x', 'p'), FONT_TAG('l', 'o', 'c', 'a'),
            FONT_TAG('g', 'l', 'y', 'f'), FONT_TAG('O', 'S', '/', '2'),
            FONT_TAG('p', 'o', 's', 't'), FONT_TAG('G', 'P', 'O', 'S'),
            FONT_TAG('k', 'e', 'r', 'n'), FONT_TAG('f', 'v', 'a', 'r'),
            FONT_TAG('C', 'F', 'F', ' '), FONT_TAG('C', 'F', 'F', '2'),
            FONT_TAG('C', 'B', 'D', 'T'), FONT_TAG('s', 'b', 'i', 'x'),
        };
        enum { CMAP, HEAD, HHEA, HMTX, MAXP, LOCA, GLYF, OS2, POST, GPOS, KERN, FVAR, CFF, CFF2, CBDT, SBIX, TABLES };
        positive at[TABLES] = {0}, length[TABLES] = {0}, sfnt = 0;

        *face = (font_face){.data = bytes, .size = size};

        if (font_u32(face, 0) == FONT_TAG('t', 't', 'c', 'f'))
        {
                if (index >= font_u32(face, 8))
                        return FONT_BAD;
                sfnt = font_u32(face, 12 + 4 * (positive)index);
        }

        p32 version = font_u32(face, sfnt);
        if (version == FONT_TAG('O', 'T', 'T', 'O'))
                return FONT_UNSUPPORTED;
        if (version != 0x00010000 && version != FONT_TAG('t', 'r', 'u', 'e'))
                return FONT_BAD;

        p32 tables = font_u16(face, sfnt + 4);
        for (p32 i = 0; i < tables; i++)
        {
                positive record = sfnt + 12 + 16 * (positive)i;
                positive offset = font_u32(face, record + 8), span = font_u32(face, record + 12);

                for (p32 k = 0; k < TABLES; k++)
                        if (tags[k] == font_u32(face, record) && span && offset <= size &&
                            span <= size - offset)
                        {
                                at[k] = offset;
                                length[k] = span;
                        }
        }

        if (length[FVAR] ||
            (!length[GLYF] && (length[CFF] || length[CFF2] || length[CBDT] || length[SBIX])))
                return FONT_UNSUPPORTED;
        if (!length[GLYF] || !length[LOCA] || length[HEAD] < 54 || length[HHEA] < 36 ||
            length[MAXP] < 6 || !length[HMTX])
                return FONT_BAD;

        face->units = font_u16(face, at[HEAD] + 18);
        face->long_loca = (p8)font_u16(face, at[HEAD] + 50);
        face->glyphs = font_u16(face, at[MAXP] + 4);
        face->metrics = font_u16(face, at[HHEA] + 34);
        if (face->units < 16 || face->units > 16384 || face->long_loca > 1 ||
            !face->glyphs || !face->metrics)
                return FONT_BAD;

        //      A loca too short for maxp's count serves the glyphs it covers,
        //      and an hmtx too short for hhea's the metrics it holds.
        positive covered = length[LOCA] / (face->long_loca ? 4 : 2);
        if (covered < 2)
                return FONT_BAD;
        if (face->glyphs > covered - 1)
                face->glyphs = (p32)covered - 1;
        if (face->metrics > face->glyphs)
                face->metrics = face->glyphs;
        if (face->metrics > length[HMTX] / 4)
                face->metrics = (p32)(length[HMTX] / 4);
        if (!face->metrics)
                return FONT_BAD;

        face->glyf = at[GLYF];
        face->glyf_size = length[GLYF];
        face->loca = at[LOCA];
        face->hmtx = at[HMTX];
        face->kern = at[KERN];
        //      The id keys every cache, and engines on several threads may load
        //      faces at once: it is taken atomically, and never zero.
        do
                face->id = __atomic_add_fetch(&faces_loaded, 1, __ATOMIC_RELAXED) & 0xffff;
        while (!face->id);

        face->ascent = (b16)font_i16(face, at[HHEA] + 4);
        face->descent = (b16)font_i16(face, at[HHEA] + 6);
        face->gap = (b16)font_i16(face, at[HHEA] + 8);
        if (length[OS2] >= 78 && ((font_u16(face, at[OS2] + 62) & 0x80) ||
                                  (!face->ascent && !face->descent)))
        {
                face->ascent = (b16)font_i16(face, at[OS2] + 68);
                face->descent = (b16)font_i16(face, at[OS2] + 70);
                face->gap = (b16)font_i16(face, at[OS2] + 72);
        }
        if (length[OS2] >= 90 && font_u16(face, at[OS2]) >= 2)
        {
                face->x_height = (b16)font_i16(face, at[OS2] + 86);
                face->cap_height = (b16)font_i16(face, at[OS2] + 88);
        }
        if (length[POST] >= 16)
        {
                face->underline = (b16)font_i16(face, at[POST] + 8);
                face->thickness = (b16)font_i16(face, at[POST] + 10);
                face->fixed = font_u32(face, at[POST] + 12) != 0;
        }

        //      The character map. Ranked so that a later, better subtable
        //      replaces an earlier one and an equal one does not.
        p32 maps = length[CMAP] >= 4 ? font_u16(face, at[CMAP] + 2) : 0, best = 0;
        for (p32 i = 0; i < maps; i++)
        {
                positive record = at[CMAP] + 4 + 8 * (positive)i;
                p32 platform = font_u16(face, record), encoding = font_u16(face, record + 2);
                positive table = at[CMAP] + font_u32(face, record + 4);
                p32 format = font_u16(face, table);
                p32 rank = format == 12 && (platform == 0 || (platform == 3 && encoding == 10)) ? 3
                           : format == 4 && (platform == 0 || (platform == 3 && encoding == 1)) ? 2
                           : format == 4 && platform == 3 && encoding == 0 ? 1 : 0;

                if (rank > best)
                {
                        best = rank;
                        face->cmap = table;
                        face->cmap_format = (p8)format;
                        face->symbol = rank == 1;
                }
        }

        if (length[GPOS] >= 10)
                font_kern_lookups(face, at[GPOS]);

        if (!face->x_height)
                face->x_height = (b16)font_height_of(face, font_glyph(face, 'x'));
        if (!face->cap_height)
                face->cap_height = (b16)font_height_of(face, font_glyph(face, 'H'));
        p32 em = font_glyph(face, 'M');
        face->em_advance = (p16)font_u16(face, face->hmtx + 4 * (positive)(em < face->metrics ? em : face->metrics - 1));
        return 0;
}

/*
        The character map, by binary search in either format. A glyph the
        map names past maxp's count is no glyph at all.
*/
p32 font_glyph(const font_face address_to face, p32 code)
{
        positive table = face->cmap;
        p64 glyph = 0;

        //      A symbol font keeps its characters at U+F000 up; a byte asked
        //      for that it does not have is looked for there.
        if (face->symbol && code < 0x100)
        {
                font_face plain = *face;

                plain.symbol = 0;
                glyph = font_glyph(&plain, code);
                return glyph ? (p32)glyph : font_glyph(&plain, code | 0xf000);
        }

        if (face->cmap_format == 12 && table + 16 <= face->size)
        {
                p32 count = font_u32(face, table + 12);

                if (count > (face->size - table - 16) / 12)
                        count = (p32)((face->size - table - 16) / 12);

                p32 low = font_search(face, table + 16, count, 12, 4, 4, code);
                positive group = table + 16 + 12 * (positive)low;
                if (low < count && font_u32(face, group) <= code)
                        glyph = (p64)font_u32(face, group + 8) + code - font_u32(face, group);
        }
        else if (face->cmap_format == 4 && code <= 0xffff)
        {
                p32 segments = font_u16(face, table + 6) / 2;
                positive ends = table + 14, starts = ends + 2 * (positive)segments + 2;
                positive deltas = starts + 2 * (positive)segments, ranges = deltas + 2 * (positive)segments;
                p32 low = font_search(face, ends, segments, 2, 0, 2, code);

                if (low == segments)
                        return 0;

                p32 start = font_u16(face, starts + 2 * (positive)low);
                p32 delta = font_u16(face, deltas + 2 * (positive)low);
                p32 range = font_u16(face, ranges + 2 * (positive)low);

                if (code < start)
                        return 0;
                glyph = range ? font_u16(face, ranges + 2 * (positive)low + range + 2 * (positive)(code - start))
                              : code;
                if (glyph)
                        glyph = (glyph + delta) & 0xffff;
        }
        return glyph < face->glyphs ? (p32)glyph : 0;
}

/*
        Advances and kerning.
*/
static p32 font_advance_units(const font_face address_to face, p32 glyph)
{
        return font_u16(face, face->hmtx + 4 * (positive)(glyph < face->metrics ? glyph : face->metrics - 1));
}

b32 font_advance(const font_face address_to face, p32 glyph, p32 size)
{
        return font_fixed(face, font_advance_units(face, glyph), size);
}

static p32 font_bits(p32 format)
{
        p32 count = 0;

        for (format &= 0xff; format; format &= format - 1)
                count++;
        return count;
}

//      Where glyph sits in a coverage table, or -1.
static b32 font_coverage(const font_face address_to face, positive table, p32 glyph)
{
        p32 format = font_u16(face, table), count = font_u16(face, table + 2);

        if (format != 1 && format != 2)
                return -1;

        p32 low = font_search(face, table + 4, count, format == 1 ? 2 : 6, format == 1 ? 0 : 2, 2, glyph);
        positive at = table + 4 + (format == 1 ? 2 : 6) * (positive)low;
        if (low == count)
                return -1;
        if (format == 1)
                return font_u16(face, at) == glyph ? (b32)low : -1;
        return font_u16(face, at) <= glyph
                   ? (b32)(font_u16(face, at + 4) + glyph - font_u16(face, at))
                   : -1;
}

//      The class glyph belongs to in a class definition table; 0 when none.
static p32 font_class(const font_face address_to face, positive table, p32 glyph)
{
        p32 format = font_u16(face, table);

        if (format == 1)
        {
                p32 first = font_u16(face, table + 2);

                return glyph >= first && glyph - first < font_u16(face, table + 4)
                           ? font_u16(face, table + 6 + 2 * (positive)(glyph - first))
                           : 0;
        }
        if (format != 2)
                return 0;

        p32 count = font_u16(face, table + 2);
        p32 low = font_search(face, table + 4, count, 6, 2, 2, glyph);
        positive at = table + 4 + 6 * (positive)low;
        return low < count && font_u16(face, at) <= glyph ? font_u16(face, at + 4) : 0;
}

//      One pair adjustment subtable: whether it applies to the pair, and the
//      first glyph's x advance from it when it does. A subtable that covers
//      the first glyph but not the pair lets the next one try.
static bool font_pair(const font_face address_to face, positive table, p32 left,
                      p32 right, b32 address_to value)
{
        p32 format = font_u16(face, table), first = font_u16(face, table + 4);
        positive record = 2 * (positive)(font_bits(first) + font_bits(font_u16(face, table + 6)));
        b32 index = font_coverage(face, table + font_u16(face, table + 2), left);
        positive at;

        if (index < 0)
                return false;
        if (format == 1)
        {
                if ((p32)index >= font_u16(face, table + 8))
                        return false;

                positive set = table + font_u16(face, table + 10 + 2 * (positive)index);
                p32 count = font_u16(face, set);
                p32 low = font_search(face, set + 2, count, record + 2, 0, 2, right);

                at = set + 2 + (record + 2) * low;
                if (low == count || font_u16(face, at) != right)
                        return false;
                at += 2;
        }
        else if (format == 2)
        {
                p32 one = font_class(face, table + font_u16(face, table + 8), left);
                p32 two = font_class(face, table + font_u16(face, table + 10), right);
                p32 ones = font_u16(face, table + 12), twos = font_u16(face, table + 14);

                if (one >= ones || two >= twos)
                        return false;
                at = table + 16 + ((positive)one * twos + two) * record;
        }
        else
                return false;

        if (first & 4)
                *value += font_i16(face, at + 2 * (positive)font_bits(first & 3));
        return true;
}

static b32 font_kerning_units(const font_face address_to face, p32 left, p32 right)
{
        b32 value = 0;

        for (p32 i = 0; i < face->lookup_count; i++)
        {
                if (face->lookups[i] >= font_u16(face, face->lookup_list))
                        continue;

                positive lookup = face->lookup_list +
                                  font_u16(face, face->lookup_list + 2 + 2 * (positive)face->lookups[i]);
                p32 type = font_u16(face, lookup), count = font_u16(face, lookup + 4);

                for (p32 k = 0; k < count && k < FONT_SUBTABLES; k++)
                {
                        positive table = lookup + font_u16(face, lookup + 6 + 2 * (positive)k);
                        p32 kind = type;

                        if (type == 9)
                        {
                                kind = font_u16(face, table + 2);
                                table += font_u32(face, table + 4);
                        }
                        if (kind == 2 && font_pair(face, table, left, right, &value))
                                break;
                }
        }

        //      The Microsoft kern table when GPOS kerns nothing: the first
        //      horizontal format 0 subtable, neither minimums nor cross-stream.
        if (!face->lookup_count && face->kern && !font_u16(face, face->kern))
        {
                positive at = face->kern + 4;

                for (p32 t = font_u16(face, face->kern + 2); t && at < face->size; t--)
                {
                        p32 length = font_u16(face, at + 2);

                        if ((font_u16(face, at + 4) & 0xff07) == 1)
                        {
                                p32 count = font_u16(face, at + 6), key = left << 16 | right;
                                p32 low = font_search(face, at + 14, count, 6, 0, 4, key);

                                if (low < count && font_u32(face, at + 14 + 6 * (positive)low) == key)
                                        value = font_i16(face, at + 18 + 6 * (positive)low);
                                break;
                        }
                        if (length < 6)
                                break;
                        at += length;
                }
        }
        return value;
}

b32 font_kerning(const font_face address_to face, p32 left, p32 right, p32 size)
{
        b32 value = font_kerning_units(face, left, right);

        return value ? font_fixed(face, value, size) : 0;
}

//      Whole pixels from 16.16, up or to the nearest, for the cell.
static inline b32 font_ceiling(b32 fixed)
{
        return (b32)(((b64)fixed + 0xffff) >> 16);
}

font_metrics font_metrics_at(const font_face address_to face, p32 size)
{
        font_metrics metrics = {
            .ascent = font_fixed(face, face->ascent, size),
            .descent = font_fixed(face, face->descent, size),
            .gap = font_fixed(face, face->gap, size),
            .x_height = font_fixed(face, face->x_height, size),
            .cap_height = font_fixed(face, face->cap_height, size),
            .underline = font_fixed(face, face->underline, size),
            .thickness = font_fixed(face, face->thickness, size),
        };
        b32 above = font_ceiling(metrics.ascent), below = font_ceiling(-metrics.descent);
        b32 gap = metrics.gap > 0 ? (metrics.gap + 0x8000) >> 16 : 0;

        metrics.cell_width = (font_fixed(face, face->em_advance, size) + 0x8000) >> 16;
        metrics.cell_width = metrics.cell_width < 1 ? 1 : metrics.cell_width;
        metrics.cell_height = above + below + gap;
        metrics.cell_height = metrics.cell_height < 1 ? 1 : metrics.cell_height;
        metrics.baseline = above + gap / 2;
        return metrics;
}

/*
        One glyph's outline, appended to the engine's points in font units
        kept to 2^-14, y up. A composite loads each component after the
        points already there, transforms them by its F2Dot14 matrix, and
        moves them by its offset -- scaled by the matrix too when the
        component asks -- or by matching one of its points to one before it.
*/
static inline b64 font_exact(b64 value)
{
        return value > ((b64)1 << 36) ? (b64)1 << 36 : value < -((b64)1 << 36) ? -((b64)1 << 36) : value;
}

static b32 font_outline(font_engine address_to e, const font_face address_to face,
                        p32 glyph, p32 depth)
{
        if (glyph >= face->glyphs || depth > FONT_DEPTH || ++e->components > FONT_COMPONENTS)
                return FONT_BAD;

        positive stop, start = font_location(face, glyph, &stop);
        if (stop <= start + 10)
                return 0;
        if (stop > face->glyf_size)
                return FONT_BAD;

        positive at = face->glyf + start, limit = face->glyf + stop;
        b32 contours = font_i16(face, at);
        p32 first = e->point_count;

        if (!depth)
                e->left_side = font_i16(face, at + 2);

        if (contours >= 0)
        {
                p32 count = 0;

                if (e->contour_count + (p32)contours > FONT_CONTOURS)
                        return FONT_BAD;
                for (b32 k = 0; k < contours; k++)
                {
                        p32 last = font_u16(face, at + 10 + 2 * (positive)k);

                        if (last < count || first + last >= FONT_POINTS)
                                return FONT_BAD;
                        count = last + 1;
                        e->ends[e->contour_count++] = (p16)(first + last);
                }

                positive p = at + 10 + 2 * (positive)contours;
                p = p + 2 + font_u16(face, p);

                p32 i = 0;
                while (i < count && p < limit)
                {
                        p32 flag = font_u8(face, p++);
                        p32 repeat = flag & 8 ? font_u8(face, p++) : 0;

                        for (p32 r = 0; r <= repeat && i < count; r++)
                                e->on[first + i++] = (p8)flag;
                }
                if (i < count)
                        return FONT_BAD;

                for (p32 axis = 0; axis < 2; axis++)
                {
                        p32 brief = axis ? 4 : 2, same = axis ? 32 : 16;
                        b64 value = 0;

                        for (i = first; i < first + count; i++)
                        {
                                if (e->on[i] & brief)
                                {
                                        b64 step = font_u8(face, p++);
                                        value += e->on[i] & same ? step : -step;
                                }
                                else if (!(e->on[i] & same))
                                {
                                        value += font_i16(face, p);
                                        p += 2;
                                }
                                if (axis)
                                        e->points[i].y = value * (1 << FONT_EXACT);
                                else
                                        e->points[i].x = value * (1 << FONT_EXACT);
                        }
                }
                if (p > limit)
                        return FONT_BAD;

                for (i = first; i < first + count; i++)
                        e->on[i] &= 1;
                e->point_count = first + count;
                return 0;
        }

        positive p = at + 10;
        p32 flags;
        do
        {
                if (p + 4 > limit)
                        return FONT_BAD;

                flags = font_u16(face, p);
                p32 child = font_u16(face, p + 2);
                b64 one, two;
                p += 4;
                if (flags & 1)
                {
                        one = flags & 2 ? font_i16(face, p) : (b32)font_u16(face, p);
                        two = flags & 2 ? font_i16(face, p + 2) : (b32)font_u16(face, p + 2);
                        p += 4;
                }
                else
                {
                        one = flags & 2 ? (b8)font_u8(face, p) : (b32)font_u8(face, p);
                        two = flags & 2 ? (b8)font_u8(face, p + 1) : (b32)font_u8(face, p + 1);
                        p += 2;
                }

                b64 xx = 1 << 14, xy = 0, yx = 0, yy = 1 << 14;
                if (flags & 8)
                {
                        xx = yy = font_i16(face, p);
                        p += 2;
                }
                else if (flags & 0x40)
                {
                        xx = font_i16(face, p);
                        yy = font_i16(face, p + 2);
                        p += 4;
                }
                else if (flags & 0x80)
                {
                        xx = font_i16(face, p);
                        yx = font_i16(face, p + 2);
                        xy = font_i16(face, p + 4);
                        yy = font_i16(face, p + 6);
                        p += 8;
                }

                p32 from = e->point_count;
                b32 refused = font_outline(e, face, child, depth + 1);
                if (refused < 0)
                        return refused;

                if (flags & 0xc8)
                        for (p32 i = from; i < e->point_count; i++)
                        {
                                b64 x = e->points[i].x, y = e->points[i].y;

                                e->points[i].x = font_exact(font_divide(x * xx + y * xy, 1 << 14));
                                e->points[i].y = font_exact(font_divide(x * yx + y * yy, 1 << 14));
                        }

                b64 dx, dy;
                if (flags & 2)
                {
                        dx = one * (1 << FONT_EXACT);
                        dy = two * (1 << FONT_EXACT);
                        if ((flags & 0xc8) && (flags & 0x800))
                        {
                                f64 sx = (f64)xx * xx + (f64)xy * xy, sy = (f64)yy * yy + (f64)yx * yx;

                                dx = (b64)((f64)dx * __builtin_sqrt(sx) / 16384);
                                dy = (b64)((f64)dy * __builtin_sqrt(sy) / 16384);
                        }
                }
                else
                {
                        //      Point matching: one is a point of this
                        //      composite so far, two one of the component.
                        p32 mine = first + (p32)one, theirs = from + (p32)two;

                        if (one < 0 || two < 0 || mine >= from || theirs >= e->point_count)
                                return FONT_BAD;
                        dx = e->points[mine].x - e->points[theirs].x;
                        dy = e->points[mine].y - e->points[theirs].y;
                }
                for (p32 i = from; i < e->point_count; i++)
                {
                        e->points[i].x = font_exact(e->points[i].x + dx);
                        e->points[i].y = font_exact(e->points[i].y + dy);
                }
        } while (flags & 0x20);
        return 0;
}

/*
        Grid fitting, in 1024ths of a pixel. Below hint_until pixels the
        outline's heights move so that the baseline, the x-height and the cap
        height each fall on a whole pixel: the x-height zone is scaled about
        the baseline (and the descenders with it), the zone up to the cap
        height is stretched to meet both, and everything above moves with
        the cap height. An x-height rounds up from four tenths of a pixel,
        because at these sizes the larger x-height is the more legible one.
        Horizontal positions are not touched: phases and advances stay exact.
*/
static fn font_fit(font_engine address_to e, const font_face address_to face, p32 size)
{
        b64 low = font_divide((b64)face->x_height * size * (FONT_ONE / 64), face->units);
        b64 high = font_divide((b64)face->cap_height * size * (FONT_ONE / 64), face->units);
        b64 snapped = (low + FONT_ONE * 6 / 10) & ~(b64)(FONT_ONE - 1);
        b64 capped = (high + FONT_ONE / 2) & ~(b64)(FONT_ONE - 1);

        if (low < FONT_ONE / 2)
                return;
        snapped = snapped < FONT_ONE ? FONT_ONE : snapped;
        if (high <= low + FONT_ONE / 2 || capped <= snapped)
                high = 0;

        for (p32 i = 0; i < e->point_count; i++)
        {
                b64 y = e->points[i].y;

                if (y <= low)
                        y = font_divide(y * snapped, low);
                else if (high && y <= high)
                        y = snapped + font_divide((y - low) * (capped - snapped), high - low);
                else
                        y += high ? capped - high : snapped - low;
                e->points[i].y = y;
        }
}

/*
        Stem darkening: every point moves outwards along the bisector of its
        two edges, so each stem gains strength across its width. Which side
        is out comes from the outline's own winding, and a point between
        short edges moves no further than they are long, so a thin detail
        thickens without turning inside out.
*/
static fn font_embolden(font_engine address_to e, f32 strength)
{
        f64 area = 0;
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count; first = e->ends[c++] + 1u)
                for (p32 i = first; i <= e->ends[c]; i++)
                {
                        font_point a = e->points[i], b = e->points[i == e->ends[c] ? first : i + 1];

                        area += (f64)a.x * b.y - (f64)b.x * a.y;
                }

        f32 half = strength * (area < 0 ? 0.5f : -0.5f);
        first = 0;
        for (p32 c = 0; c < e->contour_count; first = e->ends[c++] + 1u)
        {
                p32 last = e->ends[c];
                if (last <= first)
                        continue;

                font_point before = e->points[last], here = e->points[first], start = here;
                for (p32 i = first; i <= last; i++)
                {
                        font_point after = i == last ? start : e->points[i + 1];
                        f32 ix = (f32)(here.x - before.x), iy = (f32)(here.y - before.y);
                        f32 ox = (f32)(after.x - here.x), oy = (f32)(after.y - here.y);
                        f32 in = __builtin_sqrtf(ix * ix + iy * iy), out = __builtin_sqrtf(ox * ox + oy * oy);
                        f32 sx = 0, sy = 0;

                        if (in > 0 && out > 0)
                        {
                                ix /= in, iy /= in, ox /= out, oy /= out;

                                f32 d = 1 + ix * ox + iy * oy;
                                f32 q = (ox * iy - oy * ix) * (half < 0 ? 1 : -1);
                                f32 l = in < out ? in : out;
                                if (d > 1.0f / 16)
                                {
                                        f32 limit = half < 0 ? -half : half;
                                        f32 k = limit * q <= l * d ? half / d : (half < 0 ? -l : l) / q;

                                        sx = -(iy + oy) * k;
                                        sy = (ix + ox) * k;
                                }
                        }
                        e->points[i].x = here.x + (b64)(sx < 0 ? sx - 0.5f : sx + 0.5f);
                        e->points[i].y = here.y + (b64)(sy < 0 ? sy - 0.5f : sy + 0.5f);
                        before = here;
                        here = after;
                }
        }
}

/*
        The rasterizer. Points arrive in 1024ths of a pixel with the bitmap's
        bottom left corner at the origin. Each cell a segment crosses gets
        its cover and its area, kept as one difference array a row -- 2048
        cover less area into the cell, area into the next -- so the running
        sum along the row is twice the covered area in 2^20ths of the pixel,
        2^21 for a whole one. A row holds that for a thousand overlapping
        contours before its 32 bits wrap, and a wrapped sum is only a wrong
        level, never a wrong address.

        Rows are drawn a band at a time, as many as FONT_BAND cells hold, so
        a glyph of any size needs no more memory than that: each band walks
        the outline again and drops what lies outside it. Edges left of the
        bitmap count as lying on its left edge, and edges right of it touch
        nothing, which is what lets a cell glyph run past its cell and be
        clipped exactly.

        A curve drawn this finely is mostly pieces shorter than a pixel, so
        the cell the path is in keeps its cover and area in hand, and a piece
        that stays inside it adds to them and nothing else; the cell is
        written only when the path leaves it.

        Most of a big glyph's row is no edge at all: the inside of a stem,
        the air beside it. Each row keeps a bit for every block of columns an
        edge touched, at least 8 wide and few enough to fit 64 bits, and the
        sweep fills an untouched block with the one value the running sum
        already holds. The sweep also leaves every cell it read at zero, so
        the next band and the next glyph start clean without a clearing pass.
*/
static inline fn font_cell_add(font_engine address_to e, b32 row, b32 column, b32 cover, b32 area)
{
        p32 address_to cells = e->cells + (positive)row * (positive)e->stride;

        cells[column] += (p32)(2 * FONT_ONE * cover - area);
        cells[column + 1] += (p32)area;
        e->touched[row] |= (p64)1 << (column >> e->grain) | (p64)1 << ((column + 1) >> e->grain);
}

static fn font_span(font_engine address_to e, b32 row, b32 xa, b32 xb, b32 cover)
{
        b32 column = xa >> FONT_SHIFT, last = xb >> FONT_SHIFT, mask = FONT_ONE - 1;

        if (column == last)
        {
                font_cell_add(e, row, column, cover, cover * ((xa & mask) + (xb & mask)));
                return;
        }

        //      The cover each column takes is its share of the x travelled,
        //      by a remainder walk: one division for the first column and
        //      one for the step, each part exact to the floor.
        bool right = xb > xa;
        b32 step = right ? 1 : -1, leave = right ? FONT_ONE : 0, enter = FONT_ONE - leave;
        b64 travel = right ? (b64)xb - xa : (b64)xa - xb;
        b64 size = cover < 0 ? -(b64)cover : cover;
        b64 first = right ? FONT_ONE - (xa & mask) : xa & mask;
        b64 done = size * first / travel, carry = size * first % travel;
        b64 lift = size * FONT_ONE / travel, spare = size * FONT_ONE % travel;
        b32 sign = cover < 0 ? -1 : 1, from = xa & mask;

        b32 part = (b32)done * sign;
        font_cell_add(e, row, column, part, part * (from + leave));
        for (column += step; column != last; column += step)
        {
                b64 piece = lift;

                carry += spare;
                if (carry >= travel)
                {
                        carry -= travel;
                        piece++;
                }
                done += piece;
                part = (b32)piece * sign;
                font_cell_add(e, row, column, part, part * FONT_ONE);
        }
        part = cover - (b32)done * sign;
        font_cell_add(e, row, last, part, part * (enter + (xb & mask)));
}

static inline fn font_floor_divide(b64 numerator, b64 denominator, b64 address_to quotient,
                                   b64 address_to remainder)
{
        *quotient = numerator / denominator;
        *remainder = numerator % denominator;
        if (*remainder < 0)
        {
                *quotient -= 1;
                *remainder += denominator;
        }
}

static fn font_flush(font_engine address_to e)
{
        if (e->cover | e->area)
                font_cell_add(e, e->cell_y, e->cell_x, (b32)e->cover, (b32)e->area);
        e->cover = e->area = 0;
}

/*
        Overlapping contours. The sum of signed areas is the union's area only
        where at most one contour covers a point: where two wound alike
        overlap and both their edges cross one pixel -- a composite's ogonek
        on its letter -- the clamped sum counts the shared part twice, and
        FreeType draws such a pixel up to half too dark (a font may flag the
        glyph, and few static ones do). So the rows where two contours wound
        alike have boxes that meet are drawn again exactly: cut into slabs at
        every end of an edge and every crossing of two, inside which no edge
        begins, ends or crosses another, so that the edges are in one order
        and the winding between neighbours is fixed; each run of nonzero
        winding is then a trapezoid, and its area in every pixel is
        integrated in closed form. The edges are those the clamped pass drew,
        gathered a band of rows at a time into the cells, which lie idle
        meanwhile; a row with more edges or crossings than the scratch holds
        keeps its clamped bytes.
*/
typedef struct
{
        b32 group[FONT_GROUP][4];
        p16 listed[2 * FONT_GROUP];
        p32 listing[34];
        f64 near[FONT_NEAR][5], along[FONT_NEAR], reach[FONT_NEAR];
        p32 foot[FONT_NEAR], active[FONT_NEAR], across[FONT_NEAR];
        f64 events[FONT_EVENTS];
        f64 cover[FONT_EDGE + 2];
} font_scratch;

_Static_assert(sizeof(font_scratch) <= FONT_BAND * sizeof(p32), "the union's scratch is the cells");

static fn font_gather(font_engine address_to e, b32 x0, b32 y0, b32 x1, b32 y1)
{
        b32 address_to edge = ((font_scratch address_to)e->cells)->group[e->gathered];

        if (e->gathered == FONT_GROUP)
        {
                e->overflow = true;
                return;
        }
        edge[0] = x0, edge[1] = y0, edge[2] = x1, edge[3] = y1;
        e->gathered++;
}

static fn font_edge(font_engine address_to e, b32 x0, b32 y0, b32 x1, b32 y1)
{
        b32 low = e->band * FONT_ONE, high = (e->band + e->rows) * FONT_ONE, wide = e->width * FONT_ONE;

        if (y0 == y1 || (y0 >= high && y1 >= high) || (y0 <= low && y1 <= low))
                return;
        if (e->gather)
        {
                font_gather(e, x0, y0, x1, y1);
                return;
        }

        //      A drawn cell's outline may run past the bitmap: split where
        //      the edge crosses either side of it. A glyph's never does.
        if (e->clip)
        {
                for (b32 side = 0; side <= wide; side += wide ? wide : 1)
                        if ((x0 < side) != (x1 < side) && x0 != side && x1 != side)
                        {
                                b32 y = y0 + (b32)(((b64)side - x0) * (y1 - y0) / ((b64)x1 - x0));

                                font_edge(e, x0, y0, side, y);
                                font_edge(e, side, y, x1, y1);
                                return;
                        }
                if (x0 >= wide && x1 >= wide)
                        return;
                if (x0 < 0 || x1 < 0)
                        x0 = x1 = 0;
        }

        b32 sign = 1;
        if (y0 > y1)
        {
                b32 t = x0;
                x0 = x1, x1 = t, t = y0, y0 = y1, y1 = t, sign = -1;
        }

        //      x where the edge crosses each row's boundary, to the floor,
        //      by a remainder walk: a whole row moves it by lift and spare
        //      over dy, a part row divides afresh, and the edge's own end
        //      needs no division at all -- so an edge inside one row, which
        //      is most of them at text sizes, divides never.
        b64 dx = (b64)x1 - x0, dy = (b64)y1 - y0, at = 0, carry = 0, lift = 0, spare = -1;
        b32 ya = y0 < low ? low : y0, top = y1 > high ? high : y1;

        if (ya != y0)
                font_floor_divide(dx * (ya - y0), dy, &at, &carry);

        b32 xa = x0 + (b32)at;
        while (ya < top)
        {
                b32 yb = (ya & ~(FONT_ONE - 1)) + FONT_ONE;

                if (ya & (FONT_ONE - 1) || yb > top)
                {
                        yb = yb > top ? top : yb;
                        if (yb == y1)
                                at = dx;
                        else
                                font_floor_divide(dx * (yb - y0), dy, &at, &carry);
                }
                else
                {
                        if (spare < 0)
                                font_floor_divide(dx * FONT_ONE, dy, &lift, &spare);
                        at += lift;
                        carry += spare;
                        if (carry >= dy)
                        {
                                carry -= dy;
                                at++;
                        }
                }

                b32 xb = x0 + (b32)at;
                font_span(e, (ya >> FONT_SHIFT) - e->band, xa, xb, (yb - ya) * sign);
                ya = yb;
                xa = xb;
        }
}

//      A piece inside one cell, the common case, adds to the cell in hand.
static inline fn font_piece(font_engine address_to e, b32 x0, b32 y0, b32 x1, b32 y1)
{
        b32 row = (y0 >> FONT_SHIFT) - e->band;

        if (!e->clip && !e->gather && !(((x0 ^ x1) | (y0 ^ y1)) >> FONT_SHIFT) && x0 >= 0 && y0 >= 0)
        {
                if ((p32)row >= (p32)e->rows || y0 == y1)
                        return;
                if (row != e->cell_y || x0 >> FONT_SHIFT != e->cell_x)
                {
                        font_flush(e);
                        e->cell_x = x0 >> FONT_SHIFT;
                        e->cell_y = row;
                }
                e->cover += (p32)(y1 - y0);
                e->area += (p32)((y1 - y0) * ((x0 & (FONT_ONE - 1)) + (x1 & (FONT_ONE - 1))));
                return;
        }
        font_edge(e, x0, y0, x1, y1);
}

//      A quadratic in n even steps, the fewest that leave each piece's
//      second difference at most FONT_FLAT. The point at step i is the start
//      plus (2 b i n + a i^2) / n^2, walked as a forward difference in 32.32:
//      each step adds the first difference, which adds the second.
static b64 font_ratio(b64 numerator, b64 denominator)
{
        b64 whole, rest;

        font_floor_divide(numerator, denominator, &whole, &rest);
        return whole * ((b64)1 << 32) + (rest << 32) / denominator;
}

static fn font_walker_start(b64 address_to at, b64 address_to step, b64 address_to bend,
                            b64 a, b64 b, b64 n, b64 square)
{
        *at = (b64)1 << 31;
        *step = font_ratio(2 * b * n + a, square);
        *bend = font_ratio(2 * a, square);
}

static fn font_conic(font_engine address_to e, b32 cx, b32 cy, b32 x, b32 y)
{
        b32 x0 = e->x, y0 = e->y;
        b32 low = e->band * FONT_ONE, high = (e->band + e->rows) * FONT_ONE;

        e->x = x;
        e->y = y;
        if ((y0 >= high && cy >= high && y >= high) || (y0 < low && cy < low && y < low))
                return;

        b64 ax = (b64)x0 - 2 * (b64)cx + x, ay = (b64)y0 - 2 * (b64)cy + y;
        b64 bow = ax < 0 ? -ax : ax, rise = ay < 0 ? -ay : ay;

        bow = bow > rise ? bow : rise;
        if (bow <= FONT_FLAT)
        {
                font_piece(e, x0, y0, x, y);
                return;
        }

        b64 n = (b64)__builtin_sqrt((f64)bow / FONT_FLAT);
        while (n * n * FONT_FLAT < bow)
                n++;

        b64 square = n * n, ux, sx, bx, uy, sy, by;
        font_walker_start(&ux, &sx, &bx, ax, (b64)cx - x0, n, square);
        font_walker_start(&uy, &sy, &by, ay, (b64)cy - y0, n, square);

        b32 px = x0, py = y0;
        for (b64 i = 1; i < n; i++)
        {
                ux += sx, sx += bx, uy += sy, sy += by;

                b32 qx = x0 + (b32)(ux >> 32), qy = y0 + (b32)(uy >> 32);
                font_piece(e, px, py, qx, qy);
                px = qx;
                py = qy;
        }
        font_piece(e, px, py, x, y);
}

static fn font_line(font_engine address_to e, b32 x, b32 y)
{
        font_piece(e, e->x, e->y, x, y);
        e->x = x;
        e->y = y;
}

//      The contours as TrueType writes them: an off-curve point between two
//      on-curve ones is a control, and two off-curve points in a row imply
//      the on-curve point midway.
static fn font_walk(font_engine address_to e)
{
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count; first = e->ends[c++] + 1u)
        {
                p32 last = e->ends[c], i = first, stop = last;
                font_point address_to p = e->points;
                b32 sx, sy, cx = 0, cy = 0;
                bool pending = false;

                if (last < first || last >= e->point_count)
                        break;
                if (e->on[first])
                {
                        sx = (b32)p[first].x, sy = (b32)p[first].y;
                        i = first + 1;
                }
                else if (e->on[last])
                {
                        sx = (b32)p[last].x, sy = (b32)p[last].y;
                        stop = last - 1;
                }
                else
                {
                        sx = (b32)((p[first].x + p[last].x) >> 1);
                        sy = (b32)((p[first].y + p[last].y) >> 1);
                }

                e->x = sx;
                e->y = sy;
                for (; i <= stop; i++)
                {
                        b32 x = (b32)p[i].x, y = (b32)p[i].y;

                        if (!e->on[i])
                        {
                                if (pending)
                                        font_conic(e, cx, cy, (cx + x) >> 1, (cy + y) >> 1);
                                cx = x, cy = y, pending = true;
                        }
                        else if (pending)
                        {
                                font_conic(e, cx, cy, x, y);
                                pending = false;
                        }
                        else
                                font_line(e, x, y);
                }
                if (pending)
                        font_conic(e, cx, cy, sx, sy);
                else
                        font_line(e, sx, sy);
        }
}

//      The byte for a running sum: twice the covered area in 2^20ths of
//      the pixel, its magnitude clamped at full, times 255 to the nearest.
static inline p8 font_level(p32 sum)
{
        p32 area = (b32)sum < 0 ? -sum : sum;

        area = area > 2 * FONT_ONE * FONT_ONE ? 2 * FONT_ONE * FONT_ONE : area;
        return (p8)((area * 255 + FONT_ONE * FONT_ONE) >> (2 * FONT_SHIFT + 1));
}

//      The area of a slab of height h left of X and right of an edge that
//      runs from xa at its foot to xb at its top.
static f64 font_left_of(f64 x, f64 xa, f64 xb, f64 h)
{
        f64 low = xa < xb ? xa : xb, high = xa < xb ? xb : xa;

        return x <= low ? 0 : x >= high ? h * (x - (xa + xb) / 2) : h * (x - low) * (x - low) / (2 * (high - low));
}

//      Sort count doubles ascending, by Shell's gaps: the events of a row.
static fn font_sort(f64 address_to v, p32 count)
{
        static const p32 gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};

        for (p32 g = 0; g < 8; g++)
                for (p32 i = gaps[g]; i < count; i++)
                {
                        f64 t = v[i];
                        p32 j = i;

                        for (; j >= gaps[g] && v[j - gaps[g]] > t; j -= gaps[g])
                                v[j] = v[j - gaps[g]];
                        v[j] = t;
                }
}

static inline f64 font_at(const f64 address_to n, f64 y)
{
        return n[0] + (y - n[1]) * (n[2] - n[0]) / (n[3] - n[1]);
}

static fn font_union_row(font_engine address_to e, b32 r, b32 width, p8 address_to row)
{
        font_scratch address_to s = (font_scratch address_to)e->cells;
        f64 low = (f64)r * FONT_ONE, high = low + FONT_ONE;
        p32 near = 0, events = 2;

        //      The edges that cross the row, clipped to it, foot first, in
        //      the order of their feet.
        for (p32 at = s->listing[r - e->band]; at < s->listing[r - e->band + 1]; at++)
        {
                const b32 address_to g = s->group[s->listed[at]];
                bool up = g[1] < g[3];
                f64 xa = up ? g[0] : g[2], ya = up ? g[1] : g[3], xb = up ? g[2] : g[0], yb = up ? g[3] : g[1];

                if (yb <= low || ya >= high || ya == yb)
                        continue;
                if (near == FONT_NEAR)
                        return;

                f64 slope = (xb - xa) / (yb - ya), a = ya > low ? ya : low, b = yb < high ? yb : high;
                p32 j = near++;

                for (; j && s->near[s->foot[j - 1]][1] > a; j--)
                        s->foot[j] = s->foot[j - 1];
                s->foot[j] = near - 1;

                f64 address_to n = s->near[near - 1];
                n[0] = xa + (a - ya) * slope, n[1] = a, n[2] = xa + (b - ya) * slope, n[3] = b;
                n[4] = up ? 1 : -1;
        }
        if (!near)
                return;

        //      The slabs: the row's edges, every end, every crossing of two
        //      edges, which can only be two whose reaches along x meet: taken
        //      in the order of where they begin, each against those that begin
        //      before it ends.
        s->events[0] = low, s->events[1] = high;
        for (p32 i = 0; i < near; i++)
        {
                f64 address_to a = s->near[i];
                f64 begin = a[0] < a[2] ? a[0] : a[2];
                p32 j = i;

                for (; j && s->reach[j - 1] > begin; j--)
                {
                        s->reach[j] = s->reach[j - 1];
                        s->across[j] = s->across[j - 1];
                }
                s->reach[j] = begin;
                s->across[j] = i;
        }
        for (p32 o = 0; o < near; o++)
        {
                f64 address_to a = s->near[s->across[o]];
                f64 stop = a[0] < a[2] ? a[2] : a[0];

                if (events + 2 > FONT_EVENTS)
                        return;
                s->events[events++] = a[1];
                s->events[events++] = a[3];
                for (p32 q = o + 1; q < near && s->reach[q] <= stop; q++)
                {
                        f64 address_to b = s->near[s->across[q]];
                        f64 from = a[1] > b[1] ? a[1] : b[1], to = a[3] < b[3] ? a[3] : b[3];

                        if (to <= from)
                                continue;

                        f64 start = font_at(a, from) - font_at(b, from), finish = font_at(a, to) - font_at(b, to);

                        if ((start < 0 && finish > 0) || (start > 0 && finish < 0))
                        {
                                if (events == FONT_EVENTS)
                                        return;
                                s->events[events++] = from + (to - from) * start / (start - finish);
                        }
                }
        }
        font_sort(s->events, events);

        for (b32 c = 0; c <= width; c++)
                s->cover[c] = 0;

        //      A sweep up the row: the edges spanning each slab, kept in order
        //      along it from one slab to the next.
        p32 count = 0, next = 0;
        for (p32 k = 0; k + 1 < events; k++)
        {
                f64 a = s->events[k], b = s->events[k + 1], middle = (a + b) / 2;
                b32 winding = 0, start = 0;

                if (b <= a)
                        continue;

                p32 kept = 0;
                for (p32 i = 0; i < count; i++)
                        if (s->near[s->active[i]][3] > middle)
                                s->active[kept++] = s->active[i];
                count = kept;
                for (; next < near && s->near[s->foot[next]][1] < middle; next++)
                        if (s->near[s->foot[next]][3] > middle)
                                s->active[count++] = s->foot[next];
                for (p32 i = 0; i < count; i++)
                {
                        p32 edge = s->active[i], j = i;
                        f64 x = font_at(s->near[edge], middle);

                        for (; j && s->along[j - 1] > x; j--)
                        {
                                s->along[j] = s->along[j - 1];
                                s->active[j] = s->active[j - 1];
                        }
                        s->along[j] = x;
                        s->active[j] = edge;
                }

                for (p32 i = 0; i < count; i++)
                {
                        f64 address_to n = s->near[s->active[i]];
                        b32 before = winding;

                        winding += (b32)n[4];
                        if (!before && winding)
                                start = (b32)i;
                        if (!before || winding)
                                continue;

                        //      A run of nonzero winding, from edge start to edge
                        //      i: a trapezoid, added to each pixel it lies over.
                        f64 address_to l = s->near[s->active[start]];
                        f64 la = font_at(l, a), lb = font_at(l, b), ra = font_at(n, a), rb = font_at(n, b);
                        f64 least = la < lb ? la : lb, most = ra > rb ? ra : rb;
                        b32 first = least < 0 ? 0 : (b32)(least / FONT_ONE);
                        b32 last = most / FONT_ONE >= width ? width - 1 : (b32)(most / FONT_ONE);

                        for (b32 c = first; c <= last; c++)
                        {
                                f64 x0 = (f64)c * FONT_ONE, x1 = x0 + FONT_ONE;

                                s->cover[c] += (font_left_of(x1, la, lb, b - a) - font_left_of(x1, ra, rb, b - a)) -
                                               (font_left_of(x0, la, lb, b - a) - font_left_of(x0, ra, rb, b - a));
                        }
                }
        }
        for (b32 c = 0; c < width; c++)
        {
                f64 area = s->cover[c] / ((f64)FONT_ONE * FONT_ONE);

                row[c] = (p8)(255 * (area < 0 ? 0 : area > 1 ? 1 : area) + 0.5);
        }
}

//      The rows where two contours wound alike may share area, the one case
//      where the clamped sum overstates: those their boxes share, from *low
//      up to *high. Two whose boxes do not meet share nothing; past 64
//      contours the question is not asked.
static bool font_overlapping(const font_engine address_to e, b64 address_to low, b64 address_to high)
{
        b64 box[64][4];
        b32 wound[64];
        p32 first = 0;

        *low = (b64)1 << 62, *high = -((b64)1 << 62);
        if (e->contour_count < 2 || e->contour_count > 64)
                return false;
        for (p32 c = 0; c < e->contour_count; first = e->ends[c++] + 1u)
        {
                f64 area = 0;

                box[c][0] = box[c][1] = (b64)1 << 62, box[c][2] = box[c][3] = -((b64)1 << 62);
                for (p32 i = first; i <= e->ends[c] && i < e->point_count; i++)
                {
                        const font_point address_to a = &e->points[i];
                        const font_point address_to b = &e->points[i == e->ends[c] ? first : i + 1];

                        area += (f64)a->x * b->y - (f64)b->x * a->y;
                        box[c][0] = a->x < box[c][0] ? a->x : box[c][0];
                        box[c][1] = a->y < box[c][1] ? a->y : box[c][1];
                        box[c][2] = a->x > box[c][2] ? a->x : box[c][2];
                        box[c][3] = a->y > box[c][3] ? a->y : box[c][3];
                }
                wound[c] = area < 0 ? -1 : 1;
                for (p32 k = 0; k < c; k++)
                        if (wound[k] == wound[c] && box[k][0] < box[c][2] && box[c][0] < box[k][2] &&
                            box[k][1] < box[c][3] && box[c][1] < box[k][3])
                        {
                                b64 from = box[k][1] > box[c][1] ? box[k][1] : box[c][1];
                                b64 to = box[k][3] < box[c][3] ? box[k][3] : box[c][3];

                                *low = from < *low ? from : *low;
                                *high = to > *high ? to : *high;
                        }
        }
        return *low <= *high;
}

//      Each gathered edge listed under every row of the band it crosses, by
//      a count and a prefix: what each row walks instead of all of them.
//      False when the lists would not fit, which leaves the band clamped.
static bool font_list(font_engine address_to e)
{
        font_scratch address_to s = (font_scratch address_to)e->cells;
        p32 total = 0;

        for (p32 pass = 0; pass < 2; pass++)
        {
                for (p32 r = 0; r <= (p32)e->rows + 1; r++)
                        s->listing[r] = pass ? s->listing[r] : 0;
                for (p32 i = 0; i < e->gathered; i++)
                {
                        const b32 address_to g = s->group[i];
                        b32 low = (g[1] < g[3] ? g[1] : g[3]) - e->band * FONT_ONE;
                        b32 high = (g[1] < g[3] ? g[3] : g[1]) - e->band * FONT_ONE;
                        b32 first = low < 0 ? 0 : low >> FONT_SHIFT;
                        b32 last = (high + FONT_ONE - 1) >> FONT_SHIFT;

                        last = last > e->rows ? e->rows : last;
                        for (b32 r = first; r < last; r++)
                        {
                                if (!pass)
                                        s->listing[r + 1]++;
                                else
                                        s->listed[s->listing[r]++] = (p16)i;
                        }
                }
                if (!pass)
                {
                        for (b32 r = 0; r < e->rows; r++)
                                s->listing[r + 1] += s->listing[r];
                        total = s->listing[e->rows];
                        if (total > 2 * FONT_GROUP)
                                return false;
                }
                for (b32 r = e->rows; r > 0 && pass; r--)
                        s->listing[r] = s->listing[r - 1];
                if (pass)
                        s->listing[0] = 0;
        }
        return true;
}

static fn font_union(font_engine address_to e, b32 width, b32 from, b32 to, b32 height,
                     p8 address_to out, positive stride)
{
        b32 most = 32;

        e->gather = true;
        for (e->band = from; e->band < to;)
        {
                e->rows = to - e->band < most ? to - e->band : most;
                e->gathered = 0;
                e->overflow = false;
                font_walk(e);
                if (e->overflow && e->rows > 1)
                {
                        most = e->rows / 2;
                        continue;
                }
                if (!e->overflow && font_list(e))
                        for (b32 r = e->band; r < e->band + e->rows; r++)
                                font_union_row(e, r, width, out + (positive)(height - 1 - r) * stride);
                e->band += e->rows;
                most = most < 16 ? most * 2 : 32;
        }
        e->gather = false;
        memory_zero(e->cells, sizeof(font_scratch));
}

static fn font_raster(font_engine address_to e, b32 width, b32 height,
                      p8 address_to out, positive stride)
{
        e->width = width;
        e->stride = width + 2;
        for (e->grain = 3; (width + 1) >> e->grain >= 64;)
                e->grain++;

        b32 most = FONT_BAND / e->stride < FONT_ROWS ? FONT_BAND / e->stride : FONT_ROWS;
        for (e->band = 0; e->band < height; e->band += e->rows)
        {
                e->rows = height - e->band < most ? height - e->band : most;
                e->cover = e->area = 0;
                e->cell_y = -1;
                font_walk(e);
                font_flush(e);

                for (b32 r = 0; r < e->rows; r++)
                {
                        p32 address_to cells = e->cells + (positive)r * (positive)e->stride, sum = 0;
                        p8 address_to row = out + (positive)(height - 1 - e->band - r) * stride;
                        p64 touched = e->touched[r];

                        e->touched[r] = 0;
                        for (b32 x = 0; x < width;)
                        {
                                b32 block = x >> e->grain, next = (block + 1) << e->grain;

                                next = next < width ? next : width;
                                if (!(touched >> block & 1))
                                {
                                        p8 level = font_level(sum);

                                        for (; x < next; x++)
                                                row[x] = level;
                                        continue;
                                }
                                for (; x < next; x++)
                                {
                                        sum += cells[x];
                                        cells[x] = 0;
                                        row[x] = font_level(sum);
                                }
                        }
                        cells[width] = cells[width + 1] = 0;
                }
        }
        b64 low, high;
        if (font_overlapping(e, &low, &high))
        {
                b32 from = (b32)(low >> FONT_SHIFT), to = (b32)((high + FONT_ONE - 1) >> FONT_SHIFT);

                font_union(e, width, from < 0 ? 0 : from, to > height ? height : to, height, out, stride);
        }
}

/*
        A glyph's outline at a size, phase and style: moved so that its left
        side bearing is the one hmtx gives (as FreeType moves it), scaled to
        1024ths of a pixel in one rounding, fitted and darkened, and moved so
        its bitmap's corner is the origin. box says where that bitmap sits
        from the pen.
*/
static b32 font_shape(font_engine address_to e, const font_face address_to face,
                      p32 glyph, p32 size, p32 phase, p32 style, font_bitmap address_to box)
{
        e->point_count = e->contour_count = e->components = 0;
        e->clip = false;
        *box = (font_bitmap){0};

        b32 refused = font_outline(e, face, glyph, 0);
        if (refused < 0 || !e->point_count)
                return refused;

        b64 lsb = glyph < face->metrics
                      ? font_i16(face, face->hmtx + 4 * (positive)glyph + 2)
                      : font_i16(face, face->hmtx + 4 * (positive)face->metrics +
                                           2 * (positive)(glyph - face->metrics));
        b64 shift = (lsb - e->left_side) * (1 << FONT_EXACT), units = (b64)face->units << FONT_EXACT;
        b64 nudge = (b64)phase * (FONT_ONE / FONT_PHASES);

        for (p32 i = 0; i < e->point_count; i++)
        {
                b64 x = font_exact(e->points[i].x + shift), y = e->points[i].y;

                e->points[i].x = font_divide(x * size * (FONT_ONE / 64), units) + nudge;
                e->points[i].y = font_divide(y * size * (FONT_ONE / 64), units);
        }
        if (style & 1)
                font_fit(e, face, size);
        if (style & 2)
        {
                b32 pixels = (b32)(size >> 6), until = (b32)e->darken_until;
                b32 amount = pixels <= 12 ? (b32)e->darken
                             : pixels >= until ? 0
                             : (b32)e->darken * (until - pixels) / (until - 12);
                if (amount > 0)
                        font_embolden(e, (f32)(FONT_ONE / 64) * amount);
        }

        b64 left = (b64)1 << 62, bottom = left, right = -left, top = -left;
        for (p32 i = 0; i < e->point_count; i++)
        {
                b64 x = e->points[i].x, y = e->points[i].y;

                left = x < left ? x : left;
                right = x > right ? x : right;
                bottom = y < bottom ? y : bottom;
                top = y > top ? y : top;
        }
        left >>= FONT_SHIFT;
        bottom >>= FONT_SHIFT;
        right = (right + FONT_ONE - 1) >> FONT_SHIFT;
        top = (top + FONT_ONE - 1) >> FONT_SHIFT;
        if (right - left > FONT_EDGE || top - bottom > FONT_EDGE)
                return FONT_TOO_LARGE;
        *box = (font_bitmap){.left = (b32)left, .top = (b32)top,
                             .width = (b32)(right - left), .height = (b32)(top - bottom)};

        for (p32 i = 0; i < e->point_count; i++)
        {
                e->points[i].x -= left * FONT_ONE;
                e->points[i].y -= bottom * FONT_ONE;
        }
        return 0;
}

static p32 font_style(const font_engine address_to e, p32 size, p32 flags)
{
        return (!(flags & FONT_UNHINTED) && size <= e->hint_until * 64 ? 1 : 0) |
               ((flags & FONT_DARKEN) && e->darken && size < e->darken_until * 64 ? 2 : 0);
}

b32 font_render(font_engine address_to e, const font_face address_to face, p32 glyph,
                p32 size, p32 phase, p32 flags, p8 address_to coverage,
                positive stride, positive capacity, font_bitmap address_to box)
{
        b32 refused = font_shape(e, face, glyph, size, phase % FONT_PHASES, font_style(e, size, flags), box);

        if (refused < 0 || !box->width || !box->height)
                return refused;
        if ((positive)box->width > stride ||
            (positive)(box->height - 1) * stride + (positive)box->width > capacity)
                return FONT_TOO_LARGE;
        box->coverage = coverage;
        box->stride = stride;
        font_raster(e, box->width, box->height, coverage, stride);
        return 0;
}

/*
        Cells drawn rather than read from a font: box drawing, block elements
        and braille, sized to the cell so that each joins its neighbours.

        A box drawing character is up to four arms from the middle of the
        cell to its edges, each none, light, heavy or double; the table below
        is the Unicode chart as two bits an arm, north, south, west and east.
        Light lines are a sixteenth of the cell's height (at least a pixel),
        heavy ones twice that, and a double is two light lines a light line
        apart, all on whole pixels so that a run of them is seamless. An arm
        reaches the middle by the rule a corner needs: each line of it runs to
        the near line of the arm across it on the same side, or round to the
        far line of the arm on the other side when its own side has none, so
        that doubles make their inner and outer corners and a single line
        meeting a double stops at the double's first line. The dashed lines
        are the same lines with gaps cut, the rounded corners and diagonals
        are outlines through the rasterizer, and so are the braille dots.
*/
#define FONT_ARMS(north, south, west, east) ((north) << 6 | (south) << 4 | (west) << 2 | (east))

static const p8 font_box_arms[0x80] = {
    FONT_ARMS(0, 0, 1, 1), FONT_ARMS(0, 0, 2, 2), FONT_ARMS(1, 1, 0, 0), FONT_ARMS(2, 2, 0, 0),
    FONT_ARMS(0, 0, 1, 1), FONT_ARMS(0, 0, 2, 2), FONT_ARMS(1, 1, 0, 0), FONT_ARMS(2, 2, 0, 0),
    FONT_ARMS(0, 0, 1, 1), FONT_ARMS(0, 0, 2, 2), FONT_ARMS(1, 1, 0, 0), FONT_ARMS(2, 2, 0, 0),
    FONT_ARMS(0, 1, 0, 1), FONT_ARMS(0, 1, 0, 2), FONT_ARMS(0, 2, 0, 1), FONT_ARMS(0, 2, 0, 2),
    FONT_ARMS(0, 1, 1, 0), FONT_ARMS(0, 1, 2, 0), FONT_ARMS(0, 2, 1, 0), FONT_ARMS(0, 2, 2, 0),
    FONT_ARMS(1, 0, 0, 1), FONT_ARMS(1, 0, 0, 2), FONT_ARMS(2, 0, 0, 1), FONT_ARMS(2, 0, 0, 2),
    FONT_ARMS(1, 0, 1, 0), FONT_ARMS(1, 0, 2, 0), FONT_ARMS(2, 0, 1, 0), FONT_ARMS(2, 0, 2, 0),
    FONT_ARMS(1, 1, 0, 1), FONT_ARMS(1, 1, 0, 2), FONT_ARMS(2, 1, 0, 1), FONT_ARMS(1, 2, 0, 1),
    FONT_ARMS(2, 2, 0, 1), FONT_ARMS(2, 1, 0, 2), FONT_ARMS(1, 2, 0, 2), FONT_ARMS(2, 2, 0, 2),
    FONT_ARMS(1, 1, 1, 0), FONT_ARMS(1, 1, 2, 0), FONT_ARMS(2, 1, 1, 0), FONT_ARMS(1, 2, 1, 0),
    FONT_ARMS(2, 2, 1, 0), FONT_ARMS(2, 1, 2, 0), FONT_ARMS(1, 2, 2, 0), FONT_ARMS(2, 2, 2, 0),
    FONT_ARMS(0, 1, 1, 1), FONT_ARMS(0, 1, 2, 1), FONT_ARMS(0, 1, 1, 2), FONT_ARMS(0, 1, 2, 2),
    FONT_ARMS(0, 2, 1, 1), FONT_ARMS(0, 2, 2, 1), FONT_ARMS(0, 2, 1, 2), FONT_ARMS(0, 2, 2, 2),
    FONT_ARMS(1, 0, 1, 1), FONT_ARMS(1, 0, 2, 1), FONT_ARMS(1, 0, 1, 2), FONT_ARMS(1, 0, 2, 2),
    FONT_ARMS(2, 0, 1, 1), FONT_ARMS(2, 0, 2, 1), FONT_ARMS(2, 0, 1, 2), FONT_ARMS(2, 0, 2, 2),
    FONT_ARMS(1, 1, 1, 1), FONT_ARMS(1, 1, 2, 1), FONT_ARMS(1, 1, 1, 2), FONT_ARMS(1, 1, 2, 2),
    FONT_ARMS(2, 1, 1, 1), FONT_ARMS(1, 2, 1, 1), FONT_ARMS(2, 2, 1, 1), FONT_ARMS(2, 1, 2, 1),
    FONT_ARMS(2, 1, 1, 2), FONT_ARMS(1, 2, 2, 1), FONT_ARMS(1, 2, 1, 2), FONT_ARMS(2, 1, 2, 2),
    FONT_ARMS(1, 2, 2, 2), FONT_ARMS(2, 2, 2, 1), FONT_ARMS(2, 2, 1, 2), FONT_ARMS(2, 2, 2, 2),
    FONT_ARMS(0, 0, 1, 1), FONT_ARMS(0, 0, 2, 2), FONT_ARMS(1, 1, 0, 0), FONT_ARMS(2, 2, 0, 0),
    FONT_ARMS(0, 0, 3, 3), FONT_ARMS(3, 3, 0, 0), FONT_ARMS(0, 1, 0, 3), FONT_ARMS(0, 3, 0, 1),
    FONT_ARMS(0, 3, 0, 3), FONT_ARMS(0, 1, 3, 0), FONT_ARMS(0, 3, 1, 0), FONT_ARMS(0, 3, 3, 0),
    FONT_ARMS(1, 0, 0, 3), FONT_ARMS(3, 0, 0, 1), FONT_ARMS(3, 0, 0, 3), FONT_ARMS(1, 0, 3, 0),
    FONT_ARMS(3, 0, 1, 0), FONT_ARMS(3, 0, 3, 0), FONT_ARMS(1, 1, 0, 3), FONT_ARMS(3, 3, 0, 1),
    FONT_ARMS(3, 3, 0, 3), FONT_ARMS(1, 1, 3, 0), FONT_ARMS(3, 3, 1, 0), FONT_ARMS(3, 3, 3, 0),
    FONT_ARMS(0, 1, 3, 3), FONT_ARMS(0, 3, 1, 1), FONT_ARMS(0, 3, 3, 3), FONT_ARMS(1, 0, 3, 3),
    FONT_ARMS(3, 0, 1, 1), FONT_ARMS(3, 0, 3, 3), FONT_ARMS(1, 1, 3, 3), FONT_ARMS(3, 3, 1, 1),
    FONT_ARMS(3, 3, 3, 3), 0, 0, 0,
    0, 0, 0, 0,
    FONT_ARMS(0, 0, 1, 0), FONT_ARMS(1, 0, 0, 0), FONT_ARMS(0, 0, 0, 1), FONT_ARMS(0, 1, 0, 0),
    FONT_ARMS(0, 0, 2, 0), FONT_ARMS(2, 0, 0, 0), FONT_ARMS(0, 0, 0, 2), FONT_ARMS(0, 2, 0, 0),
    FONT_ARMS(0, 0, 1, 2), FONT_ARMS(1, 2, 0, 0), FONT_ARMS(0, 0, 2, 1), FONT_ARMS(2, 1, 0, 0),
};

static fn font_fill(p8 address_to out, positive stride, b32 width, b32 height,
                    b32 x0, b32 y0, b32 x1, b32 y1, p8 value)
{
        x0 = x0 < 0 ? 0 : x0;
        y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > width ? width : x1;
        y1 = y1 > height ? height : y1;
        for (b32 y = y0; y < y1 && x0 < x1; y++)
                memory_fill(out + (positive)y * stride + x0, (b8)value, (positive)(x1 - x0));
}

//      The lines of an arm across size pixels: one range, or two for a double.
static p32 font_lines(p32 style, b32 light, b32 size, b32 lines[2][2])
{
        b32 width = style == 2 ? 2 * light : light;
        b32 start = (size - (style == 3 ? 3 * light : width)) / 2;

        lines[0][0] = start;
        lines[0][1] = start + width;
        lines[1][0] = start + 2 * light;
        lines[1][1] = start + 3 * light;
        return style == 3 ? 2 : 1;
}

//      An outline point for a drawn cell, from pixels with y down.
static fn font_put(font_engine address_to e, f32 x, f32 y, b32 height, bool on)
{
        if (e->point_count >= FONT_POINTS)
                return;
        e->points[e->point_count] = (font_point){(b64)(x * FONT_ONE + 0.5f), (b64)((height - y) * FONT_ONE + 0.5f)};
        e->on[e->point_count++] = on;
}

static fn font_close(font_engine address_to e)
{
        if (e->point_count && e->contour_count < FONT_CONTOURS)
                e->ends[e->contour_count++] = (p16)(e->point_count - 1);
}

//      A quarter of a circle about (cx, cy) from direction (ux, uy) to
//      (vx, vy): its two ends and its middle on the curve, and the two
//      controls between them at the radius that keeps each half tangent.
static fn font_quarter(font_engine address_to e, f32 cx, f32 cy, f32 radius,
                       f32 ux, f32 uy, f32 vx, f32 vy, b32 height)
{
        static const f32 cosines[5] = {1, 0.92387953f, 0.70710678f, 0.38268343f, 0};

        for (p32 k = 0; k < 5; k++)
        {
                f32 c = cosines[k], s = cosines[4 - k], r = radius * (k & 1 ? 1.08239220f : 1);

                font_put(e, cx + r * (ux * c + vx * s), cy + r * (uy * c + vy * s), height, !(k & 1));
        }
}

static bool font_drawn(font_engine address_to e, p32 code, b32 width, b32 height,
                       p8 address_to out, positive stride)
{
        b32 light = (height + 8) / 16;
        light = light < 1 ? 1 : light;
        e->point_count = e->contour_count = 0;
        e->clip = true;

        if (code >= 0x2800 && code <= 0x28ff)
        {
                f32 radius = (width < height / 2 ? width : height / 2) * 0.15f;
                radius = radius < 0.6f ? 0.6f : radius;

                for (p32 dot = 0; dot < 8; dot++)
                {
                        if (!(code & 1u << dot))
                                continue;

                        p32 column = dot >= 3 && dot != 6, row = dot >= 6 ? 3 : dot % 3;
                        f32 cx = width * (1 + 2 * column) / 4.0f, cy = height * (1 + 2 * row) / 8.0f;

                        //      Eight controls round an octagon, whose midpoints
                        //      are on the circle: a dot from quadratics alone.
                        for (p32 k = 0; k < 8; k++)
                        {
                                static const f32 around[8][2] = {
                                    {1, 0}, {0.70710678f, 0.70710678f}, {0, 1}, {-0.70710678f, 0.70710678f},
                                    {-1, 0}, {-0.70710678f, -0.70710678f}, {0, -1}, {0.70710678f, -0.70710678f}};
                                f32 r = radius * 1.08239220f;

                                font_put(e, cx + r * around[k][0], cy + r * around[k][1], height, false);
                        }
                        font_close(e);
                }
                font_raster(e, width, height, out, stride);
                return true;
        }

        if (code >= 0x2580 && code <= 0x259f)
        {
                //      Eighths: x0, x1, y0, y1 of the cell, a nibble each.
                static const p16 rectangles[0x16] = {
                    0x0804, 0x0878, 0x0868, 0x0858, 0x0848, 0x0838, 0x0828, 0x0818,
                    0x0808, 0x0708, 0x0608, 0x0508, 0x0408, 0x0308, 0x0208, 0x0108,
                    0x4808, 0, 0, 0, 0x0801, 0x7808,
                };
                static const p8 quadrants[10] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};
                p32 at = code - 0x2580;
                b32 mx = (width * 4 + 4) / 8, my = (height * 4 + 4) / 8;

                if (at >= 0x11 && at <= 0x13)
                        font_fill(out, stride, width, height, 0, 0, width, height, (p8)(64 * (at - 0x10)));
                else if (at < 0x16)
                {
                        p32 r = rectangles[at];

                        font_fill(out, stride, width, height, (width * (b32)(r >> 12) + 4) / 8,
                                  (height * (b32)(r >> 4 & 15) + 4) / 8, (width * (b32)(r >> 8 & 15) + 4) / 8,
                                  (height * (b32)(r & 15) + 4) / 8, 255);
                }
                else
                        for (p32 q = 0; q < 4; q++)
                                if (quadrants[at - 0x16] & 1u << q)
                                        font_fill(out, stride, width, height, q & 1 ? mx : 0, q & 2 ? my : 0,
                                                  q & 1 ? width : mx, q & 2 ? height : my, 255);
                return true;
        }

        if (code < 0x2500 || code > 0x257f)
                return false;

        p32 arms = font_box_arms[code - 0x2500];
        b32 vertical[2][2], horizontal[2][2];
        font_lines(1, light, width, vertical);
        font_lines(1, light, height, horizontal);
        f32 cx = (vertical[0][0] + vertical[0][1]) / 2.0f, cy = (horizontal[0][0] + horizontal[0][1]) / 2.0f;
        f32 half = light / 2.0f;

        if (code >= 0x256d && code <= 0x2570)
        {
                //      A rounded corner: the arc from the middle of one edge's
                //      line to the other's, and straight on to both edges.
                f32 sx = code == 0x256d || code == 0x2570 ? 1 : -1, sy = code <= 0x256e ? 1 : -1;
                f32 ex = sx > 0 ? width : 0, ey = sy > 0 ? height : 0;
                f32 r = ((ex - cx) * sx < (ey - cy) * sy ? (ex - cx) * sx : (ey - cy) * sy) - 1;

                //      A pixel short of the nearer edge, so that the last
                //      column or row is straight and joins its neighbour's
                //      line exactly.
                r = r < half ? half : r;
                f32 ox = cx + sx * r, oy = cy + sy * r, inner = r - half > 0 ? r - half : 0;

                font_put(e, ex, cy - sy * half, height, true);
                font_quarter(e, ox, oy, r + half, 0, -sy, -sx, 0, height);
                font_put(e, cx - sx * half, ey, height, true);
                font_put(e, cx + sx * half, ey, height, true);
                font_quarter(e, ox, oy, inner, -sx, 0, 0, -sy, height);
                font_put(e, ex, cy + sy * half, height, true);
                font_close(e);
                font_raster(e, width, height, out, stride);
                return true;
        }

        if (code >= 0x2571 && code <= 0x2573)
        {
                //      Corner to corner, carried a stroke past each corner so
                //      the clip makes the cell's edge, and wound alike so the
                //      cross's middle stays filled.
                f32 length = __builtin_sqrtf((f32)width * width + (f32)height * height);

                for (p32 k = 0; k < 2; k++)
                {
                        if ((code == 0x2571 && k) || (code == 0x2572 && !k))
                                continue;

                        f32 ax = 0, ay = k ? 0 : height, dx = width / length, dy = (k ? height : -height) / length;
                        f32 bx = width, by = k ? height : 0, nx = -dy * half, ny = dx * half;

                        ax -= dx * light, ay -= dy * light, bx += dx * light, by += dy * light;
                        font_put(e, ax - nx, ay - ny, height, true);
                        font_put(e, bx - nx, by - ny, height, true);
                        font_put(e, bx + nx, by + ny, height, true);
                        font_put(e, ax + nx, ay + ny, height, true);
                        font_close(e);
                }
                font_raster(e, width, height, out, stride);
                return true;
        }

        for (p32 arm = 0; arm < 4; arm++)
        {
                p32 style = arms >> (6 - 2 * arm) & 3;
                if (!style)
                        continue;

                bool across = arm >= 2, far = arm & 1;
                b32 along = across ? width : height, other = across ? height : width;
                p32 low = arms >> (across ? 6 : 2) & 3, high = arms >> (across ? 4 : 0) & 3;
                b32 own[2][2], lows[2][2], highs[2][2];
                p32 lines = font_lines(style, light, other, own);
                p32 nl = low ? font_lines(low, light, along, lows) : 0;
                p32 nh = high ? font_lines(high, light, along, highs) : 0;

                for (p32 k = 0; k < lines; k++)
                {
                        p32 sides = lines == 1 ? 3 : k ? 2 : 1;
                        b32 reach = far ? along : 0;

                        for (p32 side = 1; side <= 2; side++)
                        {
                                if (!(sides & side))
                                        continue;

                                b32(*mine)[2] = side == 1 ? lows : highs, (*theirs)[2] = side == 1 ? highs : lows;
                                p32 nm = side == 1 ? nl : nh, nt = side == 1 ? nh : nl;
                                b32 to = nm   ? (far ? mine[nm - 1][0] : mine[0][1])
                                         : nt ? (far ? theirs[0][0] : theirs[nt - 1][1])
                                              : (far ? along / 2 : (along + 1) / 2);

                                reach = far ? (to < reach ? to : reach) : (to > reach ? to : reach);
                        }

                        b32 a0 = far ? reach : 0, a1 = far ? along : reach;
                        if (across)
                                font_fill(out, stride, width, height, a0, own[k][0], a1, own[k][1], 255);
                        else
                                font_fill(out, stride, width, height, own[k][0], a0, own[k][1], a1, 255);
                }
        }

        //      Dashes: gaps cut evenly, half a gap at each end of each dash,
        //      so the pattern runs on into the next cell.
        p32 dashes = code <= 0x250b && code >= 0x2504 ? (code - 0x2504) / 4 ? 4 : 3
                     : code >= 0x254c && code <= 0x254f ? 2 : 0;
        if (dashes)
        {
                bool across = !(code & 2);
                b32 along = across ? width : height, gap = along / (b32)dashes / 4;
                gap = gap < 1 ? 1 : gap;

                for (b32 i = 0; i < (b32)dashes; i++)
                {
                        b32 from = along * i / (b32)dashes, to = along * (i + 1) / (b32)dashes;

                        for (p32 tail = 0; tail < 2; tail++)
                        {
                                b32 a = tail ? to - (gap - gap / 2) : from, b = tail ? to : from + gap / 2;

                                if (across)
                                        font_fill(out, stride, width, height, a, 0, b, height, 0);
                                else
                                        font_fill(out, stride, width, height, 0, a, width, b, 0);
                        }
                }
        }
        return true;
}

/*
        The cache. A slot is found by its key in an open-addressed table, and
        its coverage lives in the atlas: shelves of glyphs of about the same
        height, the tightest shelf with room taken, a new shelf opened when
        that would waste more than a quarter of the glyph's height. When the
        atlas or the table fills, both are emptied and filling starts over,
        which costs one frame's worth of glyphs and never a stale one.

        A glyph too big for even an empty atlas is drawn through the whole
        atlas as scratch, and the cache is emptied before its next use.
*/
static fn font_reset(font_engine address_to e)
{
        memory_zero(e->slots, sizeof(e->slots));
        e->slot_count = e->shelf_count = e->shelf_bottom = 0;
        e->scratched = false;
}

static bool font_room(font_engine address_to e, b32 width, b32 height, p16 address_to x, p16 address_to y)
{
        p32 w = (p32)width + 1, h = (p32)height + 1, best = FONT_SHELVES;

        if (w > e->atlas_width || h > e->atlas_height)
                return false;
        for (p32 s = 0; s < e->shelf_count; s++)
                if (e->shelves[s].height >= h && e->shelves[s].x + w <= e->atlas_width &&
                    (best == FONT_SHELVES || e->shelves[s].height < e->shelves[best].height))
                        best = s;

        p32 tall = (h + 3) & ~3u;
        if ((best == FONT_SHELVES || e->shelves[best].height > h + h / 4 + 1) &&
            e->shelf_count < FONT_SHELVES && e->shelf_bottom + h <= e->atlas_height)
        {
                best = e->shelf_count++;
                tall = e->shelf_bottom + tall <= e->atlas_height ? tall : h;
                e->shelves[best] = (font_shelf){(p16)e->shelf_bottom, (p16)tall, 0};
                e->shelf_bottom += tall;
        }
        if (best == FONT_SHELVES)
                return false;
        *x = e->shelves[best].x;
        *y = e->shelves[best].y;
        e->shelves[best].x += (p16)w;
        return true;
}

//      The cached coverage for a glyph, or for a drawn cell when face is
//      null, with code the code point and size the cell's width << 16 |
//      height. With cell, a glyph's whole terminal cell instead -- cell
//      packs its width, height, baseline and the glyph's pixel offset in
//      it, 16 bits each -- or nothing with stride 1 when the glyph does not
//      fit inside it. An empty bitmap when there is nothing to draw.
static font_bitmap font_cached(font_engine address_to e, const font_face address_to face,
                               p32 code, p32 size, p32 phase, p32 style, p64 cell)
{
        p64 key = (p64)1 << 63 | (p64)(face ? face->id : 0) << 47 | (p64)(code & 0xffff) << 31 |
                  (p64)(face ? size & 0x3fffff : (size >> 16 & 0x7ff) << 11 | (size & 0x7ff)) << 9 |
                  (p64)(phase & 7) << 6 | (p64)(style & 3) << 4;
        font_bitmap box = {0};

        if (e->scratched || e->slot_count >= FONT_SLOTS * 3 / 4)
                font_reset(e);

        p32 at = (p32)(((key ^ cell) * 0x9e3779b97f4a7c15ull) >> 52) & (FONT_SLOTS - 1);
        while (e->slots[at].key && (e->slots[at].key != key || e->slots[at].cell != cell))
                at = (at + 1) & (FONT_SLOTS - 1);

        font_slot address_to slot = &e->slots[at];
        if (slot->key == key)
                return (font_bitmap){slot->width ? e->atlas + (positive)slot->y * e->atlas_width + slot->x : null,
                                     slot->x == 0xffff ? 1 : e->atlas_width, slot->left, slot->top,
                                     slot->width, slot->height};

        b32 dx = 0, dy = 0;
        if (face)
        {
                if (font_shape(e, face, code, size, phase, style, &box) < 0)
                        return (font_bitmap){0};
                if (cell && box.width)
                {
                        //      Where the glyph's box falls in its cell, which
                        //      is all of the cell's bitmap there is to draw.
                        dx = (b16)cell + box.left;
                        dy = (b32)(cell >> 16 & 0xffff) - box.top;
                        box.left = box.top = 0;
                        if (dx < 0 || dy < 0 || dx + box.width > (b32)(cell >> 48) ||
                            dy + box.height > (b32)(cell >> 32 & 0xffff))
                        {
                                *slot = (font_slot){.key = key, .cell = cell, .x = 0xffff};
                                e->slot_count++;
                                return (font_bitmap){.stride = 1};
                        }
                }
        }
        else if ((size >> 16) < 0x800 && (size & 0xffff) < 0x800)
                box = (font_bitmap){.width = (b32)(size >> 16), .height = (b32)(size & 0xffff)};
        if (box.width <= 0 || box.height <= 0)
        {
                *slot = (font_slot){.key = key, .cell = cell};
                e->slot_count++;
                return (font_bitmap){0};
        }

        //      A cell's bitmap is the whole cell, the glyph at dx, dy in it.
        b32 inner_width = box.width, inner_height = box.height;
        if (cell)
        {
                box.width = (b32)(cell >> 48);
                box.height = (b32)(cell >> 32 & 0xffff);
        }

        p16 x, y;
        if (!font_room(e, box.width, box.height, &x, &y))
        {
                font_reset(e);
                if (!font_room(e, box.width, box.height, &x, &y))
                {
                        if ((positive)box.width * (positive)box.height >
                            (positive)e->atlas_width * e->atlas_height)
                                return (font_bitmap){0};
                        e->scratched = true;
                        box.coverage = e->atlas;
                        box.stride = (positive)box.width;
                        memory_zero(e->atlas, (positive)box.width * (positive)box.height);
                        if (face)
                                font_raster(e, inner_width, inner_height,
                                            e->atlas + (positive)dy * box.stride + dx, box.stride);
                        else
                                font_drawn(e, code, box.width, box.height, e->atlas, box.stride);
                        return box;
                }
                slot = &e->slots[(p32)(((key ^ cell) * 0x9e3779b97f4a7c15ull) >> 52) & (FONT_SLOTS - 1)];
        }

        box.coverage = e->atlas + (positive)y * e->atlas_width + x;
        box.stride = e->atlas_width;
        for (b32 r = 0; r < box.height; r++)
                memory_zero((p8 address_to)box.coverage + (positive)r * box.stride, (positive)box.width);
        if (face)
                font_raster(e, inner_width, inner_height,
                            (p8 address_to)box.coverage + (positive)dy * box.stride + dx, box.stride);
        else
                font_drawn(e, code, box.width, box.height, (p8 address_to)box.coverage, box.stride);

        *slot = (font_slot){key, cell, x, y, (p16)box.width, (p16)box.height, (b16)box.left, (b16)box.top};
        e->slot_count++;
        return box;
}

/*
        Blending. Coverage is light: a pixel half covered by white text on
        black should give half the light, which is sRGB 188 and not 128. So
        both colours go into linear light, mix there by coverage, and come
        back to the nearest byte. Mixing in linear light thins dark text on a
        light ground, so dark text first passes its coverage through a
        contrast curve, a(k + 1) / (ak + 1), with k falling to nothing as the
        text's own lightness rises to three quarters: light text keeps its
        coverage.

        The two tables are the program's, made once: 256 bytes into 16-bit
        light, and all 65536 lights back to the byte nearest in sRGB, that is
        the byte whose decision threshold, the light of the half step
        between two bytes, the light has not reached.
*/
static p16 font_to_linear[256];
static p8 font_to_srgb[65536];

static f64 font_srgb_to_linear(f64 x)
{
        if (x <= 0.04045)
                return x / 12.92;

        f64 a = (x + 0.055) / 1.055, square = a * a, root = 1;

        //      a to the 0.4, as the fifth root of a squared, from above.
        for (p32 i = 0; i < 48; i++)
                root = (4 * root + square / (root * root * root * root)) / 5;
        return square * root;
}

static fn font_tables()
{
        static p32 made;
        p32 none = 0;

        //      Made once for every engine of the program: the first to come
        //      makes them and the rest wait for the release that says so.
        if (__atomic_load_n(&made, __ATOMIC_ACQUIRE) == 2)
                return;
        if (!__atomic_compare_exchange_n(&made, &none, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
        {
                while (__atomic_load_n(&made, __ATOMIC_ACQUIRE) != 2)
                        ;
                return;
        }

        p32 s = 0;
        f64 next = font_srgb_to_linear(0.5 / 255) * 65535;
        for (p32 v = 0; v < 65536; v++)
        {
                while (s < 255 && next <= v)
                        next = font_srgb_to_linear((++s + 0.5) / 255) * 65535;
                font_to_srgb[v] = (p8)s;
        }
        for (s = 0; s < 256; s++)
                font_to_linear[s] = (p16)(font_srgb_to_linear(s / 255.0) * 65535 + 0.5);
        __atomic_store_n(&made, 2, __ATOMIC_RELEASE);
}

fn font_start(font_engine address_to e, p8 address_to atlas, p32 width, p32 height)
{
        memory_zero(e, sizeof(*e));
        e->hint_until = 24;
        e->darken = 12;
        e->darken_until = 32;
        e->contrast = 64;
        e->linear = true;
        e->atlas = atlas;
        e->atlas_width = width > 0xffff ? 0xffff : width;
        e->atlas_height = height > 0xffff ? 0xffff : height;
        font_tables();
}

static fn font_ramp(font_engine address_to e, p32 colour)
{
        if (e->ramp_ready && e->ramp_colour == colour && e->ramp_contrast == e->contrast)
                return;

        f32 light = (0.30f * (colour >> 16 & 255) + 0.59f * (colour >> 8 & 255) + 0.11f * (colour & 255)) / 255;
        f32 weight = 4 * (0.75f - light);
        f32 k = e->contrast / 64.0f * (weight < 0 ? 0 : weight > 1 ? 1 : weight);

        for (p32 a = 0; a < 256; a++)
        {
                f32 x = a / 255.0f;

                e->ramp[a] = (p8)(255 * x * (k + 1) / (x * k + 1) + 0.5f);
        }
        e->ramp_colour = colour;
        e->ramp_contrast = e->contrast;
        e->ramp_ready = true;
}

//      One pixel: back and fore mixed by a of 255, channel by channel.
static p32 font_mix(const font_engine address_to e, p32 back, p32 fore, p32 a)
{
        p32 mixed = 0;

        for (p32 shift = 0; shift < 24; shift += 8)
        {
                p32 b = back >> shift & 255, f = fore >> shift & 255;
                p32 value = e->linear ? font_to_srgb[(font_to_linear[b] * (255 - a) + font_to_linear[f] * a + 127) / 255]
                                      : (b * (255 - a) + f * a + 127) / 255;

                mixed |= value << shift;
        }
        return mixed;
}

/*
        Text on a ground of one colour, which is nearly all text: a terminal
        paints each cell's background, a panel its own. Every coverage level
        then has one answer, so the 256 pixels for the pair of colours are
        made once -- contrast curve, linear light and all -- and a glyph is a
        lookup a pixel: the pair's 256 tones, and after them the same tones
        as four planes of bytes, which is the table memory_translate_u32_rect
        takes. The engine keeps 64 pairs.
*/
static const p32 address_to font_tones(font_engine address_to e, p32 colour, p32 background)
{
        p64 key = (p64)1 << 63 | (p64)e->contrast << 48 | (p64)e->linear << 56 |
                  (p64)(colour & 0xffffff) << 24 | (background & 0xffffff);
        font_pairing address_to pair = &e->pairs[(key * 0x9e3779b97f4a7c15ull) >> 58];

        if (pair->key != key)
        {
                font_ramp(e, colour);
                for (p32 c = 0; c < 256; c++)
                {
                        pair->pixel[c] = font_mix(e, background, colour, e->ramp[c]);
                        for (p32 k = 0; k < 4; k++)
                                pair->planes[k][c] = (p8)(pair->pixel[c] >> (8 * k));
                }
                pair->key = key;
        }
        return pair->pixel;
}

//      A glyph's coverage onto the target at x, y: through the pair's tones
//      where the pixel under it is still the background, by the full mix
//      where another glyph has already drawn there or the ground is unknown.
static fn font_blend(font_engine address_to e, const font_target address_to target,
                     b32 x, b32 y, font_bitmap glyph, p32 colour, p32 background)
{
        b32 x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
        b32 x1 = x + glyph.width > target->width ? target->width - x : glyph.width;
        b32 y1 = y + glyph.height > target->height ? target->height - y : glyph.height;
        bool known = !(background & FONT_UNKNOWN);
        const p32 address_to pixel = known ? font_tones(e, colour, background) : null;

        font_ramp(e, colour);
        colour &= 0xffffff;
        background &= 0xffffff;
        for (b32 r = y0; r < y1; r++)
        {
                const p8 address_to cover = glyph.coverage + (positive)r * glyph.stride;
                p32 address_to row = target->pixels + (positive)(y + r) * target->stride + x;

                for (b32 c = x0; c < x1; c++)
                {
                        p32 a = cover[c];

                        if (a)
                                row[c] = known && (row[c] & 0xffffff) == background
                                             ? pixel[a]
                                             : font_mix(e, row[c], colour, e->ramp[a]);
                }
        }
}

/*
        Layout. Each code point is looked for in the chain of faces in order,
        and the first that has it draws it; one none of them has is drawn as
        a cell if it is one, and as the first face's missing glyph if not.
        Pairs are kerned only within one face. A glyph sits at the pen
        rounded to the nearest eighth of a pixel. The engine remembers the
        character map's answers and the kerning pairs it has looked up, a
        few thousand of each, which is what a page of text asks again.
*/
static positive font_decode(const p8 address_to text, positive length, positive at, p32 address_to code)
{
        memory_utf8_state state = {0};

        while (at < length)
        {
                b32 said = memory_utf8_feed(&state, text[at++]);

                if (said > 0)
                {
                        *code = state.value;
                        return at;
                }
                if (said < 0)
                        break;
        }
        *code = 0xfffd;
        return at;
}

static bool font_is_drawn(p32 code)
{
        return (code >= 0x2500 && code <= 0x259f) || (code >= 0x2800 && code <= 0x28ff);
}

static font_memo address_to font_recall(font_memo address_to memo, p64 key)
{
        return &memo[((key * 0x9e3779b97f4a7c15ull) >> 40) & (FONT_MEMO - 1)];
}

static fn font_find(font_engine address_to e, const font_face address_to const address_to faces,
                    p32 count, p32 code, p32 address_to face, p32 address_to glyph)
{
        for (p32 i = 0; i < count; i++)
        {
                p64 key = (p64)1 << 63 | (p64)faces[i]->id << 32 | code;
                font_memo address_to memo = font_recall(e->glyph_memo, key);

                if (memo->key != key)
                        *memo = (font_memo){key, (b32)font_glyph(faces[i], code)};
                if ((*glyph = (p32)memo->value))
                {
                        *face = i;
                        return;
                }
        }
        *face = font_is_drawn(code) ? FONT_DRAWN : 0;
        *glyph = *face ? code : 0;
}

static b32 font_kern(font_engine address_to e, const font_face address_to face, p32 left, p32 right, p32 size)
{
        p64 key = (p64)1 << 63 | (p64)face->id << 32 | left << 16 | right;
        font_memo address_to memo = font_recall(e->kern_memo, key);

        if (memo->key != key)
                *memo = (font_memo){key, font_kerning_units(face, left, right)};
        return memo->value ? font_fixed(face, memo->value, size) : 0;
}

//      The pen in eighths of a pixel, to the nearest, halves right.
static inline b32 font_eighths(b32 pen)
{
        return (b32)(((b64)pen * FONT_PHASES + 0x8000) >> 16);
}

positive font_layout(font_engine address_to e, const font_face address_to const address_to faces,
                     p32 count, const p8 address_to text, positive length, p32 size,
                     p32 flags, font_place address_to out, positive room)
{
        positive placed = 0, at = 0;
        b32 pen = 0;
        font_metrics cell = count ? font_metrics_at(faces[0], size) : (font_metrics){0};

        while (at < length && placed < room && count)
        {
                p32 code, face, glyph;

                at = font_decode(text, length, at, &code);
                font_find(e, faces, count, code, &face, &glyph);
                if (placed && face != FONT_DRAWN && out[placed - 1].face == face &&
                    !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[face], out[placed - 1].glyph, glyph, size);

                b32 advance = face == FONT_DRAWN ? cell.cell_width << 16
                                                 : font_advance(faces[face], glyph, size);
                out[placed++] = (font_place){glyph, face, pen, advance};
                pen += advance;
        }
        return placed;
}

b32 font_draw(font_engine address_to e, const font_face address_to const address_to faces,
              p32 count, const p8 address_to text, positive length, p32 size, p32 flags,
              const font_target address_to target, b32 x, b32 baseline, p32 colour,
              p32 background)
{
        positive at = 0;
        b32 pen = 0;
        font_place last = {0, FONT_DRAWN, 0, 0};
        font_metrics cell = count ? font_metrics_at(faces[0], size) : (font_metrics){0};
        p32 style = font_style(e, size, flags);

        while (at < length && count)
        {
                font_place place;

                at = font_decode(text, length, at, &place.glyph);
                font_find(e, faces, count, place.glyph, &place.face, &place.glyph);
                if (place.face != FONT_DRAWN && last.face == place.face && !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[place.face], last.glyph, place.glyph, size);

                b32 eighth = font_eighths(x + pen);
                if (place.face == FONT_DRAWN)
                {
                        font_bitmap box = font_cached(e, null, place.glyph,
                                                      (p32)cell.cell_width << 16 | (p32)cell.cell_height, 0, 0, 0);
                        place.advance = cell.cell_width << 16;
                        font_blend(e, target, eighth >> 3, baseline - cell.baseline, box, colour, background);
                }
                else
                {
                        font_bitmap box = font_cached(e, faces[place.face], place.glyph, size,
                                                      (p32)eighth & 7, style, 0);
                        place.advance = font_advance(faces[place.face], place.glyph, size);
                        font_blend(e, target, (eighth >> 3) + box.left, baseline - box.top, box, colour,
                                   background);
                }
                pen += place.advance;
                last = place;
        }
        return pen;
}

//      A rectangle of a cell's ground.
static fn font_ground(const font_target address_to target, b32 left, b32 top, b32 right, b32 bottom,
                      p32 background)
{
        for (b32 row = top; row < bottom && left < right; row++)
                memory_fill_u32(target->pixels + (positive)row * target->stride + left, (positive)(right - left),
                                background & 0xffffff);
}

/*
        A terminal cell, whole: its background and its glyph written in one
        pass, each pixel once, through the pair's tones. A glyph is cached as
        its whole cell -- the glyph at its place, nothing around it -- so a
        cell is one rectangle of coverage made into pixels, with no fill on
        either side. A glyph too big for its cell is drawn from its own
        bitmap instead, clipped to the cell, which is the terminal's rule
        for whose pixel it is. With an unknown background only the glyph is
        blended in.
*/
fn font_cell(font_engine address_to e, const font_face address_to const address_to faces,
             p32 count, p32 code, p32 cells, const font_metrics address_to grid, p32 size,
             p32 flags, const font_target address_to target, b32 x, b32 y, p32 colour,
             p32 background)
{
        b32 width = (b32)(cells ? cells : 1) * grid->cell_width, height = grid->cell_height;
        font_bitmap box = {0};
        b32 gx = x, gy = y;
        p32 face, glyph;

        if (font_is_drawn(code))
                box = font_cached(e, null, code, (p32)grid->cell_width << 16 | (p32)height, 0, 0, 0);
        else if (count && code != ' ')
        {
                font_find(e, faces, count, code, &face, &glyph);

                b32 eighth = font_eighths(((width << 16) - font_advance(faces[face], glyph, size)) / 2);
                p32 style = font_style(e, size, flags);

                box = font_cached(e, faces[face], glyph, size, (p32)eighth & 7, style,
                                  (p64)(p16)width << 48 | (p64)(p16)height << 32 |
                                      (p64)(p16)grid->baseline << 16 | (p16)(eighth >> 3));
                if (box.stride == 1)
                {
                        box = font_cached(e, faces[face], glyph, size, (p32)eighth & 7, style, 0);
                        gx = x + (eighth >> 3) + box.left;
                        gy = y + grid->baseline - box.top;
                }
        }
        if (background & FONT_UNKNOWN)
        {
                font_blend(e, target, gx, gy, box, colour, background);
                return;
        }

        //      The cell's rectangle on the target, and the glyph's inside it.
        b32 left = x < 0 ? 0 : x, right = x + width > target->width ? target->width : x + width;
        b32 top = y < 0 ? 0 : y, bottom = y + height > target->height ? target->height : y + height;
        b32 from = gx > left ? gx : left, to = gx + box.width < right ? gx + box.width : right;
        b32 above = gy > top ? gy : top, below = gy + box.height < bottom ? gy + box.height : bottom;
        const p32 address_to pixel = font_tones(e, colour, background);

        if (left >= right || top >= bottom)
                return;
        if (from >= to || above >= below)
                from = to = left, above = below = top;
        font_ground(target, left, top, right, above, background);
        font_ground(target, left, below, right, bottom, background);
        font_ground(target, left, above, from, below, background);
        font_ground(target, to, above, right, below, background);
        if (from < to)
                memory_translate_u32_rect(target->pixels + (positive)above * target->stride + from, target->stride,
                                          box.coverage + (positive)(above - gy) * box.stride + (from - gx),
                                          box.stride, (positive)(to - from), (positive)(below - above), pixel);
}

#endif // MOONWATER_CANVAS_FONT
