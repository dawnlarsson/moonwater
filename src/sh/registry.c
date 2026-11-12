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

// How a row's value is taken from the line it is on.
#define SETTINGS_LINE 1u  // the value is the rest of the line and not a word
#define SETTINGS_SPLIT 2u // what is set is the value's words, one each, and not one word
#define SETTINGS_TICK 4u  // the value changes by itself: it is asked again while its group is shown
#define SETTINGS_INK 8u   // a choice of named colours: the verb says rrggbb, and a name is what is shown and chosen
#define SETTINGS_BATTERIES 16u // a choice of the batteries the machine has: auto, each name from the kernel, and none
#define SETTINGS_LANGUAGE 32u  // a choice of the languages: the verb says the code and the menu shows the language's own name (ui/ui.c)
#define SETTINGS_WAITS 64u     // a verb that waits for another machine (a pairing, up to five minutes): its first line is shown while it waits, its last when it ends
#define SETTINGS_TRANSPORT 128u // a choice of how linked machines reach each other: network always, modem only while the audio modem is ready (its row says so)

// The Display rows of a screen (settings.c draws the map above them): the screen chosen, its rotation and refresh (the
// window table says what each is, and the verb sets it), and a note of who turns it.
#define SETTINGS_OUT_NONE 0u
#define SETTINGS_OUT_SCREEN 1u
#define SETTINGS_OUT_TURN 2u
#define SETTINGS_OUT_REFRESH 3u
#define SETTINGS_OUT_NOTE 4u
#define SETTINGS_OUT_SCALE 5u
#define SETTINGS_OUT_NOTICES 6u // the kernel's own notices: the value is their count, and the row's lines are each one (settings.c)

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
#define SETTING_CURSOR_CLICK_PATH "/root/canvas.cursor.click"
#define SETTING_REPEAT_DELAY_PATH "/root/keyboard.repeat.delay"
#define SETTING_ONSCREEN_PATH "/root/keyboard.onscreen"
#define SETTING_REPEAT_RATE_PATH "/root/keyboard.repeat.rate"
#define SETTING_SENSITIVITY_PATH "/root/mouse.sensitivity"
#define SETTING_ACCELERATION_PATH "/root/mouse.acceleration"
#define SETTING_TASKBAR_POSITION_PATH "/root/taskbar.position"
#define SETTING_TASKBAR_DISPLAY_PATH "/root/taskbar.display"
#define SETTING_TASKBAR_SCALE_PATH "/root/taskbar.scale"
#define SETTING_TASKBAR_PADDING_PATH "/root/taskbar.padding"
#define SETTING_TASKBAR_TEXT_PATH "/root/taskbar.text"
#define SETTING_TASKBAR_LAUNCHER_PATH "/root/taskbar.launcher"
#define SETTING_TASKBAR_FULLSCREEN_PATH "/root/taskbar.fullscreen"
#define SETTING_WINDOWS_REMEMBER_PATH "/root/canvas.windows.remember"
#define SETTING_TOUCH_PATH "/root/canvas.touch"
#define SETTING_TOUCH_LEFT_PATH "/root/canvas.touch.left"
#define SETTING_TOUCH_RIGHT_PATH "/root/canvas.touch.right"
#define SETTING_PREDICT_TOUCH_PATH "/root/canvas.predict.touch"
#define SETTING_CLIPBOARD_PATH "/root/canvas.clipboard"
#define SETTING_GPU_PATH "/root/canvas.gpu"
#define SETTING_CLIPBOARD_HISTORY_PATH "/root/canvas.clipboard.history"
#define SETTING_CLIPBOARD_LIMIT_PATH "/root/canvas.clipboard.limit"
// The sound server's (src/audio/service.c reads the first three again twice a second and writes the volume and the mute back when
// a program changes them; the machine script reads the last when it starts the server).
#define SETTING_AUDIO_VOLUME_PATH "/root/audio.volume"
#define SETTING_AUDIO_MUTE_PATH "/root/audio.mute"
#define SETTING_AUDIO_DEVICE_PATH "/root/audio.device"
#define SETTING_AUDIO_SERVER_PATH "/root/audio.server"
// And the recording half, read the same way: whether programs may record at all (absent is on), the microphone they are given by name,
// and that microphone's volume and mute.
#define SETTING_AUDIO_CAPTURE_PATH "/root/audio.capture"
#define SETTING_AUDIO_SOURCE_PATH "/root/audio.source"
#define SETTING_AUDIO_SOURCE_VOLUME_PATH "/root/audio.source.volume"
#define SETTING_AUDIO_SOURCE_MUTE_PATH "/root/audio.source.mute"
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
#define SETTING_TASKBAR_TRAY_DATE_YMD "  {date.year}-{date.month}-{date.day}"
#define SETTING_TASKBAR_TRAY_DATE_DMY "  {date.day}.{date.month}.{date.year}"
#define SETTING_TASKBAR_TRAY_DATE_MDY "  {date.month}/{date.day}/{date.year}"
#define SETTING_TRAY_MOST 96 // bytes of a format
#define SETTING_THEME_PATH "/root/canvas.theme"
// The theme `auto` is light from this hour of the local clock to the other, and dark outside them: the compositor's windows
// and the session bus's color-scheme (src/dbus/service.c) take it from here, so that a program of a bowl turns with them.
#define SETTING_THEME_DAY_FROM 7
#define SETTING_THEME_DAY_UNTIL 19
#define SETTING_ACCENT_PATH "/root/canvas.theme.accent"
#define SETTING_MOTION_PATH "/root/canvas.motion"
#define SETTING_WHEEL_SMOOTH_PATH "/root/canvas.wheel.smooth"
#define SETTING_TRACKPAD_SPEED_PATH "/root/canvas.trackpad.speed"
#define SETTING_SCROLL_NATURAL_PATH "/root/canvas.scroll.natural"
#define SETTING_SCROLL_MOMENTUM_PATH "/root/canvas.scroll.momentum"
#define SETTING_SCROLL_RUBBER_PATH "/root/canvas.scroll.rubber"
#define SETTING_CARET_BLINK_PATH "/root/canvas.caret.blink"
#define SETTING_CARET_THICKNESS_PATH "/root/canvas.caret.thickness"
// The text's size, in percent of what every role's size says: ui.c's ui_em multiplies it in, and the portal says it as text-scaling-factor.
#define SETTING_TEXT_SCALE_PATH "/root/canvas.text.scale"
#define SETTING_BORDER_WIDTH_PATH "/root/canvas.border.width"
// The language the windows Canvas draws itself speak: the code of a file in /usr/share/moonwater/language (ui/ui.c reads it).
// These are the codes the Language row offers, in the order of its menu; a check keeps them the languages/ directory's.
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
};

