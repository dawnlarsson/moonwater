/*
        Rover's table, the check a table passes before it is used, and the
        evaluator that answers an event from it.

        One file for both sides of the ring: the shell includes it for the
        compiler and for the evaluator a userspace owner runs, and the kernel
        module includes it, built with KERNEL_MODE, for the rules placed there.
        It asks the library for nothing but memory_compare, and allocates
        nothing: a table is bytes somebody else holds, a run record is an
        array somebody else holds, and every answer goes into the caller's
        room. src/rover/README.md is the language this is the binary of.
*/

#ifndef ROVER_INCLUDED
#define ROVER_INCLUDED

#define ROVER_MAGIC 0x52564f52u // "ROVR"
#define ROVER_VERSION 3u
#define ROVER_TABLE_MOST 65536u
#define ROVER_EVENTS 128u
#define ROVER_CUE 96u
#define ROVER_FIELDS 4u
#define ROVER_ARGUMENTS 4u
#define ROVER_EMIT_DEPTH 4u
#define ROVER_NAME_MOST 48u
#define ROVER_WORD_MOST 62u
#define ROVER_TEXT_MOST 255u
#define ROVER_LIMIT_MOST_MS 86400000u
#define ROVER_DURATION_MOST (86400ll * 1000000000ll)

//      A page: its first sixteen bytes are its version, its sequence (odd
//      while it is being written), and the zone's offset east of UTC in
//      seconds; the states follow, a number in four bytes and a word in
//      sixty four: its length, the bytes and a NUL.
#define ROVER_PAGE_BYTES 4096u
#define ROVER_PAGE_HEAD 16u
#define ROVER_PAGE_ZONE 8u
#define ROVER_WORD_SLOT 64u
#define ROVER_PAGE_KERNEL 0u
#define ROVER_PAGE_DESKTOP 1u
#define ROVER_PAGE_SETTINGS 2u // the runner's: the Settings rows and what it keeps of them
#define ROVER_PAGES 3u

//      Where a rule runs and an action is done; the schema's sources in the
//      same order.
#define ROVER_KERNEL 0u
#define ROVER_RUNNER 1u
#define ROVER_DESKTOP 2u
#define ROVER_PLACES 3u

#define ROVER_ANY 0u
#define ROVER_LOWER_ONLY 1u
#define ROVER_DELIBERATE 2u

#define ROVER_T_INT 0u
#define ROVER_T_PERCENT 1u
#define ROVER_T_DURATION 2u
#define ROVER_T_CLOCK 3u
#define ROVER_T_WEEKDAY 4u
#define ROVER_T_ENUM 5u
#define ROVER_T_ONOFF 6u
#define ROVER_T_CHORD 7u
#define ROVER_T_WORD 8u
#define ROVER_T_TEXT 9u
#define ROVER_T_WORDS 10u // a primitive's last argument: none or more words
#define ROVER_T_KINDS 11u

#define ROVER_RULE_WHEN 1u
#define ROVER_RULE_ONCE 2u
#define ROVER_RULE_OFF 4u
#define ROVER_RULE_PLACED 8u
#define ROVER_RULE_NUMBERS 16u // every comparison of the rule has the number form: the walker's to take whole
#define ROVER_RULE_TAKE 32u    // the event is the rule's: the kernel drops it before any reader (take)
#define ROVER_RULE_FLAGS 63u

#define ROVER_TRUE 0u
#define ROVER_EQ 1u
#define ROVER_NE 2u
#define ROVER_LT 3u
#define ROVER_LE 4u
#define ROVER_GT 5u
#define ROVER_GE 6u
#define ROVER_MATCH 7u
#define ROVER_NOMATCH 8u
#define ROVER_BETWEEN 9u
#define ROVER_OPS 10u

#define ROVER_JOIN_OR 1u
#define ROVER_JOIN_LINE 2u

//      A comparison's form: how the evaluator asks it. NUMBER is two number sides (a constant, a state or a
//      field) under == != < <= > or >=, which is two loads and one compare; anything else is GENERAL.
#define ROVER_FORM_GENERAL 0u
#define ROVER_FORM_NUMBER 1u

//      A rule's key: the number its on line's first filter asks field 0 to be, or ANY.
#define ROVER_KEY_ANY 0xffffffffu

#define ROVER_NONE 0u
#define ROVER_CONST_NUMBER 1u
#define ROVER_CONST_WORD 2u
#define ROVER_STATE_NUMBER 3u
#define ROVER_STATE_WORD 4u
#define ROVER_FIELD 5u
#define ROVER_CALL 6u
#define ROVER_CLOCK 7u
#define ROVER_WEEKDAY 8u
#define ROVER_SOURCES 9u

#define ROVER_DO 1u
#define ROVER_RUN 2u
#define ROVER_TOOL 3u
#define ROVER_EMIT 4u
#define ROVER_SET 5u // one of the machine's switches set: its id the switch's

#define ROVER_CHORD_SHIFT 1u
#define ROVER_CHORD_CTRL 2u
#define ROVER_CHORD_ALT 4u
#define ROVER_CHORD_SUPER 8u

struct rover_head
{
        p32 magic;
        p16 version;
        p16 services; // the service blocks
        p32 size;
        p32 schema; // ROVER_SCHEMA_HASH of the schema it was compiled against
        p16 rules;
        p16 comparisons;
        p16 calls;
        p16 actions;
        p16 arguments;
        p16 notes;
        p32 pool;
};

_Static_assert(sizeof(struct rover_head) == 32, "rover head");

struct rover_operand
{
        p8 source; // ROVER_NONE .. ROVER_WEEKDAY
        p8 page;   // a state's page, a constant's type
        p16 index; // a state's offset, a field's number, a call's, a constant's offset in the pool
};

struct rover_rule
{
        p16 event; // 0 for a when rule
        p8 place;
        p8 flags;
        p16 comparison;  // the first of its run
        p16 comparisons; // the run's length, the trigger's included
        p8 trigger;      // the first comparisons that are the on line's filters, or the when line
        p8 then;         // actions before the else
        p8 otherwise;    // actions after it
        p8 pad;
        p16 action; // the first of its run
        p16 pad2;
        p32 limit_ms;
        p32 name;  // pool offset
        p32 about; // pool offset; 0 is the empty string, which is no about
        p32 pad3;
        p64 reads; // the states its comparisons read, one bit a state
};

_Static_assert(sizeof(struct rover_rule) == 40, "rover rule");

/*
        A service: a long-lived program the runner keeps up, with nothing of
        its text left to read. Its run is a run of the arguments section, the
        program's path first; ready, log and about are pool words; needs are
        the services earlier in the table it waits to be up for; the restart
        policy is the waits after its first, second and third short life and
        the lives it is given up after; and whether it is wanted is its if
        lines, comparisons over states like a when rule's.
*/
#define ROVER_SERVICE_OFF 1u
#define ROVER_SERVICE_NONE 0xffu
#define ROVER_NEEDS 4u
#define ROVER_SERVICES_MOST 64u

struct rover_service
{
        p32 name;          // pool word
        p32 ready;         // pool word: the path whose being there is the service up; 0, its start is
        p32 log;           // pool word: where its output goes; 0, the kernel's log
        p16 argument;      // its run, in the arguments: the program's path, then its words
        p8 count;
        p8 flags;          // ROVER_SERVICE_OFF
        p16 comparison;    // its if lines, in the comparisons
        p16 comparisons;
        p8 need[ROVER_NEEDS]; // services earlier in the table it needs up first, ROVER_SERVICE_NONE past the last
        p8 lives;          // short lives in a row it is given up after
        p8 pad[3];
        p32 wait_ms[3];    // the waits before a start after its first, second and third short life; past that, the last
        p32 short_ms;      // a life shorter than this is short; one this long resets the count
        p32 about;         // pool word, 0 none
        p64 reads;         // the states its if lines read
};

_Static_assert(sizeof(struct rover_service) == 56, "rover service");

struct rover_span
{
        p16 first;
        p16 count; // ROVER_SPAN_COUNT of it the rules; ROVER_SPAN_NUMBERS when every one is a numbers rule
};

//      A table holds at most 1,638 rules, so a span's count has its top bit to spare: the walker's hint.
#define ROVER_SPAN_COUNT 0x7fffu
#define ROVER_SPAN_NUMBERS 0x8000u

struct rover_comparison
{
        p8 op;
        p8 type;
        p8 join;
        p8 form; // ROVER_FORM_*, which the check holds to what the comparison is
        struct rover_operand a, b, c;
};

_Static_assert(sizeof(struct rover_comparison) == 16, "rover comparison");

struct rover_call
{
        p16 function;
        p8 count;
        p8 pad;
        struct rover_operand argument[ROVER_ARGUMENTS];
};

_Static_assert(sizeof(struct rover_call) == 20, "rover call");

struct rover_action
{
        p8 kind;
        p8 runs;
        p16 id; // the primitive's id, the tool's, or the event's
        p16 argument;
        p8 count;
        p8 pad;
};

_Static_assert(sizeof(struct rover_action) == 8, "rover action");

#define ROVER_NOTE_SERVICE 0x8000u // a note's rule with this bit is the service of the rest

struct rover_note
{
        p16 rule;
        p16 slot; // the statement's place in the rule's canonical order
        p32 text;
};

_Static_assert(sizeof(struct rover_note) == 8, "rover note");

//      A value as a comparison or a routine sees it: a number, or bytes and
//      their length; the bytes always end in a NUL the length does not count.
struct rover_value
{
        b64 number;
        const p8 address_to text;
        p32 length;
        p32 pad;
};

//      A routine as rover calls it: its arguments in registers, a word as its address, a buffer as its address
//      and its length, a number as itself; the six are what every machine's convention passes in registers.
typedef b64(address_to rover_function)(b64, b64, b64, b64, b64, b64);
#define ROVER_REGISTERS 6u

struct rover_type_row
{
        p8 kind;
        p8 count; // an enum's words
        b64 low;
        b64 high;
        const char address_to choices; // comma separated
};

struct rover_state_row
{
        const char address_to name;
        p8 page;
        p8 type;
        p16 offset;
};

struct rover_event_row
{
        const char address_to name;
        p16 id;
        p8 source;
        p8 fields;
        p8 field_type[ROVER_FIELDS];
        const char address_to field_name[ROVER_FIELDS];
};

struct rover_primitive_row
{
        const char address_to name;
        p16 id;
        p8 runs;
        p8 klass;
        p8 count;
        p8 type[ROVER_ARGUMENTS];
        const char address_to argument_name[ROVER_ARGUMENTS];
};

