/*
        The file browser (src/canvas/ui/files.c): desktop view on files_model.c.
        Two-pane layout (sidebar folder/drive tree + path band (the kit's token field) + virtual details/icons/list rows + foot/find bar),
        drag-and-drop, Tab carrying, trash bin & undo/redo deeds, context menus, properties sheet, and git worktree marks.
        All disk names go through ui_label_foreign; all UI copy goes through WORDS(...)/LANGUAGE_*.
        Starts: files [PATH] [-f FONT] [-b BOLD].
*/
#ifndef MOONWATER_CANVAS_FILES
#define MOONWATER_CANVAS_FILES

#define FILES_WIDTH 720
#define FILES_HEIGHT 480
#define FILES_STEP 128
#define FILES_TURN_NS 4000000ll
#define FILES_SHOW_NS 100000000ll
#define FILES_LOOK_NS 1000000000ll
#define FILES_TYPE_NS 1000000000ll
#define FILES_CARRY_POP_NS 220000000ll
#define FILES_FLASH_NS 450000000ll
#define FILES_SPRING_NS 900000000ll
#define FILES_SPRING_NODE 0x10000
#define FILES_EDGE 28
#define FILES_EDGE_STEP 14
#define FILES_ASK_NAMES 3
#define FILES_BAND 32
#define FILES_ROW UI_ROW_DENSE
#define FILES_TILE_ICON 104
#define FILES_TILE_LIST 232
#define FILES_WALL_SCREENS 8
#define FILES_SHEET_LINES 10
#define FILES_GROUP_GAP 14 // logical px of space above the first row of a sidebar group (in place of a divider line)
#define FILES_DEEDS 32
#define FILES_DROP_MOST (FILES_MOVE_MOST * FILES_PATH / 4)
#define FILES_CLIP_CUT "application/x-moonwater-cut"
#define FILES_CENSUS_NS 30000000000ll     // the devices are read again this often when nothing said they changed
#define FILES_CENSUS_WAIT_NS 10000000000ll // and a reading that takes longer than this is given up
#define FILES_CENSUS_ROOM 32768

enum { FILES_KEY_1 = 2, FILES_KEY_R = 19, FILES_KEY_Y = 21, FILES_KEY_A = 30, FILES_KEY_F = 33, FILES_KEY_H = 35,
       FILES_KEY_L = 38, FILES_KEY_Z = 44, FILES_KEY_X = 45, FILES_KEY_C = 46, FILES_KEY_V = 47, FILES_KEY_N = 49,
       FILES_KEY_F2 = 60, FILES_KEY_F5 = 63, FILES_KEY_F6 = 64, FILES_KEY_F10 = 68, FILES_KEY_MENU = 127 };
enum { FILES_EDIT_PATH = 1, FILES_EDIT_NAME = 2 };
enum { FILES_VIEW_DETAILS, FILES_VIEW_ICONS, FILES_VIEW_LIST };
enum { FILES_FOCUS_ROWS, FILES_FOCUS_TREE, FILES_FOCUS_FIND };
enum { FILES_DEED_MOVE, FILES_DEED_COPY, FILES_DEED_RENAME, FILES_DEED_BIN };

#define FILES_PEERS_MOST 16
#define FILES_RPCS_MOST 16
enum { FILES_RPC_NONE, FILES_RPC_INFO, FILES_RPC_TREE_LIST, FILES_RPC_NAV_LIST, FILES_RPC_READ };

struct files_rpc
{
        p32 req_id;
        p8 kind, machine;
        p64 sent;
        p8 path[FILES_PATH];
};

struct files_peer
{
        p8 used, machine;
        p8 name[WATERLINK_NAME_MAX], key[32];
        p32 may;
        b32 online, info_loaded;
        struct link_fs_client fs;
};

struct files_remote_read
{
        b32 active;
        p8 machine;
        p32 req_id;
        bipolar fd;
        p64 offset;
        p8 remote_path[FILES_PATH], local_path[FILES_PATH], name[FILES_NAME];
};

struct files_big
{
        struct files_model model;
        struct ui_list list, tree_list;
        struct ui_view view;
        struct files_job job;
        struct files_move move;
        struct files_tree tree;
        struct files_devices devices;
        struct files_peer peers[FILES_PEERS_MOST];
        p32 peer_count;
        struct files_rpc rpcs[FILES_RPCS_MOST];
        struct files_remote_read remote_read;
        struct ui_tokens path;     // the path band: the kit's token field, a chip a folder of the trail
        char note[256], status[192], sheet[FILES_SHEET_LINES][FILES_NAME * 3 + 64];
        p8 band_base[FILES_MOST / 8], git_state[FILES_MOST];
        char census[FILES_CENSUS_ROOM];
};

struct files_deed { int kind; p32 count; p8 address_to pairs; positive room, used; b32 binned, restored; };

static struct
{
        struct window_row own;
        struct ui ui;
        struct files_big address_to big;
        int width, height, shown, editing, focus, view;
        int carry_px, carry_py, deed_count, deed_done, drop_row, drop_node, spring, sheet_x, sheet_y;
        unsigned int ground, said, carry_kind;
        b32 focused, dirty, reseat, armed, git, moving, tree_reseat, pasting, finding;
        b32 drag_chosen, carrying, cut_dim, band_add, carry_press, drop_over, sheet, sheet_folder, measuring, menu_view;
        p32 seen, naming, armed_at, drag_place, carry_count, band_base_count, tree_at, tree_shown;
        b64 next_look, next_show, carry_at, flash_at, spring_at, next_progress, typed_at, next_census;
        b32 census_again;
        struct ui_job runner;
        string_address start;
        struct ui_rect path_box, head_box, side_box, nav_box[4];
        struct ui_iconbutton nav[4];
        struct ui_rect view_box;
        struct ui_menubutton view_button;
        struct files_trail trail;
        struct ui_text field, finder;
        struct ui_control path, finder_field;
        struct ui_popup menu;
        struct ui_drag drag;
        struct ui_columns columns;
        p8 address_to carry_paths, carry_from[FILES_PATH], move_into[FILES_PATH], flash[4096], typed[32];
        positive carry_room, carry_used, flash_used, typed_length;
        struct files_deed deed[FILES_DEEDS];
        p64 git_key;
        struct ui_drop drop;
} files;

static struct ui_app files_app;
static const int files_sort_cols[4] = {FILES_SORT_NAME, FILES_SORT_SIZE, FILES_SORT_KIND, FILES_SORT_TIME};
static char files_wall_names[FILES_WALL_SCREENS][WINDOWS_OUTPUT_NAME], files_wall_labels[FILES_WALL_SCREENS][96];

#define FILES_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define FILES_SLICE(cond, step) for (b64 _b = (b64)ui_kit_clock(); (cond) && (b64)ui_kit_clock() - _b < FILES_TURN_NS;) (step)

static b32 files_transfer(const p8 address_to paths, p32 count, int row, int how);
static b32 files_act(int which);
static fn files_moved(void);
static fn files_open_with(b32 editor);
static fn files_remote_nav(void);
static struct files_peer address_to files_peer_by_machine(p8 machine);
static struct files_peer address_to files_peer_by_name(const p8 address_to name);

static inline b32 files_in_rect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
static inline fn files_clear_msg(void) { files.said = 0, files.big->note[0] = 0; }
static inline fn files_reload(void)
{
        if (files_model_is_remote(&files.big->model))
                files_remote_nav();
        else
                files_model_reload(&files.big->model);
        files.dirty = true;
}
static inline b32 files_tree_trash(const p8 address_to name, positive len) { return (len == 6 && !memory_compare(name, ".Trash", 6)) || (len == 5 && !memory_compare(name, "Trash", 5)); }
static b32 files_row_selected(int id) { return id >= 0 && files_model_chosen(&files.big->model, (p32)id); }
static fn files_damaged(int top, int bottom) { (void)top, (void)bottom, files.dirty = true; }
static positive files_name_text(char address_to into, positive room, const struct files_model address_to m, const struct files_entry address_to e) { return room > 2 ? ui_label_foreign(into, room - 1, files_model_name(m, e), e->length) : 0; }

static b32 files_name_in_list(const p8 address_to list, positive used, const p8 address_to name, positive length, b32 base_only)
{
        for (positive at = 0; at < used;)
        {
                const p8 address_to item = list + at, address_to cmp = base_only ? files_base_name(item) : item;
                if (string_length((string_address)cmp) == length && !memory_compare(cmp, name, length))
                        return true;
                at += string_length((string_address)item) + 1;
        }
        return false;
}

static inline b32 files_carried(const p8 address_to name, positive len)
{
        return (files.carrying || files.cut_dim) && string_equals((string_address)files.carry_from, (string_address)files.big->model.path) &&
               files_name_in_list(files.carry_paths, files.carry_used, name, len, true);
}

static inline b32 files_flashed(const p8 address_to name, positive len)
{
        return files.flash_used && (b64)ui_kit_time() - files.flash_at < FILES_FLASH_NS && files_name_in_list(files.flash, files.flash_used, name, len, false);
}

/* ---- formatting, rows, tree, status ---- */

static fn files_format_time(char address_to into, b64 seconds, char sep, b32 with_seconds)
{
        static const char punct[5] = {'-', '-', ' ', ':', ':'};
        tm broken;
        time_t at = (time_t)seconds;
        int year, count = with_seconds ? 5 : 4;
        into[0] = 0;
        if (!localtime_r(&at, &broken) || (year = broken.tm_year + 1900) < 0 || year > 9999)
                return;
        {
                int parts[5] = {broken.tm_mon + 1, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec};
                into[0] = (char)('0' + year / 1000), into[1] = (char)('0' + year / 100 % 10);
                into[2] = (char)('0' + year / 10 % 10), into[3] = (char)('0' + year % 10);
                for (int k = 0; k < count; k++)
                        into[4 + 3 * k] = k == 2 ? sep : punct[k], into[5 + 3 * k] = (char)('0' + parts[k] / 10), into[6 + 3 * k] = (char)('0' + parts[k] % 10);
                into[4 + 3 * count] = 0;
        }
}

static b32 files_is_home(const p8 address_to path, const p8 address_to name, positive length)
{
        static string_address home;
        positive at = string_length((string_address)path);
        if (!home && !(home = getenv((string_address) "HOME")))
                home = (string_address) "";
        at = at == 1 ? 0 : at;
        return home[0] && string_length(home) > at && !memory_compare(home, path, at) && home[at] == '/' &&
               string_length((string_address)(home + at + 1)) == length && !memory_compare(home + at + 1, name, length);
}

static unsigned int files_symbol(const struct files_model address_to m, const struct files_entry address_to e)
{
        const p8 address_to name = files_model_name(m, e);
        p32 mode = e->looked ? e->mode : e->kind == FILES_KIND_DIRECTORY ? 0040000u : e->kind == FILES_KIND_FILE ? 0100000u : 0;
        unsigned int kind = (unsigned int)ui_file_kind((const char address_to)name, mode);
        if (kind == UI_FILE_FOLDER)
                kind = files_is_home(m->path, name, e->length) ? UI_FILE_HOME : files_tree_trash(name, e->length) ? UI_FILE_TRASH : kind;
        return kind | (e->kind == FILES_KIND_LINK ? UI_FILE_LINKED : 0);
}

static const char address_to files_extension_label(const struct files_model address_to m, const struct files_entry address_to e)
{
        static char label[8];
        char ext[8];
        if (e->kind == FILES_KIND_DIRECTORY)
                return null;
        ui_file_extension((const char address_to)files_model_name(m, e), ext, sizeof(ext));
        if (!ext[0] || string_length((string_address)ext) > 4)
                return null;
        return label[0] = '.', string_copy_bounded((string_address)label + 1, (string_address)ext, sizeof(label) - 1), label;
}

static fn files_describe(int id, struct ui_row address_to row)
{
        static char label[FILES_NAME * 3 + 4], size[16], when[24], type[48];
        const struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = id >= 0 ? files_model_entry(m, (p32)id) : null;
        const p8 address_to name;
        if (!e)
                return;
        name = files_model_name(m, e);
        files_name_text(label, sizeof(label), m, e);
        row->label = label, size[0] = when[0] = 0;
        if (e->looked)
        {
                if (e->kind == FILES_KIND_FILE)
                        ui_size_text(size, sizeof(size), e->size);
                files_format_time(when, e->modified, ' ', false);
        }
        ui_file_type(type, sizeof(type), name, e->length, e->kind == FILES_KIND_DIRECTORY, e->kind == FILES_KIND_LINK, e->kind == FILES_KIND_FILE);
        row->cells[1] = size, row->cells[2] = type, row->cells[3] = when;
        row->file = 1 + files_symbol(m, e), row->extension = files_extension_label(m, e);
        row->dim = files_carried(name, e->length), row->glow = files_flashed(name, e->length);
        if (files.git && e >= m->shown.entry && e < m->shown.entry + m->shown.count)
        {
                p8 mark = files.big->git_state[e - m->shown.entry];
                row->dim |= mark == FILES_GIT_IGNORED;
                row->ink = mark == FILES_GIT_UNTRACKED ? (ui_kit_light(&files.ui) ? 0xff00838fu : 0xff5fd7e0u) : 0;
        }
        row->mark = files_model_chosen(m, (p32)id) && m->chosen_count > 1;
}

/*
        A row of the Devices group: the group (the word Settings' group of devices has), a disk (what it says it is, or its name; its
        size), or a partition (its file system's label, else its name in the table, else its node; its file system's type and its size,
        a dot where it is mounted, quiet where nothing known is on it).
*/
static fn files_device_describe(const struct files_tree address_to t, const struct files_node address_to n, int id, struct ui_row address_to row)
{
        static char label[FILES_NAME * 3 + 4], size[16], type[48];
        const struct files_devices address_to d = &files.big->devices;
        const p8 address_to name = files_tree_name(t, (p32)id);
        row->file = 1 + UI_FILE_DRIVE, row->indent = n->depth ? n->depth - 1 : 0;
        if (n->kind == FILES_NODE_DEVICES)
        {
                row->label = language_of("Devices"), row->gap = FILES_GROUP_GAP;
                row->expander = n->open ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
                return;
        }
        ui_label_foreign(label, sizeof(label), name, n->length), row->label = label;
        if (n->kind == FILES_NODE_DISK && n->device < d->disks)
        {
                ui_size_text(size, sizeof(size), d->disk_bytes[n->device]), row->value = size;
                row->expander = !n->has ? 0 : n->open ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
                return;
        }
        if (n->kind != FILES_NODE_PART || n->device >= d->parts)
                return;
        ui_size_text(size, sizeof(size), d->part_bytes[n->device]), row->value = size;
        if (d->part_type[n->device][0])
                ui_label_foreign(type, sizeof(type), d->part_type[n->device], string_length((string_address)d->part_type[n->device])), row->note = type;
        else
                row->dim = true;
        if (files_part_mount(d, n->device))
                row->mark = true, row->mark_ink = ui_role(UI_SUCCESS);
}

static fn files_tree_describe(int id, struct ui_row address_to row)
{
        static const struct { unsigned int word, icon; } places[] = {
            [FILES_PLACE_HOME] = {LANGUAGE_files_place_home, UI_FILE_HOME}, [FILES_PLACE_DOWNLOADS] = {LANGUAGE_files_place_downloads, UI_FILE_DOWNLOADS},
            [FILES_PLACE_PICTURES] = {LANGUAGE_files_place_pictures, UI_FILE_PICTURES}, [FILES_PLACE_ROOT] = {LANGUAGE_files_drive_root, UI_FILE_DRIVE},
        };
        static char label[FILES_NAME * 3 + 4];
        const struct files_tree address_to t = &files.big->tree;
        const struct files_node address_to n = id >= 0 && (p32)id < t->count ? &t->node[id] : null;
        unsigned int icon;
        if (!n)
                return;
        if (n->kind >= FILES_NODE_DEVICES)
        {
                files_device_describe(t, n, id, row);
                return;
        }
        if (n->kind == FILES_NODE_MACHINE)
        {
                if (n->machine == 0)
                        row->label = language_text(LANGUAGE_files_this_machine);
                else
                {
                        const p8 address_to name = files_tree_name(t, (p32)id);
                        ui_label_foreign(label, sizeof(label), name, n->length), row->label = label;
                        if (n->flags & FILES_NODE_ONLINE)
                                row->mark = true, row->mark_ink = ui_role(UI_SUCCESS);
                        if (!(n->flags & FILES_NODE_ONLINE) || !(n->flags & FILES_NODE_MAY_FILES))
                                row->dim = true;
                }
                icon = UI_FILE_MACHINE;
                row->indent = n->depth ? n->depth - 1 : 0, row->file = 1 + icon;
                row->expander = n->open ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
                row->gap = id > 0 && n->machine != 0 ? FILES_GROUP_GAP : 0;
                return;
        }
        if (n->depth == 1 && n->place > FILES_PLACE_NONE && n->place <= FILES_PLACE_ROOT)
                row->label = language_text(places[n->place].word), icon = places[n->place].icon;
        else
        {
                const p8 address_to name = files_tree_name(t, (p32)id);
                ui_label_foreign(label, sizeof(label), name, n->length), row->label = label;
                icon = n->kind == FILES_NODE_DRIVE ? UI_FILE_DRIVE : files_tree_trash(name, n->length) ? UI_FILE_TRASH : n->open ? UI_FILE_FOLDER_OPEN : UI_FILE_FOLDER;
        }
        row->indent = n->depth ? n->depth - 1 : 0, row->file = 1 + icon;
        row->expander = n->open ? UI_ROW_EXPANDER_OPEN : n->read && !n->has ? 0 : UI_ROW_EXPANDER_SHUT;
        row->gap = id > 0 && n->kind == FILES_NODE_DRIVE && t->node[id - 1].kind != FILES_NODE_DRIVE && t->node[id - 1].kind != FILES_NODE_MACHINE ? FILES_GROUP_GAP : 0;
}

