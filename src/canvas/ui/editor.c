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
#define EDITOR_BAR UI_PAGE_FOOT // the status bar: the kit's status line band, logical pixels

struct editor_tab
{
        struct ui_text view;
        char path[EDITOR_PATH];  // the file a save writes, a link's target already
        p8 address_to bytes;      // what was opened: a mapping or a read
        positive length, mapped;  // mapped: the mapping's length, 0 when read
        p64 size, mtime, mtime_nsec;
        unsigned int mode;
        b32 changed_on_disk;
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct editor_tab tabs[EDITOR_TABS];
        int tab, count;
        b32 dirty, focused;
        unsigned int ground;
        int width, height, shown;
        struct ui_findbar find; // the kit's find bar: its field, its mode (find, replace, go to a line) and its flags
        struct ui_control strip; // the file tabs, the kit's segmented tabs
        char status[160];
        struct ui_popup menu;
        struct ui_confirm confirm; // the kit's press again to confirm: Ctrl+W of a file with changes, Ctrl+S of one changed on disk
        b32 leaving; // the window may go: asked to, and nothing in it unsaved
} editor = {.shown = -1};

static struct editor_tab address_to editor_now(void)
{
        return &editor.tabs[editor.tab];
}

// The code view has the keys when the window has them and the find bar does not (the kit's rule, ui_findbar_keys).
static fn editor_keys(void)
{
        ui_findbar_keys(&editor.find, &editor_now()->view, editor.focused);
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
                // A file that is not there yet is a new file of that name. A link to nothing is a new file of the name it
                // holds, beside the link (a save writes that file, and the link stays a link).
                string_copy_bounded((string_address)tab->path, path, EDITOR_PATH);
                if (handle == -2)
                {
                        char named[EDITOR_PATH];
                        long named_length = (long)system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)named, sizeof(named) - 1);
                        positive directory = string_length((string_address)path);

                        // readlinkat ends nothing: the copy below reads to a NUL, which was the stack's after the name.
                        named[named_length > 0 ? named_length : 0] = 0;

                        while (directory && path[directory - 1] != '/')
                                directory--;
                        if (named_length > 0 && named[0] == '/')
                                string_copy_bounded((string_address)tab->path, (string_address)named, EDITOR_PATH);
                        else if (named_length > 0 && directory + (positive)named_length < EDITOR_PATH)
                        {
                                memory_copy(tab->path + directory, named, (positive)named_length);
                                tab->path[directory + (positive)named_length] = 0;
                        }
                }
                tab->mode = 0644;
                return handle == -2;
        }
        if (fstat((b32)handle, address_of facts) < 0 || (facts.st_mode & 0170000) != 0100000)
        {
                system_close(handle);
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_not_regular), sizeof(editor.status));
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
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_no_file_name), sizeof(editor.status));
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
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_write_beside), sizeof(editor.status));
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
        // renameat2 with no flags: riscv64 never had renameat, and its number there is nothing (a save that always failed).
        if (!ok || system_call_5(syscall(renameat2), (positive)AT_FDCWD, (positive)temporary, (positive)AT_FDCWD, (positive)tab->path, 0) < 0)
        {
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)temporary, 0);
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_save_failed), sizeof(editor.status));
                return false;
        }
        handle = system_open_at(AT_FDCWD, (string_address)tab->path, FILE_READ | O_CLOEXEC);
        if (handle >= 0 && fstat((b32)handle, address_of facts) == 0)
                tab->size = (p64)facts.st_size, tab->mtime = (p64)facts.st_mtime, tab->mtime_nsec = (p64)facts.st_mtime_nsec;
        if (handle >= 0)
                system_close(handle);
        doc_mark_saved(&tab->view.doc);
        tab->changed_on_disk = false;
        string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_saved), sizeof(editor.status));
        return true;
}

