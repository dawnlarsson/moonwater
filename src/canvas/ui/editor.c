/*
        The editor: multi-tab code and hex editor window on the compositor.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_EDITOR
#define MOONWATER_CANVAS_EDITOR

#define EDITOR_TABS 16
#define EDITOR_MAP_FROM (8u << 20)
#define EDITOR_READ_MOST (64u << 20)
#define EDITOR_PATH 512

struct editor_tab
{
        struct ui_text view;
        char path[EDITOR_PATH];
        p8 address_to bytes;
        positive length, mapped, room;
        p64 size, mtime, mtime_nsec;
        unsigned int mode;
        p32 owner, group;
        int folder;
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
        struct ui_findbar find;
        struct ui_control strip, wrap_btn, hex_btn;
        struct ui_view view;
        struct ui_rect find_box, text_box;
        int pill_box[4], pill_wrap[4], pill_text[4], pill_hex[4], pill_hover;
        b32 pill_down;
        char bar_text[256], status[160];
        struct ui_popup menu;
        int tab_menu_at;
        struct ui_confirm confirm;
        struct ui_drop drop;
        b32 leaving;
        string_address start_files[EDITOR_TABS];
        int start_count;
} editor;

static struct editor_tab address_to editor_now(void)
{
        return &editor.tabs[editor.tab];
}

static fn editor_keys(void)
{
        ui_findbar_keys(&editor.find, &editor_now()->view, editor.focused);
}

static fn editor_status_set(const char address_to text)
{
        string_copy_bounded((string_address)editor.status, (string_address)text, sizeof(editor.status));
}

static positive editor_dir_len(const char address_to path)
{
        positive slash = string_length((string_address)path);

        while (slash && path[slash - 1] != '/')
                slash--;
        return slash;
}

static const char address_to editor_basename(const char address_to path)
{
        return path ? path + editor_dir_len(path) : null;
}

// SIGBUS handler for mapped files truncated underneath: maps anonymous zeros over the remainder.
static signal_action editor_bus_was;

static void editor_bus(int number, void address_to info, void address_to context)
{
        positive where = *(positive address_to)((p8 address_to)info + 16);

        (void)context;
        for (int at = 0; at <= editor.count && at < EDITOR_TABS; at++)
        {
                struct editor_tab address_to tab = &editor.tabs[at];

                if (tab->mapped && where - (positive)tab->bytes < tab->mapped)
                {
                        positive page = where & ~(positive)4095, past = ((positive)tab->bytes + tab->mapped + 4095) & ~(positive)4095;

                        system_call_6(syscall(mmap), page, past - page, 1, 0x32, (positive)-1, 0);
                        tab->changed_on_disk = true;
                        return;
                }
        }
        signal_action_change(number, &editor_bus_was, null);
}

static fn editor_bus_catch(void)
{
        static b32 caught;
        signal_action wanted;

        if (caught)
                return;
        memory_zero(&wanted, sizeof(wanted));
        wanted.handler = (signal_handler)(address_any)editor_bus;
        wanted.flags = SA_SIGINFO | SA_NODEFER;
        caught = signal_action_change(7, &wanted, &editor_bus_was) == 0;
}

// Pins path's parent directory via O_PATH so saves never follow a later directory swap.
static int editor_folder(const char address_to path, const error_stat address_to facts)
{
        char directory[EDITOR_PATH];
        positive slash = editor_dir_len(path);
        bipolar folder;
        error_stat there;

        if (!path[slash] || slash >= EDITOR_PATH)
                return -1;
        memory_copy(directory, slash ? path : ".", slash ? slash : 1);
        directory[slash ? slash : 1] = 0;
        if ((folder = system_open_at(AT_FDCWD, (string_address)directory, O_PATH | O_DIRECTORY | O_CLOEXEC)) < 0)
                return -1;
        if (facts && (fstatat((b32)folder, (string_address)path + slash, address_of there, AT_SYMLINK_NOFOLLOW) != 0 ||
                      there.st_dev != facts->st_dev || there.st_ino != facts->st_ino))
        {
                system_close(folder);
                return -1;
        }
        return (int)folder;
}

static fn editor_tab_release(struct editor_tab address_to tab, b32 close_view)
{
        if (close_view)
                ui_text_close(&tab->view);
        if (tab->mapped)
                system_call_2(syscall(munmap), (positive)tab->bytes, tab->mapped);
        else if (tab->bytes)
                array_store_release(tab->bytes, tab->room, tab->length);
        if (tab->folder >= 0)
                system_close(tab->folder);
        tab->bytes = null, tab->mapped = tab->room = 0, tab->folder = -1;
}

static fn editor_tab_language(struct editor_tab address_to tab, const char address_to path, const p8 address_to bytes, positive length)
{
        tab->view.doc.language = doc_language_of(editor_basename(path), bytes, length < 256 ? length : 256);
        tab->view.comment = doc_languages[tab->view.doc.language].comment;
}

// Loads a regular file safely via O_PATH pinning (mmap >= 8 MiB, read up to 64 MiB).
static b32 editor_load(struct editor_tab address_to tab, string_address path)
{
        bipolar handle = system_open_at(AT_FDCWD, path, O_PATH | O_CLOEXEC), pinned, got;
        error_stat facts;
        char link[64], resolved[EDITOR_PATH];

        tab->bytes = null, tab->length = tab->mapped = 0;
        if (handle < 0)
        {
                string_copy_bounded((string_address)tab->path, path, EDITOR_PATH);
                if (handle == -2)
                {
                        char named[EDITOR_PATH];
                        long named_length = (long)system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)named, sizeof(named) - 1);
                        positive directory = editor_dir_len((const char address_to)path);

                        named[named_length > 0 ? named_length : 0] = 0;
                        if (string_length(path) >= EDITOR_PATH || named_length >= (long)sizeof(named) - 1 ||
                            (named_length > 0 && (named[0] == '/' ? named_length : (long)directory + named_length) >= EDITOR_PATH))
                                return editor_status_set(WORDS(editor_read_failed)), false;
                        if (named_length > 0 && named[0] == '/')
                                string_copy_bounded((string_address)tab->path, (string_address)named, EDITOR_PATH);
                        else if (named_length > 0 && directory + (positive)named_length < EDITOR_PATH)
                                memory_copy(tab->path + directory, named, (positive)named_length), tab->path[directory + (positive)named_length] = 0;
                        tab->mode = 0644, tab->owner = tab->group = (p32)-1;
                        tab->folder = editor_folder(tab->path, null);
                        return true;
                }
                return editor_status_set(WORDS(editor_read_failed)), false;
        }
        if (fstat((b32)handle, address_of facts) < 0 || (facts.st_mode & 0170000) != 0100000)
                return system_close(handle), editor_status_set(WORDS(editor_not_regular)), false;
        string_copy((string_address)link, (string_address) "/proc/self/fd/");
        link[14 + positive_into_string((string_address)link + 14, (positive)handle)] = 0;
        got = system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)link, (positive)resolved, sizeof(resolved) - 1);
        resolved[got > 0 ? got : 0] = 0;
        string_copy_bounded((string_address)tab->path, got > 0 ? (string_address)resolved : path, EDITOR_PATH);
        tab->size = (p64)facts.st_size, tab->mtime = (p64)facts.st_mtime, tab->mtime_nsec = (p64)facts.st_mtime_nsec;
        tab->mode = facts.st_mode & 07777, tab->owner = facts.st_uid, tab->group = facts.st_gid;
        tab->folder = got >= (bipolar)sizeof(resolved) - 1 ? -1 : editor_folder(tab->path, &facts);
        pinned = handle;
        handle = system_open_at(AT_FDCWD, (string_address)link, FILE_READ | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
        system_close(pinned);
        if (handle < 0)
                return editor_tab_release(tab, false), editor_status_set(WORDS(editor_read_failed)), false;
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
                b32 failed = (p64)facts.st_size > EDITOR_READ_MOST;

                while (!failed && tab->length < EDITOR_READ_MOST)
                {
                        if (!array_store_reserve(tab->bytes, tab->room, tab->length, tab->length + 65536, 65536) ||
                            (got = system_read_retry((positive)handle, tab->bytes + tab->length, tab->room - tab->length)) < 0)
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
                        return editor_status_set(WORDS(editor_read_failed)), editor_tab_release(tab, false), false;
        }
        return true;
}

static struct ui_app editor_app;

static fn editor_title_sync(void)
{
        struct windows address_to table = editor.own.table;
        char title[WINDOWS_TITLE];
        const char address_to name;

        if (!table || !editor.count)
                return;
        name = editor_basename(editor.tabs[editor.tab].path);
        if (!name[0] && !doc_dirty(&editor.tabs[editor.tab].view.doc))
                string_copy_bounded((string_address)title, (string_address)language_of("Editor"), sizeof(title));
        else
        {
                positive at = 0;

                if (doc_dirty(&editor.tabs[editor.tab].view.doc))
                        at += string_copy_bounded((string_address)title, (string_address) "\xe2\x97\x8f ", sizeof(title));
                at += string_copy_bounded((string_address)title + at, (string_address)(name[0] ? name : WORDS(editor_untitled)), sizeof(title) - at);
                at += string_copy_bounded((string_address)title + at, (string_address) " \xe2\x80\x94 ", sizeof(title) - at);
                string_copy_bounded((string_address)title + at, (string_address)language_of("Editor"), sizeof(title) - at);
        }
        string_copy_bounded((string_address)table->title[editor.own.row], (string_address)title, WINDOWS_TITLE);
}

static fn editor_new_window(const char address_to path)
{
        string_address words[6];
        positive count = 0;

        words[count++] = (string_address) "/editor";
        if (editor_app.font)
                words[count++] = (string_address) "-f", words[count++] = editor_app.font;
        if (path && path[0])
                words[count++] = (string_address)path;
        words[count] = null;
        (void)ui_start_apart(words, count);
}

static b32 editor_ext_is(const char address_to name, const char address_to ext)
{
        positive nlen = string_length((string_address)name), elen = string_length((string_address)ext);

        if (nlen <= elen || name[nlen - elen - 1] != '.')
                return false;
        for (positive i = 0; i < elen; i++)
        {
                unsigned char a = (unsigned char)name[nlen - elen + i], b = (unsigned char)ext[i];

                if (a >= 'A' && a <= 'Z')
                        a = (unsigned char)(a + ('a' - 'A'));
                if (a != b)
                        return false;
        }
        return true;
}

static b32 editor_is_binary(const char address_to path, const p8 address_to bytes, positive length)
{
        static const char address_to const exts[] = {
            "bin", "o", "a", "so", "elf", "exe", "dll", "wasm", "png", "jpg", "jpeg", "gif", "webp", "bmp", "ico",
            "pdf", "zip", "gz", "xz", "zst", "bz2", "tar", "7z", "rar", "mp3", "mp4", "mkv", "webm", "ogg", "opus",
            "flac", "wav", "ttf", "otf", "woff", "woff2", "pyc", "class", "db", "sqlite", "img", "iso", "rom", "pak"};
        const char address_to base = path ? editor_basename(path) : null;
        positive probe = length < 8192 ? length : 8192;

        if (bytes && probe > 0)
        {
                if (memory_first_of((address_any)bytes, 0, probe))
                        return true;
                // Binary file magic headers (covers formats without a NUL in the first 8 KiB).
                if ((length >= 4 && !memory_compare(bytes, "\x7f" "ELF", 4)) ||
                    (length >= 4 && !memory_compare(bytes, "\x89PNG", 4)) ||
                    (length >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) ||
                    (length >= 4 && !memory_compare(bytes, "GIF8", 4)) ||
                    (length >= 5 && !memory_compare(bytes, "%PDF-", 5)) ||
                    (length >= 4 && !memory_compare(bytes, "PK\x03\x04", 4)) ||
                    (length >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b) ||
                    (length >= 4 && bytes[0] == 0x28 && bytes[1] == 0xb5 && bytes[2] == 0x2f && bytes[3] == 0xfd) ||
                    (length >= 6 && !memory_compare(bytes, "\xfd" "7zXZ\0", 6)) ||
                    (length >= 3 && !memory_compare(bytes, "BZh", 3)) ||
                    (length >= 4 && !memory_compare(bytes, "OggS", 4)) ||
                    (length >= 4 && !memory_compare(bytes, "fLaC", 4)) ||
                    (length >= 4 && !memory_compare(bytes, "RIFF", 4)))
                        return true;
        }
        if (base && base[0])
        {
                for (positive i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
                        if (editor_ext_is(base, exts[i]))
                                return true;
        }
        if (bytes && length > 0)
        {
                positive check = length < 4096 ? length : 4096, bad = 0;

                for (positive i = 0; i < check; i++)
                {
                        p8 c = bytes[i];

                        if (c < 9 || (c >= 14 && c < 32 && c != 27))
                                bad++;
                }
                if (bad * 10 > check)
                        return true;
        }
        return false;
}

static b32 editor_open(string_address path)
{
        struct editor_tab address_to tab;

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
                return editor_tab_release(tab, false), false;
        tab->view.own = &editor.own;
        tab->view.wrap = true;
        editor_tab_language(tab, (const char address_to)path, tab->bytes, tab->length);
        if (editor_is_binary((const char address_to)path, tab->bytes, tab->length))
                tab->view.hex = true;
        editor.tab = editor.count++;
        editor_title_sync();
        return true;
}

static b32 editor_open_or_switch(string_address path)
{
        b32 replace_blank;

        if (!path || !path[0])
                return false;
        for (int at = 0; at < editor.count; at++)
                if (editor.tabs[at].path[0] && string_equals((string_address)editor.tabs[at].path, path))
                        return editor.tab = at, editor_keys(), editor_title_sync(), true;
        replace_blank = editor.count == 1 && !editor.tabs[0].path[0] && !doc_length(&editor.tabs[0].view.doc) && !doc_dirty(&editor.tabs[0].view.doc);
        if (!editor_open(path))
                return false;
        if (replace_blank && editor.count == 2)
        {
                editor_tab_release(&editor.tabs[0], true);
                editor.tabs[0] = editor.tabs[1], editor.count = 1, editor.tab = 0;
                editor_keys(), editor_title_sync();
        }
        return true;
}

// Atomic save beside the original in its pinned directory, preserving owner/mode/BOM.
static b32 editor_save(struct editor_tab address_to tab)
{
        char temporary[EDITOR_PATH + 32];
        positive length = string_length((string_address)tab->path), at = 0, slash = editor_dir_len(tab->path);
        bipolar handle;
        error_stat facts, made;
        static p8 chunk[65536];
        unsigned int mode = tab->mode;
        b32 ok = true;

        if (!length)
                return editor_status_set(WORDS(editor_no_file_name)), false;
        string_copy((string_address)temporary, (string_address) ".");
        string_copy_bounded((string_address)temporary + 1, (string_address)tab->path + slash, EDITOR_PATH - 1);
        string_copy((string_address)temporary + string_length((string_address)temporary), (string_address) ".editing");
        handle = tab->folder < 0 ? -1 : system_open_at_mode(tab->folder, (string_address)temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0)
        {
                if (handle == -EEXIST)
                        language_format((char address_to)editor.status, sizeof(editor.status), WORDS(editor_lock_stale), (const char address_to)temporary);
                else
                        editor_status_set(WORDS(editor_write_beside));
                return false;
        }
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
        if (fstat((b32)handle, address_of made) == 0 &&
            ((tab->owner != (p32)-1 && made.st_uid != tab->owner) || (tab->group != (p32)-1 && made.st_gid != tab->group)) &&
            system_call_3(syscall(fchown), (positive)handle, (positive)tab->owner, (positive)tab->group) != 0)
                mode &= ~06000u;
        system_call_2(syscall(fchmod), (positive)handle, (positive)mode);
        ok &= system_call_1(syscall(fsync), (positive)handle) == 0;
        ok &= fstat((b32)handle, address_of facts) == 0;
        system_close(handle);
        if (!ok || system_call_5(syscall(renameat2), (positive)tab->folder, (positive)temporary, (positive)tab->folder, (positive)tab->path + slash, 0) < 0)
        {
                system_call_3(syscall(unlinkat), (positive)tab->folder, (positive)temporary, 0);
                return editor_status_set(WORDS(editor_save_failed)), false;
        }
        tab->size = (p64)facts.st_size, tab->mtime = (p64)facts.st_mtime, tab->mtime_nsec = (p64)facts.st_mtime_nsec;
        doc_mark_saved(&tab->view.doc);
        tab->changed_on_disk = false;
        editor_status_set(WORDS(editor_saved));
        return true;
}

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

static fn editor_reload(void)
{
        struct editor_tab address_to tab = editor_now();
        char path[EDITOR_PATH];
        int at = editor.tab, count = editor.count;

        string_copy_bounded((string_address)path, (string_address)tab->path, EDITOR_PATH);
        editor_tab_release(tab, true);
        editor.count--;
        memory_copy(&editor.tabs[at], &editor.tabs[at + 1], (positive)(editor.count - at) * sizeof(editor.tabs[0]));
        if (!editor_open((string_address)path))
        {
                if (!editor.count)
                        editor_open(null);
                editor.tab = at < editor.count ? at : editor.count - 1;
                return;
        }
        if (editor.count == count && at + 1 < count)
        {
                struct editor_tab again = editor.tabs[count - 1];

                memory_copy(&editor.tabs[at + 1], &editor.tabs[at], (positive)(count - 1 - at) * sizeof(editor.tabs[0]));
                editor.tabs[at] = again, editor.tab = at;
        }
}

static b32 editor_save_as(struct editor_tab address_to tab, const char address_to path)
{
        int folder;

        if (!path || !path[0] || string_length((string_address)path) >= EDITOR_PATH)
                return editor_status_set(WORDS(editor_save_failed)), false;
        if ((folder = editor_folder(path, null)) < 0)
                return editor_status_set(WORDS(editor_write_beside)), false;
        if (tab->folder >= 0)
                system_close(tab->folder);
        tab->folder = folder;
        string_copy_bounded((string_address)tab->path, (string_address)path, EDITOR_PATH);
        if (!tab->mode)
                tab->mode = 0644;
        tab->owner = tab->group = (p32)-1;
        editor_tab_language(tab, path, null, 0);
        if (!editor_save(tab))
                return false;
        editor_title_sync();
        return true;
}

static b32 editor_confirm_check(p32 what, b32 guard, const char address_to msg)
{
        char line[UI_CONFIRM_TEXT];

        if (ui_confirm_gate(&editor.confirm, what, guard, msg, line, sizeof(line)))
                return true;
        editor_status_set(line);
        return false;
}

static fn editor_close_tab_at(int at)
{
        if (at < 0 || at >= editor.count)
                return;
        editor_tab_release(&editor.tabs[at], true);
        editor.count--;
        memory_copy(&editor.tabs[at], &editor.tabs[at + 1], (positive)(editor.count - at) * sizeof(editor.tabs[0]));
        if (editor.tab > at)
                editor.tab--;
        if (editor.tab >= editor.count)
                editor.tab = editor.count - 1;
        if (editor.count <= 0)
                editor_open(null), editor.tab = 0;
        editor_keys(), editor_title_sync();
}

static fn editor_close_tab(void)
{
        editor_close_tab_at(editor.tab);
}

static fn editor_close_tab_asked_at(int at)
{
        if (at < 0 || at >= editor.count)
                return;
        if (at != editor.tab)
                editor.tab = at, editor_keys(), editor_title_sync();
        if (editor_confirm_check(DOC_CMD_CLOSE, doc_dirty(&editor.tabs[at].view.doc), WORDS(editor_closes_unsaved)))
                editor_close_tab_at(at);
}

static fn editor_close_others(int keep)
{
        if (keep < 0 || keep >= editor.count)
                return;
        editor.tab = keep;
        for (int at = editor.count - 1; at >= 0; at--)
                if (at != editor.tab && !doc_dirty(&editor.tabs[at].view.doc))
                        editor_close_tab_at(at);
        editor_title_sync();
}

static fn editor_open_path_bar(int mode, b32 include_file)
{
        const char address_to path = editor_now()->path, address_to home;
        char seed[EDITOR_PATH] = {0};

        if (path[0])
        {
                positive n = include_file ? string_length((string_address)path) : editor_dir_len(path);

                if (n && n < sizeof(seed))
                        memory_copy(seed, path, n), seed[n] = 0;
        }
        if (!seed[0] && (home = (const char address_to)getenv((string_address) "HOME")) && home[0])
        {
                positive n = string_copy_bounded((string_address)seed, (string_address)home, sizeof(seed) - 1);

                if (n > 0 && seed[n - 1] != '/' && n + 1 < sizeof(seed))
                        seed[n++] = '/', seed[n] = 0;
        }
        ui_findbar_open_path(&editor.own, &editor.find, &editor_now()->view, mode, seed, editor.focused);
}

static fn editor_save_asked(void)
{
        struct editor_tab address_to tab = editor_now();

        if (!tab->path[0])
                return editor_open_path_bar(UI_FINDBAR_SAVE_AS, false);
        editor_check_disk(tab);
        if (editor_confirm_check(DOC_CMD_SAVE, tab->changed_on_disk || !(tab->mode & 0222),
                                 tab->changed_on_disk ? WORDS(editor_writes_over_changed) : WORDS(editor_writes_over_readonly)))
                editor_save(tab), editor_title_sync();
}

static void editor_closing(void)
{
        for (int at = 0; at < editor.count; at++)
                if (doc_dirty(&editor.tabs[at].view.doc))
                {
                        editor_status_set(WORDS(editor_unsaved));
                        editor.dirty = true;
                        return;
                }
        editor.leaving = true;
}

static b32 editor_done(void)
{
        return editor.leaving;
}

static fn editor_said(int said, positive count)
{
        switch (said)
        {
        case UI_FINDBAR_SAID_CLEAR:       editor.status[0] = 0; break;
        case UI_FINDBAR_SAID_NO_MATCH:    editor_status_set(WORDS(editor_no_match)); break;
        case UI_FINDBAR_SAID_BAD_PATTERN: editor_status_set(WORDS(editor_pattern_bad)); break;
        case UI_FINDBAR_SAID_REPLACED:    language_format((char address_to)editor.status, sizeof(editor.status), WORDS(editor_replaced), (long)count); break;
        case UI_FINDBAR_SAID_USAGE:       editor_status_set(WORDS(editor_replace_usage)); break;
        case UI_FINDBAR_SAID_REFUSED:     editor_status_set(editor.find.field.refused); break;
        case UI_FINDBAR_SAID_OPEN:
        case UI_FINDBAR_SAID_SAVE_AS:
        {
                p8 path[EDITOR_PATH];
                if (ui_text_string(&editor.find.field, path, sizeof(path)) > 0)
                {
                        if (said == UI_FINDBAR_SAID_OPEN)
                                editor_open_or_switch((string_address)path);
                        else
                                editor_save_as(editor_now(), (const char address_to)path);
                }
                break;
        }
        }
}

static fn editor_bar(p32 command)
{
        positive count = 0;

        editor_said(ui_findbar_command(&editor.ui, &editor.own, &editor.find, &editor_now()->view, command, editor.focused, &count), count);
}

/* ---- context menus ---- */