static fn files_tree_follow(void)
{
        const struct files_model address_to m = &files.big->model;
        struct files_peer address_to p = files_model_is_remote(m) ? files_peer_by_name(m->peer) : null;
        p8 machine = p ? p->machine : 0;
        files.tree_shown = files_tree_reveal_in(&files.big->tree, machine, m->path);
        if (files.tree_shown != FILES_NO)
                files.tree_at = files.tree_shown, files.tree_reseat = true;
}

static unsigned int files_state_said(int state)
{
        static const unsigned int map[] = {
            [FILES_READING] = LANGUAGE_files_reading, [FILES_READY] = LANGUAGE_files_empty,
            [FILES_UNREADABLE] = LANGUAGE_files_unreadable, [FILES_GONE] = LANGUAGE_files_gone, [FILES_NOT_DIRECTORY] = LANGUAGE_files_not_folder,
        };
        return state >= 0 && state < FILES_COUNT(map) && map[state] ? map[state] : LANGUAGE_files_failed;
}

static inline unsigned int files_did_said(int did)
{
        return did == FILES_DID ? 0 : did == FILES_DID_EXISTS ? LANGUAGE_files_name_taken : did == FILES_DID_BAD ? LANGUAGE_files_name_bad : LANGUAGE_files_unreadable;
}

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
                p32 id = __atomic_load_n(&job->items_done, __ATOMIC_ACQUIRE), it = __atomic_load_n(&job->items_total, __ATOMIC_ACQUIRE);
                ui_size_text(done, sizeof(done), __atomic_load_n(&job->bytes_done, __ATOMIC_ACQUIRE));
                ui_size_text(whole, sizeof(whole), __atomic_load_n(&job->bytes_total, __ATOMIC_ACQUIRE));
                language_format(into, room, job->how == FILES_MOVE_MOVE ? WORDS(files_moving) : WORDS(files_copying), (int)id, (int)(it > id ? it : id), done, whole);
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
                p64 bytes = 0;
                char size[16];
                for (p32 p = 0; p < files_model_count(m); p++)
                {
                        const struct files_entry address_to e = files_model_entry(m, p);
                        if (e->kind == FILES_KIND_FILE && files_model_chosen(m, p))
                                bytes += e->size;
                }
                ui_size_text(size, sizeof(size), bytes);
                language_format(into, room, WORDS(files_chosen_size), (int)m->chosen_count, (int)m->shown.count, size);
        }
        else if (m->state == FILES_READY && m->filter_length)
                language_format(into, room, WORDS(files_count_found), (int)files_model_count(m), (int)m->shown.count);
        else if (m->state == FILES_READY || (m->state == FILES_READING && m->shown.count))
                language_format(into, room, m->shown.cut ? WORDS(files_count_cut) : m->shown.count == 1 ? WORDS(files_count_one) : WORDS(files_count), (int)m->shown.count);
}

/* ---- fields, the path band, layout ---- */

static struct ui_text address_to files_field_init(struct ui_text address_to f, positive most)
{
        if (!f->doc.selections && ui_field_open(f, &files.own, ""))
                f->most = most;
        return f;
}

static inline struct ui_text address_to files_field(void) { return files_field_init(&files.field, FILES_NAME - 1); }
static inline positive files_field_read(struct ui_text address_to t, p8 address_to into, positive room) { positive n = ui_text_string(t, into, room); return into[n < room ? n : room - 1] = end, n; }

// The path band at rest: its text is the trail (the folders deeper than the one shown stay as chips after it), the caret at the end of
// the folder shown, so that folder is the field's own text and every other is a chip. Set only when it differs.
static fn files_path_sync(void)
{
        struct ui_tokens address_to k = &files.big->path;
        const struct files_trail address_to t = &files.trail;
        const p8 address_to want = t->count ? t->path : files.big->model.path;
        positive length = string_length((string_address)want), caret = t->count ? t->to[t->current] : length;
        static p8 had[UI_TOKENS_TEXT];
        const struct doc_selection address_to sel;
        if (!k->text.doc.selections || files.editing == FILES_EDIT_PATH)
                return;
        if (length >= UI_TOKENS_TEXT)
                length = UI_TOKENS_TEXT - 1, caret = caret < length ? caret : length;
        if (doc_length(&k->text.doc) != length || doc_read(&k->text.doc, 0, had, length) != length || memory_compare(had, want, length))
                ui_text_set(&k->text, want, length);
        sel = &k->text.doc.selections[k->text.doc.primary];
        if (k->text.doc.selection_count != 1 || sel->active != caret || sel->anchor != caret)
                doc_select_set(&k->text.doc, caret, caret);
        k->invalid = false;
}

// The top bar's navigation buttons, in their order across it (the view mirrors the order in RTL, where the kit swaps back's and forward's
// arrows): a symbol, what it does, and its words, the label it had as a button of text, now its tooltip and its accessible name.
static const struct { int glyph, act; unsigned int words; } files_navs[] = {
    {UI_ICON_BACK, FILES_ACT_BACK, LANGUAGE_files_back}, {UI_ICON_FORWARD, FILES_ACT_FWD, LANGUAGE_files_forward},
    {UI_ICON_UP, FILES_ACT_UP, LANGUAGE_files_up}, {UI_ICON_REFRESH, FILES_ACT_RELOAD, LANGUAGE_files_refresh},
};
#define FILES_NAVS FILES_COUNT(files_navs)
_Static_assert(sizeof(files_navs) / sizeof(files_navs[0]) == sizeof(files.nav) / sizeof(files.nav[0]), "a button of the bar for each of the table's");

// The View button's width (logical px): its words and the kit's menu button's padding either side, the gap and the chevron (ui_menubutton_draw).
static int files_view_wide(struct ui address_to ui)
{
        int words = ui->regular ? ui_kit_measure(ui, (string_address)WORDS(files_view), UI_LABEL_LARGE) : 0, scale = ui->scale > 0 ? ui->scale : 100;
        return (words * 100 + scale - 1) / scale + 2 * UI_MENUBUTTON_PAD + 18;
}

// The buttons where the view laid them out: back with no way back, forward with none forward and up at a root do nothing, and say so.
static fn files_bar_lay(void)
{
        const struct files_model address_to m = &files.big->model;
        const b32 off[FILES_NAVS] = {!m->back_count, !m->forward_count, m->path[0] == '/' && !m->path[1], false};
        for (int k = 0; k < FILES_NAVS; k++)
        {
                struct ui_iconbutton address_to b = &files.nav[k];
                const struct ui_rect address_to r = &files.nav_box[k];
                b->glyph = files_navs[k].glyph, b->name = b->tip = language_text(files_navs[k].words);
                b->x = r->x, b->y = r->y, b->w = r->w, b->h = r->h, b->disabled = off[k];
                if (b->disabled)
                        b->hover = b->down = false;
        }
        files.view_button.label = WORDS(files_view), files.view_button.open = files.menu.open && files.menu_view;
        files.view_button.x = files.view_box.x, files.view_button.y = files.view_box.y, files.view_button.w = files.view_box.w, files.view_button.h = files.view_box.h;
}

static fn files_layout(void)
{
        static const struct { int stripe, tiles, tile_h; } modes[3] = {
            [FILES_VIEW_DETAILS] = {2, 0, 0}, [FILES_VIEW_ICONS] = {0, FILES_TILE_ICON, 0}, [FILES_VIEW_LIST] = {0, FILES_TILE_LIST, FILES_ROW},
        };
        const char address_to cols[4] = {WORDS(files_column_name), WORDS(files_column_size), WORDS(files_column_type), WORDS(files_column_modified)};
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        files.big->tree_list.total = (int)files.big->tree.count;
        files.big->tree_list.focus = files.focus == FILES_FOCUS_TREE ? (int)files.tree_at : files.tree_shown != FILES_NO ? (int)files.tree_shown : -1;
        files_status_text();
        files.path.focused = files.editing == FILES_EDIT_NAME && files.focused;
        files.finder_field.focused = files.finding && files.focus == FILES_FOCUS_FIND && files.focused;
        files.finder_field.items[0] = WORDS(files_find_hint);
        files.path.items[0] = files.editing == FILES_EDIT_NAME ? WORDS(files_rename_hint) : null;
        l->total = (int)files_model_count(m), l->focus = m->cursor != FILES_NO ? (int)m->cursor : -1;
        l->empty = m->state == FILES_NONE ? "" : language_text(files_state_said(m->state));
        l->columns = files.view == FILES_VIEW_DETAILS ? &files.columns : null;
        l->stripe = modes[files.view].stripe, l->tiles = modes[files.view].tiles, l->tile_h = modes[files.view].tile_h;
        ui_view_begin(&files.big->view, ui, files.width, -files.height);
        ui_side_frame()
        {
                ui_side_pane(&files.side_box)
                        ui_col(.class = "rows", .grow = 1)
                                ui_rows(&files.big->tree_list, .class = "list", .grow = 1);
                ui_side_body()
                {
                        ui_row(.class = "pathbar", .h = FILES_BAND + UI_GAP2 * 2)
                        {
                                for (int k = 0; k < FILES_NAVS; k++)
                                        ui_host(&files.nav_box[k], .w = FILES_BAND, .h = FILES_BAND);
                                if (files.editing == FILES_EDIT_NAME)
                                        ui_control(&files.path, .grow = 1, .h = FILES_BAND);
                                else
                                        ui_host(&files.path_box, .grow = 1, .h = FILES_BAND);
                                ui_host(&files.view_box, .w = files_view_wide(ui), .h = FILES_BAND);
                        }
                        ui_col(.class = "rows", .grow = 1)
                        {
                                ui_host(&files.head_box, .h = UI_COLUMNS_H, .hidden = files.view != FILES_VIEW_DETAILS);
                                ui_rows(l, .class = "list", .grow = 1);
                        }
                        if (files.finding)
                                ui_row(.class = "bar")
                                        ui_control(&files.finder_field, .grow = 1, .h = FILES_BAND);
                }
        }
        ui_view_end(&files.big->view);
        if (files.reseat)
                ui_scroll_set(&l->scroll, (int)m->seat), files.reseat = false;
        m->seat = l->scroll.scroll;
        for (int k = 0; k < 4; k++)
                files.columns.title[k] = cols[k];
        ui_columns_layout(ui, &files.columns, l->x, files.head_box.y, l->w);
        if (files.tree_reseat && files.big->tree_list.h > 0)
                files.big->tree_list.focus = (int)files.tree_shown, ui_list_reveal(ui, &files.big->tree_list), files.tree_reseat = false;
        files_bar_lay(), files_path_sync();
}

static fn files_show_cursor(void) { if (files.big->model.cursor != FILES_NO) files_layout(), ui_list_reveal(&files.ui, &files.big->list), files.dirty = true; }

/* ---- bin, deeds, flash ---- */

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

static inline b32 files_in_bin(void) { p8 bin[FILES_PATH]; return files_bin_root(bin, "/files") && string_equals((string_address)bin, (string_address)files.big->model.path); }

static fn files_now_text(char address_to into)
{
        timespec real = {0, 0};
        clock_gettime(CLOCK_REALTIME, address_of real);
        files_format_time(into, (b64)real.tv_sec, 'T', true);
        if (!into[0])
                string_copy_bounded((string_address)into, (string_address) "1970-01-01T00:00:00", 20);
}

static inline fn files_deed_free(struct files_deed address_to d) { array_store_release(d->pairs, d->room, d->used), memory_zero(d, sizeof(*d)); }
static inline fn files_deed_keep(struct files_deed address_to d) { if (!d->count) files_deed_free(d), files.deed_count--, files.deed_done = files.deed_count; }

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
        d = &files.deed[files.deed_count++], files.deed_done = files.deed_count;
        return memory_zero(d, sizeof(*d)), d->kind = kind, d;
}

static fn files_deed_add(struct files_deed address_to d, const p8 address_to from, const p8 address_to to)
{
        positive a = string_length((string_address)from) + 1, b = string_length((string_address)to) + 1;
        if (!array_store_reserve(d->pairs, d->room, d->used, d->used + a + b, 4096))
                return;
        memory_copy(d->pairs + d->used, from, a), memory_copy(d->pairs + d->used + a, to, b);
        d->used += a + b, d->count++;
}

static fn files_move_deed(const struct files_move address_to job)
{
        const p8 address_to path = files.big->model.path;
        positive length = string_length((string_address)path), into = string_length((string_address)files.move_into);
        struct files_deed address_to d;
        p8 to[FILES_PATH];
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
        if (job->how == FILES_MOVE_MEASURE || !files.move_into[0])
                return;
        d = files_deed_new(job->how == FILES_MOVE_MOVE ? FILES_DEED_MOVE : FILES_DEED_COPY);
        for (p32 k = 0; k < job->count; k++)
        {
                const p8 address_to landed = files_move_landed(job, k);
                if (landed[0] && job->copied[k] && files_path_join(to, FILES_PATH, files.move_into, landed))
                        files_deed_add(d, files_move_path(job, k), to);
        }
        files_deed_keep(d);
}

static p32 files_deed_unpack(const struct files_deed address_to d, const p8 address_to (*pair)[2])
{
        p32 count = 0;
        for (positive at = 0; at < d->used && count < FILES_MOVE_MOST; count++)
                pair[count][0] = d->pairs + at, at += string_length((string_address)d->pairs + at) + 1,
                pair[count][1] = d->pairs + at, at += string_length((string_address)d->pairs + at) + 1;
        return count;
}

static fn files_undo(b32 again)
{
        p8 bin[FILES_PATH], named[FILES_NAME], where[FILES_PATH];
        char when[24];
        const p8 address_to (*pair)[2];
        struct files_deed address_to d, turned, again_d;
        b32 has_bin, back = !again;
        int done = 0;
        files_clear_msg();
        if (again ? files.deed_done >= files.deed_count : files.deed_done <= 0)
        {
                files.said = again ? LANGUAGE_files_nothing_to_redo : LANGUAGE_files_nothing_to_undo, files.dirty = true;
                return;
        }
        d = &files.deed[again ? files.deed_done : files.deed_done - 1];
        if ((pair = (const p8 address_to (*)[2])files_anon_map(sizeof(*pair) * FILES_MOVE_MOST, null)))
        {
                turned = (struct files_deed){.kind = d->kind}, again_d = (struct files_deed){.kind = d->kind, .restored = d->restored};
                has_bin = files_bin_root(bin, null), files_now_text(when);
                for (p32 k = files_deed_unpack(d, pair); k-- > 0;)
                {
                        const p8 address_to from = pair[k][0], address_to to = pair[k][1], address_to nf = from, address_to nt = to;
                        b32 ok = false;
                        if (d->kind == FILES_DEED_MOVE || d->kind == FILES_DEED_RENAME)
                                ok = files_rename_nr(AT_FDCWD, back ? to : from, AT_FDCWD, back ? from : to) >= 0;
                        else if (has_bin)
                        {
                                b32 into_bin = d->kind == FILES_DEED_COPY ? !d->binned : (d->restored ? back : !back);
                                const p8 address_to src = d->kind == FILES_DEED_COPY ? to : from;
                                if (into_bin && (ok = files_bin_put(bin, src, when, named) == FILES_DID))
                                        files_bin_join(where, bin, "/files", named, null), nf = src, nt = where;
                                else if (!into_bin && (ok = files_bin_restore(bin, files_base_name(to), where) == FILES_DID))
                                        nf = where, nt = d->kind == FILES_DEED_COPY ? where : to;
                        }
                        files_deed_add(&turned, nf, nt), done += ok;
                }
                for (p32 k = files_deed_unpack(&turned, pair); k-- > 0;)
                        files_deed_add(&again_d, pair[k][0], pair[k][1]);
                again_d.binned = d->kind == FILES_DEED_COPY ? (d->binned ? !done && d->binned : done > 0) : false;
                files_unmap((address_any)pair, sizeof(*pair) * FILES_MOVE_MOST);
                files_deed_free(&turned), files_deed_free(d), *d = again_d;
                files.deed_done += again ? 1 : -1;
                language_format(files.big->note, sizeof(files.big->note), again ? WORDS(files_redone) : WORDS(files_undone), done);
        }
        else
                files.said = LANGUAGE_files_failed;
        files_reload();
}

/* ---- user actions: edit, delete, new folder, bin ---- */

