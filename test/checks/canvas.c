/*
        test/checks/canvas.c: the text engine and the canvas's cells.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_text_engine
/*
        The text engine (src/canvas/ui/ui.c) against what it stands for.

        A naive model -- the document as one array of bytes, every edit a copy, every line counted from the start --
        is driven through the same random edits as the piece tree, and after each the two must agree on every byte,
        on the line count, and on where lines start and which line an offset is on. The history is walked back and
        forth against the model's snapshots; every editing command is undone and redone and must give back the text
        and the selections exactly; an edit at many carets must be the same as the edits made one at a time; the
        cluster and word boundaries must be the official Unicode test files' (a slice of each is pinned here; the
        text_tables harness feeds all of them); find must find what the regex engine finds on each line; bytes that
        are not UTF-8 and CR LF line ends must come back as they went in; a sparse file of a gigabyte must open.
        `bench` prints the costs instead: opening 100 MB, typing in it, a line lookup, a replace of every match.
*/
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

static positive te_checks, te_failures;
static p64 te_seed = 0x2545f4914f6cdd1dull;

static fn te_check(p8 condition, const char address_to what, positive detail)
{
        te_checks++;
        if (condition)
                return;
        te_failures++;
        if (te_failures <= 40)
                string_format(log, "FAIL %s (%p)\n", (string_address)what, detail);
}

static p32 te_next(p32 below)
{
        XORSHIFT64(te_seed);
        return below ? (p32)((te_seed >> 11) % below) : 0;
}

/* ---- the model ---- */

static p8 address_to te_model;
static positive te_model_length, te_model_room;

static fn te_model_set(const p8 address_to bytes, positive length)
{
        array_store_reserve(te_model, te_model_room, 0, length + 1, 4096);
        memory_copy(te_model, bytes, length);
        te_model_length = length;
}

static fn te_model_replace(positive at, positive removed, const p8 address_to text, positive length)
{
        array_store_reserve(te_model, te_model_room, te_model_length, te_model_length - removed + length + 1, 4096);
        memory_copy(te_model + at + length, te_model + at + removed, te_model_length - at - removed);
        memory_copy(te_model + at, text, length);
        te_model_length = te_model_length - removed + length;
}

// A line end of the model at at: its length, 0 for none.
static positive te_model_break(positive at)
{
        if (te_model[at] == '\n')
                return 1;
        if (te_model[at] == '\r')
                return at + 1 < te_model_length && te_model[at + 1] == '\n' ? 2 : 1;
        return 0;
}

static positive te_model_lines(void)
{
        positive lines = 1;

        for (positive at = 0; at < te_model_length;)
        {
                positive size = te_model_break(at);

                lines += size != 0;
                at += size ? size : 1;
        }
        return lines;
}

static positive te_model_line_start(positive line)
{
        positive at = 0;

        while (line && at < te_model_length)
        {
                positive size = te_model_break(at);

                at += size ? size : 1;
                line -= size != 0;
        }
        return at;
}

static positive te_model_line_of(positive offset)
{
        positive line = 0;

        for (positive at = 0; at < offset;)
        {
                positive size = te_model_break(at);

                if (size && at + size <= offset)
                        line++;
                at += size ? size : 1;
        }
        return line;
}

static b32 te_same(const struct doc address_to doc)
{
        static p8 address_to read;
        static positive read_room;

        if (doc_length(doc) != te_model_length)
                return false;
        array_store_reserve(read, read_room, 0, te_model_length + 1, 4096);
        return doc_read(doc, 0, read, te_model_length) == te_model_length && !memory_compare(read, te_model, te_model_length);
}

// Random text that is mostly letters, with every kind of line end, multibyte characters, marks and bytes that are not UTF-8.
static positive te_text(p8 address_to into, positive most)
{
        static const char address_to const pieces[] = {"a", "b", "c", " ", "\n", "\r", "\r\n", "\xc3\xa9", "e\xcc\x81", "\xe2\x82",
                                                       "\xff", "\xf0\x9f\x98\x80", "\t", "xyz", "(", ")", "\xe4\xb8\xad"};
        positive length = 0, count = te_next((p32)most + 1);

        for (positive i = 0; i < count; i++)
        {
                const char address_to piece = pieces[te_next(sizeof(pieces) / sizeof(pieces[0]))];
                positive size = string_length((string_address)piece);

                if (length + size > most)
                        break;
                memory_copy(into + length, piece, size);
                length += size;
        }
        return length;
}

/* ---- the buffer and the line index against the model ---- */

static fn te_buffer(positive rounds, positive steps)
{
        for (positive round = 0; round < rounds; round++)
        {
                struct doc doc;
                p8 start[512], text[64];
                positive length = te_text(start, sizeof(start));

                doc_open(&doc, start, length);
                te_model_set(start, length);
                // A document starting with a byte order mark keeps it aside.
                if (length >= 3 && start[0] == 0xef && start[1] == 0xbb && start[2] == 0xbf)
                        te_model_set(start + 3, length - 3);
                for (positive step = 0; step < steps; step++)
                {
                        positive at = te_next((p32)te_model_length + 1), removed = te_next((p32)(te_model_length - at) + 1);
                        positive size = te_text(text, sizeof(text));

                        removed = te_next(3) ? removed % 8 : removed;
                        te_check(doc_replace(&doc, at, removed, text, size), "replace refused", step);
                        te_model_replace(at, removed, text, size);
                        te_check(doc_length(&doc) == te_model_length, "length", doc_length(&doc));
                        te_check(doc_line_count(&doc) == te_model_lines(), "line count", doc_line_count(&doc));
                        for (p32 probe = 0; probe < 4; probe++)
                        {
                                positive line = te_next((p32)te_model_lines() + 1), offset = te_next((p32)te_model_length + 1);

                                te_check(doc_line_start(&doc, line) == te_model_line_start(line), "line start", line);
                                te_check(doc_line_of(&doc, offset) == te_model_line_of(offset), "line of", offset);
                        }
                        if (step % 16 == 0 || step + 1 == steps)
                                te_check(te_same(&doc), "bytes", step);
                }
                doc_close(&doc);
        }
}

// Every node's counts are its bytes', and its sums its children's and its own: the tree is whole.
static b32 te_tree_whole(const struct doc address_to doc, p32 node)
{
        const struct doc_node address_to n;
        p32 lf, cr, pairs;
        p8 edge;
        struct doc_sums sum;

        if (!node)
                return true;
        n = &doc->nodes[node];
        if (!te_tree_whole(doc, n->left) || !te_tree_whole(doc, n->right))
                return false;
        doc_count(doc_piece_bytes(doc, n), n->length, &lf, &cr, &pairs, &edge);
        if (lf != n->lf || cr != n->cr || pairs != n->pairs || edge != n->edge || !n->length)
                return false;
        sum = n->sum;
        doc_update((struct doc address_to)doc, node);
        return sum.bytes == n->sum.bytes && sum.lf == n->sum.lf && sum.cr == n->sum.cr && sum.pairs == n->sum.pairs &&
               sum.edge == n->sum.edge;
}

/* ---- the history against the model's snapshots ---- */

static fn te_history(positive rounds, positive steps)
{
        static p8 address_to snapshots[256];
        static positive snapshot_length[256], snapshot_room[256];

        for (positive round = 0; round < rounds; round++)
        {
                struct doc doc;
                p8 start[256], text[32];
                positive length = te_text(start, sizeof(start)), at_step = 0, count;

                if (length >= 3 && start[0] == 0xef)
                        start[0] = 'a';
                doc_open(&doc, start, length);
                te_model_set(start, length);
                count = steps < 255 ? steps : 255;
                array_store_reserve(snapshots[0], snapshot_room[0], 0, length + 1, 256);
                memory_copy(snapshots[0], start, length);
                snapshot_length[0] = length;
                for (positive step = 1; step <= count; step++)
                {
                        positive at = te_next((p32)te_model_length + 1), removed = te_next(4), size = te_text(text, sizeof(text));

                        removed = removed > te_model_length - at ? te_model_length - at : removed;
                        // Every step changes something: a step that changed nothing would be no undo, and the walk counts undos.
                        if (!removed && !size)
                                text[0] = 'x', size = 1;
                        doc.now += 2000000000ull;
                        doc_group_begin(&doc, DOC_KIND_OTHER, 0);
                        doc_replace(&doc, at, removed, text, size);
                        // Sometimes a second change in the same group: one undo takes both back.
                        if (te_next(3) == 0 && te_model_length - removed + size > 0)
                        {
                                te_model_replace(at, removed, text, size);
                                at = te_next((p32)te_model_length + 1);
                                removed = te_next(2) && at < te_model_length ? 1 : 0;
                                doc_replace(&doc, at, removed, (const p8 address_to) "Q", 1);
                                te_model_replace(at, removed, (const p8 address_to) "Q", 1);
                        }
                        else
                                te_model_replace(at, removed, text, size);
                        doc_group_end(&doc, 0);
                        te_check(te_tree_whole(&doc, doc.root), "the tree after an edit", step);
                        array_store_reserve(snapshots[step], snapshot_room[step], 0, te_model_length + 1, 256);
                        memory_copy(snapshots[step], te_model, te_model_length);
                        snapshot_length[step] = te_model_length;
                }
                at_step = count;
                for (positive walk = 0; walk < count * 3; walk++)
                {
                        b32 back = te_next(2);

                        if (back ? doc_undo(&doc) : doc_redo(&doc))
                                at_step += back ? (positive)-1 : 1;
                        else
                                te_check(back ? at_step == 0 : at_step == count, "undo or redo refused in the middle", at_step);
                        te_model_set(snapshots[at_step], snapshot_length[at_step]);
                        te_check(te_tree_whole(&doc, doc.root), back ? "the tree after an undo" : "the tree after a redo", at_step);
                        te_check(te_same(&doc), "history walk", at_step);
                        te_check(doc_line_count(&doc) == te_model_lines(), "history line count", at_step);
                }
                // A change after an undo drops the redo: the text is that change on the state undone to, and one more
                // undo is that state again.
                if (at_step > 0)
                {
                        doc_undo(&doc);
                        at_step--;
                        doc_replace(&doc, 0, 0, (const p8 address_to) "z", 1);
                        te_check(!doc_redo(&doc), "redo kept after a new change", at_step);
                        te_model_set(snapshots[at_step], snapshot_length[at_step]);
                        te_model_replace(0, 0, (const p8 address_to) "z", 1);
                        te_check(te_same(&doc), "a change after an undo", at_step);
                        te_check(doc_undo(&doc), "the change after an undo is undone", at_step);
                        te_model_set(snapshots[at_step], snapshot_length[at_step]);
                        te_check(te_same(&doc), "undo of the change after an undo", at_step);
                        // And the undo before it is the step before: the redo dropped is not in the way.
                        if (at_step > 0)
                        {
                                te_check(doc_undo(&doc), "an undo past the change after an undo", at_step);
                                te_model_set(snapshots[at_step - 1], snapshot_length[at_step - 1]);
                                te_check(te_same(&doc), "the step before the change after an undo", at_step);
                        }
                }
                doc_close(&doc);
        }
}

/* ---- every command undone and redone ---- */

static b32 te_selections_same(const struct doc_selection address_to a, positive count_a, const struct doc_selection address_to b,
                              positive count_b)
{
        if (count_a != count_b)
                return false;
        for (positive i = 0; i < count_a; i++)
                if (a[i].anchor != b[i].anchor || a[i].active != b[i].active)
                        return false;
        return true;
}

static fn te_commands(positive rounds)
{
        static const p32 commands[] = {
            DOC_CMD_BACKSPACE, DOC_CMD_DELETE, DOC_CMD_DELETE_WORD_LEFT, DOC_CMD_DELETE_WORD_RIGHT, DOC_CMD_DELETE_LINE_LEFT,
            DOC_CMD_DELETE_LINE_RIGHT, DOC_CMD_NEWLINE, DOC_CMD_TAB, DOC_CMD_OUTDENT, DOC_CMD_INDENT, DOC_CMD_MOVE_LINE_UP,
            DOC_CMD_MOVE_LINE_DOWN, DOC_CMD_COPY_LINE_UP, DOC_CMD_COPY_LINE_DOWN, DOC_CMD_DELETE_LINES, DOC_CMD_JOIN_LINES,
            DOC_CMD_TRANSPOSE, DOC_CMD_LINE_BELOW, DOC_CMD_LINE_ABOVE, DOC_CMD_COMMENT, DOC_CMD_UPPER, DOC_CMD_LOWER,
            DOC_CMD_TITLE, DOC_CMD_SORT, DOC_CMD_SORT_DESCENDING};
        static struct doc_selection before[64], after[64];
        static p8 address_to after_text;
        static positive after_room;

        for (positive round = 0; round < rounds; round++)
        {
                struct doc doc;
                p8 start[600];
                positive length = 0, lines = 2 + te_next(8);

                for (positive line = 0; line < lines; line++)
                {
                        positive indent = te_next(3) * 4, words = te_next(4);

                        for (positive i = 0; i < indent; i++)
                                start[length++] = ' ';
                        for (positive w = 0; w < words; w++)
                        {
                                length += te_text(start + length, 8);
                                start[length++] = ' ';
                        }
                        if (line + 1 < lines || te_next(2))
                        {
                                static const char address_to const ends[] = {"\n", "\r\n", "\r"};
                                const char address_to finish = ends[te_next(10) < 8 ? 0 : te_next(3)];

                                memory_copy(start + length, finish, string_length((string_address)finish));
                                length += string_length((string_address)finish);
                        }
                }
                for (positive c = 0; c < sizeof(commands) / sizeof(commands[0]); c++)
                {
                        positive carets = 1 + te_next(3), count_before, count_after;
                        p32 command = commands[c];

                        doc_open(&doc, start, length);
                        doc.now = 0;
                        doc.selection_count = 0;
                        for (positive k = 0; k < carets; k++)
                        {
                                p64 a = te_next((p32)doc_length(&doc) + 1), b = te_next(3) ? a : te_next((p32)doc_length(&doc) + 1);

                                // On cluster boundaries, as every caret the editor makes is.
                                a = doc_cluster_end(&doc, doc_cluster_start(&doc, a));
                                b = doc_cluster_end(&doc, doc_cluster_start(&doc, b));
                                array_store_reserve(doc.selections, doc.selection_room, doc.selection_count, doc.selection_count + 1, 16);
                                doc.selections[doc.selection_count++] = (struct doc_selection){a, b, -1};
                        }
                        doc_normalize(&doc);
                        count_before = doc.selection_count < 64 ? doc.selection_count : 64;
                        memory_copy(before, doc.selections, count_before * sizeof(before[0]));
                        te_model_set(start, length);
                        doc_command(&doc, command, "//");
                        count_after = doc.selection_count < 64 ? doc.selection_count : 64;
                        memory_copy(after, doc.selections, count_after * sizeof(after[0]));
                        array_store_reserve(after_text, after_room, 0, doc_length(&doc) + 1, 4096);
                        {
                                positive after_length = doc_read(&doc, 0, after_text, (positive)doc_length(&doc));
                                b32 changed = doc.done > 0;

                                if (changed)
                                {
                                        te_check(doc_undo(&doc), "undo of a command", command);
                                        te_check(te_same(&doc), "undo gives the text back", command);
                                        te_check(te_selections_same(doc.selections, doc.selection_count, before, count_before),
                                                 "undo gives the selections back", command);
                                        te_check(doc_redo(&doc), "redo of a command", command);
                                        te_model_set(after_text, after_length);
                                        te_check(te_same(&doc), "redo gives the change back", command);
                                        te_check(te_selections_same(doc.selections, doc.selection_count, after, count_after),
                                                 "redo gives the selections back", command);
                                }
                                else
                                        te_check(te_same(&doc), "a command that changed nothing kept the text", command);
                                te_check(doc_line_count(&doc) >= 1, "lines after a command", command);
                        }
                        doc_close(&doc);
                }
        }
}

// Lines moved and copied with their carets, against the text and the places written out by hand.
static fn te_lines(void)
{
        static const struct
        {
                const char address_to text;
                p64 anchor, active;
                p32 command;
                const char address_to want;
                p64 want_anchor, want_active;
        } cases[] = {
            {"a\nbb\nc", 0, 0, DOC_CMD_MOVE_LINE_DOWN, "bb\na\nc", 3, 3},
            {"a\nbb\nc", 4, 4, DOC_CMD_MOVE_LINE_UP, "bb\na\nc", 2, 2},
            {"a\nbb\nc", 3, 4, DOC_CMD_MOVE_LINE_DOWN, "a\nc\nbb", 5, 6},
            {"a\nbb\nc", 2, 6, DOC_CMD_MOVE_LINE_UP, "bb\nc\na", 0, 4},
            {"a\r\nb\r\n", 0, 1, DOC_CMD_MOVE_LINE_DOWN, "b\r\na\r\n", 3, 4},
            {"one\ntwo", 5, 5, DOC_CMD_COPY_LINE_DOWN, "one\ntwo\ntwo", 9, 9},
            {"one\ntwo", 1, 1, DOC_CMD_COPY_LINE_UP, "one\none\ntwo", 1, 1},
            {"one\ntwo", 1, 1, DOC_CMD_COPY_LINE_DOWN, "one\none\ntwo", 5, 5},
            {"x(y)z", 2, 2, DOC_CMD_DELETE_LINES, "", 0, 0},
        };

        for (p32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
                struct doc doc;

                doc_open(&doc, (const p8 address_to)cases[i].text, string_length((string_address)cases[i].text));
                doc_select_set(&doc, cases[i].anchor, cases[i].active);
                doc_command(&doc, cases[i].command, "#");
                te_model_set((const p8 address_to)cases[i].want, string_length((string_address)cases[i].want));
                te_check(te_same(&doc) && doc.selection_count == 1 && doc.selections[0].anchor == cases[i].want_anchor &&
                             doc.selections[0].active == cases[i].want_active,
                         "lines moved or copied with their carets", i * 1000 + doc.selections[0].active);
                doc_close(&doc);
        }
}

/* ---- an edit at many carets is the edits made one at a time ---- */

static fn te_carets(positive rounds)
{
        for (positive round = 0; round < rounds; round++)
        {
                struct doc doc;
                p8 start[300];
                positive length = 0, carets = 2 + te_next(6), places[8], count = 0;
                static const p8 typed[] = "xy";

                // ASCII lines, so every offset is a cluster's boundary.
                for (positive i = 0, n = 20 + te_next(200); i < n; i++)
                        start[length++] = te_next(8) == 0 ? '\n' : (p8)('a' + te_next(26));
                doc_open(&doc, start, length);
                doc.auto_close = false;
                doc.selection_count = 0;
                for (positive k = 0; k < carets; k++)
                {
                        positive at = te_next((p32)length + 1);
                        b32 seen = false;

                        for (positive j = 0; j < count; j++)
                                seen |= places[j] == at;
                        if (seen)
                                continue;
                        places[count++] = at;
                        array_store_reserve(doc.selections, doc.selection_room, doc.selection_count, doc.selection_count + 1, 16);
                        doc.selections[doc.selection_count++] = (struct doc_selection){at, at, -1};
                }
                doc_normalize(&doc);
                doc_type(&doc, typed, 2);
                // The same insertions made one at a time, from the last place to the first.
                te_model_set(start, length);
                for (positive i = 0; i < count; i++)
                        for (positive j = i + 1; j < count; j++)
                                if (places[j] > places[i])
                                {
                                        positive t = places[i];

                                        places[i] = places[j], places[j] = t;
                                }
                for (positive i = 0; i < count; i++)
                        te_model_replace(places[i], 0, typed, 2);
                te_check(te_same(&doc), "typing at many carets", count);
                // Every caret after what it typed.
                for (positive s = 0; s < doc.selection_count; s++)
                        te_check(doc.selections[s].active >= 2 && te_model[doc.selections[s].active - 2] == 'x' &&
                                     te_model[doc.selections[s].active - 1] == 'y',
                                 "a caret after its typing", s);
                // Backspace at every caret takes back what each typed.
                doc_delete(&doc, false, DOC_REACH_CLUSTER);
                doc_delete(&doc, false, DOC_REACH_CLUSTER);
                te_model_set(start, length);
                te_check(te_same(&doc), "backspace at many carets", count);
                doc_close(&doc);
        }
}

