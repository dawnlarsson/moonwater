/*
        test/checks/compositor_places.c: where a window is put and kept, and the glass over a wallpaper.

        Rows of the compositor's manage.c and glass.c. One part of the program that the terminal's, the kit frames',
        the GPU kit's, the draw list's and the GPU model kit's sections share (checks.c opens and closes it). The
        terminal's program, the kit's frames, the draw list and the GPU model's kit run with the four faces:
        fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

/*
        Where a placed window goes (compositor_first_put and the rectangles under it). The room of a screen is the
        screen less the taskbar on each of its four edges, and the whole screen with no band or a band elsewhere;
        a window is kept on its screen whatever it asked, its top left on it when it is larger; centre is the
        middle of the room at the size asked, up to the room's; maximized fills the room and follows the band when
        it moves, and a double click gives it back the place it would have had; fullscreen is the whole screen; a
        window whose place is its own is where it asked, another asking the same place there too; and one with no
        place of its own (WINDOW_PLACE_FREE: a program that gave none, a Wayland toplevel, an X window with no
        USPosition) is centred in the room of the screen under the pointer, or on its owner's window, and kept
        in the room, at its top left when it is larger (place_free).
*/
static b32 place_same(const int address_to rect, int x, int y, int width, int height)
{
        return rect[0] == x && rect[1] == y && rect[2] == width && rect[3] == height;
}

static fn place_paint(int row, unsigned int how, int x, int y, unsigned int width, unsigned int height,
                      int program_x, int program_y, unsigned int picture_w, unsigned int picture_h)
{
        struct windows address_to table = compositor.table;

        table->place[row] = how;
        table->place_x[row] = x;
        table->place_y[row] = y;
        table->place_width[row] = width;
        table->place_height[row] = height;
        table->x[row] = program_x;
        table->y[row] = program_y;
        table->width[row] = picture_w;
        table->height[row] = picture_h;
        glass_paint(row, 0, 0, (int)picture_w, (int)picture_h, 0x203040u + (unsigned int)row, true);
}