struct rover_function_row
{
        const char address_to name;
        const char address_to signature; // w a word, b a byte buffer, i an integer
        p8 count;
        p8 kernel;
        rover_function routine; // 0 where this build has no body of it
        b64 low[ROVER_ARGUMENTS];
        b64 high[ROVER_ARGUMENTS];
};

struct rover_switch_row
{
        const char address_to name;
        p16 id;
        p8 type;
        b8 deliberate; // the value (an enum's index) a rule may never set, or -1
};

#ifdef KERNEL_MODE
#define ROVER_USER_CALL(call) 0
#else
#define ROVER_USER_CALL(call) call
#endif

#include "schema.inc"

/*
        A table as the evaluator reads it: the head and where each section
        starts, found once by rover_check and never again.
*/
struct rover_table
{
        const struct rover_head address_to head;
        const struct rover_rule address_to rule;
        const p16 address_to order;
        const p32 address_to key; // one a slot of order
        const struct rover_span address_to index;
        const struct rover_comparison address_to comparison;
        const struct rover_call address_to call;
        const struct rover_action address_to action;
        const struct rover_operand address_to argument;
        const struct rover_note address_to note;
        const p8 address_to pool;
        const struct rover_service address_to service; // last, so the walker's offsets before it stay
};

//      What a rule has done since its table was loaded.
struct rover_run
{
        p64 last; // when it last fired
        p32 fires;
        p32 dropped; // inside its limit
        p8 edge;     // a when rule's condition, as last seen
        p8 done;     // a once rule that has fired
        p8 pad[6];
};

struct rover_event
{
        p16 id;
        p8 count; // fields
        p8 depth; // cues deep
        p32 pad;
        p64 when;     // monotonic nanoseconds
        b64 realtime; // seconds since 1970, for the clock and the weekday
        struct rover_value field[ROVER_FIELDS];
};

//      A rule that fired, and the run of its actions it chose.
struct rover_hit
{
        p16 rule;
        p16 action;
        p8 count;
        p8 otherwise;
        p16 pad;
};

//      The walker's assembly reads these by offset; a struct that moves breaks the build, not the walk.
_Static_assert(offset_of(struct rover_table, rule) == 8 && offset_of(struct rover_table, order) == 16 &&
                   offset_of(struct rover_table, key) == 24 && offset_of(struct rover_table, index) == 32 &&
                   offset_of(struct rover_table, comparison) == 40 && offset_of(struct rover_table, pool) == 80,
               "rover walker: the view");
_Static_assert(offset_of(struct rover_rule, place) == 2 && offset_of(struct rover_rule, flags) == 3 &&
                   offset_of(struct rover_rule, comparison) == 4 && offset_of(struct rover_rule, comparisons) == 6 &&
                   offset_of(struct rover_rule, trigger) == 8 && offset_of(struct rover_rule, then) == 9 &&
                   offset_of(struct rover_rule, otherwise) == 10 && offset_of(struct rover_rule, action) == 12 &&
                   offset_of(struct rover_rule, limit_ms) == 16 && sizeof(struct rover_rule) == 40,
               "rover walker: the rule");
_Static_assert(offset_of(struct rover_event, count) == 2 && offset_of(struct rover_event, when) == 8 &&
                   offset_of(struct rover_event, field) == 24 && sizeof(struct rover_value) == 24,
               "rover walker: the event");
_Static_assert(offset_of(struct rover_run, fires) == 8 && offset_of(struct rover_run, dropped) == 12 &&
                   offset_of(struct rover_run, done) == 17 && sizeof(struct rover_run) == 24,
               "rover walker: the run");
_Static_assert(sizeof(struct rover_hit) == 8 && offset_of(struct rover_hit, action) == 2 &&
                   offset_of(struct rover_hit, count) == 4 && offset_of(struct rover_hit, otherwise) == 5,
               "rover walker: the hit");
_Static_assert(ROVER_RULE_WHEN == 1 && ROVER_RULE_ONCE == 2 && ROVER_RULE_OFF == 4 && ROVER_RULE_NUMBERS == 16 &&
                   ROVER_CONST_NUMBER == 1 && ROVER_STATE_NUMBER == 3 && ROVER_FIELD == 5 && ROVER_JOIN_OR == 1 &&
                   ROVER_JOIN_LINE == 2 && ROVER_EQ == 1 && ROVER_GE == 6,
               "rover walker: the codes");

struct rover_refusal
{
        const char address_to why;
        p32 rule; // the rule it is about, or ROVER_NO_RULE
        p32 at;   // the comparison, call, action or argument, where it is one
};

#define ROVER_NO_RULE 0xffffffffu

static const p8 rover_nothing[1] = {0};

static inline positive rover_align8(positive value)
{
        return (value + 7) & ~(positive)7;
}

/*
        Where each section starts, from the head's counts, and the size the
        table must be: the head, then rules, order, keys, index, comparisons,
        calls, actions, arguments, notes and the pool, each at a multiple of
        eight.
*/
static positive rover_layout(const struct rover_head address_to head, positive address_to at)
{
        positive reach = sizeof(struct rover_head);

        at[0] = reach, reach = rover_align8(reach + (positive)head->rules * sizeof(struct rover_rule));
        at[10] = reach, reach = rover_align8(reach + (positive)head->services * sizeof(struct rover_service));
        at[1] = reach, reach = rover_align8(reach + (positive)head->rules * sizeof(p16));
        at[2] = reach, reach = rover_align8(reach + (positive)head->rules * sizeof(p32));
        at[3] = reach, reach = rover_align8(reach + ROVER_EVENTS * sizeof(struct rover_span));
        at[4] = reach, reach = rover_align8(reach + (positive)head->comparisons * sizeof(struct rover_comparison));
        at[5] = reach, reach = rover_align8(reach + (positive)head->calls * sizeof(struct rover_call));
        at[6] = reach, reach = rover_align8(reach + (positive)head->actions * sizeof(struct rover_action));
        at[7] = reach, reach = rover_align8(reach + (positive)head->arguments * sizeof(struct rover_operand));
        at[8] = reach, reach = rover_align8(reach + (positive)head->notes * sizeof(struct rover_note));
        at[9] = reach, reach = rover_align8(reach + head->pool);
        return reach;
}

static const struct rover_event_row address_to rover_event_find(positive id)
{
        for (positive at = 0; at < ROVER_EVENT_ROWS; at++)
                if (rover_event_rows[at].id == id)
                        return &rover_event_rows[at];
        return null;
}

static const struct rover_switch_row address_to rover_switch_find(positive id)
{
        for (positive at = 0; at < ROVER_SWITCHES; at++)
                if (rover_switches[at].id == id)
                        return &rover_switches[at];
        return null;
}

static const struct rover_primitive_row address_to rover_primitive_find(positive id)
{
        for (positive at = 0; at < ROVER_PRIMITIVES; at++)
                if (rover_primitives[at].id == id)
                        return &rover_primitives[at];
        return null;
}

static inline bool rover_kind_is_word(p8 kind)
{
        return kind == ROVER_T_WORD || kind == ROVER_T_TEXT || kind == ROVER_T_WORDS;
}

static inline bool rover_number_side(p8 source)
{
        return source == ROVER_CONST_NUMBER || source == ROVER_STATE_NUMBER || source == ROVER_FIELD;
}

//      The form a comparison's shape gives it; the compiler writes it and the check holds the table to it.
static p8 rover_form_of(const struct rover_comparison address_to comparison)
{
        //      The walker picks between the first two pages; a state of the settings page is the general form's.
        if (comparison->op >= ROVER_EQ && comparison->op <= ROVER_GE && !rover_kind_is_word(comparison->type) &&
            rover_number_side(comparison->a.source) && rover_number_side(comparison->b.source) &&
            !(comparison->a.source == ROVER_STATE_NUMBER && comparison->a.page > ROVER_PAGE_DESKTOP) &&
            !(comparison->b.source == ROVER_STATE_NUMBER && comparison->b.page > ROVER_PAGE_DESKTOP) &&
            !comparison->c.source && !comparison->c.page && !comparison->c.index)
                return ROVER_FORM_NUMBER;
        return ROVER_FORM_GENERAL;
}

//      Whether every comparison of a rule has the number form, which ROVER_RULE_NUMBERS says.
static bool rover_rule_numbers(const struct rover_rule address_to rule,
                               const struct rover_comparison address_to comparisons)
{
        for (positive at = 0; at < rule->comparisons; at++)
                if (comparisons[rule->comparison + at].form != ROVER_FORM_NUMBER)
                        return false;
        return true;
}

//      A rule's key: the constant its on line's first filter asks of field 0, when that is a number below ANY.
static p32 rover_key_of(const struct rover_rule address_to rule, const struct rover_comparison address_to comparisons,
                        const p8 address_to pool)
{
        const struct rover_comparison address_to first = comparisons + rule->comparison;
        b64 value;

        if ((rule->flags & ROVER_RULE_WHEN) || !rule->trigger || first->op != ROVER_EQ ||
            first->a.source != ROVER_FIELD || first->a.index || first->b.source != ROVER_CONST_NUMBER)
                return ROVER_KEY_ANY;
        value = memory_load_unaligned(b64, pool + first->b.index);
        return value >= 0 && value < (b64)ROVER_KEY_ANY ? (p32)value : ROVER_KEY_ANY;
}

// ---- the evaluator ---------------------------------------------------------

static inline fn rover_word(struct rover_value address_to into, const p8 address_to text, p32 length)
{
        into->number = 0;
        into->text = text;
        into->length = length;
}

static inline fn rover_number(struct rover_value address_to into, b64 number)
{
        into->number = number;
        into->text = rover_nothing;
        into->length = 0;
}

static inline b64 rover_floor_mod(b64 value, b64 by)
{
        b64 left = value % by;

        return left < 0 ? left + by : left;
}

static b64 rover_zone(const p8 address_to const address_to pages)
{
        b32 east;

        if (!pages[ROVER_PAGE_KERNEL])
                return 0;
        memory_copy(&east, pages[ROVER_PAGE_KERNEL] + ROVER_PAGE_ZONE, sizeof(east));
        return east;
}

static fn rover_value_of(const struct rover_table address_to table, struct rover_operand operand,
                         const struct rover_event address_to event, const p8 address_to const address_to pages,
                         struct rover_value address_to into);

