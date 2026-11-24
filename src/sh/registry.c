/*
        The settings, in one table.

        Every setting a person can change by hand is a row here: its place
        in the settings window, the words of the moonwater command that read
        and set it, the line the help page and the README say of it, the
        file in /root that keeps it, and who applies it. The command
        (src/sh/host.c), the window (src/canvas/ui/settings.c) and the
        compositor (src/canvas/compositor.c) are all in one program, so each
        reads this table and none keeps a list of its own: one row added
        here is a verb that reads and sets it, a line on the help page, a
        row in the window and a file wipe leaves alone, and the lane
        that holds the README to the pages asks for its line there too.

        A plain row is read and set by the one generic path of the command
        (a bare verb says it, a word sets it, default or the row's reset word
        puts it back, a word that is none is refused with exit 1, a word
        too many is a usage page, a change needs root) and is kept by the
        word it was given, normalised, in the file of its path. A special
        row has a verb of its own in host.c, which the window and the pages
        still list from here: a layout the kernel is told of at once, a zone
        with its own tools, a switch that is a radio. An alias is a second
        row of the window for the verb of the plain or special row above it.

        The window reads a row as a verb's words after moonwater (read), the
        word after key in what the verb says, and sets it with the words of
        set and the value. A plain row's are made from its verb, and its
        key is its last word and a blank.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_SETTINGS_REGISTRY
#define MOONWATER_SETTINGS_REGISTRY

#define SETTINGS_SWITCH 1u
#define SETTINGS_CHOICE 2u
#define SETTINGS_FIELD 3u
#define SETTINGS_STEP 4u
#define SETTINGS_ACTION 5u
#define SETTINGS_INFO 7u    // a value that only says, and that a person cannot set
// The rows of a list a verb's output makes (a network in range, a device kept), which come and go as it is read again.
#define SETTINGS_NETWORK 8u
#define SETTINGS_DEVICE 9u
#define SETTINGS_NOTE 10u // what the verb said that is no row: no networks heard, and why the machine is not joined
#define SETTINGS_ADD 11u  // the last row of a list that adds one by its name
#define SETTINGS_SLIDER 12u // a whole number from the row's low to its high on a slider (the kit's UI_SLIDER); the verb sets any number in range
#define SETTINGS_PORT 14u   // a device the system sees, in the Devices list: a press allows or blocks it
#define SETTINGS_LINK 13u // a machine linked, as moonwater link says it: its name and what it was last heard as (not pressed)
#define SETTINGS_STARTUP 15u // a program the desktop starts with it, as desktop startup says it (setting_startup_entry): on or off, in its place
#define SETTINGS_LIST_ACTION 16u // a row of a list, after its add row, that is a row of the registry with no row of its own (.way): pressed, it runs that row's verb

// How a row's value is taken from the line it is on.
#define SETTINGS_LINE 1u  // the value is the rest of the line and not a word
#define SETTINGS_SPLIT 2u // what is set is the value's words, one each, and not one word
#define SETTINGS_TICK 4u  // the value changes by itself: it is asked again while its group is shown
#define SETTINGS_INK 8u   // a choice of named colours: the verb says rrggbb, and a name is what is shown and chosen
#define SETTINGS_BATTERIES 16u // a choice of the batteries the machine has: auto, each name from the kernel, and none
#define SETTINGS_LANGUAGE 32u  // a choice of the languages: the verb says the code and the menu shows the language's own name (ui/ui.c)
#define SETTINGS_WAITS 64u     // a verb that waits for another machine (a pairing, up to five minutes): its first line is shown while it waits, its last when it ends
#define SETTINGS_TRANSPORT 128u // a choice of how linked machines reach each other: network always, modem only while the audio modem is ready (settings.c knows those two words alone, so Connect over, which offers tcp and auto as well, does not set it)

// The Display rows of a screen (settings.c draws the map above them): the screen chosen, its rotation and refresh (the
// window table says what each is, and the verb sets it), and a note of who turns it.
#define SETTINGS_OUT_NONE 0u
#define SETTINGS_OUT_SCREEN 1u
#define SETTINGS_OUT_TURN 2u
#define SETTINGS_OUT_REFRESH 3u
#define SETTINGS_OUT_NOTE 4u
#define SETTINGS_OUT_SCALE 5u
#define SETTINGS_OUT_NOTICES 6u // the kernel's own notices: the value is their count, and the row's lines are each one (settings.c)
#define SETTINGS_OUT_SIZE 7u    // the screen's resolution: every size it lists, the preferred marked (the table's output_size)
#define SETTINGS_OUT_PRIMARY 8u // whether the screen is the primary: the table's index 0, the taskbar's and where a window opens

// The words of a verb, at most: canvas cursor shake delay.
#define SETTING_WORDS 4

// How the command meets a row.
#define SETTING_SPECIAL 0u // its verb is written by hand in host.c
#define SETTING_PLAIN 1u   // read and set by the generic path
#define SETTING_ALIAS 2u   // another row of the window for the verb of the row above it

// What a plain row's value is.
#define SETTING_WORD 0u   // a switch (on off) or one of its choices
#define SETTING_NUMBER 1u // a whole number from low to high, and the words of choices (off) besides
#define SETTING_COLOUR 2u // rrggbb, kept as #rrggbb

// Who takes a changed value at once. Zero is the common case, the compositor, so a row it takes needs no initialiser.
#define SETTING_LIVE_COMPOSITOR 0u // the compositor watches /root and reads the file again
#define SETTING_LIVE_NONE 1u       // the compositor does not watch the row's file; a row says it so

// What the files of the plain rows are called, in /root.
#define SETTING_BACKGROUND_PATH "/root/canvas.background"
#define SETTING_CURSOR_SIZE_PATH "/root/canvas.cursor.size"
#define SETTING_CURSOR_LOOK_PATH "/root/canvas.cursor.look"
#define SETTING_CURSOR_STYLE_PATH "/root/canvas.cursor.style"
#define SETTING_SQUEEZE_PATH "/root/canvas.cursor.squeeze"
#define SETTING_SHAKE_PATH "/root/canvas.cursor.shake"
#define SETTING_SHAKE_DELAY_PATH "/root/canvas.cursor.shake.delay"
#define SETTING_SHAKE_FEEL_PATH "/root/canvas.cursor.shake.sensitivity"
#define SETTING_CURSOR_CLICK_PATH "/root/canvas.cursor.click"
#define SETTING_REPEAT_DELAY_PATH "/root/keyboard.repeat.delay"
#define SETTING_ONSCREEN_PATH "/root/keyboard.onscreen"
#define SETTING_REPEAT_RATE_PATH "/root/keyboard.repeat.rate"
#define SETTING_SENSITIVITY_PATH "/root/mouse.sensitivity"
#define SETTING_DOUBLE_CLICK_PATH "/root/mouse.double.click"
#define SETTING_ACCELERATION_PATH "/root/mouse.acceleration"
// The aids of the keys and the pointer (src/canvas/compositor/access.c), the Accessibility group's.
#define SETTING_ACCESS_STICKY_PATH "/root/access.sticky"
#define SETTING_ACCESS_SLOW_PATH "/root/access.slow"
#define SETTING_ACCESS_BOUNCE_PATH "/root/access.bounce"
#define SETTING_ACCESS_MOUSEKEYS_PATH "/root/access.mousekeys"
#define SETTING_ACCESS_DWELL_PATH "/root/access.dwell"
#define SETTING_TASKBAR_POSITION_PATH "/root/taskbar.position"
#define SETTING_TASKBAR_DISPLAY_PATH "/root/taskbar.display"
#define SETTING_TASKBAR_SCALE_PATH "/root/taskbar.scale"
#define SETTING_TASKBAR_PADDING_PATH "/root/taskbar.padding"
#define SETTING_TASKBAR_TEXT_PATH "/root/taskbar.text"
#define SETTING_TASKBAR_LAUNCHER_PATH "/root/taskbar.launcher"
#define SETTING_TASKBAR_FULLSCREEN_PATH "/root/taskbar.fullscreen"
#define SETTING_TASKBAR_MOON_PATH "/root/taskbar.moon"
#define SETTING_TASKBAR_MOON_LOOK_PATH "/root/taskbar.moon.look"
#define SETTING_TASKBAR_MOON_SPIN_PATH "/root/taskbar.moon.spin"
#define SETTING_TASKBAR_GROUPED_PATH "/root/taskbar.tasks.grouped"
#define SETTING_TASKBAR_EDGE_PATH "/root/taskbar.edge"
#define SETTING_TASKBAR_TASKS_PATH "/root/taskbar.tasks"
#define SETTING_TASKBAR_NEWEST_PATH "/root/taskbar.newest"
#define SETTING_TASKBAR_ORDER_PATH "/root/taskbar.order"
#define SETTING_TASKBAR_CLOCK_PATH "/root/taskbar.clock"
#define SETTING_TASKBAR_CLOCK_BULGE_PATH "/root/taskbar.clock.bulge"
// A tray item's side of the tasks, one file each, in the order of taskbar.c's taskbar_item_of (its bit in taskbar.tray_before).
#define SETTING_TASKBAR_ITEM_NOTICES_PATH "/root/taskbar.item.notices"
#define SETTING_TASKBAR_ITEM_MIC_PATH "/root/taskbar.item.mic"
#define SETTING_TASKBAR_ITEM_SOUND_PATH "/root/taskbar.item.sound"
#define SETTING_TASKBAR_ITEM_BATTERY_PATH "/root/taskbar.item.battery"
#define SETTING_TASKBAR_ITEM_TEXT_PATH "/root/taskbar.item.text"
#define SETTING_TASKBAR_ITEM_CLOCK_PATH "/root/taskbar.item.clock"
// Each Scene's wallpaper (the compositor's wallpaper.c). The Scenes' words, a line a Scene (the screen's name, the Scene's number among its
// desktop Scenes from 1, and its word), written by the compositor alone: the command asks it to with a file of its own whose name starts
// SETTING_WALLPAPER_ASK (wallpaper.c's compositor_walls_asks). The pictures remembered, a name a line, the most recent first, written by the
// command alone. And their directory: a picture is NAME.png or NAME.jpg (a PNG or a JPEG, by what its bytes are) with its miniature
// NAME.mini beside it (setting_wallpaper_mini_read), and one of the feed's is curated/HEX.png or curated/HEX.jpg by its digest.
#define SETTING_WALLPAPER_SCENES_PATH "/root/canvas.wallpaper.scenes"
#define SETTING_WALLPAPER_PICTURES_PATH "/root/canvas.wallpaper.pictures"
// The feed's curated wallpapers the command verified (canvas wallpaper curated fetch): a line each, the digest of its picture in hex and its id,
// the compositor's to show; empty while no feed verifies (the key not pinned yet, an expired feed), so none is shown that is not the feed's.
#define SETTING_WALLPAPER_CURATED_PATH "/root/canvas.wallpaper.curated"
#define SETTING_WALLPAPER_DIR "/root/wallpapers"
#define SETTING_WALLPAPER_ASK_DIR "/run/moonwater"
#define SETTING_WALLPAPER_ASK "wallpaper.ask."
#define SETTING_WALLPAPER_WORD 96  // the bytes of a word, its end with them: auto, dark, white, picture:NAME or curated:HEX
#define SETTING_WALLPAPER_NAME 48  // and of a picture's name, letters, digits and dashes
#define SETTING_WALLPAPER_MINI 512 // a miniature's longer side, at the most
// A curated wallpaper's credit, as the list says it after its digest and id (a tab before each): its author and its licence, each at most
// SETTING_WALLPAPER_CREDIT bytes less one (the command cuts a longer one at a character), and its source page, the address of the page that
// carries its licence and history (setting_wallpaper_source), at most SETTING_WALLPAPER_SOURCE less one: a Commons title is 255 bytes at
// the most, 765 percent-encoded, and the page's address 40 more.
#define SETTING_WALLPAPER_CREDIT 128
#define SETTING_WALLPAPER_SOURCE 1024
#define SETTING_WINDOWS_REMEMBER_PATH "/root/canvas.windows.remember"
#define SETTING_TOUCH_PATH "/root/canvas.touch"
#define SETTING_TOUCH_LEFT_PATH "/root/canvas.touch.left"
#define SETTING_TOUCH_RIGHT_PATH "/root/canvas.touch.right"
#define SETTING_TOUCH_HOLD_PATH "/root/canvas.touch.hold"
#define SETTING_PREDICT_TOUCH_PATH "/root/canvas.predict.touch"
#define SETTING_CLIPBOARD_PATH "/root/canvas.clipboard"
#define SETTING_GPU_PATH "/root/canvas.gpu"
#define SETTING_GPU_TILED_PATH "/root/canvas.gpu.tiled"
#define SETTING_VRR_PATH "/root/canvas.vrr"
#define SETTING_CADENCE_PATH "/root/canvas.cadence"
#define SETTING_TEARING_PATH "/root/canvas.tearing"
#define SETTING_CLIPBOARD_HISTORY_PATH "/root/canvas.clipboard.history"
// The sound server's (src/audio/service.c reads the first three again twice a second and writes the volume and the mute back when
// a program changes them; the machine script reads the last when it starts the server).
#define SETTING_AUDIO_VOLUME_PATH "/root/audio.volume"
#define SETTING_AUDIO_MUTE_PATH "/root/audio.mute"
#define SETTING_AUDIO_DEVICE_PATH "/root/audio.device"
// The outputs that play what that one plays, besides it (the mixer's Outputs page): a name a line.
#define SETTING_AUDIO_MIRROR_PATH "/root/audio.mirror"
#define SETTING_AUDIO_SERVER_PATH "/root/audio.server"
// And the recording half, read the same way: whether programs may record at all (absent is on), the microphone they are given by name,
// and that microphone's volume and mute.
#define SETTING_AUDIO_CAPTURE_PATH "/root/audio.capture"
#define SETTING_AUDIO_SOURCE_PATH "/root/audio.source"
#define SETTING_AUDIO_SOURCE_VOLUME_PATH "/root/audio.source.volume"
#define SETTING_AUDIO_SOURCE_MUTE_PATH "/root/audio.source.mute"
#define SETTING_JINGLES_PATH "/root/canvas.jingles"
// Where the windows were last put, which the compositor writes (window.c's places) and is no plain row's file: `canvas
// windows` reads it and its forget empties it, and wipe keeps it with the settings (host_wipe_keep).
#define SETTING_WINDOWS_PLACES_PATH "/root/canvas.windows"
#define SETTING_CANVAS_ARRANGE_PATH "/root/canvas.arrange"
// Each screen's rotation, refresh and place, one line a screen (window.c's windows_displays_read); the compositor reads it
// and watches it, and a wipe keeps it with the arrangement.
#define SETTING_CANVAS_DISPLAYS_PATH "/root/canvas.displays"
// The tray's format (`taskbar tray`): text with {names} in it, and what it is when nothing is kept. The tray with the
// standard format is drawn as the tray always was: the battery's cell, then the clock's.
#define SETTING_TASKBAR_TRAY_PATH "/root/taskbar.tray"
#define SETTING_TASKBAR_TRAY_STANDARD "{battery}  {time.24h.hour}:{time.24h.min}"
// What the standard tray says when the clock is read in 12 hours (`time format 12h`): the hour as a 12 hour clock shows it, the minute, and am or pm.
#define SETTING_TASKBAR_TRAY_STANDARD_12H "{battery}  {time.12h.hour}:{time.24h.min} {time.ampm}"
#define SETTING_CLOCK_FORMAT_PATH "/root/time.format"
// The date beside the standard tray's clock (`time date ymd|dmy|mdy`; off, the standard, shows none): what each order adds to the format.
#define SETTING_DATE_FORMAT_PATH "/root/time.date"
#define SETTING_TASKBAR_CLOCK_24H "{time.24h.hour}:{time.24h.min}"             // the clock outside the moon, in 24 hours
#define SETTING_TASKBAR_CLOCK_12H "{time.12h.hour}:{time.24h.min} {time.ampm}" // and in 12
#define SETTING_TASKBAR_TRAY_DATE_YMD "  {date.year}-{date.month}-{date.day}"
#define SETTING_TASKBAR_TRAY_DATE_DMY "  {date.day}.{date.month}.{date.year}"
#define SETTING_TASKBAR_TRAY_DATE_MDY "  {date.month}/{date.day}/{date.year}"
#define SETTING_TRAY_MOST 96 // bytes of a format
// The programs pinned to the taskbar (`taskbar pinned`): one line, which setting_pins_valid says what is in it.
#define SETTING_TASKBAR_PINNED_PATH "/root/taskbar.pinned"
#define SETTING_PINS_MOST 2048 // bytes of the line
#define SETTING_PINS_ENTRIES 16 // programs it names
#define SETTING_PINS_WORDS 32   // words of an entry: its app_id, its executable and the words the program is started with
#define SETTING_PINS_WORD 512   // bytes of a word
// The programs the desktop opens when it starts (`desktop autostart`): a line of absolute paths, a blank between, opened in that order, or
// none, which opens nothing. With no file it is the standard, none: the desktop starts with no windows (the kernel log and the console were
// opened by default; the person adds what they want, `desktop autostart add /klog`). setting_autostart_valid
// says what a line is. It names any number of programs, each a path of any length: there is no cap, as with the rest of the system.
#define SETTING_DESKTOP_AUTOSTART_PATH "/root/desktop.autostart"
#define SETTING_AUTOSTART_STANDARD "" // none
// The programs the desktop starts with it (`desktop startup`), in order: a line each, "on" or "off" (started or kept but not started), then
// "app ID COMMAND" (a program of the start menu's by its id, and the command that starts it) or "command COMMAND" (a command line of the
// person's, run by sh -c). With no file yet, desktop autostart's programs are its entries, each on (desktop_startup_read).
#define SETTING_DESKTOP_STARTUP_PATH "/root/desktop.startup"

#define SETTING_THEME_PATH "/root/canvas.theme"
// The theme `auto` is light from this hour of the local clock to the other, and dark outside them: the compositor's windows
// and the session bus's color-scheme (src/dbus/service.c) take it from here, so that a program of a bowl turns with them.
#define SETTING_THEME_DAY_FROM 7
#define SETTING_THEME_DAY_UNTIL 19
#define SETTING_ACCENT_PATH "/root/canvas.theme.accent"
#define SETTING_MOTION_PATH "/root/canvas.motion"
#define SETTING_MOTION_SPEED_PATH "/root/canvas.motion.speed"
#define SETTING_WHEEL_SMOOTH_PATH "/root/canvas.wheel.smooth"
#define SETTING_TRACKPAD_SPEED_PATH "/root/canvas.trackpad.speed"
#define SETTING_SCROLL_NATURAL_PATH "/root/canvas.scroll.natural"
#define SETTING_SCROLL_MOMENTUM_PATH "/root/canvas.scroll.momentum"
#define SETTING_SCROLL_RUBBER_PATH "/root/canvas.scroll.rubber"
#define SETTING_SCROLL_DRAG_PATH "/root/canvas.scroll.drag"
#define SETTING_TERMINAL_WHEEL_PATH "/root/canvas.terminal.wheel"
#define SETTING_CARET_BLINK_PATH "/root/canvas.caret.blink"
#define SETTING_CARET_THICKNESS_PATH "/root/canvas.caret.thickness"
// The text's size, in percent of what every role's size says: ui.c's ui_em multiplies it in, and the portal says it as text-scaling-factor.
#define SETTING_TEXT_SCALE_PATH "/root/canvas.text.scale"
#define SETTING_BORDER_WIDTH_PATH "/root/canvas.border.width"
#define SETTING_BORDER_GRIP_PATH "/root/canvas.border.grip"
// The language the windows Canvas draws itself speak: the code of a file in /usr/share/moonwater/language (ui/ui.c reads it).
// These are the codes the Language row offers, in the order of its menu; a check keeps them the languages/ directory's.
// The network's numbers and its proxy switch (src/net/discovery.c reads each by its leaf, DISCOVERY_*_LEAF under DISCOVERY_KEPT,
// and takes a word that is no number in the row's bounds for the row's standard; CHECK_waterlink_service holds the two the same).
#define SETTING_NETWORK_STUN_MS_PATH "/root/network.discovery.stun.timeout"
#define SETTING_NETWORK_TRACE_SECONDS_PATH "/root/network.discovery.https.timeout"
#define SETTING_NETWORK_PROXY_PATH "/root/network.proxy"
#define SETTING_NETWORK_KEEPALIVE_PATH "/root/network.keepalive"
#define SETTING_LANGUAGE_PATH "/root/language"
#define SETTING_LANGUAGE_DIRECTORY "/usr/share/moonwater/language/"
#define SETTINGS_LANGUAGE_CODES "en es fr de pt it nl pl tr sv ru uk el ar he fa ur"

struct settings_row
{
        // The window.
        const char address_to section; // the header this row starts a card under, or none
        const char address_to label;
        unsigned char kind;
        unsigned char lacks;                    // a machine may have none: no key said is no value, not a failure
        unsigned char flags;                    // SETTINGS_LINE, SETTINGS_SPLIT
        const char address_to read[SETTING_WORDS]; // what is asked, after moonwater
        const char address_to key;              // the words the value follows ("" is the whole first line)
        const char address_to also;             // and where it is when that is said with nothing after it
        const char address_to set[SETTING_WORDS];  // what changes it; the value goes after
        const char address_to choices;          // a choice's words, or a step's two, a space between
        char stop;                              // a value ends here as well as at a blank, a comma, a semicolon or a bracket
        // The command and its pages.
        const char address_to usage; // the row of the pages: the words after moonwater, as typed; none when another row's says it
        const char address_to help;  // and what it says of it
        unsigned char how;           // SETTING_SPECIAL, SETTING_PLAIN, SETTING_ALIAS
        unsigned char type;          // SETTING_WORD, SETTING_NUMBER, SETTING_COLOUR
        unsigned char live;          // SETTING_LIVE_*
        // A plain row's.
        const char address_to path;     // the file in /root that keeps it
        const char address_to standard; // what it is when nothing is kept, as it is said
        const char address_to reset;    // the word kept when it is put back (default when none)
        const char address_to say;      // what its line starts with (its verb without the first word when none)
        const char address_to need;     // what a change needs root for: null is "how Canvas draws", "" is none (moonwater VERB needs root)
        const char address_to refusal;  // what a word that is none is told: %s is the word (generated when none)
        const char address_to unit;     // a number's unit, said after it
        long low, high;                 // a number's least and greatest
        unsigned char output;           // SETTINGS_OUT_*: a row of the Display map's screen, which the table says, not a verb
        // The window's again, last so that the places of the fields before stay.
        const char address_to part;  // the header of the part of its group this row starts, or none: it is in the part before it
        const char address_to about; // what it does, in plain words: the line under its label in the window
        // A change that is a deliberate act of the person's (a server turned on, root let in, a key added): the window runs it only for
        // a key or a press with no WINDOW_EVENT_VIRTUAL, which the compositor leaves off only for what it read from a device it found
        // physical, so never for uinput, uhid or the on-screen keyboard (settings.c's settings_refused; .github/SECURITY.md, the ssh server).
        unsigned char deliberate;
        // A row the window lists as no row of its own: its verb is a way of the add row of its group's list (settings.c's settings_way_row),
        // the way's number from 1; 0 for every other row.
        unsigned char way;
        // A row of the command and its pages alone, which the window does not list (canvas background: its colour is chosen in the
        // wallpaper editor's Solid colour card now, settings.c's settings_rebuild passes it over).
        unsigned char unlisted;
};

/*
        The rows. A group (section) starts at the row that names it and holds
        the rows after it; the order is the window's, and the main page of the
        command shows the rows of a verb in this order too. The labels are
        what the window and its lane find a row by.

        The groups, most used first: System (its name, build, language, notices
        and about), Canvas (how the desktop is drawn and what its windows keep),
        Display (the screens), Input (the keyboard, the mouse, the pointer and
        touch), Taskbar (the panel), Time (the clock, its zone and where it is
        set from), Network, Wi-Fi, Bluetooth, Devices, Power and Boot. A row is
        in the group a person looks for it in, by what it does and not by the
        program that applies it: the pointer's size is an Input row though the
        compositor draws it.

        A group of more than a few rows is in parts: a row that names a part
        (part) starts it, and it holds the rows after it up to the next part or
        group; rows of a group before its first part are in the group's own,
        under the group's name. The window heads each part with its name, and
        the parts of a group are in the order a person reaches for them, the
        most used first. Every row the window shows says what it does (about),
        in a line of plain words under its label.
*/
#define SETTING_UNWRAP(...) __VA_ARGS__
// A plain row: group, label, window kind and flags, verb words in brackets, key, choices, then the rest by name.
#define SETTING_PLAIN_ROW(section_, label_, kind_, flags_, verb_, key_, choices_, ...)                                \
        {section_, label_, kind_, 0, flags_, {SETTING_UNWRAP verb_}, key_, null, {SETTING_UNWRAP verb_}, choices_, 0, \
         .how = SETTING_PLAIN, __VA_ARGS__}