/* ---- the official boundaries ---- */
// TEXT_VECTORS BEGIN: every fourth GraphemeBreakTest and every sixth WordBreakTest vector of Unicode 16.0.0, and every
// one with three regional indicators or a ZWJ, made by differential.py --harness text_tables UCD_DIR --write; a code
static const p32 text_grapheme_vectors[] = {
        0x80000020, 0x80000020, 0xffffffff,
        0x80000020, 0x8000000a, 0xffffffff,
        0x80000020, 0x200c, 0xffffffff,
        0x80000020, 0x80000600, 0xffffffff,
        0x80000020, 0x80001100, 0xffffffff,
        0x80000020, 0x800011a8, 0xffffffff,
        0x80000020, 0x8000ac01, 0xffffffff,
        0x80000020, 0x80000904, 0xffffffff,
        0x80000020, 0x80000915, 0xffffffff,
        0x80000020, 0x300, 0xffffffff,
        0x80000020, 0x94d, 0xffffffff,
        0x80000020, 0x200d, 0xffffffff,
        0x80000020, 0x308, 0x200d, 0xffffffff,
        0x80000020, 0x80000378, 0xffffffff,
        0x8000000d, 0x8000000d, 0xffffffff,
        0x8000000d, 0x80000001, 0xffffffff,
        0x8000000d, 0x8001f1e6, 0xffffffff,
        0x8000000d, 0x80000a03, 0xffffffff,
        0x8000000d, 0x80001160, 0xffffffff,
        0x8000000d, 0x8000ac00, 0xffffffff,
        0x8000000d, 0x80000903, 0xffffffff,
        0x8000000d, 0x80000d4e, 0xffffffff,
        0x8000000d, 0x8000231a, 0xffffffff,
        0x8000000d, 0x80000900, 0xffffffff,
        0x8000000d, 0x8000200d, 0xffffffff,
        0x8000000d, 0x80000308, 0x200d, 0xffffffff,
        0x8000000a, 0x80000020, 0xffffffff,
        0x8000000a, 0x8000000a, 0xffffffff,
        0x8000000a, 0x8000200c, 0xffffffff,
        0x8000000a, 0x80000600, 0xffffffff,
        0x8000000a, 0x80001100, 0xffffffff,
        0x8000000a, 0x800011a8, 0xffffffff,
        0x8000000a, 0x8000ac01, 0xffffffff,
        0x8000000a, 0x80000904, 0xffffffff,
        0x8000000a, 0x80000915, 0xffffffff,
        0x8000000a, 0x80000300, 0xffffffff,
        0x8000000a, 0x8000094d, 0xffffffff,
        0x8000000a, 0x8000200d, 0xffffffff,
        0x8000000a, 0x80000308, 0x200d, 0xffffffff,
        0x8000000a, 0x80000378, 0xffffffff,
        0x80000001, 0x8000000d, 0xffffffff,
        0x80000001, 0x80000001, 0xffffffff,
        0x80000001, 0x8001f1e6, 0xffffffff,
        0x80000001, 0x80000a03, 0xffffffff,
        0x80000001, 0x80001160, 0xffffffff,
        0x80000001, 0x8000ac00, 0xffffffff,
        0x80000001, 0x80000903, 0xffffffff,
        0x80000001, 0x80000d4e, 0xffffffff,
        0x80000001, 0x8000231a, 0xffffffff,
        0x80000001, 0x80000900, 0xffffffff,
        0x80000001, 0x8000200d, 0xffffffff,
        0x80000001, 0x80000308, 0x200d, 0xffffffff,
        0x8000200c, 0x80000020, 0xffffffff,
        0x8000200c, 0x8000000a, 0xffffffff,
        0x8000200c, 0x200c, 0xffffffff,
        0x8000200c, 0x80000600, 0xffffffff,
        0x8000200c, 0x80001100, 0xffffffff,
        0x8000200c, 0x800011a8, 0xffffffff,
        0x8000200c, 0x8000ac01, 0xffffffff,
        0x8000200c, 0x80000904, 0xffffffff,
        0x8000200c, 0x80000915, 0xffffffff,
        0x8000200c, 0x300, 0xffffffff,
        0x8000200c, 0x94d, 0xffffffff,
        0x8000200c, 0x200d, 0xffffffff,
        0x8000200c, 0x308, 0x200d, 0xffffffff,
        0x8000200c, 0x80000378, 0xffffffff,
        0x8001f1e6, 0x8000000d, 0xffffffff,
        0x8001f1e6, 0x80000001, 0xffffffff,
        0x8001f1e6, 0x1f1e6, 0xffffffff,
        0x8001f1e6, 0xa03, 0xffffffff,
        0x8001f1e6, 0x80001160, 0xffffffff,
        0x8001f1e6, 0x8000ac00, 0xffffffff,
        0x8001f1e6, 0x903, 0xffffffff,
        0x8001f1e6, 0x80000d4e, 0xffffffff,
        0x8001f1e6, 0x8000231a, 0xffffffff,
        0x8001f1e6, 0x900, 0xffffffff,
        0x8001f1e6, 0x200d, 0xffffffff,
        0x8001f1e6, 0x308, 0x200d, 0xffffffff,
        0x80000600, 0x20, 0xffffffff,
        0x80000600, 0x8000000a, 0xffffffff,
        0x80000600, 0x200c, 0xffffffff,
        0x80000600, 0x600, 0xffffffff,
        0x80000600, 0x1100, 0xffffffff,
        0x80000600, 0x11a8, 0xffffffff,
        0x80000600, 0xac01, 0xffffffff,
        0x80000600, 0x904, 0xffffffff,
        0x80000600, 0x915, 0xffffffff,
        0x80000600, 0x300, 0xffffffff,
        0x80000600, 0x94d, 0xffffffff,
        0x80000600, 0x200d, 0xffffffff,
        0x80000600, 0x308, 0x200d, 0xffffffff,
        0x80000600, 0x378, 0xffffffff,
        0x80000a03, 0x8000000d, 0xffffffff,
        0x80000a03, 0x80000001, 0xffffffff,
        0x80000a03, 0x8001f1e6, 0xffffffff,
        0x80000a03, 0xa03, 0xffffffff,
        0x80000a03, 0x80001160, 0xffffffff,
        0x80000a03, 0x8000ac00, 0xffffffff,
        0x80000a03, 0x903, 0xffffffff,
        0x80000a03, 0x80000d4e, 0xffffffff,
        0x80000a03, 0x8000231a, 0xffffffff,
        0x80000a03, 0x900, 0xffffffff,
        0x80000a03, 0x200d, 0xffffffff,
        0x80000a03, 0x308, 0x200d, 0xffffffff,
        0x80001100, 0x80000020, 0xffffffff,
        0x80001100, 0x8000000a, 0xffffffff,
        0x80001100, 0x200c, 0xffffffff,
        0x80001100, 0x80000600, 0xffffffff,
        0x80001100, 0x1100, 0xffffffff,
        0x80001100, 0x800011a8, 0xffffffff,
        0x80001100, 0xac01, 0xffffffff,
        0x80001100, 0x80000904, 0xffffffff,
        0x80001100, 0x80000915, 0xffffffff,
        0x80001100, 0x300, 0xffffffff,
        0x80001100, 0x94d, 0xffffffff,
        0x80001100, 0x200d, 0xffffffff,
        0x80001100, 0x308, 0x200d, 0xffffffff,
        0x80001100, 0x80000378, 0xffffffff,
        0x80001160, 0x8000000d, 0xffffffff,
        0x80001160, 0x80000001, 0xffffffff,
        0x80001160, 0x8001f1e6, 0xffffffff,
        0x80001160, 0xa03, 0xffffffff,
        0x80001160, 0x1160, 0xffffffff,
        0x80001160, 0x8000ac00, 0xffffffff,
        0x80001160, 0x903, 0xffffffff,
        0x80001160, 0x80000d4e, 0xffffffff,
        0x80001160, 0x8000231a, 0xffffffff,
        0x80001160, 0x900, 0xffffffff,
        0x80001160, 0x200d, 0xffffffff,
        0x80001160, 0x308, 0x200d, 0xffffffff,
        0x800011a8, 0x80000020, 0xffffffff,
        0x800011a8, 0x8000000a, 0xffffffff,
        0x800011a8, 0x200c, 0xffffffff,
        0x800011a8, 0x80000600, 0xffffffff,
        0x800011a8, 0x80001100, 0xffffffff,
        0x800011a8, 0x11a8, 0xffffffff,
        0x800011a8, 0x8000ac01, 0xffffffff,
        0x800011a8, 0x80000904, 0xffffffff,
        0x800011a8, 0x80000915, 0xffffffff,
        0x800011a8, 0x300, 0xffffffff,
        0x800011a8, 0x94d, 0xffffffff,
        0x800011a8, 0x200d, 0xffffffff,
        0x800011a8, 0x308, 0x200d, 0xffffffff,
        0x800011a8, 0x80000378, 0xffffffff,
        0x8000ac00, 0x8000000d, 0xffffffff,
        0x8000ac00, 0x80000001, 0xffffffff,
        0x8000ac00, 0x8001f1e6, 0xffffffff,
        0x8000ac00, 0xa03, 0xffffffff,
        0x8000ac00, 0x1160, 0xffffffff,
        0x8000ac00, 0x8000ac00, 0xffffffff,
        0x8000ac00, 0x903, 0xffffffff,
        0x8000ac00, 0x80000d4e, 0xffffffff,
        0x8000ac00, 0x8000231a, 0xffffffff,
        0x8000ac00, 0x900, 0xffffffff,
        0x8000ac00, 0x200d, 0xffffffff,
        0x8000ac00, 0x308, 0x200d, 0xffffffff,
        0x8000ac01, 0x80000020, 0xffffffff,
        0x8000ac01, 0x8000000a, 0xffffffff,
        0x8000ac01, 0x200c, 0xffffffff,
        0x8000ac01, 0x80000600, 0xffffffff,
        0x8000ac01, 0x80001100, 0xffffffff,
        0x8000ac01, 0x11a8, 0xffffffff,
        0x8000ac01, 0x8000ac01, 0xffffffff,
        0x8000ac01, 0x80000904, 0xffffffff,
        0x8000ac01, 0x80000915, 0xffffffff,
        0x8000ac01, 0x300, 0xffffffff,
        0x8000ac01, 0x94d, 0xffffffff,
        0x8000ac01, 0x200d, 0xffffffff,
        0x8000ac01, 0x308, 0x200d, 0xffffffff,
        0x8000ac01, 0x80000378, 0xffffffff,
        0x80000903, 0x8000000d, 0xffffffff,
        0x80000903, 0x80000001, 0xffffffff,
        0x80000903, 0x8001f1e6, 0xffffffff,
        0x80000903, 0xa03, 0xffffffff,
        0x80000903, 0x80001160, 0xffffffff,
        0x80000903, 0x8000ac00, 0xffffffff,
        0x80000903, 0x903, 0xffffffff,
        0x80000903, 0x80000d4e, 0xffffffff,
        0x80000903, 0x8000231a, 0xffffffff,
        0x80000903, 0x900, 0xffffffff,
        0x80000903, 0x200d, 0xffffffff,
        0x80000903, 0x308, 0x200d, 0xffffffff,
        0x80000904, 0x80000020, 0xffffffff,
        0x80000904, 0x8000000a, 0xffffffff,
        0x80000904, 0x200c, 0xffffffff,
        0x80000904, 0x80000600, 0xffffffff,
        0x80000904, 0x80001100, 0xffffffff,
        0x80000904, 0x800011a8, 0xffffffff,
        0x80000904, 0x8000ac01, 0xffffffff,
        0x80000904, 0x80000904, 0xffffffff,
        0x80000904, 0x80000915, 0xffffffff,
        0x80000904, 0x300, 0xffffffff,
        0x80000904, 0x94d, 0xffffffff,
        0x80000904, 0x200d, 0xffffffff,
        0x80000904, 0x308, 0x200d, 0xffffffff,
        0x80000904, 0x80000378, 0xffffffff,
        0x80000d4e, 0x8000000d, 0xffffffff,
        0x80000d4e, 0x80000001, 0xffffffff,
        0x80000d4e, 0x1f1e6, 0xffffffff,
        0x80000d4e, 0xa03, 0xffffffff,
        0x80000d4e, 0x1160, 0xffffffff,
        0x80000d4e, 0xac00, 0xffffffff,
        0x80000d4e, 0x903, 0xffffffff,
        0x80000d4e, 0xd4e, 0xffffffff,
        0x80000d4e, 0x231a, 0xffffffff,
        0x80000d4e, 0x900, 0xffffffff,
        0x80000d4e, 0x200d, 0xffffffff,
        0x80000d4e, 0x308, 0x200d, 0xffffffff,
        0x80000915, 0x80000020, 0xffffffff,
        0x80000915, 0x8000000a, 0xffffffff,
        0x80000915, 0x200c, 0xffffffff,
        0x80000915, 0x80000600, 0xffffffff,
        0x80000915, 0x80001100, 0xffffffff,
        0x80000915, 0x800011a8, 0xffffffff,
        0x80000915, 0x8000ac01, 0xffffffff,
        0x80000915, 0x80000904, 0xffffffff,
        0x80000915, 0x80000915, 0xffffffff,
        0x80000915, 0x300, 0xffffffff,
        0x80000915, 0x94d, 0xffffffff,
        0x80000915, 0x200d, 0xffffffff,
        0x80000915, 0x308, 0x200d, 0xffffffff,
        0x80000915, 0x80000378, 0xffffffff,
        0x8000231a, 0x8000000d, 0xffffffff,
        0x8000231a, 0x80000001, 0xffffffff,
        0x8000231a, 0x8001f1e6, 0xffffffff,
        0x8000231a, 0xa03, 0xffffffff,
        0x8000231a, 0x80001160, 0xffffffff,
        0x8000231a, 0x8000ac00, 0xffffffff,
        0x8000231a, 0x903, 0xffffffff,
        0x8000231a, 0x80000d4e, 0xffffffff,
        0x8000231a, 0x8000231a, 0xffffffff,
        0x8000231a, 0x900, 0xffffffff,
        0x8000231a, 0x200d, 0xffffffff,
        0x8000231a, 0x308, 0x200d, 0xffffffff,
        0x80000300, 0x80000020, 0xffffffff,
        0x80000300, 0x8000000a, 0xffffffff,
        0x80000300, 0x200c, 0xffffffff,
        0x80000300, 0x80000600, 0xffffffff,
        0x80000300, 0x80001100, 0xffffffff,
        0x80000300, 0x800011a8, 0xffffffff,
        0x80000300, 0x8000ac01, 0xffffffff,
        0x80000300, 0x80000904, 0xffffffff,
        0x80000300, 0x80000915, 0xffffffff,
        0x80000300, 0x300, 0xffffffff,
        0x80000300, 0x94d, 0xffffffff,
        0x80000300, 0x200d, 0xffffffff,
        0x80000300, 0x308, 0x200d, 0xffffffff,
        0x80000300, 0x80000378, 0xffffffff,
        0x80000900, 0x8000000d, 0xffffffff,
        0x80000900, 0x80000001, 0xffffffff,
        0x80000900, 0x8001f1e6, 0xffffffff,
        0x80000900, 0xa03, 0xffffffff,
        0x80000900, 0x80001160, 0xffffffff,
        0x80000900, 0x8000ac00, 0xffffffff,
        0x80000900, 0x903, 0xffffffff,
        0x80000900, 0x80000d4e, 0xffffffff,
        0x80000900, 0x8000231a, 0xffffffff,
        0x80000900, 0x900, 0xffffffff,
        0x80000900, 0x200d, 0xffffffff,
        0x80000900, 0x308, 0x200d, 0xffffffff,
        0x8000094d, 0x80000020, 0xffffffff,
        0x8000094d, 0x8000000a, 0xffffffff,
        0x8000094d, 0x200c, 0xffffffff,
        0x8000094d, 0x80000600, 0xffffffff,
        0x8000094d, 0x80001100, 0xffffffff,
        0x8000094d, 0x800011a8, 0xffffffff,
        0x8000094d, 0x8000ac01, 0xffffffff,
        0x8000094d, 0x80000904, 0xffffffff,
        0x8000094d, 0x80000915, 0xffffffff,
        0x8000094d, 0x300, 0xffffffff,
        0x8000094d, 0x94d, 0xffffffff,
        0x8000094d, 0x200d, 0xffffffff,
        0x8000094d, 0x308, 0x200d, 0xffffffff,
        0x8000094d, 0x80000378, 0xffffffff,
        0x8000200d, 0x80000020, 0xffffffff,
        0x8000200d, 0x308, 0x80000020, 0xffffffff,
        0x8000200d, 0x8000000d, 0xffffffff,
        0x8000200d, 0x308, 0x8000000d, 0xffffffff,
        0x8000200d, 0x8000000a, 0xffffffff,
        0x8000200d, 0x308, 0x8000000a, 0xffffffff,
        0x8000200d, 0x80000001, 0xffffffff,
        0x8000200d, 0x308, 0x80000001, 0xffffffff,
        0x8000200d, 0x200c, 0xffffffff,
        0x8000200d, 0x308, 0x200c, 0xffffffff,
        0x8000200d, 0x8001f1e6, 0xffffffff,
        0x8000200d, 0x308, 0x8001f1e6, 0xffffffff,
        0x8000200d, 0x80000600, 0xffffffff,
        0x8000200d, 0x308, 0x80000600, 0xffffffff,
        0x8000200d, 0xa03, 0xffffffff,
        0x8000200d, 0x308, 0xa03, 0xffffffff,
        0x8000200d, 0x80001100, 0xffffffff,
        0x8000200d, 0x308, 0x80001100, 0xffffffff,
        0x8000200d, 0x80001160, 0xffffffff,
        0x8000200d, 0x308, 0x80001160, 0xffffffff,
        0x8000200d, 0x800011a8, 0xffffffff,
        0x8000200d, 0x308, 0x800011a8, 0xffffffff,
        0x8000200d, 0x8000ac00, 0xffffffff,
        0x8000200d, 0x308, 0x8000ac00, 0xffffffff,
        0x8000200d, 0x8000ac01, 0xffffffff,
        0x8000200d, 0x308, 0x8000ac01, 0xffffffff,
        0x8000200d, 0x903, 0xffffffff,
        0x8000200d, 0x308, 0x903, 0xffffffff,
        0x8000200d, 0x80000904, 0xffffffff,
        0x8000200d, 0x308, 0x80000904, 0xffffffff,
        0x8000200d, 0x80000d4e, 0xffffffff,
        0x8000200d, 0x308, 0x80000d4e, 0xffffffff,
        0x8000200d, 0x80000915, 0xffffffff,
        0x8000200d, 0x308, 0x80000915, 0xffffffff,
        0x8000200d, 0x8000231a, 0xffffffff,
        0x8000200d, 0x308, 0x8000231a, 0xffffffff,
        0x8000200d, 0x300, 0xffffffff,
        0x8000200d, 0x308, 0x300, 0xffffffff,
        0x8000200d, 0x900, 0xffffffff,
        0x8000200d, 0x308, 0x900, 0xffffffff,
        0x8000200d, 0x94d, 0xffffffff,
        0x8000200d, 0x308, 0x94d, 0xffffffff,
        0x8000200d, 0x200d, 0xffffffff,
        0x8000200d, 0x308, 0x200d, 0xffffffff,
        0x8000200d, 0x80000378, 0xffffffff,
        0x8000200d, 0x308, 0x80000378, 0xffffffff,
        0x80000378, 0x80000020, 0xffffffff,
        0x80000378, 0x8000000a, 0xffffffff,
        0x80000378, 0x200c, 0xffffffff,
        0x80000378, 0x80000600, 0xffffffff,
        0x80000378, 0x80001100, 0xffffffff,
        0x80000378, 0x800011a8, 0xffffffff,
        0x80000378, 0x8000ac01, 0xffffffff,
        0x80000378, 0x80000904, 0xffffffff,
        0x80000378, 0x80000915, 0xffffffff,
        0x80000378, 0x300, 0xffffffff,
        0x80000378, 0x94d, 0xffffffff,
        0x80000378, 0x200d, 0xffffffff,
        0x80000378, 0x308, 0x200d, 0xffffffff,
        0x80000378, 0x80000378, 0xffffffff,
        0x80000020, 0x200d, 0x80000646, 0xffffffff,
        0x80000646, 0x200d, 0x80000020, 0xffffffff,
        0x8000ac01, 0x11a8, 0x80001100, 0xffffffff,
        0x8001f1e6, 0x1f1e7, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x1f1e7, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x1f1e7, 0x200d, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x200d, 0x8001f1e7, 0x1f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x1f1e7, 0x8001f1e8, 0x1f1e9, 0x80000062, 0xffffffff,
        0x80000061, 0x200d, 0xffffffff,
        0x80000061, 0x903, 0x80000062, 0xffffffff,
        0x80000061, 0x1f3ff, 0x8001f476, 0x200d, 0x1f6d1, 0xffffffff,
        0x8001f476, 0x1f3ff, 0x308, 0x200d, 0x1f476, 0x1f3ff, 0xffffffff,
        0x8001f6d1, 0x200d, 0x1f6d1, 0xffffffff,
        0x80000061, 0x200d, 0x8001f6d1, 0xffffffff,
        0x80002701, 0x200d, 0x2701, 0xffffffff,
        0x80000061, 0x200d, 0x80002701, 0xffffffff,
        0x80000915, 0x94d, 0x94d, 0x924, 0xffffffff,
        0x80000915, 0x94d, 0x200d, 0x924, 0xffffffff,
        0x80000915, 0x93c, 0x200d, 0x94d, 0x924, 0xffffffff,
        0x80000915, 0x93c, 0x94d, 0x200d, 0x924, 0xffffffff,
        0x80000915, 0x94d, 0x924, 0x94d, 0x92f, 0xffffffff,
        0x80000915, 0x94d, 0x94d, 0x924, 0xffffffff,
};
static const p32 text_word_vectors[] = {
        0x80000001, 0x80000001, 0xffffffff,
        0x80000001, 0x8000000b, 0xffffffff,
        0x80000001, 0x8000003a, 0xffffffff,
        0x80000001, 0x80000030, 0xffffffff,
        0x80000001, 0x800005d0, 0xffffffff,
        0x80000001, 0x8000231a, 0xffffffff,
        0x80000001, 0x300, 0xffffffff,
        0x80000001, 0x200d, 0xffffffff,
        0x80000001, 0x308, 0x200d, 0xffffffff,
        0x80000001, 0x80000061, 0x8000003a, 0xffffffff,
        0x80000001, 0x80000061, 0x8000002c, 0xffffffff,
        0x80000001, 0x80000031, 0x8000002c, 0xffffffff,
        0x8000000d, 0x8000000d, 0xffffffff,
        0x8000000d, 0x80003031, 0xffffffff,
        0x8000000d, 0x8000002c, 0xffffffff,
        0x8000000d, 0x8000005f, 0xffffffff,
        0x8000000d, 0x80000022, 0xffffffff,
        0x8000000d, 0x80000020, 0xffffffff,
        0x8000000d, 0x8000200d, 0xffffffff,
        0x8000000d, 0x80000308, 0x200d, 0xffffffff,
        0x8000000d, 0x80000061, 0x80000027, 0xffffffff,
        0x8000000d, 0x80000031, 0x8000003a, 0xffffffff,
        0x8000000d, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x8000000a, 0x8000000a, 0xffffffff,
        0x8000000a, 0x80000041, 0xffffffff,
        0x8000000a, 0x8000002e, 0xffffffff,
        0x8000000a, 0x8001f1e6, 0xffffffff,
        0x8000000a, 0x80000027, 0xffffffff,
        0x8000000a, 0x800000ad, 0xffffffff,
        0x8000000a, 0x8000200d, 0xffffffff,
        0x8000000a, 0x80000308, 0x200d, 0xffffffff,
        0x8000000a, 0x80000061, 0x2060, 0xffffffff,
        0x8000000a, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x8000000a, 0x80000031, 0x80000027, 0xffffffff,
        0x8000000b, 0x80000001, 0xffffffff,
        0x8000000b, 0x8000000b, 0xffffffff,
        0x8000000b, 0x8000003a, 0xffffffff,
        0x8000000b, 0x80000030, 0xffffffff,
        0x8000000b, 0x800005d0, 0xffffffff,
        0x8000000b, 0x8000231a, 0xffffffff,
        0x8000000b, 0x80000300, 0xffffffff,
        0x8000000b, 0x8000200d, 0xffffffff,
        0x8000000b, 0x80000308, 0x200d, 0xffffffff,
        0x8000000b, 0x80000061, 0x8000003a, 0xffffffff,
        0x8000000b, 0x80000061, 0x8000002c, 0xffffffff,
        0x8000000b, 0x80000031, 0x8000002c, 0xffffffff,
        0x80003031, 0x8000000d, 0xffffffff,
        0x80003031, 0x3031, 0xffffffff,
        0x80003031, 0x8000002c, 0xffffffff,
        0x80003031, 0x5f, 0xffffffff,
        0x80003031, 0x80000022, 0xffffffff,
        0x80003031, 0x80000020, 0xffffffff,
        0x80003031, 0x200d, 0xffffffff,
        0x80003031, 0x308, 0x200d, 0xffffffff,
        0x80003031, 0x80000061, 0x80000027, 0xffffffff,
        0x80003031, 0x80000031, 0x8000003a, 0xffffffff,
        0x80003031, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x80000041, 0x8000000a, 0xffffffff,
        0x80000041, 0x41, 0xffffffff,
        0x80000041, 0x8000002e, 0xffffffff,
        0x80000041, 0x8001f1e6, 0xffffffff,
        0x80000041, 0x80000027, 0xffffffff,
        0x80000041, 0xad, 0xffffffff,
        0x80000041, 0x200d, 0xffffffff,
        0x80000041, 0x308, 0x200d, 0xffffffff,
        0x80000041, 0x61, 0x2060, 0xffffffff,
        0x80000041, 0x61, 0x80000027, 0x2060, 0xffffffff,
        0x80000041, 0x31, 0x80000027, 0xffffffff,
        0x8000003a, 0x80000001, 0xffffffff,
        0x8000003a, 0x8000000b, 0xffffffff,
        0x8000003a, 0x8000003a, 0xffffffff,
        0x8000003a, 0x80000030, 0xffffffff,
        0x8000003a, 0x800005d0, 0xffffffff,
        0x8000003a, 0x8000231a, 0xffffffff,
        0x8000003a, 0x300, 0xffffffff,
        0x8000003a, 0x200d, 0xffffffff,
        0x8000003a, 0x308, 0x200d, 0xffffffff,
        0x8000003a, 0x80000061, 0x8000003a, 0xffffffff,
        0x8000003a, 0x80000061, 0x8000002c, 0xffffffff,
        0x8000003a, 0x80000031, 0x8000002c, 0xffffffff,
        0x8000002c, 0x8000000d, 0xffffffff,
        0x8000002c, 0x80003031, 0xffffffff,
        0x8000002c, 0x8000002c, 0xffffffff,
        0x8000002c, 0x8000005f, 0xffffffff,
        0x8000002c, 0x80000022, 0xffffffff,
        0x8000002c, 0x80000020, 0xffffffff,
        0x8000002c, 0x200d, 0xffffffff,
        0x8000002c, 0x308, 0x200d, 0xffffffff,
        0x8000002c, 0x80000061, 0x80000027, 0xffffffff,
        0x8000002c, 0x80000031, 0x8000003a, 0xffffffff,
        0x8000002c, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x8000002e, 0x8000000a, 0xffffffff,
        0x8000002e, 0x80000041, 0xffffffff,
        0x8000002e, 0x8000002e, 0xffffffff,
        0x8000002e, 0x8001f1e6, 0xffffffff,
        0x8000002e, 0x80000027, 0xffffffff,
        0x8000002e, 0xad, 0xffffffff,
        0x8000002e, 0x200d, 0xffffffff,
        0x8000002e, 0x308, 0x200d, 0xffffffff,
        0x8000002e, 0x80000061, 0x2060, 0xffffffff,
        0x8000002e, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x8000002e, 0x80000031, 0x80000027, 0xffffffff,
        0x80000030, 0x80000001, 0xffffffff,
        0x80000030, 0x8000000b, 0xffffffff,
        0x80000030, 0x8000003a, 0xffffffff,
        0x80000030, 0x30, 0xffffffff,
        0x80000030, 0x5d0, 0xffffffff,
        0x80000030, 0x8000231a, 0xffffffff,
        0x80000030, 0x300, 0xffffffff,
        0x80000030, 0x200d, 0xffffffff,
        0x80000030, 0x308, 0x200d, 0xffffffff,
        0x80000030, 0x61, 0x8000003a, 0xffffffff,
        0x80000030, 0x61, 0x8000002c, 0xffffffff,
        0x80000030, 0x31, 0x8000002c, 0xffffffff,
        0x8000005f, 0x8000000d, 0xffffffff,
        0x8000005f, 0x3031, 0xffffffff,
        0x8000005f, 0x8000002c, 0xffffffff,
        0x8000005f, 0x5f, 0xffffffff,
        0x8000005f, 0x80000022, 0xffffffff,
        0x8000005f, 0x80000020, 0xffffffff,
        0x8000005f, 0x200d, 0xffffffff,
        0x8000005f, 0x308, 0x200d, 0xffffffff,
        0x8000005f, 0x61, 0x80000027, 0xffffffff,
        0x8000005f, 0x31, 0x8000003a, 0xffffffff,
        0x8000005f, 0x31, 0x8000002e, 0x2060, 0xffffffff,
        0x8001f1e6, 0x8000000a, 0xffffffff,
        0x8001f1e6, 0x80000041, 0xffffffff,
        0x8001f1e6, 0x8000002e, 0xffffffff,
        0x8001f1e6, 0x1f1e6, 0xffffffff,
        0x8001f1e6, 0x80000027, 0xffffffff,
        0x8001f1e6, 0xad, 0xffffffff,
        0x8001f1e6, 0x200d, 0xffffffff,
        0x8001f1e6, 0x308, 0x200d, 0xffffffff,
        0x8001f1e6, 0x80000061, 0x2060, 0xffffffff,
        0x8001f1e6, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x8001f1e6, 0x80000031, 0x80000027, 0xffffffff,
        0x800005d0, 0x80000001, 0xffffffff,
        0x800005d0, 0x8000000b, 0xffffffff,
        0x800005d0, 0x8000003a, 0xffffffff,
        0x800005d0, 0x30, 0xffffffff,
        0x800005d0, 0x5d0, 0xffffffff,
        0x800005d0, 0x8000231a, 0xffffffff,
        0x800005d0, 0x300, 0xffffffff,
        0x800005d0, 0x200d, 0xffffffff,
        0x800005d0, 0x308, 0x200d, 0xffffffff,
        0x800005d0, 0x61, 0x8000003a, 0xffffffff,
        0x800005d0, 0x61, 0x8000002c, 0xffffffff,
        0x800005d0, 0x31, 0x8000002c, 0xffffffff,
        0x80000022, 0x8000000d, 0xffffffff,
        0x80000022, 0x80003031, 0xffffffff,
        0x80000022, 0x8000002c, 0xffffffff,
        0x80000022, 0x8000005f, 0xffffffff,
        0x80000022, 0x80000022, 0xffffffff,
        0x80000022, 0x80000020, 0xffffffff,
        0x80000022, 0x200d, 0xffffffff,
        0x80000022, 0x308, 0x200d, 0xffffffff,
        0x80000022, 0x80000061, 0x80000027, 0xffffffff,
        0x80000022, 0x80000031, 0x8000003a, 0xffffffff,
        0x80000022, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x80000027, 0x8000000a, 0xffffffff,
        0x80000027, 0x80000041, 0xffffffff,
        0x80000027, 0x8000002e, 0xffffffff,
        0x80000027, 0x8001f1e6, 0xffffffff,
        0x80000027, 0x80000027, 0xffffffff,
        0x80000027, 0xad, 0xffffffff,
        0x80000027, 0x200d, 0xffffffff,
        0x80000027, 0x308, 0x200d, 0xffffffff,
        0x80000027, 0x80000061, 0x2060, 0xffffffff,
        0x80000027, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x80000027, 0x80000031, 0x80000027, 0xffffffff,
        0x8000231a, 0x80000001, 0xffffffff,
        0x8000231a, 0x8000000b, 0xffffffff,
        0x8000231a, 0x8000003a, 0xffffffff,
        0x8000231a, 0x80000030, 0xffffffff,
        0x8000231a, 0x800005d0, 0xffffffff,
        0x8000231a, 0x8000231a, 0xffffffff,
        0x8000231a, 0x300, 0xffffffff,
        0x8000231a, 0x200d, 0xffffffff,
        0x8000231a, 0x308, 0x200d, 0xffffffff,
        0x8000231a, 0x80000061, 0x8000003a, 0xffffffff,
        0x8000231a, 0x80000061, 0x8000002c, 0xffffffff,
        0x8000231a, 0x80000031, 0x8000002c, 0xffffffff,
        0x80000020, 0x8000000d, 0xffffffff,
        0x80000020, 0x80003031, 0xffffffff,
        0x80000020, 0x8000002c, 0xffffffff,
        0x80000020, 0x8000005f, 0xffffffff,
        0x80000020, 0x80000022, 0xffffffff,
        0x80000020, 0x20, 0xffffffff,
        0x80000020, 0x200d, 0xffffffff,
        0x80000020, 0x308, 0x200d, 0xffffffff,
        0x80000020, 0x80000061, 0x80000027, 0xffffffff,
        0x80000020, 0x80000031, 0x8000003a, 0xffffffff,
        0x80000020, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x800000ad, 0x8000000a, 0xffffffff,
        0x800000ad, 0x80000041, 0xffffffff,
        0x800000ad, 0x8000002e, 0xffffffff,
        0x800000ad, 0x8001f1e6, 0xffffffff,
        0x800000ad, 0x80000027, 0xffffffff,
        0x800000ad, 0xad, 0xffffffff,
        0x800000ad, 0x200d, 0xffffffff,
        0x800000ad, 0x308, 0x200d, 0xffffffff,
        0x800000ad, 0x80000061, 0x2060, 0xffffffff,
        0x800000ad, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x800000ad, 0x80000031, 0x80000027, 0xffffffff,
        0x80000300, 0x80000001, 0xffffffff,
        0x80000300, 0x8000000b, 0xffffffff,
        0x80000300, 0x8000003a, 0xffffffff,
        0x80000300, 0x80000030, 0xffffffff,
        0x80000300, 0x800005d0, 0xffffffff,
        0x80000300, 0x8000231a, 0xffffffff,
        0x80000300, 0x300, 0xffffffff,
        0x80000300, 0x200d, 0xffffffff,
        0x80000300, 0x308, 0x200d, 0xffffffff,
        0x80000300, 0x80000061, 0x8000003a, 0xffffffff,
        0x80000300, 0x80000061, 0x8000002c, 0xffffffff,
        0x80000300, 0x80000031, 0x8000002c, 0xffffffff,
        0x8000200d, 0x80000001, 0xffffffff,
        0x8000200d, 0x308, 0x80000001, 0xffffffff,
        0x8000200d, 0x8000000d, 0xffffffff,
        0x8000200d, 0x308, 0x8000000d, 0xffffffff,
        0x8000200d, 0x8000000a, 0xffffffff,
        0x8000200d, 0x308, 0x8000000a, 0xffffffff,
        0x8000200d, 0x8000000b, 0xffffffff,
        0x8000200d, 0x308, 0x8000000b, 0xffffffff,
        0x8000200d, 0x80003031, 0xffffffff,
        0x8000200d, 0x308, 0x80003031, 0xffffffff,
        0x8000200d, 0x80000041, 0xffffffff,
        0x8000200d, 0x308, 0x80000041, 0xffffffff,
        0x8000200d, 0x8000003a, 0xffffffff,
        0x8000200d, 0x308, 0x8000003a, 0xffffffff,
        0x8000200d, 0x8000002c, 0xffffffff,
        0x8000200d, 0x308, 0x8000002c, 0xffffffff,
        0x8000200d, 0x8000002e, 0xffffffff,
        0x8000200d, 0x308, 0x8000002e, 0xffffffff,
        0x8000200d, 0x80000030, 0xffffffff,
        0x8000200d, 0x308, 0x80000030, 0xffffffff,
        0x8000200d, 0x8000005f, 0xffffffff,
        0x8000200d, 0x308, 0x8000005f, 0xffffffff,
        0x8000200d, 0x8001f1e6, 0xffffffff,
        0x8000200d, 0x308, 0x8001f1e6, 0xffffffff,
        0x8000200d, 0x800005d0, 0xffffffff,
        0x8000200d, 0x308, 0x800005d0, 0xffffffff,
        0x8000200d, 0x80000022, 0xffffffff,
        0x8000200d, 0x308, 0x80000022, 0xffffffff,
        0x8000200d, 0x80000027, 0xffffffff,
        0x8000200d, 0x308, 0x80000027, 0xffffffff,
        0x8000200d, 0x231a, 0xffffffff,
        0x8000200d, 0x308, 0x8000231a, 0xffffffff,
        0x8000200d, 0x80000020, 0xffffffff,
        0x8000200d, 0x308, 0x80000020, 0xffffffff,
        0x8000200d, 0xad, 0xffffffff,
        0x8000200d, 0x308, 0xad, 0xffffffff,
        0x8000200d, 0x300, 0xffffffff,
        0x8000200d, 0x308, 0x300, 0xffffffff,
        0x8000200d, 0x200d, 0xffffffff,
        0x8000200d, 0x308, 0x200d, 0xffffffff,
        0x8000200d, 0x80000061, 0x2060, 0xffffffff,
        0x8000200d, 0x308, 0x80000061, 0x2060, 0xffffffff,
        0x8000200d, 0x80000061, 0x8000003a, 0xffffffff,
        0x8000200d, 0x308, 0x80000061, 0x8000003a, 0xffffffff,
        0x8000200d, 0x80000061, 0x80000027, 0xffffffff,
        0x8000200d, 0x308, 0x80000061, 0x80000027, 0xffffffff,
        0x8000200d, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x8000200d, 0x308, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x8000200d, 0x80000061, 0x8000002c, 0xffffffff,
        0x8000200d, 0x308, 0x80000061, 0x8000002c, 0xffffffff,
        0x8000200d, 0x80000031, 0x8000003a, 0xffffffff,
        0x8000200d, 0x308, 0x80000031, 0x8000003a, 0xffffffff,
        0x8000200d, 0x80000031, 0x80000027, 0xffffffff,
        0x8000200d, 0x308, 0x80000031, 0x80000027, 0xffffffff,
        0x8000200d, 0x80000031, 0x8000002c, 0xffffffff,
        0x8000200d, 0x308, 0x80000031, 0x8000002c, 0xffffffff,
        0x8000200d, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x8000200d, 0x308, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x80000061, 0x2060, 0x8000000a, 0xffffffff,
        0x80000061, 0x2060, 0x41, 0xffffffff,
        0x80000061, 0x2060, 0x8000002e, 0xffffffff,
        0x80000061, 0x2060, 0x8001f1e6, 0xffffffff,
        0x80000061, 0x2060, 0x80000027, 0xffffffff,
        0x80000061, 0x2060, 0xad, 0xffffffff,
        0x80000061, 0x2060, 0x200d, 0xffffffff,
        0x80000061, 0x2060, 0x308, 0x200d, 0xffffffff,
        0x80000061, 0x2060, 0x61, 0x2060, 0xffffffff,
        0x80000061, 0x2060, 0x61, 0x80000027, 0x2060, 0xffffffff,
        0x80000061, 0x2060, 0x31, 0x80000027, 0xffffffff,
        0x80000061, 0x8000003a, 0x80000001, 0xffffffff,
        0x80000061, 0x8000003a, 0x8000000b, 0xffffffff,
        0x80000061, 0x8000003a, 0x8000003a, 0xffffffff,
        0x80000061, 0x8000003a, 0x80000030, 0xffffffff,
        0x80000061, 0x3a, 0x5d0, 0xffffffff,
        0x80000061, 0x8000003a, 0x8000231a, 0xffffffff,
        0x80000061, 0x8000003a, 0x300, 0xffffffff,
        0x80000061, 0x8000003a, 0x200d, 0xffffffff,
        0x80000061, 0x8000003a, 0x308, 0x200d, 0xffffffff,
        0x80000061, 0x3a, 0x61, 0x8000003a, 0xffffffff,
        0x80000061, 0x3a, 0x61, 0x8000002c, 0xffffffff,
        0x80000061, 0x8000003a, 0x80000031, 0x8000002c, 0xffffffff,
        0x80000061, 0x80000027, 0x8000000d, 0xffffffff,
        0x80000061, 0x80000027, 0x80003031, 0xffffffff,
        0x80000061, 0x80000027, 0x8000002c, 0xffffffff,
        0x80000061, 0x80000027, 0x8000005f, 0xffffffff,
        0x80000061, 0x80000027, 0x80000022, 0xffffffff,
        0x80000061, 0x80000027, 0x80000020, 0xffffffff,
        0x80000061, 0x80000027, 0x200d, 0xffffffff,
        0x80000061, 0x80000027, 0x308, 0x200d, 0xffffffff,
        0x80000061, 0x27, 0x61, 0x80000027, 0xffffffff,
        0x80000061, 0x80000027, 0x80000031, 0x8000003a, 0xffffffff,
        0x80000061, 0x80000027, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x8000000a, 0xffffffff,
        0x80000061, 0x27, 0x2060, 0x41, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x8000002e, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x8001f1e6, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x80000027, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0xad, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x200d, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x308, 0x200d, 0xffffffff,
        0x80000061, 0x27, 0x2060, 0x61, 0x2060, 0xffffffff,
        0x80000061, 0x27, 0x2060, 0x61, 0x80000027, 0x2060, 0xffffffff,
        0x80000061, 0x80000027, 0x2060, 0x80000031, 0x80000027, 0xffffffff,
        0x80000061, 0x8000002c, 0x80000001, 0xffffffff,
        0x80000061, 0x8000002c, 0x8000000b, 0xffffffff,
        0x80000061, 0x8000002c, 0x8000003a, 0xffffffff,
        0x80000061, 0x8000002c, 0x80000030, 0xffffffff,
        0x80000061, 0x8000002c, 0x800005d0, 0xffffffff,
        0x80000061, 0x8000002c, 0x8000231a, 0xffffffff,
        0x80000061, 0x8000002c, 0x300, 0xffffffff,
        0x80000061, 0x8000002c, 0x200d, 0xffffffff,
        0x80000061, 0x8000002c, 0x308, 0x200d, 0xffffffff,
        0x80000061, 0x8000002c, 0x80000061, 0x8000003a, 0xffffffff,
        0x80000061, 0x8000002c, 0x80000061, 0x8000002c, 0xffffffff,
        0x80000061, 0x8000002c, 0x80000031, 0x8000002c, 0xffffffff,
        0x80000031, 0x8000003a, 0x8000000d, 0xffffffff,
        0x80000031, 0x8000003a, 0x80003031, 0xffffffff,
        0x80000031, 0x8000003a, 0x8000002c, 0xffffffff,
        0x80000031, 0x8000003a, 0x8000005f, 0xffffffff,
        0x80000031, 0x8000003a, 0x80000022, 0xffffffff,
        0x80000031, 0x8000003a, 0x80000020, 0xffffffff,
        0x80000031, 0x8000003a, 0x200d, 0xffffffff,
        0x80000031, 0x8000003a, 0x308, 0x200d, 0xffffffff,
        0x80000031, 0x8000003a, 0x80000061, 0x80000027, 0xffffffff,
        0x80000031, 0x8000003a, 0x80000031, 0x8000003a, 0xffffffff,
        0x80000031, 0x8000003a, 0x80000031, 0x8000002e, 0x2060, 0xffffffff,
        0x80000031, 0x80000027, 0x8000000a, 0xffffffff,
        0x80000031, 0x80000027, 0x80000041, 0xffffffff,
        0x80000031, 0x80000027, 0x8000002e, 0xffffffff,
        0x80000031, 0x80000027, 0x8001f1e6, 0xffffffff,
        0x80000031, 0x80000027, 0x80000027, 0xffffffff,
        0x80000031, 0x80000027, 0xad, 0xffffffff,
        0x80000031, 0x80000027, 0x200d, 0xffffffff,
        0x80000031, 0x80000027, 0x308, 0x200d, 0xffffffff,
        0x80000031, 0x80000027, 0x80000061, 0x2060, 0xffffffff,
        0x80000031, 0x80000027, 0x80000061, 0x80000027, 0x2060, 0xffffffff,
        0x80000031, 0x27, 0x31, 0x80000027, 0xffffffff,
        0x80000031, 0x8000002c, 0x80000001, 0xffffffff,
        0x80000031, 0x8000002c, 0x8000000b, 0xffffffff,
        0x80000031, 0x8000002c, 0x8000003a, 0xffffffff,
        0x80000031, 0x2c, 0x30, 0xffffffff,
        0x80000031, 0x8000002c, 0x800005d0, 0xffffffff,
        0x80000031, 0x8000002c, 0x8000231a, 0xffffffff,
        0x80000031, 0x8000002c, 0x300, 0xffffffff,
        0x80000031, 0x8000002c, 0x200d, 0xffffffff,
        0x80000031, 0x8000002c, 0x308, 0x200d, 0xffffffff,
        0x80000031, 0x8000002c, 0x80000061, 0x8000003a, 0xffffffff,
        0x80000031, 0x8000002c, 0x80000061, 0x8000002c, 0xffffffff,
        0x80000031, 0x2c, 0x31, 0x8000002c, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x8000000d, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x80003031, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x8000002c, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x8000005f, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x80000022, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x80000020, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x200d, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x308, 0x200d, 0xffffffff,
        0x80000031, 0x8000002e, 0x2060, 0x80000061, 0x80000027, 0xffffffff,
        0x80000031, 0x2e, 0x2060, 0x31, 0x8000003a, 0xffffffff,
        0x80000031, 0x2e, 0x2060, 0x31, 0x8000002e, 0x2060, 0xffffffff,
        0x80000020, 0x200d, 0x80000646, 0xffffffff,
        0x80000646, 0x200d, 0x80000020, 0xffffffff,
        0x80000671, 0x644, 0x631, 0x64e, 0x651, 0x62d, 0x650, 0x64a, 0x645, 0x650, 0x80000020, 0x800006dd, 0x661, 0xffffffff,
        0x800005d0, 0x27, 0xffffffff,
        0x80000041, 0x5f, 0x30, 0x5f, 0x3031, 0x5f, 0xffffffff,
        0x8001f1e6, 0x1f1e7, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x1f1e7, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x1f1e7, 0x200d, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x200d, 0x1f1e7, 0x8001f1e8, 0x80000062, 0xffffffff,
        0x80000061, 0x8001f1e6, 0x1f1e7, 0x8001f1e8, 0x1f1e9, 0x80000062, 0xffffffff,
        0x8001f6d1, 0x200d, 0x1f6d1, 0xffffffff,
        0x80000061, 0x200d, 0x1f6d1, 0xffffffff,
        0x80002701, 0x200d, 0x2701, 0xffffffff,
        0x80000061, 0x200d, 0x2701, 0xffffffff,
        0x8001f476, 0x1f3ff, 0x308, 0x200d, 0x1f476, 0x1f3ff, 0xffffffff,
        0x8000200d, 0x1f6d1, 0x1f3ff, 0xffffffff,
        0x8000200d, 0x1f6d1, 0xffffffff,
        0x8000200d, 0x1f6d1, 0xffffffff,
        0x80000061, 0x308, 0x200d, 0x308, 0x62, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000003a, 0x8000003a, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000003a, 0x8000002e, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000003a, 0x8000002c, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000002e, 0x8000003a, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000002e, 0x8000002e, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000002e, 0x8000002c, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000002c, 0x8000003a, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000002c, 0x8000002e, 0x80000061, 0xffffffff,
        0x80000031, 0x5f, 0x31, 0x8000002c, 0x8000002c, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000003a, 0x8000003a, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000003a, 0x8000002e, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000003a, 0x8000002c, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000002e, 0x8000003a, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000002e, 0x8000002e, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000002e, 0x8000002c, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000002c, 0x8000003a, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000002c, 0x8000002e, 0x80000061, 0xffffffff,
        0x80000061, 0x5f, 0x31, 0x8000002c, 0x8000002c, 0x80000061, 0xffffffff,
};
// TEXT_VECTORS END

