/*
        The file browser: a window on the model of files_model.c. It is the least a view can be and still be used: the path across
        the top with Back, Forward and Up beside it, the folder's entries as rows of the kit's list (a name, its size, when it was
        changed), and a line at the foot that says how many there are or what went wrong. A folder is opened by Enter or by a second
        click on its row (a finger's tap opens at once), Backspace and Alt+Up go up, Alt+Left and Alt+Right go back and forward.

        What the model keeps (the listing, its order, the selection, the path, the history, the errors) is the model's, and nothing
        here reads the disk: this file asks the model, lays the answer out and hands it events. The rows run from one edge of the
        window to the other, as the settings window's and the launcher's do (the list's row_x and row_w).

        A name is the disk's and is never drawn as it is: every one goes through ui_label_foreign, so a control byte is a picture of
        itself and a mark that turns the letters round is seen and does nothing. The path at the top is written the same way.

        The folder is read between events, a few milliseconds a pass (files_tick), so a folder of any size never holds the window;
        and it is looked at again once a second while the window is up (files_model_stale): one that changed is read again beside
        what is shown, and one that is gone says so. The rows are the kit's virtual list (ui_list_window): the list is told how
        many entries there are and asked for a row by its place in them, so a folder of any length scrolls from end to end, its
        bar's thumb is the view's share of the whole folder, and nothing here keeps a window of it.

        The path. At rest the band is the kit's trail (ui_trail) of the model's (files_trail): the deepest path gone to on this
        branch, each folder a press that goes to it, the folders up to the one shown in the ink and the deeper ones, gone up out
        of, dimmed where they were. A press on any of them, dimmed or not, goes there and the trail stays as it is; so do Up,
        Backspace and Alt+Up; opening the folder that is the next dimmed one moves along it; and only a folder that is not on it
        (another branch, opened or typed) makes the trail that folder's path. A dimmed folder is looked at again before it is gone
        to, by what it was: one that is gone, or has something else in its place (a link too: it is not followed), says so in the
        foot and it and the folders after it are struck through where they stand (lost), the folder shown staying: they are put
        right by spelling them again, below. Back and Forward keep paths, as they did, and the trail is
        worked out from the path gone to by the one rule (files_trail_note), so the two agree. Moving along the trail reads
        nothing: the model keeps the listings of the trail's folders that were read (files_model.c says how many), so a folder
        gone back to is shown at once as it was left, its cursor, what was chosen and where its rows were scrolled to, and is
        looked at again straight after (files_model_stale): only one that changed is read again, beside what is shown. A trail too long for the band
        shows the folders round the one shown and three dots where others are left out; a press on the dots, or on the band's own
        ground, or Ctrl+L, turns the band into the kit's text field holding the whole path: Enter goes where it says, Escape or a press
        elsewhere puts the crumbs back, and Tab finishes the folder name being typed from the listing shown when the text's
        folder is the one shown (it reads no disk). What is typed is not trusted: the field holds at most a path's bytes, and
        the text goes to the model's own open (files_model_try), which follows a link anywhere in what was typed, its last part
        too, because typing a name asks for what it names. A path that is no folder, may not be read or is not there says so in
        the foot, and the folder shown stays as it was.

        One folder of the trail spelled again. A second press on a folder of the trail, in the time a double click has, makes
        that one folder a small field holding its name, all of it chosen, with the folders round it where they were. Enter
        replaces that folder in the path and keeps every folder after it: /path/to/something/else with "to" made "foo" is
        /path/foo/something/else, and the browser goes to the deepest folder of that path that is one; the folders after it
        that are not there (or are no folder, or may not be read) stay in the trail struck through, the foot saying why the
        first of them is, so the next can be spelled the same way. A stroke typed makes several folders, no name takes the
        folder out, Escape or a press elsewhere leaves the trail as it was, and Tab finishes the name from the listing of the
        folder before it when the model has that in memory (the folder shown, or a kept one: no disk is read). The first press
        of the two is a press and goes to its folder at once, as any press on the trail does: going along the trail changes
        nothing of it and reads nothing, so there is nothing to wait for, and the second press takes that move back (the way
        back's own step, which also reads nothing) before the field opens, so Escape leaves a person where they were.

        The keys in the trail. Tab from the rows puts the keys on the trail (the folder shown first, a ring round it): Left and
        Right move between its folders, Enter goes to the one they are on, F2 spells it again, Ctrl+C copies the whole trail's
        path, the dimmed part with it, as text, and Tab, Escape or Down gives the keys back to the rows. Alt+Up and Alt+Down
        move along the trail wherever the keys are, and Ctrl+L opens the whole path's field with all of it chosen.

        Changing what is there. Delete asks first: the foot says how many would go and the first of their names, and only Delete
        again does it (any other key or a press leaves them); the delete then runs between events as the reading does
        (files_job_step), the foot counting, Escape stopping it, and the folder is read again when it ends. It is a real delete
        (the model says why there is no trash). F2 puts the name of the entry the keys are on in the band's field, where Enter
        gives it the new name and Escape leaves it; a name that is taken or is no name says so and nothing is renamed. Ctrl+Shift+N
        makes a folder called New folder (New folder 2 and on while the name is taken) with the keys on it, ready for F2.

        Dragging. A mouse's press on a row taken past the slop drags the rows out as files (text/uri-list, their paths as text beside):
        dropped on a terminal they are typed as quoted paths, on another program as it takes files. Files dragged over this window are
        copied or moved into the folder row under the pointer, ringed, or into the folder shown, the whole list ringed: Control a copy,
        Shift a move, else a move on the folder's own filesystem and a copy across. The copy is the model's (files_move_*), on a thread
        of its own: the foot counts what is done of how much, Escape stops it, and the folder is read again when it ends.

        Starts: files [PATH] [-f FONT] [-b BOLD]. With no path it shows HOME, or the root.
*/
#ifndef MOONWATER_CANVAS_FILES
#define MOONWATER_CANVAS_FILES

#define FILES_WIDTH 720              // logical pixels, times the scale
#define FILES_HEIGHT 480
#define FILES_STEP 128               // the entries of one step of the reading,
#define FILES_TURN_NS 4000000ll      // steps are taken for this long a pass,
#define FILES_SHOW_NS 100000000ll    // the entries so far are drawn this often while it reads,
#define FILES_LOOK_NS 1000000000ll   // and the folder shown is looked at again this often
#define FILES_TREE_WIDTH 180         // logical pixels of the folder tree beside the rows
#define FILES_KEY_F5 63
#define FILES_KEY_R 19
#define FILES_KEY_H 35
#define FILES_KEY_A 30
#define FILES_KEY_L 38
#define FILES_KEY_N 49
#define FILES_KEY_F2 60
#define FILES_EDIT_PATH 1            // files.editing: the band's field holds the path,
#define FILES_EDIT_NAME 2            // or the new name of the entry at files.naming
#define FILES_EDIT_PART 3            // or, in the trail itself, the name of its folder files.part
#define FILES_KEY_C 46
#define FILES_PART_PAD 6             // logical pixels of a spelled folder's field either side of its words
#define FILES_ASK_NAMES 3            // the names a delete's question says, of however many it would remove
#define FILES_KEY_F 33
#define FILES_TYPE_NS 1000000000ll    // letters typed on the rows within this of each other are one name to go to
#define FILES_KEY_X 45
#define FILES_KEY_V 47
#define FILES_KEY_F10 68
#define FILES_KEY_MENU 127           // the keyboard's menu key (KEY_COMPOSE)
// The browser's own menu items: after the kit's text menu's (1 to 7), which the path's field has.
#define FILES_MENU_OPEN 16
#define FILES_MENU_EDITOR 17
#define FILES_MENU_TERMINAL 18
#define FILES_MENU_CUT 19
#define FILES_MENU_COPY 20
#define FILES_MENU_PASTE 21
#define FILES_MENU_RENAME 22
#define FILES_MENU_DELETE 23
#define FILES_MENU_NEW_FOLDER 24
#define FILES_MENU_PROPERTIES 25
#define FILES_MENU_HIDDEN 26
#define FILES_MENU_ALL 27
#define FILES_MENU_SHEET 28          // a line of a properties sheet: chosen, it only closes the sheet
#define FILES_MENU_DETAILS 29        // the views: the rows as details under columns,
#define FILES_MENU_ICONS 30          // as large icons,
#define FILES_MENU_LIST 31           // or as a list of names, a small symbol before each
#define FILES_VIEW_DETAILS 0
#define FILES_VIEW_ICONS 1
#define FILES_VIEW_LIST 2
#define FILES_TILE_ICON 104          // logical pixels of a tile in the icon view,
#define FILES_TILE_LIST 232          // and of a name in the list view (a row high)
#define FILES_KEY_1 2
#define FILES_CLIP_CUT "application/x-moonwater-cut" // beside the files a cut offers: a paste of them moves them
#define FILES_SHEET_LINES 10                           // a properties sheet's lines

// Everything that is large: one mapping, not the image's bss.
struct files_big
{
        struct files_model model;
        struct ui_list list;
        struct ui_view view;
        struct files_job job;          // the delete under way, if one is
        struct files_move move;        // the copy or move under way (a drop's, a paste's), on its own thread
        struct files_tree tree;        // the folder tree beside the rows,
        struct ui_list tree_list;      // and the kit's list it is drawn as (a tree's rows: indented, with their chevrons)
        char crumb_words[FILES_TRAIL][FILES_NAME * 3 + 4]; // the trail's folders as the band writes them
        char note[256];                // what the foot says of the last thing done, in the language's words with its numbers and names in
        char status[192];              // the foot's words
        char sheet[FILES_SHEET_LINES][FILES_NAME * 3 + 64]; // a properties sheet's lines
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct files_big address_to big;
        int width, height, shown;
        unsigned int ground;
        b32 focused, dirty;
        p32 seen;                // the model's revision as last drawn
        p32 clicked;             // the place a mouse last clicked, and when: a second click there in time opens it
        b64 clicked_at, next_look, next_show;
        unsigned int said;       // LANGUAGE_files_* the foot says of the last thing asked, 0 none
        string_address start;    // the folder asked for as it started
        // The path's band: the crumbs at rest, the field while the path is typed.
        struct ui_rect crumbs_box;
        struct files_trail trail; // the model's trail: the deepest path of this branch, and where on it the folder shown is
        struct ui_trail crumbs;   // and the kit's, which draws it and says what a point is on
        int crumb_hover;          // the folder of it under the pointer, -1 none
        int editing;             // the band is the field: FILES_EDIT_PATH or FILES_EDIT_NAME; FILES_EDIT_PART one folder of the trail is; 0 the crumbs
        int part;                // the trail's folder being spelled again
        b32 crumb_keys;          // the keys are in the trail,
        int crumb_focus;         // on this folder of it
        int crumb_clicked, crumb_from; // the folder of the trail a press last went to, and the one that was shown before it:
        b64 crumb_clicked_at;    // a second press on it in time spells it again
        b32 reseat;              // the rows are to be put where the folder shown had them, at the next layout
        p32 naming;              // the place of the entry being named
        b32 armed;               // Delete was pressed once: the foot asks, and Delete again does it
        p32 armed_at;            // the model's revision the question was asked at: a listing that changed since is asked again
        struct ui_text field;    // the kit's text engine for it
        struct ui_control path;  // and the kit's bare field it is drawn in
        struct ui_popup menu;    // the field's own menu (copy, paste)
        struct ui_drag drag;     // the rows dragged out (files_uris: their list of files)
        p32 drag_place;          // the row the drag was pressed on
        b32 drag_chosen;         // and whether it drags the rows chosen (else that row alone)
        struct ui_columns columns; // the details' header over the rows: Name, Size, Type, Modified, each a press that orders by it
        struct ui_rect head_box;   // where the view put it
        struct ui_drop drop;     // a drag of files over the window: dropped, they are copied or moved into the folder it is over
        int drop_row;            // the folder's row the drop goes into, -1 the folder shown
        b64 next_progress;       // when the foot next says how far a copy has gone
        b32 moving;              // a copy or a move was begun and its end is not said yet
        b32 tree_keys;           // the keys are in the folder tree
        p32 tree_at;             // the tree's node the keys are on there
        p32 tree_shown;          // the tree's node of the folder shown, FILES_NO when it is not in the tree
        b32 tree_reseat;         // the tree's rows are to be scrolled to the folder shown at the next layout
        int view;                // FILES_VIEW_*: details, icons or a list
        b32 pasting;             // the clipboard was asked for files to paste: its answer is a paste into the folder shown
        // Find in the folder (Ctrl+F): a field at the foot whose text keeps the rows to the names that hold it (files_model_filter).
        b32 finding, find_keys;
        struct ui_text finder;
        struct ui_control finder_field;
        // Letters typed on the rows: the keys go to the first name that starts with them (typed, with when the last one came).
        p8 typed[32];
        positive typed_length;
        b64 typed_at;
        // A properties sheet open (the menu's popup, as lines): of the rows chosen or the cursor's (sheet_all false), or of the folder
        // shown; its size is measured on the move's thread (measuring) and the sheet is opened again with it when that is done.
        b32 sheet, sheet_folder, measuring;
        int sheet_x, sheet_y;
} files;

static struct ui_app files_app;

/* ---- the rows ---- */

// A count of bytes as a row says it: bare below 1024, then K to E with a decimal while below ten (the library's, as ls -h says it).
static fn files_size_text(char address_to into, p64 size)
{
        into[positive_into_human_1024_string((p8 address_to)into, (positive)size)] = 0;
}

