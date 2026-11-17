/*
        src/sh/host/debug.c: the verbs debug, notices, sensors, name, time, timezone, ntp and keyboard.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

// Four hexadecimal digits of a number of a device's identity, as lsusb spells a vendor and a product.
static fn host_debug_hex(p8 address_to into, unsigned short number)
{
        p8 bytes[2] = {(p8)(number >> 8), (p8)number};

        memory_into_hex(into, bytes, 2);
        into[4] = end;
}

/*
        The input devices the compositor reads, for moonwater debug: what each
        is and what it was taken for, its wheel as it was read, and what became
        of the wheel's turns. A device names itself, so its name is spelled as
        usb_shown spells what a device says. A wheel used and sent nowhere was
        turned with no window under the pointer; one never read is a device
        that said nothing of a wheel, or whose wheel is in a report this kernel
        does not read.
*/
static fn host_debug_inputs(const struct windows address_to table)
{
        positive now = clock_monotonic_nanoseconds();
        positive turns = (positive)table->wheel_sent + table->wheel_nowhere + table->wheel_full;

        host_say(log, "input\n");
        for (unsigned int at = 0; at < WINDOWS_INPUTS; at++)
        {
                unsigned int kinds = table->input_kinds[at], bus = table->input_id[at][0];
                p8 said[WINDOWS_INPUT_NAME], name[WINDOWS_INPUT_NAME * 4 + 16], vendor[5], product[5], what[160];
                positive last = table->input_wheel_last[at], ago = now > last ? (now - last) / 100000000 : 0;

                if (table->input_number[at] < 0)
                        continue;
                memory_copy(said, table->input_name[at], sizeof(said));
                said[sizeof(said) - 1] = end;
                usb_shown(name, sizeof(name), said, string_length((string_address)said));
                host_debug_hex(vendor, table->input_id[at][1]);
                host_debug_hex(product, table->input_id[at][2]);
                host_say(log, "  event%b%s%s (%s %s:%s)\n", (bipolar)table->input_number[at],
                         table->input_number[at] < 10 ? (string_address) "          " : (string_address) "         ",
                         name[0] ? (string_address)name : (string_address) "no name",
                         bus == 3      ? (string_address) "usb"
                         : bus == 5    ? (string_address) "bluetooth"
                         : bus == 6    ? (string_address) "virtual"
                         : bus == 0x11 ? (string_address) "ps/2"
                         : bus == 0x18 ? (string_address) "i2c"
                         : bus == 0x19 ? (string_address) "the machine's own"
                                       : (string_address) "another bus",
                         vendor, product);
                what[0] = end;
                string_append_bounded(what, kinds & WINDOWS_INPUT_KEYBOARD ? ", a keyboard" : "", sizeof(what));
                string_append_bounded(what, kinds & WINDOWS_INPUT_MOUSE ? ", a mouse" : "", sizeof(what));
                string_append_bounded(what, kinds & WINDOWS_INPUT_TABLET ? ", a tablet" : "", sizeof(what));
                string_append_bounded(what, (kinds & WINDOWS_INPUT_FINGERS) && !(kinds & WINDOWS_INPUT_DIRECT) ? ", a touchpad" : "", sizeof(what));
                string_append_bounded(what, kinds & WINDOWS_INPUT_DIRECT ? ", a touch screen" : "", sizeof(what));
                string_append_bounded(what, kinds & WINDOWS_INPUT_WAKES ? ", read to wake" : "", sizeof(what));
                string_append_bounded(what, kinds & WINDOWS_INPUT_WHEEL_HI ? "; its wheel in 120ths of a notch" : kinds & WINDOWS_INPUT_WHEEL ? "; its wheel in notches" : "", sizeof(what));
                string_append_bounded(what, kinds & WINDOWS_INPUT_HWHEEL_HI ? ", sideways in 120ths" : kinds & WINDOWS_INPUT_HWHEEL ? ", sideways in notches" : "", sizeof(what));
                host_say(log, "                  %s\n", what[0] ? (string_address)what + 2 : (string_address) "taken for nothing it says it is");
                if (table->input_wheel_read[at])
                        host_say(log, "                  wheel: %p events read, %p used, the last %p.%p s ago\n",
                                 (positive)table->input_wheel_read[at], (positive)table->input_wheel_used[at], ago / 10, ago % 10);
                else if (kinds & (WINDOWS_INPUT_WHEEL | WINDOWS_INPUT_WHEEL_HI | WINDOWS_INPUT_HWHEEL | WINDOWS_INPUT_HWHEEL_HI))
                        host_say(log, "                  wheel: none read\n");
                if (table->wheel_turns[at])
                {
                        // The last five seconds of the turns the compositor used: how many, how fast (the mean and the most in a second),
                        // how many were a stream's notch, and the gaps between them (the shortest and the median), in tenths.
                        struct window_wheel_report w;

                        window_wheel_report(table, at, (unsigned long long)now, &w);
                        if (!w.notches)
                                host_say(log, "                  wheel, the last 5 s: none (%p turns in all)\n", (positive)table->wheel_turns[at]);
                        else
                                host_say(log, "                  wheel, the last 5 s: %p turns, %p.%p a second on the mean and %p in the most in one second; "
                                         "%p a stream's notch (the rest are lone notches); gaps between them %p.%p ms shortest, %p.%p ms median\n",
                                         (positive)w.notches, (positive)(w.notches * 2 / 10), (positive)(w.notches * 2 % 10), (positive)w.peak,
                                         (positive)w.streams, (positive)(w.least / 1000000), (positive)(w.least / 100000 % 10),
                                         (positive)(w.median / 1000000), (positive)(w.median / 100000 % 10));
                }
                if (table->input_dropped[at])
                        host_say(log, "                  the kernel dropped its events %p times, which were not read in time\n",
                                 (positive)table->input_dropped[at]);
        }
        if (!turns)
        {
                host_say(log, "  wheel           not turned since the compositor started\n");
                return;
        }
        host_say(log, "  wheel           %p sent to a window, %p with no window under the pointer, %p refused by a window "
                      "that had not read its last %p events\n",
                 (positive)table->wheel_sent, (positive)table->wheel_nowhere, (positive)table->wheel_full, (positive)WINDOWS_EVENTS);
        if (table->wheel_row < 0 || table->wheel_row >= WINDOWS)
        {
                host_say(log, "                  the last with no window under the pointer, at %b,%b of the screen\n",
                         (bipolar)table->wheel_x, (bipolar)table->wheel_y);
                return;
        }
        {
                p8 title[WINDOWS_TITLE + 1], shown[WINDOWS_TITLE * 4 + 16];

                memory_copy(title, table->title[table->wheel_row], WINDOWS_TITLE);
                title[WINDOWS_TITLE] = end;
                usb_shown(shown, sizeof(shown), title, string_length((string_address)title));
                host_say(log, "                  the last to window %b (%s), at %b,%b in it\n", (bipolar)table->wheel_row,
                         shown, (bipolar)table->wheel_x, (bipolar)table->wheel_y);
        }
}

/*
        A window's scroll body for moonwater debug: the body that moved last in it (ui.c's ui_body_publish, the table's body_* row),
        its state in ui.c's BODY_* order, its position and where it is drawn in device pixels, its speed in pixels a second, and the
        Scrolling switches the compositor keeps (natural direction, momentum, the rubber band and the trackpad's speed).
*/
static fn host_debug_body(const struct windows address_to table, unsigned int row)
{
        static const string_address states[] = {"idle", "track", "driven", "coast", "spring", "notch"};
        unsigned int state = table->body_state[row] < 6 ? table->body_state[row] : 0, off = table->scroll_off;

        host_say(log, "                  scroll body %s, at %b px, drawn at %b px, %b px a second; natural direction %s, momentum %s, "
                      "rubber band %s, speed %b percent\n",
                 states[state], (bipolar)table->body_raw[row], (bipolar)table->body_drawn[row], (bipolar)table->body_speed[row],
                 off & WINDOW_SCROLL_FLIP ? (string_address) "on" : (string_address) "off",
                 off & WINDOW_SCROLL_STOPS ? (string_address) "off" : (string_address) "on",
                 off & WINDOW_SCROLL_STILL ? (string_address) "off" : (string_address) "on",
                 (bipolar)(table->scroll_speed ? table->scroll_speed : 100));
}

