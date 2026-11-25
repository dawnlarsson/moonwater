/*
        Software: the apps of the bowls, where they come from and the package managers they come through, as a page of the kit
        (ui_page): a column of three groups and their rows.

                Search                  what the sources offer for the words in the search field (Enter), each result with
                                        Install, or Remove where it is installed
                Installed apps          every app the bowls have, each with Remove
                Package managers        the sources (Flatpak, pacman, apk, apt, dnf, Nix's bowl, Snap): each one's state and its
                                        bowl's size, with Set up or Update, and the bowl's removal in its menu and on Delete
                Default apps            what opens a page, mail, a folder, text, images, video, music and documents (moonwater
                                        defaults): each kind's app, and pressed, every app that opens it to choose from, or Automatic

        Every list and every change is moonwater apps (src/sh/host/apps.c), run as a job of the kit's: its lines, a field a tab
        apart, are taken as they come and made rows, so a search's results come in source by source and a list has no most; a
        change says its last line as it goes and its end in the status at the foot. A removal is asked twice (the kit's confirm).
        One job runs at a time: a press while one runs says what is running.

            software [-f FONT.ttf] [-b BOLD.ttf] [-m MOONWATER]

        One window: a second start finds the first and lifts it.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_SOFTWARE
#define MOONWATER_CANVAS_SOFTWARE

#define SOFTWARE_WIDTH 880   // the window's first size, logical pixels
#define SOFTWARE_HEIGHT 620
#define SOFTWARE_BUFFER 65536 // the job's buffer: its lines are taken out as they come, so it holds a line, never a list
#define SOFTWARE_FIELDS 8     // a line's fields (six at most from apps.c) and the two the app makes of them
#define SOFTWARE_STATUS 512   // the status line's bytes
#define SOFTWARE_TEXT 256     // the words searched for

enum
{
        SOFTWARE_SEARCH,
        SOFTWARE_INSTALLED,
        SOFTWARE_SOURCES,
        SOFTWARE_DEFAULTS,
        SOFTWARE_GROUPS,
        SOFTWARE_CHOICES = SOFTWARE_GROUPS // the apps that open the kind whose menu is open (moonwater defaults KIND): no group of the page
};

// What a job is: a group's rows read, or a change (and which, for the words its end is said in).
enum
{
        SOFTWARE_JOB_NONE,
        SOFTWARE_JOB_LIST,
        SOFTWARE_JOB_INSTALL,
        SOFTWARE_JOB_REMOVE,
        SOFTWARE_JOB_SETUP,
        SOFTWARE_JOB_UPDATE,
        SOFTWARE_JOB_TAKE_AWAY,
        SOFTWARE_JOB_CHOICES, // the apps that open a kind read, for its row's menu
        SOFTWARE_JOB_DEFAULT  // a kind's app chosen
};

// A row: its line's fields, each an offset into its group's text (a tab of the line made its end).
struct software_row
{
        p32 field[SOFTWARE_FIELDS];
        p8 fields;
        b32 tried;             // its icon looked for (a search's result: ui_icon_picture_make)
        int side;              // the side its icon is kept at, device pixels (the row's symbol's), 0 none
        unsigned int address_to icon;
};

// A group's rows and the bytes of their lines, both grown as lines come (ui_map'd, doubled).
struct software_rows
{
        struct software_row address_to row;
        int count, room;
        char address_to text;
        positive used, size;
        b32 read; // listed at least once since the window opened
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct ui_page page;
        struct ui_list list;
        struct ui_popup popup;
        struct ui_view view;
        struct ui_job job;
        struct ui_text search;
        struct ui_confirm confirm;
        b32 search_open;
        int width, height, shown;
        unsigned int ground;
        b32 focused;
        struct software_rows rows[SOFTWARE_GROUPS + 1]; // each group's, and SOFTWARE_CHOICES
        int choosing; // the Default apps row whose apps are read for its menu, -1 none
        int job_kind, job_group; // the job's kind, and the group whose rows its lines are (a list's)
        char job_name[SOFTWARE_TEXT]; // what a change is of: a package's, an app's or a source's name
        char buf[SOFTWARE_BUFFER];
        char status[SOFTWARE_STATUS];
        char searched[SOFTWARE_TEXT];
        string_address moonwater;
        int again; // a group whose rows are to be read again once the job running ends (plus one; 0 none)
        int icon_side; // the side a row's symbol was last drawn at, device pixels: a search's icons are made at it
        positive pictures_seen; // the registry's count of pictures made, as the rows were last drawn with them
} software;

static struct ui_app software_app;

/* ---- the rows ---- */

static string_address software_field(int group, int id, int k)
{
        const struct software_rows address_to r = &software.rows[group];

        return id >= 0 && id < r->count && k < r->row[id].fields ? (string_address)(r->text + r->row[id].field[k]) : (string_address) "";
}

static b32 software_grow(void address_to address_to block, positive address_to size, positive need, positive first)
{
        positive next = *size ? *size : first;
        long mapped;

        if (need <= *size)
                return true;
        while (next < need)
                next *= 2;
        mapped = ui_map(next);
        if (mapped < 0 && mapped > -4096)
                return false;
        if (*block)
                memory_copy((void address_to)mapped, *block, *size), system_call_2(syscall(munmap), (positive)*block, *size);
        *block = (void address_to)mapped, *size = next;
        return true;
}

