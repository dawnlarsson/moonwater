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
            bidi        UAX #9 in full on text in logical order (embeddings,
                        overrides, isolates, brackets, mirroring), checked
                        against all of BidiTest.txt and BidiCharacterTest.txt
            shaping     GSUB lookups 1-8 and GPOS 1-9 as HarfBuzz applies
                        them, for Arabic (joining forms, ligatures, marks
                        stacked by class, presentation forms for a face with
                        no layout tables) and for the default features
                        (Hebrew points): glyph for glyph what hb-shape gives
            turning     a line a quarter turn either way, for a band on the
                        side of a screen, each glyph's coverage turned byte
                        for byte as it is laid

        Refused at load: CFF and CFF2 outlines, variable fonts (fvar), and
        colour fonts with no outlines (CBDT, sbix). Not done: bytecode hinting
        (outlines are drawn unhinted, then fitted as above), COLR layers (the
        monochrome base glyphs are drawn), shaping of scripts other than
        Arabic and Hebrew (Latin, Greek, Cyrillic and CJK are a glyph a code
        point as they always were; Syriac, N'Ko and the Indic scripts are not
        joined or reordered), and right to left text in the terminal's cells.

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

        b64 font_draw_turned(font_engine *engine, const font_face *const *faces,
                             p32 count, const p8 *text, positive length,
                             p32 size, p32 flags, p32 turn,
                             const font_target *target, b32 x, b32 baseline,
                             p32 colour, p32 background)
                font_draw onto a target turned a quarter, for a band on the
                side of a screen. The run is laid out as font_draw lays it
                on a target target->height wide and target->width high, x
                and baseline as there, and that picture is turned onto the
                target: with FONT_TOPS_RIGHT a quarter clockwise, the run
                reading down with its glyphs' tops to the right, and with
                FONT_TOPS_LEFT a quarter the other way, reading up with the
                tops to the left. Each byte of coverage is the upright
                run's, at its turned place, from the same glyph cache.

        b64 font_extent(font_engine *engine, const font_face *const *faces,
                        p32 count, const p8 *text, positive length, p32 size,
                        p32 flags)
                The advance font_draw answers for the run, laid out and not
                drawn: its length along its line, upright or turned.

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

        void font_fallback(font_engine *engine, const font_face *regular,
                           const font_face *bold)
                Adds a face behind the caller's chain (up to four, in the order
                added): a code point no face of the chain has is looked for in
                them. bold may be null; asked for bold (FONT_BOLD) the slot
                is the bold, and the regular emboldened by the engine when
                there is none. A place's face is then count + the slot, which
                font_face_at turns back into the face for the flags the line
                was laid out with.

        positive font_segments(font_engine *engine, const font_face *const
                               *faces, p32 count, const p8 *text, positive length,
                               p32 size, p32 flags, const p32 *cuts, positive n,
                               b32 *left, b32 *right, p8 *level)
                Where n segments of a line (cuts[0..n] ascending byte offsets,
                0 and length at the ends: the caller's grapheme clusters) sit:
                left[k] and right[k] in 16.16 pixels from the pen font_draw
                starts at, level[k] the bidi level. A glyph for several
                segments is shared among them in equal parts, the first in
                reading order first; marks go with their base. Answers n, 0
                for more than 4096 segments or 8192 bytes.

        p32 font_base_level(const p8 *text, positive length, p32 flags)
        p32 font_is_simple(const p8 *text, positive length)
                The paragraph level of the first paragraph under the flags (0
                or 1), and whether the text has nothing right to left, no
                Arabic number and no directional control: the text every
                function above lays out a glyph a code point from the left as
                it always did, unless FONT_RTL is asked.

        flags: FONT_UNHINTED draws the outline as the font gives it,
        FONT_DARKEN asks for stem darkening, which font_draw and font_cell
        give dark text on a lighter ground unasked and FONT_UNDARKENED takes
        away, FONT_UNKERNED leaves pairs alone, FONT_OBLIQUE slants the face
        12 degrees for an italic it does not have, FONT_EMBOLDEN thickens it
        by a 24th of the size for a bold it does not have. FONT_RTL makes the
        paragraph direction right to left, FONT_AUTO takes it from the first
        strong character (RTL, if given, serving where there is none), and
        FONT_BOLD asks for the bold of the fallback faces. Text is in logical
        order and every call lays the whole line, reordered: the glyphs
        come out, drawn and measured, in the order they read left to right
        from the pen. Each font_place also says the lift of the glyph (y_offset,
        16.16, up), the byte its first code point is at and its bidi level.

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
//      The line's paragraph direction is right to left; the first strong character decides it (the direction above
//      then serving where there is none); and the fallback faces' bold is asked for.
#define FONT_RTL 64
#define FONT_AUTO 128
#define FONT_BOLD 256

//      Which way font_draw_turned turns a run: the side its glyphs' tops face.
#define FONT_TOPS_RIGHT 1
#define FONT_TOPS_LEFT 2

#define FONT_DRAWN 0xffff
#define FONT_UNKNOWN 0xff000000u
#define FONT_PHASES 8
#define FONT_EXACT 14
#define FONT_ONE 1024
#define FONT_SHIFT 10
//      Grid steps, possibly below zero, back to a position: the shift is on the unsigned word and the result is read as
//      two's complement, which is what the position means and is defined where a left shift of a negative is not.
#define FONT_STEPS_UP(steps) ((b64)((p64)(steps) << FONT_SHIFT))
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
//      The kerning memo's sets of FONT_WAYS: a frame's pairs are a few hundred,
//      and a direct-mapped table lost some of them to collisions every frame.
#define FONT_KERN_SETS 512
//      The lookup indices the kern feature's walk at load reads at most, and the kern table's subtables a pair walks.
#define FONT_KERN_READS 4096
//      The cache's direct-mapped memo of the slots it found last: 256 ways of 24 bytes.
#define FONT_CACHED 256
#define FONT_RECENT 256
#define FONT_STRIP (1 << 17)
#define FONT_RUN 1024
#define FONT_GROUP 6144
#define FONT_NEAR 512
#define FONT_EVENTS 3840

/*      What a line that needs more than a glyph a code point may cost: a paragraph of at most FONT_PARA code points
        (a longer one is laid as two), shaped FONT_UNIT of them at a time into at most FONT_GLYPHS glyphs. The lookups
        a shape plan runs, the context a rule may match, how deep a rule may call another lookup, the faces behind the
        caller's chain, and the work a face's lookups may do for a glyph before they are given up on. */
#define FONT_PARA 4096
#define FONT_UNIT 1024
#define FONT_GLYPHS 4096
#define FONT_STEPS 64
#define FONT_MATCH 64
#define FONT_NESTING 6
#define FONT_FALLBACKS 4
#define FONT_PLANS 16
#define FONT_OPS 2048
#define FONT_SEGMENT_BYTES 8192
#define FONT_SEGMENTS 4096
#define FONT_STACK 125

#define FONT_TAG(a, b, c, d) ((p32)(a) << 24 | (p32)(b) << 16 | (p32)(c) << 8 | (p32)(d))

typedef struct
{
        const p8 address_to data;
        positive size;
        positive glyf, glyf_size, loca, hmtx, cmap, lookup_list, kern, gsub, gpos, gdef;
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
        p32 glyph;
        p16 face, level;    // the face's index (FONT_DRAWN for a cell); the bidi level it was laid at, odd reading right to left
        b64 x, advance;     // 16.16: a run of a few thousand glyphs is past 32 bits
        b32 y_offset;       // 16.16, up: what a mark or a cursive join lifts the glyph by
        p32 cluster;        // the byte of the text the glyph's first code point is at
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

//      A key and cell font_cached last found, the atlas generation it was
//      found in, and the slot's 1-based index in the list.
typedef struct
{
        p64 key, cell;
        p32 generation;
        p16 slot;
} font_cache_way;

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

_Static_assert(offset_of(font_pairing, ground) == offset_of(font_pairing, pixel) + 2048,
               "the ground follows the planes, where memory_blend_u32_rect reads it");

typedef struct
{
        p64 key;
        b32 value;
} font_memo;

//      A kerning pair's units in a face, memo'd in a set of FONT_WAYS ways:
//      the face's id (never 0, at most 0xffff), the two glyphs and the value.
typedef struct
{
        p16 face, left, right;
        b32 value;
} font_kern_way;

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
        p8 stemmed, zoned;
        b32 standard, zones[FONT_ZONES];
} font_measure;

//      The coverage curve for one pair of colours and a strength, as
//      font_ramp makes it: the table of 256 coverages, and the plain share.
#define FONT_RAMPS 8

typedef struct
{
        p32 colour, ground, contrast, plain;
        p8 ready;
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
        p8 used;
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

/*      A glyph on its way through the layout tables: its id, the code point (index in the unit) it came from, the
        features that apply to it, and what the tables ask of it. props are the GDEF class (2 base, 4 ligature, 8
        mark) with 16 for a substituted glyph, 32 for a ligated one and 64 for one of a multiple substitution, and a
        mark's attachment class above; lig is a ligature's id and the component a mark stands in, or its count. */
typedef struct
{
        p32 glyph, cluster, mask, code;
        p16 props;
        p8 lig, unicode;
} font_item;

//      Where the tables put a glyph, in font units: its advance, its offset from the pen, and the glyph it hangs from.
typedef struct
{
        b32 advance, x, y;
        b16 chain;
        p8 attach;
} font_shift;

/*      What a face runs for one script: its GSUB and GPOS lookups in the order they apply (the features' stages,
        and in a stage by index), each with the glyphs it is for (the mask) and which joiners it may not skip. */
typedef struct
{
        p32 face;
        p8 kind, fallback, steps[2];
        p16 mask_of[8];
        p16 lookup[2][FONT_STEPS], mask[2][FONT_STEPS];
        p8 manual[2][FONT_STEPS], stage_of[2][FONT_STEPS];
        p64 digest[2][FONT_STEPS][2];
} font_plan;

typedef struct
{
        const font_face address_to regular, address_to bold;
} font_fallback_slot;

//      One paragraph in hand: the text as code points, then the bidi algorithm's, then its units, then a unit shaped.
typedef struct
{
        p32 code[FONT_PARA], at[FONT_PARA + 1], glyph[FONT_PARA];
        p8 orig[FONT_PARA], type[FONT_PARA], level[FONT_PARA], base[FONT_PARA], temp[FONT_PARA];
        p16 match[FONT_PARA], run_of[FONT_PARA], flat[FONT_PARA], run_start[FONT_PARA + 1], seq[FONT_PARA],
            pair[FONT_PARA];
        p16 face[FONT_PARA], unit_start[FONT_PARA + 1], unit_order[FONT_PARA], unit_face[FONT_PARA];
        p8 unit_level[FONT_PARA], unit_shape[FONT_PARA];
        font_item item[2][FONT_GLYPHS];
        font_shift shift[FONT_GLYPHS];
        p8 action[FONT_GLYPHS];
        b64 span_left[FONT_PARA], span_right[FONT_PARA];
} font_work;

typedef struct
{
        p32 hint_until;         // largest size in pixels that is fitted (64, a terminal's largest)
        p32 darken;             // darkening at small sizes, 1/64 pixel (12)
        p32 darken_until;       // size in pixels where it has faded out (32)
        p32 contrast;           // dark text's contrast boost, 1/64 (128)
        p32 snap_widest;        // widest standard stem snapped in cells, 1/64 pixel (1024)
        p8 linear;            // blend in linear light (true)

        //      What a glyph being drawn touches first, together, then the
        //      arrays it touches the first entries of, each of one kind so
        //      that a small glyph is a few pages of them and not a page of
        //      each; the caches, which a program fills as it goes, last.
        p8 address_to atlas;
        p32 atlas_width, atlas_height, shelf_count, shelf_bottom, slot_count;
        p8 started, scratched, clip, gather, overflow, track, mark, direct;
        p32 pair_turns, generation;
        p32 point_count, contour_count, components;
        b32 left_side;
        p64 still;              // contours the snap leaves alone (the dots, font_dots places them)
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
        font_cache_way cached[FONT_CACHED];
        font_pairing pairs[FONT_PAIRS];
        font_memo glyph_memo[FONT_MEMO];
        font_kern_way kern_memo[FONT_KERN_SETS][FONT_WAYS];
        font_measure measures[FONT_MEASURES];
        font_recent recent[FONT_RECENT];
        font_ident idents[FONT_IDENTS];
        p32 ident_turn, ident_last;

        //      A run of cells on its way to the target: each cell's bitmap,
        //      then their coverage rows side by side.
        font_bitmap run[FONT_RUN];
        b16 run_dx[FONT_RUN], run_dy[FONT_RUN], run_top[FONT_RUN], run_bottom[FONT_RUN];
        p8 strip[FONT_STRIP];

        //      Text that is more than a glyph a code point, last, for the pages of an engine that never meets any
        //      are never touched: the faces behind the caller's chain, the shape plans made for faces, and the
        //      paragraph being laid.
        font_fallback_slot fallback[FONT_FALLBACKS];
        p32 fallbacks, plan_turn;
        font_plan plan[FONT_PLANS];
        font_work work;
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

//      Held to 16384 px either way: a hostile advance of 65535 units in an em of 16 is 2^42 at 16383 px and wrapped
//      in 32 bits (a terminal's cell of a negative width), and a cell width that large shifted to 16.16 overflowed.
#define FONT_FIXED_MOST ((b64)1 << 30)

static inline b32 font_fixed(const font_face address_to face, b64 units, p32 size)
{
        b64 value = font_divide(units * size * 1024, face->units);

        return (b32)(value > FONT_FIXED_MOST ? FONT_FIXED_MOST : value < -FONT_FIXED_MOST ? -FONT_FIXED_MOST : value);
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
        p32 count = font_u16(face, scripts), reads = FONT_KERN_READS;

        //      Capped as the shaping plan caps its own walks (font_language, font_plan_table): a default language of
        //      65535 features, every one the kern feature, of 65535 lookups each was 4.3 billion reads in font_load,
        //      seconds to minutes for a file of 300 KB, and a desktop that never came up on it.
        count = count > 1024 ? 1024 : count;

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

        wanted = wanted > 4096 ? 4096 : wanted;
        for (p32 i = 0; i < wanted && reads; i++)
        {
                p32 feature = langsys ? font_u16(face, langsys + 6 + 2 * (positive)i) : i;
                positive record = features + 2 + 6 * (positive)feature;

                if (font_u32(face, record) != FONT_TAG('k', 'e', 'r', 'n'))
                        continue;

                positive table = features + font_u16(face, record + 4);
                p32 lookups = font_u16(face, table + 2);

                for (p32 k = 0; k < lookups && reads; k++, reads--)
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
            FONT_TAG('G', 'S', 'U', 'B'), FONT_TAG('G', 'D', 'E', 'F'),
        };
        enum { CMAP, HEAD, HHEA, HMTX, MAXP, LOCA, GLYF, OS2, POST, GPOS, KERN, FVAR, CFF, CFF2, CBDT, SBIX, GSUB, GDEF, TABLES };
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
        {
                face->gpos = at[GPOS];
                font_kern_lookups(face, at[GPOS]);
        }
        if (length[GSUB] >= 10)
                face->gsub = at[GSUB];
        if (length[GDEF] >= 12)
                face->gdef = at[GDEF];

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

__attribute__((used)) b32 font_advance(const font_face address_to face, p32 glyph, p32 size)
{
        return font_fixed(face, font_u16(face, ((const font_face *)(face))->hmtx + 4 * (positive)(((p32)(glyph)) < ((const font_face *)(face))->metrics ? ((p32)(glyph)) : ((const font_face *)(face))->metrics - 1)), size);
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

//      One pair adjustment subtable: whether it applies to the pair, and where the value records of the two glyphs
//      are when it does. A subtable that covers the first glyph but not the pair lets the next one try.
static p8 font_pair_at(const font_face address_to face, positive table, p32 left, p32 right, positive address_to first_at,
                         positive address_to second_at)
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
        *first_at = at;
        *second_at = at + 2 * (positive)font_bits(first);
        return true;
}

//      The first glyph's x advance from it when it applies: kerning.
static p8 font_pair(const font_face address_to face, positive table, p32 left, p32 right, b32 address_to value)
{
        positive at, second;
        p32 first = font_u16(face, table + 4);

        if (!font_pair_at(face, table, left, right, &at, &second))
                return false;
        if (first & 4)
                *value += font_i16(face, at + 2 * (positive)font_bits(first & 3));
        return true;
}

static __attribute__((unused)) b32 font_kerning_units_reference(const font_face address_to face, p32 left, p32 right)
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

                for (p32 t = font_u16(face, face->kern + 2), walked = 0; t && at < face->size && walked < FONT_SUBTABLES; t--, walked++)
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

//      font_kerning_units: the GPOS pair adjustment (formats 1 and 2, through extension lookups) and the kern table
//      when GPOS kerns nothing, in font units. It is assembly on arm64 (Mach-O and ELF) and on x86-64 (ELF), and the
//      routine there; font_kerning_units_reference above is the C it replaced, which test/font_kern_check.c compares it
//      with, bit for bit, over real faces and random ones, and which is the routine on every other target. The assembly
//      reads the face through the same bounds checks as the C: a read past the file's end is 0.
#if ARM64 && (defined(__APPLE__) || defined(__ELF__))
#define FONT_KERN_ARM64 1
#elif X64 && defined(__ELF__) && !defined(_WIN32)
#define FONT_KERN_X64 1
#endif

#if defined(__APPLE__)
#define FONT_SYM(name) "_" #name
#define FONT_KERN_TEXT "__TEXT,__text"
#else
#define FONT_SYM(name) #name
#define FONT_KERN_TEXT ".text"
#endif
#if defined(__ELF__)
#define FONT_KERN_DESC ".type " FONT_SYM(font_kerning_units) ", " ASM_TYPE "\n"
#define FONT_KERN_END ".size " FONT_SYM(font_kerning_units) ", .-" FONT_SYM(font_kerning_units) "\n"
#else
#define FONT_KERN_DESC ""
#define FONT_KERN_END ""
#endif

_Static_assert(offset_of(font_face, size) == 8 && offset_of(font_face, lookup_list) == 56 &&
               offset_of(font_face, kern) == 64 && offset_of(font_face, lookups) == 112 &&
               offset_of(font_face, lookup_count) == 144,
               "font_kerning_units: the assembly reads these fields at these offsets");
#if defined(FONT_KERN_ARM64)
b32 font_kerning_units(const font_face address_to face, p32 left, p32 right);
__asm__(".pushsection " FONT_KERN_TEXT "\n"
        ".globl " FONT_SYM(font_kerning_units) "\n"
        ".p2align 4\n"
        FONT_KERN_DESC
        ".macro FKU_RD16 d, a, t, u\n"
        "    add x\\t, x\\a, #2\n"
        "    cmp x\\t, x16\n"
        "    b.hi 1f\n"
        "    add x\\u, x17, x\\a\n"
        "    ldrh w\\d, [x\\u]\n"
        "    rev16 w\\d, w\\d\n"
        "    b 2f\n"
        "1:\n"
        "    mov w\\d, #0\n"
        "2:\n"
        ".endm\n"
        ".macro FKU_RD32 d, a, t, u\n"
        "    add x\\t, x\\a, #4\n"
        "    cmp x\\t, x16\n"
        "    b.hi 3f\n"
        "    add x\\u, x17, x\\a\n"
        "    ldr w\\d, [x\\u]\n"
        "    rev w\\d, w\\d\n"
        "    b 4f\n"
        "3:\n"
        "    mov w\\d, #0\n"
        "4:\n"
        ".endm\n"
        ".macro FKU_S16 low, tbl, cnt, stride, off, val, hi, mid, at, t, u, v\n"
        "    mov w\\low, #0\n"
        "    mov w\\hi, w\\cnt\n"
        "5:\n"
        "    cmp w\\low, w\\hi\n"
        "    b.hs 6f\n"
        "    sub w\\mid, w\\hi, w\\low\n"
        "    add w\\mid, w\\low, w\\mid, lsr #1\n"
        "    madd x\\at, x\\stride, x\\mid, x\\tbl\n"
        "    add x\\at, x\\at, x\\off\n"
        "    FKU_RD16 \\v, \\at, \\t, \\u\n"
        "    cmp w\\v, w\\val\n"
        "    b.hs 7f\n"
        "    add w\\low, w\\mid, #1\n"
        "    b 5b\n"
        "7:\n"
        "    mov w\\hi, w\\mid\n"
        "    b 5b\n"
        "6:\n"
        ".endm\n"
        ".macro FKU_S32 low, tbl, cnt, stride, off, val, hi, mid, at, t, u, v\n"
        "    mov w\\low, #0\n"
        "    mov w\\hi, w\\cnt\n"
        "8:\n"
        "    cmp w\\low, w\\hi\n"
        "    b.hs 10f\n"
        "    sub w\\mid, w\\hi, w\\low\n"
        "    add w\\mid, w\\low, w\\mid, lsr #1\n"
        "    madd x\\at, x\\stride, x\\mid, x\\tbl\n"
        "    add x\\at, x\\at, x\\off\n"
        "    FKU_RD32 \\v, \\at, \\t, \\u\n"
        "    cmp w\\v, w\\val\n"
        "    b.hs 9f\n"
        "    add w\\low, w\\mid, #1\n"
        "    b 8b\n"
        "9:\n"
        "    mov w\\hi, w\\mid\n"
        "    b 8b\n"
        "10:\n"
        ".endm\n"
        ".macro FKU_CLS dst, tbl, glyph, fmt, cnt, low, hi, mid, at, t, u, v, sx, ox, tx\n"
        "    FKU_RD16 \\fmt, \\tbl, \\t, \\u\n"
        "    cmp w\\fmt, #1\n"
        "    b.ne 12f\n"
        "    add x\\t, x\\tbl, #2\n"
        "    FKU_RD16 \\cnt, \\t, \\u, \\v\n"
        "    cmp w\\glyph, w\\cnt\n"
        "    b.lo 13f\n"
        "    sub w\\low, w\\glyph, w\\cnt\n"
        "    add x\\t, x\\tbl, #4\n"
        "    FKU_RD16 \\hi, \\t, \\u, \\v\n"
        "    cmp w\\low, w\\hi\n"
        "    b.hs 13f\n"
        "    add x\\at, x\\tbl, #6\n"
        "    add x\\at, x\\at, w\\low, uxtw #1\n"
        "    FKU_RD16 \\dst, \\at, \\t, \\u\n"
        "    b 14f\n"
        "12:\n"
        "    cmp w\\fmt, #2\n"
        "    b.ne 13f\n"
        "    add x\\t, x\\tbl, #2\n"
        "    FKU_RD16 \\cnt, \\t, \\u, \\v\n"
        "    mov x\\sx, #6\n"
        "    mov x\\ox, #2\n"
        "    add x\\tx, x\\tbl, #4\n"
        "    FKU_S16 \\low, \\tx, \\cnt, \\sx, \\ox, \\glyph, \\hi, \\mid, \\at, \\t, \\u, \\v\n"
        "    cmp w\\low, w\\cnt\n"
        "    b.hs 13f\n"
        "    madd x\\at, x\\sx, x\\low, x\\tx\n"
        "    FKU_RD16 \\v, \\at, \\t, \\u\n"
        "    cmp w\\v, w\\glyph\n"
        "    b.hi 13f\n"
        "    add x\\at, x\\at, #4\n"
        "    FKU_RD16 \\dst, \\at, \\t, \\u\n"
        "    b 14f\n"
        "13:\n"
        "    mov w\\dst, #0\n"
        "14:\n"
        ".endm\n"
        "\n"
        FONT_SYM(font_kerning_units) ":\n" ASM_ENDBR
        "    stp x29, x30, [sp, #-96]!\n"
        "    mov x29, sp\n"
        "    stp x19, x20, [sp, #16]\n"
        "    stp x21, x22, [sp, #32]\n"
        "    stp x23, x24, [sp, #48]\n"
        "    stp x25, x26, [sp, #64]\n"
        "    stp x27, x28, [sp, #80]\n"
        "    mov x19, x0\n"
        "    ldr x16, [x0, #8]\n"
        "    ldr x17, [x0]\n"
        "    mov w20, w1\n"
        "    mov w21, w2\n"
        "    mov w22, #0\n"
        "    mov w23, #0\n"
        "20:\n"
        "    ldrb w0, [x19, #144]\n"
        "    cmp w23, w0\n"
        "    b.hs 32f\n"
        "    add x0, x19, #112\n"
        "    ldrh w25, [x0, w23, uxtw #1]\n"
        "    ldr x1, [x19, #56]\n"
        "    FKU_RD16 0, 1, 2, 3\n"
        "    cmp w25, w0\n"
        "    b.hs 25f\n"
        "    ldr x1, [x19, #56]\n"
        "    add x1, x1, w25, uxtw #1\n"
        "    add x1, x1, #2\n"
        "    FKU_RD16 0, 1, 2, 3\n"
        "    ldr x24, [x19, #56]\n"
        "    add x24, x24, x0\n"
        "    FKU_RD16 25, 24, 2, 3\n"
        "    add x1, x24, #4\n"
        "    FKU_RD16 26, 1, 2, 3\n"
        "    mov w27, #0\n"
        "21:\n"
        "    cmp w27, w26\n"
        "    b.hs 25f\n"
        "    cmp w27, #256\n"
        "    b.hs 25f\n"
        "    add x1, x24, w27, uxtw #1\n"
        "    add x1, x1, #6\n"
        "    FKU_RD16 0, 1, 2, 3\n"
        "    add x28, x24, x0\n"
        "    cmp w25, #9\n"
        "    b.ne 22f\n"
        "    add x1, x28, #2\n"
        "    FKU_RD16 2, 1, 3, 4\n"
        "    add x1, x28, #4\n"
        "    FKU_RD32 5, 1, 3, 4\n"
        "    add x28, x28, x5\n"
        "    mov w1, w2\n"
        "    b 23f\n"
        "22:\n"
        "    mov w1, w25\n"
        "23:\n"
        "    cmp w1, #2\n"
        "    b.ne 24f\n"
        "    add x1, x28, #2\n"
        "    FKU_RD16 13, 1, 2, 3\n"
        "    add x13, x28, x13\n"
        "    FKU_RD16 14, 13, 2, 3\n"
        "    add x1, x13, #2\n"
        "    FKU_RD16 15, 1, 2, 3\n"
        "    cmp w14, #1\n"
        "    b.eq 26f\n"
        "    cmp w14, #2\n"
        "    b.ne 24f\n"
        "26:\n"
        "    add x3, x13, #4\n"
        "    mov x2, #6\n"
        "    mov x4, #2\n"
        "    cmp w14, #1\n"
        "    b.ne 27f\n"
        "    mov x2, #2\n"
        "    mov x4, #0\n"
        "27:\n"
        "    FKU_S16 1, 3, 15, 2, 4, 20, 5, 6, 7, 8, 9, 0\n"
        "    cmp w1, w15\n"
        "    b.eq 24f\n"
        "    madd x7, x2, x1, x3\n"
        "    FKU_RD16 0, 7, 8, 9\n"
        "    cmp w14, #1\n"
        "    b.ne 28f\n"
        "    cmp w0, w20\n"
        "    b.ne 24f\n"
        "    mov w15, w1\n"
        "    b 29f\n"
        "28:\n"
        "    cmp w0, w20\n"
        "    b.hi 24f\n"
        "    add x8, x7, #4\n"
        "    FKU_RD16 9, 8, 10, 11\n"
        "    add w15, w9, w20\n"
        "    sub w15, w15, w0\n"
        "29:\n"
        "    tbnz w15, #31, 24f\n"
        "    FKU_RD16 29, 28, 2, 3\n"
        "    add x1, x28, #4\n"
        "    FKU_RD16 10, 1, 2, 3\n"
        "    add x1, x28, #6\n"
        "    FKU_RD16 12, 1, 2, 3\n"
        "    and w12, w12, #0xff\n"
        "    and w11, w10, #0xff\n"
        "    fmov s0, w11\n"
        "    cnt v0.8b, v0.8b\n"
        "    addv b0, v0.8b\n"
        "    umov w11, v0.b[0]\n"
        "    fmov s1, w12\n"
        "    cnt v1.8b, v1.8b\n"
        "    addv b1, v1.8b\n"
        "    umov w12, v1.b[0]\n"
        "    add w12, w12, w11\n"
        "    lsl x12, x12, #1\n"
        "    cmp w29, #1\n"
        "    b.ne 30f\n"
        "    add x1, x28, #8\n"
        "    FKU_RD16 7, 1, 2, 3\n"
        "    cmp w15, w7\n"
        "    b.hs 24f\n"
        "    add x1, x28, #10\n"
        "    add x1, x1, w15, uxtw #1\n"
        "    FKU_RD16 7, 1, 2, 3\n"
        "    add x7, x28, x7\n"
        "    FKU_RD16 9, 7, 2, 3\n"
        "    add x3, x7, #2\n"
        "    add x2, x12, #2\n"
        "    mov x4, #0\n"
        "    FKU_S16 1, 3, 9, 2, 4, 21, 5, 6, 8, 10, 11, 0\n"
        "    cmp w1, w9\n"
        "    b.eq 24f\n"
        "    madd x11, x2, x1, x7\n"
        "    add x11, x11, #2\n"
        "    FKU_RD16 0, 11, 5, 6\n"
        "    cmp w0, w21\n"
        "    b.ne 24f\n"
        "    add x11, x11, #2\n"
        "    b 31f\n"
        "30:\n"
        "    cmp w29, #2\n"
        "    b.ne 24f\n"
        "    add x1, x28, #8\n"
        "    FKU_RD16 2, 1, 3, 4\n"
        "    add x2, x28, x2\n"
        "    FKU_CLS 5, 2, 20, 6, 7, 8, 9, 10, 11, 13, 14, 15, 0, 1, 3\n"
        "    add x1, x28, #10\n"
        "    FKU_RD16 2, 1, 3, 4\n"
        "    add x2, x28, x2\n"
        "    FKU_CLS 4, 2, 21, 6, 7, 8, 9, 10, 11, 13, 14, 15, 0, 1, 3\n"
        "    add x1, x28, #12\n"
        "    FKU_RD16 6, 1, 3, 7\n"
        "    add x1, x28, #14\n"
        "    FKU_RD16 7, 1, 3, 8\n"
        "    cmp w5, w6\n"
        "    b.hs 24f\n"
        "    cmp w4, w7\n"
        "    b.hs 24f\n"
        "    mul x8, x5, x7\n"
        "    add x8, x8, x4\n"
        "    madd x11, x8, x12, x28\n"
        "    add x11, x11, #16\n"
        "31:\n"
        "    add x1, x28, #4\n"
        "    FKU_RD16 10, 1, 2, 3\n"
        "    tbz w10, #2, 25f\n"
        "    and w0, w10, #3\n"
        "    sub w0, w0, w0, lsr #1\n"
        "    lsl w0, w0, #1\n"
        "    add x1, x11, x0\n"
        "    FKU_RD16 0, 1, 2, 3\n"
        "    sxth w0, w0\n"
        "    add w22, w22, w0\n"
        "    b 25f\n"
        "24:\n"
        "    add w27, w27, #1\n"
        "    b 21b\n"
        "25:\n"
        "    add w23, w23, #1\n"
        "    b 20b\n"
        "32:\n"
        "    ldrb w0, [x19, #144]\n"
        "    cbnz w0, 35f\n"
        "    ldr x24, [x19, #64]\n"
        "    cbz x24, 35f\n"
        "    FKU_RD16 0, 24, 2, 3\n"
        "    cbnz w0, 35f\n"
        "    add x25, x24, #4\n"
        "    add x1, x24, #2\n"
        "    FKU_RD16 26, 1, 2, 3\n"
        "    mov w27, #0\n"
        "33:\n"
        "    cbz w26, 35f\n"
        "    cmp x25, x16\n"
        "    b.hs 35f\n"
        "    cmp w27, #256\n"
        "    b.hs 35f\n"
        "    add x1, x25, #2\n"
        "    FKU_RD16 28, 1, 2, 3\n"
        "    add x1, x25, #4\n"
        "    FKU_RD16 0, 1, 2, 3\n"
        "    mov w1, #0xff07\n"
        "    and w0, w0, w1\n"
        "    cmp w0, #1\n"
        "    b.ne 34f\n"
        "    add x1, x25, #6\n"
        "    FKU_RD16 0, 1, 2, 3\n"
        "    lsl w5, w20, #16\n"
        "    orr w5, w5, w21\n"
        "    add x3, x25, #14\n"
        "    mov x2, #6\n"
        "    mov x4, #0\n"
        "    FKU_S32 1, 3, 0, 2, 4, 5, 6, 7, 8, 9, 10, 11\n"
        "    cmp w1, w0\n"
        "    b.hs 35f\n"
        "    madd x8, x2, x1, x3\n"
        "    FKU_RD32 9, 8, 10, 11\n"
        "    cmp w9, w5\n"
        "    b.ne 35f\n"
        "    add x8, x8, #4\n"
        "    FKU_RD16 9, 8, 10, 11\n"
        "    sxth w22, w9\n"
        "    b 35f\n"
        "34:\n"
        "    cmp w28, #6\n"
        "    b.lo 35f\n"
        "    add x25, x25, x28\n"
        "    add w27, w27, #1\n"
        "    sub w26, w26, #1\n"
        "    b 33b\n"
        "35:\n"
        "    mov w0, w22\n"
        "    ldp x27, x28, [sp, #80]\n"
        "    ldp x25, x26, [sp, #64]\n"
        "    ldp x23, x24, [sp, #48]\n"
        "    ldp x21, x22, [sp, #32]\n"
        "    ldp x19, x20, [sp, #16]\n"
        "    ldp x29, x30, [sp], #96\n"
        ASM_RET
        FONT_KERN_END
        ".purgem FKU_RD16\n"
        ".purgem FKU_RD32\n"
        ".purgem FKU_S16\n"
        ".purgem FKU_S32\n"
        ".purgem FKU_CLS\n"
        ".popsection\n");
#elif defined(FONT_KERN_X64)
b32 font_kerning_units(const font_face address_to face, p32 left, p32 right);
__asm__(".pushsection " FONT_KERN_TEXT "\n"
        ".globl " FONT_SYM(font_kerning_units) "\n"
        ".p2align 4\n"
        FONT_KERN_DESC
        ".macro RD16 d, a\n"
        "    cmpq %r12, \\a\n"
        "    jg 1f\n"
        "    movzwl (%rbx,\\a), \\d\n"
        "    bswapl \\d\n"
        "    shrl $16, \\d\n"
        "    jmp 2f\n"
        "1:\n"
        "    xorl \\d, \\d\n"
        "2:\n"
        ".endm\n"
        ".macro RD32 d, a\n"
        "    cmpq %r13, \\a\n"
        "    jg 3f\n"
        "    movl (%rbx,\\a), \\d\n"
        "    bswapl \\d\n"
        "    jmp 4f\n"
        "3:\n"
        "    xorl \\d, \\d\n"
        "4:\n"
        ".endm\n"
        ".macro POP8 r, t\n"
        "    movl \\r, \\t\n"
        "    shrl $1, \\t\n"
        "    andl $0x55, \\t\n"
        "    subl \\t, \\r\n"
        "    movl \\r, \\t\n"
        "    andl $0x33, \\r\n"
        "    shrl $2, \\t\n"
        "    andl $0x33, \\t\n"
        "    addl \\t, \\r\n"
        "    movl \\r, \\t\n"
        "    shrl $4, \\t\n"
        "    addl \\t, \\r\n"
        "    andl $0x0f, \\r\n"
        ".endm\n"
        ".macro S16 low, tbl, cnt, stride, off, val, hi, mid, midq, at, v\n"
        "    xorl \\low, \\low\n"
        "    movl \\cnt, \\hi\n"
        "5:\n"
        "    cmpl \\hi, \\low\n"
        "    jae 6f\n"
        "    movl \\hi, \\mid\n"
        "    subl \\low, \\mid\n"
        "    shrl $1, \\mid\n"
        "    addl \\low, \\mid\n"
        "    movq \\stride, \\at\n"
        "    imulq \\midq, \\at\n"
        "    addq \\tbl, \\at\n"
        "    addq \\off, \\at\n"
        "    RD16 \\v, \\at\n"
        "    cmpl \\val, \\v\n"
        "    jb 7f\n"
        "    movl \\mid, \\hi\n"
        "    jmp 5b\n"
        "7:\n"
        "    movl \\mid, \\low\n"
        "    incl \\low\n"
        "    jmp 5b\n"
        "6:\n"
        ".endm\n"
        ".macro S32 low, tbl, cnt, stride, off, val, hi, mid, midq, at, v\n"
        "    xorl \\low, \\low\n"
        "    movl \\cnt, \\hi\n"
        "8:\n"
        "    cmpl \\hi, \\low\n"
        "    jae 9f\n"
        "    movl \\hi, \\mid\n"
        "    subl \\low, \\mid\n"
        "    shrl $1, \\mid\n"
        "    addl \\low, \\mid\n"
        "    movq \\stride, \\at\n"
        "    imulq \\midq, \\at\n"
        "    addq \\tbl, \\at\n"
        "    addq \\off, \\at\n"
        "    RD32 \\v, \\at\n"
        "    cmpl \\val, \\v\n"
        "    jb 10f\n"
        "    movl \\mid, \\hi\n"
        "    jmp 8b\n"
        "10:\n"
        "    movl \\mid, \\low\n"
        "    incl \\low\n"
        "    jmp 8b\n"
        "9:\n"
        ".endm\n"
        ".macro CLS dst, tbl, glyph, cnt, low, lowq, hi, mid, midq, at, v, tx\n"
        "    RD16 \\dst, \\tbl\n"
        "    cmpl $1, \\dst\n"
        "    jne 12f\n"
        "    leaq 2(\\tbl), \\at\n"
        "    RD16 \\cnt, \\at\n"
        "    cmpl \\cnt, \\glyph\n"
        "    jb 13f\n"
        "    movl \\glyph, \\low\n"
        "    subl \\cnt, \\low\n"
        "    leaq 4(\\tbl), \\at\n"
        "    RD16 \\hi, \\at\n"
        "    cmpl \\hi, \\low\n"
        "    jae 13f\n"
        "    leaq 6(\\tbl,\\lowq,2), \\at\n"
        "    RD16 \\dst, \\at\n"
        "    jmp 14f\n"
        "12:\n"
        "    cmpl $2, \\dst\n"
        "    jne 13f\n"
        "    leaq 2(\\tbl), \\at\n"
        "    RD16 \\cnt, \\at\n"
        "    leaq 4(\\tbl), \\tx\n"
        "    S16 \\low, \\tx, \\cnt, $6, $2, \\glyph, \\hi, \\mid, \\midq, \\at, \\v\n"
        "    cmpl \\cnt, \\low\n"
        "    jae 13f\n"
        "    movq $6, \\at\n"
        "    imulq \\lowq, \\at\n"
        "    addq \\tx, \\at\n"
        "    RD16 \\v, \\at\n"
        "    cmpl \\glyph, \\v\n"
        "    ja 13f\n"
        "    addq $4, \\at\n"
        "    RD16 \\dst, \\at\n"
        "    jmp 14f\n"
        "13:\n"
        "    xorl \\dst, \\dst\n"
        "14:\n"
        ".endm\n"
        "\n"
        FONT_SYM(font_kerning_units) ":\n" ASM_ENDBR
        "    pushq %rbx\n"
        "    pushq %rbp\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    subq $104, %rsp\n"
        "    movq %rdi, 0(%rsp)\n"
        "    movl %esi, %r14d\n"
        "    movl %edx, %r15d\n"
        "    xorl %ebp, %ebp\n"
        "    movq (%rdi), %rbx\n"
        "    movq 8(%rdi), %rax\n"
        "    leaq -2(%rax), %r12\n"
        "    leaq -4(%rax), %r13\n"
        "    movq $0, 8(%rsp)\n"
        "20:\n"
        "    movq 0(%rsp), %rdi\n"
        "    movq 8(%rsp), %rcx\n"
        "    movzbl 144(%rdi), %eax\n"
        "    cmpq %rax, %rcx\n"
        "    jae 32f\n"
        "    movzwl 112(%rdi,%rcx,2), %r8d\n"
        "    movq 56(%rdi), %rsi\n"
        "    RD16 %edx, %rsi\n"
        "    cmpl %edx, %r8d\n"
        "    jae 25f\n"
        "    movq 56(%rdi), %rsi\n"
        "    leaq 2(%rsi,%r8,2), %rax\n"
        "    RD16 %edx, %rax\n"
        "    addq %rsi, %rdx\n"
        "    movq %rdx, 16(%rsp)\n"
        "    RD16 %eax, %rdx\n"
        "    movq %rax, 24(%rsp)\n"
        "    leaq 4(%rdx), %rcx\n"
        "    RD16 %eax, %rcx\n"
        "    movq %rax, 32(%rsp)\n"
        "    movq $0, 40(%rsp)\n"
        "21:\n"
        "    movq 40(%rsp), %rcx\n"
        "    cmpq 32(%rsp), %rcx\n"
        "    jae 25f\n"
        "    cmpq $256, %rcx\n"
        "    jae 25f\n"
        "    movq 16(%rsp), %rdx\n"
        "    leaq 6(%rdx,%rcx,2), %rsi\n"
        "    RD16 %eax, %rsi\n"
        "    leaq (%rdx,%rax), %rsi\n"
        "    movq %rsi, 48(%rsp)\n"
        "    movq 24(%rsp), %rax\n"
        "    cmpl $9, %eax\n"
        "    jne 22f\n"
        "    leaq 2(%rsi), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    leaq 4(%rsi), %rcx\n"
        "    RD32 %r8d, %rcx\n"
        "    addq %r8, %rsi\n"
        "    movq %rsi, 48(%rsp)\n"
        "    jmp 23f\n"
        "22:\n"
        "    movl %eax, %edx\n"
        "23:\n"
        "    cmpl $2, %edx\n"
        "    jne 24f\n"
        "    movq 48(%rsp), %rsi\n"
        "    leaq 2(%rsi), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    leaq (%rsi,%rdx), %rdx\n"
        "    movq %rdx, 56(%rsp)\n"
        "    RD16 %eax, %rdx\n"
        "    movq %rax, 64(%rsp)\n"
        "    leaq 2(%rdx), %r8\n"
        "    RD16 %r8d, %r8\n"
        "    cmpl $1, %eax\n"
        "    je 26f\n"
        "    cmpl $2, %eax\n"
        "    jne 24f\n"
        "26:\n"
        "    movq 56(%rsp), %rdx\n"
        "    leaq 4(%rdx), %rsi\n"
        "    movq 64(%rsp), %rax\n"
        "    movq $6, %r11\n"
        "    movq $2, %r10\n"
        "    cmpl $1, %eax\n"
        "    jne 27f\n"
        "    movq $2, %r11\n"
        "    movq $0, %r10\n"
        "27:\n"
        "    S16 %r9d, %rsi, %r8d, %r11, %r10, %r14d, %edi, %ecx, %rcx, %rdx, %eax\n"
        "    cmpl %r8d, %r9d\n"
        "    je 24f\n"
        "    movq 56(%rsp), %rdx\n"
        "    leaq 4(%rdx), %rdx\n"
        "    movq 64(%rsp), %rax\n"
        "    movq $6, %rcx\n"
        "    cmpl $1, %eax\n"
        "    jne 28f\n"
        "    movq $2, %rcx\n"
        "28:\n"
        "    imulq %r9, %rcx\n"
        "    leaq (%rdx,%rcx), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    cmpl $1, %eax\n"
        "    jne 29f\n"
        "    cmpl %r14d, %edx\n"
        "    jne 24f\n"
        "    movl %r9d, %r8d\n"
        "    jmp 30f\n"
        "29:\n"
        "    cmpl %r14d, %edx\n"
        "    ja 24f\n"
        "    leaq 4(%rcx), %rsi\n"
        "    RD16 %r8d, %rsi\n"
        "    addl %r14d, %r8d\n"
        "    subl %edx, %r8d\n"
        "30:\n"
        "    testl %r8d, %r8d\n"
        "    js 24f\n"
        "    movq 48(%rsp), %rsi\n"
        "    RD16 %eax, %rsi\n"
        "    movq %rax, 72(%rsp)\n"
        "    leaq 4(%rsi), %rcx\n"
        "    RD16 %r9d, %rcx\n"
        "    leaq 6(%rsi), %rcx\n"
        "    RD16 %r10d, %rcx\n"
        "    movzbl %r9b, %r9d\n"
        "    movzbl %r10b, %r10d\n"
        "    POP8 %r9d, %ecx\n"
        "    POP8 %r10d, %ecx\n"
        "    addl %r10d, %r9d\n"
        "    addl %r9d, %r9d\n"
        "    movq %r9, 80(%rsp)\n"
        "    movq 72(%rsp), %rax\n"
        "    cmpl $1, %eax\n"
        "    jne 40f\n"
        "    movq 48(%rsp), %rsi\n"
        "    leaq 8(%rsi), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    cmpl %edx, %r8d\n"
        "    jae 24f\n"
        "    leaq 10(%rsi,%r8,2), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    leaq (%rsi,%rdx), %rdx\n"
        "    RD16 %r11d, %rdx\n"
        "    leaq 2(%rdx), %rsi\n"
        "    movq 80(%rsp), %r10\n"
        "    addq $2, %r10\n"
        "    S16 %r9d, %rsi, %r11d, %r10, $0, %r15d, %edi, %ecx, %rcx, %rax, %r8d\n"
        "    cmpl %r11d, %r9d\n"
        "    je 24f\n"
        "    movq 80(%rsp), %rcx\n"
        "    addq $2, %rcx\n"
        "    imulq %r9, %rcx\n"
        "    leaq 2(%rdx,%rcx), %rax\n"
        "    RD16 %r8d, %rax\n"
        "    cmpl %r15d, %r8d\n"
        "    jne 24f\n"
        "    leaq 4(%rdx,%rcx), %rax\n"
        "    movq %rax, 88(%rsp)\n"
        "    jmp 31f\n"
        "40:\n"
        "    cmpl $2, %eax\n"
        "    jne 24f\n"
        "    movq 48(%rsp), %rsi\n"
        "    leaq 8(%rsi), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    leaq (%rsi,%rdx), %rsi\n"
        "    CLS %r8d, %rsi, %r14d, %r9d, %r10d, %r10, %r11d, %edi, %rdi, %rax, %edx, %rcx\n"
        "    movl %r8d, 96(%rsp)\n"
        "    movq 48(%rsp), %rsi\n"
        "    leaq 10(%rsi), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    leaq (%rsi,%rdx), %rsi\n"
        "    CLS %r8d, %rsi, %r15d, %r9d, %r10d, %r10, %r11d, %edi, %rdi, %rax, %edx, %rcx\n"
        "    movq 48(%rsp), %rsi\n"
        "    leaq 12(%rsi), %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    leaq 14(%rsi), %rcx\n"
        "    RD16 %r10d, %rcx\n"
        "    movl 96(%rsp), %r9d\n"
        "    cmpl %edx, %r9d\n"
        "    jae 24f\n"
        "    cmpl %r10d, %r8d\n"
        "    jae 24f\n"
        "    imulq %r10, %r9\n"
        "    addq %r8, %r9\n"
        "    imulq 80(%rsp), %r9\n"
        "    leaq 16(%rsi,%r9), %rax\n"
        "    movq %rax, 88(%rsp)\n"
        "    jmp 31f\n"
        "31:\n"
        "    movq 48(%rsp), %rsi\n"
        "    leaq 4(%rsi), %rcx\n"
        "    RD16 %r10d, %rcx\n"
        "    testl $4, %r10d\n"
        "    jz 25f\n"
        "    andl $3, %r10d\n"
        "    movl %r10d, %ecx\n"
        "    shrl $1, %ecx\n"
        "    subl %ecx, %r10d\n"
        "    addl %r10d, %r10d\n"
        "    movq 88(%rsp), %rax\n"
        "    addq %r10, %rax\n"
        "    RD16 %edx, %rax\n"
        "    movswl %dx, %edx\n"
        "    addl %edx, %ebp\n"
        "    jmp 25f\n"
        "24:\n"
        "    incq 40(%rsp)\n"
        "    jmp 21b\n"
        "25:\n"
        "    incq 8(%rsp)\n"
        "    jmp 20b\n"
        "32:\n"
        "    movq 0(%rsp), %rdi\n"
        "    cmpb $0, 144(%rdi)\n"
        "    jne 35f\n"
        "    movq 64(%rdi), %rsi\n"
        "    testq %rsi, %rsi\n"
        "    jz 35f\n"
        "    RD16 %eax, %rsi\n"
        "    testl %eax, %eax\n"
        "    jnz 35f\n"
        "    leaq 2(%rsi), %rcx\n"
        "    RD16 %r8d, %rcx\n"
        "    leaq 4(%rsi), %rdx\n"
        "    xorl %r9d, %r9d\n"
        "33:\n"
        "    testl %r8d, %r8d\n"
        "    jz 35f\n"
        "    cmpq 8(%rdi), %rdx\n"
        "    jae 35f\n"
        "    cmpl $256, %r9d\n"
        "    jae 35f\n"
        "    leaq 2(%rdx), %rcx\n"
        "    RD16 %r10d, %rcx\n"
        "    leaq 4(%rdx), %rcx\n"
        "    RD16 %r11d, %rcx\n"
        "    andl $0xff07, %r11d\n"
        "    cmpl $1, %r11d\n"
        "    jne 34f\n"
        "    leaq 6(%rdx), %rcx\n"
        "    RD16 %eax, %rcx\n"
        "    movl %r14d, %r8d\n"
        "    shll $16, %r8d\n"
        "    orl %r15d, %r8d\n"
        "    leaq 14(%rdx), %rsi\n"
        "    S32 %r11d, %rsi, %eax, $6, $0, %r8d, %edi, %ecx, %rcx, %r9, %r10d\n"
        "    cmpl %eax, %r11d\n"
        "    jae 35f\n"
        "    movq $6, %rcx\n"
        "    imulq %r11, %rcx\n"
        "    leaq 14(%rdx,%rcx), %rcx\n"
        "    RD32 %edx, %rcx\n"
        "    cmpl %r8d, %edx\n"
        "    jne 35f\n"
        "    addq $4, %rcx\n"
        "    RD16 %edx, %rcx\n"
        "    movswl %dx, %ebp\n"
        "    jmp 35f\n"
        "34:\n"
        "    cmpl $6, %r10d\n"
        "    jb 35f\n"
        "    addq %r10, %rdx\n"
        "    incl %r9d\n"
        "    decl %r8d\n"
        "    jmp 33b\n"
        "35:\n"
        "    movl %ebp, %eax\n"
        "    addq $104, %rsp\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rbp\n"
        "    popq %rbx\n"
        ASM_RET
        FONT_KERN_END
        ".purgem RD16\n"
        ".purgem RD32\n"
        ".purgem S16\n"
        ".purgem S32\n"
        ".purgem CLS\n"
        ".purgem POP8\n"
        ".popsection\n");
#else
#define font_kerning_units font_kerning_units_reference
#endif

b32 font_kerning(const font_face address_to face, p32 left, p32 right, p32 size)
{
        b32 value = font_kerning_units(face, left, right);

        return value ? font_fixed(face, value, size) : 0;
}

/*
        Unicode, for text that is not left to right and not a glyph a code
        point. The tables are the UCD's of the version named, as data: the
        Bidi_Class of every code point, the pairs that mirror and the
        brackets that pair, how Arabic letters join, the combining classes
        of the Hebrew and Arabic marks (HarfBuzz's modified ones, which put
        a shadda before its vowel), the canonical decompositions of the
        letters those blocks hold precomposed, and the presentation forms a
        face with no layout tables is shaped by.
*/
/* BEGIN GENERATED: Unicode 18.0.0 tables (UCD DerivedBidiClass, BidiMirroring, BidiBrackets, DerivedJoiningType,
   DerivedCombiningClass, UnicodeData); a table here is data, not code, and is rebuilt from the UCD files. */
#define FONT_UNICODE "18.0.0"

enum { FONT_BIDI_L = 0, FONT_BIDI_R = 1, FONT_BIDI_AL = 2, FONT_BIDI_EN = 3, FONT_BIDI_ES = 4, FONT_BIDI_ET = 5, FONT_BIDI_AN = 6, FONT_BIDI_CS = 7, FONT_BIDI_NSM = 8, FONT_BIDI_BN = 9, FONT_BIDI_B = 10, FONT_BIDI_S = 11, FONT_BIDI_WS = 12, FONT_BIDI_ON = 13, FONT_BIDI_LRE = 14, FONT_BIDI_LRO = 15, FONT_BIDI_RLE = 16, FONT_BIDI_RLO = 17, FONT_BIDI_PDF = 18, FONT_BIDI_LRI = 19, FONT_BIDI_RLI = 20, FONT_BIDI_FSI = 21, FONT_BIDI_PDI = 22 };

//      Bidi_Class of U+0000 to U+00FF, and from U+0100 on the ranges: (first code point << 5) | class, the last whose first is not above.
static const p8 font_bidi_flat[256] = {
    9, 9, 9, 9, 9, 9, 9, 9, 9, 11, 10, 11, 12, 10, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 11,
    12, 13, 13, 5, 5, 5, 13, 13, 13, 13, 13, 4, 7, 4, 7, 7,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7, 13, 13, 13, 13, 13,
    13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 13,
    13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 13, 13, 13, 9,
    9, 9, 9, 9, 9, 10, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    7, 13, 5, 5, 5, 5, 13, 13, 13, 13, 0, 13, 13, 9, 13, 13,
    5, 5, 3, 3, 13, 0, 13, 13, 13, 3, 0, 13, 13, 13, 13, 13,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0,
};
#define FONT_BIDI_RANGES 1225
static const p32 font_bidi_ranges[1225] = {
    0x2000, 0x572d, 0x5760, 0x584d, 0x5a00, 0x5a4d, 0x5c00, 0x5cad,
    0x5dc0, 0x5ded, 0x6008, 0x6e00, 0x6e8d, 0x6ec0, 0x6fcd, 0x6fe0,
    0x708d, 0x70c0, 0x70ed, 0x7100, 0x7ecd, 0x7ee0, 0x9068, 0x9140,
    0xb14d, 0xb160, 0xb1ad, 0xb1e5, 0xb201, 0xb228, 0xb7c1, 0xb7e8,
    0xb801, 0xb828, 0xb861, 0xb888, 0xb8c1, 0xb8e8, 0xb941, 0xc006,
    0xc0cd, 0xc102, 0xc125, 0xc162, 0xc187, 0xc1a2, 0xc1cd, 0xc208,
    0xc362, 0xc968, 0xcc06, 0xcd45, 0xcd66, 0xcda2, 0xce08, 0xce22,
    0xdac8, 0xdba6, 0xdbcd, 0xdbe8, 0xdca2, 0xdce8, 0xdd2d, 0xdd48,
    0xddc2, 0xde03, 0xdf42, 0xe228, 0xe242, 0xe608, 0xe962, 0xf4c8,
    0xf622, 0xf801, 0xfd68, 0xfe81, 0xfecd, 0xff41, 0xffa8, 0xffc1,
    0x102c8, 0x10341, 0x10368, 0x10481, 0x104a8, 0x10501, 0x10528, 0x105c1,
    0x10b28, 0x10b81, 0x10c02, 0x11206, 0x11242, 0x112e8, 0x11402, 0x11948,
    0x11c46, 0x11c68, 0x12060, 0x12748, 0x12760, 0x12788, 0x127a0, 0x12828,
    0x12920, 0x129a8, 0x129c0, 0x12a28, 0x12b00, 0x12c48, 0x12c80, 0x13028,
    0x13040, 0x13788, 0x137a0, 0x13828, 0x138a0, 0x139a8, 0x139c0, 0x13c48,
    0x13c80, 0x13e45, 0x13e80, 0x13f65, 0x13f80, 0x13fc8, 0x13fe0, 0x14028,
    0x14060, 0x14788, 0x147a0, 0x14828, 0x14860, 0x148e8, 0x14920, 0x14968,
    0x149c0, 0x14a28, 0x14a40, 0x14e08, 0x14e40, 0x14ea8, 0x14ec0, 0x15028,
    0x15060, 0x15788, 0x157a0, 0x15828, 0x158c0, 0x158e8, 0x15920, 0x159a8,
    0x159c0, 0x15c48, 0x15c80, 0x15e25, 0x15e40, 0x15f48, 0x16000, 0x16028,
    0x16040, 0x16788, 0x167a0, 0x167e8, 0x16800, 0x16828, 0x168a0, 0x169a8,
    0x169c0, 0x16a68, 0x16ae0, 0x16c48, 0x16c80, 0x17048, 0x17060, 0x17808,
    0x17820, 0x179a8, 0x179c0, 0x17e6d, 0x17f25, 0x17f4d, 0x17f60, 0x18008,
    0x18020, 0x18088, 0x180a0, 0x18788, 0x187a0, 0x187c8, 0x18820, 0x188c8,
    0x18920, 0x18948, 0x189c0, 0x18aa8, 0x18ae0, 0x18c48, 0x18c80, 0x18f0d,
    0x18fe0, 0x19028, 0x19040, 0x19788, 0x197a0, 0x19988, 0x199c0, 0x19c48,
    0x19c80, 0x1a008, 0x1a040, 0x1a768, 0x1a7a0, 0x1a828, 0x1a8a0, 0x1a9a8,
    0x1a9c0, 0x1ac48, 0x1ac80, 0x1b028, 0x1b040, 0x1b948, 0x1b960, 0x1ba48,
    0x1baa0, 0x1bac8, 0x1bae0, 0x1c628, 0x1c640, 0x1c688, 0x1c760, 0x1c7e5,
    0x1c800, 0x1c8e8, 0x1c9e0, 0x1d628, 0x1d640, 0x1d688, 0x1d7a0, 0x1d908,
    0x1d9e0, 0x1e308, 0x1e340, 0x1e6a8, 0x1e6c0, 0x1e6e8, 0x1e700, 0x1e728,
    0x1e74d, 0x1e7c0, 0x1ee28, 0x1efe0, 0x1f008, 0x1f0a0, 0x1f0c8, 0x1f100,
    0x1f1a8, 0x1f300, 0x1f328, 0x1f7a0, 0x1f8c8, 0x1f8e0, 0x205a8, 0x20620,
    0x20648, 0x20700, 0x20728, 0x20760, 0x207a8, 0x207e0, 0x20b08, 0x20b40,
    0x20bc8, 0x20c20, 0x20e28, 0x20ea0, 0x21048, 0x21060, 0x210a8, 0x210e0,
    0x211a8, 0x211c0, 0x213a8, 0x213c0, 0x26ba8, 0x26c00, 0x2720d, 0x27340,
    0x2800d, 0x28020, 0x2d00c, 0x2d020, 0x2d36d, 0x2d3a0, 0x2e248, 0x2e2a0,
    0x2e648, 0x2e680, 0x2ea48, 0x2ea80, 0x2ee48, 0x2ee80, 0x2f688, 0x2f6c0,
    0x2f6e8, 0x2f7c0, 0x2f8c8, 0x2f8e0, 0x2f928, 0x2fa80, 0x2fb65, 0x2fb80,
    0x2fba8, 0x2fbc0, 0x2fe0d, 0x2ff40, 0x3000d, 0x30168, 0x301c9, 0x301e8,
    0x30200, 0x310a8, 0x310e0, 0x31528, 0x31540, 0x32408, 0x32460, 0x324e8,
    0x32520, 0x32648, 0x32660, 0x32728, 0x32780, 0x3280d, 0x32820, 0x3288d,
    0x328c0, 0x33bcd, 0x34000, 0x342e8, 0x34320, 0x34368, 0x34380, 0x34ac8,
    0x34ae0, 0x34b08, 0x34be0, 0x34c08, 0x34c20, 0x34c48, 0x34c60, 0x34ca8,
    0x34da0, 0x34e68, 0x34fa0, 0x34fe8, 0x35000, 0x35608, 0x35e20, 0x36008,
    0x36080, 0x36688, 0x366a0, 0x366c8, 0x36740, 0x36d68, 0x36e80, 0x37008,
    0x37040, 0x37448, 0x374c0, 0x37508, 0x37540, 0x37568, 0x375c0, 0x37cc8,
    0x37ce0, 0x37d08, 0x37d40, 0x37da8, 0x37dc0, 0x37de8, 0x37e40, 0x38588,
    0x38680, 0x386c8, 0x38700, 0x39a08, 0x39a60, 0x39a88, 0x39c20, 0x39c48,
    0x39d20, 0x39da8, 0x39dc0, 0x39e88, 0x39ea0, 0x39f08, 0x39f40, 0x3b808,
    0x3c000, 0x3f7ad, 0x3f7c0, 0x3f7ed, 0x3f840, 0x3f9ad, 0x3fa00, 0x3fbad,
    0x3fc00, 0x3fdad, 0x3fe00, 0x3ffad, 0x3ffe0, 0x4000c, 0x40169, 0x401c0,
    0x401e1, 0x4020d, 0x4050c, 0x4052a, 0x4054e, 0x40570, 0x40592, 0x405af,
    0x405d1, 0x405e7, 0x40605, 0x406ad, 0x40887, 0x408ad, 0x40bec, 0x40c09,
    0x40cd3, 0x40cf4, 0x40d15, 0x40d36, 0x40d49, 0x40e03, 0x40e20, 0x40e83,
    0x40f44, 0x40f8d, 0x40fe0, 0x41003, 0x41144, 0x4118d, 0x41200, 0x41405,
    0x41a08, 0x41e20, 0x4200d, 0x42040, 0x4206d, 0x420e0, 0x4210d, 0x42140,
    0x4228d, 0x422a0, 0x422cd, 0x42320, 0x423cd, 0x42480, 0x424ad, 0x424c0,
    0x424ed, 0x42500, 0x4252d, 0x42540, 0x425c5, 0x425e0, 0x4274d, 0x42780,
    0x4280d, 0x428a0, 0x4294d, 0x429c0, 0x42a0d, 0x42c00, 0x4312d, 0x43180,
    0x4320d, 0x44244, 0x44265, 0x4428d, 0x466c0, 0x46f6d, 0x472a0, 0x472cd,
    0x48540, 0x4880d, 0x48960, 0x48c0d, 0x49103, 0x49380, 0x49d4d, 0x4d580,
    0x4d5ad, 0x50000, 0x5200d, 0x56e80, 0x56ecd, 0x58000, 0x59cad, 0x59d60,
    0x59de8, 0x59e40, 0x59f2d, 0x5a000, 0x5afe8, 0x5b000, 0x5bc08, 0x5c00d,
    0x5cbc0, 0x5cc0d, 0x5cc80, 0x5d00d, 0x5d340, 0x5d36d, 0x5de80, 0x5e00d,
    0x5fac0, 0x5fe0d, 0x6000c, 0x6002d, 0x600a0, 0x6010d, 0x60420, 0x60548,
    0x605c0, 0x6060d, 0x60620, 0x606cd, 0x60700, 0x607ad, 0x60800, 0x61328,
    0x6136d, 0x613a0, 0x6140d, 0x61420, 0x61f6d, 0x61f80, 0x6380d, 0x63cc0,
    0x63ded, 0x63e00, 0x643ad, 0x643e0, 0x64a0d, 0x64c00, 0x64f8d, 0x64fe0,
    0x6562d, 0x65800, 0x6598d, 0x65a00, 0x66eed, 0x66f60, 0x67bcd, 0x67c00,
    0x67fed, 0x68000, 0x9b80d, 0x9c000, 0x14920d, 0x1498e0, 0x14c1ad, 0x14c200,
    0x14cde8, 0x14ce6d, 0x14ce88, 0x14cfcd, 0x14d000, 0x14d3c8, 0x14d400, 0x14de08,
    0x14de40, 0x14e00d, 0x14e440, 0x14f10d, 0x14f120, 0x150048, 0x150060, 0x1500c8,
    0x1500e0, 0x150168, 0x150180, 0x1504a8, 0x1504e0, 0x15050d, 0x150588, 0x1505a0,
    0x150705, 0x150740, 0x150e8d, 0x150f00, 0x151888, 0x1518c0, 0x151c08, 0x151e40,
    0x151fe8, 0x152000, 0x1524c8, 0x1525c0, 0x1528e8, 0x152a40, 0x153008, 0x153060,
    0x153668, 0x153680, 0x1536c8, 0x153740, 0x153788, 0x1537c0, 0x153ca8, 0x153cc0,
    0x154528, 0x1545e0, 0x154628, 0x154660, 0x1546a8, 0x1546e0, 0x154868, 0x154880,
    0x154988, 0x1549a0, 0x154f88, 0x154fa0, 0x155608, 0x155620, 0x155648, 0x1556a0,
    0x1556e8, 0x155720, 0x1557c8, 0x155800, 0x155828, 0x155840, 0x155d88, 0x155dc0,
    0x155ec8, 0x155ee0, 0x156d4d, 0x156d80, 0x157ca8, 0x157cc0, 0x157d08, 0x157d20,
    0x157da8, 0x157dc0, 0x1f63a1, 0x1f63c8, 0x1f63e1, 0x1f6524, 0x1f6541, 0x1f6a02,
    0x1f786d, 0x1f7a62, 0x1fa7cd, 0x1faa02, 0x1fb20d, 0x1fb242, 0x1fb90d, 0x1fba09,
    0x1fbe02, 0x1fbfad, 0x1fc008, 0x1fc20d, 0x1fc340, 0x1fc408, 0x1fc60d, 0x1fca07,
    0x1fca2d, 0x1fca47, 0x1fca60, 0x1fca8d, 0x1fcaa7, 0x1fcacd, 0x1fcbe5, 0x1fcc0d,
    0x1fcc44, 0x1fcc8d, 0x1fcce0, 0x1fcd0d, 0x1fcd25, 0x1fcd6d, 0x1fcd80, 0x1fce02,
    0x1fdfe9, 0x1fe000, 0x1fe02d, 0x1fe065, 0x1fe0cd, 0x1fe164, 0x1fe187, 0x1fe1a4,
    0x1fe1c7, 0x1fe203, 0x1fe347, 0x1fe36d, 0x1fe420, 0x1fe76d, 0x1fe820, 0x1feb6d,
    0x1fecc0, 0x1ffc05, 0x1ffc4d, 0x1ffca5, 0x1ffce0, 0x1ffd0d, 0x1ffde0, 0x1ffe09,
    0x1fff2d, 0x1fffc9, 0x200000, 0x20202d, 0x202040, 0x20280d, 0x2031a0, 0x20320d,
    0x2033a0, 0x20340d, 0x203420, 0x203fa8, 0x203fc0, 0x205c08, 0x205c23, 0x205f80,
    0x206ec8, 0x206f60, 0x210001, 0x2123ed, 0x212401, 0x214028, 0x214081, 0x2140a8,
    0x2140e1, 0x214188, 0x214201, 0x214708, 0x214761, 0x2147e8, 0x214801, 0x215ca8,
    0x215ce1, 0x21672d, 0x216801, 0x21a002, 0x21a488, 0x21a502, 0x21a606, 0x21a742,
    0x21a806, 0x21a941, 0x21ad28, 0x21adcd, 0x21ade1, 0x21cc06, 0x21cfe1, 0x21d568,
    0x21d5a1, 0x21d802, 0x21d968, 0x21da0d, 0x21db22, 0x21de08, 0x21e001, 0x21e602,
    0x21e8c8, 0x21ea22, 0x21ee01, 0x21f048, 0x21f0c1, 0x220000, 0x220028, 0x220040,
    0x220708, 0x2208e0, 0x220a4d, 0x220cc0, 0x220e08, 0x220e20, 0x220e68, 0x220ea0,
    0x220fe8, 0x221040, 0x221668, 0x2216e0, 0x221728, 0x221760, 0x221848, 0x221860,
    0x222008, 0x222060, 0x2224e8, 0x222580, 0x2225a8, 0x2226a0, 0x222e68, 0x222e80,
    0x223008, 0x223040, 0x2236c8, 0x2237e0, 0x223928, 0x2239a0, 0x2239e8, 0x223a00,
    0x2245e8, 0x224640, 0x224688, 0x2246a0, 0x2246c8, 0x224700, 0x2247c8, 0x2247e0,
    0x224828, 0x224840, 0x225be8, 0x225c00, 0x225c68, 0x225d60, 0x226008, 0x226040,
    0x226768, 0x2267a0, 0x226808, 0x226820, 0x226cc8, 0x226da0, 0x226e08, 0x226ea0,
    0x227768, 0x227820, 0x2279c8, 0x2279e0, 0x227a08, 0x227a20, 0x227a48, 0x227a60,
    0x227c28, 0x227c60, 0x228708, 0x228800, 0x228848, 0x2288a0, 0x2288c8, 0x2288e0,
    0x228bc8, 0x228be0, 0x229668, 0x229720, 0x229748, 0x229760, 0x2297e8, 0x229820,
    0x229848, 0x229880, 0x22b648, 0x22b6c0, 0x22b788, 0x22b7c0, 0x22b7e8, 0x22b820,
    0x22bb88, 0x22bbc0, 0x22c668, 0x22c760, 0x22c7a8, 0x22c7c0, 0x22c7e8, 0x22c820,
    0x22cc0d, 0x22cda0, 0x22d568, 0x22d580, 0x22d5a8, 0x22d5c0, 0x22d608, 0x22d6c0,
    0x22d6e8, 0x22d700, 0x22e3a8, 0x22e3c0, 0x22e3e8, 0x22e400, 0x22e448, 0x22e4c0,
    0x22e4e8, 0x22e580, 0x2305e8, 0x230700, 0x230728, 0x230760, 0x232768, 0x2327a0,
    0x2327c8, 0x2327e0, 0x232868, 0x232880, 0x233a88, 0x233b00, 0x233b48, 0x233b80,
    0x233c08, 0x233c20, 0x234028, 0x2340e0, 0x234128, 0x234160, 0x234668, 0x234720,
    0x234768, 0x2347e0, 0x2348e8, 0x234900, 0x234a28, 0x234ae0, 0x234b28, 0x234b80,
    0x235148, 0x2352e0, 0x235308, 0x235340, 0x236c08, 0x236c20, 0x236c48, 0x236ca0,
    0x236cc8, 0x236ce0, 0x238608, 0x2386e0, 0x238708, 0x2387c0, 0x239248, 0x239500,
    0x239548, 0x239620, 0x239648, 0x239680, 0x2396a8, 0x2396e0, 0x23a628, 0x23a6e0,
    0x23a748, 0x23a760, 0x23a788, 0x23a7c0, 0x23a7e8, 0x23a8c0, 0x23a8e8, 0x23a900,
    0x23b208, 0x23b240, 0x23b2a8, 0x23b2c0, 0x23b2e8, 0x23b300, 0x23be08, 0x23be20,
    0x23de68, 0x23dea0, 0x23e008, 0x23e040, 0x23e6c8, 0x23e760, 0x23e808, 0x23e820,
    0x23e848, 0x23e860, 0x23eb48, 0x23eb60, 0x23faad, 0x23fba5, 0x23fc2d, 0x23fe40,
    0x268808, 0x268820, 0x2688e8, 0x268ac0, 0x2c23c8, 0x2c2540, 0x2c25a8, 0x2c2600,
    0x2d5e08, 0x2d5ea0, 0x2d6608, 0x2d66e0, 0x2de9e8, 0x2dea00, 0x2df1e8, 0x2df260,
    0x2dfc4d, 0x2dfc60, 0x2dfc88, 0x2dfca0, 0x3793a8, 0x3793e0, 0x379409, 0x379480,
    0x39800d, 0x399ac0, 0x399e03, 0x399f4d, 0x399fa0, 0x39a00d, 0x39d680, 0x39d74d,
    0x39da20, 0x39da4d, 0x39daa0, 0x39dbad, 0x39dfc0, 0x39e008, 0x39e5c0, 0x39e608,
    0x39e8e0, 0x3a24e8, 0x3a2520, 0x3a2ce8, 0x3a2d40, 0x3a2e69, 0x3a2f68, 0x3a3060,
    0x3a30a8, 0x3a3180, 0x3a3548, 0x3a35c0, 0x3a3d2d, 0x3a3d60, 0x3a400d, 0x3a4848,
    0x3a48ad, 0x3a48c0, 0x3a4b68, 0x3a4ba0, 0x3a600d, 0x3a6ae0, 0x3ad82d, 0x3ad840,
    0x3adb6d, 0x3adb80, 0x3adf6d, 0x3adf80, 0x3ae2ad, 0x3ae2c0, 0x3ae6ad, 0x3ae6c0,
    0x3ae9ed, 0x3aea00, 0x3aeded, 0x3aee00, 0x3af12d, 0x3af140, 0x3af52d, 0x3af540,
    0x3af86d, 0x3af880, 0x3af9c3, 0x3b0000, 0x3b4008, 0x3b46e0, 0x3b4768, 0x3b4da0,
    0x3b4ea8, 0x3b4ec0, 0x3b5088, 0x3b50a0, 0x3b5368, 0x3b5400, 0x3b5428, 0x3b5600,
    0x3b600d, 0x3b63a0, 0x3c0008, 0x3c00e0, 0x3c0108, 0x3c0320, 0x3c0368, 0x3c0440,
    0x3c0468, 0x3c04a0, 0x3c04c8, 0x3c0560, 0x3c11e8, 0x3c1200, 0x3c2608, 0x3c26e0,
    0x3c55c8, 0x3c55e0, 0x3c5d88, 0x3c5e00, 0x3c5fe5, 0x3c6000, 0x3c9d88, 0x3c9e00,
    0x3cbdc8, 0x3cbe00, 0x3cdc68, 0x3cdc80, 0x3cdcc8, 0x3cdce0, 0x3cddc8, 0x3cde00,
    0x3cdea8, 0x3cdec0, 0x3d0001, 0x3d1a08, 0x3d1ae1, 0x3d2888, 0x3d2961, 0x3d8e02,
    0x3d9801, 0x3da002, 0x3daa01, 0x3dc002, 0x3dde0d, 0x3dde42, 0x3de001, 0x3e000d,
    0x3e0580, 0x3e060d, 0x3e1280, 0x3e140d, 0x3e15e0, 0x3e162d, 0x3e1800, 0x3e182d,
    0x3e1a00, 0x3e1a2d, 0x3e1ec0, 0x3e2003, 0x3e216d, 0x3e2200, 0x3e25ed, 0x3e2600,
    0x3e2d4d, 0x3e2e00, 0x3e35ad, 0x3e35c0, 0x3e4c0d, 0x3e4cc0, 0x3e600d, 0x3edb40,
    0x3edb8d, 0x3edda0, 0x3ede0d, 0x3edfa0, 0x3ee00d, 0x3efb40, 0x3efb6d, 0x3efb80,
    0x3efc0d, 0x3efd80, 0x3efe0d, 0x3f0180, 0x3f020d, 0x3f0900, 0x3f0a0d, 0x3f0b40,
    0x3f0c0d, 0x3f1100, 0x3f120d, 0x3f15c0, 0x3f160d, 0x3f1780, 0x3f180d, 0x3f1840,
    0x3f1a0d, 0x3f1b20, 0x3f200d, 0x3f4b00, 0x3f4c0d, 0x3f4dc0, 0x3f4e0d, 0x3f4fa0,
    0x3f500d, 0x3f58e0, 0x3f590d, 0x3f5920, 0x3f598d, 0x3f5bc0, 0x3f5bed, 0x3f5d80,
    0x3f5ded, 0x3f5f60, 0x3f600d, 0x3f7260, 0x3f728d, 0x3f7e03, 0x3f7f4d, 0x3f7f60,
    0x3fffc9, 0x400000, 0x5fffc9, 0x600000, 0x7fffc9, 0x800000, 0x9fffc9, 0xa00000,
    0xbfffc9, 0xc00000, 0xdfffc9, 0xe00000, 0xffffc9, 0x1000000, 0x11fffc9, 0x1200000,
    0x13fffc9, 0x1400000, 0x15fffc9, 0x1600000, 0x17fffc9, 0x1800000, 0x19fffc9, 0x1a00000,
    0x1bfffc9, 0x1c02008, 0x1c03e09, 0x1c20000, 0x1dfffc9, 0x1e00000, 0x1ffffc9, 0x2000000,
    0x21fffc9,
};

//      Bidi_Mirroring_Glyph, the exact pairs: code point and the signed distance to its mirror.
#define FONT_MIRRORS 364
static const p32 font_mirror_code[364] = {
    0x28, 0x29, 0x3c, 0x3e, 0x5b, 0x5d, 0x7b, 0x7d, 0xab, 0xbb,
    0xf3a, 0xf3b, 0xf3c, 0xf3d, 0x169b, 0x169c, 0x2039, 0x203a, 0x2045, 0x2046,
    0x207d, 0x207e, 0x208d, 0x208e, 0x2208, 0x220a, 0x220b, 0x220d, 0x2215, 0x221d,
    0x221f, 0x2220, 0x2221, 0x2222, 0x2224, 0x223c, 0x223d, 0x2243, 0x2245, 0x224c,
    0x2252, 0x2253, 0x2254, 0x2255, 0x2264, 0x2265, 0x2266, 0x2267, 0x226a, 0x226b,
    0x2276, 0x2277, 0x227a, 0x227b, 0x227c, 0x227d, 0x2282, 0x2283, 0x2286, 0x2287,
    0x228f, 0x2290, 0x2291, 0x2292, 0x2298, 0x22a2, 0x22a3, 0x22a6, 0x22a8, 0x22a9,
    0x22ab, 0x22b0, 0x22b1, 0x22b2, 0x22b3, 0x22b4, 0x22b5, 0x22b6, 0x22b7, 0x22b8,
    0x22c9, 0x22ca, 0x22cb, 0x22cc, 0x22cd, 0x22d0, 0x22d1, 0x22d6, 0x22d7, 0x22d8,
    0x22d9, 0x22da, 0x22db, 0x22dc, 0x22dd, 0x22de, 0x22df, 0x22f0, 0x22f1, 0x22f2,
    0x22f3, 0x22f4, 0x22f6, 0x22f7, 0x22fa, 0x22fb, 0x22fc, 0x22fd, 0x22fe, 0x2308,
    0x2309, 0x230a, 0x230b, 0x2329, 0x232a, 0x2768, 0x2769, 0x276a, 0x276b, 0x276c,
    0x276d, 0x276e, 0x276f, 0x2770, 0x2771, 0x2772, 0x2773, 0x2774, 0x2775, 0x27c3,
    0x27c4, 0x27c5, 0x27c6, 0x27c8, 0x27c9, 0x27cb, 0x27cd, 0x27d5, 0x27d6, 0x27dc,
    0x27dd, 0x27de, 0x27e2, 0x27e3, 0x27e4, 0x27e5, 0x27e6, 0x27e7, 0x27e8, 0x27e9,
    0x27ea, 0x27eb, 0x27ec, 0x27ed, 0x27ee, 0x27ef, 0x2983, 0x2984, 0x2985, 0x2986,
    0x2987, 0x2988, 0x2989, 0x298a, 0x298b, 0x298c, 0x298d, 0x298e, 0x298f, 0x2990,
    0x2991, 0x2992, 0x2993, 0x2994, 0x2995, 0x2996, 0x2997, 0x2998, 0x299b, 0x29a0,
    0x29a3, 0x29a4, 0x29a5, 0x29a8, 0x29a9, 0x29aa, 0x29ab, 0x29ac, 0x29ad, 0x29ae,
    0x29af, 0x29b8, 0x29c0, 0x29c1, 0x29c4, 0x29c5, 0x29cf, 0x29d0, 0x29d1, 0x29d2,
    0x29d4, 0x29d5, 0x29d8, 0x29d9, 0x29da, 0x29db, 0x29e8, 0x29e9, 0x29f5, 0x29f8,
    0x29f9, 0x29fc, 0x29fd, 0x2a2b, 0x2a2c, 0x2a2d, 0x2a2e, 0x2a34, 0x2a35, 0x2a3c,
    0x2a3d, 0x2a64, 0x2a65, 0x2a79, 0x2a7a, 0x2a7d, 0x2a7e, 0x2a7f, 0x2a80, 0x2a81,
    0x2a82, 0x2a83, 0x2a84, 0x2a8b, 0x2a8c, 0x2a91, 0x2a92, 0x2a93, 0x2a94, 0x2a95,
    0x2a96, 0x2a97, 0x2a98, 0x2a99, 0x2a9a, 0x2a9b, 0x2a9c, 0x2aa1, 0x2aa2, 0x2aa6,
    0x2aa7, 0x2aa8, 0x2aa9, 0x2aaa, 0x2aab, 0x2aac, 0x2aad, 0x2aaf, 0x2ab0, 0x2ab3,
    0x2ab4, 0x2abb, 0x2abc, 0x2abd, 0x2abe, 0x2abf, 0x2ac0, 0x2ac1, 0x2ac2, 0x2ac3,
    0x2ac4, 0x2ac5, 0x2ac6, 0x2acd, 0x2ace, 0x2acf, 0x2ad0, 0x2ad1, 0x2ad2, 0x2ad3,
    0x2ad4, 0x2ad5, 0x2ad6, 0x2ade, 0x2ae3, 0x2ae4, 0x2ae5, 0x2aec, 0x2aed, 0x2aee,
    0x2af7, 0x2af8, 0x2af9, 0x2afa, 0x2bfe, 0x2e02, 0x2e03, 0x2e04, 0x2e05, 0x2e09,
    0x2e0a, 0x2e0c, 0x2e0d, 0x2e1c, 0x2e1d, 0x2e20, 0x2e21, 0x2e22, 0x2e23, 0x2e24,
    0x2e25, 0x2e26, 0x2e27, 0x2e28, 0x2e29, 0x2e55, 0x2e56, 0x2e57, 0x2e58, 0x2e59,
    0x2e5a, 0x2e5b, 0x2e5c, 0x2e62, 0x2e63, 0x3008, 0x3009, 0x300a, 0x300b, 0x3010,
    0x3011, 0x3014, 0x3015, 0x3016, 0x3017, 0x3018, 0x3019, 0x301a, 0x301b, 0xfe59,
    0xfe5a, 0xfe5b, 0xfe5c, 0xfe5d, 0xfe5e, 0xfe64, 0xfe65, 0xff08, 0xff09, 0xff1c,
    0xff1e, 0xff3b, 0xff3d, 0xff5b, 0xff5d, 0xff5f, 0xff60, 0x1db03, 0x1db04, 0x1db05,
    0x1db06, 0x1db08, 0x1db09, 0x1db10,
};
static const b32 font_mirror_delta[364] = {
    1, -1, 2, -2, 2, -2, 2, -2, 16, -16, 1, -1,
    1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1,
    3, 3, -3, -3, 2016, 112883, 2527, 1923, 1914, 1918, 2250, 1,
    -1, 138, 7, -7, 1, -1, 1, -1, 1, -1, 1, -1,
    1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1,
    1, -1, 1, -1, 1824, 1, -1, 2104, 2108, 2106, 2106, 1,
    -1, 1, -1, 1, -1, 1, -1, 1316, 1, -1, 1, -1,
    -138, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 8, 8, 8, 7, 7, -8, -8, -8, -7,
    -7, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 2, -2, 1, -1, -1316, 1, -1, 1, -1,
    1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1,
    1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 3, 1,
    -1, -3, 1, -1, 1, -1, 1, -1, 1, -1, -1914, -1918,
    -1923, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, -1824,
    1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1,
    1, -1, 1, -1, -2016, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, -2104, -2106, -2108, -2106, 1,
    -1, -2250, 1, -1, 1, -1, -2527, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1,
    -1, 2, -2, 2, -2, 2, -2, 1, -1, 1, -1, 1,
    -1, 1, -1, -112883,
};

//      Bidi_Paired_Bracket: code point, and its partner with bit 31 set for an opening bracket.
#define FONT_BRACKETS 130
static const p32 font_bracket_code[130] = {
    0x28, 0x29, 0x5b, 0x5d, 0x7b, 0x7d, 0xf3a, 0xf3b, 0xf3c, 0xf3d,
    0x169b, 0x169c, 0x2045, 0x2046, 0x207d, 0x207e, 0x208d, 0x208e, 0x2308, 0x2309,
    0x230a, 0x230b, 0x2329, 0x232a, 0x2768, 0x2769, 0x276a, 0x276b, 0x276c, 0x276d,
    0x276e, 0x276f, 0x2770, 0x2771, 0x2772, 0x2773, 0x2774, 0x2775, 0x27c5, 0x27c6,
    0x27e6, 0x27e7, 0x27e8, 0x27e9, 0x27ea, 0x27eb, 0x27ec, 0x27ed, 0x27ee, 0x27ef,
    0x2983, 0x2984, 0x2985, 0x2986, 0x2987, 0x2988, 0x2989, 0x298a, 0x298b, 0x298c,
    0x298d, 0x298e, 0x298f, 0x2990, 0x2991, 0x2992, 0x2993, 0x2994, 0x2995, 0x2996,
    0x2997, 0x2998, 0x29d8, 0x29d9, 0x29da, 0x29db, 0x29fc, 0x29fd, 0x2e22, 0x2e23,
    0x2e24, 0x2e25, 0x2e26, 0x2e27, 0x2e28, 0x2e29, 0x2e55, 0x2e56, 0x2e57, 0x2e58,
    0x2e59, 0x2e5a, 0x2e5b, 0x2e5c, 0x2e62, 0x2e63, 0x3008, 0x3009, 0x300a, 0x300b,
    0x300c, 0x300d, 0x300e, 0x300f, 0x3010, 0x3011, 0x3014, 0x3015, 0x3016, 0x3017,
    0x3018, 0x3019, 0x301a, 0x301b, 0xfe59, 0xfe5a, 0xfe5b, 0xfe5c, 0xfe5d, 0xfe5e,
    0xff08, 0xff09, 0xff3b, 0xff3d, 0xff5b, 0xff5d, 0xff5f, 0xff60, 0xff62, 0xff63,
};
static const p32 font_bracket_pair[130] = {
    0x80000029, 0x28, 0x8000005d, 0x5b, 0x8000007d, 0x7b, 0x80000f3b, 0xf3a,
    0x80000f3d, 0xf3c, 0x8000169c, 0x169b, 0x80002046, 0x2045, 0x8000207e, 0x207d,
    0x8000208e, 0x208d, 0x80002309, 0x2308, 0x8000230b, 0x230a, 0x8000232a, 0x2329,
    0x80002769, 0x2768, 0x8000276b, 0x276a, 0x8000276d, 0x276c, 0x8000276f, 0x276e,
    0x80002771, 0x2770, 0x80002773, 0x2772, 0x80002775, 0x2774, 0x800027c6, 0x27c5,
    0x800027e7, 0x27e6, 0x800027e9, 0x27e8, 0x800027eb, 0x27ea, 0x800027ed, 0x27ec,
    0x800027ef, 0x27ee, 0x80002984, 0x2983, 0x80002986, 0x2985, 0x80002988, 0x2987,
    0x8000298a, 0x2989, 0x8000298c, 0x298b, 0x80002990, 0x298f, 0x8000298e, 0x298d,
    0x80002992, 0x2991, 0x80002994, 0x2993, 0x80002996, 0x2995, 0x80002998, 0x2997,
    0x800029d9, 0x29d8, 0x800029db, 0x29da, 0x800029fd, 0x29fc, 0x80002e23, 0x2e22,
    0x80002e25, 0x2e24, 0x80002e27, 0x2e26, 0x80002e29, 0x2e28, 0x80002e56, 0x2e55,
    0x80002e58, 0x2e57, 0x80002e5a, 0x2e59, 0x80002e5c, 0x2e5b, 0x80002e63, 0x2e62,
    0x80003009, 0x3008, 0x8000300b, 0x300a, 0x8000300d, 0x300c, 0x8000300f, 0x300e,
    0x80003011, 0x3010, 0x80003015, 0x3014, 0x80003017, 0x3016, 0x80003019, 0x3018,
    0x8000301b, 0x301a, 0x8000fe5a, 0xfe59, 0x8000fe5c, 0xfe5b, 0x8000fe5e, 0xfe5d,
    0x8000ff09, 0xff08, 0x8000ff3d, 0xff3b, 0x8000ff5d, 0xff5b, 0x8000ff60, 0xff5f,
    0x8000ff63, 0xff62,
};

//      Joining_Type (0 U, 1 L, 2 R, 3 D or C, 7 T) below U+2100 and in U+FE00-FEFF: (first << 3) | type.
#define FONT_JOINS 456
static const p32 font_join_ranges[456] = {
    0x0, 0x56f, 0x570, 0x1807, 0x1b80, 0x241f, 0x2450, 0x2c8f, 0x2df0, 0x2dff,
    0x2e00, 0x2e0f, 0x2e18, 0x2e27, 0x2e30, 0x2e3f, 0x2e50, 0x3087, 0x30d8, 0x30e7,
    0x30e8, 0x3103, 0x3108, 0x3112, 0x3133, 0x313a, 0x3143, 0x314a, 0x3153, 0x317a,
    0x319b, 0x3242, 0x324b, 0x325f, 0x3300, 0x3373, 0x3387, 0x338a, 0x33a0, 0x33aa,
    0x33c3, 0x3442, 0x34d3, 0x3602, 0x360b, 0x361a, 0x3663, 0x366a, 0x3673, 0x367a,
    0x3683, 0x3692, 0x36a0, 0x36aa, 0x36b7, 0x36e8, 0x36ff, 0x3728, 0x373f, 0x3748,
    0x3757, 0x3772, 0x3780, 0x37d3, 0x37e8, 0x37fb, 0x3800, 0x387f, 0x3882, 0x388f,
    0x3893, 0x38aa, 0x38d3, 0x38f2, 0x38fb, 0x3942, 0x394b, 0x3952, 0x395b, 0x3962,
    0x396b, 0x397a, 0x3987, 0x3a58, 0x3a6a, 0x3a73, 0x3aca, 0x3ae3, 0x3b5a, 0x3b6b,
    0x3b8a, 0x3b93, 0x3b9a, 0x3bab, 0x3bc2, 0x3bd3, 0x3c00, 0x3d37, 0x3d88, 0x3e53,
    0x3f5f, 0x3fa0, 0x3fd3, 0x3fd8, 0x3fef, 0x3ff0, 0x40b7, 0x40d0, 0x40df, 0x4120,
    0x412f, 0x4140, 0x414f, 0x4170, 0x4202, 0x420b, 0x4232, 0x4243, 0x424a, 0x4253,
    0x42a2, 0x42ab, 0x42b2, 0x42cf, 0x42e0, 0x4303, 0x4308, 0x4313, 0x4330, 0x433a,
    0x4343, 0x434a, 0x4358, 0x4382, 0x441b, 0x4438, 0x444b, 0x4472, 0x447b, 0x4480,
    0x44bf, 0x4503, 0x4552, 0x4568, 0x4572, 0x457b, 0x458a, 0x459b, 0x45ca, 0x45d3,
    0x4648, 0x4657, 0x4710, 0x471f, 0x4818, 0x49d7, 0x49d8, 0x49e7, 0x49e8, 0x4a0f,
    0x4a48, 0x4a6f, 0x4a70, 0x4a8f, 0x4ac0, 0x4b17, 0x4b20, 0x4c0f, 0x4c10, 0x4de7,
    0x4de8, 0x4e0f, 0x4e28, 0x4e6f, 0x4e70, 0x4f17, 0x4f20, 0x4ff7, 0x4ff8, 0x500f,
    0x5018, 0x51e7, 0x51e8, 0x520f, 0x5218, 0x523f, 0x5248, 0x525f, 0x5270, 0x528f,
    0x5290, 0x5387, 0x5390, 0x53af, 0x53b0, 0x540f, 0x5418, 0x55e7, 0x55e8, 0x560f,
    0x5630, 0x563f, 0x5648, 0x566f, 0x5670, 0x5717, 0x5720, 0x57d7, 0x5800, 0x580f,
    0x5810, 0x59e7, 0x59e8, 0x59ff, 0x5a00, 0x5a0f, 0x5a28, 0x5a6f, 0x5a70, 0x5a9f,
    0x5ab8, 0x5b17, 0x5b20, 0x5c17, 0x5c18, 0x5e07, 0x5e08, 0x5e6f, 0x5e70, 0x6007,
    0x6008, 0x6027, 0x6028, 0x61e7, 0x61e8, 0x61f7, 0x6208, 0x6237, 0x6248, 0x6257,
    0x6270, 0x62af, 0x62b8, 0x6317, 0x6320, 0x640f, 0x6410, 0x65e7, 0x65e8, 0x65ff,
    0x6600, 0x6637, 0x6638, 0x6667, 0x6670, 0x6717, 0x6720, 0x6807, 0x6810, 0x69df,
    0x69e8, 0x6a0f, 0x6a28, 0x6a6f, 0x6a70, 0x6b17, 0x6b20, 0x6c0f, 0x6c10, 0x6e57,
    0x6e58, 0x6e97, 0x6ea8, 0x6eb7, 0x6eb8, 0x718f, 0x7190, 0x71a7, 0x71d8, 0x723f,
    0x7278, 0x758f, 0x7590, 0x75a7, 0x75e8, 0x7647, 0x7678, 0x78c7, 0x78d0, 0x79af,
    0x79b0, 0x79bf, 0x79c0, 0x79cf, 0x79d0, 0x7b8f, 0x7bf8, 0x7c07, 0x7c28, 0x7c37,
    0x7c40, 0x7c6f, 0x7cc0, 0x7ccf, 0x7de8, 0x7e37, 0x7e38, 0x816f, 0x8188, 0x8197,
    0x81c0, 0x81cf, 0x81d8, 0x81ef, 0x81f8, 0x82c7, 0x82d0, 0x82f7, 0x8308, 0x838f,
    0x83a8, 0x8417, 0x8418, 0x842f, 0x8438, 0x846f, 0x8470, 0x84ef, 0x84f0, 0x9aef,
    0x9b00, 0xb897, 0xb8a8, 0xb997, 0xb9a0, 0xba97, 0xbaa0, 0xbb97, 0xbba0, 0xbda7,
    0xbdb0, 0xbdbf, 0xbdf0, 0xbe37, 0xbe38, 0xbe4f, 0xbea0, 0xbeef, 0xbef0, 0xc03b,
    0xc040, 0xc053, 0xc05f, 0xc070, 0xc07f, 0xc080, 0xc103, 0xc3c8, 0xc42f, 0xc43b,
    0xc54f, 0xc553, 0xc558, 0xc907, 0xc918, 0xc93f, 0xc948, 0xc997, 0xc998, 0xc9cf,
    0xc9e0, 0xd0bf, 0xd0c8, 0xd0df, 0xd0e0, 0xd2b7, 0xd2b8, 0xd2c7, 0xd2f8, 0xd307,
    0xd308, 0xd317, 0xd318, 0xd32f, 0xd368, 0xd39f, 0xd3e8, 0xd3ff, 0xd400, 0xd587,
    0xd788, 0xd807, 0xd820, 0xd9a7, 0xd9a8, 0xd9b7, 0xd9d8, 0xd9e7, 0xd9e8, 0xda17,
    0xda18, 0xdb5f, 0xdba0, 0xdc07, 0xdc10, 0xdd17, 0xdd30, 0xdd47, 0xdd50, 0xdd5f,
    0xdd70, 0xdf37, 0xdf38, 0xdf47, 0xdf50, 0xdf6f, 0xdf70, 0xdf7f, 0xdf90, 0xe167,
    0xe1a0, 0xe1b7, 0xe1c0, 0xe687, 0xe698, 0xe6a7, 0xe708, 0xe717, 0xe748, 0xe76f,
    0xe770, 0xe7a7, 0xe7a8, 0xe7c7, 0xe7d0, 0xee07, 0xf000, 0x1005f, 0x10060, 0x1006b,
    0x10077, 0x10080, 0x10157, 0x10178, 0x10307, 0x10328, 0x10357, 0x10380, 0x10687, 0x10788,
    0x7f007, 0x7f080, 0x7f107, 0x7f180, 0x7f7ff, 0x7f800,
};

//      HarfBuzz's modified combining class, U+0590 on: (first << 8) | class.
#define FONT_MCCS 139
static const p32 font_mccc_ranges[139] = {
    0x59000, 0x591dc, 0x592e6, 0x596dc, 0x597e6, 0x59ade, 0x59bdc, 0x59ce6, 0x5a2dc, 0x5a8e6,
    0x5aadc, 0x5abe6, 0x5adde, 0x5aee4, 0x5afe6, 0x5b016, 0x5b10f, 0x5b210, 0x5b311, 0x5b417,
    0x5b512, 0x5b613, 0x5b714, 0x5b815, 0x5b90e, 0x5bb18, 0x5bc0c, 0x5bd19, 0x5be00, 0x5bf0d,
    0x5c000, 0x5c10a, 0x5c20b, 0x5c300, 0x5c4e6, 0x5c5dc, 0x5c600, 0x5c715, 0x5c816, 0x5c90c,
    0x5ca00, 0x610e6, 0x6181f, 0x61920, 0x61a21, 0x61b00, 0x64b1c, 0x64c1d, 0x64d1e, 0x64e1f,
    0x64f20, 0x65021, 0x6511b, 0x65222, 0x653e6, 0x655dc, 0x657e6, 0x65cdc, 0x65de6, 0x65fdc,
    0x66000, 0x67023, 0x67100, 0x6d6e6, 0x6dd00, 0x6dfe6, 0x6e3dc, 0x6e4e6, 0x6e500, 0x6e7e6,
    0x6e900, 0x6eadc, 0x6ebe6, 0x6eddc, 0x6ee00, 0x71124, 0x71200, 0x730e6, 0x731dc, 0x732e6,
    0x734dc, 0x735e6, 0x737dc, 0x73ae6, 0x73bdc, 0x73de6, 0x73edc, 0x73fe6, 0x742dc, 0x743e6,
    0x744dc, 0x745e6, 0x746dc, 0x747e6, 0x748dc, 0x749e6, 0x74b00, 0x7ebe6, 0x7f2dc, 0x7f3e6,
    0x7f400, 0x7fddc, 0x7fe00, 0x816e6, 0x81a00, 0x81be6, 0x82400, 0x825e6, 0x82800, 0x829e6,
    0x82e00, 0x859dc, 0x85c00, 0x897e6, 0x899dc, 0x89ce6, 0x8a000, 0x8cae6, 0x8cfdc, 0x8d4e6,
    0x8e200, 0x8e3dc, 0x8e4e6, 0x8e6dc, 0x8e7e6, 0x8e9dc, 0x8eae6, 0x8eddc, 0x8f01c, 0x8f11d,
    0x8f21e, 0x8f3e6, 0x8f6dc, 0x8f7e6, 0x8f9dc, 0x8fbe6, 0x90000, 0xfb1e1a, 0xfb1f00,
};

//      Canonical decompositions in the Hebrew and Arabic blocks and their presentation forms: code, first, second (0 for a singleton),
//      and whether the pair composes (it is not a composition exclusion).
#define FONT_DECOMPS 42
static const p16 font_decomp_code[42] = {
    0x622, 0x623, 0x624, 0x625, 0x626, 0x6c0, 0x6c2, 0x6d3, 0xfb1d, 0xfb1f, 0xfb2a, 0xfb2b, 0xfb2c, 0xfb2d,
    0xfb2e, 0xfb2f, 0xfb30, 0xfb31, 0xfb32, 0xfb33, 0xfb34, 0xfb35, 0xfb36, 0xfb38, 0xfb39, 0xfb3a, 0xfb3b, 0xfb3c,
    0xfb3e, 0xfb40, 0xfb41, 0xfb43, 0xfb44, 0xfb46, 0xfb47, 0xfb48, 0xfb49, 0xfb4a, 0xfb4b, 0xfb4c, 0xfb4d, 0xfb4e,
};
static const p16 font_decomp_first[42] = {
    0x627, 0x627, 0x648, 0x627, 0x64a, 0x6d5, 0x6c1, 0x6d2, 0x5d9, 0x5f2, 0x5e9, 0x5e9, 0xfb49, 0xfb49,
    0x5d0, 0x5d0, 0x5d0, 0x5d1, 0x5d2, 0x5d3, 0x5d4, 0x5d5, 0x5d6, 0x5d8, 0x5d9, 0x5da, 0x5db, 0x5dc,
    0x5de, 0x5e0, 0x5e1, 0x5e3, 0x5e4, 0x5e6, 0x5e7, 0x5e8, 0x5e9, 0x5ea, 0x5d5, 0x5d1, 0x5db, 0x5e4,
};
static const p16 font_decomp_second[42] = {
    0x653, 0x654, 0x654, 0x655, 0x654, 0x654, 0x654, 0x654, 0x5b4, 0x5b7, 0x5c1, 0x5c2, 0x5c1, 0x5c2,
    0x5b7, 0x5b8, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc,
    0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5bc, 0x5b9, 0x5bf, 0x5bf, 0x5bf,
};
static const p8 font_decomp_composes[42] = {
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

//      Arabic presentation forms: base letter, then its isolated, final, initial and medial forms (0 for none).
#define FONT_FORMS 76
static const p16 font_forms[76][5] = {
    {0x621, 0xfe80, 0x0, 0x0, 0x0},
    {0x622, 0xfe81, 0xfe82, 0x0, 0x0},
    {0x623, 0xfe83, 0xfe84, 0x0, 0x0},
    {0x624, 0xfe85, 0xfe86, 0x0, 0x0},
    {0x625, 0xfe87, 0xfe88, 0x0, 0x0},
    {0x626, 0xfe89, 0xfe8a, 0xfe8b, 0xfe8c},
    {0x627, 0xfe8d, 0xfe8e, 0x0, 0x0},
    {0x628, 0xfe8f, 0xfe90, 0xfe91, 0xfe92},
    {0x629, 0xfe93, 0xfe94, 0x0, 0x0},
    {0x62a, 0xfe95, 0xfe96, 0xfe97, 0xfe98},
    {0x62b, 0xfe99, 0xfe9a, 0xfe9b, 0xfe9c},
    {0x62c, 0xfe9d, 0xfe9e, 0xfe9f, 0xfea0},
    {0x62d, 0xfea1, 0xfea2, 0xfea3, 0xfea4},
    {0x62e, 0xfea5, 0xfea6, 0xfea7, 0xfea8},
    {0x62f, 0xfea9, 0xfeaa, 0x0, 0x0},
    {0x630, 0xfeab, 0xfeac, 0x0, 0x0},
    {0x631, 0xfead, 0xfeae, 0x0, 0x0},
    {0x632, 0xfeaf, 0xfeb0, 0x0, 0x0},
    {0x633, 0xfeb1, 0xfeb2, 0xfeb3, 0xfeb4},
    {0x634, 0xfeb5, 0xfeb6, 0xfeb7, 0xfeb8},
    {0x635, 0xfeb9, 0xfeba, 0xfebb, 0xfebc},
    {0x636, 0xfebd, 0xfebe, 0xfebf, 0xfec0},
    {0x637, 0xfec1, 0xfec2, 0xfec3, 0xfec4},
    {0x638, 0xfec5, 0xfec6, 0xfec7, 0xfec8},
    {0x639, 0xfec9, 0xfeca, 0xfecb, 0xfecc},
    {0x63a, 0xfecd, 0xfece, 0xfecf, 0xfed0},
    {0x641, 0xfed1, 0xfed2, 0xfed3, 0xfed4},
    {0x642, 0xfed5, 0xfed6, 0xfed7, 0xfed8},
    {0x643, 0xfed9, 0xfeda, 0xfedb, 0xfedc},
    {0x644, 0xfedd, 0xfede, 0xfedf, 0xfee0},
    {0x645, 0xfee1, 0xfee2, 0xfee3, 0xfee4},
    {0x646, 0xfee5, 0xfee6, 0xfee7, 0xfee8},
    {0x647, 0xfee9, 0xfeea, 0xfeeb, 0xfeec},
    {0x648, 0xfeed, 0xfeee, 0x0, 0x0},
    {0x649, 0xfeef, 0xfef0, 0xfbe8, 0xfbe9},
    {0x64a, 0xfef1, 0xfef2, 0xfef3, 0xfef4},
    {0x671, 0xfb50, 0xfb51, 0x0, 0x0},
    {0x677, 0xfbdd, 0x0, 0x0, 0x0},
    {0x679, 0xfb66, 0xfb67, 0xfb68, 0xfb69},
    {0x67a, 0xfb5e, 0xfb5f, 0xfb60, 0xfb61},
    {0x67b, 0xfb52, 0xfb53, 0xfb54, 0xfb55},
    {0x67e, 0xfb56, 0xfb57, 0xfb58, 0xfb59},
    {0x67f, 0xfb62, 0xfb63, 0xfb64, 0xfb65},
    {0x680, 0xfb5a, 0xfb5b, 0xfb5c, 0xfb5d},
    {0x683, 0xfb76, 0xfb77, 0xfb78, 0xfb79},
    {0x684, 0xfb72, 0xfb73, 0xfb74, 0xfb75},
    {0x686, 0xfb7a, 0xfb7b, 0xfb7c, 0xfb7d},
    {0x687, 0xfb7e, 0xfb7f, 0xfb80, 0xfb81},
    {0x688, 0xfb88, 0xfb89, 0x0, 0x0},
    {0x68c, 0xfb84, 0xfb85, 0x0, 0x0},
    {0x68d, 0xfb82, 0xfb83, 0x0, 0x0},
    {0x68e, 0xfb86, 0xfb87, 0x0, 0x0},
    {0x691, 0xfb8c, 0xfb8d, 0x0, 0x0},
    {0x698, 0xfb8a, 0xfb8b, 0x0, 0x0},
    {0x6a4, 0xfb6a, 0xfb6b, 0xfb6c, 0xfb6d},
    {0x6a6, 0xfb6e, 0xfb6f, 0xfb70, 0xfb71},
    {0x6a9, 0xfb8e, 0xfb8f, 0xfb90, 0xfb91},
    {0x6ad, 0xfbd3, 0xfbd4, 0xfbd5, 0xfbd6},
    {0x6af, 0xfb92, 0xfb93, 0xfb94, 0xfb95},
    {0x6b1, 0xfb9a, 0xfb9b, 0xfb9c, 0xfb9d},
    {0x6b3, 0xfb96, 0xfb97, 0xfb98, 0xfb99},
    {0x6ba, 0xfb9e, 0xfb9f, 0x0, 0x0},
    {0x6bb, 0xfba0, 0xfba1, 0xfba2, 0xfba3},
    {0x6be, 0xfbaa, 0xfbab, 0xfbac, 0xfbad},
    {0x6c0, 0xfba4, 0xfba5, 0x0, 0x0},
    {0x6c1, 0xfba6, 0xfba7, 0xfba8, 0xfba9},
    {0x6c5, 0xfbe0, 0xfbe1, 0x0, 0x0},
    {0x6c6, 0xfbd9, 0xfbda, 0x0, 0x0},
    {0x6c7, 0xfbd7, 0xfbd8, 0x0, 0x0},
    {0x6c8, 0xfbdb, 0xfbdc, 0x0, 0x0},
    {0x6c9, 0xfbe2, 0xfbe3, 0x0, 0x0},
    {0x6cb, 0xfbde, 0xfbdf, 0x0, 0x0},
    {0x6cc, 0xfbfc, 0xfbfd, 0xfbfe, 0xfbff},
    {0x6d0, 0xfbe4, 0xfbe5, 0xfbe6, 0xfbe7},
    {0x6d2, 0xfbae, 0xfbaf, 0x0, 0x0},
    {0x6d3, 0xfbb0, 0xfbb1, 0x0, 0x0},
};
//      Lam-alef: the alef it joins, then the isolated and final ligature.
static const p16 font_lam_alef[8][3] = {
    {0x622, 0xfef5, 0xfef6},
    {0x623, 0xfef7, 0xfef8},
    {0x625, 0xfef9, 0xfefa},
    {0x627, 0xfefb, 0xfefc},
};
#define FONT_LAM_ALEFS 4
/* END GENERATED */

static inline p32 font_bidi_class(p32 code)
{
        if (code < 256)
                return font_bidi_flat[code];

        //      The last range that starts at or below the code point.
        p32 low = 0, high = FONT_BIDI_RANGES;

        while (high - low > 1)
        {
                p32 middle = (low + high) / 2;

                if ((font_bidi_ranges[middle] >> 5) <= code)
                        low = middle;
                else
                        high = middle;
        }
        return font_bidi_ranges[low] & 31;
}

//      The first of count sorted values that is not below code, for the tables of pairs.
static p32 font_find_code(const p32 address_to values, p32 count, p32 code)
{
        p32 low = 0, high = count;

        while (low < high)
        {
                p32 middle = (low + high) / 2;

                if (values[middle] < code)
                        low = middle + 1;
                else
                        high = middle;
        }
        return low;
}

//      The code point whose glyph mirrors this one's, or itself.
static p32 font_mirror_of(p32 code)
{
        p32 at = font_find_code(font_mirror_code, FONT_MIRRORS, code);

        return at < FONT_MIRRORS && font_mirror_code[at] == code ? code + (p32)font_mirror_delta[at] : code;
}

//      Whether a code point is a paired bracket, its partner (the two angle brackets that decompose to the CJK ones
//      count as those) and whether it opens.
static inline p32 font_bracket_form(p32 code)
{
        return code == 0x2329 ? 0x3008 : code == 0x232a ? 0x3009 : code;
}

static p8 font_bracket(p32 code, p32 address_to partner, p8 address_to opens)
{
        p32 at = font_find_code(font_bracket_code, FONT_BRACKETS, code);

        if (at >= FONT_BRACKETS || font_bracket_code[at] != code)
                return false;
        *partner = font_bracket_form(font_bracket_pair[at] & 0x7fffffff);
        *opens = font_bracket_pair[at] >> 31;
        return true;
}

//      0 not joining, 1 left, 2 right, 3 dual (and the joining causer), 7 transparent.
static inline p32 font_join_type(p32 code)
{
        p32 low = 0, high = FONT_JOINS;

        if (code >= 0x2100 && code < 0xfe00)
                return 0;
        while (high - low > 1)
        {
                p32 middle = (low + high) / 2;

                if ((font_join_ranges[middle] >> 3) <= code)
                        low = middle;
                else
                        high = middle;
        }
        return font_join_ranges[low] & 7;
}

//      The modified combining class of a mark of the Hebrew and Arabic blocks, 0 for anything else.
static inline p32 font_mccc(p32 code)
{
        p32 low = 0, high = FONT_MCCS;

        if (code < 0x590)
                return 0;
        while (high - low > 1)
        {
                p32 middle = (low + high) / 2;

                if ((font_mccc_ranges[middle] >> 8) <= code)
                        low = middle;
                else
                        high = middle;
        }
        return font_mccc_ranges[low] & 255;
}

//      The Default_Ignorable_Code_Points a face is not asked for a glyph of (HarfBuzz's set).
static inline p8 font_ignorable(p32 code)
{
        if (code < 0x200b)
                return code == 0x34f || code == 0x61c || code == 0x17b4 || code == 0x17b5 || (code >= 0x180b && code <= 0x180f);
        if (code < 0x3000)
                return code <= 0x200f || (code >= 0x202a && code <= 0x202e) || (code >= 0x2060 && code <= 0x206f);
        return (code >= 0xfe00 && code <= 0xfe0f) || code == 0xfeff || (code >= 0xfff0 && code <= 0xfff8) ||
               (code >= 0x1d173 && code <= 0x1d17a) || (code >= 0xe0000 && code <= 0xe0fff);
}

//      Which shaper a code point's script wants: 2 Arabic, 1 the default features (Hebrew), 0 none.
static inline p32 font_script_of(p32 code)
{
        if (code < 0x590)
                return 0;
        if (code < 0x600)
                return 1;
        if (code < 0x700)
                return 2;
        if (code >= 0x750 && code < 0x780)
                return 2;
        if (code >= 0x870 && code < 0x900)
                return 2;
        if (code >= 0xfb1d && code < 0xfb50)
                return 1;
        if ((code >= 0xfb50 && code < 0xfe00) || (code >= 0xfe70 && code < 0xff00) || (code >= 0x1ee00 && code < 0x1ef00))
                return 2;
        return 0;
}

/*
        The bidirectional algorithm (UAX #9), over code points in logical
        order, to the letter and in the reference's own order: the paragraph
        level (P2, P3), explicit levels (X1-X8) to depth 125, the characters
        X9 takes out, isolating run sequences (X10), weak types (W1-W7),
        paired brackets (N0), neutrals (N1, N2), implicit levels (I1, I2),
        and L1. Reordering (L2) is font_reorder, which the units of a line
        use as well. A character X9 removes has level FONT_GONE.
*/
#define FONT_GONE 0xff

static inline p8 font_bidi_initiator(p32 type)
{
        return type == FONT_BIDI_LRI || type == FONT_BIDI_RLI || type == FONT_BIDI_FSI;
}

//      Removed by X9: the embeddings and overrides and their ends, and boundary neutrals.
static inline p8 font_bidi_removed(p32 type)
{
        return type == FONT_BIDI_LRE || type == FONT_BIDI_RLE || type == FONT_BIDI_LRO || type == FONT_BIDI_RLO ||
               type == FONT_BIDI_PDF || type == FONT_BIDI_BN;
}

//      What L1 sets to the paragraph level when it ends a line or comes before a separator.
static inline p8 font_bidi_white(p32 type)
{
        return type == FONT_BIDI_WS || font_bidi_initiator(type) || type == FONT_BIDI_PDI || font_bidi_removed(type);
}

//      Characters that N1 treats alike: the neutrals and the isolate controls.
static inline p8 font_bidi_neutral(p32 type)
{
        return type == FONT_BIDI_B || type == FONT_BIDI_S || type == FONT_BIDI_WS || type == FONT_BIDI_ON ||
               font_bidi_initiator(type) || type == FONT_BIDI_PDI;
}

//      P2 and P3 over [from, to): 0 if the first strong character not inside an isolate is left to right, 1 if
//      right to left, -1 if there is none.
static b32 font_bidi_strong(const p8 address_to orig, p32 from, p32 to)
{
        p32 depth = 0;

        for (p32 i = from; i < to; i++)
        {
                p32 type = orig[i];

                if (font_bidi_initiator(type))
                        depth++;
                else if (type == FONT_BIDI_PDI)
                        depth -= depth > 0;
                else if (!depth && type == FONT_BIDI_L)
                        return 0;
                else if (!depth && (type == FONT_BIDI_R || type == FONT_BIDI_AL))
                        return 1;
        }
        return -1;
}

//      Which direction a type counts as in N0, N1 and N2: 0 left to right, 1 right to left (numbers included),
//      -1 for neither.
static inline b32 font_bidi_side(p32 type)
{
        return type == FONT_BIDI_L ? 0 : type == FONT_BIDI_R || type == FONT_BIDI_EN || type == FONT_BIDI_AN ? 1 : -1;
}

//      N0 for the sequence in hand: w->temp holds its m types, w->seq the characters, level is its embedding level.
static fn font_bidi_brackets(font_work address_to w, p32 m, p32 sos, p32 level)
{
        struct
        {
                p32 form;
                p32 at;
        } stack[63];
        p32 depth = 0, pairs = 0, embedding = level & 1 ? FONT_BIDI_R : FONT_BIDI_L;

        //      BD16: the pairs, in the order their brackets close; a full stack ends the search.
        for (p32 k = 0; k < m; k++)
        {
                p32 partner;
                p8 opens;

                if (w->temp[k] != FONT_BIDI_ON || !font_bracket(w->code[w->seq[k]], &partner, &opens))
                        continue;
                if (opens)
                {
                        if (depth == 63)
                                break;
                        stack[depth].form = partner;
                        stack[depth++].at = k;
                        continue;
                }
                for (p32 d = depth; d > 0; d--)
                        if (stack[d - 1].form == font_bracket_form(w->code[w->seq[k]]))
                        {
                                w->pair[2 * pairs] = (p16)stack[d - 1].at;
                                w->pair[2 * pairs + 1] = (p16)k;
                                pairs++;
                                depth = d - 1;
                                break;
                        }
        }

        //      In the order their brackets open.
        for (p32 a = 1; a < pairs; a++)
        {
                p16 open = w->pair[2 * a], close = w->pair[2 * a + 1];
                p32 b = a;

                for (; b > 0 && w->pair[2 * (b - 1)] > open; b--)
                {
                        w->pair[2 * b] = w->pair[2 * (b - 1)];
                        w->pair[2 * b + 1] = w->pair[2 * (b - 1) + 1];
                }
                w->pair[2 * b] = open;
                w->pair[2 * b + 1] = close;
        }

        for (p32 a = 0; a < pairs; a++)
        {
                p32 open = w->pair[2 * a], close = w->pair[2 * a + 1], found = FONT_BIDI_ON, opposite = 0, as;

                for (p32 k = open + 1; k < close; k++)
                {
                        b32 side = font_bidi_side(w->temp[k]);

                        if (side < 0)
                                continue;
                        if ((side ? FONT_BIDI_R : FONT_BIDI_L) == embedding)
                        {
                                found = embedding;
                                break;
                        }
                        opposite = 1;
                }
                if (found == FONT_BIDI_ON && !opposite)
                        continue;
                if (found == FONT_BIDI_ON)
                {
                        //      Only the other direction inside: it decides if what comes before it is that too.
                        p32 before = sos;

                        for (p32 k = open; k > 0; k--)
                                if (font_bidi_side(w->temp[k - 1]) >= 0)
                                {
                                        before = font_bidi_side(w->temp[k - 1]) ? FONT_BIDI_R : FONT_BIDI_L;
                                        break;
                                }
                        found = before != embedding ? before : embedding;
                }
                as = found;
                w->temp[open] = w->temp[close] = (p8)as;

                //      A mark that followed a bracket (it was a nonspacing mark) takes the bracket's type.
                for (p32 k = open + 1; k < m && w->orig[w->seq[k]] == FONT_BIDI_NSM; k++)
                        w->temp[k] = (p8)as;
                for (p32 k = close + 1; k < m && w->orig[w->seq[k]] == FONT_BIDI_NSM; k++)
                        w->temp[k] = (p8)as;
        }
}

//      One isolating run sequence: W1 to W7, N0 to N2 and I1, I2.
static fn font_bidi_sequence(font_work address_to w, p32 m, p32 sos, p32 eos, p32 level)
{
        p8 address_to t = w->temp;
        p32 last = sos, embedding = level & 1 ? FONT_BIDI_R : FONT_BIDI_L;

        for (p32 k = 0; k < m; k++)
                t[k] = w->type[w->seq[k]];

        //      W1: a nonspacing mark takes the type before it, or ON after an isolate control.
        for (p32 k = 0; k < m; k++)
                if (t[k] == FONT_BIDI_NSM)
                        t[k] = (p8)(k ? (font_bidi_initiator(t[k - 1]) || t[k - 1] == FONT_BIDI_PDI ? FONT_BIDI_ON : t[k - 1])
                                      : sos);

        //      W2: a European number after an Arabic letter is an Arabic number. W3: Arabic letters are R.
        for (p32 k = 0; k < m; k++)
        {
                if (t[k] == FONT_BIDI_L || t[k] == FONT_BIDI_R || t[k] == FONT_BIDI_AL)
                        last = t[k];
                else if (t[k] == FONT_BIDI_EN && last == FONT_BIDI_AL)
                        t[k] = FONT_BIDI_AN;
        }
        for (p32 k = 0; k < m; k++)
                if (t[k] == FONT_BIDI_AL)
                        t[k] = FONT_BIDI_R;

        //      W4: a single separator between two numbers of one kind.
        for (p32 k = 1; k + 1 < m; k++)
        {
                if (t[k] == FONT_BIDI_ES && t[k - 1] == FONT_BIDI_EN && t[k + 1] == FONT_BIDI_EN)
                        t[k] = FONT_BIDI_EN;
                else if (t[k] == FONT_BIDI_CS && t[k - 1] == FONT_BIDI_EN && t[k + 1] == FONT_BIDI_EN)
                        t[k] = FONT_BIDI_EN;
                else if (t[k] == FONT_BIDI_CS && t[k - 1] == FONT_BIDI_AN && t[k + 1] == FONT_BIDI_AN)
                        t[k] = FONT_BIDI_AN;
        }

        //      W5: terminators next to a European number are European numbers. W6: the rest are neutral.
        for (p32 k = 0; k < m;)
        {
                if (t[k] != FONT_BIDI_ET)
                {
                        k++;
                        continue;
                }
                p32 stop = k;

                while (stop < m && t[stop] == FONT_BIDI_ET)
                        stop++;
                if ((k && t[k - 1] == FONT_BIDI_EN) || (stop < m && t[stop] == FONT_BIDI_EN))
                        for (p32 j = k; j < stop; j++)
                                t[j] = FONT_BIDI_EN;
                k = stop;
        }
        for (p32 k = 0; k < m; k++)
                if (t[k] == FONT_BIDI_ES || t[k] == FONT_BIDI_ET || t[k] == FONT_BIDI_CS)
                        t[k] = FONT_BIDI_ON;

        //      W7: a European number after left to right text is left to right.
        last = sos;
        for (p32 k = 0; k < m; k++)
        {
                if (t[k] == FONT_BIDI_L || t[k] == FONT_BIDI_R)
                        last = t[k];
                else if (t[k] == FONT_BIDI_EN && last == FONT_BIDI_L)
                        t[k] = FONT_BIDI_L;
        }

        font_bidi_brackets(w, m, sos, level);

        //      N1, N2: a run of neutrals between two of one direction is that, else the embedding's.
        for (p32 k = 0; k < m;)
        {
                if (!font_bidi_neutral(t[k]))
                {
                        k++;
                        continue;
                }
                p32 stop = k;

                while (stop < m && font_bidi_neutral(t[stop]))
                        stop++;

                b32 before = k ? font_bidi_side(t[k - 1]) : sos == FONT_BIDI_R;
                b32 after = stop < m ? font_bidi_side(t[stop]) : eos == FONT_BIDI_R;
                p32 as = before >= 0 && before == after ? (before ? FONT_BIDI_R : FONT_BIDI_L) : embedding;

                for (p32 j = k; j < stop; j++)
                        t[j] = (p8)as;
                k = stop;
        }

        //      I1, I2.
        for (p32 k = 0; k < m; k++)
        {
                p32 at = w->seq[k], up = level & 1 ? (t[k] == FONT_BIDI_L || t[k] == FONT_BIDI_EN || t[k] == FONT_BIDI_AN)
                                         : t[k] == FONT_BIDI_R ? 1 : t[k] == FONT_BIDI_AN || t[k] == FONT_BIDI_EN ? 2 : 0;

                w->level[at] = (p8)(level + up);
        }
}

/*
        Levels for the n code points in w->code, by direction: 0 left to
        right, 1 right to left, 2 the first strong character with left to
        right where there is none, 3 the same with right to left. Answers
        the paragraph level; w->level has each character's, FONT_GONE for
        one X9 removed.
*/
static p32 font_bidi(font_work address_to w, p32 n, p32 direction)
{
        struct
        {
                p8 level, override, isolate;
        } stack[FONT_STACK + 2];
        p32 depth = 0, overflow_isolates = 0, overflow_embeddings = 0, valid_isolates = 0, para, runs = 0, flat = 0;

        for (p32 i = 0; i < n; i++)
                w->orig[i] = w->type[i] = (p8)font_bidi_class(w->code[i]);

        //      Isolate initiators and the PDIs that close them, by nesting alone.
        {
                p32 open = 0;

                for (p32 i = 0; i < n; i++)
                {
                        w->match[i] = (p16)n;
                        if (font_bidi_initiator(w->orig[i]))
                                w->seq[open++] = (p16)i;
                        else if (w->orig[i] == FONT_BIDI_PDI && open)
                        {
                                p32 from = w->seq[--open];

                                w->match[from] = (p16)i;
                                w->match[i] = (p16)from;
                        }
                }
        }

        //      P2, P3.
        if (direction < 2)
                para = direction;
        else
        {
                b32 strong = font_bidi_strong(w->orig, 0, n);

                para = strong < 0 ? direction - 2 : (p32)strong;
        }

        //      X1 to X8.
        stack[0].level = (p8)para;
        stack[0].override = FONT_BIDI_ON;
        stack[0].isolate = 0;
        for (p32 i = 0; i < n; i++)
        {
                p32 type = w->orig[i];

                if (type == FONT_BIDI_RLE || type == FONT_BIDI_LRE || type == FONT_BIDI_RLO || type == FONT_BIDI_LRO ||
                    font_bidi_initiator(type))
                {
                        p8 isolate = font_bidi_initiator(type);
                        p8 right = type == FONT_BIDI_RLE || type == FONT_BIDI_RLO || type == FONT_BIDI_RLI;
                        p32 level;

                        if (type == FONT_BIDI_FSI)
                                right = font_bidi_strong(w->orig, i + 1, w->match[i]) == 1;
                        w->level[i] = stack[depth].level;
                        if (isolate && stack[depth].override != FONT_BIDI_ON)
                                w->type[i] = stack[depth].override;
                        level = right ? ((p32)stack[depth].level + 1) | 1 : ((p32)stack[depth].level + 2) & ~1u;
                        if (level <= FONT_STACK && !overflow_isolates && !overflow_embeddings)
                        {
                                valid_isolates += isolate;
                                depth++;
                                stack[depth].level = (p8)level;
                                stack[depth].override = type == FONT_BIDI_LRO ? FONT_BIDI_L : type == FONT_BIDI_RLO ? FONT_BIDI_R : FONT_BIDI_ON;
                                stack[depth].isolate = isolate;
                                if (!isolate)
                                        w->level[i] = (p8)level;
                        }
                        else if (isolate)
                                overflow_isolates++;
                        else if (!overflow_isolates)
                                overflow_embeddings++;
                }
                else if (type == FONT_BIDI_PDI)
                {
                        if (overflow_isolates)
                                overflow_isolates--;
                        else if (valid_isolates)
                        {
                                overflow_embeddings = 0;
                                while (!stack[depth].isolate)
                                        depth--;
                                depth--;
                                valid_isolates--;
                        }
                        w->level[i] = stack[depth].level;
                        if (stack[depth].override != FONT_BIDI_ON)
                                w->type[i] = stack[depth].override;
                }
                else if (type == FONT_BIDI_PDF)
                {
                        w->level[i] = stack[depth].level;
                        if (overflow_isolates)
                                ;
                        else if (overflow_embeddings)
                                overflow_embeddings--;
                        else if (!stack[depth].isolate && depth >= 1)
                                depth--;
                }
                else if (type == FONT_BIDI_B)
                        w->level[i] = (p8)para;
                else
                {
                        w->level[i] = stack[depth].level;
                        if (stack[depth].override != FONT_BIDI_ON)
                                w->type[i] = stack[depth].override;
                }
        }

        //      X9, X10: the level runs of what is left, and the isolating run sequences they chain into. The levels
        //      of the explicit rules are kept, for the sequences are cut from them and not from the levels the
        //      implicit rules give.
        for (p32 i = 0; i < n; i++)
                w->base[i] = w->level[i];
        for (p32 i = 0; i < n; i++)
        {
                if (font_bidi_removed(w->orig[i]))
                        continue;
                if (!flat || w->base[w->flat[flat - 1]] != w->base[i])
                        w->run_start[runs++] = (p16)flat;
                w->run_of[i] = (p16)(runs - 1);
                w->flat[flat++] = (p16)i;
        }
        w->run_start[runs] = (p16)flat;

        for (p32 r = 0; r < runs; r++)
        {
                p32 first = w->flat[w->run_start[r]], m = 0, run = r;

                if (w->orig[first] == FONT_BIDI_PDI && w->match[first] != n)
                        continue;
                for (;;)
                {
                        for (p32 k = w->run_start[run]; k < w->run_start[run + 1]; k++)
                                w->seq[m++] = w->flat[k];

                        p32 tail = w->seq[m - 1];

                        if (font_bidi_initiator(w->orig[tail]) && w->match[tail] != n)
                        {
                                run = w->run_of[w->match[tail]];
                                continue;
                        }
                        break;
                }

                p32 level = w->base[first], before = para, after = para, tail = w->seq[m - 1];

                if (w->run_start[r])
                        before = w->base[w->flat[w->run_start[r] - 1]];
                if (!font_bidi_initiator(w->orig[tail]) && w->run_start[w->run_of[tail] + 1] < flat)
                        after = w->base[w->flat[w->run_start[w->run_of[tail] + 1]]];

                font_bidi_sequence(w, m, (level > before ? level : before) & 1 ? FONT_BIDI_R : FONT_BIDI_L,
                                   (level > after ? level : after) & 1 ? FONT_BIDI_R : FONT_BIDI_L, level);
        }

        //      L1: separators, and whitespace before them and at the end, go to the paragraph level.
        for (p32 i = 0; i < n; i++)
                if (w->orig[i] == FONT_BIDI_S || w->orig[i] == FONT_BIDI_B)
                {
                        w->level[i] = (p8)para;
                        for (p32 j = i; j > 0 && font_bidi_white(w->orig[j - 1]); j--)
                                w->level[j - 1] = (p8)para;
                }
        for (p32 j = n; j > 0 && font_bidi_white(w->orig[j - 1]); j--)
                w->level[j - 1] = (p8)para;
        for (p32 i = 0; i < n; i++)
                if (font_bidi_removed(w->orig[i]))
                        w->level[i] = FONT_GONE;
        return para;
}

/*
        L2 for a row of levels: order[] gets the indices of the entries not
        FONT_GONE in the order they read on a line, left to right. Reverses
        from the highest level down to the lowest odd one every run at or
        above it.
*/
static p32 font_reorder(const p8 address_to levels, p32 n, p16 address_to order)
{
        p32 count = 0, high = 0, low = 255;

        for (p32 i = 0; i < n; i++)
                if (levels[i] != FONT_GONE)
                {
                        order[count++] = (p16)i;
                        high = levels[i] > high ? levels[i] : high;
                        low = (levels[i] & 1) && levels[i] < low ? levels[i] : low;
                }
        for (p32 level = high; level >= low && level > 0; level--)
                for (p32 k = 0; k < count;)
                {
                        if (levels[order[k]] < level)
                        {
                                k++;
                                continue;
                        }
                        p32 stop = k;

                        while (stop < count && levels[order[stop]] >= level)
                                stop++;
                        for (p32 a = k, b = stop - 1; a < b; a++, b--)
                        {
                                p16 swap = order[a];

                                order[a] = order[b];
                                order[b] = swap;
                        }
                        k = stop;
                }
        return count;
}

/*
        The face's layout tables: GDEF's glyph classes and mark sets, GSUB's
        lookups 1 to 8 and GPOS's 1 to 9 (device tables ignored), applied
        as HarfBuzz applies them, because that is what the fonts are made
        and tested against: a buffer of glyphs read forward with an output
        behind it, every lookup flag and mark filtering set, the skipping of
        what a lookup ignores, ligature components kept so a mark finds the
        part of the ligature it belongs to, and a rule's nested lookups
        run on the glyphs a rule matched, the match shifting as they add and
        take glyphs away. The tables are the file's, and so every read is
        bounded and every count is cut to a ceiling: the lookups a plan
        runs, a rule's context, the depth rules call each other to, the
        glyphs a buffer holds, and the work a line may do altogether.
*/
typedef struct
{
        const font_face address_to face;
        font_item address_to info, address_to out, address_to spare;
        font_shift address_to shift;
        p32 len, out_len, idx, cap, table, lookup_mask, lookup_props, nesting, serial;
        positive lookups;
        b64 ops;
        p8 output, failed, auto_zwj, auto_zwnj, rtl, classes, attached;
} font_shaper;

//      What a rule's context element is matched with: a glyph, a class of a class definition, or a coverage.
typedef struct
{
        p32 kind;
        positive array, base;
} font_matcher;

enum { FONT_BY_GLYPH, FONT_BY_CLASS, FONT_BY_COVERAGE };
enum { FONT_ACT_ISOL, FONT_ACT_FINA, FONT_ACT_FIN2, FONT_ACT_FIN3, FONT_ACT_MEDI, FONT_ACT_MED2, FONT_ACT_INIT, FONT_ACT_NONE };

//      GDEF: a glyph's class (0 none, 1 base, 2 ligature, 3 mark, 4 component) and a mark's attachment class.
static inline p32 font_gdef_class(const font_face address_to face, p32 glyph)
{
        positive table = face->gdef, at = table ? font_u16(face, table + 4) : 0;

        return at ? font_class(face, table + at, glyph) : 0;
}

static inline p8 font_gdef_classes(const font_face address_to face)
{
        return face->gdef && font_u16(face, face->gdef + 4);
}

static inline p32 font_gdef_attachment(const font_face address_to face, p32 glyph)
{
        positive table = face->gdef, at = table ? font_u16(face, table + 10) : 0;

        return at ? font_class(face, table + at, glyph) : 0;
}

static p8 font_gdef_mark_set(const font_face address_to face, p32 set, p32 glyph)
{
        positive table = face->gdef, at = table && font_u32(face, table) >= 0x00010002 ? font_u16(face, table + 12) : 0;

        if (!at || font_u16(face, table + at) != 1 || set >= font_u16(face, table + at + 2))
                return false;

        positive coverage = font_u32(face, table + at + 4 + 4 * (positive)set);

        return coverage && font_coverage(face, table + at + coverage, glyph) >= 0;
}

//      The props a glyph starts with: its GDEF class, a mark's attachment class above it.
static inline p32 font_glyph_props(const font_face address_to face, p32 glyph)
{
        p32 klass = font_gdef_class(face, glyph);

        return klass == 1 ? 2 : klass == 2 ? 4 : klass == 3 ? 8 | font_gdef_attachment(face, glyph) << 8 : 0;
}

#define FONT_PROPS_MARK 8
#define FONT_PROPS_KEEP (16 | 32 | 64)

static inline p32 font_lig_id(const font_item address_to it)
{
        return it->lig >> 5;
}

static inline p32 font_lig_comp(const font_item address_to it)
{
        return it->lig & 0x10 ? 0 : it->lig & 0x0f;
}

static inline p32 font_lig_count(const font_item address_to it)
{
        return it->props & 4 && it->lig & 0x10 ? it->lig & 0x0f : 1;
}

//      A glyph gets a new class when what it is changes: a substituted one takes its GDEF class again, or the guess.
static void font_set_class(font_shaper address_to s, font_item address_to it, p32 glyph, p32 guess, p8 ligature,
                           p8 component)
{
        p32 props = it->props | 16;

        if (ligature)
                props = (props | 32) & ~(p32)64;
        if (component)
                props |= 64;
        if (s->classes)
                props = (props & FONT_PROPS_KEEP) | font_glyph_props(s->face, glyph);
        else if (guess)
                props = (props & FONT_PROPS_KEEP) | guess;
        it->props = (p16)props;
}

/*
        The buffer. Glyphs are read from info at idx; a lookup that changes
        the string writes behind it into out, and what it has not reached is
        copied over when it is done, so a lookup is a pass and not a
        shuffle. A rule that calls another lookup moves back and forth over
        the divide.
*/
static inline p8 font_buffer_room(font_shaper address_to s, p32 more)
{
        if (s->out_len + more > s->cap || s->len + more > s->cap)
                s->failed = true;
        return !s->failed;
}

static void font_next_glyph(font_shaper address_to s)
{
        if (s->output && font_buffer_room(s, 1))
                s->out[s->out_len++] = s->info[s->idx];
        s->idx++;
}

static void font_skip_glyph(font_shaper address_to s)
{
        s->idx++;
}

//      The current glyph written to the output as glyph, in the place the string is read from.
static void font_output_glyph(font_shaper address_to s, p32 glyph)
{
        if (font_buffer_room(s, 1))
        {
                s->out[s->out_len] = s->info[s->idx];
                s->out[s->out_len++].glyph = glyph;
        }
}

static void font_replace_glyph(font_shaper address_to s, p32 glyph)
{
        if (s->output)
                font_output_glyph(s, glyph);
        else
                s->info[s->idx].glyph = glyph;
        s->idx++;
}

static inline p32 font_behind(const font_shaper address_to s)
{
        return s->output ? s->out_len : s->idx;
}

static void font_clear_output(font_shaper address_to s)
{
        s->output = true;
        s->out_len = s->idx = 0;
        s->out = s->spare;
}

//      Everything not reached copied over, and the output made the string.
static void font_sync(font_shaper address_to s)
{
        font_item address_to old = s->info;

        while (s->idx < s->len && !s->failed)
                font_next_glyph(s);
        if (!s->failed)
        {
                s->info = s->out;
                s->spare = old;
                s->len = s->out_len;
        }
        s->output = false;
        s->out_len = s->idx = 0;
        s->out = s->info;
}

//      The cursor to position i of what is behind it and what is ahead, moving glyphs between the two.
static void font_move_to(font_shaper address_to s, p32 i)
{
        if (!s->output)
        {
                s->idx = i;
                return;
        }
        if (s->out_len < i)
        {
                p32 count = i - s->out_len;

                if (!font_buffer_room(s, count) || s->idx + count > s->len)
                        return;
                memory_copy(s->out + s->out_len, s->info + s->idx, count * sizeof(font_item));
                s->idx += count;
                s->out_len += count;
        }
        else if (s->out_len > i)
        {
                p32 count = s->out_len - i;

                if (s->idx < count)
                {
                        p32 shift = count - s->idx;

                        if (s->len + shift > s->cap)
                        {
                                s->failed = true;
                                return;
                        }
                        memory_copy(s->info + s->idx + shift, s->info + s->idx, (s->len - s->idx) * sizeof(font_item));
                        s->len += shift;
                        s->idx += shift;
                }
                s->idx -= count;
                s->out_len -= count;
                memory_copy(s->info + s->idx, s->out + s->out_len, count * sizeof(font_item));
        }
}

//      The clusters of [from, to) made one, the lowest.
static void font_merge(font_shaper address_to s, p32 from, p32 to)
{
        p32 low;

        if (to > s->len)
                to = s->len;
        if (to < from + 2)
                return;
        low = s->info[from].cluster;
        for (p32 i = from + 1; i < to; i++)
                low = s->info[i].cluster < low ? s->info[i].cluster : low;
        for (p32 i = from; i < to; i++)
                s->info[i].cluster = low;
}

/*
        What a lookup skips, and which glyphs the next one of a rule is
        looked for among. Whether a glyph is let through is the lookup flag
        (ignore base glyphs, ligatures, marks; a mark attachment class; a
        mark glyph set) against its GDEF class. Zero width joiners and
        non-joiners and the other default ignorables are skipped when they
        do not match, as far as the lookup says so.
*/
static p8 font_let_through(const font_shaper address_to s, const font_item address_to it, p32 props)
{
        p32 glyph_props = it->props;

        if (glyph_props & props & 0xe)
                return false;
        if (glyph_props & FONT_PROPS_MARK)
        {
                if (props & 0x10)
                        return font_gdef_mark_set(s->face, props >> 16, it->glyph);
                if (props & 0xff00)
                        return (props & 0xff00) == (glyph_props & 0xff00);
        }
        return true;
}

//      0 not skipped, 1 skipped, 2 skipped if it is not what is looked for.
static p32 font_skip_of(const font_shaper address_to s, const font_item address_to it, p8 context, p32 props)
{
        p8 zwnj = context && s->auto_zwnj, zwj = context || s->auto_zwj;

        if (!font_let_through(s, it, props))
                return 1;
        if ((it->unicode & 1) && (zwnj || !(it->unicode & 4)) && (zwj || !(it->unicode & 2)))
                return 2;
        return 0;
}

static p8 font_matches(const font_shaper address_to s, const font_matcher address_to m, p32 i, p32 glyph)
{
        positive at = m->array + 2 * (positive)i;

        switch (m->kind)
        {
        case FONT_BY_GLYPH:
                return font_u16(s->face, at) == glyph;
        case FONT_BY_CLASS:
                return font_class(s->face, m->base, glyph) == font_u16(s->face, at);
        default:
        {
                positive coverage = font_u16(s->face, at);

                return coverage && font_coverage(s->face, m->base + coverage, glyph) >= 0;
        }
        }
}

//      The glyph after *at that is the i'th of a rule, or any glyph the lookup keeps with no matcher, in info.
static p8 font_scan_next(font_shaper address_to s, const font_matcher address_to m, p32 i, p8 context, p32 address_to at,
                           p32 props)
{
        for (p32 k = *at + 1; k < s->len; k++)
        {
                const font_item address_to it = &s->info[k];
                p32 skip;
                p8 wanted = context || (it->mask & s->lookup_mask) != 0;

                if (s->ops-- <= 0)
                {
                        s->failed = true;
                        return false;
                }
                skip = font_skip_of(s, it, context, props);
                if (skip == 1)
                        continue;
                if (wanted && (m ? font_matches(s, m, i, it->glyph) : skip == 0))
                {
                        *at = k;
                        return true;
                }
                if (skip == 0)
                        return false;
        }
        return false;
}

//      The same going back through what is behind the cursor.
static p8 font_scan_prev(font_shaper address_to s, const font_matcher address_to m, p32 i, p8 context, p32 address_to at,
                           p32 props)
{
        for (p32 k = *at; k > 0; k--)
        {
                const font_item address_to it = &s->out[k - 1];
                p32 skip;
                p8 wanted = context || (it->mask & s->lookup_mask) != 0;

                if (s->ops-- <= 0)
                {
                        s->failed = true;
                        return false;
                }
                skip = font_skip_of(s, it, context, props);
                if (skip == 1)
                        continue;
                if (wanted && (m ? font_matches(s, m, i, it->glyph) : skip == 0))
                {
                        *at = k - 1;
                        return true;
                }
                if (skip == 0)
                        return false;
        }
        return false;
}

//      The count glyphs of a rule's input, the current one first: where each is, where the match ends, and how many
//      ligature components it spans. Glyphs attached to different components of a ligature already made do not match.
static p8 font_match_input(font_shaper address_to s, p32 count, const font_matcher address_to m, p32 address_to tail,
                             p32 address_to positions, p32 address_to total)
{
        const font_item address_to cur = &s->info[s->idx];
        p32 first_id = font_lig_id(cur), first_comp = font_lig_comp(cur), at = s->idx, components = font_lig_count(cur),
            ligbase = 0;

        if (count > FONT_MATCH || count == 0)
                return false;
        positions[0] = s->idx;
        for (p32 i = 1; i < count; i++)
        {
                if (!font_scan_next(s, m, i - 1, false, &at, s->lookup_props))
                {
                        *tail = at + 1;
                        return false;
                }
                positions[i] = at;

                p32 this_id = font_lig_id(&s->info[at]), this_comp = font_lig_comp(&s->info[at]);

                if (first_id && first_comp)
                {
                        if (first_id != this_id || first_comp != this_comp)
                        {
                                if (!ligbase)
                                {
                                        p32 j = s->out_len;
                                        p8 found = false;

                                        if (!s->output)
                                                j = 0;
                                        while (j && font_lig_id(&s->out[j - 1]) == first_id)
                                        {
                                                if (font_lig_comp(&s->out[j - 1]) == 0)
                                                {
                                                        j--;
                                                        found = true;
                                                        break;
                                                }
                                                j--;
                                        }
                                        ligbase = found && font_skip_of(s, &s->out[j], false, s->lookup_props) == 1 ? 2 : 1;
                                }
                                if (ligbase == 1)
                                        return false;
                        }
                }
                else if (this_id && this_comp && this_id != first_id)
                        return false;
                components += font_lig_count(&s->info[at]);
        }
        *tail = at + 1;
        if (total)
                *total = components;
        return true;
}

//      The count glyphs after position start, each wanted by the matcher; where the match stops.
static p8 font_match_ahead(font_shaper address_to s, p32 count, const font_matcher address_to m, p32 start,
                             p32 address_to stop)
{
        p32 at = start - 1;

        if (count > FONT_MATCH)
                return false;
        for (p32 i = 0; i < count; i++)
                if (!font_scan_next(s, m, i, true, &at, s->lookup_props))
                        return false;
        *stop = at + 1;
        return true;
}

//      And the count glyphs behind the cursor, nearest first.
static p8 font_match_behind(font_shaper address_to s, p32 count, const font_matcher address_to m)
{
        p32 at = font_behind(s);

        if (count > FONT_MATCH)
                return false;
        for (p32 i = 0; i < count; i++)
                if (!font_scan_prev(s, m, i, true, &at, s->lookup_props))
                        return false;
        return true;
}

static p8 font_apply_nested(font_shaper address_to s, p32 index);

/*
        A rule matched at positions[0..count): its lookup records (a place in
        the match, a lookup) run, each at the glyph that place is now at. A
        lookup that adds glyphs puts them after that place and the places
        after it move on; one that takes glyphs away takes places from
        after it. Ends with the cursor after the match, wherever it grew to.
*/
static void font_apply_records(font_shaper address_to s, p32 count, p32 address_to positions, positive records, p32 n,
                               p32 tail)
{
        b32 stop, delta = (b32)font_behind(s) - (b32)s->idx;

        stop = (b32)font_behind(s) + (b32)tail - (b32)s->idx;
        for (p32 j = 0; j < count; j++)
                positions[j] = (p32)((b32)positions[j] + delta);

        for (p32 i = 0; i < n && !s->failed; i++)
        {
                p32 at = font_u16(s->face, records + 4 * (positive)i), lookup = font_u16(s->face, records + 4 * (positive)i + 2);
                p32 before = font_behind(s) + (s->len - s->idx);
                b32 grew;

                if (at >= count || positions[at] >= before)
                        continue;
                font_move_to(s, positions[at]);
                if (s->ops-- <= 0 || s->failed)
                        break;
                if (!font_apply_nested(s, lookup))
                        continue;

                grew = (b32)(font_behind(s) + (s->len - s->idx)) - (b32)before;
                if (!grew)
                        continue;
                stop += grew;
                if (stop < (b32)positions[at])
                {
                        grew += (b32)positions[at] - stop;
                        stop = (b32)positions[at];
                }

                p32 next = at + 1;

                if (grew > 0)
                {
                        if ((p32)grew + count > FONT_MATCH)
                                break;
                }
                else
                {
                        grew = grew > (b32)next - (b32)count ? grew : (b32)next - (b32)count;
                        next = (p32)((b32)next - grew);
                }
                memory_copy(positions + next + grew, positions + next, (count - next) * sizeof(p32));
                next = (p32)((b32)next + grew);
                count = (p32)((b32)count + grew);
                for (p32 j = at + 1; j < next; j++)
                        positions[j] = positions[j - 1] + 1;
                for (; next < count; next++)
                        positions[next] = (p32)((b32)positions[next] + grew);
        }
        font_move_to(s, (p32)stop);
}

//      A context rule: its input matched, then its records.
static p8 font_rule(font_shaper address_to s, p32 count, const font_matcher address_to input, positive records, p32 n)
{
        p32 positions[FONT_MATCH], tail;

        if (!font_match_input(s, count, input, &tail, positions, null))
                return false;
        font_apply_records(s, count, positions, records, n, tail);
        return true;
}

//      A chain rule: input and what follows it, then what comes before.
static p8 font_chain_rule(font_shaper address_to s, const font_matcher address_to behind, p32 back_count,
                       const font_matcher address_to input, p32 count, const font_matcher address_to ahead, p32 ahead_count,
                       positive records, p32 n)
{
        p32 positions[FONT_MATCH], tail, stop;

        if (!font_match_input(s, count, input, &tail, positions, null) ||
            !font_match_ahead(s, ahead_count, ahead, tail, &stop) || !font_match_behind(s, back_count, behind))
                return false;
        font_apply_records(s, count, positions, records, n, tail);
        return true;
}

/*
        GSUB. One subtable applied at the glyph the cursor is on: true when
        it substituted, with the cursor past what it consumed.
*/
static p8 font_ligature(font_shaper address_to s, positive set, p32 index)
{
        const font_face address_to face = s->face;
        positive ligature = set + font_u16(face, set + 2 + 2 * (positive)index);
        p32 glyph = font_u16(face, ligature), count = font_u16(face, ligature + 2);
        p32 positions[FONT_MATCH], tail, total = 0;
        font_matcher input = {FONT_BY_GLYPH, ligature + 4, 0};

        if (!count)
                return false;
        if (count == 1)
        {
                font_set_class(s, &s->info[s->idx], glyph, 0, false, false);
                font_replace_glyph(s, glyph);
                return true;
        }
        if (!font_match_input(s, count, &input, &tail, positions, &total))
                return false;

        /*      The ligature of a base and marks is a base, and one of marks only keeps its id; any other is a new
                ligature. The marks between the components are given the components they were attached to. */
        font_item address_to first = &s->info[positions[0]];
        p8 base = (first->props & 2) != 0, mark = (first->props & FONT_PROPS_MARK) != 0;

        font_merge(s, s->idx, tail);
        for (p32 i = 1; i < count; i++)
                if (!(s->info[positions[i]].props & FONT_PROPS_MARK))
                {
                        base = false;
                        mark = false;
                        break;
                }

        p8 made = !base && !mark;
        p32 id = 0, last_id = font_lig_id(&s->info[s->idx]), last_count = font_lig_count(&s->info[s->idx]), so_far;

        if (made)
        {
                id = ++s->serial & 7;
                if (!id)
                        id = ++s->serial & 7;
        }
        so_far = last_count;
        if (made)
                s->info[s->idx].lig = (p8)(id << 5 | 0x10 | (total & 0x0f));
        font_set_class(s, &s->info[s->idx], glyph, made ? 4 : 0, true, false);
        font_replace_glyph(s, glyph);
        for (p32 i = 1; i < count; i++)
        {
                while (s->idx < positions[i] && !s->failed)
                {
                        if (made)
                        {
                                p32 comp = font_lig_comp(&s->info[s->idx]);

                                if (!comp)
                                        comp = last_count;
                                s->info[s->idx].lig = (p8)(id << 5 | ((so_far - last_count + (comp < last_count ? comp : last_count)) & 0x0f));
                        }
                        font_next_glyph(s);
                }
                last_id = font_lig_id(&s->info[s->idx]);
                last_count = font_lig_count(&s->info[s->idx]);
                so_far += last_count;
                s->idx++;
        }
        if (made)
                for (p32 i = s->idx; i < s->len; i++)
                {
                        p32 comp = font_lig_comp(&s->info[i]);

                        if (last_id != font_lig_id(&s->info[i]) || !comp)
                                break;
                        s->info[i].lig = (p8)(id << 5 | ((so_far - last_count + (comp < last_count ? comp : last_count)) & 0x0f));
                }
        return true;
}

static p8 font_gsub_apply(font_shaper address_to s, p32 kind, positive t)
{
        const font_face address_to face = s->face;
        font_item address_to cur = &s->info[s->idx];
        p32 glyph = cur->glyph, format = font_u16(face, t);
        b32 ci;

        switch (kind)
        {
        case 1:
        {
                positive coverage = t + font_u16(face, t + 2);
                p32 to;

                if ((ci = font_coverage(face, coverage, glyph)) < 0)
                        return false;
                if (format == 1)
                        to = (glyph + (p32)(b16)font_u16(face, t + 4)) & 0xffff;
                else if (format == 2 && (p32)ci < font_u16(face, t + 4))
                        to = font_u16(face, t + 6 + 2 * (positive)ci);
                else
                        return false;
                font_set_class(s, cur, to, 0, false, false);
                font_replace_glyph(s, to);
                return true;
        }
        case 2:
        case 3:
        {
                positive coverage = t + font_u16(face, t + 2);
                positive set;
                p32 count;

                if (format != 1 || (ci = font_coverage(face, coverage, glyph)) < 0 || (p32)ci >= font_u16(face, t + 4))
                        return false;
                set = t + font_u16(face, t + 6 + 2 * (positive)ci);
                count = font_u16(face, set);
                if (kind == 3)
                {
                        //      The first alternate: the feature's value is 1.
                        if (!count)
                                return false;
                        p32 to = font_u16(face, set + 2);

                        font_set_class(s, cur, to, 0, false, false);
                        font_replace_glyph(s, to);
                        return true;
                }
                if (!count)
                {
                        font_skip_glyph(s);
                        return true;
                }
                if (count == 1)
                {
                        p32 to = font_u16(face, set + 2);

                        font_set_class(s, cur, to, 0, false, false);
                        font_replace_glyph(s, to);
                        return true;
                }

                p32 klass = cur->props & 4 ? 2 : 0, id = font_lig_id(cur);

                if (count > 255)
                        count = 255;
                for (p32 i = 0; i < count && !s->failed; i++)
                {
                        if (!id)
                                cur->lig = (p8)(i & 0x0f);
                        font_set_class(s, cur, font_u16(face, set + 2 + 2 * (positive)i), klass, false, true);
                        font_output_glyph(s, font_u16(face, set + 2 + 2 * (positive)i));
                }
                font_skip_glyph(s);
                return true;
        }
        case 4:
        {
                positive coverage = t + font_u16(face, t + 2);
                positive set;
                p32 count;

                if (format != 1 || (ci = font_coverage(face, coverage, glyph)) < 0 || (p32)ci >= font_u16(face, t + 4))
                        return false;
                set = t + font_u16(face, t + 6 + 2 * (positive)ci);
                count = font_u16(face, set);
                for (p32 i = 0; i < count && i < 4096; i++)
                        if (font_ligature(s, set, i))
                                return true;
                return false;
        }
        case 5:
        {
                positive coverage;
                positive set;
                p32 count;

                if (format == 3)
                {
                        p32 glyphs = font_u16(face, t + 2), n = font_u16(face, t + 4);
                        font_matcher input = {FONT_BY_COVERAGE, t + 8, t};

                        coverage = font_u16(face, t + 6);
                        if (!coverage || font_coverage(face, t + coverage, glyph) < 0)
                                return false;
                        return font_rule(s, glyphs, &input, t + 6 + 2 * (positive)glyphs, n);
                }
                if (format != 1 && format != 2)
                        return false;
                if ((ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                if (format == 1)
                {
                        if ((p32)ci >= font_u16(face, t + 4))
                                return false;
                        set = font_u16(face, t + 6 + 2 * (positive)ci);
                }
                else
                {
                        p32 klass = font_class(face, t + font_u16(face, t + 4), glyph);

                        if (klass >= font_u16(face, t + 6))
                                return false;
                        set = font_u16(face, t + 8 + 2 * (positive)klass);
                }
                if (!set)
                        return false;
                set += t;
                count = font_u16(face, set);
                for (p32 i = 0; i < count && i < 4096; i++)
                {
                        positive rule = set + font_u16(face, set + 2 + 2 * (positive)i);
                        p32 inputs = font_u16(face, rule), n = font_u16(face, rule + 2);
                        font_matcher input = {format == 1 ? FONT_BY_GLYPH : FONT_BY_CLASS, rule + 4,
                                              format == 1 ? 0 : t + font_u16(face, t + 4)};

                        s->ops--;
                        if (font_rule(s, inputs, &input, rule + 4 + 2 * (positive)(inputs ? inputs - 1 : 0), n))
                                return true;
                }
                return false;
        }
        case 6:
        {
                positive set;
                p32 count;

                if (format == 3)
                {
                        p32 back = font_u16(face, t + 2), inputs = font_u16(face, t + 4 + 2 * (positive)back), ahead, n;
                        positive behind_at = t + 4, input_at = t + 6 + 2 * (positive)back, ahead_at;
                        positive first = font_u16(face, input_at);
                        font_matcher behind = {FONT_BY_COVERAGE, behind_at, t}, input = {FONT_BY_COVERAGE, input_at + 2, t},
                                     after = {FONT_BY_COVERAGE, 0, t};

                        ahead_at = input_at + 2 * (positive)inputs;
                        ahead = font_u16(face, ahead_at);
                        after.array = ahead_at + 2;
                        n = font_u16(face, ahead_at + 2 + 2 * (positive)ahead);
                        if (!first || font_coverage(face, t + first, glyph) < 0)
                                return false;
                        return font_chain_rule(s, &behind, back, &input, inputs, &after, ahead,
                                          ahead_at + 4 + 2 * (positive)ahead, n);
                }
                if (format != 1 && format != 2)
                        return false;
                if ((ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                if (format == 1)
                {
                        if ((p32)ci >= font_u16(face, t + 4))
                                return false;
                        set = font_u16(face, t + 6 + 2 * (positive)ci);
                }
                else
                {
                        p32 klass = font_class(face, t + font_u16(face, t + 6), glyph);

                        if (klass >= font_u16(face, t + 10))
                                return false;
                        set = font_u16(face, t + 12 + 2 * (positive)klass);
                }
                if (!set)
                        return false;
                set += t;
                count = font_u16(face, set);
                for (p32 i = 0; i < count && i < 4096; i++)
                {
                        positive rule = set + font_u16(face, set + 2 + 2 * (positive)i);
                        p32 back = font_u16(face, rule), inputs = font_u16(face, rule + 2 + 2 * (positive)back), ahead, n;
                        positive input_at = rule + 4 + 2 * (positive)back, ahead_at = input_at + 2 * (positive)(inputs ? inputs - 1 : 0);
                        p32 kind3 = format == 1 ? FONT_BY_GLYPH : FONT_BY_CLASS;
                        font_matcher behind = {kind3, rule + 2, format == 1 ? 0 : t + font_u16(face, t + 4)},
                                     input = {kind3, input_at, format == 1 ? 0 : t + font_u16(face, t + 6)},
                                     after = {kind3, ahead_at + 2, format == 1 ? 0 : t + font_u16(face, t + 8)};

                        ahead = font_u16(face, ahead_at);
                        n = font_u16(face, ahead_at + 2 + 2 * (positive)ahead);
                        s->ops--;
                        if (font_chain_rule(s, &behind, back, &input, inputs, &after, ahead, ahead_at + 4 + 2 * (positive)ahead, n))
                                return true;
                }
                return false;
        }
        case 8:
        {
                //      Reverse chaining: one glyph substituted in place, the cursor left where it is.
                p32 back, ahead, n;
                positive behind_at = t + 6, ahead_at;
                font_matcher behind = {FONT_BY_COVERAGE, behind_at, t}, after = {FONT_BY_COVERAGE, 0, t};

                if (format != 1 || s->output || (ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                back = font_u16(face, t + 4);
                ahead_at = behind_at + 2 * (positive)back;
                ahead = font_u16(face, ahead_at);
                after.array = ahead_at + 2;
                n = font_u16(face, ahead_at + 2 + 2 * (positive)ahead);
                if ((p32)ci >= n || !font_match_ahead(s, ahead, &after, s->idx + 1, &(p32){0}))
                        return false;

                //      The glyphs behind are looked for in the string itself, which is not an output buffer here.
                {
                        p32 at = s->idx;

                        for (p32 i = 0; i < back; i++)
                                if (!font_scan_prev(s, &behind, i, true, &at, s->lookup_props))
                                        return false;
                }
                p32 to = font_u16(face, ahead_at + 4 + 2 * (positive)ahead + 2 * (positive)ci);

                font_set_class(s, cur, to, 0, false, false);
                cur->glyph = to;
                return true;
        }
        default:
                return false;
        }
}

/*
        GPOS. A value record's placement and advance added to a glyph's.
*/
//      Sums that a table can apply as often as the work allows are taken modulo 2^32, which is defined.
static inline b32 font_wrap(b32 a, b32 b)
{
        return (b32)((p32)a + (p32)b);
}

static void font_value(const font_face address_to face, positive at, p32 format, font_shift address_to into)
{
        if (format & 1)
                into->x = font_wrap(into->x, font_i16(face, at));
        if (format & 2)
                into->y = font_wrap(into->y, font_i16(face, at + 2 * (positive)font_bits(format & 1)));
        if (format & 4)
                into->advance = font_wrap(into->advance, font_i16(face, at + 2 * (positive)font_bits(format & 3)));
}

//      An anchor's coordinates in font units.
static p8 font_anchor(const font_face address_to face, positive at, b32 address_to x, b32 address_to y)
{
        if (!at || !font_u16(face, at))
                return false;
        *x = font_i16(face, at + 2);
        *y = font_i16(face, at + 4);
        return true;
}

//      Cursive attachment's chain, turned round when a glyph already attached to another is attached to a new one.
static void font_reverse_cursive(font_shift address_to shift, p32 i, p32 parent)
{
        b32 chain = shift[i].chain;

        if (!chain || !(shift[i].attach & 2))
                return;
        shift[i].chain = 0;

        p32 j = (p32)((b32)i + chain);

        if (j == parent)
                return;
        font_reverse_cursive(shift, j, parent);
        shift[j].y = -shift[i].y;
        shift[j].chain = (b16)-chain;
        shift[j].attach = shift[i].attach;
}

//      A mark (the glyph the cursor is on) hung from the glyph at base by the anchors its class has there.
static p8 font_attach(font_shaper address_to s, positive marks, p32 mark_index, positive anchors, positive anchor_at,
                        p32 classes, p32 base)
{
        const font_face address_to face = s->face;
        positive record = marks + 2 + 4 * (positive)mark_index;
        p32 klass = font_u16(face, record);
        positive mark_anchor = marks + font_u16(face, record + 2), base_anchor;
        b32 mx, my, bx, by;

        if (mark_index >= font_u16(face, marks) || klass >= classes)
                return false;
        base_anchor = font_u16(face, anchor_at + 2 * (positive)klass);
        if (!base_anchor || !font_anchor(face, anchors + base_anchor, &bx, &by) ||
            !font_anchor(face, mark_anchor, &mx, &my))
                return false;

        font_shift address_to o = &s->shift[s->idx];

        o->x = bx - mx;
        o->y = by - my;
        o->attach = 1;
        o->chain = (b16)((b32)base - (b32)s->idx);
        s->attached = true;
        s->idx++;
        return true;
}

static p8 font_gpos_apply(font_shaper address_to s, p32 kind, positive t)
{
        const font_face address_to face = s->face;
        font_item address_to cur = &s->info[s->idx];
        p32 glyph = cur->glyph, format = font_u16(face, t);
        b32 ci;

        switch (kind)
        {
        case 1:
        {
                p32 value = font_u16(face, t + 4);

                if ((ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                if (format == 1)
                        font_value(face, t + 6, value, &s->shift[s->idx]);
                else if (format == 2 && (p32)ci < font_u16(face, t + 6))
                        font_value(face, t + 8 + 2 * (positive)font_bits(value) * (positive)ci, value, &s->shift[s->idx]);
                else
                        return false;
                s->idx++;
                return true;
        }
        case 2:
        {
                p32 at = s->idx;
                positive first, second;
                p32 one = font_u16(face, t + 4), two = font_u16(face, t + 6);

                if ((ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0 ||
                    !font_scan_next(s, null, 0, false, &at, s->lookup_props))
                        return false;
                if (!font_pair_at(face, t, glyph, s->info[at].glyph, &first, &second))
                        return false;
                font_value(face, first, one, &s->shift[s->idx]);
                font_value(face, second, two, &s->shift[at]);
                s->idx = two ? at + 1 : at;
                return true;
        }
        case 3:
        {
                //      Cursive attachment: the previous glyph's exit anchor meets this glyph's entry anchor.
                p32 i = s->idx, j = s->idx;
                positive entry_at, exit_at;
                b32 pi, ex, ey, nx, ny;

                if (format != 1 || (ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0 ||
                    (p32)ci >= font_u16(face, t + 4))
                        return false;
                entry_at = font_u16(face, t + 6 + 4 * (positive)ci);
                if (!entry_at || !font_scan_prev(s, null, 0, false, &i, s->lookup_props))
                        return false;
                if ((pi = font_coverage(face, t + font_u16(face, t + 2), s->info[i].glyph)) < 0 ||
                    (p32)pi >= font_u16(face, t + 4))
                        return false;
                exit_at = font_u16(face, t + 6 + 4 * (positive)pi + 2);
                if (!exit_at || !font_anchor(face, t + exit_at, &ex, &ey) || !font_anchor(face, t + entry_at, &nx, &ny))
                        return false;

                font_shift address_to pos = s->shift;
                p32 child = i, parent = j;
                b32 d, y = ny - ey;

                if (!s->rtl)
                {
                        pos[i].advance = font_wrap(ex, pos[i].x);
                        d = font_wrap(nx, pos[j].x);
                        pos[j].advance = font_wrap(pos[j].advance, (b32)(0u - (p32)d));
                        pos[j].x = font_wrap(pos[j].x, (b32)(0u - (p32)d));
                }
                else
                {
                        d = font_wrap(ex, pos[i].x);
                        pos[i].advance = font_wrap(pos[i].advance, (b32)(0u - (p32)d));
                        pos[i].x = font_wrap(pos[i].x, (b32)(0u - (p32)d));
                        pos[j].advance = font_wrap(nx, pos[j].x);
                }

                //      The child hangs from the parent; the tree stays on the baseline at its root.
                if (!(s->lookup_props & 1))
                {
                        child = j;
                        parent = i;
                        y = -y;
                }
                font_reverse_cursive(pos, child, parent);
                pos[child].attach = 2;
                pos[child].chain = (b16)((b32)parent - (b32)child);
                pos[child].y = y;
                s->attached = true;
                s->idx++;
                return true;
        }
        case 4:
        {
                //      Mark to base: back to the nearest glyph that is not a mark.
                positive marks, bases;
                p32 classes, j, base_index;

                if (format != 1 || (ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                classes = font_u16(face, t + 6);
                marks = t + font_u16(face, t + 8);
                bases = t + font_u16(face, t + 10);
                for (j = s->idx; j > 0; j--)
                {
                        const font_item address_to it = &s->info[j - 1];

                        if (s->ops-- <= 0)
                        {
                                s->failed = true;
                                return false;
                        }
                        if (font_skip_of(s, it, false, 8))
                                continue;
                        if (!(it->mask & s->lookup_mask))
                                return false;
                        break;
                }
                if (!j)
                        return false;
                j--;
                if ((b32)(base_index = (p32)font_coverage(face, t + font_u16(face, t + 4), s->info[j].glyph)) < 0 ||
                    base_index >= font_u16(face, bases))
                        return false;
                return font_attach(s, marks, (p32)ci, bases, bases + 2 + 2 * (positive)classes * (positive)base_index, classes, j);
        }
        case 5:
        {
                positive marks, ligatures, attach;
                p32 classes, j, comps, comp_index, lig_index, lig_id, mark_id, mark_comp;
                b32 li;

                if (format != 1 || (ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                j = s->idx;
                if (!font_scan_prev(s, null, 0, false, &j, 8))
                        return false;
                if ((li = font_coverage(face, t + font_u16(face, t + 4), s->info[j].glyph)) < 0)
                        return false;
                lig_index = (p32)li;
                classes = font_u16(face, t + 6);
                marks = t + font_u16(face, t + 8);
                ligatures = t + font_u16(face, t + 10);
                if (lig_index >= font_u16(face, ligatures))
                        return false;
                attach = ligatures + font_u16(face, ligatures + 2 + 2 * (positive)lig_index);
                comps = font_u16(face, attach);
                if (!comps)
                        return false;
                lig_id = font_lig_id(&s->info[j]);
                mark_id = font_lig_id(cur);
                mark_comp = font_lig_comp(cur);
                comp_index = lig_id && lig_id == mark_id && mark_comp > 0 ? (comps < mark_comp ? comps : mark_comp) - 1 : comps - 1;
                return font_attach(s, marks, (p32)ci, attach, attach + 2 + 2 * (positive)classes * (positive)comp_index,
                                   classes, j);
        }
        case 6:
        {
                positive marks, others;
                p32 classes, j = s->idx, id1, id2, comp1, comp2, other_index;
                b32 oi;

                if (format != 1 || (ci = font_coverage(face, t + font_u16(face, t + 2), glyph)) < 0)
                        return false;
                if (!font_scan_prev(s, null, 0, false, &j, s->lookup_props & ~(p32)0xe))
                        return false;
                if (!(s->info[j].props & FONT_PROPS_MARK))
                        return false;
                id1 = font_lig_id(cur);
                id2 = font_lig_id(&s->info[j]);
                comp1 = font_lig_comp(cur);
                comp2 = font_lig_comp(&s->info[j]);
                if (id1 == id2 ? id1 && comp1 != comp2 : !((id1 > 0 && !comp1) || (id2 > 0 && !comp2)))
                        return false;
                if ((oi = font_coverage(face, t + font_u16(face, t + 4), s->info[j].glyph)) < 0)
                        return false;
                other_index = (p32)oi;
                classes = font_u16(face, t + 6);
                marks = t + font_u16(face, t + 8);
                others = t + font_u16(face, t + 10);
                if (other_index >= font_u16(face, others))
                        return false;
                return font_attach(s, marks, (p32)ci, others, others + 2 + 2 * (positive)classes * (positive)other_index,
                                   classes, j);
        }
        case 7:
        case 8:
        {
                //      The same rules as GSUB's 5 and 6, their records naming GPOS lookups.
                return font_gsub_apply(s, kind - 2, t);
        }
        default:
                return false;
        }
}

/*
        Lookups: a lookup's header, the subtables it tries in order, and the
        lookup run over the whole string, or on one glyph for a rule that
        calls it.
*/
static p8 font_lookup_at(const font_shaper address_to s, p32 index, positive address_to lookup, p32 address_to type,
                           p32 address_to count, p32 address_to props)
{
        const font_face address_to face = s->face;
        positive list = s->lookups, at;

        if (!list || index >= font_u16(face, list) || !(at = font_u16(face, list + 2 + 2 * (positive)index)))
                return false;
        *lookup = list + at;
        *type = font_u16(face, *lookup);
        *count = font_u16(face, *lookup + 4);
        *props = font_u16(face, *lookup + 2);
        if (*props & 0x10)
                *props |= font_u16(face, *lookup + 6 + 2 * (positive)*count) << 16;
        return true;
}

static p8 font_try_subtables(font_shaper address_to s, positive lookup, p32 type, p32 count)
{
        const font_face address_to face = s->face;
        p32 extension = s->table ? 9 : 7;

        for (p32 k = 0; k < count && k < FONT_SUBTABLES; k++)
        {
                positive t = lookup + font_u16(face, lookup + 6 + 2 * (positive)k);
                p32 kind = type;

                //      Each subtable tried is paid for: 63 lookups of 256 subtables that cover every glyph and never
                //      apply were 16128 tries a code point and none of them charged. Real faces try under 350 a code
                //      point at worst (Noto Nastaliq Urdu), of a budget of FONT_OPS.
                if (s->ops-- <= 0)
                        return false;
                if (type == extension)
                {
                        if (font_u16(face, t) != 1)
                                continue;
                        kind = font_u16(face, t + 2);
                        t += font_u32(face, t + 4);
                        if (kind == extension)
                                continue;
                }
                if (s->table ? font_gpos_apply(s, kind, t) : font_gsub_apply(s, kind, t))
                        return true;
        }
        return false;
}

//      A lookup a rule names, tried once at the glyph the cursor is on.
static p8 font_apply_nested(font_shaper address_to s, p32 index)
{
        positive lookup;
        p32 type, count, props, saved = s->lookup_props;
        p8 done;

        if (s->nesting >= FONT_NESTING || !font_lookup_at(s, index, &lookup, &type, &count, &props) || s->idx >= s->len)
                return false;
        s->nesting++;
        s->lookup_props = props;
        done = font_let_through(s, &s->info[s->idx], props) && font_try_subtables(s, lookup, type, count);
        s->lookup_props = saved;
        s->nesting--;
        return done;
}

/*
        Which glyphs a lookup can start at, as two words of bits: a glyph sets
        one bit of the first for its low six bits and one of the second for
        the next six, and a glyph the lookup covers has both set. The lookup
        passes over a string without reading a table for a glyph that does
        not: of a line's glyphs and a face's forty lookups that is nearly
        all of them. Built once for a plan from the first coverage of every
        subtable (the mark's, for a mark attachment), with the number of
        entries read capped so that a table claiming sixty thousand glyphs in
        every one of two hundred subtables costs what a line costs.
*/
static inline fn font_digest_add(p64 address_to digest, p32 first, p32 last)
{
        if (last < first)
                return;
        if (last - first >= 63)
                digest[0] = ~(p64)0;
        else
                for (p32 g = first; g <= last; g++)
                        digest[0] |= (p64)1 << (g & 63);
        if ((last >> 6) - (first >> 6) >= 63)
                digest[1] = ~(p64)0;
        else
                for (p32 g = first >> 6; g <= last >> 6; g++)
                        digest[1] |= (p64)1 << (g & 63);
}

static fn font_digest_coverage(const font_face address_to face, positive coverage, p64 address_to digest, p32 address_to budget)
{
        p32 format = font_u16(face, coverage), count = font_u16(face, coverage + 2);

        if ((format != 1 && format != 2) || count > *budget)
        {
                digest[0] = digest[1] = ~(p64)0;
                return;
        }
        *budget -= count;
        for (p32 i = 0; i < count; i++)
                if (format == 1)
                        font_digest_add(digest, font_u16(face, coverage + 4 + 2 * (positive)i), font_u16(face, coverage + 4 + 2 * (positive)i));
                else
                        font_digest_add(digest, font_u16(face, coverage + 4 + 6 * (positive)i), font_u16(face, coverage + 6 + 6 * (positive)i));
}

static fn font_lookup_digest(const font_face address_to face, positive list, p32 index, p32 table, p64 address_to digest)
{
        p32 budget = 100000;
        positive lookup = list && index < font_u16(face, list) ? list + font_u16(face, list + 2 + 2 * (positive)index) : 0;
        p32 type = lookup ? font_u16(face, lookup) : 0, count = lookup ? font_u16(face, lookup + 4) : 0;

        digest[0] = digest[1] = 0;
        if (!lookup || !font_u16(face, list + 2 + 2 * (positive)index))
                return;
        for (p32 k = 0; k < count && k < FONT_SUBTABLES; k++)
        {
                positive t = lookup + font_u16(face, lookup + 6 + 2 * (positive)k), at = t + 2;
                p32 kind = type;

                if (type == (table ? 9 : 7))
                {
                        if (font_u16(face, t) != 1)
                                continue;
                        kind = font_u16(face, t + 2);
                        t += font_u32(face, t + 4);
                        at = t + 2;
                }
                if (table ? kind == 7 : kind == 5)
                {
                        if (font_u16(face, t) == 3)
                                at = t + 6;
                }
                else if (table ? kind == 8 : kind == 6)
                {
                        if (font_u16(face, t) == 3)
                                at = t + 6 + 2 * (positive)font_u16(face, t + 2);
                }
                if ((table ? kind == 7 || kind == 8 : kind == 5 || kind == 6) && font_u16(face, t) == 3)
                {
                        positive first = font_u16(face, at);

                        if (first)
                                font_digest_coverage(face, t + first, digest, &budget);
                        continue;
                }
                font_digest_coverage(face, t + font_u16(face, at), digest, &budget);
        }
}

static inline p8 font_digest_has(const p64 address_to digest, p32 glyph)
{
        return (digest[0] >> (glyph & 63) & digest[1] >> (glyph >> 6 & 63)) & 1;
}

//      A lookup over the whole string, for the glyphs whose mask has a bit of mask.
static void font_apply_string(font_shaper address_to s, p32 index, p32 mask, p32 manual, const p64 address_to digest)
{
        positive lookup;
        p32 type, count, props;
        p8 reverse;

        if (!s->len || !mask || !font_lookup_at(s, index, &lookup, &type, &count, &props))
                return;
        s->lookup_mask = mask;
        s->lookup_props = props;
        s->auto_zwj = !(manual & 1);
        s->auto_zwnj = !(manual & 2);

        //      Nothing in the string it can start at: nothing to copy to an output and back.
        {
                p32 at = 0;

                while (at < s->len && !((s->info[at].mask & mask) && font_digest_has(digest, s->info[at].glyph)))
                        at++;
                if (at == s->len)
                        return;
        }
        reverse = !s->table && (type == 8 || (type == 7 && count && font_u16(s->face, lookup + 6) &&
                                              font_u16(s->face, lookup + font_u16(s->face, lookup + 6) + 2) == 8));
        if (reverse)
        {
                for (b32 i = (b32)s->len - 1; i >= 0 && s->ops-- > 0 && !s->failed; i--)
                {
                        s->idx = (p32)i;
                        if ((s->info[i].mask & mask) && font_digest_has(digest, s->info[i].glyph) &&
                            font_let_through(s, &s->info[i], props))
                                font_try_subtables(s, lookup, type, count);
                }
                return;
        }
        if (!s->table)
                font_clear_output(s);
        else
        {
                s->output = false;
                s->out = s->info;
        }
        s->idx = 0;
        while (s->idx < s->len && !s->failed && s->ops-- > 0)
        {
                font_item address_to cur = &s->info[s->idx];

                if (!((cur->mask & mask) && font_digest_has(digest, cur->glyph) && font_let_through(s, cur, props) &&
                      font_try_subtables(s, lookup, type, count)))
                        font_next_glyph(s);
        }
        if (!s->table)
                font_sync(s);
}

/*
        The plan a face has for a script: which lookups run and in what
        order. HarfBuzz's: features are asked for in stages (the Arabic
        joining forms each their own, with rlig and then calt and rclt after
        them), the lookups of a stage run by index, and a glyph is given a
        bit of the mask for each form it is to have; GPOS runs the lookups
        of all its features at once, by index.
*/
typedef struct
{
        p32 tag;
        p16 mask;
        p8 stage, manual;
} font_feature;

typedef struct
{
        p16 lookup, mask;
        p8 stage, manual;
} font_step;

//      Which glyphs a feature's lookups are for: 0 all of them, then the joining forms, then rtlm.
#define FONT_MASK_FORM(action) (2u << (action))
#define FONT_MASK_RTLM 256u

static p32 font_features(p32 kind, p8 rtl, font_feature address_to out)
{
        p32 n = 0;
        static const p32 common[] = {FONT_TAG('a', 'b', 'v', 'm'), FONT_TAG('b', 'l', 'w', 'm'), FONT_TAG('c', 'l', 'i', 'g'),
                                     FONT_TAG('c', 'u', 'r', 's'), FONT_TAG('d', 'i', 's', 't'), FONT_TAG('k', 'e', 'r', 'n'),
                                     FONT_TAG('l', 'i', 'g', 'a'), FONT_TAG('m', 'a', 'r', 'k'), FONT_TAG('m', 'k', 'm', 'k')};
        static const p32 forms[] = {FONT_TAG('i', 's', 'o', 'l'), FONT_TAG('f', 'i', 'n', 'a'), FONT_TAG('f', 'i', 'n', '2'),
                                    FONT_TAG('f', 'i', 'n', '3'), FONT_TAG('m', 'e', 'd', 'i'), FONT_TAG('m', 'e', 'd', '2'),
                                    FONT_TAG('i', 'n', 'i', 't')};
        p8 arabic = kind == 2;

        out[n++] = (font_feature){FONT_TAG('r', 'v', 'r', 'n'), 0, 0, 0};
        if (rtl)
        {
                out[n++] = (font_feature){FONT_TAG('r', 't', 'l', 'a'), 0, 1, 0};
                out[n++] = (font_feature){FONT_TAG('r', 't', 'l', 'm'), FONT_MASK_RTLM, 1, 0};
        }
        else
        {
                out[n++] = (font_feature){FONT_TAG('l', 't', 'r', 'a'), 0, 1, 0};
                out[n++] = (font_feature){FONT_TAG('l', 't', 'r', 'm'), 0, 1, 0};
        }
        out[n++] = (font_feature){FONT_TAG('c', 'c', 'm', 'p'), 0, arabic ? 2 : 1, 0};
        out[n++] = (font_feature){FONT_TAG('l', 'o', 'c', 'l'), 0, arabic ? 2 : 1, 0};
        if (arabic)
        {
                for (p32 i = 0; i < 7; i++)
                        out[n++] = (font_feature){forms[i], (p16)FONT_MASK_FORM(i), (p8)(3 + i), 0};
                out[n++] = (font_feature){FONT_TAG('r', 'l', 'i', 'g'), 0, 10, 1};
                out[n++] = (font_feature){FONT_TAG('r', 'c', 'l', 't'), 0, 11, 1};
                out[n++] = (font_feature){FONT_TAG('c', 'a', 'l', 't'), 0, 11, 1};
                out[n++] = (font_feature){FONT_TAG('m', 's', 'e', 't'), 0, 12, 0};
        }
        else
        {
                out[n++] = (font_feature){FONT_TAG('r', 'l', 'i', 'g'), 0, 1, 0};
                out[n++] = (font_feature){FONT_TAG('r', 'c', 'l', 't'), 0, 1, 0};
                out[n++] = (font_feature){FONT_TAG('c', 'a', 'l', 't'), 0, 1, 0};
        }
        for (p32 i = 0; i < sizeof(common) / sizeof(common[0]); i++)
                out[n++] = (font_feature){common[i], 0, arabic ? 12 : 1,
                                          (p8)(common[i] == FONT_TAG('m', 'a', 'r', 'k') || common[i] == FONT_TAG('m', 'k', 'm', 'k') ? 3 : 0)};
        return n;
}

//      The script's default language system in one of the tables, for the script tags this kind of text goes by and
//      then DFLT, dflt and latn, or 0.
static positive font_language(const font_face address_to face, positive table, p32 kind)
{
        positive scripts = table + font_u16(face, table + 4);
        p32 count = font_u16(face, scripts);
        p32 tags[4] = {kind == 2 ? FONT_TAG('a', 'r', 'a', 'b') : FONT_TAG('h', 'e', 'b', 'r'), FONT_TAG('D', 'F', 'L', 'T'),
                       FONT_TAG('d', 'f', 'l', 't'), FONT_TAG('l', 'a', 't', 'n')};

        if (count > 1024)
                count = 1024;
        for (p32 t = 0; t < 4; t++)
                for (p32 i = 0; i < count; i++)
                {
                        positive record = scripts + 2 + 6 * (positive)i;

                        if (font_u32(face, record) == tags[t])
                        {
                                positive script = scripts + font_u16(face, record + 4), language = font_u16(face, script);

                                return language ? script + language : 0;
                        }
                }
        return 0;
}

//      The plan for a table (0 GSUB, 1 GPOS) of the face: lookups by stage and index, with what they are for.
static fn font_plan_table(const font_face address_to face, font_plan address_to plan, p32 table, p32 kind, p8 rtl)
{
        positive base = table ? face->gpos : face->gsub, features, langsys;
        font_feature wanted[40];
        p32 n = font_features(kind, rtl, wanted), found = 0, count;
        font_step step[2 * FONT_STEPS];

        if (!base || font_u16(face, base) != 1)
                return;
        langsys = font_language(face, base, kind);
        features = base + font_u16(face, base + 6);
        if (!langsys)
                return;
        count = font_u16(face, langsys + 4);
        if (count > 4096)
                count = 4096;
        for (p32 w = 0; w < n; w++)
                for (p32 i = 0; i < count; i++)
                {
                        positive feature = features + 2 + 6 * (positive)font_u16(face, langsys + 6 + 2 * (positive)i);
                        positive params;

                        if (font_u32(face, feature) != wanted[w].tag)
                                continue;
                        params = features + font_u16(face, feature + 4);
                        for (p32 k = 0, lookups = font_u16(face, params + 2); k < lookups && found < 2 * FONT_STEPS; k++)
                        {
                                step[found].lookup = (p16)font_u16(face, params + 4 + 2 * (positive)k);
                                step[found].mask = wanted[w].mask ? wanted[w].mask : 1;
                                step[found].stage = table ? 0 : wanted[w].stage;
                                step[found].manual = wanted[w].manual;
                                found++;
                        }
                        break;
                }

        //      By stage, then index; the same lookup asked for twice in a stage is one step for either's glyphs.
        for (p32 a = 1; a < found; a++)
        {
                font_step one = step[a];
                p32 b = a;

                for (; b > 0 && (step[b - 1].stage > one.stage ||
                                 (step[b - 1].stage == one.stage && step[b - 1].lookup > one.lookup)); b--)
                        step[b] = step[b - 1];
                step[b] = one;
        }
        for (p32 a = 0, out = 0; a < found; a++)
        {
                if (out && plan->lookup[table][out - 1] == step[a].lookup && plan->stage_of[table][out - 1] == step[a].stage)
                {
                        plan->mask[table][out - 1] |= step[a].mask;
                        plan->manual[table][out - 1] |= step[a].manual;
                        continue;
                }
                if (out == FONT_STEPS)
                        break;
                plan->lookup[table][out] = step[a].lookup;
                plan->mask[table][out] = step[a].mask;
                plan->manual[table][out] = step[a].manual;
                plan->stage_of[table][out++] = step[a].stage;
                plan->steps[table] = (p8)out;
        }
}

static font_plan address_to font_plan_for(font_engine address_to e, const font_face address_to face, p32 kind, p8 rtl)
{
        p32 key = kind | (rtl ? 4 : 0);
        font_plan address_to plan;

        for (p32 i = 0; i < FONT_PLANS; i++)
                if (e->plan[i].face == face->id && e->plan[i].kind == key)
                        return &e->plan[i];
        plan = &e->plan[e->plan_turn++ % FONT_PLANS];
        memory_zero(plan, sizeof(*plan));
        plan->face = face->id;
        plan->kind = (p8)key;
        for (p32 action = 0; action < 7; action++)
                plan->mask_of[action] = (p16)FONT_MASK_FORM(action);
        font_plan_table(face, plan, 0, kind, rtl);
        font_plan_table(face, plan, 1, kind, rtl);
        for (p32 table = 0; table < 2; table++)
        {
                positive base = table ? face->gpos : face->gsub;
                positive list = base && font_u16(face, base) == 1 ? base + font_u16(face, base + 8) : 0;

                for (p32 step = 0; step < plan->steps[table]; step++)
                        font_lookup_digest(face, list, plan->lookup[table][step], table, plan->digest[table][step]);
        }

        //      Without any of the joining forms in the face, the letters take their presentation forms instead.
        if (kind == 2)
        {
                static const p32 forms[] = {FONT_TAG('i', 's', 'o', 'l'), FONT_TAG('f', 'i', 'n', 'a'), FONT_TAG('m', 'e', 'd', 'i'),
                                            FONT_TAG('i', 'n', 'i', 't')};
                p8 any = false;
                positive langsys = face->gsub && font_u16(face, face->gsub) == 1 ? font_language(face, face->gsub, kind) : 0;

                if (langsys)
                        for (p32 i = 0, count = font_u16(face, langsys + 4); i < count && i < 4096 && !any; i++)
                        {
                                positive feature = face->gsub + font_u16(face, face->gsub + 6) + 2 +
                                                   6 * (positive)font_u16(face, langsys + 6 + 2 * (positive)i);

                                for (p32 f = 0; f < 4; f++)
                                        any = any || font_u32(face, feature) == forms[f];
                        }
                plan->fallback = !any;
        }
        return plan;
}

/*
        Shaping a unit: the code points of one face, one level and one
        script, as the glyphs the face's tables make of them. The steps are
        HarfBuzz's, in its order: a glyph for each code point (the mirror
        glyph for a bracket in right to left text, the pieces of a letter
        the face has only decomposed), combining marks put in order by
        class (Arabic's modifier marks first among their class), Arabic
        letters given the form their neighbours ask for, the GSUB lookups
        of the plan, the advances and GPOS's lookups, marks given no width
        of their own, and the offsets of what hangs from what settled.
*/
static const p8 font_join_state[3][4][3] = {
    //      from a letter that does not join on: after a non-joining letter, or a right joining one
    {{FONT_ACT_NONE, FONT_ACT_NONE, 0}, {FONT_ACT_NONE, FONT_ACT_ISOL, 1}, {FONT_ACT_NONE, FONT_ACT_ISOL, 0}, {FONT_ACT_NONE, FONT_ACT_ISOL, 1}},
    //      after a letter in its isolated form that joins on
    {{FONT_ACT_NONE, FONT_ACT_NONE, 0}, {FONT_ACT_NONE, FONT_ACT_ISOL, 1}, {FONT_ACT_INIT, FONT_ACT_FINA, 0}, {FONT_ACT_INIT, FONT_ACT_FINA, 2}},
    //      after a letter in its final form that joins on
    {{FONT_ACT_NONE, FONT_ACT_NONE, 0}, {FONT_ACT_NONE, FONT_ACT_ISOL, 1}, {FONT_ACT_MEDI, FONT_ACT_FINA, 0}, {FONT_ACT_MEDI, FONT_ACT_FINA, 2}},
};

//      Which form each letter of the unit takes. A mark the joining passes through has none.
static fn font_joining(const font_item address_to items, p32 n, p8 address_to action)
{
        p32 state = 0, prev = 0xffffffffu;

        for (p32 i = 0; i < n; i++)
        {
                p32 type = font_join_type(items[i].code);

                if (type == 7)
                {
                        action[i] = FONT_ACT_NONE;
                        continue;
                }

                const p8 address_to step = font_join_state[state][type];

                if (step[0] != FONT_ACT_NONE && prev != 0xffffffffu)
                        action[prev] = step[0];
                action[i] = step[1];
                prev = i;
                state = step[2];
        }
}

//      The code point the presentation form of a letter is, in the form an action asks for, or 0.
static p32 font_presentation(p32 code, p32 action)
{
        p32 low = 0, high = FONT_FORMS, column = action == FONT_ACT_ISOL ? 1 : action == FONT_ACT_FINA ? 2
                                                  : action == FONT_ACT_INIT ? 3 : action == FONT_ACT_MEDI ? 4 : 0;

        while (low < high && column)
        {
                p32 middle = (low + high) / 2;

                if (font_forms[middle][0] < code)
                        low = middle + 1;
                else
                        high = middle;
        }
        return column && low < FONT_FORMS && font_forms[low][0] == code ? font_forms[low][column] : 0;
}

//      Modifier combining marks: the marks of the Arabic block that sit nearest the base among those of their class.
static inline p8 font_arabic_modifier(p32 code)
{
        return code == 0x654 || code == 0x655 || code == 0x658 || code == 0x6dc || code == 0x6e3 || code == 0x6e7 ||
               code == 0x6e8 || (code >= 0x8ca && code <= 0x8cb) || (code >= 0x8cd && code <= 0x8cf) || code == 0x8d3 ||
               code == 0x8f3;
}

//      Runs of marks put in order by modified combining class, and for Arabic the modifier marks moved first
//      among those of the classes 220 and 230.
static fn font_order_marks(font_item address_to items, p32 n, p8 arabic)
{
        for (p32 i = 0; i < n;)
        {
                if (!font_mccc(items[i].code))
                {
                        i++;
                        continue;
                }

                p32 stop = i + 1;

                while (stop < n && font_mccc(items[stop].code))
                        stop++;
                if (stop - i > 1 && stop - i <= 32)
                {
                        for (p32 a = i + 1; a < stop; a++)
                        {
                                font_item one = items[a];
                                p32 b = a;

                                for (; b > i && font_mccc(items[b - 1].code) > font_mccc(one.code); b--)
                                        items[b] = items[b - 1];
                                items[b] = one;
                        }
                        if (arabic)
                        {
                                p32 at = i, from = i;

                                for (p32 cc = 220; cc <= 230; cc += 10)
                                {
                                        while (from < stop && font_mccc(items[from].code) < cc)
                                                from++;
                                        if (from == stop)
                                                break;
                                        if (font_mccc(items[from].code) > cc)
                                                continue;

                                        p32 to = from;

                                        while (to < stop && font_mccc(items[to].code) == cc && font_arabic_modifier(items[to].code))
                                                to++;
                                        if (from == to)
                                                continue;

                                        font_item moved[32];

                                        memory_copy(moved, items + from, (to - from) * sizeof(font_item));
                                        memory_copy(items + at + (to - from), items + at, (from - at) * sizeof(font_item));
                                        memory_copy(items + at, moved, (to - from) * sizeof(font_item));
                                        at += to - from;
                                        from = to;
                                }
                        }
                }
                i = stop;
        }
}

//      The code point a base and a mark compose to, if they do and the face has a glyph for it: a mark that follows its
//      base, or follows marks of a smaller class, is folded into it (the maddah above an alef is the alef with madda).
static fn font_compose(font_shaper address_to s)
{
        p32 out = 1, starter = 0;

        for (p32 i = 1; i < s->len; i++)
        {
                font_item cur = s->info[i];
                p32 composed = 0, glyph = 0;

                if ((cur.unicode & 8) && (starter == out - 1 || font_mccc(s->info[out - 1].code) < font_mccc(cur.code)))
                {
                        for (p32 row = 0; row < FONT_DECOMPS && !composed; row++)
                                if (font_decomp_composes[row] && font_decomp_first[row] == s->info[starter].code &&
                                    font_decomp_second[row] == cur.code)
                                        composed = font_decomp_code[row];
                        glyph = composed ? font_glyph(s->face, composed) : 0;
                }
                if (glyph)
                {
                        for (p32 k = starter; k < out; k++)
                                s->info[k].cluster = s->info[k].cluster < cur.cluster ? s->info[k].cluster : cur.cluster;
                        s->info[starter].code = composed;
                        s->info[starter].glyph = glyph;
                        s->info[starter].unicode &= (p8)~8;
                        s->info[starter].props = (p16)(s->classes ? font_glyph_props(s->face, glyph) : 2);
                        continue;
                }
                s->info[out++] = cur;
                if (!font_mccc(cur.code))
                        starter = out - 1;
        }
        if (s->len)
                s->len = out;
}

//      The canonical decomposition of a code point in the Hebrew and Arabic blocks, as the index of its row, or -1.
static b32 font_decomposition(p32 code)
{
        p32 low = 0, high = FONT_DECOMPS;

        while (low < high)
        {
                p32 middle = (low + high) / 2;

                if (font_decomp_code[middle] < code)
                        low = middle + 1;
                else
                        high = middle;
        }
        return low < FONT_DECOMPS && font_decomp_code[low] == code ? (b32)low : -1;
}

//      A code point as an item of the string: its glyph, or the glyphs of its decomposition if the face has none.
static fn font_push(font_shaper address_to s, p32 code, p32 draw, p8 rtlm, p32 cluster, p32 depth)
{
        const font_face address_to face = s->face;
        p32 glyph = font_glyph(face, draw);

        if (!glyph && depth < 3 && !font_ignorable(draw))
        {
                b32 at = font_decomposition(draw);

                if (at >= 0)
                {
                        font_push(s, font_decomp_first[at], font_decomp_first[at], rtlm, cluster, depth + 1);
                        if (font_decomp_second[at])
                                font_push(s, font_decomp_second[at], font_decomp_second[at], rtlm, cluster, depth + 1);
                        return;
                }
        }
        if (s->len >= s->cap)
        {
                s->failed = true;
                return;
        }

        font_item address_to it = &s->info[s->len++];
        p8 mark = font_mccc(code) != 0 || font_bidi_class(code) == FONT_BIDI_NSM;

        it->glyph = glyph;
        it->cluster = cluster;
        it->mask = 1 | (rtlm ? FONT_MASK_RTLM : 0);
        it->code = code;
        it->props = (p16)(s->classes ? font_glyph_props(face, glyph) : mark ? 8 : 2);
        it->lig = 0;
        it->unicode = (p8)((font_ignorable(code) ? 1 : 0) | (code == 0x200d ? 2 : 0) | (code == 0x200c ? 4 : 0) | (mark ? 8 : 0));
}

//      Where the cursive and mark attachments left each glyph, settled from the roots out: a glyph hangs from the
//      glyph before it (or, joined cursively, after it), and takes on that glyph's offset and the advances between.
static fn font_settle(font_shift address_to shift, p32 len, p16 address_to path, p8 rtl)
{
        for (p32 i = 0; i < len; i++)
        {
                p32 top = 0, at = i;

                while (shift[at].chain && top < FONT_GLYPHS)
                {
                        b32 parent = (b32)at + shift[at].chain;

                        shift[at].chain = 0;
                        if (parent < 0 || (p32)parent >= len)
                                break;
                        path[top++] = (p16)at;
                        at = (p32)parent;
                }

                p32 parent = at;

                while (top)
                {
                        p32 child = path[--top];

                        if (shift[child].attach & 2)
                                shift[child].y = font_wrap(shift[child].y, shift[parent].y);
                        else
                        {
                                shift[child].x = font_wrap(shift[child].x, shift[parent].x);
                                shift[child].y = font_wrap(shift[child].y, shift[parent].y);
                                if (!rtl)
                                        for (p32 k = parent; k < child; k++)
                                                shift[child].x = font_wrap(shift[child].x, (b32)(0u - (p32)shift[k].advance));
                                else
                                        for (p32 k = parent + 1; k < child + 1; k++)
                                                shift[child].x = font_wrap(shift[child].x, shift[k].advance);
                        }
                        parent = child;
                }
        }
}

static void font_run_plan(font_shaper address_to s, const font_plan address_to plan, p32 table)
{
        const font_face address_to face = s->face;
        positive base = table ? face->gpos : face->gsub;

        s->table = table;
        s->lookups = base && font_u16(face, base) == 1 ? base + font_u16(face, base + 8) : 0;
        for (p32 step = 0; step < plan->steps[table] && !s->failed; step++)
                font_apply_string(s, plan->lookup[table][step], plan->mask[table][step], plan->manual[table][step],
                                  plan->digest[table][step]);
}

/*
        The unit of w->code[first, first + n) shaped by the face as kind (2
        Arabic, 1 the default features), read in logical order whatever
        its direction. The glyphs come back in logical order in *result, in
        font units: w->shift has each one's advance and offsets. Ignorable
        code points are shaped (the joining sees them) and dropped at the
        end, the way a face hides them.
*/
static p32 font_shape_unit(font_engine address_to e, const font_face address_to face, p32 kind, p8 rtl, p32 first, p32 n,
                      font_item address_to address_to result)
{
        font_work address_to w = &e->work;
        const font_plan address_to plan = font_plan_for(e, face, kind, rtl);
        font_shaper s = {.face = face, .info = w->item[0], .out = w->item[0], .spare = w->item[1], .shift = w->shift,
                         .cap = FONT_GLYPHS, .rtl = rtl, .classes = font_gdef_classes(face)};

        s.ops = (b64)FONT_OPS * n + 65536;
        for (p32 i = 0; i < n && !s.failed; i++)
        {
                p32 code = w->code[first + i], draw = code;
                p8 rtlm = false;

                if (w->level[first + i] == FONT_GONE)
                        continue;
                if (rtl)
                {
                        p32 mirror = font_mirror_of(code);

                        if (mirror != code && font_glyph(face, mirror))
                                draw = mirror;
                        else
                                rtlm = true;
                }
                font_push(&s, code, draw, rtlm, first + i, 0);
        }
        font_order_marks(s.info, s.len, kind == 2);
        font_compose(&s);

        if (kind == 2)
        {
                p8 address_to action = w->action;

                font_joining(s.info, s.len, action);
                for (p32 i = 0; i < s.len; i++)
                        if (action[i] != FONT_ACT_NONE)
                                s.info[i].mask |= plan->mask_of[action[i]];

                //      A face with none of the joining forms gets the presentation forms of the letters, and lam
                //      with an alef after it their ligature.
                if (plan->fallback)
                        for (p32 i = 0; i < s.len; i++)
                        {
                                p32 glyph = 0, form = font_presentation(s.info[i].code, action[i]);

                                if (s.info[i].code == 0x644)
                                {
                                        p32 next = i + 1;

                                        while (next < s.len && font_join_type(s.info[next].code) == 7)
                                                next++;
                                        for (p32 a = 0; next < s.len && a < FONT_LAM_ALEFS; a++)
                                                if (font_lam_alef[a][0] == s.info[next].code)
                                                {
                                                        p8 alone = action[i] == FONT_ACT_ISOL || action[i] == FONT_ACT_INIT;

                                                        if ((glyph = font_glyph(face, font_lam_alef[a][alone ? 1 : 2])))
                                                        {
                                                                s.info[next].glyph = 0;
                                                                s.info[next].unicode |= 1;
                                                        }
                                                        break;
                                                }
                                }
                                if (!glyph && form)
                                        glyph = font_glyph(face, form);
                                if (glyph)
                                {
                                        s.info[i].glyph = glyph;
                                        if (s.classes)
                                                s.info[i].props = (p16)font_glyph_props(face, glyph);
                                }
                        }
        }

        font_run_plan(&s, plan, 0);

        //      Advances, then GPOS.
        for (p32 i = 0; i < s.len; i++)
        {
                s.shift[i].advance = (b32)font_u16(face, ((const font_face *)(face))->hmtx + 4 * (positive)(((p32)(s.info[i].glyph)) < ((const font_face *)(face))->metrics ? ((p32)(s.info[i].glyph)) : ((const font_face *)(face))->metrics - 1));
                s.shift[i].x = s.shift[i].y = 0;
                s.shift[i].chain = 0;
                s.shift[i].attach = 0;
        }
        s.out = s.info;
        s.output = false;
        s.failed = false;
        s.attached = false;
        font_run_plan(&s, plan, 1);

        for (p32 i = 0; i < s.len; i++)
                if (s.info[i].props & FONT_PROPS_MARK)
                        s.shift[i].advance = 0;
        for (p32 i = 0; i < s.len; i++)
                if (s.info[i].unicode & 1)
                        s.shift[i].advance = 0;
        if (s.attached)
                font_settle(s.shift, s.len, w->pair, rtl);

        //      Ignorable code points are hidden: gone from the line, and from its widths.
        p32 kept = 0;

        for (p32 i = 0; i < s.len; i++)
                if (!(s.info[i].unicode & 1))
                {
                        s.info[kept] = s.info[i];
                        s.shift[kept++] = s.shift[i];
                }
        *result = s.info;
        return kept;
}

//      Whole pixels from 16.16, up or to the nearest, for the cell.
static inline b32 font_ceiling(b32 fixed)
{
        return (b32)(((b64)fixed + 0xffff) >> 16);
}

//      The monospace cell's width alone, as font_metrics_at gives it: the
//      pen starts a run with this and needs no other metric from it.
static inline b32 font_cell_width(const font_face address_to face, p32 size)
{
        b32 width = (font_fixed(face, face->em_advance, size) + 0x8000) >> 16;

        return width < 1 ? 1 : width;
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
                                        p32 first, p32 count, positive p, p8 checked)
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

                                dx = (b64)((f64)dx * square_root(sx) / 16384);
                                dy = (b64)((f64)dy * square_root(sy) / 16384);
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
static inline p8 font_on_grid(b64 v)
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
static fn font_embolden(font_engine address_to e, f32 strength, p8 across)
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
                        f32 in = narrow_square_root(ix * ix + iy * iy), out = narrow_square_root(ox * ox + oy * oy);
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
        p8 used;
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
                if (last <= first || (c < 64 && (e->still >> c & 1)))
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
                        p8 vertical = k < n && way && (going == 0 ? b.x - a.x <= slack && a.x - b.x <= slack
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
static inline p8 font_gap_kept(b64 was, b64 now)
{
        return now >= 0 && (was < FONT_ONE / 2 || now >= FONT_ONE);
}

//      Whether the snap leaves a contour where it is (a dot, font_dots's).
static inline p8 font_still(const font_engine address_to e, p32 contour)
{
        return contour < 64 && (e->still >> contour & 1);
}

//      The least and most x of the points the snap moves.
static fn font_reach(const font_engine address_to e, b64 address_to lowest, b64 address_to highest)
{
        p32 first = 0;

        *lowest = (b64)1 << 62, *highest = -*lowest;
        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
                for (p32 i = first; i <= e->contour[c].last && !font_still(e, c); i++)
                {
                        *lowest = e->points[i].x < *lowest ? e->points[i].x : *lowest;
                        *highest = e->points[i].x > *highest ? e->points[i].x : *highest;
                }
}

//      snapped, when not null, takes each stem that stayed snapped as four
//      places -- its two edges as they were and as they are now -- and the
//      count of them is the answer. rooms are the dots inside counters (the
//      dot of a dotted zero), each its left and right as they were: the
//      counter around one is left FONT_ROOM wide or more where the glyph's
//      own pixels allow it (font_dots sets the dot in it).
#define FONT_ROOM (FONT_ONE * 3)

static p32 font_snap_axis(font_engine address_to e, b64 normal, b64 least, b64 widest,
                          const b64 address_to fixed, p32 fixes, b64 whole_from, p8 loose, b64 address_to snapped,
                          const b64 address_to rooms, p32 room_count)
{
        if (normal <= 0 || normal >= (b64)e->snap_widest * (FONT_ONE / 64))
                return 0;

        font_run run[2 * FONT_STEMS];
        b64 stem[2 * FONT_STEMS], to[2 * FONT_STEMS];
        p32 runs = font_runs(e, FONT_ONE / 32, least / 4, run);
        p32 stems = font_stems(run, runs, widest, stem, least);
        p8 dropped[FONT_STEMS] = {0};
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
        p8 whole = normal >= whole_from;
        for (p32 s = 0; s < stems; s++)
        {
                b64 left = stem[2 * s], right = stem[2 * s + 1], width = right - left;
                p8 low = font_on_grid(left), high = font_on_grid(right);

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
                        from = FONT_STEPS_UP((from + FONT_ONE) >> (FONT_SHIFT + 1));
                        to[2 * s] = from;
                        to[2 * s + 1] = from + snapped;
                }
        }

        //      No stem leaves the pixels the glyph's own ends are in: one the
        //      snap would set past them moves in by whole pixels, and one too
        //      wide for them is let go. Pushed out, the round sides of % went a
        //      pixel past both ends at every size, out of its cell.
        {
                b64 lowest, highest;

                font_reach(e, &lowest, &highest);
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
                        p8 straddled = stem[2 * s] < k && k < stem[2 * s + 1];

                        if ((straddled && !whole) || (to[2 * s] < k && k < to[2 * s + 1]) ||
                            (!straddled && stem[2 * s + 1] <= k && to[2 * s + 1] > k) ||
                            (!straddled && stem[2 * s] >= k && to[2 * s] < k))
                                dropped[s] = true;
                }

        //      Drop snaps that reorder stems or shut a counter, the stem that
        //      moved most first, until none does.
        for (p8 again = true; again;)
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

        //      The width of a counter between two stems, each rounded about
        //      its own middle, can come out a pixel off the font's: when it is
        //      more than three quarters off, the one of the two whose middle
        //      a pixel back lands the nearer its own goes back, where that
        //      keeps it in the glyph's own pixels and clear of the stem beyond,
        //      and the stem was on a tie (moved, its middle is no more than
        //      9/16 px off: a stem moved further reads as moved, and the bowl
        //      of Inter's b at 9 px, sent out 0.6 px, opened at its neck).
        //      The sides of Plex Mono's 0 and O at 10 px are 0.99 and 5.01 px
        //      in: 0 went out to the whole cell, six pixels and touching the
        //      next 0, O in to four, where both are 4.9 px wide.
        if (whole && !loose)
        {
                b64 lowest, highest;
                p32 previous = FONT_STEMS;

                font_reach(e, &lowest, &highest);
                lowest &= ~(b64)(FONT_ONE - 1);
                highest = (highest + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1);
                for (p32 s = 0; s < stems; s++)
                {
                        if (dropped[s])
                                continue;
                        if (previous == FONT_STEMS)
                        {
                                previous = s;
                                continue;
                        }

                        p32 p = previous;
                        b64 was = stem[2 * s] - stem[2 * p + 1], now = to[2 * s] - to[2 * p + 1];
                        b64 step = now > was ? FONT_ONE : -FONT_ONE;

                        previous = s;
                        if (was < FONT_ONE / 2 || (4 * (now - was) <= 3 * FONT_ONE && 4 * (was - now) <= 3 * FONT_ONE))
                                continue;

                        //      Moving p by step, or s by -step: each one's error after.
                        b64 miss_p = to[2 * p] + to[2 * p + 1] + 2 * step - stem[2 * p] - stem[2 * p + 1];
                        b64 miss_s = to[2 * s] + to[2 * s + 1] - 2 * step - stem[2 * s] - stem[2 * s + 1];
                        p32 before = FONT_STEMS, after = FONT_STEMS;

                        for (p32 q = 0; q < p; q++)
                                before = dropped[q] ? before : q;
                        for (p32 q = stems; q > s + 1; q--)
                                after = dropped[q - 1] ? after : q - 1;
                        p8 can_p = to[2 * p] + step >= lowest && to[2 * p + 1] + step <= highest &&
                                     (before == FONT_STEMS || to[2 * p] + step - to[2 * before + 1] >= FONT_ONE) &&
                                     to[2 * s] - to[2 * p + 1] - step >= FONT_ONE;
                        p8 can_s = to[2 * s] - step >= lowest && to[2 * s + 1] - step <= highest &&
                                     (after == FONT_STEMS || to[2 * after] - to[2 * s + 1] + step >= FONT_ONE) &&
                                     to[2 * s] - step - to[2 * p + 1] >= FONT_ONE;

                        //      Only a stem that sat on a tie, its middle no more
                        //      than 9/16 px off its own after the move (both
                        //      errors here are doubled). Of two that may move,
                        //      the one that leaves the pair's middle the nearer
                        //      the font's, and on a tie the pair further left:
                        //      decided by where the two end, not by which way
                        //      each went first, or 0 and O at 10 px, the one
                        //      grown and the other shrunk into the same five
                        //      pixels, stood a pixel apart in their cells.
                        miss_p = miss_p < 0 ? -miss_p : miss_p;
                        miss_s = miss_s < 0 ? -miss_s : miss_s;
                        can_p = can_p && miss_p <= FONT_ONE * 9 / 8;
                        can_s = can_s && miss_s <= FONT_ONE * 9 / 8;

                        b64 middle = stem[2 * p] + stem[2 * s + 1];
                        b64 at_p = to[2 * p] + step + to[2 * s + 1], at_s = to[2 * p] + to[2 * s + 1] - step;
                        b64 off_p = at_p > middle ? at_p - middle : middle - at_p;
                        b64 off_s = at_s > middle ? at_s - middle : middle - at_s;

                        if (can_p && (!can_s || off_p < off_s || (off_p == off_s && at_p < at_s)))
                                to[2 * p] += step, to[2 * p + 1] += step;
                        else if (can_s)
                                to[2 * s] -= step, to[2 * s + 1] -= step;
                }
        }

        //      A counter that holds a dot is a pixel of air, the dot, and a
        //      pixel of air across, three pixels at least: the stem beside it
        //      that its rounding took the further in goes back out a pixel,
        //      if that leaves it in the glyph's own pixels and clear of the
        //      stem beyond it. Snapped as O is, the dotted zero of Plex Mono at
        //      10 px kept a counter of two pixels, its dot a bar across it;
        //      with the dot among the stems, both sides went out and the zero
        //      was the whole cell wide, a pixel wider each side than O.
        for (p32 r = 0; r < room_count && whole; r++)
        {
                b64 lowest, highest;

                font_reach(e, &lowest, &highest);
                lowest &= ~(b64)(FONT_ONE - 1);
                highest = (highest + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1);
                for (p32 tries = 0; tries < 2; tries++)
                {
                        p32 left = FONT_STEMS, right = FONT_STEMS;

                        for (p32 s = 0; s < stems; s++)
                                if (!dropped[s] && stem[2 * s + 1] <= rooms[2 * r])
                                        left = s;
                                else if (!dropped[s] && stem[2 * s] >= rooms[2 * r + 1] && right == FONT_STEMS)
                                        right = s;
                        if (left == FONT_STEMS || right == FONT_STEMS || to[2 * right] - to[2 * left + 1] >= FONT_ROOM)
                                break;

                        //      How far each went in, and whether it can go out.
                        b64 in_left = to[2 * left + 1] - stem[2 * left + 1], in_right = stem[2 * right] - to[2 * right];
                        p8 out_left = to[2 * left] - FONT_ONE >= lowest &&
                                        (left == 0 || dropped[left - 1] || to[2 * left - 1] <= to[2 * left] - 2 * FONT_ONE);
                        p8 out_right = to[2 * right + 1] + FONT_ONE <= highest &&
                                         (right + 1 == stems || dropped[right + 1] ||
                                          to[2 * right + 2] >= to[2 * right + 1] + 2 * FONT_ONE);

                        if (out_left && (!out_right || in_left >= in_right))
                                to[2 * left] -= FONT_ONE, to[2 * left + 1] -= FONT_ONE;
                        else if (out_right)
                                to[2 * right] += FONT_ONE, to[2 * right + 1] += FONT_ONE;
                        else
                                break;
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
        b64 lowest, highest;
        p32 edges = 0, held = 0;
        font_reach(e, &lowest, &highest);
        for (p32 s = 0; s < stems; s++)
                if (!dropped[s])
                {
                        from[edges] = stem[2 * s], onto[edges++] = to[2 * s];
                        from[edges] = stem[2 * s + 1], onto[edges++] = to[2 * s + 1];
                        if (snapped)
                        {
                                snapped[4 * held] = stem[2 * s], snapped[4 * held + 1] = stem[2 * s + 1];
                                snapped[4 * held + 2] = to[2 * s], snapped[4 * held + 3] = to[2 * s + 1];
                                held++;
                        }
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
                return held;

        p32 first = 0;
        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
                for (p32 i = first; i <= e->contour[c].last && !font_still(e, c); i++)
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
        return held;
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
//      snapped and rooms as font_snap_axis says.
static p32 font_snap(font_engine address_to e, const font_face address_to face, p32 size, b32 standard, b64 heavier,
                     b64 address_to snapped, const b64 address_to rooms, p32 room_count)
{
        b64 scale = (b64)size * (FONT_ONE / 64);
        b64 normal = font_divide((b64)standard * scale, (b64)face->units << FONT_EXACT) + heavier;
        b64 x_height = font_divide((b64)(face->x_height > 0 ? face->x_height : face->units / 2) * scale, face->units);

        return font_snap_axis(e, normal, x_height / FONT_ROUND_RUN, scale / 4, null, 0, FONT_ONE * 3 / 4, false, snapped,
                              rooms, room_count);
}

//      And a bar's edge the same: the tops and bottoms of o, e, a, s, g, 3
//      and 8, where a curve is level for a short way, are rows of ink as a
//      straight bar's are.
#define FONT_ROUND_BAR 6

/*
        Dots: a contour of ink (not a counter, holding no other contour's box,
        and inside none but a counter's) from half a pixel to a quarter of the
        em across either way (FONT_DOT_MOST) and no more than twice as long
        one way as the other -- the dots of . : ; ! ? i and j, of umlauts, and
        of a dotted zero -- made whole pixels each way and set on the grid. A
        dot of 1.2 px fell on two rows and two columns at most places and was
        a grey blot; code is full of them.

        Which contours are dots, and their sizes, are taken before the stems
        are snapped (font_dots_measure), and the snap leaves them where they
        are: its map stretched a dot beside a stem (the dot of ! at 10 px went
        from 1.0 to 2.04 px wide), and the knots of a stem it overhung cut
        through it, so its two halves came out different greys.

        A dot over a stem that stayed snapped -- i, j, the dots of u and of !
        -- is set on the stem as it is now: as wide as the stem and as many
        whole pixels more each side as the dot is wider by half, so its
        middle is the stem's. Rounded on its own, a dot of 1.6 px was two
        pixels over a stem of one, and which side its extra column went to
        was the side its centre rounded to, which the pen's eighth decided:
        the dot of i stood a pixel right of its stem at half the places it
        was drawn at 12 to 18 px. A dot inside a counter is set in the middle
        of the counter (font_dot_in). Any other dot is set about its centre.
*/
#define FONT_DOTS 16

//      The widest dot: a quarter of the em, from two and a half pixels, as
//      it always was, to six, i's at 40 to 64 px. Up to two and a half only,
//      a dot of 2.6 px (Inter's at 24 px, a title at twice the scale, Plex
//      Mono Bold's darkened at 13 px) was a stem of its own, snapped about
//      its own middle, and stood off the stem under it by up to 0.8 px. Not a
//      part of the em, the whole body of Plex Mono's i at 10 px (4.5 by 7 px)
//      was a dot.
#define FONT_DOT_LEAST (FONT_ONE * 5 / 2)
#define FONT_DOT_MOST (FONT_ONE * 6)

//      Where a would-be dot lies among the other contours: 0 alone; 1 not a
//      dot, its box holding another's (the outside of a ring, of o or of a
//      small circle of %, whose inside would not follow it) or inside the
//      box of ink; and 2 + k inside counter k, the innermost box holding it
//      -- the dot of a dotted zero, set in its counter (font_dots).
static p32 font_dot_inside(const font_engine address_to e, p32 contour, b64 left, b64 bottom, b64 right, b64 top,
                           p8 ink_sign)
{
        p32 first = 0, inside = 0;
        f64 smallest = 0;

        for (p32 c = 0; c < e->contour_count; first = e->contour[c++].last + 1u)
        {
                b64 l = (b64)1 << 62, b = l, r = -l, t = -l;
                f64 area = 0;

                if (c == contour || e->contour[c].last < first)
                        continue;
                for (p32 i = first; i <= e->contour[c].last; i++)
                {
                        font_point p = e->points[i], q = e->points[i == e->contour[c].last ? first : i + 1];

                        area += (f64)p.x * q.y - (f64)q.x * p.y;
                        l = p.x < l ? p.x : l;
                        r = p.x > r ? p.x : r;
                        b = p.y < b ? p.y : b;
                        t = p.y > t ? p.y : t;
                }
                if (left <= l && right >= r && bottom <= b && top >= t)
                        return 1;
                if (l <= left && r >= right && b <= bottom && t >= top &&
                    (!inside || (f64)(r - l) * (f64)(t - b) < smallest))
                {
                        smallest = (f64)(r - l) * (f64)(t - b);
                        inside = (area < 0) == ink_sign ? 1 : 2 + c;
                }
        }
        return inside;
}

//      Each dot's width and height in sizes, two to a contour (zero for one
//      that is not a dot), and the dots left out of the snap. inner takes,
//      for a dot inside a counter, the counter plus one (zero for any other),
//      and rooms each such dot's left and right, rooms_count of them.
static fn font_dots_measure(font_engine address_to e, p32 size, b64 address_to sizes, p32 address_to inner,
                            b64 address_to rooms, p32 address_to room_count)
{
        f64 whole = 0;
        p32 first = 0;
        b64 most = (b64)size * (FONT_ONE / 64) / 4;

        most = most < FONT_DOT_LEAST ? FONT_DOT_LEAST : most > FONT_DOT_MOST ? FONT_DOT_MOST : most;
        memory_zero(sizes, 2 * FONT_DOTS * sizeof(b64));
        memory_zero(inner, FONT_DOTS * sizeof(p32));
        *room_count = 0;
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
                    width > most || height > most || width > 2 * height || height > 2 * width)
                        continue;

                p32 where = font_dot_inside(e, c, left, bottom, right, top, whole < 0);
                if (where == 1)
                        continue;
                sizes[2 * c] = width;
                sizes[2 * c + 1] = height;
                e->still |= (p64)1 << c;
                if (where)
                {
                        inner[c] = where - 1;
                        rooms[2 * *room_count] = left;
                        rooms[2 * *room_count + 1] = right;
                        ++*room_count;
                }
        }
}

//      Where a dot inside a counter goes, from the counter as the snaps left
//      it: the whole pixels inside its box, with a pixel of air all round,
//      the dot in their middle across, as wide as leaves the same air either
//      side (the width nearer the dot's own area of the two that do), and
//      in their middle up to the nearest pixel. With no room for a pixel of
//      dot and its air the dot is not drawn: the dotted zero of Plex Mono at
//      8 to 14 px had a dot as wide as its counter, a bar at 10 px and a
//      grey fill at 8, the fat oval it was taken for.
static fn font_dot_in(const font_engine address_to e, p32 counter, b64 across, b64 up, b64 width, p8 square,
                      b64 address_to place)
{
        b64 l = (b64)1 << 62, b = l, r = -l, t = -l;
        p32 first = counter ? e->contour[counter - 1].last + 1u : 0;

        for (p32 i = first; i <= e->contour[counter].last; i++)
        {
                l = e->points[i].x < l ? e->points[i].x : l;
                r = e->points[i].x > r ? e->points[i].x : r;
                b = e->points[i].y < b ? e->points[i].y : b;
                t = e->points[i].y > t ? e->points[i].y : t;
        }
        l = (l + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1), b = (b + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1);
        r &= ~(b64)(FONT_ONE - 1), t &= ~(b64)(FONT_ONE - 1);

        b64 room = r - l - 2 * FONT_ONE, high = t - b - 2 * FONT_ONE;
        if (((room - across) >> FONT_SHIFT) & 1)
        {
                f64 want = (f64)width * (f64)width, less = (f64)(across - FONT_ONE), more = (f64)(across + FONT_ONE);

                across = across > FONT_ONE && want - less * less <= more * more - want ? across - FONT_ONE : across + FONT_ONE;
        }
        while (across > room && across > 0)
                across -= 2 * FONT_ONE;
        up = square ? across : up;
        up = up > high ? high : up;
        if (across < FONT_ONE || up < FONT_ONE)
        {
                place[6] = place[7] = 0;
                return;
        }
        place[4] = l + FONT_ONE + (room - across) / 2;
        place[5] = b + FONT_ONE + FONT_STEPS_UP((high - up + FONT_ONE) >> (FONT_SHIFT + 1));
        place[6] = across;
        place[7] = up;
}

//      The dots placed, after both snaps, against the stems that stayed
//      snapped (kept, four places a stem as font_snap_axis gives them, stems
//      of them), the fitted lines (fixes of them), and for a dot inside a
//      counter, the counter (inner, as font_dots_measure gives it).
static fn font_dots(font_engine address_to e, const b64 address_to sizes, const p32 address_to inner,
                    const b64 address_to kept, p32 stems, const b64 address_to lines, p32 fixes)
{
        //      Each dot's box as it is (left, bottom, right, top) and as it
        //      will be (x, y, across, up), and whether a stem holds it.
        b64 box[FONT_DOTS][8];
        p8 held[FONT_DOTS] = {0};
        p32 first = 0;

        for (p32 c = 0; c < e->contour_count && c < FONT_DOTS; first = e->contour[c++].last + 1u)
        {
                p32 last = e->contour[c].last;
                b64 left = (b64)1 << 62, bottom = left, right = -left, top = -left;
                b64 across = font_round(sizes[2 * c]), up = font_round(sizes[2 * c + 1]);

                if (!sizes[2 * c])
                        continue;
                across = across < FONT_ONE ? FONT_ONE : across;
                up = up < FONT_ONE ? FONT_ONE : up;

                //      A dot about as tall as it is wide (within a quarter) is
                //      square, the whole side nearest its area: rounded each way
                //      on its own, a dot of 1.6 by 1.41 px (Plex Mono's at 14 px)
                //      was a dash two pixels long, and ä wore two of them.
                p8 square = 4 * sizes[2 * c] <= 5 * sizes[2 * c + 1] && 4 * sizes[2 * c + 1] <= 5 * sizes[2 * c];
                if (square)
                {
                        f64 area = (f64)sizes[2 * c] * (f64)sizes[2 * c + 1];

                        across = FONT_ONE;
                        while ((f64)across * (f64)across + (f64)(across + FONT_ONE) * (f64)(across + FONT_ONE) < 2 * area)
                                across += FONT_ONE;
                        up = across;
                }
                for (p32 i = first; i <= last; i++)
                {
                        left = e->points[i].x < left ? e->points[i].x : left;
                        right = e->points[i].x > right ? e->points[i].x : right;
                        bottom = e->points[i].y < bottom ? e->points[i].y : bottom;
                        top = e->points[i].y > top ? e->points[i].y : top;
                }

                b64 x = FONT_STEPS_UP((left + right - across + FONT_ONE) >> (FONT_SHIFT + 1));
                b64 y = FONT_STEPS_UP((bottom + top - up + FONT_ONE) >> (FONT_SHIFT + 1));

                if (inner[c])
                {
                        b64 place[8] = {left, bottom, right, top, x, y, across, up};

                        font_dot_in(e, inner[c] - 1, across, up, sizes[2 * c], square, place);
                        memory_copy(box[c], place, sizeof(place));
                        continue;
                }

                //      The stem under its middle, the one it overlaps most: the
                //      two dots of i with diaeresis each overhang its stem, and
                //      set on it both were one.
                b64 most = 0;
                p32 under = stems;
                for (p32 s = 0; s < stems; s++)
                {
                        b64 from = left > kept[4 * s] ? left : kept[4 * s];
                        b64 to = right < kept[4 * s + 1] ? right : kept[4 * s + 1];

                        if (to - from > most && 2 * kept[4 * s] <= left + right && left + right <= 2 * kept[4 * s + 1])
                                most = to - from, under = s;
                }
                if (under < stems)
                {
                        b64 low = kept[4 * under + 2], high = kept[4 * under + 3], stem = high - low;

                        held[c] = true;
                        if (!(stem & (FONT_ONE - 1)) && !(low & (FONT_ONE - 1)))
                        {
                                //      Whole pixels more each side while that
                                //      is nearer the dot's own area than one
                                //      fewer (2.0 px over a stem of one is one
                                //      pixel, not a blot of three), and as tall
                                //      as keeps its shape: a round dot of 1.6
                                //      px over a stem of one is one pixel each
                                //      way, not a bar of two.
                                b64 more = 0;
                                f64 dot = (f64)sizes[2 * c] * (f64)sizes[2 * c];

                                while (more < FONT_DOT_MOST && (f64)(stem + 2 * more) * (f64)(stem + 2 * more) +
                                                                       (f64)(stem + 2 * more + 2 * FONT_ONE) *
                                                                           (f64)(stem + 2 * more + 2 * FONT_ONE) <
                                                                   2 * dot)
                                        more += FONT_ONE;

                                if (square || stem + 2 * more != across)
                                {
                                        up = square ? stem + 2 * more
                                                    : font_round((b64)((f64)sizes[2 * c + 1] * (f64)(stem + 2 * more) /
                                                                       (f64)sizes[2 * c] + 0.5));
                                        up = up < FONT_ONE ? FONT_ONE : up;
                                        y = FONT_STEPS_UP((bottom + top - up + FONT_ONE) >> (FONT_SHIFT + 1));
                                }
                                across = stem + 2 * more;
                                x = low - more;
                        }
                        else
                                x = FONT_STEPS_UP((low + high - across + FONT_ONE) >> (FONT_SHIFT + 1));
                }

                //      A dot clear of a fitted line under it -- i's and j's
                //      of the x-height, an umlaut's of the cap height -- keeps a
                //      pixel of air over it: rounded down onto the line, the
                //      dot of Plex Mono Bold's i at 11 px stood on its stem
                //      (and at 9 px dark, where the fit took the x-height to
                //      0.2 px under it; the dots of A with diaeresis, 0.02 px
                //      over the cap height at 10 px dark, stood on its apex).
                //      A dot on the baseline -- under . ! ? : -- stays on it,
                //      whatever its height came to.
                for (p32 f = 0; f < fixes; f++)
                        if (f && bottom >= lines[f] - FONT_ONE / 8 && y < lines[f] + FONT_ONE)
                                y = lines[f] + FONT_ONE;
                        else if (!f && bottom > lines[f] - FONT_ONE / 8 && bottom < lines[f] + FONT_ONE / 8)
                                y = lines[f];
                        else if (!f && bottom >= lines[f] + FONT_ONE / 8 && y < lines[f] + FONT_ONE)
                                y = lines[f] + FONT_ONE;

                b64 place[8] = {left, bottom, right, top, x, y, across, up};
                memory_copy(box[c], place, sizeof(place));
        }

        //      Two dots side by side and apart (by a sixteenth of a pixel; the
        //      dots of ä are 0.21 px apart darkened at 8 px) -- an umlaut, a
        //      diaeresis -- keep a pixel between them. Each rounded
        //      about its own middle, the two of A with diaeresis at 14 px, 1.0
        //      px apart, met in a bar four pixels long, and ä read as a with
        //      a macron: the one rounded the further toward the other, and
        //      held by no stem, goes back.
        for (p32 a = 0; a < e->contour_count && a < FONT_DOTS; a++)
                for (p32 b = 0; b < e->contour_count && b < FONT_DOTS; b++)
                {
                        if (!sizes[2 * a] || !sizes[2 * b] || inner[a] || inner[b] || box[a][2] > box[b][0] ||
                            box[b][0] - box[a][2] < FONT_ONE / 16 || box[a][5] >= box[b][5] + box[b][7] ||
                            box[b][5] >= box[a][5] + box[a][7] || box[b][4] - (box[a][4] + box[a][6]) >= FONT_ONE ||
                            (held[a] && held[b]))
                                continue;

                        //      Twins, as the dots of an umlaut are, are one size:
                        //      the size of the one a stem holds, else the larger,
                        //      the other set about its own middle again. Over
                        //      the stem of a at 8 px one dot of ä was two pixels
                        //      wide and the other one.
                        if (4 * (sizes[2 * a] - sizes[2 * b]) <= sizes[2 * a] &&
                            4 * (sizes[2 * b] - sizes[2 * a]) <= sizes[2 * b] &&
                            4 * (sizes[2 * a + 1] - sizes[2 * b + 1]) <= sizes[2 * a + 1] &&
                            4 * (sizes[2 * b + 1] - sizes[2 * a + 1]) <= sizes[2 * b + 1])
                        {
                                p32 big = held[a] ? a : held[b] ? b : box[a][6] * box[a][7] >= box[b][6] * box[b][7] ? a : b;
                                p32 other = big == a ? b : a;

                                if (box[other][6] != box[big][6] || box[other][7] != box[big][7])
                                {
                                        box[other][4] = FONT_STEPS_UP((box[other][0] + box[other][2] - box[big][6] + FONT_ONE) >>
                                                                      (FONT_SHIFT + 1));
                                        box[other][5] = box[big][5] + FONT_STEPS_UP((box[other][1] + box[other][3] - box[big][1] -
                                                                                     box[big][3] + FONT_ONE) >>
                                                                                    (FONT_SHIFT + 1));
                                        box[other][6] = box[big][6];
                                        box[other][7] = box[big][7];
                                }
                                if (box[b][4] - (box[a][4] + box[a][6]) >= FONT_ONE)
                                        continue;
                        }

                        //      How far each middle went toward the other, rounded.
                        b64 went_a = 2 * box[a][4] + box[a][6] - box[a][0] - box[a][2];
                        b64 went_b = box[b][0] + box[b][2] - 2 * box[b][4] - box[b][6];
                        b64 need = FONT_ONE - (box[b][4] - (box[a][4] + box[a][6]));

                        if (!held[a] && (held[b] || went_a >= went_b))
                                box[a][4] -= need;
                        else
                                box[b][4] += need;
                }

        first = 0;
        for (p32 c = 0; c < e->contour_count && c < FONT_DOTS; first = e->contour[c++].last + 1u)
        {
                //      A contour that is no dot has no box: the first loop wrote none for it.
                if (!sizes[2 * c])
                        continue;

                b64 width = box[c][2] - box[c][0], height = box[c][3] - box[c][1];

                if (width <= 0 || height <= 0)
                        continue;
                for (p32 i = first; i <= e->contour[c].last; i++)
                {
                        f64 u = (f64)(e->points[i].x - box[c][0]) * (f64)box[c][6] / (f64)width;
                        f64 v = (f64)(e->points[i].y - box[c][1]) * (f64)box[c][7] / (f64)height;

                        e->points[i].x = box[c][4] + (b64)(u + 0.5);
                        e->points[i].y = box[c][5] + (b64)(v + 0.5);
                }
        }
        e->still = 0;
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
        font_snap_axis(e, normal, x_height / FONT_ROUND_BAR, scale / 4, fixed, fixes, FONT_BAR_WHOLE, true, null, null, 0);
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
        p8 right = xb > xa;
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
static inline fn font_piece(font_engine address_to e, font_hand address_to h, p8 inside,
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
static inline fn font_pieces(font_engine address_to e, font_hand address_to h, p8 inside,
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

        b64 n = (b64)square_root((f64)bow / FONT_FLAT);
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
        p8 inside = e->direct;
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
static p8 font_tracking(font_engine address_to e)
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

        p8 meet = false;

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
                p8 pending = false;

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
        p8 rising = false;

        //      The row's edges, clipped to it, joined into chains: a piece
        //      that starts where the one before it ended, going the same way
        //      up or down, continues its chain. A chain's points are kept in
        //      rising order and its winding in the top bit of its start.
        for (p32 at = s->listing[r - e->band]; at < s->listing[r - e->band + 1]; at++)
        {
                p32 index = s->listed[at];
                const b32 address_to g = s->group[index];
                p8 up = g[1] < g[3];
                f64 ya = up ? g[1] : g[3], yb = up ? g[3] : g[1], xa = up ? g[0] : g[2], xb = up ? g[2] : g[0];

                if (yb <= low || ya >= high || ya == yb)
                        continue;

                f64 a = ya > low ? ya : low, b = yb < high ? yb : high, slope = (xb - xa) / (yb - ya);
                f64 xl = xa + (a - ya) * slope, xh = xa + (b - ya) * slope;
                p8 joins = chains && index == previous + 1 && up == rising && s->group[index - 1][2] == g[0] &&
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
static p8 font_list(font_engine address_to e)
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
        e->still = 0;
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
        b64 dots[2 * FONT_DOTS], kept[4 * FONT_STEMS], rooms[2 * FONT_DOTS];
        p32 stems = 0, inner[FONT_DOTS], room_count = 0;
        if (style & 1)
                font_dots_measure(e, size, dots, inner, rooms, &room_count);
        b64 thin = 0;
        if (standard && !(style & 8))
        {
                b64 normal = font_divide((b64)standard * size * (FONT_ONE / 64), (b64)face->units << FONT_EXACT) + heavier;

                thin = normal >= FONT_ONE / 2 && normal < FONT_ONE * 3 / 4 && FONT_THIN_TO > normal ? FONT_THIN_TO - normal : 0;
                if (thin > 0)
                        font_embolden(e, (f32)thin, true);
                stems = font_snap(e, face, size, standard, heavier + thin, kept, rooms, room_count);
        }
        if (fixes && zones[3])
                font_bars(e, face, size, zones[3], lines, fixes, heavier);
        if (style & 1)
                font_dots(e, dots, inner, kept, stems, lines, fixes);

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
static fn font_put(font_engine address_to e, f32 x, f32 y, b32 height, p8 on)
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
        f32 dx = bx - ax, dy = by - ay, length = narrow_square_root(dx * dx + dy * dy);
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

static p8 font_draw_shape(font_engine address_to e, p32 code, b32 width, b32 height, b32 light,
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
                p8 hollow = !!(kind & SHAPE_HOLLOW);

                if (shape == SHAPE_CIRCLE)
                {
                        //      In a box of whole pixels, so its edge is the same grey all round.
                        f32 left = narrow_rounded(cx - size), right = narrow_rounded(cx + size);
                        f32 top = narrow_rounded(cy - size);

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
                                corner[k][0] = narrow_rounded(cx + size * v[k][0]);
                                corner[k][1] = narrow_rounded(cy + size * v[k][1]);
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

static p8 font_drawn(font_engine address_to e, p32 code, b32 width, b32 height,
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
                b32 across = (b32)narrow_rounded(2 * radius);

                across = across < 1 ? 1 : across;
                radius = (f32)across / 2;
                for (p32 dot = 0; dot < 8; dot++)
                {
                        if (!(code & 1u << dot))
                                continue;

                        p32 column = dot >= 3 && dot != 6, row = dot >= 6 ? 3 : dot % 3;
                        b32 left = (b32)narrow_rounded(width * (1 + 2 * column) / 4.0f - radius);
                        b32 top = (b32)narrow_rounded(height * (1 + 2 * row) / 8.0f - radius);
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
                f32 length = narrow_square_root((f32)width * width + (f32)height * height);

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

                p8 across = arm >= 2, far = arm & 1;
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
                p8 across = !(code & 2);
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

static p8 font_room(font_engine address_to e, b32 width, b32 height, p16 address_to x, p16 address_to y)
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

//      A slot's bitmap as the lookup answers it.
static inline font_bitmap font_slot_box(const font_engine address_to e, const font_slot address_to slot)
{
        return (font_bitmap){slot->width ? e->atlas + (positive)slot->y * e->atlas_width + slot->x : null,
                             slot->x == 0xffff ? 1 : e->atlas_width, slot->left, slot->top, slot->width, slot->height};
}

//      The slow half of font_cached: the slots, then a new one. A slot found
//      is remembered in the way it was asked for, which then answers it as
//      long as the atlas is the one it was found in.
static __attribute__((used)) font_bitmap font_cached_find_reference(font_engine address_to e, const font_face address_to face,
                                    p32 code, p32 size, p32 phase, p32 style, p64 cell, p64 key,
                                    font_cache_way address_to way)
{
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
                {
                        *way = (font_cache_way){key, cell, e->generation, e->index[home]};
                        return font_slot_box(e, slot);
                }
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

//      font_cached_find: the lookup's hit path, in assembly, for arm64 and x86-64 (SysV, not Windows).
//      The hit is the reference's own first loop: the key and cell in the index's chain from the hash's
//      home, the way written as the reference writes it, and the box built as font_slot_box builds it.
//      Anything else -- a size the lookup refuses, a cache that would reset first, or a miss that makes a
//      slot -- is a tail call to font_cached_find_reference with the arguments untouched, which alone
//      decides it. Nothing is written before the hit is known, so the reference sees the cache as it was.
//      The offsets are font_engine's, font_slot's, font_cache_way's and font_bitmap's, checked below.
#if defined(__APPLE__)
#define FONT_SYM(name) "_" #name
#define FONT_TYPE(name)
#define FONT_SIZE(name)
#else
#define FONT_SYM(name) #name
#define FONT_TYPE(name) ".type " FONT_SYM(name) ",@function\n"
#define FONT_SIZE(name) ".size " FONT_SYM(name) ", .-" FONT_SYM(name) "\n"
#endif

#if defined(__aarch64__) && !defined(_WIN32)
_Static_assert(offset_of(font_engine, index) == 629112 && offset_of(font_engine, slots) == 530808 &&
               offset_of(font_engine, atlas) == 24 && offset_of(font_engine, atlas_width) == 32 &&
               offset_of(font_engine, slot_count) == 48 && offset_of(font_engine, scratched) == 53 &&
               offset_of(font_engine, generation) == 64 && sizeof(font_slot) == 32 &&
               offset_of(font_slot, x) == 16 && offset_of(font_slot, y) == 18 &&
               offset_of(font_slot, width) == 20 && offset_of(font_slot, height) == 22 &&
               offset_of(font_slot, left) == 24 && offset_of(font_slot, top) == 26 &&
               sizeof(font_cache_way) == 24 && offset_of(font_cache_way, generation) == 16 &&
               offset_of(font_cache_way, slot) == 20 && sizeof(font_bitmap) == 32 &&
               offset_of(font_bitmap, stride) == 8 && offset_of(font_bitmap, left) == 16 &&
               offset_of(font_bitmap, top) == 20 && offset_of(font_bitmap, width) == 24 &&
               offset_of(font_bitmap, height) == 28,
               "font_cached_find's assembly: the layouts it reads and writes");

font_bitmap font_cached_find(font_engine address_to e, const font_face address_to face, p32 code, p32 size, p32 phase,
                             p32 style, p64 cell, p64 key, font_cache_way address_to way);
// arm64 (AAPCS64): x0 e, x1 face, x3 size, x6 cell, x7 key, [sp] way, x8 the box's buffer. Scratch x9-x15 only.
__asm__(".text\n"
        ".globl " FONT_SYM(font_cached_find) "\n"
        ".p2align 2\n"
        FONT_SYM(font_cached_find) ":\n"
        "cbz x1, 1f\n"
        "cmp w3, #0x100, lsl #12\n"               // face && size >= FONT_SIZE_LIMIT
        "b.hs 9f\n"
        "1:\n"
        "ldrb w9, [x0, #53]\n"                    // e->scratched
        "cbnz w9, 9f\n"
        "ldr w9, [x0, #48]\n"                     // e->slot_count >= FONT_SLOTS * 3 / 4
        "cmp w9, #0xc00\n"
        "b.hs 9f\n"
        "eor x10, x6, x7\n"                       // home = (key ^ cell) * golden >> 52
        "movz x11, #0x7c15\n"
        "movk x11, #0x7f4a, lsl #16\n"
        "movk x11, #0x79b9, lsl #32\n"
        "movk x11, #0x9e37, lsl #48\n"
        "mul x10, x10, x11\n"
        "lsr x10, x10, #52\n"
        "add x12, x0, #0x99, lsl #12\n"           // x12 = e->index
        "add x12, x12, #0x978\n"
        "add x13, x0, #0x81, lsl #12\n"           // x13 = e->slots
        "add x13, x13, #0x978\n"
        "2:\n"
        "ldrh w14, [x12, x10, lsl #1]\n"          // w14 = index[home], 1-based; 0 is a miss
        "cbz w14, 9f\n"
        "sub w9, w14, #1\n"
        "lsl x9, x9, #5\n"
        "add x9, x13, x9\n"                       // x9 = &slots[index - 1]
        "ldr x15, [x9]\n"
        "cmp x15, x7\n"
        "b.ne 3f\n"
        "ldr x15, [x9, #8]\n"
        "cmp x15, x6\n"
        "b.eq 4f\n"
        "3:\n"
        "add x10, x10, #1\n"
        "and x10, x10, #4095\n"
        "b 2b\n"
        "4:\n"                                    // hit: the way, then the box
        "ldr x15, [sp]\n"
        "str x7, [x15]\n"
        "str x6, [x15, #8]\n"
        "ldr w11, [x0, #64]\n"                    // e->generation
        "str w11, [x15, #16]\n"
        "strh w14, [x15, #20]\n"
        "ldrh w10, [x9, #16]\n"                   // slot->x
        "ldrh w11, [x9, #18]\n"                   // slot->y
        "ldrh w12, [x9, #20]\n"                   // slot->width
        "ldrh w13, [x9, #22]\n"                   // slot->height
        "ldrsh w14, [x9, #24]\n"                  // slot->left, b16 widened to b32
        "ldrsh w15, [x9, #26]\n"                  // slot->top
        "ldr w9, [x0, #32]\n"                     // e->atlas_width
        "ldr x0, [x0, #24]\n"                     // e->atlas
        "str w14, [x8, #16]\n"
        "str w15, [x8, #20]\n"
        "str w12, [x8, #24]\n"
        "str w13, [x8, #28]\n"
        "umaddl x11, w11, w9, x0\n"               // atlas + y * atlas_width
        "add x11, x11, w10, uxtw\n"               // + x
        "cmp w12, #0\n"
        "csel x11, xzr, x11, eq\n"                // no width, no coverage
        "movz w13, #0xffff\n"
        "cmp w10, w13\n"
        "csinc x10, x9, xzr, ne\n"                // stride: atlas_width, or 1 when x is 0xffff
        "str x11, [x8]\n"
        "str x10, [x8, #8]\n"
        "mov x0, x8\n"
        "ret\n"
        "9:\n"
        "b " FONT_SYM(font_cached_find_reference) "\n"
        FONT_TYPE(font_cached_find)
        FONT_SIZE(font_cached_find));
#elif defined(__x86_64__) && !defined(_WIN32)
_Static_assert(offset_of(font_engine, index) == 629112 && offset_of(font_engine, slots) == 530808 &&
               offset_of(font_engine, atlas) == 24 && offset_of(font_engine, atlas_width) == 32 &&
               offset_of(font_engine, slot_count) == 48 && offset_of(font_engine, scratched) == 53 &&
               offset_of(font_engine, generation) == 64 && sizeof(font_slot) == 32 &&
               offset_of(font_slot, x) == 16 && offset_of(font_slot, y) == 18 &&
               offset_of(font_slot, width) == 20 && offset_of(font_slot, height) == 22 &&
               offset_of(font_slot, left) == 24 && offset_of(font_slot, top) == 26 &&
               sizeof(font_cache_way) == 24 && offset_of(font_cache_way, generation) == 16 &&
               offset_of(font_cache_way, slot) == 20 && sizeof(font_bitmap) == 32 &&
               offset_of(font_bitmap, stride) == 8 && offset_of(font_bitmap, left) == 16 &&
               offset_of(font_bitmap, top) == 20 && offset_of(font_bitmap, width) == 24 &&
               offset_of(font_bitmap, height) == 28,
               "font_cached_find's assembly: the layouts it reads and writes");

font_bitmap font_cached_find(font_engine address_to e, const font_face address_to face, p32 code, p32 size, p32 phase,
                             p32 style, p64 cell, p64 key, font_cache_way address_to way);
// x86-64 (SysV): rdi the box's buffer, rsi e, rdx face, r8 size, then the stack from [rsp+8]: style, cell, key, way.
// rbx, r12 and r13 are saved on entry; a tail call restores them first, so the reference gets the stack as it was.
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl " FONT_SYM(font_cached_find) "\n"
        ".p2align 4\n"
        FONT_SYM(font_cached_find) ":\n"
        "push rbx\n"
        "push r12\n"
        "push r13\n"
        "test rdx, rdx\n"
        "jz 1f\n"
        "cmp r8d, 0x100000\n"                     // face && size >= FONT_SIZE_LIMIT
        "jae 9f\n"
        "1:\n"
        "cmp byte ptr [rsi + 53], 0\n"            // e->scratched
        "jne 9f\n"
        "cmp dword ptr [rsi + 48], 3072\n"        // e->slot_count >= FONT_SLOTS * 3 / 4
        "jae 9f\n"
        "mov r13, qword ptr [rsp + 40]\n"         // cell
        "mov rbx, qword ptr [rsp + 48]\n"         // key
        "mov r12, r13\n"
        "xor r12, rbx\n"
        "movabs rax, 0x9e3779b97f4a7c15\n"
        "imul r12, rax\n"
        "shr r12, 52\n"                           // home
        "2:\n"
        "movzx eax, word ptr [rsi + r12*2 + 629112]\n"   // e->index[home], 1-based; 0 is a miss
        "test eax, eax\n"
        "jz 9f\n"
        "lea r10, [rax - 1]\n"
        "shl r10, 5\n"
        "lea r10, [rsi + r10 + 530808]\n"         // r10 = &e->slots[index - 1]
        "cmp qword ptr [r10], rbx\n"
        "jne 3f\n"
        "cmp qword ptr [r10 + 8], r13\n"
        "je 4f\n"
        "3:\n"
        "add r12, 1\n"
        "and r12, 4095\n"
        "jmp 2b\n"
        "4:\n"                                    // hit: the way, then the box
        "mov r11, qword ptr [rsp + 56]\n"         // way
        "mov qword ptr [r11], rbx\n"
        "mov qword ptr [r11 + 8], r13\n"
        "mov edx, dword ptr [rsi + 64]\n"         // e->generation
        "mov dword ptr [r11 + 16], edx\n"
        "mov word ptr [r11 + 20], ax\n"
        "movzx ecx, word ptr [r10 + 16]\n"        // slot->x
        "movzx edx, word ptr [r10 + 18]\n"        // slot->y
        "movzx r8d, word ptr [r10 + 20]\n"        // slot->width
        "movzx r9d, word ptr [r10 + 22]\n"        // slot->height
        "movsx eax, word ptr [r10 + 24]\n"        // slot->left, b16 widened to b32
        "movsx r12d, word ptr [r10 + 26]\n"       // slot->top
        "mov r11d, dword ptr [rsi + 32]\n"        // e->atlas_width
        "mov rsi, qword ptr [rsi + 24]\n"         // e->atlas
        "imul rdx, r11\n"
        "add rdx, rcx\n"
        "add rdx, rsi\n"                          // atlas + y * atlas_width + x
        "test r8d, r8d\n"
        "jnz 5f\n"
        "xor edx, edx\n"                          // no width, no coverage
        "5:\n"
        "mov esi, 1\n"
        "cmp ecx, 0xffff\n"
        "je 6f\n"
        "mov esi, r11d\n"                         // stride: atlas_width, or 1 when x is 0xffff
        "6:\n"
        "mov qword ptr [rdi], rdx\n"
        "mov qword ptr [rdi + 8], rsi\n"
        "mov dword ptr [rdi + 16], eax\n"
        "mov dword ptr [rdi + 20], r12d\n"
        "mov dword ptr [rdi + 24], r8d\n"
        "mov dword ptr [rdi + 28], r9d\n"
        "mov rax, rdi\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        "ret\n"
        "9:\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        "jmp " FONT_SYM(font_cached_find_reference) "\n"
        FONT_TYPE(font_cached_find)
        FONT_SIZE(font_cached_find)
        ".att_syntax prefix\n");
#else
static font_bitmap font_cached_find(font_engine address_to e, const font_face address_to face, p32 code, p32 size,
                                    p32 phase, p32 style, p64 cell, p64 key, font_cache_way address_to way)
{
        return font_cached_find_reference(e, face, code, size, phase, style, cell, key, way);
}
#endif

//      The cached coverage for a glyph, or for a drawn cell when face is
//      null, with code the code point and size the cell's width << 16 |
//      height. With cell, a glyph's whole terminal cell instead -- cell
//      packs its width, height, baseline and the glyph's pixel offset in
//      it, 16 bits each -- or nothing with stride 1 when the glyph does not
//      fit inside it. An empty bitmap when there is nothing to draw.
static inline INLINE font_bitmap font_cached(font_engine address_to e, const font_face address_to face,
                                             p32 code, p32 size, p32 phase, p32 style, p64 cell)
{
        p64 key = (p64)1 << 63 | (p64)(face ? face->id : 0) << 47 | (p64)(code & 0xffff) << 31 |
                  (p64)(face ? size & 0x3fffff : (size >> 16 & 0x7ff) << 11 | (size & 0x7ff)) << 9 |
                  (p64)(phase & 7) << 6 | (p64)(style & 7) << 3 | (p64)(style >> 3 & 3);
        font_cache_way address_to way = &e->cached[((key ^ cell) * 0x9e3779b97f4a7c15ull) >> 56];

        //      The way answers when it is this key and cell, found in this
        //      atlas, and the lookup would not reset or refuse first.
        if (way->key == key && way->cell == cell && way->generation == e->generation && !e->scratched &&
            e->slot_count < FONT_SLOTS * 3 / 4 && (!face || size < FONT_SIZE_LIMIT))
                return font_slot_box(e, &e->slots[way->slot - 1]);
        return font_cached_find(e, face, code, size, phase, style, cell, key, way);
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
//      The sRGB byte of every linear light value, font_srgb's answer as one
//      table: the library's mix looks a channel's light up here.
static p8 font_srgb_bytes[65536];

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
        for (p32 lin = 0; lin < 65536; lin++)
        {
                p32 word = font_to_srgb[lin >> 4];

                font_srgb_bytes[lin] = (p8)((word & 255) + ((lin & 15) >= (word >> 8)));
        }
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
static __attribute__((used)) fn font_ramp(font_engine address_to e, p32 colour, p32 background, p32 strength)
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
static __attribute__((used)) p32 font_mix(const font_engine address_to e, p32 back, p32 fore, p32 a)
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
static __attribute__((used)) const p32 address_to font_tones_reference(font_engine address_to e, p32 colour, p32 background, p32 strength)
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

//      font_tones: the hit path, in assembly, for arm64 and x86-64 (SysV, not Windows). The key is the reference's, built
//      as it builds it: the strength clamped to 255, the linear flag at bit 56, the colour's and the ground's 24 bits, and
//      bit 63. The set is the hash's top four bits, and its ways are the pairings 4 * 2064 bytes apart from the pairs. A
//      way with the key answers with its pixel table, 8 bytes into the pairing, the reference's own pointer. Nothing is
//      written on a hit, so the round-robin turns stay as they were. Anything else -- a miss, which makes a way -- is a
//      tail call to font_tones_reference with the arguments untouched, and it alone fills the way. Bit 63 is always set,
//      so a way never filled (its key 0) cannot match. The offsets are font_engine's and font_pairing's, checked below.
#if (defined(__aarch64__) || defined(__x86_64__)) && !defined(_WIN32)
_Static_assert(offset_of(font_engine, linear) == 20 && offset_of(font_engine, pairs) == 643448 &&
               sizeof(font_pairing) == 2064 && offset_of(font_pairing, key) == 0 &&
               offset_of(font_pairing, pixel) == 8 && FONT_PAIRS == 64 && FONT_WAYS == 4 &&
               FONT_PAIRS / FONT_WAYS == 16 && sizeof(font_pairing) * FONT_WAYS == 8256 && sizeof(p8) == 1,
               "font_tones' assembly: the layouts it reads, and the 16 sets of 4 ways it probes");
#endif

#if defined(__aarch64__) && !defined(_WIN32)
const p32 address_to font_tones(font_engine address_to e, p32 colour, p32 background, p32 strength);
// arm64 (AAPCS64): x0 e, w1 colour, w2 background, w3 strength; the arguments are left as they came for the tail call.
// Scratch x12-x16 only; a leaf with no stack.
__asm__(".text\n"
        ".globl " FONT_SYM(font_tones) "\n"
        ".p2align 2\n"
        FONT_SYM(font_tones) ":\n"
        "mov w12, #255\n"
        "cmp w3, #255\n"
        "csel w12, w3, w12, ls\n"                 // strength = min(strength, 255)
        "and w13, w1, #0xffffff\n"                // colour & 0xffffff << 24
        "lsl x13, x13, #24\n"
        "and w14, w2, #0xffffff\n"                // background & 0xffffff
        "orr x13, x13, x14\n"
        "lsl x14, x12, #48\n"                     // strength << 48
        "orr x13, x13, x14\n"
        "ldrb w14, [x0, #20]\n"                   // e->linear, a bool, << 56
        "lsl x14, x14, #56\n"
        "orr x13, x13, x14\n"
        "orr x13, x13, #0x8000000000000000\n"     // the key's bit 63: x13 = key
        "movz x15, #0x7c15\n"                     // set = key * 0x9e3779b97f4a7c15 >> 60
        "movk x15, #0x7f4a, lsl #16\n"
        "movk x15, #0x79b9, lsl #32\n"
        "movk x15, #0x9e37, lsl #48\n"
        "mul x15, x13, x15\n"
        "lsr x15, x15, #60\n"
        "movz x16, #8256\n"                       // 4 ways of 2064 bytes
        "mul x15, x15, x16\n"
        "add x15, x0, x15\n"
        "add x15, x15, #0x9d, lsl #12\n"          // x15 = &e->pairs[set * FONT_WAYS], pairs at 643448
        "add x15, x15, #0x178\n"
        "ldr x16, [x15]\n"                        // way 0
        "cmp x16, x13\n"
        "b.eq 10f\n"
        "ldr x16, [x15, #2064]\n"                 // way 1
        "cmp x16, x13\n"
        "b.eq 11f\n"
        "ldr x16, [x15, #4128]\n"                 // way 2
        "cmp x16, x13\n"
        "b.eq 12f\n"
        "ldr x16, [x15, #6192]\n"                 // way 3
        "cmp x16, x13\n"
        "b.eq 13f\n"
        "b " FONT_SYM(font_tones_reference) "\n"  // a miss: the reference makes the way
        "10:\n"
        "add x0, x15, #8\n"                       // way 0's pixel table
        "ret\n"
        "11:\n"
        "add x0, x15, #2072\n"
        "ret\n"
        "12:\n"
        "movz x16, #4136\n"
        "add x0, x15, x16\n"
        "ret\n"
        "13:\n"
        "movz x16, #6200\n"
        "add x0, x15, x16\n"
        "ret\n"
        FONT_TYPE(font_tones)
        FONT_SIZE(font_tones));
#elif defined(__x86_64__) && !defined(_WIN32)
const p32 address_to font_tones(font_engine address_to e, p32 colour, p32 background, p32 strength);
// x86-64 (SysV): rdi e, esi colour, edx background, ecx strength. The arguments are left as they came for the tail call;
// rax and r8-r11 are scratch, and there is no stack to restore.
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl " FONT_SYM(font_tones) "\n"
        ".p2align 4\n"
        FONT_SYM(font_tones) ":\n"
        "mov r8d, ecx\n"
        "mov r9d, 255\n"
        "cmp r8d, 255\n"
        "cmova r8d, r9d\n"                        // strength = min(strength, 255)
        "mov r10d, esi\n"
        "and r10d, 0xffffff\n"                    // colour & 0xffffff << 24
        "shl r10, 24\n"
        "mov r11d, edx\n"
        "and r11d, 0xffffff\n"                    // background & 0xffffff
        "or r10, r11\n"
        "shl r8, 48\n"                            // strength << 48
        "or r10, r8\n"
        "movzx eax, byte ptr [rdi + 20]\n"        // e->linear, a bool, << 56
        "shl rax, 56\n"
        "or r10, rax\n"
        "bts r10, 63\n"                           // r10 = key
        "mov r11, r10\n"                          // set = key * 0x9e3779b97f4a7c15 >> 60
        "movabs rax, 0x9e3779b97f4a7c15\n"
        "imul r11, rax\n"
        "shr r11, 60\n"
        "imul r11, r11, 8256\n"                   // 4 ways of 2064 bytes
        "lea r11, [rdi + r11 + 643448]\n"         // r11 = &e->pairs[set * FONT_WAYS]
        "cmp qword ptr [r11], r10\n"
        "je 10f\n"
        "cmp qword ptr [r11 + 2064], r10\n"
        "je 11f\n"
        "cmp qword ptr [r11 + 4128], r10\n"
        "je 12f\n"
        "cmp qword ptr [r11 + 6192], r10\n"
        "je 13f\n"
        "jmp " FONT_SYM(font_tones_reference) "\n"   // a miss: the reference makes the way
        "10:\n"
        "lea rax, [r11 + 8]\n"                    // way 0's pixel table
        "ret\n"
        "11:\n"
        "lea rax, [r11 + 2072]\n"
        "ret\n"
        "12:\n"
        "lea rax, [r11 + 4136]\n"
        "ret\n"
        "13:\n"
        "lea rax, [r11 + 6200]\n"
        "ret\n"
        FONT_TYPE(font_tones)
        FONT_SIZE(font_tones)
        ".att_syntax prefix\n");
#else
static const p32 address_to font_tones(font_engine address_to e, p32 colour, p32 background, p32 strength)
{
        return font_tones_reference(e, colour, background, strength);
}
#endif

//      A glyph's coverage onto the target at x, y: through the pair's tones
//      where the pixel under it is still the background, by the full mix
//      where another glyph has already drawn there or the ground is unknown.
//      On a known ground the library lays the tones and stops at a pixel
//      that is not the ground; that one is mixed here and the rest resumed.
static __attribute__((used)) fn font_blend(font_engine address_to e, const font_target address_to target,
                     b32 x, b32 y, font_bitmap glyph, p32 colour, p32 background, p32 strength)
{
        b32 x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
        b32 x1 = x + glyph.width > target->width ? target->width - x : glyph.width;
        b32 y1 = y + glyph.height > target->height ? target->height - y : glyph.height;
        p8 known = !(background & FONT_UNKNOWN);

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
        //      Each covered pixel is font_mix of itself and the colour: the library
        //      lays the rows with the same weight and tables (memory_mix_u32_rect).
        memory_mix_u32_rect(into, stride, from, glyph.stride, width, height, colour, e->linear ? e->plain : 64,
                            e->ramp, font_to_linear, font_srgb_bytes);
}

/*
        A glyph at x, y of a run's own upright target (target->height wide
        and target->width high) onto the target turned a quarter, as
        font_draw_turned says. A column of the glyph's coverage is a row of
        the turned glyph, byte for byte, so the turn moves bytes and makes
        none: the rows are laid in the strip, as many as it holds at once,
        and each band of them blended as an upright glyph is, over what is
        on the target there. A glyph is turned as it is laid and not cached
        turned: a run turned is the same glyphs as the run upright, and the
        tabs' atlas is 128 pixels square.
*/
static fn font_blend_turned(font_engine address_to e, const font_target address_to target, p32 turn, b32 x, b32 y,
                            font_bitmap glyph, p32 colour, p32 background, p32 strength)
{
        p8 right = turn == FONT_TOPS_RIGHT;
        b32 left = right ? target->width - y - glyph.height : y;
        b32 most = (b32)(FONT_STRIP / (positive)(glyph.height > 0 ? glyph.height : 1));

        if (!glyph.coverage || glyph.width <= 0 || glyph.height <= 0 || left >= target->width ||
            left + glyph.height <= 0)
                return;
        for (b32 from = 0; from < glyph.width; from += most)
        {
                b32 rows = glyph.width - from < most ? glyph.width - from : most;
                //      The band's first row on the target: the glyph's column from turned right, or the band's
                //      last column turned left.
                b32 top = right ? x + from : target->height - x - from - rows;

                if (top >= target->height || top + rows <= 0)
                        continue;
                for (b32 k = 0; k < rows; k++)
                {
                        const p8 address_to column = glyph.coverage + (positive)(right ? from + k : from + rows - 1 - k);
                        p8 address_to row = e->strip + (positive)k * (positive)glyph.height;

                        for (b32 r = 0; r < glyph.height; r++)
                                row[right ? glyph.height - 1 - r : r] = column[(positive)r * glyph.stride];
                }
                font_blend(e, target, left, top, (font_bitmap){e->strip, (positive)glyph.height, 0, 0, glyph.height, rows},
                           colour, background, strength);
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

static p8 font_is_drawn(p32 code)
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

static __attribute__((used)) b32 font_kern_reference(font_engine address_to e, const font_face address_to face, p32 left, p32 right, p32 size)
{
        //      The way holds 16 bits of each glyph: a pair past that is looked up, not memo'd.
        if (left > 0xffff || right > 0xffff)
                return font_kerning(face, left, right, size);

        p64 hash = ((p64)face->id << 32 | left << 16 | right) * 0x9e3779b97f4a7c15ull;
        font_kern_way address_to ways = e->kern_memo[(hash >> 55) & (FONT_KERN_SETS - 1)];
        font_kern_way address_to way = null, address_to empty = null;

        for (p32 w = 0; w < FONT_WAYS; w++)
        {
                if (ways[w].face == face->id && ways[w].left == left && ways[w].right == right)
                {
                        way = &ways[w];
                        break;
                }
                if (!ways[w].face && !empty)
                        empty = &ways[w];
        }
        if (!way)
        {
                //      A new pair takes an empty way; a full set gives up the way the hash names.
                way = empty ? empty : &ways[(hash >> 53) & (FONT_WAYS - 1)];
                *way = (font_kern_way){(p16)face->id, (p16)left, (p16)right, font_kerning_units(face, left, right)};
        }
        return way->value ? font_fixed(face, way->value, size) : 0;
}

//      font_kern: the memo's hit path, in assembly, for arm64 and x86-64 (SysV, not Windows). A pair is a hit when
//      its way is in the set the hash names: the face's id, the two glyphs, then the adjustment in font units, which
//      font_fixed scales to 16.16 as the reference does (the same signed division, rounded as font_divide rounds, and
//      the same clamp). Anything else -- no engine or face, a glyph past the way's 16 bits, a miss, a face with no
//      units -- is a tail call to font_kern_reference with the arguments untouched, which alone decides it (a miss
//      writes the way). Nothing is written on a hit, so the reference sees the memo as it was.
//      The offsets are font_engine's kern_memo, font_face's units and id, and the way's layout, checked below.
#if (defined(__aarch64__) || defined(__x86_64__)) && !defined(_WIN32)
_Static_assert(offset_of(font_engine, kern_memo) == 783736 && offset_of(font_face, units) == 104 &&
               offset_of(font_face, id) == 108 && sizeof(font_kern_way) == 12 &&
               offset_of(font_kern_way, face) == 0 && offset_of(font_kern_way, left) == 2 &&
               offset_of(font_kern_way, right) == 4 && offset_of(font_kern_way, value) == 8 &&
               FONT_KERN_SETS == 512 && FONT_WAYS == 4 && FONT_FIXED_MOST == 0x40000000,
               "font_kern's assembly: the layouts it reads, and the set, way and clamp constants it hard-codes");
#endif

#if defined(__aarch64__) && !defined(_WIN32)
b32 font_kern(font_engine address_to e, const font_face address_to face, p32 left, p32 right, p32 size);
// arm64 (AAPCS64): x0 e, x1 face, w2 left, w3 right, w4 size. Scratch x9-x15 only, and x4 after the hit is known.
__asm__(".text\n"
        ".globl " FONT_SYM(font_kern) "\n"
        ".p2align 2\n"
        FONT_SYM(font_kern) ":\n"
        "cbz x0, 9f\n"
        "cbz x1, 9f\n"
        "lsr w10, w2, #16\n"                      // a glyph past the way's 16 bits is looked up, not memo'd
        "cbnz w10, 9f\n"
        "lsr w10, w3, #16\n"
        "cbnz w10, 9f\n"
        "ldr w15, [x1, #108]\n"                   // face->id, 32 bits: as the reference compares it
        "mov w10, w2\n"                           // left, zero-extended
        "lsl x10, x10, #16\n"
        "mov w11, w3\n"                           // right, zero-extended
        "orr x9, x10, x11\n"
        "orr x9, x9, x15, lsl #32\n"              // id << 32 | left << 16 | right
        "movz x10, #0x7c15\n"
        "movk x10, #0x7f4a, lsl #16\n"
        "movk x10, #0x79b9, lsl #32\n"
        "movk x10, #0x9e37, lsl #48\n"
        "mul x9, x9, x10\n"
        "lsr x9, x9, #55\n"                       // the set, 0..511
        "mov x10, #48\n"                          // FONT_WAYS * sizeof(font_kern_way)
        "madd x12, x9, x10, x0\n"
        "add x12, x12, #0xbf, lsl #12\n"
        "add x12, x12, #0x578\n"                  // x12 = &e->kern_memo[set][0]
        "ldrh w13, [x12]\n"                       // way 0
        "cmp w13, w15\n"
        "b.ne 10f\n"
        "ldrh w13, [x12, #2]\n"
        "cmp w13, w2\n"
        "b.ne 10f\n"
        "ldrh w13, [x12, #4]\n"
        "cmp w13, w3\n"
        "b.ne 10f\n"
        "ldrsw x13, [x12, #8]\n"                  // the value, sign-extended
        "b 5f\n"
        "10:\n"
        "ldrh w13, [x12, #12]\n"                  // way 1
        "cmp w13, w15\n"
        "b.ne 11f\n"
        "ldrh w13, [x12, #14]\n"
        "cmp w13, w2\n"
        "b.ne 11f\n"
        "ldrh w13, [x12, #16]\n"
        "cmp w13, w3\n"
        "b.ne 11f\n"
        "ldrsw x13, [x12, #20]\n"
        "b 5f\n"
        "11:\n"
        "ldrh w13, [x12, #24]\n"                  // way 2
        "cmp w13, w15\n"
        "b.ne 12f\n"
        "ldrh w13, [x12, #26]\n"
        "cmp w13, w2\n"
        "b.ne 12f\n"
        "ldrh w13, [x12, #28]\n"
        "cmp w13, w3\n"
        "b.ne 12f\n"
        "ldrsw x13, [x12, #32]\n"
        "b 5f\n"
        "12:\n"
        "ldrh w13, [x12, #36]\n"                  // way 3
        "cmp w13, w15\n"
        "b.ne 9f\n"
        "ldrh w13, [x12, #38]\n"
        "cmp w13, w2\n"
        "b.ne 9f\n"
        "ldrh w13, [x12, #40]\n"
        "cmp w13, w3\n"
        "b.ne 9f\n"
        "ldrsw x13, [x12, #44]\n"
        "5:\n"                                    // hit: x13 the value; zero is zero
        "cbnz x13, 6f\n"
        "mov w0, #0\n"
        "ret\n"
        "6:\n"
        "ldr w11, [x1, #104]\n"                   // face->units; none: the reference divides, so it answers
        "cbz x11, 9f\n"
        "mov w4, w4\n"                            // size, zero-extended
        "mul x13, x13, x4\n"
        "lsl x13, x13, #10\n"                     // units * size * 1024
        "lsr x12, x11, #1\n"                      // half = units / 2
        "tbnz x13, #63, 7f\n"
        "add x13, x13, x12\n"                     // (value + half) / units
        "sdiv x13, x13, x11\n"
        "b 8f\n"
        "7:\n"
        "sub x13, x12, x13\n"                     // -((half - value) / units)
        "sdiv x13, x13, x11\n"
        "neg x13, x13\n"
        "8:\n"
        "mov x12, #0x40000000\n"                  // clamped to FONT_FIXED_MOST, both ways
        "neg x14, x12\n"                          // -FONT_FIXED_MOST
        "cmp x13, x12\n"
        "csel x13, x12, x13, gt\n"
        "cmp x13, x14\n"
        "csel x13, x14, x13, lt\n"
        "mov w0, w13\n"
        "ret\n"
        "9:\n"
        "b " FONT_SYM(font_kern_reference) "\n"
        FONT_TYPE(font_kern)
        FONT_SIZE(font_kern));
#elif defined(__x86_64__) && !defined(_WIN32)
b32 font_kern(font_engine address_to e, const font_face address_to face, p32 left, p32 right, p32 size);
// x86-64 (SysV): rdi e, rsi face, edx left, ecx right, r8d size. Scratch rax, r9, r10, r11 and rdx after the hit is
// known, and r8 once the units are checked; nothing is saved, so a tail call gets every argument as it came.
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl " FONT_SYM(font_kern) "\n"
        ".p2align 4\n"
        FONT_SYM(font_kern) ":\n"
        "test rdi, rdi\n"
        "jz 9f\n"
        "test rsi, rsi\n"
        "jz 9f\n"
        "cmp edx, 0xffff\n"                       // a glyph past the way's 16 bits is looked up, not memo'd
        "ja 9f\n"
        "cmp ecx, 0xffff\n"
        "ja 9f\n"
        "mov eax, edx\n"                          // left, zero-extended
        "shl rax, 16\n"
        "mov r10d, ecx\n"                         // right, zero-extended
        "or rax, r10\n"
        "mov r9d, dword ptr [rsi + 108]\n"        // face->id, 32 bits: as the reference compares it
        "shl r9, 32\n"
        "or rax, r9\n"                            // id << 32 | left << 16 | right
        "movabs r9, 0x9e3779b97f4a7c15\n"
        "imul rax, r9\n"
        "shr rax, 55\n"                           // the set, 0..511
        "lea rax, [rax + rax*2]\n"
        "shl rax, 4\n"                            // set * 48
        "lea r10, [rdi + rax + 783736]\n"         // r10 = &e->kern_memo[set][0]
        "movzx r11d, word ptr [r10]\n"            // way 0
        "cmp r11d, dword ptr [rsi + 108]\n"
        "jne 10f\n"
        "movzx r11d, word ptr [r10 + 2]\n"
        "cmp r11d, edx\n"
        "jne 10f\n"
        "movzx r11d, word ptr [r10 + 4]\n"
        "cmp r11d, ecx\n"
        "jne 10f\n"
        "movsxd rax, dword ptr [r10 + 8]\n"       // the value, sign-extended
        "jmp 5f\n"
        "10:\n"
        "movzx r11d, word ptr [r10 + 12]\n"       // way 1
        "cmp r11d, dword ptr [rsi + 108]\n"
        "jne 11f\n"
        "movzx r11d, word ptr [r10 + 14]\n"
        "cmp r11d, edx\n"
        "jne 11f\n"
        "movzx r11d, word ptr [r10 + 16]\n"
        "cmp r11d, ecx\n"
        "jne 11f\n"
        "movsxd rax, dword ptr [r10 + 20]\n"
        "jmp 5f\n"
        "11:\n"
        "movzx r11d, word ptr [r10 + 24]\n"       // way 2
        "cmp r11d, dword ptr [rsi + 108]\n"
        "jne 12f\n"
        "movzx r11d, word ptr [r10 + 26]\n"
        "cmp r11d, edx\n"
        "jne 12f\n"
        "movzx r11d, word ptr [r10 + 28]\n"
        "cmp r11d, ecx\n"
        "jne 12f\n"
        "movsxd rax, dword ptr [r10 + 32]\n"
        "jmp 5f\n"
        "12:\n"
        "movzx r11d, word ptr [r10 + 36]\n"       // way 3
        "cmp r11d, dword ptr [rsi + 108]\n"
        "jne 9f\n"
        "movzx r11d, word ptr [r10 + 38]\n"
        "cmp r11d, edx\n"
        "jne 9f\n"
        "movzx r11d, word ptr [r10 + 40]\n"
        "cmp r11d, ecx\n"
        "jne 9f\n"
        "movsxd rax, dword ptr [r10 + 44]\n"
        "5:\n"                                    // hit: rax the value; zero is zero
        "test rax, rax\n"
        "jnz 6f\n"
        "xor eax, eax\n"
        "ret\n"
        "6:\n"
        "mov r11d, dword ptr [rsi + 104]\n"       // face->units; none: the reference divides, so it answers
        "test r11, r11\n"
        "jz 9f\n"
        "mov r8d, r8d\n"                          // size, zero-extended
        "imul rax, r8\n"
        "shl rax, 10\n"                           // units * size * 1024
        "mov r9, r11\n"
        "shr r9, 1\n"                             // half = units / 2
        "test rax, rax\n"
        "js 7f\n"
        "add rax, r9\n"                           // (value + half) / units
        "cqo\n"
        "idiv r11\n"
        "jmp 8f\n"
        "7:\n"
        "sub r9, rax\n"                           // -((half - value) / units)
        "mov rax, r9\n"
        "cqo\n"
        "idiv r11\n"
        "neg rax\n"
        "8:\n"
        "mov r9, 0x40000000\n"                    // clamped to FONT_FIXED_MOST, both ways
        "cmp rax, r9\n"
        "cmovg rax, r9\n"
        "mov r9, -0x40000000\n"
        "cmp rax, r9\n"
        "cmovl rax, r9\n"
        "ret\n"
        "9:\n"
        "jmp " FONT_SYM(font_kern_reference) "\n"
        FONT_TYPE(font_kern)
        FONT_SIZE(font_kern)
        ".att_syntax prefix\n");
#else
static __attribute__((used)) b32 font_kern(font_engine address_to e, const font_face address_to face, p32 left, p32 right, p32 size)
{
        return font_kern_reference(e, face, left, right, size);
}
#endif

//      The pen in eighths of a pixel, to the nearest, halves right.
static inline b32 font_eighths(b64 pen)
{
        return (b32)(((b64)pen * FONT_PHASES + 0x8000) >> 16);
}

/*
        Text that is not one glyph a code point left to right. The faces
        behind the caller's chain are added with font_fallback, a regular
        and its bold (or none): a code point no face of the chain has is
        looked for in them, in the order they were added, and a place's face
        is then count + the slot. Whether it is the bold of the slot is the
        flags' FONT_BOLD, so a line is drawn with the flags it was laid out
        with; a slot with no bold is the regular, embolden by the engine.
*/
fn font_fallback(font_engine address_to e, const font_face address_to regular, const font_face address_to bold)
{
        if (regular && e->fallbacks < FONT_FALLBACKS)
                e->fallback[e->fallbacks++] = (font_fallback_slot){regular, bold};
}

//      The face a place names: the chain's, or a slot's.
static inline const font_face address_to font_face_at(const font_engine address_to e,
                                                       const font_face address_to const address_to faces, p32 count,
                                                       p32 index, p32 flags)
{
        if (index < count)
                return faces[index];
        if (index - count >= e->fallbacks)
                return faces[0];

        const font_fallback_slot address_to slot = &e->fallback[index - count];

        return (flags & FONT_BOLD) && slot->bold ? slot->bold : slot->regular;
}

//      The synthetic bold a slot without one is given when bold is asked for.
static inline p32 font_synthetic(const font_engine address_to e, p32 count, p32 index, p32 flags)
{
        return (flags & FONT_BOLD) && index >= count && index - count < e->fallbacks && !e->fallback[index - count].bold
                   ? FONT_EMBOLDEN
                   : 0;
}

//      The faces behind the chain laid after it, so that the loops read one chain: the caller's, then each slot's regular
//      or, asked for bold, its bold. (A slot with no bold gets the engine's synthetic bold, which the loops of plain text
//      do not give: asked for bold with one of those behind the chain, they hand the line to the whole engine.)
#define FONT_CHAIN 12

static inline p32 font_extend(const font_engine address_to e, const font_face address_to const address_to faces, p32 count,
                              p32 flags, const font_face address_to address_to into)
{
        if (count + e->fallbacks > FONT_CHAIN)
                return 0;
        for (p32 i = 0; i < count; i++)
                into[i] = faces[i];
        for (p32 slot = 0; slot < e->fallbacks; slot++)
                into[count + slot] = font_face_at(e, faces, count, count + slot, flags);
        return count + e->fallbacks;
}

//      Whether some slot would be drawn emboldened by the engine for these flags.
static p8 font_slots_synthetic(const font_engine address_to e, p32 flags)
{
        for (p32 slot = 0; slot < e->fallbacks; slot++)
                if ((flags & FONT_BOLD) && !e->fallback[slot].bold)
                        return true;
        return false;
}

/*
        Whether a text can be laid as it always was, a glyph a code point
        from the left: none of its code points is right to left, an Arabic
        number, or a directional control. Lead bytes under 0xd6 are code
        points under U+0580 and need no look; the rest are decoded unless
        they are in a block that has none (U+1000 to U+1FFF, U+3000 to
        U+EFFF), and the controls of U+2000 to U+2FFF are four bytes' look.
*/
static inline p8 font_bidi_plain(p32 type)
{
        return !(type == FONT_BIDI_R || type == FONT_BIDI_AL || type == FONT_BIDI_AN || type >= FONT_BIDI_LRE);
}

//      The bytes of a word that are at least n (0x80 to 0xff), as their top bits: a byte is when its top bit is set and
//      its low seven bits, with 0x80 - (n & 0x7f) added, carry into the top bit.
static inline p64 font_bytes_from(p64 word, p64 n)
{
        return ((word & 0x7f7f7f7f7f7f7f7full) + (0x80 - (n & 0x7f)) * 0x0101010101010101ull) & word & 0x8080808080808080ull;
}

//      From byte at on, which is not a lead byte under 0xd6 in a word of eight: one at a time, and the ones that might
//      be right to left decoded.
static COLD p32 font_plain_from(const p8 address_to text, positive length, positive at)
{
        while (at < length)
        {
                positive stop = at + 8 <= length ? at + 8 : length;

                if (stop - at == 8)
                {
                        p64 word;

                        memory_copy_apart(&word, text + at, 8);

                        //      The lead bytes from 0xd6 up that might be right to left, not those of U+1000 to U+1FFF
                        //      (0xe1) and U+3000 to U+EFFF (0xe3 to 0xee), so that CJK is eight bytes' look.
                        if (!(font_bytes_from(word, 0xd6) & ~(font_bytes_from(word, 0xe1) & ~font_bytes_from(word, 0xe2)) &
                              ~(font_bytes_from(word, 0xe3) & ~font_bytes_from(word, 0xef))))
                        {
                                at = stop;
                                continue;
                        }
                }
                while (at < stop)
                {
                        p32 lead = text[at];

                        if (lead < 0xd6 || lead == 0xe1 || (lead >= 0xe3 && lead <= 0xee))
                        {
                                at++;
                                continue;
                        }
                        if (lead == 0xe2)
                        {
                                p32 second = at + 1 < length ? text[at + 1] : 0, third = at + 2 < length ? text[at + 2] : 0;

                                if ((second == 0x80 && (third == 0x8f || (third >= 0xaa && third <= 0xae))) ||
                                    (second == 0x81 && third >= 0xa6 && third <= 0xa9))
                                        return 0;
                                at++;
                                continue;
                        }

                        p32 code;

                        at = font_decode(text, length, at, &code);
                        if (!font_bidi_plain(font_bidi_class(code)))
                                return 0;
                }
        }
        return 1;
}

static inline p32 font_plain(const p8 address_to text, positive length)
{
        positive at = 0;

        //      Eight bytes at a time while none is a lead byte of U+0580 or above: Latin, Greek and Cyrillic are that.
        for (; at + 8 <= length; at += 8)
        {
                p64 word;

                memory_copy(&word, text + at, 8);
                if (font_bytes_from(word, 0xd6))
                        return font_plain_from(text, length, at);
        }
        for (; at < length; at++)
                if (text[at] >= 0xd6)
                        return font_plain_from(text, length, at);
        return 1;
}

p32 font_is_simple(const p8 address_to text, positive length)
{
        return font_plain(text, length);
}

//      The paragraph level the first paragraph of a text has under the flags: 0 or 1.
p32 font_base_level(const p8 address_to text, positive length, p32 flags)
{
        p32 fallback = flags & FONT_RTL ? 1 : 0, depth = 0;

        if (!(flags & FONT_AUTO))
                return fallback;
        for (positive at = 0; at < length;)
        {
                p32 code, type;

                at = font_decode(text, length, at, &code);
                type = font_bidi_class(code);
                if (font_bidi_initiator(type))
                        depth++;
                else if (type == FONT_BIDI_PDI)
                        depth -= depth > 0;
                else if (!depth && type == FONT_BIDI_L)
                        return 0;
                else if (!depth && (type == FONT_BIDI_R || type == FONT_BIDI_AL))
                        return 1;
                else if (type == FONT_BIDI_B)
                        break;
        }
        return fallback;
}

static inline p8 font_complex(const p8 address_to text, positive length, p32 flags)
{
        return (flags & FONT_RTL) || !font_plain(text, length);
}


/*
        A line of text laid by the whole engine: split in paragraphs, each
        given its levels, cut in units of one face, one level and one
        script, the units put in reading order and each shaped, and every
        glyph handed to what the line is for (the places of font_layout,
        the pixels of font_draw, the pen of font_extent, the edges of
        font_segments) in the order it reads on the line, left to right,
        with the pen carrying on from one paragraph to the next.
*/
enum { FONT_FLOW_EXTENT, FONT_FLOW_PLACES, FONT_FLOW_DRAW, FONT_FLOW_SEGMENTS };

typedef struct
{
        font_engine address_to e;
        const font_face address_to address_to faces;
        const font_face address_to chain[FONT_CHAIN];
        p32 count, base, size, flags, mode, turn, style, strength, colour, background;
        const font_target address_to target;
        b32 x, baseline;
        b64 pen, cell;
        font_place address_to out;
        positive room, placed;
        const p32 address_to cuts;
        positive segments;
        b32 address_to left, address_to right;
        p8 address_to level;
} font_flow;

#define FONT_NO_EDGE 0x7fffffff

//      A glyph handed on: where its origin is (the pen and its offset) and how far it moves the pen.
static fn font_flow_glyph(font_flow address_to f, p32 glyph, p32 face, b64 advance, b64 x_offset, b32 y_offset, p32 cluster,
                          p32 level)
{
        font_engine address_to e = f->e;
        b64 at = f->pen + x_offset;

        switch (f->mode)
        {
        case FONT_FLOW_PLACES:
                if (f->placed < f->room)
                        f->out[f->placed++] = (font_place){glyph, (p16)face, (p16)level, at, advance, y_offset, e->work.at[cluster]};
                break;
        case FONT_FLOW_DRAW:
        {
                b32 eighth = font_eighths((b64)f->x + at);

                if (face == FONT_DRAWN)
                {
                        font_metrics cell = font_metrics_at(f->faces[0], f->size);
                        font_bitmap box = font_cached(e, null, glyph, (p32)cell.cell_width << 16 | (p32)cell.cell_height, 0, 0, 0);

                        if (f->turn)
                                font_blend_turned(e, f->target, f->turn, eighth >> 3, f->baseline - cell.baseline, box,
                                                  f->colour, f->background, f->strength);
                        else
                                font_blend(e, f->target, eighth >> 3, f->baseline - cell.baseline, box, f->colour,
                                           f->background, f->strength);
                }
                else
                {
                        const font_face address_to one = f->faces[face];
                        font_bitmap box = font_cached(e, one, glyph, f->size, (p32)eighth & 7,
                                                      f->style | font_synthetic(e, f->base, face, f->flags), 0);
                        b32 lift = (b32)(((b64)y_offset + 0x8000) >> 16);

                        if (f->turn)
                                font_blend_turned(e, f->target, f->turn, (eighth >> 3) + box.left, f->baseline - box.top - lift,
                                                  box, f->colour, f->background, f->strength);
                        else
                                font_blend(e, f->target, (eighth >> 3) + box.left, f->baseline - box.top - lift, box,
                                           f->colour, f->background, f->strength);
                }
                break;
        }
        case FONT_FLOW_SEGMENTS:
        {
                font_work address_to w = &e->work;

                if (f->pen < w->span_left[cluster])
                        w->span_left[cluster] = f->pen;
                if (f->pen + advance > w->span_right[cluster])
                        w->span_right[cluster] = f->pen + advance;
                break;
        }
        default:
                break;
        }
        f->pen += advance;
}

//      The category a code point has for itemising: 2 Arabic, 1 Hebrew (the default features), 3 a letter of another
//      script, 0 neutral: it goes with whatever surrounds it.
static inline p32 font_unit_class(p32 code, p32 type)
{
        p32 script = font_script_of(code);

        if (script)
                return script;
        return type == FONT_BIDI_WS || type == FONT_BIDI_ON || type == FONT_BIDI_ES || type == FONT_BIDI_ET ||
                       type == FONT_BIDI_CS || type == FONT_BIDI_EN || type == FONT_BIDI_S || type == FONT_BIDI_B ||
                       type == FONT_BIDI_BN || type == FONT_BIDI_NSM || type == FONT_BIDI_PDI || font_bidi_initiator(type)
                   ? 0
                   : 3;
}

//      One unit, hidden by none of the above: its glyphs handed on.
static fn font_flow_unit(font_flow address_to f, p32 first, p32 stop, p32 face, p32 shape, p32 level)
{
        font_engine address_to e = f->e;
        font_work address_to w = &e->work;
        const font_face address_to one = f->faces[face == FONT_DRAWN ? 0 : face];
        p8 rtl = level & 1;

        if ((shape == 1 || shape == 2) && face != FONT_DRAWN)
        {
                font_item address_to items;
                p32 n = font_shape_unit(e, one, shape, rtl, first, stop - first, &items);

                for (p32 j = 0; j < n; j++)
                {
                        p32 k = rtl ? n - 1 - j : j;

                        font_flow_glyph(f, items[k].glyph, face, font_fixed(one, w->shift[k].advance, f->size),
                                        font_fixed(one, w->shift[k].x, f->size), font_fixed(one, w->shift[k].y, f->size),
                                        items[k].cluster, level);
                }
                return;
        }

        //      A glyph a code point, kerned with the glyph beside it on the line: a unit is one face.
        p32 n = 0, last = 0;
        p8 kerned = false;

        for (p32 i = first; i < stop; i++)
                if (w->level[i] != FONT_GONE && !font_ignorable(w->code[i]))
                        w->pair[n++] = (p16)i;
        for (p32 j = 0; j < n; j++)
        {
                p32 i = w->pair[rtl ? n - 1 - j : j], glyph = w->glyph[i];
                b64 advance;

                if (face == FONT_DRAWN)
                {
                        if (!f->cell)
                                f->cell = (b64)font_metrics_at(f->faces[0], f->size).cell_width << 16;
                        advance = f->cell;
                }
                else
                {
                        if (kerned && !(f->flags & FONT_UNKERNED))
                                f->pen += font_kern(e, one, last, glyph, f->size);
                        advance = font_advance(one, glyph, f->size);
                }
                font_flow_glyph(f, glyph, face, advance, 0, 0, i, level);
                last = glyph;
                kerned = face != FONT_DRAWN;
        }
}

//      The segments' edges from the spans the glyphs of a paragraph left: each cluster's extent shared out among the
//      segments it covers, the first in reading order taking the part first in reading order.
static fn font_flow_close(font_flow address_to f, p32 n)
{
        font_work address_to w = &f->e->work;
        p32 segment = 0;

        for (p32 i = 0; i < n; i++)
        {
                while (segment + 1 < f->segments && f->cuts[segment + 1] <= w->at[i])
                        segment++;
                w->pair[i] = (p16)segment;
        }
        for (p32 i = 0; i < n; i++)
        {
                if (w->span_right[i] < w->span_left[i])
                        continue;

                p32 stop = i + 1, used;

                while (stop < n && w->span_right[stop] < w->span_left[stop])
                        stop++;

                //      The code points a glyph stands for end before a joiner or a control after them: those have
                //      nothing of their own to be shared a part of.
                for (used = stop; used > i + 1 && (w->level[used - 1] == FONT_GONE || font_ignorable(w->code[used - 1])); used--)
                        ;

                p32 first = w->pair[i], last = w->pair[used - 1], parts = last - first + 1, lvl = w->level[i];
                b64 low = w->span_left[i], width = w->span_right[i] - w->span_left[i];

                for (p32 k = first; k <= last; k++)
                {
                        p32 part = lvl & 1 ? last - k : k - first;
                        b64 a = low + width * part / parts, b = low + width * (part + 1) / parts;
                        b32 ca = a > 0x7ffffffe ? 0x7ffffffe : a < -0x7ffffffe ? -0x7ffffffe : (b32)a;
                        b32 cb = b > 0x7ffffffe ? 0x7ffffffe : b < -0x7ffffffe ? -0x7ffffffe : (b32)b;

                        if (f->left[k] == FONT_NO_EDGE)
                        {
                                f->left[k] = ca;
                                f->right[k] = cb;
                                f->level[k] = (p8)lvl;
                        }
                        else
                        {
                                f->left[k] = ca < f->left[k] ? ca : f->left[k];
                                f->right[k] = cb > f->right[k] ? cb : f->right[k];
                        }
                }
                i = stop - 1;
        }
}

//      One paragraph, from byte at: how far it went.
static positive font_flow_paragraph(font_flow address_to f, const p8 address_to text, positive length, positive at)
{
        font_engine address_to e = f->e;
        font_work address_to w = &e->work;
        p32 n = 0, units = 0, prev = 0xffffffffu, direction, ucat = 0, carry;

        while (at < length && n < FONT_PARA)
        {
                w->at[n] = (p32)at;
                at = font_decode(text, length, at, &w->code[n]);
                if (font_bidi_class(w->code[n++]) == FONT_BIDI_B)
                        break;
        }
        w->at[n] = (p32)at;
        direction = f->flags & FONT_AUTO ? (f->flags & FONT_RTL ? 3 : 2) : f->flags & FONT_RTL ? 1 : 0;
        carry = font_bidi(w, n, direction);

        //      The boundary neutrals X9 takes out (the joiners among them) are not taken out of the shaping, which
        //      needs them to join or not to join the letters beside them: they go with the character before.
        for (p32 i = 0; i < n; i++)
                if (w->level[i] != FONT_GONE)
                        carry = w->level[i];
                else if (w->orig[i] == FONT_BIDI_BN)
                        w->level[i] = (p8)carry;

        //      Each code point's face and glyph: the chain's, else a fallback's; a mirrored bracket's mirror where the
        //      face has it; a mark or an ignorable one the face of the character it follows.
        for (p32 i = 0; i < n; i++)
        {
                p32 code = w->code[i], face, glyph;

                if (w->level[i] == FONT_GONE)
                        continue;
                font_find(e, f->faces, f->count, code, &face, &glyph);
                if (prev != 0xffffffffu && w->level[prev] == w->level[i] && w->face[prev] != FONT_DRAWN &&
                    (font_ignorable(code) || w->orig[i] == FONT_BIDI_NSM))
                {
                        const font_face address_to one = f->faces[w->face[prev]];
                        p32 own = font_glyph(one, code);

                        if (own || font_ignorable(code))
                        {
                                face = w->face[prev];
                                glyph = own;
                        }
                }
                if ((w->level[i] & 1) && face != FONT_DRAWN)
                {
                        p32 mirror = font_mirror_of(code), mirrored = mirror != code ? font_glyph(f->faces[face], mirror) : 0;

                        glyph = mirrored ? mirrored : glyph;
                }
                w->face[i] = (p16)face;
                w->glyph[i] = glyph;
                prev = i;
        }

        //      Units: runs of one face, one level and one script, neutrals going with their neighbours.
        for (p32 i = 0; i < n; i++)
        {
                p32 klass;

                if (w->level[i] == FONT_GONE)
                        continue;
                klass = font_unit_class(w->code[i], w->orig[i]);
                if (units && w->unit_level[units - 1] == w->level[i] && w->unit_face[units - 1] == w->face[i] &&
                    (klass == 0 || ucat == 0 || klass == ucat) && i - w->unit_start[units - 1] < FONT_UNIT)
                {
                        ucat = ucat ? ucat : klass;
                        w->unit_shape[units - 1] = (p8)ucat;
                        continue;
                }
                w->unit_start[units] = (p16)i;
                w->unit_level[units] = w->level[i];
                w->unit_face[units] = w->face[i];
                ucat = klass;
                w->unit_shape[units++] = (p8)ucat;
        }
        w->unit_start[units] = (p16)n;

        if (f->mode == FONT_FLOW_SEGMENTS)
                for (p32 i = 0; i < n; i++)
                {
                        w->span_left[i] = 0x7fffffffffffffffll;
                        w->span_right[i] = -0x7fffffffffffffffll;
                }

        //      Reading order of the units, and each handed on.
        {
                p16 address_to order = w->unit_order;
                p32 count = font_reorder(w->unit_level, units, order);

                for (p32 k = 0; k < count; k++)
                {
                        p32 u = order[k];

                        font_flow_unit(f, w->unit_start[u], w->unit_start[u + 1], w->unit_face[u], w->unit_shape[u],
                                       w->unit_level[u]);
                }
        }
        if (f->mode == FONT_FLOW_SEGMENTS)
                font_flow_close(f, n);
        return at;
}

static b64 font_flow_run(font_flow address_to f, const p8 address_to text, positive length)
{
        positive at = 0;

        f->pen = 0;
        while (at < length && f->count)
                at = font_flow_paragraph(f, text, length, at);
        return f->pen;
}

//      A flow over the caller's chain and the faces behind it.
static fn font_flow_start(font_flow address_to f, font_engine address_to e, const font_face address_to const address_to faces,
                          p32 count, p32 flags, p32 size, p32 mode)
{
        p32 n = font_extend(e, faces, count, flags, f->chain);

        if (!n)
        {
                n = count < FONT_CHAIN ? count : FONT_CHAIN;
                for (p32 i = 0; i < n; i++)
                        f->chain[i] = faces[i];
        }
        f->e = e;
        f->faces = (const font_face address_to address_to)f->chain;
        f->count = n;
        f->base = count;
        f->flags = flags;
        f->size = size;
        f->mode = mode;
}

//      The whole engine's line for each of the callers below, apart from them so that the loops they take for text that
//      is not that stay as small as they were.
static __attribute__((noinline)) positive font_layout_flow(font_engine address_to e,
                                                          const font_face address_to const address_to faces, p32 count,
                                                          const p8 address_to text, positive length, p32 size, p32 flags,
                                                          font_place address_to out, positive room)
{
        font_flow flow = {0};

        font_flow_start(&flow, e, faces, count, flags, size, FONT_FLOW_PLACES);
        flow.out = out;
        flow.room = room;
        font_flow_run(&flow, text, length);
        return flow.placed;
}

static __attribute__((noinline)) b64 font_draw_flow(font_engine address_to e, const font_face address_to const address_to faces,
                                                    p32 count, const p8 address_to text, positive length, p32 size,
                                                    p32 flags, p32 turn, p32 style, p32 strength,
                                                    const font_target address_to target, b32 x, b32 baseline, p32 colour,
                                                    p32 background)
{
        font_flow flow = {0};

        font_flow_start(&flow, e, faces, count, flags, size, FONT_FLOW_DRAW);
        flow.turn = turn;
        flow.style = style;
        flow.strength = strength;
        flow.target = target;
        flow.x = x;
        flow.baseline = baseline;
        flow.colour = colour;
        flow.background = background;
        return font_flow_run(&flow, text, length);
}

static __attribute__((noinline)) b64 font_extent_flow(font_engine address_to e,
                                                      const font_face address_to const address_to faces, p32 count,
                                                      const p8 address_to text, positive length, p32 size, p32 flags)
{
        font_flow flow = {0};

        font_flow_start(&flow, e, faces, count, flags, size, FONT_FLOW_EXTENT);
        return font_flow_run(&flow, text, length);
}

positive font_layout(font_engine address_to e, const font_face address_to const address_to faces,
                     p32 count, const p8 address_to text, positive length, p32 size,
                     p32 flags, font_place address_to out, positive room)
{
        const font_face address_to extended[FONT_CHAIN];
        positive placed = 0, at = 0;
        b64 pen = 0;

        if (font_complex(text, length, flags) || (unlikely(e->fallbacks != 0) && font_slots_synthetic(e, flags)))
                return font_layout_flow(e, faces, count, text, length, size, flags, out, room);
        if (unlikely(e->fallbacks != 0))
        {
                p32 total = font_extend(e, faces, count, flags, extended);

                if (total)
                {
                        faces = (const font_face address_to const address_to)extended;
                        count = total;
                }
        }
        while (at < length && placed < room && count)
        {
                p32 code, face, glyph, from = (p32)at;

                at = font_decode(text, length, at, &code);
                font_find(e, faces, count, code, &face, &glyph);
                if (placed && face != FONT_DRAWN && out[placed - 1].face == face &&
                    !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[face], out[placed - 1].glyph, glyph, size);

                b64 advance = face == FONT_DRAWN ? (b64)font_metrics_at(faces[0], size).cell_width << 16
                                                 : font_advance(faces[face], glyph, size);
                out[placed++] = (font_place){glyph, (p16)face, 0, pen, advance, 0, from};
                pen += advance;
        }
        return placed;
}

static __attribute__((used)) b64 font_draw_reference(font_engine address_to e, const font_face address_to const address_to faces,
                                                     p32 count, const p8 address_to text, positive length, p32 size, p32 flags,
                                                     const font_target address_to target, b32 x, b32 baseline, p32 colour,
                                                     p32 background)
{
        const font_face address_to extended[FONT_CHAIN];
        positive at = 0;
        b64 pen = 0;
        p32 last_glyph = 0, last_face = FONT_DRAWN;

        flags = font_polarity(flags, colour, background);
        p32 style = font_style(e, size, flags), strength = font_strength(e, size, flags);

        //      Stems on whole pixels as a cell's are, though the pen is in
        //      eighths: the glyph is snapped where it lands, its two ends kept,
        //      so its spacing is the font's. Unsnapped, Inter's l at 10 to
        //      14 px fell across two grey columns at most phases.
        style |= style & 1 ? 4 : 0;

        if (font_complex(text, length, flags) || (unlikely(e->fallbacks != 0) && font_slots_synthetic(e, flags)))
                return font_draw_flow(e, faces, count, text, length, size, flags, 0, style, strength, target, x, baseline,
                                      colour, background);
        if (unlikely(e->fallbacks != 0))
        {
                p32 total = font_extend(e, faces, count, flags, extended);

                if (total)
                {
                        faces = (const font_face address_to const address_to)extended;
                        count = total;
                }
        }
        while (at < length && count)
        {
                p32 code, face, glyph;
                b64 advance;

                at = font_decode(text, length, at, &code);
                font_find(e, faces, count, code, &face, &glyph);
                if (face != FONT_DRAWN && last_face == face && !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[face], last_glyph, glyph, size);

                b32 eighth = font_eighths((b64)x + pen);
                if (face == FONT_DRAWN)
                {
                        font_metrics cell = font_metrics_at(faces[0], size);
                        font_bitmap box = font_cached(e, null, glyph, (p32)cell.cell_width << 16 | (p32)cell.cell_height, 0, 0,
                                                      0);
                        advance = cell.cell_width << 16;
                        font_blend(e, target, eighth >> 3, baseline - cell.baseline, box, colour, background, strength);
                }
                else
                {
                        font_bitmap box = font_cached(e, faces[face], glyph, size, (p32)eighth & 7, style, 0);
                        advance = font_advance(faces[face], glyph, size);
                        font_blend(e, target, (eighth >> 3) + box.left, baseline - box.top, box, colour, background, strength);
                }
                pen += advance;
                last_glyph = glyph;
                last_face = face;
        }
        return pen;
}

//      font_draw: the fast path, in assembly, for arm64 and x86-64 (SysV, not Windows). The run it takes is printable
//      ASCII (0x20 to 0x7e) of at most 128 bytes, in one face (count 1, no fallbacks), not right to left, whose glyphs
//      are in the glyph memo and whose coverage is in the cache's slots, the size and the cache within what the lookup
//      takes without a reset. It lays the run as font_draw_reference does, in two passes: the pen, kerning, the phase
//      and the slot of every glyph first, then every glyph blended onto the target in order. A byte outside that, a
//      glyph the memo or the cache does not have, and a flag or a size this does not take are a tail call to
//      font_draw_reference with the arguments untouched. Nothing is drawn, and no slot is made, before the run is
//      known to be one the assembly takes; the memo's and the kerning's entries it fills are the reference's own.
//      The C steps it calls are below (font_draw_prep, font_draw_kern), font_advance, and font_blend_glyph, once per glyph:
//      the assembly makes the glyph's font_bitmap itself, the slot's box, with stores of the sizes it is read at.
typedef struct
{
        p32 slot;               // the glyph's slot, 1-based, in the cache's list
        b32 x, y;               // where its box lands on the target
} font_draw_place;

#if (defined(__aarch64__) || defined(__x86_64__)) && !defined(_WIN32)
_Static_assert(offset_of(font_engine, fallbacks) == 997248 && offset_of(font_engine, glyph_memo) == 775544 &&
               offset_of(font_engine, cached) == 637304 && offset_of(font_engine, index) == 629112 &&
               offset_of(font_engine, slots) == 530808 && offset_of(font_engine, scratched) == 53 &&
               offset_of(font_engine, slot_count) == 48 && offset_of(font_engine, generation) == 64 &&
               offset_of(font_engine, atlas) == 24 && offset_of(font_engine, atlas_width) == 32 &&
               offset_of(font_face, id) == 108 && sizeof(font_memo) == 16 &&
               offset_of(font_memo, value) == 8 && sizeof(font_cache_way) == 24 &&
               offset_of(font_cache_way, cell) == 8 && offset_of(font_cache_way, generation) == 16 &&
               offset_of(font_cache_way, slot) == 20 && sizeof(font_slot) == 32 &&
               offset_of(font_slot, x) == 16 && offset_of(font_slot, y) == 18 &&
               offset_of(font_slot, width) == 20 && offset_of(font_slot, height) == 22 &&
               offset_of(font_slot, left) == 24 && offset_of(font_slot, top) == 26 &&
               sizeof(font_draw_place) == 12 && offset_of(font_draw_place, x) == 4 &&
               offset_of(font_draw_place, y) == 8 && sizeof(font_bitmap) == 32 &&
               offset_of(font_bitmap, stride) == 8 && offset_of(font_bitmap, left) == 16 &&
               offset_of(font_bitmap, top) == 20 && offset_of(font_bitmap, width) == 24 &&
               offset_of(font_bitmap, height) == 28 && offset_of(font_engine, ramp_now) == 2392 &&
               offset_of(font_engine, ramp) == 2400 && sizeof(font_target) == 24 &&
               offset_of(font_target, stride) == 8 && offset_of(font_target, width) == 16 &&
               offset_of(font_target, height) == 20 && offset_of(font_ramp_set, ground) == 4 &&
               offset_of(font_ramp_set, contrast) == 8,
               "font_draw's assembly: the layouts it reads and writes");
#endif

//      The flags, style and strength a run is laid with, into out: out[0] the flags with the polarity, out[1] the style
//      (with the 4 for whole stems), out[2] the strength.
static __attribute__((used)) void font_draw_prep(const font_engine address_to e, p32 size, p32 flags, p32 colour, p32 background,
                                                 p32 address_to out)
{
        flags = font_polarity(flags, colour, background);

        p32 style = font_style(e, size, flags), strength = font_strength(e, size, flags);

        style |= style & 1 ? 4 : 0;
        out[0] = flags;
        out[1] = style;
        out[2] = strength;
}

static __attribute__((used)) b32 font_draw_kern(font_engine address_to e, const font_face address_to face, p32 left, p32 right,
                                                p32 size)
{
        return font_kern(e, face, left, right, size);
}

b64 font_draw(font_engine address_to e, const font_face address_to const address_to faces, p32 count, const p8 address_to text,
              positive length, p32 size, p32 flags, const font_target address_to target, b32 x, b32 baseline, p32 colour,
              p32 background);

//      font_blend_glyph: font_blend, the C above, on a known ground, in assembly, with font_blend's own arguments and
//      ABI (arm64: x0 e, x1 target, w2 x, w3 y, x4 the glyph by address, w5 colour, w6 background, w7 strength). It does
//      the C's steps in the C's order: the clip; the tones and the ramp (font_tones, and font_ramp where the ramp is not
//      the engine's current one); the kernel's row; and each stop of the row, the pixel mixed by font_mix, the rest of
//      the row and the rows below taken by the kernel again. The row's state (r, c) is in registers, as the C's is. On
//      a ground not known it tail-calls font_blend.
#if defined(__aarch64__) && !defined(_WIN32)
__asm__(".text\n"
        ".p2align 2\n"
        FONT_SYM(font_blend_glyph) ":\n"
        "tst w6, #0xff000000\n"                    // FONT_UNKNOWN: not known, the C
        "b.eq 97f\n"
        "b " FONT_SYM(font_blend) "\n"
        "97:\n"
        "ldr w9, [x4, #24]\n"                      // the glyph's width and height, the target's width and height
        "ldr w10, [x4, #28]\n"
        "ldr w11, [x1, #16]\n"
        "ldr w12, [x1, #20]\n"
        "cmp w2, #0\n"                             // x0 (w13) = x < 0 ? -x : 0
        "neg w13, w2\n"
        "csel w13, w13, wzr, lt\n"
        "cmp w3, #0\n"                             // y0 (w14)
        "neg w14, w3\n"
        "csel w14, w14, wzr, lt\n"
        "add w15, w2, w9\n"                        // x1 (w15) = x + width > target width ? target width - x : width
        "cmp w15, w11\n"
        "sub w17, w11, w2\n"
        "csel w15, w17, w9, gt\n"
        "add w16, w3, w10\n"                       // y1 (w16)
        "cmp w16, w12\n"
        "sub w17, w12, w3\n"
        "csel w16, w17, w10, gt\n"
        "cmp w13, w15\n"
        "b.ge 99f\n"                               // x0 >= x1 or y0 >= y1: nothing to draw
        "cmp w14, w16\n"
        "b.ge 99f\n"
        "stp x29, x30, [sp, #-112]!\n"
        "mov x29, sp\n"
        "stp x19, x20, [sp, #16]\n"
        "stp x21, x22, [sp, #32]\n"
        "stp x23, x24, [sp, #48]\n"
        "stp x25, x26, [sp, #64]\n"
        "stp x27, x28, [sp, #80]\n"
        "mov x19, x0\n"                            // e
        "str w5, [sp, #96]\n"                      // colour, background, strength, as given
        "str w6, [sp, #100]\n"
        "str w7, [sp, #104]\n"
        "sub w21, w15, w13\n"                      // width (x21) = x1 - x0, height (x22) = y1 - y0
        "sub w22, w16, w14\n"
        "ldr x9, [x4]\n"                           // coverage, glyph stride
        "ldr x10, [x4, #8]\n"
        "ldr x23, [x1, #8]\n"                      // stride (x23), pixels
        "ldr x26, [x1]\n"
        "add w17, w3, w14\n"                       // into (x26) = pixels + (y + y0) * stride + (x + x0), in pixels
        "mov w17, w17\n"
        "mul x17, x17, x23\n"
        "add w2, w2, w13\n"
        "add x17, x17, w2, uxtw\n"
        "add x26, x26, x17, lsl #2\n"
        "mov w14, w14\n"                           // from (x25) = coverage + y0 * glyph stride + x0
        "mul x25, x14, x10\n"
        "add x25, x25, w13, uxtw\n"
        "add x25, x9, x25\n"
        "mov x24, x10\n"                           // glyph stride (x24)
        "mov x0, x19\n"                            // the tones: font_tones(e, colour, background, strength)
        "ldr w1, [sp, #96]\n"
        "ldr w2, [sp, #100]\n"
        "ldr w3, [sp, #104]\n"
        "bl " FONT_SYM(font_tones) "\n"
        "mov x20, x0\n"                            // pixel (x20)
        "ldr w3, [sp, #104]\n"                     // the ramp's strength: strength > 255 ? 255 : strength
        "mov w4, #255\n"
        "cmp w3, w4\n"
        "csel w3, w4, w3, hi\n"
        "ldr x9, [x19, #2392]\n"                   // font_ramp, unless the engine's ramp is this one already
        "cbz x9, 20f\n"
        "ldr w10, [sp, #96]\n"
        "ldr w11, [x9]\n"
        "cmp w11, w10\n"
        "b.ne 20f\n"
        "ldr w11, [x9, #4]\n"
        "ldr w12, [sp, #100]\n"
        "cmp w11, w12\n"
        "b.ne 20f\n"
        "ldr w11, [x9, #8]\n"
        "cmp w11, w3\n"
        "b.eq 21f\n"
        "20:\n"
        "mov x0, x19\n"
        "ldr w1, [sp, #96]\n"
        "ldr w2, [sp, #100]\n"
        "bl " FONT_SYM(font_ramp) "\n"
        "21:\n"
        "mov x0, x26\n"                            // the first row: memory_blend_u32_rect(into, stride, from, glyph stride,
        "mov x1, x23\n"                            // width, height, pixel): r (x27) = at / width, c (x28) = at % width
        "mov x2, x25\n"
        "mov x3, x24\n"
        "mov x4, x21\n"
        "mov x5, x22\n"
        "mov x6, x20\n"
        "bl " FONT_SYM(memory_blend_u32_rect) "\n"
        "udiv x27, x0, x21\n"
        "msub x28, x27, x21, x0\n"
        "22:\n"                                    // while (r < height)
        "cmp x27, x22\n"
        "b.hs 98f\n"
        "madd x9, x27, x23, x28\n"                 // here = into + r * stride + c (x10)
        "add x10, x26, x9, lsl #2\n"
        "madd x12, x27, x24, x28\n"                // fw = from[r * glyph stride + c]; a = e->ramp[fw] (w3)
        "ldrb w12, [x25, x12]\n"
        "ldr x14, [x19, #2400]\n"
        "ldrb w3, [x14, x12]\n"
        "mov x0, x19\n"                            // *here = font_mix(e, *here, colour & 0xffffff, a)
        "ldr w1, [x10]\n"
        "ldr w2, [sp, #96]\n"
        "and w2, w2, #0xffffff\n"
        "bl " FONT_SYM(font_mix) "\n"
        "madd x9, x27, x23, x28\n"
        "add x10, x26, x9, lsl #2\n"
        "str w0, [x10]\n"
        "add x28, x28, #1\n"                       // ++c < width: the rest of the row, from c, by the kernel
        "cmp x28, x21\n"
        "b.hs 23f\n"
        "madd x11, x27, x23, x28\n"                // here + 1 = into + (r * stride + c) (c already counted)
        "add x0, x26, x11, lsl #2\n"
        "mov x1, x23\n"
        "madd x9, x27, x24, x28\n"                 // from + r * glyph stride + c
        "add x2, x25, x9\n"
        "mov x3, x24\n"
        "sub x4, x21, x28\n"                       // width - c, one row
        "mov x5, #1\n"
        "mov x6, x20\n"
        "bl " FONT_SYM(memory_blend_u32_rect) "\n"
        "sub x7, x21, x28\n"
        "cmp x0, x7\n"
        "b.hs 23f\n"                               // at >= width - c: the row is done
        "add x28, x28, x0\n"                       // a stop at c + at: c += at, the row goes on
        "b 22b\n"
        "23:\n"                                    // ++r; the kernel over the rows below, from column 0
        "add x27, x27, #1\n"
        "cmp x27, x22\n"
        "b.hs 98f\n"
        "mul x9, x27, x23\n"
        "add x0, x26, x9, lsl #2\n"                // into + r * stride
        "mov x1, x23\n"
        "mul x9, x27, x24\n"
        "add x2, x25, x9\n"                        // from + r * glyph stride
        "mov x3, x24\n"
        "mov x4, x21\n"
        "sub x5, x22, x27\n"                       // height - r
        "mov x6, x20\n"
        "bl " FONT_SYM(memory_blend_u32_rect) "\n"
        "udiv x9, x0, x21\n"                       // r += at / width, c = at % width
        "msub x28, x9, x21, x0\n"
        "add x27, x27, x9\n"
        "b 22b\n"
        "98:\n"
        "ldp x27, x28, [sp, #80]\n"
        "ldp x25, x26, [sp, #64]\n"
        "ldp x23, x24, [sp, #48]\n"
        "ldp x21, x22, [sp, #32]\n"
        "ldp x19, x20, [sp, #16]\n"
        "ldp x29, x30, [sp], #112\n"
        "99:\n"
        "ret\n"
        FONT_TYPE(font_blend_glyph)
        FONT_SIZE(font_blend_glyph));
#endif

//      font_blend_glyph (x86-64): font_blend, the C above, on a known ground, in assembly, with font_blend's own ABI
//      (SysV: rdi e, rsi target, edx x, ecx y, the glyph in memory at [rsp+8], r8d colour, r9d background, strength at
//      [rsp+40]). The steps and their order are the arm64 one's: the clip, the tones and the ramp, the kernel's row, and
//      each stop of the row. r (r12) and c (r13) are in registers, the stop's pixel in rbp; the kernel's table is the
//      seventh argument, at [rsp]. On a ground not known it tail-jumps to font_blend.
#if defined(__x86_64__) && !defined(_WIN32)
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".p2align 4\n"
        FONT_SYM(font_blend_glyph) ":\n"
        "test r9d, 0xff000000\n"                   // FONT_UNKNOWN: not known, the C
        "jz 97f\n"
        "jmp " FONT_SYM(font_blend) "\n"
        "97:\n"
        "push rbp\n"
        "push rbx\n"
        "push r12\n"
        "push r13\n"
        "push r14\n"
        "push r15\n"
        "sub rsp, 136\n"                           // the glyph is at [rsp+192], its strength at [rsp+224]
        "mov rbx, rdi\n"                           // e
        "mov dword ptr [rsp + 40], r8d\n"          // colour, background, strength, as given
        "mov dword ptr [rsp + 44], r9d\n"
        "mov eax, dword ptr [rsp + 224]\n"
        "mov dword ptr [rsp + 48], eax\n"
        "xor r10d, r10d\n"                         // x0 (r10d) = x < 0 ? -x : 0
        "mov eax, edx\n"
        "neg eax\n"
        "cmp edx, 0\n"
        "cmovl r10d, eax\n"
        "xor r11d, r11d\n"                         // y0 (r11d) = y < 0 ? -y : 0
        "mov eax, ecx\n"
        "neg eax\n"
        "cmp ecx, 0\n"
        "cmovl r11d, eax\n"
        "mov r8d, edx\n"                           // x1 (r8d) = x + width > target width ? target width - x : width
        "add r8d, dword ptr [rsp + 216]\n"
        "mov r9d, dword ptr [rsi + 16]\n"
        "sub r9d, edx\n"
        "mov eax, dword ptr [rsp + 216]\n"
        "cmp r8d, dword ptr [rsi + 16]\n"
        "cmovg eax, r9d\n"
        "mov r8d, eax\n"
        "mov r9d, ecx\n"                           // y1 (r9d) = y + height > target height ? target height - y : height
        "add r9d, dword ptr [rsp + 220]\n"
        "mov edi, dword ptr [rsi + 20]\n"
        "sub edi, ecx\n"
        "mov eax, dword ptr [rsp + 220]\n"
        "cmp r9d, dword ptr [rsi + 20]\n"
        "cmovg eax, edi\n"
        "mov r9d, eax\n"
        "cmp r10d, r8d\n"
        "jge 98f\n"                                // x0 >= x1 or y0 >= y1: nothing to draw
        "cmp r11d, r9d\n"
        "jge 98f\n"
        "sub r8d, r10d\n"                          // width (8), height (16): x1 - x0, y1 - y0
        "sub r9d, r11d\n"
        "mov eax, r8d\n"
        "mov qword ptr [rsp + 8], rax\n"
        "mov eax, r9d\n"
        "mov qword ptr [rsp + 16], rax\n"
        "mov rax, qword ptr [rsi + 8]\n"           // stride (24)
        "mov qword ptr [rsp + 24], rax\n"
        "mov edi, ecx\n"                           // into (r14) = pixels + (y + y0) * stride + (x + x0), in pixels
        "add edi, r11d\n"
        "imul rdi, rax\n"
        "add edx, r10d\n"
        "add rdi, rdx\n"
        "mov r14, qword ptr [rsi]\n"
        "lea r14, [r14 + rdi * 4]\n"
        "mov rax, qword ptr [rsp + 192]\n"         // from (r15) = coverage + y0 * glyph stride + x0
        "mov rcx, qword ptr [rsp + 200]\n"         // glyph stride (32)
        "mov qword ptr [rsp + 32], rcx\n"
        "mov edi, r11d\n"
        "imul rdi, rcx\n"
        "lea r15, [rax + rdi]\n"
        "add r15, r10\n"
        "mov rdi, rbx\n"                           // pixel = font_tones(e, colour, background, strength)
        "mov esi, dword ptr [rsp + 40]\n"
        "mov edx, dword ptr [rsp + 44]\n"
        "mov ecx, dword ptr [rsp + 48]\n"
        "call " FONT_SYM(font_tones) "\n"
        "mov qword ptr [rsp], rax\n"
        "mov ecx, dword ptr [rsp + 48]\n"          // the ramp's strength: strength > 255 ? 255 : strength
        "cmp ecx, 255\n"
        "jbe 19f\n"
        "mov ecx, 255\n"
        "19:\n"
        "mov rax, qword ptr [rbx + 2392]\n"        // font_ramp, unless the engine's ramp is this one already
        "test rax, rax\n"
        "jz 20f\n"
        "mov edx, dword ptr [rsp + 40]\n"
        "cmp dword ptr [rax], edx\n"
        "jne 20f\n"
        "mov edx, dword ptr [rsp + 44]\n"
        "cmp dword ptr [rax + 4], edx\n"
        "jne 20f\n"
        "cmp dword ptr [rax + 8], ecx\n"
        "je 21f\n"
        "20:\n"
        "mov rdi, rbx\n"
        "mov esi, dword ptr [rsp + 40]\n"
        "mov edx, dword ptr [rsp + 44]\n"
        "call " FONT_SYM(font_ramp) "\n"
        "21:\n"
        "mov rdi, r14\n"                           // the first row: memory_blend_u32_rect(into, stride, from, glyph stride,
        "mov rsi, qword ptr [rsp + 24]\n"          // width, height, pixel); at the first stop, r = at / width, c = at % width
        "mov rdx, r15\n"
        "mov rcx, qword ptr [rsp + 32]\n"
        "mov r8, qword ptr [rsp + 8]\n"
        "mov r9, qword ptr [rsp + 16]\n"
        "call " FONT_SYM(memory_blend_u32_rect) "\n"
        "mov rcx, qword ptr [rsp + 8]\n"
        "xor edx, edx\n"
        "div rcx\n"
        "mov r12, rax\n"
        "mov r13, rdx\n"
        "22:\n"                                    // while (r < height)
        "cmp r12, qword ptr [rsp + 16]\n"
        "jae 98f\n"
        "mov rax, r12\n"                           // here (rbp) = into + r * stride + c
        "imul rax, qword ptr [rsp + 24]\n"
        "add rax, r13\n"
        "lea rbp, [r14 + rax * 4]\n"
        "mov rax, r12\n"                           // fw = from[r * glyph stride + c]; a = e->ramp[fw] (ecx)
        "imul rax, qword ptr [rsp + 32]\n"
        "add rax, r13\n"
        "movzx eax, byte ptr [r15 + rax]\n"
        "mov rcx, qword ptr [rbx + 2400]\n"
        "movzx ecx, byte ptr [rcx + rax]\n"
        "mov rdi, rbx\n"                           // *here = font_mix(e, *here, colour & 0xffffff, a)
        "mov esi, dword ptr [rbp]\n"
        "mov edx, dword ptr [rsp + 40]\n"
        "and edx, 0xffffff\n"
        "call " FONT_SYM(font_mix) "\n"
        "mov dword ptr [rbp], eax\n"
        "inc r13\n"                                // ++c < width: the rest of the row, from c, by the kernel
        "cmp r13, qword ptr [rsp + 8]\n"
        "jae 23f\n"
        "lea rdi, [rbp + 4]\n"                     // here + 1
        "mov rsi, qword ptr [rsp + 24]\n"
        "mov rax, r12\n"
        "imul rax, qword ptr [rsp + 32]\n"
        "add rax, r13\n"
        "lea rdx, [r15 + rax]\n"                   // from + r * glyph stride + c
        "mov rcx, qword ptr [rsp + 32]\n"
        "mov r8, qword ptr [rsp + 8]\n"
        "sub r8, r13\n"                            // width - c, one row
        "mov r9d, 1\n"
        "call " FONT_SYM(memory_blend_u32_rect) "\n"
        "mov rcx, qword ptr [rsp + 8]\n"
        "sub rcx, r13\n"
        "cmp rax, rcx\n"
        "jae 23f\n"                                // at >= width - c: the row is done
        "add r13, rax\n"                           // a stop at c + at: c += at, the row goes on
        "jmp 22b\n"
        "23:\n"                                    // ++r; the kernel over the rows below, from column 0
        "inc r12\n"
        "cmp r12, qword ptr [rsp + 16]\n"
        "jae 98f\n"
        "mov rdi, r12\n"
        "imul rdi, qword ptr [rsp + 24]\n"
        "lea rdi, [r14 + rdi * 4]\n"               // into + r * stride
        "mov rsi, qword ptr [rsp + 24]\n"
        "mov rdx, r12\n"
        "imul rdx, qword ptr [rsp + 32]\n"
        "add rdx, r15\n"                           // from + r * glyph stride
        "mov rcx, qword ptr [rsp + 32]\n"
        "mov r8, qword ptr [rsp + 8]\n"
        "mov r9, qword ptr [rsp + 16]\n"
        "sub r9, r12\n"                            // height - r
        "call " FONT_SYM(memory_blend_u32_rect) "\n"
        "mov rcx, qword ptr [rsp + 8]\n"           // r += at / width, c = at % width
        "xor edx, edx\n"
        "div rcx\n"
        "add r12, rax\n"
        "mov r13, rdx\n"
        "jmp 22b\n"
        "98:\n"
        "add rsp, 136\n"
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        "pop rbp\n"
        "ret\n"
        FONT_TYPE(font_blend_glyph)
        FONT_SIZE(font_blend_glyph)
        ".att_syntax prefix\n");
#endif

#if defined(__aarch64__) && !defined(_WIN32)
//      arm64 (AAPCS64): x0 e, x1 faces, w2 count, x3 text, x4 length, w5 size, w6 flags, x7 target, then on the stack
//      x, baseline, colour, background. Apple's arm64 packs those four at their own sizes; AAPCS64 gives each 8 bytes.
//      Frame, from sp (2272 bytes): out[0..2] from font_draw_prep (flags, style, strength), size, colour, background, target,
//      x, baseline, text, length; the arguments x0-x7 kept at 80 (for the tail call); the places from 144 (16 bytes each,
//      at most 128 of them); x29, x30 and x19-x25 saved at 2192 and up.
//      x19 e, x20 the byte's index, x21 the pen, x22 the last glyph, x23 this glyph, x24 the face, x25 the eighth's whole pixels.
#if defined(__APPLE__)
#define FONT_DRAW_BASE "2276"
#define FONT_DRAW_COLOUR "2280"
#define FONT_DRAW_BG "2284"
#else
#define FONT_DRAW_BASE "2280"
#define FONT_DRAW_COLOUR "2288"
#define FONT_DRAW_BG "2296"
#endif
__asm__(".text\n"
        ".globl " FONT_SYM(font_draw) "\n"
        ".p2align 2\n"
        FONT_SYM(font_draw) ":\n"
        "sub sp, sp, #2272\n"
        "add x16, sp, #2192\n"
        "stp x29, x30, [x16]\n"
        "stp x19, x20, [x16, #16]\n"
        "stp x21, x22, [x16, #32]\n"
        "stp x23, x24, [x16, #48]\n"
        "str x25, [x16, #64]\n"
        "add x29, sp, #2192\n"
        "stp x0, x1, [sp, #80]\n"
        "stp x2, x3, [sp, #96]\n"
        "stp x4, x5, [sp, #112]\n"
        "stp x6, x7, [sp, #128]\n"
        "cmp w2, #1\n"                             // count 1
        "b.ne 9f\n"
        "cbz x4, 9f\n"                             // 1 to 128 bytes
        "cmp x4, #128\n"
        "b.hi 9f\n"
        "cmp w5, #0x100, lsl #12\n"                // size < FONT_SIZE_LIMIT
        "b.hs 9f\n"
        "mov x19, x0\n"
        "movz x10, #0x3780\n"                      // e->fallbacks == 0
        "movk x10, #0xf, lsl #16\n"
        "ldr w9, [x19, x10]\n"
        "cbnz w9, 9f\n"
        "ldrb w9, [x19, #53]\n"                    // !e->scratched
        "cbnz w9, 9f\n"
        "ldr w9, [x19, #48]\n"                     // e->slot_count < FONT_SLOTS * 3 / 4
        "cmp w9, #3072\n"
        "b.hs 9f\n"
        "tst w6, #64\n"                            // not FONT_RTL
        "b.ne 9f\n"
        "ldr x24, [x1]\n"                          // faces[0]
        "str w5, [sp, #12]\n"
        "str x3, [sp, #40]\n"
        "str x4, [sp, #48]\n"
        "str x7, [sp, #24]\n"
        "ldr w9, [sp, #2272]\n"                    // x
        "str w9, [sp, #32]\n"
        "ldr w9, [sp, #" FONT_DRAW_BASE "]\n"
        "str w9, [sp, #36]\n"
        "ldr w9, [sp, #" FONT_DRAW_COLOUR "]\n"
        "str w9, [sp, #16]\n"
        "ldr w9, [sp, #" FONT_DRAW_BG "]\n"
        "str w9, [sp, #20]\n"
        "mov x0, x19\n"
        "ldr w1, [sp, #12]\n"
        "mov w2, w6\n"
        "ldr w3, [sp, #16]\n"
        "ldr w4, [sp, #20]\n"
        "mov x5, sp\n"
        "bl " FONT_SYM(font_draw_prep) "\n"
        "mov x20, #0\n"
        "mov x21, #0\n"
        "mov x22, #0\n"
        "1:\n"                                     // pass one: the byte at x20
        "ldr x9, [sp, #48]\n"
        "cmp x20, x9\n"
        "b.hs 10f\n"
        "ldr x9, [sp, #40]\n"
        "ldrb w10, [x9, x20]\n"
        "sub w11, w10, #0x20\n"                    // 0x20 to 0x7e
        "cmp w11, #0x5e\n"
        "b.hi 9f\n"
        "ldr w11, [x24, #108]\n"                   // key = 1 << 63 | id << 32 | code
        "movz x12, #0x8000, lsl #48\n"
        "orr x12, x12, x11, lsl #32\n"
        "orr x12, x12, x10\n"
        "movz x13, #0x7c15\n"
        "movk x13, #0x7f4a, lsl #16\n"
        "movk x13, #0x79b9, lsl #32\n"
        "movk x13, #0x9e37, lsl #48\n"
        "mul x14, x12, x13\n"
        "lsr x14, x14, #40\n"                      // font_recall
        "and x14, x14, #511\n"
        "movz x15, #0xd578\n"
        "movk x15, #0xb, lsl #16\n"
        "add x15, x19, x15\n"
        "add x15, x15, x14, lsl #4\n"
        "ldr x16, [x15]\n"                         // memo->key == key, value nonzero
        "cmp x16, x12\n"
        "b.ne 9f\n"
        "ldr w23, [x15, #8]\n"
        "cbz w23, 9f\n"
        "cbz x20, 2f\n"                            // kerning: not the first, not FONT_UNKERNED
        "ldr w9, [sp, #0]\n"
        "tst w9, #4\n"
        "b.ne 2f\n"
        "mov x0, x19\n"
        "mov x1, x24\n"
        "mov w2, w22\n"
        "mov w3, w23\n"
        "ldr w4, [sp, #12]\n"
        "bl " FONT_SYM(font_draw_kern) "\n"
        "sxtw x0, w0\n"
        "add x21, x21, x0\n"
        "2:\n"
        "ldrsw x9, [sp, #32]\n"                    // eighth = (pen + x) * 8 + 0x8000 >> 16, b32
        "add x9, x9, x21\n"
        "lsl x9, x9, #3\n"
        "add x9, x9, #8, lsl #12\n"
        "asr x9, x9, #16\n"
        "asr w25, w9, #3\n"                        // whole pixels: eighth >> 3
        "and w11, w9, #7\n"                        // phase
        "ldr w12, [x24, #108]\n"                   // cache key, font_cached's
        "lsl x12, x12, #47\n"
        "movz x13, #0x8000, lsl #48\n"
        "orr x12, x12, x13\n"
        "ubfiz x13, x23, #31, #16\n"
        "orr x12, x12, x13\n"
        "ldr w13, [sp, #12]\n"
        "ubfiz x13, x13, #9, #22\n"
        "orr x12, x12, x13\n"
        "ubfiz x13, x11, #6, #3\n"
        "orr x12, x12, x13\n"
        "ldr w13, [sp, #4]\n"
        "ubfiz x14, x13, #3, #3\n"
        "orr x12, x12, x14\n"
        "ubfx x14, x13, #3, #2\n"
        "orr x12, x12, x14\n"
        "movz x13, #0x7c15\n"                      // way: ((key ^ 0) * golden) >> 56
        "movk x13, #0x7f4a, lsl #16\n"
        "movk x13, #0x79b9, lsl #32\n"
        "movk x13, #0x9e37, lsl #48\n"
        "mul x14, x12, x13\n"
        "lsr x14, x14, #56\n"
        "add x14, x14, x14, lsl #1\n"              // * 24
        "movz x15, #0xb978\n"
        "movk x15, #0x9, lsl #16\n"
        "add x15, x19, x15\n"
        "add x15, x15, x14, lsl #3\n"
        "ldr x13, [x15]\n"                         // way hit: key, cell 0, generation
        "cmp x13, x12\n"
        "b.ne 3f\n"
        "ldr x13, [x15, #8]\n"
        "cbnz x13, 3f\n"
        "ldr w13, [x15, #16]\n"
        "ldr w14, [x19, #64]\n"
        "cmp w13, w14\n"
        "b.ne 3f\n"
        "ldrh w13, [x15, #20]\n"
        "b 6f\n"
        "3:\n"                                     // the index's chain from the home
        "movz x13, #0x7c15\n"
        "movk x13, #0x7f4a, lsl #16\n"
        "movk x13, #0x79b9, lsl #32\n"
        "movk x13, #0x9e37, lsl #48\n"
        "mul x14, x12, x13\n"
        "lsr x14, x14, #52\n"
        "and x14, x14, #4095\n"
        "movz x16, #0x9978\n"
        "movk x16, #0x9, lsl #16\n"
        "add x16, x19, x16\n"
        "4:\n"
        "ldrh w13, [x16, x14, lsl #1]\n"           // index[home], 1-based; 0 is a miss: the reference's
        "cbz w13, 9f\n"
        "sub x9, x13, #1\n"
        "lsl x9, x9, #5\n"
        "movz x10, #0x1978\n"
        "movk x10, #0x8, lsl #16\n"
        "add x9, x9, x10\n"
        "add x9, x19, x9\n"
        "ldr x10, [x9]\n"
        "cmp x10, x12\n"
        "b.ne 5f\n"
        "ldr x10, [x9, #8]\n"
        "cbnz x10, 5f\n"
        "str x12, [x15]\n"                         // found: the way is written as the reference writes it
        "str xzr, [x15, #8]\n"
        "ldr w10, [x19, #64]\n"
        "str w10, [x15, #16]\n"
        "strh w13, [x15, #20]\n"
        "b 6f\n"
        "5:\n"
        "add x14, x14, #1\n"
        "and x14, x14, #4095\n"
        "b 4b\n"
        "6:\n"                                     // the slot's left and top, the place of the glyph
        "sub x9, x13, #1\n"
        "lsl x9, x9, #5\n"
        "movz x10, #0x1978\n"
        "movk x10, #0x8, lsl #16\n"
        "add x9, x9, x10\n"
        "add x9, x19, x9\n"
        "ldrsh w10, [x9, #24]\n"
        "ldrsh w11, [x9, #26]\n"
        "add w10, w25, w10\n"
        "ldr w14, [sp, #36]\n"
        "sub w11, w14, w11\n"
        "add x9, sp, #144\n"
        "add x9, x9, x20, lsl #4\n"
        "str w13, [x9]\n"
        "str w10, [x9, #4]\n"
        "str w11, [x9, #8]\n"
        "mov x0, x24\n"                            // the advance
        "mov w1, w23\n"
        "ldr w2, [sp, #12]\n"
        "bl " FONT_SYM(font_advance) "\n"
        "sxtw x0, w0\n"
        "add x21, x21, x0\n"
        "mov x22, x23\n"
        "add x20, x20, #1\n"
        "b 1b\n"
        "10:\n"                                    // pass two: each place blended in order, font_blend_glyph called with the glyph
        "mov x20, #0\n"                            // (its font_bitmap, as font_slot_box makes it) at sp+80, the arguments
        "7:\n"                                     // kept there for the tail call, which pass two never takes
        "ldr x9, [sp, #48]\n"
        "cmp x20, x9\n"
        "b.hs 8f\n"
        "add x2, sp, #144\n"
        "add x2, x2, x20, lsl #4\n"                // x2 the place
        "ldr w9, [x2]\n"
        "sub x9, x9, #1\n"
        "lsl x9, x9, #5\n"
        "movz x10, #0x1978\n"
        "movk x10, #0x8, lsl #16\n"                // 530808: the slots
        "add x9, x9, x10\n"
        "add x9, x19, x9\n"                        // &e->slots[slot - 1]
        "ldrh w10, [x9, #16]\n"                    // slot->x
        "ldrh w11, [x9, #18]\n"                    // slot->y
        "ldrh w12, [x9, #20]\n"                    // slot->width
        "ldrh w13, [x9, #22]\n"                    // slot->height
        "ldrsh w14, [x9, #24]\n"                   // slot->left
        "ldrsh w15, [x9, #26]\n"                   // slot->top
        "ldr x16, [x19, #24]\n"                    // e->atlas
        "ldr w17, [x19, #32]\n"                    // e->atlas_width
        "madd x0, x11, x17, x16\n"                 // atlas + y * atlas_width + x: font_slot_box's coverage
        "add x0, x0, x10\n"
        "cbnz w12, 11f\n"                          // no width, no coverage
        "mov x0, xzr\n"
        "11:\n"
        "mov x1, #1\n"                             // stride: 1 for the slot at 0xffff, else the atlas's width
        "movz w11, #0xffff\n"
        "cmp w10, w11\n"
        "csel x1, x1, x17, eq\n"
        "stp x0, x1, [sp, #80]\n"
        "stp w14, w15, [sp, #96]\n"                // left, top
        "stp w12, w13, [sp, #104]\n"               // width, height
        "mov x0, x19\n"
        "ldr x1, [sp, #24]\n"                      // target
        "ldr w3, [x2, #8]\n"                       // place y
        "ldr w2, [x2, #4]\n"                       // place x
        "add x4, sp, #80\n"                        // the glyph, by reference (AAPCS64, Apple's arm64 too)
        "ldr w5, [sp, #16]\n"                      // colour
        "ldr w6, [sp, #20]\n"                      // background
        "ldr w7, [sp, #8]\n"                       // strength
        "bl " FONT_SYM(font_blend_glyph) "\n"
        "add x20, x20, #1\n"
        "b 7b\n"
        "8:\n"
        "mov x0, x21\n"
        "add x16, sp, #2192\n"
        "ldp x29, x30, [x16]\n"
        "ldp x19, x20, [x16, #16]\n"
        "ldp x21, x22, [x16, #32]\n"
        "ldp x23, x24, [x16, #48]\n"
        "ldr x25, [x16, #64]\n"
        "add sp, sp, #2272\n"
        "ret\n"
        "9:\n"                                     // not the fast path: the reference, with the arguments as they were
        "ldp x0, x1, [sp, #80]\n"
        "ldp x2, x3, [sp, #96]\n"
        "ldp x4, x5, [sp, #112]\n"
        "ldp x6, x7, [sp, #128]\n"
        "add x16, sp, #2192\n"
        "ldp x29, x30, [x16]\n"
        "ldp x19, x20, [x16, #16]\n"
        "ldp x21, x22, [x16, #32]\n"
        "ldp x23, x24, [x16, #48]\n"
        "ldr x25, [x16, #64]\n"
        "add sp, sp, #2272\n"
        "b " FONT_SYM(font_draw_reference) "\n"
        FONT_TYPE(font_draw)
        FONT_SIZE(font_draw));
#elif defined(__x86_64__) && !defined(_WIN32)
//      x86-64 (SysV): rdi e, rsi faces, edx count, rcx text, r8 length, r9d size; then the stack from [rsp+8]: flags,
//      target, x, baseline, colour, background. Frame, from rsp (after six pushes and 2184 bytes): out[0..2] from
//      font_draw_prep (flags, style, strength), size, colour, background, target, x, baseline, text, length; the register
//      arguments kept at 72 (for the tail call); the places from 128 (16 bytes each).
//      rbx e, r12 the byte's index, r13 the pen, r14 the last glyph, r15 this glyph, rbp the face.
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl " FONT_SYM(font_draw) "\n"
        ".p2align 4\n"
        FONT_SYM(font_draw) ":\n"
        "push rbp\n"
        "push rbx\n"
        "push r12\n"
        "push r13\n"
        "push r14\n"
        "push r15\n"
        "sub rsp, 2184\n"
        "mov qword ptr [rsp + 72], rdi\n"
        "mov qword ptr [rsp + 80], rsi\n"
        "mov qword ptr [rsp + 88], rdx\n"
        "mov qword ptr [rsp + 96], rcx\n"
        "mov qword ptr [rsp + 104], r8\n"
        "mov qword ptr [rsp + 112], r9\n"
        "cmp edx, 1\n"                             // count 1
        "jne 9f\n"
        "test r8, r8\n"                            // 1 to 128 bytes
        "jz 9f\n"
        "cmp r8, 128\n"
        "ja 9f\n"
        "cmp r9d, 0x100000\n"                      // size < FONT_SIZE_LIMIT
        "jae 9f\n"
        "mov eax, dword ptr [rdi + 997248]\n"      // e->fallbacks == 0
        "test eax, eax\n"
        "jnz 9f\n"
        "cmp byte ptr [rdi + 53], 0\n"             // !e->scratched
        "jne 9f\n"
        "cmp dword ptr [rdi + 48], 3072\n"         // e->slot_count < FONT_SLOTS * 3 / 4
        "jae 9f\n"
        "test dword ptr [rsp + 2240], 64\n"        // not FONT_RTL
        "jnz 9f\n"
        "mov rbx, rdi\n"
        "mov rbp, qword ptr [rsi]\n"               // faces[0]
        "mov qword ptr [rsp + 40], rcx\n"
        "mov qword ptr [rsp + 48], r8\n"
        "mov dword ptr [rsp + 12], r9d\n"
        "mov rax, qword ptr [rsp + 2248]\n"
        "mov qword ptr [rsp + 24], rax\n"
        "mov eax, dword ptr [rsp + 2256]\n"
        "mov dword ptr [rsp + 32], eax\n"
        "mov eax, dword ptr [rsp + 2264]\n"
        "mov dword ptr [rsp + 36], eax\n"
        "mov eax, dword ptr [rsp + 2272]\n"
        "mov dword ptr [rsp + 16], eax\n"
        "mov eax, dword ptr [rsp + 2280]\n"
        "mov dword ptr [rsp + 20], eax\n"
        "mov rdi, rbx\n"
        "mov esi, dword ptr [rsp + 12]\n"
        "mov edx, dword ptr [rsp + 2240]\n"
        "mov ecx, dword ptr [rsp + 16]\n"
        "mov r8d, dword ptr [rsp + 20]\n"
        "mov r9, rsp\n"
        "call " FONT_SYM(font_draw_prep) "\n"
        "xor r12d, r12d\n"
        "xor r13d, r13d\n"
        "xor r14d, r14d\n"
        "1:\n"                                     // pass one: the byte at r12
        "cmp r12, qword ptr [rsp + 48]\n"
        "jae 10f\n"
        "mov rax, qword ptr [rsp + 40]\n"
        "movzx ecx, byte ptr [rax + r12]\n"
        "lea edx, [rcx - 0x20]\n"                  // 0x20 to 0x7e
        "cmp edx, 0x5e\n"
        "ja 9f\n"
        "mov eax, dword ptr [rbp + 108]\n"         // key = 1 << 63 | id << 32 | code
        "shl rax, 32\n"
        "movabs rdx, 0x8000000000000000\n"
        "or rax, rdx\n"
        "or rax, rcx\n"
        "mov r10, rax\n"
        "movabs rdx, 0x9e3779b97f4a7c15\n"
        "imul rax, rdx\n"
        "shr rax, 40\n"                            // font_recall
        "and eax, 511\n"
        "shl rax, 4\n"
        "lea rdx, [rbx + rax + 775544]\n"
        "cmp qword ptr [rdx], r10\n"               // memo->key == key, value nonzero
        "jne 9f\n"
        "mov r15d, dword ptr [rdx + 8]\n"
        "test r15d, r15d\n"
        "jz 9f\n"
        "test r12, r12\n"                          // kerning: not the first, not FONT_UNKERNED
        "jz 2f\n"
        "test dword ptr [rsp], 4\n"
        "jnz 2f\n"
        // A memo hit with no adjustment is no kerning, and a hit writes nothing: the call is skipped. The way is found as
        // font_kern finds it (the face's id, the glyphs, each in 16 bits; anything else makes the call).
        "mov eax, dword ptr [rbp + 108]\n"         // face->id
        "cmp eax, 0xffff\n"
        "ja 41f\n"
        "cmp r14d, 0xffff\n"
        "ja 41f\n"
        "cmp r15d, 0xffff\n"
        "ja 41f\n"
        "shl rax, 32\n"                            // id << 32 | left << 16 | right
        "mov edx, r14d\n"
        "shl rdx, 16\n"
        "or rax, rdx\n"
        "mov edx, r15d\n"
        "or rax, rdx\n"
        "movabs rdx, 0x9e3779b97f4a7c15\n"
        "imul rax, rdx\n"
        "shr rax, 55\n"                            // the set, 0..511
        "lea rax, [rax + rax*2]\n"
        "shl rax, 4\n"                             // set * 48
        "lea r10, [rbx + rax + 783736]\n"          // r10 = &e->kern_memo[set][0]
        "mov r11d, dword ptr [rbp + 108]\n"        // face->id
        "movzx edx, word ptr [r10]\n"              // way 0
        "cmp edx, r11d\n"
        "jne 42f\n"
        "movzx edx, word ptr [r10 + 2]\n"
        "cmp edx, r14d\n"
        "jne 42f\n"
        "movzx edx, word ptr [r10 + 4]\n"
        "cmp edx, r15d\n"
        "jne 42f\n"
        "cmp dword ptr [r10 + 8], 0\n"             // a hit with no adjustment
        "je 2f\n"
        "jmp 41f\n"
        "42:\n"
        "movzx edx, word ptr [r10 + 12]\n"         // way 1
        "cmp edx, r11d\n"
        "jne 43f\n"
        "movzx edx, word ptr [r10 + 14]\n"
        "cmp edx, r14d\n"
        "jne 43f\n"
        "movzx edx, word ptr [r10 + 16]\n"
        "cmp edx, r15d\n"
        "jne 43f\n"
        "cmp dword ptr [r10 + 20], 0\n"
        "je 2f\n"
        "jmp 41f\n"
        "43:\n"
        "movzx edx, word ptr [r10 + 24]\n"         // way 2
        "cmp edx, r11d\n"
        "jne 44f\n"
        "movzx edx, word ptr [r10 + 26]\n"
        "cmp edx, r14d\n"
        "jne 44f\n"
        "movzx edx, word ptr [r10 + 28]\n"
        "cmp edx, r15d\n"
        "jne 44f\n"
        "cmp dword ptr [r10 + 32], 0\n"
        "je 2f\n"
        "jmp 41f\n"
        "44:\n"
        "movzx edx, word ptr [r10 + 36]\n"         // way 3
        "cmp edx, r11d\n"
        "jne 41f\n"
        "movzx edx, word ptr [r10 + 38]\n"
        "cmp edx, r14d\n"
        "jne 41f\n"
        "movzx edx, word ptr [r10 + 40]\n"
        "cmp edx, r15d\n"
        "jne 41f\n"
        "cmp dword ptr [r10 + 44], 0\n"
        "je 2f\n"
        "41:\n"
        "mov rdi, rbx\n"
        "mov rsi, rbp\n"
        "mov edx, r14d\n"
        "mov ecx, r15d\n"
        "mov r8d, dword ptr [rsp + 12]\n"
        "call " FONT_SYM(font_draw_kern) "\n"
        "movsxd rax, eax\n"
        "add r13, rax\n"
        "2:\n"
        "movsxd rax, dword ptr [rsp + 32]\n"       // eighth = (pen + x) * 8 + 0x8000 >> 16, b32
        "add rax, r13\n"
        "lea rax, [rax * 8 + 32768]\n"
        "sar rax, 16\n"
        "mov r9d, eax\n"
        "sar r9d, 3\n"                             // whole pixels: eighth >> 3
        "and eax, 7\n"                             // phase
        "mov edx, dword ptr [rbp + 108]\n"         // cache key, font_cached's
        "shl rdx, 47\n"
        "movabs r10, 0x8000000000000000\n"
        "or rdx, r10\n"
        "mov r10d, r15d\n"
        "and r10d, 0xffff\n"
        "shl r10, 31\n"
        "or rdx, r10\n"
        "mov r10d, dword ptr [rsp + 12]\n"
        "and r10d, 0x3fffff\n"
        "shl r10, 9\n"
        "or rdx, r10\n"
        "mov r10d, eax\n"
        "shl r10, 6\n"
        "or rdx, r10\n"
        "mov r10d, dword ptr [rsp + 4]\n"
        "mov r11d, r10d\n"
        "and r11d, 7\n"
        "shl r11, 3\n"
        "or rdx, r11\n"
        "shr r10d, 3\n"
        "and r10d, 3\n"
        "or rdx, r10\n"
        "mov r11, rdx\n"                           // way: ((key ^ 0) * golden) >> 56, 24 bytes each
        "movabs r10, 0x9e3779b97f4a7c15\n"
        "imul r11, r10\n"
        "shr r11, 56\n"
        "lea r11, [r11 + r11 * 2]\n"
        "lea r11, [rbx + r11 * 8 + 637304]\n"
        "cmp qword ptr [r11], rdx\n"               // way hit: key, cell 0, generation
        "jne 3f\n"
        "cmp qword ptr [r11 + 8], 0\n"
        "jne 3f\n"
        "mov r10d, dword ptr [rbx + 64]\n"
        "cmp dword ptr [r11 + 16], r10d\n"
        "jne 3f\n"
        "movzx r8d, word ptr [r11 + 20]\n"
        "jmp 6f\n"
        "3:\n"                                     // the index's chain from the home
        "mov rax, rdx\n"
        "movabs r10, 0x9e3779b97f4a7c15\n"
        "imul rax, r10\n"
        "shr rax, 52\n"
        "and eax, 4095\n"
        "4:\n"
        "movzx r8d, word ptr [rbx + rax * 2 + 629112]\n"   // index[home], 1-based; 0 is a miss: the reference's
        "test r8d, r8d\n"
        "jz 9f\n"
        "lea r10, [r8 - 1]\n"
        "shl r10, 5\n"
        "lea r10, [rbx + r10 + 530808]\n"
        "cmp qword ptr [r10], rdx\n"
        "jne 5f\n"
        "cmp qword ptr [r10 + 8], 0\n"
        "jne 5f\n"
        "mov qword ptr [r11], rdx\n"               // found: the way is written as the reference writes it
        "mov qword ptr [r11 + 8], 0\n"
        "mov r10d, dword ptr [rbx + 64]\n"
        "mov dword ptr [r11 + 16], r10d\n"
        "mov word ptr [r11 + 20], r8w\n"
        "jmp 6f\n"
        "5:\n"
        "add rax, 1\n"
        "and rax, 4095\n"
        "jmp 4b\n"
        "6:\n"                                     // the slot's left and top, the place of the glyph
        "lea r10, [r8 - 1]\n"
        "shl r10, 5\n"
        "lea r10, [rbx + r10 + 530808]\n"
        "movsx edi, word ptr [r10 + 24]\n"
        "movsx esi, word ptr [r10 + 26]\n"
        "add edi, r9d\n"
        "mov eax, dword ptr [rsp + 36]\n"
        "sub eax, esi\n"
        "mov rcx, r12\n"
        "shl rcx, 4\n"
        "mov dword ptr [rsp + rcx + 128], r8d\n"
        "mov dword ptr [rsp + rcx + 132], edi\n"
        "mov dword ptr [rsp + rcx + 136], eax\n"
        "mov rdi, rbp\n"                           // the advance
        "mov esi, r15d\n"
        "mov edx, dword ptr [rsp + 12]\n"
        "call " FONT_SYM(font_advance) "\n"
        "movsxd rax, eax\n"
        "add r13, rax\n"
        "mov r14, r15\n"
        "inc r12\n"
        "jmp 1b\n"
        "10:\n"                                    // pass two: each place blended in order, font_blend_glyph called with its glyph
        "xor r12d, r12d\n"                         // (font_bitmap, as font_slot_box makes it) and strength on the stack
        "7:\n"
        "cmp r12, qword ptr [rsp + 48]\n"
        "jae 8f\n"
        "mov rdx, r12\n"
        "shl rdx, 4\n"
        "lea rdx, [rsp + rdx + 128]\n"             // rdx the place, its address kept for the arguments
        "sub rsp, 48\n"                            // the outgoing arguments: the glyph at 0, strength at 32 (the frame's
        "mov eax, dword ptr [rdx]\n"               // offsets from here are 48 more)
        "sub eax, 1\n"                             // slot, 1-based
        "shl rax, 5\n"
        "lea r10, [rbx + rax + 530808]\n"          // &e->slots[slot - 1]
        "movzx r8d, word ptr [r10 + 16]\n"         // slot->x
        "movzx r9d, word ptr [r10 + 18]\n"         // slot->y
        "movzx r11d, word ptr [r10 + 20]\n"        // slot->width
        "movzx eax, word ptr [r10 + 22]\n"         // slot->height
        "mov dword ptr [rsp + 28], eax\n"
        "mov dword ptr [rsp + 24], r11d\n"         // width
        "movsx eax, word ptr [r10 + 24]\n"         // slot->left
        "mov dword ptr [rsp + 16], eax\n"
        "movsx eax, word ptr [r10 + 26]\n"         // slot->top
        "mov dword ptr [rsp + 20], eax\n"
        "xor eax, eax\n"                           // coverage: null with no width, else atlas + y * atlas_width + x
        "test r11d, r11d\n"
        "jz 11f\n"
        "mov eax, dword ptr [rbx + 32]\n"          // e->atlas_width
        "imul r9, rax\n"
        "add r9, qword ptr [rbx + 24]\n"           // e->atlas
        "add r9, r8\n"
        "mov rax, r9\n"
        "11:\n"
        "mov qword ptr [rsp], rax\n"               // coverage
        "mov ecx, dword ptr [rbx + 32]\n"          // stride: 1 for the slot at 0xffff, else the atlas's width
        "mov r10d, 1\n"
        "cmp r8d, 0xffff\n"
        "cmove rcx, r10\n"
        "mov qword ptr [rsp + 8], rcx\n"           // stride
        "mov rdi, rbx\n"
        "mov rsi, qword ptr [rsp + 72]\n"          // target
        "mov ecx, dword ptr [rdx + 8]\n"           // place y (before rdx, the place, becomes place x)
        "mov edx, dword ptr [rdx + 4]\n"           // place x
        "mov r8d, dword ptr [rsp + 64]\n"          // colour
        "mov r9d, dword ptr [rsp + 68]\n"          // background
        "mov eax, dword ptr [rsp + 56]\n"          // strength
        "mov dword ptr [rsp + 32], eax\n"
        "call " FONT_SYM(font_blend_glyph) "\n"
        "add rsp, 48\n"
        "inc r12\n"
        "jmp 7b\n"
        "8:\n"
        "mov rax, r13\n"
        "add rsp, 2184\n"
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        "pop rbp\n"
        "ret\n"
        "9:\n"                                     // not the fast path: the reference, with the arguments as they were
        "mov rdi, qword ptr [rsp + 72]\n"
        "mov rsi, qword ptr [rsp + 80]\n"
        "mov rdx, qword ptr [rsp + 88]\n"
        "mov rcx, qword ptr [rsp + 96]\n"
        "mov r8, qword ptr [rsp + 104]\n"
        "mov r9, qword ptr [rsp + 112]\n"
        "add rsp, 2184\n"
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop rbx\n"
        "pop rbp\n"
        "jmp " FONT_SYM(font_draw_reference) "\n"
        FONT_TYPE(font_draw)
        FONT_SIZE(font_draw)
        ".att_syntax prefix\n");
#else
b64 font_draw(font_engine address_to e, const font_face address_to const address_to faces, p32 count, const p8 address_to text,
              positive length, p32 size, p32 flags, const font_target address_to target, b32 x, b32 baseline, p32 colour,
              p32 background)
{
        return font_draw_reference(e, faces, count, text, length, size, flags, target, x, baseline, colour, background);
}
#endif

/*
        font_draw's loop, each glyph laid turned. It is a loop of its own and
        not font_draw's with the turn as an argument: that one loop inlined
        into both moved gcc's registers in font_draw (the engine kept on the
        stack and read again for every glyph), and font_draw is every line of
        upright text there is.
*/
b64 font_draw_turned(font_engine address_to e, const font_face address_to const address_to faces,
                     p32 count, const p8 address_to text, positive length, p32 size, p32 flags, p32 turn,
                     const font_target address_to target, b32 x, b32 baseline, p32 colour, p32 background)
{
        const font_face address_to extended[FONT_CHAIN];
        positive at = 0;
        b64 pen = 0;
        p32 last_glyph = 0, last_face = FONT_DRAWN;

        turn = turn == FONT_TOPS_LEFT ? FONT_TOPS_LEFT : FONT_TOPS_RIGHT;
        flags = font_polarity(flags, colour, background);
        p32 style = font_style(e, size, flags), strength = font_strength(e, size, flags);

        style |= style & 1 ? 4 : 0;
        if (font_complex(text, length, flags) || (unlikely(e->fallbacks != 0) && font_slots_synthetic(e, flags)))
                return font_draw_flow(e, faces, count, text, length, size, flags, turn, style, strength, target, x, baseline,
                                      colour, background);
        if (unlikely(e->fallbacks != 0))
        {
                p32 total = font_extend(e, faces, count, flags, extended);

                if (total)
                {
                        faces = (const font_face address_to const address_to)extended;
                        count = total;
                }
        }
        while (at < length && count)
        {
                p32 code, face, glyph;
                b64 advance;

                at = font_decode(text, length, at, &code);
                font_find(e, faces, count, code, &face, &glyph);
                if (face != FONT_DRAWN && last_face == face && !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[face], last_glyph, glyph, size);

                b32 eighth = font_eighths((b64)x + pen);
                if (face == FONT_DRAWN)
                {
                        font_metrics cell = font_metrics_at(faces[0], size);
                        font_bitmap box = font_cached(e, null, glyph, (p32)cell.cell_width << 16 | (p32)cell.cell_height, 0, 0,
                                                      0);
                        advance = cell.cell_width << 16;
                        font_blend_turned(e, target, turn, eighth >> 3, baseline - cell.baseline, box, colour, background,
                                          strength);
                }
                else
                {
                        font_bitmap box = font_cached(e, faces[face], glyph, size, (p32)eighth & 7, style, 0);
                        advance = font_advance(faces[face], glyph, size);
                        font_blend_turned(e, target, turn, (eighth >> 3) + box.left, baseline - box.top, box, colour, background,
                                          strength);
                }
                pen += advance;
                last_glyph = glyph;
                last_face = face;
        }
        return pen;
}

//      The pen's steps of font_draw, kerning and the cell a drawn code
//      point takes included, without finding a bitmap.
b64 font_extent(font_engine address_to e, const font_face address_to const address_to faces,
                p32 count, const p8 address_to text, positive length, p32 size, p32 flags)
{
        const font_face address_to extended[FONT_CHAIN];
        positive at = 0;
        b64 pen = 0;
        p32 last_glyph = 0, last_face = FONT_DRAWN;

        if (font_complex(text, length, flags) || (unlikely(e->fallbacks != 0) && font_slots_synthetic(e, flags)))
                return font_extent_flow(e, faces, count, text, length, size, flags);
        if (unlikely(e->fallbacks != 0))
        {
                p32 total = font_extend(e, faces, count, flags, extended);

                if (total)
                {
                        faces = (const font_face address_to const address_to)extended;
                        count = total;
                }
        }
        while (at < length && count)
        {
                p32 code, face, glyph;

                at = font_decode(text, length, at, &code);
                font_find(e, faces, count, code, &face, &glyph);
                if (face != FONT_DRAWN && last_face == face && !(flags & FONT_UNKERNED))
                        pen += font_kern(e, faces[face], last_glyph, glyph, size);
                pen += face == FONT_DRAWN ? font_metrics_at(faces[0], size).cell_width << 16
                                          : font_advance(faces[face], glyph, size);
                last_glyph = glyph;
                last_face = face;
        }
        return pen;
}

/*
        Where the n segments of a line are on it, for a caret and for
        hit-testing. cuts[0..n] are ascending byte offsets, cuts[0] 0 and
        cuts[n] the length, and segment k is the text from cuts[k] to
        cuts[k + 1]: the caller's grapheme clusters, which this does not
        look for. left[k] and right[k] are its edges in 16.16 pixels from
        the pen the line starts at (font_draw's), and level[k] the bidi level
        it is laid at, odd reading right to left. A glyph that stands for
        several segments (a lam-alef ligature, a ligature of two clusters)
        is shared among them in equal parts, the first in reading order
        having the first part; marks belong to the cluster they hang from,
        and a segment of nothing a glyph stands for (a joiner, a control)
        has no width and sits at the edge of the segment before it. Answers
        n, or 0 for more than FONT_SEGMENTS segments or FONT_SEGMENT_BYTES
        bytes, which the caller lays out some other way.
*/
positive font_segments(font_engine address_to e, const font_face address_to const address_to faces, p32 count,
                       const p8 address_to text, positive length, p32 size, p32 flags, const p32 address_to cuts, positive n,
                       b32 address_to left, b32 address_to right, p8 address_to level)
{
        font_flow flow = {0};
        p8 base = (p8)font_base_level(text, length, flags);

        if (!n || n > FONT_SEGMENTS || length > FONT_SEGMENT_BYTES || !count)
                return 0;
        font_flow_start(&flow, e, faces, count, flags, size, FONT_FLOW_SEGMENTS);
        flow.cuts = cuts;
        flow.segments = n;
        flow.left = left;
        flow.right = right;
        flow.level = level;
        for (positive k = 0; k < n; k++)
        {
                left[k] = FONT_NO_EDGE;
                right[k] = 0;
                level[k] = base;
        }
        font_flow_run(&flow, text, length);

        //      A segment no glyph stands for sits at the end of the one before it, or the start of the one after.
        for (positive k = 0; k < n; k++)
        {
                if (left[k] != FONT_NO_EDGE)
                        continue;

                positive other = k;
                b32 edge = 0;

                if (k)
                {
                        edge = level[k - 1] & 1 ? left[k - 1] : right[k - 1];
                        level[k] = level[k - 1];
                }
                else
                {
                        while (other < n && left[other] == FONT_NO_EDGE)
                                other++;
                        if (other < n)
                        {
                                edge = level[other] & 1 ? right[other] : left[other];
                                level[k] = level[other];
                        }
                }
                left[k] = right[k] = edge;
        }
        return n;
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

                b32 eighth = font_eighths((((b64)width << 16) - font_advance(faces[face], glyph, size)) / 2);
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

/*
        The plan of font_cell's known-ground branch, drawn by nobody: what font_cell
        would write, as the rectangles and the translate it makes, so a frame can
        record the cell (fill the cell's rectangle with the ground, then translate the
        glyph's box through the tones) and the GPU can draw it. Its numbers are the
        ones font_cell takes, in the same order: the same cell, the same clip to the
        target, the same box, the same tones (font_tones), so filling the ground and
        translating the box gives font_cell's pixels. unknown: the background is
        FONT_UNKNOWN, and font_cell blends it (font_blend): no plan. empty: the cell is
        off the target. translate: the glyph's box is not empty, and the translate is
        drawn; when it is not, the whole clipped cell is the ground.
*/
typedef struct
{
        b32 unknown, empty, translate;
        b32 left, top, right, bottom;  // the cell's rectangle on the target, cut to it
        b32 from, above, to, below;    // the glyph's box inside it
        const p8 address_to coverage;  // the coverage of the box's first pixel
        positive stride;               // of the coverage: bytes a row
        const p32 address_to tones;    // the 256 tones (translate only)
        p32 ground, strength;          // background & 0xffffff, and the strength the tones were made at
} font_cell_plan_t;

fn font_cell_plan(font_engine address_to e, const font_face address_to const address_to faces, p32 count, p32 code, p32 cells,
                  const font_metrics address_to grid, p32 size, p32 flags, const font_target address_to target, b32 x, b32 y, p32 colour,
                  p32 background, font_cell_plan_t address_to plan)
{
        b32 width = (b32)(cells ? cells : 1) * grid->cell_width, height = grid->cell_height;
        b32 dx, dy;
        b16 ink[2];

        //      Zeroed in line: memory_zero is a call into lib.c, made for every cell.
        *plan = (font_cell_plan_t){0};
        plan->ground = background & 0xffffff;
        if (background & FONT_UNKNOWN)
        {
                plan->unknown = true;
                return;
        }
        flags = font_polarity(flags, colour, background);
        p64 chain = font_chain(faces, count, cells);
        p32 ident = chain ? font_identify(e, chain, font_cell_shape(grid, count, cells), cells << 3 | count) : 0;
        font_bitmap box = font_cell_box(e, faces, count, code, cells, grid, size, flags, font_cell_base(faces, count, size, flags), ident, &dx, &dy, ink);
        b32 gx = x + dx, gy = y + dy;

        //      The same arithmetic as font_cell's known branch (its comments are there).
        plan->left = x < 0 ? 0 : x, plan->right = x + width > target->width ? target->width : x + width;
        plan->top = y < 0 ? 0 : y, plan->bottom = y + height > target->height ? target->height : y + height;
        plan->from = gx > plan->left ? gx : plan->left, plan->to = gx + box.width < plan->right ? gx + box.width : plan->right;
        plan->above = gy > plan->top ? gy : plan->top, plan->below = gy + box.height < plan->bottom ? gy + box.height : plan->bottom;
        if (plan->left >= plan->right || plan->top >= plan->bottom)
        {
                plan->empty = true;
                return;
        }
        if (plan->from >= plan->to || plan->above >= plan->below)
                plan->from = plan->to = plan->left, plan->above = plan->below = plan->top;
        if (plan->from < plan->to)
        {
                plan->translate = true;
                plan->coverage = box.coverage + (positive)(plan->above - gy) * box.stride + (plan->from - gx);
                plan->stride = box.stride;
                plan->strength = font_strength(e, size, flags);
                plan->tones = font_tones(e, colour, background, plan->strength);
        }
}

//      A cluster one cell wide: a base code point and its combining marks, several code points that are one cell. Each code point's
//      glyph box is rasterized as font_cell_box does for a single cell (cells 1), placed at its own offset in the cell, and the boxes
//      are combined into one cell-sized coverage in the caller's scratch by saturating add (min 255, a plus b), the rule the rig
//      measured closest to font_draw's blend of the marks over the ground. The plan is font_cell_plan's known-ground branch with that
//      coverage as its box, so font_plan_paint draws it and the recorder records the same plan: one coverage, drawn once.
//      unknown: the background is FONT_UNKNOWN (font_draw blends it, not planned). empty: the cell is off the target.
fn font_cluster_plan(font_engine address_to e, const font_face address_to const address_to faces, p32 count, const p32 address_to codes, p32 n,
                     const font_metrics address_to grid, p32 size, p32 flags, const font_target address_to target, b32 x, b32 y, p32 colour,
                     p32 background, p8 address_to scratch, font_cell_plan_t address_to plan)
{
        b32 width = grid->cell_width, height = grid->cell_height;
        b32 dx, dy;
        b16 ink[2];

        memory_zero(plan, sizeof(*plan));
        plan->ground = background & 0xffffff;
        if (background & FONT_UNKNOWN)
        {
                plan->unknown = true;
                return;
        }
        flags = font_polarity(flags, colour, background);
        memory_zero(scratch, (positive)width * (positive)height);
        for (p32 i = 0; i < n; i++)
        {
                p64 chain = font_chain(faces, count, 1);
                p32 ident = chain ? font_identify(e, chain, font_cell_shape(grid, count, 1), 1 << 3 | count) : 0;
                font_bitmap box = font_cell_box(e, faces, count, codes[i], 1, grid, size, flags, font_cell_base(faces, count, size, flags), ident, &dx, &dy, ink);

                for (b32 r = 0; r < box.height; r++)
                        for (b32 c = 0; c < box.width; c++)
                        {
                                b32 cx = dx + c, cy = dy + r;
                                p8 address_to at;
                                unsigned int sum;

                                if (cx < 0 || cy < 0 || cx >= width || cy >= height)
                                        continue;
                                at = scratch + (positive)cy * (positive)width + (positive)cx;
                                sum = (unsigned int)*at + box.coverage[(positive)r * box.stride + c];
                                *at = (p8)(sum > 255 ? 255 : sum);
                        }
        }

        //      The cell's rectangle on the target, cut to it, and the coverage's box the whole cell.
        plan->left = x < 0 ? 0 : x, plan->right = x + width > target->width ? target->width : x + width;
        plan->top = y < 0 ? 0 : y, plan->bottom = y + height > target->height ? target->height : y + height;
        if (plan->left >= plan->right || plan->top >= plan->bottom)
        {
                plan->empty = true;
                return;
        }
        plan->from = plan->left, plan->to = plan->right, plan->above = plan->top, plan->below = plan->bottom;
        plan->translate = true;
        plan->coverage = scratch + (positive)(plan->above - y) * (positive)width + (plan->from - x);
        plan->stride = (positive)width;
        plan->strength = font_strength(e, size, flags);
        plan->tones = font_tones(e, colour, background, plan->strength);
}

//      font_cell's known-ground drawing from a plan, one place for it: the rectangle on the ground, then the box translated through the tones.
//      It is font_cell's result when the plan is font_cell_plan's or font_cluster_plan's (the rig checks both): the translate ignores what
//      the rectangle was before.
fn font_plan_paint(const font_cell_plan_t address_to plan, const font_target address_to target)
{
        if (plan->unknown || plan->empty)
                return;
        font_ground(target, plan->left, plan->top, plan->right, plan->bottom, plan->ground);
        if (plan->translate)
                memory_translate_u32_rect(target->pixels + (positive)plan->above * target->stride + plan->from, target->stride, plan->coverage,
                                          plan->stride, (positive)(plan->to - plan->from), (positive)(plan->below - plan->above), plan->tones);
}

//      Rows from to from + rows of one cell's coverage, width bytes each,
//      into the strip at into, stride bytes a row. A cell's own bitmap that
//      is the whole cell is a copy a row: one 16-byte word for cells up to
//      16 wide, its tail landing on the next cell's place, which that cell
//      writes after, or in the strip row's padding past the last cell. A
//      blank or a glyph that hangs out of its cell is the cell's zeros with
//      what of the glyph is inside.
/*
        Sixteen bytes a row, rows of them: the strip's copy of a whole cell's coverage, its tail in the next cell's place
        (which that cell writes after). It is memory_copy_strip_16 in lib.c, one body on each machine, where it was a
        body here for x86-64 and the compiler's loop of 16-byte loads and stores on the other two.
*/
static inline fn font_strip16(p8 address_to into, positive stride, const p8 address_to source, positive source_stride,
                              positive rows)
{
        memory_copy_strip_16(into, stride, source, source_stride, rows);
}

static inline fn font_lay_cell(p8 address_to into, positive stride, const font_bitmap address_to box, b32 dx,
                               b32 dy, b32 width, b32 height, b32 from, b32 rows, const p8 address_to limit)
{
        if (box->width == width && box->height == height && !dx && !dy && box->coverage)
        {
                const p8 address_to source = box->coverage + (positive)from * box->stride;

                //      The word reads past the bitmap's row into the atlas,
                //      never past the atlas.
                if (width <= 16 && source + (positive)(rows - 1) * box->stride + 16 <= limit)
                        font_strip16(into, stride, source, box->stride, (positive)rows);
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
        p8 found = false;
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