/*
        The head of the compositor's window table, mapped to be read, or null
        when no compositor is running or the table cannot be had: taken from
        /dev/spark and else from its path, both root's. Nothing in it is
        written, and no window is taken; host_windows_unmap gives it back.
*/
static const struct windows address_to host_windows_header(void)
{
        long spark, file = -1, mapped;

        spark = window_call(WINDOW_SYS_OPENAT, -100, (long)"/dev/spark", WINDOW_OPEN_FLAGS, 0, 0, 0);
        if (spark >= 0)
        {
                file = window_call(WINDOW_SYS_IOCTL, spark, WINDOWS_IOCTL_TABLE_GET, 0, 0, 0, 0);
                window_call(WINDOW_SYS_CLOSE, spark, 0, 0, 0, 0, 0);
        }
        //      Without blocking: a FIFO at the table's name is no table, and a read-only
        //      open of one waits for a writer that is not coming.
        if (file < 0)
                file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, WINDOWS_O_READ, 0, 0, 0);
        //      A table shorter than the struct is no table this build can map:
        //      the page past its end is a bus error on the first field read,
        //      which is what an empty file (a compositor still starting) was.
        if (file >= 0 && window_call(WINDOW_SYS_LSEEK, file, 0, 2, 0, 0, 0) < (long)sizeof(struct windows))
        {
                window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                file = -1;
        }
        if (file < 0)
                return null;
        // PROT_READ, MAP_SHARED
        mapped = window_call(WINDOW_SYS_MMAP, 0, (long)sizeof(struct windows), 1, 1, file, 0);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        return mapped < 0 && mapped > -4096 ? null : (const struct windows address_to)mapped;
}

static fn host_windows_unmap(const struct windows address_to table)
{
        if (table)
                window_call(WINDOW_SYS_MUNMAP, (long)table, (long)sizeof(struct windows), 0, 0, 0, 0);
}

/*
        moonwater debug: what the desktop is doing and why, read from the head
        of the window table the compositor keeps -- its card, whether the
        pointer has a plane, the overlay, how many frames the card showed
        straight from a window's buffer, and why the top window was not at the
        last present. The table is taken from /dev/spark, which hands it to
        anyone, and else from its path; nothing in it is written, and no
        window is taken.
*/
/* ---- notices: the kernel's own errors and warnings the board has (moonwater notices [clear]) ---- */
static b32 host_notices(string_address address_to arguments, positive count)
{
        bool clearing = count == 3 && string_equals(arguments[2], "clear");
        struct windows_issues board;
        unsigned int shown = 0, cleared = 0;

        if (clearing)
        {
                struct windows address_to table;

                host_need_root();
                table = windows_board_map();

                if (!table)
                        return host_refuse("moonwater notices: the board is not there (the desktop is not running)\n");
                cleared = windows_kernel_dismiss(table, KERNEL_NOTICE_ALL);
                host_say(log, "moonwater notices: %p cleared\n", (positive)cleared);
                log_flush();
                return true;
        }
        const struct windows address_to table;
        b32 read;

        if (count != 2)
                return host_refuse("moonwater notices [clear]: the kernel's errors and warnings, or clear them (root)\n");
        table = host_windows_header();
        read = table && windows_issues_read(table, &board);
        if (!read)
        {
                if (table)
                        host_windows_unmap(table);
                return host_refuse("moonwater notices: the board is not there, or busy; try again\n");
        }
        for (int at = 0; at < WINDOWS_ISSUES; at++)
                if (board.issue[at].used && windows_issue_kernel(board.issue[at].code))
                {
                        shown++;
                        host_say(log, "%s: %s, %p records, last %p s ago: %s\n", board.issue[at].key,
                                 board.issue[at].severity == WINDOWS_ISSUE_ERROR ? (string_address) "error" : (string_address) "warning",
                                 (positive)board.issue[at].a, (positive)board.issue[at].b, board.issue[at].detail);
                }
        if (!shown)
                host_say(log, "no kernel notices\n");
        log_flush();
        host_windows_unmap(table);
        return true;
}

/*
        The processor's use, clocks, GPU load, heat, fans and power, read from the kernel's files on demand: moonwater sensors prints
        them, and moonwater debug prints the same block at its end. The processor's use is two samples of /proc/stat, 250 ms apart.
        A file that is not there is left out, and nothing is read between runs.
*/
#define HOST_POWER_CPUS 64

struct host_power_stat
{
        positive total[HOST_POWER_CPUS], idle[HOST_POWER_CPUS]; // [0] the whole machine, [1 + n] processor n
        positive count;                                          // the entries seen, the whole included
};

static b32 host_power_number(string_address path, positive address_to value)
{
        p8 text[32];
        bipolar got = host_read_kernel(path, text, sizeof(text));
        bipolar at = 0;

        if (got <= 0)
                return false;
        while (at < got && (text[at] < '0' || text[at] > '9'))
                at++;
        *value = string_digits_max(text + at, got - at, null);
        return at < got;
}

// The starred line of a dpm table (a level, then a star on the one in use): its text, copied out.
static b32 host_power_starred(string_address path, char address_to out, positive room)
{
        p8 text[256];
        bipolar got = host_read_kernel(path, text, sizeof(text));
        positive at = 0, stop = 0;

        if (got <= 0)
                return false;
        for (at = 0; at < (positive)got && text[at] != '*' && text[at] != '\n'; at++)
                ;
        if (at >= (positive)got || text[at] != '*')
                return false;
        while (at > 0 && text[at - 1] != '\n')
                at--;
        for (stop = at; stop < (positive)got && text[stop] != '\n' && stop - at + 1 < room; stop++)
                out[stop - at] = (char)text[stop];
        out[stop - at] = 0;
        return true;
}

// /proc/stat: the whole machine's line (cpu) and each processor's (cpuN), their eight times: user nice system idle iowait irq softirq steal.
static b32 host_power_stat(struct host_power_stat address_to stat)
{
        static p8 text[8192];
        bipolar got = host_read_kernel("/proc/stat", text, sizeof(text));
        bipolar at = 0;

        stat->count = 0;
        if (got <= 0)
                return false;
        while (at < got)
        {
                b32 cpu = at + 3 < got && text[at] == 'c' && text[at + 1] == 'p' && text[at + 2] == 'u';
                positive index = 0, values[8] = {0, 0, 0, 0, 0, 0, 0, 0}, field = 0;

                if (cpu && text[at + 3] >= '0' && text[at + 3] <= '9')
                {
                        for (at += 3; at < got && text[at] >= '0' && text[at] <= '9'; at++)
                                index = index * 10 + (positive)(text[at] - '0');
                        index++;
                }
                else if (cpu)
                        at += 3;
                while (at < got && text[at] != '\n')
                {
                        if (text[at] >= '0' && text[at] <= '9')
                        {
                                positive value = 0;

                                while (at < got && text[at] >= '0' && text[at] <= '9')
                                        value = value * 10 + (positive)(text[at] - '0'), at++;
                                if (field < 8)
                                        values[field] = value;
                                field++;
                        }
                        else
                                at++;
                }
                at++;
                if (cpu && index < HOST_POWER_CPUS)
                {
                        stat->total[index] = values[0] + values[1] + values[2] + values[3] + values[4] + values[5] + values[6] + values[7];
                        stat->idle[index] = values[3] + values[4];
                        if (index + 1 > stat->count)
                                stat->count = index + 1;
                }
        }
        return stat->count > 0;
}

