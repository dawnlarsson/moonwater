/*
        src/sh/host/tune.c: the verbs airplane, brightness, charge, battery, power, cpu, sleep and hibernate: the
        switches the kernel keeps in sysfs.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/* ---- tune: the switches the kernel keeps in sysfs. ---- */

/*
        Airplane mode, the screen's brightness, the power profile, the CPU's
        boost and SMT, the battery's charge limit and sleep: each is a file
        the kernel already offers, so each is a verb here and not a tool to
        install. A verb with no value says where the switch stands, to anybody;
        one with a value needs root. A machine without the hardware gets one
        line that says so.

        What should outlast a boot -- the profile, the CPU switches, the
        charge limit, and what airplane mode switched off -- is kept on
        /root/tune, one "key value" line each, and put back by tune_restore
        when the machine starts. Brightness is not
        kept: the screen is the thing being looked at, and the last level is
        what the firmware brings it back at.
*/
#define TUNE_KEPT "/root/tune"
#define TUNE_SYS_BACKLIGHT "/sys/class/backlight"
#define TUNE_SYS_CPU "/sys/devices/system/cpu"
#define TUNE_SYS_BATTERY "/sys/class/power_supply"
#define TUNE_SYS_PROFILE "/sys/firmware/acpi/platform_profile"
#define TUNE_SYS_POWER "/sys/power"

/* Whether a file can be opened to be read, closing it. */
static bool tune_exists(string_address path)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

        if (handle < 0)
                return false;
        system_close((positive)handle);
        return true;
}

/* Whether a name in the cpu directory is a processor's: cpu and its number. */
static bool tune_is_cpu(string_address name)
{
        return string_has_prefix(name, "cpu") && byte_is_digit(name[3]);
}

/* The best of a directory's entries, by the rank a visitor gives each, and the
   first in the directory among equals. */
typedef struct
{
        p8 name[64];
        positive rank;
} tune_choice;

static fn tune_choose(tune_choice address_to choice, string_address name, positive rank)
{
        if (rank > choice->rank && string_length(name) < sizeof(choice->name))
        {
                choice->rank = rank;
                string_copy(choice->name, name);
        }
}

/* A kernel file's word is the one asked for. */
static bool tune_says(string_address directory, string_address name, string_address leaf,
                      string_address want)
{
        p8 text[16];

        return radio_sys_read(directory, name, leaf, text, sizeof(text)) > 0 &&
               string_equals((string_address)text, want);
}

/* One decimal number from a kernel file; false when it is not there or not one. */
static bool tune_number(string_address path, positive address_to value)
{
        p8 text[32];
        positive used;

        if (host_read_kernel(path, text, sizeof(text)) <= 0)
                return false;
        *value = string_digits_max(text, 18, address_of used);
        return used && !text[used];
}

static bool tune_word(string_address path, p8 address_to into, positive room)
{
        return host_read_kernel(path, into, room) > 0;
}

/* Write a value to a sysfs attribute. The name is never followed if it is a link. */
static bipolar tune_write(string_address path, string_address text)
{
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        O_WRONLY | FILE_TRUNCATE | O_CLOEXEC | O_NOFOLLOW);
        positive length = string_length(text);
        system_write_result wrote;

        if (handle < 0)
                return handle;
        wrote = system_write_all_checked((positive)handle, (const address_any)text, length);
        system_close((positive)handle);
        return wrote.bytes == length ? 0 : wrote.error ? wrote.error : -ERROR_INPUT_OUTPUT;
}

static bipolar tune_write_number(string_address path, positive value)
{
        p8 number[24];

        positive_into_string(number, value);
        return tune_write(path, (string_address)number);
}

/* Whether a word is in a blank-separated list. */
static bool tune_has(p8 address_to text, string_address word)
{
        positive at = 0;
        positive length = string_length(word);

        while (text[at])
        {
                positive start = at;

                while (text[at] && text[at] != ' ')
                        at++;
                if (at - start == length && !memory_compare(text + start, word, length))
                        return true;
                while (text[at] == ' ')
                        at++;
        }
        return false;
}

/* Whether a word is in a file's blank-separated list. */
static bool tune_lists(string_address path, string_address word)
{
        p8 text[256];

        return tune_word(path, text, sizeof(text)) && tune_has(text, word);
}

/* Whether a line of the kept settings is the one for a key: the key, a blank,
   then the value. */
static bool tune_line_is(p8 address_to line, positive length, string_address key)
{
        positive size = string_length(key);

        return length > size && !memory_compare(line, key, size) && line[size] == ' ';
}

/* The kept settings, as far as the room goes: what the bytes are, or negative
   when none are kept or the file is not a plain one. */
static bipolar tune_load(p8 address_to text, positive room)
{
        return host_read_state(TUNE_KEPT, text, room);
}

/* The kept value for a key among settings already loaded, or false. */
static bool tune_find(p8 address_to text, positive size, string_address key,
                      p8 address_to into, positive room)
{
        positive at = 0;
        positive start;
        positive length;
        positive value;

        while (host_line_next(text, size, address_of at, address_of start,
                              address_of length))
                if (tune_line_is(text + start, length, key))
                {
                        value = length - string_length(key) - 1;
                        if (value >= room)
                                return false;
                        memory_copy(into, text + start + length - value, value);
                        into[value] = end;
                        return true;
                }
        return false;
}

/* The kept value for a key, or false. */
static bool tune_kept(string_address key, p8 address_to into, positive room)
{
        p8 text[512];
        bipolar got = tune_load(text, sizeof(text));

        return got > 0 && tune_find(text, (positive)got, key, into, room);
}