enum
{
        EDITOR_MENU_FIND = 8,
        EDITOR_MENU_LINE,
        EDITOR_MENU_COMMENT,
        EDITOR_MENU_WRAP,
        EDITOR_MENU_HEX,
        EDITOR_MENU_OPEN,
        EDITOR_MENU_SAVE_AS,
        EDITOR_MENU_NEW_TAB,
        EDITOR_MENU_NEW_WINDOW,
        EDITOR_MENU_TAB_CLOSE,
        EDITOR_MENU_TAB_OTHERS,
        EDITOR_MENU_TAB_WINDOW,
};

static fn editor_command(p32 command);

static fn editor_menu_open(int x, int y)
{
        struct ui_popmenu address_to m = &editor.menu.menu;
        struct ui_text address_to view = &editor_now()->view;
        const struct ui_menu_spec specs[] = {
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {WORDS(menu_find), "Ctrl+F", 0, EDITOR_MENU_FIND, false},
            {WORDS(menu_go_to_line), "Ctrl+G", 0, EDITOR_MENU_LINE, false},
            {WORDS(menu_toggle_comment), "Ctrl+/", 0, EDITOR_MENU_COMMENT, view->hex},
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {WORDS(menu_word_wrap), "Alt+Z", UI_MENU_CHECK | (view->wrap ? UI_MENU_ON : 0), EDITOR_MENU_WRAP, view->hex},
            {WORDS(menu_hex_mode), "Ctrl+Shift+H", UI_MENU_CHECK | (view->hex ? UI_MENU_ON : 0), EDITOR_MENU_HEX, false},
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {WORDS(menu_open_file), "Ctrl+O", 0, EDITOR_MENU_OPEN, false},
            {WORDS(menu_save_as), "Ctrl+Shift+S", 0, EDITOR_MENU_SAVE_AS, false},
            {WORDS(menu_new_tab), "Ctrl+N", 0, EDITOR_MENU_NEW_TAB, false},
            {WORDS(menu_new_window), "Ctrl+Shift+N", 0, EDITOR_MENU_NEW_WINDOW, false},
        };

        m->count = 0, editor.tab_menu_at = -1, editor.menu.field = view;
        ui_text_menu_items(m, view, false);
        ui_popmenu_build(m, specs, (int)(sizeof(specs) / sizeof(specs[0])));
        ui_popmenu_open(&editor.menu, &editor.own, &editor.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false);
}

