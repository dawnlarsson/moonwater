/*
        The editor: files in the code view of the text engine, the proof that the kit carries a code editor.

            editor [FILE]...

        Each file is a tab (Ctrl+Tab and Ctrl+Shift+Tab go round them, Ctrl+N a new one, Ctrl+W closes one). A file of
        8 MiB or more is mapped, not read: the engine's pieces point into the mapping and only an edit is ever copied,
        so a hundred megabytes open at once. What opens is a regular file only (a FIFO, a device or a socket is refused,
        and /proc's files, which say nothing of their size, are read up to 64 MiB); the path a link names is
        resolved when the file opens, and a save goes to that file, so the link stays a link, in the directory the file was
        opened in (held open, so a directory of the path swapped for a link later is not followed).

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

struct editor_tab
{
        struct ui_text view;
        char path[EDITOR_PATH];  // the file a save writes, a link's target already
        p8 address_to bytes;      // what was opened: a mapping or a read
        positive length, mapped;  // mapped: the mapping's length, 0 when read
        p64 size, mtime, mtime_nsec;
        unsigned int mode;
        p32 owner, group;         // the file's, which a save keeps; -1 for a file not there yet
        int folder;               // the directory the file was opened in (O_PATH), which a save writes into; -1 for none
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
        struct ui_view view;     // the window as a view (editor_layout): the bands, the code view's box and the status bar
        struct ui_rect tabs_box, find_box, text_box; // the device pixels the view gave them
        char bar_text[256];      // the status bar's line at the left, which the view measures
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

/*
        A mapped file cut short under the editor (another program truncating it, a log rotated by copytruncate): the
        pages past its new end are gone from every mapping of it, and the first read of one was SIGBUS, which ended the
        editor and every tab's unsaved changes with it. The handler maps zeros over the rest of the mapping the read was
        in and the read goes on: that tab shows NULs where the file was cut and says it changed on disk. A fault in no
        tab's mapping goes to the handler there was before (the default: the program ends, as it did).
*/
static signal_action editor_bus_was;

static void editor_bus(int number, void address_to info, void address_to context)
{
        positive where = *(positive address_to)((p8 address_to)info + 16);

        (void)context;
        // The tab being opened is the one past the count: its document reads the mapping before it is counted.
        for (int at = 0; at <= editor.count && at < EDITOR_TABS; at++)
        {
                struct editor_tab address_to tab = &editor.tabs[at];

                if (tab->mapped && where - (positive)tab->bytes < tab->mapped)
                {
                        positive page = where & ~(positive)4095, past = ((positive)tab->bytes + tab->mapped + 4095) & ~(positive)4095;

                        // PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED
                        system_call_6(syscall(mmap), page, past - page, 1, 0x32, (positive)-1, 0);
                        tab->changed_on_disk = true;
                        return;
                }
        }
        signal_action_change(number, &editor_bus_was, null);
}

// The handler in place, once: SA_SIGINFO for the address, SA_NODEFER so a second cut tab is caught while in it.
static fn editor_bus_catch(void)
{
        static b32 caught;
        signal_action wanted;

        if (caught)
                return;
        memory_zero(&wanted, sizeof(wanted));
        wanted.handler = (signal_handler)(address_any)editor_bus;
        wanted.flags = SA_SIGINFO | SA_NODEFER;
        caught = signal_action_change(7, &wanted, &editor_bus_was) == 0; // SIGBUS
}

