/*
        `moonwater install`: the installer's verb, which its window (ui/installer.c) runs.

            moonwater install                          the drives, which one this booted from, which hold Moonwater
            moonwater install list                     the same, a drive a line, its fields a tab apart (the window reads it)
            moonwater install DRIVE [update|overwrite] [yes]

        update writes this build over the install on DRIVE and keeps its data and settings: the system partition's image is
        replaced (setup update's work, host_update: copied beside the loader, synced, read back and renamed over it, the image
        before kept as previous.efi), and the data partition, which holds the bowls, /root and /home, is not touched. It is
        offered only where DRIVE holds Moonwater, and is what a drive that holds it is given when no mode is said. overwrite
        erases DRIVE whole and installs it new (setup install's work, host_install_locked: a GPT, a FAT32 system partition and an
        ext4 data partition, this session's bowls, /root and /home copied onto it, and the image last). Either way the session
        then keeps DRIVE's data (host_take), so what is set after it (a language, a profile) is set on the install.

        The drive this live system started from (the stick whose image it is: host_medium_find) is never a target, nor is a
        loop, ram or zram device. At a terminal the verb asks, the drive's name typed, and a yes in its words is not taken
        for the answer; only where its input is no terminal (the window, a script) is yes the answer, and without it nothing
        is written. Then the image on DRIVE is read back from the disk, its settings slots aside, and its SHA-256 held to the
        image this system started from.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#include "../../canvas/ui/installer_model.c"

// The drive this session started from: the disk the image of this build is on. False when it is none here, or it cannot be looked for.
static bool host_install_boot_drive(p8 address_to into, positive room)
{
        p8 running[HOST_BUILD_ROOM];
        host_medium_search search;

        into[0] = end;
        if (!bowl_is_root() || !host_running_build(running, sizeof(running)) ||
            !host_medium_find(address_of search, running, null))
                return false;
        host_unmount(HOST_MEDIUM);
        return string_copy_bounded(into, search.disk, room) < room;
}

/*
        Whether an open disk, or a file written as one, has an install's layout as its GPT says it: a partition named
        HOST_SYSTEM_NAME and one named HOST_DATA_NAME, the names read from the table itself (UTF-16 at an entry's 56th byte), not
        from sysfs. What a check of the write path reads back from a file image; on a machine sysfs says the same (the census).
*/
static bool host_install_layout_is(bipolar handle, b32 sector)
{
        p8 header[512];
        p8 entry[128];
        p64 table;
        p32 count, size;
        bool system = false, data = false;

        if (sector < 512 || sector > 4096 || storage_read(handle, header, sizeof(header), sector) != sizeof(header) ||
            memory_compare(header, "EFI PART", 8))
                return false;
        table = storage_le64(header + 72);
        count = storage_le32(header + 80);
        size = storage_le32(header + 84);
        if (size < 128 || size > 1024 || count > 1024 ||
            table == 0 || table > (((p64)1 << 62) / (p64)sector))
                return false;
        for (p32 at = 0; at < count; at++)
        {
                p8 name[37];
                positive length = 0;

                if (storage_read(handle, entry, sizeof(entry), table * (p64)sector + (p64)at * size) != sizeof(entry))
                        return false;
                while (length < 36 && (entry[56 + 2 * length] || entry[57 + 2 * length]))
                {
                        name[length] = entry[57 + 2 * length] ? '?' : entry[56 + 2 * length];
                        length++;
                }
                name[length] = end;
                system |= string_equals(name, HOST_SYSTEM_NAME);
                data |= string_equals(name, HOST_DATA_NAME);
        }
        return system && data;
}

typedef struct
{
        p8 want[32];
        p8 got[32];
        bool read;
} host_install_sums;

static bool host_install_sum_visit(string_address system, address_any opaque)
{
        host_install_sums address_to sums = (host_install_sums address_to)opaque;
        p8 path[HOST_PATH_ROOM];

        sums->read = host_join(path, sizeof(path), system, HOST_IMAGE) && host_image_sum(path, true, sums->got);
        return sums->read && !memory_compare(sums->got, sums->want, sizeof(sums->want));
}