static fn editor_tab_menu_open(int at, int x, int y)
{
        const struct ui_menu_spec specs[] = {
            {WORDS(menu_close_tab), "Ctrl+W", 0, EDITOR_MENU_TAB_CLOSE, false},
            {WORDS(menu_close_others), null, editor.count <= 1 ? UI_MENU_DISABLED : 0, EDITOR_MENU_TAB_OTHERS, false},
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {WORDS(menu_new_window), "Ctrl+Shift+N", 0, EDITOR_MENU_TAB_WINDOW, false},
        };

        editor.menu.menu.count = 0, editor.tab_menu_at = at, editor.menu.field = null;
        ui_popmenu_build(&editor.menu.menu, specs, (int)(sizeof(specs) / sizeof(specs[0])));
        ui_popmenu_open(&editor.menu, &editor.own, &editor.ui, x, y, 0, 0, WINDOW_POPUP_POINT, false);
}

static fn editor_chose(int id)
{
        struct ui_text address_to view = &editor_now()->view, address_to field = editor.menu.field;

        switch (id)
        {
        case EDITOR_MENU_FIND:       editor_bar(DOC_CMD_FIND); break;
        case EDITOR_MENU_LINE:       editor_bar(DOC_CMD_GOTO_LINE); break;
        case EDITOR_MENU_COMMENT:    ui_text_command(&editor.ui, view, DOC_CMD_COMMENT); break;
        case EDITOR_MENU_WRAP:       ui_text_command(&editor.ui, view, DOC_CMD_WRAP); break;
        case EDITOR_MENU_HEX:        ui_text_command(&editor.ui, view, DOC_CMD_HEX); break;
        case EDITOR_MENU_OPEN:       editor_command(DOC_CMD_OPEN); break;
        case EDITOR_MENU_SAVE_AS:    editor_command(DOC_CMD_SAVE_AS); break;
        case EDITOR_MENU_NEW_TAB:    editor_command(DOC_CMD_NEW); break;
        case EDITOR_MENU_NEW_WINDOW: editor_command(DOC_CMD_NEW_WINDOW); break;
        case EDITOR_MENU_TAB_CLOSE:  if (editor.tab_menu_at >= 0) editor_close_tab_asked_at(editor.tab_menu_at); break;
        case EDITOR_MENU_TAB_OTHERS: if (editor.tab_menu_at >= 0) editor_close_others(editor.tab_menu_at); break;
        case EDITOR_MENU_TAB_WINDOW: if (editor.tab_menu_at >= 0 && editor.tab_menu_at < editor.count) editor_new_window(editor.tabs[editor.tab_menu_at].path); break;
        default:                     if (field == &editor.find.field || field == view) ui_text_menu_do(&editor.ui, field, id); break;
        }
        editor_title_sync();
        editor.dirty = true;
}