//      A routine's answer, its integer arguments held to the range the schema gives each.
static b64 rover_called(const struct rover_table address_to table, p16 which, const struct rover_event address_to event,
                        const p8 address_to const address_to pages)
{
        const struct rover_call address_to asked = table->call + which;
        const struct rover_function_row address_to row = &rover_functions[asked->function];
        b64 in[ROVER_REGISTERS] = {0, 0, 0, 0, 0, 0};
        positive used = 0;

        if (!row->routine)
                return 0;
        //      Straight into the registers the routine's convention reads: no value records, no copies.
        for (positive at = 0; at < asked->count; at++)
        {
                struct rover_value value;

                rover_value_of(table, asked->argument[at], event, pages, &value);
                if (row->signature[at] == 'i')
                {
                        if (value.number < row->low[at])
                                value.number = row->low[at];
                        if (value.number > row->high[at])
                                value.number = row->high[at];
                        in[used++] = value.number;
                        continue;
                }
                in[used++] = (b64)(positive)value.text;
                if (row->signature[at] == 'b')
                        in[used++] = value.length;
        }
        return row->routine(in[0], in[1], in[2], in[3], in[4], in[5]);
}

static fn rover_value_of(const struct rover_table address_to table, struct rover_operand operand,
                         const struct rover_event address_to event, const p8 address_to const address_to pages,
                         struct rover_value address_to into)
{
        const p8 address_to page;
        b64 number;
        b32 state;
        p16 length;

        switch (operand.source)
        {
        case ROVER_CONST_NUMBER:
                memory_copy(&number, table->pool + operand.index, sizeof(number));
                rover_number(into, number);
                return;
        case ROVER_CONST_WORD:
                memory_copy(&length, table->pool + operand.index, sizeof(length));
                rover_word(into, table->pool + operand.index + 2, length);
                return;
        case ROVER_STATE_NUMBER:
                page = pages[operand.page];
                if (!page)
                        break;
                memory_copy(&state, page + operand.index, sizeof(state));
                rover_number(into, state);
                return;
        case ROVER_STATE_WORD:
                page = pages[operand.page];
                if (!page)
                        break;
                //      The page is its writer's: a length past the slot is the slot.
                rover_word(into, page + operand.index + 1,
                           page[operand.index] > ROVER_WORD_MOST ? ROVER_WORD_MOST : page[operand.index]);
                return;
        case ROVER_FIELD:
                if (operand.index >= event->count)
                        break;
                *into = event->field[operand.index];
                return;
        case ROVER_CALL:
                rover_number(into, rover_called(table, operand.index, event, pages));
                return;
        case ROVER_CLOCK:
                rover_number(into, rover_floor_mod(event->realtime + rover_zone(pages), 86400) / 60);
                return;
        case ROVER_WEEKDAY:
                //      1970's first day was a Thursday, and Monday is 0.
                number = event->realtime + rover_zone(pages);
                number = (number - rover_floor_mod(number, 86400)) / 86400;
                rover_number(into, rover_floor_mod(number + 3, 7));
                return;
        }
        rover_number(into, 0);
}

/*
        A pattern over bytes: * any run, ? one byte, every other byte itself.
        One star is remembered and returned to, so the walk is at most the
        product of the two lengths, which a word bounds at 62 by 255.
*/
static bool rover_glob_loop(const p8 address_to text, p32 length, const p8 address_to pattern, p32 size)
{
        p32 at = 0, in = 0, star = 0, mark = 0;
        bool starred = false;

        while (at < length)
        {
                if (in < size && pattern[in] == '*')
                {
                        starred = true;
                        star = in++;
                        mark = at;
                }
                else if (in < size && (pattern[in] == '?' || pattern[in] == text[at]))
                        at++, in++;
                else if (starred)
                {
                        in = star + 1;
                        at = ++mark;
                }
                else
                        return false;
        }
        while (in < size && pattern[in] == '*')
                in++;
        return in == size;
}

/*
        The pattern's literal head and tail are compared whole, by the
        library's memory_compare, and only what lies between its first and
        last star is walked: steam_app_* is one compare of ten bytes.
*/
static bool rover_glob(const p8 address_to text, p32 length, const p8 address_to pattern, p32 size)
{
        p32 head = 0, tail = 0, last;

        while (head < size && pattern[head] != '*' && pattern[head] != '?')
                head++;
        if (head == size)
                return length == size && !memory_compare(text, pattern, size);
        if (head > length || (head && memory_compare(text, pattern, head)))
                return false;
        last = size;
        while (last > head && pattern[last - 1] != '*' && pattern[last - 1] != '?')
                last--, tail++;
        if (last > head && pattern[last - 1] == '*')
        {
                if (head + tail > length || (tail && memory_compare(text + length - tail, pattern + last, tail)))
                        return false;
                //      Between the head and the tail, a pattern that is only stars holds anything.
                p32 star = head;
                while (star < last && pattern[star] == '*')
                        star++;
                if (star == last)
                        return true;
                return rover_glob_loop(text + head, length - head - tail, pattern + head, last - head);
        }
        return rover_glob_loop(text + head, length - head, pattern + head, size - head);
}

//      The rare numbers, out of the comparison's way: a call, the clock, the weekday.
static __attribute__((__noinline__)) b64 rover_number_far(const struct rover_table address_to table,
                                                      struct rover_operand operand,
                                                      const struct rover_event address_to event,
                                                      const p8 address_to const address_to pages)
{
        struct rover_value value;

        rover_value_of(table, operand, event, pages, &value);
        return value.number;
}

/*
        A comparison's sides read as what they are. The check has proven
        every constant and state offset aligned and inside its pool or page,
        so a number is one load; a word is its bytes and length, and only a
        call, the clock or the weekday leaves the comparison's own code.
*/
static inline b64 rover_number_at(const struct rover_table address_to table, struct rover_operand operand,
                                  const struct rover_event address_to event, const p8 address_to const address_to pages)
{
        b64 number;
        b32 state;

        switch (operand.source)
        {
        case ROVER_CONST_NUMBER:
                number = memory_load_unaligned(b64, table->pool + operand.index);
                return number;
        case ROVER_STATE_NUMBER:
                if (!pages[operand.page])
                        return 0;
                state = memory_load_unaligned(b32, pages[operand.page] + operand.index);
                return state;
        case ROVER_FIELD:
                return operand.index < event->count ? event->field[operand.index].number : 0;
        }
        return rover_number_far(table, operand, event, pages);
}

static inline const p8 address_to rover_word_at(const struct rover_table address_to table, struct rover_operand operand,
                                                const struct rover_event address_to event,
                                                const p8 address_to const address_to pages, p32 address_to length)
{
        const p8 address_to page;
        p16 size;

        switch (operand.source)
        {
        case ROVER_CONST_WORD:
                size = memory_load_unaligned(p16, table->pool + operand.index);
                *length = size;
                return table->pool + operand.index + 2;
        case ROVER_STATE_WORD:
                page = pages[operand.page];
                if (!page)
                        break;
                //      The page is its writer's: a length past the slot is the slot.
                *length = page[operand.index] > ROVER_WORD_MOST ? ROVER_WORD_MOST : page[operand.index];
                return page + operand.index + 1;
        case ROVER_FIELD:
                if (operand.index >= event->count)
                        break;
                *length = event->field[operand.index].length;
                return event->field[operand.index].text;
        }
        *length = 0;
        return rover_nothing;
}

//      The number form's side: a constant, a state on a page that is never null here, or a field.
static inline INLINE b64 rover_side(const struct rover_table address_to table, struct rover_operand operand,
                                    const struct rover_event address_to event, const p8 address_to const address_to pages)
{
        if (operand.source == ROVER_STATE_NUMBER)
                return memory_load_unaligned(b32, pages[operand.page] + operand.index);
        if (operand.source == ROVER_CONST_NUMBER)
                return memory_load_unaligned(b64, table->pool + operand.index);
        return operand.index < event->count ? event->field[operand.index].number : 0;
}

//      Which of < == > a comparison holds for: bit 0 below, bit 1 equal, bit 2 above.
static const p8 rover_holds_for[ROVER_OPS] = {
    [ROVER_EQ] = 2, [ROVER_NE] = 5, [ROVER_LT] = 1, [ROVER_LE] = 3, [ROVER_GT] = 4, [ROVER_GE] = 6,
};

static inline INLINE bool rover_compare(const struct rover_table address_to table, const struct rover_comparison address_to comparison,
                          const struct rover_event address_to event, const p8 address_to const address_to pages)
{
        b64 a, b, c;

        if (comparison->form == ROVER_FORM_NUMBER)
        {
                a = rover_side(table, comparison->a, event, pages);
                b = rover_side(table, comparison->b, event, pages);
                return (rover_holds_for[comparison->op] >> ((a > b) - (a < b) + 1)) & 1;
        }
        if (rover_kind_is_word(comparison->type))
        {
                p32 one, two;
                const p8 address_to left = rover_word_at(table, comparison->a, event, pages, &one);
                const p8 address_to right;
                bool same;

                if (comparison->op == ROVER_TRUE)
                        return one != 0;
                right = rover_word_at(table, comparison->b, event, pages, &two);
                switch (comparison->op)
                {
                case ROVER_EQ:
                case ROVER_NE:
                        same = one == two && !memory_compare(left, right, one);
                        return comparison->op == ROVER_EQ ? same : !same;
                case ROVER_MATCH: return rover_glob(left, one, right, two);
                case ROVER_NOMATCH: return !rover_glob(left, one, right, two);
                }
                return false;
        }
        a = rover_number_at(table, comparison->a, event, pages);
        if (comparison->op == ROVER_TRUE)
                return a != 0;
        b = rover_number_at(table, comparison->b, event, pages);
        switch (comparison->op)
        {
        case ROVER_EQ: return a == b;
        case ROVER_NE: return a != b;
        case ROVER_LT: return a < b;
        case ROVER_LE: return a <= b;
        case ROVER_GT: return a > b;
        case ROVER_GE: return a >= b;
        case ROVER_BETWEEN:
                c = rover_number_at(table, comparison->c, event, pages);
                //      An upper end below the lower one wraps: 22:00 to 06:00 is the night.
                if (b <= c)
                        return a >= b && a <= c;
                return a >= b || a <= c;
        }
        return false;
}

