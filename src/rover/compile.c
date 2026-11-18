/*
        Rover's compiler, its printer, the graph and the verb: text to table,
        table to text, and `moonwater rover`. Userspace only; rover.c is the
        half the kernel shares. src/rover/README.md is the language.

        The compiler reads a file of rules one line at a time and builds the
        table's sections in place, with no tree: a line is one statement and
        its words are its arguments, typed by the schema as they are read. It
        refuses the first thing that is not the language, with its line and
        the reason, and every table it makes passes rover_check before it is
        handed back, so a bug here is a refusal and never a bad table.

        The printer is the compiler run backwards. It writes every literal in
        its one spelling and every statement in its one place, so the text it
        prints compiles to the table it read, byte for byte.
*/

#ifndef ROVER_COMPILE_INCLUDED
#define ROVER_COMPILE_INCLUDED

#include "rover.c"

#define ROVER_RULES_MOST 512u
#define ROVER_COMPARISONS_MOST 2048u
#define ROVER_CALLS_MOST 512u
#define ROVER_ACTIONS_MOST 2048u
#define ROVER_OPERANDS_MOST 4096u
#define ROVER_NOTES_MOST 1024u
#define ROVER_POOL_MOST 32768u
#define ROVER_LINE_WORDS 40u
#define ROVER_LINE_MOST 4096u
#define ROVER_SOURCE_MOST 65536u
#define ROVER_WHY_MOST 240u
#define ROVER_SLOT_AFTER 0xffffu
#define ROVER_READS_CLOCK (1ull << 62)
#define ROVER_EVENT_EVERY 102u
#define ROVER_EVENT_AT 103u
#define ROVER_EVERY_LEAST 1000000000ll // a second
#define ROVER_READS_WEEKDAY (1ull << 63)

//      The base types every schema has, at the front of its type rows (test/rover_schema.py puts them there).
#define ROVER_TYPE_INT 0u
#define ROVER_TYPE_PERCENT 1u
#define ROVER_TYPE_DURATION 2u
#define ROVER_TYPE_CLOCK 3u
#define ROVER_TYPE_WEEKDAY 4u
#define ROVER_TYPE_ONOFF 5u
#define ROVER_TYPE_CHORD 6u
#define ROVER_TYPE_WORD 7u
#define ROVER_TYPE_TEXT 8u
#define ROVER_TYPE_WORDS 9u

//      A rule's reads are 64 bits, two of them the clock's: a state's bit is its number modulo the other 62, so two
//      states may share one, which costs a when rule a look at a change it does not read and never misses one.
#define ROVER_READS_BIT(state) (1ull << ((state) % 62))

//      One word of a line: where it is, how long, and whether it was quoted
//      (its bytes then are the decoded copy, in the line's scratch).
struct rover_word
{
        const p8 address_to text;
        p32 length;
        p8 quoted;
        p8 pad[3];
};

struct rover_build
{
        struct rover_rule rule[ROVER_RULES_MOST];
        struct rover_comparison comparison[ROVER_COMPARISONS_MOST];
        struct rover_call call[ROVER_CALLS_MOST];
        struct rover_action action[ROVER_ACTIONS_MOST];
        struct rover_operand argument[ROVER_OPERANDS_MOST];
        struct rover_note note[ROVER_NOTES_MOST];
        struct rover_service service[ROVER_SERVICES_MOST];
        p32 services;
        p8 block;                  // ROVER_BLOCK_*: what the lines at depth 1 belong to
        bool merge;                // many files as one table: a file's services may follow another file's rules
        struct rover_rule service_rule; // a service's if lines, read as a when rule's
        p64 pool_words[ROVER_POOL_MOST / 8];
        p32 rules, comparisons, calls, actions, arguments, notes, pool;

        //      The rule being read.
        p8 stage;     // ROVER_STAGE_*
        p8 header;    // the last header slot said, so the header keeps its order
        p8 want;      // place kernel (1) or place user (2), as said
        p8 pad;
        p16 slot;     // the next body statement's slot
        const struct rover_event_row address_to event;

        //      Notes read and not yet given to a statement.
        p32 pending[64];
        p32 pendings;

        //      The line being read.
        struct rover_word word[ROVER_LINE_WORDS];
        p32 words;
        p8 scratch[ROVER_LINE_MOST];
        p32 scratch_used;
        p32 line;

        char why[ROVER_WHY_MOST];
        p32 why_used;

        p64 table_words[ROVER_TABLE_MOST / 8];
        positive size;
};

#define ROVER_BLOCK_NONE 0u
#define ROVER_BLOCK_RULE 1u
#define ROVER_BLOCK_SERVICE 2u

//      A service's policy unless it says other: the script's bus, audio and desktop supervisors'.
#define ROVER_SERVICE_WAITS {0, 500, 2000}
#define ROVER_SERVICE_LIVES 4u
#define ROVER_SERVICE_SHORT_MS 30000u

#define ROVER_STAGE_HEADER 0u
#define ROVER_STAGE_IF 1u
#define ROVER_STAGE_THEN 2u
#define ROVER_STAGE_ELSE 3u

//      A canonical printing, into a caller's room; past it, full is set and the rest is dropped.
struct rover_text
{
        p8 address_to bytes;
        p32 used;
        p32 room;
        bool full;
};

static fn rover_put(struct rover_text address_to text, const p8 address_to bytes, positive length)
{
        if (text->full || length > text->room - text->used)
        {
                text->full = true;
                return;
        }
        memory_copy(text->bytes + text->used, bytes, length);
        text->used += (p32)length;
}

static fn rover_puts(struct rover_text address_to text, const char address_to bytes)
{
        rover_put(text, (const p8 address_to)bytes, string_length((string_address)bytes));
}

static fn rover_put_number(struct rover_text address_to text, b64 value)
{
        p8 digits[24];

        rover_put(text, digits, bipolar_into(digits, value));
}

// ---- words and refusals ----------------------------------------------------

//      memory_first_of over bytes this file only reads.
static const p8 address_to rover_find(const p8 address_to bytes, p8 value, positive length)
{
        return (const p8 address_to)memory_first_of((address_any)bytes, value, length);
}

static bool rover_is(const struct rover_word address_to word, const char address_to text)
{
        positive length = string_length((string_address)text);

        return word->length == length && !memory_compare(word->text, text, length);
}

static bool rover_bytes_are(const p8 address_to bytes, positive length, const char address_to text)
{
        return string_length((string_address)text) == length && !memory_compare(bytes, text, length);
}

static fn rover_why_bytes(struct rover_build address_to build, const p8 address_to bytes, positive length)
{
        if (length > ROVER_WHY_MOST - 1 - build->why_used)
                length = ROVER_WHY_MOST - 1 - build->why_used;
        memory_copy(build->why + build->why_used, bytes, length);
        build->why_used += (p32)length;
        build->why[build->why_used] = 0;
}

static fn rover_why_add(struct rover_build address_to build, const char address_to text)
{
        rover_why_bytes(build, (const p8 address_to)text, string_length((string_address)text));
}

//      Refuse: the reason is the pieces given, a word of the line between them.
static bool rover_refused(struct rover_build address_to build, const char address_to first,
                          const struct rover_word address_to word, const char address_to then)
{
        if (build->why_used)
                return false;
        rover_why_add(build, first);
        if (word)
                rover_why_bytes(build, word->text, word->length);
        if (then)
                rover_why_add(build, then);
        return false;
}

//      The schema's name nearest a word that is none of them: the fewest
//      bytes changed, two at the most, so a typo is named and a stranger is not.
static p32 rover_distance(const p8 address_to a, p32 la, const char address_to b)
{
        p32 lb = (p32)string_length((string_address)b), row[ROVER_NAME_MOST + 1];

        if (la > ROVER_NAME_MOST || lb > ROVER_NAME_MOST)
                return 99;
        for (p32 j = 0; j <= lb; j++)
                row[j] = j;
        for (p32 i = 1; i <= la; i++)
        {
                p32 diagonal = row[0], next;

                row[0] = i;
                for (p32 j = 1; j <= lb; j++)
                {
                        next = row[j];
                        p32 best = diagonal + (a[i - 1] != (p8)b[j - 1]);
                        if (row[j] + 1 < best)
                                best = row[j] + 1;
                        if (row[j - 1] + 1 < best)
                                best = row[j - 1] + 1;
                        row[j] = best;
                        diagonal = next;
                }
        }
        return row[lb];
}

static fn rover_suggest(struct rover_build address_to build, const struct rover_word address_to word,
                        const char address_to near)
{
        if (near && rover_distance(word->text, word->length, near) <= 2)
        {
                rover_why_add(build, " (");
                rover_why_add(build, near);
                rover_why_add(build, "?)");
        }
}

static const char address_to rover_nearest_state(const struct rover_word address_to word)
{
        const char address_to best = null;
        p32 least = 3;

        for (positive at = 0; at < ROVER_STATES; at++)
        {
                p32 distance = rover_distance(word->text, word->length, rover_states[at].name);

                if (distance < least)
                        least = distance, best = rover_states[at].name;
        }
        return best;
}

// ---- the pool --------------------------------------------------------------

static p8 address_to rover_pool(struct rover_build address_to build)
{
        return (p8 address_to)build->pool_words;
}

//      A string record: its offset, or 0 when the pool is full (0 is the empty
//      string, which no caller asks for by this road).
static p32 rover_pool_string(struct rover_build address_to build, const p8 address_to bytes, positive length)
{
        p32 at = (build->pool + 3) & ~3u;
        p16 size = (p16)length;

        if (!length)
                return 0;
        if (at + 2 + length + 1 > ROVER_POOL_MOST)
                return rover_refused(build, "too many words and notes for one table", null, null), 0;
        //      The alignment's bytes are zero, so a text makes the same table whatever was compiled before it.
        memory_zero(rover_pool(build) + build->pool, at - build->pool);
        memory_copy(rover_pool(build) + at, &size, 2);
        memory_copy(rover_pool(build) + at + 2, bytes, length);
        rover_pool(build)[at + 2 + length] = 0;
        build->pool = (p32)(at + 2 + length + 1);
        return at;
}

static p32 rover_pool_number(struct rover_build address_to build, b64 value)
{
        p32 at = (build->pool + 7) & ~7u;

        if (at + 8 > ROVER_POOL_MOST)
                return rover_refused(build, "too many constants for one table", null, null), 0;
        memory_zero(rover_pool(build) + build->pool, at - build->pool);
        memory_copy(rover_pool(build) + at, &value, 8);
        build->pool = at + 8;
        return at;
}

// ---- literals --------------------------------------------------------------

static const struct
{
        const char address_to name;
        p16 code;
} rover_keys[] = {
    {"escape", 1}, {"1", 2}, {"2", 3}, {"3", 4}, {"4", 5}, {"5", 6}, {"6", 7}, {"7", 8}, {"8", 9}, {"9", 10},
    {"0", 11}, {"backspace", 14}, {"tab", 15}, {"q", 16}, {"w", 17}, {"e", 18}, {"r", 19}, {"t", 20}, {"y", 21},
    {"u", 22}, {"i", 23}, {"o", 24}, {"p", 25}, {"enter", 28}, {"a", 30}, {"s", 31}, {"d", 32}, {"f", 33},
    {"g", 34}, {"h", 35}, {"j", 36}, {"k", 37}, {"l", 38}, {"z", 44}, {"x", 45}, {"c", 46}, {"v", 47}, {"b", 48},
    {"n", 49}, {"m", 50}, {"space", 57}, {"f1", 59}, {"f2", 60}, {"f3", 61}, {"f4", 62}, {"f5", 63}, {"f6", 64},
    {"f7", 65}, {"f8", 66}, {"f9", 67}, {"f10", 68}, {"f11", 87}, {"f12", 88}, {"print", 99}, {"home", 102},
    {"up", 103}, {"pageup", 104}, {"left", 105}, {"right", 106}, {"end", 107}, {"down", 108}, {"pagedown", 109},
    {"insert", 110}, {"delete", 111},
    //  Every other key a keyboard has, by Linux's name for it without KEY_: the kernel's key down and key up
    //  hear each, the modifiers themselves among them.
    {"minus", 12}, {"equal", 13}, {"leftbrace", 26}, {"rightbrace", 27}, {"leftctrl", 29}, {"semicolon", 39},
    {"apostrophe", 40}, {"grave", 41}, {"leftshift", 42}, {"backslash", 43}, {"comma", 51}, {"dot", 52},
    {"slash", 53}, {"rightshift", 54}, {"kpasterisk", 55}, {"leftalt", 56}, {"capslock", 58}, {"numlock", 69},
    {"scrolllock", 70}, {"kp7", 71}, {"kp8", 72}, {"kp9", 73}, {"kpminus", 74}, {"kp4", 75}, {"kp5", 76},
    {"kp6", 77}, {"kpplus", 78}, {"kp1", 79}, {"kp2", 80}, {"kp3", 81}, {"kp0", 82}, {"kpdot", 83},
    {"kpenter", 96}, {"rightctrl", 97}, {"kpslash", 98}, {"rightalt", 100}, {"mute", 113}, {"volumedown", 114},
    {"volumeup", 115}, {"power", 116}, {"kpequal", 117}, {"pause", 119}, {"leftmeta", 125}, {"rightmeta", 126},
    {"compose", 127}, {"stop", 128}, {"menu", 139}, {"calc", 140}, {"sleep", 142}, {"wakeup", 143}, {"www", 150},
    {"mail", 155}, {"back", 158}, {"forward", 159}, {"eject", 161}, {"nextsong", 163}, {"playpause", 164},
    {"previoussong", 165}, {"stopcd", 166}, {"homepage", 172}, {"f13", 183}, {"f14", 184}, {"f15", 185},
    {"f16", 186}, {"f17", 187}, {"f18", 188}, {"f19", 189}, {"f20", 190}, {"f21", 191}, {"f22", 192},
    {"f23", 193}, {"f24", 194}, {"camera", 212}, {"search", 217}, {"brightnessdown", 224},
    {"brightnessup", 225}, {"bluetooth", 237}, {"wlan", 238}, {"rfkill", 247}, {"micmute", 248},
};

static const struct
{
        const char address_to name;
        p8 bit;
} rover_modifiers[] = {
    {"ctrl", ROVER_CHORD_CTRL}, {"alt", ROVER_CHORD_ALT}, {"shift", ROVER_CHORD_SHIFT}, {"super", ROVER_CHORD_SUPER},
};

/*
        A run of digits as a number, refused the moment the next digit would
        take it past what a b64 holds: a literal never wraps into a value it
        does not say (922337203685477580880 was 80). The compiler's literals
        and the settings files the runner reads share it.
*/
#define ROVER_DIGITS_MOST 9223372036854775807ll
static bool rover_digits(const p8 address_to text, positive length, positive address_to at, b64 address_to into,
                         positive address_to count)
{
        b64 value = 0;
        positive start = *at;

        while (*at < length && text[*at] >= '0' && text[*at] <= '9')
        {
                b64 digit = text[*at] - '0';

                if (value > (ROVER_DIGITS_MOST - digit) / 10)
                        return false;
                value = value * 10 + digit;
                (*at)++;
        }
        *into = value;
        *count = *at - start;
        return *at > start;
}