// A moment as a row says it: the day and the minute, in the machine's own zone, digits whatever the language (2026-11-13 09:41).
static fn files_time_text(char address_to into, b64 seconds)
{
        tm broken;
        time_t at = (time_t)seconds;
        int year;

        into[0] = 0;
        if (!localtime_r(&at, &broken))
                return;
        year = broken.tm_year + 1900;
        if (year < 0 || year > 9999)
                return;
        into[0] = (char)('0' + year / 1000), into[1] = (char)('0' + year / 100 % 10), into[2] = (char)('0' + year / 10 % 10), into[3] = (char)('0' + year % 10);
        into[4] = '-', into[5] = (char)('0' + (broken.tm_mon + 1) / 10), into[6] = (char)('0' + (broken.tm_mon + 1) % 10);
        into[7] = '-', into[8] = (char)('0' + broken.tm_mday / 10), into[9] = (char)('0' + broken.tm_mday % 10);
        into[10] = ' ', into[11] = (char)('0' + broken.tm_hour / 10), into[12] = (char)('0' + broken.tm_hour % 10);
        into[13] = ':', into[14] = (char)('0' + broken.tm_min / 10), into[15] = (char)('0' + broken.tm_min % 10);
        into[16] = 0;
}

// An entry's name as a row writes it (ui_label_foreign), a folder's with a slash after it and a link's with an at sign, as ls -F marks them.
static positive files_name_text(char address_to into, positive room, const struct files_model address_to m, const struct files_entry address_to entry)
{
        positive used = room > 2 ? ui_label_foreign(into, room - 1, files_model_name(m, entry), entry->length) : 0;

        if (room > 2 && (entry->kind == FILES_KIND_DIRECTORY || entry->kind == FILES_KIND_LINK))
                into[used++] = entry->kind == FILES_KIND_DIRECTORY ? '/' : '@', into[used] = 0;
        return used;
}

// What kind of thing an entry is, as the Type column says it: a folder, a link, a file by its name's extension (a TXT file), a file.
static fn files_type_text(char address_to into, positive room, const struct files_model address_to m, const struct files_entry address_to entry)
{
        const p8 address_to name = files_model_name(m, entry);
        positive dot = 0;
        char extension[16];

        into[0] = 0;
        if (entry->kind != FILES_KIND_FILE)
        {
                string_copy_bounded((string_address)into, (string_address)(entry->kind == FILES_KIND_DIRECTORY ? WORDS(files_sheet_folder)
                                                                           : entry->kind == FILES_KIND_LINK    ? WORDS(files_type_link)
                                                                                                               : WORDS(files_type_other)),
                                    room);
                return;
        }
        // The last dot's words, a dot first not counting (.profile has none), at most a few letters and digits, written in capitals.
        for (positive at = 1; at < entry->length; at++)
                if (name[at] == '.')
                        dot = at;
        if (dot && entry->length - dot - 1 > 0 && entry->length - dot - 1 < 8)
        {
                positive k = 0;

                for (positive at = dot + 1; at < entry->length; at++)
                {
                        p8 c = name[at];

                        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                        {
                                k = 0;
                                break;
                        }
                        extension[k++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
                }
                extension[k] = 0;
                if (k)
                {
                        language_format(into, room, WORDS(files_type_ext), extension);
                        return;
                }
        }
        string_copy_bounded((string_address)into, (string_address)WORDS(files_sheet_file), room);
}

// A row, for the kit's list: the entry at that place of the listing (the list is virtual: a row's id is its place), as cells under the
// columns (its name, a file's size, its type, when it was changed), and a dot before the name of one that is chosen.
static fn files_describe(int id, struct ui_row address_to row)
{
        static char label[FILES_NAME * 3 + 4], size[16], when[24], type[48];
        const struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = id >= 0 ? files_model_entry(m, (p32)id) : null;

        if (!entry)
                return;
        files_name_text(label, sizeof(label), m, entry);
        row->label = label;
        size[0] = when[0] = 0;
        if (entry->looked)
        {
                if (entry->kind == FILES_KIND_FILE)
                        files_size_text(size, entry->size);
                files_time_text(when, entry->modified);
        }
        files_type_text(type, sizeof(type), m, entry);
        row->cells[1] = size, row->cells[2] = type, row->cells[3] = when;
        row->icon = 1 + (entry->kind == FILES_KIND_DIRECTORY ? UI_ICON_FILES
                         : entry->kind == FILES_KIND_LINK    ? UI_ICON_LINK
                         : entry->kind == FILES_KIND_FILE    ? UI_ICON_EDITOR
                                                             : UI_ICON_WINDOW);
        row->mark = files_model_chosen(m, (p32)id) && m->chosen_count > 1;
}

// A node of the folder tree, for its list: its name set in by its depth, a chevron before it unless it was read to have no folders, and
// the folder shown lit.
static fn files_tree_describe(int id, struct ui_row address_to row)
{
        static char label[FILES_NAME * 3 + 4];
        const struct files_tree address_to t = &files.big->tree;
        const struct files_node address_to n = id >= 0 && (p32)id < t->count ? &t->node[id] : null;

        if (!n)
                return;
        ui_label_foreign(label, sizeof(label), files_tree_name(t, (p32)id), n->length);
        row->label = label;
        row->indent = n->depth;
        row->expander = n->open ? UI_ROW_EXPANDER_OPEN : n->read && !n->has ? 0 : UI_ROW_EXPANDER_SHUT;
}

static fn files_tree_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        files.dirty = true;
}

// The tree after the folder shown changed: the nodes down to it opened, and it is the lit one (none when it is not in the tree).
static fn files_tree_follow(void)
{
        files.tree_shown = files_tree_reveal(&files.big->tree, files.big->model.path);
        if (files.tree_shown != FILES_NO)
                files.tree_at = files.tree_shown, files.tree_reseat = true;
}

// The model's order for a column of the header: Name, Size, Type, Modified.
static int files_column_sort(int column)
{
        return column == 1 ? FILES_SORT_SIZE : column == 2 ? FILES_SORT_KIND : column == 3 ? FILES_SORT_TIME : FILES_SORT_NAME;
}

static fn files_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        files.dirty = true;
}

/* ---- the window's words ---- */

// What the rows' place says when there are none: the folder's state, in the language's words.
static const char address_to files_state_text(const struct files_model address_to m)
{
        switch (m->state)
        {
        case FILES_READING: return WORDS(files_reading);
        case FILES_UNREADABLE: return WORDS(files_unreadable);
        case FILES_GONE: return WORDS(files_gone);
        case FILES_NOT_DIRECTORY: return WORDS(files_not_folder);
        case FILES_FAILED: return WORDS(files_failed);
        case FILES_READY: return WORDS(files_empty);
        }
        return "";
}

// The foot: what the last thing asked came to, else how many entries there are (the first of more, for a listing that was cut).
static fn files_status_text(void)
{
        const struct files_model address_to m = &files.big->model;
        char address_to into = files.big->status;
        positive room = sizeof(files.big->status);

        into[0] = 0;
        if (files.moving && !files.measuring && files_move_running(&files.big->move))
        {
                const struct files_move address_to job = &files.big->move;
                char done[16], whole[16];
                p32 items_done = __atomic_load_n(&job->items_done, __ATOMIC_ACQUIRE), items_total = __atomic_load_n(&job->items_total, __ATOMIC_ACQUIRE);

                files_size_text(done, __atomic_load_n(&job->bytes_done, __ATOMIC_ACQUIRE));
                files_size_text(whole, __atomic_load_n(&job->bytes_total, __ATOMIC_ACQUIRE));
                language_format(into, room, job->how == FILES_MOVE_MOVE ? WORDS(files_moving) : WORDS(files_copying), (int)items_done,
                                (int)(items_total > items_done ? items_total : items_done), done, whole);
        }
        else if (files.big->job.state == FILES_JOB_RUNNING)
                language_format(into, room, WORDS(files_deleting), (int)files.big->job.removed);
        else if (files.big->note[0])
                string_copy_bounded((string_address)into, (string_address)files.big->note, room);
        else if (files.said)
                string_copy_bounded((string_address)into, (string_address)language_text(files.said), room);
        else if (m->state == FILES_READY && m->chosen_count > 1)
        {
                // How many are chosen of how many, and the bytes of the files among them.
                p64 bytes = 0;
                char size[16];

                for (p32 place = 0; place < files_model_count(m); place++)
                {
                        const struct files_entry address_to entry = files_model_entry(m, place);

                        if (entry->kind == FILES_KIND_FILE && files_model_chosen(m, place))
                                bytes += entry->size;
                }
                files_size_text(size, bytes);
                language_format(into, room, WORDS(files_chosen_size), (int)m->chosen_count, (int)m->shown.count, size);
        }
        else if (m->state == FILES_READY && m->filter_length)
                language_format(into, room, WORDS(files_count_found), (int)files_model_count(m), (int)m->shown.count);
        else if (m->state == FILES_READY || (m->state == FILES_READING && m->shown.count))
                language_format(into, room, m->shown.cut ? WORDS(files_count_cut) : WORDS(files_count), (int)m->shown.count);
}

/* ---- the path's band ---- */

// The field, made the first time it is wanted: one line of the engine, at most a path's bytes.
static struct ui_text address_to files_field(void)
{
        if (!files.field.doc.selections && ui_field_open(&files.field, &files.own, ""))
                files.field.most = FILES_PATH - 1;
        return &files.field;
}

// The trail given to the kit and laid out in the band's box: each folder's name as words from outside, the root's the stroke itself.
static fn files_crumbs_lay(void)
{
        struct ui_trail address_to t = &files.crumbs;
        const struct files_trail address_to trail = &files.trail;

        t->count = trail->count, t->current = trail->current;
        t->separator = "/", t->rooted = trail->count && trail->from[0] == 0 && trail->to[0] == 1;
        for (int k = 0; k < trail->count; k++)
        {
                ui_label_foreign(files.big->crumb_words[k], sizeof(files.big->crumb_words[k]), trail->path + trail->from[k], (positive)(trail->to[k] - trail->from[k]));
                t->label[k] = files.big->crumb_words[k];
        }
        for (int k = 0; k < UI_TRAIL_MOST; k++)
                t->lost[k] = k < trail->count && trail->lost[k];
        t->x = files.crumbs_box.x, t->y = files.crumbs_box.y, t->w = files.crumbs_box.w, t->h = files.crumbs_box.h;
        t->hover = files.crumb_hover;
        t->keys = files.crumb_keys && files.focused && !files.editing, t->focus = files.crumb_focus;
        t->edit = files.part, t->edit_wide = 0;
        if (files.editing == FILES_EDIT_PART)
        {
                // The folder being spelled is a field as wide as what is typed in it, and never wider than half the band.
                static p8 typed[FILES_NAME * 2];
                static char shown[FILES_NAME * 6 + 4];
                positive length = ui_text_string(files_field(), typed, sizeof(typed));
                int wide;

                ui_label_foreign(shown, sizeof(shown), typed, length);
                wide = ui_kit_measure(&files.ui, (string_address)shown, UI_BODY_LARGE) + ui_dev(&files.ui, 2 * FILES_PART_PAD + 8);
                wide = wide < ui_dev(&files.ui, 56) ? ui_dev(&files.ui, 56) : wide > t->w / 2 ? t->w / 2 : wide;
                t->edit_wide = wide, t->current = files.part; // the field is kept in sight
        }
        ui_trail_layout(&files.ui, t);
        t->current = trail->current;
}

// What of the trail a point of the window is on: a folder's number, UI_TRAIL_MARK, or -1 (the band's own ground, or not the band).
static int files_crumb_at(int x, int y)
{
        return files.editing ? -1 : ui_trail_hit(&files.ui, &files.crumbs, x, y);
}

static fn files_crumbs_draw(void)
{
        struct ui_trail address_to t = &files.crumbs;

        t->hover = files.crumb_hover;
        ui_trail_draw(&files.ui, t);
        if (files.editing == FILES_EDIT_PART && t->edit_wide && t->edit >= t->first && t->edit <= t->last)
        {
                // The folder being spelled: the kit's field, drawn where its words were.
                int x = ui_trail_flip(t, t->at[t->edit], t->wide[t->edit]), pad = ui_dev(&files.ui, FILES_PART_PAD);

                ui_text_field_draw(&files.ui, &files.path, x + pad, x + t->wide[t->edit] - pad, t->y, t->y + t->h);
                files_field()->well_left = x, files_field()->well_right = x + t->wide[t->edit];
        }
}

/* ---- the layout ---- */

static b32 files_act(int which);