static const struct settings_row settings_rows[] = {
    {"System", "Name", SETTINGS_FIELD, 0, SETTINGS_LINE, {"name"}, "", null, {"name"}, null, 0,
     .usage = "name [NEW|random]", .help = "what this machine is called, like space-wizard"},
    //  The commit this build was made from, as moonwater version says it: what tells one build on a stick from another.
    {null, "Build", SETTINGS_INFO, 0, SETTINGS_LINE, {"version"}, "commit ", null, {null}, null, 0,
     .help = "the commit this system was built from, with -dirty when the tree had changes; moonwater version also says its date and the kernel's release",
     .about = "The commit this system was built from"},
    //  What the system's own windows say, in which language: one file of languages/ read by every window of the kit, taken
    //  from the window table so a change reaches them at once. Bare, the verb adds what is installed and which are machine-made.
    SETTING_PLAIN_ROW(null, "Language", SETTINGS_CHOICE, SETTINGS_LANGUAGE, ("language"), "language ", SETTINGS_LANGUAGE_CODES,
                      .path = SETTING_LANGUAGE_PATH, .standard = "en", .say = "language", .need = "the language",
                      .refusal = "language is one of " SETTINGS_LANGUAGE_CODES ", not %s\n",
                      .usage = "language [CODE|default]",
                      .help = "the language the windows Canvas draws itself speak; bare lists them, machine-translated ones marked [en]"),
    //  The kernel's own notices (the feeder puts the errors and warnings of the kernel's log under the key system notices): no verb of its
    //  own (moonwater notices is the command), so no read; its value is the count, and opening the row lists each notice to copy.
    {null, "System notices", SETTINGS_INFO, 0, 0, {null}, null, null, {null}, null, 0, .output = SETTINGS_OUT_NOTICES,
     .help = "the kernel's own errors and warnings the desktop has: open the row to see each one, and copy a line"},
    //  Each Scene's wallpaper (the compositor's wallpaper.c): chosen in the editor over the desktop, which this opens and shuts, with a
    //  solid colour on its colour wheel, the Moonwater moon (dark, white, or with the theme), the pictures remembered and the feed's.
    //  canvas wallpaper is its verb (page.c's rows).
    {"Canvas", "Wallpaper", SETTINGS_ACTION, 0, 0, {null}, null, null, {"canvas", "wallpaper", "edit"}, null, 0,
     .help = "the editor of the wallpaper of the Scene you are on, over the desktop: a solid colour, the moon, the pictures you chose and the curated ones",
     .about = "Choose each Scene's wallpaper on the desktop"},
    //  The desktop's colour, merged into the wallpaper: the window has no row of it (unlisted), a Scene's plain colour is chosen in the
    //  editor's Solid colour card (colour:rrggbb, the compositor's wallpaper.c). What is kept here is the colour shown where no wallpaper
    //  covers (while one is made, a Scene card with no miniature) and the colour that card starts at for a Scene with no colour of its own.
    SETTING_PLAIN_ROW(null, "Background", SETTINGS_CHOICE, SETTINGS_INK, ("canvas", "background"), "background ",
                      "default black dark-grey slate midnight-blue forest plum light-grey white",
                      .type = SETTING_COLOUR, .path = SETTING_BACKGROUND_PATH, .standard = "#1e1e1e", .reset = "default",
                      .refusal = "background is a colour like 1e1e1e, or default, not %s\n",
                      .usage = "canvas background [COLOUR|default]",
                      .help = "the colour where no wallpaper covers, and the one the Solid colour card of the wallpaper editor starts at, rrggbb; a Scene is given a colour of its own by canvas wallpaper scene N colour:rrggbb [1e1e1e]",
                      .unlisted = 1),
    //  The theme of Canvas's own windows (ui.c's kit): light or dark, or auto by the clock, and the accent its controls
    //  fill with; the compositor puts them in the window table and every window turns to them at once.
    SETTING_PLAIN_ROW(null, "Theme", SETTINGS_CHOICE, 0, ("canvas", "theme"), "theme ", "light dark auto",
                      .path = SETTING_THEME_PATH, .standard = "light", .reset = "light",
                      .refusal = "theme is light, dark or auto, not %s\n",
                      .usage = "canvas theme [light|dark|auto]",
                      .help = "the colours of the windows Canvas draws itself; auto is light from 7 to 19 by the clock [light]"),
    SETTING_PLAIN_ROW(null, "Accent", SETTINGS_CHOICE, SETTINGS_INK, ("canvas", "theme", "accent"), "accent ",
                      "white amber blue green violet rose graphite",
                      .type = SETTING_COLOUR, .path = SETTING_ACCENT_PATH, .standard = "#ffffff",
                      .reset = "reset", .refusal = "accent is a colour like ff8800, or reset, not %s\n",
                      .usage = "canvas theme accent [COLOUR|reset]",
                      .help = "what the controls fill with, rrggbb; the text on it is picked to be read [ffffff]"),
    {null, "Accent colour (rrggbb or reset)", SETTINGS_FIELD, 0, 0, {"canvas", "theme", "accent"}, "accent ", null,
     {"canvas", "theme", "accent"}, null, 0, .how = SETTING_ALIAS},
    SETTING_PLAIN_ROW(null, "Motion", SETTINGS_CHOICE, 0, ("canvas", "motion"), "motion ", "full reduced",
                      .path = SETTING_MOTION_PATH, .standard = "full", .reset = "full",
                      .refusal = "motion is full or reduced, not %s\n",
                      .usage = "canvas motion [full|reduced]",
                      .help = "reduced: the windows Canvas draws change at once, with no spring, stretch or ripple [full]"),
    //  The pace of every transition the windows Canvas draws and the compositor make (ui.c's ui_paced): a percent of the speed they were
    //  designed at, so 100 is each as designed and the standard, 115, a hair faster. Reduced motion stays no motion at any speed.
    SETTING_PLAIN_ROW(null, "Animation speed (percent, or default)", SETTINGS_SLIDER, 0, ("canvas", "motion", "speed"), "speed ", null,
                      .type = SETTING_NUMBER, .path = SETTING_MOTION_SPEED_PATH, .standard = "115",
                      .unit = "percent", .low = 50, .high = 200,
                      .usage = "canvas motion speed [PERCENT|default]",
                      .help = "how fast windows, menus and controls move, 50 to 200 percent of the speed they were designed at, higher quicker; reduced motion is none at any speed [115]"),
    //  The caret of every text field and the terminal (ui.c's ui_caret_wave): how it blinks, and how wide it is. Off is solid.
    SETTING_PLAIN_ROW(null, "Caret blink speed", SETTINGS_CHOICE, 0, ("canvas", "caret", "blink"), "blink ", "slow normal fast off",
                      .path = SETTING_CARET_BLINK_PATH, .standard = "normal", .reset = "normal",
                      .refusal = "caret blink is slow, normal, fast or off, not %s\n",
                      .usage = "canvas caret blink [slow|normal|fast|off]",
                      .help = "how the caret blinks, or off for a solid caret [normal]"),
    SETTING_PLAIN_ROW(null, "Caret thickness", SETTINGS_CHOICE, 0, ("canvas", "caret", "thickness"), "thickness ", "thin normal thick",
                      .path = SETTING_CARET_THICKNESS_PATH, .standard = "normal", .reset = "normal",
                      .refusal = "caret thickness is thin, normal or thick, not %s\n",
                      .usage = "canvas caret thickness [thin|normal|thick]",
                      .help = "how wide the caret is: 1, 2 or 3 pixels at 100%; a block cursor is a whole cell [normal]"),
    //  The text of every window Canvas draws (ui.c's ui_em): its size as a percent of what each role says. A list's rows grow
    //  with it above 100, and a label wider than before makes its button wider; the terminal keeps the size of its own.
    SETTING_PLAIN_ROW(null, "Text size (percent, or default)", SETTINGS_SLIDER, 0, ("canvas", "text", "scale"), "scale ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TEXT_SCALE_PATH, .standard = "100",
                      .unit = "percent", .low = 50, .high = 300,
                      .usage = "canvas text scale [PERCENT|default]",
                      .help = "how large the text of the windows Canvas draws is, 50 to 300 percent [100]"),
    //  The width of the highlight on a window's edge or corner the pointer is over or the hand holds (compositor.c's
    //  compositor_lit_px): drawn inward from the outline, so the window does not move; 1 is the line's own pixel.
    SETTING_PLAIN_ROW(null, "Resize border width (pixels, or default)", SETTINGS_SLIDER, 0, ("canvas", "border", "width"), "width ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_BORDER_WIDTH_PATH, .standard = "2",
                      .unit = "pixels", .low = 1, .high = 4,
                      .usage = "canvas border width [PIXELS|default]",
                      .help = "how wide the highlight on the edge or corner of a window is when the pointer is over it or the hand holds it, 1 to 4 pixels [2]"),
    //  How far outside a window's edge a pointer's press resizes it (compositor.c's compositor_grip); two pixels inside are always the grip,
    //  and a finger's is wider whatever this says.
    SETTING_PLAIN_ROW(null, "Resize grip (pixels, or default)", SETTINGS_SLIDER, 0, ("canvas", "border", "grip"), "grip ", null,
                      .type = SETTING_NUMBER, .path = SETTING_BORDER_GRIP_PATH, .standard = "6",
                      .unit = "pixels", .low = 2, .high = 16,
                      .usage = "canvas border grip [PIXELS|default]",
                      .help = "how far outside the edge of a window a press of the pointer takes it to resize, 2 to 16 pixels; a finger takes 16 [6]"),
    //  Read by the compositor when a window opens and when the hand lets one go, so it is never stale and has no live.
    SETTING_PLAIN_ROW(null, "Open windows where they were last put", SETTINGS_SWITCH, 0, ("canvas", "windows", "remember"), "remember ", null,
                      .live = SETTING_LIVE_NONE, .path = SETTING_WINDOWS_REMEMBER_PATH, .standard = "on",
                      .usage = "canvas windows remember [on|off]",
                      .help = "a window opens where the hand last put it, kept across boots [on]"),
    //  The clipboard, an OS service of the compositor's (.github/SECURITY.md): off empties both selections and the
    //  history and refuses every copy; the history is copies kept in the compositor's memory and never on a disk, none
    //  by default, since a clipboard that remembers is a clipboard that leaks what was copied an hour ago.
    SETTING_PLAIN_ROW(null, "Clipboard", SETTINGS_SWITCH, 0, ("canvas", "clipboard"), "clipboard ", null,
                      .path = SETTING_CLIPBOARD_PATH, .standard = "on",
                      .usage = "canvas clipboard [on|off]", .help = "copy and paste between windows; off empties the clipboard [on]"),
    SETTING_PLAIN_ROW(null, "Clipboard history (copies kept)", SETTINGS_SLIDER, 0, ("canvas", "clipboard", "history"), "history ", null,
                      .type = SETTING_NUMBER, .path = SETTING_CLIPBOARD_HISTORY_PATH,
                      .standard = "0", .unit = "copies", .low = 0, .high = 32,
                      .usage = "canvas clipboard history [N|default]",
                      .help = "copies the compositor keeps in memory, never on a disk, 0 to 32 [0]"),
    //  The Display group: the map above these rows (settings.c) shows the screens; the rows are for the screen chosen on it.
    //  The rotation and refresh are the table's, and each change is `canvas display NAME ...` run with the screen's name.
    {"Display", "Screen", SETTINGS_CHOICE, 0, 0, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "the screen the rows under the map are for", .output = SETTINGS_OUT_SCREEN},
    {null, "Rotation", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "which way up the screen is", .output = SETTINGS_OUT_TURN},
    {null, "Turned by the screen", SETTINGS_INFO, 0, 0, {null}, null, null, {null}, null, 0,
     .help = "who turns the picture: the screen itself, or Moonwater (slower)", .output = SETTINGS_OUT_NOTE},
    {null, "Resolution", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "the size the screen is driven at: every size it lists, its own marked; a change is kept only when confirmed", .output = SETTINGS_OUT_SIZE},
    {null, "Refresh rate", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "how many times a second the screen is drawn: the rates it supports, the best marked", .output = SETTINGS_OUT_REFRESH},
    {null, "Scale of this screen", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "how big the screen draws things, in percent of its size: 50, 75, 100, 125, 150 or 200 [100]", .output = SETTINGS_OUT_SCALE},
    {null, "Primary display", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "the screen the taskbar is on and windows open on, wherever it is laid", .output = SETTINGS_OUT_PRIMARY},
    {null, "Screen modes", SETTINGS_CHOICE, 0, 0, {"canvas", "modes"}, "modes ", null, {"canvas", "modes"}, "largest preferred", 0,
     .usage = "canvas modes [largest|preferred]", .help = "which mode a screen is driven at [largest]"},
    //  Variable refresh (kms.c's compositor_vrr): a screen that can (its connector's vrr_capable) waits for each frame, so a key or a frame
    //  is shown when it is drawn and not at the next fixed vblank. On by default, for every window; games keeps it to a window shown
    //  straight from its buffer (fullscreen), for a panel whose brightness flickers as its rate swings; a screen that cannot is never set.
    SETTING_PLAIN_ROW(null, "Variable refresh rate", SETTINGS_CHOICE, 0, ("canvas", "vrr"), "vrr ", "on games off",
                      .path = SETTING_VRR_PATH, .standard = "on", .reset = "on",
                      .refusal = "vrr is on, games or off, not %s\n",
                      .usage = "canvas vrr [on|games|off]",
                      .help = "a screen that can waits for each frame, so it is shown when drawn; games: only a fullscreen window [on]"),
    //  Game frame cadence (kms.c's compositor_cadence_keep): on a screen with no variable refresh, a Steam game shown fullscreen gets the
    //  screen's mode at the highest rate that is a multiple of this many frames a second (40 on 120 Hz) and a frame every k vblanks of it,
    //  so each frame is on the screen as long as the next. Off by default: entering and leaving the game is a mode switch, a blank.
    SETTING_PLAIN_ROW(null, "Game frame cadence", SETTINGS_CHOICE, 0, ("canvas", "cadence"), "cadence ", "off 40 48 50 60 72",
                      .path = SETTING_CADENCE_PATH, .standard = "off", .reset = "off",
                      .refusal = "cadence is off, 40, 48, 50, 60 or 72, not %s\n",
                      .usage = "canvas cadence [off|40|48|50|60|72]",
                      .help = "a fullscreen Steam game on a screen without variable refresh: a rate that is a multiple of this, a frame every k vblanks [off]"),
    //  Tearing (scanout.c's compositor_tear): which frames of a window shown straight from its buffer (a fullscreen game) are put up at
    //  once, mid-scanout, instead of at the next vblank. late by default: a frame that missed its vblank by a little is shown at once,
    //  torn near the top, where it waited a whole period (the one remedy for a late frame on a screen without variable refresh); a game
    //  that asks to tear (vsync off) tears; off honours no game's ask; always tears every frame of every game.
    SETTING_PLAIN_ROW(null, "Tearing in fullscreen games", SETTINGS_CHOICE, 0, ("canvas", "tearing"), "tearing ", "off hinted late always",
                      .path = SETTING_TEARING_PATH, .standard = "late", .reset = "late",
                      .refusal = "tearing is off, hinted, late or always, not %s\n",
                      .usage = "canvas tearing [off|hinted|late|always]",
                      .help = "the frame of a fullscreen game put up at once, torn, not at the next vblank: hinted when the game asks, late also when it is late, always [late]"),
    {null, "Brightness", SETTINGS_SLIDER, 1, 0, {"brightness"}, "brightness ", null, {"brightness"}, null, 0,
     .type = SETTING_NUMBER, .unit = "percent", .low = 0, .high = 100,
     .about = "The screen backlight, from 0 to 100 percent. A display with no backlight control has none to set, and this row is greyed out.",
     .usage = "brightness [N%|+N|-N]", .help = "the screen backlight"},
#ifndef SHELL_NO_GPU
    //  In the Display group, as its Graphics part: the card that draws the screens, its status and why it is off (the board's
    //  issues under the row) beside the screens it draws. Canvas draws and composes on the GPU where it has a driver for it (src/canvas/gpu.c) and on the processor where it
    //  has none. On by default: the GPU is the robust default, and every failure (no kernels, a refused map, a hang, a lost card)
    //  falls back to the processor for that job or the session, with one notice. Not in the image
    //  when the build says CONFIG_MOONWATER_GPU=n.
    SETTING_PLAIN_ROW(null, "GPU rendering (alpha)", SETTINGS_SWITCH, 0, ("canvas", "gpu"), "gpu ", null,
                      .path = SETTING_GPU_PATH, .standard = "on",
                      .usage = "canvas gpu [on|off]",
                      .help = "draw and compose on the GPU where Canvas has a driver for it, on the processor elsewhere; alpha [on]",
                      .part = "Graphics",
                      .about = "Where Canvas draws: on the GPU, or the processor. What the card does now is under it"),
    SETTING_PLAIN_ROW(null, "GPU samples tiled client buffers", SETTINGS_SWITCH, 0, ("canvas", "gpu", "tiled"), "gpu tiled ", null,
                      .path = SETTING_GPU_TILED_PATH, .standard = "off",
                      .usage = "canvas gpu tiled [on|off]",
                      .help = "sample tiled and compressed client pictures in place on the GPU, an experimental speed-up [off]",
                      .about = "An experimental speed-up. If the screen tears or shows garbage, turn it off"),
#endif
    //  The menu names each layout once; gb, sv and nb, which the help names as well, are the same layouts as uk, se
    //  and no (locale_keyboards in host.c, the compositor's table), kept as typed, so the menu leaves them out.
    {"Input", "Keyboard", SETTINGS_CHOICE, 0, 0, {"keyboard"}, "keyboard ", null, {"keyboard"}, "us uk de se no dk fi fr es it ar il ir", 0,
     .usage = "keyboard [LAYOUT|list|xkb]", .help = "us uk gb de se sv no nb dk fi fr es it ar il ir [us]",
     .part = "Keyboard",
     .about = "The layout the keys type in"},
    //  The keyboard on the screen (src/canvas/compositor.c, the osk window): shown while a text field has the keys and there is no
    //  keyboard to type on (auto), never (off), or whenever a field has them (on).
    SETTING_PLAIN_ROW(null, "On-screen keyboard", SETTINGS_CHOICE, 0, ("keyboard", "onscreen"), "onscreen ", "auto off on",
                      .need = "", .path = SETTING_ONSCREEN_PATH, .standard = "auto", .reset = "auto",
                      .refusal = "the on-screen keyboard is auto, off or on, not %s\n",
                      .usage = "keyboard onscreen [auto|off|on]",
                      .help = "a keyboard on the screen while a text field has the keys: auto when no keyboard is plugged in, off, or on [auto]",
                      .about = "Automatic shows it when no keyboard is plugged in"),
    SETTING_PLAIN_ROW(null, "Key repeat delay (ms, or default)", SETTINGS_SLIDER, 0, ("keyboard", "repeat", "delay"), "delay ", null,
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_REPEAT_DELAY_PATH,
                      .standard = "250", .unit = "ms", .low = 100, .high = 2000,
                      .usage = "keyboard repeat delay [MS|default]",
                      .help = "how long a key is held before it repeats, 100 to 2000 ms [250]",
                      .about = "The wait before a held key repeats"),
    SETTING_PLAIN_ROW(null, "Key repeat rate (per second, off or default)", SETTINGS_FIELD, 0, ("keyboard", "repeat", "rate"), "rate ", "off",
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_REPEAT_RATE_PATH,
                      .standard = "30", .unit = "per second", .low = 1, .high = 100,
                      .usage = "keyboard repeat rate [PER-SECOND|off|default]",
                      .help = "repeats a second of a held key, 1 to 100, or none [30]",
                      .about = "How many times a second a held key repeats, or off"),
    SETTING_PLAIN_ROW(null, "Mouse sensitivity (percent, or default)", SETTINGS_SLIDER, 0, ("mouse", "sensitivity"), "sensitivity ", null,
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_SENSITIVITY_PATH,
                      .standard = "100", .unit = "percent", .low = 10, .high = 500,
                      .usage = "mouse sensitivity [PERCENT|default]", .help = "how far the pointer goes for the hand, 10 to 500 percent [100]",
                      .part = "Mouse",
                      .about = "How fast the pointer follows the mouse"),
    SETTING_PLAIN_ROW(null, "Mouse acceleration", SETTINGS_SWITCH, 0, ("mouse", "acceleration"), "acceleration ", null,
                      .need = "", .path = SETTING_ACCELERATION_PATH, .standard = "on",
                      .usage = "mouse acceleration [on|off]", .help = "the pointer outruns a fast hand; off is one to one [on]",
                      .about = "The pointer goes further when the mouse moves fast"),
    //  The kit's one double press (ui.c's ui_click_again): a press this soon after the last, and near it, is the next click of its run, in
    //  every text, the terminal, the tray and the windows' tops.
    SETTING_PLAIN_ROW(null, "Double-click time (ms, or default)", SETTINGS_SLIDER, 0, ("mouse", "double", "click"), "click ", null,
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_DOUBLE_CLICK_PATH,
                      .standard = "450", .unit = "ms", .low = 200, .high = 900,
                      .usage = "mouse double click [MS|default]",
                      .help = "how soon a second press near the first is a double click, 200 to 900 ms; a third as soon is a triple [450]",
                      .about = "How quickly the two clicks must come"),
    //  The wheel's glide (ui_wheel_turn, every region the wheel turns): on, a notch runs to its step (two and a half rows) over a
    //  short time, starting at once and easing out; off, a notch moves a whole step at once.
    SETTING_PLAIN_ROW(null, "Smooth mouse wheel", SETTINGS_SWITCH, 0, ("canvas", "wheel", "smooth"), "smooth ", null,
                      .path = SETTING_WHEEL_SMOOTH_PATH, .standard = "on",
                      .usage = "canvas wheel smooth [on|off]",
                      .help = "a notch runs to its step (two and a half rows) at once and eases out; off moves a whole step at once [on]",
                      .about = "The page glides with each notch instead of jumping"),
    //  The pointer's shape: the classic bitmaps, or one filled shape with nothing in it, a circle that a fast hand stretches and
    //  that is a rounded block over text (compositor.c's dynamic cursor). A touch always uses it, whatever this says.
    SETTING_PLAIN_ROW(null, "Cursor style", SETTINGS_CHOICE, 0, ("canvas", "cursor", "style"), "style ", "classic dynamic",
                      .path = SETTING_CURSOR_STYLE_PATH, .standard = "dynamic", .reset = "dynamic",
                      .refusal = "cursor style is classic or dynamic, not %s\n",
                      .usage = "canvas cursor style [classic|dynamic]",
                      .help = "the classic pointers, or one filled shape that stretches with a fast hand and is a rounded block over text [dynamic]",
                      .part = "Pointer",
                      .about = "Arrows, or a dot that turns into a bar over text"),
    //  The pointer's size (compositor.c's compositor_cursor_size): a percent of the size the desktop's scale gives it, which is 100;
    //  the standard, 80, is a little smaller. A file kept before it was a percent says auto, which is the standard, or 1 to 4, a size
    //  of its own (setting_cursor_size_was).
    SETTING_PLAIN_ROW(null, "Cursor size (percent, or default)", SETTINGS_SLIDER, 0, ("canvas", "cursor", "size"), "size ", null,
                      .type = SETTING_NUMBER, .path = SETTING_CURSOR_SIZE_PATH,
                      .standard = "80", .unit = "percent", .low = 50, .high = 300,
                      .usage = "canvas cursor [size [PERCENT|default]|look [light|dark|auto]]",
                      .help = "the pointer: its size, 50 to 300 percent of the size the desktop scale gives it, and its colours [80, auto]"),
    SETTING_PLAIN_ROW(null, "Cursor look", SETTINGS_CHOICE, 0, ("canvas", "cursor", "look"), "look ", "auto light dark",
                      .path = SETTING_CURSOR_LOOK_PATH, .standard = "auto", .reset = "auto",
                      .refusal = "cursor look is light, dark or auto, not %s\n",
                      .about = "White edged in black, or the reverse"),
    //  A held button changes the pointer's size for as long as it is down (compositor.c's compositor_pointer_press): on by default.
    SETTING_PLAIN_ROW(null, "Pointer squeezes on click", SETTINGS_SWITCH, 0, ("canvas", "cursor", "click"), "click ", null,
                      .path = SETTING_CURSOR_CLICK_PATH, .standard = "on",
                      .usage = "canvas cursor click [on|off]",
                      .help = "a held button changes how large the pointer is: the left shrinks it, the right grows it [on]",
                      .about = "The pointer shrinks while a button is held down"),
    //  How far a pointer shrinks while a button is held (compositor.c's compositor_pointer_press, for either style): 0 is none.
    SETTING_PLAIN_ROW(null, "Pointer squeeze (percent, or default)", SETTINGS_SLIDER, 0, ("canvas", "cursor", "squeeze"), "squeeze ", null,
                      .type = SETTING_NUMBER, .path = SETTING_SQUEEZE_PATH,
                      .standard = "25", .unit = "percent", .low = 0, .high = 50,
                      .usage = "canvas cursor squeeze [PERCENT|default]",
                      .help = "how far a pointer shrinks while a button is held, 0 to 50 percent, 0 for none; the right button grows it by half that [25]",
                      .about = "How much it shrinks while held"),
    SETTING_PLAIN_ROW(null, "Shake the pointer to find it", SETTINGS_SWITCH, 0, ("canvas", "cursor", "shake"), "shake ", null,
                      .path = SETTING_SHAKE_PATH, .standard = "on",
                      .usage = "canvas cursor shake [on|off]", .help = "a shake of the hand makes the pointer large for a moment [on]",
                      .about = "A quick shake makes the pointer large for a moment"),
    //  How much of a shake finds the pointer (compositor.c's compositor_shake): the step a reversal takes and how many come in its window.
    SETTING_PLAIN_ROW(null, "Shake sensitivity", SETTINGS_CHOICE, 0, ("canvas", "cursor", "shake", "sensitivity"), "sensitivity ", "low normal high",
                      .path = SETTING_SHAKE_FEEL_PATH, .standard = "normal", .reset = "normal",
                      .refusal = "shake sensitivity is low, normal or high, not %s\n",
                      .usage = "canvas cursor shake sensitivity [low|normal|high]",
                      .help = "how much shaking makes the pointer large: low takes more and wider shakes, high fewer and smaller [normal]",
                      .about = "How much shaking makes the pointer grow"),
    SETTING_PLAIN_ROW(null, "Large pointer stays (ms, or default)", SETTINGS_SLIDER, 0, ("canvas", "cursor", "shake", "delay"), "delay ", null,
                      .type = SETTING_NUMBER, .path = SETTING_SHAKE_DELAY_PATH,
                      .standard = "1200", .unit = "ms", .low = 100, .high = 10000,
                      .usage = "canvas cursor shake delay [MS|default]",
                      .help = "how long it stays large after the last shake, 100 to 10000 ms [1200]",
                      .about = "How long it stays large after a shake"),
    //  The prediction a frame ahead (compositor.c's predict): a finger or a pen is delivered where it will be. The mouse
    //  pointer is always predicted, so it has no row.
    SETTING_PLAIN_ROW(null, "Predict touch and pen", SETTINGS_SWITCH, 0, ("canvas", "predict", "touch"), "touch ", null,
                      .path = SETTING_PREDICT_TOUCH_PATH, .standard = "on",
                      .usage = "canvas predict touch [on|off]",
                      .help = "a finger or a pen is taken a frame ahead of where it is, to the windows it reaches [on]",
                      .part = "Touch",
                      .about = "A finger or pen is followed a frame ahead, so it lags less"),
    //  On by default (the owner's call): a finger hides what it presses, and the mark says where the screen read it. It
    //  is drawn for a touch screen's fingers alone, never for a mouse, a pad or a pen, and costs nothing with no finger
    //  down or fading.
    SETTING_PLAIN_ROW(null, "Show touch area", SETTINGS_SWITCH, 0, ("canvas", "touch"), "touch ", null,
                      .path = SETTING_TOUCH_PATH, .standard = "on",
                      .usage = "canvas touch [on|off]",
                      .help = "a circle under each finger on a touch screen, fading for a second after it lifts [on]",
                      .about = "A circle under each finger on a touch screen"),
    //  The one hold of a finger (ui.c's ui_hold_ns): still this long it is a long press, the right button, as the touch recogniser has it;
    //  a key of the on-screen keyboard held this long shows its accents, and a swipe from an edge is decided within it.
    SETTING_PLAIN_ROW(null, "Long press time (ms, or default)", SETTINGS_SLIDER, 0, ("canvas", "touch", "hold"), "hold ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TOUCH_HOLD_PATH,
                      .standard = "500", .unit = "ms", .low = 300, .high = 1500,
                      .usage = "canvas touch hold [MS|default]",
                      .help = "how long a finger held still is a long press, the right button, and a key of the on-screen keyboard shows its accents, 300 to 1500 ms [500]",
                      .about = "How long to hold a finger to right-click"),
    //  The screen's side edges (ui/ui.c, compositor.c): a swipe in from the left edge runs the window switcher or opens the
    //  launcher, one in from the right runs the switcher or opens the volume popup. Off by default: a swipe from a side is then
    //  the window's, as a touch.
    SETTING_PLAIN_ROW(null, "Left edge", SETTINGS_CHOICE, 0, ("canvas", "touch", "left"), "left ", "off windows launcher",
                      .path = SETTING_TOUCH_LEFT_PATH, .standard = "off", .reset = "off",
                      .refusal = "the left edge is off, windows or launcher, not %s\n",
                      .usage = "canvas touch left [off|windows|launcher]",
                      .help = "a swipe in from the left edge runs the window switcher, or opens the launcher [off]",
                      .part = "Edge swipes",
                      .about = "What a swipe in from this edge does"),
    SETTING_PLAIN_ROW(null, "Right edge", SETTINGS_CHOICE, 0, ("canvas", "touch", "right"), "right ", "off windows volume",
                      .path = SETTING_TOUCH_RIGHT_PATH, .standard = "off", .reset = "off",
                      .refusal = "the right edge is off, windows or volume, not %s\n",
                      .usage = "canvas touch right [off|windows|volume]",
                      .help = "a swipe in from the right edge runs the window switcher, or opens the volume popup [off]",
                      .about = "What a swipe in from this edge does"),
    //  The scrolling group: how a wheel, a trackpad and a drag move the content (ui.c's scroll body). Natural direction turns
    //  the way a wheel's notch and a pad's motion move it (a finger on the screen always takes the content with it); speed
    //  scales a trackpad's notches and streams (compositor_pad_pct, the speed row that was in Input); momentum is the coast a
    //  release carries; the rubber band stretches past an end and springs home.
    SETTING_PLAIN_ROW("Scrolling", "Natural direction", SETTINGS_SWITCH, 0, ("canvas", "scroll", "natural"), "natural ", null,
                      .path = SETTING_SCROLL_NATURAL_PATH, .standard = "off",
                      .usage = "canvas scroll natural [on|off]",
                      .help = "a wheel and a trackpad move the content with the fingers, as on a touch screen; off, the other way, as a wheel mouse does [off]"),
    //  A percent of the standard distance, 25 to 400: slow, normal and fast are the words of 60, 100 and 160 (setting_parse keeps
    //  them as the number), so a file that holds a word, as the row once kept, is read as its number.
    SETTING_PLAIN_ROW(null, "Scroll speed", SETTINGS_SLIDER, 0, ("canvas", "trackpad", "speed"), "speed ", "slow normal fast",
                      .type = SETTING_NUMBER, .path = SETTING_TRACKPAD_SPEED_PATH, .standard = "100",
                      .unit = "percent", .low = 25, .high = 400,
                      .refusal = "trackpad speed is a number from 25 to 400 percent, slow, normal, fast or default, not %s\n",
                      .usage = "canvas trackpad speed [PERCENT|slow|normal|fast|default]",
                      .help = "how far a wheel notch and a trackpad scroll move the content, 25 to 400 percent: a notch is two and a half rows, a stream notch of a trackpad 8 px at 100 and more as its notches come faster; slow, normal and fast are 60, 100 and 160 [100]"),
    //  The terminal's notch (terminal.c's terminal_wheel_lines): the lines it moves the scrollback, and on the alternate screen the arrows
    //  it sends a full-screen program. The scroll speed above scales the scrollback's as it does every notch.
    SETTING_PLAIN_ROW(null, "Terminal lines per wheel notch", SETTINGS_SLIDER, 0, ("canvas", "terminal", "wheel"), "wheel ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TERMINAL_WHEEL_PATH,
                      .standard = "3", .unit = "lines", .low = 1, .high = 10,
                      .usage = "canvas terminal wheel [LINES|default]",
                      .help = "how many lines a notch of the mouse wheel moves the terminal, 1 to 10; a full-screen program is sent as many arrows [3]"),
    SETTING_PLAIN_ROW(null, "Momentum", SETTINGS_SWITCH, 0, ("canvas", "scroll", "momentum"), "momentum ", null,
                      .path = SETTING_SCROLL_MOMENTUM_PATH, .standard = "on",
                      .usage = "canvas scroll momentum [on|off]",
                      .help = "a drag or a trackpad hand carries on past its release and slows; off, the content stops where the fingers leave it, for the guest only: momentum from a patched host is not stopped (no phase signal yet) [on]"),
    SETTING_PLAIN_ROW(null, "Rubber band", SETTINGS_SWITCH, 0, ("canvas", "scroll", "rubber"), "rubber ", null,
                      .path = SETTING_SCROLL_RUBBER_PATH, .standard = "on",
                      .usage = "canvas scroll rubber [on|off]",
                      .help = "past the top or the bottom the content stretches and springs back; off, it stops at the end [on]"),
    //  A mouse's press on a list that moves past the slop drags the list as a finger's does, and its release throws it (ui.c's
    //  ui_press_drags): the settings page's rows and groups, a list of the kit's, a scroll container of a view. A press on a slider, a
    //  scroll bar or a text is still theirs, and a press let go within the slop is still the row's click. On by default (the owner's call): off, a
    //  mouse scrolls a list by its wheel and its bar alone.
    SETTING_PLAIN_ROW(null, "Drag lists with the mouse", SETTINGS_SWITCH, 0, ("canvas", "scroll", "drag"), "drag ", null,
                      .path = SETTING_SCROLL_DRAG_PATH, .standard = "on",
                      .usage = "canvas scroll drag [on|off]",
                      .help = "a mouse press on a list that moves drags the list as a finger does, and its release throws it; a slider, a scroll bar or a text keeps its own drag; off, the mouse scrolls a list by its wheel and its bar alone [on]"),
    //  The aids of the keys and the pointer, for a hand a keyboard or a mouse asks too much of (src/canvas/compositor/access.c): each is
    //  off until the person turns it on, and with all of them off the keys and the pointer take the paths they take without them.
    SETTING_PLAIN_ROW("Accessibility", "Sticky keys", SETTINGS_SWITCH, 0, ("access", "sticky"), "sticky ", null,
                      .need = "", .path = SETTING_ACCESS_STICKY_PATH, .standard = "off",
                      .usage = "access sticky [on|off]",
                      .help = "Shift, Ctrl, Alt, AltGr or Super pressed and let go alone holds for the next key; pressed twice, it holds until pressed again [off]",
                      .about = "Press a modifier, then the key, instead of both at once"),
    SETTING_PLAIN_ROW(null, "Slow keys (ms, off or default)", SETTINGS_FIELD, 0, ("access", "slow"), "slow ", "off",
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_ACCESS_SLOW_PATH,
                      .standard = "off", .unit = "ms", .low = 0, .high = 5000,
                      .usage = "access slow [MS|off|default]",
                      .help = "a key counts only once it is held this long, up to 5000 ms, 0 or off for none; one let go sooner types nothing [off]",
                      .about = "A key types only after it is held this long"),
    SETTING_PLAIN_ROW(null, "Bounce keys (ms, off or default)", SETTINGS_FIELD, 0, ("access", "bounce"), "bounce ", "off",
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_ACCESS_BOUNCE_PATH,
                      .standard = "off", .unit = "ms", .low = 0, .high = 2000,
                      .usage = "access bounce [MS|off|default]",
                      .help = "a key pressed again this soon after it was let go is a bounce, and it and its release are dropped, up to 2000 ms, 0 or off for none [off]",
                      .about = "A key pressed again too soon after it is let go types once"),
    SETTING_PLAIN_ROW(null, "Mouse keys", SETTINGS_SWITCH, 0, ("access", "mousekeys"), "mousekeys ", null,
                      .need = "", .path = SETTING_ACCESS_MOUSEKEYS_PATH, .standard = "off",
                      .usage = "access mousekeys [on|off]",
                      .help = "the keypad moves the pointer, faster the longer it is held, whatever Num Lock says: 5 clicks, / * - choose the left, middle or right button, 0 holds it and . lets it go; numbers are typed on the top row [off]",
                      .about = "The number pad moves the pointer and clicks"),
    SETTING_PLAIN_ROW(null, "Dwell click (ms, off or default)", SETTINGS_FIELD, 0, ("access", "dwell"), "dwell ", "off",
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_ACCESS_DWELL_PATH,
                      .standard = "off", .unit = "ms", .low = 0, .high = 5000,
                      .usage = "access dwell [MS|off|default]",
                      .help = "the pointer held still this long clicks the left button once, up to 5000 ms, 0 or off for none; it clicks again after it moves [off]",
                      .about = "Hold the pointer still to click"),
    //  The sound server's master volume, mute and card (src/audio): files the server reads again twice a second and writes back
    //  when a program (the mixer, the tray, a volume key) changes the volume, so a row here and a slider there are one value.
    //  The window stops at 100 percent, as the mixer does; the server itself takes up to 200 from a file. default puts the volume
    //  back to 100 (a file that said default would be read by the server as no word at all). The card is a name from the page the
    //  server gives it or default for the automatic choice, so its verb is written by hand (host.c).
    SETTING_PLAIN_ROW("Sound", "Volume (percent, or default)", SETTINGS_SLIDER, 0, ("audio", "volume"), "volume ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_AUDIO_VOLUME_PATH, .standard = "100", .reset = "100", .unit = "percent",
                      .low = 0, .high = 100, .usage = "audio volume [PERCENT|default]",
                      .help = "the volume of everything that plays, 0 to 100 percent [100]"),
    SETTING_PLAIN_ROW(null, "Mute", SETTINGS_SWITCH, 0, ("audio", "mute"), "mute ", null,
                      .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_AUDIO_MUTE_PATH, .standard = "off", .usage = "audio mute [on|off]",
                      .help = "silence everything that plays, and keep the volume [off]"),
    {null, "Output device (name, or default)", SETTINGS_FIELD, 0, 0, {"audio", "device"}, "device ", null, {"audio", "device"}, null, 0,
     .usage = "audio device [NAME|default]",
     .help = "the sound card that plays, by the name bare audio lists; default picks the first that is not HDMI [default]"},
    SETTING_PLAIN_ROW(null, "Sound server", SETTINGS_SWITCH, 0, ("audio", "server"), "server ", null,
                      .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_AUDIO_SERVER_PATH, .standard = "on", .usage = "audio server [on|off]",
                      .help = "the server programs play their sound through; off keeps it from starting with the desktop [on]"),
    //  Recording: the person's switch for every program that records (off ends the recordings there are and refuses new ones),
    //  the microphone they are given (a name from the server's page, or default for the first), and that microphone's volume and
    //  mute, which the mixer's input strip and the microphone key change too. The microphone's verb is written by hand (host.c) as
    //  the sound card's is.
    SETTING_PLAIN_ROW(null, "Allow recording", SETTINGS_SWITCH, 0, ("audio", "capture"), "capture ", null,
                      .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_AUDIO_CAPTURE_PATH, .standard = "on", .usage = "audio capture [on|off]",
                      .help = "let programs record from the microphone; off ends every recording and refuses new ones [on]"),
    {null, "Input device (name, or default)", SETTINGS_FIELD, 0, 0, {"audio", "source"}, "source ", null, {"audio", "source"}, null, 0,
     .usage = "audio source [NAME|default]",
     .help = "the microphone programs record from, by the name bare audio lists; default picks the first [default]"},
    SETTING_PLAIN_ROW(null, "Input volume (percent, or default)", SETTINGS_SLIDER, 0, ("audio", "input", "volume"), "volume ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_AUDIO_SOURCE_VOLUME_PATH, .standard = "100", .reset = "100", .unit = "percent",
                      .low = 0, .high = 100, .usage = "audio input volume [PERCENT|default]",
                      .help = "the volume of the microphone, 0 to 100 percent [100]"),
    SETTING_PLAIN_ROW(null, "Input mute", SETTINGS_SWITCH, 0, ("audio", "input", "mute"), "mute ", null,
                      .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_AUDIO_SOURCE_MUTE_PATH, .standard = "off", .usage = "audio input mute [on|off]",
                      .help = "silence the microphone, and keep its volume [off]"),
    SETTING_PLAIN_ROW(null, "Desktop jingles", SETTINGS_SWITCH, 0, ("canvas", "jingles"), "jingles ", null,
                      .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_JINGLES_PATH, .standard = "on", .usage = "canvas jingles [on|off]",
                      .help = "short chimes for desktop startup, notices, popups and volume changes; off silences them [on]"),
    SETTING_PLAIN_ROW("Taskbar", "Taskbar position", SETTINGS_CHOICE, 0, ("taskbar", "position"), "position ", "bottom top left right",
                      .path = SETTING_TASKBAR_POSITION_PATH, .standard = "bottom", .reset = "bottom",
                      .refusal = "taskbar position is bottom, top, left or right, not %s\n",
                      .usage = "taskbar position [bottom|top|left|right]", .help = "the edge of the screen the taskbar sits on [bottom]"),
    SETTING_PLAIN_ROW(null, "Taskbar display", SETTINGS_CHOICE, 0, ("taskbar", "display"), "display ", "primary all",
                      .path = SETTING_TASKBAR_DISPLAY_PATH, .standard = "primary", .reset = "primary",
                      .refusal = "taskbar display is primary or all, not %s\n",
                      .usage = "taskbar display [primary|all]", .help = "the screens the taskbar is on: the primary, or all, each listing its windows [primary]"),
    //  The taskbar's own size, on top of its screen's scale: how thick the band is and how large everything in it is drawn (its tabs'
    //  text, the tray, the launcher's mark), from half to twice; and the padding, the room round the text inside each tab, either side of
    //  it, at that scale (the gap between the tabs is fixed). Each is a slider in the window and any number in range to the verb. The compositor takes both from their files (compositor_taskbar_settings_read), a missing file or a
    //  word that is none being the standard.
    SETTING_PLAIN_ROW(null, "Taskbar scale (percent)", SETTINGS_SLIDER, 0, ("taskbar", "scale"), "scale ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TASKBAR_SCALE_PATH, .standard = "100",
                      .unit = "percent", .low = 50, .high = 200,
                      .usage = "taskbar scale [PERCENT|default]",
                      .help = "how large the taskbar and what it shows are, 50 to 200 percent of the screen scale [100]"),
    SETTING_PLAIN_ROW(null, "Taskbar padding (pixels)", SETTINGS_SLIDER, 0, ("taskbar", "padding"), "padding ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TASKBAR_PADDING_PATH, .standard = "12",
                      .unit = "px", .low = 0, .high = 24,
                      .usage = "taskbar padding [PX|default]",
                      .help = "the room inside each taskbar button before what it shows and after it (the icon and title of a task, the moon, a tray item), 0 to 24 pixels at the taskbar scale [12]"),
    //  On by default: the band has a tab with the launcher's mark at its leading end (the left of a bottom or top band, the top of a
    //  side one) that opens and closes the launcher beside it, and the logo key pressed and let go alone is a press on it. Off, the
    //  band has no such tab and the launcher opens from its chord alone, in the middle of the screen.
    SETTING_PLAIN_ROW(null, "Taskbar launcher button", SETTINGS_SWITCH, 0, ("taskbar", "launcher"), "launcher ", null,
                      .path = SETTING_TASKBAR_LAUNCHER_PATH, .standard = "on",
                      .usage = "taskbar launcher [on|off]",
                      .help = "a tab with the launcher mark at the start of the taskbar that opens the launcher beside it and puts it away again, as the logo key alone does [on]"),
    //  On, which is the default, the band slides off its edge while a window is fullscreen (compositor.c takes it from its file,
    //  and a missing file or a file that says default is on) and comes back when none is. Off, the band stays where it is.
    SETTING_PLAIN_ROW(null, "Taskbar hides for fullscreen", SETTINGS_SWITCH, 0, ("taskbar", "fullscreen"), "fullscreen ", null,
                      .path = SETTING_TASKBAR_FULLSCREEN_PATH, .standard = "on",
                      .usage = "taskbar fullscreen [on|off]",
                      .help = "the taskbar slides away while a window is fullscreen, and comes back when none is [on]"),
    //  The letters' tops toward the middle by default: a side band is then the bottom band stood on its end, its titles
    //  facing the round corners of its tabs as the bottom band's do. Flipping the default is this row's standard, its
    //  help and the README's line; the compositor takes the standard from here, and a file that says default follows it.
    SETTING_PLAIN_ROW(null, "Side taskbar text faces", SETTINGS_CHOICE, 0, ("taskbar", "text"), "text ", "center edge",
                      .path = SETTING_TASKBAR_TEXT_PATH, .standard = "center",
                      .refusal = "taskbar text is center or edge, not %s\n",
                      .usage = "taskbar text [center|edge]",
                      .help = "on a left or right taskbar, which way the tops of the letters face: the middle of its screen or its edge [center]"),
    //  The arrangement (taskbar.c's taskbar_lay): where the moon and the tasks are along the band, the end the newest task is at, which
    //  side of the tasks each tray item is on and the order of the pills that share a place; the moon's look, and the clock on the moon
    //  or in the tray and how far its digits swell onto the sphere. The band's edit mode writes the same files when Done is pressed.
    SETTING_PLAIN_ROW(null, "Taskbar moon place", SETTINGS_CHOICE, 0, ("taskbar", "moon"), "moon ", "start center end",
                      .path = SETTING_TASKBAR_MOON_PATH, .standard = "center",
                      .usage = "taskbar moon [start|center|end]",
                      .help = "where the moon is along the taskbar while the tasks are not split about it: its start, its center or its end [center]"),
    SETTING_PLAIN_ROW(null, "Taskbar moon look", SETTINGS_CHOICE, 0, ("taskbar", "moon", "look"), "look ", "realistic ink",
                      .path = SETTING_TASKBAR_MOON_LOOK_PATH, .standard = "realistic",
                      .usage = "taskbar moon look [realistic|ink]",
                      .help = "the moon on the taskbar as it is, lit and turning, or drawn in ink as the wallpaper can be [realistic]"),
    //  The moon's turn: once in this many seconds, five small steps a second, each the moon's square drawn again and nothing else; none
    //  with reduced motion, with off, or two minutes after the last thing a hand did.
    SETTING_PLAIN_ROW(null, "Taskbar moon spin (seconds a turn, off or default)", SETTINGS_FIELD, 0, ("taskbar", "moon", "spin"), "spin ", "off",
                      .type = SETTING_NUMBER, .path = SETTING_TASKBAR_MOON_SPIN_PATH, .standard = "60", .unit = "seconds", .low = 10, .high = 3600,
                      .usage = "taskbar moon spin [SECONDS|off|default]",
                      .help = "how long the moon on the taskbar takes to turn once, 10 to 3600 seconds, or off; it rests with reduced motion and two minutes after the last key or move [60]"),
    SETTING_PLAIN_ROW(null, "Taskbar tasks place", SETTINGS_CHOICE, 0, ("taskbar", "tasks"), "tasks ", "split start center end",
                      .path = SETTING_TASKBAR_TASKS_PATH, .standard = "split",
                      .usage = "taskbar tasks [split|start|center|end]",
                      .help = "where the tasks are: split either side of the moon by turns, or together at the start, the center or the end of the taskbar [split]"),
    SETTING_PLAIN_ROW(null, "Taskbar tasks", SETTINGS_CHOICE, 0, ("taskbar", "tasks", "grouped"), "grouped ", "grouped separate",
                      .path = SETTING_TASKBAR_GROUPED_PATH, .standard = "grouped",
                      .usage = "taskbar tasks grouped [grouped|separate]",
                      .help = "the tasks of a side in one glass pill, or each in a pill of its own with the gap between them; the tray stays one pill [grouped]"),
    //  How far the pills float from the screen's edge: in by a number above 0, out past it below 0, so less of the screen is the bar's.
    //  Out, the compositor keeps 60 percent of a pill's thickness on the screen whatever the number, so the clock and the controls stay
    //  usable; the room the windows fill follows what is on the screen.
    SETTING_PLAIN_ROW(null, "Taskbar distance from the edge (pixels)", SETTINGS_SLIDER, 0, ("taskbar", "edge"), "edge ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TASKBAR_EDGE_PATH, .standard = "0", .unit = "px", .low = -16, .high = 32,
                      .usage = "taskbar edge [PX|default]",
                      .help = "how far the pills float in from the edge of the screen, -16 to 32 pixels at the taskbar scale; below 0 they reach past it, held to show 60 percent of their thickness [0]"),
    SETTING_PLAIN_ROW(null, "Taskbar newest task side", SETTINGS_CHOICE, 0, ("taskbar", "newest"), "newest ", "left right",
                      .path = SETTING_TASKBAR_NEWEST_PATH, .standard = "left",
                      .usage = "taskbar newest [left|right]",
                      .help = "the end of the tasks the newest one is at, left or right [left]"),
    SETTING_PLAIN_ROW(null, "Taskbar notices place", SETTINGS_CHOICE, 0, ("taskbar", "item", "notices"), "notices ", "before after",
                      .path = SETTING_TASKBAR_ITEM_NOTICES_PATH, .standard = "after",
                      .usage = "taskbar item notices [before|after]",
                      .help = "which side of the tasks the dot of notices and issues is on: in the pill before them or in the one after them [after]"),
    SETTING_PLAIN_ROW(null, "Taskbar microphone place", SETTINGS_CHOICE, 0, ("taskbar", "item", "mic"), "mic ", "before after",
                      .path = SETTING_TASKBAR_ITEM_MIC_PATH, .standard = "before",
                      .usage = "taskbar item mic [before|after]",
                      .help = "which side of the tasks the microphone is on: in the pill before them or in the one after them [before]"),
    SETTING_PLAIN_ROW(null, "Taskbar sound place", SETTINGS_CHOICE, 0, ("taskbar", "item", "sound"), "sound ", "before after",
                      .path = SETTING_TASKBAR_ITEM_SOUND_PATH, .standard = "before",
                      .usage = "taskbar item sound [before|after]",
                      .help = "which side of the tasks the volume is on: in the pill before them or in the one after them [before]"),
    SETTING_PLAIN_ROW(null, "Taskbar battery place", SETTINGS_CHOICE, 0, ("taskbar", "item", "battery"), "battery ", "before after",
                      .path = SETTING_TASKBAR_ITEM_BATTERY_PATH, .standard = "after",
                      .usage = "taskbar item battery [before|after]",
                      .help = "which side of the tasks the battery is on: in the pill before them or in the one after them [after]"),
    SETTING_PLAIN_ROW(null, "Taskbar tray text place", SETTINGS_CHOICE, 0, ("taskbar", "item", "text"), "text ", "before after",
                      .path = SETTING_TASKBAR_ITEM_TEXT_PATH, .standard = "after",
                      .usage = "taskbar item text [before|after]",
                      .help = "which side of the tasks the tray text of a format of your own is on: in the pill before them or in the one after them [after]"),
    SETTING_PLAIN_ROW(null, "Taskbar tray clock place", SETTINGS_CHOICE, 0, ("taskbar", "item", "clock"), "clock ", "before after",
                      .path = SETTING_TASKBAR_ITEM_CLOCK_PATH, .standard = "after",
                      .usage = "taskbar item clock [before|after]",
                      .help = "which side of the tasks the clock while it is in the tray is on: in the pill before them or in the one after them [after]"),
    SETTING_PLAIN_ROW(null, "Taskbar order", SETTINGS_CHOICE, 0, ("taskbar", "order"), "order ",
                      "moon-tasks-tray moon-tray-tasks tasks-moon-tray tasks-tray-moon tray-moon-tasks tray-tasks-moon",
                      .path = SETTING_TASKBAR_ORDER_PATH, .standard = "moon-tasks-tray",
                      .usage = "taskbar order [moon-tasks-tray|...]",
                      .help = "the order of the moon, the tasks and the tray where they share a place on the taskbar, any of the six [moon-tasks-tray]"),
    SETTING_PLAIN_ROW(null, "Taskbar clock place", SETTINGS_CHOICE, 0, ("taskbar", "clock"), "clock ", "moon none tray tab",
                      .path = SETTING_TASKBAR_CLOCK_PATH, .standard = "moon",
                      .usage = "taskbar clock [moon|none|tray|tab]",
                      .help = "where the clock is: in the moon, hours over minutes; none, the moon alone; in the tray as text in the time format with the date; or a pill of its own after the tray [moon]"),
    SETTING_PLAIN_ROW(null, "Taskbar clock bulge (percent)", SETTINGS_SLIDER, 0, ("taskbar", "clock", "bulge"), "bulge ", null,
                      .type = SETTING_NUMBER, .path = SETTING_TASKBAR_CLOCK_BULGE_PATH, .standard = "60",
                      .unit = "percent", .low = 0, .high = 100,
                      .usage = "taskbar clock bulge [PERCENT|default]",
                      .help = "how far the digits of the clock in the moon swell onto its sphere, 0 flat to 100 percent [60]"),
    //  A special row (host.c locale_taskbar_tray) whose file the compositor watches: the whole line is the format.
    {null, "Taskbar tray (text with {names}: {battery} {time.24h.hour}:{time.24h.min} {date.day} {cpu.load} ...)", SETTINGS_FIELD, 0,
     SETTINGS_LINE, {"taskbar", "tray"}, "tray ", null, {"taskbar", "tray"}, null, 0,
     .usage = "taskbar tray [FORMAT|default]",
     .help = "what the tray shows: text with {battery} {battery.percent} {battery.state} {time.24h.hour} {time.24h.min} {time.12h.hour} {time.ampm} {date.year} {date.month} {date.day} {date.weekday} {date.monthname} {cpu.load} {mem.used} {temp} {host} in it; {{ and }} are the braces [the battery, then the clock]",
     .path = SETTING_TASKBAR_TRAY_PATH},
    //  A special row (host.c locale_taskbar_pinned) whose file the compositor watches: the whole line is the programs pinned.
    {null, "Taskbar pinned programs (or none)", SETTINGS_FIELD, 0, SETTINGS_LINE, {"taskbar", "pinned"}, "pinned ", null, {"taskbar", "pinned"}, null, 0,
     .usage = "taskbar pinned [LINE|none]",
     .help = "the programs pinned to the taskbar, shown as a task while they are not running and started by a press on it: entries of an app_id, the executable and the words it starts with, each ended by a lone ;, a word in single quotes unless it is letters, digits and _ . / : = @ + , - [none]",
     .path = SETTING_TASKBAR_PINNED_PATH},
    {"Time", "Set the clock from the network", SETTINGS_SWITCH, 0, 0, {"ntp"}, "ntp ", null, {"ntp"}, null, 0,
     .usage = "ntp [on|off]", .help = "set the clock from the network [on]"},
    {null, "Time zone", SETTINGS_FIELD, 0, 0, {"timezone"}, "timezone ", null, {"timezone"}, null, 0,
     .usage = "timezone [ZONE|list]", .help = "IANA name, country, +1 or POSIX; manual"},
    //  The hours a clock is read in: the tray's (when its format is the standard one) and, to a bowl's GNOME programs, the
    //  portal's clock-format. A tray format of the person's own says its hours itself.
    SETTING_PLAIN_ROW(null, "Clock format", SETTINGS_CHOICE, 0, ("time", "format"), "format ", "24h 12h",
                      .path = SETTING_CLOCK_FORMAT_PATH, .standard = "24h",
                      .refusal = "clock format is 24h or 12h, not %s\n", .usage = "time format [24h|12h]",
                      .help = "the hours the clock is read in, on the tray and in GNOME programs [24h]"),
    //  The date the standard tray says beside the clock, in the order a person reads it: year first (2026-11-04), day first
    //  (04.11.2026) or month first (11/04/2026). A tray format of the person's own says its date itself with {date.*}.
    SETTING_PLAIN_ROW(null, "Date format", SETTINGS_CHOICE, 0, ("time", "date"), "date ", "off ymd dmy mdy",
                      .path = SETTING_DATE_FORMAT_PATH, .standard = "off",
                      .refusal = "date format is off, ymd, dmy or mdy, not %s\n", .usage = "time date [off|ymd|dmy|mdy]",
                      .help = "the date beside the clock on the tray, year first, day first or month first, and in GNOME programs [off]"),
    {null, "Clock", SETTINGS_ACTION, 0, SETTINGS_LINE | SETTINGS_TICK, {"time"}, "local ", null, {"time", "sync"}, null, 0,
     .usage = "time [sync]", .help = "the clock; sync sets it and the zone now"},
    {null, "Time server (auto for the pool)", SETTINGS_FIELD, 0, 0, {"ntp", "server"}, "ntp server ", null, {"ntp", "server"}, null, 0,
     .usage = "ntp server [NAME|auto]", .help = "who is asked first: a name or address [auto]"},
    {null, "Keep the lowest-delay sample", SETTINGS_SWITCH, 0, 0, {"ntp", "sampling"}, "ntp sampling ", null, {"ntp", "sampling"}, null, 0,
     .usage = "ntp sampling [on|off]", .help = "keep the lowest-delay sample of five [on]"},
    {null, "Authenticated time (NTS)", SETTINGS_CHOICE, 0, 0, {"ntp", "nts"}, "ntp nts ", null, {"ntp", "nts"}, "on off only", 0,
     .usage = "ntp nts [on|off|only]", .help = "authenticated time (RFC 8915) first; only refuses plain [on]"},
    {null, "NTS servers (or default)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"ntp", "nts"}, "servers ", null,
     {"ntp", "nts", "servers"}, null, ';',
     .usage = "ntp nts servers [NAME[:PORT]...|default]", .help = "who is asked for it, up to four"},
    {"Network", "Wired", SETTINGS_SWITCH, 0, 0, {"wired"}, "wired ", null, {"wired"}, null, 0,
     .usage = "wired [on|off]", .help = "the wired links; off keeps them down"},
    {null, "Airplane mode", SETTINGS_SWITCH, 0, 0, {"airplane"}, "airplane ", null, {"airplane"}, null, 0,
     .usage = "airplane [on|off]", .help = "every radio at once"},
    {null, "The internet prefers", SETTINGS_CHOICE, 0, 0, {"priority"}, "internet prefers ", null,
     {"priority", "internet"}, "wired wifi", 0,
     .usage = "priority [internet [wired|wifi]]", .help = "which link wins when both are up [wired]"},
    //  The address each connection has, read from `moonwater network discovery` as the Public ones are: the IPv4 addresses (and the
    //  first IPv6 one that reaches beyond the router) of the wired links, or the wireless ones, that are up with a carrier; none when
    //  there is none, as when a cable is out, and asked again while the group is shown.
    {null, "Wired IP address", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "wired address ", null, {null}, null, 0,
     .help = "the IPv4 addresses, and the first IPv6 one beyond the router, of the wired interfaces that are up with a cable in, or none"},
    {null, "Wi-Fi IP address", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "wifi address ", null, {null}, null, 0,
     .help = "the IPv4 addresses, and the first IPv6 one beyond the router, of the Wi-Fi interfaces that are up and joined, or none"},
    {null, "Names over TLS", SETTINGS_CHOICE, 0, 0, {"dns", "tls"}, "dns tls ", null, {"dns", "tls"}, "off on only", 0,
     .usage = "dns tls [on|off|only]", .help = "names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]"},
    {null, "Name servers (auto for the network)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"dns"}, "dns ", null, {"dns"}, null, '(',
     .usage = "dns [ADDRESS...|default|auto]",
     .help = "up to three servers names are asked of; default is the default list, then the server of the network; auto is that server first [default]"},
    //  `moonwater dns` says each of these a line a key with its value alone, and where each server came from and how internal
    //  names go on lines of their own after them (security.c's dns_say).
    {null, "Default servers (or reset)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"dns"}, "dns default ", null,
     {"dns", "default"}, null, 0,
     .usage = "dns default [ADDRESS...|reset]",
     .help = "the default list, one to three servers asked before the server of the network, and taken at once; reset is 1.1.1.1 and 1.0.0.1 [1.1.1.1 1.0.0.1]"},
    {null, "Servers in order", SETTINGS_INFO, 0, SETTINGS_LINE, {"dns"}, "dns order ", null, {null}, null, 0,
     .help = "each server in the order it is asked; where each came from is the line after it: the person's list, the default list or the network's lease"},
    {null, "Names of the network", SETTINGS_SWITCH, 0, 0, {"dns"}, "dns internal ", null, {"dns", "internal"}, null, 0,
     .usage = "dns internal [on|off]",
     .help = "a one-label name, or one under a suffix of dns internal suffixes, is asked of the server of the network first, which knows it; off asks it where every name goes [on]"},
    {null, "Their suffixes (or default)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"dns"}, "dns internal suffixes ", null,
     {"dns", "internal", "suffixes"}, null, 0,
     .usage = "dns internal suffixes [SUFFIX...|default]",
     .help = "the endings of the names of the network, one to eight [.local .home.arpa .internal .lan .localdomain .home]"},
    {null, "Their server", SETTINGS_INFO, 0, SETTINGS_LINE, {"dns"}, "dns internal server ", null, {null}, null, 0,
     .help = "the first server that is not a public one, which the names of the network are asked of first, or none"},
    //  Discovery (src/net/discovery.c, docs/discovery.md): every lookup that sends a packet off the machine to learn how it is
    //  reached, and the local network's announcing and asking, has its switch, and Discovery is all of them at once. Each is on
    //  when nothing is kept, and turning one on is deliberate (a key or a press from a device plugged into the machine); off is
    //  never refused. Under them what the lookups ask and how long they wait, then what `network discovery` kept and when, a
    //  line a key with its value alone, read from its page and never asked of the network by reading.
    {null, "Discovery", SETTINGS_SWITCH, 0, 0, {"network", "discovery"}, "discovery ", null, {"network", "discovery"}, null, 0,
     .usage = "network discovery [on|off|refresh]",
     .help = "how this machine is reached: its public address, UDP out, the NAT, the proxy, each address of each interface; on and off are every switch below at once, refresh looks again [on]",
     .deliberate = 1},
    {null, "Public address discovery (STUN)", SETTINGS_SWITCH, 0, 0, {"network", "discovery"}, "stun ", null,
     {"network", "discovery", "stun"}, null, 0,
     .usage = "network discovery stun [on|off]",
     .help = "a STUN binding to the first two servers of the list that resolve, which learn the public address of this machine; off sends no packet for it [on]",
     .deliberate = 1},
    {null, "Public address discovery (HTTPS)", SETTINGS_SWITCH, 0, 0, {"network", "discovery"}, "https ", null,
     {"network", "discovery", "https"}, null, 0,
     .usage = "network discovery https [on|off]",
     .help = "where UDP does not get out, the trace at the URL kept, fetched over HTTPS, which learns the public address of this machine; off sends no packet for it [on]",
     .deliberate = 1},
    {null, "LAN announce (mDNS)", SETTINGS_SWITCH, 0, 0, {"network", "discovery"}, "announce ", null,
     {"network", "discovery", "announce"}, null, 0,
     .usage = "network discovery announce [on|off]",
     .help = "the services of this machine announced on the local network over mDNS, and questions about them answered; off sends no announcement and no answer [on]",
     .deliberate = 1},
    {null, "LAN listen (mDNS)", SETTINGS_SWITCH, 0, 0, {"network", "discovery"}, "listen ", null,
     {"network", "discovery", "listen"}, null, 0,
     .usage = "network discovery listen [on|off]",
     .help = "the local network asked over mDNS for the machines on it, what they announce heard, and a pairing code swept over the subnet; off sends no question and greets nothing heard [on]",
     .deliberate = 1},
    {null, "STUN servers (or reset)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"network", "discovery"}, "stun servers ", null,
     {"network", "discovery", "stun", "servers"}, null, 0,
     .usage = "network discovery stun servers [NAME[:PORT]...|add NAME[:PORT]|remove NAME[:PORT]|reset]",
     .help = "the STUN servers, one to four, tried in order, each a name or an IPv4 address and its port when it is not 3478 [stun.cloudflare.com:3478 stun.l.google.com:19302]"},
    SETTING_PLAIN_ROW(null, "STUN time limit (ms, or default)", SETTINGS_FIELD, 0, ("network", "discovery", "stun", "timeout"),
                      "timeout ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_NETWORK_STUN_MS_PATH,
                      .standard = "1000", .unit = "ms", .low = 200, .high = 10000,
                      .usage = "network discovery stun timeout [MS|default]",
                      .help = "how long a STUN binding waits for its servers, asking again halfway, 200 to 10000 ms [1000]"),
    {null, "Trace URL (or default)", SETTINGS_FIELD, 0, SETTINGS_LINE, {"network", "discovery"}, "https url ", null,
     {"network", "discovery", "https", "url"}, null, 0,
     .usage = "network discovery https url [URL|default]",
     .help = "where the trace is fetched: an https URL whose answer has a line ip=ADDRESS [https://cloudflare.com/cdn-cgi/trace]"},
    SETTING_PLAIN_ROW(null, "Trace time limit (seconds, or default)", SETTINGS_FIELD, 0, ("network", "discovery", "https", "timeout"),
                      "timeout ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_NETWORK_TRACE_SECONDS_PATH,
                      .standard = "3", .unit = "seconds", .low = 1, .high = 30,
                      .usage = "network discovery https timeout [SECONDS|default]",
                      .help = "how long the whole fetch of the trace may take, 1 to 30 seconds [3]"),
    {null, "Public IPv4", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "public ipv4 ", null, {null}, null, 0,
     .help = "the address the internet sees this machine at; how it was learned and when are the lines after it"},
    {null, "Public IPv6", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "public ipv6 ", null, {null}, null, 0,
     .help = "this machine's global IPv6 address, which needs no NAT"},
    {null, "UDP out", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "udp out ", null, {null}, null, 0,
     .help = "whether a STUN server answered: UDP leaves this network and comes back"},
    {null, "NAT", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "nat ", null, {null}, null, 0,
     .help = "how the router maps this machine's port: the same for every server is one a hole can be punched through"},
    {null, "Proxy", SETTINGS_INFO, 0, SETTINGS_LINE, {"network", "discovery"}, "proxy ", null, {null}, null, 0,
     .help = "the HTTP proxy the environment names (HTTPS_PROXY), if any, and ignored when the row below is off"},
    SETTING_PLAIN_ROW(null, "Proxy from the environment", SETTINGS_SWITCH, 0, ("network", "proxy"), "proxy ", null,
                      .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_NETWORK_PROXY_PATH, .standard = "on",
                      .usage = "network proxy [on|off]",
                      .help = "take the proxy the environment names (HTTPS_PROXY, ALL_PROXY) as the way out; off ignores it [on]"),
    SETTING_PLAIN_ROW(null, "Keepalive (seconds, or default)", SETTINGS_FIELD, 0, ("network", "keepalive"), "keepalive ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_NONE, .need = "", .path = SETTING_NETWORK_KEEPALIVE_PATH,
                      .standard = "10", .unit = "seconds", .low = 1, .high = 40,
                      .usage = "network keepalive [SECONDS|default]",
                      .help = "how long a quiet link to a machine waits before it sends a datagram that keeps the NAT mapping open, 1 to 40 seconds [10]"),
    {null, "Local addresses", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "addresses ", null, {null}, null, 0,
     .help = "each interface, its kind (ethernet, wifi, vpn, virtual) and its addresses with their scope"},
    {null, "Learned", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"network", "discovery"}, "learned ", null, {null}, null, 0,
     .help = "when discovery last looked: at start, when the network changes, and when asked"},
    {null, "Discover now", SETTINGS_ACTION, 0, 0, {null}, null, null, {"network", "discovery", "refresh"}, null, 0,
     .help = "look again now, as far as the switches above allow, each lookup within its time limit"},
    //  Waterlink, the machines linked to this one (moonwater link, src/waterlink): this machine's name, the one switch of the
    //  listener, a code shown for another machine to type, the other machine's name and code typed here, and how the machines
    //  reach each other. The machines linked are a list under these rows, which settings.c feeds from `link` (its status); the code
    //  shown and the code typed are the ways of that list's add row (.way), not rows of their own; purge is the list's row after it (.way 3).
    {"Waterlink", "This machine", SETTINGS_INFO, 0, SETTINGS_LINE, {"name"}, "", null, {null}, null, 0,
     .help = "the name the other machines know this one by: moonwater name NEW changes it"},
    {null, "Waterlink", SETTINGS_SWITCH, 0, 0, {"link"}, "link ", null, {"link"}, null, 0,
     .help = "the listener: linked machines reach this one, and it them, while it is on [off]"},
    {null, "Pair a machine", SETTINGS_ACTION, 0, SETTINGS_WAITS, {null}, null, null, {"link", "pair"}, null, 0,
     .help = "shows a code, and waits up to five minutes for the other machine to type it with this machine's name", .way = 1},
    {null, "Purge unconnected links", SETTINGS_ACTION, 0, 0, {null}, null, null, {"link", "purge"}, null, 0,
     .help = "removes saved machines that are not currently connected (blocked and switched off machines stay)", .way = 3},
    {null, "Name and code of another machine", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT | SETTINGS_WAITS, {null}, null, null,
     {"link"}, null, 0,
     .help = "the other machine's name and the code it shows, which pair the two of them", .way = 2},
    {null, "Connect over", SETTINGS_CHOICE, 0, 0, {"link", "transport"}, "transport ", null, {"link", "transport"}, "network tcp auto modem", 0,
     .help = "network: over the network by udp, as linked machines always have [network]; tcp: over tcp, through HTTPS_PROXY when one is named; auto: udp, then tcp when udp is not answered in a second; modem: the audio modem, through the sound card, refused while it is not ready"},
    {null, "Audio modem", SETTINGS_INFO, 0, SETTINGS_LINE, {"link", "transport"}, "audio modem ", null, {null}, null, 0,
     .help = "the sound card as a link to another machine: it needs a sound card open on the sound server, and says why when it has none"},
    {null, "Paths", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"link", "paths"}, "paths ", null, {null}, null, 0,
     .help = "the ways in this machine has: its udp port on each local address, discovery on the network, and its public address"},
    {null, "Last-known addresses", SETTINGS_INFO, 0, SETTINGS_LINE | SETTINGS_TICK, {"link", "paths"}, "places ", null, {null}, null, 0,
     .help = "where each machine linked was last heard, and the other places it was heard at"},
    {"Wi-Fi", "Wi-Fi", SETTINGS_SWITCH, 0, 0, {"wifi"}, "wifi ", null, {"wifi"}, null, 0,
     .usage = "wifi [on|off]", .help = "the radio, saved networks and those in range [off]"},
    {"Bluetooth", "Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth"}, "bluetooth ", null, {"bluetooth"}, null, 0,
     .usage = "bluetooth [on|off]", .help = "the radio and the remembered devices [off]"},
    //  Internet sharing over Bluetooth: the machine's connection, offered to the devices that pair with it. Its verb (host.c's
    //  radio_share) starts and stops the sharing, so the row is not a plain one; the word it reads is the verb's own output.
    {null, "Internet sharing over Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth", "share"}, "share ", null, {"bluetooth", "share"}, null, 0,
     .usage = "bluetooth share [on|off]",
     .help = "the internet of this machine, shared with the Bluetooth devices that pair with it; needs the Bluetooth network tools [off]"},
    // The Devices group: the devices the system sees are a list under this row (the feed device list, settings.c), and a press
    // on one allows or blocks it. The row says what device lock says: whether a new device waits for its allow.
    {"Devices", "New devices", SETTINGS_CHOICE, 0, 0, {"device"}, "device ", null, {"device"}, "unlock lock", 0,
     .usage = "device [lock|unlock]", .help = "unlock lets a new device in; lock holds it until it is allowed [unlock]"},
    {"Power", "Battery", SETTINGS_INFO, 1, SETTINGS_LINE | SETTINGS_TICK, {"charge"}, "battery ", null, {null}, null, ';'},
    {null, "Battery shown", SETTINGS_CHOICE, 1, SETTINGS_BATTERIES, {"battery"}, "battery ", null, {"battery"}, null, 0,
     .usage = "battery [NAME|auto|none]", .help = "which battery the Battery row reads: auto is the first present one, none is no battery [auto]"},
    {null, "Power profile", SETTINGS_CHOICE, 1, 0, {"power"}, "power ", "profile ", {"power"},
     "balanced performance powersave", 0,
     .usage = "power [performance|balanced|powersave]", .help = "profile and CPU governor, kept across boots"},
    {null, "Boost", SETTINGS_SWITCH, 1, 0, {"cpu"}, "boost ", null, {"cpu", "boost"}, null, 0,
     .usage = "cpu [boost|smt on|off]", .help = "turbo and SMT, kept across boots"},
    {null, "Simultaneous threads (SMT)", SETTINGS_SWITCH, 1, 0, {"cpu"}, "smt ", null, {"cpu", "smt"}, null, 0},
    {null, "Charge limit (20 to 100, or off)", SETTINGS_FIELD, 1, 0, {"charge"}, "charging stops at ", null,
     {"charge", "limit"}, null, '%',
     .usage = "charge [limit N|limit off]", .help = "where the battery stops charging, 20 to 100"},
    {"Boot", "Start the desktop", SETTINGS_SWITCH, 1, 0, {"init", "canvas"}, "init canvas ", null, {"init", "canvas"},
     null, 0, .usage = "init canvas [on|off]", .help = "start Canvas at boot [on]"},
    {null, "Mount kept disks", SETTINGS_SWITCH, 1, 0, {"init", "mount"}, "init mount ", null, {"init", "mount"}, null,
     0, .usage = "init mount [on|off]", .help = "mount kept disks at boot [on]"},
    //  What the desktop starts with it, in order (desktop.c's desktop_startup), is no row: it is the Boot group's list (settings.c's startup
    //  feed), each entry a row of its own that the person turns on or off, moves and takes out, with an add row after them. The pages say
    //  its verb and desktop autostart's, which it took the programs of (page.c's host_rows).
    //  OpenSSH's server, from bowl profile ssh, which Moonwater runs from these rows and nothing else (src/sh/ssh.c): the one
    //  switch first, then who may log in and where it listens, what a client is to compare, and the keys. Every row reads
    //  moonwater ssh, which says "this machine has no ssh server" until the profile is installed, so each is not here then.
    //  Only root can log in: the machine's other account is the one every program of a bowl runs as.
    {"SSH", "SSH server", SETTINGS_SWITCH, 1, 0, {"ssh"}, "ssh ", null, {"ssh"}, null, 0,
     .usage = "ssh [on|off]",
     .help = "OpenSSH from bowl profile ssh, keys only and no forwarding; on ends by itself after its limit, off stops it and ends every session [off]",
     .about = "Lets the keys of the list log in over the network; off stops it and ends every session", .deliberate = 1},
    {null, "Root login", SETTINGS_SWITCH, 1, 0, {"ssh"}, "root ", null, {"ssh", "root"}, null, 0,
     .usage = "ssh root [on|off]", .help = "root may log in, by a key of the list only; it is the one account that can [off]",
     .about = "Root may log in by a key of the list, and by nothing else", .deliberate = 1},
    {null, "Port", SETTINGS_FIELD, 1, 0, {"ssh"}, "port ", null, {"ssh", "port"}, null, 0,
     .usage = "ssh port [N|default]", .help = "the tcp port the server listens on, 1 to 65535 [22]"},
    {null, "Listen address (any for every address)", SETTINGS_FIELD, 1, 0, {"ssh"}, "address ", null, {"ssh", "address"}, null, 0,
     .usage = "ssh address [ADDRESS|any]", .help = "the one IPv4 or IPv6 address of this machine it listens on, or every address [any]"},
    {null, "Time limit (minutes, or off)", SETTINGS_FIELD, 1, 0, {"ssh"}, "limit ", null, {"ssh", "limit"}, null, 0,
     .usage = "ssh limit [MINUTES|off|default]",
     .help = "an on ends by itself after this many minutes, 1 to 1440, and at the next boot whatever is left; off is no limit [60]"},
    {null, "Host key fingerprint", SETTINGS_INFO, 1, SETTINGS_LINE, {"ssh"}, "host key ", null, {null}, null, 0,
     .help = "what a client shows at its first connection: compare the two before the client is told yes"},
    {null, "Server files", SETTINGS_INFO, 1, SETTINGS_LINE, {"ssh"}, "binaries ", null, {null}, null, 0,
     .help = "whether each file sshd runs from is the one recorded at install, by its SHA-256; a changed one is not started"},
    {null, "Bowl tier", SETTINGS_INFO, 1, SETTINGS_LINE, {"ssh"}, "bowl tier ", null, {null}, null, 0,
     .help = "at the default tier a program of a bowl is root to the files of the machine, and these settings hold only against the others"},
    {null, "Add a key (label, then the public key)", SETTINGS_FIELD, 1, SETTINGS_LINE | SETTINGS_SPLIT, {null}, null, null,
     {"ssh", "key", "add"}, null, 0,
     .usage = "ssh key add LABEL KEY", .help = "let an ssh-ed25519 or sk-ssh-ed25519 public key log in, by a label; - reads the key from standard input",
     .deliberate = 1},
    {null, "End every session", SETTINGS_ACTION, 1, 0, {null}, null, null, {"ssh", "end"}, null, 0,
     .usage = "ssh end", .help = "end every session of the server now; it stays on"},
};