// A vector's code points as UTF-8 into bytes, with each one's offset: how many code points.
static positive te_vector(const p32 address_to words, p8 address_to bytes, positive address_to length, p32 address_to offsets,
                          p8 address_to cuts)
{
        positive count = 0;

        *length = 0;
        for (; words[count] != 0xffffffffu; count++)
        {
                offsets[count] = (p32)*length;
                cuts[count] = (p8)(words[count] >> 31);
                *length += memory_utf8_encode(bytes + *length, 4, words[count] & 0x1fffff);
        }
        offsets[count] = (p32)*length;
        return count;
}

static fn te_boundaries(void)
{
        positive vectors = 0;

        for (const p32 address_to words = text_grapheme_vectors; words < text_grapheme_vectors + sizeof(text_grapheme_vectors) / 4;)
        {
                p8 bytes[128], cuts[40];
                p32 offsets[41];
                positive length, count = te_vector(words, bytes, &length, offsets, cuts), at = 0, point = 0;
                struct doc doc;
                b32 same = true;

                doc_open(&doc, bytes, length);
                // Forward: every cluster's end is the next break the file marks.
                while (at < length)
                {
                        p64 finish = doc_cluster_end(&doc, at);

                        point++;
                        while (point < count && !cuts[point])
                                point++;
                        same &= finish == offsets[point];
                        at = finish > at ? finish : length;
                }
                // Backward: every cluster's start, from the end.
                at = length;
                point = count;
                while (at > 0)
                {
                        p64 start = doc_cluster_start(&doc, at);

                        point--;
                        while (point > 0 && !cuts[point])
                                point--;
                        same &= start == offsets[point];
                        at = start < at ? start : 0;
                }
                te_check(same, "grapheme vector", vectors);
                doc_close(&doc);
                words += count + 1;
                vectors++;
        }
        for (const p32 address_to words = text_word_vectors; words < text_word_vectors + sizeof(text_word_vectors) / 4;)
        {
                p16 props[40];
                p8 breaks[41];
                positive count = 0;
                b32 same = true;

                while (words[count] != 0xffffffffu)
                {
                        props[count] = doc_props(words[count] & 0x1fffff);
                        count++;
                }
                doc_word_breaks(props, count, breaks);
                for (positive i = 1; i < count; i++)
                        same &= breaks[i] == (words[i] >> 31);
                te_check(same, "word vector", vectors);
                words += count + 1;
                vectors++;
        }
        // Backspace: the last mark of a letter, a whole emoji sequence.
        {
                struct doc doc;
                static const p8 marks[] = "e\xcc\x81\xcc\x82", family[] = "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9";

                doc_open(&doc, marks, sizeof(marks) - 1);
                te_check(doc_backspace_start(&doc, doc_length(&doc)) == 3, "backspace takes the last mark", 0);
                te_check(doc_cluster_start(&doc, doc_length(&doc)) == 0, "the marks are one cluster", 0);
                doc_close(&doc);
                doc_open(&doc, family, sizeof(family) - 1);
                te_check(doc_backspace_start(&doc, doc_length(&doc)) == 0, "backspace takes a whole emoji sequence", 0);
                doc_close(&doc);
        }
}

/* ---- find against the regex engine ---- */

static fn te_find(positive rounds)
{
        static const char address_to const patterns[] = {"a", "ab", "a.b", "[ab]+", "(a|b)c", "b*", "c$", "^a", "x[^a]y", "(ab)+c",
                                                         "\xc3\xa9", ".", "a{2,3}", "(a)(b)?"};
        static p8 replaced[65536];

        for (positive round = 0; round < rounds; round++)
        {
                struct doc doc;
                p8 start[400];
                positive length = 0;

                for (positive i = 0, n = 50 + te_next(300); i < n; i++)
                {
                        p32 pick = te_next(12);

                        if (pick == 0)
                                start[length++] = '\n';
                        else if (pick == 1 && length + 2 < sizeof(start))
                                start[length++] = 0xc3, start[length++] = 0xa9;
                        else
                                start[length++] = (p8)("abcxy "[te_next(6)]);
                }
                doc_open(&doc, start, length);
                te_model_set(start, length);
                for (positive p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++)
                {
                        struct doc_find find = {0};
                        positive pattern_length = string_length((string_address)patterns[p]), found = 0, expected = 0;
                        p64 at = 0, s, e;
                        b32 same = true;

                        te_check(doc_find_set(&find, (const p8 address_to)patterns[p], pattern_length, DOC_FIND_CASE | DOC_FIND_REGEX),
                                 "pattern compiles", p);
                        // The engine on each line of the model, every match after the last (an empty one moves on a character).
                        for (positive line = 0; line < te_model_lines(); line++)
                        {
                                positive line_start = te_model_line_start(line), finish = line_start;

                                while (finish < te_model_length && te_model[finish] != '\n')
                                        finish++;
                                for (positive from = 0; from <= finish - line_start;)
                                {
                                        p64 want_s, want_e;
                                        p8 bytes[512];

                                        memory_copy(bytes, te_model + line_start, finish - line_start);
                                        bytes[finish - line_start] = 0;
                                        if (rx_find(&regex_match, &find.program, REGEX_LONGEST, true, bytes, finish - line_start, from) != RX_MATCH)
                                                break;
                                        want_s = line_start + regex_slots[0];
                                        want_e = line_start + regex_slots[1];
                                        expected++;
                                        if (doc_find_next(&doc, &find, at, false, false, &s, &e))
                                        {
                                                same &= s == want_s && e == want_e;
                                                found++;
                                                at = e > s ? e : s + 1;
                                                while (at < te_model_length && (te_model[at] & 0xc0) == 0x80)
                                                        at++;
                                        }
                                        else
                                                same = false;
                                        from = regex_slots[1] > regex_slots[0] ? regex_slots[1] : regex_slots[0] + 1;
                                        while (from < finish - line_start && (bytes[from] & 0xc0) == 0x80)
                                                from++;
                                }
                        }
                        te_check(same && found == expected, "find against the regex engine", p);
                        // Replace every match: the same text the model makes by hand.
                        {
                                positive out = 0, replaced_count, model_at = 0;
                                static const p8 with[] = "<$1|$&>";

                                for (positive line = 0; line < te_model_lines(); line++)
                                {
                                        positive line_start = te_model_line_start(line), finish = line_start, from = 0;
                                        p8 bytes[512];

                                        while (finish < te_model_length && te_model[finish] != '\n')
                                                finish++;
                                        memory_copy(bytes, te_model + line_start, finish - line_start);
                                        bytes[finish - line_start] = 0;
                                        while (from <= finish - line_start &&
                                               rx_find(&regex_match, &find.program, REGEX_LONGEST, true, bytes, finish - line_start, from) == RX_MATCH)
                                        {
                                                positive ms = regex_slots[0], me = regex_slots[1];

                                                memory_copy(replaced + out, te_model + model_at, line_start + ms - model_at);
                                                out += line_start + ms - model_at;
                                                out += doc_find_replacement(&find, bytes, with, sizeof(with) - 1, replaced + out, 4096);
                                                model_at = line_start + me;
                                                from = me > ms ? me : ms + 1;
                                                while (from < finish - line_start && (bytes[from] & 0xc0) == 0x80)
                                                        from++;
                                        }
                                }
                                memory_copy(replaced + out, te_model + model_at, te_model_length - model_at);
                                out += te_model_length - model_at;
                                replaced_count = doc_replace_all(&doc, &find, with, sizeof(with) - 1);
                                {
                                        p8 keep[400];
                                        positive keep_length = te_model_length;

                                        memory_copy(keep, te_model, keep_length);
                                        te_model_set(replaced, out);
                                        te_check(te_same(&doc), "replace all against the model", p);
                                        te_check(replaced_count == expected, "replace all counts every match", replaced_count * 100000 + expected);
                                        // One undo takes every replacement back.
                                        if (replaced_count)
                                                doc_undo(&doc);
                                        te_model_set(keep, keep_length);
                                        if (!te_same(&doc) && te_failures == 0)
                                        {
                                                static p8 got[4096];
                                                positive got_length = doc_read(&doc, 0, got, sizeof(got)), first = 0;

                                                while (first < got_length && first < keep_length && got[first] == keep[first])
                                                        first++;
                                                string_format(log, "DEBUG pattern %p: replaced %p, undone to %p bytes, model %p, first difference %p, done %p groups %p\n",
                                                              p, replaced_count, got_length, keep_length, first, (positive)doc.done, (positive)doc.group_count);
                                                for (positive k = first > 8 ? first - 8 : 0; k < first + 8 && k < got_length; k++)
                                                        string_format(log, "%p:%p/%p ", k, (positive)got[k], (positive)(k < keep_length ? keep[k] : 0));
                                                string_format(log, "\n");
                                        }
                                        te_check(te_same(&doc), "replace all is one undo", p);
                                }
                        }
                        doc_find_drop(&find);
                }
                doc_close(&doc);
        }
        // Captures in a replacement, by hand: $1 $2 the groups, $& and $0 the match, $$ a dollar, \n a line end.
        {
                struct doc doc;
                struct doc_find find = {0};
                static const p8 with[] = "<$2$1|$&|$0|$$|\\n>";

                doc_open(&doc, (const p8 address_to) "ab xab", 6);
                doc_find_set(&find, (const p8 address_to) "(a)(b)", 6, DOC_FIND_CASE | DOC_FIND_REGEX);
                te_check(doc_replace_all(&doc, &find, with, sizeof(with) - 1) == 2, "two replaced with captures", 0);
                te_model_set((const p8 address_to) "<ba|ab|ab|$|\n> x<ba|ab|ab|$|\n>", 30);
                te_check(te_same(&doc), "captures in a replacement", doc_length(&doc));
                doc_find_drop(&find);
                doc_close(&doc);
        }
        // Past DOC_REPLACE_BULK matches the replace is one swap of the span: the same text, one undo, carets mapped.
        {
                struct doc doc;
                struct doc_find find = {0};
                static p8 many[30000], want[60000];
                positive length = 0, want_length = 0, count;

                for (positive i = 0; i < 6000; i++)
                {
                        many[length++] = (p8)(i % 7 ? 'a' : 'x');
                        many[length++] = (p8)(i % 3 ? '\n' : ' ');
                }
                for (positive i = 0; i < length; i++)
                        if (many[i] == 'a')
                                want[want_length++] = 'b', want[want_length++] = 'b';
                        else
                                want[want_length++] = many[i];
                doc_open(&doc, many, length);
                doc_select_set(&doc, length - 1, length - 1);
                doc_find_set(&find, (const p8 address_to) "a", 1, DOC_FIND_CASE);
                count = doc_replace_all(&doc, &find, (const p8 address_to) "bb", 2);
                te_model_set(want, want_length);
                te_check(count > DOC_REPLACE_BULK && te_same(&doc), "a replace of thousands of matches", count);
                te_check(doc.selections[0].active == want_length - 1, "the caret after thousands of replaces", doc.selections[0].active);
                te_check(doc_undo(&doc), "undo of thousands of replaces", 0);
                te_model_set(many, length);
                te_check(te_same(&doc) && doc.selections[0].active == length - 1, "thousands of replaces are one undo", 0);
                doc_find_drop(&find);
                doc_close(&doc);
        }
        // Without case: a plain query found whatever its case (through the engine's folding).
        {
                struct doc doc;
                struct doc_find find = {0};
                static const p8 text[] = "one Two tWO \xc3\x89t\xc3\xa9 two";
                p64 s = 0, e = 0;
                b32 found;

                doc_open(&doc, text, sizeof(text) - 1);
                doc_find_set(&find, (const p8 address_to) "TWO", 3, 0);
                found = doc_find_next(&doc, &find, 0, false, false, &s, &e);
                te_check(found && s == 4 && e == 7, "find without case", found * 1000 + s);
                found = doc_find_next(&doc, &find, e, false, false, &s, &e);
                te_check(found && s == 8, "find without case, again", found * 1000 + s);
                te_check(doc_find_set(&find, (const p8 address_to) "\xc3\xa9t\xc3\xa9", 5, 0), "a query beyond ASCII compiles", find.failed);
                found = doc_find_next(&doc, &find, 0, false, false, &s, &e);
                te_check(found && s == 12 && e == 17, "find without case beyond ASCII", found * 1000 + s);
                doc_find_set(&find, (const p8 address_to) "two", 3, DOC_FIND_CASE | DOC_FIND_WORD);
                found = doc_find_next(&doc, &find, 0, false, false, &s, &e);
                te_check(found && s == 18, "find a whole word", found * 1000 + s);
                found = doc_find_next(&doc, &find, 18, true, true, &s, &e);
                te_check(found && s == 18, "find backward, wrapping", found * 1000 + s);
                doc_find_set(&find, (const p8 address_to) "wo", 2, DOC_FIND_CASE | DOC_FIND_WORD);
                te_check(!doc_find_next(&doc, &find, 0, false, true, &s, &e), "part of a word is no whole word", s);
                doc_find_drop(&find);
                doc_close(&doc);
        }
}

/* ---- bytes as they were ---- */

static fn te_utf8(void)
{
        static const struct
        {
                const char address_to bytes;
                p32 length, code;
        } forms[] = {
            {"\xc0\x80", 1, DOC_INVALID | 0xc0},     {"\xc1\xbf", 1, DOC_INVALID | 0xc1},     {"\xe0\x80\xaf", 1, DOC_INVALID | 0xe0},
            {"\xe0\x9f\xbf", 1, DOC_INVALID | 0xe0}, {"\xed\xa0\x80", 1, DOC_INVALID | 0xed}, {"\xf0\x8f\xbf\xbf", 1, DOC_INVALID | 0xf0},
            {"\xf4\x90\x80\x80", 1, DOC_INVALID | 0xf4}, {"\xf5\x80\x80\x80", 1, DOC_INVALID | 0xf5}, {"\x80", 1, DOC_INVALID | 0x80},
            {"\xe2\x82", 1, DOC_INVALID | 0xe2},     {"\xc2\x80", 2, 0x80},                {"\xe0\xa0\x80", 3, 0x800},
            {"\xed\x9f\xbf", 3, 0xd7ff},            {"\xee\x80\x80", 3, 0xe000},           {"\xf0\x90\x80\x80", 4, 0x10000},
            {"\xf4\x8f\xbf\xbf", 4, 0x10ffff},
        };

        for (p32 i = 0; i < sizeof(forms) / sizeof(forms[0]); i++)
        {
                p32 code, length = doc_decode((const p8 address_to)forms[i].bytes, string_length((string_address)forms[i].bytes), &code);

                te_check(length == forms[i].length && code == forms[i].code, "a UTF-8 form decodes as the standard says", i);
        }
}

static fn te_bytes(void)
{
        static const p8 strange[] = "ok\xff\xfe \xc0\x80 \xed\xa0\x80 \xf4\x90\x80\x80 \xe2\x82 end\r\n\xef\xbb\xbf mid\r";
        struct doc doc;
        positive steps = 0;

        doc_open(&doc, strange, sizeof(strange) - 1);
        te_model_set(strange, sizeof(strange) - 1);
        // Every byte that is not UTF-8 is a cluster of its own, and stepping over the text and back lands where it started.
        {
                p64 at = 0, clusters = 0;

                while (at < doc_length(&doc))
                {
                        p64 next = doc_cluster_end(&doc, at);

                        te_check(doc_cluster_start(&doc, next) == at, "a step back lands where a step forward began", at);
                        at = next, clusters++;
                }
                te_check(clusters > 20, "bytes that are not UTF-8 are clusters", clusters);
        }
        // Edits at the start leave the rest exactly as it was.
        for (; steps < 50; steps++)
        {
                p8 text[16];
                positive size = te_text(text, sizeof(text)), removed = te_next(2);

                doc_replace(&doc, 0, removed, text, size);
                te_model_replace(0, removed, text, size);
        }
        te_check(te_same(&doc), "bytes that are not UTF-8 kept", steps);
        doc_close(&doc);

        // CR LF: the file's line end is what Enter types, a pair is one line end even across two pieces.
        {
                static const p8 crlf[] = "one\r\ntwo\r\n";

                doc_open(&doc, crlf, sizeof(crlf) - 1);
                te_check(doc.eol == DOC_EOL_CRLF, "a CR LF file", doc.eol);
                te_check(doc_line_count(&doc) == 3, "CR LF lines", doc_line_count(&doc));
                doc_select_set(&doc, 3, 3);
                doc_newline(&doc);
                te_model_set((const p8 address_to) "one\r\n\r\ntwo\r\n", 12);
                te_check(te_same(&doc), "Enter types CR LF", 0);
                doc_close(&doc);
                doc_open(&doc, (const p8 address_to) "a\rb\rc", 5);
                te_check(doc.eol == DOC_EOL_CR && doc_line_count(&doc) == 3, "a CR file", doc_line_count(&doc));
                doc_replace(&doc, 2, 0, (const p8 address_to) "\n", 1);
                te_check(doc_line_count(&doc) == 3 && doc_line_start(&doc, 1) == 3, "a LF typed after a CR makes one line end", doc_line_start(&doc, 1));
                doc_close(&doc);
        }
        // A byte order mark is held aside.
        doc_open(&doc, (const p8 address_to) "\xef\xbb\xbfhi", 5);
        te_check(doc.bom && doc_length(&doc) == 2, "a byte order mark is held aside", doc_length(&doc));
        doc_close(&doc);
}

/* ---- a gigabyte, sparse ---- */

static fn te_sparse(void)
{
        static const char path[] = "text-engine-sparse.tmp";
        bipolar handle = (bipolar)system_open_at_mode(AT_FDCWD, (string_address)path, FILE_READ_WRITE | FILE_CREATE | O_TRUNC | O_CLOEXEC, 0600);
        p64 size = 1ull << 30;
        positive started, opened;
        long mapped;
        struct doc doc;

        if (handle < 0)
        {
                string_format(log, "sparse gigabyte: NOT RUN, no file here\n");
                return;
        }
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        if (system_call_2(syscall(ftruncate), (positive)handle, size) < 0)
        {
                system_close(handle);
                string_format(log, "sparse gigabyte: NOT RUN, no room\n");
                return;
        }
        system_call_4(syscall(pwrite64), (positive)handle, (positive)"end\n", 4, size - 4);
        mapped = (long)system_call_6(syscall(mmap), 0, size, 1, 2, (positive)handle, 0);
        system_close(handle);
        if (mapped < 0 && mapped > -4096)
        {
                string_format(log, "sparse gigabyte: NOT RUN, no mapping\n");
                return;
        }
        started = clock_monotonic_nanoseconds();
        te_check(doc_open(&doc, (const p8 address_to)mapped, size), "a sparse gigabyte opens", 0);
        opened = clock_monotonic_nanoseconds();
        te_check(doc_length(&doc) == size && doc_line_count(&doc) == 2, "its length and lines", doc_line_count(&doc));
        te_check(doc_byte(&doc, size - 4) == 'e' && doc_line_start(&doc, 1) == size, "its end", doc_line_start(&doc, 1));
        doc_select_set(&doc, size / 2, size / 2);
        doc_type(&doc, (const p8 address_to) "x", 1);
        te_check(doc_length(&doc) == size + 1 && doc_byte(&doc, size / 2) == 'x', "typing in its middle", 0);
        string_format(log, "sparse gigabyte opened in %p ms\n", (opened - started) / 1000000);
        doc_close(&doc);
        system_call_2(syscall(munmap), (positive)mapped, size);
}

/* ---- the keymap ---- */

