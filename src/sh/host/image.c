/*
        src/sh/host/image.c: the settings an image carries and a disk keeps (host_settings_*): read, written, carried
        and applied.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/* One ioctl of /dev/spark, the device opened for it and let go of. */
static bipolar host_spark_once(unsigned int command, void *request, unsigned int flags)
{
        bipolar device = system_open_at(AT_FDCWD, SPARK_DEVICE, flags | O_CLOEXEC);
        bipolar failed;

        if (device < 0)
                return device;

        failed = system_control(device, command, request);
        system_close(device);
        return failed;
}


// Settings ------------------------------------------------------

/*
        What this machine does at boot and when it stops, kept in the boot
        image itself.

        The block is spark.c's: two slots in the image's .mwset section. A
        change is made to this session's copy, /run/moonwater/settings, and
        then written over the older slot of the image this session started
        from. That image is found by its build and by the medium id and
        generation the session's copy carries, so of a stick and a disk with
        the same build only the one that booted is written.

        The write goes to the partition underneath the file, at the blocks the
        filesystem names, and only once those blocks have read back as the
        file's own bytes. The file keeps its size, and the FAT, the directory
        entry and its times are never touched, so power lost part way tears
        one slot and leaves the other to boot from. No image is copied or
        renamed to change a setting.

        What cannot be written -- a read-only stick, an image found on no
        disk, one built without the section -- stays this session's, says
        why, and still goes along with moonwater setup install.
*/
#define HOST_SETTINGS HOST_STATE "/settings"
#define HOST_SETTINGS_SECTION ".mwset\0\0"
#define HOST_SETTINGS_PAGE 4096
#define HOST_SETTINGS_BLOCKS (SPARK_SETTINGS_SLOT / 512)
#define HOST_FIBMAP 1
#define HOST_FIGETBSZ 2

#define HOST_SETTINGS_CHANGED 0
#define HOST_SETTINGS_SHOWN 1
#define HOST_SETTINGS_USAGE 2
#define HOST_SETTINGS_REFUSED 3

typedef struct
{
        struct spark_settings_entry entry;
        p8 address_to text;
        positive at;
} host_setting;

static const struct
{
        string_address verb;
        p8 list;
        string_address empty;
} host_lists[] = {
    {"init", SPARK_SETTINGS_INIT, "nothing runs at boot"},
    {"exit", SPARK_SETTINGS_EXIT, "nothing runs when the machine stops"},
};

/*
        Which command each switch is spelled under. Moving one is a line here.

        The block also holds the startup list, which nothing reads yet: no
        command takes it until the machine does, so none is a setting that is
        accepted and then does nothing. Canvas at boot is read by the kernel
        before it probes for a card.
*/
static const struct
{
        string_address verb;
        string_address word;
        p32 flag;
} host_switches[] = {
    {"init", "mount", SPARK_SETTINGS_MOUNT_OFF},
    {"init", "canvas", SPARK_SETTINGS_CANVAS_OFF},
};

static fn host_settings_empty(host_settings address_to settings)
{
        memory_zero(settings, sizeof(address_to settings));
        settings->magic = SPARK_SETTINGS_MAGIC;
        settings->version = SPARK_SETTINGS_VERSION;
        settings->header = SPARK_SETTINGS_HEADER;
        settings->slot = SPARK_SETTINGS_SLOT;
}

static fn host_settings_seal(host_settings address_to settings)
{
        settings->sum = 0;
        settings->sum = spark_settings_sum(settings);
}

/* The entry at at, stepping at past it; false once there are no more. */
static bool host_settings_next(host_settings address_to settings,
                               positive address_to at,
                               host_setting address_to into)
{
        if (address_to at + SPARK_SETTINGS_ENTRY > settings->length)
                return false;

        into->at = address_to at;
        memory_copy_apart(address_of into->entry, settings->payload + into->at,
                          SPARK_SETTINGS_ENTRY);
        //      Every caller copies the text into a buffer of the most an
        //      entry may hold, so an entry that says more is the end of the
        //      list, whoever checked the block before it got here.
        if (into->entry.length > SPARK_SETTINGS_TEXT_MOST ||
            into->entry.length > settings->length - (address_to at + SPARK_SETTINGS_ENTRY))
                return false;
        into->text = settings->payload + into->at + SPARK_SETTINGS_ENTRY;
        address_to at += SPARK_SETTINGS_ENTRY +
                         spark_settings_padded(into->entry.length);
        return true;
}

/* An entry's words, the way the command line spells them. */
static fn host_settings_text(p8 address_to into, host_setting address_to setting)
{
        if (setting->entry.kind == SPARK_SETTINGS_SHELL)
                string_copy(into, "shell");
        else if (setting->entry.kind == SPARK_SETTINGS_KERNEL_SHELL)
                string_copy(into, "kernel_shell");
        else
        {
                memory_copy_apart(into, setting->text, setting->entry.length);
                into[setting->entry.length] = end;
        }
}

static positive host_settings_count(host_settings address_to settings, p8 list)
{
        host_setting setting;
        positive at = 0;
        positive count = 0;

        while (host_settings_next(settings, address_of at, address_of setting))
                count += setting.entry.list == list;

        return count;
}

