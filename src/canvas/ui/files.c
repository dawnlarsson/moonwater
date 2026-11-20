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

        Two panes. The folder tree (files_tree_* in the model, the kit's list as a tree) is beside the rows, the folder shown lit in it
        and kept open to; a node's chevron opens and shuts it and its name shows its folder. The rows are details under the kit's
        columns (Name, Size, Type, Modified: a title pressed orders by it, an edge held widens a column), large icons or a list of names
        (Ctrl+1, 2, 3; the kit's tiles). A press of the right button, Shift+F10 or the menu key opens the menu of the rows or of the
        folder (open, open in the editor, a terminal here, cut, copy, paste, rename, delete, new folder, select all, hidden files, the
        view, the order, properties); Ctrl+X, Ctrl+C and Ctrl+V move and copy files through the clipboard; Alt+Enter shows a properties
        sheet. Ctrl+F finds in the folder (a field at the foot whose text keeps the rows to the names that hold it), a letter typed goes
        to the first name that starts with it, and a press on the rows' ground, or on a row away from its name, draws a rubber band.
        Tab takes the keys from the rows to the trail and on to the tree.

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
#define FILES_KEY_F6 64
#define FILES_CARRY_POP_NS 220000000ll // the ghost's lift as what is carried is picked up, settling to rest
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
#define FILES_MENU_SORT 32           // and 33, 34, 35: the rows ordered by name, size, type or time (the columns' order)
#define FILES_MENU_UNDO 40
#define FILES_MENU_REDO 41
#define FILES_MENU_BIN 42            // the bin shown
#define FILES_MENU_BIN_EMPTY 43      // everything in it deleted for good, asked first
#define FILES_MENU_RESTORE 44        // what is chosen in it put back where it was
#define FILES_MENU_WALLPAPER 45      // a PNG made the wallpaper of every screen (moonwater canvas wallpaper picture)
#define FILES_MENU_WALLPAPER_OF 46   // and on to 46 + FILES_WALL_SCREENS: of one screen, by its name
#define FILES_WALL_SCREENS 8
#define FILES_KEY_Z 44
#define FILES_FLASH_NS 450000000ll // a drop's things lit where they landed
#define FILES_SPRING_NS 900000000ll  // a drag held this long over a folder opens it, so the drop can go further in
#define FILES_SPRING_NODE 0x10000    // a spring's target is a tree node (this plus its number), not a row
#define FILES_EDGE 28                // logical pixels at a list's top and foot where a drag over it scrolls it
#define FILES_EDGE_STEP 14           // and how far a frame, at its very edge
#define FILES_KEY_Y 21
#define FILES_VIEW_DETAILS 0
#define FILES_VIEW_ICONS 1
#define FILES_VIEW_LIST 2
#define FILES_BAND 32                // logical pixels of the path's band and the finder's field
#define FILES_ROW UI_ROW_DENSE       // logical pixels of a row of the details, the list and the tree: denser than the kit's 40
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
        p8 band_base[FILES_MOST / 8];  // what was chosen when a band with Control began
        p8 git_state[FILES_MOST];      // each entry's git mark (FILES_GIT_*)
};

enum
{
        FILES_FOCUS_ROWS,
        FILES_FOCUS_CRUMBS,
        FILES_FOCUS_TREE,
        FILES_FOCUS_FIND,
};

/*
        A deed (Ctrl+Z, Ctrl+Y): what a move, a copy, a rename or a putting in the bin did, as pairs of paths, each with its end: where
        each thing was and where it is now (a copy's: its source and the copy). Undone, a move and a rename are renamed back, a thing in
        the bin is put back, and a copy is put in the bin (nothing is ever deleted by an undo); done again, the other way.
*/
#define FILES_DEEDS 32
enum
{
        FILES_DEED_MOVE,   // from, to: moved there (a carry, a drop, a cut's paste)
        FILES_DEED_COPY,   // source, copy
        FILES_DEED_RENAME, // old path, new path
        FILES_DEED_BIN,    // where it was, where it is in the bin's files
};

struct files_deed
{
        int kind;
        p32 count;
        p8 address_to pairs;
        positive room, used;
        b32 binned;   // a copy undone: its copies are in the bin, the pairs are the copy and where it is in the bin
        b32 restored; // a bin's deed that put things back: undone it puts them in again
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
        b64 next_look, next_show;
        unsigned int said;       // LANGUAGE_files_* the foot says of the last thing asked, 0 none
        string_address start;    // the folder asked for as it started
        // The path's band: the crumbs at rest, the field while the path is typed.
        struct ui_rect crumbs_box;
        struct files_trail trail; // the model's trail: the deepest path of this branch, and where on it the folder shown is
        struct ui_trail crumbs;   // and the kit's, which draws it and says what a point is on
        int crumb_hover;          // the folder of it under the pointer, -1 none
        int editing;             // the band is the field: FILES_EDIT_PATH or FILES_EDIT_NAME; FILES_EDIT_PART one folder of the trail is; 0 the crumbs
        int part;                // the trail's folder being spelled again
        int focus;               // FILES_FOCUS_*: which pane/field holds keyboard focus
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
        struct ui_rect side_box;   // the folder tree's pane: the window's glass, its whole height at its start
        // Carrying (Tab): the entries picked up, as paths each with its end, from the folder carry_from, dim where they lie and a ghost of
        // them at the pointer with their count, till Tab drops them in the folder shown (a press, in a folder's row) or Escape puts them
        // back; cut_dim is the same list for a cut (Ctrl+X), dim and not carried. carry_at is when they were picked up (the ghost's pop).
        b32 carrying, cut_dim;
        p8 address_to carry_paths;
        positive carry_room, carry_used;
        p32 carry_count;
        unsigned int carry_kind;
        p8 carry_from[FILES_PATH];
        b64 carry_at;
        // A rubber band begun with Control held adds what it crosses to what was chosen (band_base, as it was at its press).
        b32 band_add;
        p32 band_base_count;
        // A press while carrying (Tab's): where it went down; its release there drops as a press did, and taken past the slop it hands
        // what is carried to the desktop's drag, which another window or a Wayland client takes as its drop.
        b32 carry_press;
        int carry_px, carry_py;
        // What was done that Ctrl+Z undoes and Ctrl+Y does again (files_deed): the last FILES_DEEDS, deed_done of them done, the ones after
        // them undone (a new one forgets those). move_into is the folder a copy or a move under way goes into, for its deed.
        struct files_deed deed[FILES_DEEDS];
        int deed_count, deed_done;
        p8 move_into[FILES_PATH];
        // The listing's marks as git sees them where the folder is in a work tree (files_git_marks), worked out again when the listing changes.
        b32 git;
        p64 git_key;
        // The drop's pop: the names a copy or a move landed as in the folder shown (or the folder of it they went into), lit in the
        // accent for FILES_FLASH_NS from flash_at, each with its end.
        p8 flash[4096];
        positive flash_used;
        b64 flash_at;
        struct ui_drop drop;     // a drag of files over the window: dropped, they are copied or moved into the folder it is over
        int drop_row;            // the folder's row the drop goes into, -1 the folder shown
        int drop_node;           // or the folder tree's node it is over (a drop goes into its folder), -1 none
        b32 drop_over;           // a drag is over the window now, at drop.x, drop.y
        int spring;              // the folder (a row, or a node plus FILES_SPRING_NODE) a drag has been held over since spring_at, -1 none
        b64 spring_at;
        b64 next_progress;       // when the foot next says how far a copy has gone
        b32 moving;              // a copy or a move was begun and its end is not said yet
        p32 tree_at;             // the tree's node the keys are on there
        p32 tree_shown;          // the tree's node of the folder shown, FILES_NO when it is not in the tree
        b32 tree_reseat;         // the tree's rows are to be scrolled to the folder shown at the next layout
        int view;                // FILES_VIEW_*: details, icons or a list
        b32 pasting;             // the clipboard was asked for files to paste: its answer is a paste into the folder shown
        // Find in the folder (Ctrl+F): a field at the foot whose text keeps the rows to the names that hold it (files_model_filter).
        b32 finding;
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
static b32 files_carried(const p8 address_to name, positive length);
static b32 files_flashed(const p8 address_to name, positive length);
static b32 files_transfer(const p8 address_to paths, p32 count, int row, int how);
static b32 files_in_bin(void);
static struct files_deed address_to files_deed_new(int kind);
static fn files_deed_add(struct files_deed address_to d, const p8 address_to from, const p8 address_to to);
static fn files_deed_keep(struct files_deed address_to d);
static b32 files_tree_trash(const p8 address_to name, positive length);
static b32 files_act(int which);
static fn files_moved(void);
static fn files_part_go(void);
static fn files_open_with(b32 editor);

#define FILES_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline b32 files_in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* ---- the rows ---- */

// Whether the row at place is chosen in the model: handed to the kit's list (ui_list.selected) so selected rows drag and unselected scroll.
static b32 files_row_selected(int id)
{
        return id >= 0 && files_model_chosen(&files.big->model, (p32)id);
}

// A moment in the machine's own zone, digits whatever the language: YYYY-MM-DD<sep>HH:MM[:SS].
static fn files_format_time(char address_to into, b64 seconds, char sep, b32 with_seconds)
{
        tm broken;
        time_t at = (time_t)seconds;
        int year;

        into[0] = 0;
        if (!localtime_r(&at, &broken) || (year = broken.tm_year + 1900) < 0 || year > 9999)
                return;
        {
                static const char punct[5] = {'-', '-', ' ', ':', ':'};
                int parts[5] = {broken.tm_mon + 1, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec};
                int count = with_seconds ? 5 : 4;

                into[0] = (char)('0' + year / 1000), into[1] = (char)('0' + year / 100 % 10);
                into[2] = (char)('0' + year / 10 % 10), into[3] = (char)('0' + year % 10);
                for (int k = 0; k < count; k++)
                {
                        into[4 + 3 * k] = k == 2 ? sep : punct[k];
                        into[5 + 3 * k] = (char)('0' + parts[k] / 10);
                        into[6 + 3 * k] = (char)('0' + parts[k] % 10);
                }
                into[4 + 3 * count] = 0;
        }
}

static positive files_name_text(char address_to into, positive room, const struct files_model address_to m, const struct files_entry address_to entry)
{
        return room > 2 ? ui_label_foreign(into, room - 1, files_model_name(m, entry), entry->length) : 0;
}

// Whether the folder name in the folder at path is the home folder ($HOME).
static b32 files_is_home(const p8 address_to path, const p8 address_to name, positive length)
{
        static string_address home;
        positive at;

        if (!home && !(home = getenv((string_address) "HOME")))
                home = (string_address) "";
        at = string_length((string_address)path);
        at = at == 1 ? 0 : at; // the root's folders are "/" and the name
        return home[0] && !memory_compare(home, path, at) && home[at] == '/' && !memory_compare(home + at + 1, name, length) && !home[at + 1 + length];
}

// What an entry's symbol is (UI_FILE_*, a link's marked UI_FILE_LINKED): the kit's kind of its name and mode, a folder's open while a
// drag is over it, and the home folder and the bin their own where they are listed.
static unsigned int files_symbol(const struct files_model address_to m, const struct files_entry address_to entry)
{
        const p8 address_to name = files_model_name(m, entry);
        p32 mode = entry->looked ? entry->mode : entry->kind == FILES_KIND_DIRECTORY ? 0040000u : entry->kind == FILES_KIND_FILE ? 0100000u : 0;
        unsigned int kind = (unsigned int)ui_file_kind((const char address_to)name, mode);

        if (kind == UI_FILE_FOLDER && files_is_home(m->path, name, entry->length))
                kind = UI_FILE_HOME;
        else if (kind == UI_FILE_FOLDER && files_tree_trash(name, entry->length))
                kind = UI_FILE_TRASH;
        return kind | (entry->kind == FILES_KIND_LINK ? UI_FILE_LINKED : 0);
}

// The extension a file's symbol wears in a tile: its name's last dot on, a dot first not counting, or none.
static const char address_to files_extension_label(const struct files_model address_to m, const struct files_entry address_to entry)
{
        static char label[8];
        const p8 address_to name = files_model_name(m, entry);
        char extension[8];

        if (entry->kind == FILES_KIND_DIRECTORY)
                return null;
        ui_file_extension((const char address_to)name, extension, sizeof(extension));
        if (!extension[0] || string_length((string_address)extension) > 4)
                return null;
        label[0] = '.';
        string_copy_bounded((string_address)label + 1, (string_address)extension, sizeof(label) - 1);
        return label;
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
                        ui_size_text(size, sizeof(size), entry->size);
                files_format_time(when, entry->modified, ' ', false);
        }
        ui_file_type(type, sizeof(type), files_model_name(m, entry), entry->length, entry->kind == FILES_KIND_DIRECTORY,
                     entry->kind == FILES_KIND_LINK, entry->kind == FILES_KIND_FILE);
        row->cells[1] = size, row->cells[2] = type, row->cells[3] = when;
        row->file = 1 + files_symbol(m, entry);
        row->extension = files_extension_label(m, entry);
        row->dim = files_carried(files_model_name(m, entry), entry->length);
        row->glow = files_flashed(files_model_name(m, entry), entry->length);
        // In a work tree: what git ignores is quiet, what it does not track yet is cyan, a shade the theme's ground keeps readable.
        if (files.git && entry >= m->shown.entry && entry < m->shown.entry + m->shown.count)
        {
                p8 mark = files.big->git_state[entry - m->shown.entry];

                row->dim |= mark == FILES_GIT_IGNORED;
                row->ink = mark == FILES_GIT_UNTRACKED ? (ui_kit_light(&files.ui) ? 0xff00838fu : 0xff5fd7e0u) : 0;
        }
        row->mark = files_model_chosen(m, (p32)id) && m->chosen_count > 1;
}

// Whether a folder's name is a bin's (.Trash, Trash).
static b32 files_tree_trash(const p8 address_to name, positive length)
{
        return (length == 6 && !memory_compare(name, ".Trash", 6)) || (length == 5 && !memory_compare(name, "Trash", 5));
}

// A node of the sidebar tree, for its list: common folders (Home, Downloads, Pictures) above the storage drives (System + mounted
// drives), each expandable into its own folder tree; a subtle separator separates the drives from the common folders above them.
static fn files_tree_describe(int id, struct ui_row address_to row)
{
        static const struct
        {
                unsigned int word, icon;
        } places[] = {
            [FILES_PLACE_HOME] = {LANGUAGE_files_place_home, UI_FILE_HOME},
            [FILES_PLACE_DOWNLOADS] = {LANGUAGE_files_place_downloads, UI_FILE_DOWNLOADS},
            [FILES_PLACE_PICTURES] = {LANGUAGE_files_place_pictures, UI_FILE_PICTURES},
            [FILES_PLACE_ROOT] = {LANGUAGE_files_drive_root, UI_FILE_DRIVE},
        };
        static char label[FILES_NAME * 3 + 4];
        const struct files_tree address_to t = &files.big->tree;
        const struct files_node address_to n = id >= 0 && (p32)id < t->count ? &t->node[id] : null;
        unsigned int icon;

        if (!n)
                return;
        if (n->depth == 0 && n->place > FILES_PLACE_NONE && n->place <= FILES_PLACE_ROOT)
                row->label = language_text(places[n->place].word), icon = places[n->place].icon;
        else
        {
                ui_label_foreign(label, sizeof(label), files_tree_name(t, (p32)id), n->length);
                row->label = label;
                icon = n->kind == FILES_NODE_DRIVE ? UI_FILE_DRIVE
                       : files_tree_trash(files_tree_name(t, (p32)id), n->length) ? UI_FILE_TRASH
                       : n->open ? UI_FILE_FOLDER_OPEN : UI_FILE_FOLDER;
        }
        row->indent = n->depth;
        row->expander = n->open ? UI_ROW_EXPANDER_OPEN : n->read && !n->has ? 0 : UI_ROW_EXPANDER_SHUT;
        row->file = 1 + icon;
        // Separate the first storage drive from the common folders above it.
        if (id > 0 && n->kind == FILES_NODE_DRIVE && t->node[id - 1].kind != FILES_NODE_DRIVE)
                row->separator = true;
}

// The tree after the folder shown changed: the nodes down to it opened, and it is the lit one (none when it is not in the tree).
static fn files_tree_follow(void)
{
        files.tree_shown = files_tree_reveal(&files.big->tree, files.big->model.path);
        if (files.tree_shown != FILES_NO)
                files.tree_at = files.tree_shown, files.tree_reseat = true;
}

// The model's order for a column of the header: Name, Size, Type, Modified.
static const int files_sort_cols[4] = {FILES_SORT_NAME, FILES_SORT_SIZE, FILES_SORT_KIND, FILES_SORT_TIME};

static fn files_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        files.dirty = true;
}

