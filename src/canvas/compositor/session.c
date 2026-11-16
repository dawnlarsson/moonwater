/*
        The session around the desktop: a machine that slept and woke, the kernel's log, the settings under /root read
        (the look, the theme and its inks, the hand, the taskbar, the input devices' settings) and published to the
        table, the input devices found and opened as they come, and the doors: the threads that wait on the programs'
        knocks and the posts the compositor sends a row.

        What is read here is what a setting says, not what a key or the hand means (keymap.c, pointer.c, input.c) or what
        a window does (manage.c). It reads the compositor's state and the parts before it; what it asks of manage.c and
        input.c (the keyboard on the screen, the clipboard's and the GPU's settings, touch and the prediction) is
        declared before the call. Called by compositor.c's startup and loop (the settings, the devices, the doors, the
        wake), by kms.c, manage.c and input.c (the log, a setting's word, a door's question, a post), and for the inks by
        damage.c and the taskbar (ui/taskbar.c), through compositor.c's declarations of them.
*/

/*
        What a sleep does to the screen. The card is left to the compositor and
        nothing tells it the machine slept, so it works it out from the clocks:
        CLOCK_BOOTTIME runs through a sleep and CLOCK_MONOTONIC does not, and
        the first thing that wakes the loop afterwards (the key that woke the
        machine, a power button, a lid) finds them further apart than they
        were. A real card's driver restores the state it saved, and a panel
        that state says is on and the hardware is not showing is answered by a
        plane update as if nothing had happened; only an active change makes it
        program the pipe again, which is turning the crtc off and on. That is
        done once, at half a second, where the driver has settled, and not for
        a virtual card, which has no such drift and does not always get its
        host window back from an off.
*/
static int compositor_virtual(void)
{
        static const char *const guests[] = {"bochs-drm", "virtio_gpu", "qxl", "vmwgfx", "cirrus-qemu", "hyperv_drm", "vkms"};

        return compositor_driver_in(guests, sizeof(guests) / sizeof(guests[0]));
}

// How far ahead of the clock that stops in a sleep the one that does not is, in nanoseconds.
static positive compositor_slept_clock(void)
{
        timespec boot = {0, 0}, mono = {0, 0};
        long long ahead;

        clock_gettime(CLOCK_BOOTTIME, address_of boot);
        clock_gettime(CLOCK_MONOTONIC, address_of mono);
        ahead = ((long long)boot.tv_sec - (long long)mono.tv_sec) * 1000000000ll +
                ((long long)boot.tv_nsec - (long long)mono.tv_nsec);
        return ahead < 0 ? 0 : (positive)ahead;
}

static void compositor_resume(int cycle)
{
        if (compositor.card < 0)
                return;

        // Every screen's pipe, each showing its rectangle of the space.
        for (unsigned int at = 0; cycle && !compositor_virtual() && at < compositor.outputs; at++)
        {
                struct compositor_output *output = &compositor.output[at];
                struct compositor_crtc off = {0, 0, output->crtc, 0, 0, 0, 0, 0, {0}};
                struct compositor_crtc on = {(unsigned long)address_of output->connector, 1, output->crtc,
                                             output->fb ? output->fb : compositor.fb,
                                             output->turned == 2 ? 0 : (unsigned int)compositor.screen[at][0],
                                             output->turned == 2 ? 0 : (unsigned int)compositor.screen[at][1],
                                             0, 1, output->mode};

                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of off);
                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of on);
        }

        // What the plane wears and where it was is whatever the driver put back: say it all again.
        compositor.cursor.plane.scale = 0;
        compositor.mirror_stale = 1;
        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_cursor_move();
}

// A line in the machine's log: the compositor's own output goes where its parent put it.
static void compositor_kmsg(string_address text, positive value, string_address tail)
{
        p8 line[160];
        positive length = 0;
        bipolar handle;

        while (text[length] && length < 100)
        {
                line[length] = text[length];
                length++;
        }
        if (value)
                length += positive_into_string(line + length, value);
        for (positive at = 0; tail[at] && length < sizeof(line) - 1; at++)
                line[length++] = tail[at];
        line[length++] = '\n';

        handle = system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", O_WRONLY | O_CLOEXEC);
        if (handle < 0)
                return;
        system_write_once(handle, line, length);
        system_close(handle);
}

// Looked at whenever the loop wakes: whether the machine slept since it last did.
static void compositor_wake_check(positive now)
{
        positive asleep = compositor_slept_clock();

        if (asleep > compositor.asleep + COMPOSITOR_SLEPT_NS)
        {
                compositor_resume(0);
#ifdef MOONWATER_GPU_C
                compositor_gpu_resumed();
#endif
                compositor.wake_stage = 1;
                compositor.wake_at = now + COMPOSITOR_WAKE_FIRST_NS;
        }
        compositor.asleep = asleep;
}

// The redraws that follow a wake, when they are due.
static void compositor_wake_pass(positive now)
{
        if (!compositor.wake_stage || now < compositor.wake_at)
                return;

        compositor_resume(compositor.wake_stage == 1);
        if (++compositor.wake_stage == 3)
                compositor.wake_stage = 0;
        else
                compositor.wake_at = now + COMPOSITOR_WAKE_SECOND_NS;
}

/*
        How a file of /root (and the compositor's own of /run) is opened to be read: not through a link, which only root
        could plant there but which would have the root compositor read whatever it names, and without waiting, which a
        FIFO in a file's place would have it do for ever, the desktop with it. A file that is either is not there.
*/
#define COMPOSITOR_SETTING_OPEN (FILE_READ | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW)

static void compositor_osk_sync(void);
static void compositor_osk_unpan(int row);
static int compositor_osk_row(void);

// The layout the machine keeps, which `moonwater keyboard` writes; a machine that has not kept one has the US. A layout that
// changes is the on-screen keyboard's too: its letters are the layout's, so the keyboard is asked again (compositor_osk_sync
// writes the layout to the table) and woken to read it.
static void compositor_layout_read(void)
{
        char name[8] = {0};
        bipolar handle = system_open_at(AT_FDCWD, (string_address) "/root/keyboard", COMPOSITOR_SETTING_OPEN);
        int layout = -1;

        if (handle >= 0)
        {
                bipolar got = system_read_once(handle, name, sizeof(name) - 1);

                system_close(handle);
                for (bipolar at = 0; at < (got > 0 ? got : 0); at++)
                        if (name[at] == '\n' || name[at] == ' ')
                                name[at] = 0;
                layout = compositor_layout_index(name);
        }

        if (layout >= 0 && layout != compositor.keymap.layout)
        {
                compositor.keymap.layout = (unsigned char)layout;
                compositor.keymap.dead = 0;
                if (compositor.table)
                {
                        int row = compositor_osk_row();

                        compositor_osk_sync();
                        if (row >= 0)
                                compositor.woken |= 1u << row;
                }
        }
}

// A file's text, as far as room holds it, newlines and all: "" when the file is not there.
static void compositor_text(const char *path, char *text, unsigned int room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, COMPOSITOR_SETTING_OPEN);
        bipolar got = 0;

        if (handle >= 0)
        {
                got = system_read_once(handle, text, room - 1);
                system_close(handle);
        }
        text[got < 0 ? 0 : got] = 0;
}

// The word kept in a file of /root, cut at its first space or line end: empty when there is none.
static void compositor_word(const char *path, char *word, unsigned int room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, COMPOSITOR_SETTING_OPEN);
        bipolar got = 0, at = 0;

        if (handle >= 0)
        {
                got = system_read_once(handle, word, room - 1);
                system_close(handle);
        }
        while (at < got && word[at] != '\n' && word[at] != ' ' && word[at] != '\r')
                at++;
        word[at] = 0;
}

/*
        What `moonwater canvas background` and `moonwater canvas cursor` kept in /root: the desktop's ink,
        the pointer's size (a percent of the desktop's own for it, 80 when nothing is kept, and auto or 1 to
        4 kept before as the registry reads them) and its colours (light or dark, or the light it always
        was). Read when the compositor starts and again when a file of them is written; a word it does not
        know is the default, as if nothing were kept. What changed is drawn at once.
*/
static p8 compositor_setting_kept(const char *path, const char *word, long *value);

