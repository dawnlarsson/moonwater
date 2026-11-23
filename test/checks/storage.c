/*
        test/checks/storage.c: storage's I/O and its formats.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_storage_io
#include "../../src/lib.util.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/* Inject only positional reads. Everything else reaches the real syscall
   ABI, so stdbuf can open/read/close a real ELF-header fixture while its
   out-of-line program table receives deterministic short reads and EINTR. */
static positive storage_test_mode, storage_test_calls, storage_test_number;
static positive storage_test_offset, storage_test_length, storage_test_used;
static p8 storage_test_arguments, storage_test_preclear;
static p8 address_to storage_test_destination;
static positive storage_test_net_syncs, storage_test_net_sync_failure;
static bipolar storage_test_net_close_failure = -1;

typedef struct { p8 send; bipolar result; positive ask; } copy_test_step;
static const copy_test_step address_to copy_test_steps;
static positive copy_test_count, copy_test_at, copy_test_used, copy_test_seeks;
static positive copy_test_seek_failure;
static p8 copy_test_active, copy_test_explicit;

static bipolar copy_test_transfer(p8 send, positive in, positive out,
                                  positive source, positive destination,
                                  positive ask)
{
        check("copy cascade syscall order", copy_test_at < copy_test_count);
        if (copy_test_at >= copy_test_count)
                return -5; // EIO, before the utility errno names are included.
        const copy_test_step address_to step = copy_test_steps + copy_test_at++;
        check("copy cascade handles and remaining count",
              send == step->send && in == 12345 && out == 54321 && ask == step->ask);
        check("copy cascade pointer policy",
              !!source == copy_test_explicit && !!destination == (copy_test_explicit && !send));
        if (source)
                check("copy cascade input offset", *(p64 address_to)source == 7 + copy_test_used);
        if (destination)
                check("copy cascade output offset", *(p64 address_to)destination == 13 + copy_test_used);
        if (step->result > 0)
        {
                copy_test_used += (positive)step->result;
                if (source) *(p64 address_to)source += step->result;
                if (destination) *(p64 address_to)destination += step->result;
        }
        return step->result;
}

static bipolar storage_test_call6(positive number, positive in,
    positive source, positive out, positive destination, positive ask, positive flags)
{
        if (!copy_test_active || number != syscall(copy_file_range))
                return (system_call_6)(number, in, source, out, destination, ask, flags);
        check("copy cascade range flags", !flags);
        return copy_test_transfer(false, in, out, source, destination, ask);
}

static bipolar storage_test_call4(positive number, positive handle,
                                  positive into, positive length,
                                  positive offset)
{
        if (copy_test_active && number == syscall(sendfile))
                return copy_test_transfer(true, into, handle, length, 0, offset);
        if (number != storage_test_number || !storage_test_mode)
                return (system_call_4)(number, handle, into, length, offset);

        p8 address_to bytes = (p8 address_to)into;
        if (!storage_test_calls && storage_test_mode != 5)
                for (positive i = 0; i < storage_test_length; i++)
                        if (storage_test_destination[i] != 0xa5)
                                storage_test_preclear = true;

        if (offset != storage_test_offset + storage_test_used ||
            length != storage_test_length - storage_test_used ||
            (storage_test_destination &&
             bytes != storage_test_destination + storage_test_used))
                storage_test_arguments = false;

        storage_test_calls++;
        if (storage_test_calls == 1 &&
            (storage_test_mode == 1 || storage_test_mode == 5))
                return -4;
        if (storage_test_mode == 3 ||
            (storage_test_mode == 4 && storage_test_used == 7))
                return -5;
        if (storage_test_mode == 2 && storage_test_used == 7)
                return 0;

        positive take = min(length, (positive)7);
        if (number == syscall(pread64))
                for (positive i = 0; i < take; i++)
                        bytes[i] = storage_test_mode == 5
                                       ? (storage_test_used + i == 0 ? 3 : 0)
                                       : (p8)(storage_test_used + i + 1);
        storage_test_used += take;
        return (bipolar)take;
}

static p8 directory_test_active;
static positive directory_test_calls, directory_test_capacity;
static bipolar directory_test_result;

static bipolar storage_test_call3(positive number, positive one,
                                  positive two, positive three)
{
        if (copy_test_active && number == syscall(lseek))
        {
                copy_test_seeks++;
                check("copy cascade seek handle and position",
                      (one == 12345 || one == 54321) && three == 0 &&
                      two == (one == 12345 ? 7 : 13) + copy_test_used);
                return copy_test_seeks == copy_test_seek_failure ? -22 : (bipolar)two;
        }
        if (!directory_test_active || number != syscall(getdents64))
                return (system_call_3)(number, one, two, three);
        directory_test_calls++;
        check("directory refill retains handle and capacity",
              one == 12345 && three == directory_test_capacity);
        if (directory_test_result <= 0)
                return directory_test_result;
        p8 address_to block = (p8 address_to)two;
        memory_zero(block, 56);
        ((struct linux_dirent64 address_to)block)->d_reclen = 24;
        ((struct linux_dirent64 address_to)(block + 24))->d_reclen = 32;
        return 56;
}

static bipolar storage_test_call1(positive number, positive one)
{
        if (number == syscall(fsync) && storage_test_net_sync_failure)
        {
                storage_test_net_syncs++;
                if (storage_test_net_syncs == storage_test_net_sync_failure)
                        return -5;
        }

        bipolar result = (system_call_1)(number, one);
        if (number == syscall(close) &&
            (bipolar)one == storage_test_net_close_failure)
        {
                storage_test_net_close_failure = -1;
                return result < 0 ? result : -5;
        }
        return result;
}

#define system_call_1(...) storage_test_call1(__VA_ARGS__)
#define system_call_3(...) storage_test_call3(__VA_ARGS__)
#define system_call_4(...) storage_test_call4(__VA_ARGS__)
#define system_call_6(...) storage_test_call6(__VA_ARGS__)
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#undef system_call_1
#undef system_call_4
#undef system_call_6
#undef system_call_3

static fn storage_test_directory(void)
{
        p8 guarded[4112] __attribute__((aligned(8)));
        for (positive capacity = 2048; capacity <= 4096; capacity *= 2)
                for (bipolar finish = 0; finish >= -5; finish--)
                {
                        memory_fill(guarded, 0xa5, sizeof(guarded));
                        positive have = 0, at = 0;
                        bipolar error = 0;
                        directory_test_calls = 0;
                        directory_test_capacity = capacity;
                        directory_test_result = 56;
                        directory_test_active = true;
                        for (positive record = 0; record < 4; record++)
                        {
                                struct linux_dirent64 address_to entry =
                                    file_directory_next(12345, guarded + 8, capacity,
                                                        address_of have, address_of at,
                                                        address_of error);
                                check("directory record and refill boundary",
                                      (p8 address_to)entry == guarded + 8 + (record % 2 ? 24 : 0) &&
                                      at == (record % 2 ? 56 : 24) && have == 56 && !error);
                                check("resident records do not issue another syscall",
                                      directory_test_calls == record / 2 + 1);
                        }
                        directory_test_result = finish;
                        check("directory EOF/error stops without implicit retry",
                              !file_directory_next(12345, guarded + 8, capacity,
                                                   address_of have, address_of at,
                                                   address_of error) &&
                              directory_test_calls == 3 && error == finish);
                        directory_test_active = false;
                        check("directory refill preserves guards", guarded[7] == 0xa5 &&
                              guarded[capacity + 8] == 0xa5);
                }
}

static fn storage_test_begin(positive mode, positive length, positive offset,
                             p8 address_to bytes)
{
        storage_test_mode = mode;
        storage_test_number = syscall(pread64);
        storage_test_calls = storage_test_used = 0;
        storage_test_offset = offset;
        storage_test_length = length;
        storage_test_arguments = true;
        storage_test_preclear = false;
        storage_test_destination = bytes;
}

static fn storage_test_read(positive mode, positive wanted)
{
        p8 guarded[34];
        memory_fill(guarded, 0xa5, sizeof guarded);
        storage_test_begin(mode, 32, 123, guarded + 1);
        positive got = storage_read(12345, guarded + 1, 32, 123);
        storage_test_mode = 0;

        check("positional read count", got == wanted);
        check("positional short reads advance pointer and offset",
              storage_test_arguments);
        check("destination guards", guarded[0] == 0xa5 && guarded[33] == 0xa5);
        check("full probe is not cleared before read", !storage_test_preclear);
        for (positive i = 0; i < 32; i++)
                check("read bytes survive and unread tail is zero",
                      guarded[i + 1] == (i < got ? (p8)(i + 1) : 0));
}

static fn storage_test_exact(void)
{
        for (positive write = 0; write < 2; write++)
                for (positive mode = 1; mode <= 4; mode++)
                        for (positive length = 0; length <= 32; length += 32)
                        {
                                p8 guarded[34];
                                memory_fill(guarded, 0xa5, sizeof guarded);
                                positive offset = ((positive)1 << 32) + 123;
                                storage_test_begin(mode, length, offset, guarded + 1);
                                storage_test_number = write ? syscall(pwrite64) : syscall(pread64);
                                bipolar got = write
                                    ? storage_write(12345, guarded + 1, length, offset)
                                    : file_transfer_exact(syscall(pread64), 12345,
                                                          guarded + 1, length, offset);
                                storage_test_mode = 0;
                                check("exact positional return", got ==
                                      (!length || mode == 1 ? (bipolar)length : -5));
                                check("exact positional offset/pointer/length", storage_test_arguments);
                                check("exact positional zero does not call kernel", length || !storage_test_calls);
                                check("exact positional guards", guarded[0] == 0xa5 && guarded[33] == 0xa5);
                                for (positive i = 0; i < 32; i++)
                                        check("exact positional bytes are not cleared", guarded[i + 1] ==
                                              (!write && i < storage_test_used ? (p8)(i + 1) : 0xa5));
                        }
}

static fn storage_test_replay(void)
{
        bipolar handle = system_call_2(syscall(memfd_create),
                                        (positive)"replay-edge-test", 0);
        check("replay fixture descriptor", handle >= 0);
        if (handle < 0)
                return;
        /*
                The header the reader will carry, and one byte past it. The
                bound moved from 64 kB to a megabyte when the reader was
                made to drop the transcript's opening line whatever it holds
                -- the reference reads that line with getline and does not
                care how long it is -- so what is checked here is the bound
                that exists, with a case either side of it. The differential
                never compiles this file, so the domain stayed green for a
                whole pass while this check had stopped agreeing with the
                reader it tests.
        */
        #define REPLAY_HEADER_MAX ((positive)1 << 20)
        static p8 bytes[REPLAY_HEADER_MAX + 2048];
        static const positive headers[] = {18, 4094, 4095, 4096, 65534, 65535, 65536,
                                           REPLAY_HEADER_MAX - 1, REPLAY_HEADER_MAX};
        static const positive widths[] = {0, 1, 1022, 1023, 1024};
        for (positive h = 0; h < array_count(headers); h++)
                for (positive w = 0; w < array_count(widths); w++)
                        for (positive ending = 0; ending < 3; ending++)
                        {
                                positive header = headers[h], width = widths[w];
                                memory_fill(bytes, 'h', header);
                                memory_copy(bytes, "Script started on ", 18);
                                bytes[header] = '\n';
                                memory_fill(bytes + header + 1, 'q', width);
                                positive size = header + 1 + width;
                                if (ending == 2) bytes[size++] = '\r';
                                if (ending) bytes[size++] = '\n';
                                p8 prepared = system_seek(handle, 0, FILE_SEEK_SET) == 0 &&
                                    system_truncate_handle(handle, size) == 0 &&
                                    system_write_all(handle, bytes, size) == size &&
                                    system_seek(handle, 0, FILE_SEEK_SET) == 0;
                                check("replay fixture prepared", prepared);
                                if (!prepared) continue;
                                process_replay_reader reader = {.failed = false};
                                byte_input_open_fd(address_of reader.from, handle, reader.bytes, sizeof(reader.bytes));
                                p8 accepted = process_replay_skip_header(address_of reader);
                                check("replay header bound",
                                      accepted == (header < REPLAY_HEADER_MAX));
                                if (!accepted) continue;
                                p8 line[1026];
                                memory_fill(line, 0xa5, sizeof(line));
                                bipolar got = process_replay_line(address_of reader, line + 1, 1024);
                                p8 fits = width + (ending == 2) < 1024;
                                bipolar expected = fits ? (width || ending ? (bipolar)width + 1 : 0) : -1;
                                check("replay line bound/CRLF/EOF", got == expected && reader.failed == !fits);
                                check("replay line output guards", line[0] == 0xa5 && line[1025] == 0xa5);
                                if (got > 0)
                                        check("replay line bytes and terminator", line[width + 1] == 0 &&
                                              memory_span_byte(line + 1, 'q', width) == width);
                        }
        system_close(handle);
}

static fn utility_test_decimal(void)
{
        static const string_address cases[] = {"", "+1", "-1", " 1", "0", "7", "0042", "42tail",
            "18446744073709551614", "18446744073709551615", "18446744073709551616",
            "18446744073709551616tail", "9999999999999999999999999999999999999999999999"};
        for (positive i = 0; i < array_count(cases); i++)
                for (positive saturate = 0; saturate < 2; saturate++)
                {
                        string_address text = cases[i], at = text;
                        positive expected = 0, digits = 0;
                        p8 overflow = false;
                        while (text[digits] >= '0' && text[digits] <= '9')
                        {
                                positive digit = text[digits++] - '0';
                                if (expected > (positive_max - digit) / 10)
                                        overflow = true;
                                expected = overflow ? positive_max : expected * 10 + digit;
                        }
                        positive value = 123;
                        p8 good = file_decimal_read(address_of at, saturate, address_of value);
                        p8 want = digits && (saturate || !overflow);
                        check("decimal checked/saturated answer", good == want);
                        check("decimal cursor and output rollback", want
                              ? at == text + digits && value == expected
                              : at == text && value == 123);
                }
}

static fn storage_test_elf(void)
{
        bipolar handle = system_call_2(syscall(memfd_create),
                                        (positive)"storage-elf-test", 0);
        check("ELF fixture descriptor", handle >= 0);
        if (handle < 0)
                return;

        p8 head[64] = {0x7f, 'E', 'L', 'F', 2, 1};
        head[16] = 2;
        positive machine = stdbuf_elf_machine();
        head[18] = (p8)machine;
        head[19] = (p8)(machine >> 8);
        head[33] = 32; /* e_phoff = 8192, beyond the initial read. */
        head[54] = 56;
        head[56] = 1;
        check("ELF fixture written", system_write_all((positive)handle,
              head, sizeof head) == sizeof head);

        p8 path[64];
        memory_copy_apart(path, "/proc/self/fd/", 14);
        positive_into_string(path + 14, (positive)handle);
        storage_test_begin(5, 56, 8192, null);
        b32 kind = stdbuf_target_kind(path);
        storage_test_mode = 0;
        system_close((positive)handle);
        check("stdbuf retries interrupted and short ELF table reads",
              kind == STDBUF_ELF_DYNAMIC && storage_test_used == 56 &&
              storage_test_calls == 9 && storage_test_arguments);
}

static fn storage_test_copy(void)
{
        bipolar in = system_call_2(syscall(memfd_create), (positive)"copy-in", 0);
        bipolar out = system_call_2(syscall(memfd_create), (positive)"copy-out", 0);
        check("copy fixture descriptors", in >= 0 && out >= 0);
        if (in < 0 || out < 0)
                goto done;

        p8 bytes[257], copied[257];
        for (positive i = 0; i < sizeof(bytes); i++)
                bytes[i] = (p8)(i * 197 + 1);
        check("copy fixture offset", system_seek(in, 8192, FILE_SEEK_SET) == 8192);
        check("copy fixture data", system_write_all(in, bytes, sizeof(bytes)) == sizeof(bytes));

        // Start at unrelated descriptor positions. Each capability setting
        // must copy the explicit extent, preserving the hole before it.
        for (positive mode = 0; mode < 3; mode++)
        {
                p8 range = mode == 0, send = mode < 2;
                check("copy output reset", system_truncate_handle(out, 0) == 0);
                check("copy input position", system_seek(in, 5, FILE_SEEK_SET) == 5);
                check("copy output position", system_seek(out, 7, FILE_SEEK_SET) == 7);
                check("extent through range/sendfile/buffer", file_copy_extent(
                    in, out, 8192, sizeof(bytes), address_of range, address_of send));
                check("copied extent bytes", system_call_4(syscall(pread64), out,
                    (positive)copied, sizeof(copied), 8192) == sizeof(copied) &&
                    !memory_compare(bytes, copied, sizeof(bytes)));
                check("copy preserves preceding hole", system_call_4(
                    syscall(pread64), out, (positive)copied, 1, 0) == 1 && !copied[0]);
                check("bounded copy refuses premature EOF", !file_copy_extent(
                    in, out, 8192 + sizeof(bytes), 1, address_of range, address_of send));
        }
done:
        if (in >= 0) system_close(in);
        if (out >= 0) system_close(out);
}

static fn storage_test_copy_faults(void)
{
        static const copy_test_step partial[] = {
            {false, -4, 12}, {false, 5, 12}, {false, -95, 7},
            {true, -4, 7}, {true, 4, 7}, {true, 3, 3},
        };
        static const copy_test_step range_eof[] = {{false, 0, 12}};
        static const copy_test_step send_eof[] = {{false, -95, 12}, {true, 0, 12}};
        static const copy_test_step range_error[] = {{false, 5, 12}, {false, -5, 7}};
        static const copy_test_step send_error[] = {{false, -95, 12}, {true, 5, 12}, {true, -5, 7}};
        static const copy_test_step seek_error[] = {{false, -95, 12}};
        static const copy_test_step exhausted[] = {{false, -95, 12}, {true, -95, 12}};
        static const struct {
                const copy_test_step address_to steps;
                positive count, copied, seek_failure, seeks;
                p8 okay, range, send;
        } cases[] = {
            {partial, array_count(partial), 12, 0, 1, true, false, true},
            {range_eof, array_count(range_eof), 0, 0, 0, false, true, true},
            {send_eof, array_count(send_eof), 0, 0, 1, false, false, true},
            {range_error, array_count(range_error), 5, 0, 0, false, true, true},
            {send_error, array_count(send_error), 5, 0, 1, false, false, true},
            {seek_error, array_count(seek_error), 0, 1, 1, false, false, true},
            {exhausted, array_count(exhausted), 0, 2, 2, false, false, false},
        };
        for (positive i = 0; i < array_count(cases); i++)
        {
                p64 offsets[] = {7, 13};
                p8 range = true, send = true;
                copy_test_steps = cases[i].steps;
                copy_test_count = cases[i].count;
                copy_test_at = copy_test_used = copy_test_seeks = 0;
                copy_test_seek_failure = cases[i].seek_failure;
                copy_test_active = copy_test_explicit = true;
                p8 okay = file_copy_stream(12345, 54321, 12, true,
                                             &range, &send, offsets, null);
                copy_test_active = false;
                check("copy cascade result and cached capability flags",
                      okay == cases[i].okay && range == cases[i].range && send == cases[i].send);
                check("copy cascade script exhausted and offsets retained",
                      copy_test_at == copy_test_count && copy_test_used == cases[i].copied &&
                      offsets[0] == 7 + copy_test_used && offsets[1] == 13 + copy_test_used &&
                      copy_test_seeks == cases[i].seeks);
        }
        static const copy_test_step stream_eof[] = {{false, 0, FILE_KERNEL_COPY_SIZE}};
        p8 range = true, send = true;
        copy_test_steps = stream_eof;
        copy_test_count = 1;
        copy_test_at = copy_test_used = copy_test_seeks = 0;
        copy_test_active = true;
        copy_test_explicit = false;
        p8 okay = file_copy_stream(12345, 54321, 0, false, &range, &send, null,
                                     null);
        copy_test_active = false;
        check("unbounded copy accepts EOF without descriptor seeks", okay && !copy_test_seeks);
}

static fn storage_test_lsfd(void)
{
        string_address words[] = {"lsfd", "-p", "4294967295", "-n", "-o", "PID", null};
        program_arguments_use(words, 6);
        check("lsfd empty selection", util_linux_lsfd() == 0);
        check("lsfd does not retain unrelated PID records",
              !ul_lsfd_snapshot.header.process_count);
        program_arguments_own();
        p8 resident;
        check("lsfd keeps borrowed arena mapped", utility_arena.bytes &&
              system_call_3(syscall(mincore), (positive)utility_arena.bytes, 1,
                            (positive)address_of resident) == 0);

        static const string_address samples[] = {
            "pos:\t123\nflags:\t0100002\nmnt_id:\t77\n",
            "other: ignored\nmnt_id:77\nflags:0100002\npos:123",
            "pos:123\npos:999\nflags:0100002\nflags:0\nmnt_id:77\n",
        };
        for (positive i = 0; i < array_count(samples); i++)
        {
                p8 bytes[128];
                positive length = string_length(samples[i]);
                memory_copy_apart_end(bytes, samples[i], length);
                bytes[length] = end;
                ul_lsfd_entry entry = {0};
                ul_lsfd_fdinfo_parse(address_of entry, bytes, length);
                check("fdinfo first fields and record boundaries",
                      entry.access_known && entry.access == 2 &&
                      entry.position_known && entry.position == 123 &&
                      entry.mount_known && entry.mount_id == 77);
        }
        p8 invalid[] = "pos:123\0hidden\npos:12\nflags:8\nflags:0\nmnt_id:3 \nmnt_id:9\n";
        ul_lsfd_entry entry = {.mount_id = 41, .mount_known = true};
        ul_lsfd_fdinfo_parse(address_of entry, invalid, sizeof(invalid) - 1);
        check("fdinfo malformed first occurrence is final",
              !entry.access_known && !entry.position_known &&
              entry.mount_known && entry.mount_id == 41);
}

static fn storage_test_consumed_mounts(void)
{
        storage_mount entries[] = {
            {.source = "gone", .target = null},
            {.source = "live", .target = "/live"},
        };
        storage_mount_table table = {.entry = entries, .count = array_count(entries)};
        /*
                storage_umount_target answers the mount, not its spelling, and
                skips an entry whose target has already been consumed. So the
                source of a consumed entry finds nothing rather than finding
                an entry with a null target for a caller to walk into.
        */
        storage_mount address_to missing = storage_umount_target(address_of table, "gone");
        check("unmounted source cannot produce a null target",
              !missing || missing->target);
        storage_mount address_to live = storage_umount_target(address_of table, "/live");
        check("later target lookup skips consumed mount entries",
              live && string_equals(live->target, "/live"));
}

static p8 storage_test_output[4096];
static positive storage_test_output_used;

static fn storage_test_capture(address_any bytes, positive length)
{
        if (!length)
                length = string_length((string_address)bytes);
        if (length > sizeof(storage_test_output) - storage_test_output_used)
        {
                check("storage cell capture capacity", false);
                return;
        }
        memory_copy_apart(storage_test_output + storage_test_output_used, bytes, length);
        storage_test_output_used += length;
}

/* Independent scalar oracle: all non-NUL bytes, both root policies, duplicate
   projections, and raw+pairs precedence. OPTIONS points into read-only data;
   bounded output must never temporarily terminate a borrowed option token.
   The padded listing spells as libsmartcols does in the C locale: a control
   byte, DEL or one past ASCII as \xNN, and a backslash only where an x
   follows it; its column is as wide as what it shows. */
static positive storage_test_encode(p8 address_to into, string_address text,
                                     p8 raw, p8 pairs)
{
        positive used = 0;
        for (positive i = 0; text[i]; i++)
        {
                p8 byte = text[i];
                if ((raw || pairs) ? (byte < 32 || byte >= 127 || byte == '\\' ||
                                      (pairs ? byte == '"' : byte == ' '))
                                   : (byte < 32 || byte >= 127 ||
                                      (byte == '\\' && text[i + 1] == 'x')))
                {
                        into[used++] = '\\';
                        into[used++] = 'x';
                        into[used++] = "0123456789abcdef"[byte >> 4];
                        into[used++] = "0123456789abcdef"[byte & 15];
                }
                else
                        into[used++] = byte;
        }
        return used;
}

static fn storage_test_findmnt(void)
{
        static const p8 columns[] = {STORAGE_SOURCE, STORAGE_OPTIONS, STORAGE_ID,
            STORAGE_PARENT, STORAGE_TARGET, STORAGE_FSROOT, STORAGE_FSTYPE,
            STORAGE_MAJMIN, STORAGE_VFS_OPTIONS, STORAGE_FS_OPTIONS};
        for (positive byte = 1; byte < 256; byte++)
                for (positive mode = 0; mode < 8; mode++)
                {
                        p8 name[] = {'x', (p8)byte, 'y', 0};
                        p8 source[32], wanted[4096];
                        storage_mount mount = {.id = positive_max, .parent_id = 0,
                            .source = name, .target = "/a b", .root = "/r\n\t\\\"\xff",
                            .type = "ext4", .device = "8:1",
                            .options = "rw,,key=a b\\c\"\t\x80,dup",
                            .filesystem_options = "ro,dup,tail=\x7f"};
                        storage_findmnt_options options = {.count = array_count(columns),
                            .raw = (mode & 1) != 0, .pairs = (mode & 2) != 0,
                            .no_fsroot = (mode & 4) != 0};
                        string_copy_end(source, name);
                        if (!options.no_fsroot)
                        {
                                string_copy_end(source + 3, "[");
                                string_address end_at = string_copy_end(source + 4, mount.root);
                                string_copy_end(end_at, "]");
                        }
                        string_address values[] = {source, "ro,key=a b\\c\"\t\x80,dup,tail=\x7f",
                            "18446744073709551615", "0", mount.target, mount.root,
                            mount.type, mount.device, mount.options, mount.filesystem_options};
                        positive widths[STORAGE_COLUMN_MAX], used = 0;
                        for (positive i = 0; i < options.count; i++)
                        {
                                positive selected = (i + byte) % array_count(columns);
                                if (byte & 1) selected = i % 2; // Duplicate computed cells.
                                options.columns[i] = columns[selected];
                                p8 spelled[256];
                                positive length = options.raw || options.pairs
                                        ? string_length(values[selected])
                                        : storage_test_encode(spelled, values[selected],
                                                              false, false);
                                widths[options.columns[i]] = length + options.columns[i] % 4;
                                check("findmnt count/write projection agreement",
                                      storage_findmnt_cell(null, address_of mount,
                                          options.columns[i], address_of options) == length);
                                if (i) wanted[used++] = ' ';
                                if (options.pairs)
                                {
                                        used += string_copy_end(wanted + used,
                                            storage_column_name(options.columns[i])) - (wanted + used);
                                        wanted[used++] = '=';
                                        wanted[used++] = '"';
                                }
                                used += storage_test_encode(wanted + used, values[selected],
                                                             options.raw, options.pairs);
                                if (options.pairs)
                                        wanted[used++] = '"';
                                else if (!options.raw && i + 1 < options.count)
                                        for (positive pad = length; pad < widths[options.columns[i]]; pad++)
                                                wanted[used++] = ' ';
                        }
                        wanted[used++] = '\n';
                        storage_test_output_used = 0;
                        storage_mount_table mounts = {.entry = &mount, .count = 1};
                        storage_findmnt_view data = {&mounts, null, &options};
                        table_view view = {.output = storage_test_capture, .context = &data,
                            .cell = storage_findmnt_get, .write = storage_findmnt_field,
                            .order = options.columns, .order_size = 1, .count = options.count,
                            .separator = " ", .pairs = options.pairs};
                        table_row(&view, 0, options.raw || options.pairs ? null : widths);
                        check("findmnt all byte policies and column ordering",
                              storage_test_output_used == used &&
                              !memory_compare(storage_test_output, wanted, used));
                }
}

static fn storage_test_script_rollback(void)
{
        string_address address_to saved_words = program_words;
        b32 saved_count = program_words_count;
        string_address words[] = {"script", "-q", "-O", "/dev/null",
            "-I", "/dev/null", "-T", "/dev/null", "-c", "true", null};
        for (positive role = 0; role < 3; role++)
        {
                bipolar available = system_open_at(AT_FDCWD, "/dev/null", FILE_READ);
                check("script rollback fixture descriptor", available >= 0);
                if (available < 0) break;
                system_close(available);
                words[3 + role * 2] = "/dev/null/missing";
                program_arguments_use(words, 10);
                check("script stops before child when a log cannot open", process_script() == 1);
                bipolar after = system_open_at(AT_FDCWD, "/dev/null", FILE_READ);
                check("script rollback releases every earlier log descriptor", after == available);
                if (after >= 0) system_close(after);
                words[3 + role * 2] = "/dev/null";
        }
        if (saved_words)
                program_arguments_use(saved_words, saved_count);
        else
                program_arguments_own();
}