/* ---- the window's words ---- */

// A folder's state as its language id (used for both the empty-folder notice and foot errors).
static unsigned int files_state_said(int state)
{
        switch (state)
        {
        case FILES_READING: return LANGUAGE_files_reading;
        case FILES_UNREADABLE: return LANGUAGE_files_unreadable;
        case FILES_GONE: return LANGUAGE_files_gone;
        case FILES_NOT_DIRECTORY: return LANGUAGE_files_not_folder;
        case FILES_READY: return LANGUAGE_files_empty;
        default: return LANGUAGE_files_failed;
        }
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

                ui_size_text(done, sizeof(done), __atomic_load_n(&job->bytes_done, __ATOMIC_ACQUIRE));
                ui_size_text(whole, sizeof(whole), __atomic_load_n(&job->bytes_total, __ATOMIC_ACQUIRE));
                language_format(into, room, job->how == FILES_MOVE_MOVE ? WORDS(files_moving) : WORDS(files_copying), (int)items_done,
                                (int)(items_total > items_done ? items_total : items_done), done, whole);
        }
        else if (files.big->job.state == FILES_JOB_RUNNING)
                language_format(into, room, WORDS(files_deleting), (int)files.big->job.removed);
        else if (files.carrying)
                language_format(into, room, WORDS(files_carrying), (int)files.carry_count);
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
                ui_size_text(size, sizeof(size), bytes);
                language_format(into, room, WORDS(files_chosen_size), (int)m->chosen_count, (int)m->shown.count, size);
        }
        else if (m->state == FILES_READY && m->filter_length)
                language_format(into, room, WORDS(files_count_found), (int)files_model_count(m), (int)m->shown.count);
        else if (m->state == FILES_READY || (m->state == FILES_READING && m->shown.count))
                language_format(into, room, m->shown.cut ? WORDS(files_count_cut) : m->shown.count == 1 ? WORDS(files_count_one) : WORDS(files_count), (int)m->shown.count);
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
        t->keys = files.focus == FILES_FOCUS_CRUMBS && files.focused && !files.editing, t->focus = files.crumb_focus;
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

// The window as a tree: the path's band (the three buttons, then the path), the rows, and the foot.
static fn files_layout(void)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;

        files.big->tree_list.total = (int)files.big->tree.count;
        files.big->tree_list.focus = files.focus == FILES_FOCUS_TREE ? (int)files.tree_at : files.tree_shown != FILES_NO ? (int)files.tree_shown : -1;
        files_status_text();
        files.path.focused = files.editing && files.focused;
        files.finder_field.focused = files.finding && files.focus == FILES_FOCUS_FIND && files.focused;
        files.finder_field.items[0] = WORDS(files_find_hint);
        files.path.items[0] = files.editing == FILES_EDIT_NAME ? WORDS(files_rename_hint) : null;
        // Every entry is a row of the list, the row the keys are on is the model's cursor, and an empty folder says what it is.
        l->total = (int)files_model_count(m);
        l->focus = m->cursor != FILES_NO ? (int)m->cursor : -1;
        l->empty = m->state == FILES_NONE ? "" : language_text(files_state_said(m->state));
        // The view: details are rows under the columns (striped), icons and a list are tiles. Set before ui_view_end so ui_list_layout
        // sizes the virtual list and its scrollbar accurately on the first frame after switching view modes.
        l->columns = files.view == FILES_VIEW_DETAILS ? &files.columns : null;
        l->stripe = files.view == FILES_VIEW_DETAILS ? 2 : 0;
        l->tiles = files.view == FILES_VIEW_ICONS ? FILES_TILE_ICON : files.view == FILES_VIEW_LIST ? FILES_TILE_LIST : 0;
        l->tile_h = files.view == FILES_VIEW_LIST ? FILES_ROW : 0;
        ui_view_begin(&files.big->view, ui, files.width, -files.height);
        // The folder tree is the window's glass pane, its whole height at the start; the band, the rows and the foot are beside it.
        ui_side_frame()
        {
                ui_side_pane(&files.side_box)
                        ui_col(.class = "rows", .grow = 1)
                                ui_rows(&files.big->tree_list, .class = "list", .grow = 1);
                ui_side_body()
                {
                        ui_row(.class = "pathbar", .h = FILES_BAND + UI_GAP2 * 2)
                        {
                                ui_button(WORDS(files_back), .class = "ghost", .on = files_act, .arg = 0);
                                ui_button(WORDS(files_forward), .class = "ghost", .on = files_act, .arg = 1);
                                ui_button(WORDS(files_up), .class = "ghost", .on = files_act, .arg = 2);
                                // The path: the field while it is typed, else the room its crumbs are laid out and drawn in (files_crumbs_lay).
                                if (files.editing && files.editing != FILES_EDIT_PART)
                                        ui_control(&files.path, .grow = 1, .h = FILES_BAND);
                                else
                                        ui_host(&files.crumbs_box, .grow = 1, .h = FILES_BAND);
                        }
                        // The header of the columns over the rows: the kit's list, its words in from its margins.
                        ui_col(.class = "rows", .grow = 1)
                        {
                                ui_host(&files.head_box, .h = UI_COLUMNS_H, .hidden = files.view != FILES_VIEW_DETAILS);
                                ui_rows(l, .class = "list", .grow = 1);
                        }
                        ui_row(.class = "bar")
                        {
                                if (files.finding)
                                        ui_control(&files.finder_field, .grow = 1, .h = FILES_BAND);
                                ui_text(files.big->status, .class = "status");
                        }
                }
        }
        ui_view_end(&files.big->view);
        // A folder shown as it was left has its rows where they were; any other, from the top. And where they are now is the model's
        // to keep with the listing when the folder is left.
        if (files.reseat)
                ui_scroll_set(&l->scroll, (int)m->seat), files.reseat = false;
        m->seat = l->scroll.scroll;
        // The columns across the rows' words, in the language's titles; the one the rows are ordered by is the model's order.
        files.columns.title[0] = WORDS(files_column_name), files.columns.title[1] = WORDS(files_column_size);
        files.columns.title[2] = WORDS(files_column_type), files.columns.title[3] = WORDS(files_column_modified);
        ui_columns_layout(ui, &files.columns, l->x, files.head_box.y, l->w);
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

static positive files_field_read(struct ui_text address_to t, p8 address_to into, positive room)
{
        positive length = ui_text_string(t, into, room);

        into[length < room ? length : room - 1] = end;
        return length;
}

static unsigned int files_did_said(int did)
{
        return did == FILES_DID ? 0 : did == FILES_DID_EXISTS ? LANGUAGE_files_name_taken : did == FILES_DID_BAD ? LANGUAGE_files_name_bad : LANGUAGE_files_unreadable;
}

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
        files.focus = FILES_FOCUS_ROWS;
        files.crumb_hover = -1, files.said = 0, files.armed = false, files.big->note[0] = 0;
        files.dirty = true;
}