/* The keeping itself: the other lines stay, and an empty value drops the key. */
static bipolar tune_store(string_address key, string_address value)
{
        p8 text[512];
        p8 out[640];
        bipolar got = tune_load(text, sizeof(text));
        positive at = 0;
        positive used = 0;
        positive start;
        positive length;
        positive size = string_length(key);

        //      A file that filled the room is longer than this has read, and
        //      would be written back without the rest of it.
        if (got >= (bipolar)sizeof(text) - 1)
                return -EFBIG;
        //      No file is no keys. A file that is there and cannot be read (a
        //      link planted at the name, a directory, a device) is keys not
        //      known, and writing the one key over it dropped the rest.
        if (got < 0 && got != -ENOENT)
                return got;
        if (got < 0)
                got = 0;
        while (host_line_next(text, (positive)got, address_of at, address_of start,
                              address_of length))
        {
                if (tune_line_is(text + start, length, key))
                        continue;
                if (used + length + 1 >= sizeof(out))
                        return -EFBIG;
                memory_copy(out + used, text + start, length);
                used += length;
                out[used++] = '\n';
        }
        if (value[0])
        {
                if (used + size + string_length(value) + 2 >= sizeof(out))
                        return -EFBIG;
                memory_copy(out + used, key, size);
                used += size;
                out[used++] = ' ';
                memory_copy(out + used, value, string_length(value));
                used += string_length(value);
                out[used++] = '\n';
        }
        host_state_ready();
        return host_write_file(TUNE_KEPT, out, used, 0644, true);
}

/* Keep a value for a key: 0, or the error that kept it from being kept. Four
   verbs at once each read the file and wrote it whole, and what was left was
   the last one's key, so they take turns on the lock the radio verbs use. */
static bipolar tune_keep(string_address key, string_address value)
{
        bipolar lock = radio_lock(true);
        bipolar failed;

        if (lock < 0)
                return lock;
        failed = tune_store(key, value);
        radio_unlock(lock);
        return failed;
}

/* tune_keep, with what it could not do said: 0, or 1 when it was. */
static b32 tune_remember(string_address key, string_address value)
{
        bipolar failed = tune_keep(key, value);

        return failed < 0 ? host_fail(TUNE_KEPT, failed) : 0;
}

/* A whole percent, "N", "N%", "+N" or "-N": the sign says relative. */
static bool tune_percent(string_address text, bool address_to relative, bool address_to lower,
                         positive address_to value)
{
        positive at = 0;
        positive number;
        positive used;

        *relative = *lower = false;
        if (text[at] == '+' || text[at] == '-')
        {
                *relative = true;
                *lower = text[at] == '-';
                at++;
        }
        if (!byte_is_digit(text[at]))
                return false;
        //      Nineteen digits is as many as a word holds, and more is not a
        //      percent whatever it starts with.
        number = string_digits_max(text + at, 19, address_of used);
        at += used;
        if (number > 100000)
                return false;
        if (text[at] == '%')
                at++;
        *value = number;
        return !text[at];
}

/* moonwater airplane [on|off]: every radio, at once, and back as they were.
   What was on is kept in /root/tune as the key airplane, so that a radio that
   was off before stays off after, and the block of every radio, the modem
   included, outlasts a boot. With nothing kept both come back. */
static b32 tune_airplane(string_address address_to arguments, positive count)
{
        bool wifi_off = radio_kept(NET_WIFI_POWER) == 0;
        bool bluetooth_off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        p8 was[24];
        bool kept = tune_kept("airplane", was, sizeof(was));
        bool on;
        bool join = false;
        b32 failed;
        bipolar sent;

        //      Airplane mode is the record that it was asked for, with every radio still off: radios that are off
        //      because nothing raised them are not airplane mode, and raising one by hand ends it.
        if (count == 2)
        {
                host_say(log, host_label "airplane %s\n",
                         kept && wifi_off && bluetooth_off ? "on" : "off");
                return 0;
        }
        return_if(count != 3 || host_onoff(arguments[2]) < 0, host_usage());
        host_need_root();
        on = host_onoff(arguments[2]) > 0;

        //      What it goes by is read under the machine's lock, and held to the end: on and
        //      off at once each read the radios as they were, and each did half of the other's
        //      work (airplane off recorded, and both radios left off).
        if (!host_acquire())
                return 1;
        wifi_off = radio_kept(NET_WIFI_POWER) == 0;
        bluetooth_off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        kept = tune_kept("airplane", was, sizeof(was));

        //      Type zero is every radio, the modem included.
        if (on)
        {
                //      What was on is kept, and none when nothing was, so that off gives back exactly that: a
                //      machine whose radios are off by default stays so across on and off. Asked again while it
                //      holds, the first record stands.
                failed = kept && wifi_off && bluetooth_off
                             ? 0
                             : tune_remember("airplane", wifi_off && bluetooth_off ? "none"
                                                         : wifi_off ? "bluetooth"
                                                         : bluetooth_off ? "wifi" : "wifi bluetooth");
                failed |= radio_wifi_off(true);
                failed |= radio_bluetooth_power(false, true);
                sent = radio_rfkill(0, true);
        }
        else
        {
                bool wifi_back = kept && tune_has(was, "wifi");
                bool bluetooth_back = kept && tune_has(was, "bluetooth");

                sent = radio_rfkill(0, false);
                failed = 0;
                if (wifi_back)
                {
                        failed |= radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, true);
                        join = true;
                }
                //      A block that the kernel refuses is the first error said,
                //      as the unblock above is, and not dropped.
                else if (wifi_off)
                        sent = sent < 0 ? sent : radio_rfkill(RADIO_RFKILL_WLAN, true);
                if (bluetooth_back)
                        failed |= radio_bluetooth_power(true, true);
                else if (bluetooth_off)
                        sent = sent < 0 ? sent : radio_rfkill(RADIO_RFKILL_BLUETOOTH, true);
                if (kept)
                        failed |= tune_remember("airplane", "");
        }
        failed |= sent < 0 ? host_fail("/dev/rfkill", sent) : 0;
        host_release();
        //      The join takes what it takes, with the machine's lock let go of.
        if (join)
                (void)radio_wifi_on(false);
        return radio_done(failed, on ? "airplane on" : "airplane off");
}

/* The panel's own backlight before a raw one the driver also offers, the way
   systemd-backlight orders them: firmware's, then the platform's, then raw. */
static bool tune_backlight_visit(string_address directory, string_address name,
                                 address_any context)
{
        tune_choose((tune_choice address_to)context, name,
                    tune_says(directory, name, "/type", "firmware") ? 3
                    : tune_says(directory, name, "/type", "platform") ? 2 : 1);
        return true;
}