// Editing begun (FILES_EDIT_NAME: the cursor's name; FILES_EDIT_PATH: the path band, the folder shown its text, all of it selected) or ended (0).
static fn files_edit(int how)
{
        const struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, m->cursor);
        struct ui_tokens address_to k = &files.big->path;
        if (how == FILES_EDIT_NAME && !e)
                return;
        files.editing = how;
        if (how == FILES_EDIT_NAME)
                ui_text_set(files_field(), files_model_name(m, e), e->length), files.naming = m->cursor;
        if (how == FILES_EDIT_PATH)
                ui_text_set(&k->text, m->path, string_length((string_address)m->path));
        ui_tokens_focus(k, how == FILES_EDIT_PATH, true), k->invalid = false;
        files.focus = FILES_FOCUS_ROWS, files.armed = false, files_clear_msg(), files.dirty = true;
}

static p32 files_ask_names(char address_to into, positive room)
{
        const struct files_model address_to m = &files.big->model;
        positive used = 0;
        p32 count = 0, iter = 0, place;
        into[0] = 0;
        while ((place = files_selected_next(m, &iter)) != FILES_NO)
        {
                const struct files_entry address_to e = files_model_entry(m, place);
                if (count++ >= FILES_ASK_NAMES || used + 80 > room)
                        continue;
                if (used)
                        into[used++] = ',', into[used++] = ' ';
                used += ui_label_foreign(into + used, 64, files_model_name(m, e), e->length);
        }
        if (count > FILES_ASK_NAMES && used + 16 < room)
                into[used++] = ' ', into[used++] = '+', used += positive_into((p8 address_to)into + used, count - FILES_ASK_NAMES), into[used] = 0;
        return count;
}

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

static fn files_delete(void)
{
        struct files_model address_to m = &files.big->model;
        char names[256];
        p32 count = files_ask_names(names, sizeof(names));
        files.said = 0;
        if (!count || files_model_is_remote(m) || m->state != FILES_READY || files.big->job.state == FILES_JOB_RUNNING)
                return;
        if (files.armed && files.armed_at == m->revision)
        {
                files.armed = false, files.big->note[0] = 0;
                if (!files_delete_begin(m, &files.big->job))
                        files.said = LANGUAGE_files_changed, files_model_reload(m);
                files.dirty = true;
                return;
        }
        files.armed = true, files.armed_at = m->revision;
        language_format(files.big->note, sizeof(files.big->note), files_in_bin() ? WORDS(files_delete_ask) : WORDS(files_delete_for_good), (int)count, names);
        files.dirty = true;
}

static fn files_new_folder(void)
{
        struct files_model address_to m = &files.big->model;
        static p8 name[FILES_NAME];
        int did = FILES_DID_EXISTS;
        if (files_model_is_remote(m))
                return;
        for (int n = 1; n < 100 && did == FILES_DID_EXISTS; n++)
        {
                string_copy_bounded(name, (string_address)WORDS(files_new_folder), FILES_NAME - 8);
                if (n > 1)
                {
                        positive u = string_length(name);
                        name[u++] = ' ', name[u + positive_into(name + u, (positive)n)] = end;
                }
                did = files_model_make_folder(m, name);
        }
        files.big->note[0] = 0, files.said = files_did_said(did), files.dirty = true;
        if (did == FILES_DID)
                files_model_reload(m), string_copy_bounded(m->came, name, FILES_NAME);
}

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
                files_clear_msg(), files.armed = false;
                if (files_model_is_remote(m) || m->state != FILES_READY)
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
                const struct files_entry address_to e = files_model_entry(m, at);
                if (!e)
                        continue;
                if (restore)
                {
                        files_bin_join(where, bin, "/files", files_model_name(m, e), null);
                        if (files_bin_restore(bin, files_model_name(m, e), path) == FILES_DID)
                                done++, files_deed_add(d, path, where);
                }
                else if (files_path_join(path, FILES_PATH, m->path, files_model_name(m, e)))
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
        files_deed_keep(d), files_reload();
}

static fn files_bin_empty(void)
{
        struct files_model address_to m = &files.big->model;
        p8 bin[FILES_PATH], info[FILES_PATH];
        if (!files_in_bin() || !files_model_count(m) || !files_bin_root(bin, null))
                return;
        b32 sure = files.armed && files.armed_at == m->revision;
        if (!sure)
                files_select_all(m);
        if (sure)
                for (p32 at = 0; at < files_model_count(m); at++)
                {
                        const struct files_entry address_to e = files_model_entry(m, at);
                        if (e && files_bin_join(info, bin, "/info", files_model_name(m, e), ".trashinfo"))
                                system_remove_at(AT_FDCWD, (string_address)info, 0);
                }
        files_delete();
}

/* ---- URIs, carrying, clipboard, drag & drop ---- */

static p32 files_paths_collect(p32 single, b32 use_chosen, p8 address_to address_to out, positive address_to room, positive address_to used, unsigned int address_to first_kind)
{
        const struct files_model address_to m = &files.big->model;
        positive folder = string_length((string_address)m->path);
        p32 count = 0;
        for (p32 at = 0; at < files_model_count(m); at++)
        {
                const struct files_entry address_to e = files_model_entry(m, at);
                const p8 address_to name = e ? files_model_name(m, e) : null;
                positive len = name ? string_length((string_address)name) : 0;
                if (!name || (use_chosen ? !files_model_chosen(m, at) : at != single))
                        continue;
                if (!array_store_reserve(*out, *room, *used, *used + folder + len + 2, 4096))
                        break;
                files_path_join(*out + *used, *room - *used, m->path, name);
                *used += string_length((string_address)(*out + *used)) + 1;
                if (!count++ && first_kind)
                        *first_kind = files_symbol(m, e);
        }
        return count;
}

static b32 files_uri_pack(const p8 address_to list, positive list_used, struct windows_clip_part address_to parts, p8 address_to address_to uris,
                          positive address_to uris_room, p8 address_to address_to text, positive address_to text_room)
{
        positive uu = 0, tu = 0;
        *uris = *text = null, *uris_room = *text_room = 0;
        for (positive at = 0; at < list_used;)
        {
                const p8 address_to path = list + at;
                positive len = string_length((string_address)path);
                at += len + 1;
                if (!array_store_reserve(*uris, *uris_room, uu, uu + 3 * len + 12, 4096) ||
                    !array_store_reserve(*text, *text_room, tu, tu + len + 2, 4096))
                        break;
                memory_copy(*uris + uu, "file://", 7), uu += 7;
                for (positive k = 0; k < len; k++)
                        uu += files_uri_byte(*uris + uu, path[k]);
                (*uris)[uu++] = '\r', (*uris)[uu++] = '\n';
                memory_copy(*text + tu, path, len), tu += len, (*text)[tu++] = '\n';
        }
        if (!uu || !tu)
                return array_store_release(*uris, *uris_room, uu), array_store_release(*text, *text_room, tu), false;
        parts[0] = (struct windows_clip_part){WINDOWS_CLIP_URIS, *uris, uu};
        parts[1] = (struct windows_clip_part){WINDOWS_CLIP_TEXT, *text, tu - 1};
        return true;
}

static inline fn files_uris_free(p8 address_to uris, positive ur, p8 address_to paths, positive pr, const struct windows_clip_part address_to parts)
{
        positive ul = parts[0].length, pl = parts[1].length + 1;
        array_store_release(uris, ur, ul), array_store_release(paths, pr, pl);
}

static b32 files_drag_offer_list(const p8 address_to list, positive list_used, p32 items, p32 kind)
{
        struct windows_clip_part parts[2];
        p8 address_to uris, address_to text;
        positive ur, tr;
        b32 offered;
        if (!files_uri_pack(list, list_used, parts, &uris, &ur, &text, &tr))
                return false;
        offered = ui_drag_offer(&files.own, &files.drag, parts, 2,
                                WINDOWS_DRAG_COPY | WINDOWS_DRAG_MOVE | kind << WINDOWS_DRAG_KIND_SHIFT | (items > 0xffff ? 0xffffu : items) << WINDOWS_DRAG_ITEMS_SHIFT);
        return files_uris_free(uris, ur, text, tr, parts), offered;
}

static fn files_carry_end(void)
{
        array_store_release(files.carry_paths, files.carry_room, files.carry_used);
        files.carry_paths = null, files.carry_room = files.carry_used = 0, files.carry_count = 0;
        files.carrying = files.cut_dim = false, files.dirty = true;
}

static b32 files_carry_take_mode(b32 use_chosen)
{
        const struct files_model address_to m = &files.big->model;
        files_carry_end();
        files.carry_count = files_paths_collect(m->cursor, use_chosen, &files.carry_paths, &files.carry_room, &files.carry_used, &files.carry_kind);
        return string_copy_bounded((string_address)files.carry_from, (string_address)m->path, FILES_PATH), files.carry_count > 0;
}

static b32 files_carry_take(void)
{
        const struct files_model address_to m = &files.big->model;
        return files_carry_take_mode(m->chosen_count > 0);
}

static fn files_carry_drop(int row)
{
        if (!files.carrying)
                return;
        if (row < 0 && string_equals((string_address)files.carry_from, (string_address)files.big->model.path))
                files_carry_end(), files.said = LANGUAGE_files_carry_here;
        else
                (void)files_transfer(files.carry_paths, files.carry_count, row, FILES_MOVE_MOVE), files_carry_end();
}

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
        d = width + ui_dev(ui, 10) > d ? width + ui_dev(ui, 10) : d, cx = x + size - d / 3, cy = y - ui_dev(ui, 4);
        ui_kit_fill(ui, &(struct ui_box){(b32)cx << 8, (b32)cy << 8, (b32)(cx + d) << 8, (b32)(cy + ui_dev(ui, 20)) << 8, (b32)ui_dev(ui, 10) << 8, (b32)ui_dev(ui, 10) << 8},
                    ui_role(UI_PRIMARY) | 0xff000000u, null);
        ui_kit_text(ui, cx + (d - width) / 2, ui_kit_baseline(ui, cy + ui_dev(ui, 2), UI_LABEL_MEDIUM), (string_address)count, UI_LABEL_MEDIUM, ui_role(UI_ON_PRIMARY) | 0xff000000u, 0);
}

static fn files_rows_copy(b32 cut)
{
        const struct files_model address_to m = &files.big->model;
        struct windows_clip_part parts[3];
        p8 address_to list = null, address_to uris, address_to paths;
        positive lr = 0, lu = 0, ur, pr;
        b32 ok;
        if (!files_paths_collect(m->cursor, files_model_chosen(m, m->cursor), &list, &lr, &lu, null))
                return;
        ok = files_uri_pack(list, lu, parts, &uris, &ur, &paths, &pr);
        array_store_release(list, lr, lu);
        if (!ok)
                return;
        parts[2] = (struct windows_clip_part){FILES_CLIP_CUT, (const p8 address_to) "cut", 3};
        windows_clip_offer_parts(&files.own, WINDOWS_CLIP_CLIPBOARD, parts, cut ? 3 : 2);
        files_uris_free(uris, ur, paths, pr, parts);
        if (files_carry_take_mode(files_model_chosen(m, m->cursor)) && cut)
                files.cut_dim = true;
        else
                files_carry_end();
}

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
                memory_copy(paths + used, path, got + 1), used += got + 1, count++;
        }
        windows_clip_free(bytes, length);
        ok = count && files_transfer(paths, count, row, how);
        array_store_release(paths, room, used);
        if (out_count)
                *out_count = count;
        return ok;
}

static b32 files_clip_event(const struct window_event address_to event)
{
        p8 address_to bytes, address_to cut;
        unsigned long length, cut_len;
        p32 count = 0;
        b32 asked = files.pasting && (event->b == WINDOWS_CLIP_READY || event->b == WINDOWS_CLIP_REFUSED), moves;
        if (event->b == WINDOWS_CLIP_TAKEN || event->b == WINDOWS_CLIP_DECLINED)
                windows_clip_done((unsigned int)event->a);
        if (!asked)
                return true;
        files.pasting = false, files.dirty = true;
        if (event->b != WINDOWS_CLIP_READY || windows_clip_read_type(&files.own, (unsigned int)event->a, WINDOWS_CLIP_URIS, &bytes, &length) <= 0)
                return files.said = LANGUAGE_files_nothing_to_paste, true;
        moves = windows_clip_read_type(&files.own, (unsigned int)event->a, FILES_CLIP_CUT, &cut, &cut_len) > 0;
        if (moves)
                windows_clip_free(cut, cut_len);
        if (!files_uri_transfer(bytes, length, -1, moves ? FILES_MOVE_MOVE : FILES_MOVE_COPY, &count) && !count)
                files.said = LANGUAGE_files_nothing_to_paste;
        return true;
}

static int files_drop_row(int x, int y)
{
        struct files_model address_to m = &files.big->model;
        int row = ui_list_hit(&files.ui, &files.big->list, x, y);
        const struct files_entry address_to e = row >= 0 ? files_model_entry(m, (p32)row) : null;
        b32 dragged = files.drag.live && row >= 0 && (files.drag_chosen ? files_model_chosen(m, (p32)row) : (p32)row == files.drag_place);
        return e && e->kind == FILES_KIND_DIRECTORY && !dragged ? row : -1;
}

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

static bipolar files_drop_folder(int row)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = row >= 0 ? files_model_entry(m, (p32)row) : null;
        file_facts facts;
        if (m->handle < 0)
                return -ERROR_NO_ENTRY;
        if (row < 0)
                return files_open_dir(m->handle, (const p8 address_to) ".", 0);
        if (!e || e->kind != FILES_KIND_DIRECTORY || files_model_check(m, (p32)row, &facts) != FILES_SAME)
                return -ERROR_NO_ENTRY;
        return file_open_same(m->handle, (string_address)files_model_name(m, e), &facts, FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static b32 files_transfer_start(const p8 address_to paths, p32 count, bipolar into, const p8 address_to dest, int how, b32 reload_on_fail)
{
        struct files_move address_to job = &files.big->move;
        files_clear_msg(), files.dirty = true;
        if (files_move_running(job) || files.big->job.state == FILES_JOB_RUNNING)
                return files_close(&into), files.said = LANGUAGE_files_busy, false;
        files_move_end(job);
        if (into < 0)
        {
                files.said = LANGUAGE_files_changed;
                if (reload_on_fail)
                        files_model_reload(&files.big->model);
                return false;
        }
        if (!files_move_begin(job, how, into, paths, count))
                return files_move_end(job), false;
        string_copy_bounded((string_address)files.move_into, (string_address)dest, FILES_PATH), files_move_start(job);
        return files.moving = true, files.next_progress = (b64)ui_kit.now, true;
}

static b32 files_transfer(const p8 address_to paths, p32 count, int row, int how)
{
        const struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = row >= 0 ? files_model_entry(m, (p32)row) : null;
        p8 dest[FILES_PATH];
        if (row <= -2 && files.drop_node >= 0)
        {
                const struct files_tree address_to t = &files.big->tree;
                if ((p32)files.drop_node >= t->count || t->node[files.drop_node].kind == FILES_NODE_MACHINE || t->node[files.drop_node].machine != 0)
                        return false;
                return files_tree_path(t, (p32)files.drop_node, dest) &&
                       files_transfer_start(paths, count, files_open_dir(AT_FDCWD, dest, 0), dest, how, false);
        }
        if (e)
                files_path_join(dest, FILES_PATH, m->path, files_model_name(m, e));
        else
                string_copy_bounded((string_address)dest, (string_address)m->path, FILES_PATH);
        return files_transfer_start(paths, count, files_drop_folder(row), dest, how, true);
}

static fn files_move_said(void)
{
        const struct files_move address_to job = &files.big->move;
        char at[FILES_NAME * 3 + 4];
        int done = (int)job->items_done;
        files.said = 0, ui_label_foreign(at, 96, job->at, string_length((string_address)job->at));
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
                if (files.drag.finished && files.drag.action == WINDOWS_DRAG_MOVE)
                        files_model_reload(&files.big->model);
                return files.dirty = true, true;
        }
        if ((what = ui_drop_event(&files.drop, event)) == UI_DROP_OVER)
        {
                struct ui_list address_to tl = &files.big->tree_list;
                int hit_node = ui_list_hit(&files.ui, tl, files.drop.x, files.drop.y);
                int node = hit_node >= 0 && (p32)hit_node < files.big->tree.count &&
                           files.big->tree.node[hit_node].kind != FILES_NODE_MACHINE && files.big->tree.node[hit_node].machine == 0
                               ? hit_node : -1;
                int row = node >= 0 ? -2 : hit_node >= 0 ? -3 : files_drop_row(files.drop.x, files.drop.y);
                int spring = node >= 0 ? FILES_SPRING_NODE + node : row;
                b32 none = hit_node >= 0 && node < 0 ? true : (files.drag.live && row == -1) || (node < 0 && (files.big->model.state != FILES_READY || files_model_is_remote(&files.big->model)));
                p32 pref = files.drop.keys & WINDOW_KEY_CONTROL ? WINDOWS_DRAG_COPY
                           : files.drop.keys & WINDOW_KEY_SHIFT ? WINDOWS_DRAG_MOVE : files.drag.live && files_drop_near() ? WINDOWS_DRAG_MOVE : WINDOWS_DRAG_COPY;
                files.drop_over = true, tl->drop = node >= 0 ? node + 1 : 0;
                if (spring != files.spring)
                        files.spring = spring, files.spring_at = (b64)ui_kit_time();
                if (node != files.drop_node || files.drop_row != row || files.drop.action != pref)
                        files.drop.nth = -2;
                files.drop_node = node, files.drop_row = row, files.drop.action = pref;
                ui_drop_pick(&files.own, &files.drop, names, none ? 0 : 1, WINDOWS_DRAG_COPY | WINDOWS_DRAG_MOVE, pref);
                l->drop = files.drop.nth < 0 || node >= 0 ? 0 : row >= 0 ? row + 1 : UI_LIST_DROP_ALL;
        }
        else if (what == UI_DROP_LEFT || what == UI_DROP_DONE || what == UI_DROP_DROPPED)
                l->drop = files.big->tree_list.drop = 0, files.drop_over = false, files.spring = -1,
                files.drop_node = what == UI_DROP_DROPPED ? files.drop_node : -1;
        else if (what == UI_DROP_READY)
        {
                p8 address_to bytes;
                unsigned long length;
                b32 took = ui_drop_read(&files.own, &files.drop, &bytes, &length) &&
                           files_uri_transfer(bytes, length, files.drop_row, files.drop.action == WINDOWS_DRAG_MOVE ? FILES_MOVE_MOVE : FILES_MOVE_COPY, null);
                ui_drop_done(&files.own, &files.drop, took), files.drop_node = -1;
        }
        return files.dirty = true, true;
}

