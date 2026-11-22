/*
        The installer: six-step graphical Moonwater installer (Language, Keyboard, Network, Disk, Apps, Install).

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_INSTALLER
#define MOONWATER_CANVAS_INSTALLER

#include "installer_model.c"

#define INSTALLER_WIDTH 900
#define INSTALLER_HEIGHT 660
#define INSTALLER_COLUMN 640
#define INSTALLER_STEP_ROW 48
#define INSTALLER_CARD_H 96
#define INSTALLER_LOOK_NS 1000000000ll
#define INSTALLER_WIFI_NS 4000000000ll
#define INSTALLER_SAID 8192

enum
{
        INSTALLER_STEP_LANGUAGE,
        INSTALLER_STEP_KEYBOARD,
        INSTALLER_STEP_NETWORK,
        INSTALLER_STEP_DISK,
        INSTALLER_STEP_APPS,
        INSTALLER_STEP_REVIEW,
        INSTALLER_STEP_RUN,
        INSTALLER_STEP_DONE,
};
#define INSTALLER_STEPS 6

enum
{
        INSTALLER_JOB_NONE,
        INSTALLER_JOB_LIST,
        INSTALLER_JOB_WIFI,
        INSTALLER_JOB_JOIN,
        INSTALLER_JOB_PLAN,
};

static const struct
{
        const char address_to code;
        const char address_to name;
} installer_layouts[] = {
    {"us", "English (US)"}, {"uk", "English (UK)"}, {"de", "Deutsch"},   {"se", "Svenska"},
    {"no", "Norsk"},        {"dk", "Dansk"},        {"fi", "Suomi"},     {"fr", "Fran\xc3\xa7" "ais"},
    {"es", "Espa\xc3\xb1ol"}, {"it", "Italiano"},   {"ar", "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9"},
    {"il", "\xd7\xa2\xd7\x91\xd7\xa8\xd7\x99\xd7\xaa"}, {"ir", "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c"},
};
#define INSTALLER_LAYOUTS (sizeof(installer_layouts) / sizeof(installer_layouts[0]))
#define INSTALLER_LANGUAGES (sizeof(language_names) / sizeof(language_names[0]))

static const struct
{
        unsigned int bit;
        int icon;
        unsigned int title, about;
} installer_apps[3] = {
    {INSTALLER_CHROME, UI_ICON_GLOBE, LANGUAGE_installer_chrome, LANGUAGE_installer_chrome_about},
    {INSTALLER_STEAM, UI_ICON_GAMEPAD, LANGUAGE_installer_steam, LANGUAGE_installer_steam_about},
    {INSTALLER_SSH, UI_ICON_KEY, LANGUAGE_installer_ssh, LANGUAGE_installer_ssh_about},
};

struct installer_big
{
        struct ui_view view;
        struct ui_list list;
        struct ui_rect stepper, side_box, field_box, pass_box, card_box[3], icon_box[3], tick_box[3];
        int step_hover;
        struct ui_text field, pass;
        struct ui_control try_it, pass_c, modes, meter, callout;
        struct ui_tween card_hover[3], card_press[3], card_on[3];
        int hover_card, press_card, focus_card;
        struct installer_drive drives[INSTALLER_DRIVES];
        int drive_count;
        struct installer_network nets[INSTALLER_NETWORKS];
        int net_count, net_chosen;
        b32 wifi_on, wifi_read;
        char said[INSTALLER_SAID];
        positive said_length;
        char last[160], plan[INSTALLER_PLAN], note[160], line[3][160], says[96];
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct installer_big address_to big;
        string_address moonwater;
        int width, height, shown;
        unsigned int ground;
        b32 focused, dirty;
        int step;
        struct installer_choice choice;
        b32 x86, hand, listed, network_seen;
        int chosen;
        union
        {
                struct ui_job runner;
                struct
                {
                        bipolar child;
                        int output;
                };
        };
        int job, at, commands;
        b32 failed;
        b64 next_look, next_wifi;
} installer;

static struct ui_app installer_app;

/* ---- commands and async jobs ---- */

static fn installer_job_data(struct ui_job address_to j);
static fn installer_job_done(struct ui_job address_to j, b32 killed, positive status);

static b32 installer_start(string_address address_to words, positive count, int job, const char address_to input)
{
        installer.runner.data = installer_job_data, installer.runner.done = installer_job_done;
        if (!ui_job_start(&installer.runner, words, count, input, installer.big->said, sizeof(installer.big->said), 0))
                return false;
        installer.job = job, installer.big->said_length = 0;
        return true;
}

static b32 installer_run_at(int at)
{
        char line[256];
        string_address words[8];
        const char address_to c = installer.big->plan;
        positive length = 0, count = 0;

        for (int k = 0; k < at && *c; c++)
                if (*c == '\n')
                        k++;
        while (c[length] && c[length] != '\n' && length + 1 < sizeof(line))
                line[length] = c[length], length++;
        line[length] = 0;
        if (!length)
                return false;
        for (char address_to p = line; *p && count + 1 < 8;)
        {
                while (*p == ' ')
                        *p++ = 0;
                if (!*p)
                        break;
                words[count++] = (string_address)p;
                while (*p && *p != ' ')
                        p++;
        }
        words[count] = null;
        if (!count)
                return false;
        if (string_equals(words[0], (string_address) "/moonwater") && installer.moonwater)
                words[0] = installer.moonwater;
        return installer_start(words, count, INSTALLER_JOB_PLAN, null);
}

static fn installer_last_line(void)
{
        struct installer_big address_to big = installer.big;
        positive stop = big->said_length, start, len;
        const char address_to line;

        while (stop && (big->said[stop - 1] == '\n' || big->said[stop - 1] == ' '))
                stop--;
        start = stop;
        while (start && big->said[start - 1] != '\n')
                start--;
        len = stop - start;
        line = ui_strip_verb_prefix(big->said + start, &len);
        if (len >= sizeof(big->last))
                line += len - (sizeof(big->last) - 1), len = sizeof(big->last) - 1;
        memory_copy(big->last, line, len);
        big->last[len] = 0;
        for (positive k = 0; big->last[k]; k++)
                if ((p8)big->last[k] < 0x20)
                        big->last[k] = ' ';
}