static fn software_rows_drop(struct software_rows address_to r)
{
        for (int at = 0; at < r->count; at++)
                if (r->row[at].icon)
                        memory_free(r->row[at].icon, (positive)r->row[at].side * r->row[at].side * 4), r->row[at].icon = null;
        r->count = 0, r->used = 0;
}

// A text kept with the group's (the fields the app makes of a line), its offset given back, or 0 for none (the empty text).
static p32 software_keep(struct software_rows address_to r, const char address_to text, positive length)
{
        p32 at;

        if (!software_grow((void address_to address_to)&r->text, &r->size, r->used + length + 2, 65536))
                return 0;
        if (!r->used)
                r->text[r->used++] = 0; // offset 0 is the empty text
        at = (p32)r->used;
        memory_copy(r->text + r->used, text, length), r->used += length, r->text[r->used++] = 0;
        return at;
}

// A line of apps.c made a row of the group: its fields split at the tabs; a line with fewer than four is no row (a refusal).
static b32 software_line(int group, const char address_to line, positive length)
{
        struct software_rows address_to r = &software.rows[group];
        struct software_row address_to row;
        positive start = 0, fields = 1;
        p32 base;

        for (positive at = 0; at < length; at++)
                fields += line[at] == '\t';
        if (fields < (group == SOFTWARE_CHOICES ? 2u : 4u))
                return false;
        {
                positive room = (positive)r->room * sizeof(struct software_row);

                if (!software_grow((void address_to address_to)&r->row, &room, ((positive)r->count + 1) * sizeof(struct software_row), 4096))
                        return false;
                r->room = (int)(room / sizeof(struct software_row));
        }
        if (!(base = software_keep(r, line, length)))
                return false;
        row = &r->row[r->count];
        memory_zero(row, sizeof(*row));
        for (positive at = 0; at <= length && row->fields < SOFTWARE_FIELDS - 2; at++)
                if (at == length || line[at] == '\t')
                {
                        r->text[base + at] = 0;
                        row->field[row->fields++] = base + (p32)start;
                        start = at + 1;
                }
        r->count++;
        return true;
}

/* ---- what each row shows ---- */

static b32 software_busy(void) { return software.job.child > 0; }

static b32 software_is(int group, int id, int k, const char address_to word)
{
        return string_equals(software_field(group, id, k), (string_address)word);
}

static const char address_to software_state_words(int id)
{
        if (software_is(SOFTWARE_SOURCES, id, 1, "ready"))
                return WORDS(software_ready);
        if (software_is(SOFTWARE_SOURCES, id, 1, "unavailable"))
                return WORDS(software_unavailable);
        if (software_is(SOFTWARE_SOURCES, id, 1, "other"))
                return WORDS(software_other);
        return WORDS(software_not_set_up);
}

// A source's size, KiB in its fourth field, said in the kit's words for a size (or none where apps.c said none).
static const char address_to software_size_words(int id, char address_to into, positive room)
{
        positive used = 0;
        string_address kib = software_field(SOFTWARE_SOURCES, id, 3);
        positive value = kib[0] >= '0' && kib[0] <= '9' ? string_digits_max(kib, 20, &used) : 0;

        if (!value)
                return null;
        ui_size_text(into, room, (p64)value * 1024);
        return into;
}

/*
        The icons. An installed app's is its entry's in the kit's registry (the entry of its id in its bowl: the worker makes it off the
        draw and keeps it across processes). A search's result is no entry yet: its icon is looked for by its name in its bowl as an
        entry's would be (a Flatpak app's in Flathub's AppStream, by its id; a package installed in its bowl's themes), a few rows a
        pass in the tick, and kept at the side the row draws it at. A row without one has the tile lettered with its name.
*/
static int software_app_of(int id)
{
        string_address want = software_field(SOFTWARE_INSTALLED, id, 1), path = software_field(SOFTWARE_INSTALLED, id, 5);

        for (int at = 0; at < ui_apps.count; at++)
        {
                positive length = string_length((string_address)ui_apps.app[at].root);

                if (length && string_equals((string_address)ui_apps.app[at].id, want) && !memory_compare(path, ui_apps.app[at].root, length) &&
                    path[length] == '/')
                        return at;
        }
        return ui_apps_find((const char address_to)want);
}

static const unsigned int address_to software_installed_picture(int id, int side) { return ui_apps_picture_kept(software_app_of(id), side); }

static const unsigned int address_to software_found_picture(int id, int side)
{
        const struct software_rows address_to r = &software.rows[SOFTWARE_SEARCH];

        if (side > 0 && side != software.icon_side)
                software.icon_side = side; // the tick makes them again at this side
        return id >= 0 && id < r->count && r->row[id].icon && r->row[id].side == side ? r->row[id].icon : null;
}