static fn check_compositor_place(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
        static const char address_to const sides[4] = {"bottom", "top", "left", "right"};
        static const int rooms[4][4] = {{0, 0, 2560, 1440 - COMPOSITOR_PANEL}, {0, COMPOSITOR_PANEL, 2560, 1440 - COMPOSITOR_PANEL}, {COMPOSITOR_PANEL, 0, 2560 - COMPOSITOR_PANEL, 1440}, {0, 0, 2560 - COMPOSITOR_PANEL, 1440}};
        const int whole[4] = {0, 0, 2560, 1440}, none[4] = {0, 0, 0, 0}, elsewhere[4] = {2560, 1440 - COMPOSITOR_PANEL, 1920, COMPOSITOR_PANEL};
        const int big[4] = {0, 0, 1000, 800}, low[4] = {0, 0, 1000, 800 - COMPOSITOR_PANEL}, left[4] = {COMPOSITOR_PANEL, 0, 1000 - COMPOSITOR_PANEL, 800};
        const int screens[3][4] = {{0, 0, 1000, 800}, {1000, 0, 800, 600}, {-1280, 100, 1280, 1024}};
        struct window_place place;
        struct windows address_to table;
        int room[4], rect[4], x, y, sided = 1;

        for (unsigned int side = 0; side < 4; side++)
        {
                struct taskbar_view view;
                int band[4];

                compositor_zero();
                compositor.width = 2560;
                compositor.height = 1440;
                compositor.scale = 100;
                taskbar_settings_set(sides[side], "primary", "battery none\n");
                view = compositor_taskbar_view();
                taskbar_band(&view, &band[0], &band[1], &band[2], &band[3]);
                place_room(whole, band, view.position, room);
                sided &= place_same(room, rooms[side][0], rooms[side][1], rooms[side][2], rooms[side][3]);
                compositor_room(whole, rect);
                sided &= place_same(rect, rooms[side][0], rooms[side][1], rooms[side][2], rooms[side][3]);
        }
        taskbar_settings_set("bottom", "primary", "battery none\n");
        check("place: the room of a screen is the screen less the taskbar, on whichever of its four edges the band is", sided);
        place_room(whole, none, TASKBAR_BOTTOM, room);
        check("place: with no band the room is the whole screen", place_same(room, 0, 0, 2560, 1440));
        place_room(whole, elsewhere, TASKBAR_BOTTOM, room);
        check("place: and so it is with the band on another screen", place_same(room, 0, 0, 2560, 1440));
        place_room(rooms[0], (const int[4]){0, 0, 2560, 1440 - COMPOSITOR_PANEL}, TASKBAR_BOTTOM, room);
        check("place: a band as large as the screen leaves it whole rather than nothing", place_same(room, 0, 0, 2560, 1440 - COMPOSITOR_PANEL));

        x = 900, y = 700;
        place_clamp(big, 400, 300, 1, &x, &y);
        check("place: a window past the right and the bottom is kept on its screen, its line too", x == 599 && y == 499);
        x = -50, y = -9000;
        place_clamp(big, 400, 300, 1, &x, &y);
        check("place: and one at negative coordinates is moved in to the screen's top left", x == 1 && y == 1);
        x = 500, y = 500;
        place_clamp(big, 2000, 1500, 1, &x, &y);
        check("place: one larger than its screen has its top left on it, where it is taken by", x == 1 && y == 1);
        x = 5000, y = -5;
        place_clamp(screens[2], 200, 100, 0, &x, &y);
        check("place: on a screen left of the first, the clamp is that screen's", x == -200 && y == 100);

        memory_zero(&place, sizeof(place));
        place.how = WINDOW_PLACE_CENTER;
        check("place: centre is the middle of the room, the taskbar's strip left out",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 300, (800 - COMPOSITOR_PANEL - 300) / 2, 400, 300));
        check("place: and of a room the band takes the left of",
              place_rect(&place, big, left, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, COMPOSITOR_PANEL + (1000 - COMPOSITOR_PANEL - 400) / 2, 250, 400, 300));
        place.how = WINDOW_PLACE_CENTER | WINDOW_PLACE_SIZE;
        place.width = 5000, place.height = 5000;
        check("place: a size larger than the room is the room's, the line inside it",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 1, 1, 998, 800 - COMPOSITOR_PANEL - 2));
        place.width = 10, place.height = 1;
        check("place: and one under the least a window is made is the least",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 452, (800 - COMPOSITOR_PANEL - 48) / 2, 96, 48));
        check("place: a picture of nothing is placed at the least as well",
              (place.how = WINDOW_PLACE_CENTER, place_rect(&place, big, low, 5, 5, 0, 0, 1, 96, 48, rect)) ==
                      COMPOSITOR_FLOATING &&
                  place_same(rect, 452, (800 - COMPOSITOR_PANEL - 48) / 2, 96, 48));
        place.how = WINDOW_PLACE_AT;
        place.x = 250, place.y = 120;
        check("place: at is the top left asked for, from the screen's own top left",
              place_rect(&place, screens[1], screens[1], 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 1250, 120, 400, 300));
        place.x = -400, place.y = -30;
        check("place: at with negative coordinates is kept on the screen",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 1, 1, 400, 300));
        place.x = 16384, place.y = 16384;
        check("place: and at far off the screen is put back on it, a strip of nothing left beyond",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 599, 499, 400, 300));
        place.how = WINDOW_PLACE_AT | WINDOW_PLACE_SIZE;
        place.x = 10, place.y = 10, place.width = 16384, place.height = 16384;
        check("place: a size larger than the screen at a place is the screen's, at its top left",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 1, 1, 998, 798));
        place.how = WINDOW_PLACE_MAXIMIZED;
        check("place: maximized fills the room",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_ROOM && place_same(rect, 0, 0, 1000, 800 - COMPOSITOR_PANEL));
        place.how = WINDOW_PLACE_FULLSCREEN;
        check("place: fullscreen fills the screen, under the taskbar too",
              place_rect(&place, big, low, 5, 5, 400, 300, 1, 96, 48, rect) == COMPOSITOR_MAXIMIZED &&
                  place_same(rect, 0, 0, 1000, 800));
        place.how = WINDOW_PLACE_SIZE;
        place.width = 300, place.height = 200;
        check("place: a size with no mode is where the program asked, at the size",
              place_rect(&place, big, low, 37, 41, 400, 300, 1, 96, 48, rect) == COMPOSITOR_FLOATING &&
                  place_same(rect, 37, 41, 300, 200));

        check("place: a point is on the screen it is in, the nearest when it is between, the first of none",
              place_screen_at(1200, 300, screens, 3) == 1 && place_screen_at(-5, 300, screens, 3) == 2 &&
                  place_screen_at(1500, 700, screens, 3) == 1 && place_screen_at(10, 10, screens, 3) == 0 &&
                  place_screen_at(10, 10, screens, 0) == 0);

        // In the compositor: a 256 by 256 output, its band COMPOSITOR_PANEL high along the bottom, so its room is 256 by 256 less that.
        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = screen;
        compositor.scanout_pitch = GLASS_SIDE * 4;
        if (!glass_table_map())
        {
                check("place: the table is mapped", false);
                return;
        }
        table = compositor.table;

        place_paint(1, WINDOW_PLACE_CENTER, 0, 0, 0, 0, 3, 3, 100, 60);
        check("place: a window asked for the centre is in the middle of the room, the band's height left out",
              compositor.x[1] == 78 && compositor.y[1] == (256 - COMPOSITOR_PANEL - 60) / 2 && table->x[1] == 78 && table->y[1] == (256 - COMPOSITOR_PANEL - 60) / 2 &&
                  compositor.arranged[1] == COMPOSITOR_FLOATING);
        place_paint(2, WINDOW_PLACE_AT, 10, 20, 0, 0, 3, 3, 100, 60);
        check("place: one asked to be at 10,20 is there", compositor.x[2] == 10 && compositor.y[2] == 20);
        place_paint(3, WINDOW_PLACE_MAXIMIZED, 0, 0, 0, 0, 30, 40, 100, 60);
        check("place: maximized fills the room and is asked for its size",
              compositor.arranged[3] == COMPOSITOR_ROOM && compositor.x[3] == 0 && compositor.y[3] == 0 &&
                  compositor.w[3] == 256 && compositor.h[3] == 256 - COMPOSITOR_PANEL && table->want_width[3] == 256 &&
                  table->want_height[3] == 256 - COMPOSITOR_PANEL);
        check("place: and keeps the place and size it would have had with none, to be given back",
              compositor.saved_x[3] == 30 && compositor.saved_y[3] == 40 && compositor.saved_w[3] == 100 &&
                  compositor.saved_h[3] == 60);
        taskbar_settings_set("left", "primary", "battery none\n");
        compositor_room_follow();
        check("place: the band moved to the left, the maximized window fills the room it leaves",
              compositor.arranged[3] == COMPOSITOR_ROOM && compositor.x[3] == COMPOSITOR_PANEL && compositor.y[3] == 0 &&
                  compositor.w[3] == 256 - COMPOSITOR_PANEL && compositor.h[3] == 256 && table->want_width[3] == 256 - COMPOSITOR_PANEL &&
                  table->want_height[3] == 256);
        taskbar_settings_set("top", "primary", "battery none\n");
        compositor_room_follow();
        check("place: and moved to the top, the room under it",
              compositor.x[3] == 0 && compositor.y[3] == COMPOSITOR_PANEL && compositor.w[3] == 256 && compositor.h[3] == 256 - COMPOSITOR_PANEL);
        taskbar_settings_set("bottom", "primary", "battery none\n");
        compositor_room_follow();
        check("place: a window that asked for nothing does not move with the band", compositor.x[1] == 78 && compositor.y[1] == (256 - COMPOSITOR_PANEL - 60) / 2);

        place_paint(4, WINDOW_PLACE_CENTER | WINDOW_PLACE_SIZE, 0, 0, 120, 80, 3, 3, 100, 60);
        check("place: a size asked with the centre is asked of the program, and centred at it",
              compositor.x[4] == 68 && compositor.y[4] == (256 - COMPOSITOR_PANEL - 80) / 2 && table->want_width[4] == 120 && table->want_height[4] == 80 &&
                  compositor.w[4] == 120 && compositor.h[4] == 80);
        place_paint(5, WINDOW_PLACE_AUTO, 0, 0, 0, 0, 130, 140, 60, 40);
        place_paint(6, WINDOW_PLACE_AUTO, 0, 0, 0, 0, 130, 140, 60, 40);
        check("place: a window whose place is its own is where its program put it, and a second asking that place is there too",
              compositor.x[5] == 130 && compositor.y[5] == 140 && compositor.x[6] == 130 && compositor.y[6] == 140 &&
                  !table->want_width[5] && !table->want_width[6]);
        place_paint(7, WINDOW_PLACE_FULLSCREEN, 0, 0, 0, 0, 3, 3, 100, 60);
        check("place: fullscreen fills the output, as a double click does",
              compositor.arranged[7] == COMPOSITOR_MAXIMIZED && compositor.x[7] == 0 && compositor.y[7] == 0 &&
                  compositor.w[7] == 256 && compositor.h[7] == 256);
        table->place_screen[8] = 5;
        place_paint(8, WINDOW_PLACE_AT | WINDOW_PLACE_SCREEN, 20, 30, 0, 0, 3, 3, 100, 60);
        check("place: a screen that is not there is the first", compositor.x[8] == 20 && compositor.y[8] == 30);
        place_paint(9, 7, 20, 30, 0, 0, 44, 55, 100, 60);
        check("place: a mode the compositor does not know is no placement", compositor.x[9] == 44 && compositor.y[9] == 55);
        place_paint(10, WINDOW_PLACE_AT, 999999, -999999, 0, 0, 64, 85, 100, 60);
        check("place: nor is a place past WINDOW_PLACE_MOST, which a program writing its row itself could ask",
              compositor.x[10] == 64 && compositor.y[10] == 85);
        // Two that ask for one place off the screen (left of it) are each kept to the line inside, at the height asked.
        place_paint(11, WINDOW_PLACE_AUTO, 0, 0, 0, 0, -40, 100, 60, 40);
        place_paint(12, WINDOW_PLACE_AUTO, 0, 0, 0, 0, -40, 100, 60, 40);
        check("place: two windows that ask for a place off the screen are kept inside it where they asked",
              compositor.y[11] == 100 && compositor.y[12] == 100 && compositor.x[11] == compositor.x[12] && compositor.x[11] >= 0);

        // No place of its own: centred in the room of the screen under the pointer, wherever its x and y said (a Wayland
        // toplevel's 0,0, a kit app's own centre); a dialog on its owner; one larger than the room at the room's top left.
        compositor.pointer_x = 10, compositor.pointer_y = 10;
        place_paint(13, WINDOW_PLACE_FREE, 0, 0, 0, 0, 0, 0, 100, 60);
        check("place: a window with no place of its own is centred in the room, the band left out, not at the 0,0 it said",
              compositor.x[13] == 78 && compositor.y[13] == (256 - COMPOSITOR_PANEL - 60) / 2 && table->x[13] == 78);
        compositor_owner[14] = 13 + 1;
        place_paint(14, WINDOW_PLACE_FREE, 0, 0, 0, 0, 0, 0, 40, 20);
        check("place: a dialog with no place of its own is centred on its owner's window",
              compositor.x[14] == 78 + 30 && compositor.y[14] == compositor.y[13] + 20);
        compositor_owner[14] = 0;
        place_paint(15, WINDOW_PLACE_FREE, 0, 0, 0, 0, 0, 0, 240, 240);
        check("place: one taller than the room is kept to its top left, its line inside",
              compositor.y[15] == compositor_line(15) && compositor.x[15] == (256 - 240) / 2);
        {
                const int wide[4] = {0, 0, 1000, 760}, side[4] = {48, 0, 952, 800}, near_edge[4] = {900, 700, 100, 50};

                place_free(wide, 0, 200, 100, 0, &x, &y);
                check("place: with no owner, the middle of the room", x == 400 && y == 330);
                place_free(wide, near_edge, 200, 100, 0, &x, &y);
                check("place: on an owner at the room's corner, centred on it and kept inside the room", x == 800 && y == 660);
                place_free(wide, none, 200, 100, 0, &x, &y);
                check("place: an owner of no size is none", x == 400 && y == 330);
                place_free(side, 0, 1200, 900, 1, &x, &y);
                check("place: larger than the room, at the room's top left and its line (the band at the left left out)", x == 49 && y == 1);
        }

        // Two presses on the top of the maximized window: it is given back where it would have been.
        compositor_raise(3);
        compositor_focus(3);
        for (int press = 0; press < 2; press++)
        {
                compositor.pointer_x = 100;
                compositor.pointer_y = 5;
                compositor_button(COMPOSITOR_BTN_LEFT, 1);
                compositor_button(COMPOSITOR_BTN_LEFT, 0);
        }
        compositor.press = COMPOSITOR_NONE;
        check("place: a double click on a window that fills its room gives it back the place it would have had",
              compositor.arranged[3] == COMPOSITOR_FLOATING && compositor.x[3] == 30 && compositor.y[3] == 40 &&
                  table->want_width[3] == 100 && table->want_height[3] == 60);

        // Moved (`moonwater window move`): the row's placement written again and its moved counted up.
        {
                struct window_place moving;

                memory_zero(&moving, sizeof(moving));
                moving.how = WINDOW_PLACE_AT, moving.x = 40, moving.y = 50;
                windows_place_set(table, 2, &moving);
                compositor_take(2);
                check("place: a placement written again is nothing until moved is counted up", compositor.x[2] == 10 && compositor.y[2] == 20);
                table->moved[2]++;
                compositor_take(2);
                check("place: moved, a window that shows is put where its placement says now", compositor.x[2] == 40 && compositor.y[2] == 50);
                moving.how = WINDOW_PLACE_CENTER | WINDOW_PLACE_SIZE, moving.width = 120, moving.height = 80;
                windows_place_set(table, 2, &moving);
                table->moved[2]++;
                compositor_take(2);
                check("place: moved to the centre at a size, it is asked for the size and centred in the room at it",
                      compositor.x[2] == 68 && compositor.y[2] == (256 - COMPOSITOR_PANEL - 80) / 2 && table->want_width[2] == 120 && table->want_height[2] == 80);
                moving.how = WINDOW_PLACE_AT, moving.x = 10, moving.y = 12;
                windows_place_set(table, 7, &moving);
                table->moved[7]++;
                compositor_take(7);
                check("place: one that fills the output is taken out of it first, and put",
                      compositor.arranged[7] == COMPOSITOR_FLOATING && compositor.x[7] == 10 && compositor.y[7] == 12 &&
                          table->want_width[7] == 100 && table->want_height[7] == 60);
                moving.how = WINDOW_PLACE_MAXIMIZED;
                windows_place_set(table, 7, &moving);
                table->moved[7]++;
                compositor_take(7);
                check("place: and moved to maximized it fills the room", compositor.arranged[7] == COMPOSITOR_ROOM &&
                                                                              compositor.w[7] == 256 && compositor.h[7] == 256 - COMPOSITOR_PANEL);
                moving.how = WINDOW_PLACE_AT, moving.x = 5, moving.y = 5;
                windows_place_set(table, 2, &moving);
                table->moved[2]++;
                compositor.drag = 2;
                compositor_take(2);
                compositor.drag = COMPOSITOR_NONE;
                check("place: a window the hand holds is left to the hand", compositor.x[2] == 68 && compositor.y[2] == (256 - COMPOSITOR_PANEL - 80) / 2);
                moving.how = 0;
                windows_place_set(table, 2, &moving);
                table->moved[2]++;
                compositor_take(2);
                check("place: and a placement of nothing leaves a window where it is", compositor.x[2] == 68 && compositor.y[2] == (256 - COMPOSITOR_PANEL - 80) / 2);
        }
        compositor_call(syscall(munmap), (positive)compositor.table, compositor.pixels + compositor.buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0,
                        0, 0);
        compositor_zero();
}

