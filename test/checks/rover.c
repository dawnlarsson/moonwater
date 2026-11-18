/*
        test/checks/rover.c: rover, Moonwater's rules (src/rover).

        A run of test/checks.c, which includes this file where the run stood: it is compiled only as part of that file,
        with its statics and macros, and never on its own. Both sections are freestanding, built as test/checks.c is:
        gcc -O2 -static -nostdlib -nostartfiles -march=x86-64 -w -DCHECK_rover -fwhole-program -o OUT test/checks.c
        (sh test/run rover builds it with its freestanding_check), and -DBENCH_rover the same way for the timing.

        CHECK_rover holds the language to src/rover/README.md: the schema against the code it was generated from, the
        round trip (a canonical text prints back byte for byte and its table compiles back byte for byte), the one
        spelling of every literal, a refusal with its line for each thing the language is not, the load-time check
        against every byte of a table flipped, and the evaluator's answers: filters, else, and and or, between over
        midnight, calls, limit, once, off, when edges and where each rule is placed. BENCH_rover is nanoseconds per
        event for the rules a machine has, and what loading a file costs.
*/

#if defined(CHECK_rover) || defined(BENCH_rover)
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
#ifdef BENCH_rover
//      The bench times the walker and the C beside it in one process, a trial each in turn.
static volatile bool rover_bench_walker = true;
#define ROVER_WALKER_ASKED rover_bench_walker
#endif
#include "../../src/rover/compile.c"

static struct rover_build rover_one;
static struct rover_build rover_two;
static struct rover_table rover_view;
static struct rover_run rover_runs[ROVER_RULES_MOST];
static p64 rover_page_store[ROVER_PAGES][ROVER_PAGE_BYTES / 8];
static const p8 address_to rover_pages[ROVER_PAGES] = {(const p8 address_to)rover_page_store[0],
                                                       (const p8 address_to)rover_page_store[1],
                                                       (const p8 address_to)rover_page_store[2]};
static p8 rover_printed[ROVER_TABLE_MOST];

static bool rover_compiled(const char address_to text)
{
        struct rover_refusal refusal;

        memory_zero(rover_runs, sizeof(rover_runs));
        if (!rover_compile(&rover_one, (const p8 address_to)text, string_length((string_address)text)))
        {
                string_format(log, "  rover: line %p: %s\n", (positive)rover_one.line, rover_one.why);
                log_flush();
                return false;
        }
        return rover_check(rover_built(&rover_one), rover_one.size, &rover_view, &refusal);
}

static fn rover_set(const char address_to name, b64 number, const char address_to word)
{
        const struct rover_state_row address_to state =
            rover_state_named((const p8 address_to)name, string_length((string_address)name));
        p8 address_to page = (p8 address_to)rover_page_store[state->page];

        if (word)
        {
                positive length = string_length((string_address)word);

                page[state->offset] = (p8)length;
                memory_copy(page + state->offset + 1, word, length + 1);
                return;
        }
        b32 four = (b32)number;
        memory_copy(page + state->offset, &four, 4);
}

static struct rover_event rover_event_of(const char address_to name, const char address_to field)
{
        struct rover_event event;
        const struct rover_event_row address_to row = null;

        memory_zero(&event, sizeof(event));
        for (positive at = 0; at < ROVER_EVENT_ROWS; at++)
                if (string_equals((string_address)rover_event_rows[at].name, (string_address)name))
                        row = &rover_event_rows[at];
        event.id = row ? row->id : 0;
        event.when = 1000000000ull;
        event.realtime = 4 * 86400 + 12 * 3600; // a Monday at noon
        if (field)
        {
                event.count = 1;
                event.field[0].text = (const p8 address_to)field;
                event.field[0].length = (p32)string_length((string_address)field);
        }
        return event;
}

//      Which rule an event fires where, as "NAME:then" or "NAME:else" joined by spaces; "" for nothing.
static p8 rover_answer[512];

static const p8 address_to rover_fires(const struct rover_event address_to event, p8 place)
{
        struct rover_hit hits[32];
        positive made = rover_fire(&rover_view, rover_runs, rover_pages, event, place, hits, 32);
        struct rover_text text = {rover_answer, 0, sizeof(rover_answer) - 1, false};

        for (positive at = 0; at < made; at++)
        {
                if (at)
                        rover_puts(&text, " ");
                rover_pool_put(&text, &rover_view, rover_view.rule[hits[at].rule].name);
                rover_puts(&text, hits[at].otherwise ? ":else" : ":then");
        }
        rover_answer[text.used] = 0;
        return rover_answer;
}

static bool rover_same(const p8 address_to got, const char address_to want)
{
        if (string_equals((string_address)got, (string_address)want))
                return true;
        string_format(log, "  rover: got \"%s\", want \"%s\"\n", got, want);
        log_flush();
        return false;
}

static p8 rover_print_text[ROVER_TABLE_MOST];

static const p8 address_to rover_printed_of(void)
{
        struct rover_text text = {rover_print_text, 0, sizeof(rover_print_text) - 1, false};

        rover_print(&text, &rover_view);
        rover_print_text[text.used] = 0;
        return rover_print_text;
}
//      In the shared block, so BENCH_rover's section names the routine its key rows time as well.
/*
        memory_first_of_either_u32, lib.c's scan of rover's keys, against the loop: every count from 0 to 100, every
        place of the first word that is either value and none, the match the first value or the second, the words
        after it matching or not, and the words ending against a guard page, so a word read past the count faults. On
        x86_64 with AVX2 and without (SSE2), on arm64 with NEON, and riscv64's one body.
*/
static fn rover_check_first_of_either(void)
{
        unsigned int address_to pages = (unsigned int address_to)memory(2 * 4096);
        bool guarded = pages != null, answers = true;

        if (guarded)
                guarded = system_call_3(syscall(mprotect), (positive)((p8 address_to)pages + 4096), 4096, 0) == 0;
        check("first of either: guard page", guarded);
        if (!guarded)
                return;
#if X64
        p8 wide2 = cpu_has_avx2;
        for (positive tier = 0; tier < 2; tier++)
        {
                cpu_has_avx2 = tier == 0 ? wide2 : 0;
#else
        for (positive tier = 0; tier < 1; tier++)
        {
#endif
                unsigned int address_to ends = pages + 1024;

                for (positive count = 0; count <= 100; count++)
                        for (b64 place = -1; place < (b64)count; place++)
                                for (positive shape = 0; shape < 4; shape++)
                                {
                                        unsigned int address_to words = ends - count;
                                        unsigned int first = 0x00020039u, second = ROVER_KEY_ANY;
                                        positive want = count;

                                        for (positive i = 0; i < count; i++)
                                                words[i] = 0x00060000u + (unsigned int)i;
                                        if (place >= 0)
                                        {
                                                words[place] = shape & 1 ? second : first;
                                                for (positive i = (positive)place + 1; i < count && (shape & 2); i++)
                                                        words[i] = i & 1 ? first : second;
                                                want = (positive)place;
                                        }
                                        answers = answers && memory_first_of_either_u32(words, count, first, second) == want;
                                }
#if X64
        }
        cpu_has_avx2 = wide2;
#else
        }
#endif
        check("first of either: the first word that is either value, or the count", answers);
}

#endif

#ifdef CHECK_rover

