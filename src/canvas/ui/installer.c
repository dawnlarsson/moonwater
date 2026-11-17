/*
        The installer: the window a live boot opens on the desktop (the machine script's desktop_installer, on a verdict of live
        or ask; an installed boot's is disk, and starts none), which puts Moonwater on a drive in six steps, a trail of them
        along its top and Back and Next at its foot:

            Language   the system's language, of the seventeen: chosen, it is the live session's at once (moonwater language),
                       so this window and every other speaks it, and the install carries it
            Keyboard   the layout, with a field to try it in: chosen, it is the session's at once (moonwater keyboard)
            Network    whether the machine is online (a default route), and Settings opened for Wi-Fi; it can be skipped
            Disk       the drives `moonwater install list` names, less the one this booted from, and for the one chosen,
                       Overwrite (the drive erased and installed anew) or, where it holds Moonwater, Update (its system and boot
                       files replaced, its data and settings kept)
            Apps       Chrome, Steam (x86-64 alone) and SSH, a card each, run once the drive is written; offline, the cards
                       are there and cannot be chosen, and say why
            Install    what will be done, the drive named by its model, size and device; Install, and for an overwrite Install
                       pressed again, with a key or a press from a device of the machine's own: one from a virtual device
                       (WINDOW_EVENT_VIRTUAL: uinput, the on-screen keyboard, the Wayland shim) is refused, as a deliberate
                       Settings row refuses it

        Then the plan (installer_model.c) is run a command at a time, each one's last line said under the progress, and the
        window ends on Restart. A command that fails stops it there, with its words, and Back goes to the review.

            installer [-f FONT.ttf] [-b BOLD.ttf] [-m MOONWATER]

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_INSTALLER
#define MOONWATER_CANVAS_INSTALLER

#include "installer_model.c"

#define INSTALLER_WIDTH 760 // logical pixels
#define INSTALLER_HEIGHT 600
#define INSTALLER_LOOK_NS 1000000000ll // the network looked at this often
#define INSTALLER_SAID 8192            // what a command said, kept to its last line

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
#define INSTALLER_TRAIL 6 // the steps the trail names: the run and its end are the last one's

// The layouts the Keyboard row offers (registry.c's "keyboard" row), each by its own name, as the languages are.
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

struct installer_big
{
        struct ui_view view;
        struct ui_list list;                        // the step's choices: the languages, the layouts or the drives
        struct ui_trail trail;
        struct ui_rect trail_box;
        struct ui_text field;                       // the keyboard's try-it field
        struct ui_control try_it, modes, meter, callout, card[3], icon[3], box[3];
        struct installer_drive drives[INSTALLER_DRIVES];
        int drive_count;
        char said[INSTALLER_SAID];                  // what the command running said
        positive said_length;
        char last[160];                             // its last line
        char plan[INSTALLER_PLAN];
        char note[160];                             // what the window says of what was just refused
        char line[2][160];                          // the review's lines
        char says[96];                              // what Install's next press does
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
        b32 x86;
        b32 hand;          // the event being taken is a key or a press of a device of the machine's (no WINDOW_EVENT_VIRTUAL)
        b32 listed;        // the drives were listed
        int chosen;        // the drive chosen, by its place in drives, -1 none
        // The command running: its process and output, which of the plan it is and how many there are, and whether one failed.
        bipolar child;
        bipolar output;
        int at, commands;
        b32 failed, listing;
        b64 next_look;
} installer;

static struct ui_app installer_app;

/* ---- the commands ---- */

// A command started with its output to a pipe this end reads, its input nothing (so the verb's yes is its answer): false when it could not be.
static b32 installer_start(string_address address_to words, positive count)
{
        int ends[2] = {-1, -1};
        bipolar child;

        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) != 0)
                return false;
        system_call_3(syscall(fcntl), (positive)ends[0], 4, O_NONBLOCK);
        child = system_fork();
        if (child == 0)
        {
                bipolar quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ | O_CLOEXEC);

                if (quiet >= 0)
                        system_call_3(syscall(dup3), (positive)quiet, 0, 0);
                system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                system_call_3(syscall(dup3), (positive)ends[1], 2, 0);
                (void)shell_exec_file(words[0], words, count, window_asked_environment());
                system_call_1(syscall(exit), 127);
        }
        system_close(ends[1]);
        if (child < 0)
        {
                system_close(ends[0]);
                return false;
        }
        installer.child = child, installer.output = ends[0];
        installer.big->said_length = 0, installer.big->said[0] = 0;
        return true;
}