//      The index of a word in a comma list, or -1.
static b32 rover_choice(const char address_to choices, const p8 address_to word, positive length)
{
        b32 index = 0;
        const char address_to at = choices;

        while (*at)
        {
                const char address_to stop = at;

                while (*stop && *stop != ',')
                        stop++;
                if ((positive)(stop - at) == length && !memory_compare(at, word, length))
                        return index;
                index++;
                at = *stop ? stop + 1 : stop;
        }
        return -1;
}

static fn rover_choice_put(struct rover_text address_to text, const char address_to choices, b64 index);

//      An enum's word, into the reason being said.
static fn rover_choice_put_why(struct rover_build address_to build, const char address_to choices, b64 index)
{
        p8 bytes[64];
        struct rover_text text = {bytes, 0, sizeof(bytes), false};

        rover_choice_put(&text, choices, index);
        rover_why_bytes(build, bytes, text.used);
}

static fn rover_choice_put(struct rover_text address_to text, const char address_to choices, b64 index)
{
        const char address_to at = choices;

        while (index-- > 0 && *at)
        {
                while (*at && *at != ',')
                        at++;
                if (*at)
                        at++;
        }
        const char address_to stop = at;
        while (*stop && *stop != ',')
                stop++;
        rover_put(text, (const p8 address_to)at, (positive)(stop - at));
}

static const b64 rover_units[6] = {1, 1000, 1000000, 1000000000, 60000000000ll, 3600000000000ll};
static const char address_to const rover_unit_names[6] = {"ns", "us", "ms", "s", "m", "h"};

/*
        A literal, read as the type it must be. Answers false with the reason
        said when it is not one; on success the value is a number, or the
        word's own bytes (a word and a text are pooled by the caller).
*/
static bool rover_literal(struct rover_build address_to build, const struct rover_word address_to word, p8 type,
                          b64 address_to into)
{
        const struct rover_type_row address_to row = &rover_types[type];
        const p8 address_to text = word->text;
        positive length = word->length, at = 0, count;
        b64 whole, part;

        switch (row->kind)
        {
        case ROVER_T_INT:
        {
                bool negative = length && text[0] == '-';

                at = negative;
                if (word->quoted || !rover_digits(text, length, &at, &whole, &count) || at != length)
                        return rover_refused(build, "", word, " is not a number");
                *into = negative ? -whole : whole;
                if (*into < row->low || *into > row->high)
                        return rover_refused(build, "", word, " is outside the range it may have");
                return true;
        }
        case ROVER_T_PERCENT:
                if (word->quoted || !rover_digits(text, length, &at, &whole, &count))
                        return rover_refused(build, "", word, " is not a percent, as 30% or 12.5% is");
                part = 0;
                if (at < length && text[at] == '.')
                {
                        positive from = ++at;

                        if (!rover_digits(text, length, &at, &part, &count) || at - from > 2)
                                return rover_refused(build, "", word, " has more than two decimals");
                        if (at - from == 1)
                                part *= 10;
                }
                if (at + 1 != length || text[at] != '%' || whole > 100 || (whole == 100 && part))
                        return rover_refused(build, "", word, " is not a percent from 0% to 100%");
                *into = whole * 100 + part;
                return true;
        case ROVER_T_DURATION:
        {
                b64 scale = 1;
                positive from;

                if (word->quoted || !rover_digits(text, length, &at, &whole, &count))
                        return rover_refused(build, "", word, " is not a duration, as 500ms or 2s is");
                part = 0;
                from = at;
                if (at < length && text[at] == '.')
                {
                        from = ++at;
                        if (!rover_digits(text, length, &at, &part, &count) || at - from > 9)
                                return rover_refused(build, "", word, " has more decimals than a nanosecond");
                        for (positive i = from; i < at; i++)
                                scale *= 10;
                }
                for (positive unit = 0; unit < 6; unit++)
                        if (rover_bytes_are(text + at, length - at, rover_unit_names[unit]))
                        {
                                b64 one = rover_units[unit];

                                //      Bounded before each multiply: an hour's count of a day's worth wrapped (5124096h
                                //      was 25 minutes), and so did a fraction of one.
                                if (whole > ROVER_DURATION_MOST / one)
                                        return rover_refused(build, "", word, " is longer than a day");
                                if (one % scale ? (part * one) % scale : 0)
                                        return rover_refused(build, "", word, " is finer than a nanosecond");
                                *into = whole * one + (one % scale ? part * one / scale : part * (one / scale));
                                if (*into > ROVER_DURATION_MOST)
                                        return rover_refused(build, "", word, " is longer than a day");
                                return true;
                        }
                return rover_refused(build, "", word, " is not a duration: its unit is ns, us, ms, s, m or h");
        }
        case ROVER_T_CLOCK:
                if (word->quoted || !rover_digits(text, length, &at, &whole, &count) || count > 2 || at >= length ||
                    text[at] != ':')
                        return rover_refused(build, "", word, " is not a clock time, as 06:00 or 22:30 is");
                at++;
                if (!rover_digits(text, length, &at, &part, &count) || count != 2 || at != length || whole > 23 ||
                    part > 59)
                        return rover_refused(build, "", word, " is not a clock time from 00:00 to 23:59");
                *into = whole * 60 + part;
                return true;
        case ROVER_T_WEEKDAY:
        case ROVER_T_ENUM:
        case ROVER_T_ONOFF:
        {
                b32 index = rover_choice(row->choices, text, length);

                if (index < 0)
                {
                        rover_refused(build, "", word, " is not one of ");
                        rover_why_add(build, row->choices);
                        return false;
                }
                *into = index;
                return true;
        }
        case ROVER_T_CHORD:
        {
                b64 modifiers = 0;
                positive start = 0;

                for (at = 0; at <= length; at++)
                {
                        if (at < length && text[at] != '+')
                                continue;
                        if (at == length)
                        {
                                for (positive k = 0; k < array_count(rover_keys); k++)
                                        if (rover_bytes_are(text + start, at - start, rover_keys[k].name))
                                        {
                                                *into = modifiers << 16 | rover_keys[k].code;
                                                return true;
                                        }
                                return rover_refused(build, "", word, " ends in no key a chord can name");
                        }
                        positive k;
                        for (k = 0; k < array_count(rover_modifiers); k++)
                                if (rover_bytes_are(text + start, at - start, rover_modifiers[k].name))
                                        break;
                        if (k == array_count(rover_modifiers))
                                return rover_refused(build, "", word, " has a modifier that is not ctrl, alt, shift or super");
                        modifiers |= rover_modifiers[k].bit;
                        start = at + 1;
                }
                return false;
        }
        case ROVER_T_WORD:
        case ROVER_T_WORDS:
        case ROVER_T_TEXT:
                if (length > (row->kind == ROVER_T_TEXT ? ROVER_TEXT_MOST : ROVER_WORD_MOST))
                        return rover_refused(build, "", word, row->kind == ROVER_T_WORD ? " is longer than a word's 62 bytes"
                                                                                       : " is longer than 255 bytes");
                if (rover_find(text, 0, length))
                        return rover_refused(build, "", word, " holds a NUL");
                *into = 0;
                return true;
        }
        return false;
}

//      A word that prints bare reads back as the same word; any other is quoted.
static bool rover_bare(const p8 address_to text, positive length)
{
        if (!length || rover_bytes_are(text, length, "and") || rover_bytes_are(text, length, "or") ||
            rover_bytes_are(text, length, "between") || text[0] == '#' || text[0] == '$')
                return false;
        for (positive at = 0; at < length; at++)
        {
                p8 c = text[at];

                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                      c == '.' || c == ':' || c == '@' || c == '/' || c == '+' || c == '=' || c == '*' || c == '?' ||
                      c == '-' || c == '%'))
                        return false;
        }
        return true;
}

static fn rover_put_quoted(struct rover_text address_to text, const p8 address_to bytes, positive length)
{
        rover_puts(text, "\"");
        for (positive at = 0; at < length; at++)
        {
                if (bytes[at] == '"' || bytes[at] == '\\')
                        rover_puts(text, "\\");
                rover_put(text, bytes + at, 1);
        }
        rover_puts(text, "\"");
}

static fn rover_literal_put(struct rover_text address_to text, p8 type, b64 value, const p8 address_to bytes,
                            positive length)
{
        const struct rover_type_row address_to row = &rover_types[type];

        switch (row->kind)
        {
        case ROVER_T_INT:
                rover_put_number(text, value);
                return;
        case ROVER_T_PERCENT:
                rover_put_number(text, value / 100);
                if (value % 100)
                {
                        p8 decimals[3] = {'.', (p8)('0' + value % 100 / 10), (p8)('0' + value % 10)};

                        rover_put(text, decimals, value % 10 ? 3 : 2);
                }
                rover_puts(text, "%");
                return;
        case ROVER_T_DURATION:
        {
                positive unit = 5;

                while (unit && value % rover_units[unit])
                        unit--;
                if (!value)
                        unit = 3;
                rover_put_number(text, value / rover_units[unit]);
                rover_puts(text, rover_unit_names[unit]);
                return;
        }
        case ROVER_T_CLOCK:
        {
                p8 clock[5] = {(p8)('0' + value / 600), (p8)('0' + value / 60 % 10), ':', (p8)('0' + value % 60 / 10),
                               (p8)('0' + value % 10)};

                rover_put(text, clock, 5);
                return;
        }
        case ROVER_T_WEEKDAY:
        case ROVER_T_ENUM:
        case ROVER_T_ONOFF:
                rover_choice_put(text, row->choices, value);
                return;
        case ROVER_T_CHORD:
                for (positive k = 0; k < array_count(rover_modifiers); k++)
                        if ((value >> 16) & rover_modifiers[k].bit)
                        {
                                rover_puts(text, rover_modifiers[k].name);
                                rover_puts(text, "+");
                        }
                for (positive k = 0; k < array_count(rover_keys); k++)
                        if (rover_keys[k].code == (value & 0xffff))
                                rover_puts(text, rover_keys[k].name);
                return;
        case ROVER_T_WORD:
        case ROVER_T_WORDS:
                if (rover_bare(bytes, length))
                        rover_put(text, bytes, length);
                else
                        rover_put_quoted(text, bytes, length);
                return;
        case ROVER_T_TEXT:
                rover_put_quoted(text, bytes, length);
                return;
        }
}

// ---- operands --------------------------------------------------------------

//      An operand as it is read: its source and its type, or a literal whose
//      type the other side of its comparison will say.
struct rover_side
{
        struct rover_operand operand;
        p8 type; // the type row
        bool literal;
        const struct rover_word address_to word;
};

static const struct rover_state_row address_to rover_state_named(const p8 address_to text, positive length)
{
        for (positive at = 0; at < ROVER_STATES; at++)
                if (rover_bytes_are(text, length, rover_states[at].name))
                        return &rover_states[at];
        return null;
}

static bool rover_constant(struct rover_build address_to build, const struct rover_word address_to word, p8 type,
                           struct rover_operand address_to into)
{
        b64 value;

        if (!rover_literal(build, word, type, &value))
                return false;
        if (rover_kind_is_word(rover_types[type].kind))
        {
                p32 at = rover_pool_string(build, word->text, word->length);

                if (build->why_used)
                        return false;
                into->source = ROVER_CONST_WORD;
                into->page = type;
                into->index = (p16)at;
                return true;
        }
        into->source = ROVER_CONST_NUMBER;
        into->page = type;
        into->index = (p16)rover_pool_number(build, value);
        return !build->why_used;
}

static bool rover_side_resolve(struct rover_build address_to build, struct rover_side address_to side, p8 type)
{
        if (!side->literal)
                return true;
        side->type = type;
        side->literal = false;
        return rover_constant(build, side->word, type, &side->operand);
}

static bool rover_read_side(struct rover_build address_to build, const struct rover_word address_to word,
                            struct rover_rule address_to rule, bool in_call, struct rover_side address_to side);

//      NAME(ARG, ...): the routine, its count, and each argument the type its
//      parameter takes; a word or a buffer takes a word, an integer an int.
static bool rover_read_call(struct rover_build address_to build, const struct rover_word address_to word,
                            struct rover_rule address_to rule, struct rover_side address_to side)
{
        const p8 address_to open = rover_find(word->text, '(', word->length);
        positive name = (positive)(open - word->text), at, function;
        struct rover_call made;
        struct rover_word parts[ROVER_ARGUMENTS + 1];
        positive count = 0;

        memory_zero(&made, sizeof(made));
        if (word->text[word->length - 1] != ')')
                return rover_refused(build, "", word, " opens a call it does not close");
        for (function = 0; function < ROVER_FUNCTIONS; function++)
                if (rover_bytes_are(word->text, name, rover_functions[function].name))
                        break;
        if (function == ROVER_FUNCTIONS)
        {
                struct rover_word head = {word->text, (p32)name, 0, {0}};

                return rover_refused(build, "", &head, " is no routine rover knows (moonwater rover schema lists them)");
        }
        //      The arguments: split at commas outside quotes, each trimmed.
        at = name + 1;
        if (at < word->length - 1)
                for (;;)
                {
                        positive start = at;
                        bool quoted = false;

                        while (at < word->length - 1 && (quoted || word->text[at] != ','))
                        {
                                if (word->text[at] == '"')
                                        quoted = !quoted;
                                at++;
                        }
                        positive stop = at;
                        while (start < stop && word->text[start] == ' ')
                                start++;
                        while (stop > start && word->text[stop - 1] == ' ')
                                stop--;
                        if (count == ROVER_ARGUMENTS)
                                return rover_refused(build, "", word, " has more arguments than any routine takes");
                        parts[count].text = word->text + start;
                        parts[count].length = (p32)(stop - start);
                        parts[count].quoted = 0;
                        //      A quoted argument is its bytes without the quotes; it holds no escapes.
                        if (parts[count].length >= 2 && parts[count].text[0] == '"' &&
                            parts[count].text[parts[count].length - 1] == '"')
                        {
                                parts[count].text++;
                                parts[count].length -= 2;
                                parts[count].quoted = 1;
                        }
                        count++;
                        if (at >= word->length - 1)
                                break;
                        at++;
                }
        if (count != rover_functions[function].count)
        {
                rover_refused(build, "", word, " has another count of arguments than ");
                rover_why_add(build, rover_functions[function].name);
                rover_why_add(build, " takes");
                return false;
        }
        if (build->calls == ROVER_CALLS_MOST)
                return rover_refused(build, "too many calls for one table", null, null);
        made.function = (p16)function;
        made.count = (p8)count;
        for (positive k = 0; k < count; k++)
        {
                struct rover_side argument;
                bool integer = rover_functions[function].signature[k] == 'i';

                if (!rover_read_side(build, &parts[k], rule, true, &argument))
                        return false;
                if (!rover_side_resolve(build, &argument, integer ? ROVER_TYPE_INT : ROVER_TYPE_WORD))
                        return false;
                if (integer == rover_kind_is_word(rover_types[argument.type].kind))
                        return rover_refused(build, "", &parts[k], integer ? " is a word where the routine takes a number"
                                                                           : " is a number where the routine takes a word");
                made.argument[k] = argument.operand;
        }
        build->call[build->calls] = made;
        side->operand.source = ROVER_CALL;
        side->operand.page = 0;
        side->operand.index = (p16)build->calls++;
        side->type = ROVER_TYPE_INT;
        return true;
}