// The next rows' icons of the search's results, made at the side a row draws its symbol at: whether one was made (the page draws again).
#define SOFTWARE_ICONS_A_PASS 4
static b32 software_icons_make(void)
{
        struct software_rows address_to r = &software.rows[SOFTWARE_SEARCH];
        int side = software.icon_side > 0 ? software.icon_side : ui_dev(&software.ui, 20), made = 0, looked = 0;

        for (int at = 0; at < r->count && looked < SOFTWARE_ICONS_A_PASS; at++)
        {
                struct software_row address_to row = &r->row[at];
                string_address source = (string_address)(r->text + row->field[0]);
                char root[64];
                unsigned int address_to picture;
                int got = 0;

                if (row->tried && (!row->icon || row->side == side))
                        continue;
                if (row->icon)
                        memory_free(row->icon, (positive)row->side * row->side * 4), row->icon = null;
                row->tried = true, row->side = 0, looked++;
                // A Flatpak app's bowl is Arch's; a package's is its source's.
                string_copy_bounded((string_address)root, (string_address) "/bowls/", sizeof(root));
                string_append_bounded((string_address)root, string_equals(source, (string_address) "flatpak") ? (string_address) "arch" : source, sizeof(root));
                if (!(picture = ui_icon_picture_make(root, (const char address_to)(r->text + row->field[1]), &got)) || got <= 0)
                        continue;
                if ((row->icon = (unsigned int address_to)memory_checked((positive)side * side * 4)))
                        ui_picture_scale(row->icon, side, picture, got), row->side = side, made++;
                ui_icon_picture_free(picture);
        }
        return made != 0;
}

// A kind of moonwater defaults by its word, in the language.
static const char address_to software_kind_words(string_address kind)
{
        static const struct
        {
                const char address_to kind;
                p32 words;
        } kinds[] = {{"browser", LANGUAGE_software_kind_browser}, {"mail", LANGUAGE_software_kind_mail}, {"folders", LANGUAGE_software_kind_folders},
                     {"text", LANGUAGE_software_kind_text}, {"images", LANGUAGE_software_kind_images}, {"video", LANGUAGE_software_kind_video},
                     {"music", LANGUAGE_software_kind_music}, {"documents", LANGUAGE_software_kind_documents}};

        for (positive at = 0; at < sizeof(kinds) / sizeof(kinds[0]); at++)
                if (string_equals(kind, (string_address)kinds[at].kind))
                        return language_text(kinds[at].words);
        return (const char address_to)kind;
}

static fn software_describe(int id, struct ui_row address_to row)
{
        int group = software.page.chosen;
        static char size[SOFTWARE_GROUPS][64];

        if (group == SOFTWARE_SEARCH)
        {
                b32 installed = software_is(group, id, 3, "installed");

                row->label = (const char address_to)software_field(group, id, 1);
                row->about = (const char address_to)software_field(group, id, 4);
                row->note = (const char address_to)software_field(group, id, 0);
                row->value = (const char address_to)software_field(group, id, 2);
                row->kind = UI_ROW_BUTTON, row->words = installed ? WORDS(software_remove) : WORDS(software_install);
                row->mark = installed, row->dim = software_busy();
                row->tile_picture = software_found_picture, row->tile_name = row->label;
                return;
        }
        if (group == SOFTWARE_INSTALLED)
        {
                row->label = (const char address_to)software_field(group, id, 4);
                row->about = (const char address_to)software_field(group, id, 1);
                row->note = (const char address_to)software_field(group, id, 0);
                row->kind = UI_ROW_BUTTON, row->words = WORDS(software_remove), row->dim = software_busy();
                row->remove = true;
                ui_apps_symbol(software_app_of(id), row), row->tile_picture = software_installed_picture;
                return;
        }
        // A kind: what opens it (No app where nothing does), Automatic where the person chose none, and pressed, the apps to choose from.
        if (group == SOFTWARE_DEFAULTS)
        {
                b32 none = software_is(group, id, 1, "-");

                row->label = software_kind_words(software_field(group, id, 0));
                row->value = none ? WORDS(software_no_app) : (const char address_to)software_field(group, id, 2);
                row->note = !none && software_is(group, id, 3, "found") ? WORDS(software_automatic) : null;
                row->kind = UI_ROW_CHOICE, row->dim = software_busy() || none;
                return;
        }
        // A source: its label, its state and its bowl's size; Set up where it is not, Update where it is ready.
        row->label = (const char address_to)software_field(group, id, 4);
        row->about = software_size_words(id, size[0], sizeof(size[0]));
        row->note = software_state_words(id);
        if (software_is(group, id, 1, "unavailable"))
                return (void)(row->dim = true);
        row->kind = UI_ROW_BUTTON, row->dim = software_busy();
        row->words = software_is(group, id, 1, "ready") ? WORDS(software_update) : software_is(group, id, 1, "other") ? WORDS(software_take_away)
                                                                                                                  : WORDS(software_set_up);
        // The bowl's removal: in the row's menu and on Delete, for a source whose bowl is there.
        row->remove = !software_is(group, id, 1, "not-set-up");
}

