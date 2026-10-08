/*
        The live monitor as one long-lived utility.

        The former shell workload launched grep, awk, sort, head, date, stty,
        cp and mv on every frame and serialized typed kernel counters through
        temporary text files between them.  This consumes the shared snapshot
        records directly, keeps the preceding sample in memory and writes one
        terminal transaction.  The two-line script remains only as the stable
        /monitor.sh interface.
*/

#define MONITOR_RESTORE "\033[?2026l\033[?25h\033[0m\033[?1049l"

typedef struct
{
        struct snapshot_process address_to process;
        positive tenths;
} monitor_top;

static positive monitor_row_left;
static volatile b32 monitor_stopping;

static HOT fn monitor_row_write(address_any data, positive length)
{
        if (length > monitor_row_left)
                length = monitor_row_left;
        if (length)
        {
                text_put(data, length);
                monitor_row_left -= length;
        }
}

static fn monitor_row_end(bool newline)
{
        text_put_string("\033[K");
        if (newline)
                text_put_character('\n');
}

static fn monitor_fill(positive count, p8 byte)
{
        p8 address_to room = text_reserve(count);

        if (room)
                memory_fill(room, byte, count);
}

/*
        A number scaled by ten or a hundred, the whole part in a field and
        the fraction after the point: the cpu tenths and the load hundredths
        are one shape with a different number of places. The point and the
        places count in the width, and a width of zero pads nothing.
*/
static fn monitor_fixed(writer write, positive value, positive places,
                        positive width)
{
        fixed_decimal field = fixed_decimal_prepare(value, places, false,
                                                     width, places, 0);
        fixed_decimal_write(write, address_of field);
}

static CONST positive monitor_percent(positive part, positive whole,
                                      positive scale)
{
        if (!whole)
                return 0;
        if (part > positive_max / scale)
                return positive_max;

        positive scaled = part * scale;

        return scaled <= positive_max - whole / 2
                   ? (scaled + whole / 2) / whole
                   : scaled / whole;
}

// Swap in use and how full that is, which the layout asks before a frame
// is drawn and the memory rows ask again while drawing it.
static positive monitor_swap_percent(system_snapshot address_to sample,
                                     positive address_to used)
{
        positive total = sample->header.swap_total;
        positive taken = total >= sample->header.swap_free
                             ? total - sample->header.swap_free
                             : 0;

        if (used)
                address_to used = taken;

        return monitor_percent(taken, total, 100);
}

static fn monitor_bar(positive percent, positive width)
{
        positive filled = percent > 100 ? width : percent * width / 100;
        string_address colour = percent >= 85 ? "\033[31m"
                                : percent >= 60 ? "\033[33m"
                                                : "\033[32m";

        text_put_string("[");
        text_put_string(colour);
        monitor_fill(filled, '|');
        text_put_string("\033[0m");
        monitor_fill(width - filled, ' ');
        text_put_string("]");
}

static PURE struct snapshot_network address_to monitor_network_find(
    system_snapshot address_to sample, string_address name)
{
        for (positive i = 0; i < sample->header.network_count; i++)
                if (string_equals(sample->networks[i].name, name))
                        return sample->networks + i;

        return null;
}

/*
        Hardware: clocks, temperatures, fans, power, battery.

        Read straight from sysfs, under a root the checks can move, because
        none of it is in the snapshot and none of it needs to be: a clock is
        one small file per processor and a sensor one per value. What is
        there is found once and read every frame after, and found again
        when a read fails -- a drive or a battery gone -- and every thirty
        seconds, for one that arrived.

        Some reads are not a register. An NVMe temperature is a SMART log
        command to the drive and drivetemp's an ATA command, which keep a
        disk out of its low power states if asked twice a second; acpitz,
        a fan behind the firmware and a battery run AML that talks to the
        embedded controller. Those are read every five seconds and held in
        between. The processor's own sensors, the clocks and the energy
        counter are registers and are read every frame.

        Nothing is shown for what is not there: no row without a sensor,
        and a reading that cannot be one -- zero or below, a temperature
        past 200 C, a fan past 30,000 rpm, text that is not a number -- is
        treated as no sensor rather than drawn as a zero.

        On a Steam Deck acpitz is the embedded controller's own CPU reading,
        the one its fan loop follows, so it is worth its place next to the
        processor's; k10temp's Tctl is the other.
*/
#define MONITOR_PATH 192
#define MONITOR_SENSORS 16
#define MONITOR_CLOCKS 256
#define MONITOR_SLOW_NS ((positive)5 * SYSTEM_NANOSECONDS)
#define MONITOR_FIND_NS ((positive)30 * SYSTEM_NANOSECONDS)

enum
{
        MONITOR_TEMPERATURE,
        MONITOR_FAN,
        MONITOR_POWER,
        MONITOR_ENERGY,
        MONITOR_BATTERY,
        MONITOR_MAINS,
};

typedef struct
{
        //      The value file, or a battery's directory.
        p8 path[MONITOR_PATH];
        p8 label[12];
        p8 state[16];
        p8 kind;
        bool slow;
        bool held;
        bool counted;
        bipolar value;
        //      A battery's draw in microwatts; an energy counter's last count.
        positive extra;
        positive read_ns;
} monitor_sensor;

typedef struct
{
        string_address root;
        monitor_sensor sensor[MONITOR_SENSORS];
        positive count;
        positive found_ns;
        bool found;
        p32 khz[MONITOR_CLOCKS];
        positive clocks;
        positive khz_average;
} monitor_hardware;

//      A path under a directory, or false when it would not fit.
static bool monitor_path(p8 address_to into, string_address directory,
                         string_address name)
{
        return path_join(into, MONITOR_PATH, directory, name) + 1 < MONITOR_PATH;
}