/*
        One operand of a condition or an action: a field, a call, a state,
        the clock, or a literal left for the other side to type.
*/
static bool rover_read_side(struct rover_build address_to build, const struct rover_word address_to word,
                            struct rover_rule address_to rule, bool in_call, struct rover_side address_to side)
{
        const struct rover_state_row address_to state;

        memory_zero(side, sizeof(*side));
        side->word = word;
        if (!word->quoted && word->length > 1 && word->text[0] == '$')
        {
                if (!build->event)
                        return rover_refused(build, "", word, ": a when rule reads states, and an event's fields are no states");
                for (positive at = 0; at < build->event->fields; at++)
                        if (rover_bytes_are(word->text + 1, word->length - 1, build->event->field_name[at]))
                        {
                                side->operand.source = ROVER_FIELD;
                                side->operand.index = (p16)at;
                                side->type = build->event->field_type[at];
                                return true;
                        }
                rover_refused(build, "", word, " is no field of ");
                rover_why_add(build, build->event->name);
                return false;
        }
        if (!word->quoted && rover_find(word->text, '(', word->length))
        {
                if (in_call)
                        return rover_refused(build, "", word, ": a call's argument is never another call");
                return rover_read_call(build, word, rule, side);
        }
        if (!word->quoted && (state = rover_state_named(word->text, word->length)))
        {
                bool text = rover_kind_is_word(rover_types[state->type].kind);

                side->operand.source = text ? ROVER_STATE_WORD : ROVER_STATE_NUMBER;
                side->operand.page = state->page;
                side->operand.index = state->offset;
                side->type = state->type;
                rule->reads |= ROVER_READS_BIT((positive)(state - rover_states));
                return true;
        }
        if (!word->quoted && (rover_is(word, "clock") || rover_is(word, "weekday")))
        {
                bool clock = rover_is(word, "clock");

                side->operand.source = clock ? ROVER_CLOCK : ROVER_WEEKDAY;
                side->type = clock ? ROVER_TYPE_CLOCK : ROVER_TYPE_WEEKDAY;
                rule->reads |= clock ? ROVER_READS_CLOCK : ROVER_READS_WEEKDAY;
                return true;
        }
        side->literal = true;
        return true;
}

// ---- conditions ------------------------------------------------------------

static const struct
{
        const char address_to word;
        p8 op;
} rover_operators[] = {
    {"==", ROVER_EQ}, {"!=", ROVER_NE}, {"<", ROVER_LT}, {"<=", ROVER_LE}, {">", ROVER_GT},
    {">=", ROVER_GE}, {"~", ROVER_MATCH}, {"!~", ROVER_NOMATCH}, {"between", ROVER_BETWEEN},
};

static b32 rover_operator(const struct rover_word address_to word)
{
        if (word->quoted)
                return -1;
        for (positive at = 0; at < array_count(rover_operators); at++)
                if (rover_is(word, rover_operators[at].word))
                        return rover_operators[at].op;
        return -1;
}

static const char address_to rover_operator_word(p8 op)
{
        for (positive at = 0; at < array_count(rover_operators); at++)
                if (rover_operators[at].op == op)
                        return rover_operators[at].word;
        return "";
}

//      Whether a type's kind has an operator: order for the numbers, equality
//      for all, matching for words, and a bare truth for int, onoff and words.
static bool rover_kind_has(p8 kind, p8 op)
{
        switch (op)
        {
        case ROVER_TRUE: return kind == ROVER_T_INT || kind == ROVER_T_ONOFF || kind == ROVER_T_WORD;
        case ROVER_EQ:
        case ROVER_NE: return kind != ROVER_T_TEXT;
        case ROVER_MATCH:
        case ROVER_NOMATCH: return kind == ROVER_T_WORD;
        }
        return kind == ROVER_T_INT || kind == ROVER_T_PERCENT || kind == ROVER_T_DURATION || kind == ROVER_T_CLOCK ||
               kind == ROVER_T_WEEKDAY;
}

static bool rover_add_comparison(struct rover_build address_to build, const struct rover_comparison address_to made)
{
        if (build->comparisons == ROVER_COMPARISONS_MOST)
                return rover_refused(build, "too many comparisons for one table", null, null);
        build->comparison[build->comparisons] = *made;
        build->comparison[build->comparisons].form = rover_form_of(made);
        build->comparisons++;
        return true;
}

/*
        A condition: words from first to the line's end, comparisons joined by
        and and or. Each comparison is one, two or three operands around its
        operator; a literal takes the type of the typed side, and a pattern
        is a word on the right of ~ or !~.
*/
static bool rover_read_condition(struct rover_build address_to build, struct rover_rule address_to rule, positive first)
{
        positive at = first;

        if (at >= build->words)
                return rover_refused(build, "a condition with nothing in it", null, null);
        while (at < build->words)
        {
                struct rover_comparison made;
                struct rover_side side[3];
                positive sides = 1;
                b32 op = ROVER_TRUE;

                memory_zero(&made, sizeof(made));
                if (rover_operator(&build->word[at]) >= 0)
                        return rover_refused(build, "", &build->word[at], " has nothing on its left");
                if (!rover_read_side(build, &build->word[at], rule, false, &side[0]))
                        return false;
                at++;
                if (at < build->words && (op = rover_operator(&build->word[at])) >= 0)
                {
                        sides = op == ROVER_BETWEEN ? 3 : 2;
                        if (at + sides > build->words)
                                return rover_refused(build, "", &build->word[at], " has nothing on its right");
                        for (positive k = 1; k < sides; k++)
                                if (!rover_read_side(build, &build->word[at + k], rule, false, &side[k]))
                                        return false;
                        at += sides;
                }
                else
                        op = ROVER_TRUE;
                //      The type: the first typed side's; then every side must be of it.
                p8 type = 0;
                bool typed = false;
                for (positive k = 0; k < sides; k++)
                        if (!side[k].literal)
                        {
                                type = side[k].type;
                                typed = true;
                                break;
                        }
                if (!typed)
                {
                        //      A word with a dot in it was meant as a state: say which one it nearly is.
                        if (!side[0].word->quoted && rover_find(side[0].word->text, '.', side[0].word->length))
                        {
                                rover_refused(build, "", side[0].word, " is no state");
                                rover_suggest(build, side[0].word, rover_nearest_state(side[0].word));
                                return false;
                        }
                        return rover_refused(build, "", side[0].word,
                                             ": a comparison needs a state, a field or a call on one side");
                }
                for (positive k = 0; k < sides; k++)
                {
                        if (!rover_side_resolve(build, &side[k], type))
                                return false;
                        p8 kind = rover_types[side[k].type].kind;
                        if (kind != rover_types[type].kind ||
                            ((kind == ROVER_T_ENUM) && side[k].type != type))
                                return rover_refused(build, "", side[k].word, " is not of its comparison's type");
                }
                if (!rover_kind_has(rover_types[type].kind, (p8)op))
                {
                        if (op == ROVER_TRUE)
                                return rover_refused(build, "", side[0].word,
                                                     " is no truth by itself: compare it with == or another operator");
                        rover_refused(build, "", side[0].word, " cannot be compared with ");
                        rover_why_add(build, rover_operator_word((p8)op));
                        return false;
                }
                made.op = (p8)op;
                made.type = rover_types[type].kind;
                made.a = side[0].operand;
                if (sides > 1)
                        made.b = side[1].operand;
                if (sides > 2)
                        made.c = side[2].operand;
                if (at == build->words)
                        made.join = ROVER_JOIN_LINE;
                else if (rover_is(&build->word[at], "or") && !build->word[at].quoted)
                        made.join = ROVER_JOIN_OR, at++;
                else if (rover_is(&build->word[at], "and") && !build->word[at].quoted)
                        at++;
                else
                        return rover_refused(build, "", &build->word[at], ": and or or goes between two comparisons");
                if (made.join != ROVER_JOIN_LINE && at == build->words)
                        return rover_refused(build, "", &build->word[at - 1], " ends the line with nothing after it");
                if (!rover_add_comparison(build, &made))
                        return false;
                rule->comparisons++;
        }
        return true;
}

// ---- statements ------------------------------------------------------------

static bool rover_note_flush(struct rover_build address_to build, p16 slot)
{
        for (positive at = 0; at < build->pendings; at++)
        {
                if (build->notes == ROVER_NOTES_MOST)
                        return rover_refused(build, "too many notes for one table", null, null);
                build->note[build->notes].rule =
                    build->block == ROVER_BLOCK_SERVICE ? (p16)((build->services - 1) | ROVER_NOTE_SERVICE)
                                                        : (p16)(build->rules - 1);
                build->note[build->notes].slot = slot;
                build->note[build->notes].text = build->pending[at];
                build->notes++;
        }
        build->pendings = 0;
        return true;
}

static const struct rover_event_row address_to rover_event_named(struct rover_build address_to build, positive first,
                                                                  positive address_to used)
{
        //      The longest name the words start with: lid close before lid.
        for (positive take = 2; take >= 1; take--)
        {
                if (first + take > build->words)
                        continue;
                for (positive at = 0; at < ROVER_EVENT_ROWS; at++)
                {
                        const char address_to name = rover_event_rows[at].name;
                        positive length = string_length((string_address)name), offset = 0;
                        bool same = true;

                        for (positive k = 0; k < take && same; k++)
                        {
                                const struct rover_word address_to word = &build->word[first + k];

                                if (k)
                                        same = offset < length && name[offset++] == ' ';
                                same = same && !word->quoted && offset + word->length <= length &&
                                       !memory_compare(name + offset, word->text, word->length);
                                offset += word->length;
                        }
                        if (same && offset == length)
                        {
                                *used = take;
                                return &rover_event_rows[at];
                        }
                }
        }
        return null;
}

static const struct rover_primitive_row address_to rover_primitive_named(struct rover_build address_to build,
                                                                          positive first, positive address_to used)
{
        for (positive take = 2; take >= 1; take--)
        {
                if (first + take > build->words)
                        continue;
                for (positive at = 0; at < ROVER_PRIMITIVES; at++)
                {
                        const char address_to name = rover_primitives[at].name;
                        positive length = string_length((string_address)name);
                        const struct rover_word address_to one = &build->word[first];

                        if (take == 1 ? rover_bytes_are(one->text, one->length, name)
                                      : length == one->length + 1 + build->word[first + 1].length &&
                                            !memory_compare(name, one->text, one->length) && name[one->length] == ' ' &&
                                            !memory_compare(name + one->length + 1, build->word[first + 1].text,
                                                            build->word[first + 1].length))
                        {
                                *used = take;
                                return &rover_primitives[at];
                        }
                }
        }
        return null;
}

static bool rover_glob_word(const struct rover_word address_to word)
{
        return rover_find(word->text, '*', word->length) || rover_find(word->text, '?', word->length);
}

static bool rover_read_on(struct rover_build address_to build, struct rover_rule address_to rule)
{
        positive used = 0;
        const struct rover_event_row address_to event = rover_event_named(build, 1, &used);

        if (!event)
        {
                if (build->words < 2)
                        return rover_refused(build, "on names no event", null, null);
                return rover_refused(build, "", &build->word[1], " is no event (moonwater rover schema lists them)");
        }
        build->event = event;
        rule->event = event->id;
        if (build->words - 1 - used > event->fields)
        {
                rover_refused(build, "", &build->word[1 + used + event->fields], " is more than ");
                rover_why_add(build, event->name);
                rover_why_add(build, event->fields ? "'s fields" : ", which has no fields");
                return false;
        }
        for (positive at = 1 + used; at < build->words; at++)
        {
                struct rover_comparison made;
                positive field = at - 1 - used;
                p8 type = event->field_type[field];

                memory_zero(&made, sizeof(made));
                made.op = rover_types[type].kind == ROVER_T_WORD && rover_glob_word(&build->word[at]) ? ROVER_MATCH
                                                                                                      : ROVER_EQ;
                made.type = rover_types[type].kind;
                made.join = ROVER_JOIN_LINE;
                made.a.source = ROVER_FIELD;
                made.a.index = (p16)field;
                if (!rover_constant(build, &build->word[at], type, &made.b) || !rover_add_comparison(build, &made))
                        return false;
                rule->comparisons++;
                rule->trigger++;
        }
        return true;
}

//      The arguments of an action from first on: as the primitive's types say,
//      or words for a script, a tool and a cue.
static bool rover_read_arguments(struct rover_build address_to build, struct rover_rule address_to rule,
                                 struct rover_action address_to action, positive first,
                                 const struct rover_primitive_row address_to primitive)
{
        positive count = build->words - first;
        bool rest = primitive && primitive->count &&
                    rover_types[primitive->type[primitive->count - 1]].kind == ROVER_T_WORDS;

        if (count > ROVER_ARGUMENTS)
                return rover_refused(build, "", &build->word[first + ROVER_ARGUMENTS], " is past an action's four arguments");
        if (primitive && (rest ? count + 1 < primitive->count : count != primitive->count))
        {
                rover_refused(build, primitive->name, null, primitive->count ? " takes " : " takes no argument");
                if (primitive->count)
                        for (positive k = 0; k < primitive->count; k++)
                        {
                                rover_why_add(build, k ? ", " : "");
                                rover_why_add(build, primitive->argument_name[k]);
                        }
                return false;
        }
        if (build->arguments + count > ROVER_OPERANDS_MOST)
                return rover_refused(build, "too many arguments for one table", null, null);
        action->argument = (p16)build->arguments;
        action->count = (p8)count;
        for (positive k = 0; k < count; k++)
        {
                struct rover_side side;
                p8 type = primitive ? primitive->type[k < primitive->count ? k : primitive->count - 1] : ROVER_TYPE_WORD;

                if (!rover_read_side(build, &build->word[first + k], rule, false, &side))
                        return false;
                if (!side.literal && side.operand.source == ROVER_CALL)
                        return rover_refused(build, "", &build->word[first + k], ": a call is a condition's, not an action's");
                if (!rover_side_resolve(build, &side, type))
                        return false;
                if (rover_types[side.type].kind != rover_types[type].kind ||
                    (rover_types[type].kind == ROVER_T_ENUM && side.type != type))
                        return rover_refused(build, "", &build->word[first + k], " is not of the type the action takes");
                build->argument[build->arguments++] = side.operand;
        }
        return true;
}