/*
        An entry at the end of its list, with the next id that list hands out:
        ids are never given twice, so a remove by number cannot reach an entry
        added after the number was read. Null once it is in, else why not.
*/
static string_address host_settings_add(host_settings address_to settings,
                                        p8 list, p8 kind, string_address text,
                                        positive length, p16 address_to id)
{
        positive room = spark_settings_padded(length);
        bool security = list == SPARK_SETTINGS_SECURITY;
        bool group = list == SPARK_SETTINGS_GROUP;
        struct spark_settings_entry entry;

        if (length > (list == SPARK_SETTINGS_BIND ? SPARK_SETTINGS_BIND_TEXT_MOST
                      : security                  ? SPARK_SETTINGS_TRUST_MOST
                      : group                     ? SPARK_SETTINGS_GROUP_MOST
                                                  : SPARK_SETTINGS_TEXT_MOST))
                return list == SPARK_SETTINGS_BIND ? "a bound command is 255 bytes at most"
                       : security                  ? "an anchor is 552 bytes at most"
                       : group                     ? "a group's key and name are 67 bytes at most"
                                                   : "an entry is 4096 bytes at most";

        if (host_settings_count(settings, list) >=
            (list == SPARK_SETTINGS_BIND ? SPARK_SETTINGS_BIND_MOST
             : security                  ? SPARK_SETTINGS_SECURITY_MOST
             : group                     ? SPARK_SETTINGS_GROUPS
                                         : SPARK_SETTINGS_LIST_MOST))
                return list == SPARK_SETTINGS_BIND ? "the bind table holds 48 events at most"
                       : security                  ? "the security list holds 96 entries at most"
                       : group                     ? "an image carries 4 groups at most"
                                                   : "a list holds 16 entries at most";

        if (SPARK_SETTINGS_PAYLOAD - settings->length < SPARK_SETTINGS_ENTRY + room)
                return "the settings block is full";

        memory_zero(address_of entry, sizeof(entry));
        entry.list = list;
        entry.kind = kind;
        entry.length = (p16)length;

        //      A bound command's id is its event, a security entry's says
        //      which switch it is or who added it, and a group's is its place
        //      of four: the caller names each.
        if (list == SPARK_SETTINGS_BIND || security || group)
                entry.id = id ? address_to id : 0;
        else
        {
                p16 address_to next = settings->next + list - 1;

                entry.id = address_to next ? address_to next : 1;
                if (entry.id == 0xffff)
                        return "this list has handed out every id it has";

                address_to next = entry.id + 1;
        }

        memory_copy_apart(settings->payload + settings->length, address_of entry,
                          SPARK_SETTINGS_ENTRY);
        memory_copy_apart(settings->payload + settings->length + SPARK_SETTINGS_ENTRY,
                          text, length);
        memory_zero(settings->payload + settings->length + SPARK_SETTINGS_ENTRY +
                        length,
                    room - length);
        settings->length += SPARK_SETTINGS_ENTRY + room;

        if (id)
                address_to id = entry.id;

        return null;
}

static fn host_settings_drop(host_settings address_to settings,
                             host_setting address_to setting)
{
        positive size = SPARK_SETTINGS_ENTRY +
                        spark_settings_padded(setting->entry.length);

        memory_copy(settings->payload + setting->at,
                    settings->payload + setting->at + size,
                    settings->length - setting->at - size);
        settings->length -= size;
        memory_zero(settings->payload + settings->length, size);
}

/* An entry of a list by its exact words. */
static bool host_settings_find_words(host_settings address_to settings, p8 list,
                                     string_address wanted, host_setting address_to into)
{
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        positive at = 0;

        while (host_settings_next(settings, address_of at, into))
                if (into->entry.list == list &&
                    (host_settings_text(text, into), string_equals(text, wanted)))
                        return true;

        return false;
}

/* An entry named by its id, all digits, or by its exact words. */
static bool host_settings_find(host_settings address_to settings, p8 list,
                               string_address wanted, host_setting address_to into)
{
        positive id = 0;
        positive at = 0;
        string_address stop = wanted;
        b32 range = 0;
        bool by_id = byte_is_digit(*wanted);

        if (by_id)
                id = string_to_number_unsigned_checked(wanted, address_of stop, 10,
                                                       address_of range);
        by_id = by_id && !*stop && !range && id <= 0xffff;

        if (!by_id)
                return host_settings_find_words(settings, list, wanted, into);

        while (host_settings_next(settings, address_of at, into))
                if (into->entry.list == list && into->entry.id == id)
                        return true;

        return false;
}

//      Where host_words_write puts what it is handed, and whether it ran out.
static p8 address_to host_words_into;
static positive host_words_room;
static bool host_words_full;

static fn host_words_write(address_any data, positive length)
{
        positive used = string_length(host_words_into);

        if (!length)
                length = string_length(data);
        if (used + length >= host_words_room)
        {
                host_words_full = true;
                return;
        }
        memory_copy_apart(host_words_into + used, data, length);
        host_words_into[used + length] = end;
}

/* Words given apart, joined by one space as a shell would have them: one
   word is the command as it was typed, and several are quoted where they
   need it, so `bind init add sh -c 'echo hi there'` keeps the quotes that
   made `echo hi there` one word instead of running `sh -c echo hi there`. */
static bool host_words_join(p8 address_to into, positive room,
                            string_address address_to words, positive count,
                            positive address_to length)
{
        into[0] = end;
        host_words_into = into;
        host_words_room = room;
        host_words_full = false;

        for (positive at = 0; at < count && !host_words_full; at++)
        {
                if (at)
                        host_words_write(" ", 1);
                if (count == 1)
                        host_words_write((address_any)words[at], 0);
                else
                        writer_shell_name(host_words_write, words[at]);
        }

        address_to length = string_length(into);
        return !host_words_full;
}

/* Which slot a write goes over: a damaged one, else the older. */
static positive host_settings_older(host_settings address_to slots)
{
        if (spark_settings_check(slots) < 0)
                return 0;
        if (spark_settings_check(slots + 1) < 0)
                return 1;

        return slots[1].generation < slots[0].generation ? 1 : 0;
}

