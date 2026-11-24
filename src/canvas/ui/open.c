/*
        open: what opens what, at the command line (ui.c's "apps: what opens what", which Files, Settings and the portal's OpenURI
        open through too).

            open TARGET...                   each opened with what opens its type: a page, a link of a program's scheme, a file, a folder
            open -type TARGET                its type, and what opens it
            open -handlers TYPE|KIND         every handler that opens it: its name for -set, what it is called, * for the one it opens with
            open -defaults                   each kind, what opens it, and whether the person chose it
            open -set TYPE|KIND HANDLER|auto the person's choice, kept in UI_OPEN_DEFAULTS (auto: none, the first that opens it)
            open -asked [--] TARGET...       as a program asked it (the OpenURI portal, src/dbus/service.c): a program file is not opened

        The kinds are browser, mail, folders, text, images, video, music and documents; a type is a MIME type or x-scheme-handler/SCHEME.
        moonwater open and moonwater defaults are this.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_OPEN
#define MOONWATER_CANVAS_OPEN

static int open_fail(string_address what, string_address target)
{
        string_format(log_error, "open: %s: %s\n", target, what), log_flush();
        return 1;
}

// A type or a kind as the key a choice is kept by, and the type its handlers are found by (a kind's first).
static const char address_to open_key_type(const char address_to key)
{
        const struct ui_open_kind address_to kind = ui_open_kind_named(key);
        static char first[UI_OPEN_TYPE];
        positive at = 0;

        if (!kind)
                return key;
        while (kind->types[at] && kind->types[at] != ';' && at + 1 < sizeof(first))
                first[at] = kind->types[at], at++;
        first[at] = 0;
        return first;
}

static b32 open_key_ok(const char address_to key)
{
        positive length = string_length((string_address)key);

        if (ui_open_kind_named(key))
                return true;
        if (!length || length >= UI_OPEN_TYPE || !string_first_of((string_address)key, '/'))
                return false;
        for (positive at = 0; at < length; at++)
                if (!((key[at] >= 'a' && key[at] <= 'z') || (key[at] >= 'A' && key[at] <= 'Z') || (key[at] >= '0' && key[at] <= '9') ||
                      string_first_of((string_address) "/+-._*", key[at])))
                        return false;
        return true;
}

static int open_handlers(const char address_to key)
{
        const char address_to type = open_key_type(key);
        b32 chosen;
        int with = ui_open_handler(type, &chosen);

        for (int at = 0; at < ui_handlers.count; at++)
                if (ui_handler_takes(&ui_handlers.handler[at], type))
                        string_format(log, "%s\t%s%s\n", (string_address)ui_handlers.handler[at].handle, (string_address)ui_handlers.handler[at].name,
                                      at == with ? (string_address)(chosen ? "\t* chosen" : "\t*") : (string_address) "");
        log_flush();
        return 0;
}

static int open_defaults(void)
{
        for (positive at = 0; at < sizeof(ui_open_kinds) / sizeof(ui_open_kinds[0]); at++)
        {
                b32 chosen;
                int with = ui_open_handler(open_key_type(ui_open_kinds[at].kind), &chosen);

                string_format(log, "%s\t%s\t%s\t%s\n", (string_address)ui_open_kinds[at].kind,
                              with >= 0 ? (string_address)ui_handlers.handler[with].handle : (string_address) "-",
                              with >= 0 ? (string_address)ui_handlers.handler[with].name : (string_address) "-",
                              (string_address)(chosen ? "chosen" : "found"));
        }
        log_flush();
        return 0;
}

// The choices kept again with this one in place of the key's (or without the key, for auto): a new file, renamed over the old.
static int open_set(const char address_to key, const char address_to handle)
{
        positive size = 0, length = string_length((string_address)key), bytes;
        const char address_to text;
        char temporary[] = UI_OPEN_DEFAULTS ".new";
        bipolar out;
        b32 failed = false;

        if (!open_key_ok(key))
                return open_fail("is no kind and no type (a kind: browser, mail, folders, text, images, video, music, documents)", (string_address)key);
        if (!string_equals((string_address)handle, (string_address) "auto"))
        {
                if (ui_handler_find(handle) < 0)
                        return open_fail("is no handler here (open -handlers lists them)", (string_address)handle);
                if (!ui_handler_takes(&ui_handlers.handler[ui_handler_find(handle)], open_key_type(key)))
                        return open_fail("does not say it opens that", (string_address)handle);
        }
        if ((out = system_open_at_mode(AT_FDCWD, (string_address)temporary, FILE_WRITE | O_CLOEXEC | O_NOFOLLOW, 0644)) < 0)
                return open_fail("cannot be written", (string_address)UI_OPEN_DEFAULTS);
        bytes = compositor_map_file((string_address)UI_OPEN_DEFAULTS, &size);
        text = (const char address_to)bytes;
        for (positive at = 0; bytes && at < size;)
        {
                positive stop = at;

                while (stop < size && text[stop] != '\n')
                        stop++;
                if (stop > at && !(stop - at > length && !memory_compare(text + at, key, length) && text[at + length] == '\t'))
                        failed |= system_write_all((positive)out, text + at, stop - at) != stop - at || system_write_all((positive)out, "\n", 1) != 1;
                at = stop + 1;
        }
        if (bytes)
                system_call_2(syscall(munmap), bytes, size);
        if (!string_equals((string_address)handle, (string_address) "auto"))
        {
                positive handle_length = string_length((string_address)handle);

                failed |= system_write_all((positive)out, key, length) != length || system_write_all((positive)out, "\t", 1) != 1 ||
                          system_write_all((positive)out, handle, handle_length) != handle_length || system_write_all((positive)out, "\n", 1) != 1;
        }
        system_close(out);
        if (failed || system_call_4(syscall(renameat), (positive)AT_FDCWD, (positive)temporary, (positive)AT_FDCWD, (positive)UI_OPEN_DEFAULTS) < 0)
        {
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)temporary, 0);
                return open_fail("cannot be written", (string_address)UI_OPEN_DEFAULTS);
        }
        return 0;
}

/*
        The types that are a program, which what a program asks (-asked: OpenURI, a bowl's xdg-open) never opens: a page or a message
        that ends in one would run it with a click. The person opens them from Files, the launcher or a terminal.
*/
static b32 open_runs(const char address_to type)
{
        static const char address_to const runs[] = {
            "application/x-executable", "application/x-pie-executable", "application/x-sharedlib", "application/vnd.appimage",
            "application/x-appimage", "application/x-desktop", "application/x-ms-dos-executable", "application/x-msdownload",
            "application/x-msi", "application/vnd.microsoft.portable-executable", "application/x-ms-shortcut", "application/x-java-archive",
            "application/vnd.flatpak.ref", "application/vnd.flatpak", "application/x-deb", "application/x-rpm"};

        for (positive at = 0; at < sizeof(runs) / sizeof(runs[0]); at++)
                if (string_equals((string_address)type, (string_address)runs[at]))
                        return true;
        return false;
}