static fn storage_test_link_state(void)
{
        net_holding held = {0};

        held.index = 41;
        held.lease.address = 0x0a000102;
        check("a first observed down event invalidates the active link",
              net_link_news(41, 0, address_of held) && held.lost);
        netlink_forget(address_of net_states);
        memory_fill(address_of held, 0, sizeof held);

        /* Link up sends news without IFF_RUNNING at once and RUNNING up to a
           second later; a lease taken between them must survive the first.
           The loopback link stands in: news says down, the link says up. */
        {
                netlink_search loopback = {.wanted = (string_address)"lo"};
                bipolar handle = netlink_open_groups(0);
                p8 found = handle >= 0 &&
                             netlink_link_find((b32)handle, address_of loopback) >= 0;

                if (handle >= 0)
                        socket_close((b32)handle);
                held.index = loopback.index;
                held.lease.address = 0x7f000002;
                string_copy_max_end(held.name, (string_address)"lo", IFNAME_SIZE - 1);
                if (!found)
                        log_direct(str("storage_io: link news against lo NOT RUN -- no routing socket\n"));
                else
                        check("news without RUNNING is judged by the link as it is now",
                              !net_link_news(loopback.index, IFF_UP, address_of held) &&
                                  !held.lost);
        }
        netlink_forget(address_of net_states);
        memory_fill(address_of held, 0, sizeof held);

        {
                p8 record[NETLINK_HEADER + sizeof(netlink_link)] = {0};
                netlink_header address_to header =
                    (netlink_header address_to)record;
                netlink_link address_to link =
                    (netlink_link address_to)(record + NETLINK_HEADER);

                held.index = 42;
                held.lease.address = 0x0a000202;
                check("an active link can seed deletion state",
                      !net_link_news(42, IFF_RUNNING, address_of held) &&
                          net_states.used == sizeof(net_state));
                header->type = RTM_DELLINK;
                header->length = NETLINK_HEADER;
                link->index = 42;
                check("a truncated link deletion cannot consume its body",
                      !net_link_event(header, address_of held) && !held.lost);
                header->length = sizeof record;
                check("device removal invalidates its lease and carrier snapshot",
                      net_link_event(header, address_of held) && held.lost &&
                          !net_states.used);
        }
        netlink_forget(address_of net_states);
        memory_fill(address_of held, 0, sizeof held);

        check("new down interface can be configured", net_link_news(11, 0, address_of held));
        check("initial carrier can be configured", net_link_news(11, IFF_RUNNING, address_of held));
        held.index = 11;
        held.lease.address = 0x0a000102;
        held.lease.mask = 0xffffff00;
        held.lease.router = 0x0a000101;
        check("matching lease state does not imply kernel-object ownership",
              net_holds_address(address_of held, 11, address_of held.lease) &&
                  net_holds_route(address_of held, 11, address_of held.lease) &&
                  !net_owns_address(address_of held) &&
                  !net_owns_route(address_of held));
        held.address_owned = true;
        held.route_owned = true;
        check("exclusive installation records address and route ownership",
              net_owns_address(address_of held) &&
                  net_owns_route(address_of held));
        check("unchanged lease objects retain ownership",
              net_ownership_next(true, false, false, true));
        check("failed replacement cannot acquire ownership",
              !net_ownership_next(false, true, false, true));
        check("a lease without a route drops route ownership",
              !net_ownership_next(true, true, true, false));
        check("unchanged carrier keeps lease", !net_link_news(11, IFF_RUNNING, address_of held));
        /* A second link gaining carrier is looked at even with a lease held,
           since the preference may favour it (46a05157, "a second live link
           is therefore worth a look"); what keeps the lease is auto finding
           the preferred link is the one that holds it. */
        check("second live interface is looked at", net_link_news(12, IFF_RUNNING, address_of held));
        check("its unchanged carrier is not looked at again",
              !net_link_news(12, IFF_RUNNING, address_of held));
        check("carrier loss retains state until kernel cleanup",
              net_link_news(11, 0, address_of held) && held.index == 11 &&
                  held.lost);
        check("failed reconfiguration cannot forget stale kernel state",
              net_auto(-1, address_of held) != 0 && held.index == 11 &&
                  held.lost);
        check("new interface can retry after configuration failure",
              net_link_news(13, 0, address_of held));
        /* A flapping second link must not cut every exchange as it starts:
           after a cut the news waits out the hold-off, then cuts again. */
        check("link news may cut an exchange nothing has cut", net_news_may_cut(100));
        net_news_holdoff_until = 104;
        check("link news is held off for the seconds after a cut",
              !net_news_may_cut(100) && !net_news_may_cut(103));
        check("and cuts again when the hold-off ends", net_news_may_cut(104));
        net_news_holdoff_until = 0;

        /* The seconds a link was given are kept across cuts: three exchanges
           cut at four seconds are the twelve, and the link is then silent
           and passed over, where each cut began the twelve again and the
           link behind it was never asked. */
        memory_fill(net_quiet, 0, sizeof net_quiet);
        check("a link nothing is known of is given the whole wait",
              net_quiet_budget(31) == NET_DHCP_WATCH_SECONDS &&
                  !net_quiet_waiting(31, 100) && !net_quiet_due_in(100));
        net_quiet_cut(31, 4, 104);
        check("a cut exchange leaves the rest of the wait, and the link not yet silent",
              net_quiet_budget(31) == NET_DHCP_WATCH_SECONDS - 4 &&
                  !net_quiet_waiting(31, 104) && !net_quiet_due_in(104));
        net_quiet_cut(31, 4, 108);
        check("a second cut leaves less", net_quiet_budget(31) == NET_DHCP_WATCH_SECONDS - 8);
        net_quiet_cut(31, 4, 112);
        check("cuts that add up to the wait leave the link silent",
              net_quiet_waiting(31, 112) &&
                  net_quiet_waiting(31, 112 + NET_QUIET_FIRST_SECONDS - 1) &&
                  !net_quiet_waiting(31, 112 + NET_QUIET_FIRST_SECONDS) &&
                  net_quiet_budget(31) == NET_DHCP_WATCH_SECONDS);
        {
                netlink_search walk = {.skip_count = 1, .skip = {7}};

                check("a silent link is passed over by the walk, after the skips it had",
                      net_quiet_skip(address_of walk, 113) == 1 &&
                          walk.skip_count == 2 && walk.skip[0] == 7 &&
                          walk.skip[1] == 31);
                walk.skip_count = 0;
                check("and is not once it is due",
                      net_quiet_skip(address_of walk, 112 + NET_QUIET_FIRST_SECONDS) == 0 &&
                          walk.skip_count == 0);
        }
        /* A silent link is asked again while another holds the lease: the
           watcher's wait ends when it is due, and each silence doubles the
           gap up to the most. It was asked once and never again. */
        check("the watcher's wait ends when the silent link is due",
              net_quiet_due_in(112) == NET_QUIET_FIRST_SECONDS &&
                  net_quiet_due_in(141) == 1 && net_quiet_due_in(500) == 1);
        net_quiet_silent(31, 120);
        check("silence before it was due changes nothing",
              net_quiet_due_in(112) == NET_QUIET_FIRST_SECONDS);
        {
                positive now = 112 + NET_QUIET_FIRST_SECONDS;
                positive gap = NET_QUIET_FIRST_SECONDS;
                bool doubled = true;
                bool capped;

                for (positive turn = 0; turn < 4; turn++)
                {
                        net_quiet_silent(31, now);
                        gap *= 2;
                        doubled = doubled && net_quiet_due_in(now) == gap;
                        now += gap;
                }
                check("silence when it was due doubles the gap", doubled && gap == 480);
                net_quiet_silent(31, now);
                capped = net_quiet_due_in(now) == NET_QUIET_MOST_SECONDS;
                now += NET_QUIET_MOST_SECONDS;
                net_quiet_silent(31, now);
                check("to ten minutes and no further",
                      capped && net_quiet_due_in(now) == NET_QUIET_MOST_SECONDS);
                net_quiet_passed(now + 5);
                check("a link not yet due is left alone by a pass that did not reach it",
                      net_quiet_due_in(now + 5) == NET_QUIET_MOST_SECONDS - 5);
                net_quiet_passed(now + NET_QUIET_MOST_SECONDS + 3);
                check("and one that was due is due a gap later, not at once for good",
                      net_quiet_due_in(now + NET_QUIET_MOST_SECONDS + 3) ==
                          NET_QUIET_MOST_SECONDS);
        }
        net_quiet_cut(31, 5, 9000);
        memory_fill(address_of held, 0, sizeof held);
        held.index = 11;
        check("a carrier change on the silent link ends its schedule but not the seconds waited",
              net_link_news(31, IFF_RUNNING, address_of held) &&
                  !net_quiet_due_in(9000) && !net_quiet_waiting(31, 9000) &&
                  net_quiet_budget(31) == NET_DHCP_WATCH_SECONDS - 5);
        net_quiet_answered(31);
        check("an answer ends both", net_quiet_budget(31) == NET_DHCP_WATCH_SECONDS &&
                                         !net_quiet_of(31, false));
        for (p32 index = 40; index < 40 + NET_QUIET_LINKS + 1; index++)
                net_quiet_silent(index, 1000 + index);
        check("one link more than is kept takes the place of the one due soonest",
              !net_quiet_of(40, false) && net_quiet_of(41, false) &&
                  net_quiet_of(40 + NET_QUIET_LINKS, false));
        memory_fill(net_quiet, 0, sizeof net_quiet);
        netlink_forget(address_of net_states);
        memory_fill(address_of held, 0, sizeof held);

        /* A burst of link news is read whole and answered with one pass:
           three datagrams waiting are three links' news taken by one call
           that owes one pass, where a call took one and ran a pass for it. */
        {
                b32 pair[2];
                netlink_buffer message = {0};

                if (system_call_4(syscall(socketpair), AF_UNIX, SOCK_DGRAM, 0,
                                  (positive)pair) != 0)
                        log_direct(str("storage_io: link news burst NOT RUN -- no socket pair\n"));
                else
                {
                        struct { netlink_header header; netlink_link link; } news = {
                            .header = {.length = NETLINK_HEADER + sizeof(netlink_link),
                                       .type = RTM_NEWLINK}};
                        p8 left;

                        for (p32 index = 21; index < 24; index++)
                        {
                                news.link.index = index;
                                (void)socket_send(pair[1], address_of news,
                                                  sizeof news, 0, null, 0);
                        }
                        net_exchange_cut = true;
                        check("a burst of link news is taken whole and owes one pass",
                              net_watch_drain(pair[0], address_of message,
                                              address_of held) == 1 &&
                                  net_states.used == 3 * sizeof(net_state) &&
                                  socket_receive(pair[0], address_of left, 1,
                                                 MSG_DONTWAIT, null, null) < 0);
                        news.link.index = 21;
                        (void)socket_send(pair[1], address_of news, sizeof news, 0,
                                          null, 0);
                        check("news that changes nothing owes none",
                              net_watch_drain(pair[0], address_of message,
                                              address_of held) == 0);
                        socket_close(pair[0]);
                        socket_close(pair[1]);
                        netlink_forget(address_of message);
                }
        }
        /* A pass answers whatever cut the last exchange: the cut is not
           owed a second pass after news that was acted on. A pass on no
           routing socket is a pass all the same. */
        net_exchange_cut = true;
        check("a pass leaves no cut exchange owed another",
              net_reconfigure((b32)-1, address_of held) != 0 && !net_exchange_cut);
        netlink_forget(address_of net_states);
        memory_fill(address_of held, 0, sizeof held);
        held.index = 11;
        held.lease.address = 0x0a000102;
        held.lease.mask = 0xffffff00;
        held.lease.router = 0x0a000101;
        held.lost = true;

        dhcp_lease same = held.lease;
        dhcp_lease changed = held.lease;
        check("the held address includes interface, host and prefix",
              net_holds_address(address_of held, 11, address_of same) &&
                  !net_holds_address(address_of held, 12, address_of same));
        changed.address++;
        check("a replacement DHCP address is detected",
              !net_holds_address(address_of held, 11, address_of changed));
        changed = held.lease;
        changed.mask = 0xffff0000;
        check("a replacement DHCP prefix is a distinct kernel address",
              !net_holds_address(address_of held, 11, address_of changed));
        changed = held.lease;
        changed.router = 0;
        check("a lease without a router clears the old default",
              !net_holds_route(address_of held, 11, address_of changed));
        check("missing kernel state is an idempotent cleanup result",
              net_change_gone(0) && net_change_gone(-ERROR_NO_ENTRY) &&
                  net_change_gone(-ERROR_NO_PROCESS) &&
                  net_change_gone(-ERROR_NO_DEVICE) &&
                  net_change_gone(-EADDRNOTAVAIL) &&
                  !net_change_gone(-ERROR_ACCESS));

        {
                net_holding timed = {.index = 7, .taken = 100};

                timed.lease.seconds = 60;
                timed.lease.renewal = 30;
                timed.lease.rebinding = 52;
                timed.retry = 30;
                check("a DHCP lease remains valid before its deadline",
                      !net_lease_expired_at(address_of timed, 159));
                check("a DHCP renewal wait reaches the half-life",
                      net_lease_due_in(address_of timed, 120) == 10);
                check("a lease stays in renewal before T2",
                      !net_lease_rebinding_at(address_of timed, 151));
                check("a lease enters rebinding exactly at T2",
                      net_lease_rebinding_at(address_of timed, 152));
                check("a renewal request uses the ordinary bounded wait",
                      net_lease_attempt_time(address_of timed, 129, false) == 4);
                check("a renewal request cannot wait past T2",
                      net_lease_attempt_time(address_of timed, 151, false) == 1);
                check("a rebinding request cannot wait past expiry",
                      net_lease_attempt_time(address_of timed, 159, true) == 1);

                net_lease_retry_after(address_of timed, 140, false);
                check("a renewal timeout retries halfway to T2 without discovery",
                      timed.retry == 52 &&
                          net_lease_due_in(address_of timed, 140) == 12 &&
                          !timed.lost);
                net_lease_retry_after(address_of timed, 153, false);
                check("a renewal timeout crossing T2 wakes rebinding immediately",
                      timed.retry == 53 &&
                          net_lease_due_in(address_of timed, 153) == 1 &&
                          net_lease_rebinding_at(address_of timed, 153));
                net_lease_retry_after(address_of timed, 154, true);
                check("a rebinding timeout retries at expiry for a short remainder",
                      timed.retry == 60 &&
                          net_lease_due_in(address_of timed, 154) == 6 &&
                          !timed.lost);
                check("a DHCP lease expires at its exact deadline",
                      net_lease_expired_at(address_of timed, 160) &&
                          net_lease_due_in(address_of timed, 160) == 1);
                check("a regressed lease clock fails closed",
                      net_lease_expired_at(address_of timed, 99) &&
                          net_lease_due_in(address_of timed, 99) == 1);
                check("an unavailable lease clock fails closed",
                      net_lease_expired_at(address_of timed, 0) &&
                          net_lease_due_in(address_of timed, 0) == 1);

                timed.taken = 100;
                timed.lease.seconds = 3600;
                timed.lease.renewal = 1800;
                timed.lease.rebinding = 3150;
                timed.retry = 1800;
                net_lease_retry_after(address_of timed, 1904, false);
                check("a long renewal interval retries halfway to T2",
                      timed.retry == 2477 &&
                          net_lease_due_in(address_of timed, 1904) == 673);

                timed.taken = 100;
                timed.lease.seconds = 2;
                timed.lease.renewal = 0;
                timed.lease.rebinding = 0;
                timed.retry = 0;
                check("an unrepresentably short lease waits for expiry",
                      net_lease_due_in(address_of timed, 100) == 2 &&
                          net_lease_due_in(address_of timed, 101) == 1 &&
                          !net_lease_rebinding_at(address_of timed, 101));
                timed.lease.seconds = 0;
                check("an unbounded DHCP lease has no local deadline",
                      !net_lease_expired_at(address_of timed, 1000));
        }

        netlink_forget(address_of net_states);
}

static p8 storage_test_write_text(string_address path, const void address_to text,
                                    positive length)
{
        bipolar handle = system_open_at(AT_FDCWD, path, 1 | O_CLOEXEC);
        p8 written = handle >= 0 &&
                       system_write_all((positive)handle, text, length) == length;

        if (handle >= 0)
                system_close(handle);
        return written;
}

/* A namespace row's child that could not have its namespaces says why: 2
   when the kernel will not give them, STORAGE_TEST_THREADED when unshare
   answered EINVAL for a new user namespace, which the kernel gives only to a
   process of one thread. qemu-user runs every program beside threads of its
   own (and starts them again in a forked child), so the arm64 and riscv64
   runs cannot ask these rows; the native run on the box does. */
#define STORAGE_TEST_THREADED 7

static p8 storage_test_not_run(b32 status, string_address what,
                                 string_address otherwise)
{
        if (status != 2 && status != STORAGE_TEST_THREADED)
                return false;
        string_format(log, "storage_io: %s NOT RUN -- %s\n", what,
                      status == 2 ? otherwise
                                  : (string_address) "a multithreaded runner "
                                                     "(qemu-user) gets no new "
                                                     "user namespace");
        log_flush();
        return true;
}

static positive storage_test_boottime(void)
{
        timespec stamp = {0, 0};

        return clock_gettime(CLOCK_BOOTTIME, address_of stamp) < 0
                   ? 0
                   : (positive)stamp.tv_sec * NETWORK_NANOSECONDS +
                         (positive)stamp.tv_nsec;
}

/* The lease clock's first second.  A boot takes its lease there, and zero is
   what net_seconds says for a clock that failed -- which expires every lease
   at once.  A time namespace whose monotonic clock starts about now puts a
   grandchild in that second (NOT RUN without unprivileged user namespaces). */
static fn storage_test_lease_clock_origin(void)
{
        b32 status = 0;
        bipolar child = system_fork();

        if (child == 0)
        {
                p8 offsets[48] = "boottime -";
                positive at = 10;
                positive now = storage_test_boottime();
                timespec pause = {0, 250000000};
                b32 inner = 0;

                if (now % NETWORK_NANOSECONDS > 700000000)
                {
                        system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
                        now = storage_test_boottime();
                }
                bipolar unshared = system_call_1(syscall(unshare),
                                                 CLONE_NEWUSER | CLONE_NEWTIME);
                if (unshared < 0)
                        system_call_1(syscall(exit_group),
                                      unshared == -EINVAL ? STORAGE_TEST_THREADED : 2);
                at += positive_into(offsets + at, now / NETWORK_NANOSECONDS);
                memory_copy(offsets + at, " 0\n", 3);
                at += 3;
                if (!storage_test_write_text("/proc/self/timens_offsets", offsets, at))
                        system_call_1(syscall(exit_group), 2);
                bipolar grandchild = system_fork();
                if (grandchild == 0)
                        system_call_1(syscall(exit_group), net_seconds() == 1 ? 0 : 1);
                system_call_4(syscall(wait4), (positive)grandchild,
                              (positive)address_of inner, 0, 0);
                system_call_1(syscall(exit_group), (inner >> 8) & 0xff);
        }
        if (child > 0)
                system_call_4(syscall(wait4), (positive)child,
                              (positive)address_of status, 0, 0);
        if (!(child > 0 && (status & 0x7f) == 0 &&
              storage_test_not_run((status >> 8) & 0xff, "lease clock origin",
                                   "no time namespace")))
                check("a lease taken in the clock's first second is not a failed clock",
                      child > 0 && status == 0);
}

/* A lease held across a suspend. A time namespace moves the clocks a child
   reads: boottime two hours on is a machine that slept two hours (the
   monotonic clock does not count a suspend, CLOCK_BOOTTIME does), and its
   hour's lease has run out; boottime a hundred seconds on is a lease with
   1,700 seconds to its renewal; and the monotonic clock two hours on with
   boottime where it was is not a suspend at all. Exit bits over 16, so 2
   and the threaded runner's 7 stay NOT RUN. */
static fn storage_test_lease_asleep(void)
{
        static const struct
        {
                string_address offsets;
                p8 expired;
                positive due;
        } cases[] = {{"boottime 7200 0\n", true, 1},
                     {"boottime 100 0\n", false, 1700},
                     {"monotonic 7200 0\n", false, 1800}};
        b32 wrong = 16;
        b32 status = 0;

        for (positive at = 0; at < array_count(cases); at++)
        {
                bipolar child = system_fork();

                if (child == 0)
                {
                        net_holding held = {.index = 1};
                        b32 inner = 0;

                        held.lease.seconds = 3600;
                        held.lease.renewal = 1800;
                        held.lease.rebinding = 3150;
                        held.retry = 1800;
                        held.taken = net_seconds();
                        bipolar unshared = system_call_1(
                            syscall(unshare), CLONE_NEWUSER | CLONE_NEWTIME);
                        if (unshared < 0)
                                system_call_1(syscall(exit_group),
                                              unshared == -EINVAL
                                                  ? STORAGE_TEST_THREADED
                                                  : 2);
                        if (!storage_test_write_text(
                                "/proc/self/timens_offsets", cases[at].offsets,
                                string_length(cases[at].offsets)))
                                system_call_1(syscall(exit_group), 2);
                        bipolar grandchild = system_fork();
                        if (grandchild == 0)
                        {
                                positive now = net_seconds();
                                positive due = net_lease_due_in(address_of held,
                                                                now);

                                //      A second of slack for the clock
                                //      moving between the two readings.
                                system_call_1(
                                    syscall(exit_group),
                                    net_lease_expired_at(address_of held, now) ==
                                                cases[at].expired &&
                                            due <= cases[at].due &&
                                            due + 1 >= cases[at].due
                                        ? 0
                                        : 1);
                        }
                        system_call_4(syscall(wait4), (positive)grandchild,
                                      (positive)address_of inner, 0, 0);
                        system_call_1(syscall(exit_group), (inner >> 8) & 0xff);
                }
                if (child > 0)
                        system_call_4(syscall(wait4), (positive)child,
                                      (positive)address_of status, 0, 0);
                status = child > 0 && !(status & 0x7f) ? (status >> 8) & 0xff : 1;
                if (status == 2 || status == STORAGE_TEST_THREADED)
                        break;
                if (status)
                        wrong |= 1 << at;
        }
        if (!storage_test_not_run(status == 2 || status == STORAGE_TEST_THREADED
                                      ? status
                                      : wrong,
                                  "lease across a suspend", "no time namespace"))
        {
                check("a lease runs out while the machine sleeps",
                      !(wrong & 1));
                check("time asleep short of the renewal moves it nearer, no more",
                      !(wrong & 2));
                check("a monotonic clock that jumps is not time asleep",
                      !(wrong & 4));
        }
}

static fn storage_test_arp_claims(void)
{
        p8 packet[NET_ARP_PACKET] = {0};
        p8 mine[6] = {2, 0, 0, 0, 0, 1};
        p8 other[6] = {2, 0, 0, 0, 0, 2};
        p32 offered = 0xc0a80164;

        network_store_16(packet, 1);
        network_store_16(packet + 2, ETH_P_IP);
        packet[4] = 6;
        packet[5] = 4;
        network_store_16(packet + 6, 2);
        memory_copy(packet + 8, other, 6);
        network_store_32(packet + 14, offered);
        check("another station's ARP reply conflicts with a DHCP offer",
              net_arp_claims(packet, sizeof packet, mine, offered));
        network_store_16(packet + 6, 1);
        check("another station's ARP request also defends its address",
              net_arp_claims(packet, sizeof packet, mine, offered));
        network_store_32(packet + 14, 0);
        network_store_32(packet + 24, offered);
        check("a simultaneous ARP probe conflicts with a DHCP offer",
              net_arp_claims(packet, sizeof packet, mine, offered));
        memory_copy(packet + 8, mine, 6);
        check("our own ARP packet is not an address conflict",
              !net_arp_claims(packet, sizeof packet, mine, offered));
        memory_copy(packet + 8, other, 6);
        network_store_32(packet + 14, offered + 1);
        network_store_32(packet + 24, 0);
        check("a claim for another address is irrelevant",
              !net_arp_claims(packet, sizeof packet, mine, offered));
        check("a truncated ARP claim is refused",
              !net_arp_claims(packet, NET_ARP_PACKET - 1, mine, offered));

        //      Every field that says what the packet is, wrong one at a
        //      time, is not a claim; the frame's padding is not a field.
        {
                p8 padded[46] = {0};
                static const struct { p8 at, value; } wrong[] = {
                    {1, 6}, {3, 0x06}, {4, 8}, {5, 16}, {7, 3}, {7, 0}};

                memory_copy(padded, packet, NET_ARP_PACKET);
                network_store_32(padded + 14, offered);
                check("an ARP claim padded to the frame minimum is a claim",
                      net_arp_claims(padded, sizeof padded, mine, offered));
                for (positive at = 0; at < array_count(wrong); at++)
                {
                        p8 copy[46];

                        memory_copy(copy, padded, sizeof copy);
                        copy[wrong[at].at] = wrong[at].value;
                        check("an ARP packet of another medium, protocol, length or operation is no claim",
                              !net_arp_claims(copy, sizeof copy, mine, offered));
                }
                p8 refused = true;

                for (positive length = 0; length < NET_ARP_PACKET; length++)
                        refused &= !net_arp_claims(padded, length, mine, offered);
                check("an ARP claim cut at any byte is refused", refused);
        }
}

/* A renewal with nothing to renew answers DHCP_NO_OFFER from inside the
   child, so the status crossed the pipe; the child is reaped and no
   descriptor is left behind. */
static fn storage_test_dhcp_apart(void)
{
        p8 hardware[6] = {2, 0, 0, 0, 0, 1};
        dhcp_lease lease = {0};
        bipolar before = system_open_at(AT_FDCWD, "/", FILE_READ | O_CLOEXEC);

        if (before >= 0)
                system_close(before);
        bipolar status = net_dhcp_apart("moonwater-no-interface", hardware,
                                        address_of lease, NET_DHCP_RENEW, 1);
        bipolar after = system_open_at(AT_FDCWD, "/", FILE_READ | O_CLOEXEC);

        if (after >= 0)
                system_close(after);
        check("a lease exchange apart hands its answer back and leaves nothing",
              status == DHCP_NO_OFFER && before >= 0 && after == before &&
                  system_call_4(syscall(wait4), (positive)-1, 0, 1, 0) ==
                      -ECHILD);

        //      A DECLINE goes through the same child: nothing to send from
        //      on a link that is not there, and nothing to name without a
        //      server, and neither leaves a child or a descriptor.
        lease.address = 0x0a090909;
        lease.server = 0x0a090901;
        status = net_dhcp_apart("moonwater-no-interface", hardware,
                                address_of lease, NET_DHCP_DECLINE, 0);
        lease.server = 0;
        bipolar unnamed = net_dhcp_apart("lo", hardware, address_of lease,
                                         NET_DHCP_DECLINE, 0);
        after = system_open_at(AT_FDCWD, "/", FILE_READ | O_CLOEXEC);
        if (after >= 0)
                system_close(after);
        check("a DECLINE apart without a link or a server sends nothing and leaves nothing",
              status == DHCP_NO_SOCKET && unnamed == DHCP_NO_SOCKET &&
                  after == before &&
                  system_call_4(syscall(wait4), (positive)-1, 0, 1, 0) ==
                      -ECHILD);
}

/* The wait after a DECLINE is the declined link's, at least ten seconds, and
   the slots it is kept in never lose a link that is still waiting to one
   that is not. */
static fn storage_test_decline_hold(void)
{
        memory_zero(net_declined, sizeof net_declined);
        check("no link waits before anything was declined",
              !net_decline_waiting(3) && !net_decline_left(0));
        net_decline_hold(3);
        positive left = net_decline_left(3);
        check("a declined link waits ten seconds before it asks again",
              net_decline_waiting(3) && left > 9900 &&
                  left <= NET_DECLINE_HOLDOFF_SECONDS * 1000 + 1);
        check("and only that link waits", !net_decline_waiting(4) &&
                                              !net_decline_waiting(0));
        net_decline_hold(3);
        positive held = 0;
        for (positive at = 0; at < NET_DECLINE_LINKS; at++)
                held += net_declined[at].index == 3;
        check("declining the same link again keeps one wait for it", held == 1);
        for (p32 index = 10; index < 10 + NET_DECLINE_LINKS + 2; index++)
                net_decline_hold(index);
        check("more declined links than slots keep the latest ones waiting",
              net_decline_waiting(10 + NET_DECLINE_LINKS + 1) &&
                  net_decline_waiting(10 + NET_DECLINE_LINKS));
        net_declined[0].until.began -= 11 * NETWORK_NANOSECONDS;
        p32 expired = net_declined[0].index;
        check("a wait that has run out is over and frees its slot",
              !net_decline_waiting(expired) && !net_declined[0].index);
        memory_zero(net_declined, sizeof net_declined);
}

/* A user and mount namespace of the caller's own (which maps the caller to
   root and every other id on the host to nobody) with a tmpfs over /etc, and
   what else `more` asks to be unshared, which must be a child: exit 2 (NOT
   RUN) where namespaces are refused. */
static fn storage_test_namespace(positive more)
{
        p8 map[48] = "0 ";
        p8 groups[48] = "0 ";
        //      The ids outside, read before the namespace hides them; a
        //      file made under an unmapped id is EOVERFLOW.
        positive at = 2 + positive_into(
            map + 2, (positive)system_call_1(syscall(getuid), 0));
        positive group = 2 + positive_into(
            groups + 2, (positive)system_call_1(syscall(getgid), 0));

        bipolar unshared = system_call_1(syscall(unshare), CLONE_NEWUSER |
                                                               CLONE_NEWNS |
                                                               more);
        if (unshared < 0)
                system_call_1(syscall(exit_group),
                              unshared == -EINVAL ? STORAGE_TEST_THREADED : 2);
        memory_copy(map + at, " 1\n", 4);
        memory_copy(groups + group, " 1\n", 4);
        if (!storage_test_write_text("/proc/self/uid_map", map, at + 3) ||
            !storage_test_write_text("/proc/self/setgroups", "deny", 4) ||
            !storage_test_write_text("/proc/self/gid_map", groups, group + 3) ||
            system_call_5(syscall(mount), 0, (positive) "/", 0, MS_REC | MS_PRIVATE, 0) < 0 ||
            system_call_5(syscall(mount), (positive) "tmpfs", (positive) "/etc",
                          (positive) "tmpfs", 0, 0) < 0)
                system_call_1(syscall(exit_group), 2);
}

//      The same with lo up and a routing socket to say so in.
static bipolar storage_test_net_namespace(void)
{
        bipolar handle;

        storage_test_namespace(CLONE_NEWNET);
        if ((handle = netlink_open_groups(0)) < 0 ||
            netlink_link_up((b32)handle, 1) < 0)
                system_call_1(syscall(exit_group), 2);
        return handle;
}

//      A child's exit status, 2 being NOT RUN.
static b32 storage_test_child_status(bipolar child)
{
        b32 status = 0;

        if (child < 0)
                return -1;
        system_call_4(syscall(wait4), (positive)child, (positive)address_of status,
                      0, 0);
        return (status & 0x7f) ? -1 : (status >> 8) & 0xff;
}

/* A restarted watcher meets the address its predecessor installed: the
   exclusive create answers EEXIST. An operator's address there (added
   first, no lease stamped on it) must be taken, held without ownership,
   and released without removing an address it never owned. */
static fn storage_test_lease_over_existing(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                p8 hardware[6] = {2, 0, 0, 0, 0, 1};
                dhcp_lease lease = {.address = 0x0a090909, .mask = 0xffffff00,
                                    .server = 0x0a090901, .seconds = 60,
                                    .renewal = 30, .rebinding = 52};
                net_holding held = {0};
                bipolar handle = storage_test_net_namespace();

                if (netlink_address_add((b32)handle, 1, lease.address, 24) < 0)
                        system_call_1(syscall(exit_group), 2);
                if (net_apply_lease((b32)handle, 1, "lo", hardware, address_of lease,
                                    address_of held, false) != 0 ||
                    held.index != 1 || held.address_owned)
                        system_call_1(syscall(exit_group), 1);
                system_call_1(syscall(exit_group),
                              net_holding_release((b32)handle, address_of held) == 0 &&
                                      netlink_address_delete((b32)handle, 1, lease.address,
                                                             24) == 0
                                  ? 0 : 3);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "lease over an existing address",
                                 "no namespaces"))
                check("a lease over an address already there is taken, held unowned and left",
                      status == 0);
}

typedef struct
{
        p32 host;
        p8 found;
        p32 valid;
} storage_test_address_facts;

static p8 storage_test_address_seen(netlink_header address_to header,
                                      address_any context)
{
        storage_test_address_facts address_to facts = context;
        positive size = 0;
        p8 address_to named = netlink_find(header, sizeof(netlink_address),
                                           IFA_LOCAL, address_of size);
        p8 address_to cache;

        if (!named || size != 4 ||
            memory_load_unaligned(p32, named) != network_order_32(facts->host))
                return true;
        facts->found = true;
        cache = netlink_find(header, sizeof(netlink_address), IFA_CACHEINFO,
                             address_of size);
        facts->valid = cache && size >= 8 ? memory_load_unaligned(p32, cache + 4)
                                         : 0;
        return true;
}

/* The same address when the lease stamped it: a restarted watcher takes it
   back, so a NAK or expiry removes it, and the kernel holds it to the
   lease's lifetime on its own. A restarted watcher handed another address
   removes the lease it inherited instead, and a one-shot ip auto, which
   nothing renews, installs its address as it always did. Exit 4: no finite
   lifetime; 5: not taken back; 6: not removed on release; 7: an inherited
   lease outlived a new one; 8: a one-shot address expires. */
static fn storage_test_lease_inherited(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                p8 hardware[6] = {2, 0, 0, 0, 0, 1};
                dhcp_lease lease = {.address = 0x0a090a0a, .mask = 0xffffff00,
                                    .server = 0x0a090a01, .seconds = 60,
                                    .renewal = 30, .rebinding = 52};
                dhcp_lease other = lease;
                net_holding first = {0};
                net_holding second = {0};
                net_holding third = {0};
                storage_test_address_facts facts = {.host = lease.address};
                storage_test_address_facts moved = {.host = 0x0a090a0b};
                bipolar handle = storage_test_net_namespace();

                if (net_apply_lease((b32)handle, 1, "lo", hardware, address_of lease,
                                    address_of first, false) != 0 ||
                    !first.address_owned)
                        system_call_1(syscall(exit_group), 1);
                netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                             AF_INET, storage_test_address_seen, address_of facts);
                if (!facts.found || !facts.valid || facts.valid > 60)
                        system_call_1(syscall(exit_group), 4);
                if (net_apply_lease((b32)handle, 1, "lo", hardware, address_of lease,
                                    address_of second, false) != 0 ||
                    !second.address_owned)
                        system_call_1(syscall(exit_group), 5);
                other.address = moved.host;
                facts.found = false;
                if (net_apply_lease((b32)handle, 1, "lo", hardware, address_of other,
                                    address_of third, false) != 0 ||
                    netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                                 AF_INET, storage_test_address_seen,
                                 address_of facts) < 0 ||
                    facts.found)
                        system_call_1(syscall(exit_group), 7);
                if (net_holding_release((b32)handle, address_of third) != 0 ||
                    netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                                 AF_INET, storage_test_address_seen,
                                 address_of moved) < 0 ||
                    moved.found)
                        system_call_1(syscall(exit_group), 6);
                other.address = 0x0a090a0c;
                moved.host = other.address;
                if (net_apply_lease((b32)handle, 1, "lo", hardware, address_of other,
                                    null, false) != 0 ||
                    netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                                 AF_INET, storage_test_address_seen,
                                 address_of moved) < 0 ||
                    !moved.found || moved.valid != 0xffffffff)
                        system_call_1(syscall(exit_group), 8);
                system_call_1(syscall(exit_group), 0);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "inherited lease",
                                 "no namespaces"))
        {
                check("a leased address carries the lease's lifetime",
                      status != 4 && status != 1);
                check("a restarted watcher takes back the address its predecessor leased",
                      status != 5 && status != 1);
                check("a restarted watcher handed another address removes the one it inherited",
                      status != 7 && status != 5 && status != 4 && status != 1);
                check("an inherited lease is removed when it is released",
                      status != 6 && status != 7 && status != 5 && status != 4 &&
                          status != 1);
                check("a one-shot ip auto address keeps no lifetime",
                      status == 0);
        }
}

/* A dummy link of the namespace's own, whose carrier can be set. */
static bipolar storage_test_dummy(b32 handle, string_address name)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        //      IFLA_LINKINFO's payload: IFLA_INFO_KIND "dummy", padded.
        static const p8 kind[12] = {10, 0, IFLA_INFO_KIND, 0,
                                    'd', 'u', 'm', 'm', 'y', 0, 0, 0};

        if (!netlink_begin(address_of request, RTM_NEWLINK,
                           NLM_REQUEST | NLM_ACK | NLM_CREATE | NLM_EXCLUSIVE,
                           sequence, sizeof(netlink_link)))
                return -1;
        netlink_attribute_add(address_of request, IFLA_IFNAME, name,
                              string_length(name) + 1);
        netlink_attribute_add(address_of request, IFLA_LINKINFO,
                              (address_any)kind, sizeof kind);
        return netlink_transact(handle, address_of request, sequence, null, null);
}

#define STORAGE_TEST_IFLA_CARRIER 33

//      IFLA_CARRIER (one byte) or IFLA_MTU set on a link.
static bipolar storage_test_link_set(b32 handle, p32 index, p16 type, p32 value)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p8 carrier = (p8)value;

        if (!netlink_begin(address_of request, RTM_NEWLINK,
                           NLM_REQUEST | NLM_ACK, sequence, sizeof(netlink_link)))
                return -1;
        ((netlink_link address_to)netlink_body(address_of request))->index = index;
        if (type == STORAGE_TEST_IFLA_CARRIER)
                netlink_attribute_add(address_of request, type, address_of carrier, 1);
        else
                netlink_attribute_add(address_of request, type, address_of value, 4);
        return netlink_transact(handle, address_of request, sequence, null, null);
}

/* The news a link change made, fed to the watcher as it would read it:
   every datagram that comes within the wait, which ends early once the
   lease is lost. Carrier news goes through linkwatch, up to a second late. */
static fn storage_test_link_news(b32 events, net_holding address_to held,
                                 positive nanoseconds)
{
        netlink_buffer message = {0};
        network_deadline deadline;

        if (!network_deadline_begin(address_of deadline, nanoseconds /
                                        NETWORK_NANOSECONDS,
                                    nanoseconds % NETWORK_NANOSECONDS))
                return;
        while (!held->lost &&
               network_wait_readable_until(events, address_of deadline) > 0 &&
               netlink_receive(events, address_of message, null) > 0)
                for (positive at = 0; at + NETLINK_HEADER <= message.used;)
                {
                        netlink_header address_to header =
                            (netlink_header address_to)(message.bytes + at);

                        if (header->length < NETLINK_HEADER ||
                            at + header->length > message.used)
                                break;
                        at += netlink_align(header->length);
                        (void)net_link_event(header, held);
                }
        netlink_forget(address_of message);
}

typedef struct
{
        p32 gateway;
        p8 found;
        p32 flags;
} storage_test_route_facts;

static p8 storage_test_route_seen(netlink_header address_to header,
                                    address_any context)
{
        storage_test_route_facts address_to facts = context;
        netlink_route address_to route =
            netlink_message_body(header, sizeof(netlink_route));
        positive size = 0;
        p8 address_to gateway = netlink_find(header, sizeof(netlink_route),
                                             RTA_GATEWAY, address_of size);

        if (route && !route->destination_bits && gateway && size == 4 &&
            memory_load_unaligned(p32, gateway) ==
                network_order_32(facts->gateway))
        {
                facts->found = true;
                facts->flags = route->flags;
        }
        return true;
}

static bipolar storage_test_file_read(string_address path,
                                      p8 address_to into, positive room);

/* What a lease does about the resolver file it cannot write, and about an
   address the kernel took back at expiry. The first is a directory where the
   file belongs, so every write of it fails. Exit 4: the lease was rolled
   back over it; 5: the address was not the lease's; 6: it was not released
   with the file unwritable; 7: an address already expired could not be
   released; 8: nor a route; 9: an expired lease was held on by the watcher's
   own reconfiguration, which asks for a new one only when the release is
   done; 10: a second release found something to do or a refused one let go
   of what it still owned. */
static fn storage_test_lease_resolver(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                p8 hardware[6] = {2, 0, 0, 0, 0, 1};
                dhcp_lease lease = {.address = 0x0a090c09, .mask = 0xffffff00,
                                    .server = 0x0a090c01, .router = 0x0a090c01,
                                    .seconds = 60, .renewal = 30,
                                    .rebinding = 52};
                net_holding held = {0};
                storage_test_address_facts facts = {.host = lease.address};
                bipolar handle = storage_test_net_namespace();

                net_out = storage_test_capture;
                if (system_make_directory_at(AT_FDCWD, "/etc/resolv.conf",
                                             0755) < 0)
                        system_call_1(syscall(exit_group), 2);
                if (net_apply_lease((b32)handle, 1, "lo", hardware,
                                    address_of lease, address_of held,
                                    false) != 0 ||
                    held.index != 1 || !held.address_owned)
                        system_call_1(syscall(exit_group), 4);
                netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                             AF_INET, storage_test_address_seen,
                             address_of facts);
                if (!facts.found)
                        system_call_1(syscall(exit_group), 5);
                if (net_holding_release((b32)handle, address_of held) != 0 ||
                    held.index)
                        system_call_1(syscall(exit_group), 6);
                //      Nothing held is nothing to release, however often.
                if (net_holding_release((b32)handle, address_of held) != 0 ||
                    net_holding_release((b32)handle, null) != 0)
                        system_call_1(syscall(exit_group), 10);

                //      The kernel's own lifetime check removes a leased
                //      address at expiry, and the watcher then lets go of
                //      what it still holds.
                memory_fill(address_of held, 0, sizeof held);
                if (net_apply_lease((b32)handle, 1, "lo", hardware,
                                    address_of lease, address_of held,
                                    false) != 0 ||
                    netlink_address_delete((b32)handle, 1, lease.address,
                                           24) < 0)
                        system_call_1(syscall(exit_group), 5);
                if (net_holding_release((b32)handle, address_of held) != 0 ||
                    held.index)
                        system_call_1(syscall(exit_group), 7);

                memory_fill(address_of held, 0, sizeof held);
                if (net_apply_lease((b32)handle, 1, "lo", hardware,
                                    address_of lease, address_of held,
                                    false) != 0 ||
                    !held.route_owned ||
                    netlink_route_delete((b32)handle, 0, 0, lease.router, 1) < 0)
                        system_call_1(syscall(exit_group), 5);
                if (net_holding_release((b32)handle, address_of held) != 0 ||
                    held.index)
                        system_call_1(syscall(exit_group), 8);

                //      A release the kernel refuses keeps what it did not
                //      let go of: a socket that is not one is refused both.
                memory_fill(address_of held, 0, sizeof held);
                if (net_apply_lease((b32)handle, 1, "lo", hardware,
                                    address_of lease, address_of held,
                                    false) != 0 ||
                    !held.address_owned || !held.route_owned)
                        system_call_1(syscall(exit_group), 5);
                if (net_holding_release(-1, address_of held) == 0 ||
                    !held.index || !held.address_owned || !held.route_owned)
                        system_call_1(syscall(exit_group), 10);

                memory_fill(address_of held, 0, sizeof held);
                if (net_apply_lease((b32)handle, 1, "lo", hardware,
                                    address_of lease, address_of held,
                                    false) != 0 ||
                    netlink_address_delete((b32)handle, 1, lease.address,
                                           24) < 0)
                        system_call_1(syscall(exit_group), 5);
                held.lost = true;
                net_reconfigure((b32)handle, address_of held);
                system_call_1(syscall(exit_group), held.index ? 9 : 0);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "lease and resolver", "no namespaces"))
        {
                check("a lease is kept when its resolver file cannot be written",
                      status != 4 && status != 5);
                check("a lease is released when its resolver file cannot be written",
                      status != 6 && status != 5 && status != 4);
                check("a release done is done: nothing held is nothing to release",
                      status != 10 && status != 6);
                check("an address the kernel took back at expiry is released",
                      status != 7 && status != 6 && status != 5 && status != 4);
                check("a route that is already gone is released",
                      status != 8 && status != 7 && status != 6 && status != 5 &&
                          status != 4);
                check("the watcher asks for a new lease once the old is released",
                      status == 0);
        }
}