// Whether the file was changed by someone else since it was opened or saved. Without O_NONBLOCK, a FIFO that replaced the
// file would hang the window until a writer came (a FIFO's size is 0, so it is seen as changed).
static fn editor_check_disk(struct editor_tab address_to tab)
{
        bipolar handle = tab->path[0] ? system_open_at(AT_FDCWD, (string_address)tab->path, FILE_READ | O_NONBLOCK | O_NOCTTY | O_CLOEXEC) : -1;
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
        int at = editor.tab, count = editor.count;

        string_copy_bounded((string_address)path, (string_address)tab->path, EDITOR_PATH);
        ui_text_close(&tab->view);
        if (tab->mapped)
                system_call_2(syscall(munmap), (positive)tab->bytes, tab->mapped);
        editor.count--;
        memory_copy(&editor.tabs[editor.tab], &editor.tabs[editor.tab + 1], (positive)(editor.count - editor.tab) * sizeof(editor.tabs[0]));
        if (!editor_open((string_address)path))
        {
                // What is there now is no file a tab can show (a directory, a FIFO; the status says so): the tab is gone, and
                // the window keeps one it can point at, with the tab it was on or the last one.
                if (!editor.count)
                        editor_open(null);
                editor.tab = at < editor.count ? at : editor.count - 1;
                return;
        }
        // The file read again goes back to its place among the tabs, and is the one shown: it is not moved to the end.
        if (editor.count == count && at + 1 < count)
        {
                struct editor_tab again = editor.tabs[count - 1];

                memory_copy(&editor.tabs[at + 1], &editor.tabs[at], (positive)(count - 1 - at) * sizeof(editor.tabs[0]));
                editor.tabs[at] = again;
                editor.tab = at;
        }
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
        // The tab shown now has the caret when the editor has the keys: a close is not a key, so no event gives it one.
        editor_keys();
}

/*
        Ctrl+W: a file with changes is not closed by the first, which says why; the second, with no other key or press
        between them (editor_event disarms it), closes it and the changes go with it.
*/
static fn editor_close_tab_asked(void)
{
        char line[UI_CONFIRM_TEXT];

        if (!doc_dirty(&editor_now()->view.doc))
        {
                ui_confirm_drop(&editor.confirm);
                editor_close_tab();
                return;
        }
        if (!ui_confirm_ask(&editor.confirm, DOC_CMD_CLOSE, ""))
        {
                ui_confirm_line(line, sizeof(line), WORDS(editor_closes_unsaved));
                string_copy_bounded((string_address)editor.status, (string_address)line, sizeof(editor.status));
                return;
        }
        editor_close_tab();
}

/*
        Ctrl+S of a file changed on disk since it was read or saved, or of a read-only one: the first says so and writes
        nothing, so the other change is not written over; the second, with no other key or press between, writes it.
*/
static fn editor_save_asked(void)
{
        struct editor_tab address_to tab = editor_now();
        char line[UI_CONFIRM_TEXT];

        // A file with no write bit is asked for the same way: the rename that saves would replace it without a word.
        if (!tab->changed_on_disk && (tab->mode & 0222))
        {
                ui_confirm_drop(&editor.confirm);
                editor_save(tab);
                return;
        }
        if (!ui_confirm_ask(&editor.confirm, DOC_CMD_SAVE, ""))
        {
                ui_confirm_line(line, sizeof(line), tab->changed_on_disk ? WORDS(editor_writes_over_changed) : WORDS(editor_writes_over_readonly));
                string_copy_bounded((string_address)editor.status, (string_address)line, sizeof(editor.status));
                return;
        }
        editor_save(tab);
}

// The window asked to go (its close box, Alt+F4): held while a file in it has changes, which it says, and gone once none has.
static void editor_closing(void)
{
        for (int at = 0; at < editor.count; at++)
                if (doc_dirty(&editor.tabs[at].view.doc))
                {
                        string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_unsaved), sizeof(editor.status));
                        editor.dirty = true;
                        return;
                }
        editor.leaving = true;
}

static b32 editor_done(void)
{
        return editor.leaving;
}

// What the find bar said, in the editor's words for it: the status line (the kit says what happened, the app says it).
static fn editor_said(int said, positive count)
{
        if (said == UI_FINDBAR_SAID_CLEAR)
                editor.status[0] = 0;
        else if (said == UI_FINDBAR_SAID_NO_MATCH)
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_no_match), sizeof(editor.status));
        else if (said == UI_FINDBAR_SAID_BAD_PATTERN)
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_pattern_bad), sizeof(editor.status));
        else if (said == UI_FINDBAR_SAID_REPLACED)
                language_format((char address_to)editor.status, sizeof(editor.status), WORDS(editor_replaced), (long)count);
        else if (said == UI_FINDBAR_SAID_USAGE)
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_replace_usage), sizeof(editor.status));
        else if (said == UI_FINDBAR_SAID_REFUSED)
                string_copy_bounded((string_address)editor.status, (string_address)editor.find.field.refused, sizeof(editor.status));
}