static void compositor_look_read(void)
{
        char word[16];
        long colour, sized = 0;
        int size, look = 0, style, squeeze = 25;
        long percent = 25;

        compositor_word(SETTING_BACKGROUND_PATH, word, sizeof(word));
        colour = word[0] ? window_colour_parse((string_address)word) : -1;
        if ((unsigned int)(colour < 0 ? COMPOSITOR_INK_DESKTOP : colour) != compositor_ground)
        {
                compositor_ground = (unsigned int)(colour < 0 ? COMPOSITOR_INK_DESKTOP : colour);
                compositor_damage(0, 0, compositor.width, compositor.height);
        }

        compositor_word(SETTING_CURSOR_SIZE_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_CURSOR_SIZE_PATH, word, &sized);
        size = sized < 50 ? 50 : sized > 400 ? 400 : (int)sized;
        compositor_word(SETTING_CURSOR_LOOK_PATH, word, sizeof(word));
        if (!string_compare((string_address)word, (string_address) "light"))
                look = 1;
        else if (!string_compare((string_address)word, (string_address) "dark"))
                look = 2;
        // The style: classic is the bitmaps, and anything else (dynamic, or nothing kept) is the dynamic circle.
        compositor_word(SETTING_CURSOR_STYLE_PATH, word, sizeof(word));
        style = string_compare((string_address)word, (string_address) "classic") ? 1 : 0;
        // The squeeze: what is kept, or the row's standard (25 percent) when nothing is, 0 to 50.
        compositor_word(SETTING_SQUEEZE_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_SQUEEZE_PATH, word, &percent);
        squeeze = percent < 0 ? 0 : percent > 50 ? 50 : (int)percent;

        if (size != compositor.cursor.size || look != compositor.cursor.look || style != compositor.cursor.style ||
            squeeze != compositor.cursor.squeeze)
        {
                compositor.cursor.size = size;
                compositor.cursor.look = look;
                compositor.cursor.style = style;
                compositor.cursor.squeeze = squeeze;
                // The plane wears the old one, and is painted again (a scale it is not at); a plane that is
                // to be hidden still is told so.
                compositor.cursor.plane.scale = 0;
                compositor_cursor_move();
        }
}