/* The parents a rename or a replacement is trusted in. The namespace has the
   caller for its only user, so the host's own directories belong to nobody in
   it: a directory that is not the caller's and not root's, which is every
   directory of an image whose build left them to the user who ran it. Below
   the tight tier the kernel's permissions are the whole answer, as they are
   for the reference; the tight tier also wants a parent no one else could
   exchange the names of. Exit 100 and the bits of the cases that failed. */
static fn storage_test_name_stable(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                file_facts entry;
                file_facts blank = {0};
                static const string_address parents[] = {
                    "/etc", "/etc/open", "/etc/sticky", "/", "/etc/file"};
                positive failed = 0;
                p8 trusted[5];
                bipolar made;

                storage_test_namespace(0);
                made = system_open_at_mode(AT_FDCWD, "/etc/file",
                                           FILE_WRITE | FILE_EXCLUSIVE |
                                               O_CLOEXEC, 0644);
                if (made < 0 ||
                    system_make_directory_exact_at(AT_FDCWD, "/etc/open",
                                                   0777) < 0 ||
                    system_make_directory_exact_at(AT_FDCWD, "/etc/sticky",
                                                   01777) < 0 ||
                    !file_look(AT_FDCWD, "/etc/file", AT_SYMLINK_NOFOLLOW,
                               address_of entry))
                        system_call_1(syscall(exit_group), 2);
                system_close(made);
                for (positive at = 0; at < 5; at++)
                {
                        bipolar parent = system_open_at(
                            AT_FDCWD, parents[at], FILE_READ | O_CLOEXEC);

                        trusted[at] = parent >= 0 &&
                                      file_name_stable(parent, address_of entry);
                        if (parent >= 0)
                                system_close(parent);
                }
                {
                        bipolar parent = system_open_at(
                            AT_FDCWD, "/etc", FILE_READ | O_CLOEXEC);

                        //      Facts that were not asked for are no answer.
                        if (parent >= 0 &&
                            file_name_stable(parent, address_of blank))
                                failed |= 32;
                        if (parent >= 0)
                                system_close(parent);
                }
                //      Its own directory, and a sticky one it has an entry in.
                failed |= (trusted[0] ? 0 : 1) | (trusted[2] ? 0 : 4);
                //      One anybody can rename in, and a user's other than ours.
#if MOONWATER_STRICT < STRICT_TIGHT
                failed |= (trusted[1] ? 0 : 2) | (trusted[3] ? 0 : 8);
#else
                failed |= (trusted[1] ? 2 : 0) | (trusted[3] ? 8 : 0);
#endif
                //      Never a file.
                failed |= trusted[4] ? 16 : 0;
                system_call_1(syscall(exit_group), failed ? 100 + failed : 0);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "name stability", "no namespaces"))
        {
                positive failed = status >= 100 ? (positive)status - 100
                                  : status ? 63 : 0;

                check("a parent of the caller's own is trusted at every tier",
                      !(failed & 1));
                check("a sticky parent holding the caller's entry is trusted at every tier",
                      !(failed & 4));
#if MOONWATER_STRICT < STRICT_TIGHT
                check("a parent that is another user's is trusted as the reference trusts it",
                      !(failed & 8));
                check("a parent anybody may write is trusted as the reference trusts it",
                      !(failed & 2));
#else
                check("a parent that is another user's is refused by the tight tier",
                      !(failed & 8));
                check("a parent anybody may write, with no sticky bit, is refused by the tight tier",
                      !(failed & 2));
#endif
                check("a file is never a parent", !(failed & 16));
                check("facts that were not read are never a stable name", !(failed & 32));
        }
}

/* The second writer of a name. A resolver file is replaced by every lease
   after the first, and a restarted watcher meets the one its predecessor
   wrote: in a directory the build left to somebody else (the namespace's
   /tmp, which is nobody's) that was refused with Permission denied whatever
   the tier asked, and the tight tier is the one that still may. */
static fn storage_test_replace_foreign(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                p8 path[64] = "/tmp/moonwater-net-foreign-";
                p8 bytes[64];
                positive used = string_length((string_address)path);
                bipolar first;
                bipolar second;
                bipolar got;
                static p8 wanted[] = "nameserver 10.0.0.2\n";

                storage_test_namespace(0);
                used += positive_into(path + used, system_nonce());
                path[used] = end;
                first = net_write_resolv_to((string_address)path, 0x0a000001);
                second = net_write_resolv_to((string_address)path, 0x0a000002);
                got = storage_test_file_read((string_address)path, bytes,
                                             sizeof bytes);
                system_remove_at(AT_FDCWD, (string_address)path, 0);
#if MOONWATER_STRICT < STRICT_TIGHT
                if (first < 0 || second < 0)
                        system_call_1(syscall(exit_group), 4);
                system_call_1(syscall(exit_group),
                              got == sizeof(wanted) - 1 &&
                                      !memory_compare(bytes, wanted,
                                                      sizeof(wanted) - 1)
                                  ? 0 : 5);
#else
                //      Refused whole, or not written to begin with: never
                //      a file with the second writer's bytes in a parent that
                //      is nobody's.
                system_call_1(syscall(exit_group),
                              second >= 0 && first >= 0 ? 6 : 0);
#endif
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "replacement in another's directory",
                                  "no namespaces"))
        {
#if MOONWATER_STRICT < STRICT_TIGHT
                check("a resolver file is replaced in a directory that is another user's",
                      status != 4);
                check("and is what the second writer wrote", status == 0);
#else
                check("a resolver file is not replaced in a directory that is another user's",
                      status == 0);
#endif
        }
}

/* A /32 lease and its router off the prefix, as clouds hand them out, which
   dhcp_lease_usable takes: the default route goes in, through a router the
   kernel is told is on the link, and goes with the release; a /24 lease's
   router is on its prefix and is asked of the kernel as before. Exit bits
   over 16, so 2 and the threaded runner's 7 stay NOT RUN. */
static fn storage_test_lease_off_prefix(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                p8 hardware[6] = {2, 0, 0, 0, 0, 1};
                bipolar handle = storage_test_net_namespace();
                netlink_search link = {.wanted = (string_address) "wd3"};
                dhcp_lease host = {.address = 0x0a090d0d, .mask = 0xffffffff,
                                   .router = 0x0a090d01, .server = 0x0a090d01,
                                   .seconds = 60, .renewal = 30,
                                   .rebinding = 52};
                dhcp_lease wide = {.address = 0x0a090e0e, .mask = 0xffffff00,
                                   .router = 0x0a090e01, .server = 0x0a090e01,
                                   .seconds = 60, .renewal = 30,
                                   .rebinding = 52};
                storage_test_route_facts facts = {.gateway = host.router};
                net_holding held = {0};
                b32 wrong = 16;

                if (storage_test_dummy((b32)handle, "wd3") < 0 ||
                    netlink_link_find((b32)handle, address_of link) < 0 ||
                    netlink_link_up((b32)handle, link.index) < 0)
                        system_call_1(syscall(exit_group), 2);
                if (!dhcp_lease_usable(address_of host) ||
                    net_apply_lease((b32)handle, link.index, "wd3", hardware,
                                    address_of host, address_of held, false) != 0 ||
                    !held.route_owned ||
                    netlink_dump((b32)handle, RTM_GETROUTE, sizeof(netlink_route),
                                 AF_INET, storage_test_route_seen,
                                 address_of facts) < 0 ||
                    !facts.found || !(facts.flags & NETLINK_ROUTE_ONLINK))
                        wrong |= 1;
                facts.found = false;
                if (net_holding_release((b32)handle, address_of held) != 0 ||
                    netlink_dump((b32)handle, RTM_GETROUTE, sizeof(netlink_route),
                                 AF_INET, storage_test_route_seen,
                                 address_of facts) < 0 ||
                    facts.found)
                        wrong |= 2;
                facts.gateway = wide.router;
                if (net_apply_lease((b32)handle, link.index, "wd3", hardware,
                                    address_of wide, address_of held, false) != 0 ||
                    netlink_dump((b32)handle, RTM_GETROUTE, sizeof(netlink_route),
                                 AF_INET, storage_test_route_seen,
                                 address_of facts) < 0 ||
                    !facts.found || (facts.flags & NETLINK_ROUTE_ONLINK))
                        wrong |= 4;
                system_call_1(syscall(exit_group), wrong);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "lease off its prefix",
                                  "no namespaces or dummy links"))
        {
                check("a /32 lease installs its default route through a router on the link",
                      status >= 16 && !(status & 1));
                check("and the release takes that route away",
                      status >= 16 && !(status & 2));
                check("a router on the lease's prefix is asked of the kernel as before",
                      status >= 16 && !(status & 4));
        }
}

/*
        Lease epochs against the kernel.

        Every sequence of events from an alphabet, two deep (three with
        -DSTORAGE_EPOCH_DEPTH=3), each in a namespace of its own on a dummy
        link: a lease applied as the watcher applies one (a first lease, a
        renewal that moves the router, the resolver, the address, the prefix
        to a /32 with its router off it, a lease with no router, another
        server's lease) beside an address put on the link by hand, each of
        those again with a router the kernel refuses as a gateway (multicast,
        which fails after the address went in; a resolver file that cannot be
        written no longer fails a lease), and the release.
        After every event the kernel's addresses and default routes on the
        link and /etc/resolv.conf are read back and held to what the lease
        the watcher now holds says, and nothing else: an applied lease is
        all of the new lease's address, route and resolver, a refused one
        leaves every one of them as they were before it (no old route with a
        new resolver, no new address beside the old route), and a release
        leaves no lease's address or route and the fallback resolver. The
        first sequence that breaks one is printed, with the event, which part
        and the state.
*/
#ifndef STORAGE_EPOCH_DEPTH
#define STORAGE_EPOCH_DEPTH 2
#endif
#define STORAGE_EPOCH_LEASES 7
#define STORAGE_EPOCH_EVENTS (2 * STORAGE_EPOCH_LEASES + 1)
#define STORAGE_EPOCH_ROOM 8
//      An address somebody put on the link by hand, which no lease owns and
//      every event must leave; it also keeps the link holding an address
//      when a lease's goes, so the kernel does not sweep a route the
//      watcher forgot.
#define STORAGE_EPOCH_OPERATOR 0x0a091e1e

typedef struct
{
        p32 index;
        positive addresses;
        p32 address[STORAGE_EPOCH_ROOM];
        p8 prefix[STORAGE_EPOCH_ROOM];
        positive routes;
        p32 router[STORAGE_EPOCH_ROOM];
        p32 resolver;
} storage_epoch_state;

static p8 storage_epoch_address(netlink_header address_to header,
                                  address_any context)
{
        storage_epoch_state address_to state = context;
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        p8 address_to named = netlink_find(header, sizeof(netlink_address),
                                           IFA_LOCAL, address_of size);

        if (body && named && size == 4 && body->index == state->index &&
            state->addresses < STORAGE_EPOCH_ROOM)
        {
                state->address[state->addresses] =
                    network_order_32(memory_load_unaligned(p32, named));
                state->prefix[state->addresses++] = body->prefix;
        }
        return true;
}

static p8 storage_epoch_route(netlink_header address_to header,
                                address_any context)
{
        storage_epoch_state address_to state = context;
        netlink_route address_to body =
            netlink_message_body(header, sizeof(netlink_route));
        positive size = 0;
        p8 address_to gateway = netlink_find(header, sizeof(netlink_route),
                                             RTA_GATEWAY, address_of size);
        positive out_size = 0;
        p8 address_to out = netlink_find(header, sizeof(netlink_route), RTA_OIF,
                                         address_of out_size);

        if (body && !body->destination_bits && body->table == RT_TABLE_MAIN &&
            gateway && size == 4 && out && out_size == 4 &&
            memory_load_unaligned(p32, out) == state->index &&
            state->routes < STORAGE_EPOCH_ROOM)
                state->router[state->routes++] =
                    network_order_32(memory_load_unaligned(p32, gateway));
        return true;
}

static p8 storage_epoch_read(b32 handle, p32 index,
                               storage_epoch_state address_to state)
{
        p8 text[128];
        bipolar got;

        memory_zero(state, sizeof *state);
        state->index = index;
        if (netlink_dump(handle, RTM_GETADDR, sizeof(netlink_address), AF_INET,
                         storage_epoch_address, state) < 0 ||
            netlink_dump(handle, RTM_GETROUTE, sizeof(netlink_route), AF_INET,
                         storage_epoch_route, state) < 0)
                return false;
        got = file_slurp_once_at(AT_FDCWD, "/etc/resolv.conf", text,
                                 sizeof text - 1);
        if (got >= 11 && !memory_compare(text, "nameserver ", 11))
        {
                positive at = 11;
                positive stop = at;
                bipolar host;

                text[got] = 0;
                while (stop < (positive)got && text[stop] != '\n')
                        stop++;
                text[stop] = 0;
                host = string_to_host((string_address)(text + at));
                state->resolver = host >= 0 ? (p32)host : 1;
        }
        return true;
}

static p8 storage_epoch_same(const storage_epoch_state address_to a,
                               const storage_epoch_state address_to b)
{
        if (a->addresses != b->addresses || a->routes != b->routes ||
            a->resolver != b->resolver)
                return false;
        for (positive at = 0; at < a->addresses; at++)
        {
                p8 found = false;

                for (positive other = 0; other < b->addresses; other++)
                        found |= a->address[at] == b->address[other] &&
                                 a->prefix[at] == b->prefix[other];
                if (!found)
                        return false;
        }
        for (positive at = 0; at < a->routes; at++)
        {
                p8 found = false;

                for (positive other = 0; other < b->routes; other++)
                        found |= a->router[at] == b->router[other];
                if (!found)
                        return false;
        }
        return true;
}

//      What the kernel should hold for what the watcher holds.
static fn storage_epoch_expected(const net_holding address_to held, p32 index,
                                 p32 resolver_before,
                                 storage_epoch_state address_to want)
{
        memory_zero(want, sizeof *want);
        want->index = index;
        want->address[want->addresses] = STORAGE_EPOCH_OPERATOR;
        want->prefix[want->addresses++] = 24;
        if (held->index)
        {
                want->address[want->addresses] = held->lease.address;
                want->prefix[want->addresses++] = dhcp_prefix_of(held->lease.mask);
                if (held->lease.router)
                        want->router[want->routes++] = held->lease.router;
                want->resolver = held->lease.nameserver ? held->lease.nameserver
                                                        : DNS_FALLBACK;
        }
        else
                want->resolver = resolver_before ? DNS_FALLBACK : 0;
}

static fn storage_epoch_say(const p8 address_to events, positive depth,
                            positive step, string_address what,
                            const storage_epoch_state address_to have)
{
        string_format(log, "  epoch: events");
        for (positive at = 0; at < depth; at++)
                string_format(log, " %p", (positive)events[at]);
        string_format(log, ", after event %p: %s; have %p addresses, %p routes, resolver %p\n",
                      step, what, have->addresses, have->routes,
                      (positive)have->resolver);
        log_flush();
}

static b32 storage_epoch_walk(const p8 address_to events, positive depth)
{
        static const dhcp_lease leases[STORAGE_EPOCH_LEASES] = {
            //      the first lease
            {0x0a09140a, 0xffffff00, 0x0a091401, 0x0a091401, 0x0a091401, 600, 300, 525},
            //      the router moved
            {0x0a09140a, 0xffffff00, 0x0a091402, 0x0a091401, 0x0a091401, 600, 300, 525},
            //      the resolver moved
            {0x0a09140a, 0xffffff00, 0x0a091401, 0x0a091403, 0x0a091401, 600, 300, 525},
            //      another address
            {0x0a09140b, 0xffffff00, 0x0a091401, 0x0a091401, 0x0a091401, 600, 300, 525},
            //      a /32 with its router off it
            {0x0a09140a, 0xffffffff, 0x0a091401, 0x0a091401, 0x0a091401, 600, 300, 525},
            //      no router at all
            {0x0a09140a, 0xffffff00, 0, 0x0a091401, 0x0a091401, 600, 300, 525},
            //      another server's lease on another network
            {0x0a09150c, 0xffffff00, 0x0a091501, 0x0a091505, 0x0a091501, 600, 300, 525}};
        p8 hardware[6] = {2, 0, 0, 0, 0, 1};
        bipolar handle = storage_test_net_namespace();
        netlink_search link = {.wanted = (string_address) "wd4"};
        net_holding held = {0};

        if (storage_test_dummy((b32)handle, "wd4") < 0 ||
            netlink_link_find((b32)handle, address_of link) < 0 ||
            netlink_link_up((b32)handle, link.index) < 0 ||
            netlink_address_add((b32)handle, link.index, STORAGE_EPOCH_OPERATOR,
                                24) < 0)
                return 2;
        for (positive step = 0; step < depth; step++)
        {
                p8 event = events[step];
                storage_epoch_state before;
                storage_epoch_state after;
                storage_epoch_state want;
                net_holding was = held;
                b32 refused = 0;

                if (!storage_epoch_read((b32)handle, link.index, address_of before))
                        return 2;
                if (event == 2 * STORAGE_EPOCH_LEASES)
                        refused = net_holding_release((b32)handle, address_of held) ? 1 : 0;
                else
                {
                        p8 fault = event >= STORAGE_EPOCH_LEASES;
                        dhcp_lease lease = leases[event % STORAGE_EPOCH_LEASES];

                        //      A multicast router is one the kernel refuses
                        //      as a gateway, after the address went in: a
                        //      lease that fails part way.
                        if (fault)
                                lease.router = 0xe0000001;
                        refused = net_apply_lease((b32)handle, link.index, "wd4",
                                                  hardware, address_of lease,
                                                  address_of held, false);
                        if (fault != (refused != 0))
                        {
                                storage_epoch_say(events, depth, step,
                                                  fault ? "a refused route did not fail the lease"
                                                         : "a lease failed to apply",
                                                  address_of before);
                                return 16 | 1;
                        }
                }
                if (!storage_epoch_read((b32)handle, link.index, address_of after))
                        return 2;
                if (refused)
                {
                        if (held.lost ||
                            memory_compare(address_of held, address_of was,
                                           sizeof held))
                        {
                                storage_epoch_say(events, depth, step,
                                                  "a refused lease changed what is held",
                                                  address_of after);
                                return 16 | 2;
                        }
                        if (!storage_epoch_same(address_of after, address_of before))
                        {
                                storage_epoch_say(events, depth, step,
                                                  "a refused lease left the kernel or the resolver changed",
                                                  address_of after);
                                return 16 | 4;
                        }
                        continue;
                }
                storage_epoch_expected(address_of held, link.index,
                                       before.resolver, address_of want);
                if (!storage_epoch_same(address_of after, address_of want))
                {
                        storage_epoch_say(events, depth, step,
                                          held.index ? "the kernel and resolver are not the held lease's"
                                                     : "a release left a lease behind",
                                          address_of after);
                        return 16 | 8;
                }
        }
        return 16;
}

static fn storage_test_lease_epochs(void)
{
        p8 events[STORAGE_EPOCH_DEPTH];
        positive total = 1;
        positive walked = 0;
        b32 failed = 0;
        b32 status = 0;

        for (positive at = 0; at < STORAGE_EPOCH_DEPTH; at++)
                total *= STORAGE_EPOCH_EVENTS;
        for (positive sequence = 0; sequence < total; sequence++)
        {
                positive rest = sequence;
                bipolar child;

                for (positive at = 0; at < STORAGE_EPOCH_DEPTH; at++)
                {
                        events[at] = (p8)(rest % STORAGE_EPOCH_EVENTS);
                        rest /= STORAGE_EPOCH_EVENTS;
                }
                child = system_fork();
                if (child == 0)
                        system_call_1(syscall(exit_group),
                                      storage_epoch_walk(events,
                                                         STORAGE_EPOCH_DEPTH));
                status = storage_test_child_status(child);
                if (status == 2 || status == STORAGE_TEST_THREADED)
                        break;
                walked++;
                if (status != 16)
                        failed |= status < 16 ? 16 | 15 : status;
        }
        if (!storage_test_not_run(status == 2 || status == STORAGE_TEST_THREADED
                                      ? status
                                      : 0,
                                  "lease epochs", "no namespaces or dummy links"))
        {
                check("every lease sequence that applies leaves the kernel and the resolver the held lease's alone",
                      walked == total && !(failed & 8) && !(failed & 1));
                check("every refused lease leaves what is held, the kernel and the resolver as they were",
                      walked == total && !(failed & 6));
        }
}

/* The probe before a new lease, on a link nobody answers on: quiet after
   its three windows; cut at once by a byte on the wake pipe or by link news,
   as an exchange is (a cable pulled while it listens is not a quiet link);
   and never performed on a link that is not there. The child says which by
   bits over 16, so 2 and the threaded runner's 7 stay NOT RUN. */
static fn storage_test_arp_probe(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                bipolar handle = storage_test_net_namespace();
                netlink_search link = {.wanted = (string_address) "wd2"};
                p8 hardware[6] = {2, 0, 0, 0, 0, 1};
                b32 ends[2];
                bipolar events;
                bipolar quiet, woken, news, missing;
                positive began, quiet_took, woken_took;
                b32 wrong = 16;

                if (storage_test_dummy((b32)handle, "wd2") < 0 ||
                    netlink_link_find((b32)handle, address_of link) < 0 ||
                    netlink_link_up((b32)handle, link.index) < 0 ||
                    system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) < 0 ||
                    (events = netlink_open_groups(RTNLGRP_LINK_MASK)) < 0)
                        system_call_1(syscall(exit_group), 2);
                began = clock_monotonic_nanoseconds();
                quiet = net_arp_conflict(link.index, hardware, 0x0a090c0c);
                quiet_took = clock_monotonic_nanoseconds() - began;
                system_write_all((positive)ends[1], "x", 1);
                net_wake_watch = ends[0];
                began = clock_monotonic_nanoseconds();
                woken = net_arp_conflict(link.index, hardware, 0x0a090c0c);
                woken_took = clock_monotonic_nanoseconds() - began;
                net_wake_watch = -1;
                net_events_watch = events;
                net_news_holdoff_until = 0;
                storage_test_link_set((b32)handle, link.index, IFLA_MTU, 1400);
                news = net_arp_conflict(link.index, hardware, 0x0a090c0c);
                net_events_watch = -1;
                missing = net_arp_conflict(link.index + 1000, hardware,
                                           0x0a090c0c);
                if (quiet != 0 || quiet_took < 590000000)
                        wrong |= 1;
                if (woken != -2 || woken_took > 100000000 || news != -2)
                        wrong |= 2;
                if (missing != -1)
                        wrong |= 4;
                system_call_1(syscall(exit_group), wrong);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "ARP probe",
                                  "no namespaces or dummy links"))
        {
                check("a probe nobody answers listens its three windows and finds the address free",
                      status >= 16 && !(status & 1));
                check("the wake pipe and link news cut a probe at once",
                      status >= 16 && !(status & 2));
                check("a probe on a link that is not there is not performed",
                      status >= 16 && !(status & 4));
        }
}

/* A cable pulled and put back while the watcher was busy: both events are
   read after the carrier is back, and only the kernel's count of carrier
   losses says the link went away -- perhaps into another network. Other
   news about the link (an MTU change) is not a loss. Exit 4: an MTU change
   lost the lease; 5: the bounce did not. */
static fn storage_test_carrier_bounce(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                bipolar handle = storage_test_net_namespace();
                netlink_search link = {.wanted = (string_address) "wd0"};
                net_holding held = {0};
                bipolar events;

                if (storage_test_dummy((b32)handle, "wd0") < 0 ||
                    netlink_link_find((b32)handle, address_of link) < 0 ||
                    netlink_link_up((b32)handle, link.index) < 0)
                        system_call_1(syscall(exit_group), 2);
                //      A kernel, or an emulator, that keeps no count is
                //      not a failure of the watcher's.
                if (netlink_link_find((b32)handle, address_of link) < 0 ||
                    !link.carrier_counted ||
                    (events = netlink_open_groups(RTNLGRP_LINK_MASK)) < 0)
                        system_call_1(syscall(exit_group), 2);
                held.index = link.index;
                held.lease.address = 0x0a090b0b;
                held.lease.seconds = 60;
                held.carrier_counted = link.carrier_counted;
                held.carrier_downs = link.carrier_downs;
                string_copy_max_end(held.name, "wd0", IFNAME_SIZE - 1);
                storage_test_link_set((b32)handle, link.index, IFLA_MTU, 1400);
                storage_test_link_news((b32)events, address_of held, 1500000000);
                if (held.lost)
                        system_call_1(syscall(exit_group), 4);
                storage_test_link_set((b32)handle, link.index,
                                      STORAGE_TEST_IFLA_CARRIER, 0);
                storage_test_link_set((b32)handle, link.index,
                                      STORAGE_TEST_IFLA_CARRIER, 1);
                storage_test_link_news((b32)events, address_of held, 3000000000);
                system_call_1(syscall(exit_group), held.lost ? 0 : 5);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "carrier bounce",
                                 "no namespaces or dummy links"))
        {
                check("other news about the held link keeps the lease",
                      status != 4 && status != 1);
                check("a carrier lost and back while the watcher was busy loses the lease",
                      status == 0);
        }
        netlink_forget(address_of net_states);
}

/* Link news faster than the watcher reads it: the kernel drops what does
   not fit and says ENOBUFS, which must not end the watcher -- it asks
   again instead, and forgets every carrier snapshot, where reading the
   news would have kept one for the link. Exit 4: the watcher gave up; 5:
   it read on as if nothing were lost. */
static fn storage_test_link_news_overrun(void)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                bipolar handle = storage_test_net_namespace();
                netlink_search link = {.wanted = (string_address) "wd1"};
                netlink_buffer message = {0};
                net_holding held = {0};
                b32 small = 1;
                bipolar events = netlink_open_groups(RTNLGRP_LINK_MASK);

                if (events < 0 || storage_test_dummy((b32)handle, "wd1") < 0 ||
                    netlink_link_find((b32)handle, address_of link) < 0 ||
                    socket_option_set((b32)events, SOL_SOCKET, SO_RCVBUF,
                                      address_of small, sizeof small) < 0)
                        system_call_1(syscall(exit_group), 2);
                //      Every MTU change is news at once, where carrier news
                //      waits on linkwatch and folds together.
                for (positive flip = 0; flip < 64; flip++)
                        storage_test_link_set((b32)handle, link.index, IFLA_MTU,
                                              flip & 1 ? 1400 : 1500);
                //      Gone again, so the resync finds nothing to ask a
                //      lease for.
                {
                        netlink_buffer request = {0};
                        p32 sequence = netlink_sequence_take();

                        if (netlink_begin(address_of request, RTM_DELLINK,
                                          NLM_REQUEST | NLM_ACK, sequence,
                                          sizeof(netlink_link)))
                        {
                                ((netlink_link address_to)netlink_body(
                                     address_of request))->index = link.index;
                                netlink_transact((b32)handle, address_of request,
                                                 sequence, null, null);
                        }
                }
                if (net_watch_events((b32)events, address_of message,
                                     address_of held) < 0)
                        system_call_1(syscall(exit_group), 4);
                system_call_1(syscall(exit_group), net_states.used ? 5 : 0);
        }
        b32 status = storage_test_child_status(child);
        if (!storage_test_not_run(status, "link news overrun",
                                 "no namespaces or dummy links"))
                check("dropped link news resyncs the watcher instead of ending it",
                      status == 0);
}

static fn storage_test_netlink_output(void)
{
        writer saved = net_out;
        netlink_header short_message = {.length = NETLINK_HEADER};

        storage_test_output_used = 0;
        net_out = storage_test_capture;
        net_link_line(address_of short_message, null);
        net_address_line(address_of short_message, null);
        net_route_line(address_of short_message, null);
        check("header-only netlink messages are not dereferenced or printed",
              storage_test_output_used == 0);

        {
                netlink_buffer message = {0};
                p8 unsafe[] = {'e', 27, 0};

                check("netlink link output fixture builds",
                      netlink_begin(address_of message, RTM_NEWLINK, 0, 1,
                                    sizeof(netlink_link)));
                if (!message.failed)
                {
                        netlink_link address_to body =
                            (netlink_link address_to)netlink_body(
                                address_of message);
                        body->index = 3;
                        netlink_attribute_add(address_of message,
                                              IFLA_IFNAME, unsafe,
                                              sizeof unsafe);
                        storage_test_output_used = 0;
                        net_link_line((netlink_header address_to)message.bytes,
                                      null);
                        static p8 wanted[] =
                            "3: e\\x1b: <> state DOWN\n";
                        check("link names are terminal escaped",
                              storage_test_output_used == sizeof wanted - 1 &&
                                  !memory_compare(storage_test_output, wanted,
                                                  sizeof wanted - 1));
                        netlink_buffer failed = {.used = positive_max};
                        check("route name growth overflow poisons its staged table",
                              !net_name_seen(
                                  (netlink_header address_to)message.bytes,
                                  address_of failed) && failed.failed);
                }
                netlink_forget(address_of message);
        }

        {
                netlink_buffer message = {0};
                p32 wire = network_order_32(0x0a000001);
                p8 unsafe[] = {'e', 27, 0};

                check("netlink address output fixture builds",
                      netlink_begin(address_of message, RTM_NEWADDR, 0, 1,
                                    sizeof(netlink_address)));
                if (!message.failed)
                {
                        netlink_address address_to body =
                            (netlink_address address_to)netlink_body(
                                address_of message);
                        body->family = AF_INET;
                        body->prefix = 24;
                        body->index = 9;
                        netlink_attribute_add(address_of message, IFA_LOCAL,
                                              address_of wire, sizeof wire);
                        netlink_attribute_add(address_of message, IFA_LABEL,
                                              unsafe, sizeof unsafe);
                        storage_test_output_used = 0;
                        net_address_line(
                            (netlink_header address_to)message.bytes, null);
                        static p8 wanted[] =
                            "9: e\\x1b    inet 10.0.0.1/24\n";
                        check("address labels are terminal escaped",
                              storage_test_output_used == sizeof wanted - 1 &&
                                  !memory_compare(storage_test_output, wanted,
                                                  sizeof wanted - 1));

                        positive label_size = 0;
                        p8 address_to label = (p8 address_to)netlink_find(
                            (netlink_header address_to)message.bytes,
                            sizeof(netlink_address), IFA_LABEL,
                            address_of label_size);
                        if (label && label_size)
                                label[label_size - 1] = 'x';
                        storage_test_output_used = 0;
                        net_address_line(
                            (netlink_header address_to)message.bytes, null);
                        static p8 unknown[] =
                            "9: ?    inet 10.0.0.1/24\n";
                        check("unterminated address labels are not read",
                              storage_test_output_used ==
                                      sizeof unknown - 1 &&
                                  !memory_compare(storage_test_output, unknown,
                                                  sizeof unknown - 1));
                }
                netlink_forget(address_of message);
        }

        {
                netlink_buffer message = {0};
                p8 one = 1;

                check("malformed route output fixture builds",
                      netlink_begin(address_of message, RTM_NEWROUTE, 0, 1,
                                    sizeof(netlink_route)));
                if (!message.failed)
                {
                        netlink_route address_to body =
                            (netlink_route address_to)netlink_body(
                                address_of message);
                        body->family = AF_INET;
                        body->table = RT_TABLE_MAIN;
                        netlink_attribute_add(address_of message, RTA_GATEWAY,
                                              address_of one, 1);
                        storage_test_output_used = 0;
                        net_route_line(
                            (netlink_header address_to)message.bytes, null);
                        check("wrong-width route attributes are not read",
                              storage_test_output_used == 0);
                }
                netlink_forget(address_of message);
        }

        {
                netlink_buffer next = {0};

                check("prior route names reserve room",
                      net_room(address_of net_names, sizeof(net_name)));
                if (!net_names.failed)
                {
                        net_name address_to old =
                            (net_name address_to)net_names.bytes;
                        old->index = 3;
                        string_copy(old->name, "old");
                        net_names.used = sizeof(net_name);
                }

                check("replacement route names reserve room",
                      net_room(address_of next, sizeof(net_name)));
                if (!next.failed)
                {
                        net_name address_to fresh =
                            (net_name address_to)next.bytes;
                        fresh->index = 4;
                        string_copy(fresh->name, "new");
                        next.used = sizeof(net_name);
                }
                check("a failed link dump preserves the complete prior name table",
                      !net_names_commit(address_of next, -1) &&
                          net_name_of(3) &&
                          string_equals(net_name_of(3), "old") &&
                          !net_name_of(4));

                check("failed route names reserve room",
                      net_room(address_of next, sizeof(net_name)));
                if (!next.failed)
                {
                        next.used = sizeof(net_name);
                        next.failed = true;
                }
                check("a failed name callback preserves the complete prior table",
                      !net_names_commit(address_of next, 0) &&
                          net_name_of(3) &&
                          string_equals(net_name_of(3), "old"));

                check("successful route names reserve room",
                      net_room(address_of next, sizeof(net_name)));
                if (!next.failed)
                {
                        net_name address_to fresh =
                            (net_name address_to)next.bytes;
                        fresh->index = 4;
                        string_copy(fresh->name, "new");
                        next.used = sizeof(net_name);
                }
                check("only a complete name table is published",
                      net_names_commit(address_of next, 0) &&
                          !net_name_of(3) &&
                          net_name_of(4) &&
                          string_equals(net_name_of(4), "new"));
                netlink_forget(address_of net_names);
        }

        {
                netlink_buffer message = {0};
                p32 index = 7;

                check("route name table fixture reserves room",
                      net_room(address_of net_names, sizeof(net_name)));
                if (!net_names.failed)
                {
                        net_name address_to entry =
                            (net_name address_to)net_names.bytes;
                        entry->index = index;
                        entry->name[0] = 'd';
                        entry->name[1] = 27;
                        entry->name[2] = 0;
                        net_names.used = sizeof(net_name);
                }

                check("netlink route output fixture builds",
                      netlink_begin(address_of message, RTM_NEWROUTE, 0, 1,
                                    sizeof(netlink_route)));
                if (!message.failed && net_names.used == sizeof(net_name))
                {
                        netlink_route address_to body =
                            (netlink_route address_to)netlink_body(
                                address_of message);
                        body->family = AF_INET;
                        body->table = RT_TABLE_MAIN;
                        netlink_attribute_add(address_of message, RTA_OIF,
                                              address_of index,
                                              sizeof index);
                        storage_test_output_used = 0;
                        net_route_line(
                            (netlink_header address_to)message.bytes, null);
                        static p8 wanted[] = "default dev d\\x1b\n";
                        check("route device names are terminal escaped",
                              storage_test_output_used == sizeof wanted - 1 &&
                                  !memory_compare(storage_test_output, wanted,
                                                  sizeof wanted - 1));
                }
                netlink_forget(address_of message);
                netlink_forget(address_of net_names);
        }

        {
                socket_address_netlink source = {.family = AF_NETLINK,
                                                 .port = 0};

                check("kernel netlink sender is accepted",
                      netlink_source_is_kernel(address_of source,
                                               sizeof source));
                source.port = 9;
                check("userspace netlink sender is refused",
                      !netlink_source_is_kernel(address_of source,
                                                sizeof source));
                source.port = 0;
                check("short netlink sender address is refused",
                      !netlink_source_is_kernel(address_of source,
                                                sizeof source - 1));
        }

        check("ip command abbreviations stay within the full word",
              net_word_is((string_address) "add", "address", 1) &&
                  !net_word_is((string_address) "address-too-long",
                               "address", 1));

        net_out = saved;
}

static bipolar storage_test_file_read(string_address path,
                                      p8 address_to into, positive room)
{
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_CLOEXEC);
        bipolar got = handle < 0
                          ? handle
                          : system_read_retry((positive)handle, into, room);

        if (handle >= 0)
                system_close(handle);
        return got;
}

