/*
        test/checks/machine.c: the machine, snapshots, sensors and the browser.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_machine
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define MOONWATER_SCAN
#include "../../src/sh/shell.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        The machine script is scanned once, in moonwater.c, for the kernel overlay.
        These cases are the contract: hooks, per-event functions, case arms,
        canvas) owning both desktop events, *) not owning the rest, first
        arm and first hook keeping the line, recover not being a bind row,
        comments and quotes not inventing functions, and the file policy that
        keeps the one auditable path from being a symlink or a world-writable
        file.
*/

static fn machine_scan(string_address text, struct moonwater_overlay address_to into)
{
        moonwater_scan((const char *)text, string_length(text), into);
}

static fn machine_hooks(void)
{
        struct moonwater_overlay script;

        machine_scan("", address_of script);
        check("an empty file has no hooks", !script.hooks);

        machine_scan("echo moonwater_event\n", address_of script);
        check("a name that is not a function is not a hook", !script.hooks);

        machine_scan("function moonwater_event\necho hi\n", address_of script);
        check("function without a body is not a hook", !script.hooks);

        machine_scan("# function moonwater_event {\nfunction moonwater_init {\n  :\n}\n",
                     address_of script);
        check("a comment does not define a hook",
              script.hooks == MOONWATER_HOOK_INIT && !script.event_line);
        check("moonwater_init is the line of its name", script.init_line == 2);

        machine_scan("echo 'function moonwater_event {'\n", address_of script);
        check("a quoted function line is not a hook", !script.hooks);

        machine_scan("moonwater_event() {\n  :\n}\n", address_of script);
        check("NAME() { is a hook", script.hooks == MOONWATER_HOOK_EVENT);
        check("and the line is the name", script.event_line == 1);

        machine_scan("moonwater_init() ( : )\nmoonwater_end() [[ -n x ]]\n"
                     "moonwater_event() if :; then\n  case $1 in\n  mute) : ;;\n  esac\nfi\n"
                     "moonwater_poweroff() { :; }\n",
                     address_of script);
        check("a subshell, a test and an if are function bodies too",
              script.hooks == (MOONWATER_HOOK_INIT | MOONWATER_HOOK_EVENT |
                               MOONWATER_HOOK_END) &&
                  script.init_line == 1 && script.end_line == 2 &&
                  script.event_line == 3);
        check("a case inside an if body gives its arms, and the end is found",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 5 &&
                  moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 8);
        machine_scan("moonwater_event() ( echo fi; echo ')' )\nmoonwater_end() { :; }\n",
                     address_of script);
        check("a word that closes a compound where one cannot begin closes nothing",
              script.hooks == (MOONWATER_HOOK_EVENT | MOONWATER_HOOK_END));

        machine_scan("function moonwater_init {\n  :\n}\n"
                     "function moonwater_event {\n  :\n}\n"
                     "function moonwater_end {\n  :\n}\n",
                     address_of script);
        check("all three hooks are found",
              script.hooks == (MOONWATER_HOOK_INIT | MOONWATER_HOOK_EVENT |
                               MOONWATER_HOOK_END));
        check("init is line 1", script.init_line == 1);
        check("event is line 4", script.event_line == 4);
        check("end is line 7", script.end_line == 7);
}

static fn machine_arms(void)
{
        struct moonwater_overlay script;

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  mute) echo muted ;;\n"
                     "  canvas) echo canvas ;;\n"
                     "  *) echo other ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("an event function with arms is a hook",
              script.hooks & MOONWATER_HOOK_EVENT);
        check("mute is the mute arm",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 3);
        check("canvas on is the canvas arm",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 4);
        check("canvas off is the same canvas arm",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_OFF) == 4);
        check("poweroff is not owned by star",
              moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 0);
        check("reset is not owned by star",
              moonwater_bind_line(address_of script, SPARK_BIND_RESET) == 0);
        check("star was seen", script.star_line == 5);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  mute|volume_up) : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("mute|volume_up owns mute",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 3);
        check("and volume_up at the same line",
              moonwater_bind_line(address_of script, SPARK_BIND_VOLUME_UP) == 3);
        check("poweroff without an arm is not owned",
              moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 0);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  \"canvas on\") : ;;\n"
                     "  \"canvas off\") : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("quoted canvas on is that event only",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 3);
        check("quoted canvas off is the other",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_OFF) == 4);
        check("mute is not a canvas arm",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 0);

        machine_scan("function moonwater_event {\n  :\n}\n", address_of script);
        check("no case does not own events",
              moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 0 &&
                  moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 0);
        check("no arm is not a bind overlay",
              !script.bind_line[SPARK_BIND_POWEROFF - 1] && !script.star_line);

        machine_scan("function moonwater_init {\n"
                     "  function inner {\n"
                     "    case $1 in\n"
                     "    mute) : ;;\n"
                     "    esac\n"
                     "  }\n"
                     "}\n",
                     address_of script);
        check("a nested function inside init is not moonwater_event",
              script.hooks == MOONWATER_HOOK_INIT &&
                  !moonwater_bind_line(address_of script, SPARK_BIND_MUTE));

        machine_scan("function moonwater_event() {\n"
                     "  case $1 in\n"
                     "  mute) : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("function NAME() { is still a hook",
              script.hooks == MOONWATER_HOOK_EVENT && script.event_line == 1);
        check("and still owns its mute arm",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 3);

        machine_scan("function moonwater_init {\n  :\n}\n"
                     "function moonwater_init {\n  :\n}\n",
                     address_of script);
        check("the first hook definition keeps the line", script.init_line == 1);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  mute) : ;;\n"
                     "  mute) echo second ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("the first mute arm keeps the line",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 3);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  *) : ;;\n"
                     "  mute) : ;;\n"
                     "  canvas) : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("a later mute arm still owns mute after star",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 4);
        check("a later canvas arm still owns both canvas events",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 5 &&
                  moonwater_bind_line(address_of script,
                                             SPARK_BIND_CANVAS_OFF) == 5);
        check("poweroff is still not owned after star",
              moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 0);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  (mute) : ;;\n"
                     "  canvas|volume_up) : ;;\n"
                     "  'reset') : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("a parenthesized mute arm owns mute",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 3);
        check("canvas|volume_up owns canvas on",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 4);
        check("and volume_up at that line",
              moonwater_bind_line(address_of script, SPARK_BIND_VOLUME_UP) == 4);
        check("a quoted reset arm owns reset",
              moonwater_bind_line(address_of script, SPARK_BIND_RESET) == 5);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  canvas on) : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("unquoted canvas on is the canvas word, both events",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 3 &&
                  moonwater_bind_line(address_of script,
                                             SPARK_BIND_CANVAS_OFF) == 3);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  # mute) : ;;\n"
                     "  sleep) : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("a commented mute arm is not mute",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 0);
        check("sleep is the sleep arm",
              moonwater_bind_line(address_of script, SPARK_BIND_SLEEP) == 4);

        machine_scan("function moonwater_event {\n"
                     "  case $1 in\n"
                     "  recover) : ;;\n"
                     "  esac\n"
                     "}\n",
                     address_of script);
        check("a recover arm is not a bind row",
              moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 0);

        machine_scan("function moonwater_mute {\n  :\n}\n"
                     "function moonwater_canvas {\n  :\n}\n"
                     "function moonwater_event {\n  :\n}\n",
                     address_of script);
        check("moonwater_mute owns mute",
              moonwater_bind_line(address_of script, SPARK_BIND_MUTE) == 1);
        check("moonwater_canvas owns canvas on",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 4);
        check("and canvas off at the same line",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_OFF) == 4);
        check("moonwater_event without arms owns nothing else",
              moonwater_bind_line(address_of script, SPARK_BIND_POWEROFF) == 0 &&
                  (script.hooks & MOONWATER_HOOK_EVENT));

        machine_scan("function moonwater_canvas_on {\n  :\n}\n",
                     address_of script);
        check("moonwater_canvas_on owns only canvas on",
              moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_ON) == 1 &&
                  !moonwater_bind_line(address_of script, SPARK_BIND_CANVAS_OFF));

        machine_scan("function moonwater_tablet {\n  :\n}\n"
                     "function moonwater_resume {\n  :\n}\n",
                     address_of script);
        check("moonwater_tablet owns both tablet events",
              moonwater_bind_line(address_of script, SPARK_BIND_TABLET_ON) == 1 &&
                  moonwater_bind_line(address_of script, SPARK_BIND_TABLET_OFF) == 1);
        check("moonwater_resume owns resume",
              moonwater_bind_line(address_of script, SPARK_BIND_RESUME) == 4);
}

static fn machine_policy(void)
{
        {
                struct moonwater_overlay empty;

                memory_zero(address_of empty, sizeof(empty));
                check("an event without moonwater_event is not owned",
                      moonwater_bind_line(address_of empty,
                                                 SPARK_BIND_POWEROFF) == 0);
        }

        check("the machine ioctl request is 64 bytes",
              sizeof(struct machine_control) == 64);
        check("the machine script ioctl request is 80 bytes",
              sizeof(struct machine_script) == 80);
        check("the overlay inside it is 56 bytes",
              sizeof(struct moonwater_overlay) == 56);
        check("the overlay sits at byte 24 of the script request",
              __builtin_offsetof(struct machine_script, overlay) == 24);
        check("GET and SET are distinct script ops",
              MOONWATER_SCRIPT_GET != MOONWATER_SCRIPT_SET);
        check("builtin origin is not disk",
              MOONWATER_ORIGIN_BUILTIN != MOONWATER_ORIGIN_DISK);
        check("there are twenty-two bind events", SPARK_BIND_EVENTS == 22);
        check("the last is resume, and no stop",
              !spark_bind_is_stop(SPARK_BIND_RESUME) &&
                  string_equals((string_address)spark_bind_event_name[SPARK_BIND_RESUME - 1],
                                "resume"));
        check("poweroff, reset and ctrl_alt_delete stop the machine",
              spark_bind_is_stop(SPARK_BIND_POWEROFF) &&
                  spark_bind_is_stop(SPARK_BIND_RESET) &&
                  spark_bind_is_stop(SPARK_BIND_CTRL_ALT_DELETE));
        check("mute and canvas do not stop the machine",
              !spark_bind_is_stop(SPARK_BIND_MUTE) &&
                  !spark_bind_is_stop(SPARK_BIND_CANVAS_ON) &&
                  !spark_bind_is_stop(SPARK_BIND_CANVAS_OFF));
        check("event 0 is the orderly end, not a bind",
              !spark_bind_is_stop(0));

        {
                unsigned int event;
                p8 named_recover = false;
                p8 named_canvas_on = false;
                p8 named_canvas_off = false;
                p8 named_micmute = false;
                p8 named_resume = false;

                for (event = 0; event < SPARK_BIND_EVENTS; event++)
                {
                        if (string_equals((string_address)spark_bind_event_name[event],
                                          "recover"))
                                named_recover = true;
                        if (string_equals((string_address)spark_bind_event_name[event],
                                          "canvas on"))
                                named_canvas_on = true;
                        if (string_equals((string_address)spark_bind_event_name[event],
                                          "canvas off"))
                                named_canvas_off = true;
                        if (string_equals((string_address)spark_bind_event_name[event],
                                          "micmute"))
                                named_micmute = true;
                        if (string_equals((string_address)spark_bind_event_name[event],
                                          "resume"))
                                named_resume = true;
                }

                check("recover is not a bind name", !named_recover);
                check("canvas on is a bind name", named_canvas_on);
                check("canvas off is a bind name", named_canvas_off);
                check("micmute is a bind name", named_micmute);
                check("resume is a bind name", named_resume);
        }
}

/*
        The machine process stopping the machine it is the machine process of.

        A rule's poweroff the runner does stops the machine from inside the
        machine process. That goes through host_machine_stop like every other
        stop does, and the attach it would ask the kernel to end is its own:
        it must not poll for a detach only it could perform.
*/
/*
        The runner's pin lines (rover_runner_pin_number): the taskbar's number is nine digits from 156 s of uptime on,
        and was cut to seven, so every pin after that named a file that was not there.
*/
static fn machine_pin_numbers(void)
{
        p8 digits[ROVER_PIN_DIGITS + 1];

        check("a pin's seven-digit number is taken whole",
              rover_runner_pin_number((const p8 address_to)"pin_start_1234567", 17, digits) &&
                  string_equals(digits, "1234567"));
        check("and a nine-digit one, past 156 s of uptime, whole too",
              rover_runner_pin_number((const p8 address_to)"pin_start_100000064", 19, digits) &&
                  string_equals(digits, "100000064"));
        check("and nineteen digits", rover_runner_pin_number((const p8 address_to)"pin_start_1234567890123456789", 29, digits) &&
                                         string_equals(digits, "1234567890123456789"));
        check("twenty are refused, not cut",
              !rover_runner_pin_number((const p8 address_to)"pin_start_12345678901234567890", 30, digits));
        check("a number with anything but digits is refused",
              !rover_runner_pin_number((const p8 address_to)"pin_start_12/../x", 17, digits) &&
                  !rover_runner_pin_number((const p8 address_to)"pin_start_", 10, digits));
}

/*
        A run's script (rover_runner_script): the directory and the file each root's alone, looked at each run. As root
        the rows hold every case; as another user every directory is not root's, so a present one is refused.
*/
static fn machine_run_scripts(void)
{
        static const char address_to dir = "/tmp/rover-scripts-check";
        p8 path[160];
        bool root = (bipolar)system_call_1(syscall(getuid), 0) == 0;
        bipolar handle;

        check("a script directory that is not there is no script",
              rover_runner_script("/tmp/rover-scripts-check-none", true, "a", path, sizeof(path)) == 0);
        system_call_3(syscall(mkdirat), AT_FDCWD, (positive)dir, 0755);
        system_call_4(syscall(fchmodat), AT_FDCWD, (positive)dir, 0755, 0);
        handle = system_open_output_at(AT_FDCWD, "/tmp/rover-scripts-check/a", true, 0644);
        if (handle >= 0)
                system_close(handle);
        system_call_4(syscall(fchmodat), AT_FDCWD, (positive) "/tmp/rover-scripts-check/a", 0644, 0);
        check("a person's script directory others may read is refused, not passed to the image's",
              rover_runner_script(dir, true, "a", path, sizeof(path)) == -1);
        if (root)
        {
                check("the image's directory, root's and written by root alone, gives its script",
                      rover_runner_script(dir, false, "a", path, sizeof(path)) == 1 &&
                          string_equals(path, "/tmp/rover-scripts-check/a"));
                system_call_4(syscall(fchmodat), AT_FDCWD, (positive)dir, 0700, 0);
                check("and a person's 0700 one", rover_runner_script(dir, true, "a", path, sizeof(path)) == 1);
                check("a name the directory has not is no script", rover_runner_script(dir, true, "b", path, sizeof(path)) == 0);
                system_call_4(syscall(fchmodat), AT_FDCWD, (positive) "/tmp/rover-scripts-check/a", 0666, 0);
                check("a script others may write is refused", rover_runner_script(dir, true, "a", path, sizeof(path)) == -1);
                system_call_4(syscall(fchmodat), AT_FDCWD, (positive)dir, 0777, 0);
                system_call_4(syscall(fchmodat), AT_FDCWD, (positive) "/tmp/rover-scripts-check/a", 0644, 0);
                check("a directory others may write is refused, the image's too",
                      rover_runner_script(dir, false, "a", path, sizeof(path)) == -1);
        }
        else
                check("as another user an image's directory not root's is refused",
                      rover_runner_script(dir, false, "a", path, sizeof(path)) == -1);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive) "/tmp/rover-scripts-check/a", 0);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)dir, 0x200);
}

/*
        The runner's children (rover_runner_child): a pid is never dropped. A hundred children that end at once are all
        reaped, where past 64 they were left as zombies for good.
*/
static fn machine_runner_children(void)
{
        positive left = 0;

        for (positive at = 0; at < 100; at++)
        {
                bipolar pid = system_fork();

                if (pid == 0)
                        system_call_1(syscall(exit), 0);
                if (pid > 0)
                        rover_runner_child(pid);
        }
        for (positive tries = 0; tries < 200; tries++)
        {
                timespec pause = {0, 10000000};

                rover_runner_reap();
                left = 0;
                for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                        left += rover_runner.children[at] != 0;
                if (!left)
                        break;
                system_call_2(syscall(nanosleep), (positive)&pause, 0);
        }
        //      Each kept child carries its program's name, for the line its failure is said in (E-h10-5).
        {
                bipolar pid = system_fork();
                bool named = false;

                if (pid == 0)
                        system_call_1(syscall(exit), 3);
                string_copy_max(rover_runner_naming, (string_address) "failing-rule-program", sizeof(rover_runner_naming));
                rover_runner_on_desktop = true;
                rover_runner_child(pid);
                for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                        named = named || (rover_runner.children[at] == pid &&
                                          string_equals((string_address)rover_runner.child_name[at], "failing-rule-program"));
                check("a child the runner keeps carries its program's name", named);
                {
                        bool desktop = false;

                        for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                                desktop = desktop || (rover_runner.children[at] == pid && rover_runner.child_desktop[at]);
                        check("and a program opened on the desktop is marked to end with the compositor, the mark taken once",
                              desktop && !rover_runner_on_desktop);
                }
                check("with no compositor's service running the desktop is not up, whatever file is there",
                      !rover_runner_desktop_up());
                //      An open asked while a desktop that was up is down is dropped, not held and replayed when a
                //      compositor starts again (three Launchers at once on box); one held for a program is held once.
                {
                        struct rover_value value = {0, (const p8 address_to)"/terminal", 9, 0};

                        rover_runner.helds = 0;
                        rover_runner.desktop_was_up = false;
                        rover_runner_primitive(14, &value, 1);
                        rover_runner_primitive(14, &value, 1);
                        check("before the first desktop an open is held, and the same program held once", rover_runner.helds == 1);
                        rover_runner.desktop_was_up = true;
                        rover_runner.helds = 0;
                        rover_runner_primitive(14, &value, 1);
                        check("after a desktop was up, an open while it is down is dropped", rover_runner.helds == 0);
                        rover_runner.desktop_was_up = false;
                }
                (void)system_wait4_retry(pid, null, 0, null);
                for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                        if (rover_runner.children[at] == pid)
                                rover_runner.children[at] = 0;
        }
        check("a hundred children of the runner are all reaped, none dropped and left a zombie",
              !left && system_wait4_retry(-1, null, 1, null) < 0);
}

/*
        The loader's set (rover_load_add): a file whose rule names clash with the files before it is left out by itself,
        and the rest load, where one clash dropped every person's rule (R-compiler-8).
*/
static fn machine_load_clash(void)
{
        static const char first[] = "rule power\n\ton key power\n\tdo log one\n";
        static const char again[] = "rule power\n\ton lid close\n\tdo log two\n";
        static const char other[] = "rule lid\n\ton lid close\n\tdo log three\n";

        rover_load_used = 0;
        check("a file of rules goes into the set", rover_load_add((const p8 address_to)first, sizeof(first) - 1, "a", false));
        check("a file with a rule of a name the set has is left out by itself",
              !rover_load_add((const p8 address_to)again, sizeof(again) - 1, "b", false) &&
                  rover_load_used == sizeof(first));
        check("and the next file still goes in", rover_load_add((const p8 address_to)other, sizeof(other) - 1, "c", false));
        rover_load_all.merge = true;
        check("and the set compiles, with both rules",
              rover_compile(&rover_load_all, rover_load_set, rover_load_used) && rover_load_all.rules == 2);
        rover_load_all.merge = false;
        rover_load_used = 0;
}

//      The runner's budget (rover_runner_within): 64 firings of a rule in a second, the 65th waits (R-seams-4).
static fn machine_runner_budget(void)
{
        bool all = true;

        for (positive at = 0; at < 64; at++)
                all = all && rover_runner_within(7);
        check("a rule fires 64 times in a second", all);
        check("and the 65th waits for the next second", !rover_runner_within(7));
        check("another rule has its own budget", rover_runner_within(8));
}

//      The compositor's lines (rover_runner_line): a chord's name is the chord event, a button's word the button event.
static fn machine_runner_lines(void)
{
        static const p8 chord_line[] = "key_ctrl_shift_20";
        static const p8 volume_line[] = "volume_button";
        static const p8 unknown_line[] = "key_nothing_here";

        rover_runner.queued = 0;
        rover_runner.key_count = 1;
        string_copy(rover_runner.keys[0].name, "key_ctrl_shift_20");
        rover_runner.keys[0].chord = 0x50014;
        rover_runner_line(chord_line, sizeof(chord_line) - 1);
        check("a chord's line is the chord event with its chord",
              rover_runner.queued == 1 && rover_runner.queue[0].id == 64 && rover_runner.queue[0].field[0].number == 0x50014);
        rover_runner_line(volume_line, sizeof(volume_line) - 1);
        check("a button's word is the button event, the volume cell's number",
              rover_runner.queued == 2 && rover_runner.queue[1].id == 71 && rover_runner.queue[1].field[0].number == 1);
        rover_runner_line(unknown_line, sizeof(unknown_line) - 1);
        check("a line that is no chord or button the rules have is nothing", rover_runner.queued == 2);
        rover_runner.queued = 0;
        rover_runner.key_count = 0;
}

static fn machine_stop_self(void)
{
        p8 held = host_machine_self;

        host_machine_ended = false;
        host_machine_self = true;

        check("the machine process does not wait on the attach it holds itself",
              host_machine_stop());
        check("and that stop is the machine's end", host_machine_ended);
        check("and a second is a stop as well",
              host_machine_stop() && host_machine_ended);

        host_machine_self = held;
        host_machine_ended = false;
}

/*
        SNTP's originate nonce asks only for blocking getrandom. A seccomp
        errno filter can refuse that ask without replacing the production
        helper: the child must see sntp_put_stamp fail and sntp_exchange
        return before any send, the way the old wall-clock fallback would
        have kept going.
*/
#define SHARED_random_filter
#include "../checks.c"
#undef SHARED_random_filter

static fn sntp_test_random_child(positive which)
{
        p8 field[8];
        network_deadline deadline = {0};
        p32 sequence = 0;
        sntp_sample sample = {0};
        p8 ok;

        memory_fill(field, 0xa5, sizeof field);
        if (random_filter(which, which) < 0)
                system_call_1(syscall(exit_group), 77);
        if (which)
                ok = sntp_put_stamp(field);
        else
        {
                ok = !sntp_put_stamp(field) &&
                     sntp_exchange(0, address_of deadline, false,
                                   address_of sequence, null,
                                   address_of sample) == SNTP_NO_REPLY &&
                     !sample.ok;
                for (positive at = 0; at < sizeof field; at++)
                        ok &= field[at] == 0xa5;
        }
        system_call_1(syscall(exit_group), ok ? 0 : 1);
}

/*
        The schedule after a query, against the kernel forgetting a clock
        it was told was synchronised -- which a clocksource change does,
        and x86 makes one 1.5 s into every boot.
*/
static fn machine_ntp_schedule(void)
{
        p64 second = 1000000000ull;
        p64 now = 1000 * second;
        p64 next = locale_ntp_next;
        p64 synced = locale_ntp_synced;
        positive retry = locale_ntp_retry;
        positive every = locale_ntp_every;

        locale_ntp_next = 0;
        locale_ntp_synced = 0;
        locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
        locale_ntp_every = LOCALE_NTP_AGAIN_FIRST;
        locale_ntp_schedule(0, false, now);
        check("an answer the kernel has already forgotten is asked again in a second",
              locale_ntp_next == now + second && !locale_ntp_synced);
        locale_ntp_schedule(0, true, now + 2 * second);
        check("an answer the kernel keeps waits for the first poll",
              locale_ntp_next == now + 2 * second + LOCALE_NTP_AGAIN_FIRST * second &&
                  locale_ntp_synced == now + 2 * second);
        locale_ntp_schedule(LOCALE_CHILD_IDLE, true, now + 3 * second);
        check("a clock still synchronised keeps that poll",
              locale_ntp_next == now + 2 * second + LOCALE_NTP_AGAIN_FIRST * second);
        locale_ntp_schedule(LOCALE_CHILD_IDLE, false, now + 3 * second);
        check("one that loses it between polls is asked again in a second",
              locale_ntp_next == now + 4 * second);
        locale_ntp_schedule(LOCALE_NTP_EXIT_RATE, false, now + 5 * second);
        locale_ntp_schedule(LOCALE_CHILD_IDLE, false, now + 6 * second);
        check("but never over a RATE answer's wait",
              locale_ntp_next == now + 5 * second + LOCALE_NTP_RATE_AGAIN * second);

        //      What a machine that gets no answer asks for in an hour, in
        //      its fourth, by what the failure was: unreachable doubles to a
        //      quarter of an hour, a clock the kernel will not set or a
        //      server that said DENY is half an hour, RATE is five minutes.
        //      It was every eight seconds for ever.
        {
                static const bipolar ended[3] = {1, LOCALE_NTP_EXIT_LONG,
                                                 LOCALE_NTP_EXIT_RATE};
                positive hour[3];

                for (positive at = 0; at < 3; at++)
                {
                        p64 when = now;

                        hour[at] = 0;
                        locale_ntp_next = 0;
                        locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
                        while (when < now + 4 * 3600 * second)
                        {
                                locale_ntp_schedule(ended[at], false, when);
                                when = locale_ntp_next;
                                hour[at] += when >= now + 3 * 3600 * second &&
                                            when < now + 4 * 3600 * second;
                        }
                }
                check("a machine with no answer asks at most five times an hour",
                      hour[0] && hour[0] <= 5);
                check("one refused or denied asks at most twice an hour",
                      hour[1] && hour[1] <= 2);
                check("and one rate limited at most a dozen times",
                      hour[2] && hour[2] <= 13);
        }

        locale_ntp_next = next;
        locale_ntp_synced = synced;
        locale_ntp_retry = retry;
        locale_ntp_every = every;
}

/* UDP does not preserve a too-small receive buffer as evidence of the wire
   length unless the caller leaves room to observe the first trailing byte.
   Exercise the same recvmsg helper as the client so this cannot regress into
   accepting a 48-byte prefix of an oversized datagram. */