/*
        Where the hand put a window, kept and given back (compositor_remember, compositor_recall): a named window
        let go of after a drag or a resize is written to the file, and the next window of that name opens there,
        at that size, a step down and across from one still there; a placement stands over it; a window with no
        name, one that fills a place, or any while remembering is off is not kept; and a kept place on a screen
        that is gone goes to the nearest one left, kept on it however far off it was.
*/
static fn place_file(const char address_to path, const char address_to text)
{
        bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

        if (handle >= 0)
        {
                system_write_once(handle, text, string_length((string_address)text));
                system_close(handle);
        }
}

static fn place_named(int row, const char address_to name, unsigned int how, int x, int y, int program_x, int program_y)
{
        struct windows address_to table = compositor.table;

        memory_zero(table->name[row], WINDOWS_NAME);
        if (name)
                memory_copy(table->name[row], name, string_length((string_address)name));
        // A size, when one is asked, of 50 by 30: under the least, which is what it is made.
        place_paint(row, how, x, y, 50, 30, program_x, program_y, 100, 60);
}

// The hand lets go of a window it dragged to x, y, the pointer on the window and far from the output's edges, which
// would arrange it.
static fn place_dragged(int row, int x, int y)
{
        compositor.drag = row;
        compositor.drag_moved = 1;
        compositor_place(row, x, y);
        compositor.drag_x = compositor.drag_y = 10;
        compositor.pointer_x = compositor.x[row] + 10;
        compositor.pointer_y = compositor.y[row] + 10;
        compositor_release(COMPOSITOR_BTN_LEFT, 1);
}