//      An action counted to its run, then or else: a run's count is a byte, so a run of more than 255 is refused rather than cut.
static bool rover_count_action(struct rover_build address_to build, struct rover_rule address_to rule)
{
        p8 address_to count = build->stage == ROVER_STAGE_ELSE ? &rule->otherwise : &rule->then;

        if (*count == 255)
                return rover_refused(build, build->stage == ROVER_STAGE_ELSE ? "a rule has more than 255 actions after its else"
                                                                             : "a rule has more than 255 actions before its else",
                                     null, null);
        (*count)++;
        return true;
}

static bool rover_read_action(struct rover_build address_to build, struct rover_rule address_to rule)
{
        struct rover_action action;
        const struct rover_primitive_row address_to primitive = null;
        const struct rover_word address_to keyword = &build->word[0];
        positive first = 1;

        memory_zero(&action, sizeof(action));
        if (build->actions == ROVER_ACTIONS_MOST)
                return rover_refused(build, "too many actions for one table", null, null);
        if (build->words < 2)
                return rover_refused(build, "", keyword, " names nothing to do");
        if (rover_is(keyword, "do"))
        {
                positive used = 0;

                primitive = rover_primitive_named(build, 1, &used);
                if (!primitive)
                        return rover_refused(build, "", &build->word[1], " is no primitive (moonwater rover schema lists them)");
                if (primitive->klass == ROVER_DELIBERATE)
                {
                        rover_refused(build, primitive->name, null,
                                      " is deliberate: only a person asks for it, at Settings or a terminal, and never a rule");
                        return false;
                }
                action.kind = ROVER_DO;
                action.id = primitive->id;
                action.runs = primitive->runs;
                first = 1 + used;
        }
        else if (rover_is(keyword, "run"))
        {
                struct rover_word script = build->word[1];

                if (script.quoted || !rover_bare(script.text, script.length) ||
                    rover_find(script.text, '/', script.length))
                        return rover_refused(build, "", &build->word[1],
                                             " is no script's name: a word of a-z, 0-9, . _ and -, with no slash");
                action.kind = ROVER_RUN;
                action.runs = ROVER_RUNNER;
        }
        else if (rover_is(keyword, "tool"))
        {
                positive tool;

                for (tool = 0; tool < ROVER_TOOLS; tool++)
                        if (rover_bytes_are(build->word[1].text, build->word[1].length, rover_tools[tool]))
                                break;
                if (tool == ROVER_TOOLS)
                        return rover_refused(build, "", &build->word[1], " is none of the shell's tools");
                if (rover_tool_class[tool] == ROVER_DELIBERATE)
                        return rover_refused(build, "", &build->word[1],
                                             " is deliberate: it writes, runs another program, sets the machine or reaches "
                                             "the network, and only a person runs it (moonwater rover schema lists the tools "
                                             "a rule may run)");
                action.kind = ROVER_TOOL;
                action.id = (p16)tool;
                action.runs = ROVER_RUNNER;
                first = 2;
        }
        else if (rover_is(keyword, "set"))
        {
                const struct rover_switch_row address_to row = null;
                positive used = 0;
                struct rover_side side;

                for (positive take = 2; take >= 1 && !row; take--)
                        for (positive at = 0; at < ROVER_SWITCHES && !row; at++)
                        {
                                const char address_to name = rover_switches[at].name;
                                positive length = string_length((string_address)name);
                                const struct rover_word address_to one = &build->word[1];

                                if (1 + take > build->words)
                                        continue;
                                if (take == 1 ? rover_bytes_are(one->text, one->length, name)
                                              : length == one->length + 1 + build->word[2].length &&
                                                    !memory_compare(name, one->text, one->length) && name[one->length] == ' ' &&
                                                    !memory_compare(name + one->length + 1, build->word[2].text,
                                                                    build->word[2].length))
                                        row = &rover_switches[at], used = take;
                        }
                if (!row)
                        return rover_refused(build, "", &build->word[1], " is no switch (moonwater rover schema lists them)");
                if (build->words != 2 + used)
                        return rover_refused(build, row->name, null, " takes one value");
                if (!rover_read_side(build, &build->word[1 + used], rule, false, &side) ||
                    !rover_side_resolve(build, &side, row->type))
                        return false;
                if (side.operand.source == ROVER_CALL || rover_types[side.type].kind != rover_types[row->type].kind ||
                    (rover_types[row->type].kind == ROVER_T_ENUM && side.type != row->type))
                        return rover_refused(build, "", &build->word[1 + used], " is not of the type the switch takes");
                if (row->deliberate >= 0)
                {
                        b64 value = side.operand.source == ROVER_CONST_NUMBER
                                        ? memory_load_unaligned(b64, rover_pool(build) + side.operand.index)
                                        : -1;

                        if (side.operand.source != ROVER_CONST_NUMBER || value == row->deliberate)
                        {
                                rover_refused(build, row->name, null, " ");
                                rover_choice_put_why(build, rover_types[row->type].choices, row->deliberate);
                                rover_why_add(build, " is deliberate: only a person sets it, and a rule names its value");
                                return false;
                        }
                }
                if (build->arguments == ROVER_OPERANDS_MOST)
                        return rover_refused(build, "too many arguments for one table", null, null);
                action.kind = ROVER_SET;
                action.id = row->id;
                action.runs = ROVER_RUNNER;
                action.argument = (p16)build->arguments;
                action.count = 1;
                build->argument[build->arguments++] = side.operand;
                build->action[build->actions++] = action;
                return rover_count_action(build, rule);
        }
        else
        {
                if (!rover_is(&build->word[1], "cue") || build->words != 3)
                        return rover_refused(build, "emit posts a cue, as emit cue NAME does", null, null);
                action.kind = ROVER_EMIT;
                action.id = ROVER_CUE;
                action.runs = ROVER_RUNNER;
                first = 2;
        }
        if (!rover_read_arguments(build, rule, &action, first, primitive))
                return false;
        //      What root runs is the rule file's own words: no event's field (an app id, a device's name) and no
        //      state names a program, a script's argument, a tool's operand or a cue (README, Safety).
        if (action.kind == ROVER_RUN || action.kind == ROVER_TOOL || action.kind == ROVER_EMIT ||
            (action.kind == ROVER_DO && action.id == 14))
                for (positive k = 0; k < action.count; k++)
                {
                        p8 source = build->argument[action.argument + k].source;

                        if (source != ROVER_CONST_NUMBER && source != ROVER_CONST_WORD)
                                return rover_refused(build, "", &build->word[first + k],
                                                     " is not the rule's own word: what root runs, its arguments and a cue "
                                                     "are the rule file's words, never an event's field or a state");
                }
        if (action.kind == ROVER_RUN)
        {
                const p8 address_to name = rover_pool(build) + build->argument[action.argument].index + 2;
                p16 length;

                memory_copy(&length, rover_pool(build) + build->argument[action.argument].index, 2);
                for (positive at = 0; at < length; at++)
                        if (!((name[at] >= 'a' && name[at] <= 'z') || (name[at] >= '0' && name[at] <= '9') ||
                              name[at] == '.' || name[at] == '_' || name[at] == '-') ||
                            (length <= 2 && name[0] == '.' && name[length - 1] == '.'))
                                return rover_refused(build, "", &build->word[1],
                                                     " is no script's name: a word of a-z, 0-9, . _ and -");
        }
        build->action[build->actions++] = action;
        return rover_count_action(build, rule);
}

/*
        Where a rule runs: README.md's four cases. A kernel event's rule is
        the kernel's while everything it reads is; a stated place is a demand,
        and one the rule cannot meet is refused with what is in the way.
*/
static const char address_to rover_not_kernel(struct rover_build address_to build, const struct rover_rule address_to rule)
{
        for (positive at = 0; at < rule->comparisons; at++)
        {
                const struct rover_comparison address_to c = &build->comparison[rule->comparison + at];
                const struct rover_operand address_to side[3] = {&c->a, &c->b, &c->c};

                for (positive k = 0; k < 3; k++)
                {
                        if ((side[k]->source == ROVER_STATE_NUMBER || side[k]->source == ROVER_STATE_WORD) &&
                            side[k]->page != ROVER_PAGE_KERNEL)
                                return side[k]->page == ROVER_PAGE_SETTINGS ? " reads a setting, which the kernel does not have"
                                                                           : " reads a desktop state, which the kernel does not have";
                        if (side[k]->source == ROVER_CALL)
                        {
                                const struct rover_call address_to asked = &build->call[side[k]->index];

                                if (!rover_functions[asked->function].kernel)
                                        return " calls a routine the kernel has no body of";
                                for (positive a = 0; a < asked->count; a++)
                                        if ((asked->argument[a].source == ROVER_STATE_NUMBER ||
                                             asked->argument[a].source == ROVER_STATE_WORD) &&
                                            asked->argument[a].page != ROVER_PAGE_KERNEL)
                                                return " reads a desktop state, which the kernel does not have";
                        }
                }
        }
        for (positive at = 0; at < (positive)rule->then + rule->otherwise; at++)
        {
                const struct rover_action address_to action = &build->action[rule->action + at];

                for (positive k = 0; k < action->count; k++)
                {
                        const struct rover_operand address_to argument = &build->argument[action->argument + k];

                        if ((argument->source == ROVER_STATE_NUMBER || argument->source == ROVER_STATE_WORD) &&
                            argument->page != ROVER_PAGE_KERNEL)
                                return " hands an action a desktop state, which the kernel does not have";
                }
        }
        return null;
}

//      Whether a rule's comparisons, or the calls in them, read a state of a page.
static bool rover_reads_page(struct rover_build address_to build, const struct rover_rule address_to rule, p8 page)
{
        for (positive at = 0; at < rule->comparisons; at++)
        {
                const struct rover_comparison address_to c = &build->comparison[rule->comparison + at];
                const struct rover_operand address_to side[3] = {&c->a, &c->b, &c->c};

                for (positive k = 0; k < 3; k++)
                {
                        if ((side[k]->source == ROVER_STATE_NUMBER || side[k]->source == ROVER_STATE_WORD) &&
                            side[k]->page == page)
                                return true;
                        if (side[k]->source == ROVER_CALL)
                                for (positive a = 0; a < build->call[side[k]->index].count; a++)
                                {
                                        struct rover_operand argument = build->call[side[k]->index].argument[a];

                                        if ((argument.source == ROVER_STATE_NUMBER || argument.source == ROVER_STATE_WORD) &&
                                            argument.page == page)
                                                return true;
                                }
                }
        }
        return false;
}

static bool rover_place(struct rover_build address_to build, struct rover_rule address_to rule)
{
        const char address_to blocked = rover_not_kernel(build, rule);
        const struct rover_event_row address_to event = build->event;
        //      A when rule goes where its states are: the kernel's alone in the kernel, the desktop's in the desktop, and
        //      the settings page (the runner's) or a routine the kernel has no body of in the runner.
        p8 source = event ? event->source
                          : !blocked ? ROVER_KERNEL
                          : rover_reads_page(build, rule, ROVER_PAGE_SETTINGS) ||
                                    !rover_reads_page(build, rule, ROVER_PAGE_DESKTOP)
                              ? ROVER_RUNNER
                              : ROVER_DESKTOP;
        //      A when rule over the clock or the weekday is the runner's: it is the one that keeps the clock, and its minute
        //      is the change such a rule is asked at (the kernel's page never changes with the time).
        if (!event && (rule->reads & (ROVER_READS_CLOCK | ROVER_READS_WEEKDAY)))
        {
                if (build->want == 1)
                        return rover_refused(build, "place kernel, but a when rule over the clock or the weekday is the "
                                                    "runner's, which asks it each minute",
                                             null, null);
                source = ROVER_RUNNER;
        }
        if (build->want == 1 && (source != ROVER_KERNEL || blocked))
        {
                if (source != ROVER_KERNEL && event)
                {
                        rover_refused(build, "place kernel, but ", null, event->name);
                        rover_why_add(build, " is posted in userspace and the kernel never sees it");
                        return false;
                }
                rover_refused(build, "place kernel, but the rule", null, blocked);
                return false;
        }
        if (source == ROVER_KERNEL && (blocked || build->want == 2))
                source = ROVER_RUNNER;
        //      Only the kernel drops an event before its readers: a take is a place kernel the rule must meet.
        if ((rule->flags & ROVER_RULE_TAKE) && !event)
                return rover_refused(build, "take is an on rule's: a when rule has no event to take", null, null);
        if ((rule->flags & ROVER_RULE_TAKE) && source != ROVER_KERNEL)
        {
                if (event && event->source != ROVER_KERNEL)
                {
                        rover_refused(build, "take, but ", null, event->name);
                        rover_why_add(build, " is posted in userspace, after every reader has it");
                        return false;
                }
                return rover_refused(build, "take, but the rule", null,
                                     blocked ? blocked : " is placed in userspace, where the event has been read already");
        }
        {
                const char address_to refused =
                    rover_take_refused(rule, rover_key_of(rule, build->comparison, rover_pool(build)));

                if (refused)
                        return rover_refused(build, refused + 2, null, null);
        }
        rule->place = source;
        if (build->want)
                rule->flags |= ROVER_RULE_PLACED;
        return true;
}

static bool rover_rule_finish(struct rover_build address_to build)
{
        struct rover_rule address_to rule;

        if (build->block != ROVER_BLOCK_RULE)
                return true;
        rule = &build->rule[build->rules - 1];
        if (rule->event == 0 && !(rule->flags & ROVER_RULE_WHEN))
                return rover_refused(build, "a rule with no on or when", null, null);
        if (!rule->then)
                return rover_refused(build, "a rule with nothing to do: give it a do, run, tool or emit", null, null);
        if ((rule->event == ROVER_EVENT_EVERY || rule->event == ROVER_EVENT_AT) && !rule->trigger)
                return rover_refused(build, rule->event == ROVER_EVENT_EVERY ? "every takes its duration"
                                                                             : "at takes its clock time",
                                     null, null);
        if (rule->event == ROVER_EVENT_EVERY &&
            memory_load_unaligned(b64, rover_pool(build) + build->comparison[rule->comparison].b.index) < ROVER_EVERY_LEAST)
                return rover_refused(build, "every is a second or longer: the runner's clock is not a loop", null, null);
        if (build->stage == ROVER_STAGE_ELSE && !rule->otherwise)
                return rover_refused(build, "an else with nothing after it to do", null, null);
        if (!rover_place(build, rule))
                return false;
        if (rover_rule_numbers(rule, build->comparison))
                rule->flags |= ROVER_RULE_NUMBERS;
        //      A cue that posts itself with nothing to stop it is a loop the runner would cut at depth four: a rule on cue
        //      with no condition past its trigger, whose filter (none, a name, or a pattern) takes a cue it emits.
        if (rule->event == ROVER_CUE && rule->comparisons == rule->trigger)
        {
                const struct rover_comparison address_to trigger = rule->trigger ? &build->comparison[rule->comparison] : null;

                for (positive at = 0; at < rule->then; at++)
                {
                        const struct rover_action address_to action = &build->action[rule->action + at];
                        const struct rover_operand address_to name = &build->argument[action->argument];
                        p16 one, two;
                        bool takes;

                        if (action->kind != ROVER_EMIT || name->source != ROVER_CONST_WORD)
                                continue;
                        memory_copy(&two, rover_pool(build) + name->index, 2);
                        if (!trigger)
                                takes = true;
                        else if (trigger->b.source != ROVER_CONST_WORD || (trigger->op != ROVER_EQ && trigger->op != ROVER_MATCH))
                                takes = false;
                        else
                        {
                                memory_copy(&one, rover_pool(build) + trigger->b.index, 2);
                                takes = trigger->op == ROVER_EQ
                                            ? one == two && !memory_compare(rover_pool(build) + trigger->b.index + 2,
                                                                            rover_pool(build) + name->index + 2, one)
                                            : rover_glob(rover_pool(build) + name->index + 2, two,
                                                         rover_pool(build) + trigger->b.index + 2, one);
                        }
                        if (takes)
                                return rover_refused(build, "a rule that emits a cue its own filter takes, with no condition, never stops",
                                                     null, null);
                }
        }
        return true;
}