/* ---- tabs, status, layout, and draw ---- */

static const char address_to editor_tab_name(int at)
{
        const char address_to name = editor_basename(editor.tabs[at].path);

        return name[0] ? name : WORDS(editor_untitled);
}

static b32 editor_tab_dirty(int at)
{
        return doc_dirty(&editor.tabs[at].view.doc);
}

static fn editor_show_tab(int at)
{
        editor.tab = at, editor_keys(), editor_title_sync(), editor.dirty = true;
}

static b32 editor_tabbed(int which)
{
        (void)which;
        if (editor.strip.add_clicked)
                editor.strip.add_clicked = false, editor_open(null), editor_keys(), editor.dirty = true;
        else if (editor.strip.close_item >= 0)
        {
                int victim = editor.strip.close_item;
                editor.strip.close_item = -1, editor_close_tab_asked_at(victim), editor.dirty = true;
        }
        else if (editor.strip.tab_right_click >= 0)
        {
                int target = editor.strip.tab_right_click;
                editor.strip.tab_right_click = -1, editor_tab_menu_open(target, ui_kit.pointer_x, ui_kit.pointer_y);
        }
        else if (editor.strip.at != editor.tab)
                editor_show_tab(editor.strip.at);
        return true;
}

static b32 editor_toggle(int cmd)
{
        ui_text_command(&editor.ui, &editor_now()->view, (p32)cmd);
        editor.dirty = true;
        return true;
}