static fn storage_test_wget_304(string_address target)
{
        static p8 answer[] = "HTTP/1.1 304 Not Modified\r\n"
                             "Content-Length: 9\r\n"
                             "\r\n"
                             "forbidden";
        socket_address_internet where = {0};
        p32 where_size = sizeof where;
        b32 one = 1;
        bipolar listening = socket_new(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

        check("304 preservation server opens", listening >= 0);
        if (listening < 0)
                return;
        where.family = AF_INET;
        where.host = network_order_32(HOST_LOOPBACK);
        check("304 preservation server binds",
              socket_option_set((b32)listening, SOL_SOCKET, SO_REUSEADDR,
                                address_of one, sizeof one) == 0 &&
                  socket_bind((b32)listening, address_of where,
                              sizeof where) == 0 &&
                  socket_listen((b32)listening, 1) == 0 &&
                  socket_name((b32)listening, address_of where,
                              address_of where_size) == 0 &&
                  where_size == sizeof where && where.port);
        if (!where.port)
        {
                socket_close((b32)listening);
                return;
        }

        bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
        check("304 preservation server starts", child >= 0);
        if (!child)
        {
                bipolar client = socket_accept((b32)listening, 0, 0, 0);

                if (client >= 0)
                {
                        p8 request[256];
                        socket_receive((b32)client, request, sizeof request,
                                       0, 0, 0);
                        system_write_all((positive)client, answer,
                                         sizeof answer - 1);
                        socket_close((b32)client);
                }
                socket_close((b32)listening);
                system_call_1(syscall(exit_group), client < 0 ? 1 : 0);
        }
        if (child < 0)
        {
                socket_close((b32)listening);
                return;
        }

        socket_close((b32)listening);
        {
                p8 url[64];
                positive used = sizeof("http://127.0.0.1:") - 1;
                string_address address_to saved_words = program_words;
                b32 saved_count = program_words_count;
                positive raw = 0;
                //      qemu-user arches get --emulated from test/run. wget's
                //      network half runs in a confined child (net_apart),
                //      which qemu-user cannot filter, so there wget refuses
                //      to fetch at all and the server is never asked.
                p8 emulated = program_argument_count() > 1 &&
                                !string_compare(program_argument(1),
                                                (string_address) "--emulated");
                b32 said;

                memory_copy(url, "http://127.0.0.1:", used);
                used += positive_into(url + used,
                                      network_order_16(where.port));
                url[used++] = '/';
                url[used] = end;
                string_address words[] = {
                    "wget", "-q", "-O", target, url, null};
                program_arguments_use(words, 5);
                said = net_wget();
                if (saved_words)
                        program_arguments_use(saved_words, saved_count);
                else
                        program_arguments_own();
                if (emulated)
                {
                        check("under qemu-user wget refuses to fetch unconfined",
                              said == 1);
                        system_call_2(syscall(kill), (positive)child, SIGKILL);
                        system_wait4_retry((b32)child, null, 0, null);
                        log_direct(str("storage: wget 304 refusal NOT RUN -- qemu-user cannot confine wget\n"));
                }
                else
                {
                        check("wget refuses a terminal 304", said == 8);
                        check("304 preservation server completes",
                              system_wait4_retry((b32)child, address_of raw, 0,
                                                 null) == child &&
                                  wait_status_code(raw) == 0);
                }
        }
}

//      A stage as a writer of that pid would have left it: its name, and in
//      it, for a directory, the file it was making.
static fn storage_test_stage(p8 address_to into, p8 address_to root, positive pid,
                             string_address tail, p8 directory)
{
        p8 object[FILE_PATH_MAX];
        p8 address_to at = string_copy_end(into, root);

        at = string_copy_end(at, (string_address) "/.moonwater-stage-");
        if (pid)
                at += positive_into_string(at, pid);
        string_copy_end(at, tail);
        if (!directory)
        {
                system_close(system_open_at_mode(AT_FDCWD, into, FILE_WRITE | O_CLOEXEC, 0600));
                return;
        }
        system_make_directory_at(AT_FDCWD, into, 0700);
        if (file_path_join(object, into, SYSTEM_PATH_STAGE_LEAF))
                system_close(system_open_at_mode(AT_FDCWD, object, FILE_WRITE | O_CLOEXEC, 0600));
}

static fn storage_test_net_files(void)
{
        p8 root[FILE_PATH_MAX] = {0};
        p8 target[FILE_PATH_MAX] = {0};
        p8 retained[FILE_PATH_MAX] = {0};
        p8 raced_directory[FILE_PATH_MAX] = {0};
        p8 raced[FILE_PATH_MAX] = {0};
        p8 pinned_directory[FILE_PATH_MAX] = {0};
        p8 pinned_directory_slash[FILE_PATH_MAX] = {0};
        p8 directory_link[FILE_PATH_MAX] = {0};
        p8 endpoint_leaf[FILE_PATH_MAX] = {0};
        p8 bytes[128];
        positive used = string_copy_end(
                            root,
                            (string_address) "/tmp/moonwater-net-stage-") -
                        root;
        positive_into_string(root + used, system_nonce());

        check("network staging test directory is private",
              system_make_directory_at(AT_FDCWD, root, 0700) == 0);
        check("network staging target path fits",
              file_path_join(target, root, (string_address) "result"));
        check("pinned directory fixture path fits",
              file_path_join(pinned_directory, root,
                             (string_address)"directory"));
        check("pinned directory link path fits",
              file_path_join(directory_link, root,
                             (string_address)"directory-link"));
        positive pinned_length = string_length(pinned_directory);
        check("pinned directory slash path fits",
              pinned_length + 1 < sizeof(pinned_directory_slash));
        memory_copy_apart(pinned_directory_slash, pinned_directory,
                          pinned_length);
        pinned_directory_slash[pinned_length] = '/';
        pinned_directory_slash[pinned_length + 1] = end;
        check("pinned directory fixture is created",
              system_make_directory_at(AT_FDCWD, pinned_directory,
                                       0700) == 0);
        check("pinned directory link fixture is created",
              system_symbolic_link_at((string_address)"directory",
                                      AT_FDCWD, directory_link) == 0);

        bipolar pinned = system_open_directory_nofollow(
            AT_FDCWD, pinned_directory);
        check("directory pin accepts an ordinary path", pinned >= 0);
        if (pinned >= 0)
                system_close(pinned);
        pinned = system_open_directory_nofollow(
            AT_FDCWD, pinned_directory_slash);
        check("directory pin accepts a trailing slash", pinned >= 0);
        if (pinned >= 0)
                system_close(pinned);
        pinned = system_open_directory_nofollow(
            AT_FDCWD, (string_address)".");
        check("directory pin accepts dot", pinned >= 0);
        if (pinned >= 0)
                system_close(pinned);
        check("directory pin refuses a final symlink",
              system_open_directory_nofollow(
                  AT_FDCWD, directory_link) < 0);

        bipolar seed = system_open_at_mode(
            AT_FDCWD, target, FILE_WRITE | O_CLOEXEC, 0644);
        check("network staging fixture opens", seed >= 0);
        check("network staging fixture writes",
              seed >= 0 && system_write_all((positive)seed, "old", 3) == 3);
        if (seed >= 0)
                system_close(seed);

        storage_test_wget_304(target);
        bipolar got = storage_test_file_read(target, bytes, sizeof bytes);
        check("a terminal 304 preserves wget's existing destination",
              got == 3 && !memory_compare(bytes, "old", 3));

        file_facts endpoint = {0};
        p8 endpoint_known = file_look(
            AT_FDCWD, target, AT_SYMLINK_NOFOLLOW,
            address_of endpoint);
        check("direct endpoint fixture has complete facts",
              endpoint_known);
        bipolar endpoint_parent = file_parent_open(target, endpoint_leaf);
        check("an owned endpoint in a private parent is authorized",
              endpoint_known && endpoint_parent >= 0 &&
                  file_direct_endpoint_authorized(
                      endpoint_parent, address_of endpoint));
        p32 endpoint_owner = endpoint.owner;
        endpoint.owner = endpoint_owner == 1 ? 2 : 1;
        check("a cross-principal direct endpoint is refused",
              endpoint_known && endpoint_parent >= 0 &&
                  !file_direct_endpoint_authorized(
                      endpoint_parent, address_of endpoint));
        endpoint.owner = endpoint_owner;
        if (endpoint_parent >= 0)
                system_close(endpoint_parent);

        file_staged_name link_stage;
        bipolar link_output = file_staged_name_open(
            address_of link_stage, directory_link, 0644,
            FILE_STAGED_STREAM_SPECIAL);
#if MOONWATER_STRICT < STRICT_TIGHT
        //      A name that is a link is written through, as GNU's open does,
        //      so a link to a directory is the directory and is refused;
        //      only the tight tier replaces the link.
        check("a stream output through a link to a directory is refused",
              link_output == -ERROR_IS_DIRECTORY);
#else
        check("stream staging never follows a final symlink",
              link_output >= 0 && !link_stage.direct);
        check("stream symlink replacement writes only to its private stage",
              link_output >= 0 &&
                  system_write_all((positive)link_output, "safe", 4) == 4);
        check("stream symlink replacement publishes atomically",
              link_output >= 0 &&
                  file_staged_name_finish(
                      address_of link_stage, true, 0) == 0);
#endif

        file_staged_name staged;
        bipolar handle = file_staged_name_open(
            address_of staged, target, 0644, 0);
        check("network publication opens an adjacent exclusive stage",
              handle >= 0);
        check("network publication writes through its pinned descriptor",
              handle >= 0 &&
                  system_write_all((positive)handle, "new", 3) == 3);
        check("network publication atomically replaces the destination",
              handle >= 0 &&
                  file_staged_name_publish(address_of staged, true) == 0);
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("network publication exposes only completed bytes",
              got == 3 && !memory_compare(bytes, "new", 3));

        handle = file_staged_name_open(
            address_of staged, target, 0644, 0);
        check("pre-publication sync failure stage opens", handle >= 0);
        if (handle >= 0)
        {
                system_write_all((positive)handle, "early", 5);
                storage_test_net_syncs = 0;
                storage_test_net_sync_failure = 1;
                check("pre-publication sync failure is reported",
                      file_staged_name_publish(address_of staged, true) ==
                          -ERROR_INPUT_OUTPUT);
                storage_test_net_sync_failure = 0;
        }
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("pre-publication sync failure preserves the old name",
              got == 3 && !memory_compare(bytes, "new", 3));

        handle = file_staged_name_open(
            address_of staged, target, 0644, 0);
        check("post-publication sync failure stage opens", handle >= 0);
        if (handle >= 0)
        {
                system_write_all((positive)handle, "late", 4);
                storage_test_net_syncs = 0;
                storage_test_net_sync_failure = 2;
                check("post-publication sync failure remains committed",
                      file_staged_name_publish(address_of staged, true) == 0 &&
                          storage_test_net_syncs == 2);
                storage_test_net_sync_failure = 0;
        }
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("post-publication sync failure exposes the new name",
              got == 4 && !memory_compare(bytes, "late", 4));

        handle = file_staged_name_open(
            address_of staged, target, 0644, 0);
        check("post-publication close failure stage opens", handle >= 0);
        if (handle >= 0)
        {
                system_write_all((positive)handle, "close", 5);
                storage_test_net_close_failure = staged.directory;
                check("post-publication directory close remains committed",
                      file_staged_name_publish(address_of staged, true) == 0 &&
                          storage_test_net_close_failure == -1);
                storage_test_net_close_failure = -1;
        }
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("post-publication close failure exposes the new name",
              got == 5 && !memory_compare(bytes, "close", 5));

        handle = file_staged_name_open(
            address_of staged, target, 0644, 0);
        check("failed network publication has a private stage", handle >= 0);
        if (handle >= 0)
        {
                check("failed network publication stage path fits",
                      file_path_join(retained, root,
                                     staged.protected.private_name));
                system_write_all((positive)handle, "partial", 7);
                file_staged_name_abort(address_of staged);
        }
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("abandoned network output preserves the caller destination",
              got == 5 && !memory_compare(bytes, "close", 5));
        got = retained[0]
                  ? storage_test_file_read(retained, bytes, sizeof bytes)
                  : -1;
        check("failed staging cleanup removes only its own inode",
              got == -ERROR_NO_ENTRY);

        handle = file_staged_name_open(
            address_of staged, target, 0644, 0);
        check("identity-race network stage opens", handle >= 0);
        if (handle >= 0)
        {
                system_write_all((positive)handle, "ours", 4);
                check("identity-race private directory path fits",
                      file_path_join(raced_directory, root,
                                     staged.protected.private_name));
                check("identity-race stage path fits",
                      file_path_join(raced, raced_directory,
                                     SYSTEM_PATH_STAGE_LEAF));
                check("identity-race fixture displaces the named stage",
                      system_remove_at(staged.protected.directory,
                                       SYSTEM_PATH_STAGE_LEAF, 0) == 0);
                bipolar attacker = system_open_at_mode(
                    staged.protected.directory, SYSTEM_PATH_STAGE_LEAF,
                    FILE_WRITE | FILE_EXCLUSIVE | O_CLOEXEC, 0644);
                check("identity-race replacement opens", attacker >= 0);
                if (attacker >= 0)
                {
                        system_write_all((positive)attacker, "other", 5);
                        system_close(attacker);
                }
                check("a swapped staging name cannot be published",
                      file_staged_name_publish(address_of staged, true) ==
                          -ERROR_AGAIN);
        }
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("a staging identity race preserves the destination",
              got == 5 && !memory_compare(bytes, "close", 5));
        got = raced[0]
                  ? storage_test_file_read(raced, bytes, sizeof bytes)
                  : -1;
        check("a staging identity race retains the untrusted replacement",
              got == 5 && !memory_compare(bytes, "other", 5));

        check("fresh resolver mode fixture removes the old destination",
              system_remove_at(AT_FDCWD, target, 0) == 0);
        bipolar previous_mask = system_call_1(syscall(umask), 0077);
        bipolar resolver_written = net_write_resolv_to(target, 0x0a000001);
        if (previous_mask >= 0)
                (void)system_call_1(syscall(umask), (positive)previous_mask);
        check("resolver contents publish through the same atomic path",
              previous_mask >= 0 && resolver_written == 0);
        file_facts resolver_facts;
        //      The machine's resolvers, read by every user's lookups: 0644 under
        //      a root shell's umask 077 as under any other (c8b1bd32). This row
        //      held it to the umask, 0600, which is the bug that commit fixed.
        check("a fresh resolver file is 0644 under a restrictive umask, readable by every user",
              file_look(AT_FDCWD, target, AT_SYMLINK_NOFOLLOW,
                        address_of resolver_facts) &&
                  (resolver_facts.mode & 0777) == 0644);
        static p8 wanted[] = "nameserver 10.0.0.1\n";
        got = storage_test_file_read(target, bytes, sizeof bytes);
        check("resolver writes are complete: the network's resolver, and nothing behind it",
              got == sizeof(wanted) - 1 &&
                  !memory_compare(bytes, wanted, sizeof(wanted) - 1));
        {
                static p8 alone[] = "nameserver 1.1.1.1\n";

                check("a network with no resolver of its own gets the public one alone",
                      net_write_resolv_to(target, 0) == 0 &&
                          storage_test_file_read(target, bytes, sizeof bytes) ==
                              sizeof(alone) - 1 &&
                          !memory_compare(bytes, alone, sizeof(alone) - 1));
                check("and one that names the public resolver itself is not asked twice",
                      net_write_resolv_to(target, DNS_FALLBACK) == 0 &&
                          storage_test_file_read(target, bytes, sizeof bytes) ==
                              sizeof(alone) - 1 &&
                          !memory_compare(bytes, alone, sizeof(alone) - 1));
                {
                        static const p32 three[4] = {0x09090909, 0x08080404,
                                                     0x01000001, 0x0a000001};
                        static p8 listed[] = "nameserver 9.9.9.9\n"
                                             "nameserver 8.8.4.4\n"
                                             "nameserver 1.0.0.1\n";

                        check("kept servers are written in the order they were given, three at most",
                              net_write_resolv_servers(target, three, 4) == 0 &&
                                  storage_test_file_read(target, bytes, sizeof bytes) ==
                                      sizeof(listed) - 1 &&
                                  !memory_compare(bytes, listed, sizeof(listed) - 1));
                }
                {
                        static const struct
                        {
                                string_address text;
                                positive count;
                                p32 first;
                                p32 last;
                        } kept_case[] = {
                            {"9.9.9.9\n", 1, 0x09090909, 0x09090909},
                            {"9.9.9.9 8.8.4.4\n", 2, 0x09090909, 0x08080404},
                            {"9.9.9.9 8.8.4.4 1.0.0.1\n", 3, 0x09090909, 0x01000001},
                            {"  9.9.9.9   8.8.4.4  \r\n", 2, 0x09090909, 0x08080404},
                            {"127.0.0.1\n", 1, 0x7f000001, 0x7f000001},
                            /* all of it or none of it: a file with one bad word
                               in it is not the half that reads */
                            {"9.9.9.9 8.8.4.4 1.0.0.1 1.1.1.1\n", 0, 0, 0},
                            {"9.9.9.9 nope\n", 0, 0, 0},
                            {"9.9.9.9 8.8.4\n", 0, 0, 0},
                            {"9.9.9.9 8.8.4.4.4\n", 0, 0, 0},
                            {"0.0.0.0\n", 0, 0, 0},
                            {"0.1.2.3\n", 0, 0, 0},
                            {"224.0.0.1\n", 0, 0, 0},
                            {"255.255.255.255\n", 0, 0, 0},
                            {"-1\n", 0, 0, 0},
                            {"2001:db8::1\n", 0, 0, 0},
                            {"1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1 "
                             "1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1 1.1.1.1\n", 0, 0, 0},
                            {"", 0, 0, 0},
                        };
                        p8 held = true;
                        p32 chosen[DNS_SERVERS_MAX];
                        //      The default list's file, beside the person's, and none there.
                        p8 standard[FILE_PATH_MAX];

                        string_copy_bounded((string_address)standard, (string_address)target, sizeof standard);
                        string_append_bounded((string_address)standard, ".default", sizeof standard);
                        system_remove_at(AT_FDCWD, (string_address)standard, 0);

                        for (positive at = 0; at < array_count(kept_case); at++)
                        {
                                positive count;

                                file_publish_bytes(target, kept_case[at].text,
                                                   string_length(kept_case[at].text), 0644, false);
                                count = net_dns_kept_at(target, chosen);
                                held &= count == kept_case[at].count &&
                                        (!count || (chosen[0] == kept_case[at].first &&
                                                    chosen[count - 1] == kept_case[at].last));
                        }
                        check("/root/dns is up to three usable addresses, and a file with anything else is none", held);

                        file_publish_bytes(target, "9.9.9.9\n", 8, 0644, false);
                        held = net_resolver_choose_at(target, standard, true, 0x0a000001, chosen) == 1 && chosen[0] == 0x09090909;
                        file_publish_bytes(target, "nope\n", 5, 0644, false);
                        held &= net_resolver_choose_at(target, standard, true, 0x0a000001, chosen) == 1 && chosen[0] == 0x0a000001;
                        held &= net_resolver_choose_at(target, standard, true, 0, chosen) == 1 && chosen[0] == DNS_FALLBACK;
                        system_remove_at(AT_FDCWD, target, 0);
                        held &= net_dns_kept_at(target, chosen) == 0;
                        held &= net_resolver_choose_at(target, standard, true, 0x0a000001, chosen) == 1 && chosen[0] == 0x0a000001;
                        check("dns auto: kept servers win over a lease's, a lease's over the public one, and a file that does not read is not a choice", held);

                        //      Moonwater's default: 1.1.1.1 and 1.0.0.1, then the lease's server; a kept list still wins.
                        file_publish_bytes(target, "9.9.9.9\n", 8, 0644, false);
                        held = net_resolver_choose_at(target, standard, false, 0x0a000001, chosen) == 1 && chosen[0] == 0x09090909;
                        file_publish_bytes(target, "nope\n", 5, 0644, false);
                        held &= net_resolver_choose_at(target, standard, false, 0x0a000001, chosen) == 3 && chosen[0] == 0x01010101 &&
                                chosen[1] == 0x01000001 && chosen[2] == 0x0a000001;
                        held &= net_resolver_choose_at(target, standard, false, 0, chosen) == 2 && chosen[0] == 0x01010101 &&
                                chosen[1] == 0x01000001;
                        held &= net_resolver_choose_at(target, standard, false, 0x01000001, chosen) == 2 && chosen[1] == 0x01000001;
                        system_remove_at(AT_FDCWD, target, 0);
                        held &= net_resolver_choose_at(target, standard, false, 0x0a000001, chosen) == 3 && chosen[2] == 0x0a000001;
                        check("the default: kept servers win, else 1.1.1.1 and 1.0.0.1 with the lease's behind them, named once", held);

                        //      The default list is the person's to change: what dns default kept is asked first, the
                        //      lease's server behind it while there is room, and a file that does not read is the pair.
                        file_publish_bytes(standard, "9.9.9.9\n", 8, 0644, false);
                        held = net_resolver_choose_at(target, standard, false, 0x0a000001, chosen) == 2 &&
                               chosen[0] == 0x09090909 && chosen[1] == 0x0a000001;
                        file_publish_bytes(standard, "9.9.9.9 8.8.8.8 8.8.4.4\n", 24, 0644, false);
                        held &= net_resolver_choose_at(target, standard, false, 0x0a000001, chosen) == 3 &&
                                chosen[2] == 0x08080404;
                        file_publish_bytes(standard, "nope\n", 5, 0644, false);
                        held &= net_resolver_choose_at(target, standard, false, 0, chosen) == 2 &&
                                chosen[0] == 0x01010101 && chosen[1] == 0x01000001;
                        system_remove_at(AT_FDCWD, (string_address)standard, 0);
                        check("dns default: the kept list first and the lease's behind it while there is room, and a file that does not read is 1.1.1.1 and 1.0.0.1", held);

                        //      A name of the network's own goes to the network's server first.
                        held = dns_name_internal("printer") && dns_name_internal("nas.") &&
                               dns_name_internal("nas.local") && dns_name_internal("NAS.LAN") &&
                               dns_name_internal("a.home.arpa") && dns_name_internal("db.internal") &&
                               dns_name_internal("box.localdomain") && !dns_name_internal("example.com") &&
                               dns_name_internal("local") && !dns_name_internal("thelan.com") &&
                               !dns_name_internal("lan.example") && !dns_name_internal("") &&
                               !dns_name_internal(".") && dns_server_public(0x01010101) &&
                               !dns_server_public(0x0a000001);
                        check("a one-label name and one under .local, .lan, .internal or .home.arpa is the network's, and no other", held);

                        //      Internal names are the person's too: off routes none, the suffixes kept are the only
                        //      ones, and the default list's servers are public as the table's are.
                        {
                                p8 lines[DNS_INTERNAL_ROOM];
                                p8 suffixes[NET_DNS_SUFFIXES_ROOM];
                                const p32 defaults[2] = {0xd043dede, 0x09090909};

                                held = net_dns_internal_compose(true, DNS_INTERNAL_SUFFIXES, defaults, 0, lines) &&
                                       dns_name_internal_in("nas.local", lines) && dns_name_internal_in("printer", lines) &&
                                       dns_name_internal_in("box.localdomain", lines) &&
                                       !dns_name_internal_in("example.com", lines);
                                held &= net_dns_internal_compose(false, DNS_INTERNAL_SUFFIXES, defaults, 2, lines) &&
                                        !dns_name_internal_in("printer", lines) && !dns_name_internal_in("nas.local", lines) &&
                                        dns_server_public_in(0xd043dede, lines) && !dns_server_public_in(0x0a000001, lines);
                                held &= net_dns_suffixes_normal((const p8 address_to) ".Corp .home-net.example", 23,
                                                                suffixes) == 2 &&
                                        string_equals((string_address)suffixes, ".corp .home-net.example") &&
                                        net_dns_internal_compose(true, (string_address)suffixes, defaults, 1, lines) &&
                                        dns_name_internal_in("db.CORP", lines) && !dns_name_internal_in("nas.local", lines) &&
                                        dns_name_internal_in("printer", lines);
                                held &= !net_dns_suffixes_normal((const p8 address_to) "lan", 3, suffixes) &&
                                        !net_dns_suffixes_normal((const p8 address_to) ".a..b", 5, suffixes) &&
                                        !net_dns_suffixes_normal((const p8 address_to) ".a .a", 5, suffixes) &&
                                        !net_dns_suffixes_normal((const p8 address_to) ".a;b", 4, suffixes) &&
                                        !net_dns_suffixes_normal((const p8 address_to) ".", 1, suffixes) &&
                                        !net_dns_suffixes_normal((const p8 address_to) ".a .b .c .d .e .f .g .h .i", 26,
                                                                 suffixes);
                                check("dns internal: off routes no name, the kept suffixes are the only ones, a one-label name "
                                      "stays the network's, and a server of the default list is public",
                                      held);
                        }
                }
                //      The count is kept only while a failure is planned;
                //      one that is never reached counts.
                storage_test_net_sync_failure = 99;
                storage_test_net_syncs = 0;
                check("a file published without the syncs is the same bytes and asks for none",
                      file_publish_bytes(target, "plain\n", 6, 0644, false) == 0 &&
                          storage_test_net_syncs == 0 &&
                          storage_test_file_read(target, bytes, sizeof bytes) == 6 &&
                          !memory_compare(bytes, "plain\n", 6));
                storage_test_net_syncs = 0;
                check("and with them asks for the bytes' and the directory's",
                      file_publish_bytes(target, "synced", 6, 0644, true) == 0 &&
                          storage_test_net_syncs == 2 &&
                          storage_test_file_read(target, bytes, sizeof bytes) == 6 &&
                          !memory_compare(bytes, "synced", 6));
                storage_test_net_sync_failure = 0;
                {
                        p8 dead[FILE_PATH_MAX];
                        p8 alive[FILE_PATH_MAX];
                        p8 foreign[FILE_PATH_MAX];
                        p8 plain[FILE_PATH_MAX];
                        p8 object[FILE_PATH_MAX];
                        positive gone = 0;
                        bipolar child = system_fork();

                        if (!child)
                                system_call_1(syscall(exit_group), 0);
                        system_wait4_retry(child, address_of gone, 0, null);

                        storage_test_stage(dead, root, (positive)child, "-1", true);
                        storage_test_stage(alive, root, (positive)system_call(syscall(getpid)),
                                           "-2", true);
                        storage_test_stage(foreign, root, 0, "x-3", true);
                        storage_test_stage(plain, root, (positive)child, "-4", false);
                        check("a stage left by a writer that is gone is a private directory with "
                              "its file",
                              file_path_join(object, dead, SYSTEM_PATH_STAGE_LEAF) &&
                                  system_access_at(AT_FDCWD, object, 0) == 0);
                        check("a file published without the syncs takes nothing away",
                              file_publish_bytes(target, "plain\n", 6, 0644, false) == 0 &&
                                  system_access_at(AT_FDCWD, dead, 0) == 0);
                        check("one with them takes away the stage of a writer that is gone, and "
                              "only that",
                              file_publish_bytes(target, "swept", 5, 0644, true) == 0 &&
                                  system_access_at(AT_FDCWD, dead, 0) < 0 &&
                                  system_access_at(AT_FDCWD, alive, 0) == 0 &&
                                  system_access_at(AT_FDCWD, foreign, 0) == 0 &&
                                  system_access_at(AT_FDCWD, plain, 0) == 0 &&
                                  storage_test_file_read(target, bytes, sizeof bytes) == 5 &&
                                  !memory_compare(bytes, "swept", 5));
                        file_facts published;
                        bipolar held;

                        check("a state file written over a readable one has the mode asked for",
                              file_publish_with(target, "private", 7, 0600, true,
                                                FILE_STAGED_EXACT_MODE) == 0 &&
                                  file_look(AT_FDCWD, target, AT_SYMLINK_NOFOLLOW,
                                            address_of published) &&
                                  (published.mode & 07777) == 0600);
                        held = system_open_at(AT_FDCWD, target, FILE_READ | O_CLOEXEC);
                        check("where file_publish_bytes leaves it the mode it had",
                              held >= 0 &&
                                  system_call_2(syscall(fchmod), (positive)held, 0644) == 0 &&
                                  file_publish_bytes(target, "again", 5, 0600, true) == 0 &&
                                  file_look(AT_FDCWD, target, AT_SYMLINK_NOFOLLOW,
                                            address_of published) &&
                                  (published.mode & 07777) == 0644);
                        if (held >= 0)
                                system_close(held);
                        file_path_join(object, alive, SYSTEM_PATH_STAGE_LEAF);
                        system_remove_at(AT_FDCWD, object, 0);
                        system_remove_at(AT_FDCWD, alive, AT_REMOVEDIR);
                        file_path_join(object, foreign, SYSTEM_PATH_STAGE_LEAF);
                        system_remove_at(AT_FDCWD, object, 0);
                        system_remove_at(AT_FDCWD, foreign, AT_REMOVEDIR);
                        system_remove_at(AT_FDCWD, plain, 0);
                }
                check("only an answer about the name ends the walk over resolvers",
                      dns_answer_is_final(DNS_OK) &&
                          dns_answer_is_final(DNS_NO_SUCH_NAME) &&
                          dns_answer_is_final(DNS_NO_ADDRESS) &&
                          !dns_answer_is_final(DNS_NO_REPLY) &&
                          !dns_answer_is_final(DNS_REFUSED) &&
                          !dns_answer_is_final(DNS_MALFORMED) &&
                          !dns_answer_is_final(DNS_NO_SERVER));
        }

        p8 unsafe[] = {'h', 'o', 's', 't', 27, 0};
        storage_test_output_used = 0;
        string_format(storage_test_capture, "bad %w\n",
                      writer_terminal_quoted_name, unsafe);
        /* The control byte spelled the way the diagnostic writer spells it
           since e1e04487 took GNU's octal escapes, rather than the \x1b this
           asked for until then: what matters here is that it is spelled. */
        static p8 escaped[] = "bad host\\033\n";
        check("network diagnostics escape terminal control bytes",
              storage_test_output_used == sizeof(escaped) - 1 &&
                  !memory_compare(storage_test_output, escaped,
                                  sizeof(escaped) - 1));

        system_remove_at(AT_FDCWD, target, 0);
        if (retained[0])
                system_remove_at(AT_FDCWD, retained, 0);
        if (raced[0])
                system_remove_at(AT_FDCWD, raced, 0);
        if (raced_directory[0])
                system_remove_at(AT_FDCWD, raced_directory, AT_REMOVEDIR);
        system_remove_at(AT_FDCWD, directory_link, 0);
        system_remove_at(AT_FDCWD, pinned_directory, AT_REMOVEDIR);
        system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
}

b32 main(void)
{
#ifdef STORAGE_EPOCH_ONLY
        //      net_epoch_mutants builds this alone, once for each mutant.
        storage_test_lease_epochs();
        return test_report(null);
#endif
        storage_test_read(1, 32);
        storage_test_read(2, 7);
        storage_test_read(3, 0);
        storage_test_read(4, 7);
        storage_test_exact();
        storage_test_directory();
        storage_test_replay();
        utility_test_decimal();
        storage_test_elf();
        storage_test_copy();
        storage_test_copy_faults();
        storage_test_lsfd();
        storage_test_consumed_mounts();
        storage_test_findmnt();
        storage_test_script_rollback();
        storage_test_link_state();
        storage_test_lease_clock_origin();
        storage_test_lease_asleep();
        storage_test_dhcp_apart();
        storage_test_decline_hold();
        storage_test_arp_claims();
        storage_test_lease_over_existing();
        storage_test_lease_inherited();
        storage_test_lease_resolver();
        storage_test_name_stable();
        storage_test_replace_foreign();
        storage_test_arp_probe();
        storage_test_lease_off_prefix();
        storage_test_lease_epochs();
        storage_test_carrier_bounce();
        storage_test_link_news_overrun();
        storage_test_netlink_output();
        storage_test_net_files();
        return test_report(null);
}
#endif /* CHECK_storage_io */

#ifdef CHECK_storage_format
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        What an install writes, asked before any disk is handed to it.

        The arithmetic runs on every machine: both checksums against their
        published check values, and the ext4 layout across a grammar of sizes
        -- each group count with its last group at the sizes mke2fs keeps and
        drops, the sparse backup groups, random sizes up to the 16 TiB
        ceiling and a block either side of both ends. Given a directory it
        also writes images there, and the install lane hands those to e2fsck
        and sfdisk, which are the judges of the bytes.
*/
static fn format_layout_holds(p64 asked)
{
        storage_ext4_plan plan;
        p8 made = storage_ext4_layout(address_of plan,
                                        asked * STORAGE_EXT4_BLOCK);
        p64 last;
        p64 used = 0;

        check("ext4 layouts exist exactly between 64 MiB and 2^32 blocks",
              made == (asked >= STORAGE_EXT4_FEWEST &&
                       asked <= STORAGE_EXT4_MOST));
        if (!made)
                return;

        last = storage_ext4_group_blocks(address_of plan, plan.groups - 1);

        check("ext4 groups cover the blocks with none spare",
              plan.groups == (plan.blocks + STORAGE_EXT4_PER_GROUP - 1) /
                                 STORAGE_EXT4_PER_GROUP);
        check("ext4 only drops a last group, and only one",
              plan.blocks <= asked &&
                  asked - plan.blocks < STORAGE_EXT4_PER_GROUP);
        check("ext4 keeps a last group only with its metadata and fifty blocks",
              plan.groups == 1 ||
                  last >= storage_ext4_overhead(address_of plan,
                                                plan.groups - 1) + 50);
        check("ext4 inode tables are whole blocks and one bitmap block",
              plan.inodes_per_group % STORAGE_EXT4_INODES_PER_BLOCK == 0 &&
                  plan.inodes_per_group >= STORAGE_EXT4_INODES_PER_BLOCK &&
                  plan.inodes_per_group <= STORAGE_EXT4_BLOCK * 8 &&
                  plan.table_blocks * STORAGE_EXT4_INODES_PER_BLOCK ==
                      plan.inodes_per_group);
        check("ext4 descriptors fill their blocks without a spare one",
              (p64)plan.descriptor_blocks * STORAGE_EXT4_BLOCK >=
                      (p64)plan.groups * STORAGE_EXT4_DESCRIPTOR &&
                  ((p64)plan.descriptor_blocks - 1) * STORAGE_EXT4_BLOCK <
                      (p64)plan.groups * STORAGE_EXT4_DESCRIPTOR);
        check("ext4 journal follows the e2fsprogs size table up to 64 MiB",
              plan.journal_blocks == (plan.blocks < 32768           ? 1024
                                      : plan.blocks < 256 * 1024     ? 4096
                                      : plan.blocks < 512 * 1024     ? 8192
                                                                     : 16384));
        check("ext4 root and lost+found sit right after group 0's table",
              plan.root_block == storage_ext4_overhead(address_of plan, 0) &&
                  storage_ext4_used(address_of plan, 0) <=
                      storage_ext4_group_blocks(address_of plan, 0));
        check("ext4 journal is one extent inside its group's free blocks",
              plan.journal_group < plan.groups &&
                  plan.journal_start ==
                      (p64)plan.journal_group * STORAGE_EXT4_PER_GROUP +
                          storage_ext4_overhead(address_of plan,
                                                plan.journal_group) +
                          (plan.journal_group ? 0
                                              : 1 + STORAGE_EXT4_LOST_BLOCKS) &&
                  plan.journal_start + plan.journal_blocks <=
                      (p64)plan.journal_group * STORAGE_EXT4_PER_GROUP +
                          storage_ext4_group_blocks(address_of plan,
                                                    plan.journal_group));

        for (p64 group = 0; group < plan.groups; group++)
                used += storage_ext4_used(address_of plan, group);

        check("ext4 allocates no more blocks than it has", used <= plan.blocks);
}

static fn format_layouts(void)
{
        static const p64 shaped[] = {3, 5, 7, 9, 25, 27, 49, 81, 125, 243,
                                     343, 625, 729, 2187, 2401, 3125, 6561};
        static const p64 remainders[] = {1, 2, 49, 50, 51, 300, 469, 470,
                                         520, 521, 571, 1100, 1600, 32767,
                                         32768};
        p64 random = 0x9e3779b97f4a7c15;

        format_layout_holds(STORAGE_EXT4_FEWEST - 1);
        format_layout_holds(STORAGE_EXT4_FEWEST);
        format_layout_holds(STORAGE_EXT4_MOST);
        format_layout_holds(STORAGE_EXT4_MOST + 1);

        for (p64 groups = 1; groups <= 64; groups++)
                for (positive at = 0; at < array_count(remainders); at++)
                        format_layout_holds((groups - 1) * STORAGE_EXT4_PER_GROUP +
                                            remainders[at]);

        for (positive at = 0; at < array_count(shaped); at++)
                for (positive rest = 0; rest < array_count(remainders); rest++)
                        format_layout_holds(shaped[at] * STORAGE_EXT4_PER_GROUP +
                                            remainders[rest]);

        for (positive round = 0; round < 4096; round++)
        {
                XORSHIFT64(random);
                format_layout_holds(STORAGE_EXT4_FEWEST +
                                    random % (STORAGE_EXT4_MOST -
                                              STORAGE_EXT4_FEWEST + 1));
        }
}

static fn format_sums(void)
{
        static const p8 digits[] = "123456789";

        check("CRC-32C of 123456789 is its published check value",
              (hash_crc32c(~(p32)0, digits, 9) ^ ~(p32)0) == 0xe3069283);
        check("CRC-32 of 123456789 is its published check value",
              ~hash_crc32(~(p32)0, digits, 9) == 0xcbf43926);
        check("CRC-32C chains across a split",
              hash_crc32c(hash_crc32c(~(p32)0, digits, 4), digits + 4, 5) ==
                  hash_crc32c(~(p32)0, digits, 9));
        check("the backup superblock groups are 0, 1 and powers of 3, 5 and 7",
              storage_ext4_backup(0) && storage_ext4_backup(1) &&
                  storage_ext4_backup(3) && storage_ext4_backup(25) &&
                  storage_ext4_backup(343) && storage_ext4_backup(2187) &&
                  !storage_ext4_backup(2) && !storage_ext4_backup(15) &&
                  !storage_ext4_backup(21) && !storage_ext4_backup(6560));
}

static fn format_spans(void)
{
        p64 first;
        p64 last;

        check("a GPT on 512-byte sectors leaves 34 sectors at each end",
              storage_gpt_span(1 << 20, 512, address_of first, address_of last) &&
                  first == 34 && last == (1 << 20) - 34);
        check("a GPT on 4096-byte sectors leaves 6 sectors at each end",
              storage_gpt_span(1 << 20, 4096, address_of first, address_of last) &&
                  first == 6 && last == (1 << 20) - 6);
        check("a GPT refuses sector sizes it has no layout for",
              !storage_gpt_span(1 << 20, 1000, address_of first, address_of last) &&
                  !storage_gpt_span(1 << 20, 256, address_of first, address_of last) &&
                  !storage_gpt_span(1 << 20, 8192, address_of first, address_of last));
        check("a GPT refuses a disk too small for both tables",
              !storage_gpt_span(68, 512, address_of first, address_of last) &&
                  storage_gpt_span(69, 512, address_of first, address_of last));
}

/*
        Where an install puts its two partitions, on both sector sizes and at
        the edges of what it takes: 2 GiB to the byte, a sector either side,
        what the GPT's tail leaves of a MiB, and the 16 TiB an ext4 can
        address. Every row holds the same things: the system partition is
        512 MiB at 1 MiB, the data partition follows it and both begin on a
        MiB, and nothing goes past the GPT's last usable sector.
*/
static p8 place_holds(p64 bytes, b32 sector, storage_format_partition address_to parts)
{
        p64 align = ((p64)1 << 20) / (p64)sector;
        p64 first;
        p64 last;

        memory_zero(parts, 2 * sizeof(parts[0]));
        return host_install_place(bytes, sector, parts) &&
               storage_gpt_span(bytes / (p64)sector, (p32)sector, address_of first,
                                address_of last) &&
               parts[0].first == align &&
               parts[0].last - parts[0].first + 1 == ((p64)512 << 20) / (p64)sector &&
               parts[1].first == parts[0].last + 1 && parts[1].first % align == 0 &&
               parts[1].last <= last;
}

static fn format_places(void)
{
        static const b32 sizes[] = {512, 4096};

        for (positive at = 0; at < array_count(sizes); at++)
        {
                b32 sector = sizes[at];
                p64 align = ((p64)1 << 20) / (p64)sector;
                p64 two = (p64)2 << 30;
                p64 whole = ((p64)1 << 20) + ((p64)512 << 20);
                p64 data = 0;
                storage_format_partition exact[2];
                storage_format_partition other[2];

                check("an install takes a disk of 2 GiB to the byte, ending on a MiB with "
                      "less than two of the disk's own after it",
                      place_holds(two, sector, exact) && (exact[1].last + 1) % align == 0 &&
                          two / (p64)sector - 1 - exact[1].last < 2 * align);
                check("and not one a sector short of it, nor an empty one",
                      !host_install_place(two - sector, sector, other) &&
                          !host_install_place(0, sector, other));
                check("a sector, 8 KiB or half a sector over 2 GiB is the layout of 2 GiB",
                      place_holds(two + sector, sector, other) &&
                          !memory_compare(exact, other, sizeof(exact)) &&
                          place_holds(two + 8192, sector, other) &&
                          !memory_compare(exact, other, sizeof(exact)) &&
                          place_holds(two + sector / 2, sector, other) &&
                          !memory_compare(exact, other, sizeof(exact)));
                check("once the GPT's tail is paid for the next MiB is data, and so is "
                      "each whole MiB after",
                      place_holds(two + 24576, sector, other) &&
                          other[1].last - exact[1].last == align &&
                          place_holds(two + ((p64)1 << 20), sector, other) &&
                          other[1].last - exact[1].last == align);
                check("the data partition is cut at what an ext4 addresses",
                      place_holds(whole + ((p64)16 << 40) + ((p64)1 << 20), sector, other) &&
                          (other[1].last - other[1].first + 1) * (p64)sector ==
                              (p64)0xffffffff * 4096 &&
                          place_holds(whole + ((p64)64 << 40), sector, other) &&
                          (other[1].last - other[1].first + 1) * (p64)sector ==
                              (p64)0xffffffff * 4096);
                data = (p64)16 << 40;
                check("and is the rest of the disk, to the MiB, just under it",
                      place_holds(whole + data, sector, other) &&
                          (other[1].last + 1) % align == 0 &&
                          (other[1].last - other[1].first + 1) * (p64)sector ==
                              data - ((p64)1 << 20));
        }

        {
                storage_format_partition parts[2];

                check("sectors the GPT has no layout for are refused at any size",
                      !host_install_place((p64)4 << 30, 256, parts) &&
                          !host_install_place((p64)4 << 30, 1000, parts) &&
                          !host_install_place((p64)4 << 30, 8192, parts));
        }
}

/* One partition as the census is handed it. */
static fn census_one(host_census address_to census, string_address path,
                     string_address label, string_address uuid)
{
        storage_identity identity;

        memory_zero(address_of identity, sizeof(identity));
        identity.path = path;
        string_copy(identity.partlabel, label);
        identity.partlabel_length = string_length(label);
        string_copy(identity.partuuid, uuid);
        identity.partuuid_length = string_length(uuid);
        host_census_visit(address_of identity, census);
}

/*
        Where device list says a drive is (usb_drive_bus, usb_drive_external): read off its name, the sysfs path its
        /sys/class/block link names, its card's type for an MMC card, and its removable flag. An NVMe, a SATA and a virtio
        disk are internal; an SD card is external, and a soldered eMMC internal; a disk on any other bus is scsi, and any disk
        whose block device says it is removable is external.
*/
static fn device_where(void)
{
        static const struct
        {
                string_address name, link, type, bus;
                b32 removable, external;
        } drives[] = {
            {"nvme0n1", "../../devices/pci0000:00/0000:00:1d.0/0000:3d:00.0/nvme/nvme0/nvme0n1", "", "nvme", false, false},
            {"sda", "../../devices/pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda", "", "ata", false, false},
            {"sda", "../../devices/pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda", "", "ata", true, true},
            {"vda", "../../devices/pci0000:00/0000:00:04.0/virtio1/block/vda", "", "virtio", false, false},
            {"mmcblk0", "../../devices/platform/soc/fe340000.mmc/mmc_host/mmc0/mmc0:aaaa/block/mmcblk0", "SD", "sd", false, true},
            {"mmcblk1", "../../devices/platform/soc/fe330000.mmc/mmc_host/mmc1/mmc1:0001/block/mmcblk1", "MMC", "mmc", false, false},
            {"sdb", "../../devices/pci0000:00/0000:00:10.0/host2/target2:0:0/2:0:0:0/block/sdb", "", "scsi", false, false},
        };
        b32 right = true;

        for (positive at = 0; at < sizeof(drives) / sizeof(drives[0]); at++)
        {
                string_address bus = usb_drive_bus(drives[at].name, drives[at].link, drives[at].type);

                right &= string_equals(bus, drives[at].bus) && usb_drive_external(bus, drives[at].removable) == drives[at].external;
        }
        check("device list: an NVMe, SATA or virtio disk is internal, an SD card external and an eMMC internal, and a removable disk external", right);
}

/*
        What a part line of device list says is on a partition (usb_list_segment's fs tail): the file system's type, its label with
        its blanks plained, "-" for a type or a label there is none of, and the node last; free room has no tail. A label cut short
        by its room ends before a character it would cut in two (usb_plain_copy).
*/
static fn device_part_tail(void)
{
        p8 said[512];
        usb_segment stick, bare;
        bipolar kept = system_duplicate(1, 101, 0);
        bipolar handle = system_call_2(syscall(memfd_create), (positive)(string_address) "parts", 0);
        positive got = 0;
        p8 cut[8];

        memory_zero(&stick, sizeof(stick)), memory_zero(&bare, sizeof(bare));
        stick.fs = bare.fs = 1;
        string_copy(stick.type, "vfat"), usb_plain_copy(stick.label, sizeof(stick.label), (string_address) "MY STICK"), string_copy(stick.node, "sdb1");
        string_copy(bare.node, "sdb2");
        if (handle >= 0 && kept >= 0)
        {
                system_duplicate(handle, 1, 0);
                usb_list_segment(1048576, 31000000000ull, USB_ROLE_OTHER, (string_address) "Basic_data", &stick);
                usb_list_segment(31001048576ull, 4096, USB_ROLE_FREE, null, null);
                usb_list_segment(31001052672ull, 8192, USB_ROLE_LINUX, null, &bare);
                log_flush();
                system_duplicate(kept, 1, 0);
                got = storage_read(handle, said, sizeof(said) - 1, 0);
        }
        said[got] = end;
        if (kept >= 0)
                system_close(kept);
        if (handle >= 0)
                system_close(handle);
        check("device list: a partition's line ends in fs, its type, its label plained and its node; none of either is -; free room has no tail",
              string_equals((string_address)said, (string_address) "    part 1048576 31000000000 other Basic_data fs vfat MY_STICK sdb1\n"
                                                                    "    part 31001048576 4096 free -\n"
                                                                    "    part 31001052672 8192 linux - fs - - sdb2\n"));
        usb_plain_copy(cut, sizeof(cut), (string_address) "ab\xc3\xa9\xc3\xa9\xc3\xa9");
        check("device list: a label cut short by its room ends before the character it would cut in two", string_equals((string_address)cut, (string_address) "ab\xc3\xa9\xc3\xa9"));
        usb_plain_copy(cut, sizeof(cut), (string_address) "abcde\xc3\xa9z");
        check("device list: a label that fits to the last byte keeps its last character whole", string_equals((string_address)cut, (string_address) "abcde\xc3\xa9"));
}

/*
        A partition opened for the person at the machine (device open, writable, eject, tidy): why one is refused, read-only first
        and writable as a second step for the file systems that take it, the folder it is opened at, what open keeps, an eject,
        and a drive taken out let go of. The mounts here are tmpfs in the check's own mount namespace, under /tmp.
*/
static fn device_open_rows(void)
{
        static usb_open_facts good, bad;
        static const char link_serve[] = "moonwater\0link\0serve\0003", ssh_serve[] = "/moonwater\0ssh\0serve", verb[] = "moonwater\0device\0open\0sdb1\0";
        p8 root[96], data[96], name[64], folder[HOST_PATH_ROOM], kept[128], digits[24], path[HOST_PATH_ROOM];
        const usb_openable address_to vfat = usb_openable_of("vfat"), address_to ntfs = usb_openable_of("ntfs"), address_to ext4 = usb_openable_of("ext4");
        positive flags;
        string_address said[12];
        b32 refused = true, folders, tidied, ejected = false, busy = false;
        static usb_media media;

        memory_zero(&good, sizeof(good));
        good.block = good.session = 1, good.bytes = 31000000000ull, good.diskseq = 9;
        string_copy(good.disk, "sdb"), string_copy(good.kept, "nvme0n1"), string_copy(good.boot, "nvme0n1");
        string_copy(good.identity.type, "exfat"), good.identity.type_length = 5, string_copy(good.identity.label, "MY STICK");
        check("device open: a partition of a drive plugged in, as the list said it, in an install's session, at the machine, may be opened",
              !usb_open_judge(&good, 31000000000ull, "exfat"));
        bad = good, bad.session = 0, said[0] = usb_open_judge(&bad, 31000000000ull, "exfat");
        check("device open: a session that is no install's (a live stick, or one not settled) opens nothing", said[0] && string_find(said[0], "live"));
        bad = good, bad.afar = 1, said[1] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, string_copy(bad.disk, "nvme0n1"), said[2] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, bad.boot[0] = 0, string_copy(bad.kept, "sdb"), said[3] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, string_copy(bad.disk, "dm-0"), said[4] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, bad.guard = "holds a moonwater partition", said[5] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, bad.held = 1, said[6] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, string_copy(bad.identity.partlabel, "EFI system partition"), said[7] = usb_open_judge(&bad, 31000000000ull, "exfat");
        bad = good, string_copy(bad.identity.type, "swap"), bad.identity.type_length = 4, said[8] = usb_open_judge(&bad, 31000000000ull, "swap");
        bad = good, said[9] = usb_open_judge(&bad, 32000000000ull, "exfat"), said[10] = usb_open_judge(&bad, 31000000000ull, "vfat");
        bad = good, bad.block = 0, said[11] = usb_open_judge(&bad, 31000000000ull, "exfat");
        for (positive at = 1; at < 12; at++)
                refused &= said[at] != null;
        // A disc in an optical drive is a drive's whole, as a stick with no table is: opened (read-only) as the list said it.
        bad = good, string_copy(bad.disk, "sr0"), string_copy(bad.identity.type, "iso9660"), bad.identity.type_length = 7;
        check("device open: a disc of data in an optical drive may be opened; an optical drive is a drive the list says (no loop or RAID), and UDF opens beside ISO 9660",
              !usb_open_judge(&bad, 31000000000ull, "iso9660") && usb_optical("sr0") && !usb_optical("sda") && !usb_not_drive("sr1") && usb_not_drive("loop0") &&
                  usb_openable_of("udf") && !usb_openable_of("udf")->written);
        check("device open: refused over a link, on the drive this runs from or keeps, on no drive, by the storage guard, held by a mapped or RAID volume, an EFI partition, a file system it does not open, one the list did not say, and no block device",
              refused && string_find(said[2], "runs from") && string_find(said[3], "runs from") && string_equals(said[5], "holds a moonwater partition") &&
                  string_find(said[6], "RAID") && string_find(said[7], "EFI"));
        check("device open: a command run by a link's or ssh's listener is from afar; any other is not",
              usb_afar_words((const p8 address_to)link_serve, sizeof(link_serve)) && usb_afar_words((const p8 address_to)ssh_serve, sizeof(ssh_serve)) &&
                  !usb_afar_words((const p8 address_to)verb, sizeof(verb) - 1) && !usb_afar_words((const p8 address_to) "sh", 3));
        flags = usb_open_how(vfat, false, 1000, 100, data, sizeof(data));
        check("device open: read-only first, and never a set-id program, a device or a program from it; a FAT is the caller's, with masks of its own",
              flags == (MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC) && string_equals((string_address)data, (string_address) "uid=1000,gid=100,fmask=0177,dmask=0077"));
        flags = usb_open_how(ext4, true, 0, 0, data, sizeof(data));
        check("device open: writable is a second step, a remount that drops read-only and keeps the rest; ext4 has owners of its own",
              flags == (MS_REMOUNT | MS_NOSUID | MS_NODEV | MS_NOEXEC) && !data[0]);
        check("device open: ext4, btrfs, xfs, f2fs, vfat and exfat may be made writable, NTFS and ISO 9660 are only read, swap is not opened",
              ext4->written && usb_openable_of("btrfs")->written && usb_openable_of("xfs")->written && usb_openable_of("f2fs")->written && vfat->written &&
                  usb_openable_of("exfat")->written && ntfs && !ntfs->written && ntfs->owned && !usb_openable_of("iso9660")->written && !usb_openable_of("swap"));
        bad = good, bad.diskseq = 10;
        refused = usb_open_same(&good, &good) && !usb_open_same(&good, &bad);
        bad = good, string_copy(bad.identity.label, "OTHER"), refused = refused && !usb_open_same(&good, &bad);
        bad = good, bad.bytes++, refused = refused && !usb_open_same(&good, &bad);
        bad = good, string_copy(bad.identity.uuid, "1234-5678"), refused = refused && !usb_open_same(&good, &bad);
        check("device open: the look right before the mount and the one after it are held to the first: its disk's attachment, its size, label and UUID", refused);
        usb_media_name(name, sizeof(name), "MY STICK", "sdb1");
        folders = string_equals((string_address)name, (string_address) "MY STICK");
        usb_media_name(name, sizeof(name), "../a/b", "sdb1"), folders = folders && string_equals((string_address)name, (string_address) "_._a_b");
        usb_media_name(name, sizeof(name), "", "sdb1"), folders = folders && string_equals((string_address)name, (string_address) "sdb1");
        check("device open: the folder is the label, a slash and a leading dot of it underscores, and the node where there is no label", folders);

        // The folders and the mounts, under /tmp in this check's own namespace.
        digits[positive_into_string(digits, (positive)system_call(syscall(getpid)))] = end;
        string_join(root, sizeof(root), "/tmp/mw-media.", (string_address)digits);
        string_join(kept, sizeof(kept), (string_address)root, ".kept");
        folders = !usb_media_folder((string_address)root, "root", "MY STICK", 0, folder, sizeof(folder));
        string_join(path, sizeof(path), (string_address)root, "/root/MY STICK");
        {
                file_facts facts;
                folders = folders && string_equals((string_address)folder, (string_address)path) && file_look(AT_FDCWD, (string_address)folder, AT_SYMLINK_NOFOLLOW, &facts) &&
                          (facts.mode & 07777) == 0700 && facts.owner == 0;
        }
        string_join(path, sizeof(path), (string_address)root, "/root/linked");
        system_call_3(syscall(symlinkat), (positive) "/tmp", AT_FDCWD, (positive)path);
        folders = folders && usb_media_folder((string_address)root, "root", "linked", 0, folder, sizeof(folder)) && !folder[0];
        folders = folders && usb_media_folder((string_address)root, "root", "MY STICK", 1234, folder, sizeof(folder));
        check("device open: the folder is made the caller's alone (0700); a link where it is to be, or a folder that is another's, is refused", folders);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, 0);

        string_join(path, sizeof(path), (string_address)root, "/root/MY STICK");
        if (system_mount("none", (string_address)path, "tmpfs", 0, null) >= 0)
        {
                bipolar held;
                p8 inside[HOST_PATH_ROOM];

                folders = usb_media_folder((string_address)root, "root", "MY STICK", 0, folder, sizeof(folder)) != null;
                check("device open: a folder another drive is open at is refused", folders);
                memory_zero(&media, sizeof(media)), media.count = 1;
                string_copy(media.one[0].node, "mwgone0"), media.one[0].diskseq = 7, string_copy(media.one[0].folder, path);
                (void)usb_media_write((string_address)kept, &media);
                memory_zero(&media, sizeof(media)), usb_media_read((string_address)kept, &media);
                folders = media.count == 1 && string_equals((string_address)media.one[0].node, (string_address) "mwgone0") && media.one[0].diskseq == 7 &&
                          string_equals((string_address)media.one[0].folder, (string_address)path);
                check("device open: what is opened is kept a line each, its node, its disk's diskseq and its folder, and read back the same", folders);
                // Something with a file open on it: the eject is refused and it stays opened.
                string_join(inside, sizeof(inside), (string_address)path, "/held");
                held = system_open_at_mode(AT_FDCWD, (string_address)inside, FILE_CREATE | FILE_WRITE | O_CLOEXEC, 0600);
                {
                        bipolar error = 0;
                        busy = held >= 0 && usb_media_eject((string_address)kept, "mwgone0", &error) && error == -EBUSY;
                        usb_media_read((string_address)kept, &media);
                        busy = busy && media.count == 1;
                }
                if (held >= 0)
                        system_close(held);
                system_call_3(syscall(unlinkat), AT_FDCWD, (positive)inside, 0);
                check("device eject: a partition something has a file open on is not let go, and stays opened", busy);
                {
                        file_facts facts;
                        bipolar error = 0;
                        ejected = !usb_media_eject((string_address)kept, "mwgone0", &error) && !file_look(AT_FDCWD, (string_address)path, AT_SYMLINK_NOFOLLOW, &facts);
                        usb_media_read((string_address)kept, &media);
                        ejected = ejected && !media.count && usb_media_eject((string_address)kept, "mwgone0", &error);
                }
                check("device eject: what open opened is unmounted, its folder taken away and its line with it; a second eject has nothing to eject", ejected);
        }
        else
                check("device open: the check's namespace mounts a tmpfs (it runs as root, in a mount namespace of its own)", false);
        // A drive taken out: its node's disk has another diskseq, or none; the mount is detached and its folder goes.
        (void)usb_media_folder((string_address)root, "root", "GONE", 0, folder, sizeof(folder));
        tidied = system_mount("none", (string_address)folder, "tmpfs", 0, null) >= 0;
        memory_zero(&media, sizeof(media)), media.count = 2;
        string_copy(media.one[0].node, "mwgone0"), media.one[0].diskseq = 7, string_copy(media.one[0].folder, folder);
        string_join(path, sizeof(path), (string_address)root, "/root/LEFT");
        system_call_3(syscall(mkdirat), AT_FDCWD, (positive)path, 0700);
        string_copy(media.one[1].node, "mwgone1"), media.one[1].diskseq = 8, string_copy(media.one[1].folder, path);
        (void)usb_media_write((string_address)kept, &media);
        {
                file_facts facts;
                tidied = tidied && usb_media_tidy((string_address)kept) == 2 && !file_look(AT_FDCWD, (string_address)folder, AT_SYMLINK_NOFOLLOW, &facts) &&
                         !file_look(AT_FDCWD, (string_address)path, AT_SYMLINK_NOFOLLOW, &facts);
                usb_media_read((string_address)kept, &media);
                tidied = tidied && !media.count && !usb_media_tidy((string_address)kept);
        }
        check("device tidy: a drive taken out is let go of (its mount detached, its folder gone), as is what is no longer mounted; then nothing is left to do", tidied);
        {
                // The compositor, with Files closed: a block device's remove on its uevent socket wakes its thread, which lets go of what
                // was opened from that drive (a tmpfs here, kept as opened from a node that is gone). An add, or a USB device's remove,
                // asks for nothing.
                static const char removed[] = "remove@/devices/pci0000:00/usb1/1-2/host6/block/sdb/sdb1\0ACTION=remove\0SUBSYSTEM=block\0DEVNAME=sdb1\0DEVTYPE=partition";
                static const char added[] = "add@/devices/pci0000:00/usb1/1-2/host6/block/sdb\0ACTION=add\0SUBSYSTEM=block\0DEVNAME=sdb";
                static const char unplugged[] = "remove@/devices/pci0000:00/usb1/1-2\0ACTION=remove\0SUBSYSTEM=usb\0DEVTYPE=usb_device";
                p32 ends[2] = {0, 0};
                b32 heard = compositor_uevent_block_gone(removed, sizeof(removed)) && !compositor_uevent_block_gone(added, sizeof(added)) &&
                            !compositor_uevent_block_gone(unplugged, sizeof(unplugged));
                b32 let_go = false;
                (void)usb_media_folder((string_address)root, "root", "PULLED", 0, folder, sizeof(folder));
                if (heard && folder[0] && system_mount("none", (string_address)folder, "tmpfs", 0, null) >= 0 &&
                    (bipolar)system_call_4(syscall(socketpair), AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, (positive)ends) >= 0)
                {
                        file_facts facts;
                        int saved = compositor.uevents;
                        memory_zero(&media, sizeof(media)), media.count = 1;
                        string_copy(media.one[0].node, "mwgone2"), media.one[0].diskseq = 5, string_copy(media.one[0].folder, folder);
                        (void)usb_media_write((string_address)kept, &media);
                        compositor_tidy_kept = (string_address)kept;
                        compositor.uevents = (int)ends[0];
                        system_call_3(syscall(write), ends[1], (positive)removed, sizeof(removed));
                        compositor_uevents_read();
                        compositor.uevents = saved;
                        for (int wait = 0; wait < 300 && file_look(AT_FDCWD, (string_address)folder, AT_SYMLINK_NOFOLLOW, &facts); wait++)
                                process_nap_ns(10000000);
                        let_go = !file_look(AT_FDCWD, (string_address)folder, AT_SYMLINK_NOFOLLOW, &facts);
                        system_close(ends[0]), system_close(ends[1]);
                }
                check("device tidy: the compositor hears a block device's remove (not an add, nor a USB device's remove) and its thread lets go of what was opened from it",
                      heard && let_go);
        }
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)kept, 0);
        string_join(path, sizeof(path), (string_address)root, "/root/MY STICK");
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, AT_REMOVEDIR);
        string_join(path, sizeof(path), (string_address)root, "/root");
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, AT_REMOVEDIR);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)root, AT_REMOVEDIR);
}

