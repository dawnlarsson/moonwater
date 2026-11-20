/*
        The installer: the window a live boot opens on the desktop (the machine script's desktop_installer, on a verdict of live
        or ask; an installed boot's is disk, and starts none; the launcher offers it too, as Install Moonwater, where this
        session does not keep a drive inside the machine), which puts Moonwater on a drive in six steps. A stepper of them runs
        along its top (a step done is a check in the accent, the step it is at its number ringed in the accent, a step to come
        its number in the quiet ink) and Back and Next at its foot; what a step says is a column 640 wide in the middle.

            Language   the system's language, of the seventeen: chosen, it is the live session's at once (moonwater language),
                       so this window and every other speaks it, and the install carries it
            Keyboard   the layout, with a field to try it in: chosen, it is the session's at once (moonwater keyboard)
            Network    whether the machine is online (a default route), and the networks `moonwater wifi` hears, as Settings'
                       Wi-Fi list reads them: a press on one joins it (`moonwater wifi add NAME`, its passphrase typed in a field
                       here and given to the verb's input, never its words), and Turn on Wi-Fi where the radio is off; skippable
            Disk       the drives `moonwater install list` names, less the one this booted from, and for the one chosen,
                       Overwrite (the drive erased and installed anew) or, where it holds Moonwater, Update (its system and boot
                       files replaced, its data and settings kept) or Use (this build run with its data, and nothing written:
                       the three answers the terminal's question gave)
            Apps       Chrome, Steam (x86-64 alone) and SSH, a card each: its symbol, its name and a line, and a check box in the
                       accent; the whole card is pressed to choose it, and a card chosen is ringed in the accent. They are run once
                       the drive is written; offline, the cards are there, dim, cannot be chosen, and the line under the title says
                       why
            Install    what will be done, the drive named by its model, size and device; Install, and for an overwrite Install
                       pressed again, with a key or a press from a device of the machine's own: one from a virtual device
                       (WINDOW_EVENT_VIRTUAL: uinput, the on-screen keyboard, the Wayland shim) is refused, as a deliberate
                       Settings row refuses it

        Then the plan (installer_model.c) is run a command at a time, each one's last line said under the progress, and the
        window ends on Restart (Use ends with the drive's data kept, and nothing to restart for). A command that fails stops it
        there, with its words, and Back goes to the review.

            installer [-f FONT.ttf] [-b BOLD.ttf] [-m MOONWATER]

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_INSTALLER
#define MOONWATER_CANVAS_INSTALLER

#include "installer_model.c"

#define INSTALLER_WIDTH 900 // logical pixels
#define INSTALLER_HEIGHT 660
#define INSTALLER_COLUMN 640           // what a step says, in the middle of the room beside the steps
#define INSTALLER_STEP_ROW 48          // a step's row in it
#define INSTALLER_CARD_H 96            // an app's card
#define INSTALLER_LOOK_NS 1000000000ll // the network looked at this often
#define INSTALLER_WIFI_NS 4000000000ll // and the networks heard asked again this often, while the Network step shows
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
#define INSTALLER_STEPS 6 // the steps the stepper names: the run and its end are the last one's

// What the command running is for.
enum
{
        INSTALLER_JOB_NONE,
        INSTALLER_JOB_LIST,  // moonwater install list: the drives
        INSTALLER_JOB_WIFI,  // moonwater wifi: the networks heard
        INSTALLER_JOB_JOIN,  // moonwater wifi add, or wifi on
        INSTALLER_JOB_PLAN,  // a command of the plan
};

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

// An app's card: its bit, its symbol, its words.
static const struct
{
        unsigned int bit;
        int icon;
        unsigned int title, about;
} installer_apps[3] = {{INSTALLER_CHROME, UI_ICON_GLOBE, LANGUAGE_installer_chrome, LANGUAGE_installer_chrome_about},
                       {INSTALLER_STEAM, UI_ICON_GAMEPAD, LANGUAGE_installer_steam, LANGUAGE_installer_steam_about},
                       {INSTALLER_SSH, UI_ICON_KEY, LANGUAGE_installer_ssh, LANGUAGE_installer_ssh_about}};

struct installer_big
{
        struct ui_view view;
        struct ui_list list;                        // the step's choices: the languages, the layouts, the networks or the drives
        struct ui_rect stepper, side_box, field_box, pass_box, card_box[3], icon_box[3], tick_box[3];
        int step_hover;                             // the step's row under the pointer, -1 none
        struct ui_text field;                       // the keyboard's try-it field
        struct ui_text pass;                        // a network's passphrase
        struct ui_control try_it, pass_c, modes, meter, callout;
        struct ui_tween card_hover[3], card_press[3], card_on[3];
        int hover_card, press_card, focus_card;     // the card under the pointer, the one pressed, the one the keys are on (-1 none)
        struct installer_drive drives[INSTALLER_DRIVES];
        int drive_count;
        struct installer_network nets[INSTALLER_NETWORKS];
        int net_count, net_chosen;                  // the networks heard, and the one a passphrase is asked for (-1 none)
        b32 wifi_on, wifi_read;
        char said[INSTALLER_SAID];                  // what the command running said
        positive said_length;
        char last[160];                             // its last line
        char plan[INSTALLER_PLAN];
        char note[160];                             // what the window says of what was just refused
        char line[3][160];                          // the review's lines, a passphrase's question
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
        b32 network_seen;  // the Network step was shown (and is done once passed, online or not)
        int chosen;        // the drive chosen, by its place in drives, -1 none
        // The command running: its process and output, what it is for, which of the plan it is and how many there are, and whether one failed.
        bipolar child;
        b32 output;
        int job, at, commands;
        b32 failed;
        b64 next_look, next_wifi;
} installer;

static struct ui_app installer_app;

/* ---- the commands ---- */