static fn te_keymap(void)
{
        static struct doc_keymap map;

        doc_keymap_defaults(&map);
        te_check(map.count == sizeof(doc_default_keys) / sizeof(doc_default_keys[0]), "every default binding parses", map.count);
        te_check(doc_keymap_find(&map, 44, 26, DOC_MOD_CONTROL) == DOC_CMD_UNDO, "ctrl+z", 0);
        te_check(doc_keymap_find(&map, 44, 26, DOC_MOD_CONTROL | DOC_MOD_SHIFT) == DOC_CMD_REDO, "ctrl+shift+z", 0);
        // German QWERTZ: the key Linux calls Y types z, so ctrl+z follows the layout.
        te_check(doc_keymap_find(&map, 21, 26, DOC_MOD_CONTROL) == DOC_CMD_UNDO, "ctrl+z on another layout", 0);
        te_check(doc_keymap_find(&map, 105, 0, 0) == DOC_CMD_LEFT, "left", 0);
        te_check(doc_keymap_find(&map, 105, 0, DOC_MOD_SHIFT | DOC_MOD_CONTROL) == DOC_CMD_SELECT_WORD_LEFT, "ctrl+shift+left", 0);
        te_check(doc_keymap_find(&map, 14, 8, DOC_MOD_CONTROL) == DOC_CMD_DELETE_WORD_LEFT, "ctrl+backspace is not ctrl+h", 0);
        te_check(doc_keymap_find(&map, 8, '/', DOC_MOD_CONTROL | DOC_MOD_SHIFT) == DOC_CMD_COMMENT, "ctrl+/ typed with shift", 0);
        te_check(doc_keymap_find(&map, 26, '{', DOC_MOD_CONTROL | DOC_MOD_SHIFT) == DOC_CMD_FOLD, "ctrl+{", 0);
        te_check(doc_keymap_find(&map, 30, 'a', 0) == DOC_CMD_NONE, "a letter alone is typing", 0);
        {
                static const char text[] = "ctrl+t none\nctrl+shift+u upper\nnot a binding\n# a remark\n";

                te_check(doc_keymap_read(&map, text, sizeof(text) - 1) == 1, "a keymap file's bad line is counted", 0);
                te_check(doc_keymap_find(&map, 20, 20, DOC_MOD_CONTROL) == DOC_CMD_NONE, "none takes a binding away", 0);
                te_check(doc_keymap_find(&map, 22, 21, DOC_MOD_CONTROL | DOC_MOD_SHIFT) == DOC_CMD_UPPER, "a binding added", 0);
        }
}

/* ---- tokens: the states an edit leaves are those a tokenizing from the start finds ---- */

static fn te_tokens(positive rounds)
{
        static struct doc_token one[512], two[512];

        for (positive round = 0; round < rounds; round++)
        {
                struct doc doc, fresh;
                static const char address_to const pieces[] = {"int x;", "/*", "*/", "\"s\\", "\"", "\n", " ", "// c", "{", "}", "a"};
                p8 start[400];
                positive length = 0;

                for (positive i = 0, n = 30 + te_next(100); i < n; i++)
                {
                        const char address_to piece = pieces[te_next(sizeof(pieces) / sizeof(pieces[0]))];
                        positive size = string_length((string_address)piece);

                        if (length + size >= sizeof(start))
                                break;
                        memory_copy(start + length, piece, size);
                        length += size;
                }
                doc_open(&doc, start, length);
                doc.language = DOC_LANGUAGE_C;
                for (positive step = 0; step < 20; step++)
                {
                        p8 text[8];
                        positive size = 0, at = te_next((p32)doc_length(&doc) + 1), removed = te_next(3);
                        const char address_to piece = pieces[te_next(sizeof(pieces) / sizeof(pieces[0]))];
                        p64 lines;

                        // The view looks at some lines between edits.
                        for (p32 look = 0; look < 3; look++)
                                doc_tokens(&doc, te_next((p32)doc_line_count(&doc)), one, 512);
                        size = string_length((string_address)piece);
                        memory_copy(text, piece, size);
                        removed = removed > doc_length(&doc) - at ? doc_length(&doc) - at : removed;
                        doc_replace(&doc, at, removed, text, size);
                        lines = doc_line_count(&doc);
                        // A fresh document of the same text tokenizes from nothing.
                        {
                                static p8 copy[1024];
                                positive copy_length = doc_read(&doc, 0, copy, sizeof(copy));
                                b32 same = true;

                                doc_open(&fresh, copy, copy_length);
                                fresh.language = DOC_LANGUAGE_C;
                                for (p64 line = 0; line < lines; line++)
                                {
                                        positive a = doc_tokens(&doc, line, one, 512), b = doc_tokens(&fresh, line, two, 512);

                                        same &= a == b && !memory_compare(one, two, a * sizeof(one[0]));
                                }
                                te_check(same, "tokens after an edit are tokens from the start", step);
                                doc_close(&fresh);
                        }
                }
                doc_close(&doc);
        }
}

/* ---- the clipboard service and the paste filter ---- */

/*
        The compositor's clipboard (compositor_clip_*) driven through a table in memory, the rows this process: a
        sealed memfd offered after a gesture is taken; one offered without, unsealed, with a NUL, not UTF-8 or past the
        limit is declined; a paste is READY only for the row with the keys within the gesture's time and REFUSED for
        every other; what a READY row reads is what was copied. Then a random stream of offers, asks, focus changes,
        gestures, rows leaving and the service turned off, with those rules held at every step. And the terminal's
        paste filter against a model of it: no ESC, no C1, no other control but a tab, a line end a CR, and in a
        bracketed paste one end marker, the last bytes.
*/
static struct windows *te_clip_table_scratch; // the table, mapped at its first use (windows_scratch_table)

// The next clipboard event of a row, or 0: its what.
static int te_clip_event(unsigned int row, unsigned int address_to which)
{
        struct window_row own = {windows_scratch_table(&te_clip_table_scratch), row, -1, -1, 0, 0, 0, 0, 0};
        struct window_event event;
        int found = 0;

        while (windows_event(&own, &event))
                if (event.type == WINDOW_EVENT_CLIP)
                        found = event.b, *which = (unsigned int)event.a;
        return found;
}

// Whether a row had a gesture the compositor still counts.
static b32 te_clip_recent(unsigned int row)
{
        return compositor_clip.gesture[row] && clock_monotonic_nanoseconds() - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS;
}

static fn te_clip_offer(unsigned int row, unsigned int which, long fd)
{
        windows_scratch_table(&te_clip_table_scratch)->clip_offer_fd[row] = (int)fd;
        windows_scratch_table(&te_clip_table_scratch)->clip_offer_which[row] = which;
        windows_scratch_table(&te_clip_table_scratch)->clip_offer_serial[row]++;
        compositor_clip_door((int)row);
}

static fn te_clip_ask(unsigned int row, unsigned int which)
{
        windows_scratch_table(&te_clip_table_scratch)->clip_ask_which[row] = which;
        windows_scratch_table(&te_clip_table_scratch)->clip_ask_serial[row]++;
        compositor_clip_door((int)row);
}

// A memfd with bytes in it, sealed or not.
static long te_clip_memfd(const p8 address_to bytes, positive length, b32 sealed)
{
        long fd;

        if (sealed)
                return windows_clip_memfd(bytes, length);
        fd = window_call(WINDOW_SYS_MEMFD, (long)"unsealed", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0);
        window_call(WINDOW_SYS_WRITE, fd, (long)bytes, (long)length, 0, 0, 0);
        return fd;
}

// The paste filter's model: what term_paste_filter must make of text.
static positive te_paste_model(const p8 address_to text, positive length, p8 address_to into)
{
        positive out = 0;

        for (positive at = 0; at < length; at++)
        {
                if (text[at] == '\r' && at + 1 < length && text[at + 1] == '\n')
                        continue;
                if (text[at] == '\r' || text[at] == '\n')
                        into[out++] = '\r';
                else if (text[at] == 0xc2 && at + 1 < length && text[at + 1] >= 0x80 && text[at + 1] < 0xa0)
                        at++;
                else if (text[at] >= ' ' && text[at] != 0x7f)
                        into[out++] = text[at];
                else if (text[at] == '\t')
                        into[out++] = '\t';
        }
        return out;
}

// A copy made of raw bytes and an index of the records given (each an offset, a length and a name), sealed: to try what is refused.
static long te_clip_raw(const p8 address_to bytes, positive length, const struct windows_clip_type address_to types, positive count, positive count_said)
{
        long fd = window_call(WINDOW_SYS_MEMFD, (long)"raw", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0);
        long index = window_call(WINDOW_SYS_MEMFD, (long)"raw-index", WINDOWS_MFD_CLOEXEC, 0, 0, 0, 0);

        windows_clip_put(fd, bytes, length);
        for (positive at = 0; at < count; at++)
                windows_clip_record(index, types[at].offset, types[at].length, types[at].name, types[at].name_length);
        windows_clip_seal(fd, index, length, count_said);
        window_call(WINDOW_SYS_CLOSE, index, 0, 0, 0, 0, 0);
        return fd;
}

/*
        A copy of any types, the format every kind of client's copy is held in: a program's of text, a picture and a list of files taken
        and read back a type at a time, each name text goes by read as the text, the names it is offered under (the Wayland ones, X's
        too), one far past the 16 MiB a copy once was at most taken whole, and copies that do not hold together declined.
*/
static fn te_clip_types(void)
{
        static const p8 png[] = "\x89PNG\r\n\x1a\n-not-text-\0-", uris[] = "file:///root/a.txt\r\nfile:///root/b.png\r\n", text[] = "two files";
        struct windows_clip_part parts[3] = {{WINDOWS_CLIP_TEXT, text, sizeof(text) - 1}, {WINDOWS_CLIP_PNG, png, sizeof(png) - 1},
                                             {WINDOWS_CLIP_URIS, uris, sizeof(uris) - 1}};
        struct window_row own = {windows_scratch_table(&te_clip_table_scratch), 1, -1, -1, 0, 0, 0, 0, 0};
        unsigned int which;
        p8 address_to bytes;
        unsigned long length;
        const char address_to names[8];
        positive offered = 0;

        compositor_clip_gesture(0);
        te_clip_offer(0, WINDOWS_CLIP_CLIPBOARD, windows_clip_build(parts, 3));
        te_check(te_clip_event(0, &which) == WINDOWS_CLIP_TAKEN, "a copy of text, a picture and files is taken", 0);
        compositor_clip_gesture(1);
        te_clip_ask(1, WINDOWS_CLIP_CLIPBOARD);
        te_check(te_clip_event(1, &which) == WINDOWS_CLIP_READY, "and given to the window with the keys", 0);
        te_check(windows_clip_read_type(&own, WINDOWS_CLIP_CLIPBOARD, WINDOWS_CLIP_PNG, &bytes, &length) == sizeof(png) - 1 &&
                     !memory_compare(bytes, png, length),
                 "its picture is read back whole, NUL and all", length);
        windows_clip_free(bytes, length);
        te_check(windows_clip_read_type(&own, WINDOWS_CLIP_CLIPBOARD, WINDOWS_CLIP_URIS, &bytes, &length) == sizeof(uris) - 1 &&
                     !memory_compare(bytes, uris, length),
                 "its files as text/uri-list", length);
        windows_clip_free(bytes, length);
        te_check(windows_clip_read_type(&own, WINDOWS_CLIP_CLIPBOARD, "UTF8_STRING", &bytes, &length) == sizeof(text) - 1 &&
                     !memory_compare(bytes, text, length),
                 "a name text goes by is its text", length);
        windows_clip_free(bytes, length);
        te_check(windows_clip_read_type(&own, WINDOWS_CLIP_CLIPBOARD, "image/jpeg", &bytes, &length) < 0 && !bytes, "a type it has not is nothing", 0);
        for (positive nth = 0; nth < 8; nth++)
        {
                unsigned long n;

                names[nth] = windows_clip_offered(compositor_clip.map[0], compositor.table->clip_length[0], nth, 1, &n);
                offered += names[nth] != null;
        }
        te_check(offered == 7 && string_equals((string_address)names[1], WINDOWS_CLIP_PNG) && string_equals((string_address)names[3], "UTF8_STRING") &&
                     string_equals((string_address)names[6], "STRING"),
                 "it is offered as its three types, then UTF8_STRING, text/plain, TEXT and STRING to X", offered);
        offered = 0;
        for (positive nth = 0; nth < 8; nth++)
        {
                unsigned long n;

                offered += windows_clip_offered(compositor_clip.map[0], compositor.table->clip_length[0], nth, 0, &n) != null;
        }
        te_check(offered == 5, "and as its three types with UTF8_STRING and text/plain to Wayland", offered);

        // Far past 16 MiB: taken whole, its last byte read back.
        {
                positive size = 20u << 20;
                long mapped = window_call(WINDOW_SYS_MMAP, 0, (long)size, 3, 0x22, -1, 0);
                struct windows_clip_part big = {"application/octet-stream", (const void address_to)mapped, size};

                ((p8 address_to)mapped)[size - 1] = 0x5a;
                compositor_clip_gesture(0);
                te_clip_offer(0, WINDOWS_CLIP_CLIPBOARD, windows_clip_build(&big, 1));
                window_call(WINDOW_SYS_MUNMAP, mapped, (long)size, 0, 0, 0, 0);
                te_check(te_clip_event(0, &which) == WINDOWS_CLIP_TAKEN && compositor.table->clip_length[0] > size, "a copy of 20 MiB is taken", 0);
                compositor_clip_gesture(1);
                te_clip_ask(1, WINDOWS_CLIP_CLIPBOARD);
                te_check(te_clip_event(1, &which) == WINDOWS_CLIP_READY &&
                             windows_clip_read_type(&own, WINDOWS_CLIP_CLIPBOARD, "application/octet-stream", &bytes, &length) == (long)size &&
                             bytes[size - 1] == 0x5a,
                         "and read back whole", length);
                windows_clip_free(bytes, length);
        }

        // Copies that do not hold together.
        {
                static const p8 raw[] = "abcdefgh", bad_text[] = "a\xff";
                struct windows_clip_type past = {4, 8, "x/y", 3}, fine = {0, 8, "x/y", 3}, blank = {0, 8, "x/\x01", 3}, texted = {0, 2, WINDOWS_CLIP_TEXT, 24};
                struct
                {
                        const p8 address_to bytes;
                        positive length;
                        const struct windows_clip_type address_to type;
                        positive count, said;
                        const char address_to what;
                } cases[] = {
                    {raw, 8, &past, 1, 1, "a type past the copy's bytes is declined"},
                    {raw, 8, &fine, 1, 2, "an index whose count is not its records is declined"},
                    {raw, 8, &fine, 0, 0, "a copy of no type is declined"},
                    {raw, 8, &blank, 1, 1, "a name with a control in it is declined"},
                    {bad_text, 2, &texted, 1, 1, "text that is no UTF-8 is declined"},
                };

                for (p32 c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
                {
                        unsigned int before = compositor.table->clip_serial[0];
                        long fd = te_clip_raw(cases[c].bytes, cases[c].length, cases[c].type, cases[c].count, cases[c].said);

                        compositor_clip_gesture(3);
                        te_clip_offer(3, WINDOWS_CLIP_CLIPBOARD, fd);
                        te_check(te_clip_event(3, &which) == WINDOWS_CLIP_DECLINED && compositor.table->clip_serial[0] == before, cases[c].what, c);
                        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                }
                {
                        long fd = te_clip_raw(raw, 8, &fine, 1, 1);

                        compositor_clip_gesture(3);
                        te_clip_offer(3, WINDOWS_CLIP_CLIPBOARD, fd);
                        te_check(te_clip_event(3, &which) == WINDOWS_CLIP_TAKEN, "and one made by hand that does is taken", 0);
                        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                }
        }
        // The copy of the first rows again, for what follows.
        compositor_clip_gesture(0);
        te_clip_offer(0, WINDOWS_CLIP_CLIPBOARD, windows_clip_memfd("hello, clipboard \xe2\x9c\x93", sizeof("hello, clipboard \xe2\x9c\x93") - 1));
        te_clip_event(0, &which);
}

static fn te_clipboard(positive rounds)
{
        unsigned int me = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0), which;
        static const p8 hello[] = "hello, clipboard \xe2\x9c\x93";
        p8 address_to bytes;
        unsigned long length;
        long fd;

        compositor.table = windows_scratch_table(&te_clip_table_scratch);
        windows_scratch_table(&te_clip_table_scratch)->clip_pid = me;
        windows_scratch_table(&te_clip_table_scratch)->clip_on = 1;
        windows_scratch_table(&te_clip_table_scratch)->clip_owner[0] = windows_scratch_table(&te_clip_table_scratch)->clip_owner[1] = -1;
        for (unsigned int row = 0; row < 4; row++)
                windows_scratch_table(&te_clip_table_scratch)->pid[row] = me;
        compositor.focus = 1;

        // Taken after a gesture, read back by the row with the keys.
        compositor_clip_gesture(0);
        te_clip_offer(0, WINDOWS_CLIP_CLIPBOARD, windows_clip_memfd(hello, sizeof(hello) - 1));
        te_check(te_clip_event(0, &which) == WINDOWS_CLIP_TAKEN && windows_scratch_table(&te_clip_table_scratch)->clip_length[0] > sizeof(hello) - 1 &&
                     windows_scratch_table(&te_clip_table_scratch)->clip_owner[0] == 0,
                 "a copy after a gesture is taken", windows_scratch_table(&te_clip_table_scratch)->clip_length[0]);
        compositor_clip_gesture(1);
        te_clip_ask(1, WINDOWS_CLIP_CLIPBOARD);
        te_check(te_clip_event(1, &which) == WINDOWS_CLIP_READY, "a paste by the window with the keys", 0);
        {
                struct window_row own = {windows_scratch_table(&te_clip_table_scratch), 1, -1, -1, 0, 0, 0, 0, 0};

                te_check(windows_clip_read(&own, WINDOWS_CLIP_CLIPBOARD, &bytes, &length) == sizeof(hello) - 1 &&
                             !memory_compare(bytes, hello, length),
                         "the paste is what was copied", length);
                windows_clip_free(bytes, length);
        }
        // Refused: a window without the keys; with them but nobody at them.
        compositor_clip_gesture(2);
        te_clip_ask(2, WINDOWS_CLIP_CLIPBOARD);
        te_check(te_clip_event(2, &which) == WINDOWS_CLIP_REFUSED, "a window without the keys is refused", 0);
        // One that says in the table it is a popup of the window with the keys, which the compositor never took it for, too.
        windows_scratch_table(&te_clip_table_scratch)->popup_parent[2] = 1 + 1;
        compositor_clip_gesture(2);
        te_clip_ask(2, WINDOWS_CLIP_CLIPBOARD);
        te_check(te_clip_event(2, &which) == WINDOWS_CLIP_REFUSED,
                 "a window that writes the focused window as its popup parent, and is none, is refused", 0);
        windows_scratch_table(&te_clip_table_scratch)->popup_parent[2] = 0;
        compositor_clip.gesture[1] = 0;
        te_clip_ask(1, WINDOWS_CLIP_CLIPBOARD);
        te_check(te_clip_event(1, &which) == WINDOWS_CLIP_REFUSED, "a paste with nobody at the keys is refused", 0);
        // Declined copies.
        {
                static const p8 nul[] = "a\0b", bad[] = "a\xff", overlong[] = "a\xc0\xaf";
                struct
                {
                        const p8 address_to bytes;
                        positive length;
                        b32 sealed, gesture;
                        const char address_to what;
                } cases[] = {
                    {hello, 5, false, true, "an unsealed memfd is declined"},  {nul, 3, true, true, "a NUL is declined"},
                    {bad, 2, true, true, "a byte that is not UTF-8 is declined"}, {overlong, 3, true, true, "an overlong form is declined"},
                    {hello, 5, true, false, "a copy with no gesture is declined"},
                };

                for (p32 c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
                {
                        unsigned int before = windows_scratch_table(&te_clip_table_scratch)->clip_serial[0];

                        compositor_clip.gesture[3] = 0;
                        if (cases[c].gesture)
                                compositor_clip_gesture(3);
                        fd = te_clip_memfd(cases[c].bytes, cases[c].length, cases[c].sealed);
                        te_clip_offer(3, WINDOWS_CLIP_CLIPBOARD, fd);
                        te_check(te_clip_event(3, &which) == WINDOWS_CLIP_DECLINED && windows_scratch_table(&te_clip_table_scratch)->clip_serial[0] == before,
                                 cases[c].what, c);
                        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                }
        }
        te_clip_types();
        /*
                A copy that names a descriptor which is no file to read: a FIFO its program holds open for reading with no
                writer, which the compositor's open for reading (through /proc) waits on for ever, and the desktop with it. (An
                anonymous pipe's end is opened again at once: the kernel waits for a writer only for a FIFO with a name.)
                Offered in a child the check waits two seconds for: declined at once, and the compositor not held.
        */
        {
                static const char fifo[] = "text-engine-clip-fifo.tmp";
                int ends[2] = {-1, -1};
                positive status = 0;
                bipolar child = -1, waited = 0;

                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)fifo, 0);
                if (system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)fifo, 0010600, 0) == 0)
                        ends[0] = (int)system_open_at(AT_FDCWD, (string_address)fifo, FILE_READ | O_NONBLOCK | O_CLOEXEC);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)fifo, 0);
                if (ends[0] >= 0)
                {
                        child = (bipolar)system_call_5(syscall(clone), 17, 0, 0, 0, 0);
                        if (child == 0)
                        {
                                compositor_clip.gesture[3] = 0;
                                compositor_clip_gesture(3);
                                te_clip_offer(3, WINDOWS_CLIP_CLIPBOARD, ends[0]);
                                system_call_1(syscall(exit_group), te_clip_event(3, &which) == WINDOWS_CLIP_DECLINED ? 0 : 3);
                        }
                        for (int tries = 0; child > 0 && tries < 200 && waited != child; tries++)
                        {
                                timespec nap = {0, 10000000};

                                waited = (bipolar)system_call_4(syscall(wait4), (positive)child, (positive)&status, 1, 0);
                                if (waited != child)
                                        system_call_2(syscall(nanosleep), (positive)&nap, 0);
                        }
                        if (child > 0 && waited != child)
                        {
                                system_call_2(syscall(kill), (positive)child, 9);
                                system_call_4(syscall(wait4), (positive)child, (positive)&status, 0, 0);
                        }
                        system_close(ends[0]);
                }
                te_check(child > 0 && waited == child && (status & 0xffff) == 0,
                         "a FIFO with no writer offered as a copy is declined at once, and the compositor is not held", status);
        }
        // A window gone leaves its copy, owned by nobody; off empties everything and refuses.
        compositor_clip_leave(0);
        te_check(windows_scratch_table(&te_clip_table_scratch)->clip_owner[0] == -1 && windows_scratch_table(&te_clip_table_scratch)->clip_length[0] > sizeof(hello) - 1, "a copy outlives its window", 0);
        windows_scratch_table(&te_clip_table_scratch)->clip_on = 0;
        compositor_clip_wipe();
        compositor_clip_gesture(1);
        te_clip_ask(1, WINDOWS_CLIP_CLIPBOARD);
        te_check(te_clip_event(1, &which) == WINDOWS_CLIP_REFUSED && !windows_scratch_table(&te_clip_table_scratch)->clip_length[0], "off empties and refuses", 0);
        windows_scratch_table(&te_clip_table_scratch)->clip_on = 1;

        // A random stream with the rules held at every step.
        {
                p8 last[2][64];
                positive last_length[2] = {0, 0};
                b32 has[2] = {false, false};

                for (positive step = 0; step < rounds; step++)
                {
                        unsigned int row = te_next(4), kind = te_next(7), pick = te_next(2);
                        int what;

                        if (kind == 0)
                                compositor_clip_gesture((int)row);
                        else if (kind == 1)
                                compositor.focus = (int)te_next(4);
                        else if (kind == 2)
                        {
                                p8 text[64];
                                positive size = 1 + te_next(40);
                                b32 good = te_next(4) != 0, recent = te_clip_recent(row);

                                for (positive i = 0; i < size; i++)
                                        text[i] = (p8)('a' + te_next(26));
                                if (!good)
                                        text[te_next((p32)size)] = te_next(2) ? 0 : 0xff;
                                fd = windows_clip_memfd(text, size);
                                te_clip_offer(row, pick, fd);
                                what = te_clip_event(row, &which);
                                te_check(what == (good && recent ? WINDOWS_CLIP_TAKEN : WINDOWS_CLIP_DECLINED), "a copy in the stream", step);
                                if (what == WINDOWS_CLIP_TAKEN)
                                {
                                        memory_copy(last[pick], text, size);
                                        last_length[pick] = size;
                                        has[pick] = true;
                                }
                                window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                        }
                        else if (kind == 3 || kind == 4)
                        {
                                b32 may = has[pick] && (int)row == compositor.focus && te_clip_recent(row);

                                te_clip_ask(row, pick);
                                what = te_clip_event(row, &which);
                                te_check(what == (may ? WINDOWS_CLIP_READY : WINDOWS_CLIP_REFUSED), "a paste in the stream", step * 10 + (positive)may);
                                if (what == WINDOWS_CLIP_READY)
                                {
                                        struct window_row own = {windows_scratch_table(&te_clip_table_scratch), row, -1, -1, 0, 0, 0, 0, 0};

                                        te_check(windows_clip_read(&own, pick, &bytes, &length) == (long)last_length[pick] &&
                                                     !memory_compare(bytes, last[pick], length),
                                                 "a paste in the stream is the last copy", step);
                                        windows_clip_free(bytes, length);
                                }
                        }
                        else if (kind == 5)
                                compositor_clip_leave((int)row);
                        else if (te_next(16) == 0)
                        {
                                compositor_clip_wipe();
                                has[0] = has[1] = false;
                        }
                }
        }

        // The paste filter against its model, and what a bracketed paste sends.
        for (positive round = 0; round < rounds; round++)
        {
                static const p8 alphabet[] = {'a', ' ', '\t', '\n', '\r', 0x1b, '[', '2', '0', '1', '~', 0x7f, 0, 7, 0xc2, 0x9b, 0x85, 0xa9, 0xe2, 0x80};
                p8 text[64], got[64], want[64];
                positive size = te_next(64), got_length, want_length;

                for (positive i = 0; i < size; i++)
                        text[i] = alphabet[te_next(sizeof(alphabet))];
                got_length = term_paste_filter(text, size, got);
                want_length = te_paste_model(text, size, want);
                te_check(got_length == want_length && !memory_compare(got, want, got_length), "the paste filter", round);
                te_check(!memory_first_of(got, 0x1b, got_length), "no ESC is pasted", round);
        }
        {
                static const p8 attack[] = "safe\x1b[201~rm -rf ~\n\xc2\x9b" "31m";
                static const p8 sent[] = "\x1b[200~safe[201~rm -rf ~\r31m\x1b[201~";

                to_shell_length = 0;
                line_editing = false;
                bracketed_paste = true;
                term_paste(attack, sizeof(attack) - 1);
                te_check(to_shell_length == sizeof(sent) - 1 && !memory_compare(to_shell, sent, to_shell_length),
                         "a bracketed paste cannot end its bracket early", to_shell_length);
                to_shell_length = 0;
                bracketed_paste = false;
                term_paste(attack, sizeof(attack) - 1);
                te_check(to_shell_length == sizeof(sent) - 1 - 12 && !memory_first_of(to_shell, 0x1b, to_shell_length),
                         "a plain paste carries no escape", to_shell_length);
                to_shell_length = 0;
        }
}


/*
        Where the compositor puts a popup (compositor_popup_place): below and right of a point, flipped left and up
        where the screen ends; a rectangle's menu below it, a submenu right of it and flipped left; always inside the
        screen, a second screen's own edges included, and a menu taller than the screen at its top.
*/
static fn te_popups(positive rounds)
{
        static const int screens[2][4] = {{0, 0, 1920, 1080}, {1920, -200, 1280, 1024}};
        int x, y;

        compositor_popup_place(100, 100, 0, 0, 200, 300, WINDOW_POPUP_POINT, screens[0], &x, &y);
        te_check(x == 100 && y == 100, "a context menu opens below and right of the point", (positive)x);
        compositor_popup_place(1850, 100, 0, 0, 200, 300, WINDOW_POPUP_POINT, screens[0], &x, &y);
        te_check(x == 1650 && y == 100, "past the right edge it opens to the left of the point", (positive)x);
        compositor_popup_place(100, 1000, 0, 0, 200, 300, WINDOW_POPUP_POINT, screens[0], &x, &y);
        te_check(x == 100 && y == 700, "past the bottom it opens above the point", (positive)y);
        compositor_popup_place(400, 100, 80, 24, 200, 300, 0, screens[0], &x, &y);
        te_check(x == 400 && y == 124, "a menu drops below its rectangle", (positive)y);
        compositor_popup_place(400, 900, 80, 24, 200, 300, 0, screens[0], &x, &y);
        te_check(y == 600, "a menu with no room below opens above its rectangle", (positive)y);
        compositor_popup_place(1700, 100, 200, 24, 200, 300, WINDOW_POPUP_RIGHT, screens[0], &x, &y);
        te_check(x == 1500, "a submenu with no room on the right opens on the left", (positive)x);
        compositor_popup_place(2500, -150, 0, 0, 200, 2000, WINDOW_POPUP_POINT, screens[1], &x, &y);
        te_check(y == -200 && x == 2500, "a menu taller than its screen starts at the screen's top", (positive)y);
        compositor_popup_place(3150, 700, 0, 0, 200, 300, WINDOW_POPUP_POINT, screens[1], &x, &y);
        te_check(x == 2950 && y == 400, "on the second screen it flips at that screen's edges", (positive)x);
        for (positive round = 0; round < rounds; round++)
        {
                const int address_to s = screens[te_next(2)];
                int width = 20 + (int)te_next(600), height = 20 + (int)te_next(1400);
                int ax = s[0] + (int)te_next((p32)s[2]), ay = s[1] + (int)te_next((p32)s[3]);
                unsigned int flags = te_next(3) == 0 ? WINDOW_POPUP_POINT : te_next(2) ? WINDOW_POPUP_RIGHT : 0;

                compositor_popup_place(ax, ay, (int)te_next(100), (int)te_next(40), width, height, flags, s, &x, &y);
                te_check(x >= s[0] && (width > s[2] || x + width <= s[0] + s[2]) && y >= s[1] && (height > s[3] || y + height <= s[1] + s[3]),
                         "a popup is inside its screen", round);
        }
}