static fn check_compositor_remember(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
        static char places_path[64], remember_path[64], before[PLACES_BYTES + 1], after[PLACES_BYTES + 1];
        const int screens[3][4] = {{0, 0, 1000, 800}, {1000, 0, 800, 600}, {-1280, 100, 1280, 1024}};
        const char names[3][WINDOWS_NAME] = {"eDP-1", "HDMI-A-1", "DP-2"};
        struct place_kept kept;
        struct windows address_to table;
        positive length;

        memory_zero(&kept, sizeof(kept));
        memory_copy(kept.screen, "HDMI-A-1", 9);
        kept.origin_x = 2560, kept.x = 10, kept.y = 10, kept.width = 100, kept.height = 100;
        check("remember: a kept place goes on the screen of its connector's name, wherever that screen is now",
              place_kept_screen(&kept, names, screens, 3) == 1);
        memory_copy(kept.screen, "DP-9", 5);
        kept.origin_x = -1000, kept.origin_y = 300;
        check("remember: its screen gone, it goes on the one nearest where it was",
              place_kept_screen(&kept, names, screens, 3) == 2);
        kept.origin_x = 5000, kept.origin_y = 0;
        check("remember: from far right of them all, the nearest is the one on the right", place_kept_screen(&kept, names, screens, 3) == 1);
        check("remember: and with no screens it is the first", place_kept_screen(&kept, names, screens, 0) == 0);

        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = screen;
        compositor.scanout_pitch = GLASS_SIDE * 4;
        // One screen of the output model, named as a connector is: a headless compositor's has no name, and a place
        // is kept under its screen's name.
        compositor.outputs = 1;
        compositor.screen[0][2] = compositor.screen[0][3] = GLASS_SIDE;
        memory_copy(compositor.output[0].name, "screen", 7);
        if (!glass_table_map())
        {
                check("remember: the table is mapped", false);
                return;
        }
        table = compositor.table;
        length = positive_into_string((p8 address_to)places_path + 24, (positive)system_call(syscall(getpid)));
        memory_copy(places_path, "/tmp/moonwater-places-p.", 24);
        places_path[24 + length] = 0;
        memory_copy(remember_path, places_path, 25 + length);
        remember_path[22] = 'r';
        system_remove_at(AT_FDCWD, (string_address)places_path, 0);
        system_remove_at(AT_FDCWD, (string_address)remember_path, 0);
        compositor_places_path = places_path;
        compositor_remember_path = remember_path;

        place_named(1, "probe", WINDOW_PLACE_AUTO, 0, 0, 20, 20);
        check("remember: with nothing kept, a named window is where its program put it", compositor.x[1] == 20 && compositor.y[1] == 20);
        place_dragged(1, 120, 90);
        compositor_text(places_path, after, sizeof(after));
        check("remember: let go of after a drag, its place is written under its name, from its screen's top left",
              string_equals((string_address)after, "probe screen 0 0 120 90 100 60\n"));
        table->pid[1] = 1;
        compositor_leave(1);
        place_named(1, "probe", WINDOW_PLACE_AUTO, 0, 0, 20, 20);
        check("remember: the next window of the name opens where the hand put the last", compositor.x[1] == 120 && compositor.y[1] == 90);
        place_named(2, "probe", WINDOW_PLACE_AUTO, 0, 0, 20, 20);
        check("remember: and one more while it is there, a step down and across", compositor.x[2] == 140 && compositor.y[2] == 110);
        place_named(3, "probe", WINDOW_PLACE_AT, 30, 40, 20, 20);
        check("remember: a placement stands over what was kept", compositor.x[3] == 30 && compositor.y[3] == 40);
        place_named(4, "probe", WINDOW_PLACE_SIZE, 0, 0, 20, 20);
        check("remember: a size asked for is a placement too, where the program asked",
              compositor.x[4] == 20 && compositor.y[4] == 20 && compositor.w[4] == 96 && compositor.h[4] == 48);

        // A resize: the size the hand left is kept, and given back with the place.
        compositor.resize = 2;
        compositor_want(2, 180, 120);
        compositor_release(COMPOSITOR_BTN_LEFT, 1);
        compositor_text(places_path, after, sizeof(after));
        check("remember: let go of after a resize, its size is kept with its place",
              string_equals((string_address)after, "probe screen 0 0 75 110 180 120\n"));
        place_named(5, "probe", WINDOW_PLACE_AUTO, 0, 0, 20, 20);
        check("remember: and the next window of the name is asked for that size",
              table->want_width[5] == 180 && table->want_height[5] == 120 && compositor.w[5] == 180);

        place_named(6, null, WINDOW_PLACE_AUTO, 0, 0, 200, 10);
        compositor_text(places_path, before, sizeof(before));
        place_dragged(6, 60, 60);
        compositor_text(places_path, after, sizeof(after));
        check("remember: a window with no name is not kept", string_equals((string_address)before, (string_address)after));
        place_named(7, "full", WINDOW_PLACE_MAXIMIZED, 0, 0, 10, 10);
        compositor_remember(7);
        compositor_text(places_path, after, sizeof(after));
        check("remember: nor is one that fills a place", string_equals((string_address)before, (string_address)after));

        place_file(remember_path, "off\n");
        place_named(8, "other", WINDOW_PLACE_AUTO, 0, 0, 33, 33);
        place_dragged(8, 150, 150);
        compositor_text(places_path, after, sizeof(after));
        check("remember: with remembering off nothing is written", string_equals((string_address)before, (string_address)after));
        place_named(9, "probe", WINDOW_PLACE_AUTO, 0, 0, 22, 44);
        check("remember: and nothing kept is given back", compositor.x[9] == 22 && compositor.y[9] == 44);
        place_file(remember_path, "on\n");

        place_file(places_path, "gone HDMI-A-9 1920 0 100 50 120 80\nfar screen 0 0 5000 -9000 100 60\nnot a line\n");
        place_named(10, "gone", WINDOW_PLACE_AUTO, 0, 0, 3, 3);
        check("remember: a place kept on a screen that is gone is on the nearest left, from its top left",
              compositor.x[10] == 100 && compositor.y[10] == 50 && compositor.w[10] == 120);
        place_named(11, "far", WINDOW_PLACE_AUTO, 0, 0, 3, 3);
        check("remember: and one kept far off the screen is put back on it", compositor.x[11] == 155 && compositor.y[11] == 1);
        system_remove_at(AT_FDCWD, (string_address)places_path, 0);
        place_named(12, "far", WINDOW_PLACE_AUTO, 0, 0, 3, 3);
        check("remember: the file taken away (canvas windows forget), the window is where its program puts it",
              compositor.x[12] == 3 && compositor.y[12] == 3);

        system_remove_at(AT_FDCWD, (string_address)places_path, 0);
        system_remove_at(AT_FDCWD, (string_address)remember_path, 0);
        compositor_places_path = SETTING_WINDOWS_PLACES_PATH;
        compositor_remember_path = SETTING_WINDOWS_REMEMBER_PATH;
        compositor_call(syscall(munmap), (positive)compositor.table, compositor.pixels + compositor.buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0,
                        0, 0);
        compositor_zero();
}