//      Canonical texts: each prints back as itself, and its table compiles back as itself.
static const char address_to const rover_canonical[] = {
    "# Sleep when the lid closes on battery.\n"
    "rule lid-sleep\n"
    "\tabout \"Sleep when the lid closes on battery\"\n"
    "\ton lid close\n"
    "\tif power.source == battery\n"
    "\tif display.external == 0\n"
    "\t# the screen still shows on the monitor\n"
    "\tdo suspend\n"
    "\telse\n"
    "\tdo display off\n"
    "\n"
    "rule game-latency\n"
    "\ton fullscreen on steam_app_*\n"
    "\tdo latency 20us\n"
    "\n"
    "rule late-dim\n"
    "\ton lid open\n"
    "\tif clock between 22:00 06:00\n"
    "\tdo backlight 30%\n"
    "\n"
    "rule power\n"
    "\ton key power\n"
    "\tplace kernel\n"
    "\tdo poweroff\n",

    "# The session bus, which the desktop's programs need.\n"
    "service bus\n"
    "\tabout \"The session bus\"\n"
    "\trun /sessionbus\n"
    "\tready /run/moonwater/bus\n"
    "\tlog /run/moonwater/bus.log\n"
    "\tif canvas == on\n"
    "\n"
    "service compositor\n"
    "\trun /compositor -u 3 \"two words\"\n"
    "\tneeds bus\n"
    "\tready /run/moonwater/windows\n"
    "\trestart 0s 1s\n"
    "\tgive-up 3 under 10s\n"
    "\tif canvas == on and display.count > 0\n"
    "\n"
    "service spare\n"
    "\trun /bin/true\n"
    "\toff\n"
    "\n"
    "rule desktop-rest\n"
    "\ton service failed compositor\n"
    "\tdo desktop rest\n"
    "\n"
    "rule start-audio\n"
    "\ton service up bus\n"
    "\tdo start audio\n"
    "\tdo restart spare\n"
    "\n"
    "rule launcher\n"
    "\ton chord super+space\n"
    "\tlimit 250ms\n"
    "\tdo open launcher\n"
    "\n"
    "rule terminal\n"
    "\ton chord ctrl+alt+t\n"
    "\tonce\n"
    "\tdo open terminal\n"
    "\n"
    "rule quiet-night\n"
    "\twhen battery.percent < 10% and power.source == battery or lid == closed\n"
    "\tdo log \"battery low\"\n"
    "\telse\n"
    "\tdo log \"battery back\"\n"
    "\n"
    "rule names\n"
    "\ton window opened\n"
    "\tif string_length($app) > 12 or $app ~ \"*two words*\"\n"
    "\tif memory_count($app, 95) >= 2 and $app !~ steam_*\n"
    "\tif weekday between mon fri\n"
    "\trun notify-long $app \"a b\"\n"
    "\ttool logger -t rover $app\n"
    "\temit cue long-name\n"
    "\n"
    "rule cued\n"
    "\ton cue long-name\n"
    "\tplace user\n"
    "\toff\n"
    "\tdo notify \"a long name opened\"\n"
    "# the end of the file\n",

    "rule radios\n"
    "\ton dock off\n"
    "\tif headphone and tablet == off\n"
    "\tdo radio off wifi\n"
    "\tdo usb lock\n"
    "\tdo latency 1500ms\n"
    "\tdo latency off\n",

    "rule settings\n"
    "\ton button tray-double\n"
    "\tdo open settings\n"
    "\n"
    "rule mixer\n"
    "\ton button volume\n"
    "\tdo open mixer -q -a\n"
    "\n"
    "rule microphone\n"
    "\ton button mic\n"
    "\tdo open mixer -q -a -i\n"
    "\n"
    "rule pinned\n"
    "\ton button pin\n"
    "\tdo open $app\n"
    "\n"
    "rule first-pin\n"
    "\ton button pin 0\n"
    "\tdo notify \"the first pin\"\n",

    "rule game-power\n"
    "\ton fullscreen on steam_app_*\n"
    "\tset power performance\n"
    "\tset cpu boost on\n"
    "\n"
    "rule lid-quiet\n"
    "\ton lid close\n"
    "\tif power.source == battery\n"
    "\tset airplane on\n"
    "\tset brightness 30%\n"
    "\tset charge limit 80\n"
    "\telse\n"
    "\tset cpu smt off\n",
};

//      Text that is not canonical, and the one way it prints.
static const char address_to const rover_spellings[][2] = {
    {"rule a\n\ton lid close\n\tlimit 1.5s\n\tdo backlight 50.00%\n",
     "rule a\n\ton lid close\n\tlimit 1500ms\n\tdo backlight 50%\n"},
    {"rule a\n\ton lid close\n\tdo suspend   # now\n", "rule a\n\ton lid close\n\t# now\n\tdo suspend\n"},
    {"rule a\n\ton window opened \"firefox\"\n\tdo open \"files\"\n",
     "rule a\n\ton window opened firefox\n\tdo open files\n"},
    {"rule a\n\ton lid open\n\tif clock > 6:05\n\tdo backlight 12.5%\n",
     "rule a\n\ton lid open\n\tif clock > 06:05\n\tdo backlight 12.5%\n"},
    {"rule a\n\ton chord super+shift+ctrl+f4\n\tdo latency 0.5ms\n",
     "rule a\n\ton chord ctrl+shift+super+f4\n\tdo latency 500us\n"},
    {"rule a\r\n\ton lid close\r\n\tdo suspend\r\n", "rule a\n\ton lid close\n\tdo suspend\n"},
    {"\n\nrule a\n\n\ton lid close\n\n\tdo suspend\n\n\n", "rule a\n\ton lid close\n\tdo suspend\n"},
};