static fn software_rebuild(void)
{
        const struct software_rows address_to r = &software.rows[software.page.chosen];

        software.list.ids = r->count;
        ui_list_begin(&software.list);
        for (int id = 0; id < r->count; id++)
                ui_list_add(&software.list, id);
        ui_list_end(&software.list);
        software.list.empty = software.page.chosen == SOFTWARE_SEARCH && !software.searched[0] ? WORDS(software_search_first)
                              : software_busy() && software.job_group == software.page.chosen ? WORDS(software_reading)
                              : software.page.chosen == SOFTWARE_INSTALLED                   ? WORDS(software_none_installed)
                                                                                             : WORDS(software_nothing);
        if (software.width > 0)
                ui_page_fit(&software.ui, &software.page, &software.list, software.width, software.height, &software.page.column_scroll, 0);
        ui_page_redraw(&software.page);
}

/* ---- the jobs ---- */

static fn software_say(const char address_to text)
{
        positive at = 0;

        // What a command said, less its terminal's colours (the label's bold), and less its newline.
        for (positive from = 0; text[from] && at + 1 < sizeof(software.status); from++)
                if (text[from] == 0x1b)
                {
                        while (text[from + 1] && !((text[from + 1] >= 'a' && text[from + 1] <= 'z') || (text[from + 1] >= 'A' && text[from + 1] <= 'Z')))
                                from++;
                        if (text[from + 1])
                                from++;
                }
                else if (text[from] != '\n' && text[from] != '\r')
                        software.status[at++] = text[from];
        software.status[at] = 0;
        ui_page_redraw(&software.page);
}

// The job's lines as they come: a list's made rows, a change's last one said.
static fn software_job_data(struct ui_job address_to j)
{
        positive start = 0;
        b32 added = false;

        for (positive at = 0; at < j->length; at++)
                if (j->buf[at] == '\n')
                {
                        j->buf[at] = 0;
                        if ((software.job_kind == SOFTWARE_JOB_LIST || software.job_kind == SOFTWARE_JOB_CHOICES) &&
                            software_line(software.job_group, j->buf + start, at - start))
                                added = true;
                        else if (at > start)
                                software_say(j->buf + start);
                        start = at + 1;
                }
        memory_copy(j->buf, j->buf + start, j->length - start); // memory_copy is memmove: the rest moves down over what was taken
        j->length -= start, j->buf[j->length] = 0;
        if (added && software.job_group == software.page.chosen)
                software_rebuild();
}

static fn software_list(int group);
static fn software_choices_open(void);

static fn software_job_done(struct ui_job address_to j, b32 killed, positive status)
{
        int kind = software.job_kind, group = software.job_group;
        b32 failed = killed || wait_status_code(status);
        char line[SOFTWARE_STATUS];

        if (j->length)
                j->buf[j->length] = '\n', j->length++, software_job_data(j);
        software.job_kind = SOFTWARE_JOB_NONE;
        if (kind == SOFTWARE_JOB_CHOICES)
        {
                if (!failed && software.page.chosen == SOFTWARE_DEFAULTS)
                        software_choices_open();
                else
                        software.choosing = -1;
        }
        else if (kind == SOFTWARE_JOB_DEFAULT)
        {
                if (!failed)
                        software.status[0] = 0;
                software.rows[SOFTWARE_DEFAULTS].read = false, software.again = SOFTWARE_DEFAULTS + 1;
        }
        else if (kind == SOFTWARE_JOB_LIST)
        {
                software.rows[group].read = true;
                if (!failed || software.rows[group].count)
                        software.status[0] = software.status[0] && failed ? software.status[0] : 0;
        }
        else
        {
                const char address_to done = kind == SOFTWARE_JOB_INSTALL ? WORDS(software_installed_one)
                                             : kind == SOFTWARE_JOB_REMOVE ? WORDS(software_removed_one)
                                             : kind == SOFTWARE_JOB_SETUP  ? WORDS(software_set_up_one)
                                             : kind == SOFTWARE_JOB_UPDATE ? WORDS(software_updated_one)
                                                                           : WORDS(software_gone_one);
                if (failed)
                        language_format(line, sizeof(line), WORDS(software_failed_one), (long)software.job_name, (long)software.status);
                else
                        language_format(line, sizeof(line), done, (long)software.job_name);
                software_say(line);
                // What the change moved: every group is read again, the one shown first, and the registry where its folders changed.
                for (int g = 0; g < SOFTWARE_GROUPS; g++)
                        software.rows[g].read = false;
                ui_apps_fresh((b64)ui_kit_time());
                software.again = software.page.chosen + 1;
        }
        if (software.again && !software_busy())
        {
                int next = software.again - 1;

                software.again = 0;
                software_list(next);
        }
        software_rebuild();
}

static fn software_job_ready(void) { software.job.done = software_job_done, software.job.data = software_job_data; }

static b32 software_start(string_address address_to words, positive count, int kind, int group)
{
        if (software_busy())
                return software_say(WORDS(software_wait)), false;
        software_job_ready();
        software.job_kind = kind, software.job_group = group;
        if (!ui_job_start(&software.job, words, count, null, software.buf, sizeof(software.buf), 0))
                return software.job_kind = SOFTWARE_JOB_NONE, software_say(WORDS(software_cannot_start)), false;
        return true;
}