static fn installer_listed(void)
{
        struct installer_big address_to big = installer.big;
        int count = installer_parse(big->said, big->said_length, big->drives, INSTALLER_DRIVES);

        big->drive_count = installer_targets(big->drives, count);
        installer.listed = true;
        installer.chosen = big->drive_count == 1 ? 0 : -1;
        if (installer.chosen == 0)
                string_copy_bounded((string_address)installer.choice.drive, (string_address)big->drives[0].name, sizeof(installer.choice.drive)),
                    installer.choice.bytes = big->drives[0].bytes;
}

static int installer_wifi_parse(const char address_to text, positive length, struct installer_network address_to nets, int most, b32 address_to on)
{
        struct ui_wifi_net raw[INSTALLER_NETWORKS];
        int total = ui_wifi_parse(text, length, raw, most < INSTALLER_NETWORKS ? most : INSTALLER_NETWORKS, on, null, 0), count = 0;

        for (int k = 0; k < total && count < most; k++)
                if (raw[k].flags & UI_WIFI_HEARD)
                {
                        struct installer_network address_to n = &nets[count++];

                        memory_zero(n, sizeof(*n));
                        string_copy_bounded((string_address)n->name, (string_address)raw[k].name, sizeof(n->name));
                        n->bars = raw[k].bars;
                        n->joined = (raw[k].flags & UI_WIFI_JOINED) != 0;
                        n->saved = (raw[k].flags & UI_WIFI_SAVED) != 0;
                        n->open = (raw[k].flags & UI_WIFI_OPEN) != 0;
                }
        return count;
}

static fn installer_heard(void)
{
        struct installer_big address_to big = installer.big;

        big->net_count = installer_wifi_parse(big->said, big->said_length, big->nets, INSTALLER_NETWORKS, &big->wifi_on);
        big->wifi_read = true;
        if (big->net_chosen >= big->net_count)
                big->net_chosen = -1;
}

static fn installer_job_data(struct ui_job address_to j)
{
        installer.big->said_length = j->length;
        if (installer.job == INSTALLER_JOB_PLAN)
                installer_last_line();
        installer.dirty = true;
}