/*
        The theme of Canvas's own windows, as `moonwater canvas theme`, `canvas theme accent` and `canvas
        motion` keep it, into the window table's header (theme, accent, motion), with themed counted up and
        every window knocked when any of it changes; auto is light from 7 to 19 by the local clock, and is
        worked out again each minute (compositor_theme_tick). Every glass window is tinted to the theme's
        side from then on, its blur made again.
*/
static void compositor_theme_set(unsigned int theme, unsigned int accent, unsigned int motion)
{
        struct windows *table = compositor.table;

        if (!table || (table->theme == theme && table->accent == accent && table->motion == motion && table->themed))
                return;
        table->theme = theme;
        table->accent = accent;
        table->motion = motion;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
        if (compositor.theme_side != !theme)
        {
                compositor.theme_side = !theme;
                for (int row = 0; row < WINDOWS; row++)
                        if (compositor.glassy[row])
                                compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        for (int row = 0; row < WINDOWS; row++)
                if (table->pid[row])
                        compositor.woken |= 1u << row;
}

// Whether the local clock says day (SETTING_THEME_DAY_FROM to SETTING_THEME_DAY_UNTIL).
static unsigned int compositor_theme_day(void)
{
        timespec real = {0, 0};
        tm broken;
        time_t seconds;

        clock_gettime(CLOCK_REALTIME, address_of real);
        seconds = real.tv_sec;
        if (!localtime_r(&seconds, &broken))
                return 1;
        return broken.tm_hour >= SETTING_THEME_DAY_FROM && broken.tm_hour < SETTING_THEME_DAY_UNTIL;
}

/*
        The theme's inks for the compositor's own chrome, the minimal look kept (frameless, 8 px corners, a 1 px line and no
        shadow), so the line is what tells the window with the keys from the rest. A window without the keys has the theme's
        outline (a1a1aa on the light theme, 52525b on the dark, 2.2 to 1 on the dark default wallpaper, so a dark window does
        not vanish into it). The window with the keys has the primary, made to stand out COMPOSITOR_FOCUS_READ from that
        outline (compositor_line_focus): the light standard's is the kit's ink, 18181b, at 6.9 to 1. A tab's glass is tinted
        toward the theme's surface, and the tab of the window with the keys toward its primary. A table the compositor has not
        themed yet has the standard, as the kit has.
*/
#define COMPOSITOR_FOCUS_READ 300
struct compositor_chrome
{
        struct ui_theme roles;
        unsigned int dark; // the theme the roles are made for: UI_THEME_DARK, else light
        unsigned int lit, unlit; // the line of the window with the keys and of one without them, 0xrrggbb
};

/*
        The line of the window with the keys: the primary, taken a step at a time toward the far end of the theme's lightness
        (black on the light theme, white on the dark) till it stands out COMPOSITOR_FOCUS_READ from the outline, the line of a
        window without the keys; an accent that already does is kept. On the dark theme a primary brighter than it needs to
        be is then taken toward the window's paper while it still does, so the standard's white is a light grey, a7a7a7,
        and not a glare round every window it is on; the amber, ff8800, is kept as it is. The steps are of 5 percent, so the
        line ends just past the ratio with as much of the accent's colour as it can keep.
*/
static unsigned int compositor_line_focus(const struct ui_theme *roles, unsigned int dark)
{
        unsigned int unlit = roles->role[UI_OUTLINE] | 0xff000000u, paper = roles->role[UI_SURFACE] | 0xff000000u;
        unsigned int lit = roles->role[UI_PRIMARY] | 0xff000000u, pole = dark ? 0xffffffffu : 0xff000000u;

        for (int step = 0; step < 64 && ui_contrast(lit, unlit) < COMPOSITOR_FOCUS_READ; step++)
                lit = ui_mix(lit, 95, pole);
        for (int step = 0; dark && step < 64 && ui_contrast(ui_mix(lit, 95, paper), unlit) >= COMPOSITOR_FOCUS_READ; step++)
                lit = ui_mix(lit, 95, paper);
        return lit & 0xffffffu;
}

// The roles the chrome is drawn with, made by the kit (ui/ui.c) for the window table's theme and accent, made again
// when either changes. A table not themed yet has the standard: dark, with white.
static const struct compositor_chrome *compositor_chrome_now(void)
{
        static struct compositor_chrome chrome;
        static unsigned int made, made_dark, made_accent;
        struct windows address_to table = compositor.table;
        unsigned int themed = table && table->themed, dark = themed ? table->theme == UI_THEME_DARK : 1;
        unsigned int accent = themed ? table->accent & 0xffffffu : UI_ACCENT_STANDARD;

        if (!made || made_dark != dark || made_accent != accent)
        {
                ui_theme_make(&chrome.roles, dark, accent);
                chrome.dark = dark;
                chrome.unlit = chrome.roles.role[UI_OUTLINE] & 0xffffffu;
                chrome.lit = compositor_line_focus(&chrome.roles, dark);
                made = 1, made_dark = dark, made_accent = accent;
        }
        return &chrome;
}

// The line of the window with the keys (compositor_line_focus): the light standard's 18181b, the dark standard's a7a7a7.
static unsigned int compositor_line_lit(void)
{
        return compositor_chrome_now()->lit;
}

// The line of a window without the keys: the theme's outline, a1a1aa on the light theme and 52525b on the dark.
static unsigned int compositor_line_unlit(void)
{
        return compositor_chrome_now()->unlit;
}

static unsigned int compositor_ink_shadow(void)
{
        return compositor_chrome_now()->roles.role[UI_SHADOW] & 0xffffffu;
}

// The kit's selection is the bright accent at 28%: its colour is the snap's, its alpha is not (compositor_snap_ink).
static unsigned int compositor_ink_selection(void)
{
        return compositor_chrome_now()->roles.role[UI_SELECTION];
}

/*
        The snap's tint, read on the wallpaper it is over. The selection's colour at COMPOSITOR_SNAP_PERCENT is kept while its
        edge reads COMPOSITOR_SNAP_READ against the wallpaper (the dark wallpaper: white at 40 percent is 3.8 to 1). Where it
        does not, the colour is mixed a step at a time toward black or white, whichever stands out more from the wallpaper,
        and if no colour reads at that percent the tint is made more opaque, 10 percent at a time to
        COMPOSITOR_SNAP_PERCENT_MOST (a light wallpaper: the light theme's grey at 40 percent is 2.0 to 1 at best; at 50 it
        is 3.0 in the colour 262628). Made once for each wallpaper and selection.
*/
static unsigned int compositor_snap_ink(void)
{
        static unsigned int made_ground, made_selection, made_ink;
        unsigned int ground = compositor_ground | 0xff000000u, selection = compositor_ink_selection() | 0xff000000u;
        unsigned int pole, ink = 0;

        if (made_ink && made_ground == ground && made_selection == selection)
                return made_ink;
        pole = ui_contrast(0xff000000u, ground) >= ui_contrast(0xffffffffu, ground) ? 0xff000000u : 0xffffffffu;
        for (unsigned int percent = COMPOSITOR_SNAP_PERCENT; percent <= COMPOSITOR_SNAP_PERCENT_MOST && !ink; percent += 10)
        {
                unsigned int at = selection;

                for (int step = 0; step <= 48 && !ink; step++)
                {
                        if (ui_contrast(ui_mix(at, percent, ground), ground) >= COMPOSITOR_SNAP_READ)
                                ink = (at & 0xffffffu) | (percent * 255 + 50) / 100 << 24;
                        else
                                at = ui_mix(at, 95, pole);
                }
        }
        if (!ink)
                ink = (selection & 0xffffffu) | COMPOSITOR_SNAP_PERCENT_MOST * 255 / 100 << 24;
        made_ground = ground, made_selection = selection, made_ink = ink;
        return ink;
}

// The colour a tab's glass is tinted toward: the theme's surface, or its primary for the tab of the window with the keys.
static unsigned int compositor_tab_target(b32 lit)
{
        return compositor_chrome_now()->roles.role[lit ? UI_PRIMARY : UI_SURFACE] & 0xffffffu;
}

// A role of the theme the chrome is drawn in (ui_theme_make, for the table's theme and accent), 0xaarrggbb. The compositor's tray draws
// its status cells in these: the kit's own roles (ui_role) are the windows' and are only made in a window's process, so in this one they
// are all zero, which drew the notices dot and the microphone black on either theme.
static unsigned int compositor_ink_role(int role)
{
        return compositor_chrome_now()->roles.role[role];
}


// A setting the table keeps for every window, a field of it or two (other none for one), set to what was read: a change knocks every
// window (themed counts up), as a theme change does, and the same value knocks none. The callers have a table.
static void compositor_table_publish(unsigned int *field, unsigned int value, unsigned int *other, unsigned int other_value)
{
        if (*field == value && (!other || *other == other_value))
                return;
        *field = value;
        if (other)
                *other = other_value;
        __atomic_add_fetch(&compositor.table->themed, 1, __ATOMIC_RELEASE);
}

// The wheel's glide (ui_wheel_turn), as the table keeps it.
static void compositor_wheel_publish(unsigned int off)
{
        if (compositor.table)
                compositor_table_publish(&compositor.table->wheel_off, off, 0, 0);
}

// The caret's blink and width (ui.c's ui_caret_wave and ui_caret_thick), as the table keeps them, each one more than its place.
static void compositor_caret_publish(unsigned int speed, unsigned int thick)
{
        if (compositor.table)
                compositor_table_publish(&compositor.table->caret_speed, speed, &compositor.table->caret_thick, thick);
}

// The width of a lit edge or corner (compositor_lit_px): the lit edge is drawn again at the old width and at the new, so the
// pixels the old one drew are not left behind. Only the window the hand is on has one lit.
static void compositor_border_publish(unsigned int width)
{
        if (compositor.border == width)
                return;
        if (compositor.lit_row >= 0)
                compositor_damage_line(compositor.lit_row);
        compositor.border = width;
        if (compositor.lit_row >= 0)
                compositor_damage_line(compositor.lit_row);
}

// The language the windows speak (ui/ui.c), as the table keeps it.
static void compositor_language_publish(unsigned int code)
{
        if (compositor.table)
                compositor_table_publish(&compositor.table->language, code, 0, 0);
}

// The text's size in percent (ui.c's ui_em), as the table keeps it, 0 for 100.
static void compositor_text_publish(unsigned int percent)
{
        if (compositor.table)
                compositor_table_publish(&compositor.table->text_scale, percent, 0, 0);
}

// The scroll body's switches, as the table keeps them (scroll_off: WINDOW_SCROLL_FLIP, _STOPS, _STILL and _DRAG, _DRAG alone the standard) with the
// trackpad's speed in percent (scroll_speed).
static void compositor_scroll_publish(unsigned int off, unsigned int speed)
{
        if (compositor.table)
                compositor_table_publish(&compositor.table->scroll_off, off, &compositor.table->scroll_speed, speed);
}

// Whether a Scrolling switch is on for the word its file holds (the row's standard when it holds none, or none of its words). A switch row's
// value is the place of its word in "on off" (registry.c: setting_words_of), so on is 0 and off is 1, as every other reader of a switch has it
// (touch, predict touch, shake, acceleration). This was read as if on were 1: momentum and the rubber band, whose standard is on, were off in a
// desktop that had never set them (and, set on, off), and a release threw nothing and an end stopped dead.
static p8 compositor_switch_on(const char *path, const char *word)
{
        long value = 0;

        compositor_setting_kept(path, word, &value);
        return value == 0;
}

// The table's scroll_off (WINDOW_SCROLL_FLIP, _STOPS, _STILL and _DRAG) for the words of natural direction, momentum, the rubber band and the
// mouse's drag: nothing set is the standard (natural direction off: the OS's sign; momentum and the rubber band on, no bits; a mouse's drag
// of a list on, its one bit).
static unsigned int compositor_scroll_bits(const char *natural, const char *momentum, const char *rubber, const char *drag)
{
        return (compositor_switch_on(SETTING_SCROLL_NATURAL_PATH, natural) ? WINDOW_SCROLL_FLIP : 0) |
               (compositor_switch_on(SETTING_SCROLL_MOMENTUM_PATH, momentum) ? 0 : WINDOW_SCROLL_STOPS) |
               (compositor_switch_on(SETTING_SCROLL_RUBBER_PATH, rubber) ? 0 : WINDOW_SCROLL_STILL) |
               (compositor_switch_on(SETTING_SCROLL_DRAG_PATH, drag) ? WINDOW_SCROLL_DRAG : 0);
}

// The Scrolling group's five rows, read in one place: natural direction (off when nothing is kept), momentum and the rubber band (on), the
// mouse's drag (on), and the trackpad's speed (a percent: 100 when nothing is kept, or what is kept is none of the row's words), which
// compositor_speed takes for the pads.
static void compositor_scroll_read(void)
{
        char natural[16], momentum[16], rubber[16], drag[16], speed[16];
        long percent;

        compositor_word(SETTING_SCROLL_NATURAL_PATH, natural, sizeof(natural));
        compositor_word(SETTING_SCROLL_MOMENTUM_PATH, momentum, sizeof(momentum));
        compositor_word(SETTING_SCROLL_RUBBER_PATH, rubber, sizeof(rubber));
        compositor_word(SETTING_SCROLL_DRAG_PATH, drag, sizeof(drag));
        compositor_word(SETTING_TRACKPAD_SPEED_PATH, speed, sizeof(speed));
        compositor_setting_kept(SETTING_TRACKPAD_SPEED_PATH, speed, &percent);
        compositor.trackpad_speed = (unsigned int)percent;
        compositor_scroll_publish(compositor_scroll_bits(natural, momentum, rubber, drag), compositor.trackpad_speed);
}

/*
        The pace of every transition (`moonwater canvas motion speed`, ui.c's ui_paced) for the word its file holds: the row's
        standard, 115, when it holds none or none of the row's words. The compositor's own transitions take it at once, and the
        table carries it to every window, which a change knocks, as a theme change does.
*/
static void compositor_pace_set(const char *word)
{
        struct windows *table = compositor.table;
        long percent = UI_PACE_STANDARD;

        compositor_setting_kept(SETTING_MOTION_SPEED_PATH, word, &percent);
        ui_pace = percent < UI_PACE_LEAST || percent > UI_PACE_MOST ? UI_PACE_STANDARD : (unsigned int)percent;
        if (!table || table->motion_pace == ui_pace)
                return;
        table->motion_pace = ui_pace;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
}

static void compositor_theme_read(void)
{
        char word[16];
        long accent = -1, smooth = 0, blink = 0, thick = 0, text = 100, border = COMPOSITOR_BORDER_STANDARD;
        unsigned int motion;

        compositor_word(SETTING_THEME_PATH, word, sizeof(word));
        // Dark is the theme when nothing is kept (and when what is kept is none of the words).
        compositor.theme_word = string_equals((string_address)word, (string_address) "light") ? 0
                                : string_equals((string_address)word, (string_address) "auto") ? 2 : 1;
        compositor_word(SETTING_ACCENT_PATH, word, sizeof(word));
        if (word[0] && !string_equals((string_address)word, (string_address) "reset") && !string_equals((string_address)word, (string_address) "default"))
                accent = window_colour_parse((string_address)word);
        compositor_word(SETTING_MOTION_PATH, word, sizeof(word));
        motion = string_equals((string_address)word, (string_address) "reduced");
        compositor_word(SETTING_WHEEL_SMOOTH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_WHEEL_SMOOTH_PATH, word, &smooth);
        compositor_wheel_publish(smooth != 0);
        compositor_scroll_read();
        compositor_word(SETTING_CARET_BLINK_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_CARET_BLINK_PATH, word, &blink);
        compositor_word(SETTING_CARET_THICKNESS_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_CARET_THICKNESS_PATH, word, &thick);
        compositor_caret_publish((unsigned int)blink + 1, (unsigned int)thick + 1);
        // English when nothing is kept, and when what is kept is not a code (default).
        compositor_word(SETTING_LANGUAGE_PATH, word, sizeof(word));
        compositor_language_publish(windows_language_code(word, string_length((string_address)word)));
        // 100 percent when nothing is kept (the table says 0 then, as every window takes it).
        compositor_word(SETTING_TEXT_SCALE_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_TEXT_SCALE_PATH, word, &text);
        compositor_text_publish(text == 100 ? 0 : (unsigned int)text);
        // The lit edges' width, 1 to 4 (the standard when none is kept, which compositor_setting_kept gives as the row's standard).
        compositor_word(SETTING_BORDER_WIDTH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_BORDER_WIDTH_PATH, word, &border);
        compositor_border_publish((unsigned int)(border < 1 ? 1 : border > COMPOSITOR_BORDER_MOST ? COMPOSITOR_BORDER_MOST : border));
        // White is the accent when none is kept: white fills with the dark ink on them, on the dark theme's surfaces.
        compositor_theme_set(compositor.theme_word == 2 ? !compositor_theme_day() : compositor.theme_word,
                             accent < 0 ? 0xffffffu : (unsigned int)accent, motion);
        // The pace after the theme, so a window that takes the table's first count takes its theme with it.
        compositor_word(SETTING_MOTION_SPEED_PATH, word, sizeof(word));
        compositor_pace_set(word);
}

// Auto, again by the clock: a minute's wake is enough.
static void compositor_theme_tick(void)
{
        if (compositor.theme_word == 2 && compositor.table)
                compositor_theme_set(!compositor_theme_day(), compositor.table->accent, compositor.table->motion);
}

/*
        What the settings table keeps for the compositor (live is SETTING_LIVE_COMPOSITOR): the file of a
        row, as the row says it. A value is the row's standard when nothing is kept, and kept says that
        something is.
*/
static p8 compositor_setting_kept(const char *path, const char *word, long *value)
{
        const struct settings_row *row = setting_by_path((string_address)path);

        *value = 0;
        return row && setting_kept(row, (string_address)word, value);
}

static void compositor_clip_settings(void);
#ifndef SHELL_NO_GPU
static void compositor_gpu_settings(void);
#endif

// Whether a name in /root is the file of a row the compositor takes at once.
static p8 compositor_setting_file(const char *name)
{
        for (unsigned int at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].live == SETTING_LIVE_COMPOSITOR && settings_rows[at].path &&
                    string_equals((string_address)name, (string_address)settings_rows[at].path + sizeof("/root/") - 1))
                        return true;
        return false;
}