/* moonwater brightness [N|N%|+N|-N]: the panel's backlight, in percent. */
static b32 tune_brightness(string_address address_to arguments, positive count)
{
        p8 name[64];
        p8 maximum_path[160];
        p8 current_path[160];
        positive maximum = 0;
        positive current = 0;
        positive percent = 0;
        bool relative = false;
        bool lower = false;

        if (count > 3 || (count == 3 && !tune_percent(arguments[2], address_of relative,
                                                      address_of lower, address_of percent)))
                return host_usage();
        if (count == 3)
                host_need_root();

        tune_choice panel = {.rank = 0};

        host_each_entry(TUNE_SYS_BACKLIGHT, tune_backlight_visit, address_of panel);
        string_copy(name, panel.name);
        if (!panel.rank ||
            !string_join(maximum_path, sizeof(maximum_path), TUNE_SYS_BACKLIGHT "/",
                         (string_address)name, "/max_brightness") ||
            !string_join(current_path, sizeof(current_path), TUNE_SYS_BACKLIGHT "/",
                         (string_address)name, "/brightness") ||
            !tune_number(maximum_path, address_of maximum) || !maximum ||
            !tune_number(current_path, address_of current))
        {
                //      Where it stands is an answer, and "nowhere" is one.
                if (count == 2)
                {
                        host_say(log, host_label "this machine has no backlight\n");
                        return 0;
                }
                return host_refuse("this machine has no backlight to set\n");
        }

        positive now = (current * 100 + maximum / 2) / maximum;

        if (count == 2)
        {
                host_say(log, host_label "brightness %p%% (%p of %p)\n", now, current, maximum);
                return 0;
        }

        {
                positive target = percent;
                positive value;
                bipolar failed;

                if (relative)
                        target = lower ? (percent > now ? 0 : now - percent) : now + percent;
                if (target > 100)
                        target = 100;
                //      A percent above nought is never rounded down to a dark screen.
                value = (maximum * target + 99) / 100;
                if (target && !value)
                        value = 1;
                failed = tune_write_number(current_path, value);
                return_if(failed < 0, host_fail("brightness", failed));
                host_say(log, host_label "brightness %p%%\n", target);
        }
        return 0;
}

/* A battery of the machine's own, and best the one that can limit its charge.
   A mouse or a keyboard reports its battery here too, with the scope Device,
   and is not the one a charge limit is for. */
static bool tune_battery_visit(string_address directory, string_address name, address_any context)
{
        p8 path[160];

        if (tune_says(directory, name, "/type", "Battery") &&
            !tune_says(directory, name, "/scope", "Device"))
        {
                bool limited = string_join(path, sizeof(path), directory, "/", name,
                                           "/charge_control_end_threshold") &&
                               tune_exists(path);

                tune_choose((tune_choice address_to)context, name, limited ? 2 : 1);
        }
        return true;
}

static bool tune_battery(p8 address_to name, positive room)
{
        tune_choice battery = {.rank = 0};

        host_each_entry(TUNE_SYS_BATTERY, tune_battery_visit, address_of battery);
        return battery.rank && string_length(battery.name) < room &&
               (string_copy(name, battery.name), true);
}

static bipolar tune_charge_apply(string_address percent_text)
{
        p8 name[64];
        p8 path[160];
        bipolar failed = -ENODEV;

        if (!tune_battery(name, sizeof(name)))
                return failed;
        if (!string_join(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
                         "/charge_control_end_threshold"))
                return -ERROR_INVALID;
        return tune_write(path, percent_text);
}

/* moonwater charge [limit N|off]: where the battery stops charging. */
static b32 tune_charge(string_address address_to arguments, positive count)
{
        p8 name[64];
        p8 path[160];
        positive capacity = 0;
        positive limit = 0;
        p8 status[24];
        bool have_limit;

        if (count != 2 && !(count == 4 && string_equals(arguments[2], "limit") &&
                            (string_equals(arguments[3], "off") ||
                             byte_is_digit(arguments[3][0]))))
                return host_usage();

        if (!tune_battery(name, sizeof(name)))
        {
                if (count == 2)
                {
                        host_say(log, host_label "this machine has no battery\n");
                        return 0;
                }
                return host_refuse("this machine has no battery\n");
        }

        if (count == 2)
        {
                bool have_capacity;

                have_limit = string_join(path, sizeof(path), TUNE_SYS_BATTERY "/",
                                         (string_address)name, "/charge_control_end_threshold") &&
                             tune_number(path, address_of limit);
                have_capacity = string_join(path, sizeof(path), TUNE_SYS_BATTERY "/",
                                            (string_address)name, "/capacity") &&
                                tune_number(path, address_of capacity);
                status[0] = end;
                if (string_join(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
                                "/status"))
                        (void)tune_word(path, status, sizeof(status));
                //      A battery that gives no capacity says so, and does not say 0 percent.
                if (have_capacity)
                        string_format(log, host_label "battery %p%%", capacity);
                else
                        string_format(log, host_label "battery charge unknown");
                if (status[0])
                        string_format(log, ", %s", (string_address)status);
                if (have_limit)
                        string_format(log, "; charging stops at %p%%\n", limit);
                else
                        string_format(log, "; this battery has no charge limit\n");
                log_flush();
                return 0;
        }

        {
                positive number = 100;
                bipolar failed;

                if (!string_equals(arguments[3], "off"))
                {
                        positive used;

                        number = string_digits_max(arguments[3], 4, address_of used);
                        //      A letter after the digits is no number, so a usage; more digits are one out of range.
                        if (arguments[3][used] && !byte_is_digit(arguments[3][used]))
                                return host_usage();
                        if (arguments[3][used] || number < 20 || number > 100)
                                return host_refuse("a charge limit is 20 to 100 percent\n");
                }
                host_need_root();
                {
                        p8 text[8];

                        positive_into_string(text, number);
                        failed = tune_charge_apply((string_address)text);
                        if (failed < 0)
                                return failed == -ENODEV
                                           ? host_refuse("this machine has no battery\n")
                                           : failed == -ENOENT
                                                 ? host_refuse("this battery has no charge limit\n")
                                                 : host_fail("charge", failed);
                        //      A limit of a full charge is the default: nothing to bring back.
                        if (tune_remember("charge.limit", number == 100 ? (string_address)"" : (string_address)text))
                                return 1;
                }
                host_say(log, host_label "charging stops at %p%%\n", number);
                return 0;
        }
}