/* One past the newest generation either slot believably carries. */
static p64 host_settings_generation(host_settings address_to slots)
{
        p64 most = 0;

        for (positive at = 0; at < 2; at++)
                if (spark_settings_check(slots + at) >= 0 && slots[at].generation > most)
                        most = slots[at].generation;

        return most + 1;
}

/*
        The first page of a PE image read into head, and where its section
        table is in it and how many sections it names: false for a file that
        is not one, or one whose section table does not end inside the page.
*/
static bool host_pe_table(bipolar handle, p8 address_to head, positive address_to table,
                          positive address_to count)
{
        positive pe;

        if (file_transfer_exact(syscall(pread64), handle, head, HOST_SETTINGS_PAGE, 0) !=
                (bipolar)HOST_SETTINGS_PAGE ||
            head[0] != 'M' || head[1] != 'Z')
                return false;

        pe = storage_le32(head + 0x3c);
        if (pe > HOST_SETTINGS_PAGE - 24 || memory_compare(head + pe, "PE\0\0", 4))
                return false;

        address_to count = storage_le16(head + pe + 6);
        address_to table = pe + 24 + storage_le16(head + pe + 20);
        return address_to table + address_to count * 40 <= HOST_SETTINGS_PAGE;
}

/*
        Whether a file runs as far as its own section table says it does. An
        image cut short, by a copy that stopped or a stick pulled, still names
        its build in its first page, and everything past the cut is missing
        from what would be copied and read back as itself.
*/
static bool host_image_whole(string_address path)
{
        p8 head[HOST_SETTINGS_PAGE];
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        positive table;
        positive count;
        bool whole = false;

        if (handle < 0)
                return false;

        if (host_pe_table(handle, head, address_of table, address_of count) && count &&
            file_look_code(AT_FDCWD, path, 0, address_of facts) >= 0)
        {
                whole = true;
                for (positive at = 0; at < count; at++)
                {
                        p8 address_to section = head + table + at * 40;

                        if ((p64)storage_le32(section + 20) + storage_le32(section + 16) >
                            facts.size)
                                whole = false;
                }
        }

        system_close(handle);
        return whole;
}

/*
        Whether a file is an image this machine's firmware starts, from the
        file and not from its name: a PE32+ EFI application (subsystem 10)
        whose header names this machine, that runs as far as its own sections
        say, and that names a build, which goes into build as
        host_running_build spells this one. 0 when it is; else why not, one
        of the reasons below. machine, when asked for, is what the header
        names.
*/
enum
{
        HOST_FIT_GOOD,
        HOST_FIT_NONE,     // nothing that can be read is there
        HOST_FIT_NOT_EFI,  // not an EFI application
        HOST_FIT_MACHINE,  // one for another machine
        HOST_FIT_SHORT,    // cut short of its own sections
        HOST_FIT_NO_BUILD, // no build named in it
};

#define HOST_PE_OPTIONAL_LEAST 72 // the optional header as far as its subsystem
#define HOST_PE_PLUS 0x20b
#define HOST_PE_EFI_APPLICATION 10

/* The header's half of that, for an image for the machine expected: one of
   HOST_FIT_GOOD, NONE, NOT_EFI and MACHINE. */
static b32 host_image_shape(string_address path, p16 expected, p16 address_to machine)
{
        p8 head[HOST_SETTINGS_PAGE];
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC | O_NONBLOCK);
        file_facts facts;
        positive table;
        positive count;
        b32 fit = HOST_FIT_NOT_EFI;

        if (machine)
                *machine = 0;
        if (handle < 0)
                return HOST_FIT_NONE;

        //      A file, and no FIFO or device a stick might put at the name.
        if (file_look_code(AT_FDCWD, path, 0, address_of facts) < 0 ||
            (facts.mode & MODE_FORMAT) != MODE_FILE)
                fit = HOST_FIT_NONE;
        else if (host_pe_table(handle, head, address_of table, address_of count) && count)
        {
                positive pe = storage_le32(head + 0x3c);
                p16 named = storage_le16(head + pe + 4);

                if (machine)
                        *machine = named;
                //      The section table is inside the page (host_pe_table), and an
                //      optional header as long as this one ends before it.
                if (storage_le16(head + pe + 20) >= HOST_PE_OPTIONAL_LEAST &&
                    storage_le16(head + pe + 24) == HOST_PE_PLUS &&
                    storage_le16(head + pe + 24 + 68) == HOST_PE_EFI_APPLICATION)
                        fit = named == expected ? HOST_FIT_GOOD : HOST_FIT_MACHINE;
        }
        system_close(handle);
        return fit;
}

static b32 host_image_fit(string_address path, p8 address_to build, positive room,
                          p16 address_to machine)
{
        b32 fit = host_image_shape(path, HOST_PE_MACHINE, machine);

        build[0] = end;
        if (fit)
                return fit;
        if (!host_image_whole(path))
                return HOST_FIT_SHORT;
        if (!host_image_build(path, build, room))
                return HOST_FIT_NO_BUILD;
        return HOST_FIT_GOOD;
}

// A machine as a PE header names it.
static string_address host_pe_machine(p16 machine)
{
        return machine == 0x8664   ? "x86-64"
               : machine == 0xaa64 ? "arm64"
               : machine == 0x5064 ? "riscv64"
               : machine == 0x14c  ? "32-bit x86"
               : machine == 0x1c4  ? "32-bit arm"
                                   : "another kind of machine";
}

// Why an image is not one to write, in words that follow a colon.
static string_address host_fit_said(b32 fit, p16 machine)
{
        static p8 said[96];

        switch (fit)
        {
        case HOST_FIT_NONE:
                return "there is no image there that can be read";
        case HOST_FIT_NOT_EFI:
                return "it is not an EFI application";
        case HOST_FIT_MACHINE:
                (void)string_join(said, sizeof(said), "it is for ", host_pe_machine(machine),
                                  ", and this machine is ", host_pe_machine(HOST_PE_MACHINE));
                return said;
        case HOST_FIT_SHORT:
                return "it ends before its own sections do";
        case HOST_FIT_NO_BUILD:
                return "it names no build";
        }
        return "";
}