// A keyboard's repeat as the settings have it, when that is not what it has: delay, then period.
static void compositor_repeat_apply(unsigned int at)
{
        int want[2];

        if (!compositor.repeats[at] || compositor.inputs[at] < 0)
                return;
        compositor_repeat_target(compositor.repeat_kept, compositor.repeat_delay, compositor.repeat_rate,
                                 compositor.repeat_own[at], want);
        if (want[0] == compositor.repeat_now[at][0] && want[1] == compositor.repeat_now[at][1])
                return;
        if (system_control(compositor.inputs[at], COMPOSITOR_EVIOCSREP, want) == 0)
        {
                compositor.repeat_now[at][0] = want[0];
                compositor.repeat_now[at][1] = want[1];
        }
}

/*
        `moonwater canvas cursor shake` (and its delay), `moonwater keyboard repeat` (delay and rate) and
        `moonwater mouse` (sensitivity and acceleration), as the words their files hold (none is an empty
        word): taken at once. The defaults are what the compositor did before there were settings, to the
        bit; a repeat that is not kept leaves each keyboard the way it opened, and one that was kept and is
        put back is told to the keyboards as they were.
*/
static void compositor_input_settings_set(const char *shake, const char *shake_delay, const char *sensitivity,
                                          const char *acceleration, const char *repeat_delay, const char *repeat_rate)
{
        long value;
        int was_off = compositor.cursor.shake_off;

        compositor.cursor.shake_off = compositor_setting_kept(SETTING_SHAKE_PATH, shake, &value) && value == 1;
        compositor_setting_kept(SETTING_SHAKE_DELAY_PATH, shake_delay, &value);
        compositor.cursor.shake_ns = (positive)value * 1000000ull;
        compositor_setting_kept(SETTING_SENSITIVITY_PATH, sensitivity, &value);
        compositor.sensitivity = (int)value;
        compositor.flat = compositor_setting_kept(SETTING_ACCELERATION_PATH, acceleration, &value) && value == 1;

        compositor.repeat_kept = 0;
        if (compositor_setting_kept(SETTING_REPEAT_DELAY_PATH, repeat_delay, &value))
        {
                compositor.repeat_kept |= 1;
                compositor.repeat_delay = (int)value;
        }
        if (compositor_setting_kept(SETTING_REPEAT_RATE_PATH, repeat_rate, &value))
        {
                compositor.repeat_kept |= 2;
                compositor.repeat_rate = (int)value;
        }
        for (unsigned int at = 0; at < compositor.input_count; at++)
                compositor_repeat_apply(at);

        // A pointer that is large is no longer shaken for when shaking is turned off.
        if (compositor.cursor.shake_off && !was_off && (compositor.cursor.magnified || compositor.cursor.magnify))
        {
                compositor.cursor.magnified = compositor.cursor.magnify = 0;
                compositor_cursor_move();
        }
}

/*
        The taskbar's settings: its position, the screens it is on (primary, or all: a band on every screen), its
        battery and which way a side band's text faces (the place of the word kept among center and edge, the row's
        standard when none is), as the files in /root hold them. Read when the compositor starts and again when a file
        of them is written. Every band is damaged where it was as well as where it is now, a band on a screen that had
        none is given its memory, and when a band moved, came or went a window that fills a screen's room fills the
        room the bands leave now.
*/

// Where each band is, a rectangle each and the rest none: what a change of the settings is held against.
static void compositor_bands_where(int (*rects)[4])
{
        memory_zero(rects, sizeof(int) * 4 * WINDOWS_OUTPUTS);
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();

                taskbar_band(&view, &rects[compositor.band_at][0], &rects[compositor.band_at][1], &rects[compositor.band_at][2],
                             &rects[compositor.band_at][3]);
        }
        compositor.band_at = 0;
}

static void compositor_taskbar_settings_read(void)
{
        char position[16], display[16], tune[512], text[16], tray[SETTING_TRAY_MOST + 8], launcher[16], fullscreen[16], scale[16], padding[16];
        int was[WINDOWS_OUTPUTS][4], now[WINDOWS_OUTPUTS][4];
        long edge, percent = TASKBAR_SCALE_STANDARD, pixels = TASKBAR_PADDING_STANDARD;

        compositor_word(SETTING_TASKBAR_POSITION_PATH, position, sizeof(position));
        compositor_word(SETTING_TASKBAR_DISPLAY_PATH, display, sizeof(display));
        compositor_text(TASKBAR_TUNE_PATH, tune, sizeof(tune));
        compositor_text(SETTING_TASKBAR_TRAY_PATH, tray, sizeof(tray));
        // The standard tray in 12 hours, or with the date beside the clock, is a format of its own; a format of the person's own says
        // its hours and its date itself.
        if (!tray[0] || tray[0] == '\n' || string_equals((string_address)tray, (string_address) "default") ||
            string_equals((string_address)tray, (string_address)SETTING_TASKBAR_TRAY_STANDARD))
        {
                compositor_word(SETTING_CLOCK_FORMAT_PATH, text, sizeof(text));
                if (string_equals((string_address)text, (string_address) "12h"))
                        string_copy_bounded((p8 *)tray, (string_address)SETTING_TASKBAR_TRAY_STANDARD_12H, sizeof(tray));
                compositor_word(SETTING_DATE_FORMAT_PATH, text, sizeof(text));
                if (text[0] == 'y' || text[0] == 'd' || text[0] == 'm')
                {
                        if (!tray[0] || tray[0] == '\n' || string_equals((string_address)tray, (string_address) "default"))
                                string_copy_bounded((p8 *)tray, (string_address)SETTING_TASKBAR_TRAY_STANDARD, sizeof(tray));
                        string_append_bounded((string_address)tray,
                                              (string_address)(text[0] == 'y'   ? SETTING_TASKBAR_TRAY_DATE_YMD
                                                               : text[0] == 'd' ? SETTING_TASKBAR_TRAY_DATE_DMY
                                                                                : SETTING_TASKBAR_TRAY_DATE_MDY),
                                              sizeof(tray));
                }
        }
        compositor_word(SETTING_TASKBAR_TEXT_PATH, text, sizeof(text));
        compositor_setting_kept(SETTING_TASKBAR_TEXT_PATH, text, &edge);
        compositor_word(SETTING_TASKBAR_LAUNCHER_PATH, launcher, sizeof(launcher));
        // The standard when nothing is kept, or what is kept is no number or outside the row's range.
        compositor_word(SETTING_TASKBAR_SCALE_PATH, scale, sizeof(scale));
        compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, scale, &percent);
        compositor_word(SETTING_TASKBAR_PADDING_PATH, padding, sizeof(padding));
        compositor_setting_kept(SETTING_TASKBAR_PADDING_PATH, padding, &pixels);
        compositor_bands_where(was);
        compositor_tabs_changed();
        taskbar_settings_set(position, display, tune);
        taskbar_text_set(edge == 1);
        taskbar_tray_set(tray);
        // The row's standard is on: no file, or one that says default or a word that is none of the row's, has the tab; only "off" has none.
        taskbar_launcher_set(compositor_switch_on(SETTING_TASKBAR_LAUNCHER_PATH, launcher));
        taskbar_scale_set((int)percent);
        taskbar_padding_set((int)pixels);
        compositor_word(SETTING_TASKBAR_FULLSCREEN_PATH, fullscreen, sizeof(fullscreen));
        // The row's standard is on: no file, or a file that says default, hides the band as on does; only "off" keeps it.
        taskbar_hide_set(!string_equals((string_address)fullscreen, (string_address) "off"));
        compositor_band_room();
        compositor_tabs_changed();
        compositor_bands_where(now);
        if (memory_compare(was, now, sizeof(was)))
                compositor_room_follow();
}

static void compositor_touch_settings_read(void);
static void compositor_predict_read(void);

