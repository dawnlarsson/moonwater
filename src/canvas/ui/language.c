/*
        The language the system's own windows speak.

        Every word those windows say is one id of language.inc, whose English is the table itself: asking
        for it is an index, and with English chosen nothing is read, parsed or hashed. Any other language
        is one file, SETTING_LANGUAGE_DIRECTORY (registry.c) and the code and .txt (languages/ in the tree, which the image
        carries beside the fonts), read whole into one buffer of this program the first time the table
        says that language and again when it says another. Its lines are

            ID TAB STAMP TAB TEXT         a word of the table, by its id
            =ENGLISH TAB STAMP TAB TEXT   a word of a data table (a Settings row's label), by its English

        and its header lines start with @ (@language, @name, @translated, and @direction rtl for a language that reads from
        the right; English and the rest have no @direction line). The stamp is for the checks
        alone: a hash of the English the line was made from, so an English edit shows every translation of
        it as old. The text is one line of UTF-8 with no escapes, and the line's two tabs and its end are
        turned to NULs in place, so a word asked for is a pointer into the buffer and the file is never
        copied or parsed twice. A word a file lacks is the English; nothing is ever blank.

        The code is the table's: the window table carries it (windows_language_code packs it), the
        compositor reads /root/language into it and counts the table's theme up, and the kit takes it
        where it takes the theme (ui_kit_language_take), so every window changes at once and none reads
        a setting of its own. A text an app copied when it started is set again by a listener it registers
        here (language_listen), run after the file is taken.

        language_text(id) is for a word the code names, language_of(english) for one that is data (a
        label in a table, which code also compares and so keeps in English as its key): the English
        passes through untouched when there is no other language, and is looked up by its hash in the
        file's keyed lines when there is. language_format fills the placeholders of a text in the order its
        language wants: %s a string, %d a number, %% a percent sign, and %N$s or %N$d the Nth argument,
        so a translation may put its words in another order than the English.
*/
#ifndef MOONWATER_CANVAS_LANGUAGE
#define MOONWATER_CANVAS_LANGUAGE

// A language file may be this long; a longer one is read as far as it goes and the rest is English.
#define LANGUAGE_FILE_MOST (96u << 10)
// The slots of the keyed lines (a power of two), of which three quarters are filled at most.
#define LANGUAGE_KEYED 512u
#define LANGUAGE_LISTENERS 8
#define LANGUAGE_ARGUMENTS 9
// English's own code, packed as windows_language_code packs it: the same as no language kept.
#define LANGUAGE_ENGLISH ('e' | 'n' << 8)

enum
{
#define LANGUAGE_ID(id, text) LANGUAGE_##id,
#include "language.inc"
#undef LANGUAGE_ID
        LANGUAGE_COUNT
};

static const struct language_entry
{
        const char address_to name;
        const char address_to english;
} language_table[LANGUAGE_COUNT] = {
#define LANGUAGE_ID(id, text) {#id, text},
#include "language.inc"
#undef LANGUAGE_ID
};

static struct language
{
        unsigned int code; // the language taken, 0 for English
        b32 loaded;        // a file is read: asking goes to it
        b32 rtl;           // its file says @direction rtl: it reads from the right (Arabic, Hebrew, Persian, Urdu)
        unsigned int keyed_count;
        const char address_to text[LANGUAGE_COUNT];
        struct
        {
                const char address_to key;
                const char address_to text;
        } keyed[LANGUAGE_KEYED];
        void (*listener[LANGUAGE_LISTENERS])(void);
        unsigned int listeners;
} language;

// Where the files are: the image's directory (registry.c), which a check points at the tree's languages/ instead.
static const char address_to language_directory = SETTING_LANGUAGE_DIRECTORY;

// The file, in place: untouched pages cost nothing, and a language costs the size of its file. Two, taken by turns, so the
// words a window still holds (a menu's labels, a refusal on a field) of the language just left are whole when it changes.
static char language_file[2][LANGUAGE_FILE_MOST];
static unsigned int language_side;

// The words of an id, in the language taken, or the English where the file has none.
static inline const char address_to language_text(unsigned int id)
{
        const char address_to said = language.loaded ? language.text[id] : null;

        return said ? said : language_table[id].english;
}

#define WORDS(id) language_text(LANGUAGE_##id)

// Whether the language taken reads from the right to the left: the kit's layout direction (ui_kit.rtl) is this.
static inline b32 language_rtl(void)
{
        return language.rtl;
}

static unsigned int language_hash(const char address_to text)
{
        unsigned int hash = 2166136261u;

        for (; *text; text++)
                hash = (hash ^ (p8)*text) * 16777619u;
        return hash;
}