// A line of words, a blank apart, cut into words in place: how many, a null after them.
static positive installer_words(char address_to line, string_address address_to words, positive most)
{
        positive count = 0;

        while (*line && count + 1 < most)
        {
                while (*line == ' ')
                        *line++ = 0;
                if (!*line)
                        break;
                words[count++] = (string_address)line;
                while (*line && *line != ' ')
                        line++;
        }
        words[count] = null;
        return count;
}

// The plan's command at, started; false when there is none, or it could not be.
static b32 installer_run_at(int at)
{
        char line[256];
        string_address words[8];
        const char address_to c = installer.big->plan;
        positive length = 0;

        for (int k = 0; k < at && *c; c++)
                if (*c == '\n')
                        k++;
        while (c[length] && c[length] != '\n' && length + 1 < sizeof(line))
                line[length] = c[length], length++;
        line[length] = 0;
        if (!length)
                return false;
        // The plan names the moonwater command by its path in the image; one the window was given (-m) is that one.
        if (!memory_compare(line, "/moonwater ", 11) && installer.moonwater && !string_equals(installer.moonwater, (string_address) "/moonwater"))
        {
                positive count = installer_words(line, words, 8);

                words[0] = installer.moonwater;
                return installer_start(words, count);
        }
        return installer_start(words, installer_words(line, words, 8));
}

// What a command said kept, and its last line: the last whole line it wrote, else what it is writing.
static fn installer_last_line(void)
{
        struct installer_big address_to big = installer.big;
        positive stop = big->said_length, start;

        while (stop && (big->said[stop - 1] == '\n' || big->said[stop - 1] == ' '))
                stop--;
        start = stop;
        while (start && big->said[start - 1] != '\n')
                start--;
        // The label the verb says its lines with is no word of this window's.
        if (stop - start > 12 && !memory_compare(big->said + start, "[Moonwater] ", 12))
                start += 12;
        if (stop - start >= sizeof(big->last))
                start = stop - (sizeof(big->last) - 1);
        memory_copy(big->last, big->said + start, stop - start);
        big->last[stop - start] = 0;
        for (positive k = 0; big->last[k]; k++)
                if ((p8)big->last[k] < 0x20)
                        big->last[k] = ' ';
}

// The drives the listing said, read back, the boot drive and the others left out: the list's rows.
static fn installer_listed(void)
{
        struct installer_big address_to big = installer.big;
        int count = installer_parse(big->said, big->said_length, big->drives, INSTALLER_DRIVES);

        big->drive_count = installer_targets(big->drives, count);
        installer.listed = true;
        installer.chosen = big->drive_count == 1 ? 0 : -1;
}