//      One line of a small file, its newline taken off; false when the
//      file cannot be read or is empty.
static bool monitor_word(string_address path, p8 address_to into,
                         positive room)
{
        bipolar got = file_slurp_once_at(AT_FDCWD, path, into, room);

        if (got <= 0)
                return false;

        p8 address_to line_end = memory_first_of(into, '\n', (positive)got);

        if (line_end)
                address_to line_end = end;

        return into[0] != end;
}

//      A whole decimal number, signed, and nothing after it but a newline.
static bool monitor_number(string_address path, bipolar address_to value)
{
        p8 text[32];
        positive used = 0;

        if (!monitor_word(path, text, sizeof(text)))
                return false;

        bool negative = text[0] == '-';
        positive digits = string_digits_max(text + negative, 19,
                                            address_of used);

        if (!used || text[negative + used] != end)
                return false;

        address_to value = negative ? -(bipolar)digits : (bipolar)digits;
        return true;
}

static monitor_sensor address_to monitor_sensor_add(monitor_hardware address_to hardware,
                                                   p8 kind, string_address label,
                                                   bool slow)
{
        if (hardware->count >= MONITOR_SENSORS)
                return null;

        monitor_sensor address_to sensor = hardware->sensor + hardware->count++;

        memory_fill(sensor, 0, sizeof(*sensor));
        sensor->kind = kind;
        sensor->slow = slow;
        string_copy_bounded(sensor->label, label, sizeof(sensor->label));
        return sensor;
}

static bool monitor_label_is(string_address directory, positive index,
                             string_address want)
{
        p8 name[24];
        p8 path[MONITOR_PATH];
        p8 label[32];

        memory_copy_apart(name, "temp", 4);
        positive digits = positive_into(name + 4, index);
        memory_copy_apart(name + 4 + digits, "_label", 7);

        return monitor_path(path, directory, name) &&
               monitor_word(path, label, sizeof(label)) &&
               !string_compare_max(label, want, string_length(want));
}

static fn monitor_sensor_file(monitor_hardware address_to hardware,
                              string_address directory, string_address file,
                              p8 kind, string_address label, bool slow)
{
        p8 path[MONITOR_PATH];
        bipolar value;

        if (!monitor_path(path, directory, file) ||
            !monitor_number(path, address_of value))
                return;

        monitor_sensor address_to sensor = monitor_sensor_add(hardware, kind,
                                                             label, slow);

        if (sensor)
                memory_copy_apart(sensor->path, path, string_length(path) + 1);
}

/*
        One hwmon chip. Its name says what it measures: the processor's
        package (k10temp's Tctl or Tdie, coretemp's package, an ARM
        board's cpu zone), a GPU, a drive, or the firmware's zone. Any chip
        may carry fans, and a Super I/O chip carries little else worth a
        place here -- its temperature inputs are whatever the board wired
        to them, often nothing, so only its fans are read.
*/
static fn monitor_chip(monitor_hardware address_to hardware,
                       string_address directory, bool address_to have_cpu)
{
        p8 path[MONITOR_PATH];
        p8 chip[32];

        if (!monitor_path(path, directory, "name") ||
            !monitor_word(path, chip, sizeof(chip)))
                return;

        string_address label = null;
        positive index = 1;
        bool slow = false;

        if (string_equals(chip, "k10temp") || string_equals(chip, "zenpower"))
        {
                label = "cpu";
                for (positive at = 1; at <= 4; at++)
                        if (monitor_label_is(directory, at, "Tctl") ||
                            monitor_label_is(directory, at, "Tdie"))
                        {
                                index = at;
                                break;
                        }
        }
        else if (string_equals(chip, "coretemp"))
        {
                label = "cpu";
                for (positive at = 1; at <= 4; at++)
                        if (monitor_label_is(directory, at, "Package id"))
                        {
                                index = at;
                                break;
                        }
        }
        else if (string_equals(chip, "cpu_thermal") ||
                 string_equals(chip, "cpu0_thermal"))
                label = "cpu";
        else if (string_equals(chip, "amdgpu") ||
                 string_equals(chip, "radeon") ||
                 string_equals(chip, "nouveau") || string_equals(chip, "i915") ||
                 string_equals(chip, "xe"))
                label = "gpu";
        else if (string_equals(chip, "nvme"))
                label = "nvme", slow = true;
        else if (string_equals(chip, "drivetemp"))
                label = "disk", slow = true;
        else if (string_equals(chip, "acpitz"))
                label = "acpi", slow = true;

        if (label && string_equals(label, "cpu"))
        {
                if (address_to have_cpu)
                        label = null;
                else
                        address_to have_cpu = true;
        }

        if (label)
        {
                p8 file[24];

                memory_copy_apart(file, "temp", 4);
                positive digits = positive_into(file + 4, index);
                memory_copy_apart(file + 4 + digits, "_input", 7);
                monitor_sensor_file(hardware, directory, file,
                                    MONITOR_TEMPERATURE, label, slow);
        }

        //      A GPU's draw: amdgpu names it power1_average on older parts
        //      and power1_input on newer ones.
        if (label && string_equals(label, "gpu"))
        {
                positive before = hardware->count;

                monitor_sensor_file(hardware, directory, "power1_average",
                                    MONITOR_POWER, "gpu", false);
                if (hardware->count == before)
                        monitor_sensor_file(hardware, directory, "power1_input",
                                            MONITOR_POWER, "gpu", false);
        }

        //      Fans answer from whatever the chip is; a Super I/O read is
        //      port I/O and a firmware fan AML, so both wait five seconds.
        for (positive at = 1; at <= 8; at++)
        {
                p8 file[24];

                memory_copy_apart(file, "fan", 3);
                positive digits = positive_into(file + 3, at);
                memory_copy_apart(file + 3 + digits, "_input", 7);
                monitor_sensor_file(hardware, directory, file, MONITOR_FAN,
                                    "fan", true);
        }
}