/*
        Where an image keeps its settings: the file offset of its first slot,
        or 0 for an image without them. The section table says where, by the
        section's name, a place on a page and room for two slots, and one slot
        must carry the magic there before anything believes it. One and not
        both: a slot whose first sector is lost is what a cut write leaves,
        the kernel starts on the other (spark_settings_newest), and the slot
        that does not check is the one the next write takes
        (host_settings_older). Asked of both, such an image read as one built
        without settings, so an update stamped the defaults over what was
        kept and a save was refused instead of writing the lost slot again.
*/
static p64 host_settings_section(bipolar handle)
{
        p8 head[HOST_SETTINGS_PAGE];
        p64 magic[2];
        positive table;
        positive count;

        if (!host_pe_table(handle, head, address_of table, address_of count))
                return 0;

        for (positive at = 0; at < count; at++)
        {
                p8 address_to section = head + table + at * 40;
                p64 raw = storage_le32(section + 20);

                if (memory_compare(section, HOST_SETTINGS_SECTION, 8))
                        continue;

                if (!raw || raw % HOST_SETTINGS_PAGE ||
                    storage_le32(section + 16) < 2 * SPARK_SETTINGS_SLOT ||
                    file_transfer_exact(syscall(pread64), handle, (p8 address_to)magic, 8,
                                        raw) != 8 ||
                    file_transfer_exact(syscall(pread64), handle,
                                        (p8 address_to)(magic + 1), 8,
                                        raw + SPARK_SETTINGS_SLOT) != 8 ||
                    (magic[0] != SPARK_SETTINGS_MAGIC && magic[1] != SPARK_SETTINGS_MAGIC))
                        return 0;

                return raw;
        }

        return 0;
}

static bool host_settings_slots(bipolar handle, host_settings address_to slots,
                                p64 address_to offset)
{
        address_to offset = host_settings_section(handle);

        return address_to offset &&
               file_transfer_exact(syscall(pread64), handle, (p8 address_to)slots,
                                   2 * SPARK_SETTINGS_SLOT, address_to offset) ==
                   (bipolar)(2 * SPARK_SETTINGS_SLOT);
}

/*
        The settings an image boots with: 1 from a slot that checks, 0 for the
        defaults because both are damaged, -1 for an image without settings.
*/
static b32 host_settings_image(string_address path, host_settings address_to into)
{
        host_settings slots[2];
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        p64 offset;
        bool read;
        b32 newest;

        host_settings_empty(into);
        if (handle < 0)
                return -1;

        read = host_settings_slots(handle, slots, address_of offset);
        system_close(handle);
        if (!read)
                return -1;

        newest = spark_settings_newest(slots);
        if (newest < 0)
                return 0;

        memory_copy_apart(into, slots + newest, SPARK_SETTINGS_SLOT);
        return 1;
}

