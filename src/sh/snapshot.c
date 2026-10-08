/*
        A reusable view of Linux's live system state.

        Utilities ask for sections and receive typed records in caller-owned,
        reusable storage.  procfs is parsed once here instead of independently
        by ps, dashboards and every future status tool.  The structures are
        deliberately the /dev/spark snapshot ABI: an accelerated kernel can
        fill them directly without changing a consumer.
*/

#define SYSTEM_NANOSECONDS 1000000000u
#define SYSTEM_USER_HZ 100u
#define SYSTEM_TICK_NS (SYSTEM_NANOSECONDS / SYSTEM_USER_HZ)

typedef struct
{
        struct snapshot_header header;
        struct snapshot_cpu address_to cpus;
        struct snapshot_network address_to networks;
        struct snapshot_process address_to processes;
        byte_store records;
        byte_store input;
} system_snapshot;

/* Linux's 64-bit sysinfo ABI. Keeping this private avoids importing libc's
   headers into the freestanding shell. */
typedef struct
{
        bipolar uptime;
        positive loads[3];
        positive totalram;
        positive freeram;
        positive sharedram;
        positive bufferram;
        positive totalswap;
        positive freeswap;
        p16 processes;
        p16 padding;
        positive totalhigh;
        positive freehigh;
        p32 memory_unit;
} system_information;

_Static_assert(sizeof(system_information) == 112, "Linux sysinfo ABI");

static HOT address_any system_snapshot_append(system_snapshot address_to sample,
                                              positive width)
{
        positive used = sample->records.used;

        if (width > SPARK_SNAPSHOT_MAX_BYTES - used ||
            !byte_store_reserve(address_of sample->records, used + width,
                                4096))
                return null;

        sample->records.used += width;
        return sample->records.bytes + used;
}

static CONST bool system_snapshot_range(unsigned int offset,
                                        unsigned int count, positive width,
                                        unsigned int used)
{
        return offset >= sizeof(struct snapshot_header) && !(offset & 7) &&
               offset <= used && (positive)count * width <= used - offset;
}

static HOT bool system_snapshot_accelerated(system_snapshot address_to sample,
                                            unsigned int flags)
{
        static bipolar device = -2;

        if (device == -2)
                device = system_open_at(AT_FDCWD, SPARK_DEVICE,
                                        FILE_READ | O_CLOEXEC);
        if (device < 0)
                return false;

        if (!sample->records.room &&
            !byte_store_reserve(address_of sample->records, 4096, 4096))
                return false;

        struct snapshot_request request = {
            .buffer = (unsigned long)sample->records.bytes,
            .capacity = (unsigned int)sample->records.room,
            .flags = flags,
            .version = SPARK_SNAPSHOT_VERSION,
        };
        bipolar answer = system_control(device, SPARK_IOCTL_SNAPSHOT,
                                        address_of request);

        if (answer == -28 && request.required > request.capacity &&
            request.required <= SPARK_SNAPSHOT_MAX_BYTES &&
            byte_store_reserve(address_of sample->records, request.required,
                               4096))
        {
                request.buffer = (unsigned long)sample->records.bytes;
                request.capacity = (unsigned int)sample->records.room;
                answer = system_control(device, SPARK_IOCTL_SNAPSHOT,
                                        address_of request);
        }

        if (answer < 0 || request.used < sizeof(struct snapshot_header) ||
            request.used > sample->records.room)
        {
                system_close(device);
                device = -1;
                return false;
        }

        struct snapshot_header address_to header =
            (struct snapshot_header address_to)sample->records.bytes;

        if (header->version != SPARK_SNAPSHOT_VERSION ||
            header->bytes != request.used || header->flags != flags ||
            header->page_size < 1024 ||
            ((flags & SPARK_SNAPSHOT_CPU) &&
             !system_snapshot_range(header->cpu_offset, header->cpu_count,
                                    sizeof(struct snapshot_cpu),
                                    request.used)) ||
            ((flags & SPARK_SNAPSHOT_NETWORK) &&
             !system_snapshot_range(header->network_offset,
                                    header->network_count,
                                    sizeof(struct snapshot_network),
                                    request.used)) ||
            /*      The process section too, though this kernel refuses to
                    produce one: the section pointers below are taken from
                    the header whichever section was asked for, so the one
                    offset that was not measured here was the one a device
                    answering more than this kernel does would be believed
                    about. Every section a caller can name is measured. */
            ((flags & SPARK_SNAPSHOT_PROCESS) &&
             !system_snapshot_range(header->process_offset,
                                    header->process_count,
                                    sizeof(struct snapshot_process),
                                    request.used)))
        {
                system_close(device);
                device = -1;
                return false;
        }

        sample->header = *header;
        sample->records.used = request.used;

        return true;
}