typedef struct
{
        string_address governor;
        string_address preference;
        positive written;
} tune_profile;

static bool tune_profile_visit(string_address directory, string_address name, address_any context)
{
        tune_profile address_to profile = (tune_profile address_to)context;
        p8 path[160];
        p8 policy[160];

        string_address governor = profile->governor;
        p8 driver[24];

        if (!tune_is_cpu(name) ||
            !string_join(policy, sizeof(policy), directory, "/", name) ||
            string_append_bounded(policy, "/cpufreq/", sizeof(policy)) >= sizeof(policy))
                return true;
        //      A driver that picks the P-state itself (intel_pstate, amd-pstate-epp: active mode) offers no schedutil, and its powersave is
        //      the dynamic range with the energy preference steering it, which is what balanced means there. Without this, balanced after
        //      performance left the performance governor in place, and the preference's write was refused under it (EBUSY), so the machine
        //      could not be brought back from performance at all.
        driver[0] = end;
        if (governor && string_equals(governor, "schedutil") &&
            string_join(path, sizeof(path), (string_address)policy, "scaling_available_governors") && !tune_lists(path, governor) &&
            string_join(path, sizeof(path), (string_address)policy, "scaling_driver") && tune_word(path, driver, sizeof(driver)) &&
            (string_equals((string_address)driver, "intel_pstate") || string_equals((string_address)driver, "amd-pstate-epp")))
                governor = (string_address)"powersave";
        if (governor && string_join(path, sizeof(path), (string_address)policy,
                                    "scaling_available_governors") &&
            tune_lists(path, governor) &&
            string_join(path, sizeof(path), (string_address)policy, "scaling_governor"))
                profile->written += tune_write(path, governor) >= 0;
        if (profile->preference && string_join(path, sizeof(path), (string_address)policy,
                                               "energy_performance_available_preferences") &&
            tune_lists(path, profile->preference) &&
            string_join(path, sizeof(path), (string_address)policy,
                        "energy_performance_preference"))
                profile->written += tune_write(path, profile->preference) >= 0;
        return true;
}

/* The governor and preference a profile means, on every processor that has them: how many writes went through. */
static positive tune_profile_cpus(string_address governor, string_address preference)
{
        tune_profile profile = {governor, preference, 0};

        host_each_entry(TUNE_SYS_CPU, tune_profile_visit, address_of profile);
        return profile.written;
}

/*
        The three profiles, each as the platform names it, and the governor and
        the energy preference it sets on every processor. Only the lowest has a
        fallback: firmware that has no low-power may name its lowest quiet or cool.
*/
typedef struct
{
        string_address name;
        string_address platform;
        string_address governor;
        string_address preference;
        bool lowest;
} tune_mode;

static const tune_mode tune_modes[] = {
    {(string_address)"performance", (string_address)"performance", (string_address)"performance",
     (string_address)"performance", false},
    {(string_address)"balanced", (string_address)"balanced", (string_address)"schedutil",
     (string_address)"balance_performance", false},
    {(string_address)"powersave", (string_address)"low-power", (string_address)"powersave",
     (string_address)"power", true},
};

/* The profile a word names, or null. */
static const tune_mode address_to tune_mode_named(string_address name)
{
        for (positive at = 0; at < sizeof(tune_modes) / sizeof(tune_modes[0]); at++)
                if (string_equals(name, tune_modes[at].name))
                        return &tune_modes[at];
        return null;
}

/* Apply one of performance, balanced and powersave; false when the machine has no way to. */
static bool tune_power_apply(string_address name)
{
        const tune_mode address_to mode = tune_mode_named(name);
        string_address platform;
        bool any = false;

        if (!mode)
                return false;
        platform = mode->platform;
        if (!tune_lists(TUNE_SYS_PROFILE "_choices", platform) && mode->lowest)
                platform = tune_lists(TUNE_SYS_PROFILE "_choices", "quiet") ? (string_address)"quiet"
                           : tune_lists(TUNE_SYS_PROFILE "_choices", "cool") ? (string_address)"cool"
                                                                              : platform;
        if (tune_lists(TUNE_SYS_PROFILE "_choices", platform))
                any = tune_write(TUNE_SYS_PROFILE, platform) >= 0;

        //      A way to is a write that went through: a governor that can be read
        //      and never set is no profile applied.
        any |= tune_profile_cpus(mode->governor, mode->preference) > 0;
        return any;
}

/* moonwater power [performance|balanced|powersave] */
static b32 tune_power(string_address address_to arguments, positive count)
{
        if (count == 2)
        {
                p8 profile[32];
                p8 governor[32];
                p8 preference[40];
                p8 kept[24];

                profile[0] = governor[0] = preference[0] = end;
                (void)tune_word(TUNE_SYS_PROFILE, profile, sizeof(profile));
                (void)tune_word(TUNE_SYS_CPU "/cpu0/cpufreq/scaling_governor", governor, sizeof(governor));
                (void)tune_word(TUNE_SYS_CPU "/cpu0/cpufreq/energy_performance_preference", preference,
                                sizeof(preference));
                if (!profile[0] && !governor[0])
                {
                        host_say(log, host_label "this machine has no power profile\n");
                        return 0;
                }
                kept[0] = end;
                (void)tune_kept("power", kept, sizeof(kept));
                string_format(log, host_label "power %s", kept[0] ? (string_address)kept : (string_address)"(not set here)");
                if (profile[0])
                        string_format(log, "; platform profile %s", (string_address)profile);
                if (governor[0])
                        string_format(log, "; governor %s", (string_address)governor);
                if (preference[0])
                        string_format(log, "; preference %s", (string_address)preference);
                host_say(log, "\n");
                return 0;
        }
        if (count != 3 || !tune_mode_named(arguments[2]))
                return host_usage();
        host_need_root();
        if (!tune_power_apply(arguments[2]))
                return host_refuse("this machine has no power profile to set\n");
        if (tune_remember("power", arguments[2]))
                return 1;
        host_say(log, host_label "power %s\n", arguments[2]);
        return 0;
}