// A group's rows read again: moonwater apps for the apps installed, apps sources, apps search for what was searched.
static fn software_list(int group)
{
        string_address words[5] = {software.moonwater, (string_address) "apps", null, null, null};
        positive count = 2;

        if (software_busy())
        {
                software.again = group + 1;
                return;
        }
        if (group == SOFTWARE_SEARCH)
        {
                if (!software.searched[0])
                        return software_rows_drop(&software.rows[group]), software_rebuild();
                words[2] = (string_address) "search", words[3] = (string_address)software.searched, count = 4;
        }
        else if (group == SOFTWARE_SOURCES)
                words[2] = (string_address) "sources", count = 3;
        else if (group == SOFTWARE_DEFAULTS)
                words[1] = (string_address) "defaults", count = 2;
        software_rows_drop(&software.rows[group]);
        if (software_start(words, count, SOFTWARE_JOB_LIST, group))
                software_rebuild();
}

// A change of a row: its words run, what it is of kept for the status (the row's name), the job's progress said as it comes.
static fn software_change(int kind, string_address verb, string_address source, string_address name, b32 yes)
{
        string_address words[7] = {software.moonwater, (string_address) "apps", null, null, null, null, null};
        positive count;
        char line[SOFTWARE_STATUS];

        if (kind == SOFTWARE_JOB_SETUP || kind == SOFTWARE_JOB_TAKE_AWAY)
                words[2] = (string_address) "source", words[3] = verb, words[4] = name, words[5] = yes ? (string_address) "yes" : null, count = yes ? 6 : 5;
        else if (kind == SOFTWARE_JOB_UPDATE)
                words[2] = verb, words[3] = source, count = 4;
        else
                words[2] = verb, words[3] = source, words[4] = name, count = 5;
        string_copy_bounded((string_address)software.job_name, name, sizeof(software.job_name));
        if (!software_start(words, count, kind, software.page.chosen))
                return;
        language_format(line, sizeof(line),
                        kind == SOFTWARE_JOB_INSTALL  ? WORDS(software_installing)
                        : kind == SOFTWARE_JOB_REMOVE ? WORDS(software_removing)
                        : kind == SOFTWARE_JOB_SETUP  ? WORDS(software_setting_up)
                        : kind == SOFTWARE_JOB_UPDATE ? WORDS(software_updating)
                                                      : WORDS(software_taking_away),
                        (long)name);
        software_say(line);
        software_rebuild();
}

/* ---- what a row does ---- */

// A removal is asked twice: the first press says what the second does.
static b32 software_confirmed(int what, const char address_to format, string_address name)
{
        char text[SOFTWARE_STATUS], line[SOFTWARE_STATUS];

        language_format(text, sizeof(text), format, (long)name);
        if (ui_confirm_ask(&software.confirm, what, text))
                return true;
        ui_confirm_line(line, sizeof(line), text);
        software_say(line);
        return false;
}

static fn software_take_away(int id)
{
        string_address name = software_field(SOFTWARE_SOURCES, id, 0);

        if (software_is(SOFTWARE_SOURCES, id, 1, "not-set-up") || software_is(SOFTWARE_SOURCES, id, 1, "unavailable"))
                return;
        if (software_confirmed(id, WORDS(software_confirm_take_away), name))
                software_change(SOFTWARE_JOB_TAKE_AWAY, (string_address) "remove", null, name, true);
}

static fn software_remove(int group, int id)
{
        string_address source = software_field(group, id, 0), name = software_field(group, id, 1);

        if (software_confirmed(id + group * 0x100000, WORDS(software_confirm_remove), name))
                software_change(SOFTWARE_JOB_REMOVE, (string_address) "remove", source, name, false);
}

static fn software_act(int id, int way)
{
        int group = software.page.chosen;

        (void)way;
        if (id < 0 || id >= software.rows[group].count)
                return;
        if (software_busy())
                return software_say(WORDS(software_wait));
        if (group == SOFTWARE_DEFAULTS)
        {
                string_address words[4] = {software.moonwater, (string_address) "defaults", software_field(group, id, 0), null};

                ui_confirm_drop(&software.confirm);
                software_rows_drop(&software.rows[SOFTWARE_CHOICES]);
                software.choosing = id;
                if (!software_start(words, 3, SOFTWARE_JOB_CHOICES, SOFTWARE_CHOICES))
                        software.choosing = -1;
                return;
        }
        if (group == SOFTWARE_SEARCH && !software_is(group, id, 3, "installed"))
                return ui_confirm_drop(&software.confirm),
                       software_change(SOFTWARE_JOB_INSTALL, (string_address) "install", software_field(group, id, 0), software_field(group, id, 1), false);
        if (group != SOFTWARE_SOURCES)
                return software_remove(group, id);
        ui_confirm_drop(&software.confirm);
        if (software_is(group, id, 1, "ready"))
                return software_change(SOFTWARE_JOB_UPDATE, (string_address) "update", software_field(group, id, 0), software_field(group, id, 0), false);
        if (software_is(group, id, 1, "other"))
                return software_take_away(id);
        if (!software_is(group, id, 1, "unavailable"))
                software_change(SOFTWARE_JOB_SETUP, (string_address) "setup", null, software_field(group, id, 0), false);
}

