/*
        Text: what a field, a text area and a code editor are made of, below
        any pixel. A document is bytes, a history of what changed them, and
        the selections over them; nothing here draws, reads a file or makes a
        system call of its own (memory is the library's), so the check that
        holds it to a naive model runs it as it is, on every machine.

        A file of its own rather than a part of ui.c, which is the look: the
        engine needs no font, no window and no table, so the check that
        drives it through a million random edits builds it with the library
        and nothing else of the desktop, and the look's file stays the
        look's. ui.c includes it, so it is the same translation unit as
        everything that uses it.

        THE BUFFER is a piece tree: the file as it was opened (mapped or
        read, never copied, never changed) and what was typed or pasted
        since (one buffer that only grows), and a balanced tree of pieces
        of the two in document order. Each node keeps the sums of its
        subtree: its bytes, its line feeds, its carriage returns and the
        CR LF pairs among them, and whether it starts with a LF and ends
        with a CR, so two subtrees side by side know whether a pair spans
        them. A line ends at a LF, at a CR alone, or at a CR LF, which is one
        line end and not two; the lines a subtree ends are its LFs and CRs
        less its pairs, a sum that adds up from the halves. So every
        question a view asks -- how long, how many lines, where line k
        starts, which line an offset is on -- is one walk down the tree, and
        an insert or a delete is a split and a join: O(log n) in the pieces,
        whatever the file's size. A piece is at most DOC_PIECE_MOST bytes,
        so the one scan a question ends in (the k-th line end inside a
        piece) is bounded too; the file is cut into pieces that long when it
        is opened, counted once with memory_count.

        The tree is a treap: a node's priority is random and not below its
        children's, so it is balanced with no rotations to write, and split
        and join are the whole of it.

        Bytes are never changed on the way in or out. A byte that is not
        UTF-8 is kept as it was and is a character of its own to the caret
        (U+FFFD is what a view draws for it); a file's line ends stay what
        they were, line by line, and Enter types the file's own; a byte
        order mark is held aside and written back; the last line end is
        there or not as it was.

        THE HISTORY is groups of changes. A change is a replace: at an
        offset, the pieces it took out and the pieces it put in, and since a
        piece names bytes of buffers that never change, undo and redo move
        pieces and copy no text. A group is what one undo takes back: one
        keystroke at every caret, a run of typing, a paste, a replace of
        every match. It keeps the selections from before and after, so undo
        puts the carets back where they were. Typing joins the group before
        it while the carets are where that group left them, the kind is the
        same (typing, deleting back, deleting forward), it is under a second
        since, and a word does not begin after a blank (so "hello world" is
        two undos, as in VS Code). Redo is dropped by the next change: one
        line of history, not a tree, which is what every editor a person
        already knows does. It is bounded: past DOC_HISTORY_GROUPS groups,
        or the pieces, changes and snapshots it holds past their limits, the
        oldest quarter is forgotten.

        THE SELECTIONS are a list, each an anchor and an active end and the
        column the caret keeps going up and down; one is the primary. After
        anything moves them they are sorted and the ones that overlap are
        joined. An edit of every caret is one list of replaces, applied from
        the last to the first so no offset before one moves, and the carets
        after it are worked out from the sums of what came before each.

        THE CHARACTERS the caret steps over are extended grapheme clusters
        (UAX #29, Unicode 16, rules GB1-GB999 with GB9c), from a table of the
        break properties made from the Unicode data files by differential.py's
        text_tables harness and held to the official test files. Word
        boundaries are UAX #29's too (WB1-WB999), cut again at the separators
        a person can set (VS Code's list by default), and a word jump goes
        over runs of one kind: a word, separators, blanks. Display width is
        unicode_width's, the terminal's: a tab to the next stop, a control
        picture for a control, U+FFFD for a byte that is not UTF-8.

        Backspace takes back the last code point of a letter and its marks (e
        and an acute accent typed as two is e again), and a whole emoji
        sequence, a flag, a keycap, a Hangul syllable of jamo or a CR LF;
        Delete takes a whole cluster. That is what macOS and Windows do.

        Bidirectional text is stored and stepped in logical order and drawn
        left to right in that order, not reordered: a right to left run reads
        backwards on the screen. That is a known limit.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_TEXT
#define MOONWATER_CANVAS_TEXT

#define DOC_PIECE_MOST 16384u
#define DOC_ORIGINAL 0
#define DOC_ADDED 1
#define DOC_INVALID 0x80000000u // a byte that is not UTF-8, as a code point: the byte in its low bits
#define DOC_EOL_LF 0
#define DOC_EOL_CRLF 1
#define DOC_EOL_CR 2
#define DOC_STACK 192 // the deepest walk kept: a treap of a billion pieces is far under it
#define DOC_HISTORY_GROUPS 20000u
#define DOC_HISTORY_PIECES (1u << 20)
#define DOC_HISTORY_CHANGES (1u << 19)
#define DOC_HISTORY_SNAPSHOTS (1u << 20)
#define DOC_PAUSE_NS 1000000000ull
#define DOC_NONE ((p64)-1)
#define DOC_EXPAND 16

// The kinds of group, for joining: typing, deleting back, deleting forward, anything else (never joined).
#define DOC_KIND_OTHER 0
#define DOC_KIND_TYPING 1
#define DOC_KIND_BACK 2
#define DOC_KIND_FORWARD 3

// A node's edge bits: its first byte is a LF, its last a CR.
#define DOC_FIRST_LF 1u
#define DOC_LAST_CR 2u

struct doc_sums
{
        p64 bytes, lf, cr, pairs;
        p8 edge;
};

struct doc_node
{
        p32 left, right, priority, length;
        p64 start;
        p32 lf, cr, pairs;
        p8 buffer, edge;
        struct doc_sums sum;
};

// A piece as the history keeps it.
struct doc_piece
{
        p64 start;
        p32 length;
        p8 buffer;
};

struct doc_change
{
        p64 at, removed, inserted;
        p32 removed_first, removed_count, inserted_first, inserted_count;
};

struct doc_selection
{
        p64 anchor, active;
        b64 goal; // the column or the x the caret keeps going up and down, -1 for none
};

struct doc_group
{
        p32 change_first, change_count;
        p32 before_first, before_count, after_first, after_count;
        p32 before_primary, after_primary;
        p8 kind;
        p8 last; // the last character typed into it, for the word rule
        positive time;
};

// A range that keeps its place through edits: a decoration, a read-only span, a fold, a mark.
struct doc_range
{
        p64 start, finish;
        p32 kind, data;
};

struct doc
{
        const p8 address_to original;
        p64 original_length;
        p8 address_to added;
        positive added_room, added_used;

        struct doc_node address_to nodes;
        positive node_room, node_used;
        p32 root, free_list;
        p64 random;

        p8 bom, eol;
        p32 tab_size;
        b32 insert_spaces, overwrite, auto_close;
        b32 normalize_paste; // a paste's line ends become the file's
        b32 single_line;     // a field: a line end typed or pasted is a blank
        const char address_to separators;

        // The history.
        struct doc_group address_to groups;
        positive group_room, group_count;
        struct doc_change address_to changes;
        positive change_room, change_count;
        struct doc_piece address_to pieces;
        positive piece_room, piece_count;
        struct doc_selection address_to snapshots;
        positive snapshot_room, snapshot_count;
        p32 done;         // the groups applied: those past it are redo
        b32 open;         // the last group may be joined
        p32 depth;        // doc_group_begin's nesting
        b32 depth_opened; // the outer begin made a group, not joined one
        b64 saved;        // done when last saved, -1 when that state is gone
        b32 recording;    // false while undo or redo replays

        // The selections, and what select-next and expand remember.
        struct doc_selection address_to selections;
        positive selection_room, selection_count;
        p32 primary;
        b32 occurrence_whole; // select-next began on a caret: it finds whole words
        struct doc_selection expanded[DOC_EXPAND], expand_now;
        p32 expand_count;
        p64 expand_version;

        // Ranges that move with the text.
        struct doc_range address_to ranges;
        positive range_room, range_count;

        // What changed: a count, the lowest offset and line since a view last looked.
        p64 version, changed_from, changed_line;
        positive now; // the caller's clock, for joining typing
        // Closers auto_close typed, where they are now: typing the same closer there steps over it.
        p64 closers[8];
        p32 closer_count;

        // The tokenizer's state at the start of each line, in a gap buffer, and how far it is known.
        p32 address_to states;
        positive state_room, gap_start, gap_end;
        p64 states_valid, states_converge, states_last_edit;
        p32 language;
};

static p64 doc_breaks(const struct doc_sums address_to sums)
{
        return sums->lf + sums->cr - sums->pairs;
}

/* ---- UTF-8, one code point at a time ---- */

// One code point from bytes, available of them: its length, its value in code. A byte that starts no valid
// sequence (a stray continuation, an overlong form, a surrogate, past U+10FFFF, cut short) is one byte long and
// its value is DOC_INVALID with the byte in it.
static p32 doc_decode(const p8 address_to bytes, positive available, p32 address_to code)
{
        p32 lead = bytes[0], length, value;

        if (lead < 0x80)
        {
                *code = lead;
                return 1;
        }
        if (lead < 0xc2 || lead > 0xf4)
                goto invalid;
        length = lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (available < length)
                goto invalid;
        value = lead & (0x7fu >> length);
        for (p32 at = 1; at < length; at++)
        {
                if ((bytes[at] & 0xc0) != 0x80)
                        goto invalid;
                value = value << 6 | (bytes[at] & 0x3f);
        }
        if ((length == 3 && (value < 0x800 || (value >= 0xd800 && value <= 0xdfff))) ||
            (length == 4 && (value < 0x10000 || value > 0x10ffff)))
                goto invalid;
        *code = value;
        return length;
invalid:
        *code = DOC_INVALID | lead;
        return 1;
}

// The code point that ends at bytes + count (count bytes before it readable): its length and value, read as
// doc_decode reads forward, so a step back lands where a step forward did.
static p32 doc_decode_back(const p8 address_to bytes, positive count, p32 address_to code)
{
        for (p32 back = 1; back <= 4 && back <= count; back++)
        {
                if ((bytes[count - back] & 0xc0) == 0x80 && back < 4)
                        continue;
                if (doc_decode(bytes + count - back, back, code) == back)
                        return back;
                break;
        }
        return doc_decode(bytes + count - 1, 1, code);
}

/* ---- break properties ---- */

// Grapheme_Cluster_Break, in the properties' low four bits.
enum
{
        GCB_OTHER, GCB_CR, GCB_LF, GCB_CONTROL, GCB_EXTEND, GCB_ZWJ, GCB_RI, GCB_PREPEND, GCB_SPACING,
        GCB_L, GCB_V, GCB_T, GCB_LV, GCB_LVT
};
// Word_Break, in bits 4 to 8.
enum
{
        WB_OTHER, WB_CR, WB_LF, WB_NEWLINE, WB_EXTEND, WB_ZWJ, WB_RI, WB_FORMAT, WB_KATAKANA, WB_HEBREW, WB_ALETTER,
        WB_SINGLE_QUOTE, WB_DOUBLE_QUOTE, WB_MIDNUMLET, WB_MIDLETTER, WB_MIDNUM, WB_NUMERIC, WB_EXTENDNUMLET, WB_WSEGSPACE
};
// Indic_Conjunct_Break in bits 9 and 10, Extended_Pictographic in bit 11.
#define INCB_LINKER 1
#define INCB_CONSONANT 2
#define INCB_EXTEND 3
#define DOC_GCB(props) ((p32)(props) & 15u)
#define DOC_WB(props) ((p32)(props) >> 4 & 31u)
#define DOC_INCB(props) ((p32)(props) >> 9 & 3u)
#define DOC_PICTOGRAPHIC(props) ((p32)(props) >> 11 & 1u)

static const p32 doc_break_range_count;
static const p16 doc_break_combos[];
static const p32 doc_break_ranges[];

// The break properties of a code point. A byte that is not UTF-8 is a control: a cluster and a word of its own.
static p16 doc_props(p32 code)
{
        p32 low = 0, high;

        if (code & DOC_INVALID)
                return GCB_CONTROL;
        high = doc_break_range_count;
        while (high - low > 1)
        {
                p32 middle = (low + high) / 2;

                if (doc_break_ranges[middle] >> 8 <= code)
                        low = middle;
                else
                        high = middle;
        }
        return doc_break_combos[doc_break_ranges[low] & 255];
}

/*
        Grapheme clusters, forward: a state that has seen the code points of the cluster so far, asked whether
        there is a break before the next. ri counts the regional indicators in a row; pictograph is 1 after an
        Extended_Pictographic and its Extends, 2 after a ZWJ that follows them (GB11); conjunct is 1 after an
        InCB Consonant and its Extends and Linkers, 2 once a Linker is among them (GB9c).
*/
struct doc_gstate
{
        p8 previous, ri, pictograph, conjunct;
        b32 started;
};

static b32 doc_grapheme_step(struct doc_gstate address_to state, p16 props)
{
        p32 now = DOC_GCB(props), before = state->previous, incb = DOC_INCB(props);
        b32 cut;

        if (!state->started)
                cut = true; // GB1
        else if (before == GCB_CR && now == GCB_LF)
                cut = false; // GB3
        else if (before == GCB_CR || before == GCB_LF || before == GCB_CONTROL)
                cut = true; // GB4
        else if (now == GCB_CR || now == GCB_LF || now == GCB_CONTROL)
                cut = true; // GB5
        else if (before == GCB_L && (now == GCB_L || now == GCB_V || now == GCB_LV || now == GCB_LVT))
                cut = false; // GB6
        else if ((before == GCB_LV || before == GCB_V) && (now == GCB_V || now == GCB_T))
                cut = false; // GB7
        else if ((before == GCB_LVT || before == GCB_T) && now == GCB_T)
                cut = false; // GB8
        else if (now == GCB_EXTEND || now == GCB_ZWJ || now == GCB_SPACING || before == GCB_PREPEND)
                cut = false; // GB9, GB9a, GB9b
        else if (incb == INCB_CONSONANT && state->conjunct == 2)
                cut = false; // GB9c
        else if (DOC_PICTOGRAPHIC(props) && state->pictograph == 2)
                cut = false; // GB11
        else if (before == GCB_RI && now == GCB_RI && (state->ri & 1))
                cut = false; // GB12, GB13
        else
                cut = true; // GB999

        state->started = true;
        state->ri = (p8)(now == GCB_RI ? (before == GCB_RI && !cut ? state->ri + 1 : 1) : 0);
        if (DOC_PICTOGRAPHIC(props))
                state->pictograph = 1;
        else if (state->pictograph == 1 && now == GCB_ZWJ)
                state->pictograph = 2;
        else if (!(state->pictograph == 1 && now == GCB_EXTEND))
                state->pictograph = 0;
        if (incb == INCB_CONSONANT)
                state->conjunct = 1;
        else if (state->conjunct && (incb == INCB_EXTEND || incb == INCB_LINKER))
                state->conjunct = incb == INCB_LINKER ? 2 : state->conjunct;
        else
                state->conjunct = 0;
        state->previous = (p8)now;
        return cut;
}

/*
        Word boundaries of a run of code points (UAX #29, WB1-WB999) from their properties: breaks[i] is 1 when
        there is a boundary before props[i], and breaks[0] and breaks[count] are 1. Extend, Format and ZWJ hang on
        what is before them (WB4) -- but not on a line end or the start, after which one stands alone -- so the
        rules that look past a character read the ones that are not those.
*/
static b32 doc_wb_ignored(p32 wb)
{
        return wb == WB_EXTEND || wb == WB_FORMAT || wb == WB_ZWJ;
}

static b32 doc_wb_newline(p32 wb)
{
        return wb == WB_CR || wb == WB_LF || wb == WB_NEWLINE;
}

static b32 doc_wb_letter(p32 wb)
{
        return wb == WB_ALETTER || wb == WB_HEBREW;
}

static b32 doc_wb_mid_letter(p32 wb)
{
        return wb == WB_MIDLETTER || wb == WB_MIDNUMLET || wb == WB_SINGLE_QUOTE;
}

static b32 doc_wb_mid_number(p32 wb)
{
        return wb == WB_MIDNUM || wb == WB_MIDNUMLET || wb == WB_SINGLE_QUOTE;
}

// The character the rules see before i: back over the ignored, but not past the one after a line end or the start.
static positive doc_wb_before(const p16 address_to props, positive i)
{
        positive b = i - 1;

        while (b > 0 && doc_wb_ignored(DOC_WB(props[b])) && !doc_wb_newline(DOC_WB(props[b - 1])))
                b--;
        return b;
}

static fn doc_word_breaks(const p16 address_to props, positive count, p8 address_to breaks)
{
        breaks[0] = 1;
        if (count)
                breaks[count] = 1;
        for (positive i = 1; i < count; i++)
        {
                p32 now = DOC_WB(props[i]), raw = DOC_WB(props[i - 1]), before, before_before = WB_OTHER, after = WB_OTHER;
                positive b;
                b32 cut = true;

                if (raw == WB_CR && now == WB_LF)
                        cut = false; // WB3
                else if (doc_wb_newline(raw))
                        cut = true; // WB3a
                else if (doc_wb_newline(now))
                        cut = true; // WB3b
                else if (raw == WB_ZWJ && DOC_PICTOGRAPHIC(props[i]))
                        cut = false; // WB3c
                else if (raw == WB_WSEGSPACE && now == WB_WSEGSPACE)
                        cut = false; // WB3d
                else if (doc_wb_ignored(now))
                        cut = false; // WB4
                else
                {
                        b = doc_wb_before(props, i);
                        before = DOC_WB(props[b]);
                        if (b > 0)
                                before_before = DOC_WB(props[doc_wb_before(props, b)]);
                        for (positive a = i + 1; a < count; a++)
                                if (!doc_wb_ignored(DOC_WB(props[a])))
                                {
                                        after = DOC_WB(props[a]);
                                        break;
                                }
                        if (doc_wb_letter(before) && doc_wb_letter(now))
                                cut = false; // WB5
                        else if (doc_wb_letter(before) && doc_wb_mid_letter(now) && doc_wb_letter(after))
                                cut = false; // WB6
                        else if (b > 0 && doc_wb_letter(before_before) && doc_wb_mid_letter(before) && doc_wb_letter(now))
                                cut = false; // WB7
                        else if (before == WB_HEBREW && now == WB_SINGLE_QUOTE)
                                cut = false; // WB7a
                        else if (before == WB_HEBREW && now == WB_DOUBLE_QUOTE && after == WB_HEBREW)
                                cut = false; // WB7b
                        else if (b > 0 && before_before == WB_HEBREW && before == WB_DOUBLE_QUOTE && now == WB_HEBREW)
                                cut = false; // WB7c
                        else if (before == WB_NUMERIC && now == WB_NUMERIC)
                                cut = false; // WB8
                        else if (doc_wb_letter(before) && now == WB_NUMERIC)
                                cut = false; // WB9
                        else if (before == WB_NUMERIC && doc_wb_letter(now))
                                cut = false; // WB10
                        else if (b > 0 && before_before == WB_NUMERIC && doc_wb_mid_number(before) && now == WB_NUMERIC)
                                cut = false; // WB11
                        else if (before == WB_NUMERIC && doc_wb_mid_number(now) && after == WB_NUMERIC)
                                cut = false; // WB12
                        else if (before == WB_KATAKANA && now == WB_KATAKANA)
                                cut = false; // WB13
                        else if ((doc_wb_letter(before) || before == WB_NUMERIC || before == WB_KATAKANA ||
                                  before == WB_EXTENDNUMLET) &&
                                 now == WB_EXTENDNUMLET)
                                cut = false; // WB13a
                        else if (before == WB_EXTENDNUMLET && (doc_wb_letter(now) || now == WB_NUMERIC || now == WB_KATAKANA))
                                cut = false; // WB13b
                        else if (before == WB_RI && now == WB_RI)
                        {
                                // WB15, WB16: the regional indicators before, in a row, the ignored not counting.
                                positive run = 0;

                                for (positive k = b + 1; k-- > 0;)
                                {
                                        p32 wb = DOC_WB(props[k]);

                                        if (wb == WB_RI)
                                                run++;
                                        else if (!doc_wb_ignored(wb))
                                                break;
                                }
                                cut = !(run & 1);
                        }
                }
                breaks[i] = (p8)cut;
        }
}

/* ---- the piece tree ---- */

static const p8 address_to doc_buffer(const struct doc address_to doc, p8 buffer)
{
        return buffer == DOC_ORIGINAL ? doc->original : doc->added;
}

static const p8 address_to doc_piece_bytes(const struct doc address_to doc, const struct doc_node address_to node)
{
        return doc_buffer(doc, node->buffer) + node->start;
}

// The line ends of bytes, as a piece counts them.
static fn doc_count(const p8 address_to bytes, p32 length, p32 address_to lf, p32 address_to cr, p32 address_to pairs,
                    p8 address_to edge)
{
        p32 found = 0;

        *lf = length ? (p32)memory_count((address_any)bytes, length, '\n') : 0;
        *cr = length ? (p32)memory_count((address_any)bytes, length, '\r') : 0;
        if (*cr && *lf)
        {
                const p8 address_to at = bytes, address_to stop = bytes + length - 1;

                while (at < stop)
                {
                        const p8 address_to next = memory_first_of((address_any)at, '\r', (positive)(stop - at));

                        if (!next)
                                break;
                        found += next[1] == '\n';
                        at = next + 1;
                }
        }
        *pairs = found;
        *edge = (p8)((length && bytes[0] == '\n' ? DOC_FIRST_LF : 0) | (length && bytes[length - 1] == '\r' ? DOC_LAST_CR : 0));
}

static p64 doc_rand(struct doc address_to doc)
{
        doc->random ^= doc->random << 13;
        doc->random ^= doc->random >> 7;
        doc->random ^= doc->random << 17;
        return doc->random;
}

// A node for a piece, its counts made, or 0 when there is no memory for one.
static p32 doc_node_new(struct doc address_to doc, p8 buffer, p64 start, p32 length)
{
        p32 index;
        struct doc_node address_to node;

        if (doc->free_list)
        {
                index = doc->free_list;
                doc->free_list = doc->nodes[index].left;
        }
        else
        {
                // Node 0 is no node: the first one made is 1.
                if (!array_store_reserve(doc->nodes, doc->node_room, doc->node_used, (doc->node_used ? doc->node_used : 1) + 1, 1024))
                        return 0;
                if (!doc->node_used)
                        doc->node_used = 1;
                index = (p32)doc->node_used++;
        }
        node = &doc->nodes[index];
        node->left = node->right = 0;
        node->priority = (p32)(doc_rand(doc) >> 16);
        node->buffer = buffer;
        node->start = start;
        node->length = length;
        doc_count(doc_buffer(doc, buffer) + start, length, &node->lf, &node->cr, &node->pairs, &node->edge);
        node->sum = (struct doc_sums){length, node->lf, node->cr, node->pairs, node->edge};
        return index;
}

static fn doc_node_free(struct doc address_to doc, p32 index)
{
        doc->nodes[index].left = doc->free_list;
        doc->free_list = index;
}

// A node's sums from its children's and its own.
static fn doc_update(struct doc address_to doc, p32 index)
{
        struct doc_node address_to node = &doc->nodes[index];
        struct doc_sums sum = {node->length, node->lf, node->cr, node->pairs, node->edge};

        if (node->left)
        {
                const struct doc_sums address_to left = &doc->nodes[node->left].sum;

                sum.pairs += left->pairs + ((left->edge & DOC_LAST_CR) && (node->edge & DOC_FIRST_LF));
                sum.bytes += left->bytes, sum.lf += left->lf, sum.cr += left->cr;
                sum.edge = (p8)((left->edge & DOC_FIRST_LF) | (sum.edge & DOC_LAST_CR));
        }
        if (node->right)
        {
                const struct doc_sums address_to right = &doc->nodes[node->right].sum;

                sum.pairs += right->pairs + ((node->edge & DOC_LAST_CR) && (right->edge & DOC_FIRST_LF));
                sum.bytes += right->bytes, sum.lf += right->lf, sum.cr += right->cr;
                sum.edge = (p8)((sum.edge & DOC_FIRST_LF) | (right->edge & DOC_LAST_CR));
        }
        node->sum = sum;
}

static p64 doc_bytes_of(const struct doc address_to doc, p32 index)
{
        return index ? doc->nodes[index].sum.bytes : 0;
}

/*
        A tree cut in two: its first at bytes in left, the rest in right. A cut inside a piece makes the piece
        two, the second taking the first's priority so the heap stays one. False when there was no memory for the
        node that makes (left and right then still join back into the whole).
*/
static b32 doc_split(struct doc address_to doc, p32 tree, p64 at, p32 address_to left, p32 address_to right)
{
        p64 before;
        p32 length;

        if (!tree)
        {
                *left = *right = 0;
                return true;
        }
        before = doc_bytes_of(doc, doc->nodes[tree].left);
        length = doc->nodes[tree].length;
        if (at <= before)
        {
                p32 inner_left, inner_right;
                b32 made = doc_split(doc, doc->nodes[tree].left, at, &inner_left, &inner_right);

                doc->nodes[tree].left = inner_right;
                doc_update(doc, tree);
                *left = inner_left;
                *right = tree;
                return made;
        }
        if (at >= before + length)
        {
                p32 inner_left, inner_right;
                b32 made = doc_split(doc, doc->nodes[tree].right, at - before - length, &inner_left, &inner_right);

                doc->nodes[tree].right = inner_left;
                doc_update(doc, tree);
                *left = tree;
                *right = inner_right;
                return made;
        }
        {
                p32 cut = (p32)(at - before), tail;
                struct doc_node whole = doc->nodes[tree];
                struct doc_node address_to node;

                tail = doc_node_new(doc, whole.buffer, whole.start + cut, whole.length - cut);
                if (!tail)
                {
                        *left = tree;
                        *right = 0;
                        return false;
                }
                node = &doc->nodes[tree];
                node->length = cut;
                doc_count(doc_buffer(doc, whole.buffer) + whole.start, cut, &node->lf, &node->cr, &node->pairs, &node->edge);
                doc->nodes[tail].priority = whole.priority;
                doc->nodes[tail].right = whole.right;
                node->right = 0;
                doc_update(doc, tree);
                doc_update(doc, tail);
                *left = tree;
                *right = tail;
                return true;
        }
}

static p32 doc_join(struct doc address_to doc, p32 left, p32 right)
{
        if (!left || !right)
                return left ? left : right;
        if (doc->nodes[left].priority >= doc->nodes[right].priority)
        {
                p32 joined = doc_join(doc, doc->nodes[left].right, right);

                doc->nodes[left].right = joined;
                doc_update(doc, left);
                return left;
        }
        {
                p32 joined = doc_join(doc, left, doc->nodes[right].left);

                doc->nodes[right].left = joined;
                doc_update(doc, right);
                return right;
        }
}

// A tree's pieces in order onto the history's pieces (when keep), its nodes freed. False when there was no room.
static b32 doc_collect(struct doc address_to doc, p32 tree, b32 keep)
{
        p32 stack[DOC_STACK], depth = 0;
        b32 kept = true;

        while (tree || depth)
        {
                while (tree && depth < DOC_STACK)
                {
                        stack[depth++] = tree;
                        tree = doc->nodes[tree].left;
                }
                tree = stack[--depth];
                if (keep && kept &&
                    array_store_reserve(doc->pieces, doc->piece_room, doc->piece_count, doc->piece_count + 1, 1024))
                        doc->pieces[doc->piece_count++] =
                            (struct doc_piece){doc->nodes[tree].start, doc->nodes[tree].length, doc->nodes[tree].buffer};
                else if (keep)
                        kept = false;
                {
                        p32 right = doc->nodes[tree].right;

                        doc_node_free(doc, tree);
                        tree = right;
                }
        }
        return kept;
}

// Pieces as a tree of their own, in order: 0 for none, and *failed for no memory.
static p32 doc_build(struct doc address_to doc, const struct doc_piece address_to pieces, p32 count, b32 address_to failed)
{
        p32 tree = 0;

        for (p32 at = 0; at < count; at++)
        {
                p32 node = doc_node_new(doc, pieces[at].buffer, pieces[at].start, pieces[at].length);

                if (!node)
                {
                        *failed = true;
                        return tree;
                }
                tree = doc_join(doc, tree, node);
        }
        return tree;
}

/* ---- reading ---- */

static p64 doc_length(const struct doc address_to doc)
{
        return doc_bytes_of(doc, doc->root);
}

static p64 doc_line_count(const struct doc address_to doc)
{
        return doc->root ? doc_breaks(&doc->nodes[doc->root].sum) + 1 : 1;
}

// count bytes from at into into: how many there were. A walk down to the piece at at, kept on a stack, and then in
// order through the pieces after it.
static positive doc_read(const struct doc address_to doc, p64 at, p8 address_to into, positive count)
{
        p32 stack[DOC_STACK], depth = 0, tree = doc->root;
        p64 offset = at;
        positive done = 0;

        while (tree)
        {
                const struct doc_node address_to node = &doc->nodes[tree];
                p64 before = doc_bytes_of(doc, node->left);

                if (offset < before)
                {
                        if (depth == DOC_STACK)
                                return 0;
                        stack[depth++] = tree;
                        tree = node->left;
                }
                else if (offset < before + node->length)
                {
                        offset -= before;
                        break;
                }
                else
                {
                        offset -= before + node->length;
                        tree = node->right;
                }
        }
        while (tree && done < count)
        {
                const struct doc_node address_to node = &doc->nodes[tree];
                positive take = node->length - offset;

                take = take < count - done ? take : count - done;
                memory_copy(into + done, doc_piece_bytes(doc, node) + offset, take);
                done += take;
                offset = 0;
                if (node->right)
                {
                        tree = node->right;
                        while (doc->nodes[tree].left && depth < DOC_STACK)
                        {
                                stack[depth++] = tree;
                                tree = doc->nodes[tree].left;
                        }
                }
                else
                        tree = depth ? stack[--depth] : 0;
        }
        return done;
}

// The byte at at, or 0x100 past the end.
static p32 doc_byte(const struct doc address_to doc, p64 at)
{
        p8 byte = 0;

        return doc_read(doc, at, &byte, 1) ? byte : 0x100;
}

// The k-th line end inside a piece (k from 1), the byte after the piece a LF when next_lf: the offset just past it.
static p64 doc_piece_break(const struct doc address_to doc, const struct doc_node address_to node, p64 k, b32 next_lf)
{
        const p8 address_to bytes = doc_piece_bytes(doc, node);

        // Every CR paired with its LF: the line ends are the LFs.
        if (node->cr == node->pairs)
                return memory_nth_of((address_any)bytes, '\n', node->length, k).x;
        for (p32 at = 0; at < node->length; at++)
        {
                b32 finish = bytes[at] == '\n' ||
                          (bytes[at] == '\r' && !(at + 1 < node->length ? bytes[at + 1] == '\n' : next_lf));

                if (finish && !--k)
                        return at + 1;
        }
        return node->length;
}