/* The boost switch the machine has, or -ENOENT when it has neither. */
static bipolar tune_cpu_boost(bool on)
{
        bipolar failed = tune_write(TUNE_SYS_CPU "/cpufreq/boost", on ? "1" : "0");

        return failed == -ENOENT ? tune_write(TUNE_SYS_CPU "/intel_pstate/no_turbo", on ? "0" : "1")
                                 : failed;
}


/* A processor with no switch to read is online: cpu0 has none. */
static bool tune_online_visit(string_address directory, string_address name, address_any context)
{
        p8 path[160];
        p8 word[4];

        if (tune_is_cpu(name))
                *(positive address_to)context += !string_join(path, sizeof(path), directory, "/",
                                                              name) ||
                                                 string_append_bounded(path, "/online", sizeof(path)) >= sizeof(path) ||
                                                 !tune_word(path, word, sizeof(word)) || word[0] == '1';
        return true;
}

/* moonwater cpu [boost|smt on|off] [online|offline N] */
static b32 tune_cpu(string_address address_to arguments, positive count)
{
        if (count == 2)
        {
                p8 smt[24];
                p8 boost[8];
                positive online = 0;

                boost[0] = smt[0] = end;
                if (tune_exists(TUNE_SYS_CPU "/cpufreq/boost"))
                        (void)tune_word(TUNE_SYS_CPU "/cpufreq/boost", boost, sizeof(boost));
                else if (tune_exists(TUNE_SYS_CPU "/intel_pstate/no_turbo"))
                {
                        //      no_turbo is the other way about, and a read that gave nothing says nothing.
                        if (tune_word(TUNE_SYS_CPU "/intel_pstate/no_turbo", boost, sizeof(boost)))
                                boost[0] = boost[0] == '0' ? '1' : '0';
                }
                (void)tune_word(TUNE_SYS_CPU "/smt/control", smt, sizeof(smt));
                host_each_entry(TUNE_SYS_CPU, tune_online_visit, address_of online);
                string_format(log, host_label "cpu: %p online", online);
                if (boost[0])
                        string_format(log, "; boost %s", boost[0] == '1' ? "on" : "off");
                if (smt[0])
                        string_format(log, "; smt %s", (string_address)smt);
                host_say(log, "\n");
                return 0;
        }
        if (count == 4 && (string_equals(arguments[2], "boost") || string_equals(arguments[2], "smt")) &&
            host_onoff(arguments[3]) >= 0)
        {
                bool on = host_onoff(arguments[3]);
                bool boost = string_equals(arguments[2], "boost");
                bipolar failed;

                host_need_root();
                failed = boost ? tune_cpu_boost(on) : tune_write(TUNE_SYS_CPU "/smt/control", ((bool)(on)) ? "on" : "off");
                if (failed == -ENOENT)
                        return host_refuse(boost ? "this machine has no boost switch\n"
                                                 : "this machine has no SMT switch\n");
                return_if(failed < 0, host_fail(boost ? "cpu boost" : "cpu smt", failed));
                //      Both default to on: nothing to bring back then.
                if (tune_remember(boost ? "cpu.boost" : "cpu.smt", on ? "" : "off"))
                        return 1;
                host_say(log, host_label "cpu %s %s\n", arguments[2], arguments[3]);
                return 0;
        }
        if (count == 4 && (string_equals(arguments[2], "online") || string_equals(arguments[2], "offline")) &&
            byte_is_digit(arguments[3][0]))
        {
                p8 path[160];
                positive number;
                positive used;
                p8 text[8];

                number = string_digits_max(arguments[3], 5, address_of used);
                if (arguments[3][used] && !byte_is_digit(arguments[3][used]))
                        return host_usage();
                if (arguments[3][used])
                        return host_refuse("a cpu is a number of at most five digits\n");
                if (!number)
                        return host_refuse("cpu 0 stays\n");
                host_need_root();
                bipolar failed;

                positive_into_string(text, number);
                failed = string_join(path, sizeof(path), TUNE_SYS_CPU "/cpu", (string_address)text,
                                     "/online")
                             ? tune_write(path, string_equals(arguments[2], "online") ? "1" : "0")
                             : -ENOENT;
                if (failed == -ENOENT)
                        return host_refuse("that cpu cannot be switched\n");
                return_if(failed < 0, host_fail("cpu", failed));
                host_say(log, host_label "cpu %s %p\n", arguments[2], number);
                return 0;
        }
        return host_usage();
}

/*
        A machine asleep in s2idle wakes only for what is armed to wake it, and
        the kernel leaves a keyboard's wakeup disabled: nothing but the power
        button then brings it back, which from the chair is a machine that
        never woke. PS/2 ports and USB keyboards (an interface of class 03
        speaking the boot keyboard protocol, 01) are armed before sleeping and
        left armed. Not the whole HID class: a handheld's own controller is
        HID too, presents itself as keyboard and mouse, and woke the machine
        a few seconds after it slept. What is armed is said, and after the
        wake the kernel's own wakeup sources are asked which of them fired.
*/
static bool tune_usb_keyboard(string_address bus, string_address name)
{
        for (positive interface = 0; interface < 4; interface++)
        {
                p8 path[192];
                p8 word[16];
                p8 tail[40];
                p8 number[4] = {(p8)('0' + interface), 0, 0, 0};

                if (!string_join(tail, sizeof(tail), "/", name, ":1.") ||
                    string_append_bounded(tail, (string_address)number, sizeof(tail)) >= sizeof(tail) ||
                    !string_join(path, sizeof(path), bus, (string_address)tail,
                                 "/bInterfaceClass") ||
                    !tune_word(path, word, sizeof(word)) || !string_equals((string_address)word, "03") ||
                    !string_join(path, sizeof(path), bus, (string_address)tail,
                                 "/bInterfaceProtocol") ||
                    !tune_word(path, word, sizeof(word)) || !string_equals((string_address)word, "01"))
                        continue;
                return true;
        }
        return false;
}