/*
        A kind's menu, its apps read (moonwater defaults KIND: a handler, a tab, its name, and * for the one that opens it): each app by
        its name, then Automatic; the one ticked is the person's choice, or Automatic where they chose none. A choice is moonwater
        defaults KIND HANDLER (auto for Automatic).
*/
static const char address_to software_choice_label(int row, int k)
{
        const struct software_rows address_to r = &software.rows[SOFTWARE_CHOICES];

        (void)row;
        return k < r->count ? (const char address_to)software_field(SOFTWARE_CHOICES, k, 1) : k == r->count ? WORDS(software_automatic) : null;
}

static fn software_choices_open(void)
{
        int id = software.choosing;
        const char address_to current = software_is(SOFTWARE_DEFAULTS, id, 3, "chosen") ? (const char address_to)software_field(SOFTWARE_DEFAULTS, id, 2)
                                                                                         : WORDS(software_automatic);

        if (id < 0 || !ui_page_menu_open(&software.ui, &software.page, &software.list, &software.popup, &software.own, id, software_choice_label, current))
                software.choosing = -1;
        ui_page_redraw(&software.page);
}

static fn software_choice_take(int k)
{
        int id = software.choosing;
        const struct software_rows address_to r = &software.rows[SOFTWARE_CHOICES];
        string_address words[5] = {software.moonwater, (string_address) "defaults", software_field(SOFTWARE_DEFAULTS, id, 0), null, null};

        software.choosing = -1;
        ui_page_menu_close(&software.page, &software.popup);
        if (id < 0 || k < 0 || k > r->count)
                return;
        words[3] = k == r->count ? (string_address) "auto" : software_field(SOFTWARE_CHOICES, k, 0);
        software_start(words, 4, SOFTWARE_JOB_DEFAULT, SOFTWARE_DEFAULTS);
        software_rebuild();
}

// The kit's management of the rows: a row's Remove (its menu, or Delete on it).
static fn software_managed(void)
{
        int id = software.list.removed - 1;

        if (!software.list.removed)
                return;
        software.list.removed = 0;
        if (software_busy())
                return software_say(WORDS(software_wait));
        if (software.page.chosen == SOFTWARE_SOURCES)
                return software_take_away(id);
        if (software.page.chosen == SOFTWARE_INSTALLED || software_is(software.page.chosen, id, 3, "installed"))
                software_remove(software.page.chosen, id);
}

/* ---- the search field ---- */

static struct ui_text address_to software_search_field(void)
{
        if (!software.search_open)
                ui_field_open(&software.search, &software.own, ""), software.search.most = SOFTWARE_TEXT - 1, software.search.keeps_vertical = true,
                    software.search_open = true;
        return &software.search;
}

// Enter in the field: what it holds is searched for, on the Search page.
static fn software_search_asked(p32 asks)
{
        p8 typed[SOFTWARE_TEXT];
        positive length;

        if (asks & UI_TEXT_REFUSED)
                software_say(software.search.refused ? software.search.refused : "");
        if (!(asks & UI_TEXT_SUBMIT))
                return ui_page_redraw(&software.page);
        length = ui_text_search(software_search_field(), typed, sizeof(typed));
        while (length && typed[length - 1] == ' ')
                typed[--length] = 0;
        if (!length)
                return;
        string_copy_bounded((string_address)software.searched, (string_address)typed, sizeof(software.searched));
        software.page.chosen = SOFTWARE_SEARCH, software.page.pane = 1;
        ui_page_column_show(&software.ui, &software.page);
        software_list(SOFTWARE_SEARCH);
}

static b32 software_search_starts(const struct window_event address_to e)
{
        switch (e->a)
        {
        case UI_KEY_ESCAPE: case UI_KEY_TAB: case UI_KEY_ENTER: case UI_KEY_ENTER_PAD:
        case UI_KEY_HOME: case UI_KEY_UP: case UI_KEY_PAGE_UP: case UI_KEY_LEFT: case UI_KEY_RIGHT:
        case UI_KEY_END: case UI_KEY_DOWN: case UI_KEY_PAGE_DOWN: case UI_KEY_DELETE: return false;
        }
        return e->b > ' ';
}

static b32 software_search_typed(void) { return software.search_open && doc_length(&software.search.doc) != 0; }

/* ---- the page ---- */

static const char address_to software_group_name(int group)
{
        return group == SOFTWARE_SEARCH      ? WORDS(software_search)
               : group == SOFTWARE_INSTALLED ? WORDS(software_installed)
               : group == SOFTWARE_SOURCES   ? WORDS(software_sources)
                                             : WORDS(software_defaults);
}

static fn software_group_choose(int group)
{
        if (group < 0 || group >= SOFTWARE_GROUPS)
                return;
        ui_confirm_drop(&software.confirm);
        software.page.chosen = group, software.list.focus = -1;
        ui_scroll_set(&software.list.scroll, 0);
        if (!software.rows[group].read && !(software_busy() && software.job_group == group && software.job_kind == SOFTWARE_JOB_LIST))
                software_list(group);
        software_rebuild();
}