static fn sntp_datagram_framing(void)
{
        b32 pair[2] = {-1, -1};
        p8 sent[SNTP_REPLY_ROOM];
        p8 received[SNTP_REPLY_ROOM];
        positive control[SNTP_CONTROL_WORDS];
        positive held = 0;
        bipolar got;

        memory_fill(sent, 0xa5, sizeof sent);
        check("SNTP datagram framing socket opens",
              system_call_4(syscall(socketpair), AF_UNIX, SOCK_DGRAM, 0,
                            (positive)pair) == 0);
        if (pair[0] < 0 || pair[1] < 0)
                return;

        check("SNTP exact reply datagram sends",
              socket_send(pair[0], sent, SNTP_PACKET, 0, 0, 0) == SNTP_PACKET);
        got = sntp_receive_message(pair[1], received, sizeof received, control,
                                   address_of held, 0);
        check("SNTP exact reply datagram is the one shape taken",
              got == SNTP_PACKET);

        check("SNTP oversized reply datagram sends",
              socket_send(pair[0], sent, sizeof sent, 0, 0, 0) == sizeof sent);
        got = sntp_receive_message(pair[1], received, sizeof received, control,
                                   address_of held, 0);
        check("SNTP trailing wire byte is refused",
              got == SNTP_REPLY_ROOM && got != SNTP_PACKET);

        socket_close(pair[0]);
        socket_close(pair[1]);
}

/*
        The query the machine forks and what becomes of it: every answer
        byte read once and the child waited for, so that none is left a
        zombie (the wait did not block, and found the child still running
        in nearly every case); nothing of the parent open in it, because its
        descriptor on /dev/spark kept the machine attached; and `ntp off`
        ending one in flight.
*/
static fn machine_ntp_child(void)
{
        positive status = 0;
        positive left = 0;
        positive wrong = 0;
        bipolar held = system_open_at(AT_FDCWD, "/dev/null", O_RDONLY);
        locale_child child = {0, -1};

        for (positive at = 0; at < 1000; at++)
        {
                bipolar ended;

                if (!locale_child_fork(address_of child))
                {
                        //      The third of these ends without its byte.
                        if (at % 3 == 2)
                                system_call_1(syscall(exit), 0);
                        locale_child_end(address_of child, (p8)(at % 3));
                }
                while ((ended = locale_child_poll(address_of child)) ==
                       LOCALE_CHILD_RUNNING)
                        ;
                wrong += ended != (at % 3 == 2 ? 1 : (bipolar)(at % 3));
                left += system_call_4(syscall(wait4), (positive)-1,
                                      (positive)address_of status, 1, 0) !=
                        -ECHILD;
        }
        check("every query that ended is waited for, so none is left to be a zombie",
              !left);
        check("and says what it ended with, or 1 when it died without saying",
              !wrong);

        if (held >= 0)
        {
                bipolar ended;

                if (!locale_child_fork(address_of child))
                        locale_child_end(address_of child,
                                         system_call_3(syscall(fcntl),
                                                       (positive)held, 1, 0) < 0
                                             ? 0
                                             : 5);
                while ((ended = locale_child_poll(address_of child)) ==
                       LOCALE_CHILD_RUNNING)
                        ;
                check("a query is forked with nothing of the machine open in it",
                      ended == 0);
                system_close(held);
        }

        //      A query in flight when NTP is turned off.
        locale_ntp_on = false;
        if (!locale_child_fork(address_of locale_ntp_child))
        {
                for (;;)
                        system_call_1(syscall(getppid), 0);
        }
        check("`ntp off` with a query in flight ends it and waits for it",
              locale_ntp_child.pid > 0);
        locale_ntp_keep(true, false, system_clock_ns(HOST_CLOCK_BOOTTIME));
        check("so no pid is kept, no pipe, and no child is left",
              locale_ntp_child.pid == 0 && locale_ntp_child.answer < 0 &&
                  system_call_4(syscall(wait4), (positive)-1,
                                (positive)address_of status, 1, 0) == -ECHILD);
        check("and the machine sleeps its whole wake again",
              locale_wake_ms(3000) == 3000);
}

static p8 machine_task_none(address_any context)
{
        (void)context;
        return 0;
}

/*
        A machine that cannot make a process (out of them, out of memory)
        waits that out as it does a query that failed, instead of trying
        again at the next look, four times a second. The soft limit on
        processes is set to none and a fork tried; a caller that is not held
        by it (root) has nothing to prove here.
*/
static fn machine_ntp_no_child(void)
{
        positive held[2];
        positive none[2];
        positive status = 0;
        bipolar probe;
        locale_child child = {0, -1};
        p64 next = locale_ntp_next;
        positive retry = locale_ntp_retry;
        p8 on = locale_ntp_on;
        p64 later = locale_auto_next;
        positive wait = locale_auto_wait;
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);

        if (system_call_4(syscall(prlimit64), 0, 6, 0, (positive)held) < 0)
                return;
        none[0] = 0;
        none[1] = held[1];
        if (system_call_4(syscall(prlimit64), 0, 6, (positive)none, 0) < 0)
                return;
        probe = system_fork();
        if (!probe)
                system_call_1(syscall(exit), 0);
        if (probe > 0)
        {
                (void)system_wait4_retry(probe, address_of status, 0, null);
                (void)system_call_4(syscall(prlimit64), 0, 6, (positive)held, 0);
                log_direct(str("machine: no-child assertion NOT RUN -- the process limit does not hold this user\n"));
                return;
        }

        locale_ntp_on = true;
        locale_ntp_next = 0;
        locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
        check("a fork that fails makes no query to wait for",
              locale_child_run(address_of child, machine_task_none, null) == false &&
                  child.pid <= 0);
        locale_ntp_keep(true, false, system_clock_ns(HOST_CLOCK_BOOTTIME));
        check("a machine that cannot make a child waits that out as a query that failed",
              locale_ntp_next > now && locale_ntp_next - now <=
                  (p64)LOCALE_NTP_RETRY_LEAST * 1000000000ull + 1000000000ull &&
                  locale_ntp_retry == 2 * LOCALE_NTP_RETRY_LEAST &&
                  locale_ntp_child.pid <= 0);
        {
                p64 due = locale_ntp_next;

                locale_ntp_keep(true, false, system_clock_ns(HOST_CLOCK_BOOTTIME));
                check("and does not try again before it is due",
                      locale_ntp_next == due &&
                          locale_ntp_retry == 2 * LOCALE_NTP_RETRY_LEAST);
        }

        //      A machine whose zone was typed does not ask at all.
        if (!locale_zone_manual())
        {
                locale_auto_next = 0;
                locale_auto_wait = LOCALE_AUTO_LEAST;
                locale_auto_keep(false, true, system_clock_ns(HOST_CLOCK_BOOTTIME),
                                 "lo 0100007F");
                check("the zone's question is waited out the same way, and longer each time",
                      locale_auto_next > now &&
                          locale_auto_wait == 2 * LOCALE_AUTO_LEAST &&
                          locale_auto_child.pid <= 0);
        }

        (void)system_call_4(syscall(prlimit64), 0, 6, (positive)held, 0);
        locale_ntp_on = on;
        locale_ntp_next = next;
        locale_ntp_retry = retry;
        locale_auto_next = later;
        locale_auto_wait = wait;
}

/*
        A server that answers under two names is one server. The walk asks
        the next name when the one it has is an address it has already heard
        from, so that three names of one host are not three servers agreeing.
*/
static fn machine_ntp_heard(void)
{
        sntp_sample row[3] = {{0}};

        row[0].address = 0x0a000001;
        row[1].address = 0x0a000002;
        check("nothing is heard from before the first answer",
              !locale_ntp_heard(row, 0, 0x0a000001));
        check("an address that answered is heard from",
              locale_ntp_heard(row, 1, 0x0a000001));
        check("another is not",
              !locale_ntp_heard(row, 1, 0x0a000002) &&
                  locale_ntp_heard(row, 2, 0x0a000002));
        check("and only the answers counted are looked at",
              !locale_ntp_heard(row, 1, 0x0a000002) &&
                  !locale_ntp_heard(row, 2, 0x0a000003));
}

#define SNTP_TEST_NOW \
        ((bipolar)1800000000 * (bipolar)SNTP_NANOSECONDS)
//      2035-12-31, 2036-01-01 (where the window used to end), 2036-02-07
//      06:28:16 (the wrap), 2040-01-01 and 2100-01-01, in nanoseconds.
#define SNTP_TEST_DATE_1 ((bipolar)2082672000 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_TEST_DATE_2 ((bipolar)2082758400 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_TEST_DATE_3 ((bipolar)2085978496 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_TEST_DATE_4 ((bipolar)2208988800 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_TEST_DATE_5 ((bipolar)4102444800 * (bipolar)SNTP_NANOSECONDS)

//      A control message laid out by hand for sntp_math_ok: the header,
//      then a timespec where the payload starts.
static COLD fn sntp_test_message(p8 address_to at, positive size, b32 level,
                                 b32 type, p64 seconds, p64 nanoseconds)
{
        address_to(positive address_to)at = size;
        address_to(b32 address_to)(at + sizeof(positive)) = level;
        address_to(b32 address_to)(at + sizeof(positive) + 4) = type;
        address_to(p64 address_to)(at + SNTP_CONTROL_DATA) = seconds;
        address_to(p64 address_to)(at + SNTP_CONTROL_DATA + sizeof(p64)) =
            nanoseconds;
}

//      A stamp, as the wire has it, for any time the window holds: the cast to
//      32 bits is the era, as it is on the wire.
static COLD fn sntp_test_stamp(p8 address_to at, bipolar ns)
{
        network_store_32(at, (p32)(ns / (bipolar)SNTP_NANOSECONDS + SNTP_UNIX));
        network_store_32(at + 4,
                         (p32)((p64)(ns % (bipolar)SNTP_NANOSECONDS) *
                               4294967296ull / SNTP_NANOSECONDS));
}

static COLD p8 sntp_math_ok(void)
{
        bipolar offset = 0;
        bipolar delay = 0;
        bipolar target = 0;
        bipolar two_days = (bipolar)2 * 86400 * (bipolar)SNTP_NANOSECONDS;
        positive at;
        p8 request[SNTP_PACKET];
        p8 reply[SNTP_PACKET];
        positive words[12];
        p8 address_to control = (p8 address_to)words;
        p64 arrived[2];
        p32 sequence;
        static const struct
        {
                positive claimed; /* what the message says its length is */
                b32 level;
                b32 kind;
                positive held;    /* what the buffer actually holds */
                p8 want;
        } control_case[] = {
            /* the message the kernel really sends */
            {SNTP_CONTROL_DATA + 16, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, true},
            /* some other control message, of which there are many */
            {SNTP_CONTROL_DATA + 16, SOL_SOCKET, SNTP_TIMESTAMPNS + 1,
             SNTP_CONTROL_DATA + 16, false},
            {SNTP_CONTROL_DATA + 16, 0, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, false},
            /* a length word smaller than the header it heads */
            {SNTP_CONTROL_DATA - 8, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, false},
            /* a length word reaching past the end of the buffer */
            {SNTP_CONTROL_DATA + 64, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, false},
            /* the right message with too little room for a timespec */
            {SNTP_CONTROL_DATA + 8, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 8, false},
            /* nothing at all, which is what a kernel without the option
               sends, and the case the caller falls back on */
            {SNTP_CONTROL_DATA + 16, SOL_SOCKET, SNTP_TIMESTAMPNS, 0, false},
        };
        static const struct
        {
                p8 first;
                p8 stratum;
                p32 root_delay;
                p32 root_dispersion;
                p32 reference_id;
                p8 echoed;
                bipolar want;
        } reply_case[] = {
            /* a stratum 2 server answering the question we asked */
            {0x24, 2, 0, 0, 0, true, SNTP_OK},
            /* the same packet with the origin stamp not echoed: a forgery,
               and the one check standing between us and an off-path lie */
            {0x24, 2, 0, 0, 0, false, SNTP_NO_REPLY},
            /* mode 3 is a request, not a reply */
            {0x23, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* version 0 and version 7 are no version NTP has had */
            {0x04, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            {0x3c, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* stratum 0 carries a kiss code in the reference id */
            {0x24, 0, 0, 0, SNTP_KISS_RATE, true, SNTP_RATE_LIMITED},
            {0x24, 0, 0, 0, SNTP_KISS_DENY, true, SNTP_DENIED},
            {0x24, 0, 0, 0, SNTP_KISS_RSTR, true, SNTP_DENIED},
            {0x24, 0, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* and a kiss that is none of the three tells us nothing */
            {0x24, 0, 0, 0, 0x58595a5au, true, SNTP_BAD_SERVER},
            /* RATE is still RATE when the alarm bit is set with it */
            {0xe4, 0, 0, 0, SNTP_KISS_RATE, true, SNTP_RATE_LIMITED},
            /* stratum 16 is unsynchronised, and the alarm says so too */
            {0x24, 16, 0, 0, 0, true, SNTP_BAD_SERVER},
            {0xe4, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* a second of root delay is the most we trust, and the sign
               bit of the fixed-point short is never legitimately set */
            {0x24, 2, SNTP_SHORT_SECOND, SNTP_SHORT_SECOND, 0, true, SNTP_OK},
            {0x24, 2, SNTP_SHORT_SECOND + 1, 0, 0, true, SNTP_BAD_SERVER},
            {0x24, 2, 0, SNTP_SHORT_SECOND + 1, 0, true, SNTP_BAD_SERVER},
            {0x24, 2, 0x80000000u, 0, 0, true, SNTP_BAD_SERVER},
            {0x24, 2, 0, 0x80000000u, 0, true, SNTP_BAD_SERVER},
        };
        sntp_sample row[5] = {
            {10000000, 20000000, 0, true}, {8000000, 80000000, 0, true},
            {2000000, 15000000, 0, true},  {4000000, 40000000, 0, true},
            {50000000, 200000000, 0, true},
        };
        static const bipolar delay_case[][6] = {
            {0, 1000000000, 1000000000, 2000000000, 0, 2000000000},
            {0, 1050000000, 1050000000, 2000000000, 50000000, 2000000000},
            {0, 100000000, 100000000, 1100000000, -450000000, 1100000000},
        };
        static const struct
        {
                p32 seconds;
                p32 fraction;
                bipolar want;
        } stamp_case[] = {
            /* era 0, the high bit set: 1968 through February 2036 */
            {SNTP_UNIX, 0, 0},
            {SNTP_UNIX + 1, 0, (bipolar)SNTP_NANOSECONDS},
            {SNTP_UNIX, 0x80000000u, 500000000},
            /* era 1, the high bit clear: February 2036 onward */
            {0, 0, (bipolar)2085978496 * (bipolar)SNTP_NANOSECONDS},
            {1, 0, (bipolar)2085978497 * (bipolar)SNTP_NANOSECONDS},
            /* 2040-01-01, and the last second there is a stamp for */
            {123010304u, 0, (bipolar)2208988800 * (bipolar)SNTP_NANOSECONDS},
            {0x7fffffffu, 0, (bipolar)4233462143ll * (bipolar)SNTP_NANOSECONDS},
        };
        static const struct
        {
                bipolar t1;
                bipolar t2;
                bipolar t3;
                bipolar t4;
                bipolar off;
                bipolar del;
                bipolar most;
                p8 want;
        } sane_case[] = {
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, true},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 2000000000, 0,
             2000000000, SNTP_OFFSET_SYNCED_NS, true},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 3000000001, 0,
             3000000001, SNTP_OFFSET_MOST_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 500000000, 0,
             -500000000, SNTP_OFFSET_MOST_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 3000000000,
             SNTP_TEST_NOW + 3000000000, SNTP_TEST_NOW + 50000000, 2975000000,
             50000000, SNTP_OFFSET_SYNCED_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 2000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 50000000, 0, 50000000,
             SNTP_OFFSET_MOST_NS, false},
            /* a clock that is 2.5 s out is stepped by an unsynchronised
               clock and by a hand, and left to a synchronised one */
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 2500000000,
             SNTP_TEST_NOW + 2500000000, SNTP_TEST_NOW + 2000000, 2499000000,
             2000000, SNTP_OFFSET_SYNCED_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 2500000000,
             SNTP_TEST_NOW + 2500000000, SNTP_TEST_NOW + 2000000, 2499000000,
             2000000, SNTP_OFFSET_MOST_NS, true},
            /* a clock set from the network on either side of 2036-01-01, of
               the era's wrap on 2036-02-07 and in 2040 and 2100 is stepped
               like any other: the window ended on the first of those */
            {SNTP_TEST_DATE_1, SNTP_TEST_DATE_1 + 1000000000,
             SNTP_TEST_DATE_1 + 1000000000, SNTP_TEST_DATE_1 + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, true},
            {SNTP_TEST_DATE_2, SNTP_TEST_DATE_2 + 1000000000,
             SNTP_TEST_DATE_2 + 1000000000, SNTP_TEST_DATE_2 + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, true},
            {SNTP_TEST_DATE_3, SNTP_TEST_DATE_3 + 1000000000,
             SNTP_TEST_DATE_3 + 1000000000, SNTP_TEST_DATE_3 + 2000000000, 0,
             2000000000, SNTP_OFFSET_SYNCED_NS, true},
            {SNTP_TEST_DATE_4, SNTP_TEST_DATE_4 + 1000000000,
             SNTP_TEST_DATE_4 + 1000000000, SNTP_TEST_DATE_4 + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, true},
            {SNTP_TEST_DATE_5, SNTP_TEST_DATE_5 + 1000000000,
             SNTP_TEST_DATE_5 + 1000000000, SNTP_TEST_DATE_5 + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, true},
            /* a server that answers with a time past the window's end is
               refused, and so is one that says it is 1970 */
            {SNTP_TEST_DATE_5, SNTP_WALL_MOST_NS + 1000000000,
             SNTP_WALL_MOST_NS + 1000000000, SNTP_TEST_DATE_5 + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, false},
            /* and one 25 h out only by a hand, which the window bounds */
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 90000000000000,
             SNTP_TEST_NOW + 90000000000000, SNTP_TEST_NOW + 2000000,
             89999999000000, 2000000, SNTP_OFFSET_MOST_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 90000000000000,
             SNTP_TEST_NOW + 90000000000000, SNTP_TEST_NOW + 2000000,
             89999999000000, 2000000, SNTP_OFFSET_SYNCED_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 90000000000000,
             SNTP_TEST_NOW + 90000000000000, SNTP_TEST_NOW + 2000000,
             89999999000000, 2000000, SNTP_OFFSET_ANY_NS, true},
        };

        for (at = 0; at < array_count(delay_case); at++)
        {
                sntp_offset_delay(delay_case[at][0], delay_case[at][1],
                                  delay_case[at][2], delay_case[at][3],
                                  address_of offset, address_of delay);
                if (offset != delay_case[at][4] || delay != delay_case[at][5])
                        return false;
        }

        for (at = 0; at < array_count(sane_case); at++)
                if (sntp_sample_sane(sane_case[at].t1, sane_case[at].t2,
                                     sane_case[at].t3, sane_case[at].t4,
                                     sane_case[at].off, sane_case[at].del,
                                     sane_case[at].most) != sane_case[at].want)
                        return false;

        sntp_offset_delay(0, SNTP_TEST_NOW + 1000000000,
                          SNTP_TEST_NOW + 1000000000, 100000000,
                          address_of offset, address_of delay);
        if (!sntp_sample_sane(0, SNTP_TEST_NOW + 1000000000,
                              SNTP_TEST_NOW + 1000000000, 100000000, offset,
                              delay, SNTP_OFFSET_MOST_NS) ||
            sntp_sample_sane(0, SNTP_TEST_NOW + 1000000000,
                             SNTP_TEST_NOW + 1000000000, 100000000, offset,
                             delay, SNTP_OFFSET_SYNCED_NS) ||
            !sntp_target_ok(0, offset, address_of target) ||
            target < SNTP_TEST_NOW || target > SNTP_TEST_NOW + 1000000000)
                return false;

        sntp_offset_delay(SNTP_TEST_NOW, SNTP_TEST_NOW + two_days,
                          SNTP_TEST_NOW + two_days,
                          SNTP_TEST_NOW + 50000000, address_of offset,
                          address_of delay);
        if (sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + two_days,
                             SNTP_TEST_NOW + two_days,
                             SNTP_TEST_NOW + 50000000, offset, delay,
                             SNTP_OFFSET_MOST_NS))
                return false;

        /* An authenticated (NTS) answer is asked with the whole window as its
           reach (sntp_query_nts): a month out and two years out, from a clock
           that is synchronised or not, where the same offset from the pool is
           refused; but not one outside the window, or whose order is wrong. */
        {
                bipolar month = (bipolar)30 * 86400 * (bipolar)SNTP_NANOSECONDS;
                bipolar years = (bipolar)700 * 86400 * (bipolar)SNTP_NANOSECONDS;

                if (!sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + month,
                                      SNTP_TEST_NOW + month, SNTP_TEST_NOW + 10000000,
                                      month, 10000000, SNTP_OFFSET_ANY_NS) ||
                    !sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + years,
                                      SNTP_TEST_NOW + years, SNTP_TEST_NOW + 10000000,
                                      years, 10000000, SNTP_OFFSET_ANY_NS) ||
                    sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + month,
                                     SNTP_TEST_NOW + month, SNTP_TEST_NOW + 10000000,
                                     month, 10000000, SNTP_OFFSET_SYNCED_NS) ||
                    sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + month,
                                     SNTP_TEST_NOW + month, SNTP_TEST_NOW + 10000000,
                                     month, 10000000, SNTP_OFFSET_MOST_NS) ||
                    sntp_sample_sane(SNTP_TEST_NOW, SNTP_WALL_MOST_NS + 1,
                                     SNTP_WALL_MOST_NS + 1, SNTP_TEST_NOW + 10000000,
                                     1, 10000000, SNTP_OFFSET_ANY_NS) ||
                    sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + 5, SNTP_TEST_NOW + 4,
                                     SNTP_TEST_NOW + 10000000, 0, 10000000,
                                     SNTP_OFFSET_ANY_NS))
                        return false;
        }

        if (!sntp_short_ok(0) || !sntp_short_ok(SNTP_SHORT_SECOND) ||
            sntp_short_ok(SNTP_SHORT_SECOND + 1) ||
            sntp_short_ok(0x80000000u))
                return false;

        /*
                The seconds bound is the one that has to hold exactly: a
                whole second short of it, with the largest fraction, is
                still a number, and one second past it is not.
        */
        if (sntp_timespec_ns(SNTP_TIMESPEC_SECONDS_MOST,
                             SNTP_NANOSECONDS - 1) < 0 ||
            sntp_timespec_ns(SNTP_TIMESPEC_SECONDS_MOST + 1, 0) >= 0 ||
            sntp_timespec_ns(0, SNTP_NANOSECONDS) >= 0)
                return false;

        if (!sntp_local_ok(0) || !sntp_local_ok(SNTP_WALL_MOST_NS) ||
            sntp_local_ok(SNTP_WALL_MOST_NS + 1) || sntp_local_ok(-1))
                return false;

        //      The window holds every date from this source's to the end of the
        //      era 1 stamps, and a step that would land outside it is refused
        //      on either side.
        if (!sntp_wall_ok(SNTP_TEST_DATE_1) || !sntp_wall_ok(SNTP_TEST_DATE_2) ||
            !sntp_wall_ok(SNTP_TEST_DATE_3) || !sntp_wall_ok(SNTP_TEST_DATE_4) ||
            !sntp_wall_ok(SNTP_TEST_DATE_5) ||
            !sntp_wall_ok(SNTP_WALL_MOST_NS) ||
            sntp_wall_ok(SNTP_WALL_MOST_NS + 1) ||
            sntp_wall_ok(SNTP_WALL_LEAST_NS - 1) ||
            !sntp_target_ok(SNTP_TEST_DATE_2, 1000000000, address_of target) ||
            target != SNTP_TEST_DATE_2 + 1000000000 ||
            !sntp_target_ok(SNTP_TEST_DATE_4, -1000000000, address_of target) ||
            target != SNTP_TEST_DATE_4 - 1000000000 ||
            sntp_target_ok(SNTP_WALL_MOST_NS, 1, address_of target))
                return false;

        for (at = 0; at < array_count(stamp_case); at++)
        {
                p8 field[8];

                network_store_32(field, stamp_case[at].seconds);
                network_store_32(field + 4, stamp_case[at].fraction);
                if (sntp_load_stamp(field) != stamp_case[at].want)
                        return false;
        }

        memory_fill(request, 0, sizeof(request));
        request[0] = SNTP_LI_VN_MODE;
        network_store_32(request + 40, 0xc0ffee00u);
        network_store_32(request + 44, 0x0badf00du);
        for (at = 0; at < array_count(reply_case); at++)
        {
                memory_fill(reply, 0, sizeof(reply));
                reply[0] = reply_case[at].first;
                reply[1] = reply_case[at].stratum;
                network_store_32(reply + 4, reply_case[at].root_delay);
                network_store_32(reply + 8, reply_case[at].root_dispersion);
                network_store_32(reply + 12, reply_case[at].reference_id);
                if (reply_case[at].echoed)
                        memory_copy(reply + 24, request + 40, 8);
                if (sntp_reply_ok(reply, request) != reply_case[at].want)
                        return false;
        }

        /*
                The reference stamp: all zeros is a server that does not
                say, and is the same server for all that; one that says is
                held to the window and to what the server sent.
        */
        {
                static const struct
                {
                        bipolar reference; /* unix seconds; -1 for all zeros */
                        bipolar want;
                } reference_case[] = {
                    {-1, SNTP_OK},
                    {SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS - 10, SNTP_OK},
                    {SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS + 1, SNTP_OK},
                    {SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS + 5,
                     SNTP_BAD_SERVER},
                    {SNTP_WALL_LEAST - 1, SNTP_BAD_SERVER},
                    {1, SNTP_BAD_SERVER},
                };
                sntp_sample taken;

                for (at = 0; at < array_count(reference_case); at++)
                {
                        memory_fill(reply, 0, sizeof(reply));
                        reply[0] = 0x24;
                        reply[1] = 2;
                        memory_copy(reply + 24, request + 40, 8);
                        sntp_test_stamp(reply + 32, SNTP_TEST_NOW + 1000000);
                        sntp_test_stamp(reply + 40, SNTP_TEST_NOW + 1000000);
                        if (reference_case[at].reference >= 0)
                                sntp_test_stamp(reply + 16,
                                                reference_case[at].reference *
                                                    (bipolar)SNTP_NANOSECONDS);
                        memory_zero(address_of taken, sizeof(taken));
                        if (sntp_reply_sample(reply, request, SNTP_TEST_NOW,
                                              SNTP_TEST_NOW + 2000000,
                                              SNTP_OFFSET_MOST_NS,
                                              address_of taken) !=
                            reference_case[at].want)
                                return false;
                }
        }

        /*
                A control buffer the kernel says it filled short is read
                for nothing, the error queue's own flag is not that, and
                the buffer takes the arrival stamp, the departure stamp and
                the error header with the address behind it at once.
        */
        if (sntp_control_held(0, 144) != 144 ||
            sntp_control_held(SNTP_MSG_CTRUNC, 144) ||
            sntp_control_held(SNTP_MSG_CTRUNC | SNTP_ERRQUEUE, 128) ||
            sntp_control_held(SNTP_ERRQUEUE, 96) != 96 ||
            SNTP_CONTROL_WORDS * sizeof(positive) < 32 + 64 + 48)
                return false;

        for (at = 0; at < array_count(control_case); at++)
        {
                memory_zero(words, sizeof(words));
                sntp_test_message(control, control_case[at].claimed,
                                  control_case[at].level, control_case[at].kind,
                                  1700000000ull, 250000000ull);
                arrived[0] = 0;
                arrived[1] = 0;
                if (sntp_control_stamp(control, control_case[at].held,
                                       SNTP_TIMESTAMPNS,
                                       arrived) != control_case[at].want)
                        return false;
                if (control_case[at].want &&
                    (arrived[0] != 1700000000ull || arrived[1] != 250000000ull))
                        return false;
        }

        /*
                The kernel puts its messages in the order it likes, so the
                one we want is not always first. A message of another kind
                in front of it must be stepped over, not stopped at.
        */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA, SOL_SOCKET,
                          SNTP_TIMESTAMPNS + 7, 0, 0);
        sntp_test_message(control + SNTP_CONTROL_DATA, SNTP_CONTROL_DATA + 16,
                          SOL_SOCKET, SNTP_TIMESTAMPNS, 1700000001ull,
                          750000000ull);
        arrived[0] = 0;
        arrived[1] = 0;
        if (!sntp_control_stamp(control, 2 * SNTP_CONTROL_DATA + 16,
                                SNTP_TIMESTAMPNS, arrived) ||
            arrived[0] != 1700000001ull || arrived[1] != 750000000ull)
                return false;

        /*
                The departure stamp comes back under a different type and
                in a longer payload -- three timespecs, of which the
                software one is first -- so the walk has to take its two
                words from the front and let the rest alone, and has to
                tell the two types apart rather than taking whichever
                timestamp it meets first.
        */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA + 48, SOL_SOCKET,
                          SNTP_TIMESTAMPING, 1700000002ull, 125000000ull);
        arrived[0] = 0;
        arrived[1] = 0;
        if (!sntp_control_stamp(control, SNTP_CONTROL_DATA + 48,
                                SNTP_TIMESTAMPING, arrived) ||
            arrived[0] != 1700000002ull || arrived[1] != 125000000ull)
                return false;
        if (sntp_control_stamp(control, SNTP_CONTROL_DATA + 48,
                               SNTP_TIMESTAMPNS, arrived))
                return false;

        /*
                The transmit stamp's number rides in the error header
                beside it, not in the stamp, and the same header says
                whether the entry is a timestamp at all. A real ICMP
                error carries a different origin and must not be read as
                a sequence number, or a refused port would start
                claiming to be the answer to a send.
        */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA + SNTP_ERROR_BYTES,
                          SNTP_SOL_IP, SNTP_IP_RECVERR, 0, 0);
        control[SNTP_CONTROL_DATA + SNTP_ERROR_ORIGIN] =
            SNTP_ERROR_TIMESTAMPING;
        address_to(p32 address_to)(control + SNTP_CONTROL_DATA +
                                   SNTP_ERROR_SEQUENCE) = 4u;
        sequence = 0;
        if (!sntp_control_sequence(control, SNTP_CONTROL_DATA +
                                                SNTP_ERROR_BYTES,
                                   address_of sequence) ||
            sequence != 4u)
                return false;

        /* the same entry as an ICMP error rather than a timestamp */
        control[SNTP_CONTROL_DATA + SNTP_ERROR_ORIGIN] = 2; /* ICMP */
        sequence = 0;
        if (sntp_control_sequence(control, SNTP_CONTROL_DATA +
                                               SNTP_ERROR_BYTES,
                                  address_of sequence))
                return false;

        /* a header cut short of the field the number sits in */
        control[SNTP_CONTROL_DATA + SNTP_ERROR_ORIGIN] =
            SNTP_ERROR_TIMESTAMPING;
        address_to(positive address_to)control = SNTP_CONTROL_DATA + 8;
        sequence = 0;
        if (sntp_control_sequence(control, SNTP_CONTROL_DATA + 8,
                                  address_of sequence))
                return false;

        /* and no error header at all, which is the fallback case */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA + 48, SOL_SOCKET,
                          SNTP_TIMESTAMPING, 0, 0);
        sequence = 0;
        if (sntp_control_sequence(control, SNTP_CONTROL_DATA + 48,
                                  address_of sequence))
                return false;

        /*
                One bit of the echoed stamp flipped is still a forgery.
        */
        memory_fill(reply, 0, sizeof(reply));
        reply[0] = 0x24;
        reply[1] = 2;
        memory_copy(reply + 24, request + 40, 8);
        /* Every bit of the full random nonce is reply identity. A parser
           that compares only the old random low half recreates the weak
           predictable-clock authenticator this test is meant to prevent. */
        for (positive byte = 0; byte < 8; byte++)
                for (p8 bit = 1; bit; bit <<= 1)
                {
                        reply[24 + byte] ^= bit;
                        if (sntp_reply_ok(reply, request) != SNTP_NO_REPLY)
                                return false;
                        reply[24 + byte] ^= bit;
                }
        {
                p8 later[SNTP_PACKET];

                memory_copy(later, request, sizeof later);
                later[40] ^= 1;
                /* A valid reply to the preceding exchange is a replay, not a
                   reply to this one, even when every server field is valid. */
                if (sntp_reply_ok(reply, later) != SNTP_NO_REPLY)
                        return false;
        }

        /*
                The root distance: half of a 20 ms round trip, half of a
                half-second root delay and a 1/64 s root dispersion.
        */
        if (sntp_distance_ns(20000000, 0x8000u, 0x400u) !=
            10000000 + 250000000 + 15625000)
                return false;

        /*
                Choosing among servers. The one with the least distance is a
                falseticker when neither other interval meets its own, and
                the next best is believed; two that disagree, or three that
                all do, cannot name the wrong one, so the least distance
                stands; and one answer is the answer.
        */
        {
                sntp_sample three[SNTP_SERVERS] = {
                    {0, 0, 5000000, true},
                    {1000000, 0, 2000000, true},
                    {50000000, 0, 1000000, true},
                };
                sntp_sample apart[SNTP_SERVERS] = {
                    {0, 0, 1000000, true},
                    {10000000, 0, 2000000, true},
                    {20000000, 0, 500000, true},
                };

                /* an answer 2 h out claiming half a millisecond, against
                   an honest 10 ms one: its own word is not enough, alone
                   or against that one, and two that agree are */
                sntp_sample far[2] = {
                    {(bipolar)7200 * 1000000000, 0, 500000, true},
                    {0, 0, 10000000, true},
                };

                for (positive server = 0; server < SNTP_SERVERS; server++)
                {
                        three[server].address = (p32)(server + 1);
                        apart[server].address = (p32)(server + 1);
                }
                far[0].address = 1;
                far[1].address = 2;
                if (sntp_choose(three, 3) != 1 || sntp_choose(three + 1, 2) != 1 ||
                    sntp_choose(three + 2, 1) != 0 || sntp_choose(apart, 3) != 2)
                        return false;
                three[1].ok = false;
                if (sntp_choose(three, 3) != 2)
                        return false;
                if (sntp_choose(far, 2) != 1 || sntp_choose(far, 1) != -1)
                        return false;
                far[1].offset_ns = far[0].offset_ns + 1000000;
                if (sntp_choose(far, 2) != 0)
                        return false;
                /* one server under three names is one answer: three that agree
                   from one address are not a majority, a second server that
                   agrees with them is, and a step within 2 s needs no word */
                sntp_sample same[3] = {
                    {(bipolar)7200 * 1000000000, 0, 500000, true, 9},
                    {(bipolar)7200 * 1000000000, 0, 600000, true, 9},
                    {(bipolar)7200 * 1000000000, 0, 700000, true, 9},
                };

                if (sntp_choose(same, 3) != -1)
                        return false;
                same[2].address = 10;
                if (sntp_choose(same, 3) != 0)
                        return false;
                same[0].offset_ns = same[1].offset_ns = same[2].offset_ns = 1000000;
                same[2].address = 9;
                if (sntp_choose(same, 3) != 0)
                        return false;
        }

        return sntp_pick(row, 5) == 2 && row[2].offset_ns == 2000000;
}