//      Text the language refuses: the line, and words of the reason.
static const struct
{
        const char address_to text;
        p32 line;
        const char address_to why;
} rover_refusals[] = {
    {"rule a\n  on lid close\n", 2, "space"},
    {"rule a\n\ton lid shut\n\tdo suspend\n", 2, "no event"},
    {"rule a\n\ton lid close\n\tif display.extrnal == 0\n\tdo suspend\n", 3, "display.extrnal is no state (display.external?)"},
    {"rule a\n\ton lid close\n\tif battery.percent > tall\n\tdo suspend\n", 3, "not a percent"},
    {"rule a\n\ton lid close\n\tdo backlight 120%\n", 3, "0% to 100%"},
    {"rule a\n\ton dock off\n\tdo radio on wifi\n", 3, "deliberate"},
    {"rule a\n\ton dock on\n\tdo usb allow\n", 3, "deliberate"},
    {"rule a\n\ton lid close\n\tplace kernel\n\tif display.external == 0\n\tdo suspend\n", 0, "desktop state"},
    {"rule a\n\ton fullscreen on\n\tplace kernel\n\tdo suspend\n", 0, "kernel never sees it"},
    {"rule a\n\twhen $app == x\n\tdo suspend\n", 2, "fields are no states"},
    {"rule a\n\ton cue loop\n\temit cue loop\n", 0, "own cue"},
    {"rule a\n\ton lid close\n\tdo suspend\n\tif lid == open\n", 4, "conditions come first"},
    {"rule a\n\ton lid close\n", 0, "nothing to do"},
    {"rule a\n\ton lid close\n\tdo suspend\n\telse\n", 0, "else with nothing"},
    {"rule a\n\ton lid close\n\tdo suspend\nrule a\n\ton lid open\n\tdo suspend\n", 4, "rule above"},
    {"rule a\n\ton lid close\n\t\tdo suspend\n", 3, "two tabs"},
    {"rule a\n\ton lid close\n\tif nosuch($app) > 1\n\tdo suspend\n", 3, "no routine"},
    {"rule a\n\ton window opened\n\tif string_length($app, 3) > 1\n\tdo suspend\n", 3, "count of arguments"},
    {"rule a\n\ton lid close\n\tif lid between open closed\n\tdo suspend\n", 3, "cannot be compared"},
    {"rule a\n\ton lid close\n\tif battery.percent ~ 10%\n\tdo suspend\n", 3, "cannot be compared"},
    {"rule a\n\ton lid close\n\tif lid\n\tdo suspend\n", 3, "no truth"},
    {"rule a\n\ton lid close\n\tif 3 == 4\n\tdo suspend\n", 3, "one side"},
    {"rule a\n\ton lid close\n\tif lid == open and\n\tdo suspend\n", 3, "nothing after"},
    {"rule a\n\ton lid close\n\tdo latency 2d\n", 3, "unit"},
    {"rule a\n\ton lid close\n\tdo latency 25h\n", 3, "longer than a day"},
    {"rule a\n\ton lid close\n\tif clock > 24:00\n\tdo suspend\n", 3, "00:00 to 23:59"},
    {"rule a\n\ton chord hyper+x\n\tdo suspend\n", 2, "modifier"},
    {"rule a\n\ton chord ctrl+frob\n\tdo suspend\n", 2, "no key"},
    {"rule a\n\ton lid close\n\trun ../x\n", 3, "no slash"},
    {"rule a\n\ton lid close\n\ttool nosuchtool x\n", 3, "shell's tools"},
    {"rule a\n\ton lid close\n\tlimit 0.5ms\n\tdo suspend\n", 3, "whole milliseconds"},
    {"rule a\n\ton lid close\n\tlimit 1s\n\tabout \"x\"\n\tdo suspend\n", 4, "out of order"},
    {"rule a\n\tdo suspend\n", 2, "before the rule's on"},
    {"rule a\n\ton lid close\n\ton lid open\n\tdo suspend\n", 3, "said twice"},
    {"rule A\n\ton lid close\n\tdo suspend\n", 1, "no rule name"},
    {"\ton lid close\n", 1, "before any rule or service"},
    {"rule a\n\ton lid close\n\tdo suspend\nservice b\n\trun /b\n", 4, "before its rules"},
    {"service a\n\tready /x\n", 2, "before the service's run"},
    {"service a\n\trun b\n", 2, "no absolute path"},
    {"service a\n\trun /a\n\tneeds b\n", 3, "no service above"},
    {"service a\n\trun /a\nservice a\n\trun /b\n", 3, "a service above"},
    {"service a\n\trun /a\n\tready /r\n\tneeds a\n", 4, "out of order"},
    {"service a\n\trun /a\n\trestart 1s 2s 3s 4s\n", 3, "one to three waits"},
    {"service a\n\trun /a\n\tgive-up 4 in 30s\n", 3, "give-up LIVES under"},
    {"service a\n\trun /a\n\tgive-up 40 under 30s\n", 3, "1 to 16 lives"},
    {"service a\n\trun /a\n\trestart 1.5us\n", 3, "whole milliseconds"},
    {"service a\n\trun /a\n\tif $app == x\n", 3, "fields are no states"},
    {"service a\n\tabout \"x\"\n", 0, "no run"},
    {"service a\n\trun /a\n\tfrob\n", 3, "no service statement"},
    {"rule a\n\ton button frob\n\tdo open x\n", 2, "is not one of launcher"},
    {"rule a\n\ton lid open\n\tset airplane off\n", 3, "airplane off is deliberate"},
    {"rule a\n\tevery 100ms\n\trun x\n", 0, "a second or longer"},
    {"rule a\n\tevery\n\trun x\n", 2, "takes one duration"},
    {"rule a\n\tat 25:00\n\trun x\n", 2, "00:00 to 23:59"},
    {"rule a\n\ton at\n\trun x\n", 0, "at takes its clock time"},
    {"rule a\n\tat 10:00\n\ton lid open\n\trun x\n", 3, "said twice"},
    {"rule a\n\ton lid open\n\tset airplane setting.canvas.gpu\n", 3, "airplane off is deliberate"},
    {"rule a\n\ton lid open\n\tset power turbo\n", 3, "is not one of performance"},
    {"rule a\n\ton lid open\n\tset nosuch 1\n", 3, "is no switch"},
    {"rule a\n\ton lid open\n\tset brightness\n", 3, "takes one value"},
    {"rule a\n\ton lid open\n\tset charge limit 10\n", 3, "outside the range"},
    {"rule a\n\ton lid close\n\tplace kernel\n\tif setting.canvas.gpu == on\n\tdo suspend\n", 0, "reads a setting"},
    {"rule a\n\ton lid close\n\tif setting.canvas.theme == purple\n\tdo suspend\n", 3, "is not one of light"},
    {"rule a\n\ton button volume\n\tdo open mixer -q -a -i -x\n", 3, "past an action's four arguments"},
    {"rule a\n\ton button volume\n\tdo open\n", 3, "open takes app, words"},
    {"on lid close\n", 1, "left edge"},
    {"rule a\n\ton lid close\n\tdo backlight\n", 3, "takes level"},
    {"rule a\n\ton lid close\n\tdo suspend now\n", 3, "takes no argument"},
    {"rule a\n\ton lid close extra\n\tdo suspend\n", 2, "no fields"},
    {"rule a\n\ton window opened\n\tif $nosuch == x\n\tdo suspend\n", 3, "no field"},
    {"rule a\n\ton window opened\n\tif string_length(string_length($app)) > 1\n\tdo suspend\n", 3, "another call"},
    {"rule a\n\ton lid close\n\tabout \"unclosed\n\tdo suspend\n", 3, "does not close"},
    {"rule a\n\ton lid close\n\tfrobnicate\n", 3, "no statement"},
    {"rule a\n\ton lid close\n\tdo suspend\n\telse\n\telse\n\tdo suspend\n", 5, "second else"},
    {"rule a\n\ton lid close\n\tif display.count == $app\n\tdo suspend\n", 3, "no field of lid close"},
    {"rule a\n\ton window opened\n\tif display.count == $app\n\tdo suspend\n", 3, "not of its comparison"},
};

static bool rover_holds_why(const char address_to want)
{
        positive length = string_length((string_address)want);

        for (positive at = 0; at + length <= rover_one.why_used; at++)
                if (!memory_compare(rover_one.why + at, want, length))
                        return true;
        return false;
}

//      The schema against the code it came from.
static fn rover_check_schema(void)
{
        p32 hash = 0x811c9dc5u;
        bool ids = true, kernel_names = true, calls = true, deliberate = true;
        p8 seen[ROVER_EVENTS];

        for (positive at = 0; at + 1 < sizeof(rover_schema_text); at++)
                hash = (hash ^ (p8)rover_schema_text[at]) * 0x01000193u;
        check("the schema's records hash to ROVER_SCHEMA_HASH", hash == ROVER_SCHEMA_HASH);

        memory_zero(seen, sizeof(seen));
        for (positive at = 0; at < ROVER_EVENT_ROWS; at++)
        {
                p16 id = rover_event_rows[at].id;

                if (!id || id >= ROVER_EVENTS || seen[id])
                        ids = false;
                else
                        seen[id] = 1;
        }
        check("every event id is one event's, below 128", ids);

        //      Spark's id is the kernel event's rover id, and the name is spark's with the underscore a space,
        //      but for the four keys.
        for (positive id = 1; id <= SPARK_BIND_EVENTS; id++)
        {
                const struct rover_event_row address_to row = rover_event_find(id);
                const char address_to spark = spark_bind_event_name[id - 1];
                p8 spelled[SPARK_BIND_NAME_MAX];
                positive length = string_length((string_address)spark);

                for (positive k = 0; k <= length; k++)
                        spelled[k] = spark[k] == '_' ? ' ' : (p8)spark[k];
                if (!row || row->source != ROVER_KERNEL)
                        kernel_names = false;
                else if (id > 4 && !string_equals((string_address)row->name, spelled))
                        kernel_names = false;
                else if (id <= 4 && memory_compare(row->name, "key ", 4))
                        kernel_names = false;
        }
        check("the kernel's events are spark's, by id and name", kernel_names);

        for (positive at = 0; at < ROVER_FUNCTIONS; at++)
                if (!rover_functions[at].routine || rover_functions[at].count != string_length(
                                                                                     (string_address)rover_functions[at].signature))
                        calls = false;
        check("every function has its body in userspace and its signature's count", calls);
        check("lib.c's routines are bound", ROVER_FUNCTIONS >= 40);

        for (positive at = 0; at < ROVER_PRIMITIVES; at++)
        {
                bool raising = string_equals((string_address)rover_primitives[at].name, "radio on") ||
                               string_equals((string_address)rover_primitives[at].name, "usb allow");

                if (raising != (rover_primitives[at].klass == ROVER_DELIBERATE))
                        deliberate = false;
        }
        check("the deliberate primitives are radio on and usb allow", deliberate);

        //      The integer ranges a routine takes are kept: unicode_width's variant is 0 or 1.
        for (positive at = 0; at < ROVER_FUNCTIONS; at++)
                if (string_equals((string_address)rover_functions[at].name, "unicode_width"))
                        check("unicode_width's variant is held to 0..1",
                              rover_functions[at].low[1] == 0 && rover_functions[at].high[1] == 1);
}