// The mean of a pixel's channels, for the lift: how light a colour is, 0 to 255.
static unsigned int glass_lift_luma(unsigned int pixel)
{
        return ((pixel >> 16 & 255) + (pixel >> 8 & 255) + (pixel & 255)) / 3;
}

/*
        A glass window's look over a flat wallpaper, a dark one and a light one, on each theme. The glass is lifted from
        what is behind it (a dark glass lighter over the dark wallpaper and darker over the light one, by COMPOSITOR_GLASS_LIFT toward
        mid grey; a light glass light over both, toward a near white), its ink stands out UI_READABLE from the composed pixel, and its corners and line are a normal window's:
        the corner pixel is the wallpaper's and the edge pixel is the window's line. A window that is not glass is the
        control: the same geometry, its own colour inside, and the same corners and line.
*/
static void check_glass_lift(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
        static const unsigned int walls[] = {0x1e1e1eu, 0xe6e6e6u};
        unsigned int lifted = 0, inked = 0, cornered = 0, bordered = 0, plain = 0, scenes = 0;

        for (unsigned int theme = UI_THEME_LIGHT; theme <= UI_THEME_DARK; theme++)
                for (unsigned int at = 0; at < sizeof(walls) / sizeof(walls[0]); at++)
                        for (unsigned int glassy = 0; glassy < 2; glassy++)
                        {
                                unsigned int wall = walls[at], centre, corner, edge, ground, ink, quiet, desktop;
                                int lift;
                                struct windows address_to table;
                                struct ui probe;

                                compositor_zero();
                                compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
                                compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
                                compositor.width = compositor.height = GLASS_SIDE;
                                compositor.scale = 100;
                                compositor.card = -1;
                                compositor.scanout = screen;
                                compositor.scanout_pitch = GLASS_SIDE * 4;
                                if (!glass_table_map())
                                {
                                        check("glass lift: the table is mapped", false);
                                        return;
                                }
                                table = compositor.table;
                                compositor_theme_set(theme, table->accent, table->motion);
                                compositor_ground = wall;
                                table->x[2] = 40, table->y[2] = 40, table->width[2] = 160, table->height[2] = 120;
                                table->glass[2] = glassy ? WINDOW_GLASS : 0;
                                glass_paint(2, 0, 0, 160, 120, glassy ? 0u : 0x404040u, true);
                                compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
                                compositor_present();

                                // The contents are at 40, the line is the row above them: the corner is the pixel the round corner leaves out.
                                desktop = glass_at(5, 5), centre = glass_at(120, 100), corner = glass_at(40, 40), edge = glass_at(100, 39);
                                ground = table->ground[2];
                                memory_zero(&probe, sizeof(probe));
                                probe.glass = glassy != 0;
                                ui_inks(&probe, ground ? ground : wall);
                                ink = probe.ink, quiet = probe.quiet;
                                lift = (int)glass_lift_luma(centre) - (int)glass_lift_luma(wall);
                                scenes++;
                                if (glassy)
                                {
                                        // Dark glass: lighter over the dark wallpaper, darker over the light one, a lift either way. Light glass (the light
                                        // theme's) is light over both, never dimmer than COMPOSITOR_GLASS_LIGHTEST, a menu over a light window is no grey.
                                        lifted += theme == UI_THEME_LIGHT ? glass_lift_luma(centre) >= 0xdcu : at == 0 ? lift >= 16 : -lift >= 16;
                                        inked += ui_contrast(ink, centre) >= UI_READABLE && ui_contrast(quiet, centre) >= UI_READABLE;
                                        string_format(log, "  glass lift: theme %p, wallpaper %p: glass %p, lift %p%s, ink contrast %p hundredths\n",
                                                      (positive)theme, (positive)glass_lift_luma(wall), (positive)glass_lift_luma(centre),
                                                      (positive)(lift < 0 ? -lift : lift), lift < 0 ? " darker" : " lighter",
                                                      (positive)ui_contrast(ink, centre));
                                }
                                else
                                        plain += centre == 0x404040u && desktop == wall;
                                cornered += corner == desktop;
                                bordered += edge == compositor_line_unlit();
                                compositor_call(syscall(munmap), (positive)compositor.table, compositor.pixels + compositor.buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0,
                                                0, 0);
                                compositor_ground = COMPOSITOR_INK_DESKTOP;
                        }
        check("glass lift: over a dark wallpaper the glass is at least 16 levels lighter, over a light one as much darker, on both themes",
              lifted == 2 * 2);
        check("glass lift: the ink and the quiet ink stand out UI_READABLE from the composed glass, on both themes and wallpapers",
              inked == 2 * 2);
        check("glass lift: a glass window's corners are the wallpaper's, as a normal window's are, and its line is drawn",
              cornered == scenes && bordered == scenes);
        check("glass lift: a window that is not glass keeps its own colour, corners and line", plain == 2 * 2);
}