// The window as a tree: the path's band (the three buttons, then the path), the rows, and the foot.
static fn files_layout(void)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;

        if (!l->describe)
        {
                l->virtual = true, l->beside = true;
                l->describe = files_describe;
                l->damaged = files_damaged;
                l->hover = l->down = -1;
        }
        if (!files.big->tree_list.describe)
        {
                struct ui_list address_to t = &files.big->tree_list;

                t->virtual = true, t->tree = true, t->beside = true;
                t->describe = files_tree_describe;
                t->damaged = files_tree_damaged;
                t->hover = t->down = -1;
        }
        files.big->tree_list.total = (int)files.big->tree.count;
        files.big->tree_list.focus = files.tree_keys ? (int)files.tree_at : files.tree_shown != FILES_NO ? (int)files.tree_shown : -1;
        files_status_text();
        if (!files.path.kind)
        {
                files.path.kind = UI_INPUT, files.path.variant = UI_FIELD_BARE;
                files.path.edit = files_field();
        }
        files.path.focused = files.editing && files.focused;
        if (!files.finder_field.kind)
        {
                files.finder_field.kind = UI_INPUT, files.finder_field.variant = UI_FIELD_BARE;
                files.finder_field.edit = &files.finder;
        }
        files.finder_field.focused = files.finding && files.find_keys && files.focused;
        files.finder_field.items[0] = WORDS(files_find_hint);
        files.path.items[0] = files.editing == FILES_EDIT_NAME ? WORDS(files_rename_hint) : null;
        // Every entry is a row of the list, the row the keys are on is the model's cursor, and an empty folder says what it is.
        l->total = (int)files_model_count(m);
        l->focus = m->cursor != FILES_NO ? (int)m->cursor : -1;
        l->empty = files_state_text(m);
        ui_view_begin(&files.big->view, ui, files.width, -files.height);
        ui_row(.class = "pathbar")
        {
                ui_button(WORDS(files_back), .class = "ghost", .on = files_act, .arg = 0);
                ui_button(WORDS(files_forward), .class = "ghost", .on = files_act, .arg = 1);
                ui_button(WORDS(files_up), .class = "ghost", .on = files_act, .arg = 2);
                // The path: the field while it is typed, else the room its crumbs are laid out and drawn in (files_crumbs_lay).
                if (files.editing && files.editing != FILES_EDIT_PART)
                        ui_control(&files.path, .grow = 1, .h = UI_ROW);
                else
                        ui_host(&files.crumbs_box, .grow = 1, .h = UI_ROW);
        }
        // The folder tree, then the header of the columns over the rows: the kit's lists, their words in from their margins.
        ui_row(.grow = 1)
        {
                ui_col(.class = "rows", .w = FILES_TREE_WIDTH, .shrink = 0)
                        ui_rows(&files.big->tree_list, .class = "list", .grow = 1);
                ui_col(.class = "rows", .grow = 1)
                {
                        ui_host(&files.head_box, .h = UI_COLUMNS_H, .hidden = files.view != FILES_VIEW_DETAILS);
                        ui_rows(l, .class = "list", .grow = 1);
                }
        }
        ui_row(.class = "bar")
        {
                if (files.finding)
                        ui_control(&files.finder_field, .grow = 1, .h = UI_ROW);
                ui_text(files.big->status, .class = "status");
        }
        ui_view_end(&files.big->view);
        // A folder shown as it was left has its rows where they were; any other, from the top. And where they are now is the model's
        // to keep with the listing when the folder is left.
        if (files.reseat)
                ui_scroll_set(&l->scroll, (int)m->seat), files.reseat = false;
        m->seat = l->scroll.scroll;
        // The columns across the rows' words, in the language's titles; the one the rows are ordered by is the model's order.
        files.columns.count = 4;
        files.columns.title[0] = WORDS(files_column_name), files.columns.title[1] = WORDS(files_column_size);
        files.columns.title[2] = WORDS(files_column_type), files.columns.title[3] = WORDS(files_column_modified);
        files.columns.at_end[1] = true;
        ui_columns_layout(ui, &files.columns, l->x, files.head_box.y, l->w);
        // The view: details are rows under the columns, icons and a list are tiles (a symbol over a name, or before it).
        l->columns = files.view == FILES_VIEW_DETAILS ? &files.columns : null;
        l->tiles = files.view == FILES_VIEW_ICONS ? FILES_TILE_ICON : files.view == FILES_VIEW_LIST ? FILES_TILE_LIST : 0;
        l->tile_h = files.view == FILES_VIEW_LIST ? UI_ROW : 0;
        // The tree's rows scrolled so the folder shown is in sight, once it is shown.
        if (files.tree_reseat && files.big->tree_list.h > 0)
        {
                files.big->tree_list.focus = (int)files.tree_shown;
                ui_list_reveal(ui, &files.big->tree_list);
                files.tree_reseat = false;
        }
        if (!files.editing || files.editing == FILES_EDIT_PART)
                files_crumbs_lay();
}

// The cursor's row in sight: the list scrolled to it, wherever in the listing it is.
static fn files_show_cursor(void)
{
        if (files.big->model.cursor == FILES_NO)
                return;
        files_layout();
        ui_list_reveal(&files.ui, &files.big->list);
        files.dirty = true;
}

/* ---- what a person asks ---- */

static fn files_moved(void);
static fn files_part_go(void);

// The band becomes the field: holding the whole path (FILES_EDIT_PATH), or the name of the entry the keys are on for a new one
// (FILES_EDIT_NAME); or the crumbs again (0).
static fn files_edit(int how)
{
        const struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, m->cursor);

        if (how == FILES_EDIT_NAME && !entry)
                return;
        if (how == FILES_EDIT_PART && (files.part <= 0 || files.part >= files.trail.count))
                how = FILES_EDIT_PATH; // the root is not spelled again: the whole path is
        files.editing = how;
        files_field()->most = how == FILES_EDIT_NAME ? FILES_NAME - 1 : FILES_PATH - 1;
        if (how == FILES_EDIT_PATH)
                ui_text_set(files_field(), m->path, string_length((string_address)m->path));
        else if (how == FILES_EDIT_NAME)
                ui_text_set(files_field(), files_model_name(m, entry), entry->length), files.naming = m->cursor;
        else if (how == FILES_EDIT_PART)
                ui_text_set(files_field(), files.trail.path + files.trail.from[files.part], (positive)(files.trail.to[files.part] - files.trail.from[files.part]));
        // The whole path, and a folder's name in the trail, are chosen whole: what is typed takes their place.
        if (how == FILES_EDIT_PATH || how == FILES_EDIT_PART)
                doc_select_set(&files_field()->doc, 0, doc_length(&files_field()->doc));
        files.crumb_keys = false;
        files.crumb_hover = -1, files.said = 0, files.armed = false, files.big->note[0] = 0;
        files.dirty = true;
}

// The names of what a delete would remove, the first FILES_ASK_NAMES of them as words from outside with a comma between, and how many
// more there are after a plus: what the question shows of them. Answers how many there are in all.
static p32 files_ask_names(char address_to into, positive room)
{
        const struct files_model address_to m = &files.big->model;
        positive used = 0;
        p32 count = 0;

        into[0] = 0;
        for (p32 place = 0; place < files_model_count(m); place++)
        {
                const struct files_entry address_to entry = files_model_entry(m, place);

                if (m->chosen_count ? !files_model_chosen(m, place) : place != m->cursor)
                        continue;
                if (count++ >= FILES_ASK_NAMES || used + 80 > room)
                        continue;
                if (used)
                        into[used++] = ',', into[used++] = ' ';
                used += ui_label_foreign(into + used, 64, files_model_name(m, entry), entry->length);
        }
        if (count > FILES_ASK_NAMES && used + 16 < room)
        {
                into[used++] = ' ', into[used++] = '+';
                used += positive_into((p8 address_to)into + used, count - FILES_ASK_NAMES);
                into[used] = 0;
        }
        return count;
}

// Delete: the first press asks, saying how many and which; the second, with the listing as it was asked of, begins the delete.
static fn files_delete(void)
{
        struct files_model address_to m = &files.big->model;
        char names[256];
        p32 count = files_ask_names(names, sizeof(names));

        files.said = 0;
        if (!count || m->state != FILES_READY || files.big->job.state == FILES_JOB_RUNNING)
                return;
        if (files.armed && files.armed_at == m->revision)
        {
                files.armed = false, files.big->note[0] = 0;
                if (!files_delete_begin(m, &files.big->job))
                {
                        // An entry was no longer the one listed: nothing was removed, and the folder is read again.
                        files.said = LANGUAGE_files_changed;
                        files_model_reload(m);
                }
                files.dirty = true;
                return;
        }
        files.armed = true, files.armed_at = m->revision;
        language_format(files.big->note, sizeof(files.big->note), WORDS(files_delete_ask), (int)count, names);
        files.dirty = true;
}

// A folder made in the folder shown, called New folder, or New folder 2 and on while that is taken: the keys go to it when it is listed.
static fn files_new_folder(void)
{
        struct files_model address_to m = &files.big->model;
        static p8 name[FILES_NAME];
        int did = FILES_DID_EXISTS;

        for (int number = 1; number < 100 && did == FILES_DID_EXISTS; number++)
        {
                string_copy_bounded(name, (string_address)WORDS(files_new_folder), FILES_NAME - 8);
                if (number > 1)
                {
                        positive used = string_length(name);

                        name[used++] = ' ';
                        name[used + positive_into(name + used, (positive)number)] = end;
                }
                did = files_model_make_folder(m, name);
        }
        files.big->note[0] = 0;
        files.said = did == FILES_DID ? 0 : did == FILES_DID_EXISTS ? LANGUAGE_files_name_taken : did == FILES_DID_BAD ? LANGUAGE_files_name_bad : LANGUAGE_files_unreadable;
        if (did == FILES_DID)
        {
                files_model_reload(m);
                string_copy_bounded(m->came, name, FILES_NAME);
        }
        files.dirty = true;
}

// Enter in the field while it holds a name: the entry is given it, and the keys stay on it. A name that is taken, or is no name, or an
// entry that is no longer the one listed, says so and renames nothing; the field stays for the name to be put right.
static fn files_name_go(void)
{
        struct files_model address_to m = &files.big->model;
        static p8 typed[FILES_NAME];
        positive length = ui_text_string(files_field(), typed, sizeof(typed));
        int did;

        typed[length < sizeof(typed) ? length : sizeof(typed) - 1] = end;
        did = files_model_rename(m, files.naming, typed);
        if (did != FILES_DID)
        {
                files.said = did == FILES_DID_EXISTS ? LANGUAGE_files_name_taken : did == FILES_DID_BAD ? LANGUAGE_files_name_bad : LANGUAGE_files_unreadable;
                files.dirty = true;
                return;
        }
        files_edit(0);
        files_model_reload(m);
        string_copy_bounded(m->came, typed, FILES_NAME);
}

static unsigned int files_state_said(int state)
{
        return state == FILES_GONE ? LANGUAGE_files_gone : state == FILES_UNREADABLE ? LANGUAGE_files_unreadable : state == FILES_NOT_DIRECTORY ? LANGUAGE_files_not_folder
               : LANGUAGE_files_failed;
}

// The foot says why a folder of the trail is lost, with its name before the words, since it is not the folder shown: "else: This folder is gone".
static fn files_lost_say(int which, unsigned int said)
{
        const struct files_trail address_to trail = &files.trail;
        char address_to into = files.big->note;
        positive used;

        files.said = said, into[0] = 0;
        if (which <= 0 || which >= trail->count)
                return;
        used = ui_label_foreign(into, 96, trail->path + trail->from[which], (positive)(trail->to[which] - trail->from[which]));
        into[used++] = ':', into[used++] = ' ', into[used] = 0;
        string_append_bounded((string_address)into, (string_address)language_text(said), sizeof(files.big->note));
}

// Enter in the field of one folder of the trail: the path with that folder spelled as typed and the folders after it kept, gone to as
// far as it is folders, the rest of it lost in the trail with the reason in the foot. Text that is no folder's name says so and the field stays.
static fn files_part_go(void)
{
        static p8 typed[FILES_PATH], path[FILES_PATH], before[FILES_PATH];
        struct files_model address_to m = &files.big->model;
        positive length = ui_text_string(files_field(), typed, sizeof(typed));
        int why;

        typed[length < sizeof(typed) ? length : sizeof(typed) - 1] = end;
        string_copy_bounded(before, m->path, FILES_PATH);
        if (string_length(typed) != length || !files_trail_spell(&files.trail, files.part, typed, path) ||
            (why = files_trail_resolve(m, &files.trail, path)) == FILES_NONE)
        {
                files.said = LANGUAGE_files_name_bad, files.dirty = true;
                return;
        }
        files_edit(0);
        if (!string_equals((string_address)before, (string_address)m->path))
                files_moved();
        else
                files_keep_trim(m, &files.trail);
        if (why != FILES_READY)
                files_lost_say(files.trail.current + 1, files_state_said(why));
        files.dirty = true;
}

// Tab in that field: the name being typed finished from the listing of the folder before it in the trail, when the model has it.
static fn files_part_complete(void)
{
        static p8 typed[FILES_PATH], parent[FILES_PATH], name[FILES_NAME];
        positive length = ui_text_string(files_field(), typed, sizeof(typed));

        typed[length < sizeof(typed) ? length : sizeof(typed) - 1] = end;
        length = string_length(typed);
        for (positive at = 0; at < length; at++)
                if (typed[at] == '/')
                        return;
        if (files.part <= 0 || length >= FILES_NAME)
                return;
        files_trail_path(&files.trail, files.part - 1, parent);
        if (!files_model_complete_in(&files.big->model, parent, typed, length, name, sizeof(name)) || string_length(name) == length)
                return;
        ui_text_set(files_field(), name, string_length(name));
        files.dirty = true;
}

/*
        The rows chosen (or the one pressed, place, when it is not chosen), as what a drag or a copy of files is to every program: a list of
        file URIs (text/uri-list: file:// and each path, its bytes past the unreserved ones and the slash as %XX, a line each, CRLF), and
        the same paths as text, a line each, for a program that takes text alone. The two parts, their bytes the caller releases
        (files_uris_free); false when there is no row.
*/
static b32 files_uris(p32 place, struct windows_clip_part address_to parts, p8 address_to address_to uris, positive address_to uris_room,
                      p8 address_to address_to paths, positive address_to paths_room)
{
        static const char hex[] = "0123456789ABCDEF";
        struct files_model address_to m = &files.big->model;
        positive uri_length = 0, path_length = 0;
        b32 any_chosen = files_model_chosen(m, place);

        *uris = *paths = null, *uris_room = *paths_room = 0;
        for (p32 at = 0; at < files_model_count(m); at++)
        {
                const struct files_entry address_to entry = files_model_entry(m, at);
                const p8 address_to name = entry ? files_model_name(m, entry) : null;
                positive folder = string_length(m->path), length = name ? string_length(name) : 0;

                if (!name || (any_chosen ? !files_model_chosen(m, at) : at != place))
                        continue;
                if (!array_store_reserve(*uris, *uris_room, uri_length, uri_length + 3 * (folder + length) + 12, 4096) ||
                    !array_store_reserve(*paths, *paths_room, path_length, path_length + folder + length + 3, 4096))
                        break;
                memory_copy(*uris + uri_length, "file://", 7), uri_length += 7;
                for (positive k = 0; k < folder + 1 + length; k++)
                {
                        p8 c = k < folder ? m->path[k] : k == folder ? (p8)'/' : name[k - folder - 1];

                        if (k == folder && folder == 1)
                                continue; // the root's own slash is the path's first
                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                                (*uris)[uri_length++] = c;
                        else
                                (*uris)[uri_length++] = '%', (*uris)[uri_length++] = (p8)hex[c >> 4], (*uris)[uri_length++] = (p8)hex[c & 15];
                        if (!(k == folder && folder == 1))
                                (*paths)[path_length++] = c;
                }
                (*uris)[uri_length++] = '\r', (*uris)[uri_length++] = '\n';
                (*paths)[path_length++] = '\n';
        }
        if (!uri_length)
        {
                array_store_release(*uris, *uris_room, uri_length);
                array_store_release(*paths, *paths_room, path_length);
                return false;
        }
        parts[0] = (struct windows_clip_part){WINDOWS_CLIP_URIS, *uris, uri_length};
        parts[1] = (struct windows_clip_part){WINDOWS_CLIP_TEXT, *paths, path_length - 1};
        return true;
}

