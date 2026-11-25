/*
        The share app: what the person is asked before anything of the screen leaves it, and where a share is stopped. The
        machine script starts it (share_button: the compositor says it when a program asks to share, and when the tray's share
        cell is pressed).

        While a program asks (a row of the table's casts WINDOWS_CAST_ASKED, filled by the session bus's ScreenCast portal),
        it opens in the middle of the screen with the program's name and what it may be shown, as the program asked: each
        screen, and each window that has a tab (not this one, nor a pop-up). Choosing one shares it (PICKED, and the
        compositor makes its ring); Don't share, Escape, or a press anywhere but on it, refuses (REFUSED), and the program is
        told the person said no. Asks are answered one at a time, the oldest first, each in a start of its own.

        With nothing asked it opens beside the tray's cell (-a) with what is shared, with whom, and a Stop for each and for all
        (STOPPED: the compositor drops the ring, the bus closes the session and the program is told it ended).

            share [-a] [-f FONT.ttf] [-b BOLD.ttf]
            share --pick screen N [sound] | --pick scene N [sound] | --pick window ROW|WORD | --refuse | --stop     the same, without the window: the oldest ask
                                                                             answered, or every share stopped (for the keys and checks)

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_SHARE
#define MOONWATER_CANVAS_SHARE

#define SHARE_TEXT 160
#define SHARE_ID_SCREEN 1                          // a screen is id SHARE_ID_SCREEN + its index
#define SHARE_ID_WINDOW (SHARE_ID_SCREEN + WINDOWS_OUTPUTS) // a window is SHARE_ID_WINDOW + its place in the list
#define SHARE_ID_SCENE (SHARE_ID_WINDOW + UI_MENU_ITEMS)   // a Scene is SHARE_ID_SCENE + its place in the table's list
#define SHARE_ID_SOUND (SHARE_ID_SCENE + WINDOWS_CAST_SCENES) // a screen with its sound is SHARE_ID_SOUND + its index
#define SHARE_ID_REFUSE (SHARE_ID_SOUND + WINDOWS_OUTPUTS)
#define SHARE_ID_STOP (SHARE_ID_REFUSE + 1)        // a share is SHARE_ID_STOP + its row
#define SHARE_ID_STOP_ALL (SHARE_ID_STOP + WINDOWS_CASTS)

static struct
{
        struct window_row own;
        struct ui ui;
        struct ui_popmenu menu;
        char text[UI_MENU_ITEMS][SHARE_TEXT];
        int window[UI_MENU_ITEMS];                 // the rows the window items are
        unsigned int window_pid[UI_MENU_ITEMS];
        int asked;                                 // the row of the casts being answered, -1 for none (the shares listed)
        unsigned int ticket;
        int width, height, shown, x, y, count;
        unsigned int ground;
        b32 focused, dirty, answered;
} share;

static struct ui_app share_app;

// The compositor rung after a row changed: it looks at the casts again.
static void share_ring(struct windows address_to table)
{
        bipolar door;

        __atomic_add_fetch(&table->cast_serial, 1u, __ATOMIC_ACQ_REL);
        door = system_open_at(AT_FDCWD, (string_address)WINDOWS_CLIP_DOOR, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (door >= 0)
                system_write_once((positive)door, "s", 1), system_close((positive)door);
}

// The oldest ask (by its ticket), or -1.
static int share_oldest(const struct windows address_to table)
{
        int found = -1;

        for (unsigned int k = 0; k < WINDOWS_CASTS; k++)
                if (__atomic_load_n(&table->casts[k].state, __ATOMIC_ACQUIRE) == WINDOWS_CAST_ASKED &&
                    (found < 0 || table->casts[k].ticket < table->casts[found].ticket))
                        found = (int)k;
        return found;
}

// Whether row is a window to offer: a program's, with a tab, on the table, not this app, of a size.
static b32 share_offered(const struct windows address_to table, unsigned int row, unsigned int self)
{
        return row != self && table->pid[row] && table->width[row] >= WINDOW_MIN_WIDTH && table->height[row] >= WINDOW_MIN_HEIGHT &&
               !(table->glass[row] & (WINDOW_GLASS | WINDOW_NO_TAB)) && !table->popup_parent[row] && !(table->state[row] & WINDOW_CLOSING) &&
               (table->title[row][0] || table->app_id[row][0]);
}

// An ask answered: the pick (kind and target, the window's program with it) or the refusal, if the row still asks with that ticket.
static b32 share_answer(struct windows address_to table, int at, unsigned int ticket, unsigned int kind, unsigned int target, unsigned int target_pid,
                        unsigned int sound)
{
        struct windows_cast address_to row;
        unsigned int asked = WINDOWS_CAST_ASKED;

        if (at < 0 || at >= WINDOWS_CASTS)
                return false;
        row = &table->casts[at];
        if (row->ticket != ticket)
                return false;
        row->kind = kind, row->target = target, row->target_pid = target_pid, row->sound = kind ? sound : 0;
        if (!__atomic_compare_exchange_n(&row->state, &asked, kind ? WINDOWS_CAST_PICKED : WINDOWS_CAST_REFUSED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return false;
        share_ring(table);
        return true;
}

static void share_stop(struct windows address_to table, int at)
{
        for (unsigned int k = 0; k < WINDOWS_CASTS; k++)
        {
                unsigned int live = WINDOWS_CAST_LIVE, picked = WINDOWS_CAST_PICKED;

                if (at >= 0 && (int)k != at)
                        continue;
                if (__atomic_compare_exchange_n(&table->casts[k].state, &live, WINDOWS_CAST_STOPPED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
                    __atomic_compare_exchange_n(&table->casts[k].state, &picked, WINDOWS_CAST_STOPPED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                        share_ring(table);
        }
}

// The menu: the oldest ask's choices, or the shares with their Stops.
static void share_build(void)
{
        struct windows address_to table = share.own.table;
        int n = 0;

        memory_zero(&share.menu, sizeof(share.menu));
        share.menu.hot = -1;
        share.asked = share_oldest(table);
        if (share.asked >= 0)
        {
                const struct windows_cast address_to row = &table->casts[share.asked];
                unsigned int outputs = table->outputs ? table->outputs : 1, rows = windows_rows(table);

                share.ticket = row->ticket;
                language_format(share.text[n], SHARE_TEXT, WORDS(share_asks), (long)row->app);
                ui_popmenu_add(&share.menu, share.text[n], null, UI_MENU_HEADER | UI_MENU_LITERAL, 0), n++;
                for (unsigned int k = 0; (row->types & WINDOWS_CAST_MONITOR) && k < outputs && k < WINDOWS_OUTPUTS && n < UI_MENU_ITEMS - 4; k++)
                {
                        if (outputs == 1)
                                language_format(share.text[n], SHARE_TEXT, WORDS(share_screen));
                        else
                                language_format(share.text[n], SHARE_TEXT, WORDS(share_screen_of), (long)(k + 1), (long)table->output_name[k]);
                        ui_popmenu_add(&share.menu, share.text[n], null, UI_MENU_LITERAL, SHARE_ID_SCREEN + (int)k), n++;
                        // and with what plays: the program may then record the sound's mix while the share lasts, and no longer
                        language_format(share.text[n], SHARE_TEXT, WORDS(share_with_sound), (long)share.text[n - 1]);
                        ui_popmenu_add(&share.menu, share.text[n], null, UI_MENU_LITERAL, SHARE_ID_SOUND + (int)k), n++;
                }
                // a Scene is a screen's worth of windows, so it is offered where a screen is: shown here whichever Scene the person is on
                for (unsigned int k = 0; (row->types & WINDOWS_CAST_MONITOR) && k < table->cast_scenes && k < WINDOWS_CAST_SCENES && n < UI_MENU_ITEMS - 4; k++)
                {
                        if (outputs == 1)
                                language_format(share.text[n], SHARE_TEXT, WORDS(share_scene), (long)table->cast_scene_place[k]);
                        else
                                language_format(share.text[n], SHARE_TEXT, WORDS(share_scene_of), (long)table->cast_scene_place[k],
                                                (long)(table->cast_scene_screen[k] + 1));
                        ui_popmenu_add(&share.menu, share.text[n], null, UI_MENU_LITERAL, SHARE_ID_SCENE + (int)k), n++;
                }
                if (row->types & WINDOWS_CAST_TOPLEVEL)
                        for (unsigned int r = 0; r < rows && n < UI_MENU_ITEMS - 3; r++)
                        {
                                if (!share_offered(table, r, (unsigned int)share.own.row))
                                        continue;
                                if (share.count == 0 && n > 1)
                                        ui_popmenu_add(&share.menu, null, null, UI_MENU_SEPARATOR, 0);
                                string_copy_bounded((p8 *)share.text[n], (string_address)(table->title[r][0] ? table->title[r] : table->app_id[r]), SHARE_TEXT);
                                share.window[share.count] = (int)r, share.window_pid[share.count] = table->pid[r];
                                ui_popmenu_add(&share.menu, share.text[n], null, UI_MENU_LITERAL, SHARE_ID_WINDOW + share.count), n++, share.count++;
                        }
                ui_popmenu_add(&share.menu, null, null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(&share.menu, WORDS(share_refuse), null, 0, SHARE_ID_REFUSE);
                return;
        }
        for (unsigned int k = 0; k < WINDOWS_CASTS && n < UI_MENU_ITEMS - 3; k++)
        {
                const struct windows_cast address_to row = &table->casts[k];
                unsigned int state = __atomic_load_n(&row->state, __ATOMIC_ACQUIRE);
                char what[SHARE_TEXT];

                if (state != WINDOWS_CAST_LIVE && state != WINDOWS_CAST_PICKED)
                        continue;
                if (row->kind == WINDOWS_CAST_WINDOW && row->target < windows_rows(table))
                        string_copy_bounded((p8 *)what, (string_address)table->title[row->target], sizeof what);
                else if (row->kind == WINDOWS_CAST_LINK) // a machine over Waterlink: what it hears of this one, or its sound played here
                        language_format(what, sizeof what, language_text(row->target == WINDOWS_CAST_LINK_MIC        ? LANGUAGE_share_your_microphone
                                                                         : row->target == WINDOWS_CAST_LINK_SPEAKERS ? LANGUAGE_share_your_speakers
                                                                                                                     : LANGUAGE_share_what_plays));
                else if (row->kind == WINDOWS_CAST_SCENE)
                {
                        unsigned int place = 0;

                        for (unsigned int j = 0; j < table->cast_scenes && j < WINDOWS_CAST_SCENES; j++)
                                if (table->cast_scene_id[j] == row->target)
                                        place = table->cast_scene_place[j];
                        language_format(what, sizeof what, WORDS(share_scene), (long)place);
                }
                else
                        language_format(what, sizeof what, WORDS(share_the_screen));
                language_format(share.text[n], SHARE_TEXT, WORDS(share_stop), (long)what, (long)row->app);
                ui_popmenu_add(&share.menu, share.text[n], null, UI_MENU_LITERAL | UI_MENU_ERROR, SHARE_ID_STOP + (int)k), n++;
        }
        if (n > 1)
                ui_popmenu_add(&share.menu, WORDS(share_stop_all), null, 0, SHARE_ID_STOP_ALL);
}

static void share_draw(unsigned int address_to pixels)
{
        ui_begin(&share.ui, pixels, share.own.table->stride, share.width, share.height);
        ui_popmenu_draw(&share.ui, &share.menu);
}

static b32 share_event(const struct window_event address_to event)
{
        struct windows address_to table = share.own.table;
        int answer;

        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && event->a == UI_KEY_ESCAPE)
                answer = UI_MENU_GONE;
        else
        {
                if (event->type == WINDOW_EVENT_MOTION)
                        share.x = event->a, share.y = event->b;
                ui_kit.now = (b64)ui_kit_clock();
                answer = ui_popmenu_event(&share.ui, &share.menu, event, share.x, share.y);
                share.dirty = true;
        }
        if (answer == UI_MENU_NOTHING || answer == UI_MENU_OPEN)
                return true;
        if (share.asked >= 0)
        {
                // a press anywhere but on it, or Escape, is Don't share: nothing is shared without a pick
                if (answer >= SHARE_ID_SCREEN && answer < SHARE_ID_WINDOW)
                        share_answer(table, share.asked, share.ticket, WINDOWS_CAST_SCREEN, (unsigned int)(answer - SHARE_ID_SCREEN) % WINDOWS_OUTPUTS, 0, 0);
                else if (answer >= SHARE_ID_SCENE && answer < SHARE_ID_SCENE + WINDOWS_CAST_SCENES)
                        share_answer(table, share.asked, share.ticket, WINDOWS_CAST_SCENE, table->cast_scene_id[answer - SHARE_ID_SCENE], 0, 0);
                else if (answer >= SHARE_ID_WINDOW && answer < SHARE_ID_WINDOW + share.count)
                        share_answer(table, share.asked, share.ticket, WINDOWS_CAST_WINDOW, (unsigned int)share.window[answer - SHARE_ID_WINDOW],
                                     share.window_pid[answer - SHARE_ID_WINDOW], 0);
                else if (answer >= SHARE_ID_SOUND && answer < SHARE_ID_SOUND + WINDOWS_OUTPUTS)
                        share_answer(table, share.asked, share.ticket, WINDOWS_CAST_SCREEN, (unsigned int)(answer - SHARE_ID_SOUND), 0, 1);
                else
                        share_answer(table, share.asked, share.ticket, 0, 0, 0, 0);
                share.answered = true;
                return false;
        }
        if (answer >= SHARE_ID_STOP && answer < SHARE_ID_STOP + WINDOWS_CASTS)
                share_stop(table, answer - SHARE_ID_STOP);
        else if (answer == SHARE_ID_STOP_ALL)
                share_stop(table, -1);
        return false;
}

static fn share_language(void)
{
        share_build();
        share.dirty = true;
}

static int share_open(void)
{
        if (ui_lift_other(&share.own, (string_address) "share", false))
                return 0;
        share_build();
        if (share.asked < 0 && share.menu.count == 0)
                return 0; // nothing asked, nothing shared: no window
        // an ask is in the middle of the screen, where the person looks; the shares beside the tray's cell
        share_app.beside = share.asked < 0 && share_app.beside;
        ui_popmenu_size(&share.ui, &share.menu, ui_app_room(&share_app));
        ui_app_place(&share_app, share.menu.width, share.menu.height);
        return -1;
}

static struct ui_app share_app = {
        .own = &share.own,
        .ui = &share.ui,
        .missing = (string_address) "share: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Share",
        .name = (string_address) "share",
        .glass = WINDOW_GLASS | WINDOW_DISMISS | WINDOW_NO_TAB,
        .nofont = (string_address) "share: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &share.dirty,
        .focused = &share.focused,
        .ground = &share.ground,
        .open = share_open,
        .language = share_language,
        .event = share_event,
        .dismiss = true,
        .width = &share.width,
        .height = &share.height,
        .shown = &share.shown,
        .draw = share_draw,
};

// The same answers without the window: the oldest ask picked or refused, or every share stopped. 0 when one was, 1 when none was.
static b32 share_command(positive at)
{
        struct windows address_to table = windows_board_map();
        string_address verb = program_argument(at);
        int asked;

        if (!table)
        {
                string_format(log, (string_address) "share: no desktop\n");
                return log_flush(), 2;
        }
        if (string_equals(verb, (string_address) "--stop"))
                return share_stop(table, -1), 0;
        if ((asked = share_oldest(table)) < 0)
        {
                string_format(log, (string_address) "share: nothing is asked\n");
                return log_flush(), 1;
        }
        if (string_equals(verb, (string_address) "--refuse"))
                return share_answer(table, asked, table->casts[asked].ticket, 0, 0, 0, 0) ? 0 : 1;
        if (string_equals(verb, (string_address) "--pick") && at + 2 < (positive)program_argument_count())
        {
                string_address what = program_argument(at + 1), number = program_argument(at + 2);
                unsigned int n = 0, sound = at + 3 < (positive)program_argument_count() && string_equals(program_argument(at + 3), (string_address) "sound");

                for (positive k = 0; number[k] >= '0' && number[k] <= '9' && n < 1000000; k++)
                        n = n * 10 + (unsigned int)(number[k] - '0');
                if (string_equals(what, (string_address) "screen"))
                        return share_answer(table, asked, table->casts[asked].ticket, WINDOWS_CAST_SCREEN, n, 0, sound) ? 0 : 1;
                // a Scene by its place among the first screen's (1 the first)
                if (string_equals(what, (string_address) "scene"))
                        for (unsigned int k = 0; k < table->cast_scenes && k < WINDOWS_CAST_SCENES; k++)
                                if (table->cast_scene_screen[k] == 0 && table->cast_scene_place[k] == n)
                                        return share_answer(table, asked, table->casts[asked].ticket, WINDOWS_CAST_SCENE, table->cast_scene_id[k], 0, sound) ? 0 : 1;
                // a window by its row, or the first offered whose app id or title holds the word
                if (string_equals(what, (string_address) "window") && !(number[0] >= '0' && number[0] <= '9'))
                        for (unsigned int r = 0; r < windows_rows(table); r++)
                                if (share_offered(table, r, ~0u) &&
                                    (string_find((string_address)table->app_id[r], number) || string_find((string_address)table->title[r], number)))
                                {
                                        n = r;
                                        break;
                                }
                if (string_equals(what, (string_address) "window") && n < windows_rows(table) && share_offered(table, n, ~0u))
                        return share_answer(table, asked, table->casts[asked].ticket, WINDOWS_CAST_WINDOW, n, table->pid[n], 0) ? 0 : 1;
        }
        string_format(log, (string_address) "share: --pick screen N [sound] | --pick scene N [sound] | --pick window ROW|WORD | --refuse | --stop\n");
        return log_flush(), 2;
}

static b32 share_main()
{
        memory_zero(&share, sizeof(share));
        share.asked = -1;
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took ? took : 1)
        {
                string_address arg = program_argument(at);

                if (arg[0] == '-' && arg[1] == '-')
                        return share_command(at);
                took = ui_app_option(&share_app, at);
        }
        {
                // an ask waits for the person's answer: in the middle of the screen, and not put away by a press elsewhere
                struct windows address_to table = windows_board_map();

                if (table && share_oldest(table) >= 0)
                        share_app.glass = WINDOW_GLASS | WINDOW_NO_TAB, share_app.dismiss = false, share_app.beside = false;
        }
        ui_app_run(&share_app);
        if (share.asked >= 0 && !share.answered)
                share_answer(share.own.table, share.asked, share.ticket, 0, 0, 0, 0);
        return 0;
}

#endif // MOONWATER_CANVAS_SHARE