// The names of what a delete would remove, the first FILES_ASK_NAMES of them as words from outside with a comma between, and how many
// more there are after a plus: what the question shows of them. Answers how many there are in all.
static p32 files_ask_names(char address_to into, positive room)
{
        const struct files_model address_to m = &files.big->model;
        positive used = 0;
        p32 count = 0, iter = 0, place;

        into[0] = 0;
        while ((place = files_selected_next(m, &iter)) != FILES_NO)
        {
                const struct files_entry address_to entry = files_model_entry(m, place);

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

// After a delete the cursor is put on the nearest entry that was not deleted: the first after the cursor that was not chosen, else the
// last before it, which the listing read again finds by its name.
static fn files_after_delete(struct files_model address_to m)
{
        p32 count = files_model_count(m), at = m->cursor, near = FILES_NO;

        if (at == FILES_NO || at >= count)
                return;
        for (p32 k = at + 1; k < count && near == FILES_NO; k++)
                if (!files_model_chosen(m, k))
                        near = k;
        for (p32 k = at; k-- > 0 && near == FILES_NO;)
                if (!files_model_chosen(m, k))
                        near = k;
        if (near != FILES_NO)
                string_copy_bounded(m->came, (string_address)files_model_name(m, files_model_entry(m, near)), FILES_NAME);
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
        language_format(files.big->note, sizeof(files.big->note), files_in_bin() ? WORDS(files_delete_ask) : WORDS(files_delete_for_good), (int)count, names);
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
        files.said = files_did_said(did);
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
        static p8 typed[FILES_NAME], was[FILES_PATH], now[FILES_PATH];
        const struct files_entry address_to entry = files_model_entry(m, files.naming);
        int did;

        (void)files_field_read(files_field(), typed, sizeof(typed));
        was[0] = 0;
        if (entry)
        {
                files_path_join(was, FILES_PATH, m->path, files_model_name(m, entry));
                files_path_join(now, FILES_PATH, m->path, typed);
        }
        did = files_model_rename(m, files.naming, typed);
        if (did != FILES_DID)
        {
                files.said = files_did_said(did);
                files.dirty = true;
                return;
        }
        if (was[0])
        {
                struct files_deed address_to d = files_deed_new(FILES_DEED_RENAME);

                files_deed_add(d, was, now);
                files_deed_keep(d);
        }
        files_edit(0);
        files_model_reload(m);
        string_copy_bounded(m->came, typed, FILES_NAME);
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
        positive length = files_field_read(files_field(), typed, sizeof(typed));
        int why;

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
        positive length;

        (void)files_field_read(files_field(), typed, sizeof(typed));
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

// Collect the chosen rows (or single alone when !use_chosen) as NUL-separated full paths; answers how many.
static p32 files_paths_collect(p32 single, b32 use_chosen, p8 address_to address_to out, positive address_to room, positive address_to used,
                               unsigned int address_to first_kind)
{
        const struct files_model address_to m = &files.big->model;
        positive folder = string_length((string_address)m->path);
        p32 count = 0;

        for (p32 at = 0; at < files_model_count(m); at++)
        {
                const struct files_entry address_to entry = files_model_entry(m, at);
                const p8 address_to name = entry ? files_model_name(m, entry) : null;
                positive length = name ? string_length((string_address)name) : 0;

                if (!name || (use_chosen ? !files_model_chosen(m, at) : at != single))
                        continue;
                if (!array_store_reserve(*out, *room, *used, *used + folder + length + 2, 4096))
                        break;
                files_path_join(*out + *used, *room - *used, m->path, name);
                *used += string_length((string_address)(*out + *used)) + 1;
                if (!count && first_kind)
                        *first_kind = files_symbol(m, entry);
                count++;
        }
        return count;
}

// Pack NUL-separated paths into clipboard/drag parts: text/uri-list (CRLF, %XX-escaped) and text/plain (LF).
static b32 files_uri_pack(const p8 address_to list, positive list_used, struct windows_clip_part address_to parts, p8 address_to address_to uris,
                          positive address_to uris_room, p8 address_to address_to text, positive address_to text_room)
{
        positive uris_used = 0, text_used = 0;

        *uris = *text = null, *uris_room = *text_room = 0;
        for (positive at = 0; at < list_used;)
        {
                const p8 address_to path = list + at;
                positive length = string_length((string_address)path);

                at += length + 1;
                if (!array_store_reserve(*uris, *uris_room, uris_used, uris_used + 3 * length + 12, 4096) ||
                    !array_store_reserve(*text, *text_room, text_used, text_used + length + 2, 4096))
                        break;
                memory_copy(*uris + uris_used, "file://", 7), uris_used += 7;
                for (positive k = 0; k < length; k++)
                        uris_used += files_uri_byte(*uris + uris_used, path[k]);
                (*uris)[uris_used++] = '\r', (*uris)[uris_used++] = '\n';
                memory_copy(*text + text_used, path, length), text_used += length, (*text)[text_used++] = '\n';
        }
        if (!uris_used || !text_used)
        {
                array_store_release(*uris, *uris_room, uris_used);
                array_store_release(*text, *text_room, text_used);
                return false;
        }
        parts[0] = (struct windows_clip_part){WINDOWS_CLIP_URIS, *uris, uris_used};
        parts[1] = (struct windows_clip_part){WINDOWS_CLIP_TEXT, *text, text_used - 1};
        return true;
}

static fn files_uris_free(p8 address_to uris, positive uris_room, p8 address_to paths, positive paths_room, const struct windows_clip_part address_to parts)
{
        positive uri_length = parts[0].length, path_length = parts[1].length + 1;

        array_store_release(uris, uris_room, uri_length);
        array_store_release(paths, paths_room, path_length);
}

static b32 files_drag_offer_list(const p8 address_to list, positive list_used, p32 items, p32 kind)
{
        struct windows_clip_part parts[2];
        p8 address_to uris, address_to text;
        positive uris_room, text_room;
        b32 offered;

        if (!files_uri_pack(list, list_used, parts, &uris, &uris_room, &text, &text_room))
                return false;
        offered = ui_drag_offer(&files.own, &files.drag, parts, 2,
                                WINDOWS_DRAG_COPY | WINDOWS_DRAG_MOVE | kind << WINDOWS_DRAG_KIND_SHIFT |
                                    (items > 0xffff ? 0xffffu : items) << WINDOWS_DRAG_ITEMS_SHIFT);
        files_uris_free(uris, uris_room, text, text_room, parts);
        return offered;
}

/* ---- the bin and the deeds ---- */

// The bin's folder ($HOME/.local/share/Trash, plus optional sub-path), or false without a home.
static b32 files_bin_root(p8 address_to into, const char address_to sub)
{
        string_address home = getenv((string_address) "HOME");

        if (!home || home[0] != '/')
                return false;
        string_copy_bounded((string_address)into, home, FILES_PATH);
        string_append_bounded((string_address)into, (string_address) "/.local/share/Trash", FILES_PATH);
        if (string_length((string_address)into) + 16 >= FILES_PATH)
                return false;
        if (sub)
                string_append_bounded((string_address)into, (string_address)sub, FILES_PATH);
        return true;
}

// Whether the folder shown is the bin's files.
static b32 files_in_bin(void)
{
        p8 bin[FILES_PATH];

        return files_bin_root(bin, "/files") && string_equals((string_address)bin, (string_address)files.big->model.path);
}

// Now, as a bin's info says when: 2026-11-18T17:45:00 in the machine's zone.
static fn files_now_text(char address_to into)
{
        timespec real = {0, 0};

        clock_gettime(CLOCK_REALTIME, address_of real);
        files_format_time(into, (b64)real.tv_sec, 'T', true);
        if (!into[0])
                string_copy_bounded((string_address)into, (string_address) "1970-01-01T00:00:00", 20);
}

static fn files_deed_free(struct files_deed address_to d)
{
        array_store_release(d->pairs, d->room, d->used);
        memory_zero(d, sizeof(*d));
}

// A new deed of a kind: the ones undone after the last done are forgotten, and the oldest when there are FILES_DEEDS.
static struct files_deed address_to files_deed_new(int kind)
{
        struct files_deed address_to d;

        while (files.deed_count > files.deed_done)
                files_deed_free(&files.deed[--files.deed_count]);
        if (files.deed_count == FILES_DEEDS)
        {
                files_deed_free(&files.deed[0]);
                for (int k = 0; k + 1 < FILES_DEEDS; k++)
                        files.deed[k] = files.deed[k + 1];
                files.deed_count--, files.deed_done--;
        }
        d = &files.deed[files.deed_count++];
        files.deed_done = files.deed_count;
        memory_zero(d, sizeof(*d));
        d->kind = kind;
        return d;
}

// A pair added to a deed: where it was, where it is.
static fn files_deed_add(struct files_deed address_to d, const p8 address_to from, const p8 address_to to)
{
        positive a = string_length((string_address)from) + 1, b = string_length((string_address)to) + 1;

        if (!array_store_reserve(d->pairs, d->room, d->used, d->used + a + b, 4096))
                return;
        memory_copy(d->pairs + d->used, from, a), memory_copy(d->pairs + d->used + a, to, b);
        d->used += a + b, d->count++;
}

// A deed with nothing in it is no deed.
static fn files_deed_keep(struct files_deed address_to d)
{
        if (d->count)
                return;
        files_deed_free(d);
        files.deed_count--, files.deed_done = files.deed_count;
}

// The drop's pop: what a copy or a move that is over landed as, where the folder shown shows it (its rows), or the folder of the folder
// shown it went into (that row).
static fn files_flash_set(const struct files_move address_to job)
{
        const p8 address_to path = files.big->model.path;
        positive length = string_length((string_address)path), into = string_length((string_address)files.move_into);

        files.flash_used = 0;
        if (string_equals((string_address)files.move_into, (string_address)path))
                for (p32 k = 0; k < job->count; k++)
                {
                        const p8 address_to landed = files_move_landed(job, k);
                        positive n = string_length((string_address)landed);

                        if (n && files.flash_used + n + 1 <= sizeof(files.flash))
                                memory_copy(files.flash + files.flash_used, landed, n + 1), files.flash_used += n + 1;
                }
        else if (into > length && !memory_compare(files.move_into, path, length) && (length == 1 || files.move_into[length] == '/'))
        {
                const p8 address_to name = files.move_into + (length == 1 ? 1 : length + 1);
                positive n = string_length((string_address)name);

                if (n + 1 <= sizeof(files.flash))
                        memory_copy(files.flash, name, n + 1), files.flash_used = n + 1;
        }
        files.flash_at = (b64)ui_kit_time();
}

// Whether name appears in a NUL-separated list (comparing each entry's basename when base_only).
static b32 files_name_in_list(const p8 address_to list, positive used, const p8 address_to name, positive length, b32 base_only)
{
        for (positive at = 0; at < used;)
        {
                const p8 address_to item = list + at;
                positive whole = string_length((string_address)item);
                const p8 address_to cmp = base_only ? files_base_name(item) : item;

                if (string_length((string_address)cmp) == length && !memory_compare(cmp, name, length))
                        return true;
                at += whole + 1;
        }
        return false;
}

// Whether a name of the folder shown is lit by the drop's pop now.
static b32 files_flashed(const p8 address_to name, positive length)
{
        return files.flash_used && (b64)ui_kit_time() - files.flash_at < FILES_FLASH_NS &&
               files_name_in_list(files.flash, files.flash_used, name, length, false);
}

// The deed of a copy or a move that is over: each thing that landed, from its source to its name in the folder it went into.
static fn files_move_deed(const struct files_move address_to job)
{
        struct files_deed address_to d;
        p8 to[FILES_PATH];

        files_flash_set(job);
        if (job->how == FILES_MOVE_MEASURE || !files.move_into[0])
                return;
        d = files_deed_new(job->how == FILES_MOVE_MOVE ? FILES_DEED_MOVE : FILES_DEED_COPY);
        for (p32 k = 0; k < job->count; k++)
        {
                const p8 address_to landed = files_move_landed(job, k);

                if (!landed[0] || !job->copied[k])
                        continue;
                if (files_path_join(to, FILES_PATH, files.move_into, landed))
                        files_deed_add(d, files_move_path(job, k), to);
        }
        files_deed_keep(d);
}

static p32 files_deed_unpack(const struct files_deed address_to d, const p8 address_to (*pair)[2])
{
        p32 count = 0;

        for (positive at = 0; at < d->used && count < FILES_MOVE_MOST; count++)
        {
                pair[count][0] = d->pairs + at, at += string_length((string_address)d->pairs + at) + 1;
                pair[count][1] = d->pairs + at, at += string_length((string_address)d->pairs + at) + 1;
        }
        return count;
}

/*
        A deed undone (back) or done again: each pair the other way, the last first. A rename back that fails (the name is taken now, or a
        move went across filesystems) leaves that one where it is. Answers how many were turned.
*/
static int files_deed_turn(struct files_deed address_to d, b32 back)
{
        p8 bin[FILES_PATH], named[FILES_NAME], where[FILES_PATH];
        char when[24];
        const p8 address_to (*pair)[2] = (const p8 address_to (*)[2])files_anon_map(sizeof(*pair) * FILES_MOVE_MOST, null);
        struct files_deed turned = {.kind = d->kind}, again = {.kind = d->kind, .restored = d->restored};
        b32 has_bin = files_bin_root(bin, null);
        int done = 0;
        p32 count, n;

        if (!pair)
                return 0;
        files_now_text(when);
        count = files_deed_unpack(d, pair);
        for (p32 k = count; k-- > 0;)
        {
                const p8 address_to from = pair[k][0], address_to to = pair[k][1], address_to nf = from, address_to nt = to;
                b32 ok = false;

                if (d->kind == FILES_DEED_MOVE || d->kind == FILES_DEED_RENAME)
                        ok = (bipolar)system_call_5(syscall(renameat2), AT_FDCWD, (positive)(back ? to : from), AT_FDCWD, (positive)(back ? from : to),
                                                    FILE_RENAME_NOREPLACE) >= 0;
                else if (has_bin)
                {
                        b32 into_bin = d->kind == FILES_DEED_COPY ? !d->binned : (d->restored ? back : !back);
                        const p8 address_to src = d->kind == FILES_DEED_COPY ? to : from;

                        if (into_bin && (ok = files_bin_put(bin, src, when, named) == FILES_DID))
                                files_bin_join(where, bin, "/files", named, null), nf = src, nt = where;
                        else if (!into_bin && (ok = files_bin_restore(bin, files_base_name(to), where) == FILES_DID))
                                nf = where, nt = d->kind == FILES_DEED_COPY ? where : to;
                }
                files_deed_add(&turned, nf, nt);
                done += ok;
        }
        // The pairs as they are now, in their first order again (they were turned the last first), reusing the transient pair table.
        n = files_deed_unpack(&turned, pair);
        for (p32 k = n; k-- > 0;)
                files_deed_add(&again, pair[k][0], pair[k][1]);
        again.binned = d->kind == FILES_DEED_COPY ? (d->binned ? !done && d->binned : done > 0) : false;
        system_call_2(syscall(munmap), (positive)pair, sizeof(*pair) * FILES_MOVE_MOST);
        files_deed_free(&turned);
        files_deed_free(d);
        *d = again;
        return done;
}

// Ctrl+Z: the last deed done undone. Ctrl+Y: the first undone done again. The foot says how many, and the folder is read again.
static fn files_undo(b32 again)
{
        struct files_model address_to m = &files.big->model;
        int turned;

        files.said = 0, files.big->note[0] = 0;
        if (again ? files.deed_done >= files.deed_count : files.deed_done <= 0)
        {
                files.said = again ? LANGUAGE_files_nothing_to_redo : LANGUAGE_files_nothing_to_undo;
                files.dirty = true;
                return;
        }
        turned = files_deed_turn(&files.deed[again ? files.deed_done : files.deed_done - 1], !again);
        files.deed_done += again ? 1 : -1;
        language_format(files.big->note, sizeof(files.big->note), again ? WORDS(files_redone) : WORDS(files_undone), turned);
        files_model_reload(m);
        files.dirty = true;
}

// Put what is chosen in the bin (!restore), or put what is chosen in the bin back where it was (restore): both record a deed.
static fn files_bin_act(b32 restore)
{
        struct files_model address_to m = &files.big->model;
        p8 bin[FILES_PATH], named[FILES_NAME], path[FILES_PATH], where[FILES_PATH];
        char when[24];
        struct files_deed address_to d;
        b32 cross = false;
        int done = 0;
        p32 iter = 0, at;

        if (!restore)
        {
                files.said = 0, files.big->note[0] = 0, files.armed = false;
                if (m->state != FILES_READY)
                        return;
        }
        else if (!files_in_bin())
                return;
        if (!files_bin_root(bin, null))
                return;
        if (!restore)
                files_now_text(when), files_after_delete(m);
        d = files_deed_new(FILES_DEED_BIN);
        while ((at = files_selected_next(m, &iter)) != FILES_NO)
        {
                const struct files_entry address_to entry = files_model_entry(m, at);

                if (!entry)
                        continue;
                if (restore)
                {
                        files_bin_join(where, bin, "/files", files_model_name(m, entry), null);
                        if (files_bin_restore(bin, files_model_name(m, entry), path) == FILES_DID)
                                done++, files_deed_add(d, path, where);
                }
                else if (files_path_join(path, FILES_PATH, m->path, files_model_name(m, entry)))
                {
                        int did = files_bin_put(bin, path, when, named);

                        cross |= did == FILES_DID_CROSS;
                        if (did == FILES_DID)
                                files_bin_join(where, bin, "/files", named, null), files_deed_add(d, path, where);
                }
        }
        if (restore)
                d->restored = true, language_format(files.big->note, sizeof(files.big->note), WORDS(files_restored), done);
        else if (d->count)
                language_format(files.big->note, sizeof(files.big->note), WORDS(files_binned), (int)d->count);
        else if (cross)
                files.said = LANGUAGE_files_bin_cross;
        files_deed_keep(d);
        files_model_reload(m);
        files.dirty = true;
}

// The bin shown (its files), made first if there is none yet.
static fn files_bin_show(void)
{
        p8 bin[FILES_PATH];

        if (!files_bin_root(bin, "/files"))
                return;
        files_bin_folders(bin);
        if (files_model_go(&files.big->model, (string_address)bin))
                files_moved();
}

// Empty the bin: what is in it chosen and deleted for good, asked first as a delete is (Delete again does it); its infos go with it.
static fn files_bin_empty(void)
{
        struct files_model address_to m = &files.big->model;
        p8 bin[FILES_PATH], info[FILES_PATH];

        if (!files_in_bin() || !files_model_count(m) || !files_bin_root(bin, null))
                return;
        files_select_all(m);
        if (files.armed && files.armed_at == m->revision)
                for (p32 at = 0; at < files_model_count(m); at++)
                {
                        const struct files_entry address_to entry = files_model_entry(m, at);

                        if (entry && files_bin_join(info, bin, "/info", files_model_name(m, entry), ".trashinfo"))
                                system_remove_at(AT_FDCWD, (string_address)info, 0);
                }
        files_delete();
}

/* ---- carrying ---- */

static fn files_carry_end(void)
{
        array_store_release(files.carry_paths, files.carry_room, files.carry_used);
        files.carry_paths = null, files.carry_room = files.carry_used = 0, files.carry_count = 0;
        files.carrying = files.cut_dim = false;
        files.dirty = true;
}

// The rows chosen (the cursor's when none is) as paths, each with its end, from the folder shown: what is carried or cut. False for none.
static b32 files_carry_take(void)
{
        const struct files_model address_to m = &files.big->model;

        files_carry_end();
        files.carry_count = files_paths_collect(m->cursor, m->chosen_count > 0, &files.carry_paths, &files.carry_room, &files.carry_used, &files.carry_kind);
        string_copy_bounded((string_address)files.carry_from, (string_address)m->path, FILES_PATH);
        return files.carry_count > 0;
}

// Whether the entry name of the folder shown is one carried or cut from it: drawn dim where it lies.
static b32 files_carried(const p8 address_to name, positive length)
{
        return (files.carrying || files.cut_dim) &&
               string_equals((string_address)files.carry_from, (string_address)files.big->model.path) &&
               files_name_in_list(files.carry_paths, files.carry_used, name, length, true);
}

// What is carried dropped into the folder of a row (-1 the folder shown): moved there, unless it is the folder it came from.
static fn files_carry_drop(int row)
{
        const struct files_model address_to m = &files.big->model;

        if (!files.carrying)
                return;
        if (row < 0 && string_equals((string_address)files.carry_from, (string_address)m->path))
        {
                files_carry_end();
                files.said = LANGUAGE_files_carry_here;
                return;
        }
        (void)files_transfer(files.carry_paths, files.carry_count, row, FILES_MOVE_MOVE);
        files_carry_end();
}

// What is carried handed to the desktop's drag so another browser or Wayland client takes it as a drop.
static fn files_carry_offer(void)
{
        if (files.carrying && files.carry_count &&
            files_drag_offer_list(files.carry_paths, files.carry_used, files.carry_count, (files.carry_kind & 0xffu) + 1))
                files_carry_end(), files.carry_press = false;
}

/*
        The ghost of what is carried, at the pointer while it is over the window: the first one's symbol, lifted as it is picked up and
        settling (the pop, FILES_CARRY_POP_NS), and its count on a badge of the accent at its corner.
*/
static fn files_carry_draw(void)
{
        struct ui address_to ui = &files.ui;
        int size = ui_dev(ui, UI_FILE_TILE_ICON), x = ui_kit.pointer_x + ui_dev(ui, 14), y = ui_kit.pointer_y + ui_dev(ui, 10);
        b64 since = (b64)ui_kit.now - files.carry_at;
        char count[12];
        int width, d = ui_dev(ui, 20), cx, cy;

        if (!files.carrying || !files_in_rect(ui_kit.pointer_x, ui_kit.pointer_y, 0, 0, files.width, files.height))
                return;
        if (since >= 0 && since < FILES_CARRY_POP_NS)
                y -= (int)((FILES_CARRY_POP_NS - since) * ui_dev(ui, 6) / FILES_CARRY_POP_NS);
        ui_file_icon(ui, files.carry_kind, null, x, y, size, ui_text_colour(ui, UI_ON_SURFACE));
        count[positive_into_string((p8 address_to)count, files.carry_count)] = 0;
        width = ui_kit_measure(ui, (string_address)count, UI_LABEL_MEDIUM);
        d = width + ui_dev(ui, 10) > d ? width + ui_dev(ui, 10) : d;
        cx = x + size - d / 3, cy = y - ui_dev(ui, 4);
        ui_kit_fill(ui, &(struct ui_box){(b32)cx << 8, (b32)cy << 8, (b32)(cx + d) << 8, (b32)(cy + ui_dev(ui, 20)) << 8, (b32)ui_dev(ui, 10) << 8, (b32)ui_dev(ui, 10) << 8},
                    ui_role(UI_PRIMARY) | 0xff000000u, null);
        ui_kit_text(ui, cx + (d - width) / 2, ui_kit_baseline(ui, cy + ui_dev(ui, 2), UI_LABEL_MEDIUM), (string_address)count, UI_LABEL_MEDIUM,
                    ui_role(UI_ON_PRIMARY) | 0xff000000u, 0);
}

/*
        The rows chosen (the cursor's when none is) copied, as files, or cut: a cut offers them with a word beside
        (FILES_CLIP_CUT) that makes a browser's paste of them a move, here or in another window; any other program pastes them as it
        pastes files.
*/
static fn files_rows_copy(b32 cut)
{
        const struct files_model address_to m = &files.big->model;
        struct windows_clip_part parts[3];
        p8 address_to list = null, address_to uris, address_to paths;
        positive list_room = 0, list_used = 0, uris_room, paths_room;
        b32 ok;

        if (!files_paths_collect(m->cursor, files_model_chosen(m, m->cursor), &list, &list_room, &list_used, null))
                return;
        ok = files_uri_pack(list, list_used, parts, &uris, &uris_room, &paths, &paths_room);
        array_store_release(list, list_room, list_used);
        if (!ok)
                return;
        parts[2] = (struct windows_clip_part){FILES_CLIP_CUT, (const p8 address_to) "cut", 3};
        windows_clip_offer_parts(&files.own, WINDOWS_CLIP_CLIPBOARD, parts, cut ? 3 : 2);
        files_uris_free(uris, uris_room, paths, paths_room, parts);
        // What is cut is dim where it lies till it is pasted (it is gone from here then) or another copy or cut is made.
        if (files_carry_take() && cut)
                files.cut_dim = true;
        else
                files_carry_end();
}

#define FILES_DROP_MOST (FILES_MOVE_MOST * FILES_PATH / 4) // the bytes of a drop's paths, each with its end

// Decode a clipboard/drop text/uri-list buffer, free it, and start a copy/move into row (-1 folder shown).
static b32 files_uri_transfer(p8 address_to bytes, unsigned long length, int row, int how, p32 address_to out_count)
{
        p8 address_to paths = null;
        positive at = 0, used = 0, room = 0, got;
        p32 count = 0;
        static p8 path[FILES_PATH];
        b32 ok;

        while ((got = ui_uri_next(bytes, (positive)length, &at, path, sizeof(path))) && used + got + 1 <= FILES_DROP_MOST)
        {
                if (!array_store_reserve(paths, room, used, used + got + 1, 4096))
                        break;
                memory_copy(paths + used, path, got + 1);
                used += got + 1, count++;
        }
        windows_clip_free(bytes, length);
        ok = count && files_transfer(paths, count, row, how);
        array_store_release(paths, room, used);
        if (out_count)
                *out_count = count;
        return ok;
}

/*
        The clipboard's news. A copy this window offered that was taken or declined is let go of (windows_clip_done). The answer to a paste
        it asked for: the files it holds (text/uri-list) copied into the folder shown, or moved when they were cut; a clipboard of no files
        says so in the foot.
*/
static b32 files_clip_event(const struct window_event address_to event)
{
        p8 address_to bytes, address_to cut;
        unsigned long length, cut_length;
        p32 count = 0;
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
        if (!files_uri_transfer(bytes, length, -1, moves ? FILES_MOVE_MOVE : FILES_MOVE_COPY, &count) && !count)
                files.said = LANGUAGE_files_nothing_to_paste;
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
// The folder row a drag at (x, y) over the window would go into, -1 for the folder shown.
static int files_drop_row(int x, int y)
{
        struct files_model address_to m = &files.big->model;
        int row = ui_list_hit(&files.ui, &files.big->list, x, y);
        const struct files_entry address_to entry = row >= 0 ? files_model_entry(m, (p32)row) : null;
        b32 dragged = files.drag.live && row >= 0 && (files.drag_chosen ? files_model_chosen(m, (p32)row) : (p32)row == files.drag_place);

        return entry && entry->kind == FILES_KIND_DIRECTORY && !dragged ? row : -1;
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
static b32 files_transfer_start(const p8 address_to paths, p32 count, bipolar into, const p8 address_to dest, int how, b32 reload_on_fail)
{
        struct files_move address_to job = &files.big->move;

        files.said = 0, files.big->note[0] = 0, files.dirty = true;
        if (files_move_running(job) || files.big->job.state == FILES_JOB_RUNNING)
        {
                if (into >= 0)
                        system_close(into);
                files.said = LANGUAGE_files_busy;
                return false;
        }
        files_move_end(job);
        if (into < 0)
        {
                files.said = LANGUAGE_files_changed;
                if (reload_on_fail)
                        files_model_reload(&files.big->model);
                return false;
        }
        if (!files_move_begin(job, how, into, paths, count))
                return false;
        string_copy_bounded((string_address)files.move_into, (string_address)dest, FILES_PATH);
        files_move_start(job);
        files.moving = true, files.next_progress = (b64)ui_kit.now;
        return true;
}

static b32 files_transfer(const p8 address_to paths, p32 count, int row, int how)
{
        const struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = row >= 0 ? files_model_entry(m, (p32)row) : null;
        p8 dest[FILES_PATH];

        // A drop on a node of the folder tree: into that node's folder, by its path.
        if (row <= -2 && files.drop_node >= 0)
        {
                if (!files_tree_path(&files.big->tree, (p32)files.drop_node, dest))
                        return false;
                return files_transfer_start(paths, count, system_open_at(AT_FDCWD, (string_address)dest, FILE_READ | O_DIRECTORY | O_CLOEXEC), dest, how, false);
        }
        if (entry)
                files_path_join(dest, FILES_PATH, m->path, files_model_name(m, entry));
        else
                string_copy_bounded((string_address)dest, (string_address)m->path, FILES_PATH);
        return files_transfer_start(paths, count, files_drop_folder(row), dest, how, true);
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
                struct ui_list address_to tl = &files.big->tree_list;
                int node = ui_list_hit(&files.ui, tl, files.drop.x, files.drop.y);
                int row = node >= 0 ? -2 : files_drop_row(files.drop.x, files.drop.y);
                int spring = node >= 0 ? FILES_SPRING_NODE + node : row;
                b32 none = (files.drag.live && row == -1) || (node < 0 && files.big->model.state != FILES_READY);
                p32 preferred = files.drop.keys & WINDOW_KEY_CONTROL ? WINDOWS_DRAG_COPY
                                : files.drop.keys & WINDOW_KEY_SHIFT ? WINDOWS_DRAG_MOVE
                                : files_drop_near()                  ? WINDOWS_DRAG_MOVE
                                                                     : WINDOWS_DRAG_COPY;

                // Over a node of the tree the drop goes into its folder, the node ringed; a drag held over a folder opens it (the spring).
                files.drop_over = true;
                tl->drop = node >= 0 ? node + 1 : 0;
                if (spring != files.spring)
                        files.spring = spring, files.spring_at = (b64)ui_kit_time();
                // Another folder or another preference is a new answer: the pick is said again (ui_drop_pick says only a change).
                if (node != files.drop_node || files.drop_row != row || files.drop.action != preferred)
                        files.drop.nth = -2;
                files.drop_node = node, files.drop_row = row, files.drop.action = preferred;
                ui_drop_pick(&files.own, &files.drop, names, none ? 0 : 1, WINDOWS_DRAG_COPY | WINDOWS_DRAG_MOVE, preferred);
                l->drop = files.drop.nth < 0 || node >= 0 ? 0 : row >= 0 ? row + 1 : UI_LIST_DROP_ALL;
        }
        else if (what == UI_DROP_LEFT || what == UI_DROP_DONE || what == UI_DROP_DROPPED)
                l->drop = 0, files.big->tree_list.drop = 0, files.drop_over = false, files.spring = -1, files.drop_node = what == UI_DROP_DROPPED ? files.drop_node : -1;
        else if (what == UI_DROP_READY)
        {
                p8 address_to bytes;
                unsigned long length;
                b32 took = ui_drop_read(&files.own, &files.drop, &bytes, &length) &&
                           files_uri_transfer(bytes, length, files.drop_row, files.drop.action == WINDOWS_DRAG_MOVE ? FILES_MOVE_MOVE : FILES_MOVE_COPY, null);

                ui_drop_done(&files.own, &files.drop, took);
                files.drop_node = -1;
        }
        files.dirty = true;
        return true;
}

// Try to open path as a folder; on failure record the reason in files.said and mark dirty.
static b32 files_try_go(const p8 address_to path)
{
        int came = files_model_try(&files.big->model, (string_address)path);

        if (came == FILES_READING)
                return true;
        files.said = files_state_said(came);
        files.dirty = true;
        return false;
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

        if (files.editing == FILES_EDIT_NAME)
                files_name_go();
        else if (files.editing == FILES_EDIT_PART)
                files_part_go();
        else
        {
                (void)files_field_read(files_field(), typed, sizeof(typed));
                if (files_try_go(typed))
                        files_edit(0), files_moved();
        }
}

// Tab in the field: the folder name being typed finished from the listing shown, when the text's folder is the folder shown.
static fn files_edit_complete(void)
{
        static p8 typed[FILES_PATH], whole[FILES_PATH], name[FILES_NAME];
        const struct files_model address_to m = &files.big->model;
        positive length, cut, folder;
        p32 found;

        (void)files_field_read(files_field(), typed, sizeof(typed));
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
        int seen;

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
        if (files_try_go(to))
        {
                // Up the trail: the keys are on the folder of it come up through, as Up puts them on the folder come out of.
                static p8 through[FILES_NAME];
                positive length = (positive)(trail->to[which + 1] - trail->from[which + 1]);

                memory_copy(through, trail->path + trail->from[which + 1], length < FILES_NAME ? length : 0);
                through[length < FILES_NAME ? length : 0] = end;
                files_model_came(m, through);
                files_moved();
        }
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
                        to = files.crumb_focus, files_crumb_go(to), files.focus = FILES_FOCUS_CRUMBS, files.crumb_focus = to;
                break;
        case FILES_KEY_F2:
                files_part_edit(files.crumb_focus, false);
                break;
        case UI_KEY_ESCAPE:
        case UI_KEY_DOWN:
                files.focus = FILES_FOCUS_ROWS;
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
        files.big->list.last_clicked = 0, files.said = 0;
        // The rows: where they were left for a listing shown as it was left, which is then looked at again at once; else from the top.
        files.reseat = true;
        files.next_look = (b64)ui_kit.now + (m->restored ? 0 : FILES_LOOK_NS);
        files.dirty = true;
}

// Into the entry at a place if it is a folder, or open regular files in the editor by default.
static fn files_open(p32 place)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, place);
        int came;

        if (!entry)
                return;
        if (entry->kind == FILES_KIND_FILE)
        {
                m->cursor = place;
                files_open_with(true);
                return;
        }
        came = files_model_enter(m, place);
        if (came == FILES_SAME)
                files_moved();
        else if (came == FILES_REFUSED)
                files.said = entry->kind == FILES_KIND_LINK ? LANGUAGE_files_link : 0, files.dirty = true;
        else
                files.said = LANGUAGE_files_changed, files_model_reload(m), files.dirty = true;
}

// The band's buttons: Back, Forward, Up.
static b32 files_act(int which)
{
        struct files_model address_to m = &files.big->model;

        if (which == 2 ? files_model_up(m) : files_model_back(m, which == 1))
                files_moved();
        return true;
}

/* ---- the rubber band and dragging ---- */

// Updates the model's selection from the list's active rubber band (2D tile overlap in icon/list views, row overlap in details).
static fn files_band_apply(void)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        int first = -1, last = -1;
        b32 any = false;

        if (files.band_add)
                memory_copy(m->chosen, files.big->band_base, FILES_MOST / 8), m->chosen_count = files.band_base_count;
        else
                files_model_choose_none(m);
        if (ui_list_band_rows(ui, l, &first, &last))
        {
                b32 down_band = l->band_y1 >= l->y + l->band_at - l->scroll.scroll;
                int lead = -1;

                for (int k = first; k <= last; k++)
                        if ((p32)k < m->shown.places && ui_list_band_has(ui, l, k))
                        {
                                files_chosen_put(m, m->shown.order[k], true);
                                if (!any || down_band)
                                        lead = k;
                                any = true;
                        }
                if (any && lead >= 0)
                        m->cursor = (p32)lead;
        }
        m->revision++;
}

// Hands the selected rows (or place alone) to the desktop's drag-and-drop (text/uri-list + paths).
static fn files_drag_start(p32 place)
{
        struct files_model address_to m = &files.big->model;
        p8 address_to list = null;
        positive list_room = 0, list_used = 0;

        files.drag_place = place;
        files.drag_chosen = files_model_chosen(m, place);
        if (files_paths_collect(place, files.drag_chosen, &list, &list_room, &list_used, null))
        {
                p32 items = files.drag_chosen && m->chosen_count ? m->chosen_count : 1;
                const struct files_entry address_to first = files_model_entry(m, place);
                p32 kind = first ? (files_symbol(m, first) & 0xffu) + 1 : 0;

                (void)files_drag_offer_list(list, list_used, items, kind);
        }
        array_store_release(list, list_room, list_used);
}

/* ---- the menu and the properties sheet ---- */

static fn files_sheet_escape(char address_to into, positive room, const p8 address_to raw, positive length)
{
        char words[FILES_NAME * 3 + 4];

        ui_label_foreign(words, sizeof(words), raw, length);
        ui_menu_escape(into, room, words);
}

static fn files_popup_open(int x, int y)
{
        files.menu.field = null;
        ui_popmenu_open(&files.menu, &files.own, &files.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false);
}

static fn files_cursor_pos(int address_to x, int address_to y, b32 mid)
{
        const struct files_model address_to m = &files.big->model;
        const struct ui_list address_to l = &files.big->list;
        int pitch = ui_list_pitch(&files.ui, l);

        *x = l->x + ui_dev(&files.ui, 40);
        *y = l->y + (mid ? pitch / 2 : 0) + (m->cursor != FILES_NO ? (int)m->cursor * pitch - l->scroll.scroll : 0);
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
        char (*sheet)[FILES_NAME * 3 + 64] = files.big->sheet;
        positive room = sizeof(files.big->sheet[0]);
        b32 many = !files.sheet_folder && m->chosen_count > 1;
        char escaped[FILES_NAME * 3 + 4], size[16], bytes[24], when[24], mode[12], octal[8];
        file_facts facts;
        int lines = 0;

        if (files.sheet_folder ? m->handle < 0 || file_look_code(m->handle, (string_address) "", AT_EMPTY_PATH, &facts) < 0
                               : !entry || files_model_check(m, m->cursor, &facts) != FILES_SAME)
                return 0;
        // The name, or how many.
        if (many)
                language_format(sheet[lines++], room, WORDS(files_sheet_items), (int)m->chosen_count);
        else
        {
                const p8 address_to name = files.sheet_folder ? m->path : files_model_name(m, entry);

                files_sheet_escape(sheet[lines++], room, name, string_length((string_address)name));
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

                        escaped[0] = 0;
                        if (length >= 0)
                                files_sheet_escape(escaped, sizeof(escaped), points, (positive)length);
                        language_format(sheet[lines++], room, WORDS(files_sheet_link), escaped);
                }
                else
                        string_copy_bounded((string_address)sheet[lines++], (string_address)(format == MODE_DIRECTORY ? WORDS(files_sheet_folder)
                                                                                           : format == MODE_FILE      ? WORDS(files_sheet_file)
                                                                                                                      : WORDS(files_sheet_other)),
                                            room);
        }
        // The size: a file's own; a folder's and many entries' as measured.
        if (!many && (facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
        {
                ui_size_text(size, sizeof(size), facts.size);
                bytes[positive_into((p8 address_to)bytes, (positive)facts.size)] = 0;
                language_format(sheet[lines++], room, WORDS(files_sheet_size), size, bytes);
        }
        else if (files.measuring)
                string_copy_bounded((string_address)sheet[lines++], (string_address)WORDS(files_sheet_measuring), room);
        else
        {
                p32 items = files.big->move.items_total;

                ui_size_text(size, sizeof(size), files.big->move.bytes_total);
                // What a folder holds: the count is of what is in it, not of the folder itself.
                items -= !many && items ? 1 : 0;
                language_format(sheet[lines++], room, WORDS(files_sheet_size_in), size, (int)items);
        }
        // Where it is.
        if (!files.sheet_folder)
        {
                files_sheet_escape(escaped, sizeof(escaped), m->path, string_length((string_address)m->path));
                language_format(sheet[lines++], room, WORDS(files_sheet_in), escaped);
        }
        if (many)
                return lines;
        files_format_time(when, facts.modified.seconds, ' ', true);
        language_format(sheet[lines++], room, WORDS(files_sheet_modified), when);
        if (facts.created.seconds)
        {
                files_format_time(when, facts.created.seconds, ' ', true);
                language_format(sheet[lines++], room, WORDS(files_sheet_created), when);
        }
        // The permissions, as ls writes them (the set-id and sticky bits in the execute places) and in octal.
        for (int bit = 0; bit < 9; bit++)
                mode[bit] = facts.mode & (0400 >> bit) ? "rwxrwxrwx"[bit] : '-';
        for (int k = 0; k < 3; k++)
                if (facts.mode & (04000 >> k))
                        mode[2 + 3 * k] = (char)((k == 2 ? 't' : 's') - (mode[2 + 3 * k] != 'x' ? 32 : 0));
        mode[9] = 0;
        for (int digit = 0; digit < 4; digit++)
                octal[digit] = (char)('0' + (facts.mode >> (9 - 3 * digit) & 7));
        octal[4] = 0;
        language_format(sheet[lines++], room, WORDS(files_sheet_mode), mode, octal);
        language_format(sheet[lines++], room, WORDS(files_sheet_owner), (int)facts.owner, (int)facts.group);
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
        files_popup_open(files.sheet_x, files.sheet_y);
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
                files_cursor_pos(&x, &y, false);
        files.sheet = true, files.sheet_folder = folder, files.sheet_x = x, files.sheet_y = y, files.measuring = false;
        if (measure && !files.moving)
        {
                p8 address_to list = null;
                positive used = 0, room = 0;
                p32 count = folder ? 1 : files_paths_collect(m->cursor, m->chosen_count > 1, &list, &room, &used, null);
                bipolar into = m->handle >= 0 ? system_open_at(m->handle, (string_address) ".", FILE_READ | O_DIRECTORY | O_CLOEXEC) : -1;

                files_move_end(&files.big->move);
                if (into >= 0 && count && files_move_begin(&files.big->move, FILES_MOVE_MEASURE, into, folder ? m->path : list, count))
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
                string_address words[5] = {(string_address) "/editor", (string_address) "-f", face, (string_address)path, null};

                if (!entry || entry->kind == FILES_KIND_DIRECTORY || !files_path_join(path, sizeof(path), m->path, files_model_name(m, entry)))
                        return;
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

// Whether the entry is a PNG by its name (.png, any case): the pictures a wallpaper may be.
static b32 files_is_png(struct files_model address_to m, const struct files_entry address_to entry)
{
        const char address_to name = (const char address_to)files_model_name(m, entry);
        positive length = entry ? entry->length : 0;

        return length > 4 && name[length - 4] == '.' && (name[length - 3] | 32) == 'p' && (name[length - 2] | 32) == 'n' && (name[length - 1] | 32) == 'g';
}

// The screens the menu offers a picture to, by name, and its items' words, as the table said them when it opened.
static char files_wall_names[FILES_WALL_SCREENS][WINDOWS_OUTPUT_NAME], files_wall_labels[FILES_WALL_SCREENS][96];

// The PNG the keys are on made the wallpaper of every screen (screen -1), or of one the menu named: moonwater canvas wallpaper picture
// PATH [NAME], which keeps a copy of it.
static fn files_wallpaper(int screen)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to entry = files_model_entry(m, m->cursor);
        static p8 path[FILES_PATH];
        string_address words[7] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "picture",
                                   (string_address)path, screen >= 0 && screen < FILES_WALL_SCREENS ? (string_address)files_wall_names[screen] : null, null};
        if (!entry || entry->kind == FILES_KIND_DIRECTORY || !files_is_png(m, entry) || !files_path_join(path, sizeof(path), m->path, files_model_name(m, entry)))
                return;
        (void)ui_start_apart(words, words[5] ? 6 : 5);
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
                b32 png = !folder && files_is_png(m, entry);
                const struct ui_menu_spec head[] = {
                    {WORDS(menu_open), "Enter", 0, FILES_MENU_OPEN, false},
                    {WORDS(menu_open_editor), null, 0, FILES_MENU_EDITOR, folder},
                    {WORDS(menu_set_wallpaper), null, 0, FILES_MENU_WALLPAPER, !png},
                };
                const struct ui_menu_spec tail[] = {
                    {WORDS(menu_open_terminal), null, 0, FILES_MENU_TERMINAL, !folder},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_cut), "Ctrl+X", 0, FILES_MENU_CUT, false},
                    {WORDS(menu_copy), "Ctrl+C", 0, FILES_MENU_COPY, false},
                    {WORDS(menu_paste), "Ctrl+V", 0, FILES_MENU_PASTE, false},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_rename), "F2", m->chosen_count > 1 ? UI_MENU_DISABLED : 0, FILES_MENU_RENAME, false},
                    {WORDS(menu_bin_restore), null, 0, FILES_MENU_RESTORE, !files_in_bin()},
                    {WORDS(menu_delete), files_in_bin() ? "Shift+Delete" : "Delete", 0, FILES_MENU_DELETE, false},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_properties), "Alt+Enter", 0, FILES_MENU_PROPERTIES, false},
                };

                ui_popmenu_build(menu, head, FILES_COUNT(head));
                if (png)
                {
                        unsigned int screens = ui_kit_table ? __atomic_load_n(&ui_kit_table->outputs, __ATOMIC_ACQUIRE) : 0;

                        // With more than one screen, each of them by its name as well.
                        for (unsigned int at = 0; screens > 1 && screens <= WINDOWS_OUTPUTS && at < screens && at < FILES_WALL_SCREENS; at++)
                        {
                                string_copy_bounded((p8 *)files_wall_names[at], (string_address)ui_kit_table->output_name[at], sizeof(files_wall_names[at]));
                                language_format(files_wall_labels[at], sizeof(files_wall_labels[at]), WORDS(menu_set_wallpaper_of), files_wall_names[at]);
                                ui_popmenu_add(menu, files_wall_labels[at], null, 0, FILES_MENU_WALLPAPER_OF + (int)at);
                        }
                }
                ui_popmenu_build(menu, tail, FILES_COUNT(tail));
        }
        else
        {
                b32 bin = files_in_bin(), any = files_model_count(m) != 0;
                const struct ui_menu_spec top[] = {
                    {WORDS(menu_undo), "Ctrl+Z", files.deed_done > 0 ? 0 : UI_MENU_DISABLED, FILES_MENU_UNDO, false},
                    {WORDS(menu_redo), "Ctrl+Y", files.deed_done < files.deed_count ? 0 : UI_MENU_DISABLED, FILES_MENU_REDO, false},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_paste), "Ctrl+V", 0, FILES_MENU_PASTE, false},
                    {WORDS(menu_new_folder), "Ctrl+Shift+N", 0, FILES_MENU_NEW_FOLDER, false},
                    {WORDS(menu_select_all), "Ctrl+A", any ? 0 : UI_MENU_DISABLED, FILES_MENU_ALL, false},
                    {WORDS(menu_show_hidden), "Ctrl+H", UI_MENU_CHECK | (m->hidden ? UI_MENU_ON : 0), FILES_MENU_HIDDEN, false},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_view_details), "Ctrl+1", UI_MENU_RADIO | (files.view == FILES_VIEW_DETAILS ? UI_MENU_ON : 0), FILES_MENU_DETAILS, false},
                    {WORDS(menu_view_icons), "Ctrl+2", UI_MENU_RADIO | (files.view == FILES_VIEW_ICONS ? UI_MENU_ON : 0), FILES_MENU_ICONS, false},
                    {WORDS(menu_view_list), "Ctrl+3", UI_MENU_RADIO | (files.view == FILES_VIEW_LIST ? UI_MENU_ON : 0), FILES_MENU_LIST, false},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                };
                const struct ui_menu_spec foot[] = {
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_open_terminal), null, 0, FILES_MENU_TERMINAL, false},
                    {bin ? WORDS(menu_bin_empty) : WORDS(menu_bin_open), null, bin && !any ? UI_MENU_DISABLED : 0, bin ? FILES_MENU_BIN_EMPTY : FILES_MENU_BIN, false},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(menu_properties), null, 0, FILES_MENU_PROPERTIES, false},
                };

                ui_popmenu_build(menu, top, FILES_COUNT(top));
                // The order, as the columns' header gives it (the one chosen again turns it round): the menu's way to it from the keys.
                for (int column = 0; column < 4; column++)
                        ui_popmenu_add(menu, files.columns.title[column], column == files.columns.sorted ? (files.columns.descending ? "\xe2\x86\x93" : "\xe2\x86\x91") : null,
                                       UI_MENU_RADIO | (column == files.columns.sorted ? UI_MENU_ON : 0), FILES_MENU_SORT + column);
                ui_popmenu_build(menu, foot, FILES_COUNT(foot));
                // The folder's own: nothing chosen, the cursor off the rows, so what the items do is the folder's.
                files_model_choose_none(m);
                m->cursor = FILES_NO, m->revision++;
        }
        files.sheet_folder = !on, files.sheet_x = x, files.sheet_y = y;
        files_popup_open(x, y);
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