static fn installer_job_done(struct ui_job address_to j, b32 killed, positive status)
{
        b32 ok = !killed && (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 0;
        int job = installer.job;

        installer.big->said_length = j->length;
        installer.job = INSTALLER_JOB_NONE, installer.dirty = true;
        installer_last_line();
        if (job == INSTALLER_JOB_LIST)
                installer_listed();
        else if (job == INSTALLER_JOB_WIFI)
                installer_heard();
        else if (job == INSTALLER_JOB_JOIN)
        {
                if (!ok)
                        string_copy_bounded((string_address)installer.big->note, (string_address)installer.big->last, sizeof(installer.big->note));
                installer.next_wifi = installer.next_look = 0;
        }
        else if (job == INSTALLER_JOB_PLAN)
        {
                if (!ok || (++installer.at < installer.commands && !installer_run_at(installer.at)))
                        installer.failed = true;
                else if (installer.at >= installer.commands)
                        installer.step = INSTALLER_STEP_DONE;
        }
}

static fn installer_list(void)
{
        string_address words[4] = {installer.moonwater, (string_address) "install", (string_address) "list", null};

        if (installer.child > 0)
                return;
        installer.listed = false;
        if (!installer_start(words, 3, INSTALLER_JOB_LIST, null))
                installer.listed = true, installer.big->drive_count = 0;
}

static fn installer_wifi(void)
{
        string_address words[3] = {installer.moonwater, (string_address) "wifi", null};

        if (installer.child <= 0)
                installer_start(words, 2, INSTALLER_JOB_WIFI, null);
}

static fn installer_spawn_or_note(string_address address_to words, positive count)
{
        if (!ui_start_apart(words, count))
                string_copy_bounded((string_address)installer.big->note, (string_address)WORDS(settings_cannot_run),
                                    sizeof(installer.big->note));
}

/* ---- step model ---- */

static b32 installer_can_next(void)
{
        return installer.step == INSTALLER_STEP_DISK ? (installer.chosen >= 0 && installer.chosen < installer.big->drive_count)
                                                     : installer.step < INSTALLER_STEP_REVIEW;
}

static const struct installer_drive address_to installer_drive_now(void)
{
        return installer.chosen >= 0 && installer.chosen < installer.big->drive_count ? &installer.big->drives[installer.chosen] : null;
}

static fn installer_choose_drive(int at)
{
        if (at < 0 || at >= installer.big->drive_count)
                return;
        installer.chosen = at;
        string_copy_bounded((string_address)installer.choice.drive, (string_address)installer.big->drives[at].name, sizeof(installer.choice.drive));
        installer.choice.bytes = installer.big->drives[at].bytes;
        installer.choice.mode = installer.big->drives[at].moonwater ? INSTALLER_UPDATE : INSTALLER_OVERWRITE;
        installer.dirty = true;
}

static b32 installer_step_done(int k)
{
        int current = installer.step < INSTALLER_STEPS ? installer.step : INSTALLER_STEPS - 1;

        if (installer.step == INSTALLER_STEP_DONE)
                return true;
        if (k == INSTALLER_STEP_NETWORK && k != current)
                return installer.choice.online || (k < current && installer.network_seen);
        return k < current;
}

static b32 installer_step_reachable(int k)
{
        if (installer.step > INSTALLER_STEP_REVIEW || k == installer.step || k < 0 || k >= INSTALLER_STEPS)
                return false;
        return k < installer.step || (k == INSTALLER_STEP_NETWORK && installer.choice.online && installer.step > INSTALLER_STEP_KEYBOARD);
}

static fn installer_go(int step)
{
        struct ui_list address_to l = &installer.big->list;

        if (step < INSTALLER_STEP_LANGUAGE || step > INSTALLER_STEP_DONE)
                return;
        installer.step = step;
        installer.big->note[0] = 0, installer.big->net_chosen = -1;
        if (step != INSTALLER_STEP_NETWORK)
                ui_field_close(&installer.big->pass);
        l->hover = l->down = -1, l->scroll.scroll = 0, l->focus = 0;
        if (step == INSTALLER_STEP_LANGUAGE)
                for (positive k = 0; k < INSTALLER_LANGUAGES; k++)
                        if (string_equals((string_address)language_names[k].code, (string_address)installer.choice.language))
                                l->focus = (int)k;
        if (step == INSTALLER_STEP_KEYBOARD)
                for (positive k = 0; k < INSTALLER_LAYOUTS; k++)
                        if (string_equals((string_address)installer_layouts[k].code, (string_address)installer.choice.keyboard))
                                l->focus = (int)k;
        if (step == INSTALLER_STEP_DISK)
        {
                l->focus = installer.chosen;
                if (!installer.listed)
                        installer_list();
        }
        if (step == INSTALLER_STEP_NETWORK)
                installer.next_look = installer.next_wifi = 0, installer.network_seen = true;
        installer.dirty = true;
}

static fn installer_pick_code(int at, char address_to choice, positive room, b32 address_to changed, const char address_to verb, const char address_to code)
{
        if (!string_equals((string_address)choice, (string_address)code))
        {
                string_address words[4] = {installer.moonwater, (string_address)verb, (string_address)choice, null};

                string_copy_bounded((string_address)choice, (string_address)code, room);
                *changed = true;
                installer_spawn_or_note(words, 3);
        }
        installer.big->list.focus = at, installer.dirty = true;
}

static fn installer_pick_language(int at)
{
        if (at >= 0 && at < (int)INSTALLER_LANGUAGES)
                installer_pick_code(at, installer.choice.language, sizeof(installer.choice.language), &installer.choice.language_changed, "language",
                                    language_names[at].code);
}

static fn installer_pick_layout(int at)
{
        if (at >= 0 && at < (int)INSTALLER_LAYOUTS)
                installer_pick_code(at, installer.choice.keyboard, sizeof(installer.choice.keyboard), &installer.choice.keyboard_changed, "keyboard",
                                    installer_layouts[at].code);
}

static fn installer_pick_network(int at)
{
        struct installer_big address_to big = installer.big;
        const struct installer_network address_to n;

        if (at < 0 || at >= big->net_count)
                return;
        n = &big->nets[at];
        big->list.focus = at, installer.dirty = true;
        if (n->joined)
                return;
        if (!n->open && !n->saved)
        {
                big->net_chosen = at;
                ui_field_open(&big->pass, &installer.own, "");
                big->pass.most = 63, big->pass.masked = true;
                ui_field_focus(&big->pass, true);
                return;
        }
        {
                string_address words[5] = {installer.moonwater, (string_address) "wifi", (string_address) "add", (string_address)n->name, null};
                installer_start(words, 4, INSTALLER_JOB_JOIN, null);
        }
}

static fn installer_join(void)
{
        struct installer_big address_to big = installer.big;
        char secret[96];
        positive length;

        if (big->net_chosen < 0 || big->net_chosen >= big->net_count || !big->pass.doc.selections)
                return;
        length = doc_read(&big->pass.doc, 0, (p8 address_to)secret, sizeof(secret) - 1);
        secret[length] = 0;
        {
                string_address words[6] = {installer.moonwater, (string_address) "wifi", (string_address) "add",
                                           (string_address)big->nets[big->net_chosen].name, (string_address) "-", null};
                if (length && installer_start(words, 5, INSTALLER_JOB_JOIN, secret))
                        big->net_chosen = -1, ui_field_close(&big->pass);
        }
        memory_fill(secret, 0, sizeof(secret));
        __asm__ __volatile__("" : : "r"(secret) : "memory");
}

static fn installer_toggle(unsigned int bit)
{
        if (installer.choice.online)
                installer.choice.profiles ^= bit, installer.dirty = true;
}

// Refuses virtual-device input; builds and starts the install plan (overwrite/update always runs setup).
static b32 installer_install(void)
{
        if (!installer.hand)
        {
                string_copy_bounded((string_address)installer.big->note, (string_address)WORDS(installer_virtual), sizeof(installer.big->note));
                installer.dirty = true;
                return false;
        }
        installer.commands = installer_plan(&installer.choice, installer.x86, installer.big->plan, sizeof(installer.big->plan));
        installer.at = 0, installer.failed = false, installer.big->last[0] = 0;
        if (!installer.commands || installer.child > 0)
                return false;
        installer.step = INSTALLER_STEP_RUN;
        if (!installer_run_at(0))
                installer.failed = true;
        installer.dirty = true;
        return true;
}

/* ---- view and layout ---- */

static fn installer_describe_code(struct ui_row address_to row, const char address_to name, const char address_to code, const char address_to chosen)
{
        row->label = name, row->value = code;
        row->mark = string_equals((string_address)code, (string_address)chosen), row->dim = !row->mark;
}

static fn installer_describe(int id, struct ui_row address_to row)
{
        struct installer_big address_to big = installer.big;

        if (installer.step == INSTALLER_STEP_LANGUAGE && id < (int)INSTALLER_LANGUAGES)
                installer_describe_code(row, language_names[id].name, language_names[id].code, installer.choice.language);
        else if (installer.step == INSTALLER_STEP_KEYBOARD && id < (int)INSTALLER_LAYOUTS)
                installer_describe_code(row, installer_layouts[id].name, installer_layouts[id].code, installer.choice.keyboard);
        else if (installer.step == INSTALLER_STEP_NETWORK && id < big->net_count)
        {
                const struct installer_network address_to n = &big->nets[id];

                row->label = n->name, row->bars = n->bars ? n->bars : 1, row->mark = n->joined;
                row->note = n->joined ? WORDS(installer_wifi_joined) : n->saved ? WORDS(installer_wifi_saved) : null;
        }
        else if (installer.step == INSTALLER_STEP_DISK && id < big->drive_count)
        {
                static char sizes[INSTALLER_DRIVES][24];
                const struct installer_drive address_to d = &big->drives[id];

                ui_size_decimal(d->bytes, sizes[id], sizeof(sizes[id]));
                row->label = d->model[0] ? d->model : d->name;
                row->note = d->moonwater ? WORDS(installer_disk_holds) : d->removable ? WORDS(installer_disk_removable) : null;
                row->value = sizes[id], row->about = d->name, row->mark = id == installer.chosen;
        }
}

static fn installer_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        installer.dirty = true;
}

static b32 installer_act(int which);

static b32 installer_card_shown(int k)
{
        return k != 1 || installer.x86;
}

static int installer_mode_at(int item)
{
        return item == 1 ? INSTALLER_UPDATE : item == 2 ? INSTALLER_USE : INSTALLER_OVERWRITE;
}

static fn installer_ctrl(struct ui_control address_to c, int kind, int variant)
{
        if (c->kind != kind)
                c->kind = kind, c->variant = variant, ui_control_start(c);
}

