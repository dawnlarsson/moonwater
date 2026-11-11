/*
        The machine's batteries, as the kernel's power supply class shows them
        (Documentation/ABI/testing/sysfs-class-power): each entry of
        /sys/class/power_supply whose type is Battery, by its directory name,
        with whether it is present, its charge in percent and whether it is
        charging.

        A battery is read the way btop reads one (src/linux/btop_collect.cpp,
        get_battery): it is present when its present file reads 1, its
        percent is capacity, else energy_now over energy_full, else charge_now
        over charge_full, clamped to 0 to 100, and its status is the word the
        kernel gives. One deliberate difference: an entry whose scope is Device
        (a mouse's or a keyboard's battery, reported here too) is not one the
        machine has, so it is left out.

        Nothing is allocated. A list is filled into the caller's names and a
        state into the caller's struct, and a file that is missing or unreadable
        is an answer (not present, no percent), never a failure of the caller.
*/
#ifndef MOONWATER_CANVAS_UI_BATTERY
#define MOONWATER_CANVAS_UI_BATTERY

#define BATTERY_ROOT "/sys/class/power_supply"
#define BATTERY_NAME 32    // a directory name, its null included
#define BATTERY_LIST 16    // names a caller's list holds at most
#define BATTERY_PATH 192
#define BATTERY_DIGITS 15  // the most digits a reading takes

enum battery_charging
{
        BATTERY_UNKNOWN,
        BATTERY_DISCHARGING,
        BATTERY_CHARGING,
        BATTERY_FULL,
        BATTERY_NOT_CHARGING
};

struct battery_state
{
        b32 present;             // the battery's present file reads 1
        int percent;             // 0 to 100, or -1 when no reading gives one
        unsigned int charging;   // enum battery_charging
        char name[BATTERY_NAME]; // the directory name it was read under
};

// What a power supply entry is: a battery, a UPS (which carries a battery and reports its capacity), or neither.
enum battery_kind
{
        BATTERY_KIND_NONE,
        BATTERY_KIND_BATTERY,
        BATTERY_KIND_UPS
};

struct battery_name
{
        char text[BATTERY_NAME];
        unsigned int kind; // enum battery_kind
};

// The directory the batteries are read under. A check points it at a tree of its own.
static string_address battery_root = (string_address)BATTERY_ROOT;

// A word of a battery's file, without its newline or the blanks after it: its length, or -1 when the file is not there.
static bipolar battery_word(string_address name, string_address leaf, p8 address_to into, positive room)
{
        p8 path[BATTERY_PATH];
        bipolar got;

        into[0] = end;
        if (!string_join(path, sizeof(path), battery_root, "/", name, leaf))
                return -1;
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, into, room);
        if (got < 0)
                return -1;
        while (got && (into[got - 1] == '\n' || into[got - 1] == ' ' || into[got - 1] == '\t'))
                into[--got] = end;
        return got;
}

// One reading of a battery's file: a number of digits and no more, and false when it is not one.
static b32 battery_number(string_address name, string_address leaf, positive address_to value)
{
        p8 text[32];
        positive number = 0, used = 0;
        bipolar got = battery_word(name, leaf, text, sizeof(text));

        if (got <= 0 || got > BATTERY_DIGITS)
                return false;
        number = string_digits_max((string_address)text, (positive)got, &used);
        if (used != (positive)got)
                return false;
        *value = number;
        return true;
}

struct battery_word
{
        string_address text; // a word the file may read as; a zero text ends the table
        unsigned int value;  // what the word means; the ending entry's value is the answer when none of them is
};

/*
        What a directory entry is, by its type alone. A scope of Device is not
        a reason to leave it out: a USB or Bluetooth mouse, keyboard or
        controller reports its battery here with that scope, and it is a
        battery the machine has all the same.
*/
static const struct battery_word battery_kind_words[] = {
        {"Battery", BATTERY_KIND_BATTERY}, {"UPS", BATTERY_KIND_UPS}, {0, BATTERY_KIND_NONE}};
static const struct battery_word battery_status_words[] = {
        {"Discharging", BATTERY_DISCHARGING}, {"Charging", BATTERY_CHARGING}, {"Full", BATTERY_FULL},
        {"Not charging", BATTERY_NOT_CHARGING}, {0, BATTERY_UNKNOWN}};

// The value of the table's word that the battery's file reads as: the ending
// entry's when the file is not there or reads as none of the words.
static unsigned int battery_word_value(string_address name, string_address leaf, positive room, const struct battery_word address_to table)
{
        p8 text[32];
        positive at = 0;
        b32 found = battery_word(name, leaf, text, room) >= 0;

        while (table[at].text && !(found && string_equals((string_address)text, table[at].text)))
                at++;
        return table[at].value;
}

static b32 battery_is_present(string_address name)
{
        p8 text[8];

        return battery_word(name, "/present", text, sizeof(text)) >= 0 && string_equals((string_address)text, "1");
}