typedef struct
{
        monitor_hardware address_to hardware;
        bool have_cpu;
        bool supply;
} monitor_find_context;

//      hwmon9 before hwmon10: shorter first, then by byte.
static PURE bool monitor_name_before(string_address left, string_address right)
{
        positive left_length = string_length(left);
        positive right_length = string_length(right);

        return left_length != right_length ? left_length < right_length
                                           : string_compare(left, right) < 0;
}

static fn monitor_each(string_address directory,
                       fn (address_to visit)(monitor_find_context address_to,
                                             string_address),
                       monitor_find_context address_to context)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;

        if (!file_walk_open(address_of walk, AT_FDCWD, directory))
                return;

        //      readdir's order is the kernel's, and hwmon0 before hwmon10
        //      is not it; the first cpu chip wins, so names are taken in
        //      the order a person would read them -- shorter first, then
        //      by byte -- and when there are more than the room, the
        //      lowest are the ones kept.
        p8 names[MONITOR_SENSORS * 2][40];
        positive count = 0;

        while ((entry = file_walk_next(address_of walk)))
        {
                if (entry->d_name[0] == '.')
                        continue;

                bool full = count == array_count(names);
                positive at = full ? count - 1 : count++;

                if (full && !monitor_name_before(entry->d_name, names[at]))
                        continue;

                string_copy_bounded(names[at], entry->d_name,
                                    sizeof(names[at]));

                for (; at > 0 && monitor_name_before(names[at], names[at - 1]);
                     at--)
                {
                        p8 swap[40];

                        memory_copy_apart(swap, names[at], sizeof(swap));
                        memory_copy_apart(names[at], names[at - 1], sizeof(swap));
                        memory_copy_apart(names[at - 1], swap, sizeof(swap));
                }
        }

        file_walk_close(address_of walk);

        for (positive at = 0; at < count; at++)
        {
                p8 path[MONITOR_PATH];

                if (monitor_path(path, directory, names[at]))
                        visit(context, path);
        }
}

static fn monitor_visit_hwmon(monitor_find_context address_to context,
                              string_address directory)
{
        monitor_chip(context->hardware, directory, address_of context->have_cpu);
}

/*
        A battery and the mains. A battery whose scope is Device is a
        mouse's or a controller's, not the machine's. USB-C sources are
        not mains here: a laptop with UCSI has them beside its ACAD, and
        one line saying where power comes from is the point.
*/
static fn monitor_visit_supply(monitor_find_context address_to context,
                               string_address directory)
{
        p8 path[MONITOR_PATH];
        p8 word[24];

        if (!monitor_path(path, directory, "type") ||
            !monitor_word(path, word, sizeof(word)))
                return;

        if (string_equals(word, "Battery"))
        {
                p8 scope[24];

                if (monitor_path(path, directory, "scope") &&
                    monitor_word(path, scope, sizeof(scope)) &&
                    string_equals(scope, "Device"))
                        return;

                bipolar percent;

                if (!monitor_path(path, directory, "capacity") ||
                    !monitor_number(path, address_of percent))
                        return;

                monitor_sensor address_to sensor = monitor_sensor_add(
                    context->hardware, MONITOR_BATTERY, "battery", true);

                if (sensor)
                        memory_copy_apart(sensor->path, directory,
                                          string_length(directory) + 1);
        }
        else if (string_equals(word, "Mains"))
                monitor_sensor_file(context->hardware, directory, "online",
                                    MONITOR_MAINS, "ac", true);
}

static fn monitor_hardware_find(monitor_hardware address_to hardware,
                                positive now_ns)
{
        p8 path[MONITOR_PATH];
        monitor_find_context context = {hardware, false, false};

        hardware->count = 0;
        hardware->found = true;
        hardware->found_ns = now_ns;

        if (monitor_path(path, hardware->root, "class/hwmon"))
                monitor_each(path, monitor_visit_hwmon, address_of context);

        //      The package's energy counter: RAPL on Intel, and the same
        //      driver on AMD from Zen on. Only root may read it, since the
        //      counter leaks what the processor is doing; unreadable is
        //      simply absent.
        if (monitor_path(path, hardware->root,
                         "class/powercap/intel-rapl:0"))
                monitor_sensor_file(hardware, path, "energy_uj",
                                    MONITOR_ENERGY, "cpu", false);

        if (monitor_path(path, hardware->root, "class/power_supply"))
                monitor_each(path, monitor_visit_supply, address_of context);
}