// Just past the k-th line end of the document (k from 1).
static p64 doc_find_break(const struct doc address_to doc, p64 k)
{
        p32 tree = doc->root;
        p64 offset = 0;
        b32 next_lf = false;

        while (tree)
        {
                const struct doc_node address_to node = &doc->nodes[tree];
                const struct doc_node address_to left = node->left ? &doc->nodes[node->left] : null;
                b32 after_node = node->right ? (doc->nodes[node->right].sum.edge & DOC_FIRST_LF) != 0 : next_lf;
                p64 in_left = left ? doc_breaks(&left->sum) - ((left->sum.edge & DOC_LAST_CR) && (node->edge & DOC_FIRST_LF)) : 0;
                p64 in_node = (p64)node->lf + node->cr - node->pairs - ((node->edge & DOC_LAST_CR) && after_node);

                if (k <= in_left)
                {
                        next_lf = (node->edge & DOC_FIRST_LF) != 0;
                        tree = node->left;
                        continue;
                }
                k -= in_left;
                offset += left ? left->sum.bytes : 0;
                if (k <= in_node)
                        return offset + doc_piece_break(doc, node, k, after_node);
                k -= in_node;
                offset += node->length;
                tree = node->right;
        }
        return offset;
}

// Where line starts (from 0); the end of the document for a line past the last.
static p64 doc_line_start(const struct doc address_to doc, p64 line)
{
        if (!line)
                return 0;
        if (line >= doc_line_count(doc))
                return doc_length(doc);
        return doc_find_break(doc, line);
}

// The line ends before at: which line at is on. A place between a CR and its LF is on the CR's line.
static p64 doc_line_of(const struct doc address_to doc, p64 at)
{
        p32 tree = doc->root;
        p64 lines = 0;
        b32 next_lf = false;

        while (tree)
        {
                const struct doc_node address_to node = &doc->nodes[tree];
                const struct doc_node address_to left = node->left ? &doc->nodes[node->left] : null;
                p64 before = left ? left->sum.bytes : 0;
                b32 after_node = node->right ? (doc->nodes[node->right].sum.edge & DOC_FIRST_LF) != 0 : next_lf;

                if (at < before)
                {
                        next_lf = (node->edge & DOC_FIRST_LF) != 0;
                        tree = node->left;
                        continue;
                }
                if (left)
                        lines += doc_breaks(&left->sum) - ((left->sum.edge & DOC_LAST_CR) && (node->edge & DOC_FIRST_LF));
                at -= before;
                if (at < node->length)
                {
                        const p8 address_to bytes = doc_piece_bytes(doc, node);

                        if (!at)
                                return lines;
                        lines += memory_count((address_any)bytes, at, '\n');
                        // Every CR paired: none of them ends a line (the one at at - 1 included, whose LF is at).
                        if (node->cr != node->pairs)
                                for (p64 i = 0; i < at; i++)
                                        lines += bytes[i] == '\r' && bytes[i + 1] != '\n';
                        return lines;
                }
                lines += (p64)node->lf + node->cr - node->pairs - ((node->edge & DOC_LAST_CR) && after_node);
                at -= node->length;
                tree = node->right;
        }
        return lines;
}

// Where line ends, before its line end.
static p64 doc_line_end(const struct doc address_to doc, p64 line)
{
        p64 start = doc_line_start(doc, line), next;

        if (line + 1 >= doc_line_count(doc))
                return doc_length(doc);
        next = doc_line_start(doc, line + 1);
        if (next > start && doc_byte(doc, next - 1) == '\n')
                next--;
        if (next > start && doc_byte(doc, next - 1) == '\r')
                next--;
        return next;
}

/*
        A window over the text for walking code points: bytes copied from the tree a block at a time, so a walk reads
        the tree once a block and not once a character.
*/
#define DOC_WINDOW 512

struct doc_reader
{
        const struct doc address_to doc;
        p64 from;
        p32 count;
        p8 bytes[DOC_WINDOW];
};

static fn doc_reader_start(struct doc_reader address_to reader, const struct doc address_to doc)
{
        reader->doc = doc;
        reader->from = 0;
        reader->count = 0;
}

// The code point at at: its length, and 0 at the end.
static p32 doc_code_at(struct doc_reader address_to reader, p64 at, p32 address_to code)
{
        if (at < reader->from || at + 4 > reader->from + reader->count)
        {
                reader->from = at;
                reader->count = (p32)doc_read(reader->doc, at, reader->bytes, DOC_WINDOW);
        }
        if (at >= reader->from + reader->count)
                return 0;
        return doc_decode(reader->bytes + (at - reader->from), reader->from + reader->count - at, code);
}

// The code point that ends at at: its length, and 0 at the start.
static p32 doc_code_before(struct doc_reader address_to reader, p64 at, p32 address_to code)
{
        if (!at)
                return 0;
        if (at > reader->from + reader->count || at < reader->from + (reader->from ? 4 : 1))
        {
                p64 from = at > DOC_WINDOW ? at - DOC_WINDOW : 0;

                reader->from = from;
                reader->count = (p32)doc_read(reader->doc, from, reader->bytes, (positive)(at - from));
        }
        if (at - reader->from > reader->count)
                return 0;
        return doc_decode_back(reader->bytes, at - reader->from, code);
}

/* ---- the document ---- */

/*
        A document over bytes that outlive it (a mapped file, or memory the caller keeps): a byte order mark is held
        aside, the file's line end is the first it has (LF when it has none), and the rest is cut into pieces. False
        when there was no memory.
*/
static b32 doc_open(struct doc address_to doc, const p8 address_to bytes, p64 length)
{
        p64 at = 0;

        memory_zero(doc, sizeof(*doc));
        doc->random = 0x9e3779b97f4a7c15ull ^ length;
        doc->tab_size = 4;
        doc->insert_spaces = true;
        doc->auto_close = true;
        doc->normalize_paste = true;
        doc->recording = true;
        doc->changed_from = doc->changed_line = DOC_NONE;
        doc->separators = "`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?";
        if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
                doc->bom = 1;
                bytes += 3, length -= 3;
        }
        doc->original = bytes;
        doc->original_length = length;
        {
                positive look = length < 1048576 ? length : 1048576;
                const p8 address_to lf = look ? memory_first_of((address_any)bytes, '\n', look) : null;
                const p8 address_to cr = look ? memory_first_of((address_any)bytes, '\r', look) : null;

                doc->eol = cr && (!lf || cr < lf) ? (cr + 1 < bytes + length && cr[1] == '\n' ? DOC_EOL_CRLF : DOC_EOL_CR)
                                                  : DOC_EOL_LF;
        }
        while (at < length)
        {
                p32 take = length - at > DOC_PIECE_MOST ? DOC_PIECE_MOST : (p32)(length - at), node;

                node = doc_node_new(doc, DOC_ORIGINAL, at, take);
                if (!node)
                        return false;
                doc->root = doc_join(doc, doc->root, node);
                at += take;
        }
        if (!array_store_reserve(doc->selections, doc->selection_room, 0, 1, 16))
                return false;
        doc->selections[0] = (struct doc_selection){0, 0, -1};
        doc->selection_count = 1;
        return true;
}

static fn doc_close(struct doc address_to doc)
{
        positive used;

        used = doc->node_used, array_store_release(doc->nodes, doc->node_room, used);
        used = doc->added_used, array_store_release(doc->added, doc->added_room, used);
        used = doc->group_count, array_store_release(doc->groups, doc->group_room, used);
        used = doc->change_count, array_store_release(doc->changes, doc->change_room, used);
        used = doc->piece_count, array_store_release(doc->pieces, doc->piece_room, used);
        used = doc->snapshot_count, array_store_release(doc->snapshots, doc->snapshot_room, used);
        used = doc->selection_count, array_store_release(doc->selections, doc->selection_room, used);
        used = doc->range_count, array_store_release(doc->ranges, doc->range_room, used);
        used = doc->state_room, array_store_release(doc->states, doc->state_room, used);
        memory_zero(doc, sizeof(*doc));
}

// The line end Enter types: the file's.
static const char address_to doc_eol_text(const struct doc address_to doc)
{
        return doc->eol == DOC_EOL_CRLF ? "\r\n" : doc->eol == DOC_EOL_CR ? "\r" : "\n";
}

static positive doc_eol_length(const struct doc address_to doc)
{
        return doc->eol == DOC_EOL_CRLF ? 2 : 1;
}

/* ---- ranges that move with the text ---- */

#define DOC_RANGE_READONLY 1u
#define DOC_RANGE_ERROR 2u
#define DOC_RANGE_WARNING 3u
#define DOC_RANGE_INFO 4u
#define DOC_RANGE_HIGHLIGHT 5u
#define DOC_RANGE_FOLD 6u // the folded text: the lines after a fold's first, to its last's end
#define DOC_RANGE_MARK 7u // a mark in the gutter at a line's start, data its kind
#define DOC_RANGE_LINE 8u // a line's ground, data its role

// An offset through a replace at at of removed bytes by inserted: before it stays, after it moves, inside goes to the
// start; stick takes a place at at itself, or inside, to past what was put in.
static p64 doc_shift(p64 offset, p64 at, p64 removed, p64 inserted, b32 stick)
{
        if (offset < at || (offset == at && !stick))
                return offset;
        if (offset >= at + removed && (offset > at || stick))
                return offset - removed + inserted;
        return stick ? at + inserted : at;
}

static fn doc_ranges_shift(struct doc address_to doc, p64 at, p64 removed, p64 inserted)
{
        positive kept = 0;

        for (positive i = 0; i < doc->range_count; i++)
        {
                struct doc_range range = doc->ranges[i];
                b32 had = range.finish > range.start;

                // A fold an edit reaches into is opened: what it hid is being changed.
                if (range.kind == DOC_RANGE_FOLD && at < range.finish && at + removed > range.start - (range.start > 0))
                        continue;
                range.start = doc_shift(range.start, at, removed, inserted, false);
                range.finish = doc_shift(range.finish, at, removed, inserted, false);
                if (had && range.finish <= range.start)
                        continue;
                doc->ranges[kept++] = range;
        }
        doc->range_count = kept;
        for (p32 i = 0; i < doc->closer_count; i++)
                doc->closers[i] = doc_shift(doc->closers[i], at, removed, inserted, true);
}

static b32 doc_range_add(struct doc address_to doc, p64 start, p64 finish, p32 kind, p32 data)
{
        if (!array_store_reserve(doc->ranges, doc->range_room, doc->range_count, doc->range_count + 1, 16))
                return false;
        doc->ranges[doc->range_count++] = (struct doc_range){start, finish, kind, data};
        return true;
}

static fn doc_ranges_clear(struct doc address_to doc, p32 kind)
{
        positive kept = 0;

        for (positive i = 0; i < doc->range_count; i++)
                if (doc->ranges[i].kind != kind)
                        doc->ranges[kept++] = doc->ranges[i];
        doc->range_count = kept;
}

// Whether a replace at at of removed bytes would touch read-only text: put something inside it, or take any of it.
// Its edges are not inside: text typed against a read-only span is not in it.
static b32 doc_readonly(const struct doc address_to doc, p64 at, p64 removed)
{
        for (positive i = 0; i < doc->range_count; i++)
        {
                const struct doc_range address_to range = &doc->ranges[i];

                if (range->kind != DOC_RANGE_READONLY)
                        continue;
                if (removed ? at < range->finish && at + removed > range->start : at > range->start && at < range->finish)
                        return true;
        }
        return false;
}

/* ---- the tokenizer's line states, which move with the lines ---- */

// The state kept for line, from the gap buffer.
static p32 doc_state_get(const struct doc address_to doc, p64 line)
{
        positive at = line < doc->gap_start ? line : line + (doc->gap_end - doc->gap_start);

        return at < doc->state_room ? doc->states[at] : 0;
}

static fn doc_state_set(struct doc address_to doc, p64 line, p32 state)
{
        positive at = line < doc->gap_start ? line : line + (doc->gap_end - doc->gap_start);

        if (at < doc->state_room)
                doc->states[at] = state;
}

// The gap moved to line and removed lines taken out there and inserted put in, so the states after keep their lines.
static fn doc_states_splice(struct doc address_to doc, p64 line, p64 removed, p64 inserted)
{
        positive used = doc->state_room - (doc->gap_end - doc->gap_start);

        if (!doc->states || line > used)
                return;
        removed = removed > used - line ? used - line : removed;
        // The gap to line.
        if (line < doc->gap_start)
        {
                positive move = doc->gap_start - line;

                memory_copy(doc->states + doc->gap_end - move, doc->states + line, move * sizeof(p32));
                doc->gap_start -= move, doc->gap_end -= move;
        }
        else if (line > doc->gap_start)
        {
                positive move = line - doc->gap_start;

                memory_copy(doc->states + doc->gap_start, doc->states + doc->gap_end, move * sizeof(p32));
                doc->gap_start += move, doc->gap_end += move;
        }
        doc->gap_end += removed;
        if (inserted > doc->gap_end - doc->gap_start)
        {
                positive room = doc->state_room, tail = doc->state_room - doc->gap_end;
                positive need = doc->state_room + inserted - (doc->gap_end - doc->gap_start) + 4096;

                if (!array_store_reserve(doc->states, room, doc->state_room, need, 4096))
                {
                        // No room: nothing is known past the first line, and it is all worked out again.
                        doc->states_valid = 1;
                        doc->states_converge = 0;
                        doc->gap_start = doc->gap_end;
                        return;
                }
                memory_copy(doc->states + room - tail, doc->states + doc->gap_end, tail * sizeof(p32));
                doc->gap_end = room - tail;
                doc->state_room = room;
        }
        for (p64 i = 0; i < inserted; i++)
                doc->states[doc->gap_start++] = 0;
}

/* ---- the change itself ---- */

// Bytes into the added buffer as a tree of pieces: 0 with *failed for no memory.
static p32 doc_add(struct doc address_to doc, const p8 address_to text, p64 length, b32 address_to failed)
{
        p32 tree = 0;
        positive from = doc->added_used;

        if (!length)
                return 0;
        if (!array_store_reserve(doc->added, doc->added_room, doc->added_used, doc->added_used + length, 65536))
        {
                *failed = true;
                return 0;
        }
        memory_copy(doc->added + doc->added_used, text, length);
        doc->added_used += length;
        while (from < doc->added_used)
        {
                p32 take = doc->added_used - from > DOC_PIECE_MOST ? DOC_PIECE_MOST : (p32)(doc->added_used - from);
                p32 node = doc_node_new(doc, DOC_ADDED, from, take);

                if (!node)
                {
                        *failed = true;
                        return tree;
                }
                tree = doc_join(doc, tree, node);
                from += take;
        }
        return tree;
}

/*
        The rightmost piece of a tree grown by the length bytes that follow it in the added buffer: typing at the end
        of what was just typed, so a run of typing is one piece and not one a key.
*/
static b32 doc_extend(struct doc address_to doc, p32 tree, p64 length)
{
        p32 path[DOC_STACK], depth = 0, at = tree;
        struct doc_node address_to node;

        while (at && doc->nodes[at].right && depth < DOC_STACK)
        {
                path[depth++] = at;
                at = doc->nodes[at].right;
        }
        if (!at || depth == DOC_STACK)
                return false;
        node = &doc->nodes[at];
        if (node->buffer != DOC_ADDED || node->start + node->length != doc->added_used - length ||
            node->length + length > DOC_PIECE_MOST)
                return false;
        node->length += (p32)length;
        doc_count(doc->added + node->start, node->length, &node->lf, &node->cr, &node->pairs, &node->edge);
        doc_update(doc, at);
        while (depth)
                doc_update(doc, path[--depth]);
        return true;
}

// The pieces of the document from at, length bytes, onto the history's pieces. False when there was no room.
static b32 doc_keep(struct doc address_to doc, p64 at, p64 length)
{
        p32 stack[DOC_STACK], depth = 0, tree = doc->root;
        p64 offset = 0;

        while (tree && length)
        {
                const struct doc_node address_to node = &doc->nodes[tree];
                p64 before = doc_bytes_of(doc, node->left);

                if (at < before)
                {
                        stack[depth++] = tree;
                        tree = node->left;
                }
                else if (at < before + node->length)
                {
                        at -= before;
                        break;
                }
                else
                {
                        at -= before + node->length;
                        tree = node->right;
                }
        }
        while (tree && offset < length)
        {
                const struct doc_node address_to node = &doc->nodes[tree];
                p64 take = node->length - at;

                take = take < length - offset ? take : length - offset;
                if (!array_store_reserve(doc->pieces, doc->piece_room, doc->piece_count, doc->piece_count + 1, 1024))
                        return false;
                doc->pieces[doc->piece_count++] = (struct doc_piece){node->start + at, (p32)take, node->buffer};
                offset += take;
                at = 0;
                if (node->right)
                {
                        tree = node->right;
                        while (doc->nodes[tree].left && depth < DOC_STACK)
                        {
                                stack[depth++] = tree;
                                tree = doc->nodes[tree].left;
                        }
                }
                else
                        tree = depth ? stack[--depth] : 0;
        }
        return true;
}

// What every change tells the rest: the ranges and the line states move, the lowest change is remembered.
static fn doc_changed(struct doc address_to doc, p64 at, p64 removed, p64 inserted, p64 line, p64 lines_removed,
                      p64 lines_inserted)
{
        p64 edge = line + 1;

        doc->version++;
        doc_ranges_shift(doc, at, removed, inserted);
        if (at < doc->changed_from || doc->changed_from == DOC_NONE)
                doc->changed_from = at;
        if (line < doc->changed_line || doc->changed_line == DOC_NONE)
                doc->changed_line = line;
        if (lines_removed != lines_inserted)
                doc_states_splice(doc, edge, lines_removed, lines_inserted);
        /*
                The states known stop at the edited line; the ones kept past it, moved with their lines, are what the
                tokenizer stops at when it comes out the same (states_converge), but only past every edit since.
        */
#define DOC_MOVED(line_number) ((line_number) > edge ? ((line_number) + lines_inserted > edge + lines_removed ? (line_number) + lines_inserted - lines_removed : edge) : (line_number))
        doc->states_converge = DOC_MOVED(doc->states_converge);
        doc->states_last_edit = DOC_MOVED(doc->states_last_edit);
        if (edge < doc->states_valid)
        {
                p64 moved = DOC_MOVED(doc->states_valid);

                doc->states_converge = moved > doc->states_converge ? moved : doc->states_converge;
                doc->states_valid = edge;
        }
#undef DOC_MOVED
        if (edge + lines_inserted > doc->states_last_edit)
                doc->states_last_edit = edge + lines_inserted;
}

// The pieces of a tree put in place of length bytes at at: the raw move undo and redo make.
static fn doc_swap(struct doc address_to doc, p64 at, p64 length, const struct doc_piece address_to pieces, p32 count,
                   p64 bytes)
{
        p32 left, middle, right, middle_right, put;
        b32 failed = false;
        p64 line = doc_line_of(doc, at), lines_before = doc_line_count(doc), lines_after;

        doc_split(doc, doc->root, at, &left, &middle_right);
        doc_split(doc, middle_right, length, &middle, &right);
        doc_collect(doc, middle, false);
        put = doc_build(doc, pieces, count, &failed);
        doc->root = doc_join(doc, doc_join(doc, left, put), right);
        lines_after = doc_line_count(doc);
        // The lines the swap took and put, as their difference: the state machinery needs only that.
        doc_changed(doc, at, length, bytes, line, lines_before > lines_after ? lines_before - lines_after : 0,
                    lines_after > lines_before ? lines_after - lines_before : 0);
}

/*
        The text from at, removed bytes long, replaced by length bytes of text, the change kept in the group that is
        open (doc_replace opens one when none is). False when it is refused (out of the document, read-only) or there
        was no memory; nothing has changed then.
*/
static b32 doc_replace_in(struct doc address_to doc, p64 at, p64 removed, const p8 address_to text, p64 length)
{
        p32 left, middle, right, middle_right, added = 0;
        struct doc_change change = {at, removed, length, 0, 0, 0, 0};
        b32 failed = false;
        p64 line, lines_before, lines_after;

        if (at > doc_length(doc) || removed > doc_length(doc) - at)
                return false;
        if (!removed && !length)
                return true;
        if (doc_readonly(doc, at, removed))
                return false;
        if (doc->recording && !array_store_reserve(doc->changes, doc->change_room, doc->change_count, doc->change_count + 1, 256))
                return false;
        line = doc_line_of(doc, at);
        lines_before = doc_line_count(doc);
        change.removed_first = (p32)doc->piece_count;
        if (doc->recording && removed && !doc_keep(doc, at, removed))
        {
                doc->piece_count = change.removed_first;
                return false;
        }
        change.removed_count = (p32)(doc->piece_count - change.removed_first);
        if (!doc_split(doc, doc->root, at, &left, &middle_right))
        {
                doc->root = doc_join(doc, left, middle_right);
                doc->piece_count = change.removed_first;
                return false;
        }
        if (!doc_split(doc, middle_right, removed, &middle, &right))
        {
                doc->root = doc_join(doc, left, doc_join(doc, middle, right));
                doc->piece_count = change.removed_first;
                return false;
        }
        doc_collect(doc, middle, false);

        // Typing on the end of what was typed last grows that piece.
        if (length && left && length <= DOC_PIECE_MOST &&
            array_store_reserve(doc->added, doc->added_room, doc->added_used, doc->added_used + length, 65536))
        {
                memory_copy(doc->added + doc->added_used, text, length);
                doc->added_used += length;
                if (!doc_extend(doc, left, length))
                {
                        doc->added_used -= length;
                        added = doc_add(doc, text, length, &failed);
                }
        }
        else if (length)
                added = doc_add(doc, text, length, &failed);
        doc->root = doc_join(doc, doc_join(doc, left, added), right);
        lines_after = doc_line_count(doc);

        if (doc->recording)
        {
                change.inserted_first = (p32)doc->piece_count;
                if (length && !doc_keep(doc, at, length))
                        failed = true;
                change.inserted_count = (p32)(doc->piece_count - change.inserted_first);
                doc->changes[doc->change_count++] = change;
                if (doc->group_count)
                        doc->groups[doc->group_count - 1].change_count++;
        }
        {
                // The lines the change spans: what it took is lines_removed of them, what it put lines_inserted.
                p64 inserted_lines = lines_after + 0, removed_lines = lines_before;

                doc_changed(doc, at, removed, length, line, removed_lines > inserted_lines ? removed_lines - inserted_lines : 0,
                            inserted_lines > removed_lines ? inserted_lines - removed_lines : 0);
        }
        return !failed;
}

/* ---- the history ---- */

static b32 doc_snapshot(struct doc address_to doc, p32 address_to first, p32 address_to count)
{
        if (!array_store_reserve(doc->snapshots, doc->snapshot_room, doc->snapshot_count,
                                 doc->snapshot_count + doc->selection_count, 1024))
                return false;
        *first = (p32)doc->snapshot_count;
        *count = (p32)doc->selection_count;
        memory_copy(doc->snapshots + doc->snapshot_count, doc->selections, doc->selection_count * sizeof(struct doc_selection));
        doc->snapshot_count += doc->selection_count;
        return true;
}

/*
        The oldest quarter of the history forgotten: its groups, and the changes, pieces and snapshots only they held,
        the rest moved down. What a forgotten undo would have brought back cannot be had again; saved follows the
        count and is gone when it was among them.
*/
static fn doc_forget(struct doc address_to doc)
{
        p32 drop = (p32)(doc->group_count / 4 ? doc->group_count / 4 : doc->group_count);
        p32 changes, pieces, snapshots;

        drop = drop > doc->done ? doc->done : drop;
        if (!drop)
                return;
        changes = drop < doc->group_count ? doc->groups[drop].change_first : (p32)doc->change_count;
        snapshots = drop < doc->group_count ? doc->groups[drop].before_first : (p32)doc->snapshot_count;
        pieces = changes < doc->change_count ? doc->changes[changes].removed_first : (p32)doc->piece_count;
        memory_copy(doc->groups, doc->groups + drop, (doc->group_count - drop) * sizeof(struct doc_group));
        doc->group_count -= drop;
        memory_copy(doc->changes, doc->changes + changes, (doc->change_count - changes) * sizeof(struct doc_change));
        doc->change_count -= changes;
        memory_copy(doc->pieces, doc->pieces + pieces, (doc->piece_count - pieces) * sizeof(struct doc_piece));
        doc->piece_count -= pieces;
        memory_copy(doc->snapshots, doc->snapshots + snapshots, (doc->snapshot_count - snapshots) * sizeof(struct doc_selection));
        doc->snapshot_count -= snapshots;
        for (positive g = 0; g < doc->group_count; g++)
        {
                doc->groups[g].change_first -= changes;
                doc->groups[g].before_first -= snapshots;
                doc->groups[g].after_first -= snapshots;
        }
        for (positive c = 0; c < doc->change_count; c++)
        {
                doc->changes[c].removed_first -= pieces;
                doc->changes[c].inserted_first -= pieces;
        }
        doc->done -= drop;
        doc->saved = doc->saved >= (b64)drop ? doc->saved - drop : -1;
}

// The redo past done dropped: a change made after an undo starts a new line of history.
static fn doc_drop_redo(struct doc address_to doc)
{
        const struct doc_group address_to first;

        if (doc->done >= doc->group_count)
                return;
        first = &doc->groups[doc->done];
        doc->change_count = first->change_first;
        doc->snapshot_count = first->before_first;
        // A change keeps the pieces it took out and then those it put in, so the last change's last piece ends them.
        doc->piece_count = first->change_first ? doc->changes[first->change_first - 1].inserted_first +
                                                     doc->changes[first->change_first - 1].inserted_count
                                               : 0;
        if (doc->saved > (b64)doc->done)
                doc->saved = -1;
        doc->group_count = doc->done;
}

/*
        A group begun for a change of kind: the open one joined when this may join it (the same kind, the carets
        where it left them, soon enough, and not a word typed after a blank or a line end typed), or a new one with the
        selections as they are now. Nested begins are one group. False when there was no memory.
*/
static b32 doc_group_begin(struct doc address_to doc, p8 kind, p32 typed)
{
        struct doc_group address_to last;

        if (!doc->recording)
                return true;
        if (doc->depth++)
                return true;
        doc_drop_redo(doc);
        last = doc->group_count && doc->done == doc->group_count ? &doc->groups[doc->group_count - 1] : null;
        if (last && doc->open && kind != DOC_KIND_OTHER && last->kind == kind && doc->now - last->time < DOC_PAUSE_NS &&
            last->after_count == doc->selection_count &&
            last->after_first + last->after_count == doc->snapshot_count &&
            !memory_compare(doc->snapshots + last->after_first, doc->selections,
                            doc->selection_count * sizeof(struct doc_selection)) &&
            !(kind == DOC_KIND_TYPING && typed > ' ' && (last->last == ' ' || last->last == '\t')) &&
            !(kind == DOC_KIND_TYPING && (typed == '\n' || typed == '\r')))
        {
                // The after snapshot is the last thing kept: it is made again when the group ends.
                doc->snapshot_count = last->after_first;
                doc->depth_opened = false;
                return true;
        }
        if (doc->group_count >= DOC_HISTORY_GROUPS || doc->piece_count >= DOC_HISTORY_PIECES ||
            doc->change_count >= DOC_HISTORY_CHANGES || doc->snapshot_count >= DOC_HISTORY_SNAPSHOTS)
                doc_forget(doc);
        if (!array_store_reserve(doc->groups, doc->group_room, doc->group_count, doc->group_count + 1, 256))
        {
                doc->depth--;
                return false;
        }
        last = &doc->groups[doc->group_count];
        memory_zero(last, sizeof(*last));
        last->change_first = (p32)doc->change_count;
        last->kind = kind;
        last->before_primary = doc->primary;
        if (!doc_snapshot(doc, &last->before_first, &last->before_count))
        {
                doc->depth--;
                return false;
        }
        doc->group_count++;
        doc->done = (p32)doc->group_count;
        doc->depth_opened = true;
        return true;
}

static fn doc_group_end(struct doc address_to doc, p32 typed)
{
        struct doc_group address_to last;

        if (!doc->recording || !doc->depth || --doc->depth)
                return;
        last = &doc->groups[doc->group_count - 1];
        // A group that changed nothing is no undo.
        if (!last->change_count)
        {
                doc->snapshot_count = last->before_first;
                doc->group_count--;
                doc->done = (p32)doc->group_count;
                doc->open = false;
                return;
        }
        last->after_primary = doc->primary;
        if (!doc_snapshot(doc, &last->after_first, &last->after_count))
                last->after_count = 0;
        last->time = doc->now;
        if (typed)
                last->last = (p8)(typed < 128 ? typed : 'a');
        doc->open = last->kind != DOC_KIND_OTHER;
}

// The next change is a group of its own: the carets moved, the file was saved, a command ran.
static fn doc_seal(struct doc address_to doc)
{
        doc->open = false;
}

// A replace that is an undo of its own when no group is open, so a change outside one still drops the redo.
static b32 doc_replace(struct doc address_to doc, p64 at, p64 removed, const p8 address_to text, p64 length)
{
        b32 done;

        if (!doc->recording || doc->depth)
                return doc_replace_in(doc, at, removed, text, length);
        if (!doc_group_begin(doc, DOC_KIND_OTHER, 0))
                return false;
        done = doc_replace_in(doc, at, removed, text, length);
        doc_group_end(doc, 0);
        doc_seal(doc);
        return done;
}

static fn doc_selections_load(struct doc address_to doc, p32 first, p32 count, p32 primary)
{
        if (!count || !array_store_reserve(doc->selections, doc->selection_room, 0, count, 16))
                return;
        memory_copy(doc->selections, doc->snapshots + first, count * sizeof(struct doc_selection));
        doc->selection_count = count;
        doc->primary = primary < count ? primary : 0;
}

// The last group taken back, its changes undone from the last to the first, its selections from before. False when
// there is none.
static b32 doc_undo(struct doc address_to doc)
{
        const struct doc_group address_to group;

        if (!doc->done || doc->depth)
                return false;
        group = &doc->groups[doc->done - 1];
        for (p32 c = group->change_count; c--;)
        {
                const struct doc_change address_to change = &doc->changes[group->change_first + c];

                doc_swap(doc, change->at, change->inserted, doc->pieces + change->removed_first, change->removed_count,
                         change->removed);
        }
        doc_selections_load(doc, group->before_first, group->before_count, group->before_primary);
        doc->done--;
        doc->open = false;
        doc->closer_count = 0;
        return true;
}

static b32 doc_redo(struct doc address_to doc)
{
        const struct doc_group address_to group;

        if (doc->done >= doc->group_count || doc->depth)
                return false;
        group = &doc->groups[doc->done];
        for (p32 c = 0; c < group->change_count; c++)
        {
                const struct doc_change address_to change = &doc->changes[group->change_first + c];

                doc_swap(doc, change->at, change->removed, doc->pieces + change->inserted_first, change->inserted_count,
                         change->inserted);
        }
        if (group->after_count)
                doc_selections_load(doc, group->after_first, group->after_count, group->after_primary);
        doc->done++;
        doc->open = false;
        doc->closer_count = 0;
        return true;
}