/*
        The directory path's file is in, held from the open so that a save writes there and only there: a save by path
        went wherever the path led at that moment, so a directory of the path swapped for a link after the open (to
        /etc, by its owner) had the save write through it. With facts, the name in the directory must still be that
        file. -1 when it cannot be held.
*/
static int editor_folder(const char address_to path, const error_stat address_to facts)
{
        char directory[EDITOR_PATH];
        positive slash = string_length((string_address)path);
        bipolar folder;
        error_stat there;

        while (slash && path[slash - 1] != '/')
                slash--;
        if (!path[slash] || slash >= EDITOR_PATH)
                return -1;
        memory_copy(directory, slash ? path : ".", slash ? slash : 1);
        directory[slash ? slash : 1] = 0;
        folder = system_open_at(AT_FDCWD, (string_address)directory, O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (folder < 0)
                return -1;
        if (facts && (fstatat((b32)folder, (string_address)path + slash, address_of there, AT_SYMLINK_NOFOLLOW) != 0 ||
                      there.st_dev != facts->st_dev || there.st_ino != facts->st_ino))
        {
                system_close(folder);
                return -1;
        }
        return (int)folder;
}

// A file's bytes: mapped when large, read when small, refused when it is not a regular file. False with a status.
static b32 editor_load(struct editor_tab address_to tab, string_address path)
{
        // The path pinned before what it names is opened: opening a device to look at it can act on it (a watchdog is
        // armed by its open, a tape rewinds at its close, a FIFO's waiting writer is let go), and a link named like a
        // document can name one. Only a regular file is then opened to be read, through the pin, so it is the file
        // looked at; without blocking, as a FIFO put there in between would wait for a writer.
        bipolar handle = system_open_at(AT_FDCWD, path, O_PATH | O_CLOEXEC), pinned;
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
                tab->owner = tab->group = (p32)-1;
                if (handle == -2)
                        tab->folder = editor_folder(tab->path, null);
                // Any other error (a directory not searchable, a path refused) opens nothing, and the status says so.
                else
                        string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_read_failed), sizeof(editor.status));
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
        tab->owner = facts.st_uid, tab->group = facts.st_gid;
        // A path past the room was cut short by readlinkat: a save under the cut name would write another file.
        tab->folder = got >= (bipolar)sizeof(resolved) - 1 ? -1 : editor_folder(tab->path, &facts);
        pinned = handle;
        handle = system_open_at(AT_FDCWD, (string_address)link, FILE_READ | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
        system_close(pinned);
        if (handle < 0)
        {
                if (tab->folder >= 0)
                        system_close(tab->folder);
                tab->folder = -1;
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_read_failed), sizeof(editor.status));
                return false;
        }
        /*
                A file that cannot be mapped is read instead, and a read that fails is refused: a buffer cut short by an
                error would be the file's end to the editor, and a save would write that prefix over the whole file.
        */
        if (facts.st_size >= EDITOR_MAP_FROM)
        {
                long mapped = (long)system_call_6(syscall(mmap), 0, (positive)facts.st_size, 1, 2, (positive)handle, 0);

                if (!(mapped < 0 && mapped > -4096))
                {
                        tab->bytes = (p8 address_to)mapped, tab->length = tab->mapped = (positive)facts.st_size;
                        editor_bus_catch();
                        system_close(handle);
                        return true;
                }
        }
        {
                positive room = 0;
                b32 failed = false;

                while (tab->length < EDITOR_READ_MOST)
                {
                        if (!array_store_reserve(tab->bytes, room, tab->length, tab->length + 65536, 65536))
                        {
                                failed = true;
                                break;
                        }
                        got = system_read_retry((positive)handle, tab->bytes + tab->length, room - tab->length);
                        if (got < 0)
                        {
                                failed = true;
                                break;
                        }
                        if (got == 0)
                                break;
                        tab->length += (positive)got;
                }
                system_close(handle);
                if (failed)
                {
                        string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_read_failed), sizeof(editor.status));
                        if (tab->folder >= 0)
                                system_close(tab->folder);
                        tab->folder = -1;
                        return false;
                }
        }
        return true;
}