static bool host_settings_kept(host_settings address_to into)
{
        //      Not through a link, as host_read_state reads every other state
        //      file: a link planted at the name is no copy of this session's.
        bipolar handle = system_open_at(AT_FDCWD, HOST_SETTINGS,
                                        FILE_READ | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        bipolar got;

        if (handle < 0)
                return false;

        got = file_transfer_exact(syscall(pread64), handle, (p8 address_to)into,
                                  SPARK_SETTINGS_SLOT, 0);
        system_close(handle);
        return got == (bipolar)SPARK_SETTINGS_SLOT && spark_settings_check(into) >= 0;
}

/*
        The kernel's copy: what the image booted with, or what was set since.
        A kernel with no /dev/spark, or started without the stub, has none.
*/
static bool host_settings_booted(host_settings address_to into)
{
        struct spark_settings_request request = {(unsigned long)into, 0};

        return host_spark_once(SPARK_IOCTL_SETTINGS_GET, address_of request,
                               FILE_READ_WRITE) >= 0 &&
               spark_settings_check(into) >= 0;
}

/*
        This session's copy, root's alone because a command can carry a secret,
        and the kernel's. True once both are as the caller has them: a kernel
        with no /dev/spark has no copy to be out of step, and is the only
        refusal that is not one. A copy that could not be written is not left
        beside the old one.
*/
static bool host_settings_keep(host_settings address_to settings)
{
        struct spark_settings_request request = {(unsigned long)settings, 0};
        bipolar set;
        bool written;

        host_settings_seal(settings);
        host_state_ready();
        set = host_spark_once(SPARK_IOCTL_SETTINGS_SET, address_of request,
                              FILE_READ_WRITE);

        written = host_publish(HOST_SETTINGS, settings, SPARK_SETTINGS_SLOT, 0600) >= 0;

        return written && (set >= 0 || set == -ENOENT || set == -ENODEV);
}

typedef struct
{
        string_address build;
        host_settings address_to session;
        p8 name[HOST_NAME_ROOM];
        p8 partuuid[STORAGE_PARTUUID_ROOM];
        p8 other[HOST_NAME_ROOM];
        positive count;
        host_settings found;
} host_settings_search;

typedef struct
{
        host_settings_search address_to search;
        string_address name;
        string_address partuuid;
} host_settings_look;

static bool host_settings_image_visit(string_address path, address_any opaque)
{
        host_settings_look address_to look = (host_settings_look address_to)opaque;
        host_settings_search address_to search = look->search;
        p8 build[HOST_BUILD_ROOM];
        host_settings newest;

        if (!host_image_build(path, build, sizeof(build)) ||
            !string_equals(build, search->build) ||
            host_settings_image(path, address_of newest) < 0 ||
            (search->session &&
             (newest.generation != search->session->generation ||
              memory_compare(newest.medium, search->session->medium,
                             sizeof(newest.medium)))))
                return false;

        if (!search->count)
        {
                string_copy(search->name, look->name);
                string_copy(search->partuuid, look->partuuid);
                memory_copy_apart(address_of search->found, address_of newest,
                                  SPARK_SETTINGS_SLOT);
        }
        else if (search->count == 1)
                string_copy(search->other, look->name);

        search->count++;
        return true;
}

/* A FAT partition whose image is this build and, when asked, this session's copy. */
static bool host_settings_visit(storage_identity address_to identity,
                                address_any opaque)
{
        host_settings_look look;

        look.search = (host_settings_search address_to)opaque;
        look.name = identity->path + sizeof("/dev/") - 1;
        look.partuuid = identity->partuuid_length ? (string_address)identity->partuuid
                                                  : (string_address) "";

        if (string_equals(identity->type, "vfat") && host_name_valid(look.name))
                host_with_image(look.name, HOST_MEDIUM, host_settings_image_visit,
                                address_of look);

        return true;
}

/*
        What a disk nobody has authenticated is allowed to say.

        The only thing tying a settings image on some disk to this build is
        the version string in that image's own setup header, and that string
        is bytes in a file: anybody who can read /proc/version can write it
        into an image of their own. Believing it about the two switches is a
        nuisance at worst. Believing it about the lists hands whoever pushed
        the disk in what this machine runs -- every entry in the payload is a
        command, at boot, at stop, or on a bound event, and these settings do
        not stay in memory: an install stamps them into the image it writes,
        and moonwater init and exit save them as this session's. So the switches are
        taken and the payload is not, and the startup flag goes with it
        because it says a list was written.
*/
static fn host_settings_untrusted(host_settings address_to settings)
{
        memory_zero(settings->payload, settings->length);
        settings->length = 0;
        settings->flags &= ~SPARK_SETTINGS_STARTUP_SET;
        memory_zero(settings->next, sizeof(settings->next));
        host_settings_seal(settings);
}

/*
        This session's settings: its own copy, else the one image of this
        build a disk here has, else the defaults. False when what came back
        is not known to be anything: the copies are root's, so for anybody
        else the defaults are an empty block and not a fact about this
        machine.
*/
static bool host_settings_session(host_settings address_to settings)
{
        host_settings_search search;
        p8 running[HOST_BUILD_ROOM];

        if (host_settings_kept(settings) || host_settings_booted(settings))
                return true;

        host_settings_empty(settings);

        if (!bowl_is_root())
                return false;

        /*  Tighter than safe hears nothing from a disk at all: the switches
            are only a nuisance, but a machine that wants no word from media
            somebody pushed in gets none. */
        if (MOONWATER_STRICT_HOST >= STRICT_TIGHT)
                return true;

        memory_zero(address_of search, sizeof(search));

        if (!host_running_build(running, sizeof(running)))
                return true;

        search.build = running;
        storage_each_device(host_settings_visit, address_of search);

        if (search.count != 1)
                return true;

        memory_copy_apart(settings, address_of search.found, SPARK_SETTINGS_SLOT);
        host_settings_untrusted(settings);
        return true;
}

typedef struct
{
        host_settings slots[2];
        p64 offset;
        b32 blocks[HOST_SETTINGS_BLOCKS];
        positive size;
        positive target;
        p64 seen;
        string_address failed;
} host_settings_place;

/* The partition blocks under the slot a write goes over, as its filesystem names them. */
static bool host_settings_map_visit(string_address path, address_any opaque)
{
        host_settings_place address_to place = (host_settings_place address_to)opaque;
        string_address failed = null;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        b32 size = 0;

        if (handle < 0)
                failed = "has no image";
        else if (!host_settings_slots(handle, place->slots, address_of place->offset))
                failed = "has an image built without settings";
        else if (system_control(handle, HOST_FIGETBSZ, address_of size) < 0 ||
                 size < 512 || size > HOST_SETTINGS_PAGE || HOST_SETTINGS_PAGE % size)
                failed = "keeps its image in blocks this cannot map";

        place->size = failed ? 512 : (positive)size;
        place->target = failed ? 0 : host_settings_older(place->slots);

        for (positive at = 0; !failed && at < SPARK_SETTINGS_SLOT / place->size; at++)
        {
                b32 block = (b32)((place->offset + place->target * SPARK_SETTINGS_SLOT) /
                                      place->size +
                                  at);

                if (system_control(handle, HOST_FIBMAP, address_of block) < 0 || block <= 0)
                        failed = "keeps its image in blocks this cannot map";

                place->blocks[at] = block;
        }

        if (handle >= 0)
                system_close(handle);

        place->failed = failed;
        return !failed;
}

static string_address host_settings_map(string_address name,
                                        host_settings_place address_to place)
{
        place->failed = "could not be mounted";
        host_with_image(name, HOST_MEDIUM, host_settings_map_visit, place);
        return place->failed;
}

/* The slots as the disk has them, the image's pages dropped first, and where in
   the image the section was (0 for an image with none). */
static bool host_settings_read_back(string_address path, address_any opaque)
{
        host_settings_place address_to place = (host_settings_place address_to)opaque;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

        place->seen = 0;
        if (handle >= 0)
        {
                system_call_4(syscall(fadvise64), (positive)handle, 0, 0,
                              HOST_FADVISE_DONTNEED);
                if (!host_settings_slots(handle, place->slots, address_of place->seen))
                        place->seen = 0;
                system_close(handle);
        }

        return place->seen;
}

/*
        This session's settings over the older slot of the image on name.

        Nothing else may have the partition mounted, and the blocks must hold
        the file's bytes before one is written. Afterwards the partition's
        cache is flushed and the file read again from a fresh mount with its
        pages dropped, so what is checked is the disk and not memory. Null
        once written and read back, else how it went wrong, said after the
        partition's name.
*/
static string_address host_settings_write(string_address name, string_address partuuid,
                                          host_settings address_to settings)
{
        host_settings_place place;
        host_settings slot;
        storage_mount_table table;
        p8 device[HOST_NAME_ROOM + 8];
        p8 path[HOST_PATH_ROOM];
        p8 block[HOST_SETTINGS_PAGE];
        string_address failed = null;
        bipolar handle;
        b32 newest;

        if (!host_join(device, sizeof(device), "/dev/", name) ||
            !host_join(path, sizeof(path), "/sys/class/block/", name))
                return "has a name too long to write";

        if (host_block_flag(path, "/ro"))
                return "is read-only";

        //      The name was found some time ago, and a disk comes and goes.
        if (partuuid[0])
        {
                p8 current[HOST_NAME_ROOM];

                if (!host_partition_uuid_resolve(partuuid, current) ||
                    !string_equals(current, name))
                        return "is no longer the partition it was found as";
        }

        //      A table that cannot be read says nothing about what is mounted,
        //      and is no reason to write over a partition that may be.
        if (!storage_mount_table_load(address_of table, null))
                return "could not be checked for mounts, so nothing was written";
        {
                bool mounted = false;

                for (positive at = 0; at < table.count; at++)
                        mounted |= string_equals(table.entry[at].source, device);

                storage_mount_table_release(address_of table);
                if (mounted)
                        return "is mounted; unmount it and try again";
        }

        failed = host_settings_map(name, address_of place);
        if (failed)
                return failed;

        memory_copy_apart(address_of slot, settings, SPARK_SETTINGS_SLOT);
        slot.generation = host_settings_generation(place.slots);

        if (host_medium_blank(slot.medium, sizeof(slot.medium)))
        {
                newest = spark_settings_newest(place.slots);
                if (newest >= 0)
                        memory_copy_apart(slot.medium, place.slots[newest].medium,
                                          sizeof(slot.medium));

                host_medium_fresh(slot.medium, sizeof(slot.medium));
        }

        host_settings_seal(address_of slot);

        handle = system_open_at(AT_FDCWD, device,
                                FILE_READ_WRITE | FILE_EXCLUSIVE | O_CLOEXEC);
        if (handle < 0)
                return handle == -ERROR_BUSY ? "is in use; unmount it and try again"
                       : handle == -ERROR_READ_ONLY || handle == -ERROR_ACCESS
                           ? "is read-only"
                           : "cannot be opened for writing";

        for (positive at = 0; !failed && at < SPARK_SETTINGS_SLOT / place.size; at++)
                if (file_transfer_exact(syscall(pread64), handle, block, place.size,
                                        (p64)place.blocks[at] * place.size) !=
                        (bipolar)place.size ||
                    memory_compare(block,
                                   (p8 address_to)(place.slots + place.target) +
                                       at * place.size,
                                   place.size))
                        failed = "does not keep its image where its filesystem says, "
                                 "so nothing was written";

        for (positive at = 0; !failed && at < SPARK_SETTINGS_SLOT / place.size; at++)
                if (storage_write(handle, (p8 address_to)address_of slot + at * place.size,
                                  place.size, (p64)place.blocks[at] * place.size) !=
                    (bipolar)place.size)
                        failed = "could not be written; its other slot still has the "
                                 "settings from before";

        if (!failed && system_call_1(syscall(fsync), (positive)handle) < 0)
                failed = "could not be synced; its other slot still has the settings "
                         "from before";

        system_control(handle, HOST_BLKFLSBUF, 0);
        system_close(handle);
        if (failed)
                return failed;

        if (!host_with_image(name, HOST_MEDIUM, host_settings_read_back, address_of place))
                return "was written, and could not be mounted again to check it";

        if (place.seen != place.offset ||
            memory_compare(place.slots + place.target, address_of slot,
                           SPARK_SETTINGS_SLOT) ||
            spark_settings_newest(place.slots) != (b32)place.target)
                return "was written and did not read back as written";

        settings->generation = slot.generation;
        memory_copy_apart(settings->medium, slot.medium, sizeof(slot.medium));
        return null;
}

/*
        Writes this session's settings to its image where it can, ending the
        line with where they went. False when the session could not keep its
        own copy, which the line then says: the change is in the image or
        nowhere, and the next command would read the old one.
*/
static bool host_settings_save(host_settings address_to settings)
{
        host_settings_search search;
        p8 running[HOST_BUILD_ROOM];
        string_address failed = null;

        memory_zero(address_of search, sizeof(search));

        if (host_running_build(running, sizeof(running)))
        {
                search.build = running;
                search.session = settings;
                storage_each_device(host_settings_visit, address_of search);
        }

        if (search.count == 1)
                failed = host_settings_write(search.name, search.partuuid, settings);

        if (search.count == 1 && !failed)
                string_format(log, "saved in the image on %s\n", search.name);
        else if (search.count == 1)
                string_format(log, "this session only: %s %s\n", search.name, failed);
        else if (search.count)
                string_format(log, "this session only: %s and %s both have this "
                                   "session's image\n",
                              search.name, search.other);
        else
                string_format(log, "this session only: no disk here has the image "
                                   "this session started from\n");

        log_flush();
        if (host_settings_keep(settings))
                return true;

        host_say(log_error, host_label "this session could not keep its copy of "
                                       "the settings; the next command reads the "
                                       "ones from before\n");
        return false;
}

/*
        A saved command as it is shown: a script keeps its lines and tabs, and
        every other control byte, and bytes that are not valid text, are
        written as \xNN, so what an image carries in its settings cannot put
        a control sequence in front of the terminal's parser.
*/
static string_address host_plain(string_address text)
{
        static p8 shown[SPARK_SETTINGS_TEXT_MOST * 4 + 8];
        positive used = 0;
        positive at = 0;

        while (text[at] && used + 8 < sizeof(shown))
        {
                positive start = at;

                while (text[at] && text[at] != '\n' && text[at] != '\t')
                        at++;
                radio_display(shown + used, sizeof(shown) - used, (p8 address_to)text + start,
                              at - start);
                used += string_length((string_address)shown + used);
                if (text[at] && used + 8 < sizeof(shown))
                        shown[used++] = (p8)text[at++];
        }
        shown[used] = end;
        return (string_address)shown;
}

/*
        One list said twice.

        `moonwater init` writes it as log lines of its own, and
        `moonwater status` sets the same three answers -- the hook that took
        the list away, the entries, or the sentence for an empty one -- under
        a heading on the session page. Only the frame around them differs,
        and a page is one block of output, so it does not flush per list.
*/
static fn host_settings_lines(host_settings address_to settings, positive which,
                              bool page)
{
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        host_setting setting;
        positive at = 0;
        positive shown = 0;

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                if (setting.entry.list != host_lists[which].list)
                        continue;

                if (page && !shown)
                        string_format(log, "  %s:\n", host_lists[which].verb);

                host_settings_text(text, address_of setting);
                string_format(log, page ? "    %p  %s\n" : "%p  %s\n",
                              (positive)setting.entry.id, host_plain((string_address)text));
                shown++;
        }

        if (!shown)
                string_format(log, page ? "  %s: %s\n" : host_label "%s: %s\n",
                              host_lists[which].verb, host_lists[which].empty);

        if (!page)
                log_flush();
}