// The processor's use over 250 ms: the whole machine, then each processor when there are sixteen or fewer.
static void host_power_use(void)
{
        static struct host_power_stat first, second;
        timespec nap = {0, 250000000};
        positive count;

        if (!host_power_stat(&first))
                return;
        system_call_2(syscall(nanosleep), (positive)address_of nap, 0);
        if (!host_power_stat(&second))
                return;
        count = first.count < second.count ? first.count : second.count;
        for (positive at = 0; at < count && at < HOST_POWER_CPUS; at++)
        {
                positive all = second.total[at] - first.total[at], idle = second.idle[at] - first.idle[at];
                positive percent = all ? (all - idle) * 100 / all : 0;

                if (at == 0)
                        host_say(log, "  power           processor %p percent busy over 250 ms, the whole machine\n", percent);
                else if (count <= 17)
                        host_say(log, "  power           processor %p: %p percent busy\n", at - 1, percent);
        }
}

static positive host_power_clock_sum, host_power_clock_top, host_power_clock_count;

static bool host_power_cpu_visit(string_address directory, string_address name, address_any context)
{
        char path[320];
        positive khz = 0;

        (void)context;
        if (name[0] != 'c' || name[1] != 'p' || name[2] != 'u' || name[3] < '0' || name[3] > '9')
                return true;
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) ||
            !host_join(path, sizeof(path), path, "/cpufreq/scaling_cur_freq") || !host_power_number(path, &khz))
                return true;
        host_power_clock_sum += khz;
        host_power_clock_count++;
        if (khz > host_power_clock_top)
                host_power_clock_top = khz;
        return true;
}

static bool host_power_drm(string_address directory, string_address name, address_any context)
{
        char path[256], line[64];
        positive busy = 0;

        (void)context;
        if (name[0] != 'c' || name[1] != 'a' || name[2] != 'r' || name[3] != 'd')
                return true;
        for (positive at = 4; name[at]; at++)
                if (name[at] < '0' || name[at] > '9')
                        return true;
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) ||
            !host_join(path, sizeof(path), path, "/device/gpu_busy_percent"))
                return true;
        if (host_power_number(path, &busy))
                host_say(log, "  power           gpu %p percent busy\n", busy);
        for (int k = 0; k < 2; k++)
        {
                string_address file = k ? "/device/pp_dpm_mclk" : "/device/pp_dpm_sclk";

                if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) ||
                    !host_join(path, sizeof(path), path, file))
                        continue;
                if (host_power_starred(path, line, sizeof(line)))
                        host_say(log, "  power           gpu %s clock in use: %s\n", k ? "memory" : "core", line);
        }
        return true;
}

// Whether name ends in suffix.
static b32 host_power_ends(string_address name, string_address suffix)
{
        positive length = 0, tail = 0;

        while (name[length])
                length++;
        while (suffix[tail])
                tail++;
        if (length < tail)
                return false;
        for (positive at = 0; at < tail; at++)
                if (name[length - tail + at] != suffix[at])
                        return false;
        return true;
}

static bool host_power_file(string_address directory, string_address name, address_any context)
{
        char path[320];
        positive value = 0;
        string_address chip = (string_address)context;

        if (!host_power_ends(name, "_input") && !host_power_ends(name, "_average"))
                return true;
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) || !host_power_number(path, &value))
                return true;
        if (name[0] == 't' && name[1] == 'e' && name[2] == 'm' && name[3] == 'p')
                host_say(log, "  power           %s %s: %p degrees\n", chip, name, value / 1000);
        else if (name[0] == 'f' && name[1] == 'a' && name[2] == 'n')
                host_say(log, "  power           %s %s: %p rpm\n", chip, name, value);
        else if (name[0] == 'f' && name[1] == 'r' && name[2] == 'e' && name[3] == 'q')
                host_say(log, "  power           %s %s: %p MHz\n", chip, name, value / 1000000);
        else if (name[0] == 'p' && name[1] == 'o' && name[2] == 'w' && name[3] == 'e' && name[4] == 'r')
                host_say(log, "  power           %s %s: %p mW\n", chip, name, value / 1000);
        return true;
}

static bool host_power_chip(string_address directory, string_address name, address_any context)
{
        static char chip[64];
        char path[320], label[64];

        (void)context;
        if (name[0] != 'h' || name[1] != 'w' || name[2] != 'm' || name[3] != 'o' || name[4] != 'n')
                return true;
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) ||
            !host_join(path, sizeof(path), path, "/name"))
                return true;
        if (host_read_kernel(path, (p8 address_to)label, sizeof(label)) <= 0)
                return true;
        for (positive at = 0; at < sizeof(label); at++)
                if (label[at] == '\n')
                        label[at] = 0;
        label[sizeof(label) - 1] = 0;
        string_copy(chip, label);
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name))
                return true;
        host_each_entry(path, host_power_file, (address_any)chip);
        return true;
}

static bool host_power_battery(string_address directory, string_address name, address_any context)
{
        char path[320];
        positive current = 0, voltage = 0;

        (void)context;
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) ||
            !host_join(path, sizeof(path), path, "/current_now") || !host_power_number(path, &current))
                return true;
        if (!host_join(path, sizeof(path), directory, "/") || !host_join(path, sizeof(path), path, name) ||
            !host_join(path, sizeof(path), path, "/voltage_now") || !host_power_number(path, &voltage))
                return true;
        // microamps times microvolts is picowatts: a thousand billion of them a milliwatt.
        host_say(log, "  power           %s draws %p mW (%p mA at %p mV)\n", name, current / 1000 * voltage / 1000000,
                 current / 1000, voltage / 1000);
        return true;
}

// The block: moonwater sensors prints it, and moonwater debug prints it at its end.
static void host_power_block(void)
{
        host_say(log, "  power           the machine as the kernel reports it, read now\n");
        host_power_use();
        host_power_clock_sum = host_power_clock_top = host_power_clock_count = 0;
        host_each_entry("/sys/devices/system/cpu", host_power_cpu_visit, null);
        if (host_power_clock_count)
                host_say(log, "  power           processor clock: mean %p MHz, highest %p MHz, over %p processors\n",
                         host_power_clock_sum / host_power_clock_count / 1000, host_power_clock_top / 1000, host_power_clock_count);
        host_each_entry("/sys/class/drm", host_power_drm, null);
        host_each_entry("/sys/class/hwmon", host_power_chip, null);
        host_each_entry("/sys/class/power_supply", host_power_battery, null);
}

static b32 host_sensors(string_address address_to arguments, positive count)
{
        (void)arguments;
        (void)count;
        host_power_block();
        return 0;
}