static b32 doc_dirty(const struct doc address_to doc)
{
        return doc->saved != (b64)doc->done;
}

static fn doc_mark_saved(struct doc address_to doc)
{
        doc->saved = doc->done;
        doc->open = false;
}

/* ---- selections ---- */

static p64 doc_sel_start(const struct doc_selection address_to s)
{
        return s->anchor < s->active ? s->anchor : s->active;
}

static p64 doc_sel_end(const struct doc_selection address_to s)
{
        return s->anchor < s->active ? s->active : s->anchor;
}

/*
        The selections sorted by where they start and those that overlap joined: two that share a byte, a caret on
        another caret, a caret inside a selection. Two that only touch stay two. A joined pair keeps the primary's
        direction, and is the primary when either was. Every end is clamped to the document first.
*/
static fn doc_normalize(struct doc address_to doc)
{
        p64 length = doc_length(doc);
        positive count = doc->selection_count, kept = 0;
        p32 primary = doc->primary < count ? doc->primary : 0;

        if (!count)
        {
                doc->selections[0] = (struct doc_selection){0, 0, -1};
                doc->selection_count = 1;
                doc->primary = 0;
                return;
        }
        for (positive i = 0; i < count; i++)
        {
                doc->selections[i].anchor = doc->selections[i].anchor > length ? length : doc->selections[i].anchor;
                doc->selections[i].active = doc->selections[i].active > length ? length : doc->selections[i].active;
        }
        // An insertion sort, carrying the primary: the list is nearly sorted after any edit, and short.
        for (positive i = 1; i < count; i++)
        {
                struct doc_selection moving = doc->selections[i];
                positive j = i;

                while (j && doc_sel_start(&doc->selections[j - 1]) > doc_sel_start(&moving))
                {
                        doc->selections[j] = doc->selections[j - 1];
                        if (primary == j - 1)
                                primary = (p32)j;
                        j--;
                }
                doc->selections[j] = moving;
                if (primary == i)
                        primary = (p32)j;
        }
        for (positive i = 0; i < count; i++)
        {
                struct doc_selection now = doc->selections[i];

                if (kept)
                {
                        struct doc_selection address_to last = &doc->selections[kept - 1];
                        p64 last_start = doc_sel_start(last), last_end = doc_sel_end(last);
                        p64 now_start = doc_sel_start(&now), now_end = doc_sel_end(&now);
                        b32 now_caret = now.anchor == now.active, last_caret = last->anchor == last->active;

                        if (now_start < last_end || (now_start == last_end && (now_caret ? (last_caret || now_start < last_end) : last_caret && last_start == now_start)))
                        {
                                p64 finish = now_end > last_end ? now_end : last_end;
                                b32 is_primary = primary == i, forward = is_primary ? now.anchor <= now.active : last->anchor <= last->active;

                                last->anchor = forward ? last_start : finish;
                                last->active = forward ? finish : last_start;
                                if (is_primary)
                                        last->goal = now.goal;
                                if (is_primary || primary == kept - 1)
                                        primary = (p32)(kept - 1);
                                else if (primary > i)
                                        primary--;
                                continue;
                        }
                }
                if (primary == i)
                        primary = (p32)kept;
                doc->selections[kept++] = now;
        }
        doc->selection_count = kept;
        doc->primary = primary < kept ? primary : 0;
}

static fn doc_select_set(struct doc address_to doc, p64 anchor, p64 active)
{
        doc->selections[0] = (struct doc_selection){anchor, active, -1};
        doc->selection_count = 1;
        doc->primary = 0;
        doc->closer_count = 0;
        doc_normalize(doc);
        doc_seal(doc);
}

// One more selection, the primary now.
static b32 doc_select_add(struct doc address_to doc, p64 anchor, p64 active)
{
        if (!array_store_reserve(doc->selections, doc->selection_room, doc->selection_count, doc->selection_count + 1, 16))
                return false;
        doc->selections[doc->selection_count] = (struct doc_selection){anchor, active, -1};
        doc->primary = (p32)doc->selection_count++;
        doc_normalize(doc);
        doc_seal(doc);
        return true;
}

/* ---- characters, words and columns over the document ---- */

// The end of the cluster that starts at at (which is the start of one).
static p64 doc_cluster_end(const struct doc address_to doc, p64 at)
{
        struct doc_reader reader;
        struct doc_gstate state = {0};
        p64 length = doc_length(doc);
        p32 code, size;
        p8 two[2];
        positive got = doc_read(doc, at, two, 2);

        // An ASCII character with ASCII (or nothing) after it is a cluster of one: nothing ASCII extends or prepends,
        // and only CR LF pairs.
        if (got && two[0] < 0x80 && two[0] != '\r' && (got == 1 || two[1] < 0x80))
                return at + 1;
        doc_reader_start(&reader, doc);
        if (at >= length || !(size = doc_code_at(&reader, at, &code)))
                return at;
        doc_grapheme_step(&state, doc_props(code));
        at += size;
        while (at < length && (size = doc_code_at(&reader, at, &code)))
        {
                struct doc_gstate next = state;

                if (doc_grapheme_step(&next, doc_props(code)))
                        break;
                state = next;
                at += size;
        }
        return at;
}

/*
        Where the cluster that ends at at begins. The walk starts where a cluster surely does -- after a control or
        a line end, or the document's start, at most 1024 bytes back but through a run of regional indicators, whose
        pairs count from the run's start, and never more than 64 KiB -- and goes forward to at.
*/
static p64 doc_cluster_start(const struct doc address_to doc, p64 at)
{
        struct doc_reader reader;
        p64 from = at, last;
        p32 code, size;

        if (at > doc_length(doc))
                at = from = doc_length(doc);
        if (!at)
                return 0;
        {
                // The same rule backwards: an ASCII character with ASCII before it (or nothing) begins its cluster.
                p8 two[2];
                positive back = at >= 2 ? 2 : 1;
                p8 last, before;

                doc_read(doc, at - back, two, back);
                last = two[back - 1], before = back == 2 ? two[0] : 0;
                if (last < 0x80 && !(last == '\n' && before == '\r') && before < 0x80)
                        return at - 1;
        }
        doc_reader_start(&reader, doc);
        while (from && (size = doc_code_before(&reader, from, &code)))
        {
                p32 gcb = DOC_GCB(doc_props(code));

                // A cluster starts after a control or a line end; a CR is one only when no LF follows it.
                if (from < at && (gcb == GCB_LF || gcb == GCB_CONTROL || (gcb == GCB_CR && doc_byte(doc, from) != '\n')))
                        break;
                if ((at - from >= 1024 && gcb != GCB_RI) || at - from >= 65536)
                        break;
                from -= size;
        }
        // Forward from there with one reader: the last cut before at is the cluster's start.
        last = from;
        {
                struct doc_gstate state = {0};
                p64 position = from;

                doc_reader_start(&reader, doc);
                while (position < at && (size = doc_code_at(&reader, position, &code)))
                {
                        if (doc_grapheme_step(&state, doc_props(code)))
                                last = position;
                        position += size;
                }
        }
        return last;
}

// Whether a cluster is one Backspace takes whole: an emoji sequence, a flag, a keycap, Hangul jamo, a CR LF.
static b32 doc_cluster_whole(const struct doc address_to doc, p64 start, p64 finish)
{
        struct doc_reader reader;
        p32 code, size;

        doc_reader_start(&reader, doc);
        for (p64 at = start; at < finish && (size = doc_code_at(&reader, at, &code)); at += size)
        {
                p16 props = doc_props(code);

                if (DOC_PICTOGRAPHIC(props) || DOC_GCB(props) == GCB_RI || DOC_GCB(props) == GCB_CR || code == 0x20e3 ||
                    code == 0xfe0f || (DOC_GCB(props) >= GCB_L && DOC_GCB(props) <= GCB_LVT))
                        return true;
        }
        return false;
}

// Where Backspace at at goes back to.
static p64 doc_backspace_start(const struct doc address_to doc, p64 at)
{
        p64 start = doc_cluster_start(doc, at);
        struct doc_reader reader;
        p32 code, size;

        if (start == at || doc_cluster_whole(doc, start, at))
                return start;
        doc_reader_start(&reader, doc);
        size = doc_code_before(&reader, at, &code);
        return size ? at - size : start;
}

// A character's kind for word jumps.
#define DOC_CLASS_BLANK 0
#define DOC_CLASS_SEPARATOR 1
#define DOC_CLASS_WORD 2
#define DOC_CLASS_BREAK 3

static p32 doc_class(const struct doc address_to doc, p32 code)
{
        if (code == '\n' || code == '\r')
                return DOC_CLASS_BREAK;
        if (code == ' ' || code == '\t' || code == 0xa0 || code == 0x3000 || code == 0x1680 || (code >= 0x2000 && code <= 0x200a))
                return DOC_CLASS_BLANK;
        if (code < 128 && doc->separators)
                for (const char address_to at = doc->separators; *at; at++)
                        if ((p32)(p8)*at == code)
                                return DOC_CLASS_SEPARATOR;
        return DOC_CLASS_WORD;
}

// A word jump right from at, as VS Code's: the blanks passed, then a run of one kind; a line end is a stop of its own.
static p64 doc_word_right(const struct doc address_to doc, p64 at)
{
        struct doc_reader reader;
        p64 length = doc_length(doc);
        p32 code = 0, size, kind;

        doc_reader_start(&reader, doc);
        if (at >= length)
                return length;
        doc_code_at(&reader, at, &code);
        if (doc_class(doc, code) == DOC_CLASS_BREAK)
                return doc_cluster_end(doc, at);
        while (at < length && (size = doc_code_at(&reader, at, &code)) && doc_class(doc, code) == DOC_CLASS_BLANK)
                at += size;
        if (at >= length || doc_class(doc, code) == DOC_CLASS_BREAK)
                return at;
        kind = doc_class(doc, code);
        while (at < length && doc_code_at(&reader, at, &code) && doc_class(doc, code) == kind)
                at = doc_cluster_end(doc, at);
        return at;
}

static p64 doc_word_left(const struct doc address_to doc, p64 at)
{
        struct doc_reader reader;
        p32 code = 0, size, kind;

        doc_reader_start(&reader, doc);
        if (!at)
                return 0;
        doc_code_before(&reader, at, &code);
        if (doc_class(doc, code) == DOC_CLASS_BREAK)
                return doc_cluster_start(doc, at);
        while (at && (size = doc_code_before(&reader, at, &code)) && doc_class(doc, code) == DOC_CLASS_BLANK)
                at -= size;
        if (!at || doc_class(doc, code) == DOC_CLASS_BREAK)
                return at;
        kind = doc_class(doc, code);
        while (at && doc_code_before(&reader, at, &code) && doc_class(doc, code) == kind)
                at = doc_cluster_start(doc, at);
        return at;
}

/*
        The word at at, for a double click: the UAX #29 segment that holds it, cut again at the separators, read from a
        window of the line round it. On blanks it is the blanks; on a separator, that one; at a line's end, the word
        before.
*/
#define DOC_WORD_WINDOW 2048

static fn doc_word_at(const struct doc address_to doc, p64 at, p64 address_to start, p64 address_to finish)
{
        p64 line = doc_line_of(doc, at), line_start = doc_line_start(doc, line), line_end = doc_line_end(doc, line);
        p64 from = at > line_start + DOC_WORD_WINDOW / 2 ? doc_cluster_start(doc, at - DOC_WORD_WINDOW / 2) : line_start;
        p64 to = line_end - from > DOC_WORD_WINDOW ? from + DOC_WORD_WINDOW : line_end;
        p8 bytes[DOC_WORD_WINDOW];
        p16 props[DOC_WORD_WINDOW];
        p32 offsets[DOC_WORD_WINDOW + 1];
        p8 breaks[DOC_WORD_WINDOW + 1];
        positive length = doc_read(doc, from, bytes, (positive)(to - from)), count = 0, here = 0;

        if (at == line_end && at > line_start)
                at--;
        for (positive i = 0; i < length;)
        {
                p32 code, size = doc_decode(bytes + i, length - i, &code);

                offsets[count] = (p32)i;
                props[count] = doc_props(code);
                if (from + i <= at)
                        here = count;
                count++;
                i += size;
        }
        offsets[count] = (p32)length;
        if (!count)
        {
                *start = *finish = at;
                return;
        }
        doc_word_breaks(props, count, breaks);
        // A separator is a word of its own, whatever UAX #29 joins it to.
        for (positive i = 0; i < count; i++)
        {
                p32 code;

                doc_decode(bytes + offsets[i], length - offsets[i], &code);
                if (doc_class(doc, code) == DOC_CLASS_SEPARATOR)
                        breaks[i] = breaks[i + 1] = 1;
        }
        {
                positive first = here, last = here + 1;

                while (first && !breaks[first])
                        first--;
                while (last < count && !breaks[last])
                        last++;
                *start = from + offsets[first];
                *finish = from + offsets[last];
        }
}

// How many columns a cluster takes at column: a tab to the next stop, a flag or an emoji asked for (VS16) two.
static p32 doc_cluster_width(const struct doc address_to doc, const p8 address_to bytes, positive length, p64 column)
{
        p32 code, size, width;

        if (!length)
                return 0;
        size = doc_decode(bytes, length, &code);
        if (code == '\t')
                return doc->tab_size - (p32)(column % doc->tab_size);
        if ((code & DOC_INVALID) || code < 0x20 || code == 0x7f || (code >= 0x80 && code < 0xa0))
                return 1;
        if (DOC_GCB(doc_props(code)) == GCB_RI)
                return 2;
        width = (p32)unicode_width(code, UNICODE_WIDTH_TERMINAL);
        for (positive at = size; at < length;)
        {
                p32 next;

                at += doc_decode(bytes + at, length - at, &next);
                if (next == 0xfe0f && DOC_PICTOGRAPHIC(doc_props(code)))
                        width = 2;
        }
        return width ? width : 1;
}

// The display column of at on its line.
static p64 doc_column(const struct doc address_to doc, p64 at)
{
        p64 start = doc_line_start(doc, doc_line_of(doc, at)), column = 0;
        p8 cluster[64];

        while (start < at)
        {
                p64 finish = doc_cluster_end(doc, start);
                positive size = doc_read(doc, start, cluster, finish - start < sizeof(cluster) ? (positive)(finish - start) : sizeof(cluster));

                column += doc_cluster_width(doc, cluster, size, column);
                start = finish;
        }
        return column;
}

// Where column want falls on line: the boundary before it, or the one after when want is past a wide one's middle.
static p64 doc_at_column(const struct doc address_to doc, p64 line, p64 want)
{
        p64 at = doc_line_start(doc, line), finish = doc_line_end(doc, line), column = 0;
        p8 cluster[64];

        while (at < finish)
        {
                p64 next = doc_cluster_end(doc, at);
                positive size = doc_read(doc, at, cluster, next - at < sizeof(cluster) ? (positive)(next - at) : sizeof(cluster));
                p32 width = doc_cluster_width(doc, cluster, size, column);

                if (column + width > want)
                        return want - column >= (width + 1) / 2 && width > 1 ? next : at;
                column += width;
                at = next;
        }
        return finish;
}

// The first character on line that is not a blank: the line's end when it is all blanks.
static p64 doc_first_text(const struct doc address_to doc, p64 line)
{
        p64 at = doc_line_start(doc, line), finish = doc_line_end(doc, line);

        while (at < finish)
        {
                p32 byte = doc_byte(doc, at);

                if (byte != ' ' && byte != '\t')
                        break;
                at++;
        }
        return at;
}

/* ---- edits of every selection at once ---- */

/*
        One replace of an edit list: from at, removed bytes, by length bytes of text, and where the selection it
        stands for goes, as offsets into the text it puts in (anchor and active), or DOC_NONE to map the old ones.
*/
struct doc_edit
{
        p64 at, removed;
        const p8 address_to text;
        p64 length;
        p64 anchor, active;
};

// An offset through an edit list (sorted, not overlapping): moved by every edit before it, and inside one to its end
// (stick) or start.
static p64 doc_map(p64 offset, const struct doc_edit address_to edits, positive count, b32 stick)
{
        p64 moved = offset;

        for (positive i = 0; i < count; i++)
        {
                const struct doc_edit address_to edit = &edits[i];

                if (offset < edit->at || (offset == edit->at && !stick && edit->removed == 0))
                        break;
                if (offset >= edit->at + edit->removed && !(offset == edit->at && edit->removed == 0 && !stick))
                        moved += edit->length - edit->removed;
                else
                {
                        p64 before = moved - offset;

                        return edit->at + before + (stick ? edit->length : 0);
                }
        }
        return moved;
}

/*
        An edit list applied as one group of kind: sorted by at and not overlapping, done from the last to the first,
        and the selections set from the edits that say where theirs go (when there is one an edit for every selection)
        and mapped through the list otherwise. False when one was refused; the group then holds what was done.
*/
static b32 doc_apply(struct doc address_to doc, struct doc_edit address_to edits, positive count, p8 kind, p32 typed)
{
        b32 done = true, placed = count == doc->selection_count && count;

        for (positive i = 0; i < count; i++)
        {
                if (doc_readonly(doc, edits[i].at, edits[i].removed))
                        return false;
                placed = placed && edits[i].anchor != DOC_NONE;
        }
        if (!doc_group_begin(doc, kind, typed))
                return false;
        for (positive i = count; i--;)
                if (!doc_replace(doc, edits[i].at, edits[i].removed, edits[i].text, edits[i].length))
                        done = false;
        if (placed)
        {
                p64 delta = 0;

                for (positive i = 0; i < count; i++)
                {
                        doc->selections[i].anchor = edits[i].at + delta + edits[i].anchor;
                        doc->selections[i].active = edits[i].at + delta + edits[i].active;
                        doc->selections[i].goal = -1;
                        delta += edits[i].length - edits[i].removed;
                }
        }
        else
                for (positive s = 0; s < doc->selection_count; s++)
                {
                        struct doc_selection address_to sel = &doc->selections[s];
                        b32 caret = sel->anchor == sel->active, forward = sel->anchor <= sel->active;
                        p64 start = doc_map(doc_sel_start(sel), edits, count, caret);
                        p64 finish = doc_map(doc_sel_end(sel), edits, count, true);

                        sel->anchor = forward ? start : finish;
                        sel->active = forward ? finish : start;
                        if (caret)
                                sel->anchor = sel->active = finish;
                }
        doc_normalize(doc);
        doc_group_end(doc, typed);
        return done;
}

// Room for an edit list of count, kept between calls.
static struct doc_edit address_to doc_edits_room(positive count)
{
        static struct doc_edit address_to room;
        static positive held;

        if (!array_store_reserve(room, held, 0, count ? count : 1, 64))
                return null;
        return room;
}

// Bytes kept between calls for the texts of an edit list.
static p8 address_to doc_bytes_room(positive count)
{
        static p8 address_to room;
        static positive held;

        if (!array_store_reserve(room, held, 0, count ? count : 1, 4096))
                return null;
        return room;
}

static b32 doc_any_selected(const struct doc address_to doc)
{
        for (positive s = 0; s < doc->selection_count; s++)
                if (doc->selections[s].anchor != doc->selections[s].active)
                        return true;
        return false;
}

static const char doc_opens[] = "([{\"'`", doc_closes[] = ")]}\"'`";

// Text put round every selection: open before, close after, the selections kept on the text between.
static b32 doc_surround(struct doc address_to doc, p8 open, p8 close)
{
        positive count = doc->selection_count;
        struct doc_edit address_to edits = doc_edits_room(count * 2);
        static p8 pair[2];

        if (!edits)
                return false;
        pair[0] = open, pair[1] = close;
        for (positive s = 0; s < count; s++)
        {
                edits[s * 2] = (struct doc_edit){doc_sel_start(&doc->selections[s]), 0, &pair[0], 1, DOC_NONE, DOC_NONE};
                edits[s * 2 + 1] = (struct doc_edit){doc_sel_end(&doc->selections[s]), 0, &pair[1], 1, DOC_NONE, DOC_NONE};
        }
        if (!doc_group_begin(doc, DOC_KIND_OTHER, 0))
                return false;
        {
                b32 done;
                struct doc_selection address_to before = null;
                positive room = 0;

                if (!array_store_reserve(before, room, 0, count, 16))
                {
                        doc_group_end(doc, 0);
                        return false;
                }
                memory_copy(before, doc->selections, count * sizeof(*before));
                done = doc_apply(doc, edits, count * 2, DOC_KIND_OTHER, 0);
                // Each selection on its text, one further on for every opener before it and its own.
                if (doc->selection_count == count)
                        for (positive s = 0; s < count; s++)
                        {
                                b32 forward = before[s].anchor <= before[s].active;
                                p64 start = doc_sel_start(&before[s]) + 2 * s + 1, finish = doc_sel_end(&before[s]) + 2 * s + 1;

                                doc->selections[s].anchor = forward ? start : finish;
                                doc->selections[s].active = forward ? finish : start;
                        }
                array_store_release(before, room, count);
                doc_group_end(doc, 0);
                doc_seal(doc);
                return done;
        }
}

/*
        Text typed at every selection: each selection's text replaced, the caret after it. Overwrite takes the cluster
        after a caret. With auto_close, a bracket or quote typed at a caret puts its closer after it when what follows
        is a blank, a closer or the end (a quote after a letter is an apostrophe and does not), around a selection it
        wraps it, and a closer typed where auto_close put one steps over it.
*/
static b32 doc_type(struct doc address_to doc, const p8 address_to text, p64 length)
{
        struct doc_edit address_to edits;
        p32 typed = length == 1 ? text[0] : length ? 'a' : 0;
        const char address_to open = length == 1 && doc->auto_close ? memory_first_of((address_any)doc_opens, (b8)text[0], 6) : null;
        const char address_to close = length == 1 && doc->auto_close ? memory_first_of((address_any)doc_closes, (b8)text[0], 6) : null;
        static p8 pair[2];

        if (!length)
                return true;
        if (close && doc->closer_count && !doc_any_selected(doc))
        {
                b32 all = true;

                for (positive s = 0; s < doc->selection_count && all; s++)
                {
                        b32 found = false;

                        for (p32 c = 0; c < doc->closer_count; c++)
                                found |= doc->closers[c] == doc->selections[s].active;
                        all = found && doc_byte(doc, doc->selections[s].active) == text[0];
                }
                if (all)
                {
                        for (positive s = 0; s < doc->selection_count; s++)
                                doc->selections[s].anchor = ++doc->selections[s].active;
                        doc->closer_count = 0;
                        doc_seal(doc);
                        return true;
                }
        }
        if (open && doc_any_selected(doc))
                return doc_surround(doc, (p8)*open, (p8)doc_closes[open - doc_opens]);
        edits = doc_edits_room(doc->selection_count);
        if (!edits)
                return false;
        pair[0] = open ? (p8)*open : 0;
        pair[1] = open ? (p8)doc_closes[open - doc_opens] : 0;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                const struct doc_selection address_to sel = &doc->selections[s];
                p64 start = doc_sel_start(sel), finish = doc_sel_end(sel);
                struct doc_edit edit = {start, finish - start, text, length, length, length};

                if (doc->overwrite && start == finish && start < doc_length(doc) && doc_byte(doc, start) != '\n' &&
                    doc_byte(doc, start) != '\r')
                        edit.removed = doc_cluster_end(doc, start) - start;
                if (open && start == finish)
                {
                        p32 next = doc_byte(doc, finish), before = start ? doc_byte(doc, start - 1) : ' ';
                        b32 quote = *open == '"' || *open == '\'' || *open == '`';
                        b32 room = next == 0x100 || next == ' ' || next == '\t' || next == '\n' || next == '\r' ||
                                   memory_first_of((address_any)doc_closes, (b8)next, 3) != null;

                        if (quote && (byte_is_alpha((b8)before) || byte_is_digit((b8)before) || before == '_' || before >= 0x80))
                                room = false;
                        // A quote typed before the same quote steps over it, as a closer does.
                        if (room)
                        {
                                edit.text = pair;
                                edit.length = 2;
                                edit.anchor = edit.active = 1;
                        }
                }
                edits[s] = edit;
        }
        if (!doc_apply(doc, edits, doc->selection_count, DOC_KIND_TYPING, typed))
                return false;
        if (open)
        {
                doc->closer_count = 0;
                for (positive s = 0; s < doc->selection_count && doc->closer_count < 8; s++)
                        if (doc->selections[s].active && doc_byte(doc, doc->selections[s].active) == pair[1] &&
                            doc_byte(doc, doc->selections[s].active - 1) == pair[0])
                                doc->closers[doc->closer_count++] = doc->selections[s].active;
        }
        return true;
}

/*
        Text pasted, which may hold line ends: they are made the file's when normalize_paste says so (a field makes
        them blanks), and when there are as many lines as carets each caret takes one (VS Code's multi-cursor paste).
*/
static b32 doc_paste(struct doc address_to doc, const p8 address_to text, p64 length)
{
        p8 address_to made = null;
        positive made_room = 0, made_length = 0, eol_length = doc_eol_length(doc), lines = 1;
        const char address_to eol = doc_eol_text(doc);
        struct doc_edit address_to edits;
        b32 done;

        if (doc->normalize_paste || doc->single_line)
        {
                for (p64 at = 0; at < length; at++)
                {
                        if (!array_store_reserve(made, made_room, made_length, made_length + eol_length + 1, 4096))
                        {
                                array_store_release(made, made_room, made_length);
                                return false;
                        }
                        if (text[at] != '\n' && text[at] != '\r')
                        {
                                made[made_length++] = text[at];
                                continue;
                        }
                        if (text[at] == '\r' && at + 1 < length && text[at + 1] == '\n')
                                at++;
                        if (doc->single_line)
                                made[made_length++] = ' ';
                        else
                                memory_copy(made + made_length, eol, eol_length), made_length += eol_length;
                }
                text = made, length = made_length;
        }
        edits = doc_edits_room(doc->selection_count);
        if (!edits)
        {
                array_store_release(made, made_room, made_length);
                return false;
        }
        if (doc->selection_count > 1)
        {
                for (p64 at = 0; at + eol_length <= length; at++)
                        if (!memory_compare(text + at, eol, eol_length))
                                lines++, at += eol_length - 1;
                if (length >= eol_length && !memory_compare(text + length - eol_length, eol, eol_length))
                        lines--;
        }
        doc_seal(doc);
        if (doc->selection_count > 1 && lines == doc->selection_count)
        {
                p64 from = 0;

                for (positive s = 0; s < lines; s++)
                {
                        p64 to = from;

                        while (to < length && (to + eol_length > length || memory_compare(text + to, eol, eol_length)))
                                to++;
                        edits[s] = (struct doc_edit){doc_sel_start(&doc->selections[s]),
                                                     doc_sel_end(&doc->selections[s]) - doc_sel_start(&doc->selections[s]),
                                                     text + from, to - from, to - from, to - from};
                        from = to + eol_length;
                }
        }
        else
                for (positive s = 0; s < doc->selection_count; s++)
                        edits[s] = (struct doc_edit){doc_sel_start(&doc->selections[s]),
                                                     doc_sel_end(&doc->selections[s]) - doc_sel_start(&doc->selections[s]), text,
                                                     length, length, length};
        done = doc_apply(doc, edits, doc->selection_count, DOC_KIND_OTHER, 0);
        doc_seal(doc);
        doc->closer_count = 0;
        array_store_release(made, made_room, made_length);
        return done;
}

// The selected text of every selection, joined by the file's line end, into a buffer the caller releases with
// array_store_release: its length, 0 and nothing kept when nothing is selected or there is no memory.
static positive doc_copy_text(const struct doc address_to doc, p8 address_to address_to into, positive address_to room)
{
        positive length = 0, eol_length = doc_eol_length(doc);
        const char address_to eol = doc_eol_text(doc);

        *into = null;
        *room = 0;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 start = doc_sel_start(&doc->selections[s]), finish = doc_sel_end(&doc->selections[s]);

                if (start == finish)
                        continue;
                if (!array_store_reserve(*into, *room, length, length + (finish - start) + eol_length, 4096))
                {
                        array_store_release(*into, *room, length);
                        return 0;
                }
                if (length)
                        memory_copy(*into + length, eol, eol_length), length += eol_length;
                length += doc_read(doc, start, *into + length, (positive)(finish - start));
        }
        return length;
}

// Every selection's text taken out.
static b32 doc_erase_selections(struct doc address_to doc)
{
        struct doc_edit address_to edits = doc_edits_room(doc->selection_count);

        if (!edits)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 start = doc_sel_start(&doc->selections[s]), finish = doc_sel_end(&doc->selections[s]);

                edits[s] = (struct doc_edit){start, finish - start, null, 0, 0, 0};
        }
        doc_seal(doc);
        return doc_apply(doc, edits, doc->selection_count, DOC_KIND_OTHER, 0);
}

// How far a delete reaches: a cluster, a word, to the line's start or end.
#define DOC_REACH_CLUSTER 0
#define DOC_REACH_WORD 1
#define DOC_REACH_LINE 2

/*
        Backspace (forward false) or Delete at every caret, by reach; selections are taken out instead. Backspace in
        an indent of spaces goes back to the stop before; between a pair auto_close made it takes both.
*/
static b32 doc_delete(struct doc address_to doc, b32 forward, p32 reach)
{
        struct doc_edit address_to edits;
        positive kept = 0;

        if (doc_any_selected(doc))
                return doc_erase_selections(doc);
        edits = doc_edits_room(doc->selection_count);
        if (!edits)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 at = doc->selections[s].active, other, line = doc_line_of(doc, at), line_start = doc_line_start(doc, line);

                if (forward)
                        other = reach == DOC_REACH_WORD ? doc_word_right(doc, at)
                                : reach == DOC_REACH_LINE && doc_line_end(doc, line) > at ? doc_line_end(doc, line)
                                                                                         : doc_cluster_end(doc, at);
                else if (reach == DOC_REACH_WORD)
                        other = doc_word_left(doc, at);
                else if (reach == DOC_REACH_LINE)
                        other = at > line_start ? line_start : doc_cluster_start(doc, at);
                else
                {
                        other = doc_backspace_start(doc, at);
                        if (doc->insert_spaces && at > line_start && doc_first_text(doc, line) >= at && doc_byte(doc, at - 1) == ' ')
                                other = line_start + (at - line_start - 1) / doc->tab_size * doc->tab_size;
                        if (doc->auto_close && other + 1 == at && at < doc_length(doc))
                        {
                                p32 before = doc_byte(doc, other), after = doc_byte(doc, at);
                                const char address_to which = memory_first_of((address_any)doc_opens, (b8)before, 6);

                                for (p32 c = 0; which && c < doc->closer_count; c++)
                                        if (doc->closers[c] == at && after == (p8)doc_closes[which - doc_opens])
                                        {
                                                edits[kept++] = (struct doc_edit){other, 2, null, 0, 0, 0};
                                                other = DOC_NONE;
                                                break;
                                        }
                                if (other == DOC_NONE)
                                        continue;
                        }
                }
                if (kept && (forward ? at : other) < edits[kept - 1].at + edits[kept - 1].removed)
                {
                        // Two carets that reach into each other's deletes are one delete.
                        p64 finish = forward ? other : at;

                        if (finish > edits[kept - 1].at + edits[kept - 1].removed)
                                edits[kept - 1].removed = finish - edits[kept - 1].at;
                        continue;
                }
                edits[kept++] = forward ? (struct doc_edit){at, other - at, null, 0, 0, 0}
                                        : (struct doc_edit){other, at - other, null, 0, 0, 0};
        }
        if (kept != doc->selection_count)
                for (positive k = 0; k < kept; k++)
                        edits[k].anchor = edits[k].active = DOC_NONE;
        return doc_apply(doc, edits, kept, forward ? DOC_KIND_FORWARD : DOC_KIND_BACK, 0);
}