/* What is armed to wake the machine: a PS/2 port, or a USB device with a boot keyboard on it. */
static bool tune_wake_visit(string_address directory, string_address name, address_any context)
{
        p8 path[192];
        p8 word[16];

        //      An interface's own name, 1-2:1.0, has no wakeup file.
        if (*(bool address_to)context && (string_first_of(name, ':') || !tune_usb_keyboard(directory, name)))
                return true;
        if (!string_join(path, sizeof(path), directory, "/", name) ||
            string_append_bounded(path, "/power/wakeup", sizeof(path)) >= sizeof(path))
                return true;
        if (tune_word(path, word, sizeof(word)) && string_equals((string_address)word, "disabled") &&
            tune_write(path, "enabled") >= 0)
                host_say(log, host_label "wakes on %s\n", name);
        return true;
}

static fn tune_wake_arm(string_address bus, bool usb)
{
        host_each_entry(bus, tune_wake_visit, address_of usb);
}

#define TUNE_WAKE_MOST 128
#define TUNE_WAKE_CLASS "/sys/class/wakeup"

typedef struct
{
        p8 name[48];
        positive count;
} tune_source;

typedef struct
{
        tune_source source[TUNE_WAKE_MOST];
        positive kept;
        positive seen;
} tune_sources;

static bool tune_source_visit(string_address directory, string_address name, address_any context)
{
        tune_sources address_to sources = (tune_sources address_to)context;
        tune_source address_to source = sources->source + sources->kept;
        p8 path[160];

        sources->seen++;
        if (sources->kept == TUNE_WAKE_MOST || string_length(name) >= sizeof(source->name))
                return true;
        string_copy(source->name, name);
        if (!string_join(path, sizeof(path), directory, "/", name, "/event_count") ||
            !tune_number(path, address_of source->count))
                source->count = 0;
        sources->kept++;
        return true;
}

/* The event count of every wakeup source there is, by name. */
static fn tune_wake_counts(tune_sources address_to sources)
{
        sources->kept = sources->seen = 0;
        host_each_entry(TUNE_WAKE_CLASS, tune_source_visit, sources);
}

#define TUNE_LOG_LINES 60
#define TUNE_LOG_WIDTH 160
#define TUNE_LOG_ROOM (TUNE_LOG_LINES * TUNE_LOG_WIDTH + 512)

/* The wakeup sources that fired since the counts were taken, said and put first in text for /root/sleep.log. A source is the one of its name: one that came or went while the machine slept is not matched with whatever stood where it did in the directory. */
static fn tune_wake_report(tune_sources address_to before, p8 address_to text)
{
        tune_sources after;
        bool any = false;

        tune_wake_counts(address_of after);
        string_copy_bounded(text, "woken by:", TUNE_LOG_ROOM);
        for (positive at = 0; at < after.kept; at++)
        {
                p8 path[160];
                p8 label[48];
                positive was = 0;

                for (positive then = 0; then < before->kept; then++)
                        if (string_equals(before->source[then].name, after.source[at].name))
                                was = before->source[then].count;
                if (after.source[at].count == was)
                        continue;
                if (!string_join(path, sizeof(path), TUNE_WAKE_CLASS, "/", after.source[at].name,
                                 "/name") ||
                    !tune_word(path, label, sizeof(label)))
                        string_copy_bounded(label, after.source[at].name, sizeof(label));
                string_append_bounded(text, " ", TUNE_LOG_ROOM);
                string_append_bounded(text, (string_address)label, TUNE_LOG_ROOM);
                any = true;
        }
        if (!any)
                string_append_bounded(text, " nothing the kernel counted", TUNE_LOG_ROOM);
        if (after.seen > after.kept || before->seen > before->kept)
                string_append_bounded(text, " (some sources past the room were not counted)", TUNE_LOG_ROOM);
        host_say(log, host_label "%s\n", (string_address)text);
        string_append_bounded(text, "\n", TUNE_LOG_ROOM);
}

/*
        What the kernel says went wrong, when it says anything: the device, the
        step and the error. Those are the last failure's, and a write the kernel
        refuses before any device is reached (a state it does not take) leaves
        them as they were, so they are said only when this write counted a
        failure: the fail count is the one asked before the write.
*/
static fn tune_suspend_failure(positive failures)
{
        p8 device[64];
        p8 step[32];
        p8 errno_text[24];
        positive counted = 0;

        if (!tune_number(TUNE_SYS_POWER "/suspend_stats/fail", address_of counted) || counted == failures)
                return;
        if (!tune_word(TUNE_SYS_POWER "/suspend_stats/last_failed_dev", device, sizeof(device)) || !device[0])
                return;
        if (!tune_word(TUNE_SYS_POWER "/suspend_stats/last_failed_step", step, sizeof(step)))
                step[0] = 0;
        if (!tune_word(TUNE_SYS_POWER "/suspend_stats/last_failed_errno", errno_text, sizeof(errno_text)) || !errno_text[0])
                string_copy(errno_text, "0");
        host_say(log, host_label "the kernel stopped at %s (%s), last error %s\n",
                 (string_address)device, (string_address)step, (string_address)errno_text);
}

