/*
        src/sh/host/memory.c: the verb memory.

                moonwater memory
                moonwater memory log [SECONDS]

        Where the machine's memory is, in one line: what is installed (the
        memory modules' own records, SMBIOS type 17), what Linux was given of
        it (MemTotal: installed less what the firmware, the graphics' share of
        a shared-memory GPU and the kernel's page bookkeeping hold), what is
        available, what is shared memory (Shmem: tmpfs and every memfd), and
        the three largest memfds by the memory they hold, with the program
        that holds each. A memfd's size is what it may grow to; what it holds
        is its allocated blocks, so a sparse table of terabytes is said by the
        pages it has.

        log looks again every SECONDS (sixty by default) and writes the line
        to the kernel log when it changed, and once an hour, until it is stopped: the machine script
        starts it at boot, so a boot that runs out of memory leaves what used
        it in the log (the window table faulted in whole, 7db1fea3f, was found
        from the OOM killer's lines alone).

        Root only: the module records and the descriptors of other programs
        are root's.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#define MEMORY_TOP 3
#define MEMORY_SEEN 4096 // memfds told apart (by device and inode) at most

struct memory_fd
{
        p64 device, inode, bytes;
        p8 name[48];
        p8 owner[20];
};

static struct
{
        struct memory_fd top[MEMORY_TOP];
        p64 seen[MEMORY_SEEN][2];
        positive seen_count;
        p8 owner[20];
        p8 path[64];
} census;

//      A /proc/meminfo field, in bytes: 0 when it is not there.
static p64 memory_meminfo(string_address text, string_address field)
{
        positive length = string_length(field);

        for (string_address at = text; *at; at++)
        {
                positive used = 0;
                p64 kib;

                if ((at != text && at[-1] != '\n') || string_compare_max(at, field, length))
                        continue;
                at += length;
                while (*at == ' ')
                        at++;
                kib = string_digits_max(at, 19, address_of used);
                return used ? kib * 1024 : 0;
        }
        return 0;
}

//      The memory modules installed, from their SMBIOS records (type 17): the size word at 12, in MiB, or in KiB with its top bit,
//      and 0x7fff for "see the extended size", the word at 28 in MiB. 0 is an empty slot, 0xffff a size not known.
static bool memory_module(string_address directory, string_address name, address_any context)
{
        p64 address_to total = context;
        p8 path[96];
        p8 raw[32];
        bipolar handle, got;
        p16 size;

        (void)directory;
        if (name[0] != '1' || name[1] != '7' || name[2] != '-')
                return true;
        string_copy_bounded(path, "/sys/firmware/dmi/entries/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        string_append_bounded(path, "/raw", sizeof(path));
        handle = system_open_at(AT_FDCWD, path, O_RDONLY | O_CLOEXEC);
        if (handle < 0)
                return true;
        got = system_read_retry(handle, raw, sizeof(raw));
        system_close(handle);
        if (got < 14)
                return true;
        size = (p16)(raw[12] | raw[13] << 8);
        if (!size || size == 0xffff)
                return true;
        if (size == 0x7fff && got >= 32)
                *total += (p64)(raw[28] | raw[29] << 8 | raw[30] << 16 | (p32)(raw[31] & 0x7f) << 24) << 20;
        else if (size & 0x8000)
                *total += (p64)(size & 0x7fff) << 10;
        else
                *total += (p64)size << 20;
        return true;
}

//      One descriptor of a process: a memfd is kept among the largest, once however many descriptors and processes share it.
static bool memory_descriptor(string_address directory, string_address name, address_any context)
{
        p8 path[96];
        p8 link[64];
        file_status status;
        bipolar length;
        p64 bytes;
        positive at;

        (void)context;
        string_copy_bounded(path, directory, sizeof(path));
        string_append_bounded(path, "/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        length = system_read_link_at(AT_FDCWD, path, link, sizeof(link) - 1);
        if (length < 7)
                return true;
        link[length] = end;
        if (string_compare_max(link, "/memfd:", 7) || system_status_at(AT_FDCWD, path, address_of status, 0) < 0)
                return true;

        for (at = 0; at < census.seen_count; at++)
                if (census.seen[at][0] == status.device && census.seen[at][1] == status.inode)
                        return true;
        if (census.seen_count < MEMORY_SEEN)
        {
                census.seen[census.seen_count][0] = status.device;
                census.seen[census.seen_count++][1] = status.inode;
        }

        bytes = (p64)status.blocks * 512;
        for (at = 0; at < MEMORY_TOP && census.top[at].bytes >= bytes; at++)
                ;
        if (at == MEMORY_TOP || !bytes)
                return true;
        for (positive move = MEMORY_TOP - 1; move > at; move--)
                census.top[move] = census.top[move - 1];
        census.top[at].device = status.device;
        census.top[at].inode = status.inode;
        census.top[at].bytes = bytes;
        string_copy_bounded(census.top[at].name, link + 7, sizeof(census.top[at].name));
        //      " (deleted)" is how every memfd's link ends: not part of its name.
        length = (bipolar)string_length(census.top[at].name);
        if (length > 10 && !string_compare_max(census.top[at].name + length - 10, " (deleted)", 10))
                census.top[at].name[length - 10] = end;
        string_copy_bounded(census.top[at].owner, census.owner, sizeof(census.top[at].owner));
        return true;
}

static bool memory_process(string_address directory, string_address name, address_any context)
{
        p8 path[48];
        positive length;

        (void)directory;
        if (!byte_is_digit(name[0]))
                return true;
        string_copy_bounded(path, "/proc/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        length = string_length(path);
        string_append_bounded(path, "/comm", sizeof(path));
        if (host_read_kernel((string_address)path, census.owner, sizeof(census.owner)) < 0)
                return true;
        path[length] = end;
        string_append_bounded(path, "/fd", sizeof(path));
        host_each_entry((string_address)path, memory_descriptor, context);
        return true;
}

//      Bytes as people read them: "60.5 GiB", "212 MiB", "4 KiB".
static fn memory_amount(p8 address_to into, positive room, p64 bytes)
{
        p8 number[24];
        string_address unit = " KiB";
        p64 scale = 1024;

        if (bytes >= (p64)1 << 30)
                scale = (p64)1 << 30, unit = " GiB";
        else if (bytes >= (p64)1 << 20)
                scale = (p64)1 << 20, unit = " MiB";
        positive_into_string(number, (positive)(bytes / scale));
        string_append_bounded(into, number, room);
        if (scale == (p64)1 << 30 && bytes / scale < 100)
        {
                p8 tenth[2] = {(p8)('0' + bytes % scale * 10 / scale), end};

                string_append_bounded(into, ".", room);
                string_append_bounded(into, tenth, room);
        }
        string_append_bounded(into, unit, room);
}

static fn memory_line(p8 address_to line, positive room)
{
        p8 text[4096];
        p64 installed = 0, total, available, shared;

        memory_zero(&census, sizeof(census));
        if (file_slurp_once_at(AT_FDCWD, "/proc/meminfo", text, sizeof(text) - 1) <= 0)
                text[0] = end;
        else
                text[sizeof(text) - 1] = end;
        total = memory_meminfo((string_address)text, "MemTotal:");
        available = memory_meminfo((string_address)text, "MemAvailable:");
        shared = memory_meminfo((string_address)text, "Shmem:");
        host_each_entry("/sys/firmware/dmi/entries", memory_module, address_of installed);
        host_each_entry("/proc", memory_process, null);

        line[0] = end;
        string_append_bounded(line, "memory: ", room);
        if (installed > total)
        {
                memory_amount(line, room, installed);
                string_append_bounded(line, " installed, ", room);
                memory_amount(line, room, installed - total);
                string_append_bounded(line, " held by firmware, graphics and the kernel, ", room);
        }
        memory_amount(line, room, total);
        string_append_bounded(line, " usable, ", room);
        memory_amount(line, room, available);
        string_append_bounded(line, " available, ", room);
        memory_amount(line, room, shared);
        string_append_bounded(line, " shared", room);
        for (positive at = 0; at < MEMORY_TOP && census.top[at].bytes; at++)
        {
                string_append_bounded(line, at ? ", " : "; largest memfds: ", room);
                string_append_bounded(line, census.top[at].name, room);
                string_append_bounded(line, " (", room);
                string_append_bounded(line, census.top[at].owner, room);
                string_append_bounded(line, ") ", room);
                memory_amount(line, room, census.top[at].bytes);
        }
}

static b32 host_memory(string_address address_to arguments, positive count)
{
        p8 line[300];
        positive seconds = 60, used = 0;

        return_if(count > 4 || (count >= 3 && !string_equals(arguments[2], "log")), host_usage());
        host_need_root();
        if (count == 4)
        {
                seconds = string_digits_max(arguments[3], 5, address_of used);
                if (!used || arguments[3][used] || seconds < 1)
                        return host_usage();
        }
        if (count == 2)
        {
                memory_line(line, sizeof(line));
                host_say(log, "%s\n", (string_address)line);
                return 0;
        }
        //      Looked at every SECONDS, written when it says something the last one written did not, and once an hour
        //      whatever it says: memory running out changes the line, and the log keeps the trail of it; where the same
        //      line went to the kernel's log every minute of a quiet machine (box: an hour of identical lines, the rest of
        //      the log pushed out of its ring by them).
        {
                p8 last[sizeof(line)];
                positive quiet = 0;

                last[0] = end;
                for (;;)
                {
                        string_address parts[] = {(string_address)line, null};

                        memory_line(line, sizeof(line));
                        if (!string_equals(line, last) || (quiet += seconds) >= 3600)
                        {
                                host_kmsg(parts);
                                string_copy_bounded(last, line, sizeof(last));
                                quiet = 0;
                        }
                        host_pause((p64)seconds * 1000000000);
                }
        }
}
