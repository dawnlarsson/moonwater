/*
        test/checks/rover.c: rover, Moonwater's rules (src/rover).

        A run of test/checks.c, which includes this file where the run stood: it is compiled only as part of that file,
        with its statics and macros, and never on its own. It is freestanding, built as test/checks.c is:
        gcc -O2 -static -nostdlib -nostartfiles -march=x86-64 -w -DCHECK_rover -fwhole-program -o OUT test/checks.c
        (sh test/run rover builds it with its freestanding_check).

        CHECK_rover holds the language to src/rover/README.md: the schema against the code it was generated from, the
        round trip (a canonical text prints back byte for byte and its table compiles back byte for byte), the one
        spelling of every literal, a refusal with its line for each thing the language is not, the load-time check
        against every byte of a table flipped, and the evaluator's answers: filters, else, and and or, between over
        midnight, calls, limit, once, off, when edges and where each rule is placed.
*/

#ifdef CHECK_rover
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
#include "../../src/rover/compile.c"

static struct rover_build rover_one;
static struct rover_build rover_two;
static struct rover_table rover_view;
static struct rover_run rover_runs[ROVER_RULES_MOST];
static p64 rover_page_store[ROVER_PAGES][ROVER_PAGE_BYTES / 8];
static const p8 address_to rover_pages[ROVER_PAGES] = {(const p8 address_to)rover_page_store[0],
                                                       (const p8 address_to)rover_page_store[1]};
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
    {"\ton lid close\n", 1, "before any rule"},
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
                p8 address_to writable[ROVER_PAGES] = {(p8 address_to)rover_page_store[0], (p8 address_to)rover_page_store[1]};
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
        event = rover_event_of("chord", null);
        event.count = 1;
        event.field[0].number = (b64)ROVER_CHORD_SUPER << 16 | 57;
        check("super+space opens the launcher", rover_same(rover_fires(&event, ROVER_DESKTOP), "launcher:then"));
        event.when += 100000000;
        check("again 100 ms later is inside its limit", rover_same(rover_fires(&event, ROVER_DESKTOP), ""));
        check("and is counted", rover_runs[0].dropped == 1);
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
        rover_check_schema();
        rover_check_round_trip();
        rover_check_refusals();
        rover_check_tamper();
        rover_check_evaluator();
        return test_report(null);
}

#endif /* CHECK_rover */