static positive editor_status_put(char address_to text, positive room, positive at, const char address_to words)
{
        positive length = string_length((string_address)words);

        string_copy_bounded((string_address)text + at, (string_address)words, room - at);
        return at + length < room ? at + length : room - 1;
}

static fn editor_hex_word(char address_to out, p64 value)
{
        static const char hex[] = "0123456789ABCDEF";

        out[0] = '0', out[1] = 'x';
        for (int d = 7; d >= 0; d--)
                out[2 + (7 - d)] = hex[(value >> (d * 4)) & 15];
        out[10] = 0;
}

static fn editor_hex_status_text(char address_to text, positive room, struct ui_text address_to view)
{
        const struct doc_selection address_to sel = &view->doc.selections[view->doc.primary];
        positive selected = (positive)(doc_sel_end(sel) - doc_sel_start(sel)), at = 0;
        char off_s[16], len_s[16];

        editor_hex_word(off_s, sel->active);
        editor_hex_word(len_s, doc_length(&view->doc));
        at += language_format(text, room, WORDS(editor_hex_status), off_s, len_s);
        if (selected)
                at = editor_status_put(text, room, at, "  "),
                at += language_format(text + at, room - at, WORDS(editor_selected), (long)selected);
        at = editor_status_put(text, room, at, "    ");
        at = editor_status_put(text, room, at, view->hex_ascii ? "ASCII" : WORDS(editor_mode_hex));
        text[at] = 0;
}

static positive editor_status_text(char address_to text, positive room, struct doc address_to doc)
{
        p64 caret = doc->selections[doc->primary].active, line = doc_line_of(doc, caret);
        positive at = 0, selected = 0;
        const char address_to parts[3] = {
            doc->bom ? WORDS(editor_encoding_bom) : "UTF-8",
            doc->eol == DOC_EOL_CRLF ? "CRLF" : doc->eol == DOC_EOL_CR ? "CR" : "LF",
            language_of(doc_languages[doc->language].name),
        };

        for (positive s = 0; s < doc->selection_count; s++)
                selected += doc_sel_end(&doc->selections[s]) - doc_sel_start(&doc->selections[s]);
        at += language_format(text, room, WORDS(editor_position), (long)(line + 1), (long)(doc_column(doc, caret) + 1));
        if (selected)
                at = editor_status_put(text, room, at, "  "),
                at += language_format(text + at, room - at, doc->selection_count > 1 ? WORDS(editor_selected_carets) : WORDS(editor_selected), (long)selected);
        for (int k = 0; k < 3; k++)
                at = editor_status_put(text, room, at, "    "), at = editor_status_put(text, room, at, parts[k]);
        at = editor_status_put(text, room, at, "    ");
        at += language_format(text + at, room - at, doc->insert_spaces ? WORDS(editor_indent_spaces) : WORDS(editor_indent_tab), (long)doc->tab_size);
        text[at] = 0;
        return at;
}