static b32 editor_open(string_address path)
{
        struct editor_tab address_to tab;
        string_address name = path;

        if (editor.count == EDITOR_TABS)
        {
                language_format((char address_to)editor.status, sizeof(editor.status), WORDS(editor_tabs_full), (long)EDITOR_TABS);
                return false;
        }
        tab = &editor.tabs[editor.count];
        memory_zero(tab, sizeof(*tab));
        tab->folder = -1;
        if (path && !editor_load(tab, path))
                return false;
        if (!ui_text_open(&tab->view, UI_TEXT_CODE, tab->bytes ? tab->bytes : (const p8 address_to) "", tab->length, ui_text_keymap()))
        {
                if (tab->folder >= 0)
                        system_close(tab->folder);
                return false;
        }
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
        // Beside the file in the directory it was opened in, by name in that directory: no path is walked again.
        string_copy((string_address)temporary, (string_address) ".");
        string_copy_bounded((string_address)temporary + 1, (string_address)tab->path + slash, EDITOR_PATH - 1);
        string_copy((string_address)temporary + string_length((string_address)temporary), (string_address) ".editing");
        handle = tab->folder < 0 ? -1
                                 : system_open_at_mode(tab->folder, (string_address)temporary,
                                                       O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0)
        {
                // An earlier save's lock, left when it died between its open and its rename: every save of this file is refused
                // until it goes, and the status names it so the person can remove it.
                if (handle == -EEXIST)
                        language_format((char address_to)editor.status, sizeof(editor.status), WORDS(editor_lock_stale), (const char address_to)temporary);
                else
                        string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_write_beside), sizeof(editor.status));
                return false;
        }
        /*
                system_write_all returns the bytes accepted, which is never negative, so a short write (a full disk, an
                I/O error) is told by its count and its error: a save of the prefix would replace the file with less.
        */
        if (tab->view.doc.bom)
        {
                system_write_result wrote = system_write_all_checked((b32)handle, "\xef\xbb\xbf", 3);

                ok &= wrote.error == 0 && wrote.bytes == 3;
        }
        while (ok && at < doc_length(&tab->view.doc))
        {
                positive got = doc_read(&tab->view.doc, at, chunk, sizeof(chunk));
                system_write_result wrote = system_write_all_checked((b32)handle, chunk, got);

                ok &= got > 0 && wrote.error == 0 && wrote.bytes == got;
                at += got;
        }
        /*
                The file's own owner and mode, which the new file is given before it takes the name: one of mode 0 was
                written 0644 and readable by everyone (a shadow file), and one root saved for another user became root's,
                its set-id bits kept, so a set-user-id program of that user's was root's own once saved. A file whose owner
                cannot be kept (a save by someone else) loses those bits. Written as the owner alone could read it.
        */
        {
                unsigned int mode = tab->mode;
                error_stat made;

                if (fstat((b32)handle, address_of made) == 0 &&
                    ((tab->owner != (p32)-1 && made.st_uid != tab->owner) || (tab->group != (p32)-1 && made.st_gid != tab->group)) &&
                    system_call_3(syscall(fchown), (positive)handle, (positive)tab->owner, (positive)tab->group) != 0)
                        mode &= ~06000u;
                system_call_2(syscall(fchmod), (positive)handle, (positive)mode);
        }
        ok &= system_call_1(syscall(fsync), (positive)handle) == 0;
        ok &= fstat((b32)handle, address_of facts) == 0;
        system_close(handle);
        // renameat2 with no flags: riscv64 never had renameat, and its number there is nothing (a save that always failed).
        if (!ok || system_call_5(syscall(renameat2), (positive)tab->folder, (positive)temporary, (positive)tab->folder,
                                 (positive)tab->path + slash, 0) < 0)
        {
                system_call_3(syscall(unlinkat), (positive)tab->folder, (positive)temporary, 0);
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_save_failed), sizeof(editor.status));
                return false;
        }
        tab->size = (p64)facts.st_size, tab->mtime = (p64)facts.st_mtime, tab->mtime_nsec = (p64)facts.st_mtime_nsec;
        doc_mark_saved(&tab->view.doc);
        tab->changed_on_disk = false;
        string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_saved), sizeof(editor.status));
        return true;
}

// Whether the file was changed by someone else since it was opened or saved. Looked at through O_PATH, which opens
// nothing: a FIFO that replaced the file would hang a reading open, and a device put there acts when it is opened.
static fn editor_check_disk(struct editor_tab address_to tab)
{
        bipolar handle = tab->path[0] ? system_open_at(AT_FDCWD, (string_address)tab->path, O_PATH | O_CLOEXEC) : -1;
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
        if (tab->folder >= 0)
                system_close(tab->folder);
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
        if (tab->folder >= 0)
                system_close(tab->folder);
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

        if (ui_confirm_gate(&editor.confirm, DOC_CMD_CLOSE, doc_dirty(&editor_now()->view.doc), WORDS(editor_closes_unsaved), line, sizeof(line)))
                editor_close_tab();
        else
                string_copy_bounded((string_address)editor.status, (string_address)line, sizeof(editor.status));
}