/* ---- navigation, the trail, completion, band ---- */

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

static b32 files_try_go(const p8 address_to path)
{
        int came = files_model_try(&files.big->model, (string_address)path);
        return came == FILES_READING ? true : (files.said = files_state_said(came), files.dirty = true, false);
}

static fn files_edit_go(void)
{
        static p8 typed[FILES_NAME], path[FILES_PATH], before[FILES_PATH];
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, files.naming);
        int did;
        (void)files_field_read(files_field(), typed, sizeof(typed));
        before[0] = 0;
        if (e)
                files_path_join(before, FILES_PATH, m->path, files_model_name(m, e)), files_path_join(path, FILES_PATH, m->path, typed);
        if ((did = files_model_rename(m, files.naming, typed)) != FILES_DID)
        {
                files.said = files_did_said(did), files.dirty = true;
                return;
        }
        if (before[0])
        {
                struct files_deed address_to d = files_deed_new(FILES_DEED_RENAME);
                files_deed_add(d, before, path), files_deed_keep(d);
        }
        files_edit(0), files_model_reload(m), string_copy_bounded(m->came, typed, FILES_NAME);
}

// Tab in the path band: the name after the last stroke finished from the folders listed, when what is before it is the folder shown.
static fn files_complete(void)
{
        static p8 typed[FILES_PATH], aux[FILES_PATH], name[FILES_NAME];
        struct ui_text address_to t = &files.big->path.text;
        const struct files_model address_to m = &files.big->model;
        positive length, cut, folder;
        p32 found;
        (void)files_field_read(t, typed, sizeof(typed)), length = string_length(typed);
        for (cut = length; cut > 0 && typed[cut - 1] != '/'; cut--)
                ;
        folder = cut > 1 ? cut - 1 : cut;
        if (!cut || folder != string_length(m->path) || memory_compare((address_any)typed, (address_any)m->path, folder) || length - cut >= FILES_NAME)
                return;
        if (!(found = files_model_complete(m, typed + cut, length - cut, name, sizeof(name))) || cut + string_length(name) + 2 >= sizeof(aux))
                return;
        memory_copy(aux, typed, cut), memory_copy(aux + cut, name, string_length(name) + 1);
        if (found == 1)
                string_append_bounded(aux, (string_address) "/", sizeof(aux));
        ui_text_set(t, aux, string_length(aux)), files.dirty = true;
}

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
        if (files_model_is_remote(m))
        {
                if (files_model_go_remote(m, m->peer, (string_address)(which == 0 ? (const p8 address_to) "/" : to), true))
                        files_moved(), files_remote_nav();
                return;
        }
        if (which > trail->current)
        {
                if ((seen = files_trail_open(trail, which, &handle, &error)) != FILES_SAME)
                {
                        files_lost_say(which, seen == FILES_MISSING ? LANGUAGE_files_gone : seen == FILES_REFUSED ? files_state_said(files_state_of(error)) : LANGUAGE_files_not_folder);
                        files_trail_lose(trail, which), files.dirty = true;
                        return;
                }
                files_model_take(m, to, handle), files_moved();
                return;
        }
        if (files_try_go(to))
        {
                static p8 through[FILES_NAME];
                positive len = (positive)(trail->to[which + 1] - trail->from[which + 1]);
                memory_copy(through, trail->path + trail->from[which + 1], len < FILES_NAME ? len : 0);
                through[len < FILES_NAME ? len : 0] = end;
                files_model_came(m, through), files_moved();
        }
}

static fn files_moved(void)
{
        struct files_model address_to m = &files.big->model;
        if (files.editing == FILES_EDIT_PATH)
                ui_tokens_focus(&files.big->path, false, false);
        files.editing = 0, files.armed = false, files_clear_msg();
        files_trail_note(&files.trail, m->path, m->handle >= 0 ? &m->folder : null);
        files_keep_trim(m, &files.trail), files_tree_follow();
        files.big->list.last_clicked = 0, files.reseat = true, files.next_look = (b64)ui_kit.now + (m->restored ? 0 : FILES_LOOK_NS), files.dirty = true;
}

// A chip of the path band pressed, or picked from its … menu: the folder its prefix names. A folder of the trail is gone to as the trail
// goes (a deeper one only while it is the folder it was); any other, as a typed path is.
static fn files_path_pressed(void)
{
        static p8 to[FILES_PATH];
        struct ui_tokens address_to k = &files.big->path;
        struct files_model address_to m = &files.big->model;
        positive length = k->prefix_length < FILES_PATH ? k->prefix_length : FILES_PATH - 1;
        int crumb;
        memory_copy(to, k->prefix, length), to[length] = end;
        crumb = files_trail_index(&files.trail, to, length);
        if (files.editing == FILES_EDIT_PATH)
                files_edit(0);
        if (crumb >= 0)
                files_crumb_go(crumb);
        else if (files_model_is_remote(m) ? files_model_go_remote(m, m->peer, (string_address)to, true) : files_try_go(to))
        {
                files_moved();
                if (files_model_is_remote(m))
                        files_remote_nav();
        }
        files.dirty = true;
}

// Enter in the path band: the path typed is gone to (on the peer, when the folder shown is a peer's), or the band keeps the keys and
// its text and draws its error outline, the reason said.
static fn files_path_submit(void)
{
        static p8 typed[FILES_PATH];
        struct ui_tokens address_to k = &files.big->path;
        struct files_model address_to m = &files.big->model;
        int came;
        (void)files_field_read(&k->text, typed, sizeof(typed));
        if (files_model_is_remote(m))
        {
                if (files_model_go_remote(m, m->peer, (string_address)typed, true))
                        files_moved(), files_remote_nav();
                else
                        k->invalid = true, files.said = LANGUAGE_files_not_folder;
        }
        else if ((came = files_model_try(m, (string_address)typed)) == FILES_READING)
                files_moved();
        else
                k->invalid = true, files.said = files_state_said(came);
        files.dirty = true;
}

static inline b32 files_path_chip(int x, int y)
{
        const struct ui_tokens address_to k = &files.big->path;
        return files.editing != FILES_EDIT_NAME && (ui_tokens_chip_at(k, x, y) >= 0 || (k->overflow > 0 && x >= k->overflow_left && x < k->overflow_right && y >= k->top && y < k->bottom));
}

// The path band's events. A press on a chip goes to its folder; a press on its text (or past its chips) gives it the keys, the folder shown
// its text. With the keys the events are the kit's token field's: Enter goes, Escape (or a press outside it) puts the trail back, Tab
// finishes a name, F6 goes on to the sidebar; Control+L gives it the keys with all of it selected.
static b32 files_path_event(const struct window_event address_to event, b32 down)
{
        struct ui address_to ui = &files.ui;
        struct ui_tokens address_to k = &files.big->path;
        int x = event->type == WINDOW_EVENT_MOTION ? (int)event->a : ui_kit.pointer_x, y = event->type == WINDOW_EVENT_MOTION ? (int)event->b : ui_kit.pointer_y;
        b32 editing = files.editing == FILES_EDIT_PATH, press = event->type == WINDOW_EVENT_BUTTON && down;
        b32 inside = files.editing != FILES_EDIT_NAME && files_in_rect(x, y, files.path_box.x, files.path_box.y, files.path_box.w, files.path_box.h);
        p32 asks;
        if (event->type == WINDOW_EVENT_KEY)
        {
                p32 mods = event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SHIFT);
                if (!editing)
                        return down && mods == WINDOW_KEY_CONTROL && event->a == FILES_KEY_L ? (files_edit(FILES_EDIT_PATH), true) : false;
                if (down && !mods && event->a == UI_KEY_TAB)
                        return files_complete(), true;
                if (down && !mods && event->a == FILES_KEY_F6)
                        return files_edit(0), files.focus = FILES_FOCUS_TREE, files.tree_at = files.tree_shown != FILES_NO ? files.tree_shown : 0, true;
        }
        else if (press && editing && !inside)
                return files_edit(0), false;
        if (!editing && !(press && inside && event->a == UI_POINTER_LEFT))
                return false;
        if (!editing && !files_path_chip(x, y))
        {
                files_edit(FILES_EDIT_PATH);
                doc_select_set(&k->text.doc, doc_length(&k->text.doc), doc_length(&k->text.doc));
                if (!ui_text_reach(ui, &k->text, x, y))
                        return true;
        }
        asks = ui_tokens_event(ui, k, &files.menu, event, x, y);
        if (asks & UI_TOKENS_PRESSED)
                return files_path_pressed(), true;
        if (asks & UI_TOKENS_SUBMIT)
                return files_path_submit(), true;
        if (asks & UI_TOKENS_CANCEL)
                return files_edit(0), true;
        if (asks & UI_TOKENS_CHANGED)
                files.said = 0;
        files.dirty |= asks != 0;
        return asks != 0 || event->type == WINDOW_EVENT_KEY;
}

static fn files_open(p32 place)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, place);
        int came;
        if (!e)
                return;
        if (e->kind == FILES_KIND_FILE)
        {
                m->cursor = place, files_open_with(true);
                return;
        }
        if (files_model_is_remote(m))
        {
                p8 into[FILES_PATH];
                if (e->kind == FILES_KIND_DIRECTORY && files_path_join(into, FILES_PATH, m->path, files_model_name(m, e)) &&
                    files_model_go_remote(m, m->peer, (string_address)into, true))
                        files_moved(), files_remote_nav();
                else if (e->kind == FILES_KIND_LINK)
                        files.said = LANGUAGE_files_link, files.dirty = true;
                return;
        }
        if ((came = files_model_enter(m, place)) == FILES_SAME)
                files_moved();
        else if (came == FILES_REFUSED)
                files.said = e->kind == FILES_KIND_LINK ? LANGUAGE_files_link : 0, files.dirty = true;
        else
                files.said = LANGUAGE_files_changed, files_reload();
}

static b32 files_act(int which)
{
        struct files_model address_to m = &files.big->model;
        if (which == 2 ? files_model_up(m) : files_model_back(m, which == 1))
        {
                files_moved();
                if (files_model_is_remote(m))
                        files_remote_nav();
        }
        return true;
}

static fn files_band_apply(void)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        int first = -1, last = -1;
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
                                if (lead < 0 || down_band)
                                        lead = k;
                        }
                if (lead >= 0)
                        m->cursor = (p32)lead;
        }
        m->revision++;
}

static fn files_drag_start(p32 place)
{
        struct files_model address_to m = &files.big->model;
        p8 address_to list = null;
        positive lr = 0, lu = 0;
        files.drag_place = place, files.drag_chosen = files_model_chosen(m, place);
        if (files_paths_collect(place, files.drag_chosen, &list, &lr, &lu, null))
        {
                const struct files_entry address_to first = files_model_entry(m, place);
                (void)files_drag_offer_list(list, lu, files.drag_chosen && m->chosen_count ? m->chosen_count : 1,
                                            first ? (files_symbol(m, first) & 0xffu) + 1 : 0);
        }
        array_store_release(list, lr, lu);
}

/* ---- menus, properties sheet, external launchers ---- */

static fn files_sheet_escape(char address_to into, positive room, const p8 address_to raw, positive length)
{
        char words[FILES_NAME * 3 + 4];
        ui_label_foreign(words, sizeof(words), raw, length), ui_menu_escape(into, room, words);
}

static inline fn files_popup_open(int x, int y) { files.menu.field = null, files.menu_view = false, ui_popmenu_open(&files.menu, &files.own, &files.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false); }

static fn files_cursor_pos(int address_to x, int address_to y, b32 mid)
{
        const struct files_model address_to m = &files.big->model;
        const struct ui_list address_to l = &files.big->list;
        int pitch = ui_list_pitch(&files.ui, l);
        *x = l->x + ui_dev(&files.ui, 40), *y = l->y + (mid ? pitch / 2 : 0) + (m->cursor != FILES_NO ? (int)m->cursor * pitch - l->scroll.scroll : 0);
}

static fn files_sheet_show(void)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, m->cursor);
        char (*sheet)[FILES_NAME * 3 + 64] = files.big->sheet;
        positive room = sizeof(files.big->sheet[0]);
        b32 many = !files.sheet_folder && m->chosen_count > 1;
        char escaped[FILES_NAME * 3 + 4], size[16], bytes[24], when[24], mode[12], octal[8];
        file_facts facts;
        int lines = 0;
        if (files.sheet_folder ? m->handle < 0 || files_look_fd(m->handle, &facts) < 0 : !e || files_model_check(m, m->cursor, &facts) != FILES_SAME)
                return;
        if (many)
                language_format(sheet[lines++], room, WORDS(files_sheet_items), (int)m->chosen_count);
        else
        {
                const p8 address_to name = files.sheet_folder ? m->path : files_model_name(m, e);
                p32 fmt = facts.mode & MODE_FORMAT;
                files_sheet_escape(sheet[lines++], room, name, string_length((string_address)name));
                if (fmt == MODE_LINK)
                {
                        p8 points[FILES_PATH];
                        bipolar len = (bipolar)system_call_4(syscall(readlinkat), (positive)m->handle, (positive)name, (positive)points, sizeof(points) - 1);
                        escaped[0] = 0;
                        if (len >= 0)
                                files_sheet_escape(escaped, sizeof(escaped), points, (positive)len);
                        language_format(sheet[lines++], room, WORDS(files_sheet_link), escaped);
                }
                else
                        string_copy_bounded((string_address)sheet[lines++], (string_address)(fmt == MODE_DIRECTORY ? WORDS(files_sheet_folder) : fmt == MODE_FILE ? WORDS(files_sheet_file) : WORDS(files_sheet_other)), room);
        }
        if (!many && (facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
                ui_size_text(size, sizeof(size), facts.size), bytes[positive_into((p8 address_to)bytes, (positive)facts.size)] = 0,
                    language_format(sheet[lines++], room, WORDS(files_sheet_size), size, bytes);
        else if (files.measuring)
                string_copy_bounded((string_address)sheet[lines++], (string_address)WORDS(files_sheet_measuring), room);
        else
        {
                p32 items = files.big->move.items_total;
                ui_size_text(size, sizeof(size), files.big->move.bytes_total);
                language_format(sheet[lines++], room, WORDS(files_sheet_size_in), size, (int)(items - (!many && items ? 1 : 0)));
        }
        if (!files.sheet_folder)
                files_sheet_escape(escaped, sizeof(escaped), m->path, string_length((string_address)m->path)),
                    language_format(sheet[lines++], room, WORDS(files_sheet_in), escaped);
        if (!many)
        {
                files_format_time(when, facts.modified.seconds, ' ', true), language_format(sheet[lines++], room, WORDS(files_sheet_modified), when);
                if (facts.created.seconds)
                        files_format_time(when, facts.created.seconds, ' ', true), language_format(sheet[lines++], room, WORDS(files_sheet_created), when);
                for (int bit = 0; bit < 9; bit++)
                        mode[bit] = facts.mode & (0400 >> bit) ? "rwxrwxrwx"[bit] : '-';
                for (int k = 0; k < 3; k++)
                        if (facts.mode & (04000 >> k))
                                mode[2 + 3 * k] = (char)((k == 2 ? 't' : 's') - (mode[2 + 3 * k] != 'x' ? 32 : 0));
                mode[9] = 0;
                for (int d = 0; d < 4; d++)
                        octal[d] = (char)('0' + (facts.mode >> (9 - 3 * d) & 7));
                octal[4] = 0;
                language_format(sheet[lines++], room, WORDS(files_sheet_mode), mode, octal);
                language_format(sheet[lines++], room, WORDS(files_sheet_owner), (int)facts.owner, (int)facts.group);
        }
        files.menu.menu.count = 0;
        for (int at = 0; at < lines; at++)
                ui_popmenu_add(&files.menu.menu, sheet[at], null, at ? 0 : UI_MENU_HEADER, FILES_MENU_SHEET);
        files_popup_open(files.sheet_x, files.sheet_y);
}

static fn files_sheet_open(b32 folder, int x, int y)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, m->cursor);
        b32 measure = folder || m->chosen_count > 1 || (e && e->kind == FILES_KIND_DIRECTORY);
        if (!folder && !e)
                folder = measure = true;
        if (x < 0)
                files_cursor_pos(&x, &y, false);
        files.sheet = true, files.sheet_folder = folder, files.sheet_x = x, files.sheet_y = y, files.measuring = false;
        if (measure && !files.moving)
        {
                p8 address_to list = null;
                positive used = 0, room = 0;
                p32 count = folder ? 1 : files_paths_collect(m->cursor, m->chosen_count > 1, &list, &room, &used, null);
                bipolar into = m->handle >= 0 ? files_open_dir(m->handle, (const p8 address_to) ".", 0) : -1;
                files_move_end(&files.big->move);
                if (into >= 0 && count && files_move_begin(&files.big->move, FILES_MOVE_MEASURE, into, folder ? m->path : list, count))
                        files_move_start(&files.big->move), files.moving = files.measuring = true;
                else
                        files_move_end(&files.big->move);
                array_store_release(list, room, used);
        }
        files_sheet_show();
}

