/*
        test/checks/probe.c: the probe and the stack guard.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_probe
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"

/*
        The things only a separate process can prove.

        That the loader maps the three regions with the right permissions,
        that a stack of arguments arrives, that an exit status comes back
        through wait4, that an environment was handed over, that /dev/spark
        really spawns. None of these can be a shell builtin: a builtin is not
        exec'd, so it would be testing nothing.

        One program with a mode as its first argument, in the shape the
        shell itself is in -- the name decides -- and the probe lane of
        test/run runs every mode and checks what came back.

        Built as an ordinary freestanding binary it proves these things of
        the ELF loader, which is what test/run does with it. The same
        source built with `build spark` and placed in the image would prove them
        of the spark loader and let the spawn mode reach /dev/spark, and
        nothing builds it that way yet: the boot lane covers the spark loader
        only by the kernel exec'ing the shell as /init and the shell
        answering.
*/

p8 in_data[] = "data:starts-here";
p8 in_bss[64];

p8 environment_buffer[4096];
p8 logging_block[MAX_INPUT];

static b32 probe_arguments()
{
        b32 count = program_argument_count();

        string_format(log, "%b argument(s)\n", count);

        for (b32 i = 0; i < count; i++)
                string_format(log, "  %b: %s\n", i, program_argument(i));

        return 0;
}

// A writable global lives in .data, a zeroed one in .bss, and the code is
// executable or none of this is running. Each answer is a line, so a wrong
// one says which of the three regions was wrong.
static b32 probe_regions()
{
        b32 wrong = 0;

        for (positive i = 0; i < sizeof(in_bss); i++)
                if (in_bss[i])
                        wrong++;

        string_format(log, "bss zeroed:   %s\n", wrong ? "no" : "yes");

        if (string_compare(in_data, "data:starts-here"))
        {
                string_format(log, "data initial: no\n");
                wrong++;
        }
        else
                string_format(log, "data initial: yes\n");

        in_data[0] = 'D';
        in_data[5] = '!';

        if (string_compare(in_data, "Data:!tarts-here"))
        {
                string_format(log, "data written: no\n");
                wrong++;
        }
        else
                string_format(log, "data written: yes\n");

        in_bss[0] = 'B';
        in_bss[1] = 'S';
        in_bss[2] = 'S';
        in_bss[3] = end;

        if (string_compare(in_bss, "BSS"))
        {
                string_format(log, "bss written:  no\n");
                wrong++;
        }
        else
                string_format(log, "bss written:  yes\n");

        return wrong ? 1 : 0;
}

// Out of /proc, which is the only place the environment a spawner actually
// handed over can be read back. Entries are NUL separated.
static b32 probe_environment()
{
        bipolar file = system_call_4(syscall(openat), AT_FDCWD,
                                     (positive) "/proc/self/environ", FILE_READ, 0);
        bipolar length;
        positive at = 0;

        if (file < 0)
        {
                string_format(log, "no /proc/self/environ: %b\n", file);
                return 1;
        }

        length = system_call_3(syscall(read), file, (positive)environment_buffer,
                               sizeof(environment_buffer) - 1);
        system_call_1(syscall(close), file);

        if (length <= 0)
        {
                string_format(log, "environment is empty\n");
                return 1;
        }

        environment_buffer[length] = end;

        while (at < (positive)length)
        {
                string_format(log, "  env: %s\n", environment_buffer + at);
                at += string_length(environment_buffer + at) + 1;
        }

        return 0;
}

/*
        Exercise both sides of the logger's exact boundary. A plus 4095 bytes
        fills the buffer without flushing; B takes it over and flushes it;
        the 4096-byte block takes the direct path; the final zero length asks
        string_length for DE. The harness compares every emitted byte.
*/
static b32 probe_logging()
{
        memory_fill(logging_block, 'x', sizeof(logging_block));

        log("A", 1);
        log(logging_block, MAX_INPUT - 1);
        log("B", 1);
        log(logging_block, MAX_INPUT);
        log("C", 1);
        log("DE", 0);

        return 0;
}

/*
        A diagnostic first drains buffered stdout, then makes exactly one
        stderr write. The first call asks string_length for its size; the
        second supplies one and leaves the trailing bytes unwritten.
*/
static b32 probe_diagnostic()
{
        log("stdout-before\n", 0);
        log_error("stderr-zero\n", 0);
        log("stdout-middle\n", 0);
        log_error("stderr-explicit\nTRAIL", 16);
        log("stdout-after\n", 0);

        return 0;
}