/*
        What the census makes of the partitions a hostile bus offers: nine
        disks where it has room for eight, two partitions of one label, a
        label with no identity, and names at and past the room for one.
*/
static fn install_census(void)
{
        static host_census census;
        p8 path[HOST_NAME_ROOM + 16];
        //      "/dev/" and a name one byte past the room, and its end: the
        //      array was HOST_NAME_ROOM + 2, and the rows below wrote four bytes
        //      past it (a UBSan build traps on the index; a plain one wrote over
        //      the stack beside it).
        p8 name[5 + HOST_NAME_ROOM + 1];
        p8 held = true;

        memory_zero(address_of census, sizeof(census));
        for (positive at = 0; at < 9; at++)
        {
                p8 disk[8] = {'z', 'd', (p8)('0' + at), 0};

                string_copy(path, "/dev/");
                string_append_bounded(path, (string_address)disk, sizeof(path));
                census_one(address_of census, path, "moonwater-boot", "aaaa");
                census_one(address_of census, path, "moonwater-data", "bbbb");
        }
        check("eight installs are taken and a ninth is counted, not taken in",
              census.count == HOST_INSTALLS && census.dropped > 0);

        memory_zero(address_of census, sizeof(census));
        census_one(address_of census, "/dev/ze1", "moonwater-boot", "");
        census_one(address_of census, "/dev/ze2", "something-else", "cccc");
        check("a label without an identity, and one that is not ours, make no install",
              census.count == 0);

        census_one(address_of census, "/dev/ze3", "moonwater-boot", "1111");
        census_one(address_of census, "/dev/ze3", "moonwater-boot", "2222");
        census_one(address_of census, "/dev/ze3", "moonwater-data", "3333");
        held &= census.count == 1 && string_equals(census.found[0].system_partuuid, "2222") &&
                string_equals(census.found[0].data_partuuid, "3333");
        check("two partitions of one label on a disk are one install, the last named", held);

        memory_zero(address_of census, sizeof(census));
        memory_copy_apart(name, "/dev/", 5);
        memory_fill(name + 5, 'a', HOST_NAME_ROOM - 1);
        name[5 + HOST_NAME_ROOM - 1] = end;
        census_one(address_of census, name, "moonwater-boot", "dddd");
        held = census.count == 1;
        memory_zero(address_of census, sizeof(census));
        name[5 + HOST_NAME_ROOM] = end;
        name[5 + HOST_NAME_ROOM - 1] = 'a';
        census_one(address_of census, name, "moonwater-boot", "dddd");
        check("a device name of 63 bytes is an install's and one of 64 is not",
              held && census.count == 0);
}

/* Two lines of text as standard input: the answer to the first, and the
   second read after it into next. */
static bipolar install_line(string_address text, p8 address_to into,
                            p8 address_to next)
{
        p32 ends[2];
        bipolar kept = system_duplicate(0, 100, 0);
        bipolar got;

        if (system_pipe(ends, 0) < 0)
                return -9;
        system_write_all((positive)ends[1], (const address_any)text, string_length(text));
        system_close(ends[1]);
        system_duplicate(ends[0], 0, 0);
        got = host_read_line(into, 64);
        next[0] = end;
        host_read_line(next, 32);
        if (kept >= 0)
        {
                system_duplicate(kept, 0, 0);
                system_close(kept);
        }
        system_close(ends[0]);
        return got;
}

/*
        What the install and the boot say about names, lines and files, one
        small question each.
*/
static fn install_helpers(void)
{
        p8 swapped_same = true;
        p8 names = true;
        p32 seed = 12345;
        p8 text[64];
        p8 next[32];
        p8 name[80];

        //      A GPT GUID in disk order, as the PARTUUID spelling the kernel
        //      publishes: the first three fields byte-swapped.
        for (positive round = 0; round < 256; round++)
        {
                p8 guid[16];
                p8 swapped[16];
                p8 want[40];
                p8 got[40];

                for (positive at = 0; at < 16; at++)
                {
                        seed = seed * 1103515245u + 12345u;
                        guid[at] = (p8)(seed >> 16);
                }
                swapped[0] = guid[3];
                swapped[1] = guid[2];
                swapped[2] = guid[1];
                swapped[3] = guid[0];
                swapped[4] = guid[5];
                swapped[5] = guid[4];
                swapped[6] = guid[7];
                swapped[7] = guid[6];
                memory_copy_apart(swapped + 8, guid + 8, 8);
                storage_uuid_bytes(want, swapped);
                host_partuuid(got, guid);
                swapped_same &= string_equals(want, got);
        }
        check("a GPT GUID reads as the PARTUUID the kernel publishes", swapped_same);

        for (positive byte = 1; byte < 256; byte++)
        {
                p8 probe[4] = {'a', (p8)byte, 'b', 0};

                names &= host_name_valid(probe) ==
                         (byte < 0x80 && (byte_is_alnum(byte) || byte == '-' || byte == '_'));
        }
        memory_fill(name, 'a', 64);
        name[63] = end;
        names &= host_name_valid(name);
        name[63] = 'a';
        name[64] = end;
        names &= !host_name_valid(name) && !host_name_valid("");
        check("a block device name is letters, digits, - and _ and under 64 bytes", names);

        //      A confirmation is a whole line: one that does not fit is not
        //      the short one its start spells, and the next line is still there.
        memory_fill(name, ' ', 70);
        memory_copy_apart(name, "loop2", 5);
        memory_copy_apart(name + 66, "x\nok\n", 6);
        check("a line too long for the room is refused, not cut",
              install_line((string_address)name, text, next) == HOST_LINE_LONG);
        check("and the line after it is still there to read",
              string_equals(next, "ok"));
        check("a line that fits is read, its trailing spaces and return dropped",
              install_line("nvme0n1  \r\nok\n", text, next) == 7 &&
                  string_equals(text, "nvme0n1") && string_equals(next, "ok"));
        check("the end of input is -1", install_line("", text, next) == -1);
}

static bipolar format_image(string_address directory, string_address name,
                            p64 bytes, p8 stamp)
{
        p8 path[4096];
        bipolar handle;
        bipolar failed;
        storage_format_identity identity;

        path_join(path, sizeof(path), directory, name);
        handle = system_open_at_mode(AT_FDCWD, path,
                                     FILE_READ_WRITE | FILE_CREATE |
                                         FILE_TRUNCATE | O_CLOEXEC,
                                     0644);
        if (handle < 0)
                return handle;

        failed = system_call_2(syscall(ftruncate), (positive)handle, bytes);

        memory_fill(identity.uuid, stamp, 16);
        memory_fill(identity.hash_seed, (p8)~stamp, 16);
        identity.uuid[6] = 0x40 | (stamp & 0x0f);
        identity.uuid[8] = 0x80 | (stamp & 0x3f);
        identity.time = 1757721600;
        identity.label = "moonwater";

        if (!failed && name[0] == 'e')
                failed = storage_format_ext4(handle, 0, bytes, address_of identity);

        //      A disk: a 512 MiB system partition, then the rest as ext4.
        if (!failed && name[0] == 'd')
        {
                storage_format_partition parts[2];
                p64 sectors = bytes / 512;
                p64 first;
                p64 last;

                storage_gpt_span(sectors, 512, address_of first, address_of last);
                memory_zero(parts, sizeof(parts));
                memory_copy(parts[0].type, storage_gpt_system_type, 16);
                memory_copy(parts[1].type, storage_gpt_linux_type, 16);
                memory_fill(parts[0].unique, stamp + 1, 16);
                memory_fill(parts[1].unique, stamp + 2, 16);
                parts[0].first = 2048;
                parts[0].last = 2048 + (1 << 20) - 1;
                parts[0].name = "moonwater-boot";
                parts[1].first = parts[0].last + 1;
                parts[1].last = (last + 1) / 2048 * 2048 - 1;
                parts[1].name = "moonwater-data";

                failed = storage_format_gpt(handle, sectors, 512, identity.uuid,
                                            parts, 2);
                if (!failed)
                        failed = storage_format_fat32(handle, parts[0].first * 512,
                                                      (parts[0].last - parts[0].first + 1) * 512,
                                                      512, parts[0].first,
                                                      address_of identity);
                if (!failed)
                        failed = storage_format_ext4(handle, parts[1].first * 512,
                                                     (parts[1].last - parts[1].first + 1) * 512,
                                                     address_of identity);
        }

        system_close(handle);
        return failed;
}

static fn format_images(string_address directory)
{
        static const struct
        {
                string_address name;
                p64 bytes;
        } images[] = {
            {"ext4-64m.img", (p64)64 << 20},
            {"ext4-64m-odd.img", ((p64)64 << 20) + 4095},
            {"ext4-128m-less.img", ((p64)128 << 20) - 4096},
            {"ext4-128m.img", (p64)128 << 20},
            {"ext4-130m.img", (p64)130 << 20},
            {"ext4-700m.img", (p64)700 << 20},
            {"ext4-3g.img", ((p64)3 << 30) + 12345},
            {"ext4-20g.img", (p64)20 << 30},
            {"ext4-300g.img", (p64)300 << 30},
            {"ext4-2t.img", ((p64)2 << 40) + ((p64)7 << 20)},
            {"disk-2g.img", (p64)2 << 30},
            {"disk-33g.img", ((p64)33 << 30) + 511},
        };

        for (positive at = 0; at < array_count(images); at++)
        {
                bipolar failed = format_image(directory, images[at].name,
                                              images[at].bytes, (p8)(0x21 + at));

                check("every image formats", !failed);
                if (failed)
                        string_format(log, "  %s: %s\n", images[at].name,
                                      file_reason(failed));
        }
}

/*
        The settings block: what reads as never written, as whole and as
        damaged; the limits a reader holds a slot to even when its sum is
        right; which of two slots wins; that a write torn at any sector, or
        any byte, leaves the settings from before or the new ones and never
        neither; where an image's section is believed; and the command line
        against a copy in memory.
*/
static host_settings settings_block_slot;
static host_settings settings_pair[2];
static host_settings settings_before[2];
static host_settings settings_block_next;
static p8 settings_block_text[SPARK_SETTINGS_TEXT_MOST + 2];

static fn settings_one(host_settings address_to slot, p64 generation, p8 fill,
                       positive length)
{
        memory_fill(settings_block_text, fill, length);
        host_settings_empty(slot);
        slot->generation = generation;
        memory_fill(slot->medium, 0x5a, sizeof(slot->medium));
        host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND,
                          settings_block_text, length, null);
        host_settings_seal(slot);
}

static fn settings_raw(host_settings address_to slot, p8 list, p16 length)
{
        struct spark_settings_entry entry = {list, SPARK_SETTINGS_COMMAND, 1, length, 0};

        memory_copy_apart(slot->payload + slot->length, address_of entry,
                          SPARK_SETTINGS_ENTRY);
        memory_zero(slot->payload + slot->length + SPARK_SETTINGS_ENTRY,
                    spark_settings_padded(length));
        slot->length += SPARK_SETTINGS_ENTRY + spark_settings_padded(length);
}