static CONST positive system_scaled(positive value, positive scale)
{
        return value > positive_max / scale ? positive_max : value * scale;
}

static CONST positive system_saturating_add(positive left, positive right)
{
        return left <= positive_max - right ? left + right : positive_max;
}

static fn system_process_path(p8 address_to path, p32 pid,
                              string_address directory, string_address name)
{
        positive at = 6;

        memory_copy_apart(path, "/proc/", at);
        at += positive_into(path + at, pid);
        path[at++] = '/';

        if (directory)
        {
                positive length = string_length(directory);

                memory_copy_apart(path + at, directory, length);
                at += length;
                path[at++] = '/';
        }

        string_copy(path + at, name);
}

static PURE string_address system_field_start(string_address at)
{
        return at + string_span_of_set(at, " \t");
}

#define SYSTEM_FIELD(name, type, parse)                                     \
        static type name(string_address address_to at)                      \
        {                                                                   \
                string_address here = system_field_start(address_to at);    \
                positive used = 0;                                          \
                type value = parse(here, address_of used);                  \
                                                                            \
                address_to at = here + used;                                \
                return value;                                               \
        }

SYSTEM_FIELD(system_field_unsigned, positive, string_digits)
SYSTEM_FIELD(system_field_signed, bipolar, string_bipolar)
#undef SYSTEM_FIELD

static fn system_fields_pass(string_address address_to at, positive count)
{
        string_address here = address_to at;

        while (count--)
        {
                here = system_field_start(here);
                here += string_span_without_set(here, " \t\n");
        }

        address_to at = here;
}

static positive system_clock_ns(b32 clock)
{
        p64 moment[2] = {0, 0};

        return system_call_2(syscall(clock_gettime), (positive)clock,
                             (positive)moment) < 0
                   ? 0
                   : moment[0] * SYSTEM_NANOSECONDS + moment[1];
}

static positive system_page_size()
{
        static positive page;

        if (!page)
        {
                p8 auxv[512];
                bipolar got = file_slurp_once_at(AT_FDCWD, "/proc/self/auxv",
                                                  auxv, sizeof(auxv));
                positive pair = 2 * sizeof(positive);

                if (got > 0)
                        for (positive at = 0; at + pair <= (positive)got;
                             at += pair)
                        {
                                positive type;
                                positive value;

                                memory_copy_apart(address_of type, auxv + at,
                                                  sizeof(positive));
                                memory_copy_apart(address_of value,
                                                  auxv + at + sizeof(positive),
                                                  sizeof(positive));

                                if (type == 6 && value >= 1024)
                                {
                                        page = value;
                                        break;
                                }
                        }

                if (!page)
                        page = 4096;
        }

        return page;
}

static HOT bool system_snapshot_system(system_snapshot address_to sample)
{
        system_information information;

        if (system_call_1(syscall(sysinfo),
                          (positive)address_of information) < 0)
                return false;

        for (positive i = 0; i < 3; i++)
        {
                positive whole = information.loads[i] >> 16;
                positive fraction = information.loads[i] & 0xffff;

                sample->header.load[i] = (unsigned int)(
                    system_scaled(whole, 100) +
                    (fraction * 100 + 0x8000) / 0x10000);
        }

        positive unit = information.memory_unit ? information.memory_unit : 1;

        sample->header.memory_total = system_scaled(information.totalram, unit);
        sample->header.memory_available = system_scaled(information.freeram,
                                                         unit);
        sample->header.swap_total = system_scaled(information.totalswap, unit);
        sample->header.swap_free = system_scaled(information.freeswap, unit);

        if (!file_store_slurp("/proc/meminfo", address_of sample->input))
                return false;

        string_address at = sample->input.bytes;
        while (string_get(at))
        {
                string_address next = string_first_of_or_end(at, '\n');

                if (!string_compare_max(at, "MemAvailable:", 13))
                {
                        string_address value_at = string_first_of_or_end(at, ':');

                        if (string_get(value_at))
                        {
                                value_at++;
                                sample->header.memory_available = system_scaled(
                                    system_field_unsigned(address_of value_at),
                                    1024);
                        }
                        break;
                }

                at = string_get(next) ? next + 1 : next;
        }

        return true;
}