/*
        The last kernel lines about the card, the suspend and Canvas, put after
        the wake in /root/sleep.log: the next report from a machine whose
        screen did not come back names the step that failed without anyone
        having to run dmesg on a machine that shows nothing.
*/
static fn tune_kernel_lines(p8 address_to text)
{
        static const string_address wanted[] = {
            (string_address) "moonwater canvas", (string_address) "PM:", (string_address) "amdgpu",
            (string_address) "drm", (string_address) "backlight"};
        p8 record[2048];
        p8 ring[TUNE_LOG_LINES][TUNE_LOG_WIDTH];
        positive count = 0;
        bipolar handle = system_open_at(AT_FDCWD, "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);

        if (handle < 0)
                return;
        for (;;)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                string_address line;
                bool keep = false;

                // Interrupted, or a record overwritten before it was read.
                if (got == -EINTR || got == -EPIPE)
                        continue;
                if (got <= 0)
                        break;
                record[got] = end;
                line = string_find(record, (string_address) ";");
                if (!line)
                        continue;
                line++;
                ((p8 address_to)line)[string_first_of_or_end(line, '\n') - line] = end;
                for (positive at = 0; at < sizeof(wanted) / sizeof(wanted[0]); at++)
                        keep |= string_find(line, wanted[at]) != null;
                if (keep)
                        string_copy_bounded(ring[count++ % TUNE_LOG_LINES], line, TUNE_LOG_WIDTH);
        }
        system_close((positive)handle);
        string_append_bounded(text, "kernel log lines:\n", TUNE_LOG_ROOM);
        for (positive at = count > TUNE_LOG_LINES ? count - TUNE_LOG_LINES : 0; at < count; at++)
        {
                string_append_bounded(text, (string_address)ring[at % TUNE_LOG_LINES], TUNE_LOG_ROOM);
                string_append_bounded(text, "\n", TUNE_LOG_ROOM);
        }
}

/*
        How the sleep went as the kernel counts it: whether the platform ever
        reached its deepest state (last_hw_sleep is microseconds in it, and a
        fan that spins through a sleep is usually a nought there) and how many
        suspends failed.
*/
static fn tune_sleep_stats(p8 address_to text)
{
        static const string_address names[] = {
            (string_address) "success", (string_address) "fail", (string_address) "last_hw_sleep",
            (string_address) "total_hw_sleep", (string_address) "max_hw_sleep"};

        string_append_bounded(text, "suspend_stats:", TUNE_LOG_ROOM);
        for (positive at = 0; at < sizeof(names) / sizeof(names[0]); at++)
        {
                p8 path[96];
                p8 number[24];
                positive value = 0;

                if (!string_join(path, sizeof(path), TUNE_SYS_POWER "/suspend_stats/", names[at]) ||
                    !tune_number(path, address_of value))
                        continue;
                positive_into_string(number, value);
                string_append_bounded(text, " ", TUNE_LOG_ROOM);
                string_append_bounded(text, names[at], TUNE_LOG_ROOM);
                string_append_bounded(text, "=", TUNE_LOG_ROOM);
                string_append_bounded(text, (string_address)number, TUNE_LOG_ROOM);
        }
        string_append_bounded(text, "\n", TUNE_LOG_ROOM);
}

#define TUNE_BACKLIGHTS 4
#define TUNE_NO_LEVEL ((positive)-1)

typedef struct
{
        p8 name[64];
        positive level;
} tune_light;

typedef struct
{
        tune_light light[TUNE_BACKLIGHTS];
        positive kept;
} tune_lights;

static bool tune_light_visit(string_address directory, string_address name, address_any context)
{
        tune_lights address_to lights = (tune_lights address_to)context;
        tune_light address_to light = lights->light + lights->kept;
        p8 path[160];

        if (lights->kept == TUNE_BACKLIGHTS || string_length(name) >= sizeof(light->name))
                return true;
        string_copy(light->name, name);
        if (!string_join(path, sizeof(path), directory, "/", name, "/brightness") ||
            !tune_number(path, address_of light->level))
                light->level = TUNE_NO_LEVEL;
        lights->kept++;
        return true;
}

/* Each backlight's level, by name, before the machine sleeps. */
static fn tune_backlight_save(tune_lights address_to lights)
{
        lights->kept = 0;
        host_each_entry(TUNE_SYS_BACKLIGHT, tune_light_visit, lights);
}

/*
        And put back after it. The panel's own driver keeps its level across a
        suspend on the machines where that works, and a wake that left the
        backlight off or at nought (bl_power still blanked, or a level the
        firmware picked) is a screen that is on and dark. The level is written
        back whatever the file says: it is what the class device last stored,
        and after a wake the panel may not be doing it. Each by its name, so a
        panel that came or went while the machine slept is not given another's.
*/
static fn tune_backlight_restore(tune_lights address_to lights)
{
        for (positive at = 0; at < lights->kept; at++)
        {
                string_address name = (string_address)lights->light[at].name;
                p8 path[160];
                positive blank = 0;

                if (string_join(path, sizeof(path), TUNE_SYS_BACKLIGHT, "/", name, "/bl_power") &&
                    tune_number(path, address_of blank) && blank &&
                    tune_write(path, "0") >= 0)
                        host_say(log, host_label "backlight %s was blanked, unblanked\n", name);
                if (lights->light[at].level == TUNE_NO_LEVEL ||
                    !string_join(path, sizeof(path), TUNE_SYS_BACKLIGHT, "/", name, "/brightness"))
                        continue;
                //      Written even when the file already says so: it is the value
                //      the class device last stored, not what the panel is doing.
                if (tune_write_number(path, lights->light[at].level) >= 0)
                        host_say(log, host_label "backlight %s set to %p\n", name, lights->light[at].level);
        }
}

/* A kernel switch turned on for the length of a sleep, and what it was. */
typedef struct
{
        string_address path;
        p8 was[8];
        bool changed;
} tune_flag;

static fn tune_flag_on(tune_flag address_to flag, string_address path)
{
        flag->path = path;
        flag->changed = tune_word(path, flag->was, sizeof(flag->was)) &&
                        !string_equals((string_address)flag->was, "1") &&
                        tune_write(path, "1") >= 0;
}

static fn tune_flag_back(tune_flag address_to flag)
{
        bipolar failed;

        if (flag->changed && (failed = tune_write(flag->path, (string_address)flag->was)) < 0)
                host_say(log_error, host_label "%s could not be put back to %s: %s\n", flag->path,
                         (string_address)flag->was, file_reason(failed));
}

/* What a sleep leaves in /root/sleep.log, awake or not, and the switches it
   turned on are put back as they were. */