static fn editor_btn_prep(struct ui_control address_to btn, b32 active, const char address_to label)
{
        if (btn->kind != UI_BUTTON)
                btn->kind = UI_BUTTON, ui_control_start(btn);
        btn->variant = active ? UI_BUTTON_PRIMARY : UI_BUTTON_TEXT, btn->label = label;
}

static fn editor_layout(void)
{
        struct editor_tab address_to tab = editor_now();
        struct ui_control address_to strip = &editor.strip;

        if (tab->view.hex)
                editor_hex_status_text(editor.bar_text, sizeof(editor.bar_text), &tab->view);
        else
                editor_status_text(editor.bar_text, sizeof(editor.bar_text), &tab->view.doc);
        if (strip->kind != UI_TABS)
                strip->kind = UI_TABS, strip->variant = UI_TABS_SEGMENTED, strip->closable = strip->addable = true,
                strip->close_item = strip->close_hover = strip->tab_right_click = -1, ui_control_start(strip);
        ui_tabs_fill(strip, editor.count, editor.tab, editor_tab_name, editor_tab_dirty);
        editor_btn_prep(&editor.wrap_btn, tab->view.wrap, WORDS(editor_wrap_on));
        editor_btn_prep(&editor.hex_btn, tab->view.hex, tab->view.hex ? WORDS(editor_mode_text) : WORDS(editor_mode_hex));
        ui_view_begin(&editor.view, &editor.ui, editor.width, -editor.height);
        ui_control(strip, .class = "filetabs", .on = editor_tabbed);
        ui_host(&editor.find_box, .h = UI_FINDBAR_H, .hidden = editor.find.mode == UI_FINDBAR_CLOSED);
        ui_host(&editor.text_box, .grow = 1);
        ui_view_end(&editor.view);
}

static b32 editor_pill_in(const int r[4], int x, int y)
{
        return r[2] > 0 && r[3] > 0 && x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

// Compact floating bottom-right status overlay pill (fully rounded, with Wrap toggle and fixed-width Text | Hex tab switcher).
static fn editor_pill_draw(struct ui address_to ui, struct editor_tab address_to tab)
{
        int pad_x = ui_dev(ui, 10), gap = ui_dev(ui, 6), chip_px = ui_dev(ui, 8), chip_h = ui_dev(ui, 20), ph = ui_dev(ui, 28);
        int seg_pad = ui_dev(ui, 2), seg_h = chip_h + 2 * seg_pad;
        int margin_r = ui_dev(ui, UI_PAD), margin_b = ui_dev(ui, 10);
        int max_w = editor.text_box.w - 2 * margin_r;
        const char address_to wrap_lbl = WORDS(editor_wrap_on);
        const char address_to text_lbl = WORDS(editor_mode_text);
        const char address_to hex_lbl = WORDS(editor_mode_hex);
        const char address_to extra = tab->changed_on_disk ? WORDS(editor_changed_on_disk) : editor.status[0] ? editor.status : null;
        int text_w = ui_kit_measure(ui, (string_address)text_lbl, UI_LABEL_MEDIUM) + 2 * chip_px;
        int hex_w = ui_kit_measure(ui, (string_address)hex_lbl, UI_LABEL_MEDIUM) + 2 * chip_px;
        int seg_w = text_w + hex_w + 2 * seg_pad;
        int wrap_w = tab->view.hex ? 0 : ui_kit_measure(ui, (string_address)wrap_lbl, UI_LABEL_MEDIUM) + 2 * chip_px;
        int btns_w = seg_w + (wrap_w ? wrap_w + gap : 0);
        int extra_w = extra ? ui_kit_measure(ui, (string_address)extra, UI_LABEL_MEDIUM) : 0;
        char cut[256];
        string_address info = (string_address)editor.bar_text;
        int info_room, info_w, pw, px, py, cx, cy, sy, baseline;
        struct ui_box box, seg_box;

        memory_zero(editor.pill_box, sizeof(editor.pill_box));
        memory_zero(editor.pill_wrap, sizeof(editor.pill_wrap));
        memory_zero(editor.pill_text, sizeof(editor.pill_text));
        memory_zero(editor.pill_hex, sizeof(editor.pill_hex));
        if (max_w < btns_w + 2 * pad_x || editor.text_box.h < ph + 2 * margin_b)
                return;
        info_room = max_w - 2 * pad_x - btns_w - gap - (extra_w ? extra_w + gap : 0);
        if (info_room < 0)
                extra = null, extra_w = 0, info_room = max_w - 2 * pad_x - btns_w - gap;
        if (info_room > 0 && editor.bar_text[0])
                info = ui_fit(ui, info, UI_LABEL_MEDIUM, info_room, cut, sizeof(cut)), info_w = ui_kit_measure(ui, info, UI_LABEL_MEDIUM);
        else
                info = (string_address) "", info_w = 0;
        pw = 2 * pad_x + btns_w + (info_w ? info_w + gap : 0) + (extra_w ? extra_w + gap : 0);
        if (pw > max_w)
                pw = max_w;
        px = ui_rtl() ? editor.text_box.x + margin_r : editor.text_box.x + editor.text_box.w - margin_r - pw;
        py = editor.text_box.y + editor.text_box.h - margin_b - ph;
        editor.pill_box[0] = px, editor.pill_box[1] = py, editor.pill_box[2] = pw, editor.pill_box[3] = ph;
        box = ui_box_px(px, py, pw, ph, ph * 128);
        ui_kit_fill(ui, &box, ui_tone(ui_role(UI_SURFACE_CONTAINER) & 0xffffffu, 236), null);
        ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_tone(ui_role(UI_OUTLINE_VARIANT) & 0xffffffu, 110));
        baseline = ui_kit_baseline(ui, py + (ph - ui_dev(ui, ui_types[UI_LABEL_MEDIUM].line)) / 2, UI_LABEL_MEDIUM);
        cx = px + pad_x;
        if (extra && extra_w > 0)
        {
                unsigned int eink = ui_text_colour(ui, tab->changed_on_disk ? UI_WARNING : UI_ON_SURFACE_VARIANT);
                ui_kit_text(ui, cx, baseline, (string_address)extra, UI_LABEL_MEDIUM, eink, 0);
                cx += extra_w + gap;
        }
        if (info_w > 0)
        {
                ui_kit_text(ui, cx, baseline, info, UI_LABEL_MEDIUM, ui_text_colour(ui, UI_ON_SURFACE_VARIANT), 0);
                cx += info_w + gap;
        }
        cy = py + (ph - chip_h) / 2;
        if (wrap_w > 0)
        {
                struct ui_box wb = ui_box_px(cx, cy, wrap_w, chip_h, chip_h * 128);
                unsigned int wfill = tab->view.wrap ? ui_role(UI_PRIMARY)
                                                    : editor.pill_hover == 1 ? ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 26) : 0;
                unsigned int wink = tab->view.wrap ? ui_role(UI_ON_PRIMARY) : ui_text_colour(ui, UI_ON_SURFACE_VARIANT);

                editor.pill_wrap[0] = cx, editor.pill_wrap[1] = cy, editor.pill_wrap[2] = wrap_w, editor.pill_wrap[3] = chip_h;
                if (wfill)
                        ui_kit_fill(ui, &wb, wfill, null);
                ui_kit_text(ui, cx + chip_px, baseline, (string_address)wrap_lbl, UI_LABEL_MEDIUM, wink, 0);
                cx += wrap_w + gap;
        }
        // Fixed-position segmented [ Text | Hex ] tab switcher pinned to the right edge of the pill.
        sy = py + (ph - seg_h) / 2;
        seg_box = ui_box_px(cx, sy, seg_w, seg_h, seg_h * 128);
        ui_kit_fill(ui, &seg_box, ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 18), null);
        cx += seg_pad;
        {
                struct ui_box tb = ui_box_px(cx, cy, text_w, chip_h, chip_h * 128);
                unsigned int tfill = !tab->view.hex ? ui_role(UI_PRIMARY)
                                                    : editor.pill_hover == 2 ? ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 26) : 0;
                unsigned int tink = !tab->view.hex ? ui_role(UI_ON_PRIMARY) : ui_text_colour(ui, UI_ON_SURFACE_VARIANT);

                editor.pill_text[0] = cx, editor.pill_text[1] = cy, editor.pill_text[2] = text_w, editor.pill_text[3] = chip_h;
                if (tfill)
                        ui_kit_fill(ui, &tb, tfill, null);
                ui_kit_text(ui, cx + chip_px, baseline, (string_address)text_lbl, UI_LABEL_MEDIUM, tink, 0);
                cx += text_w;
        }
        {
                struct ui_box hb = ui_box_px(cx, cy, hex_w, chip_h, chip_h * 128);
                unsigned int hfill = tab->view.hex ? ui_role(UI_PRIMARY)
                                                   : editor.pill_hover == 3 ? ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 26) : 0;
                unsigned int hink = tab->view.hex ? ui_role(UI_ON_PRIMARY) : ui_text_colour(ui, UI_ON_SURFACE_VARIANT);

                editor.pill_hex[0] = cx, editor.pill_hex[1] = cy, editor.pill_hex[2] = hex_w, editor.pill_hex[3] = chip_h;
                if (hfill)
                        ui_kit_fill(ui, &hb, hfill, null);
                ui_kit_text(ui, cx + chip_px, baseline, (string_address)hex_lbl, UI_LABEL_MEDIUM, hink, 0);
        }
}