// Redirected stdout is not a terminal, so ioctl must leave the fallback.
static b32 probe_terminal()
{
        positive2 size = term_size();

        string_format(log, "%p %p\n", size.width, size.height);

        return 0;
}

// Through the device rather than through fork and exec: the task is created
// with no address space to copy instead of one copied and thrown away.
static b32 probe_spawn()
{
        b32 device = system_call_4(syscall(openat), AT_FDCWD,
                                   (positive)SPARK_DEVICE, FILE_READ_WRITE, 0);
        b32 count = program_argument_count();

        string_format(log, "device: %b\n", device);

        if (device < 0)
                return 1;

        for (b32 i = 2; i < count; i++)
        {
                string_address path = program_argument(i);
                struct spawn request;
                p8 block[256];
                positive length = string_length(path);
                bipolar child;
                positive status = 0;

                memory_copy(block, path, length + 1);

                request.path = (unsigned long)path;
                request.argv = (unsigned long)block;
                request.argv_bytes = length + 1;
                request.argv_count = 1;
                request.envp = 0;
                request.envp_bytes = 0;
                request.envp_count = 0;
                request.envp_generation = 0;

                log_flush();

                child = system_call_3(syscall(ioctl), device, SPARK_IOCTL_SPAWN,
                                      (positive)address_of request);

                if (child < 0)
                {
                        string_format(log, "  %s: refused %b\n", path, child);
                        continue;
                }

                system_call_4(syscall(wait4), child, (positive)address_of status, 0, 0);
                string_format(log, "  %s: exit %p\n", path, (status >> 8) & 0xff);
        }

        system_call_1(syscall(close), device);
        return 0;
}

b32 main()
{
        string_address mode = program_argument_count() > 1
                                  ? program_argument(1)
                                  : (string_address) "";
        b32 answer;

        if (!string_compare(mode, "arguments"))
                answer = probe_arguments();
        else if (!string_compare(mode, "regions"))
                answer = probe_regions();
        else if (!string_compare(mode, "environment"))
                answer = probe_environment();
        else if (!string_compare(mode, "logging"))
                answer = probe_logging();
        else if (!string_compare(mode, "diagnostic"))
                answer = probe_diagnostic();
        else if (!string_compare(mode, "terminal"))
                answer = probe_terminal();
        else if (!string_compare(mode, "spawn"))
                answer = probe_spawn();
        else if (!string_compare(mode, "quack"))
        {
                string_format(log, "quack\n");
                answer = 0;
        }
        else if (!string_compare(mode, "status"))
                answer = program_argument_count() > 2
                             ? (b32)string_to_positive(program_argument(2))
                             : 0;
        else
        {
                string_format(log, "probe: arguments regions environment "
                                   "logging diagnostic terminal spawn quack status\n");
                answer = 2;
        }

        log_flush();

        return answer;
}
#endif /* CHECK_probe */

#ifdef CHECK_stackguard
#include "../../src/lib.util.c"

/*
        The stack protector's guard, as lib.util.c gives it to a program built
        with -fstack-protector-strong -mstack-protector-guard=global (what
        kernel/profile/sec_build_hardened adds to the image's programs).

        "ok" writes inside a buffer and comes back; "smash" writes past it
        and must not come back, but die of SIGABRT with the line on standard
        error; "guard" says the value the guard has, which main sets from
        getrandom with its low byte zero, so two runs differ and neither is
        the constant the program is linked with.
*/
extern unsigned long __stack_chk_guard;

static __attribute__((noinline)) b32 guard_fill(positive count)
{
        volatile p8 buffer[16];

        for (positive at = 0; at < count; at++)
                buffer[at] = 'A';

        return buffer[0];
}

b32 main()
{
        string_address mode = program_argument_count() > 1
                                  ? program_argument(1)
                                  : (string_address) "";
        b32 answer = 0;

        if (!string_compare(mode, "ok"))
        {
                guard_fill(16);
                string_format(log, "ok\n");
        }
        else if (!string_compare(mode, "smash"))
        {
                guard_fill(64);
                string_format(log, "returned\n");
        }
        else if (!string_compare(mode, "guard"))
                string_format(log, "%p %b\n", (positive)__stack_chk_guard,
                              (b32)(__stack_chk_guard & 0xff));
        else
        {
                string_format(log, "stackguard: ok smash guard\n");
                answer = 2;
        }

        log_flush();

        return answer;
}
#endif /* CHECK_stackguard */