// ---- services --------------------------------------------------------------

//      A word of a service's that must be an absolute path: pooled, its offset, or 0 with the reason said.
static p32 rover_path_word(struct rover_build address_to build, const struct rover_word address_to word)
{
        if (word->length < 2 || word->text[0] != '/' || word->length > ROVER_WORD_MOST || rover_find(word->text, 0, word->length))
                return rover_refused(build, "", word, " is no absolute path of at most 62 bytes"), 0;
        return rover_pool_string(build, word->text, word->length);
}

static bool rover_duration_ms(struct rover_build address_to build, const struct rover_word address_to word, b64 most,
                              p32 address_to into)
{
        b64 value;

        if (!rover_literal(build, word, ROVER_TYPE_DURATION, &value))
                return false;
        if (value % 1000000 || value / 1000000 > most)
                return rover_refused(build, "", word, " is not whole milliseconds, or is longer than this takes");
        *into = (p32)(value / 1000000);
        return true;
}

//      A service's statements in their order, the way a rule's header keeps its own.
static bool rover_service_slot(struct rover_build address_to build, p8 slot)
{
        static const char address_to const order =
            "a service goes about, run, needs, ready, log, restart, give-up, off, then its if lines";

        if (build->stage != ROVER_STAGE_HEADER)
                return rover_refused(build, "", &build->word[0], " comes before a service's if lines");
        if (slot <= build->header)
        {
                rover_refused(build, "", &build->word[0], slot == build->header ? " is said twice" : " is out of order: ");
                if (slot != build->header)
                        rover_why_add(build, order);
                return false;
        }
        if (slot > 2 && build->header < 2)
                return rover_refused(build, "", &build->word[0], " before the service's run");
        build->header = slot;
        return rover_note_flush(build, slot);
}

static bool rover_service_statement(struct rover_build address_to build)
{
        struct rover_service address_to service = &build->service[build->services - 1];
        const struct rover_word address_to keyword = &build->word[0];
        b64 value;

        if (rover_is(keyword, "about"))
        {
                if (!rover_service_slot(build, 1))
                        return false;
                if (build->words != 2 || !rover_literal(build, &build->word[1], ROVER_TYPE_TEXT, &value))
                        return build->why_used ? false : rover_refused(build, "about takes one text: quote it", null, null);
                service->about = rover_pool_string(build, build->word[1].text, build->word[1].length);
                return !build->why_used;
        }
        if (rover_is(keyword, "run"))
        {
                if (!rover_service_slot(build, 2))
                        return false;
                if (build->words < 2)
                        return rover_refused(build, "run names the program, by its path", null, null);
                if (build->arguments + build->words - 1 > ROVER_OPERANDS_MOST || build->words - 1 > 255)
                        return rover_refused(build, "too many words for one table", null, null);
                service->argument = (p16)build->arguments;
                service->count = (p8)(build->words - 1);
                for (positive at = 1; at < build->words; at++)
                {
                        struct rover_operand word = {ROVER_CONST_WORD, ROVER_TYPE_WORD, 0};
                        p32 where = at == 1 ? rover_path_word(build, &build->word[at]) : 0;

                        if (at > 1 && rover_literal(build, &build->word[at], ROVER_TYPE_WORD, &value))
                                where = rover_pool_string(build, build->word[at].text, build->word[at].length);
                        if (build->why_used)
                                return false;
                        if (!where)
                                return rover_refused(build, "", &build->word[at], ": a service's words are not empty");
                        word.index = (p16)where;
                        build->argument[build->arguments++] = word;
                }
                return true;
        }
        if (rover_is(keyword, "needs"))
        {
                if (!rover_service_slot(build, 3))
                        return false;
                if (build->words < 2 || build->words - 1 > ROVER_NEEDS)
                        return rover_refused(build, "needs names one to four services above", null, null);
                for (positive at = 1; at < build->words; at++)
                {
                        p32 found = ROVER_SERVICE_NONE;

                        for (p32 other = 0; other + 1 < build->services; other++)
                        {
                                p16 length = memory_load_unaligned(p16, rover_pool(build) + build->service[other].name);

                                if (length == build->word[at].length &&
                                    !memory_compare(rover_pool(build) + build->service[other].name + 2, build->word[at].text,
                                                    length))
                                        found = other;
                        }
                        if (found == ROVER_SERVICE_NONE)
                                return rover_refused(build, "", &build->word[at], " is no service above this one");
                        service->need[at - 1] = (p8)found;
                }
                return true;
        }
        if (rover_is(keyword, "ready") || rover_is(keyword, "log"))
        {
                bool ready = rover_is(keyword, "ready");

                if (!rover_service_slot(build, ready ? 4 : 5))
                        return false;
                if (build->words != 2)
                        return rover_refused(build, "", keyword, " takes one path");
                p32 where = rover_path_word(build, &build->word[1]);
                if (!where)
                        return false;
                *(ready ? &service->ready : &service->log) = where;
                return true;
        }
        if (rover_is(keyword, "restart"))
        {
                if (!rover_service_slot(build, 6))
                        return false;
                if (build->words < 2 || build->words > 4)
                        return rover_refused(build, "restart takes one to three waits, after the first, second and third short life", null,
                                             null);
                for (positive at = 0; at < 3; at++)
                        if (!rover_duration_ms(build, &build->word[at + 1 < build->words ? at + 1 : build->words - 1], 3600000,
                                               &service->wait_ms[at]))
                                return false;
                return true;
        }
        if (rover_is(keyword, "give-up"))
        {
                b64 lives;

                if (!rover_service_slot(build, 7))
                        return false;
                if (build->words != 4 || !rover_is(&build->word[2], "under"))
                        return rover_refused(build, "give-up is give-up LIVES under DURATION", null, null);
                if (build->word[1].quoted || !rover_literal(build, &build->word[1], ROVER_TYPE_INT, &lives) || lives < 1 ||
                    lives > 16)
                        return build->why_used ? false : rover_refused(build, "", &build->word[1], " is not 1 to 16 lives");
                service->lives = (p8)lives;
                return rover_duration_ms(build, &build->word[3], ROVER_LIMIT_MOST_MS, &service->short_ms) &&
                       (service->short_ms ? true : rover_refused(build, "a life of no length is never short", null, null));
        }
        if (rover_is(keyword, "off"))
        {
                if (!rover_service_slot(build, 8))
                        return false;
                if (build->words != 1)
                        return rover_refused(build, "off takes nothing after it", null, null);
                service->flags |= ROVER_SERVICE_OFF;
                return true;
        }
        if (rover_is(keyword, "if"))
        {
                if (build->header < 2)
                        return rover_refused(build, "an if before the service's run", null, null);
                build->stage = ROVER_STAGE_IF;
                build->event = null;
                if (!rover_note_flush(build, build->slot++) || !rover_read_condition(build, &build->service_rule, 1))
                        return false;
                service->comparisons = build->service_rule.comparisons;
                service->reads = build->service_rule.reads;
                return true;
        }
        return rover_refused(build, "", keyword,
                             " is no service statement: about, run, needs, ready, log, restart, give-up, off or if");
}

static bool rover_block_finish(struct rover_build address_to build)
{
        if (build->block == ROVER_BLOCK_SERVICE && !build->service[build->services - 1].count)
                return rover_refused(build, "a service with no run", null, null);
        return rover_rule_finish(build);
}

static bool rover_read_service(struct rover_build address_to build)
{
        static const p32 waits[3] = ROVER_SERVICE_WAITS;
        const struct rover_word address_to name = &build->word[1];
        struct rover_service address_to service;

        if (!rover_block_finish(build))
                return false;
        if (build->rules && !build->merge)
                return rover_refused(build, "a service after a rule: a file's services come before its rules", null, null);
        if (build->words != 2 || name->quoted || !name->length || name->length > ROVER_NAME_MOST)
                return rover_refused(build, "service takes one word, its name", null, null);
        for (positive at = 0; at < name->length; at++)
        {
                p8 c = name->text[at];

                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                        return rover_refused(build, "", name, " is no service name: at most 48 bytes of a-z, 0-9, . _ and -");
        }
        for (positive at = 0; at < build->services; at++)
        {
                p16 length = memory_load_unaligned(p16, rover_pool(build) + build->service[at].name);

                if (length == name->length && !memory_compare(rover_pool(build) + build->service[at].name + 2, name->text, length))
                        return rover_refused(build, "", name, " is the name of a service above");
        }
        if (build->services == ROVER_SERVICES_MOST)
                return rover_refused(build, "too many services for one table", null, null);
        service = &build->service[build->services++];
        memory_zero(service, sizeof(*service));
        memory_fill(service->need, ROVER_SERVICE_NONE, ROVER_NEEDS);
        memory_copy(service->wait_ms, waits, sizeof(waits));
        service->lives = ROVER_SERVICE_LIVES;
        service->short_ms = ROVER_SERVICE_SHORT_MS;
        service->comparison = (p16)build->comparisons;
        service->name = rover_pool_string(build, name->text, name->length);
        memory_zero(&build->service_rule, sizeof(build->service_rule));
        build->service_rule.comparison = (p16)build->comparisons;
        build->block = ROVER_BLOCK_SERVICE;
        build->stage = ROVER_STAGE_HEADER;
        build->header = 0;
        build->slot = 9;
        build->event = null;
        return !build->why_used && rover_note_flush(build, 0);
}

static bool rover_read_rule(struct rover_build address_to build)
{
        struct rover_rule address_to rule;
        const struct rover_word address_to name = &build->word[1];

        if (!rover_block_finish(build))
                return false;
        if (build->words != 2)
                return rover_refused(build, "rule takes one word, its name", null, null);
        if (name->quoted || !name->length || name->length > ROVER_NAME_MOST)
                return rover_refused(build, "", name, " is no rule name: at most 48 bytes of a-z, 0-9, . _ and -");
        for (positive at = 0; at < name->length; at++)
        {
                p8 c = name->text[at];

                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                        return rover_refused(build, "", name, " is no rule name: at most 48 bytes of a-z, 0-9, . _ and -");
        }
        for (positive at = 0; at < build->rules; at++)
        {
                p16 length;

                memory_copy(&length, rover_pool(build) + build->rule[at].name, 2);
                if (length == name->length && !memory_compare(rover_pool(build) + build->rule[at].name + 2, name->text,
                                                              length))
                        return rover_refused(build, "", name, " is the name of a rule above");
        }
        if (build->rules == ROVER_RULES_MOST)
                return rover_refused(build, "too many rules for one table", null, null);
        rule = &build->rule[build->rules++];
        memory_zero(rule, sizeof(*rule));
        rule->comparison = (p16)build->comparisons;
        rule->action = (p16)build->actions;
        rule->name = rover_pool_string(build, name->text, name->length);
        build->block = ROVER_BLOCK_RULE;
        build->stage = ROVER_STAGE_HEADER;
        build->header = 0;
        build->want = 0;
        build->slot = 8;
        build->event = null;
        return !build->why_used && rover_note_flush(build, 0);
}

//      A header statement in its place: about, the trigger, place, limit, once, off, take.
static bool rover_header(struct rover_build address_to build, p8 slot)
{
        static const char address_to const order = "the header goes about, on or when, place, limit, once, off, take";

        if (build->stage != ROVER_STAGE_HEADER)
                return rover_refused(build, "", &build->word[0],
                                     " belongs to the header, which comes before the conditions and actions");
        if (slot <= build->header)
        {
                rover_refused(build, "", &build->word[0], slot == build->header ? " is said twice" : " is out of order: ");
                if (slot != build->header)
                        rover_why_add(build, order);
                return false;
        }
        if (slot > 2 && build->header < 2)
                return rover_refused(build, "", &build->word[0], " before the rule's on or when");
        build->header = slot;
        return rover_note_flush(build, slot);
}