/*
        What was written, read back: the image on disk's system partition, from the disk itself (the partition mounted again with
        its buffers and the image's pages dropped, host_on_disk), and its SHA-256 with the settings slots read as nought held to
        the image this session started from, summed the same way. Said either way; 1 when they differ or cannot be read.
*/
static b32 host_install_verify(string_address disk)
{
        p8 running[HOST_BUILD_ROOM];
        p8 source[HOST_PATH_ROOM];
        p8 hex[65];
        host_medium_search search;
        host_census census;
        host_install address_to install;
        host_install_sums sums;
        bool alike;

        memory_zero(address_of sums, sizeof(sums));
        if (!host_running_build(running, sizeof(running)))
                return host_refuse("moonwater cannot read its own build\n");
        if (!host_acquire())
                return 1;
        host_census_take(address_of census);
        install = host_census_find(address_of census, disk);
        if (!install || !host_install_refresh(install))
        {
                host_release();
                return host_refuse("%s has no Moonwater install to read back\n", disk);
        }
        if (!host_medium_find(address_of search, running, disk) ||
            !host_join(source, sizeof(source), HOST_MEDIUM, HOST_IMAGE) || !host_image_sum(source, true, sums.want))
        {
                host_unmount(HOST_MEDIUM);
                host_release();
                return host_refuse("the image this started from could not be read to sum it\n");
        }
        host_unmount(HOST_MEDIUM);
        if (host_mount(install->system, HOST_SYSTEM, "vfat", HOST_READ_ONLY) < 0)
        {
                host_release();
                return host_refuse("%s could not be mounted to read it back\n", install->system);
        }
        alike = host_on_disk(install->system, HOST_SYSTEM, host_install_sum_visit, address_of sums);
        host_unmount(HOST_SYSTEM);
        host_release();
        memory_into_hex(hex, sums.got, 32);
        hex[64] = end;
        if (!alike)
                return host_refuse(sums.read ? "%s read back as sha256 %s, which is not the image written\n"
                                             : "%s could not be read back\n",
                                   disk, hex);
        host_say(log, host_label "%s reads back as the image written: sha256 %s\n", disk, hex);
        return 0;
}

// The drives as a person reads them, or as the window does (machine: the tab lines).
static b32 host_install_list(bool machine)
{
        struct installer_drive drives[INSTALLER_DRIVES];
        p8 boot[HOST_NAME_ROOM];
        p8 line[256];
        int count;

        host_install_boot_drive(boot, sizeof(boot));
        count = installer_drives_read("/sys", (const char address_to)boot, drives, INSTALLER_DRIVES);
        for (int at = 0; at < count; at++)
        {
                p8 size[24];

                if (machine)
                {
                        installer_line(&drives[at], (char address_to)line, sizeof(line));
                        string_format(log, "%s", line);
                        continue;
                }
                installer_size(drives[at].bytes, (char address_to)size, sizeof(size));
                string_format(log, "  %s  %s  %s%s%s%s\n", drives[at].name, size,
                              drives[at].model[0] ? (string_address)drives[at].model : (string_address) "a drive",
                              drives[at].removable ? (string_address) ", removable" : (string_address) "",
                              drives[at].moonwater ? (string_address) ", holds Moonwater" : (string_address) "",
                              drives[at].boot ? (string_address) ", this system started from it" : (string_address) "");
        }
        if (!machine && !count)
                string_format(log, "  no drive here to install on\n");
        log_flush();
        return 0;
}