#define SETTINGS_ROWS (sizeof(settings_rows) / sizeof(settings_rows[0]))

/* ---- what the rows mean, for whoever asks ---- */

// How many words a row's verb has.
static positive setting_words(const struct settings_row address_to row)
{
        positive count = 0;

        while (count < SETTING_WORDS && row->read[count])
                count++;
        return count;
}

// Where word is among the words of list, a blank apart; -1 when it is none of them.
static long setting_place(const char address_to list, string_address word)
{
        positive length = string_length(word), place = 0;

        while (list && *list)
        {
                positive size = (positive)(string_first_of_or_end((string_address)list, ' ') - (string_address)list);

                if (size == length && !string_compare_max((string_address)list, word, length))
                        return (long)place;
                list += size + (list[size] == ' ');
                place++;
        }
        return -1;
}

/*
        The plain row that words (the words after moonwater, count of them)
        start with: the one with the most words of its own, so that
        canvas cursor shake delay is not canvas cursor shake with a word.
        Answers its index, or -1 when none is, and in used how many words
        it takes.
*/
static int setting_find(string_address address_to words, positive count, positive address_to used)
{
        int found = -1;
        positive most = 0;

        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];
                positive size = setting_words(row), word = 0;

                if (row->how != SETTING_PLAIN || size > count || size <= most)
                        continue;
                while (word < size && string_equals(words[word], (string_address)row->read[word]))
                        word++;
                if (word == size)
                {
                        found = (int)at;
                        most = size;
                }
        }
        address_to used = most;
        return found;
}

