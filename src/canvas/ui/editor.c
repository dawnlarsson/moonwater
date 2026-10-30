/*
        The editor: files in the code view of the text engine, the proof that the kit carries a code editor.

            editor [FILE]...

        Each file is a tab (Ctrl+Tab and Ctrl+Shift+Tab go round them, Ctrl+N a new one, Ctrl+W closes one). A file of
        8 MiB or more is mapped, not read: the engine's pieces point into the mapping and only an edit is ever copied,
        so a hundred megabytes open at once. What opens is a regular file only (a FIFO, a device or a socket is refused,
        and /proc's files, which say nothing of their size, are read up to 64 MiB); the path a link names is
        resolved when the file opens, and a save goes to that file, so the link stays a link.

        Ctrl+S saves: the bytes into a new file beside the old one with the old one's mode, synced, and renamed over it,
        so a crash leaves the old file or the new one and never half of either; the byte order mark is written back if
        the file had one. A file changed by someone else since it was opened or saved is said so in the status bar when
        the window gets the keys again, and Ctrl+R reads it again. Ctrl+F finds (Enter the next match, Shift+Enter the
        one before, Alt+C case, Alt+W whole words, Alt+R a pattern), Ctrl+H replaces every match as one undo, Ctrl+G
        goes to a line. The status bar says the line and column, how many are selected, the encoding, the line ends,
        the language and the indent. The right button opens the context menu.

        Not the full-screen `edit` of a terminal: this is a window of the compositor's.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_EDITOR
#define MOONWATER_CANVAS_EDITOR

#define EDITOR_TABS 8
#define EDITOR_MAP_FROM (8u << 20)
#define EDITOR_READ_MOST (64u << 20)
#define EDITOR_PATH 512
#define EDITOR_BAR 26 // the status bar's height, logical pixels
#define EDITOR_FIND 34 // the find bar's

struct editor_tab
{
        struct ui_text view;
        char path[EDITOR_PATH];  // the file a save writes, a link's target already
        p8 address_to bytes;      // what was opened: a mapping or a read
        positive length, mapped;  // mapped: the mapping's length, 0 when read
        p64 size, mtime, mtime_nsec;
        unsigned int mode;
        b32 open, changed_on_disk;
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct editor_tab tabs[EDITOR_TABS];
        int tab, count;
        b32 dirty, focused;
        unsigned int ground;
        int width, height, shown, pointer_x, pointer_y;
        // The find bar: its field, what it does (0 closed, 1 find, 2 replace, 3 go to a line), and its flags.
        struct ui_text find;
        struct ui_control find_control;
        int finding;
        p32 find_flags;
        struct doc_find search;
        char status[160];
        struct ui_popup menu;
} editor = {.shown = -1};

static struct editor_tab address_to editor_now(void)
{
        return &editor.tabs[editor.tab];
}

// A file's bytes: mapped when large, read when small, refused when it is not a regular file. False with a status.
static b32 editor_load(struct editor_tab address_to tab, string_address path)
{
        // Without blocking: opening a FIFO to read waits for a writer, and the editor would hang on a hostile path.
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
        error_stat facts;
        char link[64], resolved[EDITOR_PATH];
        bipolar got;

        tab->bytes = null, tab->length = tab->mapped = 0;
        if (handle < 0)
        {
                // A file that is not there yet is a new file of that name.
                string_copy_bounded((string_address)tab->path, path, EDITOR_PATH);
                tab->mode = 0644;
                return handle == -2;
        }
        if (fstat((b32)handle, address_of facts) < 0 || (facts.st_mode & 0170000) != 0100000)
        {
                system_close(handle);
                string_copy_bounded((string_address)editor.status, (string_address) "not a regular file: refused", sizeof(editor.status));
                return false;
        }
        // The path the descriptor really names: a link's target, which a save then writes.
        string_copy((string_address)link, (string_address) "/proc/self/fd/");
        link[14 + positive_into_string((string_address)link + 14, (positive)handle)] = 0;
        got = system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)link, (positive)resolved, sizeof(resolved) - 1);
        resolved[got > 0 ? got : 0] = 0;
        string_copy_bounded((string_address)tab->path, got > 0 ? (string_address)resolved : path, EDITOR_PATH);
        tab->size = (p64)facts.st_size, tab->mtime = (p64)facts.st_mtime, tab->mtime_nsec = (p64)facts.st_mtime_nsec;
        tab->mode = facts.st_mode & 07777;
        if (facts.st_size >= EDITOR_MAP_FROM)
        {
                long mapped = (long)system_call_6(syscall(mmap), 0, (positive)facts.st_size, 1, 2, (positive)handle, 0);

                if (!(mapped < 0 && mapped > -4096))
                        tab->bytes = (p8 address_to)mapped, tab->length = tab->mapped = (positive)facts.st_size;
        }
        else
        {
                positive room = 0;

                while (tab->length < EDITOR_READ_MOST)
                {
                        if (!array_store_reserve(tab->bytes, room, tab->length, tab->length + 65536, 65536))
                                break;
                        got = system_read_once((b32)handle, tab->bytes + tab->length, room - tab->length);
                        if (got <= 0)
                                break;
                        tab->length += (positive)got;
                }
        }
        system_close(handle);
        return true;
}

static b32 editor_open(string_address path)
{
        struct editor_tab address_to tab;
        string_address name = path;

        if (editor.count == EDITOR_TABS)
                return false;
        tab = &editor.tabs[editor.count];
        memory_zero(tab, sizeof(*tab));
        if (path && !editor_load(tab, path))
                return false;
        if (!ui_text_open(&tab->view, UI_TEXT_CODE, tab->bytes ? tab->bytes : (const p8 address_to) "", tab->length, ui_text_keymap()))
                return false;
        tab->view.own = &editor.own;
        for (string_address at = path; at && *at; at++)
                if (*at == '/')
                        name = at + 1;
        tab->view.doc.language = doc_language_of((const char address_to)name, tab->bytes, tab->length < 256 ? tab->length : 256);
        tab->view.comment = doc_languages[tab->view.doc.language].comment;
        tab->open = true;
        editor.tab = editor.count++;
        return true;
}

// The file written whole beside itself and renamed over itself, its mode kept: false with a status.
static b32 editor_save(struct editor_tab address_to tab)
{
        char temporary[EDITOR_PATH + 32];
        positive length = string_length((string_address)tab->path), at = 0, slash = length;
        bipolar handle;
        error_stat facts;
        static p8 chunk[65536];
        b32 ok = true;

        if (!length)
        {
                string_copy_bounded((string_address)editor.status, (string_address) "no file name: start the editor with one", sizeof(editor.status));
                return false;
        }
        while (slash && tab->path[slash - 1] != '/')
                slash--;
        memory_copy(temporary, tab->path, slash);
        string_copy((string_address)temporary + slash, (string_address) ".");
        string_copy_bounded((string_address)temporary + slash + 1, (string_address)tab->path + slash, EDITOR_PATH - slash - 1);
        string_copy((string_address)temporary + string_length((string_address)temporary), (string_address) ".editing");
        handle = system_open_at_mode(AT_FDCWD, (string_address)temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, tab->mode ? tab->mode : 0644);
        if (handle < 0)
        {
                string_copy_bounded((string_address)editor.status, (string_address) "could not write beside the file", sizeof(editor.status));
                return false;
        }
        if (tab->view.doc.bom)
                ok &= system_write_all((b32)handle, "\xef\xbb\xbf", 3) >= 0;
        while (ok && at < doc_length(&tab->view.doc))
        {
                positive got = doc_read(&tab->view.doc, at, chunk, sizeof(chunk));

                ok &= system_write_all((b32)handle, chunk, got) >= 0;
                at += got;
        }
        ok &= system_call_1(syscall(fsync), (positive)handle) == 0;
        system_call_2(syscall(fchmod), (positive)handle, tab->mode ? tab->mode : 0644);
        system_close(handle);
        if (!ok || system_call_4(syscall(renameat), (positive)AT_FDCWD, (positive)temporary, (positive)AT_FDCWD, (positive)tab->path) < 0)
        {
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)temporary, 0);
                string_copy_bounded((string_address)editor.status, (string_address) "the save failed: the file is as it was", sizeof(editor.status));
                return false;
        }
        handle = system_open_at(AT_FDCWD, (string_address)tab->path, FILE_READ | O_CLOEXEC);
        if (handle >= 0 && fstat((b32)handle, address_of facts) == 0)
                tab->size = (p64)facts.st_size, tab->mtime = (p64)facts.st_mtime, tab->mtime_nsec = (p64)facts.st_mtime_nsec;
        if (handle >= 0)
                system_close(handle);
        doc_mark_saved(&tab->view.doc);
        tab->changed_on_disk = false;
        string_copy_bounded((string_address)editor.status, (string_address) "saved", sizeof(editor.status));
        return true;
}

// Whether the file was changed by someone else since it was opened or saved.
static fn editor_check_disk(struct editor_tab address_to tab)
{
        bipolar handle = tab->path[0] ? system_open_at(AT_FDCWD, (string_address)tab->path, FILE_READ | O_CLOEXEC) : -1;
        error_stat facts;

        if (handle < 0)
                return;
        if (fstat((b32)handle, address_of facts) == 0 &&
            ((p64)facts.st_size != tab->size || (p64)facts.st_mtime != tab->mtime || (p64)facts.st_mtime_nsec != tab->mtime_nsec))
                tab->changed_on_disk = true;
        system_close(handle);
}

// The file read again (Ctrl+R), what was typed since given up.
static fn editor_reload(void)
{
        struct editor_tab address_to tab = editor_now();
        char path[EDITOR_PATH];

        string_copy_bounded((string_address)path, (string_address)tab->path, EDITOR_PATH);
        ui_text_close(&tab->view);
        if (tab->mapped)
                system_call_2(syscall(munmap), (positive)tab->bytes, tab->mapped);
        editor.count--;
        memory_copy(&editor.tabs[editor.tab], &editor.tabs[editor.tab + 1], (positive)(editor.count - editor.tab) * sizeof(editor.tabs[0]));
        editor_open((string_address)path);
}

static fn editor_close_tab(void)
{
        struct editor_tab address_to tab = editor_now();

        ui_text_close(&tab->view);
        if (tab->mapped)
                system_call_2(syscall(munmap), (positive)tab->bytes, tab->mapped);
        editor.count--;
        memory_copy(&editor.tabs[editor.tab], &editor.tabs[editor.tab + 1], (positive)(editor.count - editor.tab) * sizeof(editor.tabs[0]));
        editor.tab = editor.tab >= editor.count ? editor.count - 1 : editor.tab;
        if (editor.count <= 0)
        {
                editor_open(null);
                editor.tab = 0;
        }
}

// The find bar opened for what (1 find, 2 replace, 3 go to a line), its field emptied for a line, holding the selection for a find.
static fn editor_find_open(int what)
{
        struct ui_text address_to view = &editor_now()->view;
        p8 selected[256];
        positive length = 0;

        if (!editor.find.doc.selections)
        {
                ui_text_open(&editor.find, UI_TEXT_FIELD, (const p8 address_to) "", 0, ui_text_keymap());
                editor.find.own = &editor.own;
        }
        if (what != 3 && doc_any_selected(&view->doc))
        {
                const struct doc_selection address_to s = &view->doc.selections[view->doc.primary];

                length = doc_sel_end(s) - doc_sel_start(s) < sizeof(selected) ? doc_read(&view->doc, doc_sel_start(s), selected, (positive)(doc_sel_end(s) - doc_sel_start(s))) : 0;
        }
        if (what == 3 || length)
                ui_text_set(&editor.find, selected, length);
        doc_select_all(&editor.find.doc);
        editor.finding = what;
        editor.find.focused = true;
        view->focused = false;
}

// The next match of the find bar's text from the caret (back: before it), selected and shown.
static fn editor_find_step(b32 back)
{
        struct ui_text address_to view = &editor_now()->view;
        p8 query[1024];
        positive length = ui_text_string(&editor.find, query, sizeof(query));
        p64 start, finish, from = back ? doc_sel_start(&view->doc.selections[view->doc.primary]) : doc_sel_end(&view->doc.selections[view->doc.primary]);

        if (!doc_find_set(&editor.search, query, length, editor.find_flags))
        {
                string_copy_bounded((string_address)editor.status, (string_address) "the pattern does not compile", sizeof(editor.status));
                return;
        }
        if (doc_find_next(&view->doc, &editor.search, from, back, true, &start, &finish))
        {
                doc_select_set(&view->doc, start, finish);
                ui_text_reveal(&editor.ui, view);
                editor.status[0] = 0;
        }
        else
                string_copy_bounded((string_address)editor.status, (string_address) "no match", sizeof(editor.status));
}

// What the find bar's Enter does: find, replace every match with what follows a / in the field, or go to its line.
static fn editor_find_enter(b32 back)
{
        struct ui_text address_to view = &editor_now()->view;
        p8 text[1024];
        positive length = ui_text_string(&editor.find, text, sizeof(text));

        if (editor.finding == 3)
        {
                p64 line = 0;

                for (positive at = 0; at < length && byte_is_digit((b8)text[at]); at++)
                        line = line * 10 + (p64)(text[at] - '0');
                doc_goto_line(&view->doc, line ? line - 1 : 0);
                ui_text_reveal(&editor.ui, view);
                editor.finding = 0;
                editor.find.focused = false;
                view->focused = true;
                return;
        }
        if (editor.finding == 2)
        {
                // FIND/REPLACE: the text before the first / is found, the rest replaces every match, as one undo.
                positive slash = 0, replaced;

                while (slash < length && text[slash] != '/')
                        slash++;
                if (slash < length && doc_find_set(&editor.search, text, slash, editor.find_flags))
                {
                        replaced = doc_replace_all(&view->doc, &editor.search, text + slash + 1, length - slash - 1);
                        editor.status[0] = 0;
                        positive_into_string((string_address)editor.status, replaced);
                        string_copy((string_address)editor.status + string_length((string_address)editor.status), (string_address) " replaced");
                }
                else
                        string_copy_bounded((string_address)editor.status, (string_address) "replace is FIND/REPLACEMENT", sizeof(editor.status));
                return;
        }
        editor_find_step(back);
}

/* ---- the context menu ---- */