// The leading blanks of line, at most room bytes of them: how many.
static positive doc_indent_of(const struct doc address_to doc, p64 line, p8 address_to into, positive room)
{
        p64 start = doc_line_start(doc, line), finish = doc_first_text(doc, line);

        return doc_read(doc, start, into, finish - start < room ? (positive)(finish - start) : room);
}

/*
        Enter at every caret: the file's line end and the line's indent, one stop more after an opening bracket, and
        between a pair of brackets the closer on a line of its own after the caret's. A field takes none.
*/
static b32 doc_newline(struct doc address_to doc)
{
        struct doc_edit address_to edits = doc_edits_room(doc->selection_count);
        positive used = 0, eol_length = doc_eol_length(doc), most = doc->selection_count * (2 * eol_length + 2 * 256 + 64);
        const char address_to eol = doc_eol_text(doc);
        p8 address_to texts = doc_bytes_room(most);

        if (doc->single_line)
                return false;
        if (!edits || !texts)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                const struct doc_selection address_to sel = &doc->selections[s];
                p64 start = doc_sel_start(sel), finish = doc_sel_end(sel), line = doc_line_of(doc, start);
                p8 indent[256];
                positive indent_length = doc_indent_of(doc, line, indent, sizeof(indent)), from = used, caret;
                p32 before = start ? doc_byte(doc, start - 1) : 0, after = doc_byte(doc, finish);
                b32 opened = before == '{' || before == '(' || before == '[';
                b32 between = (before == '{' && after == '}') || (before == '(' && after == ')') || (before == '[' && after == ']');

                if (start - doc_line_start(doc, line) < indent_length)
                        indent_length = (positive)(start - doc_line_start(doc, line));
                memory_copy(texts + used, eol, eol_length), used += eol_length;
                memory_copy(texts + used, indent, indent_length), used += indent_length;
                if (opened)
                {
                        if (doc->insert_spaces)
                                for (p32 i = 0; i < doc->tab_size && i < 16; i++)
                                        texts[used++] = ' ';
                        else
                                texts[used++] = '\t';
                }
                caret = used - from;
                if (between)
                {
                        memory_copy(texts + used, eol, eol_length), used += eol_length;
                        memory_copy(texts + used, indent, indent_length), used += indent_length;
                }
                edits[s] = (struct doc_edit){start, finish - start, texts + from, used - from, caret, caret};
        }
        doc->closer_count = 0;
        return doc_apply(doc, edits, doc->selection_count, DOC_KIND_TYPING, '\n');
}

/*
        The lines the selections touch, as runs of whole lines: the first and last line of each, a selection ending at
        a line's very start not taking that line, runs that meet joined. How many, into room of them.
*/
static positive doc_line_runs(const struct doc address_to doc, p64 (address_to runs)[2], positive room)
{
        positive count = 0;

        for (positive s = 0; s < doc->selection_count; s++)
        {
                const struct doc_selection address_to sel = &doc->selections[s];
                p64 first = doc_line_of(doc, doc_sel_start(sel)), last = doc_line_of(doc, doc_sel_end(sel));

                if (last > first && doc_line_start(doc, last) == doc_sel_end(sel))
                        last--;
                if (count && first <= runs[count - 1][1] + 1)
                {
                        if (last > runs[count - 1][1])
                                runs[count - 1][1] = last;
                        continue;
                }
                if (count == room)
                        break;
                runs[count][0] = first;
                runs[count][1] = last;
                count++;
        }
        return count;
}

static p64 (address_to doc_runs_room(positive count))[2]
{
        static p64 (address_to room)[2];
        static positive held;

        if (!array_store_reserve(room, held, 0, count ? count : 1, 64))
                return null;
        return room;
}

// Every line the selections touch indented by one stop, or outdented by one (a tab, or up to a stop of spaces).
static b32 doc_indent(struct doc address_to doc, b32 outdent)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0, edit_count = 0, lines = 0;
        struct doc_edit address_to edits;
        static p8 stop[64];

        for (positive r = 0; r < count; r++)
                lines += runs[r][1] - runs[r][0] + 1;
        edits = doc_edits_room(lines);
        if (!edits || !runs)
                return false;
        memory_fill(stop, ' ', sizeof(stop));
        stop[0] = doc->insert_spaces ? ' ' : '\t';
        for (positive r = 0; r < count; r++)
                for (p64 line = runs[r][0]; line <= runs[r][1]; line++)
                {
                        p64 start = doc_line_start(doc, line), finish = doc_line_end(doc, line), at = start;

                        if (!outdent)
                        {
                                if (finish == start && runs[r][0] != runs[r][1])
                                        continue;
                                edits[edit_count++] =
                                    (struct doc_edit){start, 0, stop, doc->insert_spaces ? (doc->tab_size < 64 ? doc->tab_size : 64) : 1,
                                                      DOC_NONE, DOC_NONE};
                                continue;
                        }
                        if (at < finish && doc_byte(doc, at) == '\t')
                                at++;
                        else
                                while (at < finish && at - start < doc->tab_size && doc_byte(doc, at) == ' ')
                                        at++;
                        if (at > start)
                                edits[edit_count++] = (struct doc_edit){start, at - start, null, 0, DOC_NONE, DOC_NONE};
                }
        doc_seal(doc);
        return doc_apply(doc, edits, edit_count, DOC_KIND_OTHER, 0);
}

/*
        The text of lines first to last as a block that ends in a line end: the last line's own, or the file's when
        the last line is the file's and has none (had_end says which). Into bytes from at: its length.
*/
static positive doc_block(const struct doc address_to doc, p64 first, p64 last, p8 address_to into, b32 address_to had_end)
{
        p64 start = doc_line_start(doc, first), finish = doc_line_start(doc, last + 1);
        positive length = doc_read(doc, start, into, (positive)(finish - start));

        *had_end = last + 1 < doc_line_count(doc);
        if (!*had_end)
        {
                memory_copy(into + length, doc_eol_text(doc), doc_eol_length(doc));
                length += doc_eol_length(doc);
        }
        return length;
}

// The length of a block's last line end.
static positive doc_block_eol(const p8 address_to block, positive length)
{
        if (length >= 2 && block[length - 2] == '\r' && block[length - 1] == '\n')
                return 2;
        return length && (block[length - 1] == '\n' || block[length - 1] == '\r') ? 1 : 0;
}

/*
        The lines the selections touch moved up (down false) or down past the line beside them: the run and that line
        swapped as one replace, each selection going with its run. Nothing moves when a run is at the edge. The file's
        last line has no line end, so when it is in a swap the line ends are given and taken to keep it so.
*/
static b32 doc_move_lines(struct doc address_to doc, b32 down)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0, used = 0, total = 0;
        struct doc_edit address_to edits = doc_edits_room(count);
        p64 lines = doc_line_count(doc), delta = 0;
        p8 address_to texts;
        struct doc_selection address_to before = null;
        positive before_room = 0, n = doc->selection_count;
        p64 run_old[64], run_new[64];
        b32 done;

        if (!edits || !count || count > 64)
                return false;
        for (positive r = 0; r < count; r++)
        {
                if (down ? runs[r][1] + 1 >= lines : runs[r][0] == 0)
                        return false;
                total += doc_line_start(doc, runs[r][1] + 2) - doc_line_start(doc, runs[r][0] - (down ? 0 : 1)) + 4;
        }
        texts = doc_bytes_room(total);
        if (!texts || !array_store_reserve(before, before_room, 0, n, 16))
                return false;
        memory_copy(before, doc->selections, n * sizeof(*before));
        for (positive r = 0; r < count; r++)
        {
                p64 run_first = runs[r][0], run_last = runs[r][1];
                p64 other = down ? run_last + 1 : run_first - 1;
                p64 first = down ? run_first : other, last = down ? other : run_last;
                p64 start = doc_line_start(doc, first), finish = doc_line_start(doc, last + 1);
                b32 run_end, other_end, ends_file = last + 1 >= lines;
                positive run_length, other_length;

                // The two blocks laid side by side in their new order, each ending in a line end, and the very last
                // line end taken off again when the swap ends the file.
                {
                        p8 address_to scratch = texts + used;
                        positive put = 0;

                        if (down)
                        {
                                other_length = doc_block(doc, other, other, scratch, &other_end);
                                put += other_length;
                                run_new[r] = put;
                                run_length = doc_block(doc, run_first, run_last, scratch + put, &run_end);
                                put += run_length;
                        }
                        else
                        {
                                run_new[r] = 0;
                                run_length = doc_block(doc, run_first, run_last, scratch, &run_end);
                                put += run_length;
                                other_length = doc_block(doc, other, other, scratch + put, &other_end);
                                put += other_length;
                        }
                        if (ends_file)
                                put -= doc_block_eol(scratch, put);
                        used += put;
                        run_old[r] = doc_line_start(doc, run_first);
                        edits[r] = (struct doc_edit){start, finish - start, scratch, put, DOC_NONE, DOC_NONE};
                }
        }
        doc_seal(doc);
        if (!doc_group_begin(doc, DOC_KIND_OTHER, 0))
        {
                array_store_release(before, before_room, n);
                return false;
        }
        done = doc_apply(doc, edits, count, DOC_KIND_OTHER, 0);
        if (done)
        {
                // Each selection where its run went, its place in the run kept.
                positive r = 0;

                for (positive s = 0; s < n; s++)
                {
                        while (r + 1 < count && doc_sel_start(&before[s]) >= run_old[r + 1])
                                r++;
                        delta = 0;
                        for (positive k = 0; k < r; k++)
                                delta += edits[k].length - edits[k].removed;
                        before[s].anchor = edits[r].at + delta + run_new[r] + (before[s].anchor - run_old[r]);
                        before[s].active = edits[r].at + delta + run_new[r] + (before[s].active - run_old[r]);
                }
                memory_copy(doc->selections, before, n * sizeof(*before));
                doc->selection_count = n;
                doc_normalize(doc);
        }
        doc_group_end(doc, 0);
        doc_seal(doc);
        array_store_release(before, before_room, n);
        return done;
}

// The lines the selections touch copied below themselves (above false), the selections on the copy below.
static b32 doc_duplicate_lines(struct doc address_to doc, b32 above)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0, used = 0, total = 0;
        struct doc_edit address_to edits = doc_edits_room(count);
        p64 lines = doc_line_count(doc);
        p8 address_to texts;
        struct doc_selection address_to before = null;
        positive before_room = 0, n = doc->selection_count;
        p64 shift[64], run_old[64];
        b32 done;

        if (!edits || !count || count > 64)
                return false;
        for (positive r = 0; r < count; r++)
                total += doc_line_start(doc, runs[r][1] + 1) - doc_line_start(doc, runs[r][0]) + 4;
        texts = doc_bytes_room(total);
        if (!texts || !array_store_reserve(before, before_room, 0, n, 16))
                return false;
        memory_copy(before, doc->selections, n * sizeof(*before));
        for (positive r = 0; r < count; r++)
        {
                b32 had_end;
                positive length = doc_block(doc, runs[r][0], runs[r][1], texts + used, &had_end);
                p64 start = doc_line_start(doc, runs[r][0]);

                run_old[r] = start;
                if (above || had_end)
                {
                        // The copy before the run: the run itself moves down by the copy, and the selections stay on it
                        // when copying up, or follow to the lower one when copying down.
                        edits[r] = (struct doc_edit){start, 0, texts + used, length, DOC_NONE, DOC_NONE};
                        shift[r] = above ? 0 : length;
                }
                else
                {
                        // The file's last line copied down: a line end and the copy after it.
                        p8 address_to block = texts + used;
                        positive eol = doc_block_eol(block, length);

                        memory_copy(block + eol, block, length - eol);
                        memory_copy(block, doc_eol_text(doc), eol);
                        edits[r] = (struct doc_edit){doc_length(doc), 0, block, length, DOC_NONE, DOC_NONE};
                        shift[r] = doc_length(doc) - start + eol;
                }
                used += length;
                (void)lines;
        }
        doc_seal(doc);
        if (!doc_group_begin(doc, DOC_KIND_OTHER, 0))
        {
                array_store_release(before, before_room, n);
                return false;
        }
        done = doc_apply(doc, edits, count, DOC_KIND_OTHER, 0);
        if (done)
        {
                positive r = 0;
                p64 delta = 0;

                for (positive s = 0; s < n; s++)
                {
                        while (r + 1 < count && doc_sel_start(&before[s]) >= run_old[r + 1])
                                r++;
                        delta = 0;
                        for (positive k = 0; k < r; k++)
                                delta += edits[k].length;
                        before[s].anchor += delta + shift[r];
                        before[s].active += delta + shift[r];
                }
                memory_copy(doc->selections, before, n * sizeof(*before));
                doc->selection_count = n;
                doc_normalize(doc);
        }
        doc_group_end(doc, 0);
        doc_seal(doc);
        array_store_release(before, before_room, n);
        return done;
}

// The lines the selections touch taken out whole, each caret on the line that comes up at its column's start.
static b32 doc_delete_lines(struct doc address_to doc)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0;
        struct doc_edit address_to edits = doc_edits_room(count);
        p64 lines = doc_line_count(doc);

        if (!edits || !count)
                return false;
        for (positive r = 0; r < count; r++)
        {
                p64 start = doc_line_start(doc, runs[r][0]), finish = doc_line_start(doc, runs[r][1] + 1);

                // The file's last lines: the line end before them goes, not one after.
                if (runs[r][1] + 1 >= lines && runs[r][0] > 0)
                        start = doc_line_end(doc, runs[r][0] - 1);
                edits[r] = (struct doc_edit){start, finish - start, null, 0, DOC_NONE, DOC_NONE};
        }
        doc_seal(doc);
        if (count == doc->selection_count)
                for (positive r = 0; r < count; r++)
                        edits[r].anchor = edits[r].active = 0;
        return doc_apply(doc, edits, count, DOC_KIND_OTHER, 0);
}

/*
        Lines joined: a caret's line with the next, a selection's lines with each other, the line end and the next
        line's leading blanks made one blank (none when either side is empty or already ends in one).
*/
static b32 doc_join_lines(struct doc address_to doc)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0, edit_count = 0, total = 0;
        struct doc_edit address_to edits;
        static const p8 blank = ' ';
        p64 lines = doc_line_count(doc);

        for (positive r = 0; r < count; r++)
                total += runs[r][1] - runs[r][0] + 1;
        edits = doc_edits_room(total);
        if (!edits || !runs)
                return false;
        for (positive r = 0; r < count; r++)
        {
                p64 last = runs[r][1] > runs[r][0] ? runs[r][1] : runs[r][0] + 1;

                for (p64 line = runs[r][0]; line < last && line + 1 < lines; line++)
                {
                        p64 finish = doc_line_end(doc, line), next = doc_first_text(doc, line + 1);
                        b32 empty_next = next == doc_line_end(doc, line + 1);
                        p32 before = finish > doc_line_start(doc, line) ? doc_byte(doc, finish - 1) : ' ';

                        edits[edit_count++] = (struct doc_edit){finish, next - finish, &blank, empty_next || before == ' ' || before == '\t' ? 0 : 1,
                                                                DOC_NONE, DOC_NONE};
                }
        }
        doc_seal(doc);
        return doc_apply(doc, edits, edit_count, DOC_KIND_OTHER, 0);
}

// The clusters either side of each caret swapped (at a line's end, the two before it), the caret after them.
static b32 doc_transpose(struct doc address_to doc)
{
        struct doc_edit address_to edits = doc_edits_room(doc->selection_count);
        positive kept = 0;
        p8 address_to texts = doc_bytes_room(doc->selection_count * 128);

        if (!edits || !texts)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 at = doc->selections[s].active, line = doc_line_of(doc, at), a, b, c;

                if (doc->selections[s].anchor != at)
                        continue;
                if (at == doc_line_end(doc, line))
                        at = doc_cluster_start(doc, at);
                if (at <= doc_line_start(doc, line) || at >= doc_line_end(doc, line))
                        continue;
                a = doc_cluster_start(doc, at), b = at, c = doc_cluster_end(doc, at);
                if (c - a > 128 || (kept && a < edits[kept - 1].at + edits[kept - 1].removed))
                        continue;
                doc_read(doc, b, texts + s * 128, (positive)(c - b));
                doc_read(doc, a, texts + s * 128 + (c - b), (positive)(b - a));
                edits[kept++] = (struct doc_edit){a, c - a, texts + s * 128, c - a, DOC_NONE, DOC_NONE};
        }
        if (!kept)
                return false;
        doc_seal(doc);
        return doc_apply(doc, edits, kept, DOC_KIND_OTHER, 0);
}

// A line put above (or below) each caret's line with its indent, the caret on it.
static b32 doc_insert_line(struct doc address_to doc, b32 above)
{
        struct doc_edit address_to edits = doc_edits_room(doc->selection_count);
        p8 address_to texts = doc_bytes_room(doc->selection_count * 260);
        positive kept = 0, eol_length = doc_eol_length(doc);

        if (!edits || !texts || doc->single_line)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 line = doc_line_of(doc, doc->selections[s].active);
                p8 address_to text = texts + s * 260;
                positive indent = doc_indent_of(doc, line, above ? text : text + eol_length, 250);

                if (kept && doc_line_of(doc, edits[kept - 1].at) == line)
                        continue;
                if (above)
                {
                        memory_copy(text + indent, doc_eol_text(doc), eol_length);
                        edits[kept++] = (struct doc_edit){doc_line_start(doc, line), 0, text, indent + eol_length, indent, indent};
                }
                else
                {
                        memory_copy(text, doc_eol_text(doc), eol_length);
                        edits[kept++] = (struct doc_edit){doc_line_end(doc, line), 0, text, indent + eol_length,
                                                          indent + eol_length, indent + eol_length};
                }
        }
        if (kept != doc->selection_count)
                for (positive k = 0; k < kept; k++)
                        edits[k].anchor = edits[k].active = DOC_NONE;
        doc_seal(doc);
        return doc_apply(doc, edits, kept, DOC_KIND_OTHER, 0);
}

/*
        A line comment of prefix put on or taken off every line the selections touch: taken off when every line that
        has text has it after its indent (with one blank after it, when there is one), put on at the least indent of
        them otherwise. Lines of blanks are left alone.
*/
static b32 doc_comment(struct doc address_to doc, const char address_to prefix)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0, lines = 0, edit_count = 0;
        positive size = prefix ? string_length((string_address)prefix) : 0;
        struct doc_edit address_to edits;
        static p8 with_blank[16];

        if (!size || size > 14)
                return false;
        for (positive r = 0; r < count; r++)
                lines += runs[r][1] - runs[r][0] + 1;
        edits = doc_edits_room(lines);
        if (!edits || !runs)
                return false;
        memory_copy(with_blank, prefix, size);
        with_blank[size] = ' ';
        for (positive r = 0; r < count; r++)
        {
                p64 least = DOC_NONE;
                b32 all = true;

                for (p64 line = runs[r][0]; line <= runs[r][1] && all; line++)
                {
                        p64 text = doc_first_text(doc, line);
                        p8 head[16];

                        if (text == doc_line_end(doc, line))
                                continue;
                        all = doc_read(doc, text, head, size) == size && !memory_compare(head, prefix, size);
                }
                for (p64 line = runs[r][0]; line <= runs[r][1]; line++)
                {
                        p64 text = doc_first_text(doc, line), indent = text - doc_line_start(doc, line);

                        if (text != doc_line_end(doc, line) && indent < least)
                                least = indent;
                }
                for (p64 line = runs[r][0]; line <= runs[r][1] && least != DOC_NONE; line++)
                {
                        p64 text = doc_first_text(doc, line);

                        if (text == doc_line_end(doc, line))
                                continue;
                        if (!all)
                                edits[edit_count++] = (struct doc_edit){doc_line_start(doc, line) + least, 0, with_blank, size + 1, DOC_NONE, DOC_NONE};
                        else
                                edits[edit_count++] = (struct doc_edit){text, size + (doc_byte(doc, text + size) == ' '), null, 0, DOC_NONE, DOC_NONE};
                }
        }
        doc_seal(doc);
        return doc_apply(doc, edits, edit_count, DOC_KIND_OTHER, 0);
}

// The case of every selection (a caret: the word at it) changed: upper, lower, or title (a word's first letter up).
#define DOC_CASE_UPPER 0
#define DOC_CASE_LOWER 1
#define DOC_CASE_TITLE 2

static b32 doc_case(struct doc address_to doc, p32 mode)
{
        struct doc_edit address_to edits = doc_edits_room(doc->selection_count);
        positive total = 0, used = 0;
        p8 address_to texts;
        b32 done;

        if (!edits)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 start = doc_sel_start(&doc->selections[s]), finish = doc_sel_end(&doc->selections[s]);

                if (start == finish)
                        doc_word_at(doc, start, &start, &finish);
                edits[s] = (struct doc_edit){start, finish - start, null, 0, 0, finish - start};
                total += (finish - start) * 2 + 4;
        }
        texts = doc_bytes_room(total);
        if (!texts)
                return false;
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p8 address_to from = texts + used + edits[s].removed + 4;
                positive length = doc_read(doc, edits[s].at, from, (positive)edits[s].removed), out = 0;
                b32 word_start = true;

                // Each code point mapped one to one; a byte that is not UTF-8 is kept as it is.
                for (positive at = 0; at < length;)
                {
                        p32 code, size = doc_decode(from + at, length - at, &code), mapped;

                        if (code & DOC_INVALID)
                        {
                                texts[used + out++] = from[at++];
                                word_start = false;
                                continue;
                        }
                        mapped = (p32)unicode_case(code, mode == DOC_CASE_UPPER || (mode == DOC_CASE_TITLE && word_start) ? UNICODE_CASE_UPPER
                                                                                                                         : UNICODE_CASE_LOWER);
                        word_start = doc_class(doc, code) != DOC_CLASS_WORD;
                        // A mapping that would grow past the room the text had is not made.
                        if (out + 4 > edits[s].removed * 2 + 4)
                                mapped = code;
                        out += memory_utf8_encode(texts + used + out, 4, mapped);
                        at += size;
                }
                edits[s].text = texts + used;
                edits[s].length = out;
                edits[s].active = out;
                edits[s].anchor = 0;
                if (doc_sel_start(&doc->selections[s]) == doc_sel_end(&doc->selections[s]))
                        edits[s].anchor = edits[s].active = doc->selections[s].active - edits[s].at;
                used += out;
        }
        doc_seal(doc);
        done = doc_apply(doc, edits, doc->selection_count, DOC_KIND_OTHER, 0);
        return done;
}

/*
        The lines a selection holds sorted, by their bytes (descending when asked), each selection's lines on their
        own; a selection on one line sorts nothing. Equal lines keep their order.
*/
static b32 doc_sort_lines(struct doc address_to doc, b32 descending)
{
        p64 (address_to runs)[2] = doc_runs_room(doc->selection_count);
        positive count = runs ? doc_line_runs(doc, runs, doc->selection_count) : 0, total = 0, used = 0, edit_count = 0;
        struct doc_edit address_to edits = doc_edits_room(count);
        p8 address_to texts;
        p64 address_to starts = null;
        positive starts_room = 0;

        if (!edits || !count)
                return false;
        for (positive r = 0; r < count; r++)
                total += doc_line_start(doc, runs[r][1] + 1) - doc_line_start(doc, runs[r][0]) + 4;
        texts = doc_bytes_room(total * 2);
        if (!texts)
                return false;
        for (positive r = 0; r < count; r++)
        {
                p64 first = runs[r][0], last = runs[r][1], line_count = last - first + 1;
                p8 address_to block = texts + total + used;
                b32 had_end;
                positive length, out = 0;

                if (line_count < 2)
                        continue;
                length = doc_block(doc, first, last, block, &had_end);
                if (!array_store_reserve(starts, starts_room, 0, line_count + 1, 64))
                        return false;
                // The lines' starts in the block, sorted by insertion (stable), then laid out again in that order.
                {
                        positive n = 0;

                        for (positive at = 0; at < length && n < line_count; n++)
                        {
                                starts[n] = at;
                                while (at < length && block[at] != '\n' && block[at] != '\r')
                                        at++;
                                at += at < length ? (block[at] == '\r' && at + 1 < length && block[at + 1] == '\n' ? 2 : 1) : 0;
                        }
                        starts[n] = length;
                        for (positive i = 1; i < n; i++)
                        {
                                p64 moving = starts[i];
                                positive j = i;

                                while (j)
                                {
                                        p64 a = starts[j - 1], b = moving;
                                        positive la = 0, lb = 0;
                                        b32 cmp;

                                        while (a + la < length && block[a + la] != '\n' && block[a + la] != '\r')
                                                la++;
                                        while (b + lb < length && block[b + lb] != '\n' && block[b + lb] != '\r')
                                                lb++;
                                        {
                                                b32 c = memory_compare(block + a, block + b, la < lb ? la : lb);

                                                cmp = c ? c : (b32)(la > lb) - (b32)(la < lb);
                                        }
                                        if (descending ? cmp >= 0 : cmp <= 0)
                                                break;
                                        starts[j] = starts[j - 1];
                                        j--;
                                }
                                starts[j] = moving;
                        }
                        for (positive i = 0; i < n; i++)
                        {
                                positive la = 0;

                                while (starts[i] + la < length && block[starts[i] + la] != '\n' && block[starts[i] + la] != '\r')
                                        la++;
                                memory_copy(texts + used + out, block + starts[i], la);
                                out += la;
                                memory_copy(texts + used + out, doc_eol_text(doc), doc_eol_length(doc));
                                out += doc_eol_length(doc);
                        }
                }
                if (!had_end)
                        out -= doc_eol_length(doc);
                {
                        p64 start = doc_line_start(doc, first);
                        p64 finish = had_end ? doc_line_start(doc, last + 1) : doc_length(doc);

                        edits[edit_count++] = (struct doc_edit){start, finish - start, texts + used, out, DOC_NONE, DOC_NONE};
                }
                used += out;
        }
        {
                positive none = 0;

                array_store_release(starts, starts_room, none);
        }
        if (!edit_count)
                return false;
        doc_seal(doc);
        return doc_apply(doc, edits, edit_count, DOC_KIND_OTHER, 0);
}

/* ---- moving and selecting ---- */

// Every selection moved by a step: its active end to where step says, the anchor with it unless extend.
static fn doc_move(struct doc address_to doc, p64 (address_to step)(const struct doc address_to, p64), b32 extend)
{
        for (positive s = 0; s < doc->selection_count; s++)
        {
                struct doc_selection address_to sel = &doc->selections[s];

                sel->active = step(doc, sel->active);
                if (!extend)
                        sel->anchor = sel->active;
                sel->goal = -1;
        }
        doc_normalize(doc);
        doc_seal(doc);
        doc->closer_count = 0;
}

static p64 doc_step_right(const struct doc address_to doc, p64 at)
{
        return doc_cluster_end(doc, at);
}

static p64 doc_step_left(const struct doc address_to doc, p64 at)
{
        return doc_cluster_start(doc, at);
}

/*
        Left and Right: a caret steps a cluster; without Shift a selection collapses to the end the key points at
        rather than stepping.
*/
static fn doc_move_horizontal(struct doc address_to doc, b32 right, b32 word, b32 extend)
{
        if (!extend && !word && doc_any_selected(doc))
        {
                for (positive s = 0; s < doc->selection_count; s++)
                {
                        struct doc_selection address_to sel = &doc->selections[s];

                        sel->anchor = sel->active = right ? doc_sel_end(sel) : doc_sel_start(sel);
                        sel->goal = -1;
                }
                doc_normalize(doc);
                doc_seal(doc);
                return;
        }
        doc_move(doc, word ? (right ? doc_word_right : doc_word_left) : (right ? doc_step_right : doc_step_left), extend);
}

// Home: to the line's first text, and from there (or from before it) to the line's start. End: to the line's end.
static p64 doc_step_home(const struct doc address_to doc, p64 at)
{
        p64 line = doc_line_of(doc, at), text = doc_first_text(doc, line);

        return at == text ? doc_line_start(doc, line) : text;
}

static p64 doc_step_end(const struct doc address_to doc, p64 at)
{
        return doc_line_end(doc, doc_line_of(doc, at));
}

static p64 doc_step_top(const struct doc address_to doc, p64 at)
{
        (void)doc, (void)at;
        return 0;
}

static p64 doc_step_bottom(const struct doc address_to doc, p64 at)
{
        (void)at;
        return doc_length(doc);
}

/*
        Up and Down by lines, in display columns: each caret keeps the column it had when it began going up or down
        (its goal), so it comes back to it past shorter lines. At the first line Up goes to the start, at the last Down
        to the end.
*/
static fn doc_move_vertical(struct doc address_to doc, b64 lines, b32 extend)
{
        p64 count = doc_line_count(doc);

        if (!extend && doc_any_selected(doc) && (lines == 1 || lines == -1))
                for (positive s = 0; s < doc->selection_count; s++)
                {
                        struct doc_selection address_to sel = &doc->selections[s];

                        sel->active = sel->anchor = lines < 0 ? doc_sel_start(sel) : doc_sel_end(sel);
                }
        for (positive s = 0; s < doc->selection_count; s++)
        {
                struct doc_selection address_to sel = &doc->selections[s];
                p64 line = doc_line_of(doc, sel->active);
                b64 goal = sel->goal >= 0 ? sel->goal : (b64)doc_column(doc, sel->active);
                b64 target = (b64)line + lines;

                if (target < 0)
                        sel->active = 0;
                else if ((p64)target >= count)
                        sel->active = doc_length(doc);
                else
                        sel->active = doc_at_column(doc, (p64)target, (p64)goal);
                if (!extend)
                        sel->anchor = sel->active;
                sel->goal = goal;
        }
        doc_normalize(doc);
        doc_seal(doc);
        doc->closer_count = 0;
}

static fn doc_select_all(struct doc address_to doc)
{
        doc_select_set(doc, 0, doc_length(doc));
}