static fn files_uris_free(p8 address_to uris, positive uris_room, p8 address_to paths, positive paths_room, const struct windows_clip_part address_to parts)
{
        positive uri_length = parts[0].length, path_length = parts[1].length + 1;

        array_store_release(uris, uris_room, uri_length);
        array_store_release(paths, paths_room, path_length);
}

/*
        The rows chosen (the cursor's when none is) copied, as files (files_uris), or cut: a cut offers them with a word beside
        (FILES_CLIP_CUT) that makes a browser's paste of them a move, here or in another window; any other program pastes them as it
        pastes files.
*/
static fn files_rows_copy(b32 cut)
{
        struct windows_clip_part parts[3];
        p8 address_to uris, address_to paths;
        positive uris_room, paths_room;

        if (!files_uris(files.big->model.cursor, parts, &uris, &uris_room, &paths, &paths_room))
                return;
        parts[2] = (struct windows_clip_part){FILES_CLIP_CUT, (const p8 address_to) "cut", 3};
        windows_clip_offer_parts(&files.own, WINDOWS_CLIP_CLIPBOARD, parts, cut ? 3 : 2);
        files_uris_free(uris, uris_room, paths, paths_room, parts);
}

// Paste: the clipboard is asked for its files (the answer comes as a WINDOW_EVENT_CLIP, files_clip_event).
static fn files_paste(void)
{
        files.pasting = true;
        windows_clip_ask(&files.own, WINDOWS_CLIP_CLIPBOARD);
}

static b32 files_transfer(const p8 address_to paths, p32 count, int row, int how);

/*
        The clipboard's news. A copy this window offered that was taken or declined is let go of (windows_clip_done). The answer to a paste
        it asked for: the files it holds (text/uri-list) copied into the folder shown, or moved when they were cut; a clipboard of no files
        says so in the foot.
*/
static b32 files_clip_event(const struct window_event address_to event)
{
        p8 address_to bytes, address_to cut, address_to paths = null;
        unsigned long length, cut_length;
        positive at = 0, used = 0, room = 0, got;
        p32 count = 0;
        static p8 path[FILES_PATH];
        b32 asked = files.pasting && (event->b == WINDOWS_CLIP_READY || event->b == WINDOWS_CLIP_REFUSED), moves;

        if (event->b == WINDOWS_CLIP_TAKEN || event->b == WINDOWS_CLIP_DECLINED)
                windows_clip_done((unsigned int)event->a);
        if (!asked)
                return true;
        files.pasting = false, files.dirty = true;
        if (event->b != WINDOWS_CLIP_READY || windows_clip_read_type(&files.own, (unsigned int)event->a, WINDOWS_CLIP_URIS, &bytes, &length) <= 0)
        {
                files.said = LANGUAGE_files_nothing_to_paste;
                return true;
        }
        moves = windows_clip_read_type(&files.own, (unsigned int)event->a, FILES_CLIP_CUT, &cut, &cut_length) > 0;
        if (moves)
                windows_clip_free(cut, cut_length);
        while ((got = ui_uri_next(bytes, (positive)length, &at, path, sizeof(path))) && used + got + 1 <= (positive)FILES_MOVE_MOST * FILES_PATH / 4)
        {
                if (!array_store_reserve(paths, room, used, used + got + 1, 4096))
                        break;
                memory_copy(paths + used, path, got + 1);
                used += got + 1, count++;
        }
        windows_clip_free(bytes, length);
        if (count)
                files_transfer(paths, count, -1, moves ? FILES_MOVE_MOVE : FILES_MOVE_COPY);
        else
                files.said = LANGUAGE_files_nothing_to_paste;
        array_store_release(paths, room, used);
        return true;
}

/*
        Drag and drop (the kit's ui_drag and ui_drop).

        Out: a mouse's press on a row taken past the slop drags the rows (files_uris: the chosen ones when it was pressed on one of them,
        else that row), copied or moved, to any program; the press is the drag's from then on (the list's own press is let go: it neither
        scrolls nor stretches the rows under the drag). Its target does what is done: the browser takes nothing away when a move ends,
        and reads its folder again.

        In: a drag of files (text/uri-list) over the window is copied or moved into a folder: the folder row under the pointer, ringed, or
        else the folder shown, the whole list ringed. Control makes it a copy and Shift a move; else it is a move when the files are on
        the folder's own filesystem (a drag of this window's own rows, or another program's whose first file is there) and a copy
        otherwise. A drag of this window's rows over the folder they are in, or over one of the rows dragged, is taken nowhere. The drop
        is the model's move (files_move_*), on a thread of its own, the foot counting and Escape stopping it.
*/
#define FILES_DROP_MOST (FILES_MOVE_MOST * FILES_PATH / 4) // the bytes of a drop's paths, each with its end

// Whether the row at place is one of those this window's drag holds.
static b32 files_dragged(p32 place)
{
        return files.drag.live && (files.drag_chosen ? files_model_chosen(&files.big->model, place) : place == files.drag_place);
}

// The folder row a drag at (x, y) over the window would go into, -1 for the folder shown.
static int files_drop_row(int x, int y)
{
        struct files_model address_to m = &files.big->model;
        int row = ui_list_hit(&files.ui, &files.big->list, x, y);
        const struct files_entry address_to entry = row >= 0 ? files_model_entry(m, (p32)row) : null;

        return entry && entry->kind == FILES_KIND_DIRECTORY && !files_dragged((p32)row) ? row : -1;
}

// Whether the first file of the drag over the window is on the filesystem of the folder shown: a move is then a rename.
static b32 files_drop_near(void)
{
        const struct files_model address_to m = &files.big->model;
        unsigned long size = 0;
        const unsigned char address_to copy;
        struct windows_clip_type type;
        static p8 path[FILES_PATH];
        positive at = 0;
        file_facts facts;
        b32 near = false;

        if (files.drag.live)
                return true;
        copy = windows_drag_types(&files.own, &size);
        if (copy && windows_clip_find(copy, size, WINDOWS_CLIP_URIS, string_length((string_address)WINDOWS_CLIP_URIS), &type) && type.length &&
            ui_uri_next((const p8 address_to)copy + type.offset, (positive)type.length, &at, path, sizeof(path)) &&
            file_look_code(AT_FDCWD, (string_address)path, AT_SYMLINK_NOFOLLOW, &facts) >= 0)
                near = facts.device_major == m->folder.device_major && facts.device_minor == m->folder.device_minor;
        if (copy)
                window_call(WINDOW_SYS_MUNMAP, (long)copy, (long)size, 0, 0, 0, 0);
        return near;
}

