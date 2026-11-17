/*
        src/sh/host/setting.c: the verbs splash, taskbar, mouse, language and audio, and the canvas's settings; the
        clock kept and restored at boot, and the name.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/*
        moonwater splash [FILE|default|off]: the picture Moonwater's loader draws before the kernel starts (the core of it,
        and what an install does with it, is by the image's own steps above: host_splash_place). FILE is a binary PPM (P6)
        of at most SPLASH_FILE_MOST bytes and SPLASH_SIDE_MOST pixels a side; default is the built-in picture; off takes the
        picture away. The word is judged first, a file read and checked whole, and then root is asked for. What is written
        is written to the system partition this session started from, the one whose image is this session's, found as the
        settings find it; a session started from no disk here has nowhere to keep one, and is told so. Bare, the verb says
        what that partition keeps and whether the loader draws it, and what the firmware's own logo is and why it is not set.
*/

/*
        The firmware's own logo, which a machine shows from power on until a loader draws, and which Moonwater does not
        set. UEFI has no variable for it: the firmware publishes the one it drew (the ACPI BGRT table) and takes no other.
        What each maker offers besides is its own, and none of it is a file on the system partition alone:

            Lenovo       its firmware looks for \EFI\Lenovo\Logo\mylogo_WxH.bmp, .jpg or .gif on the system partition, but
                         only when a variable of its own, LBLDESP, says a logo was set, and draws the file only when the
                         CRC-32 of its start is the one in another, LBLDVC. Neither is documented by Lenovo; what is known
                         of them is from people who took the firmware apart (Binarly's LogoFAIL advisory, BRLY-2023-006),
                         and machines have been left unable to start by a logo set this way. The route Lenovo does
                         document is a logo given to its firmware updater, which is a flash.
            HP           commercial notebooks of 2008 to 2012 read \Hewlett-Packard\Logo\HPlogo.jpg from a partition of
                         their own, HP_TOOLS, with the Custom Logo setting turned on in the firmware; later ones keep the
                         logo in the firmware, set by HP's own program.
            Dell         a logo is put in by a firmware update, by Dell or with its switch for it.
            ASUS         MyLogo writes the picture into the firmware image, which is then flashed.
            Framework    no way is offered.

        So nothing is written for any of them: a firmware variable that is wrong can leave a machine that does not start,
        and a flash is never Moonwater's to make. The maker is read from DMI, whose strings are the firmware's and so are
        whatever somebody put there: at most HOST_SPLASH_VENDOR bytes are kept, cut at the first line end, and a byte that
        is not printable ASCII is a question mark.
*/
#define HOST_SPLASH_VENDOR 48
#define HOST_SPLASH_DMI "/sys/class/dmi/id/sys_vendor"

static const struct
{
        string_address vendor; // what the maker's name starts with, in small letters
        string_address why;    // why a file cannot set its logo
} host_splash_firmwares[] = {
    {"lenovo", "reads a logo file only when two variables of its own (LBLDESP, LBLDVC) say so and hold its sum, and Moonwater writes no firmware variable"},
    {"hp", "takes a logo from a partition or a program of its own, on the models that take one at all"},
    {"hewlett", "takes a logo from a partition or a program of its own, on the models that take one at all"},
    {"dell", "changes its logo only with a firmware update"},
    {"asus", "keeps its logo in the firmware image, which only a flash changes"},
    {"framework", "offers no way to change its logo"},
};

// The maker of this machine as DMI names it, made plain, in room for HOST_SPLASH_VENDOR + 1 bytes: empty when it names none.
static fn host_splash_vendor(string_address path, p8 address_to into)
{
        p8 bytes[HOST_SPLASH_VENDOR + 1];
        bipolar got = file_slurp_once_at(AT_FDCWD, path, bytes, sizeof(bytes));
        positive length = 0;

        for (positive at = 0; got > 0 && at < (positive)got && bytes[at] != '\n' && bytes[at] != '\r' && bytes[at]; at++)
                into[length++] = bytes[at] >= 0x20 && bytes[at] < 0x7f ? bytes[at] : '?';
        while (length && into[length - 1] == ' ')
                length--;
        into[length] = end;
}

// Why a file cannot set the logo of a firmware whose maker is vendor: null for a maker this knows nothing of.
static string_address host_splash_firmware_why(string_address vendor)
{
        for (positive at = 0; at < array_count(host_splash_firmwares); at++)
        {
                string_address prefix = host_splash_firmwares[at].vendor;
                positive same = 0;

                while (prefix[same] && (vendor[same] | 0x20) == prefix[same])
                        same++;
                if (!prefix[same])
                        return host_splash_firmwares[at].why;
        }
        return null;
}

// The line of the firmware's own logo, which every status ends with.
static fn host_splash_firmware(void)
{
        p8 vendor[HOST_SPLASH_VENDOR + 1];
        string_address why;

        host_splash_vendor(HOST_SPLASH_DMI, vendor);
        why = host_splash_firmware_why((string_address)vendor);
        if (why)
                host_say(log, host_label "splash: the firmware's own logo is not set: %s firmware %s; it shows until the loader runs\n",
                         vendor, why);
        else
                host_say(log, host_label "splash: the firmware's own logo is not set: no way to set one from a file is known for %s firmware; "
                                         "it shows until the loader runs\n", vendor[0] ? (string_address)vendor : (string_address) "this");
}

// The system partition this session started from, its name into name: null, or why there is none to write to.
static string_address host_splash_partition(p8 address_to name)
{
        static host_settings session;
        static host_settings_search search;
        p8 running[HOST_BUILD_ROOM];
        p8 current[HOST_NAME_ROOM];

        memory_zero(address_of search, sizeof(search));
        if (!host_running_build(running, sizeof(running)))
                return "moonwater cannot read its own build";

        host_settings_session(address_of session);
        search.build = running;
        search.session = address_of session;
        storage_each_device(host_settings_visit, address_of search);
        if (search.count > 1)
                return "more than one disk here has the image this session started from";
        if (!search.count)
                return "no disk here has the image this session started from";
        //      The name was found a moment ago, and a disk comes and goes.
        if (search.partuuid[0] && (!host_partition_uuid_resolve(search.partuuid, current) ||
                                   !string_equals(current, search.name)))
                return "the disk this session started from is no longer the partition it was found as";

        string_copy(name, search.name);
        return null;
}

//      moonwater splash, bare: what the system partition keeps, and the firmware's own logo.
static b32 host_splash_status(void)
{
        struct splash_image image;
        p8 name[HOST_NAME_ROOM];
        p8 path[HOST_PATH_ROOM];
        string_address why;
        bipolar length;
        b32 failed = 0;

        if (!bowl_is_root())
        {
                host_say(log, host_label "splash: the picture is kept on the system partition, which root reads\n");
                host_splash_firmware();
                return 0;
        }
        if (!host_acquire())
                return 1;

        why = host_splash_partition(name);
        if (why)
                host_say(log, host_label "splash: none, and nowhere to keep one: %s\n", why);
        else if (!host_join(path, sizeof(path), HOST_LOOK, SPLASH_FILE) ||
                 host_mount(name, HOST_LOOK, "vfat", HOST_READ_ONLY) < 0)
                failed = host_refuse("splash: %s could not be mounted to read its picture\n", name);
        else
        {
                length = host_splash_read(path, host_splash_bytes);
                host_unmount(HOST_LOOK);
                why = length == -2  ? (string_address) "it is larger than 1 MiB"
                      : length < 0 ? file_reason(length)
                                   : (string_address)splash_parse(host_splash_bytes, (unsigned long)length, address_of image);
                if (length == -ERROR_NO_ENTRY)
                        host_say(log, host_label "splash: off; %s keeps no picture, so the loader draws nothing\n", name);
                else if (why)
                        host_say(log_error, host_label "splash: the picture on %s is not drawn, as if there were none: %s\n",
                                 name, why);
                else
                        host_say(log, host_label "splash: %p x %p picture on %s; Moonwater's loader draws it before the kernel starts\n",
                                 (positive)image.width, (positive)image.height, name);
        }
        host_release();

        host_splash_firmware();
        return failed;
}