static b32 host_settings_refused(string_address verb, string_address why,
                                 host_settings address_to settings)
{
        string_format(log_error, host_label "%s: %s", verb, why);
        if (string_equals(why, "the settings block is full"))
                string_format(log_error, " (%p of %p bytes); remove an entry first",
                              (positive)settings->length,
                              (positive)SPARK_SETTINGS_PAYLOAD);
        host_say(log_error, "\n");
        return HOST_SETTINGS_REFUSED;
}

/* The row of host_switches a verb's word names; the count when it names none. */
static positive host_switch_find(string_address verb, string_address word)
{
        positive at = 0;

        while (at < array_count(host_switches) &&
               !(string_equals(verb, host_switches[at].verb) &&
                 string_equals(word, host_switches[at].word)))
                at++;
        return at;
}

/*
        Whether the words are a settings command at all: a list alone, a
        switch alone or with on or off, add or remove with words. Said before
        anything is read to answer them, so a mistyped line costs its usage
        and not the search for a copy of the settings.
*/
static bool host_settings_shaped(string_address address_to arguments, positive count)
{
        if (string_table_find(arguments[1], host_lists, sizeof(host_lists[0]),
                              array_count(host_lists)) == array_count(host_lists))
                return false;

        if (count == 2)
                return true;

        if (host_switch_find(arguments[1], arguments[2]) < array_count(host_switches))
                return count == 3 ||
                       (count == 4 && host_onoff(arguments[3]) >= 0);

        return count >= 4 && (string_equals(arguments[2], "add") ||
                              string_equals(arguments[2], "remove"));
}