// The command that ran has ended, its status said: the next of the plan, or the end of it.
static fn installer_ended(positive status)
{
        b32 ok = (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 0;

        installer_last_line();
        if (installer.listing)
        {
                installer.listing = false;
                installer_listed();
                return;
        }
        if (!ok)
        {
                installer.failed = true;
                return;
        }
        if (++installer.at < installer.commands && installer_run_at(installer.at))
                return;
        if (installer.at < installer.commands)
        {
                installer.failed = true;
                return;
        }
        installer.step = INSTALLER_STEP_DONE;
}

// The output read to where it stops, and the child reaped once it ends.
static fn installer_drain(void)
{
        struct installer_big address_to big = installer.big;
        char block[1024];
        bipolar got;
        positive status = 0;

        if (installer.output < 0)
                return;
        while ((got = system_read_once((b32)installer.output, block, sizeof(block))) > 0)
        {
                // Kept to the room there is: what does not fit pushes the oldest out.
                if (big->said_length + (positive)got >= sizeof(big->said))
                {
                        positive drop = big->said_length + (positive)got - sizeof(big->said) + 1;

                        drop = drop > big->said_length ? big->said_length : drop;
                        memory_copy(big->said, big->said + drop, big->said_length - drop);
                        big->said_length -= drop;
                }
                memory_copy(big->said + big->said_length, block, (positive)got < sizeof(big->said) ? (positive)got : sizeof(big->said) - 1);
                big->said_length += (positive)got < sizeof(big->said) ? (positive)got : sizeof(big->said) - 1;
                big->said[big->said_length] = 0;
        }
        installer_last_line();
        installer.dirty = true;
        if (got == -11) // EAGAIN: it writes on
                return;
        system_close(installer.output);
        installer.output = -1;
        system_wait4_retry(installer.child, address_of status, 0, null);
        installer.child = 0;
        installer_ended(status);
}

// The drives asked for again, of the verb: what the window lists is what it says.
static fn installer_list(void)
{
        string_address words[4] = {installer.moonwater, (string_address) "install", (string_address) "list", null};

        if (installer.output >= 0)
                return;
        installer.listed = false;
        installer.listing = installer_start(words, 3);
        if (!installer.listing)
                installer.listed = true, installer.big->drive_count = 0;
}

// A setting put in force in the live session at once, as the person chose it (moonwater language, moonwater keyboard).
static fn installer_apply(const char address_to verb, const char address_to value)
{
        string_address words[4] = {installer.moonwater, (string_address)verb, (string_address)value, null};

        (void)ui_start_apart(words, 3);
}

/* ---- the model of the steps ---- */

// Whether Next goes on from the step: a drive is chosen on Disk; every other step can be passed.
static b32 installer_can_next(void)
{
        if (installer.step == INSTALLER_STEP_DISK)
                return installer.chosen >= 0 && installer.chosen < installer.big->drive_count;
        return installer.step < INSTALLER_STEP_REVIEW;
}

// The choice of the drive chosen, its mode one it can have: Update only where it holds Moonwater.
static fn installer_choose_drive(int at)
{
        struct installer_drive address_to d;

        if (at < 0 || at >= installer.big->drive_count)
                return;
        d = &installer.big->drives[at];
        installer.chosen = at;
        string_copy_bounded((string_address)installer.choice.drive, (string_address)d->name, sizeof(installer.choice.drive));
        if (!d->moonwater)
                installer.choice.mode = INSTALLER_OVERWRITE;
        installer.dirty = true;
}

// A step gone to: its list's rows made, and what a step needs on arriving (the drives listed, the network looked at).
static fn installer_go(int step)
{
        struct ui_list address_to l = &installer.big->list;

        if (step < INSTALLER_STEP_LANGUAGE || step > INSTALLER_STEP_DONE)
                return;
        installer.step = step;
        installer.big->note[0] = 0;
        l->hover = l->down = -1;
        l->scroll.scroll = 0;
        l->focus = 0;
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
                installer.next_look = 0;
        installer.dirty = true;
}

static fn installer_pick_language(int at)
{
        if (at < 0 || at >= (int)INSTALLER_LANGUAGES)
                return;
        if (!string_equals((string_address)installer.choice.language, (string_address)language_names[at].code))
        {
                string_copy_bounded((string_address)installer.choice.language, (string_address)language_names[at].code, sizeof(installer.choice.language));
                installer.choice.language_changed = true;
                installer_apply("language", installer.choice.language);
        }
        installer.big->list.focus = at;
        installer.dirty = true;
}

static fn installer_pick_layout(int at)
{
        if (at < 0 || at >= (int)INSTALLER_LAYOUTS)
                return;
        if (!string_equals((string_address)installer.choice.keyboard, (string_address)installer_layouts[at].code))
        {
                string_copy_bounded((string_address)installer.choice.keyboard, (string_address)installer_layouts[at].code, sizeof(installer.choice.keyboard));
                installer.choice.keyboard_changed = true;
                installer_apply("keyboard", installer.choice.keyboard);
        }
        installer.big->list.focus = at;
        installer.dirty = true;
}

// A profile's card thrown: its bit, when the network is there to fetch it (offline, a card is no choice).
static fn installer_toggle(unsigned int bit)
{
        if (!installer.choice.online)
                return;
        installer.choice.profiles ^= bit;
        installer.dirty = true;
}

/*
        Install pressed (the review's own button, after the view's press again for an overwrite): refused for a press or a key from a
        virtual device, which says how it is done; else the plan made and its first command started.
*/
static b32 installer_install(void)
{
        if (!installer.hand)
        {
                string_copy_bounded((string_address)installer.big->note, (string_address)WORDS(installer_virtual), sizeof(installer.big->note));
                installer.dirty = true;
                return false;
        }
        installer.commands = installer_plan(&installer.choice, installer.x86, installer.big->plan, sizeof(installer.big->plan));
        installer.at = 0, installer.failed = false;
        installer.big->last[0] = 0;
        if (!installer.commands)
                return false;
        installer.step = INSTALLER_STEP_RUN;
        if (!installer_run_at(0))
                installer.failed = true;
        installer.dirty = true;
        return true;
}

/* ---- the view ---- */

static fn installer_describe(int id, struct ui_row address_to row)
{
        if (installer.step == INSTALLER_STEP_LANGUAGE && id < (int)INSTALLER_LANGUAGES)
        {
                row->label = language_names[id].name;
                row->mark = string_equals((string_address)language_names[id].code, (string_address)installer.choice.language);
                row->value = language_names[id].code;
                row->dim = !row->mark;
        }
        else if (installer.step == INSTALLER_STEP_KEYBOARD && id < (int)INSTALLER_LAYOUTS)
        {
                row->label = installer_layouts[id].name;
                row->mark = string_equals((string_address)installer_layouts[id].code, (string_address)installer.choice.keyboard);
                row->value = installer_layouts[id].code;
                row->dim = !row->mark;
        }
        else if (installer.step == INSTALLER_STEP_DISK && id < installer.big->drive_count)
        {
                static char sizes[INSTALLER_DRIVES][24];
                const struct installer_drive address_to d = &installer.big->drives[id];

                installer_size(d->bytes, sizes[id], sizeof(sizes[id]));
                row->label = d->model[0] ? d->model : d->name;
                row->note = d->moonwater ? WORDS(installer_disk_holds) : d->removable ? WORDS(installer_disk_removable) : null;
                row->value = sizes[id];
                row->about = d->name;
                row->mark = id == installer.chosen;
        }
}

static fn installer_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        installer.dirty = true;
}