/*
        The picture in host_splash_bytes, length bytes of it, onto the system partition this session started from, or with
        no length the picture there taken away: the partition is mounted for the write, the steps are host_splash_place's
        or host_splash_clear's, and what they left is read back from the disk itself, the partition let go of first, as an
        image's is.
*/
static b32 host_splash_set(positive length)
{
        host_splash_want want = {length ? host_splash_bytes : null, length};
        p8 name[HOST_NAME_ROOM];
        string_address why;
        bipolar mounted;
        b32 failed;

        if (!host_acquire())
                return 1;

        why = host_splash_partition(name);
        if (why)
        {
                host_release();
                return host_refuse("splash: %s, so nothing was written\n", why);
        }

        mounted = host_mount(name, HOST_SYSTEM, "vfat", HOST_WRITABLE);
        if (mounted < 0)
        {
                host_release();
                return host_fail(name, mounted);
        }

        failed = length ? host_splash_place(HOST_SYSTEM, host_splash_bytes, length) : host_splash_clear(HOST_SYSTEM);
        if (failed)
                host_unmount(HOST_SYSTEM);
        else if (!host_on_disk(name, HOST_SYSTEM, host_splash_landed, address_of want))
                failed = host_refuse("splash: %s did not read back from the disk as written\n", name);
        host_release();
        return_if(failed, 1);

        if (length)
                host_say(log, host_label "splash: kept on %s; Moonwater's loader draws it from the next start\n", name);
        else
                host_say(log, host_label "splash: off; %s keeps no picture, and the loader draws nothing from the next start\n", name);
        return 0;
}

//      moonwater splash [FILE|default|off]
static b32 host_splash(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        struct splash_image image;
        string_address reason = null;
        positive length = 0;

        return_if(count > 3, host_usage());
        if (!word)
                return host_splash_status();
        //      The word first: a file is read and checked whole before anybody is asked who they are.
        if (string_equals(word, "default"))
                length = host_splash_default();
        else if (!string_equals(word, "off"))
        {
                length = host_splash_take(word, address_of image, address_of reason);
                if (!length)
                        return host_refuse("splash: %s is not a usable picture: %s\n", word, reason);
        }
        if (!bowl_is_root())
                return host_refuse("setting the splash needs root (CAP_SYS_ADMIN)\n");
        return host_splash_set(length);
}

/*
        The desktop's ink and the pointer's look and size are rows of the settings table, kept in /root as the
        scale is and taken by the compositor as soon as a file is written (it watches /root, as it does for the
        keyboard layout) and again whenever it starts. Nothing is asked of Canvas, so what is said is what is
        kept: the default when nothing is. What is left here is the verb that has no row of its own.
*/
/*
        The plain rows of the settings table (src/sh/registry.c), which are
        all read and set here and nowhere else. A bare verb says where the
        row stands, to anybody who can read /root: the word kept, or the
        standard when none is. One word sets it: the word is judged first
        (a word that is none of the row's is refused, exit 1), then root is
        asked for, and the row is kept in its file as the word normalised,
        and said again. default, and the row's reset word, put it back by
        keeping that word. More words than one is a usage page. Whoever
        applies the file (the compositor, which watches /root) takes it as
        it is written.
*/

//      What the row's line starts with: its say, or its verb without the first word.
static fn host_setting_says(const struct settings_row address_to row, p8 address_to into, positive room)
{
        positive at = 0;

        into[0] = end;
        if (row->say)
        {
                string_copy_bounded(into, (string_address)row->say, room);
                return;
        }
        for (positive word = 1; word < setting_words(row) && at + 1 < room; word++)
        {
                if (word > 1)
                        into[at++] = ' ';
                string_copy_bounded(into + at, (string_address)row->read[word], room - at);
                at += string_length(into + at);
        }
}

//      What a row says it is: the word kept or the standard, and that it is root's to read when /root is closed to the caller.
static string_address host_setting_value(const struct settings_row address_to row, p8 address_to word, p8 address_to shown)
{
        if (!locale_readable((string_address)row->path, word, 32))
                return (string_address)LOCALE_NEEDS_ROOT;
        return setting_shown(row, (string_address)word, shown, 48);
}

static fn host_language_list(void);

static b32 host_setting_status(const struct settings_row address_to row)
{
        p8 word[32], shown[48], says[48];

        host_setting_says(row, says, sizeof(says));
        host_say(log, host_label "%s %s\n", (string_address)says, host_setting_value(row, word, shown));
        if (row->flags & SETTINGS_LANGUAGE)
                host_language_list();
        return 0;
}

//      What a word that is none of a row's is told, in the row's words or made from its kind.
static b32 host_setting_refuse(const struct settings_row address_to row, string_address word)
{
        p8 says[48];

        if (row->refusal)
                return host_refuse((string_address)row->refusal, word);
        host_setting_says(row, says, sizeof(says));
        string_format(log_error, host_label);
        if (row->type == SETTING_NUMBER)
                host_say(log_error, "%s is a number from %p to %p %s, %sor default, not %s\n", (string_address)says,
                         (positive)row->low, (positive)row->high, (string_address)(row->unit ? row->unit : ""),
                         row->choices ? (string_address)"off, " : (string_address)"", word);
        else if (row->kind == SETTINGS_SWITCH)
                host_say(log_error, "%s is on or off, not %s\n", (string_address)says, word);
        else
                host_say(log_error, "%s is %s, not %s\n", (string_address)says, (string_address)row->choices, word);
        return 1;
}

//      The row the words name (found by setting_find), read or set.
static b32 host_setting_run(const struct settings_row address_to row, string_address address_to arguments, positive count)
{
        positive words = setting_words(row);
        p8 kept[32], says[48];
        long value;

        return_if(count - 1 > words + 1, host_usage());
        if (count - 1 == words)
                return host_setting_status(row);
        if (!setting_parse(row, arguments[1 + words], kept, sizeof(kept), &value))
                return host_setting_refuse(row, arguments[1 + words]);
        if (!bowl_is_root())
        {
                const char address_to need = row->need ? row->need : "how Canvas draws";
                if (*need)
                        return host_refuse("setting %s needs root (CAP_SYS_ADMIN)\n", (string_address)need);
                host_need_root();
        }
        host_setting_says(row, says, sizeof(says));
        if (host_save((string_address)says, (string_address)row->path, (string_address)kept))
                return 1;
        return host_setting_status(row);
}

//      The plain rows whose verbs start with these words, each said: what a verb that has more than one setting answers bare.
static b32 host_setting_group(string_address address_to words, positive count)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];
                positive word = 0;

                if (row->how != SETTING_PLAIN || setting_words(row) < count)
                        continue;
                while (word < count && string_equals(words[word], (string_address)row->read[word]))
                        word++;
                if (word == count)
                        host_setting_status(row);
        }
        return 0;
}

/*
        moonwater canvas arrange [NAME[@X,Y|@off]...|default]: where each
        screen is in the desktop's space, by its connector's name: laid left
        to right in the order named, the first the primary, at a place given
        (NAME@X,Y, in pixels) or dark (NAME@off); a screen no word names
        follows in the card's order. The words are kept in /root as a line
        and taken by the compositor as soon as the file is written (it
        watches /root), and a name that is no screen's now is kept for when
        it is plugged in. They are judged together before root is asked for
        (windows_arrange_valid: a name once, a place inside 32768 pixels,
        default alone). Bare it says what is kept, the default when nothing
        is, and then each screen and where it is.
*/
static fn locale_canvas_arrange_line(struct canvas_control address_to control, positive at, int (*places)[3], bool label)
{
        (void)label;
        if (places[at][2] == -2)
                host_say(log, host_label "  %s, its place " LOCALE_NEEDS_ROOT "\n", (string_address)control->output[at].connector);
        else if (places[at][2] < 0)
                host_say(log, host_label "  %s dark\n", (string_address)control->output[at].connector);
        else
                host_say(log, host_label "  %s at %b,%b%s\n", (string_address)control->output[at].connector,
                         (bipolar)places[at][0], (bipolar)places[at][1],
                         places[at][2] ? (string_address) "" : (string_address) ", primary");
}