static bool monitor_sensor_read(monitor_sensor address_to sensor,
                                positive now_ns)
{
        bipolar value = 0;
        p8 path[MONITOR_PATH];

        switch (sensor->kind)
        {
        case MONITOR_TEMPERATURE:
        case MONITOR_FAN:
        case MONITOR_POWER:
        case MONITOR_MAINS:
                if (!monitor_number(sensor->path, address_of value))
                        return false;
                sensor->value = value;
                sensor->held =
                    sensor->kind == MONITOR_TEMPERATURE ? value > 0 && value <= 200000
                    : sensor->kind == MONITOR_FAN       ? value > 0 && value < 30000
                    : sensor->kind == MONITOR_POWER     ? value > 0 && value < 2000000000
                                                        : value == 0 || value == 1;
                break;

        case MONITOR_ENERGY:
        {
                if (!monitor_number(sensor->path, address_of value) || value < 0)
                        return false;

                positive count = (positive)value;
                positive elapsed = now_ns - sensor->read_ns;

                //      Watts from the counter's climb since the last frame.
                //      The first read has nothing to climb from and a wrap
                //      has no range here, so both are a frame without.
                sensor->held = sensor->counted && count >= sensor->extra &&
                               elapsed >= 1000000;
                if (sensor->held)
                {
                        positive climbed = count - sensor->extra;

                        sensor->value = climbed > positive_max / 1000
                                            ? 0
                                            : (bipolar)(climbed * 1000 /
                                                        (elapsed / 1000000));
                        sensor->held = sensor->value > 0;
                }
                sensor->extra = count;
                sensor->counted = true;
                break;
        }

        case MONITOR_BATTERY:
        {
                bipolar power = -1;
                bipolar current;
                bipolar voltage;

                if (!monitor_path(path, sensor->path, "capacity") ||
                    !monitor_number(path, address_of value))
                        return false;

                sensor->value = value;
                sensor->held = value >= 0 && value <= 100;

                if (!monitor_path(path, sensor->path, "status") ||
                    !monitor_word(path, sensor->state, sizeof(sensor->state)))
                        sensor->state[0] = end;

                if (monitor_path(path, sensor->path, "power_now") &&
                    monitor_number(path, address_of power))
                        ;
                else if (monitor_path(path, sensor->path, "current_now") &&
                         monitor_number(path, address_of current) &&
                         monitor_path(path, sensor->path, "voltage_now") &&
                         monitor_number(path, address_of voltage) &&
                         current >= 0 && voltage > 0 &&
                         current < 100000000 && voltage < 100000000)
                        power = current * voltage / 1000000;

                sensor->extra = power > 0 && power < 1000000000
                                    ? (positive)power
                                    : 0;
                break;
        }
        }

        sensor->read_ns = now_ns;
        return true;
}

/*
        The clock each processor runs at, which on x86 cpufreq reads from
        APERF and MPERF, so it is what the core did over the last tick,
        not what was asked for. Nothing without a cpufreq driver -- a
        virtual machine, mostly -- and then the column is not drawn.
*/
static fn monitor_clocks(monitor_hardware address_to hardware,
                         system_snapshot address_to sample)
{
        positive sum = 0;

        hardware->clocks = 0;
        for (positive i = 0; i < sample->header.cpu_count; i++)
        {
                unsigned int id = sample->cpus[i].id;

                if (id == ~0u || id >= MONITOR_CLOCKS)
                        continue;

                p8 name[64];
                p8 path[MONITOR_PATH];
                bipolar khz = 0;
                positive digits;

                memory_copy_apart(name, "devices/system/cpu/cpu", 22);
                digits = positive_into(name + 22, id);
                memory_copy_apart(name + 22 + digits,
                                  "/cpufreq/scaling_cur_freq", 26);
                hardware->khz[id] = 0;

                if (monitor_path(path, hardware->root, name) &&
                    monitor_number(path, address_of khz) && khz > 0 &&
                    khz < 20000000)
                {
                        hardware->khz[id] = (p32)khz;
                        sum += (positive)khz;
                        hardware->clocks++;
                }
        }

        hardware->khz_average = hardware->clocks ? sum / hardware->clocks : 0;
}

static fn monitor_hardware_read(monitor_hardware address_to hardware,
                                system_snapshot address_to sample,
                                positive now_ns)
{
        if (!hardware->found || now_ns - hardware->found_ns >= MONITOR_FIND_NS)
        {
                //      Found again, with what was read kept for the chips
                //      still there, so a slow sensor does not go blank for
                //      a frame, nor the energy counter lose its last count.
                monitor_sensor kept[MONITOR_SENSORS];
                positive kept_count = hardware->count;

                memory_copy_apart(kept, hardware->sensor,
                                  sizeof(kept[0]) * kept_count);
                monitor_hardware_find(hardware, now_ns);

                for (positive at = 0; at < hardware->count; at++)
                        for (positive was = 0; was < kept_count; was++)
                                if (kept[was].kind == hardware->sensor[at].kind &&
                                    string_equals(kept[was].path,
                                                  hardware->sensor[at].path))
                                {
                                        memory_copy_apart(hardware->sensor + at,
                                                          kept + was,
                                                          sizeof(kept[0]));
                                        break;
                                }
        }

        for (positive at = 0; at < hardware->count; at++)
        {
                monitor_sensor address_to sensor = hardware->sensor + at;

                if (sensor->slow && sensor->read_ns &&
                    now_ns - sensor->read_ns < MONITOR_SLOW_NS)
                        continue;

                if (!monitor_sensor_read(sensor, now_ns))
                {
                        //      Gone: shown as nothing, and the chips are
                        //      found again next frame.
                        sensor->held = false;
                        hardware->found = false;
                }
        }

        if (sample)
                monitor_clocks(hardware, sample);
}

static fn monitor_item_start(writer write, bool address_to first,
                             string_address label)
{
        write("  ", address_to first ? 1 : 2);
        address_to first = false;
        write((address_any)label, string_length(label));
        write(" ", 1);
}

//      Watts to one place, from microwatts.
static fn monitor_watts(writer write, positive microwatts)
{
        monitor_fixed(write, (microwatts + 50000) / 100000, 1, 0);
        write(" W", 2);
}

