/*
        src/sh/host/desktop.c: the verb desktop: the desktop a machine starts, and which one.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/*
        moonwater desktop: what is on the screen once the machine has started.

        The compositor is the desktop, or off, the text console: Canvas is turned
        off as soon as it has started. Another compositor (weston, in the desktop
        lane) takes the display from Canvas the way any program that opens the
        card does, once desktop off has ended this one: Canvas lets go and drops
        the keys while it holds it, and has the keyboard back when it ends.

        The choice is /root/desktop, as the keyboard's is /root/keyboard. The
        rules' canvas on runs `desktop boot` (desktop-boot), once for each boot,
        which waits for the verdict so that /bowls is there and puts the choice
        in force, and `desktop start` is the same by hand. A word kept there that
        is neither (the name of a bowl profile's session, which an earlier
        Moonwater could start instead) is the compositor.
*/
#define DESKTOP_PATH "/root/desktop"
#define DESKTOP_BOOTED HOST_STATE "/desktop.booted"

static string_address desktop_choice(p8 address_to name, positive room)
{
        if (host_read_word(DESKTOP_PATH, name, room) <= 0 || !string_equals(name, "off"))
                string_copy_bounded(name, "canvas", room);
        return (string_address)name;
}

//      The compositor's pid, which the runner keeps while its service runs (src/rover/runner.c).
#define DESKTOP_COMPOSITOR HOST_STATE "/compositor.pid"

// The pid a file holds, or 0: a whole number that fits what kill takes it as, and
// nothing after its digits (4294967295 is -1, which every process answers to, and "123abc" is no pid).
static positive desktop_pid_of(string_address path)
{
        p8 word[24];
        string_address stop;
        b32 range = 0;
        positive pid;

        return_if(host_read_word(path, word, sizeof(word)) <= 0, 0);
        stop = (string_address)word;
        pid = string_to_number_unsigned_checked((string_address)word, address_of stop, 10, address_of range);
        return *stop || range || pid > 0x7fffffff ? 0 : pid;
}

static bool desktop_alive(positive pid)
{
        return pid > 1 && system_call_2(syscall(kill), pid, 0) == 0;
}

/*
        The machine woke (moonwater sleep and hibernate, awake again): the compositor of the machine's own desktop is told
        with SIGUSR2, so it takes the screens again at once and does not wait for a key or the clocks to say it slept
        (compositor_woke). Whether there was one to tell.
*/
#define DESKTOP_SIGUSR2 12
static bool desktop_woke(void)
{
        positive compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);

        return desktop_alive(compositor) && system_call_2(syscall(kill), compositor, DESKTOP_SIGUSR2) == 0;
}


// Whether Canvas is running in the kernel: -1 when it cannot be asked.
static int desktop_canvas_running(void)
{
        struct canvas_control control;

        return host_canvas_request(SPARK_CANVAS_STATUS, address_of control) < 0 ? -1 : control.running != 0;
}

/*
        The choice, put in force. on is the compositor: Canvas is turned on if it is off, or off and on if it is on
        with no compositor (the runner starts the compositor's service when Canvas is on). off is the text console:
        Canvas is turned off. At boot the runner has started the compositor already, so on is nothing then.
*/
static b32 desktop_put(string_address name, bool boot)
{
        if (!string_equals(name, "off"))
        {
                return_if(boot, 0);
                if (desktop_canvas_running() <= 0)
                        return host_canvas_on();
                return_if(desktop_alive(desktop_pid_of((string_address)DESKTOP_COMPOSITOR)), 0);
                return_if(host_canvas_off(), 1);
                return host_canvas_on();
        }

        //      The runner stops the compositor's service when Canvas is off; the card is free when it has gone,
        //      and a compositor started straight after (weston) is refused its master until then.
        positive compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);

        return_if(desktop_canvas_running() <= 0, 0);
        return_if(host_canvas_off(), 1);
        for (positive pass = 0; pass < 100 && desktop_alive(compositor); pass++)
                host_pause(50000000);
        host_console_redraw();
        return 0;
}

