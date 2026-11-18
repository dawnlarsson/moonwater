/*
        test/checks/apps.c: the kit's frames, and the terminal's own program: fields, inputs, the editor, menus,
        languages, the identify app and the terminal against a full draw.

        One part of the program that the terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model
        kit's sections share (checks.c opens and closes it). The terminal's program, the kit's frames, the draw list
        and the GPU model's kit run with the four faces: fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf
        fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_kit_frames
/*
        The kit's frames, for the filmstrips: not a check, a renderer. The settings window's gallery (the kit's
        every control at the places the kit's demo page has them in Chrome) is drawn on the light theme at scale
        1, driven through ui_kit_pointer by the same events at the same times as the reference recording drove
        the page (scratchpad kit/ref/record.mjs: hover, press, release), with a frame every 1/60 s, and the
        frames at the recording's shot times are written as PPM crops of the same rectangles:

            kit-frames REGULAR.ttf BOLD.ttf MONO.ttf OUTDIR [SCENARIO]

        A frame is ui_kit.now at the frame's time, then the page drawn whole.
*/
static unsigned int frames_pixels[GALLERY_W * GALLERY_H], frames_scratch[1400 * UI_SCRATCH_ROWS];
static p8 frames_atlas[1024 * 1024];
static font_face frames_faces[3];
static string_address frames_dir;
static b64 frames_now;
static positive frames_count;

static fn frames_at(b64 ms_millionths) // a frame at a time in nanoseconds
{
        ui_kit.now = ms_millionths;
        ui_kit.busy = 0;
        ui_kit_turn();
        settings.ui.band_top = settings.ui.band_bottom = 0;
        gallery_draw(&settings.ui);
        frames_count++;
}

static fn frames_write(string_address name, int ms, int x, int y, int w, int h)
{
        p8 path[256], head[64];
        positive length = 0;
        bipolar handle;
        static p8 row[3 * 1400];

        string_copy_bounded(path, frames_dir, sizeof(path));
        string_append_bounded(path, (string_address) "/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        string_append_bounded(path, (string_address) "-", sizeof(path));
        {
                p8 digits[8] = "0000";
                positive n = positive_into_string(digits + 4, (positive)ms);

                string_append_bounded(path, digits + n, sizeof(path)); // four places, zero filled
        }
        string_append_bounded(path, (string_address) ".ppm", sizeof(path));
        handle = system_open_at_mode(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle < 0)
                return;
        string_copy(head, (string_address) "P6\n");
        length = 3;
        length += positive_into_string(head + length, (positive)w);
        head[length++] = ' ';
        length += positive_into_string(head + length, (positive)h);
        string_copy(head + length, (string_address) "\n255\n");
        length += 5;
        system_write_once((b32)handle, head, length);
        for (int yy = y; yy < y + h; yy++)
        {
                for (int xx = x; xx < x + w; xx++)
                {
                        unsigned int pixel = frames_pixels[(positive)yy * GALLERY_W + (positive)xx];

                        row[(xx - x) * 3] = (p8)(pixel >> 16), row[(xx - x) * 3 + 1] = (p8)(pixel >> 8), row[(xx - x) * 3 + 2] = (p8)pixel;
                }
                system_write_once((b32)handle, row, (positive)w * 3);
        }
        system_close(handle);
}

// The events of a scenario: what the recording did, in its milliseconds from its start.
#define FRAMES_MOVE 1
#define FRAMES_DOWN 2
#define FRAMES_UP 3
#define FRAMES_SHOT 4
struct frames_step
{
        int ms_x100; // hundredths of a millisecond
        int what, x, y;
};

static fn frames_run(string_address name, const struct frames_step address_to steps, int count, int cx, int cy, int cw, int ch)
{
        int at = 0, since = steps[0].ms_x100; // since: the last event's time, which a shot is named after
        b64 next = (b64)steps[0].ms_x100 * 10000;

        gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
        gallery_layout(&settings.ui, settings.width, settings.height);
        ui_kit.busy = 0;
        // An event at its own time; a frame every 1/60 s from the last event (a shot is the frame at its time).
        while (at < count)
        {
                const struct frames_step address_to e = &steps[at];
                b64 when = (b64)e->ms_x100 * 10000;

                if (e->what == FRAMES_SHOT && when <= next + 1000000)
                {
                        at++;
                        frames_at(when);
                        frames_write(name, (e->ms_x100 - since + 50) / 100, cx, cy, cw, ch);
                        next = when + 16666667;
                        continue;
                }
                if (e->what != FRAMES_SHOT && when <= next)
                {
                        at++;
                        since = e->ms_x100;
                        ui_kit.now = when;
                        ui_kit_pointer(&settings.ui, gallery.control, gallery.count,
                                       e->what == FRAMES_MOVE ? WINDOW_EVENT_MOTION : WINDOW_EVENT_BUTTON, e->x, e->y, e->what == FRAMES_DOWN, 0);
                        next = when + 16666667;
                        continue;
                }
                frames_at(next);
                next += 16666667;
        }
}

/*
        The settings rows with issues fold (settings_toggle, settings_fold_step): the lines under an option open and shut on the kit's spring,
        the rows under them move with it each frame, and the expander's chevron turns with it. The page is the window's, with the Canvas group,
        the theme, the scale and the language's direction, and the board is the issues fold_issues gives: 1 a warning, 2 an error, 3 the GPU
        row's three (a warning, an error and a warning, the error's line with a detail).
*/
static struct windows *fold_table_scratch; // the table, mapped at its first use (windows_scratch_table)
static int fold_row = -1;
static const b64 fold_frame_ns = 16666667;

// The GPU row brought to the top of the list, so its lines show under it: the scroll is the row's top, held in range. The reveal of the
// keys is a notch the frames follow, and the shots are taken at once, so the scroll is set here.
static void fold_scroll(void)
{
        int at = ui_list_at(&settings.list, fold_row);

        if (at >= 0)
                settings.list.scroll.scroll = settings.list.top[at];
        ui_scroll_clamp(&settings.list.scroll);
}

static void fold_setup(int scale, b32 dark, unsigned int language_code)
{
        language_directory = "languages/"; // the tree's files, as the language checks read them (the rig runs in the tree)
        language_take(language_code);
        memory_zero(settings_expanded, sizeof(settings_expanded));
        memory_zero(settings_fold_at, sizeof(settings_fold_at));
        memory_zero(settings_fold_live, sizeof(settings_fold_live));
        windows_scratch_zero(windows_scratch_table(&fold_table_scratch));
        windows_scratch_table(&fold_table_scratch)->stride = GALLERY_W, windows_scratch_table(&fold_table_scratch)->output_width = 1920, windows_scratch_table(&fold_table_scratch)->output_height = 1080;
        settings.own.table = windows_scratch_table(&fold_table_scratch);
        settings.ui.scale = scale;
        ui_kit_theme(dark ? UI_THEME_DARK : UI_THEME_LIGHT, UI_ACCENT_STANDARD, 0);
        ui_kit_inks(&settings.ui);
        settings.ui.pitch = GALLERY_W, settings.ui.opaque = 1, settings.ui.pixels = frames_pixels;
        settings.ui.width = 700 * scale / 100, settings.ui.height = 520 * scale / 100;
        settings.ui.width = settings.ui.width > GALLERY_W ? GALLERY_W : settings.ui.width;
        gallery.on = 0;
        settings_size((unsigned int)settings.ui.width, (unsigned int)settings.ui.height);
        fold_row = -1;
        for (int row = 0; row < SETTINGS_ANY && fold_row < 0; row++)
        {
                char key[WINDOWS_ISSUE_KEY];

                if (settings_key_of(row, key, sizeof(key)) && string_equals((string_address)key, (string_address)WINDOWS_KEY_GPU))
                        fold_row = row;
        }
        settings_group_choose(settings_group_of(fold_row));
        settings.list.focus = fold_row;
        settings_gpu_sample(); // the GPU's status and metric lines say what the table counts (none yet: the card is idle)
        settings_rebuild();
        fold_scroll();
}

static void fold_issues(int which)
{
        windows_issue_clear(windows_scratch_table(&fold_table_scratch), WINDOWS_KEY_GPU, WINDOWS_ISSUE_ANY);
        if (which == 1 || which == 3)
                windows_issue_set(windows_scratch_table(&fold_table_scratch), WINDOWS_KEY_GPU, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, 0, 0, "");
        if (which == 2 || which == 3)
                windows_issue_set(windows_scratch_table(&fold_table_scratch), WINDOWS_KEY_GPU, WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, 1, 2, "the kernel's own words");
        if (which == 3)
                windows_issue_set(windows_scratch_table(&fold_table_scratch), WINDOWS_KEY_GPU, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_COOLING, 3, 0, "");
        windows_issues_read(windows_scratch_table(&fold_table_scratch), &settings_board);
        settings_rebuild();
}

// One frame at a time: the kit's clock at the frame, the fold's step, and the page's rows as the loop would lay them out.
static void fold_frame(b64 at)
{
        ui_kit.now = at;
        ui_kit.busy = 0;
        settings_fold_step();
}

static void fold_run(b64 from, b64 until)
{
        for (b64 at = from; at <= until; at += fold_frame_ns)
                fold_frame(at);
}

// The page as the window draws it at this moment, written as a frame named name (the settings' size at the scale).
static void fold_shot(string_address name)
{
        settings_draw_rows(frames_pixels, 0, 0);
        frames_write(name, 0, 0, 0, settings.width, settings.height);
        frames_count++;
}

static void fold_checks(void)
{
        const b64 base = 1000000000ll, ms = 1000000ll;
        positive unit;
        int closed, lines, open, mid_content, tall_lines = 0;
        b64 mid;
        struct ui_row row;

        fold_setup(100, 0, 0);
        check("settings fold: the GPU row is on the page", fold_row >= 0);
        fold_issues(1);
        check("settings fold: an option with a warning starts shut: a chevron, and no lines listed", settings_expander(fold_row) == UI_ROW_EXPANDER_SHUT && settings_sub_count == 0);
        closed = settings.list.content;
        unit = (positive)ui_dev(&settings.ui, UI_ROW);

        ui_kit.motion = 0;
        ui_kit.now = base;
        settings_toggle(fold_row);
        fold_frame(base);
        lines = settings_sub_count;
        check("settings fold: opening lists its lines, and the first frame has them at no height", lines > 0 && settings_fold_at[fold_row] == 0 && settings.list.content == closed);
        fold_run(base + fold_frame_ns, base + 100 * ms);
        mid = settings_fold_at[fold_row];
        mid_content = settings.list.content;
        check("settings fold: 100 ms in, the lines are part way open on the kit's spring", mid > 0 && mid < UI_ONE);
        ui_list_describe(&settings.list, fold_row, &row);
        check("settings fold: the chevron turns with the spring, part way between shut and open", row.turn == mid && row.expander == UI_ROW_EXPANDER_OPEN);
        fold_run(base + 100 * ms + fold_frame_ns, base + 2000 * ms);
        open = settings.list.content;
        // The lines' height: each as tall as its words, which wrap (the list's words do): more than a row each where they do not fit one.
        for (int at = 0; at < settings.list.count; at++)
                tall_lines += settings.list.id[at] >= (int)SETTINGS_ALL ? settings.list.whole[at] : 0;
        string_format(log, "  settings fold: %b lines open, %b tall (a row each would be %b)\n", (bipolar)lines, (bipolar)tall_lines, (bipolar)(lines * (int)unit));
        check("settings fold: at rest the lines are open at their height, and the rows under them moved down by it",
              settings_fold_at[fold_row] == UI_ONE && tall_lines > lines * (int)unit && open == closed + tall_lines);
        check("settings fold: part way the rows under the lines were between shut and open (they move each frame)", mid_content > closed && mid_content < open);
        check("settings fold: at rest the board has no fold running", !settings_fold_live[fold_row] && !(ui_kit.busy));

        // Shutting: the lines fold away, and are no longer listed at rest.
        ui_kit.now = base + 3000 * ms;
        settings_toggle(fold_row);
        fold_run(base + 3000 * ms + fold_frame_ns, base + 3100 * ms);
        check("settings fold: shutting goes part way on the spring, the lines still listed", settings_fold_at[fold_row] > 0 && settings_fold_at[fold_row] < UI_ONE && settings_sub_count > 0);
        fold_run(base + 3100 * ms + fold_frame_ns, base + 5000 * ms);
        check("settings fold: at rest shut, with no lines listed and the list as it was shut", settings_fold_at[fold_row] == 0 && settings_sub_count == 0 && settings.list.content == closed);

        // Reduced motion: at once, no spring.
        ui_kit.motion = 1;
        settings_toggle(fold_row);
        check("settings fold: with reduced motion the lines are open at once, and no spring runs", settings_fold_at[fold_row] == UI_ONE && !settings_fold_live[fold_row] && settings.list.content == closed + tall_lines);
        settings_toggle(fold_row);
        check("settings fold: with reduced motion they shut at once", settings_fold_at[fold_row] == 0 && settings_sub_count == 0 && settings.list.content == closed);
        ui_kit.motion = 0;

        // An option with an error, and one with none: the empty state is unchanged.
        fold_issues(2);
        check("settings fold: an error shows the expander shut, the same as a warning", settings_expander(fold_row) == UI_ROW_EXPANDER_SHUT && settings_sub_count == 0);
        fold_issues(0);
        check("settings fold: with no issues the GPU row keeps its chevron, shut, and lists no lines", settings_expander(fold_row) == UI_ROW_EXPANDER_SHUT && settings_sub_count == 0);
}

/*
        The settings rows with issues, drawn at each state: the frames are written next to the rest of the kit's frames (fold-*.ppm). The shot of
        a fold is its frame at its time: an opening at 100 ms (part way), and at rest after 2 s. Light and dark, four scales, and the right to left
        language (Arabic) with the same rows.
*/
static void fold_frames(void)
{
        struct fold_shot_spec {
                string_address name;
                int scale, issues, turn; // turn: 0 none, 1 open at rest, 2 open part way (100 ms)
                b32 dark;
                unsigned int language_code;
        } shots[] = {
                {"fold-warning-shut", 100, 1, 0, false, 0},
                {"fold-error-shut", 100, 2, 0, false, 0},
                {"fold-three-open", 100, 3, 1, false, 0},
                {"fold-three-mid", 100, 3, 2, false, 0},
                {"fold-empty", 100, 0, 0, false, 0},
                {"fold-three-dark", 100, 3, 1, true, 0},
                {"fold-three-rtl", 100, 3, 1, false, ('a' | 'r' << 8)},
                {"fold-three-75", 75, 3, 1, false, 0},
                {"fold-three-125", 125, 3, 1, false, 0},
                {"fold-three-150", 150, 3, 1, false, 0},
                {"fold-three-dark-150", 150, 3, 1, true, 0},
        };

        for (positive at = 0; at < sizeof(shots) / sizeof(shots[0]); at++)
        {
                b64 base = 1000000000ll;

                fold_setup(shots[at].scale, shots[at].dark, shots[at].language_code);
                fold_issues(shots[at].issues);
                ui_kit.motion = 0;
                if (shots[at].turn == 1 || shots[at].turn == 2)
                {
                        ui_kit.now = base;
                        settings_toggle(fold_row);
                        fold_frame(base);
                        fold_run(base + fold_frame_ns, base + (shots[at].turn == 2 ? 100 : 2000) * 1000000ll);
                        fold_scroll();
                }
                fold_shot(shots[at].name);
        }
}

/*
        The Display group's map of the screens (the kit's graph), drawn as the window draws it (display-map-*.ppm): a desk of three
        screens, one of them turned on its side and the first the primary, light and dark, at 150 percent and in a window read from
        the right (the screens stay where they are on the desk), and a screen in hand part way through a drag: the ghost where the
        hand has it, the screen where it would land, against its neighbour and lined up with it.
*/
static void display_frames(void)
{
        static const struct
        {
                string_address name;
                int scale;
                b32 dark, drag;
                unsigned int language_code;
        } shots[] = {
                {"display-map-light", 100, false, false, 0},
                {"display-map-dark", 100, true, false, 0},
                {"display-map-150", 150, false, false, 0},
                {"display-map-rtl", 100, false, false, ('a' | 'r' << 8)},
                {"display-map-drag", 100, false, true, 0},
                {"display-map-drag-dark", 100, true, true, 0},
        };

        for (positive at = 0; at < sizeof(shots) / sizeof(shots[0]); at++)
        {
                struct windows address_to table;
                struct window_event event;
                unsigned int number[WINDOWS_OUTPUTS] = {0};
                int screen_row, x, y, x1, y1;

                fold_setup(shots[at].scale, shots[at].dark, shots[at].language_code);
                table = windows_scratch_table(&fold_table_scratch);
                table->outputs = 3, table->scale = 100;
                table->output_x[0] = 1080, table->output_y[0] = 240, table->output_w[0] = 2560, table->output_h[0] = 1440;
                table->output_x[1] = 3640, table->output_y[1] = 600, table->output_w[1] = 1920, table->output_h[1] = 1080;
                table->output_x[2] = 0, table->output_y[2] = 0, table->output_w[2] = 1080, table->output_h[2] = 1920, table->output_turn[2] = 90;
                memory_copy(table->output_name[0], "DP-1", 5), memory_copy(table->output_model[0], "Dell U2720Q", 12);
                memory_copy(table->output_name[1], "HDMI-A-1", 9), memory_copy(table->output_name[2], "DP-2", 5);
                screen_row = settings_output_row(SETTINGS_OUT_SCREEN);
                settings_group_choose(settings_group_of(screen_row));
                settings.map.display = 0, settings.map.keys = false, settings.map.sent = false, settings.map.shown = 0;
                ui_graph_start(&settings.map.graph);
                settings.list.focus = screen_row;
                settings_rebuild();
                settings_draw_rows(frames_pixels, 0, 0);
                if (shots[at].drag)
                {
                        // Screen 1 (the right one) taken by its middle and dragged up and left, over the primary.
                        settings_map_load(number);
                        settings_map_frame();
                        ui_graph_box(&settings.map.graph, 1, settings.map.node[1].x, settings.map.node[1].y, &x, &y, &x1, &y1);
                        event = (struct window_event){WINDOW_EVENT_MOTION, 0, (x + x1) / 2, (y + y1) / 2};
                        settings_event_at(&event);
                        event = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                        settings_event_at(&event);
                        event = (struct window_event){WINDOW_EVENT_MOTION, 0, (x + x1) / 2 - (x1 - x) / 3, (y + y1) / 2 - (y1 - y) / 2 - 3};
                        settings_event_at(&event);
                }
                fold_shot(shots[at].name);
                ui_graph_let_go(&settings.map.graph);
        }
        language_take(0);
}

b32 main()
{
        compositor_rows_make(compositor_rows_in_use); // the compositor's rows, made before any check reads one
        positive size = 0, bytes;
        long engine;

        // The transitions run as designed in every check (ui_paced at 100): the durations the rows pin are the designed ones.
        ui_pace = UI_PACE_DESIGNED;

        if (program_argument_count() < 5)
        {
                string_format(log, "  kit-frames REGULAR BOLD MONO OUTDIR [SCENARIO]\n");
                return test_report(null);
        }
        frames_dir = program_argument(4);
        engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        settings.ui.engine = (font_engine address_to)engine;
        font_start(settings.ui.engine, frames_atlas, 1024, 1024);
        for (int k = 0; k < 3; k++)
        {
                bytes = compositor_map_file(program_argument((positive)k + 1), &size);
                check("kit frames: the faces load", bytes && font_load(&frames_faces[k], (const p8 address_to)bytes, size, 0) == 0);
        }
        if (failures)
                return test_report(null);
        settings.ui.regular = &frames_faces[0];
        settings.ui.bold = &frames_faces[1];
        settings.ui.mono = &frames_faces[2];
        settings.ui.scratch = frames_scratch;
        settings.ui.scratch_width = 1400;
        settings.ui.scale = 100;
        settings.ui.pixels = frames_pixels;
        settings.ui.pitch = GALLERY_W;
        settings.ui.width = GALLERY_W;
        settings.ui.height = settings.height = GALLERY_H;
        settings.width = GALLERY_W;
        gallery.on = 1;
        ui_kit_theme(UI_THEME_LIGHT, UI_ACCENT_DAWN, 0);

        /*
                The cost of a frame, for perf stat: kit-frames ... OUTDIR bench press|drag|still N draws N frames of
                what the window draws while a button is pressed (its rows cleared to the paper and the button drawn,
                pressed again every 30 frames so the ripple, the state layer and the fill always move), while the
                slider is dragged back and forth past its end, or of a switch at rest; the cost of a frame is the
                difference of two counts over the difference of their N.
        */
        if (program_argument_count() > 7 && string_equals(program_argument(5), (string_address) "bench"))
        {
                string_address what = program_argument(6);
                positive count = (positive)string_to_number_unsigned(program_argument(7), null, 10);

                if (string_equals(what, (string_address) "scroll-step"))
                {
                        // The body's frame alone: a region turned a notch, then its frame at a time to rest, count times over.
                        struct ui_scroll glide = {100, 10, 200, 1000, 200, 0, false, false, 0};
                        struct ui scroll_ui;
                        positive frames = 0;

                        memory_zero(&scroll_ui, sizeof(scroll_ui));
                        scroll_ui.scale = 100;
                        ui_body_set(&glide.body, 200ll << UI_Q, 1000ll << UI_Q, 0, 0);
                        ui_kit.now = 1000000000ll;
                        for (positive turn = 0; turn < count; turn++)
                        {
                                // a notch of the body (40 px, the kit's notch) and its frames at rest: the body's own glide, no kit wrapper
                                ui_body_notch(&glide.body, (b64)(turn % 2 ? 40 : -40) << UI_Q, ui_kit.now);
                                ui_kit.now += 16666667;
                                while (ui_body_step(&glide.body, ui_kit.now))
                                        ui_kit.now += 16666667, frames++;
                        }
                        string_format(log, "  kit bench scroll-step: %p turns, %p frames\n", count, frames);
                        return 0;
                }
                struct ui_control address_to c = null;
                b64 now = 3000000000ll;
                int band_top, band_bottom, x, y;

                /*
                        The settings window's cost: settings whole draws its Canvas group whole N times (dark, scale 1,
                        700 by 520), settings-keys moves the keys a row down and back N times, each move drawn as the
                        window draws it (the rows that changed, into the buffer the compositor is not reading), and
                        settings-hover moves the pointer over the rows, a row a frame, drawn the same way.
                */
                if (string_equals(what, (string_address) "settings") || string_equals(what, (string_address) "settings-keys") ||
                    string_equals(what, (string_address) "settings-hover") || string_equals(what, (string_address) "settings-toggle") ||
                    string_equals(what, (string_address) "settings-column") || string_equals(what, (string_address) "settings-wheel"))
                {
                        static struct windows *table_scratch;
                        struct windows *const table_at = windows_scratch_table(&table_scratch);
                        int top, bottom, wired = 0;

                        if (!settings_map())
                                return 1;
                        gallery.on = 0;
                        ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.pane = 1, settings.page.buffers.shown = -1, settings.focused = 1;
                        for (positive at = 0; at < SETTINGS_ROWS; at++)
                        {
                                const struct settings_row address_to row = &settings_rows[at];

                                settings.state[at] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[at],
                                                    (string_address)(row->kind == SETTINGS_SWITCH ? (at % 2 ? "on" : "off")
                                                                     : row->path && string_equals((string_address)row->path, (string_address)SETTING_THEME_PATH) ? "dark"
                                                                     : row->path && string_equals((string_address)row->path, (string_address)SETTING_ACCENT_PATH) ? "amber"
                                                                     : row->path && string_equals((string_address)row->path, (string_address)SETTING_MOTION_PATH) ? "full"
                                                                     : row->standard ? row->standard : "1"),
                                                    SETTINGS_VALUE);
                        }
                        table_at->stride = 700;
                        settings.own.table = table_at;
                        settings.width = 700, settings.height = 520;
                        settings.ui.pitch = 700, settings.ui.width = 700, settings.ui.height = 520;
                        settings.ui.opaque = 1;
                        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 0);
                        ui_kit_inks(&settings.ui);
                        settings_refilter();
                        settings_group_choose(1);
                        /*
                                settings-toggle throws the Wired switch every 15 frames, its verb's reading taken as the
                                window takes it, and settings-column moves the column a group down and back every 30
                                frames: each frame drawn as the loop draws it, the rows that changed and those the kit
                                says still move (the cost of an animation, frame by frame).
                        */
                        for (positive at = 0; at < SETTINGS_ROWS; at++)
                                wired = string_equals((string_address)settings_rows[at].label, (string_address) "Wired") ? (int)at : wired;
                        if (string_equals(what, (string_address) "settings-toggle"))
                        {
                                settings.page.pane = 1;
                                settings_group_choose(settings_group_of(wired));
                                settings.page.pane = 1;
                                settings.list.focus = wired;
                        }
                        if (string_equals(what, (string_address) "settings-column"))
                                settings.page.pane = 0;
                        ui_kit.now = now;
                        settings_draw_rows(frames_pixels, 0, 0);
                        for (positive frame = 0; frame < count; frame++)
                        {
                                now += 16666667;
                                ui_kit.now = now;
                                ui_kit.busy = 0;
                                if (string_equals(what, (string_address) "settings"))
                                {
                                        settings_draw_rows(frames_pixels, 0, 0);
                                        continue;
                                }
                                if (string_equals(what, (string_address) "settings-toggle") || string_equals(what, (string_address) "settings-column"))
                                {
                                        if (wired && frame % 15 == 0 && what[9] == 't')
                                        {
                                                b32 on = string_equals((string_address)settings.value[wired], (string_address) "on");

                                                string_copy_bounded((string_address)settings.value[wired], (string_address)(on ? "off" : "on"), SETTINGS_VALUE);
                                                settings_reading_changed(wired);
                                        }
                                        if (frame % 30 == 0 && what[9] == 'c')
                                                settings_press(frame % 60 ? UI_KEY_UP : UI_KEY_DOWN);
                                        ui_kit.damage_top = ui_kit.damage_bottom = 0;
                                        ui_kit_turn();
                                        ui_buffers_paint(&settings.page.buffers, (int)(frame % 2), 0, 0, settings.height, frames_pixels, settings_draw_rows, &top, &bottom);
                                        if (ui_kit.busy)
                                                ui_app_moved(&settings_app, ui_kit.damage_top, ui_kit.damage_bottom, !ui_tween_still(&ui_kit.turn));
                                        continue;
                                }
                                if (string_equals(what, (string_address) "settings-wheel"))
                                {
                                        // A notch of the wheel over the rows every 20 frames, turned down and back: the frames it moves are drawn.
                                        if (frame == 1)
                                        {
                                                struct window_event over = {WINDOW_EVENT_MOTION, 0, 450, 200};

                                                settings_event_at(&over);
                                        }
                                        if (frame % 20 == 0)
                                        {
                                                struct window_event turn = {WINDOW_EVENT_WHEEL, 0, frame % 40 ? -120 : 120, 0};

                                                settings_event_at(&turn);
                                        }
                                        ui_kit.damage_top = ui_kit.damage_bottom = 0;
                                        ui_kit_turn();
                                        ui_buffers_paint(&settings.page.buffers, (int)(frame % 2), 0, 0, settings.height, frames_pixels, settings_draw_rows, &top, &bottom);
                                        if (ui_kit.busy)
                                                ui_app_moved(&settings_app, ui_kit.damage_top, ui_kit.damage_bottom, !ui_tween_still(&ui_kit.turn));
                                        continue;
                                }
                                if (string_equals(what, (string_address) "settings-keys"))
                                        settings_press(frame % 2 ? UI_KEY_UP : UI_KEY_DOWN);
                                else
                                {
                                        struct window_event over = {WINDOW_EVENT_MOTION, 0, 450, 100 + (int)(frame % 9) * 40};

                                        settings_event_at(&over);
                                }
                                ui_buffers_paint(&settings.page.buffers, (int)(frame % 2), 0, 0, settings.height, frames_pixels, settings_draw_rows, &top, &bottom);
                        }
                        string_format(log, "  kit bench %s: %p frames, busy at the end %p\n", what, count, (positive)ui_kit.busy);
                        return 0;
                }

                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
                gallery_layout(&settings.ui, settings.width, settings.height);
                for (int k = 0; k < gallery.count; k++)
                {
                        struct ui_control address_to each = &gallery.control[k];

                        if ((string_equals(what, (string_address) "press") && each->kind == UI_BUTTON && each->label && string_equals((string_address)each->label, (string_address) "Deploy")) ||
                            (string_equals(what, (string_address) "drag") && each->kind == UI_SLIDER && each->label && string_equals((string_address)each->label, (string_address) "Log verbosity")) ||
                            (string_equals(what, (string_address) "still") && each->kind == UI_SWITCH && each->label && string_equals((string_address)each->label, (string_address) "Telemetry")))
                                c = each;
                }
                if (!c)
                        return 1;
                band_top = c->y - 40, band_bottom = c->y + c->h + 40;
                x = c->x + c->w / 2, y = c->y + c->h / 2;
                ui_kit.now = now;
                frames_at(now);
                for (positive frame = 0; frame < count; frame++)
                {
                        now += 16666667;
                        ui_kit.now = now;
                        if (string_equals(what, (string_address) "press") && frame % 30 == 0)
                        {
                                ui_kit_pointer(&settings.ui, gallery.control, gallery.count, WINDOW_EVENT_BUTTON, x, y, false, 0);
                                ui_kit_pointer(&settings.ui, gallery.control, gallery.count, WINDOW_EVENT_BUTTON, x - 10 + (int)(frame % 20), y, true, 0);
                        }
                        if (string_equals(what, (string_address) "drag"))
                        {
                                if (frame == 0)
                                        ui_kit_pointer(&settings.ui, gallery.control, gallery.count, WINDOW_EVENT_BUTTON, x, y + 30, true, 0);
                                ui_kit_pointer(&settings.ui, gallery.control, gallery.count, WINDOW_EVENT_MOTION,
                                               c->x + (int)(frame * 7 % (unsigned int)(c->w + 120)), y + 30 + (int)(frame % 9), true, 0);
                        }
                        // What the window does for a frame: the rows the moving control reaches cleared, the control drawn.
                        ui_kit.busy = 0;
                        settings.ui.band_top = band_top, settings.ui.band_bottom = band_bottom;
                        ui_fill(&settings.ui, 0, band_top, GALLERY_W, band_bottom - band_top, ui_role(UI_SURFACE) | 0xff000000u);
                        ui_control_draw(&settings.ui, c);
                }
                string_format(log, "  kit bench %s: %p frames, busy at the end %p\n", what, count, (positive)ui_kit.busy);
                return 0;
        }

        // The shots each recording took, after its release (or change), in hundredths of a millisecond.
#define SHOT(ms) {(int)((ms) * 100), FRAMES_SHOT, 0, 0}
        {
                // The switch: Offline cache, pressed near its pill and let go (record.mjs "switch").
                static const struct frames_step steps[] = {
                    {200000, FRAMES_MOVE, 661, 177}, {221000, FRAMES_DOWN, 661, 177}, {241000, FRAMES_UP, 661, 177},
                    SHOT(2410 + 16.67), SHOT(2410 + 33.33), SHOT(2410 + 50), SHOT(2410 + 66.67), SHOT(2410 + 100), SHOT(2410 + 133.33),
                    SHOT(2410 + 166.67), SHOT(2410 + 233.33), SHOT(2410 + 300), SHOT(2410 + 400)};

                frames_run((string_address) "switch", steps, sizeof(steps) / sizeof(steps[0]), 381, 145, 330, 64);
        }
        {
                // The radios: RISC-V chosen from x86-64 (record.mjs "radio").
                static const struct frames_step steps[] = {
                    {200000, FRAMES_MOVE, 305, 871}, {221000, FRAMES_DOWN, 305, 871}, {231000, FRAMES_UP, 305, 871},
                    SHOT(2310 + 16.67), SHOT(2310 + 50), SHOT(2310 + 83.33), SHOT(2310 + 116.67), SHOT(2310 + 150), SHOT(2310 + 200),
                    SHOT(2310 + 250), SHOT(2310 + 300), SHOT(2310 + 400), SHOT(2310 + 500)};

                frames_run((string_address) "radio", steps, sizeof(steps) / sizeof(steps[0]), 25, 707, 330, 210);
        }
        {
                // The segmented tabs: Stopped chosen from All (record.mjs "tabs"); its middle is where the kit lays it.
                struct ui_control address_to tabs = null;
                int x, y;

                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
                gallery_layout(&settings.ui, settings.width, settings.height);
                for (int k = 0; k < gallery.count; k++)
                        if (gallery.control[k].kind == UI_TABS)
                                tabs = &gallery.control[k];
                x = tabs ? ui_tab_left(&settings.ui, tabs, 2) + ui_tab_width(&settings.ui, tabs, 2) / 2 : 0;
                y = tabs ? tabs->y + 16 : 0;
                {
                        struct frames_step steps[] = {
                            {200000, FRAMES_MOVE, x, y}, {221000, FRAMES_DOWN, x, y}, {224000, FRAMES_UP, x, y},
                            SHOT(2240 + 16.67), SHOT(2240 + 33.33), SHOT(2240 + 50), SHOT(2240 + 66.67), SHOT(2240 + 100), SHOT(2240 + 150),
                            SHOT(2240 + 200), SHOT(2240 + 250), SHOT(2240 + 350), SHOT(2240 + 450)};

                        frames_run((string_address) "tabs", steps, sizeof(steps) / sizeof(steps[0]), 753, 81, 239, 48);
                }
        }
        {
                // The slider: Log verbosity pressed a fifth along and let go (record.mjs "slider").
                static const struct frames_step steps[] = {
                    {200000, FRAMES_MOVE, 461, 322}, {221000, FRAMES_DOWN, 461, 322}, {231000, FRAMES_UP, 461, 322},
                    SHOT(2310 + 16.67), SHOT(2310 + 33.33), SHOT(2310 + 50), SHOT(2310 + 66.67), SHOT(2310 + 100), SHOT(2310 + 150),
                    SHOT(2310 + 200), SHOT(2310 + 250), SHOT(2310 + 300), SHOT(2310 + 400)};

                frames_run((string_address) "slider", steps, sizeof(steps) / sizeof(steps[0]), 381, 258, 330, 104);
        }
        {
                // The field: Node name hovered, then pressed into (record.mjs "input").
                static const struct frames_step steps[] = {
                    {200000, FRAMES_MOVE, 78, 171}, {240000, FRAMES_DOWN, 78, 171}, {240000, FRAMES_UP, 78, 171},
                    SHOT(2400 + 16.67), SHOT(2400 + 33.33), SHOT(2400 + 50), SHOT(2400 + 66.67), SHOT(2400 + 100), SHOT(2400 + 133.33),
                    SHOT(2400 + 166.67), SHOT(2400 + 200), SHOT(2400 + 250), SHOT(2400 + 300)};

                frames_run((string_address) "input", steps, sizeof(steps) / sizeof(steps[0]), 24, 107, 332, 108);
        }
        {
                // The ripple: Logs pressed at 10, 8 and let go 120 ms on (record.mjs "ripple").
                static const struct frames_step steps[] = {
                    {200000, FRAMES_MOVE, 576, 727}, {201000, FRAMES_DOWN, 576, 727}, {213000, FRAMES_UP, 576, 727},
                    SHOT(2130 + 16.67), SHOT(2130 + 50), SHOT(2130 + 83.33), SHOT(2130 + 116.67), SHOT(2130 + 150), SHOT(2130 + 200),
                    SHOT(2130 + 250), SHOT(2130 + 300)};

                frames_run((string_address) "ripple", steps, sizeof(steps) / sizeof(steps[0]), 554, 707, 87, 60);
        }
        {
                // A press and a release of Deploy, still (record.mjs "press"): the state layer and the fill.
                static const struct frames_step steps[] = {
                    {200000, FRAMES_MOVE, 442, 737}, {230000, FRAMES_DOWN, 442, 737},
                    SHOT(2300 + 16.67), SHOT(2300 + 50), SHOT(2300 + 100), SHOT(2300 + 200)};
                static const struct frames_step up[] = {
                    {200000, FRAMES_MOVE, 442, 737}, {230000, FRAMES_DOWN, 442, 737}, {260000, FRAMES_UP, 442, 737},
                    SHOT(2600 + 16.67), SHOT(2600 + 50), SHOT(2600 + 100), SHOT(2600 + 200), SHOT(2600 + 300), SHOT(2600 + 400)};

                frames_run((string_address) "press-down", steps, sizeof(steps) / sizeof(steps[0]), 393, 707, 99, 60);
                frames_run((string_address) "press-up", up, sizeof(up) / sizeof(up[0]), 393, 707, 99, 60);
        }
        // The whole page at rest, for the gallery side by side.
        gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
        frames_at(5000000000ll);
        frames_at(5016666667ll);
        frames_write((string_address) "gallery", 0, 0, 0, GALLERY_W, GALLERY_H);
        // And on the standard theme, dark with white fills.
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
        frames_at(6000000000ll);
        frames_at(6016666667ll);
        frames_write((string_address) "gallery-standard", 0, 0, 0, GALLERY_W, GALLERY_H);

        // The settings window's Canvas group, light and dark, over the glass a light or a dark theme gives it: the kit's
        // switches, the theme's and the motion's segmented tabs and the accent's swatches in the rows.
        if (settings_map())
                for (int dark = 0; dark < 2; dark++)
                {
                        unsigned int glass = dark ? 0x2b2b2eu : 0xb4b4b4u;

                        gallery.on = 0;
                        ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.pane = 1, settings.page.buffers.shown = -1;
                        for (positive at = 0; at < SETTINGS_ROWS; at++)
                        {
                                const struct settings_row address_to row = &settings_rows[at];

                                settings.state[at] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[at],
                                                    (string_address)(row->kind == SETTINGS_SWITCH ? (at % 2 ? "on" : "off")
                                                                     : row->path && string_equals((string_address)row->path, (string_address)SETTING_THEME_PATH) ? (dark ? "dark" : "light")
                                                                     : row->path && string_equals((string_address)row->path, (string_address)SETTING_ACCENT_PATH) ? "amber"
                                                                     : row->path && string_equals((string_address)row->path, (string_address)SETTING_MOTION_PATH) ? "full"
                                                                     : row->standard ? row->standard : "1"),
                                                    SETTINGS_VALUE);
                        }
                        static struct windows *table_scratch;
                        struct windows *const table_at = windows_scratch_table(&table_scratch);

                        table_at->stride = 700;
                        settings.own.table = table_at;
                        settings.width = 700, settings.height = 520;
                        settings.ui.pitch = 700, settings.ui.width = 700, settings.ui.height = 520;
                        settings_refilter();
                        settings_layout();
                        settings_group_choose(1); // Canvas
                        (void)glass;
                        settings.ui.opaque = 1;
                        ui_kit_theme(dark ? UI_THEME_DARK : UI_THEME_LIGHT, UI_ACCENT_STANDARD, 1);
                        ui_kit_inks(&settings.ui);
                        ui_kit.now = 6000000000ll;
                        settings_draw_rows(frames_pixels, 0, 0);
                        // The picture is premultiplied over the glass: laid on the glass's colour to be seen.
                        for (positive left = 700 * 520; left > 0; left--) // backwards: the wider rows are laid over the narrow ones
                        {
                                positive at = left - 1, y = at / 700, x = at % 700;
                                unsigned int pixel = frames_pixels[y * 700 + x];

                                frames_pixels[y * GALLERY_W + x] = ui_over(pixel, 0xff000000u | glass);
                        }
                        frames_write((string_address)(dark ? "settings-dark" : "settings-light"), 0, 0, 0, 700, 520);
                }
        /*
                And the settings window's other states, dark: the keys in the rows of the Machine group with the window
                focused (the row ringed), its name typed in (the kit's field in the row), the menu a choice opens, and
                the Wi-Fi group with networks heard and saved under its switch.
        */
        if (settings_large)
        {
                static const char *const states[] = {"settings-focus", "settings-field", "settings-menu", "settings-wifi"};

                for (int state = 0; state < 4; state++)
                {
                        settings.focused = 1, settings.page.pane = 1, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1;
                        settings_group_choose(state == 3 ? settings_group_of((int)SETTINGS_LIST) : 0);
                        if (state == 1)
                                settings_hold(SETTINGS_NAME_ROW, "patient-cricket");
                        if (state == 2)
                        {
                                int keyboard = -1;

                                for (positive at = 0; at < SETTINGS_ROWS; at++)
                                        keyboard = string_equals((string_address)settings_rows[at].label, (string_address) "Keyboard") ? (int)at : keyboard;
                                string_copy_bounded((string_address)settings.value[keyboard], (string_address) "se", SETTINGS_VALUE);
                                settings.list.focus = keyboard;
                                settings_menu_open(keyboard);
                        }
                        if (state == 3)
                        {
                                static const char said[] = "[Moonwater] wifi on\n[Moonwater] * home-net      -48 dBm ###. WPA2-PSK 5GHz 36\n"
                                                           "[Moonwater]   cafe          -61 dBm ##.. open 2.4GHz 6\n"
                                                           "[Moonwater] + neighbour     -77 dBm #... WPA3-SAE 2.4GHz 1\n[Moonwater]   office\n";

                                memory_copy(settings_large->said, said, sizeof(said) - 1);
                                settings.said_length = sizeof(said) - 1;
                                settings.feed_list = 0;
                                settings_feed_read(false, false);
                        }
                        ui_kit.now += 1000000000ll;
                        settings_draw_rows(frames_pixels, 0, 0);
                        for (positive left = 700 * 520; left > 0; left--)
                        {
                                positive at = left - 1;

                                frames_pixels[(at / 700) * GALLERY_W + at % 700] = frames_pixels[at];
                        }
                        // A choice's menu is a popup of its own, which the frame has no window for: it is drawn over the rows where
                        // the popup is placed, glass as the popup is (no face, the window's ink).
                        if (settings.page.menu_row >= 0)
                        {
                                struct ui glass = settings.ui;

                                glass.opaque = false, glass.glass = true;
                                ui_inks(&glass, UI_GLASS_GROUND);
                                glass.pixels = frames_pixels + settings.page.menu_y * GALLERY_W + settings.page.menu_x;
                                glass.pitch = GALLERY_W;
                                glass.width = settings.popup.menu.width, glass.height = settings.popup.menu.height;
                                glass.top = 0, glass.bottom = glass.height;
                                ui_popmenu_draw(&glass, &settings.popup.menu);
                        }
                        frames_write((string_address)states[state], 0, 0, 0, 700, 520);
                        settings_menu_close();
                        if (state == 1)
                                settings_edit_end();
                        ui_field_close(&settings.edit);
                }
        }
        fold_checks();
        fold_frames();
        display_frames();
        // The launcher, dark, with ls typed: the kit's field, and the kit's list under it with the pointer over its third row.
        {
                static char names[6][LAUNCHER_NAME];
                static unsigned char from[6];
                static unsigned short hashed[LAUNCHER_HASH];
                static struct windows *table_scratch;
                struct windows *const table_at = windows_scratch_table(&table_scratch);
                static const char address_to const fixture[] = {"ls", "lsblk", "lsof", "less", "false", "also"};

                for (positive at = 0; at < 6; at++)
                        string_copy((string_address)names[at], (string_address)fixture[at]);
                memory_zero(&launcher, sizeof(launcher));
                launcher.names = (char address_to)names, launcher.count = 6, launcher.from = from, launcher.hashed = hashed;
                launcher.dirs[0] = "/usr/bin", launcher.dir_count = 1;
                table_at->output_width = 1920, table_at->output_height = 1080, table_at->stride = GALLERY_W;
                launcher.own.table = table_at;
                // The launcher is glass (WINDOW_GLASS): its pieces are drawn in the inks of the glass the dark theme gives it,
                // and its picture is laid over that glass to be seen, as the settings window's is.
                launcher.ui = settings.ui;
                launcher.ui.opaque = 0, launcher.ui.glass = 1;
                ui_inks(&launcher.ui, 0x2b2b2eu);
                launcher.width = LAUNCHER_WIDTH;
                launcher_type_text("ls");
                launcher_layout();
                launcher.list.hover = 2;
                ui_kit.now += 1000000000ll;
                launcher_draw(frames_pixels);
                for (positive left = (positive)launcher.width * (positive)launcher.height; left > 0; left--)
                {
                        positive at = left - 1, y = at / (positive)launcher.width, x = at % (positive)launcher.width;
                        unsigned int pixel = frames_pixels[y * GALLERY_W + x];

                        frames_pixels[y * GALLERY_W + x] = ui_over(pixel, 0xff000000u | 0x2b2b2eu);
                }
                frames_write((string_address) "launcher", 0, 0, 0, launcher.width, launcher.height);
                // A dropdown is glass too (ui_popmenu_look): its menu over the same glass, no face and no outline, its hot choice lit.
                {
                        static struct ui_popmenu menu;
                        struct ui menu_ui = launcher.ui;

                        memory_zero(&menu, sizeof(menu));
                        ui_popmenu_add(&menu, "Copy", "Ctrl+C", 0, 1);
                        ui_popmenu_add(&menu, "Paste", "Ctrl+V", 0, 2);
                        ui_popmenu_add(&menu, "", null, UI_MENU_SEPARATOR, 0);
                        ui_popmenu_add(&menu, "Select all", "Ctrl+A", 0, 3);
                        ui_popmenu_add(&menu, "Undo", null, UI_MENU_DISABLED, 4);
                        menu.hot = 1;
                        menu_ui.opaque = 0, menu_ui.glass = 1;
                        ui_inks(&menu_ui, 0x2b2b2eu);
                        ui_popmenu_size(&menu_ui, &menu, 0);
                        ui_begin(&menu_ui, frames_pixels, GALLERY_W, menu.width, menu.height);
                        ui_popmenu_draw(&menu_ui, &menu);
                        for (positive left = (positive)menu.width * (positive)menu.height; left > 0; left--)
                        {
                                positive at = left - 1, y = at / (positive)menu.width, x = at % (positive)menu.width;
                                unsigned int pixel = frames_pixels[y * GALLERY_W + x];

                                frames_pixels[y * GALLERY_W + x] = ui_over(pixel, 0xff000000u | 0x2b2b2eu);
                        }
                        frames_write((string_address) "menu", 0, 0, 0, menu.width, menu.height);
                }
        }
        string_format(log, "  kit frames: %p frames drawn\n", frames_count);
        return test_report(null);
}
#endif

#ifdef CHECK_terminal
/*
        The text widget (ui.c's ui_text_*) with the repository's faces: a field in Inter and a code view in IBM Plex Mono
        drawn into memory. A caret's x is the pen's (the same x font_draw ends a prefix at, within a pixel), a click at
        a cluster's x is that cluster, the code view's columns are whole cells, a field is one line (Enter submits, a
        pasted line end is a blank), the keys reach the engine through the keymap, the hand selects a word and a line,
        and every mode draws inside its box and nothing outside it.
*/
static fn menu_check(struct ui address_to wui);
static fn menu_defaults_check(struct ui address_to wui);
static fn kit_composites_check(struct ui address_to wui);
static fn editor_check(struct ui address_to wui);
static fn editor_close_check(struct ui address_to wui);
static fn editor_hostile_check(struct ui address_to wui);
static fn editor_tabs_check(struct ui address_to wui);
static fn editor_disk_check(struct ui address_to wui);
static fn editor_edge_check(struct ui address_to wui);
static fn editor_readonly_check(struct ui address_to wui);
static fn field_check(struct ui address_to wui);
static fn popmenu_bar_check(struct ui address_to wui);
static fn drop_check(struct ui address_to wui);
static fn band_check(struct ui address_to wui);

/*
        An error's underline in a code view is a wave one pixel tall at 100 percent, a row under the text. The line was the scale's
        height in pixels (100 at 100 percent), so the wave covered the lines under it.
*/
static fn squiggle_check(struct ui address_to wui)
{
        static struct ui_text view;
        long pixels = ui_map(400 * 300 * 4);
        unsigned int *shown = (unsigned int address_to)pixels, error = ui_role(UI_ERROR) & 0xffffffu;
        int rows = 0;

        ui_begin(wui, shown, 400, 400, 300);
        wui->scale = 100;
        memory_fill_u32(shown, 400 * 300, 0x111113);
        ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to) "alpha beta\ngamma\n", 17, ui_text_keymap());
        doc_range_add(&view.doc, 0, 5, DOC_RANGE_ERROR, 0);
        ui_text_draw(wui, &view, 10, 10, 300, 200, 0x111113);
        for (int y = view.top; y < view.top + 3 * view.line_height && y < 300; y++)
            rows += (shown[(positive)y * 400 + (positive)(view.left + 1)] & 0xffffffu) == error;
        check("squiggle: an error's underline is one pixel tall at 100 percent, not the scale's 100", rows >= 1 && rows <= 2);
        ui_text_close(&view);
        compositor_call(syscall(munmap), pixels, 400 * 300 * 4, 0, 0, 0, 0);
}

/*
        The kit's drag and drop helpers. A list of URIs gives this machine's files' paths, %XX made bytes again, and passes over a
        comment, another scheme, another machine's file and a path with a NUL in it; dropped on a command line the same list is each
        path quoted for a shell, a quote in it closed and opened again, never a line's end. A press arms a drag and its release disarms
        it, so a hand with no button down never drags what the last press was on; a release, a finger's press and another button's
        disarm as well. The drag's news carries the person's keys at a drop target, which is how a drop of files is a copy or a move.
*/
static fn drop_check(struct ui address_to wui)
{
        static const char list[] = "file:///a/b%20c\r\n# a comment\r\nhttp://example.org/x\r\nfile://localhost/tmp/z\nfile://elsewhere/etc/passwd\r\n"
                                   "file:///nul%00x\r\nfile:///last";
        static const char quoting[] = "file:///it's%0Ahere\r\nhttps://example.org/q\r\n";
        p8 path[64], address_to text;
        positive at = 0, length, room, got;
        b32 paths;
        struct ui_drag d;
        struct ui_drop drop;
        struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
        struct window_event release = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
        struct window_event finger = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
        struct window_event over = {WINDOW_EVENT_DRAG, WINDOW_KEY_CONTROL, WINDOWS_DRAG_MOTION, (7 << 16) | 9};

        length = sizeof(list) - 1;
        got = ui_uri_next((const p8 address_to)list, length, &at, path, sizeof(path));
        paths = got == 6 && !memory_compare(path, "/a/b c", 7);
        got = ui_uri_next((const p8 address_to)list, length, &at, path, sizeof(path));
        paths = paths && got == 6 && !memory_compare(path, "/tmp/z", 7);
        got = ui_uri_next((const p8 address_to)list, length, &at, path, sizeof(path));
        paths = paths && got == 5 && !memory_compare(path, "/last", 6);
        paths = paths && !ui_uri_next((const p8 address_to)list, length, &at, path, sizeof(path));
        check("drop: a list of URIs gives this machine's files, their %XX made bytes, and passes over comments, other schemes, other machines and a NUL", paths);

        got = ui_uris_quoted((const p8 address_to)quoting, sizeof(quoting) - 1, &text, &room);
        check("drop: files dropped on a command line are each quoted for the shell with a blank after, a quote closed and opened, no line end",
              got == 39 && !memory_compare(text, "'/it'\\''shere' 'https://example.org/q' ", 39));
        array_store_release(text, room, got);

        memory_zero(&d, sizeof(d));
        wui->scale = 100;
        ui_drag_button(&d, &press, 10, 10, true);
        paths = d.armed && ui_drag_past(wui, &d, 40, 10);
        ui_drag_button(&d, &release, 10, 10, false);
        paths = paths && !d.armed && !ui_drag_past(wui, &d, 40, 10);
        ui_drag_button(&d, &finger, 10, 10, true);
        paths = paths && !d.armed;
        ui_drag_button(&d, &press, 10, 10, false);
        check("drop: a press arms a drag and its release disarms it, so a hand with no button down drags nothing; a finger's press and a press on nothing to drag arm none",
              paths && !d.armed);

        memory_zero(&drop, sizeof(drop));
        check("drop: a drag over a target carries where it is and the person's Control (what makes a drop of files a copy)",
              ui_drop_event(&drop, &over) == UI_DROP_OVER && drop.x == 9 && drop.y == 7 && drop.keys == WINDOW_KEY_CONTROL);
}

/*
        A rubber band over a virtual list: from its press to the hand it holds the rows it crosses, either way up, kept to the list's rows;
        its press is held to the rows, so rows scrolled under it while it is held stay in it; and it holds none where it crosses none.
*/
static fn band_check(struct ui address_to wui)
{
        static struct ui_list l;
        int first = -1, last = -1, row;
        b32 down, up, scrolled, none;

        wui->scale = 100;
        row = ui_dev(wui, UI_ROW);
        memory_zero(&l, sizeof(l));
        l.virtual = true, l.total = 50, l.y = 100, l.h = 10 * row;
        ui_list_band_begin(&l, 10, 100 + row + 2);
        ui_list_band_follow(&l, 30, 100 + 3 * row + 2);
        down = ui_list_band_rows(wui, &l, &first, &last) && first == 1 && last == 3;
        ui_list_band_follow(&l, 30, 100 + 2);
        up = ui_list_band_rows(wui, &l, &first, &last) && first == 0 && last == 1;
        l.scroll.scroll = 5 * row;
        ui_list_band_follow(&l, 30, 100 + 2);
        scrolled = ui_list_band_rows(wui, &l, &first, &last) && first == 1 && last == 5;
        ui_list_band_end(&l);
        none = !ui_list_band_rows(wui, &l, &first, &last);
        l.total = 0;
        ui_list_band_begin(&l, 10, 110);
        none = none && !ui_list_band_rows(wui, &l, &first, &last);
        check("band: a rubber band holds the rows it crosses either way up, its press held to the rows as they scroll, and none when it is over or crosses none",
              down && up && scrolled && none);
}

/*
        A dropdown's bar: a thumb pressed and dragged down 100 pixels moves the menu's top with it. The bar's answer is the
        scroll it asks for, and that is what the menu takes; a menu that kept the old scroll drew the bar moving and the items not.
*/
static fn popmenu_bar_check(struct ui address_to wui)
{
        static struct ui_popmenu m;
        long pixels = ui_map(400 * 400 * 4);
        struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
        struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
        struct window_event motion = {WINDOW_EVENT_MOTION, 0, 0, 0};
        int top, length, x, y, shown = 0;

        ui_begin(wui, (unsigned int address_to)pixels, 400, 400, 400);
        memory_zero(&m, sizeof(m));
        for (int at = 0; at < 40; at++)
                ui_popmenu_add(&m, "Item", null, 0, at + 1);
        ui_popmenu_size(wui, &m, 300);
        shown = ui_scroll_thumb(wui, &m.scroll, &top, &length);
        check("popup menu: a long menu has a bar, with its thumb in the track", shown && length > 0 && length < m.scroll.view);
        x = m.scroll.x + 1, y = top + length / 2;
        ui_popmenu_event(wui, &m, &press, x, y);
        ui_popmenu_event(wui, &m, &motion, x, y + 100);
        ui_popmenu_event(wui, &m, &release, x, y + 100);
        check("popup menu: a thumb dragged 100 pixels down moves the menu's top with it", ui_popmenu_top(&m) > 0 && m.scroll.scroll > 0);
        {
                // The wheel over a long menu scrolls it, three items a notch, and does not close it; a trackpad's small turns add up to items; the
                // ends hold it; a menu that fits is still closed by it.
                struct window_event notch = {WINDOW_EVENT_WHEEL, 0, -120, 0}, back = {WINDOW_EVENT_WHEEL, 0, 120, 0};
                struct window_event small = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE, -12, 0};
                static struct ui_popmenu fits;
                int one, two, up, closed_fits;

                ui_popmenu_size(wui, &m, 300);
                ui_popmenu_to(&m, 0), m.wheel_rest = m.wheel_left = 0;
                one = ui_popmenu_event(wui, &m, &notch, 5, 5) == UI_MENU_NOTHING && ui_popmenu_top(&m) == 3;
                two = ui_popmenu_event(wui, &m, &notch, 5, 5) == UI_MENU_NOTHING && ui_popmenu_top(&m) == 6;
                up = ui_popmenu_event(wui, &m, &back, 5, 5) == UI_MENU_NOTHING && ui_popmenu_top(&m) == 3;
                for (int k = 0; k < 40; k++)
                        ui_popmenu_event(wui, &m, &notch, 5, 5);
                one = one && ui_popmenu_top(&m) == m.count - m.rows_shown;
                for (int k = 0; k < 40; k++)
                        ui_popmenu_event(wui, &m, &back, 5, 5);
                one = one && ui_popmenu_top(&m) == 0;
                for (int k = 0; k < 20; k++)
                        ui_popmenu_event(wui, &m, &small, 5, 5);
                two = two && ui_popmenu_top(&m) >= 1 && ui_popmenu_top(&m) <= 3;
                memory_zero(&fits, sizeof(fits));
                for (int at = 0; at < 3; at++)
                        ui_popmenu_add(&fits, "Item", null, 0, at + 1);
                ui_popmenu_size(wui, &fits, 300);
                closed_fits = ui_popmenu_event(wui, &fits, &notch, 5, 5) == UI_MENU_GONE;
                check("popup menu: the wheel over a long menu scrolls it three items a notch and holds at its ends, and does not close it", one && two && up);
                check("popup menu: a trackpad's small turns add up to the items they make, and a menu that fits is still closed by the wheel", two && closed_fits);
        }
        compositor_call(syscall(munmap), (positive)pixels, 400 * 400 * 4, 0, 0, 0, 0);
}

/*
        ui_over, the blend every piece of the kit lays on a picture, is one body of assembly per architecture; the C it
        replaced is the reference here, written out as it was. The body has to give its bytes for every pixel, every
        under and every colour, the lanes at their edges included: every alpha of the pixel and of the under, and a
        random colour in each, with the channels of a pixel that is not premultiplied too.
*/
static unsigned int ui_over_reference(unsigned int pixel, unsigned int under)
{
        unsigned int rest = 255 - (pixel >> 24), out = 0;

        if (!(pixel >> 24))
                return under;
        if (!rest)
                return pixel;
        if (!under)
                return pixel;
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                unsigned int x = (under >> shift & 255) * rest + 127;
                unsigned int channel = (pixel >> shift & 255) + ((x + 1 + (x >> 8)) >> 8);

                out |= (channel > 255 ? 255 : channel) << shift;
        }
        return out;
}

static fn ui_over_check(void)
{
        static unsigned int state = 0x2545F491u;
        b32 same = true;
        int drawn = 0;

        for (unsigned int a = 0; a < 256; a++)
                for (unsigned int b = 0; b < 256; b++)
                        for (int k = 0; k < 8; k++)
                        {
                                unsigned int p = (XORSHIFT32(state) & 0x00ffffffu) | a << 24;
                                unsigned int u = (XORSHIFT32(state) & 0x00ffffffu) | b << 24;

                                if (k & 1)
                                        p = XORSHIFT32(state);
                                if (k & 2)
                                        u = XORSHIFT32(state);
                                same &= ui_over(p, u) == ui_over_reference(p, u);
                                drawn++;
                        }
        for (unsigned int lane = 0; lane < 4; lane++)
                for (unsigned int v = 0; v < 256; v++)
                {
                        unsigned int p = 0xffffffffu, u = v << (lane * 8);

                        same &= ui_over(p, u) == ui_over_reference(p, u);
                        p = 0xc0000000u | (v << (lane * 8));
                        same &= ui_over(p, 0x80808080u) == ui_over_reference(p, 0x80808080u);
                        drawn++;
                }
        check("ui_over: the body gives the C's bytes for every alpha pair and for lanes at their edges", same && drawn > 0);

        /*
                ui_kit_put, the premultiply and the over as one body on x86-64 and arm64, against the C it replaced: every
                alpha at every cover from nothing past the whole, with colours that are not premultiplied too.
        */
        same = true;
        for (unsigned int a = 0; a < 256; a++)
                for (unsigned int cover = 0; cover <= 4200; cover += 7)
                {
                        unsigned int argb = (XORSHIFT32(state) & 0x00ffffffu) | a << 24, under = XORSHIFT32(state);
                        unsigned int row[2] = {under, under}, want = under;

                        ui_kit_put(row, 1, cover, argb);
                        if (!cover)
                                want = under;
                        else if (cover >= 4096 && argb >> 24 == 255)
                                want = argb | 0xff000000u;
                        else
                                want = ui_over_reference(ui_premultiplied(argb, cover > 4096 ? 4096 : cover), under);
                        same &= row[1] == want && row[0] == under;
                }
        check("ui_kit_put: the body gives the C's pixel for every alpha and cover, and touches its own pixel alone", same);
}

/*
        The text menu's keys are the keymap's: the first key each command is bound to, written as the menu shows it, and
        none for a command with no key. A key rebound shows its new one, and a command whose keys are taken away shows none.
*/
static b32 text_menu_keys_are(const char address_to keys, const char address_to want)
{
        return keys && string_equals((string_address)keys, (string_address)want);
}

static const char address_to text_menu_key(const struct ui_popmenu address_to m, int id)
{
        for (int at = 0; at < m->count; at++)
                if (m->items[at].id == id && !(m->items[at].flags & UI_MENU_SEPARATOR))
                        return m->items[at].keys;
        return null;
}

static fn text_menu_keys_check(void)
{
        static struct doc_keymap map;
        static struct ui_text t;
        static struct ui_popmenu m;

        doc_keymap_defaults(&map);
        ui_text_open(&t, UI_TEXT_FIELD, (const p8 address_to) "hello", 5, &map);
        m.count = 0;
        ui_text_menu_items(&m, &t, false);
        check("text menu keys: the defaults are the keymap's first keys (Ctrl+Z, Ctrl+Y, Ctrl+X, Ctrl+C, Ctrl+V, Delete, Ctrl+A)",
              text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_UNDO), "Ctrl+Z") &&
                  text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_REDO), "Ctrl+Y") &&
                  text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_CUT), "Ctrl+X") &&
                  text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_COPY), "Ctrl+C") &&
                  text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_PASTE), "Ctrl+V") &&
                  text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_DELETE), "Delete") &&
                  text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_ALL), "Ctrl+A"));

        doc_keymap_line(&map, "ctrl+x none", 11);
        doc_keymap_line(&map, "shift+delete none", 17);
        m.count = 0;
        ui_text_menu_items(&m, &t, false);
        check("text menu keys: a command with no key left shows none (Cut, with its two keys taken away)",
              text_menu_key(&m, UI_TEXT_MENU_CUT) == null);

        doc_keymap_line(&map, "ctrl+k cut", 10);
        m.count = 0;
        ui_text_menu_items(&m, &t, false);
        check("text menu keys: the key the keymap binds shows, rebound or not (Cut is Ctrl+K)",
              text_menu_keys_are(text_menu_key(&m, UI_TEXT_MENU_CUT), "Ctrl+K"));
}

static fn text_widget_check(string_address inter, string_address mono)
{
        static struct ui wui;
        static font_face regular, code;
        struct ui_text field, view;
        positive length = 0, bytes;
        long pixels;
        static struct ui_text_layout l;

        wui = terminal.ui;
        bytes = compositor_map_file(inter, &length);
        check("text widget: Inter loads", bytes && font_load(&regular, (const p8 address_to)bytes, length, 0) == 0);
        bytes = compositor_map_file(mono, &length);
        check("text widget: Plex Mono loads", bytes && font_load(&code, (const p8 address_to)bytes, length, 0) == 0);
        if (failures)
                return;
        wui.regular = &regular;
        wui.bold = &regular;
        wui.mono = &code;
        wui.scale = 100;
        // A scratch line of its own: the terminal's is the terminal's.
        wui.scratch = null;
        wui.scratch_width = 0;
        pixels = ui_map(800 * 600 * 4);
        ui_scratch_fit(&wui, 800);
        ui_begin(&wui, (unsigned int address_to)pixels, 800, 800, 600);
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_DAWN, 1);

        // A field: the caret's x is where the pen is after the text before it.
        ui_text_open(&field, UI_TEXT_FIELD, (const p8 address_to) "Hello, wide W\xc3\xa9rld", 15, ui_text_keymap());
        ui_text_draw(&wui, &field, 10, 10, 300, 30, 0);
        ui_text_layout_line(&wui, &field, 0, &l);
        {
                b32 pen_agrees = true, round_trip = true;
                const font_face address_to faces[2] = {&regular, &regular};

                for (p32 k = 0; k <= l.count; k++)
                {
                        int advance = ui_advance(&wui, faces, l.shown, l.shown_at[k], ui_text_em(&wui, &field));

                        pen_agrees &= l.x[k] - advance <= 1 && advance - l.x[k] <= 1;
                        round_trip &= ui_text_at_x(&l, l.x[k]) == l.offset[k];
                }
                check("text widget: a field's caret is at the pen of the text before it", pen_agrees);
                check("text widget: a click at a cluster's edge is that cluster", round_trip);
                check("text widget: a field lays out its clusters", l.count == 14);
        }
        // The field's rules: Enter submits, a pasted line end is a blank, the keys go through the keymap.
        {
                struct window_event enter = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 28, '\r'};
                struct window_event select_all = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, 30, 1};
                struct window_event letter = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 45, 'x'};
                p8 text[64];

                field.focused = true;
                check("text widget: Enter in a field submits", ui_text_event(&wui, &field, &enter, 0, 0) == UI_TEXT_SUBMIT);
                ui_text_event(&wui, &field, &select_all, 0, 0);
                check("text widget: Ctrl+A selects all", doc_sel_start(&field.doc.selections[0]) == 0 && doc_sel_end(&field.doc.selections[0]) == 15);
                ui_text_event(&wui, &field, &letter, 0, 0);
                check("text widget: typing over a selection replaces it", ui_text_string(&field, text, sizeof(text)) == 1 && text[0] == 'x');
                doc_paste(&field.doc, (const p8 address_to) "a\nb\r\nc", 6);
                check("text widget: a field's paste makes line ends blanks",
                      ui_text_string(&field, text, sizeof(text)) == 6 && !memory_compare(text, "xa b c", 6) && doc_line_count(&field.doc) == 1);
        }
        ui_text_close(&field);

        // Composition for an input method: shown at the caret and no part of the text until committed (one edit of
        // its own) or cancelled; a control in it (ESC, C1's CSI, a line end) is refused, and so is text past its room.
        {
                const unsigned int address_to picture = (const unsigned int address_to)pixels;
                positive inked = 0, bare = 0;
                p8 typed[16];

                ui_text_open(&field, UI_TEXT_FIELD, (const p8 address_to) "ab", 2, ui_text_keymap());
                doc_select_set(&field.doc, 2, 2);
                field.focused = true;

                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
                ui_text_draw(&wui, &field, 10, 300, 300, 30, 0);
                for (int y = 300; y < 330; y++)
                        for (int x = 10; x < 310; x++)
                                bare += picture[(positive)y * 800 + (positive)x] != 0;
                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
                check("composition: whole characters with no control are taken", ui_text_preedit(&field, (const p8 address_to) "n\xc3\xaf", 3));
                ui_text_draw(&wui, &field, 10, 300, 300, 30, 0);
                for (int y = 300; y < 330; y++)
                        for (int x = 10; x < 310; x++)
                                inked += picture[(positive)y * 800 + (positive)x] != 0;
                check("composition: refused with ESC, a C1 control, a line end, or past its room",
                      !ui_text_preedit(&field, (const p8 address_to) "a\x1b[201~", 7) && !ui_text_preedit(&field, (const p8 address_to) "a\xc2\x9b", 3) &&
                      !ui_text_preedit(&field, (const p8 address_to) "a\nb", 3) &&
                      !ui_text_preedit(&field, (const p8 address_to) "0123456789012345678901234567890123456789012345678901234567890123456789", 70) &&
                      field.preedit_length == 3);
                check("composition: drawn, and no part of the text", inked > bare && doc_length(&field.doc) == 2);
                check("composition: committed it is typed in at the caret, and gone", ui_text_preedit_end(&field, true) == UI_TEXT_CHANGED &&
                      ui_text_string(&field, typed, sizeof(typed)) == 5 && !memory_compare(typed, "abn\xc3\xaf", 5) && !field.preedit_length);
                ui_text_preedit(&field, (const p8 address_to) "zz", 2);
                check("composition: cancelled nothing is typed", ui_text_preedit_end(&field, false) == UI_TEXT_MOVED && doc_length(&field.doc) == 5 &&
                      !field.preedit_length);
                ui_text_close(&field);
                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
        }

        // A code view: columns on whole cells, a tab to its stop, a wide character two cells.
        ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to) "int x;\n\tif (\xe4\xb8\xad) {\n}\n", 22, ui_text_keymap());
        view.doc.language = DOC_LANGUAGE_C;
        ui_text_draw(&wui, &view, 50, 50, 600, 400, 0x111113);
        ui_text_layout_line(&wui, &view, 1, &l);
        {
                b32 cells = true;

                for (p32 k = 0; k <= l.count; k++)
                        cells &= l.x[k] % view.cell == 0;
                check("text widget: the code view's columns are whole cells", cells && view.cell > 0);
                check("text widget: a tab runs to its stop", l.x[1] == 4 * view.cell);
                check("text widget: a wide character is two cells", l.x[6] - l.x[5] == 2 * view.cell);
                check("text widget: the gutter holds the line numbers", view.gutter >= 3 * view.cell && view.left == 50 + view.gutter);
        }
        // The hand: a double click takes the word, a triple the line; a drag after a double click grows by words.
        {
                struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
                struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
                int x = view.left + view.cell, y = view.top + view.line_height / 2;

                view.own = null;
                ui_text_event(&wui, &view, &press, x, y);
                ui_text_event(&wui, &view, &release, x, y);
                check("text widget: a click puts the caret", view.doc.selections[0].active == 1 && view.doc.selections[0].anchor == 1);
                ui_text_event(&wui, &view, &press, x, y);
                ui_text_event(&wui, &view, &release, x, y);
                check("text widget: a double click takes the word", doc_sel_start(&view.doc.selections[0]) == 0 && doc_sel_end(&view.doc.selections[0]) == 3);
                ui_text_event(&wui, &view, &press, x, y);
                ui_text_event(&wui, &view, &release, x, y);
                check("text widget: a triple click takes the line", doc_sel_start(&view.doc.selections[0]) == 0 && doc_sel_end(&view.doc.selections[0]) == 7);
        }
        ui_text_close(&view);
        // A code view's text is cut to its body, not to the window: a line wider than the body, scrolled so that it starts
        // left of the body, draws nothing left of the body's edge and nothing right of it.
        {
                static char wide[300];
                unsigned int address_to shown = (unsigned int address_to)pixels;
                positive outside_left = 0, outside_right = 0, inside = 0;

                for (int k = 0; k < 299; k++)
                        wide[k] = (char)('a' + k % 7);
                wide[299] = 0;
                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
                ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to)wide, 299, ui_text_keymap());
                view.doc.language = DOC_LANGUAGE_C;
                view.numbers = 0;
                view.scroll_x = 30;
                ui_text_draw(&wui, &view, 50, 50, 200, 100, 0x111113);
                for (int y = 50; y < 150; y++)
                {
                        for (int x = 0; x < 50; x++)
                                outside_left += shown[(positive)y * 800 + (positive)x] != 0;
                        for (int x = 250; x < 800; x++)
                                outside_right += shown[(positive)y * 800 + (positive)x] != 0;
                        for (int x = 50; x < 250; x++)
                                inside += shown[(positive)y * 800 + (positive)x] != 0;
                }
                check("text widget: a code line draws nothing left of its body", outside_left == 0 && view.left == 50);
                check("text widget: a code line draws nothing right of its body", outside_right == 0);
                check("text widget: a code line draws its text inside its body", inside > 0);
                ui_text_close(&view);
                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
        }
        // A code view's caret is in sight when its box changes size (a window shrinks, a zoom): the box it is drawn in
        // now has the caret's line, with no key between.
        {
                static char tall[160];
                int rows = 0;

                for (int k = 0; k < 40; k++)
                        tall[k * 4] = 'l', tall[k * 4 + 1] = (char)('0' + k / 10), tall[k * 4 + 2] = (char)('0' + k % 10), tall[k * 4 + 3] = '\n';
                ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to)tall, 160, ui_text_keymap());
                ui_text_draw(&wui, &view, 50, 50, 300, 400, 0x111113);
                view.doc.selections[0].anchor = view.doc.selections[0].active = 80; // line 20
                ui_text_draw(&wui, &view, 50, 50, 300, 60, 0x111113);
                rows = ui_text_rows(&view);
                check("text widget: a code view's caret is in sight after its box shrinks",
                      rows > 0 && view.top_line <= 20 && 20 < view.top_line + (p64)rows);
                ui_text_close(&view);
                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
        }

        /*
                The code view's decorations, read off the picture: the current line's tint is under its cells as past
                its text, a line range's ground and a highlight are under the cells they cover, a mark is drawn in the
                gutter, the indent has a guide at each tab stop inside it (a blank line's taken from the line after it),
                and a selection's spaces show a dot.
        */
        {
                const unsigned int address_to picture = (const unsigned int address_to)pixels;
                const unsigned int ground = 0x111113;
                p64 two, four;

                memory_fill_u32((unsigned int address_to)pixels, 800 * 600, 0);
                ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to) "alpha one\n        beta two\n\n        gamma\nzz  zz\n", 45, ui_text_keymap());
                view.doc.tab_size = 4;
                view.focused = true;
                view.caret_fade = UI_CARET_FULL;
                doc_select_set(&view.doc, 0, 0);
                two = doc_line_start(&view.doc, 1);
                four = doc_line_start(&view.doc, 4);
                doc_range_add(&view.doc, two, doc_line_end(&view.doc, 1), DOC_RANGE_LINE, 1);
                doc_range_add(&view.doc, 6, 9, DOC_RANGE_HIGHLIGHT, 0);
                doc_range_add(&view.doc, doc_line_start(&view.doc, 3), doc_line_start(&view.doc, 3), DOC_RANGE_MARK, 2);
                ui_text_draw(&wui, &view, 50, 50, 600, 400, ground);
                {
                        // A cell's top left corner pixel: paper, never a glyph's ink.
                        int row1 = view.top, row2 = view.top + view.line_height, row3 = view.top + 2 * view.line_height;
                        unsigned int caret_cell = picture[(positive)(row1 + 1) * 800 + (positive)(view.left + 1)] & 0xffffffu;
                        unsigned int past = picture[(positive)(row1 + 1) * 800 + (positive)(view.left + 40 * view.cell)] & 0xffffffu;
                        unsigned int found = picture[(positive)(row1 + 1) * 800 + (positive)(view.left + 7 * view.cell + 1)] & 0xffffffu;
                        unsigned int ranged = picture[(positive)(row2 + 1) * 800 + (positive)(view.left + 9 * view.cell + 1)] & 0xffffffu;
                        unsigned int plain = picture[(positive)(row2 + 1) * 800 + (positive)(view.left + 40 * view.cell)] & 0xffffffu;
                        unsigned int guide = picture[(positive)(row3 + view.line_height / 2) * 800 + (positive)(view.left + 4 * view.cell)] & 0xffffffu;
                        unsigned int beside = picture[(positive)(row3 + view.line_height / 2) * 800 + (positive)(view.left + 4 * view.cell + 2)] & 0xffffffu;
                        unsigned int mark = picture[(positive)(view.top + 3 * view.line_height + view.line_height / 2) * 800 + (positive)(50 + view.cell / 2)] & 0xffffffu;

                        check("text widget: the current line's tint is the paper of its cells, as past its text", caret_cell == past && caret_cell != ground);
                        check("text widget: a highlight is the paper of the cells it covers", found != caret_cell);
                        check("text widget: a line range's ground is its cells' paper and the rest of the line", ranged == plain && ranged != ground);
                        check("text widget: a blank line inside an indent has its guide, a pixel wide", guide != ground && guide != beside);
                        check("text widget: a mark is drawn in the gutter at its line", mark != ground);
                }
                // A selection over the line with two spaces: a dot in the middle of each, on the selection's paper.
                doc_select_set(&view.doc, four, four + 6);
                ui_text_draw(&wui, &view, 50, 50, 600, 400, ground);
                {
                        int row5 = view.top + 4 * view.line_height, mid = row5 + view.line_height / 2;
                        unsigned int corner = picture[(positive)(row5 + 1) * 800 + (positive)(view.left + 2 * view.cell + 1)] & 0xffffffu;
                        unsigned int dot = picture[(positive)(mid - wui.scale / 200) * 800 + (positive)(view.left + 2 * view.cell + view.cell / 2 - wui.scale / 200)] & 0xffffffu;

                        check("text widget: a selected space shows a dot on the selection's paper", corner != ground && dot != corner);
                }
                ui_text_close(&view);
        }

        // Nothing is drawn outside the box: the picture's edges are as they were cleared.
        {
                b32 clean = true;
                const unsigned int address_to picture = (const unsigned int address_to)pixels;

                for (int x = 0; x < 800; x++)
                        clean &= !picture[x] && !picture[599 * 800 + x];
                check("text widget: nothing is drawn outside its box", clean);
        }
        compositor_call(syscall(munmap), (positive)pixels, 800 * 600 * 4, 0, 0, 0, 0);
        menu_check(&wui);
        menu_defaults_check(&wui);
        kit_composites_check(&wui);
        editor_check(&wui);
        editor_close_check(&wui);
        editor_hostile_check(&wui);
        editor_tabs_check(&wui);
        editor_disk_check(&wui);
        editor_edge_check(&wui);
        editor_readonly_check(&wui);
        field_check(&wui);
        popmenu_bar_check(&wui);
        drop_check(&wui);
        band_check(&wui);
        squiggle_check(&wui);
        ui_over_check();
}

/*
        The text widget with text that reads from the right, in the real faces (Inter, with Noto Sans Arabic behind it): the line is laid
        out by the font engine (font_segments), so the caret, the click, the selection and the keys go by what is on the screen. A window that
        reads from the right has a field's text start at its right, an empty field's caret and placeholder there, and a code view's
        numbers in a gutter at its right.
*/
static fn text_bidi_check(string_address inter)
{
        static struct ui bui;
        static font_face regular, arabic;
        static struct ui_text_layout l;
        static const char address_to word = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";                // five Arabic letters
        static const char address_to mixed = "ab \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 cd";           // eleven clusters
        struct ui_text field, view;
        positive size = 0, bytes;
        long pixels;
        unsigned int address_to picture;
        int first = 0, last = 0;
        b32 order = true, levels = true, round_trip = true;

        memory_zero(&bui, sizeof(bui));
        bytes = compositor_map_file(inter, &size);
        check("text bidi: Inter loads", bytes && font_load(&regular, (const p8 address_to)bytes, size, 0) == 0);
        bytes = compositor_map_file((string_address) "fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf", &size);
        check("text bidi: Noto Sans Arabic loads", bytes && font_load(&arabic, (const p8 address_to)bytes, size, 0) == 0);
        if (failures || !ui_engine_open_sized(&bui, 256))
                return;
        font_fallback(bui.engine, &arabic, null);
        bui.regular = &regular;
        bui.bold = null;
        bui.scale = 100;
        pixels = ui_map(800 * 600 * 4);
        picture = (unsigned int address_to)pixels;
        ui_scratch_fit(&bui, 800);
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_DAWN, 1);

        // Arabic in a field of a window that reads from the right: a line that reads from the right, its caret at the right end at the start.
        ui_kit.rtl = 1;
        ui_begin(&bui, picture, 800, 800, 600);
        ui_text_open(&field, UI_TEXT_FIELD, (const p8 address_to)word, 10, ui_text_keymap());
        ui_text_draw(&bui, &field, 10, 10, 300, 30, 0);
        ui_text_layout_line(&bui, &field, 0, &l);
        check("text bidi: Arabic is one line of five clusters that reads from the right", l.bidi && l.base == 1 && l.count == 5 && l.level[0] == 1);
        for (p32 k = 0; k < l.count; k++)
                order &= l.x[k] > l.x[k + 1];
        check("text bidi: its caret goes from the right end of the line at the start to the left end as the text goes on",
              order && l.x[l.count] == 0 && l.width > 0 && l.x[0] >= l.width - 1 && l.x[0] <= l.width + 1);
        for (int y = 10; y < 40; y++)
                for (int x = 10; x < 310; x++)
                        if (picture[(positive)y * 800 + (positive)x] != picture[(positive)y * 800 + 5])
                                last = x > last ? x : last, first = first && first < x ? first : x;
        check("text bidi: the field's text is at its right end", first > 310 - l.width - 4 && last > 305 && last < 311);
        check("text bidi: the caret at the start of the line is at the right end of the box",
              field.right == 310 && ui_text_origin(&field, &l) + l.x[0] >= 308 && ui_text_origin(&field, &l) + l.x[0] <= 311);
        // Left goes forward in a line that reads from the right, and Right back.
        field.focused = true;
        doc_select_set(&field.doc, 0, 0);
        ui_text_command(&bui, &field, DOC_CMD_LEFT);
        check("text bidi: Left in Arabic goes on in the text, to the next letter", field.doc.selections[0].active == 2);
        ui_text_command(&bui, &field, DOC_CMD_RIGHT);
        check("text bidi: and Right goes back", field.doc.selections[0].active == 0);
        ui_text_command(&bui, &field, DOC_CMD_SELECT_LEFT);
        check("text bidi: Shift+Left selects the next letter", doc_sel_start(&field.doc.selections[0]) == 0 && doc_sel_end(&field.doc.selections[0]) == 2);
        ui_text_close(&field);
        // A word goes by the way the line reads too: Ctrl+Left in two Arabic words goes to the end of the first, and Ctrl+Right back to the start.
        ui_text_open(&field, UI_TEXT_FIELD, (const p8 address_to)"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", 21,
                     ui_text_keymap());
        ui_text_draw(&bui, &field, 10, 10, 300, 30, 0);
        field.focused = true;
        doc_select_set(&field.doc, 0, 0);
        ui_text_command(&bui, &field, DOC_CMD_WORD_LEFT);
        check("text bidi: Ctrl+Left in Arabic goes on to the end of the word", field.doc.selections[0].active == 10);
        ui_text_command(&bui, &field, DOC_CMD_WORD_RIGHT);
        check("text bidi: and Ctrl+Right goes back to its start", field.doc.selections[0].active == 0);
        ui_text_close(&field);

        // The same field, empty, with its words: the words and the caret start at the right.
        ui_text_open(&field, UI_TEXT_FIELD, (const p8 address_to) "", 0, ui_text_keymap());
        field.placeholder = "Search";
        memory_fill_u32(picture, 800 * 600, 0);
        ui_text_draw(&bui, &field, 10, 10, 300, 30, 0);
        first = last = 0;
        for (int y = 10; y < 40; y++)
                for (int x = 10; x < 310; x++)
                        if (picture[(positive)y * 800 + (positive)x])
                                last = x > last ? x : last, first = first && first < x ? first : x;
        check("text bidi: a placeholder starts at the right of the field in a window that reads from the right", first > 155 && last > 280);
        ui_kit.rtl = 0;
        memory_fill_u32(picture, 800 * 600, 0);
        ui_text_draw(&bui, &field, 10, 10, 300, 30, 0);
        first = last = 0;
        for (int y = 10; y < 40; y++)
                for (int x = 10; x < 310; x++)
                        if (picture[(positive)y * 800 + (positive)x])
                                last = x > last ? x : last, first = first && first < x ? first : x;
        check("text bidi: and at the left where it reads from the left", first && first < 30 && last < 155);
        ui_text_close(&field);

        // Latin and Arabic in a window that reads from the left: the Arabic is a run of its own inside the Latin.
        ui_text_open(&field, UI_TEXT_FIELD, (const p8 address_to)mixed, 16, ui_text_keymap());
        ui_text_draw(&bui, &field, 10, 10, 300, 30, 0);
        ui_text_layout_line(&bui, &field, 0, &l);
        check("text bidi: a line with Arabic in a window that reads from the left reads from the left, eleven clusters", l.bidi && l.base == 0 && l.count == 11);
        for (p32 k = 0; k < l.count; k++)
                levels &= (l.level[k] & 1) == (k >= 3 && k <= 7 ? 1u : 0u);
        check("text bidi: the Arabic letters are at an odd level and the Latin around them at an even one", levels);
        // Inside the Arabic the caret goes right to left; at its two ends it is next to the Latin it follows and the Latin that follows it.
        order = l.x[4] > l.x[5] && l.x[5] > l.x[6] && l.x[6] > l.x[7] && l.x[3] <= l.x[7] && l.x[8] >= l.x[4] && l.x[0] < l.x[1] && l.x[1] < l.x[2] &&
                l.x[2] < l.x[3] && l.x[8] < l.x[9] && l.x[9] < l.x[10] && l.x[10] < l.x[11];
        check("text bidi: the caret goes right to left through the Arabic and left to right through the rest", order);
        for (p32 k = 0; k <= l.count; k++)
        {
                int at = ui_text_hit(&bui, &field, ui_text_origin(&field, &l) + l.x[k], field.top + 1);
                p32 j = 0;

                while (j <= l.count && l.offset[j] != (p64)at)
                        j++;
                round_trip &= j <= l.count && l.x[j] == l.x[k];
        }
        check("text bidi: a click at a boundary's place is a boundary at that place, in each of the twelve", round_trip);
        // Right from the start visits every place on the line, left to right, once.
        {
                int places = 1, here;
                b32 rising = true;

                field.focused = true;
                doc_select_set(&field.doc, 0, 0);
                here = ui_text_x_of(&l, 0);
                for (int step = 0; step < 14; step++)
                {
                        int now;

                        ui_text_command(&bui, &field, DOC_CMD_RIGHT);
                        now = ui_text_x_of(&l, field.doc.selections[0].active);
                        if (now == here)
                                continue;
                        rising &= now > here;
                        places++, here = now;
                }
                check("text bidi: Right goes to the place on the screen to the right, each in turn, and ends at the line's right end",
                      rising && places >= 9 && here >= l.width - 1);
                for (int step = 0; step < 14; step++)
                        ui_text_command(&bui, &field, DOC_CMD_LEFT);
                check("text bidi: and Left back to the left end", ui_text_x_of(&l, field.doc.selections[0].active) == 0);
        }
        ui_text_close(&field);

        // A code view's numbers are in a gutter at the start of the lines: at the left, and at the right where the window reads from the right.
        for (int mode = 0; mode < 2; mode++)
        {
                int gutter_start = 0, gutter_end = 0;

                ui_kit.rtl = (unsigned int)mode;
                memory_fill_u32(picture, 800 * 600, 0);
                ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to) "a\nb\n", 4, ui_text_keymap());
                view.numbers = true;
                ui_text_draw(&bui, &view, 10, 10, 300, 90, 0);
                for (int y = 10; y < 100; y++)
                        for (int x = 10; x < 310; x++)
                                if (picture[(positive)y * 800 + (positive)x] && x < 10 + 36)
                                        gutter_start++;
                                else if (picture[(positive)y * 800 + (positive)x] && x >= 310 - 36)
                                        gutter_end++;
                if (mode)
                        check("text bidi: a code view's line numbers are in a gutter at its right where the window reads from the right", gutter_end > 0 && view.right == 274);
                else
                        check("text bidi: and at its left where it reads from the left, the text after them", gutter_start > 0 && gutter_end == 0 && view.left == 46);
                ui_text_close(&view);
        }
        ui_kit.rtl = 0;
}

/*
        The kit's runs in a window that reads from the right, in the real faces (Inter, with Noto Sans Arabic and Noto Sans Hebrew
        behind it), as the kit hands them to the engine (kit_seen) and read back as they land on the screen, left to right: each
        glyph's character, mirrored where the engine lays it at an odd level. A label in English is a paragraph that reads from the
        left, so a path, a number with its unit and a phrase in brackets read as they are written; in Arabic and Hebrew a run of Latin
        text with its numbers and punctuation reads in its own order inside the sentence (UAX #9's HL3: as if it were between LRI and
        PDI); a text that wraps is broken in logical order and each line is laid out with its paragraph's direction, islands and open
        isolates; and a text that is wholly one way is handed to the engine as it always was.
*/
// The run k a check heard, as it reads on the screen left to right, as UTF-8 into into.
static string_address kit_seen_read(font_engine address_to engine, int k, char address_to into, positive room)
{
        static font_place places[KIT_SEEN_TEXT];
        positive count, used = 0;

        into[0] = 0;
        if (k >= kit_seen.count)
                return (string_address)into;
        count = font_layout(engine, kit_seen.faces[k], kit_seen.faces_count[k], kit_seen.text[k], kit_seen.length[k], kit_seen.size[k],
                            kit_seen.flags[k], places, KIT_SEEN_TEXT);
        for (positive at = 0; at < count && used + 4 < room; at++)
        {
                p32 code = 0;

                // A letter the face draws as more than one glyph (an Arabic final form and its dots) is one character on the screen.
                if (at && places[at].cluster == places[at - 1].cluster)
                        continue;
                font_decode(kit_seen.text[k], kit_seen.length[k], places[at].cluster, &code);
                code = places[at].level & 1 ? font_mirror_of(code) : code;
                if (code < 0x80)
                        into[used++] = (char)code;
                else if (code < 0x800)
                        into[used++] = (char)(0xc0 | code >> 6), into[used++] = (char)(0x80 | (code & 63));
                else
                        into[used++] = (char)(0xe0 | code >> 12), into[used++] = (char)(0x80 | (code >> 6 & 63)), into[used++] = (char)(0x80 | (code & 63));
        }
        into[used] = 0;
        return (string_address)into;
}

// A label drawn as a row's part is (ui_kit_text_flipped), the window reading from the right or not: whether the kit handed the engine one
// run and it reads on the screen as want.
static b32 kit_bidi_reads(struct ui address_to ui, const char address_to text, b32 rtl, const char address_to want)
{
        static char read[KIT_SEEN_TEXT * 2];

        kit_seen.count = 0, kit_seen.on = 1, ui_kit.rtl = rtl;
        ui_kit_text_flipped(ui, 10, 40, (string_address)text, UI_BODY_MEDIUM, 0xffffffffu, 0, 0, 800);
        kit_seen.on = 0, ui_kit.rtl = 0;
        return kit_seen.count == 1 && string_equals(kit_seen_read(ui->engine, 0, read, sizeof(read)), (string_address)want);
}

// The same label handed to the engine byte for byte as it is written, with the paragraph direction right to left or not.
static b32 kit_bidi_handed(struct ui address_to ui, const char address_to text, b32 rtl, b32 right_to_left)
{
        positive length = string_length((string_address)text);

        kit_seen.count = 0, kit_seen.on = 1, ui_kit.rtl = rtl;
        ui_kit_text_flipped(ui, 10, 40, (string_address)text, UI_BODY_MEDIUM, 0xffffffffu, 0, 0, 800);
        kit_seen.on = 0, ui_kit.rtl = 0;
        return kit_seen.count == 1 && kit_seen.length[0] == length && !memory_compare(kit_seen.text[0], text, length) &&
               !(kit_seen.flags[0] & FONT_RTL) == !right_to_left;
}

// A label's width in a window that reads from the right, as the kit measures it.
static int kit_bidi_width(struct ui address_to ui, const char address_to text)
{
        int width;

        ui_kit.rtl = 1;
        width = ui_kit_measure(ui, (string_address)text, UI_BODY_MEDIUM);
        ui_kit.rtl = 0;
        return width;
}

static unsigned int address_to kit_bidi_pixels; // the picture the checks draw in, 800 by 600

// A text of the view in a column width wide, at the right of a window 800 wide that reads from the right, laid out and drawn: whether it is
// the lines given, each reading on the screen as its want.
static b32 kit_bidi_wraps(struct ui address_to ui, const char address_to words, int width, int lines, const char address_to const address_to want)
{
        static struct ui_view view;
        static char read[KIT_SEEN_TEXT * 2];
        b32 same = true;

        ui_kit.rtl = 1;
        memory_zero(&view, sizeof(view));
        ui_view_begin(&view, ui, 800, 0);
        ui_col(.w = width)
                ui_text(words, .class = "quiet");
        ui_view_end(&view);
        kit_seen.count = 0, kit_seen.on = 1;
        ui_begin(ui, kit_bidi_pixels, 800, 800, 600);
        ui_view_draw(ui, &view, false);
        kit_seen.on = 0, ui_kit.rtl = 0;
        for (int k = 0; k < lines; k++)
                same &= string_equals(kit_seen_read(ui->engine, k, read, sizeof(read)), (string_address)want[k]);
        return kit_seen.count == lines && same;
}

static fn kit_bidi_check(string_address inter)
{
        static struct ui kui;
        static font_face regular, arabic, hebrew;
        static char read[KIT_SEEN_TEXT * 2], fitted[64];
        positive size = 0, bytes;
        b32 loaded;
        long pixels;

        memory_zero(&kui, sizeof(kui));
        bytes = compositor_map_file(inter, &size);
        loaded = bytes && font_load(&regular, (const p8 address_to)bytes, size, 0) == 0;
        bytes = compositor_map_file((string_address) "fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf", &size);
        loaded = loaded && bytes && font_load(&arabic, (const p8 address_to)bytes, size, 0) == 0;
        bytes = compositor_map_file((string_address) "fonts/noto-sans-hebrew/NotoSansHebrew-Regular.ttf", &size);
        loaded = loaded && bytes && font_load(&hebrew, (const p8 address_to)bytes, size, 0) == 0;
        check("kit bidi: Inter, Noto Sans Arabic and Noto Sans Hebrew load", loaded);
        if (!loaded || !ui_engine_open_sized(&kui, 256))
                return;
        font_fallback(kui.engine, &arabic, null);
        font_fallback(kui.engine, &hebrew, null);
        kui.regular = &regular, kui.bold = null, kui.scale = 100;
        pixels = ui_map(800 * 600 * 4);
        kit_bidi_pixels = (unsigned int address_to)pixels;
        ui_scratch_fit(&kui, 800);
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_DAWN, 1);
        ui_begin(&kui, kit_bidi_pixels, 800, 800, 600);
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);

        // English labels in a window that reads from the right: each reads as it is written.
        check("kit bidi: a path in a window that reads from the right reads /usr/bin, its slash at its start", kit_bidi_reads(&kui, "/usr/bin", 1, "/usr/bin"));
        check("kit bidi: a number and its unit read 250 ms, the number first", kit_bidi_reads(&kui, "250 ms", 1, "250 ms"));
        check("kit bidi: a number and a unit of two words read 30 per second, the number first",
              kit_bidi_reads(&kui, "30 per second", 1, "30 per second"));
        check("kit bidi: a phrase in brackets reads (percent, or default), each bracket the right way round",
              kit_bidi_reads(&kui, "(percent, or default)", 1, "(percent, or default)"));
        check("kit bidi: an English label with a unit in brackets reads as it is written",
              kit_bidi_reads(&kui, "Key repeat delay (ms, or default)", 1, "Key repeat delay (ms, or default)"));
        check("kit bidi: an English label is handed to the engine as it is written, a paragraph that reads from the left",
              kit_bidi_handed(&kui, "Key repeat delay (ms, or default)", 1, false));

        // Arabic and Hebrew sentences with Latin in them: the Latin runs keep their own order, the sentence reads from the right.
        check("kit bidi: an Arabic sentence with 250 ms and a path reads 250 ms and /usr/bin inside it, the sentence from the right",
              kit_bidi_reads(&kui, "\xd9\x85\xd8\xaf\xd8\xa9 250 ms \xd9\x85\xd9\x86 /usr/bin", 1,
                             "/usr/bin \xd9\x86\xd9\x85 250 ms \xd8\xa9\xd8\xaf\xd9\x85"));
        check("kit bidi: a Hebrew sentence with -5 dB and C++ in it reads them as written",
              kit_bidi_reads(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 -5 dB \xd7\xa9\xd7\x9c C++", 1,
                             "C++ \xd7\x9c\xd7\xa9 -5 dB \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94"));
        check("kit bidi: a Latin phrase in brackets inside Hebrew keeps its brackets round it the right way",
              kit_bidi_reads(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 (ms, or default)", 1,
                             "(ms, or default) \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94"));
        check("kit bidi: a Latin run with a bracket pair in it keeps the pair whole and the right way round, its bracket first or last",
              kit_bidi_reads(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 Settings (beta) \xd7\xa9\xd7\x9c", 1,
                             "\xd7\x9c\xd7\xa9 Settings (beta) \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94") &&
                  kit_bidi_reads(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 (beta) Settings \xd7\xa9\xd7\x9c", 1,
                                 "\xd7\x9c\xd7\xa9 (beta) Settings \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94"));
        check("kit bidi: a sentence that ends in a Latin word ends at its left with its full stop",
              kit_bidi_reads(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 Bluetooth.", 1, ".Bluetooth \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94"));
        check("kit bidi: an explicit first strong isolate in a string is the engine's, and its path reads as written",
              kit_bidi_reads(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 \xe2\x81\xa8/usr/bin\xe2\x81\xa9", 1, "/usr/bin \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94") &&
                  kit_bidi_handed(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 \xe2\x81\xa8/usr/bin\xe2\x81\xa9", 1, true));

        // Text wholly one way is handed to the engine as it always was.
        check("kit bidi: Hebrew with a number and a full stop and no Latin is handed to the engine as it is, right to left",
              kit_bidi_handed(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 3 \xd7\xa9\xd7\x9c.", 1, true));
        check("kit bidi: a number alone is handed to the engine as it is, right to left in a window that reads from the right",
              kit_bidi_handed(&kui, "1 - 3%", 1, true));
        check("kit bidi: in a window that reads from the left every label is handed to the engine as it is, left to right",
              kit_bidi_handed(&kui, "/usr/bin", 0, false) && kit_bidi_handed(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 250 ms", 0, false));

        // A text that wraps: broken in logical order, then each line laid out with its paragraph's direction and islands.
        {
                static const char address_to brackets[2] = {"Mouse sensitivity (percent, or", "default)"};
                static const char address_to stop[2] = {"Volume is now at", "30%."};
                static const char address_to hebrew_lines[2] = {"250 \xd7\x9d\xd7\x99\xd7\xa9\xd7\xa7\xd7\x9e\xd7\x94 \xd7\xaa\xd7\xa8\xd7\x96\xd7\x97 \xd7\x9c\xd7\xa9 \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94",
                                                                 "/usr/bin \xd7\x99\xd7\xa0\xd7\xa4\xd7\x9c ms"};
                static const char address_to kept[2] = {"ms, or) \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94", "(default"};
                static const char address_to opened[2] = {"Delay \xd7\x94\xd7\x99\xd7\x94\xd7\xa9\xd7\x94", "ms 250 set"};

                check("kit bidi: a wrapped English label's last line reads default), its bracket closing it",
                      kit_bidi_wraps(&kui, "Mouse sensitivity (percent, or default)", kit_bidi_width(&kui, "Mouse sensitivity (percent, or") + 4, 2,
                                     brackets));
                check("kit bidi: a wrapped English sentence's last line of no letters reads 30 percent and a full stop as its paragraph does, left to right",
                      kit_bidi_wraps(&kui, "Volume is now at 30%.", kit_bidi_width(&kui, "Volume is now at") + 4, 2, stop));
                check("kit bidi: a Hebrew sentence broken between 250 and ms reads 250 at the end of its first line and ms then /usr/bin on its second",
                      kit_bidi_wraps(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 \xd7\xa9\xd7\x9c \xd7\x97\xd7\x96\xd7\xa8\xd7\xaa \xd7\x94\xd7\x9e\xd7\xa7\xd7\xa9\xd7\x99\xd7\x9d 250 ms \xd7\x9c\xd7\xa4\xd7\xa0\xd7\x99 /usr/bin",
                                     kit_bidi_width(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 \xd7\xa9\xd7\x9c \xd7\x97\xd7\x96\xd7\xa8\xd7\xaa \xd7\x94\xd7\x9e\xd7\xa7\xd7\xa9\xd7\x99\xd7\x9d 250") + 4,
                                     2, hebrew_lines));
                check("kit bidi: a Hebrew sentence's line of Latin alone keeps the sentence's direction, its bracket closing at the left",
                      kit_bidi_wraps(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 (ms, or default)",
                                     kit_bidi_width(&kui, "\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 (ms, or") + 4, 2, kept));
                check("kit bidi: a line that starts inside a right to left isolate is laid out inside it, as the paragraph is (ms 250 set)",
                      kit_bidi_wraps(&kui, "Delay \xe2\x81\xa7\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 250 ms\xe2\x81\xa9 set",
                                     kit_bidi_width(&kui, "Delay \xe2\x81\xa7\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94") + 4, 2, opened));
                check("kit bidi: a line that starts inside a first strong isolate takes the direction the isolate's first letter gave it on the line before",
                      kit_bidi_wraps(&kui, "Delay \xe2\x81\xa8\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94 250 ms\xe2\x81\xa9 set",
                                     kit_bidi_width(&kui, "Delay \xe2\x81\xa8\xd7\x94\xd7\xa9\xd7\x94\xd7\x99\xd7\x94") + 4, 2, opened));
        }

        // A cut keeps the direction of the text it is cut from: a Hebrew label whose kept start is Latin has its dots at its left end.
        {
                int room = kit_bidi_width(&kui, "UTF-8...") + 2;
                string_address shown;

                ui_kit.rtl = 1;
                shown = ui_fit(&kui, (string_address) "UTF-8 \xd7\xa2\xd7\x9d BOM", UI_BODY_MEDIUM, room, fitted, sizeof(fitted));
                kit_seen.count = 0, kit_seen.on = 1;
                ui_kit_text_flipped(&kui, 10, 40, shown, UI_BODY_MEDIUM, 0xffffffffu, 0, 0, 800);
                kit_seen.on = 0, ui_kit.rtl = 0;
                kit_seen_read(kui.engine, 0, read, sizeof(read));
                check("kit bidi: a Hebrew label cut to its Latin start reads with its three dots at its left end",
                      kit_seen.count == 1 && !memory_compare(read, "...UTF", 6));
        }
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);
        compositor_call(syscall(munmap), (positive)pixels, 800 * 600 * 4, 0, 0, 0, 0);
}

/*
        The kit's field (ui_field_event over the engine), the one every input of Canvas is, driven as a window drives
        it: keys as the compositor sends them, the pointer on its well, a paste's text. A blank and any character are
        typed anywhere; the caret moves by characters (whole ones of several bytes), words and ends; Shift selects and
        typing replaces; the deletes and undo; a paste of lines is one line with no control in it; its rules hold for
        a key, a paste and a composition alike (a character refused, with its words, and a length cut); a masked field
        shows dots, lets nothing out and has no words; the keys a field does not take are its app's; the hand puts the
        caret, a double click takes a word and a triple the line, a drag selects; the menu greys what cannot be done;
        and a caret left alone stops blinking, so an idle field wakes nothing.
*/
#ifdef UI_TEXT_REFUSED
static p32 field_key(struct ui address_to wui, struct ui_text address_to t, int key, unsigned int code, unsigned int flags)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | flags, key, (int)code};

        return ui_field_event(wui, t, null, &event, 0, 0);
}

// Each character of text as a key that types it; what the field answered, every key's together.
static p32 field_type(struct ui address_to wui, struct ui_text address_to t, const char address_to text)
{
        p32 asks = 0;
        positive length = string_length((string_address)text);

        for (positive at = 0; at < length;)
        {
                p32 code;
                positive size = doc_decode((const p8 address_to)text + at, length - at, &code);

                asks |= field_key(wui, t, 0, code, 0);
                at += size ? size : 1;
        }
        return asks;
}

static b32 field_is(const struct ui_text address_to t, const char address_to want)
{
        p8 held[256];
        positive length = ui_text_string(t, held, sizeof(held));

        return length == string_length((string_address)want) && !memory_compare(held, want, length);
}

static p64 field_caret(const struct ui_text address_to t)
{
        return t->doc.selections[t->doc.primary].active;
}

static b32 field_selected(const struct ui_text address_to t, p64 start, p64 finish)
{
        return doc_sel_start(&t->doc.selections[t->doc.primary]) == start && doc_sel_end(&t->doc.selections[t->doc.primary]) == finish;
}

static p32 field_button(struct ui address_to wui, struct ui_text address_to t, int button, b32 down, int x, int y, unsigned int flags)
{
        struct window_event event = {WINDOW_EVENT_BUTTON, (down ? WINDOW_KEY_DOWN : 0) | flags, button, 0};

        return ui_field_event(wui, t, null, &event, x, y);
}

static p32 field_motion(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        struct window_event event = {WINDOW_EVENT_MOTION, 0, x, y};

        return ui_field_event(wui, t, null, &event, x, y);
}

// The x of an offset of a field's line, as the pen puts it: where a click lands on that boundary.
static int field_x(struct ui address_to wui, struct ui_text address_to t, p64 offset)
{
        static struct ui_text_layout l;

        ui_text_layout_line(wui, t, 0, &l);
        return t->left - (int)t->scroll_x + ui_text_x_of(&l, offset);
}

static const char address_to field_says_five(p32 code)
{
        (void)code;
        return null;
}

// The picture a field draws into the window's pixels, its box at 20, 20: the frame cleared, the field drawn, and the
// whole frame copied into into (400 by 100 pixels).
static fn field_picture(struct ui address_to wui, struct ui_text address_to t, unsigned int address_to into)
{
        ui_begin(wui, wui->pixels, wui->pitch, wui->width, wui->height);
        ui_text_draw(wui, t, 20, 20, 300, 30, 0);
        memory_copy(into, wui->pixels, 400 * 100 * sizeof(unsigned int));
}

/*
        The touch selection of a field: a finger that lands and is held still is a long press (the compositor cancels the
        press, then sends the right button flagged WINDOW_KEY_HOLD). It selects the word under it, the moves after it extend
        the selection by words, and the lift ends it, keeping the selection. A short tap is only a caret, and a long press's
        right button is no menu (a mouse's right button still asks for one, which field_check holds).
*/
static fn touch_hold_check(struct ui address_to wui, struct ui_text address_to t)
{
        const unsigned int touch = WINDOW_KEY_TOUCH, hold = WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD;
        const unsigned int cancel = WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD | WINDOW_KEY_CANCEL;
        int y, beta, gamma;
        p32 asks;

        ui_text_set(t, (const p8 address_to) "alpha beta gamma", 16);
        doc_select_set(&t->doc, 0, 0);
        ui_text_draw(wui, t, 20, 20, 300, 30, 0);
        y = t->top + t->line_height / 2;
        beta = field_x(wui, t, 6), gamma = field_x(wui, t, 13);
        field_button(wui, t, 0x110, true, beta + 1, y, touch);
        field_button(wui, t, 0x110, false, beta + 1, y, touch);
        check("touch: a short tap is a caret, not a selection", field_selected(t, 6, 6));
        field_button(wui, t, 0x110, true, beta + 1, y, touch);
        field_button(wui, t, 0x110, false, beta + 1, y, cancel);
        asks = field_button(wui, t, 0x111, true, beta + 1, y, hold);
        check("touch: a long press selects the word under the finger, and its right button is no menu",
              field_selected(t, 6, 10) && !(asks & UI_TEXT_MENU));
        field_button(wui, t, 0x111, false, beta + 1, y, hold);
        asks = ui_field_event(wui, t, null, &(struct window_event){WINDOW_EVENT_MOTION, touch, gamma + 1, y}, gamma + 1, y);
        check("touch: a drag after a long press extends the selection by words", field_selected(t, 6, 16));
        asks = field_button(wui, t, 0x110, false, gamma + 1, y, cancel);
        check("touch: the lift ends the drag and keeps the selection", (asks & UI_TEXT_MOVED) && field_selected(t, 6, 16) && !t->sel.dragging);

        // A tap on the selection keeps it and shows the actions bar (no popup menu), which is Cut, Copy, Paste and Select All.
        t->sel.bar = false;
        field_button(wui, t, 0x110, true, beta + 1, y, touch);
        asks = field_button(wui, t, 0x110, false, beta + 1, y, touch);
        check("touch: a tap on the selection keeps it, shows the actions bar and asks for no popup menu",
              !(asks & UI_TEXT_MENU) && t->sel.bar && field_selected(t, 6, 16));
        {
                static struct ui_popmenu m;

                m.count = 0;
                ui_text_menu_items(&m, t, true);
                check("touch: the actions of a selection are Cut, Copy, Paste and Select All",
                      m.count == 4 && m.items[0].id == UI_TEXT_MENU_CUT && m.items[1].id == UI_TEXT_MENU_COPY &&
                              m.items[2].id == UI_TEXT_MENU_PASTE && m.items[3].id == UI_TEXT_MENU_ALL);
        }
        // A tap off the selection is a caret, and asks for no menu; a press cancelled by the compositor is no tap either.
        field_button(wui, t, 0x110, true, field_x(wui, t, 0) + 1, y, touch);
        asks = field_button(wui, t, 0x110, false, field_x(wui, t, 0) + 1, y, touch);
        check("touch: a tap off the selection is a caret and asks for no menu", field_selected(t, 0, 0) && !(asks & UI_TEXT_MENU));
        field_button(wui, t, 0x110, true, beta + 1, y, touch);
        field_button(wui, t, 0x110, false, beta + 1, y, cancel);
        field_button(wui, t, 0x111, true, beta + 1, y, hold);
        field_button(wui, t, 0x111, false, beta + 1, y, hold);
        field_button(wui, t, 0x110, false, beta + 1, y, cancel);
        check("touch: a long press again selects the word", field_selected(t, 6, 10));
        field_button(wui, t, 0x110, true, beta + 1, y, touch);
        asks = field_button(wui, t, 0x110, false, beta + 1, y, cancel);
        check("touch: a cancelled press on the selection asks for no menu and keeps the selection",
              !(asks & UI_TEXT_MENU) && field_selected(t, 6, 10));

        // Each end of a touch selection has a handle: a stem under its line and a knob hung below it, in the accent. A finger on
        // a knob moves that end by characters, and the other end stays where it was.
        ui_text_draw(wui, t, 20, 20, 300, 60, 0);
        ui_text_overlay(wui, t);
        y = t->top + t->line_height / 2;
        {
                int bottom = t->top + t->line_height, radius = 6 * wui->scale / 100, knob = field_x(wui, t, 10);

                check("touch: a selection's end has a handle, a knob in the accent under its line",
                      (wui->pixels[(positive)(bottom + radius) * wui->pitch + (positive)knob] & 0xffffffu) == (ui_role(UI_PRIMARY) & 0xffffffu));
                field_button(wui, t, 0x110, true, knob, bottom + radius, touch);
                ui_field_event(wui, t, null, &(struct window_event){WINDOW_EVENT_MOTION, touch, field_x(wui, t, 13) + 1, y}, field_x(wui, t, 13) + 1, y);
                asks = field_button(wui, t, 0x110, false, field_x(wui, t, 13) + 1, y, touch);
                check("touch: a finger on a handle moves that end by characters, and the other end stays",
                      (asks & UI_TEXT_MOVED) && field_selected(t, 6, 13));
        }
}

/*
        A selection drag past the top edge of a code view scrolls it, a line at a time as the time passes, and the further past the
        edge the finger is the faster; the selection follows the text as it scrolls. A drag back inside the box, and the lift, stop it.
*/
static fn touch_scroll_open(struct ui address_to wui, struct ui_text address_to view, int top)
{
        static char text[40 * 8];
        positive length = 0;

        for (int n = 0; n < 40; n++)
        {
                text[length++] = 'l', text[length++] = 'i', text[length++] = 'n', text[length++] = 'e', text[length++] = ' ';
                text[length++] = (char)('0' + n / 10), text[length++] = (char)('0' + n % 10), text[length++] = '\n';
        }
        ui_text_open(view, UI_TEXT_CODE, (const p8 address_to)text, length, ui_text_keymap());
        view->focused = true;
        view->top_line = (p64)top;
        view->sel.dragging = false;
        view->sel.edge.past = 0;
        view->sel.edge.at = 0;
        ui_text_draw(wui, view, 20, 20, 300, 100, 0);
}

// Presses the third visible line and drags past the top edge by past, two steps 200 ms apart; the lines scrolled, and whether
// the selection's start is the first line now shown.
static int touch_scroll_rows(struct ui address_to wui, struct ui_text address_to view, int past, b32 address_to follows)
{
        b32 redraw = false;
        positive now = 1000000000ull;
        int x, row;

        touch_scroll_open(wui, view, 10);
        x = view->left + 2;
        row = view->top + 2 * view->line_height + view->line_height / 2;
        field_button(wui, view, 0x110, true, x, row, 0);
        field_motion(wui, view, x, view->top - past);
        (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_select_autoscroll(wui, ui_text_sel(view), &redraw));
        (ui_kit.now = ui_kit.test_clock = (b64)(now + 200000000ull), ui_select_autoscroll(wui, ui_text_sel(view), &redraw));
        *follows = doc_sel_start(&view->doc.selections[view->doc.primary]) == doc_line_start(&view->doc, view->top_line);
        field_button(wui, view, 0x110, false, x, row, 0);
        ui_text_close(view);
        return (int)(10 - (long)view->top_line);
}

static fn touch_scroll_check(struct ui address_to wui)
{
        static struct ui_text view;
        b32 near_follows = false, far_follows = false, redraw = false;
        int near_rows = touch_scroll_rows(wui, &view, 4, &near_follows), far_rows = touch_scroll_rows(wui, &view, 200, &far_follows);
        int x, row;
        positive now = 1000000000ull;

        check("scroll: a drag past the top edge scrolls the code view, and the further past it the faster",
              near_rows >= 1 && far_rows > near_rows);
        check("scroll: the selection follows the text as it scrolls", near_follows && far_follows);

        touch_scroll_open(wui, &view, 10);
        x = view.left + 2;
        row = view.top + 2 * view.line_height + view.line_height / 2;
        field_button(wui, &view, 0x110, true, x, row, 0);
        field_motion(wui, &view, x, view.top - 200);
        (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_select_autoscroll(wui, ui_text_sel(&view), &redraw));
        field_motion(wui, &view, x, view.top + view.line_height);
        check("scroll: a drag back inside the box stops it", !(ui_kit.now = ui_kit.test_clock = (b64)(now + 200000000ull), ui_select_autoscroll(wui, ui_text_sel(&view), &redraw)) && view.top_line == 10);
        field_motion(wui, &view, x, view.top - 200);
        (ui_kit.now = ui_kit.test_clock = (b64)(now + 400000000ull), ui_select_autoscroll(wui, ui_text_sel(&view), &redraw));
        field_button(wui, &view, 0x110, false, x, row, 0);
        check("scroll: the lift stops it", !(ui_kit.now = ui_kit.test_clock = (b64)(now + 600000000ull), ui_select_autoscroll(wui, ui_text_sel(&view), &redraw)) && view.top_line == 10);
        ui_text_close(&view);
}

/*
        A finger's selection (ui_text_finger, ui_text_overlay), one rule for every surface that takes it, tested here once: a code
        view, a field, a masked field and a read only view, at each scale the kit draws at. A long press selects the word under
        the finger; the selection shows a handle at each end, a finger's width to take it by; a finger on a handle moves that
        end across words and lines (and past the box's edge, the view scrolling by the kit's speed for the scale) to the ends
        of the text, the two ends able to cross; the actions bar shows what the surface allows, inside the window, over the
        selection or under it, and not while something moves; a tap on the selection or a handle shows or takes it away, a tap
        elsewhere clears the selection; a finger that drags the text still scrolls it; a mouse is as it was. Each answer is a
        bit; it is kept only if it held at every scale, and one row says it.
*/
#define TS_WIDTH 1280
#define TS_HEIGHT 960
#define TS_LEFT 20
#define TS_LINES 40

enum
{
        TS_LONG_PRESS, TS_HANDLES, TS_GRAB_IN, TS_GRAB_OUT, TS_ACROSS_LINES, TS_KEEPS_START, TS_CROSS, TS_TO_END, TS_TO_START, TS_SPEED,
        TS_BAR_ITEMS, TS_BAR_ABOVE, TS_BAR_BELOW, TS_BAR_INSIDE, TS_BAR_SELECT_ALL, TS_BAR_CUT, TS_BAR_COPY, TS_TOGGLE_SELECTION,
        TS_TOGGLE_HANDLE, TS_HANDLE_BAR, TS_TAP_CLEARS, TS_SCROLLS, TS_BAR_HIDES, TS_BAR_RETURNS, TS_DOUBLE_TAP, TS_TRIPLE_TAP, TS_MOUSE,
        TS_FADE, TS_REDUCED, TS_IDLE, TS_FIELD_ITEMS, TS_FIELD_ACROSS, TS_FIELD_TO_END, TS_FIELD_TO_START, TS_MASKED, TS_READ_ONLY,
        TS_CARET_BAR, TS_EMPTY_BAR, TS_WRAP_HIT, TS_WRAP_HANDLE, TS_FOLD_HIT
};

static struct ui_text ts_view;
static struct windows *ts_table_scratch; // the table, mapped at its first use (windows_scratch_table)
static struct window_row ts_own;
static unsigned int address_to ts_pixels;
static int ts_top = 40;

// The widget drawn as the app draws it: its picture cleared, the widget, then its overlay.
static void ts_draw(struct ui address_to wui, struct ui_text address_to t)
{
        ui_begin(wui, ts_pixels, TS_WIDTH, TS_WIDTH, TS_HEIGHT);
        if (t->mode == UI_TEXT_FIELD)
                ui_text_draw(wui, t, TS_LEFT, ts_top, 400 * wui->scale / 100, 44 * wui->scale / 100, 0);
        else
                ui_text_draw(wui, t, TS_LEFT, ts_top, 400 * wui->scale / 100, 300 * wui->scale / 100, 0);
        ui_text_overlay(wui, t);
}

static p32 ts_event(struct ui address_to wui, struct ui_text address_to t, unsigned int type, unsigned int flags, int a, int x, int y)
{
        struct window_event event = {type, flags, a, 0};

        return ui_field_event(wui, t, null, &event, x, y);
}

static void ts_reset(struct ui_text address_to t)
{
        t->sel.click.at = 0, t->sel.click.count = 0;
}

static p32 ts_down(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        return ts_event(wui, t, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, x, y);
}

static p32 ts_move(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        return ts_event(wui, t, WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, x, y);
}

static p32 ts_up(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        return ts_event(wui, t, WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, x, y);
}

// A tap: a press and a lift, the click count not carried from the last one.
static p32 ts_tap(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        ts_reset(t);
        ts_down(wui, t, x, y);
        return ts_up(wui, t, x, y);
}

// A long press the way the compositor sends it, then its lift: the press taken back (cancelled and held), the right button flagged
// held, and the left release flagged held that is the finger leaving.
static void ts_hold(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        const unsigned int held = WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD;

        ts_reset(t);
        ts_down(wui, t, x, y);
        ts_event(wui, t, WINDOW_EVENT_BUTTON, held | WINDOW_KEY_CANCEL, 0x110, x, y);
        ts_event(wui, t, WINDOW_EVENT_BUTTON, held | WINDOW_KEY_DOWN, 0x111, x, y);
        ts_event(wui, t, WINDOW_EVENT_BUTTON, held, 0x111, x, y);
}

static void ts_lift(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        ts_event(wui, t, WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD | WINDOW_KEY_CANCEL, 0x110, x, y);
}

// A long press and its lift where it is, and the widget drawn with its handles and bar.
static void ts_select(struct ui address_to wui, struct ui_text address_to t, int x, int y)
{
        ts_hold(wui, t, x, y);
        ts_lift(wui, t, x, y);
        ts_draw(wui, t);
}

static b32 ts_is(const struct ui_text address_to t, p64 start, p64 finish)
{
        return doc_sel_start(&t->doc.selections[t->doc.primary]) == start && doc_sel_end(&t->doc.selections[t->doc.primary]) == finish;
}

// The centre of the knob of an offset's handle, where a finger takes it. False when the offset is not in the box.
static b32 ts_knob(struct ui address_to wui, struct ui_text address_to t, p64 offset, int address_to x, int address_to y)
{
        int top;

        if (ui_text_where(wui, t, offset, x, &top))
                return false;
        *y = top + t->line_height + ui_dev(wui, UI_SELECT_HANDLE);
        return true;
}

// The middle of a line's row of the code view, and an x a little inside a column of it.
static int ts_row(struct ui_text address_to t, p64 line)
{
        return t->top + (int)(line - t->top_line) * t->line_height + t->line_height / 2;
}

static int ts_col(struct ui address_to wui, struct ui_text address_to t, int col)
{
        return t->left + col * t->cell + ui_dev(wui, 2);
}

// The accent's pixel at x, y of the picture.
static b32 ts_accent(struct ui address_to wui, int x, int y)
{
        return (wui->pixels[(positive)y * TS_WIDTH + (positive)x] & 0xffffffu) == (ui_role(UI_PRIMARY) & 0xffffffu);
}

// The view's autoscroll, 16 ms a step for steps steps from now.
static void ts_ticks(struct ui address_to wui, struct ui_text address_to t, positive now, int steps)
{
        b32 redraw = false;

        for (int step = 0; step <= steps; step++)
                (ui_kit.now = ui_kit.test_clock = (b64)(now + (positive)step * 16000000ull), ui_select_autoscroll(wui, ui_text_sel(t), &redraw));
}

// Whether the bar's items are these ids, in this order (0 for none).
static b32 ts_ids(const struct ui_text address_to t, int a, int b, int c, int d)
{
        int want[4] = {a, b, c, d};

        for (int k = 0; k < 4; k++)
                if ((want[k] != 0) != (k < t->sel.bar_count) || (want[k] && t->sel.bar_id[k] != want[k]))
                        return false;
        return true;
}

static p64 ts_line_start[TS_LINES + 1];

// The code view of forty lines "line NN" opened, at the top, drawn.
static void ts_code_open(struct ui address_to wui, positive address_to length)
{
        static char text[TS_LINES * 8];

        if (ts_view.doc.selections)
                ui_text_close(&ts_view);
        *length = 0;
        for (int n = 0; n < TS_LINES; n++)
        {
                ts_line_start[n] = (p64)*length;
                text[(*length)++] = 'l', text[(*length)++] = 'i', text[(*length)++] = 'n', text[(*length)++] = 'e', text[(*length)++] = ' ';
                text[(*length)++] = (char)('0' + n / 10), text[(*length)++] = (char)('0' + n % 10), text[(*length)++] = '\n';
        }
        ts_line_start[TS_LINES] = (p64)*length;
        ui_text_open(&ts_view, UI_TEXT_CODE, (const p8 address_to)text, *length, ui_text_keymap());
        ts_view.own = &ts_own, ts_view.focused = true;
        ts_draw(wui, &ts_view);
}

// A code view of one word of 26 letters and two short lines, at the top, drawn: a word wide enough that a finger in its middle is
// far from both its handles.
static void ts_long_open(struct ui address_to wui)
{
        if (ts_view.doc.selections)
                ui_text_close(&ts_view);
        ui_text_open(&ts_view, UI_TEXT_CODE, (const p8 address_to) "abcdefghijklmnopqrstuvwxyz\nsecond line\nthird\n", 45, ui_text_keymap());
        ts_view.own = &ts_own, ts_view.focused = true;
        ts_draw(wui, &ts_view);
}

// The view's fourth line's word selected by a long press, its end's knob in kx, ky and its start's in sx, sy.
static void ts_word(struct ui address_to wui, int address_to sx, int address_to sy, int address_to kx, int address_to ky)
{
        ts_select(wui, &ts_view, ts_col(wui, &ts_view, 2), ts_row(&ts_view, ts_view.top_line + 3));
        ts_knob(wui, &ts_view, ts_line_start[ts_view.top_line + 3], sx, sy);
        ts_knob(wui, &ts_view, ts_line_start[ts_view.top_line + 3] + 4, kx, ky);
}

// The code view at a scale. The bits of TS_* it answers true for are set in the return.
static p64 ts_code(struct ui address_to wui, int scale)
{
        positive length = 0, now = 5000000000ull;
        p64 got = 0, start, finish;
        int kx, ky, sx, sy, row, x, lh, half;

        wui->scale = scale;
        ui_kit.motion = 1;
        ts_top = 40;
        half = ui_dev(wui, UI_TARGET) / 2;

        /* A long press on the fourth line selects its word, and shows a handle at each end. */
        ts_code_open(wui, &length);
        lh = ts_view.line_height;
        row = ts_row(&ts_view, 3), x = ts_col(wui, &ts_view, 2);
        ts_hold(wui, &ts_view, x, row);
        got |= (p64)ts_is(&ts_view, ts_line_start[3], ts_line_start[3] + 4) << TS_LONG_PRESS;
        ts_lift(wui, &ts_view, x, row);
        ts_draw(wui, &ts_view);
        if (ts_knob(wui, &ts_view, ts_line_start[3], &sx, &sy) && ts_knob(wui, &ts_view, ts_line_start[3] + 4, &kx, &ky))
                got |= (p64)(ts_accent(wui, sx, sy) && ts_accent(wui, kx, ky) && ts_view.sel.handles) << TS_HANDLES;

        /* A finger a little less than half a target from a knob takes it, to the right, below and above; a little more does not. */
        {
                static const int dx[5] = {1, 0, 1, 1, 0}, dy[5] = {0, 1, 1, -1, -1};
                int in = 0, out = 0;

                for (int k = 0; k < 5; k++)
                {
                        ts_code_open(wui, &length);
                        ts_word(wui, &sx, &sy, &kx, &ky);
                        ts_down(wui, &ts_view, kx + dx[k] * (half - 2), ky + dy[k] * (half - 2));
                        in += ts_view.sel.finger == UI_SELECT_FINGER_HANDLE;
                        ts_up(wui, &ts_view, kx, ky);
                        ts_code_open(wui, &length);
                        ts_word(wui, &sx, &sy, &kx, &ky);
                        ts_down(wui, &ts_view, kx + dx[k] * (half + 3), ky + dy[k] * (half + 3));
                        out += ts_view.sel.finger == UI_SELECT_FINGER_HANDLE;
                        ts_up(wui, &ts_view, kx, ky);
                }
                got |= (p64)(in == 5) << TS_GRAB_IN;
                got |= (p64)(out == 0) << TS_GRAB_OUT;
        }

        /* The end's handle, taken on its knob and moved to the ninth line's fourth column: the end is there, and the start has not moved. The
              finger holds the knob, under the line, and the end is on the line the handle was on and not the line under the knob. */
        ts_code_open(wui, &length);
        ts_word(wui, &sx, &sy, &kx, &ky);
        ts_down(wui, &ts_view, kx, ky);
        ts_move(wui, &ts_view, kx + (ts_col(wui, &ts_view, 3) - ts_col(wui, &ts_view, 4)), ky + 5 * lh);
        ts_up(wui, &ts_view, kx, ky + 5 * lh);
        got |= (p64)ts_is(&ts_view, ts_line_start[3], ts_line_start[8] + 3) << TS_ACROSS_LINES;
        got |= (p64)(doc_sel_start(&ts_view.doc.selections[ts_view.doc.primary]) == ts_line_start[3]) << TS_KEEPS_START;
        got |= (p64)ts_view.sel.bar << TS_HANDLE_BAR; // the lift of a dragged handle brings the bar back

        /* The start's handle taken and moved past the end: the ends cross. */
        ts_code_open(wui, &length);
        ts_word(wui, &sx, &sy, &kx, &ky);
        ts_down(wui, &ts_view, sx, sy);
        ts_move(wui, &ts_view, sx + (ts_col(wui, &ts_view, 2) - ts_col(wui, &ts_view, 0)), sy + 9 * lh);
        ts_up(wui, &ts_view, sx, sy + 9 * lh);
        got |= (p64)ts_is(&ts_view, ts_line_start[3] + 4, ts_line_start[12] + 2) << TS_CROSS;

        /* The end's handle held in the band at the box's bottom: the view scrolls, by the kit's speed for the scale, and the end follows to
              the end of the text. */
        ts_code_open(wui, &length);
        ts_word(wui, &sx, &sy, &kx, &ky);
        ts_down(wui, &ts_view, kx, ky);
        ts_move(wui, &ts_view, kx, ts_view.bottom - 2);
        {
                long moved, past = (ts_view.bottom - 2) - (ts_view.bottom - ui_dev(wui, UI_TARGET) / 2) + 1, want = ui_edge_speed(past, scale) * 496 / 1000 / lh;

                ts_ticks(wui, &ts_view, now, 31); // 496 ms
                moved = (long)ts_view.top_line;
                got |= (p64)(moved > 0 && moved >= want - 2 && moved <= want + 2) << TS_SPEED;
                ts_ticks(wui, &ts_view, now + 600000000ull, 400);
                finish = doc_sel_end(&ts_view.doc.selections[ts_view.doc.primary]);
                got |= (p64)(finish == (p64)length && ts_view.top_line > 0) << TS_TO_END;
        }
        ts_up(wui, &ts_view, kx, ts_view.bottom - 2);

        /* The start's handle, on a word far down, held in the band at the top: the view scrolls up and the start follows to the start. */
        ts_code_open(wui, &length);
        ts_view.top_line = 15;
        ts_draw(wui, &ts_view);
        ts_word(wui, &sx, &sy, &kx, &ky);
        ts_down(wui, &ts_view, sx, sy);
        ts_move(wui, &ts_view, sx, ts_view.top + 2);
        ts_ticks(wui, &ts_view, now + 20000000000ull, 400);
        got |= (p64)(doc_sel_start(&ts_view.doc.selections[ts_view.doc.primary]) == 0 && ts_view.top_line == 0) << TS_TO_START;
        ts_up(wui, &ts_view, sx, ts_view.top + 2);

        /* The bar. After a long press and its lift it is wanted and, drawn, it is over the selection, inside the window, and has the editable
              view's four items. */
        ts_code_open(wui, &length);
        row = ts_row(&ts_view, 6), x = ts_col(wui, &ts_view, 2);
        ts_select(wui, &ts_view, x, row);
        got |= (p64)(ts_view.sel.bar_up && ts_ids(&ts_view, UI_SELECT_CUT, UI_SELECT_COPY, UI_SELECT_PASTE, UI_SELECT_ALL)) << TS_BAR_ITEMS;
        got |= (p64)(ts_view.sel.bar_up && ts_view.sel.bar_y + ts_view.sel.bar_h <= row - lh / 2) << TS_BAR_ABOVE;
        got |= (p64)(ts_view.sel.bar_up && ts_view.sel.bar_x >= 0 && ts_view.sel.bar_y >= ui_dev(wui, UI_SELECT_TITLEBAR) &&
                                  ts_view.sel.bar_x + ts_view.sel.bar_w <= TS_WIDTH && ts_view.sel.bar_y + ts_view.sel.bar_h <= TS_HEIGHT) << TS_BAR_INSIDE;
        /* A tap on an item of the bar does it: Select All selects the text and leaves the bar up. */
        ts_tap(wui, &ts_view, ts_view.sel.bar_x + (ts_view.sel.bar_at[3] + ts_view.sel.bar_at[4]) / 2, ts_view.sel.bar_y + ts_view.sel.bar_h / 2);
        ts_draw(wui, &ts_view);
        got |= (p64)(ts_is(&ts_view, 0, (p64)length) && ts_view.sel.bar && ts_view.sel.bar_up) << TS_BAR_SELECT_ALL;
        /* Copy takes the bar away and leaves the selection; Cut takes the text out, and the handles and the bar with it. */
        ts_code_open(wui, &length);
        ts_select(wui, &ts_view, x, row);
        ts_tap(wui, &ts_view, ts_view.sel.bar_x + (ts_view.sel.bar_at[1] + ts_view.sel.bar_at[2]) / 2, ts_view.sel.bar_y + ts_view.sel.bar_h / 2);
        got |= (p64)(ts_is(&ts_view, ts_line_start[6], ts_line_start[6] + 4) && !ts_view.sel.bar && ts_view.sel.handles) << TS_BAR_COPY;
        ts_code_open(wui, &length);
        ts_select(wui, &ts_view, x, row);
        ts_tap(wui, &ts_view, ts_view.sel.bar_x + (ts_view.sel.bar_at[0] + ts_view.sel.bar_at[1]) / 2, ts_view.sel.bar_y + ts_view.sel.bar_h / 2);
        got |= (p64)(doc_length(&ts_view.doc) == length - 4 && !ts_view.sel.bar && !ts_view.sel.handles) << TS_BAR_CUT;

        /* A selection on the first row, with the box at the window's top, has no room above for the bar: it is below the handles. */
        ts_top = 0;
        ts_code_open(wui, &length);
        ts_select(wui, &ts_view, ts_col(wui, &ts_view, 2), ts_row(&ts_view, 0));
        got |= (p64)(ts_view.sel.bar_up && ts_view.sel.bar_y >= ts_view.top + lh + 2 * ui_dev(wui, UI_SELECT_HANDLE)) << TS_BAR_BELOW;
        ts_top = 40;

        /* A tap on the selection takes the bar away and a second brings it back; a tap on a handle does the same, and a tap elsewhere
              clears the selection and its handles and bar. The word is one of 26 letters, so that a finger in its middle is on no handle. */
        ts_long_open(wui);
        row = ts_row(&ts_view, 0);
        ts_select(wui, &ts_view, ts_col(wui, &ts_view, 13), row);
        ts_tap(wui, &ts_view, ts_col(wui, &ts_view, 13), row);
        ts_draw(wui, &ts_view);
        {
                b32 gone = !ts_view.sel.bar && !ts_view.sel.bar_up && ts_is(&ts_view, 0, 26);

                ts_tap(wui, &ts_view, ts_col(wui, &ts_view, 13), row);
                ts_draw(wui, &ts_view);
                got |= (p64)(gone && ts_view.sel.bar && ts_view.sel.bar_up) << TS_TOGGLE_SELECTION;
        }
        ts_view.sel.bar = false;
        ts_draw(wui, &ts_view);
        ts_knob(wui, &ts_view, 26, &kx, &ky);
        ts_tap(wui, &ts_view, kx, ky);
        ts_draw(wui, &ts_view);
        got |= (p64)(ts_view.sel.bar && ts_view.sel.bar_up && ts_is(&ts_view, 0, 26)) << TS_TOGGLE_HANDLE;
        ts_tap(wui, &ts_view, ts_col(wui, &ts_view, 5), ts_row(&ts_view, 1));
        ts_draw(wui, &ts_view);
        got |= (p64)(ts_is(&ts_view, 27 + 5, 27 + 5) && !ts_view.sel.handles && !ts_view.sel.bar && !ts_view.sel.bar_up) << TS_TAP_CLEARS;

        /* A finger that drags the text still scrolls it, with the selection and its handles where they were, and the bar not shown while
              it moves; thrown and settled, the bar comes back, and the loop is told to draw it. */
        ts_code_open(wui, &length);
        ts_view.top_line = 20;
        ts_draw(wui, &ts_view);
        ts_select(wui, &ts_view, ts_col(wui, &ts_view, 2), ts_row(&ts_view, 23));
        {
                int fx = ts_col(wui, &ts_view, 6), fy = ts_view.top + 10 * lh + lh / 2;
                b32 redraw = false;

                ts_reset(&ts_view);
                ts_down(wui, &ts_view, fx, fy);
                // Five lines: the selection is still in the view, so the bar's absence is the finger's and not the selection's leaving it.
                for (int step = 1; step <= 10; step++)
                        ts_move(wui, &ts_view, fx, fy + step * lh / 2);
                ts_draw(wui, &ts_view);
                got |= (p64)(ts_view.top_line < 20 && ts_is(&ts_view, ts_line_start[23], ts_line_start[23] + 4) && ts_view.sel.handles) << TS_SCROLLS;
                got |= (p64)(ts_view.sel.bar && !ts_view.sel.bar_up) << TS_BAR_HIDES;
                ts_up(wui, &ts_view, fx, fy + 5 * lh);
                for (int step = 1; step < 600 && ui_body_going(&ts_view.sel.fling); step++)
                        (ui_kit.now = ui_kit.test_clock = (b64)(clock_monotonic_nanoseconds() + (positive)step * 16000000ull), ui_select_fling(wui, ui_text_sel(&ts_view)));
                (ui_kit.now = ui_kit.test_clock = (b64)(clock_monotonic_nanoseconds() + 1000000000ull), ui_select_bar_tick(ui_text_sel(&ts_view), &redraw));
                ts_view.top_line = 20;
                ts_draw(wui, &ts_view);
                got |= (p64)(redraw && ts_view.sel.bar_up) << TS_BAR_RETURNS;
        }

        /* A second tap on the same place is a double tap, a word; a third, the line. */
        ts_long_open(wui);
        ts_reset(&ts_view);
        x = ts_col(wui, &ts_view, 13), row = ts_row(&ts_view, 0);
        ts_down(wui, &ts_view, x, row);
        ts_up(wui, &ts_view, x, row);
        ts_down(wui, &ts_view, x, row);
        ts_up(wui, &ts_view, x, row);
        got |= (p64)(ts_is(&ts_view, 0, 26) && ts_view.sel.handles && ts_view.sel.bar) << TS_DOUBLE_TAP;
        ts_down(wui, &ts_view, x, row);
        ts_up(wui, &ts_view, x, row);
        got |= (p64)ts_is(&ts_view, 0, 27) << TS_TRIPLE_TAP;

        /* A mouse is as it was: a click and a drag select, with no handles, no bar and no overlay drawn. */
        ts_code_open(wui, &length);
        ts_event(wui, &ts_view, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, ts_col(wui, &ts_view, 1), ts_row(&ts_view, 4));
        ts_event(wui, &ts_view, WINDOW_EVENT_MOTION, 0, 0, ts_col(wui, &ts_view, 5), ts_row(&ts_view, 6));
        ts_event(wui, &ts_view, WINDOW_EVENT_BUTTON, 0, 0x110, ts_col(wui, &ts_view, 5), ts_row(&ts_view, 6));
        ts_draw(wui, &ts_view);
        got |= (p64)(ts_is(&ts_view, ts_line_start[4] + 1, ts_line_start[6] + 5) && !ts_view.sel.handles && !ts_view.sel.bar && !ts_view.sel.bar_up &&
                                  !ts_view.sel.over_bottom) << TS_MOUSE;

        /* The bar fades in with full motion (not whole at its first frame, and the loop is asked for the next); with reduced motion it is
              whole at once and asks for no frame; and a settled bar asks the loop for nothing, a widget without the keys neither blinks. */
        ts_code_open(wui, &length);
        ts_view.focused = false;
        ts_select(wui, &ts_view, ts_col(wui, &ts_view, 2), ts_row(&ts_view, 6));
        ui_kit.motion = 0;
        ts_view.sel.quiet_at = 0;
        ts_view.sel.bar_up = false;
        ui_kit.busy = 0;
        ts_draw(wui, &ts_view);
        {
                int px = ts_view.sel.bar_x + 6, py = ts_view.sel.bar_y + ts_view.sel.bar_h / 2;
                unsigned int first = wui->pixels[(positive)py * TS_WIDTH + (positive)px];
                b32 busy = ui_kit.busy, redraw = false;

                ts_view.sel.bar_since = clock_monotonic_nanoseconds() - 2 * UI_SELECT_BAR_FADE_NS;
                ui_kit.busy = 0;
                ts_draw(wui, &ts_view);
                got |= (p64)(busy && ts_view.sel.bar_up && !ui_kit.busy && first != wui->pixels[(positive)py * TS_WIDTH + (positive)px]) << TS_FADE;
                got |= (p64)((ui_kit.now = ui_kit.test_clock = (b64)(clock_monotonic_nanoseconds() + 1000000000ull), ui_text_timeout(wui, &ts_view, -1, &redraw)) == -1 && !redraw) << TS_IDLE;
                ui_kit.motion = 1;
                ts_view.sel.bar_up = false;
                ui_kit.busy = 0;
                ts_draw(wui, &ts_view);
                got |= (p64)(ts_view.sel.bar_up && !ui_kit.busy) << TS_REDUCED;
        }
        ui_text_close(&ts_view);
        return got;
}

// A field, a masked field and a read only view: what the bar offers each, and a handle moved across words and to the ends of a field.
static p64 ts_fields(struct ui address_to wui, int scale)
{
        const char address_to words = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega";
        p64 got = 0, length = (p64)string_length((string_address)words);
        positive now = 5000000000ull;
        int kx, ky, y, hx, top;

        wui->scale = scale;
        ui_kit.motion = 1;
        ts_top = 40;
        ui_field_open(&ts_view, &ts_own, words);
        doc_select_set(&ts_view.doc, 0, 0);
        ts_draw(wui, &ts_view);
        y = ts_view.top + ts_view.line_height / 2;

        /* A long press on "beta" selects it and shows the field's bar; its end's handle taken and moved on to the end of "delta" extends it
              across words. */
        ts_select(wui, &ts_view, field_x(wui, &ts_view, 7), y);
        got |= (p64)(ts_view.sel.bar_up && ts_ids(&ts_view, UI_SELECT_CUT, UI_SELECT_COPY, UI_SELECT_PASTE, UI_SELECT_ALL)) << TS_FIELD_ITEMS;
        ts_knob(wui, &ts_view, 10, &kx, &ky);
        ts_reset(&ts_view);
        ts_down(wui, &ts_view, kx, ky);
        ui_text_where(wui, &ts_view, 22, &hx, &top);
        ts_move(wui, &ts_view, hx, ky);
        ts_up(wui, &ts_view, hx, ky);
        got |= (p64)ts_is(&ts_view, 6, 22) << TS_FIELD_ACROSS;

        /* Held in the band at the right edge, the field scrolls and the end follows to the text's end. */
        ts_draw(wui, &ts_view);
        ts_knob(wui, &ts_view, 22, &kx, &ky);
        ts_reset(&ts_view);
        ts_down(wui, &ts_view, kx, ky);
        ts_move(wui, &ts_view, ts_view.right - 2, ky);
        ts_ticks(wui, &ts_view, now, 500);
        got |= (p64)(ts_is(&ts_view, 6, length) && ts_view.scroll_x > 0) << TS_FIELD_TO_END;
        ts_up(wui, &ts_view, ts_view.right - 2, ky);
        ui_text_close(&ts_view);

        /* And the start's handle of a selection far along, held in the band at the left, takes the start back to the text's start. */
        ui_field_open(&ts_view, &ts_own, words);
        doc_select_set(&ts_view.doc, 90, 100);
        ts_view.sel.handles = true;
        ts_draw(wui, &ts_view);
        if (ts_knob(wui, &ts_view, 90, &kx, &ky))
        {
                ts_reset(&ts_view);
                ts_down(wui, &ts_view, kx, ky);
                ts_move(wui, &ts_view, ts_view.left, ky);
                ts_ticks(wui, &ts_view, now + 20000000000ull, 600);
                got |= (p64)(doc_sel_start(&ts_view.doc.selections[ts_view.doc.primary]) == 0) << TS_FIELD_TO_START;
                ts_up(wui, &ts_view, ts_view.left, ky);
        }
        ui_text_close(&ts_view);

        /* A masked field: a long press takes all of it, and the bar has Paste and nothing that would copy it out. */
        ui_field_open(&ts_view, &ts_own, "hunter2");
        ts_view.masked = true;
        ts_draw(wui, &ts_view);
        ts_select(wui, &ts_view, ts_view.left + ts_view.cell * 2, ts_view.top + ts_view.line_height / 2);
        got |= (p64)(ts_is(&ts_view, 0, 7) && ts_view.sel.bar_up && ts_ids(&ts_view, UI_SELECT_PASTE, 0, 0, 0)) << TS_MASKED;
        ui_text_close(&ts_view);

        /* A read only view has Copy and Select All, and no Cut or Paste. */
        ui_text_open(&ts_view, UI_TEXT_CODE, (const p8 address_to) "one two three\nfour five six\n", 28, ui_text_keymap());
        ts_view.own = &ts_own, ts_view.focused = true, ts_view.read_only = true;
        ts_draw(wui, &ts_view);
        ts_select(wui, &ts_view, ts_col(wui, &ts_view, 5), ts_row(&ts_view, 0));
        got |= (p64)(ts_view.sel.bar_up && ts_ids(&ts_view, UI_SELECT_COPY, UI_SELECT_ALL, 0, 0)) << TS_READ_ONLY;
        ui_text_close(&ts_view);

        /* A caret has Paste and Select All, and an empty field Paste alone. */
        ui_text_open(&ts_view, UI_TEXT_CODE, (const p8 address_to) "ab\n", 3, ui_text_keymap());
        ts_view.own = &ts_own, ts_view.focused = true;
        ts_draw(wui, &ts_view);
        ts_tap(wui, &ts_view, ts_col(wui, &ts_view, 1), ts_row(&ts_view, 0));
        ts_view.sel.bar = true;
        ts_draw(wui, &ts_view);
        got |= (p64)(ts_view.sel.bar_up && ts_ids(&ts_view, UI_SELECT_PASTE, UI_SELECT_ALL, 0, 0)) << TS_CARET_BAR;
        ui_text_close(&ts_view);
        ui_field_open(&ts_view, &ts_own, "");
        ts_draw(wui, &ts_view);
        ts_select(wui, &ts_view, ts_view.left + 2, ts_view.top + ts_view.line_height / 2);
        got |= (p64)(ts_view.sel.bar_up && ts_ids(&ts_view, UI_SELECT_PASTE, 0, 0, 0)) << TS_EMPTY_BAR;
        ui_text_close(&ts_view);
        return got;
}

// A code view that wraps and one with a closed fold: the row under a finger is the row it is drawn at, and the handle is at it.
static p64 ts_rows(struct ui address_to wui, int scale)
{
        static char text[60 * 4 + 16];
        positive length = 0;
        p64 got = 0;
        int lh;

        wui->scale = scale;
        ui_kit.motion = 1;
        ts_top = 40;
        for (int n = 0; n < 60; n++)
                text[length++] = 'w', text[length++] = (char)('0' + n / 10), text[length++] = (char)('0' + n % 10), text[length++] = ' ';
        text[length++] = '\n', text[length++] = 'o', text[length++] = 'n', text[length++] = 'e', text[length++] = '\n';
        ui_text_open(&ts_view, UI_TEXT_CODE, (const p8 address_to)text, length, ui_text_keymap());
        ts_view.own = &ts_own, ts_view.focused = true, ts_view.wrap = true;
        ts_draw(wui, &ts_view);
        lh = ts_view.line_height;
        /* The first line wraps into rows, and "one", the second line, is under the last of them. A long press on the second row's first word
              selects a word of the first line (a w and two digits) and its handle is drawn on that row. */
        ts_select(wui, &ts_view, ts_view.left + ui_dev(wui, 2), ts_view.top + lh + lh / 2);
        {
                int x, top, y;
                p64 start = doc_sel_start(&ts_view.doc.selections[ts_view.doc.primary]);


                got |= (p64)(start > 4 * 8 && start < 60 * 4 && ts_view.sel.handles && !ui_text_where(wui, &ts_view, start, &x, &top) &&
                                          top == ts_view.top + lh) << TS_WRAP_HIT;
                y = top + lh + ui_dev(wui, UI_SELECT_HANDLE);
                got |= (p64)ts_accent(wui, x, y) << TS_WRAP_HANDLE;
        }
        ui_text_close(&ts_view);

        /* A fold hides the lines under its first: the view's rows are "a" (the fold closed), "d" and "e", and its second row is "d". */
        ui_text_open(&ts_view, UI_TEXT_CODE, (const p8 address_to) "a\n  b\n  c\nd\ne\n", 14, ui_text_keymap());
        ts_view.own = &ts_own, ts_view.focused = true;
        doc_fold(&ts_view.doc, 0, true);
        ts_draw(wui, &ts_view);
        lh = ts_view.line_height;
        got |= (p64)(ui_text_hit(wui, &ts_view, ts_view.left + ui_dev(wui, 2), ts_view.top + lh + lh / 2) == 10) << TS_FOLD_HIT;
        ui_text_close(&ts_view);
        return got;
}

static fn touch_select_check(struct ui address_to wui)
{
        static const int scales[6] = {50, 75, 100, 125, 150, 200};
        struct ui saved = *wui;
        b32 motion = ui_kit.motion;
        p64 all = ~0ull;
        long pixels = ui_map(TS_WIDTH * TS_HEIGHT * 4);

        ts_own.table = windows_scratch_table(&ts_table_scratch), ts_own.row = 0, ts_own.door = -1;
        ts_pixels = (unsigned int address_to)pixels;
        for (int k = 0; k < 6; k++)
                        all &= ts_code(wui, scales[k]) | ts_fields(wui, scales[k]) | ts_rows(wui, scales[k]);
#define TS(bit, name) check(name, (all >> (bit)) & 1)
        TS(TS_LONG_PRESS, "touch select: a long press selects the word under the finger");
        TS(TS_HANDLES, "touch select: its selection has a handle at each end, a knob in the accent under the line");
        TS(TS_GRAB_IN, "touch select: a finger within half a finger's target of a knob takes the handle (44 logical pixels each way)");
        TS(TS_GRAB_OUT, "touch select: a finger beyond that takes no handle");
        TS(TS_ACROSS_LINES, "touch select: a handle held by its knob goes across lines to where it is moved, on the line the handle was on and not the line under the knob");
        TS(TS_KEEPS_START, "touch select: the other end stays where it was");
        TS(TS_CROSS, "touch select: the start's handle moved past the end crosses it");
        TS(TS_TO_END, "touch select: the end's handle held in the band at the bottom scrolls the view and takes the end to the end of the text");
        TS(TS_TO_START, "touch select: the start's handle held in the band at the top takes the start to the start of the text");
        TS(TS_SPEED, "touch select: the view scrolls at the kit's speed for the scale (ui_edge_speed)");
        TS(TS_BAR_ITEMS, "touch select: after a long press and its lift the bar has Cut, Copy, Paste and Select All");
        TS(TS_BAR_ABOVE, "touch select: the bar is over the selection");
        TS(TS_BAR_BELOW, "touch select: and under it, past the handles, where there is no room above");
        TS(TS_BAR_INSIDE, "touch select: and inside the window, below its top and in from its edges");
        TS(TS_BAR_SELECT_ALL, "touch select: a tap on the bar's Select All selects the text and leaves the bar up");
        TS(TS_BAR_CUT, "touch select: a tap on Cut takes the selection out, and the handles and the bar away");
        TS(TS_BAR_COPY, "touch select: a tap on Copy takes the bar away and leaves the selection and its handles");
        TS(TS_TOGGLE_SELECTION, "touch select: a tap on the selection takes the bar away, and a second brings it back");
        TS(TS_TOGGLE_HANDLE, "touch select: a tap on a handle shows the bar");
        TS(TS_HANDLE_BAR, "touch select: a handle dragged brings the bar back at its lift");
        TS(TS_TAP_CLEARS, "touch select: a tap elsewhere puts the caret there and takes the handles and the bar away");
        TS(TS_SCROLLS, "touch select: a finger that drags the text still scrolls it, the selection and its handles staying");
        TS(TS_BAR_HIDES, "touch select: the bar is not shown while the text moves under a finger");
        TS(TS_BAR_RETURNS, "touch select: and it comes back, the loop told to draw it, when the view is still");
        TS(TS_DOUBLE_TAP, "touch select: a double tap selects the word under it, with handles and the bar");
        TS(TS_TRIPLE_TAP, "touch select: a triple tap selects the line");
        TS(TS_MOUSE, "touch select: a mouse's click and drag select as they did, with no handles, no bar and no overlay");
        TS(TS_FADE, "touch select: the bar fades in with full motion, and asks for the next frame while it does");
        TS(TS_REDUCED, "touch select: with reduced motion the bar is whole at once and asks for no frame");
        TS(TS_IDLE, "touch select: a settled bar and handles ask the loop for nothing, an idle window wakes nothing");
        TS(TS_FIELD_ITEMS, "touch select: a field's bar has Cut, Copy, Paste and Select All, shown after a long press");
        TS(TS_FIELD_ACROSS, "touch select: a field's handle moved on goes across words");
        TS(TS_FIELD_TO_END, "touch select: held in the band at a field's right edge, the field scrolls and the end goes to the text's end");
        TS(TS_FIELD_TO_START, "touch select: held in the band at its left edge, the start goes to the text's start");
        TS(TS_MASKED, "touch select: a masked field takes all of it, and its bar has Paste and no Cut or Copy");
        TS(TS_READ_ONLY, "touch select: a read only view's bar has Copy and Select All, and no Cut or Paste");
        TS(TS_CARET_BAR, "touch select: a caret's bar has Paste and Select All");
        TS(TS_EMPTY_BAR, "touch select: an empty field's bar has Paste alone");
        TS(TS_WRAP_HIT, "touch select: in a wrapped line the row under the finger is the row it is drawn at");
        TS(TS_WRAP_HANDLE, "touch select: and its handle is drawn on that row");
        TS(TS_FOLD_HIT, "touch select: with a fold closed, the row under the finger is the line it is drawn as");
#undef TS
        check("touch select: the bar keeps below the compositor's top of a window, where its press is the window's own", UI_SELECT_TITLEBAR == COMPOSITOR_GRAB);
        *wui = saved;
        ui_kit.motion = motion;
        compositor_call(syscall(munmap), (positive)pixels, TS_WIDTH * TS_HEIGHT * 4, 0, 0, 0, 0);
}

static fn field_check(struct ui address_to wui)
{
        static struct ui_text t;
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        static struct window_row own;
        long pixels = ui_map(400 * 100 * 4);
        const struct window_event up = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, FIELD_KEY_UP, 0};

        own.table = table_at, own.row = 0, own.door = -1;
        ui_begin(wui, (unsigned int address_to)pixels, 400, 400, 100);

        // Blanks and characters, anywhere.
        ui_field_open(&t, &own, "");
        check("field: a blank typed first is typed, as every blank is", (field_type(wui, &t, " a b ") & UI_TEXT_CHANGED) && field_is(&t, " a b "));
        ui_text_set(&t, (const p8 address_to) "hello world", 11);
        field_key(wui, &t, FIELD_KEY_HOME, 0, 0);
        field_key(wui, &t, FIELD_KEY_RIGHT, 0, 0);
        field_key(wui, &t, FIELD_KEY_RIGHT, 0, 0);
        check("field: Home goes to the start and Right a character on", field_caret(&t) == 2);
        field_key(wui, &t, FIELD_KEY_END, 0, 0);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, 0);
        check("field: End goes to the end and Left a character back", field_caret(&t) == 10);
        field_key(wui, &t, FIELD_KEY_END, 0, 0);
        for (int k = 0; k < 5; k++)
                field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_SHIFT);
        check("field: Shift and Left select back from the caret", field_selected(&t, 6, 11));
        field_type(wui, &t, "there");
        check("field: typing replaces what is selected", field_is(&t, "hello there") && field_caret(&t) == 11);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL);
        check("field: Control and Left go to the start of the word", field_caret(&t) == 6);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL);
        field_key(wui, &t, FIELD_KEY_RIGHT, 0, WINDOW_KEY_CONTROL);
        check("field: Control and Right go to the end of the word", field_caret(&t) == 5);
        field_key(wui, &t, FIELD_KEY_RIGHT, 0, WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT);
        check("field: Control, Shift and Right select a word on", doc_sel_start(&t.doc.selections[0]) == 5 && field_caret(&t) == 11);
        field_key(wui, &t, FIELD_KEY_END, 0, 0);
        field_key(wui, &t, FIELD_KEY_BACKSPACE, 0, WINDOW_KEY_CONTROL);
        check("field: Control-Backspace takes the word before the caret", field_is(&t, "hello "));
        field_key(wui, &t, FIELD_KEY_HOME, 0, 0);
        field_key(wui, &t, FIELD_KEY_DELETE, 0, WINDOW_KEY_CONTROL);
        check("field: Control-Delete takes the word after it", field_is(&t, " "));
        field_key(wui, &t, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
        check("field: Control-Z undoes the last edit", field_is(&t, "hello "));
        field_key(wui, &t, 21, 25, WINDOW_KEY_CONTROL);
        check("field: Control-Y does it again", field_is(&t, " "));
        field_key(wui, &t, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        check("field: Control-A selects it all", field_selected(&t, 0, 1));

        // UTF-8: the caret steps over whole characters, and Backspace takes a whole one.
        ui_text_set(&t, (const p8 address_to) "", 0);
        field_type(wui, &t, "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80" "b");
        field_key(wui, &t, FIELD_KEY_LEFT, 0, 0);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, 0);
        check("field: Left steps over a character of four bytes whole", field_caret(&t) == 6);
        field_key(wui, &t, FIELD_KEY_BACKSPACE, 0, 0);
        check("field: Backspace takes a character of three bytes whole", field_is(&t, "a\xc3\xa9\xf0\x9f\x98\x80" "b") && field_caret(&t) == 3);

        // A cluster is walked back from its end: a run of marks longer than a kilobyte is one cluster, to its base.
        {
                static p8 marks[1 + 600 * 2];
                positive length = 0;

                marks[length++] = 'a';
                for (int k = 0; k < 600; k++)
                        marks[length++] = 0xcc, marks[length++] = 0x81;
                ui_text_set(&t, marks, length);
                field_key(wui, &t, FIELD_KEY_LEFT, 0, 0);
                check("field: Left over a run of combining marks longer than a kilobyte goes to where its cluster starts",
                      field_caret(&t) == 0);
                field_key(wui, &t, FIELD_KEY_HOME, 0, 0);
                field_key(wui, &t, FIELD_KEY_RIGHT, 0, 0);
                check("field: and Right over the same run goes to its end", field_caret(&t) == (p64)length);

                // Three clusters, two of them long: the walk back from each one's end lands on the end before it.
                {
                        static p8 runs[1 + 600 * 2 + 1 + 600 * 2 + 1];
                        positive count = 0;
                        b32 walked = true;

                        runs[count++] = 'a';
                        for (int k = 0; k < 600; k++)
                                runs[count++] = 0xcc, runs[count++] = 0x81;
                        runs[count++] = 'b';
                        for (int k = 0; k < 600; k++)
                                runs[count++] = 0xcc, runs[count++] = 0x81;
                        runs[count++] = 'c';
                        ui_text_set(&t, runs, count);
                        for (p64 begin = 0, finish; begin < (p64)count; begin = finish)
                        {
                                finish = doc_cluster_end(&t.doc, begin);
                                walked &= doc_cluster_start(&t.doc, finish) == begin;
                        }
                        check("field: every cluster of a text with two long runs is where the walk back from its end lands", walked);
                }
        }
        {
                static const struct
                {
                        const char address_to text;
                        positive length, left;
                } clusters[] = {
                    {"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7", 18, 0}, // a family
                    {"\xf0\x9f\x87\xba\xf0\x9f\x87\xb8\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa", 16, 8},         // two flags
                    {"1\xef\xb8\x8f\xe2\x83\xa3", 7, 0},                                                 // a keycap
                    {"\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8", 9, 0},                                      // Hangul jamo
                    {"\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7", 9, 0},                                      // a conjunct
                    {"x\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7", 10, 1},                                    // a conjunct after x
                };
                b32 all = true;

                for (positive k = 0; k < sizeof(clusters) / sizeof(clusters[0]); k++)
                {
                        ui_text_set(&t, (const p8 address_to)clusters[k].text, clusters[k].length);
                        field_key(wui, &t, FIELD_KEY_LEFT, 0, 0);
                        all &= field_caret(&t) == clusters[k].left;
                }
                check("field: Left from the end of a cluster of several code points goes to where it starts", all);
        }

        // The history and the selections: a typed word over a backwards selection, a double click on a letter of two
        // bytes, the undo of a typing over a selection, the undo and redo of a word's delete, and a paste cut to its room.
        ui_text_set(&t, (const p8 address_to) "abc def", 7);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT);
        field_type(wui, &t, "X");
        check("field: typing over a word selected backwards leaves the caret after it, with no selection",
              field_is(&t, "abc X") && field_caret(&t) == 5 && !doc_any_selected(&t.doc));
        ui_text_set(&t, (const p8 address_to) "h\xc3\xa9llo w\xc3\xb6rld", 13);
        ui_text_draw(wui, &t, 20, 20, 300, 30, 0);
        field_button(wui, &t, 0x110, true, field_x(wui, &t, 8) + 1, t.top + t.line_height / 2, 0);
        field_button(wui, &t, 0x110, false, field_x(wui, &t, 8) + 1, t.top + t.line_height / 2, 0);
        field_button(wui, &t, 0x110, true, field_x(wui, &t, 8) + 1, t.top + t.line_height / 2, 0);
        field_button(wui, &t, 0x110, false, field_x(wui, &t, 8) + 1, t.top + t.line_height / 2, 0);
        check("field: a double click on a letter of two bytes takes its word", field_selected(&t, 7, 13));
        ui_text_set(&t, (const p8 address_to) "abc", 3);
        field_key(wui, &t, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        field_type(wui, &t, "x");
        field_key(wui, &t, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
        check("field: undo of a typing over a selection gives the text back with it selected", field_is(&t, "abc") && field_selected(&t, 0, 3));
        ui_text_set(&t, (const p8 address_to) "abc def", 7);
        field_key(wui, &t, FIELD_KEY_BACKSPACE, 0, WINDOW_KEY_CONTROL);
        field_key(wui, &t, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
        check("field: undo of a word's delete puts the caret back at the end", field_is(&t, "abc def") && field_caret(&t) == 7);
        field_key(wui, &t, 21, 25, WINDOW_KEY_CONTROL);
        check("field: and its redo puts the caret after the word's delete", field_is(&t, "abc ") && field_caret(&t) == 4);
        ui_text_set(&t, (const p8 address_to) "ab", 2);
        t.most = 5, t.most_said = "five";
        ui_text_paste(&t, (const p8 address_to) "\xc3\xa9\xc3\xa9", 4);
        check("field: a paste cut to its room keeps whole characters", field_is(&t, "ab\xc3\xa9"));
        t.most = 0;

        // A composition that shrank draws what it says now: the picture of what it was is the picture of what it is.
        {
                static unsigned int first[400 * 100], second[400 * 100];
                static struct ui_text shrink, fresh;

                ui_field_open(&shrink, &own, "");
                ui_text_preedit(&shrink, (const p8 address_to) "abc", 3);
                ui_text_preedit(&shrink, (const p8 address_to) "ab", 2);
                field_picture(wui, &shrink, first);
                ui_field_open(&fresh, &own, "");
                ui_text_preedit(&fresh, (const p8 address_to) "ab", 2);
                field_picture(wui, &fresh, second);
                check("field: a composition that shrinks draws what it says now", !memory_compare(first, second, sizeof(first)));
                ui_text_preedit_end(&shrink, false);
                ui_text_preedit_end(&fresh, false);
                ui_text_close(&shrink);
                ui_text_close(&fresh);
        }
        // A composition in a masked field is drawn as dots, whatever it says; in a field it is drawn as its text.
        {
                static unsigned int first[400 * 100], second[400 * 100];
                static struct ui_text mask, plain;

                ui_field_open(&mask, &own, "");
                mask.masked = true;
                ui_text_preedit(&mask, (const p8 address_to) "ab", 2);
                field_picture(wui, &mask, first);
                ui_text_preedit(&mask, (const p8 address_to) "mn", 2);
                field_picture(wui, &mask, second);
                check("field: a masked field's composition is drawn as dots, the same whatever it says", !memory_compare(first, second, sizeof(first)));
                ui_text_preedit_end(&mask, false);
                ui_text_close(&mask);

                ui_field_open(&plain, &own, "");
                ui_text_preedit(&plain, (const p8 address_to) "ab", 2);
                field_picture(wui, &plain, first);
                ui_text_preedit(&plain, (const p8 address_to) "mn", 2);
                field_picture(wui, &plain, second);
                check("field: a field's composition is drawn as the text it is", memory_compare(first, second, sizeof(first)) != 0);
                ui_text_preedit_end(&plain, false);
                ui_text_close(&plain);
        }

        // A paste is one line: line ends and tabs are blanks, any other control is left out.
        ui_text_set(&t, (const p8 address_to) "", 0);
        check("field: a paste of lines is one line of blanks for its line ends, with no control in it",
              (ui_text_paste(&t, (const p8 address_to) "one\ntwo\r\nthree\tfour\x1b[0m\x07!", 25) & UI_TEXT_CHANGED) &&
                  field_is(&t, "one two three four[0m!") && doc_line_count(&t.doc) == 1);
        field_key(wui, &t, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
        check("field: and is one undo", field_is(&t, ""));

        // The keys a field does not take are its app's; Enter and Escape are its submit and cancel.
        field_type(wui, &t, "abc");
        field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_SHIFT);
        t.keeps_vertical = true; // an app that takes Up and Down (a list's moves) leaves them to it
        check("field: Up, Down, Page Down, Tab and a chord it has no use for are not the field's",
              !ui_field_event(wui, &t, null, &up, 0, 0) && !field_key(wui, &t, FIELD_KEY_DOWN, 0, 0) && !field_key(wui, &t, 109, 0, 0) &&
                  !field_key(wui, &t, FIELD_KEY_TAB, '\t', 0) && !field_key(wui, &t, 25, 16, WINDOW_KEY_CONTROL) &&
                  !field_key(wui, &t, 31, 19, WINDOW_KEY_CONTROL) && field_is(&t, "abc"));
        check("field: Enter is its submit and Escape its cancel, a selection or none",
              field_key(wui, &t, FIELD_KEY_ENTER, '\r', 0) == UI_TEXT_SUBMIT && field_key(wui, &t, FIELD_KEY_ESCAPE, 27, 0) == UI_TEXT_CANCEL);
        t.focused = false;
        check("field: a field without the keys takes no key", !field_type(wui, &t, "x") && field_is(&t, "abc"));
        t.focused = true;

        // Its rules: a character refused with its words, for a key, a paste and a composition; and its length.
        ui_text_set(&t, (const p8 address_to) "", 0);
        t.accept = ui_field_digits;
        field_type(wui, &t, "12");
        t.refused = null;
        check("field: a character its rule refuses is not typed, and it says why",
              (field_key(wui, &t, 0, 'a', 0) & UI_TEXT_REFUSED) && field_is(&t, "12") && t.refused &&
                  string_equals((string_address)t.refused, (string_address) "a number is digits"));
        field_type(wui, &t, "3");
        check("field: a paste with a character it refuses goes in not at all",
              (ui_text_paste(&t, (const p8 address_to) "4x5", 3) & UI_TEXT_REFUSED) && field_is(&t, "123"));
        ui_text_preedit(&t, (const p8 address_to) "\xc3\xa9", 2);
        check("field: and a composition it refuses is dropped", (ui_text_preedit_end(&t, true) & UI_TEXT_REFUSED) && field_is(&t, "123"));
        t.accept = field_says_five;
        t.most = 5, t.most_said = "five at most";
        ui_text_set(&t, (const p8 address_to) "", 0);
        check("field: past its length a key is refused with its words",
              (field_type(wui, &t, "abcdefg") & UI_TEXT_REFUSED) && field_is(&t, "abcde") &&
                  string_equals((string_address)t.refused, (string_address) "five at most"));
        ui_text_set(&t, (const p8 address_to) "ab", 2);
        check("field: a paste past it is cut to its room", (ui_text_paste(&t, (const p8 address_to) "cdefgh", 6) & UI_TEXT_REFUSED) && field_is(&t, "abcde"));
        field_key(wui, &t, FIELD_KEY_LEFT, 0, 0);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_SHIFT);
        field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_SHIFT);
        check("field: what is selected is room for what replaces it", !(field_type(wui, &t, "X") & UI_TEXT_REFUSED) && field_is(&t, "abXe") &&
                                                                       !(field_type(wui, &t, "Y") & UI_TEXT_REFUSED) && field_is(&t, "abXYe"));
        t.accept = null, t.most = 0;

        // Masked: dots, nothing let out, no words.
        {
                static struct ui_text_layout l;
                unsigned int offered = table_at->clip_offer_serial[0];
                static struct ui_popmenu m;

                ui_text_close(&t);
                ui_field_open(&t, &own, "");
                t.masked = true;
                field_type(wui, &t, "pa ss");
                ui_text_layout_line(wui, &t, 0, &l);
                check("field: a masked field holds what is typed and shows a dot for each character, its blank as well",
                      field_is(&t, "pa ss") && l.count == 5 && l.shown_length == 15 &&
                          !memory_compare(l.shown, "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2", 15));
                field_key(wui, &t, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
                field_key(wui, &t, 46, 3, WINDOW_KEY_CONTROL);
                field_key(wui, &t, 45, 24, WINDOW_KEY_CONTROL);
                check("field: a masked field is never copied or cut out", field_is(&t, "pa ss") && table_at->clip_offer_serial[0] == offered);
                m.count = 0;
                ui_text_menu_items(&m, &t, false);
                check("field: and its menu has Cut and Copy greyed", m.items[3].id == UI_TEXT_MENU_CUT && (m.items[3].flags & UI_MENU_DISABLED) &&
                                                                     m.items[4].id == UI_TEXT_MENU_COPY && (m.items[4].flags & UI_MENU_DISABLED));
                field_key(wui, &t, FIELD_KEY_END, 0, 0);
                field_key(wui, &t, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL);
                check("field: a masked field has no words: Control and Left go to its start", field_caret(&t) == 0);
                field_key(wui, &t, FIELD_KEY_END, 0, 0);
                field_key(wui, &t, FIELD_KEY_BACKSPACE, 0, WINDOW_KEY_CONTROL);
                check("field: and Control-Backspace takes it all", field_is(&t, ""));
                field_type(wui, &t, "pa ss");
                ui_text_close(&t);
                check("field: closed, a masked field keeps nothing", !t.doc.added && !t.preedit_length);
        }

        // The menu: what cannot be done is greyed, and what is chosen is done to the field.
        {
                static struct ui_popmenu m;

                ui_field_open(&t, &own, "one two");
                m.count = 0;
                ui_text_menu_items(&m, &t, false);
                check("field: the menu is Undo, Redo, Cut, Copy, Paste, Delete and Select All, greyed with nothing done or selected",
                      m.count == 9 && (m.items[0].flags & UI_MENU_DISABLED) && (m.items[3].flags & UI_MENU_DISABLED) &&
                          (m.items[4].flags & UI_MENU_DISABLED) && !(m.items[5].flags & UI_MENU_DISABLED) &&
                          (m.items[6].flags & UI_MENU_DISABLED) && m.items[8].id == UI_TEXT_MENU_ALL && !(m.items[8].flags & UI_MENU_DISABLED));
                ui_text_menu_do(wui, &t, UI_TEXT_MENU_ALL);
                check("field: Select All selects it all", field_selected(&t, 0, 7));
                ui_text_menu_do(wui, &t, UI_TEXT_MENU_DELETE);
                check("field: Delete takes what is selected", field_is(&t, ""));
                ui_text_menu_do(wui, &t, UI_TEXT_MENU_UNDO);
                check("field: Undo brings it back", field_is(&t, "one two"));
                ui_text_menu_do(wui, &t, UI_TEXT_MENU_PASTE);
                check("field: Paste asks the clipboard, and the answer is this field's", t.pasting && table_at->clip_ask_serial[0]);
                {
                        struct window_event refused = {WINDOW_EVENT_CLIP, 0, WINDOWS_CLIP_CLIPBOARD, WINDOWS_CLIP_REFUSED};
                        struct window_event ready = {WINDOW_EVENT_CLIP, 0, WINDOWS_CLIP_CLIPBOARD, WINDOWS_CLIP_READY};

                        ui_field_event(wui, &t, null, &refused, 0, 0);
                        check("field: a paste refused is over, and a paste it did not ask for is not its",
                              !t.pasting && !ui_field_event(wui, &t, null, &ready, 0, 0) && field_is(&t, "one two"));
                }
        }

        // The hand, on a field drawn in a well: a click puts the caret, a double click takes a word, a triple the line,
        // a drag selects, Shift extends, the right button asks for the menu, and a press off the well is not the field's.
        {
                int y, beta, gamma;

                ui_text_set(&t, (const p8 address_to) "alpha beta gamma", 16);
                doc_select_set(&t.doc, 0, 0);
                ui_text_draw(wui, &t, 20, 20, 300, 30, 0);
                y = t.top + t.line_height / 2;
                beta = field_x(wui, &t, 6), gamma = field_x(wui, &t, 11);
                check("field: a press off its well commits a plain field, the kit's rule, and its release is not the field's",
                      field_button(wui, &t, 0x110, true, 10, y, 0) == UI_TEXT_COMMIT && !field_button(wui, &t, 0x110, false, 10, y, 0));
                field_button(wui, &t, 0x110, true, beta + 1, y, 0);
                field_button(wui, &t, 0x110, false, beta + 1, y, 0);
                check("field: a click puts the caret where it is", field_selected(&t, 6, 6));
                field_button(wui, &t, 0x110, true, beta + 1, y, 0);
                field_button(wui, &t, 0x110, false, beta + 1, y, 0);
                check("field: a double click takes the word", field_selected(&t, 6, 10));
                field_button(wui, &t, 0x110, true, beta + 1, y, 0);
                field_button(wui, &t, 0x110, false, beta + 1, y, 0);
                check("field: a triple click takes the line", field_selected(&t, 0, 16));
                t.sel.click.at = 0;
                field_button(wui, &t, 0x110, true, field_x(wui, &t, 0) + 1, y, 0);
                field_motion(wui, &t, gamma - 1, y + 40);
                check("field: a drag selects, even off the well", (field_button(wui, &t, 0x110, false, gamma - 1, y + 40, 0) & UI_TEXT_MOVED) &&
                                                                      field_selected(&t, 0, 11));
                t.sel.click.at = 0;
                field_button(wui, &t, 0x110, true, beta + 1, y, 0);
                field_button(wui, &t, 0x110, false, beta + 1, y, 0);
                t.sel.click.at = 0;
                field_button(wui, &t, 0x110, true, field_x(wui, &t, 16) - 1, y, WINDOW_KEY_SHIFT);
                field_button(wui, &t, 0x110, false, field_x(wui, &t, 16) - 1, y, WINDOW_KEY_SHIFT);
                check("field: Shift with a click extends from the caret", field_selected(&t, 6, 16));
                check("field: the right button asks for the menu", field_button(wui, &t, 0x111, true, beta + 1, y, 0) & UI_TEXT_MENU);
                check("field: the wheel is never a field's", !ui_field_event(wui, &t, null, &(struct window_event){WINDOW_EVENT_WHEEL, 0, -120, 0}, beta, y));
        }

        touch_hold_check(wui, &t);
        touch_scroll_check(wui);
        check_touch_path(wui);
        check_select_scroll(wui);
        check_flick_lift();
        check_flick_retract();
    check_kit_late_lift();
    check_kit_throw_dp();
    check_kit_pause_rule();
    check_kit_batch_stamps();
    check_touch_trace();
        check_field_drag_drawn(wui);
        check_field_flick(wui);
        check_throw_last_step(wui);
        check_code_wheel(wui);
        check_kit_pointer_send(wui);
        check_tabs_marks(wui);
        check_doc_columns();
        check_doc_collapse();
        check_doc_crlf_pieces();
        caret_fade_check();
    caret_step_check();
        caret_field_check(wui);
        check_code_finger(wui);
        switch_cancel_check(wui);
        fling_scale_check();
        touch_select_check(wui);

        // The blink: a while after the last key the caret stays drawn, and the field asks for no frame.
        {
                b32 redraw = false;
                positive now;

                t.focused = true;
                ui_kit.test_clock = 0; // the real clock: the key stamps the caret with it, and the tick below reads it
                field_type(wui, &t, "z");
                now = clock_monotonic_nanoseconds();
                check("field: the caret blinks after a key", (ui_kit.now = ui_kit.test_clock = (b64)(now), ui_text_tick(&t, &redraw)) != 0);
                t.caret_fade = UI_CARET_FULL;
                redraw = false;
                check("field: and half a minute after the last it blinks still: a frame is due",
                      (ui_kit.now = ui_kit.test_clock = (b64)(now + 30000000000ull), ui_text_tick(&t, &redraw)) != 0);
        }
        // The caret's width: 1, 2 and 3 pixels at 100 percent (thin, normal, thick), and each scales with the screen, never to nothing:
        // a thin caret at 75 percent is one pixel, not the 0 that 0.75 rounds to. An empty field holds nothing but the caret, so the
        // widest run of pixels that are not the ground is the caret.
        {
                static const unsigned int places[3] = {UI_CARET_THIN, UI_CARET_MID, UI_CARET_THICK};
                static const int wanted[3] = {1, 2, 3};
                static const int scales[4] = {50, 75, 100, 200};
                unsigned int saved_thick = ui_kit.caret_thick, saved_speed = ui_kit.caret_speed;
                int scale_was = wui->scale, widths = 0;

                ui_kit.caret_speed = UI_CARET_OFF;
                ui_text_set(&t, (const p8 address_to) "", 0);
                t.focused = true;
                t.caret_fade = 0;
                for (int s = 0; s < 4; s++)
                        for (positive k = 0; k < 3; k++)
                        {
                                unsigned int *shown = (unsigned int address_to)pixels;
                                int widest = 0, scale = scales[s], want = wanted[k] * scale / 100;

                                want = want > 0 ? want : 1;

                                wui->scale = scale;
                                ui_kit.caret_thick = places[k];
                                memory_fill_u32(shown, 400 * 100, 0x111113);
                                ui_text_draw(wui, &t, 20, 0, 200, 100, 0x111113); // clear of the left edge, where the caret's left pixel is
                                for (int y = 0; y < 100; y++)
                                {
                                        int run = 0;

                                        for (int x = 0; x < 200; x++)
                                        {
                                                run = shown[(positive)y * 400 + (positive)x] != 0x111113u ? run + 1 : 0;
                                                widest = run > widest ? run : widest;
                                        }
                                }
                                widths += widest == want;
                        }
                check("caret: thin, normal and thick are 1, 2 and 3 pixels, and each scales with the screen, never to nothing", widths == 12);
                wui->scale = scale_was;
                ui_kit.caret_thick = saved_thick;
                ui_kit.caret_speed = saved_speed;
        }
        // A dropdown's chevron is two strokes of one logical pixel: at 50 and 75 percent they are still drawn, not zero pixels thick.
        {
                static const int scales[4] = {50, 75, 100, 200};
                int scale_was = wui->scale, drawn = 0;

                for (int s = 0; s < 4; s++)
                {
                        unsigned int *shown = (unsigned int address_to)pixels;
                        int inked = 0;

                        wui->scale = scales[s];
                        memory_fill_u32(shown, 400 * 100, 0x111113);
                        ui_chevron(wui, 50, 50, 8, 0xffffffffu);
                        for (positive i = 0; i < 400 * 100; i++)
                                inked |= shown[i] != 0x111113u;
                        drawn += inked;
                }
                check("chevron: a choice row's chevron is drawn at 50, 75, 100 and 200 percent", drawn == 4);
                wui->scale = scale_was;
        }
        // A rule is a hairline: one device pixel at 50 and 75 percent too, at the top of the row it is in (its first row is 0).
        {
                static const int scales[3] = {50, 75, 100};
                static struct ui_control rule;
                int scale_was = wui->scale, painted = 0;

                memory_zero(&rule, sizeof(rule));
                rule.kind = UI_RULE, rule.x = 0, rule.y = 0, rule.w = 100, rule.h = 1;
                for (int s = 0; s < 3; s++)
                {
                        unsigned int *shown = (unsigned int address_to)pixels;

                        wui->scale = scales[s];
                        memory_fill_u32(shown, 400 * 100, 0x111113);
                        ui_control_draw(wui, &rule);
                        painted += shown[5] != 0x111113u;
                }
                check("rule: a hairline is drawn at its row's top at 50, 75 and 100 percent", painted == 3);
                wui->scale = scale_was;
        }
        ui_text_close(&t);
        compositor_call(syscall(munmap), (positive)pixels, 400 * 100 * 4, 0, 0, 0, 0);
}
#else
static fn field_check(struct ui address_to wui)
{
        (void)wui;
        check("field: the kit has a field every input is (ui_field_event: rules, masked, its menu, the keys it leaves)", false);
}
#endif

/*
        Glass: a temporary overlay is glass, what is under it shows through, and its pieces are drawn in the inks of its
        ground (ui_text_colour, ui_well), flat: no shadow, ring or outline past an edge. The launcher and its menus are glass
        (WINDOW_GLASS, the menus through ui_popmenu_look); the settings window, the editor and the terminal are the kit's
        surface and not glass. A glass window has no line (compositor_line).
*/
#if MOONWATER_UI_LAUNCHER
static fn check_glass_pieces(string_address regular)
{
        static font_face face;
        static p8 atlas[512 * 512];
        static unsigned int scratch[800 * UI_SCRATCH_ROWS], picture[800 * 240], menu_picture[800 * 240];
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        static char names[1][LAUNCHER_NAME];
        static unsigned char from[1];
        static unsigned short hashed[LAUNCHER_HASH];
        static struct ui_popmenu menu;
        positive size = 0, bytes = compositor_map_file(regular, &size);
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        struct ui ui, menu_ui, parent;
        struct ui_popup popup;
        int pad, row, kit_ok;

        if (!bytes || (engine < 0 && engine > -4096) || font_load(&face, (const p8 address_to)bytes, size, 0) != 0)
        {
                check("glass: the face loads", false);
                return;
        }
        memory_zero(&ui, sizeof(ui));
        ui.engine = (font_engine address_to)engine;
        font_start(ui.engine, atlas, 512, 512);
        ui.regular = ui.bold = &face;
        ui.scale = 100;
        ui.scratch = scratch, ui.scratch_width = 800;
        ui.opaque = 0, ui.glass = 1;
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        ui_inks(&ui, 0x2b2b2eu);

        // The flags: the launcher is glass, the windows that stay are the kit's surface. (The terminal's app is not named
        // here: reading its table would pull in its callbacks, which need the shell's klog.)
        kit_ok = (launcher_app.glass & WINDOW_GLASS) != 0;
#if MOONWATER_UI_SETTINGS
        kit_ok = kit_ok && !(settings_app.glass & WINDOW_GLASS);
#endif
#if MOONWATER_UI_EDITOR
        kit_ok = kit_ok && !(editor_app.glass & WINDOW_GLASS);
#endif
        check("glass: the launcher is glass, and the settings window and the editor are the kit's surface", kit_ok);

        // The text's ink stands out from a dark glass (7 to 1 or more, the kit's own contrast); the quiet ink is another.
        check("glass: a glass window's text is the ink that stands out from its ground (7 to 1 on a dark glass)",
              ui_contrast(ui_text_colour(&ui, UI_ON_SURFACE) & 0xffffffu, 0x2b2b2eu) >= 700 &&
              (ui_text_colour(&ui, UI_ON_SURFACE_VARIANT) & 0xffffffu) != (ui_text_colour(&ui, UI_ON_SURFACE) & 0xffffffu));

        // The launcher drawn: its field is flat and the glass shows through where nothing is drawn.
        memory_zero(&launcher, sizeof(launcher));
        string_copy((string_address)names[0], (string_address) "lsof");
        launcher.names = (char address_to)names, launcher.count = 1, launcher.from = from, launcher.hashed = hashed;
        launcher.dirs[0] = "/bin", launcher.dir_count = 1;
        table_at->stride = 800, table_at->output_width = 1920, table_at->output_height = 1080;
        launcher.own.table = table_at;
        launcher.ui = ui;
        launcher.width = LAUNCHER_WIDTH, launcher.height = 240;
        launcher.focused = true, launcher_app.had_keys = true;
        launcher_type_text("ls");
        launcher_layout();
        {
                unsigned long was = ui_kit_calls;

                launcher_draw((unsigned int address_to)picture);
                if (kit_draw_first(1))
                {
                        kit_draw_print("launcher", ui_kit_calls - was);
                        check("kit calls: a draw of the launcher is made of painter calls", ui_kit_calls - was > 0);
                }
        }
        pad = ui_px(&launcher.ui, UI_PAD);
#define GLASS_AT(x, y) (picture[(y) * 800 + (x)] >> 24)
        check("glass: a launcher's ground is clear where no piece is drawn: the glass shows through",
              GLASS_AT(2, launcher.height - 2) == 0 && GLASS_AT(LAUNCHER_WIDTH - 2, launcher.height - 2) == 0);
        check("glass: the launcher's field has no background of its own, for now: the glass shows through at its top, its edges and its foot",
              GLASS_AT(LAUNCHER_WIDTH / 2, 0) == 0 && GLASS_AT(0, 0) == 0 && GLASS_AT(LAUNCHER_WIDTH - 1, 0) == 0 &&
                  GLASS_AT(LAUNCHER_WIDTH - 12, pad + UI_TARGET / 2) == 0 && GLASS_AT(LAUNCHER_WIDTH - 12, pad + UI_TARGET - 1) == 0);
#undef GLASS_AT

        // A menu: glass with no face and no outline, its hot choice lit and the others not.
        ui_popmenu_add(&menu, "Copy", "Ctrl+C", 0, 1);
        ui_popmenu_add(&menu, "Paste", "Ctrl+V", 0, 2);
        ui_popmenu_add(&menu, "", null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(&menu, "Select all", null, 0, 3);
        menu.hot = 1, menu.press.down = false;
        menu_ui = ui;
        menu_ui.opaque = 0, menu_ui.glass = 1;
        ui_popmenu_size(&menu_ui, &menu, 0);
        ui_begin(&menu_ui, (unsigned int address_to)menu_picture, 800, menu.width, menu.height);
        ui_popmenu_draw(&menu_ui, &menu);
        row = popmenu_row(&menu);
#define MENU_AT(x, y) (menu_picture[(y) * 800 + (x)] >> 24)
        check("glass: a menu has no face and no outline: its top, left, right and bottom edges are clear",
              MENU_AT(menu.width / 2, 0) == 0 && MENU_AT(0, UI_MENU_PAD + row / 2) == 0 && MENU_AT(menu.width - 1, UI_MENU_PAD + row / 2) == 0 &&
              MENU_AT(menu.width / 2, menu.height - 1) == 0);
        check("glass: a menu's hot choice is lit over the glass, and the choice above it is not",
              MENU_AT(UI_MENU_PAD + 4, UI_MENU_PAD + row + row / 2) > 0 && MENU_AT(UI_MENU_PAD + 4, UI_MENU_PAD + row / 2) == 0);
#undef MENU_AT

        // A popup's look: glass, its inks the ground the compositor gave its row, else the parent's ground.
        parent = ui;
        parent.opaque = 1, parent.glass = 0;
        ui_inks(&parent, 0xe0e0e0u);
        memory_zero(&popup, sizeof(popup));
        popup.own.table = table_at, popup.own.row = 2;
        table_at->ground[2] = 0x112233u;
        ui_popmenu_look(&popup, table_at, &parent);
        check("glass: a menu is glass: its row is glass with no close button, and its inks are the row's ground",
              popup.ui.glass && !popup.ui.opaque && table_at->glass[2] == UI_POPUP_GLASS && popup.ui.ground == 0x112233u);
        table_at->ground[2] = 0;
        ui_popmenu_look(&popup, table_at, &parent);
        check("glass: a menu opened before the compositor gave its ground is inked by the parent's", popup.ui.ground == 0xe0e0e0u);

        // A glass window has its line too, as a floating window that is not glass does.
        {
                unsigned int was = compositor.arranged[4];
                b32 glassy = compositor.glassy[4];

                compositor.scale = 100;
                compositor.arranged[4] = COMPOSITOR_FLOATING;
                compositor.glassy[4] = 0;
                kit_ok = compositor_line(4) == COMPOSITOR_LINE;
                compositor.glassy[4] = 1;
                check("glass: a glass window has its line, as a floating window that is not glass does", kit_ok && compositor_line(4) == COMPOSITOR_LINE);
                compositor.arranged[4] = was, compositor.glassy[4] = glassy;
        }
}
#endif

/*
        No window types by hand: every input of Canvas is the kit's field, so no file of the windows (src/canvas/ui/,
        and ui.c before its text engine) has the idioms of a field written by hand -- a key's character encoded into a
        typed buffer, a Backspace stepping back over a character's bytes, a key's character tested for a printable one,
        a blank refused as the first character. text.c and the engine's part of ui.c are where typing lives. Read from
        the tree the check runs in (the repository's root); not run where the sources are not.
*/
static fn check_no_hand_typing(void)
{
        static const char address_to const files[] = {"src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/editor.c",
                                                      "src/canvas/ui/terminal.c", "src/canvas/ui/taskbar.c", "src/canvas/ui/ui.c"};
        static const char address_to const idioms[] = {"memory_utf8_encode(bytes, sizeof(bytes)", "[length - 1] & 0xc0) == 0x80",
                                                       "c == 0x7f || (event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT))",
                                                       "c == ' ' && !length", "unsigned int c = (unsigned int)event->b;"};
        positive found = 0, read = 0;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f], &size), stop = size;
                const char address_to text = (const char address_to)bytes;

                if (!bytes)
                        continue;
                read++;
                // ui.c is the kit up to its text engine, which is where a field's typing is.
                if (f == sizeof(files) / sizeof(files[0]) - 1)
                {
                        const char address_to engine = (const char address_to)memory_search((address_any)text, size,
                                                                                             (address_any) "/* ---- text: a field", 21);

                        stop = engine ? (positive)(engine - text) : size;
                }
                for (positive i = 0; i < sizeof(idioms) / sizeof(idioms[0]); i++)
                {
                        positive length = string_length((string_address)idioms[i]);

                        for (positive at = 0; at + length <= stop;)
                        {
                                const char address_to hit = (const char address_to)memory_search((address_any)(text + at), stop - at,
                                                                                                  (address_any)idioms[i], length);

                                if (!hit || (positive)(hit - text) + length > stop)
                                        break;
                                found++;
                                string_format(log, "  typing by hand in %s: %s\n", (string_address)files[f], (string_address)idioms[i]);
                                at = (positive)(hit - text) + length;
                        }
                }
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: no window types by hand, the sources are not here\n");
                return;
        }
        check("fields: no window of Canvas types by hand: every input is the kit's field", read == sizeof(files) / sizeof(files[0]) && !found);
}

static positive check_hits(const char address_to text, positive size, const char address_to idiom);

/*
        The kit's standard as a ratchet (memory ui-kit-standard-2026-11-04, section 6): an app reads no touch flag of its own
        (ui_event_context reads it once), keeps no clock of its own (ui_kit.now), holds no press of a kit widget in its fields
        and defines no static with a kit's prefix. The apps still do some of these, so each count in ratchet[] is what the file
        has now: a file may have fewer, and more is refused. Lowering a number is the fix, in the commit that removes the use.
        Read from the tree the check runs in (the repository's root); not run where the sources are not.
*/
static fn check_kit_standard(void)
{
        static const char address_to const apps[] = {"src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/editor.c",
                                                     "src/canvas/ui/terminal.c", "src/canvas/ui/taskbar.c", "src/canvas/ui/identify.c", "src/canvas/ui/mixer.c"};
        static const char address_to const idioms[] = {"WINDOW_KEY_TOUCH", "clock_monotonic_nanoseconds", ".press.down", "static fn ui_",
                                                       "static void ui_", "static int ui_", "static b32 ui_"};
        static const struct
        {
                const char address_to file;
                const char address_to idiom;
                positive most;
        } ratchet[] = {
                {"src/canvas/ui/settings.c", "WINDOW_KEY_TOUCH", 3},
                {"src/canvas/ui/launcher.c", "WINDOW_KEY_TOUCH", 3},
                {"src/canvas/ui/terminal.c", "WINDOW_KEY_TOUCH", 0},
                {"src/canvas/ui/settings.c", "clock_monotonic_nanoseconds", 0},
                {"src/canvas/ui/launcher.c", "clock_monotonic_nanoseconds", 0},
                {"src/canvas/ui/editor.c", "clock_monotonic_nanoseconds", 0},
                {"src/canvas/ui/terminal.c", "clock_monotonic_nanoseconds", 0},
                {"src/canvas/ui/identify.c", "clock_monotonic_nanoseconds", 0},
                {"src/canvas/ui/mixer.c", "clock_monotonic_nanoseconds", 0},
                {"src/canvas/ui/settings.c", ".press.down", 2},
                {"src/canvas/ui/launcher.c", ".press.down", 1},
        };
        positive found = 0, read = 0;

        for (positive f = 0; f < sizeof(apps) / sizeof(apps[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)apps[f], &size);
                const char address_to text = (const char address_to)bytes;

                if (!bytes)
                        continue;
                read++;
                for (positive i = 0; i < sizeof(idioms) / sizeof(idioms[0]); i++)
                {
                        positive hits = check_hits(text, size, idioms[i]), most = 0;

                        for (positive r = 0; r < sizeof(ratchet) / sizeof(ratchet[0]); r++)
                                if (!string_compare((string_address)ratchet[r].file, (string_address)apps[f]) &&
                                    !string_compare((string_address)ratchet[r].idiom, (string_address)idioms[i]))
                                        most = ratchet[r].most;
                        if (hits > most)
                        {
                                found++;
                                string_format(log, "  kit standard: %s has more of %s than the ratchet allows\n", (string_address)apps[f],
                                              (string_address)idioms[i]);
                        }
                }
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        if (read != sizeof(apps) / sizeof(apps[0]))
        {
                string_format(log, "  NOT RUN: the kit's standard is read from the sources, which are not all here\n");
                return;
        }
        check("kit: no app reads a touch flag, keeps a clock, holds a press or names a static with a kit's prefix beyond the ratchet", !found);
}

/*
        A kit row reads no real clock: the kit's time is ui_kit.now (a pass or an event sets it) and the kit's clock is
        ui_kit_clock, which a check sets with ui_kit.test_clock. A row that reads the monotonic clock is an oracle that the
        load of the box can fail (the settings feed and the terminal's caret were, under load), so this scan refuses one in
        a kit row: every check of this file whose name says a kit area. Strings are not read (the scan's own names are
        strings). Read from the tree the check runs in; not run where the sources are not.
*/
static fn check_kit_clock(void)
{
        static const char address_to const kit[] = {"check_kit", "check_caret", "check_identify", "check_settings", "check_field",
                                                    "check_text", "check_finger", "check_press", "check_scroll", "check_hand",
                                                    "check_wheel", "check_menu", "check_drag"};
        static const char address_to const idiom = "clock_monotonic_nanoseconds";
        positive size = 0, bytes = compositor_map_file((string_address) "test/checks.c", &size), found = 0, at = 0;
        const char address_to text = (const char address_to)bytes;
        b32 inside = false, is_kit = false;
        char name[96] = {0}, line[4096];

        if (!bytes)
        {
                string_format(log, "  NOT RUN: the kit rows are read from test/checks.c, which is not here\n");
                return;
        }
        while (at < size)
        {
                positive stop = at, n = 0;
                b32 quoted = false;

                while (stop < size && text[stop] != '\n')
                        stop++;
                if (!inside && text[at] == 's' && stop - at > 7 && memory_search((address_any)(text + at), 7, (address_any)"static ", 7))
                {
                        const char address_to call = (const char address_to)memory_search((address_any)(text + at), stop - at, (address_any)"check_", 6);

                        if (call && memory_search((address_any)(text + at), stop - at, (address_any)"(", 1))
                        {
                                positive k = 0;

                                while (call[k] && call[k] != '(' && k + 1 < sizeof(name))
                                        name[k] = call[k], k++;
                                name[k] = 0;
                                is_kit = false;
                                for (positive i = 0; i < sizeof(kit) / sizeof(kit[0]); i++)
                                {
                                        positive p = 0;

                                        while (kit[i][p] && name[p] == kit[i][p])
                                                p++;
                                        if (!kit[i][p])
                                                is_kit = true;
                                }
                                inside = is_kit;
                        }
                }
                if (inside)
                {
                        for (positive k = at; k < stop && n < sizeof(line) - 1; k++)
                        {
                                if (text[k] == '"' && (k == at || text[k - 1] != '\\'))
                                        quoted = !quoted;
                                else if (!quoted)
                                        line[n++] = text[k];
                        }
                        line[n] = 0;
                        if (check_hits((const char address_to)line, n, idiom))
                        {
                                found++;
                                string_format(log, "  kit clock: %s reads the monotonic clock in a kit row\n", (string_address)name);
                        }
                        if (stop == at + 1 && text[at] == '}')
                                inside = false;
                }
                at = stop + 1;
        }
        check("kit rows: no kit row reads the real clock: a kit's time is ui_kit.now or the test clock ui_kit.test_clock", !found);
}

// How many times idiom is in the first size bytes of text.
static positive check_hits(const char address_to text, positive size, const char address_to idiom)
{
        positive length = string_length((string_address)idiom), hits = 0;

        for (positive at = 0; at + length <= size;)
        {
                const char address_to hit = (const char address_to)memory_search((address_any)(text + at), size - at, (address_any)idiom, length);

                if (!hit)
                        break;
                hits++;
                at = (positive)(hit - text) + length;
        }
        return hits;
}

#if MOONWATER_UI_ANY
/*
        The language table and its files (ui/ui.c, ui/ui.c, languages/). Read from the tree the check runs in
        (the repository's root), as the checks above are; not run where the sources are not.

        English is the table, and languages/en.txt is the same words written out, which pins them: a word changed in the table
        and not there is red, and so is one changed there (a row proves that one changed letter is seen). Every other language
        is a file with the same ids and the same keyed texts as English, no more and no fewer, each line stamped with the
        English it was made from (an English edit makes it old), its placeholders and braces the English's one for one, its
        mnemonics before an ASCII letter and none twice in a menu, and every letter of it a glyph the faces have. The keyed
        texts are the Settings window's: every group, label and choice word of registry.c, which stay English as the keys
        code compares and are shown through language_of. Code is held to the table too: every id is used, no lookup is
        compared as a key, and in a file that is converted no English literal is left that is not a key, a path, a log line
        or assembly (languages/keep lists the few that are none of those, one `file TAB literal` a line). The kit is one file
        with the table in it, so the scan reads the table too: a line of the table is where the English is kept and is no
        literal left, and neither is a word of the kit's tables of keys or of the languages' own names, which lcheck_tables
        names one by one.
*/
#define LCHECK_ENTRIES 1024
struct lcheck_entry
{
        const char address_to key;
        const char address_to stamp;
        const char address_to text;
        positive key_length, text_length;
};

struct lcheck_file
{
        positive bytes, size, count, bad;
        const char address_to code;
        const char address_to name;
        const char address_to translated;
        const char address_to direction;
        positive code_length, name_length, translated_length, direction_length;
        struct lcheck_entry entry[LCHECK_ENTRIES];
};

static struct lcheck_file lcheck_english, lcheck_other;

static b32 lcheck_same(const char address_to a, positive a_length, const char address_to b, positive b_length)
{
        return a_length == b_length && !memory_compare(a, b, a_length);
}

// The file parsed in place into its entries and its header (@language, @name, @translated, @direction); false when it is not there.
static b32 lcheck_load(string_address path, struct lcheck_file address_to file)
{
        const char address_to text;
        positive at = 0;

        memory_zero(file, sizeof(*file));
        file->bytes = compositor_map_file(path, &file->size);
        if (!file->bytes)
                return false;
        text = (const char address_to)file->bytes;
        while (at < file->size)
        {
                positive start = at, stop, tab, second;

                while (at < file->size && text[at] != '\n')
                        at++;
                stop = at++;
                if (stop == start || text[start] == '#')
                        continue;
                if (text[start] == '@')
                {
                        if (stop - start > 10 && !memory_compare(text + start, "@language ", 10))
                                file->code = text + start + 10, file->code_length = stop - start - 10;
                        else if (stop - start > 6 && !memory_compare(text + start, "@name ", 6))
                                file->name = text + start + 6, file->name_length = stop - start - 6;
                        else if (stop - start > 12 && !memory_compare(text + start, "@translated ", 12))
                                file->translated = text + start + 12, file->translated_length = stop - start - 12;
                        else if (stop - start > 11 && !memory_compare(text + start, "@direction ", 11))
                                file->direction = text + start + 11, file->direction_length = stop - start - 11;
                        continue;
                }
                for (tab = start; tab < stop && text[tab] != '\t'; tab++)
                        ;
                second = tab + 5;
                if (second >= stop || text[second] != '\t' || file->count >= LCHECK_ENTRIES)
                {
                        file->bad++;
                        continue;
                }
                file->entry[file->count++] = (struct lcheck_entry){text + start, text + tab + 1, text + second + 1, tab - start,
                                                                   stop - second - 1};
        }
        return true;
}

static fn lcheck_close(struct lcheck_file address_to file)
{
        if (file->bytes)
                compositor_call(syscall(munmap), file->bytes, file->size, 0, 0, 0, 0);
        file->bytes = 0;
}

static long lcheck_find(const struct lcheck_file address_to file, const char address_to key, positive length)
{
        for (positive at = 0; at < file->count; at++)
                if (lcheck_same(file->entry[at].key, file->entry[at].key_length, key, length))
                        return (long)at;
        return -1;
}

// The stamp of an English text: the low 16 bits of its FNV-1a hash, four hex digits.
static fn lcheck_stamp(const char address_to text, positive length, char address_to into)
{
        unsigned int hash = 2166136261u;

        for (positive at = 0; at < length; at++)
                hash = (hash ^ (p8)text[at]) * 16777619u;
        for (int digit = 0; digit < 4; digit++)
                into[digit] = "0123456789abcdef"[hash >> (12 - 4 * digit) & 15];
}

/*
        How many ids of the table the file does not say the way the table does (its text, its stamp). With a flip, that one
        id is compared with its English changed by a letter, which is what a changed word is to the pin.
*/
static positive lcheck_identity(const struct lcheck_file address_to file, long flip)
{
        positive wrong = 0;

        for (positive id = 0; id < LANGUAGE_COUNT; id++)
        {
                char english[256], stamp[4];
                positive length = string_length((string_address)language_table[id].english);
                long at = lcheck_find(file, language_table[id].name, string_length((string_address)language_table[id].name));

                if (at < 0 || length >= sizeof(english))
                {
                        wrong++;
                        continue;
                }
                memory_copy(english, language_table[id].english, length);
                if ((long)id == flip)
                        english[0] = (char)(english[0] ^ 1);
                lcheck_stamp(english, length, stamp);
                wrong += !lcheck_same(file->entry[at].text, file->entry[at].text_length, english, length) ||
                         memory_compare(file->entry[at].stamp, stamp, 4) != 0;
        }
        return wrong;
}

// The placeholders of a text, one byte each (its index times two, and one more for a string), sorted; false when one is ill made.
static b32 lcheck_specs(const char address_to text, positive length, p8 address_to into, positive address_to count)
{
        unsigned int implicit = 0;

        *count = 0;
        for (positive at = 0; at < length; at++)
        {
                unsigned int index;

                if (text[at] != '%')
                        continue;
                if (++at >= length)
                        return false;
                if (text[at] == '%')
                        continue;
                if (text[at] >= '1' && text[at] <= '9' && at + 1 < length && text[at + 1] == '$')
                        index = (unsigned int)(text[at] - '0'), at += 2;
                else
                        index = ++implicit;
                if (at >= length || (text[at] != 's' && text[at] != 'd') || index > 9 || *count >= 16)
                        return false;
                into[(*count)++] = (p8)(index * 2 + (text[at] == 's'));
        }
        for (positive a = 1; a < *count; a++)
                for (positive b = a; b > 0 && into[b - 1] > into[b]; b--)
                {
                        p8 swap = into[b];

                        into[b] = into[b - 1], into[b - 1] = swap;
                }
        return true;
}

// The {braces} of a text, each from { to }: a sum over all of them and their count, which two texts with the same braces share.
static fn lcheck_braces(const char address_to text, positive length, unsigned long address_to sum, positive address_to count)
{
        *sum = 0, *count = 0;
        for (positive at = 0; at < length; at++)
                if (text[at] == '{')
                {
                        unsigned long each = 5381;

                        for (; at < length && text[at] != '}'; at++)
                                each = each * 33 + (p8)text[at];
                        *sum += each, ++*count;
                }
}

// The mnemonic of a text: the letter after its & in lower case, 0 for none, 1 when the & is not before an ASCII letter; marks counts the &.
static unsigned int lcheck_mnemonic(const char address_to text, positive length, positive address_to marks)
{
        unsigned int letter = 0;

        *marks = 0;
        for (positive at = 0; at < length; at++)
                if (text[at] == '&')
                {
                        ++*marks;
                        letter = at + 1 < length && (text[at + 1] | 32) >= 'a' && (text[at + 1] | 32) <= 'z' ? (unsigned int)(text[at + 1] | 32) : 1;
                }
        return letter;
}

// How many characters (not bytes) a text is.
static positive lcheck_characters(const char address_to text, positive length)
{
        positive count = 0;

        for (positive at = 0; at < length; at++)
                count += ((p8)text[at] & 0xc0) != 0x80;
        return count;
}

/*
        Whether every character of a text is well formed UTF-8 that the face draws (a glyph, not the notdef) and none is a control.
        A language of another script is drawn by the face of that script, and by the one behind it for what that lacks: the Latin of
        a key name, a placeholder or a command, the digits and the marks (behind is none for the languages Inter alone draws).
*/
static b32 lcheck_drawn(const font_face address_to face, const font_face address_to behind, const char address_to text, positive length)
{
        for (positive at = 0; at < length;)
        {
                p8 first = (p8)text[at];
                unsigned int more = first < 0x80 ? 0 : (first & 0xe0) == 0xc0 ? 1 : (first & 0xf0) == 0xe0 ? 2 : (first & 0xf8) == 0xf0 ? 3 : 9;
                unsigned int code = more ? first & (0x3f >> more) : first;

                if (more == 9 || at + more >= length)
                        return false;
                for (unsigned int k = 1; k <= more; k++)
                {
                        if (((p8)text[at + k] & 0xc0) != 0x80)
                                return false;
                        code = code << 6 | ((p8)text[at + k] & 0x3f);
                }
                if (code < 0x20 || code == 0x7f || !(font_glyph(face, code) || (behind && font_glyph(behind, code))))
                        return false;
                at += more + 1;
        }
        return true;
}

// The menus whose mnemonics must differ: each is its ids, ended by -1.
static const int lcheck_menus[][12] = {
    {LANGUAGE_menu_cut, LANGUAGE_menu_copy, LANGUAGE_menu_paste, LANGUAGE_menu_delete, LANGUAGE_menu_undo, LANGUAGE_menu_redo,
     LANGUAGE_menu_select_all, -1},
    // The editor's: the kit's seven, then find, go to a line and the comment.
    {LANGUAGE_menu_cut, LANGUAGE_menu_copy, LANGUAGE_menu_paste, LANGUAGE_menu_delete, LANGUAGE_menu_undo, LANGUAGE_menu_redo,
     LANGUAGE_menu_select_all, LANGUAGE_menu_find, LANGUAGE_menu_go_to_line, LANGUAGE_menu_toggle_comment, -1},
    // The terminal's: copy, paste, select all, and the zoom.
    {LANGUAGE_menu_copy, LANGUAGE_menu_paste, LANGUAGE_menu_select_all, LANGUAGE_menu_zoom_in, LANGUAGE_menu_zoom_out,
     LANGUAGE_menu_reset_zoom, -1},
};

// Whether a word of a row's choices is one the window shows as words (not a number, a layout code, a language or a display mode).
static b32 lcheck_choice_word(const struct settings_row address_to row, const char address_to word, positive length)
{
        if (row->kind != SETTINGS_CHOICE || !row->choices || (row->flags & SETTINGS_LANGUAGE) ||
            !string_compare_max((string_address)row->label, (string_address) "Keyboard", 9) ||
            (row->set[1] && string_equals((string_address)row->set[1], (string_address) "modes")))
                return false;
        for (positive at = 0; at < length; at++)
                if (word[at] >= '0' && word[at] <= '9')
                        return false;
        return true;
}

// Whether English has the keyed line for a text: the registry's group, label or choice word.
static positive lcheck_keyed_missing(const char address_to text, positive length)
{
        char key[200];

        if (!text || !length || length + 2 > sizeof(key))
                return 0;
        key[0] = '=';
        memory_copy(key + 1, text, length);
        key[length + 1] = 0;
        if (lcheck_find(&lcheck_english, key, length + 1) >= 0)
                return 0;
        string_format(log, "  languages: no keyed line in English for \"%s\"\n", (string_address)(key + 1));
        return 1;
}

/*
        The languages of languages/ besides English, in the order the Language row offers them, and which script each is written in.
        ar, fa and ur are Arabic script (Noto Sans Arabic) and he is Hebrew (Noto Sans Hebrew): those four read from the right, their
        file says @direction rtl and no other file does, and Inter draws the Latin they keep. The other twelve are Inter's alone.
*/
enum
{
        LCHECK_LATIN,
        LCHECK_ARABIC,
        LCHECK_HEBREW
};

static const struct
{
        const char address_to code;
        unsigned int script;
} lcheck_codes[] = {{"es", LCHECK_LATIN},  {"fr", LCHECK_LATIN},  {"de", LCHECK_LATIN},  {"pt", LCHECK_LATIN},
                    {"it", LCHECK_LATIN},  {"nl", LCHECK_LATIN},  {"pl", LCHECK_LATIN},  {"tr", LCHECK_LATIN},
                    {"sv", LCHECK_LATIN},  {"ru", LCHECK_LATIN},  {"uk", LCHECK_LATIN},  {"el", LCHECK_LATIN},
                    {"ar", LCHECK_ARABIC}, {"he", LCHECK_HEBREW}, {"fa", LCHECK_ARABIC}, {"ur", LCHECK_ARABIC}};

// The faces of the two other scripts, [script][regular, bold], read from the tree's fonts/ by language_check.
static font_face lcheck_script_face[3][2];

static fn language_files_check(const font_face address_to regular, const font_face address_to bold)
{
        const char address_to wanted = SETTINGS_LANGUAGE_CODES;
        positive missing_keys = 0, order = 0, absent = 0, bad_header = 0, bad_text = 0, bad_stamp = 0, bad_ids = 0, bad_keys = 0;
        positive bad_specs = 0, bad_menus = 0, bad_glyphs = 0, tab_words = 0, doubled = 0;
        positive english_total, language_total, longest;

        // The registry's own texts are the keyed lines of English.
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];
                const char address_to words = row->choices;

                missing_keys += lcheck_keyed_missing(row->section, row->section ? string_length((string_address)row->section) : 0);
                missing_keys += lcheck_keyed_missing(row->label, row->label ? string_length((string_address)row->label) : 0);
                missing_keys += lcheck_keyed_missing(row->part, row->part ? string_length((string_address)row->part) : 0);
                missing_keys += lcheck_keyed_missing(row->about, row->about ? string_length((string_address)row->about) : 0);
                while (words && *words)
                {
                        positive length = (positive)(string_first_of_or_end((string_address)words, ' ') - (string_address)words);

                        if (lcheck_choice_word(row, words, length))
                                missing_keys += lcheck_keyed_missing(words, length);
                        words += length + (words[length] == ' ');
                }
        }
        // The codes the Language row offers are the table of names', in its order, and each is a file.
        for (positive at = 0; at < sizeof(language_names) / sizeof(language_names[0]); at++)
        {
                positive length = string_length((string_address)language_names[at].code);

                order += !(!memory_compare(wanted, language_names[at].code, length) && (wanted[length] == ' ' || !wanted[length]));
                wanted += length + (wanted[length] == ' ');
        }
        order += *wanted != 0;
        for (positive l = 0; l < sizeof(lcheck_codes) / sizeof(lcheck_codes[0]); l++)
        {
                char path[24] = "languages/";
                const char address_to code = lcheck_codes[l].code;
                const char address_to name = null;
                // The face of its script and the one behind it: Inter alone for the languages it draws, else the script's and Inter behind.
                const font_face address_to face_regular = lcheck_codes[l].script ? &lcheck_script_face[lcheck_codes[l].script][0] : regular;
                const font_face address_to face_bold = lcheck_codes[l].script ? &lcheck_script_face[lcheck_codes[l].script][1] : bold;
                const font_face address_to behind_regular = lcheck_codes[l].script ? regular : null;
                const font_face address_to behind_bold = lcheck_codes[l].script ? bold : null;

                path[10] = code[0], path[11] = code[1], memory_copy(path + 12, ".txt", 5);
                english_total = language_total = longest = 0;
                if (!lcheck_load((string_address)path, &lcheck_other))
                {
                        absent++;
                        continue;
                }
                for (positive at = 0; at < sizeof(language_names) / sizeof(language_names[0]); at++)
                        if (!memory_compare(language_names[at].code, code, 3))
                                name = language_names[at].name;
                bad_header += !lcheck_same(lcheck_other.code, lcheck_other.code_length, code, 2) || !name ||
                              !lcheck_same(lcheck_other.name, lcheck_other.name_length, name, string_length((string_address)name)) ||
                              !(lcheck_same(lcheck_other.translated, lcheck_other.translated_length, "machine", 7) ||
                                lcheck_same(lcheck_other.translated, lcheck_other.translated_length, "reviewed", 8)) ||
                              lcheck_other.bad ||
                              // A language that reads from the right says so in its header, and one that reads from the left says nothing.
                              !(lcheck_codes[l].script ? lcheck_same(lcheck_other.direction, lcheck_other.direction_length, "rtl", 3)
                                                       : !lcheck_other.direction);
                // The same lines as English, each once: none missing, none over, none twice.
                bad_ids += lcheck_other.count != lcheck_english.count;
                for (positive at = 0; at < lcheck_english.count; at++)
                {
                        const struct lcheck_entry address_to said = &lcheck_english.entry[at];
                        const struct lcheck_entry address_to mine;
                        long found = lcheck_find(&lcheck_other, said->key, said->key_length);
                        p8 specs[16], theirs[16];
                        positive specs_count = 0, theirs_count = 0, marks = 0, theirs_marks = 0, braces, theirs_braces;
                        unsigned long sum, theirs_sum;
                        char stamp[4];

                        if (found < 0)
                        {
                                bad_keys++;
                                continue;
                        }
                        mine = &lcheck_other.entry[found];
                        lcheck_stamp(said->text, said->text_length, stamp);
                        bad_stamp += memory_compare(mine->stamp, stamp, 4) != 0;
                        bad_text += !mine->text_length || mine->text_length > 200;
                        // How long it runs against the English, in characters: the windows cut a text with ... where it does not fit, and
                        // a text more than twice the English and a dozen over is no layout's to hold.
                        {
                                positive was = lcheck_characters(said->text, said->text_length), grew = lcheck_characters(mine->text, mine->text_length);

                                bad_text += grew > 2 * was + 12;
                                english_total += was, language_total += grew;
                                longest = grew * 100 / (was ? was : 1) > longest && was > 8 ? grew * 100 / (was ? was : 1) : longest;
                        }
                        for (positive k = 0; k < mine->text_length; k++)
                                bad_text += (p8)mine->text[k] < 0x20 || mine->text[k] == 0x7f;
                        bad_specs += !lcheck_specs(said->text, said->text_length, specs, &specs_count) ||
                                     !lcheck_specs(mine->text, mine->text_length, theirs, &theirs_count) || specs_count != theirs_count ||
                                     memory_compare(specs, theirs, specs_count);
                        lcheck_braces(said->text, said->text_length, &sum, &braces);
                        lcheck_braces(mine->text, mine->text_length, &theirs_sum, &theirs_braces);
                        bad_specs += sum != theirs_sum || braces != theirs_braces;
                        // A mnemonic is kept or dropped, never moved off an ASCII letter, and a blank at either end is English's.
                        lcheck_mnemonic(said->text, said->text_length, &marks);
                        bad_menus += lcheck_mnemonic(mine->text, mine->text_length, &theirs_marks) == 1 || theirs_marks > marks;
                        bad_text += (said->text_length && said->text[said->text_length - 1] == ' ') !=
                                    (mine->text_length && mine->text[mine->text_length - 1] == ' ');
                        bad_text += (said->text_length && said->text[0] == ' ') != (mine->text_length && mine->text[0] == ' ');
                        // Words are a blank apart: no text has two in a row, the English (looked at with the first file) or a translation.
                        doubled += memory_search((address_any)mine->text, mine->text_length, (address_any) "  ", 2) != null;
                        doubled += !l && memory_search((address_any)said->text, said->text_length, (address_any) "  ", 2) != null;
                        {
                                b32 undrawn = !lcheck_drawn(face_regular, behind_regular, mine->text, mine->text_length) ||
                                              !lcheck_drawn(face_bold, behind_bold, mine->text, mine->text_length);
                                char said_key[64];
                                positive key_room = mine->key_length < sizeof(said_key) - 1 ? mine->key_length : sizeof(said_key) - 1;

                                bad_glyphs += undrawn;
                                // The first few say which text, in which language: a letter of it has no glyph in the shipped face.
                                if (undrawn && bad_glyphs <= 8)
                                {
                                        memory_copy(said_key, mine->key, key_room);
                                        said_key[key_room] = 0;
                                        string_format(log, "  language %s: a letter of '%s' has no glyph in the shipped face\n", (string_address)code, (string_address)said_key);
                                }
                        }
                        // A word a row of tabs shows has no blank in it: the tabs are the words of a list a blank apart.
                        if (said->key_length > 1 && said->key[0] == '=' &&
                            (lcheck_same(said->key, said->key_length, "=light", 6) || lcheck_same(said->key, said->key_length, "=dark", 5) ||
                             lcheck_same(said->key, said->key_length, "=auto", 5) || lcheck_same(said->key, said->key_length, "=full", 5) ||
                             lcheck_same(said->key, said->key_length, "=reduced", 8)))
                                tab_words += memory_search((address_any)mine->text, mine->text_length, (address_any) " ", 1) != null;
                }
                // No two items of a menu share a mnemonic.
                for (positive m = 0; m < sizeof(lcheck_menus) / sizeof(lcheck_menus[0]); m++)
                {
                        unsigned int seen[16], used = 0;

                        for (int k = 0; lcheck_menus[m][k] >= 0; k++)
                        {
                                const char address_to id = language_table[lcheck_menus[m][k]].name;
                                long at = lcheck_find(&lcheck_other, id, string_length((string_address)id));
                                positive marks;
                                unsigned int letter;

                                if (at < 0)
                                        continue;
                                letter = lcheck_mnemonic(lcheck_other.entry[at].text, lcheck_other.entry[at].text_length, &marks);
                                for (unsigned int s = 0; letter > 1 && s < used; s++)
                                        bad_menus += seen[s] == letter;
                                if (letter > 1 && used < 16)
                                        seen[used++] = letter;
                        }
                }
                lcheck_close(&lcheck_other);
                string_format(log, "  language %s: %p percent of the English in characters, the longest text %p percent\n",
                              (string_address)code, english_total ? language_total * 100 / english_total : 0, longest);
        }
        if (missing_keys + order + absent + bad_header + bad_text + bad_stamp + bad_ids + bad_keys + bad_specs + bad_menus + bad_glyphs)
                string_format(log, "  languages: keys %p, order %p, absent %p, header %p, text %p, stamp %p, ids %p, lines %p, placeholders %p, mnemonics %p, glyphs %p\n",
                              missing_keys, order, absent, bad_header, bad_text, bad_stamp, bad_ids, bad_keys, bad_specs, bad_menus, bad_glyphs);
        check("languages: every group, part, label, description and choice word of the registry has its keyed line in English", !missing_keys);
        check("languages: the words the tabs of the theme and the motion show are single words in every language", !tab_words);
        check("languages: no text of English or of a translation has two blanks in a row", !doubled);
        check("languages: the Language row offers the table of names' codes in its order, and each is a file", !order && !absent);
        check("languages: each file's header says its code, its own name, what made it, machine or reviewed, and @direction rtl if it reads from the right",
              !bad_header);
        check("languages: each file has the ids and keyed lines English has, once each, stamped with the English it was made from",
              !bad_ids && !bad_keys && !bad_stamp);
        check("languages: placeholders and braces are English's one for one, mnemonics before ASCII letters and not twice in a menu",
              !bad_specs && !bad_menus);
        check("languages: every text is one line of well formed UTF-8, never empty, every letter a glyph of the shipped face of its script",
              !bad_text && !bad_glyphs);
}

// The sources an id may be named in, and the ones converted: no English literal is left in them.
static const char address_to const lcheck_sources[] = {
    "src/canvas/ui/ui.c",       "src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/taskbar.c",
    "src/canvas/ui/editor.c",   "src/canvas/ui/terminal.c", "src/canvas/ui/identify.c", "src/canvas/ui/mixer.c",
    "src/canvas/ui/notices.c",  "src/canvas/ui/files.c",    "src/canvas/ui/installer.c"};
static const char address_to const lcheck_converted[] = {"src/canvas/ui/ui.c",      "src/canvas/ui/editor.c",   "src/canvas/ui/launcher.c",
                                                         "src/canvas/ui/taskbar.c", "src/canvas/ui/terminal.c", "src/canvas/ui/settings.c",
                                                         "src/canvas/ui/mixer.c",   "src/canvas/ui/notices.c",  "src/canvas/ui/files.c",
                                                         "src/canvas/ui/installer.c"};

static b32 lcheck_word_byte(char c)
{
        return c == '_' || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
}

// How many times name stands whole in the text (not at the start or end of a longer identifier).
static positive lcheck_tokens(const char address_to text, positive size, const char address_to name)
{
        positive length = string_length((string_address)name), hits = 0;

        for (positive at = 0; at + length <= size;)
        {
                const char address_to hit = (const char address_to)memory_search((address_any)(text + at), size - at, (address_any)name, length);
                positive place;

                if (!hit)
                        break;
                place = (positive)(hit - text);
                hits += (!place || !lcheck_word_byte(text[place - 1])) && (place + length >= size || !lcheck_word_byte(text[place + length]));
                at = place + length;
        }
        return hits;
}

static b32 lcheck_has(const char address_to text, positive length, const char address_to what)
{
        return memory_search((address_any)text, length, (address_any)what, string_length((string_address)what)) != null;
}

// What the statement before a literal says it is for: a call whose text is a key, a log line, assembly or an assert.
static b32 lcheck_context(const char address_to stmt, positive length)
{
        static const char address_to const names[] = {
            "asm", "ASM_", "_Static_assert", "string_equals(", "memory_compare(", "string_has_prefix(", "string_compare", "memory_search(",
            "setting_by_path(", "compositor_word(", "ui_argument(", "program_argument(", "string_table_find(", "word_is(", "string_starts(",
            "compositor_kmsg(", "string_format(log", "log_direct(", "log_error", "host_say(", ".missing =", ".nofont =", ".title =",
            "terminal_said(", "terminal_refused(", "string_copy_bounded((string_address)key", "WINDOW_TRACE", "window_trace_word("};

        for (positive k = 0; k < sizeof(names) / sizeof(names[0]); k++)
                if (lcheck_has(stmt, length, names[k]))
                        return true;
        return false;
}

// Whether a literal, as written between its quotes, is a key, a path, a terminal's escape or the like and so is no text a person reads.
static b32 lcheck_not_text(const char address_to lit, positive length)
{
        b32 letters = false, blank = false;

        for (positive at = 0; at < length; at++)
        {
                if (lit[at] == '\\')
                {
                        at++;
                        if (at < length && lit[at] == 'x')
                                while (at + 1 < length && ((lit[at + 1] | 32) >= 'a' && (lit[at + 1] | 32) <= 'f' || (lit[at + 1] >= '0' && lit[at + 1] <= '9')))
                                        at++;
                        continue;
                }
                letters |= (lit[at] | 32) >= 'a' && (lit[at] | 32) <= 'z';
                blank |= lit[at] == ' ';
        }
        if (!letters || lit[0] == '/' || lit[0] == '.' || lit[0] == '-' || lcheck_has(lit, length, "/proc") || lcheck_has(lit, length, "/sys") ||
            lcheck_has(lit, length, "/dev") || lcheck_has(lit, length, "\\033") || lcheck_has(lit, length, "\\x1b") ||
            lcheck_has(lit, length, "\\n") || lcheck_has(lit, length, "\\t") || lcheck_has(lit, length, "+"))
                return true;
        if (blank)
                return false;
        // A key of a file the program reads (MemTotal:, PATH=) ends in a colon or an equal sign.
        if (lit[length - 1] == ':' || lit[length - 1] == '=')
                return true;
        // A word with no blank: a lower case one is a key or a verb, a capitalised one with lower case after it is a word a person reads.
        return !(lit[0] >= 'A' && lit[0] <= 'Z' && length > 2 && lit[1] >= 'a' && lit[1] <= 'z');
}

// Whether languages/keep says a literal of a file may stay: a line is the file, a tab and the literal as written.
static b32 lcheck_kept(const char address_to keep, positive keep_size, const char address_to file, const char address_to lit, positive length)
{
        positive file_length = string_length((string_address)file);

        // Blanks at either end of a literal or of a line do not count: a piece of a log line starts or ends with one.
        while (length && lit[0] == ' ')
                lit++, length--;
        while (length && lit[length - 1] == ' ')
                length--;
        for (positive at = 0; at < keep_size;)
        {
                positive stop = at, from = at + file_length + 1;

                while (stop < keep_size && keep[stop] != '\n')
                        stop++;
                while (stop > at && keep[stop - 1] == ' ')
                        stop--;
                while (from < stop && keep[from] == ' ')
                        from++;
                if (stop > from && stop - from == length && !memory_compare(keep + at, file, file_length) && keep[at + file_length] == '\t' &&
                    !memory_compare(keep + from, lit, length))
                        return true;
                while (at < keep_size && keep[at] != '\n')
                        at++;
                at++;
        }
        return false;
}

/*
        The tables of a converted file whose words are no English a person reads, each named by the file it is in and the words
        its declaration ends with. A table is left out from those words to the ; that ends it, and a literal anywhere else in
        the file is held as before, one of another table among them.
*/
static const struct
{
        const char address_to file;
        const char address_to head;
} lcheck_tables[] = {
    // The keymap's own keys: each line is KEYS COMMAND as the keymap file holds it, read by the keymap's parser and shown by no window.
    {"src/canvas/ui/ui.c", "doc_default_keys[] = {"},
    // The keywords and the types of the languages a code view colours, which a tokenizer compares with the bytes of the file.
    {"src/canvas/ui/ui.c", "const keywords[] = {"},
    {"src/canvas/ui/ui.c", "const types[] = {"},
    // The kernel's words for what a power supply is and what a battery does, which the reader compares with what its files say.
    {"src/canvas/ui/ui.c", "battery_kind_words[] = {"},
    {"src/canvas/ui/ui.c", "battery_status_words[] = {"},
    // Each language's own name for itself, which the Language row shows as it is whichever language is taken, and which the
    // languages rows hold to the @name of its file.
    {"src/canvas/ui/ui.c", "language_names[] = {"},
    // Each keyboard layout by its own name, as the languages are, and the layout's code the keyboard verb takes.
    {"src/canvas/ui/installer.c", "installer_layouts[] = {"},
};

// Whether one of the file's tables that are left out is declared at a place of its text: its head stands there, whole.
static b32 lcheck_table(const char address_to file, const char address_to text, positive at, positive size)
{
        for (positive t = 0; t < sizeof(lcheck_tables) / sizeof(lcheck_tables[0]); t++)
        {
                positive length;

                if (text[at] != lcheck_tables[t].head[0] || (at && lcheck_word_byte(text[at - 1])))
                        continue;
                length = string_length((string_address)lcheck_tables[t].head);
                if (at + length <= size && !memory_compare(text + at, lcheck_tables[t].head, length) &&
                    string_equals((string_address)file, (string_address)lcheck_tables[t].file))
                        return true;
        }
        return false;
}

/*
        Whether a literal is the English of a line of the language table: LANGUAGE_ID(, the id, a comma and a blank are all that
        stands before it on its line. The table is where the English is kept and every language file is made from, so its own
        lines are no literal left; a literal anywhere else on such a line, or on the line after the table, is held as any other.
*/
static b32 lcheck_language_line(const char address_to text, positive line_start, positive from)
{
        positive at = line_start + 12;

        if (from < at + 3 || memory_compare(text + line_start, "LANGUAGE_ID(", 12))
                return false;
        while (at < from && lcheck_word_byte(text[at]))
                at++;
        return at + 3 == from && text[at] == ',' && text[at + 1] == ' ';
}

// The literals of a file that are English text and are not kept, counted, and reported by line when it is converted (report). A
// section the file says is not translated (the settings window's gallery, which is the kit's demo page) is left out, from its
// header comment to the next one, and so is each table lcheck_tables names, from its name to the ; that ends it.
static positive lcheck_left(const char address_to file, const char address_to text, positive size, const char address_to keep, positive keep_size,
                            b32 report)
{
        positive left = 0, line = 1, at = 0, line_start = 0;
        b32 table = false;

        while (at < size)
        {
                positive from, to, stmt;

                if (text[at] == '\n')
                {
                        line++, line_start = ++at;
                        // A section that is not translated: the gallery, up to the next section's header.
                        if (at + 27 < size && !memory_compare(text + at, "/* ---- the gallery ---- */", 27))
                        {
                                const char address_to next = (const char address_to)memory_search((address_any)(text + at + 27), size - at - 27,
                                                                                                   (address_any) "\n/* ---- ", 9);

                                if (!next)
                                        break;
                                for (; text + at < next; at++)
                                        line += text[at] == '\n';
                                line_start = at;
                        }
                        continue;
                }
                if (text[at] == '/' && at + 1 < size && text[at + 1] == '/')
                {
                        while (at < size && text[at] != '\n')
                                at++;
                        continue;
                }
                if (text[at] == '/' && at + 1 < size && text[at + 1] == '*')
                {
                        for (at += 2; at + 1 < size && !(text[at] == '*' && text[at + 1] == '/'); at++)
                                if (text[at] == '\n')
                                        line++, line_start = at + 1;
                        at += 2;
                        continue;
                }
                if (text[at] == '\'')
                {
                        for (at++; at < size && text[at] != '\''; at++)
                                at += text[at] == '\\';
                        at++;
                        continue;
                }
                if (text[at] != '"')
                {
                        // A table that is left out begins at its name and ends at the ; of its declaration.
                        table = text[at] != ';' && (table || lcheck_table(file, text, at, size));
                        at++;
                        continue;
                }
                from = at + 1;
                for (at++; at < size && text[at] != '"'; at++)
                        at += text[at] == '\\';
                to = at++;
                // An #include names a file; any other preprocessor line is judged on its own, and a statement back to the last ; { or }.
                {
                        positive first = line_start;

                        while (first < from && (text[first] == ' ' || text[first] == '\t'))
                                first++;
                        if (text[first] == '#')
                        {
                                if (first + 8 <= size && !memory_compare(text + first, "#include", 8))
                                        continue;
                                stmt = first;
                        }
                        else
                        {
                                stmt = from - 1;
                                while (stmt > 0 && from - stmt < 1600 && text[stmt - 1] != ';' && text[stmt - 1] != '{' && text[stmt - 1] != '}')
                                        stmt--;
                        }
                }
                if (table || lcheck_language_line(text, line_start, from) || lcheck_context(text + stmt, from - 1 - stmt) ||
                    lcheck_not_text(text + from, to - from) || (keep && lcheck_kept(keep, keep_size, file, text + from, to - from)))
                        continue;
                left++;
                if (report)
                        string_format(log, "  an English literal in a converted file: %s:%p\n", (string_address)file, line);
        }
        return left;
}

static fn language_sources_check(void)
{
        positive read = 0, unused = 0, compared = 0, left = 0, keep_size = 0, titles = 0;
        positive keep_bytes = compositor_map_file((string_address) "languages/keep", &keep_size);
        const char address_to keep = (const char address_to)keep_bytes;

        for (positive f = 0; f < sizeof(lcheck_sources) / sizeof(lcheck_sources[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)lcheck_sources[f], &size);
                const char address_to text = (const char address_to)bytes;
                b32 converted = false;

                if (!bytes)
                        continue;
                read++;
                for (positive c = 0; c < sizeof(lcheck_converted) / sizeof(lcheck_converted[0]); c++)
                        converted |= !string_compare_max((string_address)lcheck_sources[f], (string_address)lcheck_converted[c], 64);
                // A window's title is English the kit looks up (ui_app_run, language_of), so each has its keyed line.
                for (positive title = 0; title + 30 < size; title++)
                {
                        static const char address_to const head = ".title = (string_address) \"";
                        positive head_length = string_length((string_address)head), end_at;

                        if (memory_compare(text + title, head, head_length))
                                continue;
                        for (end_at = title + head_length; end_at < size && text[end_at] != '"'; end_at++)
                                ;
                        titles += lcheck_keyed_missing(text + title + head_length, end_at - title - head_length);
                }
                // A lookup is never compared: no line that asks for a word also tests a text.
                for (positive start = 0; start < size;)
                {
                        positive stop = start;

                        while (stop < size && text[stop] != '\n')
                                stop++;
                        // A line that says "// shown" compares two texts as they are shown, which a menu's label is held to.
                        if ((lcheck_has(text + start, stop - start, "WORDS(") || lcheck_has(text + start, stop - start, "language_of(") ||
                             lcheck_has(text + start, stop - start, "language_text(")) &&
                            (lcheck_has(text + start, stop - start, "string_equals(") || lcheck_has(text + start, stop - start, "memory_compare(")) &&
                            !lcheck_has(text + start, stop - start, "// shown"))
                        {
                                compared++;
                                string_format(log, "  a language lookup is compared as a key in %s\n", (string_address)lcheck_sources[f]);
                        }
                        start = stop + 1;
                }
                // The files not converted yet are counted, for whoever sweeps them next; a converted one has none.
                {
                        positive owned = lcheck_left(lcheck_sources[f], text, size, keep, keep_bytes ? keep_size : 0, converted);

                        left += converted ? owned : 0;
                        string_format(log, "  language inventory: %s %s, %p owned literals %s\n", (string_address)lcheck_sources[f],
                                      (string_address)(converted ? "converted" : "not converted"), owned,
                                      (string_address)(converted ? "left" : "to convert"));
                }
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        // Every id is named by code, as WORDS(id) or as LANGUAGE_id.
        for (positive id = 0; id < LANGUAGE_COUNT; id++)
        {
                char said[96] = "WORDS(", named[96] = "LANGUAGE_";
                positive found = 0;

                string_append_bounded((string_address)said, (string_address)language_table[id].name, sizeof(said) - 2);
                string_append_bounded((string_address)said, (string_address) ")", sizeof(said));
                string_append_bounded((string_address)named, (string_address)language_table[id].name, sizeof(named));
                for (positive f = 0; f < sizeof(lcheck_sources) / sizeof(lcheck_sources[0]); f++)
                {
                        positive size = 0, bytes = compositor_map_file((string_address)lcheck_sources[f], &size);

                        if (!bytes)
                                continue;
                        found += lcheck_tokens((const char address_to)bytes, size, named) + check_hits((const char address_to)bytes, size, said);
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
                }
                if (!found)
                {
                        unused++;
                        string_format(log, "  a language id is never used: %s\n", (string_address)language_table[id].name);
                }
        }
        if (keep_bytes)
                compositor_call(syscall(munmap), keep_bytes, keep_size, 0, 0, 0, 0);
        if (!read)
        {
                string_format(log, "  NOT RUN: the language sources, the sources are not here\n");
                return;
        }
        check("language: every id of the table is named by code", !unused && read == sizeof(lcheck_sources) / sizeof(lcheck_sources[0]));
        check("language: every window's title is a text the kit looks up, and English has its keyed line", !titles);
        check("language: no lookup is compared as a key, so a translated text is never what code tests", !compared);
        check("language: a converted file has no English literal left that is no key, path, log line or assembly", !left);
}

static unsigned int lcheck_heard;

static fn lcheck_listener(void)
{
        lcheck_heard++;
}

static fn language_runtime_check(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        const char address_to held;
        const char address_to data = "Settings";
        char text[64];

        // English is the table: the same pointers, nothing read, and a data text comes back as it went in.
        language_take(0);
        check("language: English asks the table itself, and a data text passes through as it came",
              WORDS(menu_copy) == language_table[LANGUAGE_menu_copy].english && language_of(data) == data);
        // The pin: English as the table has it is languages/en.txt, and one changed letter of one text is seen by the comparison.
        if (lcheck_load((string_address) "languages/en.txt", &lcheck_english))
        {
                check("language: languages/en.txt is the table's English, every id and every stamp", !lcheck_identity(&lcheck_english, -1) && !lcheck_english.bad);
                check("language: a changed letter of one English text is seen, once, and of each text in turn",
                      lcheck_identity(&lcheck_english, 0) == 1 && lcheck_identity(&lcheck_english, LANGUAGE_COUNT - 1) == 1);
                lcheck_close(&lcheck_english);
        }
        // Taking a language reads its file; every word changes at once, a text the file lacks stays English, and the listeners hear.
        language_listen(lcheck_listener);
        table_at->language = 'd' | 'e' << 8;
        ui_kit_language_take(table_at);
        check("language: a window takes the table's language, and its ids come out of the file",
              !string_compare_max((string_address)WORDS(menu_copy), (string_address) "&Kopieren", 10) && lcheck_heard == 1);
        check("language: a data text comes out of the file by its English, and one the file lacks stays English",
              !string_compare_max((string_address)language_of(data), (string_address) "Einstellungen", 14) &&
                  language_of("no such text") != null && !string_compare_max((string_address)language_of("no such text"), (string_address) "no such text", 13));
        ui_kit_language_take(table_at);
        check("language: the same code again changes nothing and tells nobody", lcheck_heard == 1);
        held = WORDS(menu_cut);
        table_at->language = 'f' | 'r' << 8;
        ui_kit_language_take(table_at);
        check("language: a change is taken at once, and the words a window still holds from the language it left are whole",
              !string_compare_max((string_address)WORDS(menu_copy), (string_address) "&Copier", 10) && lcheck_heard == 2 &&
                  !string_compare_max((string_address)held, (string_address) "A&usschneiden", 14));
        language_format(text, sizeof(text), "%2$s: %1$d (%%)", (long)-42, (string_address) "x");
        check("language: placeholders are filled in the order the text names them, numbers signed, a percent sign escaped",
              !string_compare_max((string_address)text, (string_address) "x: -42 (%)", 11));
        {
                char small[4];
                positive made = language_format(small, sizeof(small), "%s", (string_address) "\xd8\xa7\xd8\xa8");

                check("language: a text cut at its room ends at a whole character, never in the middle of one",
                      made == 2 && (p8)small[0] == 0xd8 && (p8)small[1] == 0xa7 && small[2] == 0);
        }
        table_at->language = 'e' | 'n' << 8;
        ui_kit_language_take(table_at);
        check("language: back to English the table answers again, with nothing left of the language before",
              WORDS(menu_copy) == language_table[LANGUAGE_menu_copy].english && language_of(data) == data && lcheck_heard == 3);
        table_at->language = 'x' | 'x' << 8;
        ui_kit_language_take(table_at);
        check("language: a code with no file is English, and no word of it is blank",
              WORDS(menu_copy) == language_table[LANGUAGE_menu_copy].english && language_of(data) == data);
        // The table's word is a peer's to write: bytes that are no two or three letters (here ".." and "/", which would make the path
        // ".../language/../.txt") are English, and no file is named by them.
        table_at->language = '.' | '.' << 8 | '/' << 16;
        ui_kit_language_take(table_at);
        check("language: a table word that is no code (bytes that would make a path) is English, and no file is named by it",
              language.code == 0 && WORDS(menu_copy) == language_table[LANGUAGE_menu_copy].english);
        table_at->language = 0;
        ui_kit_language_take(table_at);
        // The compositor's side: a change of the code counts the table's theme up once, the same code not at all.
        {
                struct windows address_to saved = compositor.table;
                unsigned int before;

                compositor.table = table_at;
                before = table_at->themed;
                compositor_language_publish(windows_language_code("de", 2));
                compositor_language_publish(windows_language_code("de", 2));
                check("language: the compositor publishes the code into the table and knocks the windows once for a change",
                      table_at->language == ('d' | 'e' << 8) && table_at->themed == before + 1);
                compositor_language_publish(windows_language_code("default", 7));
                check("language: a word that is not a code is English, and only two or three lower case letters are a code",
                      table_at->language == 0 && table_at->themed == before + 2 && !windows_language_code("EN", 2) && !windows_language_code("e", 1) &&
                          !windows_language_code("de1", 3) && windows_language_code("en", 2) == LANGUAGE_ENGLISH);
                compositor.table = saved;
        }
}

#if MOONWATER_UI_EDITOR
/*
        The editor's words. The status bar's text, made by editor_status_text from the table, is the text it was made of before
        there was a table, written out here the way it was (the reference), for every encoding, line end, language and indent,
        selection and caret place: English is the same bytes. And a language taken says it in that language, the bar and a
        status line alike.
*/
static positive editor_status_reference(char address_to text, struct doc address_to doc)
{
        p64 caret = doc->selections[doc->primary].active, line = doc_line_of(doc, caret);
        positive at = 0, selected = 0;

        for (positive s = 0; s < doc->selection_count; s++)
                selected += doc_sel_end(&doc->selections[s]) - doc_sel_start(&doc->selections[s]);
        at += (positive)string_length(string_copy((string_address)text, (string_address) "Ln "));
        at += positive_into_string((string_address)text + at, (positive)(line + 1));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address) ", Col "));
        at += positive_into_string((string_address)text + at, (positive)(doc_column(doc, caret) + 1));
        if (selected)
        {
                at += (positive)string_length(string_copy((string_address)text + at, (string_address) "  ("));
                at += positive_into_string((string_address)text + at, selected);
                at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->selection_count > 1 ? " selected, carets)" : " selected)")));
        }
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->bom ? "    UTF-8 with BOM    " : "    UTF-8    ")));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->eol == DOC_EOL_CRLF ? "CRLF    " : doc->eol == DOC_EOL_CR ? "CR    " : "LF    ")));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)doc_languages[doc->language].name));
        at += (positive)string_length(string_copy((string_address)text + at, (string_address)(doc->insert_spaces ? "    Spaces: " : "    Tab: ")));
        at += positive_into_string((string_address)text + at, doc->tab_size);
        text[at] = 0;
        return at;
}

static fn editor_words_check(void)
{
        static struct ui_text view;
        static const unsigned char eols[3] = {DOC_EOL_LF, DOC_EOL_CRLF, DOC_EOL_CR};
        char now[256], before[256];
        positive different = 0, made = 0;
        struct doc address_to doc = &view.doc;

        language_take(0);
        ui_text_open(&view, UI_TEXT_CODE, (const p8 address_to) "alpha beta\ngamma delta\n", 23, ui_text_keymap());
        for (unsigned int variant = 0; variant < 216; variant++)
        {
                doc->bom = variant & 1, doc->eol = eols[variant / 2 % 3], doc->insert_spaces = variant / 6 & 1;
                doc->language = variant / 12 % 3, doc->tab_size = 2 + variant / 36;
                doc_select_set(doc, 0, variant % 5);
                if (variant % 7 == 0)
                        doc_select_add(doc, 11, 16);
                editor_status_text(now, sizeof(now), doc);
                editor_status_reference(before, doc);
                made++;
                different += !!string_compare_max((string_address)now, (string_address)before, sizeof(now));
        }
        check("editor words: the status bar says what it said before the table, over every encoding, line end, language, indent and selection",
              made == 216 && !different);
        doc->bom = 0, doc->eol = DOC_EOL_LF, doc->insert_spaces = 1, doc->language = 0, doc->tab_size = 4;
        doc_select_set(doc, 0, 0);
        language_take('d' | 'e' << 8);
        editor_status_text(now, sizeof(now), doc);
        check("editor words: in German the same bar says its words in German",
              !string_compare_max((string_address)now, (string_address) "Z 1, Sp 1    UTF-8    LF    Nur-Text    Leerzeichen: 4", sizeof(now)));
        editor.count = 0, editor.status[0] = 0;
        editor_open((string_address) "/dev/null");
        check("editor words: a status line is said in the language taken", !string_compare_max((string_address)editor.status, (string_address) "keine normale Datei: abgelehnt", 64));
        language_take(0);
        editor.status[0] = 0;
        editor_open((string_address) "/dev/null");
        check("editor words: and in English once it is taken again", !string_compare_max((string_address)editor.status, (string_address) "not a regular file: refused", 64));
        ui_text_close(&view);
}
#endif

#if MOONWATER_UI_LAUNCHER
/*
        The launcher's words: a desktop program listed by its name and where it is from, the command row, and what a typed
        text names. English is what it said before the table (these are the old literals); a language taken names the programs
        in its words and keeps the English names working too.
*/
static fn launcher_words_check(void)
{
        struct launcher_entry window = {LAUNCHER_WINDOW, 0}, command = {LAUNCHER_COMMAND, 0};
        char label[64];
        const char address_to note;

        language_take(0);
        launcher_label(&window, label, sizeof(label), &note);
        check("launcher words: a desktop program is its English name, from the desktop",
              !string_compare_max((string_address)label, (string_address) "Settings", 9) && !string_compare_max((string_address)note, (string_address) "desktop", 8));
        string_copy_bounded((string_address)launcher.text, (string_address) "ls -l", sizeof(launcher.text));
        launcher_label(&command, label, sizeof(label), &note);
        check("launcher words: the command row is Run, one blank and the text, from the shell",
              !string_compare_max((string_address)label, (string_address) "Run ls -l", 10) && !string_compare_max((string_address)note, (string_address) "shell", 6));
        check("launcher words: sett names Settings and zzz names nothing",
              launcher_program_rank("sett", 4, &launcher_programs[0]) == 2 && !launcher_program_rank("zzz", 3, &launcher_programs[0]));
        language_take('d' | 'e' << 8);
        launcher_label(&window, label, sizeof(label), &note);
        check("launcher words: in German the program and where it is from say so",
              !string_compare_max((string_address)label, (string_address) "Einstellungen", 14) && !string_compare_max((string_address)note, (string_address) "Desktop", 8));
        launcher_label(&command, label, sizeof(label), &note);
        check("launcher words: and the command row says Ausf\xc3\xbchren, one blank and the text", !string_compare_max((string_address)label, (string_address) "Ausf\xc3\xbchren ls -l", 19));
        check("launcher words: einst names Settings in German, and the English settings still does",
              launcher_program_rank("einst", 5, &launcher_programs[0]) == 2 && launcher_program_rank("settings", 8, &launcher_programs[0]) == 3 &&
                  launcher_program_rank("ajustes", 7, &launcher_programs[0]) == 0);
        language_take(0);
}
#endif

#if MOONWATER_UI_TASKBAR
/*
        The tray's words: the weekday, the month, AM or PM and the battery's state, which a format of the person's puts in the
        tray. English is the three-letter names it always had (written out here), and a language taken says them in its own.
*/
static fn taskbar_words_check(void)
{
        static const char address_to const days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char address_to const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        struct windows address_to saved = compositor.table;
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        positive wrong = 0;
        char word[32];
        int at;
        tm broken;

        compositor.table = table_at;
        table_at->language = 0;
        memory_zero(&broken, sizeof(broken));
        taskbar.tray_custom = 1;
        taskbar.battery_item = 1;
        for (int day = 0; day < 7; day++)
        {
                broken.tm_wday = day, at = 0;
                taskbar_tray_one(word, &at, sizeof(word), 10, &broken);
                wrong += !!string_compare_max((string_address)word, (string_address)days[day], 8);
        }
        for (int month = 0; month < 12; month++)
        {
                broken.tm_mon = month, at = 0;
                taskbar_tray_one(word, &at, sizeof(word), 11, &broken);
                wrong += !!string_compare_max((string_address)word, (string_address)months[month], 8);
        }
        for (int hour = 0; hour < 24; hour += 11)
        {
                broken.tm_hour = hour, at = 0;
                taskbar_tray_one(word, &at, sizeof(word), 6, &broken);
                wrong += !!string_compare_max((string_address)word, (string_address)(hour < 12 ? "AM" : "PM"), 3);
        }
        for (int charging = 0; charging < 2; charging++)
        {
                taskbar.battery_charging = charging, at = 0;
                taskbar_tray_one(word, &at, sizeof(word), 2, &broken);
                wrong += !!string_compare_max((string_address)word, (string_address)(charging ? "charging" : "discharging"), 12);
        }
        check("taskbar words: the weekdays, the months, AM and PM and the battery's state are the English they were", !wrong);
        table_at->language = 'd' | 'e' << 8;
        language_take(table_at->language);
        broken.tm_wday = 0, at = 0;
        taskbar_tray_one(word, &at, sizeof(word), 10, &broken);
        wrong = !!string_compare_max((string_address)word, (string_address) "So", 8);
        broken.tm_mon = 2, at = 0;
        taskbar_tray_one(word, &at, sizeof(word), 11, &broken);
        wrong += !!string_compare_max((string_address)word, (string_address) "M\xc3\xa4r", 8);
        taskbar.battery_charging = 1, at = 0;
        taskbar_tray_one(word, &at, sizeof(word), 2, &broken);
        wrong += !!string_compare_max((string_address)word, (string_address) "l\xc3\xa4" "dt", 8);
        check("taskbar words: in German the same tokens say the German weekday, month and state", !wrong);
        // A tray rendered takes the language the table says, as the compositor publishes it.
        taskbar.tray_custom = 1;
        string_copy_bounded((string_address)taskbar.tray_format, (string_address) "{date.weekday} {date.monthname}", sizeof(taskbar.tray_format));
        table_at->language = 'f' | 'r' << 8;
        taskbar_tray_render();
        check("taskbar words: a tray drawn takes the language of the table, not the one it drew in before", language.code == ('f' | 'r' << 8) && taskbar.tray_chars > 0);
        table_at->language = 0;
        taskbar_tray_render();
        check("taskbar words: and English again when the table says none", !language.code);
        // The tray is drawn at once when the language changes, and not for a tick that changes neither the minute nor the language.
        taskbar.minute = taskbar_minute_now();
        taskbar_tick();
        check("taskbar words: a tick of the same minute in the same language draws nothing", taskbar_tick() == 0);
        table_at->language = 'd' | 'e' << 8;
        check("taskbar words: a language taken is drawn at that tick, and not at the minute's end", taskbar_tick() == 1 && language.code == ('d' | 'e' << 8));
        table_at->language = 0;
        taskbar_tick();
        taskbar.tray_custom = 0, taskbar.battery_item = 0, taskbar.battery_charging = 0;
        taskbar.tray_format[0] = 0;
        compositor.table = saved;
}
#endif

/*
        The terminal's words: the line printed in its window when a program it was asked to run has ended (how it did, if it
        exited, and what closes the window). English is the line it printed before the table, written out here as the reference
        over every exit status; a language taken says it in that language.
*/
static positive terminal_exit_reference(p8 address_to line)
{
        positive at;

        string_copy((string_address)line, (string_address) "\x1b[0m\r\n[");
        at = string_length(line);
        if (!(term_exit_status & 0x7f))
        {
                string_copy((string_address)line + at, (string_address) "exited with status ");
                at += string_length(line + at);
                at += positive_into_string((string_address)line + at, (term_exit_status >> 8) & 0xff);
                string_copy((string_address)line + at, (string_address) ", ");
                at += 2;
        }
        string_copy((string_address)line + at, (string_address) "press a key to close]");
        return at + string_length(line + at);
}

static fn terminal_words_check(void)
{
        p8 now[256], before[256];
        positive wrong = 0, made = 0;
        int saved = (int)term_exit_status;

        language_take(0);
        for (unsigned int status = 0; status < 0x10000; status += 0x1d)
        {
                term_exit_status = (int)status;
                wrong += terminal_exit_line(now, sizeof(now) - 1) != terminal_exit_reference(before) ||
                         string_compare_max((string_address)now, (string_address)before, sizeof(now));
                made++;
        }
        check("terminal words: the line printed when its program ended is what it was, for every exit status and signal", made > 2000 && !wrong);
        language_take('d' | 'e' << 8);
        term_exit_status = 3 << 8;
        terminal_exit_line(now, sizeof(now) - 1);
        check("terminal words: in German it says so, with the status", !string_compare_max((string_address)now, (string_address) "\x1b[0m\r\n[beendet mit Status 3, zum Schlie\xc3\x9f" "en eine Taste dr\xc3\xbc" "cken]", sizeof(now)));
        term_exit_status = 9;
        terminal_exit_line(now, sizeof(now) - 1);
        check("terminal words: and a program a signal ended gets the line without a status", !string_compare_max((string_address)now, (string_address) "\x1b[0m\r\n[zum Schlie\xc3\x9f" "en eine Taste dr\xc3\xbc" "cken]", sizeof(now)));
        language_take(0);
        term_exit_status = saved;
}

#if MOONWATER_UI_SETTINGS
// The row whose label is label, or -1.
static int lcheck_row(const char address_to label)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (string_equals((string_address)settings_rows[at].label, (string_address)label))
                        return (int)at;
        return -1;
}

/*
        The Settings window's words that are the registry's: the groups, the labels, the words of the choices and the tabs.
        In English each is the registry's own text (the same pointer, so nothing is looked up); in every language a choice's
        word that the menu shows comes back, through settings_word_of, as the English word the verb takes (so the labels of
        one row are all different), the tabs of the theme and the motion have one word each, and the filter finds a row by
        its words in the language and in English.
*/
static fn settings_words_check(void)
{
        char word[SETTINGS_VALUE], back[SETTINGS_VALUE];
        positive english_wrong = 0, round = 0, tabs = 0, blank = 0, checked = 0;
        int theme = lcheck_row("Theme"), motion = lcheck_row("Motion"), name = lcheck_row("Name"), language_row = lcheck_row("Language");

        language_take(0);
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                english_wrong += settings_label_shown((int)at) != settings_rows[at].label;
                // (The screen modes row says Biggest picture and Recommended for its words, in English too; the on-screen keyboard's
                // choice says Automatic for auto on every row, and the on-screen keyboard's off and on say Always off and Always on,
                // settings_choice_said, with the verb's words kept.)
                if (settings_rows[at].kind == SETTINGS_CHOICE && settings_rows[at].choices && !(settings_rows[at].flags & SETTINGS_LANGUAGE) &&
                    !settings_is_modes((int)at))
                        for (positive k = 0; settings_word(settings_rows[at].choices, k, word, sizeof(word)); k++)
                                english_wrong += !!string_compare_max((string_address)settings_label_of((int)at, word),
                                                                      (string_address)settings_choice_said((int)at, word), sizeof(word));
        }
        for (int group = 0; group < settings_groups(); group++)
                english_wrong += settings_group_text(group) != settings_group_name(group);
        english_wrong += theme < 0 || motion < 0 || !string_equals((string_address)settings_tab_words(theme), (string_address) "light dark Automatic");
        check("settings words: in English a row's label, a group's name, a choice's word and the tabs' words are the registry's own",
              !english_wrong && theme >= 0 && motion >= 0 && name >= 0 && language_row >= 0);
        for (positive l = 0; l < sizeof(lcheck_codes) / sizeof(lcheck_codes[0]); l++)
        {
                const char address_to tabs_said;

                language_take((unsigned int)lcheck_codes[l].code[0] | (unsigned int)lcheck_codes[l].code[1] << 8);
                for (positive at = 0; at < SETTINGS_ROWS; at++)
                {
                        blank += !settings_label_shown((int)at)[0];
                        if (settings_rows[at].kind != SETTINGS_CHOICE || !settings_rows[at].choices || (settings_rows[at].flags & SETTINGS_LANGUAGE))
                                continue;
                        for (positive k = 0; settings_word(settings_rows[at].choices, k, word, sizeof(word)); k++)
                        {
                                if (word[0] >= '0' && word[0] <= '9')
                                        continue;
                                settings_word_of((int)at, settings_label_of((int)at, word), back, sizeof(back));
                                round += !!string_compare_max((string_address)back, (string_address)word, sizeof(back));
                                checked++;
                        }
                }
                {
                        positive blanks = 0;

                        tabs_said = settings_tab_words(theme);
                        for (positive each = 0; tabs_said[each]; each++)
                                blanks += tabs_said[each] == ' ';
                        tabs += blanks != 2 || tabs_said[0] == ' ';
                        blanks = 0, tabs_said = settings_tab_words(motion);
                        for (positive each = 0; tabs_said[each]; each++)
                                blanks += tabs_said[each] == ' ';
                        tabs += blanks != 1 || tabs_said[0] == ' ';
                }
        }
        check("settings words: in every language a label is never empty, and each word a choice's menu shows comes back as the word the verb takes",
              !blank && !round && checked > 500);
        /*
                A row's line in the window (settings_label_window): a number with a unit shows its label up to the bracket the command line
                is told its unit in, in English and in every language, which keeps the bracket at the end; every other label is whole. And
                every row of the Input group but the pointer's size (its label says it) says what it does, a text of its own in every language.
        */
        {
                positive bracketed = 0, cut = 0, whole = 0, said = 0, unsaid = 0, untranslated = 0;
                int input = settings_groups_named("Input");

                for (int l = -1; l < (int)(sizeof(lcheck_codes) / sizeof(lcheck_codes[0])); l++)
                {
                        language_take(l < 0 ? 0 : (unsigned int)lcheck_codes[l].code[0] | (unsigned int)lcheck_codes[l].code[1] << 8);
                        for (positive at = 0; at < SETTINGS_ROWS; at++)
                        {
                                const char address_to label = settings_label_shown((int)at), address_to line = settings_label_window((int)at);
                                positive length = string_length((string_address)line);

                                const char address_to english = settings_rows[at].label;

                                if (settings_rows[at].unit && english[string_length((string_address)english) - 1] == ')')
                                {
                                        bracketed++;
                                        cut += length && length < string_length((string_address)label) && !string_compare_max((string_address)line, (string_address)label, length) &&
                                               !string_first_of((string_address)line, '(') && label[length] == ' ' && label[length + 1] == '(';
                                }
                                else
                                        whole += line == label;
                                if (settings_group_of((int)at) != input)
                                        continue;
                                said += settings_rows[at].about && settings_rows[at].about[0];
                                unsaid += !settings_rows[at].about || !settings_rows[at].about[0];
                                untranslated += l >= 0 && settings_rows[at].about && language_of(settings_rows[at].about) == settings_rows[at].about;
                        }
                }
                language_take(0);
                string_format(log, "  settings words: %p lines with a unit, %p of them cut at the bracket; %p rows say what they do, %p do not, %p untranslated\n",
                              bracketed, cut, said, unsaid, untranslated);
                check("settings words: a number with a unit is listed as its label up to the bracket its unit is in, in English and in every language, and every other label whole",
                      bracketed >= 17 * 17 && cut == bracketed && whole == 17 * (SETTINGS_ROWS - bracketed / 17));
                check("settings words: every row of the Input group but its pointer's size, which its label says, says what it does, in a text of its own in every language",
                      said > 0 && unsaid == 17 && !untranslated);
        }
        check("settings words: the tabs of the theme and of the motion are three and two words, one each", !tabs);
        language_take('d' | 'e' << 8);
        settings_word_of(theme, settings_label_of(theme, "dark"), back, sizeof(back));
        check("settings words: in German the groups, labels and choices are German, and the theme's dark is shown as Dunkel and taken as dark",
              !string_compare_max((string_address)settings_group_text(0), (string_address) "Ger\xc3\xa4t", 8) &&
                  !string_compare_max((string_address)settings_label_shown(language_row), (string_address) "Sprache", 8) &&
                  !string_compare_max((string_address)settings_label_of(theme, "dark"), (string_address) "Dunkel", 7) && !string_compare_max((string_address)back, (string_address) "dark", 5) &&
                  !string_compare_max((string_address)settings_tab_words(theme), (string_address) "Hell Dunkel Automatisch", 24));
        check("settings words: the filter finds the Name row by its group's word in German and by the English one",
              settings_matches_word(name, "ger\xc3\xa4t", 5) && settings_matches_word(name, "machine", 7) && !settings_matches_word(name, "xyzzy", 5));
        language_take(0);
        check("settings words: back in English the German is gone from the filter and the labels", !settings_matches_word(name, "ger\xc3\xa4t", 5) &&
                                                                                                      settings_label_shown(language_row) == settings_rows[language_row].label);
}
#endif

#if MOONWATER_UI_SETTINGS
/*
        What the Settings window says of a row of a list (a network, a device, the row that keeps one) and what a name may hold:
        English is the words it said before the table, written out here, and a language taken says them in its own. A change of
        language puts away the status line, which holds the words of the language it was said in.
*/
static fn settings_status_check(void)
{
        static const struct
        {
                unsigned int kind, flags;
                const char address_to english;
        } items[] = {
            {SETTINGS_NETWORK, SETTINGS_ITEM_JOINED, "joined: Delete forgets it"},
            {SETTINGS_NETWORK, SETTINGS_ITEM_CANNOT, "moonwater cannot join what this network asks for"},
            {SETTINGS_NETWORK, SETTINGS_ITEM_OPEN, "Enter joins it, and saves it"},
            {SETTINGS_NETWORK, SETTINGS_ITEM_SAVED, "saved: Enter types its passphrase again, Delete forgets it"},
            {SETTINGS_NETWORK, 0, "Enter asks for its passphrase and joins it"},
            {SETTINGS_DEVICE, 0, "kept by its name: Delete forgets it"},
            {SETTINGS_ADD, 0, "Enter keeps the name of a device; the radio goes on for what pairs"},
        };
        struct settings_item item;
        positive wrong = 0;

        language_take(0);
        for (positive at = 0; at < sizeof(items) / sizeof(items[0]); at++)
        {
                memory_zero(&item, sizeof(item));
                item.kind = (unsigned char)items[at].kind, item.flags = (unsigned char)items[at].flags;
                wrong += !!string_compare_max((string_address)settings_item_text(&item), (string_address)items[at].english, 128);
        }
        wrong += !!string_compare_max((string_address)settings_name_accepts('!'), (string_address) "a name is letters, digits and hyphens", 64) ||
                 settings_name_accepts('a') != null;
        check("settings status: what a row of a list says when it is pressed, and what a name may hold, are the English they were", !wrong);
        language_take('d' | 'e' << 8);
        memory_zero(&item, sizeof(item));
        item.kind = SETTINGS_DEVICE;
        check("settings status: in German a row of a list and a name's rule say German",
              !string_compare_max((string_address)settings_item_text(&item), (string_address) "nach Namen gemerkt: Delete vergisst es", 64) &&
                  !string_compare_max((string_address)settings_name_accepts('!'), (string_address) "ein Name besteht aus Buchstaben, Ziffern und Bindestrichen", 64));
        settings_say((string_address) "said in German");
        settings_language();
        check("settings status: a change of language puts away a status line made in the language before", !settings.status[0]);
        language_take(0);
}
#endif

#if MOONWATER_UI_SETTINGS
/*
        The metric line changes inside the window after a page was made, and then the card goes quiet: the page is made again once the window
        has passed (a change is not lost to the window, and nothing else needs to move for it).
*/
static fn settings_metric_check(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        b64 t0 = 100000000000ll;

        settings.ui.scale = 100;
        settings_size(700, 520);
        settings_group_choose(0);
        windows_scratch_zero(table_at);
        table_at->gpu_on = 1, table_at->gpu_able = 1, table_at->gpu_state = WINDOWS_GPU_UP;
        settings.own.table = table_at;
        settings_board_due = 0, settings_metric_at = 0;
        ui_kit.now = ui_kit.test_clock = t0;
        settings_board_tick();
        check("settings metric: the first board read makes the page at its time", settings_metric_at == (positive)t0);
        table_at->metric_card_mean = 7;
        ui_kit.now = ui_kit.test_clock = t0 + 1000000000ll;
        settings_board_tick();
        check("settings metric: a change inside the window does not make the page again yet", settings_metric_at == (positive)t0);
        ui_kit.now = ui_kit.test_clock = t0 + 11000000000ll;
        settings_board_tick();
        check("settings metric: once the window has passed on a quiet card the page is made again with the changed line",
              settings_metric_at == (positive)(t0 + 11000000000ll));
}
#endif

#if MOONWATER_UI_MIXER
/*
        A page that is read and has not moved stays mapped: the mixer looks for its server again only when the page has truly stopped, so a quiet
        page is not unmapped and mapped again every two seconds. The page is a mapping of its own (the unmap is the kernel's, of its own pages).
*/
static fn mixer_quiet_check(void)
{
        const b64 t0 = 100000000000ll, ms = 1000000ll;
        long page_map = ui_map(sizeof(struct audio_state)), big_map = ui_map(sizeof(struct mixer_big));
        b64 at;
        b32 kept = true;

        if ((page_map < 0 && page_map > -4096) || (big_map < 0 && big_map > -4096))
        {
                check("mixer quiet: the page and the state map", false);
                return;
        }
        mixer_fixture_streams();
        *(struct audio_state address_to)page_map = mixer_fixture_page;
        memory_zero(&mixer, sizeof(mixer));
        mixer.big = (struct mixer_big address_to)big_map;
        mixer.page = (const volatile struct audio_state address_to)page_map;
        mixer.page_path = "/nonexistent/audio_state";
        mixer.socket_path = "/nonexistent/native";
        mixer.have = audio_state_read(mixer.page, &mixer.big->copy);
        mixer.listed = true;
        mixer.seen_at = t0;
        for (at = t0; at <= t0 + 5000 * ms; at += 100 * ms)
        {
                ui_kit.now = ui_kit.test_clock = at;
                mixer_poll((b64)at);
                if (!mixer.page || !mixer.have)
                        kept = false;
        }
        check("mixer quiet: a page that has not moved is kept mapped for 5 s, and not mapped again every 2 s",
              kept && mixer.page == (const volatile struct audio_state address_to)page_map);
}
#endif

#if MOONWATER_UI_SETTINGS
/*
        The GPU's status line in Settings says counts of presents, and every present moves them: the board is made again when the status or the
        card's share of the presents moves, not when a count alone does (a page made again presents, and that moves the counts again).
*/
static fn settings_board_check(void)
{
        struct windows address_to saved = settings.own.table;
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        unsigned int key;

        windows_scratch_zero(table_at);
        table_at->gpu_on = 1, table_at->gpu_able = 1, table_at->gpu_state = WINDOWS_GPU_UP;
        settings.own.table = table_at;
        table_at->gpu_drawn = 50, table_at->gpu_processor = 0;
        settings_gpu_sample();
        key = settings_gpu_key;
        table_at->gpu_drawn = 60;
        settings_gpu_sample();
        check("settings board: a count that moves alone (all on the card) keeps the status key, so the page is not made again", settings_gpu_key == key && settings_gpu_shown_total == 60);
        table_at->gpu_processor = 40;
        settings_gpu_sample();
        check("settings board: a share that moves changes the status key", settings_gpu_key != key);
        table_at->gpu_on = 0;
        settings_gpu_sample();
        key = settings_gpu_key;
        table_at->gpu_drawn += 9, table_at->gpu_processor += 9;
        settings_gpu_sample();
        check("settings board: with canvas gpu off, counts moving alone keep the key too", settings_gpu_key == key);
        settings.own.table = saved;
}
#endif

#if MOONWATER_UI_SETTINGS
/*
        The Display group's words: a screen as the dropdown names it, a scale, a refresh rate, a turn, the screen modes' two words
        and the caption under the map. English is what the window said before the table (written out here), the places of a
        sentence are filled in whatever order a language puts them, and German says them in German.
*/
static fn settings_display_check(void)
{
        char label[96];
        int modes = lcheck_row("Screen modes"), held = settings.map.graph.held, moving = settings.map.graph.moving, keys = settings.map.keys;
        positive wrong = 0;

        language_take(0);
        settings_screen_label(2, "DP-1", "Dell U2720Q", label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "Screen 2: Dell U2720Q [DP-1]", 64);
        settings_screen_label(1, "HDMI-A-2", "", label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "Screen 1: HDMI-A-2", 64);
        settings_scale_label(150, 1, label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "150% (recommended)", 64);
        settings_scale_label(75, 0, label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "75%", 64);
        settings_rate_label(60, 1, label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "60 Hz (recommended)", 64);
        settings_rate_label(144, 0, label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "144 Hz", 64);
        wrong += !!string_compare_max((string_address)settings_turn_label(90), (string_address) "Turned right", 32) ||
                 !!string_compare_max((string_address)settings_turn_label(0), (string_address) "Normal", 32) ||
                 !!string_compare_max((string_address)settings_label_of(modes, "largest"), (string_address) "Biggest picture", 32) ||
                 !!string_compare_max((string_address)settings_label_of(modes, "preferred"), (string_address) "Recommended", 32);
        settings.map.graph.held = -1, settings.map.keys = 0;
        wrong += !!string_compare_max((string_address)settings_map_hint(0), (string_address) "No screen is connected.", 64) ||
                 !!string_compare_max((string_address)settings_map_hint(1), (string_address) "Only one screen is connected.", 64) ||
                 !!string_compare_max((string_address)settings_map_hint(2), (string_address) "Drag a screen to move it. Drag the grid to pan, scroll to zoom.", 128);
        settings.map.keys = 1;
        wrong += !!string_compare_max((string_address)settings_map_hint(2), (string_address) "Arrows choose a screen. Shift and an arrow moves it. Enter identifies. Down leaves.", 96);
        settings.map.graph.held = 0, settings.map.graph.moving = 1;
        wrong += !!string_compare_max((string_address)settings_map_hint(2), (string_address) "Let go to place the screen", 64);
        check("settings display: a screen, a scale, a rate, a turn, the modes' words and the map's caption are the English they were", !wrong && modes >= 0);
        settings.map.graph.held = -1, settings.map.keys = 0;
        language_take('d' | 'e' << 8);
        settings_screen_label(2, "DP-1", "Dell U2720Q", label, sizeof(label));
        wrong = !!string_compare_max((string_address)label, (string_address) "Bildschirm 2: Dell U2720Q [DP-1]", 64);
        settings_scale_label(150, 1, label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "150 % (empfohlen)", 64);
        settings_rate_label(60, 1, label, sizeof(label));
        wrong += !!string_compare_max((string_address)label, (string_address) "60 Hz (empfohlen)", 64);
        wrong += !!string_compare_max((string_address)settings_turn_label(90), (string_address) "Nach rechts gedreht", 32) ||
                 !!string_compare_max((string_address)settings_map_hint(2), (string_address) "Einen Bildschirm ziehen, um ihn zu verschieben. Das Raster ziehen, um die Ansicht zu bewegen, scrollen zum Zoomen.", 160);
        {
                char back[SETTINGS_VALUE];

                wrong += !!string_compare_max((string_address)settings_label_of(modes, "largest"), (string_address) "Gr\xc3\xb6\xc3\x9f" "tes Bild", 32);
                settings_word_of(modes, settings_label_of(modes, "preferred"), back, sizeof(back));
                wrong += !!string_compare_max((string_address)back, (string_address) "preferred", 12);
        }
        check("settings display: in German they say German, the modes' words come back as the verb's, and a sentence's places are filled in", !wrong);
        {
                positive broken = 0;

                for (positive l = 0; l < sizeof(lcheck_codes) / sizeof(lcheck_codes[0]); l++)
                {
                        language_take((unsigned int)lcheck_codes[l].code[0] | (unsigned int)lcheck_codes[l].code[1] << 8);
                        settings_scale_label(150, 1, label, sizeof(label));
                        broken += !memory_search((address_any)label, string_length((string_address)label), (address_any) "150", 3);
                        settings_rate_label(144, 0, label, sizeof(label));
                        broken += !memory_search((address_any)label, string_length((string_address)label), (address_any) "144", 3);
                        settings_screen_label(3, "DP-1", "Dell", label, sizeof(label));
                        broken += !memory_search((address_any)label, string_length((string_address)label), (address_any) "[DP-1]", 6) ||
                                  !memory_search((address_any)label, string_length((string_address)label), (address_any) "Dell", 4) ||
                                  !memory_search((address_any)label, string_length((string_address)label), (address_any) "3", 1);
                }
                check("settings display: in every language a scale, a rate and a screen keep their numbers and names", !broken);
        }
        settings.map.graph.held = held, settings.map.graph.moving = moving, settings.map.keys = keys;
        language_take(0);
}
#endif

static fn language_check(void)
{
        font_face regular, bold;
        positive size = 0, bytes;

        if (!lcheck_load((string_address) "languages/en.txt", &lcheck_english))
        {
                string_format(log, "  NOT RUN: the languages, the sources are not here\n");
                return;
        }
        if (program_argument_count() < 5)
                return;
        bytes = compositor_map_file(program_argument(3), &size);
        check("languages: the regular face loads", bytes && font_load(&regular, (const p8 address_to)bytes, size, 0) == 0);
        bytes = compositor_map_file(program_argument(4), &size);
        check("languages: the bold face loads", bytes && font_load(&bold, (const p8 address_to)bytes, size, 0) == 0);
        {
                // The faces the right to left languages are drawn in, from the tree's fonts/ as languages/ is read: Arabic, Hebrew.
                static const string_address path[3][2] = {{null, null},
                                                          {"fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf", "fonts/noto-sans-arabic/NotoSansArabic-Bold.ttf"},
                                                          {"fonts/noto-sans-hebrew/NotoSansHebrew-Regular.ttf", "fonts/noto-sans-hebrew/NotoSansHebrew-Bold.ttf"}};
                b32 loaded = 1;

                for (unsigned int script = LCHECK_ARABIC; script <= LCHECK_HEBREW; script++)
                        for (unsigned int weight = 0; weight < 2; weight++)
                        {
                                bytes = compositor_map_file(path[script][weight], &size);
                                loaded = loaded && bytes && font_load(&lcheck_script_face[script][weight], (const p8 address_to)bytes, size, 0) == 0;
                        }
                check("languages: the Noto Sans Arabic and Hebrew faces, regular and bold, load", loaded);
        }
#if MOONWATER_UI_SETTINGS && MOONWATER_UI_MIXER
        // Before the failures' return: the rows of the settings metric and the mixer's page run whatever failed before them.
        settings_metric_check();
        mixer_quiet_check();
#endif
        if (failures)
                return;
        language_files_check(&regular, &bold);
        language_sources_check();
        lcheck_close(&lcheck_english);
        language_runtime_check();
#if MOONWATER_UI_EDITOR
        editor_words_check();
#endif
#if MOONWATER_UI_LAUNCHER
        launcher_words_check();
#endif
#if MOONWATER_UI_TASKBAR
        taskbar_words_check();
#endif
        terminal_words_check();
#if MOONWATER_UI_SETTINGS
        settings_words_check();
        settings_status_check();
        settings_board_check();
        settings_display_check();
#endif
}
#endif

/*
        One blink for every field: ui.c's ui_text_timeout is the only place a field's caret deadline is folded into a loop's
        timeout, and the three windows with a field each ask it (none reads the engine's tick, or folds the deadline by
        hand). Read from the tree the check runs in, as check_no_hand_typing does; not run where the sources are not.
*/
static fn check_one_blink(void)
{
        static const char address_to const files[] = {"src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/editor.c"};
        static const char address_to const hand[] = {"ui_text_tick(", "(long)(due - now) < timeout"};
        positive read = 0, asked = 0, hand_found = 0, defined = 0;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f], &size);
                const char address_to text = (const char address_to)bytes;

                if (!bytes)
                        continue;
                read++;
                asked += check_hits(text, size, "ui_text_timeout(") + check_hits(text, size, "ui_findbar_timeout(") + check_hits(text, size, "ui_field_tick(") > 0;
                for (positive i = 0; i < sizeof(hand) / sizeof(hand[0]); i++)
                        hand_found += check_hits(text, size, hand[i]);
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        {
                positive size = 0, bytes = compositor_map_file((string_address) "src/canvas/ui/ui.c", &size);

                if (bytes)
                {
                        defined = check_hits((const char address_to)bytes, size, "static long ui_text_timeout(");
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
                }
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: one blink for every field, the sources are not here\n");
                return;
        }
        check("blink: every field's caret deadline is ui_text_timeout in ui.c, asked by each window with a field", read == sizeof(files) / sizeof(files[0]) && asked == read && !hand_found && defined == 1);
}

#if MOONWATER_UI_ANY
/*
        The caret's blink as ui.c draws it: a rounded square wave. Over one cycle it is solid for most of the first half and
        gone for most of the second, each change a smoothstep fade across 15 percent of a half, centred on the change; it never
        jumps, its two halves mirror each other, a key restarts it solid, and the speed off is solid for good. The cycle is
        the person's (ui_kit.caret_speed): 1.6 s, 1.06 s and 0.53 s, and off has none.
*/
// The caret's time in a row: the kit's clock is the time the row drives (ui_kit.now and ui_kit.test_clock together), which the
// caret reads (ui_caret_cover, ui_caret_wake), so a row's time and the caret's are one.
static positive check_text_tick_at(struct ui_text address_to t, positive when, b32 address_to redraw)
{
        ui_kit.now = ui_kit.test_clock = when;
        return ui_text_tick(t, redraw);
}

static unsigned int check_caret_cover_at(positive when)
{
        ui_kit.now = ui_kit.test_clock = when;
        return terminal_caret_cover_at(when);
}

static void check_caret_step_at(positive when, long address_to timeout)
{
        ui_kit.now = ui_kit.test_clock = when;
        terminal_caret_step(when, timeout);
}

static void check_caret_restart_at(positive when)
{
        ui_kit.now = ui_kit.test_clock = when;
        terminal_caret_restart(when);
}

static fn check_caret_blink(void)
{
        static const unsigned int speeds[4] = {UI_CARET_SLOW, UI_CARET_NORMAL, UI_CARET_FAST, UI_CARET_OFF};
        static const b64 cycles[4] = {1600000000ull, 1060000000ull, 530000000ull, 0};
        static struct ui_text t;
        unsigned int saved = ui_kit.caret_speed;
        b64 cycle, step, half, edge;
        positive samples = 0, full = 0, gone = 0, partial = 0, on = 0, off = 0, jumps = 0, rises = 0, falls = 0, mirror = 0;
        unsigned int previous = 0;
        b32 redraw = false, solid = true;
        b64 now = 1000000000000ull;

        for (positive k = 0; k < 4; k++)
        {
                ui_kit.caret_speed = speeds[k];
                check("caret: the speed's cycle is 1.6 s, 1.06 s and 0.53 s, and off has none", ui_caret_cycle() == cycles[k]);
        }
        ui_kit.caret_speed = UI_CARET_NORMAL;
        cycle = ui_caret_cycle();
        half = cycle / 2;
        edge = half * UI_CARET_EDGE_PERCENT / 100;
        step = cycle / 10600;
        for (b64 q = 0; q < cycle; q += step)
        {
                unsigned int cover = ui_caret_wave(q, cycle);

                samples++;
                full += cover == UI_CARET_FULL;
                gone += cover == 0;
                partial += cover > 0 && cover < UI_CARET_FULL;
                on += cover >= UI_CARET_FULL / 2;
                off += cover < UI_CARET_FULL / 2;
                if (samples > 1)
                {
                        unsigned int change = cover > previous ? cover - previous : previous - cover;

                        jumps += change > 1;
                        rises += cover > previous;
                        falls += cover < previous;
                }
                if (q < half)
                        mirror += ui_caret_wave(q, cycle) + ui_caret_wave(q + half, cycle) != UI_CARET_FULL;
                previous = cover;
        }
        // The integer cover rounds the first and last few samples of each fade to solid or gone, so the fades are a little short.
        check("caret: solid for 42.5 percent of a cycle, gone for 42.5, and the fades take the other 15",
              (full * 1000 / samples) >= 415 && (full * 1000 / samples) <= 435 && (gone * 1000 / samples) >= 415 &&
              (gone * 1000 / samples) <= 440 && (partial * 1000 / samples) >= 135 && (partial * 1000 / samples) <= 160);
        check("caret: on for half the cycle and off for the other half", (on * 1000 / samples) >= 495 && (on * 1000 / samples) <= 505 &&
              (off * 1000 / samples) >= 495 && (off * 1000 / samples) <= 505);
        check("caret: the fades are smooth: no step of more than one level in a hundred microseconds", jumps == 0);
        // One rising fade and one falling fade a cycle, each a level at a time from solid to gone (255 levels, the ends flat).
        check("caret: one rising fade and one falling fade a cycle, each a level at a time from one end to the other",
              rises >= 230 && rises <= 256 && falls >= 230 && falls <= 256);
        check("caret: the second half is the first one turned over: solid where the other is gone", mirror == 0);

        // A key restarts the phase: solid at once, gone in the middle of the cycle of its own, solid again at the next key.
        ui_text_open(&t, UI_TEXT_FIELD, (const p8 address_to) "", 0, ui_text_keymap());
        t.focused = true;
        t.caret.since = now;
        check("caret: solid at the key", check_text_tick_at(&t, now, &redraw) != 0 && t.caret_fade == 0);
        check("caret: gone three quarters round its own cycle", check_text_tick_at(&t, now + cycle / 2, &redraw) != 0 && t.caret_fade == UI_CARET_FULL);
        t.caret.since = now + cycle / 2 + 1000000;
        check("caret: a key restarts it solid", check_text_tick_at(&t, now + cycle / 2 + 1000000, &redraw) != 0 && t.caret_fade == 0);

        // Off is solid, and asks for no frame, whatever the time.
        ui_kit.caret_speed = UI_CARET_OFF;
        for (positive k = 0; k < 64; k++)
                solid = solid && check_text_tick_at(&t, now + k * 77000000ull, &redraw) == 0 && t.caret_fade == 0;
        check("caret: off is solid, and no frame is due", solid);

        // Left alone it blinks for as long as it has the keys: a minute on, a frame is still due.
        ui_kit.caret_speed = UI_CARET_NORMAL;
        redraw = false;
        check("caret: left alone a minute it blinks still, a frame due at its next change", check_text_tick_at(&t, t.caret.since + 60000000000ull, &redraw) != 0);
        ui_text_close(&t);
        ui_kit.caret_speed = saved;
        ui_kit.test_clock = 0;
}

// The pixel at the middle of the caret's cell (column 3 of row 0) in the buffer a frame was drawn into.
static unsigned int check_caret_pixel(struct windows address_to table, unsigned int own)
{
        const unsigned int address_to grid = (const unsigned int address_to)terminal_grid(windows_buffer(table, own, terminal.shown));

        return grid[(unsigned int)(terminal.cell_h / 2 * terminal.pitch) + 3 * terminal.cell_w + terminal.cell_w / 2];
}

/*
        The terminal's caret, as terminal.c draws it: the field's blink (ui_caret_at) restarted by a key, a block blended from
        its two ways round by the cover (none at 0 is the cell as written), a bar as wide as the setting, and the settings read
        from the kit alone (terminal.c has no caret setting of its own, as check_one_blink reads the windows).
*/
static fn check_caret_terminal(void)
{
        static const unsigned int widths[3] = {1, 2, 3};
        unsigned int saved_speed = ui_kit.caret_speed, pixels[60 * 20];
        b32 was_shown = shown, was_focused = terminal.focused;
        positive since = terminal.caret.since, now = 3000000000000ull, width_was = terminal.width;
        positive height_was = terminal.height, pitch_was = terminal.pitch, cell_h_was = terminal.cell_h;
        long cycle;

        ui_kit.caret_speed = UI_CARET_NORMAL;
        cycle = (long)ui_caret_cycle();
        b32 bar_ok = true;
        unsigned int drawn;

        check("terminal caret: a blend is the first colour at none, the second at all, and between at half",
              terminal_blend(0x000000, 0xffffff, 0) == 0 && terminal_blend(0x000000, 0xffffff, UI_CARET_FULL) == 0xffffff &&
              (terminal_blend(0x000000, 0xffffff, UI_CARET_FULL / 2) & 0xff) >= 127 && (terminal_blend(0x000000, 0xffffff, UI_CARET_FULL / 2) & 0xff) <= 128);

        shown = true;
        terminal.focused = true;
        ui_kit.caret_speed = UI_CARET_NORMAL;
        check_caret_restart_at(now);
        check("terminal caret: solid at a key, and gone three quarters round its cycle", check_caret_cover_at(now) == UI_CARET_FULL &&
              check_caret_cover_at(now + (positive)cycle / 2) == 0);
        check_caret_restart_at(now + (positive)cycle / 2 + 5000000ull);
        check("terminal caret: a key restarts it solid", check_caret_cover_at(now + (positive)cycle / 2 + 5000000ull) == UI_CARET_FULL);
        ui_kit.caret_speed = UI_CARET_OFF;
        check("terminal caret: off is solid whatever the time", check_caret_cover_at(now + (positive)cycle / 2) == UI_CARET_FULL);
        ui_kit.caret_speed = UI_CARET_NORMAL;
        terminal.focused = false;
        check("terminal caret: a window without the keys draws it solid", check_caret_cover_at(now + (positive)cycle / 2) == UI_CARET_FULL);

        // A bar caret's width: the caret setting's pixels, drawn as the marks of a cell, one row of the buffer counted.
        terminal.width = 60, terminal.height = 20, terminal.pitch = 60, terminal.cell_h = 20;
        for (positive k = 0; k < 3; k++)
        {
                struct window_cell bar = {'x', 0, 0, WINDOW_CELL_BAR};

                memory_zero(pixels, sizeof(pixels));
                terminal_decorate(pixels, &bar, 0, 0, 1, 0xffffffu, widths[k] * 2);
                drawn = 0;
                for (unsigned int x = 0; x < 60; x++)
                        drawn += pixels[5 * 60 + x] == 0xffffffu;
                bar_ok = bar_ok && drawn == widths[k] * 2;
        }
        check("terminal caret: a bar is as wide as the caret setting says, in device pixels", bar_ok);
        terminal.width = width_was, terminal.height = height_was, terminal.pitch = pitch_was, terminal.cell_h = cell_h_was;

        // The frames: a block caret on a blank cell is drawn at the cover its frame was made at, so its paper is the default
        // ink at full, the default paper at none, and between at the blend; and a frame whose cover changed repaints it.
        {
                struct windows address_to table = terminal.own.table;
                unsigned int own = terminal.own.row, at_full, at_gone, at_mid, cover_full, cover_gone, cover_mid;
                int index_full, index_gone, index_mid;
                positive mid = 10000000000ull;
                ui_kit.test_clock = mid, ui_kit.now = mid;
                positive quarter = (positive)cycle / 4;
                b32 framed;

                ui_kit.caret_speed = UI_CARET_NORMAL;
                cursor_hide();
                term_bytes((const p8 address_to) "\x1b[H\x1b[2Kabc", 11);
                cursor_show();
                terminal.focused = true;

                // The phase is set for each frame: solid at its start, gone at three quarters of a cycle, and between at the
                // middle of the falling fade (a quarter cycle in). Each frame is taken before the next, as the compositor does, and
                // its caret's pixel is read at once, since the buffer is drawn into again by the frame after.
                terminal.caret.since = mid;
                framed = terminal_frame();
                table->shown[own] = table->frame[own];
                index_full = terminal.shown, cover_full = terminal.caret_cover[index_full];
                at_full = check_caret_pixel(table, own);
                terminal.caret.since = mid - (positive)cycle / 2;
                framed = terminal_frame() && framed;
                table->shown[own] = table->frame[own];
                index_gone = terminal.shown, cover_gone = terminal.caret_cover[index_gone];
                at_gone = check_caret_pixel(table, own);
                terminal.caret.since = mid - quarter;
                framed = terminal_frame() && framed;
                table->shown[own] = table->frame[own];
                index_mid = terminal.shown, cover_mid = terminal.caret_cover[index_mid];
                at_mid = check_caret_pixel(table, own);
                check("terminal caret: the frames are made and taken", framed && cover_full == UI_CARET_FULL && cover_gone == 0 &&
                      cover_mid > 0 && cover_mid < UI_CARET_FULL);
                check("terminal caret: the block is the default ink at full cover and the default paper at none",
                      at_full == (terminal_default_ink() & 0xffffffu) && at_gone == (terminal_default_paper() & 0xffffffu));
                check("terminal caret: between the two, the block is the blend at its cover",
                      at_mid == terminal_blend(terminal_default_paper() & 0xffffffu, terminal_default_ink() & 0xffffffu, cover_mid));
        }
        // A wait that ends where the cover changes repaints the caret: an idle terminal has no key or output to draw it, and the
        // caret stayed gone from the frame that drew it in its off phase.
        {
                positive when = ui_kit.test_clock, later = when + (positive)cycle / 2 + 5000000ull;
                long timeout = -1;

                check_caret_step_at(when, &timeout);
                terminal.caret.since = when;
                terminal.dirty = false;
                check_caret_step_at(later, &timeout);
                check("terminal caret: the wait that ends where the cover changes repaints it, with nothing else to draw", terminal.dirty);
        }
        terminal.caret.since = since;

        // The settings come from the kit alone: terminal.c asks ui_caret_at and ui_caret_thick, and reads no setting of its own.
        {
                positive size = 0, bytes = compositor_map_file((string_address) "src/canvas/ui/terminal.c", &size);

                if (bytes)
                {
                        const char address_to text = (const char address_to)bytes;

                        check("terminal caret: the blink and the width come from ui.c's kit, the same as a field's",
                              check_hits(text, size, "ui_caret_cover(") >= 1 && check_hits(text, size, "ui_caret_thick()") >= 1 &&
                              check_hits(text, size, "ui_kit.caret") == 0 && check_hits(text, size, "UI_CARET_SLOW") == 0 &&
                              check_hits(text, size, "UI_CARET_FAST") == 0 && check_hits(text, size, "UI_CARET_OFF") == 0 &&
                              check_hits(text, size, "UI_CARET_THICK") == 0);
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
                }
                else
                        string_format(log, "  NOT RUN: the terminal's caret settings, the sources are not here\n");
        }

        shown = was_shown;
        terminal.focused = was_focused;
        terminal.caret.since = since;
        ui_kit.caret_speed = saved_speed;
        ui_kit.test_clock = 0;
        ui_kit.test_clock = 0;
}
#endif

/*
        One cut for every fitted label: ui.c's ui_cut_by is the only search for the longest start that fits with dots, and the
        kit's labels and a band's (taskbar.c) both ask it. No window writes its own dots after a cut (the three dots as a
        literal after a copied start), and no window keeps its own search. Read from the tree the check runs in.
*/
static fn check_one_cut(void)
{
        static const char address_to const files[] = {"src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/editor.c",
                                                      "src/canvas/ui/terminal.c", "src/canvas/ui/taskbar.c", "src/canvas/ui/identify.c"};
        static const char address_to const dots[] = {"\"...\", 3)", "\"...\", capacity)", "ui_cut_find("};
        positive read = 0, dots_found = 0, asks = 0;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f], &size);

                if (!bytes)
                        continue;
                read++;
                for (positive i = 0; i < sizeof(dots) / sizeof(dots[0]); i++)
                        dots_found += check_hits((const char address_to)bytes, size, dots[i]);
                if (f == 4)
                        asks += check_hits((const char address_to)bytes, size, "= ui_cut_by(");
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        {
                positive size = 0, bytes = compositor_map_file((string_address) "src/canvas/ui/ui.c", &size);

                if (bytes)
                {
                        asks += check_hits((const char address_to)bytes, size, "= ui_cut_by(");
                        dots_found += check_hits((const char address_to)bytes, size, "static unsigned char ui_cut_find(");
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
                }
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: one cut for every label, the sources are not here\n");
                return;
        }
        check("cut: every fitted label is ui.c's ui_cut_by (the kit's and the band's), and no window writes its own dots", read == sizeof(files) / sizeof(files[0]) && !dots_found && asks == 2);
}

/*
        One decimal reader for every window's number: the library's string_digits_max reads a number's digits, and no window
        of Canvas folds the digits into a number by hand (the value * 10 + digit loop, or the digit's byte less '0'). The four
        numbers that are read say so: the screen identify is asked for, the terminal's kept text size, and in ui.c, which is the
        whole kit, the find bar's line (the editor's line number is its) and a battery's reading. One digit that names a thing
        is no number folded, and is its byte less '0': ui.c has three, the group a replacement names ($1) and the argument a
        language's format names (%1$s, where the arguments are counted and where they are written), and the scan takes those
        three by their own words and every other as a fold. It reads the sources from the repository's root.
*/
static fn check_one_digits(void)
{
        static const struct
        {
                const char address_to file;
                positive reads, ones; // the calls of string_digits_max, and the single digits named below
        } files[] = {{"src/canvas/ui/identify.c", 1, 0}, {"src/canvas/ui/ui.c", 2, 3}, {"src/canvas/ui/terminal.c", 1, 0}};
        static const char address_to const one[] = {"(positive)(next - '0')", "index = (unsigned int)(*f - '0'), f += 2;"};
        positive read = 0, folds = 0;
        b32 as_said = true;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f].file, &size), ones = 0;
                const char address_to text = (const char address_to)bytes;

                if (!bytes)
                        continue;
                read++;
                for (positive i = 0; i < sizeof(one) / sizeof(one[0]); i++)
                        ones += check_hits(text, size, one[i]);
                // Each named digit holds the byte less '0' once: what is left of those hits, and every loop, is a fold by hand.
                folds += check_hits(text, size, "* 10 + (") + check_hits(text, size, "- '0')") - ones;
                as_said = as_said && ones == files[f].ones && check_hits(text, size, "string_digits_max(") == files[f].reads;
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: one decimal reader, the sources are not here\n");
                return;
        }
        check("digits: every window's number is read by string_digits_max, none folds its digits by hand", read == sizeof(files) / sizeof(files[0]) && !folds && as_said);
}

/*
        No input code by hand: a key and a pointer button are the kit's names (UI_KEY_*, UI_POINTER_*, in ui.c), so no
        window of Canvas defines its own key or button numbers or writes a button's evdev code as a literal. The scan
        reads the sources of the windows, as check_no_hand_typing does; the kit is where the codes are named.
*/
static fn check_no_input_codes(void)
{
        static const char address_to const files[] = {"src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/editor.c",
                                                      "src/canvas/ui/terminal.c", "src/canvas/ui/taskbar.c", "src/canvas/ui/identify.c"};
        static const char address_to const codes[] = {"#define UI_KEY_", "#define UI_KEY_", "#define TERMINAL_KEY_",
                                                      "#define SETTINGS_BUTTON_", "#define LAUNCHER_BUTTON_", "0x110", "0x111", "0x112"};
        positive found = 0, read = 0;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f], &size);
                const char address_to text = (const char address_to)bytes;

                if (!bytes)
                        continue;
                read++;
                for (positive i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
                {
                        positive length = string_length((string_address)codes[i]);

                        for (positive at = 0; at + length <= size;)
                        {
                                const char address_to hit = (const char address_to)memory_search((address_any)(text + at), size - at,
                                                                                                  (address_any)codes[i], length);

                                if (!hit)
                                        break;
                                found++;
                                string_format(log, "  input code by hand in %s: %s\n", (string_address)files[f], (string_address)codes[i]);
                                at = (positive)(hit - text) + length;
                        }
                }
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: no input code by hand, the sources are not here\n");
                return;
        }
        check("keys and buttons: no window of Canvas names a key or a pointer button by hand, the kit's UI_KEY_ and UI_POINTER_ codes do",
              read == sizeof(files) / sizeof(files[0]) && !found);
}

/*
        No shape by hand: a window's corner radius, its gap, a finger's slop, a close button, a status dot and a
        segmented tab's height are the kit's numbers (the theme's roles for the ones the compositor draws too, the rest of ui.c).
        A window that #defines one of those names with a number of its own is the copy the kit replaced, so the scan
        reads every #define in the window sources for a name of the kit's kinds with a digit where its value goes.
*/
static b32 shape_name(const char address_to name, positive length)
{
        static const char address_to const words[] = {"_RADIUS", "_GAP", "_SLOP", "_CLOSE", "_DOT", "_TAB_STRIP", "_TABS_H"};

        for (positive i = 0; i < sizeof(words) / sizeof(words[0]); i++)
                if (memory_search((address_any)name, length, (address_any)words[i], string_length((string_address)words[i])))
                        return true;
        return false;
}

static fn check_no_shapes_by_hand(void)
{
        static const char address_to const files[] = {"src/canvas/ui/settings.c", "src/canvas/ui/launcher.c", "src/canvas/ui/editor.c",
                                                      "src/canvas/ui/terminal.c", "src/canvas/ui/taskbar.c", "src/canvas/ui/identify.c"};
        positive found = 0, read = 0;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f], &size);
                const char address_to text = (const char address_to)bytes;

                if (!bytes)
                        continue;
                read++;
                for (positive at = 0; at < size;)
                {
                        positive line_end = at;

                        while (line_end < size && text[line_end] != '\n')
                                line_end++;
                        if (line_end - at > 8 && memory_search((address_any)(text + at), 8, (address_any)"#define ", 8))
                        {
                                positive name = at + 8, stop = name;

                                while (stop < line_end && text[stop] != ' ' && text[stop] != '\t')
                                        stop++;
                                if (shape_name(text + name, stop - name))
                                {
                                        positive value = stop;

                                        while (value < line_end && (text[value] == ' ' || text[value] == '\t'))
                                                value++;
                                        if (value < line_end && text[value] >= '0' && text[value] <= '9')
                                        {
                                                found++;
                                                string_format(log, "  shape by hand in %s\n", (string_address)files[f]);
                                        }
                                }
                        }
                        at = line_end + 1;
                }
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: no shape by hand, the sources are not here\n");
                return;
        }
        check("shapes: no window of Canvas keeps a radius, a gap, a slop, a close button, a dot or a tab height of its own",
              read == sizeof(files) / sizeof(files[0]) && !found);
}

/*
        No element by hand: an element the kit draws (a badge's dot, a focus outline) is drawn by the kit's function
        and not by a window's own copy of its calls. The scan reads the windows' sources for each copy this line removed.
*/
static fn check_no_element_by_hand(void)
{
        static const char address_to const files[] = {"src/canvas/ui/editor.c", "src/canvas/ui/settings.c"};
        static const char address_to const copies[] = {"ui_round(ui, ui_dev(ui, ui_tab_left", "ui_fill(ui, x, y + height - edge, width, edge, primary)",
                                                       "ui_fill(ui, 0, 0, editor.width, editor.height, ui_role_surface())"};
        positive found = 0, read = 0;

        for (positive f = 0; f < sizeof(files) / sizeof(files[0]); f++)
        {
                positive size = 0, bytes = compositor_map_file((string_address)files[f], &size);

                if (!bytes)
                        continue;
                read++;
                for (positive i = 0; i < sizeof(copies) / sizeof(copies[0]); i++)
                        if (memory_search((address_any)bytes, size, (address_any)copies[i], string_length((string_address)copies[i])))
                        {
                                found++;
                                string_format(log, "  element by hand in %s: %s\n", (string_address)files[f], (string_address)copies[i]);
                        }
                compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        if (!read)
        {
                string_format(log, "  NOT RUN: no element by hand, the sources are not here\n");
                return;
        }
        check("elements: no window of Canvas draws a kit element by hand: the tab's dot is the kit's", read == sizeof(files) / sizeof(files[0]) && !found);
}

/*
        The kit's defaults, which every window takes and none keeps a copy of. A press again to confirm is the kit's
        (ui_confirm: the first press arms it, the next with nothing between fires it, another press or key drops it, and
        the same line says so in every app). A field is UI_FIELD_H high, the kit's touch target. A press outside a field
        commits a plain value and cancels a secret or a command, in the kit. The kit's click window (450 ms, 4 px) counts
        the clicks of a text and of the terminal alike. The editor's tab strip is the kit's tab control, chosen by the
        mouse on its release. The sources of the apps hold none of the copies these replaced (a scan, as above).
        Where a default is not in the tree yet, its check is red: the names it needs are not declared.
*/
static b32 kit_defaults_scan(const char *file, const char *phrase)
{
        positive size = 0, bytes = compositor_map_file((string_address)file, &size);
        positive length = string_length((string_address)phrase), found = 0;
        const char address_to text = (const char address_to)bytes;

        if (!bytes)
                return false;
        for (positive at = 0; at + length <= size;)
        {
                const char address_to hit = (const char address_to)memory_search((address_any)(text + at), size - at, (address_any)phrase, length);

                if (!hit)
                        break;
                found++;
                string_format(log, "  %s still has: %s\n", (string_address)file, (string_address)phrase);
                at = (positive)(hit - text) + length;
        }
        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        return found != 0;
}

// The ripple's pixels outside the hover layer's footprint: a pixel the ripple changed with bare background under it and
// all round it, so an antialiased edge pixel of the layer is not one (a ripple cut square past a rounded corner is).
static positive ripple_outside_layer(const unsigned int address_to with, const unsigned int address_to without, unsigned int background,
                                     positive address_to differing)
{
        positive outside = 0;

        *differing = 0;
        for (int y = 0; y < 300; y++)
                for (int x = 0; x < 600; x++)
                {
                        b32 deep = x > 0 && y > 0 && x < 599 && y < 299;

                        if (with[y * 600 + x] == without[y * 600 + x])
                                continue;
                        (*differing)++;
                        for (int dy = -1; deep && dy <= 1; dy++)
                                for (int dx = -1; dx <= 1; dx++)
                                        deep &= without[(y + dy) * 600 + x + dx] == background;
                        outside += deep;
                }
        return outside;
}

// The fraction of a ripple's pixels that fall outside a box's rounded corners (radius r, device pixels), with a pixel's
// edge given a pixel of slack: a ripple cut square at a corner lies outside the arc by up to 0.41 r.
static positive ripple_outside_rounded(const unsigned int address_to with, const unsigned int address_to without, int x0, int y0, int x1, int y1, int r,
                                       positive address_to differing)
{
        positive outside = 0;

        *differing = 0;
        for (int y = 0; y < 300; y++)
                for (int x = 0; x < 600; x++)
                {
                        int cx = x < x0 + r ? x0 + r : x >= x1 - r ? x1 - r : x, cy = y < y0 + r ? y0 + r : y >= y1 - r ? y1 - r : y;
                        long dx = x - cx, dy = y - cy, reach = (long)(r + 1) * (r + 1);

                        if (with[y * 600 + x] == without[y * 600 + x])
                                continue;
                        (*differing)++;
                        if (x < x0 || x >= x1 || y < y0 || y >= y1 || dx * dx + dy * dy > reach)
                                outside++;
                }
        return outside;
}

#if MOONWATER_UI_EDITOR
// Whether lines first to last of a document are a comment's from their first byte to their last, as the editor colours them.
static b32 editor_comment_lines(struct doc address_to doc, p64 first, p64 last)
{
        static struct doc_token tokens[512];
        b32 whole = true;

        for (p64 line = first; line <= last; line++)
        {
                positive count = doc_tokens(doc, line, tokens, 512), covered = 0;
                const p8 address_to bytes;
                positive length;

                for (positive k = 0; k < count; k++)
                        whole = whole && tokens[k].role == DOC_ROLE_COMMENT && tokens[k].start == covered, covered += tokens[k].length;
                length = doc_line_bytes(doc, line, &bytes);
                whole = whole && count && covered == length;
        }
        return whole;
}

// Whether a line of a document has no comment in it and a string: a quoted letter the comment's close has let be one.
static b32 editor_code_line(struct doc address_to doc, p64 line)
{
        static struct doc_token tokens[512];
        positive count = doc_tokens(doc, line, tokens, 512);
        b32 comment = false, string = false;

        for (positive k = 0; k < count; k++)
                comment = comment || tokens[k].role == DOC_ROLE_COMMENT, string = string || tokens[k].role == DOC_ROLE_STRING;
        return !comment && string;
}

/*
        A block comment of twenty lines whose words have apostrophes in them (the kit's, the window's) is the comment's colour on every
        line, from its first byte to its last: in a document opened with it, and after a letter typed before the comment on its first
        line, which tokenizes the lines again from the states the edit kept. The line after the comment's close is code again, and a
        quoted letter on it is a string.
*/
static fn check_editor_comments(void)
{
        static const char inner[] = "\tthe kit's own page, the window's edge and the tabs' row\n";
        static p8 text[2048];
        positive length = 0;
        struct doc doc;
        static struct doc_token tokens[512];
        positive count;
        b32 fresh, edited, first;

        memory_copy(text, "/*\n", 3), length = 3;
        for (int k = 0; k < 18; k++)
                memory_copy(text + length, inner, sizeof(inner) - 1), length += sizeof(inner) - 1;
        memory_copy(text + length, "*/\nint x = 'a';\n", 16), length += 16;
        doc_open(&doc, text, length);
        doc.language = DOC_LANGUAGE_C;
        fresh = editor_comment_lines(&doc, 0, 19) && editor_code_line(&doc, 20);
        doc_replace(&doc, 0, 0, (const p8 address_to) "x", 1);
        count = doc_tokens(&doc, 0, tokens, 512);
        first = count == 2 && tokens[0].role == DOC_ROLE_TEXT && tokens[1].role == DOC_ROLE_COMMENT && tokens[1].start == 1 && tokens[1].length == 2;
        edited = first && editor_comment_lines(&doc, 1, 19) && editor_code_line(&doc, 20);
        doc_close(&doc);
        check("editor colours: every line of a block comment of twenty lines with apostrophes in its words is the comment's, opened fresh", fresh);
        check("editor colours: and after a letter typed before the comment on its first line, and the line after its close is code again", edited);
}

// Whether every line of a document has the tokens a document opened with its text has: the states its edits kept are the ones a
// tokenizing from the start finds.
static b32 editor_tokens_fresh(struct doc address_to doc)
{
        static struct doc_token one[512], two[512];
        static p8 copy[1024];
        struct doc fresh;
        positive length = doc_read(doc, 0, copy, sizeof(copy));
        b32 same = true;

        doc_open(&fresh, copy, length);
        fresh.language = doc->language;
        for (p64 line = 0; line < doc_line_count(doc); line++)
        {
                positive a = doc_tokens(doc, line, one, 512), b = doc_tokens(&fresh, line, two, 512);

                same = same && a == b && !memory_compare(one, two, a * sizeof(one[0]));
        }
        doc_close(&fresh);
        return same;
}

/*
        A change that takes line breaks and puts as many (three lines written over three) tokenizes again every line it put before a
        line's state may be found as it was: the lines past it are coloured as they are now and not as they were. Three lines whose
        third is a comment's opener, written over three of code the view has looked at, make the two lines after them the comment's;
        and an undo that puts such three lines back over three of code does the same: each as a document opened with that text has it.
*/
static fn check_editor_lines_put(void)
{
        static const char code[] = "a\nb\nc\nd\ne\n", opener[] = "a\nb\n/*\nd\ne\n";
        struct doc doc;
        b32 written, undone;

        doc_open(&doc, (const p8 address_to)code, sizeof(code) - 1);
        doc.language = DOC_LANGUAGE_C;
        // The view has looked at every line: their states are known before the change.
        editor_tokens_fresh(&doc);
        doc_replace(&doc, 0, 5, (const p8 address_to) "a\nb\n/*", 6);
        written = editor_comment_lines(&doc, 2, 4) && editor_tokens_fresh(&doc);
        doc_close(&doc);
        // The opener written over with code before the view looked, the view looks at every line, and the undo puts the opener back.
        doc_open(&doc, (const p8 address_to)opener, sizeof(opener) - 1);
        doc.language = DOC_LANGUAGE_C;
        doc_replace(&doc, 0, 6, (const p8 address_to) "a\nb\nc", 5);
        editor_tokens_fresh(&doc);
        doc_undo(&doc);
        undone = editor_comment_lines(&doc, 2, 4) && editor_tokens_fresh(&doc);
        doc_close(&doc);
        check("editor colours: three lines written over three, the third a comment's opener, make the lines after them the comment's", written);
        check("editor colours: and an undo that puts such three lines back over three of code does too, as a document opened with that text has it", undone);
}
#endif

#if MOONWATER_UI_ANY
static fn check_kit_defaults(struct ui address_to wui)
{
        static unsigned int with[600 * 300], without[600 * 300], scratch[600 * UI_SCRATCH_ROWS];
        static font_face face;
        struct ui ui = *wui;
        positive size = 0, bytes = compositor_map_file(program_argument(program_argument_count() > 3 ? 3 : 1), &size);
        b32 ripples_held = true, confirm_ok = false, field_ok = false, outside_ok = false, clicks_ok = false, tabs_ok = false;
        b32 inset_ok[2] = {false, false};
        positive differing = 0, outside = 0;

        if (!bytes || font_load(&face, (const p8 address_to)bytes, size, 0) != 0)
        {
                check("kit defaults: the face loads", false);
                return;
        }
        ui.regular = ui.bold = ui.mono = &face;
        static const char *const copies[][2] = {
            {"src/canvas/ui/settings.c", "press Enter again"},    {"src/canvas/ui/settings.c", "press Delete again"},
            {"src/canvas/ui/settings.c", "settings.confirm_args"}, {"src/canvas/ui/settings.c", "settings.forget[0]"},
            {"src/canvas/ui/settings.c", "ui_page_column_press(&settings.ui, &settings.page, row, x, y)"}, {"src/canvas/ui/settings.c", "settings.confirm = 0"},
            {"src/canvas/ui/editor.c", "editor.armed"},           {"src/canvas/ui/editor.c", "again writes over"},
            {"src/canvas/ui/editor.c", "Ctrl+W again"},           {"src/canvas/ui/editor.c", "UI_SEARCH_H"},
            {"src/canvas/ui/launcher.c", "UI_SEARCH_H"},          {"src/canvas/ui/launcher.c", "(row == launcher.press.target ? row : -1)"},
            {"src/canvas/ui/launcher.c", "ui_press_begin("}, {"src/canvas/ui/launcher.c", "ui_press_end("}, {"src/canvas/ui/launcher.c", "ui_press_move("},
            {"src/canvas/ui/launcher.c", "ui_list_let_go("}, {"src/canvas/ui/launcher.c", "ui_list_press_slide("}, {"src/canvas/ui/launcher.c", "ui_list_pointer("}, {"src/canvas/ui/launcher.c", "launcher.pointer_x"}, {"src/canvas/ui/launcher.c", "had_focus"}, {"src/canvas/ui/launcher.c", "launcher_focus("}, {"src/canvas/ui/settings.c", "(settings.list.down == settings.press.target))"},
            {"src/canvas/ui/terminal.c", "TERMINAL_CLICKS_NS"},   {"src/canvas/ui/terminal.c", "terminal.click_row"},
            {"src/canvas/ui/ui.c", "UI_SEARCH_H"}};
        positive copied = 0;

        ui.pixels = with, ui.pitch = 600, ui.width = 600, ui.height = 300, ui.top = 0, ui.bottom = 300, ui.scale = 100;
        ui.scratch = scratch, ui.scratch_width = 600, ui.band_top = ui.band_bottom = 0;
        ui_kit_theme(UI_THEME_DARK, 0xffffffu, 0);
        ui_kit.now = 7000000000ll;

        // Every control's ripple is cut to its own hover rect, with its corners: a pill button, a segmented strip's item, a
        // vertical strip's item and a switch's row. A press in the middle of each, fourteen frames in (about 230 ms), its
        // ripple's pixels all lie inside the rounded box of the item or row the press went down on, the box the hover layer fills.
        {
                static struct
                {
                        int kind, variant, x, y, w, h, count, item, radius;
                } specs[] = {{UI_BUTTON, UI_BUTTON_PRIMARY, 20, 20, 160, 44, 1, 0, 22},
                             {UI_BUTTON, UI_BUTTON_OUTLINED, 20, 80, 160, 44, 1, 0, 22},
                             {UI_TABS, UI_TABS_SEGMENTED, 20, 140, 180, 32, 3, 0, 16},
                             {UI_TABS, UI_TABS_VERTICAL, 240, 20, 150, 34, 12, 5, 8},
                             {UI_SWITCH, 0, 20, 200, 200, 44, 1, 0, 0}, {UI_RADIOS, 0, 300, 20, 240, 40, 3, 1, 0}};

                for (positive s = 0; s < sizeof(specs) / sizeof(specs[0]); s++)
                {
                        struct ui_control c;
                        int bx, by, bw, bh, press_x, press_y;
                        b32 vertical = specs[s].kind == UI_TABS && specs[s].variant == UI_TABS_VERTICAL;

                        memory_zero(&c, sizeof(c));
                        c.kind = specs[s].kind, c.variant = specs[s].variant, c.x = specs[s].x, c.y = specs[s].y, c.w = specs[s].w, c.h = specs[s].h;
                        c.count = specs[s].count;
                        for (int k = 0; k < specs[s].count; k++)
                                c.items[k] = "Item";
                        c.label = "Run";
                        c.at = specs[s].item;
                        ui_control_start(&c);
                        ui_control_draw(&ui, &c);
                        // The box of the item the press is on, as the kit lays it out.
                        bx = c.x, by = c.y, bw = c.w, bh = c.h;
                        if (c.kind == UI_TABS && !vertical)
                                bx = ui_tab_left(&ui, &c, specs[s].item), bw = ui_tab_width(&ui, &c, specs[s].item);
                        if (vertical)
                                by = c.y + specs[s].item * c.h;
                        if (c.kind == UI_RADIOS)
                                by = ui_radio_row_y(&c, specs[s].item);
                        if (c.kind == UI_SWITCH)
                                ui_control_row(&c, &bx, &by, &bw, &bh);
                        press_x = bx + bw / 2, press_y = by + bh / 2;
                        ui_kit_pointer(&ui, &c, 1, WINDOW_EVENT_BUTTON, press_x, press_y, true, 0);
                        for (int frame = 0; frame < 14; frame++)
                                ui_kit.now += 16666667, ui_kit.busy = 0, ui_control_draw(&ui, &c);
                        check_ripple_frame(&ui, &c, with, without, 0xff202020u);
                        if (c.kind == UI_RADIOS)
                                outside = ripple_outside_rounded(with, without, bx, by, bx + bw, by + bh, 0, &differing);
                        else
                                outside = ripple_outside_layer(with, without, 0xff202020u, &differing);
                        if (differing < 200 || outside)
                                ripples_held = false;
                        string_format(log, "  ripple control %p: %p pixels, %p outside the hover rect\n", (p64)s, (p64)differing, (p64)outside);
                }
        }
        check("kit ripple: a ripple's pixels lie inside its control's hover rect, corners and all, for every control", ripples_held);

        // Confirm: the first press arms it and does not fire, the second with nothing between fires, another press drops it,
        // a different thing arms again, and the line says the same to every app.
#ifdef UI_CONFIRM_TEXT
        {
                struct ui_confirm confirm;
                char line[UI_CONFIRM_TEXT];

                memory_zero(&confirm, sizeof(confirm));
                confirm_ok = !ui_confirm_ask(&confirm, 1, "a") && ui_confirm_ask(&confirm, 1, "a") && !ui_confirm_ask(&confirm, 1, "a") &&
                             !ui_confirm_ask(&confirm, 2, "a") && ui_confirm_ask(&confirm, 2, "a");
                ui_confirm_ask(&confirm, 3, "b");
                ui_confirm_drop(&confirm);
                confirm_ok = confirm_ok && !ui_confirm_ask(&confirm, 3, "b") && ui_confirm_ask(&confirm, 3, "b");
                ui_confirm_ask(&confirm, 3, "b");
                ui_confirm_drop(&confirm);
                ui_confirm_line(line, sizeof(line), "forget the net");
                confirm_ok = confirm_ok && string_equals((string_address)line, (string_address) "press again to confirm: forget the net");
        }
        check("kit confirm: the first press arms it, the second fires it, and another press or a drop cancels it", confirm_ok);
#else
        check("kit confirm: the first press arms it, the second fires it, and another press or a drop cancels it", false);
#endif

        // Fields: every text field is the kit's height, and a press outside a field commits a plain value and cancels a
        // secret or a command.
#ifdef UI_CONFIRM_TEXT
        {
                static struct ui_text t, plain, secret;
                static struct windows *table_scratch;
                struct windows *const table_at = windows_scratch_table(&table_scratch);
                static struct window_row own;
                own.table = table_at, own.row = 0, own.door = -1;
                field_ok = UI_FIELD_H == UI_TARGET && UI_TARGET == 48;
                ui_field_open(&plain, &own, "");
                ui_field_open(&secret, &own, "");
                secret.masked = 1, secret.command = 1;
                for (int k = 0; k < 2; k++)
                {
                        struct ui_text address_to f = k ? &secret : &plain;

                        f->well_left = 0, f->well_top = 0, f->well_right = 100, f->well_bottom = 44;
                        field_type(wui, f, "abc");
                }
                {
                        p32 out_plain = field_button(wui, &plain, 0x110, true, 300, 300, 0);
                        p32 out_secret = field_button(wui, &secret, 0x110, true, 300, 300, 0);

                        outside_ok = (out_plain & UI_TEXT_COMMIT) && field_is(&plain, "abc") && (out_secret & UI_TEXT_CANCEL) &&
                                     !(out_plain & UI_TEXT_CANCEL) && !(out_secret & UI_TEXT_COMMIT);
                }
        }
        check("kit field: every text field is UI_FIELD_H, the kit's 44 px touch target", field_ok);
        check("kit field: a press outside a plain field commits its value, and outside a secret or a command it cancels", outside_ok);
#else
        check("kit field: every text field is UI_FIELD_H, the kit's 44 px touch target", false);
        check("kit field: a press outside a plain field commits its value, and outside a secret or a command it cancels", false);
#endif

        // The click window: 450 ms and 4 px, as the kit's text counts its clicks, so a terminal's and a field's agree.
#ifdef UI_CLICK_NS
        {
                struct ui_click k;
                int twice, once, thrice, far;

                memory_zero(&k, sizeof(k));
                twice = (ui_kit.now = (b64)(1000000000ll), ui_click_count(&ui, &k, 10, 10));
                twice = (ui_kit.now = (b64)(1000000000ll + 449000000ll), ui_click_count(&ui, &k, 14, 10));
                thrice = (ui_kit.now = (b64)(1000000000ll + 898000000ll), ui_click_count(&ui, &k, 14, 10));
                memory_zero(&k, sizeof(k));
                (ui_kit.now = (b64)(1000000000ll), ui_click_count(&ui, &k, 10, 10));
                far = (ui_kit.now = (b64)(1000000000ll + 100000000ll), ui_click_count(&ui, &k, 15, 10));
                memory_zero(&k, sizeof(k));
                (ui_kit.now = (b64)(1000000000ll), ui_click_count(&ui, &k, 10, 10));
                once = (ui_kit.now = (b64)(1000000000ll + 451000000ll), ui_click_count(&ui, &k, 10, 10));
                clicks_ok = twice == 2 && thrice == 3 && far == 1 && once == 1;
        }
        check("kit clicks: a second click 450 ms and 4 px on counts two, a third three, and a slow or far one starts over", clicks_ok);
#else
        check("kit clicks: a second click 450 ms and 4 px on counts two, a third three, and a slow or far one starts over", false);
#endif

        // The editor's tabs are the kit's tab control, and a mouse press and release on one chooses it.
#ifdef UI_CONFIRM_TEXT
        {
                unsigned int pixels[600 * 300];
                int at_x, at_y;

                editor.ui = ui;
                editor.width = 600, editor.height = 300, editor.own.stride = 600;
                editor.count = 0;
                // A control the kit holds from an earlier press takes the strip's events (the view hands a held control's to it): the tabs start with none held.
                ui_kit_held = null;
                editor_open(null), editor_open(null), editor_open(null);
                editor.tab = 0;
                {
                        unsigned long was = ui_kit_calls;

                        editor_draw(pixels);
                        if (kit_draw_first(2))
                        {
                                kit_draw_print("editor", ui_kit_calls - was);
                                check("kit calls: a draw of the editor is made of painter calls", ui_kit_calls - was > 0);
                        }
                }
                at_x = ui_tab_left(&editor.ui, &editor.strip, 2) + ui_tab_width(&editor.ui, &editor.strip, 2) / 2;
                at_y = editor.strip.y * editor.ui.scale / 100 + editor.strip.h * editor.ui.scale / 100 / 2;
                ui_event_context(&(struct window_event){WINDOW_EVENT_MOTION, 0, at_x, at_y});
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0});
                tabs_ok = editor.tab == 0;
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, 0, 0x110, 0});
                tabs_ok = tabs_ok && editor.tab == 2;
                // The strip is clear of the window's edges and its rounded corner: the window's margin in from its start, a gap from its
                // top, the first tab's pill starting there; reading from the right, the margin in from the right edge, where the first tab ends.
                for (int rtl = 0; rtl < 2; rtl++)
                {
                        int first, wide = 600 * 100 / editor.ui.scale; // the window's width in logical pixels, the strip's

                        ui_kit.rtl = rtl;
                        editor_draw(pixels);
                        first = ui_tab_left(&editor.ui, &editor.strip, 0);
                        inset_ok[rtl] = editor.strip.y == UI_GAP2 && editor.strip.h == UI_TABS_H &&
                                        (rtl ? editor.strip.x + editor.strip.w == wide - UI_PAD && first + ui_tab_width(&editor.ui, &editor.strip, 0) == wide - UI_PAD
                                             : editor.strip.x == UI_PAD && first == UI_PAD);
                }
                ui_kit.rtl = 0;
                editor.count = 0;
        }
        check("editor tabs: a mouse press and release on a tab choose it, as the kit's tabs do", tabs_ok);
        check("editor tabs: the strip is the window's margin in from its start and a gap from its top, its first pill clear of the corner", inset_ok[0]);
        check("editor tabs: and reading from the right the margin is in from the right edge, where the first pill ends", inset_ok[1]);
#else
        check("editor tabs: a mouse press and release on a tab choose it, as the kit's tabs do", false);
        check("editor tabs: the strip is the window's margin in from its start and a gap from its top, its first pill clear of the corner", inset_ok[0]);
        check("editor tabs: and reading from the right the margin is in from the right edge, where the first pill ends", inset_ok[1]);
#endif

        /* The focus rule: the code view has the keys when the window has them and the find bar does not. Opening the bar takes them from
              the view, and the bar's answer (a line to go to, Enter) gives them back: the goto branch sets the view's keys by the same rule. */
#ifdef UI_CONFIRM_TEXT
        {
                unsigned int pixels[600 * 300];
                b32 open_ok, goto_ok;
                positive goto_count;

                editor.ui = ui;
                editor.width = 600, editor.height = 300, editor.own.stride = 600;
                editor.count = 0;
                editor.focused = true;
                editor_open(null);
                editor.tab = 0;
                editor_draw(pixels);
                editor_command(DOC_CMD_FIND);
                open_ok = !editor_now()->view.focused && editor.find.field.focused;
                editor_command(DOC_CMD_GOTO_LINE);
                ui_text_set(&editor.find.field, (const p8 address_to) "2", 1);
                ui_findbar_enter(&editor.ui, &editor.find, &editor_now()->view, false, editor.focused, &goto_count);
                goto_ok = editor.find.mode == UI_FINDBAR_CLOSED && !editor.find.field.focused && editor_now()->view.focused && editor.focused;
                editor.count = 0;
                check("editor focus: the find bar takes the keys from the code view, and a line's Enter gives them back to it", open_ok && goto_ok);
        }
#endif

        // A finger's long press in the code view, sent as the compositor sends it: the editor's own picture has the selection's handles
        // and its actions bar over the text (editor_draw ends with the overlay).
#ifdef UI_SELECT_BAR_ITEMS
        {
                unsigned int pixels[600 * 300];
                const unsigned int held = WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD;
                struct editor_tab address_to tab;
                int x, y;

                editor.ui = ui;
                editor.width = 600, editor.height = 300, editor.own.stride = 600;
                editor.count = 0;
                editor_open(null);
                editor.tab = 0;
                tab = editor_now();
                doc_type(&tab->view.doc, (const p8 address_to) "hello world again\n", 18);
                tab->view.focused = true;
                editor_draw(pixels);
                x = tab->view.left + 3 * tab->view.cell, y = tab->view.top + tab->view.line_height / 2;
                ui_event_context(&(struct window_event){WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y});
                editor_event(&(struct window_event){WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y});
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0});
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, held | WINDOW_KEY_CANCEL, 0x110, 0});
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, held | WINDOW_KEY_DOWN, 0x111, 0});
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, held, 0x111, 0});
                editor_event(&(struct window_event){WINDOW_EVENT_BUTTON, held | WINDOW_KEY_CANCEL, 0x110, 0});
                editor_draw(pixels);
                check("editor: a finger's long press in the code view selects a word, and the editor's picture shows its handles and the actions bar",
                      doc_sel_start(&tab->view.doc.selections[tab->view.doc.primary]) == 0 && doc_sel_end(&tab->view.doc.selections[tab->view.doc.primary]) == 5 &&
                          tab->view.sel.handles && tab->view.sel.bar_up && tab->view.sel.bar_count == 4);
                editor.count = 0;
        }
#else
        check("editor: a finger's long press in the code view selects a word, and the editor's picture shows its handles and the actions bar", false);
#endif

        // No app keeps a copy of a default: the phrases of the copies it had are gone from its sources.
        for (positive k = 0; k < sizeof(copies) / sizeof(copies[0]); k++)
                copied += kit_defaults_scan(copies[k][0], copies[k][1]) ? 1 : 0;
        check("kit defaults: no app keeps its own copy of a confirm, a field height, a click window or a press that fires on press", copied == 0);
}
#endif

/*
        The fields of the windows themselves, drawn with the repository's Inter and worked by the keys and the hand as
        their windows are: the launcher's (a click puts the caret, a drag selects, and what is typed goes there), the
        settings' filter (the arrows move in it, and a blank typed between letters parts its words) and the field a
        row is typed in (a click in the name puts the caret, and Enter sets what it then holds).
*/
#if MOONWATER_UI_SETTINGS && MOONWATER_UI_LAUNCHER
static fn check_input_matrix(struct ui address_to ui, unsigned int address_to pixels, struct windows address_to table);
static fn check_field_hands(string_address regular)
{
        static font_face face;
        static p8 atlas[512 * 512];
        static unsigned int scratch[800 * UI_SCRATCH_ROWS];
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        positive size = 0, bytes = compositor_map_file(regular, &size);
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        long pixels = ui_map(800 * 600 * 4);
        struct ui ui;

        if (!bytes || (engine < 0 && engine > -4096) || font_load(&face, (const p8 address_to)bytes, size, 0) != 0)
        {
                check("fields of the windows: the face loads", false);
                return;
        }
        memory_zero(&ui, sizeof(ui));
        ui.engine = (font_engine address_to)engine;
        font_start(ui.engine, atlas, 512, 512);
        ui.regular = ui.bold = &face;
        ui.scale = 100;
        ui.scratch = scratch, ui.scratch_width = 800;
        ui.opaque = 1;
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        ui_kit_inks(&ui);
        table_at->stride = 800, table_at->output_width = 1920, table_at->output_height = 1080;

        // The launcher: a click in its field puts the caret, a drag selects, and the keys type there.
        {
                static char names[3][LAUNCHER_NAME];
                static unsigned char from[3];
                static unsigned short hashed[LAUNCHER_HASH];
                const font_face address_to faces[2] = {&face, &face};
                int left = 14, y = UI_PAD + UI_TARGET / 2, ls = ui_run_in(&ui, faces, 0, 0, (string_address) "ls", 15, 0, 0, 0, 800, 0); // the field runs edge to edge: its text is 14 in
                struct window_event move = {WINDOW_EVENT_MOTION, 0, left + ls, y}, press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0},
                                    release = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};

                string_copy((string_address)names[0], (string_address) "lsof");
                memory_zero(&launcher, sizeof(launcher));
                launcher.names = (char address_to)names, launcher.count = 1, launcher.from = from, launcher.hashed = hashed;
                launcher.dirs[0] = "/bin", launcher.dir_count = 1;
                launcher.own.table = table_at;
                launcher.ui = ui;
                launcher.width = LAUNCHER_WIDTH;
                launcher.focused = true, launcher_app.had_keys = true;
                launcher_type_text("lsof");
                launcher_draw((unsigned int address_to)pixels);
                launcher_send(&move);
                launcher_send(&press);
                launcher_send(&release);
                launcher_typed('X');
                check("launcher: a click in the field puts the caret there, and a key types there", launcher_holds("lsXof"));
                // From the well's padding, left of the text, across it and past its end.
                move.a = left - 8;
                launcher_send(&move);
                launcher_send(&press);
                move.a = left + 200;
                launcher_send(&move);
                launcher_send(&release);
                launcher_typed('k');
                check("launcher: a drag across the field selects it, and a key types over it", launcher_holds("k"));
                // A finger's long press in the field selects its word, and the picture the launcher draws has the handles and the
                // actions bar over it (it draws the field's overlay last): the one kit rule, in this window.
                {
                        const unsigned int held = WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD;
                        struct window_event at = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, left + 4, y}, down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0},
                                            taken = {WINDOW_EVENT_BUTTON, held | WINDOW_KEY_CANCEL, UI_POINTER_LEFT, 0}, right = {WINDOW_EVENT_BUTTON, held | WINDOW_KEY_DOWN, UI_POINTER_RIGHT, 0},
                                            right_up = {WINDOW_EVENT_BUTTON, held, UI_POINTER_RIGHT, 0};

                        launcher_type_text("lsof");
                        launcher_draw((unsigned int address_to)pixels);
                        launcher_send(&at);
                        launcher_send(&down);
                        launcher_send(&taken);
                        launcher_send(&right);
                        launcher_send(&right_up);
                        launcher_send(&taken);
                        launcher_draw((unsigned int address_to)pixels);
                        check("launcher: a finger's long press in the field selects its word, and the picture shows its handles and the actions bar",
                              launcher_holds("lsof") && launcher.field.sel.handles && launcher.field.sel.bar_up && launcher.field.sel.bar_count == 3);
                        // The handles are in the picture, in the accent under the line, though the list was drawn under its own clip after the field.
                        {
                                int hx, htop;
                                const unsigned int address_to shown = (const unsigned int address_to)pixels;

                                check("launcher: the handle's knob is drawn in the accent under the field's line, past the clip the list drew under",
                                      !ui_text_where(&launcher.ui, &launcher.field, doc_sel_end(&launcher.field.doc.selections[0]), &hx, &htop) &&
                                          (shown[(positive)(htop + launcher.field.line_height + ui_dev(&launcher.ui, UI_SELECT_HANDLE)) * launcher.ui.pitch + (positive)hx] & 0xffffffu) ==
                                              (ui_role(UI_PRIMARY) & 0xffffffu));
                        }
                }
                // The session's buttons: none unless the machine can do it (the kernel's own list has the whole word mem for Sleep), and
                // with both offered the foot has four, each armed by its first press and started by its second: Sleep is `moonwater
                // sleep` and Log out `moonwater desktop stop`. A button that is not offered is not there and starts nothing.
                {
                        unsigned int was = launcher.session, runs = launcher.power_runs;
                        string_address words[4];
                        b32 read, two, four, said, refused;

                        read = launcher_sleeps("freeze mem disk\n", 16) && launcher_sleeps("mem", 3) && !launcher_sleeps("freeze disk\n", 12) &&
                               !launcher_sleeps("memory\n", 7) && !launcher_sleeps("xmem \n", 6) && !launcher_sleeps("", 0);
                        launcher_session_read("/tmp/mw-no-such-power-state");
                        read = read && launcher.session == LAUNCHER_LOG_OUT;
                        // And as the bar reads it: a list that is there and has no mem offers no Sleep, and one that has it offers it.
                        for (int which = 0; which < 2; which++)
                        {
                                const char address_to text = which ? "freeze mem disk\n" : "freeze disk\n";
                                bipolar made = system_open_at_mode(AT_FDCWD, (string_address) "/tmp/mw-launcher-power-state", FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);

                                if (made >= 0)
                                {
                                        system_write_once(made, text, string_length((string_address)text));
                                        system_close(made);
                                }
                                launcher_session_read("/tmp/mw-launcher-power-state");
                                read = read && made >= 0 && launcher.session == (which ? (LAUNCHER_SLEEP | LAUNCHER_LOG_OUT) : LAUNCHER_LOG_OUT);
                        }
                        check("launcher: Sleep is offered only where the kernel's list of sleeps has the whole word mem, and a list that cannot be read offers none", read);
                        launcher.session = 0;
                        launcher.height = launcher_build(0);
                        two = launcher.view.group.count == 2 && !launcher_power_offered(2) && !launcher_power_offered(3) && launcher_power_act(2) &&
                              launcher.power_runs == runs;
                        launcher.session = LAUNCHER_SLEEP | LAUNCHER_LOG_OUT;
                        launcher.height = launcher_build(0);
                        four = launcher.view.group.count == 4 && launcher_power_offered(2) && launcher_power_offered(3) &&
                               launcher.view.group.button[3].x + launcher.view.group.button[3].w <= LAUNCHER_WIDTH &&
                               launcher.view.group.button[0].x >= 0 && launcher.view.group.button[2].x > launcher.view.group.button[1].x;
                        said = launcher_power_words(2, words) == 3 && string_equals(words[2], (string_address) "moonwater sleep") &&
                               launcher_power_words(3, words) == 3 && string_equals(words[2], (string_address) "moonwater desktop stop") &&
                               string_equals(words[0], (string_address) "/shell") && launcher.view.group.says[2] && launcher.view.group.says[3];
                        launcher.session = LAUNCHER_LOG_OUT;
                        launcher.height = launcher_build(0);
                        refused = launcher.view.group.count == 3 && launcher_power_act(2) && launcher.power_runs == runs && launcher_power_offered(3);
                        check("launcher: with no session action offered the foot has Power off and Restart alone, and a button that is not offered starts nothing", two && refused);
                        check("launcher: with Sleep and Log out offered the foot has four buttons inside the bar, each said before it is done, Sleep starting moonwater sleep and Log out moonwater desktop stop",
                              four && said);
                        // Four buttons leave the line that says what an armed one will do no room before them: armed, the line is in the room
                        // of the others, after the first button or before the last, wide enough for its words, and the others are veiled (not
                        // drawn, and a press there is no button's) until it is dropped, when all four are back.
                        {
                                struct ui_buttons address_to group = &launcher.view.group;
                                char line_words[UI_CONFIRM_TEXT];
                                int scale = launcher.ui.scale > 0 ? launcher.ui.scale : 100, need;
                                b32 first, last, back;

                                launcher.session = LAUNCHER_SLEEP | LAUNCHER_LOG_OUT;
                                launcher.height = launcher_build(0);
                                ui_buttons_press(group, 0);
                                launcher.height = launcher_build(0);
                                ui_confirm_line(line_words, sizeof(line_words), group->says[0]);
                                need = ui_kit_measure(&launcher.ui, (string_address)line_words, group->line.variant);
                                first = group->confirm.armed && group->button[0].w > 0 && !group->button[1].w && !group->button[2].w && !group->button[3].w &&
                                        group->line.x >= group->button[0].x + group->button[0].w && group->line.w * scale / 100 + 1 >= need;
                                for (int k = 0; k < group->count; k++)
                                        first = first && ui_control_hit(&launcher.ui, &group->button[k], ui_dev(&launcher.ui, group->line.x + group->line.w / 2),
                                                                        ui_dev(&launcher.ui, group->button[0].y + group->button[0].h / 2)) < 0;
                                ui_confirm_drop(&group->confirm);
                                ui_buttons_press(group, 3);
                                launcher.height = launcher_build(0);
                                ui_confirm_line(line_words, sizeof(line_words), group->says[3]);
                                need = ui_kit_measure(&launcher.ui, (string_address)line_words, group->line.variant);
                                last = group->confirm.armed && group->button[3].w > 0 && !group->button[0].w && !group->button[1].w && !group->button[2].w &&
                                       group->line.x + group->line.w <= group->button[3].x && group->line.w * scale / 100 + 1 >= need;
                                ui_confirm_drop(&group->confirm);
                                group->keys = 0;
                                launcher.height = launcher_build(0);
                                back = group->button[0].w > 0 && group->button[1].w > 0 && group->button[2].w > 0 && group->button[3].w > 0;
                                check("launcher: with four buttons an armed one's line is in the room of the others, after the first or before the last, wide enough for its words, the others veiled till it is dropped",
                                      first && last && back);
                        }
                        launcher.session = was;
                        launcher.height = launcher_build(0);
                }
                // A name of PATH with an escape and an override in it: the row's words have a picture and a mark where they stood, and no
                // byte of either, while the name the row starts is the file's own.
                {
                        static char hostile[LAUNCHER_NAME] = "ls\x1b[2Jx\xe2\x80\xaeod.exe";
                        char was[LAUNCHER_NAME], shown[LAUNCHER_TEXT + 8];
                        const char address_to note = null;
                        struct launcher_entry entry = {0};
                        b32 clean = true;

                        string_copy_bounded((string_address)was, (string_address)launcher_name(0), sizeof(was));
                        string_copy_bounded((string_address)launcher_name(0), (string_address)hostile, LAUNCHER_NAME);
                        entry.kind = LAUNCHER_PROGRAM, entry.index = 0;
                        launcher_label(&entry, shown, sizeof(shown), &note);
                        for (const char address_to at = shown; *at; at++)
                                clean = clean && (unsigned char)*at >= 0x20 && *at != 0x7f;
                        check("launcher: a name of PATH is written as words from outside: an escape in it is its picture and an override a replacement mark, and the name itself is as the file has it",
                              clean && string_equals((string_address)shown, (string_address) "ls\xe2\x90\x9b[2Jx\xef\xbf\xbdod.exe") &&
                                  string_equals((string_address)launcher_name(0), (string_address)hostile));
                        string_copy_bounded((string_address)launcher_name(0), (string_address)was, LAUNCHER_NAME);
                }
                // The rows run from one edge of the bar to the other, as the settings window's rows run to its window's: the lit row's fill
                // is at the bar's first and last pixel and in the margin beside the list's box, the row under it is not lit there, the
                // margin is the row's to hover and press, and the words are still in from the margin (the list's own box).
                {
                        const unsigned int address_to shown = (const unsigned int address_to)pixels;
                        int lit_y, dim_y, last = launcher.width - 1, in = launcher.list.x;
                        unsigned int lit;

                        launcher_type_text("ls");
                        launcher_draw((unsigned int address_to)pixels);
                        launcher.list.focus = launcher.list.id[0];
                        launcher_draw((unsigned int address_to)pixels);
                        lit_y = ui_list_row_y(&launcher.list, 0) + 3, dim_y = ui_list_row_y(&launcher.list, 1) + 3;
                        lit = shown[(positive)lit_y * launcher.ui.pitch + (positive)in + 1];
                        check("launcher: the lit row reaches both edges of the bar, as a settings row reaches its window's, and the row under it is not lit there",
                              launcher.entries >= 2 && in > 0 && lit != shown[(positive)dim_y * launcher.ui.pitch + (positive)in + 1] &&
                                  shown[(positive)lit_y * launcher.ui.pitch] == lit && shown[(positive)lit_y * launcher.ui.pitch + (positive)last] == lit &&
                                  shown[(positive)lit_y * launcher.ui.pitch + (positive)in - 1] == lit &&
                                  shown[(positive)dim_y * launcher.ui.pitch] != lit && shown[(positive)dim_y * launcher.ui.pitch + (positive)last] != lit);
                        check("launcher: the bar's margin beside a row is the row's to hover and press, and the words stay in from the margin",
                              ui_list_hit(&launcher.ui, &launcher.list, 0, lit_y) == launcher.list.id[0] &&
                                  ui_list_hit(&launcher.ui, &launcher.list, last, dim_y) == launcher.list.id[1] && launcher.list.x == ui_px(&launcher.ui, UI_PAD) &&
                                  launcher.list.x >= ui_px(&launcher.ui, UI_PAD) && launcher.list.w <= launcher.width - 2 * ui_px(&launcher.ui, UI_PAD));
                }
                // A row runs on its release, where its press went down: a press shows the row pressed and runs nothing, a
                // press slid off the row shows none and a release off it runs nothing, and a release on the row runs it.
                {
                        int row_y = ui_list_row_y(&launcher.list, 0) + ui_px(&launcher.ui, UI_ROW) / 2, row_x = left + 100;
                        struct window_event on = {WINDOW_EVENT_MOTION, 0, row_x, row_y}, off = {WINDOW_EVENT_MOTION, 0, row_x, row_y + 400};

                        launcher_send(&on);
                        check("launcher: a press on a row shows it pressed and runs nothing", launcher_send(&press) && launcher.list.down == 0 && launcher.list.press.down);
                        launcher_send(&off);
                        check("launcher: a press slid off its row shows none", launcher.list.down < 0 && launcher.list.press.down);
                        launcher.dirty = false;
                        launcher_send(&on);
                        check("launcher: and shows it again on coming back to it, and is drawn again", launcher.list.down == 0 && launcher.dirty);
                        launcher_send(&off);
                        check("launcher: a release off the row runs nothing", launcher_send(&release) && !launcher.list.press.down && launcher.list.down < 0);
                        launcher_send(&on);
                        launcher_send(&press);
                        check("launcher: a release on the row runs it", !launcher_send(&release) && !launcher.list.press.down);
                }
                // A row's press ripples from where it went down, as every list's row does (ui_list_click, ui_list_pointer). The kit
                // makes no ripple in motion, so it is cleared for this press, and the press is let go off its row, which runs nothing.
                {
                        int row_y = ui_list_row_y(&launcher.list, 0) + ui_px(&launcher.ui, UI_ROW) / 2, row_x = left + 100;
                        struct window_event on = {WINDOW_EVENT_MOTION, 0, row_x, row_y}, off = {WINDOW_EVENT_MOTION, 0, row_x, row_y + 400};
                        b32 motion_was = ui_kit.motion;
                        struct ui_control address_to own;

                        ui_kit.motion = 0;
                        launcher_send(&on);
                        launcher_send(&press);
                        own = ui_list_control(&launcher.list, 0, 0);
                        check("launcher: a press on a row ripples from the press, as every list row does", own && (own->ripple[0].live || own->ripple[1].live || own->ripple[2].live));
                        launcher_send(&off);
                        check("launcher: a press let go off its row runs nothing and leaves no press", launcher_send(&release) && !launcher.list.press.down);
                        ui_kit.motion = motion_was;
                }
                // The wheel turns a list longer than its window: the five desktop programs in a window one row tall, a notch a
                // row, and the list glides to it (its aim moves, and a frame is asked for).
                {
                        int pad = ui_px(&launcher.ui, UI_PAD);
                        struct window_event turn = {WINDOW_EVENT_WHEEL, 0, -120, 0};

                        launcher.text[0] = 0;
                        launcher_search();
                        launcher.height = pad * 2 + ui_px(&launcher.ui, UI_TARGET) + ui_px(&launcher.ui, UI_GAP2) + ui_px(&launcher.ui, UI_ROW);
                        launcher.dirty = false;
                        launcher_send(&turn);
                        check("launcher: the wheel turns a list longer than its window, a notch a row, with the glide",
                              launcher.entries == 5 && scroll_aim(&launcher.list.scroll) > 0 && launcher.dirty);
                }
        }

        // The settings' filter, worked by the keys: the caret moves in it, and a blank between letters parts its words.
        {
                int wifi = settings_row_named("Wi-Fi"), name = settings_row_named("Name");

                settings.ui = ui;
                settings.own.table = table_at;
                settings.width = 800, settings.height = 520;
                settings.ui.pitch = 800, settings.ui.width = 800, settings.ui.height = 520;
                settings.focused = 1, settings.page.pane = 1, settings.page.menu_row = -1, settings.page.card = 0;
                settings.child = 1, settings.queued = 0, settings.job = 0, settings.job_sets = false;
                settings_edit_end();
                settings_group_choose(0);
                settings_layout();
                for (const char address_to letter = "wifi"; *letter; letter++)
                        settings_type_code((unsigned int)*letter);
                settings_press(FIELD_KEY_LEFT);
                settings_press(FIELD_KEY_LEFT);
                settings_chord(UI_KEY_SPACE, ' ', 0);
                {
                        b32 listed = false, each = settings.list.count > 0;

                        for (positive at = 0; at < settings.list.count; at++)
                                listed |= settings.list.id[at] == wifi, each &= settings_matches(settings.list.id[at]);
                        check("settings: Left moves the caret in the filter, and a blank typed there parts its words: wi fi lists Wi-Fi",
                              check_filter_says((const char address_to) "wi fi") && listed && each);
                }
                settings_chord(FIELD_KEY_HOME, 0, WINDOW_KEY_SHIFT);
                settings_type_code('n');
                check("settings: Shift and Home select in the filter, and a letter replaces what is selected", check_filter_says((const char address_to) "nfi"));
                settings_press(FIELD_KEY_ESCAPE);
                check("settings: Escape clears the filter", !settings_filter_length());

                // The name, typed in its row: a click puts the caret, and Enter sets what the field holds.
                settings.state[name] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[name], (string_address) "patient-cricket", SETTINGS_VALUE);
                settings.list.focus = name;
                settings_press(UI_KEY_ENTER);
                settings_draw_rows((unsigned int address_to)pixels, 0, 0);
                {
#ifdef UI_TEXT_REFUSED
                        int x = field_x(&settings.ui, &settings.edit, 7) + 1, y = settings.edit.top + settings.edit.line_height / 2;
#else
                        int x = 600, y = 100;
#endif
                        struct window_event move = {WINDOW_EVENT_MOTION, 0, x, y}, press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0},
                                            release = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};

                        check("settings: Enter on the name opens its field on the name", settings_editing() == name && settings_holds("patient-cricket"));
                        settings_event_at(&move);
                        settings_event_at(&press);
                        settings_event_at(&release);
                        settings_type_code('-');
                        settings_type_code('x');
                        check("settings: a click in the name's field puts the caret there, and a key types there",
                              settings_editing() == name && settings_holds("patient-x-cricket") && settings_caret() == 9);
                        // A finger's long press in the name's field selects its word, and the picture has its handles and bar over the rows.
                        {
                                const unsigned int held = WINDOW_KEY_TOUCH | WINDOW_KEY_HOLD;
                                struct window_event at = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y}, down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0},
                                                    taken = {WINDOW_EVENT_BUTTON, held | WINDOW_KEY_CANCEL, UI_POINTER_LEFT, 0}, right = {WINDOW_EVENT_BUTTON, held | WINDOW_KEY_DOWN, UI_POINTER_RIGHT, 0},
                                                    right_up = {WINDOW_EVENT_BUTTON, held, UI_POINTER_RIGHT, 0};

                                settings_event_at(&at);
                                settings_event_at(&down);
                                settings_event_at(&taken);
                                settings_event_at(&right);
                                settings_event_at(&right_up);
                                settings_event_at(&taken);
                                settings_draw_rows((unsigned int address_to)pixels, 0, 0);
                                check("settings: a finger's long press in the name's field selects a word, and the picture shows its handles and the actions bar",
                                      settings_editing() == name && settings.edit.sel.handles && settings.edit.sel.bar_up && settings.edit.sel.bar_count >= 3);
                                {
                                        int hx, htop;
                                        const unsigned int address_to shown = (const unsigned int address_to)pixels;

                                        check("settings: and the handle's knob is drawn in the accent under the field's line, past the clip the page drew under",
                                              !ui_text_where(&settings.ui, &settings.edit, doc_sel_end(&settings.edit.doc.selections[0]), &hx, &htop) &&
                                                  (shown[(positive)(htop + settings.edit.line_height + ui_dev(&settings.ui, UI_SELECT_HANDLE)) * settings.ui.pitch + (positive)hx] & 0xffffffu) ==
                                                      (ui_role(UI_PRIMARY) & 0xffffffu));
                                }
                        }
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter sets the name to what the field holds",
                              settings_editing() < 0 && settings.queued == 1 && settings.queue[0].row == name &&
                                  string_equals((string_address)settings.queue[0].value, (string_address) "patient-x-cricket"));
                }
                // A value typed in: a field on the row with no band behind it, its status says what Enter, Escape and a click
                // do, Escape keeps the value, and a click outside sets what the field holds.
                {
                        // The pointer's place below the rows, where no row is: a click there is outside every row.
                        struct window_event move = {WINDOW_EVENT_MOTION, 0, 400, settings.height - 6}, press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0},
                                            release = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};

                        settings.queued = 0;
                        settings.page.chosen = settings_group_of(name);
                        settings_refilter();
                        settings.state[name] = SETTINGS_READ;
                        string_copy_bounded((string_address)settings.value[name], (string_address) "patient-cricket", SETTINGS_VALUE);
                        settings.list.focus = name;
                        settings_press(UI_KEY_ENTER);
                        settings_draw_rows((unsigned int address_to)pixels, 0, 0);
                        check("settings: a value typed in is a field on the row, with no band behind it", settings_editing() == name && !settings.list.lit);
                        check("settings: a value typed in says what Enter, Escape and a click outside do",
                              string_equals((string_address)settings.status, (string_address) "Enter sets it, Escape keeps it, a click outside sets it too"));
                        settings_type_code('y');
                        settings_press(FIELD_KEY_ESCAPE);
                        check("settings: Escape keeps the value as it was", settings_editing() < 0 && settings.queued == 0);
                        settings_press(UI_KEY_ENTER);
                        settings_type_code('y');
                        settings_event_at(&move);
                        settings_event_at(&press);
                        settings_event_at(&release);
                        check("settings: a click outside a value sets what its field holds",
                              settings_editing() < 0 && settings.queued == 1 && settings.queue[0].row == name &&
                                  string_equals((string_address)settings.queue[0].value, (string_address) "patient-crickety"));
                        // A command is run by Enter alone: the same click outside its field leaves it as Escape does. The filter lists
                        // the row that runs any command with no row under the pointer, so the click is outside every row.
                        {
                                b32 typed;

                                settings.queued = 0;
                                for (const char address_to letter = "anything"; *letter; letter++)
                                        settings_type_code((unsigned int)*letter);
                                settings.list.focus = SETTINGS_ANY;
                                settings_press(UI_KEY_ENTER);
                                settings_draw_rows((unsigned int address_to)pixels, 0, 0);
                                for (const char address_to letter = "name lane-out"; *letter; letter++)
                                        settings_type_code((unsigned int)*letter);
                                typed = settings_editing() == SETTINGS_ANY && settings_holds("name lane-out") &&
                                        ui_list_hit(&settings.ui, &settings.list, 400, settings.height - 6) < 0;
                                settings_event_at(&move);
                                settings_event_at(&press);
                                settings_event_at(&release);
                                check("settings: a click outside a command being typed runs nothing, and leaves its field as Escape does",
                                      typed && settings_editing() < 0 && settings.queued == 0);
                                settings.queued = 0;
                                settings_press(FIELD_KEY_ESCAPE);
                        }
                }
                // A row is done on its mouse's release, where the press went down: the press alone opens nothing, the release on
                // the row opens it, and a press let go off its row opens nothing.
                {
                        int y = ui_list_row_y(&settings.list, settings_pos(name)) + ui_px(&settings.ui, UI_ROW) / 2;
                        struct window_event on = {WINDOW_EVENT_MOTION, 0, 400, y}, off = {WINDOW_EVENT_MOTION, 0, 400, 480},
                                            press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0},
                                            release = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};

                        settings.queued = 0;
                        settings_edit_end();
                        settings.state[name] = SETTINGS_READ;
                        string_copy_bounded((string_address)settings.value[name], (string_address) "patient-cricket", SETTINGS_VALUE);
                        settings_event_at(&on);
                        settings_event_at(&press);
                        check("settings: a mouse press on a row is not done until it is let go", settings_editing() < 0);
                        settings_event_at(&release);
                        check("settings: a mouse's release on the row does it", settings_editing() == name);
                        settings_edit_end();
                        settings_event_at(&on);
                        settings_event_at(&press);
                        settings_event_at(&off);
                        settings_event_at(&release);
                        check("settings: a press let go off its row does nothing", settings_editing() < 0 && settings.queued == 0);
                }
                settings.queued = 0, settings.child = 0;
                settings_group_choose(0);
        }
        check_input_matrix(&ui, (unsigned int address_to)pixels, table_at);
        compositor_call(syscall(munmap), (positive)pixels, 800 * 600 * 4, 0, 0, 0, 0);
}
#endif

#if MOONWATER_UI_SETTINGS && MOONWATER_UI_LAUNCHER
/*
        The input matrix: every text input of Canvas against the features every one has. An input is opened empty, typed
        its sample as a person types it, driven through the entry its window sends its events to (a kit field's
        ui_field_event, launcher_event, settings_event or editor_event), and read back from its field. A feature an input
        lacks is a line of the log naming the input and the feature. The one seam is the clipboard: the compositor answers
        a paste with bytes this check cannot make, so a paste's text is given to the field the way its clip answer gives it
        (ui_text_paste), after the ask was seen going out through the window's table. Where a rule takes a feature away (a
        name has no blank, a passphrase has no words and no copy) the feature is the rule's, and the table says so.
*/
#define INPUTS_ENGINE 0   // a kit field on the engine: arg 0 plain, 1 masked, 2 digits, 3 a machine name
#define INPUTS_LAUNCHER 1 // the launcher's search
#define INPUTS_FILTER 2   // the settings' search field
#define INPUTS_ROW 3      // a settings row typed in: by arg, or by its label
#define INPUTS_GALLERY 4  // the settings gallery's fields: arg 0 the node name, 1 the region
#define INPUTS_FIND 5     // the editor's bar: arg 1 find, 2 replace, 3 go to a line
#define INPUTS_VIEW 6     // the editor's document

struct inputs_input
{
        const char address_to name;
        int kind, arg;
        const char address_to label;   // a settings row by its label, when there is one
        b32 masked;                    // a passphrase: dots, nothing copied out
        const char address_to sample;  // typed as a person types it: "one two" where the words are checked
        char fill, refuse;             // a character its rule takes past its room, and one it refuses (0: none)
        b32 app_vertical, single, words, spaces, first_space, unicode, limited;
};

static struct ui inputs_ui;
static struct windows address_to inputs_windows;
static struct window_row inputs_own;
static struct ui_popup inputs_popup;
static struct ui_text inputs_plain, inputs_masked, inputs_digits, inputs_name;
static unsigned int address_to inputs_pixels;
static int inputs_pointer_x, inputs_pointer_y;

static fn inputs_check(const char address_to label, b32 okay)
{
        checks++;
        if (!okay)
        {
                failures++;
                string_format(log, "  FAIL %s\n", (string_address)label);
                log_flush();
        }
}

static fn inputs_engine_open(struct ui_text address_to t, b32 masked, const char address_to (*accept)(p32 code), positive most, const char address_to said)
{
        if (t->doc.selections)
                ui_text_close(t);
        ui_field_open(t, &inputs_own, "");
        t->masked = masked, t->accept = accept, t->most = most, t->most_said = said, t->focused = true;
}

static struct ui_text address_to inputs_target(const struct inputs_input address_to in)
{
        switch (in->kind)
        {
        case INPUTS_ENGINE: return in->arg == 0 ? &inputs_plain : in->arg == 1 ? &inputs_masked : in->arg == 2 ? &inputs_digits : &inputs_name;
        case INPUTS_LAUNCHER: return launcher_field();
        case INPUTS_FILTER: return settings_filter_field();
        case INPUTS_ROW: return &settings.edit;
        case INPUTS_GALLERY: return &gallery.field[in->arg];
        case INPUTS_FIND: return &editor.find.field;
        case INPUTS_VIEW: return &editor_now()->view;
        }
        return &inputs_plain;
}

static fn inputs_editor_ensure(struct windows address_to table)
{
        editor.ui = inputs_ui;
        editor.own.table = table, editor.own.row = 0, editor.own.door = -1;
        if (editor.count == 0)
                editor_open(null);
        editor.width = 400, editor.height = 300;
        editor.focused = true;
}

// The input empty and focused, its rules as its app sets them, and its keys its window's.
static fn inputs_open(const struct inputs_input address_to in)
{
        switch (in->kind)
        {
        case INPUTS_ENGINE:
                if (in->arg == 2)
                        inputs_engine_open(inputs_target(in), false, ui_field_digits, 19, null);
                else if (in->arg == 3)
                        inputs_engine_open(inputs_target(in), false, settings_name_accepts, SETTINGS_NAME_MOST, "a name is under 32 characters");
                else
                        inputs_engine_open(inputs_target(in), in->masked, null, 0, null);
                break;
        case INPUTS_LAUNCHER:
                launcher_clear();
                break;
        case INPUTS_FILTER:
                settings_edit_end();
                gallery.on = 0;
                settings.popup.open = 0, settings.page.menu_row = -1;
                settings_group_choose(0);
                settings_layout();
                settings_filter_field();
                settings_filter_clear();
                break;
        case INPUTS_ROW:
        {
                int row = in->label ? settings_row_named(in->label) : in->arg;

                settings_edit_end();
                gallery.on = 0;
                if (row >= (int)SETTINGS_LIST)
                {
                        // The devices list as a feed of no devices leaves it: its one row is the Add row, which is what this is.
                        struct settings_item address_to items = settings_large->item[1];

                        memory_zero(items, sizeof(settings_large->item[1]));
                        items[0].kind = SETTINGS_ADD;
                        settings_item_label(&items[0], "Keep a device by its name", string_length((string_address) "Keep a device by its name"));
                        settings.item_count[1] = 1;
                        settings_refilter();
                }
                settings_group_choose(settings_group_of(row));
                settings_layout();
                settings_edit_begin(row, "", in->masked);
                settings.list.focus = row;
                settings_layout();
                ui_page_reveal(&settings.ui, &settings.page, &settings.list);
                break;
        }
        case INPUTS_GALLERY:
                settings_edit_end();
                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
                gallery.on = 1;
                for (int k = 0; k < 2; k++)
                        gallery.field[k].focused = k == in->arg;
                ui_text_set(&gallery.field[in->arg], (const p8 address_to) "", 0);
                break;
        case INPUTS_FIND:
                inputs_editor_ensure(inputs_windows);
                editor.find.mode = UI_FINDBAR_CLOSED;
                ui_findbar_open(&editor.own, &editor.find, &editor_now()->view, in->arg, editor.focused);
                ui_text_set(&editor.find.field, (const p8 address_to) "", 0);
                editor.find.field.focused = true;
                break;
        case INPUTS_VIEW:
                inputs_editor_ensure(inputs_windows);
                editor.find.mode = UI_FINDBAR_CLOSED;
                editor.find.field.focused = false;
                ui_text_set(&editor_now()->view, (const p8 address_to) "", 0);
                editor_now()->view.focused = true;
                break;
        }
}

// One window event, as the window sends it to the input's app (a kit field takes it at its own entry).
static fn inputs_event(const struct inputs_input address_to in, const struct window_event address_to event)
{
        switch (in->kind)
        {
        case INPUTS_ENGINE:
                if (event->type == WINDOW_EVENT_MOTION)
                        inputs_pointer_x = event->a, inputs_pointer_y = event->b;
                ui_field_event(&inputs_ui, inputs_target(in), &inputs_popup, event, inputs_pointer_x, inputs_pointer_y);
                break;
        case INPUTS_LAUNCHER:
                launcher_send(event);
                break;
        case INPUTS_FILTER: case INPUTS_ROW: case INPUTS_GALLERY:
                settings_event_at(event);
                break;
        case INPUTS_FIND: case INPUTS_VIEW:
                ui_event_context(event);
                editor_event(event);
                break;
        }
}

// Drawn as its window draws it, so the well the hand reaches is where it is.
static fn inputs_draw(const struct inputs_input address_to in)
{
        switch (in->kind)
        {
        case INPUTS_ENGINE: ui_text_draw(&inputs_ui, inputs_target(in), 20, 20, 300, 30, 0); break;
        case INPUTS_LAUNCHER: launcher_draw(inputs_pixels); break;
        case INPUTS_FILTER: case INPUTS_ROW: case INPUTS_GALLERY: settings_draw_rows(inputs_pixels, 0, 0); break;
        case INPUTS_FIND: editor_draw(inputs_pixels); break;
        case INPUTS_VIEW: ui_text_draw(&inputs_ui, &editor_now()->view, 0, 0, 400, 200, 0x111113); break;
        }
}

static struct ui_popup address_to inputs_popup_of(const struct inputs_input address_to in)
{
        switch (in->kind)
        {
        case INPUTS_LAUNCHER: return &launcher.menu;
        case INPUTS_FILTER: case INPUTS_ROW: case INPUTS_GALLERY: return &settings.popup;
        case INPUTS_FIND: case INPUTS_VIEW: return &editor.menu;
        }
        return &inputs_popup;
}

static fn inputs_key(const struct inputs_input address_to in, int key, unsigned int code, unsigned int flags)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | flags, key, (int)code};

        inputs_event(in, &event);
}

// Each character of text as a key that types it, as a person types it.
static fn inputs_type(const struct inputs_input address_to in, const char address_to text)
{
        positive length = string_length((string_address)text);

        for (positive at = 0; at < length;)
        {
                p32 code;
                positive size = doc_decode((const p8 address_to)text + at, length - at, &code);

                inputs_key(in, 0, code, 0);
                at += size ? size : 1;
        }
}

static fn inputs_begin(const struct inputs_input address_to in)
{
        inputs_open(in);
        inputs_type(in, in->sample);
}

static fn inputs_clear(const struct inputs_input address_to in)
{
        inputs_key(in, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        inputs_key(in, FIELD_KEY_BACKSPACE, 0, 0);
}

static fn inputs_move(const struct inputs_input address_to in, int x, int y)
{
        struct window_event event = {WINDOW_EVENT_MOTION, 0, x, y};

        inputs_event(in, &event);
}

static fn inputs_button(const struct inputs_input address_to in, int button, b32 down, int x, int y, unsigned int flags)
{
        struct window_event event = {WINDOW_EVENT_BUTTON, (down ? WINDOW_KEY_DOWN : 0) | flags, button, 0};

        inputs_move(in, x, y);
        inputs_event(in, &event);
}

static fn inputs_click(const struct inputs_input address_to in, int button, int x, int y, unsigned int flags)
{
        inputs_button(in, button, true, x, y, flags);
        inputs_button(in, button, false, x, y, flags);
}

static int inputs_x(const struct inputs_input address_to in, p64 offset)
{
        return field_x(&inputs_ui, inputs_target(in), offset);
}

static int inputs_y(const struct inputs_input address_to in)
{
        return inputs_target(in)->top + inputs_target(in)->line_height / 2;
}

// A field that is closed has no caret and no selection: an input that closed under its check fails it, and does not fault.
static p64 inputs_caret(const struct inputs_input address_to in)
{
        return inputs_target(in)->doc.selections ? field_caret(inputs_target(in)) : ~(p64)0;
}

static b32 inputs_sel(const struct inputs_input address_to in, p64 start, p64 finish)
{
        return inputs_target(in)->doc.selections && field_selected(inputs_target(in), start, finish);
}

static b32 inputs_is(const struct inputs_input address_to in, const char address_to want)
{
        return field_is(inputs_target(in), want);
}

static b32 inputs_is_part(const struct inputs_input address_to in, positive from, positive to)
{
        char want[256];
        positive k = 0;

        for (positive at = from; at < to && k < sizeof(want) - 1; at++)
                want[k++] = in->sample[at];
        want[k] = 0;
        return inputs_is(in, want);
}

static positive inputs_length(const struct inputs_input address_to in)
{
        return inputs_target(in)->doc.selections ? (positive)doc_length(&inputs_target(in)->doc) : 0;
}

static b32 inputs_offer_moved(unsigned int before)
{
        return inputs_windows->clip_offer_serial[0] != before;
}

static b32 inputs_f_left_right(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        if (inputs_caret(in) != n - 2)
                return false;
        inputs_key(in, FIELD_KEY_RIGHT, 0, 0);
        return inputs_caret(in) == n - 1 && inputs_is(in, in->sample);
}

// Up and Down are the app's where it takes them (a list, rows, a scroll); where it does not they go to the two ends.
static b32 inputs_f_up_down(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        if (!in->single)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_UP, 0, 0);
        if (in->app_vertical)
                return inputs_caret(in) == n - 2 && inputs_is(in, in->sample);
        if (inputs_caret(in) != 0)
                return false;
        inputs_key(in, FIELD_KEY_DOWN, 0, 0);
        if (inputs_caret(in) != n)
                return false;
        inputs_key(in, FIELD_KEY_UP, 0, WINDOW_KEY_SHIFT);
        return inputs_sel(in, 0, n);
}

static b32 inputs_f_home_end(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_HOME, 0, 0);
        if (inputs_caret(in) != 0)
                return false;
        inputs_key(in, FIELD_KEY_END, 0, 0);
        return inputs_caret(in) == n;
}

static b32 inputs_f_word_moves(const struct inputs_input address_to in)
{
        if (!in->words)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL);
        if (inputs_caret(in) != 4)
                return false;
        inputs_key(in, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL);
        if (inputs_caret(in) != 0)
                return false;
        inputs_key(in, FIELD_KEY_RIGHT, 0, WINDOW_KEY_CONTROL);
        if (inputs_caret(in) != 3)
                return false;
        inputs_key(in, FIELD_KEY_RIGHT, 0, WINDOW_KEY_CONTROL);
        return inputs_caret(in) == 7;
}

static b32 inputs_f_shift_select(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        for (int k = 0; k < 3; k++)
                inputs_key(in, FIELD_KEY_LEFT, 0, WINDOW_KEY_SHIFT);
        if (!inputs_sel(in, n - 3, n))
                return false;
        inputs_key(in, FIELD_KEY_HOME, 0, WINDOW_KEY_SHIFT);
        return inputs_sel(in, 0, n);
}

static b32 inputs_f_shift_home_end(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        for (int k = 0; k < 3; k++)
                inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_HOME, 0, WINDOW_KEY_SHIFT);
        if (!inputs_sel(in, 0, n - 3))
                return false;
        inputs_key(in, FIELD_KEY_END, 0, WINDOW_KEY_SHIFT);
        return inputs_sel(in, n - 3, n);
}

static b32 inputs_f_select_all(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        inputs_key(in, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        return inputs_sel(in, 0, n);
}

static b32 inputs_f_backspace_delete(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_key(in, FIELD_KEY_BACKSPACE, 0, 0);
        if (!inputs_is_part(in, 0, n - 1))
                return false;
        inputs_key(in, FIELD_KEY_HOME, 0, 0);
        inputs_key(in, FIELD_KEY_DELETE, 0, 0);
        return inputs_is_part(in, 1, n - 1);
}

static b32 inputs_f_word_deletes(const struct inputs_input address_to in)
{
        if (!in->words)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_key(in, FIELD_KEY_BACKSPACE, 0, WINDOW_KEY_CONTROL);
        if (!inputs_is_part(in, 0, 4))
                return false;
        inputs_key(in, FIELD_KEY_HOME, 0, 0);
        inputs_key(in, FIELD_KEY_DELETE, 0, WINDOW_KEY_CONTROL);
        return inputs_is_part(in, 3, 4);
}

// What is typed last is undone by Control-Z and redone by Control-Y or Control-Shift-Z.
static b32 inputs_f_undo_redo(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_key(in, 0, (unsigned int)in->fill, 0);
        if (inputs_length(in) != n + 1)
                return false;
        inputs_key(in, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
        if (inputs_length(in) > n)
                return false;
        inputs_key(in, 21, 25, WINDOW_KEY_CONTROL);
        if (inputs_length(in) != n + 1)
                return false;
        inputs_key(in, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
        if (inputs_length(in) > n)
                return false;
        inputs_key(in, FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT);
        return inputs_length(in) == n + 1;
}

static b32 inputs_f_cut(const struct inputs_input address_to in)
{
        unsigned int before;

        if (in->masked)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        before = inputs_windows->clip_offer_serial[0];
        inputs_key(in, 45, 24, WINDOW_KEY_CONTROL);
        return inputs_length(in) == 0 && inputs_offer_moved(before);
}

static b32 inputs_f_copy(const struct inputs_input address_to in)
{
        unsigned int before;

        if (in->masked)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        before = inputs_windows->clip_offer_serial[0];
        inputs_key(in, 46, 3, WINDOW_KEY_CONTROL);
        return inputs_is(in, in->sample) && inputs_offer_moved(before);
}

// Paste asks the clipboard (Control-V and Shift-Insert) and its answer is one line: a line end is a blank, and a rule
// takes what it takes of the answer, a refused one going in not at all.
static b32 inputs_f_paste(const struct inputs_input address_to in)
{
        unsigned int before;
        b32 asked, asked_shift;

        inputs_begin(in);
        inputs_clear(in);
        before = inputs_windows->clip_ask_serial[0];
        inputs_key(in, 47, 22, WINDOW_KEY_CONTROL);
        asked = inputs_windows->clip_ask_serial[0] != before && inputs_target(in)->pasting;
        inputs_target(in)->pasting = false;
        before = inputs_windows->clip_ask_serial[0];
        inputs_key(in, 110, 0, WINDOW_KEY_SHIFT);
        asked_shift = inputs_windows->clip_ask_serial[0] != before && inputs_target(in)->pasting;
        inputs_target(in)->pasting = false;
        if (!asked || !asked_shift)
                return false;
        if (!in->single)
                return true;
        ui_text_paste(inputs_target(in), (const p8 address_to) "one\ntwo", 7);
        if (in->refuse)
                return inputs_length(in) == 0 && inputs_target(in)->refused != null;
        return inputs_is(in, "one two");
}

static b32 inputs_f_primary_paste(const struct inputs_input address_to in)
{
        unsigned int before;
        b32 asked;

        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        before = inputs_windows->clip_ask_serial[0];
        inputs_click(in, 0x112, inputs_x(in, 0), inputs_y(in), 0);
        asked = inputs_windows->clip_ask_serial[0] != before && inputs_windows->clip_ask_which[0] == WINDOWS_CLIP_PRIMARY;
        inputs_target(in)->pasting = false;
        return asked && inputs_caret(in) == 0;
}

static b32 inputs_f_click_caret(const struct inputs_input address_to in)
{
        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        inputs_click(in, 0x110, inputs_x(in, 2), inputs_y(in), 0);
        return inputs_caret(in) == 2 && inputs_target(in)->doc.selection_count == 1;
}

// A drag selects, and letting go offers what it selected as the primary selection, which a middle click pastes (a
// passphrase offers nothing).
static b32 inputs_f_drag_select(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);
        unsigned int before;
        int y;

        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        y = inputs_y(in);
        before = inputs_windows->clip_offer_serial[0];
        inputs_button(in, 0x110, true, inputs_x(in, 0), y, 0);
        inputs_move(in, inputs_x(in, n), y);
        inputs_button(in, 0x110, false, inputs_x(in, n), y, 0);
        if (in->masked)
                return inputs_sel(in, 0, n) && !inputs_offer_moved(before);
        return inputs_sel(in, 0, n) && inputs_offer_moved(before) && inputs_windows->clip_offer_which[0] == WINDOWS_CLIP_PRIMARY;
}

// A passphrase has no words, so its double click takes all of it, as its triple click does.
static b32 inputs_f_double_click(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);
        int x;

        if (!in->masked && !in->words)
                return true;
        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        x = inputs_x(in, in->masked ? 2 : 5);
        inputs_click(in, 0x110, x, inputs_y(in), 0);
        inputs_click(in, 0x110, x, inputs_y(in), 0);
        return in->masked ? inputs_sel(in, 0, n) : inputs_sel(in, 4, 7);
}

static b32 inputs_f_triple_click(const struct inputs_input address_to in)
{
        positive n = string_length((string_address)in->sample);

        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        for (int k = 0; k < 3; k++)
                inputs_click(in, 0x110, inputs_x(in, 2), inputs_y(in), 0);
        return inputs_sel(in, 0, n);
}

// The right button asks for the input's menu: Undo, Redo, Cut, Copy, Paste, Delete and Select All, in its popup.
static b32 inputs_f_right_menu(const struct inputs_input address_to in)
{
        struct ui_popup address_to p = inputs_popup_of(in);
        unsigned int seen = 0, all = (1u << UI_TEXT_MENU_UNDO) | (1u << UI_TEXT_MENU_REDO) | (1u << UI_TEXT_MENU_CUT) |
                                    (1u << UI_TEXT_MENU_COPY) | (1u << UI_TEXT_MENU_PASTE) | (1u << UI_TEXT_MENU_DELETE) |
                                    (1u << UI_TEXT_MENU_ALL);

        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        p->field = null;
        p->menu.count = 0;
        inputs_click(in, 0x111, inputs_x(in, 2), inputs_y(in), 0);
        for (int j = 0; j < p->menu.count; j++)
                seen |= 1u << (p->menu.items[j].id & 31);
        return (seen & all) == all && (!in->single || p->field == inputs_target(in));
}

static b32 inputs_f_space_typed(const struct inputs_input address_to in)
{
        char want[256];
        positive n = string_length((string_address)in->sample);

        if (!in->spaces)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_type(in, " x");
        string_copy_bounded((string_address)want, (string_address)in->sample, sizeof(want));
        string_copy_bounded((string_address)want + n, (string_address) " x", sizeof(want) - n);
        return inputs_is(in, want);
}

// A blank typed first is typed as any blank is. The search field's one exception is the rows': with nothing in it a
// Space works the row (table: first_space false), so the field does not take it first.
static b32 inputs_f_space_first(const struct inputs_input address_to in)
{
        if (!in->spaces || !in->first_space)
                return true;
        inputs_begin(in);
        inputs_clear(in);
        inputs_type(in, " x");
        return inputs_is(in, " x");
}

static b32 inputs_f_unicode(const struct inputs_input address_to in)
{
        if (!in->unicode)
                return true;
        inputs_begin(in);
        inputs_clear(in);
        inputs_type(in, "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80" "b");
        if (!inputs_is(in, "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80" "b"))
                return false;
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        inputs_key(in, FIELD_KEY_LEFT, 0, 0);
        if (inputs_caret(in) != 6)
                return false;
        inputs_key(in, FIELD_KEY_BACKSPACE, 0, 0);
        return inputs_is(in, "a\xc3\xa9\xf0\x9f\x98\x80" "b");
}

// A rule refuses its character with its words, and a field past its room refuses what would go over it.
static b32 inputs_f_limits(const struct inputs_input address_to in)
{
        positive most;

        inputs_begin(in);
        if (in->refuse)
        {
                inputs_clear(in);
                inputs_target(in)->refused = null;
                inputs_key(in, 0, (unsigned int)in->refuse, 0);
                if (inputs_length(in) != 0 || !inputs_target(in)->refused)
                        return false;
        }
        if (!in->limited)
                return true;
        most = inputs_target(in)->most;
        inputs_clear(in);
        inputs_target(in)->refused = null;
        for (positive k = 0; k < most + 8; k++)
                inputs_key(in, 0, (unsigned int)in->fill, 0);
        return inputs_length(in) == most && inputs_target(in)->refused != null;
}

// A passphrase is never copied out: a drag's copy, Control-C, Control-X, Control-Insert and Shift-Delete are all nothing.
static b32 inputs_f_masked_no_copy(const struct inputs_input address_to in)
{
        unsigned int before;
        positive n = string_length((string_address)in->sample);

        if (!in->masked)
                return true;
        inputs_begin(in);
        inputs_draw(in);
        inputs_target(in)->sel.click.at = 0;
        before = inputs_windows->clip_offer_serial[0];
        inputs_button(in, 0x110, true, inputs_x(in, 0), inputs_y(in), 0);
        inputs_move(in, inputs_x(in, n), inputs_y(in));
        inputs_button(in, 0x110, false, inputs_x(in, n), inputs_y(in), 0);
        inputs_key(in, FIELD_KEY_A, 1, WINDOW_KEY_CONTROL);
        inputs_key(in, 46, 3, WINDOW_KEY_CONTROL);
        inputs_key(in, 45, 24, WINDOW_KEY_CONTROL);
        inputs_key(in, 110, 0, WINDOW_KEY_CONTROL);
        inputs_key(in, FIELD_KEY_DELETE, 0, WINDOW_KEY_SHIFT);
        return !inputs_offer_moved(before) && inputs_is(in, in->sample);
}

// A passphrase has no words: Control-Left goes to its start, and Control-Backspace takes all of it.
static b32 inputs_f_masked_no_words(const struct inputs_input address_to in)
{
        if (!in->masked)
                return true;
        inputs_begin(in);
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_key(in, FIELD_KEY_LEFT, 0, WINDOW_KEY_CONTROL);
        if (inputs_caret(in) != 0)
                return false;
        inputs_key(in, FIELD_KEY_END, 0, 0);
        inputs_key(in, FIELD_KEY_BACKSPACE, 0, WINDOW_KEY_CONTROL);
        return inputs_length(in) == 0;
}

static const struct
{
        const char address_to name;
        b32 (*run)(const struct inputs_input address_to in);
} inputs_features[] = {
    {"left and right move the caret", inputs_f_left_right},
    {"up and down (to the ends, where the app does not take them)", inputs_f_up_down},
    {"home and end", inputs_f_home_end},
    {"ctrl-left and ctrl-right move by word", inputs_f_word_moves},
    {"shift-arrows select", inputs_f_shift_select},
    {"shift-home and shift-end select", inputs_f_shift_home_end},
    {"ctrl-a selects all", inputs_f_select_all},
    {"backspace and delete", inputs_f_backspace_delete},
    {"ctrl-backspace and ctrl-delete take a word", inputs_f_word_deletes},
    {"ctrl-z undoes, ctrl-y and ctrl-shift-z redo", inputs_f_undo_redo},
    {"ctrl-x cuts", inputs_f_cut},
    {"ctrl-c copies", inputs_f_copy},
    {"ctrl-v pastes one line", inputs_f_paste},
    {"middle click pastes the primary selection", inputs_f_primary_paste},
    {"a click puts the caret", inputs_f_click_caret},
    {"a drag selects", inputs_f_drag_select},
    {"a double click takes the word", inputs_f_double_click},
    {"a triple click takes the line", inputs_f_triple_click},
    {"a space typed anywhere", inputs_f_space_typed},
    {"a space typed first", inputs_f_space_first},
    {"unicode typed and moved over by whole characters", inputs_f_unicode},
    {"its rule refuses with its words, and its length holds", inputs_f_limits},
    {"a passphrase is never copied out", inputs_f_masked_no_copy},
    {"a passphrase has no words", inputs_f_masked_no_words},
    {"the right button asks for the menu (undo, redo, cut, copy, paste, delete, select all)", inputs_f_right_menu},
};

// Every text input of Canvas, by what it is: the kind and arg say which widget and app it is, and the flags what its
// rules and its app leave it. The sample is what is typed first; a ruled input's sample is what its rule takes.
static const struct inputs_input inputs_table[] = {
    {.name = "a kit field (the engine's, plain)", .kind = INPUTS_ENGINE, .arg = 0, .sample = "one two", .fill = 'a',
     .single = true, .words = true, .spaces = true, .first_space = true, .unicode = true},
    {.name = "a passphrase (a kit field, masked)", .kind = INPUTS_ENGINE, .arg = 1, .masked = true, .sample = "one two", .fill = 'a',
     .single = true, .spaces = true, .first_space = true, .unicode = true},
    {.name = "a number (a kit field, digits only)", .kind = INPUTS_ENGINE, .arg = 2, .sample = "1234", .fill = '7', .refuse = 'a',
     .single = true, .limited = true},
    {.name = "a machine name (a kit field, the name rule)", .kind = INPUTS_ENGINE, .arg = 3, .sample = "one-two", .fill = 'a', .refuse = ' ',
     .single = true, .limited = true},
    {.name = "the launcher search", .kind = INPUTS_LAUNCHER, .sample = "one two", .fill = 'a', .app_vertical = true, .single = true,
     .words = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the settings search (the filter)", .kind = INPUTS_FILTER, .sample = "one two", .fill = 'a', .app_vertical = true, .single = true,
     .words = true, .spaces = true, .unicode = true, .limited = true},
    {.name = "the settings command field (All commands)", .kind = INPUTS_ROW, .arg = SETTINGS_ANY, .sample = "one two", .fill = 'a',
     .single = true, .words = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the settings row Name", .kind = INPUTS_ROW, .arg = SETTINGS_NAME_ROW, .sample = "one-two", .fill = 'a', .refuse = ' ',
     .single = true, .limited = true},
    {.name = "the settings row Time server", .kind = INPUTS_ROW, .label = "Time server (auto for the pool)", .sample = "one two", .fill = 'a',
     .single = true, .words = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the settings row Name servers (DNS)", .kind = INPUTS_ROW, .label = "Name servers (auto for the network)", .sample = "one two",
     .fill = 'a', .single = true, .words = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the settings passphrase (masked, a network's)", .kind = INPUTS_ROW, .label = "Time server (auto for the pool)", .masked = true,
     .sample = "one two", .fill = 'a', .single = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the settings add-a-device field", .kind = INPUTS_ROW, .arg = SETTINGS_LIST + SETTINGS_LIST_ROWS, .sample = "one two", .fill = 'a', .single = true,
     .words = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the settings gallery node name", .kind = INPUTS_GALLERY, .arg = 0, .sample = "one-two", .fill = 'a', .refuse = ' ',
     .app_vertical = true, .single = true, .limited = true},
    {.name = "the settings gallery region", .kind = INPUTS_GALLERY, .arg = 1, .sample = "one two", .fill = 'a', .app_vertical = true,
     .single = true, .words = true, .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the editor find bar", .kind = INPUTS_FIND, .arg = 1, .sample = "one two", .fill = 'a', .single = true, .words = true,
     .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the editor replace bar", .kind = INPUTS_FIND, .arg = 2, .sample = "one two", .fill = 'a', .single = true, .words = true,
     .spaces = true, .first_space = true, .unicode = true, .limited = true},
    {.name = "the editor go-to-line bar", .kind = INPUTS_FIND, .arg = 3, .sample = "1234", .fill = '7', .refuse = 'a', .single = true,
     .limited = true},
    {.name = "the editor document (the code view)", .kind = INPUTS_VIEW, .sample = "one two", .fill = 'a', .words = true, .spaces = true,
     .first_space = true, .unicode = true},
};

/*
        Runs the matrix: every input against every feature, one check a pair, named by both. The window's state is set up
        as check_field_hands sets it (its ui, its table, its pixels), and the gallery is shut again when it is done.
*/
static fn check_input_matrix(struct ui address_to ui, unsigned int address_to pixels, struct windows address_to table)
{
        inputs_ui = *ui;
        inputs_pixels = pixels;
        inputs_windows = table;
        inputs_own.table = table, inputs_own.row = 0, inputs_own.door = -1;
        ui_begin(&inputs_ui, pixels, 800, 800, 600);
        for (positive i = 0; i < sizeof(inputs_table) / sizeof(inputs_table[0]); i++)
                for (positive f = 0; f < sizeof(inputs_features) / sizeof(inputs_features[0]); f++)
                {
                        const struct inputs_input address_to in = &inputs_table[i];
                        char label[192];
                        b32 okay = inputs_features[f].run(in);

                        string_copy_bounded((string_address)label, (string_address) "inputs: ", sizeof(label));
                        string_append_bounded((string_address)label, (string_address)in->name, sizeof(label));
                        string_append_bounded((string_address)label, (string_address) ": ", sizeof(label));
                        string_append_bounded((string_address)label, (string_address)inputs_features[f].name, sizeof(label));
                        inputs_check(label, okay);
                }
        gallery.on = 0;
        settings_edit_end();
}

#endif


/*
        The menu (ui_popmenu_*) under a random stream of keys, motion, wheel and buttons with the repository's faces:
        what it answers chosen is always an item that can be chosen, the keys never leave the hot item on a separator,
        a header or a disabled item, Escape and Left close it, a mnemonic chooses its item, and it draws inside its size.
*/
// The check's one random stream: what the menu draws from it moves where the frames check starts, which any start holds.
static p32 menu_next(p32 below)
{
        return render_next(below);
}

/*
        The kit's menus, held to one set of defaults (ui.c's menu section): a menu chooses on a release over the item its press
        went down on and on no other; drawn opaque it still has no face (a menu is glass, a temporary overlay); its size comes
        from the UI_MENU_ numbers, the one place they are, as its tree lays them out; and a menu whose items do not all show has
        the kit's bar. The kit that has these defines UI_MENU_DEFAULTS; before it, the checks that need its names are red.
*/
static fn menu_defaults_items(struct ui address_to wui, struct ui_popmenu address_to m)
{
        memory_zero(m, sizeof(*m));
        m->press.down = false;
        ui_popmenu_add(m, "Copy", null, 0, 1);
        ui_popmenu_add(m, "Paste", null, 0, 2);
        ui_popmenu_add(m, "Select all", null, 0, 3);
        ui_popmenu_size(wui, m, 0);
}

static fn menu_defaults_check(struct ui address_to wui)
{
        static struct ui_popmenu m;
        long pixels = ui_map(600 * 900 * 4);
        unsigned int address_to picture = (unsigned int address_to)pixels;
        struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0}, release = {WINDOW_EVENT_BUTTON, 0, 0x110, 0};
        int row, pad, x, y1, y2, chosen_same, chosen_other, chosen_away;

        menu_defaults_items(wui, &m);
        row = popmenu_row(&m), pad = UI_MENU_PAD * wui->scale / 100, x = m.width / 2;
        y1 = pad + row + row / 2, y2 = pad + 2 * row + row / 2;
        menu_defaults_items(wui, &m);
        ui_popmenu_event(wui, &m, &press, x, y1);
        chosen_other = ui_popmenu_event(wui, &m, &release, x, y2);
        menu_defaults_items(wui, &m);
        ui_popmenu_event(wui, &m, &press, x, y1);
        chosen_same = ui_popmenu_event(wui, &m, &release, x, y1);
        menu_defaults_items(wui, &m);
        ui_popmenu_event(wui, &m, &press, x, y1);
        chosen_away = ui_popmenu_event(wui, &m, &release, x, m.height + 40);
        check("menu: a press on an item and a release on it chooses it", chosen_same == 2);
        check("menu: a press released on another item chooses nothing", chosen_other == UI_MENU_NOTHING);
        check("menu: a press released outside the menu chooses nothing, and is consumed", chosen_away == UI_MENU_NOTHING);
        // The menu's press is the kit's (ui_press): one taken back chooses nothing and shows nothing pressed, as a button's does.
        {
                struct window_event taken = {WINDOW_EVENT_BUTTON, WINDOW_KEY_CANCEL, 0x110, 0};
                int chosen_taken;

                menu_defaults_items(wui, &m);
                ui_popmenu_event(wui, &m, &press, x, y1);
                check("menu: a press on an item shows it pressed at once", ui_pressed(&m.press, ui_popmenu_at(&m, x, y1)));
                chosen_taken = ui_popmenu_event(wui, &m, &taken, x, y1);
                check("menu: a press taken back chooses nothing, and shows nothing pressed", chosen_taken == UI_MENU_NOTHING && !m.press.down);
        }
        // A finger's press is the kit's press: moved past the slop it is a drag, and a lift back on the item it went down on is no click.
        {
                struct window_event touch_press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0};
                struct window_event touch_lift = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0};
                struct window_event touch_move = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
                int dragged_back, tapped, trow, tpad, tx, ty1, ty2;
                static struct ui_popmenu t;

                memory_zero(&t, sizeof(t));
                t.touch = true;
                ui_popmenu_add(&t, "Copy", null, 0, 1);
                ui_popmenu_add(&t, "Paste", null, 0, 2);
                ui_popmenu_add(&t, "Select all", null, 0, 3);
                ui_popmenu_size(wui, &t, 0);
                trow = popmenu_row(&t), tpad = 0, tx = t.width / 2;
                ty1 = tpad + trow + trow / 2, ty2 = tpad + 2 * trow + trow / 2;
                ui_popmenu_event(wui, &t, &touch_press, tx, ty1);
                ui_popmenu_event(wui, &t, &touch_move, tx, ty2);
                ui_popmenu_event(wui, &t, &touch_move, tx, ty1);
                dragged_back = ui_popmenu_event(wui, &t, &touch_lift, tx, ty1);
                check("menu: a finger dragged past the slop and back to its item chooses nothing (a drag is no click)", dragged_back == UI_MENU_NOTHING);
                ui_popmenu_event(wui, &t, &touch_press, tx, ty1);
                tapped = ui_popmenu_event(wui, &t, &touch_lift, tx, ty1);
                check("menu: a finger's press and lift on an item, unmoved, chooses it", tapped == 2);
        }
        // Drawn opaque (the menu's own face is what that would show), it has no face: the glass is what is under it.
        menu_defaults_items(wui, &m);
        {
                struct ui opaque = *wui;

                memory_zero((void address_to)pixels, 600 * 900 * 4);
                opaque.glass = 0;
                ui_begin(&opaque, picture, 600, m.width, m.height);
                m.hot = -1;
                ui_popmenu_draw(&opaque, &m);
                check("menu: drawn opaque, a menu has no face (a menu is glass)", picture[2 * 600 + m.height / 2] == 0);
        }
        // Every popup is glass: its row takes the glass flags (no close button), and so does its look, whatever the app.
        {
                static struct windows *glass_table_scratch;
                struct windows *const glass_table_at = windows_scratch_table(&glass_table_scratch);
                struct ui_popup popup;

                memory_zero(&popup, sizeof(popup));
                popup.own.table = glass_table_at, popup.own.row = 1;
                ui_popmenu_look(&popup, glass_table_at, wui);
                check("menu: a popup is glass, its row has the glass flags and no close button", popup.ui.glass && !popup.ui.opaque &&
                      glass_table_at->glass[1] == UI_POPUP_GLASS);
        }
        /*
                A click on an item: the item's rect is the one element it has, the whole width of the menu and square, lit in the
                selection's layer, with its press ripple drawn inside it. Nothing is lit beside it, above or below.
        */
        {
                struct ui glass = *wui;
                struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
                int row, pad = 0, top, mid; // a menu has no room above its first item or below its last
                unsigned int address_to picture2 = picture;

                glass.glass = 1, glass.opaque = false;
                ui_inks(&glass, UI_GLASS_GROUND);
                menu_defaults_items(wui, &m);
                row = popmenu_row(&m);
                top = pad + row, mid = top + row / 2;
                ui_popmenu_event(wui, &m, &press, m.width / 2, mid);
                ui_kit.now = 1000000000ll;
                memory_zero((void address_to)pixels, 600 * 900 * 4);
                ui_begin(&glass, picture2, 600, m.width, m.height);
                ui_popmenu_draw(&glass, &m);
                ui_kit.now += 100000000ll;
                memory_zero((void address_to)pixels, 600 * 900 * 4);
                ui_begin(&glass, picture2, 600, m.width, m.height);
                ui_popmenu_draw(&glass, &m);
#define CLICK_LIT(x, y) ((picture2[(y) * 600 + (x)] >> 24) > 0)
                check("menu item: a click's rect is the whole width of the menu and square (its corners are lit, its ends too)",
                      CLICK_LIT(0, top + 1) && CLICK_LIT(m.width - 1, top + 1) && CLICK_LIT(0, top + row - 2) &&
                          CLICK_LIT(m.width - 1, top + row - 2) && CLICK_LIT(0, mid) && CLICK_LIT(m.width - 1, mid));
                check("menu item: nothing else is lit by a click: the rows above and below the item draw no rect",
                      !CLICK_LIT(m.width / 2, top - 2) && !CLICK_LIT(m.width / 2, top + row + 1) && !CLICK_LIT(0, top - 2));
#ifdef UI_MENU_RIPPLE
                check("menu item: the press's ripple is live in the item, and the menu is moving until it ends",
                      m.ripple.live && m.ripple_at == 1 && m.moving);
#else
                check("menu item: the press's ripple is live in the item, and the menu is moving until it ends", false);
#endif
#undef CLICK_LIT
        }

#ifdef UI_MENU_DEFAULTS
        {
                // The rows as the menu's tree laid them out: the mouse's, and the same menu sized for a finger.
                int mouse = popmenu_row(&m), finger;

                m.touch = true;
                ui_popmenu_size(wui, &m, 0);
                finger = popmenu_row(&m);
                m.touch = false;
                ui_popmenu_size(wui, &m, 0);
                check("menu: a finger's row is the kit's touch row, and the mouse's the kit's row",
                      mouse == UI_MENU_ROW * wui->scale / 100 && finger == UI_MENU_TOUCH_ROW * wui->scale / 100);
                check("menu: a menu is no narrower than the kit's least width", m.width >= UI_MENU_LEAST * wui->scale / 100 && row == mouse);
        }
        // A menu of twenty items shown five high has the kit's bar down its right, and nothing else there.
        memory_zero(&m, sizeof(m));
        m.press.down = false;
        for (int at = 0; at < 20; at++)
                ui_popmenu_add(&m, "Item", null, 0, at + 1);
        ui_popmenu_size(wui, &m, 5 * row + 2 * pad);
        {
                struct ui glass = *wui;
                int bar = 0;

                glass.glass = 1;
                memory_zero((void address_to)pixels, 600 * 900 * 4);
                ui_begin(&glass, picture, 600, m.width, m.height);
                ui_popmenu_draw(&glass, &m);
                // The bar's ink eases in from clear, so it is drawn again once it has settled.
                ui_kit.now += 2000000000ll;
                memory_zero((void address_to)pixels, 600 * 900 * 4);
                ui_popmenu_draw(&glass, &m);
                for (int yy = 0; yy < m.height; yy++)
                        for (int xx = m.width - 8 * wui->scale / 100; xx < m.width; xx++)
                                bar += picture[yy * 600 + xx] != 0;
                check("menu: a menu whose items do not all show has the kit's bar", bar > 0 && m.rows_shown < 20);
        }
#else
        check("menu: every menu's size comes from one place (ui_menu_sizes)", false);
        check("menu: a menu whose items do not all show has the kit's bar", false);
#endif
        compositor_call(syscall(munmap), (positive)pixels, 600 * 900 * 4, 0, 0, 0, 0);
}

static fn menu_check(struct ui address_to wui)
{
        static struct ui_popmenu m;
        long pixels = ui_map(600 * 900 * 4);
        b32 chosen_ok = true, hot_ok = true;

        m.count = 0;
        ui_popmenu_add(&m, "Section", null, UI_MENU_HEADER, 0);
        ui_popmenu_add(&m, "&Undo", "Ctrl+Z", 0, 1);
        ui_popmenu_add(&m, "&Redo", "Ctrl+Y", UI_MENU_DISABLED, 2);
        ui_popmenu_add(&m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(&m, "Cu&t", "Ctrl+X", 0, 3);
        ui_popmenu_add(&m, "&Copy", "Ctrl+C", 0, 4);
        ui_popmenu_add(&m, "&Paste", "Ctrl+V", 0, 5);
        ui_popmenu_add(&m, "Word wrap", null, UI_MENU_CHECK | UI_MENU_ON, 6);
        ui_popmenu_add(&m, "More", null, UI_MENU_SUBMENU, 7);
        ui_popmenu_size(wui, &m, 900);
        ui_begin(wui, (unsigned int address_to)pixels, 600, 600, 900);
        ui_popmenu_draw(wui, &m);
        check("menu: a menu has a size", m.width > 0 && m.height > 0 && m.width < 600 && m.height < 900);
        {
                struct window_event escape = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 1, 27}, mnemonic = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 46, 'c'};

                check("menu: Escape closes it", ui_popmenu_event(wui, &m, &escape, 0, 0) == UI_MENU_GONE);
                check("menu: a mnemonic chooses its item", ui_popmenu_event(wui, &m, &mnemonic, 0, 0) == 4);
        }
        for (positive round = 0; round < 20000; round++)
        {
                static const unsigned int keys[] = {103, 108, 102, 107, 28, 106, 30, 22, 19};
                struct window_event event = {0, 0, 0, 0};
                int answer, x = (int)menu_next((p32)m.width + 40) - 20, y = (int)menu_next((p32)m.height + 40) - 20;

                switch (menu_next(4))
                {
                case 0:
                        event = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, (int)keys[menu_next(9)], (int)("\0\0\0\0\r\0auy"[menu_next(9)])};
                        break;
                case 1:
                        event = (struct window_event){WINDOW_EVENT_MOTION, 0, x, y};
                        break;
                case 2:
                        event = (struct window_event){WINDOW_EVENT_BUTTON, menu_next(2) ? WINDOW_KEY_DOWN : 0, 0x110, 0};
                        break;
                default:
                        event = (struct window_event){WINDOW_EVENT_WHEEL, 0, menu_next(2) ? 120 : -120, 0};
                }
                answer = ui_popmenu_event(wui, &m, &event, x, y);
                if (answer >= 0)
                {
                        b32 found = false;

                        for (int at = 0; at < m.count; at++)
                                found |= m.items[at].id == answer && ui_popmenu_choosable(&m, at) && !(m.items[at].flags & UI_MENU_SUBMENU);
                        chosen_ok &= found;
                }
                if (event.type == WINDOW_EVENT_KEY && (event.a == 103 || event.a == 108 || event.a == 102 || event.a == 107))
                        hot_ok &= ui_popmenu_choosable(&m, m.hot);
        }
        check("menu: only an item that can be chosen is ever chosen", chosen_ok);
        check("menu: the keys keep the hot item on one that can be chosen", hot_ok);
        compositor_call(syscall(munmap), (positive)pixels, 600 * 900 * 4, 0, 0, 0, 0);
}

/*
        The kit's composites where a key or a hand meets them: Up with nothing chosen takes a menu's last item and Down
        its first; a menu with nothing that can be chosen, or no items, keeps its view at the top; the padding under a
        menu's last row drawn is no item (no row is drawn there, so none is chosen); Down with no row chosen takes a
        list's first row; and a menu opened again has no thumb held over from its last opening.
*/
static fn kit_composites_check(struct ui address_to wui)
{
        static struct ui_popmenu up_m, pad_m, empty_m, none_m;
        static struct ui_list keyed;
        static struct ui_popmenu reopened;
        int s = wui->scale / 100, bottom;
        b32 tops_ok = true;
        struct window_event up = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 103, 0}, down = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 108, 0};

        up_m.count = 0;
        ui_popmenu_add(&up_m, "Alpha", null, 0, 1);
        ui_popmenu_add(&up_m, "Beta", null, 0, 2);
        ui_popmenu_add(&up_m, "Gamma", null, 0, 3);
        ui_popmenu_size(wui, &up_m, 900 * s);
        up_m.hot = -1, ui_popmenu_to(&up_m, 0);
        ui_popmenu_event(wui, &up_m, &up, 0, 0);
        check("menu: Up with nothing chosen takes the last item", up_m.hot == 2);

        empty_m.count = 0;
        ui_popmenu_add(&empty_m, "Heading", null, UI_MENU_HEADER, 0);
        ui_popmenu_add(&empty_m, "&Off", null, UI_MENU_DISABLED, 1);
        ui_popmenu_size(wui, &empty_m, 900 * s);
        none_m.count = 0;
        ui_popmenu_size(wui, &none_m, 900 * s);
        for (int round = 0; round < 2; round++)
        {
                struct ui_popmenu address_to m = round ? &none_m : &empty_m;

                m->hot = -1, ui_popmenu_to(m, 0);
                ui_popmenu_event(wui, m, &down, 0, 0);
                tops_ok &= ui_popmenu_top(m) >= 0 && m->hot == -1;
        }
        check("menu: keys in a menu with nothing to choose keep its view at the top", tops_ok);

        pad_m.count = 0;
        for (int at = 0; at < 8; at++)
                ui_popmenu_add(&pad_m, "Item", null, 0, at + 1);
        ui_popmenu_size(wui, &pad_m, 96 * s);
        bottom = pad_m.height + 2 * s;
        {
                int row = popmenu_row(&pad_m), middle = 2 * row + row / 2;
                struct window_event motion = {WINDOW_EVENT_MOTION, 0, 10 * s, middle};

                pad_m.hot = -1;
                ui_popmenu_event(wui, &pad_m, &motion, 10 * s, middle);
                check("menu: the row drawn under the pointer is its item", pad_m.hot == 2 && ui_popmenu_at(&pad_m, 10 * s, middle) == 2);
                motion.b = bottom;
                ui_popmenu_event(wui, &pad_m, &motion, 10 * s, bottom);
                check("menu: below the menu's last row is no item (it has no padding to be one)", pad_m.hot == -1 && ui_popmenu_at(&pad_m, 10 * s, bottom) == -1);
        }

        keyed.count = 3;
        keyed.id[0] = 10, keyed.id[1] = 11, keyed.id[2] = 12;
        keyed.top[0] = 0, keyed.top[1] = 40 * s, keyed.top[2] = 80 * s;
        keyed.content = keyed.scroll.content = 120 * s;
        keyed.scroll.view = keyed.h = 400 * s;
        keyed.focus = -1;
        {
                b32 moved = false;

                ui_list_key(wui, &keyed, 108, &moved);
                check("list: Down with no row chosen chooses the first row", keyed.focus == 10 && moved);
        }

        // A menu closed with its thumb held (the release never reached it), and opened again: nothing is held.
        memory_zero(&reopened, sizeof(reopened));
        for (int at = 0; at < 32; at++)
                ui_popmenu_add(&reopened, "Choice", null, 0, at + 1);
        ui_popmenu_size(wui, &reopened, 600 * s);
        reopened.scroll.held = 1;
        ui_popmenu_size(wui, &reopened, 600 * s);
        {
                int x = reopened.scroll.x + s, y = reopened.scroll.y + reopened.scroll.height / 2;

                check("menu: a menu opened again has no thumb held over from its last opening",
                      pointer_as(wui, &reopened.scroll, WINDOW_EVENT_MOTION, x, y, false, 0) == -1);
        }
}


/*
        The editor's files: a FIFO and a device are refused, a link is followed to its file and a save writes that file
        (the link stays a link), the mode and the byte order mark are kept, a save is a rename (the old bytes are whole
        until the new are), and a hundred megabytes open and paint their first screen: the time is printed.
*/
static fn editor_check(struct ui address_to wui)
{
        // A directory of this process's own: the three machines' checks run at once in one directory, and a file
        // another one truncated under this one's mapping was a SIGBUS (arm64 under qemu-user), its save a refusal.
        char dir[48], path[96], link_path[96], fifo[96], big[96];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-check."));
        bipolar handle;
        error_stat facts;
        positive started;

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        string_copy(string_copy((string_address)path, (string_address)dir) + string_length((string_address)dir), (string_address) "/file.c");
        string_copy(string_copy((string_address)link_path, (string_address)dir) + string_length((string_address)dir), (string_address) "/link.c");
        string_copy(string_copy((string_address)fifo, (string_address)dir) + string_length((string_address)dir), (string_address) "/fifo");
        string_copy(string_copy((string_address)big, (string_address)dir) + string_length((string_address)dir), (string_address) "/big.c");
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "\xef\xbb\xbfint x;\r\n", 11);
        system_close(handle);
        system_call_3(syscall(symlinkat), (positive) "file.c", (positive)AT_FDCWD, (positive)link_path);
        system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)fifo, 0010600, 0);
        editor.ui = *wui;
        editor.count = 0;
        check("editor: a FIFO is refused", !editor_open((string_address)fifo));
        check("editor: a device is refused", !editor_open((string_address) "/dev/null"));
        check("editor: a link opens its file", editor_open((string_address)link_path) && editor.count == 1);
        check("editor: the byte order mark is held aside and the line ends are CR LF",
              editor.tabs[0].view.doc.bom && editor.tabs[0].view.doc.eol == DOC_EOL_CRLF && doc_length(&editor.tabs[0].view.doc) == 8);
        doc_select_set(&editor.tabs[0].view.doc, 0, 0);
        doc_type(&editor.tabs[0].view.doc, (const p8 address_to) "// ", 3);
        check("editor: a save succeeds", editor_save(&editor.tabs[0]));
        {
                char got[64];
                bipolar read_handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
                bipolar length = system_read_once((b32)read_handle, got, sizeof(got));
                char target[64];
                bipolar linked = system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)link_path, (positive)target, sizeof(target));

                fstat((b32)read_handle, address_of facts);
                system_close(read_handle);
                check("editor: the save wrote the link's file, with its mark and line ends", length == 14 && !memory_compare(got, "\xef\xbb\xbf// int x;\r\n", 14));
                check("editor: the link is still a link", linked == 6 && !memory_compare(target, "file.c", 6));
                check("editor: the file's mode is kept", (facts.st_mode & 07777) == 0640);
                check("editor: saved, the document is clean", !doc_dirty(&editor.tabs[0].view.doc));
        }
        editor_close_tab();
        // A hundred megabytes: open and the first screen drawn.
        {
                static char line[] = "        if (value > limit) return compute(value, limit) + offset; // note\n";
                static p8 chunk[1 << 20];
                long pixels = ui_map(1600 * 1000 * 4);

                for (positive at = 0; at < sizeof(chunk); at++)
                        chunk[at] = (p8)line[at % (sizeof(line) - 1)];
                handle = system_open_at_mode(AT_FDCWD, (string_address)big, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                for (int k = 0; k < 100; k++)
                        system_write_all((b32)handle, chunk, sizeof(chunk));
                system_close(handle);
                started = clock_monotonic_nanoseconds();
                check("editor: a hundred megabytes open", editor_open((string_address)big));
                ui_begin(&editor.ui, (unsigned int address_to)pixels, 1600, 1600, 1000);
                ui_text_draw(&editor.ui, &editor_now()->view, 0, 0, 1600, 1000, 0x111113);
                string_format(log, "  editor: 100 MB opened and its first screen drawn in %p ms\n", (clock_monotonic_nanoseconds() - started) / 1000000);
                check("editor: a hundred megabytes are mapped, not read", editor_now()->mapped == 100u << 20);
                // A key in the middle of it: the time a keystroke and its line's layout take.
                started = clock_monotonic_nanoseconds();
                doc_select_set(&editor_now()->view.doc, 50u << 20, 50u << 20);
                for (int k = 0; k < 1000; k++)
                {
                        doc_type(&editor_now()->view.doc, (const p8 address_to) "x", 1);
                        editor_now()->view.top_line = doc_line_of(&editor_now()->view.doc, 50u << 20);
                        ui_text_draw(&editor.ui, &editor_now()->view, 0, 0, 1600, 1000, 0x111113);
                }
                string_format(log, "  editor: a key typed and the view drawn again in %p us\n", (clock_monotonic_nanoseconds() - started) / 1000 / 1000);
                editor_close_tab();
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)big, 0);
                compositor_call(syscall(munmap), (positive)pixels, 1600 * 1000 * 4, 0, 0, 0, 0);
        }
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)link_path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)fifo, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

/*
        The editor's close. Ctrl+W on a file with changes keeps it open and says why; the second Ctrl+W closes it and the
        changes go. A window asked to go with changes in it is held and says why, and goes once nothing in it is unsaved.
        A link to a file that is not there yet is saved at the file it names, and the link stays a link.
*/
static fn editor_close_check(struct ui address_to wui)
{
        char dir[48], path[96], link_path[96], later[96], got[64], target[64];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-close."));
        bipolar handle, length, linked;

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        string_copy(string_copy((string_address)path, (string_address)dir) + string_length((string_address)dir), (string_address) "/file.c");
        string_copy(string_copy((string_address)later, (string_address)dir) + string_length((string_address)dir), (string_address) "/later.c");
        string_copy(string_copy((string_address)link_path, (string_address)dir) + string_length((string_address)dir), (string_address) "/link.c");
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "int x;\n", 7);
        system_close(handle);
        editor.ui = *wui;
        editor.count = 0;
        check("editor close: a file opens", editor_open((string_address)path) && editor.count == 1);
        doc_select_set(&editor_now()->view.doc, 0, 0);
        doc_type(&editor_now()->view.doc, (const p8 address_to) "// ", 3);
        editor_command(DOC_CMD_CLOSE);
        check("editor close: Ctrl+W on a file with changes keeps it open and says why", editor.count == 1 && doc_dirty(&editor_now()->view.doc) && editor.status[0]);
        editor_command(DOC_CMD_CLOSE);
        check("editor close: the second Ctrl+W closes it, the changes given up", editor.count == 1 && editor_now()->path[0] == 0 && !doc_dirty(&editor_now()->view.doc));

        editor.count = 0;
        check("editor close: a file opens again", editor_open((string_address)path) && editor.count == 1);
        doc_select_set(&editor_now()->view.doc, 0, 0);
        doc_type(&editor_now()->view.doc, (const p8 address_to) "// ", 3);
        check("editor close: a window asked to go with changes in it is held, and says why",
              editor_app.closing && editor_app.done && (editor_app.closing(), !editor_app.done()) && editor.status[0]);
        check("editor close: and goes once nothing in it is unsaved", editor_save(editor_now()) && editor_app.closing && (editor_app.closing(), editor_app.done()));

        // A link to a file that is not there yet: saved at the file it names, the link still a link.
        system_call_3(syscall(symlinkat), (positive) "later.c", (positive)AT_FDCWD, (positive)link_path);
        editor.count = 0;
        check("editor close: a link to nothing yet opens", editor_open((string_address)link_path) && editor.count == 1);
        doc_type(&editor_now()->view.doc, (const p8 address_to) "int y;\n", 7);
        check("editor close: its save succeeds", editor_save(editor_now()));
        linked = system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)link_path, (positive)target, sizeof(target));
        check("editor close: the link is still a link, to the file it names", linked == (bipolar)string_length((string_address) "later.c") &&
              !memory_compare(target, "later.c", (positive)linked));
        handle = system_open_at(AT_FDCWD, (string_address)later, FILE_READ | O_CLOEXEC);
        length = handle >= 0 ? system_read_once((b32)handle, got, sizeof(got)) : -1;
        if (handle >= 0)
                system_close(handle);
        check("editor close: and the file it names has the save", length == 7 && !memory_compare(got, "int y;\n", 7));

        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)link_path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)later, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

// The stack the editor's next call lands on, written over with bytes that are not a NUL.
static __attribute__((noinline)) fn editor_check_scribble(void)
{
        volatile p8 junk[16384];

        for (positive at = 0; at < sizeof(junk); at++)
                junk[at] = 'x';
}

/*
        The editor against files another user chose: a link to nothing yet, named by an absolute path, is saved at
        exactly that path (readlinkat ends nothing, and the name was read to the stack's next NUL); a file of mode 0
        is saved mode 0 (it was 0644); another user's set-user-id program saved stays theirs or loses the bit; a save
        writes the directory the file was opened in, not through a link put where that directory was, and not a name
        cut short at 511 bytes.
*/
static fn editor_hostile_check(struct ui address_to wui)
{
        char dir[48], here[256], target[512], link_path[96];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-hostile."));
        bipolar got;

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        got = system_call_2(syscall(getcwd), (positive)here, sizeof(here));
        here[got > 0 ? got - 1 : 0] = 0;
        string_copy(string_copy(string_copy(string_copy((string_address)target, (string_address)here) + string_length((string_address)here),
                                            (string_address) "/") + 1, (string_address)dir) + string_length((string_address)dir),
                    (string_address) "/far.c");
        string_copy(string_copy((string_address)link_path, (string_address)dir) + string_length((string_address)dir), (string_address) "/far-link.c");
        system_call_3(syscall(symlinkat), (positive)target, (positive)AT_FDCWD, (positive)link_path);
        editor.ui = *wui;
        editor.count = 0;
        editor_check_scribble();
        check("editor hostile: a link to nothing yet, by an absolute name, opens a new file of exactly that name",
              target[0] == '/' && editor_open((string_address)link_path) && editor.count == 1 &&
                  !memory_compare(editor_now()->path, target, string_length((string_address)target) + 1));

        // A file of mode 0 (a shadow file), as root reads one: saved mode 0, not 0644 for everyone to read.
        {
                char secret[96];
                bipolar handle;
                error_stat facts;

                string_copy(string_copy((string_address)secret, (string_address)dir) + string_length((string_address)dir), (string_address) "/secret");
                handle = system_open_at_mode(AT_FDCWD, (string_address)secret, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                system_write_all((b32)handle, "k\n", 2);
                system_close(handle);
                editor.count = 0;
                check("editor hostile: a file opens", editor_open((string_address)secret) && editor.count == 1);
                editor_now()->mode = 0; // what editor_load reads of a file of mode 0, which only root can open
                doc_type(&editor_now()->view.doc, (const p8 address_to) "x", 1);
                check("editor hostile: its save succeeds", editor_save(editor_now()));
                handle = system_open_at(AT_FDCWD, (string_address)secret, O_PATH | O_CLOEXEC);
                facts.st_mode = 0777;
                if (handle >= 0)
                        fstat((b32)handle, address_of facts), system_close(handle);
                check("editor hostile: a file of mode 0 is saved mode 0, not readable by everyone", handle >= 0 && (facts.st_mode & 07777) == 0);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)secret, 0);
        }

        // Another user's set-user-id program, saved: it stays theirs, or it is no set-user-id program of the saver's.
        {
                char program[96];
                bipolar handle;
                error_stat facts;
                b32 root = system_call(syscall(getuid)) == 0;
                p32 other;

                string_copy(string_copy((string_address)program, (string_address)dir) + string_length((string_address)dir), (string_address) "/program");
                handle = system_open_at_mode(AT_FDCWD, (string_address)program, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
                system_write_all((b32)handle, "#!/bin/sh\n", 10);
                system_call_2(syscall(fchmod), (positive)handle, 04755);
                system_close(handle);
                editor.count = 0;
                check("editor hostile: a set-user-id file opens", editor_open((string_address)program) && editor_now()->mode == 04755);
                // An owner the save cannot give the new file unless it runs as root: the file is another user's.
                other = editor_now()->owner + 1;
                editor_now()->owner = other;
                doc_type(&editor_now()->view.doc, (const p8 address_to) "x", 1);
                check("editor hostile: its save succeeds", editor_save(editor_now()));
                facts.st_mode = 0, facts.st_uid = 0;
                handle = system_open_at(AT_FDCWD, (string_address)program, O_PATH | O_CLOEXEC);
                if (handle >= 0)
                        fstat((b32)handle, address_of facts), system_close(handle);
                check("editor hostile: a set-user-id file whose owner the save cannot keep loses its set-id bits (as root it keeps both)",
                      root ? facts.st_uid == other && (facts.st_mode & 07777) == 04755
                           : facts.st_uid == (p32)system_call(syscall(getuid)) && (facts.st_mode & 07777) == 0755);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)program, 0);
        }

        // The file's directory moved away after the open, and a link put where it was (by the directory's owner, to /etc
        // say): the save writes the directory the file was opened in, and nothing through the link.
        {
                char sub[96], moved[96], elsewhere[96], note[128], stray[128], kept[128], got_bytes[8];
                bipolar handle, length;
                b32 saved;

                string_copy(string_copy((string_address)sub, (string_address)dir) + string_length((string_address)dir), (string_address) "/sub");
                string_copy(string_copy((string_address)moved, (string_address)dir) + string_length((string_address)dir), (string_address) "/moved");
                string_copy(string_copy((string_address)elsewhere, (string_address)dir) + string_length((string_address)dir), (string_address) "/elsewhere");
                string_copy(string_copy((string_address)note, (string_address)sub) + string_length((string_address)sub), (string_address) "/note.c");
                string_copy(string_copy((string_address)stray, (string_address)elsewhere) + string_length((string_address)elsewhere), (string_address) "/note.c");
                string_copy(string_copy((string_address)kept, (string_address)moved) + string_length((string_address)moved), (string_address) "/note.c");
                system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)sub, 0700);
                handle = system_open_at_mode(AT_FDCWD, (string_address)note, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                system_write_all((b32)handle, "a\n", 2);
                system_close(handle);
                editor.count = 0;
                check("editor hostile: a file in a directory opens", editor_open((string_address)note) && editor.count == 1);
                doc_select_set(&editor_now()->view.doc, 0, 0);
                doc_type(&editor_now()->view.doc, (const p8 address_to) "b", 1);
                system_call_5(syscall(renameat2), (positive)AT_FDCWD, (positive)sub, (positive)AT_FDCWD, (positive)moved, 0);
                system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)elsewhere, 0700);
                system_call_3(syscall(symlinkat), (positive) "elsewhere", (positive)AT_FDCWD, (positive)sub);
                saved = editor_save(editor_now());
                handle = system_open_at(AT_FDCWD, (string_address)stray, FILE_READ | O_CLOEXEC);
                if (handle >= 0)
                        system_close(handle);
                check("editor hostile: a save after the file's directory was swapped for a link writes nothing through the link", saved && handle < 0);
                handle = system_open_at(AT_FDCWD, (string_address)kept, FILE_READ | O_CLOEXEC);
                length = handle >= 0 ? system_read_once((b32)handle, got_bytes, sizeof(got_bytes)) : -1;
                if (handle >= 0)
                        system_close(handle);
                check("editor hostile: and writes the directory the file was opened in", length == 3 && !memory_compare(got_bytes, "ba\n", 3));
                editor.count = 0;
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)stray, 0);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)kept, 0);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)sub, 0);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)moved, 0x200);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)elsewhere, 0x200);
        }

        // A file whose real path is longer than the tab holds: /proc's name of it comes back cut, and a save under the
        // cut name wrote another file beside it.
        {
                static char deep[1024], cut[1024];
                positive depth = 0, at = string_length(string_copy((string_address)deep, (string_address)dir)), real = got > 0 ? (positive)got - 1 : 0;
                positive name_length, made[8];
                bipolar handle;
                b32 saved;

                real += 1 + at; // the absolute path's length so far: here, a slash, the directory
                while (real + 101 < 480 && depth < 8)
                {
                        deep[at] = '/';
                        for (positive k = 1; k <= 100; k++)
                                deep[at + k] = 'd';
                        at += 101, real += 101;
                        deep[at] = 0;
                        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)deep, 0700);
                        made[depth++] = at;
                }
                name_length = 600 - real;
                deep[at] = '/';
                for (positive k = 1; k <= name_length; k++)
                        deep[at + k] = 'n';
                deep[at + 1 + name_length] = 0;
                handle = system_open_at_mode(AT_FDCWD, (string_address)deep, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                system_write_all((b32)handle, "a\n", 2);
                system_close(handle);
                // The name /proc gives back, cut at 511 bytes: the same directory, the file's name cut short. Made a
                // second name of the same file, which is the one case where the name in the directory is still it.
                memory_copy(cut, deep, at + 1 + (511 - real - 1));
                cut[at + 1 + (511 - real - 1)] = 0;
                system_call_5(syscall(linkat), (positive)AT_FDCWD, (positive)deep, (positive)AT_FDCWD, (positive)cut, 0);
                editor.count = 0;
                check("editor hostile: a file with a real path of 600 bytes opens", real > 300 && real < 511 && editor_open((string_address)deep) && editor.count == 1);
                doc_type(&editor_now()->view.doc, (const p8 address_to) "b", 1);
                saved = editor_save(editor_now());
                {
                        char was[8];
                        bipolar length;

                        handle = system_open_at(AT_FDCWD, (string_address)cut, FILE_READ | O_CLOEXEC);
                        length = handle >= 0 ? system_read_once((b32)handle, was, sizeof(was)) : -1;
                        if (handle >= 0)
                                system_close(handle);
                        check("editor hostile: its save is refused, and the name cut short is not written",
                              !saved && length == 2 && !memory_compare(was, "a\n", 2));
                }
                editor.count = 0;
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)cut, 0);
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)deep, 0);
                while (depth)
                {
                        deep[made[--depth]] = 0;
                        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)deep, 0x200);
                }
        }

        // A FIFO named like a document, a writer waiting on it: looking at it opens nothing, so the writer is not let go.
        // Opening a device to look at it acts on it as this open let the writer go: a watchdog is armed by its open.
        {
                char pipe_path[96];
                bipolar child, status_word = 0, reaped = 0;
                b32 opened;
                b64 pause[2] = {0, 20000000};

                string_copy(string_copy((string_address)pipe_path, (string_address)dir) + string_length((string_address)dir), (string_address) "/notes.txt");
                system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)pipe_path, 0010600, 0);
                child = system_fork();
                if (child == 0)
                {
                        // Waits here until something opens the FIFO to read it.
                        bipolar writer = system_open_at(AT_FDCWD, (string_address)pipe_path, O_WRONLY | O_CLOEXEC);

                        system_call_1(syscall(exit_group), writer >= 0 ? 7 : 1);
                }
                for (int k = 0; k < 5; k++)
                        system_call_2(syscall(nanosleep), (positive)pause, 0);
                editor.count = 0;
                opened = editor_open((string_address)pipe_path);
                for (int k = 0; k < 15 && !reaped; k++)
                {
                        system_call_2(syscall(nanosleep), (positive)pause, 0);
                        reaped = system_call_4(syscall(wait4), (positive)child, (positive)&status_word, 1, 0); // WNOHANG
                }
                check("editor hostile: a FIFO with a writer waiting on it is refused, and the writer is not let go", child > 0 && !opened && reaped == 0);
                {
                        bipolar reader = system_open_at(AT_FDCWD, (string_address)pipe_path, FILE_READ | O_NONBLOCK | O_CLOEXEC);

                        if (!reaped && child > 0)
                                system_call_4(syscall(wait4), (positive)child, (positive)&status_word, 0, 0);
                        if (reader >= 0)
                                system_close(reader);
                }
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)pipe_path, 0);
        }

        // A mapped file cut short by another program while it is open (a log rotated by copytruncate): read on as zeros
        // past the cut and said to have changed on disk. The read was SIGBUS, and the editor (this check with it) gone.
        {
                char cut_path[96];
                static p8 chunk[1 << 20];
                bipolar handle;
                p8 head[4] = {0}, tail[4] = {1, 1, 1, 1};

                for (positive at = 0; at < sizeof(chunk); at++)
                        chunk[at] = at % 64 == 63 ? '\n' : 'a';
                string_copy(string_copy((string_address)cut_path, (string_address)dir) + string_length((string_address)dir), (string_address) "/cut.log");
                handle = system_open_at_mode(AT_FDCWD, (string_address)cut_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                for (int k = 0; k < 9; k++)
                        system_write_all((b32)handle, chunk, sizeof(chunk));
                system_close(handle);
                editor.count = 0;
                check("editor hostile: a file of 9 MiB opens, mapped", editor_open((string_address)cut_path) && editor_now()->mapped == 9u << 20);
                system_call_2(syscall(truncate), (positive)cut_path, 4096);
                doc_read(&editor_now()->view.doc, 0, head, 4);
                doc_read(&editor_now()->view.doc, (8u << 20) + 5, tail, 4);
                check("editor hostile: a mapped file cut short under it reads zeros past the cut, its start as it was, and says it changed",
                      !memory_compare(head, "aaaa", 4) && !tail[0] && !tail[3] && editor_now()->changed_on_disk);
                editor_close_tab();
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)cut_path, 0);
        }

        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)link_path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

/*
        The editor's tabs: a file read again (Ctrl+R) keeps its place among them and is the one shown, and the others keep
        theirs.
*/
static fn editor_tabs_check(struct ui address_to wui)
{
        char dir[48], one[96], two[96], three[96];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-tabs."));
        bipolar handle;
        p8 got[8];

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        string_copy(string_copy((string_address)one, (string_address)dir) + string_length((string_address)dir), (string_address) "/one.c");
        string_copy(string_copy((string_address)two, (string_address)dir) + string_length((string_address)dir), (string_address) "/two.c");
        string_copy(string_copy((string_address)three, (string_address)dir) + string_length((string_address)dir), (string_address) "/three.c");
        handle = system_open_at_mode(AT_FDCWD, (string_address)one, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "one\n", 4);
        system_close(handle);
        handle = system_open_at_mode(AT_FDCWD, (string_address)two, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "two\n", 4);
        system_close(handle);
        handle = system_open_at_mode(AT_FDCWD, (string_address)three, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "three\n", 6);
        system_close(handle);
        editor.ui = *wui;
        editor.count = 0;
        check("editor tabs: three files open in order", editor_open((string_address)one) && editor_open((string_address)two) &&
              editor_open((string_address)three) && editor.count == 3);
        editor.tab = 1;
        editor_reload();
        check("editor tabs: a file read again keeps its place among the tabs, and is the one shown",
              editor.count == 3 && editor.tab == 1 && doc_length(&editor.tabs[1].view.doc) == 4 &&
                  doc_read(&editor.tabs[1].view.doc, 0, got, 4) == 4 && !memory_compare(got, "two\n", 4));
        check("editor tabs: and the others keep theirs", doc_length(&editor.tabs[0].view.doc) == 4 &&
              doc_length(&editor.tabs[2].view.doc) == 6);
        // A close is not a key, so nothing else gives the tab it shows the caret: it has it when the editor has the keys.
        editor.focused = true;
        editor.tab = 2;
        editor_close_tab();
        check("editor tabs: the tab a close shows has the caret", editor.count == 2 && editor.tab == 1 && editor_now()->view.focused);
        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)one, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)two, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)three, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

/*
        A file another program changed since it was read: Ctrl+S says so and writes nothing, so that change is not written
        over; the second Ctrl+S, with no key between, writes it.
*/
static fn editor_disk_check(struct ui address_to wui)
{
        char dir[48], path[96];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-disk."));
        bipolar handle, length;
        p8 got[32];

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        string_copy(string_copy((string_address)path, (string_address)dir) + string_length((string_address)dir), (string_address) "/disk.c");
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "one\n", 4);
        system_close(handle);
        editor.ui = *wui;
        editor.count = 0;
        check("editor disk: a file opens", editor_open((string_address)path) && editor.count == 1);
        handle = system_open_at(AT_FDCWD, (string_address)path, O_WRONLY | O_TRUNC | O_CLOEXEC);
        system_write_all((b32)handle, "theirs\n", 7);
        system_close(handle);
        editor_focus(true);
        check("editor disk: a change made on disk is seen when the window has the keys", editor_now()->changed_on_disk);
        editor_command(DOC_CMD_SAVE);
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        length = handle >= 0 ? system_read_once((b32)handle, got, sizeof(got)) : -1;
        if (handle >= 0)
                system_close(handle);
        check("editor disk: the first Ctrl+S writes nothing over the change", length == 7 && !memory_compare(got, "theirs\n", 7));
        editor_command(DOC_CMD_SAVE);
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        length = handle >= 0 ? system_read_once((b32)handle, got, sizeof(got)) : -1;
        if (handle >= 0)
                system_close(handle);
        check("editor disk: the second writes the editor's text", length == 4 && !memory_compare(got, "one\n", 4));
        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

/*
        windows_screen_numbers (window.c) on a table of three screens listed out of order: the primary on the right, a
        screen on the left below the top one, and the top left one. The numbers are reading order, the top edges first and
        then the left edges, so the top left screen is 1, the primary on the right is 2 and the one below the top left
        is 3. The 2 by 2 arrangement is listed bottom right first: its numbers are top left 1, top right 2, bottom left 3
        and bottom right 4, which is the order a person reads it in. A table that lists no screen, or more than the table
        can hold, is one screen numbered 1.
*/
static fn check_screen_numbers(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        unsigned int number[WINDOWS_OUTPUTS], count;

        windows_scratch_zero(table_at);
        table_at->outputs = 3;
        table_at->output_x[0] = 1920, table_at->output_y[0] = 0;
        table_at->output_x[1] = 0, table_at->output_y[1] = 540;
        table_at->output_x[2] = 0, table_at->output_y[2] = 0;
        count = windows_screen_numbers(table_at, number);
        check("identify: three screens are counted as three", count == 3);
        check("identify: the screens are numbered in reading order, top edge then left edge (the primary on the right is 2)",
              number[0] == 2 && number[1] == 3 && number[2] == 1);

        // A 2 by 2 arrangement, listed bottom right, bottom left, top right, top left.
        windows_scratch_zero(table_at);
        table_at->outputs = 4;
        table_at->output_x[0] = 1920, table_at->output_y[0] = 1080;
        table_at->output_x[1] = 0, table_at->output_y[1] = 1080;
        table_at->output_x[2] = 1920, table_at->output_y[2] = 0;
        table_at->output_x[3] = 0, table_at->output_y[3] = 0;
        count = windows_screen_numbers(table_at, number);
        check("identify: a 2 by 2 arrangement is counted as four", count == 4);
        check("identify: a 2 by 2 arrangement is numbered top left 1, top right 2, bottom left 3, bottom right 4",
              number[3] == 1 && number[2] == 2 && number[1] == 3 && number[0] == 4);
        table_at->outputs = 0;
        count = windows_screen_numbers(table_at, number);
        check("identify: a table that lists no screen is one screen, numbered 1", count == 1 && number[0] == 1);
        table_at->outputs = WINDOWS_OUTPUTS + 1;
        count = windows_screen_numbers(table_at, number);
        check("identify: a list past what the table holds is one screen, numbered 1", count == 1 && number[0] == 1);
}

/*
        The editor at the edges of its window and of its files. A window one column wide, or none tall, with the find bar
        and the tab strip shown, draws nothing outside its box. A file that is no longer a regular file when Ctrl+R reads
        it leaves a tab shown, never an index past the last tab. A file replaced by a FIFO after it was read is seen when
        the window gets the keys, and does not hang the window while it waits for a writer.
*/
static fn editor_edge_check(struct ui address_to wui)
{
        static const int sizes[][2] = {{1, 300}, {0, 300}, {300, 1}, {300, 0}, {2, 40}, {1, 1}, {7, 27}, {0, 0}};
        char dir[48], one[96], two[96];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-edge."));
        unsigned int address_to picture;
        long pixels;
        b32 clean = true;
        bipolar handle;

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        string_copy(string_copy((string_address)one, (string_address)dir) + string_length((string_address)dir), (string_address) "/one.c");
        string_copy(string_copy((string_address)two, (string_address)dir) + string_length((string_address)dir), (string_address) "/two.c");
        handle = system_open_at_mode(AT_FDCWD, (string_address)one, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "one\n", 4);
        system_close(handle);
        handle = system_open_at_mode(AT_FDCWD, (string_address)two, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "two\n", 4);
        system_close(handle);

        // The box: the window's pixels are its own stride, and nothing past the window's width or height is drawn.
        pixels = ui_map(800 * 600 * 4);
        picture = (unsigned int address_to)pixels;
        editor.ui = *wui;
        editor.count = 0;
        editor.own.stride = 800;
        check("editor edge: two files open for the tabs", editor_open((string_address)one) && editor_open((string_address)two) && editor.count == 2);
        editor_command(DOC_CMD_FIND);
        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
        {
                for (positive i = 0; i < 800 * 600; i++)
                        picture[i] = 0x5a5a5a5au;
                editor.width = sizes[at][0];
                editor.height = sizes[at][1];
                editor_draw(picture);
                for (int y = 0; y < 600; y++)
                        for (int x = 0; x < 800; x++)
                                if ((x >= editor.width || y >= editor.height) && picture[(positive)y * 800 + (positive)x] != 0x5a5a5a5au)
                                        clean = false;
        }
        check("editor edge: at any window size, with the find bar and the tabs shown, nothing is drawn outside the window", clean);
        editor.width = 800 * 3 / 5, editor.height = 600 * 3 / 4;

        // Ctrl+R finds the last tab's file gone and a directory in its place: the editor still shows a tab it can point at.
        editor.tab = 1;
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)two, 0);
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)two, 0700);
        editor_focus(true);
        check("editor edge: the file changed to a directory is seen when the window gets the keys", editor_now()->changed_on_disk);
        editor_reload();
        check("editor edge: a reload that finds no regular file leaves a tab shown, the one it can point at",
              editor.count >= 1 && editor.tab >= 0 && editor.tab < editor.count);
        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)one, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)two, 0x200);

        // A file replaced by a FIFO is not read: the window's focus returns, with no writer on the FIFO.
        handle = system_open_at_mode(AT_FDCWD, (string_address)one, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
        system_write_all((b32)handle, "one\n", 4);
        system_close(handle);
        editor.count = 0;
        check("editor edge: a file opens for the FIFO test", editor_open((string_address)one) && editor.count == 1);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)one, 0);
        system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)one, 0010600, 0);
        {
                bipolar child = system_fork();
                bipolar status = 0, reaped = 0;
                positive duration[2] = {0, 1000000};

                if (child == 0)
                {
                        editor_focus(true);
                        system_call_1(syscall(exit_group), 0);
                }
                for (int tries = 0; tries < 2000 && reaped != child; tries++)
                {
                        reaped = (bipolar)system_call_4(syscall(wait4), child, (positive)address_of status, 1, 0);
                        if (reaped != child)
                                system_call_2(syscall(nanosleep), (positive)address_of duration, 0);
                }
                if (reaped != child)
                {
                        system_call_2(syscall(kill), child, 9);
                        system_wait4_retry(child, address_of status, 0, null);
                }
                check("editor edge: a file replaced by a FIFO does not hang the window when it gets the keys", reaped == child && (status & 0x7f) == 0);
        }
        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)one, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

/*
        A read-only file (no write bit) is written over only by the second Ctrl+S, as a file changed on disk is: the first
        says so and writes nothing, and the mode stays read-only once it is saved.
*/
static fn editor_readonly_check(struct ui address_to wui)
{
        char dir[48], path[96];
        positive named = string_length(string_copy((string_address)dir, (string_address) "editor-ro."));
        bipolar handle, length;
        p8 got[32];

        named += positive_into_string((string_address)dir + named, (positive)system_call(syscall(getpid)));
        string_copy((string_address)dir + named, (string_address) ".tmp");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700);
        string_copy(string_copy((string_address)path, (string_address)dir) + string_length((string_address)dir), (string_address) "/ro.c");
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0444);
        system_write_all((b32)handle, "one\n", 4);
        system_close(handle);
        editor.ui = *wui;
        editor.count = 0;
        check("editor read-only: a read-only file opens", editor_open((string_address)path) && editor.count == 1);
        doc_select_set(&editor_now()->view.doc, 0, 0);
        doc_type(&editor_now()->view.doc, (const p8 address_to) "// ", 3);
        editor_command(DOC_CMD_SAVE);
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        length = handle >= 0 ? system_read_once((b32)handle, got, sizeof(got)) : -1;
        if (handle >= 0)
                system_close(handle);
        check("editor read-only: the first Ctrl+S writes nothing over the read-only file", length == 4 && !memory_compare(got, "one\n", 4));
        editor_command(DOC_CMD_SAVE);
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        length = handle >= 0 ? system_read_once((b32)handle, got, sizeof(got)) : -1;
        if (handle >= 0)
                system_close(handle);
        check("editor read-only: the second writes the editor's text, and the file stays read-only",
              length == 7 && !memory_compare(got, "// one\n", 7) && (editor_now()->mode & 0777) == 0444);
        editor.count = 0;
        editor_open(null);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200);
}

/*
        A window that asked for no tab (WINDOW_NO_TAB, the identify overlays') is not one of the windows the taskbar has a
        tab for, and a window that asked for none is, shown beside it.
*/
static fn check_no_tab(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
        struct windows address_to table;

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
                check("no tab: the table is mapped", false);
                return;
        }
        table = compositor.table;
        table->x[1] = table->y[1] = 10;
        table->width[1] = table->height[1] = 40;
        glass_paint(1, 0, 0, 40, 40, 0x00ff00u, true);
        table->x[2] = table->y[2] = 80;
        table->width[2] = table->height[2] = 40;
        table->glass[2] = WINDOW_GLASS | WINDOW_NO_TAB;
        glass_paint(2, 0, 0, 40, 40, 0, true);
        compositor_present();
        check("no tab: the window that asked for none is shown, and has no tab; the other has one",
              compositor.showing[1] && compositor.showing[2] && compositor_windows() == 1 && compositor.listed[0] == 1);

        /* Stacked bottom first: 1 (tab), 2 (no tab), 3 (tab). Alt-Tab from 3 passes 2 and gives the keys to 1. */
        table->x[3] = table->y[3] = 40;
        table->width[3] = table->height[3] = 40;
        glass_paint(3, 0, 0, 40, 40, 0x0000ffu, true);
        compositor_present();
        compositor_focus(3);
        compositor_focus_step();
        check("no tab: an Alt-Tab from the top passes the window that asked for none", compositor.focus == 1 && compositor.cycle == 1);
        compositor_focus_step();
        check("no tab: an Alt-Tab from 1 passes 2 again and gives the keys to 3", compositor.focus == 3);
        compositor_focus_commit();
        compositor_focus(2);
        check("no tab: a window that asked for none keeps the keys when it has them", compositor.focus == 2);
        compositor_focus_step();
        check("no tab: an Alt-Tab from the window that asked for none takes the keys to a tab", compositor.focus == 3);
        compositor_focus_commit();
        compositor_raise(2);
        compositor_focus(COMPOSITOR_NONE);
        compositor_refocus();
        check("no tab: the refocus does not give the keys to the window that asked for none, though it is on top",
              compositor.focus == 3);

        table->pid[1] = 1;
        table->pid[2] = 2;
        table->pid[3] = 3;
        compositor_leave(3);
        compositor_leave(2);
        compositor_leave(1);
        compositor_call(syscall(munmap), (positive)compositor.table, compositor.pixels + compositor.buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
        if (compositor.blur)
                compositor_call(syscall(munmap), (positive)compositor.blur, compositor.blur_bytes, 0, 0, 0, 0);
        compositor_zero();
}

/*
        The identify overlay's number for each screen (identify_number, which the window is given at its start) is the one
        windows_screen_numbers gives that screen, and the window is put on that screen's rectangle, windows_output's, by the
        same index. Three screens listed out of order, as check_screen_numbers has them.
*/
static fn check_identify_numbers(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        unsigned int number[WINDOWS_OUTPUTS], count;
        int x, y, width, height;
        b32 matched = true, placed = true;

        windows_scratch_zero(table_at);
        table_at->outputs = 3;
        table_at->output_x[0] = 1920, table_at->output_y[0] = 0, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
        table_at->output_x[1] = 0, table_at->output_y[1] = 540, table_at->output_w[1] = 1280, table_at->output_h[1] = 720;
        table_at->output_x[2] = 0, table_at->output_y[2] = 0, table_at->output_w[2] = 1920, table_at->output_h[2] = 1080;
        count = windows_screen_numbers(table_at, number);
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int shown = windows_output(table_at, at, &x, &y, &width, &height);

                matched &= shown == at && identify_number(table_at, at) == number[at];
                placed &= x == table_at->output_x[at] && y == table_at->output_y[at] && width == (int)table_at->output_w[at] &&
                          height == (int)table_at->output_h[at];
        }
        check("identify: each screen's window shows the number windows_screen_numbers gives that screen",
              count == 3 && matched && number[0] == 2 && number[1] == 3 && number[2] == 1);
        check("identify: each screen's window is put on that screen's rectangle", placed);
        table_at->outputs = 0;
        check("identify: a table with no screens shows the one number, 1", identify_number(table_at, 0) == 1);
}

/*
        The screens of two arrangements check_screen_numbers does not have: a 2 by 2 grid listed in no order, and three
        screens across a gap. The numbers run in reading order, the top row left to right and then the row below it, and each screen's window shows the number
        its rectangle carries on the Display map (windows_screen_numbers is the one function both read).
*/
static fn check_screen_arrangements(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        unsigned int number[WINDOWS_OUTPUTS], count;
        int x, y, width, height;
        b32 matched = true, placed = true;

        windows_scratch_zero(table_at);
        table_at->outputs = 4;
        table_at->output_x[0] = 1920, table_at->output_y[0] = 1080, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
        table_at->output_x[1] = 0, table_at->output_y[1] = 0, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
        table_at->output_x[2] = 1920, table_at->output_y[2] = 0, table_at->output_w[2] = 1920, table_at->output_h[2] = 1080;
        table_at->output_x[3] = 0, table_at->output_y[3] = 1080, table_at->output_w[3] = 1920, table_at->output_h[3] = 1080;
        count = windows_screen_numbers(table_at, number);
        check("identify: a 2 by 2 grid is numbered in reading order, top edge then left edge: top left 1, top right 2, bottom left 3, bottom right 4, whatever the list order",
              count == 4 && number[0] == 4 && number[1] == 1 && number[2] == 2 && number[3] == 3);
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int shown = windows_output(table_at, at, &x, &y, &width, &height);

                matched &= shown == at && identify_number(table_at, at) == number[at];
                placed &= x == table_at->output_x[at] && y == table_at->output_y[at];
        }
        check("identify: each screen of the grid shows its own number and is put on its own rectangle", matched && placed);

        windows_scratch_zero(table_at);
        table_at->outputs = 3;
        table_at->output_x[0] = 3000, table_at->output_y[0] = 1200, table_at->output_w[0] = 1280, table_at->output_h[0] = 1024;
        table_at->output_x[1] = 0, table_at->output_y[1] = 0, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
        table_at->output_x[2] = 2200, table_at->output_y[2] = 0, table_at->output_w[2] = 1920, table_at->output_h[2] = 1080;
        count = windows_screen_numbers(table_at, number);
        check("identify: screens across a gap are numbered left to right, then top to bottom",
              count == 3 && number[0] == 3 && number[1] == 1 && number[2] == 2);
        table_at->outputs = 0;
}

// The bounding box of the pixels in a width by height picture: box is left, top, right and bottom, and the answer is
// whether there was any. want 0 is ink (alpha 128 and over, the badge over the clear glass); any other value is the one
// pixel it names exactly.
static b32 identify_ink_box(const unsigned int address_to pixels, int stride, int width, int height, unsigned int want, int box[4])
{
        b32 any = false;

        box[0] = width, box[1] = height, box[2] = -1, box[3] = -1;
        for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                        unsigned int pixel = pixels[y * stride + x];

                        if (want ? pixel == want : pixel >> 24 >= 128)
                        {
                                any = true;
                                box[0] = x < box[0] ? x : box[0];
                                box[1] = y < box[1] ? y : box[1];
                                box[2] = x > box[2] ? x : box[2];
                                box[3] = y > box[3] ? y : box[3];
                        }
                }
        return any;
}

// Whether a and b are within slack of each other.
static b32 identify_near(int a, int b, int slack)
{
        return a - b <= slack && b - a <= slack;
}

// The identify faces and inks for a check's picture: the terminal's faces, the kit's own, with the scratch mapped fresh.
static void identify_check_ui(int scale)
{
        identify.ui = terminal.ui;
        identify.ui.regular = identify.ui.bold = terminal.plain[0];
        identify.ui.scratch = null, identify.ui.scratch_width = 0, identify.ui.scratch_scale = 0;
        identify.ui.glass = false, identify.ui.opaque = true;
        identify.ui.scale = scale * 100;
        identify_size(0, 0);
}

/*
        The identify badge (identify_badge_fit and identify_paint): a white square whose corner is a third of its em from the
        screen's top left, the number black in it, whole on the screen at every size, growing with the screen's scale, and
        smaller only on a screen too small for it. The picture is the badge's own size and opaque. Black on white is 21:1.
*/
static fn check_identify_picture(void)
{
        static struct windows *screens_scratch;
        struct windows *const screens_at = windows_scratch_table(&screens_scratch);
        static unsigned int pixels[800 * 600];
        struct identify_badge small, large, narrow;
        int digit[4], large_digit[4], narrow_digit[4];
        b32 opaque = true, inked;

        windows_scratch_zero(screens_at);
        screens_at->output_width = 1000, screens_at->output_height = 800;
        identify.own.table = screens_at;
        identify.text[0] = '8', identify.text[1] = 0;

        identify_check_ui(1);
        identify_badge_fit(&identify.ui, 400, 300, &small);
        check("identify badge: a white square a third of its em (16 px) from the screen's top left corner, whole on a 400 by 300 screen",
              small.x == 16 && small.y == 16 && small.width > 0 && small.height > 0 && small.x + small.width <= 400 - 16 &&
                  small.y + small.height <= 300 - 16);
        identify.badge = small, identify.width = small.width, identify.height = small.height;
        memory_zero(pixels, sizeof(pixels));
        identify_paint(&identify.ui, pixels, 800, small.width, small.height);
        for (int y = 0; y < small.height; y++)
                for (int x = 0; x < small.width; x++)
                        opaque &= pixels[y * 800 + x] >> 24 == 255;
        check("identify badge: the picture is the badge, opaque white with the number", opaque && pixels[0] == 0xffffffffu);
        inked = identify_ink_box(pixels, 800, small.width, small.height, 0xff000000u, digit);
        check("identify badge: the number is black and centred in the badge",
              inked && identify_near(digit[0] + digit[2], small.width - 1, 6) && identify_near(digit[1] + digit[3], small.height - 1, 8));
        check("identify badge: the paper is white and the number black, exactly, which is 21:1",
              IDENTIFY_PAPER == 0xffffffffu && IDENTIFY_INK == 0x000000u && pixels[0] == IDENTIFY_PAPER);

        identify_check_ui(2);
        identify_badge_fit(&identify.ui, 800, 600, &large);
        check("identify badge: at scale 2 it is twice the size, and still a third of its em (32 px) from the corner",
              large.x == 32 && large.y == 32 && identify_near(large.width, 2 * small.width, 4) &&
                  identify_near(large.height, 2 * small.height, 4));
        identify.badge = large, identify.width = large.width, identify.height = large.height;
        memory_zero(pixels, sizeof(pixels));
        identify_paint(&identify.ui, pixels, 800, large.width, large.height);
        inked = identify_ink_box(pixels, 800, large.width, large.height, 0xff000000u, large_digit);
        check("identify badge: the number is twice as tall at scale 2, so it reads at every scale",
              inked && identify_near(large_digit[3] - large_digit[1], 2 * (digit[3] - digit[1]), 3));

        identify_check_ui(1);
        identify_badge_fit(&identify.ui, 20, 300, &narrow);
        check("identify badge: a screen 20 px wide gets a smaller badge that is whole inside it, two pixels or more from each edge",
              narrow.x >= 2 && narrow.x + narrow.width <= 18 && narrow.width >= 8 && narrow.size < IDENTIFY_SIZE);
        identify.badge = narrow, identify.width = narrow.width, identify.height = narrow.height;
        memory_zero(pixels, sizeof(pixels));
        identify_paint(&identify.ui, pixels, 800, narrow.width, narrow.height);
        inked = identify_ink_box(pixels, 800, narrow.width, narrow.height, 0xff000000u, narrow_digit);
        check("identify badge: the number on the narrow badge is whole and black, clear of the badge's edges",
              inked && narrow_digit[0] > 0 && narrow_digit[2] < narrow.width - 1 && narrow_digit[1] > 0 && narrow_digit[3] < narrow.height - 1);

        identify.own.table = null;
}

/*
        The identify badge on the compositor's own frame, not its picture alone: the window identify_open starts is placed at
        the badge's corner of a 256 by 256 screen by the compositor, and the frame is read back. Every pixel outside the badge,
        its line and its corners, is the desktop as it was with no window; a frosted glass window would change them all. The
        badge's middle is white, and its number black.
*/
static fn check_identify_frame(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE], desktop[GLASS_SIDE * GLASS_SIDE];
        struct windows address_to table;
        int bx, by, bw, bh, placed;
        b32 outside = true;
        unsigned int paper = 0, ink = 0;

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
                check("identify frame: the table is mapped", false);
                return;
        }
        table = compositor.table;
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        memory_copy(desktop, screen, sizeof(screen));

        identify.own.table = table;
        identify.own.row = 1;
        identify.own.door = -1;
        table->glass[1] = WINDOW_NO_CLOSE | WINDOW_NO_TAB; // what ui_app_run writes for the overlay's own flags
        identify.screen = 0;
        ui_buffers_start(&identify.buffers);
        identify.text[0] = '8', identify.text[1] = 0;
        identify_check_ui(1);
        identify_open();
        compositor_take(1);
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();

        bx = identify.badge.x, by = identify.badge.y, bw = identify.badge.width, bh = identify.badge.height;
        placed = compositor.showing[1] && compositor.x[1] == bx && compositor.y[1] == by && compositor.w[1] == (unsigned int)bw &&
                 compositor.h[1] == (unsigned int)bh;
        check("identify frame: the compositor puts the badge's window at its corner of the screen, the size of the badge", placed);
        for (int y = 0; y < GLASS_SIDE; y++)
                for (int x = 0; x < GLASS_SIDE; x++)
                {
                        int at = y * GLASS_SIDE + x, inside_box = x >= bx - 3 && x < bx + bw + 3 && y >= by - 3 && y < by + bh + 3;

                        if (!inside_box && screen[at] != desktop[at])
                                outside = false;
                        if (x >= bx + 4 && x < bx + bw - 4 && y >= by + 4 && y < by + bh - 4)
                        {
                                paper += (screen[at] & 0xffffffu) == 0xffffffu;
                                ink += (screen[at] & 0xffffffu) == 0;
                        }
                }
        check("identify frame: nothing outside the badge changes from the desktop, which is not frosted", outside);
        check("identify frame: the badge is drawn white, with its number black, on the frame", paper > 0 && ink > 0 && paper > ink);

        table->pid[1] = 1;
        compositor_leave(1);
        compositor_call(syscall(munmap), (positive)compositor.table, compositor.pixels + compositor.buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
        compositor_zero();
        identify.own.table = null;
}

static fn check_identify_events(void)
{
        struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0, 0};
        struct window_event release = {WINDOW_EVENT_BUTTON, 0, 0, 0};
        struct window_event key = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 30, 'a'};
        struct window_event up = {WINDOW_EVENT_KEY, 0, 30, 'a'};
        struct window_event motion = {WINDOW_EVENT_MOTION, 0, 5, 5};
        struct window_event wheel = {WINDOW_EVENT_WHEEL, 0, 0, -1};

        check("identify: a press of any button puts the overlay away", !identify_event(&press));
        check("identify: a key going down puts it away", !identify_event(&key));
        check("identify: a button or key coming up, the pointer and the wheel do not",
              identify_event(&release) && identify_event(&up) && identify_event(&motion) && identify_event(&wheel));
}

// The overlay's three seconds: due at its own start plus IDENTIFY_SECONDS, the loop's wait shortened to it, and done
// when it is reached.
static fn check_identify_timing(void)
{
        positive now = 5000000000ull;
        long other = -1, timeout;

        ui_kit.now = (b64)now; // the pass's time: the kit owns the clock, and the overlay reads it (ui_kit_pass)
        identify.until = now + (positive)IDENTIFY_SECONDS * 1000000000ull;
        timeout = identify_tick(-1, &other);
        check("identify: its wait is the three seconds left, and it is not done before them",
              !identify_done() && timeout > 2900000000L && timeout <= (long)(IDENTIFY_SECONDS * 1000000000L));
        check("identify: a shorter wait than the time left is kept", identify_tick(10, &other) == 10);
        identify.until = now;
        check("identify: it is done when its three seconds are over", identify_done());
}

/*
        The terminal's selection on the frame: what a drag marks is marked in the frame's cells (so the frames redraw
        it), its text is the lines' text with a line end between and the trailing blanks left off, and a rectangle takes
        its columns.
*/
static fn terminal_selection_check(void)
{
        static struct window_cell built[80 * 8];
        p8 address_to text;
        positive room, length;
        unsigned int first;

        full_reset();
        term_bytes((const p8 address_to) "alpha beta  \r\ngamma\r\ndelta", 27);
        {
                unsigned int column;

                terminal.view = TERMINAL_LIVE;
                terminal_place_at(0, 0, &first, &column);
        }
        terminal_select_set(terminal_place(first, 6), terminal_place(first + 1, 3));
        terminal.select_box = false;
        length = terminal_select_text(&text, &room);
        check("terminal: a selection's text has a line end between lines and no trailing blanks",
              length == 8 && text && !memory_compare(text, "beta\ngam", 8));
        array_store_release(text, room, length);
        terminal.select_box = true;
        terminal_select_set(terminal_place(first, 1), terminal_place(first + 2, 2));
        length = terminal_select_text(&text, &room);
        check("terminal: a rectangle takes its columns of each line", length == 8 && text && !memory_compare(text, "lp\nam\nel", 8));
        array_store_release(text, room, length);
        terminal.select_box = false;
        terminal.view = TERMINAL_LIVE;
        terminal_build(built);
        {
                unsigned int marked = 0;

                for (unsigned int at = 0; at < COLUMNS * ROWS; at++)
                        marked += (built[at].flags & WINDOW_CELL_SELECTED) != 0;
                check("terminal: the selection is marked in the frame's cells", marked > 0);
        }
        terminal_select_clear();
        full_reset();
}

/*
        The terminal at its edges. A selection made from the top row of a view scrolled to the top copies the lines it
        covers, the same text after the wheel moves the view and in the frame's top rows. A selection that runs past the
        new right edge of a narrower grid keeps its text, and the grid's cursor is held inside the new columns.
*/
static fn terminal_edge_check(void)
{
        static struct window_cell built[80 * 8];
        p8 address_to text;
        positive room, length;
        unsigned int top, top_column, below, below_column, saved = COLUMNS, line;
        b32 marked = false, held = false;

        full_reset();
        for (line = 0; line < 2 * ROWS; line++)
                term_bytes((const p8 address_to) "alpha beta\r\n", 12);
        terminal.view = TERMINAL_LIVE;
        terminal_view_set(0);
        terminal_place_at(0, 0, &top, &top_column);
        terminal_place_at(2, 5, &below, &below_column);
        terminal.select_box = false;
        terminal.sel_anchor = terminal_place(top, 0);
        terminal.sel_active = terminal_place(below, below_column);
        length = terminal_select_text(&text, &room);
        check("terminal edge: a selection from the top row of a view at the top copies its lines",
              length == 27 && text && !memory_compare(text, "alpha beta\nalpha beta\nalpha", 27));
        array_store_release(text, room, length);
        terminal_view_scroll(-1);
        length = terminal_select_text(&text, &room);
        check("terminal edge: the wheel moves the view and the selection's text stays the same",
              length == 27 && text && !memory_compare(text, "alpha beta\nalpha beta\nalpha", 27));
        array_store_release(text, room, length);
        terminal_view_set(0);
        terminal_build(built);
        marked = (built[0].flags & WINDOW_CELL_SELECTED) && (built[2 * COLUMNS + 4].flags & WINDOW_CELL_SELECTED) &&
                 !(built[2 * COLUMNS + 5].flags & WINDOW_CELL_SELECTED);
        check("terminal edge: the marks of that selection are in the view's top rows", marked);

        // The grid narrows to 6 columns with the selection's end past the new edge and the cursor at column 10 of the old
        // grid: the text is the same, and the cursor is held in the 6 columns.
        term_bytes((const p8 address_to) "alpha beta", 10);
        terminal.sel_active = terminal_place(top, 10);
        window->columns = 6;
        regrid(-1);
        terminal_view_clamp();
        length = terminal_select_text(&text, &room);
        check("terminal edge: a selection past the new right edge keeps its text", length == 10 && text && !memory_compare(text, "alpha beta", 10));
        array_store_release(text, room, length);
        held = COLUMNS == 6 && column < COLUMNS && column == 5;
        check("terminal edge: the cursor after a narrower grid is inside its columns", held);
        window->columns = saved;
        regrid(-1);
        terminal_view_clamp();
        terminal_select_clear();
        full_reset();
}

/*
        A press and a release in the gutter to the right of the grid are the scroll bar's, scrolled back or not, and a
        program that asked for the pointer is not told of them: a report there would be clamped onto the grid's last column.
*/
static fn terminal_gutter_check(void)
{
        int ends[2];
        struct window_event event;
        b32 dirty = false;
        char got[64];
        bipolar length = -1;

        if (system_pipe(ends, O_NONBLOCK | O_CLOEXEC) != 0)
        {
                check("terminal gutter: a pipe to catch the reports", false);
                return;
        }
        full_reset();
        mouse_mode = 1000;
        mouse_sgr = true;
        terminal.modifiers = 0;
        terminal.pressed = false;
        ui_kit.pointer_x = (int)terminal.pad + (int)(COLUMNS * terminal.cell_w) + 2;
        ui_kit.pointer_y = (int)terminal.pad_y + 2 * (int)terminal.cell_h;
        event.type = WINDOW_EVENT_BUTTON, event.a = 0x110, event.b = 0, event.flags = WINDOW_KEY_DOWN;
        terminal_deliver((b32)ends[1], &event, &dirty);
        event.flags = 0;
        terminal_deliver((b32)ends[1], &event, &dirty);
        length = system_read_once((b32)ends[0], got, sizeof(got));
        check("terminal gutter: a press and a release in the gutter are not the program's", length < 0);
        mouse_mode = 0;
        mouse_sgr = false;
        system_close(ends[0]);
        system_close(ends[1]);
        full_reset();
}

/*
        The terminal's paper and default ink are the kit's surface and on-surface roles, light or dark, read off the
        pixels of a frame: a blank cell's paper, the ink of an underline (a rule is drawn whole, not blended), the fill a
        block cursor makes of its cell, a reversed blank, and the tint a selected cell takes over its paper. A turn of the
        theme while the terminal is open repaints it in the new pair with its text and cursor where they were, and the
        frame is the full draw of the grid in that pair. The program's colours stay the palette's, in both themes.
*/
#define THEME_BYTES(text) term_bytes((const p8 address_to)(text), sizeof(text) - 1)

static unsigned int theme_pixel(const p32 address_to buffer, unsigned int row, unsigned int column, unsigned int x, unsigned int y)
{
        return buffer[(positive)(terminal.pad_y + row * terminal.cell_h + y) * terminal.pitch + terminal.pad +
                      column * terminal.cell_w + x] & 0xffffffu;
}

// The first pixel of a cell that is not its paper: the ink of a rule through a blank cell, which is drawn in the ink whole.
static unsigned int theme_rule(const p32 address_to buffer, unsigned int row, unsigned int column, unsigned int paper)
{
        for (unsigned int y = 0; y < terminal.cell_h; y++)
                for (unsigned int x = 0; x < terminal.cell_w; x++)
                        if (theme_pixel(buffer, row, column, x, y) != paper)
                                return theme_pixel(buffer, row, column, x, y);
        return paper;
}

// What the terminal answers to OSC 10 (ink) and OSC 11 (paper) with, as the BEL-ended reply.
static b32 theme_answers(string_address question, string_address answer)
{
        positive length = string_length(answer);

        to_shell_length = 0;
        term_bytes((const p8 address_to)question, string_length(question));
        return to_shell_length == length && !memory_compare(to_shell, answer, length);
}

static void terminal_theme_check(void)
{
        static struct window_cell before[80 * 8], after[80 * 8];
        static p32 address_to reference;
        const unsigned int light_paper = ui_theme_dawning.role[UI_SURFACE] & 0xffffffu;
        const unsigned int light_ink = ui_theme_dawning.role[UI_ON_SURFACE] & 0xffffffu;
        const unsigned int dark_paper = ui_theme_night.role[UI_SURFACE] & 0xffffffu;
        const unsigned int dark_ink = ui_theme_night.role[UI_ON_SURFACE] & 0xffffffu;
        static const unsigned int sixteen[16] = {0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
                                                 0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff};
        p32 address_to buffer;
        unsigned int first, column, paper, ink, selected, mismatch = 0, palette = 0;

        if (!reference)
                reference = render_map((positive)terminal.pitch * terminal.height * 4);
        cursor_hide();
        ui_kit_theme(UI_THEME_LIGHT, UI_ACCENT_DAWN, 1);
        full_reset();
        THEME_BYTES("\x1b[1;1H\x1b[4m \x1b[0m");          // row 0, column 0: an underlined blank
        THEME_BYTES("\x1b[1;2H\x1b[7m \x1b[0m");          // row 0, column 1: a reversed blank
        THEME_BYTES("\x1b[1;3H\x1b[44m \x1b[0m");         // row 0, column 2: blue paper the program asked for
        THEME_BYTES("\x1b[1;4H\x1b[40m \x1b[0m");         // row 0, column 3: black paper the program asked for
        THEME_BYTES("\x1b[1;5H\x1b[37m\x1b[4m \x1b[0m");  // row 0, column 4: grey ink the program asked for, underlined
        THEME_BYTES("\x1b[7;1Habc");                      // row 6: text, which a turn must keep
        THEME_BYTES("\x1b[5;1H");                         // row 4, column 0: the block cursor
        cursor_show();
        terminal.view = TERMINAL_LIVE;
        terminal_place_at(6, 4, &first, &column);
        terminal.select_box = false;
        terminal.sel_anchor = terminal_place(first, 4);
        terminal.sel_active = terminal_place(first, 6);

        buffer = render_frame();
        terminal_build(before);
        paper = theme_pixel(buffer, 2, 0, 1, 1);
        ink = theme_rule(buffer, 0, 0, paper);
        check("terminal theme light: the paper is the kit's light surface", paper == light_paper);
        check("terminal theme light: the default ink is the kit's light on-surface ink", ink == light_ink);
        check("terminal theme light: the default ink reads on the paper at 4.70:1 or more", ui_contrast(ink, paper) >= UI_READABLE);
        check("terminal theme light: a reversed blank is the ink", theme_pixel(buffer, 0, 1, 1, 1) == light_ink);
        check("terminal theme light: the cursor's block is the ink", theme_pixel(buffer, 4, 0, 1, 1) == light_ink);
        check("terminal theme light: the program's blue paper is the palette's", theme_pixel(buffer, 0, 2, 1, 1) == window_cell_colour(4));
        check("terminal theme light: the program's black paper is black, not the paper",
              theme_pixel(buffer, 0, 3, 1, 1) == window_cell_colour(0));
        check("terminal theme light: the program's grey ink is the palette's", theme_rule(buffer, 0, 4, paper) == window_cell_colour(7));
        selected = theme_pixel(buffer, 6, 5, 1, 1);
        check("terminal theme light: a selected cell is tinted over the paper", selected != paper);
        check("terminal theme light: the default ink reads on the selection at 4.70:1 or more",
              ui_contrast(ink, selected) >= UI_READABLE);
        check("terminal theme light: the kit's lit scroll bar reads on the paper at 4.70:1 or more",
              ui_contrast(ui_role(UI_ON_SURFACE) & 0xffffffu, paper) >= UI_READABLE);
        check("terminal theme light: the OSC 11 question is answered with the paper",
              theme_answers("\x1b]11;?\x07", "\x1b]11;rgb:f7f7/f7f7/f8f8\x07"));
        check("terminal theme light: the OSC 10 question is answered with the ink",
              theme_answers("\x1b]10;?\x1b\\", "\x1b]10;rgb:1818/1818/1b1b\x1b\\"));
        for (unsigned int at = 0; at < 16; at++)
                palette += terminal_colour[at] == sixteen[at];
        check("terminal theme: the sixteen colours are the palette's", palette == 16);
        string_format(log, "  light: paper %p ink %p ratio %p hundredths, selection %p, scroll bar %p\n", paper, ink,
                      ui_contrast(ink, paper), ui_contrast(ink, selected),
                      ui_contrast(ui_role(UI_ON_SURFACE) & 0xffffffu, paper));

        // The theme turns to the dark pair while the terminal is open: its text and cursor do not move.
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_DAWN, 1);
        buffer = render_frame();
        terminal_build(after);
        check("terminal theme dark: a turn keeps the text, the cursor and the selection", !memory_compare(before, after, sizeof(before)));
        paper = theme_pixel(buffer, 2, 0, 1, 1);
        ink = theme_rule(buffer, 0, 0, paper);
        check("terminal theme dark: the paper is the kit's dark surface", paper == dark_paper);
        check("terminal theme dark: the default ink is the kit's dark on-surface ink", ink == dark_ink);
        check("terminal theme dark: the default ink reads on the paper at 4.70:1 or more", ui_contrast(ink, paper) >= UI_READABLE);
        check("terminal theme dark: a reversed blank is the ink", theme_pixel(buffer, 0, 1, 1, 1) == dark_ink);
        check("terminal theme dark: the cursor's block is the ink", theme_pixel(buffer, 4, 0, 1, 1) == dark_ink);
        check("terminal theme dark: the program's blue and black paper are the palette's",
              theme_pixel(buffer, 0, 2, 1, 1) == window_cell_colour(4) && theme_pixel(buffer, 0, 3, 1, 1) == window_cell_colour(0));
        check("terminal theme dark: the program's grey ink is the palette's", theme_rule(buffer, 0, 4, paper) == window_cell_colour(7));
        selected = theme_pixel(buffer, 6, 5, 1, 1);
        check("terminal theme dark: a selected cell is tinted over the paper", selected != paper);
        check("terminal theme dark: the default ink reads on the selection at 4.70:1 or more", ui_contrast(ink, selected) >= UI_READABLE);
        check("terminal theme dark: the kit's lit scroll bar reads on the paper at 4.70:1 or more",
              ui_contrast(ui_role(UI_ON_SURFACE) & 0xffffffu, paper) >= UI_READABLE);
        check("terminal theme dark: the OSC 11 question is answered with the paper",
              theme_answers("\x1b]11;?\x07", "\x1b]11;rgb:1111/1111/1313\x07"));
        check("terminal theme dark: the OSC 10 question is answered with the ink",
              theme_answers("\x1b]10;?\x1b\\", "\x1b]10;rgb:eded/eded/efef\x1b\\"));
        string_format(log, "  dark: paper %p ink %p ratio %p hundredths, selection %p, scroll bar %p\n", paper, ink,
                      ui_contrast(ink, paper), ui_contrast(ink, selected),
                      ui_contrast(ui_role(UI_ON_SURFACE) & 0xffffffu, paper));

        // The whole picture after the turn is the full draw of the grid in the dark pair.
        if (reference)
        {
                render_reference(reference, terminal.frame[terminal.shown]);
                for (unsigned int y = 0; y < terminal.height; y++)
                        for (unsigned int x = 0; x < terminal.width; x++)
                                mismatch += buffer[(positive)y * terminal.pitch + x] != reference[(positive)y * terminal.pitch + x];
        }
        check("terminal theme dark: the repainted frame is the full draw of the grid", reference && !mismatch);

        ui_kit_theme(UI_THEME_LIGHT, UI_ACCENT_DAWN, 1);
        buffer = render_frame();
        check("terminal theme light again: the paper turns back", theme_pixel(buffer, 2, 0, 1, 1) == light_paper);

        cursor_hide();
        terminal_select_clear();
        full_reset();
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
}

/*
        The emulator's parameter store and its UTF-8 and escape state at a session's start. A CSI takes every parameter it is given, not the first 32 (an SGR
        of 40 attributes whose last is red ink turns the ink red); and a pty that ended inside a sequence or a partial character leaves nothing for the next
        session's first output (its first byte is the byte it is, not a U+FFFD and not a swallowed final).
*/
static void check_term_parameters(void)
{
        char bytes[160];
        positive length = 0;

        full_reset();
        bytes[length++] = 0x1b, bytes[length++] = '[';
        for (unsigned int at = 0; at < 39; at++)
                bytes[length++] = '0', bytes[length++] = ';';
        bytes[length++] = '3', bytes[length++] = '1', bytes[length++] = 'm', bytes[length++] = 'R';
        term_bytes((const p8 address_to)bytes, length);
        check("term params: an SGR of 40 parameters ends with its last one applied: red ink on the R after 39 resets", row_cells(0)[0].character == 'R' && row_cells(0)[0].ink == 1);

        full_reset();
        term_bytes((const p8 address_to) "\xe2\x82", 2);
        full_reset();
        term_bytes((const p8 address_to) "a", 1);
        check("term params: a pty that ended mid-character leaves no U+FFFD: the next session's first byte is the a", row_cells(0)[0].character == 'a');

        full_reset();
        term_bytes((const p8 address_to) "\x1b[31", 4);
        full_reset();
        term_bytes((const p8 address_to) "xy", 2);
        check("term params: a pty that ended inside a CSI does not swallow the next session's first byte as its final", row_cells(0)[0].character == 'x' && row_cells(0)[1].character == 'y');
        full_reset();
}

/*
        The present record's frames (record.h): a recording whose screen changes size between two of its frames copies the second at its new size. The block
        was made for the first size, so the copy ran past its end (a 128 by 64 frame into a block of 64 by 32: three 8 KiB frames, then the ring). The block is
        made again for the new size and the recording's frames start over.
*/
static void record_rows(void)
{
        static unsigned int small[64 * 32], large[128 * 64];
        unsigned int *saved_scanout = compositor.scanout;
        int saved_width = compositor.width, saved_height = compositor.height;
        unsigned long saved_pitch = compositor.scanout_pitch;
        const char path[] = COMPOSITOR_RECORD_DIR "/frame-4294967-check.ppm";
        unsigned char st[256] = {0};
        unsigned short mode = 0;

        compositor_record_free();
        compositor_record_refused_size = 0;
        for (unsigned int i = 0; i < 64 * 32; i++)
                small[i] = 0x11111111u;
        for (unsigned int i = 0; i < 128 * 64; i++)
                large[i] = 0x22222222u;

        compositor.scanout = small, compositor.width = 64, compositor.height = 32, compositor.scanout_pitch = 64 * 4;
        compositor_record_capture(1);
        compositor.scanout = large, compositor.width = 128, compositor.height = 64, compositor.scanout_pitch = 128 * 4;
        compositor_record_capture(2);
        check("record: a frame of a screen that changed size is copied into a block made for that size",
              compositor_record_width == 128 && compositor_record_height == 64 && compositor_record_taken == 1 && compositor_record_shots[0].pixels[0] == 0x22222222u);

        // A frame file is the screen's pixels, so it is the owner's alone. The directory is /run/moonwater/record, which only a privileged run makes:
        // where it can be written the mode is checked, and where it cannot it is said so.
        compositor_record_write_ppm(large, 2, 2, 2, 4294967u, "check");
        if (system_stat_at(AT_FDCWD, (string_address)path, 0, 0x7ffu, st) != 0)
                string_format(log, "  record: %s is not writable here: the frame file's mode is not checked\n", (string_address)COMPOSITOR_RECORD_DIR);
        else
        {
                memory_copy(&mode, st + 28, 2);
                check("record: a frame file is 0600, the screen's pixels are the owner's alone", (mode & 0777) == 0600);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0, 0, 0, 0);
        }

        compositor_record_free();
        compositor.scanout = saved_scanout, compositor.width = saved_width, compositor.height = saved_height, compositor.scanout_pitch = saved_pitch;
}

b32 main()
{
        compositor_rows_make(compositor_rows_in_use); // the compositor's rows, made before any check reads one
        string_address paths[1];
        positive compared = 0, wrong = 0, refused = 0;

        // The transitions run as designed in every check (ui_paced at 100): the durations the rows pin are the designed ones.
        ui_pace = UI_PACE_DESIGNED;

        if (program_argument_count() < 2)
        {
                string_format(log, "  NOT RUN: the terminal's frames, no font was given\n");
                return test_report(null);
        }

#ifdef TASKBAR_PICTURES
        band_pictures(program_argument_count() > 3 ? program_argument(3) : null);
        return 0;
#endif
#ifdef WALLPAPER_BENCH
        wallpaper_bench((string_address)WALLPAPER_BENCH);
        return 0;
#endif
        paths[0] = program_argument(1);
        // The kit's UI faces are the repository's Inter files (the arguments after the mono faces), as ui_app_run's are on a machine.
        if (program_argument_count() > 4)
                terminal_ui_regular_path = program_argument(3), terminal_ui_bold_path = program_argument(4);
        check("fonts: the face loads", terminal_fonts(paths, 1, program_argument_count() > 2 ? program_argument(2) : null, 16));
        check("fonts: the kit's own text is set in the UI faces (Inter), and the grid stays in the mono face",
              terminal.ui.regular == &terminal.ui.faces[0] && terminal.ui.bold == &terminal.ui.faces[1] && terminal.plain[0] != terminal.ui.regular);
        if (failures)
                return test_report(null);
        record_rows();

        {
                unsigned int shipped = 0, wanted = 0;
                p8 soft = true;

                for (unsigned int at = 2; at < program_argument_count(); at++)
                {
                        font_face other;
                        positive length = 0;
                        positive bytes = compositor_map_file(program_argument(at), &length);

                        wanted++;
                        if (bytes && font_load(&other, (const p8 address_to)bytes, length, 0) == 0 && font_glyph(&other, 'A'))
                                shipped++;
                        {
                                //      A soft hyphen a face has not is drawn as nothing, not as the face's notdef.
                                p32 face = 0, glyph = 0;

                                if (bytes && !font_glyph(&other, 0xad))
                                {
                                        font_find(terminal.ui.engine, (const font_face *const[]){&other}, 1, 0xad, &face, &glyph);
                                        soft = soft && face == FONT_DRAWN;
                                }
                        }
                }
                check("fonts: every face the image ships loads and has the letter A", wanted && shipped == wanted);
                check("fonts: a soft hyphen a shipped face lacks is drawn as nothing, not as its notdef", soft);
        }

        if (program_argument_count() > 3)
                text_widget_check(program_argument(3), program_argument(1));
                text_bidi_check(program_argument(3));
        if (program_argument_count() > 3)
                kit_bidi_check(program_argument(3));
        text_menu_keys_check();
        for (unsigned int at = 0; at < 256; at++)
                terminal_colour[at] = window_cell_colour(at);
        term_palette = terminal_palette;
        // The kit's bar is drawn wherever the view has a scrollback, with nothing held or hovered, so the frames are held to it too.
        terminal.bar.held = terminal.bar.hover = 0;

        window = window_grid_open(30, 8, 80, 256);
        check("grid: a ring in the program's own memory", window != null);
        if (!window)
                return test_report(null);
        grid_take();
        full_reset();
        grid_take();
        full_reset();
        render_surface();
        cursor_show();
        terminal_selection_check();
        terminal_gutter_check();
        cursor_show();

        //      The glyphs a program prints, btop's among them, each in its own cell of a
        //      30 by 8 grid: every printable ASCII character, the shapes, braille, box
        //      and block characters and the arrow and cross that btop and its kin draw,
        //      and a bold row. A frame of the cell is lit where a pixel is not the blank
        //      paper beside it. Red on the parent where no face and no procedure draws it.
        {
                static const p32 glyphs[] = {
                    0x25a0, 0x25a1, 0x25aa, 0x25ab, 0x25ac, 0x25b2, 0x25b3, 0x25bc, 0x25bd, 0x25b6, 0x25b7, 0x25c0,
                    0x25c1, 0x25cf, 0x25cb, 0x25c6, 0x25c7, 0x25e6, 0x25b4, 0x25be, 0x25b8, 0x25c2, 0x25ba, 0x25c4,
                    0x2847, 0x28ff, 0x2880, 0x28c0, 0x28f0, 0x2809, 0x2803, 0x28b8,
                    0x256d, 0x2500, 0x256e, 0x2502, 0x2570, 0x256f, 0x2550, 0x2551, 0x2554, 0x2557, 0x255a, 0x255d,
                    0x250c, 0x2510, 0x2514, 0x2518, 0x253c, 0x2581, 0x2586, 0x2588, 0x2591, 0x2592, 0x2593, 0x2590,
                    0x21b5, 0x2717, 0x2191, 0x2193, 0x2190, 0x2192, 0x00b7, 0x2022, 0x2026};
                static const p32 bold[] = {0x25a0, 0x25b2, 0x25bc, 0x25cb, 0x25cf, 0x25c6};
                p8 every = true;
                p32 address_to pixels = null;
                p32 paper = 0;

                cursor_hide();
                term_bytes((const p8 address_to) "\x1b[H\x1b[2J", 7);
                for (p32 c = 0x21; c < 0x7f; c++)
                {
                        p8 bytes[4];
                        positive length = render_glyph(bytes, c);

                        term_bytes(bytes, length);
                        if ((c - 0x21 + 1) % 30 == 0)
                                term_bytes((const p8 address_to) "\r\n", 2);
                }
                term_bytes((const p8 address_to) "\r\n", 2);
                for (p32 k = 0; k < sizeof(glyphs) / sizeof(glyphs[0]); k++)
                {
                        p8 bytes[4];
                        positive length = render_glyph(bytes, glyphs[k]);

                        term_bytes(bytes, length);
                }
                //      U+E000 is in neither face: its cell is the picture a missing glyph gets.
                term_bytes((const p8 address_to) "\x1b[8;21H\xee\x80\x80\x1b[8;1H\x1b[1m",
                           sizeof("\x1b[8;21H\xee\x80\x80\x1b[8;1H\x1b[1m") - 1);
                for (p32 k = 0; k < sizeof(bold) / sizeof(bold[0]); k++)
                {
                        p8 bytes[4];
                        positive length = render_glyph(bytes, bold[k]);

                        term_bytes(bytes, length);
                }
                term_bytes((const p8 address_to) "\x1b[0m", 4);
                cursor_show();
                to_shell_length = 0;
                pixels = render_frame();
                pixels = pixels ? terminal_grid(pixels) : null;
                if (!pixels)
                        every = false;
                else
                {
                        //      The blank cell at the bottom right is the paper; a cell is lit where a pixel is not it.
                        paper = pixels[(positive)(8 * terminal.cell_h - 1) * terminal.width + 30 * terminal.cell_w - 1];
                        for (p32 at = 0; at < 94; at++)
                                every = every && render_drawn(pixels, paper, at / 30, at % 30);
                        for (p32 at = 0; at < sizeof(glyphs) / sizeof(glyphs[0]); at++)
                                every = every && render_drawn(pixels, paper, 4 + at / 30, at % 30);
                        for (p32 at = 0; at < sizeof(bold) / sizeof(bold[0]); at++)
                                every = every && render_drawn(pixels, paper, 7, at);
                }
                check("glyphs: every printable ASCII character, and each shape, braille, box and block glyph btop draws, lights its cell", every);
        }
        terminal_edge_check();

        for (positive step = 0; step < 40000; step++)
                if (!render_step(&compared, &wrong, step))
                        refused++;

        // The same steps with the window's list mode on: its frames are recorded (ui_app_run's terminal list) and the lists replayed.
        {
                positive listed_compared = 0, listed_wrong = 0;
                p64 saved = render_state; // the later checks draw the same numbers as without these steps

                render_listing = true;
                ui_draw_list_init(&ui_app_list, (unsigned char address_to)ui_map(WINDOW_LIST_BYTES), WINDOW_LIST_BYTES, WINDOW_LIST_ROOM);
                ui_app_ui = &terminal.ui, ui_app_row = terminal.own.row;
                render_surface();
                for (positive step = 0; step < 6000; step++)
                        if (!render_step(&listed_compared, &listed_wrong, step))
                                refused++;
                // The cost of a full frame on the state the steps left (min of 50 each): recorded into the list (the frame's whole
                // list path, the same painter) against the processor's full draw of the same cells. The ops and cover bytes are the list's.
                {
                        positive best_record = 0, best_paint = 0;
                        struct ui_draw_view measured;
                        static const char line[] = "\x1b[31mred \x1b[0mThe quick brown fox jumps over the lazy dog 0123456789 abcdefghij\r\n";

                        // A full grid of text, the largest the rig's ring holds (50 by 14 cells): the cells the measurement draws, the list's and the processor's alike.
                        cursor_hide();
                        window->columns = 50;
                        window->rows = 14;
                        regrid(-1);
                        terminal_view_clamp();
                        terminal_view_live();
                        render_surface();
                        for (int at = 0; at < 60; at++)
                                term_bytes((const p8 address_to)line, sizeof(line) - 1);
                        to_shell_length = 0;
                        cursor_show();

                        for (positive k = 0; k < 50; k++)
                        {
                                positive started, ended;

                                terminal.own.table->shown[0] = terminal.own.table->frame[0];
                                started = render_clock();
                                if (terminal_frame())
                                        ended = render_clock(), best_record = !best_record || ended - started < best_record ? ended - started : best_record;
                                started = render_clock();
                                render_reference(render_full_pixels, terminal.frame[2]);
                                ended = render_clock();
                                best_paint = !best_paint || ended - started < best_paint ? ended - started : best_paint;
                        }
                        if (ui_draw_list_open((unsigned char address_to)ui_app_list.base, WINDOW_LIST_BYTES, &measured) == 1)
                                string_format(log, "  terminal list, a full %p by %p frame: %p ops, %p cover bytes; record %p us, processor's full draw %p us (min of 50)\n",
                                              (positive)COLUMNS, (positive)ROWS, (positive)measured.count, (positive)measured.cover_used,
                                              best_record / 1000, best_paint / 1000);
                        {
                                // The same frame's pool: its coverage copies and tone sets (each copy is its bitmap's bytes, once per distinct glyph).
                                positive covers = 0, tones = 0;

                                for (unsigned int k = 0; k < UI_SCENE_SLOTS; k++)
                                        covers += ui_app_list.scene.cover_slots[k].used, tones += ui_app_list.scene.tone_slots[k].used;
                                string_format(log, "  terminal list pool: %p coverage copies, %p tone sets, %p bytes\n", covers, tones, (positive)ui_app_list.scene.used);
                        }
                }
                /*
                        An idle terminal presents no frame after it settles: with the keys and a caret on the grid, nothing said and nothing
                        moved, a frame is due only where the caret's cover changes. Its solid phase is drawn on the processor into both
                        pictures, then the list mode is on and the clock in the off phase: the first knock records the off caret as a list,
                        and each knock after it (the compositor took the list, the blink's wait is read again) presents nothing. A list
                        frame that kept no caret for buffer 0 left the solid one there, and the owner's machine took a list at every knock.
                */
                {
                        struct windows address_to table = terminal.own.table;
                        unsigned int own = terminal.own.row, first_lists = 0, later_lists = 0, speed_was = ui_kit.caret_speed;
                        positive since = 20000000000ull, cycle, at, clock_was = ui_kit.test_clock, now_was = (positive)ui_kit.now, since_was = terminal.caret.since;
                        b32 focused_was = terminal.focused, caret_on;
                        long timeout = -1;

                        // The caret on (a step of the stream may have hidden it) and the view at its live end, where the caret is.
                        cursor_hide();
                        term_bytes((const p8 address_to) "\x1b[?25h", 6);
                        to_shell_length = 0;
                        cursor_show();
                        terminal_view_live();
                        ui_kit.caret_speed = UI_CARET_NORMAL;
                        cycle = (positive)ui_caret_cycle();
                        terminal.focused = true;
                        ui_kit.now = ui_kit.test_clock = since;
                        terminal_caret_step(since, &timeout);
                        terminal.caret.since = since;
                        table->list_mode = 0;
                        for (unsigned int k = 0; k < 2; k++)
                        {
                                table->shown[own] = table->frame[own];
                                terminal_frame();
                        }
                        caret_on = terminal_caret_index != ~0u && terminal.caret_cover[0] == UI_CARET_FULL && terminal.caret_cover[1] == UI_CARET_FULL;
                        // The list is wanted at once: no back-off a lost list of the stream left (ui_list_wanted), and no key held as lost.
                        ui_list_skip = ui_list_wait = ui_list_whole_run = 0;
                        terminal.list_lost = 0;
                        table->list_mode = 1;
                        for (unsigned int knock = 0; knock < 20; knock++)
                        {
                                unsigned int listed = render_generation(), framed = table->frame[own];

                                at = since + cycle / 2 + (positive)knock * 1000000ull;
                                ui_kit.now = ui_kit.test_clock = at;
                                table->shown[own] = table->frame[own];
                                terminal.dirty = false;
                                timeout = -1;
                                terminal_caret_step(at, &timeout);
                                if (terminal.dirty && terminal_frame())
                                        terminal.dirty = false;
                                if (!knock)
                                        first_lists = render_generation() != listed && table->frame[own] != framed;
                                else
                                        later_lists += table->frame[own] - framed;
                        }
                        string_format(log, "  idle terminal: the caret solid in both pictures %p, the off caret's list at the first knock %p, frames at the next 19 knocks %p (shown %p, list skip %p, lost %p)\n",
                                      (positive)caret_on, (positive)first_lists, (positive)later_lists, (positive)shown, (positive)ui_list_skip,
                                      (positive)terminal.list_lost);
                        check("terminal lists: an idle terminal with the keys records its caret's off phase once, and presents no frame after it settles",
                              caret_on && first_lists && later_lists == 0);
                        terminal.dirty = false;
                        terminal.focused = focused_was, terminal.caret.since = since_was, ui_kit.caret_speed = speed_was;
                        ui_kit.test_clock = clock_was, ui_kit.now = now_was;
                }
                render_listing = false;
                render_state = saved;
                ui_app_list.ready = 0, ui_app_ui = null;
                // The list mode is the rig's own for these steps: the window it was on has no list after them, so the compositor's
                // list path (compositor_lists_take) is not taken for the later checks' windows.
                terminal.own.table->list_mode = 0;
                terminal.own.table->list_state[terminal.own.row] = 0;
                compared += listed_compared, wrong += listed_wrong;
                string_format(log, "  terminal lists: %p frames of %p steps were taken by the list\n", (positive)render_list_frames, (positive)6000);
                check("terminal lists: the window's list took frames, and the steps drew them", render_list_frames > 0);
                check("terminal lists: each list replays to the processor's full draw, left of the bar", listed_wrong == 0 && listed_compared > 0);
        }

        //      What the stream left is no later check's to start from: a grid of one row, the alternate screen, a
        //      program's pointer modes or a view scrolled back failed the killed and pointer rows for some starts of
        //      the stream. The grid of 30 by 8 again, then the emulator reset in it (reset first, the screen's lines
        //      were blanked at the stream's width and each folded in two at 30), the view live and a window of its own.
        cursor_hide();
        window->columns = 30;
        window->rows = 8;
        regrid(-1);
        full_reset();
        terminal_view_clamp();
        terminal_view_live();
        render_surface();
        cursor_show();

        check_forced();
        check_frames_refused();
        check_killed();
        check_fold();
        check_keys();
        check_pointer();
        check_terminal_select_scroll();
        check_terminal_select_speed();
        check_terminal_select_stale();
        check_edge_steps();
        check_term_parameters();
        check_kit_zoom_key();
        check_kit_clip_chord();
        check_kit_select_rule();
        check_take();
        check_land();
        check_glass();
        check_glass_lift();
        check_screen_numbers();
        check_no_tab();
        check_identify_numbers();
        check_screen_arrangements();
        check_identify_picture();
        check_identify_frame();
        check_identify_events();
        check_identify_timing();
        check_compositor_partial();
        check_compositor_runs();
        check_compositor_held();
        check_compositor_screens();
        check_compositor_scales();
        check_compositor_corner_scales();
        check_compositor_turn();
        check_compositor_refresh();
        check_compositor_preview();
        check_compositor_snap_tint();
        check_compositor_preview_held();
        check_compositor_aside();
        check_compositor_ground();
        check_compositor_chip_clip();
        check_compositor_shared();
        check_compositor_cycle();
        check_compositor_hover();
        check_compositor_lit();
        check_compositor_border();
        check_compositor_chrome();
        check_compositor_focus_line();
        check_compositor_minimise();
        check_compositor_glyphs();
        check_compositor_predict();
        check_compositor_predict_drag();
        check_compositor_predict_model();
        check_compositor_predict_table();
        check_compositor_picture();
        check_compositor_pointer_aside();
        check_compositor_lifecycle();
        check_compositor_lifecycle_seam();
        check_compositor_lifecycle_more();
        check_compositor_least_scale();
        check_compositor_touch_hold_gone();
        check_compositor_card_lists();
        check_compositor_card_turn();
        check_compositor_space_sizes();
        check_compositor_record_look();
        check_compositor_card_edid();
        check_compositor_card_walk();
        check_compositor_wake();
        check_compositor_rate_fastest();
        check_compositor_screen_wait();
        check_compositor_start_room();
        check_compositor_setting_opens();
        check_compositor_pipe_ignored();
        check_compositor_signal_again();
        check_compositor_table_sealed();
        check_compositor_table_taken();
        check_compositor_cursor_probe();
        check_compositor_input_origin();
        check_window_input_game();
        check_compositor_doors_held();
        check_compositor_asked();
        check_compositor_tiling();
        check_compositor_layout();
        check_compositor_push();
        check_compositor_seam();
        check_compositor_assist();
        check_compositor_grid_seam();
        check_compositor_tile_line();
        check_compositor_snap_cover();
        check_compositor_snap_closing();
        check_compositor_glide();
        check_compositor_preview_at_once();
        check_compositor_snap_trigger();
        check_compositor_snap_edges();
        check_compositor_restore();
        check_compositor_cancel();
        check_compositor_screen_chord();
        check_compositor_preview_motion();
        check_compositor_tile_square();
        check_compositor_preview_over();
        check_compositor_restore_glide();
        check_compositor_tile_resize();
        check_compositor_least();
        check_compositor_divider();
        check_compositor_divider_scales();
        check_compositor_touch_hold();
        check_compositor_snap_frames();
        check_compositor_overlay_fade();
        check_compositor_grid_drag();
        check_compositor_wheel();
#ifdef COMPOSITOR_LINE
        check_compositor_touch();
        check_compositor_edges();
#endif
#if MOONWATER_UI_ANY && defined(UI_TARGET)
        check_ui_press();
        check_kit_wide_span();
        check_icon_tone_tail();
#endif
        check_compositor_dismiss();
        check_compositor_close_over_control();
        check_compositor_popup();
        check_compositor_popup_press(0);
        check_compositor_popup_press(1);
        check_popup_reopen();
        check_popup_ground_steady();
        check_dropdown_labels();
        check_dropdown_selected();
        check_dropdown_scroll();
        check_wheel_modifiers();
        check_compositor_popup_grip(0);
        check_compositor_popup_grip(1);
        check_compositor_popup_holds();
        check_compositor_glass_popup();
        check_compositor_tray();
        check_compositor_tab_reach();
        check_compositor_launcher();
        check_compositor_place();
        check_compositor_remember();
#if MOONWATER_UI_LAUNCHER
        check_launcher();
        check_launcher_anchor();
#endif
        check_compositor_look();
        check_compositor_click();
        check_compositor_cursor_style();
        check_compositor_pointer_table();
        check_taskbar_geometry();
        check_rtl_chrome();
        check_taskbar_tray();
        check_taskbar_launcher();
        check_taskbar_centre();
        check_taskbar_highlight();
        check_launcher_centred();
        check_clock_places();
        check_moon_motion();
        check_wallpaper_ground();
        check_bar_options();
        check_taskbar_tree();
        check_taskbar_fullscreen();
#if MOONWATER_UI_ANY
        check_kit_motion();
        check_kit_theme();
        check_motion_pace();
        check_hand_settings();
        check_kit_arc();
        check_kit_ramps();
#if X64
        check_kit_walk();
#endif
        check_kit_fills();
        check_kit_corners();
        check_kit_slider_below();
        check_kit_lit();
        check_kit_focus();
        check_kit_virtual();
        check_kit_pull_hit();
        check_kit_pull_parts();
        check_kit_view_pull();
        check_kit_trail();
        check_kit_graph();
        check_kit_inks();
        check_kit_place();
        check_kit_controls();
        check_kit_states();
        check_kit_strips();
        check_kit_buttons();
        check_kit_scroll_ends();
        check_kit_scroll_robust();
        check_kit_ripple_pace();
        check_kit_ripple_centre();
        check_kit_ripple_cover();
        check_kit_event_context();
        check_kit_buffers();
        check_kit_page_pointer();
        check_kit_slider_row();
        check_kit_finger_pull();
        check_kit_confirm_repeat();
        check_kit_layout();
        check_kit_view();
        check_kit_view_scroll();
        check_kit_mouse_drag();
        check_kit_view_controls();
        check_kit_view_wrap();
        check_kit_list_wrap();
        check_kit_hand();
        check_kit_wheel_near_end();
        check_kit_notch_take();
        check_kit_double_press();
        check_kit_cell_rules();
        check_kit_touch_gestures();
        check_kit_pinch_zoom();
        check_pinch_delivery();
        check_kit_axis_swipe();
        check_pad_pinch();
        check_pad_scroll();
        check_pad_gain();
        check_wheel_stream();
        check_wheel_mouse();
        check_scroll_bits();
        check_logo_tap();
        check_wheel_memory();
        check_kit_precise_stream();
        check_stream_notch();
        check_trackpad_speed();
        check_wheel_report();
        check_stream_gain();
        check_stream_smooth();
        check_stream_fling();
        check_kit_anchor_flip();
        check_kit_spring_fresh();
        check_kit_wheel_flood();
        check_kit_body();
        check_terminal_wheel_curve();
        check_kit_turn();
        check_kit_tab_ripple();
#endif
#if MOONWATER_UI_TASKBAR
        check_taskbar_text(program_argument_count() > 3 ? program_argument(3) : null);
        check_moon_bench();
        check_taskbar_edit();
        check_taskbar_tab_menu();
        check_taskbar_pins();
        check_wallpaper_turn();
        check_icons();
        check_taskbar_identity();
        check_taskbar_guard();
        check_scene_damage_cut();
        check_compositor_volume(); // after the band's face is loaded: the cell is drawn with it
        check_taskbar_scale();
#if MOONWATER_UI_NOTICES && MOONWATER_UI_TASKBAR
        check_notices_dot();
#endif
#endif
#if MOONWATER_UI_SETTINGS
        check_settings(program_argument_count() > 3 ? program_argument(3) : program_argument(1),
                       program_argument_count() > 4 ? program_argument(4) : null);
        check_settings_groups();
        check_settings_device_strip();
        check_settings_kept();
        check_settings_drive_rows(program_argument_count() > 3 ? program_argument(3) : program_argument(1),
                                  program_argument_count() > 4 ? program_argument(4) : null);
        check_settings_keys();
        check_settings_frames(program_argument_count() > 3 ? program_argument(3) : program_argument(1),
                              program_argument_count() > 4 ? program_argument(4) : null);
        check_rtl_mirror();
        check_display_section();
        check_battery();
#endif
#if MOONWATER_UI_SETTINGS && MOONWATER_UI_LAUNCHER
        if (program_argument_count() > 3)
                check_field_hands(program_argument(3));
#endif
#if MOONWATER_UI_LAUNCHER
        if (program_argument_count() > 3)
                check_glass_pieces(program_argument(3));
#endif
        check_no_hand_typing();
        check_kit_standard();
        check_kit_clock();
        check_one_blink();
#if MOONWATER_UI_ANY
        language_directory = "languages/";
        language_check();
#endif
        check_one_cut();
        check_one_digits();
#if MOONWATER_UI_ANY
        check_caret_blink();
        check_caret_terminal();
        check_terminal_stretch();
        check_terminal_stretch_edges();
        check_terminal_cell_hit();
        check_terminal_stretch_cost();
#endif
        check_no_input_codes();
        check_no_shapes_by_hand();
        check_no_element_by_hand();
#if MOONWATER_UI_ANY
        check_kit_defaults(&terminal.ui);
#endif
#if MOONWATER_UI_EDITOR
        check_editor_comments();
        check_editor_lines_put();
#endif

        // What the terminal says its paper and ink are is what it paints them with: the theme's pair, and the palette's.
        terminal_theme_check();
        check_bar_cells();
        check("frames: a buffer is refused only while the compositor may still be reading it", !refused);
        check("frames: some were drawn", compared > 1000);
        check("frames: the screen and the buffer are always the full draw", !wrong);
        string_format(log, "  %p frames compared\n", compared);
        return test_report(null);
}
#endif