/*
        Ctrl+S of a file changed on disk since it was read or saved, or of a read-only one: the first says so and writes
        nothing, so the other change is not written over; the second, with no other key or press between, writes it.
*/
static fn editor_save_asked(void)
{
        struct editor_tab address_to tab = editor_now();
        char line[UI_CONFIRM_TEXT];

        // The disk is looked at now, not only when the window last took focus: a program that wrote the file while the
        // window kept it would otherwise be written over without the question.
        editor_check_disk(tab);
        // A file with no write bit is asked for the same way: the rename that saves would replace it without a word.
        if (ui_confirm_gate(&editor.confirm, DOC_CMD_SAVE, tab->changed_on_disk || !(tab->mode & 0222),
                            tab->changed_on_disk ? WORDS(editor_writes_over_changed) : WORDS(editor_writes_over_readonly), line, sizeof(line)))
                editor_save(tab);
        else
                string_copy_bounded((string_address)editor.status, (string_address)line, sizeof(editor.status));
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
// A tab's name: the file's last part, untitled for none (the kit's tabs take it as the item's name).
static const char address_to editor_tab_name(int at)
{
        const char address_to name = editor.tabs[at].path;

        for (const char address_to s = editor.tabs[at].path; *s; s++)
                if (*s == '/')
                        name = s + 1;
        return name[0] ? name : WORDS(editor_untitled);
}

static b32 editor_tab_dirty(int at)
{
        return doc_dirty(&editor.tabs[at].view.doc);
}

static fn editor_tabs_layout(struct ui address_to ui)
{
        struct ui_control address_to c = &editor.strip;
        struct ui_rect address_to box = &editor.tabs_box;

        if (c->kind != UI_TABS)
                c->kind = UI_TABS, c->variant = UI_TABS_SEGMENTED, ui_control_start(c);
        c->x = box->x * 100 / ui->scale, c->y = box->y * 100 / ui->scale, c->w = box->w * 100 / ui->scale, c->h = UI_TABS_H;
        ui_tabs_fill(c, editor.count, editor.tab, editor_tab_name, editor_tab_dirty);
}

// The file shown is at is: the keys and the mouse choose a tab alike.
static fn editor_show_tab(int at)
{
        editor.tab = at;
        editor_keys();
        editor.dirty = true;
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

/*
        The window as a view, laid out each draw and event: the tabs' band (none with one file), the find bar's (none while closed),
        the code view below them, and the status bar at the foot. Its words are made first, so the view measures them.
*/
static fn editor_layout(void)
{
        struct editor_tab address_to tab = editor_now();

        editor_status_text(editor.bar_text, sizeof(editor.bar_text), &tab->view.doc);
        ui_view_begin(&editor.view, &editor.ui, editor.width, -editor.height);
        ui_host(&editor.tabs_box, .h = UI_TABS_H, .hidden = editor.count <= 1);
        ui_host(&editor.find_box, .h = UI_FINDBAR_H, .hidden = editor.find.mode == UI_FINDBAR_CLOSED);
        ui_host(&editor.text_box, .grow = 1);
        ui_row(.class = "bar")
        {
                ui_text(editor.bar_text, .class = "status");
                ui_host(null, .grow = 1);
                if (tab->changed_on_disk)
                        ui_text(WORDS(editor_changed_on_disk), .class = "warning");
                if (editor.status[0])
                        ui_text(editor.status, .class = "status");
        }
        ui_view_end(&editor.view);
}

static fn editor_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &editor.ui;
        struct editor_tab address_to tab = editor_now();
        unsigned int paper = ui_role(UI_SURFACE) & 0xffffffu;

        // ui_begin clears the window to the kit's surface (the editor is an opaque app), so the ground is not painted again here.
        ui_begin(ui, pixels, editor.own.stride, editor.width, editor.height);
        editor_layout();
        if (editor.count > 1)
        {
                editor_tabs_layout(ui);
                ui_control_draw(ui, &editor.strip);
        }
        if (editor.find.mode != UI_FINDBAR_CLOSED)
        {
                const char address_to hints[4] = {"", WORDS(editor_find_hint), WORDS(editor_find_replace_hint), WORDS(editor_line_hint)};

                ui_findbar_draw(ui, &editor.find, editor.find_box.y, editor.width, hints[editor.find.mode]);
        }
        ui_text_draw(ui, &tab->view, editor.text_box.x, editor.text_box.y, editor.text_box.w, editor.text_box.h, paper);
        ui_view_draw(ui, &editor.view, false);
        // A finger's selection: its handles and actions bar, over everything, the find bar's and then the code view's.
        ui_findbar_overlay(ui, &editor.find);
        ui_text_overlay(ui, &tab->view);
}

static b32 editor_frame(void)
{
        return ui_app_present(&editor.own, &editor.shown, &editor.dirty, editor.width, editor.height, editor_draw);
}

// A command the view answered the app's: the files, the find bar, the tabs.
static fn editor_command(p32 command)
{
        // Save As has no name to ask for yet: it would write the file in place, so it says so and writes nothing.
        if (command == DOC_CMD_SAVE)
                editor_save_asked();
        else if (command == DOC_CMD_SAVE_AS)
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_save_as_none), sizeof(editor.status));
        else if (ui_findbar_is_command(command))
                editor_bar(command);
        else if (command == DOC_CMD_NEW)
                editor_open(null);
        // Opening a file is by the command line only (editor FILE): there is no chooser yet, so the key says so.
        else if (command == DOC_CMD_OPEN)
                string_copy_bounded((string_address)editor.status, (string_address)WORDS(editor_open_none), sizeof(editor.status));
        else if (command == DOC_CMD_CLOSE)
                editor_close_tab_asked();
        else if (command == DOC_CMD_NEXT_TAB || command == DOC_CMD_PREVIOUS_TAB)
                editor.tab = (editor.tab + (command == DOC_CMD_NEXT_TAB ? 1 : editor.count - 1)) % editor.count;
}