static b32 desktop_start(bool boot)
{
        p8 name[HOST_NAME_ROOM];

        desktop_choice(name, sizeof(name));
        return desktop_put((string_address)name, boot);
}

/*
        From the rules, when Canvas has started: the choice, once for a boot,
        in a process of its own that waits for the disks, so the runner that
        started it goes on at once.
*/
static b32 desktop_boot(void)
{
        bipolar made;
        bipolar child;

        host_need_root();
        host_state_ready();
        made = system_open_at_mode(AT_FDCWD, DESKTOP_BOOTED,
                                   FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC,
                                   0600);
        return_if(made < 0, 0);
        system_close(made);
        child = system_fork();
        if (child < 0)
        {
                //      The marker goes with the failed start, so that a boot that asks again is not told it was done.
                (void)system_remove_at(AT_FDCWD, DESKTOP_BOOTED, 0);
                return host_fail("desktop", child);
        }
        return_if(child, 0);

        system_call(syscall(setsid));
        host_verdict_settled();
        system_call_1(syscall(exit), (positive)desktop_start(true));
        return 0;
}

static b32 desktop_status(void)
{
        p8 name[HOST_NAME_ROOM];

        desktop_choice(name, sizeof(name));
        host_say(log, host_label "desktop %s\n", string_equals(name, "off") ? (string_address) "off" : (string_address) "on");
        return 0;
}

/*
        moonwater desktop autostart [PROGRAM...|none|default], add PROGRAM, remove PROGRAM: the programs the desktop opens when it
        starts, in order (registry.c's setting_autostart_valid says what a line is). The desktop opens no window of its own: the
        image's autostart rule (session.rover, its script) reads this verb's line once the compositor is up and opens each, so the kernel log and the
        console are the standard's and not the session's. Bare it says the line, none, or the standard when nothing is kept. They are
        started as root, so a change needs root, and a program added is a file that can be run. Kept in /root, and taken at the
        desktop's next start.
*/
// Memory of the autostart line given back, none being nothing to give.
#define desktop_give(block) \
        do \
        { \
                if (block) \
                        memory_give(block); \
        } while (0)

/*
        The file as it is, whole, in memory taken for it (given back by the caller), without the line's end and the blanks before it:
        read again into twice the room while it fills what it was given, so a list of any length is read whole. An empty string when
        there is no file; none, with *error the kernel's answer, when it cannot be read or the memory cannot be had.
*/
static p8 address_to desktop_autostart_read(bipolar address_to error)
{
        positive room = 4096;
        p8 address_to text = null;
        bipolar got;

        for (;;)
        {
                p8 address_to more = (p8 address_to)memory_resize(text, room);

                if (!more)
                {
                        desktop_give(text);
                        *error = -ENOMEM;
                        return null;
                }
                text = more;
                got = host_read_state(SETTING_DESKTOP_AUTOSTART_PATH, text, room);
                if (got < 0 || (positive)got + 1 < room || room >= (1u << 20))
                        break;
                room *= 2;
        }
        *error = got < 0 ? got : 0;
        if (got < 0)
                got = 0;
        text[got] = end;
        while (got > 0 && (text[got - 1] == '\n' || text[got - 1] == '\r' || text[got - 1] == ' '))
                text[--got] = end;
        return text;
}