static const char address_to language_keyed_of(const char address_to english)
{
        unsigned int at = language_hash(english) & (LANGUAGE_KEYED - 1);

        while (language.keyed[at].key)
        {
                if (string_equals((string_address)language.keyed[at].key, (string_address)english))
                        return language.keyed[at].text;
                at = (at + 1) & (LANGUAGE_KEYED - 1);
        }
        return english;
}

// A data table's English text as the language says it: itself when there is no other language, or no line for it.
static inline const char address_to language_of(const char address_to english)
{
        return language.loaded && english ? language_keyed_of(english) : english;
}

// Where name is among the ids, which the table keeps sorted by their bytes; -1 for none.
static int language_find(const char address_to name)
{
        unsigned int low = 0, high = LANGUAGE_COUNT;

        while (low < high)
        {
                unsigned int mid = (low + high) / 2;
                const char address_to other = language_table[mid].name;
                const char address_to mine = name;

                while (*mine && *mine == *other)
                        mine++, other++;
                if (*mine == *other)
                        return (int)mid;
                if ((p8)*mine < (p8)*other)
                        high = mid;
                else
                        low = mid + 1;
        }
        return -1;
}

static fn language_put(char address_to key, char address_to text)
{
        if (key[0] == '=')
        {
                unsigned int at;

                key++;
                if (language.keyed_count >= LANGUAGE_KEYED / 4 * 3)
                        return;
                at = language_hash(key) & (LANGUAGE_KEYED - 1);
                while (language.keyed[at].key && !string_equals((string_address)language.keyed[at].key, (string_address)key))
                        at = (at + 1) & (LANGUAGE_KEYED - 1);
                language.keyed_count += !language.keyed[at].key;
                language.keyed[at].key = key, language.keyed[at].text = text;
        }
        else
        {
                int found = language_find(key);

                if (found >= 0)
                        language.text[found] = text;
        }
}

// The lines of a file read to stop, which holds a NUL: header lines and any line that is not two tabs and a text are skipped.
static fn language_parse(char address_to at, char address_to stop)
{
        while (at < stop)
        {
                char address_to line = at, address_to tab;

                while (at < stop && *at != '\n')
                        at++;
                *at++ = 0;
                if (line[0] == '@')
                {
                        language.rtl |= string_equals((string_address)line, (string_address) "@direction rtl");
                        continue;
                }
                if (!line[0] || line[0] == '#')
                        continue;
                for (tab = line; *tab && *tab != '\t'; tab++)
                        ;
                if (!*tab)
                        continue;
                *tab++ = 0;
                while (*tab && *tab != '\t')
                        tab++;
                if (!*tab)
                        continue;
                language_put(line, tab + 1);
        }
}

/*
        The languages the Language row offers: a code and the language's own name for itself, which is what its menu shows
        (a person finds their language by its own name, whichever language is taken) and what the file's @name says. The
        codes are SETTINGS_LANGUAGE_CODES, in the same order, which a check holds them to.
*/
static const struct
{
        const char address_to code;
        const char address_to name;
} language_names[] = {
        {"en", "English"},
        {"es", "Espa\xc3\xb1ol"},
        {"fr", "Fran\xc3\xa7" "ais"},
        {"de", "Deutsch"},
        {"pt", "Portugu\xc3\xaas"},
        {"it", "Italiano"},
        {"nl", "Nederlands"},
        {"pl", "Polski"},
        {"tr", "T\xc3\xbcrk\xc3\xa7" "e"},
        {"sv", "Svenska"},
        {"ru", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"},
        {"uk", "\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0"},
        {"el", "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac"},
        {"ar", "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9"},
        {"he", "\xd7\xa2\xd7\x91\xd7\xa8\xd7\x99\xd7\xaa"},
        {"fa", "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c"},
        {"ur", "\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88"},
};

// A language's own name for its code, or the code when it is none of the table's.
static const char address_to language_name_of(const char address_to code)
{
        for (positive at = 0; at < sizeof(language_names) / sizeof(language_names[0]); at++)
                if (string_equals((string_address)code, (string_address)language_names[at].code))
                        return language_names[at].name;
        return code;
}

// The code of a language's own name (the inverse of language_name_of), or the word itself.
static const char address_to language_code_of(const char address_to name)
{
        for (positive at = 0; at < sizeof(language_names) / sizeof(language_names[0]); at++)
                if (string_equals((string_address)name, (string_address)language_names[at].name))
                        return language_names[at].code;
        return name;
}

// Run again, after the language changes, by the code that copied a text when it started (a title, a list of names).
static fn language_listen(void (*again)(void))
{
        if (language.listeners < LANGUAGE_LISTENERS)
                language.listener[language.listeners++] = again;
}

/*
        The language the table says, taken: English when the code is none or en, else the file of that code
        (and English if there is none, or it does not read). A code already taken does nothing, so the loops
        may ask at every pass.
*/
static fn language_take(unsigned int code)
{
        if (code == LANGUAGE_ENGLISH)
                code = 0;
        if (code == language.code)
                return;
        language.code = code, language.loaded = 0, language.keyed_count = 0, language.rtl = 0;
        memory_zero(language.text, sizeof(language.text));
        memory_zero(language.keyed, sizeof(language.keyed));
        if (code)
        {
                char path[96];
                positive at = string_length((string_address)language_directory);
                char address_to file = language_file[language_side ^= 1];
                bipolar got;

                if (at + 8 > sizeof(path))
                        at = 0;
                memory_copy(path, language_directory, at);
                for (unsigned int shift = 0; shift < 24 && (code >> shift & 255); shift += 8)
                        path[at++] = (char)(code >> shift & 255);
                memory_copy(path + at, ".txt", 5);
                got = at ? file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)file, LANGUAGE_FILE_MOST) : -1;
                if (got > 0)
                {
                        language_parse(file, file + got);
                        language.loaded = 1;
                }
        }
        for (unsigned int k = 0; k < language.listeners; k++)
                language.listener[k]();
}

