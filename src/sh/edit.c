/*
        The editor.

        What a person who has only ever used VS Code sits down at. Every key
        does what that editor does it with, there is no mode to leave before
        typing a letter, and there is no chord alphabet to learn: the whole of
        what has to be known to use this is Ctrl+S, Ctrl+Z and the arrow keys,
        and everything past that is discovered rather than studied.

        Nothing in the core below makes a system call. term.c says the same
        thing about itself and gives the reason -- an emulator that is a byte
        stream in and a grid of cells out runs the same whether a pty is
        driving it or a test is -- and an editor has exactly that shape from
        the other side: keystrokes in, a screen out. So the buffer, the
        cursors, the undo journal, the input decoder and the renderer take
        their bytes from edit_input_byte and leave their bytes in edit_emitted,
        and the only things that touch the kernel are the four routines at the
        bottom under the driver heading. the edit lane of test/run drives everything
        above that line with no terminal, no pty and no file underneath, by
        feeding what the editor emits straight into term.c's own consume() and
        asserting on the cells that come out.

        THE TEXT is an array of lines, each line a grown array of bytes with no
        newline in it. That is the honest structure here and not merely the
        easy one, and the argument is the list of operations that actually
        happen rather than the list a data structures chapter would give:

            Typing at a point is one memory_copy of the tail of one line.
            The line is the unit of work, so the cost is the line's length and
            not the file's, and a file of any size types at the same speed.

            Deleting a selection that spans lines is one memory_copy over the
            line table -- sixteen bytes a line, vectorised -- plus one join of
            the two ends. A piece table would win on the copy and lose the
            table.

            Moving a line up or down, which Alt+Up and Alt+Down have to do and
            which people use constantly, is two stores in the line table. In a
            gap buffer it is two copies of the line's bytes with a gap move
            between them; in a piece table it is three splits and a splice.
            This is the operation where an array of lines is not a compromise
            but the right answer, and it is one of the required keys.

            What edit_move_lines actually pays is a copy of the two lines,
            because it goes through the journal like everything else and a
            journal records bytes rather than table entries. That is the trade
            and it is worth naming: the structure makes the move two stores,
            and undo makes it two lines of copying, and a swap done behind the
            journal's back would be a move that Ctrl+Z put back in the wrong
            order. Two lines is what an undoable move costs here; in a piece
            table it would still be three splits and a splice on top of it.

            Holding several cursors at once wants positions that survive an
            edit somewhere else in the file. A line and a byte offset is a
            position that can be shifted by a rule anybody can read
            (edit_place_shift, below); an offset into a gap buffer is a
            position that has to be re-derived every time the gap moves, and a
            piece-table position is a piece plus an offset into it, which no
            longer exists after the piece is split.

            Rendering a window into the middle of the file is a walk of ROWS
            entries of the line table. In a gap buffer it is a scan for
            newlines, and the scan is why editors built on one keep a line
            index beside it -- which is this structure, kept twice.

        What it costs: a single line of a hundred megabytes. Typing at the
        front of that line copies a hundred megabytes per keystroke, and
        memory_copy being hand written AVX-512 only moves the number at which
        it becomes visible rather than removing it. Minified JavaScript and
        machine generated SQL are the real files with that shape. The fix, if
        it is ever wanted, is a gap inside the one line the cursor is on, which
        is a change to edit_line_splice and to nothing else in this file --
        every other routine here asks a line for its bytes and its length and
        would not notice. It is not done today because it costs a branch in the
        hottest routine to serve a file nobody in this distribution has.

        THE CURSORS are a list from the first line of the file, not a single
        position with a plan to grow one. Every editing operation walks the
        list from the last cursor to the first, so an edit never invalidates a
        position that has not been reached yet, and every position that an edit
        does move is moved by edit_place_shift. With one cursor in the list
        this is an ordinary editor and costs one iteration; with twenty it is
        the same code. Adding the list afterwards would have meant rewriting
        every operation in this file, which is why it is here before anything
        needs it.

        Dawn Larsson - Apache-2.0 license
*/

#ifndef SHELL_EDIT_INCLUDED
#define SHELL_EDIT_INCLUDED

#if !defined(KERNEL_MODE) && !defined(STANDARD_NO_PLATFORM)

/*
        The screen this can be asked to draw into.

        A terminal wider or taller than these is drawn to the limit and no
        further, which is a viewport that is smaller than the window rather
        than a crash. The numbers are here so that the per-row bookkeeping is
        an array with no allocation behind it: the renderer touches it on
        every keystroke and an allocation there would be a per-keystroke
        pointer chase for no gain.
*/
#define EDIT_COLUMNS_MAX 1024
#define EDIT_ROWS_MAX 256

// A tab is this many columns wide on the screen, and Tab inserts this many
// spaces. VS Code's default for a file with no other evidence.
#define EDIT_TAB 8

// How far a line's own store is grown by the first time it needs one, and how
// far the line table and the cursor list are. Small enough that a file of many
// short lines does not pay a page a line, large enough that typing a word does
// not reallocate on every character.
#define EDIT_LINE_FIRST 32
#define EDIT_TABLE_FIRST 64

/*
        A name this file offers to the ones layered on top of it and does not
        reach itself.

        Not the attribute that keeps a symbol: this one lets the compiler drop
        what it finds no call to, and only quiets the warning about having
        found none. term.c spells the same thing SPARE; this has its own so
        that the two files do not depend on the order they are included in.
*/
#define EDIT_SPARE __attribute__((unused))

/*
        One line of the file.

        The newline is not in the bytes. A file's lines and the separators
        between them are different things, and keeping the separator in the
        line means every routine that asks how long a line is has to say
        whether it means with or without -- which is a question that gets
        answered wrongly once per editor. What the file had at its end is
        remembered separately, in edit_final_newline, so that a file which
        ended without one is written back without one.
*/
struct edit_line
{
        p8 address_to text;
        positive length;
        positive room;
};

/*
        A position in the file: which line, and how many bytes into it.

        Bytes, not screen columns. A tab is one byte and eight columns, and a
        character outside ASCII is one character and several bytes, so a
        position that meant columns could not name the place between two bytes
        of a UTF-8 sequence -- and every routine that edits has to name exactly
        that place. Where a column is wanted for drawing or for vertical
        motion, edit_display_column works it out from the bytes.
*/
struct edit_place
{
        positive line;
        positive column;
};

/*
        One cursor, and the selection it may be dragging behind it.

        The anchor is the end that does not move: Shift+Right moves the caret
        and leaves the anchor, and the selection is everything between them in
        whichever order they happen to be in. selecting says whether the anchor
        means anything at all, which is not the same as the two being equal --
        an empty selection at a point is a thing the editor can be in the
        middle of making.

        wanted is the screen column that vertical motion aims at. Going down
        from the end of a long line onto a short one and down again should
        arrive back out at the long column, which is what every editor does and
        what a cursor that forgot would not.
*/
struct edit_cursor
{
        positive line;
        positive column;
        positive anchor_line;
        positive anchor_column;
        positive wanted;
        bool selecting;
};

// The file, as lines. There is always at least one, because a file with no
// lines has nowhere to put a cursor; an empty file is one empty line.
static struct edit_line address_to edit_lines;
static positive edit_line_count;
static positive edit_line_room;

static struct edit_cursor address_to edit_cursors;
static positive edit_cursor_count;
static positive edit_cursor_room;

//      Whether the file ended with a newline when it was read, which is what
//      it will end with when it is written.
static bool edit_final_newline = true;
// An empty file and a file containing one newline are both one empty line in
// memory. Keep the one bit that distinguishes their byte representation.
static bool edit_empty_file = true;

//      What the file is called, whether the bytes on disc still match what is
//      on the screen, and whether the editor is still running. edit_modified
//      is deliberately not called edit_dirty: the renderer has rows that are
//      dirty too and one word for both is one bug waiting.
static string_address edit_path;
static p8 EDIT_SPARE edit_path_resolved[4096];
static bool edit_modified;
static bool edit_running;
/*
        Whether the window is being driven by hand rather than by the caret.

        Ctrl+Up and Ctrl+Down scroll and leave the caret where it is, which
        means the caret is allowed to be off the screen -- and a viewport that
        always follows would drag it straight back. So an explicit scroll frees
        the window until the next thing that moves a caret, which is every
        movement, every edit and every click.
*/
static bool edit_view_free;

/*
        The bytes the editor wants written, and nothing that writes them.

        Every escape sequence and every character the renderer produces lands
        here. The driver at the bottom of this file drains it into descriptor
        one; the test harness drains it into term.c's consume() and looks at
        the cells. Neither of those is the editor's business, and keeping the
        write out of the core is what lets the harness see the screen the same
        way a person does rather than by reading the editor's variables back.
*/
static p8 address_to edit_emitted;
static positive edit_emitted_length;
static positive edit_emitted_room;

static fn edit_say(address_any text, positive length)
{
        if (!length)
                return;

        if (!memory_resize_reserve(address_of edit_emitted, address_of edit_emitted_room,
                       edit_emitted_length + length, 4096))
                return;

        memory_copy_apart(edit_emitted + edit_emitted_length, text, length);
        edit_emitted_length += length;
}

#define edit_say_literal(literal) \
        edit_say((string_address)(literal), sizeof(literal) - 1)

//      Where the next character written goes, in the terminal's own one based
//      counting. Everything the renderer draws is placed rather than assumed,
//      because a renderer that only ever moves relatively has no way back from
//      a line the terminal wrapped.
#define edit_say_at(screen_row, screen_column) \
        shell_set_cursor(edit_say, (screen_column) + 1, (screen_row) + 1)

#define edit_table_room_for(table, room, want)                               \
        memory_resize_reserve((p8 address_to address_to)address_of (table),              \
                  address_of (room), (want) * sizeof((table)[0]),             \
                  EDIT_TABLE_FIRST * sizeof((table)[0]))
#define edit_lines_room_for(want)                                            \
        edit_table_room_for(edit_lines, edit_line_room, (want))
#define edit_cursors_room_for(want)                                          \
        edit_table_room_for(edit_cursors, edit_cursor_room, (want))

/*
        Bytes taken out of one line and bytes put in, in one call.

        This is the only routine in the file that writes a line's bytes, and
        the only place a gap buffer would ever have to be introduced. It moves
        the tail once whether the replacement is longer or shorter than what it
        replaces, which is one memory_copy rather than the two a remove
        followed by an insert would cost.
*/
static inline INLINE fn edit_line_splice_ready(struct edit_line address_to line,
                                 positive from, positive to,
                                 string_address text, positive length)
{
        positive tail = line->length - to;

        // The tail moves before the new bytes land on top of where it was, and
        // memory_copy is memmove, so the two halves are allowed to overlap.
        if (tail)
                memory_copy(line->text + from + length, line->text + to, tail);

        if (length)
                memory_copy_apart(line->text + from, text, length);

        line->length = from + length + tail;
}

static bool edit_line_splice(positive which, positive from, positive to,
                             string_address text, positive length)
{
        struct edit_line address_to line = edit_lines + which;
        to = min(to, line->length);
        from = min(from, to);
        if (!memory_resize_reserve(address_of line->text,
                                   address_of line->room,
                                   from + length + line->length - to, EDIT_LINE_FIRST))
                return false;
        edit_line_splice_ready(line, from, to, text, length);
        return true;
}

//      Room for one more line at a given index, with everything below it
//      pushed down. One copy of the table, which is sixteen bytes a line.
static bool edit_line_open(positive at)
{
        if (!edit_lines_room_for(edit_line_count + 1))
                return false;

        if (at < edit_line_count)
                memory_copy(edit_lines + at + 1, edit_lines + at,
                            (edit_line_count - at) * sizeof(struct edit_line));

        edit_lines[at].text = null;
        edit_lines[at].length = 0;
        edit_lines[at].room = 0;
        edit_line_count++;
        return true;
}

//      One line taken out of the table, its bytes released with it. The store
//      is the line's own, so a line that goes has to give it back or a file
//      that is edited for an hour holds every line it ever had.
static fn edit_line_close(positive at)
{
        if (at >= edit_line_count)
                return;

        memory_give(edit_lines[at].text);

        array_remove(edit_lines, edit_line_count, at);
}

//      Somewhere inside the file, whatever was asked for. Everything that
//      takes a position from the outside -- a mouse click, a go to line, a
//      cursor restored from an undo step -- comes through here, so no other
//      routine has to ask whether it was handed a line that exists.
static PURE struct edit_place edit_place_clamped(positive line, positive column)
{
        struct edit_place place;

        if (!edit_line_count)
                line = 0;
        else if (line >= edit_line_count)
                line = edit_line_count - 1;

        place.line = line;
        place.column = column;

        if (edit_line_count && place.column > edit_lines[line].length)
                place.column = edit_lines[line].length;

        return place;
}

static CONST bool edit_place_before(struct edit_place a, struct edit_place b)
{
        return a.line < b.line || (a.line == b.line && a.column < b.column);
}

static CONST bool edit_place_same(struct edit_place a, struct edit_place b)
{
        return a.line == b.line && a.column == b.column;
}

/*
        A position, after somebody else's edit.

        An edit replaced everything between from and to with something that
        ends at after. A position that was before from is where it was; a
        position that was inside the span no longer exists and collapses to
        the start of it; a position after the span moves by the difference, and
        only its column is touched if it happened to share the line the span
        ended on.

        This is the whole of what multiple cursors need in order to survive
        each other, and it is written once here rather than at each of the
        dozen call sites that would otherwise each get it slightly wrong.
*/
static struct edit_place edit_place_shift(struct edit_place place,
                                          struct edit_place from,
                                          struct edit_place to,
                                          struct edit_place after)
{
        if (!edit_place_before(from, place))
                return place;

        if (edit_place_before(place, to))
                return from;

        if (place.line == to.line)
        {
                place.column = after.column + (place.column - to.column);
                place.line = after.line;
                return place;
        }

        place.line = place.line + after.line - to.line;
        return place;
}

//      Every cursor and every anchor, moved by one edit. The cursor that made
//      the edit is put where it belongs by its caller afterwards; this is for
//      all the others.
static fn edit_cursors_shift(struct edit_place from, struct edit_place to,
                             struct edit_place after)
{
        for (positive at = 0; at < edit_cursor_count; at++)
        {
                struct edit_cursor address_to cursor = edit_cursors + at;
                struct edit_place caret = {cursor->line, cursor->column};
                struct edit_place anchor = {cursor->anchor_line,
                                            cursor->anchor_column};

                caret = edit_place_shift(caret, from, to, after);
                anchor = edit_place_shift(anchor, from, to, after);

                cursor->line = caret.line;
                cursor->column = caret.column;
                cursor->anchor_line = anchor.line;
                cursor->anchor_column = anchor.column;
        }
}

//      The caret and the anchor of one cursor, as a pair of places, and the
//      selection they make in the order the file has them in.
#define edit_cursor_place_of(at, row, column)                                \
        ({ positive _cursor = (at);                                         \
           (struct edit_place){edit_cursors[_cursor].row,                    \
                               edit_cursors[_cursor].column}; })
#define edit_cursor_caret(at) edit_cursor_place_of(at, line, column)
#define edit_cursor_anchor(at)                                              \
        edit_cursor_place_of(at, anchor_line, anchor_column)

static PURE bool edit_cursor_has_selection(positive at)
{
        return edit_cursors[at].selecting &&
               !edit_place_same(edit_cursor_caret(at), edit_cursor_anchor(at));
}

static PURE inline INLINE struct edit_place edit_selection_edge(positive at,
                                                                bool last)
{
        struct edit_place caret = edit_cursor_caret(at);
        struct edit_place anchor = edit_cursor_anchor(at);

        return edit_place_before(anchor, caret) == last ? caret : anchor;
}

#define edit_selection_start(at) edit_selection_edge((at), false)
#define edit_selection_end(at) edit_selection_edge((at), true)

//      Where a cursor is put, with the anchor either following it or staying
//      where it was. Every movement in this file ends in one of these two.
static fn edit_cursor_place(positive at, struct edit_place place, bool extend)
{
        struct edit_cursor address_to cursor = edit_cursors + at;

        if (extend && !cursor->selecting)
        {
                cursor->anchor_line = cursor->line;
                cursor->anchor_column = cursor->column;
                cursor->selecting = true;
        }

        cursor->line = place.line;
        cursor->column = place.column;

        if (!extend)
        {
                cursor->selecting = false;
                cursor->anchor_line = place.line;
                cursor->anchor_column = place.column;
        }
}