/* An entry of any shape at the end of the payload, its text zeros. */
static fn settings_shaped(host_settings address_to slot, p8 list, p8 kind, p16 id, p16 length)
{
        struct spark_settings_entry entry = {list, kind, id, length, 0};

        memory_copy_apart(slot->payload + slot->length, address_of entry,
                          SPARK_SETTINGS_ENTRY);
        memory_zero(slot->payload + slot->length + SPARK_SETTINGS_ENTRY,
                    spark_settings_padded(length));
        slot->length += SPARK_SETTINGS_ENTRY + spark_settings_padded(length);
}

/*
        The security list (moonwater security): a block is 32 bytes and an
        anchor 104 to 552, each added by hand, and a switch is a number from
        1 to 16 with no text; any other kind, id or length in it is damage,
        and so are its kinds in any other list and a 97th entry. A slot from
        a disk nobody vouches for keeps none of it, an anchor least of all.
*/
static fn settings_security(void)
{
        static const struct
        {
                p8 kind;
                p16 id;
                p16 length;
                p8 good;
        } shapes[] = {
            {SPARK_SETTINGS_BLOCK, SPARK_SETTINGS_BY_HAND, 32, true},
            {SPARK_SETTINGS_BLOCK, SPARK_SETTINGS_BY_HAND, 31, false},
            {SPARK_SETTINGS_BLOCK, SPARK_SETTINGS_BY_HAND, 33, false},
            {SPARK_SETTINGS_BLOCK, 2, 32, false},
            {SPARK_SETTINGS_TRUST, SPARK_SETTINGS_BY_HAND, SPARK_SETTINGS_TRUST_LEAST, true},
            {SPARK_SETTINGS_TRUST, SPARK_SETTINGS_BY_HAND, SPARK_SETTINGS_TRUST_MOST, true},
            {SPARK_SETTINGS_TRUST, SPARK_SETTINGS_BY_HAND, SPARK_SETTINGS_TRUST_LEAST - 1, false},
            {SPARK_SETTINGS_TRUST, SPARK_SETTINGS_BY_HAND, SPARK_SETTINGS_TRUST_MOST + 1, false},
            {SPARK_SETTINGS_TRUST, 2, SPARK_SETTINGS_TRUST_LEAST, false},
            {SPARK_SETTINGS_SWITCH, 1, 0, true},
            {SPARK_SETTINGS_SWITCH, SPARK_SETTINGS_SWITCHES, 0, true},
            {SPARK_SETTINGS_SWITCH, SPARK_SETTINGS_SWITCHES + 1, 0, false},
            {SPARK_SETTINGS_SWITCH, 1, 4, false},
            {SPARK_SETTINGS_COMMAND, 1, 4, false},
            {SPARK_SETTINGS_KERNEL_SHELL, 1, 0, false},
            {SPARK_SETTINGS_SWITCH + 1, 1, 0, false},
        };
        host_settings address_to slot = address_of settings_block_slot;
        p8 key[32];
        p16 id = SPARK_SETTINGS_BY_HAND;
        host_setting setting;
        positive at = 0;
        p8 right = true;

        for (positive i = 0; i < array_count(shapes); i++)
        {
                host_settings_empty(slot);
                slot->generation = 1;
                settings_shaped(slot, SPARK_SETTINGS_SECURITY, shapes[i].kind, shapes[i].id,
                                shapes[i].length);
                host_settings_seal(slot);
                right &= (spark_settings_check(slot) == 1) == shapes[i].good;
        }
        check("each shape of a security entry is taken or called damage as its kind says", right);

        right = true;
        for (p8 kind = SPARK_SETTINGS_BLOCK; kind <= SPARK_SETTINGS_SWITCH; kind++)
        {
                host_settings_empty(slot);
                slot->generation = 1;
                settings_shaped(slot, SPARK_SETTINGS_INIT, kind, 1, kind == SPARK_SETTINGS_BLOCK ? 32 : 0);
                host_settings_seal(slot);
                right &= spark_settings_check(slot) < 0;
        }
        check("the security list's kinds in another list are damage", right);

        host_settings_empty(slot);
        slot->generation = 1;
        for (positive i = 0; i < SPARK_SETTINGS_SECURITY_MOST; i++)
                settings_shaped(slot, SPARK_SETTINGS_SECURITY, SPARK_SETTINGS_BLOCK,
                                SPARK_SETTINGS_BY_HAND, 32);
        host_settings_seal(slot);
        check("ninety-six security entries check", spark_settings_check(slot) == 1);
        settings_shaped(slot, SPARK_SETTINGS_SECURITY, SPARK_SETTINGS_SWITCH, 1, 0);
        host_settings_seal(slot);
        check("a ninety-seventh is damaged", spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        memory_fill(key, 0x5a, sizeof key);
        check("a security entry keeps the id it is given, and checks",
              !host_settings_add(slot, SPARK_SETTINGS_SECURITY, SPARK_SETTINGS_BLOCK,
                                 (string_address)key, 32, address_of id) &&
                  id == SPARK_SETTINGS_BY_HAND &&
                  host_settings_next(slot, address_of at, address_of setting) &&
                  setting.entry.id == SPARK_SETTINGS_BY_HAND && !memory_compare(setting.text, key, 32) &&
                  (slot->generation = 1, host_settings_seal(slot), spark_settings_check(slot) == 1));
        memory_zero(settings_block_text, sizeof settings_block_text);
        check("an anchor past 552 bytes is refused",
              host_settings_add(slot, SPARK_SETTINGS_SECURITY, SPARK_SETTINGS_TRUST,
                                settings_block_text, SPARK_SETTINGS_TRUST_MOST + 1, address_of id) != null);
        host_settings_add(slot, SPARK_SETTINGS_SECURITY, SPARK_SETTINGS_TRUST, settings_block_text,
                          SPARK_SETTINGS_TRUST_LEAST, address_of id);
        host_settings_seal(slot);
        host_settings_untrusted(slot);
        at = 0;
        check("a slot no disk vouches for keeps no key it refuses or trusts",
              !host_settings_next(slot, address_of at, address_of setting) && !slot->length &&
                  spark_settings_check(slot) == 1);
}

/*
        The group list (moonwater link group): a key, its grants and a name,
        37 to 67 bytes, in one of four places; any other kind, id or length
        in it is damage, so are its kind in any other list and a fifth
        entry, and a slot from a disk nobody vouches for keeps none of it.
*/
static fn settings_group(void)
{
        static const struct
        {
                p8 kind;
                p16 id;
                p16 length;
                p8 good;
        } shapes[] = {
            {SPARK_SETTINGS_GROUP_KEY, 1, SPARK_SETTINGS_GROUP_LEAST, true},
            {SPARK_SETTINGS_GROUP_KEY, SPARK_SETTINGS_GROUPS, SPARK_SETTINGS_GROUP_MOST, true},
            {SPARK_SETTINGS_GROUP_KEY, 1, SPARK_SETTINGS_GROUP_LEAST - 1, false},
            {SPARK_SETTINGS_GROUP_KEY, 1, SPARK_SETTINGS_GROUP_MOST + 1, false},
            {SPARK_SETTINGS_GROUP_KEY, SPARK_SETTINGS_GROUPS + 1, SPARK_SETTINGS_GROUP_LEAST, false},
            {SPARK_SETTINGS_COMMAND, 1, SPARK_SETTINGS_GROUP_LEAST, false},
            {SPARK_SETTINGS_BLOCK, 1, 32, false},
            {SPARK_SETTINGS_GROUP_KEY + 1, 1, SPARK_SETTINGS_GROUP_LEAST, false},
        };
        host_settings address_to slot = address_of settings_block_slot;
        p8 key[32];
        p16 id = 2;
        host_setting setting;
        positive at = 0;
        p8 right = true;

        for (positive i = 0; i < array_count(shapes); i++)
        {
                host_settings_empty(slot);
                slot->generation = 1;
                settings_shaped(slot, SPARK_SETTINGS_GROUP, shapes[i].kind, shapes[i].id,
                                shapes[i].length);
                host_settings_seal(slot);
                right &= (spark_settings_check(slot) == 1) == shapes[i].good;
        }
        check("each shape of a group entry is taken or called damage as its kind says", right);

        host_settings_empty(slot);
        slot->generation = 1;
        settings_shaped(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_GROUP_KEY, 1,
                        SPARK_SETTINGS_GROUP_LEAST);
        host_settings_seal(slot);
        check("a group's kind in another list is damage", spark_settings_check(slot) < 0);
        host_settings_empty(slot);
        slot->generation = 1;
        settings_shaped(slot, SPARK_SETTINGS_SECURITY, SPARK_SETTINGS_GROUP_KEY, 1,
                        SPARK_SETTINGS_GROUP_LEAST);
        host_settings_seal(slot);
        check("and in the security list, which TLS reads, is damage", spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        slot->generation = 1;
        for (positive i = 0; i < SPARK_SETTINGS_GROUPS; i++)
                settings_shaped(slot, SPARK_SETTINGS_GROUP, SPARK_SETTINGS_GROUP_KEY,
                                (p16)(i + 1), SPARK_SETTINGS_GROUP_LEAST);
        host_settings_seal(slot);
        check("four groups check", spark_settings_check(slot) == 1);
        settings_shaped(slot, SPARK_SETTINGS_GROUP, SPARK_SETTINGS_GROUP_KEY, 1,
                        SPARK_SETTINGS_GROUP_LEAST);
        host_settings_seal(slot);
        check("a fifth is damaged", spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        memory_fill(key, 0x6b, sizeof key);
        memory_zero(settings_block_text, 40);
        memory_copy(settings_block_text, key, 32);
        check("a group keeps the id it is given, and checks",
              !host_settings_add(slot, SPARK_SETTINGS_GROUP, SPARK_SETTINGS_GROUP_KEY,
                                 settings_block_text, 37, address_of id) &&
                  id == 2 && host_settings_next(slot, address_of at, address_of setting) &&
                  setting.entry.list == SPARK_SETTINGS_GROUP && setting.entry.id == 2 &&
                  !memory_compare(setting.text, key, 32) &&
                  (slot->generation = 1, host_settings_seal(slot), spark_settings_check(slot) == 1));
        check("a group of 68 bytes is refused",
              host_settings_add(slot, SPARK_SETTINGS_GROUP, SPARK_SETTINGS_GROUP_KEY,
                                settings_block_text, SPARK_SETTINGS_GROUP_MOST + 1,
                                address_of id) != null);
        for (positive i = 0; i < SPARK_SETTINGS_GROUPS; i++)
                host_settings_add(slot, SPARK_SETTINGS_GROUP, SPARK_SETTINGS_GROUP_KEY,
                                  settings_block_text, 37, address_of id);
        check("and a fifth is refused with the four there",
              host_settings_add(slot, SPARK_SETTINGS_GROUP, SPARK_SETTINGS_GROUP_KEY,
                                settings_block_text, 37, address_of id) != null);
        host_settings_seal(slot);
        host_settings_untrusted(slot);
        at = 0;
        check("a slot no disk vouches for keeps no group's key",
              !host_settings_next(slot, address_of at, address_of setting) && !slot->length &&
                  spark_settings_check(slot) == 1);
}

static p8 settings_same(host_settings address_to left, host_settings address_to right)
{
        return left->length == right->length &&
               !memory_compare(left, right, SPARK_SETTINGS_HEADER + left->length);
}

static fn settings_format(void)
{
        host_settings address_to slot = address_of settings_block_slot;
        string_address wanted[] = {"pacman -Syu", "sync", "echo hello > /tmp/marker",
                                   "echo pressed > /tmp/p"};
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        host_setting setting;
        positive at = 0;
        positive seen = 0;
        p8 order = true;
        p8 caught = true;
        p16 id = 0;

        //      The same sums the core_state harness holds its reference CRC-32 to.
        host_settings_empty(slot);
        slot->generation = 1;
        check("an empty slot sums as zlib sums it", spark_settings_sum(slot) == 0x3ae8f589);
        slot->generation = 7;
        memory_fill(slot->medium, 0x5a, sizeof(slot->medium));
        host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND, "pacman -Syu",
                          11, null);
        check("a slot with an entry sums as zlib sums it",
              slot->next[0] == 2 && spark_settings_sum(slot) == 0x3f693c1f);

        host_settings_empty(slot);
        check("a slot as built reads as never written", spark_settings_check(slot) == 0);
        slot->generation = 1;
        check("a slot with a generation and no sum is damaged",
              spark_settings_check(slot) < 0);
        host_settings_seal(slot);
        check("a sealed slot with no entries checks", spark_settings_check(slot) == 1);

        host_settings_empty(slot);
        check("a list's ids start at one",
              !host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND,
                                 wanted[0], 11, address_of id) &&
                  id == 1);
        check("every list hands out its own ids",
              !host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND,
                                 wanted[1], 4, address_of id) &&
                  id == 1);
        host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND, wanted[2],
                          24, address_of id);
        check("a list's second id is two", id == 2);
        id = 7;
        host_settings_add(slot, SPARK_SETTINGS_BIND, SPARK_SETTINGS_COMMAND, wanted[3],
                          21, address_of id);
        slot->generation = 7;
        memory_fill(slot->medium, 0xa5, sizeof(slot->medium));
        host_settings_seal(slot);
        check("a slot with entries in every list checks", spark_settings_check(slot) == 1);

        while (host_settings_next(slot, address_of at, address_of setting))
        {
                host_settings_text(text, address_of setting);
                order &= seen < 4 && string_equals(text, wanted[seen]);
                seen++;
        }
        check("entries come back as written, in the order written", order && seen == 4);

        for (positive bit = 0; bit < (SPARK_SETTINGS_HEADER + slot->length) * 8; bit++)
        {
                p8 address_to byte = (p8 address_to)slot + bit / 8;

                address_to byte ^= (p8)(1 << (bit % 8));
                caught &= spark_settings_check(slot) < 0;
                address_to byte ^= (p8)(1 << (bit % 8));
        }
        check("every one-bit change to a slot's header or entries is caught", caught);
        check("and the slot checks again once each is put back",
              spark_settings_check(slot) == 1);

        /*  A settings image found on a disk is believed about how to boot
            and about nothing that runs: every entry in the payload is a
            command, and the only thing tying that image to this build is a
            version string anybody can copy out of /proc/version. */
        slot->flags = SPARK_SETTINGS_CANVAS_OFF | SPARK_SETTINGS_MOUNT_OFF |
                      SPARK_SETTINGS_STARTUP_SET;
        host_settings_seal(slot);
        host_settings_untrusted(slot);
        {
                positive step = 0;
                positive left = 0;

                while (host_settings_next(slot, address_of step,
                                          address_of setting))
                        left++;
                check("a slot no disk vouches for keeps none of its commands",
                      !left && !slot->length);
                check("and keeps the switches that only say how to boot",
                      slot->flags == (SPARK_SETTINGS_CANVAS_OFF |
                                      SPARK_SETTINGS_MOUNT_OFF));
                check("and still checks once its commands are gone",
                      spark_settings_check(slot) == 1);
        }
}

static fn settings_bounds(void)
{
        host_settings address_to slot = address_of settings_block_slot;
        string_address failed;
        p8 refused = true;
        p16 id = 0;
        host_setting setting;

        host_settings_empty(slot);
        slot->generation = 1;
        slot->length = SPARK_SETTINGS_PAYLOAD + 4;
        host_settings_seal(slot);
        check("a length past the slot is damaged, sum or no sum",
              spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        slot->generation = 1;
        settings_raw(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_TEXT_MOST + 1);
        host_settings_seal(slot);
        check("an entry past 4096 bytes is damaged", spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        slot->generation = 1;
        settings_raw(slot, SPARK_SETTINGS_BIND, SPARK_SETTINGS_BIND_TEXT_MOST + 1);
        host_settings_seal(slot);
        check("a bound command past 255 bytes is damaged",
              spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        slot->generation = 1;
        for (positive at = 0; at < SPARK_SETTINGS_BIND_MOST; at++)
                settings_raw(slot, SPARK_SETTINGS_BIND, 3);
        host_settings_seal(slot);
        check("forty-eight bound events check", spark_settings_check(slot) == 1);
        settings_raw(slot, SPARK_SETTINGS_BIND, 3);
        host_settings_seal(slot);
        check("a forty-ninth is damaged", spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        slot->generation = 1;
        for (positive at = 0; at <= SPARK_SETTINGS_LIST_MOST; at++)
                settings_raw(slot, SPARK_SETTINGS_STARTUP, 3);
        host_settings_seal(slot);
        check("a seventeenth entry in one list is damaged", spark_settings_check(slot) < 0);

        for (p8 list = 0; list < 8; list += list == 0 ? SPARK_SETTINGS_LISTS + 1 : 1)
        {
                host_settings_empty(slot);
                slot->generation = 1;
                settings_raw(slot, list, 3);
                host_settings_seal(slot);
                refused &= spark_settings_check(slot) < 0;
        }
        check("a list nobody defined is damaged", refused);

        host_settings_empty(slot);
        slot->generation = 1;
        settings_raw(slot, SPARK_SETTINGS_INIT, 9);
        slot->length -= 4;
        host_settings_seal(slot);
        check("entries that run past the length are damaged",
              spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        slot->generation = 1;
        settings_raw(slot, SPARK_SETTINGS_INIT, 8);
        slot->length += 3;
        host_settings_seal(slot);
        check("a length that ends inside an entry header is damaged",
              spark_settings_check(slot) < 0);

        host_settings_empty(slot);
        refused = true;
        for (positive at = 0; at < SPARK_SETTINGS_LIST_MOST; at++)
                refused &= !host_settings_add(slot, SPARK_SETTINGS_INIT,
                                              SPARK_SETTINGS_COMMAND, "true", 4, null);
        check("sixteen entries fit in one list", refused);
        check("a seventeenth is refused",
              host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND,
                                "true", 4, null) != null);
        check("a full list leaves the others room",
              !host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND,
                                 "true", 4, null));

        memory_fill(settings_block_text, 'x', SPARK_SETTINGS_TEXT_MOST + 1);
        host_settings_empty(slot);
        check("an entry past 4096 bytes is refused",
              host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND,
                                settings_block_text, SPARK_SETTINGS_TEXT_MOST + 1, null) != null);
        check("a bound command past 255 bytes is refused",
              host_settings_add(slot, SPARK_SETTINGS_BIND, SPARK_SETTINGS_COMMAND,
                                settings_block_text, SPARK_SETTINGS_BIND_TEXT_MOST + 1, null) != null);

        for (positive at = 0; at < 3; at++)
                host_settings_add(slot, SPARK_SETTINGS_STARTUP, SPARK_SETTINGS_COMMAND,
                                  settings_block_text, SPARK_SETTINGS_TEXT_MOST, null);
        failed = host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND,
                                   settings_block_text, SPARK_SETTINGS_TEXT_MOST, null);
        check("a block with no room left says it is full",
              failed && string_equals(failed, "the settings block is full"));
        slot->generation = 2;
        host_settings_seal(slot);
        check("and what did fit still checks", spark_settings_check(slot) == 1);

        host_settings_empty(slot);
        host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND, "a", 1, null);
        host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND, "b", 1, null);
        host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND, "c", 1, null);
        host_settings_find(slot, SPARK_SETTINGS_EXIT, "2", address_of setting);
        host_settings_drop(slot, address_of setting);
        host_settings_add(slot, SPARK_SETTINGS_EXIT, SPARK_SETTINGS_COMMAND, "d", 1,
                          address_of id);
        check("an id is never handed out twice", id == 4);
        check("a removed entry is gone by its words",
              !host_settings_find(slot, SPARK_SETTINGS_EXIT, "b", address_of setting));
        check("the entries after it keep their ids",
              host_settings_find(slot, SPARK_SETTINGS_EXIT, "3", address_of setting) &&
                  setting.entry.length == 1 && setting.text[0] == 'c');
}

static fn settings_torn(void)
{
        p8 held = true;
        p8 bytes = true;
        positive target;

        settings_one(settings_pair, 5, 'o', 4000);
        settings_one(settings_pair + 1, 4, 'p', 3000);
        target = host_settings_older(settings_pair);
        check("a write goes over the older slot", target == 1);
        check("a write takes the generation after the newest",
              host_settings_generation(settings_pair) == 6);
        check("the newer of two good slots is believed",
              spark_settings_newest(settings_pair) == 0);
        memory_copy_apart(settings_before, settings_pair, sizeof(settings_pair));
        settings_one(address_of settings_block_next, 6, 'n', 3500);

        //      Every sector of the new slot landed or not, then every 37th byte.
        for (positive landed = 0; landed <= SPARK_SETTINGS_SLOT; landed += 1)
        {
                b32 newest;

                if (landed % 512 && landed % 37)
                        continue;

                memory_copy_apart(settings_pair, settings_before, sizeof(settings_pair));
                memory_copy_apart(settings_pair + target, address_of settings_block_next, landed);
                newest = spark_settings_newest(settings_pair);

                p8 kept = newest == (b32)target
                                ? settings_same(settings_pair + target,
                                                address_of settings_block_next)
                                : newest == 0 &&
                                      settings_same(settings_pair, settings_before);

                if (landed % 512)
                        bytes &= kept;
                else
                        held &= kept;
        }
        check("a write torn at any sector boots the old settings or the new", held);
        check("a write torn at any byte boots the old settings or the new", bytes);

        host_settings_empty(settings_pair);
        host_settings_empty(settings_pair + 1);
        check("two slots as built are the defaults", spark_settings_newest(settings_pair) == 0 &&
                                                         spark_settings_check(settings_pair) == 0);
        check("the first write goes over the first slot",
              host_settings_older(settings_pair) == 0 &&
                  host_settings_generation(settings_pair) == 1);
        settings_one(address_of settings_block_next, 1, 'f', 2000);
        held = true;
        for (positive landed = 0; landed <= SPARK_SETTINGS_SLOT; landed += 512)
        {
                b32 newest;

                host_settings_empty(settings_pair);
                memory_copy_apart(settings_pair, address_of settings_block_next, landed);
                newest = spark_settings_newest(settings_pair);
                held &= newest >= 0 &&
                        (spark_settings_check(settings_pair + newest) == 0 ||
                         settings_same(settings_pair + newest, address_of settings_block_next));
        }
        check("a first write torn anywhere boots the defaults or the new settings", held);

        settings_one(settings_pair, 3, 'a', 100);
        settings_one(settings_pair + 1, 9, 'b', 100);
        settings_pair[1].payload[20] ^= 1;
        check("a newer slot that fails its sum loses to the older",
              spark_settings_newest(settings_pair) == 0);
        check("and the write after goes over the damaged one",
              host_settings_older(settings_pair) == 1 &&
                  host_settings_generation(settings_pair) == 4);
        settings_pair[0].payload[20] ^= 1;
        check("two damaged slots are the defaults", spark_settings_newest(settings_pair) < 0);
}

static b32 settings_block_run(string_address first, string_address second,
                        string_address third, string_address fourth)
{
        string_address arguments[] = {"moonwater", first, second, third, fourth, null};
        positive count = 1;
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];

        b32 outcome;

        while (arguments[count])
                count++;

        //      The command asks the shape first, so that what is not one is
        //      usage before anything is read.
        if (!host_settings_shaped(arguments, count))
                return HOST_SETTINGS_USAGE;

        //      A change leaves its line for saving to finish, and nothing is
        //      saved here: end it, or the verdict lands on the same line.
        outcome = host_settings_apply(address_of settings_block_slot, arguments, count, text);
        if (outcome == HOST_SETTINGS_CHANGED)
        {
                string_format(log, "\n");
                log_flush();
        }

        return outcome;
}

static positive settings_listed(p8 list, p8 kind)
{
        host_setting setting;
        positive at = 0;
        positive count = 0;

        while (host_settings_next(address_of settings_block_slot, address_of at, address_of setting))
                count += setting.entry.list == list &&
                         (!kind || setting.entry.kind == kind);

        return count;
}

static fn settings_commands(void)
{
        host_settings address_to slot = address_of settings_block_slot;
        host_setting setting;

        host_settings_empty(slot);

        check("init add takes the words given apart",
              settings_block_run("init", "add", "pacman", "-Syu") == HOST_SETTINGS_CHANGED &&
                  host_settings_find(slot, SPARK_SETTINGS_INIT, "pacman -Syu",
                                     address_of setting) &&
                  setting.entry.id == 1);
        check("init add takes them quoted",
              settings_block_run("init", "add", "pacman -Syu --noconfirm", null) == HOST_SETTINGS_CHANGED &&
                  host_settings_find(slot, SPARK_SETTINGS_INIT, "2", address_of setting) &&
                  setting.entry.length == 23);
        check("init add of a line it has is the entry it has, and not another",
              settings_block_run("init", "add", "pacman -Syu", null) == HOST_SETTINGS_SHOWN &&
                  settings_listed(SPARK_SETTINGS_INIT, 0) == 2);
        check("init remove takes an id",
              settings_block_run("init", "remove", "1", null) == HOST_SETTINGS_CHANGED &&
                  !host_settings_find(slot, SPARK_SETTINGS_INIT, "1", address_of setting));
        check("init remove takes the words",
              settings_block_run("init", "remove", "pacman -Syu --noconfirm", null) == HOST_SETTINGS_CHANGED &&
                  !settings_listed(SPARK_SETTINGS_INIT, 0));
        check("init remove of no such entry is refused",
              settings_block_run("init", "remove", "9", null) == HOST_SETTINGS_REFUSED);
        check("init alone lists", settings_block_run("init", null, null, null) == HOST_SETTINGS_SHOWN);
        check("init add with nothing is usage",
              settings_block_run("init", "add", null, null) == HOST_SETTINGS_USAGE &&
                  settings_block_run("init", "add", "", null) == HOST_SETTINGS_USAGE);
        check("an init word nobody defined is usage",
              settings_block_run("init", "pacman -Syu", null, null) == HOST_SETTINGS_USAGE);

        check("init mount off turns the mount off",
              settings_block_run("init", "mount", "off", null) == HOST_SETTINGS_CHANGED &&
                  slot->flags & SPARK_SETTINGS_MOUNT_OFF);
        check("init mount alone shows it",
              settings_block_run("init", "mount", null, null) == HOST_SETTINGS_SHOWN);
        check("init mount takes on or off only",
              settings_block_run("init", "mount", "maybe", null) == HOST_SETTINGS_USAGE &&
                  settings_block_run("init", "mount", "on", "now") == HOST_SETTINGS_USAGE);
        check("init mount on turns it back on",
              settings_block_run("init", "mount", "on", null) == HOST_SETTINGS_CHANGED &&
                  !(slot->flags & SPARK_SETTINGS_MOUNT_OFF));
        check("a switch belongs to its own command",
              settings_block_run("exit", "mount", "off", null) == HOST_SETTINGS_USAGE);

        //      Canvas at boot is a switch as the mount is. Nothing reads the
        //      startup list yet, so no command takes it: a setting accepted and
        //      then ignored is one nobody can tell is unsupported.
        host_settings_empty(slot);
        check("Canvas at boot is a switch as the mount is",
              settings_block_run("init", "canvas", "off", null) == HOST_SETTINGS_CHANGED &&
                  slot->flags & SPARK_SETTINGS_CANVAS_OFF &&
                  settings_block_run("init", "canvas", "on", null) == HOST_SETTINGS_CHANGED &&
                  !(slot->flags & SPARK_SETTINGS_CANVAS_OFF));
        check("the startup list is not taken yet",
              settings_block_run("startup", null, null, null) == HOST_SETTINGS_USAGE &&
                  settings_block_run("startup", "shell", null, null) == HOST_SETTINGS_USAGE &&
                  settings_block_run("startup", "add", "btop", null) == HOST_SETTINGS_USAGE &&
                  !slot->flags && !slot->length);

        check("exit add and remove",
              settings_block_run("exit", "add", "sync", null) == HOST_SETTINGS_CHANGED &&
                  settings_listed(SPARK_SETTINGS_EXIT, 0) == 1 &&
                  settings_block_run("exit", "remove", "sync", null) == HOST_SETTINGS_CHANGED &&
                  !settings_listed(SPARK_SETTINGS_EXIT, 0));
        check("exit with a command and no add is usage",
              settings_block_run("exit", "sync", null, null) == HOST_SETTINGS_USAGE);

        slot->generation = 3;
        host_settings_seal(slot);
        check("what the commands leave behind checks", spark_settings_check(slot) == 1);
}

static p8 settings_image[0x10000];

/* A PE image whose section table names one section at raw with size, in a
   settings_image; the settings slots are empty ones, the second only when asked. */
static fn settings_build(string_address name, p32 raw, p32 size, p8 second)
{
        p8 address_to image = settings_image;
        p8 address_to table = image + 0x80 + 24 + 0xf0;

        memory_zero(image, sizeof(settings_image));
        image[0] = 'M';
        image[1] = 'Z';
        storage_put32(image + 0x3c, 0x80);
        memory_copy_apart(image + 0x80, "PE\0\0", 4);
        storage_put16(image + 0x86, 3);
        storage_put16(image + 0x94, 0xf0);
        memory_copy_apart(table, ".text\0\0\0", 8);
        storage_put32(table + 16, 0x1000);
        storage_put32(table + 20, 0x1000);
        memory_copy_apart(table + 40, name, 8);
        storage_put32(table + 56, size);
        storage_put32(table + 60, raw);
        memory_copy_apart(table + 80, ".data\0\0\0", 8);
        storage_put32(table + 96, 0x1000);
        storage_put32(table + 100, 0xa000);

        host_settings_empty(address_of settings_block_slot);
        memory_copy_apart(image + 0x2000, address_of settings_block_slot, 64);
        if (second)
                memory_copy_apart(image + 0x6000, address_of settings_block_slot, 64);
}

static fn settings_sections(void)
{
        static const struct
        {
                string_address name;
                p32 raw;
                p32 size;
                p8 first;
                p8 second;
                p64 found;
        } cases[] = {
            {".mwset\0\0", 0x2000, 0x8000, true, true, 0x2000},
            {".mwsex\0\0", 0x2000, 0x8000, true, true, 0},
            {".mwset\0\0", 0x2001, 0x8000, true, true, 0},
            {".mwset\0\0", 0x2000, 0x4000, true, true, 0},
            //      One slot's magic is enough, whichever it is (a cut write
            //      leaves the other without); neither is no settings block.
            {".mwset\0\0", 0x2000, 0x8000, true, false, 0x2000},
            {".mwset\0\0", 0x2000, 0x8000, false, true, 0x2000},
            {".mwset\0\0", 0x2000, 0x8000, false, false, 0},
        };
        p8 held = true;

        for (positive at = 0; at < array_count(cases); at++)
        {
                bipolar handle = system_call_2(syscall(memfd_create),
                                               (positive)(string_address)"settings", 0);
                p64 offset = 0;

                if (handle < 0)
                {
                        held = false;
                        break;
                }

                settings_build(cases[at].name, cases[at].raw, cases[at].size, cases[at].second);
                if (!cases[at].first)
                        memory_fill(settings_image + 0x2000, 0, 64);

                held &= storage_write(handle, settings_image, sizeof(settings_image), 0) ==
                            (bipolar)sizeof(settings_image) &&
                        host_settings_section(handle) == cases[at].found &&
                        host_settings_slots(handle, settings_pair, address_of offset) ==
                            (cases[at].found != 0) &&
                        (!cases[at].found ||
                         (offset == 0x2000 &&
                          spark_settings_newest(settings_pair) == (cases[at].first ? 0 : 1)));
                system_close(handle);
        }

        check("an image's settings are believed only where its section table and "
              "one slot's magic agree",
              held);
}

/*
        The commit this build was made from (host.c's host_commit, which build
        writes into the configuration header): twelve hex digits and perhaps
        -dirty, or unknown outside a git checkout, and the date likewise; and
        moonwater version says them, with the kernel's release, a line each.
*/
static fn build_commit(void)
{
        p8 said[256];
        string_address words[2] = {(string_address) "moonwater", (string_address) "version"};
        positive digits = 0;
        bipolar kept = system_duplicate(1, 101, 0);
        bipolar handle = system_call_2(syscall(memfd_create), (positive)(string_address) "version", 0);
        positive got = 0;
        bool shaped;
        bool dated;
        bool printed;

        while (digits < sizeof(host_commit) - 1 &&
               ((host_commit[digits] >= '0' && host_commit[digits] <= '9') ||
                (host_commit[digits] >= 'a' && host_commit[digits] <= 'f')))
                digits++;
        shaped = (digits == 12 && (!host_commit[12] || string_equals(host_commit + 12, "-dirty"))) ||
                 string_equals(host_commit, "unknown");
        dated = string_equals(host_commit_date, "unknown") || (sizeof(host_commit_date) > 20 && host_commit_date[4] == '-');

        host_verb = (string_address) "version";
        if (handle >= 0 && kept >= 0)
        {
                system_duplicate(handle, 1, 0);
                (void)host_version_verb(words, 2);
                log_flush();
                system_duplicate(kept, 1, 0);
                got = storage_read(handle, said, sizeof(said) - 1, 0);
        }
        said[got] = end;
        printed = string_has_prefix((string_address)said, "commit ") &&
                  memory_search(said, got, (address_any) "\ndate ", 6) &&
                  memory_search(said, got, (address_any) "\nkernel ", 8) &&
                  memory_search(said, got, (address_any)host_commit, sizeof(host_commit) - 1);
        if (kept >= 0)
                system_close(kept);
        if (handle >= 0)
                system_close(handle);

        check("version: the commit in the build is twelve hex digits with -dirty or none, or unknown, and its date is a date or unknown", shaped && dated);
        check("version: moonwater version says the commit, the date and the kernel's release, a line each, the commit as embedded", printed);
}

/*
        Which build an image is, read the way this machine's images carry it:
        x86's setup header, or on arm64 and riscv64 the kernel's own banner
        in an image that is the kernel uncompressed. The banner is looked for
        past the copy the kernel links before it is numbered, past the bare
        words a program in the initramfs holds, and across the seam between
        two megabyte reads; spelled as the running build is, "R (who) #N ...".
*/
static fn image_builds(void)
{
        static p8 image[(3 << 20) + 4096];
        static const p8 want[] =
            "7.2.6moonwater-25 (root@box) #4 SMP PREEMPT Tue Sep 22 07:21:00 CEST 2026";
        p8 got[256];
        p8 held = true;
        p8 refused = true;

#if X64
        static const p8 version[] =
            "7.2.6moonwater-25 (root@box) #4 SMP PREEMPT Tue Sep 22 07:21:00 CEST 2026";
        positive size = 0x4000;

        memory_zero(image, size);
        image[0] = 'M';
        image[1] = 'Z';
        memory_copy_apart(image + 0x202, "HdrS", 4);
        storage_put16(image + 0x206, 0x020f);
        storage_put16(image + 0x20e, 0x1000);
        memory_copy_apart(image + 0x1200, version, sizeof(version));
#else
        static const p8 placeholder[] =
            "Linux version 7.2.6moonwater-25 (root@box) (aarch64-linux-gnu-gcc "
            "(GCC) 16.1.0, GNU ld (GNU Binutils) 2.47) # SMP PREEMPT \n";
        static const p8 banner[] =
            "Linux version 7.2.6moonwater-25 (root@box) (aarch64-linux-gnu-gcc "
            "(GCC) 16.1.0, GNU ld (GNU Binutils) 2.47) #4 SMP PREEMPT Tue Sep 22 "
            "07:21:00 CEST 2026\n";
        positive size = sizeof(image);

        memory_zero(image, size);
        image[0] = 'M';
        image[1] = 'Z';
        memory_copy_apart(image + 0x40000, "Linux version ", 14);
        memory_copy_apart(image + (1 << 20) - 60, placeholder, sizeof(placeholder) - 1);
        memory_copy_apart(image + (2 << 20) - 40, banner, sizeof(banner) - 1);
#endif
        {
                bipolar handle = system_call_2(syscall(memfd_create),
                                               (positive)(string_address)"image", 0);

                held = handle >= 0 &&
                       storage_write(handle, image, size, 0) == (bipolar)size &&
                       host_image_version(handle, got, sizeof(got)) &&
                       string_equals(got, want);
                if (handle >= 0)
                        system_close(handle);
        }

        //      No version, one cut off before its end, and one with no room.
        for (positive at = 0; at < 3; at++)
        {
                bipolar handle = system_call_2(syscall(memfd_create),
                                               (positive)(string_address)"image", 0);
                positive length = size;

#if X64
                if (at == 0)
                        storage_put16(image + 0x20e, 0);
                else if (at == 1)
                        memory_copy_apart(image + 0x202, "HdrT", 4);
                else
                        memory_copy_apart(image + 0x202, "HdrS", 4),
                            storage_put16(image + 0x20e, 0x1000),
                            length = 0x1100;
#else
                if (at == 0)
                        memory_zero(image + (2 << 20) - 40, sizeof(banner) - 1);
                else if (at == 1)
                        memory_copy_apart(image + (2 << 20) - 40, banner,
                                          sizeof(banner) - 1),
                            length = (2 << 20) + 60;
                else
                        length = size;
#endif
                if (handle < 0 ||
                    storage_write(handle, image, length, 0) != (bipolar)length)
                        refused = false;
                else if (at < 2)
                        refused &= !host_image_version(handle, got, sizeof(got));
                else
                        refused &= !host_image_version(handle, got, 24);
                if (handle >= 0)
                        system_close(handle);
        }

        check("an image names its build as the running kernel spells it", held);
        check("an image with no whole version names no build", refused);
}

