/*
        The text engine: TrueType outlines into coverage, coverage into pixels.

        One engine for every surface that draws text -- the terminal, the
        taskbar, the launcher and settings, the compositor's surface text --
        in userspace, inside the program that includes it after lib.util.c.
        The kernel's fallback console keeps its 8x16 face. No outline code
        is ever in ring 0, and this file refuses to compile there.

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
                        says so, hhea otherwise), post's underline and thickness
            coverage    exact area: the signed area and cover of every edge
                        in every cell it crosses, a running sum along each
                        row, nonzero by the magnitude clamped at full
            blending    dark text in linear light through the sRGB transfer
                        curve, with a contrast curve that thickens it the way
                        linear blending thins it; light text toward a mix of
                        the sRGB bytes, which linear blending would thicken;
                        stem darkening for small text on request
            fitting     below hint_until pixels, the baseline, x-height and
                        cap height land on whole pixels by a piecewise
                        vertical map; nothing horizontal moves
            position    eight horizontal phases a pixel, in the cache key
            kerning     GPOS pair adjustment, formats 1 and 2, through
                        extension lookups, from the kern feature of the
                        default script; the kern table when GPOS has none
            cells       box drawing (U+2500-257F), block elements
                        (U+2580-259F), braille (U+2800-28FF) and the shapes
                        of U+25A0-25FF that programs draw, with U+21B5 and
                        U+2717, drawn to the cell, so neighbours join at every
                        size
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

        A quadratic whose second difference is over FONT_STRAIGHT 1024ths is
        cut into n equal steps of its parameter, the fewest that leave each
        piece a second difference of at most FONT_FLAT 1024ths on either axis
        and a second difference times its length of at most 1/FONT_REACH of
        a pixel squared (the second difference in pixels times the chord in
        pixels over n cubed). Each piece bows from its chord by a quarter of
        its second difference, so a chord alone would leave the pixels it
        runs through that much short, all of them the same way. The points
        between pieces are therefore moved, all by one step toward the curve's
        side, a sixth of a piece's second difference (a sixth is the mean of
        the bow, t(1-t)): each piece then covers as much as the curve under
        it, the extra half step at the first and last point makes the two
        end pieces come out even, and what is left is the part of a piece's
        sliver that lies across a pixel's edge, under a tenth of the bow's
        area in a pixel. The moved points are rounded to the nearest 1024th
        from a forward difference carried to 2^-32 of a unit (2^-8 of a unit
        off after the longest walk). Integrating the quadratic analytically
        per cell would still end on the same 1/1024 grid, and would cost a
        square root at every cell crossing; this costs integer adds, and
        comes out the same bytes on all three machines.

        Coverage is exact for that polygon: each cell gets the cover (the
        height an edge spans in it, signed by direction) and the area (cover
        times the sum of the x where the edge enters and leaves), exact in
        integers at 1/1024 px, and the byte is the area of the pixel covered
        times 255 rounded to the nearest, halves up. Where an edge crosses
        from one row or column to the next, its crossing is placed on the
        nearest 1024th, so a long edge's rounding does not lean one way.
        Measured against an exact rendering of every glyph of Inter, Inter
        Bold, IBM Plex Mono and Hack at 8, 16, 48 and 200 px, no byte is
        more than 0.97 levels from 255 times the true area (rounding to the
        byte itself is half a level of that), and none is more than 1 from
        the exact area's own nearest byte.

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
                Readies an engine (766 KB; one per thread) with the
                caller's width by height bytes for its atlas, which bounds
                the largest glyph it will draw by area. The engine's memory
                must be zero (a static, or a new mapping), or an engine that
                has been started, which is cleared; a fresh one is not
                written, so only the pages a program draws with are ever
                resident: 36 KB for a terminal's first glyph, 84 KB for the
                ninety-five of ASCII, where all of it was 764 KB before the
                first. The settings at the top of font_engine may be changed
                between calls.

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

        b64 font_draw(font_engine *engine, const font_face *const *faces,
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

        fn font_cells(font_engine *engine, const font_face *const *faces,
                      p32 count, const p32 *codes, p32 n, const font_metrics *grid,
                      p32 size, p32 flags, const font_target *target, b32 x,
                      b32 y, p32 colour, p32 background)
                font_cell for n code points in a row from x, y: the same
                pixels. On a known ground, inside the target, with the glyphs
                in the atlas at once, the run goes through the strip in one
                pass; otherwise it is drawn a cell at a time.

        background is the colour the text lies on when the caller knows it,
        which is what makes a glyph one table lookup a pixel; FONT_UNKNOWN
        reads every pixel under the glyph and blends with it instead.

        flags: FONT_UNHINTED draws the outline as the font gives it,
        FONT_DARKEN asks for stem darkening, which font_draw and font_cell
        give dark text on a lighter ground unasked and FONT_UNDARKENED takes
        away, FONT_UNKERNED leaves pairs alone, FONT_OBLIQUE slants the face
        12 degrees for an italic it does not have, FONT_EMBOLDEN thickens it
        by a 24th of the size for a bold it does not have.

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
#define FONT_OBLIQUE 8
#define FONT_EMBOLDEN 16
#define FONT_UNDARKENED 32

#define FONT_DRAWN 0xffff
#define FONT_UNKNOWN 0xff000000u
#define FONT_PHASES 8
#define FONT_EXACT 14
#define FONT_ONE 1024
#define FONT_SHIFT 10
#define FONT_STRAIGHT 4
#define FONT_FLAT 16
#define FONT_REACH 6000
#define FONT_PIECES (1 << 15)

/*      The ceilings that make a hostile file cost a bounded amount. Real
        fonts sit far under each: Noto's CJK glyphs have under a thousand
        points, and no shipping font nests composites four deep. */
#define FONT_POINTS 8192
#define FONT_CONTOURS 4096
#define FONT_DEPTH 8
#define FONT_COMPONENTS 512
#define FONT_SUBTABLES 256
#define FONT_EDGE 8192
//      The largest size drawn, 16384 px in 26.6: a glyph at it is past FONT_EDGE
//      and the cache's key keeps 22 bits of size, so no two sizes share a key.
#define FONT_SIZE_LIMIT (1 << 20)
#define FONT_BAND (1 << 16)
#define FONT_ROWS 1024
#define FONT_SLOTS 4096
#define FONT_SHELVES 512
#define FONT_LOOKUPS 16
#define FONT_PAIRS 64
#define FONT_WAYS 4
#define FONT_MEMO 512
#define FONT_RECENT 256
#define FONT_STRIP (1 << 17)
#define FONT_RUN 1024
#define FONT_GROUP 6144
#define FONT_NEAR 512
#define FONT_EVENTS 3840

#define FONT_TAG(a, b, c, d) ((p32)(a) << 24 | (p32)(b) << 16 | (p32)(c) << 8 | (p32)(d))

typedef struct
{
        const p8 address_to data;
        positive size;
        positive glyf, glyf_size, loca, hmtx, cmap, lookup_list, kern;
        p32 glyphs, metrics, units, id;
        p16 lookups[FONT_LOOKUPS];
        p8 lookup_count, long_loca, cmap_format, symbol;
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
        b64 x, advance;     // 16.16: a run of a few thousand glyphs is past 32 bits
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
        p8 on;
} font_point;

//      What a row of the bitmap being drawn needs, together: the blocks an
//      edge touched; the columns the contour in hand has an edge in; and
//      for each winding the columns some contour of it reaches (from its
//      first edge's column to its last), and the columns two or more reach.
typedef struct
{
        p64 touched, crossed, there[2], twice[2];
} font_row;

typedef struct
{
        p16 last;
        p8 wind;
} font_contour;

typedef struct
{
        p64 key;
        p32 pixel[256];
        p8 planes[4][256];
        p32 ground;
} font_pairing;

_Static_assert(__builtin_offsetof(font_pairing, ground) == __builtin_offsetof(font_pairing, pixel) + 2048,
               "the ground follows the planes, where memory_blend_u32_rect reads it");

typedef struct
{
        p64 key;
        b32 value;
} font_memo;

//      What a face measures, once a face: its standard stem and its zones
//      (see font_face_measure), kept per face id in the engine's own table
//      of FONT_MEASURES, so the glyph memo's churn cannot evict them.
#define FONT_ZONES 6
//      Sixty-four, so faces whose ids differ by 16 do not evict each other's
//      measure on every glyph (ids run in the order the faces load).
#define FONT_MEASURES 64

typedef struct
{
        p32 id;
        bool stemmed, zoned;
        b32 standard, zones[FONT_ZONES];
} font_measure;

//      The coverage curve for one pair of colours and a strength, as
//      font_ramp makes it: the table of 256 coverages, and the plain share.
#define FONT_RAMPS 8

typedef struct
{
        p32 colour, ground, contrast, plain;
        bool ready;
        p8 table[256];
} font_ramp_set;

//      The identities of drawn cells that the recent cells name by index: the
//      faces a cell is found in, the grid it lies on and how many cells it
//      spans. A new identity takes the next slot in turn, and the recent
//      cells drawn against the slot it takes go stale with it.
#define FONT_IDENTS 8

typedef struct
{
        p64 chain, grid;
        p32 spans;
        bool used;
} font_ident;

//      A cell drawn lately: what it was (face, code, size, flags, width) and
//      the identity it was drawn with (an index into the engine's identities),
//      the atlas generation it was found in, and the bitmap and its place
//      from the cell's corner.
typedef struct
{
        p64 key;
        p32 generation;
        p8 ident;
        b16 dx, dy, top, bottom;
        font_bitmap box;
} font_recent;