// The folder a drop goes into, opened as its own descriptor: the row's (held to what was listed, not followed) or the folder shown's.
static bipolar files_drop_folder(int row)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = row >= 0 ? files_model_entry(m, (p32)row) : null;
        file_facts facts;

        if (m->handle < 0)
                return -ERROR_NO_ENTRY;
        if (row < 0)
                return system_open_at(m->handle, (string_address) ".", FILE_READ | O_DIRECTORY | O_CLOEXEC);
        if (!entry || entry->kind != FILES_KIND_DIRECTORY || files_model_check(m, (p32)row, &facts) != FILES_SAME)
                return -ERROR_NO_ENTRY;
        return file_open_same(m->handle, (string_address)files_model_name(m, entry), &facts, FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

/*
        A copy or a move of files (paths, count of them, each with its end) into the folder at row (-1 the folder shown), begun on its own
        thread. False, with the foot saying why, when it could not begin.
*/
static b32 files_transfer(const p8 address_to paths, p32 count, int row, int how)
{
        struct files_move address_to job = &files.big->move;
        bipolar into;

        files.said = 0, files.big->note[0] = 0, files.dirty = true;
        if (files_move_running(job) || files.big->job.state == FILES_JOB_RUNNING)
        {
                files.said = LANGUAGE_files_busy;
                return false;
        }
        files_move_end(job);
        if ((into = files_drop_folder(row)) < 0)
        {
                files.said = LANGUAGE_files_changed;
                files_model_reload(&files.big->model);
                return false;
        }
        if (!files_move_begin(job, how, into, paths, count))
                return false;
        files_move_start(job);
        files.moving = true, files.next_progress = (b64)ui_kit.now;
        return true;
}

// What a copy or a move that is over came to, in the foot: how many, or where it stopped and why.
static fn files_move_said(void)
{
        const struct files_move address_to job = &files.big->move;
        char at[FILES_NAME * 3 + 4];
        int done = (int)job->items_done;

        files.said = 0;
        ui_label_foreign(at, 96, job->at, string_length((string_address)job->at));
        if (job->state == FILES_JOB_FAILED && job->error == -FILES_MOVE_INSIDE)
                files.said = LANGUAGE_files_move_inside;
        else if (job->state == FILES_JOB_FAILED)
                language_format(files.big->note, sizeof(files.big->note), WORDS(files_move_stopped), done, at);
        else if (job->state == FILES_JOB_CANCELLED)
                language_format(files.big->note, sizeof(files.big->note), WORDS(files_move_cancelled), done);
        else
                language_format(files.big->note, sizeof(files.big->note), job->how == FILES_MOVE_MOVE ? WORDS(files_moved) : WORDS(files_copied), done);
}

static b32 files_drag_event(const struct window_event address_to event)
{
        static const char address_to const names[1] = {WINDOWS_CLIP_URIS};
        struct ui_list address_to l = &files.big->list;
        int what;

        if (ui_drag_source(&files.drag, event))
        {
                // The drag is over: what a move took from here is gone by its target's hand, and the folder is read again to show it.
                if (files.drag.finished && files.drag.action == WINDOWS_DRAG_MOVE)
                        files_model_reload(&files.big->model);
                files.dirty = true;
                return true;
        }
        what = ui_drop_event(&files.drop, event);
        if (what == UI_DROP_OVER)
        {
                int row = files_drop_row(files.drop.x, files.drop.y);
                b32 none = (files.drag.live && row < 0) || files.big->model.state != FILES_READY;
                p32 preferred = files.drop.keys & WINDOW_KEY_CONTROL ? WINDOWS_DRAG_COPY
                                : files.drop.keys & WINDOW_KEY_SHIFT ? WINDOWS_DRAG_MOVE
                                : files_drop_near()                  ? WINDOWS_DRAG_MOVE
                                                                     : WINDOWS_DRAG_COPY;

                // Another folder or another preference is a new answer: the pick is said again (ui_drop_pick says only a change).
                if (files.drop_row != row || files.drop.action != preferred)
                        files.drop.nth = -2;
                files.drop_row = row, files.drop.action = preferred;
                ui_drop_pick(&files.own, &files.drop, names, none ? 0 : 1, WINDOWS_DRAG_COPY | WINDOWS_DRAG_MOVE, preferred);
                l->drop = files.drop.nth < 0 ? 0 : row >= 0 ? row + 1 : UI_LIST_DROP_ALL;
        }
        else if (what == UI_DROP_LEFT || what == UI_DROP_DONE || what == UI_DROP_DROPPED)
                l->drop = 0;
        else if (what == UI_DROP_READY)
        {
                p8 address_to bytes, address_to paths = null;
                unsigned long length;
                positive at = 0, used = 0, room = 0, got;
                p32 count = 0;
                b32 took = false;
                static p8 path[FILES_PATH];

                if (ui_drop_read(&files.own, &files.drop, &bytes, &length))
                {
                        while ((got = ui_uri_next(bytes, (positive)length, &at, path, sizeof(path))) && used + got + 1 <= FILES_DROP_MOST)
                        {
                                if (!array_store_reserve(paths, room, used, used + got + 1, 4096))
                                        break;
                                memory_copy(paths + used, path, got + 1);
                                used += got + 1, count++;
                        }
                        windows_clip_free(bytes, length);
                        took = count && files_transfer(paths, count, files.drop_row, files.drop.action == WINDOWS_DRAG_MOVE ? FILES_MOVE_MOVE : FILES_MOVE_COPY);
                        array_store_release(paths, room, used);
                }
                ui_drop_done(&files.own, &files.drop, took);
        }
        files.dirty = true;
        return true;
}

// The whole trail's path, the dimmed part with it, offered to the clipboard as text.
static fn files_trail_copy(void)
{
        positive length = string_length(files.trail.path), room = 0;
        p8 address_to text = null;

        if (!length || !array_store_reserve(text, room, 0, length + 1, 256))
                return;
        memory_copy(text, files.trail.path, length);
        ui_clip_offer(&files.own, WINDOWS_CLIP_CLIPBOARD, text, room, length);
}

// To the folder the field's text names, if it is one: the crumbs come back with it. One that is not says why in the foot, and the
// field stays as it is for the text to be put right.
static fn files_edit_go(void)
{
        static p8 typed[FILES_PATH];
        positive length = ui_text_string(files_field(), typed, sizeof(typed));
        int came;

        if (files.editing == FILES_EDIT_NAME)
        {
                files_name_go();
                return;
        }
        if (files.editing == FILES_EDIT_PART)
        {
                files_part_go();
                return;
        }

        typed[length < sizeof(typed) ? length : sizeof(typed) - 1] = end;
        came = files_model_try(&files.big->model, typed);
        if (came == FILES_READING)
        {
                files_edit(0);
                files_moved();
                return;
        }
        files.said = files_state_said(came);
        files.dirty = true;
}

// Tab in the field: the folder name being typed finished from the listing shown, when the text's folder is the folder shown.
static fn files_edit_complete(void)
{
        static p8 typed[FILES_PATH], whole[FILES_PATH], name[FILES_NAME];
        const struct files_model address_to m = &files.big->model;
        positive length = ui_text_string(files_field(), typed, sizeof(typed)), cut, folder;
        p32 found;

        typed[length < sizeof(typed) ? length : sizeof(typed) - 1] = end;
        length = string_length(typed);
        for (cut = length; cut > 0 && typed[cut - 1] != '/'; cut--)
                ;
        // The text's folder: what is before its last stroke (the root's is the stroke itself).
        folder = cut > 1 ? cut - 1 : cut;
        if (!cut || folder != string_length(m->path) || memory_compare((address_any)typed, (address_any)m->path, folder) || length - cut >= FILES_NAME)
                return;
        found = files_model_complete(m, typed + cut, length - cut, name, sizeof(name));
        if (!found || cut + string_length(name) + 2 >= sizeof(whole))
                return;
        memory_copy(whole, typed, cut);
        memory_copy(whole + cut, name, string_length(name) + 1);
        if (found == 1)
                string_append_bounded(whole, (string_address) "/", sizeof(whole));
        ui_text_set(files_field(), whole, string_length(whole));
        files.dirty = true;
}

// To a folder of the trail. One deeper than the folder shown is opened as the folder it was (files_trail_open: not followed, and held
// to its device and inode on the descriptor that was opened, which is then the one shown): gone, or something else in its place, is
// said in the foot and it and the folders after it are lost in the trail, the folder shown staying. The trail's folders do not change
// by going along it.
static fn files_crumb_go(int which)
{
        static p8 to[FILES_PATH];
        struct files_model address_to m = &files.big->model;
        struct files_trail address_to trail = &files.trail;
        bipolar handle, error;
        int seen, came;

        if (which < 0 || which >= trail->count || which == trail->current)
                return;
        files_trail_path(trail, which, to);
        if (which > trail->current)
        {
                if ((seen = files_trail_open(trail, which, &handle, &error)) != FILES_SAME)
                {
                        files_lost_say(which, seen == FILES_MISSING ? LANGUAGE_files_gone : seen == FILES_REFUSED ? files_state_said(files_state_of(error)) : LANGUAGE_files_not_folder);
                        files_trail_lose(trail, which);
                        files.dirty = true;
                        return;
                }
                files_model_take(m, to, handle);
                files_moved();
                return;
        }
        came = files_model_try(m, to);
        if (came == FILES_READING)
        {
                // Up the trail: the keys are on the folder of it come up through, as Up puts them on the folder come out of.
                static p8 through[FILES_NAME];
                positive length = (positive)(trail->to[which + 1] - trail->from[which + 1]);

                memory_copy(through, trail->path + trail->from[which + 1], length < FILES_NAME ? length : 0);
                through[length < FILES_NAME ? length : 0] = end;
                files_model_came(m, through);
                files_moved();
                return;
        }
        files.said = files_state_said(came);
        files.dirty = true;
}

// Along the trail by one folder: up it (-1) or down it (1), as far as it goes.
static fn files_crumb_step(int by)
{
        files_crumb_go(files.trail.current + by);
}

// One folder of the trail becomes the field. When the press before this one went to that folder, that move is taken back first, so
// what is shown is what was shown before the two presses.
static fn files_part_edit(int which, b32 pressed)
{
        if (pressed && files.trail.current == which && files.crumb_from != which && files_model_back(&files.big->model, false))
                files_moved();
        files.part = which;
        files_edit(FILES_EDIT_PART);
}

// The keys while they are in the trail. False for a key that is not the trail's (the rows have it then).
static b32 files_crumb_key(const struct window_event address_to event)
{
        b32 control = (event->flags & WINDOW_KEY_CONTROL) != 0, alt = (event->flags & WINDOW_KEY_ALT) != 0;
        int key = (int)event->a, to;

        if (control && key == FILES_KEY_C)
        {
                files_trail_copy();
                return true;
        }
        if (control || alt)
                return false;
        switch (key)
        {
        case UI_KEY_LEFT:
        case UI_KEY_RIGHT:
                to = files.crumb_focus + ui_arrow(key);
                files.crumb_focus = to < 0 ? 0 : to >= files.trail.count ? files.trail.count - 1 : to;
                break;
        case UI_KEY_HOME: files.crumb_focus = 0; break;
        case UI_KEY_END: files.crumb_focus = files.trail.count - 1; break;
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
                if (!(event->flags & WINDOW_KEY_REPEAT))
                        to = files.crumb_focus, files_crumb_go(to), files.crumb_keys = true, files.crumb_focus = to;
                break;
        case FILES_KEY_F2:
                files_part_edit(files.crumb_focus, false);
                break;
        case UI_KEY_TAB:
                // Tab goes on to the folder tree (Shift+Tab is the band's buttons', the kit's).
                if (event->flags & WINDOW_KEY_SHIFT)
                        return false;
                files.crumb_keys = false, files.tree_keys = true;
                files.tree_at = files.tree_shown != FILES_NO ? files.tree_shown : 0;
                break;
        case UI_KEY_ESCAPE:
        case UI_KEY_DOWN:
                files.crumb_keys = false;
                break;
        default: return false;
        }
        files.dirty = true;
        return true;
}

// Another folder is shown: its rows from the top, nothing said of the folder before.
static fn files_moved(void)
{
        struct files_model address_to m = &files.big->model;

        files.editing = 0, files.crumb_hover = -1, files.armed = false, files.big->note[0] = 0;
        // The trail is told where the person is now: a folder on it moves the place, any other is a new branch (files_trail_note),
        // and the listings kept are the trail's: one whose folder has left it is let go.
        files_trail_note(&files.trail, m->path, m->handle >= 0 ? &m->folder : null);
        files_keep_trim(m, &files.trail);
        files.crumb_focus = files.trail.current;
        files_tree_follow();
        files.clicked = FILES_NO, files.said = 0;
        // The rows: where they were left for a listing shown as it was left, which is then looked at again at once; else from the top.
        files.reseat = true;
        files.next_look = (b64)ui_kit.now + (m->restored ? 0 : FILES_LOOK_NS);
        files.dirty = true;
}

// Into the entry at a place, if it is a folder and is still the one listed. One that is not what was listed has the folder read again.
static fn files_open(p32 place)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, place);
        int came;

        if (!entry)
                return;
        came = files_model_enter(m, place);
        if (came == FILES_SAME)
        {
                files_moved();
                return;
        }
        if (came == FILES_REFUSED)
                files.said = entry->kind == FILES_KIND_LINK ? LANGUAGE_files_link : 0;
        else
        {
                files.said = LANGUAGE_files_changed;
                files_model_reload(m);
        }
        files.dirty = true;
}

// The band's buttons: Back, Forward, Up.
static b32 files_act(int which)
{
        struct files_model address_to m = &files.big->model;

        if (which == 2 ? files_model_up(m) : files_model_back(m, which == 1))
                files_moved();
        return true;
}

/* ---- the rubber band ---- */

// Whether a press at (x, y) begins a rubber band: on the rows' ground past the last row, or on a row away from its name (the columns
// after the first, where the hand would only choose it).
static b32 files_band_at(int x, int y)
{
        struct ui_list address_to l = &files.big->list;
        int row;

        if (x < l->x || x >= l->x + l->w || y < l->y || y >= l->y + l->h)
                return false;
        row = ui_list_hit(&files.ui, l, x, y);
        return row < 0 || (files.view == FILES_VIEW_DETAILS && ui_point(x, files.columns.x, files.columns.w) >= files.columns.at[1]);
}

/* ---- the menu and the properties sheet ---- */

// Words from outside for a menu's line: a name's & doubled, which the menu would read as the next letter's key.
static fn files_sheet_words(char address_to into, positive room, const char address_to words)
{
        positive used = 0;

        for (positive at = 0; words[at] && used + 2 < room; at++)
        {
                if (words[at] == '&')
                        into[used++] = '&';
                into[used++] = words[at];
        }
        into[used] = 0;
}

// A moment as a sheet says it: the day, the minute and the second, in the machine's own zone.
static fn files_sheet_time(char address_to into, b64 seconds)
{
        files_time_text(into, seconds);
        if (into[0])
        {
                tm broken;
                time_t at = (time_t)seconds;

                if (localtime_r(&at, &broken))
                        into[16] = ':', into[17] = (char)('0' + broken.tm_sec / 10), into[18] = (char)('0' + broken.tm_sec % 10), into[19] = 0;
        }
}

/*
        The lines of a properties sheet, from what is looked at now (not followed): of the folder shown, of the one entry the cursor is on
        (none chosen), or of the entries chosen together. Its name, what it is (a link with what it names), its size (a folder's, and many
        entries', what the move's count found, or that it is being measured), the folder it is in, when it was changed and made, its
        permissions as ls writes them and in octal, and who owns it by number. How many lines.
*/
static int files_sheet_fill(void)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, m->cursor);
        b32 many = !files.sheet_folder && m->chosen_count > 1;
        char words[FILES_NAME * 3 + 4], size[16], bytes[24], when[24], mode[12], octal[8];
        file_facts facts;
        int lines = 0;

        if (files.sheet_folder ? m->handle < 0 || file_look_code(m->handle, (string_address) "", AT_EMPTY_PATH, &facts) < 0
                               : !entry || files_model_check(m, m->cursor, &facts) != FILES_SAME)
                return 0;
        // The name, or how many.
        if (many)
                language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_items), (int)m->chosen_count);
        else
        {
                const p8 address_to name = files.sheet_folder ? m->path : files_model_name(m, entry);

                ui_label_foreign(words, sizeof(words), name, string_length((string_address)name));
                files_sheet_words(files.big->sheet[lines++], sizeof(files.big->sheet[0]), words);
        }
        // What it is.
        if (!many)
        {
                p32 format = facts.mode & MODE_FORMAT;

                if (format == MODE_LINK)
                {
                        p8 points[FILES_PATH];
                        bipolar length = (bipolar)system_call_4(syscall(readlinkat), (positive)m->handle, (positive)files_model_name(m, entry), (positive)points,
                                                                sizeof(points) - 1);
                        char named[FILES_NAME * 3 + 4];

                        named[0] = 0;
                        if (length >= 0)
                                ui_label_foreign(words, sizeof(words), points, (positive)length), files_sheet_words(named, sizeof(named), words);
                        language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_link), named);
                }
                else
                        string_copy_bounded((string_address)files.big->sheet[lines++], (string_address)(format == MODE_DIRECTORY ? WORDS(files_sheet_folder)
                                                                                                      : format == MODE_FILE      ? WORDS(files_sheet_file)
                                                                                                                                 : WORDS(files_sheet_other)),
                                            sizeof(files.big->sheet[0]));
        }
        // The size: a file's own; a folder's and many entries' as measured.
        if (!many && (facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
        {
                files_size_text(size, facts.size);
                bytes[positive_into((p8 address_to)bytes, (positive)facts.size)] = 0;
                language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_size), size, bytes);
        }
        else if (files.measuring)
                string_copy_bounded((string_address)files.big->sheet[lines++], (string_address)WORDS(files_sheet_measuring), sizeof(files.big->sheet[0]));
        else
        {
                files_size_text(size, files.big->move.bytes_total);
                // What a folder holds: the count is of what is in it, not of the folder itself.
                p32 items = files.big->move.items_total;

                items -= !many && items ? 1 : 0;
                language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_size_in), size, (int)items);
        }
        // Where it is.
        if (!files.sheet_folder)
        {
                char escaped[FILES_NAME * 3 + 4];

                ui_label_foreign(words, sizeof(words), m->path, string_length((string_address)m->path));
                files_sheet_words(escaped, sizeof(escaped), words);
                language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_in), escaped);
        }
        if (many)
                return lines;
        files_sheet_time(when, facts.modified.seconds);
        language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_modified), when);
        if (facts.created.seconds)
        {
                files_sheet_time(when, facts.created.seconds);
                language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_created), when);
        }
        // The permissions, as ls writes them (the set-id and sticky bits in the execute places) and in octal.
        for (int bit = 0; bit < 9; bit++)
                mode[bit] = facts.mode & (0400 >> bit) ? "rwxrwxrwx"[bit] : '-';
        if (facts.mode & 04000)
                mode[2] = mode[2] == 'x' ? 's' : 'S';
        if (facts.mode & 02000)
                mode[5] = mode[5] == 'x' ? 's' : 'S';
        if (facts.mode & 01000)
                mode[8] = mode[8] == 'x' ? 't' : 'T';
        mode[9] = 0;
        for (int digit = 0; digit < 4; digit++)
                octal[digit] = (char)('0' + (facts.mode >> (9 - 3 * digit) & 7));
        octal[4] = 0;
        language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_mode), mode, octal);
        language_format(files.big->sheet[lines++], sizeof(files.big->sheet[0]), WORDS(files_sheet_owner), (int)facts.owner, (int)facts.group);
        return lines;
}

// The sheet shown as the menu's lines at (x, y) in the window, where it was asked for.
static fn files_sheet_show(void)
{
        struct ui_popmenu address_to menu = &files.menu.menu;
        int lines = files_sheet_fill();

        if (!lines)
                return;
        menu->count = 0;
        for (int at = 0; at < lines; at++)
                ui_popmenu_add(menu, files.big->sheet[at], null, at ? 0 : UI_MENU_HEADER, FILES_MENU_SHEET);
        files.menu.field = null;
        ui_popmenu_open(&files.menu, &files.own, &files.ui, files.sheet_x, files.sheet_y, 0, 0, WINDOW_POPUP_POINT, false);
}