/*
        A command started with its output to a pipe this end reads, and for its input a line of input (a passphrase, which the verb
        reads from its input and never from its words, where ps would show it) or nothing, so the verb's yes is its answer and it
        asks nothing: false when it could not be.
*/
static b32 installer_start(string_address address_to words, positive count, int job, const char address_to input)
{
        if (installer.output >= 0 || installer.child > 0 ||
            !ui_start_piped(words, count, input, &installer.child, &installer.output))
                return false;
        installer.job = job;
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
        positive length = 0, count;

        for (int k = 0; k < at && *c; c++)
                if (*c == '\n')
                        k++;
        while (c[length] && c[length] != '\n' && length + 1 < sizeof(line))
                line[length] = c[length], length++;
        line[length] = 0;
        if (!length)
                return false;
        count = installer_words(line, words, 8);
        // The plan names the moonwater command by its path in the image; one the window was given (-m) is that one.
        if (string_equals(words[0], (string_address) "/moonwater") && installer.moonwater)
                words[0] = installer.moonwater;
        return installer_start(words, count, INSTALLER_JOB_PLAN, null);
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
        if (installer.chosen == 0)
                string_copy_bounded((string_address)installer.choice.drive, (string_address)big->drives[0].name, sizeof(installer.choice.drive));
}

// The networks `moonwater wifi` said, read back.
static fn installer_heard(void)
{
        struct installer_big address_to big = installer.big;

        big->net_count = installer_wifi_parse(big->said, big->said_length, big->nets, INSTALLER_NETWORKS, &big->wifi_on);
        big->wifi_read = true;
        if (big->net_chosen >= big->net_count)
                big->net_chosen = -1;
}

static fn installer_ended(positive status);

/*
        The command whose output has ended, reaped if it has exited (WNOHANG), and then ended (installer_ended); if it has not, the tick
        asks again (installer_tick), and no other command starts meanwhile (installer_start). The output ends when the command closes it,
        which a step that runs on with its output sent elsewhere does before it exits: the wait for it here was the installer's window,
        frozen until it did, and for good for one stuck in the kernel.
*/
static fn installer_reap(void)
{
        positive status = 0;
        bipolar got;

        if (installer.child <= 0 || installer.output >= 0)
                return;
        got = system_wait4_retry(installer.child, address_of status, 1, null);
        if (got == 0)
                return;
        if (got < 0)
                status = 0;
        installer.child = 0;
        installer_ended(status);
}