#define EDITOR_MENU_UNDO 1
#define EDITOR_MENU_REDO 2
#define EDITOR_MENU_CUT 3
#define EDITOR_MENU_COPY 4
#define EDITOR_MENU_PASTE 5
#define EDITOR_MENU_DELETE 6
#define EDITOR_MENU_ALL 7
#define EDITOR_MENU_FIND 8
#define EDITOR_MENU_LINE 9
#define EDITOR_MENU_COMMENT 10

static fn editor_menu_open(int x, int y)
{
        struct ui_popmenu address_to m = &editor.menu.menu;
        struct doc address_to doc = &editor_now()->view.doc;
        p32 none = doc_any_selected(doc) ? 0 : UI_MENU_DISABLED;

        m->count = 0;
        ui_popmenu_add(m, "&Undo", "Ctrl+Z", doc->done ? 0 : UI_MENU_DISABLED, EDITOR_MENU_UNDO);
        ui_popmenu_add(m, "&Redo", "Ctrl+Y", doc->done < doc->group_count ? 0 : UI_MENU_DISABLED, EDITOR_MENU_REDO);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, "Cu&t", "Ctrl+X", none, EDITOR_MENU_CUT);
        ui_popmenu_add(m, "&Copy", "Ctrl+C", none, EDITOR_MENU_COPY);
        ui_popmenu_add(m, "&Paste", "Ctrl+V", 0, EDITOR_MENU_PASTE);
        ui_popmenu_add(m, "&Delete", "Delete", none, EDITOR_MENU_DELETE);
        ui_popmenu_add(m, "Select &All", "Ctrl+A", 0, EDITOR_MENU_ALL);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, "&Find", "Ctrl+F", 0, EDITOR_MENU_FIND);
        ui_popmenu_add(m, "&Go to Line", "Ctrl+G", 0, EDITOR_MENU_LINE);
        ui_popmenu_add(m, "Toggle Co&mment", "Ctrl+/", 0, EDITOR_MENU_COMMENT);
        ui_popup_open(&editor.menu, &editor.own, &editor.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false);
}