static fn rover_check_round_trip(void)
{
        for (positive at = 0; at < array_count(rover_canonical); at++)
        {
                const char address_to text = rover_canonical[at];
                bool made = rover_compiled(text);

                check("a canonical text compiles and passes the check", made);
                if (!made)
                        continue;
                check("a canonical text prints back as itself", rover_same(rover_printed_of(), text));
                memory_copy(rover_printed, rover_built(&rover_one), rover_one.size);
                positive size = rover_one.size;
                check("its printing compiles", rover_compile(&rover_two, rover_print_text,
                                                             string_length(rover_print_text)));
                check("its printing compiles to the same table, byte for byte",
                      rover_two.size == size && !memory_compare(rover_built(&rover_two), rover_printed, size));
        }
        for (positive at = 0; at < array_count(rover_spellings); at++)
        {
                bool made = rover_compiled(rover_spellings[at][0]);

                check("a text in another spelling compiles", made);
                if (made)
                        check("and prints in the one spelling", rover_same(rover_printed_of(), rover_spellings[at][1]));
                memory_copy(rover_printed, rover_built(&rover_one), rover_one.size);
                positive size = rover_one.size;
                if (made && rover_compile(&rover_two, (const p8 address_to)rover_spellings[at][1],
                                          string_length((string_address)rover_spellings[at][1])))
                        check("the two spellings make one table",
                              rover_two.size == size && !memory_compare(rover_built(&rover_two), rover_printed, size));
        }
}

static fn rover_check_refusals(void)
{
        for (positive at = 0; at < array_count(rover_refusals); at++)
        {
                const char address_to text = rover_refusals[at].text;
                bool made = rover_compile(&rover_one, (const p8 address_to)text, string_length((string_address)text));

                if (made || rover_one.line != rover_refusals[at].line || !rover_holds_why(rover_refusals[at].why))
                {
                        string_format(log, "  rover: refusal %p: made %p line %p: %s\n", at, (positive)made,
                                      (positive)rover_one.line, rover_one.why);
                        log_flush();
                }
                check("a text the language is not is refused, at its line, with the reason",
                      !made && rover_one.line == rover_refusals[at].line && rover_holds_why(rover_refusals[at].why));
        }
}

/*
        The check against a table with each byte flipped in turn: one it
        refuses is never read; one it passes must be one the evaluator and
        the printer walk inside the table. Neither may crash, which is the
        claim: the kernel walks whatever it passes without a check of its own.
*/
static fn rover_check_tamper(void)
{
        static p64 bytes_words[ROVER_TABLE_MOST / 8];
        p8 address_to bytes = (p8 address_to)bytes_words;
        struct rover_refusal refusal;
        struct rover_table view;
        positive size, refused = 0, passed = 0;
        bool walked = true;

        if (!rover_compiled(rover_canonical[1]))
                return;
        size = rover_one.size;
        for (positive at = 0; at < size; at++)
        {
                memory_copy(bytes, rover_built(&rover_one), size);
                bytes[at] ^= 0xff;
                if (!rover_check(bytes, size, &view, &refusal))
                {
                        refused++;
                        continue;
                }
                passed++;
                struct rover_event event = rover_event_of("window opened", "a_very_long_app_name");
                struct rover_hit hits[16];
                struct rover_text text = {rover_print_text, 0, sizeof(rover_print_text), false};
                struct rover_value values[ROVER_ARGUMENTS];

                memory_zero(rover_runs, sizeof(rover_runs));
                for (p16 id = 0; id < ROVER_EVENTS; id++)
                {
                        event.id = id;
                        for (p8 place = 0; place < ROVER_PLACES; place++)
                        {
                                positive made = rover_fire(&view, rover_runs, rover_pages, &event, place, hits, 16);

                                for (positive k = 0; k < made; k++)
                                        for (positive a = 0; a < hits[k].count; a++)
                                                rover_arguments(&view, (positive)hits[k].action + a, &event, rover_pages,
                                                                values);
                        }
                }
                rover_edges(&view, rover_runs, rover_pages, ~0ull, &event, ROVER_KERNEL, hits, 16);
                rover_print(&text, &view);
                if (text.full)
                        walked = false;
        }
        check("the check refuses most flipped bytes", refused > size / 2);
        check("a flipped table the check passes is walked and printed inside its bounds", walked);
        string_format(log, "  rover: %p of %p flipped bytes refused, %p passed and walked\n", refused, size, passed);

        //      The three that matter by name.
        memory_copy(bytes, rover_built(&rover_one), size);
        ((struct rover_head address_to)bytes)->schema ^= 1;
        check("a table of another schema is refused", !rover_check(bytes, size, &view, &refusal));
        check("a table whose size is not its counts' is refused",
              !rover_check(rover_built(&rover_one), size - 8, &view, &refusal));

        if (rover_compiled("rule a\n\ton dock off\n\tdo radio off wifi\n"))
        {
                memory_copy(bytes, rover_built(&rover_one), rover_one.size);
                rover_check(bytes, rover_one.size, &view, &refusal);
                ((struct rover_action address_to)view.action)[0].id = 8; // radio on
                check("a table that asks for a deliberate primitive is refused at load",
                      !rover_check(bytes, rover_one.size, &view, &refusal) &&
                          string_equals((string_address)refusal.why,
                                        "a deliberate primitive, which only a person may ask for"));
        }
        if (rover_compiled("rule a\n\ton lid close\n\tset airplane on\n"))
        {
                memory_copy(bytes, rover_built(&rover_one), rover_one.size);
                rover_check(bytes, rover_one.size, &view, &refusal);
                b64 off = 0;
                memory_copy((p8 address_to)view.pool + view.argument[view.action[0].argument].index, &off, 8);
                check("a table that sets a deliberate value of a switch is refused at load",
                      !rover_check(bytes, rover_one.size, &view, &refusal) &&
                          string_equals((string_address)refusal.why,
                                        "a deliberate value of a switch, which only a person may set"));
        }
        if (rover_compiled("rule a\n\ton lid close\n\tif lid == closed\n\tdo suspend\n"))
        {
                memory_copy(bytes, rover_built(&rover_one), rover_one.size);
                rover_check(bytes, rover_one.size, &view, &refusal);
                check("lid close's rule is the kernel's", view.rule[0].place == ROVER_KERNEL);
                struct rover_comparison address_to c = (struct rover_comparison address_to)view.comparison;
                c[0].a.page = ROVER_PAGE_DESKTOP;
                check("a kernel rule that reads a desktop state is refused at load",
                      !rover_check(bytes, rover_one.size, &view, &refusal));
        }
}

