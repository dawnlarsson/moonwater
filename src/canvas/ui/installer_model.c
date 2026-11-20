/*
        The installer's model: what the installer window and `moonwater install` both read, and nothing that draws.

        The drives are read from a sysfs root (/sys on a machine, a tree a check makes elsewhere): ROOT/block/NAME, its size in
        512-byte sectors, its removable flag, its device/vendor and device/model, and each partition under it (a directory that
        has a `partition` file), whose uevent's PARTNAME is the GPT name the install gave it. A drive holds Moonwater when it has
        both of the names an install writes, moonwater-boot and moonwater-data (host.c's HOST_SYSTEM_NAME and HOST_DATA_NAME, the
        census's own test). Loop, ram, zram, optical and floppy devices are never drives to install on, and the drive the live
        system booted from is excluded by its name (installer_excluded), which `moonwater install` finds and says.

        `moonwater install list` writes a drive a line, its fields a tab apart: name, bytes, removable (0 or 1), Moonwater (0 or
        1), boot (0 or 1), and the model to the end of the line. The window reads that back (installer_parse), so the listing it
        shows is the verb's own, boot drive and all.

        The plan (installer_plan) is what the window runs once the person has said yes, a command a line, its words a blank
        apart (no word has a blank in it): the install, then the language and the keyboard where the person changed them, then
        each profile chosen. An install keeps the target's data and settings mounted for the rest of the live session (host_take),
        so the commands after it write the target's /root and its bowls, not the stick's.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_INSTALLER_MODEL
#define MOONWATER_INSTALLER_MODEL

#define INSTALLER_DRIVES 16
#define INSTALLER_NAME 32
#define INSTALLER_MODEL 64
#define INSTALLER_PLAN 1024
#define INSTALLER_BOOT_PART "moonwater-boot"
#define INSTALLER_DATA_PART "moonwater-data"

struct installer_drive
{
        char name[INSTALLER_NAME];
        unsigned long long bytes;
        b32 removable, moonwater, boot;
        char model[INSTALLER_MODEL];
};

// The profiles the installer offers, as bits.
#define INSTALLER_CHROME 1u
#define INSTALLER_STEAM 2u
#define INSTALLER_SSH 4u

#define INSTALLER_OVERWRITE 0
#define INSTALLER_UPDATE 1
#define INSTALLER_USE 2 // the drive's data used with this build, and nothing written (setup use)

// What the person chose: the drive, the mode, and the settings and profiles.
struct installer_choice
{
        char drive[INSTALLER_NAME];
        int mode;                 // INSTALLER_OVERWRITE or INSTALLER_UPDATE
        char language[8];         // a code of SETTINGS_LANGUAGE_CODES
        char keyboard[8];         // a layout of the Keyboard row
        b32 language_changed, keyboard_changed;
        unsigned int profiles;    // INSTALLER_* bits
        b32 online;               // the profiles need the network: without it none is run
};

// Whether a block device is never one to install on: loop, ram, zram, optical and floppy devices, and an empty name.
static b32 installer_skipped(const char address_to name)
{
        static const char address_to const prefixes[] = {"loop", "ram", "zram", "sr", "fd", "nbd"};

        if (!name || !name[0] || name[0] == '.')
                return true;
        for (positive at = 0; at < sizeof(prefixes) / sizeof(prefixes[0]); at++)
        {
                positive length = string_length((string_address)prefixes[at]);

                if (!memory_compare(name, prefixes[at], length))
                        return true;
        }
        return false;
}

// Whether a drive is left out of the targets: skipped, or the one the live system booted from.
static b32 installer_excluded(const char address_to name, const char address_to boot)
{
        return installer_skipped(name) || (boot && boot[0] && string_equals((string_address)name, (string_address)boot));
}

// A small file of the tree read as one line: its end and its trailing blanks taken off. False when it cannot be read.
static b32 installer_read_line(const char address_to root, const char address_to a, const char address_to b, const char address_to c,
                               char address_to into, positive room)
{
        char path[256];
        bipolar got;
        positive length;

        string_copy_bounded((string_address)path, (string_address)root, sizeof(path));
        string_append_bounded((string_address)path, (string_address)a, sizeof(path));
        if (b)
                string_append_bounded((string_address)path, (string_address)b, sizeof(path));
        if (c)
                string_append_bounded((string_address)path, (string_address)c, sizeof(path));
        into[0] = 0;
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)into, room - 1);
        if (got < 0)
                return false;
        length = (positive)got < room ? (positive)got : room - 1;
        into[length] = 0;
        while (length && (into[length - 1] == '\n' || into[length - 1] == ' ' || into[length - 1] == '\t'))
                into[--length] = 0;
        // A model's line is one line, with no tabs: the listing's fields are a tab apart.
        for (positive at = 0; at < length; at++)
                if (into[at] == '\t' || into[at] == '\n' || (p8)into[at] < 0x20)
                        into[at] = ' ';
        return true;
}

// The PARTNAME of a partition's uevent, or nothing.
static fn installer_partname(const char address_to root, const char address_to disk, const char address_to part, char address_to into, positive room)
{
        char path[256], text[1024];
        bipolar got;

        into[0] = 0;
        string_copy_bounded((string_address)path, (string_address)root, sizeof(path));
        string_append_bounded((string_address)path, (string_address) "/block/", sizeof(path));
        string_append_bounded((string_address)path, (string_address)disk, sizeof(path));
        string_append_bounded((string_address)path, (string_address) "/", sizeof(path));
        string_append_bounded((string_address)path, (string_address)part, sizeof(path));
        string_append_bounded((string_address)path, (string_address) "/uevent", sizeof(path));
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)text, sizeof(text) - 1);
        if (got <= 0)
                return;
        text[got] = 0;
        for (char address_to line = text; *line;)
        {
                char address_to stop = line;

                while (*stop && *stop != '\n')
                        stop++;
                if (stop - line > 9 && !memory_compare(line, "PARTNAME=", 9))
                {
                        positive length = (positive)(stop - line - 9);

                        if (length >= room)
                                length = room - 1;
                        memory_copy(into, line + 9, length);
                        into[length] = 0;
                        return;
                }
                line = *stop ? stop + 1 : stop;
        }
}

// Whether a drive of the tree has both of an install's partitions: its subdirectories that are partitions, by their GPT names.
static b32 installer_has_moonwater(const char address_to root, const char address_to disk)
{
        char path[256], name[64], flag[8];
        file_walk walk;
        struct linux_dirent64 address_to entry;
        b32 boot = false, data = false;

        string_copy_bounded((string_address)path, (string_address)root, sizeof(path));
        string_append_bounded((string_address)path, (string_address) "/block/", sizeof(path));
        string_append_bounded((string_address)path, (string_address)disk, sizeof(path));
        if (!file_walk_open(address_of walk, AT_FDCWD, (string_address)path))
                return false;
        while ((entry = file_walk_next(address_of walk)))
        {
                char sub[128];

                if (entry->d_name[0] == '.' || string_length((string_address)entry->d_name) >= sizeof(sub) - 16)
                        continue;
                string_copy_bounded((string_address)sub, (string_address) "/block/", sizeof(sub));
                string_append_bounded((string_address)sub, (string_address)disk, sizeof(sub));
                string_append_bounded((string_address)sub, (string_address) "/", sizeof(sub));
                if (!installer_read_line(root, sub, entry->d_name, "/partition", flag, sizeof(flag)))
                        continue;
                installer_partname(root, disk, entry->d_name, name, sizeof(name));
                boot |= string_equals((string_address)name, (string_address)INSTALLER_BOOT_PART);
                data |= string_equals((string_address)name, (string_address)INSTALLER_DATA_PART);
        }
        file_walk_close(address_of walk);
        return boot && data;
}

/*
        The drives of a sysfs root, at most most, in the order the directory lists them: every block device that is not skipped,
        with the one named boot marked (the live system's own: listed, so the verb can say it, and never a target). Answers how many.
*/
static int installer_drives_read(const char address_to root, const char address_to boot, struct installer_drive address_to drives, int most)
{
        char path[256], text[64], vendor[INSTALLER_MODEL];
        file_walk walk;
        struct linux_dirent64 address_to entry;
        int count = 0;

        string_copy_bounded((string_address)path, (string_address)root, sizeof(path));
        string_append_bounded((string_address)path, (string_address) "/block", sizeof(path));
        if (!file_walk_open(address_of walk, AT_FDCWD, (string_address)path))
                return 0;
        while (count < most && (entry = file_walk_next(address_of walk)))
        {
                struct installer_drive address_to d = &drives[count];
                char sub[128];

                if (installer_skipped(entry->d_name) || string_length((string_address)entry->d_name) >= INSTALLER_NAME)
                        continue;
                memory_zero(d, sizeof(*d));
                string_copy_bounded((string_address)d->name, (string_address)entry->d_name, sizeof(d->name));
                string_copy_bounded((string_address)sub, (string_address) "/block/", sizeof(sub));
                string_append_bounded((string_address)sub, (string_address)entry->d_name, sizeof(sub));
                if (installer_read_line(root, sub, "/size", null, text, sizeof(text)))
                        d->bytes = (unsigned long long)string_to_number_unsigned((string_address)text, null, 10) * 512ull;
                d->removable = installer_read_line(root, sub, "/removable", null, text, sizeof(text)) && text[0] == '1';
                vendor[0] = 0;
                installer_read_line(root, sub, "/device/vendor", null, vendor, sizeof(vendor));
                installer_read_line(root, sub, "/device/model", null, d->model, sizeof(d->model));
                if (vendor[0] && !d->model[0])
                        string_copy_bounded((string_address)d->model, (string_address)vendor, sizeof(d->model));
                else if (vendor[0] && memory_compare(d->model, vendor, string_length((string_address)vendor)))
                {
                        char both[INSTALLER_MODEL];

                        string_copy_bounded((string_address)both, (string_address)vendor, sizeof(both));
                        string_append_bounded((string_address)both, (string_address) " ", sizeof(both));
                        string_append_bounded((string_address)both, (string_address)d->model, sizeof(both));
                        string_copy_bounded((string_address)d->model, (string_address)both, sizeof(d->model));
                }
                // An empty disk (a card reader with no card) is no drive.
                if (!d->bytes)
                        continue;
                d->moonwater = installer_has_moonwater(root, d->name);
                d->boot = boot && boot[0] && string_equals((string_address)d->name, (string_address)boot);
                count++;
        }
        file_walk_close(address_of walk);
        return count;
}