/*
        The cursors, in the order the file has them, and never two in the same
        place.

        Order is what makes an edit walk safe: taking them from the last to the
        first means an edit never moves a cursor that has not had its turn.
        Merging the coincident ones is what stops Ctrl+D on a word that appears
        twice on one line, followed by a deletion that joins them, from leaving
        two cursors that then type two characters into one place.
*/
static fn edit_cursors_sort()
{
        // An insertion sort, on a list that is nearly always already ordered
        // and nearly always of length one. Anything cleverer would be slower
        // at the size this is ever used at, and qsort would cost a call per
        // comparison for the same answer.
        for (positive at = 1; at < edit_cursor_count; at++)
        {
                struct edit_cursor held = edit_cursors[at];
                positive back = at;

                while (back &&
                       (edit_cursors[back - 1].line > held.line ||
                        (edit_cursors[back - 1].line == held.line &&
                         edit_cursors[back - 1].column > held.column)))
                {
                        edit_cursors[back] = edit_cursors[back - 1];
                        back--;
                }

                edit_cursors[back] = held;
        }

        positive kept = edit_cursor_count ? 1 : 0;

        for (positive at = 1; at < edit_cursor_count; at++)
        {
                if (edit_cursors[at].line == edit_cursors[kept - 1].line &&
                    edit_cursors[at].column == edit_cursors[kept - 1].column)
                {
                        // Keep the first selection at this position. Compact
                        // once instead of moving the tail for every duplicate.
                        if (edit_cursors[at].selecting &&
                            !edit_cursors[kept - 1].selecting)
                                edit_cursors[kept - 1] = edit_cursors[at];
                }
                else
                {
                        if (kept != at)
                                edit_cursors[kept] = edit_cursors[at];
                        kept++;
                }
        }

        edit_cursor_count = kept;
}

//      Back to one cursor, wherever the first one is. Escape does this, and so
//      does anything that has to speak about a single position.
static fn edit_cursors_one()
{
        if (edit_cursor_count > 1)
                edit_cursor_count = 1;

        if (edit_cursor_count)
                edit_cursors[0].selecting = false;
}

/*
        A span of the file, as bytes, with a newline where a line ends.

        Everything that leaves the buffer -- what a copy puts on the clipboard,
        what an undo step remembers it removed -- leaves through these two, and
        everything that comes back in comes back through edit_raw_insert. So
        there is one spelling of "the text between here and there" and one of
        "put this text here", and neither has a special case for whether the
        span happens to be inside one line.
*/
static PURE positive edit_span_length(struct edit_place from,
                                      struct edit_place to)
{
        positive total;

        if (from.line == to.line)
                return to.column > from.column ? to.column - from.column : 0;

        total = edit_lines[from.line].length - from.column + 1;

        for (positive at = from.line + 1; at < to.line; at++)
                total += edit_lines[at].length + 1;

        return total + to.column;
}

static fn edit_span_copy(struct edit_place from, struct edit_place to,
                         p8 address_to into)
{
        positive at = 0;

        if (from.line == to.line)
        {
                if (to.column > from.column)
                        memory_copy_apart(into, edit_lines[from.line].text +
                                                    from.column,
                                          to.column - from.column);

                return;
        }

        at = edit_lines[from.line].length - from.column;
        memory_copy_apart(into, edit_lines[from.line].text + from.column, at);
        into[at++] = '\n';

        for (positive line = from.line + 1; line < to.line; line++)
        {
                memory_copy_apart(into + at, edit_lines[line].text,
                                  edit_lines[line].length);
                at += edit_lines[line].length;
                into[at++] = '\n';
        }

        memory_copy_apart(into + at, edit_lines[to.line].text, to.column);
}

//      The span, on the allocator, for a caller that will give it back. Zero
//      bytes still answers with a block, so that a caller never has to ask
//      whether it got one.
static p8 address_to edit_span_take(struct edit_place from,
                                    struct edit_place to, positive address_to
                                                              length)
{
        positive size = edit_span_length(from, to);
        p8 address_to block = (p8 address_to)memory_take(size + 1);

        if (!block)
        {
                address_to length = 0;
                return null;
        }

        edit_span_copy(from, to, block);
        block[size] = 0;
        address_to length = size;
        return block;
}

/*
        Bytes taken out of the file, with no journal and no cursors touched.

        A multi-line removal is the tail of the last line spliced onto the head
        of the first and every line between them closed, which is one splice
        and a run of table copies rather than a byte at a time anywhere.
*/
/*
        One atomic replacement beneath the journal.

        Every resulting line is allocated and filled before one byte of the
        document is changed.  That is more than an out-of-memory nicety: the
        old multi-line join freed all later lines even when growing the first
        line had failed, and insertion could leave half a paste in the file.
        Here failure releases only the staged lines and the original document
        is still byte-for-byte intact.
*/
struct edit_line_restore
{
        struct edit_line address_to lines;
        positive removed, made;
};

static fn edit_lines_give(struct edit_line address_to lines, positive count)
{
        for (positive at = 0; at < count; at++)
                memory_give(lines[at].text);
}

// The prepared table splice cannot allocate; the same splice restores an
// undo prefix after OOM, with the retained original line buffers as input.
static fn edit_lines_exchange(positive first, positive removed,
                              struct edit_line address_to lines,
                              positive made, bool release)
{
        positive tail = edit_line_count - first - removed;
        if (release)
                edit_lines_give(edit_lines + first, removed);
        if (tail)
                memory_copy(edit_lines + first + made, edit_lines + first + removed,
                            tail * sizeof(struct edit_line));
        memory_copy_apart(edit_lines + first, lines, made * sizeof(struct edit_line));
        edit_line_count = edit_line_count - removed + made;
}

static bool edit_raw_replace_mode(struct edit_place from, struct edit_place to,
                             string_address text, positive length,
                             struct edit_place address_to after,
                             struct edit_line_restore address_to restore)
{
        positive breaks;
        positive made;
        positive old_count = edit_line_count;
        positive new_count;
        positive input_at = 0;
        positive suffix_length;
        string_address suffix;
        struct edit_line address_to staged;
        struct edit_line address_to built_lines;

        if (edit_place_before(to, from))
                to = from;

        breaks = length ? memory_count(text, length, '\n') : 0;

        /* The overwhelmingly hot case: one line changed in place. Splice
           reserves before touching bytes, so allocation failure is already
           transactional, and capacity turns ordinary typing into one copy
           with no allocator traffic. */
        if (!breaks && from.line == to.line)
        {
                if (!edit_line_splice(from.line, from.column, to.column,
                                      text, length))
                        return false;

                after->line = from.line;
                after->column = from.column + length;
                return true;
        }

        made = breaks + 1;
        positive removed = to.line - from.line + 1;
        positive retained = restore ? removed : 0;
        if (retained > positive_max / sizeof(struct edit_line) ||
            made > positive_max / sizeof(struct edit_line) - retained ||
            made > positive_max - (old_count - removed))
                return false;
        new_count = old_count - removed + made;
        if (new_count > positive_max / sizeof(struct edit_line))
                return false;
        suffix = edit_lines[to.line].text + to.column;
        suffix_length = edit_lines[to.line].length - to.column;

        if (!edit_lines_room_for(new_count))
                return false;

        built_lines = (struct edit_line address_to)memory_take(
            (made + retained) * sizeof(struct edit_line));

        if (!built_lines)
                return false;

        staged = built_lines + retained;
        memory_zero(staged, made * sizeof(struct edit_line));

        for (positive line = 0; line < made; line++)
        {
                positive width = line < breaks
                    ? memory_span_without_byte((address_any)(text + input_at),
                                               '\n', length - input_at)
                    : length - input_at;
                positive prefix = line ? 0 : from.column;
                positive tail = line + 1 == made ? suffix_length : 0;
                if (width > positive_max - prefix ||
                    tail > positive_max - prefix - width)
                {
                        edit_lines_give(staged, line);
                        memory_give(built_lines);
                        return false;
                }
                positive room = prefix + width + tail;
                positive allocation = line + 1 == made && room
                    ? memory_growth(0, room, EDIT_LINE_FIRST)
                    : room;
                positive at = 0;

                if (room)
                {
                        staged[line].text =
                            (p8 address_to)memory_take(allocation);

                        if (!staged[line].text)
                        {
                                edit_lines_give(staged, line);
                                memory_give(built_lines);
                                return false;
                        }

                        staged[line].room = allocation;
                }

                if (prefix)
                {
                        memory_copy_apart(staged[line].text,
                                          edit_lines[from.line].text, prefix);
                        at += prefix;
                }

                if (width)
                {
                        memory_copy_apart(staged[line].text + at,
                                          text + input_at, width);
                        at += width;
                }

                if (tail)
                {
                        memory_copy_apart(staged[line].text + at, suffix, tail);
                        at += tail;
                }

                staged[line].length = at;
                input_at += width + (width < length - input_at);
        }

        after->line = from.line + breaks;
        after->column = breaks ? staged[breaks].length - suffix_length
                               : from.column + length;

        if (restore)
        {
                memory_copy_apart(built_lines, edit_lines + from.line,
                                  removed * sizeof(struct edit_line));
                *restore = (struct edit_line_restore){built_lines, removed, made};
        }
        edit_lines_exchange(from.line, removed, staged, made, !restore);
        if (!restore)
                memory_give(built_lines);
        return true;
}

#define edit_raw_replace(from, to, text, length, after) \
        edit_raw_replace_mode(from, to, text, length, after, null)

/*
        The journal.

        A patch is one span replaced by another, remembered as the bytes that
        were there and the bytes that are there now. A step is every patch one
        keystroke -- or one run of keystrokes -- produced, together with where
        every cursor was before it and where every cursor is after it.

        Undoing a step walks its patches backwards. That is not a detail: each
        patch's position was recorded in the document as it stood at the moment
        the patch was applied, so undoing them in exactly the reverse order
        puts the document back into that state before the position is used, and
        no patch position ever has to be adjusted for another patch. Applying
        them forwards again is the same argument the other way round.

        The cursor list is remembered whole rather than as a delta. An undo
        that put the text back and left the caret at the far end of the file is
        the thing people notice first and complain about second, and the list
        is a few dozen bytes.
*/
#define EDIT_STEP_OTHER 0
#define EDIT_STEP_TYPING 1
#define EDIT_STEP_ERASING 2

struct edit_patch
{
        positive line;
        positive column;
        p8 address_to removed;
        positive removed_length;
        positive removed_room;
        p8 address_to inserted;
        positive inserted_length;
        positive inserted_room;
};

struct edit_step
{
        struct edit_patch address_to patches;
        positive patch_count;
        positive patch_room;
        struct edit_cursor address_to before;
        positive before_count;
        struct edit_cursor address_to after;
        positive after_count;
        p8 kind;
        bool open;
        bool before_empty;
        bool after_empty;
};

static struct edit_step address_to edit_steps;
//      How many steps exist, and how many of them are applied. Everything at
//      or above edit_step_at is a step that has been undone and can be redone;
//      a new edit throws those away, which is what makes redo stop being
//      offered the moment the history forks.
static positive edit_step_count;
static positive edit_step_room;
static positive edit_step_at;

//      The step number the file on disc matches, so that undoing back to where
//      it was saved clears the modified marker instead of leaving it on
//      forever.
static positive edit_step_saved;
static bool edit_step_saved_known = true;

static fn edit_step_forget(struct edit_step address_to step)
{
        for (positive at = 0; at < step->patch_count; at++)
        {
                memory_give(step->patches[at].removed);
                memory_give(step->patches[at].inserted);
        }

        memory_give(step->patches);
        memory_give(step->before);
        memory_give(step->after);
        memory_zero(step, sizeof(struct edit_step));
}

//      Everything above the undo point, thrown away, which is what a new edit
//      after an undo does to the redo history.
static fn edit_steps_truncate()
{
        while (edit_step_count > edit_step_at)
                edit_step_forget(edit_steps + --edit_step_count);

        if (!edit_step_saved_known)
                return;

        // A save that is now above the top of the history can never be
        // returned to, so the marker stops meaning anything.
        if (edit_step_saved > edit_step_count)
                edit_step_saved_known = false;
}

static bool edit_cursors_remember(struct edit_cursor address_to address_to into,
                                  positive address_to count)
{
        positive size = edit_cursor_count * sizeof(struct edit_cursor);

        struct edit_cursor address_to saved = address_to into;
        if (!saved || address_to count != edit_cursor_count)
                saved = (struct edit_cursor address_to)memory_resize(saved, size + 1);
        if (!saved)
                return false;

        address_to into = saved;
        memory_copy_apart(saved, edit_cursors, size);
        address_to count = edit_cursor_count;
        return true;
}

static fn edit_cursors_restore(struct edit_cursor address_to from,
                               positive count)
{
        if (!count || !from)
                return;

        if (!edit_cursors_room_for(count))
                return;

        memory_copy_apart(edit_cursors, from,
                          count * sizeof(struct edit_cursor));
        edit_cursor_count = count;
}

/*
        The step a change is going into, opened if there is not one already.

        Coalescing is here and it is one rule: a run of the same kind of
        keystroke, uninterrupted, is one step. Typing twenty characters is one
        Ctrl+Z, which is what every editor written in the last thirty years
        does and what makes undo usable at all. Anything that is not typing --
        a movement, a paste, a save, a line moved -- seals whatever was open,
        so the caret positions remembered in the step are the ones the run
        actually started from.
*/
static inline INLINE bool edit_step_cursors(bool close);

static bool edit_step_start(p8 kind)
{
        struct edit_step address_to step;

        if (edit_step_count > edit_step_at)
                edit_steps_truncate();

        if (edit_step_count)
        {
                step = edit_steps + edit_step_count - 1;

                if (step->open && step->kind == kind)
                        return true;

                if (!edit_step_cursors(true))
                        return false;
        }

        if (!memory_resize_reserve((p8 address_to address_to)address_of edit_steps,
                       address_of edit_step_room,
                       (edit_step_count + 1) * sizeof(struct edit_step),
                       32 * sizeof(struct edit_step)))
                return false;

        step = edit_steps + edit_step_count;
        memory_zero(step, sizeof(struct edit_step));
        step->kind = kind;
        step->open = true;
        step->before_empty = edit_empty_file;
        step->after_empty = edit_empty_file;

        if (!edit_cursors_remember(address_of step->before,
                                   address_of step->before_count))
                return false;

        edit_step_count++;
        edit_step_at = edit_step_count;
        return true;
}

/*
        The open step, closed, with where the cursors ended up written into it.

        Called by every key that is not itself an edit, and by the driver after
        every keystroke that moved a caret. A step whose after-list was never
        written is a step whose redo puts the carets nowhere.
*/
static inline INLINE bool edit_step_cursors(bool close)
{
        struct edit_step address_to step;

        if (!edit_step_count)
                return true;

        step = edit_steps + edit_step_count - 1;

        if (!step->open)
                return true;

        if (!edit_cursors_remember(address_of step->after, address_of step->after_count))
                return false;
        step->after_empty = edit_empty_file;
        if (close)
                step->open = false;
        return true;
}

//      The after-list kept level with the cursors while a step is still being
//      added to, so that a run of typing that is never followed by another key
//      still redoes to the right place.
#define edit_step_seal() edit_step_cursors(true)
#define edit_step_note_cursors() edit_step_cursors(false)

/*
        One span replaced by one string, journalled, with every other cursor
        moved out of the way. This is the only mutation the keys ever call.

        The patch it records is merged into the one before it when the two are
        the same kind of continuous run -- typing that carries straight on from
        where the last character landed, or erasing that carries straight on
        backwards. That merge is not what makes the undo one step, which the
        step already is; it is what keeps a thousand typed characters from
        being a thousand allocations.
*/
static struct edit_place edit_change_mode(struct edit_place from,
                                     struct edit_place to, string_address text,
                                     positive length, p8 kind, bool shift_cursors)
{
        struct edit_step address_to step;
        struct edit_patch address_to patch;
        struct edit_place after;
        p8 address_to removed;
        positive removed_length = 0;

        if (edit_place_before(to, from))
                to = from;

        if (!edit_step_start(kind))
                return from;

        step = edit_steps + edit_step_count - 1;
        removed = edit_span_take(from, to, address_of removed_length);

        if (!removed)
                return from;

        /*
                A continuation of the run already recorded, if this is one.

                Typing continues when the new text goes in exactly where the
                last character landed and neither takes anything out. Erasing
                continues when the new removal ends exactly where the last one
                started. Only the last patch is considered, so a run with
                several cursors in it simply records a patch each and the merge
                never fires -- correct, and the memory it costs is the memory
                the multiple cursors asked for.
        */
        patch = step->patch_count ? step->patches + step->patch_count - 1 : null;

        if (patch && kind == EDIT_STEP_TYPING && !removed_length &&
            !patch->removed_length && patch->line == from.line &&
            patch->column + patch->inserted_length == from.column &&
            !memory_first_of(patch->inserted, '\n', patch->inserted_length) &&
            !memory_first_of((address_any)text, '\n', length))
        {
                if (memory_resize_reserve(address_of patch->inserted,
                              address_of patch->inserted_room,
                              patch->inserted_length + length, 32))
                {
                        if (!edit_raw_replace(from, to, text, length,
                                              address_of after))
                        {
                                memory_give(removed);
                                return from;
                        }

                        memory_copy_apart(patch->inserted +
                                              patch->inserted_length,
                                          text, length);
                        patch->inserted_length += length;
                        memory_give(removed);
                        goto changed;
                }
        }

        if (patch && kind == EDIT_STEP_ERASING && !length &&
            !patch->inserted_length && patch->line == to.line &&
            patch->column == to.column && from.line == to.line &&
            !memory_first_of(patch->removed, '\n', patch->removed_length))
        {
                if (memory_resize_reserve(address_of patch->removed,
                              address_of patch->removed_room,
                              patch->removed_length + removed_length, 32))
                {
                        if (!edit_raw_replace(from, to, null, 0,
                                              address_of after))
                        {
                                memory_give(removed);
                                return from;
                        }

                        // The run grows at its front, so what was already
                        // remembered moves up and the new bytes go in below it.
                        memory_copy(patch->removed + removed_length,
                                    patch->removed, patch->removed_length);
                        memory_copy_apart(patch->removed, removed,
                                          removed_length);
                        patch->removed_length += removed_length;
                        patch->column = from.column;
                        memory_give(removed);
                        after = from;
                        goto changed;
                }
        }

        if (!memory_resize_reserve((p8 address_to address_to)address_of step->patches,
                       address_of step->patch_room,
                       (step->patch_count + 1) * sizeof(struct edit_patch),
                       4 * sizeof(struct edit_patch)))
        {
                memory_give(removed);
                return from;
        }

        patch = step->patches + step->patch_count;
        memory_zero(patch, sizeof(struct edit_patch));
        patch->line = from.line;
        patch->column = from.column;
        patch->removed = removed;
        patch->removed_length = removed_length;
        patch->removed_room = removed_length + 1;

        if (length)
        {
                patch->inserted = (p8 address_to)memory_take(length + 1);

                if (!patch->inserted)
                {
                        memory_give(removed);
                        return from;
                }

                memory_copy_apart(patch->inserted, text, length);
                patch->inserted_length = length;
                patch->inserted_room = length + 1;
        }

        if (!edit_raw_replace(from, to, text, length, address_of after))
        {
                memory_give(patch->removed);
                memory_give(patch->inserted);
                memory_zero(patch, sizeof(struct edit_patch));
                return from;
        }

        step->patch_count++;
changed:
        if (shift_cursors)
                edit_cursors_shift(from, to, after);
        edit_empty_file = false;
        edit_modified = true;
        return after;
}