/*
        Nothing unprivileged can ask the kernel which mode a bit means,
        and a wrong one returns success, so the decision is checked here
        instead: what goes in the request for a given offset, rather than
        what the kernel does with it.
*/
static COLD p8 locale_discipline_ok(void)
{
        positive words[LOGGER_TIMEX_WORDS];
        positive at;
        static const struct
        {
                bipolar offset_ns;
                p8 step;
                bipolar sec; /* what a step carries, floored */
                bipolar nsec;
        } discipline_case[] = {
            {0, false, 0, 0},
            {1000000, false, 0, 0},
            {-1000000, false, 0, 0},
            {LOCALE_NTP_STEP_NS - 1, false, 0, 0},
            {-(LOCALE_NTP_STEP_NS - 1), false, 0, 0},
            {LOCALE_NTP_STEP_NS, true, 0, LOCALE_NTP_STEP_NS},
            {-LOCALE_NTP_STEP_NS, true, -1,
             (bipolar)SNTP_NANOSECONDS - LOCALE_NTP_STEP_NS},
            {1500000000, true, 1, 500000000},
            {-1500000000, true, -2, 500000000},
            {(bipolar)86400 * 1000000000, true, 86400, 0},
            {-(bipolar)86400 * 1000000000, true, -86400, 0},
            {(bipolar)3 * 86400 * 1000000000 + 1, true, 259200, 1},
            {-((bipolar)3 * 86400 * 1000000000 + 1), true, -259201,
             (bipolar)SNTP_NANOSECONDS - 1},
        };

        for (at = 0; at < array_count(discipline_case); at++)
        {
                bipolar offset = discipline_case[at].offset_ns;

                /* a 20 ms round trip to a server at the root of its
                   tree is a 10 ms root distance */
                locale_ntp_discipline_words(offset,
                                            locale_ntp_error_us(
                                                sntp_distance_ns(20000000, 0, 0)),
                                            words);

                if (locale_ntp_wants_step(offset) != discipline_case[at].step)
                        return false;
                /* the loop is enabled either way, and the clock counts as
                   set either way, so STA_UNSYNC never survives a reply */
                if (words[LOGGER_TIMEX_STATUS] != (STA_PLL | STA_FREQHOLD))
                        return false;
                if (words[0] & ADJ_STATUS ? false : true)
                        return false;
                /* and it carries an error bound, or the kernel takes the
                   synchronisation back at the next second */
                if (!(words[0] & ADJ_MAXERROR) || !(words[0] & ADJ_ESTERROR) ||
                    words[LOGGER_TIMEX_MAXERROR] != 10001 ||
                    words[LOCALE_TIMEX_ESTERROR] != 10001)
                        return false;
                if (discipline_case[at].step)
                {
                        /* a step carries the time, cancels any slew, and
                           has no business setting a loop time constant */
                        if (!(words[0] & ADJ_SETOFFSET) ||
                            words[0] & ADJ_TIMECONST ||
                            words[LOCALE_TIMEX_OFFSET] != 0 ||
                            (bipolar)words[LOGGER_TIMEX_TIME_SEC] !=
                                discipline_case[at].sec ||
                            (bipolar)words[LOGGER_TIMEX_TIME_NSEC] !=
                                discipline_case[at].nsec)
                                return false;
                }
                else
                {
                        /* a slew hands the offset to the loop and never
                           steps, so time does not go backwards */
                        if (words[0] & ADJ_SETOFFSET ||
                            !(words[0] & ADJ_TIMECONST) ||
                            (bipolar)words[LOCALE_TIMEX_OFFSET] != offset ||
                            words[LOCALE_TIMEX_CONSTANT] !=
                                LOCALE_NTP_TIMECONST ||
                            words[LOGGER_TIMEX_TIME_SEC] ||
                            words[LOGGER_TIMEX_TIME_NSEC])
                                return false;
                }
                if (!(words[0] & ADJ_NANO) || !(words[0] & ADJ_OFFSET))
                        return false;
        }
        /* the first answer after boot steps from a millisecond out, and a
           later one only from 128 ms */
        locale_ntp_first = true;
        if (!locale_ntp_wants_step(1000000) || !locale_ntp_wants_step(-1000000) ||
            locale_ntp_wants_step(999999) || locale_ntp_wants_step(-999999))
                return false;
        locale_ntp_first = false;
        if (locale_ntp_wants_step(1000000) || locale_ntp_wants_step(-86000000))
                return false;
        /* a poll learns offset over elapsed, trusted as far as the offset
           stands above the noise: 144 ms slow after half an hour is -80
           ppm, nearly all of it learned; 150 us after 256 s is under a
           quarter of its 0.58 ppm; held within the kernel's 500, and under
           a minute, or past 1000 ppm, learns nothing */
        if (locale_ntp_learned(-144000000, (bipolar)1800 * 1000000000, 0) !=
                -5224738 ||
            locale_ntp_learned(36000000, (bipolar)1800 * 1000000000,
                               (bipolar)7 << 16) != 1751516 ||
            locale_ntp_learned(150000, (bipolar)256 * 1000000000, 0) !=
                (bipolar)150000 * 65536 / 256000 * 150000 / 650000 ||
            locale_ntp_learned(-144000000, (bipolar)1800 * 1000000000,
                               -((bipolar)490 << 16)) != -LOCALE_NTP_FREQ_MOST ||
            locale_ntp_learned(-144000000, (bipolar)30 * 1000000000, 5) != 5 ||
            locale_ntp_learned(-5000000000, (bipolar)1800 * 1000000000, 5) != 5 ||
            locale_ntp_learned((bipolar)199 * 1000000000,
                               (bipolar)200000 * 1000000000, 0) !=
                LOCALE_NTP_FREQ_MOST)
                return false;
        /* A plain answer against the NTS time of this boot: five seconds and
           a thousandth of the time since (an hour of uptime allows 8.6 s, a
           minute 5.06 s); nothing known allows everything. */
        {
                bipolar noon = (bipolar)SNTP_TEST_NOW;
                p64 known = (p64)(SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS);

                if (locale_plain_apart(noon, 0, known, 0) ||
                    locale_plain_apart(noon, 4900000000, known, 0) ||
                    !locale_plain_apart(noon, 5100000000, known, 0) ||
                    !locale_plain_apart(noon, -5100000000, known, 0) ||
                    locale_plain_apart(noon, 8000000000, known, 3600) ||
                    !locale_plain_apart(noon, 9000000000, known, 3600) ||
                    !locale_plain_apart(noon, 3600000000000, known, 60) ||
                    locale_plain_apart(noon, 3600000000000, 0, 0) ||
                    locale_plain_apart(-1, 1, known, 0))
                        return false;
        }
        /* a 10 ms distance is 10 ms, rounded up a microsecond; a negative
           or absurd one still gives a bound the kernel accepts */
        return locale_ntp_error_us((bipolar)10 * 1000000) == 10001 &&
               locale_ntp_error_us(0) == 1 && locale_ntp_error_us(-5) == 1 &&
               locale_ntp_error_us((bipolar)60 * 1000000000) ==
                   LOCALE_NTP_ERROR_MOST_US;
}

/*
        The NTS servers file, line by line: a line is a server when it is a
        valid entry once its trailing blanks are gone and it is shorter than
        the room a server gets, never cut to fit; and a file that filled the
        buffer it was read into has no last line, since that one may be cut.
*/
static fn machine_nts_lines(void)
{
        static const char two[] = "time.example.org\r\nnts.example.net:4460  \n";
        static const char five[] = "a.example\nb.example\nc.example\nd.example\ne.example\n";
        static const char tail[] = "a.example\nb.example.org";
        p8 into[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
        p8 long_line[80];
        positive count;

        memory_zero(into, sizeof(into));
        count = locale_nts_lines((const p8 address_to)two, sizeof(two) - 1, true,
                                 into, LOCALE_NTS_ENTRY_ROOM);
        check("two servers are read from a file, CRLF and blanks trimmed",
              count == 2 && string_equals(into, "time.example.org") &&
                  string_equals(into + LOCALE_NTS_ENTRY_ROOM, "nts.example.net:4460"));
        count = locale_nts_lines((const p8 address_to)five, sizeof(five) - 1, true,
                                 into, LOCALE_NTS_ENTRY_ROOM);
        check("four servers at most are taken from a file",
              count == LOCALE_NTS_SERVERS_MOST);
        /* a name, then blanks and an x: 80 bytes, of which the first 79 are
           the name and blanks. A line too long for its room is refused, not
           taken by its first bytes */
        memory_fill(long_line, ' ', sizeof(long_line));
        memory_copy(long_line, "a.b", 3);
        long_line[79] = 'x';
        count = locale_nts_lines((const p8 address_to)long_line, sizeof(long_line),
                                 true, into, LOCALE_NTS_ENTRY_ROOM);
        check("a line too long for its room is no server, not its first bytes",
              count == 0);
        count = locale_nts_lines((const p8 address_to)tail, sizeof(tail) - 1, false,
                                 into, LOCALE_NTS_ENTRY_ROOM);
        check("a last line the buffer cut short is no server", count == 1);
        count = locale_nts_lines((const p8 address_to)tail, sizeof(tail) - 1, true,
                                 into, LOCALE_NTS_ENTRY_ROOM);
        check("a last line that ends the file is a server", count == 2);
}

static fn machine_sntp(void)
{
        check("RFC 5905 offset, min-delay pick and poison guards hold",
              sntp_math_ok());
        check("the clock is stepped when far out and slewed when near",
              locale_discipline_ok());
        sntp_datagram_framing();
        machine_ntp_schedule();
        machine_ntp_child();
        machine_ntp_no_child();
        machine_ntp_heard();
        machine_nts_lines();

        /* Case 0 refuses blocking getrandom: the stamp fails untouched and
           the exchange returns before any send. Case 1 refuses only
           GRND_NONBLOCK, which the nonce must never need. */
        for (positive which = 0; which < 2; which++)
        {
                bipolar child = system_call_2(syscall(clone), SIGCHLD, 0);
                positive raw = 0;
                p8 held;

                check("SNTP entropy-policy child starts", child >= 0);
                if (!child)
                        sntp_test_random_child(which);
                if (child <= 0)
                        return;
                held = system_wait4_retry((b32)child, address_of raw, 0,
                                          null) == child;
                if (held && wait_status_code(raw) == 77)
                {
                        log_direct(str("SNTP: entropy-policy assertion NOT RUN -- seccomp unavailable\n"));
                        return;
                }
                held = held && wait_status_code(raw) == 0;
                if (which)
                        check("SNTP draws its nonce without GRND_NONBLOCK",
                              held);
                else
                        check("SNTP fails closed when blocking entropy is unavailable",
                              held);
        }
}

/*
        Auto timezone: which answers from the network are believed, and
        when a network counts as the one already asked about. The request
        itself is the boot lane's, on a guest with a real route out.
*/
static fn machine_auto(void)
{
        locale_auto_answer answer;
        p8 word[64];

        check("a new gateway hardware address is not a new network until both are known",
              locale_network_same("eth0 0202000A", "eth0 0202000A 52:55:0a:00:02:02") &&
                  locale_network_same("eth0 0202000A 52:55:0a:00:02:02", "eth0 0202000A"));
        check("another router behind the same address is",
              !locale_network_same("wlan0 0101A8C0 aa:bb:cc:00:00:01",
                                   "wlan0 0101A8C0 aa:bb:cc:00:00:02"));
        check("and so is another interface or gateway, or none",
              !locale_network_same("wlan0 0101A8C0", "eth0 0101A8C0") &&
                  !locale_network_same("eth0 0101A8C0", "eth0 0201A8C0") &&
                  !locale_network_same("", "") &&
                  !locale_network_same("eth0 0101A8C0", ""));
        check("a zone word from the network is a plain name",
              locale_auto_word("Europe/Stockholm", 16, word, sizeof(word)) &&
                  string_equals(word, "Europe/Stockholm") &&
                  !locale_auto_word("../../etc", 9, word, sizeof(word)) &&
                  !locale_auto_word("Europe/Stock holm", 17, word, sizeof(word)) &&
                  !locale_auto_word("", 0, word, sizeof(word)));
        check("a country answer names its zone and says it is a country's",
              locale_auto_country("se", address_of answer) &&
                  string_equals(answer.zone, "Europe/Stockholm") &&
                  string_equals(answer.mode, "auto country SE") &&
                  locale_auto_country("SV", address_of answer) &&
                  string_equals(answer.zone, "America/El_Salvador") &&
                  !locale_auto_country("xx", address_of answer) &&
                  !locale_auto_country("uk", address_of answer));

        {
                static const struct
                {
                        char head[160];
                        char zone[40];
                        char mode[24];
                } heads[] = {
                        {"HTTP/1.1 200 OK\r\ncountry: SE\r\ntimezone: Europe/Stockholm\r\n\r\n",
                         "Europe/Stockholm", "auto cloudflare"},
                        //      a link is stored as the zone it names
                        {"HTTP/1.1 200 OK\r\nTimezone: Asia/Calcutta\r\n\r\n",
                         "Asia/Kolkata", "auto cloudflare"},
                        //      a name the table does not hold falls to the country
                        {"HTTP/1.1 200 OK\r\ntimezone: Mars/Olympus\r\ncountry: NZ\r\n\r\n",
                         "Pacific/Auckland", "auto country NZ"},
                        //      and one that is not a plain word is not read at all
                        {"HTTP/1.1 200 OK\r\ntimezone: ../../etc/shadow\r\ncountry: us\r\n\r\n",
                         "America/New_York", "auto country US"},
                        //      a zone that is only an abbreviation is not a place
                        {"HTTP/1.1 200 OK\r\ntimezone: CET\r\n\r\n", "", ""},
                        {"HTTP/1.1 200 OK\r\n\r\n", "", ""},
                };
                p8 all = true;

                for (positive at = 0; at < array_count(heads); at++)
                {
                        p8 took;

                        memory_zero(address_of answer, sizeof(answer));
                        took = locale_auto_from_headers(
                            (p8 address_to)heads[at].head,
                            string_length(heads[at].head), address_of answer);
                        if (took != (heads[at].zone[0] != 0) ||
                            (took && (!string_equals(answer.zone, heads[at].zone) ||
                                      !string_equals(answer.mode, heads[at].mode))))
                                all = false;
                }
                check("the speed test's headers are read as zone, else country",
                      all);
        }
        {
                static char trace[] = "fl=1030f73\nh=cloudflare.com\nip=192.0.2.1\n"
                                      "colo=ARN\nloc=SE\ntls=TLSv1.3\n";
                static char bare[] = "loc=JP\r\n";
                static char none[] = "fl=1\nloc=\n";

                check("and the trace's loc= line when they are not there",
                      locale_auto_from_trace((p8 address_to)trace,
                                             sizeof(trace) - 1, address_of answer) &&
                          string_equals(answer.zone, "Europe/Stockholm") &&
                          locale_auto_from_trace((p8 address_to)bare,
                                                 sizeof(bare) - 1, address_of answer) &&
                          string_equals(answer.zone, "Asia/Tokyo") &&
                          !locale_auto_from_trace((p8 address_to)none,
                                                  sizeof(none) - 1, address_of answer));
        }
        //      The question runs in a confined child (locale_auto_ask_apart),
        //      and what it says back is a status and two words that root
        //      believes only as the zone table spells them.
        {
                static const struct
                {
                        b64 status;
                        char zone[64];
                        char mode[32];
                        b64 believed;
                        char want_zone[40];
                        char want_mode[24];
                } said[] = {
                        {0, "Europe/Stockholm", "auto cloudflare", 0,
                         "Europe/Stockholm", "auto cloudflare"},
                        {0, "Pacific/Auckland", "auto country NZ", 0,
                         "Pacific/Auckland", "auto country NZ"},
                        //      the code is what decides, not the zone beside it
                        {0, "Europe/Stockholm", "auto country JP", 0,
                         "Asia/Tokyo", "auto country JP"},
                        //      only the table's own spelling of a zone
                        {0, "Asia/Calcutta", "auto cloudflare", HTTP_MALFORMED, "", ""},
                        {0, "europe/stockholm", "auto cloudflare", HTTP_MALFORMED, "", ""},
                        {0, "Mars/Olympus", "auto cloudflare", HTTP_MALFORMED, "", ""},
                        {0, "Europe/Stockholm\x1b]0;x\x07", "auto cloudflare",
                         HTTP_MALFORMED, "", ""},
                        {0, "Europe/Stockholm", "auto country XX", HTTP_MALFORMED, "", ""},
                        {0, "Europe/Stockholm", "auto country SEX", HTTP_MALFORMED, "", ""},
                        {0, "Europe/Stockholm", "auto manual", HTTP_MALFORMED, "", ""},
                        {0, "Europe/Stockholm", "", HTTP_MALFORMED, "", ""},
                        //      a status http_run can return is passed on; any
                        //      other is a child that cannot be believed
                        {HTTP_NO_HOST, "", "", HTTP_NO_HOST, "", ""},
                        {HTTP_MISMATCH, "Europe/Stockholm", "auto cloudflare",
                         HTTP_MISMATCH, "", ""},
                        {-6, "", "", HTTP_APART, "", ""},
                        {1, "Europe/Stockholm", "auto cloudflare", HTTP_APART, "", ""},
                        {-1000, "", "", HTTP_APART, "", ""},
                };
                p8 all = true;

                for (positive at = 0; at < array_count(said); at++)
                {
                        locale_auto_said one;
                        bipolar believed;

                        memory_zero(address_of one, sizeof one);
                        one.status = said[at].status;
                        memory_copy(one.answer.zone, said[at].zone,
                                    sizeof(one.answer.zone));
                        memory_copy(one.answer.mode, said[at].mode,
                                    sizeof(one.answer.mode));
                        believed = locale_auto_believe(address_of one,
                                                       address_of answer);
                        if (believed != said[at].believed ||
                            !string_equals(answer.zone, said[at].want_zone) ||
                            !string_equals(answer.mode, said[at].want_mode))
                                all = false;
                }
                check("a confined zone question is believed only as the table spells it",
                      all);
        }
        {
                locale_auto_said one;

                //      Every byte set and no terminator anywhere.
                memory_fill(address_of one, 'A', sizeof one);
                one.status = 0;
                check("and an answer with no terminator is refused, not read past",
                      locale_auto_believe(address_of one, address_of answer) ==
                              HTTP_MALFORMED &&
                          !answer.zone[0] && !answer.mode[0]);
        }
}

/*
        init's start of a boot service (src/sh/system.c, init_service_start),
        with a start that fails as many times as a row asks and then answers
        a child: a fork that fails is said on the console with its reason and
        tried again, at once and then after the machine process's own pauses,
        and five in a row are said and leave the service alone. A failed fork
        was neither said nor tried again, so the settle, network or machine
        service was not there for the rest of the boot.
*/
static positive machine_start_failing;
static positive machine_start_calls;
static p64 machine_start_at[8];
static p8 machine_start_said[2048];
static positive machine_start_used;

static bipolar machine_start_stub(string_address path, string_address address_to arguments,
                                  positive count)
{
        (void)path;
        (void)arguments;
        (void)count;
        if (machine_start_calls < array_count(machine_start_at))
                machine_start_at[machine_start_calls] = clock_monotonic_nanoseconds();
        return machine_start_calls++ < machine_start_failing ? -EAGAIN : 4242;
}

//      A writer handed no length is handed a string to its terminator (string_format's %s).
static fn machine_start_capture(address_any data, positive length)
{
        if (!length)
                length = string_length((string_address)data);
        if (machine_start_used + length >= sizeof(machine_start_said))
                length = sizeof(machine_start_said) - 1 - machine_start_used;
        memory_copy_apart(machine_start_said + machine_start_used, data, length);
        machine_start_used += length;
        machine_start_said[machine_start_used] = end;
}

static bipolar machine_start_run(positive failing)
{
        static const init_service service = {(string_address) "the checked service",
                                             (string_address) "/nowhere", null, 0};

        machine_start_failing = failing;
        machine_start_calls = 0;
        machine_start_used = 0;
        machine_start_said[0] = end;
        return init_service_start(machine_start_capture, machine_start_stub, address_of service);
}

// How many times the words are in what was said.
static positive machine_start_count(string_address words)
{
        positive seen = 0;

        for (string_address at = (string_address)string_find(machine_start_said, words); at;
             at = (string_address)string_find(at + 1, words))
                seen++;
        return seen;
}

static fn machine_services(void)
{
        bipolar got = machine_start_run(0);

        check("a boot service that starts is started once, and nothing is said",
              got == 4242 && machine_start_calls == 1 && !machine_start_used);

        got = machine_start_run(2);
        check("one whose fork fails twice is started at the third try",
              got == 4242 && machine_start_calls == 3);
        check("and each failure is said on the console, with the service and the reason",
              machine_start_count((string_address) "could not start the checked service: ") == 2 &&
                  !machine_start_count((string_address) "leaving it alone"));
        check("and the first is tried again at once, the second after 250 ms",
              machine_start_at[1] - machine_start_at[0] < 200000000ull &&
                  machine_start_at[2] - machine_start_at[1] >= 250000000ull);

        got = machine_start_run(1000);
        check("one that never starts is tried five times and left alone",
              got == -1 && machine_start_calls == 5 &&
                  machine_start_count((string_address) "could not start the checked service: ") == 5 &&
                  machine_start_count((string_address) "the checked service did not start in 5 "
                                                       "tries; leaving it alone") == 1);
        check("after the machine process's pauses between them, 250 ms doubling",
              machine_start_at[4] - machine_start_at[0] >= 1750000000ull &&
                  machine_start_at[3] - machine_start_at[2] >= 500000000ull &&
                  machine_start_at[4] - machine_start_at[3] >= 1000000000ull);
}

b32 main(void)
{
        machine_hooks();
        machine_arms();
        machine_policy();
        machine_stop_self();
        machine_pin_numbers();
        machine_run_scripts();
        machine_runner_children();
        machine_load_clash();
        machine_runner_budget();
        machine_runner_lines();
        machine_sntp();
        machine_auto();
        machine_services();
        return test_report(null);
}
#endif /* CHECK_machine */

#ifdef CHECK_snapshot
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

b32 main(void)
{
        struct snapshot_process process;
        static const string_address invalid[] = {
            "", "7 worker", "7 (worker", "7 (worker)", "7 (worker) ",
            "7 (worker) R", "7 (worker)R 1", "7 (worker)  1", "7 (worker) RR 1",
        };
        p8 address_to pages = memory(8192);
        p8 mapped = (bipolar)(positive)pages > 0;
        check("snapshot guard mapping", mapped);
        if (!mapped)
                return test_report(null);
        p8 protected = system_call_3(syscall(mprotect),
            (positive)(pages + 4096), 4096, 0) == 0;
        check("snapshot sentinel guard page", protected);
        if (!protected)
        {
                memory_free(pages, 8192);
                return test_report(null);
        }
        for (positive i = 0; i < array_count(invalid); i++)
        {
                positive length = string_length(invalid[i]) + 1;
                p8 address_to at = pages + 4096 - length;
                memory_copy(at, invalid[i], length);
                memory_fill(&process, 0xa5, sizeof(process));
                check("snapshot rejects truncated or malformed state without crossing sentinel",
                      !system_process_parse(at, 4096, &process) &&
                      memory_span_byte(&process, 0xa5, sizeof(process)) == sizeof(process));
        }
        /* A command may contain ')'; the final one still closes its name. */
        string_address valid = "7 (a)b) R 1 2 3 4 5 0 0 0 0 0 6 7 0 0 0 8 9 0 10 11 12";
        positive length = string_length(valid) + 1;
        p8 address_to at = pages + 4096 - length;
        memory_copy(at, valid, length);
        check("snapshot preserves valid process fields at the page edge",
              system_process_parse(at, 4096, &process) && process.pid == 7 &&
              string_equals(process.command, "a)b") && process.state == 'R' &&
              process.ppid == 1 && process.pgrp == 2 && process.session == 3 &&
              process.tty == 4 && process.tpgid == 5 && process.nice == 8 &&
              process.threads == 9 && process.user_ns == 6 * SYSTEM_TICK_NS &&
              process.system_ns == 7 * SYSTEM_TICK_NS &&
              process.start_ns == 10 * SYSTEM_TICK_NS && process.virtual_bytes == 11 &&
              process.resident_bytes == 12 * 4096);
        memory_free(pages, 8192);
        return test_report(null);
}
#endif /* CHECK_snapshot */

#ifdef CHECK_sensors
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        The monitor's hardware rows, against sysfs trees made up here.

        A seeded grammar builds each tree: hwmon chips by name (the ones the
        monitor knows and ones it does not), temperatures, labels, fans and
        a GPU's draw, a battery and the mains, processors with and without a
        clock, and every file's text drawn from good numbers, zero, negative,
        too large, empty, not a number, and a number with more after it. A
        model written from the rules as the monitor's comment states them
        says what the two rows must read, and the rows are compared as text.
        Around it, the cases a grammar walks past: slow sensors held between
        reads, the energy counter's watts, a chip gone and one arrived, and
        more chips than there is room for.
*/

static p8 sensors_base[96];
static p8 sensors_out[4096];
static positive sensors_used;
static p64 sensors_state = 0x9e3779b97f4a7c15ull;

static positive sensors_next(positive below)
{
        XORSHIFT64(sensors_state);
        return below ? (positive)(sensors_state % below) : 0;
}

static fn sensors_capture(address_any data, positive length)
{
        if (sensors_used + length >= sizeof(sensors_out))
                length = sizeof(sensors_out) - 1 - sensors_used;
        memory_copy_apart(sensors_out + sensors_used, data, length);
        sensors_used += length;
        sensors_out[sensors_used] = end;
}

static fn sensors_mkdir_parents(string_address path)
{
        p8 part[256];
        positive length = string_length(path);

        for (positive at = 1; at <= length && at < sizeof(part); at++)
                if (path[at] == '/' || at == length)
                {
                        memory_copy_apart(part, path, at);
                        part[at] = end;
                        system_call_3(syscall(mkdirat), AT_FDCWD,
                                      (positive)part, 0755);
                }
}

//      A file under the tree, its directories made on the way.
static fn sensors_put(string_address root, string_address relative,
                      string_address text)
{
        p8 path[256];
        p8 directory[256];

        path_join(path, sizeof(path), root, relative);
        path_head_copy(directory, sizeof(directory), path);
        sensors_mkdir_parents(directory);

        b32 made = system_call_4(syscall(openat), AT_FDCWD, (positive)path,
                                 FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);

        if (made >= 0)
        {
                system_call_3(syscall(write), made, (positive)text,
                              string_length(text));
                system_call_1(syscall(close), made);
        }
}

static fn sensors_remove(string_address path)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;

        if (file_walk_open(address_of walk, AT_FDCWD, path))
        {
                while ((entry = file_walk_next(address_of walk)))
                {
                        p8 inner[256];

                        if (string_equals(entry->d_name, ".") ||
                            string_equals(entry->d_name, ".."))
                                continue;
                        path_join(inner, sizeof(inner), path, entry->d_name);
                        sensors_remove(inner);
                }
                file_walk_close(address_of walk);
                system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path,
                              AT_REMOVEDIR);
        }
        else
                system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, 0);
}