static fn installer_callout_emit(const char address_to label, const char address_to text, int role, int h)
{
        struct ui_control address_to c = &installer.big->callout;

        installer_ctrl(c, UI_CALLOUT, 0);
        c->label = label, c->role = role;
        if (text)
                string_copy_bounded((string_address)c->text, (string_address)text, sizeof(c->text));
        else
                c->text[0] = 0;
        ui_control(c, .h = h);
}

static fn installer_layout(void)
{
        static const struct
        {
                unsigned int head, about;
        } meta[7] = {
            {LANGUAGE_installer_language_head, LANGUAGE_installer_language_about},
            {LANGUAGE_installer_keyboard_head, LANGUAGE_installer_keyboard_about},
            {LANGUAGE_installer_network_head, LANGUAGE_installer_network_about},
            {LANGUAGE_installer_disk_head, LANGUAGE_installer_disk_about},
            {LANGUAGE_installer_apps_head, LANGUAGE_installer_apps_about},
            {LANGUAGE_installer_review_head, 0},
            {LANGUAGE_installer_running, 0},
        };
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        b32 run = installer.step >= INSTALLER_STEP_RUN, use = installer.choice.mode == INSTALLER_USE;
        b32 rows = installer.step <= INSTALLER_STEP_DISK && (installer.step != INSTALLER_STEP_NETWORK || big->wifi_on);
        const char address_to head, address_to about;

        if (!l->describe)
                l->describe = installer_describe, l->damaged = installer_damaged, l->hover = l->down = -1;
        int count = installer.step == INSTALLER_STEP_LANGUAGE ? (int)INSTALLER_LANGUAGES
                    : installer.step == INSTALLER_STEP_KEYBOARD ? (int)INSTALLER_LAYOUTS
                    : installer.step == INSTALLER_STEP_NETWORK  ? (big->wifi_on ? big->net_count : 0)
                    : installer.step == INSTALLER_STEP_DISK     ? big->drive_count
                                                                : 0;
        ui_list_begin(l);
        for (int k = 0; k < count; k++)
                if (!ui_list_add(l, k))
                {
                        ui_list_begin(l);
                        break;
                }
        ui_list_end(l);
        l->ids = l->count > 0 ? l->count : 1;
        l->empty = installer.step == INSTALLER_STEP_DISK      ? (installer.listed ? WORDS(installer_disk_none) : WORDS(installer_disk_looking))
                   : installer.step == INSTALLER_STEP_NETWORK ? (big->wifi_read ? WORDS(installer_wifi_none) : WORDS(installer_wifi_looking))
                                                              : null;
        if (installer.step < 7)
        {
                head = language_text(meta[installer.step].head);
                about = installer.step == INSTALLER_STEP_APPS && !installer.choice.online ? WORDS(installer_apps_offline)
                        : meta[installer.step].about                                      ? language_text(meta[installer.step].about)
                                                                                          : null;
        }
        else
                head = use ? WORDS(installer_use_done_head) : WORDS(installer_done_head),
                about = use ? WORDS(installer_use_done_about) : WORDS(installer_done_about);

        ui_view_begin(&big->view, ui, installer.width, -installer.height);
        ui_side_frame()
        {
                ui_side_pane(&big->side_box)
                        ui_host(&big->stepper, .grow = 1);
                ui_side_body(.class = "inset")
                {
                        ui_host(null, .h = UI_PAD);
                        ui_row(.class = "middle", .grow = 1)
                                ui_col(.grow = 1, .max_w = INSTALLER_COLUMN, .class = "gap3")
                                {
                                        ui_text(head, .class = "title", .size = 24);
                                        if (about)
                                                ui_text(about, .class = "quiet");
                                        if (installer.step == INSTALLER_STEP_NETWORK)
                                        {
                                                installer_callout_emit(installer.choice.online ? WORDS(installer_network_online) : WORDS(installer_network_offline),
                                                                       null, installer.choice.online ? UI_SUCCESS : UI_INFO, 52);
                                                if (big->wifi_read && !big->wifi_on)
                                                {
                                                        ui_text(WORDS(installer_wifi_off), .class = "quiet");
                                                        ui_row(.class = "gap3")
                                                                ui_button(WORDS(installer_wifi_on), .class = "action.outlined", .on = installer_act, .arg = 3);
                                                }
                                        }
                                        if (rows)
                                                ui_rows(l, .class = "list", .grow = 1);
                                        if (installer.step == INSTALLER_STEP_NETWORK && big->net_chosen >= 0)
                                        {
                                                language_format(big->line[2], sizeof(big->line[2]), WORDS(installer_wifi_password), big->nets[big->net_chosen].name);
                                                ui_row(.class = "gap3.level")
                                                {
                                                        ui_view_field(&big->pass_c, &big->pass, big->line[2], 48, true, &big->pass_box);
                                                        ui_button(WORDS(installer_wifi_join), .class = "action.primary", .on = installer_act, .arg = 6);
                                                }
                                        }
                                        if (installer.step == INSTALLER_STEP_KEYBOARD)
                                        {
                                                if (!big->field.doc.selections)
                                                {
                                                        ui_field_open(&big->field, &installer.own, "");
                                                        big->field.most = 63, big->field.keeps_vertical = true;
                                                        ui_field_focus(&big->field, false);
                                                }
                                                ui_view_field(&big->try_it, &big->field, WORDS(installer_keyboard_try), 48, false, &big->field_box);
                                        }
                                        if (installer.step == INSTALLER_STEP_DISK)
                                        {
                                                const struct installer_drive address_to d = installer_drive_now();
                                                struct ui_control address_to c = &big->modes;

                                                installer_ctrl(c, UI_TABS, UI_TABS_SEGMENTED);
                                                c->items[0] = WORDS(installer_mode_overwrite), c->items[1] = WORDS(installer_mode_update), c->items[2] = WORDS(installer_mode_use);
                                                c->count = d && d->moonwater ? 3 : 1;
                                                c->at = c->count == 3 ? (installer.choice.mode == INSTALLER_UPDATE ? 1 : use ? 2 : 0) : 0;
                                                c->disabled = !d;
                                                ui_control(c, .class = "tabs", .w = 420, .on = installer_act, .arg = 4);
                                                ui_text(c->at == 1 ? WORDS(installer_mode_update_about) : c->at == 2 ? WORDS(installer_mode_use_about) : WORDS(installer_mode_overwrite_about),
                                                        .class = "quiet");
                                        }
                                        if (installer.step == INSTALLER_STEP_APPS)
                                                for (int k = 0; k < 3; k++)
                                                        if (installer_card_shown(k))
                                                                ui_view_card_check(&big->card_box[k], &big->icon_box[k], &big->tick_box[k],
                                                                                   language_text(installer_apps[k].title),
                                                                                   installer.choice.online ? language_text(installer_apps[k].about) : WORDS(installer_apps_offline),
                                                                                   INSTALLER_CARD_H);
                                        if (installer.step == INSTALLER_STEP_REVIEW)
                                        {
                                                const struct installer_drive address_to d = installer_drive_now();
                                                char size[24];

                                                if (d)
                                                        ui_size_decimal(d->bytes, size, sizeof(size)),
                                                        language_format(big->line[0], sizeof(big->line[0]), WORDS(installer_review_drive),
                                                                        d->model[0] ? d->model : d->name, size, d->name);
                                                installer_callout_emit(d ? big->line[0] : "",
                                                                       installer.choice.mode == INSTALLER_UPDATE ? WORDS(installer_review_update)
                                                                       : use                                    ? WORDS(installer_review_use)
                                                                                                                : WORDS(installer_review_overwrite),
                                                                       installer.choice.mode == INSTALLER_OVERWRITE ? UI_WARNING : UI_INFO, 76);
                                                language_format(big->line[1], sizeof(big->line[1]), WORDS(installer_review_settings),
                                                                language_name_of(installer.choice.language), installer.choice.keyboard);
                                                ui_text(big->line[1], .class = "quiet");
                                        }
                                        if (run)
                                        {
                                                installer_ctrl(&big->meter, UI_PROGRESS, 0);
                                                big->meter.value = installer.step == INSTALLER_STEP_DONE ? UI_ONE
                                                                   : installer.at == 0 || !installer.commands ? -1
                                                                                                              : UI_ONE / installer.commands * installer.at;
                                                if (installer.step == INSTALLER_STEP_RUN)
                                                {
                                                        language_format(big->line[0], sizeof(big->line[0]), WORDS(installer_running_step), installer.at + 1, installer.commands);
                                                        ui_text(big->line[0], .class = "quiet");
                                                        ui_control(&big->meter, .class = "meter");
                                                        if (installer.failed)
                                                                language_format(big->line[1], sizeof(big->line[1]), WORDS(installer_failed), big->last),
                                                                ui_text(big->line[1], .class = "warning");
                                                        else
                                                                ui_text(big->last, .class = "quiet");
                                                }
                                        }
                                        if (big->note[0])
                                                ui_text(big->note, .class = "warning");
                                        if (!rows)
                                                ui_col(.grow = 1);
                                        ui_row(.class = "foot")
                                        {
                                                if (installer.step > INSTALLER_STEP_LANGUAGE && (installer.step <= INSTALLER_STEP_REVIEW || installer.failed))
                                                        ui_button(WORDS(installer_back), .class = "action.ghost", .on = installer_act, .arg = 0);
                                                if (installer.step < INSTALLER_STEP_REVIEW)
                                                        ui_button(installer.step == INSTALLER_STEP_NETWORK && !installer.choice.online ? WORDS(installer_skip) : WORDS(installer_next),
                                                                  .class = "action.primary", .on = installer_act, .arg = 1);
                                                else if (installer.step == INSTALLER_STEP_REVIEW)
                                                {
                                                        language_format(big->says, sizeof(big->says), WORDS(installer_erase_again), installer.choice.drive);
                                                        ui_button(use ? WORDS(installer_use) : WORDS(installer_install), .class = "action.primary", .on = installer_act, .arg = 2,
                                                                  .says = installer.choice.mode == INSTALLER_OVERWRITE ? big->says : null);
                                                }
                                                else if (installer.step == INSTALLER_STEP_DONE && !use)
                                                        ui_button(WORDS(installer_reboot), .class = "action.primary", .on = installer_act, .arg = 5);
                                        }
                                }
                }
        }
        ui_view_end(&big->view);
}

