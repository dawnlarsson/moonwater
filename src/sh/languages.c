/*
        What `moonwater language` says after its one line: the languages the image carries, each by its code, the
        name the language gives itself and, for one a machine made, that it is, so nobody takes it for one a person
        reviewed. The setting itself is a plain row of registry.c; this is the listing its bare form adds.

        A language is a file of SETTING_LANGUAGE_DIRECTORY, CODE.txt, whose first lines are its header (@language,
        @name, @translated machine or reviewed); only those are read here, never the text, so the listing costs a
        few small reads. A code the row offers that has no file is not listed, as it is not installed.
*/
#define HOST_LANGUAGE_HEAD 192

// The text after a header's key (the value up to the end of its line), or none when the header has no such line.
static string_address host_language_header(p8 address_to head, positive size, string_address key, p8 address_to value, positive room)
{
        positive length = string_length(key);
        const char address_to at = (const char address_to)memory_search((address_any)head, size, (address_any)key, length);
        positive used = 0;

        if (!at || (at != (const char address_to)head && at[-1] != '\n'))
                return null;
        for (at += length; *at && *at != '\n' && used + 1 < room; at++)
                value[used++] = (p8)*at;
        value[used] = end;
        return (string_address)value;
}

static fn host_language_list(void)
{
        const char address_to codes = SETTINGS_LANGUAGE_CODES;

        // English is the table in the programs themselves, so it has no file and is always there.
        host_say(log, "  en  English\n");
        codes += 3;
        while (*codes)
        {
                p8 path[sizeof(SETTING_LANGUAGE_DIRECTORY) + 8], head[HOST_LANGUAGE_HEAD], name[48], how[16], code[8];
                positive size = 0, at = sizeof(SETTING_LANGUAGE_DIRECTORY) - 1;
                bipolar got;

                while (codes[size] && codes[size] != ' ')
                        size++;
                if (size && size < sizeof(code))
                {
                        memory_copy(code, codes, size);
                        code[size] = end;
                        memory_copy(path, SETTING_LANGUAGE_DIRECTORY, at);
                        memory_copy(path + at, code, size + 1);
                        memory_copy(path + at + size, ".txt", 5);
                        got = file_slurp_once_at(AT_FDCWD, (string_address)path, head, sizeof(head));
                        if (got > 0)
                        {
                                string_address says = host_language_header(head, (positive)got, "@name ", name, sizeof(name));
                                string_address made = host_language_header(head, (positive)got, "@translated ", how, sizeof(how));

                                host_say(log, "  %s  %s%s\n", (string_address)code, says ? says : (string_address) "",
                                         made && string_equals(made, "machine") ? (string_address) "  machine-translated"
                                                                                : (string_address) "");
                        }
                }
                codes += size + (codes[size] == ' ');
        }
}