static fn editor_chose(int id)
{
        struct ui_text address_to view = &editor_now()->view;
        static const p32 commands[] = {0, DOC_CMD_UNDO, DOC_CMD_REDO, DOC_CMD_CUT, DOC_CMD_COPY, DOC_CMD_PASTE, DOC_CMD_DELETE,
                                       DOC_CMD_SELECT_ALL, 0, 0, DOC_CMD_COMMENT};

        if (id == EDITOR_MENU_FIND || id == EDITOR_MENU_LINE)
                editor_find_open(id == EDITOR_MENU_FIND ? 1 : 3);
        else if (id > 0 && id <= EDITOR_MENU_COMMENT && commands[id])
                ui_text_command(&editor.ui, view, commands[id]);
        editor.dirty = true;
}

/* ---- the window ---- */

static fn editor_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &editor.ui;
        struct editor_tab address_to tab = editor_now();
        int bar = ui_px(ui, EDITOR_BAR), find = editor.finding ? ui_px(ui, EDITOR_FIND) : 0, tabs = editor.count > 1 ? ui_px(ui, 30) : 0;
        unsigned int paper = ui_role(UI_SURFACE) & 0xffffffu;

        ui_begin(ui, pixels, editor.own.stride, editor.width, editor.height);
        ui_fill(ui, 0, 0, editor.width, editor.height, 0xff000000u | paper);
        // The tabs: each file's name, the one shown lit, a dot after a name that has changes.
        for (int at = 0, x = ui_px(ui, 8); at < editor.count && tabs; at++)
        {
                const char address_to name = editor.tabs[at].path;
                int wide;

                for (const char address_to c = editor.tabs[at].path; *c; c++)
                        if (*c == '/')
                                name = c + 1;
                wide = ui_kit_measure(ui, (string_address)(name[0] ? name : "untitled"), UI_BODY_MEDIUM) + ui_px(ui, 28);
                if (at == editor.tab)
                        ui_fill(ui, x, 0, wide, tabs, ui_tone(ui_role(UI_SURFACE_HIGH) & 0xffffffu, 255));
                ui_kit_text(ui, x + ui_px(ui, 10), ui_kit_baseline(ui, ui_px(ui, 5), UI_BODY_MEDIUM), (string_address)(name[0] ? name : "untitled"),
                            UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0);
                if (doc_dirty(&editor.tabs[at].view.doc))
                        ui_round(ui, x + wide - ui_px(ui, 12), tabs / 2 - ui_px(ui, 3), ui_px(ui, 6), ui_px(ui, 6), ui_px(ui, 3),
                                 ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 200));
                x += wide;
        }
        if (find)
        {
                struct ui_control address_to c = &editor.find_control;
                static const char address_to const hints[4] = {"", "Find", "Find/Replace with", "Line"};

                c->kind = UI_INPUT, c->variant = UI_FIELD_BARE;
                c->x = 8, c->y = (tabs + ui_px(ui, 3)) / ui->scale, c->w = editor.width / ui->scale - 16, c->h = EDITOR_FIND - 6;
                c->items[0] = hints[editor.finding];
                c->focused = editor.find.focused;
                c->edit = &editor.find;
                ui_control_draw(ui, c);
        }
        ui_text_draw(ui, &tab->view, 0, tabs + find, editor.width, editor.height - tabs - find - bar, paper);
        // The status bar.
        {
                struct doc address_to doc = &tab->view.doc;
                p64 caret = doc->selections[doc->primary].active, line = doc_line_of(doc, caret);
                char text[256];
                positive at = 0, selected = 0;
                int y = editor.height - bar;

                for (positive s = 0; s < doc->selection_count; s++)
                        selected += doc_sel_end(&doc->selections[s]) - doc_sel_start(&doc->selections[s]);
                ui_fill(ui, 0, y, editor.width, bar, ui_tone(ui_role(UI_SURFACE_CONTAINER) & 0xffffffu, 255));
                at += (positive)string_length(string_copy((string_address)text, (string_address) "Ln "));
                at += positive_into_string((string_address)text + at, (positive)(line + 1));
                at += (positive)string_length(string_copy((string_address)text + at, (string_address) ", Col "));
                at += positive_into_string((string_address)text + at, (positive)(doc_column(doc, caret) + 1));
                if (selected)
                {
                        at += (positive)string_length(string_copy((string_address)text + at, (string_address) "  ("));
                        at += positive_into_string((string_address)text + at, selected);
                        at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->selection_count > 1 ? " selected, carets)" : " selected)")));
                }
                at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->bom ? "    UTF-8 with BOM    " : "    UTF-8    ")));
                at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->eol == DOC_EOL_CRLF ? "CRLF    " : doc->eol == DOC_EOL_CR ? "CR    " : "LF    ")));
                at += (positive)string_length(string_copy((string_address)text + at, (string_address)doc_languages[doc->language].name));
                at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->insert_spaces ? "    Spaces: " : "    Tab: ")));
                at += positive_into_string((string_address)text + at, doc->tab_size);
                text[at] = 0;
                ui_kit_text(ui, ui_px(ui, 10), ui_kit_baseline(ui, y + ui_px(ui, 3), UI_LABEL_MEDIUM), (string_address)text, UI_LABEL_MEDIUM,
                            ui_role(UI_ON_SURFACE_VARIANT), 0);
                if (tab->changed_on_disk || editor.status[0])
                {
                        const char address_to say = tab->changed_on_disk ? "changed on disk: Ctrl+R reads it again" : editor.status;

                        ui_kit_text(ui, editor.width - ui_px(ui, 10) - ui_kit_measure(ui, (string_address)say, UI_LABEL_MEDIUM),
                                    ui_kit_baseline(ui, y + ui_px(ui, 3), UI_LABEL_MEDIUM), (string_address)say, UI_LABEL_MEDIUM,
                                    ui_role(tab->changed_on_disk ? UI_WARNING : UI_ON_SURFACE_VARIANT), 0);
                }
        }
}