static bool rover_statement(struct rover_build address_to build)
{
        const struct rover_word address_to keyword = &build->word[0];
        struct rover_rule address_to rule = build->rules ? &build->rule[build->rules - 1] : null;
        b64 value;

        if (rover_is(keyword, "about"))
        {
                if (!rover_header(build, 1))
                        return false;
                if (build->words != 2)
                        return rover_refused(build, "about takes one text: quote it", null, null);
                if (!rover_literal(build, &build->word[1], ROVER_TYPE_TEXT, &value))
                        return false;
                rule->about = rover_pool_string(build, build->word[1].text, build->word[1].length);
                return !build->why_used;
        }
        if (rover_is(keyword, "on"))
                return rover_header(build, 2) && rover_read_on(build, rule);
        //      every DURATION and at CLOCK: on every and on at, with the one filter each must have.
        if (rover_is(keyword, "every") || rover_is(keyword, "at"))
        {
                const struct rover_event_row address_to row =
                    rover_event_find(rover_is(keyword, "every") ? ROVER_EVENT_EVERY : ROVER_EVENT_AT);
                struct rover_comparison made;

                if (!rover_header(build, 2))
                        return false;
                if (build->words != 2)
                        return rover_refused(build, "", keyword, rover_is(keyword, "every") ? " takes one duration"
                                                                                           : " takes one clock time");
                memory_zero(&made, sizeof(made));
                made.op = ROVER_EQ;
                made.type = rover_types[row->field_type[0]].kind;
                made.join = ROVER_JOIN_LINE;
                made.a.source = ROVER_FIELD;
                if (!rover_constant(build, &build->word[1], row->field_type[0], &made.b))
                        return false;
                build->event = row;
                rule->event = row->id;
                rule->comparisons++;
                rule->trigger++;
                return rover_add_comparison(build, &made);
        }
        if (rover_is(keyword, "when"))
        {
                if (!rover_header(build, 2))
                        return false;
                rule->flags |= ROVER_RULE_WHEN;
                if (!rover_read_condition(build, rule, 1))
                        return false;
                rule->trigger = (p8)rule->comparisons;
                //      The when line is one line, however many and and or it holds.
                for (positive at = 0; at + 1 < rule->comparisons; at++)
                        build->comparison[rule->comparison + at].join &= (p8)~ROVER_JOIN_LINE;
                return true;
        }
        if (rover_is(keyword, "place"))
        {
                if (!rover_header(build, 3))
                        return false;
                if (build->words != 2 || (!rover_is(&build->word[1], "kernel") && !rover_is(&build->word[1], "user")))
                        return rover_refused(build, "place is kernel or user", null, null);
                build->want = rover_is(&build->word[1], "kernel") ? 1 : 2;
                return true;
        }
        if (rover_is(keyword, "limit"))
        {
                if (!rover_header(build, 4))
                        return false;
                if (build->words != 2)
                        return rover_refused(build, "limit takes one duration", null, null);
                if (!rover_literal(build, &build->word[1], ROVER_TYPE_DURATION, &value))
                        return false;
                if (!value || value % 1000000)
                        return rover_refused(build, "", &build->word[1], " is no limit: a limit is whole milliseconds");
                rule->limit_ms = (p32)(value / 1000000);
                return true;
        }
        if (rover_is(keyword, "once") || rover_is(keyword, "off") || rover_is(keyword, "take"))
        {
                p8 slot = rover_is(keyword, "once") ? 5 : rover_is(keyword, "off") ? 6 : 7;

                if (!rover_header(build, slot))
                        return false;
                if (build->words != 1)
                        return rover_refused(build, "", keyword, " takes nothing after it");
                rule->flags |= slot == 5 ? ROVER_RULE_ONCE : slot == 6 ? ROVER_RULE_OFF : ROVER_RULE_TAKE;
                return true;
        }
        if (build->header < 2 && (rover_is(keyword, "if") || rover_is(keyword, "do") || rover_is(keyword, "run") ||
                                  rover_is(keyword, "tool") || rover_is(keyword, "emit") || rover_is(keyword, "set") ||
                                  rover_is(keyword, "else")))
                return rover_refused(build, "", keyword, " before the rule's on or when");
        if (rover_is(keyword, "if"))
        {
                if (build->stage > ROVER_STAGE_IF)
                        return rover_refused(build, "an if after an action: the conditions come first", null, null);
                build->stage = ROVER_STAGE_IF;
                return rover_note_flush(build, build->slot++) && rover_read_condition(build, rule, 1);
        }
        if (rover_is(keyword, "do") || rover_is(keyword, "run") || rover_is(keyword, "tool") ||
            rover_is(keyword, "emit") || rover_is(keyword, "set"))
        {
                if (build->stage < ROVER_STAGE_THEN)
                        build->stage = ROVER_STAGE_THEN;
                return rover_note_flush(build, build->slot++) && rover_read_action(build, rule);
        }
        if (rover_is(keyword, "else"))
        {
                if (build->words != 1)
                        return rover_refused(build, "else stands alone; its actions follow on their own lines", null, null);
                if (build->stage != ROVER_STAGE_THEN)
                        return rover_refused(build, build->stage == ROVER_STAGE_ELSE ? "a second else"
                                                                                     : "an else before any action",
                                             null, null);
                build->stage = ROVER_STAGE_ELSE;
                return rover_note_flush(build, build->slot++);
        }
        if (rover_is(keyword, "rule"))
                return rover_refused(build, "a rule line stands at the left edge, not inside a rule", null, null);
        return rover_refused(build, "", keyword,
                             " is no statement: about, on, when, every, at, place, limit, once, off, take, if, do, set, run, tool, emit or else");
}

/*
        One line into words. Words part at spaces and tabs; "..." is one word
        with \" and \\ inside it; a word holding ( runs to its ), spaces and
        all, so a call is one word; and # where a word starts is the line's
        note, handed back. Quoted words are copied, decoded, into the line's
        scratch.
*/
static bool rover_split(struct rover_build address_to build, const p8 address_to line, positive length,
                        const p8 address_to address_to note, positive address_to note_length)
{
        positive at = 0;

        build->words = 0;
        build->scratch_used = 0;
        *note = null;
        *note_length = 0;
        while (at < length)
        {
                struct rover_word address_to word;

                if (line[at] == ' ' || line[at] == '\t')
                {
                        at++;
                        continue;
                }
                if (line[at] == '#')
                {
                        positive from = at + 1, to = length;

                        while (from < to && line[from] == ' ')
                                from++;
                        while (to > from && (line[to - 1] == ' ' || line[to - 1] == '\t'))
                                to--;
                        *note = line + from;
                        *note_length = to - from;
                        return true;
                }
                if (build->words == ROVER_LINE_WORDS)
                        return rover_refused(build, "a line of more words than a statement has", null, null);
                word = &build->word[build->words++];
                word->quoted = 0;
                if (line[at] == '"')
                {
                        p8 address_to into = build->scratch + build->scratch_used;
                        positive made = 0;

                        at++;
                        while (at < length && line[at] != '"')
                        {
                                if (line[at] == '\\' && at + 1 < length && (line[at + 1] == '"' || line[at + 1] == '\\'))
                                        at++;
                                into[made++] = line[at++];
                        }
                        if (at == length)
                                return rover_refused(build, "a quote that does not close", null, null);
                        at++;
                        word->text = into;
                        word->length = (p32)made;
                        word->quoted = 1;
                        build->scratch_used += (p32)made;
                        continue;
                }
                positive start = at;
                bool quoted = false;
                b32 depth = 0;

                while (at < length && (depth || quoted || (line[at] != ' ' && line[at] != '\t')))
                {
                        if (line[at] == '"' && depth)
                                quoted = !quoted;
                        else if (!quoted && line[at] == '(')
                                depth++;
                        else if (!quoted && line[at] == ')' && depth)
                                depth--;
                        at++;
                }
                if (depth || quoted)
                        return rover_refused(build, "a call that does not close its (", null, null);
                word->text = line + start;
                word->length = (p32)(at - start);
        }
        return true;
}

static fn rover_build_reset(struct rover_build address_to build)
{
        build->rules = build->comparisons = build->calls = build->actions = build->arguments = build->notes = 0;
        build->services = 0;
        build->block = ROVER_BLOCK_NONE;
        build->pool = 4;
        memory_zero(build->pool_words, 8);
        build->pendings = 0;
        build->stage = ROVER_STAGE_HEADER;
        build->header = 0;
        build->event = null;
        build->why_used = 0;
        build->why[0] = 0;
        build->line = 0;
        build->size = 0;
}

static p8 address_to rover_built(struct rover_build address_to build)
{
        return (p8 address_to)build->table_words;
}

//      The sections laid out as rover_layout says, the order sorted by event, and the whole checked.
static bool rover_assemble(struct rover_build address_to build)
{
        p8 address_to table = rover_built(build);
        struct rover_head head;
        positive at[11];
        p16 count[ROVER_EVENTS];
        struct rover_span address_to index;
        p16 address_to order;
        struct rover_table view;
        struct rover_refusal refusal;

        memory_zero(&head, sizeof(head));
        head.magic = ROVER_MAGIC;
        head.version = ROVER_VERSION;
        head.schema = ROVER_SCHEMA_HASH;
        head.rules = (p16)build->rules;
        head.comparisons = (p16)build->comparisons;
        head.calls = (p16)build->calls;
        head.actions = (p16)build->actions;
        head.arguments = (p16)build->arguments;
        head.notes = (p16)build->notes;
        head.services = (p16)build->services;
        head.pool = (p32)rover_align8(build->pool);
        build->size = rover_layout(&head, at);
        if (build->size > ROVER_TABLE_MOST)
                return rover_refused(build, "the rules make a table larger than 64 KiB", null, null);
        head.size = (p32)build->size;
        memory_zero(table, build->size);
        memory_copy(table, &head, sizeof(head));
        memory_copy(table + at[0], build->rule, build->rules * sizeof(struct rover_rule));
        memory_copy(table + at[10], build->service, build->services * sizeof(struct rover_service));
        memory_copy(table + at[4], build->comparison, build->comparisons * sizeof(struct rover_comparison));
        memory_copy(table + at[5], build->call, build->calls * sizeof(struct rover_call));
        memory_copy(table + at[6], build->action, build->actions * sizeof(struct rover_action));
        memory_copy(table + at[7], build->argument, build->arguments * sizeof(struct rover_operand));
        memory_copy(table + at[8], build->note, build->notes * sizeof(struct rover_note));
        memory_copy(table + at[9], rover_pool(build), build->pool);

        index = (struct rover_span address_to)(table + at[3]);
        order = (p16 address_to)(table + at[1]);
        memory_zero(count, sizeof(count));
        for (positive k = 0; k < build->rules; k++)
                count[build->rule[k].event]++;
        p16 first = 0;
        for (positive id = 0; id < ROVER_EVENTS; id++)
        {
                index[id].first = first;
                index[id].count = 0;
                first += count[id];
        }
        for (positive k = 0; k < build->rules; k++)
        {
                struct rover_span address_to span = &index[build->rule[k].event];

                order[span->first + span->count++] = (p16)k;
        }
        for (positive id = 0; id < ROVER_EVENTS; id++)
        {
                bool numbers = index[id].count != 0;

                for (positive k = index[id].first; k < (positive)index[id].first + index[id].count; k++)
                        numbers = numbers && (build->rule[order[k]].flags & ROVER_RULE_NUMBERS);
                if (numbers)
                        index[id].count |= ROVER_SPAN_NUMBERS;
        }
        for (positive k = 0; k < build->rules; k++)
                ((p32 address_to)(table + at[2]))[k] = rover_key_of(&build->rule[order[k]], build->comparison,
                                                                    rover_pool(build));
        if (!rover_check(table, build->size, &view, &refusal))
        {
                rover_refused(build, "the compiler made a table its own check refuses: ", null, refusal.why);
                return false;
        }
        //      What the kernel's loader refuses as well (rover_kernel_fits), said here with the event's name.
        {
                p32 event = 0;
                const char address_to unfit = rover_kernel_fits(&view, &event);
                const struct rover_event_row address_to row = rover_event_find(event);

                if (unfit)
                {
                        rover_refused(build, unfit, null, ": ");
                        rover_why_add(build, row ? row->name : "the when rules");
                        return false;
                }
        }
        return true;
}

/*
        A file of rules to a table. Answers true with the table in the build;
        false with the line and the reason in it.
*/
static bool rover_compile(struct rover_build address_to build, const p8 address_to text, positive length)
{
        positive at = 0;

        rover_build_reset(build);
        if (length > ROVER_SOURCE_MOST)
                return rover_refused(build, "a rule file larger than 64 KiB", null, null);
        while (at < length)
        {
                const p8 address_to line = text + at;
                const p8 address_to stop = rover_find(line, '\n', length - at);
                positive size = stop ? (positive)(stop - line) : length - at;
                positive depth = 0;
                const p8 address_to note;
                positive note_length;

                at += size + (stop ? 1 : 0);
                build->line++;
                if (size && line[size - 1] == '\r')
                        size--;
                if (size > ROVER_LINE_MOST)
                        return rover_refused(build, "a line longer than 4096 bytes", null, null);
                //      No control byte but the tab: a carriage return inside a word is invisible where the rule is read,
                //      and a NUL ends a note early for any reader of it.
                for (positive k = 0; k < size; k++)
                        if ((line[k] < 0x20 && line[k] != '\t') || line[k] == 0x7f)
                                return rover_refused(build, "a line with a control byte in it (only a tab may be)", null, null);
                while (depth < size && line[depth] == '\t')
                        depth++;
                if (depth < size && line[depth] == ' ')
                {
                        positive look = depth;

                        while (look < size && (line[look] == ' ' || line[look] == '\t'))
                                look++;
                        if (look < size)
                                return rover_refused(build, "a line indented with a space: rover indents with tabs",
                                                     null, null);
                }
                if (!rover_split(build, line + depth, size - depth, &note, &note_length))
                        return false;
                //      A note is pooled where it is read, before the words of its line, so a trailing one and the
                //      same note printed above its statement make the same pool.
                if (note)
                {
                        if (build->pendings == array_count(build->pending))
                                return rover_refused(build, "too many notes in a row", null, null);
                        p32 where = note_length ? rover_pool_string(build, note, note_length) : 0;
                        if (build->why_used)
                                return false;
                        build->pending[build->pendings++] = where;
                }
                if (!build->words)
                        continue;
                if (depth == 0)
                {
                        if (rover_is(&build->word[0], "service"))
                        {
                                if (!rover_read_service(build))
                                        return false;
                                continue;
                        }
                        if (!rover_is(&build->word[0], "rule"))
                                return rover_refused(build, "", &build->word[0],
                                                     " stands at the left edge, where only rule and service lines stand: indent it with a tab");
                        if (!rover_read_rule(build))
                                return false;
                        continue;
                }
                if (depth > 1)
                        return rover_refused(build, "a line two tabs deep: a rule's statements are one tab deep", null,
                                             null);
                if (build->block == ROVER_BLOCK_NONE)
                        return rover_refused(build, "a statement before any rule or service line", null, null);
                if (!(build->block == ROVER_BLOCK_SERVICE ? rover_service_statement(build) : rover_statement(build)))
                        return false;
        }
        build->line = 0;
        if (!rover_block_finish(build))
                return false;
        if (build->pendings && build->block != ROVER_BLOCK_NONE && !rover_note_flush(build, ROVER_SLOT_AFTER))
                return false;
        //      Notes with no block to stand by are dropped, and so are their bytes in the pool: a file of comments alone is
        //      the empty table, as its printing is.
        if (build->pendings && build->block == ROVER_BLOCK_NONE)
                build->pool = 4;
        build->pendings = 0;
        return rover_assemble(build);
}

// ---- the printer -----------------------------------------------------------

static fn rover_pool_put(struct rover_text address_to text, const struct rover_table address_to table, p32 at)
{
        p16 length;

        memory_copy(&length, table->pool + at, 2);
        rover_put(text, table->pool + at + 2, length);
}

static const struct rover_state_row address_to rover_state_at(p8 page, p16 offset)
{
        for (positive at = 0; at < ROVER_STATES; at++)
                if (rover_states[at].page == page && rover_states[at].offset == offset)
                        return &rover_states[at];
        return null;
}