/* ---- what it costs ---- */

static fn te_bench(void)
{
        positive size = 100u << 20, at = 0, started, took;
        p8 address_to file = null;
        positive room = 0;
        struct doc doc;

        array_store_reserve(file, room, 0, size, 4096);
        // 100 MB of code-shaped lines.
        while (at < size)
        {
                static const char line[] = "        if (value > limit) return compute(value, limit) + offset; // note\n";
                positive take = sizeof(line) - 1 < size - at ? sizeof(line) - 1 : size - at;

                memory_copy(file + at, line, take);
                at += take;
        }
        started = clock_monotonic_nanoseconds();
        doc_open(&doc, file, size);
        took = clock_monotonic_nanoseconds() - started;
        string_format(log, "open 100 MB: %p us, %p lines\n", took / 1000, doc_line_count(&doc));
        {
                p64 line = doc_line_count(&doc) / 2;

                started = clock_monotonic_nanoseconds();
                for (positive i = 0; i < 100000; i++)
                        line ^= doc_line_start(&doc, (line + i * 7919) % doc_line_count(&doc)) & 1;
                took = clock_monotonic_nanoseconds() - started;
                string_format(log, "line start: %p ns a lookup\n", took / 100000);
        }
        doc_select_set(&doc, doc_line_start(&doc, doc_line_count(&doc) / 2), doc_line_start(&doc, doc_line_count(&doc) / 2));
        started = clock_monotonic_nanoseconds();
        for (positive i = 0; i < 10000; i++)
        {
                doc.now += 100000000;
                doc_type(&doc, (const p8 address_to) "x", 1);
        }
        took = clock_monotonic_nanoseconds() - started;
        string_format(log, "typing in the middle: %p ns a key\n", took / 10000);
        started = clock_monotonic_nanoseconds();
        for (positive i = 0; i < 10000; i++)
                doc_delete(&doc, false, DOC_REACH_CLUSTER);
        took = clock_monotonic_nanoseconds() - started;
        string_format(log, "backspace in the middle: %p ns a key\n", took / 10000);
        {
                struct doc_find find = {0};
                positive count;

                doc_find_set(&find, (const p8 address_to) "limit", 5, DOC_FIND_CASE);
                started = clock_monotonic_nanoseconds();
                count = doc_replace_all(&doc, &find, (const p8 address_to) "bound", 5);
                took = clock_monotonic_nanoseconds() - started;
                string_format(log, "replace all: %p matches in %p ms\n", count, took / 1000000);
                started = clock_monotonic_nanoseconds();
                doc_undo(&doc);
                took = clock_monotonic_nanoseconds() - started;
                string_format(log, "undo of it: %p ms\n", took / 1000000);
        }
        doc_close(&doc);
}

/*
        `vectors`: test vectors on standard input, one a line, G or W and then the words as the pinned ones are written
        (hex, bit 31 a boundary before): the text_tables harness feeds every vector of the official files through it.
*/
static b32 te_vectors_in(void)
{
        static p8 input[1 << 20];
        positive length = 0, failures = 0, count = 0;
        long got;

        while (length < sizeof(input) - 1 && (got = (long)system_read_once(0, input + length, sizeof(input) - 1 - length)) > 0)
                length += (positive)got;
        input[length] = 0;
        for (positive at = 0; at < length;)
        {
                p32 words[64];
                positive n = 0;
                p8 kind = input[at];

                at++;
                while (at < length && input[at] != '\n' && n < 63)
                {
                        p32 value = 0;

                        while (input[at] == ' ')
                                at++;
                        if (input[at] == '\n' || !input[at])
                                break;
                        while (byte_is_digit((b8)input[at]) || (input[at] | 32) - 'a' < 6u)
                                value = value << 4 | (p32)(byte_is_digit((b8)input[at]) ? input[at] - '0' : (input[at] | 32) - 'a' + 10), at++;
                        words[n++] = value;
                }
                words[n] = 0xffffffffu;
                at++;
                if (!n)
                        continue;
                count++;
                if (kind == 'G')
                {
                        p8 bytes[256], cuts[64];
                        p32 offsets[65];
                        positive size, points = te_vector(words, bytes, &size, offsets, cuts), point = 0;
                        p64 here = 0;
                        struct doc doc;
                        b32 same = true;

                        doc_open(&doc, bytes, size);
                        while (here < size)
                        {
                                p64 next = doc_cluster_end(&doc, here);

                                point++;
                                while (point < points && !cuts[point])
                                        point++;
                                same &= next == offsets[point];
                                here = next > here ? next : size;
                        }
                        doc_close(&doc);
                        failures += !same;
                }
                else
                {
                        p16 props[64];
                        p8 breaks[65];
                        b32 same = true;

                        for (positive i = 0; i < n; i++)
                                props[i] = doc_props(words[i] & 0x1fffff);
                        doc_word_breaks(props, n, breaks);
                        for (positive i = 1; i < n; i++)
                                same &= breaks[i] == (words[i] >> 31);
                        failures += !same;
                }
        }
        string_format(log, "vectors: %p, %p failures\n", count, failures);
        log_flush();
        return failures != 0;
}

b32 main(void)
{
        b32 bench = program_argument_count() > 1 && string_equals(program_argument(1), "bench");

        if (program_argument_count() > 1 && string_equals(program_argument(1), "vectors"))
                return te_vectors_in();

        if (bench)
        {
                te_bench();
                log_flush();
                return 0;
        }
        {
                // One section by name, or every one; each says what it found as it ends.
                string_address only = program_argument_count() > 1 ? program_argument(1) : null;
#define TE_SECTION(name, call)                                                                                \
                if (!only || string_equals(only, name))                                                       \
                {                                                                                             \
                        positive before = te_failures;                                                        \
                        call;                                                                                 \
                        string_format(log, "%s: %p failures\n", (string_address)name, te_failures - before); \
                        log_flush();                                                                          \
                }
                TE_SECTION("buffer", te_buffer(40, 300))
                TE_SECTION("history", te_history(30, 120))
                TE_SECTION("commands", te_lines(); te_commands(40))
                TE_SECTION("carets", te_carets(200))
                TE_SECTION("boundaries", te_boundaries())
                TE_SECTION("find", te_find(12))
                TE_SECTION("bytes", te_utf8(); te_bytes())
                TE_SECTION("keymap", te_keymap())
                TE_SECTION("tokens", te_tokens(30))
                TE_SECTION("sparse", te_sparse())
                TE_SECTION("clipboard", te_clipboard(3000))
                TE_SECTION("popups", te_popups(5000))
#undef TE_SECTION
        }
        string_format(log, "text engine: %p checks, %p failures\n", te_checks, te_failures);
        log_flush();
        return te_failures != 0;
}
#endif

#ifdef CHECK_canvas_cells
/* The compositor's CPU backend and the library's cell and glyph bodies, with ordinary memory instead of DRM. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
/* The library's own names the cut spells (src/lib.c): its integers, a routine
   that answers nothing, a string, an address and null. */
typedef unsigned char p8;
typedef unsigned short p16;
typedef unsigned int p32;
typedef unsigned long long p64;
typedef signed char b8;
typedef short b16;
typedef int b32;
typedef long long b64;
typedef __int128 b128;
typedef float f32;
typedef double f64;
typedef void fn;
typedef p8 *string_address;
typedef const p8 *const_string;
#define address_to *
#define address_of &
#define null ((void *)0)
/* The backend is the CPU's, so the card's part is its stubs (SHELL_NO_GPU),
   and the sound server's page is read with no socket to the server
   (CTL_NO_SYSTEM, as the audio rows' own check takes control.c). */
#define SHELL_NO_GPU
#define CTL_NO_SYSTEM
/* And the kit is not in it (ui/ui.c's windows are built after the compositor,
   and only its shared part is here), so every switch of the kit is off, as a
   build without them has it: the taskbar's and the language's answers are the
   shared part's own stubs. */
#define MOONWATER_UI_LAUNCHER 0
#define MOONWATER_UI_SETTINGS 0
#define MOONWATER_UI_IDENTIFY 0
#define MOONWATER_UI_TASKBAR 0
#define MOONWATER_UI_EDITOR 0
#define MOONWATER_UI_MIXER 0
#define MOONWATER_UI_KEYBOARD 0
#define MOONWATER_UI_NOTICES 0
#define MOONWATER_UI_FILES 0
#define HOT
#define noinline __attribute__((noinline))
#define PURE
#define CONST
#define true 1
#define false 0
#define static_assert _Static_assert
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define clamp(a,b,c) min(max(a,b),c)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define WINDOW_CELL_W 8
#define WINDOW_CELL_H 16
#define WINDOW_CELL_BOLD 1u
#define WINDOW_CELL_DIM 2u
#define WINDOW_CELL_ITALIC 4u
#define WINDOW_CELL_UNDERLINE 8u
#define WINDOW_CELL_BLINK 16u
#define WINDOW_CELL_HIDDEN 32u
#define WINDOW_CELL_STRIKE 64u
#define WINDOW_CELL_WIDE 128u
#define WINDOW_CELL_WIDE_RIGHT 256u
#define WINDOW_CELL_BAR 512u
struct window_cell {unsigned character; unsigned char ink,paper; unsigned short flags;};
void memory_fill_u32(u32 *p,unsigned long n,u32 v) {while(n--)*p++=v;}
void *memory_copy_apart(void *to,const void *from,unsigned long n) {return memcpy(to,from,n);}
void *memory_copy(void *to,const void *from,unsigned long n) {return memcpy(to,from,n);}
void *memory_fill(void *p,int v,unsigned long n) {return memset(p,v,n);}
void canvas_rect_fill(u32 *,unsigned long,unsigned long,unsigned long,u32);
void canvas_glyph(u32 *,unsigned long,const u8 *,unsigned long,unsigned long,u32);
void canvas_glyph2(u32 *,unsigned long,const u8 *,unsigned long,unsigned long,u32);
void canvas_cell(u32 *,unsigned long,const u8 *,unsigned long,u32,u32);
void canvas_cell2(u32 *,unsigned long,const u8 *,unsigned long,u32,u32);
void canvas_row_blit(u32 *,const u32 *,unsigned long,u32);
void canvas_cells(u32 *,unsigned long,const u8 *,const struct window_cell *,unsigned long,u32,u32);
void canvas_cell_wide(u32 *,unsigned long,const u8 *,unsigned long,u32,u32);
void canvas_cell2_wide(u32 *,unsigned long,const u8 *,unsigned long,u32,u32);
void canvas_row_blit_wide(u32 *,const u32 *,unsigned long,u32);
void canvas_cells_wide(u32 *,unsigned long,const u8 *,const struct window_cell *,unsigned long,u32,u32);
void canvas_rect_fill_wide(u32 *,unsigned long,unsigned long,unsigned long,u32);
void canvas_glyph_wide(u32 *,unsigned long,const u8 *,unsigned long,unsigned long,u32);
void canvas_glyph2_wide(u32 *,unsigned long,const u8 *,unsigned long,unsigned long,u32);
/* Every row is counted and the failed ones too, so a red row is a tally and not the end of the run: the rows after it are
   run, and the program answers 1 when any failed. */
static unsigned checks,failures;
static void check(int okay) {checks++;failures+=!okay;}
static u32 pixels[1024*64+2];
static u32 expected[1024*64+2];