static fn rover_check_evaluator(void)
{
        struct rover_event event;

        memory_zero(rover_page_store, sizeof(rover_page_store));
        check("the first text compiles", rover_compiled(rover_canonical[0]));
        event = rover_event_of("lid close", null);
        rover_set("power.source", 1, null);
        check("lid close on battery sleeps, in the runner (it reads a desktop state)",
              rover_same(rover_fires(&event, ROVER_RUNNER), "lid-sleep:then"));
        check("and nothing of it runs in the kernel", rover_same(rover_fires(&event, ROVER_KERNEL), ""));
        rover_set("display.external", 1, null);
        check("with a screen plugged in it turns the panel off", rover_same(rover_fires(&event, ROVER_RUNNER),
                                                                            "lid-sleep:else"));
        rover_set("display.external", 0, null);
        rover_set("power.source", 0, null);
        check("on mains it turns the panel off", rover_same(rover_fires(&event, ROVER_RUNNER), "lid-sleep:else"));

        event = rover_event_of("fullscreen on", "steam_app_440");
        check("a game full screen holds the latency", rover_same(rover_fires(&event, ROVER_DESKTOP), "game-latency:then"));
        event = rover_event_of("fullscreen on", "firefox");
        check("another app's full screen is not the rule's event", rover_same(rover_fires(&event, ROVER_DESKTOP), ""));

        event = rover_event_of("lid open", null);
        event.realtime = 4 * 86400 + 23 * 3600 + 10 * 60;
        check("23:10 is between 22:00 and 06:00", rover_same(rover_fires(&event, ROVER_KERNEL), "late-dim:then"));
        event.realtime = 5 * 86400 + 5 * 3600 + 59 * 60;
        check("so is 05:59", rover_same(rover_fires(&event, ROVER_KERNEL), "late-dim:then"));
        event.realtime = 5 * 86400 + 6 * 3600 + 1 * 60;
        check("06:01 is not", rover_same(rover_fires(&event, ROVER_KERNEL), ""));
        //      The zone moves the clock: 21:30 UTC is 23:30 two hours east.
        b32 east = 7200;
        memory_copy((p8 address_to)rover_page_store[0] + ROVER_PAGE_ZONE, &east, 4);
        event.realtime = 5 * 86400 + 21 * 3600 + 30 * 60;
        check("the page's zone moves the clock", rover_same(rover_fires(&event, ROVER_KERNEL), "late-dim:then"));
        east = 0;
        memory_copy((p8 address_to)rover_page_store[0] + ROVER_PAGE_ZONE, &east, 4);

        event = rover_event_of("key power", null);
        check("the power key powers off, in the kernel", rover_same(rover_fires(&event, ROVER_KERNEL), "power:then"));

        //      The graph Settings draws: an if's no goes to the else, its yes to the next if.
        {
                struct rover_text text = {rover_print_text, 0, sizeof(rover_print_text) - 1, false};
                const char address_to want[] = {
                    "node rule=lid-sleep id=0 kind=event place=runner label=\"on lid close\" about=\"Sleep when the lid "
                    "closes on battery\"\n",
                    "node rule=lid-sleep id=2 kind=condition label=\"display.external == 0\"\n",
                    "edge rule=lid-sleep from=1 port=yes to=2\n", "edge rule=lid-sleep from=2 port=no to=4\n",
                    "edge rule=lid-sleep from=2 port=yes to=3\n",
                    "node rule=power id=1 kind=action branch=then label=\"do poweroff\"\n"};
                bool all = true;

                rover_graph(&text, &rover_view);
                rover_print_text[text.used] = 0;
                for (positive at = 0; at < array_count(want); at++)
                        if (!string_search(rover_print_text, (string_address)want[at]))
                        {
                                string_format(log, "  rover: the graph has no %s", want[at]);
                                all = false;
                        }
                check("the graph's nodes and edges are the text's", all);
        }

        //      STATE=VALUE, as moonwater rover test reads it: typed by the state, and a typo named.
        {
                p8 address_to writable[ROVER_PAGES] = {(p8 address_to)rover_page_store[0], (p8 address_to)rover_page_store[1],
                                                       (p8 address_to)rover_page_store[2]};
                struct rover_event when = rover_event_of("lid open", null);
                const char address_to set[] = {"battery.percent=42%", "window.app=firefox", "clock=23:10", "weekday=sat"};
                bool all = true;

                for (positive at = 0; at < array_count(set); at++)
                        all = all && rover_assign(&rover_one, writable, (const p8 address_to)set[at],
                                                  string_length((string_address)set[at]), &when);
                check("states are set by their types", all && rover_page_store[0][0] == 0);
                check("the clock and the weekday move the event's time",
                      when.realtime == 2 * 86400 + 23 * 3600 + 10 * 60); // 3 January 1970, a Saturday
                rover_one.why_used = 0;
                check("a state that is none is named, with the one it nearly is",
                      !rover_assign(&rover_one, writable, (const p8 address_to)"power.sourse=ac", 15, &when) &&
                          rover_holds_why("power.sourse is no state (power.source?)"));
                memory_zero(rover_page_store, sizeof(rover_page_store));
        }
        check("an event no rule has fires nothing", (event = rover_event_of("mute", null),
                                                     rover_same(rover_fires(&event, ROVER_KERNEL), "")));

        //      The second text: chords, limit, when edges, calls, patterns, weekdays, cues, off.
        check("the second text compiles", rover_compiled(rover_canonical[1]));

        //      Its services: what the runner reads off the record, with no text.
        {
                const struct rover_service address_to bus = rover_view.service;
                const struct rover_service address_to compositor = rover_view.service + 1;

                check("three services", rover_view.head->services == 3);
                check("a service's policy unless it says other is the script's",
                      bus->wait_ms[0] == 0 && bus->wait_ms[1] == 500 && bus->wait_ms[2] == 2000 && bus->lives == 4 &&
                          bus->short_ms == 30000);
                check("restart 0s 1s waits 1s after the third short life too",
                      compositor->wait_ms[0] == 0 && compositor->wait_ms[1] == 1000 && compositor->wait_ms[2] == 1000 &&
                          compositor->lives == 3 && compositor->short_ms == 10000);
                check("needs is the index of a service above", compositor->need[0] == 0 &&
                                                                    compositor->need[1] == ROVER_SERVICE_NONE);
                check("its run is its path and its words",
                      compositor->count == 4 &&
                          rover_bytes_are(rover_view.pool + rover_view.argument[compositor->argument].index + 2, 11,
                                          "/compositor") &&
                          rover_bytes_are(rover_view.pool + rover_view.argument[compositor->argument + 3].index + 2, 9,
                                          "two words"));
                rover_set("canvas", 0, null);
                check("a service whose if does not hold is not wanted", !rover_service_wanted(&rover_view, 0, rover_pages));
                rover_set("canvas", 1, null);
                check("and is when it holds", rover_service_wanted(&rover_view, 0, rover_pages));
                check("an if over two pages: no screen, not wanted", !rover_service_wanted(&rover_view, 1, rover_pages));
                rover_set("display.count", 1, null);
                check("a screen, wanted", rover_service_wanted(&rover_view, 1, rover_pages));
                check("a service that is off is never wanted", !rover_service_wanted(&rover_view, 2, rover_pages));
                rover_set("canvas", 0, null);
                rover_set("display.count", 0, null);
                struct rover_event failed = rover_event_of("service failed", "compositor");
                check("a compositor given up rests the desktop, in the runner",
                      rover_same(rover_fires(&failed, ROVER_RUNNER), "desktop-rest:then"));
        }
        event = rover_event_of("chord", null);
        event.count = 1;
        event.field[0].number = (b64)ROVER_CHORD_SUPER << 16 | 57;
        check("super+space opens the launcher", rover_same(rover_fires(&event, ROVER_DESKTOP), "launcher:then"));
        event.when += 100000000;
        check("again 100 ms later is inside its limit", rover_same(rover_fires(&event, ROVER_DESKTOP), ""));
        check("and is counted", rover_runs[2].dropped == 1); // the launcher, after the two service rules
        event.when += 200000000;
        check("300 ms later is past it", rover_same(rover_fires(&event, ROVER_DESKTOP), "launcher:then"));
        event.field[0].number = (b64)(ROVER_CHORD_CTRL | ROVER_CHORD_ALT) << 16 | 20;
        check("ctrl+alt+t opens a terminal", rover_same(rover_fires(&event, ROVER_DESKTOP), "terminal:then"));
        check("once: a second ctrl+alt+t fires nothing", rover_same(rover_fires(&event, ROVER_DESKTOP), ""));

        {
                struct rover_hit hits[8];
                struct rover_event edge = rover_event_of("", null);
                positive made;

                rover_set("battery.percent", 2000, null);
                rover_set("power.source", 1, null);
                rover_set("lid", 0, null);
                made = rover_edges(&rover_view, rover_runs, rover_pages, ~0ull, &edge, ROVER_KERNEL, hits, 8);
                check("a when rule that does not hold at load fires nothing", made == 0);
                rover_set("battery.percent", 900, null);
                made = rover_edges(&rover_view, rover_runs, rover_pages, 1ull << 2, &edge, ROVER_KERNEL, hits, 8);
                check("its rising edge fires its then actions", made == 1 && !hits[0].otherwise);
                made = rover_edges(&rover_view, rover_runs, rover_pages, 1ull << 2, &edge, ROVER_KERNEL, hits, 8);
                check("staying true fires nothing more", made == 0);
                rover_set("battery.percent", 5000, null);
                made = rover_edges(&rover_view, rover_runs, rover_pages, 1ull << 2, &edge, ROVER_KERNEL, hits, 8);
                check("its falling edge fires its else actions", made == 1 && hits[0].otherwise);
                rover_set("battery.percent", 900, null);
                made = rover_edges(&rover_view, rover_runs, rover_pages, 1ull << 2, &edge, ROVER_KERNEL, hits, 8);
                check("a second rise fires its then actions again", made == 1 && !hits[0].otherwise);
                made = rover_edges(&rover_view, rover_runs, rover_pages, 1ull << 5, &edge, ROVER_KERNEL, hits, 8);
                check("a change of a state it does not read is not looked at", made == 0);
                memory_zero(rover_runs, sizeof(rover_runs));
                rover_set("battery.percent", 5000, null);
                rover_set("lid", 1, null);
                made = rover_edges(&rover_view, rover_runs, rover_pages, ~0ull, &edge, ROVER_KERNEL, hits, 8);
                check("or binds looser than and: a closed lid alone holds", made == 1 && !hits[0].otherwise);
        }

        event = rover_event_of("window opened", "a_short");
        event.realtime = 4 * 86400 + 12 * 3600; // Monday
        check("a short name with one underscore fires nothing",
              rover_same(rover_fires(&event, ROVER_DESKTOP), ""));
        event = rover_event_of("window opened", "many_under_scores_here");
        check("a desktop event's rule is not the runner's", rover_same(rover_fires(&event, ROVER_RUNNER), ""));
        check("a long name with underscores runs the rule, in the desktop",
              rover_same(rover_fires(&event, ROVER_DESKTOP), "names:then"));
        event = rover_event_of("window opened", "steam_a_b_c_d_e_f");
        check("but not a steam one", rover_same(rover_fires(&event, ROVER_DESKTOP), ""));
        event = rover_event_of("window opened", "many_under_scores_here");
        event.realtime = 9 * 86400 + 12 * 3600; // Saturday
        check("nor on a Saturday", rover_same(rover_fires(&event, ROVER_DESKTOP), ""));
        event = rover_event_of("cue", "long-name");
        check("a rule that is off never fires", rover_same(rover_fires(&event, ROVER_RUNNER), ""));

        //      The arguments an owner is handed: a field's bytes, a constant's.
        {
                struct rover_value values[ROVER_ARGUMENTS];
                struct rover_hit hits[4];
                positive made, count;

                event = rover_event_of("window opened", "many_under_scores_here");
                made = rover_fire(&rover_view, rover_runs, rover_pages, &event, ROVER_DESKTOP, hits, 4);
                count = made ? rover_arguments(&rover_view, hits[0].action, &event, rover_pages, values) : 0;
                check("run's arguments are the script, the field and the word",
                      count == 3 && rover_bytes_are(values[0].text, values[0].length, "notify-long") &&
                          rover_bytes_are(values[1].text, values[1].length, "many_under_scores_here") &&
                          rover_bytes_are(values[2].text, values[2].length, "a b"));
                check("a hit's actions are its then run", made && hits[0].count == 3);
        }

        //      The desktop's buttons: one event, keyed on which, and open with the words a program is started with.
        check("the fourth text compiles", rover_compiled(rover_canonical[3]));
        event = rover_event_of("button", null);
        event.count = 3;
        event.field[0].number = 1; // volume
        check("the volume cell opens the mixer's popup", rover_same(rover_fires(&event, ROVER_DESKTOP), "mixer:then"));
        event.field[0].number = 4; // tray-double
        check("the clock pressed twice opens settings", rover_same(rover_fires(&event, ROVER_DESKTOP), "settings:then"));
        event.field[0].number = 5; // pin
        event.field[1].number = 0;
        event.field[2].text = (const p8 address_to)"/usr/bin/foot";
        event.field[2].length = 13;
        check("a pinned task opens its program, and the first pin's rule hears its slot",
              rover_same(rover_fires(&event, ROVER_DESKTOP), "pinned:then first-pin:then"));
        event.field[1].number = 3;
        check("another slot is not the first pin's", rover_same(rover_fires(&event, ROVER_DESKTOP), "pinned:then"));
        {
                struct rover_hit hits[4];
                struct rover_value values[ROVER_ARGUMENTS];
                positive made = rover_fire(&rover_view, rover_runs, rover_pages, &event, ROVER_DESKTOP, hits, 4);

                check("open's argument is the pinned program",
                      made && rover_arguments(&rover_view, hits[0].action, &event, rover_pages, values) == 1 &&
                          rover_bytes_are(values[0].text, values[0].length, "/usr/bin/foot"));
                event.field[0].number = 2; // mic
                made = rover_fire(&rover_view, rover_runs, rover_pages, &event, ROVER_DESKTOP, hits, 4);
                check("open with words hands them on, each one",
                      made && rover_arguments(&rover_view, hits[0].action, &event, rover_pages, values) == 4 &&
                          rover_bytes_are(values[3].text, values[3].length, "-i"));
        }

        //      The runner's clocks: every and at, each its own trigger word, at keyed on the minute.
        check("every and at compile",
              rover_compiled("rule tidy\n\tevery 5m\n\trun tidy\n\n"
                             "rule night\n\tat 22:30\n\tif weekday between mon fri\n\tset brightness 20%\n"));
        check("and print as their own words",
              rover_same(rover_printed_of(), "rule tidy\n\tevery 5m\n\trun tidy\n\n"
                                             "rule night\n\tat 22:30\n\tif weekday between mon fri\n\tset brightness 20%\n"));
        event = rover_event_of("at", null);
        event.count = 1;
        event.field[0].number = 22 * 60 + 30;
        event.realtime = 4 * 86400 + 22 * 3600 + 30 * 60; // a Monday
        check("at 22:30 on a Monday dims", rover_same(rover_fires(&event, ROVER_RUNNER), "night:then"));
        event.field[0].number = 22 * 60 + 31;
        check("22:31 is another minute's", rover_same(rover_fires(&event, ROVER_RUNNER), ""));
        event = rover_event_of("every", null);
        event.count = 1;
        event.field[0].number = 300 * 1000000000ll;
        check("every 5m is the five minutes' timer", rover_same(rover_fires(&event, ROVER_RUNNER), "tidy:then"));
        event.field[0].number = 60 * 1000000000ll;
        check("and not a minute's", rover_same(rover_fires(&event, ROVER_RUNNER), ""));
        check("on every reads as every",
              rover_compiled("rule tidy\n\ton every 5m\n\trun tidy\n") &&
                  rover_same(rover_printed_of(), "rule tidy\n\tevery 5m\n\trun tidy\n"));

        //      Boot's verdict: one runner event a boot, keyed on which.
        check("rules on the verdict compile",
              rover_compiled("rule installer\n\ton boot\n\tif $verdict != disk\n\tdo open installer\n\n"
                             "rule home\n\ton boot disk\n\tif $disk ~ nvme*\n\tdo notify \"home\"\n"));
        event = rover_event_of("boot", null);
        event.count = 2;
        event.field[0].number = 0; // live
        check("a live boot opens the installer", rover_same(rover_fires(&event, ROVER_RUNNER), "installer:then"));
        event.field[0].number = 2; // ask
        check("so does a boot that asks", rover_same(rover_fires(&event, ROVER_RUNNER), "installer:then"));
        event.field[0].number = 1; // disk
        event.field[1].text = (const p8 address_to)"nvme0n1p2";
        event.field[1].length = 9;
        check("a disk's boot is the disk's rule's, by its name",
              rover_same(rover_fires(&event, ROVER_RUNNER), "home:then"));

        //      The machine's switches: set NAME VALUE, the runner's, its value typed by the switch.
        check("the fifth text compiles", rover_compiled(rover_canonical[4]));
        event = rover_event_of("lid close", null);
        rover_set("power.source", 1, null);
        {
                struct rover_hit hits[4];
                struct rover_value values[ROVER_ARGUMENTS];
                positive made = rover_fire(&rover_view, rover_runs, rover_pages, &event, ROVER_KERNEL, hits, 4);
                const struct rover_action address_to set = rover_view.action + (made ? hits[0].action : 0);

                //      The rule reads the kernel's states, so the kernel asks it; its sets go down the ring to the runner.
                check("a lid on battery sets three switches, asked in the kernel", made == 1 && hits[0].count == 3);
                check("airplane on is the switch's on",
                      set->kind == ROVER_SET && set->id == 6 && set->runs == ROVER_RUNNER &&
                          rover_arguments(&rover_view, hits[0].action, &event, rover_pages, values) == 1 &&
                          values[0].number == 1);
                rover_arguments(&rover_view, (positive)hits[0].action + 2, &event, rover_pages, values);
                check("the charge limit is its number", values[0].number == 80);
        }
        rover_set("power.source", 0, null);

        //      Settings and the boot's words: states of their pages, typed by the registry's rows.
        check("the registry's rows are states", rover_state_named((const p8 address_to)"setting.canvas.gpu", 18) &&
                                                    rover_state_named((const p8 address_to)"setting.audio.server", 20) &&
                                                    rover_state_named((const p8 address_to)"boot.desktop", 12));
        check("a setting's state is the settings page's",
              rover_state_named((const p8 address_to)"setting.canvas.gpu", 18)->page == ROVER_PAGE_SETTINGS);
        check("a number row is its bounds",
              rover_types[rover_state_named((const p8 address_to)"setting.canvas.border.width", 27)->type].low == 1 &&
                  rover_types[rover_state_named((const p8 address_to)"setting.canvas.border.width", 27)->type].high == 4);
        check("a when rule over a setting is the runner's",
              rover_compiled("rule gpu-off\n\twhen setting.canvas.gpu == off\n\tdo notify \"no card\"\n") &&
                  rover_view.rule[0].place == ROVER_RUNNER);
        {
                const struct rover_state_row address_to gpu =
                    rover_state_named((const p8 address_to)"setting.canvas.gpu", 18);
                struct rover_hit hits[4];
                struct rover_event edge = rover_event_of("", null);

                rover_set("setting.canvas.gpu", 1, null);
                rover_edges(&rover_view, rover_runs, rover_pages, ~0ull, &edge, ROVER_RUNNER, hits, 4);
                rover_set("setting.canvas.gpu", 0, null);
                check("its edge is heard through its state's bit, past the 62nd state too",
                      rover_edges(&rover_view, rover_runs, rover_pages, 1ull << ((positive)(gpu - rover_states) % 62), &edge,
                                  ROVER_RUNNER, hits, 4) == 1);
        }
        check("on setting hears a row by its name",
              rover_compiled("rule gpu\n\ton setting canvas.gpu\n\tif setting.canvas.gpu == off\n\tdo notify \"off\"\n"));
        event = rover_event_of("setting", "canvas.gpu");
        check("and fires in the runner when it changes", rover_same(rover_fires(&event, ROVER_RUNNER), "gpu:then"));
        event = rover_event_of("setting", "canvas.theme");
        check("another row's change is not its event", rover_same(rover_fires(&event, ROVER_RUNNER), ""));
        check("a boot word is the kernel's: a rule over it stays in the kernel",
              rover_compiled("rule no-desktop\n\ton lid close\n\tif boot.desktop == none\n\tdo suspend\n") &&
                  rover_view.rule[0].place == ROVER_KERNEL);
        rover_set("boot.desktop", 0, "none");
        event = rover_event_of("lid close", null);
        check("and reads the word", rover_same(rover_fires(&event, ROVER_KERNEL), "no-desktop:then"));
        memory_zero(rover_page_store, sizeof(rover_page_store));

        //      Placement: the kernel's rules are the kernel's, the rest are not.
        check("the third text compiles", rover_compiled(rover_canonical[2]));
        check("a dock rule over kernel states runs in the kernel", rover_view.rule[0].place == ROVER_KERNEL);
        rover_set("headphone", 1, null);
        rover_set("tablet", 0, null);
        event = rover_event_of("dock off", null);
        check("dock off with headphones turns the radios off", rover_same(rover_fires(&event, ROVER_KERNEL), "radios:then"));
        check("a kernel-safe call stays in the kernel",
              rover_compiled("rule a\n\ton lid close\n\tif string_length(\"abc\") == 3\n\tdo suspend\n") &&
                  rover_view.rule[0].place == ROVER_KERNEL);
        check("a userspace-only call goes to the runner",
              rover_compiled("rule a\n\ton lid close\n\tif string_compare_folded(\"a\", \"A\") == 0\n\tdo suspend\n") &&
                  rover_view.rule[0].place == ROVER_RUNNER);
        event = rover_event_of("lid close", null);
        check("and is evaluated there", rover_same(rover_fires(&event, ROVER_RUNNER), "a:then"));

        //      A routine's integer argument is held to its range: byte_is_alpha of 321 is asked of 255.
        check("a call's integer argument is held to the routine's range",
              rover_compiled("rule a\n\ton lid close\n\tif byte_is_alpha(321) == 0 and byte_is_alpha(65)\n\tdo suspend\n"));
        check("so it answers", rover_same(rover_fires(&event, ROVER_RUNNER), "a:then"));
}