// The find bar's command (find, replace, go to a line, the next match, the one before), and what it said of it.
static fn editor_bar(p32 command)
{
        positive count = 0;
        int said = ui_findbar_command(&editor.ui, &editor.own, &editor.find, &editor_now()->view, command, editor.focused, &count);

        editor_said(said, count);
}

/* ---- the context menu ---- */

// The kit's items are 1 to 7 (ui_text_menu_items); the code view's own come after them.
#define EDITOR_MENU_FIND 8
#define EDITOR_MENU_LINE 9
#define EDITOR_MENU_COMMENT 10

// The code view's menu: the kit's text menu, then find, go to a line and the comment. The find bar's is the kit's alone.
static fn editor_menu_open(int x, int y)
{
        struct ui_popmenu address_to m = &editor.menu.menu;
        struct ui_text address_to view = &editor_now()->view;

        m->count = 0;
        ui_text_menu_items(m, view, false);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, WORDS(menu_find), "Ctrl+F", 0, EDITOR_MENU_FIND);
        ui_popmenu_add(m, WORDS(menu_go_to_line), "Ctrl+G", 0, EDITOR_MENU_LINE);
        ui_popmenu_add(m, WORDS(menu_toggle_comment), "Ctrl+/", 0, EDITOR_MENU_COMMENT);
        editor.menu.field = view;
        ui_popmenu_open(&editor.menu, &editor.own, &editor.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false);
}

static fn editor_chose(int id)
{
        struct ui_text address_to view = &editor_now()->view, address_to field = editor.menu.field;

        if (id == EDITOR_MENU_FIND || id == EDITOR_MENU_LINE)
                editor_bar(id == EDITOR_MENU_FIND ? DOC_CMD_FIND : DOC_CMD_GOTO_LINE);
        else if (id == EDITOR_MENU_COMMENT)
                ui_text_command(&editor.ui, view, DOC_CMD_COMMENT);
        else if (field == &editor.find.field || field == view)
                ui_text_menu_do(&editor.ui, field, id);
        editor.dirty = true;
}

/* ---- the window ---- */

/*
        The file tabs: the kit's segmented tabs, one item a file's name (untitled for none), the file shown chosen; a dot
        after a name that has changes. The strip is the top band of the window, UI_TABS_H high.
*/
static fn editor_tabs_layout(struct ui address_to ui)
{
        struct ui_control address_to c = &editor.strip;

        if (c->kind != UI_TABS)
                c->kind = UI_TABS, c->variant = UI_TABS_SEGMENTED, ui_control_start(c);
        c->x = 0, c->y = 0, c->w = editor.width * 100 / ui->scale, c->h = UI_TABS_H;
        c->count = editor.count;
        for (int at = 0; at < editor.count; at++)
        {
                const char address_to name = editor.tabs[at].path;

                for (const char address_to s = editor.tabs[at].path; *s; s++)
                        if (*s == '/')
                                name = s + 1;
                c->items[at] = name[0] ? name : WORDS(editor_untitled);
        }
        c->at = editor.tab;
}

// The file shown is at is: the keys and the mouse choose a tab alike.
static fn editor_show_tab(int at)
{
        editor.tab = at;
        editor_keys();
        editor.dirty = true;
}

static fn editor_tabs_draw(struct ui address_to ui, int height)
{
        struct ui_control address_to c = &editor.strip;

        editor_tabs_layout(ui);
        ui_control_draw(ui, c);
        // The dot is the kit's badge dot (ui_kit_dot), its inset the kit's UI_DOT_IN from the tab's right edge.
        for (int at = 0; at < editor.count; at++)
                if (doc_dirty(&editor.tabs[at].view.doc))
                {
                        int left = ui_dev(ui, ui_tab_left(ui, c, at)), wide = ui_dev(ui, ui_tab_width(ui, c, at));

                        // After the name: at the tab's right end, or its left reading from the right.
                        ui_kit_dot(ui, ui_mirror(left + wide - ui_px(ui, UI_DOT_IN), ui_px(ui, UI_DOT), left, wide), height / 2 - ui_px(ui, UI_DOT / 2),
                                   ui_role(UI_ON_SURFACE) | 0xff000000u);
                }
}