static b32 editor_frame(void)
{
        struct windows address_to table = editor.own.table;
        unsigned int row = editor.own.row;
        int slot = editor.shown < 0 ? 0 : 1 - editor.shown;

        if (editor.shown >= 0 && !windows_ready(&editor.own))
                return false;
        editor_draw(windows_buffer(table, row, (unsigned int)slot));
        table->width[row] = (unsigned int)editor.width;
        table->height[row] = (unsigned int)editor.height;
        table->damage_width[row] = 0;
        windows_show(&editor.own, (unsigned int)slot);
        editor.shown = slot;
        editor.dirty = false;
        return true;
}

// A command the view answered the app's: the files, the find bar, the tabs.
static fn editor_command(p32 command)
{
        if (command == DOC_CMD_SAVE || command == DOC_CMD_SAVE_AS)
                editor_save(editor_now());
        else if (command == DOC_CMD_FIND || command == DOC_CMD_REPLACE || command == DOC_CMD_GOTO_LINE)
                editor_find_open(command == DOC_CMD_FIND ? 1 : command == DOC_CMD_REPLACE ? 2 : 3);
        else if (command == DOC_CMD_FIND_NEXT || command == DOC_CMD_FIND_PREVIOUS)
                editor_find_step(command == DOC_CMD_FIND_PREVIOUS);
        else if (command == DOC_CMD_NEW)
                editor_open(null);
        else if (command == DOC_CMD_CLOSE)
                editor_close_tab();
        else if (command == DOC_CMD_NEXT_TAB || command == DOC_CMD_PREVIOUS_TAB)
                editor.tab = (editor.tab + (command == DOC_CMD_NEXT_TAB ? 1 : editor.count - 1)) % editor.count;
}