static b32 installer_act(int which);

// A profile's card: its icon, its title and its line, and its box at the end; off and with the note where the machine is offline.
static fn installer_card(int k, const char address_to icon, const char address_to title, const char address_to about, unsigned int bit)
{
        struct installer_big address_to big = installer.big;
        struct ui_control address_to card = &big->card[k], address_to mark = &big->icon[k], address_to box = &big->box[k];

        if (!card->kind)
                card->kind = UI_CARD, ui_control_start(card);
        if (!mark->kind)
                mark->kind = UI_AVATAR, mark->variant = 1, ui_control_start(mark);
        if (!box->kind)
                box->kind = UI_CHECKBOX, ui_control_start(box);
        mark->label = icon;
        box->label = "";
        box->on = (installer.choice.profiles & bit) != 0 && installer.choice.online;
        box->disabled = card->disabled = !installer.choice.online;
        ui_row(.control = card, .class = "panel level gap3")
        {
                ui_control(mark, .w = 40, .h = 40);
                ui_col(.grow = 1, .class = "gap2")
                {
                        ui_text(title, .class = "title");
                        ui_text(about, .class = "quiet");
                }
                ui_control(box, .w = 48, .h = 48, .on = installer_act, .arg = 10 + k);
        }
}

static fn installer_layout(void)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        struct ui_trail address_to t = &big->trail;
        static const unsigned int steps[INSTALLER_TRAIL] = {LANGUAGE_installer_step_language, LANGUAGE_installer_step_keyboard,
                                                            LANGUAGE_installer_step_network,  LANGUAGE_installer_step_disk,
                                                            LANGUAGE_installer_step_apps,     LANGUAGE_installer_step_install};
        const char address_to head = null, address_to about = null;
        b32 rows = false, run = installer.step >= INSTALLER_STEP_RUN;

        if (!l->describe)
                l->describe = installer_describe, l->damaged = installer_damaged, l->hover = l->down = -1;
        l->count = installer.step == INSTALLER_STEP_LANGUAGE ? (int)INSTALLER_LANGUAGES
                   : installer.step == INSTALLER_STEP_KEYBOARD ? (int)INSTALLER_LAYOUTS
                   : installer.step == INSTALLER_STEP_DISK     ? big->drive_count
                                                               : 0;
        for (int k = 0; k < l->count; k++)
                l->id[k] = (unsigned short)k;
        l->ids = l->count > 0 ? l->count : 1;
        l->empty = installer.step == INSTALLER_STEP_DISK ? (installer.listed ? WORDS(installer_disk_none) : WORDS(installer_disk_looking)) : null;
        for (int k = 0; k < INSTALLER_TRAIL; k++)
                t->label[k] = language_text(steps[k]);
        t->count = INSTALLER_TRAIL;
        t->current = installer.step < INSTALLER_TRAIL ? installer.step : INSTALLER_TRAIL - 1;
        t->separator = "\xe2\x80\xba";
        t->hover = -1;

        switch (installer.step)
        {
        case INSTALLER_STEP_LANGUAGE:
                head = WORDS(installer_language_head), about = WORDS(installer_language_about), rows = true;
                break;
        case INSTALLER_STEP_KEYBOARD:
                head = WORDS(installer_keyboard_head), about = WORDS(installer_keyboard_about), rows = true;
                break;
        case INSTALLER_STEP_NETWORK:
                head = WORDS(installer_network_head), about = WORDS(installer_network_about);
                break;
        case INSTALLER_STEP_DISK:
                head = WORDS(installer_disk_head), about = WORDS(installer_disk_about), rows = true;
                break;
        case INSTALLER_STEP_APPS:
                head = WORDS(installer_apps_head), about = installer.choice.online ? WORDS(installer_apps_about) : WORDS(installer_apps_offline);
                break;
        case INSTALLER_STEP_REVIEW:
                head = WORDS(installer_review_head);
                break;
        case INSTALLER_STEP_RUN:
                head = WORDS(installer_running);
                break;
        default:
                head = WORDS(installer_done_head), about = WORDS(installer_done_about);
        }

        ui_view_begin(&big->view, ui, installer.width, -installer.height);
        ui_col(.class = "inset", .grow = 1)
        {
                ui_host(&big->trail_box, .h = 36);
                ui_text(head, .class = "title", .size = 22);
                if (about)
                        ui_text(about, .class = "quiet");
                if (rows)
                        ui_rows(l, .class = "list", .grow = 1);
                if (installer.step == INSTALLER_STEP_KEYBOARD)
                {
                        if (!big->try_it.kind)
                        {
                                big->try_it.kind = UI_INPUT, big->try_it.variant = UI_FIELD_BARE;
                                ui_control_start(&big->try_it);
                        }
                        if (!big->field.doc.selections)
                        {
                                ui_field_open(&big->field, &installer.own, "");
                                big->field.most = 63;
                                big->field.keeps_vertical = true;
                                ui_field_focus(&big->field, false);
                        }
                        big->try_it.edit = &big->field;
                        big->try_it.items[0] = WORDS(installer_keyboard_try);
                        big->try_it.focused = big->field.focused;
                        ui_control(&big->try_it, .h = 48);
                }
                if (installer.step == INSTALLER_STEP_NETWORK)
                {
                        if (!big->callout.kind)
                                big->callout.kind = UI_CALLOUT, ui_control_start(&big->callout);
                        big->callout.label = installer.choice.online ? WORDS(installer_network_online) : WORDS(installer_network_offline);
                        big->callout.role = installer.choice.online ? UI_SUCCESS : UI_INFO;
                        ui_control(&big->callout, .h = 70);
                        ui_row(.class = "gap3")
                                ui_button(WORDS(installer_network_wifi), .class = "action.outlined", .on = installer_act, .arg = 3);
                        ui_col(.grow = 1);
                }
                if (installer.step == INSTALLER_STEP_DISK)
                {
                        const struct installer_drive address_to d = installer.chosen >= 0 && installer.chosen < big->drive_count ? &big->drives[installer.chosen] : null;
                        struct ui_control address_to c = &big->modes;

                        if (c->kind != UI_TABS)
                                c->kind = UI_TABS, c->variant = UI_TABS_SEGMENTED, ui_control_start(c);
                        c->items[0] = WORDS(installer_mode_overwrite), c->items[1] = WORDS(installer_mode_update);
                        c->count = d && d->moonwater ? 2 : 1;
                        c->at = installer.choice.mode == INSTALLER_UPDATE && c->count == 2 ? 1 : 0;
                        c->disabled = !d;
                        ui_control(c, .class = "tabs", .w = 320, .on = installer_act, .arg = 4);
                        ui_text(c->at ? WORDS(installer_mode_update_about) : WORDS(installer_mode_overwrite_about), .class = "quiet");
                }
                if (installer.step == INSTALLER_STEP_APPS)
                {
                        ui_col(.class = "gap3", .grow = 1)
                        {
                                installer_card(0, "C", WORDS(installer_chrome), WORDS(installer_chrome_about), INSTALLER_CHROME);
                                if (installer.x86)
                                        installer_card(1, "S", WORDS(installer_steam), WORDS(installer_steam_about), INSTALLER_STEAM);
                                installer_card(2, ">_", WORDS(installer_ssh), WORDS(installer_ssh_about), INSTALLER_SSH);
                        }
                }
                if (installer.step == INSTALLER_STEP_REVIEW)
                {
                        const struct installer_drive address_to d = installer.chosen >= 0 && installer.chosen < big->drive_count ? &big->drives[installer.chosen] : null;
                        char size[24];

                        if (d)
                        {
                                installer_size(d->bytes, size, sizeof(size));
                                language_format(big->line[0], sizeof(big->line[0]), WORDS(installer_review_drive), d->model[0] ? d->model : d->name, size, d->name);
                        }
                        if (!big->callout.kind)
                                big->callout.kind = UI_CALLOUT, ui_control_start(&big->callout);
                        big->callout.label = d ? big->line[0] : "";
                        string_copy_bounded((string_address)big->callout.text,
                                            (string_address)(installer.choice.mode == INSTALLER_UPDATE ? WORDS(installer_review_update) : WORDS(installer_review_overwrite)),
                                            sizeof(big->callout.text));
                        big->callout.role = installer.choice.mode == INSTALLER_UPDATE ? UI_INFO : UI_WARNING;
                        ui_control(&big->callout, .h = 76);
                        language_format(big->line[1], sizeof(big->line[1]), WORDS(installer_review_settings),
                                        language_name_of(installer.choice.language), installer.choice.keyboard);
                        ui_text(big->line[1], .class = "quiet");
                        ui_col(.grow = 1);
                }
                if (run)
                {
                        if (!big->meter.kind)
                                big->meter.kind = UI_PROGRESS, ui_control_start(&big->meter);
                        big->meter.value = installer.step == INSTALLER_STEP_DONE ? UI_ONE
                                           : installer.at == 0 || !installer.commands ? -1
                                                                                      : UI_ONE / installer.commands * installer.at;
                        if (installer.step == INSTALLER_STEP_RUN)
                        {
                                language_format(big->line[0], sizeof(big->line[0]), WORDS(installer_running_step), installer.at + 1,
                                                installer.commands);
                                ui_text(big->line[0], .class = "quiet");
                                ui_control(&big->meter, .class = "meter");
                                if (installer.failed)
                                {
                                        language_format(big->line[1], sizeof(big->line[1]), WORDS(installer_failed), big->last);
                                        ui_text(big->line[1], .class = "warning");
                                }
                                else
                                        ui_text(big->last, .class = "quiet");
                        }
                        ui_col(.grow = 1);
                }
                if (big->note[0])
                        ui_text(big->note, .class = "warning");
                ui_row(.class = "foot")
                {
                        b32 back = installer.step > INSTALLER_STEP_LANGUAGE && (installer.step <= INSTALLER_STEP_REVIEW || installer.failed);

                        if (back)
                                ui_button(WORDS(installer_back), .class = "action.ghost", .on = installer_act, .arg = 0);
                        if (installer.step < INSTALLER_STEP_REVIEW)
                                ui_button(installer.step == INSTALLER_STEP_NETWORK && !installer.choice.online ? WORDS(installer_skip) : WORDS(installer_next),
                                          .class = "action.primary", .on = installer_act, .arg = 1);
                        else if (installer.step == INSTALLER_STEP_REVIEW)
                        {
                                language_format(big->says, sizeof(big->says), WORDS(installer_erase_again), installer.choice.drive);
                                ui_button(WORDS(installer_install), .class = "action.primary", .on = installer_act, .arg = 2,
                                          .says = installer.choice.mode == INSTALLER_OVERWRITE ? big->says : null);
                        }
                        else if (installer.step == INSTALLER_STEP_DONE)
                                ui_button(WORDS(installer_reboot), .class = "action.primary", .on = installer_act, .arg = 5);
                }
        }
        ui_view_end(&big->view);
        t->x = big->trail_box.x, t->y = big->trail_box.y, t->w = big->trail_box.w, t->h = big->trail_box.h;
        ui_trail_layout(ui, t);
}