static b32 host_debug(string_address address_to arguments, positive count)
{
        static const string_address refusals[] = {
            "nothing", "there is no /dev/udmabuf", "udmabuf refused a buffer", "the card refused a buffer's import",
            "the card refused a buffer's framebuffer", "the card refused a flip", "the table is no memfd (memfd_create)",
            "the table is no memfd", "the table is no memfd (its size)", "the table is no memfd (its seals)",
            "the table is no memfd (its mode)", "the table is no memfd (its link)"};
        static const string_address reasons[] = {
            "shown straight", "no card", "the table is no memfd", "the pointer is drawn in software",
            "no window, or one dragged or resized", "it is glass", "it is not at the size it drew",
            "it is not wholly on the screen", "it is too short past its corners and close button", "it meets the tabs",
            "the card has no overlay for it", "the card refused its framebuffer", "the card refused the overlay",
            "the desktop spans more than one screen", "a finger's mark is drawn", "a screen is turned",
            "it is drawn from its list (the card writes its picture)", "its picture is a client's buffer, read in place",
            "it is a client's window that is not fullscreen (the overlay is for a fullscreen one)"};
        const struct windows address_to table;
        char driver[17];
        unsigned int screens;
        p8 shown[17 * 4 + 16];

        (void)arguments;
        return_if(count > 2, host_usage());
        host_say(log, "build\n  commit          %s (%s)\n", host_commit, host_commit_date);
        table = host_windows_header();
        if (!table)
        {
                host_say(log, "canvas\n  no compositor is running\n");
                return 0;
        }
        memory_copy(driver, table->driver, sizeof(table->driver));
        driver[16] = 0;
        //      What any mapper of the table wrote, as the names below are: spelled, not sent to the terminal.
        usb_shown(shown, sizeof(shown), (p8 address_to)driver, string_length((string_address)driver));
        host_say(log, "canvas\n");
        host_say(log, "  card            %s, %px%p, scale %p percent\n", shown[0] ? (string_address)shown : (string_address) "none",
                 (positive)table->output_width, (positive)table->output_height, (positive)table->scale);
        //      The screens as the table lists them, the primary first.
        screens = table->outputs <= WINDOWS_OUTPUTS ? table->outputs : 0;
        for (unsigned int at = 0; at < screens; at++)
        {
                int x, y, width, height;
                char name[WINDOWS_OUTPUT_NAME];

                windows_output(table, at, &x, &y, &width, &height);
                memory_copy(name, table->output_name[at], sizeof(name));
                name[sizeof(name) - 1] = 0;
                //      Spelled: the table is any mapper's to write, and a name is printed to a terminal.
                host_say(log, "  screen %p        %w, %px%p at %b,%b%s\n", (positive)at, writer_terminal_name,
                         name[0] ? (string_address)name : (string_address) "-", (positive)width, (positive)height, (bipolar)x,
                         (bipolar)y, at ? (string_address) "" : (string_address) ", primary");
                //      The mode it is driven at: its refresh, and the rates it lists at its size, the fastest first (the recommended one).
                {
                        char rates[WINDOWS_OUTPUT_HZ * 6 + 1];
                        unsigned int count = table->output_rates[at] <= WINDOWS_OUTPUT_HZ ? table->output_rates[at] : WINDOWS_OUTPUT_HZ;
                        positive length = 0;

                        for (unsigned int each = 0; each < count && length + 6 < sizeof(rates); each++)
                        {
                                if (each)
                                        rates[length++] = ' ';
                                length += positive_into_string((p8 address_to)rates + length, (positive)(table->output_hz[at][each] % 10000));
                        }
                        rates[length] = 0;
                        host_say(log, "    mode          %p Hz, of %s Hz at its size\n", (positive)table->output_now[at],
                                 length ? (string_address)rates : (string_address) "no listed");
                }
        }
        //      A frame: the fastest screen's period, which the compositor presents and every kit window draws by, and what the card's vblanks measure.
        host_say(log, "  frame           %p us (%p Hz), the vblanks measure %p us%s\n", (positive)table->period / 1000,
                 table->period ? (positive)((1000000000ull + table->period / 2) / table->period) : (positive)0,
                 (positive)table->period_seen / 1000, table->period_seen ? (string_address) "" : (string_address) " (no flip measured yet)");
        //      The windows the table holds, each where the compositor put it: its row, its title (a program's, so
        //      spelled), its place in the space and the size of its picture.
        for (unsigned int row = 0; row < windows_rows(table); row++)
        {
                char title[WINDOWS_TITLE];

                if (!table->pid[row])
                        continue;
                memory_copy(title, table->title[row], sizeof(title));
                title[sizeof(title) - 1] = 0;
                host_say(log, "  window %p%s       %w at %b,%b, %px%p\n", (positive)row, row < 10 ? (string_address) " " : (string_address) "",
                         writer_terminal_name, title[0] ? (string_address)title : (string_address) "-", (bipolar)table->x[row],
                         (bipolar)table->y[row], (positive)table->width[row], (positive)table->height[row]);
                //      Who it is, where it has said: its app_id (a program's word, read to its bounds and spelled) and whether it has an icon.
                {
                        char app_id[WINDOWS_APP_ID];

                        if (windows_app_id_read(table, row, app_id))
                                host_say(log, "    app           %w, %s\n", writer_terminal_name, (string_address)app_id,
                                         __atomic_load_n(&table->icon_seq[row], __ATOMIC_ACQUIRE) ? (string_address) "an icon" : (string_address) "no icon");
                }
                host_debug_body(table, row);
        }
        host_say(log, "  pointer         %s\n", table->pointer_plane == 1   ? (string_address) "on a plane of its own"
                                               : table->pointer_plane == 2 ? (string_address) "drawn in software: the plane's probe saw no vblank with it"
                                                                           : (string_address) "drawn in software");
        host_say(log, "  overlay         %s\n", table->overlay > 0    ? (string_address) "found"
                                               : table->overlay < 0 ? (string_address) "none, or refused"
                                                                    : (string_address) "not asked yet");
        host_say(log, "  shown straight  %p frames\n", (positive)table->scanned);
        //      The machine's wakes: how the last was heard, and the takes after them that set every crtc again or could not yet.
        host_say(log, "  wake            %p since the start, the last %s; the screens set again %p times, not yet %p times\n",
                 (positive)table->wakes,
                 table->wake_how == 3   ? (string_address) "told by the sleep verb and seen by the clocks"
                 : table->wake_how == 2 ? (string_address) "told by the sleep verb"
                 : table->wake_how == 1 ? (string_address) "seen by the clocks"
                                        : (string_address) "none",
                 (positive)table->wake_sets, (positive)table->wake_refused);
        //      The GPU's compose: the presents the card drew, the presents the processor drew, and why the card was refused the others.
        host_say(log, "  gpu             the GPU composed %p presents, the processor %p\n", (positive)table->gpu_drawn, (positive)table->gpu_processor);
        for (unsigned int why = 1; why < WINDOWS_GPU_REASONS; why++)
                if (table->gpu_refused[why])
                        host_say(log, "  gpu refused     %p, %s\n", (positive)table->gpu_refused[why], windows_gpu_reasons[why] ? windows_gpu_reasons[why] : (string_address) "?");
        if (table->gpu_family)
        {
                char card_driver[9];

                memory_copy(card_driver, table->gpu_req_driver, 8), card_driver[8] = 0;
                host_say(log, "  gpu card        %s family %p, device id %p\n", card_driver[0] ? (string_address)card_driver : (string_address) "-",
                         (positive)table->gpu_family, (positive)table->gpu_device);
        }
        //      The card's prerequisites in the order the path needs them (gpu.c's gpu_req): each one ok, failed (with its error and the fix in words) or not reached.
        if (!table->gpu_req_checked)
                host_say(log, "  gpu requirements not checked (the card is looked for when canvas gpu is on and a present wants it)\n");
        else
        {
                char driver[9], node[21];

                memory_copy(driver, table->gpu_req_driver, 8), driver[8] = 0;
                memory_copy(node, table->gpu_req_node, 20), node[20] = 0;
                host_say(log, "  gpu requirements %s\n", table->gpu_req_first ? (string_address) "stopped at the first that failed, below" : (string_address) "all passed");
                host_say(log, "    node          %s %s, mode %p owner %p\n", table->gpu_req_mask & 2 ? (string_address) "ok    " : (string_address) "failed",
                         node[0] ? (string_address)node : (string_address) "-", (positive)table->gpu_req_mode, (positive)table->gpu_req_owner);
                host_say(log, "    driver        %s %s\n", table->gpu_req_mask & 4 ? (string_address) "ok    " : table->gpu_req_first == 2 ? (string_address) "failed" : (string_address) "not reached",
                         driver[0] ? (string_address)driver : (string_address) "-");
                for (unsigned int step = 3; step < WINDOWS_GPU_REQS; step++)
                {
                        if (table->gpu_req_mask & 1u << step)
                                host_say(log, "    %s  ok\n", windows_gpu_req_names[step]);
                        else if (table->gpu_req_first == step)
                                host_say(log, "    %s  failed, error %p: %s\n", windows_gpu_req_names[step], (positive)table->gpu_req_error, windows_gpu_req_fixes[step]);
                        else if (table->gpu_req_route == step)
                                host_say(log, "    %s  failed, error %p: %s (the card composes; the fallback for a refused userptr is not there)\n", windows_gpu_req_names[step],
                                         (positive)table->gpu_req_route_error, windows_gpu_req_fixes[step]);
                        else
                                host_say(log, "    %s  not reached\n", windows_gpu_req_names[step]);
                }
        }
        if (table->gpu_node_bus[0] || table->gpu_display_bus[0])
        {
                char node_bus[33], display_bus[33];

                memory_copy(node_bus, table->gpu_node_bus, 32), node_bus[32] = 0;
                memory_copy(display_bus, table->gpu_display_bus, 32), display_bus[32] = 0;
                host_say(log, "  gpu node        the card is %s: %s (the display is on %s)\n", node_bus[0] ? (string_address)node_bus : (string_address) "-",
                         table->gpu_node_pick == 1 ? (string_address) "the display's own device" :
                         table->gpu_node_pick == 2 ? (string_address) "another device than the display's: it composes only if its PRIME import of the scanout works" :
                                                     (string_address) "the first node of a chip with kernels (no display's bus known)",
                         display_bus[0] ? (string_address)display_bus : (string_address) "-");
        }
        if (table->gpu_recovered)
                host_say(log, "  gpu recovered   %p times: the card lost its state (a reset or a resume) and was started again\n", (positive)table->gpu_recovered);
        if (table->gpu_map_user || table->gpu_map_dma)
                host_say(log, "  gpu maps        %p pictures mapped by userptr, %p by the dma-buf route (the table's pages, when userptr is refused or off)\n",
                         (positive)table->gpu_map_user, (positive)table->gpu_map_dma);
        if (table->metric_maps_own_user)
                host_say(log, "  gpu maps own    %p of them by userptr after the dma-buf route refused the compositor's own memory (the last refusal error %p: 22 is udmabuf past its size limit)\n",
                         (positive)table->metric_maps_own_user, (positive)table->metric_maps_own_errno);
        if (table->gpu_map_why && table->gpu_map_probe)
                host_say(log, "  gpu map probes  the node answers a plain question: %s; userptr over anonymous memory: error %p; over the refused page: error %p, without validating it: error %p (0 is taken)\n",
                         table->gpu_map_probe & 1 ? (string_address) "yes" : (string_address) "no", (positive)(table->gpu_map_probe >> 8 & 255),
                         (positive)(table->gpu_map_probe >> 16 & 255), (positive)(table->gpu_map_probe >> 24));
        if (table->gpu_said[0])
        {
                char said[257];

                memory_copy(said, (const char *)table->gpu_said, 256);
                said[256] = 0;
                host_say(log, "  gpu kernel said %s\n", (string_address)said);
        }
        // The card's last fault: the first page fault the kernel's log had since the job that failed was submitted, and what its address was in (gpu.c).
        if (table->gpu_fault[0])
        {
                char fault[sizeof(table->gpu_fault) + 1];

                memory_copy(fault, (const char *)table->gpu_fault, sizeof(table->gpu_fault));
                fault[sizeof(table->gpu_fault)] = 0;
                host_say(log, "  gpu fault       %s (%p jobs found one)\n", (string_address)fault, (positive)table->gpu_faults);
        }
        if (table->gpu_map_why)
                host_say(log, "  gpu map         the last map of a picture for the card was refused: %s (error %p)\n",
                         (table->gpu_map_why & 255) == 1 ? (string_address) "all the maps are held" :
                         (table->gpu_map_why & 255) == 2 ? (string_address) "no address left for it" :
                         (table->gpu_map_why & 255) == 3 ? (string_address) "the kernel would not take the pages" :
                         (table->gpu_map_why & 255) == 4 ? (string_address) "the kernel would not give the pages their address" :
                         (table->gpu_map_why & 255) == 5 ? (string_address) "no dma-buf route for them (not the window table's pages)" :
                         (table->gpu_map_why & 255) == 6 ? (string_address) "udmabuf would not make the pages a dma-buf" :
                         (table->gpu_map_why & 255) == 7 ? (string_address) "the render node would not import the dma-buf" :
                         (table->gpu_map_why & 255) == 9 ? (string_address) "it would let go of a map the card reads in this present (the picture is the processor's)" :
                         (table->gpu_map_why & 255) == 10 ? (string_address) "it would let go of a map while a job given up on may still read it" :
                                                           (string_address) "the card would not map the dma-buf",
                         (positive)(table->gpu_map_why >> 8));
        //      The metrics: the presents' time on the card and on the processor (the mean and the worst of the last 128 each, microseconds), the maps,
        //      the card's wakes and jobs, and the rates each second.
        host_say(log, "  metrics         the card's present: %p us mean, %p us worst; the processor's: %p us mean, %p us worst (the last 128 each)\n",
                 (positive)table->metric_card_mean, (positive)table->metric_card_worst, (positive)table->metric_processor_mean, (positive)table->metric_processor_worst);
        if (table->metric_card_split)
                host_say(log, "  metrics         the card's present taken apart: the loop %p us mean (%p worst), handed to the fence %p us (%p), the fence to the loop %p us (%p); %p presents\n",
                         (positive)table->metric_card_hand_mean, (positive)table->metric_card_hand_worst, (positive)table->metric_card_job_mean,
                         (positive)table->metric_card_job_worst, (positive)table->metric_card_seen_mean, (positive)table->metric_card_seen_worst,
                         (positive)table->metric_card_split);
        if (table->metric_clocks_level)
                host_say(log, "  clocks          input holds the card at level %p (%s now); %p asks of the driver, the last took %p us; last refusal error %p\n",
                         (positive)table->metric_clocks_level, table->metric_clocks_held ? (string_address) "held" : (string_address) "let go",
                         (positive)table->metric_clocks_asks, (positive)table->metric_clocks_took, (positive)table->metric_clocks_error);
        host_say(log, "  metrics         maps held %p (lookups that found one %p, that did not %p); clock wakes %p; jobs %p, %p us mean wait\n",
                 (positive)table->metric_maps, (positive)table->metric_map_hits, (positive)table->metric_map_misses, (positive)table->metric_wakes,
                 (positive)table->metric_jobs, (positive)table->metric_job_mean);
        host_say(log, "  metrics         jobs waited %p (%p us mean in the submit, %p us mean after it), %p not waited; scenes %p (%p us in all), copies %p (%p us in all), scaled %p (%p us in all)\n",
                 (positive)table->metric_jobs_waited, (positive)(table->metric_jobs_waited ? table->metric_job_submit_ns / table->metric_jobs_waited / 1000 : 0),
                 (positive)(table->metric_jobs_waited ? table->metric_job_wait_ns / table->metric_jobs_waited / 1000 : 0), (positive)table->metric_jobs_unwaited,
                 (positive)table->metric_gpu_jobs_scene, (positive)(table->metric_gpu_job_ns_scene / 1000), (positive)table->metric_gpu_jobs_copy,
                 (positive)(table->metric_gpu_job_ns_copy / 1000), (positive)table->metric_gpu_jobs_scale, (positive)(table->metric_gpu_job_ns_scale / 1000));
        host_say(log, "  metrics         %p presents a second, %p list frames a second\n", (positive)table->metric_presents, (positive)table->metric_lists);
        host_say(log, "  presents        %p late a second, %p missed their vblank; lead %p us; longest in the loop %p us; %p flips a second with jobs in flight (at most %p)\n",
                 (positive)table->metric_late, (positive)table->metric_missed, (positive)table->metric_lead, (positive)table->metric_present_most,
                 (positive)table->metric_flip_jobs, (positive)table->metric_flip_jobs_most);
        host_say(log, "  waits           the loop waited %p us a second on the card's jobs\n", (positive)table->metric_waited);
        host_say(log, "  wakes           compositor loop %p turns a second, %p of them with nothing to wait for\n", (positive)table->metric_loops, (positive)table->metric_loop_idle);
        host_say(log, "  wakes by        signals %p, watch %p, inputs %p, windows %p, done %p, card %p, uevents %p, clip %p, wayland %p, other %p\n",
                 (positive)table->metric_loop_by[0], (positive)table->metric_loop_by[1], (positive)table->metric_loop_by[2], (positive)table->metric_loop_by[3],
                 (positive)table->metric_loop_by[4], (positive)table->metric_loop_by[5], (positive)table->metric_loop_by[6], (positive)table->metric_loop_by[7],
                 (positive)table->metric_loop_by[8], (positive)table->metric_loop_by[9]);
        host_power_block();
        // The issue board: every issue raised, one line each, so the terminal and Settings tell the same story.
        {
                struct windows_issues board;

                if (windows_issues_read(table, &board))
                        for (int at = 0; at < WINDOWS_ISSUES; at++)
                                if (board.issue[at].used)
                                        host_say(log, "  issue           %s: %s, %s (%p, %p)%s%s\n", board.issue[at].key,
                                                 board.issue[at].severity == WINDOWS_ISSUE_ERROR ? (string_address) "error" : (string_address) "warning",
                                                 board.issue[at].code < WINDOWS_ISSUE_COUNT ? windows_issue_names[board.issue[at].code] : (string_address) "an unnamed code",
                                                 (positive)board.issue[at].a, (positive)board.issue[at].b,
                                                 board.issue[at].detail[0] ? (string_address) ": " : (string_address) "",
                                                 board.issue[at].detail);
                if (board.refused)
                        host_say(log, "  issue           %p more not shown: the board is full\n", (positive)board.refused);
        }
        //      The compositor's present record: the last state it wrote (moonwater canvas record), or none.
        {
                p8 state[256];
                bipolar got = host_read_state((string_address)HOST_RECORD_DIR "/state", state, sizeof(state) - 1);

                if (got > 0)
                {
                        state[got] = 0;
                        host_say(log, "  last record     %s", (string_address)state);
                }
                else
                        host_say(log, "  last record     none yet (moonwater canvas record SECONDS)\n");
        }
        //      The apps' draw lists: the windows drawn from their lists (list state 1) and the lists the compositor refused.
        {
                unsigned int from_lists = 0;

                for (unsigned int row = 0; row < windows_rows(table); row++)
                        if (table->pid[row] && table->list_state[row] == 1)
                                from_lists++;
                host_say(log, "  lists           %p windows drawn from their lists, %p refused; list mode %s\n", (positive)from_lists, (positive)table->list_refused,
                         table->list_mode ? (string_address) "on (touch /run/moonwater/lists-off to turn it off)" : (string_address) "off (the card is not composing, canvas gpu is off, or /run/moonwater/lists-off is there)");
                if (table->list_why[0])
                        host_say(log, "  lists drawn     %p frames\n", (positive)table->list_why[0]);
                for (unsigned int why = 1; why < 10; why++)
                        if (table->list_why[why])
                                host_say(log, why == 9 ? "  list switched   %p, %s\n" : "  list refused    %p, %s\n", (positive)table->list_why[why],
                                         why == 1 ? (string_address) "the header is not valid (size, magic, version)" :
                                         why == 2 ? (string_address) "a partial list after a frame painted in pixels" :
                                         why == 3 ? (string_address) "no card to draw it (off, away, given up, or a job running)" :
                                         why == 4 ? (string_address) "the list does not open as the window's picture" :
                                         why == 5 ? (string_address) "an op is not one a window is made of, or reaches outside" :
                                         why == 6 ? (string_address) "the program published while the ops were read" :
                                         why == 7 ? (string_address) "the cover bytes or the picture could not be mapped" :
                                         why == 8 ? (string_address) "the card did not finish the scene" :
                                                    (string_address) "frames painted in pixels by a window the card drew from its list");
                //      The lists whose header was not valid, by which of window_list_check's tests each failed.
                for (unsigned int why = 1; why < WINDOW_LIST_REASONS; why++)
                        if (table->list_invalid[why])
                                host_say(log, "    not valid     %p, %s\n", (positive)table->list_invalid[why],
                                         why == WINDOW_LIST_NONE ? (string_address) "no list, or no picture to render it at" :
                                         why == WINDOW_LIST_PUBLISHING ? (string_address) "a publish was writing its header (an odd generation)" :
                                         why == WINDOW_LIST_HEADER ? (string_address) "not the kit's list (magic, version, lost flag, or halves, room, count, cover)" :
                                         why == WINDOW_LIST_TORN ? (string_address) "a publish began while it was read" :
                                         why == WINDOW_LIST_ROOM_OTHER ? (string_address) "a room other than the window list's" :
                                         why == WINDOW_LIST_SIZE ? (string_address) "its picture is not the size the window's was taken at" :
                                         why == WINDOW_LIST_PITCH ? (string_address) "a pitch narrower than its picture" :
                                                                    (string_address) "damage outside its picture");
                if (table->list_invalid[WINDOW_LIST_SIZE])
                        host_say(log, "    last size     the list %p by %p, the window's picture taken at %p by %p\n", (positive)table->list_size_list[0],
                                 (positive)table->list_size_list[1], (positive)table->list_size_taken[0], (positive)table->list_size_taken[1]);
                //      The frames each window recorded into its list that did not record whole (the program paints them as pixels), by cause.
                for (unsigned int row = 0; row < windows_rows(table); row++)
                {
                        unsigned int lost = 0;

                        if (!table->pid[row] || !table->list_tried[row])
                                continue;
                        for (unsigned int why = 1; why < UI_LOST_COUNT; why++)
                                lost += table->list_lost[row][why];
                        host_say(log, "  lost frames     window %p: %p of %p recorded frames did not record whole\n", (positive)row, (positive)lost,
                                 (positive)table->list_tried[row]);
                        for (unsigned int why = 1; why < UI_LOST_COUNT; why++)
                                if (table->list_lost[row][why])
                                        host_say(log, "                  %p, %s\n", (positive)table->list_lost[row][why],
                                                 why == UI_LOST_NOTHING ? (string_address) "nothing recorded (a painter the recorder does not see)" :
                                                 why == UI_LOST_ROOM ? (string_address) "the ops of a half are full" :
                                                 why == UI_LOST_POOL ? (string_address) "the cover bytes are full, or a bitmap does not fit" :
                                                 why == UI_LOST_GROUND ? (string_address) "a cell's ground is not one colour" :
                                                 why == UI_LOST_CELL ? (string_address) "a cell's plan or box does not fit" :
                                                 why == UI_LOST_CELL_TEXT ? (string_address) "proportional text in a cell (font_draw on the picture)" :
                                                 why == UI_LOST_MARKS ? (string_address) "a cell of marks, wide or emoji (font_draw on the ground)" :
                                                 why == UI_LOST_TERM_GRID ? (string_address) "terminal: a fill outside the grid and the picture" :
                                                 why == UI_LOST_TERM_FONT ? (string_address) "terminal: a font draw outside the grid" :
                                                 why == UI_LOST_TERM_WIDE ? (string_address) "terminal: a wide character" :
                                                 why == UI_LOST_STRETCH ? (string_address) "a list stretched past its end (painted in pixels)" :
                                                                            (string_address) "the glass (taskbar)");
                }
        }
        //      The Wayland shim's idle clock: the seconds since it was given input, the idle inhibitors that hold (a surface shown), and the
        //      ext-idle-notify notifications that are idle. There is no screen-off timer yet, so nothing here blanks or suspends the screen.
        if (table->wl_input_ms)
                host_say(log, "  idle            %p s since the last input, %p idle inhibitors holding, %p idle notifications idle\n",
                         (positive)((clock_monotonic_nanoseconds() / 1000000ull - table->wl_input_ms) / 1000), (positive)table->wl_inhibitors,
                         (positive)table->wl_idled);
        else
                host_say(log, "  idle            no clock (the Wayland shim is off)\n");
        //      The Wayland shim's hints: the bell rings and toplevel icons (counted), and the windows that say a content type, are modal, or have an icon.
        if (table->wl_input_ms)
        {
                unsigned int content = 0, modal = 0, icon = 0;

                for (unsigned int row = 0; row < windows_rows(table); row++)
                {
                        content += table->wl_content[row] != 0;
                        modal += table->wl_modal[row] != 0;
                        icon += table->wl_icon[row] != 0;
                }
                host_say(log, "  hints           %p bell rings, %p toplevel icons set; windows: %p with a content type, %p modal, %p with an icon\n",
                         (positive)table->wl_bells, (positive)table->wl_icons, (positive)content, (positive)modal, (positive)icon);
        }
        if (table->wl_input_ms)
                host_say(log, "  shortcuts       %p windows have an active keyboard shortcuts inhibitor: the compositor's chords are off for them, and Super with Escape held a second gives the keys back\n",
                         (positive)table->wl_shortcuts);
        //      The client pictures (the Wayland shim): committed pictures read where the client's buffer is, copied, and the card's presents that read one.
        if (table->client_in_place || table->client_copied)
                host_say(log, "  client frames   %p read in place, %p copied into the table; the card drew %p presents with one in place\n",
                         (positive)table->client_in_place, (positive)table->client_copied, (positive)table->client_card);
        if (table->client_rects_card || table->client_rects_cpu)
                host_say(log, "  client rects    %p damage rectangles copied by the card, %p by the processor\n",
                         (positive)table->client_rects_card, (positive)table->client_rects_cpu);
        //      Why the client pictures were not read in place by the card (compositor_wl_lend) and their damage not copied by it
        //      (compositor_wl_card_rect): the counts by reason, the nonzero ones, and what the last refused picture was.
        {
                static const string_address lend[] = {"no pixels or not the last", "blended", "off its corner", "needs a transform",
                                                      "an X window's part", "the processor composes it", "too large", "its pitch",
                                                      "its pool cut", "the card would not map it", "the card not ready", "its import refused"};
                static const string_address card[] = {"small", "the card not ready", "the card busy", "blended", "needs a transform",
                                                      "its pool cut", "a map or import refused", "the copy failed"};
                unsigned int any = 0;

                for (unsigned int at = 0; at < 12; at++)
                        any |= table->client_lend_refused[at];
                if (any)
                {
                        char fourcc[5] = {(char)(table->client_lend_fourcc & 255u), (char)(table->client_lend_fourcc >> 8 & 255u),
                                          (char)(table->client_lend_fourcc >> 16 & 255u), (char)(table->client_lend_fourcc >> 24 & 255u), 0};

                        for (unsigned int at = 0; at < 4; at++)
                                fourcc[at] = fourcc[at] >= 32 && fourcc[at] < 127 ? fourcc[at] : '?';
                        host_say(log, "  client unlent  ");
                        for (unsigned int at = 0; at < 12; at++)
                                if (table->client_lend_refused[at])
                                        host_say(log, " %s %p;", lend[at], (positive)table->client_lend_refused[at]);
                        host_say(log, " the last a %s, format %p, fourcc %s, modifier %p\n",
                                 table->client_lend_dmabuf ? (string_address) "dma-buf" : (string_address) "pool",
                                 (positive)table->client_lend_format, table->client_lend_fourcc ? (string_address)fourcc : (string_address) "none",
                                 (positive)table->client_lend_modifier);
                }
                any = 0;
                for (unsigned int at = 0; at < 8; at++)
                        any |= table->client_card_refused[at];
                if (any)
                {
                        host_say(log, "  client uncarded");
                        for (unsigned int at = 0; at < 8; at++)
                                if (table->client_card_refused[at])
                                        host_say(log, " %s %p;", card[at], (positive)table->client_card_refused[at]);
                        host_say(log, "\n");
                }
        }
        //      A client's dma-buf refused as a framebuffer to scan out (compositor_lent_fb): how many, and what the last one asked and why.
        if (table->client_scan_refused)
        {
                char fourcc[5] = {(char)(table->client_scan_fourcc & 255u), (char)(table->client_scan_fourcc >> 8 & 255u),
                                  (char)(table->client_scan_fourcc >> 16 & 255u), (char)(table->client_scan_fourcc >> 24 & 255u), 0};

                for (unsigned int at = 0; at < 4; at++)
                        fourcc[at] = fourcc[at] >= 32 && fourcc[at] < 127 ? fourcc[at] : '?';
                host_say(log, "  client scan-out %p refused; the last: %s, modifier %p, pitch %p, offset %p, %p by %p: %s %p\n",
                         (positive)table->client_scan_refused, (string_address)fourcc, (positive)table->client_scan_modifier,
                         (positive)table->client_scan_pitch, (positive)table->client_scan_offset, (positive)table->client_scan_width,
                         (positive)table->client_scan_height,
                         table->client_scan_errno == -1 ? (string_address) "not asked, the pitch is no whole 256-byte blocks (amdgpu), " :
                         table->client_scan_errno == -2 ? (string_address) "not asked, the offset is no whole 256-byte blocks (amdgpu), " :
                         table->client_scan_errno == -3 ? (string_address) "the display's node would not take the buffer (PRIME), " :
                                                          (string_address) "the kernel's errno",
                         (positive)(table->client_scan_errno < 0 ? 0 : table->client_scan_errno));
        }
        //      A window's subsurfaces: the layers laid into the table by the processor, gathered into pictures of their own (turned, flipped,
        //      cropped or scaled) by the card and the processor, and why a window's were not kept as layers (compositor_wl_row_layers).
        {
                static const string_address layer[] = {"the shim made none", "landing or glass", "the root asked again", "the root copied",
                                                       "another window size", "the processor composes it", "cannot be gathered", "an X window's pool",
                                                       "a round corner", "its buffer, pool or map", "no room", "drawn scaled or not in place"};
                unsigned int any = table->client_layers_laid | table->client_layers_gathered[0] | table->client_layers_gathered[1];

                for (unsigned int at = 0; at < 12; at++)
                        any |= table->client_layers_refused[at];
                if (any)
                {
                        host_say(log, "  client layers   %p laid into the table by the processor, %p gathered by the card, %p by the processor;",
                                 (positive)table->client_layers_laid, (positive)table->client_layers_gathered[0],
                                 (positive)table->client_layers_gathered[1]);
                        for (unsigned int at = 0; at < 12; at++)
                                if (table->client_layers_refused[at])
                                        host_say(log, " %s %p;", layer[at], (positive)table->client_layers_refused[at]);
                        host_say(log, "\n");
                }
        }
        //      A lent dma-buf the processor read in place (its write-combined mapping): the presents it did, and the rows copied once for it.
        if (table->client_wc_presents || table->client_wc_seeded)
                host_say(log, "  client wc       %p presents the processor read a client's dma-buf in place, %p rows copied into the table for it\n",
                         (positive)table->client_wc_presents, (positive)table->client_wc_seeded);
        host_say(log, "  refused         %s\n",
                 table->refused < sizeof(refusals) / sizeof(refusals[0]) ? refusals[table->refused] : (string_address) "?");
        if (table->refused && table->refused_error)
                host_say(log, "  refused error   %p (the kernel's, as a number: 22 is EINVAL, 12 ENOMEM, 95 EOPNOTSUPP)\n", (positive)table->refused_error);
        host_say(log, "  top window      %s%s\n", table->unscanned ? (string_address) "not shown straight: " : (string_address) "",
                 table->unscanned < sizeof(reasons) / sizeof(reasons[0]) ? reasons[table->unscanned] : (string_address) "?");
        //      The on-screen keyboard: its setting, whether it is shown, the text fields with the keys in the focused window, the last input.
        {
                unsigned int fields = table->focus < WINDOWS ? table->field[table->focus] : 0;

                host_say(log, "  keyboard        setting %s; %s; the focus has %p text %s with the keys; last input %s; a physical keyboard %s; the desktop starts it %s\n",
                         table->osk_setting == 1 ? (string_address) "off" : table->osk_setting == 2 ? (string_address) "on" : (string_address) "auto",
                         table->osk_shown ? (string_address) "shown" : (string_address) "hidden", (positive)fields,
                         fields == 1 ? (string_address) "field" : (string_address) "fields",
                         table->osk_input == 2 ? (string_address) "a key" : table->osk_input == 1 ? (string_address) "a touch or a pen"
                                                                                                    : (string_address) "the mouse",
                         table->osk_physical ? (string_address) "plugged in" : (string_address) "none",
                         table->osk_start ? (string_address) "yes" : (string_address) "no");
                // The focus's text field, when it is one with the keys: its content purpose (the protocol's number, 0 normal) and the
                // bottom of its cursor in the window's pixels (0 none), which the keyboard keeps above its top.
                host_say(log, "  text input      the focus has its field's content purpose %p and cursor bottom %p (0 for none)\n",
                         (positive)table->osk_purpose, (positive)table->osk_caret);
        }
        host_debug_inputs(table);
        host_windows_unmap(table);
        return 0;
}