// The per-pixel grain the glass's rows are made with (memory_glass_over_u32 is the same operation over a row), the
// reference the rows below compare them against: whatever grain a pixel has, what memory_glass_over_u32 gives a clear
// pixel over ground at x, y. It was glass.c's until nothing there called it.
static unsigned int compositor_grained(unsigned int ground, unsigned int light, int x, int y)
{
        unsigned int out = 0;
        const unsigned int *grain = compositor_grain_row(light, x, y);

        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                int c = (int)(ground >> shift & 255) + (int)(grain[0] >> shift & 255);

                c = c > 255 ? 255 : c;
                c -= (int)(grain[64] >> shift & 255);
                out |= (unsigned int)(c < 0 ? 0 : c) << shift;
        }
        return out;
}

static fn check_glass(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE], made[160 * 160], shown[160 * 160];
        struct windows address_to table;
        unsigned int side, mismatched = 0, bounded = 1, kept = 1, distinct = 0, previous = 0, falling = 1;
        positive blurred, recomposed;

        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = screen;
        compositor.scanout_pitch = GLASS_SIDE * 4;
        if (!glass_table_map())
        {
                check("glass: the table is mapped", false);
                return;
        }
        table = compositor.table;

        // Under: red on the left, green on the right, from 10 to 250 each way.
        table->x[1] = table->y[1] = 10;
        table->width[1] = table->height[1] = 240;
        glass_paint(1, 0, 0, 120, 240, 0xff0000u, true);
        glass_paint(1, 120, 0, 120, 240, 0x00ff00u, true);

        // The glass over it, 40 to 200: clear, but for an opaque square and a half white one.
        table->x[2] = table->y[2] = 40;
        table->width[2] = table->height[2] = 160;
        table->glass[2] = WINDOW_GLASS;
        glass_paint(2, 0, 0, 160, 160, 0, true);
        glass_paint(2, 5, 5, 10, 10, 0xff123456u, true);
        glass_paint(2, 15, 30, 15, 10, 0x80808080u, true);
        compositor_present();
        side = compositor.glass_side[2];

        check("glass: an opaque pixel of the picture is its own colour", glass_at(50, 50) == 0x123456u);
        check("glass: a clear one far from any edge under it is the tint of the colour under it, with its grain",
              glass_at(62, 110) == compositor_grained(compositor_glass_tint(0xff0000u, side), side, 62, 110));
        check("glass: a half one is the picture over that",
              glass_at(62, 72) == glass_over(0x80808080u, compositor_grained(compositor_glass_tint(0xff0000u, side), side, 62, 72)));
        // Across the edge, the grain taken off: a ramp of a blur, not one step.
        for (int x = 90; x < 170; x += 2)
        {
                unsigned int red = glass_ungrained(glass_at(x, 150), side, x, 150) >> 16 & 255;

                distinct += x == 90 || red != previous;
                falling &= x == 90 || red <= previous;
                previous = red;
        }
        check("glass: the edge between red and green under it comes through smooth, many levels and no step back", distinct >= 12 && falling);
        for (int y = 40; y < 200; y++)
                for (int x = 40; x < 200; x++)
                {
                        unsigned int pixel = glass_ungrained(glass_at(x, y), side, x, y);
                        unsigned int r = pixel >> 16 & 255, g = pixel >> 8 & 255, b = pixel & 255;

                        if ((x - 40 >= 5 && x - 40 < 15 && y - 40 >= 5 && y - 40 < 15) ||
                            (x - 40 >= 15 && x - 40 < 30 && y - 40 >= 30 && y - 40 < 40) ||
                            (x - 40 < UI_RADIUS && y - 40 < UI_RADIUS) ||
                            (x - 40 >= 160 - UI_RADIUS && y - 40 < UI_RADIUS) ||
                            (x - 40 < UI_RADIUS && y - 40 >= 160 - UI_RADIUS) ||
                            (x - 40 >= 160 - UI_RADIUS && y - 40 >= 160 - UI_RADIUS) || (x >= 160 && y < 60))
                                continue;
                        kept &= side ? (r >= COMPOSITOR_GLASS_LIGHTEST && g >= COMPOSITOR_GLASS_LIGHTEST && b >= COMPOSITOR_GLASS_LIGHTEST)
                                     : (r <= COMPOSITOR_GLASS_DARKEST + 3 && g <= COMPOSITOR_GLASS_DARKEST + 3 &&
                                        b <= COMPOSITOR_GLASS_DARKEST + 3);
                }
        check("glass: every clear pixel is kept on its side of lightness, so an ink that stands out from the ground does everywhere", kept);
        check("glass: its program is told the ground, and knocked", table->ground[2] && (rows_low(&compositor.woken) & 4u));

        // Its own picture changed: composed again where it changed, and nothing blurred.
        blurred = compositor.blurred;
        recomposed = compositor.recomposed;
        glass_paint(2, 60, 60, 6, 4, 0xff000000u, false);
        compositor_present();
        check("glass: a change to its own picture blurs nothing again and composes only the change",
              compositor.blurred == blurred && compositor.recomposed == recomposed + 24 && glass_at(102, 102) == 0);

        // A window over it changes: nothing of the glass under it is made again.
        table->x[3] = 150;
        table->y[3] = 150;
        table->width[3] = table->height[3] = 40;
        glass_paint(3, 0, 0, 40, 40, 0x0000ffu, true);
        compositor_present();
        blurred = compositor.blurred;
        recomposed = compositor.recomposed;
        glass_paint(3, 4, 4, 8, 8, 0xffff00u, false);
        compositor_present();
        check("glass: a change to a window over it blurs nothing under it again",
              compositor.blurred == blurred && compositor.recomposed == recomposed);
        table->pid[3] = 3;
        compositor_leave(3);
        compositor_present();

        // The scene under it changes at random: blurred again within the reach of each change, and the same as
        // blurring all of it again from nothing.
        for (int round = 0; round < 40; round++)
        {
                // Every other one just outside it, where only the blur's reach brings it in.
                int x = round & 1 ? 201 + (int)render_next(24) : (int)render_next(225), y = (int)render_next(225),
                    width = 1 + (int)render_next(14),
                    height = 1 + (int)render_next(10), reach = 2 * COMPOSITOR_GLASS_SPREAD + 2;
                positive most = (positive)(width / COMPOSITOR_GLASS_BLOCK + reach) * (positive)(height / COMPOSITOR_GLASS_BLOCK + reach);
                positive cells;

                blurred = compositor.blurred;
                glass_paint(1, x, y, width, height, render_next(0x1000000), false);
                compositor_present();
                bounded &= compositor.blurred - blurred <= most;
                cells = (positive)compositor.glass_blocks[2][2] * (positive)compositor.glass_blocks[2][3];
                memory_copy(made, compositor.backdrop[2], cells * 4);
                memory_copy(shown, compositor.composed[2], sizeof(shown));

                compositor.glass_made[2][2] = 0;
                compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
                compositor_present();
                mismatched += memory_compare(made, compositor.backdrop[2], cells * 4) != 0 ||
                              memory_compare(shown, compositor.composed[2], sizeof(shown)) != 0;
        }
        check("glass: a change under it is blurred again in no more blocks than the blur reaches from it", bounded);
        check("glass: what is frosted again in part is what frosting all of it from nothing makes", !mismatched);

        // Lifted: the window under rises and takes the keys.
        table->lift[1]++;
        compositor_take(1);
        check("glass: a lift raises a window and gives it the keys",
              compositor.stack[compositor.stacked - 1] == 1 && compositor.focus == 1);

        table->pid[1] = 1;
        table->pid[2] = 2;
        compositor_leave(2);
        compositor_leave(1);
        check("glass: a glass window gone gives its pictures back", !compositor.backdrop[2] && !compositor.glassy[2]);
        compositor_call(syscall(munmap), (positive)compositor.table,
                        compositor.pixels + compositor.buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
        if (compositor.blur)
                compositor_call(syscall(munmap), (positive)compositor.blur, compositor.blur_bytes, 0, 0, 0, 0);
        compositor_zero();
}