static fn software_layout(void)
{
        software.page.column_on = true, software.page.body = &software.list.scroll;
        software.page.view = &software.view, software.page.popup = &software.popup, software.page.own = &software.own;
        software.page.column.count = SOFTWARE_GROUPS;
        ui_page_groups(&software.ui, &software.page, software_group_name, SOFTWARE_GROUPS);
        ui_page_fit(&software.ui, &software.page, &software.list, software.width, software.height, &software.page.column_scroll, 0);
}

static fn software_said(int before) { (void)before; ui_confirm_drop(&software.confirm); }
static fn software_forget(void)
{
        if (software.list.focus >= 0)
                software.list.removed = software.list.focus + 1, software_managed();
}

static const struct ui_page_keys software_keys = {.act = software_act, .said = software_said, .group = software_group_choose, .forget = software_forget};

static b32 software_key(const struct window_event address_to event)
{
        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;
        if (software.popup.open)
        {
                int answer = ui_popmenu_event(&software.ui, &software.popup.menu, event, 0, 0);

                if (software.page.menu_row >= 0 && answer >= 0)
                        software_choice_take(answer);
                else if (software.page.menu_row >= 0 && answer == UI_MENU_GONE)
                        software.choosing = -1, ui_page_menu_close(&software.page, &software.popup);
                return ui_page_redraw(&software.page), true;
        }
        if (ui_page_context_key(&software.ui, &software.page, &software.list, event))
                return true;
        if (software_search_typed() || software_search_starts(event) || (software.search.focused && event->a != UI_KEY_TAB))
        {
                struct ui_text address_to field = software_search_field();
                p32 asks;

                ui_field_focus(field, true);
                asks = ui_field_event(&software.ui, field, &software.popup, event, 0, 0);
                if (asks & UI_TEXT_CANCEL)
                        return ui_text_set(field, (const p8 address_to) "", 0), ui_field_focus(field, false), ui_page_redraw(&software.page), true;
                software_search_asked(asks);
                if (asks)
                        return true;
        }
        if (event->a == UI_KEY_ESCAPE)
                return software_say(""), ui_confirm_drop(&software.confirm), true;
        if (software.page.pane && ui_list_manage_key(&software.ui, &software.list, event))
                return software_managed(), ui_page_redraw(&software.page), true;
        if (ui_page_key(&software.ui, &software.page, &software.list, (int)event->a, &software_keys))
                return ui_page_redraw(&software.page), true;
        return true;
}

static fn software_press_at(void)
{
        int x = ui_kit.pointer_x, y = ui_kit.pointer_y, row;

        if ((row = ui_page_column_hit(&software.ui, &software.page, x, y)) >= 0)
                return software.page.pane = 0, software_group_choose(row), ui_page_redraw(&software.page);
        if ((row = ui_list_hit(&software.ui, &software.list, x, y)) < 0)
                return;
        software.page.pane = 1;
        if (software.search_open)
                ui_field_focus(&software.search, false);
        software_act(row, 1);
        ui_page_redraw(&software.page);
}

static b32 software_event(const struct window_event address_to event)
{
        if ((event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON) && (event->type != WINDOW_EVENT_BUTTON || event->a == UI_POINTER_LEFT) &&
            ui_page_bars(&software.ui, &software.page, (int)event->type, (event->flags & WINDOW_KEY_DOWN) != 0))
                return true;
        if (event->type != WINDOW_EVENT_KEY && event->type != WINDOW_EVENT_WHEEL && software.page.menu_row < 0)
        {
                p32 asks = ui_field_event(&software.ui, software_search_field(), &software.popup, event, ui_kit.pointer_x, ui_kit.pointer_y);

                if (asks && (event->type != WINDOW_EVENT_BUTTON || (asks != UI_TEXT_COMMIT && asks != UI_TEXT_CANCEL)))
                        return software_search_asked(asks), true;
        }
        switch (event->type)
        {
        case WINDOW_EVENT_KEY: return software_key(event);
        case WINDOW_EVENT_MOTION:
        case WINDOW_EVENT_BUTTON: ui_page_pointer(&software.ui, &software.page, &software.list, event, software_press_at), software_managed(); break;
        case WINDOW_EVENT_WHEEL: ui_page_wheel(&software.ui, &software.page, event); break;
        }
        return true;
}

static fn software_paint(unsigned int address_to pixels, int band_top, int band_bottom)
{
        struct ui address_to ui = &software.ui;
        struct ui_page_parts parts;

        ui_paint_into(ui, pixels, software.own.table->stride, software.width, software.height, band_top, band_bottom);
        software.page.title = software.status[0] ? software.status : WORDS(software_title);
        software.page.search.items[0] = WORDS(software_search_hint);
        software.page.search.edit = software_search_field();
        memory_zero(&parts, sizeof(parts));
        parts.keys = software.search.focused && software.focused;
        parts.group_name = software_group_name, parts.groups = SOFTWARE_GROUPS;
        parts.column_keys = software.focused && !software.page.pane;
        parts.list_lit = software.page.pane != 0, parts.list_keys = software.focused && software.page.pane && !software.search.focused;
        parts.list = &software.list;
        ui_page_paint(ui, &software.page, &parts);
}