/*
        One settings command against a copy in memory, nothing read or written
        but that copy, once host_settings_shaped has said it is one: CHANGED
        once it printed what changed and wants the line finished by saving,
        SHOWN once it printed what was asked, USAGE for words that name
        nothing to add or remove, or REFUSED once it said why.
*/
static b32 host_settings_apply(host_settings address_to settings,
                               string_address address_to arguments, positive count,
                               p8 address_to text)
{
        string_address verb = arguments[1];
        host_setting setting;
        string_address failed;
        positive length = 0;
        positive at;
        positive which = string_table_find(verb, host_lists,
                                           sizeof(host_lists[0]),
                                           array_count(host_lists));
        p16 id = 0;
        p8 list;

        list = host_lists[which].list;

        if (count == 2)
        {
                host_settings_lines(settings, which, false);
                return HOST_SETTINGS_SHOWN;
        }
        at = host_switch_find(verb, arguments[2]);

        if (at < array_count(host_switches))
        {
                p32 flag = host_switches[at].flag;

                if (count == 3)
                {
                        host_say(log, host_label "%s %s %s\n", verb,
                                 host_switches[at].word,
                                 settings->flags & flag ? "off" : "on");
                        return HOST_SETTINGS_SHOWN;
                }

                //      A switch already as asked is not written again.
                if (!!(settings->flags & flag) == !host_onoff(arguments[3]))
                {
                        host_say(log, host_label "%s %s %s\n", verb,
                                 host_switches[at].word, arguments[3]);
                        return HOST_SETTINGS_SHOWN;
                }

                if (!host_onoff(arguments[3]))
                        settings->flags |= flag;
                else
                        settings->flags &= ~flag;

                string_format(log, host_label "%s %s %s; ", verb,
                              host_switches[at].word, arguments[3]);
                return HOST_SETTINGS_CHANGED;
        }

        {
                bool adding = string_equals(arguments[2], "add");

                if (!host_words_join(text, SPARK_SETTINGS_TEXT_MOST + 1, arguments + 3, count - 3,
                                         address_of length))
                        return host_settings_refused(verb, "an entry is 4096 bytes at most",
                                                     settings);

                if (!length)
                        return HOST_SETTINGS_USAGE;

                if (adding)
                {
                        //      The same line again, as a script says it at every
                        //      boot, is the entry it already has and not another
                        //      that runs beside it.
                        if (host_settings_find_words(settings, list, text, address_of setting))
                        {
                                host_say(log, host_label "%s %p is already: %s\n", verb,
                                         (positive)setting.entry.id, text);
                                return HOST_SETTINGS_SHOWN;
                        }
                        failed = host_settings_add(settings, list, SPARK_SETTINGS_COMMAND,
                                                   text, length, address_of id);
                        return_if(failed, host_settings_refused(verb, failed, settings));

                        string_format(log, host_label "%s %p added: %s; ", verb,
                                      (positive)id, text);
                        return HOST_SETTINGS_CHANGED;
                }

                if (!host_settings_find(settings, list, text, address_of setting))
                {
                        host_say(log_error, host_label "%s has no entry %s\n", verb,
                                 text);
                        return HOST_SETTINGS_REFUSED;
                }

                id = setting.entry.id;
                host_settings_text(text, address_of setting);
                host_settings_drop(settings, address_of setting);
                string_format(log, host_label "%s %p removed: %s; ", verb, (positive)id,
                              text);
                return HOST_SETTINGS_CHANGED;
        }
}