static fn tune_suspend_record(p8 address_to text, tune_flag address_to messages,
                              tune_flag address_to times)
{
        tune_sleep_stats(text);
        tune_kernel_lines(text);
        (void)host_write_file("/root/sleep.log", text, string_length((string_address)text), 0644, true);
        tune_flag_back(messages);
        tune_flag_back(times);
}

static bool desktop_woke(void); // desktop.c: the desktop's compositor told the machine woke

/* moonwater sleep and moonwater hibernate: the kernel's own suspend and hibernate. */
static b32 tune_suspend(string_address verb, string_address state)
{
        tune_sources sources;
        tune_lights lights;
        tune_flag messages;
        tune_flag times;
        p8 text[TUNE_LOG_ROOM];

        if (!tune_lists(TUNE_SYS_POWER "/state", state))
                return host_refuse(string_equals(state, "mem")
                                       ? "this kernel does not offer sleep\n"
                                       : "this kernel does not offer hibernate\n");
        host_need_root();
        //      So a sleep that never wakes leaves the device it stopped at in the
        //      log, and what they were is put back when it is over.
        tune_flag_on(address_of messages, TUNE_SYS_POWER "/pm_debug_messages");
        tune_flag_on(address_of times, TUNE_SYS_POWER "/pm_print_times");
        tune_wake_arm("/sys/bus/usb/devices", true);
        tune_wake_arm("/sys/bus/serio/devices", false);
        if (string_equals(state, "mem"))
        {
                p8 mode[64];

                //      Which kind of sleep, as the kernel spells it: [s2idle] deep.
                if (tune_word(TUNE_SYS_POWER "/mem_sleep", mode, sizeof(mode)))
                        host_say(log, host_label "sleeping, mem_sleep %s\n", (string_address)mode);
        }
        tune_wake_counts(address_of sources);
        tune_backlight_save(address_of lights);
        system_call(syscall(sync));
        {
                positive failures = 0;
                bipolar failed;

                (void)tune_number(TUNE_SYS_POWER "/suspend_stats/fail", address_of failures);
                failed = tune_write(TUNE_SYS_POWER "/state", state);
                if (failed < 0)
                {
                        tune_suspend_failure(failures);
                        text[0] = end;
                        tune_suspend_record(text, address_of messages, address_of times);
                        return host_fail(verb, failed);
                }
        }
        //      Reached again once the machine has woken.
        tune_wake_report(address_of sources, text);
        host_say(log, host_label "awake again\n");
        //      What a sleep can leave behind: the panel's level, the link (the
        //      watcher is asked to look again, since a carrier that came back
        //      before it was listening is news it never heard), and the
        //      settings the machine put in place at boot.
        tune_backlight_restore(address_of lights);
        //      The desktop's compositor is told at once, and takes the screens
        //      again (every connector asked, every crtc set anew), where it
        //      would otherwise wait for a key or its clocks to say so.
        if (desktop_woke())
                host_say(log, host_label "the desktop is told it woke\n");
        radio_net_wake();
        tune_restore();
        tune_suspend_record(text, address_of messages, address_of times);
        return 0;
}

/* What was kept, put back at boot. */
static fn tune_restore(void)
{
        p8 text[512];
        p8 value[24];
        bipolar got = tune_load(text, sizeof(text));

        if (got <= 0)
                return;
        if (tune_find(text, (positive)got, "power", value, sizeof(value)))
                (void)tune_power_apply((string_address)value);
        if (tune_find(text, (positive)got, "cpu.boost", value, sizeof(value)) &&
            string_equals((string_address)value, "off"))
                (void)tune_cpu_boost(false);
        if (tune_find(text, (positive)got, "cpu.smt", value, sizeof(value)) &&
            string_equals((string_address)value, "off"))
                (void)tune_write(TUNE_SYS_CPU "/smt/control", false ? "on" : "off");
        if (tune_find(text, (positive)got, "charge.limit", value, sizeof(value)))
                (void)tune_charge_apply((string_address)value);
        //      Airplane mode is every radio off, and the modem has no word of its own.
        if (tune_find(text, (positive)got, "airplane", value, sizeof(value)) &&
            radio_kept(NET_WIFI_POWER) == 0 && radio_kept(NET_BLUETOOTH_POWER) == 0)
                (void)radio_rfkill(0, true);
}

/* moonwater battery [NAME|auto|none]: the battery the settings window's Battery row reads. auto (the default) is the
   first present battery; none is no battery; NAME is a battery or UPS the kernel names under power_supply. It is kept
   in /root/tune like the charge limit, with auto kept as nothing. */
static b32 tune_battery_verb(string_address address_to arguments, positive count)
{
        p8 value[72];
        string_address name;
        string_address stored;

        if (count == 2)
        {
                if (tune_kept("battery", value, sizeof(value)) && value[0])
                        host_say(log, host_label "battery %s\n", (string_address)value);
                else
                        host_say(log, host_label "battery auto\n");
                return 0;
        }
        if (count != 3)
                return host_usage();

        name = arguments[2];
        if (string_equals(name, (string_address) "auto"))
                stored = (string_address) "";
        else if (string_equals(name, (string_address) "none"))
                stored = (string_address) "none";
        else
        {
                positive at = 0;

                // A name is one directory entry: no slash, no dot-name, and a battery or a UPS under the kernel's power_supply.
                while (name[at] && name[at] != '/' && at < 64)
                        at++;
                if (!at || name[0] == '.' || name[at] || at >= 64 ||
                    !(tune_says((string_address)TUNE_SYS_BATTERY, name, (string_address) "/type", (string_address) "Battery") ||
                      tune_says((string_address)TUNE_SYS_BATTERY, name, (string_address) "/type", (string_address) "UPS")))
                        return host_refuse("this machine has no battery called %s\n", name);
                stored = name;
        }
        host_need_root();
        if (tune_remember("battery", stored))
                return 1;
        host_say(log, host_label "battery %s\n", name);
        return 0;
}

/* sleep and hibernate take no words, and differ in what they ask the kernel for. */
static b32 tune_sleep_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? tune_suspend(arguments[1], "mem") : host_usage();
}

static b32 tune_hibernate_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? tune_suspend(arguments[1], "disk") : host_usage();
}