/*
        The temperature and fan row, and the power row: each written only
        when it has something, and the count of rows the layout sets aside
        is the same question asked without writing.
*/
static bool monitor_hardware_row(monitor_hardware address_to hardware,
                                 bool power, writer write)
{
        bool first = true;

        for (positive at = 0; at < hardware->count; at++)
        {
                monitor_sensor address_to sensor = hardware->sensor + at;
                bool mine = power ? sensor->kind >= MONITOR_POWER
                                  : sensor->kind < MONITOR_POWER;

                if (!mine || !sensor->held)
                        continue;
                if (!write)
                        return true;

                if (first)
                        write(power ? " power " : " temp  ", 7);

                switch (sensor->kind)
                {
                case MONITOR_TEMPERATURE:
                        monitor_item_start(write, address_of first, sensor->label);
                        positive_to_string(write, ((positive)sensor->value + 500) / 1000);
                        write("\xc2\xb0" "C", 3);
                        break;

                case MONITOR_FAN:
                        monitor_item_start(write, address_of first, sensor->label);
                        positive_to_string(write, (positive)sensor->value);
                        write(" rpm", 4);
                        break;

                case MONITOR_POWER:
                case MONITOR_ENERGY:
                        monitor_item_start(write, address_of first, sensor->label);
                        monitor_watts(write, (positive)sensor->value);
                        break;

                case MONITOR_BATTERY:
                        monitor_item_start(write, address_of first, sensor->label);
                        positive_to_string(write, (positive)sensor->value);
                        write("%", 1);
                        if (sensor->state[0])
                        {
                                write(" ", 1);
                                for (positive i = 0; sensor->state[i]; i++)
                                {
                                        p8 byte = sensor->state[i];

                                        //      Charging, Not charging: the
                                        //      kernel's words, in the case
                                        //      the rest of the row is in.
                                        byte = byte_is_alpha(byte)
                                                   ? (p8)byte_to_lower(byte)
                                                   : ' ';
                                        write(address_of byte, 1);
                                }
                        }
                        if (sensor->extra)
                        {
                                write(" ", 1);
                                monitor_watts(write, sensor->extra);
                        }
                        break;

                case MONITOR_MAINS:
                        monitor_item_start(write, address_of first, sensor->label);
                        if (sensor->value)
                                write("on", 2);
                        else
                                write("off", 3);
                        break;
                }
        }

        return !first;
}

static fn monitor_header(system_snapshot address_to sample,
                         string_address host, positive count,
                         positive columns)
{
        positive seconds = sample->header.uptime_ns / SYSTEM_NANOSECONDS;
        positive days = seconds / 86400;
        positive hours = seconds / 3600 % 24;
        positive minutes = seconds / 60 % 60;
        time_t now = (time_t)sample->header.realtime_seconds;
        tm broken;

        text_put_string("\033[1m ");
        writer_field_bulk(text_put, host, string_length(host), 14, ' ', true);
        text_put_string("\033[0m ");

        if (columns >= 72)
        {
                text_put_string("up ");

                if (days)
                {
                        positive_to_string(text_put, days);
                        text_put_character('d');
                        text_put_character(' ');
                }
                if (days || hours)
                {
                        positive_to_string(text_put, hours);
                        text_put_character('h');
                        text_put_character(' ');
                }
                positive_to_string(text_put, minutes);
                text_put_string("m ");

                text_put_string("load ");
                for (positive i = 0; i < 3; i++)
                {
                        monitor_fixed(text_put, sample->header.load[i], 2, 0);
                        text_put_character(' ');
                }
        }

        if (localtime_r(address_of now, address_of broken))
        {
                p8 clock[8];

                positive_into_padded(clock, (positive)broken.tm_hour, 2, '0');
                clock[2] = ':';
                positive_into_padded(clock + 3, (positive)broken.tm_min, 2, '0');
                clock[5] = ':';
                positive_into_padded(clock + 6, (positive)broken.tm_sec, 2, '0');
                text_put(clock, sizeof(clock));
        }

        text_put_string(" #");
        positive_to_padded(text_put, count % 100, 2, '0', 0);
        text_put_string("\033[K\n\033[K\n");
}

static fn monitor_cpus(system_snapshot address_to old,
                       system_snapshot address_to sample,
                       monitor_hardware address_to hardware, positive rows,
                       positive bar_width)
{
        //      The clock goes after the percentage, " 2400 MHz", and the
        //      bar gives up the room for it.
        if (hardware->clocks && bar_width > 9)
                bar_width -= 9;

        positive shown = 0;
        positive previous = 0;

        for (positive i = 0; i < sample->header.cpu_count && shown < rows; i++)
        {
                struct snapshot_cpu address_to cpu = sample->cpus + i;
                unsigned int id = cpu->id;

                while (previous < old->header.cpu_count &&
                       old->cpus[previous].id != id &&
                       (old->cpus[previous].id == ~0u ||
                        old->cpus[previous].id < id))
                        previous++;

                struct snapshot_cpu address_to before =
                    previous < old->header.cpu_count &&
                            old->cpus[previous].id == id
                        ? old->cpus + previous++
                        : null;
                positive total = before && cpu->total_ns >= before->total_ns
                                     ? cpu->total_ns - before->total_ns
                                     : 0;
                positive idle = before && cpu->idle_ns >= before->idle_ns
                                    ? cpu->idle_ns - before->idle_ns
                                    : 0;
                positive busy = total >= idle ? total - idle : 0;
                positive tenths = monitor_percent(busy, total, 1000);
                p8 name[24];
                positive name_length;

                if (cpu->id == ~0u)
                {
                        memory_copy_apart(name, "all", 3);
                        name_length = 3;
                }
                else
                {
                        memory_copy_apart(name, "cpu", 3);
                        name_length = 3 + positive_into(name + 3, cpu->id);
                }

                text_put_character(' ');
                writer_field_bulk(text_put, name, name_length, 6, ' ', true);
                text_put_string(" ");
                monitor_bar((tenths + 5) / 10, bar_width);
                text_put_character(' ');
                monitor_fixed(text_put, tenths, 1, 5);
                text_put_character('%');

                if (hardware->clocks)
                {
                        positive khz = cpu->id == ~0u ? hardware->khz_average
                                       : cpu->id < MONITOR_CLOCKS
                                           ? hardware->khz[cpu->id]
                                           : 0;

                        if (khz)
                        {
                                text_put_character(' ');
                                positive_to_padded(text_put, (khz + 500) / 1000,
                                                   4, ' ', 0);
                                text_put_string(" MHz");
                        }
                }

                text_put_string("\033[K\n");
                shown++;
        }
}

