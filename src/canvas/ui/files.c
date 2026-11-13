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

// Everything that is large: one mapping, not the image's bss.
struct files_big
{
        struct files_model model;
        struct ui_list list;
        struct ui_view view;
        char path[FILES_PATH * 3 + 1]; // the path as the top of the window writes it
        char status[192];              // the foot's words
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct files_big address_to big;
        int width, height, shown;
        unsigned int ground;
        b32 focused, dirty;
        struct ui_rect list_box; // what the tree gave the rows
        p32 seen;                // the model's revision as last drawn
        p32 clicked;             // the place a mouse last clicked, and when: a second click there in time opens it
        b64 clicked_at, next_look, next_show;
        unsigned int said;       // LANGUAGE_files_* the foot says of the last thing asked, 0 none
        string_address start;    // the folder asked for as it started
} files;

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

// A row, for the kit's list: the entry at that place of the listing (the list is virtual: a row's id is its place). A file's size at its
// right, when it was changed before that, and a dot before the name of one that is chosen.
static fn files_describe(int id, struct ui_row address_to row)
{
        static char label[FILES_NAME * 3 + 4], size[16], when[24];
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
        row->value = size, row->note = when;
        row->mark = files_model_chosen(m, (p32)id) && m->chosen_count > 1;
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
        if (files.said)
                string_copy_bounded((string_address)into, (string_address)language_text(files.said), room);
        else if (m->state == FILES_READY && m->chosen_count > 1)
                language_format(into, room, WORDS(files_chosen), (int)m->chosen_count, (int)m->shown.count);
        else if (m->state == FILES_READY || (m->state == FILES_READING && m->shown.count))
                language_format(into, room, m->shown.cut ? WORDS(files_count_cut) : WORDS(files_count), (int)m->shown.count);
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
                l->virtual = true;
                l->describe = files_describe;
                l->damaged = files_damaged;
                l->hover = l->down = -1;
        }
        ui_label_foreign(files.big->path, sizeof(files.big->path), m->path, string_length(m->path));
        files_status_text();
        ui_view_begin(&files.big->view, ui, files.width, -files.height);
        ui_row(.class = "pathbar")
        {
                ui_button(WORDS(files_back), .class = "ghost", .on = files_act, .arg = 0);
                ui_button(WORDS(files_forward), .class = "ghost", .on = files_act, .arg = 1);
                ui_button(WORDS(files_up), .class = "ghost", .on = files_act, .arg = 2);
                ui_text(files.big->path, .class = "path");
        }
        ui_col(.class = "rows", .grow = 1)
                ui_host(&files.list_box, .class = "list", .grow = 1);
        ui_row(.class = "bar")
                ui_text(files.big->status, .class = "status");
        ui_view_end(&files.big->view);
        // Every entry is a row of the list, and the row the keys are on is the model's cursor.
        l->total = (int)files_model_count(m);
        l->focus = m->cursor != FILES_NO ? (int)m->cursor : -1;
        ui_list_place(ui, l, files.list_box.x, files.list_box.y, files.list_box.w, files.list_box.h);
        // A row runs from one edge of the window to the other, as the settings window's and the launcher's do: the words stay in
        // from the margin, where the list's own box is.
        l->row_x = 0, l->row_w = files.width;
        l->empty = files_state_text(m);
        ui_list_layout(ui, l);
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