static b32 files_is_png(struct files_model address_to m, const struct files_entry address_to e)
{
        const char address_to name = (const char address_to)files_model_name(m, e);
        positive len = e ? e->length : 0;
        return len > 4 && name[len - 4] == '.' && (name[len - 3] | 32) == 'p' && (name[len - 2] | 32) == 'n' && (name[len - 1] | 32) == 'g';
}

static fn files_remote_open_file(struct files_peer address_to p, const p8 address_to remote_path, const p8 address_to name);

static fn files_open_with(b32 editor)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, m->cursor);
        static p8 path[FILES_PATH];
        if (files_model_is_remote(m))
        {
                struct files_peer address_to p = files_peer_by_name(m->peer);
                if (editor && p && e && e->kind != FILES_KIND_DIRECTORY &&
                    files_path_join(path, sizeof(path), m->path, files_model_name(m, e)))
                        files_remote_open_file(p, path, files_model_name(m, e));
                return;
        }
        if (editor)
        {
                string_address words[6] = {(string_address) "/editor", (string_address) "-f", files_app.font ? files_app.font : (string_address)UI_FONT_REGULAR, (string_address) "--", (string_address)path, null};
                if (e && e->kind != FILES_KIND_DIRECTORY && files_path_join(path, sizeof(path), m->path, files_model_name(m, e)))
                        (void)ui_start_apart(words, 5);
                return;
        }
        {
                string_address words[2] = {(string_address) "/terminal", null};
                bipolar folder = files_drop_folder(!files.sheet_folder && e && e->kind == FILES_KIND_DIRECTORY ? (int)m->cursor : -1);
                if (folder < 0)
                        files.said = LANGUAGE_files_changed;
                else
                        (void)ui_start_apart_at(folder, words, 1), system_close(folder);
        }
}

static fn files_wallpaper(int screen)
{
        struct files_model address_to m = &files.big->model;
        const struct files_entry address_to e = files_model_entry(m, m->cursor);
        static p8 path[FILES_PATH];
        string_address words[7] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "picture",
                                   (string_address)path, screen >= 0 && screen < FILES_WALL_SCREENS ? (string_address)files_wall_names[screen] : null, null};
        if (!files_model_is_remote(m) && e && e->kind != FILES_KIND_DIRECTORY && files_is_png(m, e) && files_path_join(path, sizeof(path), m->path, files_model_name(m, e)))
                (void)ui_start_apart(words, words[5] ? 6 : 5);
}

#define FILES_SEP {null, null, UI_MENU_SEPARATOR, 0, false}

// The words of the folder's acts (the model's files_folder_acts), and the keys that do them.
static const struct { p8 act; unsigned int words; const char address_to keys; } files_folder_words[] = {
    {FILES_MENU_DETAILS, LANGUAGE_menu_view_details, "Ctrl+1"}, {FILES_MENU_ICONS, LANGUAGE_menu_view_icons, "Ctrl+2"},
    {FILES_MENU_LIST, LANGUAGE_menu_view_list, "Ctrl+3"}, {FILES_MENU_HIDDEN, LANGUAGE_menu_show_hidden, "Ctrl+H"},
    {FILES_ACT_RELOAD, LANGUAGE_files_refresh, "F5"}, {FILES_MENU_UNDO, LANGUAGE_menu_undo, "Ctrl+Z"}, {FILES_MENU_REDO, LANGUAGE_menu_redo, "Ctrl+Y"},
    {FILES_MENU_PASTE, LANGUAGE_menu_paste, "Ctrl+V"}, {FILES_MENU_NEW_FOLDER, LANGUAGE_menu_new_folder, "Ctrl+Shift+N"},
    {FILES_MENU_ALL, LANGUAGE_menu_select_all, "Ctrl+A"}, {FILES_MENU_TERMINAL, LANGUAGE_menu_open_terminal, null},
    {FILES_MENU_BIN, LANGUAGE_menu_bin_open, null}, {FILES_MENU_PROPERTIES, LANGUAGE_menu_properties, null},
};

// The folder's acts as a menu, item for item of the model's one table: the menu of the folder's empty space and the View menu. A sort is its
// column's title, the order it goes in its keys; in the bin, its Open is Empty.
static fn files_folder_menu(struct ui_popmenu address_to menu)
{
        const struct files_model address_to m = &files.big->model;
        b32 any = files_model_count(m) != 0, bin = files_in_bin(), line = false;
        menu->count = 0;
        for (int at = 0; at < FILES_COUNT(files_folder_acts); at++)
        {
                int act = files_folder_acts[at], col = act - FILES_MENU_SORT;
                const char address_to label = null, address_to keys = null;
                p32 flags = 0;
                if (!act)
                {
                        line = menu->count > 0;
                        continue;
                }
                for (int w = 0; w < FILES_COUNT(files_folder_words); w++)
                        if (files_folder_words[w].act == act)
                                label = language_text(files_folder_words[w].words), keys = files_folder_words[w].keys;
                if (col >= 0 && col < 4)
                        label = files.columns.title[col], keys = col == files.columns.sorted ? (files.columns.descending ? "\xe2\x86\x93" : "\xe2\x86\x91") : null,
                        flags = UI_MENU_RADIO | (col == files.columns.sorted ? UI_MENU_ON : 0);
                if (act >= FILES_MENU_DETAILS && act <= FILES_MENU_LIST)
                        flags = UI_MENU_RADIO | (files.view == act - FILES_MENU_DETAILS ? UI_MENU_ON : 0);
                if (act == FILES_MENU_HIDDEN)
                        flags = UI_MENU_CHECK | (m->hidden ? UI_MENU_ON : 0);
                if ((act == FILES_MENU_UNDO && files.deed_done <= 0) || (act == FILES_MENU_REDO && files.deed_done >= files.deed_count) ||
                    (act == FILES_MENU_ALL && !any) || (act == FILES_MENU_BIN && bin && !any))
                        flags |= UI_MENU_DISABLED;
                if (act == FILES_MENU_BIN && bin)
                        label = WORDS(menu_bin_empty), act = FILES_MENU_BIN_EMPTY;
                if (line)
                        ui_popmenu_add(menu, null, null, UI_MENU_SEPARATOR, 0), line = false;
                ui_popmenu_add(menu, label, keys, flags, act);
        }
}

static fn files_menu_open(b32 on, int x, int y)
{
        struct files_model address_to m = &files.big->model;
        struct ui_popmenu address_to menu = &files.menu.menu;
        const struct files_entry address_to e = files_model_entry(m, m->cursor);
        b32 folder = e && e->kind == FILES_KIND_DIRECTORY, bin = files_in_bin();
        menu->count = 0, files.sheet = false;
        if (on && e)
        {
                b32 png = !folder && !files_model_is_remote(m) && files_is_png(m, e);
                const struct ui_menu_spec head[] = {
                    {WORDS(menu_open), "Enter", 0, FILES_MENU_OPEN, false},
                    {WORDS(menu_open_editor), null, 0, FILES_MENU_EDITOR, folder},
                    {WORDS(menu_set_wallpaper), null, 0, FILES_MENU_WALLPAPER, !png},
                };
                const struct ui_menu_spec tail[] = {
                    {WORDS(menu_open_terminal), null, 0, FILES_MENU_TERMINAL, !folder}, FILES_SEP,
                    {WORDS(menu_cut), "Ctrl+X", 0, FILES_MENU_CUT, false}, {WORDS(menu_copy), "Ctrl+C", 0, FILES_MENU_COPY, false},
                    {WORDS(menu_paste), "Ctrl+V", 0, FILES_MENU_PASTE, false}, FILES_SEP,
                    {WORDS(menu_rename), "F2", m->chosen_count > 1 ? UI_MENU_DISABLED : 0, FILES_MENU_RENAME, false},
                    {WORDS(menu_bin_restore), null, 0, FILES_MENU_RESTORE, !bin},
                    {WORDS(menu_delete), bin ? "Shift+Delete" : "Delete", 0, FILES_MENU_DELETE, false}, FILES_SEP,
                    {WORDS(menu_properties), "Alt+Enter", 0, FILES_MENU_PROPERTIES, false},
                };
                ui_popmenu_build(menu, head, FILES_COUNT(head));
                if (png)
                {
                        unsigned int screens = ui_kit_table ? __atomic_load_n(&ui_kit_table->outputs, __ATOMIC_ACQUIRE) : 0;
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
                files_folder_menu(menu);
                if (m->chosen_count || m->cursor != FILES_NO)
                        files_model_choose_none(m), m->cursor = FILES_NO, m->revision++;
        }
        files.sheet_folder = !on, files.sheet_x = x, files.sheet_y = y, files_popup_open(x, y), files.dirty = true;
}

// The View menu: the folder's acts (files_folder_menu), under the View button's bottom-left corner. What is chosen stays chosen, and what the
// menu does is the folder's, as the menu of its empty space does (its Properties are the folder's, its terminal opens in it).
static fn files_view_open(void)
{
        const struct ui_menubutton address_to v = &files.view_button;
        files.sheet = false, files_folder_menu(&files.menu.menu);
        files.sheet_folder = true, files.sheet_x = v->x, files.sheet_y = v->y + v->h;
        files_popup_open(files.sheet_x, files.sheet_y), files.menu_view = true, files.dirty = true;
}

static fn files_find(b32 on)
{
        if (on)
                files_field_init(&files.finder, FILES_NAME - 1), files.finding = true, files.focus = FILES_FOCUS_FIND,
                    doc_select_set(&files.finder.doc, 0, doc_length(&files.finder.doc));
        else
                files.finding = false, files.focus = FILES_FOCUS_ROWS,
                    ui_text_set(&files.finder, (const p8 address_to) "", 0), files_model_filter(&files.big->model, (const p8 address_to) "", 0);
        files.dirty = true;
}

static fn files_menu_do(int id)
{
        struct files_model address_to m = &files.big->model;
        if (id >= FILES_MENU_WALLPAPER_OF && id < FILES_MENU_WALLPAPER_OF + FILES_WALL_SCREENS)
                return files_wallpaper(id - FILES_MENU_WALLPAPER_OF);
        if (id >= FILES_MENU_DETAILS && id <= FILES_MENU_LIST)
                return files.view = id - FILES_MENU_DETAILS, files.big->list.scroll.scroll = 0, files.dirty = true, files_show_cursor();
        if (id >= FILES_MENU_SORT && id < FILES_MENU_SORT + 4)
        {
                int col = id - FILES_MENU_SORT;
                files.columns.descending = col == files.columns.sorted ? !files.columns.descending : false, files.columns.sorted = col;
                return files_model_sort(m, files_sort_cols[col], files.columns.descending), files_show_cursor();
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
        case FILES_MENU_DELETE: if (files_in_bin()) files_delete(); else files_bin_act(false); break;
        case FILES_MENU_UNDO: files_undo(false); break;
        case FILES_MENU_REDO: files_undo(true); break;
        case FILES_MENU_RESTORE: files_bin_act(true); break;
        case FILES_MENU_BIN: { p8 bin[FILES_PATH]; if (files_bin_root(bin, "/files") && files_bin_folders(bin) && files_model_go(m, (string_address)bin)) files_moved(); } break;
        case FILES_MENU_BIN_EMPTY: files_bin_empty(); break;
        case FILES_MENU_NEW_FOLDER: files_new_folder(); break;
        case FILES_MENU_PROPERTIES: files_sheet_open(files.sheet_folder, files.sheet_x, files.sheet_y); break;
        case FILES_MENU_HIDDEN: m->hidden = !m->hidden, files_reload(); break;
        case FILES_MENU_ALL: files_select_all(m); break;
        case FILES_ACT_BACK:
        case FILES_ACT_FWD:
        case FILES_ACT_UP: (void)files_act(id - FILES_ACT_BACK); break;
        case FILES_ACT_RELOAD: files.said = 0, files_reload(); break;
        case FILES_ACT_FIND: files_find(true); break;
        case FILES_ACT_CTX: { int x, y; files_cursor_pos(&x, &y, true), files_menu_open(m->cursor != FILES_NO, x, y); } break;
        case FILES_ACT_SHEET: files_sheet_open(false, -1, -1); break;
        }
}

/* ---- Waterlink remote peers & async FS RPC ---- */

static struct files_peer address_to files_peer_by_machine(p8 machine)
{
        for (p32 k = 0; k < files.big->peer_count; k++)
                if (files.big->peers[k].used && files.big->peers[k].machine == machine)
                        return &files.big->peers[k];
        return null;
}

static struct files_peer address_to files_peer_by_name(const p8 address_to name)
{
        if (!name || !name[0])
                return null;
        for (p32 k = 0; k < files.big->peer_count; k++)
                if (files.big->peers[k].used && string_equals((string_address)files.big->peers[k].name, (string_address)name))
                        return &files.big->peers[k];
        return null;
}

static p32 files_tree_machine_node(const struct files_tree address_to t, p8 machine)
{
        for (p32 k = 0; k < t->count; k++)
                if (t->node[k].kind == FILES_NODE_MACHINE && t->node[k].machine == machine)
                        return k;
        return FILES_NO;
}

// A peer that left the link's peer list: its tree node, requests, reads and slot go; a folder open on it goes home.
static fn files_peer_drop(p32 k)
{
        struct files_peer address_to p = &files.big->peers[k];
        struct files_model address_to m = &files.big->model;
        struct files_remote_read address_to rr = &files.big->remote_read;
        const char address_to home = getenv((string_address) "HOME");
        (void)files_tree_drop_machine(&files.big->tree, p->machine);
        for (int r = 0; r < FILES_RPCS_MOST; r++)
                if (files.big->rpcs[r].kind != FILES_RPC_NONE && files.big->rpcs[r].machine == p->machine)
                        files.big->rpcs[r].kind = FILES_RPC_NONE;
        if (rr->active && rr->machine == p->machine)
                files_close(&rr->fd), rr->active = false, files.big->note[0] = 0;
        if (p->fs.used)
                link_fs_close(&p->fs);
        if (files_model_is_remote(m) && string_equals((string_address)m->peer, (string_address)p->name))
                files_model_go(m, (string_address)(home && home[0] == '/' ? home : "/")), files_moved();
        memory_zero(p, sizeof(*p));
}

static fn files_peers_sync(void)
{
        link_peers lp;
        link_state st;
        b32 has_state;
        positive st_got = 0;
        struct files_tree address_to t = &files.big->tree;
        b32 kept[FILES_PEERS_MOST];
        memory_zero(kept, sizeof(kept));
        memory_zero(&lp, sizeof(lp));
        memory_zero(&st, sizeof(st));
        link_peers_load(&lp);
        has_state = link_read_private_records(LINK_STATE_PATH, (p8 address_to)&st, sizeof(st), sizeof(st), &st_got) == 0 && st_got == sizeof(st);
        for (positive i = 0; i < lp.count; i++)
        {
                const struct waterlink_peer address_to wp = &lp.peer[i];
                struct files_peer address_to p = null;
                b32 online = false;
                p8 flags;
                p32 node;
                if (wp->flags & WATERLINK_PEER_BLOCKED)
                        continue;
                for (p32 k = 0; k < files.big->peer_count; k++)
                        if (files.big->peers[k].used && !memory_compare(files.big->peers[k].key, wp->key, 32))
                        {
                                p = &files.big->peers[k];
                                break;
                        }
                if (has_state)
                {
                        for (p32 j = 0; j < st.open_count && j < LINK_SESSIONS && !online; j++)
                                if (!memory_compare(st.open[j].key, wp->key, 32))
                                        online = true;
                        for (p32 j = 0; j < st.seen_count && j < LINK_PEERS_MAX && !online; j++)
                                if (!memory_compare(st.seen[j].key, wp->key, 32) && st.seen[j].seen > 0)
                                        online = true;
                }
                if (!p)
                {
                        p32 free_at = 0;
                        while (free_at < FILES_PEERS_MOST && files.big->peers[free_at].used)
                                free_at++;
                        if (free_at >= FILES_PEERS_MOST)
                                continue;
                        p = &files.big->peers[free_at];
                        memory_zero(p, sizeof(*p));
                        p->used = true;
                        p->machine = (p8)(free_at + 1);
                        if (free_at >= files.big->peer_count)
                                files.big->peer_count = free_at + 1;
                }
                kept[p - files.big->peers] = true;
                if (p->fs.used && p->fs.state == LINK_FS_READY)
                        online = true;
                memory_copy(p->key, wp->key, 32);
                string_copy_bounded((string_address)p->name, (string_address)wp->name, WATERLINK_NAME_MAX);
                p->may = wp->may, p->online = online;
                flags = (online ? FILES_NODE_ONLINE : 0) | ((wp->may & WATERLINK_MAY_FILES) ? FILES_NODE_MAY_FILES : 0);
                node = files_tree_machine_node(t, p->machine);
                if (node == FILES_NO)
                        files_tree_add_machine(t, p->name, p->machine, flags, false);
                else
                        t->node[node].flags = (p8)((t->node[node].flags & FILES_NODE_LOADING) | flags);
        }
        for (p32 k = 0; k < files.big->peer_count; k++)
                if (files.big->peers[k].used && !kept[k])
                        files_peer_drop(k);
}

static fn files_rpc_forget_machine(p8 machine);

static b32 files_peer_ensure(struct files_peer address_to p)
{
        if (!p)
                return false;
        if (!(p->may & WATERLINK_MAY_FILES))
                return files.said = LANGUAGE_files_remote_denied, files.dirty = true, false;
        if (!p->fs.used || p->fs.state == LINK_FS_FAILED)
        {
                files_rpc_forget_machine(p->machine);
                if (p->fs.used)
                        link_fs_close(&p->fs);
                if (!link_fs_open(&p->fs, (string_address)p->name))
                        return files.said = LANGUAGE_files_remote_offline, files.dirty = true, false;
        }
        return true;
}

static b32 files_rpc_free(void)
{
        for (int k = 0; k < FILES_RPCS_MOST; k++)
                if (files.big->rpcs[k].kind == FILES_RPC_NONE)
                        return true;
        return false;
}

// Gives up one pending request: the node it was loading stops loading, and a navigation it was for fails.
static fn files_rpc_forget(struct files_rpc address_to r)
{
        struct files_tree address_to t = &files.big->tree;
        struct files_model address_to m = &files.big->model;
        p32 node = FILES_NO;
        if (r->kind == FILES_RPC_INFO)
                node = files_tree_machine_node(t, r->machine);
        else if (r->kind == FILES_RPC_TREE_LIST)
                node = files_tree_find_in(t, r->machine, r->path);
        if (node != FILES_NO && r->kind != FILES_RPC_NAV_LIST && r->kind != FILES_RPC_READ)
                t->node[node].flags &= (p8)~FILES_NODE_LOADING;
        if (r->kind == FILES_RPC_NAV_LIST && files_model_is_remote(m) && string_equals((string_address)r->path, (string_address)m->path))
                files_model_fail(m, -5, true), files.said = LANGUAGE_files_failed, files.dirty = true;
        r->kind = FILES_RPC_NONE;
}

static fn files_rpc_forget_machine(p8 machine)
{
        for (int r = 0; r < FILES_RPCS_MOST; r++)
                if (files.big->rpcs[r].kind != FILES_RPC_NONE && files.big->rpcs[r].machine == machine)
                        files_rpc_forget(&files.big->rpcs[r]);
}

static fn files_rpc_put(p8 machine, p32 req_id, p8 kind, const p8 address_to path)
{
        for (int k = 0; k < FILES_RPCS_MOST; k++)
                if (files.big->rpcs[k].kind == FILES_RPC_NONE)
                {
                        files.big->rpcs[k].req_id = req_id, files.big->rpcs[k].kind = kind, files.big->rpcs[k].machine = machine;
                        files.big->rpcs[k].sent = (p64)ui_kit.now;
                        string_copy_bounded((string_address)files.big->rpcs[k].path, (string_address)(path ? path : (const p8 address_to) ""), FILES_PATH);
                        return;
                }
}

static b32 files_rpc_take(p8 machine, p32 req_id, struct files_rpc address_to out)
{
        for (int k = 0; k < FILES_RPCS_MOST; k++)
                if (files.big->rpcs[k].kind != FILES_RPC_NONE && files.big->rpcs[k].machine == machine && files.big->rpcs[k].req_id == req_id)
                {
                        *out = files.big->rpcs[k], files.big->rpcs[k].kind = FILES_RPC_NONE;
                        return true;
                }
        return false;
}

static fn files_remote_request_info(struct files_peer address_to p)
{
        p32 id, node;
        if (!files_peer_ensure(p) || !files_rpc_free())
                return;
        if ((id = link_fs_request(&p->fs, FS_OP_INFO, null, 0)))
        {
                files_rpc_put(p->machine, id, FILES_RPC_INFO, null);
                node = files_tree_machine_node(&files.big->tree, p->machine);
                if (node != FILES_NO)
                        files.big->tree.node[node].flags |= FILES_NODE_LOADING;
        }
}

static b32 files_remote_request_list(struct files_peer address_to p, const p8 address_to path, b32 dirs_only, p8 rpc_kind)
{
        p8 req[FILES_PATH + 4];
        positive plen = path ? string_length((string_address)path) : 0;
        p32 id;
        if (!plen || plen >= FILES_PATH || !files_peer_ensure(p) || !files_rpc_free())
                return false;
        req[0] = (p8)((files.big->model.hidden && !dirs_only ? 1 : 0) | (dirs_only ? 2 : 0));
        link_fs_put16(req + 1, (p16)plen);
        memory_copy(req + 3, path, plen);
        if (!(id = link_fs_request(&p->fs, FS_OP_LIST, req, (p32)(3 + plen))))
                return false;
        files_rpc_put(p->machine, id, rpc_kind, path);
        return true;
}

static fn files_remote_nav(void)
{
        struct files_model address_to m = &files.big->model;
        struct files_peer address_to p = files_peer_by_name(m->peer);
        if (!p || !files_peer_ensure(p))
        {
                files_model_fail(m, -13, true), files.dirty = true;
                return;
        }
        files_remote_request_list(p, m->path, false, FILES_RPC_NAV_LIST);
}

// The folder a peer's files land in: ~/.cache/moonwater/files/PEER, made 0700, and refused unless it is a folder of ours that nobody else can write.
static bipolar files_remote_dir(p8 address_to into, const p8 address_to peer)
{
        string_address home = getenv((string_address) "HOME");
        bipolar folder;
        file_facts facts;
        if (!home || home[0] != '/' || !peer || !files_name_good(peer))
                return -1;
        string_copy_bounded((string_address)into, home, FILES_PATH);
        string_append_bounded((string_address)into, (string_address) "/.cache/moonwater/files/", FILES_PATH);
        string_append_bounded((string_address)into, (string_address)peer, FILES_PATH);
        if (!files_bin_folders(into) || (folder = files_open_dir(AT_FDCWD, into, O_NOFOLLOW)) < 0)
                return -1;
        if (files_look_fd(folder, &facts) < 0 || (facts.mode & MODE_FORMAT) != MODE_DIRECTORY || (facts.mode & 0022) ||
            facts.owner != (p32)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0))
        {
                system_close(folder);
                return -1;
        }
        return folder;
}