/*
        The last way, where nothing took a target: the person is told, on the desktop, and a link (a sign-in's above all) is put on the
        clipboard to paste into a browser by hand, so it is never lost. A file that nothing opens is named.
*/
static fn open_last_way(string_address target, int said)
{
        positive length = string_length(target), room = length + 512;
        char address_to message = (char address_to)memory_checked(room);
        b32 link = false, copied = false;
        positive scheme = 0;

        while ((target[scheme] >= 'a' && target[scheme] <= 'z') || (target[scheme] >= 'A' && target[scheme] <= 'Z') ||
               (scheme && ((target[scheme] >= '0' && target[scheme] <= '9') || string_first_of((string_address) "+-.", target[scheme]))))
                scheme++;
        link = scheme >= 2 && target[scheme] == ':' && string_compare_max(target, (string_address) "file:", 5);
        if (link)
        {
                string_address words[5] = {(string_address) "/moonwater", (string_address) "clipboard", (string_address) "set", target, null};

                copied = ui_run_wait(words);
        }
        if (!message)
                return;
        language_format(message, room, copied ? WORDS(open_copied) : WORDS(open_nothing), (long)target);
        if (said != UI_OPEN_NOT_THERE || link)
                ui_alert_show(UI_ALERT_ERROR, WORDS(open_failed), message);
        memory_free(message, room);
}

static b32 open_main()
{
        positive count = (positive)program_argument_count();
        string_address first = count > 1 ? program_argument(1) : (string_address) "";
        int failed = 0;

        if (count < 2)
                return string_format(log_error, "usage: open TARGET... | -type TARGET | -handlers TYPE|KIND | -defaults | -set TYPE|KIND HANDLER|auto\n"),
                       log_flush(), 2;
        ui_handlers_read();
        if (string_equals(first, (string_address) "-defaults") && count == 2)
                return open_defaults();
        if (string_equals(first, (string_address) "-handlers") && count == 3)
                return open_handlers((const char address_to)program_argument(2));
        if (string_equals(first, (string_address) "-set") && count == 4)
                return open_set((const char address_to)program_argument(2), (const char address_to)program_argument(3));
        if (string_equals(first, (string_address) "-type") && count == 3)
        {
                char type[UI_OPEN_TYPE], path[4096];
                b32 chosen;
                int with;

                if (!ui_open_type((const char address_to)program_argument(2), type, sizeof(type), path, sizeof(path)))
                        return open_fail("is not there", program_argument(2));
                with = ui_open_handler(type, &chosen);
                string_format(log, "%s\t%s\t%s\n", (string_address)type, with >= 0 ? (string_address)ui_handlers.handler[with].handle : (string_address) "-",
                              (string_address)(with < 0 ? "nothing opens it" : chosen ? "chosen" : "found")),
                    log_flush();
                return 0;
        }
        {
                positive at = 1;
                b32 asked = false;

                if (string_equals(program_argument(at), (string_address) "-asked"))
                        asked = true, at++;
                if (at < count && string_equals(program_argument(at), (string_address) "--"))
                        at++;
                for (; at < count; at++)
                {
                        char type[UI_OPEN_TYPE], path[4096];
                        int said;

                        if (asked && ui_open_type((const char address_to)program_argument(at), type, sizeof(type), path, sizeof(path)) && open_runs(type))
                        {
                                failed = open_fail("is a program, which a program's link does not start", program_argument(at));
                                continue;
                        }
                        said = ui_open((const char address_to)program_argument(at));
                        if (said != UI_OPEN_DONE)
                                open_last_way(program_argument(at), said);

                        if (said == UI_OPEN_NOT_THERE)
                                failed = open_fail("is not there, and no address", program_argument(at));
                        else if (said == UI_OPEN_NO_HANDLER)
                                failed = open_fail("nothing here opens it (open -type says its type)", program_argument(at));
                        else if (said == UI_OPEN_NOT_STARTED)
                                failed = open_fail("what opens it could not be started", program_argument(at));
                }
        }
        return failed;
}

#endif // MOONWATER_CANVAS_OPEN