static b32 installer_act(int which)
{
        switch (which)
        {
        case 0:
                if (installer.step == INSTALLER_STEP_RUN)
                        installer.step = INSTALLER_STEP_REVIEW, installer.failed = false;
                else
                        installer_go(installer.step - 1);
                break;
        case 1:
                if (installer_can_next())
                {
                        int next = installer.step + 1;
                        installer_go(next == INSTALLER_STEP_NETWORK && installer.choice.online ? INSTALLER_STEP_DISK : next);
                }
                break;
        case 2:
                installer_install();
                break;
        case 3:
                installer_start((string_address[]){installer.moonwater, (string_address) "wifi", (string_address) "on", null}, 3, INSTALLER_JOB_JOIN, null);
                break;
        case 4:
                installer.choice.mode = installer_mode_at(installer.big->modes.at);
                break;
        case 5:
                installer_spawn_or_note((string_address[]){(string_address) "/shell", (string_address) "-c", (string_address) "reboot", null}, 3);
                break;
        case 6:
                installer_join();
                break;
        default:
                if (which >= 10 && which < 13)
                        installer_toggle(installer_apps[which - 10].bit);
        }
        installer.dirty = true;
        return true;
}

/* ---- custom painting: stepper, cards, field wells ---- */

static struct ui_box installer_box(struct ui_rect r, int radius)
{
        return (struct ui_box){r.x * 256, r.y * 256, (r.x + r.w) * 256, (r.y + r.h) * 256, radius * 256, radius * 256};
}

static fn installer_checkmark(struct ui address_to ui, int x, int y, int size, b64 share, unsigned int argb)
{
        b32 ax = x * 256 + size * 256 * 27 / 100, ay = y * 256 + size * 256 * 52 / 100;
        b32 bx = x * 256 + size * 256 * 43 / 100, by = y * 256 + size * 256 * 68 / 100;
        b32 cx = x * 256 + size * 256 * 75 / 100, cy = y * 256 + size * 256 * 34 / 100;
        b32 thick = ui_q8(ui, 2);
        b64 first = share * 2 > UI_ONE ? UI_ONE : share * 2, second = share * 2 > UI_ONE ? share * 2 - UI_ONE : 0;

        if (first <= 0)
                return;
        ui_chevron_stroke(ui, ax, ay, ax + (b32)ui_qmul(first, bx - ax), ay + (b32)ui_qmul(first, by - ay), thick, argb);
        if (second > 0)
                ui_chevron_stroke(ui, bx, by, bx + (b32)ui_qmul(second, cx - bx), by + (b32)ui_qmul(second, cy - by), thick, argb);
}