static fn files_remote_read_chunk(struct files_peer address_to p)
{
        struct files_remote_read address_to rr = &files.big->remote_read;
        p8 req[FILES_PATH + 16];
        positive plen = string_length((string_address)rr->remote_path);
        p32 id;
        if (!rr->active || !plen || plen >= FILES_PATH)
                return;
        link_fs_put64(req, rr->offset);
        link_fs_put32(req + 8, 65536u);
        link_fs_put16(req + 12, (p16)plen);
        memory_copy(req + 14, rr->remote_path, plen);
        if ((id = link_fs_request(&p->fs, FS_OP_READ, req, (p32)(14 + plen))))
                rr->req_id = id, files_rpc_put(p->machine, id, FILES_RPC_READ, rr->remote_path);
}

static fn files_remote_read_abort(void)
{
        struct files_remote_read address_to rr = &files.big->remote_read;
        if (rr->active)
        {
                files_close(&rr->fd);
                if (rr->local_path[0])
                        system_remove_at(AT_FDCWD, (string_address)rr->local_path, 0), rr->local_path[0] = 0;
                rr->active = false;
        }
}

static fn files_remote_open_file(struct files_peer address_to p, const p8 address_to remote_path, const p8 address_to name)
{
        struct files_remote_read address_to rr = &files.big->remote_read;
        p8 dir[FILES_PATH], safe[FILES_NAME];
        char shown[FILES_NAME * 3 + 4];
        positive nlen = string_length((string_address)name);
        bipolar folder;
        if (!files_peer_ensure(p) || !files_peer_name_ok(name, nlen))
                return;
        files_remote_read_abort();
        for (positive k = 0; k < nlen; k++)
                safe[k] = name[k] == '/' ? '_' : name[k];
        safe[nlen] = 0;
        if (!files_name_good(safe) || (folder = files_remote_dir(dir, p->name)) < 0)
                return;
        if (!files_path_join(rr->local_path, sizeof(rr->local_path), dir, safe))
        {
                system_close(folder);
                return;
        }
        system_remove_at(folder, (string_address)safe, 0);
        rr->fd = system_open_at_mode(folder, (string_address)safe, FILE_WRITE | FILE_CREATE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC, 0600);
        system_close(folder);
        if (rr->fd < 0)
                return;
        rr->active = true, rr->machine = p->machine, rr->offset = 0;
        string_copy_bounded((string_address)rr->remote_path, (string_address)remote_path, sizeof(rr->remote_path));
        string_copy_bounded((string_address)rr->name, (string_address)name, sizeof(rr->name));
        ui_label_foreign(shown, 96, name, nlen);
        language_format(files.big->note, sizeof(files.big->note), WORDS(files_remote_opening), shown);
        files.dirty = true;
        files_remote_read_chunk(p);
}

static fn files_remote_handle_reply(struct files_peer address_to p, const struct link_fs_reply address_to rep)
{
        struct files_rpc rpc;
        if (!files_rpc_take(p->machine, rep->req_id, &rpc))
                return;
        if (rpc.kind == FILES_RPC_INFO && rep->op == FS_OP_INFO && rep->length >= 11)
        {
                b32 status = (b32)link_fs_get32(rep->data);
                p32 node = files_tree_machine_node(&files.big->tree, p->machine);
                if (status == 0 && node != FILES_NO)
                {
                        p8 hflags = rep->data[4];
                        p16 hlen = link_fs_get16(rep->data + 5);
                        if (7u + hlen + 4u <= rep->length)
                        {
                                p8 home[FILES_PATH];
                                p32 mlen = link_fs_get32(rep->data + 7 + hlen);
                                positive copy = hlen < FILES_PATH - 1 ? hlen : FILES_PATH - 1;
                                memory_copy(home, rep->data + 7, copy), home[copy] = 0;
                                if (mlen <= rep->length - (11u + hlen))
                                        files_tree_set_machine_children(&files.big->tree, node, home, hflags, mlen ? rep->data + 11 + hlen : null, mlen);
                                p->info_loaded = true;
                        }
                }
                else if (node != FILES_NO)
                        files.big->tree.node[node].flags &= (p8)~FILES_NODE_LOADING;
                files.dirty = true;
        }
        else if (rpc.kind == FILES_RPC_TREE_LIST && rep->op == FS_OP_LIST && rep->length >= 4)
        {
                b32 status = (b32)link_fs_get32(rep->data);
                p32 node = files_tree_find_in(&files.big->tree, p->machine, rpc.path);
                if (status == 0 && node != FILES_NO && rep->length >= 8)
                {
                        struct files_tree_open_scratch address_to s = (struct files_tree_open_scratch address_to)files_anon_map(sizeof(*s), null);
                        if (s)
                        {
                                p32 count = link_fs_get32(rep->data + 4), found = 0;
                                positive at = 8, used = 0;
                                for (p32 k = 0; k < count && at + 37 <= rep->length && found < FILES_TREE_CHILDREN; k++)
                                {
                                        p8 kind = rep->data[at];
                                        p16 nlen = link_fs_get16(rep->data + at + 35);
                                        at += 37;
                                        if (at + nlen > rep->length)
                                                break;
                                        if (kind == FILES_KIND_DIRECTORY && files_peer_name_ok(rep->data + at, nlen) && used + nlen + 1 <= sizeof(s->found))
                                        {
                                                s->offsets[found++] = (p32)used;
                                                memory_copy(s->found + used, rep->data + at, nlen);
                                                s->found[used + nlen] = 0, used += nlen + 1;
                                        }
                                        at += nlen;
                                }
                                files_tree_splice_folders(&files.big->tree, node, s->found, s->offsets, found, used);
                                files_unmap(s, sizeof(*s));
                        }
                }
                else if (node != FILES_NO)
                        files.big->tree.node[node].read = true, files.big->tree.node[node].has = 0, files.big->tree.node[node].flags &= (p8)~FILES_NODE_LOADING;
                files.dirty = true;
        }
        else if (rpc.kind == FILES_RPC_NAV_LIST && rep->op == FS_OP_LIST)
        {
                struct files_model address_to m = &files.big->model;
                if (files_model_is_remote(m) && string_equals((string_address)m->peer, (string_address)p->name) &&
                    string_equals((string_address)m->path, (string_address)rpc.path))
                {
                        files_model_take_remote(m, rep->data, rep->length);
                        files_tree_follow();
                        files.dirty = true;
                }
        }
        else if (rpc.kind == FILES_RPC_READ && rep->op == FS_OP_READ)
        {
                struct files_remote_read address_to rr = &files.big->remote_read;
                if (!rr->active || rr->machine != p->machine || rr->req_id != rep->req_id)
                        return;
                if (rep->length < 17 || (b32)link_fs_get32(rep->data) < 0)
                {
                        files_remote_read_abort(), files.big->note[0] = 0, files.said = LANGUAGE_files_failed, files.dirty = true;
                        return;
                }
                {
                        p8 eof = rep->data[12];
                        p32 dlen = link_fs_get32(rep->data + 13);
                        if (dlen > rep->length - 17u || (!eof && !dlen) || rr->offset + (p64)dlen > (64u << 20))
                        {
                                files_remote_read_abort(), files.big->note[0] = 0, files.said = LANGUAGE_files_failed, files.dirty = true;
                                return;
                        }
                        if (dlen && (bipolar)system_write_all((positive)rr->fd, rep->data + 17, dlen) != (bipolar)dlen)
                        {
                                files_remote_read_abort(), files.big->note[0] = 0, files.said = LANGUAGE_files_failed, files.dirty = true;
                                return;
                        }
                        rr->offset += dlen;
                        if (eof)
                        {
                                string_address words[6] = {(string_address) "/editor", (string_address) "-f",
                                                           files_app.font ? files_app.font : (string_address)UI_FONT_REGULAR,
                                                           (string_address) "--", (string_address)rr->local_path, null};
                                files_close(&rr->fd), rr->active = false, files.big->note[0] = 0, files.dirty = true;
                                (void)ui_start_apart(words, 5);
                        }
                        else
                                files_remote_read_chunk(p);
                }
        }
}

static fn files_remote_tick(long address_to timeout, long address_to other)
{
        b32 busy = false;
        for (p32 k = 0; k < files.big->peer_count; k++)
        {
                struct files_peer address_to p = &files.big->peers[k];
                struct link_fs_reply rep;
                int st;
                if (!p->used || !p->fs.used)
                        continue;
                for (int n = 0; n < 8; n++)
                {
                        st = link_fs_step(&p->fs, &rep);
                        if (st > 0)
                                files_remote_handle_reply(p, &rep);
                        else
                                break;
                }
                if (st < 0)
                {
                        p32 node = files_tree_machine_node(&files.big->tree, p->machine);
                        b32 refused = p->fs.refusal[0] != 0;
                        if (!refused)
                                p->online = false;
                        if (node != FILES_NO)
                                files.big->tree.node[node].flags &= (p8)~(refused ? FILES_NODE_LOADING : (FILES_NODE_ONLINE | FILES_NODE_LOADING));
                        for (int r = 0; r < FILES_RPCS_MOST; r++)
                                if (files.big->rpcs[r].kind != FILES_RPC_NONE && files.big->rpcs[r].machine == p->machine)
                                {
                                        if (files.big->rpcs[r].kind == FILES_RPC_NAV_LIST &&
                                            files_model_is_remote(&files.big->model) &&
                                            string_equals((string_address)files.big->model.peer, (string_address)p->name))
                                                files_model_fail(&files.big->model, refused ? -13 : -5, true);
                                        files.big->rpcs[r].kind = FILES_RPC_NONE;
                                }
                        if (files.big->remote_read.active && files.big->remote_read.machine == p->machine)
                                files_remote_read_abort(), files.big->note[0] = 0;
                        if (refused)
                        {
                                string_copy_bounded((string_address)files.big->note, (string_address)p->fs.refusal, sizeof(files.big->note));
                                files.said = LANGUAGE_files_remote_denied;
                        }
                        else
                                files.said = LANGUAGE_files_remote_offline;
                        link_fs_close(&p->fs);
                        files.dirty = true;
                        continue;
                }
                if (p->fs.state == LINK_FS_READY && !p->online)
                {
                        p32 node = files_tree_machine_node(&files.big->tree, p->machine);
                        p->online = true;
                        if (node != FILES_NO)
                                files.big->tree.node[node].flags |= FILES_NODE_ONLINE;
                        files.dirty = true;
                }
                if (other && *other < 0)
                        *other = (long)link_fs_poll_fd(&p->fs);
                if (p->fs.state == LINK_FS_CONNECTING)
                        busy = true;
        }
        for (int r = 0; !busy && r < FILES_RPCS_MOST; r++)
                if (files.big->rpcs[r].kind != FILES_RPC_NONE)
                        busy = true;
        for (int r = 0; r < FILES_RPCS_MOST; r++)
                if (files.big->rpcs[r].kind != FILES_RPC_NONE && files_rpc_overdue(files.big->rpcs[r].sent, (p64)ui_kit.now))
                        files_rpc_forget(&files.big->rpcs[r]), files.dirty = true;
        if (busy)
                *timeout = ui_timeout_min(*timeout, 50000000);
}