/*
        A properties sheet: of the folder shown (folder), else of the entries chosen or the cursor's, at (x, y) in the window (-1: at the
        cursor's row). A folder's size, and that of many entries, is counted on the move's thread (files_move with FILES_MOVE_MEASURE) and the
        sheet shown again when it is whole; while a copy or a move is under way it is not measured.
*/
static fn files_sheet_open(b32 folder, int x, int y)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, m->cursor);
        b32 measure = folder || (m->chosen_count > 1) || (entry && entry->kind == FILES_KIND_DIRECTORY);

        if (!folder && !entry)
                folder = true, measure = true;
        if (x < 0)
        {
                struct ui_list address_to l = &files.big->list;

                x = l->x + ui_dev(&files.ui, 40), y = l->y + (m->cursor != FILES_NO ? (int)m->cursor * ui_dev(&files.ui, UI_ROW) - l->scroll.scroll : 0);
        }
        files.sheet = true, files.sheet_folder = folder, files.sheet_x = x, files.sheet_y = y, files.measuring = false;
        if (measure && !files.moving)
        {
                p8 address_to list = null;
                positive used = 0, room = 0;
                p32 count = 0;
                bipolar into = m->handle >= 0 ? system_open_at(m->handle, (string_address) ".", FILE_READ | O_DIRECTORY | O_CLOEXEC) : -1;

                // The sources: the folder shown's own path, else each entry's, chosen or the cursor's.
                for (p32 place = 0; place < (folder ? 1 : files_model_count(m)); place++)
                {
                        const struct files_entry address_to e = folder ? null : files_model_entry(m, place);
                        positive length = string_length((string_address)m->path), name = e ? e->length : 0;

                        if (!folder && (m->chosen_count > 1 ? !files_model_chosen(m, place) : place != m->cursor))
                                continue;
                        if (!array_store_reserve(list, room, used, used + length + name + 2, 4096))
                                break;
                        memory_copy(list + used, m->path, length);
                        if (e)
                        {
                                if (length > 1)
                                        list[used + length++] = '/';
                                memory_copy(list + used + length, files_model_name(m, e), name);
                                length += name;
                        }
                        list[used + length] = 0;
                        used += length + 1, count++;
                }
                files_move_end(&files.big->move);
                if (into >= 0 && count && files_move_begin(&files.big->move, FILES_MOVE_MEASURE, into, list, count))
                {
                        files_move_start(&files.big->move);
                        files.moving = files.measuring = true;
                }
                else
                        files_move_end(&files.big->move);
                array_store_release(list, room, used);
        }
        files_sheet_show();
}

// Something chosen to open with: the editor, a file at a time; a terminal, in the folder (the cursor's, or the folder shown).
static fn files_open_with(b32 editor)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, m->cursor);
        static p8 path[FILES_PATH];
        string_address face = files_app.font ? files_app.font : (string_address)UI_FONT_REGULAR;

        if (editor)
        {
                positive length = string_length((string_address)m->path);
                string_address words[5] = {(string_address) "/editor", (string_address) "-f", face, (string_address)path, null};

                if (!entry || entry->kind == FILES_KIND_DIRECTORY || length + entry->length + 2 > sizeof(path))
                        return;
                memory_copy(path, m->path, length);
                if (length > 1)
                        path[length++] = '/';
                memory_copy(path + length, files_model_name(m, entry), (positive)entry->length + 1);
                (void)ui_start_apart(words, 4);
                return;
        }
        {
                string_address words[2] = {(string_address) "/terminal", null};
                bipolar folder = entry && entry->kind == FILES_KIND_DIRECTORY ? files_drop_folder((int)m->cursor) : files_drop_folder(-1);

                if (folder < 0)
                {
                        files.said = LANGUAGE_files_changed;
                        return;
                }
                (void)ui_start_apart_at(folder, words, 1);
                system_close(folder);
        }
}

/*
        The browser's menu at (x, y) in the window: of the rows (on) or of the folder shown. The rows' is Open, Open in the editor (a
        file), Open a terminal here (a folder), Cut, Copy, Paste, Rename, Delete and Properties; the folder's Paste, New folder, Select
        all, Show hidden files (checked when they are), Open a terminal here and Properties. Each item's keys are said beside it.
*/
static fn files_menu_open(b32 on, int x, int y)
{
        struct files_model address_to m = &files.big->model;
        struct ui_popmenu address_to menu = &files.menu.menu;
        const struct files_entry address_to entry = files_model_entry(m, m->cursor);
        b32 folder = entry && entry->kind == FILES_KIND_DIRECTORY;

        menu->count = 0;
        files.sheet = false;
        if (on && entry)
        {
                ui_popmenu_add(menu, WORDS(menu_open), "Enter", 0, FILES_MENU_OPEN);
                if (!folder)
                        ui_popmenu_add(menu, WORDS(menu_open_editor), null, 0, FILES_MENU_EDITOR);
                else
                        ui_popmenu_add(menu, WORDS(menu_open_terminal), null, 0, FILES_MENU_TERMINAL);
                ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(menu, WORDS(menu_cut), "Ctrl+X", 0, FILES_MENU_CUT);
                ui_popmenu_add(menu, WORDS(menu_copy), "Ctrl+C", 0, FILES_MENU_COPY);
                ui_popmenu_add(menu, WORDS(menu_paste), "Ctrl+V", 0, FILES_MENU_PASTE);
                ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(menu, WORDS(menu_rename), "F2", m->chosen_count > 1 ? UI_MENU_DISABLED : 0, FILES_MENU_RENAME);
                ui_popmenu_add(menu, WORDS(menu_delete), "Delete", 0, FILES_MENU_DELETE);
                ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(menu, WORDS(menu_properties), "Alt+Enter", 0, FILES_MENU_PROPERTIES);
        }
        else
        {
                ui_popmenu_add(menu, WORDS(menu_paste), "Ctrl+V", 0, FILES_MENU_PASTE);
                ui_popmenu_add(menu, WORDS(menu_new_folder), "Ctrl+Shift+N", 0, FILES_MENU_NEW_FOLDER);
                ui_popmenu_add(menu, WORDS(menu_select_all), "Ctrl+A", files_model_count(m) ? 0 : UI_MENU_DISABLED, FILES_MENU_ALL);
                ui_popmenu_add(menu, WORDS(menu_show_hidden), "Ctrl+H", UI_MENU_CHECK | (m->hidden ? UI_MENU_ON : 0), FILES_MENU_HIDDEN);
                ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(menu, WORDS(menu_view_details), "Ctrl+1", UI_MENU_RADIO | (files.view == FILES_VIEW_DETAILS ? UI_MENU_ON : 0), FILES_MENU_DETAILS);
                ui_popmenu_add(menu, WORDS(menu_view_icons), "Ctrl+2", UI_MENU_RADIO | (files.view == FILES_VIEW_ICONS ? UI_MENU_ON : 0), FILES_MENU_ICONS);
                ui_popmenu_add(menu, WORDS(menu_view_list), "Ctrl+3", UI_MENU_RADIO | (files.view == FILES_VIEW_LIST ? UI_MENU_ON : 0), FILES_MENU_LIST);
                ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(menu, WORDS(menu_open_terminal), null, 0, FILES_MENU_TERMINAL);
                ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(menu, WORDS(menu_properties), null, 0, FILES_MENU_PROPERTIES);
                // The folder's own: nothing chosen, the cursor off the rows, so what the items do is the folder's.
                files_model_choose_none(m);
                m->cursor = FILES_NO, m->revision++;
        }
        files.sheet_folder = !on, files.sheet_x = x, files.sheet_y = y;
        files.menu.field = null;
        ui_popmenu_open(&files.menu, &files.own, &files.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false);
        files.dirty = true;
}

// The right button at (x, y): the row there chosen (alone, unless it is one of those chosen) and its menu; no row, the folder's.
static fn files_menu_at(int x, int y)
{
        struct files_model address_to m = &files.big->model;
        int row = ui_list_hit(&files.ui, &files.big->list, x, y);

        if (files.editing)
                files_edit(0);
        if (row >= 0 && files_model_entry(m, (p32)row))
        {
                if (!files_model_chosen(m, (p32)row))
                        files_model_choose(m, (p32)row, FILES_CHOOSE_ONLY);
                m->cursor = (p32)row, m->revision++;
        }
        files_menu_open(row >= 0, x, y);
}

// Shift+F10 and the menu key: the menu of the row the keys are on, at it; of the folder where there is none.
static fn files_menu_keys(void)
{
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        int x = l->x + ui_dev(&files.ui, 40), y = l->y + ui_dev(&files.ui, UI_ROW) / 2;

        if (m->cursor != FILES_NO)
                y += (int)m->cursor * ui_dev(&files.ui, UI_ROW) - l->scroll.scroll;
        files_menu_open(m->cursor != FILES_NO, x, y);
}

static fn files_delete(void);
static fn files_new_folder(void);
static fn files_open(p32 place);

// The rows shown another way (FILES_VIEW_*): the cursor kept in sight.
static fn files_view(int view)
{
        files.view = view;
        files.big->list.scroll.scroll = 0;
        files.dirty = true;
        files_show_cursor();
}

// An item of the browser's menu, done.
static fn files_menu_do(int id)
{
        struct files_model address_to m = &files.big->model;

        switch (id)
        {
        case FILES_MENU_OPEN: files_open(m->cursor); break;
        case FILES_MENU_EDITOR: files_open_with(true); break;
        case FILES_MENU_TERMINAL: files_open_with(false); break;
        case FILES_MENU_CUT: files_rows_copy(true); break;
        case FILES_MENU_COPY: files_rows_copy(false); break;
        case FILES_MENU_PASTE: files_paste(); break;
        case FILES_MENU_RENAME: files_edit(FILES_EDIT_NAME); break;
        case FILES_MENU_DELETE: files_delete(); break;
        case FILES_MENU_NEW_FOLDER: files_new_folder(); break;
        case FILES_MENU_PROPERTIES: files_sheet_open(files.sheet_folder, files.sheet_x, files.sheet_y); break;
        case FILES_MENU_HIDDEN:
                m->hidden = !m->hidden;
                files_model_reload(m);
                break;
        case FILES_MENU_DETAILS:
        case FILES_MENU_ICONS:
        case FILES_MENU_LIST:
                files_view(id - FILES_MENU_DETAILS);
                break;
        case FILES_MENU_ALL:
                if (files_model_count(m))
                {
                        files_model_choose(m, 0, FILES_CHOOSE_ONLY);
                        files_model_choose(m, files_model_count(m) - 1, FILES_CHOOSE_RANGE);
                }
                break;
        }
}

// To the tree's node at: its folder shown (opened as a named path: the model says what is there now).
static fn files_tree_go(p32 at)
{
        static p8 path[FILES_PATH];
        int came;

        if (at >= files.big->tree.count || !files_tree_path(&files.big->tree, at, path))
                return;
        came = files_model_try(&files.big->model, (string_address)path);
        if (came == FILES_READING)
        {
                files_moved();
                return;
        }
        files.said = files_state_said(came);
        files.dirty = true;
}

// A node opened or shut, the keys kept on it.
static fn files_tree_toggle(p32 at)
{
        struct files_tree address_to t = &files.big->tree;

        if (at >= t->count)
                return;
        if (t->node[at].open)
                files_tree_shut(t, at);
        else
                files_tree_open(t, at);
        files.tree_shown = files_tree_reveal(t, files.big->model.path);
        files.dirty = true;
}

/*
        The keys while they are in the tree: Up and Down move, Home and End to its ends, Right opens a node (or goes to its first folder
        when it is open), Left shuts it (or goes to its parent when it is shut), Enter and Space show its folder, Tab or Escape give the
        keys back to the rows. False for a key that is not the tree's.
*/
static b32 files_tree_key(const struct window_event address_to event)
{
        struct files_tree address_to t = &files.big->tree;
        int key = (int)event->a, step;
        p32 at = files.tree_at < t->count ? files.tree_at : 0;

        if (event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT))
                return false;
        switch (key)
        {
        case UI_KEY_UP: at = at ? at - 1 : 0; break;
        case UI_KEY_DOWN: at = at + 1 < t->count ? at + 1 : at; break;
        case UI_KEY_HOME: at = 0; break;
        case UI_KEY_END: at = t->count ? t->count - 1 : 0; break;
        case UI_KEY_LEFT:
        case UI_KEY_RIGHT:
                step = ui_arrow(key);
                if (step > 0 && !t->node[at].open)
                        files_tree_toggle(at);
                else if (step > 0 && at + 1 < t->count && t->node[at + 1].depth > t->node[at].depth)
                        at++;
                else if (step < 0 && t->node[at].open)
                        files_tree_toggle(at);
                else if (step < 0 && files_tree_parent(t, at) != FILES_NO)
                        at = files_tree_parent(t, at);
                break;
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
        case UI_KEY_SPACE:
                if (!(event->flags & WINDOW_KEY_REPEAT))
                        files_tree_go(at);
                break;
        case UI_KEY_TAB:
        case UI_KEY_ESCAPE:
                files.tree_keys = false;
                break;
        default: return false;
        }
        files.tree_at = at;
        files.big->tree_list.focus = (int)at;
        ui_list_reveal(&files.ui, &files.big->tree_list);
        files.dirty = true;
        return true;
}

// The find field's text made the model's filter: the rows are those whose names hold it, the cursor on the first of them.
static fn files_find_take(void)
{
        static p8 text[FILES_NAME];
        positive length = ui_text_string(&files.finder, text, sizeof(text));

        files_model_filter(&files.big->model, text, length < sizeof(text) ? length : sizeof(text) - 1);
        files.reseat = true, files.big->model.seat = 0;
        files.dirty = true;
}