// The command that ran has ended, its status said: what it was for, then the next of the plan, or the end of it.
static fn installer_ended(positive status)
{
        b32 ok = (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 0;
        int job = installer.job;

        installer.job = INSTALLER_JOB_NONE;
        installer_last_line();
        if (job == INSTALLER_JOB_LIST)
        {
                installer_listed();
                return;
        }
        if (job == INSTALLER_JOB_WIFI)
        {
                installer_heard();
                return;
        }
        if (job == INSTALLER_JOB_JOIN)
        {
                // A join said what came of it; the networks are asked again at once, so the one joined shows so.
                if (!ok)
                        string_copy_bounded((string_address)installer.big->note, (string_address)installer.big->last, sizeof(installer.big->note));
                installer.next_wifi = installer.next_look = 0;
                return;
        }
        if (job != INSTALLER_JOB_PLAN)
                return;
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
        if (installer.job == INSTALLER_JOB_PLAN)
                installer_last_line();
        installer.dirty = true;
        if (got == -11) // EAGAIN: it writes on
                return;
        system_close(installer.output);
        installer.output = -1;
        installer_reap();
}

// The drives asked for again, of the verb: what the window lists is what it says.
static fn installer_list(void)
{
        string_address words[4] = {installer.moonwater, (string_address) "install", (string_address) "list", null};

        if (installer.output >= 0)
                return;
        installer.listed = false;
        if (!installer_start(words, 3, INSTALLER_JOB_LIST, null))
                installer.listed = true, installer.big->drive_count = 0;
}

// The networks heard, asked again (the Network step's own feed, as Settings' Wi-Fi list runs `moonwater wifi`).
static fn installer_wifi(void)
{
        string_address words[3] = {installer.moonwater, (string_address) "wifi", null};

        if (installer.output < 0)
                installer_start(words, 2, INSTALLER_JOB_WIFI, null);
}

// A setting put in force in the live session at once, as the person chose it (moonwater language, moonwater keyboard).
static fn installer_apply(const char address_to verb, const char address_to value)
{
        string_address words[4] = {installer.moonwater, (string_address)verb, (string_address)value, null};

        //      A verb that could not be started is said on the step, as Restart's is: the session keeps what it had.
        if (!ui_start_apart(words, 3))
                string_copy_bounded((string_address)installer.big->note, (string_address)WORDS(settings_cannot_run),
                                    sizeof(installer.big->note));
}

/* ---- the model of the steps ---- */

// Whether Next goes on from the step: a drive is chosen on Disk; every other step can be passed.
static b32 installer_can_next(void)
{
        if (installer.step == INSTALLER_STEP_DISK)
                return installer.chosen >= 0 && installer.chosen < installer.big->drive_count;
        return installer.step < INSTALLER_STEP_REVIEW;
}

// The choice of the drive chosen, its mode one it can have: Update and Use only where it holds Moonwater.
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

/*
        The step Next goes to: the one after, but past Network while the machine is online (a default route), which is done by being so;
        the person can still press Network among the steps to look at the networks. A link that drops before Install puts it back in the way.
*/
static int installer_next_step(void)
{
        int next = installer.step + 1;

        return next == INSTALLER_STEP_NETWORK && installer.choice.online ? INSTALLER_STEP_DISK : next;
}

// Whether step k shows as done among the steps: a step passed, and Network while the machine is online and it is not the step shown, or
// once it was shown and passed (Skip); one passed by because it was online is not done once the link is gone.
static b32 installer_step_done(int k)
{
        int current = installer.step < INSTALLER_STEPS ? installer.step : INSTALLER_STEPS - 1;

        if (installer.step == INSTALLER_STEP_DONE)
                return true;
        if (k == INSTALLER_STEP_NETWORK && k != current)
                return installer.choice.online || (k < current && installer.network_seen);
        return k < current;
}

// Whether a press on step k goes to it: a step done, before the install runs (Network done by being online too, once Keyboard is passed).
static b32 installer_step_reachable(int k)
{
        if (installer.step > INSTALLER_STEP_REVIEW || k == installer.step || k < 0 || k >= INSTALLER_STEPS)
                return false;
        return k < installer.step || (k == INSTALLER_STEP_NETWORK && installer.choice.online && installer.step > INSTALLER_STEP_KEYBOARD);
}

// A step gone to: its list's rows made, and what a step needs on arriving (the drives listed, the networks asked).
static fn installer_go(int step)
{
        struct ui_list address_to l = &installer.big->list;

        if (step < INSTALLER_STEP_LANGUAGE || step > INSTALLER_STEP_DONE)
                return;
        installer.step = step;
        installer.big->note[0] = 0;
        installer.big->net_chosen = -1;
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
                installer.next_look = installer.next_wifi = 0, installer.network_seen = true;
        installer.dirty = true;
}

// A code chosen into a choice of room bytes (the language's, the keyboard's): where it is another, it is taken, marked changed and applied
// at once by its verb; the row is the list's lit one.
static fn installer_pick_code(int at, char address_to choice, positive room, b32 address_to changed, const char address_to verb, const char address_to code)
{
        if (!string_equals((string_address)choice, (string_address)code))
        {
                string_copy_bounded((string_address)choice, (string_address)code, room);
                *changed = true;
                installer_apply(verb, choice);
        }
        installer.big->list.focus = at;
        installer.dirty = true;
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

// A network pressed: an open one, or one saved, is joined at once; another asks its passphrase first.
static fn installer_pick_network(int at)
{
        struct installer_big address_to big = installer.big;
        const struct installer_network address_to n;
        string_address words[5];

        if (at < 0 || at >= big->net_count)
                return;
        n = &big->nets[at];
        big->list.focus = at;
        installer.dirty = true;
        if (n->joined)
                return;
        if (!n->open && !n->saved)
        {
                big->net_chosen = at;
                if (!big->pass.doc.selections)
                {
                        ui_field_open(&big->pass, &installer.own, "");
                        big->pass.most = 63;
                }
                else
                        ui_field_open(&big->pass, &installer.own, "");
                big->pass.masked = true;
                ui_field_focus(&big->pass, true);
                return;
        }
        words[0] = installer.moonwater, words[1] = (string_address) "wifi", words[2] = (string_address) "add", words[3] = (string_address)n->name, words[4] = null;
        installer_start(words, 4, INSTALLER_JOB_JOIN, null);
}

// The passphrase given: the network chosen joined with it, read by the verb from its input ("-").
static fn installer_join(void)
{
        struct installer_big address_to big = installer.big;
        char secret[96];
        positive length;
        string_address words[6];

        if (big->net_chosen < 0 || big->net_chosen >= big->net_count || !big->pass.doc.selections)
                return;
        length = doc_read(&big->pass.doc, 0, (p8 address_to)secret, sizeof(secret) - 1);
        secret[length] = 0;
        words[0] = installer.moonwater, words[1] = (string_address) "wifi", words[2] = (string_address) "add";
        words[3] = (string_address)big->nets[big->net_chosen].name, words[4] = (string_address) "-", words[5] = null;
        if (length && installer_start(words, 5, INSTALLER_JOB_JOIN, secret))
                big->net_chosen = -1, ui_field_focus(&big->pass, false);
        memory_zero(secret, sizeof(secret));
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
        if (!installer.commands || installer.output >= 0)
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
        struct installer_big address_to big = installer.big;

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
        else if (installer.step == INSTALLER_STEP_NETWORK && id < big->net_count)
        {
                const struct installer_network address_to n = &big->nets[id];

                row->label = n->name;
                row->bars = n->bars ? n->bars : 1;
                row->mark = n->joined;
                row->note = n->joined ? WORDS(installer_wifi_joined) : n->saved ? WORDS(installer_wifi_saved) : null;
        }
        else if (installer.step == INSTALLER_STEP_DISK && id < big->drive_count)
        {
                static char sizes[INSTALLER_DRIVES][24];
                const struct installer_drive address_to d = &big->drives[id];

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

// An app's card: a box the window paints (installer_cards_paint) with its symbol's place, its name and its line, and its check box's place.
static fn installer_card(int k)
{
        struct installer_big address_to big = installer.big;

        ui_row(.host = &big->card_box[k], .class = "panel.level.gap3", .h = INSTALLER_CARD_H)
        {
                ui_host(&big->icon_box[k], .w = 48, .h = 48);
                ui_col(.grow = 1, .class = "gap2")
                {
                        ui_text(language_text(installer_apps[k].title), .class = "title", .size = 18);
                        ui_text(installer.choice.online ? language_text(installer_apps[k].about) : WORDS(installer_apps_offline), .class = "quiet");
                }
                ui_host(&big->tick_box[k], .w = 24, .h = 24);
        }
}

// The cards there are: Steam on x86-64 alone.
static b32 installer_card_shown(int k)
{
        return k != 1 || installer.x86;
}

// The mode the Disk step's tabs show at an item, and the item a mode is at.
static int installer_mode_at(int item)
{
        return item == 1 ? INSTALLER_UPDATE : item == 2 ? INSTALLER_USE : INSTALLER_OVERWRITE;
}

static fn installer_layout(void)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        const char address_to head = null, address_to about = null;
        b32 rows = false, run = installer.step >= INSTALLER_STEP_RUN, use = installer.choice.mode == INSTALLER_USE;

        if (!l->describe)
                l->describe = installer_describe, l->damaged = installer_damaged, l->hover = l->down = -1;
        l->count = installer.step == INSTALLER_STEP_LANGUAGE ? (int)INSTALLER_LANGUAGES
                   : installer.step == INSTALLER_STEP_KEYBOARD ? (int)INSTALLER_LAYOUTS
                   : installer.step == INSTALLER_STEP_NETWORK  ? (big->wifi_on ? big->net_count : 0)
                   : installer.step == INSTALLER_STEP_DISK     ? big->drive_count
                                                               : 0;
        for (int k = 0; k < l->count; k++)
                l->id[k] = (unsigned short)k;
        l->ids = l->count > 0 ? l->count : 1;
        l->empty = installer.step == INSTALLER_STEP_DISK      ? (installer.listed ? WORDS(installer_disk_none) : WORDS(installer_disk_looking))
                   : installer.step == INSTALLER_STEP_NETWORK ? (big->wifi_read ? WORDS(installer_wifi_none) : WORDS(installer_wifi_looking))
                                                              : null;

        switch (installer.step)
        {
        case INSTALLER_STEP_LANGUAGE:
                head = WORDS(installer_language_head), about = WORDS(installer_language_about), rows = true;
                break;
        case INSTALLER_STEP_KEYBOARD:
                head = WORDS(installer_keyboard_head), about = WORDS(installer_keyboard_about), rows = true;
                break;
        case INSTALLER_STEP_NETWORK:
                head = WORDS(installer_network_head), about = WORDS(installer_network_about), rows = big->wifi_on;
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
                head = use ? WORDS(installer_use_done_head) : WORDS(installer_done_head);
                about = use ? WORDS(installer_use_done_about) : WORDS(installer_done_about);
        }

        ui_view_begin(&big->view, ui, installer.width, -installer.height);
        // The steps are the window's glass pane, its whole height at its start; what a step says is beside them.
        ui_side_frame()
        {
        ui_side_pane(&big->side_box)
                ui_host(&big->stepper, .grow = 1);
        ui_side_body(.class = "inset")
        {
                // The body's top is the window's top band: its words a margin under the window's edge, level with the first step.
                ui_host(null, .h = UI_PAD);
                ui_row(.class = "middle", .grow = 1)
                        ui_col(.grow = 1, .max_w = INSTALLER_COLUMN, .class = "gap3")
                        {
                                ui_text(head, .class = "title", .size = 24);
                                if (about)
                                        ui_text(about, .class = "quiet");
                                if (installer.step == INSTALLER_STEP_NETWORK)
                                {
                                        if (!big->callout.kind)
                                                big->callout.kind = UI_CALLOUT, ui_control_start(&big->callout);
                                        big->callout.label = installer.choice.online ? WORDS(installer_network_online) : WORDS(installer_network_offline);
                                        big->callout.text[0] = 0;
                                        big->callout.role = installer.choice.online ? UI_SUCCESS : UI_INFO;
                                        ui_control(&big->callout, .h = 52);
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
                                        if (!big->pass_c.kind)
                                                big->pass_c.kind = UI_INPUT, big->pass_c.variant = UI_FIELD_BARE, ui_control_start(&big->pass_c);
                                        language_format(big->line[2], sizeof(big->line[2]), WORDS(installer_wifi_password), big->nets[big->net_chosen].name);
                                        big->pass_c.edit = &big->pass, big->pass_c.items[0] = big->line[2], big->pass_c.focused = big->pass.focused;
                                        ui_row(.class = "gap3.level")
                                        {
                                                ui_control(&big->pass_c, .h = 48, .grow = 1, .host = &big->pass_box);
                                                ui_button(WORDS(installer_wifi_join), .class = "action.primary", .on = installer_act, .arg = 6);
                                        }
                                }
                                if (installer.step == INSTALLER_STEP_KEYBOARD)
                                {
                                        if (!big->try_it.kind)
                                                big->try_it.kind = UI_INPUT, big->try_it.variant = UI_FIELD_BARE, ui_control_start(&big->try_it);
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
                                        ui_control(&big->try_it, .h = 48, .host = &big->field_box);
                                }
                                if (installer.step == INSTALLER_STEP_DISK)
                                {
                                        const struct installer_drive address_to d = installer.chosen >= 0 && installer.chosen < big->drive_count ? &big->drives[installer.chosen] : null;
                                        struct ui_control address_to c = &big->modes;

                                        if (c->kind != UI_TABS)
                                                c->kind = UI_TABS, c->variant = UI_TABS_SEGMENTED, ui_control_start(c);
                                        c->items[0] = WORDS(installer_mode_overwrite), c->items[1] = WORDS(installer_mode_update), c->items[2] = WORDS(installer_mode_use);
                                        c->count = d && d->moonwater ? 3 : 1;
                                        c->at = c->count == 3 ? (installer.choice.mode == INSTALLER_UPDATE ? 1 : installer.choice.mode == INSTALLER_USE ? 2 : 0) : 0;
                                        c->disabled = !d;
                                        ui_control(c, .class = "tabs", .w = 420, .on = installer_act, .arg = 4);
                                        ui_text(c->at == 1 ? WORDS(installer_mode_update_about) : c->at == 2 ? WORDS(installer_mode_use_about) : WORDS(installer_mode_overwrite_about),
                                                .class = "quiet");
                                }
                                if (installer.step == INSTALLER_STEP_APPS)
                                        for (int k = 0; k < 3; k++)
                                                if (installer_card_shown(k))
                                                        installer_card(k);
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
                                                            (string_address)(installer.choice.mode == INSTALLER_UPDATE ? WORDS(installer_review_update)
                                                                             : use                                    ? WORDS(installer_review_use)
                                                                                                                      : WORDS(installer_review_overwrite)),
                                                            sizeof(big->callout.text));
                                        big->callout.role = installer.choice.mode == INSTALLER_OVERWRITE ? UI_WARNING : UI_INFO;
                                        ui_control(&big->callout, .h = 76);
                                        language_format(big->line[1], sizeof(big->line[1]), WORDS(installer_review_settings),
                                                        language_name_of(installer.choice.language), installer.choice.keyboard);
                                        ui_text(big->line[1], .class = "quiet");
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
                                }
                                if (big->note[0])
                                        ui_text(big->note, .class = "warning");
                                if (!rows)
                                        ui_col(.grow = 1);
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

// A button or a control of the view acted: Back (0), Next (1), Install (2), Wi-Fi on (3), the mode (4), Restart (5), Join (6), a card (10 on).
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
                        installer_go(installer_next_step());
                break;
        case 2:
                installer_install();
                break;
        case 3:
        {
                string_address words[4] = {installer.moonwater, (string_address) "wifi", (string_address) "on", null};

                installer_start(words, 3, INSTALLER_JOB_JOIN, null);
                break;
        }
        case 4:
                installer.choice.mode = installer_mode_at(installer.big->modes.at);
                break;
        case 5:
        {
                string_address words[4] = {(string_address) "/shell", (string_address) "-c", (string_address) "reboot", null};

                //      A restart that could not be started is said where the person pressed it, not left as nothing.
                if (!ui_start_apart(words, 3))
                        string_copy_bounded((string_address)installer.big->note, (string_address)WORDS(settings_cannot_run),
                                            sizeof(installer.big->note));
                break;
        }
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

/* ---- the window's own pieces: the stepper, the cards and their boxes, a field's well ---- */

// A box of the window, logical pixels from a rectangle in device pixels, in the kit's 256ths.
static struct ui_box installer_box(struct ui_rect r, int radius)
{
        return (struct ui_box){r.x * 256, r.y * 256, (r.x + r.w) * 256, (r.y + r.h) * 256, radius * 256, radius * 256};
}

// A check mark in a box of side size at x, y (device pixels): its strokes as far as share (Q30) of their length.
static fn installer_checkmark(struct ui address_to ui, int x, int y, int size, b64 share, unsigned int argb)
{
        b32 ax = (x * 256) + size * 256 * 27 / 100, ay = y * 256 + size * 256 * 52 / 100;
        b32 bx = (x * 256) + size * 256 * 43 / 100, by = y * 256 + size * 256 * 68 / 100;
        b32 cx = (x * 256) + size * 256 * 75 / 100, cy = y * 256 + size * 256 * 34 / 100;
        b32 thick = ui_q8(ui, 2);
        b64 first = share * 2 > UI_ONE ? UI_ONE : share * 2, second = share * 2 > UI_ONE ? share * 2 - UI_ONE : 0;

        if (first <= 0)
                return;
        ui_chevron_stroke(ui, ax, ay, ax + (b32)ui_qmul(first, bx - ax), ay + (b32)ui_qmul(first, by - ay), thick, argb);
        if (second > 0)
                ui_chevron_stroke(ui, bx, by, bx + (b32)ui_qmul(second, cx - bx), by + (b32)ui_qmul(second, cy - by), thick, argb);
}

/*
        The steps, down the glass pane, a row each: a step done is the accent's circle with a check in the ink on it, the step shown the
        accent's ring with its number in the accent, its row lit (the list's lit fill, a well of the glass's ink) and its name in the ink,
        a step to come a ring and its number in the quiet ink. A step a press goes to (ui: installer_step_reachable) has the ink's hover
        layer under the pointer. Reading from the right the rows are turned over the pane: the circle at the right.
*/
static fn installer_stepper(struct ui address_to ui)
{
        static const unsigned int steps[INSTALLER_STEPS] = {LANGUAGE_installer_step_language, LANGUAGE_installer_step_keyboard,
                                                            LANGUAGE_installer_step_network,  LANGUAGE_installer_step_disk,
                                                            LANGUAGE_installer_step_apps,     LANGUAGE_installer_step_install};
        struct ui_rect r = installer.big->stepper;
        int current = installer.step < INSTALLER_STEPS ? installer.step : INSTALLER_STEPS - 1, d = ui_dev(ui, 26), row = ui_dev(ui, INSTALLER_STEP_ROW);
        int pad = ui_dev(ui, UI_INSET);
        unsigned int accent = ui_role(UI_PRIMARY), quiet, strong;
        unsigned int line;

        ui_side_inks(ui, true);
        quiet = ui_text_colour(ui, UI_ON_SURFACE_VARIANT), strong = ui_text_colour(ui, UI_ON_SURFACE), line = ui_well(ui, UI_OUTLINE_VARIANT, 40);
        if (installer.step == INSTALLER_STEP_DONE)
                current = INSTALLER_STEPS;
        for (int k = 0; k < INSTALLER_STEPS; k++)
        {
                int top = r.y + k * row, cx = ui_mirror(r.x + pad, d, r.x, r.w) + d / 2, cy = top + row / 2;
                struct ui_box circle = {(cx - d / 2) * 256, (cy - d / 2) * 256, (cx - d / 2 + d) * 256, (cy - d / 2 + d) * 256, d * 128, d * 128};
                struct ui_box band = {r.x * 256, top * 256, (r.x + r.w) * 256, (top + row) * 256, 0, 0};
                const char address_to name = language_text(steps[k]);
                int baseline = ui_kit_baseline(ui, top + (row - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM), width = ui_kit_measure(ui, (string_address)name, UI_BODY_MEDIUM);
                char number[4] = {(char)('1' + k), 0, 0, 0};
                b32 done = installer_step_done(k);

                if (top >= r.y + r.h)
                        break;
                if (k == current)
                        ui_kit_fill(ui, &band, ui_kit_lit(ui), null);
                else if (k == installer.big->step_hover && installer_step_reachable(k))
                        ui_kit_fill(ui, &band, 0x14000000u | (strong & 0xffffffu), null);
                // The line down from a step to the next, in the accent once the step is done.
                if (k + 1 < INSTALLER_STEPS)
                        ui_kit_fill(ui, &(struct ui_box){cx * 256 - 128, (cy + d / 2 + ui_dev(ui, 3)) * 256, cx * 256 + 128 + (ui_dev(ui, 1) - 1) * 256,
                                                        (cy + row - d / 2 - ui_dev(ui, 3)) * 256, 0, 0},
                                    done ? accent : line, null);
                if (done && k != current)
                {
                        ui_kit_fill(ui, &circle, accent, null);
                        installer_checkmark(ui, cx - d / 2, cy - d / 2, d, UI_ONE, ui_role(UI_ON_PRIMARY));
                }
                else
                {
                        int nw = ui_kit_measure(ui, (string_address)number, UI_LABEL_LARGE);

                        ui_kit_ring(ui, &circle, ui_q8(ui, 2), k == current ? accent : line);
                        ui_kit_text_clip(ui, cx - nw / 2, ui_kit_baseline(ui, cy - ui_dev(ui, 10), UI_LABEL_LARGE), (string_address)number, UI_LABEL_LARGE,
                                         k == current ? accent : quiet, 0, ui->width);
                }
                ui_kit_text_clip(ui, ui_mirror(r.x + pad + d + ui_dev(ui, 12), width, r.x, r.w), baseline, (string_address)name, UI_BODY_MEDIUM,
                                 k == current || done ? strong : quiet, 0, ui->width);
        }
        ui_side_inks(ui, false);
}

// The step's row of the pane a point is on, or -1.
static int installer_step_at(int x, int y)
{
        struct ui_rect r = installer.big->stepper;
        int row = ui_dev(&installer.ui, INSTALLER_STEP_ROW), k;

        if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h || row <= 0)
                return -1;
        k = (y - r.y) / row;
        return k < INSTALLER_STEPS ? k : -1;
}

// The pointer over the steps: the hover follows it, a press on a step done goes to it (the left button, released on the same step).
static b32 installer_steps_event(const struct window_event address_to event)
{
        struct installer_big address_to big = installer.big;
        int x = event->type == WINDOW_EVENT_MOTION ? (int)event->a : ui_kit.pointer_x, y = event->type == WINDOW_EVENT_MOTION ? (int)event->b : ui_kit.pointer_y;
        int k = installer_step_at(x, y);

        if (event->type == WINDOW_EVENT_MOTION)
        {
                if (k != big->step_hover)
                        big->step_hover = k, installer.dirty = true;
                // A step a press goes to shows the hand, which also tells the compositor the window's top band gives way to it there.
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

// A field's well: a box with a hairline, the accent's ring of two when it has the keys.
static fn installer_well(struct ui address_to ui, struct ui_rect r, b32 focused)
{
        struct ui_box box = installer_box(r, ui_dev(ui, 10));

        if (r.w <= 0)
                return;
        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_LOWEST), null);
        ui_kit_ring(ui, &box, ui_q8(ui, focused ? 2 : 1), focused ? ui_role(UI_PRIMARY) : ui_role(UI_OUTLINE_VARIANT));
}

// The cards' grounds, under the view's words: the surface, and the state layer of the hover and the press, which the kit's motion moves.
static fn installer_cards_ground(struct ui address_to ui)
{
        struct installer_big address_to big = installer.big;

        for (int k = 0; k < 3; k++)
        {
                b32 on = (installer.choice.profiles & installer_apps[k].bit) && installer.choice.online;
                struct ui_box box = installer_box(big->card_box[k], ui_dev(ui, 18));
                b64 hover, press;
                unsigned int layer;

                if (!installer_card_shown(k) || big->card_box[k].w <= 0)
                        continue;
                ui_kit_number_to(&big->card_hover[k], big->hover_card == k && installer.choice.online ? UI_ONE : 0, 200, &ui_ease);
                ui_kit_number_to(&big->card_press[k], big->press_card == k && installer.choice.online ? UI_ONE : 0, 150, &ui_ease);
                ui_kit_number_to(&big->card_on[k], on ? UI_ONE : 0, 220, &ui_ease);
                hover = ui_kit_tween(&big->card_hover[k]), press = ui_kit_tween(&big->card_press[k]);
                ui_kit_fill(ui, &box, ui_role(UI_SURFACE_LOWEST), null);
                // The ink at 6 hundredths over a card hovered and 10 over one pressed, as a list row's layer.
                layer = (unsigned int)((ui_qmul(hover, UI_Q2(6)) + ui_qmul(press, UI_Q2(4))) * 255 / UI_ONE);
                if (layer)
                        ui_kit_fill(ui, &box, (ui_role(UI_ON_SURFACE) & 0xffffffu) | layer << 24, null);
        }
}

// What is over the cards' words: the symbol on its tile, the box and its check, the accent's ring of a card chosen, the keys' ring and the veil of a card that cannot be chosen.
static fn installer_cards_over(struct ui address_to ui)
{
        struct installer_big address_to big = installer.big;

        for (int k = 0; k < 3; k++)
        {
                struct ui_rect t = big->tick_box[k], i = big->icon_box[k];
                struct ui_box box = installer_box(big->card_box[k], ui_dev(ui, 18)), tick = installer_box(t, ui_dev(ui, 7)), tile = installer_box(i, ui_dev(ui, 12));
                b64 share = ui_kit_tween(&big->card_on[k]);
                unsigned int accent = ui_role(UI_PRIMARY), alpha = (unsigned int)(share * 255 / UI_ONE);

                if (!installer_card_shown(k) || big->card_box[k].w <= 0)
                        continue;
                ui_kit_fill(ui, &tile, ui_role(UI_PRIMARY_CONTAINER), null);
                ui_icon(ui, installer_apps[k].icon, i.x + i.w / 6, i.y + i.h / 6, i.w * 2 / 3, 0, ui_role(UI_ON_PRIMARY_CONTAINER));
                // The box: its outline off, filled in the accent on, the fill and the check moving in with the kit's ease.
                ui_kit_ring(ui, &tick, ui_q8(ui, 2), share >= UI_ONE ? accent : ui_role(UI_OUTLINE));
                if (alpha)
                {
                        ui_kit_fill(ui, &tick, (accent & 0xffffffu) | alpha << 24, null);
                        installer_checkmark(ui, t.x, t.y, t.w, share, ui_role(UI_ON_PRIMARY));
                }
                if (alpha)
                        ui_kit_ring(ui, &box, ui_q8(ui, 2), (accent & 0xffffffu) | alpha << 24);
                else
                        ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
                if (k == big->focus_card && installer.focused && !installer.big->view.group.keys)
                {
                        struct ui_box ring = {box.x0 - ui_q8(ui, 3), box.y0 - ui_q8(ui, 3), box.x1 + ui_q8(ui, 3), box.y1 + ui_q8(ui, 3), box.rx + ui_q8(ui, 3),
                                              box.ry + ui_q8(ui, 3)};

                        ui_kit_ring(ui, &ring, ui_q8(ui, 2), ui_role(UI_ON_SURFACE));
                }
                if (!installer.choice.online)
                        ui_kit_fill(ui, &box, (ui_role(UI_SURFACE) & 0xffffffu) | 0x8c000000u, null);
        }
}

static fn installer_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;

        ui_begin(ui, pixels, installer.own.table ? installer.own.table->stride : (unsigned long)installer.width, installer.width, installer.height);
        memory_zero(big->card_box, sizeof(big->card_box));
        big->field_box.w = big->pass_box.w = 0;
        installer_layout();
        ui_side_place(ui, big->side_box.x, big->side_box.w);
        // The lists of a step (the languages, the layouts, the networks, the drives) in stripes, every second row tinted.
        l->stripe = 2;
        l->lit = l->focus >= 0, l->keys = installer.focused && !big->view.group.keys && !big->field.focused && !big->pass.focused;
        if (installer.step == INSTALLER_STEP_APPS)
                installer_cards_ground(ui);
        if (installer.step == INSTALLER_STEP_KEYBOARD)
                installer_well(ui, big->field_box, big->field.focused);
        if (installer.step == INSTALLER_STEP_NETWORK && big->net_chosen >= 0)
                installer_well(ui, big->pass_box, big->pass.focused);
        ui_view_draw(ui, &big->view, installer.focused);
        installer_stepper(ui);
        if (installer.step == INSTALLER_STEP_APPS)
                installer_cards_over(ui);
        if (installer.step == INSTALLER_STEP_KEYBOARD)
                ui_text_overlay(ui, &big->field);
        if (installer.step == INSTALLER_STEP_NETWORK && big->net_chosen >= 0)
                ui_text_overlay(ui, &big->pass);
}

// A row of the step's list chosen: a language, a layout, a network or a drive.
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

// The card a point of the window is on, or -1.
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

// The cards' own events: the pointer's hover, a press and its release on the same card (which chooses it), and the keys among them.
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
                if (event->a == 57 /* Space */ && big->focus_card >= 0)
                {
                        installer_toggle(installer_apps[big->focus_card].bit);
                        return true;
                }
        }
        return false;
}

static b32 installer_event_take(const struct window_event address_to event)
{
        struct ui address_to ui = &installer.ui;
        struct installer_big address_to big = installer.big;
        struct ui_list address_to l = &big->list;
        b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
        struct ui_text address_to field = installer.step == INSTALLER_STEP_KEYBOARD                           ? &big->field
                                          : installer.step == INSTALLER_STEP_NETWORK && big->net_chosen >= 0 ? &big->pass
                                                                                                             : null;
        p32 asks;
        int row;

        installer_layout();
        // A field has the keys once it is pressed, and gives them back on Escape or a press off it; Enter in the passphrase joins.
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
                {
                        installer_pick(l->focus);
                        return true;
                }
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
        struct ui_text address_to field = installer.step == INSTALLER_STEP_KEYBOARD ? &installer.big->field
                                          : installer.step == INSTALLER_STEP_NETWORK ? &installer.big->pass
                                                                                     : null;

        if (installer.output >= 0)
        {
                installer_drain();
                if (installer.output >= 0)
                        *other = installer.output;
        }
        else
                installer_reap();
        if (field && field->focused)
        {
                b32 redraw = false;

                timeout = ui_text_timeout(&installer.ui, field, timeout, &redraw);
                installer.dirty |= redraw;
        }
        if (now >= installer.next_look)
        {
                b32 was = installer.choice.online;

                installer.choice.online = installer_online();
                installer.dirty |= was != installer.choice.online;
                installer.next_look = now + INSTALLER_LOOK_NS;
        }
        if (installer.step == INSTALLER_STEP_NETWORK && now >= installer.next_wifi && installer.output < 0)
        {
                installer_wifi();
                installer.next_wifi = now + INSTALLER_WIFI_NS;
        }
        wait = (installer.step == INSTALLER_STEP_NETWORK && installer.next_wifi < installer.next_look ? installer.next_wifi : installer.next_look) - now;
        if ((installer.output >= 0 || installer.child > 0) && wait > 100000000ll)
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
#ifdef X64
        installer.x86 = X64; // X64 is lib.c's only on x86-64: the build tool, built on an arm64 host, has none
#else
        installer.x86 = 0;
#endif
        installer.big->hover_card = installer.big->press_card = installer.big->focus_card = -1;
        installer.big->net_chosen = -1, installer.big->step_hover = -1;
        installer_settings();
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
        .glass = 0, // the kit's surface, as every window that stays is,
        .side = true, // with its steps the desktop's own glass beside it: the first thing a live boot shows, over the wallpaper
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
        installer.output = -1;
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