/*      One human-readable amount, with its length held inside the nine
        bytes the formatter promises. */
#define MONITOR_HUMAN 9

static positive monitor_human(p8 address_to into, positive value)
{
        positive used = positive_into_human_nearest_string(into, value, true);

        return used < MONITOR_HUMAN ? used : MONITOR_HUMAN - 1;
}

static fn monitor_memory(system_snapshot address_to sample,
                         positive bar_width)
{
        positive total = sample->header.memory_total;
        positive available = sample->header.memory_available;
        positive used = total >= available ? total - available : 0;
        positive percent = monitor_percent(used, total, 100);
        /*      Nine bytes because the widest answer the formatter gives is
                "1023 KiB" and its terminator. The lengths are clamped to that
                rather than taken on trust: the formatter is an assembly
                routine, so the compiler has no range for what it returns and
                plans the wide copy paths for a length these buffers could
                never hold. */
        p8 used_text[MONITOR_HUMAN];
        p8 total_text[MONITOR_HUMAN];
        positive used_length = monitor_human(used_text, used);
        positive total_length = monitor_human(total_text, total);

        text_put_string(" mem    ");
        monitor_bar(percent, bar_width);
        text_put_character(' ');
        positive_to_base_field(text_put, percent, 10, 3, -1, 0);
        text_put_string("%  ");
        text_put(used_text, used_length);
        text_put_string(" / ");
        text_put(total_text, total_length);
        text_put_string("\033[K\n");

        positive swap_used;
        positive swap_percent = monitor_swap_percent(sample,
                                                     address_of swap_used);

        if (swap_percent)
        {
                p8 swap_text[MONITOR_HUMAN];
                positive swap_length = monitor_human(swap_text, swap_used);

                text_put_string(" swap   ");
                monitor_bar(swap_percent, bar_width);
                text_put_character(' ');
                positive_to_base_field(text_put, swap_percent, 10, 3, -1, 0);
                text_put_string("%  ");
                text_put(swap_text, swap_length);
                text_put_string("\033[K\n");
        }
}

static fn monitor_networks(system_snapshot address_to old,
                           system_snapshot address_to sample, positive rows,
                           positive columns, positive elapsed_ns)
{
        positive shown = 0;
        positive elapsed_us = elapsed_ns / 1000;

        if (!elapsed_us)
                elapsed_us = 1;

        for (positive i = 0; i < sample->header.network_count && shown < rows;
             i++)
        {
                struct snapshot_network address_to network =
                    sample->networks + i;

                if (string_equals(network->name, "lo"))
                        continue;

                struct snapshot_network address_to before =
                    monitor_network_find(old, network->name);
                positive received = before &&
                                            network->received >= before->received
                                        ? network->received - before->received
                                        : 0;
                positive transmitted =
                    before && network->transmitted >= before->transmitted
                        ? network->transmitted - before->transmitted
                        : 0;

                if (!received && !transmitted && !network->received)
                        continue;

                positive down = received > positive_max / 1000000
                                    ? positive_max
                                    : received * 1000000 / elapsed_us;
                positive up = transmitted > positive_max / 1000000
                                  ? positive_max
                                  : transmitted * 1000000 / elapsed_us;
                p8 down_text[16];
                p8 up_text[16];
                positive down_length =
                    positive_into_human_nearest_string(down_text, down, false);
                positive up_length =
                    positive_into_human_nearest_string(up_text, up, false);
                monitor_row_left = columns > 1 ? columns - 1 : 1;
                monitor_row_write(" ", 1);
                string_to_field_bulk(monitor_row_write, network->name, 10, ' ',
                                true);
                monitor_row_write(" down ", 6);
                down_text[down_length++] = '/';
                down_text[down_length++] = 's';
                writer_field_bulk(monitor_row_write, down_text, down_length, 12,
                             ' ', true);
                monitor_row_write(" up ", 4);
                up_text[up_length++] = '/';
                up_text[up_length++] = 's';
                writer_field_bulk(monitor_row_write, up_text, up_length, 12, ' ',
                             true);
                monitor_row_end(true);
                shown++;
        }
}

static HOT fn monitor_top_insert(monitor_top address_to top, positive rows,
                                 positive address_to count,
                                 struct snapshot_process address_to process,
                                 positive tenths)
{
        positive at = 0;

        while (at < address_to count && top[at].tenths >= tenths)
                at++;

        if (at >= rows)
                return;

        positive stop = address_to count < rows ? address_to count
                                                 : rows - 1;

        if (stop > at)
                memory_copy(top + at + 1, top + at,
                            (stop - at) * sizeof(top[0]));

        top[at].process = process;
        top[at].tenths = tenths;
        if (address_to count < rows)
                address_to count += 1;
}