static fn installer_stepper(struct ui address_to ui)
{
        static const unsigned int steps[INSTALLER_STEPS] = {
            LANGUAGE_installer_step_language, LANGUAGE_installer_step_keyboard, LANGUAGE_installer_step_network,
            LANGUAGE_installer_step_disk,     LANGUAGE_installer_step_apps,     LANGUAGE_installer_step_install,
        };
        struct ui_rect r = installer.big->stepper;
        int current = installer.step == INSTALLER_STEP_DONE ? INSTALLER_STEPS : installer.step < INSTALLER_STEPS ? installer.step : INSTALLER_STEPS - 1;
        int d = ui_dev(ui, 26), row = ui_dev(ui, INSTALLER_STEP_ROW), pad = ui_dev(ui, UI_INSET);
        unsigned int accent = ui_role(UI_PRIMARY), quiet, strong, line;

        ui_side_inks(ui, true);
        quiet = ui_text_colour(ui, UI_ON_SURFACE_VARIANT), strong = ui_text_colour(ui, UI_ON_SURFACE), line = ui_well(ui, UI_OUTLINE_VARIANT, 40);
        for (int k = 0; k < INSTALLER_STEPS; k++)
        {
                int top = r.y + k * row, cx = ui_mirror(r.x + pad, d, r.x, r.w) + d / 2, cy = top + row / 2;
                struct ui_box circle = {(cx - d / 2) * 256, (cy - d / 2) * 256, (cx - d / 2 + d) * 256, (cy - d / 2 + d) * 256, d * 128, d * 128};
                struct ui_box band = installer_box((struct ui_rect){r.x, top, r.w, row}, 0);
                const char address_to name = language_text(steps[k]);
                int width = ui_kit_measure(ui, (string_address)name, UI_BODY_MEDIUM);
                char number[2] = {(char)('1' + k), 0};
                b32 done = installer_step_done(k);

                if (top >= r.y + r.h)
                        break;
                if (k == current)
                        ui_kit_fill(ui, &band, ui_kit_lit(ui), null);
                else if (k == installer.big->step_hover && installer_step_reachable(k))
                        ui_kit_fill(ui, &band, 0x14000000u | (strong & 0xffffffu), null);
                if (k + 1 < INSTALLER_STEPS)
                        ui_kit_fill(ui, &(struct ui_box){cx * 256 - 128, (cy + d / 2 + ui_dev(ui, 3)) * 256, cx * 256 + 128 + (ui_dev(ui, 1) - 1) * 256,
                                                         (cy + row - d / 2 - ui_dev(ui, 3)) * 256, 0, 0},
                                    done ? accent : line, null);
                if (done && k != current)
                        ui_kit_fill(ui, &circle, accent, null), installer_checkmark(ui, cx - d / 2, cy - d / 2, d, UI_ONE, ui_role(UI_ON_PRIMARY));
                else
                {
                        int nw = ui_kit_measure(ui, (string_address)number, UI_LABEL_LARGE);

                        ui_kit_ring(ui, &circle, ui_q8(ui, 2), k == current ? accent : line);
                        ui_kit_text_clip(ui, cx - nw / 2, ui_kit_baseline(ui, cy - ui_dev(ui, 10), UI_LABEL_LARGE), (string_address)number,
                                         UI_LABEL_LARGE, k == current ? accent : quiet, 0, ui->width);
                }
                ui_kit_text_clip(ui, ui_mirror(r.x + pad + d + ui_dev(ui, 12), width, r.x, r.w),
                                 ui_kit_baseline(ui, top + (row - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM), (string_address)name, UI_BODY_MEDIUM,
                                 k == current || done ? strong : quiet, 0, ui->width);
        }
        ui_side_inks(ui, false);
}

static b32 installer_steps_event(const struct window_event address_to event)
{
        struct installer_big address_to big = installer.big;
        struct ui_rect r = big->stepper;
        int row = ui_dev(&installer.ui, INSTALLER_STEP_ROW), x = ui_kit.pointer_x, y = ui_kit.pointer_y, k = -1;

        if (row > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && (y - r.y) / row < INSTALLER_STEPS)
                k = (y - r.y) / row;
        if (event->type == WINDOW_EVENT_MOTION)
        {
                if (k != big->step_hover)
                        big->step_hover = k, installer.dirty = true;
                if (installer_step_reachable(k))
                        ui_kit_shape = WINDOW_POINTER_HAND;
                return false;
        }
        if (event->type != WINDOW_EVENT_BUTTON || k < 0)
                return false;
        if ((event->flags & WINDOW_KEY_DOWN) && installer_step_reachable(k))
        {
                if (installer.step == INSTALLER_STEP_REVIEW && k < INSTALLER_STEP_REVIEW)
                        installer.failed = false;
                installer_go(k);
        }
        return true;
}

static fn installer_well(struct ui address_to ui, struct ui_rect r, b32 focused)
{
        struct ui_box box = installer_box(r, ui_dev(ui, 10));

        if (r.w <= 0)
                return;
        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_LOWEST), null);
        ui_kit_ring(ui, &box, ui_q8(ui, focused ? 2 : 1), focused ? ui_role(UI_PRIMARY) : ui_role(UI_OUTLINE_VARIANT));
}