//      A named question about the rows, which says what they read when
//      the answer is no.
static fn sensors_expect(string_address name, p8 right)
{
        checks++;
        if (!right)
        {
                failures++;
                string_format(log, "  FAIL %s: rows [%s]\n", name, sensors_out);
        }
}

static fn sensors_rows(monitor_hardware address_to hardware)
{
        sensors_used = 0;
        sensors_out[0] = end;
        if (monitor_hardware_row(hardware, false, sensors_capture))
                sensors_capture("|", 1);
        if (monitor_hardware_row(hardware, true, sensors_capture))
                sensors_capture("|", 1);
}

//      A value the grammar writes, and what the model makes of it: the
//      number when the whole file is one, else nothing.
typedef struct
{
        p8 text[40];
        p8 number;
        bipolar value;
} sensors_value;

static fn sensors_draw(sensors_value address_to into, bipolar good_low,
                       bipolar good_high)
{
        positive shape = sensors_next(10);
        bipolar value = good_low + (bipolar)sensors_next(
                                       (positive)(good_high - good_low));
        positive at = 0;

        into->number = true;
        switch (shape)
        {
        case 0: value = 0; break;
        case 1: value = -(bipolar)sensors_next(100000) - 1; break;
        case 2: value = good_high + 1 + (bipolar)sensors_next(1000000); break;
        case 3: into->text[0] = end; into->number = false; return;
        case 4: memory_copy_apart(into->text, "n/a\n", 5); into->number = false; return;
        case 5:
                at = positive_into(into->text, (positive)value);
                memory_copy_apart(into->text + at, "x\n", 3);
                into->number = false;
                return;
        default: break;
        }

        into->value = value;
        if (value < 0)
        {
                into->text[at++] = '-';
                value = -value;
        }
        at += positive_into(into->text + at, (positive)value);
        if (sensors_next(4))
                into->text[at++] = '\n';
        into->text[at] = end;
}

//      The model's rows, built beside the tree.
static p8 model_temp[2048];
static p8 model_power[2048];
static positive model_count;

static fn model_append(p8 address_to row, string_address text)
{
        positive length = string_length(row);

        string_copy_bounded(row + length, text, 2048 - length);
}

static fn model_number(p8 address_to row, positive value)
{
        p8 digits[24];

        digits[positive_into(digits, value)] = end;
        model_append(row, digits);
}

static fn model_item(p8 address_to row, string_address label)
{
        model_append(row, row[0] ? "  " : (row == model_temp ? " temp  " : " power "));
        if (string_length(row) == 7)
                model_append(row, " ");
        model_append(row, label);
        model_append(row, " ");
}

//      One sensor the monitor would find: counted against the room it has,
//      and drawn when its value is one.
static p8 model_room(void)
{
        return model_count++ < MONITOR_SENSORS;
}

static fn model_temperature(string_address label, sensors_value address_to value)
{
        if (!value->number || !model_room())
                return;
        if (value->value > 0 && value->value <= 200000)
        {
                model_item(model_temp, label);
                model_number(model_temp, ((positive)value->value + 500) / 1000);
                model_append(model_temp, "\xc2\xb0" "C");
        }
}

static fn model_fan(sensors_value address_to value)
{
        if (!value->number || !model_room())
                return;
        if (value->value > 0 && value->value < 30000)
        {
                model_item(model_temp, "fan");
                model_number(model_temp, (positive)value->value);
                model_append(model_temp, " rpm");
        }
}

static fn model_watts(p8 address_to row, positive microwatts)
{
        positive tenths = (microwatts + 50000) / 100000;

        model_number(row, tenths / 10);
        model_append(row, ".");
        model_number(row, tenths % 10);
        model_append(row, " W");
}

static fn sensors_file_name(p8 address_to into, string_address stem,
                            positive index, string_address tail)
{
        positive length = string_length(stem);

        memory_copy_apart(into, stem, length);
        length += positive_into(into + length, index);
        string_copy_bounded(into + length, tail, 32);
}

static fn sensors_chip(string_address root, positive number, p8 address_to have_cpu)
{
        static string_address names[] = {"k10temp", "coretemp", "amdgpu", "nvme",
                                         "drivetemp", "acpitz", "nct6775", "it87",
                                         "zenpower", "cpu_thermal", "mystery"};
        string_address name = names[sensors_next(array_count(names))];
        p8 directory[64];
        p8 file[64];
        p8 relative[128];

        sensors_file_name(directory, "class/hwmon/hwmon", number, "");
        path_join(relative, sizeof(relative), directory, "name");
        sensors_put(root, relative, name);
        //      A name file with its newline, as the kernel writes it,
        //      more often than not.
        if (sensors_next(3))
        {
                p8 named[32];

                string_copy_bounded(named, name, sizeof(named) - 1);
                model_append(named, "");
                positive length = string_length(named);
                named[length] = '\n';
                named[length + 1] = end;
                sensors_put(root, relative, named);
        }

        string_address label = null;
        positive index = 1;
        p8 cpu = false;

        if (string_equals(name, "k10temp") || string_equals(name, "zenpower") ||
            string_equals(name, "coretemp"))
        {
                string_address want = string_equals(name, "coretemp")
                                          ? "Package id 0\n"
                                          : sensors_next(2) ? "Tctl\n" : "Tdie\n";
                cpu = true;
                label = "cpu";
                //      The label at a drawn place, or none: temp1 then.
                if (sensors_next(3))
                {
                        index = 1 + sensors_next(3);
                        sensors_file_name(file, "/temp", index, "_label");
                        string_copy_bounded(relative, directory, sizeof(relative));
                        model_append(relative, file);
                        sensors_put(root, relative, want);
                }
                //      A decoy label ahead of it, which must not be taken.
                if (index > 1 && sensors_next(2))
                {
                        sensors_file_name(file, "/temp", index - 1, "_label");
                        string_copy_bounded(relative, directory, sizeof(relative));
                        model_append(relative, file);
                        sensors_put(root, relative, "Tccd1\n");
                }
        }
        else if (string_equals(name, "cpu_thermal"))
                cpu = true, label = "cpu";
        else if (string_equals(name, "amdgpu"))
                label = "gpu";
        else if (string_equals(name, "nvme"))
                label = "nvme";
        else if (string_equals(name, "drivetemp"))
                label = "disk";
        else if (string_equals(name, "acpitz"))
                label = "acpi";

        if (cpu)
        {
                if (address_to have_cpu)
                        label = null;
                address_to have_cpu = true;
        }

        //      Every chip gets temperatures on three places; only the one
        //      the rules pick may show.
        for (positive at = 1; at <= 3; at++)
        {
                sensors_value value;

                sensors_draw(address_of value, 20000, 110000);
                sensors_file_name(file, "/temp", at, "_input");
                string_copy_bounded(relative, directory, sizeof(relative));
                model_append(relative, file);
                if (sensors_next(5))
                        sensors_put(root, relative, value.text);
                else if (at == index && label)
                        continue;
                if (at == index && label)
                        model_temperature(label, address_of value);
        }

        if (label && string_equals(label, "gpu"))
        {
                sensors_value value;
                positive which = sensors_next(3);

                sensors_draw(address_of value, 1000000, 300000000);
                if (which < 2)
                {
                        string_copy_bounded(relative, directory, sizeof(relative));
                        model_append(relative, which ? "/power1_input" : "/power1_average");
                        sensors_put(root, relative, value.text);
                        if (value.number && model_room() && value.value > 0 &&
                            value.value < 2000000000)
                        {
                                model_item(model_power, "gpu");
                                model_watts(model_power, (positive)value.value);
                        }
                }
        }

        for (positive at = 1; at <= 8; at++)
        {
                if (sensors_next(3))
                        continue;

                sensors_value value;

                sensors_draw(address_of value, 300, 9000);
                sensors_file_name(file, "/fan", at, "_input");
                string_copy_bounded(relative, directory, sizeof(relative));
                model_append(relative, file);
                sensors_put(root, relative, value.text);
                model_fan(address_of value);
        }
}

static fn sensors_supplies(string_address root)
{
        positive batteries = sensors_next(3);
        p8 mains = sensors_next(2);

        //      power_supply is walked in name order: AC before BAT0.
        if (mains)
        {
                positive shape = sensors_next(4);

                sensors_put(root, "class/power_supply/AC/type", "Mains\n");
                sensors_put(root, "class/power_supply/AC/online",
                            shape == 0   ? "1\n"
                            : shape == 1 ? "0\n"
                            : shape == 2 ? "2\n"
                                         : "on\n");
                if (shape != 3 && model_room() && shape < 2)
                {
                        model_item(model_power, "ac");
                        model_append(model_power, shape == 0 ? "on" : "off");
                }
        }

        for (positive at = 0; at < batteries; at++)
        {
                p8 directory[64];
                p8 relative[128];
                p8 device = sensors_next(4) == 0;
                sensors_value capacity;

                sensors_file_name(directory, "class/power_supply/BAT", at, "");
                path_join(relative, sizeof(relative), directory, "type");
                sensors_put(root, relative, "Battery\n");
                if (device)
                {
                        path_join(relative, sizeof(relative), directory, "scope");
                        sensors_put(root, relative, "Device\n");
                }
                sensors_draw(address_of capacity, 1, 100);
                path_join(relative, sizeof(relative), directory, "capacity");
                sensors_put(root, relative, capacity.text);

                string_address status = sensors_next(2) ? "Discharging\n"
                                                        : "Not charging\n";
                p8 has_status = sensors_next(4) != 0;

                if (has_status)
                {
                        path_join(relative, sizeof(relative), directory, "status");
                        sensors_put(root, relative, status);
                }

                positive power = 0;
                positive how = sensors_next(3);

                if (how == 0)
                {
                        power = 1000000 + sensors_next(30000000);
                        p8 text[24];

                        text[positive_into(text, power)] = end;
                        path_join(relative, sizeof(relative), directory, "power_now");
                        sensors_put(root, relative, text);
                }
                else if (how == 1)
                {
                        positive current = 100000 + sensors_next(3000000);
                        positive voltage = 7000000 + sensors_next(9000000);
                        p8 text[24];

                        text[positive_into(text, current)] = end;
                        path_join(relative, sizeof(relative), directory, "current_now");
                        sensors_put(root, relative, text);
                        text[positive_into(text, voltage)] = end;
                        path_join(relative, sizeof(relative), directory, "voltage_now");
                        sensors_put(root, relative, text);
                        power = current * voltage / 1000000;
                }

                //      A Device battery is not added; one whose capacity is
                //      not a number is not found at all.
                if (device || !capacity.number || !model_room())
                        continue;
                if (capacity.value < 0 || capacity.value > 100)
                        continue;

                model_item(model_power, "battery");
                model_number(model_power, (positive)capacity.value);
                model_append(model_power, "%");
                if (has_status)
                        model_append(model_power, string_equals(status, "Discharging\n")
                                                      ? " discharging"
                                                      : " not charging");
                if (power)
                {
                        model_append(model_power, " ");
                        model_watts(model_power, power);
                }
        }
}

static fn sensors_grammar(void)
{
        positive wrong = 0;
        positive clocks_wrong = 0;
        positive cases = 600;

        for (positive round = 0; round < cases; round++)
        {
                p8 root[128];
                static monitor_hardware hardware;
                static system_snapshot sample;
                static struct snapshot_cpu cpus[9];
                p8 have_cpu = false;

                sensors_file_name(root, (string_address)sensors_base, round, "");
                sensors_mkdir_parents(root);
                model_temp[0] = end;
                model_power[0] = end;
                model_count = 0;

                //      Chips numbered out of order and sparse, sometimes
                //      past ten so hwmon10 must come after hwmon9.
                positive chips = sensors_next(7);
                positive used_numbers = 0;

                for (positive chip = 0; chip < chips; chip++)
                {
                        used_numbers += 1 + sensors_next(6);
                        sensors_chip(root, used_numbers, address_of have_cpu);
                }

                sensors_supplies(root);

                //      Processors: the aggregate and up to eight, each with a
                //      clock or not.
                positive processors = sensors_next(9);
                positive sum = 0;
                positive known = 0;
                positive want_khz[8];

                cpus[0].id = ~0u;
                for (positive at = 0; at < processors; at++)
                {
                        sensors_value value;
                        p8 relative[128];

                        cpus[at + 1].id = (unsigned int)at;
                        sensors_draw(address_of value, 400000, 5800000);
                        want_khz[at] = 0;
                        if (!sensors_next(4))
                                continue;
                        sensors_file_name(relative, "devices/system/cpu/cpu", at,
                                          "/cpufreq/scaling_cur_freq");
                        sensors_put(root, relative, value.text);
                        if (value.number && value.value > 0 && value.value < 20000000)
                        {
                                want_khz[at] = (positive)value.value;
                                sum += want_khz[at];
                                known++;
                        }
                }

                memory_fill(address_of hardware, 0, sizeof(hardware));
                memory_fill(address_of sample, 0, sizeof(sample));
                sample.cpus = cpus;
                sample.header.cpu_count = (unsigned int)(processors + 1);
                hardware.root = root;
                monitor_hardware_read(address_of hardware, address_of sample,
                                      SYSTEM_NANOSECONDS);
                sensors_rows(address_of hardware);

                p8 want[4200];

                want[0] = end;
                if (model_temp[0])
                {
                        string_copy_bounded(want, model_temp, sizeof(want));
                        model_append(want, "|");
                }
                if (model_power[0])
                {
                        positive length = string_length(want);

                        string_copy_bounded(want + length, model_power,
                                            sizeof(want) - length);
                        length = string_length(want);
                        string_copy_bounded(want + length, "|", sizeof(want) - length);
                }

                checks++;
                if (!string_equals(sensors_out, want))
                {
                        failures++;
                        if (wrong++ < 5)
                                string_format(log, "  FAIL sensors tree %p: got [%s] want [%s]\n",
                                              round, sensors_out, want);
                }

                p8 clocks_right = hardware.clocks == known &&
                                    hardware.khz_average == (known ? sum / known : 0);

                for (positive at = 0; at < processors; at++)
                        clocks_right = clocks_right && hardware.khz[at] == want_khz[at];

                checks++;
                if (!clocks_right)
                {
                        failures++;
                        if (clocks_wrong++ < 5)
                                string_format(log, "  FAIL sensors clocks %p: %p known of %p, average %p of %p\n",
                                              round, hardware.clocks, known,
                                              hardware.khz_average, known ? sum / known : 0);
                }

                sensors_remove(root);
        }
}