static HOT bool system_snapshot_cpu(system_snapshot address_to sample)
{
        if (!file_store_slurp("/proc/stat", address_of sample->input))
                return false;

        sample->header.cpu_offset = (unsigned int)sample->records.used;
        string_address line = sample->input.bytes;

        while (line[0] == 'c' && line[1] == 'p' && line[2] == 'u' &&
               (line[3] == ' ' || byte_is_digit(line[3])))
        {
                struct snapshot_cpu address_to cpu =
                    system_snapshot_append(sample,
                                           sizeof(struct snapshot_cpu));

                if (!cpu)
                        return false;

                string_address at = line + 3;
                positive total = 0;
                positive idle = 0;

                sample->header.cpu_count++;
                memory_fill(cpu, 0, sizeof(*cpu));
                cpu->id = line[3] == ' '
                              ? ~0u
                              : (unsigned int)system_field_unsigned(address_of at);

                /* /proc/stat's guest fields are already in user/nice. */
                for (positive field = 0; field < 8; field++)
                {
                        positive value = system_field_unsigned(address_of at);

                        total = system_saturating_add(total, value);

                        if (field == 3 || field == 4)
                                idle = system_saturating_add(idle, value);
                }

                cpu->total_ns = system_scaled(total, SYSTEM_TICK_NS);
                cpu->idle_ns = system_scaled(idle, SYSTEM_TICK_NS);
                line = string_first_of_or_end(line, '\n');
                line += string_get(line) != end;
        }

        return sample->header.cpu_count != 0;
}

static HOT bool system_snapshot_network(system_snapshot address_to sample)
{
        if (!file_store_slurp("/proc/net/dev", address_of sample->input))
                return false;

        sample->header.network_offset = (unsigned int)sample->records.used;
        string_address line = sample->input.bytes;

        while (string_get(line))
        {
                string_address next = string_first_of_or_end(line, '\n');
                string_address colon = (string_address)memory_first_of(
                    line, ':', (positive)(next - line));

                if (colon)
                {
                        string_address name = line;
                        string_address name_end = colon;

                        name += string_span_max(name, name_end - name, string_set_blanks);
                        while (name_end > name &&
                               (name_end[-1] == ' ' || name_end[-1] == '\t'))
                                name_end--;

                        positive length = (positive)(name_end - name);

                        if (length && length < 16)
                        {
                                struct snapshot_network address_to network =
                                    system_snapshot_append(
                                        sample,
                                        sizeof(struct snapshot_network));

                                if (!network)
                                        return false;

                                string_address at = colon + 1;

                                sample->header.network_count++;
                                memory_fill(network, 0, sizeof(*network));
                                memory_copy_apart(network->name, name, length);
                                network->received =
                                    system_field_unsigned(address_of at);
                                system_fields_pass(address_of at, 7);
                                network->transmitted =
                                    system_field_unsigned(address_of at);
                        }
                }

                line = string_get(next) ? next + 1 : next;
        }

        return true;
}

static HOT bool system_process_parse(
    string_address text, positive page_size,
    struct snapshot_process address_to process)
{
        string_address open = string_first_of_or_end(text, '(');
        string_address close = string_last_of(text, ')');

        /* The state is one character between spaces. Check its prefix in
           order so a truncated record never advances beyond its sentinel. */
        if (!string_get(open) || !close || close <= open ||
            close[1] != ' ' || close[2] <= ' ' || close[3] != ' ')
                return false;

        memory_fill(process, 0, sizeof(*process));
        process->pid = (unsigned int)system_field_unsigned(address_of text);

        positive command = (positive)(close - open - 1);
        if (command > sizeof(process->command) - 1)
                command = sizeof(process->command) - 1;
        memory_copy_apart(process->command, open + 1, command);

        string_address at = close + 2;
        process->state = (unsigned int)string_get(at);
        at += string_get(at) != end;
        process->ppid = (unsigned int)system_field_unsigned(address_of at);
        process->pgrp = (unsigned int)system_field_unsigned(address_of at);
        process->session = (unsigned int)system_field_unsigned(address_of at);
        process->tty = (int)system_field_signed(address_of at);
        process->tpgid = (int)system_field_signed(address_of at);
        system_fields_pass(address_of at, 5);
        process->user_ns = system_scaled(system_field_unsigned(address_of at),
                                         SYSTEM_TICK_NS);
        process->system_ns = system_scaled(system_field_unsigned(address_of at),
                                           SYSTEM_TICK_NS);
        system_fields_pass(address_of at, 3);
        process->nice = (int)system_field_signed(address_of at);
        process->threads =
            (unsigned int)system_field_unsigned(address_of at);
        system_fields_pass(address_of at, 1);
        process->start_ns = system_scaled(system_field_unsigned(address_of at),
                                          SYSTEM_TICK_NS);
        process->virtual_bytes = system_field_unsigned(address_of at);
        process->resident_bytes = system_scaled(
            system_field_unsigned(address_of at), page_size);
        return process->pid != 0;
}