static fn installer_cards_paint(struct ui address_to ui, b32 over)
{
        struct installer_big address_to big = installer.big;

        for (int k = 0; k < 3; k++)
        {
                struct ui_box box = installer_box(big->card_box[k], ui_dev(ui, 18));

                if (!installer_card_shown(k) || big->card_box[k].w <= 0)
                        continue;
                if (!over)
                {
                        b32 on = (installer.choice.profiles & installer_apps[k].bit) && installer.choice.online;
                        unsigned int layer;

                        ui_kit_number_to(&big->card_hover[k], big->hover_card == k && installer.choice.online ? UI_ONE : 0, 200, &ui_ease);
                        ui_kit_number_to(&big->card_press[k], big->press_card == k && installer.choice.online ? UI_ONE : 0, 150, &ui_ease);
                        ui_kit_number_to(&big->card_on[k], on ? UI_ONE : 0, 220, &ui_ease);
                        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_LOWEST), null);
                        layer = (unsigned int)((ui_qmul(ui_kit_tween(&big->card_hover[k]), UI_Q2(6)) +
                                                ui_qmul(ui_kit_tween(&big->card_press[k]), UI_Q2(4))) * 255 / UI_ONE);
                        if (layer)
                                ui_kit_fill(ui, &box, (ui_role(UI_ON_SURFACE) & 0xffffffu) | layer << 24, null);
                }
                else
                {
                        struct ui_rect t = big->tick_box[k], i = big->icon_box[k];
                        struct ui_box tick = installer_box(t, ui_dev(ui, 7)), tile = installer_box(i, ui_dev(ui, 12));
                        b64 share = ui_kit_tween(&big->card_on[k]);
                        unsigned int accent = ui_role(UI_PRIMARY), alpha = (unsigned int)(share * 255 / UI_ONE);

                        ui_kit_fill(ui, &tile, ui_role(UI_PRIMARY_CONTAINER), null);
                        ui_icon(ui, installer_apps[k].icon, i.x + i.w / 6, i.y + i.h / 6, i.w * 2 / 3, 0, ui_role(UI_ON_PRIMARY_CONTAINER));
                        ui_kit_ring(ui, &tick, ui_q8(ui, 2), share >= UI_ONE ? accent : ui_role(UI_OUTLINE));
                        if (alpha)
                                ui_kit_fill(ui, &tick, (accent & 0xffffffu) | alpha << 24, null),
                                installer_checkmark(ui, t.x, t.y, t.w, share, ui_role(UI_ON_PRIMARY));
                        ui_kit_ring(ui, &box, ui_q8(ui, alpha ? 2 : 1), alpha ? ((accent & 0xffffffu) | alpha << 24) : ui_role(UI_OUTLINE_VARIANT));
                        if (k == big->focus_card && installer.focused && !big->view.group.keys)
                        {
                                b32 o = ui_q8(ui, 3);
                                ui_kit_ring(ui, &(struct ui_box){box.x0 - o, box.y0 - o, box.x1 + o, box.y1 + o, box.rx + o, box.ry + o},
                                            ui_q8(ui, 2), ui_role(UI_ON_SURFACE));
                        }
                        if (!installer.choice.online)
                                ui_kit_fill(ui, &box, (ui_role(UI_SURFACE) & 0xffffffu) | 0x8c000000u, null);
                }
        }
}

static struct ui_text address_to installer_active_field(void)
{
        if (installer.step == INSTALLER_STEP_KEYBOARD)
                return &installer.big->field;
        if (installer.step == INSTALLER_STEP_NETWORK && installer.big->net_chosen >= 0)
                return &installer.big->pass;
        return null;
}

static fn installer_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        struct ui_text address_to field;

        ui_begin(ui, pixels, installer.own.table ? installer.own.table->stride : (unsigned long)installer.width, installer.width, installer.height);
        memory_zero(big->card_box, sizeof(big->card_box));
        big->field_box.w = big->pass_box.w = 0;
        installer_layout();
        ui_side_place(ui, big->side_box.x, big->side_box.w);
        l->stripe = 2, l->lit = l->focus >= 0;
        l->keys = installer.focused && !big->view.group.keys && !big->field.focused && !big->pass.focused;
        if (installer.step == INSTALLER_STEP_APPS)
                installer_cards_paint(ui, false);
        if ((field = installer_active_field()))
                installer_well(ui, field == &big->field ? big->field_box : big->pass_box, field->focused);
        ui_view_draw(ui, &big->view, installer.focused);
        installer_stepper(ui);
        if (installer.step == INSTALLER_STEP_APPS)
                installer_cards_paint(ui, true);
        if (field)
                ui_text_overlay(ui, field);
}

/* ---- events and timers ---- */

static fn installer_pick(int row)
{
        if (installer.step == INSTALLER_STEP_LANGUAGE)
                installer_pick_language(row);
        else if (installer.step == INSTALLER_STEP_KEYBOARD)
                installer_pick_layout(row);
        else if (installer.step == INSTALLER_STEP_NETWORK)
                installer_pick_network(row);
        else if (installer.step == INSTALLER_STEP_DISK)
                installer_choose_drive(row);
}

static int installer_card_at(int x, int y)
{
        for (int k = 0; k < 3; k++)
        {
                struct ui_rect r = installer.big->card_box[k];

                if (installer_card_shown(k) && r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
                        return k;
        }
        return -1;
}

static b32 installer_cards_event(const struct window_event address_to event)
{
        struct installer_big address_to big = installer.big;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        int at;

        if (event->type == WINDOW_EVENT_MOTION)
        {
                at = installer_card_at(event->a, event->b);
                if (at != big->hover_card)
                        big->hover_card = at, installer.dirty = true;
                if (at >= 0)
                        ui_kit_shape = WINDOW_POINTER_HAND;
                return false;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_LEFT)
        {
                at = installer_card_at(ui_kit.pointer_x, ui_kit.pointer_y);
                if (down)
                {
                        big->press_card = at, installer.dirty = true;
                        if (at >= 0)
                                big->focus_card = at;
                        return at >= 0;
                }
                if (big->press_card >= 0 && at == big->press_card && !(event->flags & WINDOW_KEY_CANCEL))
                        installer_toggle(installer_apps[at].bit);
                at = big->press_card, big->press_card = -1, installer.dirty = true;
                return at >= 0;
        }
        if (event->type == WINDOW_EVENT_KEY && down && !big->view.group.keys)
        {
                int step = event->a == UI_KEY_DOWN ? 1 : event->a == UI_KEY_UP ? -1 : 0;

                if (step)
                {
                        at = big->focus_card < 0 ? 0 : big->focus_card;
                        do
                                at = (at + step + 3) % 3;
                        while (!installer_card_shown(at));
                        big->focus_card = at, installer.dirty = true;
                        return true;
                }
                if (event->a == 57 && big->focus_card >= 0)
                        return installer_toggle(installer_apps[big->focus_card].bit), true;
        }
        return false;
}

static b32 installer_event_take(const struct window_event address_to event)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        struct ui_text address_to field = installer_active_field();
        p32 asks;
        int row;

        installer_layout();
        if (field && field->doc.selections)
        {
                if (event->type == WINDOW_EVENT_BUTTON && down)
                        ui_field_focus(field, ui_text_inside(field, ui_kit.pointer_x, ui_kit.pointer_y));
                asks = event->type != WINDOW_EVENT_KEY || field->focused ? ui_field_event(ui, field, null, event, ui_kit.pointer_x, ui_kit.pointer_y) : 0;
                if (event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL))
                        asks = 0;
                if ((asks & UI_TEXT_SUBMIT) && field == &big->pass)
                        installer_join();
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_SUBMIT))
                        ui_field_focus(field, false);
                if (asks)
                        return installer.dirty = true;
        }
        if (event->type == WINDOW_EVENT_KEY && down && event->a == UI_KEY_ESCAPE && installer.step != INSTALLER_STEP_RUN)
                return false;
        if (installer.step == INSTALLER_STEP_APPS && installer_cards_event(event))
                return true;
        asks = ui_view_event(ui, &big->view, event);
        if (asks & UI_VIEW_QUIT)
                return false;
        installer.dirty |= (asks & UI_VIEW_DRAW) != 0;
        if (asks & UI_VIEW_TAKEN)
                return true;
        if ((event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON) && installer_steps_event(event))
                return true;
        if (!l->count)
                return true;
        if (event->type == WINDOW_EVENT_KEY && down)
        {
                b32 moved = false;

                if (event->a == UI_KEY_ENTER)
                        return installer_pick(l->focus), true;
                if (ui_list_key(ui, l, (int)event->a, &moved))
                {
                        if (moved && installer.step != INSTALLER_STEP_NETWORK)
                                installer_pick(l->focus);
                        installer.dirty = true;
                }
                return true;
        }
        if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
        {
                if ((row = ui_list_click(ui, l, event, ui_kit.pointer_x, ui_kit.pointer_y)) >= 0)
                        installer_pick(row);
                installer.dirty = true;
        }
        return true;
}