static bool monitor_processes(system_snapshot address_to old,
                              system_snapshot address_to sample,
                              monitor_top address_to address_to top,
                              positive address_to top_room, positive rows,
                              positive columns, positive elapsed_ns)
{
        if (!memory_reserve((address_any address_to)top, top_room, 0, rows,
                            sizeof(monitor_top), 16))
                return false;

        positive count = 0;
        positive before = 0;

        for (positive i = 0; i < sample->header.process_count; i++)
        {
                struct snapshot_process address_to process =
                    sample->processes + i;

                while (before < old->header.process_count &&
                       old->processes[before].pid < process->pid)
                        before++;

                positive used = 0;

                if (before < old->header.process_count &&
                    old->processes[before].pid == process->pid)
                {
                        positive now = system_saturating_add(
                            process->user_ns, process->system_ns);
                        positive was = system_saturating_add(
                            old->processes[before].user_ns,
                            old->processes[before].system_ns);
                        used = now >= was ? now - was : 0;
                }

                positive tenths = monitor_percent(used, elapsed_ns, 1000);

                monitor_top_insert(address_to top, rows, address_of count,
                                   process, tenths);
        }

        text_put_string("\033[K\n\033[1m pid       cpu%    memory  command\033[0m\033[K\n");

        for (positive i = 0; i < count; i++)
        {
                p8 memory_text[MONITOR_HUMAN];
                positive memory_length = monitor_human(
                    memory_text, (address_to top)[i].process->resident_bytes);
                monitor_row_left = columns > 1 ? columns - 1 : 1;
                monitor_row_write(" ", 1);
                positive_to_base_field(
                    monitor_row_write, (address_to top)[i].process->pid, 10,
                    7, -1, (positive)1 << 27);
                monitor_row_write(" ", 1);
                monitor_fixed(monitor_row_write, (address_to top)[i].tenths, 1,
                              6);
                monitor_row_write(" ", 1);
                writer_field_bulk(monitor_row_write, memory_text, memory_length, 9,
                             ' ', false);
                monitor_row_write("  ", 2);
                terminal_safe_field(
                    monitor_row_write, (address_to top)[i].process->command,
                    string_length((address_to top)[i].process->command));
                monitor_row_end(i + 1 < count);
        }

        return true;
}

static fn monitor_caught(b32 number)
{
        (void)number;
        monitor_stopping = 1;
}

#define MONITOR_SIGNAL_BLOCK 0
#define MONITOR_SIGNAL_SET_MASK 2
#define MONITOR_EINTR 4

/*
        The interval, cut short by a signal: one when it slept, zero when
        it was told to stop, negative when the kernel refused.

        The four signals that stop the monitor are blocked while the flag
        is read and let through only inside ppoll, which swaps the mask in
        and sleeps as one step. A signal landing between the test and the
        call is then delivered inside the call and ends it, where nanosleep
        after the same test slept the whole interval with the flag already
        set. ppoll with nothing to poll is the sleep every architecture
        has, and the last argument is the size of a signal set, which the
        kernel checks.
*/
static b32 monitor_sleep(p64 address_to span)
{
        positive stopping = ((positive)1 << 0) | ((positive)1 << 1) |
                            ((positive)1 << 2) | ((positive)1 << 14);
        positive previous = 0;
        b32 answer = -1;

        if (system_signal_mask(MONITOR_SIGNAL_BLOCK, address_of stopping,
                               address_of previous, 8) < 0)
                return -1;

        while (1)
        {
                if (monitor_stopping)
                {
                        answer = 0;
                        break;
                }

                // A copy, because the kernel writes what was left of the
                // interval back into the timespec it was given: the second
                // sleep on the same span was a sleep of nothing, and every
                // frame after the first came out at once.
                timespec left = {(b64)span[0], (b64)span[1]};
                bipolar polled = system_poll_wait(
                    null, 0, address_of left, address_of previous);

                if (polled >= 0)
                {
                        answer = 1;
                        break;
                }

                // Some other signal cut the sleep short, and the interval
                // starts over.
                if (polled != -MONITOR_EINTR)
                        break;
        }

        system_signal_mask(MONITOR_SIGNAL_SET_MASK, address_of previous, 0, 8);

        return answer;
}