static HOT bool system_snapshot_processes(system_snapshot address_to sample,
                                          bool owners,
                                          positive address_to selected,
                                          positive selected_count)
{
        file_walk walk;

        if (!file_walk_open(address_of walk, AT_FDCWD, "/proc"))
                return false;
        scope_exit(file_walk_close(address_of walk));

        sample->header.process_offset = (unsigned int)sample->records.used;
        struct linux_dirent64 address_to entry;

        /* proc_pid_readdir advances through TGIDs in numeric order; namespace
           and visibility filtering only skip entries, so consumers can merge
           consecutive snapshots directly without sorting them again. */
        while ((entry = file_walk_next(address_of walk)))
        {
                if (!byte_is_digit(entry->d_name[0]))
                        continue;

                /* Keep procfs' numeric ordering, but do not open unrelated
                   stat/status files for an explicit PID-only selection. */
                if (selected_count)
                {
                        positive pid = string_digits(entry->d_name, null);
                        positive at = 0;
                        while (at < selected_count && selected[at] != pid)
                                at++;
                        if (at == selected_count)
                                continue;
                }

                p8 path[64];
                p8 block[8192];
                path_join(path, sizeof(path), entry->d_name, "stat");

                if (file_slurp_once_at(walk.handle, path, block,
                                       sizeof(block)) <= 0)
                        continue;

                struct snapshot_process address_to process =
                    system_snapshot_append(sample,
                                           sizeof(struct snapshot_process));

                if (!process)
                        return false;

                if (system_process_parse(block, sample->header.page_size,
                                         process))
                {
                        if (owners)
                        {
                                path_join(path, sizeof(path), entry->d_name,
                                          "status");

                                if (file_slurp_once_at(walk.handle, path, block,
                                                       sizeof(block)) > 0)
                                {
                                        string_address uid = string_search(
                                            block, "\nUid:");

                                        if (uid)
                                        {
                                                uid += 5;
                                                process->uid = (unsigned int)
                                                    system_field_unsigned(
                                                        address_of uid);
                                        }
                                }
                        }

                        sample->header.process_count++;
                }
                else
                        sample->records.used -= sizeof(*process);
        }

        return !walk.error;
}

static HOT bool system_snapshot_take_selected(
    system_snapshot address_to sample, unsigned int flags, bool process_owners,
    positive address_to selected, positive selected_count)
{
        unsigned int kernel_flags = flags & SPARK_SNAPSHOT_KERNEL;
        bool accelerated = system_snapshot_accelerated(sample, kernel_flags);

        if (!accelerated)
        {
                /* The header prefix is reserved space, not live bytes until
                   the backing store exists. In particular the first /proc
                   sample must not ask reserve to preserve bytes from a null
                   mapping with zero capacity. */
                sample->records.used = 0;
                if (!byte_store_reserve(address_of sample->records,
                                        sizeof(struct snapshot_header), 4096))
                        return false;
                sample->records.used = sizeof(struct snapshot_header);
                memory_fill(address_of sample->header, 0,
                            sizeof(sample->header));
                sample->header.version = SPARK_SNAPSHOT_VERSION;
                sample->header.page_size = (unsigned int)system_page_size();
                sample->header.monotonic_ns = system_clock_ns(1);
                sample->header.realtime_seconds = system_clock_ns(0) /
                                                   SYSTEM_NANOSECONDS;
                sample->header.uptime_ns = system_clock_ns(7);
        }

        if (!accelerated && (flags & SPARK_SNAPSHOT_SYSTEM) &&
            !system_snapshot_system(sample))
                return false;
        if (!accelerated && (flags & SPARK_SNAPSHOT_CPU) &&
            !system_snapshot_cpu(sample))
                return false;
        if (!accelerated && (flags & SPARK_SNAPSHOT_NETWORK) &&
            !system_snapshot_network(sample))
                return false;
        if ((flags & SPARK_SNAPSHOT_PROCESS) &&
            !system_snapshot_processes(sample, process_owners, selected,
                                       selected_count))
                return false;

        sample->header.flags = flags;
        sample->header.bytes = (unsigned int)sample->records.used;
        if (flags & SPARK_SNAPSHOT_CPU)
                sample->cpus = (struct snapshot_cpu address_to)
                    (sample->records.bytes + sample->header.cpu_offset);
        if (flags & SPARK_SNAPSHOT_NETWORK)
                sample->networks = (struct snapshot_network address_to)
                    (sample->records.bytes + sample->header.network_offset);
        if (flags & SPARK_SNAPSHOT_PROCESS)
                sample->processes = (struct snapshot_process address_to)
                    (sample->records.bytes + sample->header.process_offset);
        return true;
}

static HOT bool system_snapshot_take(system_snapshot address_to sample,
                                     unsigned int flags, bool process_owners)
{
        return system_snapshot_take_selected(sample, flags, process_owners,
                                              null, 0);
}