static b32 editor_pill_event(struct editor_tab address_to tab, const struct window_event address_to event)
{
        if (tab->view.sel.dragging || tab->view.gutter_drag || tab->view.drag.armed || tab->view.bar.held)
        {
                if (editor.pill_hover)
                        editor.pill_hover = 0, editor.dirty = true;
                return false;
        }
        if (event->type == WINDOW_EVENT_MOTION)
        {
                int hover = editor_pill_in(editor.pill_wrap, event->a, event->b) ? 1
                            : editor_pill_in(editor.pill_text, event->a, event->b) ? 2
                            : editor_pill_in(editor.pill_hex, event->a, event->b) ? 3 : 0;

                if (hover)
                        ui_kit_shape = WINDOW_POINTER_HAND;
                if (hover != editor.pill_hover)
                        editor.pill_hover = hover, editor.dirty = true;
                return editor_pill_in(editor.pill_box, event->a, event->b);
        }
        if (event->type == WINDOW_EVENT_BUTTON && (p32)event->a == UI_POINTER_LEFT)
        {
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        if (!editor_pill_in(editor.pill_box, ui_kit.pointer_x, ui_kit.pointer_y))
                                return false;
                        editor.pill_down = true;
                        if (editor_pill_in(editor.pill_wrap, ui_kit.pointer_x, ui_kit.pointer_y))
                                editor_toggle((int)DOC_CMD_WRAP);
                        else if (editor_pill_in(editor.pill_text, ui_kit.pointer_x, ui_kit.pointer_y) && tab->view.hex)
                                editor_toggle((int)DOC_CMD_HEX);
                        else if (editor_pill_in(editor.pill_hex, ui_kit.pointer_x, ui_kit.pointer_y) && !tab->view.hex)
                                editor_toggle((int)DOC_CMD_HEX);
                        return true;
                }
                if (editor.pill_down)
                {
                        editor.pill_down = false;
                        return true;
                }
        }
        return false;
}

static fn editor_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &editor.ui;
        struct editor_tab address_to tab = editor_now();

        ui_begin(ui, pixels, editor.own.stride, editor.width, editor.height);
        editor_layout();
        if (editor.find.mode != UI_FINDBAR_CLOSED)
        {
                const char address_to hints[6] = {"", WORDS(editor_find_hint), WORDS(editor_find_replace_hint),
                                                  WORDS(editor_line_hint), WORDS(editor_open_hint), WORDS(editor_save_as_hint)};
                ui_findbar_draw(ui, &editor.find, editor.find_box.y, editor.width,
                                editor.find.mode >= 0 && editor.find.mode < 6 ? hints[editor.find.mode] : "");
        }
        ui_text_draw(ui, &tab->view, editor.text_box.x, editor.text_box.y, editor.text_box.w, editor.text_box.h, ui_role(UI_SURFACE) & 0xffffffu);
        ui_view_draw(ui, &editor.view, false);
        editor_pill_draw(ui, tab);
        ui_findbar_overlay(ui, &editor.find);
        ui_text_overlay(ui, &tab->view);
}