// The row whose file is path (one of the SETTING_ paths), which every row that is kept has.
static const struct settings_row address_to setting_by_path(string_address path)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].path && string_equals(path, (string_address)settings_rows[at].path))
                        return &settings_rows[at];
        return null;
}

/*
        A colour as #rrggbb in lower case, from rrggbb or the three digits of either that stand for
        each one twice: false for anything else.
*/
static bool setting_colour_normal(string_address word, p8 address_to into)
{
        long colour = window_colour_parse(word);
        p8 bytes[3] = {(p8)(colour >> 16), (p8)(colour >> 8), (p8)colour};

        if (colour < 0)
                return false;
        into[0] = '#';
        memory_into_hex(into + 1, bytes, 3);
        into[7] = end;
        return true;
}

// A whole number as it is kept: digits only, no more than nine, none leading but a lone zero.
static bool setting_whole(string_address word, long address_to into)
{
        string_address stop = word;
        b32 range = 0;
        positive length = string_length(word);

        if (!length || length > 9 || !byte_is_digit(word[0]) || (word[0] == '0' && length > 1))
                return false;
        *into = (long)string_to_number_unsigned_checked(word, address_of stop, 10, address_of range);
        return !*stop && !range;
}

// Whether word is unit, without the case of its letters: the one a row's number is written with.
static bool setting_unit_same(string_address word, const char address_to unit)
{
        return string_compare_folded(word, (string_address)unit) == 0;
}