static fn files_close_window(void)
{
        if (!files.big)
                return;
        files_remote_read_abort();
        for (p32 k = 0; k < files.big->peer_count; k++)
                if (files.big->peers[k].used && files.big->peers[k].fs.used)
                        link_fs_close(&files.big->peers[k].fs);
}

/* ---- tree, type-ahead, keyboard ---- */

static fn files_tree_toggle(p32 at);

static b32 files_tree_go(p32 at)
{
        static p8 path[FILES_PATH];
        const struct files_tree address_to t = &files.big->tree;
        if (at >= t->count)
                return false;
        if (t->node[at].kind == FILES_NODE_MACHINE || t->node[at].kind == FILES_NODE_DEVICES || t->node[at].kind == FILES_NODE_DISK)
                return files_tree_toggle(at), true;
        if (t->node[at].kind == FILES_NODE_PART)
        {
                // A partition that is mounted is gone to where it is mounted, as its drive is.
                const p8 address_to mount = files_part_mount(&files.big->devices, t->node[at].device);
                if (!mount || string_copy_bounded((string_address)path, (string_address)mount, sizeof(path)) >= sizeof(path) ||
                    (!files_model_is_remote(&files.big->model) && string_equals((string_address)path, (string_address)files.big->model.path)))
                        return false;
                if (files_try_go(path))
                        files_moved();
                return true;
        }
        if (!files_tree_path(t, at, path))
                return false;
        if (t->node[at].machine > 0)
        {
                struct files_peer address_to p = files_peer_by_machine(t->node[at].machine);
                if (!p)
                        return false;
                if (files_model_is_remote(&files.big->model) &&
                    string_equals((string_address)files.big->model.peer, (string_address)p->name) &&
                    string_equals((string_address)files.big->model.path, (string_address)path))
                        return false;
                if (files_model_go_remote(&files.big->model, p->name, (string_address)path, true))
                        files_moved(), files_remote_nav();
                return true;
        }
        if (!files_model_is_remote(&files.big->model) && string_equals((string_address)path, (string_address)files.big->model.path))
                return false;
        if (files_try_go(path) ||
            (files.said == LANGUAGE_files_gone && t->node[at].depth == 1 && t->node[at].kind == FILES_NODE_PLACE &&
             files_mkdir(AT_FDCWD, path, 0755) >= 0 && files_try_go(path)))
                files_moved();
        return true;
}

static fn files_tree_toggle(p32 at)
{
        struct files_tree address_to t = &files.big->tree;
        if (at >= t->count)
                return;
        if (t->node[at].kind == FILES_NODE_DEVICES || t->node[at].kind == FILES_NODE_DISK)
        {
                // What is shut is the table's, so that it stays shut when the devices are read again.
                struct files_devices address_to d = &files.big->devices;
                if (t->node[at].kind == FILES_NODE_DEVICES)
                        d->shut = !d->shut;
                else if (t->node[at].device < d->disks)
                        d->disk_shut[t->node[at].device] = !d->disk_shut[t->node[at].device];
                (void)files_tree_devices(t);
        }
        else if (t->node[at].kind == FILES_NODE_PART)
                return;
        else if (t->node[at].open)
                files_tree_shut(t, at);
        else if (t->node[at].kind == FILES_NODE_MACHINE)
        {
                if (t->node[at].machine == 0)
                        t->node[at].open = true, files_tree_sync_drives(t, (const p8 address_to)getenv((string_address) "HOME"), true);
                else
                {
                        struct files_peer address_to p = files_peer_by_machine(t->node[at].machine);
                        if (p)
                                files_remote_request_info(p);
                }
        }
        else if (t->node[at].machine > 0)
        {
                static p8 path[FILES_PATH];
                struct files_peer address_to p = files_peer_by_machine(t->node[at].machine);
                if (p && files_tree_path(t, at, path) && files_remote_request_list(p, path, true, FILES_RPC_TREE_LIST))
                        t->node[at].flags |= FILES_NODE_LOADING;
        }
        else
                files_tree_open(t, at);
        {
                struct files_peer address_to cur = files_model_is_remote(&files.big->model) ? files_peer_by_name(files.big->model.peer) : null;
                files.tree_shown = files_tree_find_in(t, cur ? cur->machine : 0, files.big->model.path), files.dirty = true;
        }
}

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
        case UI_KEY_SPACE: if (!(event->flags & WINDOW_KEY_REPEAT)) files_tree_go(at); break;
        case UI_KEY_TAB:
        case UI_KEY_ESCAPE: files.focus = FILES_FOCUS_ROWS; break;
        default: return false;
        }
        files.tree_at = at, files.big->tree_list.focus = (int)at, ui_list_reveal(&files.ui, &files.big->tree_list);
        return files.dirty = true, true;
}

static fn files_type_ahead(p32 code)
{
        struct files_model address_to m = &files.big->model;
        p32 count = files_model_count(m), start;
        b64 now = (b64)ui_kit.now;
        p8 bytes[4];
        positive len = memory_utf8_encode(bytes, sizeof(bytes), code);
        if (now - files.typed_at > FILES_TYPE_NS)
                files.typed_length = 0;
        files.typed_at = now;
        if (!len || files.typed_length + len > sizeof(files.typed))
                return;
        memory_copy(files.typed + files.typed_length, bytes, len), files.typed_length += len;
        start = m->cursor == FILES_NO ? 0 : files.typed_length == len ? m->cursor + 1 : m->cursor;
        for (p32 k = 0; k < count; k++)
        {
                p32 place = (start + k) % count;
                const struct files_entry address_to e = files_model_entry(m, place);
                if (e->length >= files.typed_length && !memory_compare_ascii_case(files_model_name(m, e), files.typed, files.typed_length))
                {
                        files_model_choose(m, place, FILES_CHOOSE_ONLY), files_show_cursor();
                        return;
                }
        }
}

static b32 files_key(const struct window_event address_to event)
{
        static const struct { p32 req; int key, act; } shortcuts[] = {
            {0, FILES_KEY_F2, FILES_MENU_RENAME}, {WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT, FILES_KEY_N, FILES_MENU_NEW_FOLDER},
            {WINDOW_KEY_CONTROL, FILES_KEY_H, FILES_MENU_HIDDEN}, {WINDOW_KEY_CONTROL, FILES_KEY_C, FILES_MENU_COPY},
            {WINDOW_KEY_CONTROL, FILES_KEY_X, FILES_MENU_CUT}, {WINDOW_KEY_CONTROL, FILES_KEY_V, FILES_MENU_PASTE},
            {WINDOW_KEY_CONTROL, FILES_KEY_1, FILES_MENU_DETAILS}, {WINDOW_KEY_CONTROL, FILES_KEY_1 + 1, FILES_MENU_ICONS},
            {WINDOW_KEY_CONTROL, FILES_KEY_1 + 2, FILES_MENU_LIST}, {WINDOW_KEY_CONTROL, FILES_KEY_A, FILES_MENU_ALL},
            {WINDOW_KEY_ALT, UI_KEY_LEFT, FILES_ACT_BACK}, {WINDOW_KEY_ALT, UI_KEY_RIGHT, FILES_ACT_FWD},
            {0, UI_KEY_BACKSPACE, FILES_ACT_UP}, {0, FILES_KEY_F5, FILES_ACT_RELOAD},
            {WINDOW_KEY_CONTROL, FILES_KEY_R, FILES_ACT_RELOAD}, {WINDOW_KEY_CONTROL, FILES_KEY_F, FILES_ACT_FIND},
        };
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        b32 alt = (event->flags & WINDOW_KEY_ALT) != 0, control = (event->flags & WINDOW_KEY_CONTROL) != 0, shift = (event->flags & WINDOW_KEY_SHIFT) != 0;
        int key = (int)event->a;
        b32 moved = false;
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
        if (files.carrying && key == UI_KEY_ESCAPE)
                return files_carry_end(), true;
        if (key == UI_KEY_TAB && !shift && !control && !alt && files.focus == FILES_FOCUS_ROWS)
        {
                if (files.carrying)
                        files_carry_drop(-1);
                else if (files_carry_take())
                        files.carrying = true, files.carry_at = (b64)ui_kit.now, files_clear_msg();
                return files.dirty = true, true;
        }
        if (alt && (key == UI_KEY_UP || key == UI_KEY_DOWN))
                return files_crumb_go(files.trail.current + (key == UI_KEY_UP ? -1 : 1)), true;
        if (files.focus == FILES_FOCUS_TREE && files_tree_key(event))
                return true;
        if (key == FILES_KEY_F6 && !shift && !control && !alt)
                return files_edit(FILES_EDIT_PATH), true;
        for (int k = 0; k < FILES_COUNT(shortcuts); k++)
                if (key == shortcuts[k].key && (event->flags & shortcuts[k].req) == shortcuts[k].req)
                        return files_menu_do(shortcuts[k].act), files.dirty = true, true;
        if (key == UI_KEY_ESCAPE && files.finding)
                return files_find(false), true;
        if (!control && !alt && event->b > ' ' && event->b != 0x7f && key != UI_KEY_SPACE)
                return files_type_ahead((p32)event->b), true;
        if ((shift && key == FILES_KEY_F10) || key == FILES_KEY_MENU)
                return files_menu_do(FILES_ACT_CTX), true;
        if (alt && (key == UI_KEY_ENTER || key == UI_KEY_ENTER_PAD))
                return files_menu_do(FILES_ACT_SHEET), true;
        switch (key)
        {
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD: if (!(event->flags & WINDOW_KEY_REPEAT)) files_open(m->cursor); return true;
        case UI_KEY_SPACE: return files_model_choose(m, m->cursor, FILES_CHOOSE_TOGGLE), files.dirty = true, true;
        }
        // The list's own keys: Up, Down, Left, Right, Page Up, Page Down, Home and End move the focus row.
        if (!ui_list_key(&files.ui, l, key, &moved))
                return true;
        files.said = 0;
        if (moved)
                m->cursor = l->focus < 0 ? FILES_NO : (p32)l->focus, m->revision++;
        if (m->cursor != FILES_NO)
        {
                if (!control)
                        files_model_choose(m, m->cursor, shift ? FILES_CHOOSE_RANGE : FILES_CHOOSE_ONLY);
                files_show_cursor();
        }
        return true;
}

/* ---- event dispatch ---- */

static b32 files_field_event(const struct window_event address_to event, b32 down)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        p32 asks;
        if (files.editing == FILES_EDIT_NAME)
        {
                asks = ui_field_send(ui, files_field(), &files.menu, event);
                if (asks & UI_TEXT_SUBMIT)
                        return files_edit_go(), true;
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_COMMIT))
                {
                        files_edit(0);
                        if (event->type == WINDOW_EVENT_KEY)
                                return true;
                        return files_layout(), false;
                }
                if (asks)
                        return files.said = 0, files.dirty = true, true;
                return event->type == WINDOW_EVENT_KEY;
        }
        if (files_path_event(event, down))
                return true;
        if (files.editing || !files.finding || (files.focus != FILES_FOCUS_FIND && event->type != WINDOW_EVENT_BUTTON))
                return false;
        if (event->type == WINDOW_EVENT_BUTTON && down &&
            files_in_rect(ui_kit.pointer_x, ui_kit.pointer_y, files.finder.well_left, files.finder.well_top,
                          files.finder.well_right - files.finder.well_left, files.finder.well_bottom - files.finder.well_top))
                files.focus = FILES_FOCUS_FIND;
        if (files.focus != FILES_FOCUS_FIND)
                return false;
        asks = ui_field_send(ui, &files.finder, &files.menu, event);
        if (asks & (UI_TEXT_SUBMIT | UI_TEXT_COMMIT))
                files.focus = FILES_FOCUS_ROWS;
        else if (asks & UI_TEXT_CANCEL)
                files_find(false);
        if (asks & ~(p32)(UI_TEXT_SUBMIT | UI_TEXT_CANCEL | UI_TEXT_COMMIT))
        {
                static p8 text[FILES_NAME];
                positive len = files_field_read(&files.finder, text, sizeof(text));
                files_model_filter(m, text, len < sizeof(text) ? len : sizeof(text) - 1);
                files.reseat = true, m->seat = 0, files.dirty = true;
        }
        return files.dirty |= asks != 0, asks || event->type == WINDOW_EVENT_KEY;
}

static fn files_button_event(const struct window_event address_to event, b32 down)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list, address_to hit_list = null;
        int row;
        files.dirty = true;
        if (down && event->a != UI_POINTER_RIGHT)
                files.armed = false, files.big->note[0] = 0;
        if (event->a == UI_POINTER_LEFT && down && files.carrying)
        {
                files.carry_press = true, files.carry_px = ui_kit.pointer_x, files.carry_py = ui_kit.pointer_y;
                return;
        }
        if (event->a == UI_POINTER_LEFT && !down && files.carry_press)
        {
                files.carry_press = false;
                if (ui_point(files.carry_px, l->x, l->w) >= l->x && files.carry_py >= l->y && files.carry_py < l->y + l->h)
                {
                        int at = ui_list_hit(ui, l, files.carry_px, files.carry_py);
                        const struct files_entry address_to e = at >= 0 ? files_model_entry(m, (p32)at) : null;
                        if (!e || e->kind == FILES_KIND_DIRECTORY)
                                files_carry_drop(e ? at : -1);
                }
                return;
        }
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
                                b32 moved = files_tree_go((p32)row);
                                files_tree_toggle((p32)row);
                                if (!moved && files.big->tree.node[row].read && !files.big->tree.node[row].has)
                                        files.focus = FILES_FOCUS_ROWS, files_model_reload(m);
                        }
                        else if (files_tree_go((p32)row))
                                files.dirty = true;
                }
                return;
        }
        if (hit_list != l)
                return;
        if (down && event->a == UI_POINTER_LEFT)
                files.focus = FILES_FOCUS_ROWS;
        if (l->band_began)
        {
                files.band_add = (event->flags & WINDOW_KEY_CONTROL) != 0;
                memory_copy(files.big->band_base, m->chosen, FILES_MOST / 8), files.band_base_count = m->chosen_count, files_band_apply();
                return;
        }
        if (l->menu_asked)
        {
                int mrow = ui_list_hit(ui, l, l->menu_x, l->menu_y);
                ui_view_cancel(ui, &files.big->view);
                if (files.editing)
                        files_edit(0);
                if (mrow >= 0 && files_model_entry(m, (p32)mrow))
                {
                        if (!files_model_chosen(m, (p32)mrow))
                                files_model_choose(m, (p32)mrow, FILES_CHOOSE_ONLY);
                        m->cursor = (p32)mrow, m->revision++;
                }
                files_menu_open(mrow >= 0, l->menu_x, l->menu_y);
                return;
        }
        if (down && l->down >= 0 && (p32)l->down != m->cursor)
                m->cursor = (p32)l->down, m->revision++;
        if (l->ground_clicked)
                if (m->chosen_count || m->cursor != FILES_NO)
                        files_model_choose_none(m), m->cursor = FILES_NO, m->revision++;
        if (row >= 0)
        {
                files.said = 0;
                if ((event->flags & WINDOW_KEY_TOUCH) || l->double_clicked)
                        files_open((p32)row);
                else
                        files_model_choose(m, (p32)row, event->flags & WINDOW_KEY_CONTROL ? FILES_CHOOSE_TOGGLE : event->flags & WINDOW_KEY_SHIFT ? FILES_CHOOSE_RANGE : FILES_CHOOSE_ONLY);
        }
}