/* ---- commands and events ---- */

static fn editor_command(p32 command)
{
        if (command == DOC_CMD_SAVE)
                editor_save_asked();
        else if (command == DOC_CMD_SAVE_AS || command == DOC_CMD_OPEN)
                editor_open_path_bar(command == DOC_CMD_SAVE_AS ? UI_FINDBAR_SAVE_AS : UI_FINDBAR_OPEN, command == DOC_CMD_SAVE_AS);
        else if (ui_findbar_is_command(command))
                editor_bar(command);
        else if (command == DOC_CMD_NEW)
                editor_open(null), editor_keys();
        else if (command == DOC_CMD_NEW_WINDOW)
                editor_new_window(null);
        else if (command == DOC_CMD_CLOSE)
                editor_close_tab_asked_at(editor.tab);
        else if (command == DOC_CMD_NEXT_TAB || command == DOC_CMD_PREVIOUS_TAB)
                editor_show_tab((editor.tab + (command == DOC_CMD_NEXT_TAB ? 1 : editor.count - 1)) % editor.count);
}

#define EDITOR_RELOAD DOC_CMDS

static b32 editor_reload_key(const struct window_event address_to event)
{
        return event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_CONTROL) && ((p32)event->b | 0x60) == 'r';
}

static b32 editor_event(const struct window_event address_to event)
{
        struct editor_tab address_to tab = editor_now();
        p32 asks;

        if ((event->flags & WINDOW_KEY_DOWN) &&
            (event->type == WINDOW_EVENT_BUTTON ||
             (event->type == WINDOW_EVENT_KEY && !ui_key_modifier(event->a) &&
              (editor_reload_key(event) ? EDITOR_RELOAD : ui_key_command(event)) != editor.confirm.what)))
                ui_confirm_drop(&editor.confirm);
        editor_layout();
        asks = ui_view_event(&editor.ui, &editor.view, event);
        if (asks & UI_VIEW_DRAW)
                editor.dirty = true;
        if (asks & UI_VIEW_TAKEN)
        {
                if (editor.pill_hover)
                        editor.pill_hover = 0, editor.dirty = true;
                if (event->type == WINDOW_EVENT_BUTTON && (p32)event->a == UI_POINTER_LEFT && !(event->flags & WINDOW_KEY_DOWN))
                        editor.pill_down = false;
                return true;
        }
        if (editor_pill_event(tab, event))
                return true;
        if (editor_reload_key(event) && (event->flags & WINDOW_KEY_DOWN) && tab->changed_on_disk)
        {
                if (editor_confirm_check(EDITOR_RELOAD, doc_dirty(&tab->view.doc), WORDS(editor_reload_unsaved)))
                        editor_reload();
                return editor.dirty = true;
        }
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
        if (event->type == WINDOW_EVENT_DRAG && !tab->view.drag.live)
        {
                static const char address_to const uri_name[1] = {WINDOWS_CLIP_URIS};
                int what = ui_drop_event(&editor.drop, event);

                if (what == UI_DROP_OVER && ui_drop_pick(&editor.own, &editor.drop, uri_name, 1, WINDOWS_DRAG_COPY, WINDOWS_DRAG_COPY))
                        return true;
                if (what == UI_DROP_READY && editor.drop.nth == 0)
                {
                        p8 address_to bytes;
                        unsigned long length;
                        positive opened = 0;

                        if (ui_drop_read(&editor.own, &editor.drop, &bytes, &length))
                        {
                                p8 uri_path[EDITOR_PATH];
                                positive pos = 0;

                                while (ui_uri_next(bytes, length, &pos, uri_path, sizeof(uri_path)))
                                        opened += editor_open_or_switch((string_address)uri_path);
                                windows_clip_free(bytes, length);
                                editor.dirty = true;
                        }
                        ui_drop_done(&editor.own, &editor.drop, opened > 0);
                        return true;
                }
        }
        tab = editor_now();
        editor_keys();
        asks = ui_text_event(&editor.ui, &tab->view, event, ui_kit.pointer_x, ui_kit.pointer_y);
        if (asks & UI_TEXT_COMMAND)
                editor_command(tab->view.command);
        if (asks & UI_TEXT_MENU)
                editor_menu_open(tab->view.menu_x, tab->view.menu_y);
        if ((asks & UI_TEXT_REFUSED) && tab->view.refused)
                editor_status_set(tab->view.refused);
        if (asks & UI_TEXT_CHANGED)
                editor_title_sync();
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
        return editor.dirty = true;
}

static long editor_tick(long timeout, long address_to other)
{
        b32 redraw = false;

        (void)other;
        timeout = ui_findbar_timeout(&editor.ui, &editor.find, &editor_now()->view, timeout, &redraw);
        if (redraw)
                editor.dirty = true;
        return timeout;
}

static fn editor_language(void)
{
        editor.status[0] = 0, editor_title_sync(), editor.dirty = true;
}

static int editor_start(void)
{
        struct windows address_to table = editor.own.table;
        int screen_x, screen_y, screen_w, screen_h;

        windows_output(table, 0, &screen_x, &screen_y, &screen_w, &screen_h);
        __atomic_store_n(&table->pinch_wanted[editor.own.row], 1u, __ATOMIC_RELEASE);
        editor.width = screen_w * 3 / 5, editor.height = screen_h * 3 / 4;
        ui_free(table, editor.own.row, editor.width, editor.height, 0);
        ui_scratch_fit(&editor.ui, (unsigned int)screen_w);
        for (int at = 0; at < editor.start_count; at++)
                if (!editor_open(editor.start_files[at]) && !editor.count)
                        break;
        if (!editor.count)
                editor_open(null);
        editor.tab = 0, editor_title_sync(), editor.dirty = true;
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
    .language = editor_language,
    .event = editor_event,
    .focus = editor_focus,
    .tick = editor_tick,
    .popup = &editor.menu,
    .chose = editor_chose,
    .closing = editor_closing,
    .done = editor_done,
    .mono = true,
    .own_title = true,
    .width = &editor.width,
    .height = &editor.height,
    .shown = &editor.shown,
    .draw = editor_draw,
};

static b32 editor_main()
{
        editor.start_count = 0;
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took ? took : 1)
                if (!(took = ui_app_option(&editor_app, at)) && editor.start_count < EDITOR_TABS)
                        editor.start_files[editor.start_count++] = program_argument(at);
        return ui_app_run(&editor_app);
}

#endif // MOONWATER_CANVAS_EDITOR