// An item of the browser's menu, done.
static fn files_menu_do(int id)
{
        struct files_model address_to m = &files.big->model;

        if (id >= FILES_MENU_WALLPAPER_OF && id < FILES_MENU_WALLPAPER_OF + FILES_WALL_SCREENS)
        {
                files_wallpaper(id - FILES_MENU_WALLPAPER_OF);
                return;
        }
        switch (id)
        {
        case FILES_MENU_OPEN: files_open(m->cursor); break;
        case FILES_MENU_EDITOR: files_open_with(true); break;
        case FILES_MENU_WALLPAPER: files_wallpaper(-1); break;
        case FILES_MENU_TERMINAL: files_open_with(false); break;
        case FILES_MENU_CUT: files_rows_copy(true); break;
        case FILES_MENU_COPY: files_rows_copy(false); break;
        case FILES_MENU_PASTE: files.pasting = true, windows_clip_ask(&files.own, WINDOWS_CLIP_CLIPBOARD); break;
        case FILES_MENU_RENAME: files_edit(FILES_EDIT_NAME); break;
        case FILES_MENU_DELETE:
                if (files_in_bin())
                        files_delete();
                else
                        files_bin_act(false);
                break;
        case FILES_MENU_UNDO: files_undo(false); break;
        case FILES_MENU_REDO: files_undo(true); break;
        case FILES_MENU_RESTORE: files_bin_act(true); break;
        case FILES_MENU_BIN: files_bin_show(); break;
        case FILES_MENU_BIN_EMPTY: files_bin_empty(); break;
        case FILES_MENU_NEW_FOLDER: files_new_folder(); break;
        case FILES_MENU_PROPERTIES: files_sheet_open(files.sheet_folder, files.sheet_x, files.sheet_y); break;
        case FILES_MENU_HIDDEN: m->hidden = !m->hidden, files_model_reload(m); break;
        case FILES_MENU_DETAILS:
        case FILES_MENU_ICONS:
        case FILES_MENU_LIST:
                files.view = id - FILES_MENU_DETAILS;
                files.big->list.scroll.scroll = 0;
                files.dirty = true;
                files_show_cursor();
                break;
        case FILES_MENU_SORT:
        case FILES_MENU_SORT + 1:
        case FILES_MENU_SORT + 2:
        case FILES_MENU_SORT + 3:
                files.columns.descending = id - FILES_MENU_SORT == files.columns.sorted ? !files.columns.descending : false;
                files.columns.sorted = id - FILES_MENU_SORT;
                files_model_sort(m, files_sort_cols[files.columns.sorted], files.columns.descending);
                files_show_cursor();
                break;
        case FILES_MENU_ALL: files_select_all(m); break;
        }
}