// Whether a number row's unit is a percent, which is written % (`100%`) where it is shown.
static bool setting_percent(const struct settings_row address_to row)
{
        return row->unit && setting_unit_same((string_address)row->unit, "percent");
}

/*
        A whole number as a row takes it when it is typed: its digits, then
        nothing, or its unit after one blank or none. A percent's unit is %
        or percent, in any case, so 100, 100% and 100 percent are all 100.
        The number is what is kept (setting_parse writes it as digits). False
        for anything else: a sign, a fraction, a zero in front, or another
        unit.
*/
static bool setting_typed_number(const struct settings_row address_to row, string_address word, long address_to into)
{
        positive digits = 0, length = string_length(word);
        p8 digit[12];
        string_address rest;
        // A row whose least is under 0 (taskbar edge) takes a minus before the digits, and no other row does.
        bool minus = row->low < 0 && word[0] == '-';

        if (minus)
                word++, length--;
        while (digits < length && byte_is_digit(word[digits]))
                digits++;
        if (!digits || digits >= sizeof(digit))
                return false;
        memory_copy(digit, word, digits);
        digit[digits] = end;
        if (!setting_whole((string_address)digit, into))
                return false;
        if (minus)
                address_to into = -address_to into;
        if (digits == length)
                return true;
        if (!row->unit)
                return false;
        rest = word + digits + (word[digits] == ' ');
        if (setting_percent(row) && setting_unit_same(rest, "%"))
                return true;
        return setting_unit_same(rest, row->unit);
}