b32 main(void)
{
        rover_check_first_of_either();
        rover_check_schema();
        rover_check_round_trip();
        rover_check_refusals();
        rover_check_tamper();
        rover_check_evaluator();
        return test_report(null);
}

#endif /* CHECK_rover */


#ifdef BENCH_rover
/*
        Nanoseconds per event, the best of seven runs of a million events
        each, for the shapes of rule a machine has: a kernel event with two
        conditions, a chord among thirty-two, a pattern and a call on a
        window's name, a when edge, and an event no rule has.
*/
static char rover_bench_text[16384];

static positive rover_bench_c; // the C side's best of the last row, when the walker took the row too

static positive rover_bench(const struct rover_event address_to event, p8 place, bool edges, p64 changed)
{
        struct rover_hit hits[8];
        positive best = ~(positive)0;
        volatile positive sink = 0;

        //      Fourteen trials, the walker's and the C's in turn, so neither has the warm half of the machine.
        rover_bench_c = ~(positive)0;
        for (positive trial = 0; trial < 14; trial++)
        {
                positive start, made = 0;

                rover_bench_walker = !(trial & 1);
                start = clock_monotonic_nanoseconds();

                for (positive at = 0; at < 1000000; at++)
                {
                        if (edges)
                        {
                                rover_runs[0].edge = 0;
                                made += rover_edges(&rover_view, rover_runs, rover_pages, changed, event, place, hits, 8);
                        }
                        else
                                made += rover_fire(&rover_view, rover_runs, rover_pages, event, place, hits, 8);
                }
                positive took = clock_monotonic_nanoseconds() - start;
                sink += made;
                if (trial & 1)
                        rover_bench_c = took < rover_bench_c ? took : rover_bench_c;
                else if (took < best)
                        best = took;
        }
        rover_bench_walker = true;
        (void)sink;
        return best; // per million events: picoseconds a thousand per event, i.e. ns * 1000
}