static fn sensors_moments(void)
{
        static monitor_hardware hardware;
        p8 root[128];
        positive second = SYSTEM_NANOSECONDS;

        sensors_file_name(root, (string_address)sensors_base, 9999, "");
        sensors_mkdir_parents(root);

        //      Nothing at all: no rows and no clock column.
        memory_fill(address_of hardware, 0, sizeof(hardware));
        hardware.root = root;
        monitor_hardware_read(address_of hardware, null, second);
        sensors_rows(address_of hardware);
        sensors_expect("an empty tree draws no rows", sensors_out[0] == end);
        sensors_expect("and has no clock", hardware.clocks == 0);

        //      A drive's temperature is held between reads five seconds apart.
        sensors_put(root, "class/hwmon/hwmon0/name", "nvme\n");
        sensors_put(root, "class/hwmon/hwmon0/temp1_input", "41850\n");
        sensors_put(root, "class/hwmon/hwmon1/name", "k10temp\n");
        sensors_put(root, "class/hwmon/hwmon1/temp1_input", "55000\n");
        hardware.found = false;
        monitor_hardware_read(address_of hardware, null, 2 * second);
        sensors_rows(address_of hardware);
        sensors_expect("a drive and the processor are read",
              string_equals(sensors_out, " temp   nvme 42\xc2\xb0" "C  cpu 55\xc2\xb0" "C|"));
        sensors_put(root, "class/hwmon/hwmon0/temp1_input", "60000\n");
        sensors_put(root, "class/hwmon/hwmon1/temp1_input", "70000\n");
        monitor_hardware_read(address_of hardware, null, 3 * second);
        sensors_rows(address_of hardware);
        sensors_expect("the drive is held and the processor read again",
              string_equals(sensors_out, " temp   nvme 42\xc2\xb0" "C  cpu 70\xc2\xb0" "C|"));
        monitor_hardware_read(address_of hardware, null, 7 * second + 1);
        sensors_rows(address_of hardware);
        sensors_expect("until five seconds have gone",
              string_equals(sensors_out, " temp   nvme 60\xc2\xb0" "C  cpu 70\xc2\xb0" "C|"));

        //      The drive unplugged: gone at once, not drawn from its last
        //      reading, even though it is a slow one.
        p8 gone[160];

        path_join(gone, sizeof(gone), root, "class/hwmon/hwmon0");
        sensors_remove(gone);
        monitor_hardware_read(address_of hardware, null, 13 * second);
        sensors_rows(address_of hardware);
        sensors_expect("a chip that went is not drawn",
              string_equals(sensors_out, " temp   cpu 70\xc2\xb0" "C|"));
        sensors_expect("and the chips are found again", hardware.found == false);

        //      A chip that arrived is found with the next search, the one a
        //      failed read asked for.
        sensors_put(root, "class/hwmon/hwmon7/name", "amdgpu\n");
        sensors_put(root, "class/hwmon/hwmon7/temp1_input", "48000\n");
        sensors_put(root, "class/hwmon/hwmon7/power1_average", "5150000\n");
        sensors_put(root, "class/hwmon/hwmon7/fan1_input", "1800\n");
        monitor_hardware_read(address_of hardware, null, 14 * second);
        sensors_rows(address_of hardware);
        sensors_expect("a chip that arrived is drawn",
              string_equals(sensors_out, " temp   cpu 70\xc2\xb0" "C  gpu 48\xc2\xb0" "C  fan 1800 rpm| power  gpu 5.2 W|"));

        //      And one that arrives with nothing failing waits for the
        //      thirty-second search.
        sensors_put(root, "class/hwmon/hwmon8/name", "acpitz\n");
        sensors_put(root, "class/hwmon/hwmon8/temp1_input", "50000\n");
        monitor_hardware_read(address_of hardware, null, 20 * second);
        sensors_rows(address_of hardware);
        sensors_expect("a quiet arrival waits for the search",
              string_search_folded(sensors_out, "acpi") == null);
        monitor_hardware_read(address_of hardware, null, 45 * second);
        sensors_rows(address_of hardware);
        sensors_expect("and is drawn after it",
              string_search_folded(sensors_out, "acpi 50\xc2\xb0" "C") != null);

        //      The energy counter: nothing on the first read, watts from
        //      its climb after, and nothing again when it wraps.
        sensors_put(root, "class/powercap/intel-rapl:0/energy_uj", "1000000\n");
        hardware.found = false;
        monitor_hardware_read(address_of hardware, null, 50 * second);
        sensors_rows(address_of hardware);
        sensors_expect("an energy counter read once gives no watts",
              string_search_folded(sensors_out, "power  gpu 5.2 W|") != null);
        sensors_put(root, "class/powercap/intel-rapl:0/energy_uj", "13500000\n");
        monitor_hardware_read(address_of hardware, null, 50 * second + second / 2);
        sensors_rows(address_of hardware);
        sensors_expect("and 12.5 J over half a second is 25 W",
              string_search_folded(sensors_out, "power  gpu 5.2 W  cpu 25.0 W|") != null);
        sensors_put(root, "class/powercap/intel-rapl:0/energy_uj", "20\n");
        monitor_hardware_read(address_of hardware, null, 51 * second);
        sensors_rows(address_of hardware);
        sensors_expect("a wrapped counter gives none that frame",
              string_search_folded(sensors_out, "cpu 25") == null &&
                  string_search_folded(sensors_out, "power  gpu 5.2 W|") != null);

        sensors_remove(root);

        //      More chips than there is room for: sixteen sensors are kept,
        //      in hwmon order, and the row is cut at the screen's width.
        sensors_mkdir_parents(root);
        for (positive at = 0; at < 40; at++)
        {
                p8 relative[96];

                sensors_file_name(relative, "class/hwmon/hwmon", at, "/name");
                sensors_put(root, relative, "nvme\n");
                sensors_file_name(relative, "class/hwmon/hwmon", at, "/temp1_input");
                sensors_put(root, relative, at < 10 ? "30000\n" : "31000\n");
        }
        memory_fill(address_of hardware, 0, sizeof(hardware));
        hardware.root = root;
        monitor_hardware_read(address_of hardware, null, second);
        sensors_expect("forty chips keep sixteen sensors", hardware.count == MONITOR_SENSORS);
        sensors_expect("the lowest sixteen in hwmon order, whatever readdir gave",
              string_search_folded(hardware.sensor[9].path, "/hwmon9/temp1_input") &&
                  string_search_folded(hardware.sensor[10].path, "/hwmon10/temp1_input") &&
                  string_search_folded(hardware.sensor[15].path, "/hwmon15/temp1_input"));
        sensors_rows(address_of hardware);
        {
                positive drawn = 0;

                for (string_address at = sensors_out;
                     (at = string_search_folded(at, "nvme ")); at++)
                        drawn++;
                sensors_expect("and all sixteen are drawn", drawn == MONITOR_SENSORS);
        }
        sensors_remove(root);
}

/*
        Names drawn from the kernel and from a remote peer, an interface's and
        the hostname, show their control bytes as question marks, so a name
        cannot carry an escape sequence to the terminal (one that rewrites the
        title or sets the clipboard).
*/
static p8 sensors_has(p8 address_to bytes, positive length, string_address text)
{
        positive wanted = string_length(text);

        for (positive at = 0; at + wanted <= length; at++)
        {
                positive same = 0;

                while (same < wanted && bytes[at + same] == (p8)text[same])
                        same++;
                if (same == wanted)
                        return true;
        }
        return false;
}

static fn sensors_names(void)
{
        static system_snapshot old;
        static system_snapshot sample;
        static struct snapshot_network networks[1];

        memory_fill(address_of old, 0, sizeof(old));
        memory_fill(address_of sample, 0, sizeof(sample));
        memory_fill(networks, 0, sizeof(networks));
        memory_copy_apart(networks[0].name, "\x1b]0;X\x07", 6);
        networks[0].received = 5000;
        sample.header.network_count = 1;
        sample.networks = networks;

        text_out_used = 0;
        monitor_networks(address_of old, address_of sample, 1, 120, SYSTEM_NANOSECONDS);
        sensors_expect("an interface's escape bytes are not written",
              !sensors_has(text_out_buffer, text_out_used, "\x1b]0;X"));
        sensors_expect("and an interface's control bytes are question marks",
              sensors_has(text_out_buffer, text_out_used, "?]0;X?"));

        text_out_used = 0;
        monitor_header(address_of sample, "\x1b]0;X\x07", 1, 120);
        sensors_expect("a hostname's escape bytes are not written",
              !sensors_has(text_out_buffer, text_out_used, "\x1b]0;X"));
        sensors_expect("and a hostname's control bytes are question marks",
              sensors_has(text_out_buffer, text_out_used, "?]0;X?"));
        text_out_used = 0;
}

b32 main(void)
{
        p8 digits[24];

        digits[positive_into(digits, (positive)system_call_1(syscall(getpid), 0))] = end;
        string_copy_bounded(sensors_base, "/tmp/dawning-sensors.", sizeof(sensors_base));
        model_append(sensors_base, digits);
        model_append(sensors_base, "/t");
        sensors_grammar();
        sensors_moments();
        sensors_names();
        {
                p8 top[96];

                path_head_copy(top, sizeof(top), sensors_base);
                sensors_remove(top);
        }
        return test_report(null);
}
#endif /* CHECK_sensors */

#ifdef CHECK_browser
/*
        The file browser's model (src/canvas/ui/files_model.c), with no window: folders made here, under /tmp, and the model run
        on them as the view runs it. A listing holds 12,000 entries in this build, so that one cut short is a few thousand files
        and not sixty-five thousand; nothing else is changed.
*/
#define FILES_MOST 12000u
#define FILES_KEPT_BYTES (256u << 10) // and a quarter of a megabyte of kept listings, so that the bound is met by folders of a few thousand files
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 browser_base[96];

static fn browser_path(p8 address_to into, string_address relative)
{
        string_copy_bounded(into, browser_base, FILES_PATH);
        if (relative[0])
        {
                string_append_bounded(into, (string_address) "/", FILES_PATH);
                string_append_bounded(into, relative, FILES_PATH);
        }
}

static fn browser_folder(string_address relative)
{
        p8 path[FILES_PATH];

        browser_path(path, relative);
        system_call_3(syscall(mkdirat), AT_FDCWD, (positive)path, 0755);
}

// A file of so many bytes, changed at a moment (0: now).
static fn browser_file(string_address relative, positive bytes, b64 seconds)
{
        static const p8 fill[4096];
        p8 path[FILES_PATH];
        bipolar made;

        browser_path(path, relative);
        made = (bipolar)system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);
        if (made < 0)
                return;
        for (positive left = bytes; left;)
        {
                positive part = left < sizeof(fill) ? left : sizeof(fill);

                system_call_3(syscall(write), made, (positive)fill, part);
                left -= part;
        }
        if (seconds)
        {
                b64 times[4] = {seconds, 0, seconds, 0};

                system_call_4(syscall(utimensat), made, 0, (positive)times, 0);
        }
        system_call_1(syscall(close), made);
}

static fn browser_link(string_address target, string_address relative)
{
        p8 path[FILES_PATH];

        browser_path(path, relative);
        system_call_3(syscall(symlinkat), (positive)target, AT_FDCWD, (positive)path);
}

// A name taken away, whatever it is; a folder with all that is in it. Nothing is followed: a link is removed, not what it names.
static fn browser_remove(string_address path)
{
        file_walk walk;
        file_facts facts;
        struct linux_dirent64 address_to entry;

        if ((bipolar)system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, 0) >= 0)
                return;
        if (file_look_code(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) < 0 || (facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
                return;
        system_call_3(syscall(fchmodat), AT_FDCWD, (positive)path, 0755);
        if (!file_walk_open(address_of walk, AT_FDCWD, path))
                return;
        while ((entry = file_walk_next(address_of walk)))
        {
                p8 inner[FILES_PATH];

                if (file_is_dot(entry->d_name))
                        continue;
                string_copy_bounded(inner, path, FILES_PATH);
                string_append_bounded(inner, (string_address) "/", FILES_PATH);
                string_append_bounded(inner, entry->d_name, FILES_PATH);
                browser_remove(inner);
        }
        file_walk_close(address_of walk);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)path, AT_REMOVEDIR);
}

static fn browser_remove_in(string_address relative)
{
        p8 path[FILES_PATH];

        browser_path(path, relative);
        browser_remove(path);
}

// The reading under way taken to its end, so many entries a step. The steps it took are counted.
static positive browser_read(struct files_model address_to m, positive step)
{
        positive steps = 1;

        while (files_model_step(m, step))
                steps++;
        return steps;
}

static b32 browser_go(struct files_model address_to m, string_address relative)
{
        p8 path[FILES_PATH];

        browser_path(path, relative);
        if (!files_model_go(m, path))
                return false;
        browser_read(m, 64);
        return true;
}

// The names shown, in their order, a blank after each: what a row of the checks compares.
static string_address browser_names(const struct files_model address_to m)
{
        static p8 text[4096];
        positive used = 0;

        for (p32 place = 0; place < files_model_count(m); place++)
        {
                const struct files_entry address_to entry = files_model_entry(m, place);

                if (used + entry->length + 2 > sizeof(text))
                        break;
                memory_copy(text + used, files_model_name(m, entry), entry->length);
                used += entry->length;
                text[used++] = ' ';
        }
        text[used] = end;
        return text;
}

static b32 browser_is(const struct files_model address_to m, string_address want)
{
        b32 same = string_equals(browser_names(m), want);

        if (!same)
                string_format(log, "    shown: [%s] wanted: [%s]\n", browser_names(m), want);
        return same;
}

static p8 files_git_check_state[FILES_MOST];

static p32 browser_at(const struct files_model address_to m, const char address_to name)
{
        return files_model_find(m, (const p8 address_to)name);
}

// A relative name with a number in it, and after that, when asked, more words and another number: kept/big2/file-17.
static fn browser_numbered(p8 address_to into, const char address_to before, int number, const char address_to after, int second)
{
        positive used;

        string_copy_bounded(into, (string_address)before, FILES_PATH);
        used = string_length(into);
        into[used + positive_into(into + used, (positive)number)] = end;
        if (!after)
                return;
        string_append_bounded(into, (string_address)after, FILES_PATH);
        used = string_length(into);
        into[used + positive_into(into + used, (positive)second)] = end;
}

static fn browser_wait_a_tick(void)
{
        b64 pause[2] = {0, 30000000};

        system_call_2(syscall(nanosleep), (positive)pause, 0);
}

static fn browser_order(struct files_model address_to m)
{
        b32 chosen_kept, cursor_kept;
        p32 big;

        browser_folder((string_address) "order");
        browser_folder((string_address) "order/zeta");
        browser_folder((string_address) "order/Alpha");
        browser_file((string_address) "order/b.txt", 300, 1700000300);
        browser_file((string_address) "order/A.txt", 100, 1700000200);
        browser_file((string_address) "order/a.txt", 200, 1700000100);
        browser_file((string_address) "order/c", 50, 1700000400);
        browser_file((string_address) "order/.hidden", 1, 0);
        browser_go(m, (string_address) "order");
        check("browser: a folder is listed with its folders first, then by name with capitals among the small letters, and a name with a dot before it is not listed",
              m->state == FILES_READY && browser_is(m, (string_address) "Alpha zeta A.txt a.txt b.txt c "));
        check("browser: an entry has its kind, its size and its time, as the disk has them",
              files_model_entry(m, 0)->kind == FILES_KIND_DIRECTORY && files_model_entry(m, 4)->kind == FILES_KIND_FILE && files_model_entry(m, 4)->size == 300 &&
                  files_model_entry(m, 4)->modified == 1700000300 && files_model_entry(m, 4)->looked);
        // Chosen and under the cursor, then another order: the same entries are chosen and the cursor is on the same one.
        big = browser_at(m, "b.txt");
        files_model_choose(m, browser_at(m, "c"), FILES_CHOOSE_ONLY);
        files_model_choose(m, big, FILES_CHOOSE_TOGGLE);
        files_model_sort(m, FILES_SORT_SIZE, false);
        check("browser: by size the folders are still first and the files go from the smallest", browser_is(m, (string_address) "Alpha zeta c A.txt a.txt b.txt "));
        chosen_kept = m->chosen_count == 2 && files_model_chosen(m, browser_at(m, "b.txt")) && files_model_chosen(m, browser_at(m, "c")) &&
                      !files_model_chosen(m, browser_at(m, "a.txt"));
        cursor_kept = m->cursor == browser_at(m, "b.txt");
        check("browser: another order keeps what is chosen chosen and the cursor on its entry", chosen_kept && cursor_kept);
        files_model_sort(m, FILES_SORT_SIZE, true);
        check("browser: descending turns the files over and leaves the folders first", browser_is(m, (string_address) "zeta Alpha b.txt a.txt A.txt c "));
        files_model_sort(m, FILES_SORT_TIME, false);
        check("browser: by time the files go from the oldest", browser_is(m, (string_address) "Alpha zeta a.txt A.txt b.txt c "));
        files_model_sort(m, FILES_SORT_NAME, false);
        // A range from the anchor, and a move held at the ends.
        files_model_choose(m, 1, FILES_CHOOSE_ONLY);
        files_model_choose(m, 4, FILES_CHOOSE_RANGE);
        check("browser: a range is chosen from the entry last chosen alone to the one asked, and no other",
              m->chosen_count == 4 && !files_model_chosen(m, 0) && files_model_chosen(m, 1) && files_model_chosen(m, 4) && !files_model_chosen(m, 5) && m->cursor == 4);
        check("browser: the cursor and the chosen set are kept between a range and the next key (moves are the kit list's, its rows test them)",
              m->cursor == 4 && m->chosen_count == 4);
        m->hidden = true;
        files_model_reload(m);
        browser_read(m, 64);
        check("browser: with hidden names asked for, the name with a dot before it is listed too", browser_is(m, (string_address) "Alpha zeta .hidden A.txt a.txt b.txt c "));
        m->hidden = false;
}

static fn browser_states(struct files_model address_to m)
{
        p8 before[FILES_PATH];
        bipolar child;

        browser_folder((string_address) "empty");
        browser_go(m, (string_address) "empty");
        check("browser: an empty folder is read whole and has no entries, which is no error", m->state == FILES_READY && files_model_count(m) == 0 && m->cursor == FILES_NO);
        browser_go(m, (string_address) "order/b.txt");
        check("browser: a path that names a file is not a folder, said as a state", m->state == FILES_NOT_DIRECTORY && files_model_count(m) == 0);
        browser_go(m, (string_address) "nowhere");
        check("browser: a path that names nothing is gone, said as a state", m->state == FILES_GONE && files_model_count(m) == 0);
        string_copy_bounded(before, m->path, FILES_PATH);
        check("browser: a path that is not absolute is refused and the folder shown stays",
              !files_model_go(m, (string_address) "order") && !files_model_go(m, (string_address) "") && string_equals(m->path, before));
        check("browser: the kernel's refusals are the states a view says: may not be read, gone, not a folder, and any other with its code",
              files_state_of(-13) == FILES_UNREADABLE && files_state_of(-1) == FILES_UNREADABLE && files_state_of(-2) == FILES_GONE &&
                  files_state_of(-20) == FILES_NOT_DIRECTORY && files_state_of(-40) == FILES_NOT_DIRECTORY && files_state_of(-5) == FILES_FAILED);
        // A folder with no permission at all, read as a user who is not its owner (root reads anything, so a child gives root up).
        browser_folder((string_address) "locked");
        browser_file((string_address) "locked/secret", 10, 0);
        {
                p8 path[FILES_PATH];

                browser_path(path, (string_address) "locked");
                system_call_3(syscall(fchmodat), AT_FDCWD, (positive)path, 0);
        }
        child = (bipolar)system_fork();
        if (child == 0)
        {
                int answer = 2;
                b32 root = (bipolar)system_call_1(syscall(getuid), 0) == 0;

                if (!root || ((bipolar)system_call_3(syscall(setresgid), 65534, 65534, 65534) >= 0 &&
                              (bipolar)system_call_3(syscall(setresuid), 65534, 65534, 65534) >= 0))
                {
                        browser_go(m, (string_address) "locked");
                        answer = m->state == FILES_UNREADABLE && files_model_count(m) == 0 ? 0 : 1;
                }
                system_call_1(syscall(exit_group), (positive)answer);
        }
        else if (child > 0)
        {
                b32 raw = 0;

                system_wait4_retry(child, address_of raw, 0, null);
                if ((raw & 0x7f) == 0 && (raw >> 8 & 0xff) == 2)
                        log_direct(str("  browser: a folder that may not be read, read by a user who is not root: NOT RUN (this user cannot become another)\n"));
                else
                        check("browser: a folder that may not be read is said to be so, as a state, with no entries", (raw & 0x7f) == 0 && (raw >> 8 & 0xff) == 0);
        }
        else
                log_direct(str("  browser: a folder that may not be read: NOT RUN (no child could be started)\n"));
}

static fn browser_names_hostile(struct files_model address_to m)
{
        static const p8 newline[] = "line\nbreak\x1b[31m", turned[] = "evil\xe2\x80\xaetxt.exe", broken[] = "bad\xff\xfename", tab[] = "tab\there\x7f";
        static char shown[FILES_NAME * 3 + 4];
        const p8 address_to const each[] = {newline, turned, broken, tab};
        b32 kept, clean = true, pictured, replaced, marked;

        browser_folder((string_address) "names");
        for (int at = 0; at < 4; at++)
        {
                p8 path[FILES_PATH];

                browser_path(path, (string_address) "names");
                string_append_bounded(path, (string_address) "/", FILES_PATH);
                string_append_bounded(path, (string_address)each[at], FILES_PATH);
                system_call_1(syscall(close), system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_CREATE | FILE_WRITE, 0644));
        }
        browser_go(m, (string_address) "names");
        kept = files_model_count(m) == 4 && files_model_find(m, newline) != FILES_NO && files_model_find(m, turned) != FILES_NO &&
               files_model_find(m, broken) != FILES_NO && files_model_find(m, tab) != FILES_NO;
        check("browser: a name with a newline, an escape, a tab, bytes that are not UTF-8 and an override is kept as its bytes are, and found by them", kept);
        if (!kept)
                return;
        // As a row writes them: no control byte, no escape, no override, and nothing that is not UTF-8.
        for (p32 place = 0; place < files_model_count(m); place++)
        {
                positive used = files_name_text(shown, sizeof(shown), m, files_model_entry(m, place));

                for (positive at = 0; at < used;)
                {
                        p32 code, size = doc_decode((const p8 address_to)shown + at, used - at, &code);

                        clean = clean && !(code & DOC_INVALID) && code >= 0x20 && code != 0x7f && !(code >= 0x80 && code < 0xa0) && code != 0x202e;
                        at += size;
                }
        }
        check("browser: a row's words have no control byte, no escape, no override and no byte that is not UTF-8, whatever the name has", clean);
        files_name_text(shown, sizeof(shown), m, files_model_entry(m, files_model_find(m, newline)));
        pictured = string_equals((string_address)shown, (string_address) "line\xe2\x90\x8a" "break\xe2\x90\x9b[31m");
        files_name_text(shown, sizeof(shown), m, files_model_entry(m, files_model_find(m, tab)));
        pictured = pictured && string_equals((string_address)shown, (string_address) "tab\xe2\x90\x89here\xe2\x90\xa1");
        check("browser: a control byte is drawn as its picture where it stands: a newline, an escape, a tab and a delete are seen and move nothing", pictured);
        files_name_text(shown, sizeof(shown), m, files_model_entry(m, files_model_find(m, broken)));
        replaced = string_equals((string_address)shown, (string_address) "bad\xef\xbf\xbd\xef\xbf\xbdname");
        files_name_text(shown, sizeof(shown), m, files_model_entry(m, files_model_find(m, turned)));
        marked = string_equals((string_address)shown, (string_address) "evil\xef\xbf\xbdtxt.exe");
        check("browser: bytes that are not UTF-8 are a replacement mark each, and an override is one too: the name that ends txt.exe is drawn ending txt.exe", replaced && marked);
        {
                static const p8 marks[] = "a\xe2\x80\x8e" "b\xe2\x81\xa6" "c\xe2\x80\xa8" "d\xef\xbb\xbf" "e\xc2\x85" "f";
                char into[64], tight[6];

                ui_label_foreign(into, sizeof(into), marks, sizeof(marks) - 1);
                check("browser: the kit's rule for words from outside replaces every mark that orders or hides letters: a direction mark, an isolate, a line separator, a byte order mark, a C1 control",
                      string_equals((string_address)into, (string_address) "a\xef\xbf\xbd" "b\xef\xbf\xbd" "c\xef\xbf\xbd" "d\xef\xbf\xbd" "e\xef\xbf\xbd" "f"));
                check("browser: and it writes no half of a character where the room ends, and always an end",
                      ui_label_foreign(tight, sizeof(tight), (const p8 address_to) "ab\n\ncd", 6) == 5 && string_equals((string_address)tight, (string_address) "ab\xe2\x90\x8a"));
        }
}

static fn browser_links(struct files_model address_to m)
{
        p8 target[FILES_PATH], before[FILES_PATH];
        const struct files_entry address_to entry;
        p32 place;

        browser_folder((string_address) "links");
        browser_folder((string_address) "links/real");
        browser_file((string_address) "links/real/inside", 5, 0);
        browser_file((string_address) "links/file", 1000, 0);
        browser_path(target, (string_address) "links/real");
        browser_link(target, (string_address) "links/to-folder");
        browser_link((string_address) "file", (string_address) "links/to-file");
        browser_link((string_address) "/nowhere/at/all", (string_address) "links/dangling");
        browser_go(m, (string_address) "links");
        place = browser_at(m, "to-folder");
        entry = files_model_entry(m, place);
        check("browser: a link is listed as a link, with its own size, whether it names a folder, a file or nothing: none is followed",
              files_model_count(m) == 5 && entry && entry->kind == FILES_KIND_LINK && entry->size == string_length(target) &&
                  files_model_entry(m, browser_at(m, "to-file"))->kind == FILES_KIND_LINK && files_model_entry(m, browser_at(m, "to-file"))->size == 4 &&
                  files_model_entry(m, browser_at(m, "dangling"))->kind == FILES_KIND_LINK);
        string_copy_bounded(before, m->path, FILES_PATH);
        check("browser: a link to a folder is not entered: the folder shown stays, whole",
              files_model_enter(m, place) == FILES_REFUSED && string_equals(m->path, before) && m->state == FILES_READY && files_model_count(m) == 5);
        check("browser: a file is not entered either, and a place past the listing is none",
              files_model_enter(m, browser_at(m, "file")) == FILES_REFUSED && files_model_enter(m, 99) == FILES_REFUSED && string_equals(m->path, before));
}

static fn browser_history(struct files_model address_to m)
{
        p8 root[FILES_PATH], sub[FILES_PATH], deep[FILES_PATH];
        b32 entered, up, back, forward, forgot, bounded = true;
        p32 backs;

        browser_folder((string_address) "way");
        browser_folder((string_address) "way/a");
        browser_folder((string_address) "way/sub");
        browser_folder((string_address) "way/sub/deep");
        browser_folder((string_address) "way/z");
        browser_path(root, (string_address) "way"), browser_path(sub, (string_address) "way/sub"), browser_path(deep, (string_address) "way/sub/deep");
        browser_go(m, (string_address) "way");
        backs = m->back_count;
        entered = files_model_enter(m, browser_at(m, "sub")) == FILES_SAME && string_equals(m->path, sub) && m->state == FILES_READING;
        browser_read(m, 64);
        entered = entered && m->state == FILES_READY && browser_is(m, (string_address) "deep ") && m->back_count == backs + 1;
        check("browser: a folder of the listing is entered: it is the folder shown, read in steps, and the one come from is on the way back", entered);
        up = files_model_up(m) && string_equals(m->path, root);
        browser_read(m, 64);
        up = up && browser_is(m, (string_address) "a sub z ") && m->cursor == 1;
        check("browser: up shows the folder this one is in, with the cursor on the folder come out of", up);
        back = files_model_back(m, false) && string_equals(m->path, sub);
        browser_read(m, 64);
        back = back && m->forward_count == 1 && files_model_back(m, false) && string_equals(m->path, root);
        browser_read(m, 64);
        check("browser: back shows the folder shown before, and again the one before that, the cursor on the folder come out of", back && m->cursor == 1);
        forward = files_model_back(m, true) && string_equals(m->path, sub);
        browser_read(m, 64);
        forward = forward && files_model_back(m, true) && string_equals(m->path, root) && !files_model_back(m, true);
        browser_read(m, 64);
        check("browser: forward shows the folder gone back from, as far as there were, and no further", forward);
        files_model_back(m, false);
        browser_read(m, 64);
        forgot = m->forward_count == 1 && files_model_go(m, deep) && m->forward_count == 0 && !files_model_back(m, true);
        browser_read(m, 64);
        check("browser: a folder gone to forgets the way forward", forgot && string_equals(m->path, deep));
        for (int at = 0; at < 3 * FILES_HISTORY; at++)
        {
                files_model_go(m, at & 1 ? sub : root);
                bounded = bounded && m->back_count <= FILES_HISTORY;
        }
        browser_read(m, 64);
        check("browser: the way back holds its last folders and no more, the oldest let go",
              bounded && m->back_count == FILES_HISTORY && string_equals(m->back + (positive)(FILES_HISTORY - 1) * FILES_PATH, root));
        files_model_go(m, (string_address) "/");
        browser_read(m, 64);
        check("browser: the root has no folder to go up to", !files_model_up(m) && string_equals(m->path, (string_address) "/"));
}