#define edit_change(from, to, text, length, kind) \
        edit_change_mode(from, to, text, length, kind, true)

//      Where a span ends, worked out from the text rather than remembered,
//      because the two would then have to be kept level.
static PURE struct edit_place edit_span_end(struct edit_place place,
                                            string_address text,
                                            positive length)
{
        p8 address_to last;
        positive breaks;

        if (!length)
                return place;

        last = (p8 address_to)memory_last_of(text, '\n', length);
        breaks = memory_count(text, length, '\n');

        place.line += breaks;

        if (!breaks)
        {
                place.column += length;
                return place;
        }

        place.column = length - (positive)(last + 1 - text);
        return place;
}

/* Undo and redo are the same cold replacement machine in opposite directions.
   Keeping it out of the typing path removes two copies of the journal walk. */
static PURE bool edit_patch_multiline(struct edit_patch address_to patch)
{
        return memory_first_of(patch->removed, '\n', patch->removed_length) ||
               memory_first_of(patch->inserted, '\n', patch->inserted_length);
}

static COLD bool edit_step_apply(struct edit_step address_to step,
                                 bool backward)
{
        positive multiline = 0, kept = 0, done = 0;
        struct edit_line_restore address_to held = null;
        if (step->patch_count > 1)
                for (positive at = 0; at < step->patch_count; at++)
                        multiline += edit_patch_multiline(step->patches + at);
        if (multiline)
        {
                if (multiline > positive_max / sizeof(*held))
                        return false;
                held = memory_take(multiline * sizeof(*held));
                if (!held)
                        return false;
        }

        for (; done < step->patch_count; done++)
        {
                positive at = backward ? step->patch_count - done - 1 : done;
                struct edit_patch address_to patch = step->patches + at;
                struct edit_place from = {patch->line, patch->column};
                string_address removed = backward ? patch->inserted
                                                  : patch->removed;
                positive removed_length = backward ? patch->inserted_length
                                                    : patch->removed_length;
                struct edit_place after;
                bool retain = held && edit_patch_multiline(patch);

                if (!edit_raw_replace_mode(
                        from, edit_span_end(from, removed, removed_length),
                        backward ? patch->removed : patch->inserted,
                        backward ? patch->removed_length
                                 : patch->inserted_length,
                        address_of after, retain ? held + kept : null))
                        goto rollback;
                kept += retain;
        }

        for (positive at = 0; at < kept; at++)
        {
                edit_lines_give(held[at].lines, held[at].removed);
                memory_give(held[at].lines);
        }
        memory_give(held);
        return true;

rollback:
        while (done)
        {
                done--;
                positive at = backward ? step->patch_count - done - 1 : done;
                struct edit_patch address_to patch = step->patches + at;
                if (edit_patch_multiline(patch))
                {
                        struct edit_line_restore saved = held[--kept];
                        edit_lines_exchange(patch->line, saved.made,
                                            saved.lines, saved.removed, true);
                        memory_give(saved.lines);
                }
                else
                {
                        // Splices never shrink capacity. Later multiline
                        // patches have already restored these exact buffers,
                        // so the inverse fits without an allocation or copy
                        // of the rest of the document.
                        edit_line_splice_ready(edit_lines + patch->line,
                            patch->column, patch->column +
                                (backward ? patch->removed_length : patch->inserted_length),
                            backward ? patch->inserted : patch->removed,
                            backward ? patch->inserted_length : patch->removed_length);
                }
        }
        memory_give(held);
        return false;
}

static COLD ARM64_ERRATUM_ALIGN bool edit_step_move(bool backward)
{
        struct edit_step address_to step;

        if (backward ? !edit_step_at : edit_step_at >= edit_step_count)
                return false;

        step = edit_steps + (backward ? edit_step_at - 1 : edit_step_at);
        if (backward && !edit_step_seal())
                return false;
        positive cursors = backward ? step->before_count : step->after_count;
        if (cursors > positive_max / sizeof(struct edit_cursor) ||
            !edit_cursors_room_for(cursors) || !edit_step_apply(step, backward))
                return false;

        edit_step_at = backward ? edit_step_at - 1 : edit_step_at + 1;
        edit_cursors_restore(backward ? step->before : step->after,
                             backward ? step->before_count : step->after_count);
        edit_empty_file = backward ? step->before_empty : step->after_empty;
        edit_view_free = false;
        edit_modified = !edit_step_saved_known || edit_step_saved != edit_step_at;
        return true;
}

/*
        Bytes, characters and columns, which are three different things.

        A cursor holds a byte offset. What the screen shows at that offset is a
        column, and a tab is one byte and up to eight columns while a character
        outside ASCII is several bytes and one column. Every routine below that
        has "column" in its name means the screen; everything else means bytes.
*/
/*
        The scalar that begins text: how many bytes it took and its value, or
        false for a sequence that is not UTF-8 (at least one byte is used
        either way, and a byte that interrupted a sequence is not one of them).
*/
static bool edit_scalar(const p8 address_to text, positive length,
                        positive address_to used, p32 address_to value)
{
        memory_utf8_state state = {0, 0, 0};
        positive at = 0;

        while (at < length)
        {
                b32 result = memory_utf8_feed(address_of state, text[at++]);

                if (result > 0)
                {
                        address_to used = at;
                        address_to value = state.value;
                        return true;
                }
                if (result < 0)
                {
                        if (at > 1 && (text[at - 1] & 0xc0) != 0x80)
                                at--;
                        address_to used = at;
                        address_to value = 0;
                        return false;
                }
        }

        address_to used = at ? at : 1;
        address_to value = 0;
        return false;
}

/*
        What is sent to the terminal for a character of the file. The file is
        the program's input and not the terminal's: an escape, a C0 control, a
        DEL, and a sequence that is not UTF-8 (a lone 0x9b is a CSI to some
        terminals) are drawn as a question mark, which is one cell, and a C1
        control spelled in UTF-8 is not drawn at all, having no cell. What the
        file holds is not changed, and is saved as it was.
*/
static CONST bool edit_is_continuation(p8 character)
{
        return (character & 0xc0) == 0x80;
}

static CONST bool edit_is_word(p8 character)
{
        return byte_is_alnum(character) || character == '_' ||
               character >= 0x80;
}

static PURE positive edit_step_forward(positive line, positive column);

//      The cells the character at a byte offset takes, as term.c will draw
//      it: unicode_width for the terminal, two for an ideograph and nought for
//      a combining mark, and one for bytes that are no character, which the
//      terminal draws as U+FFFD. A tab is the caller's.
static PURE positive edit_cells_at(positive line, positive column)
{
        struct edit_line address_to text = edit_lines + line;
        positive next = edit_step_forward(line, column);
        memory_utf8_state state = {0, 0, 0};
        b32 result = 0;

        for (positive at = column; at < next && !result; at++)
                result = memory_utf8_feed(address_of state, text->text[at]);

        // A control or a DEL is drawn as one question mark
        // (edit_row_put_character), which is one cell.
        if (result == 1 && (state.value < 0x20 || state.value == 0x7f))
                return 1;

        // A C1 control spelled in UTF-8 is dropped by the terminal and takes
        // no cell there, whatever width the table gives it.
        if (result == 1 && state.value >= 0x80 && state.value < 0xa0)
                return 0;

        return result == 1
                   ? unicode_width(state.value, UNICODE_WIDTH_TERMINAL)
                   : 1;
}

static PURE positive edit_display_column(positive line, positive column)
{
        struct edit_line address_to text = edit_lines + line;
        positive width = 0;

        if (column > text->length)
                column = text->length;

        for (positive at = 0; at < column; at = edit_step_forward(line, at))
        {
                if (text->text[at] == '\t')
                        width += EDIT_TAB - width % EDIT_TAB;
                else
                        width += edit_cells_at(line, at);
        }

        return width;
}

//      The byte offset a wanted screen column lands on, which is the first
//      offset at or past it. Landing inside a tab puts the caret after it,
//      which is what a tab being one thing means, and landing on a mark puts
//      it after the mark, which belongs to the character before it.
static PURE positive edit_column_at_display(positive line, positive wanted)
{
        struct edit_line address_to text = edit_lines + line;
        positive width = 0;

        for (positive at = 0; at < text->length; at = edit_step_forward(line, at))
        {
                positive cells = text->text[at] == '\t'
                                     ? EDIT_TAB - width % EDIT_TAB
                                     : edit_cells_at(line, at);

                if (width >= wanted && cells)
                        return at;

                width += cells;
        }

        return text->length;
}

//      One character forward or back, whole. Stepping a byte at a time through
//      a UTF-8 sequence would put the caret between two bytes of one letter,
//      and then the next insert would break it in half.
static PURE positive edit_step_forward(positive line, positive column)
{
        struct edit_line address_to text = edit_lines + line;

        if (column >= text->length)
                return text->length;

        column++;

        while (column < text->length && edit_is_continuation(text->text[column]))
                column++;

        return column;
}

static PURE positive edit_step_back(positive line, positive column)
{
        struct edit_line address_to text = edit_lines + line;

        if (column > text->length)
                column = text->length;

        if (!column)
                return 0;

        column--;

        while (column && edit_is_continuation(text->text[column]))
                column--;

        return column;
}

//      Where the file ends, which several keys want and none should work out
//      for themselves.
static struct edit_place edit_place_last()
{
        struct edit_place place;

        place.line = edit_line_count - 1;
        place.column = edit_lines[place.line].length;
        return place;
}

/*
        The viewport, and the caret the viewport follows.

        A place rather than an index into the cursor list, because sorting the
        list moves the indices and the thing that has to stay on screen is a
        position in the file. Whoever adds a cursor says which one is now the
        one being driven by writing its place here; with one cursor that is
        every movement, and it costs two stores.
*/
static positive edit_top;
static positive edit_left;

static positive edit_columns = 80;
static positive edit_rows = 24;
static struct edit_place edit_primary_place;

//      Which screen rows hold what, so that a redraw can write only the rows
//      whose contents actually changed. A hash of the bytes the row was last
//      given is enough and needs no second copy of the screen: two rows that
//      hash the same were drawn the same, and the one collision in four
//      billion costs a row that is a keystroke stale.
static positive edit_row_drawn[EDIT_ROWS_MAX];
static bool edit_row_known[EDIT_ROWS_MAX];

//      How many rows the last draw actually wrote, which is the only way from
//      outside to tell a repaint from a keystroke. A test that asserts on the
//      screen cannot see the difference -- both leave the same cells -- so the
//      count is kept rather than inferred.
static positive edit_rows_painted;

//      A message the status line shows until the next keystroke, and the
//      prompt that Ctrl+G and the quit question put there. A prompt is not a
//      mode: every key that is not part of answering it is still the key it
//      always was, and Escape puts it away.
static p8 edit_message[128];
static positive edit_message_length;
static string_address edit_prompt_label;
static p8 edit_prompt_text[256];
static positive edit_prompt_length;
static bool edit_prompt_active;
static p8 edit_prompt_kind;

#define EDIT_PROMPT_NONE 0
#define EDIT_PROMPT_LINE 1
#define EDIT_PROMPT_QUIT 2

static fn edit_repaint_all()
{
        memory_zero(edit_row_known, sizeof(edit_row_known));
}

static fn edit_status_say(string_address text)
{
        positive length = string_length(text);

        if (length > sizeof(edit_message) - 1)
                length = sizeof(edit_message) - 1;

        memory_copy_apart(edit_message, text, length);
        edit_message_length = length;
}

static fn edit_resize(positive columns, positive rows)
{
        edit_columns = columns > EDIT_COLUMNS_MAX ? EDIT_COLUMNS_MAX
                                                  : columns ? columns : 1;
        edit_rows = rows > EDIT_ROWS_MAX ? EDIT_ROWS_MAX : rows ? rows : 1;
        edit_repaint_all();
}

//      How many rows the text gets, which is everything but the status line.
static PURE positive edit_text_rows()
{
        return edit_rows > 1 ? edit_rows - 1 : 1;
}

//      The line numbers down the side, as wide as the largest number plus the
//      space that separates it from the text.
static PURE positive edit_gutter()
{
        return positive_digits(edit_line_count) + 1;
}

/*
        The window moved so that the caret is inside it.

        Scrolling by whole screens when the caret leaves would be cheaper to
        write and is what an editor that jumps feels like; this keeps the caret
        where it was put and moves the window by exactly as much as it has to.
*/
static fn edit_follow()
{
        positive rows = edit_text_rows();
        positive gutter = edit_gutter();
        positive width = edit_columns > gutter + 1 ? edit_columns - gutter : 1;
        positive display;

        edit_primary_place = edit_place_clamped(edit_primary_place.line,
                                                edit_primary_place.column);

        if (!edit_view_free && edit_primary_place.line < edit_top)
                edit_top = edit_primary_place.line;

        if (!edit_view_free && edit_primary_place.line >= edit_top + rows)
                edit_top = edit_primary_place.line - rows + 1;

        if (edit_line_count <= rows)
                edit_top = 0;
        else if (edit_top + rows > edit_line_count)
                edit_top = edit_line_count - rows;

        display = edit_display_column(edit_primary_place.line,
                                      edit_primary_place.column);

        if (display < edit_left)
                edit_left = display;

        if (display >= edit_left + width)
                edit_left = display - width + 1;
}

/*
        The screen, from the buffer, one row at a time and only where it
        differs from what is already on it.

        Every row is built into a scratch buffer first -- gutter, text,
        whatever colour the selection wants -- and then hashed. A row whose
        hash matches what was last written to it is not written again, so
        typing a character sends one row and a status line rather than a
        screenful, and a serial line at 9600 baud stays usable. A scroll or a
        resize forgets every hash, because a row that now shows a different
        line of the file has to be written whatever it looks like.
*/
static p8 edit_row_bytes[EDIT_COLUMNS_MAX * 16];
static byte_store edit_row_output = {
    edit_row_bytes, sizeof(edit_row_bytes), 0};

static fn edit_row_put(address_any text, positive length)
{
        byte_store_append_exact(address_of edit_row_output, text, length);
}

static fn edit_row_put_character(string_address text, positive length)
{
        positive used;
        p32 value;

        //      Only the first complete scalar is sent: bytes stuck to it that
        //      are not part of it (a stray continuation byte, which the step
        //      over a character takes along) are dropped, and they have no
        //      cell of their own.
        if (!edit_scalar((const p8 address_to)text, length, address_of used,
                         address_of value) ||
            value < 0x20 || value == 0x7f)
                edit_row_put((string_address)"?", 1);
        else if (value < 0x80 || value >= 0xa0)
                edit_row_put(text, used);
}

#define edit_row_put_literal(literal) \
        edit_row_put((string_address)(literal), sizeof(literal) - 1)

//      Whether a place is inside any cursor's selection, and whether any
//      cursor's caret is sitting on it. Walked rather than indexed because the
//      list is short and a per-row index would have to be rebuilt on every
//      edit anyway.
static PURE bool edit_selected(positive line, positive column)
{
        for (positive at = 0; at < edit_cursor_count; at++)
        {
                struct edit_place place = {line, column};
                struct edit_place from;
                struct edit_place to;

                if (!edit_cursor_has_selection(at))
                        continue;

                from = edit_selection_start(at);
                to = edit_selection_end(at);

                if (!edit_place_before(place, from) && edit_place_before(place, to))
                        return true;
        }

        return false;
}