static fn rover_operand_put(struct rover_text address_to text, const struct rover_table address_to table,
                            const struct rover_event_row address_to event, struct rover_operand operand)
{
        b64 number;
        p16 length;

        switch (operand.source)
        {
        case ROVER_CONST_NUMBER:
                memory_copy(&number, table->pool + operand.index, 8);
                rover_literal_put(text, operand.page, number, null, 0);
                return;
        case ROVER_CONST_WORD:
                memory_copy(&length, table->pool + operand.index, 2);
                rover_literal_put(text, operand.page, 0, table->pool + operand.index + 2, length);
                return;
        case ROVER_STATE_NUMBER:
        case ROVER_STATE_WORD:
        {
                const struct rover_state_row address_to state = rover_state_at(operand.page, operand.index);

                rover_puts(text, state ? state->name : "?");
                return;
        }
        case ROVER_FIELD:
                rover_puts(text, "$");
                rover_puts(text, event && operand.index < event->fields ? event->field_name[operand.index] : "?");
                return;
        case ROVER_CALL:
        {
                const struct rover_call address_to asked = table->call + operand.index;

                rover_puts(text, rover_functions[asked->function].name);
                rover_puts(text, "(");
                for (positive at = 0; at < asked->count; at++)
                {
                        struct rover_operand argument = asked->argument[at];

                        if (at)
                                rover_puts(text, ", ");
                        //      A call's quoted argument is read with no escapes (rover_read_call), so it is written with
                        //      none: its bytes between two quotes, as they were read.
                        if (argument.source == ROVER_CONST_WORD &&
                            (memory_copy(&length, table->pool + argument.index, 2),
                             !rover_bare(table->pool + argument.index + 2, length)))
                        {
                                rover_puts(text, "\"");
                                rover_put(text, table->pool + argument.index + 2, length);
                                rover_puts(text, "\"");
                        }
                        else
                                rover_operand_put(text, table, event, argument);
                }
                rover_puts(text, ")");
                return;
        }
        case ROVER_CLOCK:
                rover_puts(text, "clock");
                return;
        case ROVER_WEEKDAY:
                rover_puts(text, "weekday");
                return;
        }
}

//      The comparisons from first, up to and with the one that ends their line.
static positive rover_line_put(struct rover_text address_to text, const struct rover_table address_to table,
                               const struct rover_event_row address_to event, positive first, positive last)
{
        positive at = first;

        for (; at < last; at++)
        {
                const struct rover_comparison address_to c = table->comparison + at;

                rover_operand_put(text, table, event, c->a);
                if (c->op != ROVER_TRUE)
                {
                        rover_puts(text, " ");
                        rover_puts(text, rover_operator_word(c->op));
                        rover_puts(text, " ");
                        rover_operand_put(text, table, event, c->b);
                        if (c->op == ROVER_BETWEEN)
                        {
                                rover_puts(text, " ");
                                rover_operand_put(text, table, event, c->c);
                        }
                }
                if (c->join & ROVER_JOIN_LINE)
                        return at + 1;
                rover_puts(text, c->join & ROVER_JOIN_OR ? " or " : " and ");
        }
        return at;
}

static fn rover_action_put(struct rover_text address_to text, const struct rover_table address_to table,
                           const struct rover_event_row address_to event, positive which)
{
        const struct rover_action address_to action = table->action + which;
        positive first = 0;

        switch (action->kind)
        {
        case ROVER_DO:
                rover_puts(text, "do ");
                rover_puts(text, rover_primitive_find(action->id)->name);
                break;
        case ROVER_RUN:
                rover_puts(text, "run ");
                rover_operand_put(text, table, event, table->argument[action->argument]);
                first = 1;
                break;
        case ROVER_TOOL:
                rover_puts(text, "tool ");
                rover_puts(text, rover_tools[action->id]);
                break;
        case ROVER_EMIT:
                rover_puts(text, "emit cue");
                break;
        case ROVER_SET:
                rover_puts(text, "set ");
                rover_puts(text, rover_switch_find(action->id)->name);
                break;
        }
        for (positive at = first; at < action->count; at++)
        {
                rover_puts(text, " ");
                rover_operand_put(text, table, event, table->argument[action->argument + at]);
        }
}

static fn rover_notes_put(struct rover_text address_to text, const struct rover_table address_to table, positive rule,
                          p16 slot, bool indent)
{
        for (positive at = 0; at < table->head->notes; at++)
        {
                if (table->note[at].rule != rule || table->note[at].slot != slot)
                        continue;
                rover_puts(text, indent ? "\t#" : "#");
                if (table->note[at].text)
                {
                        rover_puts(text, " ");
                        rover_pool_put(text, table, table->note[at].text);
                }
                rover_puts(text, "\n");
        }
}

//      A rule's trigger line, as the text says it: on EVENT FILTERS, or when CONDITION.
static fn rover_trigger_put(struct rover_text address_to text, const struct rover_table address_to table,
                            const struct rover_rule address_to rule)
{
        const struct rover_event_row address_to event = rule->event ? rover_event_find(rule->event) : null;

        if (!event)
        {
                rover_puts(text, "when ");
                rover_line_put(text, table, null, rule->comparison, (positive)rule->comparison + rule->trigger);
                return;
        }
        //      every and at are their own words: the one filter is their argument.
        if ((event->id == ROVER_EVENT_EVERY || event->id == ROVER_EVENT_AT) && rule->trigger == 1)
        {
                rover_puts(text, event->name);
                rover_puts(text, " ");
                rover_operand_put(text, table, event, table->comparison[rule->comparison].b);
                return;
        }
        rover_puts(text, "on ");
        rover_puts(text, event->name);
        for (positive at = 0; at < rule->trigger; at++)
        {
                rover_puts(text, " ");
                rover_operand_put(text, table, event, table->comparison[rule->comparison + at].b);
        }
}

static fn rover_word_put(struct rover_text address_to text, const struct rover_table address_to table, p32 at)
{
        p16 length = memory_load_unaligned(p16, table->pool + at);

        rover_literal_put(text, ROVER_TYPE_WORD, 0, table->pool + at + 2, length);
}

//      A service as the text says it, its policy said only where it is not the script's.
static fn rover_service_put(struct rover_text address_to text, const struct rover_table address_to table, positive which)
{
        static const p32 waits[3] = ROVER_SERVICE_WAITS;
        const struct rover_service address_to service = table->service + which;
        p16 owner = (p16)(which | ROVER_NOTE_SERVICE), slot = 9;

        rover_notes_put(text, table, owner, 0, false);
        rover_puts(text, "service ");
        rover_pool_put(text, table, service->name);
        rover_puts(text, "\n");
        if (service->about)
        {
                rover_notes_put(text, table, owner, 1, true);
                rover_puts(text, "\tabout ");
                rover_put_quoted(text, table->pool + service->about + 2, memory_load_unaligned(p16, table->pool + service->about));
                rover_puts(text, "\n");
        }
        rover_notes_put(text, table, owner, 2, true);
        rover_puts(text, "\trun");
        for (positive at = 0; at < service->count; at++)
        {
                rover_puts(text, " ");
                rover_word_put(text, table, table->argument[service->argument + at].index);
        }
        rover_puts(text, "\n");
        if (service->need[0] != ROVER_SERVICE_NONE)
        {
                rover_notes_put(text, table, owner, 3, true);
                rover_puts(text, "\tneeds");
                for (positive at = 0; at < ROVER_NEEDS && service->need[at] != ROVER_SERVICE_NONE; at++)
                {
                        rover_puts(text, " ");
                        rover_pool_put(text, table, table->service[service->need[at]].name);
                }
                rover_puts(text, "\n");
        }
        if (service->ready)
        {
                rover_notes_put(text, table, owner, 4, true);
                rover_puts(text, "\tready ");
                rover_word_put(text, table, service->ready);
                rover_puts(text, "\n");
        }
        if (service->log)
        {
                rover_notes_put(text, table, owner, 5, true);
                rover_puts(text, "\tlog ");
                rover_word_put(text, table, service->log);
                rover_puts(text, "\n");
        }
        if (memory_compare(service->wait_ms, waits, sizeof(waits)))
        {
                positive said = 3;

                //      The waits after the last one said are that one again, so they are not said.
                while (said > 1 && service->wait_ms[said - 1] == service->wait_ms[said - 2])
                        said--;
                rover_notes_put(text, table, owner, 6, true);
                rover_puts(text, "\trestart");
                for (positive at = 0; at < said; at++)
                {
                        rover_puts(text, " ");
                        rover_literal_put(text, ROVER_TYPE_DURATION, (b64)service->wait_ms[at] * 1000000, null, 0);
                }
                rover_puts(text, "\n");
        }
        if (service->lives != ROVER_SERVICE_LIVES || service->short_ms != ROVER_SERVICE_SHORT_MS)
        {
                rover_notes_put(text, table, owner, 7, true);
                rover_puts(text, "\tgive-up ");
                rover_put_number(text, service->lives);
                rover_puts(text, " under ");
                rover_literal_put(text, ROVER_TYPE_DURATION, (b64)service->short_ms * 1000000, null, 0);
                rover_puts(text, "\n");
        }
        if (service->flags & ROVER_SERVICE_OFF)
        {
                rover_notes_put(text, table, owner, 8, true);
                rover_puts(text, "\toff\n");
        }
        for (positive at = service->comparison; at < (positive)service->comparison + service->comparisons;)
        {
                rover_notes_put(text, table, owner, slot++, true);
                rover_puts(text, "\tif ");
                at = rover_line_put(text, table, null, at, (positive)service->comparison + service->comparisons);
                rover_puts(text, "\n");
        }
        rover_notes_put(text, table, owner, ROVER_SLOT_AFTER, false);
}

static fn rover_print(struct rover_text address_to text, const struct rover_table address_to table)
{
        for (positive which = 0; which < table->head->services; which++)
        {
                if (which)
                        rover_puts(text, "\n");
                rover_service_put(text, table, which);
        }
        for (positive number = 0; number < table->head->rules; number++)
        {
                const struct rover_rule address_to rule = table->rule + number;
                const struct rover_event_row address_to event = rule->event ? rover_event_find(rule->event) : null;
                p16 slot = 8;

                if (number || table->head->services)
                        rover_puts(text, "\n");
                rover_notes_put(text, table, number, 0, false);
                rover_puts(text, "rule ");
                rover_pool_put(text, table, rule->name);
                rover_puts(text, "\n");
                if (rule->about)
                {
                        rover_notes_put(text, table, number, 1, true);
                        rover_puts(text, "\tabout ");
                        p16 length;
                        memory_copy(&length, table->pool + rule->about, 2);
                        rover_put_quoted(text, table->pool + rule->about + 2, length);
                        rover_puts(text, "\n");
                }
                rover_notes_put(text, table, number, 2, true);
                rover_puts(text, "\t");
                rover_trigger_put(text, table, rule);
                rover_puts(text, "\n");
                if (rule->flags & ROVER_RULE_PLACED)
                {
                        rover_notes_put(text, table, number, 3, true);
                        rover_puts(text, rule->place == ROVER_KERNEL ? "\tplace kernel\n" : "\tplace user\n");
                }
                if (rule->limit_ms)
                {
                        rover_notes_put(text, table, number, 4, true);
                        rover_puts(text, "\tlimit ");
                        rover_literal_put(text, ROVER_TYPE_DURATION, (b64)rule->limit_ms * 1000000, null, 0);
                        rover_puts(text, "\n");
                }
                if (rule->flags & ROVER_RULE_ONCE)
                {
                        rover_notes_put(text, table, number, 5, true);
                        rover_puts(text, "\tonce\n");
                }
                if (rule->flags & ROVER_RULE_OFF)
                {
                        rover_notes_put(text, table, number, 6, true);
                        rover_puts(text, "\toff\n");
                }
                if (rule->flags & ROVER_RULE_TAKE)
                {
                        rover_notes_put(text, table, number, 7, true);
                        rover_puts(text, "\ttake\n");
                }
                for (positive at = (positive)rule->comparison + rule->trigger;
                     at < (positive)rule->comparison + rule->comparisons;)
                {
                        rover_notes_put(text, table, number, slot++, true);
                        rover_puts(text, "\tif ");
                        at = rover_line_put(text, table, event, at, (positive)rule->comparison + rule->comparisons);
                        rover_puts(text, "\n");
                }
                for (positive at = 0; at < (positive)rule->then + rule->otherwise; at++)
                {
                        if (at == rule->then)
                        {
                                rover_notes_put(text, table, number, slot++, true);
                                rover_puts(text, "\telse\n");
                        }
                        rover_notes_put(text, table, number, slot++, true);
                        rover_puts(text, "\t");
                        rover_action_put(text, table, event, (positive)rule->action + at);
                        rover_puts(text, "\n");
                }
                rover_notes_put(text, table, number, ROVER_SLOT_AFTER, false);
        }
}

// ---- the graph -------------------------------------------------------------

static fn rover_record_word(struct rover_text address_to text, const char address_to key, const p8 address_to bytes,
                            positive length)
{
        rover_puts(text, " ");
        rover_puts(text, key);
        rover_puts(text, "=");
        if (rover_bare(bytes, length) && !rover_find(bytes, '=', length))
                rover_put(text, bytes, length);
        else
                rover_put_quoted(text, bytes, length);
}