/*
        A path that is typed (files_model_try, files_model_complete) and the crumbs a path is shown as (files_crumbs_of). A typed path
        that is a folder is the folder shown, with the one before it on the way back; one that names nothing, names a file, or is no
        absolute path is refused with its reason and the folder shown stays, whole, the way back as it was; a link that was typed is
        followed, which a link of a listing never is. A name being typed is finished from the folders of the listing shown: to the
        whole name when one starts so, to what they share when several do, never to a file's. And a path is its folders in order,
        each with the path that goes to it.
*/
static fn browser_typed(struct files_model address_to m)
{
        p8 root[FILES_PATH], real[FILES_PATH], through[FILES_PATH], before[FILES_PATH], missing[FILES_PATH], file[FILES_PATH], into[FILES_NAME], longest[FILES_PATH + 64];
        p32 backs, count;
        b32 went, kept, linked, finished, crumbs;

        browser_folder((string_address) "typed");
        browser_folder((string_address) "typed/alpha");
        browser_folder((string_address) "typed/alphabet");
        browser_folder((string_address) "typed/beta");
        browser_file((string_address) "typed/alpine", 1, 0);
        browser_file((string_address) "typed/gamma", 1, 0);
        browser_folder((string_address) "typed/alpha/inside");
        browser_path(root, (string_address) "typed"), browser_path(real, (string_address) "typed/alpha");
        browser_path(through, (string_address) "typed/way-in"), browser_path(missing, (string_address) "typed/nothing"), browser_path(file, (string_address) "typed/gamma");
        browser_link(real, (string_address) "typed/way-in");
        browser_go(m, (string_address) "typed");
        backs = m->back_count;
        string_copy_bounded(before, m->path, FILES_PATH);
        memory_fill(longest, 'a', sizeof(longest) - 1), longest[0] = '/', longest[sizeof(longest) - 1] = end;
        kept = files_model_try(m, missing) == FILES_GONE && files_model_try(m, file) == FILES_NOT_DIRECTORY &&
               files_model_try(m, (string_address) "typed/alpha") == FILES_NOT_DIRECTORY && files_model_try(m, (string_address) "") == FILES_NOT_DIRECTORY &&
               files_model_try(m, longest) == FILES_NOT_DIRECTORY && string_equals(m->path, before) && m->state == FILES_READY && m->back_count == backs &&
               files_model_count(m) == 6;
        check("browser: a typed path that names nothing, names a file, is not absolute or is longer than a path is refused with its reason, and the folder shown stays whole with the way back as it was",
              kept);
        // Finishing a name from the listing shown: folders only.
        count = files_model_complete(m, (const p8 address_to) "b", 1, into, sizeof(into));
        finished = count == 1 && string_equals(into, (string_address) "beta");
        count = files_model_complete(m, (const p8 address_to) "al", 2, into, sizeof(into));
        finished = finished && count == 2 && string_equals(into, (string_address) "alpha"); // alpha and alphabet: alpine is a file
        count = files_model_complete(m, (const p8 address_to) "g", 1, into, sizeof(into));
        finished = finished && count == 0 && string_equals(into, (string_address) "g");
        count = files_model_complete(m, (const p8 address_to) "", 0, into, sizeof(into));
        finished = finished && count == 3 && string_equals(into, (string_address) ""); // the three folders: the link and the files are none
        check("browser: a name being typed is finished from the folders listed: the whole name when one starts so, what they share when several do, and never a file's",
              finished);
        went = files_model_try(m, real) == FILES_READING && string_equals(m->path, real) && m->back_count &&
               string_equals(m->back + (positive)(m->back_count - 1) * FILES_PATH, before);
        browser_read(m, 64);
        went = went && browser_is(m, (string_address) "inside ") && files_model_back(m, false) && string_equals(m->path, before);
        browser_read(m, 64);
        check("browser: a typed path that is a folder is the folder shown, read in steps, and the one shown before it is on the way back", went);
        // The link in the listing is not entered; the same link typed is followed, and the path shown is what was typed.
        linked = files_model_enter(m, browser_at(m, "way-in")) == FILES_REFUSED && files_model_try(m, through) == FILES_READING && string_equals(m->path, through);
        browser_read(m, 64);
        check("browser: a link that was typed is followed, as naming it asks, where the same link in a listing is not entered", linked && browser_is(m, (string_address) "inside "));
        {
                static struct files_trail trail;
                p8 part[FILES_PATH];

                string_copy_bounded(trail.path, (string_address) "/usr/share/fonts", FILES_PATH);
                files_trail_split(&trail);
                files_trail_path(&trail, 2, part);
                crumbs = trail.count == 4 && trail.to[0] == 1 && trail.from[1] == 1 && trail.to[1] == 4 && trail.from[2] == 5 && trail.to[2] == 10 &&
                         trail.from[3] == 11 && trail.to[3] == 16 && string_equals(part, (string_address) "/usr/share");
                string_copy_bounded(trail.path, (string_address) "/", FILES_PATH);
                files_trail_split(&trail);
                crumbs = crumbs && trail.count == 1 && trail.to[0] == 1;
                string_copy_bounded(trail.path, (string_address) "relative", FILES_PATH);
                files_trail_split(&trail);
                crumbs = crumbs && trail.count == 0;
        }
        check("browser: a path is its folders in order, the root first, each with the path that goes to it", crumbs);
}

/*
        The trail (files_trail): the deepest path of the branch a person is on, and where on it they are. A folder above the one shown
        moves the place and keeps the deeper folders; so does the next deeper one; a folder off the trail makes the trail its path,
        keeping what the folders they share were; a deeper folder is the folder it was only while its path, not followed, is a folder
        of the same device and inode: removed it is missing, and a link or another folder in its place is changed; and a cut drops a
        folder and all after it, never the one shown.
*/
static fn browser_trail(struct files_model address_to m)
{
        static struct files_trail trail;
        p8 a[FILES_PATH], b[FILES_PATH], c[FILES_PATH], x[FILES_PATH], part[FILES_PATH], aside[FILES_PATH];
        b32 grown, kept, along, branched, same, missing, linked, replaced, cut;
        p64 inode_a;

        browser_folder((string_address) "trail");
        browser_folder((string_address) "trail/b");
        browser_folder((string_address) "trail/b/c");
        browser_folder((string_address) "trail/x");
        browser_path(a, (string_address) "trail"), browser_path(b, (string_address) "trail/b"), browser_path(c, (string_address) "trail/b/c");
        browser_path(x, (string_address) "trail/x"), browser_path(aside, (string_address) "trail/c-aside");
        memory_fill(&trail, 0, sizeof(trail));
        // Down the branch, a folder at a time, as a window goes: each is noted with what it is.
        browser_go(m, (string_address) "trail");
        files_trail_note(&trail, m->path, &m->folder);
        inode_a = m->folder.inode;
        browser_go(m, (string_address) "trail/b");
        files_trail_note(&trail, m->path, &m->folder);
        browser_go(m, (string_address) "trail/b/c");
        grown = !files_trail_note(&trail, m->path, &m->folder) && string_equals(trail.path, c) && trail.current == trail.count - 1 && trail.count >= 4 &&
                trail.known[trail.count - 1] && trail.known[trail.count - 3] && trail.inode[trail.count - 3] == inode_a;
        check("browser: a trail grows with the folders gone down into, the folder shown its last, and each folder shown is remembered by what it is", grown);
        // Up two folders: the place moves, the trail and its deeper folders stay.
        browser_go(m, (string_address) "trail");
        kept = files_trail_note(&trail, m->path, &m->folder) && string_equals(trail.path, c) && trail.current == trail.count - 3;
        files_trail_path(&trail, trail.current, part);
        kept = kept && string_equals(part, a);
        check("browser: going to a folder above the one shown moves the place on the trail and keeps the deeper folders where they were", kept);
        // The next deeper one is opened: still the same trail.
        browser_go(m, (string_address) "trail/b");
        along = files_trail_note(&trail, m->path, &m->folder) && string_equals(trail.path, c) && trail.current == trail.count - 2;
        check("browser: opening the folder that is the next one of the trail moves along it and changes nothing of it", along);
        same = files_trail_check(&trail, trail.count - 1) == FILES_SAME && files_trail_check(&trail, trail.current) == FILES_SAME &&
               files_trail_check(&trail, trail.count) == FILES_REFUSED;
        // The deeper folder moved aside and a link to another folder given its name; then another folder; then nothing.
        system_call_5(syscall(renameat2), AT_FDCWD, (positive)c, AT_FDCWD, (positive)aside, 0);
        browser_link(x, (string_address) "trail/b/c");
        linked = files_trail_check(&trail, trail.count - 1) == FILES_CHANGED;
        browser_remove_in((string_address) "trail/b/c");
        browser_folder((string_address) "trail/b/c");
        replaced = files_trail_check(&trail, trail.count - 1) == FILES_CHANGED;
        browser_remove_in((string_address) "trail/b/c");
        missing = files_trail_check(&trail, trail.count - 1) == FILES_MISSING;
        check("browser: a deeper folder of the trail is the folder it was while its path is a folder of the same device and inode", same);
        check("browser: a deeper folder that was removed is missing, and one with a link or another folder in its place is changed: neither is the folder it was, and a link is not followed",
              missing && linked && replaced);
        {
                int was = trail.count, at = trail.current;

                files_trail_cut(&trail, trail.current);
                cut = trail.count == was;
                files_trail_cut(&trail, trail.count - 1);
                cut = cut && trail.count == was - 1 && trail.current == at && string_equals(trail.path, b);
        }
        check("browser: a cut drops a folder of the trail and all after it, and never the folder shown", cut);
        // Another branch from the folder above: the trail is that folder's path, and the folders they share are still known.
        browser_go(m, (string_address) "trail");
        files_trail_note(&trail, m->path, &m->folder);
        browser_go(m, (string_address) "trail/x");
        branched = !files_trail_note(&trail, m->path, &m->folder) && string_equals(trail.path, x) && trail.current == trail.count - 1 &&
                   trail.known[trail.count - 2] && trail.inode[trail.count - 2] == inode_a;
        files_trail_path(&trail, trail.count - 2, part);
        check("browser: opening a folder that is not on the trail makes the trail its path: what was deeper than the fork is dropped, the new folder is the end, and the folders shared are still known",
              branched && string_equals(part, a));
}

/*
        Kept listings. A folder that was read whole and left is shown again as it was left, with no reading: its entries, the place
        the keys were on, what was chosen and where the view had its rows; the look for staleness then says whether it changed while
        it was away. A folder with another folder under its path is read. What is kept is bounded by a count and by bytes, the one left
        longest ago let go first, and a trim lets go of every one whose folder is not on the trail.
*/
static fn browser_kept(struct files_model address_to m)
{
        static struct files_trail trail;
        p8 a[FILES_PATH], b[FILES_PATH], c[FILES_PATH], name[FILES_NAME], path[FILES_PATH], aside[FILES_PATH];
        b32 along, left, stale, other, counted, bytes, trimmed, finished;
        p32 reads;
        int kept = 0;

        browser_folder((string_address) "kept");
        browser_folder((string_address) "kept/b");
        browser_folder((string_address) "kept/b/c");
        browser_folder((string_address) "kept/b/cat");
        browser_folder((string_address) "kept/b/dog");
        browser_file((string_address) "kept/b/cap", 1, 0);
        browser_file((string_address) "kept/one", 1, 0);
        browser_file((string_address) "kept/two", 1, 0);
        browser_file((string_address) "kept/three", 1, 0);
        browser_path(a, (string_address) "kept"), browser_path(b, (string_address) "kept/b"), browser_path(c, (string_address) "kept/b/c");
        memory_fill(&trail, 0, sizeof(trail));
        browser_go(m, (string_address) "kept");
        files_trail_note(&trail, m->path, &m->folder);
        // In the first folder: two chosen, the keys on one of them, the rows somewhere.
        files_model_choose(m, browser_at(m, "one"), FILES_CHOOSE_ONLY);
        files_model_choose(m, browser_at(m, "two"), FILES_CHOOSE_TOGGLE);
        m->seat = 77;
        browser_go(m, (string_address) "kept/b");
        files_trail_note(&trail, m->path, &m->folder);
        files_model_choose(m, browser_at(m, "dog"), FILES_CHOOSE_ONLY);
        browser_go(m, (string_address) "kept/b/c");
        files_trail_note(&trail, m->path, &m->folder);
        reads = m->reads;
        // Up to the first, down to the second, down to the third, up again: every one was read before, and none is read now.
        files_model_go(m, a);
        left = m->state == FILES_READY && !m->walking && m->restored && browser_is(m, (string_address) "b one three two ") && m->chosen_count == 2 &&
               files_model_chosen(m, browser_at(m, "one")) && files_model_chosen(m, browser_at(m, "two")) && !files_model_chosen(m, browser_at(m, "three")) &&
               m->cursor == browser_at(m, "two") && m->seat == 77 && files_model_stale(m) == FILES_FRESH;
        files_model_go(m, b);
        along = m->state == FILES_READY && m->restored && m->cursor == browser_at(m, "dog");
        files_model_go(m, c);
        along = along && m->state == FILES_READY && m->restored;
        files_model_up(m);
        // Up out of a folder puts the keys on it, as it always did; what was chosen there is as it was left.
        along = along && m->state == FILES_READY && m->restored && m->cursor == browser_at(m, "c") && files_model_chosen(m, browser_at(m, "dog")) && m->chosen_count == 1 &&
                !m->came[0] && m->reads == reads;
        check("browser: moving along a trail whose folders were read begins no reading at all: each is shown whole at once, from the listing kept of it", along);
        check("browser: a folder gone back to is as it was left: its entries, the two that were chosen, the place the keys were on and where its rows were", left);
        // The first folder changes while another is shown: gone back to, it is shown as it was left, and the look says it is stale.
        browser_wait_a_tick();
        browser_file((string_address) "kept/four", 1, 0);
        files_model_go(m, a);
        stale = m->restored && m->reads == reads && browser_is(m, (string_address) "b one three two ") && files_model_stale(m) == FILES_STALE;
        files_model_reload(m);
        browser_read(m, 64);
        stale = stale && m->reads == reads + 1 && browser_is(m, (string_address) "b four one three two ");
        check("browser: a kept folder that changed while it was away is shown as it was left and is stale to the look that follows, and only then read again", stale);
        // The second folder moved aside and another made under its name: its kept listing is not that folder's, and it is read.
        browser_path(aside, (string_address) "kept/b-aside");
        system_call_5(syscall(renameat2), AT_FDCWD, (positive)b, AT_FDCWD, (positive)aside, 0);
        browser_folder((string_address) "kept/b");
        browser_file((string_address) "kept/b/new", 1, 0);
        reads = m->reads;
        files_model_go(m, b);
        other = !m->restored && m->state == FILES_READING && m->reads == reads + 1;
        browser_read(m, 64);
        other = other && browser_is(m, (string_address) "new ");
        check("browser: a kept listing is a folder's only while its path opens to the same folder: another folder under the path is read, and the kept one is let go", other);
        // The names of a kept folder finish a name typed for it, with no disk read: the folder moved aside is the one kept as b-aside... it is
        // read once here, left, and then asked.
        browser_go(m, (string_address) "kept/b-aside");
        files_model_go(m, a);
        files_model_reload(m);
        browser_read(m, 64);
        reads = m->reads;
        finished = files_model_complete_in(m, aside, (const p8 address_to) "ca", 2, name, sizeof(name)) == 1 && string_equals(name, (string_address) "cat") &&
                   files_model_complete_in(m, aside, (const p8 address_to) "c", 1, name, sizeof(name)) == 2 && string_equals(name, (string_address) "c") &&
                   files_model_complete_in(m, a, (const p8 address_to) "b-", 2, name, sizeof(name)) == 1 && string_equals(name, (string_address) "b-aside") &&
                   files_model_complete_in(m, c, (const p8 address_to) "x", 1, name, sizeof(name)) == 0 && m->reads == reads;
        check("browser: a folder's name is finished from the listing of a kept folder as from the one shown, folders only, and from neither for a folder that is not in memory, reading no disk",
              finished);
        // Ten folders gone through: no more than FILES_KEPT are kept, and the ones left longest ago are the ones let go.
        for (int k = 0; k < 10; k++)
        {
                browser_numbered(path, "kept/n", k, null, 0);
                browser_folder(path);
                browser_go(m, path);
        }
        for (int k = 0; k < FILES_KEPT; k++)
                kept += m->kept[k].path[0] != 0;
        browser_path(path, (string_address) "kept/n0");
        counted = kept == FILES_KEPT && files_keep_find(m, path) < 0;
        browser_path(path, (string_address) "kept/n8");
        counted = counted && files_keep_find(m, path) >= 0;
        check("browser: no more listings are kept than the count allows, and the one left longest ago is the one let go", counted);
        // Three folders of fifteen hundred files: each fits the bytes alone and no three do together, so the oldest goes; and the trim
        // lets go of what is not on the trail.
        for (int k = 0; k < 3; k++)
        {
                browser_numbered(path, "kept/big", k, null, 0);
                browser_folder(path);
                for (int file = 0; file < 1500; file++)
                {
                        browser_numbered(path, "kept/big", k, "/file-", file);
                        browser_file(path, 0, 0);
                }
                browser_numbered(path, "kept/big", k, null, 0);
                browser_go(m, path);
        }
        browser_go(m, (string_address) "kept");
        {
                positive held = 0;

                for (int k = 0; k < FILES_KEPT; k++)
                        held += m->kept[k].bytes;
                browser_path(path, (string_address) "kept/big0");
                bytes = held <= FILES_KEPT_BYTES && files_keep_find(m, path) < 0;
                browser_path(path, (string_address) "kept/big2");
                bytes = bytes && files_keep_find(m, path) >= 0 && m->kept[files_keep_find(m, path)].bytes > FILES_KEPT_BYTES / 3;
        }
        check("browser: the listings kept are no more bytes together than the bound, the one left longest ago let go to make room", bytes);
        memory_fill(&trail, 0, sizeof(trail));
        browser_go(m, (string_address) "kept/big2");
        files_trail_note(&trail, m->path, &m->folder);
        browser_go(m, (string_address) "kept");
        files_trail_note(&trail, m->path, &m->folder);
        files_keep_trim(m, &trail);
        kept = 0;
        for (int k = 0; k < FILES_KEPT; k++)
                kept += m->kept[k].path[0] != 0;
        browser_path(path, (string_address) "kept/big2");
        trimmed = kept == 1 && files_keep_find(m, path) >= 0;
        check("browser: a trim lets go of every kept listing whose folder is not on the trail, and keeps the trail's", trimmed);
        for (int k = 0; k < 3; k++)
        {
                browser_numbered(path, "kept/big", k, null, 0);
                browser_remove_in(path);
        }
}

/*
        One folder of a trail spelled again (files_trail_spell, files_trail_resolve): the folder is replaced and the folders after it
        kept; the model goes to the deepest folder of the new path that is one, and the rest stay in the trail, lost; what a person
        typed is not trusted.
*/
static fn browser_spell(struct files_model address_to m)
{
        static struct files_trail trail;
        p8 full[FILES_PATH], want[FILES_PATH], into[FILES_PATH], base[FILES_PATH];
        b32 replaced, several, refused, whole, part, file, again;
        bipolar handle = -1, error = 0;
        int at, why;

        browser_folder((string_address) "spell");
        browser_folder((string_address) "spell/to");
        browser_folder((string_address) "spell/to/something");
        browser_folder((string_address) "spell/to/something/else");
        browser_folder((string_address) "spell/foo");
        browser_folder((string_address) "spell/foo/something");
        browser_folder((string_address) "spell/foo/something/else");
        browser_folder((string_address) "spell/bar");
        browser_folder((string_address) "spell/bar/something");
        browser_file((string_address) "spell/file", 1, 0);
        browser_folder((string_address) "spell/deep");
        browser_folder((string_address) "spell/deep/er");
        browser_folder((string_address) "spell/deep/er/something");
        browser_path(base, (string_address) "spell");
        memory_fill(&trail, 0, sizeof(trail));
        browser_go(m, (string_address) "spell/to/something/else");
        files_trail_note(&trail, m->path, &m->folder);
        at = trail.count - 3; // "to"
        browser_path(full, (string_address) "spell/to/something/else");
        browser_path(want, (string_address) "spell/foo/something/else");
        replaced = files_trail_spell(&trail, at, (const p8 address_to) "foo", into) && string_equals(into, want) && string_equals(trail.path, full);
        check("browser: a folder of the trail spelled again is replaced in the path and every folder after it is kept: /path/to/something/else with to made foo is /path/foo/something/else",
              replaced);
        browser_path(want, (string_address) "spell/deep/er/something/else");
        several = files_trail_spell(&trail, at, (const p8 address_to) "deep//er/", into) && string_equals(into, want);
        browser_path(want, (string_address) "spell/something/else");
        several = several && files_trail_spell(&trail, at, (const p8 address_to) "", into) && string_equals(into, want);
        browser_path(want, (string_address) "spell/to/something");
        several = several && files_trail_spell(&trail, trail.count - 1, (const p8 address_to) "", into) && string_equals(into, want);
        check("browser: a stroke typed in a folder's name makes several folders of it (strokes run together are one), and no name takes the folder out of the path", several);
        refused = !files_trail_spell(&trail, at, (const p8 address_to) "fo\no", into) && !files_trail_spell(&trail, at, (const p8 address_to) "a\x7f", into) &&
                  !files_trail_spell(&trail, at, (const p8 address_to) "..", into) && !files_trail_spell(&trail, at, (const p8 address_to) "a/../b", into) &&
                  !files_trail_spell(&trail, at, (const p8 address_to) "./a", into) && !files_trail_spell(&trail, 0, (const p8 address_to) "x", into) &&
                  !files_trail_spell(&trail, trail.count, (const p8 address_to) "x", into) && !files_trail_spell(&trail, -1, (const p8 address_to) "x", into);
        {
                static p8 long_name[FILES_PATH + 8];

                memory_fill(long_name, 'n', FILES_NAME);
                long_name[FILES_NAME] = end;
                refused = refused && !files_trail_spell(&trail, at, long_name, into); // a name longer than a name
                for (positive k = 0; k + 1 < FILES_PATH; k++)
                        long_name[k] = k % 100 == 99 ? '/' : 'n';
                long_name[FILES_PATH - 1] = end;
                refused = refused && !files_trail_spell(&trail, at, long_name, into); // a path longer than a path
        }
        check("browser: what is typed for a folder is not trusted: a control byte, a folder spelled . or .., a name or a path too long, the root and a folder that is not of the trail are refused",
              refused);
        // Gone to: every folder of the new path is one, so the deepest is shown and nothing is lost.
        browser_path(want, (string_address) "spell/foo/something/else");
        files_trail_spell(&trail, at, (const p8 address_to) "foo", into);
        why = files_trail_resolve(m, &trail, into);
        browser_read(m, 64);
        whole = why == FILES_READY && string_equals(m->path, want) && string_equals(trail.path, want) && trail.current == trail.count - 1 && !trail.lost[trail.count - 1];
        check("browser: the path spelled is gone to as far as it is folders: with every folder there, the deepest is shown and is the trail's current one", whole);
        // bar has something and no else: the browser stops in bar/something, and else stays in the trail, lost, with the reason.
        at = trail.count - 3;
        browser_path(want, (string_address) "spell/bar/something");
        browser_path(full, (string_address) "spell/bar/something/else");
        files_trail_spell(&trail, at, (const p8 address_to) "bar", into);
        why = files_trail_resolve(m, &trail, into);
        browser_read(m, 64);
        part = why == FILES_GONE && string_equals(m->path, want) && string_equals(trail.path, full) && trail.current == trail.count - 2 && trail.lost[trail.count - 1] &&
               !trail.lost[trail.count - 2] && files_trail_open(&trail, trail.count - 1, &handle, &error) == FILES_MISSING && handle < 0;
        // nowhere has nothing: the browser stops at the folder before it, shown already, and all three after it are lost.
        files_trail_spell(&trail, at, (const p8 address_to) "nowhere", into);
        why = files_trail_resolve(m, &trail, into);
        browser_read(m, 64);
        part = part && why == FILES_GONE && string_equals(m->path, base) && trail.current == trail.count - 4 && trail.lost[trail.count - 3] && trail.lost[trail.count - 2] &&
               trail.lost[trail.count - 1] && trail.count >= 5;
        check("browser: where a folder of the path spelled is not there the browser stops at the last that is, and the folders after it stay in the trail, lost, with the reason", part);
        // A file where a folder is spelled: not a folder, said as that, and the trail keeps it lost.
        at = trail.count - 3;
        files_trail_spell(&trail, at, (const p8 address_to) "file", into);
        why = files_trail_resolve(m, &trail, into);
        file = why == FILES_NOT_DIRECTORY && string_equals(m->path, base) && trail.lost[at];
        check("browser: a file spelled where a folder goes is said to be no folder, and the browser stays at the last folder", file);
        // The lost folder spelled right: the path is gone down again and nothing of it is lost.
        browser_path(want, (string_address) "spell/to/something/else");
        files_trail_spell(&trail, at, (const p8 address_to) "to", into);
        why = files_trail_resolve(m, &trail, into);
        browser_read(m, 64);
        again = why == FILES_READY && string_equals(m->path, want) && trail.current == trail.count - 1 && !trail.lost[at] && !trail.lost[trail.count - 1];
        check("browser: a lost folder spelled right is found: the path is gone down to its end and none of it is lost", again);
        // A folder of the trail opened as the folder it was: the descriptor is that folder (compared on the descriptor itself).
        browser_go(m, (string_address) "spell/to");
        files_trail_note(&trail, m->path, &m->folder);
        {
                file_facts opened;
                b32 held = files_trail_open(&trail, trail.count - 1, &handle, &error) == FILES_SAME && handle >= 0 &&
                           file_look_code(handle, (string_address) "", AT_EMPTY_PATH, &opened) >= 0 && opened.inode == trail.inode[trail.count - 1] &&
                           trail.known[trail.count - 1];
                p8 moved[FILES_PATH], other[FILES_PATH];

                if (handle >= 0)
                        system_call_1(syscall(close), (positive)handle);
                // A link to another folder under its name: refused by the open itself, and nothing is opened.
                browser_path(want, (string_address) "spell/to/something/else"), browser_path(moved, (string_address) "spell/to/something/else-aside");
                browser_path(other, (string_address) "spell/foo");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)want, AT_FDCWD, (positive)moved, 0);
                browser_link(other, (string_address) "spell/to/something/else");
                held = held && files_trail_open(&trail, trail.count - 1, &handle, &error) == FILES_CHANGED && handle < 0;
                // And for a trail that only names its folders (one that was spelled: nothing is remembered of them to compare), the link
                // is still not a folder of the trail: the open itself does not follow it.
                {
                        static struct files_trail named;

                        memory_fill(&named, 0, sizeof(named));
                        files_trail_become(&named, want);
                        held = held && !named.known[named.count - 1] && files_trail_open(&named, named.count - 1, &handle, &error) == FILES_CHANGED && handle < 0;
                }
                // The folder before it swapped for a link to a folder with the same names inside: what opens is another folder, and it is refused.
                browser_remove_in((string_address) "spell/to/something/else");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)moved, AT_FDCWD, (positive)want, 0);
                browser_path(want, (string_address) "spell/to/something"), browser_path(moved, (string_address) "spell/to/something-aside");
                browser_path(other, (string_address) "spell/foo/something");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)want, AT_FDCWD, (positive)moved, 0);
                browser_link(other, (string_address) "spell/to/something");
                held = held && files_trail_open(&trail, trail.count - 1, &handle, &error) == FILES_CHANGED && handle < 0;
                browser_remove_in((string_address) "spell/to/something");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)moved, AT_FDCWD, (positive)want, 0);
                check("browser: a deeper folder of the trail is opened without following and held to what it was on the descriptor opened: a link under its name, or a folder before it swapped for a link to another tree, opens nothing",
                      held);
        }
        // Lost folders stay in the trail, the current one is never lost, and going to a folder clears what is before it.
        {
                int count = trail.count;
                b32 kept;

                files_trail_lose(&trail, trail.current);
                kept = trail.count == count && !trail.lost[trail.current] && trail.lost[trail.current + 1] && trail.lost[count - 1];
                browser_go(m, (string_address) "spell/to/something");
                files_trail_note(&trail, m->path, &m->folder);
                kept = kept && !trail.lost[trail.current] && trail.lost[count - 1] && trail.count == count;
                check("browser: folders of the trail that are lost stay in it where they stand, the folder shown is never one, and a folder gone to is lost no more", kept);
        }
}