static PURE bool edit_extra_caret(positive line, positive column)
{
        for (positive at = 0; at < edit_cursor_count; at++)
        {
                if (edit_cursors[at].line != line ||
                    edit_cursors[at].column != column)
                        continue;

                if (line == edit_primary_place.line &&
                    column == edit_primary_place.column)
                        continue;

                return true;
        }

        return false;
}

/*
        One row of text, as the bytes that would be sent for it.

        The colour is toggled where the selection starts and stops rather than
        written per cell, so a fully selected line costs two escape sequences
        and not one a character.
*/
static fn edit_row_build(positive screen_row)
{
        positive line = edit_top + screen_row;
        positive gutter = edit_gutter();
        positive width = edit_columns > gutter ? edit_columns - gutter : 0;
        positive display = 0;
        positive drawn = 0;
        bool marked = false;

        edit_row_output.used = 0;

        if (line >= edit_line_count)
        {
                // Past the end of the file. VS Code leaves the gutter empty
                // there rather than numbering lines that do not exist.
                edit_row_put_literal(TERM_GREY);
                edit_row_put((string_address)"~", 1);
                edit_row_put_literal(TERM_RESET);
                return;
        }

        edit_row_put_literal(TERM_GREY);
        positive_to_padded(edit_row_put, line + 1, gutter - 1, ' ', 0);
        edit_row_put_literal(TERM_RESET);
        edit_row_put((string_address)" ", 1);

        for (positive at = 0; at <= edit_lines[line].length && drawn < width;)
        {
                p8 character = at < edit_lines[line].length
                                   ? edit_lines[line].text[at]
                                   : ' ';
                positive cells = 1;
                bool wanted = edit_selected(line, at) ||
                              edit_extra_caret(line, at);

                if (at >= edit_lines[line].length)
                {
                        // The newline itself is shown as one selected space,
                        // which is how a selection that swallows a line end
                        // announces that it did.
                        if (!edit_selected(line, at) &&
                            !edit_extra_caret(line, at))
                                break;

                        cells = 1;
                }
                else if (character == '\t')
                        cells = EDIT_TAB - display % EDIT_TAB;
                else
                        cells = edit_cells_at(line, at);

                if (wanted != marked)
                {
                        if (wanted)
                                edit_row_put_literal(TERM_REVERSE);
                        else
                                edit_row_put_literal(TERM_RESET);
                        marked = wanted;
                }

                if (at < edit_lines[line].length && character != '\t' &&
                    cells != 1)
                {
                        positive next = edit_step_forward(line, at);

                        // A mark goes out with the character it sits on. A wide
                        // character goes out whole where it fits and as blanks
                        // where the view's edge cuts it, so the terminal's
                        // cursor and the drawn count never part.
                        if (!cells)
                        {
                                if (drawn && display > edit_left)
                                        edit_row_put_character(edit_lines[line].text + at,
                                                               next - at);
                        }
                        else if (display >= edit_left && drawn + cells <= width)
                        {
                                edit_row_put_character(edit_lines[line].text + at,
                                                       next - at);
                                drawn += cells;
                        }
                        else
                                for (positive cell = 0; cell < cells && drawn < width; cell++)
                                        if (display + cell >= edit_left)
                                        {
                                                edit_row_put((string_address)" ", 1);
                                                drawn++;
                                        }

                        display += cells;
                        at = next;
                        continue;
                }

                for (positive cell = 0; cell < cells; cell++)
                {
                        if (display + cell < edit_left)
                                continue;

                        if (drawn >= width)
                                break;

                        if (character == '\t' || at >= edit_lines[line].length)
                                edit_row_put((string_address)" ", 1);
                        else
                        {
                                edit_row_put_character(edit_lines[line].text + at,
                                                       edit_step_forward(line, at) - at);
                        }

                        drawn++;
                }

                display += cells;

                if (at >= edit_lines[line].length)
                        break;

                at = edit_step_forward(line, at);
        }

        if (marked)
                edit_row_put_literal(TERM_RESET);
}

//      The status line: what the file is called, whether it has been changed,
//      where the caret is, and whatever the last command had to say. Or, while
//      one is up, the prompt and what has been typed into it.
//
//      Built without any colour in it and coloured at the end, because the bar
//      has to be filled to the width of the window and counting cells through
//      escape sequences is how a status line ends up one character short.
static p8 edit_status_bytes[EDIT_COLUMNS_MAX * 4];
static byte_store edit_status_output = {
    edit_status_bytes, sizeof(edit_status_bytes), 0};
static positive edit_status_cells;

static fn edit_status_put(address_any data, positive length)
{
        p8 address_to text = data;

        //      The name of the file and what a command says of it are the
        //      file's and the user's bytes: a control, a DEL, a C1 control (in
        //      UTF-8 or alone) and what is not UTF-8 are one question mark
        //      each here as they are in the rows.
        for (positive at = 0; at < length;)
        {
                positive used;
                p32 value;
                bool safe = edit_scalar(text + at, length - at, address_of used,
                                        address_of value) &&
                            value >= 0x20 && value != 0x7f &&
                            (value < 0x80 || value >= 0xa0);

                if (!byte_store_append_exact(address_of edit_status_output,
                                             safe ? (address_any)(text + at)
                                                  : (address_any)"?",
                                             safe ? used : 1))
                        return;
                edit_status_cells++;
                at += used;
        }
}

static fn edit_status_put_text(string_address text)
{
        edit_status_put(text, string_length(text));
}

#define edit_status_put_number(value) positive_to_string(edit_status_put, (value))

static fn edit_status_build()
{
        edit_status_output.used = 0;
        edit_status_cells = 0;

        if (edit_prompt_active)
        {
                edit_status_put_text((string_address) " ");
                edit_status_put_text(edit_prompt_label);
                edit_status_put(edit_prompt_text, edit_prompt_length);
        }
        else
        {
                edit_status_put_text((string_address) " ");
                edit_status_put_text(edit_path ? edit_path
                                               : (string_address) "untitled");

                if (edit_modified)
                        edit_status_put_text((string_address) " +");

                edit_status_put_text((string_address) "  Ln ");
                edit_status_put_number(edit_primary_place.line + 1);
                edit_status_put_text((string_address) ", Col ");
                edit_status_put_number(
                    edit_display_column(edit_primary_place.line,
                                        edit_primary_place.column) +
                    1);

                if (edit_cursor_count > 1)
                {
                        edit_status_put_text((string_address) "  ");
                        edit_status_put_number(edit_cursor_count);
                        edit_status_put_text((string_address) " cursors");
                }

                if (edit_message_length)
                {
                        edit_status_put_text((string_address) "  ");
                        edit_status_put(edit_message, edit_message_length);
                }
        }

        positive padding = min(edit_columns - min(edit_status_cells, edit_columns),
                               sizeof(edit_status_bytes) - edit_status_output.used);
        memory_fill(edit_status_bytes + edit_status_output.used, ' ', padding);
        edit_status_output.used += padding;
        edit_status_cells += padding;

        edit_row_output.used = 0;
        edit_row_put_literal(TERM_REVERSE);
        edit_row_put(edit_status_bytes, edit_status_output.used);
        edit_row_put_literal(TERM_RESET);
}

/*
        Everything that changed, sent.

        The cursor is hidden for the whole of a repaint and shown again at the
        end, because a terminal that draws the caret where each row happens to
        finish flickers it across the screen on every keystroke.
*/
static fn edit_draw()
{
        positive rows = edit_text_rows();
        positive gutter;

        edit_follow();
        gutter = edit_gutter();

        edit_say_literal(TERM_HIDE_CURSOR);
        edit_rows_painted = 0;

        for (positive screen_row = 0; screen_row < rows; screen_row++)
        {
                positive mark;

                edit_row_build(screen_row);
                mark = memory_hash_33(edit_row_bytes, edit_row_output.used);

                if (edit_row_known[screen_row] &&
                    edit_row_drawn[screen_row] == mark)
                        continue;

                edit_say_at(screen_row, 0);
                edit_say(edit_row_bytes, edit_row_output.used);
                edit_say_literal(ANSI "K");
                edit_row_drawn[screen_row] = mark;
                edit_row_known[screen_row] = true;
                edit_rows_painted++;
        }

        edit_status_build();
        edit_say_at(edit_rows - 1, 0);
        edit_say_literal(ANSI "K");
        edit_say(edit_row_bytes, edit_row_output.used);

        /*
                Where a person's eye goes, which is the primary caret and not
                wherever the last row finished.

                Except when the caret is not on the screen at all, which is
                what Ctrl+Up and Ctrl+Down are allowed to do: the window
                scrolls and the caret stays on the line it was on, which may
                now be above the top of the window. There is nowhere on the
                screen to put a cursor that is not on the screen, so it stays
                hidden until the caret comes back -- and the row it would have
                been on is never worked out, which is the subtraction that
                would otherwise have gone below zero and, on an unsigned count
                of rows, come out at eighteen quintillion.
        */
        {
                positive display = edit_display_column(edit_primary_place.line,
                                                       edit_primary_place.column);
                positive screen_row;
                positive screen_column;

                if (edit_prompt_active)
                {
                        edit_say_at(edit_rows - 1,
                                    1 + string_length(edit_prompt_label) +
                                        edit_prompt_length);
                        edit_say_literal(TERM_SHOW_CURSOR);
                        return;
                }

                if (edit_primary_place.line < edit_top ||
                    edit_primary_place.line >= edit_top + rows)
                {
                        //      Parked rather than left where the status line
                        //      finished. It is hidden either way, and a
                        //      terminal that ignores DECTCEM should show it
                        //      somewhere deliberate.
                        edit_say_at(edit_rows - 1, 0);
                        return;
                }

                screen_row = edit_primary_place.line - edit_top;
                screen_column = gutter + display - edit_left;
                edit_say_at(screen_row, screen_column);
        }

        edit_say_literal(TERM_SHOW_CURSOR);
}

/*
        The clipboard.

        One piece per cursor, because copying with three cursors and pasting
        with three cursors has to give each of them its own line back rather
        than all three the same blob. Whether the copy was of whole lines is
        remembered with it: Ctrl+C with nothing selected takes the line, and a
        line that is pasted has to arrive as a line above the caret rather than
        spliced into the middle of whatever the caret is in the middle of.
        People rely on that every day without knowing they do.
*/
struct edit_clip
{
        p8 address_to text;
        positive length;
};

static struct edit_clip address_to edit_clips;
static positive edit_clip_count;
static positive edit_clip_room;
static bool edit_clip_lines;

static fn edit_clips_empty()
{
        for (positive at = 0; at < edit_clip_count; at++)
                memory_give(edit_clips[at].text);

        edit_clip_count = 0;
        edit_clip_lines = false;
}

static bool edit_clip_add(p8 address_to text, positive length)
{
        if (!memory_resize_reserve((p8 address_to address_to)address_of edit_clips,
                       address_of edit_clip_room,
                       (edit_clip_count + 1) * sizeof(struct edit_clip),
                       4 * sizeof(struct edit_clip)))
                return false;

        edit_clips[edit_clip_count].text = text;
        edit_clips[edit_clip_count].length = length;
        edit_clip_count++;
        return true;
}

/*
        Motion.

        Every arrow, every Home, every word jump is one entry in this switch
        and one loop over the cursor list. Writing them as separate routines
        would mean writing the loop, the selection rule and the wanted-column
        rule nine times, and the ninth would be the one that forgot to extend
        the selection.
*/
#define EDIT_MOVE_LEFT 0
#define EDIT_MOVE_RIGHT 1
#define EDIT_MOVE_UP 2
#define EDIT_MOVE_DOWN 3
#define EDIT_MOVE_HOME 4
#define EDIT_MOVE_END 5
#define EDIT_MOVE_WORD_LEFT 6
#define EDIT_MOVE_WORD_RIGHT 7
#define EDIT_MOVE_PAGE_UP 8
#define EDIT_MOVE_PAGE_DOWN 9
#define EDIT_MOVE_FILE_START 10
#define EDIT_MOVE_FILE_END 11

//      Where the text of a line begins, past whatever it is indented with.
static PURE positive edit_line_indent(positive line)
{
        struct edit_line address_to text = edit_lines + line;
        return string_span_max(text->text, text->length, string_set_blanks);
}

/*
        A word to the left, and a word to the right.

        The rule is the one every editor with these keys uses and nobody writes
        down: going right skips whatever run the caret is standing in and stops
        at the start of the next one; going left does the same backwards.
        Whitespace is skipped on the way but is not itself a run to stop in the
        middle of, which is why walking off the end of a line lands at the
        start of the next rather than at its first word.
*/
static PURE struct edit_place edit_word_right(struct edit_place place)
{
        struct edit_line address_to text;

        if (place.column >= edit_lines[place.line].length)
        {
                if (place.line + 1 >= edit_line_count)
                        return place;

                place.line++;
                place.column = 0;
                return place;
        }

        text = edit_lines + place.line;
        place.column += string_span_max(text->text + place.column,
                                         text->length - place.column, string_set_blanks);

        if (place.column < text->length && edit_is_word(text->text[place.column]))
        {
                while (place.column < text->length &&
                       edit_is_word(text->text[place.column]))
                        place.column = edit_step_forward(place.line, place.column);

                return place;
        }

        while (place.column < text->length &&
               !edit_is_word(text->text[place.column]) &&
               !byte_is_blank(text->text[place.column]))
                place.column = edit_step_forward(place.line, place.column);

        return place;
}

static PURE struct edit_place edit_word_left(struct edit_place place)
{
        struct edit_line address_to text;

        if (!place.column)
        {
                if (!place.line)
                        return place;

                place.line--;
                place.column = edit_lines[place.line].length;
                return place;
        }

        text = edit_lines + place.line;

        while (place.column && byte_is_blank(text->text[place.column - 1]))
                place.column--;

        if (place.column && edit_is_word(text->text[place.column - 1]))
        {
                while (place.column && edit_is_word(text->text[place.column - 1]))
                        place.column = edit_step_back(place.line, place.column);

                return place;
        }

        while (place.column && !edit_is_word(text->text[place.column - 1]) &&
               !byte_is_blank(text->text[place.column - 1]))
                place.column = edit_step_back(place.line, place.column);

        return place;
}

static struct edit_place edit_motion(p8 kind,
                                     struct edit_cursor address_to cursor)
{
        struct edit_place place = {cursor->line, cursor->column};
        positive rows = edit_text_rows();
        positive indent;

        switch (kind)
        {
        case EDIT_MOVE_LEFT:
                if (place.column)
                        place.column = edit_step_back(place.line, place.column);
                else if (place.line)
                {
                        place.line--;
                        place.column = edit_lines[place.line].length;
                }

                break;

        case EDIT_MOVE_RIGHT:
                if (place.column < edit_lines[place.line].length)
                        place.column = edit_step_forward(place.line, place.column);
                else if (place.line + 1 < edit_line_count)
                {
                        place.line++;
                        place.column = 0;
                }

                break;

        case EDIT_MOVE_UP:
                if (!place.line)
                {
                        place.column = 0;
                        break;
                }

                place.line--;
                place.column = edit_column_at_display(place.line, cursor->wanted);
                return place;

        case EDIT_MOVE_DOWN:
                if (place.line + 1 >= edit_line_count)
                {
                        place.column = edit_lines[place.line].length;
                        break;
                }

                place.line++;
                place.column = edit_column_at_display(place.line, cursor->wanted);
                return place;

        case EDIT_MOVE_PAGE_UP:
                place.line = place.line > rows - 1 ? place.line - (rows - 1) : 0;
                place.column = edit_column_at_display(place.line, cursor->wanted);
                return place;

        case EDIT_MOVE_PAGE_DOWN:
                place.line = place.line + rows - 1 < edit_line_count
                                 ? place.line + rows - 1
                                 : edit_line_count - 1;
                place.column = edit_column_at_display(place.line, cursor->wanted);
                return place;

        case EDIT_MOVE_HOME:
                /*
                        Home goes to the first thing on the line and, from
                        there, to the very start of it. Indented code is
                        almost always what a person wants the caret in front
                        of, and the second press is there for the times it is
                        not.
                */
                indent = edit_line_indent(place.line);
                place.column = place.column == indent ? 0 : indent;
                break;

        case EDIT_MOVE_END:
                place.column = edit_lines[place.line].length;
                break;

        case EDIT_MOVE_WORD_LEFT:
                place = edit_word_left(place);
                break;

        case EDIT_MOVE_WORD_RIGHT:
                place = edit_word_right(place);
                break;

        case EDIT_MOVE_FILE_START:
                place.line = 0;
                place.column = 0;
                break;

        case EDIT_MOVE_FILE_END:
                place = edit_place_last();
                break;
        }

        cursor->wanted = edit_display_column(place.line, place.column);
        return place;
}

//      Which cursor the window is following, found by its position so that
//      sorting the list cannot lose it.
static PURE positive edit_primary_index()
{
        for (positive at = 0; at < edit_cursor_count; at++)
                if (edit_cursors[at].line == edit_primary_place.line &&
                    edit_cursors[at].column == edit_primary_place.column)
                        return at;

        return 0;
}

static fn edit_primary_from(positive index)
{
        if (!edit_cursor_count)
                return;

        if (index >= edit_cursor_count)
                index = edit_cursor_count - 1;

        edit_primary_place = edit_cursor_caret(index);
}