// A button or a control of the view acted: Back (0), Next (1), Install (2), Wi-Fi (3), the mode (4), Restart (5), a card's box (10 on).
static b32 installer_act(int which)
{
        string_address face = installer_app.font ? installer_app.font : (string_address)UI_FONT_REGULAR;

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
                        installer_go(installer.step + 1);
                break;
        case 2:
                installer_install();
                break;
        case 3:
        {
                string_address words[4] = {(string_address) "/settings", (string_address) "-f", face, null};

                (void)ui_start_apart(words, 3);
                break;
        }
        case 4:
                installer.choice.mode = installer.big->modes.at == 1 ? INSTALLER_UPDATE : INSTALLER_OVERWRITE;
                break;
        case 5:
        {
                string_address words[4] = {(string_address) "/shell", (string_address) "-c", (string_address) "reboot", null};

                (void)ui_start_apart(words, 3);
                break;
        }
        default:
                if (which >= 10 && which < 13)
                        installer_toggle(which == 10 ? INSTALLER_CHROME : which == 11 ? INSTALLER_STEAM : INSTALLER_SSH);
        }
        installer.dirty = true;
        return true;
}

static fn installer_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &installer.ui;
        struct ui_list address_to l = &installer.big->list;

        ui_begin(ui, pixels, installer.own.table ? installer.own.table->stride : (unsigned long)installer.width, installer.width, installer.height);
        installer_layout();
        l->lit = l->focus >= 0, l->keys = installer.focused && !installer.big->view.group.keys && !installer.big->field.focused;
        ui_view_draw(ui, &installer.big->view, installer.focused);
        ui_trail_draw(ui, &installer.big->trail);
        if (installer.step == INSTALLER_STEP_KEYBOARD)
                ui_text_overlay(ui, &installer.big->field);
}