static fn browser_changes(struct files_model address_to m)
{
        p8 before[FILES_PATH], elsewhere[FILES_PATH];
        p32 was, place;
        b32 beside, whole;
        file_facts facts;

        browser_folder((string_address) "live");
        browser_folder((string_address) "live/inner");
        browser_file((string_address) "live/one", 10, 0);
        browser_file((string_address) "live/two", 20, 0);
        browser_folder((string_address) "elsewhere");
        browser_file((string_address) "elsewhere/prize", 1, 0);
        browser_go(m, (string_address) "live");
        check("browser: a folder nothing touched is fresh", files_model_stale(m) == FILES_FRESH && browser_is(m, (string_address) "inner one two "));
        browser_wait_a_tick();
        browser_file((string_address) "live/three", 30, 0);
        was = files_model_count(m);
        check("browser: a file added under the listing makes it stale, and what is shown is still the old listing", files_model_stale(m) == FILES_STALE && files_model_count(m) == was);
        // Read again beside what is shown: every entry of the old listing is there till the new one is whole.
        files_model_choose(m, browser_at(m, "two"), FILES_CHOOSE_ONLY);
        files_model_reload(m);
        beside = m->reloading && m->state == FILES_READY && files_model_count(m) == was && browser_is(m, (string_address) "inner one two ");
        files_model_step(m, 1);
        beside = beside && files_model_count(m) == was && browser_is(m, (string_address) "inner one two ");
        browser_read(m, 1);
        whole = !m->reloading && m->state == FILES_READY && browser_is(m, (string_address) "inner one three two ");
        check("browser: a folder read again is read beside the listing shown, which stays whole till the new one takes its place", beside && whole);
        check("browser: and the cursor is on the entry it was on, by its name, and the folder is fresh again", m->cursor == browser_at(m, "two") && files_model_stale(m) == FILES_FRESH);
        // A listed file taken away, and one replaced by another thing of its name: neither is the file that was listed.
        place = browser_at(m, "one");
        check("browser: a listed entry looked at again is the same file while nothing touched it", files_model_check(m, place, &facts) == FILES_SAME && facts.size == 10);
        browser_remove_in((string_address) "live/one");
        check("browser: a listed entry that was removed is missing when it is looked at again, though the listing still has its row",
              files_model_check(m, place, null) == FILES_MISSING && files_model_entry(m, place) != null);
        browser_remove_in((string_address) "live/two");
        browser_folder((string_address) "live/two");
        check("browser: a listed file whose name another thing has now is changed, and is not acted on",
              files_model_check(m, browser_at(m, "two"), null) == FILES_CHANGED && files_model_enter(m, browser_at(m, "two")) == FILES_CHANGED);
        // The swap: a folder that was listed is taken away and a link to another place is given its name. It is not entered.
        // (The folder is moved aside and not removed, so its inode stays taken and what comes after has another.)
        browser_path(elsewhere, (string_address) "elsewhere");
        {
                p8 from[FILES_PATH], to[FILES_PATH];

                browser_path(from, (string_address) "live/inner"), browser_path(to, (string_address) "live/inner-aside");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)from, AT_FDCWD, (positive)to, 0);
        }
        browser_link(elsewhere, (string_address) "live/inner");
        string_copy_bounded(before, m->path, FILES_PATH);
        check("browser: a listed folder swapped for a link to another place is not entered: the stale row is looked at again first, and nothing is followed",
              files_model_enter(m, browser_at(m, "inner")) == FILES_CHANGED && string_equals(m->path, before));
        // The same swap with a folder in its place: another inode under the name, so still not the folder listed.
        browser_remove_in((string_address) "live/inner");
        browser_folder((string_address) "live/inner");
        browser_folder((string_address) "live/inner/new");
        check("browser: a listed folder replaced by another folder of its name is not the one listed either",
              files_model_enter(m, browser_at(m, "inner")) == FILES_CHANGED && string_equals(m->path, before));
        files_model_reload(m);
        browser_read(m, 64);
        check("browser: read again, the folder of that name is the one that is there, and is entered", files_model_enter(m, browser_at(m, "inner")) == FILES_SAME);
        browser_read(m, 64);
        // The folder shown is itself taken away, and then one that was renamed with another put where it was.
        browser_go(m, (string_address) "live");
        browser_remove_in((string_address) "live");
        check("browser: the folder shown, removed, is lost: its state is gone and it has no entries", files_model_stale(m) == FILES_LOST && m->state == FILES_GONE && files_model_count(m) == 0);
        browser_folder((string_address) "moved");
        browser_file((string_address) "moved/kept", 1, 0);
        browser_go(m, (string_address) "moved");
        {
                p8 from[FILES_PATH], to[FILES_PATH];

                browser_path(from, (string_address) "moved"), browser_path(to, (string_address) "moved-away");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)from, AT_FDCWD, (positive)to, 0);
        }
        browser_folder((string_address) "moved");
        check("browser: the folder shown, renamed, with another put where it was, is lost too: the path names another folder now",
              files_model_stale(m) == FILES_LOST && m->state == FILES_GONE);
}

static fn browser_many_name(p8 address_to name, int at)
{
        name[6] = (p8)('0' + at / 10000 % 10), name[7] = (p8)('0' + at / 1000 % 10), name[8] = (p8)('0' + at / 100 % 10);
        name[9] = (p8)('0' + at / 10 % 10), name[10] = (p8)('0' + at % 10);
}

static fn browser_many(struct files_model address_to m)
{
        p8 name[32] = "many/f00000", path[FILES_PATH];
        positive steps = 0, most_grown = 0;
        b32 stepped = true, shown_partly = false, ordered = true;

        browser_folder((string_address) "many");
        for (int at = 0; at < 10000; at++)
        {
                browser_many_name(name, at);
                browser_file(name, 0, 0);
        }
        browser_path(path, (string_address) "many");
        files_model_go(m, path);
        // Step by step: a step takes at most what it was given and returns, the entries so far are there to show, and the state says it reads.
        for (;;)
        {
                p32 before = files_model_count(m);
                b32 more = files_model_step(m, 256);

                steps++;
                most_grown = files_model_count(m) - before > most_grown ? files_model_count(m) - before : most_grown;
                if (!more)
                        break;
                stepped = stepped && m->state == FILES_READING;
                shown_partly = shown_partly || (files_model_count(m) > 0 && files_model_count(m) < 10000 && files_model_entry(m, files_model_count(m) - 1) != null);
        }
        check("browser: ten thousand entries are read in steps of at most the entries a step is given: forty steps and more, none of them the whole folder",
              stepped && steps >= 40 && most_grown <= 256 && files_model_count(m) == 10000);
        check("browser: while it reads, the state says so and the entries so far are there to show", shown_partly);
        for (p32 place = 0; place < 10000 && ordered && files_model_count(m) == 10000; place++)
        {
                const p8 address_to shown = files_model_name(m, files_model_entry(m, place));

                ordered = shown[0] == 'f' && (p32)((shown[1] - '0') * 10000 + (shown[2] - '0') * 1000 + (shown[3] - '0') * 100 + (shown[4] - '0') * 10 + (shown[5] - '0')) == place;
        }
        check("browser: read whole, the ten thousand are in order, every one once, the cursor on the first, and none cut", m->state == FILES_READY && ordered && !m->shown.cut && m->cursor == 0);
        // More than a listing holds: it is cut there and says so, and that is no error.
        for (int at = 10000; at < 12100; at++)
        {
                browser_many_name(name, at);
                browser_file(name, 0, 0);
        }
        files_model_reload(m);
        browser_read(m, 256);
        check("browser: a folder with more entries than a listing holds is cut at what it holds and says so, read whole and no error",
              m->state == FILES_READY && files_model_count(m) == FILES_MOST && m->shown.cut);
}

static b32 browser_there(string_address relative)
{
        p8 path[FILES_PATH];
        file_facts facts;

        browser_path(path, relative);
        return file_look_code(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) == 0;
}

/*
        What changes the disk (files_model_make_folder, files_model_rename, the delete job). A new folder and a new name are made
        only where the name is free and is a name; a rename is of the entry that was listed and no other. A delete removes files,
        links as links and folders with all that is in them, a bounded number of things a step; it removes nothing when an entry is
        no longer the one listed, stops where a folder was swapped for a link under it and leaves what the link names alone, stops
        with the kernel's reason where it may not remove, can be cancelled between two steps with what it has not reached still
        there, and takes ten thousand files in steps.
*/
static fn browser_ops(struct files_model address_to m)
{
        static struct files_job job;
        p8 elsewhere[FILES_PATH], name[32] = "ops/hundred/f000";
        positive steps;
        b32 made, renamed, deleted, stale, swapped, cancelled, many;
        bipolar child;

        browser_folder((string_address) "ops");
        browser_file((string_address) "ops/one", 10, 0);
        browser_file((string_address) "ops/two", 20, 0);
        browser_go(m, (string_address) "ops");
        made = files_model_make_folder(m, (const p8 address_to) "made") == FILES_DID && browser_there((string_address) "ops/made") &&
               files_model_make_folder(m, (const p8 address_to) "made") == FILES_DID_EXISTS && files_model_make_folder(m, (const p8 address_to) "one") == FILES_DID_EXISTS &&
               files_model_make_folder(m, (const p8 address_to) "") == FILES_DID_BAD && files_model_make_folder(m, (const p8 address_to) "a/b") == FILES_DID_BAD &&
               files_model_make_folder(m, (const p8 address_to) "..") == FILES_DID_BAD && !browser_there((string_address) "ops/a");
        check("browser: a new folder is made where its name is free, a name that is taken is refused whatever has it, and what is no name (empty, with a stroke, the folder above) makes nothing",
              made);
        files_model_reload(m);
        browser_read(m, 64);
        renamed = files_model_rename(m, browser_at(m, "one"), (const p8 address_to) "uno") == FILES_DID && browser_there((string_address) "ops/uno") &&
                  !browser_there((string_address) "ops/one");
        files_model_reload(m);
        browser_read(m, 64);
        renamed = renamed && files_model_rename(m, browser_at(m, "uno"), (const p8 address_to) "two") == FILES_DID_EXISTS && browser_there((string_address) "ops/uno") &&
                  files_model_rename(m, browser_at(m, "uno"), (const p8 address_to) "x/y") == FILES_DID_BAD;
        // The listed file replaced by another of its name: it is not the entry listed, and is not renamed.
        browser_remove_in((string_address) "ops/two");
        browser_folder((string_address) "ops/two");
        renamed = renamed && files_model_rename(m, browser_at(m, "two"), (const p8 address_to) "three") == FILES_DID_BAD && !browser_there((string_address) "ops/three");
        check("browser: an entry is given a new name where the name is free and is a name, never over a name that is taken, and only if it is still the entry that was listed",
              renamed);

        // A delete: a file, a link to a folder elsewhere (the link goes, the folder it names stays whole) and a folder of folders.
        browser_folder((string_address) "ops/tree");
        browser_folder((string_address) "ops/tree/a");
        browser_folder((string_address) "ops/tree/a/b");
        browser_file((string_address) "ops/tree/a/b/deep", 5, 0);
        browser_file((string_address) "ops/tree/top", 5, 0);
        browser_folder((string_address) "ops-elsewhere");
        browser_file((string_address) "ops-elsewhere/prize", 1, 0);
        browser_path(elsewhere, (string_address) "ops-elsewhere");
        browser_link(elsewhere, (string_address) "ops/tree/a/way-out");
        browser_link(elsewhere, (string_address) "ops/link");
        files_model_reload(m);
        browser_read(m, 64);
        files_model_choose(m, browser_at(m, "tree"), FILES_CHOOSE_ONLY);
        files_model_choose(m, browser_at(m, "link"), FILES_CHOOSE_TOGGLE);
        files_model_choose(m, browser_at(m, "uno"), FILES_CHOOSE_TOGGLE);
        deleted = files_delete_begin(m, &job) && job.count == 3 && job.state == FILES_JOB_RUNNING;
        for (steps = 0; files_job_step(m, &job, 1) && steps < 100; steps++)
                deleted = deleted && job.state == FILES_JOB_RUNNING;
        // tree, a, b, deep, top, way-out, link, uno: eight things, one a step, with the steps that went into a folder beside them
        deleted = deleted && job.state == FILES_JOB_DONE && job.removed == 8 && steps >= 8 && !browser_there((string_address) "ops/tree") &&
                  !browser_there((string_address) "ops/link") && !browser_there((string_address) "ops/uno") && browser_there((string_address) "ops/made") &&
                  browser_there((string_address) "ops-elsewhere/prize");
        check("browser: a delete removes a file, a link as the link it is and a folder with all that is in it, one thing a step, and what a link named is left whole",
              deleted);

        // Stale: the entry listed is replaced before the delete is asked. Nothing is removed.
        browser_file((string_address) "ops/stale", 3, 0);
        browser_file((string_address) "ops/kept", 3, 0);
        files_model_reload(m);
        browser_read(m, 64);
        browser_remove_in((string_address) "ops/stale");
        browser_file((string_address) "ops/stale", 4, 0);
        files_model_choose(m, browser_at(m, "kept"), FILES_CHOOSE_ONLY);
        files_model_choose(m, browser_at(m, "stale"), FILES_CHOOSE_TOGGLE);
        stale = !files_delete_begin(m, &job) && job.state == FILES_JOB_FAILED && string_equals(job.at, (string_address) "stale") && !files_job_step(m, &job, 100) &&
                job.removed == 0 && browser_there((string_address) "ops/stale") && browser_there((string_address) "ops/kept");
        check("browser: a delete asked of a listing that is stale removes nothing: one entry that is no longer the one listed stops it before it starts, at that name", stale);

        // Swapped under it: a folder that was looked at is moved aside and a link to another place given its name before the job reaches it.
        browser_folder((string_address) "ops/victim");
        browser_file((string_address) "ops/victim/mine", 1, 0);
        files_model_reload(m);
        browser_read(m, 64);
        files_model_choose(m, browser_at(m, "victim"), FILES_CHOOSE_ONLY);
        swapped = files_delete_begin(m, &job);
        {
                p8 from[FILES_PATH], to[FILES_PATH];

                browser_path(from, (string_address) "ops/victim"), browser_path(to, (string_address) "ops/victim-aside");
                system_call_5(syscall(renameat2), AT_FDCWD, (positive)from, AT_FDCWD, (positive)to, 0);
        }
        browser_link(elsewhere, (string_address) "ops/victim");
        while (files_job_step(m, &job, 8))
                ;
        swapped = swapped && job.state == FILES_JOB_FAILED && job.removed == 0 && browser_there((string_address) "ops-elsewhere/prize") &&
                  browser_there((string_address) "ops/victim") && browser_there((string_address) "ops/victim-aside/mine");
        check("browser: a folder swapped for a link to another place before the delete reaches it stops the delete there: the link is not gone into, and what it names is whole",
              swapped);
        browser_remove_in((string_address) "ops/victim");

        // May not: a user who is not the folder's owner deletes nothing, and the job says the kernel's reason and the name.
        files_model_reload(m);
        browser_read(m, 64);
        files_model_choose(m, browser_at(m, "kept"), FILES_CHOOSE_ONLY);
        child = (bipolar)system_fork();
        if (child == 0)
        {
                int answer = 2;

                if ((bipolar)system_call_3(syscall(setresgid), 65534, 65534, 65534) >= 0 && (bipolar)system_call_3(syscall(setresuid), 65534, 65534, 65534) >= 0)
                {
                        files_delete_begin(m, &job);
                        while (files_job_step(m, &job, 8))
                                ;
                        answer = job.state == FILES_JOB_FAILED && job.error < 0 && job.removed == 0 && string_equals(job.at, (string_address) "kept") ? 0 : 1;
                }
                system_call_1(syscall(exit_group), (positive)answer);
        }
        else if (child > 0)
        {
                b32 raw = 0;

                system_wait4_retry(child, address_of raw, 0, null);
                if ((raw & 0x7f) == 0 && (raw >> 8 & 0xff) == 2)
                        log_direct(str("  browser: a delete by a user who may not: NOT RUN (this user cannot become another)\n"));
                else
                        check("browser: a delete by a user who may not remove from the folder removes nothing and stops with the kernel's reason at the name",
                              (raw & 0x7f) == 0 && (raw >> 8 & 0xff) == 0 && browser_there((string_address) "ops/kept"));
        }
        else
                log_direct(str("  browser: a delete by a user who may not: NOT RUN (no child could be started)\n"));

        // Cancelled part way: what was reached is gone, what was not is there, and the job says how far it got.
        browser_folder((string_address) "ops/hundred");
        for (int at = 0; at < 100; at++)
        {
                name[13] = (p8)('0' + at / 100), name[14] = (p8)('0' + at / 10 % 10), name[15] = (p8)('0' + at % 10);
                browser_file(name, 0, 0);
        }
        files_model_reload(m);
        browser_read(m, 64);
        files_model_choose(m, browser_at(m, "hundred"), FILES_CHOOSE_ONLY);
        cancelled = files_delete_begin(m, &job) && files_job_step(m, &job, 31) && job.removed == 30; // the first step goes into the folder
        files_job_cancel(&job);
        cancelled = cancelled && job.state == FILES_JOB_CANCELLED && !files_job_step(m, &job, 100) && job.removed == 30 && job.depth == 0 &&
                    browser_there((string_address) "ops/hundred");
        {
                p32 left = 0;

                for (int at = 0; at < 100; at++)
                {
                        name[13] = (p8)('0' + at / 100), name[14] = (p8)('0' + at / 10 % 10), name[15] = (p8)('0' + at % 10);
                        left += browser_there(name);
                }
                cancelled = cancelled && left == 70;
        }
        check("browser: a delete cancelled part way stops between two things: the thirty it reached are gone, the seventy it did not are there, and it says thirty", cancelled);

        // Ten thousand and more: the folder of the reading rows, in steps.
        browser_go(m, (string_address) "");
        files_model_choose(m, browser_at(m, "many"), FILES_CHOOSE_ONLY);
        many = files_delete_begin(m, &job);
        for (steps = 1; files_job_step(m, &job, 256); steps++)
                many = many && job.state == FILES_JOB_RUNNING;
        many = many && job.state == FILES_JOB_DONE && job.removed == 12101 && steps >= 40 && !browser_there((string_address) "many");
        check("browser: a folder of twelve thousand files is deleted in steps of at most the things a step is given, forty steps and more, and is gone", many);
        files_job_end(&job);
}

/*
        The window's own rules for a delete, with no window: Delete asks before anything goes, and the question says how many and the
        first names of them as words from outside (a control byte in a name is its picture there too); nothing is removed by the
        asking; Delete again, with the listing as it was asked of, begins the job; and a listing that changed since the question is
        asked about again and removes nothing.
*/
/*
        Copying and moving (files_move_*), with no window: paths into a folder held open, stepped to the end here (and once on its own
        thread). A copy makes a file, a folder with what is in it and a link as the link it is, never over a name that is taken (that one is
        given a number), with the bytes of a file of several pieces whole; a move on one filesystem is a rename, and one into the folder it
        is in is nothing; a folder into itself is refused before anything is made; a cancel between two pieces leaves no half file.
*/
static positive browser_size(string_address relative)
{
        p8 path[FILES_PATH];
        file_facts facts;

        browser_path(path, relative);
        return file_look_code(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) == 0 ? (positive)facts.size : 0;
}

static b32 browser_is_link(string_address relative)
{
        p8 path[FILES_PATH];
        file_facts facts;

        browser_path(path, relative);
        return file_look_code(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) == 0 && (facts.mode & MODE_FORMAT) == MODE_LINK;
}

// The sources, absolute, each with its end, into paths (room bytes): how many.
static p32 browser_sources(p8 address_to paths, positive room, const char address_to const address_to relative, p32 count)
{
        positive used = 0;

        for (p32 at = 0; at < count; at++)
        {
                p8 path[FILES_PATH];
                positive length;

                browser_path(path, (string_address)relative[at]);
                length = string_length(path);
                if (used + length + 1 > room)
                        return at;
                memory_copy(paths + used, path, length + 1);
                used += length + 1;
        }
        return count;
}

static bipolar browser_open_folder(string_address relative)
{
        p8 path[FILES_PATH];

        browser_path(path, relative);
        return system_open_at(AT_FDCWD, path, FILE_READ | O_DIRECTORY | O_CLOEXEC);
}

// The move stepped to its end here: how many steps it took.
static positive browser_move_run(struct files_move address_to job)
{
        positive steps = 0;

        while (files_move_step(job) && steps < 100000)
                steps++;
        return steps;
}