/*
        A motion, applied to every cursor.

        The undo step is sealed here and not anywhere else, and that is what
        makes "undo puts the carets back" true rather than usually true: the
        positions a step remembers are the ones the run of typing started from,
        so nothing may move a caret between the step opening and it closing.
*/
static fn edit_move(p8 kind, bool extend)
{
        positive index = edit_primary_index();

        edit_step_seal();
        edit_view_free = false;

        for (positive at = 0; at < edit_cursor_count; at++)
        {
                struct edit_place place;

                /*
                        Left and Right with something selected put the caret at
                        the end of it rather than moving. Pressing Right after
                        selecting a word should leave the caret after the word,
                        not after the character following it, and an editor
                        that moves instead is one people stop trusting arrows
                        in.
                */
                if (!extend && edit_cursor_has_selection(at) &&
                    (kind == EDIT_MOVE_LEFT || kind == EDIT_MOVE_RIGHT))
                {
                        place = kind == EDIT_MOVE_LEFT ? edit_selection_start(at)
                                                       : edit_selection_end(at);
                        edit_cursors[at].wanted =
                            edit_display_column(place.line, place.column);
                }
                else
                        place = edit_motion(kind, edit_cursors + at);

                edit_cursor_place(at, place, extend);
        }

        edit_cursors_sort();
        edit_primary_from(index);
}

/*
        Editing.

        Every one of these walks the cursor list from the last to the first.
        That order is the whole of what makes several cursors work: an edit at
        a later position cannot move an earlier one, so every cursor still
        holds a position that means what it meant when the loop started. Going
        the other way would need every remaining cursor adjusted after every
        edit, which is edit_cursors_shift doing work that ordering does for
        free.
*/
static bool edit_delete_selections(p8 kind)
{
        bool any = false;

        for (positive at = edit_cursor_count; at; at--)
        {
                positive index = at - 1;
                struct edit_place from;
                struct edit_place to;

                if (!edit_cursor_has_selection(index))
                        continue;

                from = edit_selection_start(index);
                to = edit_selection_end(index);
                edit_change(from, to, null, 0, kind);
                edit_cursor_place(index, from, false);
                any = true;
        }

        return any;
}

static fn edit_settle(positive index)
{
        edit_view_free = false;
        edit_cursors_sort();
        edit_primary_from(index);
        edit_step_note_cursors();
}

static fn edit_insert(string_address text, positive length, p8 kind)
{
        positive index = edit_primary_index();

        if (kind == EDIT_STEP_OTHER)
                edit_step_seal();

        for (positive at = edit_cursor_count; at; at--)
        {
                positive cursor = at - 1;
                struct edit_place from = edit_cursor_caret(cursor);
                struct edit_place to = from;
                struct edit_place after;

                if (edit_cursor_has_selection(cursor))
                {
                        from = edit_selection_start(cursor);
                        to = edit_selection_end(cursor);
                }

                after = edit_change(from, to, text, length, kind);
                edit_cursor_place(cursor, after, false);
                edit_cursors[cursor].wanted =
                    edit_display_column(after.line, after.column);
        }

        edit_settle(index);

        if (kind == EDIT_STEP_OTHER)
                edit_step_seal();
}

static fn edit_delete_character(bool backward)
{
        positive index = edit_primary_index();

        if (edit_delete_selections(EDIT_STEP_ERASING))
        {
                edit_settle(index);
                return;
        }

        for (positive at = edit_cursor_count; at; at--)
        {
                positive cursor = at - 1;
                struct edit_place from = edit_cursor_caret(cursor);
                struct edit_place to = from;

                if (backward)
                {
                        if (to.column)
                                from.column = edit_step_back(to.line, to.column);
                        else if (to.line)
                        {
                                from.line = to.line - 1;
                                from.column = edit_lines[from.line].length;
                        }
                        else
                                continue;
                }
                else if (from.column < edit_lines[from.line].length)
                        to.column = edit_step_forward(from.line, from.column);
                else if (from.line + 1 < edit_line_count)
                {
                        to.line = from.line + 1;
                        to.column = 0;
                }
                else
                        continue;

                edit_change(from, to, null, 0, EDIT_STEP_ERASING);
                edit_cursor_place(cursor, from, false);

                if (backward)
                        edit_cursors[cursor].wanted =
                            edit_display_column(from.line, from.column);
        }

        edit_settle(index);
}

static fn edit_delete_word_left()
{
        positive index = edit_primary_index();

        edit_step_seal();

        if (!edit_delete_selections(EDIT_STEP_OTHER))
                for (positive at = edit_cursor_count; at; at--)
                {
                        positive cursor = at - 1;
                        struct edit_place to = edit_cursor_caret(cursor);
                        struct edit_place from = edit_word_left(to);

                        if (edit_place_same(from, to))
                                continue;

                        edit_change(from, to, null, 0, EDIT_STEP_OTHER);
                        edit_cursor_place(cursor, from, false);
                }

        edit_settle(index);
        edit_step_seal();
}

/*
        Enter, which is a newline and then whatever the line above was indented
        with.

        The indent is taken per cursor rather than once, because with several
        cursors on differently indented lines one indent for all of them is
        wrong for all but one of them. Only the part of the indent before the
        caret is carried, so splitting a line in the middle of its indentation
        does not hand the new line more than there was.
*/
static fn edit_newline()
{
        positive index = edit_primary_index();

        edit_step_seal();
        edit_delete_selections(EDIT_STEP_OTHER);

        for (positive at = edit_cursor_count; at; at--)
        {
                positive cursor = at - 1;
                struct edit_place from = edit_cursor_caret(cursor);
                struct edit_place after;
                p8 built[EDIT_COLUMNS_MAX + 1];
                positive indent = edit_line_indent(from.line);
                positive length = 1;

                if (indent > from.column)
                        indent = from.column;

                if (indent > sizeof(built) - 1)
                        indent = sizeof(built) - 1;

                built[0] = '\n';
                memory_copy_apart(built + 1, edit_lines[from.line].text, indent);
                length += indent;

                after = edit_change(from, from, built, length, EDIT_STEP_OTHER);
                edit_cursor_place(cursor, after, false);
                edit_cursors[cursor].wanted =
                    edit_display_column(after.line, after.column);
        }

        edit_settle(index);
        edit_step_seal();
}

/*
        Two cursors on one line are one line, for anything that works by the
        line.

        Tab, Ctrl+/, Alt+Up and cutting all take a range of lines rather than a
        span of characters, and two carets on one line hand them the same range
        twice. Without this, Tab with two carets on a line indents it sixteen
        columns and Ctrl+X cuts two lines where one was highlighted -- which is
        the first thing anybody notices about a multiple cursor editor that has
        not thought about it.

        The walk is downwards, so what has already been done is everything at
        or below claimed, and a range that reaches into it is trimmed or
        dropped.
*/
static bool edit_range_taken(positive address_to claimed, positive first,
                             positive address_to last)
{
        if (first >= address_to claimed)
                return true;

        if (address_to last >= address_to claimed)
                address_to last = address_to claimed - 1;

        address_to claimed = first;
        return false;
}

//      The first and last line a cursor is working on. A selection that stops
//      at the very start of a line has not reached into it, which is the rule
//      that stops Tab from indenting one line more than was highlighted.
static PURE positive edit_range_first(positive at)
{
        return edit_cursor_has_selection(at) ? edit_selection_start(at).line
                                             : edit_cursors[at].line;
}

static PURE positive edit_range_last(positive at)
{
        struct edit_place from;
        struct edit_place to;

        if (!edit_cursor_has_selection(at))
                return edit_cursors[at].line;

        from = edit_selection_start(at);
        to = edit_selection_end(at);

        if (!to.column && to.line > from.line)
                return to.line - 1;

        return to.line;
}

struct edit_line_range
{
        positive first, last;
};

static CONST bipolar edit_range_order(struct edit_line_range a,
                                      struct edit_line_range b)
{
        return (a.first > b.first) - (a.first < b.first);
}

/* Caret order is not selection-start order. Capture and merge the selected
   rows before any edit can shift an overlapping selection's endpoints. */
static positive edit_ranges_normalize(struct edit_line_range address_to ranges,
                                      bool adjacent)
{
        positive count = edit_cursor_count, kept = 0;
        bool ordered = true;
        for (positive at = 0; at < count; at++)
        {
                ranges[at] = (struct edit_line_range){
                    edit_range_first(at), edit_range_last(at)};
                if (at && ranges[at - 1].first > ranges[at].first)
                        ordered = false;
        }

        struct edit_line_range address_to sorted = ordered ? ranges
            : array_merge_sort(ranges, ranges + count, count, edit_range_order);
        for (positive at = 0; at < count; at++)
        {
                struct edit_line_range held = sorted[at];
                if (kept && held.first <= ranges[kept - 1].last + adjacent)
                        ranges[kept - 1].last = max(ranges[kept - 1].last, held.last);
                else
                        ranges[kept++] = held;
        }
        return kept;
}

struct edit_line_ranges
{
        struct edit_line_range single;
        struct edit_line_range address_to data;
        positive count;
};

static bool edit_ranges_take(struct edit_line_ranges address_to ranges,
                             bool adjacent)
{
        ranges->data = address_of ranges->single;
        if (edit_cursor_count > 1)
        {
                if (edit_cursor_count > positive_max / (2 * sizeof(*ranges->data)))
                        return false;
                ranges->data = memory_take(edit_cursor_count * 2 * sizeof(*ranges->data));
                if (!ranges->data)
                        return false;
        }
        ranges->count = edit_ranges_normalize(ranges->data, adjacent);
        return true;
}

static fn edit_ranges_release(struct edit_line_ranges address_to ranges)
{
        if (ranges->data != address_of ranges->single)
                memory_give(ranges->data);
}

/*
        Tab and Shift+Tab.

        With nothing selected Tab is a tab: spaces out to the next stop, from
        wherever the caret is, which is what makes it usable for lining
        something up as well as for indenting. With a selection it is the
        block: every line the selection touches moves, the selection stays on
        the same text, and the caret does not jump to the front of the file.
*/
static fn edit_indent_lines(bool out)
{
        positive index = edit_primary_index();
        p8 spaces[EDIT_TAB];
        struct edit_line_ranges ranges;
        if (!edit_ranges_take(address_of ranges, false))
                return;

        edit_step_seal();
        memory_fill(spaces, ' ', sizeof(spaces));

        for (positive at = ranges.count; at; at--)
        {
                positive first = ranges.data[at - 1].first;
                positive last = ranges.data[at - 1].last;

                for (positive line = last + 1; line > first; line--)
                {
                        positive which = line - 1;
                        struct edit_place from = {which, 0};
                        struct edit_place to = {which, 0};

                        if (!out)
                        {
                                // An empty line is left empty rather than
                                // given eight spaces of nothing.
                                if (!edit_lines[which].length)
                                        continue;

                                edit_change(from, from, spaces, EDIT_TAB,
                                            EDIT_STEP_OTHER);
                                continue;
                        }

                        while (to.column < EDIT_TAB &&
                               to.column < edit_lines[which].length &&
                               edit_lines[which].text[to.column] == ' ')
                                to.column++;

                        if (!to.column && edit_lines[which].length &&
                            edit_lines[which].text[0] == '\t')
                                to.column = 1;

                        if (!to.column)
                                continue;

                        edit_change(from, to, null, 0, EDIT_STEP_OTHER);
                }
        }

        edit_ranges_release(address_of ranges);
        edit_settle(index);
        edit_step_seal();
}

static fn edit_tab(bool back)
{
        bool block = false;

        for (positive at = 0; at < edit_cursor_count; at++)
                if (edit_cursor_has_selection(at) &&
                    edit_selection_start(at).line != edit_selection_end(at).line)
                        block = true;

        if (back || block)
        {
                edit_indent_lines(back);
                return;
        }

        {
                p8 spaces[EDIT_TAB];
                positive width;
                positive column = edit_display_column(edit_primary_place.line,
                                                      edit_primary_place.column);

                width = EDIT_TAB - column % EDIT_TAB;
                memory_fill(spaces, ' ', width);
                edit_insert(spaces, width, EDIT_STEP_OTHER);
        }
}

/* A selection ending at column zero belongs to the preceding line's end,
   not the text at that position. Map that boundary through the same row
   permutation as its selected text, then turn it back into a boundary. */
static struct edit_place edit_line_shift(struct edit_place place, bool boundary,
                                        positive first, positive last,
                                        bool up, bool copy)
{
        positive line = place.line - boundary;
        if (line >= first && line <= last)
                line = up ? line - !copy : line + (copy ? last - first + 1 : 1);
        else if (copy && line >= first)
                line += last - first + 1;
        else if (!copy && line == (up ? first - 1 : last + 1))
                line = up ? last : first;
        place.line = line + boundary;
        return place;
}

/* Whole-line move and duplication share one journalled replacement. Build
   its two spans directly: no intermediate copy of the complete old block.
   Cursor positions follow the row permutation, not the generic replacement
   collapse, which loses interior columns and can underflow on an upward move. */
static fn edit_transfer_lines(bool up, bool copy)
{
        positive index = edit_primary_index();
        struct edit_line_ranges ranges;
        // Adjacent moves are one block; independent adjacent copies retain
        // their per-line duplication order. Overlaps are always one range.
        if (!edit_ranges_take(address_of ranges, !copy))
                return;

        edit_step_seal();

        for (positive at = ranges.count; at; at--)
        {
                positive first = ranges.data[at - 1].first;
                positive last = ranges.data[at - 1].last;
                if (!copy && (up ? !first : last + 1 >= edit_line_count))
                        continue;

                struct edit_place from = {first, 0};
                struct edit_place to = {last, edit_lines[last].length};
                positive length = edit_span_length(from, to);
                positive other = copy ? first : up ? first - 1 : last + 1;
                positive extra = copy ? 0 : edit_lines[other].length;
                if (extra > positive_max - 2 || length > positive_max - extra - 2)
                        continue;
                p8 address_to built = (p8 address_to)memory_take(length + extra + 2);
                if (!built)
                        continue;

                if (up || copy)
                {
                        edit_span_copy(from, to, built);
                        built[length] = '\n';
                        if (extra)
                                memory_copy_apart(built + length + 1,
                                                  edit_lines[other].text, extra);
                }
                else
                {
                        memory_copy_apart(built, edit_lines[other].text, extra);
                        built[extra] = '\n';
                        edit_span_copy(from, to, built + extra + 1);
                }

                if (copy)
                        to = from;
                else if (up)
                        from.line = other;
                else
                        to = (struct edit_place){other, extra};
                struct edit_place after = edit_change_mode(
                    from, to, built, length + extra + 1, EDIT_STEP_OTHER, false);
                memory_give(built);
                if (edit_place_same(after, from))
                        continue;

                for (positive c = 0; c < edit_cursor_count; c++)
                {
                        struct edit_cursor address_to moving = edit_cursors + c;
                        bool caret_end = moving->selecting && !moving->column &&
                                         moving->line > moving->anchor_line;
                        bool anchor_end = moving->selecting && !moving->anchor_column &&
                                          moving->anchor_line > moving->line;
                        struct edit_place caret = edit_line_shift(edit_cursor_caret(c),
                            caret_end, first, last, up, copy);
                        struct edit_place anchor = edit_line_shift(edit_cursor_anchor(c),
                            anchor_end, first, last, up, copy);
                        moving->line = caret.line;
                        moving->column = caret.column;
                        moving->anchor_line = anchor.line;
                        moving->anchor_column = anchor.column;
                }
        }

        edit_ranges_release(address_of ranges);
        edit_settle(index);
        edit_step_seal();
}

#define edit_move_lines(up) edit_transfer_lines(up, false)
#define edit_copy_lines(up) edit_transfer_lines(up, true)

/*
        Ctrl+/ -- the comment marker for whatever this file is, put on or taken
        off every line the selection touches.

        Which marker is a question about the file name and nothing else. There
        is no parser here and there does not need to be: getting it wrong costs
        two characters that are easy to see and easy to undo, and getting it
        from a language server costs a language server.
*/
static PURE string_address edit_comment_marker()
{
        static const string_address names[] = {
            ".c", ".h", ".cc", ".cpp", ".hpp", ".js", ".ts", ".go",
            ".rs", ".inc", ".java", ".lua", ".sql"};
        string_address tail = edit_path ? string_last_of(edit_path, '.') : null;
        positive which = tail ? string_table_find(tail, names, sizeof(names[0]),
                                                   array_count(names))
                              : array_count(names);
        return (string_address)(which < 11 ? "// "
                                : which < array_count(names) ? "-- " : "# ");
}