/*
        What a word is to a plain row. False when it is not one of its
        words; else it is kept as it is wanted in the file, normalised (a
        colour as #rrggbb), and what it stands for is value: a switch's 1
        or 0, a choice's place in its words, a number, a colour's rgb, and
        for the standard (default, or the reset word) the standard's.
*/
static long setting_standard(const struct settings_row address_to row);

// The words a value is one of: a switch's on and off, else the row's choices.
static const char address_to setting_words_of(const struct settings_row address_to row)
{
        return row->kind == SETTINGS_SWITCH ? "on off" : row->choices;
}

// The word kept when a row is put back: its reset word, else default.
static const char address_to setting_reset_of(const struct settings_row address_to row)
{
        return row->reset ? row->reset : "default";
}

static bool setting_parse(const struct settings_row address_to row, string_address word, p8 address_to kept, positive room,
                          long address_to value)
{
        const char address_to words = setting_words_of(row);
        const char address_to reset = setting_reset_of(row);
        p8 colour[8];
        long place;

        kept[0] = end;
        if (string_equals(word, (string_address) "default") || string_equals(word, (string_address)reset))
        {
                string_copy_bounded(kept, (string_address)reset, room);
                address_to value = setting_standard(row);
                return true;
        }
        if (row->type == SETTING_COLOUR)
        {
                if (!setting_colour_normal(word, colour))
                        return false;
                string_copy_bounded(kept, (string_address)colour, room);
                address_to value = window_colour_parse((string_address)colour);
                return true;
        }
        place = setting_place(words, word);
        // The words of a percent row (slow normal fast, the Scroll speed row's) stand for a number each and are kept as it.
        if (row->type == SETTING_NUMBER && place >= 0 && place < 3 && setting_percent(row))
        {
                static const long preset[3] = {60, 100, 160};

                positive_into_string((p8 address_to)kept, (positive)preset[place]);
                address_to value = preset[place];
                return true;
        }
        if (row->type == SETTING_NUMBER && place < 0)
        {
                long number;

                // The kept word is the number alone, digits as the file holds them, whatever unit was typed after it.
                if (!setting_typed_number(row, word, &number) || number < row->low || number > row->high)
                        return false;
                if (number < 0)
                        kept[0] = '-', positive_into_string((p8 address_to)kept + 1, (positive)-number);
                else
                        positive_into_string((p8 address_to)kept, (positive)number);
                address_to value = number;
                return true;
        }
        if (place < 0)
                return false;
        string_copy_bounded(kept, word, room);
        address_to value = row->type == SETTING_NUMBER ? 0 : place;
        return true;
}