/*
        Whether a rule holds for an event: 1 its then actions, 0 its else
        actions, -1 an on line's filter that did not match, which is not the
        rule's event at all and runs neither.

        Each line is an and of terms joined by or, and a term an and of
        comparisons. A term that has failed asks nothing more, and a line
        that has held asks nothing more, so a comparison is made at most once
        and only when it can still change the answer.
*/
static inline INLINE b32 rover_holds(const struct rover_table address_to table, const struct rover_rule address_to rule,
                       const struct rover_event address_to event, const p8 address_to const address_to pages)
{
        const struct rover_comparison address_to comparison = table->comparison + rule->comparison;
        bool line = false, term = true;

        for (positive at = 0; at < rule->comparisons; at++, comparison++)
        {
                if (term && !line)
                        term = rover_compare(table, comparison, event, pages);
                if (comparison->join & (ROVER_JOIN_OR | ROVER_JOIN_LINE))
                {
                        line |= term;
                        term = true;
                }
                if (comparison->join & ROVER_JOIN_LINE)
                {
                        if (!line)
                                return at < rule->trigger && !(rule->flags & ROVER_RULE_WHEN) ? -1 : 0;
                        line = false;
                }
        }
        return 1;
}

//      Whether a rule may fire now, by its once and its limit; a firing it
//      refuses is counted.
static bool rover_may(const struct rover_rule address_to rule, struct rover_run address_to run, p64 when)
{
        if ((rule->flags & ROVER_RULE_ONCE) && run->done)
                return false;
        if (rule->limit_ms && run->fires && when - run->last < (p64)rule->limit_ms * 1000000)
        {
                run->dropped++;
                return false;
        }
        return true;
}

static fn rover_fired(const struct rover_rule address_to rule, struct rover_run address_to run, p64 when)
{
        run->fires++;
        run->last = when;
        if (rule->flags & ROVER_RULE_ONCE)
                run->done = 1;
}

/*
        The walker: an event's run of rules, while each rule it meets is one
        whose comparisons all have the number form, in assembly, as a leaf
        (no call, so a kernel build takes the same body). It answers the hits
        it made in the low 32 bits; with bit 63 set, it stopped at the slot
        in bits 32 to 47, a rule it does not take, and rover_fire_rules asks
        that rule and the rest in C. place_room is the place in its low byte
        and the room in its high 32 bits. pages are never null here.

        A comparison is its two sides loaded (a state one movslq from its
        page, a constant one load from the pool, a field one load from the
        event) and which of below, equal and above its operator holds for
        read out of one immediate: 0x06431520 holds the mask of operator n in
        bits 4n to 4n+2, so the answer is that word shifted by four times the
        operator plus the side the compare fell on, its low bit. There is no
        table to load and no jump table to take; the C's lines and terms are
        bit 1 and bit 0 of one register.
*/
positive rover_walk_numbers(const struct rover_table address_to table, struct rover_run address_to runs,
                            const p8 address_to const address_to pages, const struct rover_event address_to event,
                            positive place_room, struct rover_hit address_to hits);

#define ROVER_WALK_STOPPED (1ull << 63)

/*
        The walker takes runs of fewer than ROVER_WALK_MOST rules, where it
        was measured faster, paired against the C in one process, a trial
        each in turn. On box (Zen 5), against gcc -O2 a run of one rule is 1
        to 9 percent faster through it (the dispatch, one condition, two) and
        four chord rules 7 percent; eight chord rules were 4 percent slower,
        the C's key loop beating the walker's at that length. On arm64 (Lima),
        against gcc a run of one rule is 15 to 21 percent faster, and four
        chord rules 2 percent slower (6 percent faster against clang), so
        arm64 stops at four.
*/
#if ARM64
#define ROVER_WALK_MOST 4u
#else
#define ROVER_WALK_MOST 8u
#endif

//      Whether the walker is asked at all: 1, but for BENCH_rover, which points it at a switch to time both sides.
#ifndef ROVER_WALKER_ASKED
#define ROVER_WALKER_ASKED 1
#endif

#if X64
#define ROVER_WALK_SIDE(at, next)                                                                                 \
    "movzbl " #at "(%r8), %ecx\n   movzwl " #at "+2(%r8), %edx\n   cmp $3, %ecx\n   jne 61f\n"                        \
    "mov (%rsp), %rax\n   cmpb $0, " #at "+1(%r8)\n   cmovne 8(%rsp), %rax\n   movslq (%rax,%rdx), %rax\n   jmp " next "\n" \
    "61: cmp $1, %ecx\n   jne 62f\n   mov 16(%rsp), %rax\n   mov (%rax,%rdx), %rax\n   jmp " next "\n"               \
    "62: xor %eax, %eax\n   movzbl 2(%r15), %ecx\n   cmp %ecx, %edx\n   jae " next "\n"                             \
    "lea (%rdx,%rdx,2), %rdx\n   mov 24(%r15,%rdx,8), %rax\n"

__asm__(
    ASM_FUNC(rover_walk_numbers)
    "push %rbx\n   push %rbp\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n   sub $56, %rsp\n"
    //  The frame: 0 and 8 the pages, 16 the pool, 24 the key wanted, 32 place and room, 40 the table, 48 the runs.
    "mov (%rdx), %rax\n   mov %rax, (%rsp)\n   mov 8(%rdx), %rax\n   mov %rax, 8(%rsp)\n"
    "mov 80(%rdi), %rax\n   mov %rax, 16(%rsp)\n   mov %r8, 32(%rsp)\n   mov %rdi, 40(%rsp)\n   mov %rsi, 48(%rsp)\n"
    "mov %rcx, %r15\n"
    //  The event's run: r13 its order, r14 its keys, r12 how many; rbx the slot, rbp the hits made.
    "movzwl (%r15), %eax\n   mov 32(%rdi), %rdx\n   movzwl (%rdx,%rax,4), %ecx\n   movzwl 2(%rdx,%rax,4), %r12d\n"
    "and $0x7fff, %r12d\n"
    "mov 16(%rdi), %r13\n   lea (%r13,%rcx,2), %r13\n   mov 24(%rdi), %r14\n   lea (%r14,%rcx,4), %r14\n"
    "mov $-1, %eax\n   cmpb $0, 2(%r15)\n   je 1f\n   mov 24(%r15), %rdx\n   test %rdx, %rdx\n   js 1f\n"
    "cmp %rax, %rdx\n   jae 1f\n   mov %edx, %eax\n"
    "1:  mov %eax, 24(%rsp)\n   xor %ebx, %ebx\n   xor %ebp, %ebp\n"
    //  The keys: a slot whose key is the one wanted or any.
    "10: cmp %r12, %rbx\n   jae 90f\n   mov (%r14,%rbx,4), %eax\n   cmp 24(%rsp), %eax\n   je 20f\n"
    "cmp $-1, %eax\n   je 20f\n   inc %rbx\n   jmp 10b\n"
    //  The rule: rdi; placed here, not off, and every comparison a number's, or handed back.
    "20: movzwl (%r13,%rbx,2), %eax\n   lea (%rax,%rax,4), %rdi\n   mov 40(%rsp), %rdx\n   shl $3, %rdi\n   add 8(%rdx), %rdi\n"
    "movzbl 2(%rdi), %ecx\n   cmp 32(%rsp), %cl\n   jne 80f\n"
    "movzbl 3(%rdi), %ecx\n   test $4, %cl\n   jnz 80f\n   test $16, %cl\n   jz 95f\n"
    //  Its comparisons, r8 to r11; r10 bit 0 the term, bit 1 the line.
    "movzwl 4(%rdi), %r8d\n   shl $4, %r8\n   add 40(%rdx), %r8\n   movzwl 6(%rdi), %r11d\n   shl $4, %r11\n   add %r8, %r11\n"
    "mov $1, %r10d\n"
    "30: cmp %r11, %r8\n   jae 50f\n   cmp $1, %r10d\n   jne 38f\n"
    ROVER_WALK_SIDE(4, "63f")
    "63: mov %rax, %rsi\n"
    ROVER_WALK_SIDE(8, "64f")
    "64: xor %ecx, %ecx\n   xor %edx, %edx\n   cmp %rax, %rsi\n   setg %cl\n   setl %dl\n   sub %edx, %ecx\n"
    "movzbl (%r8), %edx\n   lea 1(%rcx,%rdx,4), %ecx\n   mov $0x06431520, %eax\n   shr %cl, %eax\n   and $1, %eax\n"
    "and $2, %r10d\n   or %eax, %r10d\n"
    //  The join: or and the line's end fold the term into the line; the end asks the line.
    "38: movzbl 2(%r8), %ecx\n   test $3, %cl\n   jz 39f\n   lea (%r10,%r10), %eax\n   or %eax, %r10d\n   and $2, %r10d\n"
    "or $1, %r10d\n   test $2, %cl\n   jz 39f\n   test $2, %r10d\n   jz 45f\n   mov $1, %r10d\n"
    "39: add $16, %r8\n   jmp 30b\n"
    //  A line that did not hold: inside an on line's filters it is not the rule's event at all.
    "45: testb $1, 3(%rdi)\n   jnz 47f\n   mov 40(%rsp), %rdx\n   movzwl 4(%rdi), %eax\n   shl $4, %rax\n   add 40(%rdx), %rax\n"
    "sub %rax, %r8\n   shr $4, %r8\n   movzbl 8(%rdi), %eax\n   cmp %rax, %r8\n   jb 80f\n"
    "47: movzbl 10(%rdi), %ecx\n   test %ecx, %ecx\n   jz 80f\n   movzwl 12(%rdi), %edx\n   movzbl 9(%rdi), %eax\n"
    "add %eax, %edx\n   mov $1, %r8d\n   jmp 55f\n"
    "50: movzbl 9(%rdi), %ecx\n   movzwl 12(%rdi), %edx\n   xor %r8d, %r8d\n"
    //  ecx the actions, edx the first, r8 the else; the run (r10) says whether once and the limit let it.
    "55: movzwl (%r13,%rbx,2), %eax\n   lea (%rax,%rax,2), %r10\n   shl $3, %r10\n   add 48(%rsp), %r10\n"
    "testb $2, 3(%rdi)\n   jz 56f\n   cmpb $0, 17(%r10)\n   jne 80f\n"
    "56: mov 16(%rdi), %esi\n   test %esi, %esi\n   jz 57f\n   cmpl $0, 8(%r10)\n   je 57f\n"
    "imul $1000000, %rsi, %rsi\n   mov 8(%r15), %r11\n   sub (%r10), %r11\n   cmp %rsi, %r11\n   jae 57f\n"
    "incl 12(%r10)\n   jmp 80f\n"
    //  The hit, one store: the rule, its first action, the count and the else.
    "57: shl $16, %rdx\n   or %rdx, %rax\n   shl $32, %rcx\n   or %rcx, %rax\n   shl $40, %r8\n   or %r8, %rax\n"
    "mov %rax, (%r9,%rbp,8)\n   incl 8(%r10)\n   mov 8(%r15), %rax\n   mov %rax, (%r10)\n"
    "testb $2, 3(%rdi)\n   jz 58f\n   movb $1, 17(%r10)\n"
    "58: inc %rbp\n   mov 32(%rsp), %rax\n   shr $32, %rax\n   cmp %rax, %rbp\n   jae 90f\n"
    "80: inc %rbx\n   jmp 10b\n"
    "90: mov %rbp, %rax\n   jmp 99f\n"
    "95: mov %rbx, %rax\n   shl $32, %rax\n   or %rbp, %rax\n   bts $63, %rax\n"
    "99: add $56, %rsp\n   pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbp\n   pop %rbx\n"
    ASM_RET
    ASM_END(rover_walk_numbers)
);
#define ROVER_WALKER 1
#undef ROVER_WALK_SIDE
#elif ARM64
#define ROVER_WALK_SIDE(at, into, next)                                                                           \
    "ldrb w0, [x20, #" #at "]\n   ldrh w2, [x20, #" #at "+2]\n   cmp w0, #3\n   b.ne 61f\n"                            \
    "ldrb w0, [x20, #" #at "+1]\n   cmp w0, #0\n   csel x0, x6, x7, eq\n   ldrsw " into ", [x0, x2]\n   b " next "\n"  \
    "61: cmp w0, #1\n   b.ne 62f\n   ldr " into ", [x8, x2]\n   b " next "\n"                                           \
    "62: mov " into ", #0\n   ldrb w0, [x3, #2]\n   cmp w2, w0\n   b.hs " next "\n   mov x0, #24\n   mul x2, x2, x0\n"   \
    "add x2, x2, x3\n   ldr " into ", [x2, #24]\n"