/*
        The graph Settings draws, as records: per rule an event node, a
        condition node per if line with a yes and a no port, an action node
        per action, and the edges between them in the text's order. A
        condition's no goes to the first else action, or nowhere.
*/
static fn rover_graph(struct rover_text address_to text, const struct rover_table address_to table)
{
        static const char address_to const places[ROVER_PLACES] = {"kernel", "runner", "desktop"};
        p8 label_bytes[512];

        for (positive which = 0; which < table->head->services; which++)
        {
                const struct rover_service address_to service = table->service + which;
                struct rover_text label = {label_bytes, 0, sizeof(label_bytes), false};

                for (positive at = 0; at < service->count; at++)
                {
                        if (at)
                                rover_puts(&label, " ");
                        rover_word_put(&label, table, table->argument[service->argument + at].index);
                }
                rover_puts(text, "node");
                rover_record_word(text, "service", table->pool + service->name + 2,
                                  memory_load_unaligned(p16, table->pool + service->name));
                rover_puts(text, " kind=service");
                rover_record_word(text, "label", label.bytes, label.used);
                if (service->flags & ROVER_SERVICE_OFF)
                        rover_puts(text, " off=yes");
                rover_puts(text, "\n");
                for (positive at = 0; at < ROVER_NEEDS && service->need[at] != ROVER_SERVICE_NONE; at++)
                {
                        const struct rover_service address_to need = table->service + service->need[at];

                        rover_puts(text, "edge");
                        rover_record_word(text, "service", table->pool + service->name + 2,
                                          memory_load_unaligned(p16, table->pool + service->name));
                        rover_record_word(text, "needs", table->pool + need->name + 2,
                                          memory_load_unaligned(p16, table->pool + need->name));
                        rover_puts(text, "\n");
                }
        }
        for (positive number = 0; number < table->head->rules; number++)
        {
                const struct rover_rule address_to rule = table->rule + number;
                const struct rover_event_row address_to event = rule->event ? rover_event_find(rule->event) : null;
                p8 name[ROVER_NAME_MOST + 1];
                p16 name_length;
                positive node = 1, conditions = 0, first_action, first_else;

                memory_copy(&name_length, table->pool + rule->name, 2);
                memory_copy(name, table->pool + rule->name + 2, name_length);

                struct rover_text label = {label_bytes, 0, sizeof(label_bytes), false};
                rover_trigger_put(&label, table, rule);
                rover_puts(text, "node");
                rover_record_word(text, "rule", name, name_length);
                rover_puts(text, " id=0 kind=event place=");
                rover_puts(text, places[rule->place]);
                rover_record_word(text, "label", label.bytes, label.used);
                if (rule->about)
                {
                        p16 length;

                        memory_copy(&length, table->pool + rule->about, 2);
                        rover_record_word(text, "about", table->pool + rule->about + 2, length);
                }
                if (rule->flags & ROVER_RULE_OFF)
                        rover_puts(text, " off=yes");
                rover_puts(text, "\n");

                for (positive at = (positive)rule->comparison + rule->trigger;
                     at < (positive)rule->comparison + rule->comparisons; conditions++, node++)
                {
                        label.used = 0;
                        at = rover_line_put(&label, table, event, at, (positive)rule->comparison + rule->comparisons);
                        rover_puts(text, "node");
                        rover_record_word(text, "rule", name, name_length);
                        rover_puts(text, " id=");
                        rover_put_number(text, (b64)node);
                        rover_puts(text, " kind=condition");
                        rover_record_word(text, "label", label.bytes, label.used);
                        rover_puts(text, "\n");
                }
                first_action = node;
                first_else = node + rule->then;
                for (positive at = 0; at < (positive)rule->then + rule->otherwise; at++, node++)
                {
                        label.used = 0;
                        rover_action_put(&label, table, event, (positive)rule->action + at);
                        rover_puts(text, "node");
                        rover_record_word(text, "rule", name, name_length);
                        rover_puts(text, " id=");
                        rover_put_number(text, (b64)node);
                        rover_puts(text, at < rule->then ? " kind=action branch=then" : " kind=action branch=else");
                        rover_record_word(text, "label", label.bytes, label.used);
                        rover_puts(text, "\n");
                }

                //      Edges: event to the first condition (or action), each condition's yes to the next, its no to
                //      the else, and each action's then to the next of its branch.
                for (positive from = 0; from <= conditions; from++)
                {
                        positive to = from + 1;

                        rover_puts(text, "edge");
                        rover_record_word(text, "rule", name, name_length);
                        rover_puts(text, " from=");
                        rover_put_number(text, (b64)from);
                        rover_puts(text, from ? " port=yes to=" : " port=out to=");
                        rover_put_number(text, (b64)to);
                        rover_puts(text, "\n");
                        if (from && rule->otherwise)
                        {
                                rover_puts(text, "edge");
                                rover_record_word(text, "rule", name, name_length);
                                rover_puts(text, " from=");
                                rover_put_number(text, (b64)from);
                                rover_puts(text, " port=no to=");
                                rover_put_number(text, (b64)first_else);
                                rover_puts(text, "\n");
                        }
                }
                for (positive at = first_action; at + 1 < node; at++)
                {
                        if (at + 1 == first_else)
                                continue;
                        rover_puts(text, "edge");
                        rover_record_word(text, "rule", name, name_length);
                        rover_puts(text, " from=");
                        rover_put_number(text, (b64)at);
                        rover_puts(text, " port=then to=");
                        rover_put_number(text, (b64)(at + 1));
                        rover_puts(text, "\n");
                }
        }
}

// ---- states for a test -----------------------------------------------------

//      STATE=VALUE into a page, or the clock and weekday into the event's time.
static bool rover_assign(struct rover_build address_to build, p8 address_to address_to pages, const p8 address_to text,
                         positive length, struct rover_event address_to event)
{
        const p8 address_to equals = rover_find(text, '=', length);
        struct rover_word value;
        const struct rover_state_row address_to state;
        b64 number;

        if (!equals)
                return false;
        value.text = equals + 1;
        value.length = (p32)(length - (positive)(equals + 1 - text));
        value.quoted = 0;
        positive name = (positive)(equals - text);
        if (rover_bytes_are(text, name, "clock") || rover_bytes_are(text, name, "weekday"))
        {
                bool clock = rover_bytes_are(text, name, "clock");
                b64 days = rover_floor_mod(event->realtime, 86400 * 7) / 86400, minutes;

                if (!rover_literal(build, &value, clock ? ROVER_TYPE_CLOCK : ROVER_TYPE_WEEKDAY, &number))
                        return false;
                minutes = rover_floor_mod(event->realtime, 86400) / 60;
                if (clock)
                        minutes = number;
                else
                        days = rover_floor_mod(number - 3, 7);
                event->realtime = days * 86400 + minutes * 60;
                return true;
        }
        state = rover_state_named(text, name);
        if (!state)
        {
                struct rover_word word = {text, (p32)name, 0, {0}};

                rover_refused(build, "", &word, " is no state");
                rover_suggest(build, &word, rover_nearest_state(&word));
                return false;
        }
        if (!rover_literal(build, &value, state->type, &number))
                return false;
        if (rover_kind_is_word(rover_types[state->type].kind))
        {
                pages[state->page][state->offset] = (p8)value.length;
                memory_copy(pages[state->page] + state->offset + 1, value.text, value.length);
                pages[state->page][state->offset + 1 + value.length] = 0;
        }
        else
        {
                b32 four = (b32)number;

                memory_copy(pages[state->page] + state->offset, &four, 4);
        }
        return true;
}

// ---- the image's rules -----------------------------------------------------

/*
        The image's rule files, src/rover/examples, carried in the program
        itself, so the image's table needs no file: safe mode loads these
        alone, and /lib/moonwater/rules holds the same files to read and copy.
        By name, the order a table is made in.
*/
#define ROVER_IMAGE(name)                                                                                      \
        __asm__(".pushsection .rodata\n.globl rover_image_" #name "\nrover_image_" #name                       \
                ":\n.incbin \"src/rover/examples/" #name ".rover\"\n.byte 0\n.popsection\n");                  \
        extern const char rover_image_##name[];

ROVER_IMAGE(buttons)
ROVER_IMAGE(chords)
ROVER_IMAGE(desktop)
ROVER_IMAGE(power)
ROVER_IMAGE(session)

//      The at'th file's text, its name in *name; null past the last. A function, not a table of pointers: a static
//      program's read-only data takes no relocation.
#define ROVER_IMAGE_FILES 5u
static const char address_to rover_image_file(positive at, const char address_to address_to name)
{
        switch (at)
        {
        case 0: *name = "buttons.rover"; return rover_image_buttons;
        case 1: *name = "chords.rover"; return rover_image_chords;
        case 2: *name = "desktop.rover"; return rover_image_desktop;
        case 3: *name = "power.rover"; return rover_image_power;
        case 4: *name = "session.rover"; return rover_image_session;
        }
        *name = null;
        return null;
}

// ---- the verb --------------------------------------------------------------

#ifdef MOONWATER_CLI_ROVER

static struct rover_build rover_work;
static p8 rover_source[ROVER_SOURCE_MOST + 1];
static p64 rover_page_words[ROVER_PAGES][ROVER_PAGE_BYTES / 8];
static p8 rover_out_bytes[ROVER_TABLE_MOST * 4];

static bipolar rover_read_file(string_address path)
{
        bipolar handle = string_equals(path, "-") ? 0 : system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        positive used = 0;

        if (handle < 0)
                return handle;
        for (;;)
        {
                bipolar got = system_read_retry((positive)handle, rover_source + used, sizeof(rover_source) - used);

                if (got <= 0)
                {
                        if (handle)
                                system_close(handle);
                        if (got < 0)
                                return got;
                        break;
                }
                used += (positive)got;
                if (used == sizeof(rover_source))
                {
                        if (handle)
                                system_close(handle);
                        return -27; // EFBIG
                }
        }
        return (bipolar)used;
}

static b32 rover_usage(void)
{
        string_format(log_error,
                      "usage: moonwater rover check FILE ...      compile, and say ok or the line and the reason\n"
                      "       moonwater rover print FILE          the canonical text\n"
                      "       moonwater rover compile FILE OUT    the table, written to OUT\n"
                      "       moonwater rover show FILE           the graph, as records\n"
                      "       moonwater rover schema              the schema's records\n"
                      "       moonwater rover test FILE EVENT ... [STATE=VALUE ...]\n");
        log_flush();
        return 2;
}

static bool rover_verb_compile(string_address path)
{
        bipolar length = rover_read_file(path);

        if (length < 0)
        {
                string_format(log_error, "moonwater rover: %s: cannot be read (error %b)\n", path, (b32)-length);
                log_flush();
                return false;
        }
        if (!rover_compile(&rover_work, rover_source, (positive)length))
        {
                if (rover_work.line)
                        string_format(log_error, "%s:%p: %s\n", path, (positive)rover_work.line, rover_work.why);
                else
                        string_format(log_error, "%s: %s\n", path, rover_work.why);
                log_flush();
                return false;
        }
        return true;
}

static b32 rover_verb_test(string_address address_to arguments, positive count)
{
        struct rover_table table;
        struct rover_refusal refusal;
        struct rover_run runs[ROVER_RULES_MOST];
        struct rover_event event;
        struct rover_hit hits[64];
        const p8 address_to pages[ROVER_PAGES] = {(const p8 address_to)rover_page_words[0],
                                                   (const p8 address_to)rover_page_words[1],
                                                   (const p8 address_to)rover_page_words[2]};
        p8 address_to writable[ROVER_PAGES] = {(p8 address_to)rover_page_words[0], (p8 address_to)rover_page_words[1],
                                               (p8 address_to)rover_page_words[2]};
        static const char address_to const places[ROVER_PLACES] = {"kernel", "runner", "desktop"};
        positive at = 4, used = 0, fired = 0;
        const struct rover_event_row address_to row;

        if (count < 5)
                return rover_usage();
        if (!rover_verb_compile(arguments[3]) || !rover_check(rover_built(&rover_work), rover_work.size, &table, &refusal))
                return 1;
        memory_zero(runs, sizeof(runs));
        memory_zero(&event, sizeof(event));
        memory_zero(rover_page_words, sizeof(rover_page_words));
        event.realtime = 4 * 86400 + 12 * 3600; // a Monday at noon
        event.when = 1000000000;
        //      The event's words, then its fields, then the states.
        rover_work.words = 0;
        while (at < count && !string_first_of(arguments[at], '=') && rover_work.words < ROVER_LINE_WORDS)
        {
                rover_work.word[rover_work.words].text = (const p8 address_to)arguments[at];
                rover_work.word[rover_work.words].length = (p32)string_length(arguments[at]);
                rover_work.word[rover_work.words].quoted = 0;
                rover_work.words++;
                at++;
        }
        row = rover_event_named(&rover_work, 0, &used);
        if (!row)
        {
                string_format(log_error, "moonwater rover: %s is no event\n", arguments[4]);
                log_flush();
                return 2;
        }
        event.id = row->id;
        rover_work.why_used = 0;
        for (positive field = used; field < rover_work.words; field++)
        {
                p8 type = row->field_type[field - used];
                b64 number = 0;

                if (field - used >= row->fields || !rover_literal(&rover_work, &rover_work.word[field], type, &number))
                {
                        string_format(log_error, "moonwater rover: %s: %s\n", arguments[4 + field],
                                      rover_work.why_used ? rover_work.why : "is more than the event's fields");
                        log_flush();
                        return 2;
                }
                event.field[field - used].number = number;
                event.field[field - used].text = rover_work.word[field].text;
                event.field[field - used].length = rover_work.word[field].length;
                event.count = (p8)(field - used + 1);
        }
        for (; at < count; at++)
                if (!rover_assign(&rover_work, writable, (const p8 address_to)arguments[at], string_length(arguments[at]),
                                  &event))
                {
                        string_format(log_error, "moonwater rover: %s: %s\n", arguments[at],
                                      rover_work.why_used ? rover_work.why : "is not STATE=VALUE");
                        log_flush();
                        return 2;
                }
        for (p8 place = 0; place < ROVER_PLACES; place++)
        {
                positive made = rover_fire(&table, runs, pages, &event, place, hits, array_count(hits));
                struct rover_event edge = event;

                edge.id = 0;
                edge.count = 0;
                made += rover_edges(&table, runs, pages, ~0ull, &edge, place, hits + made, array_count(hits) - made);
                for (positive k = 0; k < made; k++)
                {
                        const struct rover_rule address_to rule = table.rule + hits[k].rule;
                        struct rover_text line = {rover_out_bytes, 0, sizeof(rover_out_bytes), false};

                        rover_puts(&line, "fire rule=");
                        rover_pool_put(&line, &table, rule->name);
                        rover_puts(&line, " place=");
                        rover_puts(&line, places[place]);
                        rover_puts(&line, hits[k].otherwise ? " branch=else\n" : " branch=then\n");
                        for (positive a = 0; a < hits[k].count; a++)
                        {
                                rover_puts(&line, "\t");
                                rover_action_put(&line, &table, rule->event ? rover_event_find(rule->event) : null,
                                                 (positive)hits[k].action + a);
                                rover_puts(&line, "\n");
                        }
                        log(line.bytes, line.used);
                        fired++;
                }
        }
        if (!fired)
                string_format(log, "nothing fires\n");
        log_flush();
        return 0;
}

static b32 rover_verb(string_address address_to arguments, positive count)
{
        string_address what = count > 2 ? arguments[2] : null;
        struct rover_table table;
        struct rover_refusal refusal;
        struct rover_text text = {rover_out_bytes, 0, sizeof(rover_out_bytes), false};

        if (!what)
                return rover_usage();
        if (string_equals(what, "schema"))
        {
                log((address_any)rover_schema_text, sizeof(rover_schema_text) - 1);
                log_flush();
                return 0;
        }
        if (string_equals(what, "check"))
        {
                b32 status = 0;

                if (count < 4)
                        return rover_usage();
                for (positive at = 3; at < count; at++)
                        if (rover_verb_compile(arguments[at]))
                                string_format(log, "%s: ok, %p rules, %p bytes\n", arguments[at],
                                              (positive)rover_work.rules, rover_work.size);
                        else
                                status = 1;
                log_flush();
                return status;
        }
        if (string_equals(what, "test"))
                return rover_verb_test(arguments, count);
        if (!string_equals(what, "print") && !string_equals(what, "show") && !string_equals(what, "compile"))
                return rover_usage();
        if (count != (string_equals(what, "compile") ? 5u : 4u))
                return rover_usage();
        if (!rover_verb_compile(arguments[3]))
                return 1;
        if (!rover_check(rover_built(&rover_work), rover_work.size, &table, &refusal))
                return 1;
        if (string_equals(what, "compile"))
        {
                bipolar handle = system_open_output_at(AT_FDCWD, arguments[4], true, 0644);

                if (handle < 0 || system_write_all((positive)handle, rover_built(&rover_work), rover_work.size) !=
                                      rover_work.size)
                {
                        if (handle >= 0)
                                system_close(handle);
                        string_format(log_error, "moonwater rover: %s: cannot be written\n", arguments[4]);
                        log_flush();
                        return 1;
                }
                system_close(handle);
                return 0;
        }
        if (string_equals(what, "print"))
                rover_print(&text, &table);
        else
                rover_graph(&text, &table);
        log(text.bytes, text.used);
        log_flush();
        return text.full ? 1 : 0;
}

#endif /* MOONWATER_CLI_ROVER */

#endif /* ROVER_COMPILE_INCLUDED */