// `moonwater canvas cursor click`: on unless the file says off, read when the compositor starts and again when it is written.
static void compositor_click_read(void)
{
        char word[16];
        long value;

        compositor_word(SETTING_CURSOR_CLICK_PATH, word, sizeof(word));
        compositor.cursor.click_off = compositor_setting_kept(SETTING_CURSOR_CLICK_PATH, word, &value) && value == 1;
}

/*
        What a hand takes, as the files of their rows keep them (none, default or a word that is none of a row's is its standard):
        `moonwater mouse double click` and `canvas touch hold` (ui.c's ui_click_ns and ui_hold_ns, which the windows' tops, the tray and
        the touch recogniser take at once), `canvas terminal wheel` (the terminal's), `canvas cursor shake sensitivity` (compositor_shake)
        and `canvas border grip` (compositor_grip). The double click, the long press and the terminal's lines are the table's too, for
        every window, which a change of them knocks, as a theme change does.
*/
static void compositor_hand_set(const char *click, const char *hold, const char *wheel, const char *feel, const char *grip)
{
        struct windows *table = compositor.table;
        long value;
        unsigned int click_ms, hold_ms, lines;

        compositor_setting_kept(SETTING_DOUBLE_CLICK_PATH, click, &value);
        click_ms = value >= UI_CLICK_LEAST && value <= UI_CLICK_MOST ? (unsigned int)value : (unsigned int)(UI_CLICK_NS / UI_MS);
        compositor_setting_kept(SETTING_TOUCH_HOLD_PATH, hold, &value);
        hold_ms = value >= UI_HOLD_LEAST && value <= UI_HOLD_MOST ? (unsigned int)value : (unsigned int)(UI_HOLD_NS / UI_MS);
        compositor_setting_kept(SETTING_TERMINAL_WHEEL_PATH, wheel, &value);
        lines = value >= 1 && value <= 10 ? (unsigned int)value : 3;
        // The words are low, normal and high; the shake's feel is 0 for normal (what a zeroed compositor is), 1 low and 2 high.
        compositor_setting_kept(SETTING_SHAKE_FEEL_PATH, feel, &value);
        compositor.shake.feel = value == 0 ? 1 : value == 2 ? 2 : 0;
        compositor_setting_kept(SETTING_BORDER_GRIP_PATH, grip, &value);
        compositor.grip = value >= 2 && value <= 16 ? (int)value : COMPOSITOR_GRIP;
        ui_click_ns = (b64)click_ms * UI_MS, ui_hold_ns = (b64)hold_ms * UI_MS;
        if (!table || (table->click_ms == click_ms && table->hold_ms == hold_ms && table->wheel_lines == lines))
                return;
        table->click_ms = click_ms, table->hold_ms = hold_ms, table->wheel_lines = lines;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
}

static void compositor_hand_read(void)
{
        char words[5][16];
        static const char *const paths[5] = {SETTING_DOUBLE_CLICK_PATH, SETTING_TOUCH_HOLD_PATH, SETTING_TERMINAL_WHEEL_PATH,
                                             SETTING_SHAKE_FEEL_PATH, SETTING_BORDER_GRIP_PATH};

        for (unsigned int at = 0; at < 5; at++)
                compositor_word(paths[at], words[at], sizeof(words[at]));
        compositor_hand_set(words[0], words[1], words[2], words[3], words[4]);
}

// Read when the compositor starts and again when a file of them is written.
static void compositor_input_settings_read(void)
{
        char words[6][16];
        static const char *const paths[6] = {SETTING_SHAKE_PATH, SETTING_SHAKE_DELAY_PATH, SETTING_SENSITIVITY_PATH,
                                             SETTING_ACCELERATION_PATH, SETTING_REPEAT_DELAY_PATH, SETTING_REPEAT_RATE_PATH};

        for (unsigned int at = 0; at < 6; at++)
                compositor_word(paths[at], words[at], sizeof(words[at]));
        compositor_input_settings_set(words[0], words[1], words[2], words[3], words[4], words[5]);
        compositor_taskbar_settings_read();
        compositor_touch_settings_read();
        compositor_predict_read();
        compositor_click_read();
        compositor_hand_read();
}

_Static_assert(COMPOSITOR_INPUTS == WINDOWS_INPUTS, "an input slot is a row of the table's");

// What moonwater debug is shown of slot at: the device in it, or none, its counts started again. Once the table is made.
static void compositor_input_publish(unsigned int at)
{
        struct windows *table = compositor.table;

        if (!table)
                return;
        table->input_number[at] = at < compositor.input_count && compositor.inputs[at] >= 0 ? compositor.device_number[at] : -1;
        table->input_kinds[at] = compositor.input_kinds[at];
        memory_copy(table->input_id[at], compositor.input_id[at], sizeof(table->input_id[at]));
        memory_copy(table->input_name[at], compositor.input_name[at], sizeof(table->input_name[at]));
        table->input_wheel_read[at] = table->input_wheel_used[at] = table->input_dropped[at] = 0;
        table->input_wheel_last[at] = 0;
}

/*
        Every keyboard and pointer evdev has, now and as they come. Read
        beside Canvas's own handler, not grabbed from it: a grab would take
        Control-Alt-Backspace away from the kernel, and that chord is how a
        person gets out of a compositor that has stopped answering.

        The directory is watched before it is read, so a device that appears
        in between is in one or the other, and one that is in both is open
        once: the number in its name says which it is. A device that goes
        away is let go of where it is read (compositor_input_read) and its
        number is free again for the one that is plugged in next.
*/
static void compositor_space_retake(void);

// A touch screen as it is opened: no finger on it, and the range of its places, its slots' where it has slots.
static void compositor_touch_screen_open(unsigned int at, bipolar handle, int slotted)
{
        struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        struct compositor_absinfo x = {0}, y = {0};

        memory_zero(screen, sizeof(*screen));
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                screen->id[slot] = screen->was[slot] = -1;
        if (!compositor.touchscreen[at])
                return;
        screen->slots = slotted;
        system_control(handle, COMPOSITOR_EVIOCGABS(slotted ? COMPOSITOR_ABS_MT_POSITION_X : 0), address_of x);
        system_control(handle, COMPOSITOR_EVIOCGABS(slotted ? COMPOSITOR_ABS_MT_POSITION_Y : 1), address_of y);
        screen->range[0] = x.minimum, screen->range[1] = x.maximum;
        screen->range[2] = y.minimum, screen->range[3] = y.maximum;
        // The slot the kernel is on now, which its next events are about until it names another.
        if (slotted)
        {
                struct compositor_absinfo slot = {0};

                system_control(handle, COMPOSITOR_EVIOCGABS(COMPOSITOR_ABS_MT_SLOT), address_of slot);
                screen->slot = slot.value;
        }
}

/*
        Where the events being posted come from (WINDOW_EVENT_VIRTUAL). An input device is the machine's own when the node opened is
        a device the kernel hangs under a real bus: its device number (statx of the descriptor opened, so it is the node read and no
        other one that took its name since) resolved through /sys/dev/char lies under the kernel's devices and not under
        devices/virtual, where uinput and uhid make theirs. A program writes a uinput or uhid device's bus, vendor, name and phys, so
        none of those is asked; and a Bluetooth keyboard bluez brings up through uhid (Bluetooth LE) is virtual by the same rule, as
        nothing it cannot forge tells bluez's from any other program that can open /dev/uhid, where one through the kernel's hidp
        (classic Bluetooth) hangs under its hci and is the machine's. A laptop's lid, power and hotkey devices, its keyboard and pad
        hang under ACPI, the platform, i8042, USB or I2C, and a virtual machine's virtio-input and usb-tablet under the guest's PCI or
        USB: the machine's own, as the guest sees it. A link that cannot be read is not the machine's.
*/
static unsigned char compositor_input_physical[COMPOSITOR_INPUTS]; // the slots whose device was found to be the machine's own
static int compositor_post_physical; // set by compositor_input_read alone, for the span of its own posts (compositor_post)

// Whether a device's sysfs path (the link /sys/dev/char/MAJOR:MINOR resolves to, "../../devices/...") is the machine's own.
static int compositor_input_path_physical(const char *resolved)
{
        static const char devices[] = "devices/", virtual[] = "virtual/";

        if (!resolved)
                return 0;
        while (resolved[0] == '.' && resolved[1] == '.' && resolved[2] == '/')
                resolved += 3;
        for (unsigned int at = 0; at < sizeof(devices) - 1; at++)
                if (resolved[at] != devices[at])
                        return 0;
        resolved += sizeof(devices) - 1;
        for (unsigned int at = 0; at < sizeof(virtual) - 1; at++)
                if (resolved[at] != virtual[at])
                        return resolved[0] != 0;
        return 0;
}

// The device of an open input node, classified (compositor_input_path_physical): 1 the machine's own, 0 not or not known.
static int compositor_input_handle_physical(bipolar handle)
{
        p8 statx[256] = {0}, link[48] = "/sys/dev/char/", resolved[512];
        positive length = string_length(link);
        unsigned int major, minor;
        long got;

        // STATX_TYPE, AT_EMPTY_PATH: stx_rdev_major and stx_rdev_minor at 128 and 132.
        if (compositor_call(syscall(statx), (positive)handle, (positive) "", 0x1000, 1, (positive)statx, 0) != 0)
                return 0;
        memory_copy(address_of major, statx + 128, 4);
        memory_copy(address_of minor, statx + 132, 4);
        length += positive_into_string(link + length, major);
        link[length++] = ':';
        length += positive_into_string(link + length, minor);
        link[length] = 0;
        got = compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive)link, (positive)resolved, sizeof(resolved) - 1, 0, 0);
        if (got <= 0)
                return 0;
        resolved[got] = 0;
        return compositor_input_path_physical((const char *)resolved);
}