/* Where the image's own programs are: they are the image, and every boot has them. */
static const string_address host_image_programs[] = {
    "/bin/", "/sbin/", "/usr/bin/", "/usr/sbin/", "/lib/", "/usr/lib/", "/lib64/"};

/*
        A command naming a file the machine will not have when the entry runs:
        a live session's files, and /root, /home and the bowls once boot stops
        mounting them, are gone at power off. The image's own programs are not.
*/
static fn host_settings_note(host_settings address_to settings,
                             string_address verb, string_address text)
{
        p8 path[HOST_PATH_ROOM];
        p8 verdict[HOST_NAME_ROOM + 16];
        positive length = 0;
        bool kept = false;

        if (text[0] != '/')
                return;

        for (positive at = 0; at < array_count(host_image_programs); at++)
                if (host_starts(text, host_image_programs[at]))
                        return;

        while (text[length] && text[length] != ' ' && length + 1 < sizeof(path))
        {
                path[length] = text[length];
                length++;
        }
        path[length] = end;

        if (system_access_at(AT_FDCWD, path, 0) < 0)
                return;

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));

        for (positive at = 0; at < array_count(host_kept); at++)
        {
                positive prefix = string_length(host_kept[at].path);

                kept |= host_starts(path, host_kept[at].path) && path[prefix] == '/';
        }

        if (kept && string_has_prefix(verdict, "disk ") &&
            !(settings->flags & SPARK_SETTINGS_MOUNT_OFF))
                return;

        host_say(log, host_label "%s is read when the entry runs and is not kept "
                                 "after power off; moonwater %s add \"$(cat %s)\" "
                                 "keeps the script itself\n",
                 path, verb, path);
}

static b32 host_settings_locked(string_address address_to arguments, positive count)
{
        host_settings settings;
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        b32 outcome;

        host_state_ready();
        host_settings_session(address_of settings);

        outcome = host_settings_apply(address_of settings, arguments, count, text);
        return_if(outcome == HOST_SETTINGS_USAGE, host_usage());
        if (outcome != HOST_SETTINGS_CHANGED)
                return outcome == HOST_SETTINGS_SHOWN ? 0 : 1;

        if (!host_settings_save(address_of settings))
                return 1;

        //      The words of an added command, as apply joined them.
        if (count > 3 && string_equals(arguments[2], "add"))
                host_settings_note(address_of settings, arguments[1], text);

        return 0;
}

static b32 host_settings_command(string_address address_to arguments, positive count)
{
        b32 outcome;

        host_need_root();

        return_if(!host_settings_shaped(arguments, count), host_usage());

        //      Showing a list or a switch changes nothing and waits for nobody.
        if (count < 4)
                return host_settings_locked(arguments, count);

        if (!host_acquire())
                return 1;

        outcome = host_settings_locked(arguments, count);
        host_release();
        return outcome;
}

/*
        Both slots of an image not yet in place, as these settings: 0 once they
        are, 1 for an image with no section to put them in -- one built before
        there were settings, which starts with the defaults and has said so --
        and negative when the image could not be written.
*/
static bipolar host_settings_stamp(string_address path, host_settings address_to settings)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_CLOEXEC);
        bipolar failed = 0;
        p64 offset;

        if (handle < 0)
                return handle;

        offset = host_settings_section(handle);
        if (!offset)
                failed = 1;
        else
        {
                host_settings_seal(settings);
                failed = storage_format_write(handle, (p8 address_to)settings,
                                              SPARK_SETTINGS_SLOT, offset);
                if (!failed)
                        failed = storage_format_write(handle, (p8 address_to)settings,
                                                      SPARK_SETTINGS_SLOT,
                                                      offset + SPARK_SETTINGS_SLOT);
                if (!failed)
                        failed = system_call_1(syscall(fsync), (positive)handle);
        }

        system_close(handle);
        return failed;
}

typedef struct
{
        host_settings address_to into;
        b32 found;
} host_settings_found;

static bool host_settings_found_visit(string_address path, address_any opaque)
{
        host_settings_found address_to look = (host_settings_found address_to)opaque;

        look->found = host_settings_image(path, look->into);
        return look->found >= 0;
}

/* The settings an install's own image starts with. */
static bool host_settings_install(host_install address_to install,
                                  host_settings address_to into)
{
        host_settings_found look = {into, -1};

        return host_install_refresh(install) &&
               host_with_image(install->system, HOST_LOOK, host_settings_found_visit,
                               address_of look) &&
               look.found >= 0;
}