static fn edit_toggle_comment()
{
        positive index = edit_primary_index();
        string_address marker = edit_comment_marker();
        positive marker_length = string_length(marker);
        positive marker_bare = marker_length - 1;
        struct edit_line_ranges ranges;
        if (!edit_ranges_take(address_of ranges, false))
                return;

        // The marker is written with a space after it and recognised without
        // one, so a line commented by hand as "//x" is still uncommented.
        edit_step_seal();

        for (positive at = ranges.count; at; at--)
        {
                positive first = ranges.data[at - 1].first;
                positive last = ranges.data[at - 1].last;
                // An empty first line keeps the block at column zero.
                positive column = edit_line_indent(first);
                bool all = true;
                bool any = false;

                for (positive line = first; line <= last; line++)
                {
                        if (!edit_lines[line].length)
                                continue;

                        any = true;
                        positive indent = edit_line_indent(line);
                        column = min(column, indent);
                        if (indent + marker_bare > edit_lines[line].length ||
                            memory_compare(edit_lines[line].text + indent,
                                           marker, marker_bare))
                                all = false;
                }

                if (!any)
                        continue;

                for (positive line = last + 1; line > first; line--)
                {
                        positive which = line - 1;
                        positive indent;
                        struct edit_place from;
                        struct edit_place to;

                        if (!edit_lines[which].length)
                                continue;

                        indent = edit_line_indent(which);

                        if (!all)
                        {
                                from.line = which;
                                from.column = column;
                                edit_change(from, from, marker, marker_length,
                                            EDIT_STEP_OTHER);
                                continue;
                        }

                        from.line = which;
                        from.column = indent;
                        to.line = which;
                        to.column = indent + marker_length;

                        if (to.column > edit_lines[which].length ||
                            memory_compare(edit_lines[which].text + indent,
                                           marker, marker_length) != 0)
                                to.column = indent + marker_bare;

                        edit_change(from, to, null, 0, EDIT_STEP_OTHER);
                }
        }

        edit_ranges_release(address_of ranges);
        edit_settle(index);
        edit_step_seal();
}

/*
        Copy, cut and paste.

        Ctrl+C with nothing selected takes the whole line, including the
        newline that ends it, and Ctrl+V of such a copy puts it back as a line
        above the caret rather than splicing it into the middle of one. That
        pair is what makes "copy this line, go somewhere, paste it" work
        without ever selecting anything, and it is the single most used thing
        in this file that nobody could name.
*/
static fn edit_copy(bool cut)
{
        positive index = edit_primary_index();
        positive taken = 0;
        positive claimed;
        bool lines = true;

        edit_step_seal();

        for (positive at = 0; at < edit_cursor_count; at++)
                if (edit_cursor_has_selection(at))
                        lines = false;

        edit_clips_empty();
        edit_clip_lines = lines;

        for (positive at = 0; at < edit_cursor_count; at++)
        {
                struct edit_place from;
                struct edit_place to;
                p8 address_to block;
                positive length = 0;

                if (lines)
                {
                        positive first = edit_range_first(at);
                        positive last = edit_range_last(at);

                        //      Upwards this time, because the pieces have to
                        //      come out in the order the file has them.
                        if (last < taken)
                                continue;

                        if (first < taken)
                                first = taken;

                        taken = last + 1;
                        from.line = first;
                        from.column = 0;
                        to.line = last;
                        to.column = edit_lines[last].length;
                }
                else if (edit_cursor_has_selection(at))
                {
                        from = edit_selection_start(at);
                        to = edit_selection_end(at);
                }
                else
                        continue;

                block = edit_span_take(from, to, address_of length);

                if (block)
                        edit_clip_add(block, length);
        }

        if (!cut)
        {
                edit_status_say((string_address) "copied");
                return;
        }

        if (!lines)
        {
                edit_delete_selections(EDIT_STEP_OTHER);
                edit_settle(index);
                edit_step_seal();
                return;
        }

        claimed = edit_line_count;

        for (positive at = edit_cursor_count; at; at--)
        {
                positive cursor = at - 1;
                positive first = edit_range_first(cursor);
                positive last = edit_range_last(cursor);
                struct edit_place from;
                struct edit_place to;

                if (edit_range_taken(address_of claimed, first, address_of last))
                        continue;

                from.line = first;
                from.column = 0;

                // The line and the newline after it, or -- on the last line of
                // the file, which has no newline after it -- the newline
                // before it, so that cutting the last line does not leave an
                // empty one behind.
                if (last + 1 < edit_line_count)
                {
                        to.line = last + 1;
                        to.column = 0;
                }
                else if (first)
                {
                        from.line = first - 1;
                        from.column = edit_lines[first - 1].length;
                        to.line = last;
                        to.column = edit_lines[last].length;
                }
                else
                {
                        to.line = last;
                        to.column = edit_lines[last].length;
                }

                edit_change(from, to, null, 0, EDIT_STEP_OTHER);
                edit_cursor_place(cursor, edit_place_clamped(from.line, 0), false);
        }

        edit_settle(index);
        edit_step_seal();
}

static fn edit_paste()
{
        positive index = edit_primary_index();

        if (!edit_clip_count)
                return;

        edit_step_seal();

        for (positive at = edit_cursor_count; at; at--)
        {
                positive cursor = at - 1;
                //      One piece each when the copy came from the same number
                //      of cursors, and the first piece for everybody
                //      otherwise -- which is a plain single-cursor copy being
                //      pasted at several places.
                positive which = edit_clip_count == edit_cursor_count ? cursor : 0;
                struct edit_place from = edit_cursor_caret(cursor);
                struct edit_place to = from;
                struct edit_place after;

                if (edit_clip_lines)
                {
                        p8 address_to built =
                            (p8 address_to)memory_take(edit_clips[which].length + 2);

                        if (!built)
                                continue;

                        memory_copy_apart(built, edit_clips[which].text,
                                          edit_clips[which].length);
                        built[edit_clips[which].length] = '\n';

                        from.column = 0;
                        to = from;
                        after = edit_change(from, to, built,
                                            edit_clips[which].length + 1,
                                            EDIT_STEP_OTHER);
                        memory_give(built);
                        edit_cursor_place(cursor, after, false);
                        continue;
                }

                if (edit_cursor_has_selection(cursor))
                {
                        from = edit_selection_start(cursor);
                        to = edit_selection_end(cursor);
                }

                after = edit_change(from, to, edit_clips[which].text,
                                    edit_clips[which].length, EDIT_STEP_OTHER);
                edit_cursor_place(cursor, after, false);
        }

        edit_settle(index);
        edit_step_seal();
}

//      Ctrl+A, and Ctrl+L, which grows a line at a time the way pressing it
//      again in VS Code does.
static fn edit_select_all()
{
        struct edit_place last = edit_place_last();

        edit_step_seal();
        edit_cursors_one();
        edit_cursors[0].anchor_line = 0;
        edit_cursors[0].anchor_column = 0;
        edit_cursors[0].selecting = true;
        edit_cursors[0].line = last.line;
        edit_cursors[0].column = last.column;
        edit_primary_place = last;
}

static fn edit_select_line()
{
        positive index = edit_primary_index();

        edit_step_seal();

        for (positive at = 0; at < edit_cursor_count; at++)
        {
                struct edit_cursor address_to cursor = edit_cursors + at;
                positive first = edit_range_first(at);
                positive last = edit_range_last(at);

                if (edit_cursor_has_selection(at) && !cursor->anchor_column &&
                    !cursor->column && cursor->line > cursor->anchor_line)
                        last = cursor->line;

                cursor->anchor_line = first;
                cursor->anchor_column = 0;
                cursor->selecting = true;

                if (last + 1 < edit_line_count)
                {
                        cursor->line = last + 1;
                        cursor->column = 0;
                }
                else
                {
                        cursor->line = last;
                        cursor->column = edit_lines[last].length;
                }
        }

        edit_cursors_sort();
        edit_primary_from(index);
}

/*
        The two things a mouse needs, named here so that the agent who decodes
        the reports does not have to invent them or reach into the cursor list.

        A click is edit_place_cursor with extend false, a drag is the same
        place with extend true, and Alt+click is edit_cursor_add. That is the
        whole surface, and everything else about a mouse -- the wheel, the
        double click, the selection that grows by words -- is those three plus
        arithmetic.
*/
static fn EDIT_SPARE edit_place_cursor(positive line, positive column, bool extend)
{
        struct edit_place place = edit_place_clamped(line, column);

        edit_step_seal();
        edit_view_free = false;

        if (!extend)
                edit_cursors_one();

        if (!edit_cursor_count)
                return;

        edit_cursor_place(0, place, extend);
        edit_cursors[0].wanted = edit_display_column(place.line, place.column);
        edit_primary_place = place;
}

static bool EDIT_SPARE edit_cursor_add(positive line, positive column)
{
        struct edit_place place = edit_place_clamped(line, column);

        edit_step_seal();

        if (!edit_cursors_room_for(edit_cursor_count + 1))
                return false;

        memory_zero(edit_cursors + edit_cursor_count, sizeof(struct edit_cursor));
        edit_cursors[edit_cursor_count].line = place.line;
        edit_cursors[edit_cursor_count].column = place.column;
        edit_cursors[edit_cursor_count].anchor_line = place.line;
        edit_cursors[edit_cursor_count].anchor_column = place.column;
        edit_cursors[edit_cursor_count].wanted =
            edit_display_column(place.line, place.column);
        edit_cursor_count++;
        edit_cursors_sort();
        edit_primary_place = place;
        return true;
}

/*
        What a terminal actually sends, and what it means.

        A key here is one number: a Unicode code point for anything that is a
        character, one of the names below for anything that is not, and the
        three modifier bits on top of either. One number rather than a pair
        because every table in this file wants to be a switch and a switch on
        two things is a nest.

        The names start above the last code point Unicode has, so a character
        and a name can never be the same number by accident. The modifier bits
        start above those, for the same reason.
*/
#define EDIT_KEY_NONE 0
#define EDIT_KEY_NAMED 0x110000u

#define EDIT_KEY_UP (EDIT_KEY_NAMED + 1)
#define EDIT_KEY_DOWN (EDIT_KEY_NAMED + 2)
#define EDIT_KEY_RIGHT (EDIT_KEY_NAMED + 3)
#define EDIT_KEY_LEFT (EDIT_KEY_NAMED + 4)
#define EDIT_KEY_HOME (EDIT_KEY_NAMED + 5)
#define EDIT_KEY_END (EDIT_KEY_NAMED + 6)
#define EDIT_KEY_PAGE_UP (EDIT_KEY_NAMED + 7)
#define EDIT_KEY_PAGE_DOWN (EDIT_KEY_NAMED + 8)
#define EDIT_KEY_INSERT (EDIT_KEY_NAMED + 9)
#define EDIT_KEY_REMOVE (EDIT_KEY_NAMED + 10)
#define EDIT_KEY_BACKSPACE (EDIT_KEY_NAMED + 11)
#define EDIT_KEY_ENTER (EDIT_KEY_NAMED + 12)
#define EDIT_KEY_TAB (EDIT_KEY_NAMED + 13)
#define EDIT_KEY_ESCAPE (EDIT_KEY_NAMED + 14)
#define EDIT_KEY_FUNCTION (EDIT_KEY_NAMED + 32)

#define EDIT_KEY_SHIFT 0x1000000u
#define EDIT_KEY_ALT 0x2000000u
#define EDIT_KEY_CONTROL 0x4000000u
#define EDIT_KEY_MODIFIERS 0x7000000u

//      The decoder's state. Escape is the only one of these that a person can
//      also mean on its own, which is what edit_input_idle is for.
#define EDIT_INPUT_GROUND 0
#define EDIT_INPUT_ESCAPE 1
#define EDIT_INPUT_CSI 2
#define EDIT_INPUT_SS3 3
#define EDIT_INPUT_UTF8 4

static p8 edit_input_state;
static terminal_parameters edit_input_csi;
static memory_utf8_state edit_input_utf8;
static bool edit_input_alt;

/*
        Bracketed paste is one edit, not a stream of commands.

        Without this, a pasted newline went through edit_newline and acquired
        indentation that was not in the clipboard, while a control byte or an
        escape sequence could invoke an editor command.  The terminal frames a
        paste with CSI 200~ and CSI 201~. Hold its bytes until the closing
        frame, then insert the exact block in one journal step.
*/
static p8 address_to edit_input_paste;
static positive edit_input_paste_length;
static positive edit_input_paste_room;
static positive edit_input_paste_match;
static bool edit_input_pasting;
static bool edit_input_paste_failed;

static p8 edit_input_paste_end[] = {27, '[', '2', '0', '1', '~'};

static fn edit_key(positive key);

//      The three bits a terminal packs into the second CSI parameter: one is
//      no modifier at all, and everything above that is one more than the sum
//      of shift, alt and control.
static CONST positive edit_modifiers_from(positive value)
{
        return value > 1 ? ((value - 1) & 7) * EDIT_KEY_SHIFT : 0;
}

//      A byte below space, as the key a person pressed to make it. Ctrl+letter
//      arrives as the letter's place in the alphabet and nothing else, which
//      is why an editor that wants Ctrl+S has to know that 0x13 is what it
//      looks like.
static CONST positive edit_key_from_control(p8 byte)
{
        // xterm sends DEL for Backspace and BS for Ctrl+Backspace.
        // Ctrl+/ and Ctrl+_ share byte 31; the binding uses Ctrl+/.
        static const positive keys[32] = {
            [0] = EDIT_KEY_CONTROL | ' ', [8] = EDIT_KEY_CONTROL | EDIT_KEY_BACKSPACE,
            ['\t'] = EDIT_KEY_TAB, ['\r'] = EDIT_KEY_ENTER, ['\n'] = EDIT_KEY_ENTER,
            [27] = EDIT_KEY_ESCAPE, [28] = EDIT_KEY_CONTROL | '\\',
            [29] = EDIT_KEY_CONTROL | ']', [30] = EDIT_KEY_CONTROL | '^',
            [31] = EDIT_KEY_CONTROL | '/'
        };
        if (byte < array_count(keys) && keys[byte])
                return keys[byte];
        return byte < 27 ? EDIT_KEY_CONTROL | (positive)('a' + byte - 1)
                         : EDIT_KEY_NONE;
}

//      The keys a CSI sequence ending in a tilde names, by its first
//      parameter. The numbers are xterm's and every terminal worth decoding
//      agrees with them.
static CONST positive edit_key_from_tilde(positive value)
{
        static const positive keys[] = {
            [1] = EDIT_KEY_HOME, [2] = EDIT_KEY_INSERT,
            [3] = EDIT_KEY_REMOVE, [4] = EDIT_KEY_END,
            [5] = EDIT_KEY_PAGE_UP, [6] = EDIT_KEY_PAGE_DOWN,
            [7] = EDIT_KEY_HOME, [8] = EDIT_KEY_END,
            [11] = EDIT_KEY_FUNCTION + 1, [12] = EDIT_KEY_FUNCTION + 2,
            [13] = EDIT_KEY_FUNCTION + 3, [14] = EDIT_KEY_FUNCTION + 4,
            [15] = EDIT_KEY_FUNCTION + 5, [17] = EDIT_KEY_FUNCTION + 6,
            [18] = EDIT_KEY_FUNCTION + 7, [19] = EDIT_KEY_FUNCTION + 8,
            [20] = EDIT_KEY_FUNCTION + 9, [21] = EDIT_KEY_FUNCTION + 10,
            [23] = EDIT_KEY_FUNCTION + 11, [24] = EDIT_KEY_FUNCTION + 12
        };
        return value < array_count(keys) ? keys[value] : EDIT_KEY_NONE;
}

static CONST positive edit_key_from_final(p8 final)
{
        if (final >= 'A' && final <= 'D')
                return EDIT_KEY_UP + final - 'A';
        if (final >= 'P' && final <= 'S')
                return EDIT_KEY_FUNCTION + final - 'P' + 1;
        if (final == 'F')
                return EDIT_KEY_END;
        if (final == 'H')
                return EDIT_KEY_HOME;

        return EDIT_KEY_NONE;
}

static fn edit_input_reset()
{
        edit_input_state = EDIT_INPUT_GROUND;
        edit_input_utf8.left = 0;
        terminal_parameters_reset(address_of edit_input_csi);
        edit_input_alt = false;
        edit_input_pasting = false;
        edit_input_paste_length = 0;
        edit_input_paste_match = 0;
        edit_input_paste_failed = false;
}

static fn edit_input_deliver(positive key)
{
        if (key == EDIT_KEY_NONE)
                return;

        if (edit_input_alt)
                key |= EDIT_KEY_ALT;

        edit_key(key);
}

static bool edit_input_paste_append(string_address bytes, positive length)
{
        if (!memory_resize_reserve(address_of edit_input_paste,
                       address_of edit_input_paste_room,
                       edit_input_paste_length + length, 4096))
        {
                edit_input_paste_failed = true;
                return false;
        }

        memory_copy_apart(edit_input_paste + edit_input_paste_length, bytes,
                          length);
        edit_input_paste_length += length;
        return true;
}

static fn edit_input_paste_byte(p8 byte)
{
        if (byte == edit_input_paste_end[edit_input_paste_match])
        {
                edit_input_paste_match++;

                if (edit_input_paste_match == sizeof(edit_input_paste_end))
                {
                        edit_input_pasting = false;
                        edit_input_paste_match = 0;

                        if (edit_input_paste_failed)
                                edit_status_say((string_address)"paste did not fit");
                        else if (edit_input_paste_length)
                        {
                                edit_insert(edit_input_paste,
                                            edit_input_paste_length,
                                            EDIT_STEP_OTHER);
                                edit_repaint_all();
                        }

                        edit_input_paste_length = 0;
                        edit_input_paste_failed = false;
                }

                return;
        }

        if (edit_input_paste_match)
        {
                edit_input_paste_append(edit_input_paste_end,
                                        edit_input_paste_match);
                edit_input_paste_match = 0;

                if (byte == edit_input_paste_end[0])
                {
                        edit_input_paste_match = 1;
                        return;
                }
        }

        edit_input_paste_append(address_of byte, 1);
}