static void compositor_input_add(unsigned int number)
{
        p8 path[32] = "/dev/input/event";
        unsigned long kinds = 0;
        p8 keys[96] = {0};
        unsigned long relative = 0, absolute = 0;
        positive length = string_length(path);
        bipolar handle;
        unsigned int at = 0;

        if (number >= COMPOSITOR_DEVICES || compositor.devices >> number & 1)
                return;

        while (at < compositor.input_count && compositor.inputs[at] >= 0)
                at++;
        if (at == COMPOSITOR_INPUTS)
                return;

        length += positive_into_string(path + length, number);
        path[length] = 0;

        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NONBLOCK);
        if (handle < 0)
                return;

        system_control(handle, COMPOSITOR_EVIOCGBIT(0, 8), address_of kinds);
        system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_KEY, 96), keys);
        system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_REL, 8), address_of relative);
        system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_ABS, 8), address_of absolute);

        int keyboard = (kinds >> COMPOSITOR_EV_KEY & 1) &&
                       (keys[COMPOSITOR_KEY_A / 8] >> (COMPOSITOR_KEY_A % 8) & 1);
        int mouse = (kinds >> COMPOSITOR_EV_REL & 1) && (relative & 3) == 3;
        int tablet = (kinds >> COMPOSITOR_EV_ABS & 1) && (absolute & 3) == 3;
        // A power button and a lid are not typed on, and a machine may be woken by either.
        int wakes = ((kinds >> COMPOSITOR_EV_KEY & 1) && (keys[COMPOSITOR_KEY_POWER / 8] >> (COMPOSITOR_KEY_POWER % 8) & 1)) ||
                    (kinds >> COMPOSITOR_EV_SW & 1);
        unsigned int properties = 0;
        /*
                A screen fingers touch: INPUT_PROP_DIRECT, slots (ABS_MT_POSITION_X and Y) or a place and BTN_TOUCH, and
                no pen, whose screen is a tablet's as it always was. A virtual machine's (QEMU's virtio-multitouch) has
                slots and no ABS_X at all, and was not read.
        */
        int slotted = (kinds >> COMPOSITOR_EV_ABS & 1) && (absolute >> COMPOSITOR_ABS_MT_POSITION_X & 1) &&
                      (absolute >> COMPOSITOR_ABS_MT_POSITION_Y & 1);
        int touchscreen;

        system_control(handle, COMPOSITOR_EVIOCGPROP(4), address_of properties);
        touchscreen = (properties & 2) && !(keys[COMPOSITOR_BTN_TOOL_PEN / 8] >> (COMPOSITOR_BTN_TOOL_PEN % 8) & 1) &&
                      (slotted || (tablet && (keys[COMPOSITOR_BTN_TOUCH / 8] >> (COMPOSITOR_BTN_TOUCH % 8) & 1)));

        if (!keyboard && !mouse && !tablet && !wakes && !touchscreen)
        {
                system_close(handle);
                return;
        }

        compositor.absolute_x[at][0] = compositor.absolute_x[at][1] = 0;
        compositor.absolute_y[at][0] = compositor.absolute_y[at][1] = 0;
        compositor.held[at] = 0;
        compositor.wakeonly[at] = (unsigned char)(!keyboard && !mouse && !tablet && !touchscreen);
        // A wheel that reports in 120ths says so, and its detents are then not counted twice.
        compositor.hires[at] = (unsigned char)((relative >> COMPOSITOR_REL_WHEEL_HI & 1) |
                                               (relative >> COMPOSITOR_REL_HWHEEL_HI & 1) << 1);
        compositor.wheel_at[at] = compositor.wheel_stream_at[at] = 0;
        compositor.wheel_run[at] = 0, compositor.wheel_streamed[at] = 0;
        compositor.wheel_rest[at][0] = compositor.wheel_rest[at][1] = 0;
        compositor.stream_hist_n[at] = 0;
        // What it is, for moonwater debug: a device that is not what this took it for says so there. A pad that
        // counts the fingers on it and is no screen (INPUT_PROP_DIRECT is a screen's mark) is a touchpad.
        {
                unsigned int kinds;

                compositor.touchpad[at] = (unsigned char)(tablet && !(properties & 2) &&
                                                          (keys[COMPOSITOR_BTN_TOOL_FINGER / 8] >> (COMPOSITOR_BTN_TOOL_FINGER % 8) & 1));
                compositor.pad[at] = (struct compositor_pad){0};
                compositor.touchscreen[at] = (unsigned char)touchscreen;
                compositor_touch_screen_open(at, handle, slotted);
                kinds = (keyboard ? WINDOWS_INPUT_KEYBOARD : 0) | (mouse ? WINDOWS_INPUT_MOUSE : 0) |
                        (tablet && !compositor.touchpad[at] && !touchscreen ? WINDOWS_INPUT_TABLET : 0) |
                        (compositor.wakeonly[at] ? WINDOWS_INPUT_WAKES : 0);
                kinds |=(relative >> COMPOSITOR_REL_WHEEL & 1 ? WINDOWS_INPUT_WHEEL : 0) |
                         (relative >> COMPOSITOR_REL_WHEEL_HI & 1 ? WINDOWS_INPUT_WHEEL_HI : 0) |
                         (relative >> COMPOSITOR_REL_HWHEEL & 1 ? WINDOWS_INPUT_HWHEEL : 0) |
                         (relative >> COMPOSITOR_REL_HWHEEL_HI & 1 ? WINDOWS_INPUT_HWHEEL_HI : 0) |
                         (keys[COMPOSITOR_BTN_LEFT / 8] >> (COMPOSITOR_BTN_LEFT % 8) & 1 ? WINDOWS_INPUT_BUTTONS : 0) |
                         (keys[COMPOSITOR_BTN_TOUCH / 8] >> (COMPOSITOR_BTN_TOUCH % 8) & 1 ? WINDOWS_INPUT_TOUCH : 0) |
                         (keys[COMPOSITOR_BTN_TOOL_FINGER / 8] >> (COMPOSITOR_BTN_TOOL_FINGER % 8) & 1 ? WINDOWS_INPUT_FINGERS : 0) |
                         (absolute >> COMPOSITOR_ABS_MT_POSITION_X & 1 ? WINDOWS_INPUT_SLOTS : 0) |
                         (properties & 1 ? WINDOWS_INPUT_POINTER : 0) | (properties & 2 ? WINDOWS_INPUT_DIRECT : 0);
                compositor.input_kinds[at] = kinds;
                memory_zero(compositor.input_id[at], sizeof(compositor.input_id[at]));
                memory_zero(compositor.input_name[at], sizeof(compositor.input_name[at]));
                system_control(handle, COMPOSITOR_EVIOCGID, compositor.input_id[at]);
                system_control(handle, COMPOSITOR_EVIOCGNAME(WINDOWS_INPUT_NAME - 1), compositor.input_name[at]);
        }
        // The events are stamped by the clock the speed of the hand is measured on.
        {
                int monotonic = 1;

                system_control(handle, COMPOSITOR_EVIOCSCLOCKID, address_of monotonic);
        }

        if (tablet)
        {
                struct compositor_absinfo x = {0}, y = {0};

                system_control(handle, COMPOSITOR_EVIOCGABS(0), address_of x);
                system_control(handle, COMPOSITOR_EVIOCGABS(1), address_of y);
                compositor.absolute_x[at][0] = x.minimum;
                compositor.absolute_x[at][1] = x.maximum;
                compositor.absolute_y[at][0] = y.minimum;
                compositor.absolute_y[at][1] = y.maximum;
                // A touchpad's units to a millimetre, which a finger's moves are measured in.
                compositor.pad[at].per_mm_x = x.resolution > 0 ? x.resolution
                                              : ((long)x.maximum - x.minimum) / COMPOSITOR_PAD_WIDTH_MM > 0
                                                  ? (int)(((long)x.maximum - x.minimum) / COMPOSITOR_PAD_WIDTH_MM)
                                                  : 1;
                compositor.pad[at].per_mm_y = y.resolution > 0 ? y.resolution : compositor.pad[at].per_mm_x;
                compositor.pad[at].next_x = compositor.pad[at].at_x = x.value;
                compositor.pad[at].next_y = compositor.pad[at].at_y = y.value;
        }

        compositor.inputs[at] = (int)handle;
        compositor.device_number[at] = (int)number;
        compositor_input_physical[at] = (unsigned char)compositor_input_handle_physical(handle);
        // A keyboard whose repeat can be asked is told what the settings keep, now and when they change.
        compositor.repeats[at] = 0;
        if (keyboard && system_control(handle, COMPOSITOR_EVIOCGREP, compositor.repeat_own[at]) == 0)
        {
                compositor.repeats[at] = 1;
                compositor.repeat_now[at][0] = compositor.repeat_own[at][0];
                compositor.repeat_now[at][1] = compositor.repeat_own[at][1];
                compositor_repeat_apply(at);
        }
        compositor.devices |= 1ul << number;
        compositor.input_count = at + 1 > compositor.input_count ? at + 1 : compositor.input_count;
        compositor_input_publish(at);
}