/*
        A text with its placeholders filled. The arguments come in the English's order and a placeholder may
        name any of them (%2$s before %1$d), which the first pass learns from the format itself; one
        a translation leaves out is still read, as a number, so the arguments after it are the ones named.
        Answers the length, which is cut to room - 1.
*/
static positive language_format(char address_to into, positive room, const char address_to format, ...)
{
        union
        {
                long number;
                const char address_to string;
        } argument[LANGUAGE_ARGUMENTS];
        char kind[LANGUAGE_ARGUMENTS] = {0};
        unsigned int implicit = 0, used = 0;
        positive out = 0;
        var_args list;

        for (const char address_to f = format; *f; f++)
        {
                unsigned int index;

                if (*f != '%')
                        continue;
                if (*++f == '%')
                        continue;
                if (*f >= '1' && *f <= '9' && f[1] == '$')
                        index = (unsigned int)(*f - '0'), f += 2;
                else
                        index = ++implicit;
                if (!*f)
                        break;
                if (index >= 1 && index <= LANGUAGE_ARGUMENTS)
                        kind[index - 1] = *f == 's' ? 's' : 'd', used = index > used ? index : used;
        }
        var_list(list, format);
        for (unsigned int k = 0; k < used; k++)
        {
                if (kind[k] == 's')
                        argument[k].string = var_list_get(list, const char address_to);
                else
                        argument[k].number = var_list_get(list, long);
        }
        var_list_end(list);
        implicit = 0;
        for (const char address_to f = format; *f && out + 1 < room; f++)
        {
                unsigned int index;

                if (*f != '%')
                {
                        into[out++] = *f;
                        continue;
                }
                if (*++f == '%')
                {
                        into[out++] = '%';
                        continue;
                }
                if (*f >= '1' && *f <= '9' && f[1] == '$')
                        index = (unsigned int)(*f - '0'), f += 2;
                else
                        index = ++implicit;
                if (!*f)
                        break;
                if (index < 1 || index > used)
                        continue;
                if (*f == 's')
                {
                        for (const char address_to s = argument[index - 1].string; s && *s && out + 1 < room; s++)
                                into[out++] = *s;
                }
                else
                {
                        char digits[24];
                        unsigned int count = 0;
                        long number = argument[index - 1].number;
                        unsigned long magnitude = number < 0 ? 0ul - (unsigned long)number : (unsigned long)number;

                        do
                                digits[count++] = (char)('0' + magnitude % 10), magnitude /= 10;
                        while (magnitude);
                        if (number < 0 && out + 1 < room)
                                into[out++] = '-';
                        while (count && out + 1 < room)
                                into[out++] = digits[--count];
                }
        }
        // A text cut at the room's end has no half of a character left at its end: the lead byte of a sequence that did not all fit goes.
        {
                positive back = out;

                while (back && out - back < 3 && (((unsigned char)into[back - 1]) & 0xc0) == 0x80)
                        back--;
                if (back && (unsigned char)into[back - 1] >= 0xc0)
                {
                        unsigned char lead = (unsigned char)into[back - 1];

                        if (back - 1 + (lead >= 0xf0 ? 4u : lead >= 0xe0 ? 3u : 2u) > out)
                                out = back - 1;
                }
        }
        into[out] = 0;
        return out;
}

#endif
