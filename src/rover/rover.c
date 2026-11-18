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
#define ROVER_VERSION 1u
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
#define ROVER_PAGES 2u

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
#define ROVER_T_KINDS 10u

#define ROVER_RULE_WHEN 1u
#define ROVER_RULE_ONCE 2u
#define ROVER_RULE_OFF 4u
#define ROVER_RULE_PLACED 8u
#define ROVER_RULE_FLAGS 15u

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

#define ROVER_CHORD_SHIFT 1u
#define ROVER_CHORD_CTRL 2u
#define ROVER_CHORD_ALT 4u
#define ROVER_CHORD_SUPER 8u

struct rover_head
{
        p32 magic;
        p16 version;
        p16 flags; // none yet; nonzero is refused
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

struct rover_span
{
        p16 first;
        p16 count;
};

struct rover_comparison
{
        p8 op;
        p8 type;
        p8 join;
        p8 pad;
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

typedef b64(address_to rover_function)(const struct rover_value address_to value);

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
        const struct rover_span address_to index;
        const struct rover_comparison address_to comparison;
        const struct rover_call address_to call;
        const struct rover_action address_to action;
        const struct rover_operand address_to argument;
        const struct rover_note address_to note;
        const p8 address_to pool;
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
        table must be: the head, then rules, order, index, comparisons, calls,
        actions, arguments, notes and the pool, each at a multiple of eight.
*/
static positive rover_layout(const struct rover_head address_to head, positive address_to at)
{
        positive reach = sizeof(struct rover_head);

        at[0] = reach, reach = rover_align8(reach + (positive)head->rules * sizeof(struct rover_rule));
        at[1] = reach, reach = rover_align8(reach + (positive)head->rules * sizeof(p16));
        at[2] = reach, reach = rover_align8(reach + ROVER_EVENTS * sizeof(struct rover_span));
        at[3] = reach, reach = rover_align8(reach + (positive)head->comparisons * sizeof(struct rover_comparison));
        at[4] = reach, reach = rover_align8(reach + (positive)head->calls * sizeof(struct rover_call));
        at[5] = reach, reach = rover_align8(reach + (positive)head->actions * sizeof(struct rover_action));
        at[6] = reach, reach = rover_align8(reach + (positive)head->arguments * sizeof(struct rover_operand));
        at[7] = reach, reach = rover_align8(reach + (positive)head->notes * sizeof(struct rover_note));
        at[8] = reach, reach = rover_align8(reach + head->pool);
        return reach;
}

static const struct rover_event_row address_to rover_event_find(positive id)
{
        for (positive at = 0; at < ROVER_EVENT_ROWS; at++)
                if (rover_event_rows[at].id == id)
                        return &rover_event_rows[at];
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
        return kind == ROVER_T_WORD || kind == ROVER_T_TEXT;
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
        struct rover_value value[ROVER_ARGUMENTS];

        if (!row->routine)
                return 0;
        for (positive at = 0; at < asked->count; at++)
        {
                rover_value_of(table, asked->argument[at], event, pages, &value[at]);
                if (row->signature[at] == 'i')
                {
                        if (value[at].number < row->low[at])
                                value[at].number = row->low[at];
                        if (value[at].number > row->high[at])
                                value[at].number = row->high[at];
                }
        }
        return row->routine(value);
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
static bool rover_glob(const p8 address_to text, p32 length, const p8 address_to pattern, p32 size)
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

//      The rare numbers, out of the comparison's way: a call, the clock, the weekday.
static __attribute__((noinline)) b64 rover_number_far(const struct rover_table address_to table,
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

static inline INLINE bool rover_compare(const struct rover_table address_to table, const struct rover_comparison address_to comparison,
                          const struct rover_event address_to event, const p8 address_to const address_to pages)
{
        b64 a, b, c;

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
        The rules an event fires, among those placed where this runs: each
        hit is a rule and the run of actions it chose, in the table's order.
        Answers how many hits; past room, the rest are not looked at.
*/
static positive rover_fire(const struct rover_table address_to table, struct rover_run address_to runs,
                           const p8 address_to const address_to pages, const struct rover_event address_to event,
                           p8 place, struct rover_hit address_to hits, positive room)
{
        positive made = 0;
        struct rover_span span;

        if (!event->id || event->id >= ROVER_EVENTS)
                return 0;
        span = table->index[event->id];
        for (positive at = span.first; at < (positive)span.first + span.count && made < room; at++)
        {
                p16 number = table->order[at];
                const struct rover_rule address_to rule = table->rule + number;
                b32 held;

                if (rule->place != place || (rule->flags & ROVER_RULE_OFF))
                        continue;
                //      A rule keyed on a number (a chord, every on line's filter of a number) is passed over at its
                //      key: thirty two chord rules are thirty one loads and compares, not thirty one walks.
                if (rule->trigger)
                {
                        const struct rover_comparison address_to key = table->comparison + rule->comparison;
                        b64 value;

                        if (key->op == ROVER_EQ && key->b.source == ROVER_CONST_NUMBER)
                        {
                                value = memory_load_unaligned(b64, table->pool + key->b.index);
                                if (key->a.index >= event->count || event->field[key->a.index].number != value)
                                        continue;
                        }
                }
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
        The when rules some states changed under: changed has a bit for each
        state that did, and a rule that reads none of them is not looked at.
        A rule fires its then actions when its condition becomes true and its
        else actions when it stops being true. Loading a table is every state
        changing at once, so a when rule that already holds fires then.
*/
static positive rover_edges(const struct rover_table address_to table, struct rover_run address_to runs,
                            const p8 address_to const address_to pages, p64 changed,
                            const struct rover_event address_to event, p8 place, struct rover_hit address_to hits,
                            positive room)
{
        positive made = 0;
        struct rover_span span = table->index[0];

        for (positive at = span.first; at < (positive)span.first + span.count && made < room; at++)
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

        if (comparison->op >= ROVER_OPS || comparison->type >= ROVER_T_KINDS || comparison->pad ||
            (comparison->join & ~(ROVER_JOIN_OR | ROVER_JOIN_LINE)))
                return "a comparison of no kind";
        want = rover_kind_is_word(comparison->type) ? 2 : 1;
        if (rover_kind_is_word(comparison->type) ? comparison->op > ROVER_NE && comparison->op != ROVER_MATCH &&
                                                       comparison->op != ROVER_NOMATCH
                                                 : comparison->op == ROVER_MATCH || comparison->op == ROVER_NOMATCH)
                return "an operator its type does not have";
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
                if (action->count != primitive->count)
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
                break;
        case ROVER_EMIT:
                if (action->id != ROVER_CUE || action->count != 1 || action->runs != ROVER_RUNNER)
                        return "an emit of anything but a cue";
                break;
        default:
                return "an action of no kind";
        }
        for (positive at = 0; at < action->count; at++)
        {
                b32 want = 2;

                if (primitive && !rover_kind_is_word(rover_types[primitive->type[at]].kind))
                        want = 1;
                if (argument[at].source == ROVER_CALL)
                        return "a call as an action's argument";
                if ((why = rover_operand_check(table, argument[at], want, kernel, false)))
                        return why;
                if (primitive && !rover_constant_ok(table, argument[at], primitive->type[at]))
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
        return null;
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
        positive at[9];
        positive total = 0;
        p8 seen[ROVER_TABLE_MOST / sizeof(struct rover_rule) / 8 + 1];

        if (size < sizeof(struct rover_head) || size > ROVER_TABLE_MOST || ((positive)bytes & 7))
                return rover_refuse(why, "a table smaller than its head, larger than 64 KiB, or not 8-aligned",
                                    ROVER_NO_RULE, 0);
        if (head->magic != ROVER_MAGIC || head->version != ROVER_VERSION || head->flags)
                return rover_refuse(why, "not a rover table of this version", ROVER_NO_RULE, 0);
        if (head->schema != ROVER_SCHEMA_HASH)
                return rover_refuse(why, "a table of another schema: compile its text again", ROVER_NO_RULE, 0);
        if (head->size != size || rover_layout(head, at) != size)
                return rover_refuse(why, "a table whose size is not what its counts make", ROVER_NO_RULE, 0);

        view->head = head;
        view->rule = (const struct rover_rule address_to)(bytes + at[0]);
        view->order = (const p16 address_to)(bytes + at[1]);
        view->index = (const struct rover_span address_to)(bytes + at[2]);
        view->comparison = (const struct rover_comparison address_to)(bytes + at[3]);
        view->call = (const struct rover_call address_to)(bytes + at[4]);
        view->action = (const struct rover_action address_to)(bytes + at[5]);
        view->argument = (const struct rover_operand address_to)(bytes + at[6]);
        view->note = (const struct rover_note address_to)(bytes + at[7]);
        view->pool = bytes + at[8];

        //      The pool opens with the empty string, which is what an about of 0 is.
        if (head->pool < 4 || view->pool[0] || view->pool[1] || view->pool[2] || view->pool[3])
                return rover_refuse(why, "a pool that does not open with the empty string", ROVER_NO_RULE, 0);

        //      Every rule once in order, and each event's rules a run of it, in id order.
        memory_fill(seen, 0, sizeof(seen));
        for (positive id = 0; id < ROVER_EVENTS; id++)
        {
                struct rover_span span = view->index[id];

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
                }
                total += span.count;
        }
        if (total != head->rules)
                return rover_refuse(why, "an index that leaves rules out", ROVER_NO_RULE, 0);
        for (positive k = head->rules; k < rover_align8(head->rules * 2) / 2; k++)
                if (view->order[k])
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
        for (positive k = 0; k < head->notes; k++)
                if (view->note[k].rule >= head->rules || !rover_string_ok(view, view->note[k].text, ROVER_TEXT_MOST))
                        return rover_refuse(why, "a note outside the table", ROVER_NO_RULE, (p32)k);
        why->why = null;
        return true;
}

#endif /* ROVER_INCLUDED */