/*
        The rows. A group (section) starts at the row that names it and holds
        the rows after it; the order is the window's, and the main page of the
        command shows the rows of a verb in this order too. The labels are
        what the window and its lane find a row by.

        The groups, most used first: Machine (its name), Canvas (how the
        desktop is drawn and what its windows keep), Display (the screens),
        Input (the keyboard, the mouse, the pointer and touch), Taskbar (the
        panel), Time (the clock, its zone and where it is set from), Network, Wi-Fi,
        Bluetooth, Devices, Power and Boot. A row is in the group a person looks
        for it in, by what it does and not by the program that applies it: the
        pointer's size is an Input row though the compositor draws it.
*/
#define SETTING_UNWRAP(...) __VA_ARGS__
// A plain row: group, label, window kind and flags, verb words in brackets, key, choices, then the rest by name.
#define SETTING_PLAIN_ROW(section_, label_, kind_, flags_, verb_, key_, choices_, ...)                                \
        {section_, label_, kind_, 0, flags_, {SETTING_UNWRAP verb_}, key_, null, {SETTING_UNWRAP verb_}, choices_, 0, \
         .how = SETTING_PLAIN, __VA_ARGS__}

static const struct settings_row settings_rows[] = {
    {"Machine", "Name", SETTINGS_FIELD, 0, SETTINGS_LINE, {"name"}, "", null, {"name"}, null, 0,
     .usage = "name [NEW|random]", .help = "what this machine is called, like space-wizard"},
    //  What the system's own windows say, in which language: one file of languages/ read by every window of the kit, taken
    //  from the window table so a change reaches them at once. Bare, the verb adds what is installed and which are machine-made.
    SETTING_PLAIN_ROW(null, "Language", SETTINGS_CHOICE, SETTINGS_LANGUAGE, ("language"), "language ", SETTINGS_LANGUAGE_CODES,
                      .path = SETTING_LANGUAGE_PATH, .standard = "en", .say = "language", .need = "the language",
                      .refusal = "language is one of " SETTINGS_LANGUAGE_CODES ", not %s\n",
                      .usage = "language [CODE|default]",
                      .help = "the language the windows Canvas draws itself speak; bare lists them, machine-translated ones marked [en]"),
    SETTING_PLAIN_ROW("Canvas", "Background", SETTINGS_CHOICE, SETTINGS_INK, ("canvas", "background"), "background ",
                      "default black dark-grey slate midnight-blue forest plum light-grey white",
                      .type = SETTING_COLOUR, .path = SETTING_BACKGROUND_PATH, .standard = "#1e1e1e", .reset = "default",
                      .refusal = "background is a colour like 1e1e1e, or default, not %s\n",
                      .usage = "canvas background [COLOUR|default]", .help = "the desktop colour, rrggbb [1e1e1e]"),
    {null, "Background colour (rrggbb or default)", SETTINGS_FIELD, 0, 0, {"canvas", "background"}, "background ", null,
     {"canvas", "background"}, null, 0, .how = SETTING_ALIAS},
    //  The theme of Canvas's own windows (ui.c's kit): light or dark, or auto by the clock, and the accent its controls
    //  fill with; the compositor puts them in the window table and every window turns to them at once.
    SETTING_PLAIN_ROW(null, "Theme", SETTINGS_CHOICE, 0, ("canvas", "theme"), "theme ", "light dark auto",
                      .path = SETTING_THEME_PATH, .standard = "dark", .reset = "dark",
                      .refusal = "theme is light, dark or auto, not %s\n",
                      .usage = "canvas theme [light|dark|auto]",
                      .help = "the colours of the windows Canvas draws itself; auto is light from 7 to 19 by the clock [dark]"),
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
                      .help = "how wide the highlight on a window's edge or corner is when the pointer is over it or the hand holds it, 1 to 4 pixels [2]"),
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
#ifndef SHELL_NO_GPU
    //  Canvas draws and composes on the GPU where it has a driver for it (src/canvas/gpu.c) and on the processor where it
    //  has none. Alpha: off by default, so every pixel stays on the processor until the user turns it on. Not in the image
    //  when the build says CONFIG_MOONWATER_GPU=n.
    SETTING_PLAIN_ROW(null, "GPU rendering (alpha)", SETTINGS_SWITCH, 0, ("canvas", "gpu"), "gpu ", null,
                      .path = SETTING_GPU_PATH, .standard = "off",
                      .usage = "canvas gpu [on|off]",
                      .help = "draw and compose on the GPU where Canvas has a driver for it, on the processor elsewhere; alpha [off]"),