/*
        An image that is not what its header says, in every way a stranger's
        disk could make one: the section table is read from the first page of
        a file somebody else wrote, so every number in it is a guess about how
        far to go.
*/
static fn settings_hostile(void)
{
        static const struct
        {
                positive at;
                positive width;
                p64 value;
                p64 found;
        } cases[] = {
            {0x94, 2, 0xffff, 0},      // an optional header the page cannot hold
            {0x86, 2, 0xffff, 0},      // more sections than the page can hold
            {0x86, 2, 1, 0},           // fewer than the one that matters
            {0x3c, 4, 0xffc, 0},       // the PE header at the end of the page
            {0x3c, 4, 0xff8, 0},
            {0x3c, 4, 0xffffffff, 0},
            {0x3c, 4, 0x4000, 0},
            {0, 1, 'Z', 0},            // not MZ
            {0x80, 1, 'Q', 0},         // not PE
            {0x80 + 24 + 0xf0 + 40 + 20, 4, 0x20000, 0}, // its raw data past the end of the file
            {0x80 + 24 + 0xf0 + 40 + 20, 4, 0xfffff000, 0},
            {0x80 + 24 + 0xf0 + 40 + 20, 4, 0, 0},
            {0x80 + 24 + 0xf0 + 40 + 16, 4, 0, 0}, // a section with no size
            {0x80 + 24 + 0xf0 + 40 + 16, 4, 0xffffffff, 0x2000},
        };
        p8 held = true;
        p8 second = true;

        for (positive at = 0; at < array_count(cases); at++)
        {
                bipolar handle = system_call_2(syscall(memfd_create),
                                               (positive)(string_address)"hostile", 0);

                if (handle < 0)
                        break;

                settings_build(".mwset\0\0", 0x2000, 0x8000, true);
                if (cases[at].width == 1)
                        settings_image[cases[at].at] = (p8)cases[at].value;
                else if (cases[at].width == 2)
                        storage_put16(settings_image + cases[at].at, (p16)cases[at].value);
                else
                        storage_put32(settings_image + cases[at].at, (p32)cases[at].value);

                held &= storage_write(handle, settings_image, sizeof(settings_image), 0) ==
                            (bipolar)sizeof(settings_image) &&
                        host_settings_section(handle) == cases[at].found;
                system_close(handle);
        }

        //      A second section of the name does not outrank the first.
        {
                bipolar handle = system_call_2(syscall(memfd_create),
                                               (positive)(string_address)"twice", 0);
                p8 address_to table = settings_image + 0x80 + 24 + 0xf0;

                settings_build(".mwset\0\0", 0x2000, 0x8000, true);
                memory_copy_apart(table + 80, ".mwset\0\0", 8);
                storage_put32(table + 96, 0x8000);
                storage_put32(table + 100, 0x6000);
                second = handle >= 0 &&
                         storage_write(handle, settings_image, sizeof(settings_image), 0) ==
                             (bipolar)sizeof(settings_image) &&
                         host_settings_section(handle) == 0x2000;
                if (handle >= 0)
                        system_close(handle);
        }

        check("a hostile section table finds no settings it should not, and never reads past its page",
              held);
        check("a second section of the settings name is not the first", second);
}

#if X64
/* The version field of an x86 setup header, with every field wrong in turn. */
static fn image_hostile(void)
{
        static p8 image[0x4000];
        static const struct
        {
                positive at;
                positive width;
                p64 value;
        } cases[] = {
            {0x20e, 2, 0xffff}, // a version string past the end of the file
            {0x20e, 2, 0},      // none
            {0x206, 2, 0x1ff},  // a protocol that has none
            {0x202, 1, 'X'},    // not a setup header
            {0, 1, 'Z'},        // not MZ
        };
        p8 refused = true;
        p8 got[256];

        for (positive at = 0; at < array_count(cases) + 2; at++)
        {
                bipolar handle = system_call_2(syscall(memfd_create),
                                               (positive)(string_address)"version", 0);
                positive length = at == array_count(cases) + 1 ? 0x100 : sizeof(image);

                memory_zero(image, sizeof(image));
                image[0] = 'M';
                image[1] = 'Z';
                memory_copy_apart(image + 0x202, "HdrS", 4);
                storage_put16(image + 0x206, 0x020f);
                storage_put16(image + 0x20e, 0x1000);
                memory_copy_apart(image + 0x1200, "7.2.6 (a@b) #1 x", 17);

                if (at < array_count(cases))
                {
                        if (cases[at].width == 1)
                                image[cases[at].at] = (p8)cases[at].value;
                        else
                                storage_put16(image + cases[at].at, (p16)cases[at].value);
                }
                else if (at == array_count(cases))
                        memory_fill(image + 0x1200, 'v', 0x2000); // no end to it

                if (handle < 0 || storage_write(handle, image, length, 0) != (bipolar)length)
                        refused = false;
                else
                        refused &= !host_image_version(handle, got, sizeof(got));
                if (handle >= 0)
                        system_close(handle);
        }

        check("an image whose setup header lies names no build", refused);
}
#endif

/* The answers a settings image gives and takes: what is in it, damaged, or not
   there at all, and what stamping it did. */
static fn settings_codes(void)
{
        static p8 back[sizeof(settings_image)];
        host_settings address_to slot = address_of settings_block_slot;
        host_settings address_to into = address_of settings_block_next;
        p8 path[64];
        // A real file, not a memfd named through /proc/self/fd: the reader opens its image with O_NOFOLLOW (df81078c8), and that
        // refuses a /proc link, which no product caller passes (every caller hands a path it looked up).
        static const char image_name[] = "/tmp/mw-settings-image-check";
        bipolar handle;
        p8 held;

        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)image_name, 0);
        handle = system_open_at_mode(AT_FDCWD, image_name, FILE_READ_WRITE | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        held = handle >= 0;
        b32 kinds[4] = {0, 0, 0, 0};
        bipolar stamps[4] = {0, 0, 0, 0};
        p8 whole = true;
        p8 one = true;
        p8 junk[512];

        if (handle < 0)
                return;

        memory_copy(path, image_name, sizeof(image_name));

        //      An image with a block, both slots never written.
        settings_build(".mwset\0\0", 0x2000, 0x8000, true);
        host_settings_empty(slot);
        slot->generation = 4;
        host_settings_add(slot, SPARK_SETTINGS_INIT, SPARK_SETTINGS_COMMAND, "sync", 4, null);
        held &= storage_write(handle, settings_image, sizeof(settings_image), 0) ==
                (bipolar)sizeof(settings_image);
        kinds[0] = host_settings_image(path, into);
        stamps[0] = host_settings_stamp(path, slot);
        held &= host_settings_image(path, into) == 1 && into->generation == 4 &&
                settings_same(into, slot);

        //      One slot's first sector lost, each in turn: the kernel starts on the
        //      other (spark_settings_newest), and so the image still has its settings
        //      here, where it read as an image built without any, an update stamped
        //      the defaults over what the person had kept, and the next stamp, which
        //      is what writes the lost slot again, was refused.
        for (positive lost = 0; lost < 2; lost++)
        {
                slot->generation = 7 + lost;
                one &= host_settings_stamp(path, slot) == 0;
                memory_fill(junk, 0xff, sizeof(junk));
                one &= storage_write(handle, junk, sizeof(junk),
                                     0x2000 + lost * SPARK_SETTINGS_SLOT) == (bipolar)sizeof(junk);
                one &= host_settings_image(path, into) == 1 &&
                       into->generation == 7 + lost && settings_same(into, slot);
        }
        slot->generation = 9;
        one &= host_settings_stamp(path, slot) == 0 && host_settings_image(path, into) == 1 &&
               into->generation == 9 && settings_same(into, slot);

        //      Both slots torn: a block is there and cannot be read.
        settings_build(".mwset\0\0", 0x2000, 0x8000, true);
        host_settings_empty(slot);
        slot->generation = 5;
        slot->sum = 1;
        memory_copy_apart(settings_image + 0x2000, slot, SPARK_SETTINGS_HEADER);
        memory_copy_apart(settings_image + 0x6000, slot, SPARK_SETTINGS_HEADER);
        held &= storage_write(handle, settings_image, sizeof(settings_image), 0) ==
                (bipolar)sizeof(settings_image);
        kinds[1] = host_settings_image(path, into);

        //      An image built without one, which stamping leaves exactly as it was.
        settings_build(".mwsex\0\0", 0x2000, 0x8000, true);
        held &= storage_write(handle, settings_image, sizeof(settings_image), 0) ==
                (bipolar)sizeof(settings_image);
        kinds[2] = host_settings_image(path, into);
        host_settings_empty(slot);
        slot->generation = 6;
        stamps[2] = host_settings_stamp(path, slot);
        whole = file_transfer_exact(syscall(pread64), handle, back, sizeof(back), 0) ==
                    (bipolar)sizeof(back) &&
                !memory_compare(back, settings_image, sizeof(back));

        kinds[3] = host_settings_image("/proc/self/fd/none/image", into);
        stamps[3] = host_settings_stamp("/proc/self/fd/none/image", slot);
        system_close(handle);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)image_name, 0);

        check("an image with a block gives its settings, and stamping puts new ones in both slots",
              held && kinds[0] == 1 && stamps[0] == 0);
        check("an image with one slot's first sector lost gives the other slot's settings, and the next stamp writes both again",
              one);
        check("an image whose slots are both torn is the defaults, and is said to be",
              kinds[1] == 0);
        //      Main answers -1 for both: an image with no usable block, and a file
        //      that cannot be read, so these two are only known to be refused.
        check("an image built without a block, and one that cannot be read, give no settings",
              kinds[2] < 0 && kinds[3] < 0);
        check("and stamping it writes nothing and says it carries no settings",
              stamps[2] == 1 && whole && stamps[3] < 0);
}

/*
        An update from a stick, on a system partition that is a directory
        here: the build each image says and how two compare, what an image
        has to be to be written, what a stick carries and which one an update
        takes, the sum a stick says beside it, an update and a rollback whole,
        and the same cut after every step and inside every copy, which must
        leave a loader firmware starts, of the build there before or the new
        one, and the next write finishing what the cut left. Unmounting and
        reading back from the disk itself is the install lane's, in a guest.
*/
#define UPDATE_BYTES 0xc000
#define UPDATE_ROOT_ROOM 96

static p8 update_bytes[UPDATE_BYTES];
static p8 update_root[UPDATE_ROOT_ROOM];
static host_settings update_disk;
static host_settings update_carry;

static const p8 update_old[] = "7.2.6moonwater-25 (root@box) #4 SMP PREEMPT Tue Sep 22 07:21:00 CEST 2026";
static const p8 update_new[] = "7.2.6moonwater-25 (root@box) #5 SMP PREEMPT Wed Sep 23 09:02:11 CEST 2026";
static const p8 update_older[] = "7.2.5moonwater-24 (root@box) #3 SMP PREEMPT Mon Sep 21 18:40:00 CEST 2026";

// The machine an image for this one is not.
#if X64
#define UPDATE_OTHER 0xaa64
#else
#define UPDATE_OTHER 0x8664
#endif

/*
        A PE image as an install keeps one, in update_bytes: the machine its
        header names, the build it says (in the setup header on x86, in the
        kernel's banner elsewhere), a body of its own and a settings section
        of two slots never written. How many bytes it is.
*/
static positive update_image(p16 machine, string_address build, p8 body)
{
        p8 address_to image = update_bytes;
        p8 address_to table = image + 0x40 + 24 + 0xf0;

        memory_zero(image, UPDATE_BYTES);
        image[0] = 'M';
        image[1] = 'Z';
        storage_put32(image + 0x3c, 0x40);
        memory_copy_apart(image + 0x40, "PE\0\0", 4);
        storage_put16(image + 0x44, machine);
        storage_put16(image + 0x46, 2);
        storage_put16(image + 0x54, 0xf0);
        storage_put16(image + 0x58, 0x20b);
        storage_put16(image + 0x58 + 68, 10);
        memory_copy_apart(table, ".text\0\0\0", 8);
        storage_put32(table + 8, 0x1000);
        storage_put32(table + 12, 0x1000);
        storage_put32(table + 16, 0x1000);
        storage_put32(table + 20, 0x1000);
        memory_copy_apart(table + 40, ".mwset\0\0", 8);
        storage_put32(table + 48, 0x8000);
        storage_put32(table + 52, 0x2000);
        storage_put32(table + 56, 0x8000);
        storage_put32(table + 60, 0x2000);
#if X64
        memory_copy_apart(image + 0x202, "HdrS", 4);
        storage_put16(image + 0x206, 0x020f);
        storage_put16(image + 0x20e, 0x0e00);
        memory_copy_apart(image + 0x1000, build, string_length(build));
#else
        {
                //      "R (who) " and the rest, with the compiler between them.
                string_address rest = string_find(build, ") #");
                positive head = rest ? (positive)(rest - build) + 1 : 0;
                static const p8 compiler[] = " (gcc (GCC) 16.1.0, GNU ld (GNU Binutils) 2.47)";
                p8 address_to at = image + 0x1000;

                memory_copy_apart(at, "Linux version ", 14);
                at += 14;
                memory_copy_apart(at, build, head);
                at += head;
                memory_copy_apart(at, compiler, sizeof(compiler) - 1);
                at += sizeof(compiler) - 1;
                memory_copy_apart(at, build + head, string_length(build) - head);
                at += string_length(build) - head;
                *at = '\n';
        }
#endif
        memory_fill(image + 0x1400, body, 0xc00);
        host_settings_empty(address_of settings_block_slot);
        memory_copy_apart(image + 0x2000, address_of settings_block_slot, 64);
        memory_copy_apart(image + 0x6000, address_of settings_block_slot, 64);
        memory_fill(image + 0xa000, body ^ 0x5a, 0x2000);
        return UPDATE_BYTES;
}

// A path under the check's own directory.
static string_address update_at(p8 address_to into, positive room, string_address leaf)
{
        string_copy(into, update_root);
        string_append_bounded(into, leaf, room);
        return (string_address)into;
}

static bool update_write(string_address path, positive length)
{
        bipolar handle = system_open_at_mode(AT_FDCWD, path,
                                             FILE_READ_WRITE | FILE_CREATE | FILE_TRUNCATE |
                                                 O_CLOEXEC,
                                             0644);
        bool written;

        if (handle < 0)
                return false;
        written = storage_write(handle, update_bytes, length, 0) == (bipolar)length;
        system_close(handle);
        return written;
}

// An image written at a path under the check's directory.
static bool update_put(string_address leaf, p16 machine, string_address build, p8 body)
{
        p8 path[HOST_PATH_ROOM];

        return update_write(update_at(path, sizeof(path), leaf), update_image(machine, build, body));
}

static bool update_gone(string_address leaf)
{
        p8 path[HOST_PATH_ROOM];

        return system_access_at(AT_FDCWD, update_at(path, sizeof(path), leaf), 0) < 0;
}

// The partition and the sticks as they were before any scene.
static fn update_clear(void)
{
        static const string_address files[] = {
            "/esp" HOST_IMAGE,       "/esp" HOST_IMAGE_NEXT,  "/esp" HOST_KEPT_IMAGE,
            "/esp" HOST_KEPT_IMAGE_NEXT, "/stick" HOST_IMAGE, "/stick" HOST_UPDATE_FILE,
            "/stick" HOST_UPDATE_FILE HOST_DIGEST_SUFFIX,
        };
        p8 path[HOST_PATH_ROOM];

        for (positive at = 0; at < array_count(files); at++)
                system_remove_at(AT_FDCWD, update_at(path, sizeof(path), files[at]), 0);
}

static fn update_directories(bool remove)
{
        static const string_address made[] = {
            "", "/esp", "/esp/EFI", "/esp/EFI/BOOT", "/esp" HOST_KEPT_DIRECTORY, "/stick",
            "/stick/EFI", "/stick/EFI/BOOT", "/stick/moonwater",
        };
        p8 path[HOST_PATH_ROOM];

        for (positive at = 0; at < array_count(made); at++)
        {
                positive one = remove ? array_count(made) - 1 - at : at;

                update_at(path, sizeof(path), made[one]);
                if (remove)
                        system_remove_at(AT_FDCWD, path, AT_REMOVEDIR);
                else
                        system_make_directory_at(AT_FDCWD, path, 0755);
        }
}

// The sum of a file under the check's directory, its slots aside with masked.
static bool update_sum(string_address leaf, bool masked, p8 address_to sum)
{
        p8 path[HOST_PATH_ROOM];

        return host_image_sum(update_at(path, sizeof(path), leaf), masked, sum);
}

static bool update_sum_is(string_address leaf, bool masked, p8 address_to want)
{
        p8 sum[32];

        return update_sum(leaf, masked, sum) && !memory_compare(sum, want, 32);
}

/*
        The images of the scenes: the old one as the disk keeps it (stamped
        with the disk's settings), the one kept from before it, and the new
        one a stick carries; their sums, whole and with the slots aside.
*/
static p8 update_sum_old[32];      // the loader the partition had, as it is there
static p8 update_sum_old_body[32]; // and with its slots aside
static p8 update_sum_new_body[32];
static p8 update_sum_kept[32];     // previous.efi before the scene

static bool update_ready(void)
{
        p8 path[HOST_PATH_ROOM];

        update_clear();
        settings_one(address_of update_disk, 7, 'q', 120);
        return update_put("/esp" HOST_IMAGE, HOST_PE_MACHINE, (string_address)update_old, 0x11) &&
               !host_settings_stamp(update_at(path, sizeof(path), "/esp" HOST_IMAGE),
                                    address_of update_disk) &&
               update_put("/esp" HOST_KEPT_IMAGE, HOST_PE_MACHINE, (string_address)update_older,
                          0x33) &&
               update_put("/stick" HOST_UPDATE_FILE, HOST_PE_MACHINE, (string_address)update_new,
                          0x22) &&
               update_sum("/esp" HOST_IMAGE, false, update_sum_old) &&
               update_sum("/esp" HOST_IMAGE, true, update_sum_old_body) &&
               update_sum("/stick" HOST_UPDATE_FILE, true, update_sum_new_body) &&
               update_sum("/esp" HOST_KEPT_IMAGE, false, update_sum_kept);
}

// The settings an update of the partition carries: the loader's, a generation on.
static bool update_carried(void)
{
        p8 path[HOST_PATH_ROOM];

        if (host_settings_image(update_at(path, sizeof(path), "/esp" HOST_IMAGE),
                                address_of update_carry) != 1)
                return false;
        update_carry.generation++;
        return true;
}

/* An update from the stick, or with back a rollback, of the partition, to build. */
static bool update_open(host_place address_to place, bool back, string_address build)
{
        static p8 esp[HOST_PATH_ROOM];
        static p8 source[HOST_PATH_ROOM];

        update_at(esp, sizeof(esp), "/esp");
        update_at(source, sizeof(source), "/stick" HOST_UPDATE_FILE);
        if (!update_carried() ||
            !host_place_open(place, (string_address)esp, back ? null : (string_address)source,
                             build, address_of update_carry))
                return false;
        if (back)
                place->source = place->kept;
        return true;
}

/*
        What a firmware finds on the partition: a loader that is an image
        this machine starts, whole, whose bytes but its slots are the old
        image's or the new one's; which of them, 1 for the new, 0 for the old,
        -1 for neither.
*/
static b32 update_boots(void)
{
        p8 path[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];

        if (host_image_fit(update_at(path, sizeof(path), "/esp" HOST_IMAGE), build,
                           sizeof(build), null))
                return -1;
        if (string_equals(build, update_new) && update_sum_is("/esp" HOST_IMAGE, true,
                                                              update_sum_new_body))
                return 1;
        if (string_equals(build, update_old) && update_sum_is("/esp" HOST_IMAGE, true,
                                                              update_sum_old_body))
                return 0;
        return -1;
}

// Nothing beside the images, as every write ends and every next write begins.
static bool update_unstaged(void)
{
        return update_gone("/esp" HOST_IMAGE_NEXT) && update_gone("/esp" HOST_KEPT_IMAGE_NEXT);
}

static fn update_builds(void)
{
        check("a build linked a day later is the newer, the earlier one the older",
              host_build_order(update_new, update_old) == 1 &&
                  host_build_order(update_old, update_new) == -1 &&
                  host_build_order(update_older, update_old) == -1);
        check("a build is the same build as itself",
              host_build_order(update_old, update_old) == 0 &&
                  string_equals(host_build_named(update_old, update_old), "the same build"));
        check("a stick's build is named newer, the same or older beside this one",
              string_equals(host_build_named(update_new, update_old), "a newer build") &&
                  string_equals(host_build_named(update_older, update_old), "an older build"));
        check("two builds of one second are told apart by their count",
              host_build_order("7.2.6 (a@b) #9 SMP Tue Sep 22 07:21:00 UTC 2026",
                               "7.2.6 (a@b) #8 SMP Tue Sep 22 07:21:00 UTC 2026") == 1);
        check("a zone the table knows is taken to UTC: 02:10 CET is after 02:30 CEST",
              host_build_order("7.2.6 (a@b) #2 SMP Sun Oct 25 02:10:00 CET 2026",
                               "7.2.6 (a@b) #1 SMP Sun Oct 25 02:30:00 CEST 2026") == 1);
        check("a build whose words say no moment is not put before or after another",
              host_build_order("7.2.6 (a@b) #2 SMP some other words", update_old) ==
                      HOST_BUILD_UNORDERED &&
                  host_build_order("7.2.6 (a@b) #2 SMP Sun Oct 32 02:10:00 CET 2026", update_old) ==
                      HOST_BUILD_UNORDERED &&
                  string_equals(host_build_named("7.2.6 (a@b) #2 SMP x", update_old),
                                "another build"));
}

/*
        What an image has to be, judged by its bytes: one for this machine
        is taken and its build read; one for another machine, one that is no
        EFI application, one cut short of its sections and one that names no
        build are refused, each for its own reason, and an update from any
        of them leaves every byte of the partition as it was.
*/
static fn update_fits(void)
{
        static const struct
        {
                positive at;
                positive width;
                p64 value;
                positive length;
                b32 fit;
        } cases[] = {
            {0x44, 2, UPDATE_OTHER, UPDATE_BYTES, HOST_FIT_MACHINE},
            {0x58, 2, 0x10b, UPDATE_BYTES, HOST_FIT_NOT_EFI},     // PE32, not PE32+
            {0x58 + 68, 2, 3, UPDATE_BYTES, HOST_FIT_NOT_EFI},    // a console program
            {0x54, 2, 0x40, UPDATE_BYTES, HOST_FIT_NOT_EFI},      // no subsystem to read
            {0x40, 1, 'Q', UPDATE_BYTES, HOST_FIT_NOT_EFI},       // no PE header
            {0, 1, 0, 0x9000, HOST_FIT_SHORT},                    // its settings past the end
            {0, 1, 0, 0x800, HOST_FIT_NOT_EFI},                   // not a page of header
#if X64
            {0x20e, 2, 0, UPDATE_BYTES, HOST_FIT_NO_BUILD},
#else
            {0x1000, 1, 'X', UPDATE_BYTES, HOST_FIT_NO_BUILD},
#endif
        };
        p8 path[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];
        p8 sums[2][32];
        p16 machine = 0;
        p8 reasons = true;
        p8 untouched = true;
        host_place place;

        update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE);
        check("an image for this machine is taken, and names its build as the running one is spelled",
              update_ready() && !host_image_fit((string_address)path, build, sizeof(build),
                                                address_of machine) &&
                  string_equals(build, update_new) && machine == HOST_PE_MACHINE);

        for (positive at = 0; at < array_count(cases); at++)
        {
                positive length = update_image(HOST_PE_MACHINE, (string_address)update_new, 0x22);

                if (cases[at].width == 2)
                        storage_put16(update_bytes + cases[at].at, (p16)cases[at].value);
                else if (cases[at].at || cases[at].value)
                        update_bytes[cases[at].at] = (p8)cases[at].value;
                if (cases[at].length < length)
                        length = cases[at].length;
                if (!update_write((string_address)path, length))
                {
                        reasons = untouched = false;
                        continue;
                }
                reasons &= host_image_fit((string_address)path, build, sizeof(build),
                                          address_of machine) == cases[at].fit;

                //      And put through an update, which refuses it first.
                untouched &= update_sum("/esp" HOST_IMAGE, false, sums[0]) &&
                             update_sum("/esp" HOST_KEPT_IMAGE, false, sums[1]) &&
                             update_open(address_of place, false, (string_address)update_new) &&
                             host_place_written(address_of place, false) &&
                             update_sum_is("/esp" HOST_IMAGE, false, sums[0]) &&
                             update_sum_is("/esp" HOST_KEPT_IMAGE, false, sums[1]) &&
                             update_unstaged();
        }
        check("an image for another machine, no EFI application, one cut short and one that "
              "names no build are each refused for what they are",
              reasons);
        check("and an update from any of them is refused before a byte of the partition changes",
              untouched);
        check("the words for another machine name both",
              string_find(host_fit_said(HOST_FIT_MACHINE, UPDATE_OTHER),
                          host_pe_machine(UPDATE_OTHER)) &&
                  string_find(host_fit_said(HOST_FIT_MACHINE, UPDATE_OTHER),
                              host_pe_machine(HOST_PE_MACHINE)) &&
                  !string_equals(host_pe_machine(UPDATE_OTHER), host_pe_machine(HOST_PE_MACHINE)));

        update_at(path, sizeof(path), "/stick" HOST_IMAGE);
        check("a FIFO where an image goes is no image, and is not waited on",
              !system_call_4(syscall(mknodat), AT_FDCWD, (positive)(string_address)path,
                             0010644, 0) &&
                  host_image_fit((string_address)path, build, sizeof(build), null) ==
                      HOST_FIT_NONE);
        system_remove_at(AT_FDCWD, path, 0);
}

/*
        What a stick carries, looked at where it is mounted: a stick written
        by build.sh --usb has the loader, and a FAT stick anybody copied the
        one file to has the update file, which is the one taken when both are
        there; and which stick an update takes, of several looked at.
*/
static fn update_detects(void)
{
        static host_sticks sticks;
        host_stick stick;
        host_install install;
        p8 where[HOST_PATH_ROOM];

        update_clear();
        update_at(where, sizeof(where), "/stick");
        memory_zero(address_of stick, sizeof(stick));
        host_stick_look(address_of stick, (string_address)where);
        check("a FAT stick with no Moonwater on it carries nothing", !stick.file);

        update_put("/stick" HOST_IMAGE, HOST_PE_MACHINE, (string_address)update_new, 0x22);
        host_stick_look(address_of stick, (string_address)where);
        check("a stick written to start a machine is read where firmware starts it, without starting it",
              stick.file && string_equals(stick.file, HOST_IMAGE) && !stick.fit &&
                  string_equals(stick.build, update_new) &&
                  string_equals(host_build_named(stick.build, update_old), "a newer build"));

        update_clear();
        update_put("/stick" HOST_UPDATE_FILE, HOST_PE_MACHINE, (string_address)update_old, 0x11);
        host_stick_look(address_of stick, (string_address)where);
        check("a plain FAT stick with only the update file at its documented path is read",
              stick.file && string_equals(stick.file, HOST_UPDATE_FILE) && !stick.fit &&
                  string_equals(host_build_named(stick.build, update_old), "the same build"));

        update_put("/stick" HOST_IMAGE, HOST_PE_MACHINE, (string_address)update_older, 0x33);
        host_stick_look(address_of stick, (string_address)where);
        check("with both, the update file is the one taken",
              string_equals(stick.file, HOST_UPDATE_FILE) &&
                  string_equals(stick.build, update_old));

        update_clear();
        update_put("/stick" HOST_UPDATE_FILE, HOST_PE_MACHINE, (string_address)update_older, 0x33);
        host_stick_look(address_of stick, (string_address)where);
        check("a stick with an older build says so",
              !stick.fit && string_equals(host_build_named(stick.build, update_old),
                                          "an older build"));

        update_put("/stick" HOST_UPDATE_FILE, UPDATE_OTHER, (string_address)update_new, 0x22);
        host_stick_look(address_of stick, (string_address)where);
        check("a stick with an image for another machine says it is refused, and why",
              stick.fit == HOST_FIT_MACHINE && stick.machine == UPDATE_OTHER);
        update_clear();

        //      Which one an update takes: only one, of another build than the disk's.
        memory_zero(address_of install, sizeof(install));
        string_copy(install.disk, "nvme0n1");
        string_copy(install.build, update_old);
        install.readable = true;

        memory_zero(address_of sticks, sizeof(sticks));
        sticks.count = 4;
        string_copy(sticks.found[0].name, "sdb1");
        string_copy(sticks.found[0].disk, "sdb");
        sticks.found[0].held = true;
        string_copy(sticks.found[1].name, "sdc1");
        sticks.found[1].fit = HOST_FIT_NONE;
        string_copy(sticks.found[2].name, "sdd1");
        string_copy(sticks.found[2].build, update_old);
        string_copy(sticks.found[3].name, "sde1");
        string_copy(sticks.found[3].build, update_new);
        check("of a held stick, a refused one, one of the disk's build and a newer one, the newer is taken",
              host_stick_chosen(address_of sticks, address_of install) == sticks.found + 3);
        check("a held stick alone is no stick to update from",
              (sticks.count = 1, !host_stick_chosen(address_of sticks, address_of install)));
        sticks.found[0] = sticks.found[2];
        check("nor is one that carries the build the disk has",
              !host_stick_chosen(address_of sticks, address_of install));
        sticks.found[1] = sticks.found[3];
        sticks.found[0] = sticks.found[3];
        sticks.count = 2;
        string_copy(sticks.found[1].name, "sdf1");
        check("and two that carry a build to write are refused, the person to choose by plugging",
              !host_stick_chosen(address_of sticks, address_of install));
        //      What the census last read of a disk whose image cannot be read is no build.
        install.readable = false;
        string_copy(install.build, update_new);
        sticks.count = 1;
        check("a disk whose image cannot be read takes the build of the one stick there is",
              host_stick_chosen(address_of sticks, address_of install) == sticks.found);
}

/* The sum a stick says beside its update: none, its own, another, and words that are none. */
static fn update_digests(void)
{
        p8 path[HOST_PATH_ROOM];
        p8 line[160];
        p8 sum[32];
        bool said = true;
        positive length;

        update_clear();
        update_put("/stick" HOST_UPDATE_FILE, HOST_PE_MACHINE, (string_address)update_new, 0x22);
        update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE);
        check("an update with no sum beside it is taken, and not said to match one",
              !host_stick_digest((string_address)path, address_of said) && !said);

        update_sum("/stick" HOST_UPDATE_FILE, false, sum);
        length = memory_into_hex(line, sum, 32);
        memory_copy_apart(line + length, "  bootx64.efi\n", 14);
        length += 14;
        memory_copy_apart(update_bytes, line, length);
        update_write(update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE HOST_DIGEST_SUFFIX),
                     length);
        update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE);
        check("an update whose bytes are the sum beside it is taken, and said to be",
              !host_stick_digest((string_address)path, address_of said) && said);

        line[0] = line[0] == '0' ? '1' : '0';
        memory_copy_apart(update_bytes, line, length);
        update_write(update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE HOST_DIGEST_SUFFIX),
                     length);
        update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE);
        check("an update that is not the sum beside it is refused",
              host_stick_digest((string_address)path, address_of said) == 1);

        memory_copy_apart(update_bytes, "not a sum\n", 10);
        update_write(update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE HOST_DIGEST_SUFFIX),
                     10);
        update_at(path, sizeof(path), "/stick" HOST_UPDATE_FILE);
        check("and so is one beside words that are no sum",
              host_stick_digest((string_address)path, address_of said) == 1);
        update_clear();
}

/*
        An update and a rollback whole: the new build is the loader, carrying
        the disk's settings a generation on, the one it had kept as
        previous.efi byte for byte, nothing left beside them; a rollback
        swaps the two, carrying the settings the disk has then; a second one
        swaps them back.
*/
static fn update_places(void)
{
        host_place place;
        p8 path[HOST_PATH_ROOM];
        p8 loader[32];
        host_settings read;

        check("an update from a stick writes its build over the partition",
              update_ready() && update_open(address_of place, false, (string_address)update_new) &&
                  !host_place_written(address_of place, false) && update_boots() == 1 &&
                  place.carried);
        check("and the build the partition had is kept as previous.efi, byte for byte",
              update_sum_is("/esp" HOST_KEPT_IMAGE, false, update_sum_old));
        check("and the disk's settings go with it, a generation on",
              host_settings_image(update_at(path, sizeof(path), "/esp" HOST_IMAGE),
                                  address_of read) == 1 &&
                  read.generation == update_disk.generation + 1 &&
                  settings_same(address_of read, address_of update_carry) &&
                  !memory_compare(read.medium, update_disk.medium, sizeof(read.medium)));
        check("and nothing is left beside them", update_unstaged());

        update_sum("/esp" HOST_IMAGE, false, loader);
        check("a rollback starts the build kept from before again",
              update_open(address_of place, true, (string_address)update_old) &&
                  !host_place_written(address_of place, true) && update_boots() == 0 &&
                  place.carried);
        check("and keeps the build it replaces as previous.efi, byte for byte",
              update_sum_is("/esp" HOST_KEPT_IMAGE, false, loader) && update_unstaged());
        check("and carries the settings the disk has now",
              host_settings_image(update_at(path, sizeof(path), "/esp" HOST_IMAGE),
                                  address_of read) == 1 &&
                  read.generation == update_disk.generation + 2);

        check("a second rollback undoes the first",
              update_open(address_of place, true, (string_address)update_new) &&
                  !host_place_written(address_of place, true) && update_boots() == 1 &&
                  update_unstaged() &&
                  update_sum_is("/esp" HOST_KEPT_IMAGE, true, update_sum_old_body));

        //      What a cut left beside the images, which on FAT can share its
        //      clusters with a file that is kept, is gone before the first copy.
        update_put("/esp" HOST_IMAGE_NEXT, HOST_PE_MACHINE, (string_address)update_older, 0x44);
        update_put("/esp" HOST_KEPT_IMAGE_NEXT, HOST_PE_MACHINE, (string_address)update_older, 0x44);
        check("what a cut write left beside the images is removed before anything is copied",
              update_open(address_of place, false, (string_address)update_new) &&
                  !host_place_step(address_of place, HOST_PLACE_JUDGE) &&
                  !host_place_step(address_of place, HOST_PLACE_CLEAR) && update_unstaged());

        system_remove_at(AT_FDCWD, update_at(path, sizeof(path), "/esp" HOST_KEPT_IMAGE), 0);
        update_sum("/esp" HOST_IMAGE, false, loader);
        check("a rollback with no build kept from before is refused, the loader as it was",
              update_open(address_of place, true, (string_address)update_old) &&
                  host_place_written(address_of place, true) &&
                  update_sum_is("/esp" HOST_IMAGE, false, loader) && update_unstaged());

        update_put("/esp" HOST_KEPT_IMAGE, UPDATE_OTHER, (string_address)update_old, 0x11);
        check("and so is one whose kept build is for another machine",
              update_open(address_of place, true, (string_address)update_old) &&
                  host_place_written(address_of place, true) &&
                  update_sum_is("/esp" HOST_IMAGE, false, loader) && update_unstaged());
        update_clear();
}