// A drive as the listing's line, with its end: name, bytes, removable, Moonwater, boot, model, a tab apart.
static positive installer_line(const struct installer_drive address_to d, char address_to into, positive room)
{
        char number[24];

        string_copy_bounded((string_address)into, (string_address)d->name, room);
        string_append_bounded((string_address)into, (string_address) "\t", room);
        number[positive_into_string((p8 address_to)number, (positive)d->bytes)] = 0;
        string_append_bounded((string_address)into, (string_address)number, room);
        string_append_bounded((string_address)into, (string_address)(d->removable ? "\t1" : "\t0"), room);
        string_append_bounded((string_address)into, (string_address)(d->moonwater ? "\t1" : "\t0"), room);
        string_append_bounded((string_address)into, (string_address)(d->boot ? "\t1\t" : "\t0\t"), room);
        string_append_bounded((string_address)into, (string_address)d->model, room);
        string_append_bounded((string_address)into, (string_address) "\n", room);
        return string_length((string_address)into);
}

// The next field of a line, to the tab or the line's end: answers where the one after it starts.
static const char address_to installer_field(const char address_to at, const char address_to stop, char address_to into, positive room, b32 last)
{
        positive length = 0;

        while (at < stop && (last || *at != '\t'))
        {
                if (length + 1 < room)
                        into[length++] = *at;
                at++;
        }
        into[length] = 0;
        return at < stop ? at + 1 : at;
}