static b32 editor_event(const struct window_event address_to event)
{
        struct editor_tab address_to tab = editor_now();
        p32 asks;

        if (event->type == WINDOW_EVENT_MOTION)
                editor.pointer_x = event->a, editor.pointer_y = event->b;
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && (event->flags & WINDOW_KEY_CONTROL) &&
            ((p32)event->b | 0x60) == 'r' && tab->changed_on_disk)
        {
                editor_reload();
                editor.dirty = true;
                return true;
        }
        // The find bar has the keys while it is open: Enter does what it is for, Escape closes it, the rest edit it.
        if (editor.finding && event->type == WINDOW_EVENT_KEY && editor.find.focused)
        {
                if ((event->flags & WINDOW_KEY_DOWN) && (event->a == 28 || event->a == 96))
                        editor_find_enter((event->flags & WINDOW_KEY_SHIFT) != 0);
                else if ((event->flags & WINDOW_KEY_DOWN) && event->a == 1)
                {
                        editor.finding = 0;
                        editor.find.focused = false;
                        tab->view.focused = true;
                }
                else if ((event->flags & WINDOW_KEY_DOWN) && (event->flags & WINDOW_KEY_ALT) && memory_first_of((address_any) "cwr", (b8)((p32)event->b | 0x20), 3))
                        editor.find_flags ^= ((p32)event->b | 0x20) == 'c' ? DOC_FIND_CASE : ((p32)event->b | 0x20) == 'w' ? DOC_FIND_WORD : DOC_FIND_REGEX;
                else
                        ui_text_event(&editor.ui, &editor.find, event, editor.pointer_x, editor.pointer_y);
                editor.dirty = true;
                return true;
        }
        if (event->type == WINDOW_EVENT_CLIP && editor.find.focused)
                ui_text_event(&editor.ui, &editor.find, event, 0, 0);
        else
        {
                tab->view.focused = editor.focused && !editor.find.focused;
                asks = ui_text_event(&editor.ui, &tab->view, event, editor.pointer_x, editor.pointer_y);
                if (asks & UI_TEXT_COMMAND)
                        editor_command(tab->view.command);
                if (asks & UI_TEXT_MENU)
                        editor_menu_open(tab->view.menu_x, tab->view.menu_y);
                if (asks)
                        editor.dirty = true;
                if (event->type == WINDOW_EVENT_BUTTON && editor.finding && (event->flags & WINDOW_KEY_DOWN))
                        editor.find.focused = editor.pointer_y < tab->view.top;
        }
        return true;
}

