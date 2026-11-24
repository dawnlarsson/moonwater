/*
        The notices popup: the tray's notices dot, pressed, opens it beside the dot (the machine script starts it with -a, when the compositor
        says notices_button). It lists what the board holds (the kernel's notices and the options' issues, the most urgent first, up to six),
        each with its severity in its colour (the taskbar's dot at its start and the word at its right) and, for a kernel notice, how often
        it came and how long since it last did (once or N times, just now or N min ago); then two rows:
        Open System notices, which starts Settings on its System notices page (-n), and Dismiss all, which dismisses the kernel's notices
        (windows_kernel_dismiss; an option's issue stays until it is cleared by its own program). Choosing an item opens Settings the same
        way. The menu is the kit's (ui_popmenu_*), drawn in this window, which is put away by a press anywhere but on it, by losing the keys
        and by Escape, as the mixer's popup is. Nothing is shown when the board is empty: the dot is not there then.

        The board is read when the popup opens and when the language changes; it is not followed while the popup is up.

            notices [-a] [-f FONT.ttf] [-b BOLD.ttf]

        One window: a second start finds the first and lifts it. -a opens it beside the cell that was pressed (the compositor leaves its
        rectangle, the edge its band is along and its screen in the table, as it does for the mixer's popup), kept on the screen.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_NOTICES
#define MOONWATER_CANVAS_NOTICES

#include "../../audio/control.c"
#include "../jingle.c"

#define NOTICES_SHOWN 6              // the items listed, the most urgent first
#define NOTICES_TEXT 512             // a line's bytes, its null with them
#define NOTICES_ID_ITEM 1            // a listed item is id NOTICES_ID_ITEM + its index: chosen, it opens Settings
#define NOTICES_ID_OPEN (NOTICES_ID_ITEM + NOTICES_SHOWN)
#define NOTICES_ID_DISMISS (NOTICES_ID_OPEN + 1)

static struct
{
        struct window_row own;
        struct ui ui;
        struct ui_popmenu menu;
        struct ui_alert alert;
        int alert_kind;
        const char address_to alert_title;
        const char address_to alert_msg;
        struct windows_issues board;
        char text[NOTICES_SHOWN][NOTICES_TEXT], keys[NOTICES_SHOWN][32];
        int width, height, shown, x, y;
        unsigned int ground;
        b32 focused, dirty;
} notices;

static struct ui_app notices_app; // the app, defined with its runner below: its faces (-f, -b) are the ones the Settings it starts is drawn in

/*
        A kernel notice's line: its key and its message, then how often it came and how long ago it last did, each in the form its
        number takes: once or N times, just now (under a minute) or N min ago. times is its count and seconds the time since its last.
*/
static fn notices_said(char address_to into, positive room, const char address_to key, const char address_to message, unsigned int times,
                       unsigned int seconds)
{
        long minutes = (long)(seconds / 60);

        if (times <= 1)
                language_format(into, room, minutes ? WORDS(notices_item_once) : WORDS(notices_item_once_now), (long)key, (long)message, minutes);
        else
                language_format(into, room, minutes ? WORDS(notices_item_times) : WORDS(notices_item_times_now), (long)key, (long)message,
                                (long)times, minutes);
}