static b32 host_name(string_address address_to arguments, positive count)
{
        p8 name[NAME_ROOM];
        bipolar applied;

        return_if(count > 3, host_usage());

        if (count == 2)
        {
                file_machine machine;

                //      A read never rolls, saves or sets a name. Root reads
                //      the saved one; with none (a namespace whose /root is
                //      empty) root is told what the kernel calls the machine.
                //      The roll is the boot's, and `moonwater name random`
                //      asks for one. Anyone else is told what the kernel calls
                //      the machine.
                if (bowl_is_root())
                        host_read_word(NAME_PATH, name, sizeof(name));
                if (!bowl_is_root() || !name_valid(name))
                {
                        if (!file_machine_read(address_of machine))
                                return host_fail("name", -EIO);
                        string_copy_bounded(name, machine.node, sizeof(name));
                }

                //      What the kernel says can be whatever the root of a
                //      UTS namespace set, escape and bell included.
                host_say(log, "%w\n", writer_terminal_name, name);
                return 0;
        }

        host_need_root();

        if (string_equals(arguments[2], "random"))
                name_roll(name, sizeof(name));
        else
        {
                string_copy_bounded(name, arguments[2], sizeof(name));
                memory_to_lower_ascii(name, string_length(name));

                if (!name_valid(name))
                        return host_refuse("%s is not a machine name: lowercase "
                                           "letters, digits and hyphens, a "
                                           "letter or digit at each end, under "
                                           "32 characters, and not random or "
                                           "a word moonwater link takes\n",
                                           arguments[2]);
        }

        //      Kept first, so a machine whose kernel will not take the name still
        //      has it at the next boot, and says so when it does not. Kept and
        //      applied together: two names at once could end with one kept and
        //      the other the machine's.
        if (!host_acquire())
                return 1;
        if (host_save(NAME_PATH, NAME_PATH, name))
        {
                host_release();
                return 1;
        }
        applied = name_apply(name);
        host_release();
        if (applied < 0)
        {
                host_say(log_error, host_label "%s is saved, but the "
                                    "kernel did not take it: %s\n",
                         name, file_reason(applied));
                return 1;
        }

        host_say(log, "%s\n", name);
        return 0;
}