// Every selection made whole lines, line ends included; a second press takes the next line too.
static fn doc_select_lines(struct doc address_to doc)
{
        for (positive s = 0; s < doc->selection_count; s++)
        {
                struct doc_selection address_to sel = &doc->selections[s];
                p64 first = doc_line_of(doc, doc_sel_start(sel)), last = doc_line_of(doc, doc_sel_end(sel));
                p64 start = doc_line_start(doc, first);

                if (doc_sel_end(sel) == doc_line_start(doc, last) && last > first)
                        last--;
                if (sel->anchor == start && sel->active == doc_line_start(doc, last + 1) && sel->active > start)
                        last++;
                sel->anchor = start;
                sel->active = doc_line_start(doc, last + 1);
                if (sel->active == doc_length(doc) && last + 1 >= doc_line_count(doc))
                        sel->active = doc_length(doc);
        }
        doc_normalize(doc);
        doc_seal(doc);
}

static fn doc_select_word(struct doc address_to doc)
{
        for (positive s = 0; s < doc->selection_count; s++)
        {
                struct doc_selection address_to sel = &doc->selections[s];
                p64 start, finish;

                if (sel->anchor != sel->active)
                        continue;
                doc_word_at(doc, sel->active, &start, &finish);
                sel->anchor = start, sel->active = finish;
        }
        doc_normalize(doc);
        doc_seal(doc);
}

// Esc: the selections back to the primary alone.
static fn doc_single(struct doc address_to doc)
{
        struct doc_selection primary = doc->selections[doc->primary];

        if (doc->selection_count == 1)
                primary.anchor = primary.active;
        doc->selections[0] = primary;
        doc->selection_count = 1;
        doc->primary = 0;
        doc_seal(doc);
}

/*
        Brackets: the one that matches the bracket at at (or just before it), counting nesting of its own kind, at
        most a MiB away. DOC_NONE for none.
*/
#define DOC_BRACKET_REACH 1048576u

static p64 doc_match_bracket(const struct doc address_to doc, p64 at)
{
        static const char brackets[] = "()[]{}";
        p64 length = doc_length(doc);

        for (p32 side = 0; side < 2; side++)
        {
                p64 here = side ? at - 1 : at;
                p32 byte = side && !at ? 0x100 : doc_byte(doc, here);
                const char address_to found = byte < 0x100 ? memory_first_of((address_any)brackets, (b8)byte, 6) : null;
                p32 depth = 0;
                b32 forward;
                p8 open, close;

                if (!found)
                        continue;
                forward = !((found - brackets) & 1);
                open = (p8)brackets[(found - brackets) & ~1];
                close = (p8)brackets[((found - brackets) & ~1) + 1];
                for (p64 step = 0; step < DOC_BRACKET_REACH; step++)
                {
                        p64 at_now;
                        p32 now;

                        if (forward ? here + step >= length : step > here)
                                break;
                        at_now = forward ? here + step : here - step;
                        now = doc_byte(doc, at_now);
                        if (now == open)
                                depth += forward ? 1 : (p32)-1;
                        else if (now == close)
                                depth += forward ? (p32)-1 : 1;
                        if (!depth)
                                return at_now;
                }
        }
        return DOC_NONE;
}

// Every caret to the bracket that matches the one it is at.
static fn doc_jump_bracket(struct doc address_to doc)
{
        for (positive s = 0; s < doc->selection_count; s++)
        {
                p64 match = doc_match_bracket(doc, doc->selections[s].active);

                if (match != DOC_NONE)
                        doc->selections[s].anchor = doc->selections[s].active = match;
        }
        doc_normalize(doc);
        doc_seal(doc);
}

/*
        Expand: a caret to its word, a word to what the brackets round it hold, that to the brackets too, then the line,
        then everything; shrink goes back the way expand came (only while nothing else moved the selection).
*/
static fn doc_expand(struct doc address_to doc)
{
        struct doc_selection address_to sel = &doc->selections[doc->primary];
        p64 start = doc_sel_start(sel), finish = doc_sel_end(sel), length = doc_length(doc);
        p64 new_start = 0, new_end = length;

        if (doc->expand_version != doc->version || doc->expand_now.anchor != sel->anchor || doc->expand_now.active != sel->active)
                doc->expand_count = 0;
        if (start == finish)
                doc_word_at(doc, start, &new_start, &new_end);
        if (new_start == start && new_end == finish)
        {
                // The innermost brackets round the selection: a walk back for an opener whose closer is past the end.
                p32 depth[3] = {0, 0, 0};

                new_start = 0, new_end = length;
                for (p64 at = start; at-- > 0 && start - at < DOC_BRACKET_REACH;)
                {
                        p32 byte = doc_byte(doc, at);
                        const char address_to which = memory_first_of((address_any) "([{)]}", (b8)byte, 6);
                        p32 kind;

                        if (!which)
                                continue;
                        kind = (p32)(which - "([{)]}") % 3;
                        if (which - "([{)]}" >= 3)
                                depth[kind]++;
                        else if (depth[kind])
                                depth[kind]--;
                        else
                        {
                                p64 match = doc_match_bracket(doc, at);

                                if (match != DOC_NONE && match >= finish)
                                {
                                        // The inside first; when that is what is selected already, the brackets too.
                                        if (at + 1 == start && match == finish)
                                                new_start = at, new_end = match + 1;
                                        else
                                                new_start = at + 1, new_end = match;
                                        break;
                                }
                        }
                }
                if (new_start == 0 && new_end == length)
                {
                        p64 first = doc_line_of(doc, start), last = doc_line_of(doc, finish);

                        if (start != doc_line_start(doc, first) || finish != doc_line_end(doc, last))
                                new_start = doc_line_start(doc, first), new_end = doc_line_end(doc, last);
                }
        }
        if (doc->expand_count == DOC_EXPAND)
        {
                memory_copy(doc->expanded, doc->expanded + 1, (DOC_EXPAND - 1) * sizeof(doc->expanded[0]));
                doc->expand_count--;
        }
        doc->expanded[doc->expand_count++] = *sel;
        doc->selections[0] = (struct doc_selection){new_start, new_end, -1};
        doc->selection_count = 1;
        doc->primary = 0;
        doc->expand_now = doc->selections[0];
        doc->expand_version = doc->version;
        doc_seal(doc);
}

// Shrink: back to the selection before the last expand, while nothing else has moved it.
static fn doc_shrink(struct doc address_to doc)
{
        const struct doc_selection address_to sel = &doc->selections[doc->primary];

        if (!doc->expand_count || doc->expand_version != doc->version || sel->anchor != doc->expand_now.anchor ||
            sel->active != doc->expand_now.active)
                return;
        doc->selections[0] = doc->expanded[--doc->expand_count];
        doc->selections[0].goal = -1;
        doc->selection_count = 1;
        doc->primary = 0;
        doc->expand_now = doc->selections[0];
        doc_seal(doc);
}

// A caret added on the line above (or below) each caret at the end of the list in that direction, at its column.
static fn doc_add_cursor(struct doc address_to doc, b32 below)
{
        const struct doc_selection address_to edge = &doc->selections[below ? doc->selection_count - 1 : 0];
        p64 line = doc_line_of(doc, edge->active);
        b64 goal = edge->goal >= 0 ? edge->goal : (b64)doc_column(doc, edge->active);
        p64 at;

        if (below ? line + 1 >= doc_line_count(doc) : line == 0)
                return;
        at = doc_at_column(doc, below ? line + 1 : line - 1, (p64)goal);
        if (doc_select_add(doc, at, at))
                doc->selections[doc->primary].goal = goal;
        for (positive s = 0; s < doc->selection_count; s++)
                if (doc->selections[s].goal < 0)
                        doc->selections[s].goal = goal;
}

/*
        Column (box) selection, from the anchor's line and column to the active one's: one selection on each line
        between, from the one column to the other. A line too short to reach the left column has none, unless no
        line does.
*/
static fn doc_select_box(struct doc address_to doc, p64 anchor_line, p64 anchor_column, p64 active_line, p64 active_column)
{
        p64 first = anchor_line < active_line ? anchor_line : active_line, last = anchor_line < active_line ? active_line : anchor_line;
        p64 left = anchor_column < active_column ? anchor_column : active_column;
        positive count = 0;

        last = last >= doc_line_count(doc) ? doc_line_count(doc) - 1 : last;
        if (!array_store_reserve(doc->selections, doc->selection_room, 0, last - first + 1, 16))
                return;
        for (p64 line = first; line <= last; line++)
        {
                p64 anchor = doc_at_column(doc, line, anchor_column), active = doc_at_column(doc, line, active_column);

                if (doc_column(doc, doc_line_end(doc, line)) < left && anchor_line != active_line)
                        continue;
                doc->selections[count++] = (struct doc_selection){anchor, active, (b64)active_column};
        }
        if (!count)
        {
                p64 at = doc_at_column(doc, active_line, active_column);

                doc->selections[count++] = (struct doc_selection){at, at, (b64)active_column};
        }
        doc->selection_count = count;
        doc->primary = (p32)(active_line >= anchor_line ? count - 1 : 0);
        doc_normalize(doc);
        doc_seal(doc);
}

// The caret to the start of line (from 0), the view's to show it.
static fn doc_goto_line(struct doc address_to doc, p64 line)
{
        p64 count = doc_line_count(doc);

        line = line >= count ? count - 1 : line;
        doc_select_set(doc, doc_line_start(doc, line), doc_line_start(doc, line));
}

/* ---- find and replace ---- */

#define DOC_FIND_CASE 1u   // case matters
#define DOC_FIND_WORD 2u   // a match is a whole word
#define DOC_FIND_REGEX 4u  // the query is a regular expression (POSIX extended, the system's one engine)
#define DOC_FIND_LINE_MOST 1048576u // a line longer than this is searched in its first MiB

struct doc_find
{
        p8 query[1024];
        positive length;
        p32 flags;
        b32 ready, failed;
#ifdef RX_NODE_LIMIT
        regex_program program;
        b32 kept;
#endif
};

// The query set, and the program compiled when it is a pattern or the case does not matter. False for a pattern that
// does not compile (failed then stays set, and nothing is found).
static b32 doc_find_set(struct doc_find address_to find, const p8 address_to query, positive length, p32 flags)
{
        if (length >= sizeof(find->query))
                length = sizeof(find->query) - 1;
#ifdef RX_NODE_LIMIT
        if (find->kept)
                regex_release(&find->program);
        find->kept = false;
#endif
        memory_copy(find->query, query, length);
        find->query[length] = 0;
        find->length = length;
        find->flags = flags;
        find->ready = length != 0;
        find->failed = false;
        if (!length || (!(flags & DOC_FIND_REGEX) && (flags & DOC_FIND_CASE)))
                return true;
#ifdef RX_NODE_LIMIT
        {
                // A plain query without case is a pattern too: its specials escaped, so the engine's case folding
                // (Unicode's simple folds) is the one used.
                p8 pattern[2 * sizeof(find->query) + 1];
                positive size = 0;

                if (flags & DOC_FIND_REGEX)
                        memory_copy(pattern, query, length), size = length;
                else
                        for (positive at = 0; at < length; at++)
                        {
                                if (memory_first_of((address_any) "\\.[]()*+?{}|^$", (b8)query[at], 14))
                                        pattern[size++] = '\\';
                                pattern[size++] = query[at];
                        }
                pattern[size] = 0;
                if (!regex_compile_bytes(pattern, size, true, !(flags & DOC_FIND_CASE), true, REGEX_CHARACTERS | REGEX_POLICY_DEFAULT) ||
                    !regex_keep_checked(&find->program))
                {
                        find->failed = true;
                        find->ready = false;
                        return false;
                }
                find->kept = true;
        }
#else
        if (flags & DOC_FIND_REGEX)
        {
                find->failed = true;
                find->ready = false;
                return false;
        }
#endif
        return true;
}

static fn doc_find_drop(struct doc_find address_to find)
{
#ifdef RX_NODE_LIMIT
        if (find->kept)
                regex_release(&find->program);
        find->kept = false;
#endif
        find->ready = false;
}

// Whether a match from start to end of a line's bytes is a whole word: no word character either side of it.
static b32 doc_find_whole(const struct doc address_to doc, const p8 address_to bytes, positive length, positive start, positive finish)
{
        p32 code;

        if (start)
        {
                doc_decode_back(bytes, start, &code);
                if (doc_class(doc, code) == DOC_CLASS_WORD)
                        return false;
        }
        if (finish < length)
        {
                doc_decode(bytes + finish, length - finish, &code);
                if (doc_class(doc, code) == DOC_CLASS_WORD)
                        return false;
        }
        return true;
}

/*
        The first match in a line's bytes at or after from: its start and end, and the captures' (the regex engine's
        slots) when there are any. False for none.
*/
static b32 doc_find_in(const struct doc address_to doc, struct doc_find address_to find, const p8 address_to bytes, positive length,
                       positive from, positive address_to start, positive address_to finish)
{
        while (from <= length)
        {
                positive found_start, found_end;

                if (!find->ready)
                        return false;
#ifdef RX_NODE_LIMIT
                if (find->kept)
                {
                        p8 result = rx_find(&regex_match, &find->program, REGEX_LONGEST, true, (string_address)bytes, length, from);

                        if (result != RX_MATCH)
                                return false;
                        found_start = regex_slots[0];
                        found_end = regex_slots[1];
                }
                else
#endif
                {
                        const p8 address_to hit = length - from >= find->length
                                                      ? memory_search((address_any)(bytes + from), length - from, find->query, find->length)
                                                      : null;

                        if (!hit)
                                return false;
                        found_start = (positive)(hit - bytes);
                        found_end = found_start + find->length;
                }
                if (!(find->flags & DOC_FIND_WORD) || doc_find_whole(doc, bytes, length, found_start, found_end))
                {
                        *start = found_start;
                        *finish = found_end;
                        return true;
                }
                from = found_start + 1;
                // The next search starts at a character, not inside one.
                while (from < length && (bytes[from] & 0xc0) == 0x80)
                        from++;
        }
        return false;
}

// A line's bytes, at most DOC_FIND_LINE_MOST, into a buffer kept between calls: its length.
static positive doc_line_bytes(const struct doc address_to doc, p64 line, const p8 address_to address_to bytes)
{
        static p8 address_to room;
        static positive held;
        p64 start = doc_line_start(doc, line), finish = doc_line_end(doc, line);
        positive length = finish - start > DOC_FIND_LINE_MOST ? DOC_FIND_LINE_MOST : (positive)(finish - start);

        if (!array_store_reserve(room, held, 0, length + 1, 4096))
        {
                *bytes = (const p8 address_to) "";
                return 0;
        }
        length = doc_read(doc, start, room, length);
        room[length] = 0;
        *bytes = room;
        return length;
}

/*
        The next match from at (backward: the last that starts before at), wrapping round the document when wrap: its
        start and end. False for none. Matches are found a line at a time: a pattern does not match across a line end.
*/
static b32 doc_find_next(const struct doc address_to doc, struct doc_find address_to find, p64 at, b32 backward, b32 wrap,
                         p64 address_to start, p64 address_to finish)
{
        p64 lines = doc_line_count(doc), first = doc_line_of(doc, at);

        if (!find->ready)
                return false;
        for (p64 step = 0; step <= lines; step++)
        {
                p64 line;
                const p8 address_to bytes;
                positive length, line_start, s, e, from = 0, best_s = 0, best_e = 0;
                b32 any = false;

                if (!backward)
                {
                        line = first + step;
                        if (line >= lines)
                        {
                                if (!wrap)
                                        return false;
                                line -= lines;
                        }
                }
                else
                {
                        if (step > first && !wrap)
                                return false;
                        line = step <= first ? first - step : lines - (step - first);
                }
                length = doc_line_bytes(doc, line, &bytes);
                line_start = doc_line_start(doc, line);
                if (!backward)
                {
                        from = step == 0 ? (positive)(at - line_start) : 0;
                        if (step == lines && line == first)
                                length = at - line_start < length ? (positive)(at - line_start) : length;
                        if (doc_find_in(doc, find, bytes, length, from, &s, &e))
                        {
                                *start = line_start + s;
                                *finish = line_start + e;
                                return true;
                        }
                        continue;
                }
                while (from <= length && doc_find_in(doc, find, bytes, length, from, &s, &e))
                {
                        if (step == 0 && line_start + s >= at)
                                break;
                        if (step == lines && line_start + s < at)
                        {
                                from = s + 1;
                                continue;
                        }
                        best_s = s, best_e = e, any = true;
                        from = e > s ? e : s + 1;
                }
                if (any)
                {
                        *start = line_start + best_s;
                        *finish = line_start + best_e;
                        return true;
                }
        }
        return false;
}

/*
        A replacement for the match just found, with $1 to $9 the captures, $& and $0 the match, $$ a dollar, \n a
        line end, \t a tab and \\ a backslash when the query is a pattern; the text as it is otherwise. Into room
        bytes: its length.
*/
static positive doc_find_replacement(const struct doc_find address_to find, const p8 address_to bytes, const p8 address_to with,
                                     positive with_length, p8 address_to into, positive room)
{
        positive out = 0;

        if (!(find->flags & DOC_FIND_REGEX))
        {
                with_length = with_length < room ? with_length : room;
                memory_copy(into, with, with_length);
                return with_length;
        }
        for (positive at = 0; at < with_length && out < room; at++)
        {
                p8 c = with[at], next = at + 1 < with_length ? with[at + 1] : 0;

                if (c == '\\' && (next == 'n' || next == 't' || next == '\\'))
                {
                        into[out++] = next == 'n' ? '\n' : next == 't' ? '\t' : '\\';
                        at++;
                        continue;
                }
                if (c == '$' && next == '$')
                {
                        into[out++] = '$';
                        at++;
                        continue;
                }
#ifdef RX_NODE_LIMIT
                if (c == '$' && (next == '&' || (next >= '0' && next <= '9')))
                {
                        positive group = next == '&' ? 0 : (positive)(next - '0');
                        positive s = regex_slots[group * 2], e = regex_slots[group * 2 + 1];

                        if (group <= (positive)find->program.groups && s != positive_max && e != positive_max && e >= s)
                        {
                                positive take = e - s < room - out ? e - s : room - out;

                                memory_copy(into + out, bytes + s, take);
                                out += take;
                        }
                        at++;
                        continue;
                }
#endif
                into[out++] = c;
        }
        return out;
}

/*
        Many replaces made as one: the text from the first edit's start to the last's end built again in a buffer (what
        is between the edits read from the tree, the replacements put in their places) and swapped in with one change,
        the selections mapped through the edits. Millions of matches are then one copy of the span and one undo record,
        where an edit each was a split and a join each, and as many records to undo.
*/
#define DOC_REPLACE_BULK 4096

static b32 doc_replace_bulk(struct doc address_to doc, const struct doc_edit address_to edits, positive count)
{
        p64 start = edits[0].at, finish = edits[count - 1].at + edits[count - 1].removed, at = start;
        p8 address_to span = null;
        positive room = 0, used = 0, need = 0;
        b32 done;

        for (positive i = 0; i < count; i++)
                need += edits[i].length;
        need += finish - start;
        if (!array_store_reserve(span, room, 0, need + 1, 65536))
                return false;
        for (positive i = 0; i < count; i++)
        {
                used += doc_read(doc, at, span + used, (positive)(edits[i].at - at));
                memory_copy(span + used, edits[i].text, edits[i].length);
                used += edits[i].length;
                at = edits[i].at + edits[i].removed;
        }
        if (!doc_group_begin(doc, DOC_KIND_OTHER, 0))
        {
                array_store_release(span, room, used);
                return false;
        }
        for (positive s = 0; s < doc->selection_count; s++)
        {
                struct doc_selection address_to sel = &doc->selections[s];

                sel->anchor = doc_map(sel->anchor, edits, count, sel->anchor > sel->active);
                sel->active = doc_map(sel->active, edits, count, sel->active >= sel->anchor);
        }
        {
                struct doc_selection address_to kept = null;
                positive kept_room = 0, n = doc->selection_count;

                // The swap would move every selection inside the span to its end; they are put back where the map said.
                if (array_store_reserve(kept, kept_room, 0, n, 16))
                        memory_copy(kept, doc->selections, n * sizeof(*kept));
                done = doc_replace_in(doc, start, finish - start, span, used);
                if (kept)
                {
                        memory_copy(doc->selections, kept, n * sizeof(*kept));
                        array_store_release(kept, kept_room, n);
                }
        }
        doc_normalize(doc);
        doc_group_end(doc, 0);
        array_store_release(span, room, used);
        return done;
}

/*
        Every match replaced, as one undo: the matches found a line at a time from the start, each replacement made
        (with its captures, for a pattern), and the edit list applied once from the last to the first. How many were
        replaced, which is 0 when a replacement was refused (read-only text) and nothing changed.
*/
static positive doc_replace_all(struct doc address_to doc, struct doc_find address_to find, const p8 address_to with,
                                positive with_length)
{
        struct doc_edit address_to edits = null;
        positive edit_room = 0, count = 0, text_room = 0, text_used = 0;
        p8 address_to texts = null;
        p64 lines = doc_line_count(doc);
        b32 done;

        if (!find->ready)
                return 0;
        for (p64 line = 0; line < lines; line++)
        {
                const p8 address_to bytes;
                positive length = doc_line_bytes(doc, line, &bytes), from = 0, s, e;
                p64 line_start = doc_line_start(doc, line);

                while (from <= length && doc_find_in(doc, find, bytes, length, from, &s, &e))
                {
                        positive most = with_length + (find->flags & DOC_FIND_REGEX ? 10 * (length + 1) : 0) + 16;

                        if (!array_store_reserve(edits, edit_room, count, count + 1, 1024) ||
                            !array_store_reserve(texts, text_room, text_used, text_used + most, 65536))
                        {
                                array_store_release(edits, edit_room, count);
                                array_store_release(texts, text_room, text_used);
                                return 0;
                        }
                        {
                                positive made = doc_find_replacement(find, bytes, with, with_length, texts + text_used, most);

                                edits[count++] = (struct doc_edit){line_start + s, e - s, (const p8 address_to)text_used, made, DOC_NONE, DOC_NONE};
                                text_used += made;
                        }
                        // An empty match moves on a character, or it would be found again.
                        from = e > s ? e : s + 1;
                        while (from < length && (bytes[from] & 0xc0) == 0x80)
                                from++;
                }
        }
        for (positive i = 0; i < count; i++)
                edits[i].text = texts + (positive)edits[i].text;
        doc_seal(doc);
        if (count > DOC_REPLACE_BULK)
                done = doc_replace_bulk(doc, edits, count);
        else
                done = count && doc_apply(doc, edits, count, DOC_KIND_OTHER, 0);
        doc_seal(doc);
        {
                positive replaced = done ? count : 0;

                // A release empties the count it is given.
                array_store_release(edits, edit_room, count);
                array_store_release(texts, text_room, text_used);
                return replaced;
        }
}

/*
        Select next occurrence (Ctrl+D): on a caret, the word at it; then each press adds the next match of the
        primary selection's text after the last selection, wrapping, as a new primary. A first press on a caret finds
        whole words with case, as VS Code does; one on a selection finds its text anywhere.
*/
static b32 doc_select_next(struct doc address_to doc)
{
        struct doc_selection address_to primary = &doc->selections[doc->primary];
        struct doc_find find = {0};
        p8 text[1024];
        positive length;
        p64 start, finish, after = 0;

        if (primary->anchor == primary->active)
        {
                doc_word_at(doc, primary->active, &start, &finish);
                if (start == finish)
                        return false;
                primary->anchor = start, primary->active = finish;
                doc->occurrence_whole = doc->selection_count == 1;
                doc_normalize(doc);
                doc_seal(doc);
                return true;
        }
        start = doc_sel_start(primary), finish = doc_sel_end(primary);
        if (finish - start >= sizeof(text))
                return false;
        length = doc_read(doc, start, text, (positive)(finish - start));
        doc_find_set(&find, text, length, DOC_FIND_CASE | (doc->occurrence_whole ? DOC_FIND_WORD : 0));
        for (positive s = 0; s < doc->selection_count; s++)
                after = doc_sel_end(&doc->selections[s]) > after ? doc_sel_end(&doc->selections[s]) : after;
        for (p32 tries = 0; tries < 2; tries++)
        {
                b32 taken = false;

                if (!doc_find_next(doc, &find, after, false, true, &start, &finish))
                        break;
                for (positive s = 0; s < doc->selection_count; s++)
                        taken |= doc_sel_start(&doc->selections[s]) == start && doc_sel_end(&doc->selections[s]) == finish;
                if (taken)
                {
                        after = 0;
                        if (!doc_find_next(doc, &find, after, false, false, &start, &finish))
                                break;
                        taken = false;
                        for (positive s = 0; s < doc->selection_count; s++)
                                taken |= doc_sel_start(&doc->selections[s]) == start && doc_sel_end(&doc->selections[s]) == finish;
                        // Every match is selected: nothing more to add.
                        if (taken)
                                break;
                }
                doc_find_drop(&find);
                return doc_select_add(doc, start, finish);
        }
        doc_find_drop(&find);
        return false;
}

// Every match of the primary selection's text (or of the word at a caret) selected.
static positive doc_select_all_matches(struct doc address_to doc)
{
        struct doc_selection primary = doc->selections[doc->primary];
        struct doc_find find = {0};
        p8 text[1024];
        positive length, count = 0;
        p64 start = doc_sel_start(&primary), finish = doc_sel_end(&primary), at = 0;
        b32 whole = false;

        if (start == finish)
        {
                doc_word_at(doc, start, &start, &finish);
                whole = true;
        }
        if (start == finish || finish - start >= sizeof(text))
                return 0;
        length = doc_read(doc, start, text, (positive)(finish - start));
        doc_find_set(&find, text, length, DOC_FIND_CASE | (whole ? DOC_FIND_WORD : 0));
        doc->selection_count = 0;
        while (doc_find_next(doc, &find, at, false, false, &start, &finish))
        {
                if (!array_store_reserve(doc->selections, doc->selection_room, doc->selection_count, doc->selection_count + 1, 64))
                        break;
                doc->selections[doc->selection_count++] = (struct doc_selection){start, finish, -1};
                if (start == primary.anchor || finish == primary.active)
                        doc->primary = (p32)(doc->selection_count - 1);
                count++;
                at = finish > start ? finish : start + 1;
                if (at > doc_length(doc))
                        break;
        }
        if (!doc->selection_count)
                doc->selections[doc->selection_count++] = primary;
        doc_normalize(doc);
        doc_seal(doc);
        doc_find_drop(&find);
        return count;
}

/* ---- commands and the keys that name them ---- */

/*
        What a key can do, by name: the keymap is data, a list of lines KEYS COMMAND, the defaults below and then the
        lines of /root/canvas.text.keys over them (KEYS none takes a default away). KEYS is modifiers and a key joined
        by +: ctrl, shift, alt, super, and a key's name (left, home, pageup, f3, ...) or one character. A character
        binds what the keyboard's layout types (so Ctrl+Z is undo on any layout); Shift is part of a binding only for
        a name or a letter, since a symbol often needs it to be typed at all.
*/
enum
{
        DOC_CMD_NONE,
        DOC_CMD_LEFT, DOC_CMD_RIGHT, DOC_CMD_UP, DOC_CMD_DOWN, DOC_CMD_WORD_LEFT, DOC_CMD_WORD_RIGHT, DOC_CMD_HOME,
        DOC_CMD_END, DOC_CMD_TOP, DOC_CMD_BOTTOM, DOC_CMD_PAGE_UP, DOC_CMD_PAGE_DOWN,
        DOC_CMD_SELECT_LEFT, DOC_CMD_SELECT_RIGHT, DOC_CMD_SELECT_UP, DOC_CMD_SELECT_DOWN, DOC_CMD_SELECT_WORD_LEFT,
        DOC_CMD_SELECT_WORD_RIGHT, DOC_CMD_SELECT_HOME, DOC_CMD_SELECT_END, DOC_CMD_SELECT_TOP, DOC_CMD_SELECT_BOTTOM,
        DOC_CMD_SELECT_PAGE_UP, DOC_CMD_SELECT_PAGE_DOWN,
        DOC_CMD_BACKSPACE, DOC_CMD_DELETE, DOC_CMD_DELETE_WORD_LEFT, DOC_CMD_DELETE_WORD_RIGHT, DOC_CMD_DELETE_LINE_LEFT,
        DOC_CMD_DELETE_LINE_RIGHT, DOC_CMD_NEWLINE, DOC_CMD_TAB, DOC_CMD_OUTDENT, DOC_CMD_INDENT,
        DOC_CMD_MOVE_LINE_UP, DOC_CMD_MOVE_LINE_DOWN, DOC_CMD_COPY_LINE_UP, DOC_CMD_COPY_LINE_DOWN, DOC_CMD_DELETE_LINES,
        DOC_CMD_JOIN_LINES, DOC_CMD_TRANSPOSE, DOC_CMD_LINE_BELOW, DOC_CMD_LINE_ABOVE, DOC_CMD_COMMENT, DOC_CMD_UPPER,
        DOC_CMD_LOWER, DOC_CMD_TITLE, DOC_CMD_SORT, DOC_CMD_SORT_DESCENDING,
        DOC_CMD_UNDO, DOC_CMD_REDO, DOC_CMD_CUT, DOC_CMD_COPY, DOC_CMD_PASTE, DOC_CMD_SELECT_ALL, DOC_CMD_SELECT_LINE,
        DOC_CMD_EXPAND, DOC_CMD_SHRINK, DOC_CMD_CURSOR_ABOVE, DOC_CMD_CURSOR_BELOW, DOC_CMD_NEXT_MATCH, DOC_CMD_ALL_MATCHES,
        DOC_CMD_SINGLE, DOC_CMD_BRACKET, DOC_CMD_FIND, DOC_CMD_REPLACE, DOC_CMD_FIND_NEXT, DOC_CMD_FIND_PREVIOUS,
        DOC_CMD_GOTO_LINE, DOC_CMD_ZOOM_IN, DOC_CMD_ZOOM_OUT, DOC_CMD_ZOOM_RESET, DOC_CMD_WRAP, DOC_CMD_OVERWRITE,
        DOC_CMD_SAVE, DOC_CMD_OPEN, DOC_CMD_NEW, DOC_CMD_CLOSE, DOC_CMD_NEXT_TAB, DOC_CMD_PREVIOUS_TAB, DOC_CMD_MENU,
        DOC_CMD_FOLD, DOC_CMD_UNFOLD, DOC_CMD_SAVE_AS,
        DOC_CMDS
};

static const char address_to const doc_command_names[DOC_CMDS] = {
    "none", "left", "right", "up", "down", "word_left", "word_right", "home", "end", "top", "bottom", "page_up", "page_down",
    "select_left", "select_right", "select_up", "select_down", "select_word_left", "select_word_right", "select_home",
    "select_end", "select_top", "select_bottom", "select_page_up", "select_page_down", "backspace", "delete",
    "delete_word_left", "delete_word_right", "delete_line_left", "delete_line_right", "newline", "tab", "outdent", "indent",
    "move_line_up", "move_line_down", "copy_line_up", "copy_line_down", "delete_lines", "join_lines", "transpose",
    "line_below", "line_above", "comment", "upper", "lower", "title", "sort", "sort_descending", "undo", "redo", "cut",
    "copy", "paste", "select_all", "select_line", "expand", "shrink", "cursor_above", "cursor_below", "next_match",
    "all_matches", "single", "bracket", "find", "replace", "find_next", "find_previous", "goto_line", "zoom_in",
    "zoom_out", "zoom_reset", "wrap", "overwrite", "save", "open", "new", "close", "next_tab", "previous_tab", "menu",
    "fold", "unfold", "save_as",
};