/*
        The arm64 walker: the x86_64 block's algorithm, with every value it
        keeps in a register of its own (x6 and x7 the pages, x8 the pool, x9
        the rules, x10 the comparisons, x11 the order, x12 the keys, x13 the
        count, x14 the slot, x15 the hits made, w16 the key wanted, x17 the
        room), so it has no frame but the six registers it saves.
*/
__asm__(
    ASM_FUNC(rover_walk_numbers)
    "stp x19, x20, [sp, #-48]!\n   stp x21, x22, [sp, #16]\n   stp x23, x24, [sp, #32]\n"
    "ldp x6, x7, [x2]\n   ldr x8, [x0, #80]\n   ldr x9, [x0, #8]\n   ldr x10, [x0, #40]\n"
    "ldrh w2, [x3]\n   ldr x24, [x0, #32]\n   add x24, x24, x2, lsl #2\n   ldrh w2, [x24]\n   ldrh w13, [x24, #2]\n"
    "and w13, w13, #0x7fff\n   ldr x11, [x0, #16]\n   add x11, x11, x2, lsl #1\n   ldr x12, [x0, #24]\n   add x12, x12, x2, lsl #2\n"
    "mov w16, #-1\n   ldrb w2, [x3, #2]\n   cbz w2, 1f\n   ldr x2, [x3, #24]\n   tbnz x2, #63, 1f\n"
    "mov x24, #0xffffffff\n   cmp x2, x24\n   b.hs 1f\n   mov w16, w2\n"
    "1:  lsr x17, x4, #32\n   and w4, w4, #0xff\n   mov x14, #0\n   mov x15, #0\n"
    "10: cmp x14, x13\n   b.hs 90f\n   ldr w2, [x12, x14, lsl #2]\n   cmp w2, w16\n   b.eq 20f\n   cmn w2, #1\n   b.eq 20f\n"
    "add x14, x14, #1\n   b 10b\n"
    "20: ldrh w0, [x11, x14, lsl #1]\n   mov x24, #40\n   madd x19, x0, x24, x9\n"
    "ldrb w2, [x19, #2]\n   cmp w2, w4\n   b.ne 80f\n   ldrb w2, [x19, #3]\n   tbnz w2, #2, 80f\n   tbz w2, #4, 95f\n"
    "ldrh w20, [x19, #4]\n   add x20, x10, x20, lsl #4\n   ldrh w21, [x19, #6]\n   add x21, x20, x21, lsl #4\n   mov w22, #1\n"
    "30: cmp x20, x21\n   b.hs 50f\n   cmp w22, #1\n   b.ne 38f\n"
    ROVER_WALK_SIDE(4, "x23", "63f")
    "63:\n"
    ROVER_WALK_SIDE(8, "x24", "64f")
    "64: cmp x23, x24\n   cset w0, gt\n   csinv w0, w0, wzr, ge\n   add w0, w0, #1\n   ldrb w2, [x20]\n   add w0, w0, w2, lsl #2\n"
    "mov w2, #0x1520\n   movk w2, #0x0643, lsl #16\n   lsr w2, w2, w0\n   and w2, w2, #1\n   and w22, w22, #2\n   orr w22, w22, w2\n"
    "38: ldrb w0, [x20, #2]\n   tst w0, #3\n   b.eq 39f\n   orr w22, w22, w22, lsl #1\n   and w22, w22, #2\n   orr w22, w22, #1\n"
    "tbz w0, #1, 39f\n   tbz w22, #1, 45f\n   mov w22, #1\n"
    "39: add x20, x20, #16\n   b 30b\n"
    "45: ldrb w0, [x19, #3]\n   tbnz w0, #0, 47f\n   ldrh w0, [x19, #4]\n   add x0, x10, x0, lsl #4\n   sub x0, x20, x0\n"
    "lsr x0, x0, #4\n   ldrb w2, [x19, #8]\n   cmp x0, x2\n   b.lo 80f\n"
    "47: ldrb w21, [x19, #10]\n   cbz w21, 80f\n   ldrh w22, [x19, #12]\n   ldrb w0, [x19, #9]\n   add w22, w22, w0\n"
    "mov w23, #1\n   b 55f\n"
    "50: ldrb w21, [x19, #9]\n   ldrh w22, [x19, #12]\n   mov w23, #0\n"
    "55: ldrh w0, [x11, x14, lsl #1]\n   mov x24, #24\n   madd x24, x0, x24, x1\n"
    "ldrb w2, [x19, #3]\n   tbz w2, #1, 56f\n   ldrb w2, [x24, #17]\n   cbnz w2, 80f\n"
    "56: ldr w2, [x19, #16]\n   cbz w2, 57f\n   ldr w0, [x24, #8]\n   cbz w0, 57f\n   mov x0, #16960\n   movk x0, #15, lsl #16\n"
    "mul x2, x2, x0\n   ldr x0, [x3, #8]\n   ldr x20, [x24]\n   sub x0, x0, x20\n   cmp x0, x2\n   b.hs 57f\n"
    "ldr w0, [x24, #12]\n   add w0, w0, #1\n   str w0, [x24, #12]\n   b 80f\n"
    "57: ldrh w0, [x11, x14, lsl #1]\n   orr x0, x0, x22, lsl #16\n   orr x0, x0, x21, lsl #32\n   orr x0, x0, x23, lsl #40\n"
    "str x0, [x5, x15, lsl #3]\n   ldr w0, [x24, #8]\n   add w0, w0, #1\n   str w0, [x24, #8]\n   ldr x0, [x3, #8]\n   str x0, [x24]\n"
    "ldrb w0, [x19, #3]\n   tbz w0, #1, 58f\n   mov w0, #1\n   strb w0, [x24, #17]\n"
    "58: add x15, x15, #1\n   cmp x15, x17\n   b.hs 90f\n"
    "80: add x14, x14, #1\n   b 10b\n"
    "90: mov x0, x15\n   b 99f\n"
    "95: lsl x0, x14, #32\n   orr x0, x0, x15\n   orr x0, x0, #0x8000000000000000\n"
    "99: ldp x23, x24, [sp, #32]\n   ldp x21, x22, [sp, #16]\n   ldp x19, x20, [sp], #48\n"
    ASM_RET
    ASM_END(rover_walk_numbers)
);
#define ROVER_WALKER 1
#undef ROVER_WALK_SIDE
#else
#define ROVER_WALKER 0
#endif

//      The page the evaluator reads where a caller has none: every state on it is 0, the empty word.
static const p8 rover_zero_page[ROVER_PAGE_BYTES];

/*
        The next slot from at, below count, whose key is want or ANY: the
        keys of an event's run are a row of words, so a chord among many is
        a scan of them and not a walk of each rule.
*/
/*
        Sixteen keys and more are the library's vector scan; fewer, the two
        compares a key here, which measured faster below sixteen than the
        call (eight chord rules: 5.0 ns here against 5.9 through the scan on
        x86_64, 8.0 against 9.1 on arm64). riscv64's library body is the same
        word loop as this one, and nothing here could time it on hardware, so
        that machine keeps the loop, and so does a kernel build, which has no
        body of the routine at all (lib.c keeps it under #ifndef KERNEL_MODE,
        and the kernel owns no vector register for it).
*/
#if RISCV64 || defined(KERNEL_MODE)
#define ROVER_KEY_SCAN ((positive)-1)
#else
#define ROVER_KEY_SCAN 16u
#endif

static inline INLINE positive rover_key_next(const p32 address_to keys, positive at, positive count, p32 want)
{
#ifndef KERNEL_MODE
        if (count - at >= ROVER_KEY_SCAN)
                return at + memory_first_of_either_u32(keys + at, count - at, want, ROVER_KEY_ANY);
#endif
        while (at < count && keys[at] != want && keys[at] != ROVER_KEY_ANY)
                at++;
        return at;
}

/*
        The rules an event fires, among those placed where this runs: each
        hit is a rule and the run of actions it chose, in the table's order.
        Answers how many hits; past room, the rest are not looked at. from is
        the slot to start at, a candidate already, or ROVER_FROM_FIRST for the
        run's first; made is how many hits are in hits already.
*/
#define ROVER_FROM_FIRST ((positive)-1)