static fn rover_bench_say(const char address_to name, positive per_million)
{
        positive c = rover_bench_c;

        string_format(log, "  %s: %p.%p%p ns per event", name, per_million / 1000000, per_million / 100000 % 10,
                      per_million / 10000 % 10);
        if (ROVER_WALKER)
                string_format(log, " (C alone %p.%p%p)", c / 1000000, c / 100000 % 10, c / 10000 % 10);
        string_format(log, "\n");
        log_flush();
}

//      The loop memory_first_of_either_u32 replaced in rover_key_next, kept here as its C side.
static __attribute__((noinline)) positive rover_bench_either_c(const unsigned int address_to words, positive count,
                                                               unsigned int first, unsigned int second)
{
        positive at = 0;

        while (at < count && words[at] != first && words[at] != second)
                at++;
        return at;
}

/*
        memory_first_of_either_u32 against that loop, paired and alternated:
        rows of keys with the match last, at 16, 32, 128 and 1000 words.
*/
static fn rover_bench_either(void)
{
        static unsigned int words[1000];
        static const positive counts[4] = {16, 32, 128, 1000};

        for (positive at = 0; at < 1000; at++)
                words[at] = 0x00060000u + (unsigned int)at;
        for (positive c = 0; c < 4; c++)
        {
                positive count = counts[c], best_asm = ~(positive)0, best_c = ~(positive)0;
                volatile positive sink = 0;

                words[count - 1] = 0x00020039u;
                for (positive trial = 0; trial < 14; trial++)
                {
                        positive start = clock_monotonic_nanoseconds();

                        for (positive at = 0; at < 100000; at++)
                                sink += (trial & 1) ? rover_bench_either_c(words, count, 0x00020039u, ROVER_KEY_ANY)
                                                    : memory_first_of_either_u32(words, count, 0x00020039u, ROVER_KEY_ANY);
                        positive took = (clock_monotonic_nanoseconds() - start) * 10;
                        if (trial & 1)
                                best_c = took < best_c ? took : best_c;
                        else
                                best_asm = took < best_asm ? took : best_asm;
                }
                words[count - 1] = 0x00060000u + (unsigned int)(count - 1);
                (void)sink;
                string_format(log, "  memory_first_of_either_u32, %p keys: asm %p.%p%p ns, C %p.%p%p ns\n", count,
                              best_asm / 1000000, best_asm / 100000 % 10, best_asm / 10000 % 10, best_c / 1000000,
                              best_c / 100000 % 10, best_c / 10000 % 10);
                log_flush();
        }
}