/*
        The default keys: the common ones of every desktop and VS Code's, on Linux. Each line is what the keymap file
        holds, and is read by the same parser.
*/
static const char address_to const doc_default_keys[] = {
    "left left", "right right", "up up", "down down", "ctrl+left word_left", "ctrl+right word_right", "home home",
    "end end", "ctrl+home top", "ctrl+end bottom", "pageup page_up", "pagedown page_down",
    "shift+left select_left", "shift+right select_right", "shift+up select_up", "shift+down select_down",
    "ctrl+shift+left select_word_left", "ctrl+shift+right select_word_right", "shift+home select_home",
    "shift+end select_end", "ctrl+shift+home select_top", "ctrl+shift+end select_bottom",
    "shift+pageup select_page_up", "shift+pagedown select_page_down",
    "backspace backspace", "shift+backspace backspace", "delete delete", "ctrl+backspace delete_word_left",
    "ctrl+delete delete_word_right", "ctrl+shift+backspace delete_line_left", "ctrl+shift+delete delete_line_right",
    "enter newline", "shift+enter newline", "kpenter newline", "tab tab", "shift+tab outdent", "ctrl+] indent",
    "ctrl+[ outdent", "alt+up move_line_up", "alt+down move_line_down", "ctrl+shift+alt+up copy_line_up",
    "ctrl+shift+alt+down copy_line_down", "ctrl+shift+k delete_lines", "ctrl+j join_lines", "ctrl+t transpose",
    "ctrl+enter line_below", "ctrl+shift+enter line_above", "ctrl+/ comment",
    "ctrl+z undo", "ctrl+y redo", "ctrl+shift+z redo", "ctrl+x cut", "shift+delete cut", "ctrl+c copy",
    "ctrl+insert copy", "ctrl+v paste", "shift+insert paste", "ctrl+a select_all", "ctrl+l select_line",
    "shift+alt+right expand", "shift+alt+left shrink", "shift+alt+up cursor_above", "shift+alt+down cursor_below",
    "ctrl+shift+up cursor_above", "ctrl+shift+down cursor_below", "ctrl+d next_match", "ctrl+shift+l all_matches",
    "escape single", "ctrl+shift+\\ bracket", "ctrl+f find", "ctrl+h replace", "f3 find_next",
    "shift+f3 find_previous", "ctrl+g goto_line", "ctrl+= zoom_in", "ctrl++ zoom_in", "ctrl+- zoom_out",
    "ctrl+0 zoom_reset", "alt+z wrap", "insert overwrite", "ctrl+s save", "ctrl+shift+s save_as", "ctrl+o open",
    "ctrl+n new", "ctrl+w close", "ctrl+tab next_tab", "ctrl+pagedown next_tab", "ctrl+shift+tab previous_tab",
    "ctrl+pageup previous_tab", "shift+f10 menu", "menu menu", "ctrl+{ fold", "ctrl+} unfold",
};

// Linux's numbers for the keys a binding can name.
static const struct
{
        const char address_to name;
        p16 code;
} doc_key_names[] = {
    {"escape", 1}, {"backspace", 14}, {"tab", 15}, {"enter", 28}, {"space", 57}, {"f1", 59}, {"f2", 60}, {"f3", 61},
    {"f4", 62}, {"f5", 63}, {"f6", 64}, {"f7", 65}, {"f8", 66}, {"f9", 67}, {"f10", 68}, {"f11", 87}, {"f12", 88},
    {"kpenter", 96}, {"home", 102}, {"up", 103}, {"pageup", 104}, {"left", 105}, {"right", 106}, {"end", 107},
    {"down", 108}, {"pagedown", 109}, {"insert", 110}, {"delete", 111}, {"menu", 127},
};

#define DOC_MOD_SHIFT 1u
#define DOC_MOD_CONTROL 2u
#define DOC_MOD_ALT 4u
#define DOC_MOD_SUPER 8u
#define DOC_KEYS_MOST 256

struct doc_binding
{
        p16 code;      // a key by its number, or 0
        p32 character; // a key by what it types, or 0
        p8 modifiers;
        p8 command;
};

struct doc_keymap
{
        struct doc_binding bindings[DOC_KEYS_MOST];
        p32 count;
};

// One line of a keymap, KEYS COMMAND, onto it: a binding of the same keys is replaced; none takes it away. False for
// a line that is not one.
static b32 doc_keymap_line(struct doc_keymap address_to map, const char address_to line, positive length)
{
        struct doc_binding binding = {0, 0, 0, 0};
        positive at = 0, word;
        b32 known = false;

        while (at < length && (line[at] == ' ' || line[at] == '\t'))
                at++;
        if (at == length || line[at] == '#')
                return true;
        // The keys: words joined by +, a + of its own being the key itself (ctrl++).
        for (;;)
        {
                positive start = at;
                b32 last;

                while (at < length && line[at] != ' ' && line[at] != '\t' && (line[at] != '+' || at == start))
                        at++;
                word = at - start;
                last = at >= length || line[at] != '+';
                if (!word)
                        return false;
                if (!last)
                {
                        if (word == 4 && !memory_compare(line + start, "ctrl", 4))
                                binding.modifiers |= DOC_MOD_CONTROL;
                        else if (word == 5 && !memory_compare(line + start, "shift", 5))
                                binding.modifiers |= DOC_MOD_SHIFT;
                        else if (word == 3 && !memory_compare(line + start, "alt", 3))
                                binding.modifiers |= DOC_MOD_ALT;
                        else if (word == 5 && !memory_compare(line + start, "super", 5))
                                binding.modifiers |= DOC_MOD_SUPER;
                        else
                                return false;
                        at++;
                        continue;
                }
                for (p32 k = 0; k < sizeof(doc_key_names) / sizeof(doc_key_names[0]); k++)
                        if (string_length((string_address)doc_key_names[k].name) == word &&
                            !memory_compare(line + start, doc_key_names[k].name, word))
                                binding.code = doc_key_names[k].code, known = true;
                if (!known)
                {
                        p32 code;

                        if (doc_decode((const p8 address_to)line + start, word, &code) != word || (code & DOC_INVALID))
                                return false;
                        binding.character = code < 128 ? (p32)byte_to_lower((b8)code) : code;
                }
                break;
        }
        while (at < length && (line[at] == ' ' || line[at] == '\t'))
                at++;
        word = 0;
        while (at + word < length && line[at + word] != ' ' && line[at + word] != '\t' && line[at + word] != '\r')
                word++;
        for (p32 c = 0; c < DOC_CMDS; c++)
                if (string_length((string_address)doc_command_names[c]) == word && !memory_compare(line + at, doc_command_names[c], word))
                {
                        binding.command = (p8)c;
                        for (p32 b = 0; b < map->count; b++)
                                if (map->bindings[b].code == binding.code && map->bindings[b].character == binding.character &&
                                    map->bindings[b].modifiers == binding.modifiers)
                                {
                                        map->bindings[b] = binding;
                                        return true;
                                }
                        if (map->count == DOC_KEYS_MOST || !c)
                                return !c;
                        map->bindings[map->count++] = binding;
                        return true;
                }
        return false;
}

static fn doc_keymap_defaults(struct doc_keymap address_to map)
{
        map->count = 0;
        for (p32 i = 0; i < sizeof(doc_default_keys) / sizeof(doc_default_keys[0]); i++)
                doc_keymap_line(map, doc_default_keys[i], string_length((string_address)doc_default_keys[i]));
}

// A keymap file's text over the defaults, a line at a time: how many lines were not bindings (and were skipped).
static positive doc_keymap_read(struct doc_keymap address_to map, const char address_to text, positive length)
{
        positive bad = 0;

        for (positive at = 0; at < length;)
        {
                positive finish = at;

                while (finish < length && text[finish] != '\n')
                        finish++;
                bad += !doc_keymap_line(map, text + at, finish - at);
                at = finish + 1;
        }
        return bad;
}

/*
        The command a key names: by its number, or by the character it types (the control code Ctrl turns a letter
        into is that letter again). AltGr types: a key with it is a character, never a chord.
*/
static p32 doc_keymap_find(const struct doc_keymap address_to map, p32 code, p32 character, p32 modifiers)
{
        p32 typed = character && character < 32 && (modifiers & DOC_MOD_CONTROL) && code != 1 && code != 14 && code != 15 && code != 28
                        ? character | 0x60
                        : character;

        typed = typed < 128 ? (p32)byte_to_lower((b8)typed) : typed;
        for (p32 b = 0; b < map->count; b++)
        {
                const struct doc_binding address_to binding = &map->bindings[b];

                if (binding->code ? binding->code != code : !typed || binding->character != typed)
                        continue;
                if (binding->code || (binding->character >= 'a' && binding->character <= 'z')
                        ? binding->modifiers != modifiers
                        : (binding->modifiers & ~DOC_MOD_SHIFT) != (modifiers & ~DOC_MOD_SHIFT))
                        continue;
                return binding->command;
        }
        return DOC_CMD_NONE;
}

/*
        A command that needs only the document, done: the moves (by lines, since only a view knows its rows), the
        edits and the selections. Answers whether it was one of these; the rest (the clipboard, pages, find, zoom,
        files) are the view's.
*/
static b32 doc_command(struct doc address_to doc, p32 command, const char address_to comment)
{
        switch (command)
        {
        case DOC_CMD_LEFT: case DOC_CMD_SELECT_LEFT:
                doc_move_horizontal(doc, false, false, command == DOC_CMD_SELECT_LEFT);
                return true;
        case DOC_CMD_RIGHT: case DOC_CMD_SELECT_RIGHT:
                doc_move_horizontal(doc, true, false, command == DOC_CMD_SELECT_RIGHT);
                return true;
        case DOC_CMD_WORD_LEFT: case DOC_CMD_SELECT_WORD_LEFT:
                doc_move_horizontal(doc, false, true, command == DOC_CMD_SELECT_WORD_LEFT);
                return true;
        case DOC_CMD_WORD_RIGHT: case DOC_CMD_SELECT_WORD_RIGHT:
                doc_move_horizontal(doc, true, true, command == DOC_CMD_SELECT_WORD_RIGHT);
                return true;
        case DOC_CMD_UP: case DOC_CMD_SELECT_UP:
                doc_move_vertical(doc, -1, command == DOC_CMD_SELECT_UP);
                return true;
        case DOC_CMD_DOWN: case DOC_CMD_SELECT_DOWN:
                doc_move_vertical(doc, 1, command == DOC_CMD_SELECT_DOWN);
                return true;
        case DOC_CMD_HOME: case DOC_CMD_SELECT_HOME:
                doc_move(doc, doc_step_home, command == DOC_CMD_SELECT_HOME);
                return true;
        case DOC_CMD_END: case DOC_CMD_SELECT_END:
                doc_move(doc, doc_step_end, command == DOC_CMD_SELECT_END);
                return true;
        case DOC_CMD_TOP: case DOC_CMD_SELECT_TOP:
                doc_move(doc, doc_step_top, command == DOC_CMD_SELECT_TOP);
                return true;
        case DOC_CMD_BOTTOM: case DOC_CMD_SELECT_BOTTOM:
                doc_move(doc, doc_step_bottom, command == DOC_CMD_SELECT_BOTTOM);
                return true;
        case DOC_CMD_BACKSPACE:
                doc_delete(doc, false, DOC_REACH_CLUSTER);
                return true;
        case DOC_CMD_DELETE:
                doc_delete(doc, true, DOC_REACH_CLUSTER);
                return true;
        case DOC_CMD_DELETE_WORD_LEFT:
                doc_delete(doc, false, DOC_REACH_WORD);
                return true;
        case DOC_CMD_DELETE_WORD_RIGHT:
                doc_delete(doc, true, DOC_REACH_WORD);
                return true;
        case DOC_CMD_DELETE_LINE_LEFT:
                doc_delete(doc, false, DOC_REACH_LINE);
                return true;
        case DOC_CMD_DELETE_LINE_RIGHT:
                doc_delete(doc, true, DOC_REACH_LINE);
                return true;
        case DOC_CMD_NEWLINE:
                doc_newline(doc);
                return true;
        case DOC_CMD_TAB:
        {
                // Tab: indent the lines of a selection that spans lines; type a stop's worth at the carets otherwise.
                b32 lines = false;

                for (positive s = 0; s < doc->selection_count; s++)
                        lines |= doc_line_of(doc, doc->selections[s].anchor) != doc_line_of(doc, doc->selections[s].active);
                if (lines)
                        doc_indent(doc, false);
                else if (doc->insert_spaces)
                {
                        static const p8 spaces[17] = "                ";
                        p64 column = doc_column(doc, doc->selections[doc->primary].active);
                        p32 size = doc->tab_size - (p32)(column % doc->tab_size);

                        doc_type(doc, spaces, size < 16 ? size : 16);
                }
                else
                        doc_type(doc, (const p8 address_to) "\t", 1);
                return true;
        }
        case DOC_CMD_OUTDENT:
                doc_indent(doc, true);
                return true;
        case DOC_CMD_INDENT:
                doc_indent(doc, false);
                return true;
        case DOC_CMD_MOVE_LINE_UP: case DOC_CMD_MOVE_LINE_DOWN:
                doc_move_lines(doc, command == DOC_CMD_MOVE_LINE_DOWN);
                return true;
        case DOC_CMD_COPY_LINE_UP: case DOC_CMD_COPY_LINE_DOWN:
                doc_duplicate_lines(doc, command == DOC_CMD_COPY_LINE_UP);
                return true;
        case DOC_CMD_DELETE_LINES:
                doc_delete_lines(doc);
                return true;
        case DOC_CMD_JOIN_LINES:
                doc_join_lines(doc);
                return true;
        case DOC_CMD_TRANSPOSE:
                doc_transpose(doc);
                return true;
        case DOC_CMD_LINE_BELOW: case DOC_CMD_LINE_ABOVE:
                doc_insert_line(doc, command == DOC_CMD_LINE_ABOVE);
                return true;
        case DOC_CMD_COMMENT:
                doc_comment(doc, comment);
                return true;
        case DOC_CMD_UPPER: case DOC_CMD_LOWER: case DOC_CMD_TITLE:
                doc_case(doc, command == DOC_CMD_UPPER ? DOC_CASE_UPPER : command == DOC_CMD_LOWER ? DOC_CASE_LOWER : DOC_CASE_TITLE);
                return true;
        case DOC_CMD_SORT: case DOC_CMD_SORT_DESCENDING:
                doc_sort_lines(doc, command == DOC_CMD_SORT_DESCENDING);
                return true;
        case DOC_CMD_UNDO:
                doc_undo(doc);
                return true;
        case DOC_CMD_REDO:
                doc_redo(doc);
                return true;
        case DOC_CMD_SELECT_ALL:
                doc_select_all(doc);
                return true;
        case DOC_CMD_SELECT_LINE:
                doc_select_lines(doc);
                return true;
        case DOC_CMD_EXPAND:
                doc_expand(doc);
                return true;
        case DOC_CMD_SHRINK:
                doc_shrink(doc);
                return true;
        case DOC_CMD_CURSOR_ABOVE: case DOC_CMD_CURSOR_BELOW:
                doc_add_cursor(doc, command == DOC_CMD_CURSOR_BELOW);
                return true;
        case DOC_CMD_NEXT_MATCH:
                doc_select_next(doc);
                return true;
        case DOC_CMD_ALL_MATCHES:
                doc_select_all_matches(doc);
                return true;
        case DOC_CMD_SINGLE:
                doc_single(doc);
                return true;
        case DOC_CMD_BRACKET:
                doc_jump_bracket(doc);
                return true;
        case DOC_CMD_OVERWRITE:
                doc->overwrite = !doc->overwrite;
                return true;
        }
        return false;
}

/* ---- tokenizers: colour roles for a line, from the state the line before left ---- */

/*
        A tokenizer reads one line and the state the lines before it left (0 at the file's start) and says the roles of
        its runs and the state it leaves: a comment or a string still open, as a number. The document keeps the state at
        each line's start, so an edit tokenizes again from its line, only as far as the view needs, and stops early when
        a line's state comes out as it was (the lines after it are then as they were). The roles are what the theme
        colours; a language is a tokenizer and the comment its lines take.
*/
enum
{
        DOC_ROLE_TEXT, DOC_ROLE_KEYWORD, DOC_ROLE_TYPE, DOC_ROLE_STRING, DOC_ROLE_NUMBER, DOC_ROLE_COMMENT,
        DOC_ROLE_OPERATOR, DOC_ROLE_PREPROCESSOR, DOC_ROLE_FUNCTION, DOC_ROLE_VARIABLE, DOC_ROLE_INVALID, DOC_ROLES
};

struct doc_token
{
        p32 start, length;
        p8 role;
};

typedef p32 (address_to doc_tokenizer)(const p8 address_to line, positive length, p32 state, struct doc_token address_to tokens,
                                       positive address_to count, positive room);

static fn doc_token_put(struct doc_token address_to tokens, positive address_to count, positive room, positive start,
                        positive length, p8 role)
{
        if (!length)
                return;
        if (*count && tokens[*count - 1].role == role && tokens[*count - 1].start + tokens[*count - 1].length == start)
        {
                tokens[*count - 1].length += (p32)length;
                return;
        }
        if (*count < room)
                tokens[(*count)++] = (struct doc_token){(p32)start, (p32)length, role};
}

static b32 doc_word_byte(p8 byte)
{
        return byte_is_alpha((b8)byte) || byte_is_digit((b8)byte) || byte == '_' || byte >= 0x80;
}

static b32 doc_in_list(const char address_to const address_to list, const p8 address_to word, positive length)
{
        for (; *list; list++)
                if (string_length((string_address)*list) == length && !memory_compare(*list, word, length))
                        return true;
        return false;
}

// Plain text: one role, no state.
static p32 doc_tokenize_plain(const p8 address_to line, positive length, p32 state, struct doc_token address_to tokens,
                              positive address_to count, positive room)
{
        (void)line, (void)state;
        doc_token_put(tokens, count, room, 0, length, DOC_ROLE_TEXT);
        return 0;
}

/*
        The languages written like C (C, C++, JavaScript, TypeScript, Rust, Go, Java, C#): // and block comments, quoted
        strings with backslashes, numbers, the preprocessor's lines, and the keywords and types of all of them. The
        state is 1 inside a block comment and 2 inside a string a line end continues (a backslash at the end).
*/
static p32 doc_tokenize_c(const p8 address_to line, positive length, p32 state, struct doc_token address_to tokens,
                          positive address_to count, positive room)
{
        static const char address_to const keywords[] = {
            "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto",
            "struct", "union", "enum", "typedef", "static", "extern", "const", "volatile", "inline", "sizeof", "class",
            "public", "private", "protected", "virtual", "template", "typename", "namespace", "using", "new", "delete",
            "try", "catch", "throw", "function", "var", "let", "import", "export", "from", "async", "await", "yield",
            "fn", "pub", "mod", "impl", "trait", "match", "loop", "mut", "use", "where", "go", "func", "package",
            "interface", "type", "defer", "range", "this", "self", "true", "false", "null", "nullptr", "None", "in",
            "of", "as", "extends", "implements", "super", "static_assert", "_Static_assert", "constexpr", "auto",
            "register", "restrict", null};
        static const char address_to const types[] = {
            "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "_Bool", "size_t",
            "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f32", "f64", "usize", "isize", "str", "String",
            "p8", "p16", "p32", "p64", "b8", "b16", "b32", "b64", "positive", "bipolar", "string", "boolean", "number",
            "byte", "rune", null};
        positive at = 0;

        if (state == 1)
        {
                const p8 address_to close = length >= 2 ? memory_search((address_any)line, length, "*/", 2) : null;

                at = close ? (positive)(close - line) + 2 : length;
                doc_token_put(tokens, count, room, 0, at, DOC_ROLE_COMMENT);
                state = close ? 0 : 1;
        }
        else if (state == 2)
        {
                positive finish = 0;

                while (finish < length && line[finish] != '"')
                        finish += line[finish] == '\\' ? 2 : 1;
                finish = finish < length ? finish + 1 : length;
                doc_token_put(tokens, count, room, 0, finish, DOC_ROLE_STRING);
                at = finish;
                state = finish >= length && length && line[length - 1] == '\\' ? 2 : 0;
        }
        {
                positive first = at;

                while (first < length && (line[first] == ' ' || line[first] == '\t'))
                        first++;
                if (!at && first < length && line[first] == '#')
                {
                        doc_token_put(tokens, count, room, 0, length, DOC_ROLE_PREPROCESSOR);
                        return 0;
                }
        }
        while (at < length)
        {
                p8 c = line[at];
                positive start = at;

                if (c == '/' && at + 1 < length && line[at + 1] == '/')
                {
                        doc_token_put(tokens, count, room, at, length - at, DOC_ROLE_COMMENT);
                        return 0;
                }
                if (c == '/' && at + 1 < length && line[at + 1] == '*')
                {
                        const p8 address_to close = length - at >= 4 ? memory_search((address_any)(line + at + 2), length - at - 2, "*/", 2) : null;

                        at = close ? (positive)(close - line) + 2 : length;
                        doc_token_put(tokens, count, room, start, at - start, DOC_ROLE_COMMENT);
                        if (!close)
                                return 1;
                        continue;
                }
                if (c == '"' || c == '\'' || c == '`')
                {
                        at++;
                        while (at < length && line[at] != c)
                                at += line[at] == '\\' ? 2 : 1;
                        at = at < length ? at + 1 : length;
                        doc_token_put(tokens, count, room, start, at - start, DOC_ROLE_STRING);
                        if (at >= length && c == '"' && line[length - 1] == '\\')
                                return 2;
                        continue;
                }
                if (byte_is_digit((b8)c) || (c == '.' && at + 1 < length && byte_is_digit((b8)line[at + 1])))
                {
                        while (at < length && (doc_word_byte(line[at]) || line[at] == '.'))
                                at++;
                        doc_token_put(tokens, count, room, start, at - start, DOC_ROLE_NUMBER);
                        continue;
                }
                if (doc_word_byte(c))
                {
                        while (at < length && doc_word_byte(line[at]))
                                at++;
                        doc_token_put(tokens, count, room, start, at - start,
                                      doc_in_list(keywords, line + start, at - start) ? DOC_ROLE_KEYWORD
                                      : doc_in_list(types, line + start, at - start) ? DOC_ROLE_TYPE
                                      : at < length && line[at] == '(' ? DOC_ROLE_FUNCTION
                                                                       : DOC_ROLE_TEXT);
                        continue;
                }
                at++;
                doc_token_put(tokens, count, room, start, 1,
                              memory_first_of((address_any) "+-*/%=<>!&|^~?:", (b8)c, 15) ? DOC_ROLE_OPERATOR : DOC_ROLE_TEXT);
        }
        return 0;
}

/*
        Shell (POSIX sh, bash, zsh): # comments where a word starts, strings in single and double quotes (which a line
        end does not close: the state says which is open), $ variables, and the shell's words.
*/
static p32 doc_tokenize_shell(const p8 address_to line, positive length, p32 state, struct doc_token address_to tokens,
                              positive address_to count, positive room)
{
        static const char address_to const keywords[] = {
            "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac", "in", "function",
            "return", "local", "export", "readonly", "set", "unset", "shift", "exit", "break", "continue", "trap",
            "eval", "exec", "source", "alias", "declare", "typeset", "select", "time", null};
        positive at = 0;

        if (state == 1 || state == 2)
        {
                p8 quote = state == 1 ? '\'' : '"';

                while (at < length && line[at] != quote)
                        at += line[at] == '\\' && quote == '"' ? 2 : 1;
                at = at < length ? at + 1 : length;
                doc_token_put(tokens, count, room, 0, at, DOC_ROLE_STRING);
                if (at >= length && (length == 0 || line[length - 1] != quote))
                        return state;
        }
        while (at < length)
        {
                p8 c = line[at];
                positive start = at;

                if (c == '#' && (at == 0 || line[at - 1] == ' ' || line[at - 1] == '\t' || line[at - 1] == ';'))
                {
                        doc_token_put(tokens, count, room, at, length - at, DOC_ROLE_COMMENT);
                        return 0;
                }
                if (c == '\'' || c == '"')
                {
                        at++;
                        while (at < length && line[at] != c)
                                at += line[at] == '\\' && c == '"' ? 2 : 1;
                        if (at >= length)
                        {
                                doc_token_put(tokens, count, room, start, length - start, DOC_ROLE_STRING);
                                return c == '\'' ? 1 : 2;
                        }
                        at++;
                        doc_token_put(tokens, count, room, start, at - start, DOC_ROLE_STRING);
                        continue;
                }
                if (c == '$')
                {
                        at++;
                        if (at < length && line[at] == '{')
                        {
                                while (at < length && line[at] != '}')
                                        at++;
                                at += at < length;
                        }
                        else
                                while (at < length && (doc_word_byte(line[at]) || (at == start + 1 && memory_first_of((address_any) "?@#*$!-0", (b8)line[at], 8))))
                                        at++;
                        doc_token_put(tokens, count, room, start, at - start, DOC_ROLE_VARIABLE);
                        continue;
                }
                if (doc_word_byte(c) || c == '-')
                {
                        while (at < length && (doc_word_byte(line[at]) || line[at] == '-' || line[at] == '.'))
                                at++;
                        doc_token_put(tokens, count, room, start, at - start,
                                      doc_in_list(keywords, line + start, at - start) ? DOC_ROLE_KEYWORD
                                      : byte_is_digit((b8)c) ? DOC_ROLE_NUMBER
                                                             : DOC_ROLE_TEXT);
                        continue;
                }
                at++;
                doc_token_put(tokens, count, room, start, 1,
                              memory_first_of((address_any) "|&;<>()=!", (b8)c, 9) ? DOC_ROLE_OPERATOR : DOC_ROLE_TEXT);
        }
        return 0;
}

#define DOC_LANGUAGE_PLAIN 0
#define DOC_LANGUAGE_C 1
#define DOC_LANGUAGE_SHELL 2

static const struct
{
        const char address_to name;
        doc_tokenizer tokenize;
        const char address_to comment;
} doc_languages[] = {
    {"Plain Text", doc_tokenize_plain, "#"},
    {"C", doc_tokenize_c, "//"},
    {"Shell", doc_tokenize_shell, "#"},
};

// A file's language from its name and its first bytes: a C family extension, a shell one or a #! naming a shell.
static p32 doc_language_of(const char address_to name, const p8 address_to head, positive length)
{
        static const char address_to const c_like[] = {"c", "h", "cc", "cpp", "cxx", "hpp", "hh", "js", "mjs", "ts",
                                                       "tsx", "jsx", "rs", "go", "java", "cs", "swift", "kt", "zig", "json",
                                                       "css", "scss", null};
        static const char address_to const shells[] = {"sh", "bash", "zsh", "ksh", "profile", "bashrc", "zshrc", null};
        positive size = name ? string_length((string_address)name) : 0, dot = size;

        while (dot && name[dot - 1] != '.' && name[dot - 1] != '/')
                dot--;
        if (dot && name[dot - 1] == '.')
        {
                if (doc_in_list(c_like, (const p8 address_to)name + dot, size - dot))
                        return DOC_LANGUAGE_C;
                if (doc_in_list(shells, (const p8 address_to)name + dot, size - dot))
                        return DOC_LANGUAGE_SHELL;
        }
        if (length > 2 && head[0] == '#' && head[1] == '!')
        {
                positive finish = 2;

                while (finish < length && finish < 128 && head[finish] != '\n')
                        finish++;
                if (memory_search((address_any)head, finish, "sh", 2))
                        return DOC_LANGUAGE_SHELL;
        }
        return DOC_LANGUAGE_PLAIN;
}

/*
        The tokens of line: the line's start state made known first (tokenizing from the last line known, and stopping
        at the first line past every edit whose state comes out as it was), then the line itself. Into room tokens:
        how many. Each line is read at most DOC_FIND_LINE_MOST into it.
*/
static positive doc_tokens(struct doc address_to doc, p64 line, struct doc_token address_to tokens, positive room)
{
        doc_tokenizer tokenize = doc_languages[doc->language < 3 ? doc->language : 0].tokenize;
        p64 lines = doc_line_count(doc);
        positive count = 0;
        const p8 address_to bytes;
        positive length;
        static struct doc_token scratch[512];

        if (line >= lines)
                return 0;
        // The states, one a line, made the first time (the gap after the last line); every change keeps them a line each.
        if (!doc->states)
        {
                positive room = 0;

                if (!array_store_reserve(doc->states, room, 0, lines + 4096, 4096))
                        return 0;
                doc->state_room = room;
                memory_zero(doc->states, room * sizeof(p32));
                doc->gap_start = lines;
                doc->gap_end = room;
                doc->states_valid = 1;
                doc->states_converge = doc->states_last_edit = 0;
        }
        if (doc->state_room - (doc->gap_end - doc->gap_start) < lines)
                doc_states_splice(doc, doc->state_room - (doc->gap_end - doc->gap_start), 0,
                                  lines - (doc->state_room - (doc->gap_end - doc->gap_start)));
        if (!doc->states_valid)
                doc->states_valid = 1;
        while (doc->states_valid <= line)
        {
                p64 at = doc->states_valid - 1;
                positive scratch_count = 0;
                p32 state;

                length = doc_line_bytes(doc, at, &bytes);
                state = tokenize(bytes, length, doc_state_get(doc, at), scratch, &scratch_count, 512);
                if (doc->states_valid > doc->states_last_edit && doc->states_valid < doc->states_converge &&
                    doc_state_get(doc, doc->states_valid) == state)
                {
                        // The lines after are as they were: their states hold.
                        doc->states_valid = doc->states_converge;
                        doc->states_converge = 0;
                        continue;
                }
                doc_state_set(doc, doc->states_valid, state);
                doc->states_valid++;
        }
        length = doc_line_bytes(doc, line, &bytes);
        tokenize(bytes, length, doc_state_get(doc, line), tokens, &count, room);
        return count;
}

/* ---- folding ---- */

// A line's indent in columns, and whether it is all blanks.
static p64 doc_indent_columns(const struct doc address_to doc, p64 line, b32 address_to blank)
{
        p64 text = doc_first_text(doc, line);

        *blank = text == doc_line_end(doc, line);
        return doc_column(doc, text);
}

/*
        The range that folds at line, by indent: the lines after it with more indent than it (blank lines among them),
        when the next line with text has more. Answers the last line of the range, or DOC_NONE when line folds nothing.
        At most 100000 lines are looked at.
*/
static p64 doc_fold_end(const struct doc address_to doc, p64 line)
{
        p64 lines = doc_line_count(doc), last = DOC_NONE;
        b32 blank;
        p64 indent = doc_indent_columns(doc, line, &blank);

        if (blank)
                return DOC_NONE;
        for (p64 next = line + 1; next < lines && next - line < 100000; next++)
        {
                p64 now = doc_indent_columns(doc, next, &blank);

                if (blank)
                        continue;
                if (now <= indent)
                        break;
                last = next;
        }
        return last;
}