// To the tree's node at: its folder shown (opened as a named path: the model says what is there now). A common folder (Downloads,
// Pictures) that does not yet exist is created on demand when clicked. Returns false when already in that folder.
static b32 files_tree_go(p32 at)
{
        static p8 path[FILES_PATH];
        const struct files_tree address_to t = &files.big->tree;

        if (at >= t->count || !files_tree_path(t, at, path) || string_equals((string_address)path, (string_address)files.big->model.path))
                return false;
        if (files_try_go(path) ||
            (files.said == LANGUAGE_files_gone && t->node[at].depth == 0 && t->node[at].kind == FILES_NODE_PLACE &&
             (bipolar)system_call_3(syscall(mkdirat), AT_FDCWD, (positive)path, 0755) >= 0 && files_try_go(path)))
                files_moved();
        return true;
}

// A node opened or shut, the keys kept on it. Uses files_tree_find (not reveal) so collapsing an ancestor keeps it collapsed.
static fn files_tree_toggle(p32 at)
{
        struct files_tree address_to t = &files.big->tree;

        if (at >= t->count)
                return;
        if (t->node[at].open)
                files_tree_shut(t, at);
        else
                files_tree_open(t, at);
        files.tree_shown = files_tree_find(t, files.big->model.path);
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
                if ((step > 0) != (t->node[at].open != 0))
                        files_tree_toggle(at);
                else if (step > 0 && at + 1 < t->count && t->node[at + 1].depth > t->node[at].depth)
                        at++;
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
                files.focus = FILES_FOCUS_ROWS;
                break;
        default: return false;
        }
        files.tree_at = at;
        files.big->tree_list.focus = (int)at;
        ui_list_reveal(&files.ui, &files.big->tree_list);
        files.dirty = true;
        return true;
}