static void golden_sha256(const u32 *pixels,size_t count,char out[65]) {
    static const uint32_t k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint64_t bytes=(uint64_t)count*4,total=(bytes+9+63)/64*64;
    #define GROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
    for(uint64_t at=0;at<total;at+=64) {
        uint8_t block[64];uint32_t w[64];
        for(unsigned i=0;i<64;i++) {
            uint64_t b=at+i;
            block[i]=b<bytes?(uint8_t)((pixels[b/4]&0xffffffu)>>(b%4*8)):
                b==bytes?0x80:b>=total-8?(uint8_t)(bytes*8>>((total-1-b)*8)):0;
        }
        for(unsigned i=0;i<16;i++)w[i]=(uint32_t)block[i*4]<<24|(uint32_t)block[i*4+1]<<16|
            (uint32_t)block[i*4+2]<<8|block[i*4+3];
        for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(GROR(w[i-15],7)^GROR(w[i-15],18)^(w[i-15]>>3))+
            w[i-7]+(GROR(w[i-2],17)^GROR(w[i-2],19)^(w[i-2]>>10));
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(unsigned i=0;i<64;i++) {
            uint32_t t1=hh+(GROR(e,6)^GROR(e,11)^GROR(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            uint32_t t2=(GROR(a,2)^GROR(a,13)^GROR(a,22))+((a&b)^(a&c)^(b&c));
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    #undef GROR
    for(unsigned i=0;i<8;i++)snprintf(out+i*8,9,"%08x",h[i]);
}
static u32 golden_screen[1024*768];
/* M1.3: src/canvas/compositor.c's CPU backend draws G-S1 its own way: the
   window is its pixels and nothing around them (no border, titlebar or close
   box), a black line of one logical
   pixel round them and corners round at 8, so its four hashes are its own,
   recorded from the backend
   that made them and pinned so a later change to the look is as
   deliberate. */
/* That black line is the scene's: the frame is given its ink, which the
   compositor no longer names itself (a window's line is the theme's now,
   compositor_chrome). */
#define COMPOSITOR_INK_LINE 0x000000u
static unsigned long compositor_written;
static void memory_fill_u32_rect(u32 *into,unsigned long stride,unsigned long width,
                                 unsigned long height,u32 value) {
    compositor_written+=width*height;
    for(unsigned long y=0;y<height;y++)for(unsigned long x=0;x<width;x++)into[y*stride+x]=value;
}
/* What the backend takes from the library: the pool it bands a scene across (here one band, in this thread), the
   pages the corner masks are mapped into, and the copy that moves a list along. */
typedef unsigned long positive;
typedef void *address_any;
#define PARALLEL_MINIMUM_BYTES ((positive)1 << 62)
static positive parallel_width(void) { return 1; }
static void parallel_for(void (*body)(address_any, positive), address_any context, positive count, positive bytes)
{
    (void)bytes;
    for(positive at=0;at<count;at++)body(context,at);
}
static void *compositor_map_zeroed(unsigned long bytes) { return calloc(1,bytes); }
#ifndef memory_copy
#define memory_copy memmove
#endif
/* The library's pixel routines the backend calls, each written out as its
   contract at its declaration in src/lib.c says (CHECK_verify holds lib.c's
   bodies to the same rules): the compositor's mix a row at a time, a picture
   laid OVER, the nearest-neighbour resample of a landing window, a rectangle
   copied row by row as memory_copy copies, and one copied with each row
   reversed. memory_compare is memcmp, which lib.c names it. */
static void memory_mix_u32_row(u32 *into,const u32 *ink,const u8 *cover,positive count) {
    for(positive i=0;i<count;i++) {
        u32 c=cover[i],out=0;
        if(!c)continue;
        if(c==255){into[i]=ink[i];continue;}
        for(unsigned shift=0;shift<24;shift+=8)
            out|=((into[i]>>shift&255)*(255-c)+(ink[i]>>shift&255)*c+127)/255<<shift;
        into[i]=out;
    }
}
static void memory_over_u32_rect(u32 *into,positive into_stride,const u32 *from,positive from_stride,
                                 positive width,positive height) {
    for(positive y=0;y<height;y++)for(positive x=0;x<width;x++) {
        u32 over=from[y*from_stride+x],under=into[y*into_stride+x],through=255-(over>>24),out=0xff000000u;
        for(unsigned shift=0;shift<24;shift+=8) {
            u32 ch=(over>>shift&255)+((under>>shift&255)*through+127)/255;
            out|=(ch>255?255:ch)<<shift;
        }
        into[y*into_stride+x]=out;
    }
}
static void memory_resample_u32_rect(u32 *into,positive into_stride,positive visible_width,positive visible_height,
                                     const u32 *from,positive from_stride,positive from_width,positive from_height,
                                     positive width,positive height,positive left,positive top) {
    positive step_x=(from_width<<24)/width,step_y=(from_height<<24)/height;
    positive fy=((2*top+1)*from_height<<23)/height;
    if(!visible_width||!visible_height)return;
    for(positive row=0;row<visible_height;row++,fy+=step_y) {
        const u32 *source=from+(fy>>24)*from_stride;
        u32 *to=into+row*into_stride;
        positive fx=((2*left+1)*from_width<<23)/width;
        for(positive col=0;col<visible_width;col++,fx+=step_x)*to++=source[fx>>24];
    }
}
static void memory_copy_rect_u32(u32 *into,positive into_stride,const u32 *from,positive from_stride,
                                 positive width,positive height) {
    if(!width)return;
    for(positive y=0;y<height;y++)memmove(into+y*into_stride,from+y*from_stride,width*4);
}
/* A copy into a scanout (lib.c streams its rows past the cache) is the same words as memory_copy_rect_u32's; a constant ink
   mixed by a cover table is compositor_mix of the pixel by the share, a share of none leaving it whole; and the scratch a scene
   composes the rows it reads in is grown as the glass's is. */
static void memory_copy_rect_out_u32(u32 *into,positive into_stride,const u32 *from,positive from_stride,
                                     positive width,positive height) {
    memory_copy_rect_u32(into,into_stride,from,from_stride,width,height);
}
static void memory_mix_const_u32_rect(u32 *into,positive into_stride,const u8 *cover,positive cover_stride,
                                      positive width,positive height,u32 ink,u32 alpha) {
    for(positive y=0;y<height;y++)for(positive x=0;x<width;x++) {
        u32 share=cover[y*cover_stride+x]*(alpha&255)/255,pixel=into[y*into_stride+x],out=0;
        if(!share)continue;
        for(unsigned shift=0;shift<24;shift+=8)
            out|=((pixel>>shift&255)*(255-share)+(ink>>shift&255)*share+127)/255<<shift;
        into[y*into_stride+x]=out;
    }
}
static unsigned int *compositor_scratch(unsigned int **at,unsigned long *had,unsigned long bytes) {
    if(bytes<=*had)return *at;
    free(*at);
    *at=calloc(1,bytes);
    *had=*at?bytes:0;
    return *at;
}
static void memory_copy_rect_reverse_u32(u32 *into,long into_pitch,const u32 *from,long from_pitch,
                                         positive width,positive height) {
    for(positive r=0;r<height;r++)for(positive c=0;c<width;c++)
        into[(long)r*into_pitch+(long)c]=from[(long)r*from_pitch+(long)(width-1-c)];
}
#define memory_compare memcmp
/* ui/ui.c holds the face and engine the tabs are written in by
   pointer; the backend's cut never looks inside them. */
typedef struct font_face font_face;
typedef struct font_engine font_engine;
/* The curve and the step are the library's (src/lib.c: pointer_gain, pointer_scale), lifted beside the Canvas loops. */
unsigned long long pointer_gain(long long dx, long long dy, unsigned long long interval);
long long pointer_scale(long long delta, int *remainder, unsigned long long gain);
#include "compositor-backend.inc"
/* The curve at a speed: a movement of that many counts along one axis in a millisecond. */
static int compositor_gain(int speed) { return compositor_gain_of(speed, 0, 1000000); }
/* A failed row says which one, not only that one failed. */
#define cc_check(x) do{if(!(x))fprintf(stderr,"compositor row failed, line %d: %s\n",__LINE__,#x);check(x);}while(0)
#define COMPOSITOR_GS1_1 "c03fc7b82739b497cd5eff57c85b696a8d52772cf69a0a0ff9cacf0b9f1170e3"
#define COMPOSITOR_GS1_2 "f706d5e07f45ec0927174174cada49401fbbabae9125302c19ed1f1f41aa192a"
#define COMPOSITOR_GS1_3 "08b1e123cede390d1692f8385aed42fe7b5dadec6643ce01119dcfbe6debe6af"
#define COMPOSITOR_GS1_4 "b548675f8ed4ac92ea1a8d08a32b52b2fd8d7c5d76d0acc0f9aedfe1e9adf207"
static const char *const compositor_gs1[4]={COMPOSITOR_GS1_1,COMPOSITOR_GS1_2,COMPOSITOR_GS1_3,COMPOSITOR_GS1_4};
static void check_compositor_gs1(void) {
    static u32 body[128*64];
    for(unsigned i=0;i<128*64;i++)body[i]=0x3a5a7a;
    for(int scale=1;scale<=4;scale++) {
        struct compositor_target t={golden_screen,1024,1024,768,0,0,1024,768};
        struct compositor_frame f={80,80,128,64,scale,body,128,0,0,0,UI_RADIUS*scale,COMPOSITOR_LINE*scale,
            COMPOSITOR_INK_LINE};
        compositor_fill(&t,0,0,1024,768,COMPOSITOR_INK_DESKTOP);
        compositor_draw(&t,&f);
        char got[65];golden_sha256(golden_screen,1024*768,got);
        if(strcmp(got,compositor_gs1[scale-1]))
            fprintf(stderr,"the compositor's G-S1 at scale %d is %s, not the golden %s\n",scale,got,compositor_gs1[scale-1]);
        cc_check(!strcmp(got,compositor_gs1[scale-1]));
        /* The window is its pixels inside a line of its own, round at the
           corners: the corner pixel of the line's square is the desktop, the
           middle of the line's top and left are the line, the middle of the
           contents' top row and left column are the contents, the pixels
           just outside the line the desktop, and where the arc leaves the
           line's top row a pixel is between the desktop and the line. */
        int line=COMPOSITOR_LINE*scale,top=80-line,between=0;
        u32 edge=COMPOSITOR_INK_LINE;
        cc_check(golden_screen[top*1024+top]==COMPOSITOR_INK_DESKTOP);
        cc_check(golden_screen[top*1024+80+64]==edge&&golden_screen[(80+32)*1024+top]==edge);
        cc_check(golden_screen[80*1024+80+64]==0x3a5a7a&&golden_screen[(80+32)*1024+80]==0x3a5a7a);
        cc_check(golden_screen[(top-1)*1024+100]==COMPOSITOR_INK_DESKTOP&&golden_screen[(80+32)*1024+top-1]==COMPOSITOR_INK_DESKTOP);
        for(int x=top;x<top+UI_RADIUS*scale;x++){u32 p=golden_screen[top*1024+x];between|=p!=COMPOSITOR_INK_DESKTOP&&p!=edge;}
        cc_check(between);
        /* Damage is a clip, not a different picture: the same scene drawn
           in four quarters is the same buffer. */
        for(int q=0;q<4;q++) {
            struct compositor_target part={golden_screen,1024,1024,768,
                q%2?512:0,q/2?384:0,q%2?1024:512,q/2?768:384};
            compositor_fill(&part,0,0,1024,768,COMPOSITOR_INK_DESKTOP);
            compositor_draw(&part,&f);
        }
        golden_sha256(golden_screen,1024*768,got);
        if(strcmp(got,compositor_gs1[scale-1]))
            fprintf(stderr,"the compositor's G-S1 at scale %d drawn in quarters is %s\n",scale,got);
        cc_check(!strcmp(got,compositor_gs1[scale-1]));
    }
}

/* A window's title is counted in characters: a byte that continues one is
   not one, and the count stops at its room. */
static void check_compositor_title_length(void) {
    cc_check(compositor_title_length("shell",28)==5&&compositor_title_length("\xc3\xa5\xc3\xa4",28)==2&&
             compositor_title_length("abc",2)==2&&compositor_title_length("",28)==0);
}

/* The keymap is a function of a state and a key, so a layout is typed at
   with no card: what the Swedish keys make (å ä ö, AltGr levels, the 102nd
   key), the dead accents and what they make with a letter, Caps Lock, the
   Control fold, and that the other layouts say what the kernel desktop's
   tables said. */
/* cc_check names its argument twice, and these rows have effects: evaluated once. */
#define ck_ok(x) do{int ok_=!!(x);if(!ok_)fprintf(stderr,"keymap row failed, line %d: %s\n",__LINE__,#x);check(ok_);}while(0)
#define KEY_ALTGR WINDOW_KEY_ALTGR
#define KEY_SHIFT WINDOW_KEY_SHIFT
#define KEY_CTRL WINDOW_KEY_CONTROL
static int ck_press(struct compositor_keymap *map,unsigned code,unsigned mods,unsigned out[2]) {
    unsigned scratch[2]={0,0};
    int n=compositor_keymap_event(map,code,1,mods,out);
    int up=compositor_keymap_event(map,code,0,mods,scratch);
    ck_ok(up==(n<0?-1:0));
    return n;
}
static unsigned ck_one(struct compositor_keymap *map,unsigned code,unsigned mods) {
    unsigned out[2]={0,0};
    return ck_press(map,code,mods,out)==1?out[0]:0xffffffffu;
}
static int ck_layout(struct compositor_keymap *map,const char *name) {
    int at=compositor_layout_index(name);
    memset(map,0,sizeof(*map));
    if(at>=0)map->layout=(unsigned char)at;
    return at;
}
static void check_compositor_keymap(void) {
    struct compositor_keymap m;
    unsigned out[2];
    int n;
    /* The US base: what the kernel's table said, and a layout that does not exist. */
    ck_ok(ck_layout(&m,"us")==0&&ck_layout(&m,"xx")<0&&ck_layout(&m,"s")<0&&ck_layout(&m,"sev")<0);
    ck_ok(ck_one(&m,30,0)=='a'&&ck_one(&m,30,KEY_SHIFT)=='A'&&ck_one(&m,2,KEY_SHIFT)=='!');
    ck_ok(ck_one(&m,28,0)=='\n'&&ck_one(&m,14,0)==127&&ck_one(&m,1,0)==27&&ck_one(&m,57,0)==' ');
    ck_ok(ck_one(&m,82,0)=='0'&&ck_one(&m,98,0)=='/'&&ck_one(&m,96,0)=='\n'&&ck_one(&m,55,0)=='*');
    ck_ok(ck_one(&m,30,KEY_CTRL)==1&&ck_one(&m,46,KEY_CTRL|KEY_SHIFT)==3&&ck_one(&m,2,KEY_CTRL)=='1');
    ck_ok(ck_one(&m,CK_102ND,0)==0xffffffffu&&ck_one(&m,59,0)==0xffffffffu&&ck_one(&m,103,0)==0xffffffffu);
    ck_ok(compositor_keymap_event(&m,30,0,0,out)==0&&compositor_keymap_event(&m,200,1,0,out)==0);
    /* Swedish: the three letters, the AltGr level, the key beside the left shift. */
    ck_ok(ck_layout(&m,"se")>0);
    ck_ok(ck_one(&m,26,0)==0xE5&&ck_one(&m,39,0)==0xF6&&ck_one(&m,40,0)==0xE4);
    ck_ok(ck_one(&m,26,KEY_SHIFT)==0xC5&&ck_one(&m,39,KEY_SHIFT)==0xD6&&ck_one(&m,40,KEY_SHIFT)==0xC4);
    ck_ok(ck_one(&m,3,KEY_ALTGR)=='@'&&ck_one(&m,3,KEY_SHIFT)=='"'&&ck_one(&m,4,KEY_ALTGR)==0xA3);
    ck_ok(ck_one(&m,8,KEY_ALTGR)=='{'&&ck_one(&m,9,KEY_ALTGR)=='['&&ck_one(&m,10,KEY_ALTGR)==']'&&ck_one(&m,11,KEY_ALTGR)=='}');
    ck_ok(ck_one(&m,8,KEY_SHIFT)=='/'&&ck_one(&m,12,KEY_ALTGR)=='\\'&&ck_one(&m,6,KEY_ALTGR)==0x20AC);
    ck_ok(ck_one(&m,CK_102ND,0)=='<'&&ck_one(&m,CK_102ND,KEY_SHIFT)=='>'&&ck_one(&m,CK_102ND,KEY_ALTGR)=='|');
    ck_ok(ck_one(&m,53,0)=='-'&&ck_one(&m,53,KEY_SHIFT)=='_'&&ck_one(&m,41,0)==0xA7&&ck_one(&m,41,KEY_SHIFT)==0xBD);
    /* AltGr with Shift, where the layout has nothing there, is AltGr; Shift where it has nothing is plain. */
    ck_ok(ck_one(&m,8,KEY_ALTGR|KEY_SHIFT)=='{'&&ck_one(&m,57,KEY_ALTGR)==' ');
    /* A dead key makes nothing and is taken whole: its release and its repeats. */
    ck_ok(compositor_keymap_event(&m,13,1,0,out)==-1&&compositor_keymap_event(&m,13,2,0,out)==-1&&
             compositor_keymap_event(&m,42,1,0,out)==0&&compositor_keymap_event(&m,13,0,0,out)==-1&&
             compositor_keymap_event(&m,13,0,0,out)==0);
    ck_ok(m.dead==CK_ACUTE);
    /* ´ and e is é, ´ and E is É, and nothing is left waiting. */
    ck_ok(ck_one(&m,18,0)==0xE9&&m.dead==0&&ck_one(&m,18,0)=='e');
    ck_press(&m,13,0,out);ck_ok(ck_one(&m,18,KEY_SHIFT)==0xC9);
    /* ` (Shift and the same key) and a is à, ¨ and u is ü, ^ (Shift and ¨) and o is ô. */
    ck_press(&m,13,KEY_SHIFT,out);ck_ok(ck_one(&m,30,0)==0xE0);
    ck_press(&m,27,0,out);ck_ok(ck_one(&m,22,0)==0xFC);
    ck_press(&m,27,KEY_SHIFT,out);ck_ok(ck_one(&m,24,0)==0xF4);
    /* AltGr and ¨ is a dead tilde, and with n it is ñ, with a ã, with o õ. */
    ck_press(&m,27,KEY_ALTGR,out);ck_ok(ck_one(&m,49,0)==0xF1);
    ck_press(&m,27,KEY_ALTGR,out);ck_ok(ck_one(&m,30,KEY_SHIFT)==0xC3);
    ck_press(&m,27,KEY_ALTGR,out);ck_ok(ck_one(&m,24,0)==0xF5);
    /* The accent on its own: with a space, or twice, it is the spacing accent. */
    ck_press(&m,13,0,out);ck_ok(ck_one(&m,57,0)==CK_ACUTE);
    ck_press(&m,13,0,out);ck_ok(ck_one(&m,13,0)==CK_ACUTE&&m.dead==0);
    ck_press(&m,27,KEY_ALTGR,out);ck_ok(ck_one(&m,57,0)=='~');
    /* On a letter it does not go on, the accent is typed and then the letter: nothing is lost. */
    ck_press(&m,13,0,out);n=ck_press(&m,45,0,out);
    ck_ok(n==2&&out[0]==CK_ACUTE&&out[1]=='x'&&m.dead==0);
    /* A second accent over the first types the first and waits with the second. */
    ck_press(&m,13,0,out);n=ck_press(&m,27,0,out);
    ck_ok(n==1&&out[0]==CK_ACUTE&&m.dead==CK_DIAERESIS);
    ck_ok(ck_one(&m,30,0)==0xE4&&m.dead==0);
    /* A key that is no character leaves it waiting; a control character ends it. */
    ck_press(&m,13,0,out);ck_ok(compositor_keymap_event(&m,103,1,0,out)==0&&m.dead==CK_ACUTE);
    ck_ok(ck_one(&m,28,0)=='\n'&&m.dead==0);
    /* Control over a dead key's letter is a control code and no accent. */
    ck_press(&m,13,0,out);ck_ok(ck_one(&m,30,KEY_CTRL)==1&&m.dead==0);
    /* Caps Lock turns a letter over, å and ä with it, and nothing that is not one. */
    ck_ok(compositor_keymap_event(&m,58,1,0,out)==0&&compositor_keymap_event(&m,58,2,0,out)==0&&m.caps==1);
    ck_ok(ck_one(&m,30,0)=='A'&&ck_one(&m,26,0)==0xC5&&ck_one(&m,40,0)==0xC4&&ck_one(&m,39,0)==0xD6);
    ck_ok(ck_one(&m,30,KEY_SHIFT)=='a'&&ck_one(&m,26,KEY_SHIFT)==0xE5&&ck_one(&m,2,0)=='1');
    ck_ok(ck_one(&m,3,KEY_ALTGR)=='@'&&ck_one(&m,53,0)=='-');
    ck_press(&m,13,0,out);ck_ok(ck_one(&m,18,0)==0xC9);
    compositor_keymap_event(&m,58,0,0,out);compositor_keymap_event(&m,58,1,0,out);
    ck_ok(m.caps==0&&ck_one(&m,30,0)=='a');
    /* The other layouts are the kernel desktop's, and aliased as the CLI names them. */
    #define SAME(a,b) (compositor_layouts[ck_layout(&m,a)].patch==compositor_layouts[compositor_layout_index(b)].patch)
    ck_ok(SAME("sv","se")&&SAME("fi","se")&&SAME("gb","uk")&&SAME("nb","no")&&SAME("dk","no"));
    #undef SAME
    ck_ok(ck_layout(&m,"uk")>0&&ck_one(&m,4,KEY_SHIFT)==0xA3&&ck_one(&m,43,0)=='#'&&ck_one(&m,CK_102ND,0)=='\\');
    ck_ok(ck_layout(&m,"de")>0&&ck_one(&m,21,0)=='z'&&ck_one(&m,44,0)=='y'&&ck_one(&m,12,0)==0xDF&&ck_one(&m,16,KEY_ALTGR)=='@');
    ck_press(&m,13,0,out);ck_ok(ck_one(&m,18,0)==0xE9);
    ck_ok(ck_layout(&m,"no")>0&&ck_one(&m,39,0)==0xF8&&ck_one(&m,40,0)==0xE6&&ck_one(&m,26,0)==0xE5&&ck_one(&m,13,0)=='\\');
    ck_press(&m,27,0,out);ck_ok(ck_one(&m,24,0)==0xF6);
    ck_ok(ck_layout(&m,"fr")>0&&ck_one(&m,16,0)=='a'&&ck_one(&m,2,0)=='&'&&ck_one(&m,2,KEY_SHIFT)=='1'&&ck_one(&m,3,0)==0xE9);
    ck_press(&m,26,0,out);ck_ok(ck_one(&m,18,0)==0xEA);
    compositor_keymap_event(&m,58,1,0,out);ck_ok(ck_one(&m,3,0)==0xC9&&ck_one(&m,3,KEY_SHIFT)=='2');
    ck_ok(ck_layout(&m,"es")>0&&ck_one(&m,39,0)==0xF1&&ck_one(&m,13,0)==0xA1&&ck_one(&m,43,0)==0xE7);
    ck_press(&m,40,0,out);ck_ok(ck_one(&m,30,0)==0xE1);
    ck_ok(ck_layout(&m,"it")>0&&ck_one(&m,26,0)==0xE8&&ck_one(&m,26,KEY_SHIFT)==0xE9&&ck_one(&m,39,0)==0xF2&&ck_one(&m,41,0)=='\\');
    /* Arabic, Hebrew and Persian: the letters of their keys, the digits as they are, and a chord with Control is the Latin key. */
    ck_ok(ck_layout(&m,"ar")>0&&ck_one(&m,35,0)==0x627&&ck_one(&m,30,0)==0x634&&ck_one(&m,16,0)==0x636&&ck_one(&m,53,0)==0x638&&
             ck_one(&m,53,KEY_SHIFT)==0x61F&&ck_one(&m,2,0)=='1'&&ck_one(&m,16,KEY_SHIFT)==0x64E&&ck_one(&m,34,KEY_SHIFT)==0xFEF7);
    ck_ok(ck_one(&m,46,KEY_CTRL)==3&&ck_one(&m,45,KEY_CTRL)==24&&ck_one(&m,47,KEY_CTRL)==22&&ck_one(&m,35,KEY_CTRL|KEY_SHIFT)==8);
    ck_ok(ck_layout(&m,"il")>0&&ck_one(&m,20,0)==0x5D0&&ck_one(&m,20,KEY_SHIFT)==0x5D0&&ck_one(&m,30,0)==0x5E9&&ck_one(&m,53,0)=='.'&&
             ck_one(&m,51,0)==0x5EA&&ck_one(&m,46,KEY_CTRL)==3);
    ck_ok(ck_layout(&m,"ir")>0&&ck_one(&m,32,0)==0x6CC&&ck_one(&m,27,0)==0x686&&ck_one(&m,50,0)==0x67E&&ck_one(&m,40,0)==0x6AF&&
             ck_one(&m,35,0)==0x627);
    /* Every layout, every key, every level: a character of the layout's own scripts and of nothing else is a bug's absence. A Latin
       layout's are Latin-1, the euro and the dead accents; Arabic's are those, the Arabic block (U+0600 to U+06FF) and the lam-alef
       ligatures of Presentation Forms-B (U+FEF5 to U+FEFC); Persian's are those and the Arabic block (its pe, che, ke, ge and ye are
       in it), without the ligatures; Hebrew's are those and the Hebrew block (U+0590 to U+05FF). The three were held to the Latin
       set, which they are right to leave; a Latin layout is held to what it was. */
    for(unsigned l=0;l<sizeof(compositor_layouts)/sizeof(compositor_layouts[0]);l++) {
        const char *name=compositor_layouts[l].name;
        int arabic=!strcmp(name,"ar")||!strcmp(name,"ir"),forms=!strcmp(name,"ar"),hebrew=!strcmp(name,"il");
        for(unsigned code=0;code<CK_TABLE;code++)
            for(unsigned mods=0;mods<4;mods++) {
                struct compositor_keymap k={0};k.layout=(unsigned char)l;
                unsigned c=compositor_keymap_character(&k,code,(mods&1?KEY_SHIFT:0)|(mods&2?KEY_ALTGR:0));
                ck_ok(c<0x100||c==0x20AC||(c&CK_DEAD&&(c&~CK_DEAD)<0x100)||(arabic&&c>=0x600&&c<=0x6FF)||
                      (forms&&c>=0xFEF5&&c<=0xFEFC)||(hebrew&&c>=0x590&&c<=0x5FF));
            }
    }
    /* And the three have no hole: every letter key of the US layout is a letter of the script plain (Hebrew's q and w are / and '
       by SI 1452, as XKB's il has them), and no letter key answers a Latin letter plain or with Shift, which a key its table left
       out would, falling back to the US one's. */
    {
        static const unsigned char letters[26]={16,17,18,19,20,21,22,23,24,25,30,31,32,33,34,35,36,37,38,44,45,46,47,48,49,50};
        static const char *const scripts[3]={"ar","il","ir"};
        for(unsigned s=0;s<3;s++) {
            struct compositor_keymap k={0};k.layout=(unsigned char)compositor_layout_index(scripts[s]);
            for(unsigned at=0;at<26;at++) {
                unsigned plain=compositor_keymap_character(&k,letters[at],0),shifted=compositor_keymap_character(&k,letters[at],KEY_SHIFT);
                int punctuation=s==1&&(letters[at]==16||letters[at]==17);
                int letter=s==1?plain>=0x5D0&&plain<=0x5EA:(plain>=0x621&&plain<=0x64A)||(plain>=0x671&&plain<=0x6D3)||(s==0&&plain>=0xFEF5&&plain<=0xFEFC);
                ck_ok(compositor_layout_index(scripts[s])>0&&(punctuation?plain<0x80:letter));
                ck_ok(!((plain|0x20)>='a'&&(plain|0x20)<='z')&&!((shifted|0x20)>='a'&&(shifted|0x20)<='z'));
            }
        }
    }
    /* What is held is a bit a key, and one keyboard letting go of Shift is not another's. */
    ck_ok(compositor_held_flags(compositor_held_bit(42)|compositor_held_bit(54))==WINDOW_KEY_SHIFT);
    ck_ok(compositor_held_flags(compositor_held_bit(97))==WINDOW_KEY_CONTROL&&compositor_held_flags(compositor_held_bit(29))==WINDOW_KEY_CONTROL);
    ck_ok(compositor_held_flags(compositor_held_bit(100))==WINDOW_KEY_ALTGR&&compositor_held_flags(compositor_held_bit(56))==WINDOW_KEY_ALT);
    ck_ok(compositor_held_flags(compositor_held_bit(125))==WINDOW_KEY_SUPER&&compositor_held_flags(compositor_held_bit(126))==WINDOW_KEY_SUPER);
    ck_ok(compositor_held_bit(30)==0&&compositor_held_flags(0)==0);
}

/* What a hand means: the gain of a mouse's speed (the kernel desktop's curve,
   one to one below a count a millisecond and two and a half times above
   eight), a count turned into pixels without losing its fraction, a tablet's
   report on the output rounded down as the kernel desktop's was, and the
   square root the speed is made with. */
static void check_compositor_pointer(void) {
    ck_ok(compositor_gain(0)==1024&&compositor_gain(1)==1024&&compositor_gain(8)==2560&&compositor_gain(99)==2560);
    ck_ok(compositor_gain(4)==1024+1536*3/7&&compositor_gain(2)<compositor_gain(3)&&compositor_gain(7)<2560);
    /* The curve only rises, and is exact at each count. */
    for(int speed=1;speed<8;speed++)ck_ok(compositor_gain(speed+1)>compositor_gain(speed)&&
        compositor_gain(speed)==1024+1536*(speed-1)/7);
    /* The fraction is kept: a hundred steps of one count at a gain that is not whole are what the gain says. */
    for(int gain=1024;gain<=2560;gain+=137)
        for(int sign=-1;sign<=1;sign+=2) {
            int remainder=0;long long pixels=0;
            for(int step=0;step<100;step++)pixels+=compositor_accel(sign,&remainder,gain);
            ck_ok(pixels*1024+remainder==(long long)sign*100*gain&&remainder>-1024&&remainder<1024);
        }
    { int remainder=0; ck_ok(compositor_accel(0,&remainder,2560)==0&&remainder==0); }
    { int remainder=0; ck_ok(compositor_accel(-2147483647-1,&remainder,2560)==-2147483647&&compositor_accel(2147483647,&remainder,2560)==2147483647); }
    /* One movement is the gain of both axes at their speed: 3 and 4 is 5 counts, in the time between reports. */
    ck_ok(compositor_gain_of(0,0,1000000)==1024&&compositor_gain_of(3,4,1000000)==compositor_gain(5)&&
          compositor_gain_of(3,4,0)==compositor_gain(5)&&compositor_gain_of(3,4,10000000)==1024);
    ck_ok(compositor_gain_of(-3,-4,1000000)==compositor_gain(5)&&compositor_gain_of(4,-3,1000000)==compositor_gain(5));
    ck_ok(compositor_gain_of(1000,0,1000000)==2560&&compositor_gain_of(-2147483647-1,-2147483647-1,1000000)==2560);
    ck_ok(compositor_gain_of(1,0,~0ull)==1024);
    /* The gain the hand chose: no setting and a hundred percent are the curve to the bit; a sensitivity scales it, with
       the curve (the pixels go as the percent) or one to one when acceleration is off, whatever the speed. */
    for(int gain=1024;gain<=2560;gain+=61) {
        ck_ok(compositor_gain_wanted(gain,0,0)==gain&&compositor_gain_wanted(gain,0,100)==gain);
        ck_ok(compositor_gain_wanted(gain,0,200)==2*gain&&compositor_gain_wanted(gain,0,50)==gain/2);
        ck_ok(compositor_gain_wanted(gain,1,0)==1024&&compositor_gain_wanted(gain,1,100)==1024&&
              compositor_gain_wanted(gain,1,150)==1536&&compositor_gain_wanted(gain,1,10)==102);
    }
    { int remainder=0; ck_ok(compositor_accel(10,&remainder,compositor_gain_wanted(1024,0,200))==20&&
                             compositor_accel(10,&remainder,compositor_gain_wanted(1024,0,0))==10); }
    { int remainder=0; long long pixels=0;
      for(int step=0;step<100;step++)pixels+=compositor_accel(1,&remainder,compositor_gain_wanted(1024,0,25));
      ck_ok(pixels==25); /* a quarter of a hundred counts, the fraction kept */ }
    /* A keyboard's repeat: the setting's delay and period where kept, the device's own where not; a rate of none is a period of none. */
    ck_ok(compositor_repeat_period(30)==33&&compositor_repeat_period(1)==1000&&compositor_repeat_period(100)==10&&
          compositor_repeat_period(7)==143&&compositor_repeat_period(0)==0&&compositor_repeat_period(-3)==0);
    { const int own[2]={250,33}; int into[2];
      compositor_repeat_target(0,999,999,own,into);ck_ok(into[0]==250&&into[1]==33);
      compositor_repeat_target(1,400,999,own,into);ck_ok(into[0]==400&&into[1]==33);
      compositor_repeat_target(2,999,10,own,into);ck_ok(into[0]==250&&into[1]==100);
      compositor_repeat_target(3,150,0,own,into);ck_ok(into[0]==150&&into[1]==0);
      compositor_repeat_target(3,2000,100,own,into);ck_ok(into[0]==2000&&into[1]==10); }
    /* The square root, which is exact on whole squares and the floor between. */
    for(unsigned long long v=0;v<70000;v++) {
        unsigned long long r=compositor_isqrt(v);
        ck_ok(r*r<=v&&(r+1)*(r+1)>v);
    }
    for(unsigned long long v=0x7fffffffffffffffull;v>0x7fffffffffff0000ull;v-=4093) {
        unsigned long long r=compositor_isqrt(v);
        ck_ok(r*r<=v&&(r+1)*(r+1)>v);
    }
    ck_ok(compositor_isqrt(~0ull)==4294967295ull&&compositor_isqrt(1ull<<62)==1ull<<31);
    /* A tablet: offset over span times the size, rounded down, held to the range. */
    ck_ok(compositor_tablet(0,0,32767,2560)==0&&compositor_tablet(32767,0,32767,2560)==2560&&
          compositor_tablet(16384,0,32767,2560)==1280&&compositor_tablet(-5,0,32767,2560)==0&&
          compositor_tablet(99999,0,32767,2560)==2560);
    ck_ok(compositor_tablet(5,0,0,2560)==0&&compositor_tablet(5,9,3,2560)==0&&compositor_tablet(-100,-200,200,1000)==250);
    /* The lane's tablet says the report of the pixel rounded up and gets the pixel, and the one rounded down is one short. */
    int short_by_one=0;
    for(int x=0;x<2560;x++) {
        int up=(int)((x*32767LL+2559)/2560);
        ck_ok(compositor_tablet(up,0,32767,2560)==x);
        short_by_one+=compositor_tablet((int)(x*32767LL/2560),0,32767,2560)==x-1;
    }
    ck_ok(short_by_one>0);
    /* Shake to find it: five reversals of six counts or more inside 700 ms, and no less. */
    {
        struct compositor_shake shake={0};
        unsigned long long now=1000000000ull;
        int found=0;
        for(int step=0;step<12;step++){found+=compositor_shake(&shake,step&1?-8:8,now);now+=50000000ull;}
        ck_ok(found==2); /* the fifth reversal and the tenth */
        memset(&shake,0,sizeof(shake));found=0;now=1000000000ull;
        for(int step=0;step<12;step++){found+=compositor_shake(&shake,step&1?-8:8,now);now+=200000000ull;}
        ck_ok(found==0); /* a wobble that takes too long */
        memset(&shake,0,sizeof(shake));found=0;now=1000000000ull;
        for(int step=0;step<12;step++){found+=compositor_shake(&shake,step&1?-5:5,now);now+=10000000ull;}
        ck_ok(found==0); /* a hand resting on the mouse */
        memset(&shake,0,sizeof(shake));found=0;now=1000000000ull;
        for(int step=0;step<12;step++){found+=compositor_shake(&shake,8,now);now+=10000000ull;}
        ck_ok(found==0); /* a hand going the one way */
    }
    /* The cursors, at the size the classic Windows ones are: the arrow is 81 white and 53 black pixels, the horizontal and
       vertical resize 80 and 61, the diagonal resize 84 and 63, the hand 158 and 75, each at its scale squared and nothing
       between; the hotspot is the corner of the arrow and the middle of the others (the hand's is its fingertip), and
       what is drawn stays inside its cell and inside the damage. */
    {
        static const int white[CP_SHAPES]={81,80,80,84,84,64,72,136,158,105,164},black[CP_SHAPES]={53,61,61,63,63,26,33,89,75,125,176};
        static u32 plane[64*64];
        for(unsigned shape=0;shape<CP_SHAPES;shape++)
            for(int scale=1;scale<=2;scale++) {
                struct compositor_target t={plane,64,64,64,0,0,64,64};
                struct compositor_pointer p={.shape=shape,.scale=scale*256};
                int hot_x,hot_y,span=compositor_pointer_span(&p,&hot_x,&hot_y);
                memset(plane,0,sizeof(plane));
                compositor_cursor_draw(&t,hot_x,hot_y,&p,0xffffffffu,0xff000000u);
                int w=0,b=0,other=0,outside=0;
                for(int y=0;y<64;y++)for(int x=0;x<64;x++) {
                    u32 v=plane[y*64+x];
                    w+=v==0xffffffffu;b+=v==0xff000000u;other+=v&&v!=0xffffffffu&&v!=0xff000000u;
                    outside+=v&&(x>=CP_CELL*scale||y>=CP_CELL*scale);
                }
                ck_ok(span==CP_CELL*scale&&hot_x==compositor_cursor_hot[shape][0]*scale&&hot_y==compositor_cursor_hot[shape][1]*scale);
                ck_ok(w==white[shape]*scale*scale&&b==black[shape]*scale*scale&&!other&&!outside);
            }
        /* The same into a damaged part of a screen: nothing outside the damage is written. */
        static u32 screen[200*100];
        for(unsigned shape=0;shape<CP_SHAPES;shape++) {
            struct compositor_target t={screen,200,200,100,50,30,90,60};
            struct compositor_pointer p={.shape=shape,.scale=2*256};
            for(unsigned i=0;i<sizeof(screen)/sizeof(screen[0]);i++)screen[i]=7;
            compositor_cursor_draw(&t,60,40,&p,0xffffffu,0);
            int inside_changed=0,outside_changed=0;
            for(int y=0;y<100;y++)for(int x=0;x<200;x++) {
                int inside=x>=50&&x<90&&y>=30&&y<60;
                if(screen[y*200+x]!=7){if(inside)inside_changed++;else outside_changed++;}
            }
            ck_ok(inside_changed>0&&!outside_changed);
        }
        ck_ok(compositor_cursor_by_edges[0]==CP_ARROW&&compositor_cursor_by_edges[CP_EDGE_LEFT]==CP_RESIZE_H&&
              compositor_cursor_by_edges[CP_EDGE_RIGHT]==CP_RESIZE_H&&compositor_cursor_by_edges[CP_EDGE_TOP]==CP_RESIZE_V&&
              compositor_cursor_by_edges[CP_EDGE_BOTTOM]==CP_RESIZE_V&&
              compositor_cursor_by_edges[CP_EDGE_TOP|CP_EDGE_LEFT]==CP_RESIZE_NWSE&&
              compositor_cursor_by_edges[CP_EDGE_BOTTOM|CP_EDGE_RIGHT]==CP_RESIZE_NWSE&&
              compositor_cursor_by_edges[CP_EDGE_TOP|CP_EDGE_RIGHT]==CP_RESIZE_NESW&&
              compositor_cursor_by_edges[CP_EDGE_BOTTOM|CP_EDGE_LEFT]==CP_RESIZE_NESW&&
              compositor_cursor_by_edges[CP_EDGE_LEFT|CP_EDGE_RIGHT]==CP_ARROW);
        /* The click hand has its index finger up: the pointer is the fingertip, the top row of the finger, and the arrow's is its tip. */
        ck_ok(compositor_cursor_hot[CP_HAND][0]==5&&compositor_cursor_hot[CP_HAND][1]==0&&compositor_cursor_art[CP_HAND][0][5]=='#'&&
              compositor_cursor_art[CP_ARROW][0][0]=='#'&&compositor_cursor_art[CP_ARROW][0][1]=='.');
    }
}

/* The space the screens make (the outputs section): the arrangement's words and
   what they lay, the pointer kept to the screens and crossing only onto one,
   the screen a window belongs to, a window kept reachable, a drop read at a
   screen's free edges only, the halves of a screen, and a window carried from
   a screen that changed to the one it is on now. One screen is the old rules
   to the pixel. */
#define co_check(x) do{int ok_=!!(x);if(!ok_)fprintf(stderr,"outputs row failed, line %d: %s\n",__LINE__,#x);check(ok_);}while(0)
static int co_rect(const int *r,int x,int y,int w,int h){return r[0]==x&&r[1]==y&&r[2]==w&&r[3]==h;}
static unsigned int co_lay(const char *text,int (*rects)[4],unsigned char *order) {
    static const char names[3][WINDOWS_OUTPUT_NAME]={"Virtual-1","Virtual-2","HDMI-A-1"};
    static const int sizes[3][2]={{2560,1080},{1920,1080},{1280,1024}};
    return windows_arrange_lay(text,names,sizes,2,rects,order);
}
static void dline(const char *text, struct windows_display *display) { windows_display_line(text, strlen(text), display); }
/* The displays file (/root/canvas.displays): a line's fields and their defaults, a bad field named by its bit and
   kept at its default, the first of a repeated name holding, a write that reads back the same, and the arrangement
   with a display's place put in it through the same words. */
static void check_compositor_displays(void) {
    static const char names[2][WINDOWS_OUTPUT_NAME]={"Virtual-1","Virtual-2"};
    static const int sizes[2][2]={{2560,1080},{1920,1080}};
    struct windows_display d[WINDOWS_DISPLAYS], list[WINDOWS_DISPLAYS];
    char text[1024], pinned[256];
    int rects[WINDOWS_OUTPUTS][4];
    unsigned char order[WINDOWS_OUTPUTS];
    unsigned int n, made;
    /* Defaults: a name alone is rotation 0, refresh preferred, no place, and nothing bad. */
    dline("DP-1",&d[0]);
    co_check(!strcmp(d[0].name,"DP-1")&&d[0].rotation==0&&d[0].refresh==0&&!d[0].placed&&d[0].bad==0);
    /* Every field, in any order: rotation 90 at 60 Hz, placed at -1920,40. */
    dline("HDMI-A-1 y=40 refresh=60 x=-1920 rotation=90",&d[0]);
    co_check(!strcmp(d[0].name,"HDMI-A-1")&&d[0].rotation==90&&d[0].refresh==60&&d[0].placed&&d[0].x==-1920&&d[0].y==40&&d[0].bad==0);
    dline("eDP-1 rotation=270 refresh=preferred",&d[0]);
    co_check(!strcmp(d[0].name,"eDP-1")&&d[0].rotation==270&&d[0].refresh==0&&!d[0].placed&&d[0].bad==0);
    /* Malformed fields keep their defaults and set their bits; x alone is missing its y. */
    dline("Virtual-2 rotation=45 refresh=fast x=5",&d[0]);
    co_check(d[0].rotation==0&&d[0].refresh==0&&!d[0].placed&&d[0].bad==(WINDOWS_DISPLAY_ROTATION|WINDOWS_DISPLAY_REFRESH|WINDOWS_DISPLAY_Y));
    dline("Virtual-2 rotation=360",&d[0]);
    co_check(d[0].rotation==0&&d[0].bad==WINDOWS_DISPLAY_ROTATION);
    dline("Virtual-2 refresh=0",&d[0]);
    co_check(d[0].refresh==0&&d[0].bad==WINDOWS_DISPLAY_REFRESH);
    dline("Virtual-2 refresh=1001",&d[0]);
    co_check(d[0].bad==WINDOWS_DISPLAY_REFRESH);
    dline("Virtual-2 x=1 y=2x",&d[0]);
    co_check(!d[0].placed&&d[0].bad==WINDOWS_DISPLAY_Y);
    dline("Virtual-2 x=32769 y=0",&d[0]);
    co_check(!d[0].placed&&d[0].bad==WINDOWS_DISPLAY_X);
    /* A word that is no field, a word with no '=', a field given twice (the first holds): all bad with other. */
    dline("Virtual-2 scale=2",&d[0]);
    co_check(d[0].scale==100&&d[0].bad==WINDOWS_DISPLAY_SCALE);
    dline("Virtual-2 rotation",&d[0]);
    co_check(d[0].bad==WINDOWS_DISPLAY_OTHER);
    dline("Virtual-2 rotation=90 rotation=0",&d[0]);
    co_check(d[0].rotation==90&&d[0].bad==WINDOWS_DISPLAY_OTHER);
    /* A name that is not a connector's: an '@', a space, too long, or none at all: the line says so and has none. */
    dline("bad_name rotation=90",&d[0]);
    co_check(d[0].name[0]==0&&d[0].bad==WINDOWS_DISPLAY_NAME);
    dline("Virtual-2@1,2 rotation=90",&d[0]);
    co_check(d[0].name[0]==0&&d[0].bad==WINDOWS_DISPLAY_NAME);
    dline("ABCDEFGHIJKLMNOP",&d[0]);
    co_check(d[0].name[0]==0&&d[0].bad==WINDOWS_DISPLAY_NAME);
    dline("   ",&d[0]);
    co_check(d[0].name[0]==0&&d[0].bad==WINDOWS_DISPLAY_NAME);
    /* The scale: one of the six presets in percent, 100 when none is given; any other value is bad and kept at 100, and so is a
       scale given twice (the first holds). */
    { static const char *const presets[6]={"Virtual-2 scale=50","Virtual-2 scale=75","Virtual-2 scale=100","Virtual-2 scale=125",
                                           "Virtual-2 scale=150","Virtual-2 scale=200"};
      static const unsigned int percent[6]={50,75,100,125,150,200};
      for (unsigned int each = 0; each < 6; each++) {
          dline(presets[each],&d[0]);
          co_check(d[0].scale==percent[each]&&d[0].bad==0); }
      dline("eDP-1 rotation=90",&d[0]);
      co_check(d[0].scale==100&&d[0].bad==0);
      dline("Virtual-2 scale=60",&d[0]);
      co_check(d[0].scale==100&&d[0].bad==WINDOWS_DISPLAY_SCALE);
      dline("Virtual-2 scale=0",&d[0]);
      co_check(d[0].scale==100&&d[0].bad==WINDOWS_DISPLAY_SCALE);
      dline("Virtual-2 scale=1000",&d[0]);
      co_check(d[0].scale==100&&d[0].bad==WINDOWS_DISPLAY_SCALE);
      dline("Virtual-2 scale=150%",&d[0]);
      co_check(d[0].scale==100&&d[0].bad==WINDOWS_DISPLAY_SCALE);
      dline("Virtual-2 scale=150 scale=50",&d[0]);
      co_check(d[0].scale==150&&d[0].bad==WINDOWS_DISPLAY_OTHER);
      /* Written with a scale other than 100 only, and read back to the same scale; a 100 is the default and is not written. */
      static const char scales[]="HDMI-A-1 scale=75\nVirtual-2 rotation=90 scale=200 x=1 y=2\neDP-1 scale=100\n";
      n=windows_displays_read(scales,list,WINDOWS_DISPLAYS);
      made=windows_displays_write(text,sizeof(text),list,n);
      co_check(made&&!strcmp(text,"HDMI-A-1 rotation=0 refresh=preferred scale=75\nVirtual-2 rotation=90 refresh=preferred scale=200 x=1 y=2\n"));
      made=windows_displays_read(text,d,WINDOWS_DISPLAYS);
      co_check(made==2&&d[0].scale==75&&d[1].scale==200&&d[1].rotation==90&&d[1].placed&&d[1].bad==0); }

    /* A file: blank lines are none, a bad name is kept as a line with none, and a repeated name is bad after the first. */
    static const char file[]="HDMI-A-1 rotation=90 refresh=60\n\n  \nbad_name x=1 y=2\nVirtual-2 x=-10 y=20\nVirtual-2 x=0 y=0\n";
    n=windows_displays_read(file,list,WINDOWS_DISPLAYS);
    co_check(n==4&&!strcmp(list[0].name,"HDMI-A-1")&&list[1].name[0]==0&&list[1].bad==WINDOWS_DISPLAY_NAME);
    co_check(!strcmp(list[2].name,"Virtual-2")&&list[2].placed&&list[2].x==-10&&list[2].y==20&&list[2].bad==0);
    co_check(!strcmp(list[3].name,"Virtual-2")&&list[3].bad==WINDOWS_DISPLAY_OTHER&&list[3].placed);
    co_check(windows_display_of(list,n,"Virtual-2",9)==&list[2]&&windows_display_of(list,n,"Virtual-1",9)==0);
    /* A write gives the lines that have a setting; read back, they are the same settings. */
    static const char lines[]="HDMI-A-1 rotation=90 refresh=60\nVirtual-2 x=-10 y=20 rotation=180\neDP-1 refresh=preferred\n";
    n=windows_displays_read(lines,list,WINDOWS_DISPLAYS);
    made=windows_displays_write(text,sizeof(text),list,n);
    co_check(made&&!strcmp(text,"HDMI-A-1 rotation=90 refresh=60\nVirtual-2 rotation=180 refresh=preferred x=-10 y=20\n"));
    { char small[8]; co_check(windows_displays_write(small,sizeof(small),list,n)==0); }
    made=windows_displays_read(text,d,WINDOWS_DISPLAYS);
    co_check(made==2&&d[0].rotation==90&&d[0].refresh==60&&!d[0].placed&&d[0].bad==0);
    co_check(!strcmp(d[1].name,"Virtual-2")&&d[1].rotation==180&&d[1].refresh==0&&d[1].placed&&d[1].x==-10&&d[1].y==20&&d[1].bad==0);
    /* The arrangement with the places put in: the words as they were, a placed screen's word takes its place, the
       screens the arrangement does not name follow in the card's order, and a placed screen it does not name comes
       last. A screen the arrangement holds dark stays dark. */
    dline("Virtual-2 x=-1920 y=200",&list[0]);
    n=1;
    made=windows_arrange_pinned("",names,2,list,n,pinned,sizeof(pinned));
    co_check(made&&!strcmp(pinned,"Virtual-1 Virtual-2@-1920,200"));
    co_check(windows_arrange_lay(pinned,names,sizes,2,rects,order)==2&&co_rect(rects[0],1920,0,2560,1080)&&
             co_rect(rects[1],0,200,1920,1080)&&order[0]==0);
    made=windows_arrange_pinned("Virtual-2 Virtual-1",names,2,list,n,pinned,sizeof(pinned));
    co_check(made&&!strcmp(pinned,"Virtual-2@-1920,200 Virtual-1"));
    co_check(windows_arrange_lay(pinned,names,sizes,2,rects,order)==2&&order[0]==1);
    dline("Virtual-1 x=0 y=0",&list[0]);
    made=windows_arrange_pinned("Virtual-1@off",names,2,list,n,pinned,sizeof(pinned));
    co_check(made&&!strcmp(pinned,"Virtual-1@off Virtual-2"));
    co_check(windows_arrange_lay(pinned,names,sizes,2,rects,order)==1&&rects[0][2]==0&&co_rect(rects[1],0,0,1920,1080));
    /* A screen not plugged in is no word; default is the card's order; an arrangement that is not one is none. */
    dline("HDMI-A-1 x=5 y=5",&list[0]);
    made=windows_arrange_pinned("default",names,2,list,1,pinned,sizeof(pinned));
    co_check(made&&!strcmp(pinned,"Virtual-1 Virtual-2"));
    made=windows_arrange_pinned("Virtual-2@x",names,2,list,1,pinned,sizeof(pinned));
    co_check(made&&!strcmp(pinned,"Virtual-1 Virtual-2"));
    dline("Virtual-1 x=0 y=0",&list[0]);
    made=windows_arrange_pinned("Virtual-2@x",names,2,list,1,pinned,sizeof(pinned));
    co_check(made&&!strcmp(pinned,"Virtual-2 Virtual-1@0,0"));
    co_check(windows_arrange_pinned("Virtual-2 Virtual-1",names,2,list,1,pinned,4)==0);
    /* The refresh: the first mode of the screen at its size and rate; a rate it does not list is none. */
    { static const unsigned int modes[3][3]={{1920,1080,60},{1920,1080,50},{1280,720,60}};
      co_check(windows_mode_at(modes,3,1920,1080,50)==1);
      co_check(windows_mode_at(modes,3,1920,1080,60)==0);
      co_check(windows_mode_at(modes,3,1280,720,50)==3);
      co_check(windows_mode_at(modes,3,1280,1024,60)==3); }
    /* The rotation: the card's rotate-DEGREES enum is its bit; a card that lists none rotates in software. */
    { static const char *const listed[]={"rotate-0","rotate-180","reflect-x"};
      static const unsigned long bits[]={0,2,4};
      static const char *const more[]={"rotate-0","rotate-90","rotate-180","rotate-270"};
      static const unsigned long more_bits[]={0,1,2,3};
      co_check(windows_rotation_bit(listed,bits,3,180)==4);
      co_check(windows_rotation_bit(listed,bits,3,90)==0);
      co_check(windows_rotation_bit(listed,bits,3,270)==0);
      co_check(windows_rotation_bit(more,more_bits,4,90)==2);
      co_check(windows_rotation_bit(more,more_bits,4,270)==8);
      co_check(windows_rotation_bit(more,more_bits,4,45)==0);
      co_check(windows_rotation_bit(more,more_bits,4,0)==1); }
    /* Software rotation: a 3 by 2 picture turned for the panel, 90 clockwise to 2 by 3, 270 anti-clockwise, 180 both ways. */
    { unsigned int pic[2][3]={{1,2,3},{4,5,6}}, quarter[3][2]={{0}}, anti[3][2]={{0}}, turned[2][3]={{0}}, panel[4];
      windows_rotate_u32(&quarter[0][0],2,&pic[0][0],3,3,2,90,0,0,3,2);
      co_check(quarter[0][0]==4&&quarter[0][1]==1&&quarter[1][0]==5&&quarter[1][1]==2&&quarter[2][0]==6&&quarter[2][1]==3);
      windows_rotate_u32(&anti[0][0],2,&pic[0][0],3,3,2,270,0,0,3,2);
      co_check(anti[0][0]==3&&anti[0][1]==6&&anti[1][0]==2&&anti[1][1]==5&&anti[2][0]==1&&anti[2][1]==4);
      windows_rotate_u32(&turned[0][0],3,&pic[0][0],3,3,2,180,0,0,3,2);
      co_check(turned[0][0]==6&&turned[0][2]==4&&turned[1][0]==3&&turned[1][2]==1);
      /* A part of the picture turns into the part of the panel it lands on, and the panel rectangle says where. */
      windows_rotate_panel(3,2,90,0,0,3,2,panel);
      co_check(panel[0]==0&&panel[1]==0&&panel[2]==2&&panel[3]==3);
      windows_rotate_panel(3,2,180,2,0,3,1,panel);
      co_check(panel[0]==0&&panel[1]==1&&panel[2]==1&&panel[3]==2);
      memset(quarter,0,sizeof(quarter));
      windows_rotate_u32(&quarter[0][0],2,&pic[0][0],3,3,2,90,2,0,3,1);
      co_check(quarter[2][1]==3&&quarter[0][0]==0&&quarter[0][1]==0&&quarter[1][0]==0&&quarter[1][1]==0&&quarter[2][0]==0); }
}

/* The `canvas display` verb's work on the displays file: each word judged as the reader judges its field, and a run of
   changes to one file, each keeping every other line as it was, changing only the named field, dropping a screen left
   with nothing set, and reading back to the same settings. */
static void check_compositor_display_verb(void) {
    static const char start[]="HDMI-A-1 rotation=90 refresh=60\n\nVirtual-2 x=-10 y=20 rotation=180\nbad_name x=1 y=2\n";
    struct windows_display change, list[WINDOWS_DISPLAYS];
    char file[1024], next[1024], big[1024], few[10];
    size_t size = 0;
    int value, made;
    unsigned int n;

    /* The words: rotation 0, 90, 180 or 270 and no other; refresh preferred or 1 to 1000 (0 is no rate); a place to 32768 either way. */
    co_check(windows_display_value(WINDOWS_DISPLAY_ROTATION,"90",2,&value)&&value==90);
    co_check(windows_display_value(WINDOWS_DISPLAY_ROTATION,"0",1,&value)&&value==0);
    co_check(!windows_display_value(WINDOWS_DISPLAY_ROTATION,"45",2,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_ROTATION,"left",4,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_ROTATION,"-90",3,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_ROTATION,"",0,&value));
    co_check(windows_display_value(WINDOWS_DISPLAY_REFRESH,"preferred",9,&value)&&value==0);
    co_check(windows_display_value(WINDOWS_DISPLAY_REFRESH,"60",2,&value)&&value==60);
    co_check(windows_display_value(WINDOWS_DISPLAY_REFRESH,"1000",4,&value)&&value==1000);
    co_check(!windows_display_value(WINDOWS_DISPLAY_REFRESH,"0",1,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_REFRESH,"1001",4,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_REFRESH,"Preferred",9,&value));
    co_check(windows_display_value(WINDOWS_DISPLAY_X,"-32768",6,&value)&&value==-32768);
    co_check(!windows_display_value(WINDOWS_DISPLAY_X,"32769",5,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_Y,"4x",2,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_Y,"",0,&value));
    /* The scale: the six presets in percent, and no other number (a percent sign or a blank is not one). */
    co_check(windows_display_value(WINDOWS_DISPLAY_SCALE,"50",2,&value)&&value==50);
    co_check(windows_display_value(WINDOWS_DISPLAY_SCALE,"125",3,&value)&&value==125);
    co_check(windows_display_value(WINDOWS_DISPLAY_SCALE,"200",3,&value)&&value==200);
    co_check(!windows_display_value(WINDOWS_DISPLAY_SCALE,"60",2,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_SCALE,"100%",4,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_SCALE,"",0,&value));
    co_check(!windows_display_value(WINDOWS_DISPLAY_SCALE,"1.5",3,&value));

    /* Virtual-1, which the file does not have, is turned to 270: its line comes last, every other line is as it was. */
    memset(&change,0,sizeof(change));
    change.rotation=270;
    made=windows_displays_set(file,sizeof(file),start,"Virtual-1",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made>0&&made==(int)strlen(file)&&!strcmp(file,"HDMI-A-1 rotation=90 refresh=60\n\nVirtual-2 x=-10 y=20 rotation=180\nbad_name x=1 y=2\nVirtual-1 rotation=270 refresh=preferred\n"));

    /* A screen's scale is set the same way: its line keeps its other fields, and 100, the default, is not written back.
       Set in a file of its own, from start, so the file the rows below go on from keeps Virtual-1's line. */
    { char scaled[sizeof(file)];
    memset(&change,0,sizeof(change));
    change.scale=150;
    made=windows_displays_set(scaled,sizeof(scaled),start,"Virtual-2",WINDOWS_DISPLAY_SCALE,&change);
    co_check(made>0&&made==(int)strlen(scaled)&&!strcmp(scaled,"HDMI-A-1 rotation=90 refresh=60\n\nVirtual-2 rotation=180 refresh=preferred scale=150 x=-10 y=20\nbad_name x=1 y=2\n"));
    change.scale=100;
    made=windows_displays_set(next,sizeof(next),scaled,"Virtual-2",WINDOWS_DISPLAY_SCALE,&change);
    co_check(made>0&&made==(int)strlen(next)&&!strcmp(next,"HDMI-A-1 rotation=90 refresh=60\n\nVirtual-2 rotation=180 refresh=preferred x=-10 y=20\nbad_name x=1 y=2\n")); }

    /* HDMI-A-1 goes to its preferred rate: its rotation stays, and its line stays where it was. */
    memset(&change,0,sizeof(change));
    made=windows_displays_set(next,sizeof(next),file,"HDMI-A-1",WINDOWS_DISPLAY_REFRESH,&change);
    co_check(made>0&&!strcmp(next,"HDMI-A-1 rotation=90 refresh=preferred\n\nVirtual-2 x=-10 y=20 rotation=180\nbad_name x=1 y=2\nVirtual-1 rotation=270 refresh=preferred\n"));
    strcpy(file,next);

    /* Virtual-2 is placed at 5,6: its turn stays, and both of its place's numbers are written. */
    memset(&change,0,sizeof(change));
    change.x=5; change.y=6;
    made=windows_displays_set(next,sizeof(next),file,"Virtual-2",WINDOWS_DISPLAY_X|WINDOWS_DISPLAY_Y,&change);
    co_check(made>0&&!strcmp(next,"HDMI-A-1 rotation=90 refresh=preferred\n\nVirtual-2 rotation=180 refresh=preferred x=5 y=6\nbad_name x=1 y=2\nVirtual-1 rotation=270 refresh=preferred\n"));
    strcpy(file,next);

    /* Virtual-1 turned back to 0 with nothing else set is a screen with nothing set: its line goes. */
    memset(&change,0,sizeof(change));
    made=windows_displays_set(next,sizeof(next),file,"Virtual-1",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made>0&&!strcmp(next,"HDMI-A-1 rotation=90 refresh=preferred\n\nVirtual-2 rotation=180 refresh=preferred x=5 y=6\nbad_name x=1 y=2\n"));
    strcpy(file,next);

    /* Virtual-2 turned to 90 keeps its place. */
    memset(&change,0,sizeof(change));
    change.rotation=90;
    made=windows_displays_set(next,sizeof(next),file,"Virtual-2",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made>0&&!strcmp(next,"HDMI-A-1 rotation=90 refresh=preferred\n\nVirtual-2 rotation=90 refresh=preferred x=5 y=6\nbad_name x=1 y=2\n"));
    strcpy(file,next);

    /* HDMI-A-1 turned to 180 in the file that has its refresh at 60 keeps the 60. */
    memset(&change,0,sizeof(change));
    change.rotation=180;
    made=windows_displays_set(next,sizeof(next),start,"HDMI-A-1",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made>0&&!strcmp(next,"HDMI-A-1 rotation=180 refresh=60\n\nVirtual-2 x=-10 y=20 rotation=180\nbad_name x=1 y=2\n"));

    /* Read back, the file gives the same settings, and a write of them is the lines that have a setting. */
    n=windows_displays_read(file,list,WINDOWS_DISPLAYS);
    co_check(n==3&&!strcmp(list[0].name,"HDMI-A-1")&&list[0].rotation==90&&list[0].refresh==0&&!list[0].placed&&list[0].bad==0);
    co_check(!strcmp(list[1].name,"Virtual-2")&&list[1].rotation==90&&list[1].refresh==0&&list[1].placed&&list[1].x==5&&list[1].y==6&&list[1].bad==0);
    co_check(list[2].name[0]==0&&list[2].bad==WINDOWS_DISPLAY_NAME);
    made=windows_displays_write(next,sizeof(next),list,n);
    co_check(made>0&&!strcmp(next,"HDMI-A-1 rotation=90 refresh=preferred\nVirtual-2 rotation=90 refresh=preferred x=5 y=6\n"));

    /* A name that is no connector's, a room too small, and a file of 32 screens without the one named are refused. */
    memset(&change,0,sizeof(change));
    change.rotation=180;
    co_check(windows_displays_set(next,sizeof(next),file,"",WINDOWS_DISPLAY_ROTATION,&change)==-1);
    co_check(windows_displays_set(next,sizeof(next),file,"Virtual-1@",WINDOWS_DISPLAY_ROTATION,&change)==-1);
    co_check(windows_displays_set(next,sizeof(next),file,"ABCDEFGHIJKLMNOP",WINDOWS_DISPLAY_ROTATION,&change)==-1);
    co_check(windows_displays_set(few,sizeof(few),file,"Virtual-2",WINDOWS_DISPLAY_ROTATION,&change)==-1);
    for (int each = 0; each < WINDOWS_DISPLAYS; each++)
        size += (size_t)snprintf(big + size, sizeof(big) - size, "S-%d rotation=90\n", each);
    co_check(windows_displays_set(next,sizeof(next),big,"Virtual-9",WINDOWS_DISPLAY_ROTATION,&change)==-1);
    made=windows_displays_set(next,sizeof(next),big,"S-5",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made>0&&strstr(next,"S-4 rotation=90\nS-5 rotation=180 refresh=preferred\nS-6 rotation=90\n")!=0&&!strstr(next,"S-5 rotation=90"));

    /* An empty file takes a setting as its only line, and a default write of an empty file is no line at all. */
    memset(&change,0,sizeof(change));
    change.rotation=90;
    made=windows_displays_set(next,sizeof(next),"","Virtual-1",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made>0&&!strcmp(next,"Virtual-1 rotation=90 refresh=preferred\n"));
    memset(&change,0,sizeof(change));
    made=windows_displays_set(next,sizeof(next),"","Virtual-1",WINDOWS_DISPLAY_ROTATION,&change);
    co_check(made==0&&next[0]==0);

    /* A screen given twice: the first line takes the change, the later one goes, and the other lines are as they were. */
    memset(&change,0,sizeof(change));
    change.refresh=60;
    made=windows_displays_set(next,sizeof(next),"eDP-1 rotation=90\nVirtual-2 x=0 y=0\neDP-1 rotation=180\n","eDP-1",WINDOWS_DISPLAY_REFRESH,&change);
    co_check(made>0&&!strcmp(next,"eDP-1 rotation=90 refresh=60\nVirtual-2 x=0 y=0\n"));
}

static void check_compositor_outputs(void) {
    char name[WINDOWS_OUTPUT_NAME];
    int rects[WINDOWS_OUTPUTS][4];
    unsigned char order[WINDOWS_OUTPUTS];
    /* A connector is named as the kernel names it. */
    compositor_connector_name(15,2,name);co_check(!strcmp(name,"Virtual-2"));
    compositor_connector_name(11,1,name);co_check(!strcmp(name,"HDMI-A-1"));
    compositor_connector_name(14,1,name);co_check(!strcmp(name,"eDP-1"));
    compositor_connector_name(10,12,name);co_check(!strcmp(name,"DP-12"));
    compositor_connector_name(99,3,name);co_check(!strcmp(name,"Unknown-3"));
    compositor_connector_name(5,4000000000u,name);co_check(strlen(name)<WINDOWS_OUTPUT_NAME&&!strncmp(name,"Composite-",10));
    /* The words: a name, NAME@X,Y, NAME@off, default alone, each name once. */
    static const char *const valid[]={"","default"," default ","Virtual-2","Virtual-2 Virtual-1","DP-1@0,0","DP-1@-1920,40",
        "DP-1@off eDP-1","a@32768,-32768","\tx\ny\r"};
    static const char *const invalid[]={"Virtual-2@","Virtual-2@1","Virtual-2@1,","Virtual-2@1,2x","@1,2","a b a","default x",
        "x default","abcdefghijklmnop","a@32769,0","a@1,,2","a@of","a_b","a@--1,2","a@1,2,3"};
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++)co_check(windows_arrange_valid(valid[i]));
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)co_check(!windows_arrange_valid(invalid[i]));
    { unsigned int length;int how,x,y;
      co_check(windows_arrange_word("DP-1@-1920,40",13,&length,&how,&x,&y)&&length==4&&how==WINDOWS_PLACE_AT&&x==-1920&&y==40);
      co_check(windows_arrange_word("eDP-1@off",9,&length,&how,&x,&y)&&length==5&&how==WINDOWS_PLACE_OFF);
      co_check(windows_arrange_word("eDP-1",5,&length,&how,&x,&y)&&length==5&&how==WINDOWS_PLACE_FLOW); }
    /* Left to right in the card's order, tops level, the first the primary. */
    co_check(co_lay("",rects,order)==2&&co_rect(rects[0],0,0,2560,1080)&&co_rect(rects[1],2560,0,1920,1080)&&order[0]==0&&order[1]==1);
    co_check(co_lay("default",rects,order)==2&&co_rect(rects[1],2560,0,1920,1080)&&order[0]==0);
    /* Named first, in the setting's order: the named one is the primary, on the left. */
    co_check(co_lay("Virtual-2 Virtual-1",rects,order)==2&&co_rect(rects[1],0,0,1920,1080)&&co_rect(rects[0],1920,0,2560,1080)&&
             order[0]==1&&order[1]==0);
    co_check(co_lay("Virtual-2",rects,order)==2&&co_rect(rects[1],0,0,1920,1080)&&co_rect(rects[0],1920,0,2560,1080)&&order[0]==1);
    /* A name no screen has is a screen not plugged in now. */
    co_check(co_lay("HDMI-A-1 Virtual-2",rects,order)==2&&order[0]==1&&co_rect(rects[0],1920,0,2560,1080));
    /* Dark: off, and the rest close up; every screen off lights the first. */
    co_check(co_lay("Virtual-2@off",rects,order)==1&&co_rect(rects[0],0,0,2560,1080)&&co_rect(rects[1],0,0,0,0)&&order[0]==0);
    co_check(co_lay("Virtual-1@off",rects,order)==1&&co_rect(rects[1],0,0,1920,1080)&&rects[0][2]==0&&order[0]==1);
    co_check(co_lay("Virtual-1@off Virtual-2@off",rects,order)==1&&co_rect(rects[0],0,0,2560,1080)&&rects[1][2]==0&&order[0]==0);
    /* A place given is kept, the space moved so its top left is 0, 0. */
    co_check(co_lay("Virtual-1@0,0 Virtual-2@0,-1080",rects,order)==2&&co_rect(rects[0],0,1080,2560,1080)&&
             co_rect(rects[1],0,0,1920,1080)&&order[0]==0);
    co_check(co_lay("Virtual-2@-1920,200",rects,order)==2&&co_rect(rects[1],0,200,1920,1080)&&co_rect(rects[0],1920,0,2560,1080));
    /* Words that are not an arrangement are the default. */
    co_check(co_lay("Virtual-2@x",rects,order)==2&&co_rect(rects[0],0,0,2560,1080)&&order[0]==0);
    co_check(co_lay("Virtual-2 Virtual-2",rects,order)==2&&co_rect(rects[0],0,0,2560,1080)&&order[0]==0);

    /* The pointer: A 2560x1080 and, to its right, B taller at 1920x1200. */
    static const int two[2][4]={{0,0,2560,1080},{2560,0,1920,1200}};
    int x,y;
    x=100,y=100;compositor_screen_keep_point(two,2,90,90,&x,&y);co_check(x==100&&y==100);
    x=2570,y=500;compositor_screen_keep_point(two,2,2550,500,&x,&y);co_check(x==2570&&y==500); /* across where they meet */
    x=2550,y=1150;compositor_screen_keep_point(two,2,2570,1150,&x,&y);co_check(x==2560&&y==1150); /* beside A's bottom: stops */
    x=2570,y=1090;compositor_screen_keep_point(two,2,2550,1070,&x,&y);co_check(x==2570&&y==1090); /* onto B's taller part */
    x=-50,y=100;compositor_screen_keep_point(two,2,10,100,&x,&y);co_check(x==0&&y==100);
    x=9000,y=1300;compositor_screen_keep_point(two,2,4000,1100,&x,&y);co_check(x==4479&&y==1199);
    /* From nowhere (a tablet's point): the nearest point of the nearest screen, the lower index of two as near. */
    x=2000,y=1150;compositor_screen_keep_point(two,2,-1,-1,&x,&y);co_check(x==2000&&y==1079);
    x=2540,y=1150;compositor_screen_keep_point(two,2,-1,-1,&x,&y);co_check(x==2560&&y==1150);
    { static const int gap[2][4]={{0,0,1000,1000},{1100,0,1000,1000}},even[2][4]={{0,0,1000,1000},{1101,0,1000,1000}};
      x=1050,y=500;compositor_screen_keep_point(gap,2,990,500,&x,&y);co_check(x==999&&y==500); /* a gap: stops */
      x=1150,y=500;compositor_screen_keep_point(gap,2,990,500,&x,&y);co_check(x==1150&&y==500); /* ends on B: there */
      x=1049,y=500;compositor_screen_keep_point(gap,2,-5,-5,&x,&y);co_check(x==999&&y==500);
      x=1051,y=500;compositor_screen_keep_point(gap,2,-5,-5,&x,&y);co_check(x==1100&&y==500);
      x=1050,y=500;compositor_screen_keep_point(even,2,-5,-5,&x,&y);co_check(x==999&&y==500); }
    co_check(compositor_screen_at(two,2,2559,1079)==0&&compositor_screen_at(two,2,2560,1199)==1&&compositor_screen_at(two,2,2559,1080)==-1&&
             compositor_screen_at(two,2,4480,0)==-1);
    /* A window's screen: the most of it, the lower index on a tie, the nearest when none holds any. */
    co_check(compositor_screen_owner(two,2,2400,100,400,300)==1&&compositor_screen_owner(two,2,2300,100,400,300)==0&&
             compositor_screen_owner(two,2,2360,100,400,300)==0&&compositor_screen_owner(two,2,2000,1100,100,50)==0&&
             compositor_screen_owner(two,2,4600,1300,100,50)==1);
    { int box[4];compositor_screens_box(two,2,box);co_check(co_rect(box,0,0,4480,1200));
      static const int shifted[2][4]={{100,50,10,10},{-20,70,5,100}};compositor_screens_box(shifted,2,box);co_check(co_rect(box,-20,50,130,120)); }
    /* Kept reachable: on one screen exactly the old rule (all of it when it fits, its top left when not), line inside,
       but on an axis where the window is exactly the screen's size: there the line has no room and is not kept (df950770). */
    for(int seed=1;seed<4000;seed++) {
        int w=1+(seed*7919)%3000,h=1+(seed*104729)%1500,line=seed&1,wx=(seed*31337)%6000-3000,wy=(seed*7177)%3000-1500;
        static const int one[1][4]={{0,0,2560,1080}};
        int kx=wx,ky=wy,old_x=wx,old_y=wy,lx=w==2560?0:line,ly=h==1080?0:line;
        compositor_screen_keep_window(one,1,&kx,&ky,w,h,line,24);
        old_x=old_x>2560-w-lx?2560-w-lx:old_x;old_y=old_y>1080-h-ly?1080-h-ly:old_y;
        old_x=old_x<lx?lx:old_x;old_y=old_y<ly?ly:old_y;
        if(kx!=old_x||ky!=old_y){co_check(0);break;}
    }
    x=2450,y=100;compositor_screen_keep_window(two,2,&x,&y,400,300,1,24);co_check(x==2450&&y==100); /* across both: left alone */
    x=2000,y=1100;compositor_screen_keep_window(two,2,&x,&y,100,60,1,24);co_check(x==2000&&y==1019); /* top in no screen: onto A */
    x=2450,y=1100;compositor_screen_keep_window(two,2,&x,&y,200,60,1,24);co_check(x==2450&&y==1100); /* top on B for 90 px */
    x=2545,y=1100;compositor_screen_keep_window(two,2,&x,&y,30,60,1,24);co_check(x==2561&&y==1100); /* 15 of 24 on B: onto B */
    /* Snap: a window's rectangle against its screen, never the pointer. A seam between two screens is no edge of either;
       the rule touches an edge at 0 px, holds it inside the dead zone, and a corner's quarter wins over the edge it meets. */
    { static const int two_b[2][4]={{0,0,2560,1080},{2560,0,1920,1080}};
      static const int one_s[1][4]={{0,0,2560,1080}};
      const unsigned int all = COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT | COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM;
      int box[4] = {2400, 100, 400, 300}, touch = 0;
      co_check(compositor_snap_free(two_b,2,box,0)==(all&~COMPOSITOR_SNAP_RIGHT));
      co_check(compositor_snap_free(two_b,2,box,1)==(all&~COMPOSITOR_SNAP_LEFT));
      box[1] = 1500;
      co_check(compositor_snap_free(two_b,2,box,0)==all && compositor_snap_free(two_b,2,box,1)==all);
      box[0] = 0, box[1] = 500;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_LEFT && touch==COMPOSITOR_SNAP_LEFT);
      box[0] = 1;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_FLOATING && touch==0);
      box[0] = 7;
      co_check(compositor_snap_rule(box,one_s[0],all,COMPOSITOR_SNAP_LEFT,8,16,&touch)==COMPOSITOR_LEFT && touch==COMPOSITOR_SNAP_LEFT);
      box[0] = 8;
      co_check(compositor_snap_rule(box,one_s[0],all,COMPOSITOR_SNAP_LEFT,8,16,&touch)==COMPOSITOR_FLOATING && touch==0);
      box[0] = 7;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_FLOATING && touch==0);
      box[0] = 15, box[1] = 0;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_TOP_LEFT && touch==COMPOSITOR_SNAP_TOP);
      box[0] = 16;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_MAXIMIZED && touch==COMPOSITOR_SNAP_TOP);
      box[0] = 0, box[1] = 0, box[2] = 2560;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_MAXIMIZED);
      box[0] = 2160, box[1] = 1020, box[2] = 400, box[3] = 60;
      co_check(compositor_snap_rule(box,one_s[0],all,0,8,16,&touch)==COMPOSITOR_BOTTOM_RIGHT && touch==(COMPOSITOR_SNAP_RIGHT|COMPOSITOR_SNAP_BOTTOM));
      /* The bottom is a seam: the corner is no corner, and the right side alone is that half. */
      co_check(compositor_snap_rule(box,one_s[0],all&~COMPOSITOR_SNAP_BOTTOM,0,8,16,&touch)==COMPOSITOR_RIGHT && touch==COMPOSITOR_SNAP_RIGHT);
    }
    /* Every target at 1x and at 2x: a band is 8 logical pixels, so 16 device pixels at 2x; a corner is its quarter, and
       each target's rectangle is the screen's, halved as it says. */
    for (int scale = 1; scale <= 2; scale++)
    {
      int w = 1920 * scale, h = 1080 * scale, m = 8 * scale, at, part[4];
      static int big[1][4];
      big[0][0] = 0, big[0][1] = 0, big[0][2] = w, big[0][3] = h;
      /* The rule at this scale: an edge touched at 0 px, held to m px inside, and a corner whose edges are within 2m px. */
      {
        const unsigned int all = COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT | COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM;
        int box[4] = {0, h / 3, w / 4, h / 4}, touch = 0;

        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_LEFT && touch==COMPOSITOR_SNAP_LEFT);
        box[0] = 1;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_FLOATING && touch==0);
        box[0] = m - 1;
        co_check(compositor_snap_rule(box,big[0],all,COMPOSITOR_SNAP_LEFT,m,2*m,&touch)==COMPOSITOR_LEFT);
        box[0] = m;
        co_check(compositor_snap_rule(box,big[0],all,COMPOSITOR_SNAP_LEFT,m,2*m,&touch)==COMPOSITOR_FLOATING);
        box[0] = w - w / 4, box[1] = h / 3;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_RIGHT);
        box[0] = 0, box[1] = 0;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_TOP_LEFT);
        box[0] = w - w / 4, box[1] = 0;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_TOP_RIGHT);
        box[0] = 0, box[1] = h - h / 4;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_BOTTOM_LEFT);
        box[0] = w - w / 4, box[1] = h - h / 4;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_BOTTOM_RIGHT);
        box[0] = 2*m - 1, box[1] = 0;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_TOP_LEFT);
        box[0] = 2*m, box[1] = 0;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_MAXIMIZED);
        box[0] = 2*m - 1, box[1] = 2*m - 1;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_FLOATING && touch==0);
        box[0] = w / 2 - w / 8, box[1] = h / 2 - h / 8;
        co_check(compositor_snap_rule(box,big[0],all,0,m,2*m,&touch)==COMPOSITOR_FLOATING);
      }
      compositor_screen_part(big[0],COMPOSITOR_MAXIMIZED,part);co_check(co_rect(part,0,0,w,h));
      compositor_screen_part(big[0],COMPOSITOR_LEFT,part);co_check(co_rect(part,0,0,w/2,h));
      compositor_screen_part(big[0],COMPOSITOR_RIGHT,part);co_check(co_rect(part,w/2,0,w-w/2,h));
      compositor_screen_part(big[0],COMPOSITOR_BOTTOM,part);co_check(co_rect(part,0,h/2,w,h-h/2));
      compositor_screen_part(big[0],COMPOSITOR_TOP_LEFT,part);co_check(co_rect(part,0,0,w/2,h/2));
      compositor_screen_part(big[0],COMPOSITOR_TOP_RIGHT,part);co_check(co_rect(part,w/2,0,w-w/2,h/2));
      compositor_screen_part(big[0],COMPOSITOR_BOTTOM_LEFT,part);co_check(co_rect(part,0,h/2,w/2,h-h/2));
      compositor_screen_part(big[0],COMPOSITOR_BOTTOM_RIGHT,part);co_check(co_rect(part,w/2,h/2,w-w/2,h-h/2));
    }
    /* What an arrangement fills of a screen. */
    { int part[4];
      compositor_screen_part(two[1],COMPOSITOR_MAXIMIZED,part);co_check(co_rect(part,2560,0,1920,1200));
      compositor_screen_part(two[1],COMPOSITOR_LEFT,part);co_check(co_rect(part,2560,0,960,1200));
      compositor_screen_part(two[1],COMPOSITOR_RIGHT,part);co_check(co_rect(part,3520,0,960,1200));
      static const int odd[4]={10,20,101,50};
      compositor_screen_part(odd,COMPOSITOR_LEFT,part);co_check(co_rect(part,10,20,50,50));
      compositor_screen_part(odd,COMPOSITOR_RIGHT,part);co_check(co_rect(part,60,20,51,50)); }
    /* The cursor plane is a crtc's: the cell shows on each screen it meets, at its place on that screen, and on no other. */
    { int cx,cy;
      co_check(compositor_screen_cell(two[0],2500,100,64,&cx,&cy)&&cx==2500&&cy==100&&compositor_screen_cell(two[1],2500,100,64,&cx,&cy)&&cx==-60&&cy==100);
      co_check(compositor_screen_cell(two[0],2400,100,64,&cx,&cy)&&!compositor_screen_cell(two[1],2400,100,64,&cx,&cy));
      co_check(!compositor_screen_cell(two[0],2560,100,64,&cx,&cy)&&compositor_screen_cell(two[1],2560,100,64,&cx,&cy)&&cx==0);
      co_check(compositor_screen_cell(two[0],2496,1016,64,&cx,&cy)&&!compositor_screen_cell(two[0],2496,1080,64,&cx,&cy)&&
               !compositor_screen_cell(two[0],-64,0,64,&cx,&cy)&&compositor_screen_cell(two[0],-63,0,64,&cx,&cy)); }
    /* A change is told to each screen's framebuffer for what of it is on that screen: one on the first alone is the
       first's, one across the edge is each screen's part, one under the shorter screen is the taller's, and one that is
       on no screen is told to none -- so a DIRTYFB waits on the vertical blanks of the screens that changed only. */
    { static const unsigned short change[4][4]={{100,100,500,400},{2400,50,2700,300},{3000,1150,3100,1190},{2000,1100,2100,1150}};
      unsigned short cut[4][4];
      co_check(compositor_screen_clips(two[0],change,4,cut)==2&&cut[0][0]==100&&cut[0][2]==500&&cut[0][3]==400&&
               cut[1][0]==2400&&cut[1][2]==2560&&cut[1][1]==50&&cut[1][3]==300);
      co_check(compositor_screen_clips(two[1],change,4,cut)==2&&cut[0][0]==2560&&cut[0][2]==2700&&cut[1][0]==3000&&
               cut[1][1]==1150&&cut[1][3]==1190);
      co_check(compositor_screen_clips(two[1],change,1,cut)==0&&compositor_screen_clips(two[0],change+3,1,cut)==0); }
    /* Carried: a screen that only moved takes its windows along; another size keeps the share of the room. */
    { static const int was[4]={2560,0,1920,1080},moved[4]={0,0,1920,1080},bigger[4]={0,0,2560,1080};
      x=2560+700,y=390;compositor_screen_carry(was,moved,&x,&y,400,300);co_check(x==700&&y==390);
      x=2560+760,y=390;compositor_screen_carry(was,bigger,&x,&y,400,300);co_check(x==1080&&y==390);
      x=2560-50,y=-10;compositor_screen_carry(was,bigger,&x,&y,400,300);co_check(x==0&&y==0);
      x=4000,y=900;compositor_screen_carry(was,bigger,&x,&y,400,300);co_check(x==2046&&y==780);
      x=4100,y=900;compositor_screen_carry(was,bigger,&x,&y,400,300);co_check(x==2160&&y==780);
      x=3000,y=10;compositor_screen_carry(was,moved,&x,&y,1920,300);co_check(x==0&&y==10);
      x=3000,y=10;compositor_screen_carry(bigger,moved,&x,&y,2000,300);co_check(x==0); }
}