#endif
    SETTING_PLAIN_ROW(null, "Clipboard history (copies kept)", SETTINGS_SLIDER, 0, ("canvas", "clipboard", "history"), "history ", null,
                      .type = SETTING_NUMBER, .path = SETTING_CLIPBOARD_HISTORY_PATH,
                      .standard = "0", .unit = "copies", .low = 0, .high = 32,
                      .usage = "canvas clipboard history [N|default]",
                      .help = "copies the compositor keeps in memory, never on a disk, 0 to 32 [0]"),
    SETTING_PLAIN_ROW(null, "Clipboard limit (MiB)", SETTINGS_SLIDER, 0, ("canvas", "clipboard", "limit"), "limit ", null,
                      .type = SETTING_NUMBER, .path = SETTING_CLIPBOARD_LIMIT_PATH,
                      .standard = "16", .unit = "MiB", .low = 1, .high = 16,
                      .usage = "canvas clipboard limit [MIB|default]",
                      .help = "the most one copy may be, 1 to 16 MiB [16]"),
    //  The splash: the picture on the desktop's background while the desktop loads. Its verb is host.c's (host_splash), which
    //  keeps the copy; this row says what is kept, and no key changes it here. It stays in the Canvas group, so it has no header.
    {null, "Splash", SETTINGS_INFO, 0, SETTINGS_LINE, {"splash"}, "", null, {null}, null, 0,
     .usage = "splash [FILE|default|off]",
     .help = "the picture Canvas shows while the desktop loads: a binary PPM of at most 1 MiB, the built-in one, or off"},
    //  The Display group: the map above these rows (settings.c) shows the screens; the rows are for the screen chosen on it.
    //  The rotation and refresh are the table's, and each change is `canvas display NAME ...` run with the screen's name.
    {"Display", "Screen", SETTINGS_CHOICE, 0, 0, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "the screen the rows under the map are for", .output = SETTINGS_OUT_SCREEN},
    {null, "Rotation", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "which way up the screen is", .output = SETTINGS_OUT_TURN},
    {null, "Turned by the screen", SETTINGS_INFO, 0, 0, {null}, null, null, {null}, null, 0,
     .help = "who turns the picture: the screen itself, or Moonwater (slower)", .output = SETTINGS_OUT_NOTE},
    {null, "Refresh rate", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "how many times a second the screen is drawn: the rates it supports, the best marked", .output = SETTINGS_OUT_REFRESH},
    {null, "Scale of this screen", SETTINGS_CHOICE, 0, SETTINGS_SPLIT, {"canvas", "display"}, null, null, {"canvas", "display"}, null, 0,
     .help = "how big the screen draws things, in percent of its size: 50, 75, 100, 125, 150 or 200 [100]", .output = SETTINGS_OUT_SCALE},
    {null, "Screen modes", SETTINGS_CHOICE, 0, 0, {"canvas", "modes"}, "modes ", null, {"canvas", "modes"}, "largest preferred", 0,
     .usage = "canvas modes [largest|preferred]", .help = "which mode a screen is driven at [largest]"},
    {null, "Brightness", SETTINGS_STEP, 1, 0, {"brightness"}, "brightness ", null, {"brightness"}, "-10 +10", 0,
     .usage = "brightness [N%|+N|-N]", .help = "the screen backlight"},
    //  The menu names each layout once; gb, sv and nb, which the help names as well, are the same layouts as uk, se
    //  and no (locale_keyboards in host.c, the compositor's table), kept as typed, so the menu leaves them out.
    {"Input", "Keyboard", SETTINGS_CHOICE, 0, 0, {"keyboard"}, "keyboard ", null, {"keyboard"}, "us uk de se no dk fi fr es it ar il ir", 0,
     .usage = "keyboard [LAYOUT|list|xkb]", .help = "us uk gb de se sv no nb dk fi fr es it ar il ir [us]"},
    //  The keyboard on the screen (src/canvas/compositor.c, the osk window): shown while a text field has the keys and there is no
    //  keyboard to type on (auto), never (off), or whenever a field has them (on).
    SETTING_PLAIN_ROW(null, "On-screen keyboard", SETTINGS_CHOICE, 0, ("keyboard", "onscreen"), "onscreen ", "auto off on",
                      .need = "", .path = SETTING_ONSCREEN_PATH, .standard = "auto", .reset = "auto",
                      .refusal = "the on-screen keyboard is auto, off or on, not %s\n",
                      .usage = "keyboard onscreen [auto|off|on]",
                      .help = "a keyboard on the screen while a text field has the keys: auto when no keyboard is plugged in, off, or on [auto]"),
    SETTING_PLAIN_ROW(null, "Key repeat delay (ms, or default)", SETTINGS_SLIDER, 0, ("keyboard", "repeat", "delay"), "delay ", null,
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_REPEAT_DELAY_PATH,
                      .standard = "250", .unit = "ms", .low = 100, .high = 2000,
                      .usage = "keyboard repeat delay [MS|default]",
                      .help = "how long a key is held before it repeats, 100 to 2000 ms [250]"),
    SETTING_PLAIN_ROW(null, "Key repeat rate (per second, off or default)", SETTINGS_FIELD, 0, ("keyboard", "repeat", "rate"), "rate ", "off",
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_REPEAT_RATE_PATH,
                      .standard = "30", .unit = "per second", .low = 1, .high = 100,
                      .usage = "keyboard repeat rate [PER-SECOND|off|default]",
                      .help = "repeats a second of a held key, 1 to 100, or none [30]"),
    SETTING_PLAIN_ROW(null, "Mouse sensitivity (percent, or default)", SETTINGS_SLIDER, 0, ("mouse", "sensitivity"), "sensitivity ", null,
                      .type = SETTING_NUMBER, .need = "", .path = SETTING_SENSITIVITY_PATH,
                      .standard = "100", .unit = "percent", .low = 10, .high = 500,
                      .usage = "mouse sensitivity [PERCENT|default]", .help = "how far the pointer goes for the hand, 10 to 500 percent [100]"),
    SETTING_PLAIN_ROW(null, "Mouse acceleration", SETTINGS_SWITCH, 0, ("mouse", "acceleration"), "acceleration ", null,
                      .need = "", .path = SETTING_ACCELERATION_PATH, .standard = "on",
                      .usage = "mouse acceleration [on|off]", .help = "the pointer outruns a fast hand; off is one to one [on]"),
    //  The wheel's glide (ui_wheel_turn, every region the wheel turns): on, a notch runs to its step (two and a half rows) over a
    //  short time, starting at once and easing out; off, a notch moves a whole step at once.
    SETTING_PLAIN_ROW(null, "Smooth mouse wheel", SETTINGS_SWITCH, 0, ("canvas", "wheel", "smooth"), "smooth ", null,
                      .path = SETTING_WHEEL_SMOOTH_PATH, .standard = "on",
                      .usage = "canvas wheel smooth [on|off]",
                      .help = "a notch runs to its step (two and a half rows) at once and eases out; off moves a whole step at once [on]"),
    SETTING_PLAIN_ROW(null, "Cursor size", SETTINGS_CHOICE, 0, ("canvas", "cursor", "size"), "size ", "auto 1 2 3 4",
                      .path = SETTING_CURSOR_SIZE_PATH, .standard = "auto", .reset = "auto",
                      .refusal = "cursor size is auto or a number from 1 to 4, not %s\n",
                      .usage = "canvas cursor [size [N|auto]|look [light|dark|auto]]", .help = "the pointer: size 1 to 4, colours [auto]"),
    SETTING_PLAIN_ROW(null, "Cursor look", SETTINGS_CHOICE, 0, ("canvas", "cursor", "look"), "look ", "auto light dark",
                      .path = SETTING_CURSOR_LOOK_PATH, .standard = "auto", .reset = "auto",
                      .refusal = "cursor look is light, dark or auto, not %s\n"),
    //  The pointer's shape: the classic bitmaps, or one filled shape with nothing in it, a circle that a fast hand stretches and
    //  that is a rounded block over text (compositor.c's dynamic cursor). A touch always uses it, whatever this says.
    SETTING_PLAIN_ROW(null, "Cursor style", SETTINGS_CHOICE, 0, ("canvas", "cursor", "style"), "style ", "classic dynamic",
                      .path = SETTING_CURSOR_STYLE_PATH, .standard = "dynamic", .reset = "dynamic",
                      .refusal = "cursor style is classic or dynamic, not %s\n",
                      .usage = "canvas cursor style [classic|dynamic]",
                      .help = "the classic pointers, or one filled shape that stretches with a fast hand and is a rounded block over text [dynamic]"),
    //  How far a pointer shrinks while a button is held (compositor.c's compositor_pointer_press, for either style): 0 is none.
    SETTING_PLAIN_ROW(null, "Pointer squeeze (percent, or default)", SETTINGS_SLIDER, 0, ("canvas", "cursor", "squeeze"), "squeeze ", null,
                      .type = SETTING_NUMBER, .path = SETTING_SQUEEZE_PATH,
                      .standard = "25", .unit = "percent", .low = 0, .high = 50,
                      .usage = "canvas cursor squeeze [PERCENT|default]",
                      .help = "how far a pointer shrinks while a button is held, 0 to 50 percent, 0 for none; the right button grows it by half that [25]"),
    SETTING_PLAIN_ROW(null, "Shake the pointer to find it", SETTINGS_SWITCH, 0, ("canvas", "cursor", "shake"), "shake ", null,
                      .path = SETTING_SHAKE_PATH, .standard = "on",
                      .usage = "canvas cursor shake [on|off]", .help = "a shake of the hand makes the pointer large for a moment [on]"),
    SETTING_PLAIN_ROW(null, "Large pointer stays (ms, or default)", SETTINGS_SLIDER, 0, ("canvas", "cursor", "shake", "delay"), "delay ", null,
                      .type = SETTING_NUMBER, .path = SETTING_SHAKE_DELAY_PATH,
                      .standard = "1200", .unit = "ms", .low = 100, .high = 10000,
                      .usage = "canvas cursor shake delay [MS|default]",
                      .help = "how long it stays large after the last shake, 100 to 10000 ms [1200]"),
    //  A held button changes the pointer's size for as long as it is down (compositor.c's compositor_pointer_press): on by default.
    SETTING_PLAIN_ROW(null, "Pointer squeezes on click", SETTINGS_SWITCH, 0, ("canvas", "cursor", "click"), "click ", null,
                      .path = SETTING_CURSOR_CLICK_PATH, .standard = "on",
                      .usage = "canvas cursor click [on|off]",
                      .help = "a held button changes how large the pointer is: the left shrinks it, the right grows it [on]"),
    //  The prediction a frame ahead (compositor.c's predict): a finger or a pen is delivered where it will be. The mouse
    //  pointer is always predicted, so it has no row.
    SETTING_PLAIN_ROW(null, "Predict touch and pen", SETTINGS_SWITCH, 0, ("canvas", "predict", "touch"), "touch ", null,
                      .path = SETTING_PREDICT_TOUCH_PATH, .standard = "on",
                      .usage = "canvas predict touch [on|off]",
                      .help = "a finger or a pen is taken a frame ahead of where it is, to the windows it reaches [on]"),
    //  On by default (the owner's call): a finger hides what it presses, and the mark says where the screen read it. It
    //  is drawn for a touch screen's fingers alone, never for a mouse, a pad or a pen, and costs nothing with no finger
    //  down or fading.
    SETTING_PLAIN_ROW(null, "Show touch area", SETTINGS_SWITCH, 0, ("canvas", "touch"), "touch ", null,
                      .path = SETTING_TOUCH_PATH, .standard = "on",
                      .usage = "canvas touch [on|off]",
                      .help = "a circle under each finger on a touch screen, fading for a second after it lifts [on]"),
    //  The screen's side edges (ui/ui.c, compositor.c): a swipe in from the left edge runs the window switcher or opens the
    //  launcher, one in from the right runs the switcher or opens the volume popup. Off by default: a swipe from a side is then
    //  the window's, as a touch.
    SETTING_PLAIN_ROW(null, "Left edge", SETTINGS_CHOICE, 0, ("canvas", "touch", "left"), "left ", "off windows launcher",
                      .path = SETTING_TOUCH_LEFT_PATH, .standard = "off", .reset = "off",
                      .refusal = "the left edge is off, windows or launcher, not %s\n",
                      .usage = "canvas touch left [off|windows|launcher]",
                      .help = "a swipe in from the left edge runs the window switcher, or opens the launcher [off]"),
    SETTING_PLAIN_ROW(null, "Right edge", SETTINGS_CHOICE, 0, ("canvas", "touch", "right"), "right ", "off windows volume",
                      .path = SETTING_TOUCH_RIGHT_PATH, .standard = "off", .reset = "off",
                      .refusal = "the right edge is off, windows or volume, not %s\n",
                      .usage = "canvas touch right [off|windows|volume]",
                      .help = "a swipe in from the right edge runs the window switcher, or opens the volume popup [off]"),
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
    SETTING_PLAIN_ROW(null, "Momentum", SETTINGS_SWITCH, 0, ("canvas", "scroll", "momentum"), "momentum ", null,
                      .path = SETTING_SCROLL_MOMENTUM_PATH, .standard = "on",
                      .usage = "canvas scroll momentum [on|off]",
                      .help = "a drag or a trackpad hand carries on past its release and slows; off, the content stops where the fingers leave it, for the guest only: momentum from a patched host is not stopped (no phase signal yet) [on]"),
    SETTING_PLAIN_ROW(null, "Rubber band", SETTINGS_SWITCH, 0, ("canvas", "scroll", "rubber"), "rubber ", null,
                      .path = SETTING_SCROLL_RUBBER_PATH, .standard = "on",
                      .usage = "canvas scroll rubber [on|off]",
                      .help = "past the top or the bottom the content stretches and springs back; off, it stops at the end [on]"),
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
                      .type = SETTING_NUMBER, .path = SETTING_TASKBAR_PADDING_PATH, .standard = "6",
                      .unit = "px", .low = 0, .high = 24,
                      .usage = "taskbar padding [PX|default]",
                      .help = "the room round the text inside each taskbar tab, 0 to 24 pixels at the taskbar scale [6]"),
    //  Off by default: the launcher opens from its key alone. On, the band has a tab with the launcher's mark at its leading end (the
    //  left of a bottom or top band, the top of a side one) that opens and closes the launcher beside it.
    SETTING_PLAIN_ROW(null, "Taskbar launcher button", SETTINGS_SWITCH, 0, ("taskbar", "launcher"), "launcher ", null,
                      .path = SETTING_TASKBAR_LAUNCHER_PATH, .standard = "off",
                      .usage = "taskbar launcher [on|off]",
                      .help = "a tab with the launcher mark at the start of the taskbar that opens the launcher beside it and puts it away again [off]"),
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
    //  A special row (host.c locale_taskbar_tray) whose file the compositor watches: the whole line is the format.
    {null, "Taskbar tray (text with {names}: {battery} {time.24h.hour}:{time.24h.min} {date.day} {cpu.load} ...)", SETTINGS_FIELD, 0,
     SETTINGS_LINE, {"taskbar", "tray"}, "tray ", null, {"taskbar", "tray"}, null, 0,
     .usage = "taskbar tray [FORMAT|default]",
     .help = "what the tray shows: text with {battery} {battery.percent} {battery.state} {time.24h.hour} {time.24h.min} {time.12h.hour} {time.ampm} {date.year} {date.month} {date.day} {date.weekday} {date.monthname} {cpu.load} {mem.used} {temp} {host} in it; {{ and }} are the braces [the battery, then the clock]",
     .path = SETTING_TASKBAR_TRAY_PATH},
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
    {null, "Clock (Enter sets it now)", SETTINGS_ACTION, 0, SETTINGS_LINE | SETTINGS_TICK, {"time"}, "local ", null, {"time", "sync"}, null, 0,
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
    {null, "Names over TLS", SETTINGS_CHOICE, 0, 0, {"dns", "tls"}, "dns tls ", null, {"dns", "tls"}, "off on only", 0,
     .usage = "dns tls [on|off|only]", .help = "names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]"},
    {null, "Name servers (auto for the network)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"dns"}, "dns ", null, {"dns"}, null, '(',
     .usage = "dns [ADDRESS...|auto]", .help = "up to three servers names are asked of, or the network gives them [auto]"},
    //  Waterlink, the machines linked to this one (moonwater link, src/waterlink): this machine's name, the one switch of the
    //  listener, a code shown for another machine to type, the other machine's name and code typed here, and how the machines
    //  reach each other. The machines linked are a list under these rows, which settings.c feeds from `link` (its status).
    {"Waterlink", "This machine", SETTINGS_INFO, 0, SETTINGS_LINE, {"name"}, "", null, {null}, null, 0,
     .help = "the name the other machines know this one by: moonwater name NEW changes it"},
    {null, "Waterlink", SETTINGS_SWITCH, 0, 0, {"link"}, "link ", null, {"link"}, null, 0,
     .help = "the listener: linked machines reach this one, and it them, while it is on [off]"},
    {null, "Pair a machine", SETTINGS_ACTION, 0, SETTINGS_WAITS, {null}, null, null, {"link", "pair"}, null, 0,
     .help = "shows a code, and waits up to five minutes for the other machine to type it with this machine's name"},
    {null, "Name and code of another machine", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT | SETTINGS_WAITS, {null}, null, null,
     {"link"}, null, 0,
     .help = "the other machine's name and the code it shows, which pair the two of them"},
    {null, "Connect over", SETTINGS_CHOICE, 0, SETTINGS_TRANSPORT, {"link", "transport"}, "transport ", null, {"link", "transport"}, "network modem", 0,
     .help = "network: over the network, as linked machines always have [network]; modem: the audio modem, through the sound card"},
    {null, "Audio modem", SETTINGS_INFO, 0, SETTINGS_LINE, {"link", "transport"}, "audio modem ", null, {null}, null, 0,
     .help = "the sound card as a link to another machine: it needs a sound card open on the sound server, and says why when it has none"},
    {"Wi-Fi", "Wi-Fi", SETTINGS_SWITCH, 0, 0, {"wifi"}, "wifi ", null, {"wifi"}, null, 0,
     .usage = "wifi [on|off]", .help = "the radio, saved networks and those in range [off]"},
    {"Bluetooth", "Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth"}, "bluetooth ", null, {"bluetooth"}, null, 0,
     .usage = "bluetooth [on|off]", .help = "the radio and the remembered devices [off]"},
    //  Internet sharing over Bluetooth: the machine's connection, offered to the devices that pair with it. Its verb (host.c's
    //  radio_share) starts and stops the sharing, so the row is not a plain one; the word it reads is the verb's own output.
    {null, "Internet sharing over Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth", "share"}, "share ", null, {"bluetooth", "share"}, null, 0,
     .usage = "bluetooth share [on|off]",
     .help = "the machine's internet, shared with the Bluetooth devices that pair with it; needs the Bluetooth network tools [off]"},
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
    {"Boot", "Start the desktop", SETTINGS_SWITCH, 1, 0, {"bind", "init", "canvas"}, "init canvas ", null, {"bind", "init", "canvas"},
     null, 0, .usage = "bind init canvas [on|off]", .help = "start Canvas at boot [on]"},
    {null, "Mount kept disks", SETTINGS_SWITCH, 1, 0, {"bind", "init", "mount"}, "init mount ", null, {"bind", "init", "mount"}, null,
     0, .usage = "bind init mount [on|off]", .help = "mount kept disks at boot [on]"},
    //  The kernel's own notices (the feeder puts the errors and warnings of the kernel's log under the key system notices): no verb of its
    //  own (moonwater notices is the command), so no read; its value is the count, and opening the row lists each notice to copy.
    {"System", "System notices", SETTINGS_INFO, 0, 0, {null}, null, null, {null}, null, 0, .output = SETTINGS_OUT_NOTICES,
     .help = "the kernel's own errors and warnings the desktop has: open the row to see each one, and copy a line"},
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
        positive at = 0;

        while (word[at] && unit[at])
        {
                p8 a = word[at], b = (p8)unit[at];

                if (a >= 'A' && a <= 'Z')
                        a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z')
                        b += 'a' - 'A';
                if (a != b)
                        return false;
                at++;
        }
        return word[at] == 0 && unit[at] == 0;
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

        while (digits < length && byte_is_digit(word[digits]))
                digits++;
        if (!digits || digits >= sizeof(digit))
                return false;
        memory_copy(digit, word, digits);
        digit[digits] = end;
        if (!setting_whole((string_address)digit, into))
                return false;
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
        What a file of a plain row holds, as a value, in value: the standard when it holds nothing,
        the word default or a word that is none of the row's. True when something is kept.