// Whether line is hidden in a fold, and the fold's end line when it starts one that is folded.
static b32 doc_folded_at(const struct doc address_to doc, p64 line, p64 address_to end_line)
{
        p64 start = doc_line_start(doc, line);

        for (positive i = 0; i < doc->range_count; i++)
                if (doc->ranges[i].kind == DOC_RANGE_FOLD && doc->ranges[i].start == doc_line_end(doc, line))
                {
                        *end_line = doc_line_of(doc, doc->ranges[i].finish);
                        return true;
                }
        (void)start;
        return false;
}

static b32 doc_hidden(const struct doc address_to doc, p64 line)
{
        p64 start = doc_line_start(doc, line);

        for (positive i = 0; i < doc->range_count; i++)
                if (doc->ranges[i].kind == DOC_RANGE_FOLD && start > doc->ranges[i].start && start <= doc->ranges[i].finish)
                        return true;
        return false;
}

// The fold at line closed (or opened again): what it hides is a range from the line's end to its last line's end.
static b32 doc_fold(struct doc address_to doc, p64 line, b32 close)
{
        p64 end_line;

        for (positive i = 0; i < doc->range_count; i++)
                if (doc->ranges[i].kind == DOC_RANGE_FOLD && doc->ranges[i].start == doc_line_end(doc, line))
                {
                        if (close)
                                return true;
                        doc->ranges[i] = doc->ranges[--doc->range_count];
                        return true;
                }
        if (!close)
                return false;
        end_line = doc_fold_end(doc, line);
        if (end_line == DOC_NONE)
                return false;
        return doc_range_add(doc, doc_line_end(doc, line), doc_line_end(doc, end_line), DOC_RANGE_FOLD, 0);
}

/* ---- the break property table ---- */