static b32 editor_focus(b32 focused)
{
        editor.focused = focused;
        editor_now()->view.focused = focused && !editor.find.focused;
        if (focused)
                for (int at = 0; at < editor.count; at++)
                        editor_check_disk(&editor.tabs[at]);
        editor.dirty = true;
        return true;
}

static fn editor_size(unsigned int width, unsigned int height)
{
        editor.width = (int)width;
        editor.height = (int)height;
        editor.dirty = true;
}

// The caret's blink and a thrown finger's scroll: the next frame they need, and none for an idle window.
static long editor_tick(long timeout, long address_to other)
{
        positive now = clock_monotonic_nanoseconds(), due;
        b32 redraw = false;
        struct ui_text address_to view = editor.find.focused ? &editor.find : &editor_now()->view;

        (void)other;
        if (editor_now()->view.kinetic.flying && ui_text_fling(&editor_now()->view, now))
                redraw = true, timeout = UI_DRAG_FRAME_NS;
        due = ui_text_tick(view, now, &redraw);
        if (redraw)
                editor.dirty = true;
        if (due && (timeout < 0 || (long)(due - now) < timeout))
                timeout = (long)(due - now);
        return timeout;
}

static int editor_start(void)
{
        struct windows address_to table = editor.own.table;
        int screen_x, screen_y, screen_w, screen_h;

        windows_output(table, 0, &screen_x, &screen_y, &screen_w, &screen_h);
        editor.width = screen_w * 3 / 5, editor.height = screen_h * 3 / 4;
        ui_centre(table, editor.own.row, editor.width, editor.height, 0);
        ui_scratch_fit(&editor.ui, (unsigned int)screen_w);
        for (positive at = 1; at < (positive)program_argument_count(); at++)
                if (!editor_open(program_argument(at)) && !editor.count)
                        break;
        if (!editor.count)
                editor_open(null);
        editor.tab = 0;
        editor.dirty = true;
        return -1;
}

static struct ui_app editor_app = {
    .own = &editor.own,
    .ui = &editor.ui,
    .missing = (string_address) "editor: no window: the compositor's desktop is not up\n",
    .title = (string_address) "Editor",
    .name = (string_address) "editor",
    .glass = 0,
    .nofont = (string_address) "editor: no font to draw with, pid ",
    .dirty = &editor.dirty,
    .focused = &editor.focused,
    .ground = &editor.ground,
    .open = editor_start,
    .frame = editor_frame,
    .event = editor_event,
    .focus = editor_focus,
    .size = editor_size,
    .tick = editor_tick,
    .popup = &editor.menu,
    .chose = editor_chose,
    .mono = true,
};

static b32 editor_main()
{
        return ui_app_run(&editor_app);
}

#endif // MOONWATER_CANVAS_EDITOR
