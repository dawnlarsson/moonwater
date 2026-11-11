/*
        The notices popup: the tray's notices dot, pressed, opens it beside the dot (the machine script starts it with -a, when the compositor
        says notices_button). It lists what the board holds (the kernel's notices and the options' issues, the most urgent first, up to six),
        each with its severity word at its right and, for a kernel notice, its count and the minutes since its last record; then two rows:
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
        struct ui_anchor anchor; // the dot the popup opens beside (-a): the kit's
        struct windows_issues board;
        char text[NOTICES_SHOWN][NOTICES_TEXT], keys[NOTICES_SHOWN][32];
        int width, height, shown, x, y;
        unsigned int ground;
        b32 focused, dirty, beside;
        string_address font;     // the face this is drawn in (-f), which the Settings it starts is drawn in too
        string_address bold;     // and the bold face (-b)
} notices;

// Reads the board and makes the menu: the items (errors before warnings, the board's order within each), then the two rows.
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
                                language_format(notices.text[count], sizeof(notices.text[count]), WORDS(notices_item_times), (long)key,
                                                (long)message, (long)issue->a, (long)(issue->b / 60));
                        else
                                language_format(notices.text[count], sizeof(notices.text[count]), WORDS(notices_item), (long)key, (long)message);
                        language_format(notices.keys[count], sizeof(notices.keys[count]), issue->severity == WINDOWS_ISSUE_ERROR
                                        ? WORDS(settings_badge_error) : WORDS(settings_badge_warning));
                        ui_popmenu_add(&notices.menu, notices.text[count], notices.keys[count], 0, NOTICES_ID_ITEM + count);
                        count++;
                }
        if (count)
                ui_popmenu_add(&notices.menu, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(&notices.menu, WORDS(notices_open), null, 0, NOTICES_ID_OPEN);
        if (count)
                ui_popmenu_add(&notices.menu, WORDS(notices_dismiss), null, 0, NOTICES_ID_DISMISS);
}

static void notices_size(unsigned int width, unsigned int height)
{
        notices.width = (int)width, notices.height = (int)height;
        // The labels are drawn through the kit's scratch line, which only ui_scratch_fit maps: a popup that maps none draws no text.
        ui_scratch_fit(&notices.ui, width);
        notices.dirty = true;
}

static void notices_dirty(void)
{
        notices.dirty = true;
}

static void notices_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &notices.ui;

        ui_begin(ui, pixels, notices.own.table->stride, notices.width, notices.height);
        ui_popmenu_draw(ui, &notices.menu);
}

static b32 notices_frame(void)
{
        return ui_app_present(&notices.own, &notices.shown, &notices.dirty, notices.width, notices.height, notices_draw, null);
}

// Another program started apart, as the machine script starts what is on the desktop (the mixer's Mixer row starts its own the same way).
static b32 notices_start(string_address address_to words, positive count)
{
        bipolar child = system_fork();
        positive status = 0;

        if (child < 0)
                return false;
        if (child == 0)
        {
                bipolar quiet;

                if (system_fork())
                        system_call_1(syscall(exit_group), 0);
                system_call(syscall(setsid));
                quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
                for (positive at = 0; quiet >= 0 && at < 3; at++)
                        system_call_3(syscall(dup3), (positive)quiet, at, 0);
                (void)shell_exec_file(words[0], words, count, window_asked_environment());
                system_call_1(syscall(exit_group), 127);
        }
        system_wait4_retry(child, address_of status, 0, null);
        return true;
}

// Settings on its System notices page: started with -n, which lifts a Settings already up and asks it for that page.
static b32 notices_settings(void)
{
        string_address face = notices.font ? notices.font : (string_address)UI_FONT_REGULAR;
        string_address words[5] = {(string_address) "/settings", (string_address) "-f", face, (string_address) "-n", null};

        return notices_start(words, 4);
}

// Dismiss all: every kernel notice leaves the board, and the feeder starts each class again from nothing.
static void notices_dismiss(void)
{
        windows_kernel_dismiss(notices.own.table, KERNEL_NOTICE_ALL);
}

static b32 notices_event(const struct window_event address_to event)
{
        int answer;

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
        notices_board();
        notices.dirty = true;
}

static int notices_open(void)
{
        struct windows address_to table = notices.own.table;
        unsigned int row = notices.own.row;
        int room = (int)table->output_height - ui_px(&notices.ui, 2 * (24 + 24));
        int width, height;

        // A second start finds the popup up and lifts it, and this one stops (the header's one window).
        if (ui_lift_other(&notices.own, (string_address) "notices", false))
                return 0;
        notices_board();
        ui_popmenu_size(&notices.ui, &notices.menu, room > 0 ? room : 0);
        width = notices.menu.width, height = notices.menu.height;
        notices_size((unsigned int)(width > (int)table->output_width ? (int)table->output_width : width), (unsigned int)height);
        ui_anchor_take(&notices.anchor, table, notices.beside);
        if (notices.anchor.anchored)
                ui_anchor_set(&notices.anchor, table, row, notices.width, notices.height);
        else
                ui_centre(table, row, notices.width, notices.height, ui_px(&notices.ui, 12));
        notices_frame();
        return -1;
}

static struct ui_app notices_app = {
        .own = &notices.own,
        .ui = &notices.ui,
        .missing = (string_address) "notices: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Notices",
        .name = (string_address) "notices",
        .glass = WINDOW_GLASS | WINDOW_DISMISS | WINDOW_NO_TAB, // glass over what is under it, put away by a press anywhere but on it, and no tab
        .nofont = (string_address) "notices: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &notices.dirty,
        .focused = &notices.focused,
        .ground = &notices.ground,
        .open = notices_open,
        .frame = notices_frame,
        .language = notices_language,
        .event = notices_event,
        .changed = notices_dirty,
        .size = notices_size,
        .dismiss = true, // and by the keys going elsewhere
};

static b32 notices_main()
{
        memory_zero(&notices, sizeof(notices));
        notices.shown = -1;
        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address value;

                if (string_equals(program_argument(at), (string_address) "-a"))
                {
                        notices.beside = true;
                        continue;
                }
                if (at + 1 >= (positive)program_argument_count())
                        break;
                if ((value = ui_argument(at, (string_address) "-f")))
                        notices_app.font = notices.font = value;
                else if ((value = ui_argument(at, (string_address) "-b")))
                        notices_app.bold = notices.bold = value;
                else
                        continue;
                at++;
        }
        return ui_app_run(&notices_app);
}

#endif // MOONWATER_CANVAS_NOTICES