// Reads the board and makes the menu: the items (errors before warnings, the board's order within each), then the two rows, each item
// marked with its severity (the taskbar's dot in its colour, and the word).
static void notices_board(void)
{
        int count = 0;
        unsigned int passes[2] = {WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_WARNING};

        if (!windows_issues_read(notices.own.table, &notices.board))
                memory_zero(&notices.board, sizeof(notices.board));
        memory_zero(&notices.menu, sizeof(notices.menu));
        notices.menu.hot = -1;
        for (int p = 0; p < 2 && count < NOTICES_SHOWN; p++)
                for (int i = 0; i < WINDOWS_ISSUES && count < NOTICES_SHOWN; i++)
                {
                        struct windows_issue *issue = &notices.board.issue[i];
                        char message[NOTICES_TEXT];
                        const char *key;

                        if (!issue->used || issue->severity != passes[p])
                                continue;
                        settings_issue_text(issue, message, sizeof(message));
                        key = (const char *)language_of(issue->key);
                        if (windows_issue_kernel(issue->code)) // the classes the feeder files; every other issue is an option's
                                notices_said(notices.text[count], sizeof(notices.text[count]), key, message, issue->a, issue->b);
                        else
                                language_format(notices.text[count], sizeof(notices.text[count]), WORDS(notices_item), (long)key, (long)message);
                        language_format(notices.keys[count], sizeof(notices.keys[count]), issue->severity == WINDOWS_ISSUE_ERROR
                                        ? WORDS(settings_badge_error) : WORDS(settings_badge_warning));
                        ui_popmenu_add(&notices.menu, notices.text[count], notices.keys[count],
                                       issue->severity == WINDOWS_ISSUE_ERROR ? UI_MENU_ERROR : UI_MENU_WARNING, NOTICES_ID_ITEM + count);
                        count++;
                }
        {
                const struct ui_menu_spec foot[] = {
                    {null, null, UI_MENU_SEPARATOR, 0, !count},
                    {WORDS(notices_open), null, 0, NOTICES_ID_OPEN, false},
                    {WORDS(notices_dismiss), null, 0, NOTICES_ID_DISMISS, !count},
                };

                ui_popmenu_build(&notices.menu, foot, (int)(sizeof(foot) / sizeof(foot[0])));
        }
}

static void notices_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &notices.ui;

        ui_begin(ui, pixels, notices.own.table->stride, notices.width, notices.height);
        if (notices.alert_kind)
        {
                int scale = ui->scale ? ui->scale : 100;
                ui_alert_draw(ui, &notices.alert, notices.width * 100 / scale, notices.height * 100 / scale);
        }
        else
                ui_popmenu_draw(ui, &notices.menu);
}

// Settings on its System notices page: started with -n, which lifts a Settings already up and asks it for that page.
static b32 notices_settings(void)
{
        string_address face = notices_app.font ? notices_app.font : (string_address)UI_FONT_REGULAR;
        string_address words[5] = {(string_address) "/settings", (string_address) "-f", face, (string_address) "-n", null};

        return ui_start_apart(words, 4);
}

// Dismiss all: every kernel notice leaves the board, and the feeder starts each class again from nothing.
static void notices_dismiss(void)
{
        windows_kernel_dismiss(notices.own.table, KERNEL_NOTICE_ALL);
}

static b32 notices_event(const struct window_event address_to event)
{
        int answer;

        if (notices.alert_kind)
        {
                int scale = notices.ui.scale ? notices.ui.scale : 100;
                ui_kit.now = (b64)ui_kit_clock();
                ui_alert_event(&notices.ui, &notices.alert, event, notices.width * 100 / scale, notices.height * 100 / scale, &notices.dirty);
                return notices.alert.open;
        }
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && event->a == UI_KEY_ESCAPE)
                return false;
        if (event->type == WINDOW_EVENT_MOTION)
                notices.x = event->a, notices.y = event->b;
        ui_kit.now = (b64)ui_kit_clock();
        answer = ui_popmenu_event(&notices.ui, &notices.menu, event, notices.x, notices.y);
        notices.dirty = true;
        if (answer == UI_MENU_NOTHING || answer == UI_MENU_OPEN)
                return true;
        if (answer == UI_MENU_GONE)
                return false;
        if (answer == NOTICES_ID_DISMISS)
        {
                notices_dismiss();
                return false;
        }
        if (answer == NOTICES_ID_OPEN || (answer >= NOTICES_ID_ITEM && answer < NOTICES_ID_OPEN))
                notices_settings();
        return false;
}

static fn notices_language(void)
{
        if (notices.alert_kind)
                return;
        notices_board();
        notices.dirty = true;
}