static b32 locale_canvas_arrange_status(void)
{
        p8 text[512];
        bipolar got = host_read_state(SETTING_CANVAS_ARRANGE_PATH, text, sizeof(text) - 1);
        struct canvas_control control;

        text[got > 0 ? got : 0] = end;
        for (positive at = 0; text[at]; at++)
                if (text[at] == '\n' || text[at] == '\r' || text[at] == '\t')
                        text[at] = ' ';
        while (got > 0 && text[got - 1] == ' ')
                text[--got] = end;
        if (got < 0 && got != -ENOENT && !bowl_is_root())
                host_say(log, host_label "arrange " LOCALE_NEEDS_ROOT "\n");
        else
                host_say(log, host_label "arrange %s\n", text[0] && windows_arrange_valid((const char address_to)text)
                                                                ? (string_address)text : (string_address) "default");
        if (host_canvas_request(SPARK_CANVAS_STATUS, address_of control) >= 0 && control.running)
        {
                int places[SPARK_CANVAS_OUTPUTS][3];

                host_canvas_places(address_of control, places);
                host_canvas_screens(address_of control, places, locale_canvas_arrange_line, true);
        }
        return 0;
}

static b32 locale_canvas_arrange(string_address address_to arguments, positive count)
{
        p8 text[512];
        positive at = 0;
        bipolar failed;

        if (count == 3)
                return locale_canvas_arrange_status();
        for (positive word = 3; word < count; word++)
        {
                positive length = string_length(arguments[word]);

                if (at + length + 2 > sizeof(text))
                        return host_refuse("arrange is a word for each screen, not %p words\n", (positive)(count - 3));
                if (word > 3)
                        text[at++] = ' ';
                memory_copy(text + at, arguments[word], length);
                at += length;
        }
        text[at] = end;
        if (!windows_arrange_valid((const char address_to)text))
                return host_refuse("arrange is screens' names, each alone, NAME@X,Y or NAME@off and named once, "
                                   "or default, not %s\n", text);
        if (!bowl_is_root())
                return host_refuse("setting how Canvas draws needs root (CAP_SYS_ADMIN)\n");
        text[at++] = '\n';
        failed = host_write_file(SETTING_CANVAS_ARRANGE_PATH, text, at, 0644, true);
        if (failed < 0)
                return host_fail("canvas arrange", failed);
        return locale_canvas_arrange_status();
}

#define LOCALE_CANVAS_DISPLAY_USAGE "display is NAME rotation DEGREES, NAME refresh HZ|preferred, NAME scale PERCENT or NAME place X Y\n"
#define LOCALE_CANVAS_DISPLAYS SETTING_CANVAS_DISPLAYS_PATH

/*
        moonwater canvas display NAME rotation DEGREES, NAME refresh HZ|preferred, NAME scale PERCENT or NAME place X Y: one screen's setting
        in /root/canvas.displays, which the compositor watches and applies as the file is written. DEGREES is 0, 90, 180
        or 270; HZ is a whole number from 1 to 1000, or preferred for the screen's own rate; X and Y are whole numbers of
        pixels, as canvas arrange's places are. NAME is the connector's (Virtual-1, HDMI-A-1, eDP-1). Only that field of
        that screen changes, and every other line of the file is kept as it is (windows_displays_set). The words are
        judged before root is asked for, as canvas arrange's are; the file is read and written under the settings lock.
*/
static b32 locale_canvas_display(string_address address_to arguments, positive count)
{
        struct windows_display change;
        char name[WINDOWS_OUTPUT_NAME], out[4096];
        p8 text[4096];
        unsigned int field;
        bool rotation, refresh, scale, place;
        int value = 0, x = 0, y = 0, made;
        bipolar got, failed;

        if (count < 4)
                return host_refuse(LOCALE_CANVAS_DISPLAY_USAGE);
        rotation = count == 6 && string_equals(arguments[4], "rotation");
        refresh = count == 6 && string_equals(arguments[4], "refresh");
        scale = count == 6 && string_equals(arguments[4], "scale");
        place = count == 7 && string_equals(arguments[4], "place");
        if (!rotation && !refresh && !scale && !place)
                return host_refuse(LOCALE_CANVAS_DISPLAY_USAGE);
        if (!windows_display_name((const char *)arguments[3], string_length(arguments[3]), name))
                return host_refuse("\"%s\" is not a screen's name: letters, digits and dashes, 1 to %p bytes "
                                   "(Virtual-1, HDMI-A-1, eDP-1)\n", arguments[3], (positive)(WINDOWS_OUTPUT_NAME - 1));

        memory_zero(address_of change, sizeof(change));
        if (rotation)
        {
                if (!windows_display_value(WINDOWS_DISPLAY_ROTATION, (const char *)arguments[5],
                                           string_length(arguments[5]), &value))
                        return host_refuse("rotation is 0, 90, 180 or 270 degrees, not %s\n", arguments[5]);
                change.rotation = (unsigned int)value;
                field = WINDOWS_DISPLAY_ROTATION;
        }
        else if (refresh)
        {
                if (!windows_display_value(WINDOWS_DISPLAY_REFRESH, (const char *)arguments[5],
                                           string_length(arguments[5]), &value))
                        return host_refuse("refresh is preferred, the screen's own rate, or a whole number of Hz "
                                           "from 1 to 1000, not %s\n", arguments[5]);
                change.refresh = (unsigned int)value;
                field = WINDOWS_DISPLAY_REFRESH;
        }
        else if (scale)
        {
                if (!windows_display_value(WINDOWS_DISPLAY_SCALE, (const char *)arguments[5],
                                           string_length(arguments[5]), &value))
                        return host_refuse("scale is 50, 75, 100, 125, 150 or 200 percent, not %s\n", arguments[5]);
                change.scale = (unsigned int)value;
                field = WINDOWS_DISPLAY_SCALE;
        }
        else
        {
                if (!windows_display_value(WINDOWS_DISPLAY_X, (const char *)arguments[5], string_length(arguments[5]), &x) ||
                    !windows_display_value(WINDOWS_DISPLAY_Y, (const char *)arguments[6], string_length(arguments[6]), &y))
                        return host_refuse("place is two whole numbers of pixels, X and Y, from -%p to %p, not %s %s\n",
                                           (positive)WINDOWS_PLACE_MOST, (positive)WINDOWS_PLACE_MOST,
                                           arguments[5], arguments[6]);
                change.x = x, change.y = y;
                field = WINDOWS_DISPLAY_X | WINDOWS_DISPLAY_Y;
        }
        if (!bowl_is_root())
                return host_refuse("setting how a screen is drawn needs root (CAP_SYS_ADMIN)\n");

        host_state_ready();
        if (!host_acquire())
                return 1;
        got = host_read_state(LOCALE_CANVAS_DISPLAYS, text, sizeof(text));
        if (got == -ENOENT)
                text[0] = end;
        else if (got == -ELOOP)
        {
                host_release();
                return host_refuse(LOCALE_CANVAS_DISPLAYS " is a link, so it is not changed\n");
        }
        else if (got < 0)
        {
                host_release();
                return host_fail("canvas display", got);
        }
        else if (got >= (bipolar)sizeof(text) - 1)
        {
                host_release();
                return host_refuse(LOCALE_CANVAS_DISPLAYS " is too long to change here, so it is not changed\n");
        }

        made = windows_displays_set(out, sizeof(out), (const char *)text, name, field, &change);
        if (made < 0)
        {
                host_release();
                return host_refuse(LOCALE_CANVAS_DISPLAYS " names more screens than Canvas reads, or the change "
                                   "does not fit, so it is not changed\n");
        }

        /*
                Published by rename, as canvas arrange's is: the new bytes are written beside the file and renamed over it,
                so the compositor, which reads this file as it is written, never finds it empty (a truncating open left it
                so for as long as the write took). The file keeps the mode it has, as the truncate did, and a new one is
                0644; a link is not followed (the read above refused one).
        */
        failed = host_publish_again((string_address)LOCALE_CANVAS_DISPLAYS, out, (positive)made, 0644, true, 0);
        host_release();
        if (failed < 0)
                return host_fail("canvas display", failed);

        if (rotation)
                host_say(log, host_label "%s is turned to %p degrees, saved to " LOCALE_CANVAS_DISPLAYS "\n",
                         (string_address)name, (positive)change.rotation);
        else if (refresh && change.refresh)
                host_say(log, host_label "%s refreshes at %p Hz, saved to " LOCALE_CANVAS_DISPLAYS "\n",
                         (string_address)name, (positive)change.refresh);
        else if (refresh)
                host_say(log, host_label "%s refreshes at its preferred rate, saved to " LOCALE_CANVAS_DISPLAYS "\n",
                         (string_address)name);
        else if (scale)
                host_say(log, host_label "%s is drawn at %p percent, saved to " LOCALE_CANVAS_DISPLAYS "\n",
                         (string_address)name, (positive)change.scale);
        else
                host_say(log, host_label "%s is placed at %b,%b, saved to " LOCALE_CANVAS_DISPLAYS "\n",
                         (string_address)name, (bipolar)x, (bipolar)y);
        return 0;
}