*/
static bool setting_kept(const struct settings_row address_to row, string_address word, long address_to value)
{
        p8 kept[16];
        const char address_to reset = setting_reset_of(row);

        long found;

        address_to value = setting_standard(row);
        if (!word[0] || string_equals(word, (string_address) "default") || string_equals(word, (string_address)reset) ||
            !setting_parse(row, word, kept, sizeof(kept), &found))
                return false;
        address_to value = found;
        return true;
}

/*
        What a plain row says of what its file holds: the word, a colour
        normalised, a number and its unit; the standard when nothing is kept
        or what is kept is none of its words.
*/
static string_address setting_shown(const struct settings_row address_to row, string_address word, p8 address_to into, positive room)
{
        p8 kept[16];
        long value = 0;
        positive length;

        if (!word[0] || string_equals(word, (string_address) "default") || !setting_parse(row, word, kept, sizeof(kept), &value))
                string_copy_bounded(into, (string_address)row->standard, room);
        else
                string_copy_bounded(into, kept, room);
        if (row->type == SETTING_NUMBER && byte_is_digit(into[0]) && row->unit)
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
                positive same = 0;

                while (same < length && setting_tray_names[at][same] == name[same])
                        same++;
                if (same == length && !setting_tray_names[at][same])
                        return at;
        }
        return -1;
}

// Whether text is a format the tray takes. When it is not and bad is given, bad is where the fault starts.
static bool setting_tray_valid(const char *text, positive *bad)
{
        positive at = 0, length = 0;

        while (text[length])
                length++;
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
                        positive close = at + 1;

                        while (text[close] && text[close] != '}')
                                close++;
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

#endif // MOONWATER_SETTINGS_REGISTRY