// Another folder is shown: its rows from the top, nothing said of the folder before.
static fn files_moved(void)
{
        files.clicked = FILES_NO, files.said = 0;
        ui_scroll_set(&files.big->list.scroll, 0);
        files.next_look = (b64)ui_kit.now + FILES_LOOK_NS;
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

static b32 files_key(const struct window_event address_to event)
{
        struct files_model address_to m = &files.big->model;
        struct ui_list address_to l = &files.big->list;
        b32 alt = (event->flags & WINDOW_KEY_ALT) != 0, control = (event->flags & WINDOW_KEY_CONTROL) != 0, shift = (event->flags & WINDOW_KEY_SHIFT) != 0;
        int page = l->scroll.view / ui_px(&files.ui, UI_ROW) - 1, key = (int)event->a;
        bipolar by = 0;

        page = page < 1 ? 1 : page;
        if (alt && (key == UI_KEY_LEFT || key == UI_KEY_RIGHT))
                return files_act(key == UI_KEY_RIGHT);
        if ((alt && key == UI_KEY_UP) || key == UI_KEY_BACKSPACE)
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
        if (control && key == FILES_KEY_A)
        {
                if (m->shown.count)
                {
                        files_model_choose(m, 0, FILES_CHOOSE_ONLY);
                        files_model_choose(m, m->shown.count - 1, FILES_CHOOSE_RANGE);
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
        case UI_KEY_UP: by = -1; break;
        case UI_KEY_DOWN: by = 1; break;
        case UI_KEY_PAGE_UP: by = -page; break;
        case UI_KEY_PAGE_DOWN: by = page; break;
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
        // The band's buttons first: a press on one, and the keys while they are on one (Shift+Tab gives them the keys).
        asks = ui_view_event(ui, &files.big->view, event);
        if (asks & UI_VIEW_QUIT)
                return false;
        files.dirty |= (asks & UI_VIEW_DRAW) != 0;
        if (asks & UI_VIEW_TAKEN)
                return true;
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return !down || ui_key_modifier((int)event->a) ? true : files_key(event);
        case WINDOW_EVENT_MOTION:
                ui_list_click(ui, l, event, event->a, event->b);
                files.dirty = true;
                break;
        case WINDOW_EVENT_WHEEL:
                if (ui_wheel_turn(ui, &l->scroll, event))
                        files.dirty = true;
                break;
        case WINDOW_EVENT_BUTTON:
                files.dirty = true;
                row = ui_list_click(ui, l, event, ui_kit.pointer_x, ui_kit.pointer_y);
                // The press itself puts the cursor on its row, as the list lights it.
                if (down && l->down >= 0 && (p32)l->down != m->cursor)
                        m->cursor = (p32)l->down, m->revision++;
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
        l->lit = l->focus >= 0, l->keys = files.focused && !files.big->view.group.keys;
        ui_list_draw(ui, l);
        ui_view_draw(ui, &files.big->view, files.focused);
        files.seen = files.big->model.revision;
}

// One picture, into the buffer the compositor is not reading; false when there is none yet.
static b32 files_frame(void)
{
        return ui_app_present(&files.own, &files.shown, &files.dirty, files.width, files.height, files_draw);
}

static fn files_dirty(void)
{
        files.dirty = true;
}

static fn files_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = files.own.table;

        files.width = (int)(width > table->output_width ? table->output_width : width);
        files.height = (int)(height > table->output_height ? table->output_height : height);
        ui_scratch_fit(&files.ui, (unsigned int)files.width);
        files.dirty = true;
}

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

static fn files_language(void)
{
        files.dirty = true;
}

static int files_open_window(void)
{
        struct windows address_to table = files.own.table;
        int width = ui_px(&files.ui, FILES_WIDTH), height = ui_px(&files.ui, FILES_HEIGHT);
        int room = (int)table->output_height - ui_px(&files.ui, 2 * (24 + 24));
        long mapped = ui_map(sizeof(struct files_big));

        if (mapped < 0 && mapped > -4096)
        {
                compositor_kmsg((string_address) "files: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }
        files.big = (struct files_big address_to)mapped;
        if (!files_model_open(&files.big->model))
        {
                compositor_kmsg((string_address) "files: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }
        files.clicked = FILES_NO;
        if (!files.start || !files_model_go(&files.big->model, files.start))
                files_model_go(&files.big->model, (string_address) "/");
        files.next_look = (b64)ui_kit.now + FILES_LOOK_NS;
        files_size((unsigned int)(width > (int)table->output_width ? (int)table->output_width : width),
                   (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_centre(table, files.own.row, files.width, files.height, ui_px(&files.ui, 12));
        files_frame();
        return -1;
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
        .frame = files_frame,
        .language = files_language,
        .event = files_event,
        .changed = files_dirty,
        .size = files_size,
        .tick = files_tick,
};

static b32 files_main()
{
        memory_zero(&files, sizeof(files));
        files.shown = -1;
        files.start = getenv((string_address) "HOME");
        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address value;

                if (at + 1 < (positive)program_argument_count() && (value = ui_argument(at, (string_address) "-f")))
                        files_app.font = value, at++;
                else if (at + 1 < (positive)program_argument_count() && (value = ui_argument(at, (string_address) "-b")))
                        files_app.bold = value, at++;
                else if (program_argument(at)[0] == '/')
                        files.start = program_argument(at);
        }
        return ui_app_run(&files_app);
}

#endif // MOONWATER_CANVAS_FILES