static b32 installer_hand_of(const struct window_event address_to event)
{
        return ui_event_hand(event);
}

static b32 installer_event(const struct window_event address_to event)
{
        b32 taken;

        installer.hand = installer_hand_of(event);
        taken = installer_event_take(event);
        installer.hand = false;
        return taken;
}

static long installer_tick(long timeout, long address_to other)
{
        b64 now = (b64)ui_kit.now;
        struct ui_text address_to field = installer_active_field();

        (void)other;
        if (field && field->focused)
        {
                b32 redraw = false;

                timeout = ui_text_timeout(&installer.ui, field, timeout, &redraw);
                installer.dirty |= redraw;
        }
        if (now >= installer.next_look)
        {
                char text[4096];
                bipolar got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/net/route", (p8 address_to)text, sizeof(text) - 1);
                b32 was = installer.choice.online;

                installer.choice.online = got > 0 && installer_route_default(text, (positive)got);
                installer.dirty |= was != installer.choice.online;
                installer.next_look = now + INSTALLER_LOOK_NS;
        }
        if (installer.step == INSTALLER_STEP_NETWORK && now >= installer.next_wifi && installer.child <= 0)
                installer_wifi(), installer.next_wifi = now + INSTALLER_WIFI_NS;
        timeout = ui_deadline_wait(timeout, now, installer.next_look);
        if (installer.step == INSTALLER_STEP_NETWORK)
                timeout = ui_deadline_wait(timeout, now, installer.next_wifi);
        return timeout == 0 ? 1 : timeout;
}

/* ---- startup ---- */

static fn installer_begin(void)
{
        char text[16];
        bipolar got;

        memory_zero(&installer.runner, sizeof(installer.runner));
        installer.runner.output = -1, installer.chosen = -1, installer.step = INSTALLER_STEP_LANGUAGE;
#ifdef X64
        installer.x86 = X64;
#else
        installer.x86 = 0;
#endif
        installer.big->hover_card = installer.big->press_card = installer.big->focus_card = -1;
        installer.big->net_chosen = -1, installer.big->step_hover = -1;
        string_copy_bounded((string_address)installer.choice.language, (string_address) "en", sizeof(installer.choice.language));
        string_copy_bounded((string_address)installer.choice.keyboard, (string_address) "us", sizeof(installer.choice.keyboard));
        if ((got = file_slurp_once_at(AT_FDCWD, (string_address)SETTING_LANGUAGE_PATH, (p8 address_to)text, sizeof(text) - 1)) >= 2)
                text[2] = 0, string_copy_bounded((string_address)installer.choice.language, (string_address)text, sizeof(installer.choice.language));
        if ((got = file_slurp_once_at(AT_FDCWD, (string_address) "/root/keyboard", (p8 address_to)text, sizeof(text) - 1)) >= 2)
        {
                positive length = 0;

                while (length < (positive)got && length < 7 && text[length] >= 'a' && text[length] <= 'z')
                        length++;
                text[length] = 0;
                if (length)
                        string_copy_bounded((string_address)installer.choice.keyboard, (string_address)text, sizeof(installer.choice.keyboard));
        }
        installer_go(INSTALLER_STEP_LANGUAGE);
}

static int installer_open(void)
{
        if (ui_lift_other(&installer.own, (string_address) "installer", false))
                return 0;
        if (!(installer.big = ui_app_map(&installer_app, sizeof(struct installer_big))))
                return 1;
        installer_app.view = &installer.big->view;
        installer_begin();
        installer_list();
        ui_app_place(&installer_app, ui_px(&installer.ui, INSTALLER_WIDTH), ui_px(&installer.ui, INSTALLER_HEIGHT));
        return -1;
}

static struct ui_app installer_app = {
    .own = &installer.own,
    .ui = &installer.ui,
    .missing = (string_address) "installer: no window: the compositor's desktop is not up\n",
    .title = (string_address) "Install Moonwater",
    .name = (string_address) "installer",
    .glass = 0,
    .side = true,
    .nofont = (string_address) "installer: no font to draw with (-f FONT.ttf), pid ",
    .dirty = &installer.dirty,
    .focused = &installer.focused,
    .ground = &installer.ground,
    .open = installer_open,
    .event = installer_event,
    .tick = installer_tick,
    .width = &installer.width,
    .height = &installer.height,
    .shown = &installer.shown,
    .draw = installer_draw,
    .job = &installer.runner,
};

static b32 installer_main()
{
        memory_zero(&installer, sizeof(installer));
        installer.moonwater = (string_address) "/moonwater", installer.output = -1;
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took)
        {
                string_address value;

                if ((took = ui_app_option(&installer_app, at)))
                        continue;
                took = 1;
                if ((value = ui_argument(at, (string_address) "-m")))
                        installer.moonwater = value, took = 2;
        }
        return ui_app_run(&installer_app);
}

#endif // MOONWATER_CANVAS_INSTALLER