// Finding begun (the field at the foot, with the keys and what it held chosen) or ended (the field gone, every entry shown again).
static fn files_find(b32 on)
{
        if (on)
        {
                if (!files.finder.doc.selections && ui_field_open(&files.finder, &files.own, ""))
                        files.finder.most = FILES_NAME - 1;
                files.finding = true, files.focus = FILES_FOCUS_FIND;
                doc_select_set(&files.finder.doc, 0, doc_length(&files.finder.doc));
        }
        else
        {
                files.finding = false, files.focus = FILES_FOCUS_ROWS;
                ui_text_set(&files.finder, (const p8 address_to) "", 0);
                files_model_filter(&files.big->model, (const p8 address_to) "", 0);
        }
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
        // Delete puts what is chosen in the bin (Ctrl+Z puts it back); Shift+Delete, or Delete in the bin, deletes it for good, asked first.
        if (key == UI_KEY_DELETE)
        {
                if (!(event->flags & WINDOW_KEY_REPEAT))
                {
                        if (shift || files_in_bin())
                                files_delete();
                        else
                                files_bin_act(false);
                }
                return true;
        }
        if (control && (key == FILES_KEY_Z || key == FILES_KEY_Y))
                return files_undo(key == FILES_KEY_Y || shift), true;
        if (key == UI_KEY_ESCAPE && files.big->job.state == FILES_JOB_RUNNING)
                return files_job_cancel(&files.big->job), true;
        if (key == UI_KEY_ESCAPE && files.moving && files_move_running(&files.big->move))
                return files_move_cancel(&files.big->move), true;
        if (files.armed || files.big->note[0])
                files.armed = false, files.big->note[0] = 0, files.dirty = true;
        // Carrying: Tab on the rows drops what is carried in the folder shown, Escape puts it back; anything else goes on as it would.
        if (files.carrying && key == UI_KEY_ESCAPE)
                return files_carry_end(), true;
        if (key == UI_KEY_TAB && !shift && !control && !alt && files.focus == FILES_FOCUS_ROWS)
        {
                if (files.carrying)
                        files_carry_drop(-1);
                else if (files_carry_take())
                        files.carrying = true, files.carry_at = (b64)ui_kit.now, files.said = 0, files.big->note[0] = 0;
                files.dirty = true;
                return true;
        }
        // Along the trail, wherever the keys are; then the trail's own keys while they are in it.
        if (alt && (key == UI_KEY_UP || key == UI_KEY_DOWN))
                return files_crumb_go(files.trail.current + (key == UI_KEY_UP ? -1 : 1)), true;
        if (files.focus == FILES_FOCUS_CRUMBS && files_crumb_key(event))
                return true;
        if (files.focus == FILES_FOCUS_TREE && files_tree_key(event))
                return true;
        // F6 (and Tab from the trail): the rows, the trail, the tree, and the rows again (Shift+Tab is the band's buttons', the kit's).
        if ((key == UI_KEY_TAB || key == FILES_KEY_F6) && !shift && !control && !alt)
        {
                if (files.focus == FILES_FOCUS_CRUMBS)
                        files.focus = FILES_FOCUS_TREE, files.tree_at = files.tree_shown != FILES_NO ? files.tree_shown : 0;
                else
                        files.focus = FILES_FOCUS_CRUMBS, files.crumb_focus = files.trail.current;
                files.dirty = true;
                return true;
        }
        {
                static const struct
                {
                        p32 req;
                        int key, menu_id;
                } shortcuts[] = {
                    {0, FILES_KEY_F2, FILES_MENU_RENAME},
                    {WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT, FILES_KEY_N, FILES_MENU_NEW_FOLDER},
                    {WINDOW_KEY_CONTROL, FILES_KEY_H, FILES_MENU_HIDDEN},
                    {WINDOW_KEY_CONTROL, FILES_KEY_C, FILES_MENU_COPY},
                    {WINDOW_KEY_CONTROL, FILES_KEY_X, FILES_MENU_CUT},
                    {WINDOW_KEY_CONTROL, FILES_KEY_V, FILES_MENU_PASTE},
                    {WINDOW_KEY_CONTROL, FILES_KEY_1, FILES_MENU_DETAILS},
                    {WINDOW_KEY_CONTROL, FILES_KEY_1 + 1, FILES_MENU_ICONS},
                    {WINDOW_KEY_CONTROL, FILES_KEY_1 + 2, FILES_MENU_LIST},
                    {WINDOW_KEY_CONTROL, FILES_KEY_A, FILES_MENU_ALL},
                };

                for (int k = 0; k < FILES_COUNT(shortcuts); k++)
                        if (key == shortcuts[k].key && (event->flags & shortcuts[k].req) == shortcuts[k].req)
                                return files_menu_do(shortcuts[k].menu_id), files.dirty = true, true;
        }
        if (alt && (key == UI_KEY_LEFT || key == UI_KEY_RIGHT))
                return files_act(key == UI_KEY_RIGHT);
        if (key == UI_KEY_BACKSPACE)
                return files_act(2);
        if (key == FILES_KEY_F5 || (control && key == FILES_KEY_R))
                return files.said = 0, files_model_reload(m), files.dirty = true, true;
        if (control && key == FILES_KEY_F)
                return files_find(true), true;
        if (key == UI_KEY_ESCAPE && files.finding)
                return files_find(false), true;
        // A letter on the rows: the keys go to the first name that starts with what was typed in the last second.
        if (!control && !alt && event->b > ' ' && event->b != 0x7f && key != UI_KEY_SPACE)
                return files_type_ahead((p32)event->b), true;
        if ((shift && key == FILES_KEY_F10) || key == FILES_KEY_MENU)
        {
                int x, y;

                files_cursor_pos(&x, &y, true);
                files_menu_open(m->cursor != FILES_NO, x, y);
                return true;
        }
        if (alt && (key == UI_KEY_ENTER || key == UI_KEY_ENTER_PAD))
                return files_sheet_open(false, -1, -1), true;
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
                        return files_edit_go(), true;
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_COMMIT))
                {
                        files_edit(0);
                        if (event->type == WINDOW_EVENT_KEY)
                                return true;
                        files_layout();
                }
                else if (asks)
                        return files.said = 0, files.dirty = true, true;
                else if (event->type == WINDOW_EVENT_KEY)
                {
                        if (down && event->a == UI_KEY_TAB && !(event->flags & WINDOW_KEY_SHIFT))
                        {
                                if (files.editing == FILES_EDIT_PATH)
                                        files_edit_complete();
                                else if (files.editing == FILES_EDIT_PART)
                                        files_part_complete();
                        }
                        return true;
                }
        }
        else if (event->type == WINDOW_EVENT_KEY && down && (event->flags & WINDOW_KEY_CONTROL) && event->a == FILES_KEY_L)
                return files_edit(FILES_EDIT_PATH), true;
        // The find field: its keys while it has them, a press on it gives them to it; Enter gives them to the rows (what it found stays),
        // Escape ends the finding (every entry shown again), and its text is the filter as it is typed.
        else if (files.finding && (files.focus == FILES_FOCUS_FIND || event->type == WINDOW_EVENT_BUTTON))
        {
                if (event->type == WINDOW_EVENT_BUTTON && down &&
                    files_in_rect(ui_kit.pointer_x, ui_kit.pointer_y, files.finder.well_left, files.finder.well_top,
                                  files.finder.well_right - files.finder.well_left, files.finder.well_bottom - files.finder.well_top))
                        files.focus = FILES_FOCUS_FIND;
                if (files.focus == FILES_FOCUS_FIND)
                {
                        asks = ui_field_send(ui, &files.finder, &files.menu, event);
                        if (asks & (UI_TEXT_SUBMIT | UI_TEXT_COMMIT))
                                files.focus = FILES_FOCUS_ROWS;
                        else if (asks & UI_TEXT_CANCEL)
                                files_find(false);
                        if (asks & ~(p32)(UI_TEXT_SUBMIT | UI_TEXT_CANCEL | UI_TEXT_COMMIT))
                        {
                                static p8 text[FILES_NAME];
                                positive length = files_field_read(&files.finder, text, sizeof(text));

                                files_model_filter(m, text, length < sizeof(text) ? length : sizeof(text) - 1);
                                files.reseat = true, m->seat = 0, files.dirty = true;
                        }
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
                        files_model_sort(m, files_sort_cols[files.columns.sorted], files.columns.descending);
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
                files.dirty |= files.carrying; // the ghost follows the pointer
                // A press while carrying (Tab's) taken past the slop hands what is carried to the desktop's drag.
                if (files.carry_press && ui_press_past(ui, files.carry_px, files.carry_py, (int)event->a, (int)event->b))
                {
                        files_carry_offer();
                        files.dirty = true;
                        break;
                }
                // Both lists take motion through the kit's standardized ui_view_list_click (box-select, selected-item drag, UI scroll, hover).
                ui_view_list_click(ui, &files.big->view, event, event->a, event->b, null);
                if (l->band_moved)
                        files_band_apply();
                if (l->drag_began)
                        files_drag_start((p32)l->drag_row);
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
                if (down && event->a == UI_POINTER_LEFT && !files.editing &&
                    files_in_rect(ui_kit.pointer_x, ui_kit.pointer_y, files.crumbs_box.x, files.crumbs_box.y, files.crumbs_box.w, files.crumbs_box.h))
                {
                        int crumb = files_crumb_at(ui_kit.pointer_x, ui_kit.pointer_y);

                        // A folder of the trail is gone to, and a second press on it in time spells it again (the first one's move
                        // taken back); its ground, and the dots that stand for folders left out, open the field.
                        files.focus = FILES_FOCUS_ROWS;
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
                {
                        if (files.focus == FILES_FOCUS_CRUMBS)
                                files.focus = FILES_FOCUS_ROWS;
                        files.crumb_clicked = -1;
                }
                // Carrying (Tab): a press on a folder's row drops what is carried in it, a press on the rows' ground in the folder shown;
                // taken past the slop it hands what is carried to the desktop's drag.
                if (event->a == UI_POINTER_LEFT && down && files.carrying)
                {
                        files.carry_press = true, files.carry_px = ui_kit.pointer_x, files.carry_py = ui_kit.pointer_y;
                        break;
                }
                if (event->a == UI_POINTER_LEFT && !down && files.carry_press)
                {
                        files.carry_press = false;
                        if (ui_point(files.carry_px, l->x, l->w) >= l->x && files.carry_py >= l->y && files.carry_py < l->y + l->h)
                        {
                                int at = ui_list_hit(ui, l, files.carry_px, files.carry_py);
                                const struct files_entry address_to entry = at >= 0 ? files_model_entry(m, (p32)at) : null;

                                if (!entry || entry->kind == FILES_KIND_DIRECTORY)
                                        files_carry_drop(entry ? at : -1);
                        }
                        files.dirty = true;
                        break;
                }
                // Standardized list routing across the sidebar tree and main rows: whichever list is active or under the pointer owns it.
                {
                        struct ui_list address_to hit_list = null;

                        row = ui_view_list_click(ui, &files.big->view, event, ui_kit.pointer_x, ui_kit.pointer_y, &hit_list);
                        if (hit_list == &files.big->tree_list)
                        {
                                if (down && event->a == UI_POINTER_LEFT)
                                        files.focus = FILES_FOCUS_TREE;
                                if (row >= 0 && (p32)row < files.big->tree.count)
                                {
                                        files.tree_at = (p32)row;
                                        if (ui_list_tree_chevron_at(ui, hit_list, files.big->tree.node[row].depth, ui_kit.pointer_x))
                                                files_tree_toggle((p32)row);
                                        else if (hit_list->double_clicked)
                                        {
                                                // Double-click in the sidebar toggles expand/collapse (giving immediate feedback even when
                                                // already in that folder); a leaf folder with no children reloads and moves focus to rows.
                                                b32 moved = files_tree_go((p32)row);

                                                files_tree_toggle((p32)row);
                                                if (!moved && files.big->tree.node[row].read && !files.big->tree.node[row].has)
                                                        files.focus = FILES_FOCUS_ROWS, files_model_reload(m);
                                        }
                                        else if (files_tree_go((p32)row))
                                                files.dirty = true;
                                }
                                break;
                        }
                        if (hit_list != l)
                                break;
                }
                if (down && event->a == UI_POINTER_LEFT)
                        files.focus = FILES_FOCUS_ROWS;
                if (l->band_began)
                {
                        files.band_add = (event->flags & WINDOW_KEY_CONTROL) != 0;
                        memory_copy(files.big->band_base, m->chosen, FILES_MOST / 8), files.band_base_count = m->chosen_count;
                        files_band_apply();
                        break;
                }
                if (l->menu_asked)
                {
                        ui_view_cancel(ui, &files.big->view);
                        files_menu_at(l->menu_x, l->menu_y);
                        break;
                }
                if (down && l->down >= 0 && (p32)l->down != m->cursor)
                        m->cursor = (p32)l->down, m->revision++;
                if (l->ground_clicked)
                        files_model_choose_none(m), m->cursor = FILES_NO, m->revision++, files.dirty = true;
                if (row >= 0)
                {
                        files.said = 0;
                        if ((event->flags & WINDOW_KEY_TOUCH) || l->double_clicked)
                                files_open((p32)row);
                        else
                                files_model_choose(m, (p32)row, event->flags & WINDOW_KEY_CONTROL ? FILES_CHOOSE_TOGGLE : event->flags & WINDOW_KEY_SHIFT ? FILES_CHOOSE_RANGE : FILES_CHOOSE_ONLY);
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
        ui_side_place(ui, files.side_box.x, files.side_box.w);
        l->lit = l->focus >= 0, l->keys = files.focused && !files.big->view.group.keys && files.focus == FILES_FOCUS_ROWS;
        files.big->tree_list.lit = files.big->tree_list.focus >= 0, files.big->tree_list.keys = files.focused && files.focus == FILES_FOCUS_TREE;
        ui_view_draw(ui, &files.big->view, files.focused);
        if (files.view == FILES_VIEW_DETAILS)
                ui_columns_draw(ui, &files.columns);
        if (!files.editing || files.editing == FILES_EDIT_PART)
                files_crumbs_draw();
        if (files.editing)
                ui_text_overlay(ui, files_field()); // a finger's selection in the field: its handles, over the rows
        if (files.finding)
                ui_text_overlay(ui, &files.finder);
        files_carry_draw();
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
        b64 now = (b64)ui_kit.now;

        (void)other;
        // The git marks of a listing that is whole and is not the one they were worked out for, here and not in the picture: once a listing,
        // by the count of listings made whole (files_model.listed), which a listing of the same names' lengths changes too.
        if (m->state == FILES_READY && m->listed != files.git_key)
        {
                files.git = files_git_marks(m, files.big->git_state), files.git_key = m->listed;
                files.dirty = true;
        }
        // A drag over the window: held over a folder it opens it (a row's folder shown, a tree node's shown and opened in the tree), and
        // near a list's top or foot it scrolls that list, faster nearer the edge, a frame at a time.
        if (files.drop_over)
        {
                b64 at = (b64)ui_kit_time();
                struct ui_list address_to lists[2] = {&files.big->list, &files.big->tree_list};

                if (files.spring >= 0 && at - files.spring_at >= FILES_SPRING_NS)
                {
                        int spring = files.spring;

                        files.spring = -1, files.drop.nth = -2;
                        if (spring >= FILES_SPRING_NODE && (p32)(spring - FILES_SPRING_NODE) < files.big->tree.count)
                        {
                                p32 node = (p32)(spring - FILES_SPRING_NODE);

                                if (!files.big->tree.node[node].open)
                                        files_tree_toggle(node);
                                (void)files_tree_go(node);
                        }
                        else if (spring < FILES_SPRING_NODE)
                                files_open((p32)spring);
                }
                for (int k = 0; k < 2; k++)
                        if (ui_list_edge_scroll(&files.ui, lists[k], files.drop.x, files.drop.y, FILES_EDGE, FILES_EDGE_STEP, false))
                                files.dirty = true;
                timeout = ui_timeout_min(timeout, 16000000);
        }
        // A rubber band held near the list's top or foot scrolls the rows under it and keeps updating the selection.
        if (files.big->list.band)
        {
                struct ui_list address_to l = &files.big->list;

                if (ui_list_edge_scroll(&files.ui, l, l->x, l->band_y1, FILES_EDGE, FILES_EDGE_STEP, true))
                        files_band_apply(), files.dirty = true;
                timeout = ui_timeout_min(timeout, 16000000);
        }
        // The drop's pop: a picture as it begins and as it ends.
        if (files.flash_used)
        {
                b64 since = (b64)ui_kit_time() - files.flash_at;

                files.dirty = true;
                if (since >= FILES_FLASH_NS)
                        files.flash_used = 0;
                else
                        timeout = ui_timeout_min(timeout, (long)(FILES_FLASH_NS - since));
        }
        // The ghost of what is carried settling after it was picked up: a picture a frame till it is at rest.
        if (files.carrying && now - files.carry_at < FILES_CARRY_POP_NS + 20000000ll)
        {
                files.dirty = true;
                timeout = ui_timeout_min(timeout, 16000000);
        }
        // The active field's caret blinks while the path or find field has the keys.
        {
                struct ui_text address_to active = files.editing ? files_field() : files.finding && files.focus == FILES_FOCUS_FIND ? &files.finder : null;

                if (active)
                {
                        b32 redraw = false;

                        timeout = ui_text_timeout(&files.ui, active, timeout, &redraw);
                        files.dirty |= redraw;
                }
        }
        // A copy or a move under way: stepped here when it has no thread of its own, the foot counting while it runs, and when it ends
        // the foot says what it came to and the folder is read again.
        if (files.moving)
        {
                struct files_move address_to job = &files.big->move;
                b64 began = (b64)ui_kit_clock();

                while (!job->worker && files_move_running(job) && (b64)ui_kit_clock() - began < FILES_TURN_NS)
                        files_move_step(job);
                if (!files_move_running(job))
                {
                        if (files.measuring)
                        {
                                files.measuring = false;
                                if (files.sheet && files.menu.open)
                                        files_sheet_show();
                        }
                        else
                                files_move_said(), files_move_deed(job), files_model_reload(m), files.dirty = true;
                        files_move_end(job), files.moving = false;
                }
                else if (now >= files.next_progress)
                        files.next_progress = now + FILES_SHOW_NS, files.dirty = true;
                if (files.moving)
                        timeout = job->worker ? ui_timeout_min(timeout, (long)FILES_SHOW_NS) : 1;
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
                        files_after_delete(m);
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
                        if (!more && !files.big->list.scroll.scroll && !ui_body_going(&files.big->list.scroll.body))
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
                if (files_tree_sync_drives(&files.big->tree, (const p8 address_to)getenv((string_address) "HOME"), false))
                {
                        files_tree_follow();
                        files.dirty = true;
                }
                files.dirty |= became != FILES_FRESH;
                files.next_look = now + FILES_LOOK_NS;
                if (m->walking)
                        return 1;
        }
        timeout = ui_deadline_wait(timeout, now, files.next_look);
        return timeout == 0 ? 1 : timeout;
}

static int files_open_window(void)
{
        if (!(files.big = ui_app_map(&files_app, sizeof(struct files_big))))
                return 1;
        if (!files_model_open(&files.big->model))
        {
                compositor_kmsg((string_address) "files: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }
        {
                struct ui_list address_to lists[2] = {&files.big->list, &files.big->tree_list};

                for (int k = 0; k < 2; k++)
                {
                        lists[k]->virtual = true, lists[k]->beside = true;
                        lists[k]->row_h = FILES_ROW;
                        lists[k]->damaged = files_damaged;
                        lists[k]->hover = lists[k]->down = -1;
                }
                files.big->list.describe = files_describe, files.big->list.selected = files_row_selected;
                files.big->tree_list.tree = true, files.big->tree_list.describe = files_tree_describe;
        }
        files.path.kind = UI_INPUT, files.path.variant = UI_FIELD_BARE, files.path.edit = files_field();
        files.finder_field.kind = UI_INPUT, files.finder_field.variant = UI_FIELD_BARE, files.finder_field.edit = &files.finder;
        files.crumb_hover = -1, files.crumb_clicked = -1, files.drop_row = -1, files.drop_node = -1, files.spring = -1;
        files.columns.count = 4, files.columns.at_end[1] = true;
        files.columns.width[1] = 90, files.columns.width[2] = 110, files.columns.width[3] = 140;
        files.columns.sorted = 0, files.columns.hover = files.columns.held = -1;
        files_app.view = &files.big->view;
        files_move_end(&files.big->move);
        if (!files_tree_begin(&files.big->tree))
                return 1;
        files_tree_populate_live(&files.big->tree, (const p8 address_to)getenv((string_address) "HOME"));
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
                files_menu_do(id), files.dirty = true;
        else if (files.menu.field && ui_text_menu_do(&files.ui, files.menu.field, id))
                files.dirty = true;
}

static struct ui_app files_app = {
        .own = &files.own,
        .ui = &files.ui,
        .missing = (string_address) "files: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Files",
        .name = (string_address) "files",
        .glass = 0, // the kit's surface, as every window that stays is,
        .side = true, // with the folder tree its glass pane
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