// A row of the step's list chosen: a language, a layout or a drive.
static fn installer_pick(int row)
{
        if (installer.step == INSTALLER_STEP_LANGUAGE)
                installer_pick_language(row);
        else if (installer.step == INSTALLER_STEP_KEYBOARD)
                installer_pick_layout(row);
        else if (installer.step == INSTALLER_STEP_DISK)
                installer_choose_drive(row);
}

static b32 installer_event_take(const struct window_event address_to event)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        p32 asks;
        int row;

        installer_layout();
        // The try-it field has the keys once it is pressed, and gives them back on Escape or a press off it.
        if (installer.step == INSTALLER_STEP_KEYBOARD && big->field.doc.selections)
        {
                if (event->type == WINDOW_EVENT_BUTTON && down)
                        ui_field_focus(&big->field, ui_text_inside(&big->field, ui_kit.pointer_x, ui_kit.pointer_y));
                asks = event->type != WINDOW_EVENT_KEY || big->field.focused
                           ? ui_field_event(ui, &big->field, null, event, ui_kit.pointer_x, ui_kit.pointer_y)
                           : 0;
                if (event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL))
                        asks = 0;
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_SUBMIT))
                        ui_field_focus(&big->field, false);
                if (asks)
                        return installer.dirty = true;
        }
        if (event->type == WINDOW_EVENT_KEY && down && event->a == UI_KEY_ESCAPE && installer.step != INSTALLER_STEP_RUN)
                return false;
        asks = ui_view_event(ui, &big->view, event);
        if (asks & UI_VIEW_QUIT)
                return false;
        installer.dirty |= (asks & UI_VIEW_DRAW) != 0;
        if (asks & UI_VIEW_TAKEN)
                return true;
        if (!l->count)
                return true;
        if (event->type == WINDOW_EVENT_KEY && down)
        {
                b32 moved = false;

                if (event->a == UI_KEY_ENTER)
                {
                        installer_pick(l->focus);
                        return true;
                }
                if (ui_list_key(ui, l, (int)event->a, &moved))
                {
                        if (moved)
                                installer_pick(l->focus);
                        installer.dirty = true;
                }
                return true;
        }
        if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
        {
                row = ui_list_click(ui, l, event, event->type == WINDOW_EVENT_MOTION ? event->a : ui_kit.pointer_x,
                                    event->type == WINDOW_EVENT_MOTION ? event->b : ui_kit.pointer_y);
                if (row >= 0)
                        installer_pick(row);
                installer.dirty = true;
        }
        return true;
}