static HOT b32 tools_monitor()
{
        positive arguments = (positive)program_argument_count();

        text_begin("monitor");
        p64 interval[2] = {0, 500000000};
        positive frames = 0;

        if (arguments > 3 ||
            (arguments > 1 &&
             (!sleep_read(program_argument(1), address_of interval[0],
                          address_of interval[1]) ||
              (!interval[0] && !interval[1]))) ||
            (arguments > 2 &&
             !string_digits_exact(program_argument(2), address_of frames)))
        {
                string_diagnostic(&text_diagnostic, 0, null, "usage: monitor [interval] [frames]");
                return text_done(2);
        }

        file_machine facts;
        p8 host[15];

        memory_fill(host, 0, sizeof(host));
        if (file_machine_read(address_of facts))
        {
                positive length = string_length_max(facts.node, 14);

                memory_copy_apart(host, facts.node, length);
        }

        static system_snapshot samples[2];
        static monitor_hardware hardware;
        system_snapshot address_to old = samples;
        system_snapshot address_to sample = old;
        static monitor_top address_to top;
        static positive top_room;
        positive count = 0;
        b32 status = 0;
        //      Said once the main screen is back: written while the
        //      alternate one is up, a reason is on the screen the restore
        //      then puts away, and the monitor seems to leave for nothing.
        string_address failed_where = null;
        string_address failed_why = null;

        monitor_stopping = 0;
        hardware.root = "/sys";
        hardware.found = false;
        hardware.count = 0;
        system_signal_install(1, (positive)monitor_caught, SIGNAL_CATCH_FLAGS,
                              SIGNAL_CATCH_RESTORER, null);
        system_signal_install(2, (positive)monitor_caught, SIGNAL_CATCH_FLAGS,
                              SIGNAL_CATCH_RESTORER, null);
        //      Ctrl+\ as well: its default ends the process where it stands,
        //      on the alternate screen with the cursor hidden, and the shell
        //      prompt comes back on a screen nothing will give back.
        system_signal_install(3, (positive)monitor_caught, SIGNAL_CATCH_FLAGS,
                              SIGNAL_CATCH_RESTORER, null);
        system_signal_install(15, (positive)monitor_caught, SIGNAL_CATCH_FLAGS,
                              SIGNAL_CATCH_RESTORER, null);
        text_put_string("\033[?1049h\033[?25l\033[2J\033[H\033[1m monitor\033[0m  sampling...\033[K");
        text_flush();

        if (!system_snapshot_take(old, SPARK_SNAPSHOT_ALL, false))
        {
                failed_where = "/proc";
                failed_why = "cannot read system snapshot";
                status = 1;
                goto finished;
        }

        /*      Frames are due at fixed points, one interval apart, and the
                sleep is to the next of them: a sleep of the whole interval
                after each frame's own work ran every frame late by that
                work, a frame in a thousand at a tenth of a second. */
        positive interval_ns = (positive)interval[0] * 1000000000u +
                               (positive)interval[1];
        positive due = clock_monotonic_nanoseconds();

        while (!monitor_stopping)
        {
                if (count)
                {
                        positive now = clock_monotonic_nanoseconds();
                        p64 wait[2] = {0, 0};

                        if (due > now)
                        {
                                positive left = due - now;

                                wait[0] = left / 1000000000;
                                wait[1] = left % 1000000000;
                        }

                        b32 slept = monitor_sleep(wait);

                        if (slept <= 0)
                        {
                                if (slept < 0)
                                {
                                        failed_why = "sleep failed";
                                        status = 1;
                                }
                                break;
                        }

                        sample = old == samples ? samples + 1 : samples;

                        if (!system_snapshot_take(sample, SPARK_SNAPSHOT_ALL,
                                                  false))
                        {
                                failed_where = "/proc";
                                failed_why = "cannot read system snapshot";
                                status = 1;
                                break;
                        }
                }

                positive elapsed_ns = sample->header.monotonic_ns >=
                                               old->header.monotonic_ns
                                          ? sample->header.monotonic_ns -
                                                old->header.monotonic_ns
                                          : 0;

                if (!elapsed_ns)
                        elapsed_ns = 10000000;

                monitor_hardware_read(address_of hardware, sample,
                                      sample->header.monotonic_ns);

                positive2 size = term_size();
                positive columns = size.width ? size.width : 80;
                positive rows = size.height ? size.height : 24;

                text_put_string("\033[?2026h\033[H");

                if (rows < 12 || columns < 40)
                {
                        text_put_string(" monitor ");
                        positive_to_string(text_put, columns);
                        text_put_character('x');
                        positive_to_string(text_put, rows);
                        text_put_string("\033[K\033[J");
                }
                else
                {
                        positive networks = 0;

                        for (positive i = 0;
                             i < sample->header.network_count; i++)
                                networks += !string_equals(
                                    sample->networks[i].name, "lo");

                        positive swap = monitor_swap_percent(sample, null) != 0;
                        positive sensors =
                            monitor_hardware_row(address_of hardware, false, null) +
                            monitor_hardware_row(address_of hardware, true, null);
                        positive network_room = rows > 7 + swap + sensors
                                                    ? rows - 7 - swap - sensors
                                                    : 0;

                        if (networks > network_room)
                                networks = network_room;

                        positive fixed = 5 + swap + sensors + networks;
                        positive available = rows > fixed ? rows - fixed : 2;

                        if (available < 2)
                                available = 2;

                        positive cpu_rows = available / 2;

                        if (cpu_rows > sample->header.cpu_count)
                                cpu_rows = sample->header.cpu_count;
                        if (!cpu_rows)
                                cpu_rows = 1;

                        positive process_rows = available - cpu_rows;

                        if (!process_rows)
                                process_rows = 1;

                        monitor_header(sample, host, count, columns);
                        monitor_cpus(old, sample, address_of hardware, cpu_rows,
                                     columns - 18);
                        monitor_memory(sample, columns - 39);

                        for (positive power = 0; power < 2; power++)
                        {
                                monitor_row_left = columns > 1 ? columns - 1 : 1;
                                if (monitor_hardware_row(address_of hardware,
                                                         power, monitor_row_write))
                                        monitor_row_end(true);
                        }
                        monitor_networks(old, sample, networks, columns,
                                         elapsed_ns);

                        if (!monitor_processes(old, sample, address_of top,
                                               address_of top_room,
                                               process_rows, columns,
                                               elapsed_ns))
                        {
                                failed_why = "cannot hold the process list";
                                status = 1;
                                break;
                        }

                        text_put_string("\033[J");
                }

                text_put_string("\033[?2026l");
                text_flush();
                count++;

                /*      A frame that could not be written ends the monitor,
                        as a closed pipe ends it by SIGPIPE when that signal
                        is not ignored. Ignored (inherited from a parent that
                        ignored it) or to a full disk, the write error was
                        otherwise dropped and frames went on for ever. */
                if (text_out_failed)
                {
                        failed_where = "write error";
                        failed_why = file_reason(text_out_error
                                                     ? text_out_error
                                                     : -ERROR_INPUT_OUTPUT);
                        status = 1;
                        break;
                }

                if (frames && count >= frames)
                        break;

                /*      A frame that overran its interval starts the next one
                        at once, and does not owe the frames it missed. */
                positive after = clock_monotonic_nanoseconds();

                due = due > positive_max - interval_ns ? positive_max
                                                       : due + interval_ns;
                if (due < after)
                        due = after;
                old = sample;
        }

finished:
        text_put_string(MONITOR_RESTORE);
        text_flush();
        if (failed_why)
                string_diagnostic(&text_diagnostic, 0, failed_where, failed_why);
        return text_done(status);
}

#undef MONITOR_RESTORE
#undef MONITOR_SIGNAL_BLOCK
#undef MONITOR_SIGNAL_SET_MASK
#undef MONITOR_EINTR