static b32 host_time(string_address address_to arguments, positive count)
{
        locale_zone_own();
        if (count == 2)
                return locale_time_status();
        return_if(count != 3 || !string_equals(arguments[2], "sync"), host_usage());
        host_need_root();
        return locale_time_sync();
}

static b32 host_timezone(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        locale_zone_own();
        if (count == 2)
                return locale_zone_status();
        return_if(count != 3, host_usage());
        if (string_equals(word, "list"))
                return locale_zone_list();
        host_need_root();
        if (string_equals(word, "auto"))
                return locale_zone_auto();
        return locale_zone_set(word);
}

static b32 host_ntp(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        locale_zone_own();
        if (count == 2)
                return locale_ntp_status();
        if (string_equals(word, "server"))
        {
                if (count == 3)
                        return locale_ntp_server_status();
                return_if(count != 4, host_usage());
                host_need_root();
                return locale_ntp_server_set(arguments[3]);
        }
        if (string_equals(word, "nts"))
                return locale_nts_command(arguments, count);
        if (string_equals(word, "sampling"))
        {
                if (count == 3)
                        return locale_ntp_sampling_status();
                return_if(count != 4 || host_onoff(arguments[3]) < 0, host_usage());
                host_need_root();
                return locale_ntp_set(true, arguments[3]);
        }
        return_if(count != 3 || host_onoff(word) < 0, host_usage());
        host_need_root();
        return locale_ntp_set(false, word);
}