static b32 host_install_verb(string_address address_to arguments, positive count)
{
        struct installer_drive drives[INSTALLER_DRIVES];
        struct installer_drive address_to drive = null;
        p8 boot[HOST_NAME_ROOM];
        p8 answer[HOST_NAME_ROOM];
        p8 size[24];
        string_address name;
        positive words = 3;
        int mode = -1;
        bool yes = false;
        bool sized = false;
        unsigned long long size_want = 0;
        bool interactive;
        bool keeps;
        p8 kept[HOST_NAME_ROOM];
        int found;
        b32 failed;

        if (count == 2)
                return host_install_list(false);
        if (count == 3 && string_equals(arguments[2], "list"))
                return host_install_list(true);
        name = host_disk_word(arguments[2]);
        if (count > words)
        {
                static const struct
                {
                        string_address word;
                        int mode;
                } modes[] = {
                    {"update", INSTALLER_UPDATE},
                    {"overwrite", INSTALLER_OVERWRITE},
                    {"use", INSTALLER_USE},
                };
                positive hit = string_table_lookup(arguments[words], modes);

                if (hit < array_count(modes))
                        mode = modes[hit].mode, words++;
        }
        if (count > words && string_equals(arguments[words], HOST_YES))
                yes = true, words++;
        //      The window's plan says the size the listing showed: the install refuses a drive that has changed since.
        if (count > words && arguments[words][0] >= '0' && arguments[words][0] <= '9')
        {
                string_address tail = null;

                sized = true;
                size_want = (unsigned long long)string_to_number_unsigned(arguments[words], address_of tail, 10);
                return_if(tail && *tail, host_usage());
                words++;
        }
        return_if(count != words || !host_name_valid(name), host_usage());
        host_need_root();
        interactive = host_asks();
        return_if(!interactive && !yes,
                  host_refuse("installing writes %s and nothing here can be asked: moonwater install %s %s " HOST_YES "\n", name, name,
                              installer_mode_word(mode < 0 ? INSTALLER_OVERWRITE : mode)));
        host_state_ready();

        //      Fail closed: when the drive this system started from cannot be found, no drive is written but by a use.
        return_if(!host_install_boot_drive(boot, sizeof(boot)) && mode != INSTALLER_USE,
                  host_refuse("cannot tell which drive this system started from, so nothing is written\n"));
        return_if(installer_excluded((const char address_to)name, (const char address_to)boot),
                  host_refuse(boot[0] && string_equals(name, boot) ? "%s is the drive this system started from, which an install copies\n"
                                                                 : "%s is not a drive to install on\n",
                              name));
        found = installer_drives_read("/sys", (const char address_to)boot, drives, INSTALLER_DRIVES);
        for (int at = 0; at < found; at++)
                if (string_equals((string_address)drives[at].name, name))
                        drive = &drives[at];
        return_if(!drive, host_refuse("there is no drive called %s\n", name));
        return_if(sized && drive->bytes != size_want,
                  host_refuse("%s is no longer the drive that was listed (its size changed): nothing is written\n", name));
        if (mode < 0)
                mode = drive->moonwater ? INSTALLER_UPDATE : INSTALLER_OVERWRITE;
        return_if(mode != INSTALLER_OVERWRITE && !drive->moonwater,
                  host_refuse("%s holds no Moonwater to %s: moonwater install %s overwrite erases it and installs anew\n", name,
                              installer_mode_word(mode), name));
        //      A session started from an installed stick keeps the stick's data: it writes another drive (overwrite, update)
        //      and goes on keeping the stick's, and it cannot take a second drive's data on top (use).
        keeps = host_session_disk(kept, sizeof(kept));
        return_if(keeps && mode == INSTALLER_USE, host_refuse("this session already keeps %s, so %s's data cannot be used too\n", kept, name));
        if (mode == INSTALLER_USE)
        {
                host_census census;
                host_install address_to install;

                host_census_take(address_of census);
                install = host_census_find(address_of census, name);
                return install ? host_take(install, false) : host_refuse("%s is no longer here\n", name);
        }

        installer_size(drive->bytes, (char address_to)size, sizeof(size));
        host_say(log, mode == INSTALLER_UPDATE
                          ? host_label "Updating %s (%s, %s, /dev/%s): its system and boot files are replaced, and its data and settings kept.\n"
                          : host_label "Overwriting %s (%s, %s, /dev/%s) erases everything on it.\n",
                 name, drive->model[0] ? (string_address)drive->model : (string_address) "a drive", size, name);
        if (interactive)
        {
                host_say(log, host_label "Type %s to go on: ", name);
                if (host_read_line(answer, sizeof(answer)) < 0 || !string_equals(answer, name))
                {
                        host_say(log, host_label "nothing written\n");
                        return 1;
                }
        }

        if (mode == INSTALLER_OVERWRITE)
        {
                host_install_beside = keeps;
                failed = host_install_disk(name, true, true);
                host_install_beside = false;
        }
        else
        {
                host_census census;
                host_install address_to install;

                host_census_take(address_of census);
                install = host_census_find(address_of census, name);
                failed = !install ? host_refuse("%s is no longer here\n", name) : keeps ? host_update(install) : host_take(install, true);
        }
        if (failed)
                return failed;
        host_say(log, host_label "syncing\n");
        system_call(syscall(sync));
        return host_install_verify(name);
}