/*
        Ctrl+R, which reads a file changed on disk again. It is no key command, so the confirm names it past every command.
*/
#define EDITOR_RELOAD DOC_CMDS

static b32 editor_reload_key(const struct window_event address_to event)
{
        return event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_CONTROL) && ((p32)event->b | 0x60) == 'r';
}

static b32 editor_event(const struct window_event address_to event)
{
        struct editor_tab address_to tab = editor_now();
        p32 asks;

        // A key or a press other than the one that asked is a change of mind: the next such key asks again. A modifier alone is
        // no key of its own (Ctrl+W, let go, Ctrl+W again is the second press), so its press leaves the confirm armed.
        if ((event->flags & WINDOW_KEY_DOWN) &&
            (event->type == WINDOW_EVENT_BUTTON ||
             (event->type == WINDOW_EVENT_KEY && !ui_key_modifier(event->a) &&
              (editor_reload_key(event) ? EDITOR_RELOAD : ui_key_command(event)) != editor.confirm.what)))
                ui_confirm_drop(&editor.confirm);
        editor_layout();
        if (editor_tabs_event(event))
                return true;
        // What was typed since the file was read goes with the read: over unsaved changes the first press says so and the second reads.
        if (editor_reload_key(event) && (event->flags & WINDOW_KEY_DOWN) && tab->changed_on_disk)
        {
                char line[UI_CONFIRM_TEXT];

                if (ui_confirm_gate(&editor.confirm, EDITOR_RELOAD, doc_dirty(&tab->view.doc), WORDS(editor_reload_unsaved), line, sizeof(line)))
                        editor_reload();
                else
                        string_copy_bounded((string_address)editor.status, (string_address)line, sizeof(editor.status));
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
        asks = ui_text_event(&editor.ui, &tab->view, event, ui_kit.pointer_x, ui_kit.pointer_y);
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
        positive now = (positive)ui_kit.now;
        b32 redraw = false;

        (void)other;
        // The code view's frames and the find bar's (ui_findbar_timeout: its throw, its blink, its drag).
        timeout = ui_findbar_timeout(&editor.ui, &editor.find, &editor_now()->view, timeout, &redraw);
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
        // A pinch on the text zooms it (ui_text_pinch): the window takes the pinch, and is not sent it as a wheel with Ctrl.
        __atomic_store_n(&((const struct window_row *)(&editor.own))->table->pinch_wanted[((const struct window_row *)(&editor.own))->row], true ? 1u : 0u, __ATOMIC_RELEASE);
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
    .language = editor_language,
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