/*
        The file tabs take the mouse as the kit's tabs do: a press on one shows it pressed, and its release on the same tab
        chooses it; a press released elsewhere chooses nothing. Only a press in the strip, and the release of one, are the
        strip's; a motion over it moves its hover. Answers true when the event was the strip's.
*/
static b32 editor_tabs_event(const struct window_event address_to event)
{
        struct ui_control address_to strip = &editor.strip;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0, band = editor.count > 1 && ui_kit.pointer_y >= 0 && ui_kit.pointer_y < ui_px(&editor.ui, UI_TABS_H);
        p32 bits;

        // The strip's own: a left press in its band (or the release of a press it holds), and a motion over it. The kit's tabs take the rest.
        if (event->type == WINDOW_EVENT_BUTTON && (event->a != UI_POINTER_LEFT || (down ? !band : !strip->down)))
                return false;
        if (event->type == WINDOW_EVENT_MOTION && !band && !strip->hover)
                return false;
        editor_tabs_layout(&editor.ui);
        bits = ui_tabs_pointer(&editor.ui, strip, event, ui_kit.pointer_x, ui_kit.pointer_y);
        if ((bits & UI_TABS_CHOSEN) && strip->at != editor.tab)
                editor_show_tab(strip->at);
        editor.dirty = true;
        return event->type == WINDOW_EVENT_BUTTON;
}

/*
        The status bar's text: where the caret is, what is selected, and the file's encoding, line ends, language and indent,
        each said by the table and set apart by four blanks. Here and not in editor_draw so a check can ask it in any language.
*/
static positive editor_status_text(char address_to text, positive room, struct doc address_to doc)
{
        p64 caret = doc->selections[doc->primary].active, line = doc_line_of(doc, caret);
        positive at = 0, selected = 0;

        for (positive s = 0; s < doc->selection_count; s++)
                selected += doc_sel_end(&doc->selections[s]) - doc_sel_start(&doc->selections[s]);
        at += language_format(text, room, WORDS(editor_position), (long)(line + 1), (long)(doc_column(doc, caret) + 1));
        if (selected)
        {
                at += (positive)string_length(string_copy((string_address)text + at, (string_address) "  "));
                at += language_format(text + at, room - at, doc->selection_count > 1 ? WORDS(editor_selected_carets) : WORDS(editor_selected), (long)selected);
        }
        at += (positive)string_length(string_copy((string_address)text + at, (string_address) "    "));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->bom ? WORDS(editor_encoding_bom) : "UTF-8")));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address) "    "));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->eol == DOC_EOL_CRLF ? "CRLF" : doc->eol == DOC_EOL_CR ? "CR" : "LF")));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address) "    "));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)language_of(doc_languages[doc->language].name)));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address) "    "));
        at += language_format(text + at, room - at, doc->insert_spaces ? WORDS(editor_indent_spaces) : WORDS(editor_indent_tab), (long)doc->tab_size);
        text[at] = 0;
        return at;
}