static fn browser_moves(void)
{
        static struct files_move job;
        static p8 paths[4 * FILES_PATH];
        static const char address_to const copy_these[] = {"mv/src/f1", "mv/src/d", "mv/src/link", "mv/src/big"};
        static const char address_to const move_these[] = {"mv/dst/d"};
        static const char address_to const back_these[] = {"mv/src/d"};
        static const char address_to const inside_these[] = {"mv/src/d"};
        static const char address_to const cancel_these[] = {"mv/src/big"};
        b32 copied, same_bytes = true, moved, inside, cancelled, threaded;
        p8 a[FILES_PATH], b[FILES_PATH];
        p32 count;

        browser_folder((string_address) "mv");
        browser_folder((string_address) "mv/src");
        browser_folder((string_address) "mv/src/d");
        browser_folder((string_address) "mv/src/d/sub");
        browser_folder((string_address) "mv/dst");
        browser_folder((string_address) "mv/dst2");
        browser_file((string_address) "mv/src/f1", 100, 1700000000);
        browser_file((string_address) "mv/src/d/x", 5, 0);
        browser_file((string_address) "mv/src/d/sub/y", 7, 0);
        browser_file((string_address) "mv/dst/f1", 3, 0);
        browser_link((string_address) "f1", (string_address) "mv/src/link");
        // Nine megabytes of bytes that differ, so a piece out of place shows: two pieces and a part.
        {
                p8 path[FILES_PATH];
                bipolar made;
                static p8 block[65536];

                browser_path(path, (string_address) "mv/src/big");
                made = (bipolar)system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);
                for (positive k = 0; made >= 0 && k < 144; k++)
                {
                        for (positive at = 0; at < sizeof(block); at++)
                                block[at] = (p8)(at * 7 + k * 13);
                        system_call_3(syscall(write), made, (positive)block, sizeof(block));
                }
                if (made >= 0)
                        system_call_1(syscall(close), made);
        }

        count = browser_sources(paths, sizeof(paths), copy_these, 4);
        copied = files_move_begin(&job, FILES_MOVE_COPY, browser_open_folder((string_address) "mv/dst"), paths, count) && job.count == 4;
        browser_move_run(&job);
        copied = copied && job.state == FILES_JOB_DONE && browser_size((string_address) "mv/dst/f1") == 3 && browser_size((string_address) "mv/dst/f1.copy") == 100 &&
                 browser_size((string_address) "mv/dst/d/x") == 5 && browser_size((string_address) "mv/dst/d/sub/y") == 7 &&
                 browser_is_link((string_address) "mv/dst/link") && browser_size((string_address) "mv/dst/big") == 144 * 65536 && job.renamed == 1 &&
                 job.items_done == job.items_total && job.items_total == 7 && job.bytes_done == job.bytes_total && job.bytes_total == 100 + 5 + 7 + 144 * 65536;
        // The big copy's bytes, compared.
        {
                static p8 one[65536], two[65536];
                bipolar left, right;

                browser_path(a, (string_address) "mv/src/big"), browser_path(b, (string_address) "mv/dst/big");
                left = system_open_at(AT_FDCWD, a, FILE_READ | O_CLOEXEC), right = system_open_at(AT_FDCWD, b, FILE_READ | O_CLOEXEC);
                for (positive k = 0; left >= 0 && right >= 0 && k < 144; k++)
                        same_bytes = same_bytes && system_read_once(left, one, sizeof(one)) == sizeof(one) && system_read_once(right, two, sizeof(two)) == sizeof(two) &&
                                     !memory_compare(one, two, sizeof(one));
                if (left >= 0)
                        system_close(left);
                if (right >= 0)
                        system_close(right);
        }
        files_move_end(&job);
        check("browser: a copy makes a file, a folder with what is in it and a link as the link it is, a name that is taken given a copy's name and never written over, a file of several pieces byte for byte, the counts whole",
              copied && same_bytes);
        // The copy's names: f1 and f1.copy are taken, so the next is f1.copy2; a name with its dot first has no extension to keep.
        {
                p8 free[FILES_NAME];
                b32 numbered = false, named;
                bipolar fresh, into = browser_open_folder((string_address) "mv/dst");

                named = into >= 0 && files_move_free_name(into, (const p8 address_to) "f1", free, &numbered) && numbered &&
                        string_equals((string_address)free, (string_address) "f1.copy2");
                named = named && files_move_free_name(into, (const p8 address_to) "x.tar", free, &numbered) && !numbered &&
                        string_equals((string_address)free, (string_address) "x.tar");
                fresh = into >= 0 ? (bipolar)system_call_4(syscall(openat), (positive)into, (positive) "a.txt", FILE_CREATE | FILE_WRITE | O_TRUNC, 0644) : -1;
                if (fresh >= 0)
                        system_call_1(syscall(close), fresh);
                named = named && files_move_free_name(into, (const p8 address_to) "a.txt", free, &numbered) && numbered &&
                        string_equals((string_address)free, (string_address) "a.copy.txt");
                fresh = into >= 0 ? (bipolar)system_call_4(syscall(openat), (positive)into, (positive) ".rc", FILE_CREATE | FILE_WRITE | O_TRUNC, 0644) : -1;
                if (fresh >= 0)
                        system_call_1(syscall(close), fresh);
                named = named && files_move_free_name(into, (const p8 address_to) ".rc", free, &numbered) && string_equals((string_address)free, (string_address) ".rc.copy");
                if (into >= 0)
                        system_close(into);
                check("browser: a taken name's copy is stem.copy.ext, then stem.copy2.ext, a name with its dot first stem.copy, a free name its own", named);
        }
        // The bin: a file put there is gone from its folder, in BIN/files with an info that says where it was (its odd bytes as %XX), a
        // second of the same name given a copy's name; put back, it is where it was and its info gone; put back where its name is taken,
        // it is given a free one. And a move says the names its things landed as, for its undo.
        {
                p8 bin[FILES_PATH], path[FILES_PATH], named[FILES_NAME], second[FILES_NAME], was[FILES_PATH], back[FILES_PATH];
                bipolar fresh;
                b32 put, restored, again;

                browser_path(bin, (string_address) "bin");
                browser_path(path, (string_address) "mv/dst/a b.txt");
                fresh = (bipolar)system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);
                if (fresh >= 0)
                        system_call_1(syscall(close), fresh);
                put = files_bin_put(bin, path, "2026-11-18T12:00:00", named) == FILES_DID && string_equals((string_address)named, (string_address) "a b.txt") &&
                      !browser_there((string_address) "mv/dst/a b.txt") && browser_there((string_address) "bin/files/a b.txt") &&
                      browser_there((string_address) "bin/info/a b.txt.trashinfo") && files_bin_was(bin, named, was) && string_equals((string_address)was, (string_address)path);
                {
                        p8 info[FILES_PATH * 3];
                        bipolar got;

                        browser_path(was, (string_address) "bin/info/a b.txt.trashinfo");
                        got = file_slurp_once_at(AT_FDCWD, (string_address)was, info, sizeof(info) - 1);
                        info[got > 0 ? got : 0] = 0;
                        put = put && got > 0 && string_find((string_address)info, (string_address) "a%20b.txt\nDeletionDate=2026-11-18T12:00:00\n") &&
                              !memory_compare(info, "[Trash Info]\nPath=/", 19);
                }
                fresh = (bipolar)system_call_4(syscall(openat), AT_FDCWD, (positive)path, FILE_CREATE | FILE_WRITE | O_TRUNC, 0644);
                if (fresh >= 0)
                        system_call_1(syscall(close), fresh);
                put = put && files_bin_put(bin, path, "2026-11-18T12:00:01", second) == FILES_DID && string_equals((string_address)second, (string_address) "a b.copy.txt");
                check("browser: Delete puts a file in the bin's files with an info of where it was and when, a second of its name as a copy's", put);
                restored = files_bin_restore(bin, named, back) == FILES_DID && string_equals((string_address)back, (string_address)path) &&
                           browser_there((string_address) "mv/dst/a b.txt") && !browser_there((string_address) "bin/info/a b.txt.trashinfo") &&
                           !browser_there((string_address) "bin/files/a b.txt");
                again = files_bin_restore(bin, second, back) == FILES_DID && browser_there((string_address) "mv/dst/a b.copy.txt");
                check("browser: put back, a thing in the bin is where it was and its info is gone; where its name is taken it has a free one", restored && again);
        }

        // A move on one filesystem: a rename. Then back into the folder it came from, where its name is free again.
        count = browser_sources(paths, sizeof(paths), move_these, 1);
        moved = files_move_begin(&job, FILES_MOVE_MOVE, browser_open_folder((string_address) "mv/dst2"), paths, count);
        browser_move_run(&job);
        moved = moved && job.state == FILES_JOB_DONE && !browser_there((string_address) "mv/dst/d") && browser_size((string_address) "mv/dst2/d/sub/y") == 7 &&
                job.bytes_done == 0 && job.items_done == 1;
        files_move_end(&job);
        count = browser_sources(paths, sizeof(paths), back_these, 1);
        moved = moved && files_move_begin(&job, FILES_MOVE_MOVE, browser_open_folder((string_address) "mv/src"), paths, count);
        browser_move_run(&job);
        moved = moved && job.state == FILES_JOB_DONE && browser_there((string_address) "mv/src/d") && !browser_there((string_address) "mv/src/d.copy") && job.items_done == 0;
        files_move_end(&job);
        check("browser: a move on one filesystem is a rename of the whole folder, and one into the folder it is already in does nothing", moved);

        // Into itself: refused, nothing made.
        count = browser_sources(paths, sizeof(paths), inside_these, 1);
        inside = files_move_begin(&job, FILES_MOVE_COPY, browser_open_folder((string_address) "mv/src/d/sub"), paths, count);
        browser_move_run(&job);
        inside = inside && job.state == FILES_JOB_FAILED && job.error == -FILES_MOVE_INSIDE && !browser_there((string_address) "mv/src/d/sub/d");
        files_move_end(&job);
        check("browser: a folder copied into a folder inside itself is refused before anything of it is made", inside);

        // A cancel after the first piece: no half file left.
        count = browser_sources(paths, sizeof(paths), cancel_these, 1);
        cancelled = files_move_begin(&job, FILES_MOVE_COPY, browser_open_folder((string_address) "mv/dst2"), paths, count);
        while (files_move_step(&job) && !job.bytes_done)
                ;
        files_move_cancel(&job);
        browser_move_run(&job);
        cancelled = cancelled && job.state == FILES_JOB_CANCELLED && job.bytes_done > 0 && job.bytes_done < 144 * 65536 && !browser_there((string_address) "mv/dst2/big");
        files_move_end(&job);
        check("browser: a copy cancelled between two pieces stops there and leaves no half of the file it was in", cancelled);

        // On its own thread, waited for.
        count = browser_sources(paths, sizeof(paths), cancel_these, 1);
        threaded = files_move_begin(&job, FILES_MOVE_COPY, browser_open_folder((string_address) "mv/dst2"), paths, count) && files_move_start(&job);
        for (int waited = 0; files_move_running(&job) && waited < 500; waited++)
                browser_wait_a_tick();
        threaded = threaded && job.state == FILES_JOB_DONE && browser_size((string_address) "mv/dst2/big") == 144 * 65536;
        files_move_end(&job);
        check("browser: a copy runs to its end on a thread of its own", threaded);

        // A measure (a properties sheet's size): what a folder holds counted, nothing made or moved.
        count = browser_sources(paths, sizeof(paths), inside_these, 1);
        inside = files_move_begin(&job, FILES_MOVE_MEASURE, browser_open_folder((string_address) "mv/dst2"), paths, count);
        browser_move_run(&job);
        inside = inside && job.state == FILES_JOB_DONE && job.items_total == 4 && job.bytes_total == 12 && job.items_done == 0 &&
                 !browser_there((string_address) "mv/dst2/d.copy") && browser_there((string_address) "mv/src/d/sub/y");
        files_move_end(&job);
        check("browser: a measure counts what a folder holds (its folders, its files, their bytes) and makes and moves nothing", inside);
        browser_remove_in((string_address) "mv");
}

/*
        The folder tree (files_tree_*): a path revealed opens each folder down it from the root, a node's folders are its folders alone
        in name order (no file, no link to a folder, no hidden one), the path of a node is the path it was made from, and a node shut
        lets go of everything under it.
*/
static fn browser_tree(void)
{
        static struct files_tree tree;
        p8 path[FILES_PATH], made[FILES_PATH];
        p32 at, sub;
        b32 revealed, children, shut;

        browser_folder((string_address) "tr");
        browser_folder((string_address) "tr/b");
        browser_folder((string_address) "tr/a");
        browser_folder((string_address) "tr/.hidden");
        browser_folder((string_address) "tr/a/deep");
        browser_file((string_address) "tr/file", 3, 0);
        browser_link((string_address) "a", (string_address) "tr/link");
        browser_path(path, (string_address) "tr/a/deep");
        if (!files_tree_begin(&tree))
        {
                check("browser: the tree's memory is mapped", false);
                return;
        }
        at = files_tree_reveal(&tree, path);
        revealed = at != FILES_NO && files_tree_path(&tree, at, made) && string_equals((string_address)made, (string_address)path) &&
                   tree.node[0].open;
        browser_path(path, (string_address) "tr");
        sub = files_tree_reveal(&tree, path);
        children = sub != FILES_NO && tree.node[sub].open && tree.node[sub].has == 2 && sub + 2 < tree.count &&
                   tree.node[sub + 1].length == 1 && files_tree_name(&tree, sub + 1)[0] == 'a' && tree.node[sub + 1].open &&
                   tree.node[sub + 2].depth == tree.node[sub + 1].depth + 1 && files_tree_name(&tree, files_tree_after(&tree, sub + 1))[0] == 'b';
        files_tree_shut(&tree, sub);
        shut = !tree.node[sub].open && files_tree_after(&tree, sub) == sub + 1 && (sub + 1 >= tree.count || tree.node[sub + 1].depth <= tree.node[sub].depth);
        check("browser: the tree opens each folder down a path from the root, a node's folders are its folders in name order (no file, no link, no hidden one), its path is the path, and shut it lets go of what is under it",
              revealed && children && shut);
        // Multi-root sidebar: common folders (Home, Downloads, Pictures) above storage drives (/ + mounted drives), all collapsed by default,
        // longest-prefix reveal never force-expands /, and files_tree_find finds visible nodes without expanding anything.
        {
                static const char mountinfo[] =
                    "20 1 0:19 / /proc rw,nosuid - proc proc rw\n"
                    "21 1 8:1 / / rw - ext4 /dev/sda1 rw\n"
                    "22 21 8:2 / /boot/efi rw - vfat /dev/sda2 rw\n"
                    "23 21 0:25 / /run rw - tmpfs tmpfs rw\n"
                    "24 21 8:17 / /mnt/usb rw - exfat /dev/sdb1 rw\n"
                    "25 21 259:1 / /run/media/u/My\\040Drive rw - ext4 /dev/nvme1n1p1 rw\n"
                    "26 21 8:17 / /mnt/usb rw - exfat /dev/sdb1 rw\n"
                    "27 21 7:0 / /mnt/loop rw - squashfs /dev/loop0 ro\n";
                p8 home[FILES_PATH], sub_path[FILES_PATH];
                p32 hnode, dnode, snode;
                b32 roots_ok, reveal_ok, find_ok;

                browser_path(home, (string_address) "tr");
                browser_path(sub_path, (string_address) "tr/a");
                files_tree_populate(&tree, home, (const p8 address_to)mountinfo, sizeof(mountinfo) - 1);
                // Expected depth-0 roots: Home, Downloads, Pictures, /, usb, My Drive (6 roots, all collapsed).
                roots_ok = tree.count == 6 &&
                           tree.node[0].kind == FILES_NODE_PLACE && tree.node[0].place == FILES_PLACE_HOME && !tree.node[0].open &&
                           tree.node[1].kind == FILES_NODE_PLACE && tree.node[1].place == FILES_PLACE_DOWNLOADS &&
                           tree.node[2].kind == FILES_NODE_PLACE && tree.node[2].place == FILES_PLACE_PICTURES &&
                           tree.node[3].kind == FILES_NODE_DRIVE && tree.node[3].place == FILES_PLACE_ROOT && !tree.node[3].open &&
                           tree.node[4].kind == FILES_NODE_DRIVE && string_equals((string_address)files_tree_name(&tree, 4), (string_address) "usb") &&
                           tree.node[5].kind == FILES_NODE_DRIVE && string_equals((string_address)files_tree_name(&tree, 5), (string_address) "My Drive") &&
                           files_tree_path(&tree, 5, made) && string_equals((string_address)made, (string_address) "/run/media/u/My Drive");
                hnode = files_tree_reveal(&tree, home);
                dnode = files_tree_find(&tree, (const p8 address_to) "/");
                reveal_ok = hnode == 0 && !tree.node[0].open && dnode == 3 && !tree.node[3].open;
                snode = files_tree_reveal(&tree, sub_path);
                reveal_ok = reveal_ok && snode != FILES_NO && tree.node[0].open && !tree.node[3].open &&
                            files_tree_path(&tree, snode, made) && string_equals((string_address)made, (string_address)sub_path);
                files_tree_shut(&tree, 0);
                find_ok = files_tree_find(&tree, sub_path) == FILES_NO && !tree.node[0].open && files_tree_find(&tree, home) == 0;
                check("browser: multi-root tree puts common folders above root and mounted drives, keeps / collapsed, and find never re-opens shut nodes",
                      roots_ok && reveal_ok && find_ok);
        }
        files_tree_end(&tree);
        browser_remove_in((string_address) "tr");
}

/*
        Find in a folder (files_model_filter): only the names that hold the text are shown (letters of either case alike), in the order
        asked for, what is chosen staying chosen; none shows every entry again, and another folder shown has no filter.
*/
static fn browser_filter(struct files_model address_to m)
{
        b32 kept, again, gone;

        browser_folder((string_address) "fl");
        browser_file((string_address) "fl/Alpha.txt", 1, 0);
        browser_file((string_address) "fl/beta.TXT", 2, 0);
        browser_file((string_address) "fl/gamma", 3, 0);
        browser_go(m, (string_address) "fl");
        files_model_choose(m, browser_at(m, "gamma"), FILES_CHOOSE_ONLY);
        files_model_filter(m, (const p8 address_to) "txt", 3);
        kept = browser_is(m, "Alpha.txt beta.TXT ") && files_model_count(m) == 2 && m->shown.count == 3 && m->chosen_count == 1;
        files_model_filter(m, (const p8 address_to) "ALP", 3);
        kept = kept && browser_is(m, "Alpha.txt ") && m->cursor == 0;
        files_model_filter(m, (const p8 address_to) "", 0);
        again = browser_is(m, "Alpha.txt beta.TXT gamma ") && files_model_chosen(m, browser_at(m, "gamma"));
        files_model_filter(m, (const p8 address_to) "zzz", 3);
        gone = files_model_count(m) == 0 && !files_model_entry(m, 0);
        browser_go(m, (string_address) "");
        gone = gone && !m->filter_length && files_model_count(m) == m->shown.count;
        check("browser: a find keeps the rows to the names that hold its text, either case alike, what is chosen staying chosen; none shows them all, and another folder has none",
              kept && again && gone);
        browser_remove_in((string_address) "fl");
}

static fn browser_asks(void)
{
        static const p8 hostile[] = "asks/b\nad";
        long mapped = ui_map(sizeof(struct files_big));
        struct files_model address_to m;
        char names[256];
        b32 said, asked, again, begun, clean = true;
        p32 count;

        if (mapped < 0 && mapped > -4096)
        {
                check("browser: the window's memory is mapped", false);
                return;
        }
        files.big = (struct files_big address_to)mapped;
        m = &files.big->model;
        files_model_open(m);
        browser_folder((string_address) "asks");
        browser_file((string_address) "asks/a", 1, 0);
        browser_file((string_address)hostile, 1, 0);
        browser_file((string_address) "asks/c", 1, 0);
        browser_file((string_address) "asks/d", 1, 0);
        browser_file((string_address) "asks/e", 1, 0);
        browser_file((string_address) "asks/kept", 1, 0);
        browser_go(m, (string_address) "asks");
        files_model_choose(m, 0, FILES_CHOOSE_ONLY);
        files_model_choose(m, 4, FILES_CHOOSE_RANGE);
        count = files_ask_names(names, sizeof(names));
        for (const char address_to at = names; *at; at++)
                clean = clean && (unsigned char)*at >= 0x20;
        said = count == 5 && clean && string_equals((string_address)names, (string_address) "a, b\xe2\x90\x8a" "ad, c +2");
        check("browser: a delete's question says how many would go and the first three of their names as words from outside, with how many more after them", said);
        files_delete();
        asked = files.armed && files.big->job.state == FILES_JOB_IDLE && files.big->note[0] && browser_there((string_address) "asks/a") &&
                string_equals((string_address)files.big->note, (string_address) "Delete 5 for good: a, b\xe2\x90\x8a" "ad, c +2? Shift+Delete again does it, Escape leaves them");
        check("browser: Delete asks first: the foot has the question, and nothing is removed by the asking", asked);
        // The listing changes after the question (a reload counts its revision up): Delete asks again and removes nothing.
        files_model_reload(m);
        browser_read(m, 64);
        files_model_choose(m, 0, FILES_CHOOSE_ONLY);
        files_delete();
        again = files.armed && files.big->job.state == FILES_JOB_IDLE && browser_there((string_address) "asks/a") &&
                string_equals((string_address)files.big->note, (string_address) "Delete 1 for good: a? Shift+Delete again does it, Escape leaves them");
        files_delete();
        begun = !files.armed && files.big->job.state == FILES_JOB_RUNNING && files.big->job.count == 1;
        while (files_job_step(m, &files.big->job, 4))
                ;
        begun = begun && files.big->job.state == FILES_JOB_DONE && files.big->job.removed == 1 && !browser_there((string_address) "asks/a") &&
                browser_there((string_address) "asks/c") && browser_there((string_address) "asks/kept");
        check("browser: a listing that changed since the question is asked about again, and Delete again on the listing asked of removes what was asked and no more",
              again && begun);
        files_job_end(&files.big->job);
        // A work tree: an index that holds kept.c and sub/x, a .gitignore of *.o and build/; the folder's names marked as git sees them.
        {
                static p8 index[256];
                static const char ignore[] = "# built\n*.o\nbuild/\n!keep.o\n";
                const char address_to tracked[2] = {"kept.c", "sub/x"};
                positive used = 12;
                p8 path[FILES_PATH], at_path_check[FILES_PATH];
                bipolar out;
                b32 marked;

                memory_zero(index, sizeof(index));
                memory_copy(index, "DIRC\0\0\0\2\0\0\0\2", 12);
                for (int k = 0; k < 2; k++)
                {
                        positive length = string_length((string_address)tracked[k]);

                        index[used + 61] = (p8)length;
                        memory_copy(index + used + 62, tracked[k], length);
                        used += (62 + length + 8) & ~(positive)7;
                }
                browser_folder((string_address) "gt");
                browser_folder((string_address) "gt/.git");
                browser_folder((string_address) "gt/sub");
                browser_folder((string_address) "gt/build");
                browser_file((string_address) "gt/kept.c", 1, 0);
                browser_file((string_address) "gt/new.c", 1, 0);
                browser_file((string_address) "gt/a.o", 1, 0);
                browser_path(path, (string_address) "gt/.git/index");
                out = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
                if (out >= 0)
                        system_write_once((b32)out, index, used + 20), system_close(out);
                browser_path(path, (string_address) "gt/.gitignore");
                out = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
                if (out >= 0)
                        system_write_once((b32)out, ignore, sizeof(ignore) - 1), system_close(out);
                m->hidden = true;
                browser_go(m, (string_address) "gt");
                marked = files_git_marks(m, files_git_check_state);
#define GIT_MARK(name) files_git_check_state[m->shown.order[browser_at(m, name)]]
                marked = marked && GIT_MARK("kept.c") == FILES_GIT_TRACKED && GIT_MARK("sub") == FILES_GIT_TRACKED && GIT_MARK("new.c") == FILES_GIT_UNTRACKED &&
                         GIT_MARK("a.o") == FILES_GIT_IGNORED && GIT_MARK("build") == FILES_GIT_IGNORED && GIT_MARK(".git") == FILES_GIT_NONE &&
                         GIT_MARK(".gitignore") == FILES_GIT_UNTRACKED;
#undef GIT_MARK
                // A listing read again counts its listings up, so marks kept against one are worked out again even where the names swapped
                // for others of the same lengths (kept.c gone and kepu.c new: the same count and the same bytes of names).
                {
                        p32 listed = m->listed;
                        b32 fresh_marks;

                        browser_path(path, (string_address) "gt/kept.c");
                        browser_path(at_path_check, (string_address) "gt/kepu.c");
                        system_call_5(syscall(renameat2), AT_FDCWD, (positive)path, AT_FDCWD, (positive)at_path_check, 0);
                        files_model_reload(m);
                        browser_read(m, 64);
                        fresh_marks = m->listed != listed && files_git_marks(m, files_git_check_state) &&
                                      files_git_check_state[m->shown.order[browser_at(m, "kepu.c")]] == FILES_GIT_UNTRACKED;
                        check("browser: a listing read again counts listed up, and marks worked out again see a name swapped for one of its length untracked", fresh_marks);
                }
                // A hostile ignore file: sixteen KiB of wildcard lines that backtrack, and a name line after them; the marks come back, in the glob
                // steps' budget, and the name line still ignores its name.
                {
                        static char hostile[FILES_GIT_IGNORE_MOST];
                        positive used = 0;
                        b32 bounded;

                        while (used + 40 < sizeof(hostile) - 16)
                        {
                                memory_copy(hostile + used, "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b\n", 33);
                                used += 33;
                        }
                        memory_copy(hostile + used, "new.c\n", 6), used += 6;
                        browser_path(path, (string_address) "gt/.gitignore");
                        out = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
                        if (out >= 0)
                                system_write_once((b32)out, hostile, used), system_close(out);
                        files_model_reload(m);
                        browser_read(m, 64);
                        bounded = files_git_marks(m, files_git_check_state) && files_git_check_state[m->shown.order[browser_at(m, "new.c")]] == FILES_GIT_IGNORED;
                        check("browser: a hostile .gitignore of backtracking wildcard lines is read in the marks' budget, and its name lines still ignore", bounded);
                }
                browser_go(m, (string_address) "asks");
                check("browser: in a work tree a name the index holds is tracked (a folder by what is under it), one a .gitignore matches ignored (a folder by a pattern with its /), any other untracked, .git none",
                      marked && !files_git_marks(m, files_git_check_state));
        }
        files_model_close(m);
}

// The model names nothing of the kit and nothing of a window: outside its comments, its source has none of their words.
static fn browser_apart(void)
{
        static const char address_to const words[] = {"ui_", "struct ui", "window", "compositor", "font_", "language", "WORDS"};
        positive size = 0;
        const p8 address_to text = (const p8 address_to)compositor_map_file((string_address) "src/canvas/ui/files_model.c", address_of size);
        b32 apart = true, block = false;
        positive lines = 0;

        if (!text || !size)
        {
                log_direct(str("  browser: the model's source is not here to be read (run from the tree's root): NOT RUN\n"));
                return;
        }
        for (positive at = 0; at < size;)
        {
                positive stop = at, code;

                while (stop < size && text[stop] != '\n')
                        stop++;
                // What of the line is code: none of it inside a block comment, and nothing after a // on it.
                code = at;
                while (code < stop)
                {
                        if (block)
                        {
                                if (code + 1 < stop && text[code] == '*' && text[code + 1] == '/')
                                        block = false, code++;
                                code++;
                                continue;
                        }
                        if (code + 1 < stop && text[code] == '/' && text[code + 1] == '*')
                        {
                                block = true, code += 2;
                                continue;
                        }
                        if (code + 1 < stop && text[code] == '/' && text[code + 1] == '/')
                                break;
                        for (positive which = 0; which < sizeof(words) / sizeof(words[0]); which++)
                        {
                                positive length = string_length((string_address)words[which]);

                                if (code + length <= stop && !memory_compare((address_any)(text + code), (address_any)words[which], length))
                                        apart = false;
                        }
                        code++;
                }
                lines++;
                at = stop + 1;
        }
        check("browser: the model's code names nothing of the kit, a window, a font or a language: it is apart from the view", apart && lines > 300);
}

b32 main(void)
{
        static struct files_model model;
        p8 digits[24];

        digits[positive_into(digits, (positive)system_call_1(syscall(getpid), 0))] = end;
        string_copy_bounded(browser_base, (string_address) "/tmp/mw-browser.", sizeof(browser_base));
        string_append_bounded(browser_base, digits, sizeof(browser_base));
        browser_remove(browser_base);
        system_call_3(syscall(mkdirat), AT_FDCWD, (positive)browser_base, 0755);
        if (!files_model_open(address_of model))
        {
                check("browser: the model's memory is mapped", false);
                return test_report(null);
        }
        browser_order(address_of model);
        browser_states(address_of model);
        browser_names_hostile(address_of model);
        browser_links(address_of model);
        browser_history(address_of model);
        browser_typed(address_of model);
        browser_trail(address_of model);
        browser_kept(address_of model);
        browser_spell(address_of model);
        browser_changes(address_of model);
        browser_many(address_of model);
        browser_ops(address_of model);
        browser_moves();
        browser_tree();
        browser_filter(address_of model);
        browser_asks();
        browser_apart();
        files_model_close(address_of model);
        browser_remove(browser_base);
        return test_report(null);
}
#endif /* CHECK_browser */