// TEXT_TABLES BEGIN
// Generated by differential.py --harness text_tables from the Unicode 16.0.0 data files: GraphemeBreakProperty,
// WordBreakProperty, emoji-data (Extended_Pictographic) and DerivedCoreProperties (InCB). A range is its first
// code point shifted left 8 and the index of its properties in doc_break_combos.
static const p32 doc_break_range_count = 3079;
static const p16 doc_break_combos[] = {
        0x0, 0x3, 0x8, 0x11, 0x22, 0x33, 0x40, 0x44, 0x48, 0x66, 0x73, 0x80,
        0x90, 0xa0, 0xa7, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xb0, 0xc0, 0xd0, 0xe0,
        0xf0, 0x100, 0x107, 0x110, 0x120, 0x244, 0x4a0, 0x644, 0x655, 0x800, 0x8a0,
};
static const p32 doc_break_ranges[] = {
        0x1, 0xa04, 0xb05, 0xd03, 0xe01, 0x201c, 0x2100, 0x2215, 0x2300, 0x2714,
        0x2800, 0x2c18, 0x2d00, 0x2e16, 0x2f00, 0x3019, 0x3a17, 0x3b18, 0x3c00, 0x410d,
        0x5b00, 0x5f1b, 0x6000, 0x610d, 0x7b00, 0x7f01, 0x8505, 0x8601, 0xa000, 0xa921,
        0xaa0d, 0xab00, 0xad0a, 0xae21, 0xaf00, 0xb50d, 0xb600, 0xb717, 0xb800, 0xba0d,
        0xbb00, 0xc00d, 0xd700, 0xd80d, 0xf700, 0xf80d, 0x2d800, 0x2de0d, 0x3001f, 0x3700d,
        0x37500, 0x3760d, 0x37800, 0x37a0d, 0x37e18, 0x37f0d, 0x38000, 0x3860d, 0x38717, 0x3880d,
        0x38b00, 0x38c0d, 0x38d00, 0x38e0d, 0x3a200, 0x3a30d, 0x3f600, 0x3f70d, 0x48200, 0x4831f,
        0x48a0d, 0x53000, 0x5310d, 0x55700, 0x5590d, 0x55d00, 0x55e0d, 0x55f17, 0x5600d, 0x58918,
        0x58a0d, 0x58b00, 0x5911f, 0x5be00, 0x5bf1f, 0x5c000, 0x5c11f, 0x5c300, 0x5c41f, 0x5c600,
        0x5c71f, 0x5c800, 0x5d00c, 0x5eb00, 0x5ef0c, 0x5f30d, 0x5f417, 0x5f500, 0x6001a, 0x60600,
        0x60c18, 0x60e00, 0x6101f, 0x61b00, 0x61c0a, 0x61d00, 0x6200d, 0x64b1f, 0x66019, 0x66a00,
        0x66b19, 0x66c18, 0x66d00, 0x66e0d, 0x6701f, 0x6710d, 0x6d400, 0x6d50d, 0x6d61f, 0x6dd1a,
        0x6de00, 0x6df1f, 0x6e50d, 0x6e71f, 0x6e900, 0x6ea1f, 0x6ee0d, 0x6f019, 0x6fa0d, 0x6fd00,
        0x6ff0d, 0x70000, 0x70f0e, 0x7100d, 0x7111f, 0x7120d, 0x7301f, 0x74b00, 0x74d0d, 0x7a61f,
        0x7b10d, 0x7b200, 0x7c019, 0x7ca0d, 0x7eb1f, 0x7f40d, 0x7f600, 0x7f818, 0x7f900, 0x7fa0d,
        0x7fb00, 0x7fd1f, 0x7fe00, 0x8000d, 0x8161f, 0x81a0d, 0x81b1f, 0x8240d, 0x8251f, 0x8280d,
        0x8291f, 0x82e00, 0x8400d, 0x8591f, 0x85c00, 0x8600d, 0x86b00, 0x8700d, 0x88800, 0x8890d,
        0x88f00, 0x8901a, 0x89200, 0x8971f, 0x8a00d, 0x8ca1f, 0x8e21a, 0x8e31f, 0x90308, 0x9040d,
        0x9151e, 0x93a1f, 0x93b08, 0x93c1f, 0x93d0d, 0x93e08, 0x9411f, 0x94908, 0x94d1d, 0x94e08,
        0x9500d, 0x9511f, 0x9581e, 0x9600d, 0x9621f, 0x96400, 0x96619, 0x97000, 0x9710d, 0x9781e,
        0x9800d, 0x9811f, 0x98208, 0x98400, 0x9850d, 0x98d00, 0x98f0d, 0x99100, 0x9930d, 0x9951e,
        0x9a900, 0x9aa1e, 0x9b100, 0x9b21e, 0x9b300, 0x9b61e, 0x9ba00, 0x9bc1f, 0x9bd0d, 0x9be1f,
        0x9bf08, 0x9c11f, 0x9c500, 0x9c708, 0x9c900, 0x9cb08, 0x9cd1d, 0x9ce0d, 0x9cf00, 0x9d71f,
        0x9d800, 0x9dc1e, 0x9de00, 0x9df1e, 0x9e00d, 0x9e21f, 0x9e400, 0x9e619, 0x9f01e, 0x9f200,
        0x9fc0d, 0x9fd00, 0x9fe1f, 0x9ff00, 0xa011f, 0xa0308, 0xa0400, 0xa050d, 0xa0b00, 0xa0f0d,
        0xa1100, 0xa130d, 0xa2900, 0xa2a0d, 0xa3100, 0xa320d, 0xa3400, 0xa350d, 0xa3700, 0xa380d,
        0xa3a00, 0xa3c1f, 0xa3d00, 0xa3e08, 0xa411f, 0xa4300, 0xa471f, 0xa4900, 0xa4b1f, 0xa4e00,
        0xa511f, 0xa5200, 0xa590d, 0xa5d00, 0xa5e0d, 0xa5f00, 0xa6619, 0xa701f, 0xa720d, 0xa751f,
        0xa7600, 0xa811f, 0xa8308, 0xa8400, 0xa850d, 0xa8e00, 0xa8f0d, 0xa9200, 0xa930d, 0xa951e,
        0xaa900, 0xaaa1e, 0xab100, 0xab21e, 0xab400, 0xab51e, 0xaba00, 0xabc1f, 0xabd0d, 0xabe08,
        0xac11f, 0xac600, 0xac71f, 0xac908, 0xaca00, 0xacb08, 0xacd1d, 0xace00, 0xad00d, 0xad100,
        0xae00d, 0xae21f, 0xae400, 0xae619, 0xaf000, 0xaf91e, 0xafa1f, 0xb0000, 0xb011f, 0xb0208,
        0xb0400, 0xb050d, 0xb0d00, 0xb0f0d, 0xb1100, 0xb130d, 0xb151e, 0xb2900, 0xb2a1e, 0xb3100,
        0xb321e, 0xb3400, 0xb351e, 0xb3a00, 0xb3c1f, 0xb3d0d, 0xb3e1f, 0xb4008, 0xb411f, 0xb4500,
        0xb4708, 0xb4900, 0xb4b08, 0xb4d1d, 0xb4e00, 0xb551f, 0xb5800, 0xb5c1e, 0xb5e00, 0xb5f1e,
        0xb600d, 0xb621f, 0xb6400, 0xb6619, 0xb7000, 0xb711e, 0xb7200, 0xb821f, 0xb830d, 0xb8400,
        0xb850d, 0xb8b00, 0xb8e0d, 0xb9100, 0xb920d, 0xb9600, 0xb990d, 0xb9b00, 0xb9c0d, 0xb9d00,
        0xb9e0d, 0xba000, 0xba30d, 0xba500, 0xba80d, 0xbab00, 0xbae0d, 0xbba00, 0xbbe1f, 0xbbf08,
        0xbc01f, 0xbc108, 0xbc300, 0xbc608, 0xbc900, 0xbca08, 0xbcd1f, 0xbce00, 0xbd00d, 0xbd100,
        0xbd71f, 0xbd800, 0xbe619, 0xbf000, 0xc001f, 0xc0108, 0xc041f, 0xc050d, 0xc0d00, 0xc0e0d,
        0xc1100, 0xc120d, 0xc151e, 0xc2900, 0xc2a1e, 0xc3a00, 0xc3c1f, 0xc3d0d, 0xc3e1f, 0xc4108,
        0xc4500, 0xc461f, 0xc4900, 0xc4a1f, 0xc4d1d, 0xc4e00, 0xc551f, 0xc5700, 0xc581e, 0xc5b00,
        0xc5d0d, 0xc5e00, 0xc600d, 0xc621f, 0xc6400, 0xc6619, 0xc7000, 0xc800d, 0xc811f, 0xc8208,
        0xc8400, 0xc850d, 0xc8d00, 0xc8e0d, 0xc9100, 0xc920d, 0xca900, 0xcaa0d, 0xcb400, 0xcb50d,
        0xcba00, 0xcbc1f, 0xcbd0d, 0xcbe08, 0xcbf1f, 0xcc108, 0xcc21f, 0xcc308, 0xcc500, 0xcc61f,
        0xcc900, 0xcca1f, 0xcce00, 0xcd51f, 0xcd700, 0xcdd0d, 0xcdf00, 0xce00d, 0xce21f, 0xce400,
        0xce619, 0xcf000, 0xcf10d, 0xcf308, 0xcf400, 0xd001f, 0xd0208, 0xd040d, 0xd0d00, 0xd0e0d,
        0xd1100, 0xd120d, 0xd151e, 0xd3b1f, 0xd3d0d, 0xd3e1f, 0xd3f08, 0xd411f, 0xd4500, 0xd4608,
        0xd4900, 0xd4a08, 0xd4d1d, 0xd4e0e, 0xd4f00, 0xd540d, 0xd571f, 0xd5800, 0xd5f0d, 0xd621f,
        0xd6400, 0xd6619, 0xd7000, 0xd7a0d, 0xd8000, 0xd811f, 0xd8208, 0xd8400, 0xd850d, 0xd9700,
        0xd9a0d, 0xdb200, 0xdb30d, 0xdbc00, 0xdbd0d, 0xdbe00, 0xdc00d, 0xdc700, 0xdca1f, 0xdcb00,
        0xdcf1f, 0xdd008, 0xdd21f, 0xdd500, 0xdd61f, 0xdd700, 0xdd808, 0xddf1f, 0xde000, 0xde619,
        0xdf000, 0xdf208, 0xdf400, 0xe311f, 0xe3200, 0xe3302, 0xe341f, 0xe3b00, 0xe471f, 0xe4f00,
        0xe5019, 0xe5a00, 0xeb11f, 0xeb200, 0xeb302, 0xeb41f, 0xebd00, 0xec81f, 0xecf00, 0xed019,
        0xeda00, 0xf000d, 0xf0100, 0xf181f, 0xf1a00, 0xf2019, 0xf2a00, 0xf351f, 0xf3600, 0xf371f,
        0xf3800, 0xf391f, 0xf3a00, 0xf3e08, 0xf400d, 0xf4800, 0xf490d, 0xf6d00, 0xf711f, 0xf7f08,
        0xf801f, 0xf8500, 0xf861f, 0xf880d, 0xf8d1f, 0xf9800, 0xf991f, 0xfbd00, 0xfc61f, 0xfc700,
        0x102b06, 0x102d1f, 0x103108, 0x10321f, 0x103806, 0x10391f, 0x103b08, 0x103d1f, 0x103f00, 0x104019,
        0x104a00, 0x105608, 0x10581f, 0x105a00, 0x105e1f, 0x106100, 0x106206, 0x106500, 0x106706, 0x106e00,
        0x10711f, 0x107500, 0x10821f, 0x108306, 0x108408, 0x10851f, 0x108706, 0x108d1f, 0x108e00, 0x108f06,
        0x109019, 0x109a06, 0x109d1f, 0x109e00, 0x10a00d, 0x10c600, 0x10c70d, 0x10c800, 0x10cd0d, 0x10ce00,
        0x10d00d, 0x10fb00, 0x10fc0d, 0x11000f, 0x116010, 0x11a811, 0x12000d, 0x124900, 0x124a0d, 0x124e00,
        0x12500d, 0x125700, 0x12580d, 0x125900, 0x125a0d, 0x125e00, 0x12600d, 0x128900, 0x128a0d, 0x128e00,
        0x12900d, 0x12b100, 0x12b20d, 0x12b600, 0x12b80d, 0x12bf00, 0x12c00d, 0x12c100, 0x12c20d, 0x12c600,
        0x12c80d, 0x12d700, 0x12d80d, 0x131100, 0x13120d, 0x131600, 0x13180d, 0x135b00, 0x135d1f, 0x136000,
        0x13800d, 0x139000, 0x13a00d, 0x13f600, 0x13f80d, 0x13fe00, 0x14010d, 0x166d00, 0x166f0d, 0x16801c,
        0x16810d, 0x169b00, 0x16a00d, 0x16eb00, 0x16ee0d, 0x16f900, 0x17000d, 0x17121f, 0x171600, 0x171f0d,
        0x17321f, 0x173500, 0x17400d, 0x17521f, 0x175400, 0x17600d, 0x176d00, 0x176e0d, 0x177100, 0x17721f,
        0x177400, 0x17b41f, 0x17b608, 0x17b71f, 0x17be08, 0x17c61f, 0x17c708, 0x17c91f, 0x17d400, 0x17dd1f,
        0x17de00, 0x17e019, 0x17ea00, 0x180b1f, 0x180e0a, 0x180f1f, 0x181019, 0x181a00, 0x18200d, 0x187900,
        0x18800d, 0x18851f, 0x18870d, 0x18a91f, 0x18aa0d, 0x18ab00, 0x18b00d, 0x18f600, 0x19000d, 0x191f00,
        0x19201f, 0x192308, 0x19271f, 0x192908, 0x192c00, 0x193008, 0x19321f, 0x193308, 0x19391f, 0x193c00,
        0x194619, 0x195000, 0x19d019, 0x19db00, 0x1a000d, 0x1a171f, 0x1a1908, 0x1a1b1f, 0x1a1c00, 0x1a5508,
        0x1a561f, 0x1a5708, 0x1a581f, 0x1a5f00, 0x1a601f, 0x1a6106, 0x1a621f, 0x1a6306, 0x1a651f, 0x1a6d08,
        0x1a731f, 0x1a7d00, 0x1a7f1f, 0x1a8019, 0x1a8a00, 0x1a9019, 0x1a9a00, 0x1ab01f, 0x1acf00, 0x1b001f,
        0x1b0408, 0x1b050d, 0x1b341f, 0x1b3e08, 0x1b421f, 0x1b450d, 0x1b4d00, 0x1b5019, 0x1b5a00, 0x1b6b1f,
        0x1b7400, 0x1b801f, 0x1b8208, 0x1b830d, 0x1ba108, 0x1ba21f, 0x1ba608, 0x1ba81f, 0x1bae0d, 0x1bb019,
        0x1bba0d, 0x1be61f, 0x1be708, 0x1be81f, 0x1bea08, 0x1bed1f, 0x1bee08, 0x1bef1f, 0x1bf400, 0x1c000d,
        0x1c2408, 0x1c2c1f, 0x1c3408, 0x1c361f, 0x1c3800, 0x1c4019, 0x1c4a00, 0x1c4d0d, 0x1c5019, 0x1c5a0d,
        0x1c7e00, 0x1c800d, 0x1c8b00, 0x1c900d, 0x1cbb00, 0x1cbd0d, 0x1cc000, 0x1cd01f, 0x1cd300, 0x1cd41f,
        0x1ce108, 0x1ce21f, 0x1ce90d, 0x1ced1f, 0x1cee0d, 0x1cf41f, 0x1cf50d, 0x1cf708, 0x1cf81f, 0x1cfa0d,
        0x1cfb00, 0x1d000d, 0x1dc01f, 0x1e000d, 0x1f1600, 0x1f180d, 0x1f1e00, 0x1f200d, 0x1f4600, 0x1f480d,
        0x1f4e00, 0x1f500d, 0x1f5800, 0x1f590d, 0x1f5a00, 0x1f5b0d, 0x1f5c00, 0x1f5d0d, 0x1f5e00, 0x1f5f0d,
        0x1f7e00, 0x1f800d, 0x1fb500, 0x1fb60d, 0x1fbd00, 0x1fbe0d, 0x1fbf00, 0x1fc20d, 0x1fc500, 0x1fc60d,
        0x1fcd00, 0x1fd00d, 0x1fd400, 0x1fd60d, 0x1fdc00, 0x1fe00d, 0x1fed00, 0x1ff20d, 0x1ff500, 0x1ff60d,
        0x1ffd00, 0x20001c, 0x200700, 0x20081c, 0x200b01, 0x200c07, 0x200d20, 0x200e0a, 0x201000, 0x201816,
        0x201a00, 0x202416, 0x202500, 0x202717, 0x202805, 0x202a0a, 0x202f1b, 0x203000, 0x203c21, 0x203d00,
        0x203f1b, 0x204100, 0x204418, 0x204500, 0x204921, 0x204a00, 0x20541b, 0x205500, 0x205f1c, 0x20600a,
        0x206501, 0x20660a, 0x207000, 0x20710d, 0x207200, 0x207f0d, 0x208000, 0x20900d, 0x209d00, 0x20d01f,
        0x20f100, 0x21020d, 0x210300, 0x21070d, 0x210800, 0x210a0d, 0x211400, 0x21150d, 0x211600, 0x21190d,
        0x211e00, 0x212221, 0x212300, 0x21240d, 0x212500, 0x21260d, 0x212700, 0x21280d, 0x212900, 0x212a0d,
        0x212e00, 0x212f0d, 0x213922, 0x213a00, 0x213c0d, 0x214000, 0x21450d, 0x214a00, 0x214e0d, 0x214f00,
        0x21600d, 0x218900, 0x219421, 0x219a00, 0x21a921, 0x21ab00, 0x231a21, 0x231c00, 0x232821, 0x232900,
        0x238821, 0x238900, 0x23cf21, 0x23d000, 0x23e921, 0x23f400, 0x23f821, 0x23fb00, 0x24b60d, 0x24c222,
        0x24c30d, 0x24ea00, 0x25aa21, 0x25ac00, 0x25b621, 0x25b700, 0x25c021, 0x25c100, 0x25fb21, 0x25ff00,
        0x260021, 0x260600, 0x260721, 0x261300, 0x261421, 0x268600, 0x269021, 0x270600, 0x270821, 0x271300,
        0x271421, 0x271500, 0x271621, 0x271700, 0x271d21, 0x271e00, 0x272121, 0x272200, 0x272821, 0x272900,
        0x273321, 0x273500, 0x274421, 0x274500, 0x274721, 0x274800, 0x274c21, 0x274d00, 0x274e21, 0x274f00,
        0x275321, 0x275600, 0x275721, 0x275800, 0x276321, 0x276800, 0x279521, 0x279800, 0x27a121, 0x27a200,
        0x27b021, 0x27b100, 0x27bf21, 0x27c000, 0x293421, 0x293600, 0x2b0521, 0x2b0800, 0x2b1b21, 0x2b1d00,
        0x2b5021, 0x2b5100, 0x2b5521, 0x2b5600, 0x2c000d, 0x2ce500, 0x2ceb0d, 0x2cef1f, 0x2cf20d, 0x2cf400,
        0x2d000d, 0x2d2600, 0x2d270d, 0x2d2800, 0x2d2d0d, 0x2d2e00, 0x2d300d, 0x2d6800, 0x2d6f0d, 0x2d7000,
        0x2d7f1f, 0x2d800d, 0x2d9700, 0x2da00d, 0x2da700, 0x2da80d, 0x2daf00, 0x2db00d, 0x2db700, 0x2db80d,
        0x2dbf00, 0x2dc00d, 0x2dc700, 0x2dc80d, 0x2dcf00, 0x2dd00d, 0x2dd700, 0x2dd80d, 0x2ddf00, 0x2de01f,
        0x2e0000, 0x2e2f0d, 0x2e3000, 0x30001c, 0x300100, 0x30050d, 0x300600, 0x302a1f, 0x303021, 0x30310b,
        0x303600, 0x303b0d, 0x303d21, 0x303e00, 0x30991f, 0x309b0b, 0x309d00, 0x30a00b, 0x30fb00, 0x30fc0b,
        0x310000, 0x31050d, 0x313000, 0x31310d, 0x318f00, 0x31a00d, 0x31c000, 0x31f00b, 0x320000, 0x329721,
        0x329800, 0x329921, 0x329a00, 0x32d00b, 0x32ff00, 0x33000b, 0x335800, 0xa0000d, 0xa48d00, 0xa4d00d,
        0xa4fe00, 0xa5000d, 0xa60d00, 0xa6100d, 0xa62019, 0xa62a0d, 0xa62c00, 0xa6400d, 0xa66f1f, 0xa67300,
        0xa6741f, 0xa67e00, 0xa67f0d, 0xa69e1f, 0xa6a00d, 0xa6f01f, 0xa6f200, 0xa7080d, 0xa7ce00, 0xa7d00d,
        0xa7d200, 0xa7d30d, 0xa7d400, 0xa7d50d, 0xa7dd00, 0xa7f20d, 0xa8021f, 0xa8030d, 0xa8061f, 0xa8070d,
        0xa80b1f, 0xa80c0d, 0xa82308, 0xa8251f, 0xa82708, 0xa82800, 0xa82c1f, 0xa82d00, 0xa8400d, 0xa87400,
        0xa88008, 0xa8820d, 0xa8b408, 0xa8c41f, 0xa8c600, 0xa8d019, 0xa8da00, 0xa8e01f, 0xa8f20d, 0xa8f800,
        0xa8fb0d, 0xa8fc00, 0xa8fd0d, 0xa8ff1f, 0xa90019, 0xa90a0d, 0xa9261f, 0xa92e00, 0xa9300d, 0xa9471f,
        0xa95208, 0xa9531f, 0xa95400, 0xa9600f, 0xa97d00, 0xa9801f, 0xa98308, 0xa9840d, 0xa9b31f, 0xa9b408,
        0xa9b61f, 0xa9ba08, 0xa9bc1f, 0xa9be08, 0xa9c01f, 0xa9c100, 0xa9cf0d, 0xa9d019, 0xa9da00, 0xa9e51f,
        0xa9e600, 0xa9f019, 0xa9fa00, 0xaa000d, 0xaa291f, 0xaa2f08, 0xaa311f, 0xaa3308, 0xaa351f, 0xaa3700,
        0xaa400d, 0xaa431f, 0xaa440d, 0xaa4c1f, 0xaa4d08, 0xaa4e00, 0xaa5019, 0xaa5a00, 0xaa7b06, 0xaa7c1f,
        0xaa7d06, 0xaa7e00, 0xaab01f, 0xaab100, 0xaab21f, 0xaab500, 0xaab71f, 0xaab900, 0xaabe1f, 0xaac000,
        0xaac11f, 0xaac200, 0xaae00d, 0xaaeb08, 0xaaec1f, 0xaaee08, 0xaaf000, 0xaaf20d, 0xaaf508, 0xaaf61f,
        0xaaf700, 0xab010d, 0xab0700, 0xab090d, 0xab0f00, 0xab110d, 0xab1700, 0xab200d, 0xab2700, 0xab280d,
        0xab2f00, 0xab300d, 0xab6a00, 0xab700d, 0xabe308, 0xabe51f, 0xabe608, 0xabe81f, 0xabe908, 0xabeb00,
        0xabec08, 0xabed1f, 0xabee00, 0xabf019, 0xabfa00, 0xac0012, 0xac0113, 0xac1c12, 0xac1d13, 0xac3812,
        0xac3913, 0xac5412, 0xac5513, 0xac7012, 0xac7113, 0xac8c12, 0xac8d13, 0xaca812, 0xaca913, 0xacc412,
        0xacc513, 0xace012, 0xace113, 0xacfc12, 0xacfd13, 0xad1812, 0xad1913, 0xad3412, 0xad3513, 0xad5012,
        0xad5113, 0xad6c12, 0xad6d13, 0xad8812, 0xad8913, 0xada412, 0xada513, 0xadc012, 0xadc113, 0xaddc12,
        0xaddd13, 0xadf812, 0xadf913, 0xae1412, 0xae1513, 0xae3012, 0xae3113, 0xae4c12, 0xae4d13, 0xae6812,
        0xae6913, 0xae8412, 0xae8513, 0xaea012, 0xaea113, 0xaebc12, 0xaebd13, 0xaed812, 0xaed913, 0xaef412,
        0xaef513, 0xaf1012, 0xaf1113, 0xaf2c12, 0xaf2d13, 0xaf4812, 0xaf4913, 0xaf6412, 0xaf6513, 0xaf8012,
        0xaf8113, 0xaf9c12, 0xaf9d13, 0xafb812, 0xafb913, 0xafd412, 0xafd513, 0xaff012, 0xaff113, 0xb00c12,
        0xb00d13, 0xb02812, 0xb02913, 0xb04412, 0xb04513, 0xb06012, 0xb06113, 0xb07c12, 0xb07d13, 0xb09812,
        0xb09913, 0xb0b412, 0xb0b513, 0xb0d012, 0xb0d113, 0xb0ec12, 0xb0ed13, 0xb10812, 0xb10913, 0xb12412,
        0xb12513, 0xb14012, 0xb14113, 0xb15c12, 0xb15d13, 0xb17812, 0xb17913, 0xb19412, 0xb19513, 0xb1b012,
        0xb1b113, 0xb1cc12, 0xb1cd13, 0xb1e812, 0xb1e913, 0xb20412, 0xb20513, 0xb22012, 0xb22113, 0xb23c12,
        0xb23d13, 0xb25812, 0xb25913, 0xb27412, 0xb27513, 0xb29012, 0xb29113, 0xb2ac12, 0xb2ad13, 0xb2c812,
        0xb2c913, 0xb2e412, 0xb2e513, 0xb30012, 0xb30113, 0xb31c12, 0xb31d13, 0xb33812, 0xb33913, 0xb35412,
        0xb35513, 0xb37012, 0xb37113, 0xb38c12, 0xb38d13, 0xb3a812, 0xb3a913, 0xb3c412, 0xb3c513, 0xb3e012,
        0xb3e113, 0xb3fc12, 0xb3fd13, 0xb41812, 0xb41913, 0xb43412, 0xb43513, 0xb45012, 0xb45113, 0xb46c12,
        0xb46d13, 0xb48812, 0xb48913, 0xb4a412, 0xb4a513, 0xb4c012, 0xb4c113, 0xb4dc12, 0xb4dd13, 0xb4f812,
        0xb4f913, 0xb51412, 0xb51513, 0xb53012, 0xb53113, 0xb54c12, 0xb54d13, 0xb56812, 0xb56913, 0xb58412,
        0xb58513, 0xb5a012, 0xb5a113, 0xb5bc12, 0xb5bd13, 0xb5d812, 0xb5d913, 0xb5f412, 0xb5f513, 0xb61012,
        0xb61113, 0xb62c12, 0xb62d13, 0xb64812, 0xb64913, 0xb66412, 0xb66513, 0xb68012, 0xb68113, 0xb69c12,
        0xb69d13, 0xb6b812, 0xb6b913, 0xb6d412, 0xb6d513, 0xb6f012, 0xb6f113, 0xb70c12, 0xb70d13, 0xb72812,
        0xb72913, 0xb74412, 0xb74513, 0xb76012, 0xb76113, 0xb77c12, 0xb77d13, 0xb79812, 0xb79913, 0xb7b412,
        0xb7b513, 0xb7d012, 0xb7d113, 0xb7ec12, 0xb7ed13, 0xb80812, 0xb80913, 0xb82412, 0xb82513, 0xb84012,
        0xb84113, 0xb85c12, 0xb85d13, 0xb87812, 0xb87913, 0xb89412, 0xb89513, 0xb8b012, 0xb8b113, 0xb8cc12,
        0xb8cd13, 0xb8e812, 0xb8e913, 0xb90412, 0xb90513, 0xb92012, 0xb92113, 0xb93c12, 0xb93d13, 0xb95812,
        0xb95913, 0xb97412, 0xb97513, 0xb99012, 0xb99113, 0xb9ac12, 0xb9ad13, 0xb9c812, 0xb9c913, 0xb9e412,
        0xb9e513, 0xba0012, 0xba0113, 0xba1c12, 0xba1d13, 0xba3812, 0xba3913, 0xba5412, 0xba5513, 0xba7012,
        0xba7113, 0xba8c12, 0xba8d13, 0xbaa812, 0xbaa913, 0xbac412, 0xbac513, 0xbae012, 0xbae113, 0xbafc12,
        0xbafd13, 0xbb1812, 0xbb1913, 0xbb3412, 0xbb3513, 0xbb5012, 0xbb5113, 0xbb6c12, 0xbb6d13, 0xbb8812,
        0xbb8913, 0xbba412, 0xbba513, 0xbbc012, 0xbbc113, 0xbbdc12, 0xbbdd13, 0xbbf812, 0xbbf913, 0xbc1412,
        0xbc1513, 0xbc3012, 0xbc3113, 0xbc4c12, 0xbc4d13, 0xbc6812, 0xbc6913, 0xbc8412, 0xbc8513, 0xbca012,
        0xbca113, 0xbcbc12, 0xbcbd13, 0xbcd812, 0xbcd913, 0xbcf412, 0xbcf513, 0xbd1012, 0xbd1113, 0xbd2c12,
        0xbd2d13, 0xbd4812, 0xbd4913, 0xbd6412, 0xbd6513, 0xbd8012, 0xbd8113, 0xbd9c12, 0xbd9d13, 0xbdb812,
        0xbdb913, 0xbdd412, 0xbdd513, 0xbdf012, 0xbdf113, 0xbe0c12, 0xbe0d13, 0xbe2812, 0xbe2913, 0xbe4412,
        0xbe4513, 0xbe6012, 0xbe6113, 0xbe7c12, 0xbe7d13, 0xbe9812, 0xbe9913, 0xbeb412, 0xbeb513, 0xbed012,
        0xbed113, 0xbeec12, 0xbeed13, 0xbf0812, 0xbf0913, 0xbf2412, 0xbf2513, 0xbf4012, 0xbf4113, 0xbf5c12,
        0xbf5d13, 0xbf7812, 0xbf7913, 0xbf9412, 0xbf9513, 0xbfb012, 0xbfb113, 0xbfcc12, 0xbfcd13, 0xbfe812,
        0xbfe913, 0xc00412, 0xc00513, 0xc02012, 0xc02113, 0xc03c12, 0xc03d13, 0xc05812, 0xc05913, 0xc07412,
        0xc07513, 0xc09012, 0xc09113, 0xc0ac12, 0xc0ad13, 0xc0c812, 0xc0c913, 0xc0e412, 0xc0e513, 0xc10012,
        0xc10113, 0xc11c12, 0xc11d13, 0xc13812, 0xc13913, 0xc15412, 0xc15513, 0xc17012, 0xc17113, 0xc18c12,
        0xc18d13, 0xc1a812, 0xc1a913, 0xc1c412, 0xc1c513, 0xc1e012, 0xc1e113, 0xc1fc12, 0xc1fd13, 0xc21812,
        0xc21913, 0xc23412, 0xc23513, 0xc25012, 0xc25113, 0xc26c12, 0xc26d13, 0xc28812, 0xc28913, 0xc2a412,
        0xc2a513, 0xc2c012, 0xc2c113, 0xc2dc12, 0xc2dd13, 0xc2f812, 0xc2f913, 0xc31412, 0xc31513, 0xc33012,
        0xc33113, 0xc34c12, 0xc34d13, 0xc36812, 0xc36913, 0xc38412, 0xc38513, 0xc3a012, 0xc3a113, 0xc3bc12,
        0xc3bd13, 0xc3d812, 0xc3d913, 0xc3f412, 0xc3f513, 0xc41012, 0xc41113, 0xc42c12, 0xc42d13, 0xc44812,
        0xc44913, 0xc46412, 0xc46513, 0xc48012, 0xc48113, 0xc49c12, 0xc49d13, 0xc4b812, 0xc4b913, 0xc4d412,
        0xc4d513, 0xc4f012, 0xc4f113, 0xc50c12, 0xc50d13, 0xc52812, 0xc52913, 0xc54412, 0xc54513, 0xc56012,
        0xc56113, 0xc57c12, 0xc57d13, 0xc59812, 0xc59913, 0xc5b412, 0xc5b513, 0xc5d012, 0xc5d113, 0xc5ec12,
        0xc5ed13, 0xc60812, 0xc60913, 0xc62412, 0xc62513, 0xc64012, 0xc64113, 0xc65c12, 0xc65d13, 0xc67812,
        0xc67913, 0xc69412, 0xc69513, 0xc6b012, 0xc6b113, 0xc6cc12, 0xc6cd13, 0xc6e812, 0xc6e913, 0xc70412,
        0xc70513, 0xc72012, 0xc72113, 0xc73c12, 0xc73d13, 0xc75812, 0xc75913, 0xc77412, 0xc77513, 0xc79012,
        0xc79113, 0xc7ac12, 0xc7ad13, 0xc7c812, 0xc7c913, 0xc7e412, 0xc7e513, 0xc80012, 0xc80113, 0xc81c12,
        0xc81d13, 0xc83812, 0xc83913, 0xc85412, 0xc85513, 0xc87012, 0xc87113, 0xc88c12, 0xc88d13, 0xc8a812,
        0xc8a913, 0xc8c412, 0xc8c513, 0xc8e012, 0xc8e113, 0xc8fc12, 0xc8fd13, 0xc91812, 0xc91913, 0xc93412,
        0xc93513, 0xc95012, 0xc95113, 0xc96c12, 0xc96d13, 0xc98812, 0xc98913, 0xc9a412, 0xc9a513, 0xc9c012,
        0xc9c113, 0xc9dc12, 0xc9dd13, 0xc9f812, 0xc9f913, 0xca1412, 0xca1513, 0xca3012, 0xca3113, 0xca4c12,
        0xca4d13, 0xca6812, 0xca6913, 0xca8412, 0xca8513, 0xcaa012, 0xcaa113, 0xcabc12, 0xcabd13, 0xcad812,
        0xcad913, 0xcaf412, 0xcaf513, 0xcb1012, 0xcb1113, 0xcb2c12, 0xcb2d13, 0xcb4812, 0xcb4913, 0xcb6412,
        0xcb6513, 0xcb8012, 0xcb8113, 0xcb9c12, 0xcb9d13, 0xcbb812, 0xcbb913, 0xcbd412, 0xcbd513, 0xcbf012,
        0xcbf113, 0xcc0c12, 0xcc0d13, 0xcc2812, 0xcc2913, 0xcc4412, 0xcc4513, 0xcc6012, 0xcc6113, 0xcc7c12,
        0xcc7d13, 0xcc9812, 0xcc9913, 0xccb412, 0xccb513, 0xccd012, 0xccd113, 0xccec12, 0xcced13, 0xcd0812,
        0xcd0913, 0xcd2412, 0xcd2513, 0xcd4012, 0xcd4113, 0xcd5c12, 0xcd5d13, 0xcd7812, 0xcd7913, 0xcd9412,
        0xcd9513, 0xcdb012, 0xcdb113, 0xcdcc12, 0xcdcd13, 0xcde812, 0xcde913, 0xce0412, 0xce0513, 0xce2012,
        0xce2113, 0xce3c12, 0xce3d13, 0xce5812, 0xce5913, 0xce7412, 0xce7513, 0xce9012, 0xce9113, 0xceac12,
        0xcead13, 0xcec812, 0xcec913, 0xcee412, 0xcee513, 0xcf0012, 0xcf0113, 0xcf1c12, 0xcf1d13, 0xcf3812,
        0xcf3913, 0xcf5412, 0xcf5513, 0xcf7012, 0xcf7113, 0xcf8c12, 0xcf8d13, 0xcfa812, 0xcfa913, 0xcfc412,
        0xcfc513, 0xcfe012, 0xcfe113, 0xcffc12, 0xcffd13, 0xd01812, 0xd01913, 0xd03412, 0xd03513, 0xd05012,
        0xd05113, 0xd06c12, 0xd06d13, 0xd08812, 0xd08913, 0xd0a412, 0xd0a513, 0xd0c012, 0xd0c113, 0xd0dc12,
        0xd0dd13, 0xd0f812, 0xd0f913, 0xd11412, 0xd11513, 0xd13012, 0xd13113, 0xd14c12, 0xd14d13, 0xd16812,
        0xd16913, 0xd18412, 0xd18513, 0xd1a012, 0xd1a113, 0xd1bc12, 0xd1bd13, 0xd1d812, 0xd1d913, 0xd1f412,
        0xd1f513, 0xd21012, 0xd21113, 0xd22c12, 0xd22d13, 0xd24812, 0xd24913, 0xd26412, 0xd26513, 0xd28012,
        0xd28113, 0xd29c12, 0xd29d13, 0xd2b812, 0xd2b913, 0xd2d412, 0xd2d513, 0xd2f012, 0xd2f113, 0xd30c12,
        0xd30d13, 0xd32812, 0xd32913, 0xd34412, 0xd34513, 0xd36012, 0xd36113, 0xd37c12, 0xd37d13, 0xd39812,
        0xd39913, 0xd3b412, 0xd3b513, 0xd3d012, 0xd3d113, 0xd3ec12, 0xd3ed13, 0xd40812, 0xd40913, 0xd42412,
        0xd42513, 0xd44012, 0xd44113, 0xd45c12, 0xd45d13, 0xd47812, 0xd47913, 0xd49412, 0xd49513, 0xd4b012,
        0xd4b113, 0xd4cc12, 0xd4cd13, 0xd4e812, 0xd4e913, 0xd50412, 0xd50513, 0xd52012, 0xd52113, 0xd53c12,
        0xd53d13, 0xd55812, 0xd55913, 0xd57412, 0xd57513, 0xd59012, 0xd59113, 0xd5ac12, 0xd5ad13, 0xd5c812,
        0xd5c913, 0xd5e412, 0xd5e513, 0xd60012, 0xd60113, 0xd61c12, 0xd61d13, 0xd63812, 0xd63913, 0xd65412,
        0xd65513, 0xd67012, 0xd67113, 0xd68c12, 0xd68d13, 0xd6a812, 0xd6a913, 0xd6c412, 0xd6c513, 0xd6e012,
        0xd6e113, 0xd6fc12, 0xd6fd13, 0xd71812, 0xd71913, 0xd73412, 0xd73513, 0xd75012, 0xd75113, 0xd76c12,
        0xd76d13, 0xd78812, 0xd78913, 0xd7a400, 0xd7b010, 0xd7c700, 0xd7cb11, 0xd7fc00, 0xfb000d, 0xfb0700,
        0xfb130d, 0xfb1800, 0xfb1d0c, 0xfb1e1f, 0xfb1f0c, 0xfb2900, 0xfb2a0c, 0xfb3700, 0xfb380c, 0xfb3d00,
        0xfb3e0c, 0xfb3f00, 0xfb400c, 0xfb4200, 0xfb430c, 0xfb4500, 0xfb460c, 0xfb500d, 0xfbb200, 0xfbd30d,
        0xfd3e00, 0xfd500d, 0xfd9000, 0xfd920d, 0xfdc800, 0xfdf00d, 0xfdfc00, 0xfe001f, 0xfe1000, 0xfe1317,
        0xfe1400, 0xfe201f, 0xfe3000, 0xfe331b, 0xfe3500, 0xfe4d1b, 0xfe5018, 0xfe5100, 0xfe5216, 0xfe5300,
        0xfe5418, 0xfe5517, 0xfe5600, 0xfe700d, 0xfe7500, 0xfe760d, 0xfefd00, 0xfeff0a, 0xff0000, 0xff0716,
        0xff0800, 0xff0c18, 0xff0d00, 0xff0e16, 0xff0f00, 0xff1019, 0xff1a17, 0xff1b18, 0xff1c00, 0xff210d,
        0xff3b00, 0xff3f1b, 0xff4000, 0xff410d, 0xff5b00, 0xff660b, 0xff9e1f, 0xffa00d, 0xffbf00, 0xffc20d,
        0xffc800, 0xffca0d, 0xffd000, 0xffd20d, 0xffd800, 0xffda0d, 0xffdd00, 0xfff001, 0xfff90a, 0xfffc00,
        0x100000d, 0x1000c00, 0x1000d0d, 0x1002700, 0x100280d, 0x1003b00, 0x1003c0d, 0x1003e00, 0x1003f0d, 0x1004e00,
        0x100500d, 0x1005e00, 0x100800d, 0x100fb00, 0x101400d, 0x1017500, 0x101fd1f, 0x101fe00, 0x102800d, 0x1029d00,
        0x102a00d, 0x102d100, 0x102e01f, 0x102e100, 0x103000d, 0x1032000, 0x1032d0d, 0x1034b00, 0x103500d, 0x103761f,
        0x1037b00, 0x103800d, 0x1039e00, 0x103a00d, 0x103c400, 0x103c80d, 0x103d000, 0x103d10d, 0x103d600, 0x104000d,
        0x1049e00, 0x104a019, 0x104aa00, 0x104b00d, 0x104d400, 0x104d80d, 0x104fc00, 0x105000d, 0x1052800, 0x105300d,
        0x1056400, 0x105700d, 0x1057b00, 0x1057c0d, 0x1058b00, 0x1058c0d, 0x1059300, 0x105940d, 0x1059600, 0x105970d,
        0x105a200, 0x105a30d, 0x105b200, 0x105b30d, 0x105ba00, 0x105bb0d, 0x105bd00, 0x105c00d, 0x105f400, 0x106000d,
        0x1073700, 0x107400d, 0x1075600, 0x107600d, 0x1076800, 0x107800d, 0x1078600, 0x107870d, 0x107b100, 0x107b20d,
        0x107bb00, 0x108000d, 0x1080600, 0x108080d, 0x1080900, 0x1080a0d, 0x1083600, 0x108370d, 0x1083900, 0x1083c0d,
        0x1083d00, 0x1083f0d, 0x1085600, 0x108600d, 0x1087700, 0x108800d, 0x1089f00, 0x108e00d, 0x108f300, 0x108f40d,
        0x108f600, 0x109000d, 0x1091600, 0x109200d, 0x1093a00, 0x109800d, 0x109b800, 0x109be0d, 0x109c000, 0x10a000d,
        0x10a011f, 0x10a0400, 0x10a051f, 0x10a0700, 0x10a0c1f, 0x10a100d, 0x10a1400, 0x10a150d, 0x10a1800, 0x10a190d,
        0x10a3600, 0x10a381f, 0x10a3b00, 0x10a3f1f, 0x10a4000, 0x10a600d, 0x10a7d00, 0x10a800d, 0x10a9d00, 0x10ac00d,
        0x10ac800, 0x10ac90d, 0x10ae51f, 0x10ae700, 0x10b000d, 0x10b3600, 0x10b400d, 0x10b5600, 0x10b600d, 0x10b7300,
        0x10b800d, 0x10b9200, 0x10c000d, 0x10c4900, 0x10c800d, 0x10cb300, 0x10cc00d, 0x10cf300, 0x10d000d, 0x10d241f,
        0x10d2800, 0x10d3019, 0x10d3a00, 0x10d4019, 0x10d4a0d, 0x10d6600, 0x10d691f, 0x10d6e00, 0x10d6f0d, 0x10d8600,
        0x10e800d, 0x10eaa00, 0x10eab1f, 0x10ead00, 0x10eb00d, 0x10eb200, 0x10ec20d, 0x10ec500, 0x10efc1f, 0x10f000d,
        0x10f1d00, 0x10f270d, 0x10f2800, 0x10f300d, 0x10f461f, 0x10f5100, 0x10f700d, 0x10f821f, 0x10f8600, 0x10fb00d,
        0x10fc500, 0x10fe00d, 0x10ff700, 0x1100008, 0x110011f, 0x1100208, 0x110030d, 0x110381f, 0x1104700, 0x1106619,
        0x110701f, 0x110710d, 0x110731f, 0x110750d, 0x1107600, 0x1107f1f, 0x1108208, 0x110830d, 0x110b008, 0x110b31f,
        0x110b708, 0x110b91f, 0x110bb00, 0x110bd1a, 0x110be00, 0x110c21f, 0x110c300, 0x110cd1a, 0x110ce00, 0x110d00d,
        0x110e900, 0x110f019, 0x110fa00, 0x111001f, 0x111030d, 0x111271f, 0x1112c08, 0x1112d1f, 0x1113500, 0x1113619,
        0x1114000, 0x111440d, 0x1114508, 0x111470d, 0x1114800, 0x111500d, 0x111731f, 0x1117400, 0x111760d, 0x1117700,
        0x111801f, 0x1118208, 0x111830d, 0x111b308, 0x111b61f, 0x111bf08, 0x111c01f, 0x111c10d, 0x111c20e, 0x111c40d,
        0x111c500, 0x111c91f, 0x111cd00, 0x111ce08, 0x111cf1f, 0x111d019, 0x111da0d, 0x111db00, 0x111dc0d, 0x111dd00,
        0x112000d, 0x1121200, 0x112130d, 0x1122c08, 0x1122f1f, 0x1123208, 0x112341f, 0x1123800, 0x1123e1f, 0x1123f0d,
        0x112411f, 0x1124200, 0x112800d, 0x1128700, 0x112880d, 0x1128900, 0x1128a0d, 0x1128e00, 0x1128f0d, 0x1129e00,
        0x1129f0d, 0x112a900, 0x112b00d, 0x112df1f, 0x112e008, 0x112e31f, 0x112eb00, 0x112f019, 0x112fa00, 0x113001f,
        0x1130208, 0x1130400, 0x113050d, 0x1130d00, 0x1130f0d, 0x1131100, 0x113130d, 0x1132900, 0x1132a0d, 0x1133100,
        0x113320d, 0x1133400, 0x113350d, 0x1133a00, 0x1133b1f, 0x1133d0d, 0x1133e1f, 0x1133f08, 0x113401f, 0x1134108,
        0x1134500, 0x1134708, 0x1134900, 0x1134b08, 0x1134d1f, 0x1134e00, 0x113500d, 0x1135100, 0x113571f, 0x1135800,
        0x1135d0d, 0x1136208, 0x1136400, 0x113661f, 0x1136d00, 0x113701f, 0x1137500, 0x113800d, 0x1138a00, 0x1138b0d,
        0x1138c00, 0x1138e0d, 0x1138f00, 0x113900d, 0x113b600, 0x113b70d, 0x113b81f, 0x113b908, 0x113bb1f, 0x113c100,
        0x113c21f, 0x113c300, 0x113c51f, 0x113c600, 0x113c71f, 0x113ca08, 0x113cb00, 0x113cc08, 0x113ce1f, 0x113d10e,
        0x113d21f, 0x113d30d, 0x113d400, 0x113e11f, 0x113e300, 0x114000d, 0x1143508, 0x114381f, 0x1144008, 0x114421f,
        0x1144508, 0x114461f, 0x114470d, 0x1144b00, 0x1145019, 0x1145a00, 0x1145e1f, 0x1145f0d, 0x1146200, 0x114800d,
        0x114b01f, 0x114b108, 0x114b31f, 0x114b908, 0x114ba1f, 0x114bb08, 0x114bd1f, 0x114be08, 0x114bf1f, 0x114c108,
        0x114c21f, 0x114c40d, 0x114c600, 0x114c70d, 0x114c800, 0x114d019, 0x114da00, 0x115800d, 0x115af1f, 0x115b008,
        0x115b21f, 0x115b600, 0x115b808, 0x115bc1f, 0x115be08, 0x115bf1f, 0x115c100, 0x115d80d, 0x115dc1f, 0x115de00,
        0x116000d, 0x1163008, 0x116331f, 0x1163b08, 0x1163d1f, 0x1163e08, 0x1163f1f, 0x1164100, 0x116440d, 0x1164500,
        0x1165019, 0x1165a00, 0x116800d, 0x116ab1f, 0x116ac08, 0x116ad1f, 0x116ae08, 0x116b01f, 0x116b80d, 0x116b900,
        0x116c019, 0x116ca00, 0x116d019, 0x116e400, 0x1171d1f, 0x1171e08, 0x1171f1f, 0x1172006, 0x117221f, 0x1172608,
        0x117271f, 0x1172c00, 0x1173019, 0x1173a00, 0x118000d, 0x1182c08, 0x1182f1f, 0x1183808, 0x118391f, 0x1183b00,
        0x118a00d, 0x118e019, 0x118ea00, 0x118ff0d, 0x1190700, 0x119090d, 0x1190a00, 0x1190c0d, 0x1191400, 0x119150d,
        0x1191700, 0x119180d, 0x119301f, 0x1193108, 0x1193600, 0x1193708, 0x1193900, 0x1193b1f, 0x1193f0e, 0x1194008,
        0x119410e, 0x1194208, 0x119431f, 0x1194400, 0x1195019, 0x1195a00, 0x119a00d, 0x119a800, 0x119aa0d, 0x119d108,
        0x119d41f, 0x119d800, 0x119da1f, 0x119dc08, 0x119e01f, 0x119e10d, 0x119e200, 0x119e30d, 0x119e408, 0x119e500,
        0x11a000d, 0x11a011f, 0x11a0b0d, 0x11a331f, 0x11a3908, 0x11a3a0e, 0x11a3b1f, 0x11a3f00, 0x11a471f, 0x11a4800,
        0x11a500d, 0x11a511f, 0x11a5708, 0x11a591f, 0x11a5c0d, 0x11a840e, 0x11a8a1f, 0x11a9708, 0x11a981f, 0x11a9a00,
        0x11a9d0d, 0x11a9e00, 0x11ab00d, 0x11af900, 0x11bc00d, 0x11be100, 0x11bf019, 0x11bfa00, 0x11c000d, 0x11c0900,
        0x11c0a0d, 0x11c2f08, 0x11c301f, 0x11c3700, 0x11c381f, 0x11c3e08, 0x11c3f1f, 0x11c400d, 0x11c4100, 0x11c5019,
        0x11c5a00, 0x11c720d, 0x11c9000, 0x11c921f, 0x11ca800, 0x11ca908, 0x11caa1f, 0x11cb108, 0x11cb21f, 0x11cb408,
        0x11cb51f, 0x11cb700, 0x11d000d, 0x11d0700, 0x11d080d, 0x11d0a00, 0x11d0b0d, 0x11d311f, 0x11d3700, 0x11d3a1f,
        0x11d3b00, 0x11d3c1f, 0x11d3e00, 0x11d3f1f, 0x11d460e, 0x11d471f, 0x11d4800, 0x11d5019, 0x11d5a00, 0x11d600d,
        0x11d6600, 0x11d670d, 0x11d6900, 0x11d6a0d, 0x11d8a08, 0x11d8f00, 0x11d901f, 0x11d9200, 0x11d9308, 0x11d951f,
        0x11d9608, 0x11d971f, 0x11d980d, 0x11d9900, 0x11da019, 0x11daa00, 0x11ee00d, 0x11ef31f, 0x11ef508, 0x11ef700,
        0x11f001f, 0x11f020e, 0x11f0308, 0x11f040d, 0x11f1100, 0x11f120d, 0x11f3408, 0x11f361f, 0x11f3b00, 0x11f3e08,
        0x11f401f, 0x11f4300, 0x11f5019, 0x11f5a1f, 0x11f5b00, 0x11fb00d, 0x11fb100, 0x120000d, 0x1239a00, 0x124000d,
        0x1246f00, 0x124800d, 0x1254400, 0x12f900d, 0x12ff100, 0x130000d, 0x134300a, 0x134401f, 0x134410d, 0x134471f,
        0x1345600, 0x134600d, 0x143fb00, 0x144000d, 0x1464700, 0x161000d, 0x1611e1f, 0x1612a08, 0x1612d1f, 0x1613019,
        0x1613a00, 0x168000d, 0x16a3900, 0x16a400d, 0x16a5f00, 0x16a6019, 0x16a6a00, 0x16a700d, 0x16abf00, 0x16ac019,
        0x16aca00, 0x16ad00d, 0x16aee00, 0x16af01f, 0x16af500, 0x16b000d, 0x16b301f, 0x16b3700, 0x16b400d, 0x16b4400,
        0x16b5019, 0x16b5a00, 0x16b630d, 0x16b7800, 0x16b7d0d, 0x16b9000, 0x16d400d, 0x16d6310, 0x16d640d, 0x16d6710,
        0x16d6b0d, 0x16d6d00, 0x16d7019, 0x16d7a00, 0x16e400d, 0x16e8000, 0x16f000d, 0x16f4b00, 0x16f4f1f, 0x16f500d,
        0x16f5108, 0x16f8800, 0x16f8f1f, 0x16f930d, 0x16fa000, 0x16fe00d, 0x16fe200, 0x16fe30d, 0x16fe41f, 0x16fe500,
        0x16ff01f, 0x16ff200, 0x1aff00b, 0x1aff400, 0x1aff50b, 0x1affc00, 0x1affd0b, 0x1afff00, 0x1b0000b, 0x1b00100,
        0x1b1200b, 0x1b12300, 0x1b1550b, 0x1b15600, 0x1b1640b, 0x1b16800, 0x1bc000d, 0x1bc6b00, 0x1bc700d, 0x1bc7d00,
        0x1bc800d, 0x1bc8900, 0x1bc900d, 0x1bc9a00, 0x1bc9d1f, 0x1bc9f00, 0x1bca00a, 0x1bca400, 0x1ccf019, 0x1ccfa00,
        0x1cf001f, 0x1cf2e00, 0x1cf301f, 0x1cf4700, 0x1d1651f, 0x1d16a00, 0x1d16d1f, 0x1d1730a, 0x1d17b1f, 0x1d18300,
        0x1d1851f, 0x1d18c00, 0x1d1aa1f, 0x1d1ae00, 0x1d2421f, 0x1d24500, 0x1d4000d, 0x1d45500, 0x1d4560d, 0x1d49d00,
        0x1d49e0d, 0x1d4a000, 0x1d4a20d, 0x1d4a300, 0x1d4a50d, 0x1d4a700, 0x1d4a90d, 0x1d4ad00, 0x1d4ae0d, 0x1d4ba00,
        0x1d4bb0d, 0x1d4bc00, 0x1d4bd0d, 0x1d4c400, 0x1d4c50d, 0x1d50600, 0x1d5070d, 0x1d50b00, 0x1d50d0d, 0x1d51500,
        0x1d5160d, 0x1d51d00, 0x1d51e0d, 0x1d53a00, 0x1d53b0d, 0x1d53f00, 0x1d5400d, 0x1d54500, 0x1d5460d, 0x1d54700,
        0x1d54a0d, 0x1d55100, 0x1d5520d, 0x1d6a600, 0x1d6a80d, 0x1d6c100, 0x1d6c20d, 0x1d6db00, 0x1d6dc0d, 0x1d6fb00,
        0x1d6fc0d, 0x1d71500, 0x1d7160d, 0x1d73500, 0x1d7360d, 0x1d74f00, 0x1d7500d, 0x1d76f00, 0x1d7700d, 0x1d78900,
        0x1d78a0d, 0x1d7a900, 0x1d7aa0d, 0x1d7c300, 0x1d7c40d, 0x1d7cc00, 0x1d7ce19, 0x1d80000, 0x1da001f, 0x1da3700,
        0x1da3b1f, 0x1da6d00, 0x1da751f, 0x1da7600, 0x1da841f, 0x1da8500, 0x1da9b1f, 0x1daa000, 0x1daa11f, 0x1dab000,
        0x1df000d, 0x1df1f00, 0x1df250d, 0x1df2b00, 0x1e0001f, 0x1e00700, 0x1e0081f, 0x1e01900, 0x1e01b1f, 0x1e02200,
        0x1e0231f, 0x1e02500, 0x1e0261f, 0x1e02b00, 0x1e0300d, 0x1e06e00, 0x1e08f1f, 0x1e09000, 0x1e1000d, 0x1e12d00,
        0x1e1301f, 0x1e1370d, 0x1e13e00, 0x1e14019, 0x1e14a00, 0x1e14e0d, 0x1e14f00, 0x1e2900d, 0x1e2ae1f, 0x1e2af00,
        0x1e2c00d, 0x1e2ec1f, 0x1e2f019, 0x1e2fa00, 0x1e4d00d, 0x1e4ec1f, 0x1e4f019, 0x1e4fa00, 0x1e5d00d, 0x1e5ee1f,
        0x1e5f00d, 0x1e5f119, 0x1e5fb00, 0x1e7e00d, 0x1e7e700, 0x1e7e80d, 0x1e7ec00, 0x1e7ed0d, 0x1e7ef00, 0x1e7f00d,
        0x1e7ff00, 0x1e8000d, 0x1e8c500, 0x1e8d01f, 0x1e8d700, 0x1e9000d, 0x1e9441f, 0x1e94b0d, 0x1e94c00, 0x1e95019,
        0x1e95a00, 0x1ee000d, 0x1ee0400, 0x1ee050d, 0x1ee2000, 0x1ee210d, 0x1ee2300, 0x1ee240d, 0x1ee2500, 0x1ee270d,
        0x1ee2800, 0x1ee290d, 0x1ee3300, 0x1ee340d, 0x1ee3800, 0x1ee390d, 0x1ee3a00, 0x1ee3b0d, 0x1ee3c00, 0x1ee420d,
        0x1ee4300, 0x1ee470d, 0x1ee4800, 0x1ee490d, 0x1ee4a00, 0x1ee4b0d, 0x1ee4c00, 0x1ee4d0d, 0x1ee5000, 0x1ee510d,
        0x1ee5300, 0x1ee540d, 0x1ee5500, 0x1ee570d, 0x1ee5800, 0x1ee590d, 0x1ee5a00, 0x1ee5b0d, 0x1ee5c00, 0x1ee5d0d,
        0x1ee5e00, 0x1ee5f0d, 0x1ee6000, 0x1ee610d, 0x1ee6300, 0x1ee640d, 0x1ee6500, 0x1ee670d, 0x1ee6b00, 0x1ee6c0d,
        0x1ee7300, 0x1ee740d, 0x1ee7800, 0x1ee790d, 0x1ee7d00, 0x1ee7e0d, 0x1ee7f00, 0x1ee800d, 0x1ee8a00, 0x1ee8b0d,
        0x1ee9c00, 0x1eea10d, 0x1eea400, 0x1eea50d, 0x1eeaa00, 0x1eeab0d, 0x1eebc00, 0x1f00021, 0x1f10000, 0x1f10d21,
        0x1f11000, 0x1f12f21, 0x1f1300d, 0x1f14a00, 0x1f1500d, 0x1f16a00, 0x1f16c21, 0x1f17022, 0x1f1720d, 0x1f17e22,
        0x1f1800d, 0x1f18a00, 0x1f18e21, 0x1f18f00, 0x1f19121, 0x1f19b00, 0x1f1ad21, 0x1f1e609, 0x1f20000, 0x1f20121,
        0x1f21000, 0x1f21a21, 0x1f21b00, 0x1f22f21, 0x1f23000, 0x1f23221, 0x1f23b00, 0x1f23c21, 0x1f24000, 0x1f24921,
        0x1f3fb1f, 0x1f40021, 0x1f53e00, 0x1f54621, 0x1f65000, 0x1f68021, 0x1f70000, 0x1f77421, 0x1f78000, 0x1f7d521,
        0x1f80000, 0x1f80c21, 0x1f81000, 0x1f84821, 0x1f85000, 0x1f85a21, 0x1f86000, 0x1f88821, 0x1f89000, 0x1f8ae21,
        0x1f90000, 0x1f90c21, 0x1f93b00, 0x1f93c21, 0x1f94600, 0x1f94721, 0x1fb0000, 0x1fbf019, 0x1fbfa00, 0x1fc0021,
        0x1fffe00, 0xe000001, 0xe00010a, 0xe000201, 0xe00201f, 0xe008001, 0xe01001f, 0xe01f001, 0xe100000,
};
// TEXT_TABLES END

#endif // MOONWATER_CANVAS_TEXT