/*
        moonwater taskbar tray [FORMAT|default]: what the tray at the end of the taskbar shows, as text with {names}
        in it ({battery}, {time.24h.hour}, {date.day}, {cpu.load}, ...: the help page lists them). Several words are
        joined by single blanks; a format with blanks to keep is one quoted word. It is judged whole before root is
        asked for (setting_tray_valid) and kept in /root as a line, and the compositor, which watches /root, draws it
        at once and again each minute. Bare it says what is kept, the standard when nothing is.
*/
static b32 locale_taskbar_tray_status(void)
{
        p8 text[SETTING_TRAY_MOST + 16];
        bipolar got = host_read_state(SETTING_TASKBAR_TRAY_PATH, text, sizeof(text) - 1);

        text[got > 0 ? got : 0] = end;
        while (got > 0 && (text[got - 1] == '\n' || text[got - 1] == '\r'))
                text[--got] = end;
        if (got < 0 && got != -ENOENT && !bowl_is_root())
        {
                host_say(log, host_label "tray " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        host_say(log, host_label "tray %s\n",
                 text[0] && !string_equals((string_address)text, "default") && setting_tray_valid((const char *)text, null)
                     ? (string_address)text
                     : (string_address)SETTING_TASKBAR_TRAY_STANDARD);
        return 0;
}

static b32 locale_taskbar_tray(string_address address_to arguments, positive count)
{
        p8 text[SETTING_TRAY_MOST + 16];
        positive at = 0, bad;
        bipolar failed;

        if (count == 3)
                return locale_taskbar_tray_status();
        for (positive word = 3; word < count; word++)
        {
                positive length = string_length(arguments[word]);

                if (at + length + 2 > sizeof(text))
                        return host_refuse("tray is at most %p bytes of text\n", (positive)SETTING_TRAY_MOST);
                if (word > 3)
                        text[at++] = ' ';
                memory_copy(text + at, arguments[word], length);
                at += length;
        }
        text[at] = end;
        if (!string_equals((string_address)text, "default") && !setting_tray_valid((const char *)text, &bad))
                return host_refuse("tray is text of at most %p bytes in which {name} is one of battery, battery.percent, "
                                   "battery.state, time.24h.hour, time.24h.min, time.12h.hour, time.ampm, date.year, "
                                   "date.month, date.day, date.weekday, date.monthname, cpu.load, mem.used, temp or host "
                                   "and {{ }} are the braces; it goes wrong at byte %p\n",
                                   (positive)SETTING_TRAY_MOST, bad);
        if (!bowl_is_root())
                return host_refuse("setting how Canvas draws needs root (CAP_SYS_ADMIN)\n");
        text[at++] = '\n';
        failed = host_write_file(SETTING_TASKBAR_TRAY_PATH, text, at, 0644, true);
        if (failed < 0)
                return host_fail("taskbar tray", failed);
        return locale_taskbar_tray_status();
}

/*
        moonwater taskbar pinned [LINE|none]: the programs kept on the taskbar (setting_pins_valid says what a line is), which the taskbar's
        menu on a task writes with its Pin to taskbar and Unpin from taskbar. Several words are joined by single blanks, so a line may be
        typed as it is or quoted whole. It is judged whole before root is asked for, and kept in /root as a line, which the compositor,
        which watches /root, takes at once. Bare it says what is kept: the line, or none.
*/
static b32 locale_taskbar_pinned_status(void)
{
        p8 text[SETTING_PINS_MOST + 16];
        bipolar got = host_read_state(SETTING_TASKBAR_PINNED_PATH, text, sizeof(text) - 1);

        text[got > 0 ? got : 0] = end;
        while (got > 0 && (text[got - 1] == '\n' || text[got - 1] == '\r'))
                text[--got] = end;
        if (got < 0 && got != -ENOENT && !bowl_is_root())
        {
                host_say(log, host_label "pinned " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        host_say(log, host_label "pinned %s\n",
                 text[0] && !string_equals((string_address)text, "none") && setting_pins_valid((const char *)text, null) ? (string_address)text
                                                                                                                      : (string_address) "none");
        return 0;
}

static b32 locale_taskbar_pinned(string_address address_to arguments, positive count)
{
        p8 text[SETTING_PINS_MOST + 16];
        positive at = 0, bad;
        bipolar failed;

        if (count == 3)
                return locale_taskbar_pinned_status();
        for (positive word = 3; word < count; word++)
        {
                positive length = string_length(arguments[word]);

                if (at + length + 2 > sizeof(text) - 8)
                        return host_refuse("pinned is at most %p bytes of text\n", (positive)SETTING_PINS_MOST);
                if (word > 3)
                        text[at++] = ' ';
                memory_copy(text + at, arguments[word], length);
                at += length;
        }
        text[at] = end;
        if (string_equals((string_address)text, "default"))
                string_copy((string_address)text, (string_address) "none"), at = 4;
        if (!string_equals((string_address)text, "none") && !setting_pins_valid((const char *)text, &bad))
                return host_refuse("pinned is a line of at most %p bytes with at most %p programs, each an app_id, the executable it ran as and the words it "
                                   "is started with (the first of them a path from /), a program ended by a lone ; and a word in single quotes where it "
                                   "has more than letters, digits and _ . / : = @ + , -; it goes wrong at byte %p\n",
                                   (positive)SETTING_PINS_MOST, (positive)SETTING_PINS_ENTRIES, bad);
        if (!bowl_is_root())
                return host_refuse("setting how Canvas draws needs root (CAP_SYS_ADMIN)\n");
        text[at++] = '\n';
        failed = host_write_file(SETTING_TASKBAR_PINNED_PATH, text, at, 0644, true);
        if (failed < 0)
                return host_fail("taskbar pinned", failed);
        return locale_taskbar_pinned_status();
}

// moonwater taskbar: the plain rows (position, display, text), and tray and pinned, which are a line of text.
static b32 host_setting_verb(string_address address_to arguments, positive count);
static b32 host_taskbar(string_address address_to arguments, positive count)
{
        if (count >= 3 && string_equals(arguments[2], "tray"))
                return locale_taskbar_tray(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "pinned"))
                return locale_taskbar_pinned(arguments, count);
        return host_setting_verb(arguments, count);
}

//      moonwater canvas cursor: bare it says the size, a percent (cursor size 80 percent), and the look in one
//      line, then the rows that are the shake's; any other word is a usage page (the rows have words of their
//      own, found before this).
static b32 locale_canvas_cursor(string_address address_to arguments, positive count)
{
        p8 word[32], shown[48], other[32], more[48];
        string_address shake[] = {(string_address) "canvas", (string_address) "cursor", (string_address) "shake"};

        return_if(count != 3, host_usage());
        host_say(log, host_label "cursor size %s, look %s\n",
                 host_setting_value(setting_by_path(SETTING_CURSOR_SIZE_PATH), word, shown),
                 host_setting_value(setting_by_path(SETTING_CURSOR_LOOK_PATH), other, more));
        (void)arguments;
        return host_setting_group(shake, 3);
}

/*
        moonwater canvas windows [forget]: whether windows open where the hand last put them, and where each one
        that is kept will, from the compositor's own file read with its own reader (window.c's places), so what is
        said is what it will do. forget takes the file away: the compositor reads it when a window opens and holds
        nothing of it, so the next window opens where its program puts it. The rows of remember are found before
        this, as a plain row's words are.
*/
static b32 locale_canvas_windows(string_address address_to arguments, positive count)
{
        static p8 text[PLACES_BYTES + 1];
        struct places places;
        bool forget = count == 4 && string_equals(arguments[3], "forget");
        bipolar handle, got = 0;

        return_if(count > 4 || (count == 4 && !forget), host_usage());
        if (forget)
        {
                bipolar failed;

                host_need_root();
                failed = system_remove_at(AT_FDCWD, (string_address)SETTING_WINDOWS_PLACES_PATH, 0);
                return_if(failed < 0 && failed != -ENOENT, host_fail("canvas windows forget", failed));
                host_say(log, host_label "windows forgotten: each opens where its program puts it\n");
                return 0;
        }

        host_setting_status(setting_by_path((string_address)SETTING_WINDOWS_REMEMBER_PATH));
        handle = system_open_at(AT_FDCWD, (string_address)SETTING_WINDOWS_PLACES_PATH, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (handle == -EACCES || handle == -EPERM)
        {
                host_say(log, "  places " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        if (handle >= 0)
        {
                got = system_read_once((b32)handle, text, PLACES_BYTES);
                system_close(handle);
        }
        places_parse(&places, (const char *)text, got > 0 ? (unsigned long)got : 0);
        for (unsigned int at = 0; at < places.count; at++)
                host_say(log, "  %s on %s at %b,%b, %b by %b\n", (string_address)places.kept[at].name,
                         (string_address)places.kept[at].screen, (bipolar)places.kept[at].x, (bipolar)places.kept[at].y,
                         (bipolar)places.kept[at].width, (bipolar)places.kept[at].height);
        if (!places.count)
                host_say(log, "  no window is kept\n");
        return 0;
}

//      A verb with no words of its own but its rows': bare it says them all.
static b32 host_setting_verb(string_address address_to arguments, positive count)
{
        return_if(count != 2, host_usage());
        return host_setting_group(arguments + 1, 1);
}

/*
        moonwater audio [volume|mute|server|device|source|capture|input ...|test]: the sound server's settings (src/audio). The volume,
        the mute, the server's switch, the switch for recording and the microphone's volume and mute are plain rows of the table,
        found before this; the card and the microphone are names that are kept as they are typed and that the server, which
        reads /root/audio.device and /root/audio.source again twice a second, plays on or records from when it finds them, else it
        takes what it would have chosen. A name is what the page of the server lists (letters, digits and . _ - : @ +, at
        most 63), or default. Bare, it says them all. `test` plays a second of 440 Hz through the server and says in words what
        became of it (no server, no card, a card's mixer off, muted, or played on this card).
*/
#define LOCALE_AUDIO_NAME 64

static bool locale_audio_name(string_address word)
{
        positive length = string_length(word);

        if (!length || length >= LOCALE_AUDIO_NAME)
                return false;
        for (positive at = 0; at < length; at++)
        {
                p8 c = word[at];

                if (!byte_is_alnum(c) && c != '.' && c != '_' && c != '-' && c != ':' && c != '@' && c != '+')
                        return false;
        }
        return true;
}

// What `audio device` and `audio source` keep, said: the verb is the label after "audio ".
static b32 locale_audio_pick_status(string_address label, string_address path)
{
        p8 word[LOCALE_AUDIO_NAME + 16];

        if (!locale_readable(path, word, sizeof(word)))
                host_say(log, host_label "%s " LOCALE_NEEDS_ROOT "\n", label + 6);
        else
                host_say(log, host_label "%s %s\n", label + 6, word[0] && locale_audio_name(word) ? (string_address)word : (string_address) "default");
        return 0;
}

static b32 locale_audio_pick(string_address address_to arguments, positive count, string_address label, string_address path)
{
        if (count == 3)
                return locale_audio_pick_status(label, path);
        return_if(count > 4, host_usage());
        if (!string_equals(arguments[3], "default") && !locale_audio_name(arguments[3]))
                return host_refuse("%s is a name audio lists (letters, digits and . _ - : @ +, at most %p bytes), or default, not %s\n", label,
                                   (positive)(LOCALE_AUDIO_NAME - 1), arguments[3]);
        host_need_root();
        if (host_save(label, path, arguments[3]))
                return 1;
        return locale_audio_pick_status(label, path);
}

/*
        The server's page of memory as a file: read whole and its sequence looked at again (the server writes it sixty times a
        second while anything plays, and a sequence that is odd or moved under the read is read again, at most eight times).
        False for no page, one that is not the server's, or one that is not still. A page is root's (mode 0600).
*/
static struct audio_state locale_audio_state;
static p8 locale_audio_raw[sizeof(struct audio_state) + 8]; // the read leaves its last byte for the end, so the buffer is a little more than the page

static bipolar locale_audio_page(void)
{
        for (positive tries = 0; tries < 8; tries++)
        {
                p8 again[32];
                unsigned int sequence;
                bipolar got = host_read_state(AUDIO_STATE_PATH, locale_audio_raw, sizeof(locale_audio_raw));

                if (got < 0)
                        return got;
                if (got < (bipolar)sizeof(locale_audio_state))
                        return 0;
                memory_copy(&locale_audio_state, locale_audio_raw, sizeof(locale_audio_state));
                if (locale_audio_state.seq & 1u)
                        continue;
                if (host_read_state(AUDIO_STATE_PATH, again, sizeof(again)) < 12)
                        continue;
                memory_copy(&sequence, again + 8, sizeof(sequence));
                if (sequence != locale_audio_state.seq)
                        continue;
                return locale_audio_state.magic == AUDIO_STATE_MAGIC && locale_audio_state.version == AUDIO_STATE_VERSION &&
                               locale_audio_state.bytes == sizeof(locale_audio_state) ? 1 : 0;
        }
        return 0;
}

//      A percent of the server's 65536 for 100, rounded, as the mixer shows it.
static positive locale_audio_percent(unsigned int volume)
{
        return ((positive)volume * 100 + 32768) / 65536;
}

/*
        Bare, `moonwater audio` says the rows' values and then what the server shows: the card it plays on and its volume, the cards
        there are (the active one starred, each by the name `audio device` takes), and the programs that have a sound with their
        volumes. The settings are anybody's to read; the page is root's.
*/
static b32 locale_audio_status(string_address address_to arguments)
{
        bipolar page;
        const struct audio_state address_to state = &locale_audio_state;

        host_setting_group(arguments + 1, 1);
        locale_audio_pick_status("audio device", SETTING_AUDIO_DEVICE_PATH);
        locale_audio_pick_status("audio source", SETTING_AUDIO_SOURCE_PATH);
        page = locale_audio_page();
        if (page < 0 && page != -ENOENT)
        {
                host_say(log, host_label "sound " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        if (page <= 0)
        {
                host_say(log, host_label "the sound server is not running\n");
                return 0;
        }
        host_say(log, host_label "playing on %s, %p percent%s\n", (string_address)state->device, locale_audio_percent(state->volume),
                 state->muted ? (string_address) ", muted" : (string_address) "");
        // What the output really is: said when it is not a card that will be heard.
        if (state->output_state & AUDIO_STATE_OUT_SUSPENDED)
                host_say(log, "  the output is suspended: no stream has played for five seconds, so the sound card is released; the next sound opens it again (a delay of about a period, or the card's prepare time: `moonwater audio` shows it once it plays)\n");
        else if (state->output_state & AUDIO_STATE_OUT_CLOCK)
                host_say(log, "  no sound card is open: the server plays to a clock, and nothing is heard (a card that could not be opened is named in the server's log)\n");
        if (state->output_state & AUDIO_STATE_OUT_HW_OFF)
                host_say(log, "  the card's own mixer has \"%s\" off and would not take it on: nothing may be heard\n", (string_address)state->hw_note);
        else if (state->output_state & AUDIO_STATE_OUT_HW_SET)
                host_say(log, "  the card's own mixer was set for sound when it was opened (%p of its %p output controls were off)\n", (positive)state->hw_changed, (positive)state->hw_controls);
        if (state->muted || !state->volume)
                host_say(log, "  nothing is heard while the volume is %s: `moonwater audio %s`\n", state->muted ? (string_address) "muted" : (string_address) "0", state->muted ? (string_address) "mute off" : (string_address) "volume 100");
        if (!(state->output_state & AUDIO_STATE_OUT_CLOCK) && state->output_why)
        {
                static const char address_to const why[] = {"", "you chose it", "the server was started with it", "it is a speaker, a USB card, or an analog output with something plugged in or none that could tell",
                                                           "a display is plugged into it and nothing is plugged into the analog jacks", "it is an HDMI or DisplayPort output that cannot say whether a display is plugged in",
                                                           "nothing is plugged in anywhere, so it is the first output there is"};

                if (state->output_why < sizeof(why) / sizeof(why[0]))
                        host_say(log, "  picked because %s\n", (string_address)why[state->output_why]);
        }
        for (positive at = 0; at < state->devices && at < AUDIO_STATE_DEVICES; at++)
                host_say(log, "  %s %s  %s%s\n", (state->device_list[at].flags & AUDIO_STATE_DEVICE_ACTIVE) ? (string_address) "*" : (string_address) " ",
                         (string_address)state->device_list[at].name, (string_address)state->device_list[at].description,
                         (state->device_list[at].flags & AUDIO_STATE_DEVICE_UNPLUGGED) ? (string_address) "  (nothing is plugged in)" : (string_address) "");
        for (positive at = 0; at < AUDIO_STATE_STREAMS; at++)
        {
                const struct audio_state_stream address_to stream = &state->stream[at];

                if (stream->index == 0xffffffffu)
                        continue;
                host_say(log, "  %s  %p percent%s%s\n", (string_address)(stream->app[0] ? stream->app : stream->media[0] ? stream->media : "?"),
                         locale_audio_percent(stream->volume), (stream->flags & AUDIO_STATE_MUTED) ? (string_address) ", muted" : (string_address) "",
                         (stream->flags & AUDIO_STATE_PLAYING) && !(stream->flags & AUDIO_STATE_CORKED) ? (string_address) ", playing" : (string_address) "");
        }
        // The recording half: whether programs may record, the microphone they are given and the ones there are (what plays back
        // through a sink is no microphone), and the programs that record from one.
        if (!state->capture)
                host_say(log, host_label "recording is off\n");
        else if (state->input[0])
                host_say(log, host_label "recording from %s, %p percent%s\n", (string_address)state->input, locale_audio_percent(state->input_volume),
                         state->input_muted ? (string_address) ", muted" : (string_address) "");
        else
                host_say(log, host_label "no microphone\n");
        for (positive at = 0; at < state->inputs && at < AUDIO_STATE_INPUTS; at++)
                if (!(state->input_list[at].flags & AUDIO_STATE_DEVICE_MONITOR))
                        host_say(log, "  %s %s  %s\n", (state->input_list[at].flags & AUDIO_STATE_DEVICE_ACTIVE) ? (string_address) "*" : (string_address) " ",
                                 (string_address)state->input_list[at].name, (string_address)state->input_list[at].description);
        for (positive at = 0; at < AUDIO_STATE_RECORDS; at++)
        {
                const struct audio_state_stream address_to record = &state->record[at];

                if (record->index == 0xffffffffu || (record->flags & AUDIO_STATE_MONITOR))
                        continue;
                host_say(log, "  %s  recording, %p percent%s%s\n", (string_address)(record->app[0] ? record->app : record->media[0] ? record->media : "?"),
                         locale_audio_percent(record->volume), (record->flags & AUDIO_STATE_MUTED) ? (string_address) ", muted" : (string_address) "",
                         (record->flags & AUDIO_STATE_CORKED) ? (string_address) ", paused" : (string_address) "");
        }
        return 0;
}

static b32 audio_test_run(void);

static b32 host_audio(string_address address_to arguments, positive count)
{
        if (count >= 3 && string_equals(arguments[2], "device"))
                return locale_audio_pick(arguments, count, "audio device", SETTING_AUDIO_DEVICE_PATH);
        if (count >= 3 && string_equals(arguments[2], "source"))
                return locale_audio_pick(arguments, count, "audio source", SETTING_AUDIO_SOURCE_PATH);
        // A second of 440 Hz through the sound server, and what happened in words (src/audio/service.c).
        if (count == 3 && string_equals(arguments[2], "test"))
                return audio_test_run();
        return_if(count != 2, host_usage());
        return locale_audio_status(arguments);
}

/*
        What the schedule makes of a query that ended, or of a look between
        queries. A success is only a success if the kernel still says the
        clock is synchronised when it is looked at: any change of
        clocksource clears that (timekeeping_notify ends in ntp_clear), and
        x86 changes it by itself about 1.5 s into a boot, when the TSC's
        refined calibration replaces tsc-early, and again whenever the
        watchdog gives up on the TSC. A first answer that landed before the
        switch left a correct clock marked unsynchronised and the next query
        256 s away: on a guest whose entropy was ready at once, 6 boots in 6
        synced at 1.4 s, were unsynchronised at 1.5 s, and were still
        waiting at 42 s. So an answer the kernel has forgotten is asked for
        again at the retry pace, whether that is seen as the child ends or
        on a later look -- but not over a RATE answer's wait, which leaves
        the retry pace at its slowest.
*/
static fn locale_ntp_schedule(bipolar ended, bool synced, p64 now)
{
        if (!ended && synced)
        {
                //      Soon at first, while the loop has not learned how
                //      fast this clock runs, and half-hourly once it has
                //      had the time to.
                locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
                locale_ntp_synced = now;
                locale_ntp_next = locale_after(now, locale_ntp_every);
                locale_ntp_every = locale_doubled(locale_ntp_every,
                                                  LOCALE_NTP_AGAIN);
        }
        else if (ended == LOCALE_NTP_EXIT_RATE)
        {
                locale_ntp_retry = LOCALE_NTP_RETRY_MOST;
                locale_ntp_next = locale_after(now, LOCALE_NTP_RATE_AGAIN);
        }
        else if (ended == LOCALE_NTP_EXIT_LONG)
                locale_ntp_next = locale_after(now, LOCALE_NTP_AGAIN);
        else if (ended != LOCALE_CHILD_IDLE)
        {
                //      Doubling from a second to a quarter of an hour: a
                //      machine that cannot reach a server does not walk
                //      seven names every eight seconds for ever.
                locale_ntp_next = locale_after(now, locale_ntp_retry);
                locale_ntp_retry = locale_doubled(locale_ntp_retry,
                                                  LOCALE_NTP_RETRY_MOST);
        }
        else if (locale_ntp_synced && !synced &&
                 locale_ntp_retry == LOCALE_NTP_RETRY_LEAST &&
                 locale_ntp_next > locale_after(now, LOCALE_NTP_RETRY_LEAST))
                locale_ntp_next = locale_after(now, LOCALE_NTP_RETRY_LEAST);
}

/*
        Whether NTP was wanted at the machine's last turn, which the wake
        below reads rather than the file again; and which network the turn
        was on, so that a different one, or the first, is a new chance for
        the queries that wait out a failure.
*/
static bool locale_ntp_on;
static bool locale_online_held;
static p8 locale_network_held[LOCALE_NETWORK_ROOM];

//      The walk in the child, and the class of what became of it.
static p8 locale_ntp_task(address_any context)
{
        bipolar failed = locale_ntp_apply(false);

        (void)context;
        return failed >= 0 ? 0
               : failed == SNTP_RATE_LIMITED ? LOCALE_NTP_EXIT_RATE
               : failed == SNTP_DENIED || failed > SNTP_ANSWER
                   ? LOCALE_NTP_EXIT_LONG
                   : 1;
}

static fn locale_ntp_keep(bool online, bool synced, p64 now)
{
        bipolar ended;

        //      `ntp off` ends a query in flight and waits for it: left to
        //      run it would set the clock after it was told not to, and
        //      nothing would reap it or close its pipe.
        if (!locale_ntp_on)
        {
                locale_child_stop(address_of locale_ntp_child);
                return;
        }
        ended = locale_child_poll(address_of locale_ntp_child);
        if (ended == LOCALE_CHILD_RUNNING)
                return;
        locale_ntp_schedule(ended, synced, now);
        if (ended != LOCALE_CHILD_IDLE || (locale_ntp_next && now < locale_ntp_next) ||
            !online)
                return;

        //      No child to make is a query that failed: a machine out of
        //      processes does not ask four times a second.
        if (!locale_child_run(address_of locale_ntp_child, locale_ntp_task,
                              null))
                locale_ntp_schedule(1, synced, now);
}

static fn locale_restore(void)
{
        p8 zone[80];
        p8 keyboard[16];

        locale_zone_own();

        host_read_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        if (zone[0])
        {
                tzset();
                (void)locale_zone_kernel();
                (void)bowl_write_localtime_host();
                (void)bowl_write_localtime_all(null);
        }

        //      How Canvas draws, as it was left: a word the kernel does not
        //      take is not one that was ever kept.
        host_read_word(LOCALE_CANVAS_MODES_PATH, keyboard, sizeof(keyboard));
        if (keyboard[0] && locale_canvas_modes_ok(keyboard))
                locale_canvas_live(keyboard);

        locale_ntp_next = 0;
        locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
        locale_ntp_synced = 0;
        locale_ntp_every = LOCALE_NTP_AGAIN_FIRST;
        //      The floor this boot began with, once a boot, from the disk's
        //      /root when one was taken.
        locale_clock_base_keep();
        locale_auto_next = 0;
        locale_auto_wait = LOCALE_AUTO_LEAST;
        //      Only the machine process asks the time. `moonwater boot` comes
        //      through here too when a disk is taken, and the machine
        //      process, released by the same verdict, asks as well: two
        //      queries that each measured an offset before either applied it
        //      stepped the clock twice.
        locale_ntp_on = locale_ntp_wanted();
        if (host_machine_self && locale_ntp_on)
                locale_ntp_keep(true, locale_clock_synced(),
                                system_clock_ns(HOST_CLOCK_BOOTTIME));
}

/*
        How long the machine loop may sleep before locale_ntp_keep must look
        again, at most the loop's own wake. A query in flight is polled every
        quarter second and a retry is woken for when it falls due: the first
        ask of a boot can run before the lease (its DNS id waits on the same
        entropy the DHCP transaction id does). Never 0, which the machine
        wait reads as not waiting at all; a retry already past due (its fork
        failed) is looked at again in a quarter second rather than in a
        spin. For ten seconds after a query that set the clock it is a
        quarter second as well, so that the kernel dropping that
        synchronisation at the boot's clocksource switch (see
        locale_ntp_schedule) is seen at once and not at the next three-second
        wake: three instrumented boots lost it at 1.5 s and asked again only
        at 5.6 s. In the first minute of a boot with no clock set yet it is
        a second, because the network that comes up then is the one the
        clock waits for.
*/
static unsigned int locale_wake_ms(unsigned int most)
{
        p64 now;
        p64 due;

        if (locale_ntp_child.pid > 0)
                return most < 250 ? most : 250;
        if (!locale_ntp_on)
                return most;
        now = system_clock_ns(HOST_CLOCK_BOOTTIME);
        if (!locale_ntp_synced && now < 60000000000ull && most > 1000)
                return 1000;
        if (!locale_ntp_next)
                return most;
        if (locale_ntp_synced && now - locale_ntp_synced < 10000000000ull)
                return most < 250 ? most : 250;
        due = locale_ntp_next <= now ? 250
                                     : (locale_ntp_next - now) / 1000000 + 1;
        return due < most ? (unsigned int)due : most;
}

//      Daylight saving moves the offset twice a year without anyone setting
//      anything; the kernel's copy follows it here. Bowls need nothing, since
//      their file carries the rule rather than the offset.
static fn locale_recover(void)
{
        p8 network[LOCALE_NETWORK_ROOM];
        bool online = locale_network(network, sizeof(network));
        bool synced;
        p64 now;

        (void)locale_zone_kernel();
        locale_ntp_on = locale_ntp_wanted();
        //      A network that is not the one of the last turn, or the first
        //      after none, is not a reason to go on waiting out a failure
        //      that was the last one's: both queries are asked at once.
        if (online && (!locale_online_held ||
                       (network[0] &&
                        !locale_network_same(network, locale_network_held))))
        {
                locale_ntp_next = 0;
                locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
                locale_auto_next = 0;
                locale_auto_wait = LOCALE_AUTO_LEAST;
        }
        locale_online_held = online;
        string_copy_bounded(locale_network_held, network,
                            sizeof(locale_network_held));
        synced = locale_clock_synced();
        now = system_clock_ns(HOST_CLOCK_BOOTTIME);
        locale_ntp_keep(online, synced, now);
        locale_auto_keep(locale_ntp_on, synced, now, network);
        locale_clock_keep();
}

/*
        What this machine is called.

        Two words and a hyphen, rolled the first time a machine boots and kept
        in /root with the other settings, so a name made on a live stick goes
        to the disk it installs on, `update` keeps it and `wipe` leaves it.
        There are five hundred and twelve of each, so a roll is eighteen bits
        and two machines of one person's are very unlikely to share a name;
        pairing is what settles one that does. The words are lowercase
        letters only and the name is a valid host name, which is what the
        kernel, a prompt and a router will all take.

        The kernel's own default is the same on every machine this image is
        built for, which is why it is not the name: `hostname NAME` changes
        what the kernel says for the rest of the session and nothing else,
        where this is what the machine is.
*/
#define NAME_PATH "/root/name"
#define NAME_ROOM 80
#define NAME_WORDS 512
#define NAME_LETTERS "abcdefghijklmnopqrstuvwxyz0123456789-"

static const p8 name_adjectives[] =
    "ample aqua arctic ashen astral azure balmy bashful bold brave bright brisk "
    "bronze breezy bubbly burly calm candid careful cheery chilly civil clever "
    "cloudy coastal cobalt cozy crisp curious dainty dandy dapper daring "
    "dashing dewy distant dreamy dusky eager earnest early earthy elated "
    "electric elegant emerald epic even faint fancy fearless fiery fine fleet "
    "floral fluffy fluent foggy fond frank frosty fuzzy gallant gentle giddy "
    "gilded glad gleaming glossy glowing golden graceful grand grassy great "
    "green hardy hasty hazy hearty hidden humble hushed icy idle indigo ivory "
    "jaunty jolly jovial joyful keen kind lively lofty lucid lucky lunar lush "
    "magic merry mellow mighty mild minty misty modest mossy mythic natural "
    "neat nifty nimble noble oaken orbital pastel patient peaceful pearly perky "
    "plucky plush polar polite precise proud pure quaint quick quiet radiant "
    "rapid regal rosy royal rustic sable sandy savvy scarlet serene shady sharp "
    "shiny silent silky silver sleek smart smooth snowy snug solar sonic spry "
    "stable starry steady stellar still stormy sturdy subtle sunny super swift "
    "tame teal tender tidal tidy tiny tranquil trusty twilit upbeat urban "
    "valiant velvet verdant vibrant vivid warm wavy whole wily windy wintry "
    "wise witty woolly young zany zesty zippy acoustic adept aerial agile airy "
    "alert alpine amiable amused ancient angular antique apt ardent artful "
    "atomic avid awake aware balanced beaming benign blazing blithe blooming "
    "bonny boundless bouncy bountiful bracing brainy brawny brimming broad "
    "buoyant busy buttery cardinal carefree cascading celestial central certain "
    "charming cheerful chief chipper choice chosen circular classic clean clear "
    "cloudless clustered colorful comfy compact composed concise content cool "
    "copper countless courtly crafty creamy crimson cubic cuddly curly cute "
    "deft deluxe dense devoted diamond digital diligent divine double downy "
    "drifting driven dual dynamic eastern ebony ecstatic elastic elfin elite "
    "endless energetic enigmatic equal erudite ethereal exact exotic expert "
    "fabled fair faithful famed fanciful fast fertile festive fit flaxen "
    "fleeting flexible flowing flying formal fortunate fragrant free fresh "
    "friendly frozen fruitful full funky future galactic gamma genial genuine "
    "gifted glacial glittering gleeful global gracious gradual grateful "
    "grounded growing guiding gusty halcyon handy happy harmonic heady heavenly "
    "helpful heroic honest hopeful humming hybrid ideal immense inner inspired "
    "intent intrepid inventive iron jazzy jeweled jumbo just kindly knowing "
    "lacy lasting legendary level liberal light likely limber linen lithe "
    "little lone long loyal lyrical majestic mature maximal mental metallic "
    "milky mindful mirrored mobile molten moonlit morning mountain musical "
    "mystic narrow native nautical nearby neon nested neutral new next "
    "nocturnal north northern novel oceanic offbeat open optimal orange organic "
    "original outer oval pacific paper parallel peachy perfect pink placid "
    "plain playful pleasant plentiful pocket poised polished polka portable "
    "positive potent practical primal prismatic private prized prompt "
    "prosperous quantum quilted rare ready real reborn refined regular relaxed "
    "reliable remote resolute rested rhythmic rich ringing rippling rising "
    "robust rolling rooted round roving rugged running sacred safe sailing "
    "savory scenic secret select settled shimmering shining simple sincere "
    "singing skilled slender slow small smiling snappy social soft solid solo "
    "soothing sound southern sparkling special speedy spiral splendid spotted "
    "square standing steel sterling stout strong studious sublime summer sunlit "
    "supreme sure sweet synthetic";

static const p8 name_nouns[] =
    "comet nebula quasar pulsar galaxy orbit meteor asteroid planet moon "
    "eclipse aurora cosmos rocket probe satellite lander rover capsule shuttle "
    "station beacon horizon zenith equinox solstice wizard sorcerer mage druid "
    "knight bard dragon griffin phoenix unicorn sprite pixie golem elf dwarf "
    "oracle sage ranger paladin alchemist wanderer pilgrim voyager explorer "
    "pioneer captain skipper sailor mariner otter falcon heron lynx fox wolf "
    "bear owl raven crane ibis panda koala lemur llama alpaca gecko newt toad "
    "frog turtle dolphin whale seal walrus narwhal orca manta squid octopus "
    "crab lobster shrimp beetle firefly moth mantis cricket bee ant robin finch "
    "wren sparrow magpie osprey condor kestrel egret stork puffin penguin "
    "pelican albatross swan goose duck badger beaver bison camel caribou "
    "cheetah cougar coyote dingo donkey eagle elk ferret gazelle giraffe goat "
    "hare hawk hedgehog hippo horse hyena ibex impala jackal jaguar kangaroo "
    "kiwi leopard lion lizard marmot meerkat mole moose mouse mule ocelot "
    "opossum oryx parrot pigeon pony porcupine puma quail rabbit raccoon ram "
    "reindeer rhino salmon seahorse sloth snail sparrowhawk squirrel starling "
    "tapir tiger tortoise trout toucan turkey vole weasel wombat yak zebra "
    "maple willow cedar birch aspen oak pine fern moss lotus orchid daisy "
    "clover thistle ivy bamboo cactus poppy tulip violet lily jasmine lavender "
    "thyme basil mint rosemary fennel ginger pepper mango lemon peach cherry "
    "berry honey biscuit pretzel waffle muffin noodle pancake pudding cookie "
    "cocoa almond walnut pecan hazel acorn olive plum apricot melon papaya "
    "guava lime citrus canyon glacier meadow lagoon harbor island reef delta "
    "dune ridge summit valley tundra prairie oasis grove forest river creek "
    "brook spring cascade geyser volcano plateau mesa cliff cavern grotto fjord "
    "bay cove shore beach lake pond marsh swamp steppe savanna jungle orchard "
    "garden lantern compass anchor kettle teapot mirror prism lighthouse bridge "
    "tower castle window ribbon button lamp clock harp flute drum violin cello "
    "piano trumpet kite sail mast paddle canoe kayak sled wagon cart wheel gear "
    "lever pulley hammock tent cabin cottage barn windmill fountain statue arch "
    "gate path trail road bench swing slide ladder rope knot basket bucket jar "
    "bottle cup bowl plate spoon fork pan pot kiln forge anvil loom spindle "
    "needle thimble quilt blanket pillow candle torch ember spark flame cinder "
    "ash coal pebble marble crystal gem pearl opal ruby topaz jade amber garnet "
    "quartz agate onyx jasper coral shell atom photon quark proton neutron "
    "electron vector matrix tensor fractal helix prime lattice cipher token "
    "signal packet socket kernel module driver router switch relay beam laser "
    "maser radar sonar echo pulse wave ripple tide current breeze gust gale "
    "squall storm thunder lightning rainbow cloud mist fog frost snow sleet "
    "hail dew rain drizzle sunbeam moonbeam stardust starlight twilight sunrise "
    "sunset daybreak midnight noon dusk dawn hour minute season harvest bloom "
    "sprout seedling branch petal blossom bud vine root trunk leaf feather "
    "scale paw whisker tail mane hoof horn tusk antler tuba banjo ukulele "
    "guitar bagpipe fiddle bell chime gong cymbal tambourine whistle trombone "
    "oboe clarinet bassoon sitar zither lyre lute dulcimer harmonica accordion "
    "kazoo marimba xylophone anemone bluebell buttercup chickadee dandelion "
    "dragonfly hummingbird kingfisher nightingale periwinkle sandpiper "
    "snowflake tangerine";

/* The word at an index in a list of words one space apart, the empty word for
   an index past the last. */
static fn name_word(const p8 address_to list, positive size, positive index,
                    p8 address_to into, positive room)
{
        string_address word = (string_address)list +
                              memory_nth_of((address_any)list, ' ', size, index).x;
        positive length = (positive)(string_first_of_or_end(word, ' ') - word);

        if (length >= room)
                length = room - 1;
        memory_copy_apart(into, word, length);
        into[length] = end;
}

static fn name_roll(p8 address_to into, positive room)
{
        p8 noun[16];
        p32 bits;

        system_random_fill(address_of bits, sizeof(bits), 0);

        name_word(name_adjectives, sizeof(name_adjectives) - 1, bits & (NAME_WORDS - 1),
                  into, room);
        string_append_bounded(into, "-", room);
        name_word(name_nouns, sizeof(name_nouns) - 1, (bits >> 9) & (NAME_WORDS - 1),
                  noun, sizeof(noun));
        string_append_bounded(into, noun, room);
}

static bool link_name_good(string_address name);

/* Lowercase letters, digits and hyphens, a letter or digit at each end: one
   label of a host name, and not the word that rolls a new one. It is also
   what `moonwater link` can say, which is the shorter limit (under 32) and
   leaves out the words that command takes in a name's place, so the name a
   machine has is always one it can be linked by. */
static bool name_valid(string_address name)
{
        positive length = string_length(name);

        return link_name_good(name) && string_span_of_set(name, NAME_LETTERS) == length &&
               name[length - 1] != '-' && !string_equals(name, "random");
}

static bipolar name_apply(string_address name)
{
        bipolar applied = system_call_2(syscall(sethostname), (positive)name,
                                        string_length(name));

        if (applied >= 0)
                bowl_hosts_write_in("", name, false);
        return applied;
}

//      A roll that could not be saved, kept so that the next ask in this
//      process is the same name and not another roll.
static p8 name_unsaved[NAME_ROOM];

/* The saved name, rolled and saved first when there is none or it does not
   read. Says whether it had to roll in `rolled`, and whether the name is
   saved at the end: false when /root would not take it. */
static bool name_ensure(p8 address_to into, positive room, bool address_to rolled)
{
        bool held;
        bool saved;

        host_read_word(NAME_PATH, into, room);
        address_to rolled = !name_valid(into);

        if (!address_to rolled)
                return true;

        /*      The roll is made under the host lock, and the file is read again
                there: a take's restore and the machine process's, both at boot,
                that found no name each rolled one and saved it, and the kernel was
                given whichever applied last, so /root and the machine's name
                disagreed until the next boot. A lock that cannot be had is no
                reason not to roll. */
        held = host_acquire();
        if (held)
        {
                host_read_word(NAME_PATH, into, room);
                address_to rolled = !name_valid(into);
                if (!address_to rolled)
                {
                        host_release();
                        return true;
                }
        }

        if (name_unsaved[0])
                string_copy_bounded(into, (string_address)name_unsaved, room);
        else
                name_roll(into, room);
        saved = radio_write_word(NAME_PATH, into) >= 0;
        if (held)
                host_release();
        if (saved)
        {
                name_unsaved[0] = 0;
                return true;
        }
        string_copy_bounded((string_address)name_unsaved, (string_address)into,
                            sizeof(name_unsaved));
        return false;
}

static fn name_restore(void)
{
        p8 name[NAME_ROOM];
        bool rolled;

        (void)name_ensure(name, sizeof(name), address_of rolled);
        (void)name_apply(name);

        if (rolled)
                host_say(log, host_label "this machine is called %s; "
                                         "moonwater name changes it\n", name);
}