/*
        The listing read back: a drive a line, its fields as installer_line writes them. A line that is not one (fewer fields, a name
        that is none, a size that is no number) is passed over. Answers how many drives, at most most.
*/
static int installer_parse(const char address_to text, positive length, struct installer_drive address_to drives, int most)
{
        const char address_to at = text, address_to limit = text + length;
        int count = 0;

        while (at < limit && count < most)
        {
                const char address_to stop = at, address_to next;
                char bytes[24], removable[4], moonwater[4], boot[4];
                struct installer_drive address_to d = &drives[count];
                b32 digits = true;

                while (stop < limit && *stop != '\n')
                        stop++;
                next = stop < limit ? stop + 1 : stop;
                memory_zero(d, sizeof(*d));
                at = installer_field(at, stop, d->name, sizeof(d->name), false);
                at = installer_field(at, stop, bytes, sizeof(bytes), false);
                at = installer_field(at, stop, removable, sizeof(removable), false);
                at = installer_field(at, stop, moonwater, sizeof(moonwater), false);
                at = installer_field(at, stop, boot, sizeof(boot), false);
                at = installer_field(at, stop, d->model, sizeof(d->model), true);
                for (positive k = 0; bytes[k]; k++)
                        digits &= bytes[k] >= '0' && bytes[k] <= '9';
                if (d->name[0] && !installer_skipped(d->name) && bytes[0] && digits && (removable[0] == '0' || removable[0] == '1') &&
                    (moonwater[0] == '0' || moonwater[0] == '1') && (boot[0] == '0' || boot[0] == '1'))
                {
                        d->bytes = (unsigned long long)string_to_number_unsigned((string_address)bytes, null, 10);
                        d->removable = removable[0] == '1', d->moonwater = moonwater[0] == '1', d->boot = boot[0] == '1';
                        count++;
                }
                at = next;
        }
        return count;
}

// The drives a person may choose: the listing less the boot drive, in place. Answers how many are left.
static int installer_targets(struct installer_drive address_to drives, int count)
{
        int kept = 0;

        for (int at = 0; at < count; at++)
                if (!drives[at].boot && !installer_skipped(drives[at].name))
                        drives[kept++] = drives[at];
        return kept;
}