static b32 desktop_autostart_status(void)
{
        bipolar error;
        p8 address_to text = desktop_autostart_read(address_of error);

        if (!text || (error < 0 && error != -ENOENT && !bowl_is_root()))
        {
                desktop_give(text);
                host_say(log, host_label "autostart " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        host_say(log, host_label "autostart %s\n",
                 string_equals((string_address)text, "none") || (!text[0] && !SETTING_AUTOSTART_STANDARD[0]) ? (string_address) "none"
                 : text[0] && setting_autostart_valid((const char *)text, null) ? (string_address)text
                                                                                : (string_address)SETTING_AUTOSTART_STANDARD);
        desktop_give(text);
        return 0;
}

static b32 desktop_autostart_runnable(string_address program)
{
        return_if(!setting_autostart_path((const char *)program),
                  host_refuse("a program to start is an absolute path of letters, digits and _ . / + -, not %s\n", program));
        return_if(system_access_at(AT_FDCWD, program, 1) != 0, host_refuse("%s is not a program this machine can run\n", program));
        return 0;
}

// A word appended to the line being made (*used bytes of room, a blank before it unless it is the first), the room grown as it needs.
static bool desktop_autostart_append(p8 address_to address_to text, positive address_to used, positive address_to room, const char address_to word,
                                     positive length)
{
        positive need = *used + (*used ? 1 : 0) + length + 2;

        if (need > *room)
        {
                positive grown = *room * 2 > need ? *room * 2 : need;
                p8 address_to more = (p8 address_to)memory_resize(*text, grown);

                if (!more)
                        return false;
                *text = more, *room = grown;
        }
        if (*used)
                (*text)[(*used)++] = ' ';
        memory_copy(*text + *used, word, length);
        *used += length;
        (*text)[*used] = end;
        return true;
}

static b32 desktop_autostart(string_address address_to arguments, positive count)
{
        p8 address_to text = null, address_to kept = null;
        positive used = 0, room = 0, bad;
        bipolar failed;
        bool add = count == 5 && string_equals(arguments[3], "add"), remove = count == 5 && string_equals(arguments[3], "remove");

        return_if(count == 3, desktop_autostart_status());
        return_if((string_equals(arguments[3], "add") || string_equals(arguments[3], "remove")) && count != 5, host_usage());
        return_if((string_equals(arguments[3], "default") || string_equals(arguments[3], "none")) && count != 4, host_usage());
        host_need_root();
        if (string_equals(arguments[3], "default"))
        {
                failed = system_remove_at(AT_FDCWD, SETTING_DESKTOP_AUTOSTART_PATH, 0);
                return_if(failed < 0 && failed != -ENOENT, host_fail("desktop autostart", failed));
                return desktop_autostart_status();
        }
        if (add || remove)
        {
                positive from = 0, length;
                const char address_to word;

                //      The line kept now: the standard when nothing (or nothing that is a line) is, and empty for none.
                kept = desktop_autostart_read(address_of failed);
                return_if(!kept, host_fail("desktop autostart", failed));
                if (string_equals((string_address)kept, "none"))
                        kept[0] = end;
                else if (!kept[0] || !setting_autostart_valid((const char *)kept, null))
                {
                        desktop_give(kept);
                        kept = (p8 address_to)memory_take(sizeof(SETTING_AUTOSTART_STANDARD));
                        return_if(!kept, host_fail("desktop autostart", -ENOMEM));
                        memory_copy(kept, SETTING_AUTOSTART_STANDARD, sizeof(SETTING_AUTOSTART_STANDARD));
                }
                if (add && (desktop_autostart_runnable(arguments[4]) ||
                            (setting_autostart_has((const char *)kept, (const char *)arguments[4]) &&
                             host_refuse("%s is started with the desktop already\n", arguments[4]))))
                {
                        desktop_give(kept);
                        return 1;
                }
                if (remove && !setting_autostart_has((const char *)kept, (const char *)arguments[4]))
                {
                        desktop_give(kept);
                        return host_refuse("%s is not started with the desktop\n", arguments[4]);
                }
                while ((word = setting_autostart_next((const char *)kept, &from, &length)))
                        if (!remove || !setting_autostart_same(word, length, (const char *)arguments[4], string_length(arguments[4])))
                                if (!desktop_autostart_append(&text, &used, &room, word, length))
                                        goto no_memory;
                if (add && !desktop_autostart_append(&text, &used, &room, (const char *)arguments[4], string_length(arguments[4])))
                        goto no_memory;
                desktop_give(kept);
                kept = null;
        }
        else if (!string_equals(arguments[3], "none"))
        {
                //      The whole list, a word each.
                for (positive each = 3; each < count; each++)
                {
                        if (desktop_autostart_runnable(arguments[each]))
                        {
                                desktop_give(text);
                                return 1;
                        }
                        if (!desktop_autostart_append(&text, &used, &room, (const char *)arguments[each], string_length(arguments[each])))
                                goto no_memory;
                }
        }
        //      A list left with nothing in it is none, which opens nothing: not the standard, which no file would be.
        if (!used && !desktop_autostart_append(&text, &used, &room, "none", 4))
                goto no_memory;
        if (!string_equals((string_address)text, "none") && !setting_autostart_valid((const char *)text, &bad))
        {
                desktop_give(text);
                return host_refuse("the programs to start are absolute paths, none twice; it goes wrong at byte %p\n", bad);
        }
        text[used++] = '\n';
        failed = host_write_file(SETTING_DESKTOP_AUTOSTART_PATH, text, used, 0644, true);
        desktop_give(text);
        return_if(failed < 0, host_fail("desktop autostart", failed));
        return desktop_autostart_status();

no_memory:
        desktop_give(kept);
        desktop_give(text);
        return host_fail("desktop autostart", -ENOMEM);
}

/*
        moonwater desktop startup [add app ID COMMAND...|add command COMMAND...|remove N|move N TO|on N|off N|none]: the programs the desktop
        starts with it, a line each in order (registry.c's setting_startup_entry says what one is); N counts from 1. With no file yet, desktop
        autostart's programs are its entries, each on, so what a person kept there before is kept. Each says its place and its line:
        "startup 2 on app files /files"; none says "startup none". The image's autostart script starts the ones on, in order.
*/
static p8 address_to desktop_file_read(string_address path, bipolar address_to error)
{
        positive room = 4096;
        p8 address_to text = null;
        bipolar got;

        for (;;)
        {
                p8 address_to more = (p8 address_to)memory_resize(text, room);

                if (!more)
                {
                        desktop_give(text);
                        *error = -ENOMEM;
                        return null;
                }
                text = more;
                got = host_read_state(path, text, room);
                if (got < 0 || (positive)got + 1 < room)
                        break;
                room *= 2;
        }
        *error = got < 0 ? got : 0;
        text[got < 0 ? 0 : got] = end;
        return text;
}

// A line appended to the text being made, its line's end after it, the room grown as it needs.
static bool desktop_line_append(p8 address_to address_to text, positive address_to used, positive address_to room, const char address_to a, positive a_length,
                                const char address_to b, positive b_length)
{
        positive need = *used + a_length + b_length + 2;

        if (need > *room)
        {
                positive grown = *room * 2 > need ? *room * 2 : need;
                p8 address_to more = (p8 address_to)memory_resize(*text, grown);

                if (!more)
                        return false;
                *text = more, *room = grown;
        }
        memory_copy(*text + *used, a, a_length), *used += a_length;
        memory_copy(*text + *used, b, b_length), *used += b_length;
        (*text)[(*used)++] = '\n';
        (*text)[*used] = end;
        return true;
}

// The entries as they are now: the file's text, or with none yet what desktop autostart keeps, each on. Null when it cannot be read.
static p8 address_to desktop_startup_read(bipolar address_to error)
{
        p8 address_to text = desktop_file_read((string_address)SETTING_DESKTOP_STARTUP_PATH, error), address_to old, address_to made = null;
        positive used = 0, room = 0, from = 0, length;
        const char address_to word;
        bipolar old_error;

        if (!text || *error != -ENOENT)
                return text;
        old = desktop_autostart_read(address_of old_error);
        if (!old)
                return text;
        if (old[0] && !string_equals((string_address)old, "none") && setting_autostart_valid((const char *)old, null))
                while ((word = setting_autostart_next((const char *)old, &from, &length)))
                        if (!desktop_line_append(&made, &used, &room, "on command ", 11, word, length))
                                break;
        desktop_give(old);
        if (made)
        {
                desktop_give(text);
                text = made;
        }
        *error = 0;
        return text;
}

// The lines of text that are entries, in order: their starts and lengths, in memory taken for them (given back by the caller).
static positive desktop_startup_lines(p8 address_to text, positive address_to address_to starts, positive address_to address_to lengths)
{
        positive count = 0, at = 0, total = string_length((string_address)text) + 1;
        struct setting_startup e;

        *starts = (positive address_to)memory_take(total * sizeof(positive)), *lengths = (positive address_to)memory_take(total * sizeof(positive));
        if (!*starts || !*lengths)
                return 0;
        while (text[at])
        {
                positive from = at;

                while (text[at] && text[at] != '\n')
                        at++;
                if (setting_startup_entry((const char *)text + from, at - from, &e))
                        (*starts)[count] = from, (*lengths)[count] = at - from, count++;
                if (text[at])
                        at++;
        }
        return count;
}

static b32 desktop_startup_status(void)
{
        bipolar error;
        p8 address_to text = desktop_startup_read(address_of error);
        positive address_to starts = null, address_to lengths = null, count;

        if (!text || (error < 0 && error != -ENOENT && !bowl_is_root()))
        {
                desktop_give(text);
                host_say(log, host_label "startup " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        count = desktop_startup_lines(text, &starts, &lengths);
        if (!count)
                host_say(log, host_label "startup none\n");
        for (positive k = 0; k < count; k++)
        {
                text[starts[k] + lengths[k]] = end;
                host_say(log, host_label "startup %p %s\n", k + 1, (string_address)text + starts[k]);
        }
        desktop_give(starts);
        desktop_give(lengths);
        desktop_give(text);
        return 0;
}

// A place said by the person (from 1), among count entries: its index from 0, or count when it is none of them.
static positive desktop_startup_place(string_address word, positive count)
{
        positive n = 0;

        for (positive at = 0; word && word[at]; at++)
        {
                if (word[at] < '0' || word[at] > '9' || n > count)
                        return count;
                n = n * 10 + (positive)(word[at] - '0');
        }
        return n >= 1 && n <= count ? n - 1 : count;
}

static b32 desktop_startup(string_address address_to arguments, positive count)
{
        string_address verb = count > 3 ? arguments[3] : null;
        p8 address_to text, address_to made = null;
        positive address_to starts = null, address_to lengths = null, entries, used = 0, room = 0, which, to;
        bipolar error, failed;
        b32 ok = true;

        return_if(count == 3, desktop_startup_status());
        host_need_root();
        text = desktop_startup_read(address_of error);
        return_if(!text, host_fail("desktop startup", error));
        entries = desktop_startup_lines(text, &starts, &lengths);
        which = count > 4 ? desktop_startup_place(arguments[4], entries) : entries;
        if (string_equals(verb, "add") && count >= 6 && (string_equals(arguments[4], "command") || (string_equals(arguments[4], "app") && count >= 7)))
        {
                b32 app = string_equals(arguments[4], "app");
                positive first = app ? 6 : 5;
                p8 address_to line = null;
                positive line_used = 0, line_room = 0;

                // The entry made: on, its kind (and an app's id), and the words of its command a blank apart; a line's end in them is none.
                ok = desktop_line_append(&line, &line_used, &line_room, app ? "on app " : "on command ", app ? 7 : 11,
                                         app ? (const char address_to)arguments[5] : "", app ? string_length(arguments[5]) : 0);
                if (ok && app)
                        line[line_used - 1] = ' ';
                else if (ok)
                        line_used--;
                for (positive w = first; ok && w < count; w++)
                {
                        ok = desktop_line_append(&line, &line_used, &line_room, w > first ? " " : "", w > first ? 1 : 0, (const char address_to)arguments[w],
                                                 string_length(arguments[w]));
                        if (ok)
                                line_used--;
                }
                {
                        struct setting_startup e;
                        if (!ok || !setting_startup_entry((const char address_to)line, line_used, &e))
                        {
                                desktop_give(line);
                                desktop_give(starts);
                                desktop_give(lengths);
                                desktop_give(text);
                                return ok ? host_refuse("an app's id is letters, digits and . _ - + @, and a command is one line\n") : host_fail("desktop startup", -ENOMEM);
                        }
                }
                for (positive k = 0; ok && k < entries; k++)
                        ok = desktop_line_append(&made, &used, &room, (const char address_to)text + starts[k], lengths[k], "", 0);
                ok = ok && desktop_line_append(&made, &used, &room, (const char address_to)line, line_used, "", 0);
                desktop_give(line);
        }
        else if ((string_equals(verb, "remove") || string_equals(verb, "on") || string_equals(verb, "off")) && count == 5 && which < entries)
        {
                for (positive k = 0; ok && k < entries; k++)
                {
                        const char address_to line = (const char address_to)text + starts[k];
                        positive skip = line[1] == 'n' ? 3 : 4; // past "on " or "off "
                        if (k != which)
                                ok = desktop_line_append(&made, &used, &room, line, lengths[k], "", 0);
                        else if (!string_equals(verb, "remove"))
                                ok = desktop_line_append(&made, &used, &room, string_equals(verb, "on") ? "on " : "off ", string_equals(verb, "on") ? 3 : 4,
                                                         line + skip, lengths[k] - skip);
                }
        }
        else if (string_equals(verb, "move") && count == 6 && which < entries && (to = desktop_startup_place(arguments[5], entries)) < entries)
        {
                // Every entry in order with the one moved taken out, and put back in at its new place.
                for (positive place = 0, k = 0; ok && place < entries; place++)
                {
                        positive from;
                        if (place == to)
                                from = which;
                        else
                        {
                                if (k == which)
                                        k++;
                                from = k++;
                        }
                        ok = desktop_line_append(&made, &used, &room, (const char address_to)text + starts[from], lengths[from], "", 0);
                }
        }
        else if (string_equals(verb, "none") && count == 4)
                ok = true;
        else
        {
                desktop_give(starts);
                desktop_give(lengths);
                desktop_give(text);
                return which >= entries && count > 4 && !string_equals(verb, "add") ? host_refuse("there is no entry %s\n", arguments[4]) : host_usage();
        }
        desktop_give(starts);
        desktop_give(lengths);
        desktop_give(text);
        if (!ok)
        {
                desktop_give(made);
                return host_fail("desktop startup", -ENOMEM);
        }
        failed = host_write_file((string_address)SETTING_DESKTOP_STARTUP_PATH, made ? made : (p8 address_to) "", used, 0644, true);
        desktop_give(made);
        return_if(failed < 0, host_fail("desktop startup", failed));
        return desktop_startup_status();
}

/* moonwater desktop [on|off|start|rest]: the choice kept and put in force now. rest is off put in force and not kept: the
   text console until the next start or `desktop on`, for a desktop that would not stay up on this boot (and rover's desktop
   rest). The machine script's supervisor said `desktop off` after four quick ends of the compositor, which is kept, so one
   bad boot (a card that came up late, a full disk) left every later one on the text console until somebody typed `desktop
   on`. */
static b32 host_desktop(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        return_if(count == 2, desktop_status());
        return_if(string_equals(word, "autostart"), desktop_autostart(arguments, count));
        return_if(string_equals(word, "startup"), desktop_startup(arguments, count));
        return_if(count != 3, host_usage());

        return_if(string_equals(word, "boot"), desktop_boot());
        if (string_equals(word, "start"))
        {
                host_need_root();
                return desktop_start(false);
        }
        if (string_equals(word, "rest"))
        {
                host_need_root();
                host_say(log, host_label "desktop off until the next start; moonwater desktop on starts it again\n");
                return desktop_put("off", false);
        }
        host_need_root();
        return_if(!string_equals(word, "on") && !string_equals(word, "off"),
                  host_refuse("unknown desktop %s: on or off\n", word));
        //      The choice is kept before it is put in force, and stays kept when putting it is refused: it is what every
        //      start does, and the verb is the person's word for that first and for this moment second.
        return_if(host_save("desktop", DESKTOP_PATH, string_equals(word, "on") ? (string_address) "canvas" : word), 1);
        host_say(log, host_label "desktop %s\n", word);
        return desktop_put(word, false);
}