/*
        The power going out after every step of an update and of a
        rollback, and inside each copy (the file being written cut to half):
        what is left starts the build there before or the one asked for,
        whole, and keeps a build from before that is whole; and the next
        update, or rollback, from there ends as an uncut one does.
*/
static b32 update_cut(bool back, positive steps, bool inside)
{
        host_place place;
        positive copy = back ? HOST_BACK_COPY : HOST_PLACE_COPY;
        positive keep = back ? HOST_BACK_KEEP_COPY : HOST_PLACE_KEEP_COPY;
        b32 boots;

        if (!update_ready())
                return -2;

        //      A rollback is cut from what an update leaves: the new build the
        //      loader, the old one kept.
        if (back && (!update_open(address_of place, false, (string_address)update_new) ||
                     host_place_written(address_of place, false)))
                return -2;

        if (!update_open(address_of place, back,
                         (string_address)(back ? update_old : update_new)))
                return -2;

        for (positive step = 0; step < steps; step++)
                if (back ? host_back_step(address_of place, step)
                         : host_place_step(address_of place, step))
                        return -2;

        if (inside && (steps - 1 == copy || steps - 1 == keep))
        {
                string_address cut = (string_address)(steps - 1 == copy ? place.next
                                                                       : place.kept_next);
                bipolar handle = system_open_at(AT_FDCWD, cut, FILE_READ_WRITE | O_CLOEXEC);

                if (handle < 0)
                        return -2;
                system_call_2(syscall(ftruncate), (positive)handle, UPDATE_BYTES / 2);
                system_close(handle);
        }

        boots = update_boots();
        //      What is kept from before is whole, whichever it is now.
        if (!update_sum_is("/esp" HOST_KEPT_IMAGE, false, update_sum_kept) &&
            !update_sum_is("/esp" HOST_KEPT_IMAGE, false, update_sum_old) &&
            !update_sum_is("/esp" HOST_KEPT_IMAGE, true, update_sum_new_body))
                return -1;
        return boots;
}

/*
        The next write from where a cut left the partition, and what it
        leaves. An update cut before its switch is run again and ends as an
        uncut one; one cut after it is done, with the old build kept. A
        rollback cut before its switch is run again; one cut after it starts
        the old build, kept twice where its last rename did not land (the
        new one only in the copy beside them), and the next update from the
        stick takes the copy away and ends as an uncut update does.
*/
static bool update_after(bool back, b32 boots)
{
        host_place place;

        if (boots == (back ? 1 : 0))
                return update_open(address_of place, back,
                                   (string_address)(back ? update_old : update_new)) &&
                       !host_place_written(address_of place, back) &&
                       update_boots() == (back ? 0 : 1) &&
                       update_sum_is("/esp" HOST_KEPT_IMAGE, true,
                                     back ? update_sum_new_body : update_sum_old_body) &&
                       update_unstaged();
        if (!back)
                return update_sum_is("/esp" HOST_KEPT_IMAGE, true, update_sum_old_body) &&
                       update_unstaged();
        return update_open(address_of place, false, (string_address)update_new) &&
               !host_place_written(address_of place, false) && update_boots() == 1 &&
               update_sum_is("/esp" HOST_KEPT_IMAGE, true, update_sum_old_body) &&
               update_unstaged();
}

/* Every cut of an update, or a rollback with back: into held, whether each
   left a loader firmware starts (0, after a step; 1, inside a copy), whether
   it was the one it should be, and whether the next write ended right. */
static fn update_cuts_of(bool back, p8 address_to held)
{
        positive steps = back ? HOST_BACK_STEPS : HOST_PLACE_STEPS;
        positive switched = back ? HOST_BACK_SWITCH : HOST_PLACE_SWITCH;
        positive copy = back ? HOST_BACK_COPY : HOST_PLACE_COPY;
        positive keep = back ? HOST_BACK_KEEP_COPY : HOST_PLACE_KEEP_COPY;
        //      The build asked for once the switch is done, the one before until then.
        b32 asked = back ? 0 : 1;

        for (positive at = 0; at < 4; at++)
                held[at] = true;

        for (positive cut = 0; cut <= steps; cut++)
                for (positive half = 0; half < 2; half++)
                {
                        b32 boots;

                        if (half && !(cut && (cut - 1 == copy || cut - 1 == keep)))
                                continue;
                        boots = update_cut(back, cut, half);
                        if (boots < 0)
                        {
                                held[half] = false;
                                continue;
                        }
                        held[2] &= boots == (cut > switched ? asked : 1 - asked);
                        held[3] &= update_after(back, boots);
                }
}

static fn update_cuts(void)
{
        p8 held[4];

        update_cuts_of(false, held);
        check("an update cut after any step starts the build it had or the new one, whole", held[0]);
        check("and cut inside either of its copies, the same", held[1]);
        check("and the one it had until its switch, the new one after", held[2]);
        check("and the next update from there ends as an uncut one does", held[3]);

        update_cuts_of(true, held);
        check("a rollback cut after any step starts the build it had or the one asked for, whole",
              held[0]);
        check("and cut inside either of its copies, the same", held[1]);
        check("and the one it had until its switch, the one asked for after", held[2]);
        check("and the next write from there ends as an uncut one does", held[3]);
        update_clear();
}

/*
        An update and a rollback over a loader that cannot be read: a link to
        a directory stands for one here, a name that opens and does not read
        and that a rename replaces. Each writes its image all the same and
        keeps nothing of the loader, so previous.efi is the build it kept
        before, whole, and nothing is left beside them; where the copy of the
        loader that previous.new is made from stopped both before anything
        was written, and a loader gone bad could not be replaced at all.
*/
static b32 update_unread_loader(void)
{
        p8 path[HOST_PATH_ROOM];
        p8 aside[HOST_PATH_ROOM];

        update_at(path, sizeof(path), "/esp" HOST_IMAGE);
        update_at(aside, sizeof(aside), "/esp/EFI/unread");
        system_make_directory_at(AT_FDCWD, (string_address)aside, 0755);
        return system_remove_at(AT_FDCWD, (string_address)path, 0) >= 0 &&
               system_symbolic_link_at((string_address)aside, AT_FDCWD, (string_address)path) >= 0;
}

static fn update_unreadable(void)
{
        host_place place;
        p8 path[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];
        bool ready = update_ready() &&
                     update_open(address_of place, false, (string_address)update_new) &&
                     update_unread_loader();

        check("an update over a loader that cannot be read writes its image all the same",
              ready && !host_place_written(address_of place, false) && update_boots() == 1);
        check("and keeps nothing of it: previous.efi is the build kept before, whole, and "
              "nothing is left beside them",
              ready && update_sum_is("/esp" HOST_KEPT_IMAGE, false, update_sum_kept) &&
                  update_unstaged());

        ready = ready && update_open(address_of place, true, (string_address)update_older) &&
                update_unread_loader();
        check("a rollback over a loader that cannot be read starts the build asked for all the same",
              ready && !host_place_written(address_of place, true) &&
                  !host_image_fit(update_at(path, sizeof(path), "/esp" HOST_IMAGE), build,
                                  sizeof(build), null) &&
                  string_equals(build, update_older));
        check("and leaves previous.efi as it was, whole, and nothing beside them",
              ready && update_sum_is("/esp" HOST_KEPT_IMAGE, false, update_sum_kept) &&
                  update_unstaged());

        system_remove_at(AT_FDCWD, update_at(path, sizeof(path), "/esp/EFI/unread"), AT_REMOVEDIR);
        update_clear();
}

/*
        A rollback whose last rename fails, after its switch: previous.efi is
        made a directory with a file in it between the switch and the rename,
        which a rename of a file cannot replace. The loader is the build asked
        for and previous.efi still is too, so the build the loader was is in
        previous.new and nowhere else: what is left of the stopped write keeps
        it, whole, and says the switch was made, where it was removed with the
        copies a stopped write leaves and the rollback said only that it failed.
*/
static fn update_back_stopped(void)
{
        host_place place;
        p8 path[HOST_PATH_ROOM];
        p8 inside[HOST_PATH_ROOM];
        p8 loader[32];
        bool ready = update_ready() &&
                     update_open(address_of place, false, (string_address)update_new) &&
                     !host_place_written(address_of place, false) &&
                     update_sum("/esp" HOST_IMAGE, false, loader) &&
                     update_open(address_of place, true, (string_address)update_old);
        bool stepped = ready;
        bool stopped;

        for (positive step = 0; stepped && step <= HOST_BACK_SWITCH; step++)
                stepped = !host_back_step(address_of place, step);

        update_at(path, sizeof(path), "/esp" HOST_KEPT_IMAGE);
        update_at(inside, sizeof(inside), "/esp" HOST_KEPT_IMAGE "/held");
        stepped = stepped && system_remove_at(AT_FDCWD, path, 0) >= 0 &&
                  system_make_directory_at(AT_FDCWD, path, 0755) >= 0 &&
                  update_write((string_address)inside, 16);
        stopped = stepped && host_back_step(address_of place, HOST_BACK_KEEP);
        if (stopped)
                host_place_stopped(address_of place, HOST_BACK_KEEP);

        check("a rollback whose last rename fails after its switch is stopped there",
              stopped && place.switched && place.stranded);
        check("and the loader is the build asked for", stopped && update_boots() == 0);
        check("and the build the loader was is still beside it in previous.new, whole",
              stopped && update_sum_is("/esp" HOST_KEPT_IMAGE_NEXT, false, loader) &&
                  update_gone("/esp" HOST_IMAGE_NEXT));

        system_remove_at(AT_FDCWD, (string_address)inside, 0);
        system_remove_at(AT_FDCWD, path, AT_REMOVEDIR);
        update_clear();
}


/*
        The boot splash (src/moonwater/splash.c, and its half of src/sh/host.c): what a file of a picture
        is, where one goes on a screen and how the firmware is handed it, which is the code the loader
        runs too (kernel/patch/splash-stub.c includes the same file); and what moonwater splash and an
        install write, on a system partition that is a directory: the file by its steps, nothing beside
        it, an update and a rollback that leave it, and the firmware's maker read from hostile words.
*/
static p8 splash_check_file[SPLASH_FILE_MOST + 2];
static p8 splash_check_other[SPLASH_FILE_MOST + 2];

// A picture of width by height as a file at into, no two pixels near each other alike: its length.
static positive splash_check_make(p8 address_to into, unsigned int width, unsigned int height)
{
        positive head = splash_header(into, width, height);

        for (positive at = 0; at < (positive)width * height; at++)
        {
                into[head + at * 3] = (p8)(at * 5 + 1);
                into[head + at * 3 + 1] = (p8)(at * 3 + 2);
                into[head + at * 3 + 2] = (p8)(at * 7 + 3);
        }
        return head + (positive)width * height * 3;
}

// Whether bytes are refused, and for the reason said.
static bool splash_check_refused(const p8 address_to bytes, positive length, string_address why)
{
        struct splash_image image;
        const char address_to reason = splash_parse(bytes, length, address_of image);

        return reason && string_equals((string_address)reason, why);
}

static bool splash_check_text(p8 address_to into, string_address header, positive pixels, string_address why)
{
        positive head = string_length(header);

        memory_copy_apart(into, header, head);
        memory_fill(into + head, 0x40, pixels);
        return why ? splash_check_refused(into, head + pixels, why) : !splash_parse(into, head + pixels, address_of(struct splash_image){0});
}

static fn splash_format(void)
{
        static const struct
        {
                string_address header;
                positive pixels;
                string_address why;
        } refused[] = {
            {"P5\n3 2\n255\n", 18, "not a binary PPM (P6) image"},
            {"P", 0, "not a binary PPM (P6) image"},
            {"", 0, "not a binary PPM (P6) image"},
            {"P6\n3 2\n", 0, "the header is cut short or has no number in it"},
            {"P6\n3 x\n255\n", 18, "the header is cut short or has no number in it"},
            {"P6\n3 2 # 255\n", 18, "the header is cut short or has no number in it"},
            {"P6\n70000 2\n255\n", 18, "the header is cut short or has no number in it"},
            {"P6\n99999999999999999999999 2\n255\n", 18, "the header is cut short or has no number in it"},
            {"P6\n0 2\n255\n", 0, "the size is not 1 to 1024 pixels a side"},
            {"P6\n3 0\n255\n", 0, "the size is not 1 to 1024 pixels a side"},
            {"P6\n1025 1\n255\n", 3075, "the size is not 1 to 1024 pixels a side"},
            {"P6\n1 1025\n255\n", 3075, "the size is not 1 to 1024 pixels a side"},
            {"P6\n3 2\n254\n", 18, "the maximum value is not 255"},
            {"P6\n3 2\n65535\n", 36, "the maximum value is not 255"},
            {"P6\n3 2\n255", 0, "the pixels do not follow the header"},
            {"P6\n3 2\n255x", 18, "the pixels do not follow the header"},
            {"P6\n3 2\n255\n", 17, "the pixels are not exactly width x height x 3 bytes"},
            {"P6\n3 2\n255\n", 19, "the pixels are not exactly width x height x 3 bytes"},
            {"P6\n3 2\n255\n", 0, "the pixels are not exactly width x height x 3 bytes"},
            {"P6\n1024 1024\n255\n", 18, "the pixels are not exactly width x height x 3 bytes"},
        };
        struct splash_image image;
        struct splash_place place;
        positive length = splash_check_make(splash_check_file, 3, 2);
        positive head = length - 18;
        positive state = 0x9e3779b97f4a7c15ull;
        bool held = true;
        p8 pixels[8];

        check("a picture's file is its header and its pixels, and reads back as its size and where they lie",
              head == 11 && !memory_compare(splash_check_file, "P6\n3 2\n255\n", 11) &&
                  !splash_parse(splash_check_file, length, address_of image) && image.width == 3 &&
                  image.height == 2 && image.rgb == splash_check_file + head);
        check("white space and comments between the numbers of a header are read past",
              splash_check_text(splash_check_other, "P6 # made by hand\n\t3\r\n# two rows\n2 255\n", 18, null) &&
                  splash_check_text(splash_check_other, "P6\n003 02\n255\t", 18, null));

        for (positive at = 0; at < array_count(refused); at++)
                held &= splash_check_text(splash_check_other, refused[at].header, refused[at].pixels, refused[at].why);
        check("a file of another kind, a header cut short or too large a number, a side that is nought or over 1024, "
              "another maximum, pixels that do not follow and a byte too few or too many are each refused for what they are",
              held);

        held = splash_check_refused(splash_check_file, length + 1, "the pixels are not exactly width x height x 3 bytes");
        for (positive cut = 0; cut < length; cut++)
                held &= splash_parse(splash_check_file, cut, address_of image) != null;
        check("every shorter length of a picture's file is refused, and one byte longer", held);

        length = splash_check_make(splash_check_other, 1024, 341);
        check("the largest picture a mebibyte holds is taken, and one row more is refused before a byte of it is read",
              length == 16 + 1024 * 341 * 3 && length <= SPLASH_FILE_MOST &&
                  !splash_parse(splash_check_other, length, address_of image) && image.width == 1024 && image.height == 341 &&
                  splash_check_refused(splash_check_other, SPLASH_FILE_MOST + 1, "it is larger than 1 MiB") &&
                  splash_check_refused((const p8 address_to)8, SPLASH_FILE_MOST + 1, "it is larger than 1 MiB"));

        //      A header made wrong at random, at a random length: what parses lies inside the bytes it was given, to the end.
        held = true;
        for (positive round = 0; round < 20000; round++)
        {
                positive given;

                length = splash_check_make(splash_check_file, 3, 2);
                for (positive change = 0; change < 3; change++)
                {
                        state ^= state << 13;
                        state ^= state >> 7;
                        state ^= state << 17;
                        splash_check_file[state % 12] = "0123456789 \n\t#P6x\0\377"[(state >> 8) % 19];
                }
                given = (state >> 20) % (length + 3);
                if (!splash_parse(splash_check_file, given, address_of image))
                        held &= image.width && image.height && image.rgb > splash_check_file &&
                                image.rgb + (positive)image.width * image.height * 3 == splash_check_file + given;
        }
        check("of 20000 headers made wrong at random, each one that parses names pixels that end where its bytes do", held);

        check("the header written for the largest size fits the room kept for it",
              splash_header(splash_check_file, 1024, 1024) == 17 && SPLASH_HEADER_MOST >= 17 &&
                  !memory_compare(splash_check_file, "P6\n1024 1024\n255\n", 17));

        image.width = 200;
        image.height = 100;
        splash_place(address_of image, 1280, 800, address_of place);
        held = place.x == 540 && place.y == 350 && place.width == 200 && place.height == 100 && !place.source_x && !place.source_y;
        splash_place(address_of image, 200, 100, address_of place);
        held &= !place.x && !place.y && place.width == 200 && place.height == 100 && !place.source_x && !place.source_y;
        check("a picture smaller than the screen is drawn whole in the middle of it, and one its size fills it", held);

        image.width = 700;
        image.height = 490;
        splash_place(address_of image, 640, 480, address_of place);
        held = !place.x && !place.y && place.width == 640 && place.height == 480 && place.source_x == 30 && place.source_y == 5;
        image.width = 1024;
        image.height = 10;
        splash_place(address_of image, 800, 600, address_of place);
        held &= !place.x && place.y == 295 && place.width == 800 && place.height == 10 && place.source_x == 112 && !place.source_y;
        splash_place(address_of image, 0, 0, address_of place);
        held &= !place.width && !place.height;
        check("one larger than the screen is cut evenly at both sides, never past its own edge, and a screen of no pixels takes none",
              held);

        length = splash_check_make(splash_check_file, 2, 1);
        memory_fill(pixels, 0xee, sizeof(pixels));
        held = !splash_parse(splash_check_file, length, address_of image);
        splash_pixels(address_of image, pixels);
        check("the firmware is handed each pixel as blue, green, red and a nought",
              held && pixels[0] == image.rgb[2] && pixels[1] == image.rgb[1] && pixels[2] == image.rgb[0] && !pixels[3] &&
                  pixels[4] == image.rgb[5] && pixels[5] == image.rgb[4] && pixels[6] == image.rgb[3] && !pixels[7] &&
                  image.rgb[0] != image.rgb[2]);
}

static bool splash_check_put(string_address leaf, const p8 address_to bytes, positive length)
{
        p8 path[HOST_PATH_ROOM];
        bipolar handle = system_open_at_mode(AT_FDCWD, update_at(path, sizeof(path), leaf),
                                             FILE_READ_WRITE | FILE_CREATE | FILE_TRUNCATE | O_CLOEXEC, 0644);
        bool written;

        if (handle < 0)
                return false;
        written = !length || storage_write(handle, (p8 address_to)bytes, length, 0) == (bipolar)length;
        system_close(handle);
        return written;
}

static bool splash_check_is(string_address leaf, const p8 address_to bytes, positive length)
{
        p8 path[HOST_PATH_ROOM];

        return host_splash_file_is(update_at(path, sizeof(path), leaf), bytes, length);
}

static fn splash_check_remove(string_address leaf, bool directory)
{
        p8 path[HOST_PATH_ROOM];

        system_remove_at(AT_FDCWD, update_at(path, sizeof(path), leaf), directory ? AT_REMOVEDIR : 0);
}

static fn splash_files(void)
{
        static p8 esp[HOST_PATH_ROOM];
        static p8 stick[HOST_PATH_ROOM];
        struct splash_image image;
        string_address reason = null;
        host_splash_want want;
        host_place place;
        p8 path[HOST_PATH_ROOM];
        positive length;
        positive made;
        bool held;

        update_at(esp, sizeof(esp), "/esp");
        update_at(stick, sizeof(stick), "/stick");
        system_make_directory_at(AT_FDCWD, update_at(path, sizeof(path), "/stick" SPLASH_DIRECTORY), 0755);

        //      A person's file: a header of its own, with a comment, and the pixels of the 3 by 2 the format rows made.
        made = splash_check_make(splash_check_file, 3, 2);
        memory_copy_apart(splash_check_other, "P6 # mine\n0003\t2\n\n255\n", 22);
        memory_copy_apart(splash_check_other + 22, splash_check_file + 11, 18);
        held = splash_check_put("/person.ppm", splash_check_other, 40);
        length = host_splash_take(update_at(path, sizeof(path), "/person.ppm"), address_of image, address_of reason);
        check("a person's picture is kept as the header this writes and the pixels that were checked, never the header it came with",
              held && length == made && !reason && !memory_compare(host_splash_bytes, splash_check_file, made) &&
                  image.width == 3 && image.height == 2 && image.rgb == host_splash_bytes + 11);

        held = splash_check_put("/person.ppm", splash_check_other, 41);
        length = host_splash_take(update_at(path, sizeof(path), "/person.ppm"), address_of image, address_of reason);
        held &= !length && reason && string_equals(reason, "the pixels are not exactly width x height x 3 bytes");
        memory_fill(splash_check_other + 40, 0, SPLASH_FILE_MOST + 1 - 40);
        held &= splash_check_put("/person.ppm", splash_check_other, SPLASH_FILE_MOST + 1);
        length = host_splash_take(update_at(path, sizeof(path), "/person.ppm"), address_of image, address_of reason);
        held &= !length && reason && string_equals(reason, "it is larger than 1 MiB");
        splash_check_remove("/person.ppm", false);
        length = host_splash_take(update_at(path, sizeof(path), "/person.ppm"), address_of image, address_of reason);
        held &= !length && reason && reason[0];
        length = host_splash_take((string_address)esp, address_of image, address_of reason);
        held &= !length && reason && reason[0];
        check("one a byte too long, one over a mebibyte, one that is not there and a directory are each no picture, with the reason",
              held);

        length = host_splash_default();
        check("the built-in picture is a file of its own size that checks, grey and not blank",
              length == 15 + SPLASH_MARK_W * SPLASH_MARK_H * 3 && !splash_parse(host_splash_bytes, length, address_of image) &&
                  image.width == SPLASH_MARK_W && image.height == SPLASH_MARK_H);
        held = true;
        made = 0;
        for (positive at = 0; at < (positive)SPLASH_MARK_W * SPLASH_MARK_H; at++)
        {
                const p8 address_to pixel = image.rgb + at * 3;

                held &= pixel[0] == pixel[1] && pixel[1] == pixel[2] && !(pixel[0] % 85);
                made += pixel[0] != 255;
        }
        check("and every pixel of it is one of four greys, dark ink on a pure white ground",
              held && made > 1000 && made < (positive)SPLASH_MARK_W * SPLASH_MARK_H / 2);

        //      The steps of a write, on a partition that is a directory.
        made = splash_check_make(splash_check_file, 40, 30);
        check("a picture is written to its name on the system partition, and nothing is left beside it",
              !host_splash_place((string_address)esp, splash_check_file, made) && splash_check_is("/esp" SPLASH_FILE, splash_check_file, made) &&
                  update_gone("/esp" SPLASH_FILE_NEXT));
        length = splash_check_make(splash_check_other, 7, 9);
        held = splash_check_put("/esp" SPLASH_FILE_NEXT, splash_check_file, 4000);
        check("another over it replaces it whole, and what a cut write left beside it is removed and not kept",
              held && !host_splash_place((string_address)esp, splash_check_other, length) &&
                  splash_check_is("/esp" SPLASH_FILE, splash_check_other, length) && update_gone("/esp" SPLASH_FILE_NEXT));

        want.bytes = splash_check_other;
        want.length = length;
        held = host_splash_landed((string_address)esp, address_of want);
        want.length = length - 1;
        held &= !host_splash_landed((string_address)esp, address_of want);
        want.bytes = splash_check_file;
        want.length = length;
        held &= !host_splash_landed((string_address)esp, address_of want);
        want.bytes = null;
        held &= !host_splash_landed((string_address)esp, address_of want);
        check("what is read back is the picture written to the byte: one shorter, another and none are each not it", held);

        check("a picture is not written where there is no partition, or with no bytes or over a mebibyte of them",
              host_splash_place(update_at(path, sizeof(path), "/nowhere"), splash_check_file, made) &&
                  host_splash_place((string_address)esp, splash_check_file, 0) &&
                  host_splash_place((string_address)esp, splash_check_file, SPLASH_FILE_MOST + 1) &&
                  splash_check_is("/esp" SPLASH_FILE, splash_check_other, length) && update_gone("/esp" SPLASH_FILE_NEXT));

        //      An update and a rollback are the image's steps, and the picture is none of their files.
        held = update_ready() && !host_splash_place((string_address)esp, splash_check_file, made);
        check("an update from a stick leaves the picture the partition had, byte for byte",
              held && update_open(address_of place, false, (string_address)update_new) &&
                  !host_place_written(address_of place, false) && update_boots() == 1 &&
                  splash_check_is("/esp" SPLASH_FILE, splash_check_file, made));
        check("and a rollback does",
              update_open(address_of place, true, (string_address)update_old) && !host_place_written(address_of place, true) &&
                  update_boots() == 0 && splash_check_is("/esp" SPLASH_FILE, splash_check_file, made));
        held = splash_check_put("/esp" SPLASH_FILE_NEXT, splash_check_other, 5000);
        check("what a cut write of a picture left is removed by the next write of an image, before anything is copied",
              held && update_open(address_of place, false, (string_address)update_new) &&
                  !host_place_step(address_of place, HOST_PLACE_JUDGE) && !host_place_step(address_of place, HOST_PLACE_CLEAR) &&
                  update_gone("/esp" SPLASH_FILE_NEXT) && splash_check_is("/esp" SPLASH_FILE, splash_check_file, made));

        held = splash_check_put("/esp" SPLASH_FILE_NEXT, splash_check_other, 5000);
        want.bytes = null;
        want.length = 0;
        check("off removes the picture and what was left beside it, reads back as none, and off again is no error",
              held && !host_splash_clear((string_address)esp) && update_gone("/esp" SPLASH_FILE) &&
                  update_gone("/esp" SPLASH_FILE_NEXT) && host_splash_landed((string_address)esp, address_of want) &&
                  !host_splash_clear((string_address)esp));

        //      An install: the picture of the medium it is made from when that checks, else the built-in one.
        made = splash_check_make(splash_check_file, 12, 5);
        held = splash_check_put("/stick" SPLASH_FILE, splash_check_file, made);
        length = 0;
        host_splash_install((string_address)stick, (string_address)esp, "disk0", address_of length);
        check("an install gives the disk the picture of the session it is made from",
              held && length == made && splash_check_is("/esp" SPLASH_FILE, splash_check_file, made) &&
                  update_gone("/esp" SPLASH_FILE_NEXT));
        splash_check_remove("/stick" SPLASH_FILE, false);
        host_splash_install((string_address)stick, (string_address)esp, "disk0", address_of length);
        made = host_splash_default();
        memory_copy_apart(splash_check_other, host_splash_bytes, made);
        check("and the built-in one when that session keeps none", length == made && splash_check_is("/esp" SPLASH_FILE, splash_check_other, made));
        held = splash_check_put("/stick" SPLASH_FILE, splash_check_file, 9);
        host_splash_clear((string_address)esp);
        host_splash_install((string_address)stick, (string_address)esp, "disk0", address_of length);
        check("and the built-in one when what that session keeps does not check",
              held && length == made && splash_check_is("/esp" SPLASH_FILE, splash_check_other, made));
        length = 7;
        host_splash_install((string_address)stick, update_at(path, sizeof(path), "/nowhere"), "disk0", address_of length);
        check("an install that cannot write a picture says so and has none to read back", !length);

        //      An update ensures the boot splash is set up: missing or legacy dark default gets the new white/black splash,
        //      a custom picture is kept when the medium has none, and a medium's own picture replaces it.
        splash_check_remove("/stick" SPLASH_FILE, false);
        host_splash_clear((string_address)esp);
        length = 0;
        host_splash_ensure((string_address)stick, (string_address)esp, "disk0", address_of length);
        check("an update installs the built-in splash when the disk has none",
              length == made && splash_check_is("/esp" SPLASH_FILE, splash_check_other, made));

        {
                positive head = splash_header(splash_check_file, SPLASH_MARK_W, SPLASH_MARK_H);

                for (positive at = 0; at < (positive)SPLASH_MARK_W * SPLASH_MARK_H; at++)
                {
                        p8 grey = (p8)(((splash_mark[at >> 2] >> (6 - 2 * (at & 3))) & 3u) * 85u);

                        splash_check_file[head + at * 3] = grey;
                        splash_check_file[head + at * 3 + 1] = grey;
                        splash_check_file[head + at * 3 + 2] = grey;
                }
                held = !host_splash_place((string_address)esp, splash_check_file, head + SPLASH_MARK_W * SPLASH_MARK_H * 3);
                length = 0;
                host_splash_ensure((string_address)stick, (string_address)esp, "disk0", address_of length);
                check("an update upgrades the legacy dark default splash to the white/black splash",
                      held && length == made && splash_check_is("/esp" SPLASH_FILE, splash_check_other, made));
        }

        {
                positive custom = splash_check_make(splash_check_file, 14, 6);

                held = !host_splash_place((string_address)esp, splash_check_file, custom);
                length = 7;
                host_splash_ensure((string_address)stick, (string_address)esp, "disk0", address_of length);
                check("an update preserves a custom splash on disk when the medium carries none",
                      held && !length && splash_check_is("/esp" SPLASH_FILE, splash_check_file, custom));

                held = splash_check_put("/stick" SPLASH_FILE, splash_check_other, made);
                length = 0;
                host_splash_ensure((string_address)stick, (string_address)esp, "disk0", address_of length);
                check("and replaces it when the live medium carries a valid splash of its own",
                      held && length == made && splash_check_is("/esp" SPLASH_FILE, splash_check_other, made));
        }

        splash_check_remove("/stick" SPLASH_FILE, false);
        splash_check_remove("/stick" SPLASH_DIRECTORY, true);
        splash_check_remove("/esp" SPLASH_FILE, false);
        update_clear();
}

static fn splash_firmware(void)
{
        static const string_address known[] = {"LENOVO", "Lenovo", "HP", "Hewlett-Packard", "Dell Inc.", "ASUSTeK COMPUTER INC.", "Framework"};
        static const string_address unknown[] = {"", "QEMU", "len", "Le novo", "?LENOVO", "System manufacturer"};
        p8 vendor[HOST_SPLASH_VENDOR + 1];
        p8 path[HOST_PATH_ROOM];
        bool held = true;

        for (positive at = 0; at < array_count(known); at++)
                held &= host_splash_firmware_why(known[at]) != null;
        for (positive at = 0; at < array_count(unknown); at++)
                held &= host_splash_firmware_why(unknown[at]) == null;
        check("the makers whose firmware is known are told by how their name starts, in any case, and no other name is one of them", held);

        held = splash_check_put("/vendor", (const p8 address_to) "LENOVO  \nsecond line\n", 21);
        host_splash_vendor(update_at(path, sizeof(path), "/vendor"), vendor);
        held &= string_equals((string_address)vendor, "LENOVO");
        held &= splash_check_put("/vendor", (const p8 address_to) "A\x1b[31m\x07\xc3\xa5%s\x7f" "B\rC", 15);
        host_splash_vendor(update_at(path, sizeof(path), "/vendor"), vendor);
        held &= string_equals((string_address)vendor, "A?[31m???%s?B");
        memory_fill(splash_check_other, 'x', 4096);
        held &= splash_check_put("/vendor", splash_check_other, 4096);
        host_splash_vendor(update_at(path, sizeof(path), "/vendor"), vendor);
        held &= string_length((string_address)vendor) == HOST_SPLASH_VENDOR;
        splash_check_remove("/vendor", false);
        host_splash_vendor(update_at(path, sizeof(path), "/vendor"), vendor);
        held &= !vendor[0];
        check("the maker's name is DMI's first line with its controls and bytes that are not ASCII made question marks, cut at 48 bytes, "
              "and empty when there is none",
              held);
}

// Whether the file at path, a source of this tree, has needle in it: 1, 0, or -1 when it cannot be read from here.
static b32 splash_check_source(string_address path, string_address needle)
{
        static p8 chunk[65536 + 256];
        positive want = string_length(needle);
        positive kept = 0;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        b32 found = 0;

        if (handle < 0)
                return -1;
        for (;;)
        {
                bipolar got = system_read_once(handle, chunk + kept, 65536);
                positive have;

                if (got <= 0)
                        break;
                have = kept + (positive)got;
                for (positive at = 0; !found && at + want <= have; at++)
                        found = !memory_compare(chunk + at, needle, want);
                kept = have < want - 1 ? have : want - 1;
                memory_copy(chunk, chunk + have - kept, kept);
        }
        system_close(handle);
        return found;
}

/*
        The sources the rows above cannot reach by calling them, read from the tree the check runs in (the repository's
        root) and not run where they are not: the loader's file opens the name the shell writes, the kernel's patch puts
        that file among the stub's and calls it, and the compositor has no splash of its own left.
*/
static fn splash_sources(void)
{
        static const p8 file[] = SPLASH_FILE;
        p8 name[2 * sizeof(file) + 8] = "L\"";
        positive at = 2;

        for (positive each = 0; each + 1 < sizeof(file); each++)
        {
                if (file[each] == '/')
                {
                        name[at++] = '\\';
                        name[at++] = '\\';
                }
                else
                        name[at++] = file[each];
        }
        name[at++] = '"';
        name[at] = end;

        if (splash_check_source("kernel/patch/splash-stub.c", "efi_moonwater_splash") < 0 ||
            splash_check_source("src/canvas/compositor.c", "compositor") < 0)
        {
                string_format(log, "  boot splash sources: NOT RUN -- the tree's sources are not where this runs\n");
                return;
        }
        check("the loader opens the file the shell writes, by the same name",
              splash_check_source("kernel/patch/splash-stub.c", (string_address)name) == 1 &&
                  splash_check_source("kernel/patch/splash-stub.c", "kernel/moonwater/splash.c") == 1);
        check("the kernel's patch puts the loader's file among the stub's objects and calls it on each machine",
              splash_check_source("kernel/patch/apply", "moonwater-splash.o") == 1 &&
                  splash_check_source("kernel/patch/apply", "efi_moonwater_splash(handle);") == 1 &&
                  splash_check_source("kernel/patch/apply", "efi_decompress_kernel(&kernel_entry, boot_params);") == 1 &&
                  splash_check_source("kernel/patch/apply", "dpy = setup_primary_display();") == 1);
        {
                // compositor.c and the parts it includes (src/canvas/compositor/, test/compositor_split.py's cut list).
                static const char *const compositor_files[] = {
                    "src/canvas/compositor.c", "src/canvas/compositor/compose.c", "src/canvas/compositor/keymap.c",
                    "src/canvas/compositor/pointer.c", "src/canvas/compositor/outputs.c", "src/canvas/compositor/damage.c",
                    "src/canvas/compositor/glass.c", "src/canvas/compositor/scanout.c", "src/canvas/compositor/gpu_host.c",
                    "src/canvas/compositor/present.c", "src/canvas/compositor/kms.c", "src/canvas/compositor/session.c",
                    "src/canvas/compositor/manage.c", "src/canvas/compositor/input.c"};
                b32 none = splash_check_source("src/sh/registry.c", "splash") == 0 && splash_check_source("src/sh/registry.c", "Splash") == 0;

                for (unsigned int at = 0; at < sizeof(compositor_files) / sizeof(compositor_files[0]); at++)
                        none = none && splash_check_source((string_address)compositor_files[at], "splash") == 0 &&
                               splash_check_source((string_address)compositor_files[at], "Splash") == 0;
                check("the compositor draws no splash of its own: no word of one is left in it, its parts or the settings table", none);
        }
}

static fn splash_scenes(void)
{
        splash_format();
        splash_files();
        splash_firmware();
        splash_sources();
}

static fn update_scenes(void)
{
        string_copy(update_root, "/tmp/moonwater-update-check-");
        positive_into_string(update_root + string_length(update_root),
                             (positive)system_call(syscall(getpid)));
        update_directories(false);

        update_builds();
        update_fits();
        update_detects();
        update_digests();
        update_places();
        update_cuts();
        update_unreadable();
        update_back_stopped();
        splash_scenes();

        update_clear();
        update_directories(true);
}

b32 main(void)
{
        settings_format();
        settings_bounds();
        settings_security();
        settings_group();
        settings_torn();
        settings_commands();
        settings_sections();
        settings_hostile();
        image_builds();
        build_commit();
#if X64
        image_hostile();
#endif
        settings_codes();
        format_sums();
        format_spans();
        format_places();
        format_layouts();
        install_census();
        install_helpers();
        update_scenes();
        device_where();
        device_part_tail();
        device_open_rows();

        if (program_argument_count() > 1)
                format_images(program_argument_list()[1]);

        return test_report(null);
}
#endif /* CHECK_storage_format */