// What the row is when nothing is kept: its standard as a value.
static long setting_standard(const struct settings_row address_to row)
{
        long value = 0;

        if (row->type == SETTING_NUMBER)
                return setting_whole((string_address)row->standard, &value) ? value : 0;
        if (row->type == SETTING_COLOUR)
                return window_colour_parse((string_address)row->standard);
        // A word's own place among its words.
        value = setting_place(setting_words_of(row), (string_address)row->standard);
        return value < 0 ? 0 : value;
}

/*
        The pointer's size as a file kept it before it was a percent, when it was a choice of auto or a whole
        number from 1 to 4: auto was the size the desktop's scale gives the pointer, which is 100, and N was N
        pixels a cell, which is N times that on a desktop drawn at one pixel a logical pixel (any scale below
        150 percent), so N is N times 100 and looks as it did there. A 4 is 400, more than the slider's
        greatest, which the compositor draws as it was and the slider shows at its end. False for any other
        word or row; a word of these typed is refused, as any number outside the range is.
*/
static bool setting_cursor_size_was(const struct settings_row address_to row, string_address word, long address_to percent)
{
        if (!row->path || !string_equals((string_address)row->path, (string_address)SETTING_CURSOR_SIZE_PATH))
                return false;
        // auto was the standard size, and it reads as the standard is now, a little smaller than the desktop's; a number was a size of
        // the person's own, and reads as the size it drew.
        if (string_equals(word, (string_address) "auto"))
                address_to percent = (long)string_digits((string_address)row->standard, null);
        else if (word[0] >= '1' && word[0] <= '4' && !word[1])
                address_to percent = (word[0] - '0') * 100;
        else
                return false;
        return true;
}

/*
        What a file of a plain row holds, as a value, in value: the standard when it holds nothing,
        the word default or a word that is none of the row's. True when something is kept.
*/
static bool setting_kept(const struct settings_row address_to row, string_address word, long address_to value)
{
        p8 kept[16];
        const char address_to reset = setting_reset_of(row);

        long found;

        if (setting_cursor_size_was(row, word, value))
                return true;
        address_to value = setting_standard(row);
        if (!word[0] || string_equals(word, (string_address) "default") || string_equals(word, (string_address)reset) ||
            !setting_parse(row, word, kept, sizeof(kept), &found))
                return false;
        address_to value = found;
        return true;
}

/*
        The pointer's size as a program that draws a pointer of its own takes it (the session bus's
        cursor-size, src/dbus/service.c): the 24 pixels of a pointer at 100 percent times the percent
        the file of the size says, as setting_kept reads it, to the nearest pixel and never less than 12.
*/
static long setting_cursor_pixels(string_address word)
{
        const struct settings_row address_to row = setting_by_path((string_address)SETTING_CURSOR_SIZE_PATH);
        long percent = 100, pixels;

        if (row)
                setting_kept(row, word, &percent);
        pixels = (24 * percent + 50) / 100;
        return pixels < 12 ? 12 : pixels;
}

/*
        What a plain row says of what its file holds: the word, a colour
        normalised, a number and its unit; the standard when nothing is kept
        or what is kept is none of its words. The pointer's size kept before
        it was a percent is said as the percent it is read as.
*/
static string_address setting_shown(const struct settings_row address_to row, string_address word, p8 address_to into, positive room)
{
        p8 kept[16];
        long value = 0;
        positive length;

        if (setting_cursor_size_was(row, word, &value))
                positive_into_string(into, (positive)value);
        else if (!word[0] || string_equals(word, (string_address) "default") || !setting_parse(row, word, kept, sizeof(kept), &value))
                string_copy_bounded(into, (string_address)row->standard, room);
        else
                string_copy_bounded(into, kept, room);
        if (row->type == SETTING_NUMBER && (byte_is_digit(into[0]) || (into[0] == '-' && byte_is_digit(into[1]))) && row->unit)
        {
                length = string_length(into);
                string_copy_bounded(into + length, (string_address) " ", room - length);
                string_copy_bounded(into + length + 1, (string_address)row->unit, room - length - 1);
        }
        return into;
}


/*
        The tray's format: what it may say. A name between braces is one of these (the tray fills it in each minute),
        {{ and }} are the braces themselves, and everything else is shown as it is. A format is text of printable
        characters, whole UTF-8 sequences, at most SETTING_TRAY_MOST bytes; one that is none of that, or names what is
        not here, is refused where it is set.
*/
static const char *const setting_tray_names[] = {
    "battery", "battery.percent", "battery.state", "time.24h.hour", "time.24h.min", "time.12h.hour", "time.ampm",
    "date.year", "date.month", "date.day", "date.weekday", "date.monthname", "cpu.load", "mem.used", "temp", "host", null};

// A name's number among the tray's, -1 when it is none.
static int setting_tray_name(const char *name, positive length)
{
        for (int at = 0; setting_tray_names[at]; at++)
        {
                if (string_length((string_address)setting_tray_names[at]) == length &&
                    !memory_compare((address_any)setting_tray_names[at], (address_any)name, length))
                        return at;
        }
        return -1;
}

// Whether text is a format the tray takes. When it is not and bad is given, bad is where the fault starts.
static bool setting_tray_valid(const char *text, positive *bad)
{
        positive at = 0, length = string_length((string_address)text);

        if (bad)
                *bad = 0;
        if (length > SETTING_TRAY_MOST)
                return bad ? (*bad = SETTING_TRAY_MOST, false) : false;
        while (text[at])
        {
                unsigned char byte = (unsigned char)text[at];
                positive follow = 0;

                if (byte < 0x20 || byte == 0x7f)
                        goto wrong;
                if (byte >= 0xc2 && byte < 0xe0)
                        follow = 1;
                else if (byte >= 0xe0 && byte < 0xf0)
                        follow = 2;
                else if (byte >= 0xf0 && byte < 0xf5)
                        follow = 3;
                else if (byte >= 0x80)
                        goto wrong;
                if (follow)
                {
                        /*
                                The whole sequence, judged as memory_utf8_span
                                judges it: an overlong form (E0 needs A0 or more
                                after it, F0 needs 90), a surrogate (ED needs 9F or
                                less) and anything past U+10FFFF (F4 needs 8F or
                                less) are not sequences, however well the bytes
                                after the lead continue. A sequence the end of the
                                text cuts off is not one either.
                        */
                        if (at + follow >= length ||
                            memory_utf8_valid_span((address_any)(text + at), follow + 1).x != follow + 1)
                                goto wrong;
                        at += follow + 1;
                        continue;
                }
                if (byte == '{' && text[at + 1] == '{')
                        at += 2;
                else if (byte == '}' && text[at + 1] == '}')
                        at += 2;
                else if (byte == '{')
                {
                        string_address end_brace = string_first_of_or_end((string_address)(text + at + 1), '}');
                        positive close = (positive)(end_brace - (string_address)text);

                        if (!text[close] || setting_tray_name(text + at + 1, close - at - 1) < 0)
                                goto wrong;
                        at = close + 1;
                }
                else if (byte == '}')
                        goto wrong;
                else
                        at++;
        }
        return true;
wrong:
        if (bad)
                *bad = at;
        return false;
}

/*
        The taskbar's pinned programs: a line of entries, each an app_id, the executable the program ran as when it was pinned (for the
        record) and the words it is started with (argv, the first an absolute path), the entries ended by a lone ;, as in
            org.mozilla.firefox /usr/lib/firefox/firefox /bowls/bin/firefox --new-window ; settings /settings /settings
        A word is made of parts that touch: a run of letters, digits and _ . / : = @ % + , -, a piece in single quotes (any bytes but a quote,
        none below a blank or the delete one), and \' for a quote; so a line is read as a shell reads single quotes, though nothing here is
        run by one. setting_pins_next takes the next word of a line, or the separator, or finds the fault; setting_pins_valid judges a line
        whole (at most SETTING_PINS_MOST bytes and SETTING_PINS_ENTRIES entries, an entry of 3 to SETTING_PINS_WORDS words whose app_id is
        letters, digits and _ . + - up to WINDOWS_APP_ID - 1 and whose executable and first word are absolute); setting_pins_quote writes
        a word in the form that reads back as it is.
*/
#define SETTING_PINS_END 0
#define SETTING_PINS_WORD_NEXT 1
#define SETTING_PINS_SEPARATOR 2
#define SETTING_PINS_BAD 3

static bool setting_pins_bare(unsigned char byte)
{
    return byte_is_alnum(byte) || byte == '_' || byte == '.' ||
           byte == '/' || byte == ':' || byte == '=' || byte == '@' || byte == '%' || byte == '+' || byte == ',' || byte == '-';
}

// The next word of line from *at into word (room bytes, a null after it) and where it ends in *at: SETTING_PINS_WORD_NEXT, or SETTING_PINS_SEPARATOR
// for a lone ;, SETTING_PINS_END at the end of the line, SETTING_PINS_BAD for what is none of that (*at is then at the fault).
static int setting_pins_next(const char *line, positive *at, char *word, positive room)
{
    positive used = 0;
    bool any = false;

    while (line[*at] == ' ')
        (*at)++;
    if (!line[*at])
        return SETTING_PINS_END;
    if (line[*at] == ';' && (line[*at + 1] == ' ' || !line[*at + 1]))
        return (*at)++, SETTING_PINS_SEPARATOR;
    while (line[*at] && line[*at] != ' ')
    {
        unsigned char byte = (unsigned char)line[*at];

        if (byte == '\'')
        {
            (*at)++;
            while (line[*at] && line[*at] != '\'')
            {
                if ((unsigned char)line[*at] < 0x20 || (unsigned char)line[*at] == 0x7f || used + 1 >= room)
                    return SETTING_PINS_BAD;
                word[used++] = line[(*at)++];
            }
            if (!line[*at])
                return SETTING_PINS_BAD;
            (*at)++;
        }
        else if (byte == '\\' && line[*at + 1] == '\'')
        {
            if (used + 1 >= room)
                return SETTING_PINS_BAD;
            word[used++] = '\'', *at += 2;
        }
        else if (setting_pins_bare(byte) && used + 1 < room)
            word[used++] = (char)byte, (*at)++;
        else
            return SETTING_PINS_BAD;
        any = true;
    }
    word[used] = 0;
    return any ? SETTING_PINS_WORD_NEXT : SETTING_PINS_BAD;
}

// Whether line is what `taskbar pinned` keeps. When it is not and bad is given, bad is where the fault starts.
static bool setting_pins_valid(const char *line, positive *bad)
{
    char word[SETTING_PINS_WORD];
    positive at = 0, length = string_length((string_address)line), words = 0, entries = 0, here;
    int kind;

    if (bad)
        *bad = 0;
    if (length > SETTING_PINS_MOST)
        return bad ? (*bad = SETTING_PINS_MOST, false) : false;
    for (;;)
    {
        here = at;
        kind = setting_pins_next(line, &at, word, sizeof(word));
        if (kind == SETTING_PINS_BAD)
            goto wrong;
        if (kind == SETTING_PINS_WORD_NEXT)
        {
            if (words == 0)
                for (positive each = 0; word[each] || !each; each++)
                {
                    unsigned char byte = (unsigned char)word[each];

                    if (!(setting_pins_bare(byte) && byte != '/' && byte != ':' && byte != '=' && byte != '@' && byte != '%' && byte != ',') || each > 62)
                        goto wrong;
                }
            else if ((words == 1 || words == 2) && word[0] != '/')
                goto wrong;
            if (++words > SETTING_PINS_WORDS)
                goto wrong;
            continue;
        }
        // The end of an entry: a separator, or the end of the line. An entry has its app_id, its executable and a first word at least.
        if (words)
        {
            if (words < 3 || ++entries > SETTING_PINS_ENTRIES)
                goto wrong;
            words = 0;
        }
        else if (kind == SETTING_PINS_SEPARATOR)
            goto wrong;
        if (kind == SETTING_PINS_END)
            return true;
    }
wrong:
    if (bad)
        *bad = here;
    return false;
}