// A size as a person reads it: whole GB under 1000, else TB with a tenth (decimal, as a drive's box says it).
static fn installer_size(unsigned long long bytes, char address_to into, positive room)
{
        char digits[24];
        unsigned long long gb = (bytes + 500000000ull) / 1000000000ull;

        if (gb < 1000)
        {
                digits[positive_into_string((p8 address_to)digits, (positive)gb)] = 0;
                string_copy_bounded((string_address)into, (string_address)digits, room);
                string_append_bounded((string_address)into, (string_address) " GB", room);
                return;
        }
        digits[positive_into_string((p8 address_to)digits, (positive)(gb / 1000))] = 0;
        string_copy_bounded((string_address)into, (string_address)digits, room);
        string_append_bounded((string_address)into, (string_address) ".", room);
        digits[positive_into_string((p8 address_to)digits, (positive)(gb % 1000 / 100))] = 0;
        string_append_bounded((string_address)into, (string_address)digits, room);
        string_append_bounded((string_address)into, (string_address) " TB", room);
}

// Whether the routing table (/proc/net/route's text) has a default route: a line whose destination is 00000000, on any interface.
static b32 installer_route_default(const char address_to text, positive length)
{
        const char address_to at = text, address_to limit = text + length;

        while (at < limit)
        {
                const char address_to stop = at;

                while (stop < limit && *stop != '\n')
                        stop++;
                // The interface, then the destination in hex.
                for (const char address_to c = at; c < stop; c++)
                        if (*c == '\t')
                        {
                                if (stop - c > 8 && !memory_compare(c + 1, "00000000", 8) && (c + 9 == stop || c[9] == '\t'))
                                        return true;
                                break;
                        }
                at = stop < limit ? stop + 1 : stop;
        }
        return false;
}

// The verb's word for a mode.
static const char address_to installer_mode_word(int mode)
{
        return mode == INSTALLER_UPDATE ? "update" : mode == INSTALLER_USE ? "use" : "overwrite";
}

/*
        The networks `moonwater wifi` says, as Settings' Wi-Fi list reads them (settings_feed_wifi): a heard network is a line of
        its marker (* joined, + saved, or two blanks), its name, its signal as -NN dBm, four bars of # and ., and what it asks
        for (open asks no passphrase); the verb's label before a line is passed over. A line "wifi off" says the radio is off.
        Answers how many networks, at most most; *on is false when the radio is off.
*/
#define INSTALLER_NETWORKS 16
struct installer_network
{
        char name[64];
        int bars;
        b32 joined, saved, open;
};

// A command of the plan, its end a newline.
static fn installer_plan_line(char address_to into, positive room, const char address_to a, const char address_to b, const char address_to c,
                              const char address_to d, const char address_to e)
{
        const char address_to words[5] = {a, b, c, d, e};

        for (int k = 0; k < 5 && words[k]; k++)
        {
                if (k)
                        string_append_bounded((string_address)into, (string_address) " ", room);
                string_append_bounded((string_address)into, (string_address)words[k], room);
        }
        string_append_bounded((string_address)into, (string_address) "\n", room);
}

/*
        What is run, a command a line: the install of the drive in its mode (yes: the window has asked, and the verb's input is no
        terminal), then the language and the keyboard where they were changed in the flow (an update keeps the install's own
        otherwise, and an overwrite has them already, from the live /root it copies), then the profiles chosen, when the machine is
        online; an SSH server is its profile and then the server turned on. Steam is offered on x86-64 only, so it is dropped
        elsewhere. Answers how many commands.
*/
static int installer_plan(const struct installer_choice address_to c, b32 x86, char address_to into, positive room)
{
        int count = 0;

        into[0] = 0;
        if (!c->drive[0] || installer_skipped(c->drive))
                return 0;
        installer_plan_line(into, room, "/moonwater", "install", c->drive, installer_mode_word(c->mode), "yes"), count++;
        if (c->language_changed && c->language[0])
                installer_plan_line(into, room, "/moonwater", "language", c->language, null, null), count++;
        if (c->keyboard_changed && c->keyboard[0])
                installer_plan_line(into, room, "/moonwater", "keyboard", c->keyboard, null, null), count++;
        if (c->online && (c->profiles & INSTALLER_CHROME))
                installer_plan_line(into, room, "/bowl", "profile", "chrome", null, null), count++;
        if (c->online && x86 && (c->profiles & INSTALLER_STEAM))
                installer_plan_line(into, room, "/bowl", "profile", "steam", null, null), count++;
        if (c->online && (c->profiles & INSTALLER_SSH))
        {
                installer_plan_line(into, room, "/bowl", "profile", "ssh", null, null), count++;
                installer_plan_line(into, room, "/moonwater", "ssh", "on", null, null), count++;
        }
        return count;
}

#endif // MOONWATER_INSTALLER_MODEL