// Whether an event is a key or a press from a device of the machine's own: no WINDOW_EVENT_VIRTUAL, which the compositor sets on
// what uinput, uhid, the on-screen keyboard and the Wayland shim send (Settings' deliberate rows read it so).
static b32 installer_hand_of(const struct window_event address_to event)
{
        return (event->type == WINDOW_EVENT_KEY || event->type == WINDOW_EVENT_BUTTON) && !(event->flags & WINDOW_EVENT_VIRTUAL);
}

// An event, taken with whether it is a key or a press from a device of the machine's own (installer.hand, for Install).
static b32 installer_event(const struct window_event address_to event)
{
        b32 taken;

        installer.hand = installer_hand_of(event);
        taken = installer_event_take(event);
        installer.hand = false;
        return taken;
}

// Whether the machine is online: a default route in the kernel's table.
static b32 installer_online(void)
{
        char text[4096];
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/net/route", (p8 address_to)text, sizeof(text) - 1);

        return got > 0 && installer_route_default(text, (positive)got);
}

static long installer_tick(long timeout, long address_to other)
{
        b64 now = (b64)ui_kit.now, wait;

        if (installer.output >= 0)
        {
                installer_drain();
                if (installer.output >= 0)
                        *other = installer.output;
        }
        if (installer.step == INSTALLER_STEP_KEYBOARD && installer.big->field.focused)
        {
                b32 redraw = false;

                timeout = ui_text_timeout(&installer.ui, &installer.big->field, timeout, &redraw);
                installer.dirty |= redraw;
        }
        if (now >= installer.next_look)
        {
                b32 was = installer.choice.online;

                installer.choice.online = installer_online();
                installer.dirty |= was != installer.choice.online;
                installer.next_look = now + INSTALLER_LOOK_NS;
        }
        wait = installer.next_look - now;
        if (installer.output >= 0 && wait > 100000000ll)
                wait = 100000000ll;
        return timeout < 0 || (b64)timeout > wait ? (long)(wait > 0 ? wait : 1) : timeout;
}