static __attribute__((__noinline__)) positive rover_fire_rules(const struct rover_table address_to table,
                                                           struct rover_run address_to runs,
                                                           const p8 address_to const address_to given,
                                                           const struct rover_event address_to event, p8 place,
                                                           struct rover_hit address_to hits, positive room,
                                                           positive from, positive made)
{
        positive count, at;
        struct rover_span span = table->index[event->id];
        const p16 address_to order = table->order + span.first;
        const p32 address_to keys = table->key + span.first;
        const p8 address_to pages[ROVER_PAGES] = {given[0] ? given[0] : rover_zero_page, given[1] ? given[1] : rover_zero_page, given[2] ? given[2] : rover_zero_page};
        p32 want = ROVER_KEY_ANY;

        //      The key the event's field 0 is, when it is one a rule could have.
        if (event->count && event->field[0].number >= 0 && event->field[0].number < (b64)ROVER_KEY_ANY)
                want = (p32)event->field[0].number;
        count = span.count & ROVER_SPAN_COUNT;
        at = from == ROVER_FROM_FIRST ? rover_key_next(keys, 0, count, want) : from;
        for (; at < count && made < room; at = rover_key_next(keys, at + 1, count, want))
        {
                p16 number = order[at];
                const struct rover_rule address_to rule = table->rule + number;
                b32 held;

                if (rule->place != place || (rule->flags & ROVER_RULE_OFF))
                        continue;
                held = rover_holds(table, rule, event, pages);
                if (held < 0 || (!held && !rule->otherwise) || !rover_may(rule, runs + number, event->when))
                        continue;
                hits[made].rule = number;
                hits[made].action = held ? rule->action : (p16)(rule->action + rule->then);
                hits[made].count = held ? rule->then : rule->otherwise;
                hits[made].otherwise = !held;
                hits[made].pad = 0;
                made++;
                rover_fired(rule, runs + number, event->when);
        }
        return made;
}

/*
        An event with no rules is one load and one branch, here, before any
        call. A short run goes to the walker, where there is one, straight
        from here; a rule it does not take, and the rest after it, are the
        C's from that slot.
*/
static inline INLINE positive rover_fire(const struct rover_table address_to table, struct rover_run address_to runs,
                                         const p8 address_to const address_to pages,
                                         const struct rover_event address_to event, p8 place,
                                         struct rover_hit address_to hits, positive room)
{
        p16 rules;

        if ((p32)event->id - 1 >= ROVER_EVENTS - 1 || !((rules = table->index[event->id].count) & ROVER_SPAN_COUNT))
                return 0;
#if ROVER_WALKER
        //      Only a run the table says is all numbers rules: one with another would be handed back at once.
        if ((rules & ROVER_SPAN_NUMBERS) && (rules & ROVER_SPAN_COUNT) < ROVER_WALK_MOST && room && ROVER_WALKER_ASKED)
        {
                const p8 address_to seen[ROVER_PAGES] = {pages[0] ? pages[0] : rover_zero_page, pages[1] ? pages[1] : rover_zero_page, pages[2] ? pages[2] : rover_zero_page};
                positive walked = rover_walk_numbers(table, runs, seen, event,
                                                     place | (room > 0xffffffffu ? 0xffffffffu : room) << 32, hits);

                if (!(walked & ROVER_WALK_STOPPED))
                        return (p32)walked;
                return rover_fire_rules(table, runs, pages, event, place, hits, room, (walked >> 32) & 0xffff,
                                        (p32)walked);
        }
#endif
        return rover_fire_rules(table, runs, pages, event, place, hits, room, ROVER_FROM_FIRST, 0);
}

/*
        The when rules some states changed under: changed has a bit for each
        state that did, and a rule that reads none of them is not looked at.
        A rule fires its then actions when its condition becomes true and its
        else actions when it stops being true. Loading a table is every state
        changing at once, so a when rule that already holds fires then.
*/
static positive rover_edges(const struct rover_table address_to table, struct rover_run address_to runs,
                            const p8 address_to const address_to given, p64 changed,
                            const struct rover_event address_to event, p8 place, struct rover_hit address_to hits,
                            positive room)
{
        positive made = 0;
        struct rover_span span = table->index[0];
        const p8 address_to pages[ROVER_PAGES] = {given[0] ? given[0] : rover_zero_page, given[1] ? given[1] : rover_zero_page, given[2] ? given[2] : rover_zero_page};

        for (positive at = span.first; at < (positive)span.first + (span.count & ROVER_SPAN_COUNT) && made < room;
             at++)
        {
                p16 number = table->order[at];
                const struct rover_rule address_to rule = table->rule + number;
                struct rover_run address_to run = runs + number;
                b32 held;

                if (rule->place != place || (rule->flags & ROVER_RULE_OFF) || !(rule->reads & changed))
                        continue;
                held = rover_holds(table, rule, event, pages) > 0;
                if (held == run->edge)
                        continue;
                run->edge = (p8)held;
                if ((!held && !rule->otherwise) || !rover_may(rule, run, event->when))
                        continue;
                hits[made].rule = number;
                hits[made].action = held ? rule->action : (p16)(rule->action + rule->then);
                hits[made].count = held ? rule->then : rule->otherwise;
                hits[made].otherwise = !held;
                hits[made].pad = 0;
                made++;
                rover_fired(rule, run, event->when);
        }
        return made;
}

//      An action's arguments, for the owner that does it: how many.
static positive rover_arguments(const struct rover_table address_to table, positive which,
                                const struct rover_event address_to event, const p8 address_to const address_to pages,
                                struct rover_value address_to into)
{
        const struct rover_action address_to action = table->action + which;

        for (positive at = 0; at < action->count; at++)
                rover_value_of(table, table->argument[action->argument + at], event, pages, &into[at]);
        return action->count;
}

// ---- the check -------------------------------------------------------------

static bool rover_refuse(struct rover_refusal address_to why, const char address_to text, p32 rule, p32 at)
{
        why->why = text;
        why->rule = rule;
        why->at = at;
        return false;
}

//      A string record of the pool: 4-aligned, a two-byte length of at most
//      most, the bytes and a NUL, all inside the pool.
static bool rover_string_ok(const struct rover_table address_to table, p32 at, positive most)
{
        p32 pool = table->head->pool;
        p16 length;

        if ((at & 3) || at > pool || pool - at < 3)
                return false;
        memory_copy(&length, table->pool + at, sizeof(length));
        if (length > most || pool - at - 2 < (p32)length + 1)
                return false;
        return table->pool[at + 2 + length] == 0;
}

static bool rover_is_number_source(p8 source)
{
        return source == ROVER_CONST_NUMBER || source == ROVER_STATE_NUMBER || source == ROVER_FIELD ||
               source == ROVER_CALL || source == ROVER_CLOCK || source == ROVER_WEEKDAY;
}

static bool rover_is_word_source(p8 source)
{
        return source == ROVER_CONST_WORD || source == ROVER_STATE_WORD || source == ROVER_FIELD;
}

//      A constant's value, which the check holds to its type's range.
static bool rover_constant_ok(const struct rover_table address_to table, struct rover_operand operand, p8 type)
{
        const struct rover_type_row address_to row = &rover_types[type];
        b64 number;

        if (operand.source != ROVER_CONST_NUMBER)
                return true;
        memory_copy(&number, table->pool + operand.index, sizeof(number));
        switch (row->kind)
        {
        case ROVER_T_INT: return number >= row->low && number <= row->high;
        case ROVER_T_PERCENT: return number >= 0 && number <= 10000;
        case ROVER_T_DURATION: return number >= 0 && number <= ROVER_DURATION_MOST;
        case ROVER_T_CLOCK: return number >= 0 && number < 1440;
        case ROVER_T_WEEKDAY: return number >= 0 && number < 7;
        case ROVER_T_ENUM:
        case ROVER_T_ONOFF: return number >= 0 && number < row->count;
        case ROVER_T_CHORD: return number >= 0 && (number >> 16) <= 15 && (number & 0xffff) < 768;
        }
        return false;
}

/*
        One operand: in range, of the shape it says it is, a number or a word
        as the caller wants (want 1 a number, 2 a word, 0 either), and, in a
        kernel rule, the kernel's own: no desktop state, no routine the
        kernel has no body of.
*/
static const char address_to rover_operand_check(const struct rover_table address_to table,
                                                  struct rover_operand operand, b32 want, bool kernel,
                                                  bool in_call)
{
        if (operand.source >= ROVER_SOURCES || operand.source == ROVER_NONE)
                return "an operand of no kind";
        if (want == 1 && !rover_is_number_source(operand.source))
                return "a word where a number goes";
        if (want == 2 && !rover_is_word_source(operand.source))
                return "a number where a word goes";
        switch (operand.source)
        {
        case ROVER_CONST_NUMBER:
                if (operand.page >= ROVER_TYPES || rover_kind_is_word(rover_types[operand.page].kind))
                        return "a number constant of a word's type";
                if ((operand.index & 7) || (p32)operand.index + 8 > table->head->pool)
                        return "a number outside the pool";
                return null;
        case ROVER_CONST_WORD:
                if (operand.page >= ROVER_TYPES || !rover_kind_is_word(rover_types[operand.page].kind))
                        return "a word constant of a number's type";
                if (!rover_string_ok(table, operand.index,
                                     rover_types[operand.page].kind == ROVER_T_TEXT ? ROVER_TEXT_MOST : ROVER_WORD_MOST))
                        return "a word outside the pool, too long, or not ended";
                return null;
        case ROVER_STATE_NUMBER:
        case ROVER_STATE_WORD:
                if (operand.page >= ROVER_PAGES)
                        return "a state on no page";
                if (kernel && operand.page != ROVER_PAGE_KERNEL)
                        return "a kernel rule reads a state the kernel does not have";
                if ((operand.index & 3) || operand.index < ROVER_PAGE_HEAD ||
                    (p32)operand.index + (operand.source == ROVER_STATE_WORD ? ROVER_WORD_SLOT : 4) > ROVER_PAGE_BYTES)
                        return "a state outside its page";
                return null;
        case ROVER_FIELD:
                return operand.index < ROVER_FIELDS ? null : "a field past the fourth";
        case ROVER_CALL:
                if (in_call)
                        return "a call inside a call";
                if (operand.index >= table->head->calls)
                        return "a call past the calls";
                if (!kernel)
                        return null;
                if (!rover_functions[table->call[operand.index].function].kernel ||
                    !rover_functions[table->call[operand.index].function].routine)
                        return "a kernel rule calls a routine the kernel has no body of";
                //      The call's arguments were checked as values; in a kernel rule they are the kernel's too.
                for (positive at = 0; at < table->call[operand.index].count; at++)
                {
                        const char address_to why =
                            rover_operand_check(table, table->call[operand.index].argument[at], 0, true, true);

                        if (why)
                                return why;
                }
                return null;
        case ROVER_CLOCK:
        case ROVER_WEEKDAY:
                return operand.index || operand.page ? "a clock with an index" : null;
        }
        return "an operand of no kind";
}