// The popup as large as its menu, beside the dot (-a) or in the middle. A second start finds the popup up and lifts it, and stops.
static int notices_open(void)
{
        if (notices.alert_kind)
        {
                ui_alert_open(&notices.alert, notices.alert_kind, notices.alert_title ? notices.alert_title : (notices.alert_kind == UI_ALERT_WARNING ? WORDS(settings_badge_warning) : WORDS(settings_badge_error)),
                              notices.alert_msg ? notices.alert_msg : "");
                {
                        struct ctl ctl;
                        struct canvas_jingle jingle;
                        memory_zero(&ctl, sizeof(ctl));
                        memory_zero(&jingle, sizeof(jingle));
                        ctl.timeout_ms = 60;
                        canvas_jingle_play(&ctl, &jingle, CTL_SOCKET, CANVAS_JINGLE_NOTICE, CTL_NONE, (long)(ui_kit_clock() / 1000000ll), 0);
                        ctl_disconnect(&ctl);
                }
                ui_app_place(&notices_app, ui_px(&notices.ui, UI_ALERT_W), ui_px(&notices.ui, UI_ALERT_H));
                return -1;
        }
        if (ui_lift_other(&notices.own, (string_address) "notices", false))
                return 0;
        notices_board();
        {
                struct ctl ctl;
                struct canvas_jingle jingle;
                memory_zero(&ctl, sizeof(ctl));
                memory_zero(&jingle, sizeof(jingle));
                ctl.timeout_ms = 60;
                canvas_jingle_play(&ctl, &jingle, CTL_SOCKET, CANVAS_JINGLE_POP, CTL_NONE, (long)(ui_kit_clock() / 1000000ll), 0);
                ctl_disconnect(&ctl);
        }
        ui_popmenu_size(&notices.ui, &notices.menu, ui_app_room(&notices_app));
        ui_app_place(&notices_app, notices.menu.width, notices.menu.height);
        return -1;
}

static struct ui_app notices_app = {
        .own = &notices.own,
        .ui = &notices.ui,
        .missing = (string_address) "notices: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Notices",
        .name = (string_address) "notices",
        .glass = WINDOW_GLASS | WINDOW_DISMISS | WINDOW_NO_TAB | WINDOW_NO_KEYS, // glass over what is under it, put away by a press anywhere but on it, no tab, and never the keys (a passive surface)
        .nofont = (string_address) "notices: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &notices.dirty,
        .focused = &notices.focused,
        .ground = &notices.ground,
        .open = notices_open,
        .language = notices_language,
        .event = notices_event,
        .dismiss = true, // and by the keys going elsewhere
        .width = &notices.width,
        .height = &notices.height,
        .shown = &notices.shown,
        .draw = notices_draw,
};

static b32 notices_main()
{
        memory_zero(&notices, sizeof(notices));
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took ? took : 1)
        {
                string_address arg = program_argument(at);
                if (string_equals(arg, (string_address) "-E") || string_equals(arg, (string_address) "--error"))
                {
                        notices.alert_kind = UI_ALERT_ERROR;
                        if (at + 1 < (positive)program_argument_count())
                                notices.alert_title = (const char *)program_argument(++at);
                        if (at + 1 < (positive)program_argument_count())
                                notices.alert_msg = (const char *)program_argument(++at);
                        took = 1;
                }
                else if (string_equals(arg, (string_address) "-W") || string_equals(arg, (string_address) "--warn"))
                {
                        notices.alert_kind = UI_ALERT_WARNING;
                        if (at + 1 < (positive)program_argument_count())
                                notices.alert_title = (const char *)program_argument(++at);
                        if (at + 1 < (positive)program_argument_count())
                                notices.alert_msg = (const char *)program_argument(++at);
                        took = 1;
                }
                else
                        took = ui_app_option(&notices_app, at);
        }
        if (notices.alert_kind)
        {
                notices_app.title = (string_address)(notices.alert_kind == UI_ALERT_WARNING ? "Warning" : "Error");
                notices_app.glass = 0;
                notices_app.dismiss = false;
        }
        return ui_app_run(&notices_app);
}

#endif // MOONWATER_CANVAS_NOTICES