typedef struct
{
        p32 hint_until;         // largest size in pixels that is fitted (64, a terminal's largest)
        p32 darken;             // darkening at small sizes, 1/64 pixel (12)
        p32 darken_until;       // size in pixels where it has faded out (32)
        p32 contrast;           // dark text's contrast boost, 1/64 (128)
        p32 snap_widest;        // widest standard stem snapped in cells, 1/64 pixel (1024)
        bool linear;            // blend in linear light (true)

        //      What a glyph being drawn touches first, together, then the
        //      arrays it touches the first entries of, each of one kind so
        //      that a small glyph is a few pages of them and not a page of
        //      each; the caches, which a program fills as it goes, last.
        p8 address_to atlas;
        p32 atlas_width, atlas_height, shelf_count, shelf_bottom, slot_count;
        bool started, scratched, clip, gather, overflow, track, mark, direct;
        p32 pair_turns, generation;
        p32 point_count, contour_count, components;
        b32 left_side;
        b32 band, rows, stride, width, grain, cross, wound, x, y;
        b32 cell_x, cell_y;
        p32 cover, area, gathered, used[5];
        b32 floor_y, height_y, wide_x;
        font_ramp_set ramps[FONT_RAMPS], address_to ramp_now;
        p8 address_to ramp;
        p32 ramp_turn, plain;
        p64 shared[FONT_EDGE / 64 + 1];
        font_shelf shelves[FONT_SHELVES];

        font_row row[FONT_ROWS];
        font_contour contour[FONT_CONTOURS];
        font_point points[FONT_POINTS];
        p32 cells[FONT_BAND];

        font_slot slots[FONT_SLOTS * 3 / 4];
        p16 index[FONT_SLOTS];
        font_pairing pairs[FONT_PAIRS];
        font_memo glyph_memo[FONT_MEMO], kern_memo[FONT_MEMO];
        font_measure measures[FONT_MEASURES];
        font_recent recent[FONT_RECENT];
        font_ident idents[FONT_IDENTS];
        p32 ident_turn, ident_last;

        //      A run of cells on its way to the target: each cell's bitmap,
        //      then their coverage rows side by side.
        font_bitmap run[FONT_RUN];
        b16 run_dx[FONT_RUN], run_dy[FONT_RUN], run_top[FONT_RUN], run_bottom[FONT_RUN];
        p8 strip[FONT_STRIP];
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
        //      faces at once: it is taken atomically, and never zero. It is
        //      16 bits, so the ids are 1 to 65535 and a process loads at most
        //      that many faces: a 65536th would be issued the first face's id,
        //      and both are live in one engine's cache.
        p32 serial = __atomic_load_n(&faces_loaded, __ATOMIC_RELAXED);
        while (serial < 0xffff && !__atomic_compare_exchange_n(&faces_loaded, &serial, serial + 1, false,
                                                               __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                ;
        if (serial >= 0xffff)
                return FONT_BAD;
        face->id = (p32)(serial + 1);

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

static inline positive font_coordinates(font_engine address_to e, const font_face address_to face,
                                        p32 first, p32 count, positive p, bool checked)
{
        const p8 address_to data = face->data;

        for (p32 axis = 0; axis < 2; axis++)
        {
                p32 brief = axis ? 4 : 2, same = axis ? 32 : 16;
                b64 value = 0;

                for (p32 i = first; i < first + count; i++)
                {
                        p32 flag = e->points[i].on;

                        if (flag & brief)
                        {
                                b64 step = checked ? font_u8(face, p) : data[p];

                                p++;
                                value += flag & same ? step : -step;
                        }
                        else if (!(flag & same))
                        {
                                value += checked ? font_i16(face, p) : (b16)((p32)data[p] << 8 | data[p + 1]);
                                p += 2;
                        }
                        if (axis)
                                e->points[i].y = value * (1 << FONT_EXACT);
                        else
                                e->points[i].x = value * (1 << FONT_EXACT);
                }
        }
        return p;
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
                        e->contour[e->contour_count++].last = (p16)(first + last);
                }

                positive p = at + 10 + 2 * (positive)contours;
                p = p + 2 + font_u16(face, p);

                p32 i = 0;
                while (i < count && p < limit)
                {
                        p32 flag = font_u8(face, p++);
                        p32 repeat = flag & 8 ? font_u8(face, p++) : 0;

                        for (p32 r = 0; r <= repeat && i < count; r++)
                                e->points[first + i++].on = (p8)flag;
                }
                if (i < count)
                        return FONT_BAD;

                //      Four bytes a point is the most the coordinates can
                //      take: when that much lies inside the file nothing
                //      below needs a bounds test, and the same bytes are read
                //      that the tested reads would have read.
                if (p + 4 * (positive)count + 2 <= face->size)
                        p = font_coordinates(e, face, first, count, p, false);
                else
                        p = font_coordinates(e, face, first, count, p, true);
                if (p > limit)
                        return FONT_BAD;

                for (i = first; i < first + count; i++)
                        e->points[i].on &= 1;
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

//      Whole pixels, and whether a place is on one (within 1/64).
static inline bool font_on_grid(b64 v)
{
        b64 r = v & (FONT_ONE - 1);
        return r <= FONT_ONE / 64 || r >= FONT_ONE - FONT_ONE / 64;
}

static inline b64 font_round(b64 v)
{
        return (v + FONT_ONE / 2) & ~(b64)(FONT_ONE - 1);
}

/*
        Grid fitting, in 1024ths of a pixel. Below hint_until pixels the
        outline's heights move so that the baseline, the x-height and the cap
        height each fall on a whole pixel: the x-height zone is scaled about
        the baseline (and the descenders with it), the zone up to the cap
        height is stretched to meet both, and everything above moves with
        the cap height. An x-height rounds up from four tenths of a pixel,
        because at these sizes the larger x-height is the more legible one.

        Round letters reach past those lines a little (the top of o over the
        x-height, its bottom under the baseline, the top of O over the cap
        height), so that they look as tall as the flat ones. Under half a
        pixel that overshoot is one grey row on top of every round letter
        and nothing else, so each overshoot is a zone of its own: flattened
        onto its line while it is under half a pixel, a whole pixel or more
        when it is not. The overshoots are measured once a face, from o and
        O, in font units.

        Horizontal positions are not touched here: phases and advances stay
        exact. lines answers where the fitted baseline, x-height and cap
        height are, for the bars to keep.
*/
static inline b64 font_overshoot(b64 pixels)
{
        return pixels < FONT_ONE / 2 ? 0 : font_round(pixels);
}

static p32 font_fit(font_engine address_to e, const font_face address_to face, p32 size, const b32 address_to zones,
                    b64 grown, b64 address_to lines)
{
        b64 scale = (b64)size * (FONT_ONE / 64);
        b64 low = font_divide((b64)face->x_height * scale, face->units);
        b64 high = font_divide((b64)face->cap_height * scale, face->units);
        b64 snapped = (low + FONT_ONE * 6 / 10) & ~(b64)(FONT_ONE - 1);
        b64 capped = (high + FONT_ONE / 2) & ~(b64)(FONT_ONE - 1);

        if (low < FONT_ONE / 2)
                return 0;
        snapped = snapped < FONT_ONE ? FONT_ONE : snapped;
        if (high <= low + FONT_ONE / 2 || capped <= snapped)
                high = 0;

        b64 over = font_divide((b64)zones[0] * scale, face->units);
        b64 under = font_divide((b64)zones[1] * scale, face->units);
        b64 cover = font_divide((b64)zones[2] * scale, face->units);
        b64 ascender = font_divide((b64)zones[4] * scale, face->units);
        b64 descender = font_divide((b64)zones[5] * scale, face->units);

        //      The knots of the map, from the outline as it was to as fitted:
        //      the descender's bottom and the ascender's top are whole pixels
        //      too, where they took the fractions of the lines next to them,
        //      grey rows under p and q and over b, d, h, k and l.
        //      An outline already grown (darkened, or made bold) has its flat
        //      bottoms grown down past their lines and its tops up past
        //      theirs: the knots are where the grown edges are, so those are
        //      what land on the lines.
        b64 was[8], now[8];
        p32 knots = 0;
        //      A knot is kept wherever the map stays in order: its place past
        //      the one before, its fitted place no nearer. A margin of half a
        //      pixel more, as there was, left b, d, h and k of Plex Mono at 12
        //      px a third of a pixel off their row, their ascender 0.37 px over
        //      the cap height's knot and riding its shift.
        if (descender > under && font_round(descender) >= font_overshoot(under) && font_round(descender) > 0)
                was[knots] = -descender - grown, now[knots++] = -font_round(descender);
        if (under > 0)
                was[knots] = -under - grown, now[knots++] = -font_overshoot(under);
        was[knots] = -grown, now[knots++] = 0;
        was[knots] = low + grown, now[knots++] = snapped;
        if (over > 0 && (!high || low + over < high))
                was[knots] = low + over + grown, now[knots++] = snapped + font_overshoot(over);
        if (high)
        {
                was[knots] = high + grown, now[knots++] = capped;
                if (cover > 0)
                        was[knots] = high + cover + grown, now[knots++] = capped + font_overshoot(cover);
        }
        if (ascender > was[knots - 1] - grown && font_round(ascender) >= now[knots - 1])
                was[knots] = ascender + grown, now[knots++] = font_round(ascender);

        for (p32 i = 0; i < e->point_count; i++)
        {
                b64 y = e->points[i].y;

                if (y < was[0])
                        //      Below the lowest knot the descenders keep the
                        //      x-height zone's scale.
                        y = now[0] + font_divide((y - was[0]) * snapped, low + 2 * grown);
                else if (y >= was[knots - 1])
                        y += now[knots - 1] - was[knots - 1];
                else
                {
                        p32 k = 1;
                        while (was[k] <= y)
                                k++;
                        y = now[k - 1] + font_divide((y - was[k - 1]) * (now[k] - now[k - 1]), was[k] - was[k - 1]);
                }
                e->points[i].y = y;
        }

        lines[0] = 0;
        lines[1] = snapped;
        if (high)
                lines[2] = capped;
        return high ? 3 : 2;
}

/*
        Stem darkening: every point moves outwards along the bisector of its
        two edges, so each stem gains strength across its width. Which side
        is out comes from the outline's own winding, and a point between
        short edges moves no further than they are long, so a thin detail
        thickens without turning inside out.
*/
static fn font_embolden(font_engine address_to e, f32 strength, bool across)
{
        f64 area = 0;
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
                for (p32 i = first; i <= e->contour[c].last; i++)
                {
                        font_point a = e->points[i], b = e->points[i == e->contour[c].last ? first : i + 1];

                        area += (f64)a.x * b.y - (f64)b.x * a.y;
                }

        f32 half = strength * (area < 0 ? 0.5f : -0.5f);
        first = 0;
        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
        {
                p32 last = e->contour[c].last;
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
                        e->points[i].y = here.y + (across ? 0 : (b64)(sy < 0 ? sy - 0.5f : sy + 0.5f));
                        before = here;
                        here = after;
                }
        }
}

/*
        Stem snapping, for the terminal's cells, where every glyph's pen is a
        whole pixel and one shared eighth: the vertical stems move so that
        each edge lands on a pixel boundary, and the outline between them is
        stretched to follow. Unsnapped, a stem 1.4 px wide at a fractional x
        is three grey columns; snapped it is one or two black ones.

        An edge is a run of the outline's points at one x, at least a sixth
        of the x-height tall (FONT_ROUND_RUN), so serifs and the vertical
        tangents of curves are left to the stretch. The standard is measured
        from stems of a third of the x-height, which are straight ones.
        Which side of it the ink is on comes from the glyph's own winding
        (TrueType's outer contours run clockwise and put the ink on the right
        of travel, but a converted font may not). A
        stem is a left edge and the nearest right edge across from it that
        overlaps it by half, no wider than a quarter of the em.

        One standard width a face, the median stem of l n m h i u I H in font
        units, measured once and kept per face, so every stem near
        it rounds to the same whole pixels and a line of text has one weight:
        rounding each stem alone gives n one pixel and l two. A stem within a
        quarter of the standard takes the standard's rounding, others round on
        their own, none to less than a pixel. Each stem then sits on the whole
        pixels nearest its centre. A snap that would reorder two stems, or
        close the counter between them to less than a pixel where it was half
        a pixel or more, is dropped, the stem that moved most first.

        Whole widths from a standard stem of three quarters of a pixel;
        under that each stem keeps its width and only its nearer edge moves
        onto the grid. Points between two edges move by the straight
        line between the two moves, and the glyph's own left and right ends
        stay put, so an arm stretches instead of riding into the next cell.
        Snapping stops where the standard stem is snap_widest, sixteen pixels,
        and at the fitting's own ceiling, hint_until: both past the largest
        size a terminal zooms to, which unfitted (28 px up, when the ceiling
        was 24) was greyer than the size below it.
*/
#define FONT_STEMS 32

typedef struct
{
        b64 x, low, high;
        b32 side;
        bool used;
} font_run;

static p32 font_runs(const font_engine address_to e, b64 slack, b64 least, font_run address_to run)
{
        f64 area = 0;
        p32 first = 0, count = 0;

        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
                for (p32 i = first; i <= e->contour[c].last; i++)
                {
                        font_point a = e->points[i], b = e->points[i == e->contour[c].last ? first : i + 1];

                        area += (f64)a.x * b.y - (f64)b.x * a.y;
                }

        //      Up a left edge or down a right one when the ink is on the right.
        b32 right = area < 0 ? 1 : -1;
        first = 0;
        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
        {
                p32 last = e->contour[c].last, n = last - first + 1;
                if (last <= first)
                        continue;

                //      Start where an edge is not vertical, so that no run is
                //      cut in two by the contour's first point.
                p32 start = n;
                for (p32 k = 0; k < n && start == n; k++)
                {
                        font_point a = e->points[first + (k + n - 1) % n], b = e->points[first + k];
                        b64 dx = b.x - a.x;

                        if (dx > slack || dx < -slack || b.y == a.y)
                                start = k;
                }
                if (start == n)
                        continue;

                font_run here = {0};
                b32 going = 0;
                for (p32 k = 0; k <= n; k++)
                {
                        font_point a = e->points[first + (start + k) % n], b = e->points[first + (start + k + 1) % n];
                        b64 dx = b.x - here.x;
                        b32 way = b.y > a.y ? 1 : b.y < a.y ? -1 : 0;
                        bool vertical = k < n && way && (going == 0 ? b.x - a.x <= slack && a.x - b.x <= slack
                                                                   : way == going && dx <= slack && -dx <= slack);

                        if (vertical && going == 0)
                        {
                                here = (font_run){a.x, a.y < b.y ? a.y : b.y, a.y < b.y ? b.y : a.y, 0, false};
                                going = way;
                        }
                        else if (vertical)
                        {
                                here.low = b.y < here.low ? b.y : here.low;
                                here.high = b.y > here.high ? b.y : here.high;
                        }
                        if (!vertical || k == n)
                        {
                                if (going && here.high - here.low >= least && count < FONT_STEMS * 2)
                                {
                                        here.side = going * right;
                                        run[count++] = here;
                                }
                                going = 0;
                                if (k < n && way && b.x - a.x <= slack && a.x - b.x <= slack)
                                {
                                        here = (font_run){a.x, a.y < b.y ? a.y : b.y, a.y < b.y ? b.y : a.y, 0, false};
                                        going = way;
                                }
                        }
                }
        }
        return count;
}

//      Left edges paired with right ones, the narrowest pairs first: stems as
//      their two x, sorted left to right. With least, a pair needs one run of
//      least or more, and the other may be a curve's short extreme: the two
//      are taken to overlap where they do with each widened by half the
//      stroke, since a curve turns within about one stroke. The inner side of
//      a round stroke -- the top of n's counter, the bottom of a's bowl -- is
//      a level run of a fraction of a pixel (n's 0.23 px at 10 px), so a round
//      stroke never paired and was never snapped whole; flattened onto its
//      line by the fit it lost its overshoot from its width, fell under half
//      ink, and opened the counter it closed (b, p, a, g, @, & at 8 to 31 px).
static p32 font_stems(font_run address_to run, p32 runs, b64 widest, b64 address_to stem, b64 least)
{
        p32 count = 0;

        for (;;)
        {
                b64 best = widest + 1;
                p32 left = 0, right = 0;

                for (p32 a = 0; a < runs; a++)
                        if (!run[a].used && run[a].side > 0)
                                for (p32 b = 0; b < runs; b++)
                                {
                                        if (run[b].used || run[b].side > 0 || run[b].x <= run[a].x)
                                                continue;

                                        b64 width = run[b].x - run[a].x, reach = least ? width / 2 : 0;
                                        b64 low = run[a].low > run[b].low ? run[a].low : run[b].low;
                                        b64 high = run[a].high < run[b].high ? run[a].high : run[b].high;
                                        b64 shorter = run[a].high - run[a].low < run[b].high - run[b].low
                                                          ? run[a].high - run[a].low
                                                          : run[b].high - run[b].low;
                                        b64 longer = run[a].high - run[a].low + run[b].high - run[b].low - shorter;

                                        if (width < best && 2 * (high - low + 2 * reach) >= shorter && longer >= least)
                                                best = width, left = a, right = b;
                                }
                if (best > widest || count == FONT_STEMS)
                        break;
                run[left].used = run[right].used = true;
                stem[2 * count] = run[left].x;
                stem[2 * count + 1] = run[right].x;
                count++;
        }
        for (p32 i = 1; i < count; i++)
                for (p32 j = i; j > 0 && stem[2 * j] < stem[2 * j - 2]; j--)
                {
                        b64 x0 = stem[2 * j], x1 = stem[2 * j + 1];

                        stem[2 * j] = stem[2 * j - 2], stem[2 * j + 1] = stem[2 * j - 1];
                        stem[2 * j - 2] = x0, stem[2 * j - 1] = x1;
                }
        return count;
}

//      The face's measure slot, cleared when another face held it.
static font_measure address_to font_measured(font_engine address_to e, const font_face address_to face)
{
        font_measure address_to m = &e->measures[face->id & (FONT_MEASURES - 1)];

        if (m->id != face->id)
                *m = (font_measure){.id = face->id};
        return m;
}

//      The face's standard stem in font units times 2^14, or 0 for none.
static b32 font_stem_standard(font_engine address_to e, const font_face address_to face)
{
        font_measure address_to m = font_measured(e, face);

        if (m->stemmed)
                return m->standard;

        static const char references[] = "lnmhiuIH";
        b64 width[FONT_STEMS], stem[2 * FONT_STEMS];
        font_run run[2 * FONT_STEMS];
        p32 found = 0;
        b64 one = (b64)face->units << FONT_EXACT;
        b64 least = ((b64)(face->x_height > 0 ? face->x_height : face->units / 2) << FONT_EXACT) / 3;

        for (p32 r = 0; references[r] && found < FONT_STEMS; r++)
        {
                p32 glyph = font_glyph(face, (p32)references[r]);

                e->point_count = e->contour_count = e->components = 0;
                if (!glyph || font_outline(e, face, glyph, 0) < 0)
                        continue;

                p32 runs = font_runs(e, one / 512, least, run);
                p32 stems = font_stems(run, runs, one / 4, stem, 0);
                for (p32 s = 0; s < stems && found < FONT_STEMS; s++)
                        width[found++] = stem[2 * s + 1] - stem[2 * s];
        }
        e->point_count = e->contour_count = e->components = 0;

        for (p32 i = 1; i < found; i++)
                for (p32 j = i; j > 0 && width[j] < width[j - 1]; j--)
                {
                        b64 t = width[j];
                        width[j] = width[j - 1], width[j - 1] = t;
                }
        b32 standard = found ? (b32)width[found / 2] : 0;
        m->standard = standard, m->stemmed = true;
        return standard;
}

static fn font_swap_axes(font_engine address_to e)
{
        for (p32 i = 0; i < e->point_count; i++)
        {
                b64 t = e->points[i].x;
                e->points[i].x = e->points[i].y;
                e->points[i].y = t;
        }
}

/*
        What a face's round letters and bars measure, once a face, kept in
        the engine's measures: how far o reaches over the x-height and under the
        baseline and O over the cap height, in font units, the standard
        bar, the median horizontal stem of H E F T L e in font units times
        2^14, found as the vertical stems are with the axes swapped, and the
        top of d's ascender and the bottom of p's descender, in font units
        (the descender as a depth).
*/
static fn font_face_measure(font_engine address_to e, const font_face address_to face, b32 address_to out)
{
        font_measure address_to m = font_measured(e, face);

        if (m->zoned)
        {
                memory_copy(out, m->zones, sizeof(m->zones));
                return;
        }

        memory_zero(out, sizeof(m->zones));
        for (p32 tall = 0; tall < 2; tall++)
        {
                p32 glyph = font_glyph(face, tall ? 'p' : 'd');

                e->point_count = e->contour_count = e->components = 0;
                if (!glyph || font_outline(e, face, glyph, 0) < 0 || !e->point_count)
                        continue;

                b64 reach = 0;
                for (p32 i = 0; i < e->point_count; i++)
                        if (e->points[i].on)
                                reach = tall ? (-e->points[i].y > reach ? -e->points[i].y : reach)
                                             : (e->points[i].y > reach ? e->points[i].y : reach);
                out[4 + tall] = (b32)(reach >> FONT_EXACT);
        }
        for (p32 round = 0; round < 2; round++)
        {
                p32 glyph = font_glyph(face, round ? 'O' : 'o');

                e->point_count = e->contour_count = e->components = 0;
                if (!glyph || font_outline(e, face, glyph, 0) < 0 || !e->point_count)
                        continue;

                b64 top = -((b64)1 << 62), bottom = (b64)1 << 62;
                for (p32 i = 0; i < e->point_count; i++)
                        if (e->points[i].on)
                        {
                                top = e->points[i].y > top ? e->points[i].y : top;
                                bottom = e->points[i].y < bottom ? e->points[i].y : bottom;
                        }
                top >>= FONT_EXACT;
                bottom >>= FONT_EXACT;
                if (!round)
                {
                        out[0] = face->x_height > 0 && top > face->x_height ? (b32)(top - face->x_height) : 0;
                        out[1] = bottom < 0 ? (b32)-bottom : 0;
                }
                else
                        out[2] = face->cap_height > 0 && top > face->cap_height ? (b32)(top - face->cap_height) : 0;
        }

        //      Overshoots are small: one past an eighth of the em is not an
        //      overshoot, and is taken as none.
        for (p32 k = 0; k < 3; k++)
                out[k] = out[k] > (b32)face->units / 8 ? 0 : out[k];

        static const char bars[] = "HEFTLe";
        b64 width[FONT_STEMS], stem[2 * FONT_STEMS];
        font_run run[2 * FONT_STEMS];
        p32 found = 0;
        b64 one = (b64)face->units << FONT_EXACT;
        b64 least = ((b64)(face->x_height > 0 ? face->x_height : face->units / 2) << FONT_EXACT) / 3;
        for (p32 r = 0; bars[r] && found < FONT_STEMS; r++)
        {
                p32 glyph = font_glyph(face, (p32)bars[r]);

                e->point_count = e->contour_count = e->components = 0;
                if (!glyph || font_outline(e, face, glyph, 0) < 0)
                        continue;
                font_swap_axes(e);

                p32 runs = font_runs(e, one / 512, least, run);
                p32 stems = font_stems(run, runs, one / 4, stem, 0);
                for (p32 i = 0; i < stems && found < FONT_STEMS; i++)
                        width[found++] = stem[2 * i + 1] - stem[2 * i];
        }
        e->point_count = e->contour_count = e->components = 0;
        for (p32 i = 1; i < found; i++)
                for (p32 j = i; j > 0 && width[j] < width[j - 1]; j--)
                {
                        b64 t = width[j];
                        width[j] = width[j - 1], width[j - 1] = t;
                }
        out[3] = found ? (b32)width[found / 2] : 0;
        memory_copy(m->zones, out, sizeof(m->zones));
        m->zoned = true;
}

/*
        One axis's stems snapped, on x as the points stand. normal is the
        standard stem in 1024ths of a pixel and least the shortest run that is
        an edge; fixed are lines that must not move (the fitted baseline,
        x-height and cap height when the axis is y), and a stem edge already on
        one of them, or on any whole pixel, keeps its place while the other
        edge takes the width.
*/
//      Bars are whole pixels from a standard of half a pixel: a bar between
//      two black ones does not fade the way a diagonal between two black
//      stems does, and at 8 to 10 px Plex Mono's 0.6 to 0.73 px bars were the
//      grey rows of every E, F, H, T, e, t, f and z.
#define FONT_BAR_WHOLE (FONT_ONE / 2)

//      Whether a gap between two edges of the map is kept by their moves:
//      in order, and one of half a pixel or more still a pixel, so no counter
//      is shut (a gap under half a pixel may close).
static inline bool font_gap_kept(b64 was, b64 now)
{
        return now >= 0 && (was < FONT_ONE / 2 || now >= FONT_ONE);
}

static fn font_snap_axis(font_engine address_to e, b64 normal, b64 least, b64 widest,
                         const b64 address_to fixed, p32 fixes, b64 whole_from, bool loose)
{
        if (normal <= 0 || normal >= (b64)e->snap_widest * (FONT_ONE / 64))
                return;

        font_run run[2 * FONT_STEMS];
        b64 stem[2 * FONT_STEMS], to[2 * FONT_STEMS];
        p32 runs = font_runs(e, FONT_ONE / 32, least / 4, run);
        p32 stems = font_stems(run, runs, widest, stem, least);
        bool dropped[FONT_STEMS] = {0};
        b64 rounded = font_round(normal);

        rounded = rounded < FONT_ONE ? FONT_ONE : rounded;

        //      Whole widths from a standard of three quarters of a pixel up:
        //      a stem a fraction of a pixel off the grid is a grey column on
        //      every one of them, and that grey was most of the blur of 16 to
        //      20 px text, where Plex Mono's 1.28, 1.44 and 1.6 px stems kept
        //      their widths (grey ink share 665, 673 and 635 permille, now
        //      570, 538 and 429, for ink 7 and 10 percent lighter and 6
        //      heavier). Under three quarters (Plex Mono at 8 and 9 px) the
        //      stems would grow by half and more, and a diagonal between two
        //      black stems, N's, fades to nothing: those keep their width and
        //      put the nearer edge on the grid.
        bool whole = normal >= whole_from;
        for (p32 s = 0; s < stems; s++)
        {
                b64 left = stem[2 * s], right = stem[2 * s + 1], width = right - left;
                bool low = font_on_grid(left), high = font_on_grid(right);

                if (!whole)
                {
                        b64 l = font_round(left) - left, r = font_round(right) - right;
                        b64 move = low || high ? 0 : (l < 0 ? -l : l) <= (r < 0 ? -r : r) ? l : r;

                        to[2 * s] = left + move;
                        to[2 * s + 1] = right + move;
                        continue;
                }

                b64 snapped = 4 * (width - normal) <= normal && 4 * (normal - width) <= normal ? rounded
                                                                                                : font_round(width);
                snapped = snapped < FONT_ONE ? FONT_ONE : snapped;

                //      An edge already on the grid stays and the other takes
                //      the width; otherwise the whole pixels nearest the
                //      stem's centre: the left edge rounds where the centre
                //      less half the new width is.
                if (low)
                        to[2 * s] = font_round(left), to[2 * s + 1] = font_round(left) + snapped;
                else if (high)
                        to[2 * s + 1] = font_round(right), to[2 * s] = font_round(right) - snapped;
                else
                {
                        b64 from = 2 * left + width - snapped;
                        from = ((from + FONT_ONE) >> (FONT_SHIFT + 1)) << FONT_SHIFT;
                        to[2 * s] = from;
                        to[2 * s + 1] = from + snapped;
                }
        }

        //      No stem leaves the pixels the glyph's own ends are in: one the
        //      snap would set past them moves in by whole pixels, and one too
        //      wide for them is let go. Pushed out, the round sides of % went a
        //      pixel past both ends at every size, out of its cell.
        {
                b64 lowest = (b64)1 << 62, highest = -lowest;

                for (p32 i = 0; i < e->point_count; i++)
                {
                        lowest = e->points[i].x < lowest ? e->points[i].x : lowest;
                        highest = e->points[i].x > highest ? e->points[i].x : highest;
                }
                lowest &= ~(b64)(FONT_ONE - 1);
                highest = (highest + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1);
                for (p32 s = 0; s < stems && !loose; s++)
                {
                        b64 shift = to[2 * s] < lowest ? lowest - to[2 * s] : to[2 * s + 1] > highest ? highest - to[2 * s + 1] : 0;

                        to[2 * s] += shift, to[2 * s + 1] += shift;
                        dropped[s] = dropped[s] || to[2 * s] < lowest || to[2 * s + 1] > highest;
                }
        }

        //      A whole stem that straddles a fixed line is laid against it, on
        //      the side its centre is: f's arm, 0.69 px across the cap height
        //      at 11 px, was let go and drawn as two rows a third grey, the
        //      smudge at the top of every f. Then a stem that would cross a
        //      fixed line, or still straddles one, is let go: the line wins.
        for (p32 s = 0; s < stems && whole; s++)
                for (p32 f = 0; f < fixes; f++)
                {
                        b64 k = fixed[f], width = to[2 * s + 1] - to[2 * s];

                        if (!(stem[2 * s] < k && k < stem[2 * s + 1]))
                                continue;
                        if (stem[2 * s] + stem[2 * s + 1] < 2 * k)
                                to[2 * s] = k - width, to[2 * s + 1] = k;
                        else
                                to[2 * s] = k, to[2 * s + 1] = k + width;
                        break;
                }
        for (p32 s = 0; s < stems; s++)
                for (p32 f = 0; f < fixes && !dropped[s]; f++)
                {
                        b64 k = fixed[f];
                        bool straddled = stem[2 * s] < k && k < stem[2 * s + 1];

                        if ((straddled && !whole) || (to[2 * s] < k && k < to[2 * s + 1]) ||
                            (!straddled && stem[2 * s + 1] <= k && to[2 * s + 1] > k) ||
                            (!straddled && stem[2 * s] >= k && to[2 * s] < k))
                                dropped[s] = true;
                }

        //      Drop snaps that reorder stems or shut a counter, the stem that
        //      moved most first, until none does.
        for (bool again = true; again;)
        {
                again = false;
                p32 previous = FONT_STEMS;
                for (p32 s = 0; s < stems; s++)
                {
                        if (dropped[s])
                                continue;
                        if (previous < FONT_STEMS)
                        {
                                b64 gap = stem[2 * s] - stem[2 * previous + 1];
                                b64 kept = to[2 * s] - to[2 * previous + 1];

                                if (gap < 0 || kept < 0 || (gap >= FONT_ONE / 2 && kept < FONT_ONE))
                                {
                                        b64 here = to[2 * s] - stem[2 * s], there = to[2 * previous] - stem[2 * previous];

                                        here = here < 0 ? -here : here;
                                        there = there < 0 ? -there : there;
                                        dropped[here >= there ? s : previous] = true;
                                        again = true;
                                        break;
                                }
                        }
                        previous = s;
                }
        }

        //      The map: the edges that stay, the fixed lines, and the glyph's
        //      own two ends, which stay where they are, so an arm reaching out
        //      from a stem stretches or shrinks rather than riding the stem's
        //      move into the next cell (the arms of [ and ] would otherwise
        //      meet across the cell boundary).
        //      Two edges to a stem, room for the sixteen fixed lines and the
        //      glyph's two ends, and the edges no stem took.
        b64 from[4 * FONT_STEMS + 18], onto[4 * FONT_STEMS + 18];
        b64 lowest = (b64)1 << 62, highest = -lowest;
        p32 edges = 0;
        for (p32 i = 0; i < e->point_count; i++)
        {
                lowest = e->points[i].x < lowest ? e->points[i].x : lowest;
                highest = e->points[i].x > highest ? e->points[i].x : highest;
        }
        for (p32 s = 0; s < stems; s++)
                if (!dropped[s])
                {
                        from[edges] = stem[2 * s], onto[edges++] = to[2 * s];
                        from[edges] = stem[2 * s + 1], onto[edges++] = to[2 * s + 1];
                }
        //      The stems' edges are in order already; the lines and the ends
        //      go in between them where they keep the map in order, and are
        //      left out where a stem's move would fold it. Loose, the glyph's
        //      ends are not kept: they are edges like any other below.
        b64 extra[18];
        p32 more = 0;
        for (p32 f = 0; f < fixes && more < 16; f++)
                extra[more++] = fixed[f];
        if (!loose)
        {
                extra[more++] = lowest;
                extra[more++] = highest;
        }
        for (p32 x = 0; x < more; x++)
        {
                b64 k = extra[x], target = k;
                p32 at = 0;

                while (at < edges && from[at] < k)
                        at++;
                //      An end past every edge that a stem's move would fold
                //      moves with that stem, but no further than the pixel it
                //      was in: translated, the bulge of a round side 0.02 px
                //      past its stem took % a whole column wider each side.
                if (!loose && x >= more - 2 && at == 0 && edges && onto[0] < k)
                        target = onto[0] - (from[0] - k) > (k & ~(b64)(FONT_ONE - 1)) ? onto[0] - (from[0] - k)
                                                                                     : k & ~(b64)(FONT_ONE - 1);
                if (!loose && x >= more - 2 && at == edges && edges && onto[edges - 1] > k)
                {
                        b64 ceiling = (k + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1);

                        target = onto[edges - 1] + (k - from[edges - 1]) < ceiling ? onto[edges - 1] + (k - from[edges - 1]) : ceiling;
                }
                if ((at < edges && from[at] == k) || (at > 0 && onto[at - 1] > target) || (at < edges && onto[at] < target))
                        continue;
                for (p32 j = edges; j > at; j--)
                        from[j] = from[j - 1], onto[j] = onto[j - 1];
                from[at] = k, onto[at] = target;
                edges++;
        }

        //      Loose: an edge no stem took -- the cut end of a stroke, the
        //      bottom of a quote or a comma, the top of | or ( -- goes to its
        //      nearest whole pixel too, where that keeps the map in order and
        //      shuts no gap (font_gap_kept). Left where they were, these were
        //      most of the flat edges still off the grid after the bars.
        for (p32 r = 0; loose && r < runs && edges < 4 * FONT_STEMS + 18; r++)
        {
                b64 k = run[r].x, target = font_round(k);
                p32 at = 0;

                if (run[r].used)
                        continue;
                while (at < edges && from[at] < k)
                        at++;
                if ((at < edges && from[at] == k) || (at > 0 && !font_gap_kept(k - from[at - 1], target - onto[at - 1])) ||
                    (at < edges && !font_gap_kept(from[at] - k, onto[at] - target)))
                        continue;
                for (p32 j = edges; j > at; j--)
                        from[j] = from[j - 1], onto[j] = onto[j - 1];
                from[at] = k, onto[at] = target;
                edges++;
        }
        if (!edges)
                return;

        for (p32 i = 0; i < e->point_count; i++)
        {
                b64 x = e->points[i].x;
                p32 k = 0;

                while (k < edges && from[k] < x)
                        k++;
                if (k == 0)
                        x += onto[0] - from[0];
                else if (k == edges)
                        x += onto[edges - 1] - from[edges - 1];
                else if (from[k] == x)
                        x = onto[k];
                else
                {
                        //      In doubles, which a hostile font's coordinates cannot
                        //      overflow and which every machine rounds alike.
                        f64 t = (f64)(x - from[k - 1]) * (f64)(onto[k] - onto[k - 1]) / (f64)(from[k] - from[k - 1]);
                        x = onto[k - 1] + (b64)(t < 0 ? t - 0.5 : t + 0.5);
                }
                e->points[i].x = x;
        }
}

//      An edge of a vertical stem is a run of the outline at one x a sixth
//      of the x-height tall: the sides of o, c, e, O and 0, where a curve is
//      vertical for a short way, are edges too (Plex Mono's o is 123 units
//      against the third, 172, that the bars and the standard still take),
//      so a round letter's sides are black columns as a straight one's are.
#define FONT_ROUND_RUN 6

//      What a stem under three quarters of a pixel is thickened across to:
//      just past the three quarters that snap whole, so the stems are a pixel
//      and the diagonals and bowls gain as little as that takes.
#define FONT_THIN_TO (FONT_ONE * 13 / 16)

//      Vertical stems, for a cell's whole-pixel pen. heavier is what a
//      synthetic bold added to every stem, so the standard grows with it.
static fn font_snap(font_engine address_to e, const font_face address_to face, p32 size, b32 standard, b64 heavier)
{
        b64 scale = (b64)size * (FONT_ONE / 64);
        b64 normal = font_divide((b64)standard * scale, (b64)face->units << FONT_EXACT) + heavier;
        b64 x_height = font_divide((b64)(face->x_height > 0 ? face->x_height : face->units / 2) * scale, face->units);

        font_snap_axis(e, normal, x_height / FONT_ROUND_RUN, scale / 4, null, 0, FONT_ONE * 3 / 4, false);
}

//      And a bar's edge the same: the tops and bottoms of o, e, a, s, g, 3
//      and 8, where a curve is level for a short way, are rows of ink as a
//      straight bar's are.
#define FONT_ROUND_BAR 6

/*
        Dots: a contour of ink (not a counter, and inside no other contour's
        box) half a pixel to two and a half across either way and no more
        than twice as long one way as the other -- the dots of . : ; ! ? i
        and j -- made whole pixels each way and set on the grid about its
        centre. A dot of 1.2 px fell on two rows and two columns at most
        places and was a grey blot; code is full of them. Which contours are
        dots, and their sizes, are taken before the stems are snapped
        (font_dots_measure), whose map stretches a dot beside a stem (the dot
        of ! at 10 px went from 1.0 to 2.04 px wide); where each goes is its
        centre after them, so the dot of i stays over its stem.
*/
#define FONT_DOTS 16

//      Whether another contour's box holds this one's: the dot of a dotted
//      zero, which whole pixels would set off its centre (at 10 px it lies
//      between two columns) and the zero would read as a ring.
static bool font_dot_inside(const font_engine address_to e, p32 contour, b64 left, b64 bottom, b64 right, b64 top)
{
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
        {
                b64 l = (b64)1 << 62, b = l, r = -l, t = -l;

                if (c == contour)
                        continue;
                for (p32 i = first; i <= e->contour[c].last; i++)
                {
                        l = e->points[i].x < l ? e->points[i].x : l;
                        r = e->points[i].x > r ? e->points[i].x : r;
                        b = e->points[i].y < b ? e->points[i].y : b;
                        t = e->points[i].y > t ? e->points[i].y : t;
                }
                if (l <= left && r >= right && b <= bottom && t >= top)
                        return true;
        }
        return false;
}

static fn font_dots_measure(const font_engine address_to e, b64 address_to sizes)
{
        f64 whole = 0;
        p32 first = 0;

        memory_zero(sizes, 2 * FONT_DOTS * sizeof(b64));
        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
                for (p32 i = first; i <= e->contour[c].last; i++)
                {
                        font_point a = e->points[i], b = e->points[i == e->contour[c].last ? first : i + 1];

                        whole += (f64)a.x * b.y - (f64)b.x * a.y;
                }
        first = 0;
        for (p32 c = 0; c < e->contour_count && c < FONT_DOTS; first = e->contour[c++].last + 1u)
        {
                p32 last = e->contour[c].last;
                b64 left = (b64)1 << 62, bottom = left, right = -left, top = -left;
                f64 area = 0;

                if (last <= first)
                        continue;
                for (p32 i = first; i <= last; i++)
                {
                        font_point a = e->points[i], b = e->points[i == last ? first : i + 1];

                        area += (f64)a.x * b.y - (f64)b.x * a.y;
                        left = a.x < left ? a.x : left;
                        right = a.x > right ? a.x : right;
                        bottom = a.y < bottom ? a.y : bottom;
                        top = a.y > top ? a.y : top;
                }

                b64 width = right - left, height = top - bottom;
                if ((area < 0) != (whole < 0) || width < FONT_ONE / 2 || height < FONT_ONE / 2 ||
                    width > FONT_ONE * 5 / 2 || height > FONT_ONE * 5 / 2 || width > 2 * height || height > 2 * width ||
                    font_dot_inside(e, c, left, bottom, right, top))
                        continue;
                sizes[2 * c] = font_round(width) < FONT_ONE ? FONT_ONE : font_round(width);
                sizes[2 * c + 1] = font_round(height) < FONT_ONE ? FONT_ONE : font_round(height);
        }
}

static fn font_dots(font_engine address_to e, const b64 address_to sizes)
{
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count && c < FONT_DOTS; first = e->contour[c++].last + 1u)
        {
                p32 last = e->contour[c].last;
                b64 left = (b64)1 << 62, bottom = left, right = -left, top = -left;
                b64 across = sizes[2 * c], up = sizes[2 * c + 1];

                if (!across)
                        continue;
                for (p32 i = first; i <= last; i++)
                {
                        left = e->points[i].x < left ? e->points[i].x : left;
                        right = e->points[i].x > right ? e->points[i].x : right;
                        bottom = e->points[i].y < bottom ? e->points[i].y : bottom;
                        top = e->points[i].y > top ? e->points[i].y : top;
                }

                b64 width = right - left, height = top - bottom;
                b64 x = ((left + right - across + FONT_ONE) >> (FONT_SHIFT + 1)) << FONT_SHIFT;
                b64 y = ((bottom + top - up + FONT_ONE) >> (FONT_SHIFT + 1)) << FONT_SHIFT;

                if (width <= 0 || height <= 0)
                        continue;
                for (p32 i = first; i <= last; i++)
                {
                        f64 u = (f64)(e->points[i].x - left) * (f64)across / (f64)width;
                        f64 v = (f64)(e->points[i].y - bottom) * (f64)up / (f64)height;

                        e->points[i].x = x + (b64)(u + 0.5);
                        e->points[i].y = y + (b64)(v + 0.5);
                }
        }
}

//      Horizontal bars, for any fitted text: its baseline is a whole pixel.
//      The axes are swapped so the bars are the stems above, and the
//      fitted lines are kept where the fit put them.
static fn font_bars(font_engine address_to e, const font_face address_to face, p32 size, b32 standard,
                    const b64 address_to fixed, p32 fixes, b64 heavier)
{
        b64 scale = (b64)size * (FONT_ONE / 64);
        b64 normal = font_divide((b64)standard * scale, (b64)face->units << FONT_EXACT) + heavier;
        b64 x_height = font_divide((b64)(face->x_height > 0 ? face->x_height : face->units / 2) * scale, face->units);

        font_swap_axes(e);
        font_snap_axis(e, normal, x_height / FONT_ROUND_BAR, scale / 4, fixed, fixes, FONT_BAR_WHOLE, true);
        font_swap_axes(e);
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

        Most of a really big glyph's row is no edge at all: the inside of a
        stem, the air beside it. Each row of a bitmap over 510 columns wide
        keeps a bit for every block of columns an edge touched, at least 8
        wide and few enough to fit 64 bits, and the sweep fills an untouched
        block with the one value the running sum already holds. Under that a
        row is summed whole, one library call: at 16 cells for 14 vector
        instructions it is 110 instructions for a row of 120, where a call a
        run of blocks was 60 each and a row has a half dozen of them. The
        sweep also leaves every cell it read at zero, so the next band and the
        next glyph start clean without a clearing pass.
*/
static inline fn font_cell_add(font_engine address_to e, b32 row, b32 column, b32 cover, b32 area)
{
        p32 address_to cells = e->cells + (positive)row * (positive)e->stride;

        cells[column] += (p32)(2 * FONT_ONE * cover - area);
        cells[column + 1] += (p32)area;
        if (e->mark)
                e->row[row].touched |= (p64)1 << (column >> e->grain) | (p64)1 << ((column + 1) >> e->grain);
        if (e->track)
                e->row[row].crossed |= (p64)1 << (column >> e->cross);
}

//      The blocks from the one holding column first to the one holding last.
static inline fn font_mark(font_engine address_to e, b32 row, b32 first, b32 last)
{
        if (e->mark)
                e->row[row].touched |= ((p64)2 << (last >> e->grain)) - ((p64)1 << (first >> e->grain));
}

//      The columns from first to last that an edge has a cell in, when the
//      overlap of contours is looked for.
static inline fn font_cross(font_engine address_to e, b32 row, b32 first, b32 last)
{
        if (e->track)
                e->row[row].crossed |= ((p64)2 << (last >> e->cross)) - ((p64)1 << (first >> e->cross));
}

//      A span inside one column: the cover is the height, the area the
//      height times the x where the edge enters and leaves.
static inline fn font_span_one(font_engine address_to e, p32 address_to cells, b32 row, b32 column,
                               b32 xa, b32 xb, b32 cover)
{
        b32 area = cover * ((xa & (FONT_ONE - 1)) + (xb & (FONT_ONE - 1)));

        cells[column] += (p32)(2 * FONT_ONE * cover - area);
        cells[column + 1] += (p32)area;
        font_mark(e, row, column, column + 1);
        font_cross(e, row, column, column);
}

static fn font_span_many(font_engine address_to e, p32 address_to cells, b32 row, b32 xa, b32 xb, b32 cover)
{
        b32 column = xa >> FONT_SHIFT, last = xb >> FONT_SHIFT, mask = FONT_ONE - 1;

        //      The cover each column takes is its share of the x travelled,
        //      by a remainder walk: one division for the first column and
        //      one for the step, each part exact to the nearest.
        bool right = xb > xa;
        b32 step = right ? 1 : -1, leave = right ? FONT_ONE : 0, enter = FONT_ONE - leave;
        b64 travel = right ? (b64)xb - xa : (b64)xa - xb;
        b64 size = cover < 0 ? -(b64)cover : cover;
        b64 first = right ? FONT_ONE - (xa & mask) : xa & mask;
        b64 half = travel >> 1, done = (size * first + half) / travel, carry = (size * first + half) % travel;
        b64 lift = size * FONT_ONE / travel, spare = size * FONT_ONE % travel;
        b32 sign = cover < 0 ? -1 : 1, from = xa & mask;

        font_mark(e, row, right ? column : last, right ? last + 1 : column + 1);
        font_cross(e, row, right ? column : last, right ? last : column);

        b32 part = (b32)done * sign;
        b32 area = part * (from + leave);
        cells[column] += (p32)(2 * FONT_ONE * part - area);
        cells[column + 1] += (p32)area;
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
                cells[column] += (p32)(part * FONT_ONE);
                cells[column + 1] += (p32)(part * FONT_ONE);
        }
        part = cover - (b32)done * sign;
        area = part * (enter + (xb & mask));
        cells[last] += (p32)(2 * FONT_ONE * part - area);
        cells[last + 1] += (p32)area;
}

static inline fn font_span(font_engine address_to e, b32 row, b32 xa, b32 xb, b32 cover)
{
        p32 address_to cells = e->cells + (positive)row * (positive)e->stride;

        if ((xa ^ xb) >> FONT_SHIFT)
                font_span_many(e, cells, row, xa, xb, cover);
        else
                font_span_one(e, cells, row, xa >> FONT_SHIFT, xa, xb, cover);
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
        overlap and both their edges cross one pixel -- a stroke laid over a
        bowl, an ogonek on its letter -- the clamped sum counts the shared
        part twice, and FreeType draws such a pixel up to half too dark. Rows
        where that can happen (below) are drawn again exactly. A row's edges
        are joined into chains, each running one way, up or down, and the row
        is cut into slabs at every chain's ends and every crossing of two,
        inside which the chains keep one order and the winding between
        neighbours is fixed; each run of nonzero winding is then bounded by
        two chains, and its area is laid into the row's differences by its
        two sides, piece by piece, in closed form. The edges are those the
        clamped pass drew, gathered a band of rows at a time into the cells,
        which lie idle meanwhile; a row with more edges or crossings than the
        scratch holds keeps its clamped bytes.
*/
typedef struct
{
        b32 group[FONT_GROUP][4];
        p16 listed[2 * FONT_GROUP];
        p32 listing[34];
        f64 point[3 * FONT_NEAR][2], along[FONT_NEAR], reach[FONT_NEAR];
        p32 start[FONT_NEAR + 1], cursor[FONT_NEAR], active[FONT_NEAR], order[FONT_NEAR];
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

        //      x where the edge crosses each row's boundary, to the nearest,
        //      by a remainder walk: a whole row moves it by lift and spare
        //      over dy, a part row divides afresh, and the edge's own end
        //      needs no division at all -- so an edge inside one row, which
        //      is most of them at text sizes, divides never.
        b64 dx = (b64)x1 - x0, dy = (b64)y1 - y0, half = dy >> 1, at = 0, carry = half, lift = 0, spare = -1;
        b32 ya = y0 < low ? low : y0, top = y1 > high ? high : y1;

        if (ya != y0)
                font_floor_divide(dx * (ya - y0) + half, dy, &at, &carry);

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
                                font_floor_divide(dx * (yb - y0) + half, dy, &at, &carry);
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

//      font_edge for a piece that lies wholly inside the band's rows and the
//      bitmap's columns when nothing is clipped or gathered, which is every
//      piece of a glyph that fits one band: none of the checks, and a piece
//      inside one row, which is most of them, goes straight to its span.
static fn font_trace(font_engine address_to e, b32 x0, b32 y0, b32 x1, b32 y1)
{
        b32 sign = 1;

        if (y0 == y1)
                return;
        if (y0 > y1)
        {
                b32 t = x0;
                x0 = x1, x1 = t, t = y0, y0 = y1, y1 = t, sign = -1;
        }
        if ((y0 >> FONT_SHIFT) == ((y1 - 1) >> FONT_SHIFT))
        {
                font_span(e, (y0 >> FONT_SHIFT) - e->band, x0, x1, (y1 - y0) * sign);
                return;
        }

        b64 dx = (b64)x1 - x0, dy = (b64)y1 - y0, half = dy >> 1, at = 0, carry = half, lift = 0, spare = -1;
        b32 ya = y0, xa = x0;

        //      Across one row's edge, most of the rest: one division, two spans.
        if (((y1 - 1) >> FONT_SHIFT) - (y0 >> FONT_SHIFT) == 1)
        {
                b32 yb = (y1 - 1) & ~(FONT_ONE - 1), xb;

                font_floor_divide(dx * (yb - y0) + half, dy, &at, &carry);
                xb = x0 + (b32)at;
                font_span(e, (y0 >> FONT_SHIFT) - e->band, x0, xb, (yb - y0) * sign);
                font_span(e, (yb >> FONT_SHIFT) - e->band, xb, x1, (y1 - yb) * sign);
                return;
        }

        while (ya < y1)
        {
                b32 yb = (ya & ~(FONT_ONE - 1)) + FONT_ONE;

                if (ya & (FONT_ONE - 1) || yb > y1)
                {
                        yb = yb > y1 ? y1 : yb;
                        if (yb == y1)
                                at = dx;
                        else
                                font_floor_divide(dx * (yb - y0) + half, dy, &at, &carry);
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

//      The cell the path is in, kept in registers while a conic or a line
//      walks, with the band's edges every piece is tested against.
typedef struct
{
        b32 floor, height, wide, band, cell_x, cell_y;
        p32 cover, area;
} font_hand;

static inline fn font_pick_up(const font_engine address_to e, font_hand address_to h)
{
        *h = (font_hand){e->floor_y, e->height_y, e->wide_x, e->band, e->cell_x, e->cell_y, e->cover, e->area};
}

static inline fn font_put_down(font_engine address_to e, const font_hand address_to h)
{
        e->cell_x = h->cell_x, e->cell_y = h->cell_y, e->cover = h->cover, e->area = h->area;
}

//      A piece known to be inside the band and the bitmap: across cells it
//      is traced, inside one it adds to the cell in hand, which is written
//      when the path leaves it.
static inline fn font_piece_in(font_engine address_to e, font_hand address_to h, b32 x0, b32 y0, b32 x1, b32 y1)
{
        if (((x0 ^ x1) | (y0 ^ y1)) >> FONT_SHIFT)
        {
                font_trace(e, x0, y0, x1, y1);
                return;
        }
        if (y0 == y1)
                return;

        b32 row = (y0 >> FONT_SHIFT) - h->band, column = x0 >> FONT_SHIFT;

        if (row != h->cell_y || column != h->cell_x)
        {
                if (h->cover | h->area)
                        font_cell_add(e, h->cell_y, h->cell_x, (b32)h->cover, (b32)h->area);
                h->cover = h->area = 0;
                h->cell_x = column;
                h->cell_y = row;
        }
        h->cover += (p32)(y1 - y0);
        h->area += (p32)((y1 - y0) * ((x0 & (FONT_ONE - 1)) + (x1 & (FONT_ONE - 1))));
}

//      Any piece: the plain ones inside the band and the bitmap go the short
//      way (inside says the caller has already seen that they do), the rest,
//      clipped, gathered or across a band's edge, to font_edge.
static inline fn font_piece(font_engine address_to e, font_hand address_to h, bool inside,
                            b32 x0, b32 y0, b32 x1, b32 y1)
{
        if (inside || (e->direct && (p32)(y0 - h->floor) <= (p32)h->height && (p32)(y1 - h->floor) <= (p32)h->height &&
                       (p32)x0 <= (p32)h->wide && (p32)x1 <= (p32)h->wide))
                font_piece_in(e, h, x0, y0, x1, y1);
        else
                font_edge(e, x0, y0, x1, y1);
}

//      A quadratic in n even steps, the fewest that leave each piece's
//      second difference at most FONT_FLAT. The point at step i is the start
//      plus (2 b i n + a i^2) / n^2, walked as a forward difference in 32.32:
//      each step adds the first difference, which adds the second. The two
//      differences and the shift are over the same n^2, so they are products
//      with one reciprocal of it, 2^64 / n^2 to the floor, each product an
//      unrounded 32.32 quotient short of its floor by under num / 2^32 of
//      the last place (a walk of n steps is then under n^2 num / 2^64 units
//      off: 2^-12 of a unit at the longest n a glyph the engine draws needs).
static inline b64 font_scaled(b64 numerator, p64 reciprocal)
{
        return (b64)(((__int128)numerator * (__int128)reciprocal) >> 32);
}

//      The pieces of a conic: its points from the forward differences, each
//      joined to the one before, the last to the conic's end.
static inline fn font_pieces(font_engine address_to e, font_hand address_to h, bool inside,
                             b32 x0, b32 y0, b32 x, b32 y, b64 n, b64 ux, b64 sx, b64 bx,
                             b64 uy, b64 sy, b64 by, b64 hx, b64 hy)
{
        //      The first point and the last take half a step more than the
        //      rest, both of them on a conic of two pieces.
        b64 first = n == 2 ? 2 : 1;
        b32 px = x0, py = y0, qx, qy;

        ux += sx, sx += bx, uy += sy, sy += by;
        qx = x0 + (b32)((ux + first * hx) >> 32), qy = y0 + (b32)((uy + first * hy) >> 32);
        font_piece(e, h, inside, px, py, qx, qy);
        px = qx, py = qy;
        for (b64 i = 2; i < n - 1; i++)
        {
                ux += sx, sx += bx, uy += sy, sy += by;
                qx = x0 + (b32)(ux >> 32), qy = y0 + (b32)(uy >> 32);
                font_piece(e, h, inside, px, py, qx, qy);
                px = qx, py = qy;
        }
        if (n > 2)
        {
                ux += sx, sx += bx, uy += sy, sy += by;
                qx = x0 + (b32)((ux + hx) >> 32), qy = y0 + (b32)((uy + hy) >> 32);
                font_piece(e, h, inside, px, py, qx, qy);
                px = qx, py = qy;
        }
        font_piece(e, h, inside, px, py, x, y);
}

static fn font_conic(font_engine address_to e, b32 cx, b32 cy, b32 x, b32 y)
{
        b32 x0 = e->x, y0 = e->y;
        b32 low = e->band * FONT_ONE, high = (e->band + e->rows) * FONT_ONE;
        font_hand h;

        e->x = x;
        e->y = y;
        if ((y0 >= high && cy >= high && y >= high) || (y0 < low && cy < low && y < low))
                return;
        b64 ax = (b64)x0 - 2 * (b64)cx + x, ay = (b64)y0 - 2 * (b64)cy + y;
        b64 bow = ax < 0 ? -ax : ax, rise = ay < 0 ? -ay : ay;

        font_pick_up(e, &h);
        bow = bow > rise ? bow : rise;
        if (bow <= FONT_STRAIGHT)
        {
                font_piece(e, &h, false, x0, y0, x, y);
                font_put_down(e, &h);
                return;
        }

        b64 n = (b64)__builtin_sqrt((f64)bow / FONT_FLAT);
        while (n * n * FONT_FLAT < bow)
                n++;
        n = n < 2 ? 2 : n;

        b64 chord = x - x0 < 0 ? x0 - x : x - x0, lift = y - y0 < 0 ? y0 - y : y - y0;
        f64 reach = (f64)bow * (f64)(chord > lift ? chord : lift) / FONT_REACH;
        while ((f64)(n * n * n) < reach && n < FONT_PIECES)
                n += (n >> 3) + 1;
        n = n > FONT_PIECES ? FONT_PIECES : n;

        //      The walk starts half a unit up, so the floor of the 32.32
        //      position is its nearest 1024th, and a sixth of a second
        //      difference toward the curve's side (see the arithmetic above).
        p64 reciprocal = ~(p64)0 / (p64)(n * n);
        b64 shx = font_scaled(-ax, reciprocal) / 6, shy = font_scaled(-ay, reciprocal) / 6;
        b64 ux = ((b64)1 << 31) + shx, sx = font_scaled(2 * ((b64)cx - x0) * n + ax, reciprocal), bx = font_scaled(2 * ax, reciprocal);
        b64 uy = ((b64)1 << 31) + shy, sy = font_scaled(2 * ((b64)cy - y0) * n + ay, reciprocal), by = font_scaled(2 * ay, reciprocal);

        //      The pieces stay within a few 1024ths of the three points'
        //      box, so a conic whose points are well inside the band and the
        //      bitmap needs no test on any of its pieces.
        bool inside = e->direct;
        b32 near = 8;

        inside = inside && (p32)(x0 - near) <= (p32)(h.wide - 2 * near) && (p32)(cx - near) <= (p32)(h.wide - 2 * near) &&
                 (p32)(x - near) <= (p32)(h.wide - 2 * near);
        inside = inside && (p32)(y0 - h.floor - near) <= (p32)(h.height - 2 * near) &&
                 (p32)(cy - h.floor - near) <= (p32)(h.height - 2 * near) &&
                 (p32)(y - h.floor - near) <= (p32)(h.height - 2 * near);
        if (inside)
                font_pieces(e, &h, true, x0, y0, x, y, n, ux, sx, bx, uy, sy, by, shx >> 1, shy >> 1);
        else
                font_pieces(e, &h, false, x0, y0, x, y, n, ux, sx, bx, uy, sy, by, shx >> 1, shy >> 1);
        font_put_down(e, &h);
}

static fn font_line(font_engine address_to e, b32 x, b32 y)
{
        font_hand h;

        font_pick_up(e, &h);
        font_piece(e, &h, false, e->x, e->y, x, y);
        font_put_down(e, &h);
        e->x = x;
        e->y = y;
}

/*
        Where the clamped sum is wrong. It counts a point twice only where
        two contours wound alike both cover it, and inside a row a contour
        covers nothing outside the columns from its first edge to its last:
        so each contour notes, row by row, the columns its edges are in, and
        a row where the span from the first to the last meets that of the
        contours wound the same way before it is drawn again as the union.
        Every other row keeps the sum, which is exact there. One contour's
        own loops are not looked for.
*/
//      Whether any two contours wound alike have boxes that meet, which
//      the spans are needed for; each contour's winding kept for the walk.
//      The windings come first, and the boxes only of contours that have
//      another wound as they are. Past 64 contours the boxes are not
//      compared and the spans decide.
static bool font_tracking(font_engine address_to e)
{
        b32 box[64][4];
        p32 first = 0, alike[2] = {0, 0};

        //      One contour has none to overlap, and its winding is looked
        //      at only for the rows two of them are drawn again.
        if (e->contour_count < 2)
                return false;
        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
        {
                b64 area = 0;

                for (p32 i = first; i <= e->contour[c].last && i < e->point_count; i++)
                {
                        const font_point address_to p = &e->points[i];
                        const font_point address_to q = &e->points[i == e->contour[c].last ? first : i + 1];

                        area += (b64)p->x * q->y - (b64)q->x * p->y;
                }
                e->contour[c].wind = area < 0;
                alike[area < 0]++;
        }
        if (e->contour_count > 64)
                return true;
        if (alike[0] < 2 && alike[1] < 2)
                return false;

        bool meet = false;

        first = 0;
        for (p32 c = 0; c < e->contour_count && !meet; first = e->contour[c++].last + 1u)
        {
                b32 b[4] = {0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff};

                if (alike[e->contour[c].wind] < 2)
                        continue;
                for (p32 i = first; i <= e->contour[c].last && i < e->point_count; i++)
                {
                        const font_point address_to p = &e->points[i];

                        b[0] = p->x < b[0] ? (b32)p->x : b[0], b[1] = p->y < b[1] ? (b32)p->y : b[1];
                        b[2] = p->x > b[2] ? (b32)p->x : b[2], b[3] = p->y > b[3] ? (b32)p->y : b[3];
                }
                memory_copy(box[c], b, sizeof(b));
                for (p32 k = 0; k < c && !meet; k++)
                        meet = e->contour[k].wind == e->contour[c].wind && box[k][0] < b[2] && b[0] < box[k][2] &&
                               box[k][1] < b[3] && b[1] < box[k][3];
        }
        return meet;
}

static fn font_marks(font_engine address_to e)
{
        if (e->gather || !e->track)
                return;
        font_flush(e);
        for (b32 r = 0; r < e->rows; r++)
        {
                font_row address_to row = &e->row[r];
                p64 crossed = row->crossed;

                if (!crossed)
                        continue;

                p64 reach = ((p64)2 << (63 - bits_leading_zeros(crossed))) - ((p64)1 << bits_trailing_zeros(crossed));

                row->twice[e->wound] |= reach & row->there[e->wound];
                row->there[e->wound] |= reach;
                row->crossed = 0;
        }
}

//      The rows where two contours wound alike reach one column, which the
//      union is drawn again for.
static fn font_overlaps(font_engine address_to e)
{
        if (e->gather || !e->track)
                return;
        for (b32 r = 0; r < e->rows; r++)
                if (e->row[r].twice[0] | e->row[r].twice[1])
                        e->shared[(e->band + r) >> 6] |= (p64)1 << ((e->band + r) & 63);
}

//      The contours as TrueType writes them: an off-curve point between two
//      on-curve ones is a control, and two off-curve points in a row imply
//      the on-curve point midway.
static fn font_walk(font_engine address_to e)
{
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
        {
                p32 last = e->contour[c].last, i = first, stop = last;
                font_point address_to p = e->points;
                b32 sx, sy, cx = 0, cy = 0;
                bool pending = false;

                if (last < first || last >= e->point_count)
                        break;
                if (c)
                        font_marks(e);
                e->wound = e->contour[c].wind;
                if (e->points[first].on)
                {
                        sx = (b32)p[first].x, sy = (b32)p[first].y;
                        i = first + 1;
                }
                else if (e->points[last].on)
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

                        if (!e->points[i].on)
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
        font_marks(e);
        font_overlaps(e);
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

//      A chain's x at y, its piece found from the cursor up.
static f64 font_chain_at(const font_scratch address_to s, p32 chain, p32 address_to at, f64 y)
{
        p32 last = (s->start[chain + 1] & 0x7fffffff) - 1;

        while (*at + 1 < last && s->point[*at + 1][1] <= y)
                ++*at;

        const f64 address_to p = s->point[*at], address_to q = s->point[*at + 1];
        return p[0] + (y - p[1]) * (q[0] - p[0]) / (q[1] - p[1]);
}

//      One side of a run of nonzero winding across a slab of height h, the
//      edge from xa at its foot to xb at its top, into the row's differences:
//      the pixels it crosses take their part of the area right of it, every
//      pixel past it the whole slab; sign is +1 for a run's left side and -1
//      for its right.
static fn font_side(f64 address_to step, b32 width, f64 xa, f64 xb, f64 h, f64 sign)
{
        f64 least = xa < xb ? xa : xb, most = xa < xb ? xb : xa, before = 0;
        b32 first = least < 0 ? 0 : (b32)(least / FONT_ONE);
        b32 last = most / FONT_ONE >= width ? width - 1 : (b32)(most / FONT_ONE);

        if (first > last)
                return;
        for (b32 c = first; c <= last; c++)
        {
                f64 x0 = (f64)c * FONT_ONE, part = sign * (font_left_of(x0 + FONT_ONE, xa, xb, h) - font_left_of(x0, xa, xb, h));

                step[c] += part - before;
                before = part;
        }
        step[last + 1] += sign * h * FONT_ONE - before;
}

static fn font_union_row(font_engine address_to e, b32 r, b32 width, p8 address_to row)
{
        font_scratch address_to s = (font_scratch address_to)e->cells;
        f64 low = (f64)r * FONT_ONE, high = low + FONT_ONE;
        p32 chains = 0, points = 0, events = 2, previous = 0x7fffffff;
        bool rising = false;

        //      The row's edges, clipped to it, joined into chains: a piece
        //      that starts where the one before it ended, going the same way
        //      up or down, continues its chain. A chain's points are kept in
        //      rising order and its winding in the top bit of its start.
        for (p32 at = s->listing[r - e->band]; at < s->listing[r - e->band + 1]; at++)
        {
                p32 index = s->listed[at];
                const b32 address_to g = s->group[index];
                bool up = g[1] < g[3];
                f64 ya = up ? g[1] : g[3], yb = up ? g[3] : g[1], xa = up ? g[0] : g[2], xb = up ? g[2] : g[0];

                if (yb <= low || ya >= high || ya == yb)
                        continue;

                f64 a = ya > low ? ya : low, b = yb < high ? yb : high, slope = (xb - xa) / (yb - ya);
                f64 xl = xa + (a - ya) * slope, xh = xa + (b - ya) * slope;
                bool joins = chains && index == previous + 1 && up == rising && s->group[index - 1][2] == g[0] &&
                             s->group[index - 1][3] == g[1];

                if (points + 2 > 3 * FONT_NEAR || (!joins && chains == FONT_NEAR))
                {
                        e->used[2] = FONT_NEAR;
                        e->used[4] = 3 * FONT_NEAR;
                        return;
                }
                if (!joins)
                {
                        if (chains && !rising)
                                for (p32 i = s->start[chains - 1] & 0x7fffffff, j = points - 1; i < j; i++, j--)
                                {
                                        f64 x = s->point[i][0], y = s->point[i][1];
                                        s->point[i][0] = s->point[j][0], s->point[i][1] = s->point[j][1];
                                        s->point[j][0] = x, s->point[j][1] = y;
                                }
                        s->start[chains++] = points | (up ? 0 : 0x80000000u);
                        s->point[points][0] = up ? xl : xh, s->point[points++][1] = up ? a : b;
                }
                s->point[points][0] = up ? xh : xl, s->point[points++][1] = up ? b : a;
                previous = index, rising = up;
        }
        if (chains && !rising)
                for (p32 i = s->start[chains - 1] & 0x7fffffff, j = points - 1; i < j; i++, j--)
                {
                        f64 x = s->point[i][0], y = s->point[i][1];
                        s->point[i][0] = s->point[j][0], s->point[i][1] = s->point[j][1];
                        s->point[j][0] = x, s->point[j][1] = y;
                }
        s->start[chains] = points;
        e->used[2] = chains > e->used[2] ? chains : e->used[2];
        e->used[4] = points > e->used[4] ? points : e->used[4];
        if (!chains)
                return;

        //      The slabs: the row's ends, every chain's ends, and every
        //      crossing of two chains, which can only be two whose reaches
        //      along x meet -- found between consecutive points of either,
        //      where both are straight and the gap between them is too.
        s->events[0] = low, s->events[1] = high;
        for (p32 c = 0; c < chains; c++)
        {
                p32 first = s->start[c] & 0x7fffffff, last = (s->start[c + 1] & 0x7fffffff) - 1, j = c;
                f64 least = s->point[first][0], most = least;

                for (p32 i = first + 1; i <= last; i++)
                {
                        least = s->point[i][0] < least ? s->point[i][0] : least;
                        most = s->point[i][0] > most ? s->point[i][0] : most;
                }
                for (; j && s->reach[j - 1] > least; j--)
                {
                        s->reach[j] = s->reach[j - 1];
                        s->along[j] = s->along[j - 1];
                        s->order[j] = s->order[j - 1];
                }
                s->reach[j] = least, s->along[j] = most, s->order[j] = c;
                if (events + 2 > FONT_EVENTS)
                {
                        e->used[3] = FONT_EVENTS;
                        return;
                }
                s->events[events++] = s->point[first][1];
                s->events[events++] = s->point[last][1];
        }
        for (p32 o = 0; o < chains; o++)
                for (p32 q = o + 1; q < chains && s->reach[q] <= s->along[o]; q++)
                {
                        p32 one = s->order[o], two = s->order[q];
                        p32 i = s->start[one] & 0x7fffffff, j = s->start[two] & 0x7fffffff;
                        p32 ia = i, ja = j;
                        f64 from = s->point[i][1] > s->point[j][1] ? s->point[i][1] : s->point[j][1];
                        f64 to = s->point[(s->start[one + 1] & 0x7fffffff) - 1][1];
                        f64 top = s->point[(s->start[two + 1] & 0x7fffffff) - 1][1];

                        to = to < top ? to : top;
                        if (to <= from)
                                continue;

                        f64 y = from, gap = font_chain_at(s, one, &ia, y) - font_chain_at(s, two, &ja, y);
                        while (y < to)
                        {
                                f64 next = to;
                                if (ia + 1 < (s->start[one + 1] & 0x7fffffff) && s->point[ia + 1][1] > y)
                                        next = s->point[ia + 1][1] < next ? s->point[ia + 1][1] : next;
                                if (ja + 1 < (s->start[two + 1] & 0x7fffffff) && s->point[ja + 1][1] > y)
                                        next = s->point[ja + 1][1] < next ? s->point[ja + 1][1] : next;

                                f64 later = font_chain_at(s, one, &ia, next) - font_chain_at(s, two, &ja, next);
                                if ((gap < 0 && later > 0) || (gap > 0 && later < 0) || later == 0)
                                {
                                        if (events == FONT_EVENTS)
                                        {
                                                e->used[3] = FONT_EVENTS;
                                                return;
                                        }
                                        s->events[events++] = later == 0 ? next : y + (next - y) * gap / (gap - later);
                                }
                                y = next, gap = later;
                        }
                }
        font_sort(s->events, events);
        e->used[3] = events > e->used[3] ? events : e->used[3];

        memory_zero(s->cover, ((positive)width + 2) * sizeof(s->cover[0]));

        //      A sweep up the row: the chains spanning each slab, in order
        //      along it, and each run of nonzero winding between two of them
        //      integrated by its sides, piece by piece.
        for (p32 c = 0; c < chains; c++)
                s->cursor[c] = s->start[c] & 0x7fffffff;
        for (p32 k = 0; k + 1 < events; k++)
        {
                f64 a = s->events[k], b = s->events[k + 1], middle = (a + b) / 2;
                p32 count = 0;
                b32 winding = 0, begin = 0;

                if (b <= a)
                        continue;
                for (p32 c = 0; c < chains; c++)
                {
                        p32 first = s->start[c] & 0x7fffffff, last = (s->start[c + 1] & 0x7fffffff) - 1;

                        if (s->point[first][1] >= middle || s->point[last][1] <= middle)
                                continue;
                        while (s->point[s->cursor[c] + 1][1] <= a)
                                s->cursor[c]++;

                        p32 at = s->cursor[c], j = count++;
                        f64 x = font_chain_at(s, c, &at, middle);

                        for (; j && s->along[j - 1] > x; j--)
                        {
                                s->along[j] = s->along[j - 1];
                                s->active[j] = s->active[j - 1];
                        }
                        s->along[j] = x, s->active[j] = c;
                }
                for (p32 i = 0; i < count; i++)
                {
                        p32 chain = s->active[i];
                        b32 before = winding;

                        winding += s->start[chain] >> 31 ? -1 : 1;
                        if (!before && winding)
                                begin = (b32)i;
                        if (!before || winding)
                                continue;
                        for (p32 side = 0; side < 2; side++)
                        {
                                p32 c = side ? chain : s->active[begin];
                                p32 last = (s->start[c + 1] & 0x7fffffff) - 1;

                                for (p32 at = s->cursor[c]; at < last && s->point[at][1] < b; at++)
                                {
                                        const f64 address_to p = s->point[at], address_to q = s->point[at + 1];
                                        f64 y0 = p[1] > a ? p[1] : a, y1 = q[1] < b ? q[1] : b;

                                        if (y1 <= y0)
                                                continue;

                                        f64 slope = (q[0] - p[0]) / (q[1] - p[1]);
                                        font_side(s->cover, width, p[0] + (y0 - p[1]) * slope, p[0] + (y1 - p[1]) * slope,
                                                  y1 - y0, side ? -1 : 1);
                                }
                        }
                }
        }
        f64 sum = 0;
        for (b32 c = 0; c < width; c++)
        {
                sum += s->cover[c];

                f64 area = sum / ((f64)FONT_ONE * FONT_ONE);
                row[c] = (p8)(255 * (area < 0 ? 0 : area > 1 ? 1 : area) + 0.5);
        }
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
                        e->used[1] = total > e->used[1] ? total : e->used[1];
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
        font_scratch address_to s = (font_scratch address_to)e->cells;
        b32 most = 32;

        memory_zero(e->used, sizeof(e->used));
        e->gather = true;
        for (e->band = from; e->band < to;)
        {
                e->rows = to - e->band < most ? to - e->band : most;
                e->gathered = 0;
                e->overflow = false;
                font_walk(e);
                //      What the walk gathered is noted before a band that overflowed is tried again smaller: the
                //      cleanup below zeroes what was used, and the records of the band given up are in it.
                e->used[0] = e->gathered > e->used[0] ? e->gathered : e->used[0];
                if (e->overflow && e->rows > 1)
                {
                        most = e->rows / 2;
                        continue;
                }
                if (!e->overflow && font_list(e))
                        for (b32 r = e->band; r < e->band + e->rows; r++)
                                if (e->shared[r >> 6] >> (r & 63) & 1)
                                        font_union_row(e, r, width, out + (positive)(height - 1 - r) * stride);
                e->band += e->rows;
                most = most < 16 ? most * 2 : 32;
        }
        e->gather = false;

        //      The cells are zero again for the next glyph: what the scratch
        //      used of them, which is little of the quarter megabyte.
        p32 near = e->used[2] < FONT_NEAR ? e->used[2] + 1 : FONT_NEAR;
        memory_zero(s->group, (positive)(e->used[0] < FONT_GROUP ? e->used[0] + 1 : FONT_GROUP) * sizeof(s->group[0]));
        memory_zero(s->listed, (positive)(e->used[1] < 2 * FONT_GROUP ? e->used[1] + 1 : 2 * FONT_GROUP) * 2);
        memory_zero(s->listing, sizeof(s->listing));
        memory_zero(s->point, (positive)(e->used[4] < 3 * FONT_NEAR ? e->used[4] + 1 : 3 * FONT_NEAR) * sizeof(s->point[0]));
        memory_zero(s->along, near * 8), memory_zero(s->reach, near * 8);
        memory_zero(s->start, (near + 1) * 4), memory_zero(s->cursor, near * 4);
        memory_zero(s->active, near * 4), memory_zero(s->order, near * 4);
        memory_zero(s->events, (positive)(e->used[3] < FONT_EVENTS ? e->used[3] + 2 : FONT_EVENTS) * 8);
        memory_zero(s->cover, (positive)(width + 2) * 8);
}

static fn font_raster(font_engine address_to e, b32 width, b32 height,
                      p8 address_to out, positive stride)
{
        e->width = width;
        e->stride = width + 2;
        for (e->grain = 3; (width + 1) >> e->grain >= 64;)
                e->grain++;
        memory_zero(e->shared, ((positive)height + 63) / 64 * 8);
        for (e->cross = 0; (width + 1) >> e->cross >= 64;)
                e->cross++;
        e->track = font_tracking(e);
        e->mark = width + 2 > 512;

        b32 most = FONT_BAND / e->stride < FONT_ROWS ? FONT_BAND / e->stride : FONT_ROWS;
        for (e->band = 0; e->band < height; e->band += e->rows)
        {
                e->rows = height - e->band < most ? height - e->band : most;
                e->cover = e->area = 0;
                e->cell_y = -1;
                for (b32 r = 0; r < e->rows && e->track; r++)
                        e->row[r].crossed = e->row[r].there[0] = e->row[r].there[1] = e->row[r].twice[0] = e->row[r].twice[1] = 0;
                e->floor_y = e->band * FONT_ONE, e->height_y = e->rows * FONT_ONE, e->wide_x = width * FONT_ONE;
                e->direct = !e->clip;
                font_walk(e);
                e->direct = false;
                font_flush(e);

                for (b32 r = 0; r < e->rows; r++)
                {
                        p32 address_to cells = e->cells + (positive)r * (positive)e->stride, sum = 0;
                        p8 address_to row = out + (positive)(height - 1 - e->band - r) * stride;
                        p64 touched = e->row[r].touched;

                        //      A narrow row has no blocks and is summed whole.
                        //      Otherwise a run of blocks no edge touched is one
                        //      level; a run of touched ones is summed, made
                        //      bytes and cleared by the library.
                        e->row[r].touched = 0;
                        if (!e->mark)
                                memory_sweep_coverage(row, cells, (positive)width, 0);
                        for (b32 x = 0; e->mark && x < width;)
                        {
                                b32 block = x >> e->grain;
                                p64 rest = touched >> block;
                                b32 next = (block + bits_trailing_zeros(rest & 1 ? ~rest : rest)) << e->grain;

                                next = next < width ? next : width;
                                if (rest & 1)
                                        sum = memory_sweep_coverage(row + x, cells + x, (positive)(next - x), sum);
                                else
                                        memory_fill(row + x, (b8)font_level(sum), (positive)(next - x));
                                x = next;
                        }
                        cells[width] = cells[width + 1] = 0;
                }
        }
        b32 from = -1, to = 0;
        for (b32 w = 0; w < (height + 63) / 64; w++)
                if (e->shared[w])
                {
                        from = from < 0 ? w * 64 + (b32)bits_trailing_zeros(e->shared[w]) : from;
                        to = w * 64 + 64 - (b32)bits_leading_zeros(e->shared[w]);
                }
        if (from >= 0)
                font_union(e, width, from, to, height, out, stride);
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
        if (size >= FONT_SIZE_LIMIT)
                return FONT_TOO_LARGE;

        b32 standard = style & 4 ? font_stem_standard(e, face) : 0;
        b32 zones[FONT_ZONES] = {0};
        if (style & 1)
                font_face_measure(e, face, zones);
        b32 refused = font_outline(e, face, glyph, 0);
        if (refused < 0 || !e->point_count)
                return refused;

        b64 lsb = glyph < face->metrics
                      ? font_i16(face, face->hmtx + 4 * (positive)glyph + 2)
                      : font_i16(face, face->hmtx + 4 * (positive)face->metrics +
                                           2 * (positive)(glyph - face->metrics));
        b64 shift = (lsb - e->left_side) * (1 << FONT_EXACT);
        b64 nudge = (b64)phase * (FONT_ONE / FONT_PHASES);

        //      value * size / 2^14 / units to the nearest, halves away from
        //      zero, is the floor of ((|value * size| + units * 2^13) >> 14)
        //      over units, and that floor is a multiply-high by the
        //      reciprocal rounded up, exact while the quotient times units
        //      stays under 2^64, which units of 16384 or fewer and a
        //      numerator under 2^48 do.
        p64 reciprocal = ~(p64)0 / face->units + 1, bias = (p64)face->units << 13;

        for (p32 i = 0; i < e->point_count; i++)
        {
                //      y is clamped as x is: 8192 points of a crafted glyph can sum to 2^28 font units, and
                //      scaled by a size of 512 px or more that product was past 2^63 (signed overflow).
                b64 x = font_exact(e->points[i].x + shift) * size * (FONT_ONE / 64),
                    y = font_exact(e->points[i].y) * size * (FONT_ONE / 64);
                p64 mx = x < 0 ? -(p64)x : (p64)x, my = y < 0 ? -(p64)y : (p64)y;
                b64 qx = (b64)(((unsigned __int128)((mx + bias) >> 14) * reciprocal) >> 64);
                b64 qy = (b64)(((unsigned __int128)((my + bias) >> 14) * reciprocal) >> 64);

                e->points[i].x = (x < 0 ? -qx : qx) + nudge;
                e->points[i].y = y < 0 ? -qy : qy;
        }
        //      The weight first, then the fit: darkening and synthetic bold grow
        //      the outline every way, and grown after the fit they took every
        //      fitted edge off its pixel (a third of the flat edges of dark
        //      text, against an eighth of light, over 19 faces at 8 to 32 px).
        b64 grown = 0;
        if (style & 2)
        {
                b32 pixels = (b32)(size >> 6), until = (b32)e->darken_until;
                b32 amount = pixels <= 12 ? (b32)e->darken
                             : pixels >= until ? 0
                             : (b32)e->darken * (until - pixels) / (until - 12);
                if (amount > 0)
                {
                        font_embolden(e, (f32)(FONT_ONE / 64) * amount, false);
                        grown += (b64)amount * (FONT_ONE / 64) / 2;
                }
        }
        b64 heavier = style & 16 ? (b64)size * (FONT_ONE / 64) / 24 : 0;
        if (heavier)
        {
                //      Synthetic bold, for a face that has no bold: a 24th of
                //      the size, half each side, the weight a bold face adds.
                font_embolden(e, (f32)heavier, false);
                grown += heavier / 2;
        }
        b64 lines[3];
        p32 fixes = style & 1 ? font_fit(e, face, size, zones, grown, lines) : 0;
        if (style & 8)
        {
                //      Synthetic italic: a 12 degree slant about half the
                //      x-height, so a lowercase letter leans out of its cell
                //      by as little at the top as at the bottom. Its stems are
                //      no longer upright, so they are not snapped; its bars
                //      still are.
                b64 mid = font_divide((b64)(face->x_height > 0 ? face->x_height : face->units / 2) * size *
                                          (FONT_ONE / 64),
                                      2 * (b64)face->units);
                for (p32 i = 0; i < e->point_count; i++)
                {
                        f64 lean = (f64)(e->points[i].y - mid) * 0.21255656167002213;
                        e->points[i].x += (b64)(lean < 0 ? lean - 0.5 : lean + 0.5);
                }
        }
        //      Stems under three quarters of a pixel (Plex Mono at 8 and 9 px) are thickened across to a whole
        //      pixel and snapped whole: grown in both directions the bars and the lines would move, and snapped
        //      whole at their own width the diagonal between two black stems, N's, faded to nothing.
        b64 dots[2 * FONT_DOTS];
        if (style & 1)
                font_dots_measure(e, dots);
        b64 thin = 0;
        if (standard && !(style & 8))
        {
                b64 normal = font_divide((b64)standard * size * (FONT_ONE / 64), (b64)face->units << FONT_EXACT) + heavier;

                thin = normal >= FONT_ONE / 2 && normal < FONT_ONE * 3 / 4 && FONT_THIN_TO > normal ? FONT_THIN_TO - normal : 0;
                if (thin > 0)
                        font_embolden(e, (f32)thin, true);
                font_snap(e, face, size, standard, heavier + thin);
        }
        if (fixes && zones[3])
                font_bars(e, face, size, zones[3], lines, fixes, heavier);
        if (style & 1)
                font_dots(e, dots);

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
               ((flags & FONT_DARKEN) && e->darken && size < e->darken_until * 64 ? 2 : 0) |
               (flags & FONT_OBLIQUE ? 8 : 0) | (flags & FONT_EMBOLDEN ? 16 : 0);
}

//      Lightness of a colour as 0.30 red, 0.59 green and 0.11 blue, of 65280.
static inline p32 font_luma(p32 colour)
{
        return 77 * (colour >> 16 & 255) + 150 * (colour >> 8 & 255) + 29 * (colour & 255);
}

//      Text darker than its ground by three quarters of the way from black
//      to white or more -- black or near it on a light ground -- is
//      darkened unasked, unless the caller says FONT_UNDARKENED. Lighter
//      ink is not: its edge pixels are already fuller of it in linear light
//      (darkened, 909090 on white read 1.21 times its ink at 48 px at 8 px,
//      and white on black 1.23 to 1.39 at 8 to 11). On a ground not known,
//      text under half light is taken for dark on white.
static inline p32 font_polarity(p32 flags, p32 colour, p32 background)
{
        p32 ink = font_luma(colour);
        p32 ground = background & FONT_UNKNOWN ? (ink < 32640 ? 65280 : 0) : font_luma(background);

        if (flags & FONT_UNDARKENED)
                return flags & ~(p32)(FONT_DARKEN | FONT_UNDARKENED);
        return ink + 48960 <= ground ? flags | FONT_DARKEN : flags;
}

/*
        How strong dark text's contrast curve is at a size, in 64ths of k:
        e->contrast where the text is not darkened, and where it is, a
        quarter of that at 12 px and under, back to all of it at the size
        darkening has faded out by, as darkening fades. Darkening gives
        small text the weight the curve was giving it, and both at once was
        1.13 to 1.25 times the ink at 48 px at 8 to 12 px.
*/
static p32 font_strength(const font_engine address_to e, p32 size, p32 flags)
{
        b32 pixels = (b32)(size >> 6), until = (b32)e->darken_until;

        if (!(font_style(e, size, flags) & 2) || until <= 12)
                return e->contrast;
        b32 faded = pixels <= 12 ? 0 : pixels >= until ? 64 : 64 * (pixels - 12) / (until - 12);
        return (p32)((b64)e->contrast * (16 + 48 * faded / 64) / 64);
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
        Cells drawn rather than read from a font: box drawing, block elements,
        braille and the shapes below, sized to the cell so that each joins its
        neighbours.

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
        e->points[e->point_count++].on = on;
}

static fn font_close(font_engine address_to e)
{
        if (e->point_count && e->contour_count < FONT_CONTOURS)
                e->contour[e->contour_count++].last = (p16)(e->point_count - 1);
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

/*
        Shapes that no face the terminal draws with has: the squares, triangles,
        diamonds and circles of U+25A0-25FF that programs draw (btop's meters
        and sort marks among them), and U+21B5 and U+2717. Each is outlined
        about the middle of the cell with a half size of 0.36 of the width
        or 0.2 of the height, whichever is less, and a small one half of
        that; a hollow one has a light line's stroke inside its outline. Every
        contour of a shape is wound the same way, so the parts of one shape add
        and a hollow one is a hole. Entries are the shape, HOLLOW or SMALL.
*/
#define SHAPE_SQUARE 1
#define SHAPE_RECT 2
#define SHAPE_UP 3
#define SHAPE_DOWN 4
#define SHAPE_RIGHT 5
#define SHAPE_LEFT 6
#define SHAPE_DIAMOND 7
#define SHAPE_CIRCLE 8
#define SHAPE_HOLLOW 0x10
#define SHAPE_SMALL 0x20
#define SHAPE_ARROW 0x40
#define SHAPE_CROSS 0x80

static const p8 font_drawn_kind[96] = {
        [0x00] = SHAPE_SQUARE,                 [0x01] = SHAPE_SQUARE | SHAPE_HOLLOW,        // ■ □
        [0x0a] = SHAPE_SQUARE | SHAPE_SMALL,   [0x0b] = SHAPE_SQUARE | SHAPE_SMALL | SHAPE_HOLLOW, // ▪ ▫
        [0x0c] = SHAPE_RECT,                                                              // ▬
        [0x12] = SHAPE_UP,                     [0x13] = SHAPE_UP | SHAPE_HOLLOW,            // ▲ △
        [0x14] = SHAPE_UP | SHAPE_SMALL,       [0x15] = SHAPE_UP | SHAPE_SMALL | SHAPE_HOLLOW, // ▴ ▵
        [0x16] = SHAPE_RIGHT,                  [0x17] = SHAPE_RIGHT | SHAPE_HOLLOW,         // ▶ ▷
        [0x18] = SHAPE_RIGHT | SHAPE_SMALL,    [0x19] = SHAPE_RIGHT | SHAPE_SMALL | SHAPE_HOLLOW, // ▸ ▹
        [0x1a] = SHAPE_RIGHT,                                                             // ►
        [0x1c] = SHAPE_DOWN,                   [0x1d] = SHAPE_DOWN | SHAPE_HOLLOW,          // ▼ ▽
        [0x1e] = SHAPE_DOWN | SHAPE_SMALL,     [0x1f] = SHAPE_DOWN | SHAPE_SMALL | SHAPE_HOLLOW, // ▾ ▿
        [0x20] = SHAPE_LEFT,                   [0x21] = SHAPE_LEFT | SHAPE_HOLLOW,          // ◀ ◁
        [0x22] = SHAPE_LEFT | SHAPE_SMALL,     [0x23] = SHAPE_LEFT | SHAPE_SMALL | SHAPE_HOLLOW, // ◂ ◃
        [0x24] = SHAPE_LEFT,                                                              // ◄
        [0x26] = SHAPE_DIAMOND,                [0x27] = SHAPE_DIAMOND | SHAPE_HOLLOW,       // ◆ ◇
        [0x2a] = SHAPE_DIAMOND | SHAPE_HOLLOW,                                            // ◊
        [0x2b] = SHAPE_CIRCLE | SHAPE_HOLLOW,  [0x2f] = SHAPE_CIRCLE,                       // ○ ●
        [0x46] = SHAPE_CIRCLE | SHAPE_SMALL | SHAPE_HOLLOW,                               // ◦
};

//      The outline of each polygon, wound the same way, and its incentre and
//      inradius (for a hollow one's stroke): the triangles have an incentre a
//      0.382 of a half size from their middle, toward the base.
static const f32 font_drawn_vertex[8][4][2] = {
        {{0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}},
        {{-1, -0.5f}, {1, -0.5f}, {1, 0.5f}, {-1, 0.5f}},
        {{0, -1}, {1, 1}, {-1, 1}, {0, 0}},
        {{0, 1}, {-1, -1}, {1, -1}, {0, 0}},
        {{1, 0}, {-1, 1}, {-1, -1}, {0, 0}},
        {{-1, 0}, {1, -1}, {1, 1}, {0, 0}},
        {{0, -1}, {1, 0}, {0, 1}, {-1, 0}},
};
static const f32 font_drawn_centre[8][3] = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 0, 0.5f},
        {0, 0.381966f, 0.618034f},
        {0, -0.381966f, 0.618034f},
        {-0.381966f, 0, 0.618034f},
        {0.381966f, 0, 0.618034f},
        {0, 0, 0.707107f},
};

//      A straight stroke of width t from a to b: four points, wound the same
//      way whatever the direction, so strokes that cross add.
static fn font_stroke(font_engine address_to e, f32 ax, f32 ay, f32 bx, f32 by, f32 t, b32 height)
{
        f32 dx = bx - ax, dy = by - ay, length = __builtin_sqrtf(dx * dx + dy * dy);
        f32 nx = -dy / length * t / 2, ny = dx / length * t / 2;

        font_put(e, ax - nx, ay - ny, height, true);
        font_put(e, bx - nx, by - ny, height, true);
        font_put(e, bx + nx, by + ny, height, true);
        font_put(e, ax + nx, ay + ny, height, true);
        font_close(e);
}

static p8 font_drawn_shape(p32 code)
{
        if (code >= 0x25a0 && code <= 0x25ff)
                return font_drawn_kind[code - 0x25a0];
        return code == 0x21b5 ? SHAPE_ARROW : code == 0x2717 ? SHAPE_CROSS : 0;
}

static bool font_draw_shape(font_engine address_to e, p32 code, b32 width, b32 height, b32 light,
                            p8 address_to out, positive stride)
{
        p32 kind = font_drawn_shape(code);
        f32 cx = (f32)width / 2, cy = (f32)height / 2, t = (f32)light;
        f32 half = (f32)width * 0.36f < (f32)height * 0.2f ? (f32)width * 0.36f : (f32)height * 0.2f;

        if (!kind)
                return false;
        if (kind == SHAPE_ARROW)
        {
                //      A stem down from the top right, a bar to the left and a head.
                f32 w = (f32)width, h = (f32)height, right = w * 0.72f, left = w * 0.22f, mid = h * 0.6f;
                f32 wing = w * 0.2f;

                font_stroke(e, right, h * 0.28f, right, mid + t / 2, t, height);
                font_stroke(e, right, mid, left, mid, t, height);
                font_stroke(e, left, mid, left + wing, mid - wing, t, height);
                font_stroke(e, left, mid, left + wing, mid + wing, t, height);
        }
        else if (kind == SHAPE_CROSS)
        {
                font_stroke(e, cx - half, cy - half, cx + half, cy + half, t, height);
                font_stroke(e, cx + half, cy - half, cx - half, cy + half, t, height);
        }
        else
        {
                p32 shape = kind & 15;
                f32 size = kind & SHAPE_SMALL ? half / 2 : half;
                bool hollow = kind & SHAPE_HOLLOW;

                if (shape == SHAPE_CIRCLE)
                {
                        //      In a box of whole pixels, so its edge is the same grey all round.
                        f32 left = __builtin_roundf(cx - size), right = __builtin_roundf(cx + size);
                        f32 top = __builtin_roundf(cy - size);

                        size = (right - left) / 2;
                        cx = left + size;
                        cy = top + size;

                        //      Quarters, the first from each direction to the next; a hollow one's
                        //      inner ring runs back the other way, the quarters in reverse order.
                        static const f32 quarter[4][4] = {{1, 0, 0, 1}, {0, 1, -1, 0}, {-1, 0, 0, -1}, {0, -1, 1, 0}};

                        for (p32 q = 0; q < 4; q++)
                                font_quarter(e, cx, cy, size, quarter[q][0], quarter[q][1], quarter[q][2], quarter[q][3],
                                             height);
                        font_close(e);
                        if (hollow)
                        {
                                //      The stroke is at most half the inside, so a small one stays hollow.
                                f32 inner = size - (t < size / 2 ? t : size / 2);

                                for (p32 q = 4; q-- > 0;)
                                        font_quarter(e, cx, cy, inner, quarter[q][2], quarter[q][3], quarter[q][0],
                                                     quarter[q][1], height);
                                font_close(e);
                        }
                }
                else
                {
                        p32 n = shape == SHAPE_UP || shape == SHAPE_DOWN || shape == SHAPE_RIGHT || shape == SHAPE_LEFT ? 3 : 4;
                        const f32 (*v)[2] = font_drawn_vertex[shape];
                        const f32 *c = font_drawn_centre[shape];

                        //      Every corner on a whole pixel: a square's sides and a triangle's base are
                        //      edges of black, where at the cell's centre they were grey on every side.
                        f32 corner[4][2];

                        for (p32 k = 0; k < n; k++)
                        {
                                corner[k][0] = __builtin_roundf(cx + size * v[k][0]);
                                corner[k][1] = __builtin_roundf(cy + size * v[k][1]);
                                font_put(e, corner[k][0], corner[k][1], height, true);
                        }
                        font_close(e);
                        if (hollow)
                        {
                                //      Each corner moved toward the incentre until the edges are t inside, from
                                //      the corners as drawn, so the hole is inside the shape at any size.
                                f32 stroke = t < size * c[2] / 2 ? t : size * c[2] / 2;
                                f32 scale = 1 - stroke / (size * c[2]);
                                f32 mx = cx + size * c[0], my = cy + size * c[1];

                                for (p32 k = n; k-- > 0;)
                                        font_put(e, mx + (corner[k][0] - mx) * scale, my + (corner[k][1] - my) * scale,
                                                 height, true);
                                font_close(e);
                        }
                }
        }
        font_raster(e, width, height, out, stride);
        return true;
}

static bool font_drawn(font_engine address_to e, p32 code, b32 width, b32 height,
                       p8 address_to out, positive stride)
{
        b32 light = (height + 8) / 16;
        light = light < 1 ? 1 : light;
        e->point_count = e->contour_count = 0;
        e->clip = true;

        if (font_drawn_shape(code))
                return font_draw_shape(e, code, width, height, light, out, stride);
        //      A soft hyphen is nothing: the cell is left as it was cleared, which is what
        //      the faces that have one draw (Plex's is empty), and what a face without one must too.
        if (code == 0xad)
                return true;
        /*
                Braille, which btop draws its graphs in: each dot in a box of
                whole pixels at its place on the 2 by 4 grid, a square of ink
                while it is three pixels or less across (where a round dot was
                a blob of grey at any phase) and a circle in the box above
                that.
        */
        if (code >= 0x2800 && code <= 0x28ff)
        {
                f32 radius = (width < height / 2 ? width : height / 2) * 0.15f;
                b32 across = (b32)__builtin_roundf(2 * radius);

                across = across < 1 ? 1 : across;
                radius = (f32)across / 2;
                for (p32 dot = 0; dot < 8; dot++)
                {
                        if (!(code & 1u << dot))
                                continue;

                        p32 column = dot >= 3 && dot != 6, row = dot >= 6 ? 3 : dot % 3;
                        b32 left = (b32)__builtin_roundf(width * (1 + 2 * column) / 4.0f - radius);
                        b32 top = (b32)__builtin_roundf(height * (1 + 2 * row) / 8.0f - radius);
                        f32 cx = (f32)left + radius, cy = (f32)top + radius;

                        if (across <= 3)
                        {
                                b32 from = left < 0 ? 0 : left, to = left + across < width ? left + across : width;

                                for (b32 y = top < 0 ? 0 : top; y < top + across && y < height; y++)
                                        if (from < to)
                                                memory_fill(out + (positive)y * stride + (positive)from, 255,
                                                            (positive)(to - from));
                                continue;
                        }

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
                //      Squares were written straight into the cleared cell; only circles have outlines.
                if (across > 3)
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
        memory_zero(e->index, sizeof(e->index));
        e->slot_count = e->shelf_count = e->shelf_bottom = 0;
        e->scratched = false;
        e->generation++;
}

//      A slot made, at the place in the index the lookup ended at.
static fn font_enter(font_engine address_to e, p32 home, font_slot slot)
{
        e->slots[e->slot_count] = slot;
        e->index[home] = (p16)++e->slot_count;
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
                  (p64)(phase & 7) << 6 | (p64)(style & 7) << 3 | (p64)(style >> 3 & 3);
        font_bitmap box = {0};

        if (face && size >= FONT_SIZE_LIMIT)
                return box;
        if (e->scratched || e->slot_count >= FONT_SLOTS * 3 / 4)
                font_reset(e);

        //      The slots are a list in the order they were made, and an index
        //      of 16 bits over them is what is hashed: a few glyphs touch a
        //      few pages of the list and two of the index, not the 128 KB a
        //      table of slots spread by their hashes would.
        p32 home = (p32)(((key ^ cell) * 0x9e3779b97f4a7c15ull) >> 52) & (FONT_SLOTS - 1);
        while (e->index[home])
        {
                const font_slot address_to slot = &e->slots[e->index[home] - 1];

                if (slot->key == key && slot->cell == cell)
                        return (font_bitmap){slot->width ? e->atlas + (positive)slot->y * e->atlas_width + slot->x : null,
                                             slot->x == 0xffff ? 1 : e->atlas_width, slot->left, slot->top,
                                             slot->width, slot->height};
                home = (home + 1) & (FONT_SLOTS - 1);
        }

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
                                font_enter(e, home, (font_slot){.key = key, .cell = cell, .x = 0xffff});
                                return (font_bitmap){.stride = 1};
                        }
                }
        }
        else if ((size >> 16) < 0x800 && (size & 0xffff) < 0x800)
                box = (font_bitmap){.width = (b32)(size >> 16), .height = (b32)(size & 0xffff)};
        if (box.width <= 0 || box.height <= 0)
        {
                font_enter(e, home, (font_slot){.key = key, .cell = cell});
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
                home = (p32)(((key ^ cell) * 0x9e3779b97f4a7c15ull) >> 52) & (FONT_SLOTS - 1);
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

        font_enter(e, home, (font_slot){key, cell, x, y, (p16)box.width, (p16)box.height, (b16)box.left, (b16)box.top});
        return box;
}

/*
        Blending. Coverage is light: a pixel half covered by black text on
        white should give half the light, which is sRGB 188 and not 128. So
        both colours go into linear light, mix there by coverage, and come
        back to the nearest byte. But the eye reads lightness, not light, and
        small text is mostly edge pixels: measured as ink -- lightness away
        from the ground per em squared, against the same run at 48 px -- dark
        text mixed in linear light reads 6 to 24 percent light at 8 to 16 px
        (Inter, Plex Mono, Hack), and white text on black 18 to 61 percent
        heavy. So dark text first passes its coverage through a contrast
        curve, a(k + 1) / (ak + 1), k = 2 at full strength (within 10 percent
        at every one of those sizes; k = 1 left it 16 percent light), falling
        to nothing as the text's lightness rises to three quarters; and light
        text moves, as its lightness rises from a half to three quarters, to
        halfway between linear light and the plain mix of the sRGB bytes (all
        the way, white on black read within 16 percent but its diagonals were
        smudges; a contrast curve bent the other way, k = -0.45, took the
        worst of it only from 1.41 to 1.23).

        The two tables are the program's, made once: 256 bytes into 16-bit
        light, and the way back to the byte nearest in sRGB, that is the byte
        whose decision threshold, the light of the half step between two
        bytes, the light has not reached. The way back is a word for every
        sixteen lights (8 KB, where a byte for each light was 64 KB): the byte
        at the first of the sixteen in the low half, and in the high half
        where in the sixteen the next byte begins, 16 when none does. The
        sRGB curve's steepest part is its linear foot, a byte in 19.9 lights,
        so no sixteen lights hold two steps, and every one of the 65536
        answers is the table's answer before it.
*/
static p16 font_to_linear[256];
static p16 font_to_srgb[4096];

static inline p32 font_srgb(p32 light)
{
        p32 word = font_to_srgb[light >> 4];

        return (word & 255) + ((light & 15) >= (word >> 8));
}

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

        //      The lights where each byte begins: the first whole light the
        //      half step below it has been reached by.
        p32 s = 0, begins[257];

        for (p32 b = 1; b < 256; b++)
        {
                f64 half = font_srgb_to_linear((b - 0.5) / 255) * 65535;

                begins[b] = (p32)half + ((f64)(p32)half < half);
        }
        begins[0] = 0;
        begins[256] = 1 << 20;
        for (p32 k = 0; k < 4096; k++)
        {
                while (s < 255 && begins[s + 1] <= k * 16)
                        s++;
                font_to_srgb[k] = (p16)(s | (begins[s + 1] <= k * 16 + 15 ? begins[s + 1] - k * 16 : 16) << 8);
        }
        for (s = 0; s < 256; s++)
                font_to_linear[s] = (p16)(font_srgb_to_linear(s / 255.0) * 65535 + 0.5);
        __atomic_store_n(&made, 2, __ATOMIC_RELEASE);
}

fn font_start(font_engine address_to e, p8 address_to atlas, p32 width, p32 height)
{
        //      An engine that has never run is zero already -- a static, or a
        //      new mapping -- and clearing it would make every page of its
        //      684 KB resident, 500 KB more than a terminal ever touches.
        if (e->started)
                memory_zero(e, sizeof(*e));
        e->started = true;
        e->hint_until = 64;
        e->darken = 12;
        e->darken_until = 32;
        e->contrast = 128;
        e->snap_widest = 1024;
        e->linear = true;
        e->atlas = atlas;
        e->atlas_width = width > 0xffff ? 0xffff : width;
        e->atlas_height = height > 0xffff ? 0xffff : height;
        font_tables();
}

//      The coverage curve and the share of the plain mix for text of one
//      colour on another, at a strength from font_strength. Which way the
//      text is, is the ink's lightness against the ground's. Darker ink
//      takes the curve by the square of how much darker, all of it at nine
//      tenths (black on white) and about half at two thirds, where blue
//      2050c0 and 606060 on white read 1.09 to 1.18 times their ink at 48 px
//      at 8 to 12 px with all of it, and 0.94 to 1.10 with half. Lighter ink
//      takes the plain mix, all of it at half lighter. Keyed on the ink's
//      lightness alone, green 4ec94e on black took two thirds of the curve
//      meant for dark text and read 1.23 to 1.41 at 8 to 11 px.
static fn font_ramp(font_engine address_to e, p32 colour, p32 background, p32 strength)
{
        font_ramp_set address_to set = e->ramp_now;

        if (set && set->colour == colour && set->ground == background && set->contrast == strength)
                return;
        for (p32 w = 0; w < FONT_RAMPS; w++)
        {
                set = &e->ramps[w];
                if (set->ready && set->colour == colour && set->ground == background && set->contrast == strength)
                {
                        e->ramp_now = set;
                        e->ramp = set->table;
                        e->plain = set->plain;
                        return;
                }
        }

        //      On a ground not known, text whose colour alternates made a new
        //      table of 256 floats for every glyph; the last FONT_RAMPS are kept.
        set = &e->ramps[e->ramp_turn];
        e->ramp_turn = (e->ramp_turn + 1) % FONT_RAMPS;

        f32 light = font_luma(colour) / 65280.0f;
        f32 ground = background & FONT_UNKNOWN ? (light < 0.5f ? 1 : 0) : font_luma(background) / 65280.0f;
        f32 dark = ground - light, bright = 2 * (light - ground);
        f32 share = dark <= 0 ? 0 : dark * dark / 0.81f;
        f32 k = strength / 64.0f * (share > 1 ? 1 : share);

        //      Light text takes at most half the plain mix and half linear
        //      light: all of the plain mix kept white on black within 16
        //      percent of its weight at 48 px, but a half-covered pixel of
        //      it is sRGB 128, a fifth of the light, and the diagonals of m,
        //      w, M, W and # at 8 to 14 px were grey smudges. Half and half
        //      reads a little heavier and draws them as strokes.
        set->plain = (p32)(32 * (bright < 0 ? 0 : bright > 1 ? 1 : bright) + 0.5f);
        for (p32 a = 0; a < 256; a++)
        {
                f32 x = a / 255.0f;

                set->table[a] = (p8)(255 * x * (k + 1) / (x * k + 1) + 0.5f);
        }
        set->colour = colour;
        set->ground = background;
        set->contrast = strength;
        set->ready = true;
        e->ramp_now = set;
        e->ramp = set->table;
        e->plain = set->plain;
}

//      One pixel: back and fore mixed by a of 255, channel by channel, in
//      linear light; light text moves, by e->plain of 64, to the mix of the
//      sRGB bytes themselves.
static p32 font_mix(const font_engine address_to e, p32 back, p32 fore, p32 a)
{
        p32 mixed = 0;

        //      No light leaves the ground, and all of it is the ink: the
        //      round trip through light returns every byte it is given.
        if (a == 0)
                return back & 0xffffff;
        if (a == 255)
                return fore & 0xffffff;

        //      How much of the plain mix, in 64ths, a channel takes: all of
        //      it without linear light, none of it for dark text, which then
        //      needs no plain mix, and all of it for the lightest, which
        //      needs no light.
        p32 weight = e->linear ? e->plain : 64;

        for (p32 shift = 0; shift < 24; shift += 8)
        {
                p32 b = back >> shift & 255, f = fore >> shift & 255, value = 0, plain = 0;

                if (weight < 64)
                        value = font_srgb((font_to_linear[b] * (255 - a) + font_to_linear[f] * a + 127) / 255);
                if (weight)
                {
                        plain = (b * (255 - a) + f * a + 127) / 255;
                        value = weight == 64 ? plain : (value * (64 - weight) + plain * weight + 32) >> 6;
                }
                mixed |= value << shift;
        }
        return mixed;
}

/*
        Text on a ground of one colour, which is nearly all text: a terminal
        paints each cell's background, a window its own. Every coverage level
        then has one answer, so the 256 pixels for the pair of colours are
        made once -- contrast curve, linear light and all -- and a glyph is a
        lookup a pixel: the pair's 256 tones, and after them the same tones
        as four planes of bytes, which is the table memory_translate_u32_rect
        takes. The engine keeps 64 pairs.
*/
static const p32 address_to font_tones(font_engine address_to e, p32 colour, p32 background, p32 strength)
{
        strength = strength > 255 ? 255 : strength;
        p64 key = (p64)1 << 63 | (p64)strength << 48 | (p64)e->linear << 56 |
                  (p64)(colour & 0xffffff) << 24 | (background & 0xffffff);
        //      Four ways to a set of pairs, and a set picked by the key's hash:
        //      a terminal's colours of a screen are a few dozen pairs, and one
        //      way to a set made every cell of two colours that hashed alike
        //      a fresh 256-pixel table (17 times the cost of a cell at 64 pairs).
        //      The ways of a set are taken round-robin, two bits a set.
        p32 set = (p32)((key * 0x9e3779b97f4a7c15ull) >> 60), turn = (e->pair_turns >> (2 * set)) & 3;
        font_pairing address_to pair = &e->pairs[set * FONT_WAYS];

        for (p32 w = 0; w < FONT_WAYS; w++)
                if (pair[w].key == key)
                        return pair[w].pixel;

        e->pair_turns = (e->pair_turns & ~(3u << 2 * set)) | (((turn + 1) & 3) << 2 * set);
        pair += turn;
        font_ramp(e, colour, background, strength);
        for (p32 c = 0; c < 256; c++)
        {
                pair->pixel[c] = font_mix(e, background, colour, e->ramp[c]);
                for (p32 k = 0; k < 4; k++)
                        pair->planes[k][c] = (p8)(pair->pixel[c] >> (8 * k));
        }
        pair->ground = background & 0xffffff;
        pair->key = key;
        return pair->pixel;
}

//      A glyph's coverage onto the target at x, y: through the pair's tones
//      where the pixel under it is still the background, by the full mix
//      where another glyph has already drawn there or the ground is unknown.
//      On a known ground the library lays the tones and stops at a pixel
//      that is not the ground; that one is mixed here and the rest resumed.
static fn font_blend(font_engine address_to e, const font_target address_to target,
                     b32 x, b32 y, font_bitmap glyph, p32 colour, p32 background, p32 strength)
{
        b32 x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
        b32 x1 = x + glyph.width > target->width ? target->width - x : glyph.width;
        b32 y1 = y + glyph.height > target->height ? target->height - y : glyph.height;
        bool known = !(background & FONT_UNKNOWN);

        if (x0 >= x1 || y0 >= y1)
                return;
        const p32 address_to pixel = known ? font_tones(e, colour, background, strength) : null;
        font_ramp(e, colour, background, strength > 255 ? 255 : strength);
        colour &= 0xffffff;

        positive width = (positive)(x1 - x0), height = (positive)(y1 - y0), stride = target->stride;
        p32 address_to into = target->pixels + (positive)(y + y0) * stride + (positive)(x + x0);
        const p8 address_to from = glyph.coverage + (positive)y0 * glyph.stride + (positive)x0;

        if (known)
        {
                positive at = memory_blend_u32_rect(into, stride, from, glyph.stride, width, height, pixel);
                positive r = at / width, c = at % width;

                while (r < height)
                {
                        p32 address_to here = into + r * stride + c;

                        *here = font_mix(e, *here, colour, e->ramp[from[r * glyph.stride + c]]);

                        //      The rest of row r, then every row after it.
                        if (++c < width &&
                            (at = memory_blend_u32_rect(here + 1, stride, from + r * glyph.stride + c, glyph.stride,
                                                        width - c, 1, pixel)) < width - c)
                        {
                                c += at;
                                continue;
                        }
                        if (++r >= height)
                                break;
                        at = memory_blend_u32_rect(into + r * stride, stride, from + r * glyph.stride, glyph.stride,
                                                   width, height - r, pixel);
                        r += at / width, c = at % width;
                }
                return;
        }
        for (positive r = 0; r < height; r++)
        {
                const p8 address_to row = from + r * glyph.stride;
                p32 address_to ground = into + r * stride;

                for (positive c = 0; c < width; c++)
                {
                        if (row[c])
                                ground[c] = font_mix(e, ground[c], colour, e->ramp[row[c]]);
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
        //      The cell of an ASCII or Latin-1 character, the common case, is one compare.
        if (code < 0x2190)
                return code == 0xad;
        return (code >= 0x2500 && code <= 0x259f) || (code >= 0x2800 && code <= 0x28ff) || font_drawn_shape(code);
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
static inline b32 font_eighths(b64 pen)
{
        return (b32)(((b64)pen * FONT_PHASES + 0x8000) >> 16);
}

positive font_layout(font_engine address_to e, const font_face address_to const address_to faces,
                     p32 count, const p8 address_to text, positive length, p32 size,
                     p32 flags, font_place address_to out, positive room)
{
        positive placed = 0, at = 0;
        b64 pen = 0;

        while (at < length && placed < room && count)
        {
                p32 code, face, glyph;

                at = font_decode(text, length, at, &code);
                font_find(e, faces, count, code, &face, &glyph);
                if (placed && face != FONT_DRAWN && out[placed - 1].face == face &&
                    !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[face], out[placed - 1].glyph, glyph, size);

                b64 advance = face == FONT_DRAWN ? (b64)font_metrics_at(faces[0], size).cell_width << 16
                                                 : font_advance(faces[face], glyph, size);
                out[placed++] = (font_place){glyph, face, pen, advance};
                pen += advance;
        }
        return placed;
}

b64 font_draw(font_engine address_to e, const font_face address_to const address_to faces,
              p32 count, const p8 address_to text, positive length, p32 size, p32 flags,
              const font_target address_to target, b32 x, b32 baseline, p32 colour,
              p32 background)
{
        positive at = 0;
        b64 pen = 0;
        font_place last = {0, FONT_DRAWN, 0, 0};

        flags = font_polarity(flags, colour, background);
        p32 style = font_style(e, size, flags), strength = font_strength(e, size, flags);

        //      Stems on whole pixels as a cell's are, though the pen is in
        //      eighths: the glyph is snapped where it lands, its two ends kept,
        //      so its spacing is the font's. Unsnapped, Inter's l at 10 to
        //      14 px fell across two grey columns at most phases.
        style |= style & 1 ? 4 : 0;

        while (at < length && count)
        {
                font_place place;

                at = font_decode(text, length, at, &place.glyph);
                font_find(e, faces, count, place.glyph, &place.face, &place.glyph);
                if (place.face != FONT_DRAWN && last.face == place.face && !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[place.face], last.glyph, place.glyph, size);

                b32 eighth = font_eighths((b64)x + pen);
                if (place.face == FONT_DRAWN)
                {
                        font_metrics cell = font_metrics_at(faces[0], size);
                        font_bitmap box = font_cached(e, null, place.glyph,
                                                      (p32)cell.cell_width << 16 | (p32)cell.cell_height, 0, 0, 0);
                        place.advance = cell.cell_width << 16;
                        font_blend(e, target, eighth >> 3, baseline - cell.baseline, box, colour, background,
                                   strength);
                }
                else
                {
                        font_bitmap box = font_cached(e, faces[place.face], place.glyph, size,
                                                      (p32)eighth & 7, style, 0);
                        place.advance = font_advance(faces[place.face], place.glyph, size);
                        font_blend(e, target, (eighth >> 3) + box.left, baseline - box.top, box, colour,
                                   background, strength);
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
        if (left < right && top < bottom)
                memory_fill_u32_rect(target->pixels + (positive)top * target->stride + left, target->stride,
                                     (positive)(right - left), (positive)(bottom - top), background & 0xffffff);
}

//      What a cell is, less its code: one 64-bit key a run shares, and the
//      grid it lies on.
static inline INLINE p64 font_cell_base(const font_face address_to const address_to faces, p32 count, p32 size,
                                        p32 flags)
{
        return (p64)1 << 63 | (p64)(count ? faces[0]->id & 0xffff : 0) << 47 | (p64)(size & 0xfffff) << 6 |
               (p64)(flags & 31) << 1;
}

//      The faces a cell is found in, every id of them, 16 bits an id: the
//      cell's glyph is the first of them that has its code, so two chains
//      that agree on the first face and their length can draw different
//      glyphs. Zero when the chain is longer than four, or a cell wider
//      than 16 bits, which is found every time and kept nowhere.
static inline p64 font_chain(const font_face address_to const address_to faces, p32 count, p32 cells)
{
        p64 chain = 0;

        if (count < 1 || count > 4 || cells > 0xffff)
                return 0;
        for (p32 i = 0; i < count; i++)
                chain |= (p64)(faces[i]->id & 0xffff) << (16 * i);
        return chain;
}

//      The index of a cell identity, plus one, for the recent cells to name:
//      the last one found first, then the table. Zero is never an index.
static p8 font_identify(font_engine address_to e, p64 chain, p64 grid, p32 spans)
{
        p32 last = e->ident_last;

        if (last < FONT_IDENTS && e->idents[last].used && e->idents[last].chain == chain &&
            e->idents[last].grid == grid && e->idents[last].spans == spans)
                return (p8)(last + 1);
        for (p32 i = 0; i < FONT_IDENTS; i++)
                if (e->idents[i].used && e->idents[i].chain == chain && e->idents[i].grid == grid &&
                    e->idents[i].spans == spans)
                        return (p8)(e->ident_last = i) + 1;

        //      A new identity: the cells drawn against the slot it takes are
        //      stale, which the generation moving on makes them.
        p32 i = e->ident_turn;

        e->ident_turn = (i + 1) % FONT_IDENTS;
        e->idents[i] = (font_ident){chain, grid, spans, true};
        e->ident_last = i;
        e->generation++;
        return (p8)(i + 1);
}

//      The grid it lies on, and how many cells and faces.
static inline INLINE p64 font_cell_shape(const font_metrics address_to grid, p32 count, p32 cells)
{
        return (p64)(p16)grid->cell_width << 32 | (p64)(p16)grid->cell_height << 16 | (p16)grid->baseline |
               (p64)(cells > 1) << 48 | (p64)((count - 1) & 3) << 49;
}

//      A cell not drawn lately: its glyph found, drawn into the atlas, and
//      kept in the recent cells. Out of line, so that the test before it
//      keeps a run's arguments in registers (spilled around this, they were
//      read back wider than they were written, a store the machine could not
//      forward and most of a cell's cost).
static COLD __attribute__((noinline)) font_bitmap
font_cell_find(font_engine address_to e, const font_face address_to const address_to faces, p32 count, p32 code,
               p32 cells, const font_metrics address_to grid, p32 size, p32 flags, p64 key, p32 ident,
               font_recent address_to recent, b32 address_to dx, b32 address_to dy)
{
        b32 width = (b32)(cells ? cells : 1) * grid->cell_width, height = grid->cell_height;
        font_bitmap box = {0};
        p32 face, glyph;

        *dx = *dy = 0;
        if (font_is_drawn(code))
                box = font_cached(e, null, code, (p32)grid->cell_width << 16 | (p32)height, 0, 0, 0);
        else if (count && code != ' ')
        {
                font_find(e, faces, count, code, &face, &glyph);

                b32 eighth = font_eighths(((width << 16) - font_advance(faces[face], glyph, size)) / 2);
                p32 style = font_style(e, size, flags);

                //      A cell's pen is a whole pixel, so its stems can be too.
                style |= style & 1 ? 4 : 0;
                box = font_cached(e, faces[face], glyph, size, (p32)eighth & 7, style,
                                  (p64)(p16)width << 48 | (p64)(p16)height << 32 |
                                      (p64)(p16)grid->baseline << 16 | (p16)(eighth >> 3));
                if (box.stride == 1)
                {
                        box = font_cached(e, faces[face], glyph, size, (p32)eighth & 7, style, 0);
                        *dx = (eighth >> 3) + box.left;
                        *dy = grid->baseline - box.top;
                }
        }

        //      The first and last rows of the cell with any ink, which a run
        //      draws through the strip, the rows outside every cell's being
        //      ground (a blank's are none: top past bottom).
        b32 top = height, bottom = -1;
        for (b32 r = 0; box.coverage && r < box.height; r++)
        {
                b32 row = *dy + r;
                const p8 address_to bits = box.coverage + (positive)r * box.stride;
                p8 any = 0;

                if (row < 0 || row >= height)
                        continue;
                for (b32 c = 0; c < box.width && !any; c++)
                        any = bits[c];
                if (any)
                {
                        top = row < top ? row : top;
                        bottom = row;
                }
        }

        //      A bitmap drawn into the atlas's scratch is gone with the next
        //      glyph, so it is not kept.
        if (!e->scratched)
                *recent = (font_recent){.key = key, .generation = e->generation, .ident = (p8)ident,
                                        .dx = (b16)*dx, .dy = (b16)*dy, .top = (b16)top, .bottom = (b16)bottom,
                                        .box = box};
        else
                recent->key = 0;
        return box;
}

/*
        The bitmap a cell draws, and where it sits from the cell's corner.
        The cells of a screen are a few dozen codes in one size and style, so
        a cell drawn lately is found again here by one hash of what it is,
        without finding its glyph, its advance (a divide) or its slot again.
        A reset of the atlas moves the generation on, and every entry with it.
*/
static inline INLINE font_bitmap font_cell_box(font_engine address_to e, const font_face address_to const address_to faces,
                                               p32 count, p32 code, p32 cells, const font_metrics address_to grid,
                                               p32 size, p32 flags, p64 base, p32 ident, b32 address_to dx,
                                               b32 address_to dy, b16 address_to ink)
{
        p64 key = base | (p64)(code & 0x1fffff) << 26;
        font_recent address_to recent = &e->recent[(key * 0x9e3779b97f4a7c15ull) >> 56];

        if (!ident)
        {
                font_recent kept = {0};
                font_bitmap box = font_cell_find(e, faces, count, code, cells, grid, size, flags, key, 0, &kept, dx,
                                                 dy);
                ink[0] = kept.top;
                ink[1] = kept.bottom;
                return box;
        }
        if (recent->key == key && recent->ident == ident && recent->generation == e->generation)
        {
                *dx = recent->dx;
                *dy = recent->dy;
                ink[0] = recent->top;
                ink[1] = recent->bottom;
                return recent->box;
        }
        font_bitmap box = font_cell_find(e, faces, count, code, cells, grid, size, flags, key, ident, recent, dx, dy);
        ink[0] = recent->top;
        ink[1] = recent->bottom;
        return box;
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
        b32 dx, dy;
        b16 ink[2];

        flags = font_polarity(flags, colour, background);
        p64 chain = font_chain(faces, count, cells);
        p32 ident = chain ? font_identify(e, chain, font_cell_shape(grid, count, cells), cells << 3 | count) : 0;
        font_bitmap box = font_cell_box(e, faces, count, code, cells, grid, size, flags,
                                        font_cell_base(faces, count, size, flags), ident, &dx, &dy, ink);
        b32 gx = x + dx, gy = y + dy;

        if (background & FONT_UNKNOWN)
        {
                font_blend(e, target, gx, gy, box, colour, background, font_strength(e, size, flags));
                return;
        }

        //      The cell's rectangle on the target, and the glyph's inside it.
        b32 left = x < 0 ? 0 : x, right = x + width > target->width ? target->width : x + width;
        b32 top = y < 0 ? 0 : y, bottom = y + height > target->height ? target->height : y + height;
        b32 from = gx > left ? gx : left, to = gx + box.width < right ? gx + box.width : right;
        b32 above = gy > top ? gy : top, below = gy + box.height < bottom ? gy + box.height : bottom;

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
                                          box.stride, (positive)(to - from), (positive)(below - above),
                                          font_tones(e, colour, background, font_strength(e, size, flags)));
}

//      Rows from to from + rows of one cell's coverage, width bytes each,
//      into the strip at into, stride bytes a row. A cell's own bitmap that
//      is the whole cell is a copy a row: one 16-byte word for cells up to
//      16 wide, its tail landing on the next cell's place, which that cell
//      writes after, or in the strip row's padding past the last cell. A
//      blank or a glyph that hangs out of its cell is the cell's zeros with
//      what of the glyph is inside.
typedef p8 font_v16 __attribute__((vector_size(16)));

static inline fn font_lay_cell(p8 address_to into, positive stride, const font_bitmap address_to box, b32 dx,
                               b32 dy, b32 width, b32 height, b32 from, b32 rows, const p8 address_to limit)
{
        if (box->width == width && box->height == height && !dx && !dy && box->coverage)
        {
                const p8 address_to source = box->coverage + (positive)from * box->stride;

                //      The word reads past the bitmap's row into the atlas,
                //      never past the atlas.
                if (width <= 16 && source + (positive)(rows - 1) * box->stride + 16 <= limit)
                        for (b32 r = 0; r < rows; r++, into += stride, source += box->stride)
                                memory_store_unaligned(font_v16, into, memory_load_unaligned(font_v16, source));
                else
                        for (b32 r = 0; r < rows; r++, into += stride, source += box->stride)
                                memory_copy(into, source, (positive)width);
                return;
        }
        for (b32 r = 0; r < rows; r++, into += stride)
        {
                b32 g = from + r - dy, left = dx < 0 ? 0 : dx;
                b32 right = dx + box->width < width ? dx + box->width : width;

                memory_zero(into, (positive)width);
                if (box->coverage && g >= 0 && g < box->height && left < right)
                        memory_copy(into + left, box->coverage + (positive)g * box->stride + (left - dx),
                                    (positive)(right - left));
        }
}

/*
        A run of single cells on one pair of colours, left to right from x:
        what a terminal's row mostly is. One cell at a time is a rectangle of
        a few pixels a row through the library, a short masked store a row,
        and that store, not the glyph, was the cell's cost (11 cycles a row of
        a cell at 16 px). So the run's cells are found first, their coverage
        rows laid side by side in the strip, and the strip made pixels in one
        call: rows as wide as the run, written with whole vector stores.

        Finding a cell can draw a glyph into the atlas, and drawing one can
        empty the atlas when it is full, which would leave the cells found
        before it pointing at another glyph's place. So when the generation
        moves while the run is found it is found again, and a run whose
        glyphs do not fit the atlas at once is drawn a cell at a time.
*/
fn font_cells(font_engine address_to e, const font_face address_to const address_to faces, p32 count,
              const p32 address_to codes, p32 n, const font_metrics address_to grid, p32 size, p32 flags,
              const font_target address_to target, b32 x, b32 y, p32 colour, p32 background)
{
        b32 width = grid->cell_width, height = grid->cell_height;
        positive across = (positive)n * (positive)(width > 0 ? width : 0);
        bool found = false;
        p32 asked = flags;

        flags = font_polarity(flags, colour, background);
        if (!(background & FONT_UNKNOWN) && n && n <= FONT_RUN && width > 0 && height > 0 && x >= 0 && y >= 0 &&
            x + (b64)across <= target->width && y + height <= target->height && across + 16 <= FONT_STRIP)
        {
                p64 chain = font_chain(faces, count, 1);
                p32 ident = chain ? font_identify(e, chain, font_cell_shape(grid, count, 1), 1 << 3 | count) : 0;

                for (p32 attempt = 0; attempt < 2 && !found; attempt++)
                {
                        p32 generation = e->generation;
                        p64 base = font_cell_base(faces, count, size, flags);

                        found = true;
                        for (p32 i = 0; i < n && found; i++)
                        {
                                b32 dx, dy;

                                b16 ink[2];

                                e->run[i] = font_cell_box(e, faces, count, codes[i], 1, grid, size, flags, base, ident,
                                                          &dx, &dy, ink);
                                e->run_dx[i] = (b16)dx;
                                e->run_dy[i] = (b16)dy;
                                e->run_top[i] = ink[0];
                                e->run_bottom[i] = ink[1];
                                found = !e->scratched;
                        }
                        found = found && e->generation == generation;
                }
        }
        if (!found)
        {
                for (p32 i = 0; i < n; i++)
                        font_cell(e, faces, count, codes[i], 1, grid, size, asked, target, x + (b32)i * width, y,
                                  colour, background);
                return;
        }

        //      The rows no cell has ink in -- above the capitals, below the
        //      descenders, all of a run of blanks -- are the ground, one fill;
        //      only the rows between go through the strip.
        b32 lo = height, hi = -1;
        for (p32 i = 0; i < n; i++)
        {
                lo = e->run_top[i] < lo ? e->run_top[i] : lo;
                hi = e->run_bottom[i] > hi ? e->run_bottom[i] : hi;
        }
        lo = lo < 0 ? 0 : lo;
        hi = hi >= height ? height - 1 : hi;
        if (lo > hi)
                lo = height, hi = height - 1;
        if (lo > 0)
                memory_fill_u32_rect(target->pixels + (positive)y * target->stride + (positive)x, target->stride, across,
                                     (positive)lo, background & 0xffffff);
        if (hi + 1 < height)
                memory_fill_u32_rect(target->pixels + (positive)(y + hi + 1) * target->stride + (positive)x,
                                     target->stride, across, (positive)(height - hi - 1), background & 0xffffff);

        //      Strip rows are 16 bytes longer than the run, for the last
        //      cell's word.
        positive stride = across + 16;
        const p8 address_to limit = e->atlas + (positive)e->atlas_width * e->atlas_height;
        b32 band = (b32)(FONT_STRIP / stride);
        band = band > hi - lo + 1 ? hi - lo + 1 : band;
        band = band < 1 ? 1 : band;
        for (b32 from = lo; from <= hi; from += band)
        {
                b32 rows = from + band > hi + 1 ? hi + 1 - from : band;

                for (p32 i = 0; i < n; i++)
                        font_lay_cell(e->strip + (positive)i * (positive)width, stride, &e->run[i], e->run_dx[i],
                                      e->run_dy[i], width, height, from, rows, limit);
                memory_translate_u32_rect(target->pixels + (positive)(y + from) * target->stride + (positive)x,
                                          target->stride, e->strip, stride, across, (positive)rows,
                                          font_tones(e, colour, background, font_strength(e, size, flags)));
        }
}

#endif // MOONWATER_CANVAS_FONT