/*
        One byte from the terminal.

        The escape key and the first byte of every sequence a terminal sends
        are the same byte, and nothing in the byte stream distinguishes them:
        what tells them apart is whether anything follows, and how soon. So an
        escape is held here rather than delivered, and the driver -- which is
        the only thing that knows time exists -- calls edit_input_idle when a
        read came back with nothing, which is the moment a held escape becomes
        the Escape key.
*/
static fn edit_input_byte(p8 byte)
{
        if (edit_input_pasting)
        {
                edit_input_paste_byte(byte);
                return;
        }

        switch (edit_input_state)
        {
        case EDIT_INPUT_ESCAPE:
                if (byte == '[')
                {
                        edit_input_state = EDIT_INPUT_CSI;
                        terminal_parameters_reset(address_of edit_input_csi);
                        return;
                }

                if (byte == 'O')
                {
                        edit_input_state = EDIT_INPUT_SS3;
                        return;
                }

                if (byte == 27)
                {
                        //      Two escapes in a row is the key, pressed, with
                        //      another one behind it.
                        edit_input_alt = false;
                        edit_key(EDIT_KEY_ESCAPE);
                        return;
                }

                //      Anything else after an escape is that key with Alt
                //      held, which is how every terminal has sent Alt since
                //      before there were terminals with an Alt key.
                edit_input_state = EDIT_INPUT_GROUND;
                edit_input_alt = true;
                edit_input_byte(byte);
                if (edit_input_state != EDIT_INPUT_UTF8)
                        edit_input_alt = false;
                return;

        case EDIT_INPUT_SS3:
                edit_input_state = EDIT_INPUT_GROUND;
                edit_input_deliver(edit_key_from_final(byte));
                return;

        case EDIT_INPUT_CSI:
                if (!terminal_parameters_take(address_of edit_input_csi,
                                              byte))
                        return;

                edit_input_state = EDIT_INPUT_GROUND;

                {
                        positive first = edit_input_csi.count
                                             ? edit_input_csi.value[0] : 0;
                        positive second = edit_input_csi.count > 1
                                              ? edit_input_csi.value[1]
                                              : 0;
                        positive modifiers = edit_modifiers_from(second);
                        positive key;

                        // Mouse reports are complete CSI frames here. The
                        // editor has no pointer actions yet, so consuming and
                        // discarding the frame is the whole operation.
                        if (edit_input_csi.marker == '<' &&
                            (byte == 'M' || byte == 'm'))
                                return;

                        if (byte == 'Z')
                        {
                                edit_input_deliver(EDIT_KEY_SHIFT |
                                                   EDIT_KEY_TAB);
                                return;
                        }

                        //      Two spellings of "this character, with these
                        //      modifiers": xterm's modifyOtherKeys and the
                        //      newer CSI u. Neither is asked for by this
                        //      editor, and both are decoded, because a
                        //      terminal configured to send them is the only
                        //      way Ctrl+Shift+Z is ever distinguishable from
                        //      Ctrl+Z.
                        if (byte == 'u')
                        {
                                edit_input_deliver(modifiers | first);
                                return;
                        }

                        if (byte == '~' && first == 27)
                        {
                                positive third =
                                    edit_input_csi.count > 2
                                        ? edit_input_csi.value[2]
                                        : 0;

                                edit_input_deliver(edit_modifiers_from(second) |
                                                   third);
                                return;
                        }

                        if (byte == '~')
                        {
                                if (first == 200)
                                {
                                        edit_input_pasting = true;
                                        edit_input_paste_length = 0;
                                        edit_input_paste_match = 0;
                                        edit_input_paste_failed = false;
                                        return;
                                }

                                edit_input_deliver(modifiers |
                                                   edit_key_from_tilde(first));
                                return;
                        }

                        key = edit_key_from_final(byte);

                        if (key != EDIT_KEY_NONE)
                                edit_input_deliver(modifiers | key);

                        return;
                }

        case EDIT_INPUT_UTF8:
                if (!edit_is_continuation(byte))
                {
                        //      A sequence cut short is not a character. The
                        //      byte that cut it short is still a key, so it is
                        //      read again from the ground rather than thrown
                        //      away with the sequence.
                        edit_input_state = EDIT_INPUT_GROUND;
                        edit_input_utf8.left = 0;
                        edit_input_alt = false;
                        edit_input_byte(byte);
                        return;
                }
                goto utf8;
        }

        if (byte == 27)
        {
                edit_input_state = EDIT_INPUT_ESCAPE;
                return;
        }

        if (byte == 127)
        {
                edit_input_deliver(EDIT_KEY_BACKSPACE);
                return;
        }

        if (byte < ' ')
        {
                edit_input_deliver(edit_key_from_control(byte));
                return;
        }

        if (byte < 0x80)
        {
                edit_input_deliver(byte);
                return;
        }

utf8:
        b32 result = memory_utf8_feed(address_of edit_input_utf8, byte);
        edit_input_state = result ? EDIT_INPUT_GROUND : EDIT_INPUT_UTF8;
        if (result > 0)
                edit_input_deliver(edit_input_utf8.value);
        if (result)
                edit_input_alt = false;
}

//      Nothing more arrived. A held escape was the key after all; a sequence
//      that stopped half way was noise on the line and is dropped.
static fn edit_input_idle()
{
        if (edit_input_state == EDIT_INPUT_ESCAPE)
        {
                edit_input_reset();
                edit_key(EDIT_KEY_ESCAPE);
                return;
        }

        if (edit_input_state != EDIT_INPUT_GROUND)
                edit_input_reset();
}

/*
        The file, as lines, and the file, as bytes.

        A file with no final newline is remembered as such and written back as
        such, because a text editor that adds one is a text editor that shows
        up in a diff of a file it was only used to read.
*/
/*
        A buffer with nothing in it, and nothing left over from the last one.

        edit is a builtin, so a person can run it twice in one shell, and every
        piece of state below would otherwise carry: the undo history of the
        previous file would undo edits into this one, and the modified marker
        would still be on. The clipboard deliberately does survive, which is
        how copying in one file and pasting in the next works.
*/
static bool edit_empty()
{
        while (edit_step_count)
                edit_step_forget(edit_steps + --edit_step_count);

        edit_step_at = 0;
        edit_step_saved = 0;
        edit_step_saved_known = true;
        edit_modified = false;
        edit_final_newline = true;
        edit_empty_file = true;
        edit_view_free = false;
        edit_message_length = 0;
        edit_prompt_active = false;
        edit_prompt_kind = EDIT_PROMPT_NONE;
        edit_prompt_length = 0;

        while (edit_line_count)
                edit_line_close(edit_line_count - 1);

        if (!edit_lines_room_for(1) || !edit_cursors_room_for(1))
                return false;

        edit_line_count = 1;
        edit_lines[0].text = null;
        edit_lines[0].length = 0;
        edit_lines[0].room = 0;

        edit_cursor_count = 1;
        memory_zero(edit_cursors, sizeof(struct edit_cursor));
        edit_primary_place = edit_place_clamped(0, 0);
        edit_top = 0;
        edit_left = 0;
        edit_repaint_all();
        return true;
}

/* The shell is long lived; quitting a large document must not make that
   document and its undo history part of the shell's permanent RSS.  The
   clipboard is intentionally absent so copy in one file can paste in the
   next. */
static fn edit_document_release()
{
        while (edit_step_count)
                edit_step_forget(edit_steps + --edit_step_count);

        while (edit_line_count)
                edit_line_close(edit_line_count - 1);

        memory_give(edit_steps);
        memory_give(edit_lines);
        memory_give(edit_cursors);
        memory_give(edit_input_paste);
        memory_give(edit_emitted);

        edit_steps = null;
        edit_step_at = 0;
        edit_step_room = 0;
        edit_lines = null;
        edit_line_room = 0;
        edit_cursors = null;
        edit_cursor_count = 0;
        edit_cursor_room = 0;
        edit_input_paste = null;
        edit_input_paste_length = 0;
        edit_input_paste_room = 0;
        edit_emitted = null;
        edit_emitted_length = 0;
        edit_emitted_room = 0;
}

static bool edit_load(string_address text, positive length)
{
        positive at = 0;
        positive line = 0;

        if (!edit_empty())
                return false;
        //      A file that ended without a newline is written back without
        //      one; one that had nothing in it at all is a new file and gets
        //      the newline every other one has.
        edit_final_newline = length ? text[length - 1] == '\n' : true;
        edit_empty_file = length == 0;

        while (at < length)
        {
                positive width = memory_span_without_byte(
                    (address_any)(text + at), '\n', length - at);

                if (line && !edit_line_open(line))
                        return false;

                if (!edit_line_splice(line, 0, 0, text + at, width))
                        return false;
                line++;
                at += width;

                if (at == length)
                        break;

                at++;
        }

        edit_modified = false;
        edit_step_saved = 0;
        edit_step_saved_known = true;
        return true;
}

static p8 address_to EDIT_SPARE edit_bytes_take(positive address_to length)
{
        struct edit_place from = {0, 0};
        struct edit_place to = edit_place_last();
        positive size = edit_span_length(from, to);
        p8 address_to block = (p8 address_to)memory_take(size + 2);

        if (!block)
        {
                address_to length = 0;
                return null;
        }

        //      A buffer holding nothing is a file holding nothing. Writing the
        //      newline that a buffer with text in it would end with turns an
        //      empty file into a one byte one every time it is opened.
        if (edit_line_count == 1 && !edit_lines[0].length)
        {
                positive held = edit_final_newline && !edit_empty_file ? 1 : 0;

                if (held)
                        block[0] = '\n';

                block[held] = 0;
                address_to length = held;
                return block;
        }

        edit_span_copy(from, to, block);

        if (edit_final_newline)
                block[size++] = '\n';

        block[size] = 0;
        address_to length = size;
        return block;
}

/*
        The prompt on the status line, which is the only thing in this editor
        that reads a line of its own.

        It is not a mode. Every key that is not part of answering it does what
        it always does, Escape puts it away, and nothing on the screen changes
        except the bar at the bottom. Ctrl+G uses it, quitting with unsaved
        changes uses it, and whoever adds find and replace should use it too
        rather than growing a second one.
*/
static fn edit_prompt_open(p8 kind, string_address label)
{
        edit_prompt_active = true;
        edit_prompt_kind = kind;
        edit_prompt_label = label;
        edit_prompt_length = 0;
}

static fn edit_prompt_close()
{
        edit_prompt_active = false;
        edit_prompt_kind = EDIT_PROMPT_NONE;
        edit_prompt_length = 0;
}

//      Saving is the one thing in the core that has to reach the outside, so
//      the core asks for it and the driver at the bottom does it. A build with
//      no kernel under it -- which is what the tests are -- leaves this
//      answering false, and the editor says so on the status line rather than
//      pretending the file was written.
static bool edit_write_file();

static fn edit_save()
{
        edit_step_seal();

        if (!edit_path)
        {
                edit_status_say((string_address) "no file name");
                return;
        }

        if (!edit_write_file())
        {
                edit_status_say((string_address) "could not write");
                return;
        }

        edit_modified = false;
        edit_step_saved = edit_step_at;
        edit_step_saved_known = true;
        edit_status_say((string_address) "saved");
}

static fn edit_quit(bool ask)
{
        if (ask && edit_modified)
        {
                edit_prompt_open(EDIT_PROMPT_QUIT,
                                 (string_address) "save changes? (y/n) ");
                return;
        }

        edit_running = false;
}

static fn edit_prompt_key(positive key)
{
        positive plain = key & ~EDIT_KEY_MODIFIERS;

        if (plain == EDIT_KEY_ESCAPE)
        {
                edit_prompt_close();
                return;
        }

        if (edit_prompt_kind == EDIT_PROMPT_QUIT)
        {
                if (plain == 'y' || plain == 'Y')
                {
                        edit_prompt_close();
                        edit_save();

                        if (!edit_modified)
                                edit_running = false;

                        return;
                }

                if (plain == 'n' || plain == 'N')
                {
                        edit_prompt_close();
                        edit_running = false;
                        return;
                }

                return;
        }

        if (plain == EDIT_KEY_BACKSPACE)
        {
                // A character, not a byte: one press takes the whole of
                // whatever was typed, however many bytes it was spelled in.
                if (edit_prompt_length)
                        edit_prompt_length--;

                while (edit_prompt_length &&
                       edit_is_continuation(edit_prompt_text[edit_prompt_length]))
                        edit_prompt_length--;

                return;
        }

        if (plain == EDIT_KEY_ENTER)
        {
                positive wanted = 0;

                for (positive at = 0; at < edit_prompt_length; at++)
                        if (edit_prompt_text[at] >= '0' &&
                            edit_prompt_text[at] <= '9')
                                wanted = wanted * 10 +
                                         (positive)(edit_prompt_text[at] - '0');

                edit_prompt_close();

                if (wanted)
                        edit_place_cursor(wanted - 1, 0, false);

                return;
        }

        /*
                Typed into the prompt, in the encoding the rest of the file
                uses.

                This truncated the scalar to its low byte, so a prompt was the
                one place in the editor where typing a character put a
                different one on the screen: U+4E2D arrived as 0x2D and was
                drawn as a hyphen, U+00E9 as a lone 0xE9 that is no UTF-8 at
                all and drew as the replacement glyph. Every other place a key
                becomes text goes through memory_utf8_encode; so does this one
                now, and it takes the character only when the whole of it
                fits, since half a character in the buffer is the same bug
                spelled differently.
        */
        if (plain >= ' ' && plain < 0x110000)
        {
                p8 built[4];
                positive made = memory_utf8_encode(built, sizeof(built), plain);

                if (made && edit_prompt_length <= sizeof(edit_prompt_text) - made)
                {
                        memory_copy_apart(edit_prompt_text + edit_prompt_length,
                                          built, made);
                        edit_prompt_length += made;
                }
        }
}

// Movement keys share one binding table; editing commands carry their actions.
static fn edit_key(positive key)
{
        positive plain = key & ~EDIT_KEY_MODIFIERS;
        bool shift = (key & EDIT_KEY_SHIFT) != 0;
        bool alt = (key & EDIT_KEY_ALT) != 0;
        bool control = (key & EDIT_KEY_CONTROL) != 0;

        edit_message_length = 0;

        //      A terminal that reports modified keys the long way round sends
        //      the shifted letter for Ctrl+Shift+Z, and one that reports them
        //      the other long way round sends the unshifted one. The binding
        //      is the same key either way.
        if (control && plain >= 'A' && plain <= 'Z')
                plain += 'a' - 'A';

        if (edit_prompt_active)
        {
                edit_prompt_key(key);
                return;
        }

        if (alt && !control && (plain == EDIT_KEY_UP || plain == EDIT_KEY_DOWN))
        {
                if (shift)
                        edit_copy_lines(plain == EDIT_KEY_UP);
                else
                        edit_move_lines(plain == EDIT_KEY_UP);
                return;
        }

        if (!control && plain >= EDIT_KEY_UP && plain <= EDIT_KEY_PAGE_DOWN)
        {
                static const p8 motions[] = {
                    EDIT_MOVE_UP, EDIT_MOVE_DOWN, EDIT_MOVE_RIGHT, EDIT_MOVE_LEFT,
                    EDIT_MOVE_HOME, EDIT_MOVE_END, EDIT_MOVE_PAGE_UP, EDIT_MOVE_PAGE_DOWN
                };
                edit_move(motions[plain - EDIT_KEY_UP], shift);
                if (plain >= EDIT_KEY_PAGE_UP)
                        edit_repaint_all();
                return;
        }

        if (control)
        {
                switch (plain)
                {
                case 's':
                        edit_save();
                        return;
                case 'q':
                case 'w':
                        edit_quit(true);
                        return;
                case 'z':
                case 'y':
                        //      Ctrl+Shift+Z is redo where a terminal can say
                        //      so, and Ctrl+Z is undo everywhere.
                        edit_step_move(plain == 'z' && !shift);
                        edit_primary_from(edit_cursor_count - 1);
                        edit_repaint_all();
                        return;
                case 'c':
                        edit_copy(false);
                        return;
                case 'x':
                        edit_copy(true);
                        edit_repaint_all();
                        return;
                case 'v':
                        edit_paste();
                        edit_repaint_all();
                        return;
                case 'a':
                        edit_select_all();
                        return;
                case 'l':
                        edit_select_line();
                        return;
                case 'g':
                        edit_prompt_open(EDIT_PROMPT_LINE,
                                         (string_address) "go to line: ");
                        return;
                case '/':
                        edit_toggle_comment();
                        edit_repaint_all();
                        return;
                case EDIT_KEY_LEFT:
                        edit_move(EDIT_MOVE_WORD_LEFT, shift);
                        return;
                case EDIT_KEY_RIGHT:
                        edit_move(EDIT_MOVE_WORD_RIGHT, shift);
                        return;
                case EDIT_KEY_UP:
                        //      Ctrl+Up and Ctrl+Down scroll the window and
                        //      leave the caret where it is, which is the one
                        //      pair of keys in this list that moves the view
                        //      rather than the text.
                        if (edit_top)
                                edit_top--;

                        edit_view_free = true;
                        edit_repaint_all();
                        return;
                case EDIT_KEY_DOWN:
                        if (edit_top + 1 < edit_line_count)
                                edit_top++;

                        edit_view_free = true;
                        edit_repaint_all();
                        return;
                case EDIT_KEY_HOME:
                        edit_move(EDIT_MOVE_FILE_START, shift);
                        return;
                case EDIT_KEY_END:
                        edit_move(EDIT_MOVE_FILE_END, shift);
                        return;
                case EDIT_KEY_BACKSPACE:
                        edit_delete_word_left();
                        edit_repaint_all();
                        return;
                }

                return;
        }

        switch (plain)
        {
        case EDIT_KEY_BACKSPACE:
                edit_delete_character(true);
                edit_repaint_all();
                return;
        case EDIT_KEY_REMOVE:
                edit_delete_character(false);
                edit_repaint_all();
                return;
        case EDIT_KEY_ENTER:
                edit_newline();
                edit_repaint_all();
                return;
        case EDIT_KEY_TAB:
                edit_tab(shift);
                edit_repaint_all();
                return;
        case EDIT_KEY_ESCAPE:
                //      Back to one cursor with nothing selected, which is what
                //      Escape means in an editor with no modes to leave.
                edit_step_seal();
                edit_cursors_one();
                edit_primary_from(0);
                edit_repaint_all();
                return;
        case EDIT_KEY_INSERT:
                return;
        }

        if (plain >= EDIT_KEY_NAMED)
                return;

        //      An ordinary character, which is everything that is left. There
        //      is no mode to be in and nothing to check: typing a letter types
        //      the letter.
        {
                p8 built[4];
                positive length = memory_utf8_encode(built, sizeof(built),
                                                       plain);

                if (length)
                        edit_insert(built, length, EDIT_STEP_TYPING);
        }
}