// Word appended to into (at its end *at, room bytes in all) in the form setting_pins_next reads back as it is: bare where it can be,
// else in single quotes with a quote written \'. False, with into as it was, for a word with a byte below a blank or the delete one, or no room.
static bool setting_pins_quote(char *into, positive *at, positive room, const char *word)
{
    positive used = *at;
    bool bare = word[0] != 0;

    for (positive each = 0; word[each]; each++)
    {
        unsigned char byte = (unsigned char)word[each];

        if (byte < 0x20 || byte == 0x7f)
            return false;
        bare &= setting_pins_bare(byte);
    }
    if (used)
    {
        if (used + 1 >= room)
            return false;
        into[used++] = ' ';
    }
    if (bare)
    {
        positive length = string_length((string_address)word);

        if (length >= room - used)
            return false;
        memory_copy_apart(into + used, (address_any)word, length);
        used += length;
    }
    else
    {
        if (used + 1 >= room)
            return false;
        into[used++] = '\'';
        for (positive each = 0; word[each]; each++)
        {
            positive need = word[each] == '\'' ? 4 : 1;

            if (used + need + 2 > room)
                return false;
            if (word[each] == '\'')
                into[used++] = '\'', into[used++] = '\\', into[used++] = '\'', into[used++] = '\'';
            else
                into[used++] = word[each];
        }
        into[used++] = '\'';
    }
    into[used] = 0;
    *at = used;
    return true;
}

/*
        The programs the desktop opens when it starts (`desktop autostart`): a line of absolute paths, a blank between each, as in
            /klog /terminal /bowls/bin/firefox
        A path is letters, digits and _ . / + -, starts with a / and is more than it, and has no empty part and no part that is . or ..; a
        line names one of them or more, none twice. There is no cap on how many or how long: a line is what the memory holds. An empty line
        is no line: none is kept as the word it is, by the verb. setting_autostart_next finds the next path of a line in place (nothing is
        copied, so no buffer bounds it) and setting_autostart_has finds one in it.
*/
static bool setting_autostart_bare(unsigned char byte)
{
    return byte_is_alnum(byte) || byte == '_' || byte == '.' ||
           byte == '/' || byte == '+' || byte == '-';
}

// The next word of line from *at: where it starts, its length in *length and *at after it; none at the end of the line.
static const char *setting_autostart_next(const char *line, positive *at, positive *length)
{
    positive from;

    while (line[*at] == ' ')
        (*at)++;
    if (!line[*at])
        return null;
    for (from = *at; line[*at] && line[*at] != ' '; (*at)++)
        ;
    *length = *at - from;
    return line + from;
}

// Whether two words, each where it starts and its length, are the same.
static bool setting_autostart_same(const char *one, positive one_length, const char *other, positive other_length)
{
    return one_length == other_length && !memory_compare((address_any)one, (address_any)other, one_length);
}

// Whether the part of a path from after one slash to before the next (from, to) is empty, . or ..
static bool setting_autostart_part_bad(const char *word, positive from, positive to)
{
    return to == from || (to == from + 1 && word[from] == '.') || (to == from + 2 && word[from] == '.' && word[from + 1] == '.');
}

// Whether a word, where it starts and its length, is a path a line may name.
static bool setting_autostart_span(const char *word, positive length)
{
    positive part = 1;

    if (length < 2 || word[0] != '/')
        return false;
    for (positive at = 1; at < length; at++)
    {
        if (!setting_autostart_bare((unsigned char)word[at]))
            return false;
        if (word[at] == '/')
        {
            if (setting_autostart_part_bad(word, part, at))
                return false;
            part = at + 1;
        }
    }
    return !setting_autostart_part_bad(word, part, length);
}

// Whether word, a string, is a path a line may name.
static bool setting_autostart_path(const char *word)
{
    return setting_autostart_span(word, string_length((string_address)word));
}

// Whether line names word.
static bool setting_autostart_has(const char *line, const char *word)
{
    positive at = 0, length, want = string_length((string_address)word);
    const char *each;

    while ((each = setting_autostart_next(line, &at, &length)))
        if (setting_autostart_same(each, length, word, want))
            return true;
    return false;
}

// Whether line is what `desktop autostart` keeps. When it is not and bad is given, bad is where the fault starts.
static bool setting_autostart_valid(const char *line, positive *bad)
{
    positive at = 0, entries = 0, length;
    const char *word;

    if (bad)
        *bad = 0;
    while ((word = setting_autostart_next(line, &at, &length)))
    {
        positive from = 0, before_length;
        const char *before;
        bool twice = false;

        // None twice: each word before this one is another.
        while (!twice && (before = setting_autostart_next(line, &from, &before_length)) && before < word)
            twice = setting_autostart_same(before, before_length, word, length);
        if (twice || !setting_autostart_span(word, length))
        {
            if (bad)
                *bad = (positive)(word - line);
            return false;
        }
        entries++;
    }
    return entries > 0;
}

/*
        One line of desktop startup (its bytes, no line's end) taken apart: on or off, an app's id or none, and the command. False for a line
        that is no entry: another first word, an id with a byte no registry id has, or no command. Nothing is copied, so no buffer bounds it.
*/
struct setting_startup
{
    bool on, app;
    const char *id, *command;
    positive id_length, command_length;
};

static bool setting_startup_id_byte(unsigned char byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
           byte == '-' || byte == '+' || byte == '@';
}

static bool setting_startup_entry(const char *line, positive length, struct setting_startup *e)
{
    positive at;

    e->id = e->command = 0, e->id_length = e->command_length = 0, e->app = false;
    if (length >= 3 && !memory_compare((address_any)line, (address_any) "on ", 3))
        e->on = true, at = 3;
    else if (length >= 4 && !memory_compare((address_any)line, (address_any) "off ", 4))
        e->on = false, at = 4;
    else
        return false;
    if (length - at >= 4 && !memory_compare((address_any)(line + at), (address_any) "app ", 4))
    {
        e->app = true, at += 4, e->id = line + at;
        while (at < length && line[at] != ' ' && setting_startup_id_byte((unsigned char)line[at]))
            at++;
        e->id_length = (positive)(line + at - e->id);
        if (!e->id_length || at >= length || line[at] != ' ')
            return false;
        at++;
    }
    else if (length - at >= 8 && !memory_compare((address_any)(line + at), (address_any) "command ", 8))
        at += 8;
    else
        return false;
    e->command = line + at, e->command_length = length - at;
    for (positive k = at; k < length; k++)
        if (line[k] == '\n' || line[k] == '\r' || !line[k])
            return false;
    return e->command_length > 0;
}

/*
        Each Scene's wallpaper, the words of it as the compositor, the command and the window read them (SETTING_WALLPAPER_*). A picture's
        name is letters, digits and dashes, lower case, 1 to SETTING_WALLPAPER_NAME - 1 of them; a word is auto, dark, white, picture:NAME,
        curated:HEX, the 64 hex digits of the digest of one of the feed's, or colour:RRGGBB, a plain colour in six hex digits, lower case.
*/
static bool setting_wallpaper_name_ok(const char address_to name, positive length)
{
        if (!length || length >= SETTING_WALLPAPER_NAME || name[0] == '-')
                return false;
        for (positive at = 0; at < length; at++)
                if (!((name[at] >= 'a' && name[at] <= 'z') || (name[at] >= '0' && name[at] <= '9') || name[at] == '-'))
                        return false;
        return true;
}

static bool setting_wallpaper_word_ok(const char address_to word, positive length)
{
        if ((length == 4 && !memory_compare(word, "auto", 4)) || (length == 4 && !memory_compare(word, "dark", 4)) ||
            (length == 5 && !memory_compare(word, "white", 5)))
                return true;
        if (length > 8 && !memory_compare(word, "picture:", 8))
                return setting_wallpaper_name_ok(word + 8, length - 8);
        if (!(length == 8 + 64 && !memory_compare(word, "curated:", 8)) && !(length == 7 + 6 && !memory_compare(word, "colour:", 7)))
                return false;
        for (positive at = word[0] == 'c' && word[1] == 'o' ? 7 : 8; at < length; at++)
                if (!((word[at] >= '0' && word[at] <= '9') || (word[at] >= 'a' && word[at] <= 'f')))
                        return false;
        return true;
}

// A word's plain colour, 0xrrggbb: -1 for any word that is not colour:RRGGBB as setting_wallpaper_word_ok takes it.
static long setting_wallpaper_colour(const char address_to word)
{
        positive length = word ? string_length((string_address)word) : 0;
        long colour = 0;

        if (length != 13 || memory_compare(word, "colour:", 7) || !setting_wallpaper_word_ok(word, length))
                return -1;
        for (positive at = 7; at < 13; at++)
                colour = colour << 4 | (word[at] <= '9' ? word[at] - '0' : word[at] - 'a' + 10);
        return colour;
}

// The word of a plain colour (0xrrggbb, its low 24 bits) into word, SETTING_WALLPAPER_WORD bytes or more: colour:rrggbb, lower case.
static fn setting_wallpaper_colour_word(unsigned int colour, char address_to word)
{
        static const char digits[] = "0123456789abcdef";

        memory_copy(word, "colour:", 7);
        for (int at = 0; at < 6; at++)
                word[7 + at] = digits[colour >> (20 - 4 * at) & 15];
        word[13] = 0;
}

// The file of a word's picture (SETTING_WALLPAPER_DIR/NAME or curated/HEX, then suffix: .png, .jpg, .mini): false for the moon's words.
static bool setting_wallpaper_file(const char address_to word, const char address_to suffix, char address_to path, positive room)
{
        positive length = string_length((string_address)word);
        bool curated = length > 8 && !memory_compare(word, "curated:", 8);

        if (!setting_wallpaper_word_ok(word, length) || (memory_compare(word, "picture:", 8) && !curated) ||
            sizeof(SETTING_WALLPAPER_DIR "/curated/") + length + string_length((string_address)suffix) > room)
                return false;
        string_copy((string_address)path, (string_address)(curated ? SETTING_WALLPAPER_DIR "/curated/" : SETTING_WALLPAPER_DIR "/"));
        string_append_bounded((string_address)path, (string_address)word + 8, room);
        string_append_bounded((string_address)path, (string_address)suffix, room);
        return true;
}

/*
        A curated wallpaper's source page. The page is known for a picture of Wikimedia Commons alone: its original's address,
        https://upload.wikimedia.org/wikipedia/commons/X/XY/NAME, is the page https://commons.wikimedia.org/wiki/File:NAME, NAME the last
        segment kept byte for byte (it is escaped as Commons escapes it). Any other address has no page: the card says the author and the
        licence with no button. A page is held to the feed's rules for an address wherever it is read: https, a host of letters, digits, dots
        and dashes, a path that starts with a slash and has no segment of dots alone, its bytes printable ASCII with no blank, double quote,
        angle bracket, backslash, backquote or fragment, and shorter than SETTING_WALLPAPER_SOURCE.
*/
#define SETTING_WALLPAPER_COMMONS_FILE "https://upload.wikimedia.org/wikipedia/commons/"
#define SETTING_WALLPAPER_COMMONS_PAGE "https://commons.wikimedia.org/wiki/File:"
static bool setting_wallpaper_source_ok(const char address_to text, positive length)
{
        positive at = 8, host;

        if (length <= 8 || length >= SETTING_WALLPAPER_SOURCE || memory_compare(text, "https://", 8))
                return false;
        for (host = at; at < length && text[at] != '/'; at++)
                if (!((text[at] >= 'a' && text[at] <= 'z') || (text[at] >= '0' && text[at] <= '9') || text[at] == '.' || text[at] == '-'))
                        return false;
        if (at == host || at >= length || text[host] == '.' || text[at - 1] == '.')
                return false;
        for (positive segment = at; at < length; at++)
        {
                char c = text[at];

                if (c <= ' ' || c >= 0x7f || c == '"' || c == '<' || c == '>' || c == '\\' || c == '#' || c == '`')
                        return false;
                if (c == '/' || at + 1 == length)
                {
                        positive stop = c == '/' ? at : at + 1, from = segment + 1;
                        bool dots = stop > from;

                        for (positive k = from; k < stop; k++)
                                dots &= text[k] == '.';
                        if (dots && stop - from <= 2)
                                return false;
                        segment = at;
                }
        }
        return true;
}

// The source page of a picture's original address (length bytes, not ended) into page, room bytes: false when it has none or it is no page.
static bool setting_wallpaper_source(const char address_to url, positive length, char address_to page, positive room)
{
        positive prefix = sizeof(SETTING_WALLPAPER_COMMONS_FILE) - 1, name = length, made = sizeof(SETTING_WALLPAPER_COMMONS_PAGE) - 1;

        page[0] = 0;
        if (length <= prefix || memory_compare(url, SETTING_WALLPAPER_COMMONS_FILE, prefix))
                return false;
        while (name > prefix && url[name - 1] != '/')
                name--;
        if (name >= length || made + length - name >= room || made + length - name >= SETTING_WALLPAPER_SOURCE)
                return false;
        memory_copy(page, SETTING_WALLPAPER_COMMONS_PAGE, made);
        memory_copy(page + made, url + name, length - name);
        page[made + length - name] = 0;
        if (setting_wallpaper_source_ok(page, made + length - name))
                return true;
        page[0] = 0;
        return false;
}

// Whether a word's moon is the dark one: dark, or auto in a dark theme (white and auto in a light one are the ink on paper).
static bool setting_wallpaper_dark(const char address_to word, bool theme_dark)
{
        return string_equals((string_address)word, "dark") || (string_equals((string_address)word, "auto") && theme_dark);
}

/*
        The next line of the Scenes' words (SETTING_WALLPAPER_SCENES_PATH) from text[*at]: the screen's name, the Scene's number and the
        word, each as it may be; false at the text's end. A line that is none of that is passed over, so a word written by hand wrong is
        the Scene's auto and nothing else.
*/
static bool setting_wallpaper_line(const char address_to text, positive size, positive address_to at, char address_to screen,
                                   unsigned int address_to scene, char address_to word)
{
        while (*at < size)
        {
                positive start = *at, stop = start, field[3][2], count = 0, digit;
                unsigned int number = 0;

                while (stop < size && text[stop] != '\n')
                        stop++;
                *at = stop + (stop < size);
                for (positive k = start; k < stop && count < 4;)
                {
                        positive from;

                        while (k < stop && text[k] == ' ')
                                k++;
                        if (k >= stop)
                                break;
                        from = k;
                        while (k < stop && text[k] != ' ')
                                k++;
                        if (count < 3)
                                field[count][0] = from, field[count][1] = k - from;
                        count++;
                }
                if (count != 3 || !field[0][1] || field[0][1] >= WINDOWS_OUTPUT_NAME || field[1][1] > 4 ||
                    !setting_wallpaper_word_ok(text + field[2][0], field[2][1]))
                        continue;
                for (digit = 0; digit < field[1][1] && text[field[1][0] + digit] >= '0' && text[field[1][0] + digit] <= '9'; digit++)
                        number = number * 10 + (unsigned int)(text[field[1][0] + digit] - '0');
                if (digit != field[1][1] || !number)
                        continue;
                memory_copy(screen, text + field[0][0], field[0][1]), screen[field[0][1]] = end;
                memory_copy(word, text + field[2][0], field[2][1]), word[field[2][1]] = end;
                *scene = number;
                return true;
        }
        return false;
}

/*
        A picture's miniature (NAME.mini beside NAME.png or NAME.jpg, made by `canvas wallpaper picture`): "MWMI", its width and height (little end
        first, each 1 to SETTING_WALLPAPER_MINI) and four bytes of none, then its pixels, opaque, a row after a row. The window and the
        compositor read it, never the picture, for a thumbnail: the picture is megabytes to unpack, this a few hundred kilobytes to read.
        Answers its pixels (memory_free them with *bytes), null when it is not there or not one.
*/
#define SETTING_WALLPAPER_MINI_HEAD 16u
static unsigned int address_to setting_wallpaper_mini_read(const char address_to path, int address_to width, int address_to height,
                                                           positive address_to bytes)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        p8 head[SETTING_WALLPAPER_MINI_HEAD];
        unsigned int address_to pixels = null, w, h;
        positive size = 0, got = 0;

        if (handle < 0)
                return null;
        if (system_read_once(handle, head, sizeof(head)) == (bipolar)sizeof(head) && !memory_compare(head, "MWMI", 4))
        {
                w = memory_load_unaligned(p32, head + 4), h = memory_load_unaligned(p32, head + 8);
                if (w && h && w <= SETTING_WALLPAPER_MINI && h <= SETTING_WALLPAPER_MINI)
                        size = (positive)w * h * 4, pixels = (unsigned int address_to)memory_checked(size);
                while (pixels && got < size)
                {
                        bipolar read = system_read_once(handle, (p8 address_to)pixels + got, size - got);

                        if (read <= 0)
                                break;
                        got += (positive)read;
                }
                if (pixels && got == size)
                        *width = (int)w, *height = (int)h, *bytes = size;
                else if (pixels)
                        memory_free(pixels, size), pixels = null;
        }
        system_close(handle);
        return pixels;
}

#endif // MOONWATER_SETTINGS_REGISTRY