static fn editor_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &editor.ui;
        struct editor_tab address_to tab = editor_now();
        int bar = ui_px(ui, EDITOR_BAR), find = ui_findbar_band(ui, &editor.find), tabs = editor.count > 1 ? ui_px(ui, UI_TABS_H) : 0;
        unsigned int paper = ui_role(UI_SURFACE) & 0xffffffu;

        // ui_begin clears the window to the kit's surface (the editor is an opaque app), so the ground is not painted again here.
        ui_begin(ui, pixels, editor.own.stride, editor.width, editor.height);
        if (tabs)
                editor_tabs_draw(ui, tabs);
        if (find)
        {
                const char address_to hints[4] = {"", WORDS(editor_find_hint), WORDS(editor_find_replace_hint), WORDS(editor_line_hint)};

                ui_findbar_draw(ui, &editor.find, tabs, editor.width, hints[editor.find.mode]);
        }
        ui_text_draw(ui, &tab->view, 0, tabs + find, editor.width, editor.height - tabs - find - bar, paper);
        // The status bar.
        {
                struct doc address_to doc = &tab->view.doc;
                char text[256];
                int y = editor.height - bar;

                ui_fill(ui, 0, y, editor.width, bar, ui_role(UI_SURFACE_CONTAINER) | 0xff000000u);
                editor_status_text(text, sizeof(text), doc);
                ui_kit_text_flipped(ui, ui_px(ui, UI_INSET), ui_kit_baseline(ui, y + (bar - ui_px(ui, ui_types[UI_LABEL_MEDIUM].line)) / 2, UI_LABEL_MEDIUM),
                                    (string_address)text, UI_LABEL_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0, 0, editor.width);
                if (tab->changed_on_disk || editor.status[0])
                {
                        const char address_to say = tab->changed_on_disk ? WORDS(editor_changed_on_disk) : editor.status;

                        ui_kit_text_flipped(ui, editor.width - ui_px(ui, UI_INSET) - ui_kit_measure(ui, (string_address)say, UI_LABEL_MEDIUM),
                                            ui_kit_baseline(ui, y + (bar - ui_px(ui, ui_types[UI_LABEL_MEDIUM].line)) / 2, UI_LABEL_MEDIUM), (string_address)say, UI_LABEL_MEDIUM,
                                            ui_role(tab->changed_on_disk ? UI_WARNING : UI_ON_SURFACE_VARIANT), 0, 0, editor.width);
                }
        }
        // A finger's selection: its handles and actions bar, over everything, the find bar's and then the code view's.
        ui_findbar_overlay(ui, &editor.find);
        ui_text_overlay(ui, &tab->view);
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
                editor_save_asked();
        else if (ui_findbar_is_command(command))
                editor_bar(command);
        else if (command == DOC_CMD_NEW)
                editor_open(null);
        else if (command == DOC_CMD_CLOSE)
                editor_close_tab_asked();
        else if (command == DOC_CMD_NEXT_TAB || command == DOC_CMD_PREVIOUS_TAB)
                editor.tab = (editor.tab + (command == DOC_CMD_NEXT_TAB ? 1 : editor.count - 1)) % editor.count;
}

static b32 editor_event(const struct window_event address_to event)
{
        struct editor_tab address_to tab = editor_now();
        p32 asks;

        // A key or a press other than the one that asked is a change of mind: the next such key asks again.
        if ((event->flags & WINDOW_KEY_DOWN) &&
            (event->type == WINDOW_EVENT_BUTTON || (event->type == WINDOW_EVENT_KEY && ui_key_command(event) != editor.confirm.what)))
                ui_confirm_drop(&editor.confirm);
        if (editor_tabs_event(event))
                return true;
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && (event->flags & WINDOW_KEY_CONTROL) &&
            ((p32)event->b | 0x60) == 'r' && tab->changed_on_disk)
        {
                editor_reload();
                editor.dirty = true;
                return true;
        }
        /*
                The find bar takes the keys while it is open and has them, and the pointer's presses in its band; what it does is the kit's
                (ui_findbar_event), and what it asks of the app is a command (save, the files, the tabs) or what it said.
        */
        {
                struct ui_findbar_said said = ui_findbar_event(&editor.ui, &editor.find, &tab->view, &editor.menu, &editor.own, event, editor.focused);

                if (said.said)
                        editor_said(said.said, said.count);
                if (said.command)
                        editor_command(said.command);
                if (said.took || said.said || said.command)
                        editor.dirty = true;
                if (said.took)
                        return true;
        }
        tab = editor_now();
        editor_keys();
        asks = ui_text_send(&editor.ui, &tab->view, event);
        if (asks & UI_TEXT_COMMAND)
                editor_command(tab->view.command);
        if (asks & UI_TEXT_MENU)
                editor_menu_open(tab->view.menu_x, tab->view.menu_y);
        if (asks)
                editor.dirty = true;
        return true;
}

static b32 editor_focus(b32 focused)
{
        editor.focused = focused;
        editor_keys();
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
        positive now = clock_monotonic_nanoseconds();
        b32 redraw = false;

        (void)other;
        // The code view's frames and the find bar's (ui_findbar_timeout: its throw, its blink, its drag).
        timeout = ui_findbar_timeout(&editor.ui, &editor.find, &editor_now()->view, timeout, now, &redraw);
        if (redraw)
                editor.dirty = true;
        return timeout;
}

// The status line says what happened in the language of that moment, so a change of language ends it.
static fn editor_language(void)
{
        editor.status[0] = 0;
        editor.dirty = true;
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
        language_listen(editor_language);
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
    .closing = editor_closing,
    .done = editor_done,
    .mono = true,
};

static b32 editor_main()
{
        return ui_app_run(&editor_app);
}

#endif // MOONWATER_CANVAS_EDITOR