/*
        C8 Scenes, step 1: the overview's layout is pure. A Scene's windows are laid out in a grid with no overlap, inside
        the area, each in its own aspect; the Scene strip is one card per Scene at the screen's aspect with "+" last; the hit
        test takes the first rect that holds the point.
*/
static void check_compositor_scenes_layout(void)
{
    static const int sizes[7][2] = {{1920, 1080}, {800, 600}, {600, 900}, {1024, 768}, {1280, 720}, {500, 500}, {1600, 900}};
    const int area[4] = {0, 40, 1000, 600};
    int out[7][4];
    int all_placed = 1, inside = 1, disjoint = 1, aspect = 1;

    for (unsigned int n = 1; n <= 7; n++)
    {
        unsigned int placed = compositor_scenes_grid(sizes, n, area, 16, out);

        all_placed &= placed == n;
        for (unsigned int i = 0; i < n; i++)
        {
            long long w = sizes[i][0], h = sizes[i][1], ow = out[i][2], oh = out[i][3];

            inside &= out[i][0] >= area[0] && out[i][1] >= area[1] && out[i][0] + out[i][2] <= area[0] + area[2] &&
                      out[i][1] + out[i][3] <= area[1] + area[3] && out[i][2] > 0 && out[i][3] > 0;
            // One pixel of the rect's height or width is w or h in these units: the cross product is within that of the ratio.
            aspect &= (ow * h - oh * w < 0 ? -(ow * h - oh * w) : ow * h - oh * w) <= w + h;
            for (unsigned int j = i + 1; j < n; j++)
                disjoint &= out[i][0] + out[i][2] <= out[j][0] || out[j][0] + out[j][2] <= out[i][0] ||
                            out[i][1] + out[i][3] <= out[j][1] || out[j][1] + out[j][3] <= out[i][1];
        }
    }
    check("scenes: a Scene of 1 to 7 windows places every window", all_placed);
    check("scenes: every window's rect is inside the area and has a size", inside);
    check("scenes: no two windows of a Scene overlap", disjoint);
    check("scenes: each window keeps its own aspect within a pixel of ratio", aspect);

    {
        const int strip_area[4] = {0, 0, 1000, 80};
        int cards[4][4];
        unsigned int count = compositor_scenes_strip(3, strip_area, 16, 9, 8, cards);
        int in = 1, order = 1, disjoint_cards = 1;

        for (unsigned int i = 0; i < count; i++)
        {
            in &= cards[i][0] >= 0 && cards[i][1] >= 0 && cards[i][0] + cards[i][2] <= 1000 && cards[i][1] + cards[i][3] <= 80;
            if (i)
                order &= cards[i][0] > cards[i - 1][0];
            for (unsigned int j = i + 1; j < count; j++)
                disjoint_cards &= cards[i][0] + cards[i][2] <= cards[j][0] || cards[j][0] + cards[j][2] <= cards[i][0];
        }
        check("scenes strip: one card per Scene and a \"+\" card last", count == 4);
        check("scenes strip: the cards are inside the strip, left to right, and do not overlap", in && order && disjoint_cards);
        check("scenes strip: a card is the screen's aspect within a pixel", cards[0][2] * 9 - cards[0][3] * 16 <= cards[0][2] + cards[0][3] &&
              cards[0][3] * 16 - cards[0][2] * 9 <= cards[0][2] + cards[0][3]);
        check("scenes hit: the centre of a card is that card, and a point off the strip is none",
              compositor_scenes_hit(cards, count, cards[1][0] + cards[1][2] / 2, cards[1][1] + cards[1][3] / 2) == 1 &&
              compositor_scenes_hit(cards, count, -5, -5) == -1);
    }
}

/*
        C8 Scenes, step 2: the overview's input. A click on a window focuses it and leaves; a click on a Scene card switches to
        that Scene and leaves; the "+" card adds a Scene and stays; a window dropped on a Scene card moves to it and the overview
        stays open; a click on nothing, a right button and Escape leave.
*/
static void scenes_at(int x, int y)
{
    compositor.pointer_x = x;
    compositor.pointer_y = y;
}

static void scenes_click(int x, int y)
{
    scenes_at(x, y);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
}

static int scenes_rect_of(int row, int out[4])
{
    for (unsigned int i = 0; i < compositor.scenes.windows; i++)
        if (compositor.scenes.row[i] == row)
        {
            memory_copy(out, compositor.scenes.rect[i], 4 * sizeof(int));
            return 1;
        }
    return 0;
}