static const char address_to rover_comparison_check(const struct rover_table address_to table,
                                                     const struct rover_comparison address_to comparison, bool kernel)
{
        const char address_to why;
        b32 want;

        if (comparison->op >= ROVER_OPS || comparison->type >= ROVER_T_KINDS ||
            (comparison->join & ~(ROVER_JOIN_OR | ROVER_JOIN_LINE)))
                return "a comparison of no kind";
        want = rover_kind_is_word(comparison->type) ? 2 : 1;
        if (rover_kind_is_word(comparison->type) ? comparison->op > ROVER_NE && comparison->op != ROVER_MATCH &&
                                                       comparison->op != ROVER_NOMATCH
                                                 : comparison->op == ROVER_MATCH || comparison->op == ROVER_NOMATCH)
                return "an operator its type does not have";
        if (comparison->form != rover_form_of(comparison))
                return "a comparison whose form is not its shape's";
        if ((why = rover_operand_check(table, comparison->a, want, kernel, false)))
                return why;
        if (comparison->op == ROVER_TRUE)
                return comparison->b.source || comparison->c.source || comparison->b.page || comparison->b.index ||
                               comparison->c.page || comparison->c.index
                           ? "a truth with a second side"
                           : null;
        if ((why = rover_operand_check(table, comparison->b, want, kernel, false)))
                return why;
        if (comparison->op != ROVER_BETWEEN)
                return comparison->c.source || comparison->c.page || comparison->c.index ? "a third side" : null;
        return rover_operand_check(table, comparison->c, want, kernel, false);
}

static const char address_to rover_call_check(const struct rover_table address_to table,
                                               const struct rover_call address_to asked)
{
        const struct rover_function_row address_to row;
        const char address_to why;

        if (asked->function >= ROVER_FUNCTIONS || asked->pad)
                return "a call to no routine";
        row = &rover_functions[asked->function];
        if (asked->count != row->count)
                return "a call with another count of arguments than its routine takes";
        for (positive at = 0; at < ROVER_ARGUMENTS; at++)
        {
                if (at >= asked->count)
                {
                        if (asked->argument[at].source || asked->argument[at].page || asked->argument[at].index)
                                return "an argument past the call's count";
                        continue;
                }
                if ((why = rover_operand_check(table, asked->argument[at], row->signature[at] == 'i' ? 1 : 2,
                                               false, true)))
                        return why;
        }
        return null;
}

//      A script's name: a word of [a-z0-9._-] that is not . or .., so it
//      names a file in the scripts' directory and nothing above it.
static bool rover_script_ok(const struct rover_table address_to table, p16 at)
{
        p16 length;
        const p8 address_to name = table->pool + at + 2;

        memory_copy(&length, table->pool + at, sizeof(length));
        if (!length || (length == 1 && name[0] == '.') || (length == 2 && name[0] == '.' && name[1] == '.'))
                return false;
        for (positive i = 0; i < length; i++)
        {
                p8 c = name[i];

                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                        return false;
        }
        return true;
}

static const char address_to rover_action_check(const struct rover_table address_to table,
                                                 const struct rover_action address_to action, bool kernel)
{
        const struct rover_primitive_row address_to primitive = null;
        const struct rover_operand address_to argument;
        const char address_to why;

        if (action->pad || action->runs >= ROVER_PLACES || action->count > ROVER_ARGUMENTS ||
            (positive)action->argument + action->count > table->head->arguments)
                return "an action of no shape";
        argument = table->argument + action->argument;
        //      What root runs is the rule file's own words: constants only where a program, its words or a cue are named.
        if (action->kind == ROVER_RUN || action->kind == ROVER_TOOL || action->kind == ROVER_EMIT ||
            (action->kind == ROVER_DO && action->id == 14))
                for (positive k = 0; k < action->count; k++)
                        if (argument[k].source != ROVER_CONST_NUMBER && argument[k].source != ROVER_CONST_WORD)
                                return "a program, its words or a cue named by an event's field or a state";
        switch (action->kind)
        {
        case ROVER_DO:
                primitive = rover_primitive_find(action->id);
                if (!primitive)
                        return "a primitive the schema does not have";
                if (primitive->klass == ROVER_DELIBERATE)
                        return "a deliberate primitive, which only a person may ask for";
                if (action->runs != primitive->runs)
                        return "a primitive done somewhere it does not run";
                //      A last argument of words takes none or more, up to the four an action has.
                if (primitive->count && rover_types[primitive->type[primitive->count - 1]].kind == ROVER_T_WORDS
                        ? action->count + 1 < primitive->count
                        : action->count != primitive->count)
                        return "a primitive with another count of arguments than it takes";
                break;
        case ROVER_RUN:
                if (!action->count)
                        return "a run with no script";
                if (argument[0].source != ROVER_CONST_WORD || action->runs != ROVER_RUNNER)
                        return "a run whose script is not a word";
                break;
        case ROVER_TOOL:
                if (action->id >= ROVER_TOOLS || action->runs != ROVER_RUNNER)
                        return "a tool the schema does not have";
                if (rover_tool_class[action->id] == ROVER_DELIBERATE)
                        return "a deliberate tool, which only a person runs";
                break;
        case ROVER_EMIT:
                if (action->id != ROVER_CUE || action->count != 1 || action->runs != ROVER_RUNNER)
                        return "an emit of anything but a cue";
                break;
        case ROVER_SET:
        {
                const struct rover_switch_row address_to row = rover_switch_find(action->id);
                b32 want = row && rover_kind_is_word(rover_types[row->type].kind) ? 2 : 1;

                if (!row || action->count != 1 || action->runs != ROVER_RUNNER)
                        return "a set of a switch the schema does not have";
                if (argument[0].source == ROVER_CALL || (why = rover_operand_check(table, argument[0], want, kernel, false)))
                        return why ? why : "a call as an action's argument";
                if (!rover_constant_ok(table, argument[0], row->type))
                        return "an argument outside its type's range";
                //      A switch with a deliberate value takes a constant, and never that one.
                if (row->deliberate >= 0 &&
                    (argument[0].source != ROVER_CONST_NUMBER ||
                     memory_load_unaligned(b64, table->pool + argument[0].index) == row->deliberate))
                        return "a deliberate value of a switch, which only a person may set";
                return null;
        }
        default:
                return "an action of no kind";
        }
        for (positive at = 0; at < action->count; at++)
        {
                b32 want = 2;

                p8 type = primitive ? primitive->type[at < primitive->count ? at : primitive->count - 1] : 0;

                if (primitive && !rover_kind_is_word(rover_types[type].kind))
                        want = 1;
                if (argument[at].source == ROVER_CALL)
                        return "a call as an action's argument";
                if ((why = rover_operand_check(table, argument[at], want, kernel, false)))
                        return why;
                if (primitive && !rover_constant_ok(table, argument[at], type))
                        return "an argument outside its type's range";
                if (primitive && argument[at].source == ROVER_CONST_WORD &&
                    !rover_kind_is_word(rover_types[argument[at].page].kind))
                        return "an argument of another type";
        }
        if (action->kind == ROVER_RUN && !rover_script_ok(table, argument[0].index))
                return "a script name with a slash or a byte outside [a-z0-9._-]";
        return null;
}

static const char address_to rover_rule_check(const struct rover_table address_to table,
                                               const struct rover_rule address_to rule)
{
        const struct rover_event_row address_to event = null;
        bool kernel = rule->place == ROVER_KERNEL;
        const char address_to why;
        p16 length;

        if (rule->pad || rule->pad2 || rule->pad3 || (rule->flags & ~ROVER_RULE_FLAGS) || rule->place >= ROVER_PLACES)
                return "a rule of no shape";
        if ((rule->event == 0) != !!(rule->flags & ROVER_RULE_WHEN))
                return "a when rule with an event, or an on rule with none";
        if (rule->event)
        {
                event = rule->event < ROVER_EVENTS ? rover_event_find(rule->event) : null;
                if (!event)
                        return "an event the schema does not have";
                //      A rule runs where its event is posted, or in the runner the kernel hands it to.
                if (rule->place != event->source && !(event->source == ROVER_KERNEL && rule->place == ROVER_RUNNER))
                        return "a rule placed where its event never arrives";
        }
        if (!rover_string_ok(table, rule->name, ROVER_NAME_MOST))
                return "a rule's name outside the pool";
        memory_copy(&length, table->pool + rule->name, sizeof(length));
        if (!length)
                return "a rule with no name";
        if (rule->about && !rover_string_ok(table, rule->about, ROVER_TEXT_MOST))
                return "an about outside the pool";
        if (rule->limit_ms > ROVER_LIMIT_MOST_MS)
                return "a limit past a day";
        if ((positive)rule->comparison + rule->comparisons > table->head->comparisons ||
            rule->trigger > rule->comparisons || ((rule->flags & ROVER_RULE_WHEN) && !rule->trigger))
                return "a run of comparisons outside the table";
        if (rule->comparisons && !(table->comparison[rule->comparison + rule->comparisons - 1].join & ROVER_JOIN_LINE))
                return "a rule whose last comparison does not end its line";
        if (!rule->then || (positive)rule->action + rule->then + rule->otherwise > table->head->actions)
                return "a run of actions outside the table";
        for (positive at = 0; at < rule->comparisons; at++)
        {
                const struct rover_comparison address_to comparison = table->comparison + rule->comparison + at;

                if ((why = rover_comparison_check(table, comparison, kernel)))
                        return why;
                //      An on line's filter is one comparison, a field against a constant, ending its line.
                if (at < rule->trigger && !(rule->flags & ROVER_RULE_WHEN) &&
                    (comparison->a.source != ROVER_FIELD || comparison->a.index >= event->fields ||
                     (comparison->b.source != ROVER_CONST_NUMBER && comparison->b.source != ROVER_CONST_WORD) ||
                     (comparison->op != ROVER_EQ && comparison->op != ROVER_MATCH) ||
                     !(comparison->join & ROVER_JOIN_LINE)))
                        return "an on line's filter that is not a field's value";
                if ((rule->flags & ROVER_RULE_WHEN) && at < rule->trigger &&
                    ((comparison->join & ROVER_JOIN_LINE) != 0) != (at + 1 == rule->trigger))
                        return "a when line that is not one line";
        }
        for (positive at = 0; at < (positive)rule->then + rule->otherwise; at++)
                if ((why = rover_action_check(table, table->action + rule->action + at, kernel)))
                        return why;
        if (!!(rule->flags & ROVER_RULE_NUMBERS) != rover_rule_numbers(rule, table->comparison))
                return "a rule whose numbers flag is not its comparisons' forms";
        //      Only the kernel can drop an event before its readers, and only an event of its own.
        if ((rule->flags & ROVER_RULE_TAKE) && (!kernel || !event || event->source != ROVER_KERNEL))
                return "a take on a rule outside the kernel";
        return null;
}

