/*
        The desktop's features, one file each under src/canvas/ui. A feature
        is in the shell when its switch is 1 and out of it when 0: its file is
        not compiled in, its tool is not in the table, and its tests are not
        built. The switches below are the defaults; to turn one off, change its
        line here, or define it to 0 in the compiler's flags.

        ui/ui.c holds the drawing primitives the features share. It is built
        whenever any feature is, and nothing else in the shell depends on it.
*/
#ifndef MOONWATER_UI_FEATURES
#define MOONWATER_UI_FEATURES

// The launcher: the search bar, the programs and their keys.
#ifndef MOONWATER_UI_LAUNCHER
#define MOONWATER_UI_LAUNCHER 1
#endif

// The settings window: the moonwater settings verb as a window, its groups and rows.
#ifndef MOONWATER_UI_SETTINGS
#define MOONWATER_UI_SETTINGS 1
#endif

// The taskbar: the strip along an edge of the desktop with the windows' tabs, the clock and, when the settings show it,
// the battery. It is drawn by the compositor, which is always in the shell; this switch takes the band out of it.
#ifndef MOONWATER_UI_TASKBAR
#define MOONWATER_UI_TASKBAR 1
#endif

#define MOONWATER_UI_ANY (MOONWATER_UI_LAUNCHER || MOONWATER_UI_SETTINGS || MOONWATER_UI_TASKBAR)

#endif
