/*
        test/checks/font.c: the font engine and shaping.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_font
#include "../../src/lib.util.c"
#include "../../src/canvas/font.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        The text engine against fonts made here, so that every number it has
        to produce is known without a reference renderer.

        Two fonts are written byte by byte: a proportional one (units per em
        1024, long loca, cmap format 4 with a delta segment and a glyph-array
        segment, GPOS kerning through a format 1 pair, a format 2 class pair
        behind an extension lookup and a liga feature that must not apply,
        OS/2 with USE_TYPO_METRICS, composites scaled, turned and
        point-matched, one that contains itself) and a monospace one (2048,
        short loca, cmap format 12 preferred over a format 4 that disagrees,
        the kern table, no x-height in OS/2).

        A square whose corners sit on the 1/1024 grid is drawn exactly, so its
        bytes are 255 times the overlap of two intervals, rounded to the
        nearest, at every phase. A circle of eight off-curve points is held to
        a double-precision rendering of the same quadratics within one level a
        pixel. Composites are held to the simple glyphs they equal. The drawn
        cells are held to their seams: an arm reaching an edge leaves the edge
        exactly as the plain line of its weight does, so neighbours join at
        every cell size.

        Then the hostile half: each font cut at every length and mutated some
        thousands of times, every glyph drawn, every pair kerned and a line
        laid out, with the font's last byte against a page that faults. A read
        past the end ends the run.
*/

typedef struct
{
        p8 bytes[4096];
        positive size;
} font_table;

static fn table16(font_table address_to t, p32 value)
{
        t->bytes[t->size++] = (p8)(value >> 8);
        t->bytes[t->size++] = (p8)value;
}

static fn table32(font_table address_to t, p32 value)
{
        table16(t, value >> 16);
        table16(t, value & 0xffff);
}

//      Patch a 16-bit offset at at with the distance from base to here.
static fn table_mark(font_table address_to t, positive at, positive base)
{
        positive value = t->size - base;

        t->bytes[at] = (p8)(value >> 8);
        t->bytes[at + 1] = (p8)value;
}

static fn table_pad(font_table address_to t, positive size)
{
        while (t->size < size)
                t->bytes[t->size++] = 0;
}

//      The sfnt: a directory of count tables, then the tables, each aligned.
static positive font_build(p8 address_to out, p32 count, const p32 address_to tags,
                           font_table address_to const address_to tables)
{
        positive at = 12 + 16 * (positive)count;

        memory_zero(out, at);
        out[1] = 1;
        out[5] = (p8)count;
        for (p32 i = 0; i < count; i++)
        {
                p8 address_to record = out + 12 + 16 * i;

                for (p32 k = 0; k < 4; k++)
                {
                        record[k] = (p8)(tags[i] >> (24 - 8 * k));
                        record[8 + k] = (p8)(at >> (24 - 8 * k));
                        record[12 + k] = (p8)(tables[i]->size >> (24 - 8 * k));
                }
                memory_copy(out + at, tables[i]->bytes, tables[i]->size);
                at += (tables[i]->size + 3) & ~(positive)3;
        }
        return at;
}

//      A simple glyph, flags and coordinates in their shortest spellings and
//      repeated flags folded, so that every path of the parser is walked.
static fn glyph_simple(font_table address_to glyf, const b32 address_to xy, const p8 address_to on,
                       p32 count, const p16 address_to ends, p32 contours)
{
        b32 x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
        p8 flags[64];

        for (p32 i = 0; i < count; i++)
        {
                x0 = xy[2 * i] < x0 ? xy[2 * i] : x0;
                x1 = xy[2 * i] > x1 ? xy[2 * i] : x1;
                y0 = xy[2 * i + 1] < y0 ? xy[2 * i + 1] : y0;
                y1 = xy[2 * i + 1] > y1 ? xy[2 * i + 1] : y1;
        }
        table16(glyf, contours);
        table16(glyf, (p32)x0 & 0xffff), table16(glyf, (p32)y0 & 0xffff);
        table16(glyf, (p32)x1 & 0xffff), table16(glyf, (p32)y1 & 0xffff);
        for (p32 c = 0; c < contours; c++)
                table16(glyf, ends[c]);
        table16(glyf, 2);
        table16(glyf, 0xb001);

        for (p32 i = 0; i < count; i++)
        {
                b32 dx = xy[2 * i] - (i ? xy[2 * i - 2] : 0), dy = xy[2 * i + 1] - (i ? xy[2 * i - 1] : 0);
                p8 f = on[i];

                f |= !dx ? 0x10 : dx > -256 && dx < 256 ? (p8)(0x02 | (dx > 0 ? 0x10 : 0)) : 0;
                f |= !dy ? 0x20 : dy > -256 && dy < 256 ? (p8)(0x04 | (dy > 0 ? 0x20 : 0)) : 0;
                flags[i] = f;
        }
        for (p32 i = 0; i < count;)
        {
                p32 run = 1;

                while (i + run < count && flags[i + run] == flags[i])
                        run++;
                glyf->bytes[glyf->size++] = (p8)(flags[i] | (run > 1 ? 8 : 0));
                if (run > 1)
                        glyf->bytes[glyf->size++] = (p8)(run - 1);
                i += run;
        }
        for (p32 axis = 0; axis < 2; axis++)
                for (p32 i = 0; i < count; i++)
                {
                        b32 d = xy[2 * i + axis] - (i ? xy[2 * i - 2 + axis] : 0);

                        if (flags[i] & (axis ? 4 : 2))
                                glyf->bytes[glyf->size++] = (p8)(d < 0 ? -d : d);
                        else if (d)
                                table16(glyf, (p32)d & 0xffff);
                }
        table_pad(glyf, (glyf->size + 3) & ~(positive)3);
}

//      A glyph of components: each with its flags, child, two arguments
//      (words) and its transform words.
static fn glyph_composite(font_table address_to glyf, p32 parts, const p32 address_to flags,
                          const p32 address_to children, const b32 address_to args,
                          const b16 address_to transforms)
{
        table16(glyf, 0xffff);
        table16(glyf, 0), table16(glyf, 0), table16(glyf, 0), table16(glyf, 0);
        for (p32 k = 0, t = 0; k < parts; k++)
        {
                p32 f = flags[k] | 1 | (k + 1 < parts ? 0x20 : 0);
                p32 words = f & 8 ? 1 : f & 0x40 ? 2 : f & 0x80 ? 4 : 0;

                table16(glyf, f);
                table16(glyf, children[k]);
                table16(glyf, (p32)args[2 * k] & 0xffff);
                table16(glyf, (p32)args[2 * k + 1] & 0xffff);
                for (p32 w = 0; w < words; w++)
                        table16(glyf, (p32)(b32)transforms[t++] & 0xffff);
        }
        table_pad(glyf, (glyf->size + 3) & ~(positive)3);
}

static p8 font_one[8192], font_two[8192];
static positive font_one_size, font_two_size;

//      One pair adjustment subtable of format 1: first then second is value.
static fn gpos_pair(font_table address_to t, p32 first, p32 second, b32 value)
{
        positive base = t->size;

        table16(t, 1), table16(t, 12), table16(t, 4), table16(t, 0), table16(t, 1), table16(t, 18);
        table16(t, 1), table16(t, 1), table16(t, first);
        table16(t, 1), table16(t, second), table16(t, (p32)value & 0xffff);
        (void)base;
}

/*
        The proportional font, units per em 1024. Glyphs:
            0  the missing glyph, a bar
            1  a square, x 64..576, y 100..612
            2  a circle: eight off-curve points round (600, 500)
            3  glyph 1 at half scale, moved by (100, 200)
            4  glyph 1 turned a quarter (0 1, -1 0), then a second glyph 1
               placed by matching its point 0 to the first's point 2
            5  a box with a hole, the hole wound the other way
            6  two squares wound alike, overlapping at one corner
            7  glyph 3 written out: x 132..388, y 250..506
            8  a composite that contains itself
            9  a square whose top is the x-height, y 0..500
           10  glyph 4 written out
*/
static const b32 circle_one[] = {1000, 500, 883, 783, 600, 900, 317, 783, 200, 500, 317, 217, 600, 100, 883, 217};
static const b32 circle_two[] = {2000, 1000, 1766, 1566, 1200, 1800, 634, 1566, 400, 1000, 634, 434, 1200, 200, 1766, 434};

static fn font_make_one()
{
        static font_table head, hhea, maxp, os2, hmtx, cmap, loca, glyf, post, gpos;
        static const b32 bar[] = {100, 0, 100, 700, 400, 700, 400, 0};
        static const b32 square[] = {64, 100, 64, 612, 576, 612, 576, 100};
        static const b32 holed[] = {0, 0, 0, 800, 800, 800, 800, 0, 200, 200, 600, 200, 600, 600, 200, 600};
        static const b32 overlap[] = {0, 0, 0, 600, 600, 600, 600, 0, 580, 580, 580, 900, 900, 900, 900, 580};
        static const b32 half[] = {132, 250, 132, 506, 388, 506, 388, 250};
        static const b32 low[] = {64, 0, 64, 500, 576, 500, 576, 0};
        static const p8 ons[8] = {1, 1, 1, 1, 1, 1, 1, 1}, offs[8] = {0};
        static const p16 four[1] = {3}, eight[2] = {3, 7}, octagon[1] = {7};
        b32 turned[16];
        positive offsets[12];

        for (p32 i = 0; i < 4; i++)
        {
                turned[2 * i] = -square[2 * i + 1];
                turned[2 * i + 1] = square[2 * i];
                turned[8 + 2 * i] = square[2 * i] + (-612 - 64);
                turned[8 + 2 * i + 1] = square[2 * i + 1] + (576 - 100);
        }

        offsets[0] = glyf.size, glyph_simple(&glyf, bar, ons, 4, four, 1);
        offsets[1] = glyf.size, glyph_simple(&glyf, square, ons, 4, four, 1);
        offsets[2] = glyf.size, glyph_simple(&glyf, circle_one, offs, 8, octagon, 1);
        offsets[3] = glyf.size;
        glyph_composite(&glyf, 1, (const p32[]){0x0a}, (const p32[]){1}, (const b32[]){100, 200},
                        (const b16[]){8192});
        offsets[4] = glyf.size;
        glyph_composite(&glyf, 2, (const p32[]){0x82, 0}, (const p32[]){1, 1}, (const b32[]){0, 0, 2, 0},
                        (const b16[]){0, 16384, -16384, 0});
        offsets[5] = glyf.size, glyph_simple(&glyf, holed, ons, 8, eight, 2);
        offsets[6] = glyf.size, glyph_simple(&glyf, overlap, ons, 8, eight, 2);
        offsets[7] = glyf.size, glyph_simple(&glyf, half, ons, 4, four, 1);
        offsets[8] = glyf.size;
        glyph_composite(&glyf, 1, (const p32[]){0x02}, (const p32[]){8}, (const b32[]){10, 10}, null);
        offsets[9] = glyf.size, glyph_simple(&glyf, low, ons, 4, four, 1);
        offsets[10] = glyf.size, glyph_simple(&glyf, turned, ons, 8, eight, 2);
        offsets[11] = glyf.size;
        for (p32 g = 0; g < 12; g++)
                table32(&loca, (p32)offsets[g]);

        table32(&head, 0x00010000), table32(&head, 0x00010000), table32(&head, 0), table32(&head, 0x5f0f3cf5);
        table16(&head, 0), table16(&head, 1024);
        table_pad(&head, 50);
        table16(&head, 1), table16(&head, 0);

        table32(&hhea, 0x00010000), table16(&hhea, 900), table16(&hhea, (p32)-300 & 0xffff), table16(&hhea, 0);
        table_pad(&hhea, 34);
        table16(&hhea, 11);

        table32(&maxp, 0x00005000), table16(&maxp, 11);

        static const p32 advances[11] = {500, 640, 1100, 500, 700, 800, 900, 500, 500, 640, 700};
        static const b32 sides[11] = {100, 64, 200, 0, 0, 0, 0, 132, 0, 64, -612};
        for (p32 g = 0; g < 11; g++)
                table16(&hmtx, advances[g]), table16(&hmtx, (p32)sides[g] & 0xffff);

        table16(&os2, 4);
        table_pad(&os2, 62);
        table16(&os2, 0x80);
        table16(&os2, 0x41), table16(&os2, 0x2192);
        table16(&os2, 800), table16(&os2, (p32)-200 & 0xffff), table16(&os2, 100);
        table16(&os2, 900), table16(&os2, 300);
        table32(&os2, 1), table32(&os2, 0);
        table16(&os2, 500), table16(&os2, 700);
        table_pad(&os2, 96);

        table32(&post, 0x00030000), table32(&post, 0);
        table16(&post, (p32)-100 & 0xffff), table16(&post, 50), table32(&post, 0);
        table_pad(&post, 32);

        //      cmap: one format 4 subtable, (3,1). Segment 0 is A..D by delta,
        //      segment 1 is U+2190..2192 through the glyph array, then 0xffff.
        table16(&cmap, 0), table16(&cmap, 1);
        table16(&cmap, 3), table16(&cmap, 1), table32(&cmap, 12);
        positive start = cmap.size;
        table16(&cmap, 4), table16(&cmap, 0), table16(&cmap, 0);
        table16(&cmap, 6), table16(&cmap, 4), table16(&cmap, 1), table16(&cmap, 2);
        table16(&cmap, 0x44), table16(&cmap, 0x2192), table16(&cmap, 0xffff);
        table16(&cmap, 0);
        table16(&cmap, 0x41), table16(&cmap, 0x2190), table16(&cmap, 0xffff);
        table16(&cmap, (1 - 0x41) & 0xffff), table16(&cmap, 0), table16(&cmap, 1);
        table16(&cmap, 0), table16(&cmap, 4), table16(&cmap, 0);
        table16(&cmap, 5), table16(&cmap, 6), table16(&cmap, 0);
        cmap.bytes[start + 2] = (p8)((cmap.size - start) >> 8);
        cmap.bytes[start + 3] = (p8)(cmap.size - start);

        //      GPOS: scripts latn and DFLT. DFLT's default language uses
        //      features 0 and 1: feature 0 is kern (lookups 0 and 1), feature
        //      1 is liga (lookup 2). latn's uses feature 2 only, a second kern
        //      feature reaching lookup 2. Lookup 2 adds 999 to glyph 1 then 1,
        //      so that pair kerns only if the wrong script, a non-kern feature
        //      or every kern feature regardless of script is taken.
        table16(&gpos, 1), table16(&gpos, 0), table16(&gpos, 0), table16(&gpos, 0), table16(&gpos, 0);
        table_mark(&gpos, 4, 0);
        positive scripts = gpos.size;
        table16(&gpos, 2), table32(&gpos, FONT_TAG('l', 'a', 't', 'n')), table16(&gpos, 0);
        table32(&gpos, FONT_TAG('D', 'F', 'L', 'T')), table16(&gpos, 0);
        table_mark(&gpos, scripts + 6, scripts);
        table16(&gpos, 4), table16(&gpos, 0);
        table16(&gpos, 0), table16(&gpos, 0xffff), table16(&gpos, 1), table16(&gpos, 2);
        table_mark(&gpos, scripts + 12, scripts);
        table16(&gpos, 4), table16(&gpos, 0);
        table16(&gpos, 0), table16(&gpos, 0xffff), table16(&gpos, 2), table16(&gpos, 0), table16(&gpos, 1);
        table_mark(&gpos, 6, 0);
        positive features = gpos.size;
        table16(&gpos, 3), table32(&gpos, FONT_TAG('k', 'e', 'r', 'n')), table16(&gpos, 0);
        table32(&gpos, FONT_TAG('l', 'i', 'g', 'a')), table16(&gpos, 0);
        table32(&gpos, FONT_TAG('k', 'e', 'r', 'n')), table16(&gpos, 0);
        table_mark(&gpos, features + 6, features);
        table16(&gpos, 0), table16(&gpos, 2), table16(&gpos, 0), table16(&gpos, 1);
        table_mark(&gpos, features + 12, features);
        table16(&gpos, 0), table16(&gpos, 1), table16(&gpos, 2);
        table_mark(&gpos, features + 18, features);
        table16(&gpos, 0), table16(&gpos, 1), table16(&gpos, 2);
        table_mark(&gpos, 8, 0);
        positive list = gpos.size;
        table16(&gpos, 3), table16(&gpos, 0), table16(&gpos, 0), table16(&gpos, 0);

        //      lookup 0: type 2, format 1, glyph 1 then 2 is -50
        table_mark(&gpos, list + 2, list);
        positive lookup = gpos.size;
        table16(&gpos, 2), table16(&gpos, 0), table16(&gpos, 1), table16(&gpos, 0);
        table_mark(&gpos, lookup + 6, lookup);
        gpos_pair(&gpos, 1, 2, -50);

        //      lookup 1: type 9 to a type 2 of format 2, where glyph 3 is class
        //      1 of the first glyphs and glyph 4 class 1 of the second: -30.
        table_mark(&gpos, list + 4, list);
        lookup = gpos.size;
        table16(&gpos, 9), table16(&gpos, 0), table16(&gpos, 1), table16(&gpos, 0);
        table_mark(&gpos, lookup + 6, lookup);
        table16(&gpos, 1), table16(&gpos, 2), table32(&gpos, 8);
        positive pair = gpos.size;
        table16(&gpos, 2), table16(&gpos, 0), table16(&gpos, 4), table16(&gpos, 0);
        table16(&gpos, 0), table16(&gpos, 0), table16(&gpos, 2), table16(&gpos, 2);
        table16(&gpos, 0), table16(&gpos, 0), table16(&gpos, 0), table16(&gpos, (p32)-30 & 0xffff);
        table_mark(&gpos, pair + 2, pair);
        table16(&gpos, 1), table16(&gpos, 1), table16(&gpos, 3);
        table_mark(&gpos, pair + 8, pair);
        table16(&gpos, 1), table16(&gpos, 3), table16(&gpos, 1), table16(&gpos, 1);
        table_mark(&gpos, pair + 10, pair);
        table16(&gpos, 2), table16(&gpos, 1), table16(&gpos, 4), table16(&gpos, 4), table16(&gpos, 1);

        //      lookup 2: +999 on glyph 1 then 1, from liga and latn's kern
        table_mark(&gpos, list + 6, list);
        lookup = gpos.size;
        table16(&gpos, 2), table16(&gpos, 0), table16(&gpos, 1), table16(&gpos, 0);
        table_mark(&gpos, lookup + 6, lookup);
        gpos_pair(&gpos, 1, 1, 999);

        static const p32 tags[] = {
            FONT_TAG('G', 'P', 'O', 'S'), FONT_TAG('O', 'S', '/', '2'), FONT_TAG('c', 'm', 'a', 'p'),
            FONT_TAG('g', 'l', 'y', 'f'), FONT_TAG('h', 'e', 'a', 'd'), FONT_TAG('h', 'h', 'e', 'a'),
            FONT_TAG('h', 'm', 't', 'x'), FONT_TAG('l', 'o', 'c', 'a'), FONT_TAG('m', 'a', 'x', 'p'),
            FONT_TAG('p', 'o', 's', 't')};
        font_table address_to const tables[] = {&gpos, &os2, &cmap, &glyf, &head, &hhea, &hmtx, &loca, &maxp, &post};
        font_one_size = font_build(font_one, 10, tags, tables);
}

/*
        The monospace font, units per em 2048, short loca. Glyphs: 0 a bar,
        1 a square (also 'x', whose top is the measured x-height), 2 a wide
        square, 3 the circle. cmap has format 12 (3,10) and a format 4 (3,1)
        that maps A to glyph 2; the format 12 one must win. Kerning is the
        kern table's: glyph 1 then 2 is -77.
*/
static fn font_make_two()
{
        static font_table head, hhea, maxp, os2, hmtx, cmap, loca, glyf, post, kern;
        static const b32 bar[] = {200, 0, 200, 1400, 800, 1400, 800, 0};
        static const b32 square[] = {128, 0, 128, 1024, 1152, 1024, 1152, 0};
        static const b32 wide[] = {0, 0, 0, 1024, 1228, 1024, 1228, 0};
        static const p8 ons[8] = {1, 1, 1, 1, 1, 1, 1, 1}, offs[8] = {0};
        static const p16 four[1] = {3}, octagon[1] = {7};
        positive offsets[5];

        offsets[0] = glyf.size, glyph_simple(&glyf, bar, ons, 4, four, 1);
        offsets[1] = glyf.size, glyph_simple(&glyf, square, ons, 4, four, 1);
        offsets[2] = glyf.size, glyph_simple(&glyf, wide, ons, 4, four, 1);
        offsets[3] = glyf.size, glyph_simple(&glyf, circle_two, offs, 8, octagon, 1);
        offsets[4] = glyf.size;
        for (p32 g = 0; g < 5; g++)
                table16(&loca, (p32)(offsets[g] / 2));

        table32(&head, 0x00010000), table32(&head, 0x00010000), table32(&head, 0), table32(&head, 0x5f0f3cf5);
        table16(&head, 0), table16(&head, 2048);
        table_pad(&head, 54);

        table32(&hhea, 0x00010000), table16(&hhea, 1600), table16(&hhea, (p32)-400 & 0xffff), table16(&hhea, 0);
        table_pad(&hhea, 34);
        table16(&hhea, 1);

        table32(&maxp, 0x00005000), table16(&maxp, 4);

        table16(&hmtx, 1228), table16(&hmtx, 200);
        table16(&hmtx, 128), table16(&hmtx, 0), table16(&hmtx, 400);

        table16(&os2, 1);
        table_pad(&os2, 86);

        table32(&post, 0x00030000), table32(&post, 0), table16(&post, 0), table16(&post, 0), table32(&post, 1);
        table_pad(&post, 32);

        table16(&cmap, 0), table16(&cmap, 2);
        table16(&cmap, 3), table16(&cmap, 1), table32(&cmap, 20);
        table16(&cmap, 3), table16(&cmap, 10), table32(&cmap, 52);
        table16(&cmap, 4), table16(&cmap, 32), table16(&cmap, 0), table16(&cmap, 4), table16(&cmap, 4), table16(&cmap, 1), table16(&cmap, 0);
        table16(&cmap, 0x41), table16(&cmap, 0xffff), table16(&cmap, 0), table16(&cmap, 0x41), table16(&cmap, 0xffff);
        table16(&cmap, (2 - 0x41) & 0xffff), table16(&cmap, 1), table16(&cmap, 0), table16(&cmap, 0);
        table16(&cmap, 12), table16(&cmap, 0), table32(&cmap, 16 + 3 * 12), table32(&cmap, 0), table32(&cmap, 3);
        table32(&cmap, 0x41), table32(&cmap, 0x42), table32(&cmap, 1);
        table32(&cmap, 0x78), table32(&cmap, 0x78), table32(&cmap, 1);
        table32(&cmap, 0x1f600), table32(&cmap, 0x1f600), table32(&cmap, 3);

        table16(&kern, 0), table16(&kern, 1);
        table16(&kern, 0), table16(&kern, 14 + 6 * 2), table16(&kern, 1);
        table16(&kern, 2), table16(&kern, 12), table16(&kern, 1), table16(&kern, 0);
        table16(&kern, 1), table16(&kern, 2), table16(&kern, (p32)-77 & 0xffff);
        table16(&kern, 2), table16(&kern, 1), table16(&kern, 33);

        static const p32 tags[] = {
            FONT_TAG('O', 'S', '/', '2'), FONT_TAG('c', 'm', 'a', 'p'), FONT_TAG('g', 'l', 'y', 'f'),
            FONT_TAG('h', 'e', 'a', 'd'), FONT_TAG('h', 'h', 'e', 'a'), FONT_TAG('h', 'm', 't', 'x'),
            FONT_TAG('k', 'e', 'r', 'n'), FONT_TAG('l', 'o', 'c', 'a'), FONT_TAG('m', 'a', 'x', 'p'),
            FONT_TAG('p', 'o', 's', 't')};
        font_table address_to const tables[] = {&os2, &cmap, &glyf, &head, &hhea, &hmtx, &kern, &loca, &maxp, &post};
        font_two_size = font_build(font_two, 10, tags, tables);
}

static font_engine engine, small_engine;
static p8 atlas[512 * 512], small_atlas[128 * 32];
static p8 coverage[1 << 16];

static b32 render(const font_face address_to face, p32 glyph, p32 pixels64, p32 phase, p32 flags,
                  font_bitmap address_to box)
{
        memory_zero(coverage, sizeof(coverage));
        return font_render(&engine, face, glyph, pixels64, phase, flags, coverage, 256, sizeof(coverage), box);
}

static p32 level_of(p64 numerator, p64 denominator)
{
        return (p32)((255 * numerator + denominator / 2) / denominator);
}

/*
        Glyph 1, the square, at 16 px (a unit is 1/64 px) and 24 px (3/128),
        every phase: every edge is on the 1/1024 grid, so every pixel's area
        is a product of two exact intervals and its byte is known exactly.
*/
static fn check_square(const font_face address_to face)
{
        for (p32 size = 16; size <= 24; size += 8)
                for (p32 phase = 0; phase < FONT_PHASES; phase++)
                {
                        font_bitmap box;
                        b32 refused = render(face, 1, size * 64, phase, FONT_UNHINTED, &box);
                        //      Edges in 1/8192 px, size * 8 of them to the unit.
                        p64 x0 = 64 * size * 8 + phase * 1024, x1 = 576 * size * 8 + phase * 1024;
                        p64 y0 = 100 * size * 8, y1 = 612 * size * 8;
                        p8 exact = !refused && box.left == (b32)(x0 >> 13) &&
                                     box.top == (b32)((y1 + 8191) >> 13) && box.width < 256;

                        for (b32 r = 0; exact && r < box.height; r++)
                                for (b32 c = 0; c < box.width; c++)
                                {
                                        b64 px = (b64)(box.left + c) << 13, py = (b64)(box.top - 1 - r) << 13;
                                        b64 ox = ((b64)x1 < px + 8192 ? (b64)x1 : px + 8192) - ((b64)x0 > px ? (b64)x0 : px);
                                        b64 oy = ((b64)y1 < py + 8192 ? (b64)y1 : py + 8192) - ((b64)y0 > py ? (b64)y0 : py);
                                        p32 want = ox > 0 && oy > 0 ? level_of((p64)(ox * oy), (p64)8192 * 8192) : 0;

                                        exact = exact && coverage[r * 256 + c] == want;
                                }
                        check("square: every pixel is 255 times its exact area, every phase and size", exact);
                }
}

/*
        A circle of eight off-curve points against the same quadratics drawn
        in doubles: each flattened until no piece is 2^-14 px from its curve,
        and every edge's area per cell exact (the polygon accumulation every
        reference rasterizer shares). Within one level a pixel.
*/
static f64 reference[130 * 130];

static fn reference_line(f64 x0, f64 y0, f64 x1, f64 y1, b32 height)
{
        if (y0 == y1)
                return;

        f64 sign = 1;
        if (y0 > y1)
        {
                f64 t = x0;
                x0 = x1, x1 = t, t = y0, y0 = y1, y1 = t, sign = -1;
        }
        f64 slope = (x1 - x0) / (y1 - y0), x = x0;
        for (b32 y = (b32)y0; y < height && y < y1; y++)
        {
                f64 *row = reference + y * 130;
                f64 dy = ((y + 1 < y1 ? y + 1 : y1) - (y > y0 ? y : y0)), next = x + slope * dy, d = dy * sign;
                f64 lo = x < next ? x : next, hi = x < next ? next : x;
                b32 a = (b32)lo, b = (b32)hi + 1;

                lo = lo < 0 ? 0 : lo;
                if (b <= a + 1)
                {
                        f64 middle = 0.5 * (x + next) - a;
                        row[a] += d - d * middle;
                        row[a + 1] += d * middle;
                }
                else
                {
                        f64 s = 1 / (hi - lo), first = lo - a, last = hi - (b - 1);
                        f64 a0 = 0.5 * s * (1 - first) * (1 - first), am = 0.5 * s * last * last;
                        row[a] += d * a0;
                        if (b == a + 2)
                                row[a + 1] += d * (1 - a0 - am);
                        else
                        {
                                f64 a1 = s * (1.5 - first);
                                row[a + 1] += d * (a1 - a0);
                                for (b32 c = a + 2; c < b - 1; c++)
                                        row[c] += d * s;
                                row[b - 1] += d * (1 - (a1 + (b - a - 3) * s) - am);
                        }
                        row[b] += d * am;
                }
                x = next;
        }
}

static fn check_circle(const font_face address_to face, p32 glyph, const b32 address_to points, f64 units)
{
        for (p32 size = 9; size <= 57; size += 24)
        {
                font_bitmap box;
                b32 refused = render(face, glyph, size * 64, 3, FONT_UNHINTED, &box);
                f64 scale = size / units, shift = 3.0 / FONT_PHASES;
                p32 worst = 0;

                memory_zero(reference, sizeof(reference));
                for (p32 k = 0; k < 8; k++)
                {
                        //      Point k is a control; the curve runs from the midpoint
                        //      before it to the midpoint after it.
                        p32 before = (k + 7) % 8, after = (k + 1) % 8;
                        f64 px[3], py[3];
                        const b32 address_to q[3] = {points + 2 * before, points + 2 * k, points + 2 * after};

                        for (p32 j = 0; j < 3; j++)
                        {
                                px[j] = q[j][0] * scale + shift - box.left;
                                py[j] = box.top - q[j][1] * scale;
                        }
                        f64 ax = 0.5 * (px[0] + px[1]), ay = 0.5 * (py[0] + py[1]);
                        f64 bx = 0.5 * (px[1] + px[2]), by = 0.5 * (py[1] + py[2]);
                        f64 lastx = ax, lasty = ay;

                        for (p32 i = 1; i <= 512; i++)
                        {
                                f64 t = i / 512.0, u = 1 - t;
                                f64 x = u * u * ax + 2 * u * t * px[1] + t * t * bx;
                                f64 y = u * u * ay + 2 * u * t * py[1] + t * t * by;

                                reference_line(lastx, lasty, x, y, box.height);
                                lastx = x, lasty = y;
                        }
                }
                for (b32 r = 0; r < box.height; r++)
                {
                        f64 sum = 0;

                        for (b32 c = 0; c < box.width; c++)
                        {
                                sum += reference[r * 130 + c];

                                f64 area = sum < 0 ? -sum : sum;
                                p32 want = (p32)(255 * (area > 1 ? 1 : area) + 0.5), got = coverage[r * 256 + c];
                                p32 off = got > want ? got - want : want - got;

                                worst = off > worst ? off : worst;
                        }
                }
                check("circle of quadratics: within one level of the exact area, every pixel",
                      !refused && box.width && box.width < 128 && box.height < 128 && worst <= 1);
        }
}

//      Two glyphs of a face drawn alike at every phase and two sizes.
static p8 same_glyphs(const font_face address_to face, p32 one, p32 two)
{
        static p8 first[1 << 16];
        p8 same = true;

        for (p32 size = 13; size <= 40; size += 27)
                for (p32 phase = 0; phase < FONT_PHASES; phase++)
                {
                        font_bitmap a, b;

                        render(face, one, size * 64, phase, FONT_UNHINTED, &a);
                        memory_copy(first, coverage, sizeof(first));
                        render(face, two, size * 64, phase, FONT_UNHINTED, &b);
                        same = same && a.left == b.left && a.top == b.top && a.width == b.width &&
                               a.height == b.height && !memory_compare(first, coverage, sizeof(first)) &&
                               a.width > 0;
                }
        return same;
}

static fn check_outlines(const font_face address_to one, const font_face address_to two)
{
        font_bitmap box;

        check_square(one);
        check_circle(one, 2, circle_one, 1024);
        check_circle(two, 3, circle_two, 2048);
        check("composite: half scale and an offset draws as the glyph written out", same_glyphs(one, 3, 7));
        check("composite: a quarter turn and a point match draw as the glyph written out", same_glyphs(one, 4, 10));
        check("composite: one that contains itself is refused", render(one, 8, 1024, 0, 0, &box) == FONT_BAD);

        //      The box with a hole at 20 px: 800 units is 15.625 px, the hole
        //      3.90625..11.71875 px. Pixels wholly in the hole are empty, wholly
        //      in the ring full.
        render(one, 5, 20 * 64, 0, FONT_UNHINTED, &box);
        check("hole: the inside wound the other way is empty", coverage[8 * 256 + 8] == 0 && coverage[7 * 256 + 6] == 0);
        check("hole: the ring is full", coverage[1 * 256 + 1] == 255 && coverage[8 * 256 + 1] == 255);

        //      Two squares wound alike, 0..600 and 580..900 units: at 20 px
        //      11.71875 and 11.328125 px, so the pixel from 11 to 12 on both
        //      axes holds a corner of each. Its union is 0.71875^2 + 0.671875^2
        //      - 0.390625^2 of the pixel, 208; the clamped sum would be 246.
        render(one, 6, 20 * 64, 0, FONT_UNHINTED, &box);
        check("overlap: the union of two corners in one pixel is exact, not their sum",
              box.top == 18 && coverage[6 * 256 + 11] == 208);
        check("overlap: inside each square full, beside one its own edge",
              coverage[12 * 256 + 5] == 255 && coverage[3 * 256 + 14] == 255 &&
                  coverage[12 * 256 + 11] == level_of(736, 1024));

        //      Fitting: glyph 9's top is the x-height, 500 units, 5.37 px at 11
        //      px. Hinted, the top lands on 5 px and the top row is whole;
        //      unhinted, it is 6 rows with a top row 0.37 covered.
        render(one, 9, 11 * 64, 0, 0, &box);
        check("fitting: the x-height lands on a whole pixel", box.height == 5 && box.top == 5 && coverage[1] == 255);
        render(one, 9, 11 * 64, 0, FONT_UNHINTED, &box);
        check("fitting: unhinted, the x-height row is partly covered",
              box.height == 6 && coverage[1] == level_of(5500 - 5120, 1024));

        //      Darkening thickens at 10 px and leaves 40 px alone.
        for (p32 size = 10; size <= 40; size += 30)
        {
                positive plain = 0, dark = 0;

                render(one, 2, size * 64, 0, FONT_UNHINTED, &box);
                for (positive i = 0; i < sizeof(coverage); i++)
                        plain += coverage[i];
                render(one, 2, size * 64, 0, FONT_UNHINTED | FONT_DARKEN, &box);
                for (positive i = 0; i < sizeof(coverage); i++)
                        dark += coverage[i];
                check("darkening: small text gains weight, large text none", size < 32 ? dark > plain : dark == plain);
        }
}

static fn check_tables(const font_face address_to one, const font_face address_to two)
{
        p32 size = 16 * 64;
        font_metrics m = font_metrics_at(one, size);

        check("metrics: typo metrics when USE_TYPO_METRICS is set",
              m.ascent == 819200 && m.descent == -204800 && m.gap == 102400);
        check("metrics: x-height and cap height from OS/2", m.x_height == 512000 && m.cap_height == 716800);
        check("metrics: the cell is the ascent, descent and gap rounded out",
              m.cell_height == 13 + 4 + 2 && m.baseline == 13 + 1);
        m = font_metrics_at(two, size);
        check("metrics: hhea when the typo bit is clear, x-height measured from x",
              m.ascent == 819200 && m.descent == -204800 && m.x_height == 524288);
        check("metrics: the monospace cell is the advance rounded", m.cell_width == 10 && m.cell_height == 17);

        check("cmap 4: a delta segment", font_glyph(one, 'A') == 1 && font_glyph(one, 'D') == 4);
        check("cmap 4: past the segment, nothing", font_glyph(one, 'E') == 0 && font_glyph(one, '@') == 0);
        check("cmap 4: the glyph array", font_glyph(one, 0x2190) == 5 && font_glyph(one, 0x2191) == 6 &&
                                            font_glyph(one, 0x2192) == 0);
        check("cmap 4: nothing past the plane", font_glyph(one, 0x1f600) == 0);
        check("cmap 12: preferred over format 4", font_glyph(two, 'A') == 1 && font_glyph(two, 'B') == 2);
        check("cmap 12: a group past the plane", font_glyph(two, 0x1f600) == 3 && font_glyph(two, 0x1f601) == 0);

        check("advance: exact in 16.16", font_advance(one, 1, size) == 655360 && font_advance(two, 3, size) == 628736);
        check("GPOS: a format 1 pair", font_kerning(one, 1, 2, size) == -51200);
        check("GPOS: a format 2 class pair behind an extension lookup", font_kerning(one, 3, 4, size) == -30720);
        check("GPOS: DFLT's kern feature only, not liga's lookup nor latn's kern",
              font_kerning(one, 1, 1, size) == 0);
        check("GPOS: a pair no lookup names", font_kerning(one, 2, 1, size) == 0);
        check("kern: the table's pairs both ways", font_kerning(two, 1, 2, size) == -39424 &&
                                                     font_kerning(two, 2, 1, size) == 16896);
}

/*
        A run past 32768 pixels of advance: its pen is 16.16 and was 32 bits, so
        the 4000th glyph of a line of M was placed where the wrap put it. Each
        glyph's x must be its index times the advance, through the whole run.
*/
static font_place long_run[4000];

static fn check_long_run(const font_face address_to two)
{
        const font_face address_to chain[1] = {two};
        p8 text[4000];
        positive placed;
        b64 advance, steps = 0;
        p8 exact = true;

        for (positive i = 0; i < sizeof(text); i++)
                text[i] = 'M';
        placed = font_layout(&engine, chain, 1, text, sizeof(text), 16 * 64, FONT_UNKERNED, long_run, 4000);
        advance = long_run[1].x - long_run[0].x;
        for (positive i = 0; i < placed; i++)
        {
                exact = exact && long_run[i].x == (b64)i * advance;
                steps++;
        }
        check("layout: a run of 4000 glyphs past 32768 px places every glyph where its advance puts it",
              placed == 4000 && advance > 0 && exact && steps == 4000 && long_run[3999].x > 0x80000000ll);
}

static fn check_layout(const font_face address_to one, const font_face address_to two)
{
        const font_face address_to chain[2] = {one, two};
        static const p8 text[] = "AB\xf0\x9f\x98\x80x\xff";
        font_place places[8];
        positive count = font_layout(&engine, chain, 2, text, sizeof(text) - 1, 16 * 64, 0, places, 8);

        check("layout: one place a code point, the bad byte one too", count == 5);
        check("layout: each from the first face that has it",
              places[0].face == 0 && places[0].glyph == 1 && places[1].face == 0 && places[1].glyph == 2 &&
                  places[2].face == 1 && places[2].glyph == 3 && places[3].face == 1 && places[3].glyph == 1 &&
                  places[4].face == 0 && places[4].glyph == 0);
        check("layout: pens exact, kerned within a face only",
              places[0].x == 0 && places[1].x == 655360 - 51200 && places[2].x == 604160 + 1126400 &&
                  places[3].x == 1730560 + 628736 && places[4].x == 2359296 + 628736);
        count = font_layout(&engine, chain, 2, text, 2, 16 * 64, FONT_UNKERNED, places, 8);
        check("layout: unkerned on request", count == 2 && places[1].x == 655360);
}

/*
        The sRGB tables: every byte survives the round trip, light is
        monotone, a blend at no coverage is the ground and at full the ink,
        half of black over white is 188, half the light, and half of white
        over black 128, the light text's mix of the bytes. The way back is
        8 KB of words for 65536 lights, so each light's answer is held to the
        definition -- the bytes whose half-step light it has reached, counted
        -- and a blend to the formula written out over the definition, for
        every coverage level, and ground and ink bytes from the whole range.
*/
static p32 light_begins[257];

static p32 light_byte(p32 light)
{
        p32 low = 0, high = 255;

        while (low < high)
        {
                p32 middle = (low + high + 1) / 2;

                if (light_begins[middle] <= light)
                        low = middle;
                else
                        high = middle - 1;
        }
        return low;
}

static p32 light_mix(p32 back, p32 fore, p32 a, p8 linear, p32 weight)
{
        p32 mixed = 0;

        for (p32 shift = 0; shift < 24; shift += 8)
        {
                p32 b = back >> shift & 255, f = fore >> shift & 255;
                p32 plain = (b * (255 - a) + f * a + 127) / 255;
                p32 value = linear ? light_byte((font_to_linear[b] * (255 - a) + font_to_linear[f] * a + 127) / 255) : plain;

                value = linear ? (value * (64 - weight) + plain * weight + 32) >> 6 : value;
                mixed |= value << shift;
        }
        return mixed;
}

static fn check_light()
{
        p8 round = true, rising = true;

        for (p32 s = 0; s < 256; s++)
        {
                round = round && font_srgb_bytes[font_to_linear[s]] == s;
                rising = rising && (!s || font_to_linear[s] > font_to_linear[s - 1]);
        }
        check("sRGB: every byte survives the round trip through 16-bit light", round);
        check("sRGB: light rises with every byte", rising && font_to_linear[255] == 65535);

        for (p32 s = 1; s < 256; s++)
        {
                f64 half = font_srgb_to_linear((s - 0.5) / 255) * 65535;

                light_begins[s] = (p32)half + ((f64)(p32)half < half);
        }
        light_begins[0] = 0;
        p8 back = true;
        for (p32 light = 0; light < 65536; light++)
                back = back && font_srgb_bytes[light] == light_byte(light);
        check("sRGB: all 65536 lights go back to the byte whose half step they have reached", back);

        p8 same = true;
        static const struct { p8 linear; p32 weight; } modes[] = {{true, 0}, {true, 21}, {true, 43}, {true, 63}, {true, 64}, {false, 0}};
        for (p32 m = 0; m < 6; m++)
        {
                engine.linear = modes[m].linear;
                engine.plain = modes[m].weight;
                for (p32 b = 0; b < 256; b += b < 248 ? 7 : 1)
                        for (p32 f = 0; f < 256; f += f < 240 ? 11 : 1)
                                for (p32 a = 0; a < 256; a++)
                                {
                                        p32 ground = b | (b ^ 0x5a) << 8 | (b * 7 & 255) << 16 | 0xab000000u;
                                        p32 ink = f | (f * 3 & 255) << 8 | (f ^ 0xa5) << 16;

                                        same = same && font_mix(&engine, ground, ink, a) ==
                                                           light_mix(ground, ink, a, modes[m].linear, modes[m].weight);
                                }
        }
        engine.linear = true;
        engine.plain = 0;
        check("blend: every coverage level of a spread of grounds and inks is the formula over the definition", same);

        p8 ends = true;
        for (p32 s = 0; s < 256; s += 17)
                ends = ends && font_mix(&engine, s * 0x010101, 0x204060, 0) == s * 0x010101 &&
                       font_mix(&engine, s * 0x010101, 0x204060, 255) == 0x204060;
        check("blend: no coverage is the ground, full is the ink", ends);
        font_ramp(&engine, 0, 0xffffff, engine.contrast);
        check("blend: half of black over white is half the light", font_mix(&engine, 0xffffff, 0, 127) == 0xbcbcbc);
        font_ramp(&engine, 0xffffff, 0, engine.contrast);
        //      Light text is halfway between half the light (bc) and the bytes' mix (80).
        check("blend: half of white over black is halfway between half the light and the bytes' mix",
              font_mix(&engine, 0, 0xffffff, 128) == 0x9e9e9e);

        const p32 address_to tones = font_tones(&engine, 0xdfe7ef, 0x101820, engine.contrast);
        check("tones: the ends of a pair are its two colours", tones[0] == 0x101820 && tones[255] == 0xdfe7ef);

        //      Which way the text is: the ink against the ground.
        check("polarity: black on white is darkened, and not with FONT_UNDARKENED",
              (font_polarity(0, 0, 0xffffff) & FONT_DARKEN) && font_polarity(FONT_UNDARKENED | FONT_DARKEN, 0, 0xffffff) == 0);
        check("polarity: white on black, grey on white and black on dark grey are not darkened",
              !(font_polarity(0, 0xffffff, 0) & FONT_DARKEN) && !(font_polarity(0, 0x909090, 0xffffff) & FONT_DARKEN) &&
                  !(font_polarity(0, 0, 0x404040) & FONT_DARKEN));
        check("polarity: on a ground not known, dark text is darkened and light text is not",
              (font_polarity(0, 0x202020, FONT_UNKNOWN) & FONT_DARKEN) && !(font_polarity(0, 0xe0e0e0, FONT_UNKNOWN) & FONT_DARKEN));

        p32 c = engine.contrast;
        p8 rises = true;
        for (p32 px = 13; px < engine.darken_until; px++)
                rises = rises && font_strength(&engine, px * 64, FONT_DARKEN) >= font_strength(&engine, (px - 1) * 64, FONT_DARKEN);
        check("strength: darkened, a quarter at 12 px and under, rising to all of it where darkening ends",
              font_strength(&engine, 8 * 64, FONT_DARKEN) == c / 4 && font_strength(&engine, 12 * 64, FONT_DARKEN) == c / 4 &&
                  font_strength(&engine, engine.darken_until * 64, FONT_DARKEN) == c && rises);
        check("strength: not darkened, all of it at every size",
              font_strength(&engine, 8 * 64, 0) == c && font_strength(&engine, 20 * 64, FONT_UNHINTED) == c);

        font_ramp(&engine, 0x4ec94e, 0, c);
        check("ramp: green on black is half the plain mix and no curve", engine.plain == 32 && engine.ramp[128] == 128);
        font_ramp(&engine, 0, 0xffffff, c);
        check("ramp: black on white is the whole curve and no plain mix", engine.plain == 0 && engine.ramp[128] == 192);
        font_ramp(&engine, 0x606060, 0xffffff, c);
        check("ramp: 606060 on white is about half the curve", engine.plain == 0 && engine.ramp[128] > 160 && engine.ramp[128] < 180);
}

/*
        The drawn cells. Each box character's edges against the plain line of
        each weight: an arm that reaches an edge leaves exactly the plain
        line's profile there, and an edge with no arm is empty -- at four cell
        sizes, so that cells join at any of them. Dashed lines leave their
        edges empty by design; the diagonals are checked for their mirror.
*/
static p8 drawn[128 * 128], plain[6][128 * 128];

static fn draw_cell(p32 code, b32 width, b32 height, p8 address_to into)
{
        memory_zero(into, 128 * 128);
        font_drawn(&engine, code, width, height, into, 128);
}

static p8 edge_same(const p8 address_to a, const p8 address_to b, b32 width, b32 height, p32 side)
{
        for (b32 i = 0; i < (side < 2 ? width : height); i++)
        {
                positive at = side == 0 ? (positive)i : side == 1 ? (positive)(height - 1) * 128 + i
                              : side == 2 ? (positive)i * 128 : (positive)i * 128 + width - 1;

                if (a[at] != (b ? b[at] : 0))
                        return false;
        }
        return true;
}

static fn check_drawn()
{
        static const b32 sizes[4][2] = {{8, 16}, {10, 21}, {17, 37}, {32, 70}};
        static const p32 lines[6] = {0x2502, 0x2503, 0x2551, 0x2500, 0x2501, 0x2550};

        for (p32 s = 0; s < 4; s++)
        {
                b32 width = sizes[s][0], height = sizes[s][1];
                p8 seams = true;

                for (p32 k = 0; k < 6; k++)
                        draw_cell(lines[k], width, height, plain[k]);
                for (p32 code = 0x2500; code <= 0x257f; code++)
                {
                        p32 arms = font_box_arms[code - 0x2500];

                        if ((code >= 0x2504 && code <= 0x250b) || (code >= 0x254c && code <= 0x254f) ||
                            (code >= 0x2571 && code <= 0x2573))
                                continue;
                        if (code >= 0x256d && code <= 0x2570)
                                arms = code == 0x256d ? 0x11 : code == 0x256e ? 0x14 : code == 0x256f ? 0x44 : 0x41;
                        draw_cell(code, width, height, drawn);
                        //      sides: 0 top (north), 1 bottom (south), 2 left (west), 3 right (east)
                        for (p32 side = 0; side < 4; side++)
                        {
                                p32 style = arms >> (6 - 2 * side) & 3;
                                const p8 address_to want = style ? plain[(side < 2 ? 0 : 3) + style - 1] : null;

                                seams = seams && edge_same(drawn, want, width, height, side);
                        }
                }
                check("box drawing: every arm meets its edge as the plain line does, at every cell size", seams);

                draw_cell(0x2571, width, height, drawn);
                draw_cell(0x2572, width, height, plain[0]);
                p8 mirror = true;
                for (b32 r = 0; r < height; r++)
                        for (b32 c = 0; c < width; c++)
                        {
                                p32 a = drawn[r * 128 + c], b = plain[0][r * 128 + width - 1 - c];
                                mirror = mirror && (a > b ? a - b : b - a) <= 1;
                        }
                check("box drawing: the two diagonals are mirrors", mirror);

                //      Blocks: halves complementary, shades even, quadrants placed.
                draw_cell(0x2580, width, height, drawn);
                draw_cell(0x2584, width, height, plain[0]);
                p8 halves = true;
                for (b32 r = 0; r < height; r++)
                        for (b32 c = 0; c < width; c++)
                                halves = halves && drawn[r * 128 + c] + plain[0][r * 128 + c] == 255;
                check("blocks: the upper and lower halves tile the cell", halves);
                draw_cell(0x2592, width, height, drawn);
                check("blocks: a shade is even", drawn[0] == 128 && drawn[(height - 1) * 128 + width - 1] == 128);
                draw_cell(0x259a, width, height, drawn);
                check("blocks: a quadrant pair is where it says",
                      drawn[0] == 255 && drawn[(height - 1) * 128 + width - 1] == 255 &&
                          drawn[width - 1] == 0 && drawn[(height - 1) * 128] == 0);

                //      Braille: each dot's centre is lit exactly when its bit is set.
                p8 dots = true;
                for (p32 pattern = 0; pattern < 256; pattern += 37)
                {
                        draw_cell(0x2800 + pattern, width, height, drawn);
                        for (p32 dot = 0; dot < 8; dot++)
                        {
                                p32 column = dot >= 3 && dot != 6, row = dot >= 6 ? 3 : dot % 3;
                                b32 x = (b32)(width * (1 + 2 * column) / 4), y = (b32)(height * (1 + 2 * row) / 8);

                                dots = dots && (drawn[y * 128 + x] > 64) == !!(pattern & 1u << dot);
                        }
                }
                check("braille: each dot lit exactly when its bit is set", dots);
        }
}

/*
        The shapes the terminal draws with no face: each one lights something at
        every size, and a hollow one less than its filled twin; the up and right
        triangles are the down and left ones turned over and mirrored; a full
        block covers its cell edge to edge; the cross and the arrow light their
        strokes. Red on the parent, where none of these is drawn.
*/
static positive cell_ink(const p8 address_to bits, b32 width, b32 height)
{
        positive total = 0;

        for (b32 r = 0; r < height; r++)
                for (b32 c = 0; c < width; c++)
                        total += bits[r * 128 + c];
        return total;
}

static fn check_shapes(void)
{
        static const b32 sizes[4][2] = {{8, 16}, {10, 21}, {17, 37}, {32, 70}};
        static const p32 pairs[][2] = {{0x25a0, 0x25a1}, {0x25aa, 0x25ab}, {0x25b2, 0x25b3}, {0x25b4, 0x25b5},
                                       {0x25b6, 0x25b7}, {0x25b8, 0x25b9}, {0x25bc, 0x25bd}, {0x25be, 0x25bf},
                                       {0x25c0, 0x25c1}, {0x25c2, 0x25c3}, {0x25c6, 0x25c7}, {0x25cf, 0x25cb}};
        static const p32 alone[] = {0x25e6, 0x25ac, 0x25ca, 0x25c4, 0x25ba, 0x2717, 0x21b5};
        p8 lit = true, lighter = true, mirrors = true, edges = true;

        for (p32 s = 0; s < 4; s++)
        {
                b32 width = sizes[s][0], height = sizes[s][1];

                for (p32 k = 0; k < sizeof(pairs) / sizeof(pairs[0]); k++)
                {
                        draw_cell(pairs[k][0], width, height, plain[0]);
                        positive filled = cell_ink(plain[0], width, height);
                        draw_cell(pairs[k][1], width, height, drawn);
                        positive hole = cell_ink(drawn, width, height);

                        lit = lit && filled > 0 && hole > 0;
                        lighter = lighter && hole < filled;
                        //      A hollow one lies inside its filled twin, pixel by pixel (a rounding pixel either way).
                        for (b32 r = 0; r < height; r++)
                                for (b32 c = 0; c < width; c++)
                                        lighter = lighter && drawn[r * 128 + c] <= plain[0][r * 128 + c] + 1;
                }
                for (p32 k = 0; k < sizeof(alone) / sizeof(alone[0]); k++)
                {
                        draw_cell(alone[k], width, height, drawn);
                        lit = lit && cell_ink(drawn, width, height) > 0;
                }

                draw_cell(0x25b2, width, height, drawn);
                draw_cell(0x25bc, width, height, plain[0]);
                draw_cell(0x25b6, width, height, plain[1]);
                draw_cell(0x25c0, width, height, plain[2]);
                for (b32 r = 0; r < height; r++)
                        for (b32 c = 0; c < width; c++)
                        {
                                p32 up = drawn[r * 128 + c], down = plain[0][(height - 1 - r) * 128 + c];
                                p32 right = plain[1][r * 128 + c], left = plain[2][r * 128 + width - 1 - c];

                                mirrors = mirrors && (up > down ? up - down : down - up) <= 2 &&
                                          (right > left ? right - left : left - right) <= 2;
                        }

                draw_cell(0x2588, width, height, drawn);
                for (b32 i = 0; i < width; i++)
                        edges = edges && drawn[i] == 255 && drawn[(height - 1) * 128 + i] == 255;
                for (b32 i = 0; i < height; i++)
                        edges = edges && drawn[i * 128] == 255 && drawn[i * 128 + width - 1] == 255;
        }
        check("shapes: every filled and hollow shape lights its cell, and a hollow one lights less than its filled twin",
              lit && lighter);
        check("shapes: the down triangle is the up one turned over, and the left the right one mirrored", mirrors);
        check("blocks: a full block covers its cell edge to edge at every size", edges);
}

/*
        Drawing into a target: what a known background draws is what an
        unknown one blends to over the same ground, a cell writes all of its
        rectangle and nothing else, clipping at every edge writes nothing
        outside, and a tiny atlas that empties itself, or must lend itself
        whole to one glyph, draws what a roomy one draws.
*/
static p32 canvas[96 * 96], canvas_again[96 * 96];

/*
        Stem snapping for the terminal's cells, on outlines laid straight into
        the engine in 1024ths of a pixel: a face of 1024 units at 16 px, so a
        unit of the standard stem is 1/64 px, 1229 a stem of 1.2 px.
*/
static fn snap_load(const b32 address_to xy, p32 count, p8 turned)
{
        engine.point_count = count;
        engine.contour_count = 1;
        engine.contour[0].last = (p16)(count - 1);
        for (p32 i = 0; i < count; i++)
        {
                p32 k = turned ? count - 1 - i : i;
                engine.points[i] = (font_point){xy[2 * k], xy[2 * k + 1], 1};
        }
}

static b32 snap_standard(b32 pixels1024)
{
        return (b32)((b64)pixels1024 * (1024 << FONT_EXACT) / (16 * 1024));
}

static fn check_snap()
{
        font_face face = {.units = 1024, .x_height = 500};
        p32 size = 16 * 64;
        static p8 bits[16 * 16];

        engine.snap_widest = engine.snap_widest ? engine.snap_widest : 1024;

        //      A stem 1.2 px wide at 1.3 px: a standard of three quarters of
        //      a pixel or more is whole pixels, so it becomes the pixel nearest
        //      its centre, one column of 255 between two of 0, wound either way.
        for (p32 turned = 0; turned < 2; turned++)
        {
                const b32 stem[] = {1331, 0, 1331, 10240, 2560, 10240, 2560, 0};

                snap_load(stem, 4, turned);
                font_snap(&engine, &face, size, snap_standard(1229), 0, 0, null, null, 0);
                p8 snapped = true;
                for (p32 i = 0; i < 4; i++)
                        snapped = snapped && engine.points[i].x == (stem[2 * (turned ? 3 - i : i)] == 1331 ? 1024 : 2048);
                memory_zero(bits, sizeof(bits));
                font_raster(&engine, 3, 10, bits, 3);
                for (p32 r = 0; r < 10; r++)
                        snapped = snapped && bits[3 * r] == 0 && bits[3 * r + 1] == 255 && bits[3 * r + 2] == 0;
                if (turned)
                        check("snap: a stem wound the other way snaps the same", snapped);
                else
                        check("snap: a 1.2 px stem is one column of 255", snapped);
        }

        //      1.45 px, which kept its width when whole widths were only for a
        //      standard within a fifth of one, is whole too: the pixel nearest
        //      its centre (2.03 px), not a grey column beside a black one.
        {
                const b32 stem[] = {1331, 0, 1331, 10240, 2816, 10240, 2816, 0};

                snap_load(stem, 4, false);
                font_snap(&engine, &face, size, snap_standard(1485), 0, 0, null, null, 0);
                check("snap: a 1.45 px stem is the whole pixel nearest its centre",
                      engine.points[0].x == 2048 && engine.points[2].x == 3072);
        }

        //      A bracket: the stem snaps, the ends of its arms stay where the
        //      font put them, so they do not reach into the next cell.
        {
                const b32 bracket[] = {1331, 0, 1331, 10240, 4000, 10240, 4000, 9216,
                                       2560, 9216, 2560, 1024, 4000, 1024, 4000, 0};

                snap_load(bracket, 8, false);
                font_snap(&engine, &face, size, snap_standard(1229), 0, 0, null, null, 0);
                check("snap: a bracket's stem snaps and its arms' ends stay",
                      engine.points[0].x == 1024 && engine.points[4].x == 2048 && engine.points[2].x == 4000 &&
                          engine.points[7].x == 4000);
        }

        //      Two stems half a pixel apart that would snap shoulder to shoulder:
        //      the one that moved most is let go, and the counter stays open.
        {
                const b32 two[] = {1444, 0, 1444, 10240, 2673, 10240, 2673, 0,
                                   3185, 0, 3185, 10240, 4414, 10240, 4414, 0};

                engine.point_count = 8;
                engine.contour_count = 2;
                engine.contour[0].last = 3;
                engine.contour[1].last = 7;
                for (p32 i = 0; i < 8; i++)
                        engine.points[i] = (font_point){two[2 * i], two[2 * i + 1], 1};
                font_snap(&engine, &face, size, snap_standard(1229), 0, 0, null, null, 0);
                check("snap: a counter half a pixel wide is not shut",
                      engine.points[4].x == 3072 && engine.points[6].x == 4096 && engine.points[2].x < 3072 &&
                          engine.points[0].x < engine.points[2].x);
        }

        //      Under a pixel: 0.8 px becomes a whole one, 0.6 px keeps its
        //      width and puts its nearer edge on the grid.
        {
                const b32 stem[] = {1331, 0, 1331, 10240, 2150, 10240, 2150, 0};

                snap_load(stem, 4, false);
                font_snap(&engine, &face, size, snap_standard(819), 0, 0, null, null, 0);
                check("snap: a 0.8 px stem is a whole pixel", engine.points[0].x == 1024 && engine.points[2].x == 2048);
        }
        {
                const b32 stem[] = {1331, 0, 1331, 10240, 1945, 10240, 1945, 0};

                snap_load(stem, 4, false);
                font_snap(&engine, &face, size, snap_standard(614), 0, 0, null, null, 0);
                check("snap: a 0.6 px stem keeps its width with an edge on a pixel",
                      engine.points[0].x == 1434 && engine.points[2].x == 2048);
        }
}

/*
        A run of cells is the same pixels as its cells drawn one at a time:
        glyphs, blanks, drawn box and block cells and a code no face has,
        on a known ground, from a fresh engine and again from a warm one;
        a run that reaches past the target's edge, which is drawn a cell at
        a time; and in an engine whose atlas holds a few cells, so that the
        run's own glyphs empty it while the run is being found.
*/
static p32 cells_one[64 * 400], cells_two[64 * 400];

static fn check_cells(font_engine address_to with, const font_face address_to two)
{
        const font_face address_to chain[1] = {two};
        static const p32 codes[] = {'A', ' ', 'B', 0x2500, 0x253c, 0x2588, 'C', 0x1f600, 'D', 'A', ' ', 0x2593, 'B'};
        p32 n = sizeof(codes) / sizeof(codes[0]);
        font_metrics grid = font_metrics_at(two, 18 * 64);

        for (p32 pass = 0; pass < 3; pass++)
        {
                //      Pass 2 starts the run so that its last cells hang past
                //      the right edge.
                b32 x = pass == 2 ? 400 - (b32)(n - 2) * grid.cell_width : 3;
                font_target one = {cells_one, 400, 400, 64}, other = {cells_two, 400, 400, 64};

                memory_fill_u32(cells_one, sizeof(cells_one) / 4, 0xabcdef);
                memory_fill_u32(cells_two, sizeof(cells_two) / 4, 0xabcdef);
                font_cells(with, chain, 1, codes, n, &grid, 18 * 64, 0, &one, x, 5, 0xdfe7ef, 0x303030);
                for (p32 i = 0; i < n; i++)
                        font_cell(with, chain, 1, codes[i], 1, &grid, 18 * 64, 0, &other, x + (b32)i * grid.cell_width,
                                  5, 0xdfe7ef, 0x303030);
                p8 same = !memory_compare(cells_one, cells_two, sizeof(cells_one));
                check("cells: a run draws what its cells draw one at a time", same);
        }
}

static p8 chain_atlas[512 * 512];
static font_engine chain_engine, chain_fresh;
static p32 chain_one[64 * 400], chain_two[64 * 400];

/*
        A cell is found in the chain of faces it is drawn with, and the cache
        of drawn cells is keyed on the whole chain and on how many cells the
        code spans. A code only the second face has is drawn from that face
        in [one, two]; in [one, one], the same first face and the same length,
        it must draw nothing, as it does in an engine of its own. And two
        cells of a code are not three cells of it.
*/
static fn check_chains(const font_face address_to one, const font_face address_to two)
{
        const font_face address_to both[2] = {one, two}, address_to alone[2] = {one, one};
        font_metrics grid = font_metrics_at(one, 18 * 64);
        font_target first = {chain_one, 400, 400, 64}, second = {chain_two, 400, 400, 64};
        p32 code = 0;

        for (p32 c = 0x20; c < 0x3000 && !code; c++)
                if (!font_glyph(one, c) && font_glyph(two, c))
                        code = c;
        check("chains: a code only the second face has", code != 0);
        if (!code)
                return;

        font_start(&chain_engine, chain_atlas, 512, 512);
        memory_fill_u32(chain_one, sizeof(chain_one) / 4, 0xabcdef);
        font_cell(&chain_engine, both, 2, code, 1, &grid, 18 * 64, 0, &first, 3, 5, 0xdfe7ef, 0x303030);
        memory_fill_u32(chain_one, sizeof(chain_one) / 4, 0xabcdef);
        font_cell(&chain_engine, alone, 2, code, 1, &grid, 18 * 64, 0, &first, 3, 5, 0xdfe7ef, 0x303030);
        font_start(&chain_fresh, chain_atlas, 512, 512);
        memory_fill_u32(chain_two, sizeof(chain_two) / 4, 0xabcdef);
        font_cell(&chain_fresh, alone, 2, code, 1, &grid, 18 * 64, 0, &second, 3, 5, 0xdfe7ef, 0x303030);
        check("chains: [one, two] then [one, one] draws what [one, one] draws in an engine of its own",
              memory_compare(chain_one, chain_two, sizeof(chain_one)) == 0);

        font_start(&chain_engine, chain_atlas, 512, 512);
        memory_fill_u32(chain_one, sizeof(chain_one) / 4, 0xabcdef);
        font_cell(&chain_engine, both, 2, code, 2, &grid, 18 * 64, 0, &first, 3, 5, 0xdfe7ef, 0x303030);
        memory_fill_u32(chain_one, sizeof(chain_one) / 4, 0xabcdef);
        font_cell(&chain_engine, both, 2, code, 3, &grid, 18 * 64, 0, &first, 3, 5, 0xdfe7ef, 0x303030);
        font_start(&chain_fresh, chain_atlas, 512, 512);
        memory_fill_u32(chain_two, sizeof(chain_two) / 4, 0xabcdef);
        font_cell(&chain_fresh, both, 2, code, 3, &grid, 18 * 64, 0, &second, 3, 5, 0xdfe7ef, 0x303030);
        check("chains: three cells after two of a code draw what three cells draw in an engine of their own",
              memory_compare(chain_one, chain_two, sizeof(chain_one)) == 0);
}

/*
        Drawn cells stay in their cell: every box, block, shape and braille
        code at small cell sizes, where a dot or a shape is only a pixel or
        two, writes nothing outside its width and height. The stride is 128,
        so a write past the cell's right edge would land in a row the check
        reads, and one past its bottom in a row it reads too.
*/
static p8 bounds_cell[128 * 128];

static fn check_drawn_bounds()
{
        static const p32 ranges[][2] = {{0x2500, 0x25ff}, {0x2800, 0x28ff}, {0x21b5, 0x21b5}, {0x2717, 0x2717}, {0xad, 0xad}};
        p8 stays = true;

        for (p32 r = 0; r < sizeof(ranges) / sizeof(ranges[0]) && stays; r++)
                for (b32 width = 1; width <= 5 && stays; width++)
                        for (b32 height = 1; height <= 5 && stays; height++)
                                for (p32 code = ranges[r][0]; code <= ranges[r][1] && stays; code++)
                                {
                                        memory_zero(bounds_cell, sizeof(bounds_cell));
                                        font_drawn(&engine, code, width, height, bounds_cell, 128);
                                        for (b32 y = 0; y < 128 && stays; y++)
                                                for (b32 x = 0; x < 128 && stays; x++)
                                                        if ((x >= width || y >= height) && bounds_cell[y * 128 + x])
                                                                stays = false;
                                }
        check("drawn: no cell writes outside its width and height, 1 to 5 px, every box, block, shape and braille code", stays);
}

/*
        Horizontal bars and overshoots: bars snapped as the stems are, with
        the axes swapped, keeping the fitted lines; a round letter's reach
        past the x-height and under the baseline flattened onto its line
        while it is under half a pixel, a whole pixel when it is not.
*/
static fn check_bars()
{
        font_face face = {.units = 1024, .x_height = 500, .cap_height = 700};
        p32 size = 16 * 64;
        static const b64 lines[2] = {0, 7168};

        engine.snap_widest = engine.snap_widest ? engine.snap_widest : 1024;
        {
                const b32 bar[] = {0, 3379, 0, 4608, 10240, 4608, 10240, 3379};

                snap_load(bar, 4, false);
                font_bars(&engine, &face, size, snap_standard(1229), lines, 2, 0, 0);
                check("bars: a 1.2 px bar at 3.3 px is the row from 3 to 4",
                      engine.points[0].y == 3072 && engine.points[1].y == 4096 && engine.points[2].y == 4096 &&
                          engine.points[3].y == 3072);
        }
        {
                const b32 bar[] = {0, 5939, 0, 7168, 10240, 7168, 10240, 5939};

                snap_load(bar, 4, false);
                font_bars(&engine, &face, size, snap_standard(1229), lines, 2, 0, 0);
                check("bars: a bar on the x-height keeps it and takes a whole pixel below",
                      engine.points[1].y == 7168 && engine.points[0].y == 6144);
        }

        //      x-height 500 of 1024 at 16 px is 7.81 px, fitted to 8; o reaches
        //      15 units over it (0.23 px) and 15 under the baseline: both are
        //      flattened. At 48 px the 0.70 px reach is a whole pixel.
        static const b32 zones[6] = {15, 15, 0, 0, 0, 0};
        b64 fitted[3];
        const b32 round16[] = {0, 8240, 0, -240, 100, 0, 100, 8000};
        snap_load(round16, 4, false);
        font_fit(&engine, &face, 16 * 64, zones, 0, fitted);
        check("fit: an overshoot under half a pixel lies on the x-height and the baseline",
              engine.points[0].y == 8192 && engine.points[1].y == 0 && engine.points[3].y == 8192 && fitted[1] == 8192);

        const b32 round48[] = {0, 24720, 0, 24000};
        snap_load(round48, 2, false);
        font_fit(&engine, &face, 48 * 64, zones, 0, fitted);
        check("fit: an overshoot over half a pixel is a whole pixel over the x-height",
              engine.points[0].y == fitted[1] + 1024 && engine.points[1].y == fitted[1]);
}

/*
        Synthetic italic and bold. Glyph 0 of the first font is a bar from
        y 0 to 700 units, 300 wide: slanted, its top moves right of its
        bottom by its height times tan 12 degrees, about half the x-height
        (500 units) staying where it was; made heavy, it has more ink. A
        cell drawn upright, slanted and upright again is the same pixels the
        first and third time: the cache keeps the two apart.
*/
static p32 synthetic_one[64 * 400], synthetic_two[64 * 400], synthetic_three[64 * 400];

static fn check_synthetic(const font_face address_to one, const font_face address_to two)
{
        font_bitmap box;
        p32 size = 16 * 64;

        font_shape(&engine, one, 0, size, 0, 8, &box);
        b64 bottom_x = (b64)1 << 62, top_x = -bottom_x, low = bottom_x, high = -bottom_x;
        for (p32 i = 0; i < engine.point_count; i++)
        {
                low = engine.points[i].y < low ? engine.points[i].y : low;
                high = engine.points[i].y > high ? engine.points[i].y : high;
        }
        for (p32 i = 0; i < engine.point_count; i++)
        {
                if (engine.points[i].y == low)
                        bottom_x = engine.points[i].x < bottom_x ? engine.points[i].x : bottom_x;
                if (engine.points[i].y == high)
                        top_x = engine.points[i].x > top_x ? engine.points[i].x : top_x;
        }
        f64 want = (f64)(high - low) * 0.21255656167002213 + 300.0 * 16384 / 1024;
        check("synthetic: a slant of 12 degrees moves the top right by the height times tan 12",
              (f64)(top_x - bottom_x) > want - 3 && (f64)(top_x - bottom_x) < want + 3);

        positive plain = 0, heavy = 0;
        render(one, 0, size, 0, FONT_UNHINTED, &box);
        for (positive i = 0; i < sizeof(coverage); i++)
                plain += coverage[i];
        render(one, 0, size, 0, FONT_UNHINTED | FONT_EMBOLDEN, &box);
        for (positive i = 0; i < sizeof(coverage); i++)
                heavy += coverage[i];
        check("synthetic: a bold made of a plain face has more ink", heavy > plain + plain / 10);

        const font_face address_to chain[1] = {two};
        font_metrics grid = font_metrics_at(two, size);
        font_target first = {synthetic_one, 400, 400, 64}, second = {synthetic_two, 400, 400, 64},
                    third = {synthetic_three, 400, 400, 64};
        font_cell(&engine, chain, 1, 'A', 1, &grid, size, 0, &first, 4, 4, 0xdfe7ef, 0x303030);
        font_cell(&engine, chain, 1, 'A', 1, &grid, size, FONT_OBLIQUE, &second, 4, 4, 0xdfe7ef, 0x303030);
        font_cell(&engine, chain, 1, 'A', 1, &grid, size, 0, &third, 4, 4, 0xdfe7ef, 0x303030);
        check("synthetic: upright, slanted and upright again keep their own pixels",
              !memory_compare(synthetic_one, synthetic_three, sizeof(synthetic_one)) &&
                  memory_compare(synthetic_one, synthetic_two, sizeof(synthetic_one)));
}

/*
        What a fit must hold, over every printable ASCII glyph of the faces
        this tree ships at 8 to 32 px, as dark cells (fitted, snapped and
        darkened), light cells (fitted and snapped) and drawn text (fitted and
        snapped at a phase of three eighths):
          flat     an outline segment level and a pixel or longer lies on a
                   whole pixel (within 1/64);
          counter  the hinted bitmap at half ink encloses as many regions of
                   ground as the unhinted one;
          cell     a hinted glyph stays in its cell where the unhinted one
                   does.
        Each is a count that may not grow: the ceilings are the counts when
        the check was written (a32972a0), so a change to the fit that makes
        any face worse on any of them is red, and one that makes them better
        lowers them here. Before that commit, the same faces had 1,976 flat
        edges off the grid as dark cells, where 570 are now.
*/
static font_engine invariant_engine;
static p8 invariant_atlas[1 << 20];
static p8 invariant_bitmap[256 * 256];
static p8 invariant_seen[260 * 260];
static b32 invariant_queue[260 * 260];

//      Regions of ground the ink at half or more encloses, four-connected, the bitmap's border counting as outside.
static b32 invariant_holes(b32 w, b32 h)
{
        b32 W = w + 2, H = h + 2, count = 0;

        for (b32 y = 0; y < H; y++)
                for (b32 x = 0; x < W; x++)
                        invariant_seen[y * W + x] =
                            x > 0 && y > 0 && x <= w && y <= h && invariant_bitmap[(y - 1) * w + (x - 1)] >= 128 ? 2 : 0;
        for (b32 pass = 0; pass < 2; pass++)
                for (b32 s = 0; s < W * H; s++)
                {
                        b32 sx = s % W, sy = s / W, head = 0, tail = 0;

                        if (invariant_seen[s] || (!pass && sx && sy && sx < W - 1 && sy < H - 1))
                                continue;
                        count += pass;
                        invariant_queue[tail++] = s;
                        invariant_seen[s] = 1;
                        while (head < tail)
                        {
                                b32 at = invariant_queue[head++], x = at % W, y = at / W;
                                b32 next[4] = {x > 0 ? at - 1 : -1, x < W - 1 ? at + 1 : -1, y > 0 ? at - W : -1,
                                               y < H - 1 ? at + W : -1};

                                for (b32 k = 0; k < 4; k++)
                                        if (next[k] >= 0 && !invariant_seen[next[k]])
                                                invariant_seen[next[k]] = 1, invariant_queue[tail++] = next[k];
                        }
                }
        return count;
}

static p8 invariant_shape(const font_face address_to face, p32 glyph, p32 size, p32 phase, p32 style,
                            font_bitmap address_to box)
{
        if (font_shape(&invariant_engine, face, glyph, size, phase, style, box) < 0 || box->width <= 0 ||
            box->height <= 0 || box->width > 256 || box->width * box->height > 256 * 256)
                return false;
        memory_zero(invariant_bitmap, (positive)box->width * (positive)box->height);
        font_raster(&invariant_engine, box->width, box->height, invariant_bitmap, (positive)box->width);
        return true;
}

static fn check_invariants(void)
{
        static const string_address faces[4] = {"fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf",
                                                 "fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf",
                                                 "fonts/inter/Inter-Regular.ttf", "fonts/inter/Inter-Bold.ttf"};
        //      Flat edges off the grid, counters lost, glyphs out of their cell: a face's dark cells, light
        //      cells and drawn text.
        static const p32 ceiling[4][3][3] = {
            {{84, 24, 0}, {47, 21, 0}, {44, 24, 0}},
            {{122, 29, 0}, {125, 21, 0}, {116, 23, 0}},
            {{64, 10, 0}, {61, 14, 0}, {61, 13, 0}},
            {{74, 26, 0}, {55, 16, 0}, {57, 17, 0}},
        };
        static const string_address paths[3] = {"dark cells", "light cells", "drawn text"};
        static p8 file[1 << 20];

        font_start(&invariant_engine, invariant_atlas, 1024, 1024);
        for (p32 f = 0; f < 4; f++)
        {
                bipolar got = file_slurp_regular_at(AT_FDCWD, faces[f], file, sizeof(file), 0);
                font_face face;

                check("invariants: the face this tree ships loads", got > 0 && font_load(&face, file, (positive)got, 0) == 0);
                if (got <= 0)
                        continue;
                for (p32 path = 0; path < 3; path++)
                {
                        p32 style = path ? 5 : 7, found[3] = {0, 0, 0};

                        for (p32 px = 8; px <= 32; px++)
                        {
                                p32 size = px * 64;
                                font_metrics grid = font_metrics_at(&face, size);

                                for (p32 code = 0x21; code < 0x7f; code++)
                                {
                                        p32 glyph = font_glyph(&face, code);
                                        b32 eighth = path == 2 ? 3
                                                               : font_eighths(((grid.cell_width << 16) -
                                                                               font_advance(&face, glyph, size)) / 2);
                                        font_bitmap box, plain;

                                        if (!glyph || !invariant_shape(&face, glyph, size, (p32)eighth & 7, style, &box))
                                                continue;
                                        p32 first = 0;
                                        for (p32 c = 0; c < invariant_engine.contour_count;
                                             first = invariant_engine.contour[c++].last + 1u)
                                                for (p32 i = first; i <= invariant_engine.contour[c].last; i++)
                                                {
                                                        font_point a = invariant_engine.points[i];
                                                        font_point b = invariant_engine.points[i == invariant_engine.contour[c].last ? first : i + 1];

                                                        found[0] += a.y == b.y && (b.x - a.x >= FONT_ONE || a.x - b.x >= FONT_ONE) &&
                                                                    !font_on_grid(a.y);
                                                }
                                        b32 hinted = invariant_holes(box.width, box.height);
                                        b32 x0 = (eighth >> 3) + box.left, y0 = grid.baseline - box.top;
                                        p8 out = x0 < 0 || y0 < 0 || x0 + box.width > grid.cell_width ||
                                                   y0 + box.height > grid.cell_height;

                                        if (!invariant_shape(&face, glyph, size, (p32)eighth & 7, path ? 0 : 2, &plain))
                                                continue;
                                        b32 u0 = (eighth >> 3) + plain.left, v0 = grid.baseline - plain.top;
                                        found[1] += hinted < invariant_holes(plain.width, plain.height);
                                        found[2] += path < 2 && out &&
                                                    !(u0 < 0 || v0 < 0 || u0 + plain.width > grid.cell_width ||
                                                      v0 + plain.height > grid.cell_height);
                                }
                        }
                        string_format(log, "  %s, %s: %p flat edges off the grid, %p counters lost, %p out of their cell\n",
                                      faces[f], paths[path], (positive)found[0], (positive)found[1], (positive)found[2]);
                        check("invariants: flat edges on the grid, no worse than the ceiling", found[0] <= ceiling[f][path][0]);
                        check("invariants: counters kept, no worse than the ceiling", found[1] <= ceiling[f][path][1]);
                        check("invariants: glyphs in their cells, no worse than the ceiling", found[2] <= ceiling[f][path][2]);
                }
        }
        log_flush();
}

/*
        Dots, over the faces this tree ships at 8 to 32 px, as dark drawn
        text and light drawn text at every eighth and as light and dark cells
        (each at the eighth that centres it), from the bitmap the engine
        caches:
          i and j    the dot -- the band of ink rows above the first empty
                     row -- is over the middle of the stem, the ink of the
                     rows a third to two thirds down the x-height, within a
                     quarter of a pixel, and stands clear of it (the band
                     under half the glyph's height). Rounded on its own, a dot
                     of 1.6 px over a stem of one was two pixels, its extra
                     column on the side the pen's eighth rounded it to;
          umlauts    the two dots of a diaeresis are two runs of columns at
                     half ink or more, not one: rounded each about its own
                     middle, they met in a bar;
          the zero   Plex Mono's dot in its zero is whole pixels, in the middle
                     of the counter across with at least a pixel of air on
                     every side, or not drawn, and 0 is no more than a pixel
                     wider than O, whose outline it shares (the pixel a dot
                     may need, at 8 and 10 px bold), and in its cell: the dot
                     was snapped as a stem, a bar across the counter at 10 px,
                     and the zero two pixels wider than O there and at 18 and
                     25 px, the fat oval it was taken for.
*/
static font_engine dots_engine;
static p8 dots_bitmap[256 * 256];
static p8 dots_file[1 << 20];

//      The glyph for code drawn into dots_bitmap; false when it is empty or too large to look at.
static p8 dots_draw(const font_face address_to face, p32 code, p32 size, p32 phase, p32 style, font_bitmap address_to box)
{
        if (font_shape(&dots_engine, face, font_glyph(face, code), size, phase, style, box) < 0 || box->width <= 0 ||
            box->height <= 0 || box->width * box->height > 256 * 256)
                return false;
        memory_zero(dots_bitmap, (positive)box->width * (positive)box->height);
        font_raster(&dots_engine, box->width, box->height, dots_bitmap, (positive)box->width);
        return true;
}

//      The rows of ink at the top, down to the first empty row.
static b32 dots_band(const font_bitmap address_to box)
{
        b32 band = 0;

        for (p8 any = true; band < box->height && any; band++)
        {
                any = false;
                for (b32 c = 0; c < box->width; c++)
                        any = any || dots_bitmap[band * box->width + c];
        }
        return band;
}

//      Twice the ink-weighted middle, in pixels times 256, of rows from to to; -1 with no ink.
static b64 dots_middle(const font_bitmap address_to box, b32 from, b32 to)
{
        b64 mass = 0, moment = 0;

        for (b32 r = from < 0 ? 0 : from; r < to && r < box->height; r++)
                for (b32 c = 0; c < box->width; c++)
                        mass += dots_bitmap[r * box->width + c], moment += dots_bitmap[r * box->width + c] * (2 * c + 1);
        return mass ? moment * 256 / mass : -1;
}

//      The box of contour c of the last glyph shaped.
static fn dots_contour(p32 c, b64 address_to box)
{
        p32 first = c ? dots_engine.contour[c - 1].last + 1u : 0;

        box[0] = box[1] = (b64)1 << 62, box[2] = box[3] = -((b64)1 << 62);
        for (p32 i = first; i <= dots_engine.contour[c].last; i++)
        {
                box[0] = dots_engine.points[i].x < box[0] ? dots_engine.points[i].x : box[0];
                box[1] = dots_engine.points[i].y < box[1] ? dots_engine.points[i].y : box[1];
                box[2] = dots_engine.points[i].x > box[2] ? dots_engine.points[i].x : box[2];
                box[3] = dots_engine.points[i].y > box[3] ? dots_engine.points[i].y : box[3];
        }
}

//      Whether the dot of the zero just shaped sits in its counter as the engine sets it: the smallest contour, whole
//      pixels, a pixel or more of air across either side alike and up and down; or drawn as nothing.
static p8 dots_zero_dot(void)
{
        p32 dot = 0, counter = dots_engine.contour_count;
        b64 box[4], best = (b64)1 << 62, room[4];

        for (p32 c = 0; c < dots_engine.contour_count; c++)
        {
                dots_contour(c, box);
                if ((box[2] - box[0]) * (box[3] - box[1]) < best)
                        best = (box[2] - box[0]) * (box[3] - box[1]), dot = c;
        }
        dots_contour(dot, box);
        if (box[2] == box[0] || box[3] == box[1])
                return true;
        best = (b64)1 << 62;
        for (p32 c = 0; c < dots_engine.contour_count; c++)
        {
                dots_contour(c, room);
                if (c != dot && room[0] <= box[0] && room[2] >= box[2] && room[1] <= box[1] && room[3] >= box[3] &&
                    (room[2] - room[0]) * (room[3] - room[1]) < best)
                        best = (room[2] - room[0]) * (room[3] - room[1]), counter = c;
        }
        if (counter == dots_engine.contour_count)
                return false;
        dots_contour(counter, room);
        b64 left = (room[0] + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1), right = room[2] & ~(b64)(FONT_ONE - 1);
        b64 bottom = (room[1] + FONT_ONE - 1) & ~(b64)(FONT_ONE - 1), top = room[3] & ~(b64)(FONT_ONE - 1);
        p8 whole = true;

        for (p32 k = 0; k < 4; k++)
                whole = whole && font_on_grid(box[k]);
        return whole && box[0] - left == right - box[2] && box[0] - left >= FONT_ONE && box[1] - bottom >= FONT_ONE &&
               top - box[3] >= FONT_ONE;
}

static fn check_dots(void)
{
        static const string_address faces[4] = {"fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf",
                                                 "fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf",
                                                 "fonts/inter/Inter-Regular.ttf", "fonts/inter/Inter-Bold.ttf"};
        static const p32 umlauts[] = {0xe4, 0xf6, 0xfc, 0xeb, 0xef, 0xff, 0xc4, 0xd6, 0xdc};
        positive stems = 0, off = 0, pairs = 0, fused = 0, zeros = 0, loose = 0, wider = 0;

        font_start(&dots_engine, null, 0, 0);
        for (p32 f = 0; f < 4; f++)
        {
                bipolar got = file_slurp_regular_at(AT_FDCWD, faces[f], dots_file, sizeof(dots_file), 0);
                font_face face;

                if (got <= 0 || font_load(&face, dots_file, (positive)got, 0) != 0)
                {
                        check("dots: the face this tree ships loads", false);
                        continue;
                }
                for (p32 px = 8; px <= 32; px++)
                {
                        p32 size = px * 64;
                        font_metrics grid = font_metrics_at(&face, size);
                        b32 x_height = (grid.x_height + 0x8000) >> 16;

                        //      Eight eighths of drawn text dark and light, then a light and a dark cell.
                        for (p32 way = 0; way < 18; way++)
                        {
                                p32 style = way < 8 || way == 17 ? 7 : 5;
                                font_bitmap box;

                                for (p32 k = 0; k < 2 + sizeof(umlauts) / sizeof(umlauts[0]); k++)
                                {
                                        p32 code = k == 0 ? 'i' : k == 1 ? 'j' : umlauts[k - 2];
                                        p32 phase = way < 16 ? way & 7
                                                             : (p32)font_eighths(((grid.cell_width << 16) -
                                                                                  font_advance(&face, font_glyph(&face, code), size)) / 2) & 7;

                                        if (!dots_draw(&face, code, size, phase, style, &box))
                                                continue;
                                        b32 band = dots_band(&box);
                                        if (k < 2)
                                        {
                                                b32 below = box.top - x_height;
                                                b64 dot = dots_middle(&box, 0, band);
                                                b64 stem = dots_middle(&box, below + x_height * 3 / 10, below + x_height * 7 / 10);

                                                stems++;
                                                off += band > box.height / 2 || dot < 0 || stem < 0 || dot - stem > 128 || stem - dot > 128;
                                                continue;
                                        }

                                        b32 runs = 0;
                                        p8 in = false;
                                        for (b32 c = 0; c < box.width; c++)
                                        {
                                                p32 most = 0;

                                                for (b32 r = 0; r < band; r++)
                                                        most = dots_bitmap[r * box.width + c] > most ? dots_bitmap[r * box.width + c] : most;
                                                runs += most >= 128 && !in;
                                                in = most >= 128;
                                        }
                                        pairs++;
                                        fused += runs < 2;
                                }

                                //      Plex Mono's zero is dotted, and shares O's outline.
                                if (f < 2 && way >= 16)
                                {
                                        b32 eighth = font_eighths(((grid.cell_width << 16) -
                                                                   font_advance(&face, font_glyph(&face, '0'), size)) / 2);
                                        font_bitmap round;

                                        if (!dots_draw(&face, 'O', size, (p32)eighth & 7, style, &round) ||
                                            !dots_draw(&face, '0', size, (p32)eighth & 7, style, &box))
                                                continue;
                                        zeros++;
                                        loose += !dots_zero_dot();
                                        wider += box.width > round.width + 1 || (eighth >> 3) + box.left < 0 ||
                                                 (eighth >> 3) + box.left + box.width > grid.cell_width;
                                }
                        }
                }
        }
        if (off || fused || loose || wider)
                string_format(log, "  dots: %p of %p i and j off their stem or on it, %p of %p umlauts one blot, %p of %p "
                                   "zeros with their dot off the middle of the counter, %p wider than O by more than a pixel or out of the cell\n",
                              off, stems, fused, pairs, loose, zeros, wider);
        check("dots: the dot of i and j is over the middle of its stem and clear of it, at every eighth and in cells", stems && !off);
        check("dots: the two dots of an umlaut stay two", pairs && !fused);
        check("dots: the dot of a dotted zero is whole and in the middle of its counter, with air round it", zeros && !loose);
        check("dots: a dotted zero is no more than a pixel wider than O, and in its cell", zeros && !wider);
}

/*
        Uniform stem and bar weights, and consistent ascender heights:
          top bars    capital letters (B D E F I J P R T Z 5 7) and ascenders
                      (f l) never snap thicker than the face's rounded bar
                      width from 8 to 32 px, dark or light, across all four
                      shipped faces;
          ascenders   f and l match d's fitted top pixel at every size from
                      8 to 32 px in IBM Plex Mono Regular and Bold;
          false pairs vertical stems of r and a and horizontal bars of 4, $
                      and ~ never pair across counters or serifs into stems
                      or bars wider than the face's rounded weight.
*/
static fn check_thickness(void)
{
        static const string_address faces[4] = {"fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf",
                                                "fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf",
                                                "fonts/inter/Inter-Regular.ttf", "fonts/inter/Inter-Bold.ttf"};
        static const p8 bar_glyphs[] = "BDEFHIJPRT57fl";
        positive thick_bars = 0, tall_mismatch = 0, false_stems = 0;

        font_start(&dots_engine, null, 0, 0);
        for (p32 f = 0; f < 4; f++)
        {
                bipolar got = file_slurp_regular_at(AT_FDCWD, faces[f], dots_file, sizeof(dots_file), 0);
                font_face face;

                if (got <= 0 || font_load(&face, dots_file, (positive)got, 0) != 0)
                        continue;
                b32 standard = font_stem_standard(&dots_engine, &face);
                b32 zones[FONT_ZONES];
                font_face_measure(&dots_engine, &face, zones);

                for (p32 px = 8; px <= 32; px++)
                {
                        p32 size = px * 64;
                        b64 scale = (b64)size * (FONT_ONE / 64);
                        b64 norm_v = font_divide((b64)standard * scale, (b64)face.units << FONT_EXACT);
                        b64 norm_h = font_divide((b64)zones[3] * scale, (b64)face.units << FONT_EXACT);
                        b64 round_v = font_round(norm_v) < FONT_ONE ? FONT_ONE : font_round(norm_v);
                        b64 round_h = font_round(norm_h) < FONT_ONE ? FONT_ONE : font_round(norm_h);
                        b64 x_height = font_divide((b64)(face.x_height > 0 ? face.x_height : face.units / 2) * scale, face.units);

                        for (p32 style = 5; style <= 7; style += 2)
                        {
                                if (f < 2)
                                {
                                        font_bitmap bd, bf, bl;
                                        if (font_shape(&dots_engine, &face, font_glyph(&face, 'd'), size, 0, style, &bd) == 0 &&
                                            font_shape(&dots_engine, &face, font_glyph(&face, 'f'), size, 0, style, &bf) == 0 &&
                                            font_shape(&dots_engine, &face, font_glyph(&face, 'l'), size, 0, style, &bl) == 0)
                                                tall_mismatch += bf.top != bd.top || bl.top != bd.top;
                                }
                                for (p32 k = 0; bar_glyphs[k]; k++)
                                {
                                        font_bitmap box;
                                        if (font_shape(&dots_engine, &face, font_glyph(&face, bar_glyphs[k]), size, 0, style, &box) != 0)
                                                continue;
                                        font_swap_axes(&dots_engine);
                                        font_run run[2 * FONT_STEMS];
                                        b64 stem[2 * FONT_STEMS];
                                        p32 runs = font_runs(&dots_engine, FONT_ONE / 32, x_height / FONT_ROUND_BAR / 4, run);
                                        p32 stems = font_stems(run, runs, norm_h * 3 / 2, stem, x_height / FONT_ROUND_BAR);
                                        for (p32 s = 0; s < stems; s++)
                                        {
                                                b64 w = stem[2 * s + 1] - stem[2 * s];
                                                if (w >= round_h + FONT_ONE / 2)
                                                        thick_bars++;
                                        }
                                        font_swap_axes(&dots_engine);
                                }
                                if (f == 0)
                                {
                                        static const p8 vglyphs[] = "ra";
                                        for (p32 k = 0; vglyphs[k]; k++)
                                        {
                                                font_bitmap box;
                                                if (font_shape(&dots_engine, &face, font_glyph(&face, vglyphs[k]), size, 0, style, &box) != 0)
                                                        continue;
                                                font_run run[2 * FONT_STEMS];
                                                b64 stem[2 * FONT_STEMS];
                                                p32 runs = font_runs(&dots_engine, FONT_ONE / 32, x_height / FONT_ROUND_RUN / 4, run);
                                                p32 stems = font_stems(run, runs, norm_v * 3 / 2, stem, x_height / FONT_ROUND_RUN);
                                                for (p32 s = 0; s < stems; s++)
                                                        if (stem[2 * s + 1] - stem[2 * s] >= round_v + FONT_ONE / 2)
                                                                false_stems++;
                                        }
                                }
                        }
                }
        }
        check("thickness: capital and ascender bars stay at the face's rounded bar weight from 8 to 32 px", !thick_bars);
        check("thickness: f and l match d's fitted ascender height from 8 to 32 px", !tall_mismatch);
        check("thickness: r and a vertical stems stay at the face's rounded stem weight", !false_stems);
}

/*
        Darkening by default: a cell of black on white drawn as asked has
        more ink than with FONT_UNDARKENED, and one of white on black is the
        same pixels either way. Unhinted: hinted, the grown outline is fitted
        and its stems snapped, so on this face's straight A, every edge on a
        line or a stem, the grid decides the ink and darkening adds none.
*/
static p32 polarity_one[64 * 400], polarity_two[64 * 400];

static fn check_polarity(const font_face address_to face)
{
        const font_face address_to chain[1] = {face};
        p32 size = 10 * 64;
        font_metrics grid = font_metrics_at(face, size);
        font_target one = {polarity_one, 400, 400, 64}, two = {polarity_two, 400, 400, 64};
        positive plain = 0, darker = 0;

        font_cell(&engine, chain, 1, 'A', 1, &grid, size, FONT_UNDARKENED | FONT_UNHINTED, &one, 4, 4, 0, 0xffffff);
        font_cell(&engine, chain, 1, 'A', 1, &grid, size, FONT_UNHINTED, &two, 4, 4, 0, 0xffffff);
        for (positive i = 0; i < 64 * 400; i++)
        {
                plain += 255 - (polarity_one[i] & 255);
                darker += 255 - (polarity_two[i] & 255);
        }
        check("polarity: black on white drawn as asked has more ink than undarkened", darker > plain);

        font_cell(&engine, chain, 1, 'A', 1, &grid, size, FONT_UNDARKENED, &one, 4, 4, 0xffffff, 0);
        font_cell(&engine, chain, 1, 'A', 1, &grid, size, 0, &two, 4, 4, 0xffffff, 0);
        check("polarity: white on black is the same pixels darkened or not",
              !memory_compare(polarity_one, polarity_two, sizeof(polarity_one)));
}

static fn ground(p32 address_to pixels)
{
        for (p32 i = 0; i < 96 * 96; i++)
                pixels[i] = 0xabcdef;
        for (p32 y = 8; y < 88; y++)
                for (p32 x = 8; x < 88; x++)
                        pixels[y * 96 + x] = 0x101820;
}

static fn check_drawing(const font_face address_to one, const font_face address_to two)
{
        const font_face address_to chain[2] = {one, two};
        font_target target = {canvas + 8 * 96 + 8, 96, 80, 80}, again = {canvas_again + 8 * 96 + 8, 96, 80, 80};
        static const p8 text[] = "ABCD\xe2\x86\x90\xe2\x94\x80";

        ground(canvas), ground(canvas_again);
        font_draw(&engine, chain, 2, text, sizeof(text) - 1, 20 * 64, 0, &target, 3 << 16, 40, 0xdfe7ef, 0x101820);
        font_draw(&engine, chain, 2, text, sizeof(text) - 1, 20 * 64, 0, &again, 3 << 16, 40, 0xdfe7ef, FONT_UNKNOWN);
        check("draw: a known ground draws what an unknown one blends to",
              !memory_compare(canvas, canvas_again, sizeof(canvas)));

        p8 inside = true;
        for (p32 i = 0; i < 96 * 96; i++)
                inside = inside && ((i / 96 >= 8 && i / 96 < 88 && i % 96 >= 8 && i % 96 < 88) || canvas[i] == 0xabcdef);
        check("draw: nothing outside the target", inside);

        //      Off every edge: the run hangs off the left, the top, the right
        //      and the bottom in turn, and nothing outside is written.
        static const b32 corners[4][2] = {{-30, 10}, {10, 5}, {60, 40}, {10, 95}};
        for (p32 k = 0; k < 4; k++)
        {
                ground(canvas);
                font_draw(&engine, chain, 2, text, sizeof(text) - 1, 30 * 64, 0, &target, corners[k][0] * (1 << 16),
                          corners[k][1], 0xffffff, k & 1 ? FONT_UNKNOWN : 0x101820);
                p8 clipped = true;
                for (p32 i = 0; i < 96 * 96; i++)
                        clipped = clipped && ((i / 96 >= 8 && i / 96 < 88 && i % 96 >= 8 && i % 96 < 88) ||
                                              canvas[i] == 0xabcdef);
                check("draw: clipped at every edge of the target", clipped);
        }

        //      A cell writes its whole rectangle -- ground or tone -- and nothing else.
        font_metrics grid = font_metrics_at(two, 16 * 64);
        const p32 address_to tones = font_tones(&engine, 0xdfe7ef, 0x303030, engine.contrast);
        static const p32 codes[] = {'A', 0x2500, 0x253c, ' ', 0x1f600, 0x2593};
        for (p32 k = 0; k < sizeof(codes) / sizeof(codes[0]); k++)
        {
                ground(canvas);
                font_cell(&engine, chain + 1, 1, codes[k], 1, &grid, 16 * 64, 0, &target, 20, 30, 0xdfe7ef, 0x303030);
                p8 whole = true;
                for (b32 y = 0; y < 96; y++)
                        for (b32 x = 0; x < 96; x++)
                        {
                                p8 in = x >= 28 && x < 28 + grid.cell_width && y >= 38 && y < 38 + grid.cell_height;
                                p32 pixel = canvas[y * 96 + x];
                                p8 tone = false;

                                for (p32 c = 0; c < 256 && in && !tone; c++)
                                        tone = pixel == tones[c];
                                whole = whole && (in ? tone && (codes[k] != ' ' || pixel == 0x303030)
                                                     : pixel == (x >= 8 && x < 88 && y >= 8 && y < 88 ? 0x101820u : 0xabcdefu));
                        }
                check("cell: its whole rectangle written in its tones, nothing else", whole);
        }

        //      A run of blank cells 1000 pixels wide: the background exactly,
        //      every pixel, and nothing past it.
        static p32 strip[4 * 1100];
        font_target wide = {strip, 1100, 1100, 4};
        memory_fill_u32(strip, sizeof(strip) / 4, 0xabcdef);
        font_cell(&engine, chain + 1, 1, ' ', 1000 / grid.cell_width, &grid, 16 * 64, 0, &wide, 7, -1, 0xdfe7ef, 0x303030);
        p8 blank = true;
        for (b32 i = 0; i < 4 * 1100; i++)
        {
                b32 x = i % 1100;
                blank = blank && strip[i] == (x >= 7 && x < 7 + 1000 / grid.cell_width * grid.cell_width ? 0x303030u : 0xabcdefu);
        }
        check("cell: a blank run is the background exactly, and no wider", blank);

        //      A tiny atlas, 128 by 32: at 30 px emptied many times over, at
        //      70 px too short for any glyph's shelf and lent whole to each.
        for (p32 size = 30; size <= 70; size += 40)
        {
                ground(canvas), ground(canvas_again);
                for (p32 pass = 0; pass < 3; pass++)
                {
                        font_draw(&engine, chain, 2, text, sizeof(text) - 1, size * 64, 0, &target, 0, size, 0xffffff, FONT_UNKNOWN);
                        font_draw(&small_engine, chain, 2, text, sizeof(text) - 1, size * 64, 0, &again, 0, size, 0xffffff, FONT_UNKNOWN);
                }
                check("atlas: a tiny one that resets draws what a roomy one draws",
                      !memory_compare(canvas, canvas_again, sizeof(canvas)));
        }
}

/*
        memory_translate_u32_rect against the loop it replaces: every width
        from 0 to 80 and every height from 0 to 9 at two strides, then rows
        about 128, 256 and 640 wide up to three high (bzhi reads only the
        low byte of its index, which is how a row of 256 once came out
        empty), the source's last row ending against a page that faults and
        the destination's too, the words between and before the rows held to
        a canary. On x86_64 once with the planes body and once with the narrow
        one; test/run runs riscv64 with V at two lengths and without it, every
        call there the first in a process, which asks for V. Three
        tables, which the planes body tells apart and draws three ways: words
        of four random bytes, three random bytes and a zero fourth (the text
        engine's), and a grey, its three low bytes alike.
*/
static fn check_translate_table(p32 kind);

static fn check_translate()
{
        for (p32 kind = 0; kind < 3; kind++)
                check_translate_table(kind);
}

static fn check_translate_table(p32 kind)
{
        static p32 table[512];
        p8 address_to source = memory(4 * 4096);
        p32 address_to into = memory(4 * 4096);
        p8 guarded = source && into &&
                       system_call_3(syscall(mprotect), (positive)(source + 3 * 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)into + 3 * 4096), 4096, 0) == 0;
        p64 state = 0x8f3a1c5e7b2d9046ull;

        check("translate: guard pages", guarded);
        if (!guarded)
                return;
        for (p32 i = 0; i < 256; i++)
        {
                XORSHIFT64(state);
                table[i] = kind == 0 ? (p32)state : kind == 1 ? (p32)state & 0xffffff : ((p32)state & 0xff) * 0x010101;
                for (p32 k = 0; k < 4; k++)
                        ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
        }
#if RISCV64
        //      The first call asks for V itself, with the height and the
        //      table in registers the detect does not keep.
        cpu_has_vector = 0;
        source[0] = 7, source[1] = 200, source[2] = 9, source[3] = 255;
        memory_translate_u32_rect(into, 2, source, 2, 2, 2, table);
        check("translate: the first call asks for V and still has its height and table",
              into[0] == table[7] && into[1] == table[200] && into[2] == table[9] && into[3] == table[255] &&
                  cpu_has_vector != 0);
#endif
#if X64
        p8 planes = cpu_has_avx512_vbmi;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx512_vbmi = tier ? 0 : planes;
#endif
                static const positive wide[] = {127, 128, 129, 255, 256, 257, 300, 640};
                p8 fine = true;
                for (positive w = 0; w <= 80 + sizeof(wide) / sizeof(wide[0]) - 1; w++)
                        for (positive width = w <= 80 ? w : wide[w - 81], height = 0; height <= (width > 80 ? 3u : 9u); height++)
                                for (positive gap = 0; gap <= 37; gap += 37)
                                {
                                        positive stride = width + gap, span = height ? (height - 1) * stride + width : 0;
                                        p8 address_to from = source + 3 * 4096 - span;
                                        p32 address_to to = (p32 address_to)((p8 address_to)into + 3 * 4096) - span;

                                        for (positive i = 0; i < span; i++)
                                        {
                                                XORSHIFT64(state);
                                                from[i] = (p8)state;
                                        }
                                        for (positive i = 0; i < span + 16; i++)
                                                to[(b64)i - 16] = 0xdeadbeef;
                                        VECTOR_FIRST_CALL(true);
                                        memory_translate_u32_rect(to, stride, from, stride, width, height, table);
                                        for (b64 i = -16; i < (b64)span; i++)
                                        {
                                                p8 inside = i >= 0 && (positive)i % stride < width;
                                                fine = fine && to[i] == (inside ? table[from[i]] : 0xdeadbeef);
                                        }
                                }
                check("translate: every word its byte's, nothing else written, every width and height", fine);
#if X64
        }
        cpu_has_avx512_vbmi = planes;
#endif
}

/*
        memory_translate_planes_u32_rect against its definition: every width
        from 0 to 40 and rows of 64 to 300 words, heights to 5, two strides,
        the source's last row and the destination's against pages that
        fault, the words around the rows held to a canary. Three tables:
        four random bytes a word, a zero fourth byte (the text engine's), and
        grey, its three low planes alike (arm64's one-plane body). On x86_64
        with VBMI and without, on riscv64 with every call the first in a
        process, which asks for V, and without V.
*/
static fn check_translate_planes()
{
        static p32 table[512];
        p8 address_to source = memory(8 * 4096);
        p32 address_to into = memory(4 * 4096);
        p8 guarded = source && into &&
                       system_call_3(syscall(mprotect), (positive)(source + 7 * 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)into + 3 * 4096), 4096, 0) == 0;
        p64 state = 0x51ce0f7a3d2b9c41ull;
        p8 fine = true;

        check("translate planes: guard pages", guarded);
        if (!guarded)
                return;
        for (p32 kind = 0; kind < 3; kind++)
        {
                for (p32 i = 0; i < 256; i++)
                {
                        XORSHIFT64(state);
                        table[i] = kind == 0 ? (p32)state : kind == 1 ? (p32)state & 0xffffff : ((p32)state & 0xff) * 0x010101;
                        for (p32 k = 0; k < 4; k++)
                                ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
                }
                const p8 address_to planes = (const p8 address_to)(table + 256);
#if RISCV64
                cpu_has_vector = 0;
                source[0] = 7, source[1] = 200, source[2] = 9, source[3] = 255;
                memory_translate_planes_u32_rect(into, 1, source, 4, 1, 1, table);
                check("translate planes: the first call asks for V and still has its height and table",
                      into[0] == ((p32)planes[7] | (p32)planes[256 + 200] << 8 | (p32)planes[512 + 9] << 16 |
                                  (p32)planes[768 + 255] << 24) &&
                          cpu_has_vector != 0);
                p8 vector = cpu_has_vector;
                for (p32 tier = 0; tier < 2; tier++)
                {
                        cpu_has_vector = tier ? 1 : vector;
#elif X64
                p8 wide_on = cpu_has_avx512_vbmi;
                for (p32 tier = 0; tier < 2; tier++)
                {
                        cpu_has_avx512_vbmi = tier ? 0 : wide_on;
#endif
                        static const positive wide[] = {64, 127, 128, 300};
                        for (positive w = 0; w <= 40 + sizeof(wide) / sizeof(wide[0]) - 1; w++)
                                for (positive width = w <= 40 ? w : wide[w - 41], height = 0; height <= (width > 40 ? 3u : 5u);
                                     height++)
                                        for (positive gap = 0; gap <= 7; gap += 7)
                                        {
                                                positive bytes = 4 * width, from_stride = bytes + gap, stride = width + gap;
                                                positive span = height ? (height - 1) * from_stride + bytes : 0;
                                                positive words = height ? (height - 1) * stride + width : 0;
                                                p8 address_to from = source + 7 * 4096 - span;
                                                p32 address_to to = (p32 address_to)((p8 address_to)into + 3 * 4096) - words;

                                                for (positive i = 0; i < span; i++)
                                                {
                                                        XORSHIFT64(state);
                                                        from[i] = (p8)state;
                                                }
                                                for (positive i = 0; i < words + 16; i++)
                                                        to[(b64)i - 16] = 0xdeadbeef;
                                                VECTOR_FIRST_CALL(tier == 0);
                                                memory_translate_planes_u32_rect(to, stride, from, from_stride, width, height, table);
                                                for (b64 i = -16; i < (b64)words; i++)
                                                {
                                                        p8 inside = i >= 0 && (positive)i % stride < width;
                                                        p32 want = 0xdeadbeef;

                                                        if (inside)
                                                        {
                                                                const p8 address_to b = from + (positive)i / stride * from_stride +
                                                                                        4 * ((positive)i % stride);
                                                                want = (p32)planes[b[0]] | (p32)planes[256 + b[1]] << 8 |
                                                                       (p32)planes[512 + b[2]] << 16 | (p32)planes[768 + b[3]] << 24;
                                                        }
                                                        fine = fine && to[i] == want;
                                                }
                                        }
#if RISCV64
                }
                cpu_has_vector = vector;
#elif X64
                }
                cpu_has_avx512_vbmi = wide_on;
#endif
        }
        check("translate planes: every byte its plane's, nothing else written, every width and height", fine);
}

/*
        memory_blend_u32_rect against the loop it replaces in font_blend:
        every width to 80 and rows of 127 to 300, heights to 6, two strides,
        coverage half zero, the pixels the ground but for a few that another
        glyph drew (none, one, or one in sixty), the bytes' last row and the
        pixels' last against pages that fault; the index it answers, every
        pixel before it written, none after. Both x86_64 bodies, and on
        riscv64 every call the first in a process, which asks for V.
*/
static positive blend_reference(p32 address_to into, positive stride, const p8 address_to from, positive from_stride,
                                positive width, positive height, const p32 address_to table)
{
        for (positive r = 0; r < height; r++)
                for (positive c = 0; c < width; c++)
                {
                        p32 a = from[r * from_stride + c], address_to p = &into[r * stride + c];

                        if (!a)
                                continue;
                        if ((*p & 0xffffff) != table[512])
                                return r * width + c;
                        *p = table[a];
                }
        return width * height;
}

/*
        memory_blend_u32_rect_baseline against the reference on random glyph rows: the answer (the index of the
        first pixel the run stops at, or the whole rectangle) and every word written must match, on widths and
        heights to 17 and 5 with gaps in both strides.
*/
static fn check_blend_baseline(void)
{
        static p32 table[513], want[64 * 16], into[64 * 16], seed[64 * 16];
        static p8 from[64 * 16];
        p64 state = 0x9e3779b97f4a7c15ull;
        p8 fine = true;

        for (p32 i = 0; i < 256; i++)
        {
                XORSHIFT64(state);
                table[i] = (p32)state & 0xffffff;
                for (p32 k = 0; k < 4; k++)
                        ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
        }
        table[512] = 0x203040;
        for (positive trial = 0; trial < 300; trial++)
        {
                positive width = trial % 17 + 1, height = trial % 5 + 1, stride = width + trial % 4;
                positive from_stride = width + (trial / 4) % 3;
                positive span = (height - 1) * stride + width;

                for (positive i = 0; i < span; i++)
                {
                        XORSHIFT64(state);
                        from[i] = state & 1 ? 0 : (p8)(state >> 8);
                        seed[i] = state & 4 ? (p32)0x203040 : (p32)(state >> 20) | (p32)0xff000000;
                        into[i] = seed[i];
                        want[i] = seed[i];
                }
                positive expected = blend_reference(want, stride, from, from_stride, width, height, table);
                positive answered = memory_blend_u32_rect_baseline(into, stride, from, from_stride, width, height, table);

                fine = fine && answered == expected && !memory_compare(into, want, span * 4);
        }
        check("blend: the baseline body answers and writes what the reference does", fine);
}

static fn check_blend()
{
        static const positive wide[] = {127, 128, 129, 255, 256, 257, 300};
        static p32 table[513], want[3072];
        p8 address_to source = memory(4 * 4096);
        p32 address_to into = memory(4 * 4096);
        p64 state = 0x2b7e151628aed2a6ull;
        p8 guarded = source && into &&
                       system_call_3(syscall(mprotect), (positive)(source + 3 * 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)into + 3 * 4096), 4096, 0) == 0;

        check("blend: guard pages", guarded);
        if (!guarded)
                return;
        for (p32 i = 0; i < 256; i++)
        {
                XORSHIFT64(state);
                table[i] = (p32)state & 0xffffff;
                for (p32 k = 0; k < 4; k++)
                        ((p8 address_to)(table + 256))[k * 256 + i] = (p8)(table[i] >> (8 * k));
        }
        table[512] = 0x203040;
#if RISCV64
        cpu_has_vector = 0;
        source[0] = 9, source[1] = 0, source[2] = 200, source[3] = 7;
        into[0] = into[1] = into[2] = 0xff203040, into[3] = 0x123456;
        positive first = memory_blend_u32_rect(into, 2, source, 2, 2, 2, table);
        check("blend: the first call asks for V and keeps its height and table",
              first == 3 && into[0] == table[9] && into[1] == 0xff203040 && into[2] == table[200] &&
                  into[3] == 0x123456 && cpu_has_vector != 0);
#endif
#if X64
        p8 planes = cpu_has_avx512_vbmi;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx512_vbmi = tier ? 0 : planes;
#endif
                p8 fine = true;
                for (positive w = 0; w <= 80 + sizeof(wide) / sizeof(wide[0]) - 1; w++)
                        for (positive width = w <= 80 ? w : wide[w - 81], height = 0; height <= (width > 80 ? 3u : 6u); height++)
                                for (positive shape = 0; shape < 6; shape++)
                                {
                                        positive gap = shape & 1 ? 0 : 37, stride = width + gap;
                                        positive span = height ? (height - 1) * stride + width : 0;
                                        p8 address_to from = source + 3 * 4096 - span;
                                        p32 address_to to = (p32 address_to)((p8 address_to)into + 3 * 4096) - span;

                                        for (positive i = 0; i < span; i++)
                                        {
                                                XORSHIFT64(state);
                                                from[i] = state & 1 ? 0 : (p8)(state >> 8);
                                                to[i] = (shape >> 1 == 2 && state % 60 == 7) || (shape >> 1 == 1 && i == span / 2)
                                                                ? 0x5a5a5a : 0xff203040 - (p32)(state >> 60) * 0x1000000;
                                                want[i] = to[i];
                                        }
                                        positive expected = blend_reference(want, stride, from, stride, width, height, table);
                                        VECTOR_FIRST_CALL(true);
                                        positive answered = memory_blend_u32_rect(to, stride, from, stride, width, height, table);

                                        fine = fine && answered == expected && !memory_compare(to, want, span * 4);
                                }
                check("blend: the ground's pixels its tones, the first other one answered, nothing after touched", fine);
#ifdef CHECK_verify
        check_blend_baseline(); /* memory_blend_u32_rect_baseline: the asm baseline body against the reference */
#endif
#if X64
        }
        cpu_has_avx512_vbmi = planes;
#endif
}

/*
        memory_mix_u32_rect against font_mix, the loop it replaces in font_blend
        on an unknown ground: every weight from 0 to 64, every coverage byte
        (the ramp a random table with the ends and a few values forced),
        grounds and colours random with their top bytes set, widths to 40 and
        a few wide ones, two strides, heights to 2, a zero byte in four and
        the others covered, the gap after each row and the bytes past the
        span untouched, and guard pages at both ends of the source and the
        pixels.
*/
static fn check_text_mix(font_engine address_to e)
{
        static const positive wide[] = {127, 128, 129, 255, 256, 257, 300};
        static p32 want[3 * 4096];
        static p8 ramp[256];
        p8 address_to source = memory(4 * 4096);
        p32 address_to into = memory(4 * 4096);
        p64 state = 0x243f6a8885a308d3ull;
        p8 guarded = source && into &&
                       system_call_3(syscall(mprotect), (positive)(source + 3 * 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)into + 3 * 4096), 4096, 0) == 0;
        p8 linear = e->linear;
        b32 plain = e->plain;

        check("mix: guard pages", guarded);
        if (!guarded)
                return;
        for (p32 i = 0; i < 256; i++)
        {
                XORSHIFT64(state);
                ramp[i] = (p8)(state >> 16);
        }
        ramp[0] = 0, ramp[1] = 255, ramp[2] = 1, ramp[3] = 254, ramp[255] = 128;
        e->linear = true;
        p8 fine = true;
        for (p32 weight = 0; weight <= 64; weight++)
        {
                for (positive w = 0; w <= 40 + sizeof(wide) / sizeof(wide[0]) - 1; w++)
                        for (positive width = w <= 40 ? w : wide[w - 41], height = 0; height <= 2; height++)
                                for (positive shape = 0; shape < 4; shape++)
                                {
                                        positive gap = shape & 1 ? 0 : 37, stride = width + gap;
                                        positive span = height ? (height - 1) * stride + width : 0;
                                        p8 address_to from = source + 3 * 4096 - span;
                                        p32 address_to to = (p32 address_to)((p8 address_to)into + 3 * 4096) - span;
                                        p32 colour = (p32)(state >> 20) & 0xffffff;

                                        XORSHIFT64(state);
                                        colour = shape & 2 ? colour | (p32)(state >> 40) << 24 : colour;
                                        e->plain = weight;
                                        for (positive i = 0; i < span; i++)
                                        {
                                                XORSHIFT64(state);
                                                from[i] = state & 3 ? (p8)(state >> 8) : 0;
                                                to[i] = (p32)(state >> 32);
                                                want[i] = to[i];
                                        }
                                        for (positive r = 0; r < height; r++)
                                                for (positive c = 0; c < width; c++)
                                                {
                                                        p8 cover = from[r * stride + c];

                                                        if (cover)
                                                                want[r * stride + c] =
                                                                    font_mix(e, want[r * stride + c], colour, ramp[cover]);
                                                }
                                        memory_mix_u32_rect(to, stride, from, stride, width, height, colour, weight, ramp,
                                                            font_to_linear, font_srgb_bytes);
                                        fine = fine && !memory_compare(to, want, span * 4);
                                }
        }
        check("mix: every pixel font_mix's answer, every weight, the rest untouched", fine);
        e->linear = linear;
        e->plain = plain;
}

/*
        memory_average_ahead against the byte loop it is: every count to 300
        and a few long ones, distances of a byte to a glass row's three and
        a half pages, the last byte it reads against a page that faults and
        the distance's bytes after the count left as they were; the three
        x86_64 bodies, and riscv64 with every call the first in a process,
        which asks for V, and without V.
*/
static fn check_average_ahead()
{
        static const positive distances[] = {1, 2, 3, 4, 5, 7, 8, 9, 16, 32, 63, 64, 65, 2560, 10240};
        static const positive longs[] = {511, 512, 513, 1000, 4095};
        static p8 want[3 * 4096];
        p8 address_to base = memory(4 * 4096);
        p64 state = 0x6a09e667f3bcc908ull;
        p8 guarded = base && system_call_3(syscall(mprotect), (positive)(base + 3 * 4096), 4096, 0) == 0;

        check("average: a guard page", guarded);
        if (!guarded)
                return;
#if RISCV64
        cpu_has_vector = 0;
        base[0] = 10, base[1] = 20, base[2] = 31;
        memory_average_ahead(base, 2, 1);
        check("average: the first call asks for V and keeps its arguments",
              base[0] == 15 && base[1] == 26 && base[2] == 31 && cpu_has_vector != 0);
        p8 vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
#endif
                p8 fine = true;
                for (positive d = 0; d < sizeof(distances) / sizeof(distances[0]); d++)
                        for (positive n = 0; n < 301 + sizeof(longs) / sizeof(longs[0]); n++)
                        {
                                positive count = n <= 300 ? n : longs[n - 301], distance = distances[d];
                                positive span = count + distance;
                                p8 address_to data = base + 3 * 4096 - span;

                                if (span > 3 * 4096)
                                        continue;
                                for (positive i = 0; i < span; i++)
                                {
                                        XORSHIFT64(state);
                                        data[i] = want[i] = (p8)(state >> 24);
                                }
                                for (positive i = 0; i < count; i++)
                                        want[i] = (p8)((want[i] + want[i + distance] + 1) >> 1);
                                VECTOR_FIRST_CALL(tier == 0);
                                memory_average_ahead(data, count, distance);
                                fine = fine && !memory_compare(data, want, span);
                        }
                check("average: each byte the mean rounded up with the one ahead, first to last, nothing after touched", fine);
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#endif
#if RISCV64
        }
        cpu_has_vector = vector;
#endif
}

/*
        The glass's three routines against the byte loops they are: halving
        (into apart from from and in place), doubling a pixel round what it
        names, and a premultiplied picture over a ground with its grain;
        widths to 40 and heights to 5 at pitches past them, against a page
        that faults after the last thing each reads or writes; the x86_64
        bodies with AVX2 and without, and riscv64 with every call the first in
        a process, which asks for V, and without V.
*/
static p32 glass_mean(p32 a, p32 b)
{
        p32 out = 0;

        for (p32 shift = 0; shift < 32; shift += 8)
                out |= (((a >> shift & 255) + (b >> shift & 255) + 1) >> 1) << shift;
        return out;
}

// The mean rounded down.
static p32 glass_low(p32 a, p32 b)
{
        p32 out = 0;

        for (p32 shift = 0; shift < 32; shift += 8)
                out |= (((a >> shift & 255) + (b >> shift & 255)) >> 1) << shift;
        return out;
}

static fn glass_halve_reference(p32 address_to into, positive into_pitch, const p32 address_to from, positive from_pitch,
                                positive width, positive height)
{
        for (positive y = 0; y < height; y++)
                for (positive x = 0; x < width; x++)
                {
                        const p32 address_to at = from + 2 * y * from_pitch + 2 * x;

                        into[y * into_pitch + x] = glass_low(glass_mean(at[0], at[from_pitch]), glass_mean(at[1], at[from_pitch + 1]));
                }
}

static fn glass_double_reference(p32 address_to into, positive into_pitch, const p32 address_to from, positive from_pitch,
                                 positive width, positive height)
{
        for (positive y = 0; y < height; y++)
                for (positive side = 0; side < 2; side++)
                {
                        const p32 address_to c = from + y * from_pitch;
                        const p32 address_to n = side ? c + from_pitch : c - from_pitch;
                        p32 address_to out = into + (2 * y + side) * into_pitch;

                        for (positive x = 0; x < width; x++)
                        {
                                p32 v = glass_low(c[x], glass_mean(c[x], n[x]));
                                p32 l = glass_low(c[x - 1], glass_mean(c[x - 1], n[x - 1]));
                                p32 r = glass_low(c[x + 1], glass_mean(c[x + 1], n[x + 1]));

                                out[2 * x] = glass_low(v, glass_mean(v, l));
                                out[2 * x + 1] = glass_low(v, glass_mean(v, r));
                        }
                }
}

static fn glass_over_reference(p32 address_to into, const p32 address_to picture, const p32 address_to ground, positive count,
                               const p32 address_to grain)
{
        for (positive i = 0; i < count; i++)
        {
                p32 p = picture[i], rest = 255 - (p >> 24), out = 0;

                for (p32 shift = 0; shift < 32; shift += 8)
                {
                        b32 c = (b32)(ground[i] >> shift & 255) + (b32)(grain[i & 15] >> shift & 255);

                        c = c > 255 ? 255 : c;
                        c -= (b32)(grain[64 + (i & 15)] >> shift & 255);
                        c = c < 0 ? 0 : c;
                        p32 t = (p32)c * rest + 128, v = (p >> shift & 255) + ((t + (t >> 8)) >> 8);

                        out |= (v > 255 ? 255 : v) << shift;
                }
                into[i] = out;
        }
}

static fn check_glass_routines()
{
        static p32 want[4096], grain[128];
        // What is read ends on the guard at word 4096, what is written on the one at word 12288.
        p32 address_to base = (p32 address_to)memory(16 * 4096);
        p32 address_to target = base + 10240;
        p64 state = 0x452821e638d01377ull;
        p8 guarded = base && system_call_3(syscall(mprotect), (positive)(base + 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(base + 12288), 4096, 0) == 0;

        check("glass routines: a guard page", guarded);
        if (!guarded)
                return;
        for (positive i = 0; i < 128; i++)
        {
                XORSHIFT64(state);
                grain[i] = (p32)(state >> 20) & 0x03030303u;
        }
#if RISCV64
        p8 vector = cpu_has_vector;
        cpu_has_vector = 0;
        {
                p32 two[8] = {0x10203040, 0x50607080, 0, 0, 0x10203040, 0x50607080, 0, 0}, one = 0;

                memory_halve_u32(&one, 1, two, 4, 1, 1);
                check("glass routines: the first call asks for V and keeps its arguments",
                      one == glass_low(glass_mean(two[0], two[4]), glass_mean(two[1], two[5])) && cpu_has_vector != 0);
        }
        vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
#if X64
        p8 wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier ? 0 : wide256;
#endif
                p8 halved = true, doubled = true, laid = true, in_place = true;

                for (positive width = 0; width <= 40; width++)
                        for (positive height = 0; height <= 5; height++)
                        {
                                positive pitch = 2 * width + 3, into_pitch = width + 1;
                                p32 address_to from = base + 4096 - 2 * height * pitch - 2;
                                p32 address_to into;

                                // Halving: from ends on the guard, into apart from it.
                                for (positive i = 0; i < 2 * height * pitch + 2; i++)
                                {
                                        XORSHIFT64(state);
                                        from[i] = (p32)(state >> 16);
                                }
                                into = target + 2048 - height * into_pitch;
                                memory_fill(want, 0x5a, sizeof(want));
                                memory_fill(into, 0x5a, height * into_pitch * 4);
                                glass_halve_reference(want, into_pitch, from, pitch, width, height);
                                VECTOR_FIRST_CALL(tier == 0);
                                memory_halve_u32(into, into_pitch, from, pitch, width, height);
                                halved = halved && !memory_compare(into, want, height * into_pitch * 4);

                                // In place, at the same pitch.
                                memory_copy(want, from, (2 * height * pitch + 2) * 4);
                                glass_halve_reference(want, pitch, want, pitch, width, height);
                                VECTOR_FIRST_CALL(tier == 0);
                                memory_halve_u32(from, pitch, from, pitch, width, height);
                                in_place = in_place && !memory_compare(from, want, (2 * height * pitch + 2) * 4);

                                // Doubling: a pixel round what it names, the row below the last against the guard.
                                {
                                        positive dpitch = width + 2, dinto = 2 * width + 1;
                                        p32 address_to whole = base + 4096 - (height + 2) * dpitch;
                                        p32 address_to named = whole + dpitch + 1;

                                        for (positive i = 0; i < (height + 2) * dpitch; i++)
                                        {
                                                XORSHIFT64(state);
                                                whole[i] = (p32)(state >> 8);
                                        }
                                        if (2 * height * dinto <= 2048)
                                        {
                                                into = target + 2048 - 2 * height * dinto;
                                                memory_fill(want, 0x5a, sizeof(want));
                                                memory_fill(into, 0x5a, 2 * height * dinto * 4);
                                                glass_double_reference(want, dinto, named, dpitch, width, height);
                                                VECTOR_FIRST_CALL(tier == 0);
                                                memory_double_u32(into, dinto, named, dpitch, width, height);
                                                doubled = doubled && !memory_compare(into, want, 2 * height * dinto * 4);
                                        }
                                }

                                // A picture over a ground: count = width * height + width, from every start in the grain.
                                {
                                        positive count = width * height + width;
                                        p32 address_to picture = base + 4096 - count, address_to ground = base + 8192 - count;
                                        positive start = (width + height) & 15;

                                        for (positive i = 0; i < count; i++)
                                        {
                                                XORSHIFT64(state);
                                                picture[i] = (p32)(state >> 32);
                                                // Premultiplied: no colour past its alpha, and some clear, some opaque.
                                                p32 alpha = (i % 5 == 0) ? 0 : (i % 7 == 0) ? 255 : picture[i] >> 24;
                                                p32 made = alpha << 24;
                                                for (p32 shift = 0; shift < 24; shift += 8)
                                                        made |= (((picture[i] >> shift & 255) * alpha) / 255) << shift;
                                                picture[i] = made;
                                                ground[i] = (p32)(state >> 8) & 0xffffffu;
                                        }
                                        into = target + 2048 - count;
                                        glass_over_reference(want, picture, ground, count, grain + start);
                                        VECTOR_FIRST_CALL(tier == 0);
                                        memory_glass_over_u32(into, picture, ground, count, grain + start);
                                        laid = laid && !memory_compare(into, want, count * 4);
                                }
                        }
                // Every width to 300 at every alignment to 63 words, two rows, apart and in place, against the
                // reference: the rows, and the words around them, which the reference's bounds hold still.
                {
                        static p32 swept_from[2800], swept_copy[2800], swept_into[1000], swept_want[1000];
                        p8 wide = true;

                        for (positive width = 0; width <= 300; width++)
                                for (positive at = 0; at < 64; at++)
                                {
                                        positive pitch = 2 * width + 5, into_pitch = width + 3;
                                        positive span = 4 * pitch + 64, ispan = 2 * into_pitch + 64;

                                        for (positive i = 0; i < span; i++)
                                        {
                                                XORSHIFT64(state);
                                                swept_from[i] = (p32)(state >> 16);
                                        }
                                        memory_fill(swept_into, 0x5a, ispan * 4);
                                        memory_fill(swept_want, 0x5a, ispan * 4);
                                        glass_halve_reference(swept_want + at, into_pitch, swept_from + at, pitch, width, 2);
                                        VECTOR_FIRST_CALL(tier == 0);
                                        memory_halve_u32(swept_into + at, into_pitch, swept_from + at, pitch, width, 2);
                                        wide = wide && !memory_compare(swept_into, swept_want, ispan * 4);

                                        memory_copy(swept_copy, swept_from, span * 4);
                                        glass_halve_reference(swept_copy + at, pitch, swept_copy + at, pitch, width, 2);
                                        VECTOR_FIRST_CALL(tier == 0);
                                        memory_halve_u32(swept_from + at, pitch, swept_from + at, pitch, width, 2);
                                        wide = wide && !memory_compare(swept_from, swept_copy, span * 4);
                                }
                        check("glass routines: halving every width to 300 at every alignment to 63 words, apart and in place, is the reference", wide);
                }
                check("glass routines: halving is the mean of each two by two, rows rounded up and columns down, nothing past it touched", halved);
                check("glass routines: halving in place at the same pitch is halving apart", in_place);
                check("glass routines: doubling is three quarters of a pixel and a quarter of its neighbour each way, rounded up and down", doubled);
                // Every count to 300 at every alignment to 63 words, each start in the grain (the alignment's low four
                // bits), against glass_over_reference: the same picture and ground the sweep above makes.
                {
                        static p32 glass_picture[400], glass_ground[400], glass_into[400], glass_made[400];
                        p8 glassy = true;

                        for (positive count = 0; count <= 300; count++)
                                for (positive at = 0; at < 64; at++)
                                {
                                        p32 address_to picture = glass_picture + at, address_to ground = glass_ground + at;

                                        for (positive i = 0; i < count; i++)
                                        {
                                                XORSHIFT64(state);
                                                picture[i] = (p32)(state >> 32);
                                                p32 alpha = (i % 5 == 0) ? 0 : (i % 7 == 0) ? 255 : picture[i] >> 24;
                                                p32 made = alpha << 24;
                                                for (p32 shift = 0; shift < 24; shift += 8)
                                                        made |= (((picture[i] >> shift & 255) * alpha) / 255) << shift;
                                                picture[i] = made;
                                                ground[i] = (p32)(state >> 8) & 0xffffffu;
                                        }
                                        memory_fill(glass_into, 0x5a, sizeof(glass_into));
                                        memory_fill(glass_made, 0x5a, sizeof(glass_made));
                                        glass_over_reference(glass_made + at, picture, ground, count, grain + (at & 15));
                                        VECTOR_FIRST_CALL(tier == 0);
                                        memory_glass_over_u32(glass_into + at, picture, ground, count, grain + (at & 15));
                                        glassy = glassy && !memory_compare(glass_into, glass_made, sizeof(glass_into));
                                }
                        check("glass routines: every count to 300 at every alignment to 63 words, each start in the grain, is the reference", glassy);
                }
                check("glass routines: a premultiplied picture over a ground with its grain, every colour rounded", laid);
#if X64
        }
        cpu_has_avx2 = wide256;
#endif
#if RISCV64
        }
        cpu_has_vector = vector;
#endif
}

/*
        The glass routines over every width to 300 and every alignment of their sources to a 64-byte line (sixteen
        words), each tier the machine has: AVX-512, AVX2 and SSE2 on x86-64, the vector and word bodies on riscv64,
        one body on arm64. Halving and doubling take two rows so the row step is run; glass takes a start in the grain
        that matches the alignment. Each output is compared with its scalar reference over its whole written area.
*/
static fn check_glass_tiers(void)
{
        static p32 field[4][8192], want[8192], grain[128];
        p64 state = 0x2d358dccaa6c78a5ull;
        p8 halved = true, doubled = true, laid = true;

        for (positive i = 0; i < 128; i++)
        {
                XORSHIFT64(state);
                grain[i] = (p32)(state >> 20) & 0x03030303u;
        }
#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                // Tier 0 is the AVX-512 body where the machine has it (the AVX2 body where it does not), 1 AVX2, 2 SSE2.
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
#endif
#if RISCV64
        p8 vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
                for (positive width = 0; width <= 300; width++)
                        for (positive a = 0; a < 16; a++)
                        {
                                // Halving: two rows of width pixels (pitch 2 width + 3), into at a pitch width + 1.
                                {
                                        positive pitch = 2 * width + 3, ipitch = width + 1, height = 2;
                                        p32 address_to from = field[0] + a, address_to into = field[1] + a;

                                        for (positive i = 0; i < 2 * height * pitch + 2; i++)
                                        {
                                                XORSHIFT64(state);
                                                from[i] = (p32)(state >> 16);
                                        }
                                        memory_fill(into, 0x5a, height * ipitch * 4);
                                        memory_fill(want, 0x5a, height * ipitch * 4);
                                        glass_halve_reference(want, ipitch, from, pitch, width, height);
                                        memory_halve_u32(into, ipitch, from, pitch, width, height);
                                        halved = halved && !memory_compare(into, want, height * ipitch * 4);
                                }
                                // Doubling: a pixel round what it names, two rows out.
                                {
                                        positive dpitch = width + 2, dinto = 2 * width + 1, height = 2;
                                        p32 address_to named = field[2] + a, address_to into = field[3] + a;

                                        for (positive i = 0; i < (height + 2) * dpitch; i++)
                                        {
                                                XORSHIFT64(state);
                                                named[i] = (p32)(state >> 8);
                                        }
                                        memory_fill(into, 0x5a, 2 * height * dinto * 4);
                                        memory_fill(want, 0x5a, 2 * height * dinto * 4);
                                        glass_double_reference(want, dinto, named + dpitch + 1, dpitch, width, height);
                                        memory_double_u32(into, dinto, named + dpitch + 1, dpitch, width, height);
                                        doubled = doubled && !memory_compare(into, want, 2 * height * dinto * 4);
                                }
                                // A premultiplied picture over a ground, from grain start a: count = width.
                                {
                                        positive count = width;
                                        p32 address_to picture = field[0] + a, address_to ground = field[1] + 16 + a;
                                        p32 address_to into = field[2] + a;

                                        for (positive i = 0; i < count; i++)
                                        {
                                                XORSHIFT64(state);
                                                picture[i] = (p32)(state >> 32);
                                                p32 alpha = (i % 5 == 0) ? 0 : (i % 7 == 0) ? 255 : picture[i] >> 24;
                                                p32 made = alpha << 24;
                                                for (p32 shift = 0; shift < 24; shift += 8)
                                                        made |= (((picture[i] >> shift & 255) * alpha) / 255) << shift;
                                                picture[i] = made;
                                                ground[i] = (p32)(state >> 8) & 0xffffffu;
                                        }
                                        memory_fill(into, 0x5a, count * 4);
                                        glass_over_reference(want, picture, ground, count, grain + a);
                                        memory_glass_over_u32(into, picture, ground, count, grain + a);
                                        laid = laid && !memory_compare(into, want, count * 4);
                                }
                        }
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#endif
#if RISCV64
        }
        cpu_has_vector = vector;
#endif
        check("glass tiers: halving is the mean of each two by two at every width to 300 and alignment, on each tier", halved);
        check("glass tiers: doubling is the rise at every width to 300 and alignment, on each tier", doubled);
        check("glass tiers: a picture over a ground at every width to 300 and grain start, on each tier", laid);
}

// The colour boost compositor_vivid made: each channel half as far again from the grey of the pixel, clamped.
static p32 saturate_reference(p32 pixel)
{
        int r = (int)(pixel >> 16 & 255), g = (int)(pixel >> 8 & 255), b = (int)(pixel & 255);
        int grey = (r * 77 + g * 150 + b * 29) >> 8;

        r += (r - grey) >> 1;
        g += (g - grey) >> 1;
        b += (b - grey) >> 1;
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        return (p32)(r << 16 | g << 8 | b);
}

#if defined(__aarch64__)
/* d8 and x19 are callee-saved on arm64 (AAPCS64: the low half of v8-v15, and x19-x28). The probe sets them, calls the
   body, and answers zero when both come back as they went. A body that spends d8 on a constant answers wrong here. */
#if defined(__APPLE__)
#define SATURATE_CALL "_memory_saturate_u32"
#define SATURATE_PROBE "_saturate_callee_probe"
#else
#define SATURATE_CALL "memory_saturate_u32"
#define SATURATE_PROBE "saturate_callee_probe"
#endif
unsigned long long saturate_callee_probe(unsigned int *into, const unsigned int *from, unsigned long count);
__asm__(
    ".text\n"
    ".globl " SATURATE_PROBE "\n"
    SATURATE_PROBE ":\n"
    "   stp x29, x30, [sp, #-16]!\n   mov x29, sp\n"
    "   fmov d8, #2.0\n   mov x19, #0x1234\n"
    "   bl " SATURATE_CALL "\n"
    "   fmov x0, d8\n   eor x0, x0, #0x4000000000000000\n   mov x1, #0x1234\n   eor x1, x19, x1\n   orr x0, x0, x1\n"
    "   ldp x29, x30, [sp], #16\n   ret\n");
#endif

/*
        memory_saturate_u32 against saturate_reference: every width to 300 at every alignment of its source and its
        destination to a 64-byte line, on each tier the machine has (AVX-512, AVX2 and the word body on x86-64; the
        vector and word bodies on riscv64; the one body on arm64), with the words past the count untouched. Then every
        pixel of the 24-bit colour space, 64 Ki at a time, and the top byte ignored.
*/
static fn check_saturate(void)
{
        static p32 source[1 << 16], made[1 << 16], want[1 << 16];
        static p32 small[512], small_made[512];
        p64 state = 0x6a09e667f3bcc909ull;
        p8 same = true, untouched = true, whole = true;

#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
#endif
#if RISCV64
        p8 vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
                // The count's words written and the words after them, which must not be: a source at offset a, a
                // destination at offset b, in a buffer of 512 words the sentinel fills first.
                for (positive width = 0; width <= 300; width++)
                        for (positive a = 0; a < 16; a++)
                                for (positive b = 0; b < 16; b += 5)
                                {
                                        p32 address_to from = small + a, address_to into = small_made + b;

                                        for (positive i = 0; i < width; i++)
                                        {
                                                XORSHIFT64(state);
                                                from[i] = (p32)(state >> 11);
                                        }
                                        memory_fill(small_made, 0x5a, sizeof(small_made));
                                        memory_saturate_u32(into, from, width);
                                        for (positive i = 0; i < width; i++)
                                                same = same && into[i] == saturate_reference(from[i]);
                                        for (positive i = b + width; i < b + width + 16 && i < 512; i++)
                                                untouched = untouched && small_made[i] == 0x5a5a5a5au;
                                }
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#endif
#if RISCV64
        }
        cpu_has_vector = vector;
#endif
        for (p32 chunk = 0; chunk < 256; chunk++)
        {
                for (p32 i = 0; i < (1 << 16); i++)
                {
                        source[i] = (chunk << 16 | i) | 0xa5000000u;
                        want[i] = saturate_reference(source[i]);
                }
                memory_fill(made, 0x5a, sizeof(made));
                memory_saturate_u32(made, source, 1 << 16);
                whole = whole && !memory_compare(made, want, sizeof(want));
        }
        check("saturate: every width to 300 at every alignment of source and destination, on each tier, is the boost of each pixel", same);
        check("saturate: words past the count are not touched", untouched);
        check("saturate: every colour of the 24-bit space, the top byte ignored and zero on the way out", whole);
#if defined(__aarch64__)
        check("saturate: d8 and x19 come back across the call, as AAPCS64 requires", saturate_callee_probe(small_made, small, 17) == 0);
#endif
}

// The glass's channel sums as compositor_glass_pass kept them, word by word: each channel of now added, the same channel
// of old taken off (64-bit, wrapping), and old takes now.
static fn sum_swap_reference(unsigned long long address_to sums, p32 address_to old, const p32 address_to now, positive count)
{
        for (positive i = 0; i < count; i++)
        {
                sums[0] += now[i] >> 16 & 255;
                sums[0] -= old[i] >> 16 & 255;
                sums[1] += now[i] >> 8 & 255;
                sums[1] -= old[i] >> 8 & 255;
                sums[2] += now[i] & 255;
                sums[2] -= old[i] & 255;
                old[i] = now[i];
        }
}

static fn check_sum_swap(void)
{
        static p32 old[300 + 32], made[300 + 32], now[300 + 32];
        unsigned long long sums[3], want[3];
        p64 state = 0x2545f4914f6cdd1dull;
        p8 same = true, untouched = true, big = true;
#if RISCV64
        /* the vector body and the scalar one, each run: the first call asks the machine for V, as the body does */
        p8 vector_asked;

        memory_sum_swap_u32((p64 address_to)sums, old, now, 4);
        vector_asked = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                if (tier == 0 && vector_asked != 2)
                        continue;
                cpu_has_vector = tier == 0 ? vector_asked : 1;
#endif

        // Every width to 300 at every alignment of now and of old, with the sums starting anywhere in 64 bits (so they
        // wrap), and the words past the count left as they were.
        for (positive width = 0; width <= 300; width++)
                for (positive a = 0; a < 16; a++)
                        for (positive b = 0; b < 16; b++)
                        {
                                p32 address_to from = now + a, address_to into = old + b;

                                for (positive i = 0; i < 300 + 32; i++)
                                {
                                        XORSHIFT64(state);
                                        now[i] = (p32)(state >> 13);
                                        old[i] = (p32)(state >> 7);
                                }
                                memory_copy(made, old, sizeof(old));
                                for (int k = 0; k < 3; k++)
                                {
                                        XORSHIFT64(state);
                                        sums[k] = want[k] = state;
                                }
                                sum_swap_reference(want, made + b, from, width);
                                memory_sum_swap_u32((p64 address_to)sums, into, from, width);
                                same = same && sums[0] == want[0] && sums[1] == want[1] && sums[2] == want[2] &&
                                       !memory_compare(into, made + b, width * 4);
                                for (positive i = b + width; i < b + width + 8 && i < 300 + 32; i++)
                                        untouched = untouched && old[i] == made[i];
                        }
        // A row of 65536 words, twice: the sums of many words, against the loop word by word, and the words written.
        for (positive round = 0; round < 2; round++)
        {
                static p32 long_now[1 << 16], long_old[1 << 16], long_want[1 << 16];

                for (positive i = 0; i < (1 << 16); i++)
                {
                        XORSHIFT64(state);
                        long_now[i] = (p32)(state >> 9);
                        long_old[i] = long_want[i] = (p32)(state >> 3);
                }
                for (int k = 0; k < 3; k++)
                {
                        XORSHIFT64(state);
                        sums[k] = want[k] = state;
                }
                sum_swap_reference(want, long_want, long_now, 1 << 16);
                memory_sum_swap_u32((p64 address_to)sums, long_old, long_now, 1 << 16);
                big = big && sums[0] == want[0] && sums[1] == want[1] && sums[2] == want[2] &&
                      !memory_compare(long_old, long_want, sizeof(long_old));
        }
#if RISCV64
        }
        cpu_has_vector = vector_asked;
#endif
        check("sum swap: every width to 300 at every alignment of now and old, the sums wrapping at 2^64, is the loop word by word", same);
        check("sum swap: words past the count are not touched", untouched);
        check("sum swap: a row of 65536 words, twice, sums and words as the loop makes them", big);
}

// The complement the frost made of a row, word by word, as compositor_frost did it.
static fn check_invert(void)
{
        static p32 source[300 + 32], made[300 + 32], shared[300 + 32], big_source[1 << 16], big_made[1 << 16];
        p64 state = 0x6a09e667f3bcc908ull;
        p8 same = true, untouched = true, inplace = true, big = true;
#if RISCV64
        /* the vector body and the scalar one, each run: the first call asks the machine for V, as the body does */
        p8 vector_asked;

        memory_invert_u32(made, source, 4);
        vector_asked = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                if (tier == 0 && vector_asked != 2)
                        continue;
                cpu_has_vector = tier == 0 ? vector_asked : 1;
#endif

        // Every width to 300 at every alignment of source and of the destination, apart from it and in place, with the
        // words outside the count left as they were.
        for (positive width = 0; width <= 300; width++)
                for (positive a = 0; a < 16; a++)
                        for (positive b = 0; b < 16; b++)
                        {
                                for (positive i = 0; i < 300 + 32; i++)
                                {
                                        XORSHIFT64(state);
                                        source[i] = (p32)(state >> 11);
                                        shared[i] = (p32)(state >> 5);
                                        made[i] = 0x5a5a5a5au;
                                }
                                memory_invert_u32(made + b, source + a, width);
                                for (positive i = 0; i < width; i++)
                                        same = same && made[b + i] == ~source[a + i];
                                for (positive i = 0; i < b; i++)
                                        untouched = untouched && made[i] == 0x5a5a5a5au;
                                for (positive i = b + width; i < b + width + 8 && i < 300 + 32; i++)
                                        untouched = untouched && made[i] == 0x5a5a5a5au;

                                memory_copy(made, shared, sizeof(shared));
                                memory_invert_u32(shared + a, shared + a, width);
                                for (positive i = 0; i < width; i++)
                                        inplace = inplace && shared[a + i] == ~made[a + i];
                                for (positive i = 0; i < 300 + 32; i++)
                                        if (i < a || i >= a + width)
                                                inplace = inplace && shared[i] == made[i];
                        }
        // A row of 65536 words, apart and in place, twice.
        for (positive round = 0; round < 2; round++)
        {
                for (positive i = 0; i < (1 << 16); i++)
                {
                        XORSHIFT64(state);
                        big_source[i] = (p32)(state >> 9);
                }
                memory_fill(big_made, 0x5a, sizeof(big_made));
                memory_invert_u32(big_made, big_source, 1 << 16);
                for (positive i = 0; i < (1 << 16); i++)
                        big = big && big_made[i] == ~big_source[i];
                memory_invert_u32(big_source, big_source, 1 << 16);
                for (positive i = 0; i < (1 << 16); i++)
                        big = big && big_source[i] == big_made[i];
        }
#if RISCV64
        }
        cpu_has_vector = vector_asked;
#endif
        check("invert: every width to 300 at every alignment of source and destination, apart, is the complement word by word", same);
        check("invert: words outside the count are not touched", untouched);
        check("invert: in place, every width and alignment, is the complement too, and the words around it are not touched", inplace);
        check("invert: a row of 65536 words, apart and in place, twice", big);
}

// The tint compositor_glass_tone made, with the cap and floor as parameters: window_mix toward the colour, then the
// brightest channel capped (scaled by cap over it) or else the dimmest raised to floor, with the divisions C makes.
static p32 tint_reference(p32 pixel, p32 toward, p32 amount, p32 cap, p32 floor_)
{
        int c[3], most, least;

        for (int i = 0; i < 3; i++)
        {
                unsigned int shift = 16 - 8 * (unsigned int)i;
                unsigned int from = pixel >> shift & 255, to = toward >> shift & 255;

                c[i] = (int)((from * (256 - amount) + to * amount + 128) >> 8);
        }
        most = c[0] > c[1] ? c[0] : c[1];
        most = most > c[2] ? most : c[2];
        least = c[0] < c[1] ? c[0] : c[1];
        least = least < c[2] ? least : c[2];
        if (most > (int)cap)
                for (int i = 0; i < 3; i++)
                        c[i] = c[i] * (int)cap / most;
        else if (least < (int)floor_)
                for (int i = 0; i < 3; i++)
                        c[i] = 255 - (255 - c[i]) * (255 - (int)floor_) / (255 - least);
        return (p32)(c[0] << 16 | c[1] << 8 | c[2]);
}

/*
        memory_tint_u32 against tint_reference: every width to 300 at every alignment of source and destination, on each
        tier (AVX-512, AVX2 and the word body on x86-64; the vector and word bodies on riscv64; the one body on arm64),
        with several amounts, caps and floors and both sides; the words past the count untouched. Then every colour of
        the 24-bit space on each side of the glass, with the glass's own cap and floor.
*/
static fn check_tint(void)
{
        static p32 source[1 << 16], made[1 << 16], want[1 << 16];
        static p32 small[512], small_made[512];
        static const p32 amounts[4] = {96, 0, 255, 37}, caps[5] = {0x5c, 255, 0, 1, 200}, floors[4] = {0xb4, 0, 255, 77};
        p64 state = 0x3c6ef372fe94f82bull;
        p8 same = true, untouched = true, whole = true;

#if X64
        p8 wide512 = cpu_has_avx512, wide256 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide256;
#endif
#if RISCV64
        p8 vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
                for (positive width = 0; width <= 300; width++)
                        for (positive a = 0; a < 16; a++)
                                for (positive b = 0; b < 16; b += 5)
                                {
                                        p32 address_to from = small + a, address_to into = small_made + b;
                                        p32 toward = (width & 1) ? 0xffffffu : 0, amount = amounts[(width >> 1) & 3];
                                        p32 cap = caps[(width / 3) % 5], floor_ = floors[(width / 7) & 3];

                                        for (positive i = 0; i < width; i++)
                                        {
                                                XORSHIFT64(state);
                                                from[i] = (p32)(state >> 11);
                                        }
                                        memory_fill(small_made, 0x5a, sizeof(small_made));
                                        memory_tint_u32(into, from, width, toward, amount, cap, floor_);
                                        for (positive i = 0; i < width; i++)
                                                same = same && into[i] == tint_reference(from[i], toward, amount, cap, floor_);
                                        for (positive i = b + width; i < b + width + 16 && i < 512; i++)
                                                untouched = untouched && small_made[i] == 0x5a5a5a5au;
                                }
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide256;
#endif
#if RISCV64
        }
        cpu_has_vector = vector;
#endif
        for (p32 side = 0; side < 2; side++)
        {
                p32 toward = side ? 0xffffffu : 0, cap = side ? 255 : 0x5c, floor_ = side ? 0xb4 : 0;

                for (p32 chunk = 0; chunk < 256; chunk++)
                {
                        for (p32 i = 0; i < (1 << 16); i++)
                        {
                                source[i] = (chunk << 16 | i) | 0x5a000000u;
                                want[i] = tint_reference(source[i], toward, 96, cap, floor_);
                        }
                        memory_fill(made, 0x5a, sizeof(made));
                        memory_tint_u32(made, source, 1 << 16, toward, 96, cap, floor_);
                        whole = whole && !memory_compare(made, want, sizeof(want));
                }
        }
        check("tint: every width to 300 at every alignment of source and destination, several amounts, caps and floors, on each tier, is the window's tint", same);
        check("tint: words past the count are not touched", untouched);
        check("tint: every colour of the 24-bit space on each side of the glass, with the glass's cap and floor", whole);
}

/*
        memory_mix_u32_row against mix_reference, which is the compositor's mix written out: a cover of 0 keeps the pixel,
        255 takes the ink whole, and between each channel is (g (255 - c) + i c + 127) / 255 rounded down, the top byte
        zero. Every width to 300 at every alignment of pixels, ink and cover, the covers cycling through zero, 255 and
        values between; the words and bytes outside the run untouched. Then every channel value against every ink
        channel at every cover from 1 to 254, the other channels held.
*/
static p32 mix_reference(p32 pixel, p32 ink, p32 cover)
{
        p32 out = 0;

        if (cover == 0)
                return pixel;
        if (cover == 255)
                return ink;
        for (unsigned int shift = 0; shift < 24; shift += 8)
                out |= ((pixel >> shift & 255) * (255 - cover) + (ink >> shift & 255) * cover + 127) / 255 << shift;
        return out;
}

static fn check_mix(void)
{
        static p32 ink[512], made[512], want[512], row[256], ground[256];
        static p8 cover[512], cover_row[256];
        p64 state = 0x2545f4914f6cdd1dull;
        p8 same = true, untouched = true, whole = true;

#if RISCV64
        p8 vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
        for (positive width = 0; width <= 300; width++)
                for (positive a = 0; a < 4; a++)
                        for (positive b = 0; b < 8; b++)
                                for (positive c = 0; c < 4; c++)
                                {
                                        for (positive i = 0; i < 512; i++)
                                        {
                                                XORSHIFT64(state);
                                                ink[i] = (p32)(state >> 11);
                                                XORSHIFT64(state);
                                                cover[i] = (p8)(state >> 7);
                                                if ((state >> 20) % 4 == 0)
                                                        cover[i] = (state >> 22) & 1 ? 0 : 255;
                                        }
                                        memory_fill(made, 0x5a, sizeof(made));
                                        for (positive i = 0; i < width; i++)
                                        {
                                                XORSHIFT64(state);
                                                made[b + i] = (p32)(state >> 9);
                                                want[b + i] = mix_reference(made[b + i], ink[a + i], cover[c + i]);
                                        }
                                        memory_mix_u32_row(made + b, ink + a, cover + c, width);
                                        for (positive i = 0; i < width; i++)
                                                same = same && made[b + i] == want[b + i];
                                        for (positive i = 0; i < b; i++)
                                                untouched = untouched && made[i] == 0x5a5a5a5au;
                                        for (positive i = b + width; i < b + width + 16 && i < 512; i++)
                                                untouched = untouched && made[i] == 0x5a5a5a5au;
                                }

        for (p32 channel = 0; channel < 3; channel++)
                for (p32 c = 1; c < 255; c++)
                {
                        p32 shift = 8 * channel;

                        for (p32 i = 0; i < 256; i++)
                        {
                                row[i] = (0x3cc3a55au & ~(255u << shift)) | i << shift;
                                cover_row[i] = (p8)c;
                        }
                        for (p32 g = 0; g < 256; g++)
                        {
                                for (p32 i = 0; i < 256; i++)
                                {
                                        ground[i] = (0x5ac3a500u & ~(255u << shift)) | g << shift;
                                        made[i] = ground[i];
                                }
                                memory_mix_u32_row(made, row, cover_row, 256);
                                for (p32 i = 0; i < 256; i++)
                                        whole = whole && made[i] == mix_reference(ground[i], row[i], c);
                        }
                }
#if RISCV64
        }
        cpu_has_vector = vector;
#endif
        check("mix: every width to 300 at every alignment of pixels, ink and cover, is the compositor's mix, with cover 0 and 255 whole", same);
        check("mix: words and bytes outside the run are not touched", untouched);
        check("mix: every red, green and blue against every other at every cover from 1 to 254 is the compositor's mix", whole);
}

/*
        memory_fill_u32_rect: every width to 80 and rows of 127 to 300,
        heights to 6, two strides, the last row against a page that faults,
        the words between rows and before the first held to a canary; with
        AVX-512 and without on x86_64, and on riscv64 every call the first in
        a process, which asks for V.
*/
static fn check_fill_rect()
{
        static const positive wide[] = {127, 128, 129, 255, 256, 257, 300};
        p32 address_to into = memory(4 * 4096);
        p8 guarded = into && system_call_3(syscall(mprotect), (positive)((p8 address_to)into + 3 * 4096), 4096, 0) == 0;

        check("fill: a guard page", guarded);
        if (!guarded)
                return;
#if RISCV64
        cpu_has_vector = 0;
        into[0] = into[1] = into[2] = into[3] = 1;
        memory_fill_u32_rect(into, 2, 1, 2, 7);
        check("fill: the first call asks for V and keeps its arguments",
              into[0] == 7 && into[1] == 1 && into[2] == 7 && into[3] == 1 && cpu_has_vector != 0);
#endif
#if X64
        p8 wide_on = cpu_has_avx512;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx512 = tier ? 0 : wide_on;
#endif
                p8 fine = true;
                for (positive w = 0; w <= 80 + sizeof(wide) / sizeof(wide[0]) - 1; w++)
                        for (positive width = w <= 80 ? w : wide[w - 81], height = 0; height <= (width > 80 ? 3u : 6u); height++)
                                for (positive gap = 0; gap <= 37; gap += 37)
                                {
                                        positive stride = width + gap, span = height ? (height - 1) * stride + width : 0;
                                        p32 address_to to = (p32 address_to)((p8 address_to)into + 3 * 4096) - span;

                                        for (positive i = 0; i < span + 16; i++)
                                                to[(b64)i - 16] = 0xdeadbeef;
                                        VECTOR_FIRST_CALL(true);
                                        memory_fill_u32_rect(to, stride, width, height, 0x00c0ffee + (p32)width);
                                        for (b64 i = -16; i < (b64)span; i++)
                                                fine = fine && to[i] == (i >= 0 && (positive)i % stride < width ? 0x00c0ffee + (p32)width
                                                                                                                : 0xdeadbeef);
                                }
                check("fill: every word of the rectangle, nothing else", fine);
#if X64
        }
        cpu_has_avx512 = wide_on;
#endif
}

/*
        memory_copy_rect_u32: every width to 300 words, zero to three rows, the source and the destination each at every
        word offset to sixteen, strides of the width plus a few words, the two rectangles ending a few words short of a
        page that faults so a word read or written past them does too, every word beside the rectangle held to a
        canary and the source to what it was; with AVX-512 and without on x86_64, and on riscv64 every call the first
        in a process, which asks for V. Then the rectangles overlap, in place and across rows: the answer is what
        memory_copy makes of them one row at a time, in row order.
*/
static fn check_copy_rect()
{
        static const positive gaps[3][2] = {{0, 0}, {1, 37}, {37, 1}};
        p32 address_to from_pool = memory(4 * 4096);
        p32 address_to to_pool = memory(4 * 4096);
        p8 guarded = from_pool && to_pool && system_call_3(syscall(mprotect), (positive)((p8 address_to)from_pool + 3 * 4096), 4096, 0) == 0
                       && system_call_3(syscall(mprotect), (positive)((p8 address_to)to_pool + 3 * 4096), 4096, 0) == 0;

        check("copy rect: a guard page at the end of each", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide_on = cpu_has_avx512;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx512 = tier ? 0 : wide_on;
#endif
                p8 fine = true, kept = true;
                p32 address_to from_end = from_pool + 3 * 4096 / 4;
                p32 address_to to_end = to_pool + 3 * 4096 / 4;

                for (positive w = 0; w <= 300; w++)
                        for (positive height = 0; height <= 3; height++)
                                for (positive g = 0; g < 3; g++)
                                        for (positive sa = 0; sa < 16; sa++)
                                                for (positive da = 0; da < 16; da++)
                                        {
                                                positive ds = w + gaps[g][0], ss = w + gaps[g][1];
                                                positive span_d = height ? (height - 1) * ds + w : 0;
                                                positive span_s = height ? (height - 1) * ss + w : 0;
                                                p32 address_to fwin = from_end - (48 + span_s);
                                                p32 address_to twin = to_end - (48 + span_d);
                                                p32 address_to frect = fwin + 16 + sa;
                                                p32 address_to trect = twin + 16 + da;

                                                for (positive i = 0; i < 48 + span_s; i++)
                                                        fwin[i] = 0x5a000000u + (p32)i;
                                                for (positive i = 0; i < 48 + span_d; i++)
                                                        twin[i] = 0xdeadbeef;
                                                VECTOR_FIRST_CALL(true);
                                                memory_copy_rect_u32(trect, ds, frect, ss, w, height);
                                                for (positive r = 0; r < height; r++)
                                                        for (positive c = 0; c < w; c++)
                                                                fine = fine && trect[r * ds + c] == frect[r * ss + c];
                                                for (b64 i = 0; i < (b64)(48 + span_d); i++)
                                                {
                                                        b64 j = i - (16 + (b64)da);
                                                        p8 inside = j >= 0 && j < (b64)span_d && (positive)j % ds < w;

                                                        fine = fine && (inside || twin[i] == 0xdeadbeef);
                                                }
                                                for (positive i = 0; i < 48 + span_s; i++)
                                                        kept = kept && fwin[i] == 0x5a000000u + (p32)i;
                                        }
                check("copy rect: every word of the rectangle, nothing else, the source unchanged", fine && kept);

                fine = true;
                for (positive w = 1; w <= 300; w++)
                        for (positive height = 1; height <= 3; height++)
                                for (b64 shift = -20; shift <= 20; shift += 5)
                                {
                                        positive ds = w + 3, ss = w + 1;
                                        p32 address_to base = from_pool + 512;
                                        p32 address_to ref = to_pool + 512;
                                        p32 address_to dst = base + 40;
                                        p32 address_to src = (p32 address_to)((b64)dst + 4 * shift);

                                        for (positive i = 0; i < 1024; i++)
                                                base[i] = ref[i] = 0x3c000000u + (p32)i * 2654435761u;
                                        VECTOR_FIRST_CALL(true);
                                        memory_copy_rect_u32(dst, ds, src, ss, w, height);
                                        for (positive r = 0; r < height; r++)
                                                memory_copy(ref + 40 + r * ds, (p32 address_to)((b64)ref + 4 * (40 + shift + (b64)(r * ss))), w * 4);
                                        for (positive i = 0; i < 1024; i++)
                                                fine = fine && base[i] == ref[i];
                                }
                check("copy rect: overlapping rows answer as memory_copy one row at a time", fine);
#if X64
        }
        cpu_has_avx512 = wide_on;
#endif
}

/*
        memory_copy_rect_stream_u32: the rectangle memory_copy_rect_u32 makes when the two do not overlap, for widths from zero
        to 400 words (the streaming loop begins at 32 words), zero to three rows, the source at every word offset to sixteen
        (every alignment of 32 bytes the loop's head walks) and the destination at the first few, strides of the width plus a few
        words, the rectangles ending a few words short of a page that faults so a word read or written past them does too, every
        word beside the rectangle held to a canary and the source to what it was; with AVX2 and without on x86_64.
*/
static fn check_copy_rect_stream()
{
        static const positive gaps[3][2] = {{0, 0}, {1, 37}, {37, 1}};
        p32 address_to from_pool = memory(4 * 4096);
        p32 address_to to_pool = memory(4 * 4096);
        p8 guarded = from_pool && to_pool && system_call_3(syscall(mprotect), (positive)((p8 address_to)from_pool + 3 * 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)to_pool + 3 * 4096), 4096, 0) == 0;

        check("copy rect stream: a guard page at the end of each", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide_on = cpu_has_avx2;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier ? 0 : wide_on;
#endif
                p8 fine = true, kept = true;
                p32 address_to from_end = from_pool + 3 * 4096 / 4;
                p32 address_to to_end = to_pool + 3 * 4096 / 4;

                for (positive w = 0; w <= 400; w++)
                        for (positive height = 0; height <= 3; height++)
                                for (positive g = 0; g < 3; g++)
                                        for (positive sa = 0; sa < 16; sa++)
                                                for (positive da = 0; da < 4; da++)
                                                {
                                                        positive ds = w + gaps[g][0], ss = w + gaps[g][1];
                                                        positive span_d = height ? (height - 1) * ds + w : 0;
                                                        positive span_s = height ? (height - 1) * ss + w : 0;
                                                        p32 address_to fwin = from_end - (48 + span_s);
                                                        p32 address_to twin = to_end - (48 + span_d);
                                                        p32 address_to frect = fwin + 16 + sa;
                                                        p32 address_to trect = twin + 16 + da;

                                                        for (positive i = 0; i < 48 + span_s; i++)
                                                                fwin[i] = 0x5a000000u + (p32)i;
                                                        for (positive i = 0; i < 48 + span_d; i++)
                                                                twin[i] = 0xdeadbeef;
                                                        memory_copy_rect_stream_u32(trect, ds, frect, ss, w, height);
                                                        for (positive r = 0; r < height; r++)
                                                                for (positive c = 0; c < w; c++)
                                                                        fine = fine && trect[r * ds + c] == frect[r * ss + c];
                                                        for (b64 i = 0; i < (b64)(48 + span_d); i++)
                                                        {
                                                                b64 j = i - (16 + (b64)da);
                                                                p8 inside = j >= 0 && j < (b64)span_d && (positive)j % ds < w;

                                                                fine = fine && (inside || twin[i] == 0xdeadbeef);
                                                        }
                                                        for (positive i = 0; i < 48 + span_s; i++)
                                                                kept = kept && fwin[i] == 0x5a000000u + (p32)i;
                                                }
                check("copy rect stream: every word of the rectangle, nothing else, the source unchanged", fine && kept);
#if X64
        }
        cpu_has_avx2 = wide_on;
#endif
}


/*
        memory_runs_u32 is the contract in its comment, written out: a row is chunks of 16 words, the last short one and any
        past the 4095th left out, a chunk is flat when its words are its first, a run is two or more flat chunks in a row of one
        colour, and a row keeps the first 15. The reference is that, a chunk at a time with the loops in plain sight; the
        routine's rows must equal it in the count, the spare word and every run a row has, and the bytes after a row's last
        run, the rows after the last and the picture must be as they were. The pictures are made of stretches of one
        colour (from a palette of three, so neighbours join) and of noise, of random lengths, the source at each word offset
        to 15 and ending at a guard page; every width to 300 words, zero to three rows, and one past the 4095th chunk; with
        AVX-512 and without on x86_64.
*/
static void runs_reference(p8 address_to rows, const p32 address_to from, positive from_stride, positive width, positive height)
{
        positive chunks = width / 16 > 4095 ? 4095 : width / 16;

        for (positive y = 0; y < height; y++)
        {
                p8 address_to row = rows + y * 128;
                const p32 address_to line = from + y * from_stride;
                positive count = 0, start = 0;
                p8 open = false;
                p32 colour = 0;

                for (positive c = 0; c <= chunks; c++)
                {
                        p8 flat = false;

                        if (c < chunks)
                        {
                                flat = true;
                                for (positive k = 1; k < 16; k++)
                                        flat = flat && line[c * 16 + k] == line[c * 16];
                        }
                        if (flat && open && line[c * 16] == colour)
                                continue;
                        if (open && c - start >= 2 && count < 15)
                        {
                                p16 address_to run = (p16 address_to)(row + 8 + count * 8);

                                run[0] = (p16)(start * 16);
                                run[1] = (p16)(c * 16);
                                *(p32 address_to)(row + 8 + count * 8 + 4) = colour;
                                count++;
                        }
                        open = flat;
                        if (flat)
                        {
                                start = c;
                                colour = line[c * 16];
                        }
                }
                *(p32 address_to)row = (p32)count;
                *(p32 address_to)(row + 4) = 0;
        }
}

static fn check_runs()
{
        static const p32 palette[3] = {0x00203040u, 0x00ffffffu, 0x80000000u};
        p32 address_to from_pool = memory(80 * 4096);
        p8 address_to out_pool = memory(4096);
        p8 address_to want_pool = memory(4096);
        p8 guarded = from_pool && out_pool && want_pool &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)from_pool + 79 * 4096), 4096, 0) == 0;
        p64 seed = 0x9e3779b97f4a7c15ull;

        check("runs: a guard page at the end of the picture", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide_on = cpu_has_avx512;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_avx512 = tier ? 0 : wide_on;
#endif
                p8 fine = true, kept = true;
                p32 address_to stop = from_pool + 79 * 4096 / 4;

                // Widths to 300 words, and three that hold more runs than a row keeps and one past the 4095th chunk.
                for (positive shape = 0; shape < 305 + 3; shape++)
                        for (positive height = 0; height <= 3; height++)
                                for (positive sa = 0; sa < 16; sa++)
                                {
                                        positive w = shape <= 300 ? shape : shape == 301 ? 400 : shape == 302 ? 770 : 800;
                                        positive stride = w + sa % 3 * 7;
                                        positive span = height ? (height - 1) * stride + w : 0;
                                        p32 address_to picture = stop - span - sa;
                                        p64 before = 0, after = 0;

                                        for (positive y = 0; y < height; y++)
                                        {
                                                positive x = 0;

                                                while (x < w)
                                                {
                                                        positive length;
                                                        p8 solid;
                                                        p32 colour;

                                                        XORSHIFT64(seed);
                                                        solid = shape > 300 ? (x / 16) % 3 != 2 : (seed >> 33) % 3 != 0;
                                                        length = shape > 300 ? (solid ? 32 : 16) : 1 + (seed >> 20) % ((seed >> 10) % 4 ? 40 : 90);
                                                        length = x + length > w ? w - x : length;
                                                        colour = palette[shape > 300 ? 0 : (seed >> 50) % 3];
                                                        for (positive k = 0; k < length; k++)
                                                        {
                                                                XORSHIFT64(seed);
                                                                picture[y * stride + x + k] = solid ? colour : (p32)(seed >> 8);
                                                        }
                                                        x += length;
                                                }
                                        }
                                        for (positive i = 0; i < span; i++)
                                                before = before * 31 + picture[i];
                                        memory_fill(out_pool, 0xa5, 4096);
                                        memory_fill(want_pool, 0xa5, 4096);
                                        runs_reference(want_pool + 64, picture, stride, w, height);
                                        memory_runs_u32(out_pool + 64, picture, stride, w, height);
                                        fine = fine && !memory_compare(out_pool, want_pool, 4096);
                                        for (positive i = 0; i < span; i++)
                                                after = after * 31 + picture[i];
                                        kept = kept && before == after;
                                }
                check("runs: every row the reference makes, and nothing past a row's last run or the last row", fine);
                check("runs: the picture is what it was", kept);

                // One past the 4095th chunk: the row ends at 65520 words, with the 16 + 5 beyond it unread.
                {
                        positive w = 4096 * 16 + 5;
                        p32 address_to picture = stop - w;

                        for (positive i = 0; i < w; i++)
                                picture[i] = 0x00123456u;
                        memory_fill(out_pool, 0xa5, 4096);
                        memory_fill(want_pool, 0xa5, 4096);
                        runs_reference(want_pool + 64, picture, w, w, 1);
                        memory_runs_u32(out_pool + 64, picture, w, w, 1);
                        check("runs: no chunk past the 4095th is read", !memory_compare(out_pool, want_pool, 4096) &&
                                                                          *(p32 address_to)(out_pool + 64) == 1 &&
                                                                          *(p16 address_to)(out_pool + 64 + 10) == 4095 * 16);
                }
#if X64
        }
        cpu_has_avx512 = wide_on;
#endif
}

/*
        The walk compositor_blit_scaled made before it moved to lib.c, with the rectangle as arguments: the visible part of
        a width by height rectangle, its top left at left, top, of a picture from_width by from_height, a source position
        per target pixel's centre stepping in 24-bit fixed point and rounded down. memory_resample_u32_rect is checked
        against it, and timed against it below.
*/
static void resample_reference(unsigned int address_to into, positive into_stride, positive visible_width,
                               positive visible_height, const unsigned int address_to from, positive from_stride,
                               positive from_width, positive from_height, positive width, positive height,
                               positive left, positive top)
{
        positive step_x = (from_width << 24) / width, step_y = (from_height << 24) / height;
        positive fy = ((2 * top + 1) * from_height << 23) / height;

        if (!visible_width || !visible_height)
                return;
        for (positive row = 0; row < visible_height; row++, fy += step_y)
        {
                const unsigned int address_to source = from + (fy >> 24) * from_stride;
                unsigned int address_to to = into + row * into_stride;
                positive fx = ((2 * left + 1) * from_width << 23) / width;

                for (positive col = 0; col < visible_width; col++, fx += step_x)
                        *to++ = source[fx >> 24];
        }
}

/*
        memory_resample_u32_rect against resample_reference: every width to 300 and a sample of heights to 300, a picture
        at 100%, 104% (the landing's 96% to 100%), half and double of the rectangle, the visible part clipped on the left
        and top, on the right and bottom or not at all, every word offset of the target and of the picture from zero to
        fifteen (four each), rows strided past their width, the words around the visible part held to a canary; on x86_64
        with AVX-512, with AVX2 alone and with neither, and on arm64 and riscv64 once.
*/
static fn check_resample_rect()
{
        static const positive heights[] = {1, 2, 3, 4, 7, 8, 9, 16, 17, 31, 33, 64, 65, 127, 129, 255, 300};
        static const positive offsets[] = {0, 1, 8, 15};
        static unsigned int picture[1 << 19], into_asm[1 << 17], into_ref[1 << 17];
        p8 fine = true;
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;
#endif

        for (positive word = 0; word < sizeof(picture) / sizeof(picture[0]); word++)
                picture[word] = (unsigned int)word * 2654435761u;
#if X64
        for (positive tier = 0; tier < 3; tier++)
#else
        for (positive tier = 0; tier < 1; tier++)
#endif
        {
#if X64
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : wide2;
#endif
                for (positive width = 1; width <= 300; width++)
                        for (positive at = 0; at < sizeof(heights) / sizeof(heights[0]); at++)
                                for (positive scale = 0; scale < 4; scale++)
                                        for (positive clip = 0; clip < 4; clip++)
                                                for (positive pair = 0; pair < 16; pair++)
                                                {
                                                        positive height = heights[at];
                                                        positive from_width = scale == 0 ? width : scale == 1 ? width + width / 25 : scale == 2 ? width / 2 + 1 : 2 * width - 1;
                                                        positive from_height = scale == 0 ? height : scale == 1 ? height + height / 25 : scale == 2 ? height / 2 + 1 : 2 * height - 1;
                                                        positive left = clip & 1 ? 3 : 0, top = clip & 2 ? 2 : 0;
                                                        positive right = clip == 3 ? 1 : 0, bottom = clip == 1 ? 1 : 0;
                                                        positive ao = offsets[pair / 4], fo = offsets[pair % 4];
                                                        positive into_stride = width + (pair & 1) * 3, from_stride = from_width + (pair & 2) * 2;
                                                        positive vis_w, vis_h, span;

                                                        if (left + right >= width || top + bottom >= height)
                                                                continue;
                                                        vis_w = width - left - right;
                                                        vis_h = height - top - bottom;
                                                        span = vis_h * into_stride + vis_w + 16;
                                                        for (positive i = 0; i < ao + span + 16; i++)
                                                                into_asm[i] = into_ref[i] = 0xdeadbeefu;
                                                        resample_reference(into_ref + ao, into_stride, vis_w, vis_h, picture + fo, from_stride,
                                                                           from_width, from_height, width, height, left, top);
                                                        memory_resample_u32_rect(into_asm + ao, into_stride, vis_w, vis_h, picture + fo, from_stride,
                                                                                 from_width, from_height, width, height, left, top);
                                                        for (positive i = 0; i < ao + span + 16; i++)
                                                                fine = fine && into_asm[i] == into_ref[i];
                                                }
        }
#if X64
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide2;
#endif
        check("resample: every visible word of the rectangle the reference's, nothing else touched, on each tier", fine);
        check("resample: a zero visible size touches nothing", ({
                      static unsigned int canary[4] = {7, 7, 7, 7};
                      memory_resample_u32_rect(canary, 2, 0, 3, picture, 4, 4, 4, 2, 2, 0, 0);
                      memory_resample_u32_rect(canary, 2, 3, 0, picture, 4, 4, 4, 2, 2, 0, 0);
                      canary[0] == 7 && canary[1] == 7 && canary[2] == 7 && canary[3] == 7;
              }));
}

/*
        memory_copy_changed_u32: every count to 80 words and rows of 127 to
        1000, differences none, one at each place and everywhere; the answer
        against chunks counted from the front, mirror equal to from after,
        into holding from over every chunk that differed and nothing but
        from or its own words elsewhere, nothing before or after any run
        touched, the ends of from, mirror and into against pages that fault,
        and into as mirror itself; on x86_64 with AVX-512, AVX2 and SSE2,
        on riscv64 with every call the first in a process, which asks for V,
        and without V.
*/
static fn check_copy_changed()
{
        static const positive wide[] = {127, 128, 129, 255, 256, 257, 300, 1000};
        p32 address_to pages = memory(4 * 4096 * 3);
        p8 guarded = pages != null;

        for (positive at = 0; guarded && at < 3; at++)
                guarded = system_call_3(syscall(mprotect), (positive)((p8 address_to)pages + (at * 4 + 3) * 4096), 4096, 0) == 0;
        check("copy changed: guard pages", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
#elif RISCV64
        cpu_has_vector = 0;
        memory_copy_changed_u32(pages, pages + 4096, pages + 8192, 0);
        p8 vector = cpu_has_vector;
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#endif
                p8 answers = true, mirrored = true, into_right = true, outside = true, same = true;

                for (positive w = 0; w <= 80 + sizeof(wide) / sizeof(wide[0]) - 1; w++)
                {
                        positive count = w <= 80 ? w : wide[w - 81];

                        for (b64 place = -2; place < (b64)count; place++)
                        {
                                p32 address_to ends[3];

                                for (positive at = 0; at < 3; at++)
                                        ends[at] = (p32 address_to)((p8 address_to)pages + (at * 4 + 3) * 4096);
                                p32 address_to into = ends[0] - count, address_to mirror = ends[1] - count;
                                p32 address_to from = ends[2] - count;
                                positive want = 0;

                                for (positive i = 0; i < count + 16; i++)
                                {
                                        into[(b64)i - 16] = 0xdead0000u + (p32)i;
                                        mirror[(b64)i - 16] = 0x51000000u + (p32)i * 7;
                                        from[(b64)i - 16] = 0x51000000u + (p32)i * 7;
                                }
                                // place -2: nothing differs; -1: everything; else the one word there.
                                for (positive i = 0; i < count; i++)
                                        if (place == -1 || (b64)i == place)
                                                from[i] ^= 0x00010000u;
                                for (positive chunk = 0; chunk < count; chunk += 16)
                                {
                                        p8 differs = false;

                                        for (positive i = chunk; i < count && i < chunk + 16; i++)
                                                differs = differs || from[i] != mirror[i];
                                        want += differs;
                                }
                                p32 before[16];

                                for (positive i = 0; i < 16; i++)
                                        before[i] = mirror[(b64)i - 16];
                                p32 kept[1000];

                                for (positive i = 0; i < count; i++)
                                        kept[i] = mirror[i];
                                VECTOR_FIRST_CALL(tier == 0);
                                answers = answers && memory_copy_changed_u32(into, mirror, from, count) == want;
                                for (positive i = 0; i < count; i++)
                                {
                                        positive chunk = i / 16 * 16;
                                        p8 differed = false;

                                        for (positive j = chunk; j < count && j < chunk + 16; j++)
                                                differed = differed || kept[j] != from[j];
                                        mirrored = mirrored && mirror[i] == from[i];
                                        into_right = into_right && (differed ? into[i] == from[i]
                                                                             : into[i] == from[i] || into[i] == 0xdead0000u + (p32)i + 16);
                                        // A chunk that did not differ is not written, but for the last 16 words, which
                                        // the chunk of the count's rest may cover again.
                                        if (!differed && (count < 16 || i + 16 < count))
                                                into_right = into_right && into[i] == 0xdead0000u + (p32)i + 16;
                                }
                                for (positive i = 0; i < 16; i++)
                                        outside = outside && into[(b64)i - 16] == 0xdead0000u + (p32)i &&
                                                  mirror[(b64)i - 16] == before[i] && from[(b64)i - 16] == 0x51000000u + (p32)i * 7;
                        }

                        // into as mirror itself: one copy, the same answer.
                        {
                                p32 address_to mirror = (p32 address_to)((p8 address_to)pages + 7 * 4096) - count;
                                p32 address_to from = (p32 address_to)((p8 address_to)pages + 11 * 4096) - count;
                                positive want = 0;

                                for (positive i = 0; i < count; i++)
                                        mirror[i] = 3, from[i] = i % 37 ? 3 : 4;
                                for (positive chunk = 0; chunk < count; chunk += 16)
                                {
                                        p8 differs = false;

                                        for (positive i = chunk; i < count && i < chunk + 16; i++)
                                                differs = differs || i % 37 == 0;
                                        want += differs;
                                }
                                VECTOR_FIRST_CALL(tier == 0);
                                same = same && memory_copy_changed_u32(mirror, mirror, from, count) == want;
                                for (positive i = 0; i < count; i++)
                                        same = same && mirror[i] == from[i];
                        }
                }
                check("copy changed: the chunks it answers", answers);
                check("copy changed: mirror is from after", mirrored);
                check("copy changed: into is from where a chunk differed and nothing else", into_right);
                check("copy changed: nothing before a run touched", outside);
                check("copy changed: into as mirror", same);
#if X64
        }
        cpu_has_avx512 = wide512;
        cpu_has_avx2 = wide2;
#elif RISCV64
        }
        cpu_has_vector = vector;
#endif
}

/*
        memory_first_differs_u32 against the loop it replaces in ui_run_in: every count from 0 to 200, and for each
        count every place of the first word that is not the value, and none; the words after that place all
        different or all the value again, and the differing word different in one bit, the bit walked over every
        lane, so each lane of each vector is the answer once. The answer is the index of that word, or the count.
        The words end against a guard page and a zero count points at the guard itself, so a word read past the
        count faults. On x86_64 with AVX2 and without, on riscv64 with V and without, and on arm64.
*/
static fn check_first_differs(void)
{
        static const unsigned int values[] = {0xffffffu, 0u, 0x80000000u, 0x00ff00ffu};
        unsigned int address_to pages = (unsigned int address_to)memory(2 * 4096);
        p8 guarded = pages != null;

        if (guarded)
                guarded = system_call_3(syscall(mprotect), (positive)((p8 address_to)pages + 4096), 4096, 0) == 0;
        check("first differs: guard page", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide2 = cpu_has_avx2;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier == 0 ? wide2 : 0;
#elif RISCV64
        cpu_has_vector = 0;
        memory_first_differs_u32(pages + 1024, 0, 0);
        p8 vector = cpu_has_vector;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                p8 answers = true;
                unsigned int address_to ends = pages + 1024;

                for (positive count = 0; count <= 200; count++)
                        for (positive v = 0; v < 4; v++)
                                for (b64 place = -1; place < (b64)count; place++)
                                        for (positive shape = 0; shape < 2; shape++)
                                        {
                                                unsigned int address_to words = ends - count;
                                                unsigned int value = values[v];
                                                positive want = count;

                                                for (positive i = 0; i < count; i++)
                                                {
                                                        if (place >= 0 && (b64)i == place)
                                                                words[i] = value ^ (1u << (place % 32));
                                                        else if (place >= 0 && (b64)i > place)
                                                                words[i] = shape ? value : value ^ 0x40000000u;
                                                        else
                                                                words[i] = value;
                                                }
                                                for (positive i = 0; i < count && want == count; i++)
                                                        if (words[i] != value)
                                                                want = i;
                                                VECTOR_FIRST_CALL(tier == 0);
                                                answers = answers && memory_first_differs_u32(words, count, value) == want;
                                        }
                check("first differs: the first word that is not the value, or the count", answers);
#if X64
        }
        cpu_has_avx2 = wide2;
#elif RISCV64
        }
        cpu_has_vector = vector;
#else
        }
#endif
}

/*
        memory_over_tone_u32 against ui.c's ui_over, one tone over a run: every alpha from 0 to 255 with colours at most
        their alpha (premultiplied) and at any value, runs of 0 to 1000 pixels, the run ending at a page that faults and the
        words before it held to a canary; each x86_64 tier (SSE2, AVX2, AVX-512) and the first arm64 and riscv64 call.
*/
static unsigned int over_tone_seed = 0x2545f491u;

static unsigned int over_tone_next(void)
{
        over_tone_seed = over_tone_seed * 1664525u + 1013904223u;
        return over_tone_seed >> 8;
}

static p32 over_tone_reference(p32 tone, p32 under)
{
        p32 rest = 255 - (tone >> 24), out = 0;

        if (!(tone >> 24))
                return under;
        for (int shift = 0; shift < 32; shift += 8)
        {
                p32 x = (under >> shift & 255) * rest + 127;
                p32 channel = (tone >> shift & 255) + ((x + 1 + (x >> 8)) >> 8);

                out |= (channel > 255 ? 255 : channel) << shift;
        }
        return out;
}

static fn check_over_tone()
{
        static const positive counts[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 1000};
        p32 address_to into = memory(4 * 4096);
        p8 guarded = into && system_call_3(syscall(mprotect), (positive)((p8 address_to)into + 3 * 4096), 4096, 0) == 0;

        check("over tone: a guard page", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide_on = cpu_has_avx512, narrow_on = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 2 ? wide_on : 0;
                cpu_has_avx2 = tier >= 1 ? narrow_on : 0;
#endif
                p8 fine = true;
                for (p32 alpha = 0; alpha < 256; alpha++)
                        for (p32 trial = 0; trial < 6; trial++)
                                for (positive k = 0; k < sizeof counts / sizeof counts[0]; k++)
                                {
                                        p32 colour = 0;
                                        p32 address_to to = (p32 address_to)((p8 address_to)into + 3 * 4096) - counts[k];
                                        positive count = counts[k];
                                        p32 tone;

                                        for (int c = 0; c < 3; c++)
                                        {
                                                p32 channel = trial == 2 ? 255 : trial == 3 ? 0 : over_tone_next() & 255;

                                                if (trial < 2 && channel > alpha)
                                                        channel = over_tone_next() % (alpha + 1);
                                                colour = colour << 8 | channel;
                                        }
                                        tone = alpha << 24 | colour;
                                        for (positive i = 0; i < count + 16; i++)
                                                to[(b64)i - 16] = i < 16 ? 0xdeadbeef : over_tone_next();
                                        {
                                                p32 want[1100];

                                                for (positive i = 0; i < count; i++)
                                                        want[i] = over_tone_reference(tone, to[i]);
                                                memory_over_tone_u32(to, count, tone);
                                                for (positive i = 0; i < count; i++)
                                                        fine = fine && to[i] == want[i];
                                                for (positive i = 0; i < 16; i++)
                                                        fine = fine && to[(b64)i - 16] == 0xdeadbeef;
                                        }
                                }
                check("over tone: every word of the run, nothing before it", fine);
#if X64
        }
        cpu_has_avx512 = wide_on;
        cpu_has_avx2 = narrow_on;
#endif
}

/*
        memory_sweep_coverage against the loop it replaces in font_raster:
        every count from 0 to 80 and runs of 127 to 1000, cells sparse as an
        outline leaves them and dense, sums that run past full both ways and
        to INT_MIN, the cells' last word and the bytes' last against pages
        that fault, the bytes before and after held to a canary, the cells
        past the count untouched, every cell in the count cleared, and the
        sum it answers. On x86_64 with AVX-512, with AVX2 alone and with
        neither; on riscv64 every call is the first in a process, which asks
        for V itself.
*/
static p32 sweep_level(p32 sum)
{
        p32 area = (b32)sum < 0 ? -sum : sum;

        area = area > 0x200000 ? 0x200000 : area;
        return (area * 255 + 0x100000) >> 21;
}

static fn check_sweep()
{
        static const positive long_runs[] = {127, 128, 129, 255, 256, 257, 640, 1000};
        p8 address_to bytes = memory(2 * 4096);
        p32 address_to cells = memory(2 * 4096);
        static p32 want_cells[1100];
        static p8 want_bytes[1100];
        p64 state = 0x5d1c3a9e7f20b468ull;
        p8 guarded = bytes && cells &&
                       system_call_3(syscall(mprotect), (positive)(bytes + 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)((p8 address_to)cells + 4096), 4096, 0) == 0;

        check("sweep: guard pages", guarded);
        if (!guarded)
                return;
#if RISCV64
        cpu_has_vector = 0;
        cells[1020] = 0x100000, cells[1021] = 0x100000, cells[1022] = -0x200000, cells[1023] = 7;
        p32 first = memory_sweep_coverage(bytes + 4092, cells + 1020, 4, 3);
        check("sweep: the first call asks for V and keeps its count and sum",
              first == 10 && bytes[4092] == 128 && bytes[4093] == 255 && bytes[4094] == 0 && bytes[4095] == 0 &&
                  !cells[1020] && !cells[1023] && cpu_has_vector != 0);
#endif
#if X64
        p8 wide = cpu_has_avx512, two = cpu_has_avx2;
        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier ? 0 : wide;
                cpu_has_avx2 = tier == 2 ? 0 : two;
#endif
                p8 fine = true;
                for (positive k = 0; k <= 80 + sizeof(long_runs) / sizeof(long_runs[0]) - 1; k++)
                        for (p32 shape = 0; shape < 4; shape++)
                        {
                                //      Even shapes end against the pages that fault,
                                //      odd ones keep three words and bytes after.
                                positive count = k <= 80 ? k : long_runs[k - 81], pad = shape & 1 ? 3 : 0;
                                p32 address_to run = cells + 1024 - count - pad;
                                p8 address_to into = bytes + 4096 - count - pad;
                                p32 sum = shape == 3 ? 0x80000000u - 5 : (p32)(state >> 40) - (1u << 23);

                                //      Sparse as an outline's cells, dense, saturating,
                                //      and wrapping through INT_MIN.
                                for (positive i = 0; i < count + pad; i++)
                                {
                                        XORSHIFT64(state);
                                        b32 value = (b32)(state >> 32) >> (shape == 0 ? 9 : shape == 1 ? 11 : 6);
                                        run[i] = shape == 0 && state % 5 ? 0 : shape == 3 && i < count ? (i & 1 ? 3 : 1)
                                                                                                     : (p32)value;
                                }
                                memory_fill(into - 3, 0xa5, count + pad + 3);
                                p32 expected = sum;
                                for (positive i = 0; i < count; i++)
                                {
                                        expected += run[i];
                                        want_bytes[i] = (p8)sweep_level(expected);
                                }
                                for (positive i = 0; i < count + pad; i++)
                                        want_cells[i] = i < count ? 0 : run[i];

                                VECTOR_FIRST_CALL(true);
                                p32 answered = memory_sweep_coverage(into, run, count, sum);

                                fine = fine && answered == expected && !memory_compare(into, want_bytes, count) &&
                                       !memory_compare(run, want_cells, (count + pad) * 4);
                                for (positive i = 0; i < 3; i++)
                                        fine = fine && (into - 3)[i] == 0xa5 && (i >= pad || into[count + i] == 0xa5);
                        }
                check("sweep: every byte its running sum's level, the cells cleared, nothing else touched", fine);
#if X64
        }
        cpu_has_avx512 = wide, cpu_has_avx2 = two;
#endif
}

/*
        Lines of the faces this tree ships, as the taskbar and the windows
        write them: Inter alone, and Inter with IBM Plex Mono behind it for
        what Inter has not. Kerned pairs, Latin-1, drawn cells, a code point
        neither face has and a byte that is no UTF-8; at the tabs' 11 px and
        at 13, 16.5 and 24 px; from pens a quarter of a pixel apart; dark on
        light (darkened), light on dark, a colour on a colour, and white on a
        ground not known (a ramp, blended pixel by pixel).
*/
#define LINE_W 560
#define LINE_H 40
#define LINE_BASELINE 29

static p8 line_inter[1 << 19], line_plex[1 << 18];
static font_face line_faces[2];
static p32 line_pixels[LINE_W * LINE_H];
static const string_address line_texts[] = {
    "Moonwater: the quick brown fox jumps over the lazy dog, 0123456789",
    "AVATAR To Ty Wa ffi T.V. \xc3\xa5\xc3\xa4\xc3\xb6 \xc3\x85\xc3\x84\xc3\x96 \xc3\xa9",
    "\xe2\x94\x80\xe2\x94\xbc\xe2\x95\xad\xe2\xa3\xbf\xe2\x96\x92 shell \xe4\xb8\xad \xff end",
};
static const p32 line_sizes[] = {11 * 64, 13 * 64, 1056, 24 * 64};
static const p32 line_inks[][2] = {{0x101010, 0xf0f0f0}, {0xe4e4e4, 0x2f2f33}, {0x3366ff, 0xffee88}, {0xffffff, FONT_UNKNOWN}};

static p8 line_load(void)
{
        bipolar inter = file_slurp_regular_at(AT_FDCWD, "fonts/inter/Inter-Regular.ttf", line_inter, sizeof(line_inter), 0);
        bipolar plex = file_slurp_regular_at(AT_FDCWD, "fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf", line_plex,
                                             sizeof(line_plex), 0);

        return inter > 0 && plex > 0 && font_load(&line_faces[0], line_inter, (positive)inter, 0) == 0 &&
               font_load(&line_faces[1], line_plex, (positive)plex, 0) == 0;
}

//      The ground a line is drawn on, width by height: its colour, or for a ground not known a ramp.
static fn line_ground(p32 address_to pixels, b32 width, b32 height, p32 ground)
{
        for (b32 y = 0; y < height; y++)
                for (b32 x = 0; x < width; x++)
                        pixels[y * width + x] = ground & FONT_UNKNOWN ? (p32)(x * 0x010203 + y * 0x030100) & 0xffffff : ground;
}

static p64 line_hash(p64 hash, const p32 address_to pixels, positive count)
{
        for (positive at = 0; at < count; at++)
                for (p32 shift = 0; shift < 32; shift += 8)
                        hash = (hash ^ (pixels[at] >> shift & 255)) * 0x100000001b3ull;
        return hash;
}

/*
        Upright text is the bytes it was before text could be turned: every
        line above, hashed, against the hash the engine made then, the same
        on all three machines; made again when dots were set on the
        stems under them, when a counter between two stems on a tie
        was held to within three quarters of a pixel of the font's, and when
        ascender fitting, top-bar thickness preservation and stem/bar weight
        uniformity were fixed.
*/
#define LINE_UPRIGHT 8821402309694946517ull

static fn check_upright(void)
{
        static font_engine e;
        static p8 store[256 * 256];
        p64 hash = 0xcbf29ce484222325ull;

        if (!line_load())
        {
                check("upright: the faces this tree ships load", false);
                return;
        }
        font_start(&e, store, 256, 256);
        for (p32 chain = 1; chain <= 2; chain++)
                for (p32 text = 0; text < sizeof(line_texts) / sizeof(line_texts[0]); text++)
                        for (p32 size = 0; size < sizeof(line_sizes) / sizeof(line_sizes[0]); size++)
                                for (p32 ink = 0; ink < sizeof(line_inks) / sizeof(line_inks[0]); ink++)
                                        for (p32 pen = 0; pen < 4; pen++)
                                        {
                                                const font_face address_to faces[2] = {&line_faces[0], &line_faces[1]};
                                                font_target target = {line_pixels, LINE_W, LINE_W, LINE_H};

                                                line_ground(line_pixels, LINE_W, LINE_H, line_inks[ink][1]);
                                                font_draw(&e, faces, chain, (const p8 address_to)line_texts[text],
                                                          string_length(line_texts[text]), line_sizes[size], 0, &target,
                                                          (b32)(3 << 16) + (b32)pen * 0x4000, LINE_BASELINE,
                                                          line_inks[ink][0], line_inks[ink][1]);
                                                hash = line_hash(hash, line_pixels, LINE_W * LINE_H);
                                        }
        if (hash != LINE_UPRIGHT)
                string_format(log, "  upright text hashes to %p\n", (positive)hash);
        check("upright: text is the bytes it was before text could be turned", hash == LINE_UPRIGHT);
}

/*
        Turned text is upright text turned, pixel for pixel. Each run is drawn
        upright on a width by height picture and turned on a height by width
        one whose ground is the upright ground turned the same way; the turned
        picture must be the upright one turned a quarter clockwise with its
        tops right and anticlockwise with them left, every byte, and the two
        advances and font_extent's one number. The lines above, a title cut
        to fit with its dots, and each run also cut: begun left of the
        picture and running off its end, its tops cut off, its descenders and
        half its bodies cut off. Then one glyph so large that its turn takes
        the strip more than once.
*/
static p32 turn_upright[LINE_W * LINE_H], turn_turned[LINE_W * LINE_H], turn_want[LINE_W * LINE_H];

//      The picture width by height turned onto into (height by width): clockwise, or anticlockwise for left.
static fn turn_picture(const p32 address_to from, b32 width, b32 height, p32 address_to into, p8 left)
{
        for (b32 y = 0; y < width; y++)
                for (b32 x = 0; x < height; x++)
                        into[y * height + x] = left ? from[x * width + (width - 1 - y)] : from[(height - 1 - x) * width + y];
}

//      One run both ways on width by height; answers whether both turns are the upright run turned.
static p8 turn_run(font_engine address_to e, const font_face address_to const address_to faces, p32 count,
                     string_address text, p32 size, b32 x, b32 baseline, p32 colour, p32 background, b32 width,
                     b32 height, p32 address_to upright, p32 address_to turned, p32 address_to want)
{
        font_target flat = {upright, (positive)width, width, height}, stood = {turned, (positive)height, height, width};
        positive length = string_length(text);
        p8 same = true;

        line_ground(upright, width, height, background);
        b64 advance = font_draw(e, faces, count, (const p8 address_to)text, length, size, 0, &flat, x, baseline, colour,
                                background);
        same = advance == font_extent(e, faces, count, (const p8 address_to)text, length, size, 0);
        for (p32 way = 0; way < 2; way++)
        {
                line_ground(want, width, height, background);
                turn_picture(want, width, height, turned, way);
                same = same && font_draw_turned(e, faces, count, (const p8 address_to)text, length, size, 0,
                                                way ? FONT_TOPS_LEFT : FONT_TOPS_RIGHT, &stood, x, baseline, colour,
                                                background) == advance;
                turn_picture(upright, width, height, want, way);
                same = same && !memory_compare(turned, want, (positive)width * (positive)height * 4);
        }
        return same;
}

static fn check_turned(void)
{
        static font_engine e;
        static p8 store[1024 * 1024];
        static p32 big_upright[720 * 640], big_turned[720 * 640], big_want[720 * 640];
        static const string_address cut = "Settings \xe2\x80\x94 Display and t...";
        static const b32 cuts[4][2] = {{(b32)(3 << 16), LINE_BASELINE}, {-(b32)(5 << 16) - 0x4000, LINE_BASELINE},
                                       {(b32)(3 << 16) + 0x2000, 6}, {(b32)(3 << 16) + 0x6000, LINE_H + 5}};
        const font_face address_to faces[2] = {&line_faces[0], &line_faces[1]};
        positive runs = 0, wrong = 0;
        p32 face, glyph;

        if (!line_faces[0].data || !line_faces[1].data)
        {
                check("turned: the faces this tree ships load", false);
                return;
        }
        font_start(&e, store, 1024, 1024);
        font_find(&e, faces, 1, 0x28ff, &face, &glyph);
        check("turned: a drawn cell is among the lines (braille Inter has not)", face == FONT_DRAWN);
        for (p32 chain = 1; chain <= 2; chain++)
                for (p32 text = 0; text <= sizeof(line_texts) / sizeof(line_texts[0]); text++)
                        for (p32 size = 0; size < sizeof(line_sizes) / sizeof(line_sizes[0]); size++)
                                for (p32 ink = 0; ink < sizeof(line_inks) / sizeof(line_inks[0]); ink++)
                                        for (p32 at = 0; at < 4; at++)
                                        {
                                                string_address run = text < sizeof(line_texts) / sizeof(line_texts[0])
                                                                         ? line_texts[text] : cut;

                                                runs++;
                                                wrong += !turn_run(&e, faces, chain, run, line_sizes[size], cuts[at][0],
                                                                   cuts[at][1], line_inks[ink][0], line_inks[ink][1],
                                                                   LINE_W, LINE_H, turn_upright, turn_turned, turn_want);
                                        }
        if (wrong)
                string_format(log, "  %p of %p turned runs are not the upright run turned\n", wrong, runs);
        check("turned: every run, cut or whole, both ways, is the upright run turned, byte for byte", !wrong && runs > 0);
        check("turned: a glyph past the strip, turned a band of rows at a time, both ways",
              turn_run(&e, faces, 1, "W", 560 * 64, 8 << 16, 560, 0x101010, 0xf0f0f0, 720, 640, big_upright, big_turned,
                       big_want) &&
                  turn_run(&e, faces, 1, "W", 560 * 64, 8 << 16, 560, 0xffffff, FONT_UNKNOWN, 720, 640, big_upright,
                           big_turned, big_want));
}

/*
        Hostile fonts. The bytes are copied so that the last one sits against
        a page that faults, and each cut and each mutation is loaded, every
        glyph drawn hinted and not at two sizes, every pair of the first
        sixteen kerned, its metrics taken and a line laid out and drawn.
        Surviving is the check; what the engine answers is not judged.
*/
static p8 address_to guarded;
static positive hostile_cases, hostile_loaded;

static fn exercise(const p8 address_to bytes, positive size)
{
        p8 address_to at = guarded + 8 * 4096 - size;
        font_face face;

        memory_copy(at, bytes, size);
        hostile_cases++;
        if (font_load(&face, at, size, 0))
                return;
        hostile_loaded++;

        const font_face address_to chain[1] = {&face};
        font_target target = {canvas + 8 * 96 + 8, 96, 80, 80};
        font_metrics grid = font_metrics_at(&face, 13 * 64);
        font_bitmap box;
        p32 glyphs = face.glyphs < 24 ? face.glyphs : 24;

        for (p32 g = 0; g < glyphs; g++)
        {
                font_render(&engine, &face, g, 13 * 64, g & 7, g & 1 ? FONT_UNHINTED : FONT_DARKEN,
                            coverage, 256, sizeof(coverage), &box);
                font_render(&engine, &face, g, 97 * 64, 3, 0, coverage, 256, sizeof(coverage), &box);
        }
        for (p32 left = 0; left < 16; left++)
                for (p32 right = 0; right < 16; right++)
                        font_kerning(&face, left, right, 13 * 64);
        for (p32 code = 0x20; code < 0x80; code += 7)
                font_glyph(&face, code);
        font_glyph(&face, 0x1f600);
        font_draw(&engine, chain, 1, (const p8 address_to)"AB x\xe2\x94\x80", 7, 13 * 64, 0, &target, 0, 20,
                  0xffffff, 0x101820);
        font_cell(&engine, chain, 1, 'A', 1, &grid, 13 * 64, 0, &target, 4, 4, 0xffffff, 0x101820);
}

static p64 hostile_state = 0x2545f4914f6cdd1dull;

static p64 hostile_next(p64 below)
{
        XORSHIFT64(hostile_state);
        return below ? hostile_state % below : 0;
}

static fn check_hostile(const p8 address_to font, positive size, p32 mutations)
{
        static p8 copy[8192];
        positive before = hostile_cases, loaded = hostile_loaded;

        for (positive cut = 0; cut <= size; cut++)
                exercise(font, cut);

        static const p32 extremes[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0xffffffff, 0x10000};
        for (p32 m = 0; m < mutations; m++)
        {
                memory_copy(copy, font, size);
                p32 changes = 1 + (p32)hostile_next(4);
                for (p32 k = 0; k < changes; k++)
                {
                        positive at = hostile_next(size);

                        if (hostile_next(2))
                                copy[at] = (p8)hostile_next(256);
                        else
                        {
                                //      A field at an even offset set to an extreme.
                                p32 value = extremes[hostile_next(8)], wide = hostile_next(2) ? 4 : 2;

                                at &= ~(positive)1;
                                for (p32 b = 0; b < wide && at + b < size; b++)
                                        copy[at + b] = (p8)(value >> (8 * (wide - 1 - b)));
                        }
                }
                exercise(copy, size);
        }
        check("hostile: every cut and mutation survived against a guard page",
              hostile_cases == before + size + 1 + mutations);
        //      Surviving a font that never loads is no check: once the face ids ran out (check_edges loads until the
        //      load is refused), every one of these was refused at its first line and nothing was drawn.
        check("hostile: most mutations still load and are drawn", hostile_loaded - loaded > mutations / 2);
}

//      The stack the next call lands on, written over with the words most likely to overflow the difference of two
//      of them: in every phase of four words, words two apart are the least and the greatest there are.
static __attribute__((noinline)) fn font_check_scribble(void)
{
        volatile b64 junk[8192];

        for (positive at = 0; at < 8192; at++)
                junk[at] = at % 4 < 2 ? (b64)((p64)1 << 63) : (b64)(~(p64)0 >> 1);
}

/*
        Kerning's walks are bounded by work, not by what the file claims. A GPOS whose default language lists 65535
        features, every one the kern feature, of 65535 lookups each was 4.3 billion reads in font_load (seconds for a
        file of 300 KB; the fuzzer met 2.8 s); a kern table that claims 65535 subtables of six bytes was walked to its
        end for every pair. The faces are the test fonts with one table pointed at a copy built after them.
*/
static p8 kern_hostile[8192 + (400u << 10)];

static fn kern_put16(p8 address_to at, p32 value)
{
        at[0] = (p8)(value >> 8), at[1] = (p8)value;
}

//      The directory record of tag in a test font, pointed at length bytes at offset.
static fn kern_point(p8 address_to font, p32 tag, positive offset, positive length)
{
        positive tables = (positive)font[4] << 8 | font[5];

        for (positive i = 0; i < tables; i++)
        {
                p8 address_to record = font + 12 + 16 * i;

                if (((p32)record[0] << 24 | (p32)record[1] << 16 | (p32)record[2] << 8 | record[3]) == tag)
                {
                        kern_put16(record + 8, (p32)(offset >> 16)), kern_put16(record + 10, (p32)offset & 0xffff);
                        kern_put16(record + 12, (p32)(length >> 16)), kern_put16(record + 14, (p32)length & 0xffff);
                }
        }
}

static fn check_kern_walks(void)
{
        positive base = (font_one_size + 3) & ~(positive)3, started, took;
        p8 address_to g = kern_hostile + base;
        font_face face;
        b32 loaded;

        memory_zero(kern_hostile, sizeof(kern_hostile));
        memory_copy(kern_hostile, font_one, font_one_size);
        //      GPOS 1.0: the script list at 65000, the feature list at 10, the lookup list at 10.
        kern_put16(g, 1), kern_put16(g + 2, 0), kern_put16(g + 4, 65000), kern_put16(g + 6, 10), kern_put16(g + 8, 10);
        //      One feature, kern, its table at 18: 65535 lookups (lookup 0, but where the script list lies over them).
        kern_put16(g + 10, 1), memory_copy(g + 12, "kern", 4), kern_put16(g + 16, 8);
        kern_put16(g + 18, 0), kern_put16(g + 20, 0xffff);
        //      DFLT, its script at 130000, whose default language is at 132000: 65535 features, every one feature 0.
        kern_put16(g + 65000, 1), memory_copy(g + 65002, "DFLT", 4), kern_put16(g + 65006, 65000);
        kern_put16(g + 130000, 2000), kern_put16(g + 130002, 0);
        kern_put16(g + 132000, 0), kern_put16(g + 132002, 0xffff), kern_put16(g + 132004, 0xffff);
        kern_point(kern_hostile, FONT_TAG('G', 'P', 'O', 'S'), base, 132006 + 2 * 65535);
        started = clock_monotonic_nanoseconds();
        loaded = font_load(&face, kern_hostile, base + 132006 + 2 * 65535, 0) == 0;
        took = clock_monotonic_nanoseconds() - started;
        string_format(log, "  kerning: a GPOS of 65535 kern features of 65535 lookups loaded in %p us\n", took / 1000);
        check("kerning: a GPOS of 65535 kern features of 65535 lookups each loads in well under 50 ms",
              loaded && took < 50000000 && face.lookup_count >= 1);
        font_kerning(&face, 1, 1, 13 * 64);

        //      The kern table: 65535 subtables of six bytes, none of them horizontal kerning, walked for every pair.
        base = (font_two_size + 3) & ~(positive)3;
        g = kern_hostile + base;
        memory_zero(kern_hostile, sizeof(kern_hostile));
        memory_copy(kern_hostile, font_two, font_two_size);
        kern_put16(g, 0), kern_put16(g + 2, 0xffff);
        for (positive t = 0; t < 0xffff; t++)
                kern_put16(g + 4 + 6 * t, 0), kern_put16(g + 6 + 6 * t, 6), kern_put16(g + 8 + 6 * t, 2);
        kern_point(kern_hostile, FONT_TAG('k', 'e', 'r', 'n'), base, 4 + 6 * 0xffff);
        loaded = font_load(&face, kern_hostile, base + 4 + 6 * 0xffff, 0) == 0;
        started = clock_monotonic_nanoseconds();
        for (p32 pair = 0; pair < 20000 && loaded; pair++)
                font_kerning(&face, pair & 7, pair >> 3 & 7, 13 * 64);
        took = clock_monotonic_nanoseconds() - started;
        string_format(log, "  kerning: 20000 pairs through a kern table of 65535 subtables in %p us\n", took / 1000);
        check("kerning: a kern table of 65535 subtables walks a ceiling of them a pair, 20000 pairs in well under a quarter second",
              loaded && face.kern && !face.lookup_count && took < 250000000);
}

//      The dot pass alone, in a frame of its own at the depth the scribble wrote.
static __attribute__((noinline)) fn font_check_dots(font_engine address_to e, const b64 address_to sizes, const p32 address_to inner)
{
        static const b64 kept[4] = {0}, lines[3] = {0};

        font_dots(e, sizes, inner, kept, 0, lines, 0);
}

/*
        Arithmetic a hostile face, or a stack, steers. The dot pass read the box of every contour, a stem's never
        written, before it asked whether the contour was a dot, and the difference of two such words overflowed: it
        is run here alone straight after the stack is written with extremes, a stem and a dot, and the trap build
        (MW_UBSAN=1) stopped there; a plain build answers the same either way. Then a dotted letter, hinted.
*/
//      A row of cells laid by font_cell_run is the plans font_cell_plan gives its cells one by one: every field, and the
//      coverage's place and the tones, where a plan translates. The run goes first, so both read the same cached boxes.
static fn check_cell_run(font_face address_to two)
{
        static p32 pixels_a[256 * 64], pixels_b[256 * 64];
        static p32 codes[12] = {'A', 'g', '@', ' ', 0x2500, 0x2588, 'W', '.', 0x2805, 0xe9, '!', 0x25a0};
        static font_cell_plan_t runs[12], singles[12];
        font_target one_target = {pixels_a, 256, 256, 64}, other = {pixels_b, 256, 256, 64};
        const font_face address_to chain[1] = {two};
        font_metrics grid = font_metrics_at(two, 18 * 64);
        b32 width = grid.cell_width;
        b32 same = 1, translated = 0;

        font_cell_run(&engine, chain, 1, codes, 12, 1, &grid, 18 * 64, 0, &one_target, 3, 5, 0xdfe7ef, 0x303030, runs);
        for (p32 i = 0; i < 12; i++)
        {
                font_cell_plan(&engine, chain, 1, codes[i], 1, &grid, 18 * 64, 0, &other, 3 + (b32)i * width, 5, 0xdfe7ef, 0x303030,
                               &singles[i]);
                same = same && runs[i].left == singles[i].left && runs[i].right == singles[i].right &&
                       runs[i].top == singles[i].top && runs[i].bottom == singles[i].bottom && runs[i].from == singles[i].from &&
                       runs[i].to == singles[i].to && runs[i].above == singles[i].above && runs[i].below == singles[i].below &&
                       runs[i].translate == singles[i].translate && runs[i].empty == singles[i].empty &&
                       runs[i].strength == singles[i].strength && runs[i].ground == singles[i].ground;
                if (singles[i].translate)
                {
                        translated++;
                        same = same && runs[i].coverage == singles[i].coverage && runs[i].stride == singles[i].stride &&
                               memory_compare(runs[i].tones, singles[i].tones, 256 * sizeof(p32)) == 0;
                }
        }
        check("cell run: the plans of a row are the plans of its cells, one by one, and some of them translate",
              same && translated >= 8);
}

//      Big-endian 16-bit store for a hand-built table.
static fn reverse_put16(p8 address_to at, p32 value)
{
        at[0] = (p8)(value >> 8), at[1] = (p8)value;
}

//      Two overlapping rectangles (two contours, stems that cross: 0 to 10 px and 5 to 15 px) snapped on the x axis keep
//      their order: a point left of another in the outline is not right of it after the snap. Without the fix the stems'
//      map is not monotone, and points cross.
static fn check_crossing_stems(void)
{
        //      The stems 410 to 2458 and 1331 to 3379 (1024ths of a pixel) cross. The point at 1800 is between the second's
        //      left edge and the first's right edge, so it maps along the segment from (1331, 1024) to (2458, 2048) in the
        //      edges sorted by where they were: 1450. Taken in stem order the segment is (410, 410) to (2458, 2048): 1521.
        static const b64 xs[9] = {410, 2458, 2458, 410, 1331, 3379, 3379, 1331, 1800};
        static const b64 ys[9] = {0, 0, 20480, 20480, 0, 0, 20480, 20480, 10240};
        p8 monotone = true;
        const b64 want = 1450;

        engine.point_count = 9, engine.contour_count = 3, engine.still = 0, engine.snap_widest = 1024;
        engine.contour[0].last = 3, engine.contour[1].last = 7, engine.contour[2].last = 8;
        for (p32 i = 0; i < 9; i++)
                engine.points[i] = (font_point){xs[i], ys[i], 1};
        font_snap_axis(&engine, 1434, 0, 2048, 3072, null, 0, 768, false, null, null, 0);
        for (p32 i = 0; i < 9; i++)
                for (p32 j = 0; j < 9; j++)
                        monotone = monotone && (xs[i] >= xs[j] || engine.points[i].x <= engine.points[j].x || xs[i] == xs[j]);
        check("snap: overlapping contours' stems keep the outline's order on the x axis", monotone);
        check("snap: a point between crossing stems' edges maps along the sorted segment",
              engine.points[8].x >= want - 2 && engine.points[8].x <= want + 2);
}

//      A multiple substitution offered to a string that is not being output (the reverse branch's) is refused and
//      leaves the glyph it reads alone. Without the refusal it wrote its first glyph into info[0] in place.
static fn check_reverse_multiple(void)
{
        static p8 table[32];
        static font_item info[4], spare[4];
        static font_shift shift[4];
        font_face fake = {.data = table, .size = 32};
        font_shaper s = {.face = &fake, .info = info, .out = info, .spare = spare, .shift = shift, .cap = 4, .len = 1};
        b32 answer;

        //      Multiple 1: coverage at 10 (glyph 5), sequence at 16 (glyphs 7 and 8).
        reverse_put16(table, 1), reverse_put16(table + 2, 10), reverse_put16(table + 4, 1), reverse_put16(table + 6, 16);
        reverse_put16(table + 10, 1), reverse_put16(table + 12, 1), reverse_put16(table + 14, 5);
        reverse_put16(table + 16, 2), reverse_put16(table + 18, 7), reverse_put16(table + 20, 8);
        info[0] = (font_item){.glyph = 5};
        answer = font_gsub_apply(&s, 2, 0);
        check("reverse: a multiple substitution is refused where the string is not output", !answer);
        check("reverse: the glyph it reads is left alone", info[0].glyph == 5 && !s.output);
}

//      A raster asked for a row past the band's cells returns; without the guard its band loop never advances (the
//      mutant hangs here, and the run times out).
static fn check_raster_ceiling(void)
{
        static p8 scratch[8];

        font_raster(&engine, 70000, 1, scratch, 70002);
        check("raster: a row wider than the band's cells is refused, and the call returns", true);
}

static fn check_hostile_arithmetic(void)
{
        {
                static b64 sizes[2 * FONT_DOTS];
                static p32 inner[FONT_DOTS];
                static const b64 square[8][2] = {{0, 0}, {4096, 0}, {4096, 8192}, {0, 8192},
                                                 {1024, 10240}, {3072, 10240}, {3072, 12288}, {1024, 12288}};
                b32 sane = 1;

                engine.point_count = 8, engine.contour_count = 2;
                engine.contour[0].last = 3, engine.contour[1].last = 7;
                for (p32 i = 0; i < 8; i++)
                        engine.points[i] = (font_point){square[i][0], square[i][1], 1};
                sizes[2] = sizes[3] = 2048;
                font_check_scribble();
                font_check_dots(&engine, sizes, inner);
                for (p32 i = 0; i < 8; i++)
                        sane = sane && engine.points[i].x >= 0 && engine.points[i].x <= 8192 && engine.points[i].y >= 0 &&
                               engine.points[i].y <= 16384;
                check("arithmetic: the dot pass over a stem and a dot, its stack full of extremes, moves only the dot, and sanely",
                      sane && engine.points[0].x == 0 && engine.points[2].y == 8192);
                engine.point_count = engine.contour_count = 0;
        }
        static p8 file[1 << 20];
        bipolar got = file_slurp_regular_at(AT_FDCWD, "fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf", file, sizeof(file), 0);
        font_face face;
        font_bitmap box;
        b32 drawn = -1;

        check("arithmetic: the mono face this tree ships loads", got > 0 && font_load(&face, file, (positive)got, 0) == 0);
        if (got <= 0)
                return;
        for (p32 size = 9 * 64; size <= 15 * 64; size += 64)
        {
                font_check_scribble();
                drawn = font_render(&engine, &face, font_glyph(&face, 'i'), size, 0, FONT_DARKEN, coverage, 256, sizeof(coverage), &box);
                if (drawn != 0 || !box.width)
                        break;
        }
        check("arithmetic: a dotted letter hinted over a stack of extremes is drawn (no difference of words never written)",
              drawn == 0 && box.width > 0);

        //      A face whose M is 65535 units wide in an em of 16 (the advance a cell is cut from): at 64 px that is
        //      262140 px, 2^34 in 16.16, which was wrapped in 32 bits to a cell of one pixel (and of a negative width at
        //      other sizes); it is held at 16384 px, and a cell two of them wide is drawn without a shift past 32 bits.
        {
                font_face wide = face;
                font_metrics grid;
                const font_face address_to chain[1] = {&wide};
                font_target target = {canvas, 96, 96, 96};

                wide.units = 16, wide.em_advance = 0xffff;
                grid = font_metrics_at(&wide, 64 * 64);
                check("arithmetic: a cell as wide as an advance of 65535 units in an em of 16 is held at 16384 px, not wrapped",
                      grid.cell_width == 16384 && font_metrics_at(&wide, 16383 * 64).cell_width == 16384);
                font_cell(&engine, chain, 1, 'A', 2, &grid, 64 * 64, 0, &target, 0, 0, 0xffffff, 0x101820);
                check("arithmetic: an advance past 16384 px is held there too", font_advance(&wide, font_glyph(&wide, 'M'), 16383 * 64) == 1 << 30);
        }
        check_kern_walks();
}

/*
        Edge cases of the cache's keys. A face id is 16 bits, so a process
        loads 65535 faces at most and the load after them is refused, never
        given an id a live face holds: a glyph cached for one face must not
        answer for another. A size from 16384 px up is no
        size the engine draws, and is not one the key keeps the bits of, so a
        huge size must never draw the small glyph whose key it truncates to. A
        size of zero draws nothing and measures a one-pixel cell.
*/
static p8 reissue_atlas[512 * 512], truth_atlas[512 * 512];
static font_engine reissue_engine, truth_engine;
static p32 edge_frames[4][48 * 32];

/*
        One letter at 20 px onto a 48 by 32 frame of background, drawn with
        the engine given. A truth is drawn by an engine started again for it,
        so nothing it drew before can answer for it.
*/
static fn edge_draw(font_engine address_to e, const font_face address_to const address_to chain, p32 code,
                    p32 size, p32 frame)
{
        font_target target = {edge_frames[frame], 48, 48, 32};
        p8 text[1] = {(p8)code};

        for (positive i = 0; i < 48 * 32; i++)
                edge_frames[frame][i] = 0x123456;
        font_draw(e, chain, 1, text, 1, size, 0, &target, 4 << 16, 24, 0xffffff, 0x123456);
}

static fn truth_draw(const font_face address_to face, p32 code, p32 frame)
{
        const font_face address_to chain[1] = {face};

        font_start(&truth_engine, truth_atlas, 512, 512);
        edge_draw(&truth_engine, chain, code, 20 * 64, frame);
}

static fn check_edges(font_face address_to one, font_face address_to two)
{
        font_face again;
        const font_face address_to chain[1] = {one};
        const font_face address_to other[1] = {&again};
        static p32 untouched[48 * 32];
        p32 code = 0;
        positive loads = 0;

        for (positive i = 0; i < 48 * 32; i++)
                untouched[i] = 0x123456;

        //      A letter the two faces draw differently, so the test can tell them apart.
        for (p8 letter = 'A'; letter <= 'D' && !code; letter++)
        {
                truth_draw(one, letter, 2);
                truth_draw(two, letter, 3);
                if (memory_compare(edge_frames[2], edge_frames[3], sizeof(edge_frames[2])) != 0)
                        code = letter;
        }
        check("reissue: some letter of A to D is drawn differently by the two faces", code != 0);

        //      The engine draws the first face, the second face is loaded
        //      until the load is refused, and the engine draws the last face
        //      that loaded with the same letter: it must be that face's letter.
        //      Issued ids were once reused after 65536 loads, which put the
        //      first face's id on a live face.
        font_face next;
        p8 refused = false, shared = false;

        font_start(&reissue_engine, reissue_atlas, 512, 512);
        edge_draw(&reissue_engine, chain, code, 20 * 64, 0);
        do
        {
                refused = font_load(&next, font_two, font_two_size, 0) != 0;
                if (!refused)
                        again = next, shared = shared || next.id == one->id;
                loads++;
        } while (!refused && loads < 140000);
        check("reissue: the load past the 16-bit id space is refused, and no face loaded has the first face's id",
              refused && !shared && again.id != one->id);

        edge_draw(&reissue_engine, other, code, 20 * 64, 1);
        truth_draw(&again, code, 2);
        check("reissue: a glyph cached for one face does not answer for another",
              memory_compare(edge_frames[1], edge_frames[2], sizeof(edge_frames[1])) == 0);

        //      A size whose bits wrap in the key, and one past the engine's
        //      largest, draw nothing; a size of zero draws nothing and measures
        //      a one-pixel cell.
        edge_draw(&engine, chain, 'A', 30 * 64, 0);
        edge_draw(&engine, chain, 'A', 1 << 22 | 30 * 64, 1);
        check("size: 65536 px, where the key's bits wrap, draws nothing",
              memory_compare(edge_frames[1], untouched, sizeof(untouched)) == 0);
        edge_draw(&engine, chain, 'A', 1 << 20, 1);
        check("size: 16384 px is past what is drawn and draws nothing",
              memory_compare(edge_frames[1], untouched, sizeof(untouched)) == 0);

        font_metrics zero = font_metrics_at(one, 0);
        font_target target = {edge_frames[1], 48, 48, 32};
        p8 pair[2] = {'A', 'B'};
        b64 pen;

        for (positive i = 0; i < 48 * 32; i++)
                edge_frames[1][i] = 0x123456;
        pen = font_draw(&engine, chain, 1, pair, 2, 0, 0, &target, 4 << 16, 24, 0xffffff, 0x123456);
        check("size: zero draws nothing, advances nothing, and measures a one-pixel cell",
              pen == 0 && zero.cell_width == 1 && zero.cell_height == 1 &&
              memory_compare(edge_frames[1], untouched, sizeof(untouched)) == 0);
}

/*
        font_strip16, the strip's copy of a cell's coverage, against the loop it replaces: every row count from none up to
        forty, every stride the strip and a bitmap can have, data random in both, and the bytes the body wrote (its
        sixteen-byte tails included) compared with the loop's over the whole span either of them can reach.
*/
static fn check_strip(void)
{
        static p8 source[41 * 80 + 16], asm_into[41 * 96 + 16], c_into[41 * 96 + 16];
        p64 state = 0x2c7d9e11b5a38f04ull;
        p8 same = true;

        for (p32 trial = 0; trial < 4000; trial++)
        {
                positive rows = trial % 41;
                positive source_stride = 16 + (trial * 7) % 65, stride = 16 + (trial * 11) % 81;
                positive span = rows * stride + 16;

                for (positive i = 0; i < sizeof(source); i++)
                {
                        state ^= state << 13, state ^= state >> 7, state ^= state << 17;
                        source[i] = (p8)state;
                }
                for (positive i = 0; i < sizeof(asm_into); i++)
                        asm_into[i] = c_into[i] = (p8)(i * 37 + trial);
                font_strip16(asm_into, stride, source, source_stride, rows);
                for (positive r = 0; r < rows; r++)
                        for (positive k = 0; k < 16; k++)
                                c_into[r * stride + k] = source[r * source_stride + k];
                for (positive i = 0; i < span; i++)
                        if (asm_into[i] != c_into[i])
                                same = false;
        }
        check("strip: the body's bytes are the loop's, every row count, stride and tail", same);
}

//      Whether a way or a recent cell is still keyed to the engine's current generation: one a lookup would hit.
static p8 generation_kept(const font_engine address_to e)
{
        for (positive i = 0; i < FONT_RECENT; i++)
                if (e->recent[i].key && e->recent[i].generation == e->generation)
                        return true;
        return false;
}

static fn check_generation_wrap(void)
{
        font_engine g = {0};

        font_start(&g, small_atlas, 128, 32);
        check("font_start initializes generation to 1", g.generation == 1);
        g.generation = 0xffffffffu;
        font_reset(&g);
        check("font_reset skips 0 on 32-bit wrap and lands on 1", g.generation == 1);
        g.generation = 0xffffffffu;
        font_start(&g, small_atlas, 128, 32);
        check("font_start skips 0 on 32-bit wrap and lands on 1", g.generation == 1);

        //      A drawn cell written at generation 1, then the counter run round to 1 again through a reset: the way
        //      and a recent cell with the same key and cell must miss, not answer with the slot they named.
        font_cached(&g, null, 0x2500, 8 << 16 | 16, 0, 0, 0);
        g.recent[0] = (font_recent){.key = (p64)1 << 63 | 5, .generation = 1};
        check("wrap: a drawn cell is kept at generation 1 before the counter wraps", generation_kept(&g));
        g.generation = 0xffffffffu;
        font_reset(&g);
        check("wrap: font_reset clears the ways and recent cells a generation 1 left behind", !generation_kept(&g));

        //      The same by a new cell identity: the identity's wrap runs the counter round to 1 too.
        font_cached(&g, null, 0x2500, 8 << 16 | 16, 0, 0, 0);
        g.recent[0] = (font_recent){.key = (p64)1 << 63 | 5, .generation = 1};
        g.generation = 0xffffffffu;
        font_identify(&g, 0x1234, 0x5678, 1);
        check("wrap: font_identify clears the ways and recent cells a generation 1 left behind",
              g.generation == 1 && !generation_kept(&g));
}

b32 main()
{
        font_face one, two;

        font_make_one();
        font_make_two();
        font_start(&engine, atlas, 512, 512);
        font_start(&small_engine, small_atlas, 128, 32);

        check("load: the proportional font", font_load(&one, font_one, font_one_size, 0) == 0);
        check("load: the monospace font", font_load(&two, font_two, font_two_size, 0) == 0);
        check("load: an OpenType CFF font is out of scope",
              font_load(&(font_face){0}, (const p8 address_to)"OTTO\0\0\0\0\0\0\0\0", 12, 0) == FONT_UNSUPPORTED);
        check("load: nothing is not a font", font_load(&(font_face){0}, font_one, 0, 0) == FONT_BAD);

        check_tables(&one, &two);
        check_outlines(&one, &two);
        check_layout(&one, &two);
        check_long_run(&two);
        check_light();
        check_drawn();
        check_shapes();
        check_snap();
        check_bars();
        check_drawn_bounds();
        check_drawing(&one, &two);
        check_cells(&engine, &two);
        check_chains(&one, &two);
        check_synthetic(&one, &two);
        check_polarity(&two);
        check_invariants();
        check_dots();
        check_thickness();
        check_upright();
        check_turned();
        check_cells(&small_engine, &two);
        check_translate();
        check_translate_planes();
        check_strip();
        check_blend();
        check_text_mix(&engine);
        check_average_ahead();
        check_glass_routines();
        check_glass_tiers();
        check_saturate();
        check_sum_swap();
        check_invert();
        check_tint();
        check_mix();
        check_fill_rect();
        check_over_tone();
        check_copy_rect();
        check_copy_rect_stream();
        check_runs();
        check_resample_rect();
        check_copy_changed();
        check_first_differs();
        check_sweep();

        guarded = memory(9 * 4096);
        check("hostile: a guard page",
              guarded && system_call_3(syscall(mprotect), (positive)(guarded + 8 * 4096), 4096, 0) == 0);
        check_hostile(font_one, font_one_size, 20000);
        check_hostile(font_two, font_two_size, 20000);
        check_hostile_arithmetic();
        check_raster_ceiling();
        check_reverse_multiple();
        check_cell_run(&two);
        check_crossing_stems();
        //      Last: it loads faces until the 16-bit ids run out, and no face loads after it.
        check_edges(&one, &two);
        check_generation_wrap();

        return test_report(null);
}
#endif


#ifdef CHECK_font_shape
#include "../../src/lib.util.c"
#include "../../src/canvas/font.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        The text engine's right to left half: the bidirectional algorithm,
        shaping, and the laying of a line that is not left to right.

        The algorithm is held to the Unicode Consortium's own tests: the
        sample below is every 450th line of BidiCharacterTest.txt, a denser
        sample of the lines with paired brackets, and every 3000th case of
        BidiTest.txt with every case of one character, to the levels and the
        order each says. (The whole of both files, 770,241 cases and 91,707
        lines of Unicode 18.0.0, is run through the same code out of tree
        and passes; the sample is what is kept in the tree to turn red.)
*/

static font_engine engine;
static p8 atlas[512 * 512];

static const string_address class_names[23] = {"L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S",
                                               "WS", "ON", "LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI"};

//      A code point of each class, for the lines of BidiTest.txt, which name classes.
static const p32 class_samples[23] = {0x61, 0x5d0, 0x627, 0x31, 0x2b, 0x24, 0x660, 0x2c, 0x300, 0xad, 0x2029, 0x09,
                                      0x20, 0x21, 0x202a, 0x202d, 0x202b, 0x202e, 0x202c, 0x2066, 0x2067, 0x2068, 0x2069};

//      A deterministic sample of BidiCharacterTest.txt (every 450th line, and a denser one of the lines with brackets):
//      code points; direction; paragraph level; levels; order.
static const string_address bidi_character_sample[] = {
    "05D0 05D1 0028 05D2 05D3 005B 0026 0065 0066 005D 002E 0029 0067 0068;0;0;1 1 0 1 1 0 0 0 0 0 0 0 0 0;1 0 2 4 3 5 6 7 8 9 10 11 12 13",
    "0061 0028 05D0 0029 05D1;1;1;2 1 1 1 1;4 3 2 1 0",
    "05D0 0028 0061 0029 2680 05D1;1;1;1 1 2 1 1 1;5 4 3 2 1 0",
    "0028 0029 2680 0028 0061 2681 0029;1;1;1 1 1 1 2 1 1;6 5 4 3 2 1 0",
    "0061 0028 0062 0029 0028 05D0 0029;1;1;2 2 2 2 1 1 1;6 5 4 0 1 2 3",
    "0028 0029 05D0 0061 05D1 0028 0029;1;1;1 1 1 2 1 1 1;6 5 4 3 2 1 0",
    "0028 2680 0061 2681 0029 0028 2682 0029;1;1;1 1 2 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 2680 0029 0061 0028 05D0 0029 0062;1;1;1 1 1 2 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 0029 2680 05D0 0028 0061 0029 0062;1;1;1 1 1 1 1 2 1 2;7 6 5 4 3 2 1 0",
    "0061 2680 0028 0029 0028 0062 0029 0063;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0061 0028 0062 2680 0029 0028 0029 0063;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0061 0028 05D0 2680 0029 0028 0029 05D1;1;1;2 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 2680 0029 0028 0029 2681 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 05D0 0029 0028 2680 05D1 2681 0029;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 05D0 0061 0029 0062 0028 2680 0029;1;1;1 1 2 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 05D1 0029 0028 0061 0029 2680;1;1;1 1 1 1 1 2 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 0028 0029 2681 0061 0029;1;1;1 1 1 1 1 2 1;6 5 4 3 2 1 0",
    "0061 0028 2680 0028 0029 0029 05D0;1;1;2 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "05D0 0028 0028 0061 0029 2680 0029;1;1;1 1 1 2 1 1 1;6 5 4 3 2 1 0",
    "0028 2680 0028 0029 2681 05D0 0029 2682;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 2680 0061 05D0 2681 0028 0029 0029;1;1;1 1 2 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 0028 05D0 0061 2681 0029 0029;1;1;1 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "0061 2680 0028 0062 0028 2681 0029 0029;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0028 0028 0061 2680 0029 05D0 0029 0062;1;1;1 1 2 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 0028 0029 0061 05D0 2680 0029 2681;1;1;1 1 1 2 1 1 1 1;7 6 5 4 3 2 1 0",
    "0061 0028 05D0 0028 05D1 0029 0062 0029;1;1;2 1 1 1 1 1 2 1;7 6 5 4 3 2 1 0",
    "0028 05D0 0028 2680 0061 0029 05D1 0029;1;1;1 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 0061 0028 2680 05D1 0029 0029;1;1;1 1 2 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 0028 0029 05D1 2680 0061 0029;1;1;1 1 1 1 1 1 2 1;7 6 5 4 3 2 1 0",
    "05D0 2680 0028 0029 005B 005D;1;1;1 1 1 1 1 1;5 4 3 2 1 0",
    "0061 0028 2680 0029 005B 005D 2681;1;1;2 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 05D0 2680 0029 005B 005D 0061;1;1;1 1 1 1 1 1 2;6 5 4 3 2 1 0",
    "2680 0028 0029 2681 005B 0061 005D 0062;1;1;1 1 1 1 1 2 1 2;7 6 5 4 3 2 1 0",
    "2680 0028 0061 0029 005B 0062 2681 005D;1;1;1 1 2 1 1 2 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 05D0 0029 2681 005B 05D1 005D;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0061 0028 0029 2680 005B 2681 005D 0062;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0028 0029 0061 2680 05D0 2681 005B 005D;1;1;1 1 2 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0061 0028 0029 0062 05D0 005B 0063 005D;1;1;2 2 2 2 1 1 2 1;7 6 5 4 0 1 2 3",
    "0061 0028 05D0 0029 0062 05D1 005B 005D;1;1;2 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 0029 2680 0061 005B 0062 005D;1;1;1 1 1 1 2 2 2 2;4 5 6 7 3 2 1 0",
    "05D0 0028 0061 2680 0062 0029 005B 005D;1;1;1 1 2 2 2 1 1 1;7 6 5 2 3 4 1 0",
    "05D0 0028 0061 05D1 0029 005B 005D 05D2;1;1;1 1 2 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 005B 005D 2680 0061 0029;1;1;1 1 1 1 2 1;5 4 3 2 1 0",
    "0028 005B 2680 05D0 2681 005D 0029;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 005B 005D 0061 05D0 0029 0062;1;1;1 1 1 2 1 1 2;6 5 4 3 2 1 0",
    "2680 0028 005B 005D 2681 0029 2682 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 005B 005D 2680 0061 0029 2681 0062;1;1;1 1 1 1 2 1 1 2;7 6 5 4 3 2 1 0",
    "2680 0028 05D0 2681 005B 0061 005D 0029;1;1;1 1 1 1 1 2 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 05D0 005B 005D 05D1 0061 0029;1;1;1 1 1 1 1 1 2 1;7 6 5 4 3 2 1 0",
    "0028 0061 005B 2680 005D 0062 05D0 0029;1;1;1 2 2 2 2 2 1 1;7 6 1 2 3 4 5 0",
    "0061 0028 0062 005B 005D 0063 0029 2680;1;1;2 2 2 2 2 2 2 1;7 0 1 2 3 4 5 6",
    "0028 0061 05D0 0062 2680 005B 005D 0029;1;1;1 2 1 2 1 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 2680 0028 005B 005D 0061 0029 2681;1;1;1 1 1 1 1 2 1 1;7 6 5 4 3 2 1 0",
    "05D0 2680 0028 05D1 005B 005D 0029 05D2;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 05D0 005B 005D 0061 05D1 2680 0029;1;1;1 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 005B 05D1 005D 05D2 0029 2680;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 005B 0029 0061 005D 0062;1;1;1 1 1 1 2 2 2;4 5 6 3 2 1 0",
    "0028 0061 005B 0062 0029 005D 05D0;1;1;1 2 2 2 1 1 1;6 5 4 1 2 3 0",
    "05D0 0028 05D1 005B 0029 2680 005D;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 005B 2680 0061 2681 0029 2682 005D;1;1;1 1 1 2 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 005B 2680 0029 0061 05D0 005D 0062;1;1;1 1 1 1 2 1 1 2;7 6 5 4 3 2 1 0",
    "2680 0028 05D0 005B 0061 0029 005D 05D1;1;1;1 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "0061 0028 005B 2680 0029 0062 005D 0063;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0061 0028 005B 0062 2680 0029 005D 0063;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0061 0028 005B 05D0 2680 0029 005D 05D1;1;1;2 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 05D0 2680 005B 0029 2681 005D 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 005B 0029 05D0 2680 05D1 2681 005D;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "05D0 0028 0061 005B 0062 0029 005D 0063;1;1;1 1 2 2 2 1 1 2;7 6 5 2 3 4 1 0",
    "0028 05D0 005B 05D1 0061 0029 2680 005D;1;1;1 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "0028 0028 05D0 0029 05D1;1;1;1 1 1 1 1;4 3 2 1 0",
    "0028 0028 05D0 2680 05D1 0029;1;1;1 1 1 1 1 1;5 4 3 2 1 0",
    "2680 0028 05D0 0028 0029 2681 0061;1;1;1 1 1 1 1 1 2;6 5 4 3 2 1 0",
    "0061 0028 0028 2680 05D0 0029 05D1;1;1;2 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "05D0 0028 2680 0061 0028 0029 0062;1;1;1 1 1 2 2 2 2;3 4 5 6 2 1 0",
    "05D0 0028 05D1 0028 0061 0029 0062;1;1;1 1 1 1 2 1 2;6 5 4 3 2 1 0",
    "0028 0061 2680 0029 0028 05D0;1;1;1 2 1 1 1 1;5 4 3 2 1 0",
    "2680 0028 0029 0061 0028 2681 0062;1;1;1 1 1 2 2 2 2;3 4 5 6 2 1 0",
    "0061 2680 0028 0029 0062 2681 0028;1;1;2 2 2 2 2 1 1;6 5 0 1 2 3 4",
    "0028 0061 0029 05D0 0062 2680 0028;1;1;1 2 1 1 2 1 1;6 5 4 3 2 1 0",
    "0028 05D0 0061 0029 2680 0028 05D1;1;1;1 1 2 1 1 1 1;6 5 4 3 2 1 0",
    "2680 005B 0061 0028 0029 2681;1;1;1 1 2 1 1 1;5 4 3 2 1 0",
    "005B 05D0 0061 05D1 0028 0029;1;1;1 1 2 1 1 1;5 4 3 2 1 0",
    "005B 2680 05D0 2681 0028 0029 05D1;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0061 005B 0062 0028 2680 0029 05D0;1;1;2 2 2 1 1 1 1;6 5 4 3 0 1 2",
    "005B 0028 05D0 2680 0061 05D1 0029;1;1;1 1 1 1 2 1 1;6 5 4 3 2 1 0",
    "0028 05D0 005B 0029;1;1;1 1 1 1;3 2 1 0",
    "0061 0028 05D0 005B 0029 2680;1;1;2 1 1 1 1 1;5 4 3 2 1 0",
    "0028 2680 0061 005B 0029 2681 05D0;1;1;1 1 2 1 1 1 1;6 5 4 3 2 1 0",
    "0028 0061 2680 005B 0062 0029 0063;1;1;1 2 2 2 2 1 2;6 5 1 2 3 4 0",
    "0028 005B 0061 05D0 0062 05D1 0029;1;1;1 1 2 1 2 1 1;6 5 4 3 2 1 0",
    "0028 05D0 0061 05D1 005B 0029 2680;1;1;1 1 2 1 1 1 1;6 5 4 3 2 1 0",
    "0028 0029 2680 0061 05D0 005B;1;1;1 1 1 2 1 1;5 4 3 2 1 0",
    "2680 0028 2681 0029 2682 005B 0061;1;1;1 1 1 1 1 1 2;6 5 4 3 2 1 0",
    "2680 0028 05D0 0061 0029 005B 0062;1;1;1 1 1 2 1 1 2;6 5 4 3 2 1 0",
    "0061 0028 0029 0062 05D0 0063 005B;1;1;2 2 2 2 1 2 1;6 5 4 0 1 2 3",
    "05D0 2680 0028 0029 05D1 005B 0061;1;1;1 1 1 1 1 1 2;6 5 4 3 2 1 0",
    "0061 2680 0029 0028 0029;1;1;2 1 1 1 1;4 3 2 1 0",
    "0029 0061 0028 05D0 0029 05D1;1;1;1 2 1 1 1 1;5 4 3 2 1 0",
    "0029 2680 0061 0028 05D0 2681 0029;1;1;1 1 2 1 1 1 1;6 5 4 3 2 1 0",
    "0061 2680 0029 0028 05D0 2681 0029;1;1;2 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0029 05D0 2680 0028 2681 0061 0029;1;1;1 1 1 1 1 2 1;6 5 4 3 2 1 0",
    "0029 05D0 0061 05D1 0028 05D2 0029;1;1;1 1 2 1 1 1 1;6 5 4 3 2 1 0",
    "2680 0028 05D0 0029 0029 05D1;1;1;1 1 1 1 1 1;5 4 3 2 1 0",
    "2680 0028 2681 0029 0061 0029 05D0;1;1;1 1 1 1 2 1 1;6 5 4 3 2 1 0",
    "0028 2680 05D0 0029 05D1 0029 2681;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 0061 05D0 2680 0062 0029 0029;1;1;1 2 1 1 2 1 1;6 5 4 3 2 1 0",
    "0028 05D0 0029 2680 05D1 0029 05D2;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "005D 05D0 2680 0028 0029;1;1;1 1 1 1 1;4 3 2 1 0",
    "005D 0028 05D0 2680 0061 0029;1;1;1 1 1 1 2 1;5 4 3 2 1 0",
    "005D 2680 0061 05D0 0028 0029 05D1;1;1;1 1 2 1 1 1 1;6 5 4 3 2 1 0",
    "0061 005D 2680 0028 05D0 0029 0062;1;1;2 1 1 1 1 1 2;6 5 4 3 2 1 0",
    "05D0 005D 2680 0061 0028 0029 2681;1;1;1 1 1 2 1 1 1;6 5 4 3 2 1 0",
    "05D0 005D 05D1 0028 0029 2680 05D2;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 0061 2680 005D 0029 0062;1;1;1 2 1 1 1 2;5 4 3 2 1 0",
    "0028 2680 005D 2681 05D0 0029 05D1;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0061 2680 0028 2681 05D0 005D 0029;1;1;2 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 0061 005D 05D0 2680 05D1 0029;1;1;1 2 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 05D0 005D 0061 2680 0029 0062;1;1;1 1 1 2 1 1 2;6 5 4 3 2 1 0",
    "2680 0028 0029 2681 005D 0061;1;1;1 1 1 1 1 2;5 4 3 2 1 0",
    "0028 05D0 0061 0029 2680 005D;1;1;1 1 2 1 1 1;5 4 3 2 1 0",
    "0028 2680 05D0 0029 005D 2681 0061;1;1;1 1 1 1 1 1 2;6 5 4 3 2 1 0",
    "0061 0028 0029 0062 2680 005D 2681;1;1;2 2 2 2 1 1 1;6 5 4 0 1 2 3",
    "05D0 2680 0028 0061 0029 05D1 005D;1;1;1 1 1 2 1 1 1;6 5 4 3 2 1 0",
    "05D0 0028 0029 05D1 0061 005D 05D2;1;1;1 1 1 1 2 1 1;6 5 4 3 2 1 0",
    "0028 2680 0028 2681 0061 0029 0028 0029;1;1;1 1 1 1 2 1 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 0028 05D0 0029 0028 0029 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 0061 0028 0062 0029 0028 2680 0029;1;1;1 2 2 2 2 1 1 1;7 6 5 1 2 3 4 0",
    "05D0 0028 2680 0028 0029 0028 0061 0029;1;1;1 1 1 1 1 1 2 1;7 6 5 4 3 2 1 0",
    "05D0 0028 0028 05D1 0029 0028 2680 0029;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 0028 2681 0061 0029 0028 0029 0062;1;1;1 1 1 1 2 1 1 1 2;8 7 6 5 4 3 2 1 0",
    "2680 0028 0061 2681 0028 0029 0028 2682 0029;1;1;1 1 2 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 2680 0061 0029 2681 0028 05D0 0029;1;1;1 1 1 2 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "2680 0028 0061 0028 05D0 0029 0028 0062 0029;1;1;1 1 2 1 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "0028 2680 05D0 2681 0028 0029 0028 0061 0029;1;1;1 1 1 1 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 2680 05D0 0029 0061 2681 0028 0029;1;1;1 1 1 1 1 2 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 2680 05D0 0028 0029 0028 05D1 0061 0029;1;1;1 1 1 1 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "0061 2680 0028 0028 0062 0029 0028 2681 0029;1;1;2 2 2 2 2 2 1 1 1;8 7 6 0 1 2 3 4 5",
    "0028 0061 0028 0029 2680 0062 0028 05D0 0029;1;1;1 2 2 2 2 2 1 1 1;8 7 6 1 2 3 4 5 0",
    "0028 0028 0029 0061 0028 2680 05D0 0062 0029;1;1;1 1 1 2 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 0061 0029 0062 0028 2680 05D0 0029;1;1;1 1 2 1 2 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 05D0 0028 2680 0029 0028 0029 2681;1;1;2 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 0061 05D0 0029 0028 0029 2680 05D1;1;1;1 1 2 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 0061 05D0 0062 0029 0028 0029 05D1;1;1;1 1 2 1 2 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 05D0 0029 2680 0028 2681 0061 0029;1;1;1 1 1 1 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "0028 05D0 0028 0029 2680 0061 0028 0029 0062;1;1;1 1 1 1 1 2 2 2 2;5 6 7 8 4 3 2 1 0",
    "05D0 2680 0028 0028 0029 05D1 0061 0028 0029;1;1;1 1 1 1 1 1 2 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 0028 0028 0061 2680 0062 0029 0028 0029;1;1;1 1 1 2 2 2 1 1 1;8 7 6 3 4 5 2 1 0",
    "05D0 0028 0061 0028 0062 0029 0028 0063 0029;1;1;1 1 2 2 2 2 2 2 2;2 3 4 5 6 7 8 1 0",
    "0028 0028 05D0 0029 0028 0061 05D1 0029 0062;1;1;1 1 1 1 1 2 1 1 2;8 7 6 5 4 3 2 1 0",
    "0028 0028 05D0 0029 0028 05D1 2680 0029 05D2;1;1;1 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 005B 0061 005D 0029;1;1;1 1 1 2 1 1;5 4 3 2 1 0",
    "0028 0028 005B 2680 005D 0029 2681 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "0028 2680 0028 005B 005D 05D0 0061 0029;1;1;1 1 1 1 1 1 2 1;7 6 5 4 3 2 1 0",
    "0061 0028 0028 0062 005B 0063 005D 0029;1;1;2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7",
    "0028 05D0 0028 2680 005B 0061 005D 0029;1;1;1 1 1 1 1 2 1 1;7 6 5 4 3 2 1 0",
    "0028 0028 05D0 005B 05D1 005D 0029 2680;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 0028 2680 005B 2681 0061 005D 0062 0029;1;1;1 1 1 1 1 2 1 2 1;8 7 6 5 4 3 2 1 0",
    "0028 2680 0061 2681 0028 2682 005B 005D 0029;1;1;1 1 2 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 005B 2680 0061 005D 2681 0029 05D0;1;1;1 1 1 1 2 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "2680 0028 0028 005B 0061 05D0 005D 0029 0062;1;1;1 1 1 1 2 1 1 1 2;8 7 6 5 4 3 2 1 0",
    "0028 2680 0028 05D0 005B 2681 005D 0029 0061;1;1;1 1 1 1 1 1 1 1 2;8 7 6 5 4 3 2 1 0",
    "0028 0028 005B 2680 005D 05D0 0061 0029 2681;1;1;1 1 1 1 1 1 2 1 1;8 7 6 5 4 3 2 1 0",
    "2680 0028 05D0 0028 05D1 005B 05D2 005D 0029;1;1;1 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 2680 0028 005B 0062 005D 0029 2681;1;1;2 2 2 2 2 2 2 2 1;8 0 1 2 3 4 5 6 7",
    "0028 0028 0061 005B 2680 005D 0062 05D0 0029;1;1;1 1 2 2 2 2 2 1 1;8 7 2 3 4 5 6 1 0",
    "0061 0028 2680 0028 05D0 005B 05D1 005D 0029;1;1;2 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 0028 0062 005B 0063 2680 005D 0029;1;1;2 2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7 8",
    "0028 0061 05D0 0028 2680 005B 2681 005D 0029;1;1;1 2 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 005B 0061 005D 05D0 0029 2680 05D1;1;1;1 1 1 2 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 005B 0061 005D 05D0 0062 05D1 0029;1;1;1 1 1 2 1 1 2 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 2680 0028 0028 005B 2681 05D1 005D 0029;1;1;1 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 2680 0028 0061 05D1 0028 005B 005D 0029;1;1;1 1 1 2 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 0028 2680 0028 005B 005D 05D1 0029 0061;1;1;1 1 1 1 1 1 1 1 2;8 7 6 5 4 3 2 1 0",
    "05D0 0028 0028 005B 005D 0061 0029 2680 0062;1;1;1 1 1 1 1 2 1 1 2;8 7 6 5 4 3 2 1 0",
    "0028 05D0 0028 005B 0061 005D 0062 0029 0063;1;1;1 1 1 1 2 1 2 1 2;8 7 6 5 4 3 2 1 0",
    "05D0 0028 0061 0028 005B 05D1 005D 0029 05D2;1;1;1 1 2 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 0028 05D1 0028 005B 005D 0061 0029 2680;1;1;1 1 1 1 1 1 2 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 2680 005B 2681 0029 005D;1;1;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 2680 0028 2681 005B 05D0 0029 005D;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "2680 0028 05D0 0028 05D1 005B 0029 005D;1;1;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0061 0028 0062 0028 005B 05D0 0029 005D;1;1;2 2 2 1 1 1 1 1;7 6 5 4 3 0 1 2",
    "0028 0028 05D0 005B 0029 005D 2680 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "05D0 0028 0028 005B 0029 05D1 005D 0061;1;1;1 1 1 1 1 1 1 2;7 6 5 4 3 2 1 0",
    "2680 0028 2681 0028 005B 0029 0061 005D 05D0;1;1;1 1 1 1 1 1 2 1 1;8 7 6 5 4 3 2 1 0",
    "0028 2680 0028 005B 0061 0029 2681 005D 2682;1;1;1 1 1 1 2 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "2680 0028 0028 0061 005B 0062 0029 2681 005D;1;1;1 1 1 2 2 2 1 1 1;8 7 6 3 4 5 2 1 0",
    "0028 2680 0061 0028 005B 0029 05D0 005D 0062;1;1;1 1 2 1 1 1 1 1 2;8 7 6 5 4 3 2 1 0",
    "0028 0028 2680 05D0 005B 2681 0029 0061 005D;1;1;1 1 1 1 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "2680 0028 0028 05D0 005B 0029 0061 005D 0062;1;1;1 1 1 1 1 1 2 2 2;6 7 8 5 4 3 2 1 0",
    "0028 2680 0028 05D0 005B 0029 05D1 005D 05D2;1;1;1 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 0028 005B 0029 2680 0062 005D 2681;1;1;2 2 2 2 2 2 2 1 1;8 7 0 1 2 3 4 5 6",
    "0061 2680 0028 0028 05D0 2681 005B 0029 005D;1;1;2 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0061 2680 05D0 0028 005B 0029 05D1 005D;1;1;1 2 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 0062 0028 0063 005B 0029 005D 0064;1;1;2 2 2 2 2 2 2 2 2;0 1 2 3 4 5 6 7 8",
    "0028 0028 0061 05D0 2680 005B 0029 005D 2681;1;1;1 1 2 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 0028 05D0 0062 2680 005B 0029 005D;1;1;2 1 1 1 2 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0028 0028 005B 05D0 0029 05D1 2680 005D;1;1;2 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 0028 0028 2680 005B 2681 0029 005D 05D1;1;1;1 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "05D0 0028 2680 0061 0028 005B 0029 005D 05D1;1;1;1 1 1 2 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 05D0 2680 05D1 0028 0061 005B 0029 005D;1;1;1 1 1 1 1 2 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 05D0 0028 0061 2680 005B 0029 0062 005D;1;1;1 1 1 2 1 1 1 2 1;8 7 6 5 4 3 2 1 0",
    "05D0 0028 0028 0061 005B 0029 0062 05D1 005D;1;1;1 1 1 2 1 1 2 1 1;8 7 6 5 4 3 2 1 0",
    "0028 05D0 0061 0028 05D1 005B 0029 005D 05D2;1;1;1 1 2 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 05D0 0028 05D1 0061 005B 2680 0029 005D;1;1;1 1 1 1 2 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0061 0062 0063 0020 0028 0627 0628 062C 0020 0064 0065 0066 0029 0020 05D0 05D1 05D2;1;1;2 2 2 1 1 1 1 1 1 2 2 2 1 1 1 1 1;16 15 14 13 12 9 10 11 8 7 6 5 4 3 0 1 2",
    "05D0 2680 0028 0061 0029;0;0;1 0 0 0 0;0 1 2 3 4",
    "05D0 2680 0028 05D1 0061 0029;0;0;1 0 0 1 0 0;0 1 2 3 4 5",
    "0028 0029 2680 0028 2681 0029 0061;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0029 0061 2680 0028 0062 0029;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0029 0028 05D0 2680 0029 0061;0;0;0 0 0 1 0 0 0;0 1 2 3 4 5 6",
    "0028 2680 0029 0028 2681 0061 0029 2682;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "2680 0028 0029 0028 0061 2681 0029 05D0;0;0;0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 2680 0029 0028 05D0 2681 0029 2682;0;0;0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "0028 2680 0029 05D0 0028 0061 05D1 0029;0;0;0 0 0 1 0 0 1 0;0 1 2 3 4 5 6 7",
    "0061 0028 2680 0029 0028 0062 0029 0063;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0029 0028 0061 2680 05D0 0029 05D1;0;0;0 0 0 0 0 1 0 1;0 1 2 3 4 5 6 7",
    "0061 0028 05D0 0029 0028 0029 2680 0062;0;0;0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0061 0028 05D0 0029 05D1 0028 0029 2680;0;0;0 0 1 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0029 05D0 2680 0061 0028 0062 0029;0;0;0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 05D0 0061 0029 2680 0028 0029 2681;0;0;0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "05D0 0028 0061 0029 0028 05D1 0062 0029;0;0;1 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "0028 0029 05D0 0028 05D1 0061 0029 05D2;0;0;0 0 1 0 1 0 0 1;0 1 2 3 4 5 6 7",
    "0028 0028 2680 0029 0029 2681 05D0;0;0;0 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0061 0028 0028 0029 0029 2680 05D0;0;0;0 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0028 05D0 0028 2680 0029 05D1 0029;0;0;0 1 1 1 1 1 0;0 5 4 3 2 1 6",
    "2680 0028 2681 0061 0028 0029 0029 05D0;0;0;0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 0028 2680 0061 2681 05D0 0029 0029;0;0;0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "2680 0028 0028 05D0 0029 0029 2681 0061;0;0;0 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 2680 05D0 0028 05D1 0029 2681 0029;0;0;0 0 1 1 1 1 0 0;0 1 5 4 3 2 6 7",
    "0061 2680 0028 0062 0028 05D0 0029 0029;0;0;0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "0061 0028 0062 0028 2680 0029 0063 0029;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0061 0028 05D0 2680 0029 0062 0029;0;0;0 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0061 05D0 0028 05D1 0062 0029 0029;0;0;0 0 1 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "05D0 0028 2680 0028 0029 0061 05D1 0029;0;0;1 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "05D0 0028 0028 0061 2680 0062 0029 0029;0;0;1 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 05D0 0028 0061 0029 05D1 0062 0029;0;0;0 1 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "05D0 0028 05D1 0028 0029 05D2 0029 05D3;0;0;1 1 1 1 1 1 1 1;7 6 5 4 3 2 1 0",
    "0028 0029 2680 0061 005B 2681 005D;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0061 0028 0062 0029 2680 005B 005D;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 0028 0029 005B 0061 2680 005D;0;0;1 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0029 2680 005B 2681 0061 005D 05D0;0;0;0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 2680 0061 0029 0062 005B 005D 2681;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 2680 0029 005B 05D0 2681 005D 0061;0;0;0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "0028 2680 0029 05D0 005B 05D1 005D 0061;0;0;0 0 0 1 1 1 1 0;0 1 2 6 5 4 3 7",
    "0061 0028 0029 2680 005B 0062 005D 05D0;0;0;0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 0061 0029 005B 0062 2680 005D 0063;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0061 0028 05D0 2680 0029 05D1 005B 005D;0;0;0 0 1 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "05D0 2680 0028 0029 2681 005B 2682 005D;0;0;1 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 05D0 0029 2680 005B 0061 05D1 005D;0;0;0 1 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "0028 05D0 0029 0061 2680 005B 005D 0062;0;0;0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "05D0 0028 0029 0061 005B 05D1 005D 05D2;0;0;1 0 0 0 0 1 0 1;0 1 2 3 4 5 6 7",
    "2680 0028 005B 005D 2681 0029;0;0;0 0 0 0 0 0;0 1 2 3 4 5",
    "0028 005B 005D 2680 0061 0029 0062;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0061 005B 0062 005D 0063 0029;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 0028 005B 0061 005D 0062 0029;0;0;1 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 2680 005B 2681 005D 05D0 0029 2682;0;0;0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "2680 0028 005B 0061 005D 0062 05D0 0029;0;0;0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "2680 0028 05D0 005B 005D 2681 05D1 0029;0;0;0 0 1 1 1 1 1 0;0 1 6 5 4 3 2 7",
    "0061 0028 005B 2680 005D 2681 0029 2682;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 005B 0061 2680 0062 005D 0029 05D0;0;0;0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0061 0028 005B 005D 0062 2680 05D0 0029;0;0;0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "0028 0061 05D0 2680 005B 005D 0029 05D1;0;0;0 0 1 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "05D0 2680 0028 005B 005D 0029 2681 0061;0;0;1 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "05D0 2680 0028 005B 005D 05D1 0029 2681;0;0;1 1 1 1 1 1 1 0;6 5 4 3 2 1 0 7",
    "05D0 0028 0061 2680 005B 005D 05D1 0029;0;0;1 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "05D0 0028 05D1 005B 2680 005D 0029 2681;0;0;1 1 1 1 1 1 1 0;6 5 4 3 2 1 0 7",
    "0028 005B 0029 005D 2680 0061;0;0;0 0 0 0 0 0;0 1 2 3 4 5",
    "0028 005B 2680 0061 0029 005D 05D0;0;0;0 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0061 0028 05D0 005B 2680 0029 005D;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 0028 005B 0029 0061 005D 05D1;0;0;1 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0028 2680 005B 2681 05D0 0061 0029 005D;0;0;0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "2680 0028 005B 0061 05D0 0029 2681 005D;0;0;0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "0028 2680 05D0 005B 0029 005D 2681 05D1;0;0;0 0 1 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0061 0028 2680 005B 0029 005D 2681 0062;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0061 0028 2680 005B 05D0 0029 2681 005D;0;0;0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "0061 0028 005B 0062 0029 0063 005D 2680;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 005B 0061 05D0 0029 005D 2680 05D1;0;0;0 0 0 1 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 005B 05D0 2680 0029 2681 0061 005D;0;0;0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 05D0 2680 005B 05D1 2681 0029 005D;0;0;0 1 1 1 1 0 0 0;0 4 3 2 1 5 6 7",
    "0028 05D0 0061 005B 2680 05D1 0029 005D;0;0;0 1 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "05D0 0028 005B 05D1 2680 0061 0029 005D;0;0;1 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 2680 0028 0029 0061;0;0;0 0 0 0 0;0 1 2 3 4",
    "0061 0028 0028 0062 0029 0063;0;0;0 0 0 0 0 0;0 1 2 3 4 5",
    "0028 0028 2680 0061 2681 0029 2682;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0061 2680 0028 2681 0029 2682;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0061 05D0 2680 0028 2681 0029;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "0028 05D0 2680 05D1 0028 2681 0029;0;0;0 1 1 1 0 0 0;0 3 2 1 4 5 6",
    "0028 0029 0028;0;0;0 0 0;0 1 2",
    "0061 0028 2680 05D0 0029 0028;0;0;0 0 0 1 0 0;0 1 2 3 4 5",
    "0028 2680 0029 2681 05D0 2682 0028;0;0;0 0 0 0 1 0 0;0 1 2 3 4 5 6",
    "2680 0028 0029 05D0 0061 0028 05D1;0;0;0 0 0 1 0 0 1;0 1 2 3 4 5 6",
    "0061 0028 0062 0029 05D0 2680 0028;0;0;0 0 0 0 1 0 0;0 1 2 3 4 5 6",
    "05D0 0028 0029 2680 0061 05D1 0028;0;0;1 0 0 0 0 1 0;0 1 2 3 4 5 6",
    "05D0 0028 05D1 0029 0061 0028 2680;0;0;1 1 1 1 0 0 0;3 2 1 0 4 5 6",
    "005B 0061 2680 0028 0029 2681;0;0;0 0 0 0 0 0;0 1 2 3 4 5",
    "2680 005B 0028 2681 0061 0029 2682;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "005B 2680 05D0 0061 2681 0028 0029;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "0061 005B 0028 0062 0029 2680 0063;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "005B 0028 05D0 2680 0061 2681 0029;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 005B 05D1 0028 2680 0029 0061;0;0;1 1 1 0 0 0 0;2 1 0 3 4 5 6",
    "2680 0028 05D0 005B 0029 0061;0;0;0 0 1 0 0 0;0 1 2 3 4 5",
    "05D0 0028 005B 05D1 0029 0061;0;0;1 1 1 1 1 0;4 3 2 1 0 5",
    "2680 0028 05D0 005B 2681 0029 05D1;0;0;0 0 1 0 0 0 1;0 1 2 3 4 5 6",
    "0061 2680 0028 05D0 005B 0029 05D1;0;0;0 0 0 1 0 0 1;0 1 2 3 4 5 6",
    "0028 05D0 2680 005B 2681 05D1 0029;0;0;0 1 1 1 1 1 0;0 5 4 3 2 1 6",
    "0028 05D0 0061 005B 05D1 0062 0029;0;0;0 1 0 0 1 0 0;0 1 2 3 4 5 6",
    "2680 0028 0061 0029 005B 05D0;0;0;0 0 0 0 0 1;0 1 2 3 4 5",
    "0028 0029 05D0 0061 005B 0062;0;0;0 0 1 0 0 0;0 1 2 3 4 5",
    "2680 0028 05D0 2681 0029 0061 005B;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0029 0061 2680 05D0 2681 005B;0;0;0 0 0 0 1 0 0;0 1 2 3 4 5 6",
    "05D0 2680 0028 2681 0029 0061 005B;0;0;1 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 0028 0061 05D1 0029 005B 2680;0;0;1 0 0 1 0 0 0;0 1 2 3 4 5 6",
    "0029 2680 0028 2681 0029 05D0;0;0;0 0 0 0 0 1;0 1 2 3 4 5",
    "0029 05D0 0028 2680 05D1 0029;0;0;0 1 1 1 1 1;0 5 4 3 2 1",
    "0029 2680 0061 0028 05D0 0029 0062;0;0;0 0 0 0 1 0 0;0 1 2 3 4 5 6",
    "0029 0061 2680 0028 0062 0029 05D0;0;0;0 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0029 0061 05D0 0062 0028 05D1 0029;0;0;0 0 1 0 0 1 0;0 1 2 3 4 5 6",
    "0029 05D0 0061 2680 05D1 0028 0029;0;0;0 1 0 0 1 0 0;0 1 2 3 4 5 6",
    "0028 05D0 0029 05D1 0029;0;0;0 1 0 1 0;0 1 2 3 4",
    "05D0 0028 0029 2680 0061 0029;0;0;1 0 0 0 0 0;0 1 2 3 4 5",
    "2680 0028 0061 0029 05D0 2681 0029;0;0;0 0 0 0 1 0 0;0 1 2 3 4 5 6",
    "0061 0028 2680 0062 0029 0029 0063;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0028 0061 05D0 0062 0029 0029 2680;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 0028 0029 0061 2680 0062 0029;0;0;1 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "005D 0061 0028 05D0 0029;0;0;0 0 0 1 0;0 1 2 3 4",
    "005D 0028 0061 05D0 0029 0062;0;0;0 0 0 1 0 0;0 1 2 3 4 5",
    "005D 2680 0061 2681 0028 05D0 0029;0;0;0 0 0 0 0 1 0;0 1 2 3 4 5 6",
    "0061 2680 005D 0062 0028 2681 0029;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0061 005D 0028 05D0 2680 05D1 0029;0;0;0 0 0 1 1 1 0;0 1 2 5 4 3 6",
    "05D0 005D 2680 05D1 0028 05D2 0029;0;0;1 1 1 1 1 1 1;6 5 4 3 2 1 0",
    "0028 2680 005D 05D0 0029;0;0;0 0 0 1 0;0 1 2 3 4",
    "0028 0061 005D 0062 0029 05D0;0;0;0 0 0 0 0 1;0 1 2 3 4 5",
    "2680 0028 005D 0061 2681 0029 0062;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0061 0028 2680 005D 2681 0029 0062;0;0;0 0 0 0 0 0 0;0 1 2 3 4 5 6",
    "0061 0028 05D0 005D 2680 0062 0029;0;0;0 0 1 0 0 0 0;0 1 2 3 4 5 6",
    "05D0 2680 0028 05D1 0061 005D 0029;0;0;1 0 0 1 0 0 0;0 1 2 3 4 5 6",
    "0028 0029 005D 0061;0;0;0 0 0 0;0 1 2 3",
    "0028 0061 2680 0029 005D 05D0;0;0;0 0 0 0 0 1;0 1 2 3 4 5",
    "2680 0028 0029 2681 05D0 005D 0061;0;0;0 0 0 0 1 0 0;0 1 2 3 4 5 6",
    "0028 0029 2680 05D0 0061 05D1 005D;0;0;0 0 0 1 0 1 0;0 1 2 3 4 5 6",
    "0061 0028 0062 05D0 0029 005D 0063;0;0;0 0 0 1 0 0 0;0 1 2 3 4 5 6",
    "0028 05D0 0029 2680 0061 005D 05D1;0;0;0 1 0 0 0 0 1;0 1 2 3 4 5 6",
    "05D0 0028 0029 05D1 0061 005D 0062;0;0;1 1 1 1 0 0 0;3 2 1 0 4 5 6",
    "2680 0028 2681 0028 0029 0028 2682 0029;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0028 0029 2680 0061 0028 05D0 0029;0;0;0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "0028 0028 0061 0029 2680 0028 0029 0062;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0061 0028 0028 0029 05D0 0062 0028 0029;0;0;0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0028 0029 05D0 2680 0028 05D1 0029;0;0;0 0 0 1 1 1 1 1;0 1 2 7 6 5 4 3",
    "0028 05D0 0028 0029 05D1 0028 0029 05D2;0;0;0 1 1 1 1 1 1 1;0 7 6 5 4 3 2 1",
    "2680 0028 0028 2681 0029 0061 0028 05D0 0029;0;0;0 0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7 8",
    "2680 0028 0028 0029 0028 0061 2681 0029 2682;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 0029 0028 0061 2681 05D0 0029;0;0;0 0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 0029 0028 0061 05D0 0029 2681;0;0;0 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 05D0 0029 2681 0028 0029 2682;0;0;0 0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 2680 0029 05D0 2681 0028 0029 05D1;0;0;0 0 0 0 1 1 1 1 1;0 1 2 3 8 7 6 5 4",
    "0028 2680 05D0 0028 0029 0061 0028 0029 05D1;0;0;0 0 1 0 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "0061 2680 0028 0028 0029 0028 0029 2681 0062;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 0061 2680 0062 2681 0029 0028 0029;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 2680 0028 0028 0029 05D0 0028 0029 2681;0;0;0 0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 0028 2680 05D0 0028 0029 0028 0029 05D1;0;0;0 0 0 1 1 1 1 1 1;0 1 2 8 7 6 5 4 3",
    "0028 0061 0028 0029 0062 0028 2680 0029 05D0;0;0;0 0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "0061 0028 0062 0028 0029 05D0 0028 0029 05D1;0;0;0 0 0 0 0 1 1 1 1;0 1 2 3 4 8 7 6 5",
    "0061 0028 0028 05D0 0029 2680 05D1 0028 0029;0;0;0 0 0 1 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0061 0028 05D0 0029 0028 0062 0029 0063;0;0;0 0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0061 05D0 0028 0029 05D1 0028 05D2 0029;0;0;0 0 1 1 1 1 1 1 1;0 1 8 7 6 5 4 3 2",
    "05D0 2680 0028 0028 0029 0028 0061 0029 2681;0;0;1 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 05D0 2680 0028 0061 05D1 0029 0028 0029;0;0;0 1 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 05D0 2680 0028 05D1 0029 0028 0061 0029;0;0;0 1 1 1 1 1 0 0 0;0 5 4 3 2 1 6 7 8",
    "05D0 0028 0028 0029 0028 0061 2680 0029 0062;0;0;1 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 05D0 0061 0029 0062 0028 0029 2680;0;0;0 0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 05D0 0061 0028 0029 0028 05D1 0062 0029;0;0;0 1 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "05D0 0028 05D1 2680 0028 0029 05D2 0028 0029;0;0;1 1 1 1 1 1 1 0 0;6 5 4 3 2 1 0 7 8",
    "0028 0028 05D0 0029 05D1 0061 0028 05D2 0029;0;0;0 0 1 0 1 0 0 1 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 005B 005D 0061 0029 05D0;0;0;0 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0028 2680 0061 0028 005B 0062 005D 0029;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0061 0028 2680 005B 2681 005D 0029;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 0028 0061 005B 005D 0062 05D0 0029;0;0;0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7",
    "0028 0028 05D0 005B 005D 2680 0029 0061;0;0;0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 05D0 0028 05D1 005B 005D 0029 2680;0;0;0 1 1 1 1 1 1 0;0 6 5 4 3 2 1 7",
    "0028 0028 2680 005B 005D 2681 0061 0029 2682;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 2680 005B 005D 2681 05D0 0029 0061;0;0;0 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "2680 0028 0061 0028 2681 005B 005D 0029 05D0;0;0;0 0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "2680 0028 0061 05D0 0028 005B 2681 005D 0029;0;0;0 0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 0061 05D0 005B 005D 0029 05D1;0;0;0 0 0 0 1 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "2680 0028 0028 05D0 005B 005D 2681 0029 05D1;0;0;0 0 0 1 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 005B 005D 05D0 0061 0029 0062;0;0;0 0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 05D0 005B 05D1 005D 0029 05D2;0;0;0 0 0 1 1 1 1 0 1;0 1 2 6 5 4 3 7 8",
    "0061 0028 2680 0062 0028 005B 005D 2681 0029;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0061 2680 0028 005B 0062 05D0 005D 0029;0;0;0 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0061 2680 0028 005B 05D0 005D 0029 0062;0;0;0 0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 0061 005B 0062 005D 2680 0063 0029;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 0028 0062 0028 05D0 005B 0063 005D 0029;0;0;0 0 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0061 0028 005B 05D0 2680 005D 0029 0062;0;0;0 0 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 0061 05D0 0062 005B 005D 0029 2680;0;0;0 0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 0061 005B 05D0 005D 05D1 0029 2680;0;0;0 0 0 0 1 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "05D0 0028 0028 2680 005B 2681 005D 05D1 0029;0;0;1 1 1 1 1 1 1 1 1;8 7 6 5 4 3 2 1 0",
    "0028 0028 05D0 005B 2680 0061 005D 0029 0062;0;0;0 0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 005B 05D0 2680 005D 05D1 2681 0029;0;0;0 0 0 1 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 05D0 0028 0061 2680 005B 2681 005D 0029;0;0;0 1 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 05D0 0061 005B 005D 2680 0029 05D1;0;0;0 0 1 0 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "0028 0028 05D0 005B 0061 05D1 005D 2680 0029;0;0;0 0 1 0 0 1 0 0 0;0 1 2 3 4 5 6 7 8",
    "05D0 0028 05D1 0028 005B 2680 005D 0061 0029;0;0;1 1 1 0 0 0 0 0 0;2 1 0 3 4 5 6 7 8",
    "0028 05D0 0028 05D1 005B 005D 0061 0029 0062;0;0;0 1 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 005B 2680 0029 005D 05D0;0;0;0 0 0 0 0 0 1;0 1 2 3 4 5 6",
    "0028 0028 005B 2680 0029 005D 2681 05D0;0;0;0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 0028 2680 005B 0029 05D0 005D 0061;0;0;0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7",
    "0028 0061 0028 005B 0062 0029 2680 005D;0;0;0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "0028 05D0 0028 2680 005B 0029 005D 2681;0;0;0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7",
    "05D0 0028 0061 0028 005B 0029 005D 05D1;0;0;1 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7",
    "0028 0028 2680 005B 2681 0029 2682 05D0 005D;0;0;0 0 0 0 0 0 0 1 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 2680 005B 2681 0029 05D0 005D 2682;0;0;0 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 0061 2681 005B 0062 0029 005D;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0061 0028 0062 005B 0029 0063 005D;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 2680 0061 05D0 0062 005B 0029 005D;0;0;0 0 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 2680 0028 005B 0029 05D0 2681 0061 005D;0;0;0 0 0 0 0 1 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 2680 05D0 005B 0029 0061 2681 005D;0;0;0 0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "2680 0028 05D0 0028 05D1 005B 0061 0029 005D;0;0;0 0 1 0 1 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 0028 0028 2680 005B 0029 005D 2681 05D0;0;0;0 0 0 0 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "0028 0028 005B 0061 2680 0062 0029 005D 0063;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 005B 0029 0061 2680 05D0 2681 005D;0;0;0 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 005B 0061 0029 0062 2680 005D 2681;0;0;0 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 0028 0062 05D0 0028 005B 0029 005D 2680;0;0;0 0 0 1 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 0028 05D0 2680 0028 005B 0029 005D 0062;0;0;0 0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0061 0028 05D0 0028 0062 005B 2680 0029 005D;0;0;0 0 1 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 0061 05D0 005B 0062 0029 005D 05D1;0;0;0 0 0 1 0 0 0 0 1;0 1 2 3 4 5 6 7 8",
    "0028 05D0 2680 0028 005B 2681 0061 0029 005D;0;0;0 1 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "05D0 2680 0028 0061 0028 0062 005B 0029 005D;0;0;1 0 0 0 0 0 0 0 0;0 1 2 3 4 5 6 7 8",
    "05D0 0028 0028 2680 005B 0029 05D1 005D 2681;0;0;1 1 1 1 1 1 1 0 0;6 5 4 3 2 1 0 7 8",
    "0028 05D0 2680 0028 005B 05D1 0029 005D 05D2;0;0;0 1 1 1 1 1 1 1 1;0 8 7 6 5 4 3 2 1",
    "05D0 0028 0028 0061 005B 2680 05D1 0029 005D;0;0;1 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "05D0 0028 0061 0028 005B 0029 05D1 005D 2680;0;0;1 0 0 0 0 0 1 0 0;0 1 2 3 4 5 6 7 8",
    "0028 0028 05D0 0061 005B 05D1 0029 05D2 005D;0;0;0 0 1 0 0 1 0 1 0;0 1 2 3 4 5 6 7 8",
    "05D0 0028 0028 005B 05D1 0061 2680 0029 005D;0;0;1 0 0 0 1 0 0 0 0;0 1 2 3 4 5 6 7 8",
};

//      And of BidiTest.txt (every 3000th case, and every one of one character): classes; bitset; levels; reorder.
static const string_address bidi_class_sample[] = {
    "LRE;7;x;",
    "LRO;7;x;",
    "RLE;7;x;",
    "RLO;7;x;",
    "PDF;7;x;",
    "BN;7;x;",
    "L;3;0;0",
    "EN;3;0;0",
    "ES;3;0;0",
    "ET;3;0;0",
    "CS;3;0;0",
    "NSM;3;0;0",
    "B;3;0;0",
    "S;3;0;0",
    "WS;3;0;0",
    "ON;3;0;0",
    "LRI;3;0;0",
    "RLI;3;0;0",
    "FSI;3;0;0",
    "PDI;3;0;0",
    "R;7;1;0",
    "AL;7;1;0",
    "ES;4;1;0",
    "ET;4;1;0",
    "CS;4;1;0",
    "NSM;4;1;0",
    "B;4;1;0",
    "S;4;1;0",
    "WS;4;1;0",
    "ON;4;1;0",
    "LRI;4;1;0",
    "RLI;4;1;0",
    "FSI;4;1;0",
    "PDI;4;1;0",
    "L;4;2;0",
    "EN;4;2;0",
    "AN;7;2;0",
    "LRO AN BN;7;x 2 x;1",
    "NSM WS LRO;3;0 0 x;0 1",
    "ET BN BN LRO;3;0 x x x;0",
    "AL AN LRO;7;1 2 x;1 0",
    "NSM RLO RLO RLO;4;1 x x x;0",
    "LRE S FSI;3;x 0 0;1 2",
    "PDF ES ON;4;x 1 1;2 1",
    "LRO R LRI;2;x 2 0;1 2",
    "LRE RLE RLI RLO;3;x x 0 x;2",
    "PDF BN R RLO;7;x x 1 x;2",
    "LRE RLO BN RLI;3;x x x 0;3",
    "PDF RLO BN PDI;4;x x x 1;3",
    "RLO RLO LRE NSM;4;x x x 6;3",
    "L PDI RLE BN;3;0 0 x x;0 1",
    "WS CS PDF BN;3;0 0 x x;0 1",
    "ON R LRE LRO;2;0 1 x x;0 1",
    "ES PDF PDI BN;3;0 x 0 x;0 2",
    "EN RLO L BN;3;0 x 1 x;0 2",
    "LRI RLO AL BN;3;0 x 3 x;0 2",
    "WS RLO PDF ES;3;0 x x 0;0 3",
    "WS RLO LRE EN;3;0 x x 2;0 3",
    "R ES RLE RLO;2;1 0 x x;0 1",
    "PDI WS L;4;1 1 2;2 1 0",
    "NSM PDI PDF BN;4;1 1 x x;1 0",
    "FSI ON RLI;4;1 2 1;2 1 0",
    "R PDF S RLO;2;1 x 0 x;0 2",
    "ON LRE LRI RLO;4;1 x 1 x;2 0",
    "AL RLO ES RLO;5;1 x 3 x;2 0",
    "R BN RLO ET;2;1 x x 1;3 0",
    "RLI PDF RLO RLI;4;1 x x 1;3 0",
    "ET LRE LRE ET;4;1 x x 4;3 0",
    "L FSI EN;4;2 1 2;2 1 0",
    "EN BN ON RLE;4;2 x 1 x;2 0",
    "EN LRO RLO NSM;4;2 x x 3;0 3",
    "BN ON ES PDF;3;x 0 0 x;1 2",
    "BN EN RLE RLI;3;x 0 x 0;1 3",
    "LRE S LRI PDF;4;x 1 1 x;2 1",
    "BN ES AL PDF;5;x 1 1 x;2 1",
    "RLO PDI PDF L;3;x 1 x 0;1 3",
    "RLE AL BN EN;2;x 1 x 2;3 1",
    "LRO R WS RLO;2;x 2 0 x;1 2",
    "BN EN EN RLO;4;x 2 2 x;1 2",
    "LRE PDI BN NSM;7;x 2 x 2;1 3",
    "RLE ON ES RLO;4;x 3 3 x;2 1",
    "RLO EN LRO EN;4;x 3 x 4;3 1",
    "RLO LRE PDI RLI;3;x x 0 0;2 3",
    "LRO LRE WS S;4;x x 1 1;3 2",
    "PDF BN ET L;4;x x 1 2;3 2",
    "BN LRE NSM ET;7;x x 2 2;2 3",
    "BN RLE ES ET;4;x x 3 3;3 2",
    "RLE LRE FSI ON;4;x x 4 6;2 3",
    "ES L L PDF;3;0 0 0 x;0 1 2",
    "NSM CS CS PDF;3;0 0 0 x;0 1 2",
    "ON ON ON PDF;3;0 0 0 x;0 1 2",
    "S S AL PDF;2;0 0 1 x;0 1 2",
    "L NSM LRO FSI;3;0 0 x 0;0 1 3",
    "CS RLI LRE WS;3;0 0 x 0;0 1 3",
    "LRI LRI PDF PDI;3;0 0 x 0;0 1 3",
    "WS EN PDF R;2;0 0 x 1;0 1 3",
    "ON CS LRE EN;3;0 0 x 2;0 1 3",
    "S AL FSI PDF;2;0 1 0 x;0 1 2",
    "WS AL BN WS;2;0 1 x 0;0 1 3",
    "WS AN RLI LRE;3;0 2 0 x;0 1 2",
    "LRI EN LRO FSI;3;0 2 x 0;0 1 3",
    "LRI R BN EN;3;0 3 x 4;0 3 1",
    "NSM RLO LRI PDI;3;0 x 0 0;0 2 3",
    "PDI PDF RLI LRI;3;0 x 0 0;0 2 3",
    "NSM BN R ET;2;0 x 1 0;0 2 3",
    "EN RLE ES AN;3;0 x 1 2;0 3 2",
    "PDI PDF AN WS;3;0 x 2 0;0 2 3",
    "WS LRE WS AL;2;0 x 2 3;0 2 3",
    "RLI RLE EN R;3;0 x 4 3;0 3 2",
    "AL L BN ES;2;1 0 x 0;0 1 3",
    "AL NSM FSI PDF;5;1 1 1 x;2 1 0",
    "CS FSI RLI PDF;4;1 1 1 x;2 1 0",
    "ON ET AL PDF;5;1 1 1 x;2 1 0",
    "ES LRI ES PDF;4;1 1 2 x;2 1 0",
    "ON RLI L PDF;4;1 1 4 x;2 1 0",
    "ET ES PDF WS;4;1 1 x 1;3 1 0",
    "WS WS LRO FSI;4;1 1 x 1;3 1 0",
    "AL PDI LRE ON;5;1 1 x 2;3 1 0",
    "ET NSM RLO CS;4;1 1 x 3;3 1 0",
    "NSM FSI RLE AL;4;1 1 x 5;3 1 0",
    "PDI AN PDI LRE;4;1 2 1 x;2 1 0",
    "CS EN BN LRI;4;1 2 x 1;3 1 0",
    "LRI ET RLE CS;4;1 2 x 3;1 3 0",
    "LRI AL RLO NSM;4;1 3 x 3;3 1 0",
    "AL BN LRI AN;2;1 x 0 4;0 2 3",
    "CS PDF ON LRI;4;1 x 1 1;3 2 0",
    "FSI BN WS B;4;1 x 1 1;3 2 0",
    "R LRO NSM LRI;5;1 x 2 1;3 2 0",
    "CS LRO EN R;5;1 x 2 2;2 3 0",
    "ET RLO NSM B;4;1 x 3 1;3 2 0",
    "LRI RLE R ET;4;1 x 3 3;3 2 0",
    "CS RLE L NSM;4;1 x 4 4;2 3 0",
    "AN ON PDF B;3;2 0 x 0;0 1 3",
    "AN RLI FSI LRE;4;2 1 1 x;2 1 0",
    "AN FSI LRE LRI;4;2 1 x 1;3 1 0",
    "L PDI L RLE;4;2 2 2 x;0 1 2",
    "L RLO FSI LRI;4;2 x 1 1;3 2 0",
    "L PDF EN EN;4;2 x 2 2;0 2 3",
    "RLE FSI PDI LRI;3;x 0 0 0;1 2 3",
    "BN NSM FSI B;3;x 0 0 0;1 2 3",
    "BN RLI CS RLI;3;x 0 1 0;1 2 3",
    "RLO ET S PDI;3;x 1 0 0;1 2 3",
    "RLE R WS ET;2;x 1 1 1;3 2 1",
    "PDF ET R R;5;x 1 1 1;3 2 1",
    "BN PDI LRI RLI;4;x 1 1 1;3 2 1",
    "RLE R EN AL;2;x 1 2 1;3 2 1",
    "LRE S AL ET;5;x 1 3 2;2 3 1",
    "LRE EN FSI RLI;4;x 2 1 1;3 2 1",
    "LRO ES R B;2;x 2 2 0;1 2 3",
    "LRE PDI PDI NSM;7;x 2 2 2;1 2 3",
    "LRE RLI ON B;4;x 2 3 1;3 1 2",
    "RLE ES S EN;4;x 3 1 4;3 2 1",
    "RLO ET AN ON;4;x 3 3 3;3 2 1",
    "RLO RLI ON RLI;4;x 3 5 1;3 2 1",
    "L WS LRI LRI;3;0 0 0 0;0 1 2 3",
    "ES CS WS L;3;0 0 0 0;0 1 2 3",
    "CS EN CS RLI;3;0 0 0 0;0 1 2 3",
    "NSM LRI RLI S;3;0 0 0 0;0 1 2 3",
    "WS NSM PDI NSM;3;0 0 0 0;0 1 2 3",
    "RLI WS LRI WS;3;0 0 0 0;0 1 2 3",
    "EN ON WS AL;2;0 0 0 1;0 1 2 3",
    "EN ET FSI EN;3;0 0 0 2;0 1 2 3",
    "CS S LRI AN;3;0 0 0 4;0 1 2 3",
    "RLI PDI AL PDI;2;0 0 1 0;0 1 2 3",
    "L NSM AN PDI;3;0 0 2 0;0 1 2 3",
    "PDI RLI AN CS;3;0 0 2 1;0 1 3 2",
    "LRI S R EN;3;0 0 3 4;0 1 3 2",
    "FSI R RLI S;3;0 1 0 0;0 1 2 3",
    "RLI ES ES CS;3;0 1 1 1;0 3 2 1",
    "PDI AL AN EN;2;0 1 2 2;0 2 3 1",
    "ES AN LRI CS;3;0 2 0 2;0 1 2 3",
    "LRI WS L ET;3;0 2 2 2;0 1 2 3",
    "FSI LRI L R;3;0 2 4 5;0 1 2 3",
    "AL ET ON RLI;2;1 0 0 0;0 1 2 3",
    "R LRI AL CS;2;1 0 3 2;0 1 2 3",
    "R ON WS CS;5;1 1 1 1;3 2 1 0",
    "ES NSM NSM AL;5;1 1 1 1;3 2 1 0",
    "CS ES ES FSI;4;1 1 1 1;3 2 1 0",
    "NSM PDI R ON;5;1 1 1 1;3 2 1 0",
    "WS S ON PDI;4;1 1 1 1;3 2 1 0",
    "RLI FSI RLI LRI;4;1 1 1 1;3 2 1 0",
    "AL NSM CS AN;7;1 1 1 2;3 2 1 0",
    "ON AL ET AN;5;1 1 1 2;3 2 1 0",
    "R ES EN WS;2;1 1 2 0;2 1 0 3",
    "NSM ON L CS;4;1 1 2 1;3 2 1 0",
    "ET ES L AN;4;1 1 2 2;2 3 1 0",
    "AL RLI ON FSI;5;1 1 3 1;3 2 1 0",
    "R RLI L AL;5;1 1 4 3;3 2 1 0",
    "CS EN WS RLI;4;1 2 1 1;3 2 1 0",
    "PDI AN LRI S;4;1 2 1 1;3 2 1 0",
    "WS L EN WS;4;1 2 2 1;3 1 2 0",
    "LRI L RLI R;4;1 2 2 3;1 2 3 0",
    "RLI ON LRI S;4;1 3 1 1;3 2 1 0",
    "RLI WS AN ET;4;1 3 4 3;3 2 1 0",
    "AN S L ON;3;2 0 0 0;0 1 2 3",
    "L ES LRI S;4;2 1 1 1;3 2 1 0",
    "AN ES S FSI;4;2 1 1 1;3 2 1 0",
    "L WS AN ON;4;2 1 2 1;3 2 1 0",
    "AN NSM ET L;3;2 2 0 0;0 1 2 3",
    "L L EN CS;4;2 2 2 1;3 0 1 2",
};

static p32 sample_hex(string_address text, positive address_to at)
{
        p32 value = 0;

        for (;; (*at)++)
        {
                p8 c = (p8)text[*at];

                if (c >= '0' && c <= '9')
                        value = value * 16 + (c - '0');
                else if (c >= 'A' && c <= 'F')
                        value = value * 16 + (c - 'A' + 10);
                else if (c >= 'a' && c <= 'f')
                        value = value * 16 + (c - 'a' + 10);
                else
                        return value;
        }
}

static p32 sample_dec(string_address text, positive address_to at)
{
        p32 value = 0;

        while (text[*at] >= '0' && text[*at] <= '9')
                value = value * 10 + (p32)(text[(*at)++] - '0');
        return value;
}

//      A row of levels, "x" for one the algorithm removes, or of indices, up to the next semicolon or the end.
static p32 sample_list(string_address text, positive address_to at, p32 address_to out, p8 gone)
{
        p32 n = 0;

        while (text[*at] && text[*at] != ';')
        {
                if (text[*at] == 'x' && gone)
                        out[n++] = FONT_GONE, (*at)++;
                else if (text[*at] >= '0' && text[*at] <= '9')
                        out[n++] = sample_dec(text, at);
                else
                        (*at)++;
        }
        return n;
}

//      Whether the levels and order the algorithm gives w's n characters are the ones wanted.
static p8 bidi_agrees(font_work address_to w, p32 n, const p32 address_to levels, p32 nlevels, const p32 address_to order,
                        p32 norder)
{
        static p16 got[FONT_PARA];
        p8 same = n == nlevels;

        for (p32 i = 0; i < n && same; i++)
                same = w->level[i] == levels[i];

        p32 count = font_reorder(w->level, n, got);

        same = same && count == norder;
        for (p32 i = 0; i < count && same; i++)
                same = got[i] == order[i];
        return same;
}

static fn check_bidi(void)
{
        font_work address_to w = &engine.work;
        p32 levels[FONT_PARA], order[FONT_PARA];
        positive wrong = 0, ran = 0;

        for (p32 c = 0; c < 23; c++)
                wrong += font_bidi_class(class_samples[c]) != c;
        check("bidi: a code point of each of the 23 classes has it", wrong == 0);

        for (positive row = 0; row < sizeof(bidi_character_sample) / sizeof(bidi_character_sample[0]); row++)
        {
                string_address text = bidi_character_sample[row];
                positive at = 0;
                p32 n = 0;

                while (text[at] != ';')
                {
                        if (text[at] == ' ')
                                at++;
                        else
                                w->code[n++] = sample_hex(text, &at);
                }
                at++;

                p32 direction = sample_dec(text, &at), level;

                at++;
                level = sample_dec(text, &at);
                at++;

                p32 nlevels = sample_list(text, &at, levels, true), norder;

                at++;
                norder = sample_list(text, &at, order, false);

                p32 para = font_bidi(w, n, direction == 0 ? 0 : direction == 1 ? 1 : 2);

                ran++;
                if (para != level || !bidi_agrees(w, n, levels, nlevels, order, norder))
                {
                        wrong++;
                        if (wrong < 4)
                                string_format(log, "  BidiCharacterTest row %p: %s\n", row, text);
                }
        }
        log_flush();
        check("bidi: the sample of BidiCharacterTest.txt, levels and order and paragraph level", wrong == 0 && ran > 300);

        wrong = ran = 0;
        for (positive row = 0; row < sizeof(bidi_class_sample) / sizeof(bidi_class_sample[0]); row++)
        {
                string_address text = bidi_class_sample[row];
                positive at = 0;
                p32 n = 0;

                while (text[at] != ';')
                {
                        if (text[at] == ' ')
                        {
                                at++;
                                continue;
                        }

                        positive from = at;
                        p32 c = 0;

                        while (text[at] != ' ' && text[at] != ';')
                                at++;
                        for (; c < 23; c++)
                        {
                                positive k = 0;

                                while (class_names[c][k] && class_names[c][k] == text[from + k])
                                        k++;
                                if (!class_names[c][k] && from + k == at)
                                        break;
                        }
                        w->code[n++] = class_samples[c < 23 ? c : 0];
                }
                at++;

                p32 bits = sample_hex(text, &at), nlevels, norder;

                at++;
                nlevels = sample_list(text, &at, levels, true);
                at++;
                norder = sample_list(text, &at, order, false);
                for (p32 d = 0; d < 3; d++)
                {
                        if (!(bits & (1u << d)))
                                continue;
                        font_bidi(w, n, d == 0 ? 2 : d == 1 ? 0 : 1);
                        ran++;
                        if (!bidi_agrees(w, n, levels, nlevels, order, norder))
                        {
                                wrong++;
                                if (wrong < 4)
                                        string_format(log, "  BidiTest row %p direction %p: %s\n", row, d, text);
                        }
                }
        }
        log_flush();
        check("bidi: the sample of BidiTest.txt, levels and order, in all three directions", wrong == 0 && ran > 300);
}


//      Sequences the engine shapes, and the glyphs hb-shape 14.6 (--shapers=ot, --remove-default-ignorables, right to
//      left, script Arab or Hebr, with the Noto Sans Arabic and Hebrew of this tree) gives for each, in reading order:
//      glyph, advance, x offset and y offset in font units (1000 to the em).
#define SHAPE_CASES 409
static const p32 shape_codes[1816] = {
    1569, 1576, 1572, 1576, 1576, 1576, 1579, 1576, 1583, 1576, 1586, 1576, 1590, 1576,
    1593, 1576, 1603, 1576, 1606, 1576, 1610, 1576, 1649, 1576, 1653, 1576, 1656, 1576,
    1660, 1576, 1663, 1576, 1667, 1576, 1670, 1576, 1674, 1576, 1677, 1576, 1681, 1576,
    1684, 1576, 1688, 1576, 1691, 1576, 1695, 1576, 1698, 1576, 1702, 1576, 1705, 1576,
    1709, 1576, 1712, 1576, 1716, 1576, 1719, 1576, 1723, 1576, 1726, 1576, 1730, 1576,
    1733, 1576, 1737, 1576, 1740, 1576, 1744, 1576, 1747, 1576, 1788, 1576, 1873, 1576,
    1877, 1576, 1880, 1576, 1884, 1576, 1887, 1576, 1891, 1576, 1894, 1576, 1898, 1576,
    1901, 1576, 2161, 1576, 2164, 1576, 2168, 1576, 2171, 1576, 2175, 1576, 2178, 1576,
    2182, 1576, 2185, 1576, 2189, 1576, 2208, 1576, 2212, 1576, 2215, 1576, 2219, 1576,
    2222, 1576, 2226, 1576, 2229, 1576, 2233, 1576, 2236, 1576, 2240, 1576, 2243, 1576,
    2247, 1576, 1614, 1576, 1611, 1576, 1614, 1617, 1576, 1619, 1576, 1628, 1576, 1621,
    1616, 1604, 1617, 1604, 1648, 1604, 1620, 1604, 1552, 1604, 1620, 1617, 1575, 1615,
    1575, 1612, 1575, 1617, 1616, 1575, 1617, 1648, 1575, 1617, 1611, 1575, 1616, 1621,
    1607, 1618, 1607, 1617, 1614, 1607, 1621, 1607, 1750, 1607, 1617, 1620, 1610, 1616,
    1610, 1613, 1610, 1616, 1617, 1610, 1761, 1610, 1611, 1617, 1603, 1614, 1603, 1611,
    1603, 1614, 1617, 1603, 1619, 1603, 1628, 1603, 1621, 1616, 1593, 1617, 1593, 1648,
    1593, 1620, 1593, 1552, 1593, 1620, 1617, 1605, 1615, 1605, 1612, 1605, 1617, 1616,
    1605, 1617, 1648, 1605, 1617, 1611, 1605, 1616, 1621, 1705, 1618, 1705, 1617, 1614,
    1705, 1621, 1705, 1750, 1705, 1617, 1620, 1740, 1616, 1740, 1613, 1740, 1616, 1617,
    1740, 1761, 1740, 1611, 1617, 1575, 1604, 1587, 1604, 1575, 1605, 32, 1593, 1604,
    1610, 1603, 1605, 32, 1608, 1585, 1581, 1605, 1577, 32, 1575, 1604, 1604, 1607,
    32, 1608, 1576, 1585, 1603, 1575, 1578, 1607, 1605, 1585, 1581, 1576, 1575, 32,
    1576, 1603, 1605, 1603, 1578, 1575, 1576, 32, 1580, 1605, 1610, 1604, 1605, 1583,
    1585, 1587, 1577, 1575, 1604, 1604, 1594, 1577, 32, 1575, 1604, 1593, 1585, 1576,
    1610, 1577, 1605, 1581, 1605, 1583, 1576, 1616, 1587, 1618, 1605, 1616, 32, 1649,
    1604, 1604, 1614, 1617, 1607, 1616, 32, 1649, 1604, 1585, 1614, 1617, 1581, 1618,
    1605, 1614, 1648, 1606, 1616, 32, 1649, 1604, 1585, 1614, 1617, 1581, 1616, 1610,
    1605, 1616, 1575, 1604, 1581, 1605, 1583, 32, 1604, 1604, 1607, 32, 1585, 1576,
    32, 1575, 1604, 1593, 1575, 1604, 1605, 1610, 1606, 1604, 1575, 32, 1573, 1604,
    1607, 32, 1573, 1604, 1575, 32, 1575, 1604, 1604, 1607, 1571, 1607, 1604, 1575,
    32, 1608, 1587, 1607, 1604, 1575, 1588, 1603, 1585, 1575, 32, 1580, 1586, 1610,
    1604, 1575, 1589, 1576, 1575, 1581, 32, 1575, 1604, 1582, 1610, 1585, 1605, 1587,
    1575, 1569, 32, 1575, 1604, 1606, 1608, 1585, 1603, 1610, 1601, 32, 1581, 1575,
    1604, 1603, 1567, 1571, 1606, 1575, 32, 1576, 1582, 1610, 1585, 1548, 32, 1588,
    1603, 1585, 1575, 1605, 1575, 32, 1575, 1587, 1605, 1603, 1567, 1575, 1587, 1605,
    1610, 32, 1571, 1581, 1605, 1583, 1571, 1610, 1606, 32, 1575, 1604, 1605, 1603,
    1578, 1576, 1577, 1567, 1607, 1584, 1575, 32, 1575, 1604, 1603, 1578, 1575, 1576,
    32, 1605, 1601, 1610, 1583, 32, 1580, 1583, 1575, 1575, 1604, 1588, 1605, 1587,
    32, 1608, 1575, 1604, 1602, 1605, 1585, 1575, 1604, 1602, 1575, 1607, 1585, 1577,
    32, 1608, 1583, 1605, 1588, 1602, 32, 1608, 1576, 1594, 1583, 1575, 1583, 1580,
    1575, 1605, 1593, 1577, 32, 1575, 1604, 1605, 1604, 1603, 32, 1587, 1593, 1608,
    1583, 1605, 1572, 1587, 1587, 1577, 32, 1575, 1604, 1571, 1576, 1581, 1575, 1579,
    32, 1575, 1604, 1593, 1604, 1605, 1610, 1577, 1573, 1604, 1609, 32, 1575, 1604,
    1604, 1602, 1575, 1569, 1578, 1614, 1593, 1618, 1604, 1616, 1610, 1605, 1605, 1615,
    1581, 1614, 1605, 1614, 1617, 1583, 1575, 1614, 1604, 1618, 1593, 1614, 1585, 1614,
    1576, 1616, 1610, 1614, 1617, 1577, 1601, 1616, 1610, 32, 1575, 1604, 1618, 1576,
    1614, 1610, 1618, 1578, 1616, 1593, 1614, 1604, 1614, 1609, 32, 1575, 1604, 1591,
    1614, 1617, 1575, 1608, 1616, 1604, 1614, 1577, 1616, 1610, 1614, 1603, 1618, 1578,
    1615, 1576, 1615, 32, 1575, 1604, 1591, 1614, 1617, 1575, 1604, 1616, 1576, 1615,
    1602, 1615, 1585, 1618, 1570, 1606, 1605, 1614, 1587, 1618, 1572, 1615, 1608, 1604,
    1588, 1615, 1572, 1615, 1608, 1606, 1605, 1615, 1572, 1618, 1605, 1616, 1606, 1587,
    1614, 1605, 1614, 1575, 1569, 1607, 1648, 1584, 1614, 1575, 1584, 1648, 1604, 1616,
    1603, 1614, 1575, 1604, 1585, 1614, 1617, 1581, 1618, 1605, 1648, 1606, 1604, 1614,
    1575, 1604, 1616, 1571, 1614, 1606, 1614, 1617, 1573, 1616, 1606, 1614, 1617, 1571,
    1614, 1606, 1614, 1617, 1570, 1605, 1616, 1606, 1587, 1604, 1575, 1605, 1601, 1575,
    1585, 1587, 1740, 1662, 1575, 1585, 1587, 1740, 1711, 1604, 1670, 1575, 1740, 1688,
    1575, 1604, 1607, 1586, 1606, 1583, 1711, 1740, 1582, 1608, 1588, 32, 1570, 1605,
    1583, 1740, 1583, 1575, 1740, 1585, 1575, 1606, 1578, 1607, 1585, 1575, 1606, 1575,
    1589, 1601, 1607, 1575, 1606, 1705, 1578, 1575, 1576, 32, 1582, 1608, 1576, 1605,
    1606, 32, 1583, 1575, 1606, 1588, 1580, 1608, 32, 1607, 1587, 1578, 1605, 1606,
    1575, 1605, 32, 1588, 1605, 1575, 32, 1670, 1740, 1587, 1578, 1567, 1583, 1608,
    1587, 1578, 32, 1583, 1575, 1588, 1578, 1606, 1588, 1705, 1585, 1575, 1585, 1583,
    1608, 1662, 1575, 1705, 1587, 1578, 1575, 1606, 1604, 1575, 1729, 1608, 1585, 1705,
    1585, 1575, 1670, 1740, 1657, 1726, 1740, 1705, 32, 1729, 1746, 1576, 1726, 1575,
    1574, 1740, 1705, 1740, 1575, 32, 1581, 1575, 1604, 32, 1729, 1746, 1605, 1740,
    1722, 32, 1657, 1726, 1740, 1705, 32, 1729, 1608, 1722, 1588, 1705, 1585, 1740,
    1729, 1570, 1662, 32, 1705, 1575, 32, 1606, 1575, 1605, 32, 1705, 1740, 1575,
    32, 1729, 1746, 1729, 1606, 1583, 1608, 1587, 1578, 1575, 1606, 1672, 1575, 1705,
    1657, 1585, 1576, 1670, 1617, 1746, 1575, 1604, 1604, 1729, 1605, 1587, 1580, 1583,
    1705, 1578, 1575, 1576, 1740, 1722, 1604, 1681, 1705, 1740, 1711, 1726, 1585, 1729,
    1746, 1608, 1729, 1662, 1726, 1608, 1604, 1600, 1576, 1600, 1600, 1600, 1576, 1603,
    1600, 1600, 1600, 1578, 1575, 1576, 1605, 1581, 1600, 1600, 1600, 1605, 1583, 1604,
    1600, 1600, 1604, 1576, 32, 1576, 32, 1576, 1576, 1576, 32, 1576, 1576, 1548,
    1563, 1567, 40, 1576, 41, 171, 1576, 187, 91, 1576, 93, 123, 1576, 125,
    1576, 1548, 1576, 1576, 46, 1576, 1576, 45, 1576, 1604, 1575, 32, 1604, 1575,
    32, 1604, 1575, 1604, 1575, 1604, 1575, 1604, 1604, 1575, 65275, 65271, 65273, 65269,
    65010, 65018, 65021, 65167, 65169, 65170, 65168, 65166, 65276, 1609, 1574, 1572, 1577, 1607,
    1607, 1575, 1607, 1608, 1607, 1610, 1606, 1581, 1606, 1571, 1606, 1578, 1605, 1603,
    1604, 1605, 1577, 1603, 1604, 1605, 1575, 1578, 1605, 1603, 1578, 1576, 1577, 1605,
    1603, 1578, 1608, 1576, 1610, 1603, 1578, 1576, 1603, 1575, 1578, 1576, 1603, 1578,
    1576, 1580, 1605, 1610, 1604, 1577, 1580, 1605, 1575, 1604, 1602, 1604, 1605, 1602,
    1605, 1585, 1601, 1580, 1585, 1594, 1585, 1608, 1576, 1590, 1610, 1575, 1569, 1592,
    1604, 1575, 1605, 1591, 1601, 1604, 1593, 1575, 1604, 1605, 1593, 1592, 1610, 1605,
    1582, 1585, 1608, 1580, 1581, 1585, 1608, 1601, 1580, 1583, 1608, 1604, 1579, 1604,
    1575, 1579, 1577, 1588, 1580, 1585, 1577, 1589, 1581, 1585, 1575, 1569, 1586, 1607,
    1585, 1577, 1585, 1580, 1604, 1584, 1607, 1576, 1583, 1575, 1585, 1610, 1608, 1605,
    1571, 1610, 1575, 1605, 1571, 1587, 1576, 1608, 1593, 1588, 1607, 1585, 1587, 1606,
    1577, 1575, 1604, 1587, 1604, 1575, 1605, 32, 1593, 1604, 1610, 1603, 1605, 32,
    1608, 1585, 1581, 1605, 1577, 32, 1575, 1604, 1604, 1607, 32, 1608, 1576, 1585,
    1603, 1575, 1578, 1607, 1604, 1575, 32, 1573, 1604, 1607, 32, 1573, 1604, 1575,
    32, 1575, 1604, 1604, 1607, 1575, 1587, 1605, 1610, 32, 1571, 1581, 1605, 1583,
    1578, 1614, 1593, 1618, 1604, 1616, 1610, 1605, 1588, 1615, 1572, 1615, 1608, 1606,
    1573, 1616, 1606, 1614, 1617, 1688, 1575, 1604, 1607, 1606, 1575, 1605, 32, 1588,
    1605, 1575, 32, 1670, 1740, 1587, 1578, 1567, 1576, 1726, 1575, 1574, 1740, 1575,
    1604, 1604, 1729, 1576, 1600, 1600, 1600, 1576, 1563, 1576, 45, 1576, 65010, 65275,
    1607, 1605, 1603, 1578, 1576, 1577, 1602, 1605, 1585, 1582, 1585, 1608, 1580, 1584,
    1607, 1576, 1576, 8205, 1576, 1576, 8204, 1576, 1576, 1600, 1576, 1576, 8205, 1575,
    1576, 8204, 1575, 1604, 8205, 1575, 1604, 8204, 1575, 1575, 8205, 1576, 1575, 8204,
    1576, 8205, 1576, 1576, 8205, 8204, 1576, 1576, 8204, 8205, 8204, 1576, 8205, 1614,
    1576, 1576, 1614, 8205, 1576, 1604, 1614, 8205, 1575, 1604, 1600, 1575, 1604, 1600,
    1600, 1575, 1576, 8205, 8205, 1576, 8206, 1576, 8207, 1576, 1564, 1576, 1604, 1564,
    1575, 1488, 1488, 1468, 1473, 1489, 1468, 1490, 1456, 1490, 1473, 1468, 1491, 1469,
    1492, 1457, 1492, 1473, 1468, 1464, 1493, 1471, 1494, 1458, 1494, 1464, 1468, 1473,
    1495, 1473, 1496, 1459, 1496, 1460, 1468, 1497, 1474, 1498, 1460, 1498, 1468, 1460,
    1499, 1476, 1500, 1461, 1500, 1469, 1463, 1501, 1477, 1502, 1462, 1502, 1463, 1469,
    1503, 1479, 1504, 1463, 1504, 1451, 1505, 1468, 1463, 1506, 1464, 1506, 1425, 1507,
    1463, 1468, 1508, 1465, 1508, 1440, 1509, 1473, 1464, 1510, 1466, 1510, 1455, 1511,
    1464, 1473, 1512, 1467, 1513, 1513, 1468, 1473, 1514, 1468, 1513, 1500, 1493, 1501,
    1506, 1489, 1512, 1497, 1514, 1489, 1456, 1468, 1512, 1461, 1488, 1513, 1460, 1473,
    1497, 1514, 1513, 1464, 1473, 1500, 1493, 1465, 1501, 1506, 1460, 1489, 1456, 1512,
    1460, 1497, 1514, 1514, 1468, 1493, 1465, 1491, 1464, 1492, 1489, 1456, 1468, 1489,
    1463, 1511, 1464, 1468, 1513, 1464, 1473, 1492, 1497, 1456, 1512, 1493, 1468, 1513,
    1464, 1473, 1500, 1463, 1497, 1460, 1501, 1514, 1500, 32, 1488, 1489, 1497, 1489,
    1497, 1513, 1512, 1488, 1500, 1488, 1504, 1497, 32, 1488, 1493, 1492, 1489, 32,
    1488, 1493, 1514, 1498, 1502, 1492, 32, 1513, 1500, 1493, 1502, 1498, 1489, 1493,
    1511, 1512, 32, 1496, 1493, 1489, 1500, 1497, 1500, 1492, 32, 1496, 1493, 1489,
    1505, 1508, 1512, 1502, 1495, 1513, 1489, 1488, 1457, 1500, 1465, 1492, 1460, 1497,
    1501, 1513, 1473, 1493, 1465, 1508, 1464, 1512, 1511, 1460, 1497, 1489, 1468, 1493,
    1468, 1509, 1492, 1463, 1513, 1464, 1468, 1473, 1502, 1463, 1497, 1460, 1501, 32,
    1493, 1456, 1492, 1464, 1488, 1464, 1512, 1462, 1509, 1488, 1489, 1490, 1491, 1492,
    1493, 1494, 1495, 1496, 1497, 1499, 1500, 1502, 1504, 1505, 1506, 1508, 1510, 1511,
    1512, 1513, 1514, 1498, 1501, 1503, 1507, 1509, 1488, 1470, 1489, 1488, 1523, 1489,
    1524, 1490, 1513, 1464, 1473, 1500, 1493, 1465, 1501, 32, 1475, 40, 1513, 1500,
    1493, 1501, 41, 91, 1513, 1500, 1493, 1501, 93, 34, 1513, 1500, 1493, 1501,
    34, 1513, 1473, 1464, 1500, 1513, 1489, 1458, 1488, 1461, 1493, 1464, 1489, 1493,
    1511, 1512, 32, 1496, 1493, 1489, 1513, 1500, 1493, 1501, 1513, 1464, 1473, 1500,
    1493, 1465, 1501, 1489, 1456, 1468, 1489, 1463, 1511, 1464, 1468, 1513, 1464, 1473,
    1492, 1497, 1513, 1512, 1488, 1500, 1502, 1495, 1513, 1489, 1511, 1460, 1497, 1489,
    1468, 1493, 1468, 1509, 1498, 1501, 1503, 1507, 1509, 1513, 1464, 1473, 1500, 1493,
    1465, 1501, 32, 1475, 40, 1513, 1500, 1493, 1501, 41,
};
static const b16 shape_glyphs[9112] = {
    4, 411, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 297, 0, 103, -180,
    100, 472, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 292, 0, 54, -180,
    18, 373, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 28, 477, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 282, 0, 87, -112, 31, 396, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 282, 0, 979, -89, 38, 1306, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 48, 521, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 358, 0, 0, 0, 58, 882, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 282, 0, 80, -180, 16, 343, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 318, 0, 215, -232, 103, 760, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 300, 0, -74, 126, 13, 289, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 8, 238, 0, 0, 5, 411, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 16, 343, 0, 0, 5, 411, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 359, 0, 372, -143, 287, 0, 344, -165, 14, 993, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 296, 0, 54, -180, 18, 373, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 336, 0, 206, -66, 23, 659, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 323, 0, 116, -3, 26, 637, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 316, 0, 150, -3, 28, 477, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 318, 0, 93, -5, 29, 532, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 310, 0, 41, -79, 30, 367, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 316, 0, 51, -215, 31, 396, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 292, 0, 34, -112, 30, 367, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 323, 0, 241, -8, 36, 850, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 292, 0, 424, -89, 42, 788, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 316, 0, 167, -3, 52, 495, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 296, 0, 616, 38, 50, 983, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 60, 508, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 294, 0, 324, 65, 358, 0, 0, 0, 58, 882, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 360, 0, 228, 467, 304, 0, 2, 0,
    60, 508, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 293, 0, 459, 205,
    304, 0, 467, 0, 62, 877, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    292, 0, 2, 251, 70, 300, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    310, 0, 196, -135, 80, 685, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    96, 507, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 297, 0, 78, -61,
    88, 405, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 363, 0, 54, -40,
    101, 472, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 303, 0, 117, -93,
    98, 447, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 318, 0, 39, -5,
    18, 373, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 320, 0, 280, -233,
    103, 760, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 297, 0, 66, -479,
    106, 549, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 333, 0, 222, -95,
    282, 0, 166, 36, 46, 507, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    316, 0, 104, -3, 292, 0, 54, -180, 18, 373, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 328, 0, 378, -25, 14, 993, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 322, 0, 116, -3, 26, 637, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 296, 0, 679, -131, 34, 1209, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 291, 0, 199, -52, 48, 521, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 293, 0, 513, 102, 62, 877, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 316, 0, 208, -18, 78, 578, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 362, 0, 386, -1, 68, 695, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 291, 0, 353, -131, 36, 850, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 352, 0, 48, 56, 8, 238, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 353, 0, -19, 97, 9, 291, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 347, 0, -48, -313, 8, 238, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 282, 0, 37, 251, 352, 0, 31, 56, 9, 291, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 355, 0, -69, 295, 357, 0, 43, -266,
    8, 238, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 349, 0, -81, -349,
    9, 291, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 318, 0, -28, -5,
    20, 299, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 303, 0, 40, -147,
    16, 343, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 320, 0, 334, -22,
    62, 877, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 327, 0, 62, -5,
    16, 343, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 316, 0, 694, -23,
    292, 0, 616, 38, 50, 983, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    292, 0, 157, -101, 78, 578, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    334, 0, 24, -70, 99, 447, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    323, 0, 89, -8, 29, 532, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    285, 0, 66, -112, 30, 367, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    316, 0, 167, -3, 52, 495, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    314, 0, 40, -107, 30, 367, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    282, 0, 171, -48, 52, 495, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    312, 0, 351, -132, 14, 993, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    283, 0, 199, -52, 48, 521, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    310, 0, 423, 284, 68, 695, 0, 0, 370, 0, 324, -160, 316, 0, 410, -23,
    14, 993, 0, 0, 374, 0, 324, -159, 316, 0, 410, -23, 14, 993, 0, 0,
    370, 0, 307, 40, 367, 0, 315, -160, 316, 0, 410, -23, 14, 993, 0, 0,
    301, 0, 316, -220, 316, 0, 410, -23, 14, 993, 0, 0, 441, 0, 403, -211,
    316, 0, 410, -23, 14, 993, 0, 0, 426, 0, 365, -480, 325, 0, 380, -211,
    316, 0, 410, -23, 14, 993, 0, 0, 367, 0, 387, 256, 68, 695, 0, 0,
    410, 0, 480, 256, 68, 695, 0, 0, 297, 0, 429, 196, 68, 695, 0, 0,
    421, 0, 378, 256, 68, 695, 0, 0, 367, 0, 376, 472, 297, 0, 429, 196,
    68, 695, 0, 0, 371, 0, -45, 256, 8, 238, 0, 0, 375, 0, -55, 256,
    8, 238, 0, 0, 368, 0, -61, 56, 8, 238, 0, 0, 410, 0, 34, 456,
    367, 0, -42, 256, 8, 238, 0, 0, 374, 0, -50, 457, 367, 0, -42, 256,
    8, 238, 0, 0, 426, 0, 11, -269, 325, 0, 26, 0, 8, 238, 0, 0,
    378, 0, 199, -1, 84, 672, 0, 0, 370, 0, 147, 199, 367, 0, 155, -1,
    84, 672, 0, 0, 325, 0, 292, 0, 84, 672, 0, 0, 469, 0, 0, 0,
    84, 672, 0, 0, 367, 0, 144, 215, 297, 0, 197, -61, 84, 672, 0, 0,
    426, 0, 217, -418, 318, 0, 215, -232, 103, 760, 0, 0, 427, 0, 217, -418,
    318, 0, 215, -232, 103, 760, 0, 0, 368, 0, 26, -508, 318, 0, 215, -232,
    103, 760, 0, 0, 402, 0, 93, -398, 318, 0, 215, -232, 103, 760, 0, 0,
    374, 0, 37, -107, 367, 0, 45, -308, 318, 0, 215, -232, 103, 760, 0, 0,
    370, 0, 304, 70, 358, 0, 0, 0, 58, 882, 0, 0, 374, 0, 304, 71,
    358, 0, 0, 0, 58, 882, 0, 0, 370, 0, 287, 270, 367, 0, 295, 70,
    358, 0, 0, 0, 58, 882, 0, 0, 301, 0, 296, 10, 358, 0, 0, 0,
    58, 882, 0, 0, 441, 0, 327, -21, 358, 0, 0, 0, 58, 882, 0, 0,
    426, 0, 289, -290, 325, 0, 304, -21, 358, 0, 0, 0, 58, 882, 0, 0,
    367, 0, 70, 41, 46, 507, 0, 0, 410, 0, 163, 41, 46, 507, 0, 0,
    297, 0, 112, -19, 46, 507, 0, 0, 421, 0, 61, 41, 46, 507, 0, 0,
    367, 0, 59, 257, 297, 0, 112, -19, 46, 507, 0, 0, 371, 0, 89, -76,
    74, 484, 0, 0, 375, 0, 79, -76, 74, 484, 0, 0, 368, 0, 73, -276,
    74, 484, 0, 0, 410, 0, 168, 124, 367, 0, 92, -76, 74, 484, 0, 0,
    374, 0, 84, 125, 367, 0, 92, -76, 74, 484, 0, 0, 426, 0, 139, -269,
    325, 0, 154, 0, 74, 484, 0, 0, 378, 0, 541, 160, 62, 877, 0, 0,
    370, 0, 489, 360, 367, 0, 497, 160, 62, 877, 0, 0, 325, 0, 304, -21,
    62, 877, 0, 0, 469, 0, 0, 0, 62, 877, 0, 0, 367, 0, 486, 376,
    297, 0, 539, 100, 62, 877, 0, 0, 426, 0, 214, -232, 103, 760, 0, 0,
    427, 0, 214, -232, 103, 760, 0, 0, 368, 0, 26, -508, 103, 760, 0, 0,
    402, 0, 93, -398, 103, 760, 0, 0, 374, 0, 37, -107, 367, 0, 45, -308,
    103, 760, 0, 0, 85, 486, 0, 0, 287, 0, 5, -59, 21, 299, 0, 0,
    9, 291, 0, 0, 61, 412, 0, 0, 31, 356, -40, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 98, 447, 0, 0, 3, 260, 0, 0, 85, 486, 0, 0,
    70, 300, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0, 3, 260, 0, 0,
    287, 0, 101, 65, 85, 486, 0, 0, 78, 578, 0, 0, 27, 589, 0, 0,
    30, 327, -40, 0, 98, 447, 0, 0, 3, 260, 0, 0, 77, 562, 0, 0,
    60, 508, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 70, 300, 0, 0,
    49, 503, 0, 0, 3, 260, 0, 0, 74, 484, 0, 0, 11, 379, 0, 0,
    71, 220, 0, 0, 36, 850, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    77, 562, 0, 0, 60, 508, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    3, 260, 0, 0, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 69, 730, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 78, 578, 0, 0, 316, 0, 203, 2,
    27, 589, 0, 0, 3, 260, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0,
    287, 0, 101, 65, 85, 486, 0, 0, 37, 784, 0, 0, 30, 337, -30, 0,
    29, 532, 0, 0, 79, 525, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    31, 366, -30, 0, 48, 521, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    3, 260, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0, 282, 0, 199, -52,
    48, 521, 0, 0, 70, 300, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    29, 532, 0, 0, 78, 578, 0, 0, 26, 637, 0, 0, 79, 525, 0, 0,
    426, 0, 139, 0, 77, 562, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0,
    426, 0, 137, 0, 27, 589, 0, 0, 370, 0, -57, 93, 367, 0, -49, -107,
    31, 356, -40, 0, 72, 260, 0, 0, 300, 0, -73, 126, 12, 235, 0, 0,
    3, 260, 0, 0, 426, 0, 171, -232, 282, 0, 256, -168, 83, 730, 0, 0,
    410, 0, 213, 82, 370, 0, 137, -96, 78, 578, 0, 0, 378, 0, 39, -106,
    27, 589, 0, 0, 370, 0, -57, 93, 367, 0, -49, -107, 31, 356, -40, 0,
    72, 260, 0, 0, 300, 0, -73, 126, 12, 235, 0, 0, 3, 260, 0, 0,
    426, 0, 111, 0, 85, 486, 0, 0, 370, 0, -35, 456, 367, 0, -27, 256,
    70, 300, 0, 0, 72, 260, 0, 0, 300, 0, -73, 126, 12, 235, 0, 0,
    3, 260, 0, 0, 426, 0, 139, 0, 77, 562, 0, 0, 378, 0, 301, -126,
    36, 850, 0, 0, 426, 0, -35, -191, 316, 0, 31, -3, 19, 269, 0, 0,
    282, 0, 256, -168, 83, 730, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0,
    78, 578, 0, 0, 72, 260, 0, 0, 9, 291, 0, 0, 48, 521, 0, 0,
    72, 260, 0, 0, 8, 238, 0, 0, 3, 260, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 30, 367, 0, 0, 3, 260, 0, 0, 85, 486, 0, 0,
    70, 300, 0, 0, 72, 260, 0, 0, 3, 260, 0, 0, 29, 532, 0, 0,
    78, 578, 0, 0, 26, 637, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    85, 486, 0, 0, 70, 300, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    3, 260, 0, 0, 10, 363, 0, 0, 73, 219, 0, 0, 325, 0, 26, 0,
    8, 238, 0, 0, 3, 260, 0, 0, 85, 486, 0, 0, 72, 260, 0, 0,
    325, 0, 26, 0, 8, 238, 0, 0, 3, 260, 0, 0, 10, 363, 0, 0,
    73, 219, 0, 0, 11, 379, 0, 0, 71, 220, 0, 0, 86, 507, 0, 0,
    37, 784, 0, 0, 98, 447, 0, 0, 3, 260, 0, 0, 11, 379, 0, 0,
    71, 220, 0, 0, 87, 574, 0, 0, 297, 0, -16, 126, 12, 235, 0, 0,
    11, 379, 0, 0, 71, 220, 0, 0, 318, 0, -28, -5, 21, 299, 0, 0,
    282, 0, 57, -112, 31, 366, -30, 0, 316, 0, 203, 2, 27, 589, 0, 0,
    3, 260, 0, 0, 8, 238, 0, 0, 31, 371, -25, 0, 60, 508, 0, 0,
    292, 0, 286, -131, 37, 784, 0, 0, 31, 396, 0, 0, 318, 0, 39, -5,
    18, 373, 0, 0, 282, 0, 81, -111, 26, 637, 0, 0, 72, 260, 0, 0,
    8, 238, 0, 0, 3, 260, 0, 0, 22, 639, 0, 0, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 41, 903, 0, 0, 30, 367, 0, 0,
    100, 472, 0, 0, 282, 0, 80, -180, 16, 343, 0, 0, 72, 260, 0, 0,
    8, 238, 0, 0, 3, 260, 0, 0, 4, 411, 0, 0, 9, 291, 0, 0,
    36, 850, 0, 0, 79, 525, 0, 0, 183, 434, 0, 0, 358, 0, 0, 0,
    59, 922, 0, 0, 72, 260, 0, 0, 9, 291, 0, 0, 27, 589, 0, 0,
    3, 260, 0, 0, 282, 0, 774, -48, 51, 1098, 0, 0, 318, 0, 39, -5,
    18, 373, 0, 0, 61, 412, 0, 0, 8, 238, 0, 0, 31, 371, -25, 0,
    60, 508, 0, 0, 292, 0, 286, -131, 37, 784, 0, 0, 3, 260, 0, 0,
    176, 328, 0, 0, 31, 396, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0,
    282, 0, 81, -111, 26, 637, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    3, 260, 0, 0, 9, 291, 0, 0, 282, 0, 52, -59, 19, 269, 0, 0,
    297, 0, -16, 126, 12, 235, 0, 0, 183, 434, 0, 0, 358, 0, 0, 0,
    59, 922, 0, 0, 78, 578, 0, 0, 37, 784, 0, 0, 8, 238, 0, 0,
    3, 260, 0, 0, 9, 291, 0, 0, 79, 525, 0, 0, 29, 532, 0, 0,
    78, 578, 0, 0, 27, 589, 0, 0, 297, 0, -16, 126, 12, 235, 0, 0,
    3, 260, 0, 0, 318, 0, 185, -232, 104, 736, 0, 0, 78, 578, 0, 0,
    37, 784, 0, 0, 8, 238, 0, 0, 183, 434, 0, 0, 287, 0, 101, 65,
    85, 486, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 60, 508, 0, 0, 78, 578, 0, 0, 72, 260, 0, 0,
    8, 238, 0, 0, 3, 260, 0, 0, 282, 0, 256, -168, 83, 730, 0, 0,
    318, 0, -28, -5, 21, 299, 0, 0, 297, 0, -16, 126, 12, 235, 0, 0,
    8, 238, 0, 0, 29, 532, 0, 0, 316, 0, 203, 2, 27, 589, 0, 0,
    3, 260, 0, 0, 29, 532, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0,
    282, 0, 171, -48, 52, 495, 0, 0, 79, 525, 0, 0, 3, 260, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 60, 508, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    3, 260, 0, 0, 8, 238, 0, 0, 282, 0, 145, -6, 29, 532, 0, 0,
    87, 574, 0, 0, 31, 396, 0, 0, 78, 578, 0, 0, 287, 0, 104, -48,
    52, 495, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0, 98, 447, 0, 0,
    3, 260, 0, 0, 35, 1269, 0, 0, 78, 578, 0, 0, 292, 0, 286, -131,
    36, 850, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0, 28, 477, 0, 0,
    8, 238, 0, 0, 29, 532, 0, 0, 282, 0, 199, -52, 48, 521, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 98, 447, 0, 0, 3, 260, 0, 0,
    287, 0, 426, -132, 55, 808, 0, 0, 292, 0, 286, -131, 36, 850, 0, 0,
    79, 525, 0, 0, 28, 477, 0, 0, 98, 447, 0, 0, 3, 260, 0, 0,
    287, 0, 65, -6, 92, 405, 0, 0, 31, 356, -40, 0, 87, 574, 0, 0,
    9, 291, 0, 0, 287, 0, 104, -48, 52, 495, 0, 0, 72, 260, 0, 0,
    8, 238, 0, 0, 28, 477, 0, 0, 100, 472, 0, 0, 48, 521, 0, 0,
    37, 784, 0, 0, 3, 260, 0, 0, 358, 0, 0, 0, 59, 922, 0, 0,
    70, 300, 0, 0, 78, 578, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    3, 260, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0, 48, 521, 0, 0,
    79, 525, 0, 0, 9, 291, 0, 0, 316, 0, 203, 2, 27, 589, 0, 0,
    287, 0, 101, 65, 85, 486, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0,
    78, 578, 0, 0, 70, 300, 0, 0, 48, 521, 0, 0, 72, 260, 0, 0,
    8, 238, 0, 0, 3, 260, 0, 0, 292, 0, 344, -165, 14, 993, 0, 0,
    9, 291, 0, 0, 26, 637, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    297, 0, -6, 50, 10, 363, 0, 0, 73, 219, 0, 0, 8, 238, 0, 0,
    3, 260, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0, 36, 850, 0, 0,
    37, 784, 0, 0, 297, 0, 103, -180, 100, 472, 0, 0, 79, 525, 0, 0,
    4, 411, 0, 0, 9, 291, 0, 0, 287, 0, 104, -48, 52, 495, 0, 0,
    70, 300, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0, 3, 260, 0, 0,
    104, 736, 0, 0, 72, 260, 0, 0, 325, 0, 26, 0, 8, 238, 0, 0,
    77, 562, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 426, 0, 13, 0,
    70, 300, 0, 0, 378, 0, 147, -47, 48, 521, 0, 0, 370, 0, -20, 148,
    287, 0, 5, -59, 21, 299, 0, 0, 29, 532, 0, 0, 370, 0, 120, 104,
    367, 0, 128, -96, 78, 578, 0, 0, 370, 0, -6, -106, 26, 637, 0, 0,
    371, 0, 130, -100, 79, 525, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0,
    370, 0, 17, 25, 367, 0, 25, -175, 318, 0, 39, -5, 18, 373, 0, 0,
    426, 0, -35, -191, 316, 0, 31, -3, 19, 269, 0, 0, 370, 0, -30, -107,
    31, 366, -30, 0, 370, 0, 112, -47, 48, 521, 0, 0, 378, 0, 3, 256,
    72, 260, 0, 0, 370, 0, -33, 256, 8, 238, 0, 0, 426, 0, 321, -25,
    287, 0, 345, -165, 15, 1093, 0, 0, 378, 0, 69, -175, 318, 0, 39, -5,
    18, 373, 0, 0, 370, 0, -7, -175, 316, 0, 94, -3, 16, 343, 0, 0,
    378, 0, 3, 256, 72, 260, 0, 0, 8, 238, 0, 0, 3, 260, 0, 0,
    318, 0, 185, -232, 104, 736, 0, 0, 426, 0, 60, -5, 282, 0, 147, 38,
    53, 447, 0, 0, 426, 0, 111, 0, 287, 0, 101, 65, 85, 486, 0, 0,
    370, 0, -32, 256, 72, 260, 0, 0, 426, 0, 82, -240, 98, 447, 0, 0,
    9, 291, 0, 0, 370, 0, 226, 111, 367, 0, 234, -89, 44, 710, 0, 0,
    72, 260, 0, 0, 8, 238, 0, 0, 3, 260, 0, 0, 104, 736, 0, 0,
    370, 0, -18, 256, 70, 300, 0, 0, 370, 0, 102, -19, 49, 503, 0, 0,
    371, 0, 313, -160, 316, 0, 387, -23, 15, 1093, 0, 0, 426, 0, -12, 0,
    72, 260, 0, 0, 9, 291, 0, 0, 370, 0, 226, 111, 367, 0, 234, -89,
    44, 710, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0, 3, 260, 0, 0,
    371, 0, 313, -160, 316, 0, 387, -23, 15, 1093, 0, 0, 371, 0, 17, 27,
    287, 0, 54, -180, 18, 373, 0, 0, 378, 0, 55, 147, 60, 508, 0, 0,
    370, 0, -15, -54, 318, 0, -28, -5, 21, 299, 0, 0, 282, 0, 256, -168,
    80, 685, 0, 0, 301, 0, -57, 126, 12, 235, 0, 0, 378, 0, 10, -107,
    31, 371, -25, 0, 371, 0, 43, 245, 287, 0, 80, 38, 53, 447, 0, 0,
    68, 695, 0, 0, 98, 447, 0, 0, 371, 0, 47, 96, 297, 0, 103, -180,
    100, 472, 0, 0, 378, 0, 301, -126, 36, 850, 0, 0, 370, 0, 142, -100,
    79, 525, 0, 0, 282, 0, 256, -168, 80, 685, 0, 0, 98, 447, 0, 0,
    371, 0, 47, 96, 297, 0, 103, -180, 100, 472, 0, 0, 371, 0, 257, 186,
    292, 0, 286, -131, 37, 784, 0, 0, 282, 0, 256, -168, 83, 730, 0, 0,
    426, 0, 121, 0, 79, 525, 0, 0, 378, 0, 94, 96, 297, 0, 103, -180,
    100, 472, 0, 0, 371, 0, 130, -100, 79, 525, 0, 0, 4, 411, 0, 0,
    9, 291, 0, 0, 370, 0, 137, -96, 78, 578, 0, 0, 370, 0, 266, -126,
    37, 784, 0, 0, 8, 238, 0, 0, 370, 0, 49, 201, 282, 0, 145, -6,
    29, 532, 0, 0, 410, 0, 159, -1, 87, 574, 0, 0, 370, 0, 304, 70,
    358, 0, 0, 0, 59, 922, 0, 0, 426, 0, -12, 0, 72, 260, 0, 0,
    410, 0, 128, 201, 282, 0, 140, -6, 28, 477, 0, 0, 282, 0, 256, -168,
    83, 730, 0, 0, 410, 0, 221, -96, 78, 578, 0, 0, 378, 0, 39, -106,
    27, 589, 0, 0, 370, 0, -57, 93, 367, 0, -49, -107, 31, 356, -40, 0,
    72, 260, 0, 0, 8, 238, 0, 0, 10, 363, 0, 0, 370, 0, -114, 256,
    73, 219, 0, 0, 370, 0, 143, 239, 367, 0, 151, 39, 282, 0, 256, -168,
    80, 685, 0, 0, 370, 0, -50, 326, 297, 0, -6, 50, 10, 363, 0, 0,
    426, 0, -50, 0, 73, 219, 0, 0, 370, 0, 143, 239, 367, 0, 151, 39,
    282, 0, 256, -168, 80, 685, 0, 0, 426, 0, 11, -269, 325, 0, 26, 0,
    8, 238, 0, 0, 370, 0, 143, 239, 367, 0, 151, 39, 282, 0, 256, -168,
    80, 685, 0, 0, 370, 0, -60, 402, 297, 0, -16, 126, 12, 235, 0, 0,
    282, 0, 256, -168, 83, 730, 0, 0, 426, 0, 121, 0, 79, 525, 0, 0,
    301, 0, -57, 126, 12, 235, 0, 0, 74, 484, 0, 0, 11, 379, 0, 0,
    71, 220, 0, 0, 37, 784, 0, 0, 104, 736, 0, 0, 37, 784, 0, 0,
    30, 337, -30, 0, 9, 291, 0, 0, 282, 0, 147, 38, 53, 447, 0, 0,
    104, 736, 0, 0, 37, 784, 0, 0, 30, 337, -30, 0, 9, 291, 0, 0,
    323, 0, -32, -8, 21, 299, 0, 0, 69, 730, 0, 0, 304, 0, 2, 0,
    61, 412, 0, 0, 103, 760, 0, 0, 9, 291, 0, 0, 323, 0, 134, -3,
    27, 589, 0, 0, 85, 486, 0, 0, 72, 260, 0, 0, 8, 238, 0, 0,
    292, 0, 9, -112, 30, 342, -25, 0, 104, 736, 0, 0, 304, 0, 2, 0,
    61, 412, 0, 0, 29, 532, 0, 0, 282, 0, 52, -59, 19, 269, 0, 0,
    282, 0, 71, -112, 30, 337, -30, 0, 29, 532, 0, 0, 318, 0, -28, -5,
    21, 299, 0, 0, 29, 532, 0, 0, 79, 525, 0, 0, 301, 0, -57, 126,
    12, 235, 0, 0, 3, 260, 0, 0, 292, 0, 679, -131, 34, 1209, 0, 0,
    100, 472, 0, 0, 282, 0, 91, -111, 27, 589, 0, 0, 282, 0, 256, -168,
    80, 685, 0, 0, 8, 238, 0, 0, 31, 371, -25, 0, 318, 0, -28, -5,
    21, 299, 0, 0, 8, 238, 0, 0, 282, 0, 256, -168, 80, 685, 0, 0,
    8, 238, 0, 0, 31, 371, -25, 0, 86, 507, 0, 0, 287, 0, 5, -59,
    21, 299, 0, 0, 282, 0, 256, -168, 80, 685, 0, 0, 9, 291, 0, 0,
    86, 507, 0, 0, 282, 0, 171, -48, 52, 495, 0, 0, 41, 903, 0, 0,
    8, 238, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 100, 472, 0, 0,
    282, 0, 91, -111, 27, 589, 0, 0, 3, 260, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    61, 412, 0, 0, 77, 562, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    36, 850, 0, 0, 87, 574, 0, 0, 3, 260, 0, 0, 100, 472, 0, 0,
    316, 0, 185, 2, 26, 637, 0, 0, 292, 0, 286, -131, 36, 850, 0, 0,
    282, 0, 52, -59, 19, 269, 0, 0, 8, 238, 0, 0, 28, 477, 0, 0,
    3, 260, 0, 0, 282, 0, 256, -168, 83, 730, 0, 0, 79, 525, 0, 0,
    183, 434, 0, 0, 287, 0, 345, -165, 15, 1093, 0, 0, 36, 850, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 323, 0, 134, -3, 27, 589, 0, 0,
    3, 260, 0, 0, 9, 291, 0, 0, 78, 578, 0, 0, 292, 0, 286, -131,
    37, 784, 0, 0, 3, 260, 0, 0, 74, 484, 0, 0, 9, 291, 0, 0,
    282, 0, 52, -59, 19, 269, 0, 0, 282, 0, 256, -168, 83, 730, 0, 0,
    287, 0, 54, -180, 18, 373, 0, 0, 292, 0, 286, -131, 37, 784, 0, 0,
    8, 238, 0, 0, 28, 477, 0, 0, 3, 260, 0, 0, 287, 0, 345, -165,
    15, 1093, 0, 0, 37, 784, 0, 0, 98, 447, 0, 0, 28, 477, 0, 0,
    31, 396, 0, 0, 60, 508, 0, 0, 292, 0, 286, -131, 37, 784, 0, 0,
    98, 447, 0, 0, 28, 477, 0, 0, 30, 337, -30, 0, 8, 238, 0, 0,
    282, 0, 256, -168, 80, 685, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 36, 850, 0, 0, 61, 412, 0, 0, 9, 291, 0, 0,
    323, 0, -32, -8, 21, 299, 0, 0, 30, 367, 0, 0, 100, 472, 0, 0,
    91, 269, 0, 0, 10, 363, 0, 0, 73, 219, 0, 0, 104, 736, 0, 0,
    323, 0, 134, -3, 27, 589, 0, 0, 8, 238, 0, 0, 31, 371, -25, 0,
    61, 412, 0, 0, 106, 549, 0, 0, 91, 269, 0, 0, 3, 260, 0, 0,
    63, 975, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 96, 507, 0, 0,
    310, 0, 12, -26, 21, 299, 0, 0, 104, 736, 0, 0, 297, 0, -2, -114,
    19, 269, 0, 0, 9, 291, 0, 0, 96, 507, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 106, 549, 0, 0, 91, 269, 0, 0, 3, 260, 0, 0,
    68, 695, 0, 0, 9, 291, 0, 0, 27, 589, 0, 0, 3, 260, 0, 0,
    9, 291, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 61, 412, 0, 0,
    80, 685, 0, 0, 100, 472, 0, 0, 91, 269, 0, 0, 3, 260, 0, 0,
    63, 975, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 96, 507, 0, 0,
    310, 0, 12, -26, 21, 299, 0, 0, 3, 260, 0, 0, 83, 730, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 79, 525, 0, 0, 89, 479, 0, 0,
    318, 0, -28, -5, 21, 299, 0, 0, 31, 366, -30, 0, 60, 508, 0, 0,
    292, 0, 286, -131, 37, 784, 0, 0, 106, 549, 0, 0, 91, 269, 0, 0,
    3, 260, 0, 0, 9, 291, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0,
    61, 412, 0, 0, 3, 260, 0, 0, 74, 484, 0, 0, 9, 291, 0, 0,
    282, 0, 52, -59, 19, 269, 0, 0, 3, 260, 0, 0, 9, 291, 0, 0,
    61, 412, 0, 0, 3, 260, 0, 0, 323, 0, 341, -28, 14, 993, 0, 0,
    301, 0, -57, 126, 12, 235, 0, 0, 282, 0, 256, -168, 80, 685, 0, 0,
    9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 37, 784, 0, 0,
    98, 447, 0, 0, 29, 532, 0, 0, 282, 0, 80, -180, 16, 343, 0, 0,
    91, 269, 0, 0, 31, 396, 0, 0, 310, 0, 61, -147, 18, 373, 0, 0,
    61, 412, 0, 0, 8, 238, 0, 0, 310, 0, 80, 27, 28, 477, 0, 0,
    106, 549, 0, 0, 367, 0, -15, -106, 323, 0, 116, -3, 26, 637, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 89, 479, 0, 0, 70, 300, 0, 0,
    72, 260, 0, 0, 8, 238, 0, 0, 29, 532, 0, 0, 316, 0, 185, 2,
    26, 637, 0, 0, 36, 850, 0, 0, 79, 525, 0, 0, 83, 730, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0,
    104, 736, 0, 0, 61, 412, 0, 0, 310, 0, -13, -79, 31, 356, -40, 0,
    72, 260, 0, 0, 31, 396, 0, 0, 96, 507, 0, 0, 304, 0, 2, 0,
    61, 412, 0, 0, 106, 549, 0, 0, 91, 269, 0, 0, 88, 405, 0, 0,
    98, 447, 0, 0, 68, 695, 0, 0, 100, 472, 0, 0, 96, 507, 0, 0,
    323, 0, -32, -8, 21, 299, 0, 0, 111, 270, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 111, 270, 0, 0, 111, 270, 0, 0, 111, 270, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 111, 270, 0, 0,
    111, 270, 0, 0, 111, 270, 0, 0, 61, 412, 0, 0, 29, 532, 0, 0,
    78, 578, 0, 0, 111, 270, 0, 0, 111, 270, 0, 0, 111, 270, 0, 0,
    26, 637, 0, 0, 79, 525, 0, 0, 111, 270, 0, 0, 72, 260, 0, 0,
    69, 730, 0, 0, 112, 470, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    3, 260, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 3, 260, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 3, 260, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 176, 328, 0, 0,
    177, 344, 0, 0, 183, 434, 0, 0, 0, 600, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 0, 600, 0, 0, 222, 509, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 223, 509, 0, 0, 0, 600, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 0, 600, 0, 0, 0, 600, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 0, 600, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    176, 328, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 172, 268, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 219, 322, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 10, 363, 0, 0, 73, 219, 0, 0, 3, 260, 0, 0,
    10, 363, 0, 0, 73, 219, 0, 0, 3, 260, 0, 0, 10, 363, 0, 0,
    73, 219, 0, 0, 10, 363, 0, 0, 73, 219, 0, 0, 10, 363, 0, 0,
    73, 219, 0, 0, 11, 379, 0, 0, 71, 220, 0, 0, 72, 260, 0, 0,
    1268, 582, 0, 0, 1264, 582, 0, 0, 1266, 582, 0, 0, 1262, 582, 0, 0,
    233, 1284, 0, 0, 236, 1142, 0, 0, 258, 9779, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 1270, 269, 0, 0, 1302, 343, 0, 0, 1337, 1093, 0, 0,
    9, 291, 0, 0, 1269, 599, 0, 0, 103, 760, 0, 0, 297, 0, 87, -368,
    103, 760, 0, 0, 297, 0, 103, -181, 98, 447, 0, 0, 287, 0, 65, -6,
    92, 405, 0, 0, 84, 672, 0, 0, 9, 291, 0, 0, 87, 574, 0, 0,
    100, 472, 0, 0, 87, 574, 0, 0, 318, 0, 185, -232, 104, 736, 0, 0,
    87, 574, 0, 0, 282, 0, 256, -168, 83, 730, 0, 0, 26, 637, 0, 0,
    282, 0, 52, -59, 19, 269, 0, 0, 77, 562, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 282, 0, 52, -59, 19, 269, 0, 0, 297, 0, -16, 126,
    12, 235, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0, 78, 578, 0, 0,
    70, 300, 0, 0, 61, 412, 0, 0, 287, 0, 344, -165, 14, 993, 0, 0,
    9, 291, 0, 0, 78, 578, 0, 0, 70, 300, 0, 0, 61, 412, 0, 0,
    287, 0, 101, 65, 85, 486, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    287, 0, 54, -180, 18, 373, 0, 0, 60, 508, 0, 0, 79, 525, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 100, 472, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 60, 508, 0, 0, 79, 525, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 60, 508, 0, 0,
    318, 0, -28, -5, 21, 299, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    287, 0, 5, -59, 21, 299, 0, 0, 9, 291, 0, 0, 61, 412, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    61, 412, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0, 70, 300, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 78, 578, 0, 0, 316, 0, 203, 2,
    27, 589, 0, 0, 68, 695, 0, 0, 9, 291, 0, 0, 78, 578, 0, 0,
    316, 0, 203, 2, 27, 589, 0, 0, 77, 562, 0, 0, 70, 300, 0, 0,
    287, 0, 80, 38, 53, 447, 0, 0, 31, 396, 0, 0, 78, 578, 0, 0,
    287, 0, 80, 38, 53, 447, 0, 0, 31, 396, 0, 0, 316, 0, 185, 2,
    26, 637, 0, 0, 282, 0, 147, 38, 53, 447, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 98, 447, 0, 0, 31, 396, 0, 0, 282, 0, 189, -24,
    49, 503, 0, 0, 4, 411, 0, 0, 9, 291, 0, 0, 318, 0, 39, -5,
    18, 373, 0, 0, 282, 0, 576, -94, 41, 903, 0, 0, 74, 484, 0, 0,
    11, 379, 0, 0, 71, 220, 0, 0, 282, 0, 330, -94, 45, 677, 0, 0,
    69, 730, 0, 0, 282, 0, 171, -48, 52, 495, 0, 0, 45, 677, 0, 0,
    77, 562, 0, 0, 72, 260, 0, 0, 9, 291, 0, 0, 49, 503, 0, 0,
    77, 562, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 282, 0, 330, -94,
    44, 710, 0, 0, 49, 503, 0, 0, 333, 0, 275, -66, 22, 639, 0, 0,
    98, 447, 0, 0, 31, 396, 0, 0, 282, 0, 91, -111, 27, 589, 0, 0,
    282, 0, 683, 38, 50, 983, 0, 0, 98, 447, 0, 0, 31, 396, 0, 0,
    27, 589, 0, 0, 68, 695, 0, 0, 98, 447, 0, 0, 29, 532, 0, 0,
    316, 0, 203, 2, 27, 589, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0,
    292, 0, 5, -59, 21, 299, 0, 0, 11, 379, 0, 0, 71, 220, 0, 0,
    292, 0, 5, -59, 21, 299, 0, 0, 287, 0, 65, -6, 92, 405, 0, 0,
    31, 356, -40, 0, 316, 0, 185, 2, 26, 637, 0, 0, 292, 0, 286, -131,
    37, 784, 0, 0, 4, 411, 0, 0, 8, 238, 0, 0, 31, 371, -25, 0,
    26, 637, 0, 0, 41, 903, 0, 0, 287, 0, 65, -6, 92, 405, 0, 0,
    31, 356, -40, 0, 87, 574, 0, 0, 282, 0, 61, -112, 30, 327, -40, 0,
    69, 730, 0, 0, 316, 0, 203, 2, 27, 589, 0, 0, 30, 327, -40, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 87, 574, 0, 0, 282, 0, 140, -6,
    28, 477, 0, 0, 30, 367, 0, 0, 8, 238, 0, 0, 28, 477, 0, 0,
    74, 484, 0, 0, 100, 472, 0, 0, 318, 0, -28, -5, 21, 299, 0, 0,
    74, 484, 0, 0, 9, 291, 0, 0, 318, 0, -28, -5, 21, 299, 0, 0,
    297, 0, -16, 126, 12, 235, 0, 0, 46, 507, 0, 0, 100, 472, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 37, 784, 0, 0, 297, 0, -16, 126,
    12, 235, 0, 0, 31, 396, 0, 0, 86, 507, 0, 0, 292, 0, 286, -131,
    37, 784, 0, 0, 287, 0, 101, 65, 85, 486, 0, 0, 282, 0, 80, -180,
    16, 343, 0, 0, 37, 784, 0, 0, 85, 556, 0, 0, 287, 0, 0, 13,
    21, 359, 0, 0, 9, 318, 0, 0, 61, 454, 0, 0, 31, 410, -40, 0,
    316, 0, 27, -2, 19, 317, 0, 0, 98, 498, 0, 0, 3, 260, 0, 0,
    85, 556, 0, 0, 70, 344, 0, 0, 72, 297, 0, 0, 8, 273, 0, 0,
    3, 260, 0, 0, 287, 0, 105, 149, 85, 556, 0, 0, 78, 624, 0, 0,
    27, 629, 0, 0, 30, 369, -40, 0, 98, 498, 0, 0, 3, 260, 0, 0,
    77, 661, 0, 0, 60, 508, 0, 0, 318, 0, 48, -5, 18, 409, 0, 0,
    70, 344, 0, 0, 49, 557, 0, 0, 3, 260, 0, 0, 74, 548, 0, 0,
    11, 417, 0, 0, 71, 264, 0, 0, 36, 1019, 0, 0, 72, 297, 0, 0,
    8, 273, 0, 0, 85, 556, 0, 0, 70, 344, 0, 0, 72, 297, 0, 0,
    8, 273, 0, 0, 3, 260, 0, 0, 10, 376, 0, 0, 73, 256, 0, 0,
    325, 0, 30, 0, 8, 273, 0, 0, 3, 260, 0, 0, 85, 556, 0, 0,
    72, 297, 0, 0, 325, 0, 30, 0, 8, 273, 0, 0, 3, 260, 0, 0,
    10, 376, 0, 0, 73, 256, 0, 0, 29, 571, 0, 0, 78, 624, 0, 0,
    27, 629, 0, 0, 297, 0, -21, 144, 12, 271, 0, 0, 3, 260, 0, 0,
    318, 0, 239, -231, 104, 800, 0, 0, 78, 624, 0, 0, 37, 950, 0, 0,
    8, 273, 0, 0, 77, 661, 0, 0, 318, 0, 48, -5, 18, 409, 0, 0,
    426, 0, 25, -2, 70, 344, 0, 0, 378, 0, 159, 12, 48, 552, 0, 0,
    370, 0, 11, 238, 287, 0, 0, 13, 21, 359, 0, 0, 282, 0, 252, -136,
    80, 741, 0, 0, 98, 498, 0, 0, 371, 0, 60, 183, 297, 0, 105, -115,
    100, 524, 0, 0, 371, 0, 337, 282, 292, 0, 373, -62, 37, 950, 0, 0,
    370, 0, 163, 318, 367, 0, 171, 98, 282, 0, 252, -136, 80, 741, 0, 0,
    426, 0, 15, -275, 325, 0, 30, 0, 8, 273, 0, 0, 85, 556, 0, 0,
    72, 297, 0, 0, 8, 273, 0, 0, 292, 0, 67, -59, 30, 384, -25, 0,
    183, 512, 0, 0, 287, 0, 339, -106, 15, 1142, 0, 0, 36, 1019, 0, 0,
    318, 0, 48, -5, 18, 409, 0, 0, 323, 0, 154, 1, 27, 629, 0, 0,
    3, 260, 0, 0, 9, 318, 0, 0, 78, 624, 0, 0, 292, 0, 373, -62,
    37, 950, 0, 0, 3, 260, 0, 0, 74, 548, 0, 0, 9, 318, 0, 0,
    282, 0, 44, 13, 19, 317, 0, 0, 104, 800, 0, 0, 297, 0, -5, -54,
    19, 317, 0, 0, 9, 318, 0, 0, 96, 571, 0, 0, 316, 0, 27, -2,
    19, 317, 0, 0, 89, 586, 0, 0, 70, 344, 0, 0, 72, 297, 0, 0,
    8, 273, 0, 0, 316, 0, 400, -22, 15, 1142, 0, 0, 111, 289, 0, 0,
    111, 289, 0, 0, 111, 289, 0, 0, 316, 0, 27, -2, 19, 317, 0, 0,
    177, 340, 0, 0, 316, 0, 405, -22, 14, 1024, 0, 0, 219, 322, 0, 0,
    316, 0, 405, -22, 14, 1024, 0, 0, 233, 1470, 0, 0, 1268, 632, 0, 0,
    84, 771, 0, 0, 287, 0, 105, 149, 85, 556, 0, 0, 316, 0, 82, -2,
    16, 384, 0, 0, 287, 0, 50, -106, 18, 409, 0, 0, 60, 508, 0, 0,
    79, 582, 0, 0, 31, 450, 0, 0, 78, 624, 0, 0, 287, 0, 80, 129,
    53, 505, 0, 0, 333, 0, 334, -35, 22, 687, 0, 0, 98, 498, 0, 0,
    31, 450, 0, 0, 282, 0, 73, -17, 27, 629, 0, 0, 316, 0, 400, -22,
    15, 1142, 0, 0, 87, 641, 0, 0, 282, 0, 124, 49, 28, 517, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    316, 0, 410, -23, 14, 993, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    316, 0, 387, -23, 15, 1093, 0, 0, 111, 270, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 9, 291, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0,
    8, 238, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0,
    72, 260, 0, 0, 8, 238, 0, 0, 68, 695, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 8, 238, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0,
    8, 238, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 316, 0, 410, -23,
    14, 993, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 370, 0, 0, 0,
    316, 0, 31, -3, 19, 269, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0,
    370, 0, -35, -54, 316, 0, 31, -3, 19, 269, 0, 0, 9, 291, 0, 0,
    370, 0, -32, 256, 72, 260, 0, 0, 9, 291, 0, 0, 111, 270, 0, 0,
    72, 260, 0, 0, 9, 291, 0, 0, 111, 270, 0, 0, 111, 270, 0, 0,
    72, 260, 0, 0, 316, 0, 387, -23, 15, 1093, 0, 0, 316, 0, 31, -3,
    19, 269, 0, 0, 316, 0, 410, -23, 14, 993, 0, 0, 316, 0, 387, -23,
    15, 1093, 0, 0, 316, 0, 31, -3, 19, 269, 0, 0, 10, 363, 0, 0,
    73, 219, 0, 0, 3, 632, 0, 0, 15, 0, 200, -186, 100, 0, 0, 0,
    3, 632, 0, 0, 15, 0, 126, 0, 12, 572, 0, 0, 95, 0, 122, 0,
    34, 374, 0, 0, 15, 0, 26, 12, 100, 0, 0, 0, 34, 374, 0, 0,
    103, 0, 36, 0, 16, 542, 0, 0, 40, 0, 111, 0, 42, 627, 0, 0,
    79, 0, 158, 0, 15, 0, 214, 0, 100, 0, 0, 0, 42, 627, 0, 0,
    85, 0, 25, 0, 124, 301, 0, 0, 36, 0, -17, 0, 146, 330, 0, 0,
    79, 0, 32, 0, 15, 0, -65, 0, 100, 0, 0, 0, 146, 330, 0, 0,
    100, 0, 0, 0, 44, 662, 0, 0, 38, 0, 126, -3, 111, 634, 0, 0,
    45, 0, 232, -3, 15, 0, 234, 12, 111, 634, 0, 0, 104, 0, 48, 0,
    138, 295, 0, 0, 45, 0, 114, 226, 21, 545, 0, 0, 45, 0, 114, 226,
    15, 0, 115, 0, 21, 545, 0, 0, 123, 0, 89, 0, 52, 515, 0, 0,
    117, 0, 110, 0, 55, 522, 0, 0, 103, 0, 152, 0, 73, 0, 104, 0,
    55, 522, 0, 0, 57, 0, 266, 0, 23, 684, 0, 0, 91, 0, 218, 0,
    61, 660, 0, 0, 103, 0, 28, 0, 73, 0, 215, 0, 61, 660, 0, 0,
    80, 0, 0, 0, 24, 329, 0, 0, 73, 0, 28, 0, 67, 379, 0, 0,
    70, 0, 54, 0, 67, 379, 0, 0, 73, 0, 190, 0, 15, 0, 248, 0,
    89, 643, 0, 0, 79, 0, 143, 0, 10, 593, 0, 0, 20, 0, -123, 0,
    10, 593, 0, 0, 73, 0, 197, 222, 15, 0, 258, -26, 25, 620, 0, 0,
    46, 0, -31, 0, 75, 601, 0, 0, 109, 0, 138, 0, 75, 601, 0, 0,
    79, 0, 0, 0, 100, 0, 0, 0, 27, 529, 0, 0, 47, 0, -68, 0,
    115, 556, 0, 0, 60, 0, 150, 0, 115, 556, 0, 0, 79, 0, 233, 0,
    100, 0, 0, 0, 82, 627, 0, 0, 84, 0, 243, 0, 86, 523, 0, 0,
    96, 730, 0, 0, 15, 0, 363, -71, 100, 0, 539, 0, 96, 730, 0, 0,
    15, 0, 293, -7, 107, 685, 0, 0, 23, 684, 0, 0, 124, 291, -10, 0,
    55, 522, 0, 0, 96, 730, 0, 0, 107, 685, 0, 0, 138, 275, -20, 0,
    86, 523, 0, 0, 12, 547, -25, 0, 10, 593, 0, 0, 107, 685, 0, 0,
    138, 275, -20, 0, 45, 0, 284, 0, 100, 0, 539, 0, 96, 730, 0, 0,
    3, 612, -20, 0, 117, 0, 268, 0, 86, 523, 0, 0, 95, 0, 167, 0,
    15, 0, 101, 0, 12, 547, -25, 0, 23, 684, 0, 0, 46, 0, 82, 0,
    124, 291, -10, 0, 55, 522, 0, 0, 79, 0, 227, 0, 100, 0, 539, 0,
    96, 730, 0, 0, 107, 685, 0, 0, 138, 275, -20, 0, 45, 0, 319, 0,
    86, 523, 0, 0, 95, 0, 167, 0, 12, 547, -25, 0, 45, 0, 200, 0,
    10, 593, 0, 0, 42, 627, 0, 0, 79, 0, 207, 0, 16, 522, -20, 0,
    46, 0, 92, 0, 124, 301, 0, 0, 15, 0, 293, -7, 107, 685, 0, 0,
    42, 627, 0, 0, 79, 0, 227, 0, 100, 0, 539, 0, 96, 730, 0, 0,
    79, 0, 233, 0, 15, 0, 200, 7, 82, 627, 0, 0, 73, 0, 134, 0,
    12, 572, 0, 0, 95, 0, 192, 0, 15, 0, 126, 0, 12, 572, 0, 0,
    23, 684, 0, 0, 45, 0, 56, 0, 138, 265, -30, 0, 73, 0, 104, 0,
    55, 522, 0, 0, 79, 0, 227, 0, 100, 0, 539, 0, 96, 730, 0, 0,
    15, 0, -44, 0, 124, 301, 0, 0, 86, 523, 0, 0, 95, 0, 67, 0,
    138, 275, -20, 0, 12, 572, 0, 0, 138, 231, -64, 0, 12, 572, 0, 0,
    3, 632, 0, 0, 106, 270, 0, 0, 55, 522, 0, 0, 107, 681, -4, 0,
    55, 522, 0, 0, 3, 626, -6, 0, 86, 523, 0, 0, 96, 723, -7, 0,
    138, 295, 0, 0, 21, 545, 0, 0, 107, 685, 0, 0, 124, 301, 0, 0,
    3, 632, 0, 0, 106, 270, 0, 0, 12, 572, 0, 0, 42, 627, 0, 0,
    124, 301, 0, 0, 3, 632, 0, 0, 106, 270, 0, 0, 138, 295, 0, 0,
    67, 365, -14, 0, 3, 632, 0, 0, 21, 545, 0, 0, 61, 660, 0, 0,
    124, 301, 0, 0, 55, 522, 0, 0, 96, 730, 0, 0, 106, 270, 0, 0,
    42, 627, 0, 0, 61, 660, 0, 0, 12, 572, 0, 0, 124, 295, -6, 0,
    111, 634, 0, 0, 106, 270, 0, 0, 86, 523, 0, 0, 82, 627, 0, 0,
    124, 301, 0, 0, 12, 572, 0, 0, 12, 572, 0, 0, 124, 295, -6, 0,
    111, 634, 0, 0, 106, 270, 0, 0, 42, 627, 0, 0, 55, 522, 0, 0,
    138, 295, 0, 0, 55, 522, 0, 0, 86, 523, 0, 0, 75, 601, 0, 0,
    89, 631, -12, 0, 12, 572, 0, 0, 96, 706, -24, 0, 44, 662, 0, 0,
    61, 660, 0, 0, 23, 684, 0, 0, 138, 265, -30, 0, 45, 0, 215, 0,
    42, 627, 0, 0, 46, 0, -131, 0, 55, 522, 0, 0, 40, 0, 114, 0,
    3, 626, -6, 0, 86, 523, 0, 0, 79, 0, 138, 0, 75, 601, 0, 0,
    46, 0, 72, 0, 124, 281, -20, 0, 100, 0, 539, 0, 96, 730, 0, 0,
    27, 529, 0, 0, 15, 0, -44, 0, 124, 301, 0, 0, 15, 0, 126, 0,
    12, 572, 0, 0, 138, 231, -64, 0, 45, 0, 290, 0, 82, 627, 0, 0,
    27, 529, 0, 0, 91, 0, 265, 0, 86, 523, 0, 0, 79, 0, 167, 0,
    3, 632, 0, 0, 79, 0, 158, 0, 42, 627, 0, 0, 95, 0, 92, 0,
    124, 301, 0, 0, 106, 270, 0, 0, 23, 684, 0, 0, 45, 0, 56, 0,
    138, 265, -30, 0, 73, 0, 215, 0, 61, 660, 0, 0, 79, 0, 227, 0,
    15, 0, 363, -71, 100, 0, 539, 0, 96, 730, 0, 0, 73, 0, 158, 0,
    42, 627, 0, 0, 107, 685, 0, 0, 96, 730, 0, 0, 86, 523, 0, 0,
    82, 627, 0, 0, 115, 556, 0, 0, 75, 601, 0, 0, 10, 593, 0, 0,
    89, 643, 0, 0, 67, 379, 0, 0, 61, 660, 0, 0, 55, 522, 0, 0,
    52, 515, 0, 0, 138, 288, -7, 0, 111, 634, 0, 0, 44, 662, 0, 0,
    146, 330, 0, 0, 124, 301, 0, 0, 42, 627, 0, 0, 16, 522, -20, 0,
    34, 374, 0, 0, 12, 572, 0, 0, 3, 632, 0, 0, 27, 529, 0, 0,
    25, 620, 0, 0, 24, 329, 0, 0, 23, 684, 0, 0, 21, 545, 0, 0,
    12, 572, 0, 0, 59, 324, 0, 0, 3, 632, 0, 0, 34, 374, 0, 0,
    32, 365, 0, 0, 12, 572, 0, 0, 29, 186, 0, 0, 3, 632, 0, 0,
    105, 246, 0, 0, 106, 270, 0, 0, 23, 684, 0, 0, 46, 0, 82, 0,
    124, 291, -10, 0, 55, 522, 0, 0, 79, 0, 227, 0, 100, 0, 539, 0,
    96, 730, 0, 0, 0, 500, 0, 0, 23, 684, 0, 0, 124, 291, -10, 0,
    55, 522, 0, 0, 96, 730, 0, 0, 0, 500, 0, 0, 0, 500, 0, 0,
    23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0,
    0, 500, 0, 0, 0, 500, 0, 0, 23, 684, 0, 0, 124, 291, -10, 0,
    55, 522, 0, 0, 96, 730, 0, 0, 0, 500, 0, 0, 55, 522, 0, 0,
    79, 0, 227, 0, 100, 0, 539, 0, 96, 730, 0, 0, 96, 787, 0, 0,
    36, 0, 81, 0, 12, 594, 0, 0, 117, 0, 181, 0, 3, 676, 0, 0,
    79, 0, 37, 0, 124, 338, 0, 0, 12, 594, 0, 0, 124, 327, -11, 0,
    111, 660, 0, 0, 106, 270, 0, 0, 86, 542, 0, 0, 82, 654, 0, 0,
    124, 338, 0, 0, 12, 594, 0, 0, 23, 702, 0, 0, 124, 328, -10, 0,
    55, 556, 0, 0, 96, 787, 0, 0, 23, 702, 0, 0, 46, 0, 90, 0,
    124, 328, -10, 0, 55, 556, 0, 0, 79, 0, 240, 0, 100, 0, 562, 0,
    96, 787, 0, 0, 42, 658, 0, 0, 79, 0, 240, 0, 100, 0, 562, 0,
    96, 787, 0, 0, 79, 0, 236, 0, 15, 0, 210, 15, 82, 654, 0, 0,
    73, 0, 140, 0, 12, 594, 0, 0, 95, 0, 202, 0, 15, 0, 100, 0,
    12, 594, 0, 0, 55, 556, 0, 0, 3, 674, -2, 0, 86, 542, 0, 0,
    96, 772, -15, 0, 138, 332, 0, 0, 12, 594, 0, 0, 96, 760, -27, 0,
    44, 687, 0, 0, 61, 702, 0, 0, 27, 587, 0, 0, 15, 0, -61, 0,
    124, 338, 0, 0, 15, 0, 100, 0, 12, 594, 0, 0, 138, 273, -59, 0,
    45, 0, 298, 0, 82, 654, 0, 0, 27, 587, 0, 0, 25, 654, 0, 0,
    24, 355, 0, 0, 23, 702, 0, 0, 21, 570, 0, 0, 105, 272, 0, 0,
    106, 270, 0, 0, 23, 702, 0, 0, 46, 0, 90, 0, 124, 328, -10, 0,
    55, 556, 0, 0, 79, 0, 240, 0, 100, 0, 562, 0, 96, 787, 0, 0,
    0, 500, 0, 0, 23, 702, 0, 0, 124, 328, -10, 0, 55, 556, 0, 0,
    96, 787, 0, 0, 0, 500, 0, 0,
};
//      Per case: the face (0 and 1 Arabic Regular and Bold, 2 and 3 Hebrew), where its code points start and how many,
//      where its glyphs start and how many.
static const p32 shape_case[SHAPE_CASES][5] = {
    {0, 0, 1, 0, 1},
    {0, 1, 3, 1, 6},
    {0, 4, 1, 7, 2},
    {0, 5, 3, 9, 6},
    {0, 8, 1, 15, 1},
    {0, 9, 3, 16, 6},
    {0, 12, 1, 22, 2},
    {0, 13, 3, 24, 5},
    {0, 16, 1, 29, 2},
    {0, 17, 3, 31, 6},
    {0, 20, 1, 37, 2},
    {0, 21, 3, 39, 6},
    {0, 24, 1, 45, 2},
    {0, 25, 3, 47, 6},
    {0, 28, 1, 53, 3},
    {0, 29, 3, 56, 6},
    {0, 32, 1, 62, 2},
    {0, 33, 3, 64, 6},
    {0, 36, 1, 70, 2},
    {0, 37, 3, 72, 6},
    {0, 40, 1, 78, 2},
    {0, 41, 3, 80, 6},
    {0, 44, 1, 86, 2},
    {0, 45, 3, 88, 6},
    {0, 48, 1, 94, 2},
    {0, 49, 3, 96, 6},
    {0, 52, 1, 102, 2},
    {0, 53, 3, 104, 5},
    {0, 56, 1, 109, 3},
    {0, 57, 3, 112, 7},
    {0, 60, 1, 119, 3},
    {0, 61, 3, 122, 6},
    {0, 64, 1, 128, 2},
    {0, 65, 3, 130, 5},
    {0, 68, 1, 135, 2},
    {0, 69, 3, 137, 6},
    {0, 72, 1, 143, 2},
    {0, 73, 3, 145, 6},
    {0, 76, 1, 151, 2},
    {0, 77, 3, 153, 6},
    {0, 80, 1, 159, 3},
    {0, 81, 3, 162, 7},
    {0, 84, 1, 169, 2},
    {0, 85, 3, 171, 6},
    {0, 88, 1, 177, 2},
    {0, 89, 3, 179, 6},
    {0, 92, 1, 185, 2},
    {0, 93, 3, 187, 6},
    {0, 96, 1, 193, 2},
    {0, 97, 3, 195, 6},
    {0, 100, 1, 201, 2},
    {0, 101, 3, 203, 6},
    {0, 104, 1, 209, 2},
    {0, 105, 3, 211, 7},
    {0, 108, 1, 218, 3},
    {0, 109, 3, 221, 6},
    {0, 112, 1, 227, 2},
    {0, 113, 3, 229, 6},
    {0, 116, 1, 235, 2},
    {0, 117, 3, 237, 6},
    {0, 120, 1, 243, 3},
    {0, 121, 3, 246, 6},
    {0, 124, 1, 252, 2},
    {0, 125, 3, 254, 6},
    {0, 128, 1, 260, 2},
    {0, 129, 3, 262, 6},
    {0, 132, 1, 268, 2},
    {0, 133, 3, 270, 6},
    {0, 136, 1, 276, 2},
    {0, 137, 3, 278, 6},
    {0, 140, 1, 284, 2},
    {0, 141, 2, 286, 3},
    {0, 143, 2, 289, 3},
    {0, 145, 3, 292, 4},
    {0, 148, 2, 296, 3},
    {0, 150, 2, 299, 3},
    {0, 152, 3, 302, 4},
    {0, 155, 2, 306, 2},
    {0, 157, 2, 308, 2},
    {0, 159, 2, 310, 2},
    {0, 161, 2, 312, 2},
    {0, 163, 3, 314, 3},
    {0, 166, 2, 317, 2},
    {0, 168, 2, 319, 2},
    {0, 170, 3, 321, 2},
    {0, 173, 3, 323, 3},
    {0, 176, 3, 326, 3},
    {0, 179, 3, 329, 3},
    {0, 182, 2, 332, 2},
    {0, 184, 3, 334, 3},
    {0, 187, 2, 337, 2},
    {0, 189, 2, 339, 2},
    {0, 191, 3, 341, 3},
    {0, 194, 2, 344, 3},
    {0, 196, 2, 347, 3},
    {0, 198, 3, 350, 3},
    {0, 201, 2, 353, 3},
    {0, 203, 3, 356, 4},
    {0, 206, 2, 360, 3},
    {0, 208, 2, 363, 3},
    {0, 210, 3, 366, 4},
    {0, 213, 2, 370, 3},
    {0, 215, 2, 373, 3},
    {0, 217, 3, 376, 4},
    {0, 220, 2, 380, 2},
    {0, 222, 2, 382, 2},
    {0, 224, 2, 384, 2},
    {0, 226, 2, 386, 2},
    {0, 228, 3, 388, 3},
    {0, 231, 2, 391, 2},
    {0, 233, 2, 393, 2},
    {0, 235, 3, 395, 2},
    {0, 238, 3, 397, 3},
    {0, 241, 3, 400, 3},
    {0, 244, 3, 403, 3},
    {0, 247, 2, 406, 2},
    {0, 249, 3, 408, 3},
    {0, 252, 2, 411, 2},
    {0, 254, 2, 413, 2},
    {0, 256, 3, 415, 3},
    {0, 259, 2, 418, 2},
    {0, 261, 2, 420, 2},
    {0, 263, 3, 422, 2},
    {0, 266, 2, 424, 2},
    {0, 268, 3, 426, 3},
    {0, 271, 31, 429, 35},
    {0, 302, 9, 464, 11},
    {0, 311, 9, 475, 13},
    {0, 320, 5, 488, 6},
    {0, 325, 13, 494, 18},
    {0, 338, 4, 512, 4},
    {0, 342, 38, 516, 44},
    {0, 380, 21, 560, 24},
    {0, 401, 15, 584, 17},
    {0, 416, 10, 601, 11},
    {0, 426, 10, 612, 14},
    {0, 436, 10, 626, 13},
    {0, 446, 10, 639, 11},
    {0, 456, 9, 650, 12},
    {0, 465, 14, 662, 20},
    {0, 479, 8, 682, 9},
    {0, 487, 9, 691, 11},
    {0, 496, 12, 702, 18},
    {0, 508, 19, 720, 25},
    {0, 527, 12, 745, 14},
    {0, 539, 20, 759, 26},
    {0, 559, 16, 785, 19},
    {0, 575, 21, 804, 28},
    {0, 596, 10, 832, 12},
    {0, 606, 8, 844, 10},
    {0, 614, 8, 854, 8},
    {0, 622, 14, 862, 17},
    {0, 636, 13, 879, 18},
    {0, 649, 18, 897, 19},
    {0, 667, 19, 916, 23},
    {0, 686, 6, 939, 9},
    {0, 692, 8, 948, 9},
    {0, 700, 6, 957, 9},
    {0, 706, 7, 966, 9},
    {0, 713, 6, 975, 6},
    {0, 719, 5, 981, 6},
    {0, 724, 6, 987, 8},
    {0, 730, 10, 995, 11},
    {0, 740, 3, 1006, 3},
    {0, 743, 7, 1009, 9},
    {0, 750, 5, 1018, 7},
    {0, 755, 5, 1025, 7},
    {0, 760, 4, 1032, 6},
    {0, 764, 4, 1038, 4},
    {0, 768, 5, 1042, 6},
    {0, 773, 5, 1048, 6},
    {0, 778, 2, 1054, 3},
    {0, 780, 3, 1057, 4},
    {0, 783, 4, 1061, 5},
    {0, 787, 5, 1066, 8},
    {0, 792, 9, 1074, 13},
    {0, 801, 5, 1087, 7},
    {0, 806, 5, 1094, 7},
    {0, 811, 6, 1101, 8},
    {0, 817, 8, 1109, 12},
    {0, 825, 14, 1121, 19},
    {0, 839, 13, 1140, 18},
    {0, 852, 10, 1158, 14},
    {0, 862, 3, 1172, 4},
    {0, 865, 4, 1176, 4},
    {0, 869, 7, 1180, 10},
    {0, 876, 5, 1190, 5},
    {0, 881, 5, 1195, 6},
    {0, 886, 7, 1201, 9},
    {0, 893, 5, 1210, 7},
    {0, 898, 10, 1217, 11},
    {0, 908, 12, 1228, 15},
    {0, 920, 5, 1243, 7},
    {0, 925, 16, 1250, 20},
    {0, 941, 8, 1270, 11},
    {0, 949, 5, 1281, 7},
    {0, 954, 4, 1288, 6},
    {0, 958, 4, 1294, 4},
    {0, 962, 4, 1298, 5},
    {0, 966, 6, 1303, 9},
    {0, 972, 4, 1312, 5},
    {0, 976, 3, 1317, 4},
    {0, 979, 2, 1321, 2},
    {0, 981, 2, 1323, 2},
    {0, 983, 4, 1325, 5},
    {0, 987, 1, 1330, 1},
    {0, 988, 5, 1331, 7},
    {0, 993, 7, 1338, 9},
    {0, 1000, 7, 1347, 7},
    {0, 1007, 2, 1354, 2},
    {0, 1009, 2, 1356, 2},
    {0, 1011, 5, 1358, 8},
    {0, 1016, 5, 1366, 9},
    {0, 1021, 1, 1375, 1},
    {0, 1022, 1, 1376, 1},
    {0, 1023, 1, 1377, 1},
    {0, 1024, 3, 1378, 4},
    {0, 1027, 3, 1382, 4},
    {0, 1030, 3, 1386, 4},
    {0, 1033, 3, 1390, 4},
    {0, 1036, 3, 1394, 5},
    {0, 1039, 3, 1399, 5},
    {0, 1042, 3, 1404, 5},
    {0, 1045, 8, 1409, 8},
    {0, 1053, 4, 1417, 4},
    {0, 1057, 3, 1421, 3},
    {0, 1060, 1, 1424, 1},
    {0, 1061, 1, 1425, 1},
    {0, 1062, 1, 1426, 1},
    {0, 1063, 1, 1427, 1},
    {0, 1064, 1, 1428, 1},
    {0, 1065, 1, 1429, 1},
    {0, 1066, 1, 1430, 1},
    {0, 1067, 1, 1431, 2},
    {0, 1068, 1, 1433, 1},
    {0, 1069, 1, 1434, 1},
    {0, 1070, 1, 1435, 1},
    {0, 1071, 1, 1436, 1},
    {0, 1072, 1, 1437, 1},
    {0, 1073, 1, 1438, 1},
    {0, 1074, 1, 1439, 2},
    {0, 1075, 1, 1441, 2},
    {0, 1076, 1, 1443, 2},
    {0, 1077, 1, 1445, 1},
    {0, 1078, 2, 1446, 2},
    {0, 1080, 2, 1448, 2},
    {0, 1082, 2, 1450, 3},
    {0, 1084, 3, 1453, 5},
    {0, 1087, 4, 1458, 7},
    {0, 1091, 4, 1465, 5},
    {0, 1095, 5, 1470, 6},
    {0, 1100, 5, 1476, 8},
    {0, 1105, 5, 1484, 7},
    {0, 1110, 4, 1491, 7},
    {0, 1114, 4, 1498, 6},
    {0, 1118, 3, 1504, 5},
    {0, 1121, 5, 1509, 8},
    {0, 1126, 4, 1517, 5},
    {0, 1130, 3, 1522, 4},
    {0, 1133, 3, 1526, 4},
    {0, 1136, 3, 1530, 5},
    {0, 1139, 4, 1535, 6},
    {0, 1143, 4, 1541, 6},
    {0, 1147, 4, 1547, 5},
    {0, 1151, 3, 1552, 4},
    {0, 1154, 4, 1556, 4},
    {0, 1158, 4, 1560, 6},
    {0, 1162, 4, 1566, 6},
    {0, 1166, 4, 1572, 5},
    {0, 1170, 4, 1577, 5},
    {0, 1174, 5, 1582, 8},
    {0, 1179, 4, 1590, 7},
    {0, 1183, 5, 1597, 5},
    {0, 1188, 4, 1602, 6},
    {0, 1192, 3, 1608, 4},
    {0, 1195, 3, 1612, 5},
    {0, 1198, 3, 1617, 3},
    {0, 1201, 3, 1620, 4},
    {0, 1204, 4, 1624, 6},
    {0, 1208, 5, 1630, 7},
    {0, 1213, 3, 1637, 4},
    {0, 1216, 3, 1641, 5},
    {1, 1219, 31, 1646, 35},
    {1, 1250, 15, 1681, 17},
    {1, 1265, 9, 1698, 11},
    {1, 1274, 8, 1709, 10},
    {1, 1282, 6, 1719, 9},
    {1, 1288, 5, 1728, 7},
    {1, 1293, 4, 1735, 5},
    {1, 1297, 13, 1740, 18},
    {1, 1310, 5, 1758, 7},
    {1, 1315, 4, 1765, 4},
    {1, 1319, 5, 1769, 7},
    {1, 1324, 1, 1776, 1},
    {1, 1325, 3, 1777, 5},
    {1, 1328, 1, 1782, 1},
    {1, 1329, 1, 1783, 1},
    {1, 1330, 1, 1784, 1},
    {1, 1331, 5, 1785, 8},
    {1, 1336, 3, 1793, 4},
    {1, 1339, 4, 1797, 6},
    {1, 1343, 3, 1803, 5},
    {0, 1346, 3, 1808, 4},
    {0, 1349, 3, 1812, 4},
    {0, 1352, 3, 1816, 5},
    {0, 1355, 3, 1821, 3},
    {0, 1358, 3, 1824, 3},
    {0, 1361, 3, 1827, 2},
    {0, 1364, 3, 1829, 2},
    {0, 1367, 3, 1831, 3},
    {0, 1370, 3, 1834, 3},
    {0, 1373, 2, 1837, 2},
    {0, 1375, 2, 1839, 2},
    {0, 1377, 2, 1841, 2},
    {0, 1379, 2, 1843, 2},
    {0, 1381, 1, 1845, 0},
    {0, 1382, 1, 1845, 0},
    {0, 1383, 4, 1845, 5},
    {0, 1387, 4, 1850, 5},
    {0, 1391, 4, 1855, 3},
    {0, 1395, 3, 1858, 3},
    {0, 1398, 4, 1861, 4},
    {0, 1402, 4, 1865, 4},
    {0, 1406, 3, 1869, 2},
    {0, 1409, 3, 1871, 4},
    {0, 1412, 3, 1875, 2},
    {2, 1415, 1, 1877, 1},
    {2, 1416, 3, 1878, 3},
    {2, 1419, 2, 1881, 2},
    {2, 1421, 2, 1883, 2},
    {2, 1423, 3, 1885, 3},
    {2, 1426, 2, 1888, 2},
    {2, 1428, 2, 1890, 2},
    {2, 1430, 4, 1892, 4},
    {2, 1434, 2, 1896, 2},
    {2, 1436, 2, 1898, 2},
    {2, 1438, 4, 1900, 4},
    {2, 1442, 2, 1904, 2},
    {2, 1444, 2, 1906, 2},
    {2, 1446, 3, 1908, 3},
    {2, 1449, 2, 1911, 2},
    {2, 1451, 2, 1913, 2},
    {2, 1453, 3, 1915, 3},
    {2, 1456, 2, 1918, 2},
    {2, 1458, 2, 1920, 2},
    {2, 1460, 3, 1922, 3},
    {2, 1463, 2, 1925, 2},
    {2, 1465, 2, 1927, 2},
    {2, 1467, 3, 1929, 3},
    {2, 1470, 2, 1932, 2},
    {2, 1472, 2, 1934, 2},
    {2, 1474, 2, 1936, 2},
    {2, 1476, 3, 1938, 3},
    {2, 1479, 2, 1941, 2},
    {2, 1481, 2, 1943, 2},
    {2, 1483, 3, 1945, 3},
    {2, 1486, 2, 1948, 2},
    {2, 1488, 2, 1950, 2},
    {2, 1490, 3, 1952, 3},
    {2, 1493, 2, 1955, 2},
    {2, 1495, 2, 1957, 2},
    {2, 1497, 3, 1959, 3},
    {2, 1500, 2, 1962, 2},
    {2, 1502, 1, 1964, 1},
    {2, 1503, 3, 1965, 3},
    {2, 1506, 2, 1968, 2},
    {2, 1508, 4, 1970, 4},
    {2, 1512, 5, 1974, 5},
    {2, 1517, 11, 1979, 11},
    {2, 1528, 7, 1990, 7},
    {2, 1535, 8, 1997, 8},
    {2, 1543, 7, 2005, 7},
    {2, 1550, 12, 2012, 12},
    {2, 1562, 13, 2024, 13},
    {2, 1575, 7, 2037, 7},
    {2, 1582, 5, 2044, 5},
    {2, 1587, 13, 2049, 13},
    {2, 1600, 8, 2062, 8},
    {2, 1608, 8, 2070, 8},
    {2, 1616, 8, 2078, 8},
    {2, 1624, 3, 2086, 3},
    {2, 1627, 4, 2089, 4},
    {2, 1631, 8, 2093, 8},
    {2, 1639, 7, 2101, 7},
    {2, 1646, 8, 2108, 8},
    {2, 1654, 21, 2116, 21},
    {2, 1675, 22, 2137, 22},
    {2, 1697, 5, 2159, 5},
    {2, 1702, 3, 2164, 3},
    {2, 1705, 5, 2167, 5},
    {2, 1710, 9, 2172, 9},
    {2, 1719, 6, 2181, 6},
    {2, 1725, 6, 2187, 6},
    {2, 1731, 6, 2193, 6},
    {2, 1737, 4, 2199, 4},
    {3, 1741, 1, 2203, 1},
    {3, 1742, 2, 2204, 2},
    {3, 1744, 2, 2206, 2},
    {3, 1746, 2, 2208, 2},
    {3, 1748, 8, 2210, 8},
    {3, 1756, 4, 2218, 4},
    {3, 1760, 7, 2222, 7},
    {3, 1767, 12, 2229, 12},
    {3, 1779, 5, 2241, 5},
    {3, 1784, 4, 2246, 4},
    {3, 1788, 8, 2250, 8},
    {3, 1796, 5, 2258, 5},
    {3, 1801, 9, 2263, 9},
    {3, 1810, 6, 2272, 6},
};

//      Mixed lines: right to left Arabic and Hebrew with Latin, digits and brackets, in all three paragraph directions.
//      The levels are fribidi 1.0.17's, the Arabic and Hebrew runs hb-shape's, in reading order from the pen: per case
//      the direction (0 left to right, 1 right to left, 2 the first strong character), the text, and the runs:
//      a count, then for each its face (0 Inter, 1 Arabic, 2 Hebrew), level and glyph count, and its code points
//      (Latin: in reading order, mirrored brackets in) or its glyphs as four numbers.
#define MIXED_CASES 141
static const b32 mixed_stream[8080] = {
    97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575, 32, 100, 101, 102, 3,
    0, 0, 4, 97, 98, 99, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 0, 0, 4, 32, 100, 101, 102, 97,
    98, 99, 32, 1605, 1585, 1581, 1576, 1575, 32, 100, 101, 102, 5, 0,
    2, 3, 100, 101, 102, 0, 1, 1, 32, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 32, 0, 2,
    3, 97, 98, 99, 97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575, 32,
    100, 101, 102, 3, 0, 0, 4, 97, 98, 99, 32, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0, 4, 32,
    100, 101, 102, 1605, 1585, 1581, 1576, 1575, 32, 97, 98, 99, 32, 1603,
    1578, 1575, 1576, 3, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94,
    -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79,
    525, 0, 0, 0, 0, 5, 32, 97, 98, 99, 32, 1, 1, 6,
    316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0,
    54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 1605, 1585, 1581, 1576,
    1575, 32, 97, 98, 99, 32, 1603, 1578, 1575, 1576, 5, 1, 1, 6,
    316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0,
    54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1, 32,
    0, 2, 3, 97, 98, 99, 0, 1, 1, 32, 1, 1, 6, 9,
    291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0,
    0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575,
    32, 97, 98, 99, 32, 1603, 1578, 1575, 1576, 5, 1, 1, 6, 316,
    0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54,
    -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1, 32, 0,
    2, 3, 97, 98, 99, 0, 1, 1, 32, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 97, 98, 99, 32, 1605, 1585,
    1581, 1576, 1575, 2, 0, 0, 4, 97, 98, 99, 32, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 97, 98, 99, 32,
    1605, 1585, 1581, 1576, 1575, 3, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 0, 1, 1, 32, 0, 2, 3, 97, 98,
    99, 97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575, 2, 0, 0, 4,
    97, 98, 99, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94,
    -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79,
    525, 0, 0, 1605, 1585, 1581, 1576, 1575, 32, 97, 98, 99, 2, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0,
    4, 32, 97, 98, 99, 1605, 1585, 1581, 1576, 1575, 32, 97, 98, 99,
    3, 0, 2, 3, 97, 98, 99, 0, 1, 1, 32, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576,
    1575, 32, 97, 98, 99, 3, 0, 2, 3, 97, 98, 99, 0, 1,
    1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 40, 1605, 1585, 1581, 1576, 1575, 41, 3, 0, 0, 1, 40, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0,
    1, 41, 40, 1605, 1585, 1581, 1576, 1575, 41, 3, 0, 1, 1, 40,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    1, 1, 41, 40, 1605, 1585, 1581, 1576, 1575, 41, 3, 0, 1, 1,
    40, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343,
    0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0,
    0, 1, 1, 41, 97, 98, 99, 32, 40, 1605, 1585, 1581, 1576, 1575,
    41, 32, 100, 101, 102, 3, 0, 0, 5, 97, 98, 99, 32, 40,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    0, 5, 41, 32, 100, 101, 102, 97, 98, 99, 32, 40, 1605, 1585,
    1581, 1576, 1575, 41, 32, 100, 101, 102, 5, 0, 2, 3, 100, 101,
    102, 0, 1, 2, 32, 40, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 0, 1, 2, 41, 32, 0, 2, 3, 97,
    98, 99, 97, 98, 99, 32, 40, 1605, 1585, 1581, 1576, 1575, 41, 32,
    100, 101, 102, 3, 0, 0, 5, 97, 98, 99, 32, 40, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0, 5,
    41, 32, 100, 101, 102, 97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575,
    32, 49, 50, 51, 32, 1603, 1578, 1575, 1576, 6, 0, 0, 4, 97,
    98, 99, 32, 1, 1, 6, 316, 0, 410, -23, 14, 993, 0, 0,
    9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412,
    0, 0, 0, 1, 1, 32, 0, 2, 3, 49, 50, 51, 0, 1,
    1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575, 32, 49, 50, 51,
    32, 1603, 1578, 1575, 1576, 7, 1, 1, 6, 316, 0, 410, -23, 14,
    993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0,
    0, 61, 412, 0, 0, 0, 1, 1, 32, 0, 2, 3, 49, 50,
    51, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0,
    94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0,
    79, 525, 0, 0, 0, 1, 1, 32, 0, 2, 3, 97, 98, 99,
    97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575, 32, 49, 50, 51, 32,
    1603, 1578, 1575, 1576, 6, 0, 0, 4, 97, 98, 99, 32, 1, 1,
    6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287,
    0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1,
    32, 0, 2, 3, 49, 50, 51, 0, 1, 1, 32, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576,
    1575, 32, 49, 50, 51, 3, 0, 2, 3, 49, 50, 51, 0, 1,
    1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 1605, 1585, 1581, 1576, 1575, 32, 49, 50, 51, 3, 0, 2, 3,
    49, 50, 51, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575, 32, 49, 50,
    51, 3, 0, 2, 3, 49, 50, 51, 0, 1, 1, 32, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 49, 50, 51,
    32, 1605, 1585, 1581, 1576, 1575, 2, 0, 0, 4, 49, 50, 51, 32,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 49,
    50, 51, 32, 1605, 1585, 1581, 1576, 1575, 3, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 32, 0, 2,
    3, 49, 50, 51, 49, 50, 51, 32, 1605, 1585, 1581, 1576, 1575, 3,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    1, 1, 32, 0, 2, 3, 49, 50, 51, 1605, 1585, 1581, 1576, 1575,
    32, 49, 50, 46, 53, 32, 1603, 1578, 1575, 1576, 5, 1, 1, 6,
    316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0,
    54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1, 32,
    0, 2, 4, 49, 50, 46, 53, 0, 1, 1, 32, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576,
    1575, 32, 49, 50, 46, 53, 32, 1603, 1578, 1575, 1576, 5, 1, 1,
    6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287,
    0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1,
    32, 0, 2, 4, 49, 50, 46, 53, 0, 1, 1, 32, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581,
    1576, 1575, 32, 49, 50, 46, 53, 32, 1603, 1578, 1575, 1576, 5, 1,
    1, 6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0,
    287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1,
    1, 32, 0, 2, 4, 49, 50, 46, 53, 0, 1, 1, 32, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 91, 1605,
    1585, 1581, 1576, 1575, 93, 32, 97, 98, 99, 3, 0, 0, 1, 91,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    0, 5, 93, 32, 97, 98, 99, 91, 1605, 1585, 1581, 1576, 1575, 93,
    32, 97, 98, 99, 4, 0, 2, 3, 97, 98, 99, 0, 1, 2,
    32, 91, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 0, 1, 1, 93, 91, 1605, 1585, 1581, 1576, 1575, 93, 32, 97,
    98, 99, 4, 0, 2, 3, 97, 98, 99, 0, 1, 2, 32, 91,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    1, 1, 93, 97, 98, 99, 32, 91, 1605, 1585, 1581, 1576, 1575, 93,
    32, 100, 101, 102, 3, 0, 0, 5, 97, 98, 99, 32, 91, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0,
    5, 93, 32, 100, 101, 102, 97, 98, 99, 32, 91, 1605, 1585, 1581,
    1576, 1575, 93, 32, 100, 101, 102, 5, 0, 2, 3, 100, 101, 102,
    0, 1, 2, 32, 91, 1, 1, 6, 9, 291, 0, 0, 316, 0,
    94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0,
    79, 525, 0, 0, 0, 1, 2, 93, 32, 0, 2, 3, 97, 98,
    99, 97, 98, 99, 32, 91, 1605, 1585, 1581, 1576, 1575, 93, 32, 100,
    101, 102, 3, 0, 0, 5, 97, 98, 99, 32, 91, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0, 5, 93,
    32, 100, 101, 102, 171, 1605, 1585, 1581, 1576, 1575, 187, 3, 0, 0,
    1, 171, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 0, 0, 1, 187, 171, 1605, 1585, 1581, 1576, 1575, 187, 3, 0,
    1, 1, 171, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 0, 1, 1, 187, 171, 1605, 1585, 1581, 1576, 1575, 187, 3,
    0, 1, 1, 171, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94,
    -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79,
    525, 0, 0, 0, 1, 1, 187, 1605, 1585, 1581, 1576, 1575, 1548, 32,
    1603, 1578, 1575, 1576, 3, 1, 1, 6, 316, 0, 410, -23, 14, 993,
    0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    61, 412, 0, 0, 0, 1, 1, 32, 1, 1, 7, 176, 328, 0,
    0, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581,
    1576, 1575, 1548, 32, 1603, 1578, 1575, 1576, 3, 1, 1, 6, 316, 0,
    410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1, 32, 1, 1,
    7, 176, 328, 0, 0, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 1605, 1585, 1581, 1576, 1575, 1548, 32, 1603, 1578, 1575, 1576, 3, 1,
    1, 6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0,
    287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1,
    1, 32, 1, 1, 7, 176, 328, 0, 0, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575, 46, 32, 97, 98,
    99, 2, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 0, 0, 5, 46, 32, 97, 98, 99, 1605, 1585, 1581, 1576, 1575,
    46, 32, 97, 98, 99, 3, 0, 2, 3, 97, 98, 99, 0, 1,
    2, 32, 46, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 1605, 1585, 1581, 1576, 1575, 46, 32, 97, 98, 99, 3, 0,
    2, 3, 97, 98, 99, 0, 1, 2, 32, 46, 1, 1, 6, 9,
    291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0,
    0, 31, 356, -40, 0, 79, 525, 0, 0, 1575, 1604, 1593, 1585, 1576,
    1610, 1577, 32, 1603, 1578, 1575, 1576, 32, 40, 97, 98, 99, 41, 4,
    1, 1, 6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0,
    0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0,
    1, 1, 32, 1, 1, 10, 287, 0, 101, 65, 85, 486, 0, 0,
    318, 0, 39, -5, 18, 373, 0, 0, 316, 0, 31, -3, 19, 269,
    0, 0, 31, 366, -30, 0, 48, 521, 0, 0, 72, 260, 0, 0,
    8, 238, 0, 0, 0, 0, 6, 32, 40, 97, 98, 99, 41, 1575,
    1604, 1593, 1585, 1576, 1610, 1577, 32, 1603, 1578, 1575, 1576, 32, 40, 97,
    98, 99, 41, 6, 0, 1, 1, 40, 0, 2, 3, 97, 98, 99,
    0, 1, 2, 41, 32, 1, 1, 6, 316, 0, 410, -23, 14, 993,
    0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    61, 412, 0, 0, 0, 1, 1, 32, 1, 1, 10, 287, 0, 101,
    65, 85, 486, 0, 0, 318, 0, 39, -5, 18, 373, 0, 0, 316,
    0, 31, -3, 19, 269, 0, 0, 31, 366, -30, 0, 48, 521, 0,
    0, 72, 260, 0, 0, 8, 238, 0, 0, 1575, 1604, 1593, 1585, 1576,
    1610, 1577, 32, 1603, 1578, 1575, 1576, 32, 40, 97, 98, 99, 41, 6,
    0, 1, 1, 40, 0, 2, 3, 97, 98, 99, 0, 1, 2, 41,
    32, 1, 1, 6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291,
    0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0,
    0, 1, 1, 32, 1, 1, 10, 287, 0, 101, 65, 85, 486, 0,
    0, 318, 0, 39, -5, 18, 373, 0, 0, 316, 0, 31, -3, 19,
    269, 0, 0, 31, 366, -30, 0, 48, 521, 0, 0, 72, 260, 0,
    0, 8, 238, 0, 0, 80, 114, 105, 99, 101, 58, 32, 57, 57,
    32, 1603, 1578, 1575, 1576, 2, 0, 0, 10, 80, 114, 105, 99, 101,
    58, 32, 57, 57, 32, 1, 1, 6, 316, 0, 410, -23, 14, 993,
    0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    61, 412, 0, 0, 80, 114, 105, 99, 101, 58, 32, 57, 57, 32,
    1603, 1578, 1575, 1576, 3, 1, 1, 6, 316, 0, 410, -23, 14, 993,
    0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0,
    61, 412, 0, 0, 0, 1, 1, 32, 0, 2, 9, 80, 114, 105,
    99, 101, 58, 32, 57, 57, 80, 114, 105, 99, 101, 58, 32, 57,
    57, 32, 1603, 1578, 1575, 1576, 2, 0, 0, 10, 80, 114, 105, 99,
    101, 58, 32, 57, 57, 32, 1, 1, 6, 316, 0, 410, -23, 14,
    993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0,
    0, 61, 412, 0, 0, 73, 68, 32, 49, 50, 51, 52, 53, 32,
    1605, 1585, 1581, 1576, 1575, 32, 1603, 1578, 1575, 1576, 4, 0, 0, 9,
    73, 68, 32, 49, 50, 51, 52, 53, 32, 1, 1, 6, 316, 0,
    410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180,
    18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1, 32, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 73, 68, 32,
    49, 50, 51, 52, 53, 32, 1605, 1585, 1581, 1576, 1575, 32, 1603, 1578,
    1575, 1576, 5, 1, 1, 6, 316, 0, 410, -23, 14, 993, 0, 0,
    9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412,
    0, 0, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 0, 1, 1, 32, 0, 2, 8, 73, 68,
    32, 49, 50, 51, 52, 53, 73, 68, 32, 49, 50, 51, 52, 53,
    32, 1605, 1585, 1581, 1576, 1575, 32, 1603, 1578, 1575, 1576, 4, 0, 0,
    9, 73, 68, 32, 49, 50, 51, 52, 53, 32, 1, 1, 6, 316,
    0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54,
    -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 1, 1, 32, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 120, 32,
    61, 32, 1605, 1585, 1581, 1576, 1575, 2, 0, 0, 4, 120, 32, 61,
    32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343,
    0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0,
    120, 32, 61, 32, 1605, 1585, 1581, 1576, 1575, 3, 1, 1, 6, 9,
    291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0,
    0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 3, 32, 61,
    32, 0, 2, 1, 120, 120, 32, 61, 32, 1605, 1585, 1581, 1576, 1575,
    2, 0, 0, 4, 120, 32, 61, 32, 1, 1, 6, 9, 291, 0,
    0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31,
    356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575, 32, 61,
    32, 120, 2, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 0, 0, 4, 32, 61, 32, 120, 1605, 1585, 1581, 1576, 1575,
    32, 61, 32, 120, 3, 0, 2, 1, 120, 0, 1, 3, 32, 61,
    32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343,
    0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0,
    1605, 1585, 1581, 1576, 1575, 32, 61, 32, 120, 3, 0, 2, 1, 120,
    0, 1, 3, 32, 61, 32, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 97, 98, 99, 32, 1513, 1500, 1493, 1501, 32,
    100, 101, 102, 3, 0, 0, 4, 97, 98, 99, 32, 2, 1, 4,
    23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730,
    0, 0, 0, 0, 4, 32, 100, 101, 102, 97, 98, 99, 32, 1513,
    1500, 1493, 1501, 32, 100, 101, 102, 5, 0, 2, 3, 100, 101, 102,
    0, 1, 1, 32, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10,
    0, 55, 522, 0, 0, 96, 730, 0, 0, 0, 1, 1, 32, 0,
    2, 3, 97, 98, 99, 97, 98, 99, 32, 1513, 1500, 1493, 1501, 32,
    100, 101, 102, 3, 0, 0, 4, 97, 98, 99, 32, 2, 1, 4,
    23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730,
    0, 0, 0, 0, 4, 32, 100, 101, 102, 1513, 1500, 1493, 1501, 32,
    97, 98, 99, 32, 1513, 1500, 1493, 1501, 3, 2, 1, 4, 23, 684,
    0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0,
    0, 0, 5, 32, 97, 98, 99, 32, 2, 1, 4, 23, 684, 0,
    0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 1513,
    1500, 1493, 1501, 32, 97, 98, 99, 32, 1513, 1500, 1493, 1501, 5, 2,
    1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0,
    96, 730, 0, 0, 0, 1, 1, 32, 0, 2, 3, 97, 98, 99,
    0, 1, 1, 32, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10,
    0, 55, 522, 0, 0, 96, 730, 0, 0, 1513, 1500, 1493, 1501, 32,
    97, 98, 99, 32, 1513, 1500, 1493, 1501, 5, 2, 1, 4, 23, 684,
    0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0,
    0, 1, 1, 32, 0, 2, 3, 97, 98, 99, 0, 1, 1, 32,
    2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0,
    0, 96, 730, 0, 0, 97, 98, 99, 32, 1506, 1460, 1489, 1456, 1512,
    1460, 1497, 1514, 2, 0, 0, 4, 97, 98, 99, 32, 2, 1, 8,
    107, 685, 0, 0, 138, 275, -20, 0, 45, 0, 319, 0, 86, 523,
    0, 0, 95, 0, 167, 0, 12, 547, -25, 0, 45, 0, 200, 0,
    10, 593, 0, 0, 97, 98, 99, 32, 1506, 1460, 1489, 1456, 1512, 1460,
    1497, 1514, 3, 2, 1, 8, 107, 685, 0, 0, 138, 275, -20, 0,
    45, 0, 319, 0, 86, 523, 0, 0, 95, 0, 167, 0, 12, 547,
    -25, 0, 45, 0, 200, 0, 10, 593, 0, 0, 0, 1, 1, 32,
    0, 2, 3, 97, 98, 99, 97, 98, 99, 32, 1506, 1460, 1489, 1456,
    1512, 1460, 1497, 1514, 2, 0, 0, 4, 97, 98, 99, 32, 2, 1,
    8, 107, 685, 0, 0, 138, 275, -20, 0, 45, 0, 319, 0, 86,
    523, 0, 0, 95, 0, 167, 0, 12, 547, -25, 0, 45, 0, 200,
    0, 10, 593, 0, 0, 1489, 1456, 1468, 1512, 1461, 1488, 1513, 1460, 1473,
    1497, 1514, 32, 97, 98, 99, 2, 2, 1, 11, 107, 685, 0, 0,
    138, 275, -20, 0, 45, 0, 284, 0, 100, 0, 539, 0, 96, 730,
    0, 0, 3, 612, -20, 0, 117, 0, 268, 0, 86, 523, 0, 0,
    95, 0, 167, 0, 15, 0, 101, 0, 12, 547, -25, 0, 0, 0,
    4, 32, 97, 98, 99, 1489, 1456, 1468, 1512, 1461, 1488, 1513, 1460, 1473,
    1497, 1514, 32, 97, 98, 99, 3, 0, 2, 3, 97, 98, 99, 0,
    1, 1, 32, 2, 1, 11, 107, 685, 0, 0, 138, 275, -20, 0,
    45, 0, 284, 0, 100, 0, 539, 0, 96, 730, 0, 0, 3, 612,
    -20, 0, 117, 0, 268, 0, 86, 523, 0, 0, 95, 0, 167, 0,
    15, 0, 101, 0, 12, 547, -25, 0, 1489, 1456, 1468, 1512, 1461, 1488,
    1513, 1460, 1473, 1497, 1514, 32, 97, 98, 99, 3, 0, 2, 3, 97,
    98, 99, 0, 1, 1, 32, 2, 1, 11, 107, 685, 0, 0, 138,
    275, -20, 0, 45, 0, 284, 0, 100, 0, 539, 0, 96, 730, 0,
    0, 3, 612, -20, 0, 117, 0, 268, 0, 86, 523, 0, 0, 95,
    0, 167, 0, 15, 0, 101, 0, 12, 547, -25, 0, 40, 1513, 1500,
    1493, 1501, 41, 3, 0, 0, 1, 40, 2, 1, 4, 23, 684, 0,
    0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 0,
    0, 1, 41, 40, 1513, 1500, 1493, 1501, 41, 3, 0, 1, 1, 40,
    2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0,
    0, 96, 730, 0, 0, 0, 1, 1, 41, 40, 1513, 1500, 1493, 1501,
    41, 3, 0, 1, 1, 40, 2, 1, 4, 23, 684, 0, 0, 124,
    291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 0, 1, 1,
    41, 97, 98, 99, 32, 40, 1513, 1500, 1493, 1501, 41, 32, 100, 101,
    102, 3, 0, 0, 5, 97, 98, 99, 32, 40, 2, 1, 4, 23,
    684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0,
    0, 0, 0, 5, 41, 32, 100, 101, 102, 97, 98, 99, 32, 40,
    1513, 1500, 1493, 1501, 41, 32, 100, 101, 102, 5, 0, 2, 3, 100,
    101, 102, 0, 1, 2, 32, 40, 2, 1, 4, 23, 684, 0, 0,
    124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 0, 1,
    2, 41, 32, 0, 2, 3, 97, 98, 99, 97, 98, 99, 32, 40,
    1513, 1500, 1493, 1501, 41, 32, 100, 101, 102, 3, 0, 0, 5, 97,
    98, 99, 32, 40, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10,
    0, 55, 522, 0, 0, 96, 730, 0, 0, 0, 0, 5, 41, 32,
    100, 101, 102, 1513, 1500, 1493, 1501, 32, 49, 50, 51, 32, 1513, 1500,
    1493, 1501, 5, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0,
    55, 522, 0, 0, 96, 730, 0, 0, 0, 1, 1, 32, 0, 2,
    3, 49, 50, 51, 0, 1, 1, 32, 2, 1, 4, 23, 684, 0,
    0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 1513,
    1500, 1493, 1501, 32, 49, 50, 51, 32, 1513, 1500, 1493, 1501, 5, 2,
    1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0,
    96, 730, 0, 0, 0, 1, 1, 32, 0, 2, 3, 49, 50, 51,
    0, 1, 1, 32, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10,
    0, 55, 522, 0, 0, 96, 730, 0, 0, 1513, 1500, 1493, 1501, 32,
    49, 50, 51, 32, 1513, 1500, 1493, 1501, 5, 2, 1, 4, 23, 684,
    0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0,
    0, 1, 1, 32, 0, 2, 3, 49, 50, 51, 0, 1, 1, 32,
    2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0,
    0, 96, 730, 0, 0, 97, 98, 99, 32, 1513, 1500, 1493, 1501, 32,
    1605, 1585, 1581, 1576, 1575, 32, 100, 101, 102, 5, 0, 0, 4, 97,
    98, 99, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 0, 1, 1, 32, 2, 1, 4, 23, 684, 0, 0, 124,
    291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 0, 0, 4,
    32, 100, 101, 102, 97, 98, 99, 32, 1513, 1500, 1493, 1501, 32, 1605,
    1585, 1581, 1576, 1575, 32, 100, 101, 102, 7, 0, 2, 3, 100, 101,
    102, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0,
    94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0,
    79, 525, 0, 0, 0, 1, 1, 32, 2, 1, 4, 23, 684, 0,
    0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 0,
    1, 1, 32, 0, 2, 3, 97, 98, 99, 97, 98, 99, 32, 1513,
    1500, 1493, 1501, 32, 1605, 1585, 1581, 1576, 1575, 32, 100, 101, 102, 5,
    0, 0, 4, 97, 98, 99, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 0, 1, 1, 32, 2, 1, 4, 23,
    684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0,
    0, 0, 0, 4, 32, 100, 101, 102, 1513, 1500, 1493, 1501, 32, 1605,
    1585, 1581, 1576, 1575, 32, 97, 98, 99, 4, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 32, 2, 1,
    4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96,
    730, 0, 0, 0, 0, 4, 32, 97, 98, 99, 1513, 1500, 1493, 1501,
    32, 1605, 1585, 1581, 1576, 1575, 32, 97, 98, 99, 5, 0, 2, 3,
    97, 98, 99, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 0, 1, 1, 32, 2, 1, 4, 23,
    684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0,
    0, 1513, 1500, 1493, 1501, 32, 1605, 1585, 1581, 1576, 1575, 32, 97, 98,
    99, 5, 0, 2, 3, 97, 98, 99, 0, 1, 1, 32, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1,
    32, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522,
    0, 0, 96, 730, 0, 0, 1605, 1585, 1581, 1576, 1575, 32, 1513, 1500,
    1493, 1501, 3, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0,
    55, 522, 0, 0, 96, 730, 0, 0, 0, 1, 1, 32, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581,
    1576, 1575, 32, 1513, 1500, 1493, 1501, 3, 2, 1, 4, 23, 684, 0,
    0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730, 0, 0, 0,
    1, 1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 1605, 1585, 1581, 1576, 1575, 32, 1513, 1500, 1493, 1501, 3, 2,
    1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0,
    96, 730, 0, 0, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0,
    0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31,
    356, -40, 0, 79, 525, 0, 0, 97, 1605, 1585, 1581, 1576, 1575, 98,
    1513, 1500, 1493, 1501, 99, 5, 0, 0, 1, 97, 1, 1, 6, 9,
    291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0,
    0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0, 1, 98, 2,
    1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0,
    96, 730, 0, 0, 0, 0, 1, 99, 97, 1605, 1585, 1581, 1576, 1575,
    98, 1513, 1500, 1493, 1501, 99, 5, 0, 2, 1, 99, 2, 1, 4,
    23, 684, 0, 0, 124, 291, -10, 0, 55, 522, 0, 0, 96, 730,
    0, 0, 0, 2, 1, 98, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 0, 2, 1, 97, 97, 1605, 1585, 1581, 1576,
    1575, 98, 1513, 1500, 1493, 1501, 99, 5, 0, 0, 1, 97, 1, 1,
    6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0, 1,
    98, 2, 1, 4, 23, 684, 0, 0, 124, 291, -10, 0, 55, 522,
    0, 0, 96, 730, 0, 0, 0, 0, 1, 99, 1604, 1575, 32, 1604,
    1575, 32, 97, 98, 99, 4, 1, 1, 2, 10, 363, 0, 0, 73,
    219, 0, 0, 0, 1, 1, 32, 1, 1, 2, 10, 363, 0, 0,
    73, 219, 0, 0, 0, 0, 4, 32, 97, 98, 99, 1604, 1575, 32,
    1604, 1575, 32, 97, 98, 99, 5, 0, 2, 3, 97, 98, 99, 0,
    1, 1, 32, 1, 1, 2, 10, 363, 0, 0, 73, 219, 0, 0,
    0, 1, 1, 32, 1, 1, 2, 10, 363, 0, 0, 73, 219, 0,
    0, 1604, 1575, 32, 1604, 1575, 32, 97, 98, 99, 5, 0, 2, 3,
    97, 98, 99, 0, 1, 1, 32, 1, 1, 2, 10, 363, 0, 0,
    73, 219, 0, 0, 0, 1, 1, 32, 1, 1, 2, 10, 363, 0,
    0, 73, 219, 0, 0, 97, 98, 99, 32, 1604, 1575, 2, 0, 0,
    4, 97, 98, 99, 32, 1, 1, 2, 10, 363, 0, 0, 73, 219,
    0, 0, 97, 98, 99, 32, 1604, 1575, 3, 1, 1, 2, 10, 363,
    0, 0, 73, 219, 0, 0, 0, 1, 1, 32, 0, 2, 3, 97,
    98, 99, 97, 98, 99, 32, 1604, 1575, 2, 0, 0, 4, 97, 98,
    99, 32, 1, 1, 2, 10, 363, 0, 0, 73, 219, 0, 0, 1605,
    1585, 1581, 1576, 1575, 8204, 1603, 1578, 1575, 1576, 1, 1, 1, 12, 316,
    0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54,
    -180, 18, 373, 0, 0, 61, 412, 0, 0, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575, 8204, 1603, 1578, 1575,
    1576, 1, 1, 1, 12, 316, 0, 410, -23, 14, 993, 0, 0, 9,
    291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0,
    0, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27,
    589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581,
    1576, 1575, 8204, 1603, 1578, 1575, 1576, 1, 1, 1, 12, 316, 0, 410,
    -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18,
    373, 0, 0, 61, 412, 0, 0, 9, 291, 0, 0, 316, 0, 94,
    -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79,
    525, 0, 0, 97, 98, 99, 8207, 1605, 1585, 1581, 1576, 1575, 8206, 32,
    100, 101, 102, 4, 0, 0, 3, 97, 98, 99, 1, 1, 6, 9,
    291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0,
    0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 0, 0, 0,
    4, 32, 100, 101, 102, 97, 98, 99, 8207, 1605, 1585, 1581, 1576, 1575,
    8206, 32, 100, 101, 102, 4, 0, 2, 4, 32, 100, 101, 102, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 1,
    0, 0, 2, 3, 97, 98, 99, 97, 98, 99, 8207, 1605, 1585, 1581,
    1576, 1575, 8206, 32, 100, 101, 102, 4, 0, 0, 3, 97, 98, 99,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    1, 0, 0, 0, 4, 32, 100, 101, 102, 8235, 97, 98, 99, 32,
    1605, 1585, 1581, 1576, 1575, 8236, 32, 100, 101, 102, 4, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 32,
    0, 2, 3, 97, 98, 99, 0, 0, 4, 32, 100, 101, 102, 8235,
    97, 98, 99, 32, 1605, 1585, 1581, 1576, 1575, 8236, 32, 100, 101, 102,
    5, 0, 2, 3, 100, 101, 102, 0, 1, 1, 32, 1, 3, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 3, 1, 32,
    0, 4, 3, 97, 98, 99, 8235, 97, 98, 99, 32, 1605, 1585, 1581,
    1576, 1575, 8236, 32, 100, 101, 102, 4, 1, 1, 6, 9, 291, 0,
    0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31,
    356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 32, 0, 2, 3,
    97, 98, 99, 0, 0, 4, 32, 100, 101, 102, 8295, 97, 98, 99,
    8297, 32, 1605, 1585, 1581, 1576, 1575, 4, 0, 0, 0, 0, 2, 3,
    97, 98, 99, 0, 0, 1, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 8295, 97, 98, 99, 8297, 32, 1605, 1585,
    1581, 1576, 1575, 4, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94,
    -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79,
    525, 0, 0, 0, 1, 1, 32, 0, 4, 3, 97, 98, 99, 0,
    1, 0, 8295, 97, 98, 99, 8297, 32, 1605, 1585, 1581, 1576, 1575, 4,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    1, 1, 32, 0, 4, 3, 97, 98, 99, 0, 1, 0, 32, 32,
    1605, 1585, 1581, 1576, 1575, 32, 32, 3, 0, 0, 2, 32, 32, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0,
    2, 32, 32, 32, 32, 1605, 1585, 1581, 1576, 1575, 32, 32, 3, 0,
    1, 2, 32, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94,
    -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79,
    525, 0, 0, 0, 1, 2, 32, 32, 32, 32, 1605, 1585, 1581, 1576,
    1575, 32, 32, 3, 0, 1, 2, 32, 32, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 2, 32, 32, 97,
    98, 99, 32, 32, 32, 1, 0, 0, 6, 97, 98, 99, 32, 32,
    32, 97, 98, 99, 32, 32, 32, 2, 0, 1, 3, 32, 32, 32,
    0, 2, 3, 97, 98, 99, 97, 98, 99, 32, 32, 32, 1, 0,
    0, 6, 97, 98, 99, 32, 32, 32, 1605, 1585, 1581, 1576, 1575, 32,
    32, 32, 2, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 0, 0, 3, 32, 32, 32, 1605, 1585, 1581, 1576, 1575, 32,
    32, 32, 2, 0, 1, 3, 32, 32, 32, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575, 32,
    32, 32, 2, 0, 1, 3, 32, 32, 32, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 97, 32, 98, 32, 99, 32,
    1605, 1585, 1581, 1576, 1575, 32, 100, 32, 101, 32, 102, 3, 0, 0,
    6, 97, 32, 98, 32, 99, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 0, 0, 6, 32, 100, 32, 101, 32,
    102, 97, 32, 98, 32, 99, 32, 1605, 1585, 1581, 1576, 1575, 32, 100,
    32, 101, 32, 102, 5, 0, 2, 5, 100, 32, 101, 32, 102, 0,
    1, 1, 32, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3,
    16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525,
    0, 0, 0, 1, 1, 32, 0, 2, 5, 97, 32, 98, 32, 99,
    97, 32, 98, 32, 99, 32, 1605, 1585, 1581, 1576, 1575, 32, 100, 32,
    101, 32, 102, 3, 0, 0, 6, 97, 32, 98, 32, 99, 32, 1,
    1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0,
    27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0,
    6, 32, 100, 32, 101, 32, 102, 123, 1605, 1585, 1581, 1576, 1575, 125,
    3, 0, 0, 1, 123, 1, 1, 6, 9, 291, 0, 0, 316, 0,
    94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0,
    79, 525, 0, 0, 0, 0, 1, 125, 123, 1605, 1585, 1581, 1576, 1575,
    125, 3, 0, 1, 1, 123, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 0, 1, 1, 125, 123, 1605, 1585, 1581, 1576,
    1575, 125, 3, 0, 1, 1, 123, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 0, 1, 1, 125, 60, 1605, 1585, 1581,
    1576, 1575, 62, 3, 0, 0, 1, 60, 1, 1, 6, 9, 291, 0,
    0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31,
    356, -40, 0, 79, 525, 0, 0, 0, 0, 1, 62, 60, 1605, 1585,
    1581, 1576, 1575, 62, 3, 0, 1, 1, 60, 1, 1, 6, 9, 291,
    0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0,
    31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 62, 60, 1605,
    1585, 1581, 1576, 1575, 62, 3, 0, 1, 1, 60, 1, 1, 6, 9,
    291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0,
    0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 1, 1, 62, 1605,
    1585, 1581, 1576, 1575, 40, 97, 98, 99, 41, 1603, 1578, 1575, 1576, 3,
    1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0,
    0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0,
    0, 5, 40, 97, 98, 99, 41, 1, 1, 6, 316, 0, 410, -23,
    14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373,
    0, 0, 61, 412, 0, 0, 1605, 1585, 1581, 1576, 1575, 40, 97, 98,
    99, 41, 1603, 1578, 1575, 1576, 5, 1, 1, 6, 316, 0, 410, -23,
    14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373,
    0, 0, 61, 412, 0, 0, 0, 1, 1, 40, 0, 2, 3, 97,
    98, 99, 0, 1, 1, 41, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 1605, 1585, 1581, 1576, 1575, 40, 97, 98, 99,
    41, 1603, 1578, 1575, 1576, 5, 1, 1, 6, 316, 0, 410, -23, 14,
    993, 0, 0, 9, 291, 0, 0, 287, 0, 54, -180, 18, 373, 0,
    0, 61, 412, 0, 0, 0, 1, 1, 40, 0, 2, 3, 97, 98,
    99, 0, 1, 1, 41, 1, 1, 6, 9, 291, 0, 0, 316, 0,
    94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0,
    79, 525, 0, 0, 65, 1605, 1585, 1581, 1576, 1575, 66, 1603, 1578, 1575,
    1576, 67, 5, 0, 0, 1, 65, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 0, 0, 1, 66, 1, 1, 6, 316,
    0, 410, -23, 14, 993, 0, 0, 9, 291, 0, 0, 287, 0, 54,
    -180, 18, 373, 0, 0, 61, 412, 0, 0, 0, 0, 1, 67, 65,
    1605, 1585, 1581, 1576, 1575, 66, 1603, 1578, 1575, 1576, 67, 5, 0, 2,
    1, 67, 1, 1, 6, 316, 0, 410, -23, 14, 993, 0, 0, 9,
    291, 0, 0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0,
    0, 0, 2, 1, 66, 1, 1, 6, 9, 291, 0, 0, 316, 0,
    94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0,
    79, 525, 0, 0, 0, 2, 1, 65, 65, 1605, 1585, 1581, 1576, 1575,
    66, 1603, 1578, 1575, 1576, 67, 5, 0, 0, 1, 65, 1, 1, 6,
    9, 291, 0, 0, 316, 0, 94, -3, 16, 343, 0, 0, 27, 589,
    0, 0, 31, 356, -40, 0, 79, 525, 0, 0, 0, 0, 1, 66,
    1, 1, 6, 316, 0, 410, -23, 14, 993, 0, 0, 9, 291, 0,
    0, 287, 0, 54, -180, 18, 373, 0, 0, 61, 412, 0, 0, 0,
    0, 1, 67, 97, 98, 99, 32, 100, 101, 102, 32, 103, 104, 105,
    1, 0, 2, 11, 97, 98, 99, 32, 100, 101, 102, 32, 103, 104,
    105, 1605, 1585, 1581, 1576, 1575, 32, 97, 98, 99, 3, 0, 2, 3,
    97, 98, 99, 0, 1, 1, 32, 1, 1, 6, 9, 291, 0, 0,
    316, 0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356,
    -40, 0, 79, 525, 0, 0, 49, 50, 51, 32, 1605, 1585, 1581, 1576,
    1575, 3, 1, 1, 6, 9, 291, 0, 0, 316, 0, 94, -3, 16,
    343, 0, 0, 27, 589, 0, 0, 31, 356, -40, 0, 79, 525, 0,
    0, 0, 1, 1, 32, 0, 2, 3, 49, 50, 51, 40, 1605, 1585,
    1581, 1576, 1575, 41, 32, 97, 98, 99, 4, 0, 2, 3, 97, 98,
    99, 0, 1, 2, 32, 40, 1, 1, 6, 9, 291, 0, 0, 316,
    0, 94, -3, 16, 343, 0, 0, 27, 589, 0, 0, 31, 356, -40,
    0, 79, 525, 0, 0, 0, 1, 1, 41, 0, 32, 1, 0, 1,
    1, 32,
};
static const p32 mixed_case[MIXED_CASES][4] = {
    {0, 0, 13, 13},
    {1, 55, 13, 68},
    {2, 116, 13, 129},
    {0, 171, 14, 185},
    {1, 248, 14, 262},
    {2, 331, 14, 345},
    {0, 414, 9, 423},
    {1, 458, 9, 467},
    {2, 505, 9, 514},
    {0, 549, 9, 558},
    {1, 593, 9, 602},
    {2, 640, 9, 649},
    {0, 687, 7, 694},
    {1, 730, 7, 737},
    {2, 773, 7, 780},
    {0, 816, 15, 831},
    {1, 875, 15, 890},
    {2, 940, 15, 955},
    {0, 999, 18, 1017},
    {1, 1093, 18, 1111},
    {2, 1190, 18, 1208},
    {0, 1284, 9, 1293},
    {1, 1331, 9, 1340},
    {2, 1378, 9, 1387},
    {0, 1425, 9, 1434},
    {1, 1469, 9, 1478},
    {2, 1516, 9, 1525},
    {0, 1563, 15, 1578},
    {1, 1648, 15, 1663},
    {2, 1733, 15, 1748},
    {0, 1818, 11, 1829},
    {1, 1869, 11, 1880},
    {2, 1923, 11, 1934},
    {0, 1977, 15, 1992},
    {1, 2036, 15, 2051},
    {2, 2101, 15, 2116},
    {0, 2160, 7, 2167},
    {1, 2203, 7, 2210},
    {2, 2246, 7, 2253},
    {0, 2289, 11, 2300},
    {1, 2363, 11, 2374},
    {2, 2437, 11, 2448},
    {0, 2511, 10, 2521},
    {1, 2557, 10, 2567},
    {2, 2606, 10, 2616},
    {0, 2655, 18, 2673},
    {1, 2757, 18, 2775},
    {2, 2865, 18, 2883},
    {0, 2973, 14, 2987},
    {1, 3028, 14, 3042},
    {2, 3086, 14, 3100},
    {0, 3141, 19, 3160},
    {1, 3231, 19, 3250},
    {2, 3324, 19, 3343},
    {0, 3414, 9, 3423},
    {1, 3458, 9, 3467},
    {2, 3505, 9, 3514},
    {0, 3549, 9, 3558},
    {1, 3593, 9, 3602},
    {2, 3640, 9, 3649},
    {0, 3687, 12, 3699},
    {1, 3733, 12, 3745},
    {2, 3785, 12, 3797},
    {0, 3831, 13, 3844},
    {1, 3891, 13, 3904},
    {2, 3957, 13, 3970},
    {0, 4023, 12, 4035},
    {1, 4078, 12, 4090},
    {2, 4136, 12, 4148},
    {0, 4191, 15, 4206},
    {1, 4261, 15, 4276},
    {2, 4334, 15, 4349},
    {0, 4407, 6, 4413},
    {1, 4441, 6, 4447},
    {2, 4475, 6, 4481},
    {0, 4509, 14, 4523},
    {1, 4559, 14, 4573},
    {2, 4615, 14, 4629},
    {0, 4665, 13, 4678},
    {1, 4731, 13, 4744},
    {2, 4797, 13, 4810},
    {0, 4863, 18, 4881},
    {1, 4946, 18, 4964},
    {2, 5035, 18, 5053},
    {0, 5118, 14, 5132},
    {1, 5190, 14, 5204},
    {2, 5265, 14, 5279},
    {0, 5340, 10, 5350},
    {1, 5401, 10, 5411},
    {2, 5462, 10, 5472},
    {0, 5523, 12, 5535},
    {1, 5594, 12, 5606},
    {2, 5665, 12, 5677},
    {0, 5736, 9, 5745},
    {1, 5779, 9, 5788},
    {2, 5825, 9, 5834},
    {0, 5871, 6, 5877},
    {1, 5896, 6, 5902},
    {2, 5924, 6, 5930},
    {0, 5949, 10, 5959},
    {1, 6011, 10, 6021},
    {2, 6073, 10, 6083},
    {0, 6135, 14, 6149},
    {1, 6193, 14, 6207},
    {2, 6251, 14, 6265},
    {0, 6309, 15, 6324},
    {1, 6369, 15, 6384},
    {2, 6432, 15, 6447},
    {0, 6492, 11, 6503},
    {1, 6544, 11, 6555},
    {2, 6596, 11, 6607},
    {0, 6648, 9, 6657},
    {1, 6695, 9, 6704},
    {2, 6742, 9, 6751},
    {0, 6789, 6, 6795},
    {1, 6805, 6, 6811},
    {2, 6824, 6, 6830},
    {0, 6840, 8, 6848},
    {1, 6882, 8, 6890},
    {2, 6924, 8, 6932},
    {0, 6966, 17, 6983},
    {1, 7029, 17, 7046},
    {2, 7098, 17, 7115},
    {0, 7161, 7, 7168},
    {1, 7204, 7, 7211},
    {2, 7247, 7, 7254},
    {0, 7290, 7, 7297},
    {1, 7333, 7, 7340},
    {2, 7376, 7, 7383},
    {0, 7419, 14, 7433},
    {1, 7496, 14, 7510},
    {2, 7579, 14, 7593},
    {0, 7662, 12, 7674},
    {1, 7741, 12, 7753},
    {2, 7820, 12, 7832},
    {1, 7899, 11, 7910},
    {2, 7925, 9, 7934},
    {2, 7972, 9, 7981},
    {2, 8019, 11, 8030},
    {0, 8073, 0, 8073},
    {1, 8074, 1, 8075},
};

/*
        The faces of this tree the lines are laid with: Inter, and Noto Sans
        Arabic and Hebrew in Regular and Bold behind it.
*/
static p8 shape_inter_bytes[1 << 19], shape_arabic_bytes[2][1 << 18], shape_hebrew_bytes[2][1 << 17];
static font_face shape_inter, shape_arabic[2], shape_hebrew[2];
static font_place shape_places[FONT_PARA + 16];
static p32 shape_pixels[1280 * 96];

#define SHAPE_SIZE (32 * 64)
#define LINE_SIZE (20 * 64)

static p8 shape_load(void)
{
        static const string_address arabic[2] = {"fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf",
                                                 "fonts/noto-sans-arabic/NotoSansArabic-Bold.ttf"};
        static const string_address hebrew[2] = {"fonts/noto-sans-hebrew/NotoSansHebrew-Regular.ttf",
                                                 "fonts/noto-sans-hebrew/NotoSansHebrew-Bold.ttf"};
        bipolar n = file_slurp_regular_at(AT_FDCWD, "fonts/inter/Inter-Regular.ttf", shape_inter_bytes,
                                          sizeof(shape_inter_bytes), 0);
        p8 ok = n > 0 && font_load(&shape_inter, shape_inter_bytes, (positive)n, 0) == 0;

        for (p32 i = 0; i < 2; i++)
        {
                n = file_slurp_regular_at(AT_FDCWD, arabic[i], shape_arabic_bytes[i], sizeof(shape_arabic_bytes[i]), 0);
                ok = ok && n > 0 && font_load(&shape_arabic[i], shape_arabic_bytes[i], (positive)n, 0) == 0;
                n = file_slurp_regular_at(AT_FDCWD, hebrew[i], shape_hebrew_bytes[i], sizeof(shape_hebrew_bytes[i]), 0);
                ok = ok && n > 0 && font_load(&shape_hebrew[i], shape_hebrew_bytes[i], (positive)n, 0) == 0;
        }
        return ok;
}

//      UTF-8 of n code points.
static positive shape_utf8(const p32 address_to codes, positive n, p8 address_to into, positive room)
{
        positive length = 0;

        for (positive k = 0; k < n; k++)
                length += memory_utf8_encode(into + length, room - length, codes[k]);
        return length;
}

static const font_face address_to shape_face(p32 which)
{
        return which < 2 ? &shape_arabic[which] : &shape_hebrew[which - 2];
}

/*
        The glyphs HarfBuzz gave for each sequence of the corpus above, laid
        by the engine as a right to left paragraph over the one face: each
        place's glyph, its advance, its pen and the lift of its offset are
        the numbers of hb-shape scaled as the engine scales a font unit (once,
        to 16.16, and added), its level odd, its cluster inside the text, and
        font_extent and font_draw answer the pen the places end at.
*/
static fn check_shaping(void)
{
        static p8 text[512];
        font_target target = {shape_pixels, 1280, 1280, 96};
        positive wrong = 0, wrong_extent = 0;

        for (positive c = 0; c < SHAPE_CASES; c++)
        {
                const p32 address_to def = shape_case[c];
                const font_face address_to face = shape_face(def[0]);
                const font_face address_to chain[1] = {face};
                positive length = shape_utf8(shape_codes + def[1], def[2], text, sizeof(text));
                positive n = font_layout(&engine, chain, 1, text, length, SHAPE_SIZE, FONT_RTL, shape_places, 64);
                b64 pen = 0;
                p8 same = n == def[4];
                positive at = 0;

                for (positive k = 0; k < n && same; k++)
                {
                        const b16 address_to g = &shape_glyphs[4 * (def[3] + k)];
                        const font_place address_to p = &shape_places[k];
                        b64 advance = font_fixed(face, g[1], SHAPE_SIZE);

                        same = p->glyph == (p32)g[0] && p->face == 0 && (p->level & 1) && p->advance == advance &&
                               p->x == pen + font_fixed(face, g[2], SHAPE_SIZE) &&
                               p->y_offset == font_fixed(face, g[3], SHAPE_SIZE) && p->cluster < length;
                        pen += advance;
                        at = k;
                }
                if (!same)
                {
                        wrong++;
                        if (wrong < 4)
                                string_format(log, "  shape case %p (face %p, %p code points, %p glyphs, got %p) differs from HarfBuzz at glyph %p: got %p want %p\n", c,
                                              def[0], def[2], def[4], n, at, at < n ? shape_places[at].glyph : 0,
                                              (positive)shape_glyphs[4 * (def[3] + at)]);
                }
                if (font_extent(&engine, chain, 1, text, length, SHAPE_SIZE, FONT_RTL) != pen ||
                    font_draw(&engine, chain, 1, text, length, SHAPE_SIZE, FONT_RTL, &target, 8 << 16, 60, 0x101010,
                              0xf0f0f0) != pen)
                        wrong_extent++;
        }
        log_flush();
        check("shape: the glyphs, advances, pens and lifts of HarfBuzz for every sequence of the corpus", wrong == 0);
        check("shape: font_extent and font_draw answer the pen font_layout ends at, right to left", wrong_extent == 0);
}

//      Whether none of the code points is right to left, an Arabic number or a directional control, by the class table.
static p8 shape_plain(const p32 address_to codes, positive n)
{
        for (positive k = 0; k < n; k++)
        {
                p32 type = font_bidi_class(codes[k]);

                if (type == FONT_BIDI_R || type == FONT_BIDI_AL || type == FONT_BIDI_AN || type >= FONT_BIDI_LRE)
                        return false;
        }
        return true;
}

//      The expected places of a mixed case, laid by walking its runs: Latin runs by the engine's own glyph, advance and
//      kerning calls, shaped runs by HarfBuzz's numbers.
static p8 mixed_agrees(positive c, const font_place address_to places, positive n, b64 address_to total)
{
        const p32 address_to def = mixed_case[c];
        positive at = def[3] + 1;
        positive runs = (positive)mixed_stream[def[3]], k = 0;
        b64 pen = 0;

        for (positive r = 0; r < runs; r++)
        {
                p32 face = (p32)mixed_stream[at], level = (p32)mixed_stream[at + 1], count = (p32)mixed_stream[at + 2];
                const font_face address_to one = face == 0 ? &shape_inter : face == 1 ? &shape_arabic[0] : &shape_hebrew[0];

                at += 3;
                for (p32 i = 0; i < count; i++)
                {
                        if (k >= n)
                                return false;

                        const font_place address_to p = &places[k++];

                        if (face == 0)
                        {
                                p32 glyph = font_glyph(one, (p32)mixed_stream[at++]);

                                if (i)
                                        pen += font_kern(&engine, one, places[k - 2].glyph, glyph, LINE_SIZE);
                                if (p->glyph != glyph || p->face != 0 || p->level != level || p->x != pen ||
                                    p->advance != font_advance(one, glyph, LINE_SIZE) || p->y_offset != 0)
                                        return false;
                                pen += p->advance;
                        }
                        else
                        {
                                b64 advance = font_fixed(one, mixed_stream[at + 1], LINE_SIZE);

                                if (p->glyph != (p32)mixed_stream[at] || p->face != face || p->level != level ||
                                    p->advance != advance || p->x != pen + font_fixed(one, mixed_stream[at + 2], LINE_SIZE) ||
                                    p->y_offset != font_fixed(one, mixed_stream[at + 3], LINE_SIZE) || (level & 1) == 0)
                                        return false;
                                pen += advance;
                                at += 4;
                        }
                }
        }
        *total = pen;
        return k == n;
}

/*
        Lines with Latin, digits, brackets, Arabic and Hebrew in all three
        paragraph directions, over Inter with Noto behind it: the order of the
        runs, the face and level of every glyph, the mirrored brackets, and
        the shaped runs, against fribidi's levels and hb-shape. And the same
        lines drawn and measured.
*/
static fn check_mixed(void)
{
        static p8 text[256];
        font_target target = {shape_pixels, 1280, 1280, 96};
        const font_face address_to chain[1] = {&shape_inter};
        positive wrong = 0, others = 0;

        for (positive c = 0; c < MIXED_CASES; c++)
        {
                const p32 address_to def = mixed_case[c];
                p32 flags = def[0] == 0 ? 0 : def[0] == 1 ? FONT_RTL : FONT_AUTO;
                p32 codes[128];
                b64 total = 0;

                for (positive k = 0; k < def[2]; k++)
                        codes[k] = (p32)mixed_stream[def[1] + k];

                positive length = shape_utf8(codes, def[2], text, sizeof(text));
                positive n = font_layout(&engine, chain, 1, text, length, LINE_SIZE, flags, shape_places, 256);

                if (!mixed_agrees(c, shape_places, n, &total))
                {
                        wrong++;
                        if (wrong < 4)
                                string_format(log, "  mixed case %p (direction %p) differs\n", c, def[0]);
                }
                if (font_extent(&engine, chain, 1, text, length, LINE_SIZE, flags) != total ||
                    font_draw(&engine, chain, 1, text, length, LINE_SIZE, flags, &target, 4 << 16, 60, 0x101010, 0xf0f0f0) !=
                        total ||
                    (def[0] != 2 && font_base_level(text, length, flags) != (def[0] == 1)) ||
                    font_is_simple(text, length) != shape_plain(codes, def[2]))
                        others++;
        }
        log_flush();
        check("mixed: runs, faces, levels, mirrors, advances and lifts against fribidi and HarfBuzz, all three directions",
              wrong == 0);
        check("mixed: font_extent, font_draw and font_base_level agree with the places", others == 0);
}

/*
        Segments for a caret. A line is cut into segments at the byte offsets
        a test names; what is asked is where each sits, and it is held to the
        places the same line lays: left to right a segment is its glyph's
        cell; right to left the first in reading order is at the right end and
        the cells tile; a glyph for several segments is shared in equal
        parts; a joiner or a mark by itself is a point at the edge of its
        neighbour.
*/
static void segment_cuts(positive length, const positive address_to at, positive n, p32 address_to cuts)
{
        for (positive k = 0; k <= n; k++)
                cuts[k] = (p32)(k < n ? at[k] : length);
}

static fn check_segments(void)
{
        static b32 left[64], right[64];
        static p8 level[64];
        static p32 cuts[65];
        const font_face address_to chain[1] = {&shape_inter};
        const font_face address_to arabic[1] = {&shape_arabic[0]};
        static const p8 latin[] = "Waterline AVA";
        static const p8 word[] = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
        static const p8 mixed[] = "abc \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 def";
        static const p8 hebrew[] = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
        static const p8 allah[] = "\xd8\xa7\xd9\x84\xd9\x84\xd9\x91\xd9\xb0\xd9\x87";
        static const p8 joined[] = "\xd8\xa8\xe2\x80\x8d\xd8\xa7";
        static const p8 marked[] = "\xd8\xa8\xd9\x8e\xd8\xa7";
        positive at[64];
        positive n, bad = 0;
        b64 extent;

        //      Left to right: each segment is its glyph's cell, kerning and all.
        for (positive k = 0; k < 13; k++)
                at[k] = k;
        segment_cuts(13, at, 13, cuts);
        n = font_segments(&engine, chain, 1, latin, 13, LINE_SIZE, 0, cuts, 13, left, right, level);
        positive count = font_layout(&engine, chain, 1, latin, 13, LINE_SIZE, 0, shape_places, 64);
        p8 same = n == 13 && count == 13;

        for (positive k = 0; k < 13 && same; k++)
                same = left[k] == (b32)shape_places[k].x && right[k] == (b32)(shape_places[k].x + shape_places[k].advance) &&
                       level[k] == 0;
        check("segments: left to right, each is its glyph's cell and level 0", same);

        //      Right to left, one segment a letter: the first is at the right end and they tile, the dots belonging to their letter.
        for (positive k = 0; k < 5; k++)
                at[k] = 2 * k;
        segment_cuts(10, at, 5, cuts);
        extent = font_extent(&engine, arabic, 1, word, 10, LINE_SIZE, FONT_RTL);
        n = font_segments(&engine, arabic, 1, word, 10, LINE_SIZE, FONT_RTL, cuts, 5, left, right, level);
        same = n == 5 && right[0] == (b32)extent && left[4] == 0;
        for (positive k = 0; k < 5 && same; k++)
                same = (level[k] & 1) && left[k] < right[k] && (k == 0 || right[k] == left[k - 1]);
        check("segments: right to left, the first letter at the right end, the letters tiling the line", same);

        //      Hebrew, the same.
        for (positive k = 0; k < 4; k++)
                at[k] = 2 * k;
        segment_cuts(8, at, 4, cuts);
        const font_face address_to hechain[1] = {&shape_hebrew[0]};

        extent = font_extent(&engine, hechain, 1, hebrew, 8, LINE_SIZE, FONT_RTL);
        n = font_segments(&engine, hechain, 1, hebrew, 8, LINE_SIZE, FONT_RTL, cuts, 4, left, right, level);
        same = n == 4 && right[0] == (b32)extent && left[3] == 0;
        for (positive k = 1; k < 4 && same; k++)
                same = right[k] == left[k - 1] && (level[k] & 1);
        check("segments: Hebrew, the same", same);

        //      A mixed line cut at every code point: levels, and no two segments overlapping by more than kerning.
        static const p32 expect[13] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0};

        for (positive k = 0; k < 13; k++)
                at[k] = k < 4 ? k : k < 9 ? 4 + 2 * (k - 4) : 14 + (k - 9);
        segment_cuts(sizeof(mixed) - 1, at, 13, cuts);
        n = font_segments(&engine, chain, 1, mixed, sizeof(mixed) - 1, LINE_SIZE, 0, cuts, 13, left, right, level);
        extent = font_extent(&engine, chain, 1, mixed, sizeof(mixed) - 1, LINE_SIZE, 0);
        same = n == 13;
        for (positive k = 0; k < 13 && same; k++)
                same = level[k] == expect[k] && left[k] <= right[k] && right[k] <= (b32)extent + 1;
        p8 order = same;

        //      Reading order: the Arabic letters go right to left inside their run, the Latin around it left to right.
        for (positive k = 1; k < 4 && order; k++)
                order = left[k] >= left[k - 1];
        for (positive k = 5; k < 9 && order; k++)
                order = right[k] <= left[k - 1] + 1;
        for (positive k = 10; k < 13 && order; k++)
                order = left[k] >= left[k - 1];
        order = order && left[8] >= right[3] - 1 && right[4] <= left[9] + 1;
        check("segments: a mixed line, the levels and the reading order within each run", same && order);

        //      One glyph for six code points, four segments of it: equal parts, the first in reading order at the right.
        at[0] = 0, at[1] = 2, at[2] = 4, at[3] = 10;
        segment_cuts(sizeof(allah) - 1, at, 4, cuts);
        count = font_layout(&engine, arabic, 1, allah, sizeof(allah) - 1, LINE_SIZE, FONT_RTL, shape_places, 8);
        extent = font_extent(&engine, arabic, 1, allah, sizeof(allah) - 1, LINE_SIZE, FONT_RTL);
        n = font_segments(&engine, arabic, 1, allah, sizeof(allah) - 1, LINE_SIZE, FONT_RTL, cuts, 4, left, right, level);
        same = count == 1 && n == 4 && right[0] == (b32)extent && left[3] == 0;
        for (positive k = 0; k < 4 && same; k++)
        {
                b32 width = right[k] - left[k], part = (b32)extent / 4;

                same = (level[k] & 1) && (width >= part - 1 && width <= part + 1) && (k == 0 || right[k] == left[k - 1]);
        }
        check("segments: a ligature of six code points shared in four equal parts, the first at the right", same);

        //      A joiner between letters is a point, and a mark by itself is a point at its base's edge.
        at[0] = 0, at[1] = 2, at[2] = 5;
        segment_cuts(sizeof(joined) - 1, at, 3, cuts);
        n = font_segments(&engine, arabic, 1, joined, sizeof(joined) - 1, LINE_SIZE, FONT_RTL, cuts, 3, left, right, level);
        same = n == 3 && left[1] == right[1] && left[1] == left[0] && right[0] > left[0] && right[2] == left[0];
        check("segments: a zero width joiner is a point at the edge of the letter before it", same);
        at[0] = 0, at[1] = 2, at[2] = 4;
        segment_cuts(sizeof(marked) - 1, at, 3, cuts);
        n = font_segments(&engine, arabic, 1, marked, sizeof(marked) - 1, LINE_SIZE, FONT_RTL, cuts, 3, left, right, level);
        same = n == 3 && left[1] == right[1] && (left[1] == left[0] || left[1] == right[0]) && (level[1] & 1);
        check("segments: a mark by itself is a point at the edge of its base", same);

        //      The limits, and nothing written when refused.
        static p8 big[FONT_SEGMENT_BYTES + 8];
        static p32 many[FONT_SEGMENTS + 8];
        static b32 wide[FONT_SEGMENTS + 8], wide_right[FONT_SEGMENTS + 8];
        static p8 wide_level[FONT_SEGMENTS + 8];

        for (positive k = 0; k < sizeof(big); k++)
                big[k] = 'a';
        for (positive k = 0; k < sizeof(many) / sizeof(many[0]); k++)
                many[k] = (p32)k;
        bad += font_segments(&engine, chain, 1, big, FONT_SEGMENT_BYTES + 1, LINE_SIZE, 0, many, 4, left, right, level) != 0;
        bad += font_segments(&engine, chain, 1, big, 100, LINE_SIZE, 0, many, FONT_SEGMENTS + 1, wide, wide_right, wide_level) != 0;
        bad += font_segments(&engine, chain, 1, big, 100, LINE_SIZE, 0, many, 0, left, right, level) != 0;
        many[100] = 100;
        bad += font_segments(&engine, chain, 1, big, FONT_SEGMENT_BYTES, LINE_SIZE, 0, many, 100, wide, wide_right, wide_level) != 100;
        check("segments: more than 4096 segments or 8192 bytes answer 0, the limits themselves are laid", bad == 0);
}

/*
        Pixels. The mirror glyph of a bracket in right to left text is the
        pixels of the other bracket in left to right text; the pen
        font_draw answers is the extent and the places'; and the ink of a
        mixed line is where its glyphs were put and nowhere else.
*/
static fn shape_clear(p32 ground)
{
        for (positive k = 0; k < sizeof(shape_pixels) / sizeof(shape_pixels[0]); k++)
                shape_pixels[k] = ground;
}

static p64 shape_ink(positive address_to first, positive address_to last, positive address_to inked)
{
        p64 hash = 0xcbf29ce484222325ull;

        *first = 1280;
        *last = 0;
        *inked = 0;
        for (positive y = 0; y < 96; y++)
                for (positive x = 0; x < 1280; x++)
                {
                        p32 pixel = shape_pixels[y * 1280 + x];

                        hash = (hash ^ pixel) * 0x100000001b3ull;
                        if (pixel != 0xf0f0f0)
                        {
                                *inked += 1;
                                *first = x < *first ? x : *first;
                                *last = x > *last ? x : *last;
                        }
                }
        return hash;
}

static fn check_render(void)
{
        font_target target = {shape_pixels, 1280, 1280, 96};
        const font_face address_to chain[1] = {&shape_inter};
        static const string_address pairs[][2] = {{"(", ")"}, {"[", "]"}, {"{", "}"}, {"<", ">"}, {"\xc2\xab", "\xc2\xbb"}};
        positive first, last, inked, wrong = 0;

        //      A bracket in right to left text is the other bracket.
        for (positive k = 0; k < sizeof(pairs) / sizeof(pairs[0]); k++)
                for (positive side = 0; side < 2; side++)
                {
                        p64 one, two;
                        positive length = 0;

                        while (pairs[k][side][length])
                                length++;
                        shape_clear(0xf0f0f0);
                        font_draw(&engine, chain, 1, (const p8 address_to)pairs[k][side], length, LINE_SIZE, FONT_RTL, &target, 8 << 16, 60,
                                  0x101010, 0xf0f0f0);
                        one = shape_ink(&first, &last, &inked);
                        shape_clear(0xf0f0f0);
                        font_draw(&engine, chain, 1, (const p8 address_to)pairs[k][1 - side], length, LINE_SIZE, 0, &target, 8 << 16, 60,
                                  0x101010, 0xf0f0f0);
                        two = shape_ink(&first, &last, &inked);
                        wrong += one != two || !inked;
                }
        check("render: a bracket drawn right to left is the pixels of the other bracket drawn left to right", wrong == 0);

        //      A mixed line: the pen is the extent, and the ink is within the glyphs' places.
        static const p8 line[] = "abc \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 123 (def) \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
        positive length = sizeof(line) - 1;
        const font_face address_to inter_only[1] = {&shape_inter};

        wrong = 0;
        for (p32 flags = 0; flags <= FONT_AUTO; flags += FONT_RTL)
        {
                b64 extent = font_extent(&engine, inter_only, 1, line, length, LINE_SIZE, flags);
                positive count = font_layout(&engine, inter_only, 1, line, length, LINE_SIZE, flags, shape_places, 64);
                b64 pen = 0;

                shape_clear(0xf0f0f0);
                b64 drawn = font_draw(&engine, inter_only, 1, line, length, LINE_SIZE, flags, &target, 8 << 16, 60, 0x101010,
                                      0xf0f0f0);
                shape_ink(&first, &last, &inked);
                for (positive k = 0; k < count; k++)
                        pen += shape_places[k].advance;
                wrong += drawn != extent || count == 0 || !inked || first + 4 < 8 || last > 8 + (positive)(extent >> 16) + 4;
                (void)pen;
        }
        check("render: font_draw answers the extent, and the ink of a mixed line lies within it", wrong == 0);

        //      Reading order of a right to left word: the first letter's place is the right end, and the pens and clusters run
        //      against each other.
        static const p8 word[] = "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9";
        const font_face address_to arabic[1] = {&shape_arabic[0]};
        positive count = font_layout(&engine, arabic, 1, word, sizeof(word) - 1, LINE_SIZE, FONT_RTL, shape_places, 64);
        p8 run = count > 6;

        for (positive k = 1; k < count && run; k++)
                run = shape_places[k].cluster <= shape_places[k - 1].cluster;
        check("render: the first letter of a right to left word is the last glyph and the clusters run backwards", run &&
              shape_places[count - 1].cluster == 0);
}

/*
        The question font_is_simple answers, of every kind of code point, in
        a text of its own and in the middle of Latin at every offset the
        eight byte scan can meet it: the class table says. And a text that
        is simple, laid by the whole engine, is the places the plain loop lays.
*/
static fn check_simple(void)
{
        static const p32 samples[] = {0x41, 0xe9, 0x3b1, 0x416, 0x531, 0x5be, 0x5d0, 0x600, 0x627, 0x660, 0x6f1, 0x700, 0x7c0, 0x800, 0x900,
                                      0xe01, 0x10a0, 0x1100, 0x1e00, 0x2010, 0x200c, 0x200d, 0x200e, 0x200f, 0x202a, 0x202b, 0x202c,
                                      0x202d, 0x202e, 0x2066, 0x2067, 0x2068, 0x2069, 0x2190, 0x2500, 0x3042, 0x4e2d, 0xac00,
                                      0xfb1d, 0xfb50, 0xfdf2, 0xfe70, 0xfeff, 0xff21, 0x10800, 0x10d30, 0x1e900, 0x1f600, 0xe0001};
        static p8 text[64];
        static p32 codes[64];
        positive wrong = 0;

        for (positive s = 0; s < sizeof(samples) / sizeof(samples[0]); s++)
                for (positive pad = 0; pad < 18; pad += 1)
                        for (positive tail = 0; tail < 10; tail += 3)
                        {
                                positive n = 0;

                                for (positive k = 0; k < pad; k++)
                                        codes[n++] = 'a' + (p32)k % 26;
                                codes[n++] = samples[s];
                                for (positive k = 0; k < tail; k++)
                                        codes[n++] = 'z' - (p32)k;

                                positive length = shape_utf8(codes, n, text, sizeof(text));

                                wrong += font_is_simple(text, length) != shape_plain(codes, n);
                        }
        check("simple: font_is_simple is the class table's answer for every kind of code point at every offset", wrong == 0);

        //      A simple text through the whole engine is the plain loop's places.
        static const string_address lines[] = {"Waterline AVATAR To Ty Wa", "caf\xc3\xa9 \xc3\x85ngstr\xc3\xb6m, \xc3\x9f\xc3\xb8",
                                               "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82, \xd0\xbc\xd0\xb8\xd1\x80 \xce\xb1\xce\xb2\xce\xb3",
                                               "\xe2\x94\x80\xe2\x94\xbc \xe2\x96\x92 x \xe4\xb8\xad \xff end", "e\xcc\x81 a\xcc\x88 1,234.50 ... ",
                                               "\xc2\xad soft \xe2\x80\x99 quote \xe2\x80\xa6", ""};
        static font_place plain[128], full[128];
        const font_face address_to two[2] = {&shape_inter, &shape_inter};

        wrong = 0;
        for (positive k = 0; k < sizeof(lines) / sizeof(lines[0]); k++)
        {
                positive length = 0;

                while (lines[k][length])
                        length++;
                positive a = font_layout(&engine, two, 2, (const p8 address_to)lines[k], length, LINE_SIZE, 0, plain, 128);
                positive b = font_layout_flow(&engine, two, 2, (const p8 address_to)lines[k], length, LINE_SIZE, 0, full, 128);
                p8 same = a == b && font_is_simple((const p8 address_to)lines[k], length);

                for (positive j = 0; j < a && same; j++)
                        same = plain[j].glyph == full[j].glyph && plain[j].face == full[j].face && plain[j].x == full[j].x &&
                               plain[j].advance == full[j].advance && plain[j].cluster == full[j].cluster && full[j].level == 0 &&
                               full[j].y_offset == 0;
                wrong += !same;
        }
        check("simple: a simple text laid by the whole engine is the places of the plain loop, kerning, cells and all", wrong == 0);
}

/*
        Fallback faces. Behind the chain, in the order added; the bold of a
        slot for FONT_BOLD and the regular emboldened by the engine for a
        slot with none; the face of a place told back by font_face_at; and a
        line laid with the slot's face is what the face alone lays.
*/
static font_engine second_engine;
static p8 second_atlas[256 * 256];

static fn check_fallback(void)
{
        const font_face address_to chain[1] = {&shape_inter};
        static p8 text[256];
        positive wrong = 0, drawn = 0;

        check("fallback: the faces behind the chain are those added, in order",
              engine.fallbacks == 2 && font_face_at(&engine, chain, 1, 0, 0) == &shape_inter &&
                  font_face_at(&engine, chain, 1, 1, 0) == &shape_arabic[0] && font_face_at(&engine, chain, 1, 1, FONT_BOLD) == &shape_arabic[1] &&
                  font_face_at(&engine, chain, 1, 2, 0) == &shape_hebrew[0] && font_face_at(&engine, chain, 1, 2, FONT_BOLD) == &shape_hebrew[1]);

        //      Every case of the corpus set in Arabic Bold lays the same laid through the fallback with FONT_BOLD.
        for (positive c = 0; c < SHAPE_CASES; c++)
        {
                const p32 address_to def = shape_case[c];
                const font_face address_to bold = shape_face(def[0]);

                p8 alone = true;

                if (def[0] != 1 && def[0] != 3)
                        continue;

                //      Only the code points no face of the chain has: a space or a digit is Inter's.
                for (positive k = 0; k < def[2]; k++)
                        alone = alone && shape_codes[def[1] + k] >= 0x590;
                if (!alone)
                        continue;

                positive length = shape_utf8(shape_codes + def[1], def[2], text, sizeof(text));
                positive n = font_layout(&engine, chain, 1, text, length, SHAPE_SIZE, FONT_RTL | FONT_BOLD, shape_places, 64);
                b64 pen = 0;
                p8 same = n == def[4];

                for (positive k = 0; k < n && same; k++)
                {
                        const b16 address_to g = &shape_glyphs[4 * (def[3] + k)];

                        same = shape_places[k].glyph == (p32)g[0] && shape_places[k].face == (def[0] == 1 ? 1 : 2) &&
                               shape_places[k].advance == font_fixed(bold, g[1], SHAPE_SIZE) &&
                               shape_places[k].x == pen + font_fixed(bold, g[2], SHAPE_SIZE);
                        pen += shape_places[k].advance;
                }
                wrong += !same;
                drawn++;
        }
        check("fallback: asked for bold the slots' bold faces lay what the bold alone lays", wrong == 0 && drawn > 20);

        //      A slot with no bold is emboldened by the engine: the same line, the same pen, more ink.
        font_start(&second_engine, second_atlas, 256, 256);
        font_fallback(&second_engine, &shape_arabic[0], null);

        static const p8 word[] = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
        font_target target = {shape_pixels, 1280, 1280, 96};
        positive first, last, regular, heavy;

        shape_clear(0xf0f0f0);
        b64 a = font_draw(&second_engine, chain, 1, word, 10, 24 * 64, FONT_RTL, &target, 8 << 16, 60, 0x101010, 0xf0f0f0);
        shape_ink(&first, &last, &regular);
        shape_clear(0xf0f0f0);
        b64 b = font_draw(&second_engine, chain, 1, word, 10, 24 * 64, FONT_RTL | FONT_BOLD, &target, 8 << 16, 60, 0x101010, 0xf0f0f0);
        shape_ink(&first, &last, &heavy);
        check("fallback: a slot with no bold is emboldened by the engine: the same pen, more ink", a == b && heavy > regular);
        (void)last;
}

/*
        Hostile layout tables. Noto Sans Arabic is copied so that its last
        byte sits against a page that faults, and its GSUB, GPOS and GDEF
        (and now and then any other byte) are mutated thousands of times, the
        font cut at lengths all through, a rule of every chaining lookup made
        to call its own lookup, and lines in Arabic, Hebrew, with marks, with
        joiners and with a ligature laid, measured, drawn and cut in
        segments with each. Surviving is the check; what the engine answers
        is not judged. A read past the end ends the run.
*/
static p8 shape_hostile_font[1 << 18];
static p8 address_to shape_guard;
static positive shape_guard_pages, shape_cases_run;
static p64 shape_state = 0x2545f4914f6cdd1dull;

static p64 shape_next(p64 below)
{
        XORSHIFT64(shape_state);
        return below ? shape_state % below : 0;
}

static fn shape_exercise(const p8 address_to bytes, positive size)
{
        static const p8 arabic[] = "\xd9\x85\xd9\x8f\xd8\xad\xd9\x8e\xd9\x85\xd9\x8e\xd9\x91\xd8\xaf \xd9\x84\xd8\xa7 \xd8\xa7\xd9\x84\xd9\x84\xd9\x91\xd9\xb0\xd9\x87 "
                                   "\xd8\xa8\xe2\x80\x8d\xd8\xa7\xe2\x80\x8c\xd8\xa8\xd9\x80\xd8\xa8 \xdb\xb1\xdb\xb2 (\xd8\xa8)";
        static const p8 hebrew[] = "\xd7\xa9\xd6\xb8\xd7\x81\xd7\x9c\xd7\x95\xd6\xbc\xd7\x9d \xd7\x91\xd6\xb0\xd6\xbc\xd7\xa8\xd7\x90\xd7\xa9\xd7\x99\xd7\xaa abc";
        p8 address_to at = shape_guard + shape_guard_pages * 4096 - size;
        font_face face;
        font_target target = {shape_pixels, 1280, 1280, 96};
        static p32 cuts[24];
        static b32 left[24], right[24];
        static p8 level[24];

        memory_copy(at, bytes, size);
        shape_cases_run++;
        if (font_load(&face, at, size, 0))
                return;

        const font_face address_to chain[1] = {&face};
        const p8 address_to lines[2] = {arabic, hebrew};
        positive lengths[2] = {sizeof(arabic) - 1, sizeof(hebrew) - 1};

        for (positive l = 0; l < 2; l++)
        {
                font_layout(&engine, chain, 1, lines[l], lengths[l], 20 * 64, FONT_RTL, shape_places, 64);
                font_extent(&engine, chain, 1, lines[l], lengths[l], 20 * 64, FONT_AUTO);
                font_draw(&engine, chain, 1, lines[l], lengths[l], 20 * 64, FONT_RTL | FONT_BOLD, &target, 8 << 16, 60, 0x101010, 0xf0f0f0);
        }
        for (positive k = 0; k < 23; k++)
                cuts[k] = (p32)(k * 3);
        cuts[23] = (p32)lengths[0];
        if (cuts[22] < cuts[23])
                font_segments(&engine, chain, 1, arabic, lengths[0], 20 * 64, FONT_RTL, cuts, 23, left, right, level);
}

//      A rule of every chaining and context lookup of the GSUB that calls the lookup it is in.
static fn shape_recurse(p8 address_to font, positive size)
{
        positive tables = (positive)(font[4] << 8 | font[5]), gsub = 0;

        for (positive i = 0; i < tables; i++)
        {
                positive record = 12 + 16 * i;

                if (record + 16 <= size && font[record] == 'G' && font[record + 1] == 'S' && font[record + 2] == 'U' && font[record + 3] == 'B')
                        gsub = (positive)font[record + 8] << 24 | (positive)font[record + 9] << 16 | (positive)font[record + 10] << 8 | font[record + 11];
        }
        if (!gsub)
                return;

#define U16(at) ((positive)font[(at)] << 8 | font[(at) + 1])
        positive list = gsub + U16(gsub + 8), count = U16(list);

        for (positive i = 0; i < count && list + 2 + 2 * i + 2 <= size; i++)
        {
                positive lookup = list + U16(list + 2 + 2 * i), type = U16(lookup), subtables = U16(lookup + 4);

                for (positive k = 0; k < subtables && k < 16; k++)
                {
                        positive t = lookup + U16(lookup + 6 + 2 * k), kind = type, records = 0, n = 0;

                        if (type == 7)
                        {
                                kind = U16(t + 2);
                                t += (positive)font[t + 4] << 24 | (positive)font[t + 5] << 16 | (positive)font[t + 6] << 8 | font[t + 7];
                        }
                        if (t + 40 > size)
                                continue;
                        if (kind == 6 && U16(t) == 3)
                        {
                                positive back = U16(t + 2), inputs = U16(t + 4 + 2 * back), ahead_at = t + 6 + 2 * back + 2 * inputs;
                                positive ahead = U16(ahead_at);

                                n = U16(ahead_at + 2 + 2 * ahead);
                                records = ahead_at + 4 + 2 * ahead;
                        }
                        else if (kind == 5 && U16(t) == 3)
                        {
                                positive glyphs = U16(t + 2);

                                n = U16(t + 4);
                                records = t + 6 + 2 * glyphs;
                        }
                        for (positive r = 0; r < n && records + 4 * r + 4 <= size; r++)
                        {
                                font[records + 4 * r] = 0;
                                font[records + 4 * r + 1] = 0;
                                font[records + 4 * r + 2] = (p8)(i >> 8);
                                font[records + 4 * r + 3] = (p8)i;
                        }
                }
        }
#undef U16
}

static fn check_hostile_layout(const p8 address_to font, positive size)
{
        static p8 copy[1 << 18];
        positive gsub = 0, gpos = 0, gdef = 0, lengths[3] = {0, 0, 0}, tables = (positive)(font[4] << 8 | font[5]);
        positive before = shape_cases_run;

        for (positive i = 0; i < tables; i++)
        {
                positive record = 12 + 16 * i, offset = (positive)font[record + 8] << 24 | (positive)font[record + 9] << 16 |
                                                        (positive)font[record + 10] << 8 | font[record + 11];
                positive span = (positive)font[record + 12] << 24 | (positive)font[record + 13] << 16 | (positive)font[record + 14] << 8 | font[record + 15];

                if (font[record] == 'G' && font[record + 1] == 'S')
                        gsub = offset, lengths[0] = span;
                else if (font[record] == 'G' && font[record + 1] == 'P')
                        gpos = offset, lengths[1] = span;
                else if (font[record] == 'G' && font[record + 1] == 'D')
                        gdef = offset, lengths[2] = span;
        }

        //      Cuts through the file, and a stretch just before and after each layout table's start.
        for (positive k = 0; k < 500; k++)
                shape_exercise(font, size * k / 500);
        for (positive k = 0; k < 3; k++)
        {
                positive offset = k == 0 ? gsub : k == 1 ? gpos : gdef;

                for (positive cut = offset > 64 ? offset - 64 : 0; cut < offset + 200 && cut <= size; cut += 2)
                        shape_exercise(font, cut);
        }

        static const p32 extremes[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0xffffffff, 0x10000};

        for (positive m = 0; m < 12000; m++)
        {
                memory_copy(copy, font, size);
                p32 changes = 1 + (p32)shape_next(4);

                for (p32 k = 0; k < changes; k++)
                {
                        positive pick = shape_next(10), which = pick < 4 ? 0 : pick < 8 ? 1 : pick < 9 ? 2 : 3;
                        positive base = which == 0 ? gsub : which == 1 ? gpos : which == 2 ? gdef : 0;
                        positive span = which == 3 ? size : lengths[which] ? lengths[which] : 1;
                        positive where = base + shape_next(span);

                        if (where >= size)
                                where = size - 1;
                        if (shape_next(2))
                                copy[where] = (p8)shape_next(256);
                        else
                        {
                                p32 value = extremes[shape_next(8)], wide = shape_next(2) ? 4 : 2;

                                where &= ~(positive)1;
                                for (p32 b = 0; b < wide && where + b < size; b++)
                                        copy[where + b] = (p8)(value >> (8 * (wide - 1 - b)));
                        }
                }
                shape_exercise(copy, size);
        }

        //      Lookups that call themselves.
        memory_copy(copy, font, size);
        shape_recurse(copy, size);
        shape_exercise(copy, size);
        check("hostile: every cut and mutation of the layout tables survived against a guard page",
              shape_cases_run >= before + 12000 + 500);
}

/*
        The work a face's lookups do is paid for by the shaping budget a subtable at a time. Noto Sans Arabic with its
        GSUB pointed at one built here: rlig of 63 lookups of 256 subtables that cover every glyph and never apply,
        then a 64th that turns the Arabic-Indic one into the next glyph. A subtable tried was not charged, so a line of
        4096 of them tried 66 million (the fuzzer met 0.6 s for a few words); charged, the budget (FONT_OPS a code
        point) ends the walk inside the costly lookups and the last is never reached.
*/
static p8 shape_costly[1 << 19];

static fn costly_put16(p8 address_to at, p32 value)
{
        at[0] = (p8)(value >> 8), at[1] = (p8)value;
}

static fn check_costly_lookups(void)
{
        static p8 text[8192];
        bipolar n = file_slurp_regular_at(AT_FDCWD, "fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf", shape_costly,
                                          sizeof(shape_costly) - 40000, 0);
        positive base = n > 0 ? ((positive)n + 3) & ~(positive)3 : 0, tables, placed, started, took;
        p8 address_to g = shape_costly + base;
        p32 one = font_glyph(&shape_arabic[0], 0x661), turned = 0, kept = 0;
        font_face face;

        if (n <= 0 || !one)
        {
                check("costly lookups: Noto Sans Arabic loads, with an Arabic-Indic one", false);
                return;
        }
        //      GSUB 1.0: scripts at 10, features at 30, lookups at 172.
        costly_put16(g, 1), costly_put16(g + 2, 0), costly_put16(g + 4, 10), costly_put16(g + 6, 30), costly_put16(g + 8, 172);
        costly_put16(g + 10, 1), memory_copy(g + 12, "arab", 4), costly_put16(g + 16, 8);
        costly_put16(g + 18, 4), costly_put16(g + 20, 0);
        costly_put16(g + 22, 0), costly_put16(g + 24, 0xffff), costly_put16(g + 26, 1), costly_put16(g + 28, 0);
        costly_put16(g + 30, 1), memory_copy(g + 32, "rlig", 4), costly_put16(g + 36, 8);
        costly_put16(g + 38, 0), costly_put16(g + 40, 64);
        for (p32 k = 0; k < 64; k++)
                costly_put16(g + 42 + 2 * k, k);
        costly_put16(g + 172, 64);
        for (p32 k = 0; k < 64; k++)
                costly_put16(g + 174 + 2 * k, 302 + 518 * k - 172);
        //      Lookups 0 to 62: single substitutions of 256 subtables, every one the subtable at 32944, which covers
        //      every glyph and has no glyph to put (so never applies). Lookup 63, at 32936: one subtable at 32960.
        for (p32 k = 0; k < 63; k++)
        {
                p8 address_to lookup = g + 302 + 518 * k;

                costly_put16(lookup, 1), costly_put16(lookup + 2, 0), costly_put16(lookup + 4, 256);
                for (p32 t = 0; t < 256; t++)
                        costly_put16(lookup + 6 + 2 * t, 32944 - (302 + 518 * k));
        }
        costly_put16(g + 32936, 1), costly_put16(g + 32938, 0), costly_put16(g + 32940, 1), costly_put16(g + 32942, 24);
        costly_put16(g + 32944, 2), costly_put16(g + 32946, 6), costly_put16(g + 32948, 0);
        costly_put16(g + 32950, 2), costly_put16(g + 32952, 1), costly_put16(g + 32954, 0), costly_put16(g + 32956, 0xffff),
            costly_put16(g + 32958, 0);
        costly_put16(g + 32960, 1), costly_put16(g + 32962, 6), costly_put16(g + 32964, 1);
        costly_put16(g + 32966, 1), costly_put16(g + 32968, 1), costly_put16(g + 32970, one);
        tables = (positive)shape_costly[4] << 8 | shape_costly[5];
        for (positive i = 0; i < tables; i++)
        {
                p8 address_to record = shape_costly + 12 + 16 * i;

                if (!memory_compare(record, "GSUB", 4))
                {
                        costly_put16(record + 8, (p32)(base >> 16)), costly_put16(record + 10, (p32)base & 0xffff);
                        costly_put16(record + 12, 0), costly_put16(record + 14, 32972);
                }
        }
        check("costly lookups: the face with the costly GSUB loads", font_load(&face, shape_costly, base + 32972, 0) == 0);
        for (positive at = 0; at < sizeof(text); at += 2)
                text[at] = 0xd9, text[at + 1] = 0xa1;
        {
                const font_face address_to chain[1] = {&face};

                started = clock_monotonic_nanoseconds();
                placed = font_layout(&engine, chain, 1, text, sizeof(text), 20 * 64, FONT_RTL, shape_places, FONT_PARA + 16);
                took = clock_monotonic_nanoseconds() - started;
                for (positive i = 0; i < placed; i++)
                        turned += shape_places[i].glyph == one + 1, kept += shape_places[i].glyph == one;
                //      A short line, under the budget's own room, is shaped whole: the last lookup does turn it.
                placed = font_layout(&engine, chain, 1, text, 8, 20 * 64, FONT_RTL, shape_places, FONT_PARA + 16);
        }
        string_format(log, "  costly lookups: 4096 code points laid in %p us, %p turned by the last lookup\n", took / 1000, (positive)turned);
        check("costly lookups: a line of 4096 runs out of its budget in the costly lookups, and the last never runs",
              kept == 4096 && !turned);
        check("costly lookups: four code points are shaped whole, the last lookup turning them",
              placed == 4 && shape_places[0].glyph == one + 1 && shape_places[3].glyph == one + 1);
}

//      Texts that are hostile as texts: directional controls nested past the limit, a hundred thousand combining marks,
//      a paragraph of ten thousand letters, five thousand paragraphs, bytes that are no UTF-8.
static fn check_hostile_text(void)
{
        static p8 text[200100];
        const font_face address_to chain[1] = {&shape_arabic[0]};
        const font_face address_to inter[1] = {&shape_inter};
        font_target target = {shape_pixels, 1280, 1280, 96};
        font_work address_to w = &engine.work;
        positive length = 0, deepest = 0;

        //      Seventy right to left embeddings and a letter: level 125 is as deep as an embedding goes, and the letter is a level above.
        for (positive k = 0; k < 70; k++)
                w->code[k] = 0x202b;
        w->code[70] = 'a';
        for (positive k = 0; k < 70; k++)
                w->code[71 + k] = 0x202c;
        font_bidi(w, 141, 0);
        check("hostile: seventy nested embeddings stop at level 125, a letter in the last at 126", w->level[70] == 126);

        for (positive k = 0; k < 130; k++)
                w->code[k] = 0x2067;
        w->code[130] = 'a';
        font_bidi(w, 131, 0);
        for (positive k = 0; k < 131; k++)
                deepest = w->level[k] != FONT_GONE && w->level[k] > deepest ? w->level[k] : deepest;
        check("hostile: a hundred and thirty nested isolates stop at level 125 or 126", deepest >= 125 && deepest <= 126);

        //      A letter and a hundred thousand marks.
        length += memory_utf8_encode(text, sizeof(text), 0x628);
        for (positive k = 0; k < 100000; k++)
                length += memory_utf8_encode(text + length, sizeof(text) - length, 0x64e);
        b64 extent = font_extent(&engine, chain, 1, text, length, 20 * 64, FONT_RTL);

        font_layout(&engine, chain, 1, text, length, 20 * 64, FONT_RTL, shape_places, 64);
        font_draw(&engine, chain, 1, text, length, 20 * 64, FONT_RTL, &target, 8 << 16, 60, 0x101010, 0xf0f0f0);
        static p32 refused_cuts[8];
        static b32 refused_left[8], refused_right[8];
        static p8 refused_level[8];

        check("hostile: a letter and a hundred thousand marks are laid, measured, drawn and refused as segments",
              extent > 0 && font_segments(&engine, chain, 1, text, length, 20 * 64, FONT_RTL, refused_cuts, 4, refused_left,
                                          refused_right, refused_level) == 0);

        //      Ten thousand letters in a paragraph, and the same cut into five thousand.
        length = 0;
        for (positive k = 0; k < 10000; k++)
                length += memory_utf8_encode(text + length, sizeof(text) - length, 0x628 + k % 20);
        b64 whole = font_extent(&engine, chain, 1, text, length, 20 * 64, FONT_RTL);

        check("hostile: a paragraph past the limit is laid as two", whole > 0 && font_layout(&engine, chain, 1, text, length, 20 * 64, FONT_RTL,
                                                                                            shape_places, 16) == 16);
        length = 0;
        for (positive k = 0; k < 5000; k++)
        {
                length += memory_utf8_encode(text + length, sizeof(text) - length, 0x627);
                text[length++] = '\n';
        }
        check("hostile: five thousand paragraphs", font_extent(&engine, chain, 1, text, length, 20 * 64, FONT_RTL) > 0);

        //      Bytes that are no text, with Arabic in them.
        for (positive k = 0; k < 6000; k++)
                text[k] = (p8)(0x80 + shape_next(128));
        for (positive k = 0; k < 6000; k += 7)
                text[k] = 0xd8;
        check("hostile: bytes that are no UTF-8", font_extent(&engine, inter, 1, text, 6000, 20 * 64, FONT_RTL) >= 0 &&
                                                     font_layout(&engine, chain, 1, text, 6000, 20 * 64, FONT_AUTO, shape_places, 64) <= 64);
}

b32 main()
{
        font_start(&engine, atlas, 512, 512);
        check("load: Inter, Noto Sans Arabic and Noto Sans Hebrew in Regular and Bold", shape_load());
        font_fallback(&engine, &shape_arabic[0], &shape_arabic[1]);
        font_fallback(&engine, &shape_hebrew[0], &shape_hebrew[1]);

        check_bidi();
        check_simple();
        check_shaping();
        check_mixed();
        check_fallback();
        check_segments();
        check_render();
        check_costly_lookups();

        shape_guard_pages = (sizeof(shape_arabic_bytes[0]) + 4096) / 4096 + 2;
        shape_guard = memory((shape_guard_pages + 1) * 4096);
        check("hostile: a guard page",
              shape_guard && system_call_3(syscall(mprotect), (positive)(shape_guard + shape_guard_pages * 4096), 4096, 0) == 0);
        {
                bipolar n = file_slurp_regular_at(AT_FDCWD, "fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf", shape_hostile_font,
                                                  sizeof(shape_hostile_font), 0);

                if (n > 0)
                        check_hostile_layout(shape_hostile_font, (positive)n);
        }
        check_hostile_text();
        return test_report(null);
}
#endif