// The percent a battery reads at: capacity, else energy, else charge, and -1 when none of them gives one.
static int battery_percent(string_address name)
{
        positive now, full;

        if (battery_number(name, "/capacity", &now))
                return now > 100 ? 100 : (int)now;
        if ((battery_number(name, "/energy_now", &now) && battery_number(name, "/energy_full", &full)) ||
            (battery_number(name, "/charge_now", &now) && battery_number(name, "/charge_full", &full)))
        {
                if (!full)
                        return -1;
                positive percent = (now * 100 + full / 2) / full;

                return percent > 100 ? 100 : (int)percent;
        }
        return -1;
}

// A battery's state, under its name: a battery that is not present has no percent and no charging state.
static void battery_fill(string_address name, struct battery_state address_to state)
{
        state->present = false;
        state->percent = -1;
        state->charging = BATTERY_UNKNOWN;
        string_copy_bounded((string_address)state->name, name, BATTERY_NAME);
        if (!battery_is_present(name))
                return;
        state->present = true;
        state->percent = battery_percent(name);
        state->charging = battery_word_value(name, "/status", 32, battery_status_words);
}

/*
        Every battery and UPS the machine has, in the order the directory
        gives them, into names (room of them at most), each with its kind. A
        name is listed once among those kept: a repeat past room is not seen,
        and is counted again, which no caller can tell, since both clamp the
        answer to room. The answer is how many there are, which can be more
        than room; the names are not sorted. Nothing is allocated: the block
        of directory records is on the stack.
*/
static positive battery_list(struct battery_name address_to names, positive room)
{
        p64 block[256];   // the getdents records, 2 KiB on the stack
        p8 address_to bytes = (p8 address_to)block;
        bipolar handle = system_open_at(AT_FDCWD, battery_root, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        positive count = 0;

        if (handle < 0)
                return 0;
        for (;;)
        {
                bipolar got = system_read_directory(handle, block, sizeof(block));

                if (got <= 0)
                        break;
                for (bipolar at = 0; at < got;)
                {
                        struct linux_dirent64 address_to entry = (struct linux_dirent64 address_to)(bytes + at);
                        string_address name = (string_address)entry->d_name;

                        //      A record of no length would stop the walk where it is: the block is left then.
                        unsigned int kind;
                        b32 listed = false;

                        at += entry->d_reclen > 0 ? entry->d_reclen : got;
                        if (name[0] == '.' || string_length(name) >= BATTERY_NAME)
                                continue;
                        kind = battery_word_value(name, "/type", 16, battery_kind_words);
                        if (kind == BATTERY_KIND_NONE)
                                continue;
                        for (positive have = 0; have < count && have < room; have++)
                                listed |= string_equals((string_address)names[have].text, name);
                        if (listed)
                                continue;
                        if (count < room)
                        {
                                string_copy_bounded((string_address)names[count].text, name, BATTERY_NAME);
                                names[count].kind = kind;
                        }
                        count++;
                }
        }
        system_close((positive)handle);
        return count;
}

/*
        The state of one battery: the one named, or with no name (NULL or
        empty) the first battery that is present, or else the first there is.
        Answers whether a battery of that name is one the machine has. A name
        that is not a plain directory name of a battery is not one, and gives
        a state that is not present.
*/
static b32 battery_read(string_address name, struct battery_state address_to state)
{
        struct battery_name names[BATTERY_LIST];
        positive count, chosen = 0;
        b32 found = false;

        if (name && name[0])
        {
                // A name is one directory entry: no slash and no dot-name, so it reads nothing outside the root.
                b32 plain = string_length(name) < BATTERY_NAME && name[0] != '.';

                for (positive at = 0; name[at]; at++)
                        plain &= name[at] != '/';
                if (!plain || battery_word_value(name, "/type", 16, battery_kind_words) == BATTERY_KIND_NONE)
                {
                        state->present = false;
                        state->percent = -1;
                        state->charging = BATTERY_UNKNOWN;
                        string_copy_bounded((string_address)state->name, plain ? name : (string_address) "", BATTERY_NAME);
                        return false;
                }
                battery_fill(name, state);
                return true;
        }

        count = battery_list(names, BATTERY_LIST);
        if (count > BATTERY_LIST)
                count = BATTERY_LIST;
        if (!count)
        {
                state->present = false;
                state->percent = -1;
                state->charging = BATTERY_UNKNOWN;
                state->name[0] = end;
                return false;
        }
        //      The default is the first present battery of type Battery; a present UPS is taken only when there is none.
        for (positive pass = 0; pass < 2 && !found; pass++)
                for (positive at = 0; at < count && !found; at++)
                        if ((pass || names[at].kind == BATTERY_KIND_BATTERY) && battery_is_present(names[at].text))
                        {
                                chosen = at;
                                found = true;
                        }
        battery_fill((string_address)names[chosen].text, state);
        return true;
}

#endif