/* The scene is the painter's picture: surfaces drawn bottom to top over a
   desktop, inside the damage, and no pixel outside it. Random surfaces (some
   off the screen, some with round corners of every radius up to past half
   their smaller side, some square, some with a line round them of up to
   three pixels, some one over another, some the panel),
   random damage, byte for byte -- the corners' scratch included. */
static void check_compositor_scene(void) {
    enum { W=200, H=150 };
    static u32 seen[W*H], want[W*H], body[COMPOSITOR_ROWS_FIRST][96*80];
    static const unsigned char marks[COMPOSITOR_ROWS_FIRST]={1,0,2,0,1,0,0,2,0,0,1,0};
    u64 seed=0x9e3779b97f4a7c15ull;
    #define RND(n) (seed^=seed<<13,seed^=seed>>7,seed^=seed<<17,(int)(seed%(u64)(n)))
    int equal=1;
    for(int scene=0;scene<4000;scene++) {
        static struct compositor_frame frames[COMPOSITOR_FRAMES_FOR(COMPOSITOR_ROWS_FIRST)];
        unsigned count=scene%61==60?COMPOSITOR_ROWS_FIRST:(unsigned)RND(7);
        for(unsigned i=0;i<count;i++) {
            int w=1+RND(96),h=1+RND(80);
            for(int p=0;p<w*h;p++)body[i][p]=0x400000u|(u32)(i<<16)|(u32)p;
            frames[i]=(struct compositor_frame){RND(W+60)-40,RND(H+60)-40,w,h,1+RND(2),
                body[i],(unsigned long)w,0,0,0,RND(3)?RND(60):0,RND(3)?RND(4):0,0x203040u+(u32)i,RND(4)?0:RND(16)};
            /* An edge, a corner or all of its line lit, now and then: drawn in pieces as it is drawn whole. */
            frames[i].lit=RND(3)?0:(unsigned)RND(32);
            frames[i].lit_ink=0xe4e4e4u-(u32)i;
        }
        /* The compositor's panel, somewhere among them, as wide as it likes. */
        if(count<COMPOSITOR_FRAMES_FOR(COMPOSITOR_ROWS_FIRST)&&scene%3==0) {
            unsigned at=(unsigned)RND(count+1);
            for(unsigned i=count;i>at;i--)frames[i]=frames[i-1];
            frames[at]=(struct compositor_frame){RND(W)-20,RND(H)-10,40+RND(W),12+RND(30),1+RND(2),0,0,1,marks,
                (unsigned)RND(sizeof(marks)+1)};
            count++;
        }
        int x1=RND(W),y1=RND(H),x2=x1+1+RND(W),y2=y1+1+RND(H);
        if(x2>W)x2=W;
        if(y2>H)y2=H;
        for(int p=0;p<W*H;p++)seen[p]=want[p]=0xdeadbeefu;
        struct compositor_target a={want,W,W,H,x1,y1,x2,y2},b={seen,W,W,H,x1,y1,x2,y2};
        compositor_fill(&a,0,0,W,H,COMPOSITOR_INK_DESKTOP);
        for(unsigned i=0;i<count;i++)compositor_draw(&a,&frames[i]);
        compositor_scene(&b,frames,count);
        equal&=!memcmp(seen,want,sizeof(seen));
    }
    cc_check(equal);
    #undef RND
}