static void check_compositor_scenes_input(void)
{
    int a[4], b[4], card[4];

    if (!layout_table_map(512))
    {
        check("scenes input: the table is mapped", false);
        return;
    }
    layout_setup(512, 1);
    compositor.table->motion = 1;
    compositor.outputs = 1;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 512, compositor.screen[0][3] = 512;
    compositor_spaces_ensure();
    layout_show(1, 40, 200, 120, 80);
    layout_show(2, 260, 200, 120, 80);
    compositor.stack[0] = 1, compositor.stack[1] = 2, compositor.stacked = 2;
    // layout_show marks a row no-tab (a row that is not in a Space); an ordinary window has a tab, so the windows are cleared of it.
    compositor.asks[1] = compositor.asks[2] = 0;
    compositor_space_assign_new(1, 0);
    compositor_space_assign_new(2, 0);

    compositor_scenes_open(0);
    check("scenes input: the overview opens with a card per Scene and a \"+\" card, and both windows of the Scene",
          compositor.scenes.open && compositor.scenes.cards == compositor_spaces_count_on(0) + 1 && compositor.scenes.windows == 2);

    // A click on a window focuses it and the overview leaves.
    scenes_rect_of(2, b);
    scenes_click(b[0] + b[2] / 2, b[1] + b[3] / 2);
    check("scenes input: a click on a window focuses it and the overview leaves", !compositor.scenes.open && compositor.focus == 2);

    // A click on a Scene card switches to that Scene and leaves.
    compositor_space_add_id(0, 0);
    compositor_space_switch_index(0, 1);
    compositor_scenes_open(0);
    memory_copy(card, compositor.scenes.card[0], sizeof(card));
    scenes_click(card[0] + card[2] / 2, card[1] + card[3] / 2);
    check("scenes input: a click on a Scene card switches to that Scene and the overview leaves",
          !compositor.scenes.open && compositor_spaces_active_index(0) == 0);

    // The "+" card adds a Scene and the overview stays open.
    compositor_scenes_open(0);
    {
        unsigned int before = compositor_spaces_count_on(0);
        int plus[4];

        memory_copy(plus, compositor.scenes.card[compositor.scenes.cards - 1], sizeof(plus));
        scenes_click(plus[0] + plus[2] / 2, plus[1] + plus[3] / 2);
        check("scenes input: the \"+\" card adds a Scene and the overview stays open",
              compositor.scenes.open && compositor_spaces_count_on(0) == before + 1);
    }
    compositor_scenes_close();

    // A window dropped on a Scene card moves to that Scene; the overview stays open.
    compositor_space_switch_index(0, 0);
    compositor_scenes_open(0);
    scenes_rect_of(1, a);
    {
        unsigned int target = compositor_scenes_sid(1);
        int dest[4];

        memory_copy(dest, compositor.scenes.card[1], sizeof(dest));
        scenes_at(a[0] + a[2] / 2, a[1] + a[3] / 2);
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        scenes_at(dest[0] + dest[2] / 2, dest[1] + dest[3] / 2);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        check("scenes input: a window dropped on a Scene card moves to it, and the overview stays open",
              compositor.scenes.open && compositor.row_space[1] == target);
    }

    // A press that moves to nothing leaves the window where it was.
    scenes_rect_of(1, a);
    {
        unsigned int before = compositor.row_space[1];

        scenes_at(a[0] + a[2] / 2, a[1] + a[3] / 2);
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        scenes_at(6, 6);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        check("scenes input: a window dropped on nothing stays where it was", compositor.row_space[1] == before);
    }

    // Escape leaves; so does a click on nothing and a right button.
    compositor_key(0, CK_KEY_ESC, 1, 0);
    check("scenes input: Escape leaves the overview", !compositor.scenes.open);
    compositor_scenes_open(0);
    scenes_click(6, 6);
    check("scenes input: a click on nothing leaves the overview", !compositor.scenes.open);
    compositor_scenes_open(0);
    compositor_button(COMPOSITOR_BTN_RIGHT, 1);
    check("scenes input: a right button leaves the overview", !compositor.scenes.open);


    // A press begun before the overview opened, and let go while it is open, is let go by the normal path: the row holds a
    // button and its grab as a press before the overview would, and the release must clear both.
    compositor_scenes_close();
    compositor.button_down = 1;
    compositor.button_row[0] = 1;
    compositor.grab = 1;
    compositor_scenes_open(0);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    check("scenes input: a press begun before the overview is let go by the normal path, and no button or grab stays held",
          (compositor.button_down & 1) == 0 && compositor.grab == COMPOSITOR_NONE);
}

/*
        C8 Scenes, step 3: the globe's drag. BEGIN starts it and opens nothing; MOVE sets its progress and velocity; END commits
        (the overview opens) over the swipe speed away from the band, cancels over it toward the band, and otherwise commits
        when progress is over half. Progress is clamped to 0..1024. Phases outside a drag, and a BEGIN while open, do nothing.
*/
static void check_compositor_scenes_drag(void)
{
    if (!layout_table_map(512))
    {
        check("scenes drag: the table is mapped", false);
        return;
    }
    layout_setup(512, 1);
    compositor.table->motion = 1;
    compositor.outputs = 1;
    compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 512, compositor.screen[0][3] = 512;
    compositor_spaces_ensure();

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, 0, 0, 0);
    check("scenes drag: BEGIN starts a drag and opens nothing", compositor.scenes.drag && !compositor.scenes.open);

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, 0, 600, 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, 0, 600, 0);
    check("scenes drag: a release past half, at no speed, commits: the overview opens", compositor.scenes.open && !compositor.scenes.drag);
    compositor_scenes_close();

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, 0, 0, 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, 0, 300, 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, 0, 300, 0);
    check("scenes drag: a release short of half, at no speed, cancels", !compositor.scenes.open && !compositor.scenes.drag);

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, 0, 0, 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, 0, 300, 1500);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, 0, 300, 1500);
    check("scenes drag: a short release flung away from the band commits by speed", compositor.scenes.open);
    compositor_scenes_close();

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, 0, 0, 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, 0, 900, -1500);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, 0, 900, -1500);
    check("scenes drag: a long drag flung back toward the band cancels by speed", !compositor.scenes.open);

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, 0, 0, 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, 0, 5000, 0);
    check("scenes drag: progress over the whole is clamped to 1024", compositor.scenes.progress == 1024);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, 0, -50, 0);
    check("scenes drag: progress back past the band is clamped to 0", compositor.scenes.progress == 0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, 0, 0, 0);
    check("scenes drag: dragged back to the band and let go, the overview does not open", !compositor.scenes.open);

    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, 0, 900, 0);
    check("scenes drag: an END without a BEGIN does nothing", !compositor.scenes.open && !compositor.scenes.drag);

    compositor_scenes_open(0);
    compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, 0, 0, 0);
    check("scenes drag: a BEGIN while the overview is open does nothing", !compositor.scenes.drag);
    compositor_scenes_close();
}