// The bar's buttons: one pressed (let go on) does what it does; a press on one is the bar's, and the hover is each button's to keep (its tip).
static b32 files_bar_event(const struct window_event address_to event)
{
        int x = event->type == WINDOW_EVENT_MOTION ? (int)event->a : ui_kit.pointer_x, y = event->type == WINDOW_EVENT_MOTION ? (int)event->b : ui_kit.pointer_y;
        b32 took = false;
        for (int k = 0; k < FILES_NAVS; k++)
        {
                struct ui_iconbutton address_to b = &files.nav[k];
                b32 hover = b->hover;
                if (ui_iconbutton_event(&files.ui, b, event, x, y) & UI_ICONBUTTON_PRESSED)
                        files_menu_do(files_navs[k].act);
                took |= event->type == WINDOW_EVENT_BUTTON && files_in_rect(x, y, b->x, b->y, b->w, b->h);
                files.dirty |= hover != b->hover || took;
        }
        {
                struct ui_menubutton address_to v = &files.view_button;
                b32 hover = v->hover;
                if (ui_menubutton_event(&files.ui, v, event, x, y))
                        files_view_open();
                took |= event->type == WINDOW_EVENT_BUTTON && files_in_rect(x, y, v->x, v->y, v->w, v->h);
                files.dirty |= hover != v->hover || took;
        }
        return took;
}

static b32 files_event(const struct window_event address_to event)
{
        struct ui address_to ui = &files.ui;
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        p32 asks;
        files_layout();
        ui_drag_button(&files.drag, event, 0, 0, false);
        if (event->type == WINDOW_EVENT_DRAG && files.drag.live &&
            (event->a == WINDOWS_DRAG_ACTION || event->a == WINDOWS_DRAG_ENDED || event->a == WINDOWS_DRAG_DECLINED))
                return files_drag_event(event);
        if (files_field_event(event, down) || files_bar_event(event))
                return true;
        asks = ui_view_event(ui, &files.big->view, event);
        if (asks & UI_VIEW_QUIT)
                return false;
        files.dirty |= (asks & UI_VIEW_DRAW) != 0;
        if (asks & UI_VIEW_TAKEN)
                return true;
        if (files.view == FILES_VIEW_DETAILS && (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON))
        {
                p32 took = ui_columns_event(ui, &files.columns, event, event->type == WINDOW_EVENT_MOTION ? event->a : ui_kit.pointer_x,
                                            event->type == WINDOW_EVENT_MOTION ? event->b : ui_kit.pointer_y);
                if (took & UI_COLUMNS_SORTED)
                        files_model_sort(m, files_sort_cols[files.columns.sorted], files.columns.descending), files_show_cursor();
                files.dirty |= (took & UI_COLUMNS_DRAW) != 0;
                if (took & UI_COLUMNS_TAKEN)
                        return true;
        }
        switch (event->type)
        {
        case WINDOW_EVENT_DRAG: return files_drag_event(event);
        case WINDOW_EVENT_CLIP: return files_clip_event(event);
        case WINDOW_EVENT_KEY: return !down || ui_key_modifier((int)event->a) ? true : files_key(event);
        case WINDOW_EVENT_MOTION:
                files.dirty |= files.carrying;
                if (files.carry_press && ui_press_past(ui, files.carry_px, files.carry_py, (int)event->a, (int)event->b))
                {
                        if (files.carrying && files.carry_count &&
                            files_drag_offer_list(files.carry_paths, files.carry_used, files.carry_count, (files.carry_kind & 0xffu) + 1))
                                files_carry_end(), files.carry_press = false;
                        return files.dirty = true, true;
                }
                ui_view_list_click(ui, &files.big->view, event, event->a, event->b, null);
                if (l->band_moved)
                        files_band_apply();
                if (l->drag_began)
                        files_drag_start((p32)l->drag_row);
                if (files_path_chip((int)event->a, (int)event->b))
                        ui_kit_shape = WINDOW_POINTER_HAND;
                files.dirty = true;
                break;
        case WINDOW_EVENT_BUTTON: files_button_event(event, down); break;
        }
        return true;
}

/* ---- draw, tick, lifecycle ---- */

static fn files_status_pill_draw(struct ui address_to ui, const struct ui_list address_to l)
{
        int pad_x = ui_dev(ui, 10), ph = ui_dev(ui, 28), margin_r = ui_dev(ui, UI_PAD), margin_b = ui_dev(ui, 10);
        int max_w = l->w - 2 * margin_r;
        char cut[192];
        string_address text;
        int tw, pw, px, py, baseline;
        struct ui_box box;

        if (!files.big->status[0] || max_w < 2 * pad_x + ui_dev(ui, 24) || l->h < ph + 2 * margin_b)
                return;
        text = ui_fit(ui, (string_address)files.big->status, UI_LABEL_MEDIUM, max_w - 2 * pad_x, cut, sizeof(cut));
        tw = ui_kit_measure(ui, text, UI_LABEL_MEDIUM);
        pw = tw + 2 * pad_x;
        if (pw > max_w)
                pw = max_w;
        px = ui_mirror(l->x + l->w - margin_r - pw, pw, l->x, l->w);
        py = l->y + l->h - margin_b - ph;
        box = ui_box_px(px, py, pw, ph, ph * 128);
        ui_kit_fill(ui, &box, ui_tone(ui_role(UI_SURFACE_CONTAINER) & 0xffffffu, 236), null);
        ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_tone(ui_role(UI_OUTLINE_VARIANT) & 0xffffffu, 110));
        baseline = ui_kit_baseline(ui, py + (ph - ui_dev(ui, ui_types[UI_LABEL_MEDIUM].line)) / 2, UI_LABEL_MEDIUM);
        ui_kit_text(ui, px + pad_x, baseline, text, UI_LABEL_MEDIUM, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0);
}

// The band across the top, over the view's drawing: the path field (the rename field, while there is one, is a control of the view's).
static fn files_bar_draw(struct ui address_to ui)
{
        const struct ui_rect address_to b = &files.path_box;
        for (int k = 0; k < FILES_NAVS; k++)
                ui_iconbutton_draw(ui, &files.nav[k]);
        ui_menubutton_draw(ui, &files.view_button);
        if (files.editing != FILES_EDIT_NAME)
                ui_tokens_draw(ui, &files.big->path, b->x, b->y, b->x + b->w, b->y + b->h);
}

// The tip of the button the pointer is on, over everything else the window draws.
static fn files_bar_tips(struct ui address_to ui)
{
        for (int k = 0; k < FILES_NAVS; k++)
                ui_iconbutton_tip(ui, &files.nav[k]);
}

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
        files_bar_draw(ui);
        files_status_pill_draw(ui, l);
        if (files.editing)
                ui_text_overlay(ui, files.editing == FILES_EDIT_PATH ? &files.big->path.text : files_field());
        if (files.finding)
                ui_text_overlay(ui, &files.finder);
        files_carry_draw();
        files_bar_tips(ui);
        files.seen = files.big->model.revision;
}

/*
        The devices are what moonwater device list says, read in a job of their own: when the mounts changed, when a block device came
        or went, and every FILES_CENSUS_NS otherwise; one reading at a time, and one asked for while another runs is made after it.
*/
static fn files_census_done(struct ui_job address_to j, b32 killed, positive status)
{
        b32 ok = !killed && (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 0;
        if (ok && files_devices_read(&files.big->devices, (const p8 address_to)files.big->census, j->length) &&
            files_tree_sync_drives(&files.big->tree, (const p8 address_to)getenv((string_address) "HOME"), false))
                files_tree_follow(), files.dirty = true;
        files.next_census = files.census_again ? 0 : (b64)ui_kit.now + FILES_CENSUS_NS, files.census_again = false;
}

static fn files_census(void)
{
        string_address words[4] = {(string_address) "/moonwater", (string_address) "device", (string_address) "list", null};
        if (files.runner.child > 0)
        {
                files.census_again = true;
                return;
        }
        files.runner.done = files_census_done, files.runner.data = null;
        files.next_census = (b64)ui_kit.now + FILES_CENSUS_NS;
        (void)ui_job_start(&files.runner, words, 3, null, files.big->census, sizeof(files.big->census), (positive)FILES_CENSUS_WAIT_NS);
}

static long files_tick(long timeout, long address_to other)
{
        struct files_model address_to m = &files.big->model;
        struct ui_text address_to active = files.editing == FILES_EDIT_PATH ? &files.big->path.text : files.editing ? files_field() : files.finding && files.focus == FILES_FOCUS_FIND ? &files.finder : null;
        b64 now = (b64)ui_kit.now;
        files_remote_tick(&timeout, other);
        if (m->state == FILES_READY && m->listed != files.git_key)
                files.git = files_model_is_remote(m) ? false : files_git_marks(m, files.big->git_state), files.git_key = m->listed, files.dirty = true;
        if (files.drop_over)
        {
                struct ui_list address_to lists[2] = {&files.big->list, &files.big->tree_list};
                if (files.spring >= 0 && (b64)ui_kit_time() - files.spring_at >= FILES_SPRING_NS)
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
                        files.dirty |= ui_list_edge_scroll(&files.ui, lists[k], files.drop.x, files.drop.y, FILES_EDGE, FILES_EDGE_STEP, false);
                timeout = ui_timeout_min(timeout, 16000000);
        }
        if (files.big->list.band)
        {
                if (ui_list_edge_scroll(&files.ui, &files.big->list, files.big->list.x, files.big->list.band_y1, FILES_EDGE, FILES_EDGE_STEP, true))
                        files_band_apply(), files.dirty = true;
                timeout = ui_timeout_min(timeout, 16000000);
        }
        if (files.flash_used)
        {
                b64 since = (b64)ui_kit_time() - files.flash_at;
                files.dirty = true;
                if (since >= FILES_FLASH_NS)
                        files.flash_used = 0;
                else
                        timeout = ui_timeout_min(timeout, (long)(FILES_FLASH_NS - since));
        }
        if (files.carrying && now - files.carry_at < FILES_CARRY_POP_NS + 20000000ll)
                files.dirty = true, timeout = ui_timeout_min(timeout, 16000000);
        if (active)
        {
                b32 redraw = false;
                timeout = ui_text_timeout(&files.ui, active, timeout, &redraw), files.dirty |= redraw;
        }
        if (files.moving)
        {
                struct files_move address_to job = &files.big->move;
                FILES_SLICE(!job->worker && files_move_running(job), files_move_step(job));
                if (!files_move_running(job))
                {
                        if (files.measuring)
                        {
                                files.measuring = false;
                                if (files.sheet && files.menu.open)
                                        files_sheet_show();
                        }
                        else
                                files_move_said(), files_move_deed(job), files_reload();
                        files_move_end(job), files.moving = false;
                }
                else if (now >= files.next_progress)
                        files.next_progress = now + FILES_SHOW_NS, files.dirty = true;
                if (files.moving)
                        timeout = job->worker ? ui_timeout_min(timeout, (long)FILES_SHOW_NS) : 1;
        }
        if (files.big->job.state == FILES_JOB_RUNNING)
        {
                struct files_job address_to job = &files.big->job;
                b32 more = true;
                FILES_SLICE(more, more = files_job_step(m, job, 16));
                if (!more)
                {
                        char at[FILES_NAME * 3 + 4];
                        ui_label_foreign(at, 96, job->at, string_length(job->at));
                        if (job->state == FILES_JOB_FAILED)
                                language_format(files.big->note, sizeof(files.big->note), WORDS(files_delete_stopped), (int)job->removed, at);
                        else
                                language_format(files.big->note, sizeof(files.big->note), WORDS(files_deleted), (int)job->removed);
                        files_job_end(job), files_after_delete(m), files_model_reload(m);
                }
                if (!more || now >= files.next_show)
                        files.next_show = now + FILES_SHOW_NS, files.dirty = true;
                return 1;
        }
        if (m->walking)
        {
                b32 more = true, reveal_came = !m->reloading && m->came[0] != 0;
                FILES_SLICE(more, more = files_model_step(m, FILES_STEP));
                if (!more || now >= files.next_show)
                {
                        files.next_show = now + FILES_SHOW_NS, files.dirty |= files.seen != m->revision;
                        if (!more && reveal_came && !files.big->list.scroll.scroll && !ui_body_going(&files.big->list.scroll.body))
                                files_show_cursor();
                }
                return more ? 1 : timeout;
        }
        files.dirty |= files.seen != m->revision;
        if (now >= files.next_look && !(files.own.table->state[files.own.row] & WINDOW_MINIMIZED))
        {
                int became = files_model_is_remote(m) ? FILES_FRESH : files_model_stale(m);
                if (became == FILES_STALE)
                        files_model_reload(m);
                b32 census = files_tree_sync_drives(&files.big->tree, (const p8 address_to)getenv((string_address) "HOME"), false);
                p64 blocks = files_devices_blocks();
                if (census)
                        files_tree_follow(), files.dirty = true;
                if (blocks != files.big->devices.blocks)
                        files.big->devices.blocks = blocks, census = true;
                if (census || now >= files.next_census)
                        files_census();
                files_peers_sync();
                files.dirty |= became != FILES_FRESH, files.next_look = now + FILES_LOOK_NS;
                if (m->walking)
                        return 1;
        }
        timeout = ui_deadline_wait(timeout, now, files.next_look);
        return timeout == 0 ? 1 : timeout;
}

// The window's parts that need no window of the compositor's: the lists, the columns, the fields and the path band (the browser's check rows
// open them so too).
static b32 files_parts_open(void)
{
        struct ui_list address_to lists[2] = {&files.big->list, &files.big->tree_list};
        for (int k = 0; k < 2; k++)
                lists[k]->virtual = lists[k]->beside = true, lists[k]->row_h = FILES_ROW, lists[k]->damaged = files_damaged, lists[k]->hover = lists[k]->down = -1;
        files.big->list.describe = files_describe, files.big->list.selected = files_row_selected;
        files.big->tree_list.tree = true, files.big->tree_list.describe = files_tree_describe;
        files.path.kind = files.finder_field.kind = UI_INPUT, files.path.variant = files.finder_field.variant = UI_FIELD_BARE;
        files.path.edit = files_field(), files.finder_field.edit = &files.finder;
        files.drop_row = files.drop_node = files.spring = -1;
        files.columns.count = 4, files.columns.at_end[1] = true, files.columns.sorted = 0, files.columns.hover = files.columns.held = -1;
        files.columns.width[1] = 90, files.columns.width[2] = 110, files.columns.width[3] = 140;
        if (!ui_tokens_open(&files.big->path, &files.own, "", '/'))
                return false;
        ui_tokens_focus(&files.big->path, false, false);
        return true;
}

static int files_open_window(void)
{
        if (!(files.big = ui_app_map(&files_app, sizeof(struct files_big))))
                return 1;
        if (!files_model_open(&files.big->model))
                return compositor_kmsg((string_address) "files: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) ""), 1;
        files_app.view = &files.big->view, files_move_end(&files.big->move);
        if (!files_parts_open())
                return 1;
        if (!files_tree_begin(&files.big->tree))
                return 1;
        files.big->tree.devices = &files.big->devices, files.big->devices.blocks = files_devices_blocks();
        files_tree_populate_live(&files.big->tree, (const p8 address_to)getenv((string_address) "HOME"));
        files_census();
        files_peers_sync();
        if (!files.start || !files_model_go(&files.big->model, files.start))
                files_model_go(&files.big->model, (string_address) "/");
        files.next_look = (b64)ui_kit.now + FILES_LOOK_NS;
        files_trail_note(&files.trail, files.big->model.path, files.big->model.handle >= 0 ? &files.big->model.folder : null);
        files_tree_follow(), ui_app_place(&files_app, ui_px(&files.ui, FILES_WIDTH), ui_px(&files.ui, FILES_HEIGHT));
        return -1;
}

static fn files_chose(int id)
{
        if (id >= UI_TOKENS_ITEM && id < UI_TOKENS_ITEM + UI_TOKENS_MOST)
        {
                if (ui_tokens_menu_do(&files.ui, &files.big->path, id) & UI_TOKENS_PRESSED)
                        files_path_pressed();
        }
        else if (id >= FILES_MENU_OPEN)
                files_menu_do(id), files.dirty = true;
        else if (files.menu.field && ui_text_menu_do(&files.ui, files.menu.field, id))
                files.dirty = true;
}

static struct ui_app files_app = {
    .own = &files.own, .ui = &files.ui,
    .missing = (string_address) "files: no window: the compositor's desktop is not up\n",
    .title = (string_address) "Files", .name = (string_address) "files",
    .glass = 0, .side = true,
    .nofont = (string_address) "files: no font to draw with (-f FONT.ttf), pid ",
    .dirty = &files.dirty, .focused = &files.focused, .ground = &files.ground,
    .open = files_open_window, .close = files_close_window, .event = files_event, .tick = files_tick, .job = &files.runner,
    .popup = &files.menu, .chose = files_chose,
    .width = &files.width, .height = &files.height, .shown = &files.shown, .draw = files_draw,
};

static b32 files_main()
{
        memory_zero(&files, sizeof(files));
        files.runner.output = -1;
        files.start = getenv((string_address) "HOME");
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took ? took : 1)
                if (!(took = ui_app_option(&files_app, at)) && program_argument(at)[0] == '/')
                        files.start = program_argument(at);
        return ui_app_run(&files_app);
}

#endif // MOONWATER_CANVAS_FILES