static void compositor_inputs_open(void)
{
        // IN_CREATE | IN_MOVED_TO on /dev/input, non-blocking and closed on exec.
        compositor.watch = (int)compositor_call(syscall(inotify_init1), COMPOSITOR_CLOEXEC | COMPOSITOR_NONBLOCK,
                                                0, 0, 0, 0, 0);
        if (compositor.watch >= 0 &&
            compositor_call(syscall(inotify_add_watch), (positive)compositor.watch,
                            (positive) "/dev/input", COMPOSITOR_IN_CREATE | COMPOSITOR_IN_MOVED_TO,
                            0, 0, 0) < 0)
        {
                system_close(compositor.watch);
                compositor.watch = -1;
        }

        // The keyboard setting is kept in /root/keyboard, written when it is changed.
        if (compositor.watch >= 0)
                compositor_call(syscall(inotify_add_watch), (positive)compositor.watch, (positive) "/root",
                                COMPOSITOR_IN_CLOSE_WRITE | COMPOSITOR_IN_MOVED_TO | COMPOSITOR_IN_CREATE,
                                0, 0, 0);

        for (unsigned int number = 0; number < COMPOSITOR_DEVICES; number++)
                compositor_input_add(number);
}

static void compositor_osk_setting_read(void);

// What appeared in /dev/input since the last look: a name that is eventN is a device;
// and a keyboard written in /root is a layout that was changed.
static void compositor_inputs_watch(void)
{
        for (;;)
        {
                // struct inotify_event: wd, mask, cookie, len, and len bytes of name.
                unsigned int buffer[72]; // one event with the longest name: 16 bytes, and NAME_MAX + 1 after them
                long got = compositor_call(syscall(read), (positive)compositor.watch, (positive)buffer,
                                           sizeof(buffer), 0, 0, 0);

                for (long at = 0; at + 16 <= got;)
                {
                        const char *name = (const char *)(buffer + (at + 16) / 4);
                        unsigned int length = buffer[(at + 12) / 4], number = 0;

                        if (at + 16 + (long)length > got)
                                break;

                        if (compositor_setting_file(name) || string_equals((string_address)name, (string_address)TASKBAR_TUNE_NAME) ||
                            string_equals((string_address)name, (string_address)SETTING_CANVAS_DISPLAYS_PATH + sizeof("/root/") - 1))
                        {
                                compositor_clip_settings();
#ifndef SHELL_NO_GPU
                                compositor_gpu_settings();
#endif
                                compositor_look_read();
                                compositor_theme_read();
                                compositor_input_settings_read();
                                compositor_osk_setting_read();
                                // The arrangement and the displays: the screens laid again, which probes them, so only when one
                                // of those files is the one written.
                                if (string_equals((string_address)name,
                                                  (string_address)SETTING_CANVAS_ARRANGE_PATH + sizeof("/root/") - 1) ||
                                    string_equals((string_address)name,
                                                  (string_address)SETTING_CANVAS_DISPLAYS_PATH + sizeof("/root/") - 1))
                                        compositor_space_retake();
                        }
                        else if (length > 8 && !string_compare_max((string_address)name, (string_address) "keyboard", 8) && !name[8] &&
                                 !compositor.layout_given)
                                compositor_layout_read();
                        else if (length > 5 && name[0] == 'e' && name[1] == 'v' && name[2] == 'e' &&
                            name[3] == 'n' && name[4] == 't')
                        {
                                for (unsigned int digit = 5; digit < length && name[digit] >= '0' &&
                                                             name[digit] <= '9' && number < 1000;
                                     digit++)
                                        number = number * 10 + (unsigned int)(name[digit] - '0');
                                compositor_input_add(number);
                        }

                        at += 16 + (long)length;
                }

                if (got <= 0)
                        return;
        }
}

/*
        /root read again, whole, at SIGUSR1: the desktop is started before the machine's disks are known (the machine
        script's desktop_early), on the image's own /root, and a disk taken after that is another directory bound over
        the name, which the watch on the first hears nothing of. The session sends the signal when the disk is taken
        (desktop_settled): the name is watched again and every setting the start read from it is read as a change of its
        file would have it read, the screens laid again by the disk's displays and arrangement. What the disk does not
        say stays as the first frame had it, so a disk with no settings of its own changes nothing on the screen. All
        on the loop's thread, as the watch's own reads are.
*/
static void compositor_root_again(void)
{
        if (compositor.watch >= 0)
                compositor_call(syscall(inotify_add_watch), (positive)compositor.watch, (positive) "/root",
                                COMPOSITOR_IN_CLOSE_WRITE | COMPOSITOR_IN_MOVED_TO | COMPOSITOR_IN_CREATE,
                                0, 0, 0);
        compositor_clip_settings();
#ifndef SHELL_NO_GPU
        compositor_gpu_settings();
#endif
        compositor_look_read();
        compositor_theme_read();
        compositor_input_settings_read();
        compositor_osk_setting_read();
        if (!compositor.layout_given)
                compositor_layout_read();
        compositor_space_retake();
}

/*
        An event into a row's columns. The compositor writes head and the
        program tail, and a full ring keeps what came first; a motion behind
        a motion the program has not read yet replaces it. tail is the
        program's, so it is read once and only believed as far as the ring
        is long. Whether it was put in the ring, or joined to the one before.

        A wheel behind a wheel the program has not read, turned the same way
        round its axis, is added to it once the program is half a ring
        behind: a swipe is sixty to a hundred and fifty events a second, a
        wheel that reports in 120ths sends eight to a notch, and a program
        that is busy for a moment had the rest of the swipe cut off where the
        ring filled. A program takes an event's number and leaves 0 where it
        was (windows_event), so a turn is added only to a number that is
        still there, in one compare and swap: one the program takes before
        the add is not added to, and the turn goes in an event of its own,
        and one it takes after has the turn in it. Below half a ring every
        turn is an event of its own, as it always was; and an event holds at
        most COMPOSITOR_WHEEL_JOIN, since a program bounds what one event may
        do (a terminal scrolls a screen at most).
*/
static void compositor_clip_gesture(int row);

// The device stamp (CLOCK_MONOTONIC ns) of the device event being read, which the events posted for it carry (window_event.when);
// 0 for an event the device did not make (a timer's, a key of the loop's own). compositor_input_read sets it for each event.
static unsigned long long compositor_stamp_now;

// Whether the left button is down on a keyboard window (WINDOW_KEYBOARD): its key requests are read while it is (compositor_osk_read).
static unsigned char compositor_osk_held[WINDOWS];

static int compositor_post(int row, unsigned int type, unsigned int flags, int a, int b)
{
        struct windows *table = compositor.table;
        unsigned int head, used, last;

        if (row < 0)
                return 0;
        if (type == WINDOW_EVENT_BUTTON && a == COMPOSITOR_BTN_LEFT)
                compositor_osk_held[row] = (flags & WINDOW_KEY_DOWN) != 0;
        // A key pressed or a button pressed or let go is the person's: the clipboard lets the row copy and paste for a while.
        if ((type == WINDOW_EVENT_KEY && (flags & WINDOW_KEY_DOWN) && !(flags & WINDOW_KEY_REPEAT)) || type == WINDOW_EVENT_BUTTON)
                compositor_clip_gesture(row);

        // What a finger did says so: a program may scroll what a finger drags where a mouse would select.
        if (compositor.touching && type != WINDOW_EVENT_KEY)
                flags |= WINDOW_KEY_TOUCH;
        // A lead taken away is a place and no step of the hand's (WINDOW_KEY_SETTLE).
        if (compositor.retracting && type == WINDOW_EVENT_MOTION)
                flags |= WINDOW_KEY_SETTLE;
        // The right button of a long press says so, which a program may take as its own (WINDOW_KEY_HOLD).
        if (compositor.touch.holding && type == WINDOW_EVENT_BUTTON)
                flags |= WINDOW_KEY_HOLD;
        // Not read from a device found to be the machine's own, by the device's read itself: marked (WINDOW_EVENT_VIRTUAL). A pinch has no
        // such bit (it is its WINDOW_PINCH_END), and no event of another type is marked.
        if (!compositor_post_physical &&
            (type == WINDOW_EVENT_KEY || type == WINDOW_EVENT_BUTTON || type == WINDOW_EVENT_MOTION || type == WINDOW_EVENT_WHEEL))
                flags |= WINDOW_EVENT_VIRTUAL;
        head = table->event_head[row];
        used = head - __atomic_load_n(&table->event_tail[row], __ATOMIC_ACQUIRE);
        last = (head - 1) % WINDOWS_EVENTS;
        compositor.woken |= 1u << row;

        if (type == WINDOW_EVENT_WHEEL && used >= WINDOWS_EVENTS / 2 && used <= WINDOWS_EVENTS &&
            table->event_type[row][last] == WINDOW_EVENT_WHEEL && table->event_b[row][last] == b &&
            table->event_flags[row][last] == flags)
        {
                int held = __atomic_load_n(&table->event_a[row][last], __ATOMIC_ACQUIRE);

                if (held && (held < 0) == (a < 0) && (long)held + a <= COMPOSITOR_WHEEL_JOIN &&
                    (long)held + a >= -COMPOSITOR_WHEEL_JOIN &&
                    __atomic_compare_exchange_n(&table->event_a[row][last], &held, held + a, 0, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE))
                {
                        table->event_when[row][last] = compositor_stamp_now;
                        return 1;
                }
        }

        // The record in the row's ring (ring_post, src/lib.c): a motion over the last unread motion, so a program that is behind
        // has one place to catch up to; else in the cell after the last, the head stored once the cell is whole; and nothing
        // when the ring is full, which the program's tail may say whatever is true.
        {
                const address_any ring[7] = {table->event_type[row], table->event_flags[row], table->event_a[row], table->event_b[row],
                                             table->event_when[row], &table->event_head[row], &table->event_tail[row]};
                const p64 record[3] = {compositor_stamp_now, (p64)(unsigned int)a | (p64)(unsigned int)b << 32,
                                       (p64)(unsigned short)type | (p64)(unsigned short)flags << 16};

                return (int)ring_post(ring, WINDOWS_EVENTS, record, type == WINDOW_EVENT_MOTION);
        }
}