//      The layout in XKB's name, alone on a line: what a compositor's
//      XKB_DEFAULT_LAYOUT is, for a script to read.
static b32 locale_keyboard_xkb(void)
{
        p8 name[16];
        string_address held = locale_keyboard_say(name, sizeof(name));
        positive found = array_where(at, array_count(locale_keyboards),
                                     string_equals(held, (string_address)locale_keyboards[at].name));

        return_if(found == array_count(locale_keyboards),
                  host_refuse("keyboard %s has no XKB name\n", held));
        host_say(log, "%s\n", (string_address)locale_keyboards[found].xkb);
        return 0;
}

/*
        moonwater keyboard start: whether the desktop starts the on-screen keyboard (osk) at its start. The machine script
        runs osk when this exits 0. The compositor decides and keeps it in the window table (osk_start): the setting on, or
        auto with a touch screen plugged in or no physical keyboard. Nothing is written and no window is taken.
*/
static b32 host_keyboard_start(void)
{
        const struct windows address_to table = host_windows_header();
        b32 starts;

        if (!table)
                return host_refuse("the compositor's desktop is not up: moonwater desktop on starts it\n");
        starts = table->osk_start != 0;
        host_windows_unmap(table);
        host_say(log, host_label "keyboard start %s\n", starts ? "yes" : "no");
        return !starts;
}

static b32 host_keyboard(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        locale_zone_own();
        if (count == 2)
                return locale_keyboard_status();
        //      repeat is the rows of the settings table: delay and rate, each by its own words.
        if (string_equals(word, "repeat"))
                return count == 3 ? host_setting_group(arguments + 1, 2) : host_usage();
        return_if(count != 3, host_usage());
        if (string_equals(word, "start"))
                return host_keyboard_start();
        if (string_equals(word, "list"))
                return locale_keyboard_list();
        if (string_equals(word, "xkb"))
                return locale_keyboard_xkb();
        host_need_root();
        return locale_keyboard_set(word);
}