/*
        The driver.

        Everything above this line is arithmetic on arrays: it can be run with
        no kernel underneath it, which is what the edit lane of test/run does. Everything
        below it is the four things an editor cannot do without a kernel --
        reading the file, writing the file, putting the terminal into the state
        where keys arrive one at a time, and waiting for one.

        EDIT_NO_DRIVER leaves it out, which is how the harness links the core
        without linking a terminal it has not got.
*/
#ifndef EDIT_NO_DRIVER

//      The same numbers screen.c writes, under names that cannot collide with
//      them: the two files end up in one translation unit and a second
//      typedef of one anonymous struct under one name is not a duplicate, it
//      is an error.

//      c_iflag
#define EDIT_INPUT_IGNORE_BREAK 0x0001u
#define EDIT_INPUT_BREAK 0x0002u
#define EDIT_INPUT_MARK_PARITY 0x0008u
#define EDIT_INPUT_STRIP 0x0020u
#define EDIT_INPUT_NL_TO_CR 0x0040u
#define EDIT_INPUT_IGNORE_CR 0x0080u
#define EDIT_INPUT_CR_TO_NL 0x0100u
#define EDIT_INPUT_FLOW 0x0400u
//      c_oflag
#define EDIT_OUTPUT_PROCESS 0x0001u
//      c_cflag
#define EDIT_HARDWARE_SIZE 0x0030u
#define EDIT_HARDWARE_EIGHT 0x0030u
#define EDIT_HARDWARE_PARITY 0x0100u
//      c_lflag
#define EDIT_LOCAL_SIGNALS 0x0001u
#define EDIT_LOCAL_CANONICAL 0x0002u
#define EDIT_LOCAL_ECHO 0x0008u
#define EDIT_LOCAL_ECHO_NL 0x0040u
#define EDIT_LOCAL_EXTENDED 0x8000u
//      c_cc
#define EDIT_CONTROL_TIME 5
#define EDIT_CONTROL_MIN 6
static terminal_modes edit_modes_before;
static bool edit_modes_held;

/*
        Raw, and every flag in here is load bearing for a key the editor is
        supposed to have.

        ISIG off is why Ctrl+C can be copy: with it on, the line discipline
        raises SIGINT and the byte never arrives. IXON off is why Ctrl+Q can be
        quit rather than being eaten as flow control. ICRNL off is why Enter
        arrives as the byte the Enter key sends instead of as a line feed, and
        OPOST off is why a carriage return has to be written out by hand -- so
        every place that ends a line here sends both bytes. ICANON and ECHO off
        are the obvious two: the editor wants keys, not lines, and it draws
        what was typed itself.
*/
static bool edit_terminal_raw()
{
        terminal_modes modes;

        if (system_control(standard_input_descriptor, PTY_TCGETS,
                           address_of edit_modes_before) != 0)
                return false;

        modes = edit_modes_before;
        modes.arriving &= ~(EDIT_INPUT_IGNORE_BREAK | EDIT_INPUT_BREAK |
                            EDIT_INPUT_MARK_PARITY | EDIT_INPUT_STRIP |
                            EDIT_INPUT_NL_TO_CR | EDIT_INPUT_IGNORE_CR |
                            EDIT_INPUT_CR_TO_NL | EDIT_INPUT_FLOW);
        modes.leaving &= ~EDIT_OUTPUT_PROCESS;
        modes.hardware &= ~(EDIT_HARDWARE_SIZE | EDIT_HARDWARE_PARITY);
        modes.hardware |= EDIT_HARDWARE_EIGHT;
        modes.behaviour &= ~(EDIT_LOCAL_SIGNALS | EDIT_LOCAL_CANONICAL |
                             EDIT_LOCAL_ECHO | EDIT_LOCAL_ECHO_NL |
                             EDIT_LOCAL_EXTENDED);
        modes.controls[EDIT_CONTROL_MIN] = 1;
        modes.controls[EDIT_CONTROL_TIME] = 0;

        if (system_control(standard_input_descriptor, PTY_TCSETS,
                           address_of modes) != 0)
                return false;

        edit_modes_held = true;
        return true;
}

static fn edit_terminal_restore()
{
        if (!edit_modes_held)
                return;

        system_control(standard_input_descriptor, PTY_TCSETS,
                       address_of edit_modes_before);
        edit_modes_held = false;
}

static bool edit_flush()
{
        positive wrote;

        if (!edit_emitted_length)
                return true;

        wrote = system_write_all(standard_output_descriptor, edit_emitted,
                                 edit_emitted_length);

        if (wrote >= edit_emitted_length)
        {
                edit_emitted_length = 0;
                return true;
        }

        if (wrote)
        {
                memory_copy(edit_emitted, edit_emitted + wrote,
                            edit_emitted_length - wrote);
                edit_emitted_length -= wrote;
        }

        return false;
}

/*
        The file, read whole.

        Whole because an editor holds the whole file anyway, and a read that
        grows its buffer as it goes needs no size from the kernel and therefore
        works on the things that do not have one -- a pipe, a device, /proc.
*/
#define EDIT_ENOENT 2
#define EDIT_PATH_MAX 4096

/* Hold the destination namespace and, when it existed, its inode for the
   complete editing session.  A save can then prove it is replacing the file
   that was read, while a new-file save remains no-clobber even if somebody
   creates the name before Ctrl+S. */
static bipolar edit_file_directory = -1;
static bipolar edit_file_original = -1;
static bool edit_file_existed;
static file_facts edit_file_facts;
static p8 edit_file_leaf[EDIT_PATH_MAX];

static fn edit_file_release()
{
        if (edit_file_original >= 0)
                system_close(edit_file_original);
        if (edit_file_directory >= 0)
                system_close(edit_file_directory);

        edit_file_original = -1;
        edit_file_directory = -1;
        edit_file_existed = false;
        edit_file_leaf[0] = end;
}

static bipolar edit_read_file(string_address path,
                                byte_store address_to store)
{
        bipolar looked;
        bipolar result;

        edit_file_directory = file_parent_open(path, edit_file_leaf);
        if (edit_file_directory < 0)
                return edit_file_directory;

        looked = file_look_code(edit_file_directory, edit_file_leaf,
                                AT_SYMLINK_NOFOLLOW,
                                address_of edit_file_facts);
        if (looked == -EDIT_ENOENT)
                return looked;
        if (looked < 0)
        {
                edit_file_release();
                return looked;
        }

        edit_file_original = file_open_same(
            edit_file_directory, edit_file_leaf,
            address_of edit_file_facts, FILE_READ | O_NOFOLLOW);
        if (edit_file_original < 0)
        {
                result = edit_file_original;
                edit_file_release();
                return result;
        }

        edit_file_existed = true;
        result = file_store_read((positive)edit_file_original, store);
        if (result < 0)
                edit_file_release();
        return result;
}

static bool edit_write_file()
{
        bipolar handle;
        file_facts staged;
        p8 address_to block;
        positive length = 0;
        positive wrote;
        bipolar changed;
        bipolar result;

        if (edit_file_directory < 0 ||
            (edit_file_existed &&
             (edit_file_facts.mode & MODE_FORMAT) != MODE_FILE))
                return false;

        block = edit_bytes_take(address_of length);

        if (!block)
                return false;

        system_path_stage protected;
        handle = file_stage_file_open_at(
            address_of protected, edit_file_directory,
            edit_file_leaf, 0600);

        if (handle < 0)
        {
                memory_give(block);
                return false;
        }

        wrote = system_write_all((positive)handle, block, length);
        memory_give(block);

        changed = wrote != length
                      ? -1
                      : edit_file_existed
                            ? file_preserve_owner_mode(
                                  handle, protected.directory,
                                  SYSTEM_PATH_STAGE_LEAF,
                                  address_of edit_file_facts,
                                  file_replacement_mode(
                                      edit_file_facts.mode))
                            : file_change_mode_handle(
                                  handle, 0666 & ~file_umask());
        result = changed >= 0
                     ? file_look_code(handle, (string_address)"",
                                      AT_EMPTY_PATH, address_of staged)
                     : -1;
        if (result >= 0)
                result = system_call_1(syscall(fsync), (positive)handle);
        bipolar new_original = -1;
        result = file_stage_publish_protected_keep_at(
            address_of protected, edit_file_directory, edit_file_leaf,
            handle, result, !edit_file_existed,
            edit_file_existed ? address_of edit_file_facts : null,
            0, address_of new_original, false);

        if (result < 0)
                return false;

        if (edit_file_original >= 0)
                system_close(edit_file_original);
        edit_file_original = new_original;
        edit_file_facts = staged;
        edit_file_existed = true;
        return true;
}

/*
        Waiting for a key, with a limit.

        The limit is only ever reached in one situation and it is the situation
        that makes an editor's escape key work: an escape has arrived, nothing
        has followed it, and the question is whether something is about to. A
        twentieth of a second is long enough for the rest of an arrow key to
        cross a serial line and short enough that nobody notices Escape
        arriving late.

        ppoll rather than poll because arm64 has no poll, and the fifth
        argument is the size of a signal set, which the kernel checks: anything
        but eight is EINVAL.
*/
#define EDIT_EINTR 4
#define EDIT_SIGNAL_WINCH 28
#define EDIT_WAIT_ERROR -1
#define EDIT_WAIT_IDLE 0
#define EDIT_WAIT_INPUT 1
#define EDIT_WAIT_REDRAW 2
#define EDIT_SIGNAL_BLOCK 0
#define EDIT_SIGNAL_SET_MASK 2

static volatile bool edit_window_pending;
static positive edit_window_wait_mask;

static fn edit_window_changed_handler(b32 number)
{
        (void)number;
        edit_window_pending = true;
}

static bool edit_window_signal(positive address_to previous)
{
        return system_signal_install(
            EDIT_SIGNAL_WINCH, (positive)edit_window_changed_handler,
            SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART, SIGNAL_CATCH_RESTORER,
            previous);
}

static bool edit_window_block(positive address_to previous)
{
        positive blocked = (positive)1 << (EDIT_SIGNAL_WINCH - 1);

        if (system_signal_mask(EDIT_SIGNAL_BLOCK, address_of blocked,
                               previous, 8) < 0)
                return false;

        edit_window_wait_mask = address_to previous & ~blocked;
        edit_window_pending = false;
        return true;
}

static bipolar edit_wait(bool briefly)
{
        timespec limit = {0, 50000000};
        bipolar ready = descriptor_wait_readable(
            standard_input_descriptor, briefly ? address_of limit : null,
            address_of edit_window_wait_mask);

        if (ready > 0)
                return EDIT_WAIT_INPUT;

        if (!ready)
                return EDIT_WAIT_IDLE;

        if (ready == -EDIT_EINTR)
                return EDIT_WAIT_REDRAW;

        return EDIT_WAIT_ERROR;
}

// edit ---------------------------------------------------------
/*
        Open a file, edit it, save it, leave.

        The alternate buffer is entered before anything is drawn and left
        before anything is said, so a person's scrollback is exactly as it was.
        The terminal's modes are put back on every path out of here, including
        the one where the file could not be read, because a shell prompt with
        no echo and no line editing is a machine that looks broken.
*/
static b32 system_edit()
{
        bool styles = shell_styles;
        positive2 size;
        positive window_action[4] = {0, 0, 0, 0};
        positive window_mask = 0;
        b32 result = 0;

        edit_file_release();
        edit_path = program_argument_count() > 1 ? program_argument(1) : null;

        // Saving is an atomic rename. Resolve an existing symbolic link first
        // so that rename replaces its target rather than replacing the link.
        if (edit_path &&
            file_resolve(edit_path, edit_path_resolved, true))
                edit_path = edit_path_resolved;

        if (!edit_empty())
        {
                log_direct(str("edit: no memory\n"));
                edit_document_release();
                return 1;
        }
        edit_input_reset();

        if (edit_path)
        {
                byte_store loaded = {0};
                bipolar read_failure = edit_read_file(edit_path, address_of loaded);
                bool loaded_all = !read_failure && edit_load(loaded.bytes, loaded.used);
                byte_store_release(address_of loaded);

                if (!read_failure)
                {
                        if (!loaded_all)
                        {
                                log_direct(str("edit: file did not fit\n"));
                                edit_file_release();
                                edit_document_release();
                                return 1;
                        }
                }
                else if (read_failure == -EDIT_ENOENT)
                        //      A name that is not there yet is a new file, not
                        //      an error. It is the second thing anybody tries.
                        edit_status_say((string_address) "new file");
                else
                {
                        log_direct(str("edit: could not read file\n"));
                        edit_file_release();
                        edit_document_release();
                        return 1;
                }
        }

        if (!edit_terminal_raw())
        {
                log_direct(str("edit: not a terminal\n"));
                edit_file_release();
                edit_document_release();
                return 1;
        }

        if (!edit_window_block(address_of window_mask))
        {
                edit_terminal_restore();
                log_direct(str("edit: could not watch terminal size\n"));
                edit_file_release();
                edit_document_release();
                return 1;
        }

        if (!edit_window_signal(window_action))
        {
                system_signal_mask(EDIT_SIGNAL_SET_MASK, address_of window_mask, 0, 8);
                edit_terminal_restore();
                log_direct(str("edit: could not watch terminal size\n"));
                edit_file_release();
                edit_document_release();
                return 1;
        }

        shell_styles = false;
        size = term_size();
        edit_resize(size.width, size.height);
        edit_running = true;

        log_flush();
        edit_say_literal(TERM_ALT_BUFFER TERM_RESET TERM_CLEAR_SCREEN
                         "\033[?2004h");

        while (edit_running)
        {
                p8 arriving[512];
                bipolar got;
                bipolar waited;

                if (edit_window_pending)
                        edit_window_pending = false;

                size = term_size();

                if (size.width != edit_columns || size.height != edit_rows)
                        edit_resize(size.width, size.height);

                edit_draw();
                if (!edit_flush())
                {
                        result = 1;
                        break;
                }

                waited = edit_wait(edit_input_state != EDIT_INPUT_GROUND);

                if (waited == EDIT_WAIT_REDRAW)
                        continue;

                if (waited == EDIT_WAIT_IDLE)
                {
                        edit_input_idle();
                        continue;
                }

                if (waited == EDIT_WAIT_ERROR)
                        break;

                got = system_read_retry(standard_input_descriptor, arriving,
                                        sizeof(arriving));

                if (got <= 0)
                        break;

                for (bipolar at = 0; at < got; at++)
                        edit_input_byte(arriving[at]);
        }

        edit_say_literal("\033[?2004l" TERM_RESET TERM_SHOW_CURSOR
                         TERM_MAIN_BUFFER);
        if (!edit_flush())
                result = 1;
        system_signal_action(EDIT_SIGNAL_WINCH, window_action, 0, 8);
        system_signal_mask(EDIT_SIGNAL_SET_MASK, address_of window_mask, 0, 8);
        edit_terminal_restore();
        shell_styles = styles;
        edit_file_release();
        edit_document_release();
        return result;
}

#else

//      With no driver there is nowhere for a save to go, so it answers the way
//      a failed one does and the status line says so. That is what the tests
//      link against, and it is honest: nothing was written.
static bool edit_write_file()
{
        return false;
}

#endif // EDIT_NO_DRIVER

#endif // KERNEL_MODE / STANDARD_NO_PLATFORM
#endif // SHELL_EDIT_INCLUDED