b32 main(void)
{
        struct rover_event event;
        struct rover_text text = {(p8 address_to)rover_bench_text, 0, sizeof(rover_bench_text) - 1, false};
        static const char address_to const keys = "abcdefghijklmnopqrstuvwxyz012345";

        memory_zero(rover_page_store, sizeof(rover_page_store));
        rover_set("power.source", 1, null);

        rover_set("lid", 1, null);
        event = rover_event_of("lid close", null);
        rover_compiled("rule lid-sleep\n\ton lid close\n\tdo suspend\n");
        rover_bench_say("lid close, no condition (the dispatch), kernel", rover_bench(&event, ROVER_KERNEL, false, 0));
        rover_compiled("rule lid-sleep\n\ton lid close\n\tif power.source == battery\n\tdo suspend\n");
        rover_bench_say("lid close, one condition, kernel", rover_bench(&event, ROVER_KERNEL, false, 0));
        rover_compiled("rule lid-sleep\n\ton lid close\n\tif power.source == battery\n\tif lid == closed\n"
                       "\tdo suspend\n\telse\n\tdo backlight 10%\n");
        rover_bench_say("lid close, two conditions, kernel", rover_bench(&event, ROVER_KERNEL, false, 0));

        //      Chords: n rules of ctrl+alt+KEY, the event the n-2th's, so all but two are passed over.
        static const positive counts[4] = {4, 8, 32, 128};
        static const char address_to const chord_rows[4] = {
            "a chord among 4 rules, desktop", "a chord among 8 rules, desktop", "a chord among 32 rules, desktop",
            "a chord among 128 rules, desktop"};
        for (positive c = 0; c < 4; c++)
        {
                text.used = 0;
                for (positive at = 0; at < counts[c]; at++)
                {
                        p8 key[4] = {(p8)keys[at % 32], 0, 0, 0};
                        static const char address_to const mods[4] = {"ctrl+alt+", "ctrl+shift+", "alt+shift+",
                                                                      "ctrl+super+"};

                        rover_puts(&text, "rule chord-");
                        rover_put_number(&text, (b64)at);
                        rover_puts(&text, "\n\ton chord ");
                        rover_puts(&text, mods[at / 32]);
                        rover_puts(&text, (const char address_to)key);
                        rover_puts(&text, "\n\tdo open terminal\n\n");
                }
                rover_bench_text[text.used] = 0;
                rover_compiled(rover_bench_text);
                event = rover_event_of("chord", null);
                event.count = 1;
                event.field[0].number = rover_view.pool[0]; // replaced below by the n-2th rule's own key
                {
                        const struct rover_rule address_to rule = rover_view.rule + counts[c] - 2;
                        b64 key;

                        memory_copy(&key, rover_view.pool + rover_view.comparison[rule->comparison].b.index, 8);
                        event.field[0].number = key;
                }
                rover_bench_say(chord_rows[c], rover_bench(&event, ROVER_DESKTOP, false, 0));
        }

        rover_compiled("rule game\n\ton fullscreen on steam_app_*\n\tdo latency 20us\n");
        event = rover_event_of("fullscreen on", "steam_app_1172470");
        rover_bench_say("a pattern on the app, desktop", rover_bench(&event, ROVER_DESKTOP, false, 0));
        rover_compiled("rule game\n\ton fullscreen on\n\tif string_length($app) > 4\n\tdo latency 20us\n");
        rover_bench_say("a lib.c call on the app, desktop", rover_bench(&event, ROVER_DESKTOP, false, 0));
        rover_compiled("rule game\n\ton fullscreen on steam_app_*\n\tif string_length($app) > 4\n\tdo latency 20us\n");
        event = rover_event_of("fullscreen on", "steam_app_1172470");
        rover_bench_say("a pattern and a call on the app, desktop", rover_bench(&event, ROVER_DESKTOP, false, 0));

        rover_compiled("rule low\n\twhen battery.percent < 10% and power.source == battery\n\tdo suspend\n");
        rover_set("battery.percent", 500, null);
        event = rover_event_of("", null);
        rover_bench_say("a when edge, kernel", rover_bench(&event, ROVER_KERNEL, true, 1ull << 2));

        event = rover_event_of("mute", null);
        rover_bench_say("an event no rule has", rover_bench(&event, ROVER_KERNEL, false, 0));

        rover_bench_either();

        //      The load, which happens once a file and never at an event: compile, check, and print back.
        {
                static const char address_to const file =
                    "rule lid-sleep\n\tabout \"Sleep when the lid closes on battery\"\n\ton lid close\n"
                    "\tif power.source == battery\n\tif display.external == 0\n\tdo suspend\n\telse\n\tdo display off\n\n"
                    "rule game-latency\n\ton fullscreen on steam_app_*\n\tdo latency 20us\n\n"
                    "rule late-dim\n\ton lid open\n\tif clock between 22:00 06:00\n\tdo backlight 30%\n\n"
                    "rule power\n\ton key power\n\tplace kernel\n\tdo poweroff\n";
                positive best = ~(positive)0, check_best = ~(positive)0;
                struct rover_table view;
                struct rover_refusal refusal;

                for (positive trial = 0; trial < 7; trial++)
                {
                        positive start = clock_monotonic_nanoseconds();

                        for (positive at = 0; at < 1000; at++)
                                rover_compile(&rover_one, (const p8 address_to)file, string_length((string_address)file));
                        positive took = clock_monotonic_nanoseconds() - start;
                        if (took < best)
                                best = took;
                        start = clock_monotonic_nanoseconds();
                        for (positive at = 0; at < 1000; at++)
                                rover_check(rover_built(&rover_one), rover_one.size, &view, &refusal);
                        took = clock_monotonic_nanoseconds() - start;
                        if (took < check_best)
                                check_best = took;
                }
                string_format(log, "  compiling the README's four rules (%p bytes of table): %p.%p us, its check alone %p.%p us\n",
                              rover_one.size, best / 1000000, best / 100000 % 10, check_best / 1000000,
                              check_best / 100000 % 10);
                log_flush();
        }
        return 0;
}
#endif /* BENCH_rover */