// Finding begun (the field at the foot, with the keys and what it held chosen) or ended (the field gone, every entry shown again).
static fn files_find(b32 on)
{
        if (on)
        {
                if (!files.finder.doc.selections && ui_field_open(&files.finder, &files.own, ""))
                        files.finder.most = FILES_NAME - 1;
                files.finding = files.find_keys = true;
                doc_select_set(&files.finder.doc, 0, doc_length(&files.finder.doc));
        }
        else
        {
                files.finding = files.find_keys = false;
                ui_text_set(&files.finder, (const p8 address_to) "", 0);
                files_model_filter(&files.big->model, (const p8 address_to) "", 0);
        }
        files.crumb_keys = files.tree_keys = false;
        files.dirty = true;
}

// A letter typed on the rows: added to what was typed in the last second (else it starts again), and the cursor to the first name that
// starts with it (letters of either case alike), from the cursor on and round.
static fn files_type_ahead(p32 code)
{
        struct files_model address_to m = &files.big->model;
        p32 count = files_model_count(m), start;
        b64 now = (b64)ui_kit.now;
        p8 bytes[4];
        positive length = memory_utf8_encode(bytes, sizeof(bytes), code);

        if (now - files.typed_at > FILES_TYPE_NS)
                files.typed_length = 0;
        files.typed_at = now;
        if (!length || files.typed_length + length > sizeof(files.typed))
                return;
        memory_copy(files.typed + files.typed_length, bytes, length);
        files.typed_length += length;
        // One letter again and again steps through the names that start with it; more letters keep to the name they began on.
        start = m->cursor == FILES_NO ? 0 : files.typed_length == length ? m->cursor + 1 : m->cursor;
        for (p32 k = 0; k < count; k++)
        {
                p32 place = (start + k) % count;
                const struct files_entry address_to entry = files_model_entry(m, place);

                if (entry->length >= files.typed_length && !memory_compare_ascii_case(files_model_name(m, entry), files.typed, files.typed_length))
                {
                        files_model_choose(m, place, FILES_CHOOSE_ONLY);
                        files_show_cursor();
                        return;
                }
        }
}

static b32 files_key(const struct window_event address_to event)
{
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        b32 alt = (event->flags & WINDOW_KEY_ALT) != 0, control = (event->flags & WINDOW_KEY_CONTROL) != 0, shift = (event->flags & WINDOW_KEY_SHIFT) != 0;
        int across = l->tiles ? ui_list_per(&files.ui, l) : 1, page = l->scroll.view / ui_list_unit(&files.ui, l) - 1, key = (int)event->a;
        bipolar by = 0;

        page = page < 1 ? 1 : page;
        // Delete asks and Delete again does; Escape stops a delete under way; any other key leaves what was asked about.
        if (key == UI_KEY_DELETE)
        {
                if (!(event->flags & WINDOW_KEY_REPEAT))
                        files_delete();
                return true;
        }
        if (key == UI_KEY_ESCAPE && files.big->job.state == FILES_JOB_RUNNING)
        {
                files_job_cancel(&files.big->job);
                return true;
        }
        if (key == UI_KEY_ESCAPE && files.moving && files_move_running(&files.big->move))
        {
                files_move_cancel(&files.big->move);
                return true;
        }
        if (files.armed || files.big->note[0])
                files.armed = false, files.big->note[0] = 0, files.dirty = true;
        // Along the trail, wherever the keys are; then the trail's own keys while they are in it.
        if (alt && (key == UI_KEY_UP || key == UI_KEY_DOWN))
        {
                files_crumb_step(key == UI_KEY_UP ? -1 : 1);
                return true;
        }
        if (files.crumb_keys && files_crumb_key(event))
                return true;
        if (files.tree_keys && files_tree_key(event))
                return true;
        // Tab: the rows, the trail, the tree, and the rows again (Shift+Tab is the band's buttons', the kit's).
        if (key == UI_KEY_TAB && !shift && !control && !alt)
        {
                if (files.crumb_keys)
                        files.crumb_keys = false, files.tree_keys = true, files.tree_at = files.tree_shown != FILES_NO ? files.tree_shown : 0;
                else
                        files.crumb_keys = true, files.crumb_focus = files.trail.current;
                files.dirty = true;
                return true;
        }
        if (key == FILES_KEY_F2)
        {
                files_edit(FILES_EDIT_NAME);
                return true;
        }
        if (control && shift && key == FILES_KEY_N)
        {
                files_new_folder();
                return true;
        }
        if (alt && (key == UI_KEY_LEFT || key == UI_KEY_RIGHT))
                return files_act(key == UI_KEY_RIGHT);
        if (key == UI_KEY_BACKSPACE)
                return files_act(2);
        if (key == FILES_KEY_F5 || (control && key == FILES_KEY_R))
        {
                files.said = 0;
                files_model_reload(m);
                files.dirty = true;
                return true;
        }
        if (control && key == FILES_KEY_H)
        {
                // Names that start with a dot, shown or not: the folder is read again with the other answer.
                m->hidden = !m->hidden;
                files_model_reload(m);
                files.dirty = true;
                return true;
        }
        if (control && (key == FILES_KEY_C || key == FILES_KEY_X))
        {
                files_rows_copy(key == FILES_KEY_X);
                return true;
        }
        if (control && key == FILES_KEY_V)
        {
                files_paste();
                return true;
        }
        if (control && key == FILES_KEY_F)
        {
                files_find(true);
                return true;
        }
        if (control && key >= FILES_KEY_1 && key < FILES_KEY_1 + 3)
        {
                files_view(key - FILES_KEY_1);
                return true;
        }
        if (key == UI_KEY_ESCAPE && files.finding)
        {
                files_find(false);
                return true;
        }
        // A letter on the rows: the keys go to the first name that starts with what was typed in the last second.
        if (!control && !alt && event->b > ' ' && event->b != 0x7f && key != UI_KEY_SPACE)
        {
                files_type_ahead((p32)event->b);
                return true;
        }
        if ((shift && key == FILES_KEY_F10) || key == FILES_KEY_MENU)
        {
                files_menu_keys();
                return true;
        }
        if (alt && (key == UI_KEY_ENTER || key == UI_KEY_ENTER_PAD))
        {
                files_sheet_open(false, -1, -1);
                return true;
        }
        if (control && key == FILES_KEY_A)
        {
                if (files_model_count(m))
                {
                        files_model_choose(m, 0, FILES_CHOOSE_ONLY);
                        files_model_choose(m, files_model_count(m) - 1, FILES_CHOOSE_RANGE);
                }
                files.dirty = true;
                return true;
        }
        switch (key)
        {
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
                if (!(event->flags & WINDOW_KEY_REPEAT))
                        files_open(m->cursor);
                return true;
        case UI_KEY_SPACE:
                files_model_choose(m, m->cursor, FILES_CHOOSE_TOGGLE);
                files.dirty = true;
                return true;
        case UI_KEY_UP: by = -across; break;
        case UI_KEY_DOWN: by = across; break;
        case UI_KEY_LEFT:
        case UI_KEY_RIGHT:
                // Over tiles Left and Right go along a line, the way it is read; over rows they are no move.
                if (across < 2 && !l->tiles)
                        return true;
                by = ui_arrow(key);
                break;
        case UI_KEY_PAGE_UP: by = -page * across; break;
        case UI_KEY_PAGE_DOWN: by = page * across; break;
        case UI_KEY_HOME: by = -(bipolar)FILES_MOST; break;
        case UI_KEY_END: by = (bipolar)FILES_MOST; break;
        default: return true;
        }
        // An arrow moves the cursor and chooses where it lands; with Shift, from where the choosing began to there; with Control it moves alone.
        files.said = 0;
        if (files_model_move(m, by) || m->cursor != FILES_NO)
        {
                if (!control)
                        files_model_choose(m, m->cursor, shift ? FILES_CHOOSE_RANGE : FILES_CHOOSE_ONLY);
                files_show_cursor();
        }
        return true;
}

static b32 files_event(const struct window_event address_to event)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        p32 asks;
        int row;

        files_layout();
        // Every press and release disarms a drag of the rows; a press on a row arms it again, below (ui_drag_button). The news of the
        // rows' own drag (its action, its end) is the rows', whatever has the keys: a field open meanwhile does not take it.
        ui_drag_button(&files.drag, event, 0, 0, false);
        if (event->type == WINDOW_EVENT_DRAG && files.drag.live &&
            (event->a == WINDOWS_DRAG_ACTION || event->a == WINDOWS_DRAG_ENDED || event->a == WINDOWS_DRAG_DECLINED))
                return files_drag_event(event);
        // The path's field while it is typed: its keys, its pointer, its menu. Enter goes, Escape and a press elsewhere put the crumbs
        // back (the press goes on to what it is on), Tab finishes a name, and no other key is the rows' while the field has them.
        if (files.editing)
        {
                asks = ui_field_send(ui, files_field(), &files.menu, event);
                if (asks & UI_TEXT_SUBMIT)
                {
                        files_edit_go();
                        return true;
                }
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_COMMIT))
                {
                        files_edit(0);
                        if (event->type == WINDOW_EVENT_KEY)
                                return true;
                        files_layout();
                }
                else if (asks)
                {
                        files.said = 0, files.dirty = true;
                        return true;
                }
                else if (event->type == WINDOW_EVENT_KEY)
                {
                        if (down && event->a == UI_KEY_TAB && !(event->flags & WINDOW_KEY_SHIFT) && files.editing == FILES_EDIT_PATH)
                                files_edit_complete();
                        else if (down && event->a == UI_KEY_TAB && !(event->flags & WINDOW_KEY_SHIFT) && files.editing == FILES_EDIT_PART)
                                files_part_complete();
                        return true;
                }
        }
        else if (event->type == WINDOW_EVENT_KEY && down && (event->flags & WINDOW_KEY_CONTROL) && event->a == FILES_KEY_L)
        {
                files_edit(FILES_EDIT_PATH);
                return true;
        }
        // The find field: its keys while it has them, a press on it gives them to it; Enter gives them to the rows (what it found stays),
        // Escape ends the finding (every entry shown again), and its text is the filter as it is typed.
        else if (files.finding && (files.find_keys || event->type == WINDOW_EVENT_BUTTON))
        {
                b32 on = ui_kit.pointer_x >= files.finder.well_left && ui_kit.pointer_x < files.finder.well_right && ui_kit.pointer_y >= files.finder.well_top &&
                         ui_kit.pointer_y < files.finder.well_bottom;

                if (event->type == WINDOW_EVENT_BUTTON && down && on)
                        files.find_keys = true;
                if (files.find_keys)
                {
                        asks = ui_field_send(ui, &files.finder, &files.menu, event);
                        if (asks & UI_TEXT_SUBMIT)
                                files.find_keys = false;
                        else if (asks & UI_TEXT_CANCEL)
                                files_find(false);
                        else if (asks & UI_TEXT_COMMIT)
                                files.find_keys = false;
                        if (asks & ~(p32)(UI_TEXT_SUBMIT | UI_TEXT_CANCEL | UI_TEXT_COMMIT))
                                files_find_take();
                        files.dirty |= asks != 0;
                        if (asks || event->type == WINDOW_EVENT_KEY)
                                return true;
                }
        }
        // The band's buttons first: a press on one, and the keys while they are on one (Shift+Tab gives them the keys).
        asks = ui_view_event(ui, &files.big->view, event);
        if (asks & UI_VIEW_QUIT)
                return false;
        files.dirty |= (asks & UI_VIEW_DRAW) != 0;
        if (asks & UI_VIEW_TAKEN)
                return true;
        // The columns' header: a title pressed orders the rows by it, an edge held makes a column wider or narrower.
        if (files.view == FILES_VIEW_DETAILS && (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON))
        {
                p32 took = ui_columns_event(ui, &files.columns, event, event->type == WINDOW_EVENT_MOTION ? event->a : ui_kit.pointer_x,
                                            event->type == WINDOW_EVENT_MOTION ? event->b : ui_kit.pointer_y);

                if (took & UI_COLUMNS_SORTED)
                {
                        files_model_sort(m, files_column_sort(files.columns.sorted), files.columns.descending);
                        files_show_cursor();
                }
                files.dirty |= (took & UI_COLUMNS_DRAW) != 0;
                if (took & UI_COLUMNS_TAKEN)
                        return true;
        }
        switch (event->type)
        {
        case WINDOW_EVENT_DRAG:
                return files_drag_event(event);
        case WINDOW_EVENT_CLIP:
                return files_clip_event(event);
        case WINDOW_EVENT_KEY:
                return !down || ui_key_modifier((int)event->a) ? true : files_key(event);
        case WINDOW_EVENT_MOTION:
                // A rubber band held: the rows it crosses are the ones chosen, the cursor on the row the hand is at.
                if (l->band)
                {
                        int first, last;

                        if (ui_list_band_follow(l, event->a, event->b) && ui_list_band_rows(ui, l, &first, &last))
                        {
                                b32 down_band = event->b >= l->y + l->band_at - l->scroll.scroll;

                                files_model_choose(m, (p32)(down_band ? first : last), FILES_CHOOSE_ONLY);
                                files_model_choose(m, (p32)(down_band ? last : first), FILES_CHOOSE_RANGE);
                        }
                        else if (!ui_list_band_rows(ui, l, &first, &last))
                                files_model_choose_none(m), m->revision++;
                        files.dirty = true;
                        break;
                }
                // A row pressed and taken past the slop: the rows dragged out, as files, copied or moved; the list's press is the drag's now.
                if (ui_drag_past(ui, &files.drag, event->a, event->b))
                {
                        struct windows_clip_part parts[2];
                        p8 address_to uris, address_to paths;
                        positive uris_room, paths_room;

                        ui_list_press_cancel(ui, l);
                        files.drag_chosen = files_model_chosen(m, files.drag_place);
                        if (files_uris(files.drag_place, parts, &uris, &uris_room, &paths, &paths_room))
                        {
                                ui_drag_offer(&files.own, &files.drag, parts, 2, WINDOWS_DRAG_COPY | WINDOWS_DRAG_MOVE);
                                files_uris_free(uris, uris_room, paths, paths_room, parts);
                        }
                        files.drag.armed = false;
                        break;
                }
                ui_list_click(ui, l, event, event->a, event->b);
                ui_list_click(ui, &files.big->tree_list, event, event->a, event->b);
                // A crumb under the pointer is the hand's, and is drawn in the ink.
                files.crumb_hover = files_crumb_at(event->a, event->b);
                if (files.crumb_hover >= 0)
                        ui_kit_shape = WINDOW_POINTER_HAND;
                files.dirty = true;
                break;
        case WINDOW_EVENT_BUTTON:
                files.dirty = true;
                if (down)
                        files.armed = false, files.big->note[0] = 0; // a press leaves what Delete asked about
                // The band: a press on a crumb goes to its folder, and one on the band's own ground opens the field.
                if (down && event->a == UI_POINTER_LEFT && !files.editing && ui_kit.pointer_x >= files.crumbs_box.x &&
                    ui_kit.pointer_x < files.crumbs_box.x + files.crumbs_box.w && ui_kit.pointer_y >= files.crumbs_box.y &&
                    ui_kit.pointer_y < files.crumbs_box.y + files.crumbs_box.h)
                {
                        int crumb = files_crumb_at(ui_kit.pointer_x, ui_kit.pointer_y);

                        // A folder of the trail is gone to, and a second press on it in time spells it again (the first one's move
                        // taken back); its ground, and the dots that stand for folders left out, open the field.
                        files.crumb_keys = false;
                        if (crumb >= 0 && crumb == files.crumb_clicked && (b64)ui_kit.now - files.crumb_clicked_at <= ui_click_ns)
                        {
                                files.crumb_clicked = -1;
                                files_part_edit(crumb, true);
                        }
                        else if (crumb >= 0)
                        {
                                files.crumb_from = files.trail.current, files.crumb_clicked = crumb, files.crumb_clicked_at = (b64)ui_kit.now;
                                files_crumb_go(crumb);
                        }
                        else
                                files_edit(FILES_EDIT_PATH);
                        break;
                }
                if (down)
                        files.crumb_keys = false, files.crumb_clicked = -1;
                // The tree: a press on a node's chevron opens or shuts it, anywhere else on it shows its folder; the keys go to the tree.
                if (event->a == UI_POINTER_LEFT && ui_list_hit(ui, &files.big->tree_list, ui_kit.pointer_x, ui_kit.pointer_y) >= 0)
                {
                        struct ui_list address_to tl = &files.big->tree_list;
                        int node = ui_list_click(ui, tl, event, ui_kit.pointer_x, ui_kit.pointer_y);

                        if (down)
                                files.tree_keys = true, files.crumb_keys = false;
                        if (node >= 0 && (p32)node < files.big->tree.count)
                        {
                                files.tree_at = (p32)node;
                                if (ui_list_tree_chevron_at(ui, tl, files.big->tree.node[node].depth, ui_kit.pointer_x))
                                        files_tree_toggle((p32)node);
                                else
                                        files_tree_go((p32)node);
                        }
                        break;
                }
                // A rubber band: a press of the left button on the rows' ground (past the last row) or on a row away from its name (in the
                // columns after the first) begins one, choosing that row as a click would; its release ends it. A press on a row's name chooses
                // it and may drag it out (below).
                if (event->a == UI_POINTER_LEFT && !down && l->band)
                {
                        ui_list_band_end(l);
                        break;
                }
                if (event->a == UI_POINTER_LEFT && down && !files.editing && files_band_at(ui_kit.pointer_x, ui_kit.pointer_y))
                {
                        int at = ui_list_hit(ui, l, ui_kit.pointer_x, ui_kit.pointer_y);

                        files.crumb_keys = files.tree_keys = false;
                        if (at >= 0 && (p32)at == files.clicked && (b64)ui_kit.now - files.clicked_at <= ui_click_ns)
                        {
                                files.clicked = FILES_NO;
                                files_open((p32)at);
                                break;
                        }
                        files_model_choose_none(m);
                        if (at >= 0)
                                files_model_choose(m, (p32)at, FILES_CHOOSE_ONLY), files.clicked = (p32)at, files.clicked_at = (b64)ui_kit.now;
                        else
                                m->cursor = FILES_NO, files.clicked = FILES_NO;
                        m->revision++;
                        ui_list_band_begin(l, ui_kit.pointer_x, ui_kit.pointer_y);
                        break;
                }
                // The right button: the menu of the row under the pointer (chosen alone first, unless it is among those chosen), or of the
                // folder shown where no row is.
                if (event->a == UI_POINTER_RIGHT)
                {
                        if (down)
                                files_menu_at(ui_kit.pointer_x, ui_kit.pointer_y);
                        break;
                }
                row = ui_list_click(ui, l, event, ui_kit.pointer_x, ui_kit.pointer_y);
                // The press itself puts the cursor on its row, as the list lights it; a mouse's left press on a row may drag it out.
                if (down && l->down >= 0 && (p32)l->down != m->cursor)
                        m->cursor = (p32)l->down, m->revision++;
                ui_drag_button(&files.drag, event, ui_kit.pointer_x, ui_kit.pointer_y, down && l->down >= 0);
                if (files.drag.armed)
                        files.drag_place = (p32)l->down;
                if (row >= 0)
                {
                        p32 place = (p32)row;
                        b32 again = place == files.clicked && (b64)ui_kit.now - files.clicked_at <= ui_click_ns;

                        files.said = 0;
                        // A finger's tap opens; a mouse's click chooses, and a second one on the row in time opens.
                        if ((event->flags & WINDOW_KEY_TOUCH) || again)
                        {
                                files.clicked = FILES_NO;
                                files_open(place);
                                break;
                        }
                        files_model_choose(m, place, event->flags & WINDOW_KEY_CONTROL ? FILES_CHOOSE_TOGGLE : event->flags & WINDOW_KEY_SHIFT ? FILES_CHOOSE_RANGE : FILES_CHOOSE_ONLY);
                        files.clicked = place, files.clicked_at = (b64)ui_kit.now;
                }
                break;
        }
        return true;
}

