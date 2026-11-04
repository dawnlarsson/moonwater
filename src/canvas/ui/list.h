/*
        A window's list: its layout and the reader's side. The kit's recorder writes a list (ui.c: ui_scene_begin, ui_scene_end),
        and the compositor reads it (window.c's window_list_valid, compositor.c). The shell builds all of them as one translation
        unit, and this header is the one copy of the layout they share.
*/
#ifndef MOONWATER_CANVAS_UI_LIST
#define MOONWATER_CANVAS_UI_LIST

/*
        The draw list: a frame recorded for the compositor, in shared memory the app maps and the compositor maps too (a memfd). It holds the
        ops ui.scene records, in the same format, so a list is the recording and no re-encoding. Layout, in bytes: a header of sixteen 32-bit
        words, then two halves of one size; a half is one generation's ops (room of them, 32 bytes each) and then the cover bytes its text ops
        index (each text op's word 7 is an offset from the list's base, so the compositor needs one mapping and no atlas). The halves ping-pong:
        the app records into the half that is not published, and ui_scene_end publishes it, so the compositor reads the published half while
        the app writes the other. Header words: 0 magic, 1 version, 2 the generation, a seqlock (odd while a publish writes the header, even when
        it is whole, and it moves by two a publish), 3 the published half (0 or 1), 4 its ops, 5 the room of a half, 6 and 7 the picture's width
        and height in device pixels, 8 its pitch in pixels, 9 to 12 the damage of the generation (x0 y0 x1 y1, empty when x1 <= x0), 13 the offset
        from the base of the cover bytes, 14 the cover bytes used, 15 the flags (bit 0: lost, a record did not fit, so the frame is the
        processor's). The reader maps the whole list and takes the halves' size from it, as ui_draw_list_init did. ui_draw_list_open answers 1
        when the published half may be read, 0 when a publish is writing the header (try again later), and -1 when this is no list it can use
        (paint the frame on the processor). After it has read the ops, ui_draw_list_still says whether the generation held: if it moved, what
        was read is refused. Production does not record a list: ui.scene is set by a check only until the compositor consumes lists.
*/
#define UI_DRAW_LIST_MAGIC 0x4d574c53u // "MWLS"
#define UI_DRAW_LIST_VERSION 1u
#define UI_DRAW_LIST_HEADER 64ul
#define UI_DRAW_LIST_OP 32ul

enum
{
        UI_DRAW_LIST_MAGIC_AT, UI_DRAW_LIST_VERSION_AT, UI_DRAW_LIST_GEN, UI_DRAW_LIST_HALF, UI_DRAW_LIST_COUNT, UI_DRAW_LIST_ROOM,
        UI_DRAW_LIST_WIDTH, UI_DRAW_LIST_HEIGHT, UI_DRAW_LIST_PITCH, UI_DRAW_LIST_DX0, UI_DRAW_LIST_DY0, UI_DRAW_LIST_DX1, UI_DRAW_LIST_DY1,
        UI_DRAW_LIST_COVER, UI_DRAW_LIST_COVER_USED, UI_DRAW_LIST_FLAGS
};

struct ui_draw_view
{
        const unsigned int address_to ops; // the published half's ops
        const unsigned char address_to cover;
        unsigned int count, room, width, height, gen, half;
        unsigned long pitch, cover_at, cover_used;
        int x0, y0, x1, y1;
};

// The reader's open: 1 when the published half may be read (its view is filled), 0 when a publish is writing the header, -1 when this is no
// list it can use. Reads nothing but the header until it answers 1.
static inline int ui_draw_list_open(unsigned char address_to base, unsigned long bytes, struct ui_draw_view address_to view)
{
        unsigned int address_to head = (unsigned int address_to)base;
        unsigned int gen, half_index, room, count;
        unsigned long half, ops, at;

        if (!base || bytes < UI_DRAW_LIST_HEADER)
                return -1;
        gen = __atomic_load_n(&head[UI_DRAW_LIST_GEN], __ATOMIC_ACQUIRE);
        if (gen & 1)
                return 0;
        if (head[UI_DRAW_LIST_MAGIC_AT] != UI_DRAW_LIST_MAGIC || head[UI_DRAW_LIST_VERSION_AT] != UI_DRAW_LIST_VERSION || head[UI_DRAW_LIST_FLAGS] & 1)
                return -1;
        half_index = head[UI_DRAW_LIST_HALF], room = head[UI_DRAW_LIST_ROOM], count = head[UI_DRAW_LIST_COUNT];
        half = (bytes - UI_DRAW_LIST_HEADER) / 2 & ~15ul;
        ops = (unsigned long)room * UI_DRAW_LIST_OP;
        if (half_index > 1 || !room || half <= ops + 16 || count > room)
                return -1;
        at = UI_DRAW_LIST_HEADER + (unsigned long)half_index * half;
        if ((unsigned long)head[UI_DRAW_LIST_COVER] != at + ops || (unsigned long)head[UI_DRAW_LIST_COVER_USED] > half - ops)
                return -1;
        view->ops = (const unsigned int address_to)(base + at);
        view->cover = base + at + ops;
        view->count = count, view->room = room, view->gen = gen, view->half = half_index;
        view->width = head[UI_DRAW_LIST_WIDTH], view->height = head[UI_DRAW_LIST_HEIGHT], view->pitch = head[UI_DRAW_LIST_PITCH];
        view->x0 = (int)head[UI_DRAW_LIST_DX0], view->y0 = (int)head[UI_DRAW_LIST_DY0], view->x1 = (int)head[UI_DRAW_LIST_DX1], view->y1 = (int)head[UI_DRAW_LIST_DY1];
        view->cover_at = head[UI_DRAW_LIST_COVER], view->cover_used = head[UI_DRAW_LIST_COVER_USED];
        return 1;
}

// After the reader has read the ops it opened: 1 when the generation is the one it opened (what it read holds), 0 when it moved (refuse it).
static inline b32 ui_draw_list_still(unsigned char address_to base, const struct ui_draw_view address_to view)
{
        unsigned int address_to head = (unsigned int address_to)base;

        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        return __atomic_load_n(&head[UI_DRAW_LIST_GEN], __ATOMIC_RELAXED) == view->gen;
}

#endif /* MOONWATER_CANVAS_UI_LIST */