//      An absolute path's word: a pool string that starts with a slash.
static bool rover_path_ok(const struct rover_table address_to table, p32 at)
{
        return rover_string_ok(table, at, ROVER_WORD_MOST) && table->pool[at + 2] == '/';
}

/*
        One service: its words in the pool, its run words and its program a
        path, its needs only services before it (so needs have no cycle), its
        policy in range, and its if lines comparisons over states alone.
*/
static const char address_to rover_service_check(const struct rover_table address_to table, p32 which)
{
        const struct rover_service address_to service = table->service + which;
        const char address_to why;
        p16 length;

        if (service->pad[0] || service->pad[1] || service->pad[2] || (service->flags & ~ROVER_SERVICE_OFF))
                return "a service of no shape";
        if (!rover_string_ok(table, service->name, ROVER_NAME_MOST))
                return "a service's name outside the pool";
        length = memory_load_unaligned(p16, table->pool + service->name);
        if (!length)
                return "a service with no name";
        for (p32 other = 0; other < which; other++)
        {
                const struct rover_service address_to before = table->service + other;

                if (memory_load_unaligned(p16, table->pool + before->name) == length &&
                    !memory_compare(table->pool + before->name + 2, table->pool + service->name + 2, length))
                        return "two services of one name";
        }
        if ((service->ready && !rover_path_ok(table, service->ready)) ||
            (service->log && !rover_path_ok(table, service->log)) ||
            (service->about && !rover_string_ok(table, service->about, ROVER_TEXT_MOST)))
                return "a service's path that is not one, or an about outside the pool";
        if (!service->count || (positive)service->argument + service->count > table->head->arguments)
                return "a service with no run, or its run outside the table";
        for (positive at = 0; at < service->count; at++)
        {
                struct rover_operand word = table->argument[service->argument + at];

                if (word.source != ROVER_CONST_WORD || rover_operand_check(table, word, 2, false, false))
                        return "a service's run that is not words";
        }
        if (!rover_path_ok(table, table->argument[service->argument].index))
                return "a service whose program is not a path";
        for (positive at = 0; at < ROVER_NEEDS; at++)
                if (service->need[at] != ROVER_SERVICE_NONE && service->need[at] >= which)
                        return "a service that needs one that is not before it";
        if (!service->lives || service->lives > 16 || service->short_ms > ROVER_LIMIT_MOST_MS ||
            service->wait_ms[0] > 3600000 || service->wait_ms[1] > 3600000 || service->wait_ms[2] > 3600000)
                return "a service's restart policy out of range";
        if ((positive)service->comparison + service->comparisons > table->head->comparisons)
                return "a service's if lines outside the table";
        if (service->comparisons &&
            !(table->comparison[service->comparison + service->comparisons - 1].join & ROVER_JOIN_LINE))
                return "a service whose last comparison does not end its line";
        for (positive at = 0; at < service->comparisons; at++)
        {
                const struct rover_comparison address_to comparison = table->comparison + service->comparison + at;

                if (comparison->a.source == ROVER_FIELD || comparison->b.source == ROVER_FIELD ||
                    comparison->c.source == ROVER_FIELD)
                        return "a service's if line that reads an event's field";
                if ((why = rover_comparison_check(table, comparison, false)))
                        return why;
        }
        return null;
}

/*
        Whether a service is wanted now: not off, and its if lines hold over
        the pages at realtime (the turn's seconds since the epoch, which a
        clock or weekday if line reads). What the runner asks every turn; a
        kernel build has no services to run, and keeps it unused.
*/
static __attribute__((unused)) bool rover_service_wanted(const struct rover_table address_to table, positive which,
                                 const p8 address_to const address_to given, b64 realtime)
{
        const struct rover_service address_to service = table->service + which;
        const p8 address_to pages[ROVER_PAGES] = {given[0] ? given[0] : rover_zero_page, given[1] ? given[1] : rover_zero_page, given[2] ? given[2] : rover_zero_page};
        struct rover_rule rule;
        struct rover_event none;

        if (service->flags & ROVER_SERVICE_OFF)
                return false;
        memory_zero(&rule, sizeof(rule));
        memory_zero(&none, sizeof(none));
        none.realtime = realtime;
        rule.comparison = service->comparison;
        rule.comparisons = service->comparisons;
        rule.flags = ROVER_RULE_WHEN;
        return rover_holds(table, &rule, &none, pages) > 0;
}

/*
        The whole table, before anything reads it. Every count and offset
        inside the table, every section where the counts put it, every
        reference in range, every string ended, the schema the same, every
        kernel rule the kernel's own, and no deliberate primitive anywhere.
        A table that passes can be walked by the evaluator without a bound
        check of its own; on success view says where each section is.
*/
static bool rover_check(const p8 address_to bytes, positive size, struct rover_table address_to view,
                        struct rover_refusal address_to why)
{
        const struct rover_head address_to head = (const struct rover_head address_to)bytes;
        positive at[11];
        positive total = 0;
        p8 seen[ROVER_TABLE_MOST / sizeof(struct rover_rule) / 8 + 1];

        if (size < sizeof(struct rover_head) || size > ROVER_TABLE_MOST || ((positive)bytes & 7))
                return rover_refuse(why, "a table smaller than its head, larger than 64 KiB, or not 8-aligned",
                                    ROVER_NO_RULE, 0);
        if (head->magic != ROVER_MAGIC || head->version != ROVER_VERSION || head->services > ROVER_SERVICES_MOST)
                return rover_refuse(why, "not a rover table of this version", ROVER_NO_RULE, 0);
        if (head->schema != ROVER_SCHEMA_HASH)
                return rover_refuse(why, "a table of another schema: compile its text again", ROVER_NO_RULE, 0);
        if (head->size != size || rover_layout(head, at) != size)
                return rover_refuse(why, "a table whose size is not what its counts make", ROVER_NO_RULE, 0);

        view->head = head;
        view->rule = (const struct rover_rule address_to)(bytes + at[0]);
        view->order = (const p16 address_to)(bytes + at[1]);
        view->key = (const p32 address_to)(bytes + at[2]);
        view->index = (const struct rover_span address_to)(bytes + at[3]);
        view->comparison = (const struct rover_comparison address_to)(bytes + at[4]);
        view->call = (const struct rover_call address_to)(bytes + at[5]);
        view->action = (const struct rover_action address_to)(bytes + at[6]);
        view->argument = (const struct rover_operand address_to)(bytes + at[7]);
        view->note = (const struct rover_note address_to)(bytes + at[8]);
        view->pool = bytes + at[9];
        view->service = (const struct rover_service address_to)(bytes + at[10]);

        //      The pool opens with the empty string, which is what an about of 0 is.
        if (head->pool < 4 || view->pool[0] || view->pool[1] || view->pool[2] || view->pool[3])
                return rover_refuse(why, "a pool that does not open with the empty string", ROVER_NO_RULE, 0);

        //      Every rule once in order, and each event's rules a run of it, in id order.
        memory_fill(seen, 0, sizeof(seen));
        for (positive id = 0; id < ROVER_EVENTS; id++)
        {
                struct rover_span span = view->index[id];
                bool numbers = true;

                span.count &= ROVER_SPAN_COUNT;
                if (span.first != total || total + span.count > head->rules)
                        return rover_refuse(why, "an index that is not the order's runs", ROVER_NO_RULE, (p32)id);
                for (positive k = span.first; k < (positive)span.first + span.count; k++)
                {
                        p16 number = view->order[k];

                        if (number >= head->rules || (seen[number >> 3] & (1u << (number & 7))) ||
                            view->rule[number].event != id)
                                return rover_refuse(why, "an order that is not each rule once, under its event",
                                                    ROVER_NO_RULE, (p32)k);
                        seen[number >> 3] |= (p8)(1u << (number & 7));
                        numbers = numbers && (view->rule[number].flags & ROVER_RULE_NUMBERS);
                }
                //      The walker's hint is a promise about the rules' flags, which the rules' check holds.
                if (!!(view->index[id].count & ROVER_SPAN_NUMBERS) != (numbers && span.count))
                        return rover_refuse(why, "a span whose numbers bit is not its rules'", ROVER_NO_RULE, (p32)id);
                total += span.count;
        }
        if (total != head->rules)
                return rover_refuse(why, "an index that leaves rules out", ROVER_NO_RULE, 0);
        for (positive k = head->rules; k < rover_align8(head->rules * 2) / 2; k++)
                if (view->order[k])
                        return rover_refuse(why, "padding that is not zero", ROVER_NO_RULE, 0);
        for (positive k = head->rules; k < rover_align8(head->rules * 4) / 4; k++)
                if (view->key[k])
                        return rover_refuse(why, "padding that is not zero", ROVER_NO_RULE, 0);

        for (positive k = 0; k < head->calls; k++)
        {
                const char address_to reason = rover_call_check(view, view->call + k);

                if (reason)
                        return rover_refuse(why, reason, ROVER_NO_RULE, (p32)k);
        }
        for (positive k = 0; k < head->rules; k++)
        {
                const char address_to reason = rover_rule_check(view, view->rule + k);

                if (reason)
                        return rover_refuse(why, reason, (p32)k, 0);
        }
        //      After the rules, whose comparison runs are now known to be inside the table.
        for (positive k = 0; k < head->rules; k++)
                if (view->key[k] != rover_key_of(view->rule + view->order[k], view->comparison, view->pool))
                        return rover_refuse(why, "a key that is not its rule's", (p32)view->order[k], 0);
        for (positive k = 0; k < head->services; k++)
        {
                const char address_to reason = rover_service_check(view, (p32)k);

                if (reason)
                        return rover_refuse(why, reason, ROVER_NO_RULE, (p32)k);
        }
        for (positive k = 0; k < head->notes; k++)
                if (((view->note[k].rule & ROVER_NOTE_SERVICE) ? (view->note[k].rule & ~ROVER_NOTE_SERVICE) >= head->services
                                                               : view->note[k].rule >= head->rules) ||
                    !rover_string_ok(view, view->note[k].text, ROVER_TEXT_MOST))
                        return rover_refuse(why, "a note outside the table", ROVER_NO_RULE, (p32)k);
        why->why = null;
        return true;
}

#endif /* ROVER_INCLUDED */