/*
        The doors' own threads. Every read, write and open of a pipe takes the pipe's lock first, whatever O_NONBLOCK says,
        and poll takes none: a process with an end of one of the compositor's FIFOs can hold that lock for as long as it
        likes (a splice into the pipe from a socket that has nothing to say holds it while it waits, and one out of it into
        a socket that is full), and no signal frees the one that waits for it. So the loop makes no read, write or open of
        a door, a wake, the clipboard's door or keys.fire: it polls the doors (no lock), takes what a knock says from the
        table at once, and hands the pipe's work to a thread of the row's -- the knock's bytes drained, a closed door opened
        again, a wake's byte written -- which may wait on a held lock with nothing else waiting for it; the clipboard's door
        and keys.fire have one thread between them (the last slot). A door is not polled while its thread has it, and the
        thread says it is done through an eventfd the loop polls: the row is read again then (a knock that came before
        the drain is taken), and a row whose door closed is let go once its new door is open, so a program that takes the
        row finds a door to open. With no thread (none could be made, or a check's compositor) it is done on the loop.
*/
#define COMPOSITOR_DOOR_DRAIN 1u
#define COMPOSITOR_DOOR_REOPEN 2u
#define COMPOSITOR_DOOR_WAKE 4u
#define COMPOSITOR_DOOR_LINES 8u
#define COMPOSITOR_DOOR_MISC WINDOWS // the clipboard's door and keys.fire
#define COMPOSITOR_DOOR_LINE 34

static struct
{
        thread *worker[WINDOWS + 1];
        int kick[WINDOWS + 1];          // eventfds, the loop to a thread
        unsigned int asked[WINDOWS + 1]; // COMPOSITOR_DOOR_* asked of a thread
        unsigned long busy;             // the slots whose door a thread has: not polled
        unsigned long drained, reopened; // what the threads have done, for the loop to take
        int done;                       // the eventfd a thread knocks the loop with, or -1
        int clip;                       // the clipboard's door, as the misc thread has it
        char lines[16][COMPOSITOR_DOOR_LINE];
        unsigned int line_head, line_tail; // the lines for keys.fire, the loop's to add and the thread's to say
} compositor_doors = {.done = -1};

static const char *compositor_keys_fire;

// Something asked of a slot's thread: false when it has none, and the loop does it itself.
static int compositor_door_ask(unsigned int slot, unsigned int what)
{
        unsigned long one = 1;

        if (!compositor_doors.worker[slot])
                return 0;
        __atomic_or_fetch(&compositor_doors.asked[slot], what, __ATOMIC_RELEASE);
        compositor_call(syscall(write), (positive)compositor_doors.kick[slot], (positive)&one, sizeof one, 0, 0, 0);
        return 1;
}

// A door's knocks read and thrown away, a bounded number a turn: what a writer that never stops costs.
static void compositor_door_drain(int handle)
{
        char bytes[64];

        for (int reads = 0; reads < 64 && system_read_once(handle, bytes, sizeof(bytes)) > 0; reads++)
                ;
}

// A thread done with what it was asked: the loop is told.
static void compositor_door_told(unsigned long *into, unsigned int slot)
{
        unsigned long one = 1;

        __atomic_or_fetch(into, 1ul << slot, __ATOMIC_RELEASE);
        compositor_call(syscall(write), (positive)compositor_doors.done, (positive)&one, sizeof one, 0, 0, 0);
}

// A row's thread: its wake written, its door drained, and its door opened again when it closed.
static fn compositor_door_run(address_any context)
{
        unsigned int row = (unsigned int)(positive)context;

        for (;;)
        {
                unsigned long count = 0;
                unsigned int asked;
                char byte = 0;

                // The kick waits in its read (compositor_doors_start), so a wake is this read's return and the write below.
                if (compositor_call(syscall(read), (positive)compositor_doors.kick[row], (positive)&count, sizeof count, 0, 0, 0) == -4)
                        continue;
                asked = __atomic_exchange_n(&compositor_doors.asked[row], 0u, __ATOMIC_ACQ_REL);
                if (asked & COMPOSITOR_DOOR_WAKE)
                        system_write_once(compositor.wake[row], &byte, 1);
                if (asked & (COMPOSITOR_DOOR_DRAIN | COMPOSITOR_DOOR_REOPEN))
                        compositor_door_drain(compositor.door[row]);
                if (asked & COMPOSITOR_DOOR_REOPEN)
                {
                        p8 name[48];

                        windows_door_name((char *)name, row, 0);
                        system_close(compositor.door[row]);
                        __atomic_store_n(&compositor.door[row], (int)system_open_at(AT_FDCWD, name, FILE_READ | O_NONBLOCK | O_CLOEXEC),
                                         __ATOMIC_RELEASE);
                        compositor_door_told(&compositor_doors.reopened, row);
                }
                else if (asked & COMPOSITOR_DOOR_DRAIN)
                        compositor_door_told(&compositor_doors.drained, row);
        }
}

// The last slot's thread: the clipboard's door drained, and the lines for keys.fire said, each a open, a write and a close.
static fn compositor_door_misc(address_any context)
{
        (void)context;
        for (;;)
        {
                unsigned long count = 0;
                unsigned int asked;

                if (compositor_call(syscall(read), (positive)compositor_doors.kick[COMPOSITOR_DOOR_MISC], (positive)&count, sizeof count, 0, 0, 0) == -4)
                        continue;
                asked = __atomic_exchange_n(&compositor_doors.asked[COMPOSITOR_DOOR_MISC], 0u, __ATOMIC_ACQ_REL);
                while (__atomic_load_n(&compositor_doors.line_tail, __ATOMIC_ACQUIRE) != __atomic_load_n(&compositor_doors.line_head, __ATOMIC_ACQUIRE))
                {
                        const char *line = compositor_doors.lines[compositor_doors.line_tail % 16];
                        bipolar handle = system_open_at(AT_FDCWD, (string_address)compositor_keys_fire,
                                                        O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);

                        if (handle >= 0)
                        {
                                system_write_once(handle, line, string_length((string_address)line));
                                system_close(handle);
                        }
                        __atomic_add_fetch(&compositor_doors.line_tail, 1u, __ATOMIC_RELEASE);
                }
                if (asked & COMPOSITOR_DOOR_DRAIN)
                {
                        compositor_door_drain(compositor_doors.clip);
                        compositor_door_told(&compositor_doors.drained, COMPOSITOR_DOOR_MISC);
                }
        }
}

// The threads, once the doors are open (clip, the clipboard's); a slot that can have none is the loop's, as every slot was.
static void compositor_doors_start(int clip)
{
        compositor_doors.clip = clip;
        compositor_doors.done = (int)compositor_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0); // EFD_CLOEXEC | EFD_NONBLOCK
        if (compositor_doors.done < 0)
                return;
        for (unsigned int slot = 0; slot <= WINDOWS; slot++)
        {
                // A kick is read where its thread waits (EFD_CLOEXEC alone: the read blocks), so a thread asked for a wake makes two
                // calls, the read that ends and the wake's write, where a poll, a read and the write were three.
                compositor_doors.kick[slot] = (int)compositor_call(syscall(eventfd2), 0, 0x80000, 0, 0, 0, 0);
                if (compositor_doors.kick[slot] >= 0)
                        compositor_doors.worker[slot] = thread_start(slot < WINDOWS ? compositor_door_run : compositor_door_misc,
                                                                     (address_any)(positive)slot);
        }
}

// Every row that was given something in this pass is knocked on, once: its byte written by its thread, or here.
static void compositor_knock(void)
{
        char byte = 0;

        for (int row = 0; row < WINDOWS; row++)
                if ((compositor.woken & (1u << row)) && !compositor_door_ask((unsigned int)row, COMPOSITOR_DOOR_WAKE))
                        system_write_once(compositor.wake[row], &byte, 1);
        compositor.woken = 0;
}