static fn installer_ready(void)
{
        installer_drain();
}

// The settings the session has now: what the steps start on.
static fn installer_settings(void)
{
        char text[16];
        bipolar got;

        string_copy_bounded((string_address)installer.choice.language, (string_address) "en", sizeof(installer.choice.language));
        string_copy_bounded((string_address)installer.choice.keyboard, (string_address) "us", sizeof(installer.choice.keyboard));
        got = file_slurp_once_at(AT_FDCWD, (string_address)SETTING_LANGUAGE_PATH, (p8 address_to)text, sizeof(text) - 1);
        if (got >= 2)
                text[2] = 0, string_copy_bounded((string_address)installer.choice.language, (string_address)text, sizeof(installer.choice.language));
        got = file_slurp_once_at(AT_FDCWD, (string_address) "/root/keyboard", (p8 address_to)text, sizeof(text) - 1);
        if (got >= 2)
        {
                positive length = 0;

                while (length < (positive)got && length < 7 && text[length] >= 'a' && text[length] <= 'z')
                        length++;
                text[length] = 0;
                if (length)
                        string_copy_bounded((string_address)installer.choice.keyboard, (string_address)text, sizeof(installer.choice.keyboard));
        }
}

// The state a new window starts in, the big part mapped already: the first step, the session's settings, nothing listed.
static fn installer_begin(void)
{
        installer.output = -1;
        installer.child = 0;
        installer.chosen = -1;
        installer.step = INSTALLER_STEP_LANGUAGE;
        installer.x86 = X64;
        installer_settings();
        installer_go(INSTALLER_STEP_LANGUAGE);
}

static int installer_open(void)
{
        if (ui_lift_other(&installer.own, (string_address) "installer", false))
                return 0;
        if (!(installer.big = ui_app_map(&installer_app, sizeof(struct installer_big))))
                return 1;
        installer_begin();
        installer_list();
        ui_app_place(&installer_app, ui_px(&installer.ui, INSTALLER_WIDTH), ui_px(&installer.ui, INSTALLER_HEIGHT));
        return -1;
}

static fn installer_close(void)
{
        if (installer.output >= 0)
                system_close(installer.output), installer.output = -1;
}

static struct ui_app installer_app = {
        .own = &installer.own,
        .ui = &installer.ui,
        .missing = (string_address) "installer: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Install Moonwater",
        .name = (string_address) "installer",
        .glass = WINDOW_GLASS, // the desktop's own glass: the first thing a live boot shows, over the wallpaper
        .nofont = (string_address) "installer: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &installer.dirty,
        .focused = &installer.focused,
        .ground = &installer.ground,
        .open = installer_open,
        .event = installer_event,
        .tick = installer_tick,
        .ready = installer_ready,
        .close = installer_close,
        .width = &installer.width,
        .height = &installer.height,
        .shown = &installer.shown,
        .draw = installer_draw,
};

static b32 installer_main()
{
        memory_zero(&installer, sizeof(installer));
        installer.moonwater = (string_address) "/moonwater";
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