static b32 software_frame(void)
{
        if (!ui_buffers_present(&software.page.buffers, &software.own, software.width, software.height, software_paint, null))
                return false;
        return software.page.dirty = false, true;
}

static fn software_size(unsigned int width, unsigned int height)
{
        struct windows address_to t = software.own.table;

        software.width = (int)(width > t->output_width ? t->output_width : width);
        software.height = (int)(height > t->output_height ? t->output_height : height);
        ui_scratch_fit(&software.ui, (unsigned int)software.width);
        software_layout(), ui_page_column_show(&software.ui, &software.page), ui_page_redraw(&software.page);
}

static long software_tick(long timeout, long address_to other)
{
        b32 taken = false;

        (void)other;
        if (software.search_open)
                timeout = ui_field_tick(&software.ui, &software.page, &software.search, timeout, software.focused);
        // The installed apps' pictures as the worker makes them: whoever takes one in (this tick, or the registry's own read), the
        // count of pictures made moves, and the rows are drawn again.
        timeout = ui_apps_pictures_tick(timeout, false, &taken);
        if (ui_apps_pictures_made != software.pictures_seen)
                software.pictures_seen = ui_apps_pictures_made, taken = true;
        if (software.page.chosen == SOFTWARE_SEARCH && software_icons_make())
                taken = true, timeout = 0;
        if (taken)
                ui_page_redraw(&software.page);
        return timeout;
}

static fn software_language(void)
{
        if (software.width > 0)
                software_layout(), software_rebuild();
}

static b32 software_focus(b32 focused)
{
        if (!focused)
                ui_page_let_go(&software.ui, &software.page, &software.list), ui_confirm_drop(&software.confirm);
        return true;
}

static fn software_chose(int id)
{
        if (software.page.menu_row >= 0)
                return software_choice_take(id), ui_page_redraw(&software.page);
        if (ui_list_menu_do(&software.popup, &software.own, id))
                return software_managed(), ui_page_redraw(&software.page);
        if (software.popup.field == &software.search)
                software_search_asked(ui_text_menu_do(&software.ui, &software.search, id));
        ui_page_redraw(&software.page);
}

static fn software_close(void)
{
        if (software.search_open)
                ui_field_close(&software.search);
}

static p32 software_menu_flags(int id) { (void)id; return UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW; }
static const char address_to software_copy(int id) { return (const char address_to)software_field(software.page.chosen, id, 1); }
static const struct ui_list_menu software_menu = {.flags = software_menu_flags, .copy = software_copy};

static fn software_changed(int top, int bottom) { ui_page_damage(&software.page, top, bottom); }

static int software_open(void)
{
        struct windows address_to t = software.own.table;
        int width = ui_px(&software.ui, SOFTWARE_WIDTH), height = ui_px(&software.ui, SOFTWARE_HEIGHT);
        int room = (int)t->output_height - ui_px(&software.ui, 2 * (24 + 24));

        if (ui_lift_other(&software.own, (string_address) "software", false))
                return 0;
        software.list.describe = software_describe, software.list.damaged = software_changed, software.list.wrap = true, software.list.menu = &software_menu;
        software.list.hover = software.list.down = software.list.focus = -1;
        software.page.chosen = SOFTWARE_INSTALLED, software.page.menu_row = -1, software.page.pane = 1, software.choosing = -1;
        software_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_free(t, software.own.row, software.width, software.height, ui_px(&software.ui, 12));
        ui_apps_fresh((b64)ui_kit_time());
        software_list(SOFTWARE_INSTALLED);
        software_rebuild();
        software_frame();
        return -1;
}

static struct ui_app software_app = {
    .own = &software.own, .ui = &software.ui,
    .missing = (string_address) "software: no window: the compositor's desktop is not up\n",
    .title = (string_address) "Software", .name = (string_address) "software",
    .glass = 0, .side = true,
    .nofont = (string_address) "software: no font to draw with (-f FONT.ttf), pid ",
    .dirty = &software.page.dirty, .focused = &software.focused, .ground = &software.ground,
    .open = software_open, .frame = software_frame, .language = software_language, .event = software_event, .focus = software_focus,
    .buffers = &software.page.buffers, .size = software_size, .tick = software_tick,
    .job = &software.job, .close = software_close, .popup = &software.popup, .chose = software_chose,
};

static b32 software_main()
{
        memory_zero(&software, sizeof(software));
        software.moonwater = (string_address) "/moonwater", ui_buffers_start(&software.page.buffers);
        software.job.output = -1, software_job_ready();
        for (positive at = 1; at < (positive)program_argument_count();)
        {
                positive used = ui_app_option(&software_app, at);
                string_address value;

                if (used)
                        at += used;
                else if ((value = ui_argument(at, (string_address) "-m")))
                        software.moonwater = value, at += 2;
                else
                        at++;
        }
        return ui_app_run(&software_app);
}

#endif // MOONWATER_CANVAS_SOFTWARE