/* What the touch work added to the backend and the pointer's rules, on ordinary memory, by gcc and clang alike:
   a finger's mark, a lit edge and corner, and the edges a point is on. A scale is a percent of the logical size, as
   compositor.scale is since a166974f (the rows took a multiplier, 1 to 4, before it). */
static void check_compositor_touch_backend(void) {
    enum { W=200, H=150 };
    static u32 buffer[W*H];
    struct compositor_target t={buffer,W,W,H,0,0,W,H};
    int symmetric=1,outside=1,inside=1;
    for(int scale=100;scale<=300;scale+=100) {
        int r=compositor_spot_radius(scale),cx=100,cy=75;
        compositor_fill(&t,0,0,W,H,0x303030u);
        compositor_spot_draw(&t,cx,cy,scale,255);
        for(int dy=0;dy<r+2&&cy+dy<H;dy++)for(int dx=0;dx<r+2;dx++) {
            u32 a=buffer[(cy+dy)*W+cx+dx],b=buffer[(cy+dy)*W+cx-1-dx],c=buffer[(cy-1-dy)*W+cx+dx],d=buffer[(cy-1-dy)*W+cx-1-dx];
            symmetric&=a==b&&a==c&&a==d;
            if(dx>=r||dy>=r)outside&=a==0x303030u;
        }
        inside&=buffer[cy*W+cx]==compositor_mix(0x303030u,COMPOSITOR_INK_LINE_HELD,COMPOSITOR_SPOT_FILL);
    }
    cc_check(symmetric&&outside&&inside);
    cc_check(compositor_spot_radius(100)==COMPOSITOR_SPOT&&compositor_spot_radius(400)==COMPOSITOR_SPOT*4);
    compositor_fill(&t,0,0,W,H,0x303030u);
    compositor_spot_draw(&t,100,75,100,0);
    cc_check(buffer[75*W+100]==0x303030u);
    compositor_spot_draw(&t,100,75,100,128);
    cc_check(buffer[75*W+100]==compositor_mix(0x303030u,COMPOSITOR_INK_LINE_HELD,COMPOSITOR_SPOT_FILL*128/255));
    /* A mark at the target's edge writes nothing outside its clip. */
    {
        struct compositor_target clip={buffer,W,W,H,50,50,60,60};
        int clean=1;
        compositor_fill(&t,0,0,W,H,0x303030u);
        compositor_spot_draw(&clip,52,52,100,255);
        for(int y=0;y<H;y++)for(int x=0;x<W;x++)
            if(x<50||x>=60||y<50||y>=60)clean&=buffer[y*W+x]==0x303030u;
        cc_check(clean&&buffer[52*W+52]!=0x303030u);
    }
    /* A window at 50, 50, its line 1 and its corners 8: the left edge lit whole, and the top left corner lit along both
       its edges for the reach, the rest of the line its own ink. */
    {
        static u32 body[120*80];
        struct compositor_frame frame;
        for(int p=0;p<120*80;p++)body[p]=0x404040u;
        frame=(struct compositor_frame){50,50,60,40,100,body,120,0,0,0,8,1,0x4d4d4du,0};
        frame.lit=COMPOSITOR_LIT_LEFT;frame.lit_ink=COMPOSITOR_INK_LINE_HELD;
        compositor_fill(&t,0,0,W,H,0x1e1e1eu);
        compositor_draw(&t,&frame);
        cc_check(buffer[70*W+49]==COMPOSITOR_INK_LINE_HELD&&buffer[49*W+80]==0x4d4d4du&&buffer[70*W+110]==0x4d4d4du&&
                 buffer[90*W+80]==0x4d4d4du);
        frame=(struct compositor_frame){50,50,120,80,100,body,120,0,0,0,8,1,0x4d4d4du,0};
        frame.lit=COMPOSITOR_LIT_LEFT|COMPOSITOR_LIT_TOP;frame.lit_ink=COMPOSITOR_INK_LINE_HELD;
        compositor_fill(&t,0,0,W,H,0x1e1e1eu);
        compositor_draw(&t,&frame);
        cc_check(buffer[(49+8+31)*W+49]==COMPOSITOR_INK_LINE_HELD&&buffer[(49+8+32)*W+49]==0x4d4d4du&&
                 buffer[49*W+49+8+31]==COMPOSITOR_INK_LINE_HELD&&buffer[49*W+49+8+32]==0x4d4d4du&&buffer[100*W+170]==0x4d4d4du);
    }
    /* The edges a point is on: with no corner reach what the compositor always said, and a finger's corner. */
    {
        int same=1;
        for(int y=-20;y<120;y++)for(int x=-20;x<140;x++) {
            unsigned want=0;
            if(!(x<10-7||x>=110+7||y<20-7||y>=80+7)) {
                want|=x<10+2?CP_EDGE_LEFT:x>=110-2?CP_EDGE_RIGHT:0;
                want|=y<20+2?CP_EDGE_TOP:y>=80-2?CP_EDGE_BOTTOM:0;
            }
            same&=compositor_edges_at(x,y,10,20,110,80,7,2,2,0)==want;
        }
        cc_check(same);
        cc_check(compositor_edges_at(5,40,10,20,110,80,17,8,2,24)==(CP_EDGE_LEFT|CP_EDGE_TOP)&&
                 compositor_edges_at(5,50,10,20,110,80,17,8,2,24)==CP_EDGE_LEFT&&
                 compositor_edges_at(100,85,10,20,110,80,17,8,2,24)==(CP_EDGE_RIGHT|CP_EDGE_BOTTOM)&&
                 compositor_edges_at(60,25,10,20,110,80,17,8,2,24)==0&&compositor_edges_at(60,0,10,20,110,80,17,8,2,24)==0);
        cc_check(ui_touch_past(0,0,5,6,8)==0&&ui_touch_past(0,0,0,8,8)==1&&ui_touch_past(10,10,4,4,8)==1);
        cc_check(compositor_builtin_panel("eDP-1")&&compositor_builtin_panel("LVDS-1")&&compositor_builtin_panel("DSI-2")&&
                 !compositor_builtin_panel("DP-1")&&!compositor_builtin_panel("HDMI-A-1")&&!compositor_builtin_panel("Virtual-1"));
    }
}

#if defined(__riscv)
#include <sys/auxv.h>
#endif
static int simd_usable(void) {
#if defined(__x86_64__)
    return __builtin_cpu_supports("avx2");
#elif defined(__riscv)
    return (getauxval(AT_HWCAP) & (1ul<<('V'-'A')))!=0;
#else
    return 1;
#endif
}

/* Every wide body against its narrow one, on the same inputs: the same
   pixels, and nothing written outside the cell, row or run. */
/* The canvas routines these name exist only in the kernel build: lib.c emits them under KERNEL_MODE and CONFIG_MOONWATER_CANVAS. */
#if defined(KERNEL_MODE) && defined(CONFIG_MOONWATER_CANVAS)
static void check_wide_bodies(void) {
    const u32 colors[]={0,1,0x80000000,0xffffffff,0xaabbccdd,0x10203040};
    static u32 got[4096],want[4096];
    for(unsigned a=0;a<6;a++)for(unsigned b=0;b<6;b++)for(unsigned pattern=0;pattern<256;pattern++) {
        unsigned rows=1+pattern%17,pitch=8+(pattern%10),align=(pattern/10)%4;
        u8 bits[17];for(unsigned r=0;r<rows;r++)bits[r]=(pattern+r*17)&255;
        memset(got,0xa5,sizeof(got));memset(want,0xa5,sizeof(want));
        canvas_cell(want+align,pitch,bits,rows,colors[a],colors[b]);
        canvas_cell_wide(got+align,pitch,bits,rows,colors[a],colors[b]);
        check(!memcmp(got,want,sizeof(got)));
        pitch=16+2*(pattern%5);
        memset(got,0xa5,sizeof(got));memset(want,0xa5,sizeof(want));
        canvas_cell2(want+align,pitch,bits,rows,colors[a],colors[b]);
        canvas_cell2_wide(got+align,pitch,bits,rows,colors[a],colors[b]);
        check(!memcmp(got,want,sizeof(got)));
    }
    static u8 face[256*16];
    for(unsigned trial=0;trial<4096;trial++) {
        unsigned count=1+trial%23,pitch=count*8+(trial/23)%5,align=(trial/115)%4;
        struct window_cell run[23];
        u32 ink=colors[trial%6],paper=colors[(trial/6)%6];
        for(unsigned i=0;i<sizeof(face);i++)face[i]=(i*131+trial*7)&255;
        for(unsigned i=0;i<count;i++)
            run[i]=(struct window_cell){(trial*13+i*29)%256,(u8)i,(u8)trial,(unsigned short)i};
        memset(got,0xa5,sizeof(got));memset(want,0xa5,sizeof(want));
        canvas_cells(want+align,pitch,face,run,count,ink,paper);
        canvas_cells_wide(got+align,pitch,face,run,count,ink,paper);
        check(!memcmp(got,want,sizeof(got)));
    }
    /* Rectangles of every narrow width and a spread of wide ones, at four
       alignments and three pitches, and a row with no pitch at all. */
    static u32 big[8*720+8],big_want[8*720+8];
    for(unsigned width=0;width<=700;width+=width<40?1:width<640?29:1)
    for(unsigned rows=0;rows<=3;rows++)for(unsigned align=0;align<4;align++) {
        unsigned pitch=rows<2?0:width+align+rows*3;
        if(rows>=2&&pitch<width)continue;
        memset(big,0xa5,sizeof(big));memset(big_want,0xa5,sizeof(big_want));
        canvas_rect_fill(big_want+align,pitch,width,rows,colors[(width+rows)%6]);
        canvas_rect_fill_wide(big+align,pitch,width,rows,colors[(width+rows)%6]);
        check(!memcmp(big,big_want,sizeof(big)));
    }
    /* Glyphs drawn over a picture, which only the set bits may change. */
    for(unsigned trial=0;trial<2048;trial++) {
        unsigned rows=trial%18,stride=1+trial%3,pitch=8+trial%9,align=trial%4;
        u8 bits[18*3];for(unsigned i=0;i<sizeof(bits);i++)bits[i]=(u8)(trial*37+i*101);
        if(trial%5==0)bits[0]=0;
        for(unsigned i=0;i<4096;i++)want[i]=got[i]=i*2654435761u;
        canvas_glyph(want+align,pitch,bits,stride,rows,colors[trial%6]);
        canvas_glyph_wide(got+align,pitch,bits,stride,rows,colors[trial%6]);
        check(!memcmp(got,want,sizeof(got)));
    }
    /* Scale two glyphs over the same picture: each source row covers two
       rows of pixels, two pixels to a bit, and only the set bits may change. */
    for(unsigned trial=0;trial<2048;trial++) {
        unsigned rows=trial%9,stride=1+trial%3,pitch=16+trial%9,align=trial%4;
        u8 bits[9*3];for(unsigned i=0;i<sizeof(bits);i++)bits[i]=(u8)(trial*53+i*89);
        if(trial%5==0)bits[0]=0;
        if(trial%7==0)bits[1]=0xff;
        for(unsigned i=0;i<4096;i++)want[i]=got[i]=i*2246822519u;
        canvas_glyph2(want+align,pitch,bits,stride,rows,colors[trial%6]);
        canvas_glyph2_wide(got+align,pitch,bits,stride,rows,colors[trial%6]);
        check(!memcmp(got,want,sizeof(got)));
    }
    static u32 source[1100];
    for(unsigned i=0;i<1100;i++)source[i]=i*0x9e3779b9u;
    for(unsigned count=0;count<=1060;count+=count<40?1:count>=1016&&count<1032?1:97)
    for(unsigned mask=0;mask<3;mask++)for(unsigned align=0;align<4;align++) {
        u32 opaque=mask==0?0:mask==1?0xff000000:0x00ff00ff;
        memset(got,0xa5,sizeof(got));memset(want,0xa5,sizeof(want));
        canvas_row_blit(want+align,source+align,count,opaque);
        canvas_row_blit_wide(got+align,source+align,count,opaque);
        check(!memcmp(got,want,sizeof(got)));
    }
}
#endif /* KERNEL_MODE && CONFIG_MOONWATER_CANVAS */

/* The cells a run is drawn from are the program's shared page, and the
   bodies read each character again on every scanline, after the caller
   checked it: a second thread can make it anything by then. Only the low
   byte may reach the face, which here ends at a page nothing is mapped
   behind, so a body reading the whole word faults instead of drawing. */
static void check_character_bounds(int wide) {
    long page=sysconf(_SC_PAGESIZE);
    unsigned char *pages=mmap(NULL,(size_t)page*3,PROT_READ|PROT_WRITE,
                              MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(pages!=MAP_FAILED);
    assert(!mprotect(pages+2*page,(size_t)page,PROT_NONE));
    u8 *face=pages+2*page-256*16;
    for(unsigned i=0;i<256*16;i++)face[i]=(u8)(i*131+i/16*7);
    static u32 got[8*23*16+64],want[8*23*16+64];
    const unsigned high[]={0x100,0x1000,0x10000,0xdead0000,0xffffff00,0x80000000};
    for(unsigned trial=0;trial<6*23;trial++) {
        unsigned count=1+trial%23,pitch=count*8;
        struct window_cell run[23],plain[23];
        for(unsigned i=0;i<count;i++) {
            unsigned character=33+(trial*7+i*13)%94;
            plain[i]=(struct window_cell){character,1,2,0};
            run[i]=(struct window_cell){high[(trial+i)%6]|character,1,2,0};
        }
        memset(want,0xa5,sizeof(want));
        canvas_cells(want,pitch,face,plain,count,0x11223344,0x55667788);
        memset(got,0xa5,sizeof(got));
        canvas_cells(got,pitch,face,run,count,0x11223344,0x55667788);
        check(!memcmp(got,want,sizeof(got)));
        if(!wide)continue;
        memset(got,0xa5,sizeof(got));
        canvas_cells_wide(got,pitch,face,run,count,0x11223344,0x55667788);
        check(!memcmp(got,want,sizeof(got)));
    }
    assert(!munmap(pages,(size_t)page*3));
}

int main(void) {
    const int wide=simd_usable();
#if defined(KERNEL_MODE) && defined(CONFIG_MOONWATER_CANVAS)
    if(wide)check_wide_bodies();
#endif
    check_character_bounds(wide);
    const u32 colors[]={0,1,0x80000000,0xffffffff,0xaabbccdd,0x10203040};
    for(unsigned a=0;a<6;a++)for(unsigned b=0;b<6;b++)for(unsigned pattern=0;pattern<256;pattern++) {
        unsigned rows=1+pattern%17,pitch=8+(pattern%10),align=(pattern/10)%2;
        u8 bits[17];for(unsigned r=0;r<rows;r++)bits[r]=(pattern+r*17)&255;
        memset(pixels,0xa5,300*sizeof(*pixels));memset(expected,0xa5,300*sizeof(*expected));
        for(unsigned r=0;r<rows;r++)for(unsigned c=0;c<8;c++)
            expected[align+r*pitch+c]=colors[(bits[r]&(0x80>>c))?a:b];
        canvas_cell(pixels+align,pitch,bits,rows,colors[a],colors[b]);
        check(!memcmp(pixels,expected,300*sizeof(*pixels)));
    }
    for(unsigned a=0;a<6;a++)for(unsigned b=0;b<6;b++)for(unsigned pattern=0;pattern<256;pattern++) {
        unsigned rows=1+pattern%17,pitch=16+2*(pattern%5),align=((pattern/10)%2)*2;
        u8 bits[17];for(unsigned r=0;r<rows;r++)bits[r]=(pattern+r*17)&255;
        memset(pixels,0xa5,1200*sizeof(*pixels));memset(expected,0xa5,1200*sizeof(*expected));
        for(unsigned r=0;r<rows;r++)for(unsigned c=0;c<8;c++) {
            u32 color=colors[(bits[r]&(0x80>>c))?a:b];
            expected[align+(2*r)*pitch+2*c]=color;
            expected[align+(2*r)*pitch+2*c+1]=color;
            expected[align+(2*r+1)*pitch+2*c]=color;
            expected[align+(2*r+1)*pitch+2*c+1]=color;
        }
        canvas_cell2(pixels+align,pitch,bits,rows,colors[a],colors[b]);
        check(!memcmp(pixels,expected,1200*sizeof(*pixels)));
    }
    // A run is canvas_cell once a cell, at either alignment of a pixel pair.
    for(unsigned trial=0;trial<4096;trial++) {
        unsigned count=1+trial%23,pitch=count*8+(trial/23)%5,align=(trial/115)%2;
        struct window_cell run[23];static u8 face[256*16];
        u32 ink=colors[trial%6],paper=colors[(trial/6)%6];
        for(unsigned i=0;i<sizeof(face);i++)face[i]=(i*131+trial*7)&255;
        for(unsigned i=0;i<count;i++)
            run[i]=(struct window_cell){32+(trial*13+i*29)%95,(u8)i,(u8)trial,(unsigned short)i};
        memset(pixels,0xa5,4096*sizeof(*pixels));memset(expected,0xa5,4096*sizeof(*expected));
        canvas_cells(pixels+align,pitch,face,run,count,ink,paper);
        for(unsigned i=0;i<count;i++)
            canvas_cell(expected+align+i*8,pitch,face+run[i].character*16,16,ink,paper);
        check(!memcmp(pixels,expected,4096*sizeof(*pixels)));
    }
    check_compositor_gs1();
    check_compositor_title_length();
    check_compositor_keymap();
    check_compositor_pointer();
    check_compositor_outputs();
    check_compositor_displays();
    check_compositor_display_verb();
    check_compositor_scene();
    check_compositor_touch_backend();
    printf("  canvas-cells %u of %u\n",checks-failures,checks);
    const char *tally=getenv("TEST_TALLY");
    if(tally){FILE *f=fopen(tally,"a");assert(f);fprintf(f,"canvas-cells %u %u\n",checks-failures,checks);assert(!fclose(f));}
    return failures!=0;
}
#endif /* CHECK_canvas_cells */