/* ---- the picture ---- */

static fn files_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &files.ui;
        struct ui_list address_to l = &files.big->list;

        ui_begin(ui, pixels, files.own.table->stride, files.width, files.height);
        files_layout();
        l->lit = l->focus >= 0, l->keys = files.focused && !files.big->view.group.keys && !files.crumb_keys && !files.tree_keys;
        files.big->tree_list.lit = files.big->tree_list.focus >= 0, files.big->tree_list.keys = files.focused && files.tree_keys;
        ui_view_draw(ui, &files.big->view, files.focused);
        if (files.view == FILES_VIEW_DETAILS)
                ui_columns_draw(ui, &files.columns);
        if (!files.editing || files.editing == FILES_EDIT_PART)
                files_crumbs_draw();
        if (files.editing)
                ui_text_overlay(ui, files_field()); // a finger's selection in the field: its handles, over the rows
        if (files.finding)
                ui_text_overlay(ui, &files.finder);
        files.seen = files.big->model.revision;
}

// One picture, into the buffer the compositor is not reading; false when there is none yet.
/*
        The timer. While a folder is read, steps of it for FILES_TURN_NS and back to the events, the entries so far drawn every
        FILES_SHOW_NS and the whole listing when it is whole. Else the folder shown is looked at again every FILES_LOOK_NS (not while
        the window is put away): one that changed is read again, one that is gone says so.
*/
static long files_tick(long timeout, long address_to other)
{
        struct files_model address_to m = &files.big->model;
        b64 now = (b64)ui_kit.now, wait;

        (void)other;
        // The field's caret blinks while the path is typed, and the find field's while it has the keys.
        if (files.editing)
        {
                b32 redraw = false;

                timeout = ui_text_timeout(&files.ui, files_field(), timeout, &redraw);
                files.dirty |= redraw;
        }
        if (files.finding && files.find_keys)
        {
                b32 redraw = false;

                timeout = ui_text_timeout(&files.ui, &files.finder, timeout, &redraw);
                files.dirty |= redraw;
        }
        // A copy or a move under way: stepped here when it has no thread of its own, the foot counting while it runs, and when it ends
        // the foot says what it came to and the folder is read again.
        if (files.moving)
        {
                struct files_move address_to job = &files.big->move;
                b64 began = (b64)ui_kit_clock();

                while (!job->worker && files_move_running(job) && (b64)ui_kit_clock() - began < FILES_TURN_NS)
                        files_move_step(job);
                if (!files_move_running(job) && files.measuring)
                {
                        // A sheet's size, measured: the sheet again with it, if it is still open.
                        files.measuring = false;
                        if (files.sheet && files.menu.open)
                                files_sheet_show();
                        files_move_end(job);
                        files.moving = false;
                }
                else if (!files_move_running(job))
                {
                        files_move_said();
                        files_move_end(job);
                        files.moving = false;
                        files_model_reload(m);
                        files.dirty = true;
                }
                else if (now >= files.next_progress)
                        files.next_progress = now + FILES_SHOW_NS, files.dirty = true;
                if (files.moving)
                        timeout = job->worker ? (timeout < 0 || timeout > FILES_SHOW_NS ? (long)FILES_SHOW_NS : timeout) : 1;
        }
        // A delete under way: steps of it for a few milliseconds and back to the events; when it ends the foot says what it came to and
        // the folder is read again.
        if (files.big->job.state == FILES_JOB_RUNNING)
        {
                struct files_job address_to job = &files.big->job;
                b64 began = (b64)ui_kit_clock();
                b32 more = true;

                while (more && (b64)ui_kit_clock() - began < FILES_TURN_NS)
                        more = files_job_step(m, job, 16);
                if (!more)
                {
                        char at[FILES_NAME * 3 + 4];

                        ui_label_foreign(at, 96, job->at, string_length(job->at));
                        if (job->state == FILES_JOB_FAILED)
                                language_format(files.big->note, sizeof(files.big->note), WORDS(files_delete_stopped), (int)job->removed, at);
                        else
                                language_format(files.big->note, sizeof(files.big->note), WORDS(files_deleted), (int)job->removed);
                        files_job_end(job);
                        files_model_reload(m);
                }
                if (!more || now >= files.next_show)
                        files.next_show = now + FILES_SHOW_NS, files.dirty = true;
                return 1;
        }
        if (m->walking)
        {
                b64 began = (b64)ui_kit_clock();
                b32 more = true;

                while (more && (b64)ui_kit_clock() - began < FILES_TURN_NS)
                        more = files_model_step(m, FILES_STEP);
                if (!more || now >= files.next_show)
                {
                        files.next_show = now + FILES_SHOW_NS;
                        files.dirty |= files.seen != m->revision;
                        if (!more)
                                files_show_cursor();
                }
                return more ? 1 : timeout;
        }
        if (files.seen != m->revision)
                files.dirty = true;
        if (now >= files.next_look && !(files.own.table->state[files.own.row] & WINDOW_MINIMIZED))
        {
                int became = files_model_stale(m);

                if (became == FILES_STALE)
                        files_model_reload(m);
                files.dirty |= became != FILES_FRESH;
                files.next_look = now + FILES_LOOK_NS;
                if (m->walking)
                        return 1;
        }
        wait = files.next_look - now;
        return timeout < 0 || (b64)timeout > wait ? (long)(wait > 0 ? wait : 1) : timeout;
}

static struct ui_app files_app;

static int files_open_window(void)
{
        if (!(files.big = ui_app_map(&files_app, sizeof(struct files_big))))
                return 1;
        if (!files_model_open(&files.big->model))
        {
                compositor_kmsg((string_address) "files: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }
        files.clicked = FILES_NO, files.crumb_hover = -1, files.crumb_clicked = -1, files.drop_row = -1;
        files.columns.width[1] = 90, files.columns.width[2] = 110, files.columns.width[3] = 140;
        files.columns.sorted = 0, files.columns.hover = files.columns.held = -1;
        files_move_end(&files.big->move);
        if (!files_tree_begin(&files.big->tree))
                return 1;
        if (!files.start || !files_model_go(&files.big->model, files.start))
                files_model_go(&files.big->model, (string_address) "/");
        files.next_look = (b64)ui_kit.now + FILES_LOOK_NS;
        files_trail_note(&files.trail, files.big->model.path, files.big->model.handle >= 0 ? &files.big->model.folder : null);
        files_tree_follow();
        ui_app_place(&files_app, ui_px(&files.ui, FILES_WIDTH), ui_px(&files.ui, FILES_HEIGHT));
        return -1;
}

// An item of the path field's own menu (copy, paste), done to the field; or of the browser's (files_menu_do).
static fn files_chose(int id)
{
        if (id >= FILES_MENU_OPEN)
        {
                files_menu_do(id);
                files.dirty = true;
                return;
        }
        if (files.menu.field && ui_text_menu_do(&files.ui, files.menu.field, id))
                files.dirty = true;
}

static struct ui_app files_app = {
        .own = &files.own,
        .ui = &files.ui,
        .missing = (string_address) "files: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Files",
        .name = (string_address) "files",
        .glass = 0, // the kit's surface, as every window that stays is
        .nofont = (string_address) "files: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &files.dirty,
        .focused = &files.focused,
        .ground = &files.ground,
        .open = files_open_window,
        .event = files_event,
        .tick = files_tick,
        .popup = &files.menu,
        .chose = files_chose,
        .width = &files.width,
        .height = &files.height,
        .shown = &files.shown,
        .draw = files_draw,
};

static b32 files_main()
{
        memory_zero(&files, sizeof(files));
        files.start = getenv((string_address) "HOME");
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took ? took : 1)
                if (!(took = ui_app_option(&files_app, at)) && program_argument(at)[0] == '/')
                        files.start = program_argument(at);
        return ui_app_run(&files_app);
}

#endif // MOONWATER_CANVAS_FILES
